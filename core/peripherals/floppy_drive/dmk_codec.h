/**
 * @file dmk_codec.h
 * @brief DmkCodec – David Keil's Disk Image (`.dmk`) ⇄ @ref DiskMedium.
 *
 * DMK speichert je Spur den **rohen Byte-Strom** (wie ihn der Datenseparator liefert)
 * plus eine Tabelle der Adressmarken-Positionen — also fast genau unsere
 * @ref TrackImage.  Damit ist DMK wie HFE verlustfrei fuer Gaps, Anhaenge hinter der
 * Daten-CRC (UDOS-Sektorkontrollblock), Mischdichte und unformatierte Spuren, kommt
 * aber ohne Bitzellen-Ebene aus.
 *
 * Format:
 * @code
 *   Datei-Header (16 B)
 *     0      Schreibschutz: 0xFF = geschuetzt, 0x00 = frei
 *     1      Anzahl Zylinder
 *     2..3   Spurlaenge in Bytes (LE), INKLUSIVE der 128-B-IDAM-Tabelle
 *     4      Optionen: Bit4=1 einseitig, Bit6=1 reine SD-Diskette (FM NICHT verdoppelt),
 *                      Bit7=1 Dichte-Flags ignorieren
 *     5..11  reserviert (0)
 *     12..15 0x00000000 = Datei-Image (0x12345678 = echter Laufwerkszugriff, n. u.)
 *
 *   Spuren in der Reihenfolge (0,0) (0,1) (1,0) (1,1) …, je Spur:
 *     128 B  IDAM-Tabelle: 64 × u16 LE
 *              Bit15 = 1 → Sektor in MFM, 0 → FM;  Bit14 reserviert
 *              Bit0..13 = Offset des 0xFE-Bytes AB SPURANFANG (also ≥ 0x80)
 *              0x0000 = unbenutzt; Eintraege aufsteigend
 *     Rest   Spur-Bytes.  Bei FM-Spuren ist jedes Byte VERDOPPELT (ausser Bit6 gesetzt).
 * @endcode
 *
 * @see doc/design/09_floppy_drive.md §4.3
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_medium.h"
#include <string>

/**
 * @namespace DmkCodec
 * @brief Laden/Speichern von DMK-Images.
 */
namespace DmkCodec {

/**
 * @struct SourceInfo
 * @brief Eigenschaften der geladenen Datei, die die Bindung betreffen.
 */
struct SourceInfo {
    bool write_allowed = true;   ///< Header-Byte 0 (0xFF = schreibgeschuetzt)
};

/**
 * @brief Plausibilitaetspruefung: sieht @p path wie ein DMK aus?
 *
 * DMK hat **keine Magic-Zahl**, daher wird der Header geprueft: Byte 0 ∈ {0x00, 0xFF},
 * `1 ≤ n_tracks ≤ 96`, `0x80 < track_len ≤ 0x4000`, reservierte Bytes 5..11 = 0 und
 * Dateigroesse passend zu `16 + n_tracks * sides * track_len`.
 */
bool looksLikeDmk(const std::string& path);

/// @brief `.dmk` vollstaendig in @p out laden.
bool load(const std::string& path, DiskMedium& out, SourceInfo* info, std::string& err);

/// @brief @p in vollstaendig als `.dmk` schreiben (Header + IDAM-Tabellen neu berechnet).
bool save(const std::string& path, const DiskMedium& in, std::string& err);

}  // namespace DmkCodec
