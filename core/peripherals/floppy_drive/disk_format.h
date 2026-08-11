/**
 * @file disk_format.h
 * @brief DiskFormat / TrackFormat — Geometrie- und Verfahrensbeschreibung einer Diskette.
 *
 * Reines Datenmodell ohne Parser: geladen wird es vom @ref FormatCatalog aus
 * `data/formats.yaml` (siehe doc/K1520_architecture.md §8.6).  Ein Format besteht aus
 * disjunkten **Spurbereichen** (Zylinder × Kopf), von denen jeder seine eigene
 * Sektorgeometrie **und sein eigenes Aufzeichnungsverfahren** trägt — damit sind
 * Mischdichte-Disketten (FM-Systemspur + MFM-Datenspuren) darstellbar.
 *
 * (Vorgänger: `format_parser.h` mit einkompilierten `builtinFormats()` und einem
 * INI-artigen `parseFile()`; beide sind mit dem YAML-Katalog entfallen.)
 *
 * @see core/peripherals/floppy_drive/format_catalog.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/track_image.h"   // Encoding
#include <cstdint>
#include <string>
#include <vector>

/**
 * @struct TrackFormat
 * @brief Geometrie + Verfahren eines zusammenhängenden Spurbereichs (Zylinder × Kopf).
 *
 * Bereichsgrenzen sind **inklusive**.  Die Bereiche eines @ref DiskFormat sind
 * überlappungsfrei (wird beim Laden validiert).
 */
struct TrackFormat {
    uint8_t  cyl_first      = 0;
    uint8_t  cyl_last       = 0;
    uint8_t  head_first     = 0;
    uint8_t  head_last      = 1;
    uint8_t  secs_per_track = 0;
    uint16_t bytes_per_sec  = 0;

    /**
     * @brief Aufzeichnungsverfahren **dieses Spurbereichs**.
     *
     * Auflösung beim Laden: `tracks[].encoding` → `formats[].encoding` → Laufwerks-Default.
     * Erlaubt Mischdichte innerhalb einer Diskette (8″-System-34: FM-System-, MFM-Datenspuren).
     */
    Encoding encoding = Encoding::MFM;

    /// @brief Erste Sektor-ID der Spur (IBM-üblich 1-basiert).
    uint8_t  first_sector_id = 1;

    uint32_t trackBytes() const {
        return static_cast<uint32_t>(secs_per_track) * bytes_per_sec;
    }

    /// @brief Deckt dieser Bereich (cyl, head) ab?
    bool covers(uint8_t cyl, uint8_t head) const {
        return cyl  >= cyl_first  && cyl  <= cyl_last
            && head >= head_first && head <= head_last;
    }

    /// @brief Überschneidet sich dieser Bereich mit @p o? (Validierung V2)
    bool overlaps(const TrackFormat& o) const {
        return cyl_first  <= o.cyl_last  && o.cyl_first  <= cyl_last
            && head_first <= o.head_last && o.head_first <= head_last;
    }
};

/**
 * @struct DiskFormat
 * @brief Ein benanntes Diskettenformat: Spurbereiche + Laufwerks-Kompatibilität.
 */
struct DiskFormat {
    std::string name;           ///< eindeutiger Katalogname (C-API, GUI, Tools)
    std::string description;    ///< Klartext für die GUI (leer erlaubt)

    /// @brief Namen der kompatiblen DriveProfile (§8.4) — **abschließend**, keine Heuristik.
    std::vector<std::string> drives;
    /// @brief Profile, für die dies das Standardformat beim Anlegen ist.
    std::vector<std::string> default_for;

    bool allow_img = true;      ///< als rohes Sektorimage (.img) darstellbar
    bool allow_hfe = true;      ///< als HFE-Bitstromimage (.hfe) darstellbar

    /**
     * @brief **Schrittweite**: logische Spur `n` liegt auf physischem Zylinder `n·step`.
     *
     * Vorgabe 1 — jedes gewöhnliche Format ist davon unberührt.  `step: 2` ist der
     * **Doppelschritt**: ein 96-tpi-Laufwerk (K5601, K5600.20) beschreibt nur jeden
     * zweiten Zylinder, damit die Spuren radial dort liegen, wo ein 48-tpi-Laufwerk
     * (K5600.10) sie erwartet — das Austauschformat zwischen beiden Spurdichten
     * (`doc/format.md`, „Wozu einseitig und Doppelschritt gut sind").
     *
     * `tracks:` bleibt dabei **logisch** (`cyls: 0-39`), also so, wie das Gastsystem
     * die Diskette sieht; und genauso steht die Spurnummer im **ID-Feld** — nachgemessen
     * an den von CP/A erzeugten Abbildern: physisch c4h0 trägt `cyl=2`.
     */
    uint8_t step = 1;

    std::vector<TrackFormat> tracks;

    uint8_t  numHeads()     const;
    /// @brief **Logische** Spurzahl (so viele Spuren sieht das Dateisystem).
    uint8_t  numCylinders() const;
    /// @brief Physischer Zylinder einer logischen Spur.
    uint8_t  physicalCylinder(uint8_t logical_cyl) const {
        return static_cast<uint8_t>(logical_cyl * step);
    }
    /// @brief So viele Zylinder muss das Laufwerk haben (bei `step: 2` fast doppelt so viele).
    uint8_t  physicalCylinders() const;
    uint64_t totalBytes()   const;

    /// @brief Spurbereich, der die **logische** Spur (cyl, head) abdeckt, oder nullptr.
    const TrackFormat* findTrack(uint8_t cyl, uint8_t head) const;

    /// @brief Physischer Zylinder → logische Spur; -1, wenn dort keine Spur liegen darf.
    int logicalCylinder(uint8_t physical_cyl) const {
        if (step <= 1) return physical_cyl;
        if (physical_cyl % step != 0) return -1;
        return physical_cyl / step;
    }

    /// @brief Ist dieses Profil in @ref drives gelistet?
    bool supportsDrive(const std::string& profile_name) const;

    /**
     * @brief Vorherrschendes Verfahren (nach Spuranzahl) — für HFE-Header/DiskGeometry.
     *
     * HFE v1 trägt nur EIN Verfahren im Header; die abweichenden Spuren einer
     * Mischdichte-Diskette deckt die Pro-Spur-Erkennung in `HfeImage::readTrack` ab.
     */
    Encoding predominantEncoding() const;

    /// @brief true, wenn nicht alle Spurbereiche dasselbe Verfahren benutzen.
    bool isMixedEncoding() const;
};
