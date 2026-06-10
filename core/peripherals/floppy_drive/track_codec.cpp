/**
 * @file track_codec.cpp
 * @brief TrackCodec – IBM-Track (FM/MFM) aufbauen/parsen und CRC-Primitive.
 *
 * CRC-Vereinheitlichung: loaderCrc16 (aus core/cards/k5122/k5122.cpp) byte-genau
 * als TrackCodec::crc16 übernommen.  Die frühere Zwei-Seed-Logik der alten K5122
 * (0xBF84 über reine Datenbytes / 0xCDB4 über [0xFB]+Daten) entsteht daraus
 * automatisch:
 *   crc16([A1,A1,A1],     3, 0xFF,0xFF) == 0xCDB4
 *   crc16([A1,A1,A1,FB], 4, 0xFF,0xFF) == 0xBF84
 * buildTrack startet daher die komplette CRC immer mit [A1,A1,A1,FE/FB, ...] und
 * Seed 0xFF,0xFF.
 *
 * @see core/peripherals/floppy_drive/track_codec.h
 * @see doc/refactoring_floppy_emulator.md §4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/track_codec.h"
#include <cassert>
#include <climits>
#include <stdexcept>

namespace TrackCodec {

// ─── CRC ──────────────────────────────────────────────────────────────────────

/**
 * Byte-genaue Übersetzung der verifizierten Robotron-Z80-Routine sub_0407 /
 * sub_1E44 (Sekundär- und Tertiärlader).  Unverändert aus loaderCrc16 in
 * core/cards/k5122/k5122.cpp übernommen.
 */
uint16_t crc16(const uint8_t* data, size_t n, uint8_t b, uint8_t c) {
    auto ror = [](uint8_t x, int k) -> uint8_t {
        return static_cast<uint8_t>((x >> k) | (x << (8 - k)));
    };
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = data[i] ^ b;
        b = a;
        a = ror(a, 4) & 0x0F;
        a ^= b;
        b = a;
        a = ror(a, 3);
        uint8_t d = a;
        a = (a & 0x1F) ^ c;
        c = a;
        a = ror(d, 1) & 0xF0;
        a ^= c;
        c = a;
        a = (d & 0xE0) ^ b;
        b = c;
        c = a;
    }
    return static_cast<uint16_t>((b << 8) | c);
}

/**
 * Standard-CRC-16-CCITT, Polynom 0x1021, MSB-first.  Für IBM-3740-FM-Tracks.
 */
uint16_t crc16Ccitt(const uint8_t* data, size_t n, uint16_t seed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000)
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            else
                crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// ─── Gap-Parameter ────────────────────────────────────────────────────────────

GapParams gapsFor(Encoding enc) {
    GapParams g;
    if (enc == Encoding::MFM) {
        g.gap_fill  = 0x4E;
        g.sync_len  = 12;
        g.with_iam  = true;
        g.gap1      = 16;
        g.gap2      = 11;
        g.gap3      = 24;
        g.gap4a     = 16;
    } else {
        // FM (Single Density, IBM-3740-kompatibel)
        g.gap_fill  = 0xFF;
        g.sync_len  = 6;
        g.with_iam  = true;
        g.gap1      = 16;
        g.gap2      = 11;
        g.gap3      = 24;
        g.gap4a     = 16;
    }
    return g;
}

// ─── Hilfsfunktion: size_code aus Sektorgröße ──────────────────────────────

static uint8_t sizeCode(uint16_t size) {
    switch (size) {
        case 128:  return 0;
        case 256:  return 1;
        case 512:  return 2;
        case 1024: return 3;
        default:
            // Ungültige Sektorgröße — Laufzeitfehler
            throw std::invalid_argument("TrackCodec: ungültige Sektorgröße");
    }
}

// ─── buildTrack ───────────────────────────────────────────────────────────────

TrackImage buildTrack(const std::vector<LogicalSector>& sectors,
                      Encoding enc,
                      const GapParams& gaps) {
    TrackImage t;
    t.encoding = enc;

    // Hilfslambdas: Byte ohne Markierung / mit Markierung anfügen
    auto push = [&](uint8_t byte) {
        t.bytes.push_back(byte);
        t.marks.push_back(MarkType::None);
    };
    auto pushMark = [&](uint8_t byte, MarkType mt) {
        t.bytes.push_back(byte);
        t.marks.push_back(mt);
    };
    auto fill = [&](uint8_t byte, size_t count) {
        for (size_t i = 0; i < count; ++i) push(byte);
    };

    // ── Spur-Präambel (gap4a + optional IAM + gap1) ──────────────────────────
    fill(gaps.gap_fill, gaps.gap4a);

    if (gaps.with_iam) {
        if (enc == Encoding::MFM) {
            // 3×0xC2 (ohne Marke) + 0xFC (Index-Marke)
            push(0xC2);
            push(0xC2);
            push(0xC2);
            pushMark(0xFC, MarkType::Index);
        } else {
            // FM: Markenbyte allein (kein A1-Sync)
            pushMark(0xFC, MarkType::Index);
        }
    }

    fill(gaps.gap_fill, gaps.gap1);

    // ── Sektoren ──────────────────────────────────────────────────────────────
    for (const auto& sec : sectors) {
        const uint8_t sc = sizeCode(sec.size);

        if (enc == Encoding::MFM) {
            // ── MFM IDAM ──────────────────────────────────────────────────────
            fill(0x00, gaps.sync_len);      // 12×00 Sync
            push(0xA1);
            push(0xA1);
            pushMark(0xFE, MarkType::Id);   // letztes A1+Mark-Byte tragen Id-Marke

            // ID-CRC über [A1,A1,A1,FE,cyl,head,id,sc], Seed 0xFF,0xFF
            const uint8_t idam_preamble[] = {0xA1, 0xA1, 0xA1, 0xFE,
                                              sec.cyl, sec.head, sec.id, sc};
            uint16_t idCrc = crc16(idam_preamble, sizeof(idam_preamble), 0xFF, 0xFF);

            // ID-Feld
            push(sec.cyl);
            push(sec.head);
            push(sec.id);
            push(sc);
            push(static_cast<uint8_t>(idCrc >> 8));
            push(static_cast<uint8_t>(idCrc & 0xFF));

            fill(gaps.gap_fill, gaps.gap2);

            // ── MFM DAM ────────────────────────────────────────────────────────
            fill(0x00, gaps.sync_len);      // 12×00 Sync
            push(0xA1);
            push(0xA1);
            pushMark(0xFB, MarkType::Data); // Mark-Byte trägt Data-Marke

            // Daten-CRC: Robotron-Boot-Kompatibilität erfordert Seed 0xBF84 über die
            // reinen Datenbytes — crc16([A1,A1,A1,FB,data],0xFF,0xFF) ergibt einen
            // anderen Wert (0xE295 nach 4 Bytes; die Kette stimmt erst ab dem 5. Byte
            // mit dem 0xBF84-Pfad überein, NICHT ab Byte 4).  Daher: Boot-kompatibler
            // Pfad = crc16(data, 0xBF, 0x84).
            uint16_t dataCrc = crc16(sec.data.data(), sec.data.size(), 0xBF, 0x84);

            // Datenbytes
            for (uint8_t b : sec.data) push(b);
            push(static_cast<uint8_t>(dataCrc >> 8));
            push(static_cast<uint8_t>(dataCrc & 0xFF));

        } else {
            // ── FM IDAM (kein A1-Sync) ────────────────────────────────────────
            fill(0x00, gaps.sync_len);      // 6×00 Sync
            pushMark(0xFE, MarkType::Id);

            // CRC-CCITT über [FE,cyl,head,id,sc]
            const uint8_t idam_fm[] = {0xFE, sec.cyl, sec.head, sec.id, sc};
            uint16_t idCrc = crc16Ccitt(idam_fm, sizeof(idam_fm));

            push(sec.cyl);
            push(sec.head);
            push(sec.id);
            push(sc);
            push(static_cast<uint8_t>(idCrc >> 8));
            push(static_cast<uint8_t>(idCrc & 0xFF));

            fill(gaps.gap_fill, gaps.gap2);

            // ── FM DAM ────────────────────────────────────────────────────────
            fill(0x00, gaps.sync_len);      // 6×00 Sync
            pushMark(0xFB, MarkType::Data);

            // CRC-CCITT über [FB, <data>]
            std::vector<uint8_t> dataCrcInput;
            dataCrcInput.reserve(1 + sec.data.size());
            dataCrcInput.push_back(0xFB);
            dataCrcInput.insert(dataCrcInput.end(), sec.data.begin(), sec.data.end());
            uint16_t dataCrc = crc16Ccitt(dataCrcInput.data(), dataCrcInput.size());

            // Datenbytes
            for (uint8_t b : sec.data) push(b);
            push(static_cast<uint8_t>(dataCrc >> 8));
            push(static_cast<uint8_t>(dataCrc & 0xFF));
        }

        fill(gaps.gap_fill, gaps.gap3);
    }

    return t;
}

TrackImage buildTrack(const std::vector<LogicalSector>& sectors, Encoding enc) {
    return buildTrack(sectors, enc, gapsFor(enc));
}

// ─── parseTrack ───────────────────────────────────────────────────────────────

/**
 * Liefert den Byte-Offset der drei A1-Bytes VOR der Marke (MFM) bzw. den
 * Offset des Mark-Bytes selbst (FM), von dem ab die CRC berechnet wird.
 * Für MFM: marks[markPos] liegt auf dem Mark-Byte (nach drei A1-Bytes);
 *          die CRC-Berechnung beginnt beim ersten A1, also markPos - 2
 *          (zwei A1 ohne Marke stehen vor dem Mark-Byte-A1+Marke: insgesamt
 *          3 A1 => markPos-2 ist Byte #1 der Präambel).
 * Für FM:  keine A1-Sync; CRC beginnt beim Mark-Byte selbst.
 */
std::vector<LogicalSector> parseTrack(const TrackImage& track) {
    std::vector<LogicalSector> result;
    const size_t n = track.bytes.size();
    if (n == 0) return result;

    const bool isMfm = (track.encoding == Encoding::MFM);

    // Iteriere über alle Id-Marken in der Spur
    size_t pos = 0;
    while (true) {
        // Nächste IDAM-Position (Id-Marke)
        size_t idPos = SIZE_MAX;
        for (size_t i = pos; i < n; ++i) {
            if (track.marks[i] == MarkType::Id) { idPos = i; break; }
        }
        if (idPos == SIZE_MAX) break;

        // ID-Feld: cyl head id size_code (4 Bytes nach dem Mark-Byte)
        if (idPos + 1 + 4 + 2 > n) break;   // zu wenig Daten

        const uint8_t cyl       = track.bytes[idPos + 1];
        const uint8_t head      = track.bytes[idPos + 2];
        const uint8_t id        = track.bytes[idPos + 3];
        const uint8_t sizeCode  = track.bytes[idPos + 4];
        const uint8_t crcHi     = track.bytes[idPos + 5];
        const uint8_t crcLo     = track.bytes[idPos + 6];

        // ID-CRC prüfen
        bool id_crc_ok = false;
        if (isMfm) {
            // CRC-Start: 3×A1 + FE (Mark-Byte) + cyl head id sc
            // marks[idPos] ist das Mark-Byte (0xFE), davor 2×A1 ohne Marke
            // und das dritte A1 wird als Mark-Byte (idPos-0) betrachtet.
            // Tatsächliches Layout: ...A1 A1 [A1/Id] FE cyl head id sc CRC CRC...
            // Nein: das Mark-Byte IST 0xFE (marks[idPos]=Id), die drei A1-Sync
            // liegen davor, das letzte (marks[]=None) ist das dritte A1.
            // Die CRC-Eingabe ist [A1,A1,A1,FE,cyl,head,id,sc].
            const uint8_t crcIn[] = {0xA1, 0xA1, 0xA1, 0xFE,
                                      cyl, head, id, sizeCode};
            uint16_t calc = crc16(crcIn, sizeof(crcIn), 0xFF, 0xFF);
            id_crc_ok = (calc == static_cast<uint16_t>((crcHi << 8) | crcLo));
        } else {
            // FM: CRC über [FE, cyl, head, id, sizeCode]
            const uint8_t crcIn[] = {0xFE, cyl, head, id, sizeCode};
            uint16_t calc = crc16Ccitt(crcIn, sizeof(crcIn));
            id_crc_ok = (calc == static_cast<uint16_t>((crcHi << 8) | crcLo));
        }

        const uint16_t secSize = static_cast<uint16_t>(128u << sizeCode);

        // Nächste Data-Marke nach dem ID-Feld suchen
        size_t dataPos = SIZE_MAX;
        for (size_t i = idPos + 1; i < n; ++i) {
            if (track.marks[i] == MarkType::Data) { dataPos = i; break; }
            // Stop, wenn eine weitere Id-Marke kommt (kein Data-Feld für diesen Sektor)
            if (track.marks[i] == MarkType::Id)   break;
        }

        bool data_crc_ok = false;
        std::vector<uint8_t> data;

        if (dataPos != SIZE_MAX && dataPos + 1 + secSize + 2 <= n) {
            data.assign(track.bytes.begin() + dataPos + 1,
                        track.bytes.begin() + dataPos + 1 + secSize);
            const uint8_t dCrcHi = track.bytes[dataPos + 1 + secSize];
            const uint8_t dCrcLo = track.bytes[dataPos + 1 + secSize + 1];

            if (isMfm) {
                // Daten-CRC: Boot-kompatibler Seed 0xBF84 über reine Datenbytes
                // (identisch zu buildTrack — sieh Kommentar dort).
                uint16_t calc = crc16(data.data(), data.size(), 0xBF, 0x84);
                data_crc_ok = (calc == static_cast<uint16_t>((dCrcHi << 8) | dCrcLo));
            } else {
                // FM: CRC über [FB, <data>]
                std::vector<uint8_t> crcIn;
                crcIn.reserve(1 + secSize);
                crcIn.push_back(0xFB);
                crcIn.insert(crcIn.end(), data.begin(), data.end());
                uint16_t calc = crc16Ccitt(crcIn.data(), crcIn.size());
                data_crc_ok = (calc == static_cast<uint16_t>((dCrcHi << 8) | dCrcLo));
            }
        }

        LogicalSector ls;
        ls.cyl          = cyl;
        ls.head         = head;
        ls.id           = id;
        ls.size         = secSize;
        ls.data         = std::move(data);
        ls.id_crc_ok    = id_crc_ok;
        ls.data_crc_ok  = data_crc_ok;
        result.push_back(std::move(ls));

        // Weiter hinter dem aktuellen IDAM
        pos = idPos + 1;
    }
    return result;
}

// ─── buildRobotronTrack ───────────────────────────────────────────────────────

/**
 * Robotron-A5120-Boot-Track-Layout: kein IAM, einzelner A1-Sync, Marke auf dem A1-Byte.
 *
 * Motivation: Die ZVE2-Leseroutinen der A5120-Boot-ROM (und des Sekundärladers) erwarten
 * nach einem MK/MK1-Resync-Strobe:
 *   buf[0] = A1  (das Marken-Byte, auf dem nextMark() steht)
 *   buf[1] = FE  (IDAM-Marke-Byte) bzw. 0xFB (DAM-Marke-Byte)
 *
 * Das generische buildTrack-Layout hat die Marke auf dem FE/FB-Byte (nach drei A1-Bytes),
 * was zu einem Off-by-one führt und die IDAM-Erkennung der ROM-Routine bricht.
 *
 * Hier: Marke liegt auf dem einzelnen A1-Byte; FE/FB ist das unmittelbar folgende Byte ohne
 * Marke.  parseTrack() versteht dieses Layout NICHT (es sucht die Marke auf FE/FB); diese
 * Funktion ist ausschließlich für den Vorwärts-Streaming-Pfad (ZVE2 liest, nie schreibt).
 */
TrackImage buildRobotronTrack(const std::vector<LogicalSector>& sectors) {
    TrackImage t;
    t.encoding = Encoding::MFM;
    t.bitcells = 0;

    // Hilfslambdas
    auto push = [&](uint8_t byte) {
        t.bytes.push_back(byte);
        t.marks.push_back(MarkType::None);
    };
    auto pushMark = [&](uint8_t byte, MarkType mt) {
        t.bytes.push_back(byte);
        t.marks.push_back(mt);
    };
    auto fill = [&](uint8_t byte, size_t count) {
        for (size_t i = 0; i < count; ++i) push(byte);
    };

    for (const auto& sec : sectors) {
        const uint8_t sc = sizeCode(sec.size);

        // ── IDAM: einzelner A1 mit Id-Marke ──────────────────────────────────
        pushMark(0xA1, MarkType::Id);   // Marke auf dem A1, nicht auf FE
        push(0xFE);
        push(sec.cyl);
        push(sec.head);
        push(sec.id);
        push(sc);

        // id_gap: 18 Bytes für 128B-Sektoren, 27 Bytes für größere
        const size_t id_gap = (sec.size <= 128u) ? 18u : 27u;
        fill(0x4E, id_gap);

        // ── DAM: einzelner A1 mit Data-Marke ─────────────────────────────────
        pushMark(0xA1, MarkType::Data); // Marke auf dem A1, nicht auf FB
        push(0xFB);

        // Daten
        for (uint8_t b : sec.data) push(b);

        // CRC: size<=128 → Seed 0xBF84 über Datenbytes;
        //      size> 128 → Seed 0xCDB4 über [0xFB]+Datenbytes
        uint16_t crc;
        if (sec.size <= 128u) {
            crc = crc16(sec.data.data(), sec.data.size(), 0xBF, 0x84);
        } else {
            // Temporärer Puffer [FB] + data für die CRC
            std::vector<uint8_t> tmp;
            tmp.reserve(1 + sec.data.size());
            tmp.push_back(0xFB);
            tmp.insert(tmp.end(), sec.data.begin(), sec.data.end());
            crc = crc16(tmp.data(), tmp.size(), 0xCD, 0xB4);
        }
        push(static_cast<uint8_t>(crc >> 8));
        push(static_cast<uint8_t>(crc & 0xFF));

        // 8 Bytes Gap nach dem Datenfeld
        fill(0x4E, 8u);
    }

    return t;
}

}  // namespace TrackCodec
