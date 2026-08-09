/**
 * @file file_system.h
 * @brief Gemeinsame Fassade der Dateisysteme (CP/M, UDOS) — ein Volume.
 *
 * Ein **Volume** ist ein Dateisystem: bei CP/M die ganze Diskette, bei UDOS **eine
 * Seite** (`doc/udos_diskettenformat.md` §2).  Die Diskette als Ganzes — also 1..n
 * Volumes plus Dateibindung — ist eine Ebene darueber (`DiskVolume`).
 *
 * Fehlerstil wie im Kern: Rueckgabe `bool` + @ref FileSystem::lastError in
 * Klartext-Deutsch, keine Ausnahmen ueber die Modulgrenze.
 *
 * @see doc/design/13_k1520disktool.md §9
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * @struct FileEntry
 * @brief Ein Verzeichniseintrag, dateisystemunabhaengig.
 */
struct FileEntry {
    int         volume = 0;      ///< 0..n-1 — bei UDOS die Seite, sonst immer 0
    std::string name;            ///< CP/M "NAME.TYP" · UDOS "HELP.DAT.00"
    int         user   = 0;      ///< CP/M-Nutzerbereich 0..15 (UDOS: immer 0)
    uint64_t    size   = 0;      ///< Nutzbytes
    std::string type;            ///< CP/M "" · UDOS "A"/"P"/"P1"/"B"/"D"
    std::string attributes;      ///< CP/M "RO SYS ARC" · UDOS "WELS"
    std::string date;            ///< "" wenn das Dateisystem keins fuehrt
    bool        hidden  = false; ///< CP/M SYS · UDOS SECRET
    bool        damaged = false; ///< CRC-Fehler oder Kettenbruch beim Lesen

    /// @brief Eindeutige Bezeichnung innerhalb des Volumes ("NAME.TYP" bzw. "3:NAME.TYP").
    std::string qualifiedName() const {
        return user == 0 ? name : std::to_string(user) + ":" + name;
    }
};

/**
 * @struct FsInfo
 * @brief Zustand eines Volumes fuer Anzeige und Pruefbericht.
 */
struct FsInfo {
    std::string label;             ///< Datentraegername ("" wenn keiner gefuehrt wird)
    uint64_t    total_bytes = 0;   ///< Nutzkapazitaet des Dateisystems
    uint64_t    free_bytes  = 0;
    uint64_t    used_bytes  = 0;
    int         files       = 0;
    std::vector<std::string> warnings;   ///< Auffaelligkeiten (CRC, Ketten, Zaehler)
};

/**
 * @struct WriteOptions
 * @brief Steuerung des Einfuegens.
 */
struct WriteOptions {
    bool overwrite = false;   ///< vorhandene gleichnamige Datei ersetzen
    bool text      = false;   ///< Textmodus: Rest des letzten Satzes mit 0x1A fuellen
};

/**
 * @struct PlannedFile
 * @brief Was eingefuegt werden SOLL — Eingabe der Platzpruefung (§9.2).
 */
struct PlannedFile {
    std::string name;
    uint64_t    size   = 0;
    int         volume = 0;
    bool        replaces_existing = false;   ///< von @ref FileSystem::wouldFit gesetzt
};

/**
 * @struct FitReport
 * @brief Ergebnis der Platzpruefung — mit Zahlen, damit die Meldung etwas taugt.
 */
struct FitReport {
    bool     fits        = false;
    uint64_t needed      = 0;   ///< Bedarf INKLUSIVE Verwaltungsaufwand
    uint64_t available   = 0;
    int      dir_needed  = 0;   ///< benoetigte Verzeichnisplaetze
    int      dir_free    = 0;
    std::string detail;         ///< Klartext, auch wenn es passt ("" = ohne Befund)
};

/**
 * @class FileSystem
 * @brief Ein Volume: auflisten, lesen, schreiben, loeschen.
 */
class FileSystem {
public:
    virtual ~FileSystem() = default;

    /// @brief Verzeichnis — IMMER frisch aus dem Medium gelesen (§9.3, kein Zwischenspeicher).
    virtual std::vector<FileEntry> list() const = 0;

    /// @brief Dateiinhalt lesen.  @p name wie @ref FileEntry::qualifiedName.
    virtual bool read(const std::string& name, std::vector<uint8_t>& out) = 0;

    /// @brief Datei einfuegen/ersetzen.
    virtual bool write(const std::string& name, const std::vector<uint8_t>& data,
                       const WriteOptions& opt) = 0;

    /// @brief Datei entfernen.
    virtual bool erase(const std::string& name) = 0;

    /**
     * @brief Was WUERDE das Einfuegen kosten?  Schreibt nichts.
     *
     * Rechnet den Verwaltungsaufwand mit — bei CP/M die Blockrundung und die
     * Verzeichnisplaetze je Extent, bei UDOS Kopfsektor, Satzrundung und das
     * Wachstum der Verzeichnisdatei.  Ersetzte gleichnamige Dateien geben ihren
     * Platz zurueck und werden gutgeschrieben.
     */
    virtual bool wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const = 0;

    /// @brief Leeres Dateisystem anlegen (Verzeichnis initialisieren).
    virtual bool mkfs() = 0;

    /// @brief Zustand des Volumes.
    virtual FsInfo info() const = 0;

    const std::string& lastError() const { return last_error_; }

protected:
    bool fail(const std::string& why) const { last_error_ = why; return false; }
    mutable std::string last_error_;
};
