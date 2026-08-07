/**
 * @file image_codec.h
 * @brief ImageCodec – Container-Schicht: Image-Datei ⇄ @ref DiskMedium.
 *
 * Ein Container-Codec **lädt** eine Datei vollständig in das interne Diskettenabbild
 * bzw. **schreibt** das Abbild vollständig als Datei.  Kein Codec hält Dateizustand
 * über den Aufruf hinaus — genau das erlaubt den nachträglichen Formatwechsel
 * (@ref DiskImage::saveAs).
 *
 * | Container | Endung | self-describing | braucht @ref DiskFormat |
 * |-----------|--------|-----------------|-------------------------|
 * | Img       | `.img` | nein            | **ja** (Geometrie + Sektorlayout) |
 * | Hfe       | `.hfe` | ja (Header/LUT) | nein |
 * | Dmk       | `.dmk` | ja (Header/IDAM-Tabelle) | nein |
 *
 * @see doc/design/09_floppy_drive.md §4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_format.h"
#include "disk_medium.h"
#include <string>

/**
 * @enum ContainerType
 * @brief Unterstützte Image-Dateiformate.
 */
enum class ContainerType : uint8_t {
    Img = 0,   ///< rohes Sektorimage (.img) — braucht ein DiskFormat
    Hfe,       ///< HxC Floppy Emulator v1 (.hfe) — Bitzellen, self-describing
    Dmk        ///< David Keil's Disk Image (.dmk) — Spur-Bytes + IDAM-Tabelle
};

/**
 * @namespace ImageCodec
 * @brief Fabrik + Ein-/Ausgabe für alle Container-Formate.
 */
namespace ImageCodec {

/// @brief Containertyp aus der Dateiendung (ohne Dateizugriff); Default @ref ContainerType::Img.
ContainerType fromExtension(const std::string& path);

/**
 * @brief Containertyp einer vorhandenen Datei bestimmen.
 *
 * HFE über die Signatur `HXCPICFE`; DMK über Endung **plus** Header-Plausibilität
 * (DMK hat keine Magic-Zahl); sonst @ref ContainerType::Img.
 */
ContainerType detect(const std::string& path);

const char* name(ContainerType t);        ///< "img" | "hfe" | "dmk"
const char* extension(ContainerType t);   ///< ".img" | ".hfe" | ".dmk"
bool selfDescribing(ContainerType t);     ///< Hfe/Dmk = true
bool needsDiskFormat(ContainerType t);    ///< nur Img = true

/**
 * @brief Datei vollständig in @p out laden (Geometrie inklusive).
 * @param fmt nur für @ref ContainerType::Img nötig (sonst ignoriert; nullptr erlaubt)
 * @param err Fehlertext bei false
 */
bool load(const std::string& path, ContainerType type,
          const DiskFormat* fmt, DiskMedium& out, std::string& err);

/**
 * @brief @p in vollständig als Datei @p path schreiben (Datei wird neu erzeugt).
 * @param fmt nur für @ref ContainerType::Img nötig; dort wird zusätzlich geprüft,
 *            ob das Medium überhaupt sektorweise darstellbar ist
 *            (@ref DiskMedium::rawCompatible).
 * @param err Fehlertext bei false
 */
bool save(const std::string& path, ContainerType type,
          const DiskFormat* fmt, const DiskMedium& in, std::string& err);

}  // namespace ImageCodec
