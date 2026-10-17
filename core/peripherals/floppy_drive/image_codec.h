/**
 * @file image_codec.h
 * @brief ImageCodec – Container-Schicht: Image-Datei ⇄ @ref DiskMedium.
 *
 * Ein Container-Codec **laedt** eine Datei vollstaendig in das interne Diskettenabbild
 * bzw. **schreibt** das Abbild vollstaendig als Datei.  Kein Codec haelt Dateizustand
 * ueber den Aufruf hinaus — genau das erlaubt den nachtraeglichen Formatwechsel
 * (@ref DiskImage::saveAs).
 *
 * | Container | Endung | self-describing | braucht @ref DiskFormat |
 * |-----------|--------|-----------------|-------------------------|
 * | Img       | `.img` | nein            | **ja** (Geometrie + Sektorlayout) |
 * | Hfe       | `.hfe` | ja (Header/LUT) | nein |
 * | Dmk       | `.dmk` | ja (Header/IDAM-Tabelle) | nein |
 * | Cqm       | `.cqm` | ja (Kopf) | nein (Geometrie aus dem Medium, wie `.img` nur fuer rawCompatible-Medien) |
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
 * @brief Unterstuetzte Image-Dateiformate.
 */
enum class ContainerType : uint8_t {
    Img = 0,   ///< rohes Sektorimage (.img) — braucht ein DiskFormat
    Hfe,       ///< HxC Floppy Emulator v1 (.hfe) — Bitzellen, self-describing
    Dmk,       ///< David Keil's Disk Image (.dmk) — Spur-Bytes + IDAM-Tabelle
    Cqm        ///< CopyQM (.cqm) — RLE-gepackte Sektoren; schreibbar nur fuer rawCompatible-Medien einheitlicher Geometrie
};

/**
 * @namespace ImageCodec
 * @brief Fabrik + Ein-/Ausgabe fuer alle Container-Formate.
 */
namespace ImageCodec {

/// @brief Containertyp aus der Dateiendung (ohne Dateizugriff); Default @ref ContainerType::Img.
ContainerType fromExtension(const std::string& path);

/**
 * @brief Containertyp einer vorhandenen Datei bestimmen.
 *
 * HFE ueber die Signatur `HXCPICFE`; CQM ueber `CQ` + Kopfpruefbyte (Endung egal);
 * DMK ueber Endung **plus** Header-Plausibilitaet (DMK hat keine Magic-Zahl); sonst
 * @ref ContainerType::Img.
 */
ContainerType detect(const std::string& path);

const char* name(ContainerType t);        ///< "img" | "hfe" | "dmk" | "cqm"
const char* extension(ContainerType t);   ///< ".img" | ".hfe" | ".dmk" | ".cqm"
bool selfDescribing(ContainerType t);     ///< Hfe/Dmk/Cqm = true
bool needsDiskFormat(ContainerType t);    ///< nur Img = true
bool writable(ContainerType t);           ///< alle Container sind schreibbar (Grenzen: save())

/**
 * @brief Datei vollstaendig in @p out laden (Geometrie inklusive).
 * @param fmt nur fuer @ref ContainerType::Img noetig (sonst ignoriert; nullptr erlaubt)
 * @param err Fehlertext bei false
 */
bool load(const std::string& path, ContainerType type,
          const DiskFormat* fmt, DiskMedium& out, std::string& err);

/**
 * @brief @p in vollstaendig als Datei @p path schreiben (Datei wird neu erzeugt).
 * @param fmt nur fuer @ref ContainerType::Img noetig; dort wird zusaetzlich geprueft,
 *            ob das Medium ueberhaupt sektorweise darstellbar ist
 *            (@ref DiskMedium::rawCompatible).
 * @param err Fehlertext bei false
 */
bool save(const std::string& path, ContainerType type,
          const DiskFormat* fmt, const DiskMedium& in, std::string& err);

}  // namespace ImageCodec
