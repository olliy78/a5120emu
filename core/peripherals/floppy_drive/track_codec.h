/**
 * @file track_codec.h
 * @brief TrackCodec – IBM-Track (FM/MFM) aus logischen Sektoren bauen/parsen + CRC.
 *
 * Erzeugt aus logischen Sektoren einen normgerechten Track-Bytestrom (FM oder MFM)
 * mit echten Adressmarken, Gaps und CRCs und parst ihn zurück.  Wird vom
 * @ref RawSectorImage -Backend benutzt (.img → @ref TrackImage) und in Tests.
 * **Hier ist die CRC zentralisiert** — es gibt genau eine CRC-Primitive.
 *
 * CRC-Vereinheitlichung (siehe doc/refactoring_floppy_emulator.md §4.1): Die früheren
 * zwei „Startwerte" 0xBF84 (über 128 B Daten) und 0xCDB4 (über [FB]+Daten) der alten
 * K5122 sind KEIN zweites Verfahren, sondern dieselbe CRC an verschiedenen Stellen der
 * Präambel angesetzt.  @ref TrackCodec::crc16 ist die byte-genaue Übersetzung der
 * verifizierten Z80-Routinen sub_0407 (Sekundärlader) und sub_1E44 (3. Stufe), unverändert
 * aus der alten K5122 (loaderCrc16) übernommen.
 *
 * @see doc/refactoring_floppy_emulator.md §4
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

}  // namespace TrackCodec
