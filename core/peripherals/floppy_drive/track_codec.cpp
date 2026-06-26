/**
 * @file track_codec.cpp
 * @brief TrackCodec – IBM-Track (FM/MFM) aufbauen/parsen und CRC-Primitive.
 *
 * TrackCodec::crc16 IST CRC-16-CCITT (Poly 0x1021) — die ror-basierte Implementierung
 * (byte-genau zur Lader-Routine sub_0407) ist äquivalent zum Standard-CCITT.  Es gibt
 * KEINE „Robotron-Spezial-CRC"; die echten A5120-Disks sind Standard-IBM-MFM.  Die
 * vermeintlichen „Sonderseeds" sind nur CCITT-Zwischenstände:
 *   crc16([A1,A1,A1],     3, 0xFF,0xFF) == 0xCDB4
 *   crc16([A1,A1,A1,FB],  4, 0xFF,0xFF) == 0xE295   (Seed der MFM-Daten-CRC, MIT A1)
 *   crc16([FB],           1, 0xFF,0xFF) == 0xBF84   (FM-Daten-CRC, OHNE A1)
 * buildTrack startet die komplette CRC daher immer mit [A1,A1,A1,FE/FB, ...] (MFM) bzw.
 * [FE/FB, ...] (FM) und Seed 0xFF,0xFF — exakt die natürliche IBM-FM-vs-MFM-Differenz.
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
            push(0xA1);                     // Standard-IBM-MFM: 3× A1-Sync (fehlendes
            push(0xA1);                     // Clock-Bit) VOR dem Mark-Byte. Stimmt mit
            push(0xA1);                     // der CRC-Annahme {A1,A1,A1,FE,…} überein.
            pushMark(0xFE, MarkType::Id);   // Mark-Byte FE trägt die Id-Marke

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
            push(0xA1);                     // 3× A1-Sync vor dem Data-Mark-Byte (wie IDAM)
            push(0xA1);
            push(0xA1);
            pushMark(0xFB, MarkType::Data); // Mark-Byte FB trägt die Data-Marke

            // Standard-IBM-MFM-Daten-CRC (CRC-16-CCITT) über [A1,A1,A1,FB] + Daten,
            // Seed 0xFFFF — exakt wie die echten A5120-Disks (disks/cpadisk_*, z.B.
            // Sektor 0 = 0x233D) und wie der Lader im MFM-Modus ([03FD] bit1=0,
            // Seed 0xE295 = crc16([A1A1A1 FB],0xFFFF) über die Datenbytes).
            std::vector<uint8_t> dataCrcIn = {0xA1, 0xA1, 0xA1, 0xFB};
            dataCrcIn.insert(dataCrcIn.end(), sec.data.begin(), sec.data.end());
            uint16_t dataCrc = crc16(dataCrcIn.data(), dataCrcIn.size(), 0xFF, 0xFF);

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
                // Standard-IBM-MFM-Daten-CRC (CCITT) über [A1,A1,A1,FB] + Daten,
                // Seed 0xFFFF — identisch zu buildTrack (siehe Kommentar dort).
                std::vector<uint8_t> dataCrcIn = {0xA1, 0xA1, 0xA1, 0xFB};
                dataCrcIn.insert(dataCrcIn.end(), data.begin(), data.end());
                uint16_t calc = crc16(dataCrcIn.data(), dataCrcIn.size(), 0xFF, 0xFF);
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

// ─── romReadResyncTarget (Resync-Offset des MK/MK1-Strobes) ────────────────────

/**
 * Zielposition des Lesekopfs nach einem MK/MK1-Resync-Strobe.  Der Marken-FF erkennt die
 * nächste Adressmarke (FE/FB) und rückt davor, sodass der Lese-Stream danach
 * `[A1×4][FE/FB]` (MFM, markPos-4) bzw. `[sync][FE/FB]` (FM, markPos-1) liefert.  Diese
 * Ausrichtung passt für ROM-Boot-Read (1 Wegwerf-Byte + Vergleich) UND SYL-Lader
 * (skip-A1-Schleife).  Der Lese-Stream stammt aus @ref buildFaithfulReadTrack.
 */
size_t romReadResyncTarget(const TrackImage& track, size_t fromPos, Encoding readEnc) {
    if (track.bytes.empty()) return SIZE_MAX;
    const size_t m = track.nextMark(fromPos);
    if (m == SIZE_MAX) return SIZE_MAX;

    const size_t backoff = (readEnc == Encoding::MFM) ? 4 : 1;
    const size_t sz = track.bytes.size();
    return (m >= backoff) ? (m - backoff) : (sz + m - backoff);
}

// ─── buildFaithfulReadTrack ─────────────────────────────────────────────────────

/**
 * Treuer FM/MFM-Lese-Stream für den K5122-Boot-Lesepfad — siehe track_codec.h für die
 * Begründung der 4×A1-Sync-Länge (gemeinsamer Modus für ROM-Boot-Read und SYL-Lader).
 *
 * Layout je Sektor (FM: kein A1-Sync, Marke FE/FB direkt; MFM: 4×A1 vor FE/FB):
 *   [12×00 sync][A1×4][FE][cyl][head][id][sc][idcrc][gap][12×00][A1×4][FB][data][crc][gap]
 * marks[] liegt auf dem FE/FB-Byte (wie buildTrack); CRC ist Standard-IBM-CCITT über die
 * 3×A1-Spanne (identisch zu buildTrack/parseTrack).
 */
TrackImage buildFaithfulReadTrack(const std::vector<LogicalSector>& sectors, Encoding enc) {
    TrackImage t;
    t.encoding = enc;
    t.bitcells = 0;

    auto push = [&](uint8_t byte) {
        t.bytes.push_back(byte);
        t.marks.push_back(MarkType::None);
    };
    auto pushMark = [&](uint8_t byte, MarkType mt) {
        t.bytes.push_back(byte);
        t.marks.push_back(mt);
    };
    auto fill = [&](uint8_t byte, size_t count) { for (size_t i = 0; i < count; ++i) push(byte); };

    const bool isMfm = (enc == Encoding::MFM);
    const size_t kSync = isMfm ? 12u : 6u;
    const size_t kReadA1 = 4u;          // 4×A1 = der für ROM UND SYL gemeinsame Modus

    for (const auto& sec : sectors) {
        const uint8_t sc = sizeCode(sec.size);

        // ── IDAM ──────────────────────────────────────────────────────────────
        fill(0x00, kSync);
        if (isMfm) fill(0xA1, kReadA1);
        pushMark(0xFE, MarkType::Id);
        // ID-CRC: Standard-IBM-MFM über [A1,A1,A1,FE,…] bzw. FM über [FE,…]
        uint16_t idCrc;
        if (isMfm) {
            const uint8_t in[] = {0xA1, 0xA1, 0xA1, 0xFE, sec.cyl, sec.head, sec.id, sc};
            idCrc = crc16(in, sizeof(in), 0xFF, 0xFF);
        } else {
            const uint8_t in[] = {0xFE, sec.cyl, sec.head, sec.id, sc};
            idCrc = crc16Ccitt(in, sizeof(in));
        }
        push(sec.cyl); push(sec.head); push(sec.id); push(sc);
        push(static_cast<uint8_t>(idCrc >> 8));
        push(static_cast<uint8_t>(idCrc & 0xFF));

        fill(0x4E, (sec.size <= 128u) ? 18u : 27u);

        // ── DAM ───────────────────────────────────────────────────────────────
        fill(0x00, kSync);
        if (isMfm) fill(0xA1, kReadA1);
        pushMark(0xFB, MarkType::Data);
        uint16_t dataCrc;
        if (isMfm) {
            std::vector<uint8_t> in = {0xA1, 0xA1, 0xA1, 0xFB};
            in.insert(in.end(), sec.data.begin(), sec.data.end());
            dataCrc = crc16(in.data(), in.size(), 0xFF, 0xFF);
        } else {
            std::vector<uint8_t> in; in.reserve(1 + sec.data.size());
            in.push_back(0xFB);
            in.insert(in.end(), sec.data.begin(), sec.data.end());
            dataCrc = crc16Ccitt(in.data(), in.size());
        }
        for (uint8_t b : sec.data) push(b);
        push(static_cast<uint8_t>(dataCrc >> 8));
        push(static_cast<uint8_t>(dataCrc & 0xFF));

        fill(0x4E, 8u);
    }
    return t;
}

}  // namespace TrackCodec
