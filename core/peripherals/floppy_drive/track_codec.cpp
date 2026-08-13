/**
 * @file track_codec.cpp
 * @brief TrackCodec – IBM-Track (FM/MFM) aufbauen/parsen und CRC-Primitive.
 *
 * TrackCodec::crc16 IST die Standard-IBM-CRC-16-CCITT (Poly 0x1021) — die ror-basierte
 * Implementierung (byte-genau zur Lader-Routine sub_0407) ist äquivalent zum Standard-CCITT.
 * Die echten A5120-Disks sind Standard-IBM-MFM.  buildTrack berechnet die komplette CRC immer
 * über [A1,A1,A1,FE/FB, ...] (MFM) bzw. [FE/FB, ...] (FM) mit Seed 0xFF,0xFF — die natürliche
 * IBM-FM-vs-MFM-Differenz (FM ohne, MFM mit A1-Präambel im CRC-Bereich).
 *
 * @see core/peripherals/floppy_drive/track_codec.h
 * @see doc/design/07_k5122_afs.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/track_codec.h"
#include <algorithm>
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

        // Nachspann wie auf dem Medium (LogicalSector::tail) — symmetrisch zu
        // buildFaithfulReadTrack.  Bei einer Standard-IBM-Spur ist tail schlicht
        // 8× Gap-Fuellbyte, das Ergebnis also bitgleich zum frueheren
        // fill(gap_fill, gap3).  Fremdformate behalten so ihren Sektorkontroll-
        // block: **UDOS** legt hinter der Daten-CRC vier Zeigerbytes ab; ohne
        // diese Ausgabe verlor JEDER Schreibzugriff die Verkettung ALLER Sektoren
        // der Spur (commitWriteField baut die ganze Spur neu) → „POINTER CHECK
        // ERROR CA".  Sektoren ohne tail (frisch erzeugt: DiskImage::create,
        // Formatierstrom) fallen auf das Gap-Fuellbyte zurueck.
        const size_t tail_n = std::min<size_t>(kSectorTailBytes, gaps.gap3);
        for (size_t i = 0; i < tail_n; ++i)
            push(i < sec.tail.size() ? sec.tail[i] : gaps.gap_fill);
        fill(gaps.gap_fill, gaps.gap3 - tail_n);
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

        // Das Größenfeld der IBM-Adressmarke ist 2 Bit breit (0..3 = 128..1024 B).
        // Vom MEDIUM gelesene Bytes können beliebige Werte tragen — bei einer
        // gestörten Spur (halb formatiert, Schreibabbruch) steht dort Müll.  Ohne
        // Maske ergäbe das eine unmögliche Sektorgröße, an der buildTrack später
        // mit std::invalid_argument abbricht und den ganzen Emulator mitnimmt;
        // ab Schiebeweiten ≥ 32 wäre es sogar undefiniertes Verhalten.
        // Die K5122 maskiert an ihren beiden Auswertestellen längst genauso
        // (parseFormatStream, beginWriteField) — hier wird nur nachgezogen.
        // Für die CRC zählt weiterhin das ROHE Byte (siehe crcIn oben), sodass
        // ein verfälschtes Größenfeld korrekt als CRC-Fehler auffällt.
        const uint16_t secSize = static_cast<uint16_t>(128u << (sizeCode & 0x03));

        // Nächste Data-Marke nach dem ID-Feld suchen
        size_t dataPos = SIZE_MAX;
        for (size_t i = idPos + 1; i < n; ++i) {
            if (track.marks[i] == MarkType::Data) { dataPos = i; break; }
            // Stop, wenn eine weitere Id-Marke kommt (kein Data-Feld für diesen Sektor)
            if (track.marks[i] == MarkType::Id)   break;
        }

        bool data_crc_ok = false;
        std::vector<uint8_t> data;
        std::vector<uint8_t> tail;   // Bytes hinter der Daten-CRC (s. LogicalSector::tail)

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

            // Sektor-Nachspann mitnehmen: die naechsten kSectorTailBytes Bytes so, wie
            // sie auf dem Medium stehen.  Standard-IBM = Gap (0x4E/0xFF, folgenlos);
            // UDOS legt hier seinen Sektorkontrollblock ab (s. LogicalSector::tail).
            const size_t tailStart = dataPos + 1 + secSize + 2;
            const size_t tailEnd   = std::min(tailStart + kSectorTailBytes, n);
            tail.assign(track.bytes.begin() + static_cast<long>(tailStart),
                        track.bytes.begin() + static_cast<long>(tailEnd));
        }

        LogicalSector ls;
        ls.cyl          = cyl;
        ls.head         = head;
        ls.id           = id;
        ls.size         = secSize;
        ls.data         = std::move(data);
        ls.id_crc_ok    = id_crc_ok;
        ls.data_crc_ok  = data_crc_ok;
        ls.tail         = std::move(tail);

        // Lage auf der Spur — die Grundlage jeder Darstellung „wo liegt der Sektor".
        // sync_pos zeigt auf den ANFANG der Sync-Gruppe, nicht auf die A1-Bytes:
        // die 00-Bytes davor gehoeren zum Sektor (der Formatierer schreibt sie mit),
        // und nur so decken sich „wo faengt der Sektor an" und „wo setzt ein neuer
        // Sektor auf" (@ref newSectorPosition).  Rueckwaerts gelaufen wird hoechstens
        // eine doppelte Sync-Laenge, damit ein Datenfeld, das auf Nullbytes endet,
        // nicht verschluckt wird.
        ls.id_pos   = idPos;
        {
            const size_t a1 = (isMfm && idPos >= 3) ? idPos - 3 : idPos;
            const size_t grenze = 2u * gapsFor(track.encoding).sync_len;
            size_t p = a1;
            while (p > 0 && (a1 - p) < grenze && track.bytes[p - 1] == 0x00) --p;
            ls.sync_pos = p;
        }
        ls.id_crc   = static_cast<uint16_t>((crcHi << 8) | crcLo);
        if (dataPos != SIZE_MAX) {
            ls.data_pos = dataPos;
            ls.deleted  = (track.bytes[dataPos] == 0xF8);
            if (dataPos + 1 + secSize + 2 <= n) {
                ls.end_pos  = dataPos + 1 + secSize + 2;
                ls.data_crc = static_cast<uint16_t>(
                    (track.bytes[dataPos + 1 + secSize] << 8)
                    | track.bytes[dataPos + 1 + secSize + 1]);
            }
        }
        result.push_back(std::move(ls));

        // Weiter hinter dem aktuellen IDAM
        pos = idPos + 1;
    }
    return result;
}

// ─── writeSector (Einzelsektor an Ort und Stelle ersetzen) ────────────────────

/**
 * Zweiphasig: erst alles pruefen (Sektor finden, Laengen, Nachspann-Platz), dann in einem
 * Zug schreiben.  Nur so gilt die Zusage „false = keine Aenderung" auch dann, wenn der
 * Fehler erst spaet auffaellt.
 */
/// @brief Gemeinsamer Kern von @ref writeSector und @ref writeSectorAt.
///        @p crc == nullptr = CRC neu rechnen, sonst genau diesen Wert schreiben.
static bool schreibeDatenfeld(TrackImage& track, size_t dataPos, uint16_t secSize,
                              const std::vector<uint8_t>& data,
                              const std::vector<uint8_t>& tail,
                              const uint16_t* crc_woertlich) {
    const size_t n = track.bytes.size();
    const bool isMfm = (track.encoding == Encoding::MFM);

    if (dataPos == SIZE_MAX)                      return false;   // ID fehlt / kein Datenfeld
    if (data.size() != secSize)                   return false;   // Laenge passt nicht
    if (dataPos + 1 + secSize + 2 > n)            return false;   // Datenfeld ragt heraus

    const size_t tailStart = dataPos + 1 + secSize + 2;
    const size_t tailLen   = std::min(tail.size(), kSectorTailBytes);
    if (tailLen > 0) {
        if (tailStart + tailLen > n) return false;
        // Ein Nachspann darf niemals in die naechste Adressmarke laufen — sonst waere die
        // Spur danach kaputt, und zwar unbemerkt bis zum naechsten Lesen.
        for (size_t i = tailStart; i < tailStart + tailLen; ++i)
            if (track.marks[i] != MarkType::None) return false;
    }

    // ── Phase 2: schreiben ───────────────────────────────────────────────────
    std::copy(data.begin(), data.end(), track.bytes.begin() + static_cast<long>(dataPos + 1));

    // CRC ueber [A1 A1 A1 <Marke>] + Daten (MFM) bzw. [<Marke>] + Daten (FM).  Die Marke wird
    // aus der Spur gelesen statt fest 0xFB anzunehmen: geloeschte Datenfelder (0xF8) bleiben
    // damit geloescht und behalten eine gueltige CRC.
    const uint8_t markByte = track.bytes[dataPos];
    std::vector<uint8_t> crcIn;
    crcIn.reserve(4 + secSize);
    if (isMfm) { crcIn.push_back(0xA1); crcIn.push_back(0xA1); crcIn.push_back(0xA1); }
    crcIn.push_back(markByte);
    crcIn.insert(crcIn.end(), data.begin(), data.end());

    const uint16_t crc = crc_woertlich ? *crc_woertlich
                       : (isMfm ? crc16(crcIn.data(), crcIn.size(), 0xFF, 0xFF)
                                : crc16Ccitt(crcIn.data(), crcIn.size()));
    track.bytes[dataPos + 1 + secSize]     = static_cast<uint8_t>(crc >> 8);
    track.bytes[dataPos + 1 + secSize + 1] = static_cast<uint8_t>(crc & 0xFF);

    for (size_t i = 0; i < tailLen; ++i) track.bytes[tailStart + i] = tail[i];

    return true;
}

bool writeSector(TrackImage& track, uint8_t sector_id,
                 const std::vector<uint8_t>& data,
                 const std::vector<uint8_t>& tail) {
    const size_t n = track.bytes.size();
    if (n == 0 || track.marks.size() != n) return false;

    size_t   dataPos = SIZE_MAX;
    uint16_t secSize = 0;

    for (size_t idPos = 0; idPos < n; ++idPos) {
        if (track.marks[idPos] != MarkType::Id) continue;
        if (idPos + 1 + 4 + 2 > n) break;                 // ID-Feld unvollstaendig
        if (track.bytes[idPos + 3] != sector_id) continue;

        // Groessencode wie in parseTrack maskieren (gestoerte Spur → Muellwert).
        secSize = static_cast<uint16_t>(128u << (track.bytes[idPos + 4] & 0x03));

        for (size_t i = idPos + 1; i < n; ++i) {
            if (track.marks[i] == MarkType::Data) { dataPos = i; break; }
            if (track.marks[i] == MarkType::Id)   break;   // Sektor ohne Datenfeld
        }
        break;
    }
    return schreibeDatenfeld(track, dataPos, secSize, data, tail, nullptr);
}

/**
 * Ueber die LAUFENDE NUMMER statt ueber die Sektor-ID: eine Spur darf dieselbe ID
 * mehrfach tragen (fehlerhaft formatiert, Kopierschutz), und ein Sektoreditor muss
 * genau den Sektor treffen, den der Anwender angeklickt hat.  Die Nummerierung ist
 * die von @ref parseTrack — Spurreihenfolge ab dem Index.
 */
bool writeSectorAt(TrackImage& track, size_t index,
                   const std::vector<uint8_t>& data,
                   const std::vector<uint8_t>& tail,
                   const uint16_t* crc_woertlich) {
    const std::vector<LogicalSector> sektoren = parseTrack(track);
    if (index >= sektoren.size()) return false;
    const LogicalSector& s = sektoren[index];
    return schreibeDatenfeld(track, s.data_pos, s.size, data, tail, crc_woertlich);
}

bool sectorDataCrc(const TrackImage& track, size_t index,
                   const std::vector<uint8_t>& data, uint16_t& out) {
    const std::vector<LogicalSector> sektoren = parseTrack(track);
    if (index >= sektoren.size()) return false;
    const LogicalSector& s = sektoren[index];
    if (s.data_pos == SIZE_MAX || data.size() != s.size) return false;

    const bool isMfm = (track.encoding == Encoding::MFM);
    std::vector<uint8_t> crcIn;
    crcIn.reserve(4 + data.size());
    if (isMfm) { crcIn.push_back(0xA1); crcIn.push_back(0xA1); crcIn.push_back(0xA1); }
    crcIn.push_back(track.bytes[s.data_pos]);      // 0xFB bzw. 0xF8 (geloescht)
    crcIn.insert(crcIn.end(), data.begin(), data.end());

    out = isMfm ? crc16(crcIn.data(), crcIn.size(), 0xFF, 0xFF)
                : crc16Ccitt(crcIn.data(), crcIn.size());
    return true;
}

// ─── Sektor anlegen und loeschen (Sektoreditor, §19.4) ────────────────────────

size_t newSectorLength(Encoding enc, uint16_t size, uint16_t tail_bytes) {
    const GapParams g = gapsFor(enc);
    const size_t a1 = (enc == Encoding::MFM) ? 3 : 0;
    //  Sync A1×n FE [c h id sc] crc  gap2  Sync A1×n FB [data] crc  tail
    return g.sync_len + a1 + 1 + 4 + 2
         + g.gap2
         + g.sync_len + a1 + 1 + size + 2
         + tail_bytes;
}

size_t newSectorPosition(const TrackImage& track, uint8_t id, uint16_t gap_before) {
    size_t hinter = 0;                       // Vorgabe: direkt hinter dem Index
    int    beste  = -1;                      // groesste vorhandene ID kleiner als `id`

    for (const LogicalSector& s : parseTrack(track)) {
        if (s.id >= id) continue;
        if (static_cast<int>(s.id) <= beste) continue;
        beste = s.id;
        // Ende des Sektors: hinter der Daten-CRC, sonst hinter dem ID-Feld.
        hinter = (s.end_pos != SIZE_MAX) ? s.end_pos
               : (s.id_pos != SIZE_MAX ? s.id_pos + 1 + 4 + 2 : 0);
    }
    return hinter + gap_before;
}

bool createSector(TrackImage& track, const NewSectorSpec& spec, Encoding enc,
                  std::string* warum) {
    auto nein = [&](const std::string& text) {
        if (warum) *warum = text;
        return false;
    };
    const size_t n = track.bytes.size();
    if (n == 0 || track.marks.size() != n)
        return nein("Diese Spur gibt es auf dem Datentraeger nicht.");
    if (spec.size != 128 && spec.size != 256 && spec.size != 512 && spec.size != 1024)
        return nein("Die Sektorgroesse muss 128, 256, 512 oder 1024 Byte sein.");

    // FM und MFM lassen sich in einer Spur nicht mischen — das Verfahren haengt am
    // Bit-Codec der ganzen Spur.  Nur eine markenlose Spur darf es noch festlegen.
    const bool leer = std::none_of(track.marks.begin(), track.marks.end(),
                                   [](MarkType m) { return m != MarkType::None; });
    if (!leer && enc != track.encoding)
        return nein(std::string("Diese Spur ist ")
                    + (track.encoding == Encoding::MFM ? "MFM" : "FM")
                    + "-kodiert; FM und MFM lassen sich in einer Spur nicht mischen. "
                      "Erst alle Sektoren der Spur loeschen.");

    const size_t ab   = newSectorPosition(track, spec.id, spec.gap_before);
    const size_t len  = newSectorLength(enc, spec.size, spec.tail_bytes);
    if (ab + len > n)
        return nein("Der Sektor passt nicht mehr auf die Spur: er braucht "
                    + std::to_string(len) + " Byte ab Position " + std::to_string(ab)
                    + ", die Spur ist " + std::to_string(n) + " Byte lang. "
                      "Kleinerer Gap oder kleinere Sektorgroesse.");

    // ── Schreiben ────────────────────────────────────────────────────────────
    const GapParams g = gapsFor(enc);
    const bool isMfm = (enc == Encoding::MFM);
    size_t p = ab;
    auto setze = [&](uint8_t b, MarkType m = MarkType::None) {
        track.bytes[p] = b;
        track.marks[p] = m;
        ++p;
    };
    auto fuelle = [&](uint8_t b, size_t k) { for (size_t i = 0; i < k; ++i) setze(b); };

    const uint8_t sc = sizeCode(spec.size);

    fuelle(0x00, g.sync_len);
    if (isMfm) { setze(0xA1); setze(0xA1); setze(0xA1); }
    setze(0xFE, MarkType::Id);

    uint16_t idCrc;
    if (isMfm) {
        const uint8_t vor[] = {0xA1, 0xA1, 0xA1, 0xFE, spec.cyl, spec.head, spec.id, sc};
        idCrc = crc16(vor, sizeof(vor), 0xFF, 0xFF);
    } else {
        const uint8_t vor[] = {0xFE, spec.cyl, spec.head, spec.id, sc};
        idCrc = crc16Ccitt(vor, sizeof(vor));
    }
    setze(spec.cyl); setze(spec.head); setze(spec.id); setze(sc);
    setze(static_cast<uint8_t>(idCrc >> 8));
    setze(static_cast<uint8_t>(idCrc & 0xFF));

    fuelle(g.gap_fill, g.gap2);

    fuelle(0x00, g.sync_len);
    if (isMfm) { setze(0xA1); setze(0xA1); setze(0xA1); }
    setze(0xFB, MarkType::Data);

    std::vector<uint8_t> crcIn;
    crcIn.reserve(4 + spec.size);
    if (isMfm) { crcIn.push_back(0xA1); crcIn.push_back(0xA1); crcIn.push_back(0xA1); }
    crcIn.push_back(0xFB);
    crcIn.insert(crcIn.end(), spec.size, spec.fill);
    const uint16_t dataCrc = isMfm ? crc16(crcIn.data(), crcIn.size(), 0xFF, 0xFF)
                                   : crc16Ccitt(crcIn.data(), crcIn.size());

    fuelle(spec.fill, spec.size);
    setze(static_cast<uint8_t>(dataCrc >> 8));
    setze(static_cast<uint8_t>(dataCrc & 0xFF));

    // Der Kontrollblock beginnt als „Kettenende" (FF FF FF FF) — nicht als Gap:
    // vier Fuellbytes saehen aus wie ein Zeiger auf Spur 0x4E.
    fuelle(0xFF, spec.tail_bytes);

    if (leer) track.encoding = enc;
    return true;
}

bool eraseSectorAt(TrackImage& track, size_t index, uint16_t tail_bytes) {
    const std::vector<LogicalSector> sektoren = parseTrack(track);
    if (index >= sektoren.size()) return false;
    const LogicalSector& s = sektoren[index];
    if (s.sync_pos == SIZE_MAX) return false;

    const size_t bis = ((s.end_pos != SIZE_MAX) ? s.end_pos
                                                : s.id_pos + 1 + 4 + 2) + tail_bytes;
    const uint8_t fuell = gapsFor(track.encoding).gap_fill;
    for (size_t i = s.sync_pos; i < std::min(bis, track.bytes.size()); ++i) {
        track.bytes[i] = fuell;
        track.marks[i] = MarkType::None;
    }
    return true;
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

        // Nachspann: die Bytes hinter der Daten-CRC so uebernehmen, wie sie auf dem
        // Medium standen (LogicalSector::tail, genau kSectorTailBytes = 8 Bytes).  Bei
        // einer Standard-IBM-Spur ist das 8x Gap 0x4E — bitgleich zum bisherigen
        // fill(0x4E, 8).  Fremdformate behalten so ihren Sektorkontrollblock: UDOS
        // liest direkt hinter der Daten-CRC 4 Zeigerbytes weiter (s. tail).
        for (size_t i = 0; i < kSectorTailBytes; ++i)
            push(i < sec.tail.size() ? sec.tail[i] : 0x4E);
    }
    return t;
}

}  // namespace TrackCodec
