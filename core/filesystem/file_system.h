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
    uint16_t    entry_addr = 0;  ///< UDOS: Startadresse bei Typ P/P1 (0 = keine)
    uint16_t    record_len = 0;  ///< UDOS: Satzlaenge aus dem Kopfsektor (0 = unbekannt)
    uint16_t    block_len  = 0;  ///< UDOS: zweite Laengenangabe (Offset 17)
    uint16_t    bytes_in_last = 0;  ///< UDOS: Bytes im letzten Satz (Offset 22) —
                                    ///< bestimmt die logische Laenge
    uint32_t    extra      = 0;  ///< UDOS: Kopfsektor Offset 44–47, Bedeutung offen
    std::string created;         ///< UDOS: Erstellungsvermerk (auch Versionstext "V 4.3 ")
    uint16_t    segment_start  = 0;  ///< UDOS: Anfang des Speichersegments (Kopf 40)
    uint16_t    segment_len  = 0;  ///< UDOS: Laenge des Segments (nicht die Dateigroesse!)
    uint16_t    low_addr   = 0;  ///< UDOS: LOW ADDRESS — erste belegte Adresse (Kopf 122)
    uint16_t    high_addr  = 0;  ///< UDOS: HIGH ADDRESS — letzte belegte (Kopf 124)
    uint16_t    stack_size = 0;  ///< UDOS: STACK SIZE (Kopf 126)
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
    /// @brief UDOS-Datum „JJMMTT" fuer den Kopfsektor; leer = heutiges Systemdatum.
    ///        (CP/M 2.2 fuehrt keine Zeitstempel und ignoriert das Feld.)
    std::string date;

    // ── nur UDOS: was im Kopfsektor steht und Linux nicht mitliefert ─────────
    //
    // Ein UDOS-Kopfsektor traegt mehr als Name und Bytes: TYP, EIGENSCHAFTEN und —
    // bei Programmen — die STARTADRESSE.  Ohne sie wird aus einer Systemdatei eine
    // gewoehnliche Binaerdatei, und der Urlader springt ins Leere: eine so kopierte
    // UDOS-Bootdiskette bleibt im Debugger stehen (`BREAK 2600`).  CP/M ignoriert
    // diese Felder.

    /// @brief "A"|"P"|"P1"|"B"|"D"; leer = aus @ref text ableiten (A bzw. B).
    std::string udos_type;
    /// @brief Eigenschaften als Buchstaben (W E L S R F), z. B. "WS"; leer = keine.
    std::string udos_properties;
    /// @brief ENTRY — Einsprungadresse fuer Typ P/P1; 0 = keine.
    uint16_t    udos_entry = 0;
    /// @brief Satzlaenge (Vielfaches von 128); 0 = 128.  **Kein Schoenheitswert:**
    ///        ein Satz belegt `Satzlaenge/128` aufeinanderfolgende Sektoren einer
    ///        Spur, und UDOS liest Systemdateien satzweise — `ZDOS` hat 1024, `OS`
    ///        hat 512.  Mit 128 zurueckgeschrieben bootet die Diskette nicht.
    uint16_t    udos_record_len = 0;
    /// @brief SEGMENTS — Anfang und Laenge des Speichersegments (Kopfsektor 40/42).
    ///        Die Laenge ist NICHT die Dateigroesse (`OS`: 5504 Byte gross,
    ///        Segment 5632 Byte).
    uint16_t    udos_segment = 0;
    uint16_t    udos_segment_len = 0;
    /// @brief LOW/HIGH ADDRESS und STACK SIZE (Kopfsektor 122/124/126) — der
    ///        Speicher, den das Programm insgesamt belegt (Segment + Arbeitsspeicher
    ///        + Stapel).  **Das ist es, was der Lader zuteilen laesst**: er traegt
    ///        LOW/HIGH in die Nukleusvariablen 1275H/1277H und ruft den
    ///        Speicherverwalter (1009H).  Fehlt die Angabe, steht dort FFFF und UDOS
    ///        weist die Datei mit `MEMORY PROTECT VIOLATION` ab
    ///        (doc/udos_diskettenformat.md §14).
    uint16_t    udos_low_addr   = 0;
    uint16_t    udos_high_addr  = 0;
    uint16_t    udos_stack_size = 0;
    /// @brief Zweite Laengenangabe (Offset 17); @ref udos_block_len_gesetzt sagt,
    ///        ob der Wert gilt — 0 ist ein GUELTIGER Wert (Nukleus).
    uint16_t    udos_block_len = 0;
    bool        udos_block_len_gesetzt = false;
    /// @brief „Bytes im letzten Satz" (Offset 22); 0 = aus der Datengroesse rechnen.
    ///        Noetig, wenn das Speicherabbild ueber das logische Dateiende hinausreicht.
    uint16_t    udos_bytes_in_last = 0;
    /// @brief Kopfsektor Offset 44–47 (Bedeutung offen, unveraendert uebernehmen).
    uint32_t    udos_extra = 0;
    /// @brief Erstellungsvermerk (6 Zeichen; auch ein Versionstext wie "V 4.3 ");
    ///        leer = wie @ref date.
    std::string udos_created;
};

/**
 * @struct UdosAttrs
 * @brief Die Kopfsektorangaben einer UDOS-Datei — lesen und **aendern**.
 *
 * Dieselben Felder wie in @ref WriteOptions, aber als Bausatz fuer eine Datei, die
 * schon auf der Diskette liegt (@ref FileSystem::setAttributes).  Leere Zeichenketten
 * und `false`-Kennzeichen heissen „unveraendert lassen" — so kann eine Oberflaeche ein
 * einzelnes Feld setzen, ohne die uebrigen zu kennen.
 */
struct UdosAttrs {
    std::string type;            ///< "A"/"P"/"P1"/"B"; leer = unveraendert
    std::string properties;      ///< "WELS"; leer = unveraendert (";" = alle loeschen)
    std::string created;         ///< 6 Zeichen; leer = unveraendert
    std::string modified;        ///< "JJMMTT"; leer = unveraendert

    bool     set_entry = false;      uint16_t entry = 0;        ///< ENTRY (Offset 20)
    bool     set_block_len = false;  uint16_t block_len = 0;    ///< Offset 17
    bool     set_segment = false;    uint16_t segment = 0, segment_len = 0;  ///< 40/42
    bool     set_memory = false;     uint16_t low = 0, high = 0, stack = 0;  ///< 122/124/126
    bool     set_extra = false;      uint32_t extra = 0;        ///< Offset 44…47
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
     * @brief Kopfsektorangaben einer vorhandenen Datei aendern (nur UDOS).
     *
     * Der Inhalt bleibt unangetastet; geaendert wird allein der Kopfsektor.  CP/M
     * kennt diese Angaben nicht und meldet das als Fehler.
     */
    virtual bool setAttributes(const std::string& name, const UdosAttrs&) {
        last_error_ = "Dieses Dateisystem fuehrt keine solchen Dateiangaben";
        return false;
    }

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
