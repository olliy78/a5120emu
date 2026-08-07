/**
 * @file hfe_codec.h
 * @brief HfeCodec – HxC-Floppy-Emulator-Image v1 (`HXCPICFE`) ⇄ @ref DiskMedium.
 *
 * HFE speichert je Spurseite den **Bitzellen-Strom** — damit verlustfrei für Gaps,
 * Sync-Muster, Anhänge hinter der Daten-CRC, Mischdichte und unformatierte Spuren.
 *
 * Format (Auszug, little-endian):
 * @code
 *   Header (Block 0, 512 B): "HXCPICFE", formatrev=0, n_track, n_side,
 *       track_encoding(0=MFM,2=FM), bitrate[kbit/s], rpm, iface, dnu,
 *       track_list_block, write_allowed, single_step, …  (Rest 0xFF)
 *   Track-LUT (track_list_block*512): je Zylinder { u16 offset[512-B-Blöcke]; u16 len[Byte, beide Seiten] }
 *   Spurdaten (ab offset*512): 256-B-Blöcke seitenverschränkt [S0][S1][S0][S1]… , Zellen LSB-first
 *                              (bei EINSEITIGEN Medien kontinuierlich, 512 B/Block)
 * @endcode
 *
 * Beim **Speichern** werden Header und LUT stets neu berechnet (Spurlänge aus der
 * längsten Spur des Mediums) — deshalb kann ein aus `.img` geladenes oder frisch
 * formatiertes Medium ohne Vorlage als `.hfe` geschrieben werden.
 *
 * @see doc/design/09_floppy_drive.md §4.2
 * @see https://hxc2001.com/floppy_drive_emulator/HFE-file-format.html
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_medium.h"
#include <string>

/**
 * @namespace HfeCodec
 * @brief Laden/Speichern von HFE-v1-Images.
 */
namespace HfeCodec {

/**
 * @struct SourceInfo
 * @brief Eigenschaften der geladenen Datei, die die Bindung betreffen.
 */
struct SourceInfo {
    bool     write_allowed = true;   ///< HFE-Header-Feld `write_allowed`
    bool     oversampled   = false;  ///< Flux-Mitschnitt mit erhöhter Zellrate (nur lesen)
    uint16_t rpm           = 0;      ///< Drehzahl aus dem Header (0 = nicht angegeben)
};

/// @brief Trägt @p path die HFE-v1-Signatur?
bool isHfe(const std::string& path);

/// @brief `.hfe` vollständig in @p out laden.
bool load(const std::string& path, DiskMedium& out, SourceInfo* info, std::string& err);

/// @brief @p in vollständig als `.hfe` schreiben (Header + LUT werden neu berechnet).
bool save(const std::string& path, const DiskMedium& in, std::string& err);

}  // namespace HfeCodec
