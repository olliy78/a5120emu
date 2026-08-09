/**
 * @file geometry_probe.h
 * @brief Geometrie eines Mediums MESSEN und gegen den Formatkatalog abgleichen.
 *
 * Stufe 1 der Formaterkennung des k1520DiskTool (doc/design/13_k1520disktool.md §12.1):
 * `.hfe` und `.dmk` sind selbstbeschreibend — nach dem Laden liegt das ganze Medium vor,
 * und @ref TrackCodec::parseTrack liefert je Spur die **tatsaechlichen** Sektor-IDs,
 * Sektorgroessen und das Verfahren.  Daraus entsteht ein gemessener Spurbereichsplan, der
 * mit jedem @ref DiskFormat des Katalogs verglichen wird.
 *
 * Kein Treffer heisst **Abbruch mit Diagnose**, nicht „irgendein Format nehmen":
 * @ref GeometryProbe::describe formatiert die Messung so, dass sie zugleich als Vorlage
 * fuer einen fehlenden Katalogeintrag taugt.
 *
 * @see doc/design/13_k1520disktool.md §12
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/disk_medium.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @struct MeasuredTrack
 * @brief Was auf einer Spur tatsaechlich steht.
 */
struct MeasuredTrack {
    uint8_t  cyl        = 0;
    uint8_t  head       = 0;
    bool     formatted  = false;   ///< traegt Adressmarken
    uint8_t  sectors    = 0;       ///< Anzahl gefundener Sektoren
    uint16_t sector_size= 0;       ///< einheitliche Sektorgroesse (0 = uneinheitlich)
    uint8_t  first_id   = 0;       ///< kleinste Sektor-ID
    bool     ids_dense  = false;   ///< IDs bilden lueckenlos first_id … first_id+sectors-1
    Encoding encoding   = Encoding::MFM;
    uint16_t crc_errors = 0;       ///< Sektoren mit ID- oder Daten-CRC-Fehler
};

/**
 * @struct GeometryMatch
 * @brief Ergebnis des Abgleichs einer Messung mit EINEM Format.
 */
struct GeometryMatch {
    const DiskFormat* format = nullptr;
    bool        ok        = false;
    std::string reason;              ///< bei ok == false: warum nicht
    uint16_t    empty_tracks = 0;    ///< tolerierte unformatierte Spuren
    uint16_t    stray_tracks = 0;    ///< beschriebene Spuren HINTER dem Format (Altbestand)
    uint16_t    defect_tracks= 0;    ///< Spuren mit zu WENIGEN Sektoren (Schaden)
    uint16_t    crc_errors   = 0;    ///< Summe ueber alle Spuren
    uint16_t    slack_cyls   = 0;    ///< ungenutzte Zylinder des Formats (kleiner = besser)

    /// @brief Auffaelligkeiten im Klartext ("" = makellos) — gehoert in die Anzeige.
    std::string remarks() const;
};

/**
 * @namespace GeometryProbe
 * @brief Messen, Abgleichen, Beschreiben.
 */
namespace GeometryProbe {

/// @brief Alle Spuren des Mediums vermessen (Reihenfolge: Zylinder aussen, Kopf innen).
std::vector<MeasuredTrack> measure(const DiskMedium& medium);

/// @brief Hoechster Zylinder mit Adressmarken; -1, wenn das Medium unformatiert ist.
int lastFormattedCylinder(const std::vector<MeasuredTrack>& tracks);

/**
 * @brief Passt die Messung zu @p fmt?
 *
 * Regeln (doc/design/13_k1520disktool.md §12.1):
 *  1. jede **formatierte** Spur muss von einem Spurbereich abgedeckt sein und in
 *     Sektorgroesse, erster ID und Verfahren uebereinstimmen;
 *  2. unformatierte Spuren werden toleriert (echte Abbilder tragen oft ein bis drei
 *     leere Zusatzspuren) — innerhalb des Formats zaehlen sie als @ref empty_tracks;
 *  3. beschriebene Spuren **hinter** dem Format sind Altbestand einer frueheren
 *     Formatierung (@ref stray_tracks) und disqualifizieren nicht: das Dateisystem
 *     fasst sie nie an.  Der Referenzdatentraeger `udos_boot_scp.hfe` traegt genau
 *     das — 77 UDOS-Spuren, dahinter drei Spuren 9×512 aus einem frueheren Leben;
 *  4. **zu wenige** Sektoren auf einer Spur sind ein Schaden (@ref defect_tracks), kein
 *     anderes Format — zu VIELE dagegen schon;
 *  5. einzelne CRC-Fehler disqualifizieren nicht — sie werden gezaehlt und gemeldet;
 *  6. ein Format mit mehr Koepfen als das Medium passt nie (kein DS-Format auf einer
 *     einseitigen Diskette).
 */
GeometryMatch match(const std::vector<MeasuredTrack>& tracks, const DiskFormat& fmt);

/**
 * @brief Alle passenden Formate, bestes zuerst.
 *
 * Rangfolge: wenige ungenutzte Zylinder vor vielen, dann wenige tolerierte Leerspuren.
 * Mehrere gleich gute Treffer sind **normal** — `formats.yaml` enthaelt geometrisch
 * identische Eintraege (`cpa640` ≡ `k5601_16x256`).  Welches Dateisystem darauf liegt,
 * entscheidet Stufe 2.
 */
std::vector<GeometryMatch> matchAll(const std::vector<MeasuredTrack>& tracks,
                                    const std::vector<DiskFormat>& formats);

/// @brief Messung als Klartext — Grundlage der Fehlermeldung „passt zu keinem Format".
std::string describe(const std::vector<MeasuredTrack>& tracks);

}  // namespace GeometryProbe
