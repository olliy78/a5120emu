/**
 * @file fs_catalog.h
 * @brief FsCatalog — laedt die Sektion `filesystems:` aus denselben Katalogdateien.
 *
 * Baut auf @ref FormatCatalog auf: dieselbe Pfadsuche, dasselbe YAML, dieselbe
 * zweistufige Fehlerbehandlung (fatal vs. „Eintrag uebersprungen").  Ein Profil, dessen
 * `format:` der Formatkatalog nicht kennt, wird mit Begruendung uebersprungen — so faellt
 * ein Tippfehler auf, statt ein halb funktionierendes Profil zu erzeugen.
 *
 * **Eine Katalogdatei ohne `filesystems:` ist gueltig** (§6.5): der Emulator kommt ohne
 * die Sektion aus, sie ist reine Zutat des k1520DiskTool.
 *
 * @see doc/design/13_k1520disktool.md §6
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/fs_profile.h"
#include "core/peripherals/floppy_drive/format_catalog.h"

#include <string>
#include <vector>

/**
 * @class FsCatalog
 * @brief Geladener Bestand an Dateisystemprofilen samt Lade-Diagnose.
 */
class FsCatalog {
public:
    /**
     * @brief Laedt alle existierenden Dateien aus @p paths (aufsteigende Prioritaet).
     * @param paths    Kandidatenpfade — in der Regel @ref FormatCatalog::searchPaths
     * @param formats  bereits geladener Formatkatalog (fuer die Querpruefung `format:`)
     * @param fatal_error  gesetzt, wenn der Katalog unbrauchbar ist (YAML kaputt o. Ae.)
     */
    static FsCatalog load(const std::vector<std::string>& paths,
                          const FormatCatalog& formats,
                          std::string* fatal_error);

    /// @brief Wie @ref load mit @ref FormatCatalog::searchPaths.
    static FsCatalog loadDefault(const FormatCatalog& formats, std::string* fatal_error);

    /// @brief Profil per Name; nullptr, wenn unbekannt.
    const FsProfile* find(const std::string& name) const;

    /// @brief Alle Profile, die auf der Geometrie @p format_name liegen.
    std::vector<const FsProfile*> forFormat(const std::string& format_name) const;

    const std::vector<FsProfile>&   profiles() const { return profiles_; }
    /// @brief Uebersprungene/auffaellige Definitionen (bereits formatierte Meldungen).
    const std::vector<std::string>& issues()   const { return issues_; }
    /// @brief Tatsaechlich gelesene Dateien.
    const std::vector<std::string>& sources()  const { return sources_; }

private:
    std::vector<FsProfile>   profiles_;
    std::vector<std::string> issues_;
    std::vector<std::string> sources_;
};
