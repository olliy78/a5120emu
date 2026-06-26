/**
 * @file track_codec.h
 * @brief TrackCodec – IBM-Track (FM/MFM) aus logischen Sektoren bauen/parsen + CRC.
 *
 * Erzeugt aus logischen Sektoren einen normgerechten Track-Bytestrom (FM oder MFM)
 * mit echten Adressmarken, Gaps und CRCs und parst ihn zurück.  Wird vom
 * @ref RawSectorImage -Backend benutzt (.img → @ref TrackImage) und in Tests.
 * **Hier ist die CRC zentralisiert** — es gibt genau eine CRC-Primitive
 * (@ref TrackCodec::crc16, Standard-IBM-CRC-16-CCITT, Poly 0x1021).  FM vs. MFM
 * unterscheiden sich nur durch die A1-Präambel im CRC-Bereich (FM: kein A1, MFM: 3×A1),
 * nicht durch ein zweites Verfahren.
 *
 * @see doc/design/07_k5122_afs.md
 * @see doc/analyse_bootloader.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"        // Encoding, TrackImage, MarkType
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @struct LogicalSector
 * @brief Ein logischer Sektor, wie ihn das Raw-Backend liefert/erwartet.
 */
struct LogicalSector {
    uint8_t  cyl  = 0;
    uint8_t  head = 0;
    uint8_t  id   = 1;       ///< 1-basierte Sektor-ID
    uint16_t size = 128;     ///< Bytes/Sektor (128/256/512/1024)
    std::vector<uint8_t> data;
    bool     id_crc_ok   = true;   ///< von parseTrack gesetzt
    bool     data_crc_ok = true;   ///< von parseTrack gesetzt
};

/**
 * @struct GapParams
 * @brief Gap-/Sync-Parameter (Default je Verfahren via TrackCodec::gapsFor()).
 *
 * MFM: Sync 12×00 + 3×A1, Gap-Füller 0x4E.  FM: Sync 6×00, kein A1, Gap-Füller 0xFF.
 */
struct GapParams {
    uint8_t  gap1     = 16;    ///< nach IAM
    uint8_t  gap2     = 11;    ///< zwischen ID-Feld und DAM
    uint8_t  gap3     = 24;    ///< nach Datenfeld (vor nächstem IDAM)
    uint16_t gap4a    = 16;    ///< Vor-Index-Gap
    uint8_t  sync_len = 12;    ///< 00-Sync vor jeder Markengruppe (MFM 12, FM 6)
    uint8_t  gap_fill = 0x4E;  ///< MFM 0x4E, FM 0xFF
    bool     with_iam = true;
};

/**
 * @namespace TrackCodec
 * @brief Verfahrensabhängige Track-Synthese/-Analyse + CRC-Primitive.
 */
namespace TrackCodec {

/// @brief Verfahrensabhängige Default-Gaps (MFM/FM).
GapParams gapsFor(Encoding enc);

/**
 * @brief Baut eine vollständige Spur mit echten Marken und echten CRCs.
 *
 * Layout: gap4a, optional IAM, gap1, dann je Sektor
 * [Sync IDAM gap2 Sync DAM data CRC gap3], im gewählten Verfahren.
 * Das Ergebnis-TrackImage trägt @p enc als encoding-Tag und setzt @ref TrackImage::marks
 * auf den Mark-Bytes.  Sektorgröße/-anzahl frei gemischt.
 *
 * @param sectors logische Sektoren (in Reihenfolge der physischen Spurlage)
 * @param enc     Aufzeichnungsverfahren (MFM = verifizierter Boot-Pfad, FM = 8″)
 * @param gaps    Gap-Parameter (Default: gapsFor(enc))
 * @return decodiertes TrackImage (Bytes + Marken + Encoding)
 */
TrackImage buildTrack(const std::vector<LogicalSector>& sectors,
                      Encoding enc,
                      const GapParams& gaps);
TrackImage buildTrack(const std::vector<LogicalSector>& sectors, Encoding enc);

/**
 * @brief Parst eine Spur (FM oder MFM, aus track.encoding) in logische Sektoren zurück.
 *
 * Verifiziert ID- und Daten-CRC; setzt bei CRC-Fehler @ref LogicalSector::id_crc_ok
 * bzw. @ref LogicalSector::data_crc_ok auf false.  Für Zurückschreiben in .img und Tests.
 *
 * @param track decodierte Spur (mit marks[])
 * @return geparste logische Sektoren in Spurreihenfolge
 */
std::vector<LogicalSector> parseTrack(const TrackImage& track);

/**
 * @brief Resync-Zielposition für den ZRE-ROM-Lesepfad (Kalibrierung, doc/design/07 §10.5.1).
 *
 * Findet ab @p fromPos die nächste Adressmarke und liefert die Position, auf die der
 * K5122-Datenseparator re-synchronisiert, sodass der ROM-Read das Mark-Byte an der
 * erwarteten buf[]-Position findet (FM: buf[1]=FE, MFM: buf[4]=FE):
 *   - **Legacy single-A1-Layout** (Mark-Byte == 0xA1, @ref buildRobotronTrack): Resync direkt
 *     auf die Marke (kein Offset, kein Encoding-Gate) — für den aktuellen Boot-Pfad.
 *   - **Faithful-Layout** (Mark-Byte FE/FB, @ref buildTrack): nur wenn @p readEnc ==
 *     `track.encoding` (sonst feuert kein MKE → @c SIZE_MAX); Resync landet bei
 *     `markPos − (1 + nA1)` (FM: −1, MFM: −4) auf dem Sync-Byte vor der Markensequenz.
 *
 * @return Resync-Zielposition; @c SIZE_MAX = kein Resync (keine Marke ODER Encoding-Mismatch).
 */
size_t romReadResyncTarget(const TrackImage& track, size_t fromPos, Encoding readEnc);

/**
 * @brief Robotron-MFM-CRC-16 (verifizierte loaderCrc16, byte-genau).
 *
 * Byte-für-Byte-Übersetzung von sub_0407/sub_1E44.  @p seed_hi/@p seed_lo sind der
 * Startzustand (B,C).  Liefert (B<<8)|C (B = high, C = low CRC-Byte).
 *
 * @param data    Eingabebytes
 * @param n       Anzahl Bytes
 * @param seed_hi Startwert B
 * @param seed_lo Startwert C
 * @return 16-Bit-CRC (high<<8 | low)
 */
uint16_t crc16(const uint8_t* data, size_t n, uint8_t seed_hi, uint8_t seed_lo);

/**
 * @brief Standard-CRC-16-CCITT (Poly 0x1021) für IBM-3740-FM-Tracks.
 * @param data Eingabebytes
 * @param n    Anzahl Bytes
 * @param seed Startwert (Default 0xFFFF)
 * @return 16-Bit-CRC
 */
uint16_t crc16Ccitt(const uint8_t* data, size_t n, uint16_t seed = 0xFFFF);

/**
 * @brief Treuer FM/MFM-Lese-Stream für den K5122-Boot-Lesepfad (kein IAM, Marke auf FE/FB,
 *        Standard-IBM-CCITT-CRC), mit 4×A1-Sync je MFM-Feld.
 *
 * Die 4×A1-Sync-Länge ist die, die ROM-Boot-Leseroutine UND SYL-Lader gleichzeitig bedienen:
 * der ROM-Z-Pfad liest 1 Wegwerf-Byte + 3 Bytes und vergleicht buf[4] mit FE; der SYL-Lader
 * überspringt A1-Bytes per Schleife und vergleicht dann mit FE.  Mit `[A1 A1 A1 A1 FE]` nach
 * dem Resync trifft buf[4]=FE für das ROM, und die Skip-Schleife findet FE für den Lader.
 * Das 4. A1 ist reines Sync und geht NICHT in die CRC ein (Lader seeden ab der Marke), sodass
 * die echte 3×A1-Standard-CRC unverändert gilt.
 *
 * Layout je Sektor (MFM; FM: ohne A1, Marke FE/FB direkt):
 *   [12×00][A1×4][FE c h id sc][idcrc][gap][12×00][A1×4][FB][data][crc][gap]
 * marks[] liegt auf dem FE/FB-Byte (wie @ref buildTrack).
 *
 * @param sectors Logische Sektoren in Spurreihenfolge.
 * @param enc     Aufzeichnungsverfahren (MFM = 4×A1-Sync, FM = ohne A1).
 */
TrackImage buildFaithfulReadTrack(const std::vector<LogicalSector>& sectors, Encoding enc);

}  // namespace TrackCodec
