/**
 * @file disk_volume.h
 * @brief DiskVolume — die Diskette als Ganzes: 1..n Dateisysteme + Dateibindung.
 *
 * Das ist die Arbeitsschnittstelle des k1520DiskTool.  Sie setzt die vier Zusagen um,
 * die den Umgang mit dem Werkzeug ausmachen (doc/design/13_k1520disktool.md §1):
 *
 * 1. **Eine beidseitige UDOS-Diskette ist EIN Datentraeger.**  Beide Seiten werden
 *    gemeinsam geoeffnet und gemeinsam angezeigt; extrahiert wird nach `Side0/` und
 *    `Side1/`, eingefuegt nur aus einem Ordner, der genau diese Unterverzeichnisse hat.
 *    Bei EINEM Dateisystem (jede CP/M-Diskette, auch beidseitige) ist der Ordner flach.
 * 2. **Die Ansicht ist immer frisch**: @ref list liest Verzeichnis und Belegung jedes
 *    Mal neu aus dem Medium — es gibt keinen zwischengespeicherten Verzeichnisstand.
 * 3. **Passt es nicht, wird gar nicht erst geschrieben**: Stapeloperationen pruefen den
 *    Platz vorab und nehmen bei einem Fehler die ganze Aenderung zurueck (§9.2).
 * 4. **Format und Dateisystem werden erkannt**; passt ein Abbild zu keinem Eintrag in
 *    `formats.yaml`, bricht das Oeffnen mit einer Meldung ab, die die gemessene
 *    Geometrie nennt (§12).
 *
 * Geschrieben wird immer nur ins Medium; die Datei wird erst durch @ref flush oder
 * @ref saveAs angefasst.
 *
 * @see doc/design/13_k1520disktool.md §9
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/file_system.h"
#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/fs_profile.h"
#include "core/filesystem/sector_space.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @struct FileRef
 * @brief Eindeutige Bezeichnung einer Datei ueber die ganze Diskette.
 *
 * Textform `Side1/HELP.DAT.00` bzw. bei einem Dateisystem schlicht `HELP.DAT.00`.
 * Noetig, weil beide Seiten einer UDOS-Diskette dieselben Dateinamen tragen duerfen —
 * auf `udos_boot_scp.hfe` ist genau das der Normalfall.
 */
struct FileRef {
    int         volume = 0;
    std::string name;

    /// @brief `Side1/NAME` zerlegen; ohne Praefix bleibt @p volume der Vorgabewert.
    static FileRef parse(const std::string& text, int default_volume = 0);
};

/**
 * @struct TransferOptions
 * @brief Steuerung von Extrahieren und Einfuegen.
 */
struct TransferOptions {
    bool text      = false;   ///< Zeilenenden umsetzen (CR LF ↔ LF, UDOS CR ↔ LF)
    bool overwrite = false;   ///< vorhandene Zieldateien ersetzen
    bool dry_run   = false;   ///< nur planen und pruefen, nichts schreiben
};

/**
 * @struct DetectionResult
 * @brief Was beim Oeffnen erkannt wurde — gehoert sichtbar in die Oberflaeche.
 */
struct DetectionResult {
    std::string format;               ///< erkannte Geometrie ("" = nicht erkannt)
    std::string filesystem;           ///< erkanntes Dateisystem ("" = nicht erkannt)
    bool        unambiguous = true;   ///< false = mehrere gleich gute Kandidaten
    std::vector<std::string> alternatives;   ///< weitere Dateisystem-Kandidaten
    std::string remarks;              ///< Auffaelligkeiten (Altbestand, CRC, Schaeden)
};

/**
 * @class DiskVolume
 * @brief Gemountete Diskette mit allen ihren Dateisystemen.
 */
class DiskVolume {
public:
    ~DiskVolume();
    DiskVolume(const DiskVolume&)            = delete;
    DiskVolume& operator=(const DiskVolume&) = delete;

    /**
     * @brief Diskette oeffnen.
     * @param path     `.img`, `.hfe` oder `.dmk`
     * @param fs_name  Dateisystem erzwingen; "" = erkennen (§12)
     * @param formats  Formatkatalog
     * @param fs_cat   Dateisystemkatalog
     * @param err      Grund, wenn nullptr zurueckkommt — bei „kein Format passt"
     *                 enthaelt er die gemessene Geometrie im Klartext
     */
    static std::unique_ptr<DiskVolume> open(const std::string& path,
                                            const std::string& fs_name,
                                            const FormatCatalog& formats,
                                            const FsCatalog& fs_cat,
                                            std::string& err);

    // ─── Auskunft ────────────────────────────────────────────────────────────

    const std::string&     path()      const { return path_; }
    const DetectionResult& detection() const { return detection_; }
    const FsProfile&       profile()   const { return *profile_; }

    int  volumeCount() const { return static_cast<int>(volumes_.size()); }
    /// @brief Unterverzeichnisname: "" bei einem Volume, sonst "Side0"/"Side1".
    std::string volumeDir(int v) const;
    FsInfo      volumeInfo(int v) const;
    /// @brief Volume-Index zu einem Ordnernamen; -1 = passt zu keinem.
    int         volumeFromDir(const std::string& dir_name) const;

    /// @brief Verzeichnis ALLER Volumes — immer frisch aus dem Medium (§9.3).
    std::vector<FileEntry> list() const;

    // ─── Einzeloperationen ───────────────────────────────────────────────────

    bool extract(const FileRef& ref, const std::string& dest_path, const TransferOptions&);
    bool insert (const std::string& src_path, const FileRef& ref, const TransferOptions&);
    bool erase  (const FileRef& ref);

    // ─── Stapeloperationen (transaktional, §9.2) ─────────────────────────────

    /**
     * @brief Alles in @p dest_dir extrahieren.
     *
     * Bei mehreren Volumes werden `Side0/`, `Side1/` … angelegt — auch wenn eine Seite
     * leer ist (ein leerer Ordner ist die ehrliche Auskunft).
     */
    bool extractAll(const std::string& dest_dir, const TransferOptions&);

    /**
     * @brief Den Inhalt von @p src_dir einfuegen.
     *
     * Bei mehreren Volumes MUSS der Ordner genau die `SideN/`-Unterverzeichnisse
     * enthalten; fehlt eines oder liegen lose Dateien daneben, ist das ein Fehler und
     * die Diskette bleibt unveraendert.  Bei einem Volume ist der Ordner flach.
     */
    bool insertAll(const std::string& src_dir, const TransferOptions&);

    /// @brief Wuerde @p src_dir passen?  Schreibt nichts (§9.2, Schritt 1+2).
    bool checkFit(const std::string& src_dir, std::string& bericht) const;

    // ─── Dateibindung ────────────────────────────────────────────────────────

    bool dirty() const;
    bool flush();
    bool saveAs(const std::string& path);

    const std::string& lastError() const { return last_error_; }

private:
    DiskVolume() = default;

    /// @brief Ein Dateisystem samt seinem Sektorraum.
    struct Vol {
        std::unique_ptr<SectorSpace> space;
        std::unique_ptr<FileSystem>  fs;
        uint8_t                      head = 0;
    };

    bool fail(const std::string& why) const { last_error_ = why; return false; }
    bool valid(int v) const { return v >= 0 && v < static_cast<int>(volumes_.size()); }

    /// @brief Erwartete Unterverzeichnisse pruefen und Dateien je Volume einsammeln.
    bool sammleQuelldateien(const std::string& src_dir,
                            std::vector<std::vector<std::string>>& je_volume) const;

    std::string        path_;
    std::unique_ptr<DiskImage> disk_;
    const DiskFormat*  format_  = nullptr;
    const FsProfile*   profile_ = nullptr;
    DetectionResult    detection_;
    std::vector<Vol>   volumes_;
    mutable std::string last_error_;
};
