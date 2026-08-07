/**
 * @file img_codec.h
 * @brief ImgCodec – rohes Sektorimage (`.img`) ⇄ @ref DiskMedium.
 *
 * `.img` enthält **ausschließlich Sektor-Nutzdaten** an berechneten Offsets; alles
 * Weitere (Gaps, Marken, CRCs, Anhänge hinter der Daten-CRC) muss aus einem
 * @ref DiskFormat rekonstruiert bzw. verworfen werden.  Deshalb:
 *   - **Laden**: je Spur die Sektoren lesen und über @ref TrackCodec::buildTrack eine
 *     normgerechte IBM-Spur mit echten Marken und CRCs synthetisieren;
 *   - **Speichern**: je Spur @ref TrackCodec::parseTrack und die Nutzdaten an ihren
 *     Offset schreiben — verlustbehaftet, siehe @ref DiskMedium::rawCompatible.
 *
 * Spurreihenfolge in der Datei (A5120-Layout): verschränkt `(0,0) (0,1) (1,0) (1,1) …`.
 *
 * @see doc/design/09_floppy_drive.md §4.1
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_format.h"
#include "disk_medium.h"
#include <string>

/**
 * @namespace ImgCodec
 * @brief Laden/Speichern roher Sektorimages.
 */
namespace ImgCodec {

/// @brief `.img` mit @p fmt in @p out laden.
bool load(const std::string& path, const DiskFormat& fmt, DiskMedium& out, std::string& err);

/// @brief @p in als `.img` in @p fmt-Layout schreiben.
bool save(const std::string& path, const DiskFormat& fmt, const DiskMedium& in, std::string& err);

/**
 * @brief Byte-Offset von Sektor (cyl, head, id) in der `.img`-Datei, oder -1.
 * @note Sektor-IDs sind 1-basiert (bzw. ab @ref TrackFormat::first_sector_id).
 */
int64_t sectorOffset(const DiskFormat& fmt, uint8_t cyl, uint8_t head, uint8_t sector_id);

/**
 * @brief Prüft, ob das Medium zur Sektorgeometrie von @p fmt passt.
 *
 * Verglichen werden je nicht-leerer Spur Sektoranzahl, Sektorgröße und Sektor-IDs.
 * @return "" bei Übereinstimmung, sonst eine Beschreibung der ersten Abweichung.
 */
std::string mismatchReason(const DiskFormat& fmt, const DiskMedium& in);

}  // namespace ImgCodec
