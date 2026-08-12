/**
 * @file cpa_dpb.h
 * @brief CpaDpb — die Formaterkennung des CP/A-BIOS nachgebildet.
 *
 * `filesystems:` in `data/formats.yaml` beschreibt **benannte** Dateisysteme.  Fuer die
 * Handvoll Disketten, die man staendig in der Hand hat, ist das genau richtig — fuer die
 * rund 60 Geometrien des Katalogs waere es Handarbeit mit Rateanteil, und jedes neu vermessene
 * Format brauchte wieder einen Eintrag.
 *
 * Das ist unnoetig: **CP/A raet nicht, CP/A rechnet.**  Sein BIOS (`biosdsk.mac`,
 * `drdfrm`) leitet den DPB beim LOGIN aus dem ab, was es auf der Diskette vorfindet —
 * Sektorlaengencode der Datenspur, Spurzahl, ein-/beidseitig, Inhalt der Spur 0.  Die
 * Zuordnung steht dort in vier Tabellen `dtrsl0..3` (eine je Sektorlaenge, darin je eine
 * Zeile fuer 40SS / 80SS / 40DS / 80DS und die 8″-Laufwerke).  Diese Datei bildet genau
 * das nach: aus Geometrie + Spur 0 wird ein @ref FsProfile.
 *
 * Damit ist jede CP/A-formatierte Diskette lesbar, ohne dass ihr Format im Katalog steht;
 * und die Werte sind nicht geschaetzt, sondern die des Originals.  Gegenprobe: die
 * Regel reproduziert die von Hand nachgemessenen Profile `cpa780` (4 Systemspuren,
 * 2 KB, 128 Eintraege) und `scpx798` exakt — siehe `tests/unit/filesystem/test_cpa_dpb.cpp`.
 * Und sie korrigierte einen geratenen Katalogwert: `cpa800` hat **192** Verzeichnisplaetze,
 * am laufenden CP/A nachgewiesen (`DiskToolNeueDisketten.CpaFindetDateiJenseitsVonPlatz128`).
 *
 * ### Rangfolge im Werkzeug
 * Ein benanntes Profil aus dem Katalog gewinnt immer; die Ableitung ist der **Rueckfall**,
 * wenn keines passt (@ref DiskVolume::open).  Erzwingen laesst sie sich mit
 * `--fs cpa_auto`.
 *
 * @see doc/cpa_format_detection.md, doc/design/13_k1520disktool.md §6.4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/fs_profile.h"
#include "core/filesystem/sector_space.h"
#include "core/peripherals/floppy_drive/disk_format.h"

#include <cstdint>
#include <string>

/**
 * @struct CpaDpbEntry
 * @brief Eine Zeile aus `dtrsl0..3` — die Tabellenwerte VOR der Aufloesung.
 */
struct CpaDpbEntry {
    uint16_t dir_entries = 0;    ///< `dsldir`+1 — Verzeichnisplaetze laut Tabelle
    uint8_t  sys_tracks  = 0;    ///< `dsloff`/2 — logische Systemspuren
    bool     fixed_off   = false;///< `dslfo` — Systemspurzahl ist fest, nicht zu pruefen
    uint32_t block_size  = 2048; ///< aus `dbl1k`/`dbl2k`/`dbl2k0`
};

/**
 * @struct CpaDpb
 * @brief Das Ergebnis der Erkennung, wie es der DPB traegt.
 */
struct CpaDpb {
    uint8_t  size_code   = 0;    ///< Sektorlaengencode der DATENspur (0…3)
    uint8_t  row         = 0;    ///< gewaehlte Tabellenzeile (@ref CpaDpb::Row)
    uint16_t dir_entries = 0;    ///< nach der 192→128-Korrektur
    uint8_t  sys_tracks  = 0;    ///< aufgeloest (logische Spuren)
    uint32_t block_size  = 2048;
    uint8_t  skew        = 0;    ///< 6 bei 128-B-Sektoren (Tabelle `xlt`), sonst 0
    uint8_t  data_cyl    = 0;    ///< @ref sys_tracks in (Zylinder, Kopf) umgerechnet
    uint8_t  data_head   = 0;

    /// @brief Zeilen der `dtrsl`-Tabellen, in der Reihenfolge des BIOS.
    enum Row : uint8_t { Ss40 = 0, Ss80 = 1, Ds40 = 2, Ds80 = 3, Fm8 = 4, Mfm8 = 5 };
};

/**
 * @class CpaDpbRule
 * @brief Die Regel selbst — reine Rechnung, kein Zustand.
 */
class CpaDpbRule {
public:
    /// @brief Name des abgeleiteten Profils; auch als `--fs`-Wert gueltig.
    static constexpr const char* kName = "cpa_auto";

    /// @brief Tabellenzeile @p row der Tabelle zu @p size_code (0…3).
    static CpaDpbEntry entry(uint8_t size_code, uint8_t row);

    /**
     * @brief Erkennung durchfuehren.
     * @param fmt    erkannte Geometrie
     * @param space  Sektorraum ueber der ganzen Diskette (fuer Spur 0 und die Leerprobe)
     * @param out    ausgefuellter DPB
     * @param why    Grund, wenn false zurueckkommt
     *
     * Kein Ergebnis gibt es nur, wenn die Diskette gar nicht CP/M-artig sein kann:
     * unbekannte Sektorlaenge (nicht 128…1024) oder kein einheitlicher Datenbereich.
     */
    static bool derive(const DiskFormat& fmt, const SectorSpace& space,
                       CpaDpb& out, std::string* why);

    /// @brief Wie @ref derive, aber gleich als Profil (Name @ref kName).
    static bool profile(const DiskFormat& fmt, const SectorSpace& space,
                        FsProfile& out, std::string* why);

    /// @brief Klartext des DPB fuer Meldungen (`detection().remarks`).
    static std::string describe(const CpaDpb& d);
};
