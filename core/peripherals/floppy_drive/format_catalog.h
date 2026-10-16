/**
 * @file format_catalog.h
 * @brief FormatCatalog — laedt die Diskettenformate aus `formats.yaml`.
 *
 * Loest die frueher einkompilierten `FormatParser::builtinFormats()` ab: der Katalog ist
 * eine **reine Laufzeitdatei**.  Geladen wird ueber eine Pfadliste in aufsteigender
 * Prioritaet (@ref FormatCatalog::searchPaths); ein spaeteres Format gleichen Namens
 * ersetzt ein frueheres (User-Override).
 *
 * ### Fehlerverhalten (bewusst zweistufig)
 * - **Fatal** (Rueckgabe ueber @p fatal_error, Katalog unbrauchbar): keine Katalogdatei
 *   gefunden, Datei nicht lesbar, YAML-Syntaxfehler, oder am Ende kein einziges
 *   gueltiges Format.  Der Aufrufer (`A5120Machine`) bricht damit den Start ab.
 * - **Pro Format** (Rueckgabe ueber @ref issues): eine fehlerhafte Formatdefinition wird
 *   mit Datei, Zeile, Name und Grund gemeldet und **uebersprungen** — die uebrigen,
 *   korrekt spezifizierten Formate bleiben nutzbar.
 *
 * @see doc/K1520_architecture.md §8.6
 * @see core/peripherals/floppy_drive/disk_format.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include <string>
#include <vector>

/**
 * @brief Passt @p fmt geometrisch/verfahrensseitig auf @p prof? (Validierung V4)
 *
 * Prueft Kopfzahl, Zylinderzahl und ob das Laufwerk **jedes** im Format verwendete
 * Aufzeichnungsverfahren beherrscht.  @p why erhaelt bei `false` den konkreten Grund.
 */
bool formatFitsDrive(const DiskFormat& fmt, const DriveProfile& prof, std::string* why);

/**
 * @class FormatCatalog
 * @brief Geladener Bestand an Diskettenformaten samt Lade-Diagnose.
 */
class FormatCatalog {
public:
    /**
     * @brief Kandidatenpfade in **aufsteigender** Prioritaet.
     *
     * 1. `K1520_FORMATS_DEFAULT` (CMake-Compile-Define — Quell- bzw. Installbaum)
     * 2. `<Verzeichnis des eigenen Moduls>/../share/a5120emu/formats.yaml`
     * 3. `./data/formats.yaml` (aktuelles Verzeichnis)
     * 4. `${XDG_CONFIG_HOME:-~/.config}/a5120emu/formats.yaml` (Windows: `%APPDATA%`)
     * 5. `$K1520_FORMATS` (Datei oder Verzeichnis; mehrere `:`- bzw. `;`-getrennt)
     *
     * „Eigenes Modul" ist bei den statisch gelinkten Werkzeugen die Programmdatei,
     * bei `libk1520core` die Bibliothek selbst — sonst suchte der von der GUI per
     * `ctypes` geladene Kern neben dem Python-Interpreter des venv.  Eine
     * Installation ist dadurch frei verschiebbar.
     *
     * Die Liste enthaelt **alle** Kandidaten, auch nicht existierende — fuer die
     * Fehlermeldung „keine Katalogdatei gefunden, gesucht in: …".
     */
    static std::vector<std::string> searchPaths();

    /**
     * @brief Laedt alle existierenden Dateien aus @p paths (aufsteigende Prioritaet).
     * @param paths        Kandidatenpfade (nicht existierende werden uebersprungen)
     * @param fatal_error  bei Rueckgabe eines unbrauchbaren Katalogs gesetzt (sonst leer)
     * @return Katalog; bei fatalem Fehler leer
     */
    static FormatCatalog load(const std::vector<std::string>& paths,
                              std::string* fatal_error);

    /// @brief Wie @ref load mit @ref searchPaths().
    static FormatCatalog loadDefault(std::string* fatal_error);

    /// @brief Format per Name; nullptr, wenn unbekannt.
    const DiskFormat* find(const std::string& name) const;

    /// @brief Alle Formate, die @p prof in ihrer `drives:`-Liste nennen (Standard zuerst).
    std::vector<const DiskFormat*> forDrive(const DriveProfile& prof) const;

    /// @brief Standardformat fuer @p prof (`default_for:`); nullptr, wenn keines benannt.
    const DiskFormat* defaultFor(const DriveProfile& prof) const;

    const std::vector<DiskFormat>&  formats() const { return formats_; }
    /// @brief Uebersprungene/auffaellige Definitionen (bereits formatierte Meldungen).
    const std::vector<std::string>& issues()  const { return issues_; }
    /// @brief Tatsaechlich gelesene Dateien (Diagnose, C-API).
    const std::vector<std::string>& sources() const { return sources_; }

private:
    std::vector<DiskFormat>  formats_;
    std::vector<std::string> issues_;
    std::vector<std::string> sources_;
};
