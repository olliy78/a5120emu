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
 * 4. **Format und Dateisystem werden erkannt** — notfalls ausgerechnet.  Passt kein
 *    `formats:`-Eintrag, wird die Geometrie aus dem Abbild VERMESSEN
 *    (@ref GeometryProbe::synthesize) und die Diskette **schreibgeschuetzt** geoeffnet;
 *    passt kein `filesystems:`-Eintrag, rechnet @ref CpaDpbRule den DPB aus, wie es das
 *    CP/A-BIOS beim LOGIN tut.  Erst wenn auch das nichts hergibt, bricht das Oeffnen
 *    ab — mit einer Meldung, die die gemessene Geometrie nennt (§12).
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
#include "core/peripherals/floppy_drive/track_view.h"

#include <memory>
#include <optional>
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

    /// @brief UDOS-Kopfsektorangaben, die eine Linux-Datei nicht mitbringt:
    ///        Typ ("A"/"P"/"P1"/"B"), Eigenschaften ("WS") und Startadresse.
    ///        Leer = Vorgabe (A/B, keine Eigenschaften, keine Startadresse).
    ///        Ohne sie wird aus einer UDOS-Systemdatei eine gewoehnliche
    ///        Binaerdatei — und die Diskette bootet nicht (§13a).
    std::string udos_type;
    std::string udos_properties;
    uint16_t    udos_entry = 0;
    /// @brief Satzlaenge (Vielfaches von 128); 0 = 128.
    uint16_t    udos_record_len = 0;
    /// @brief Ladeadresse und Laenge des Speicherabbilds (Typ P/P1); 0 = keine.
    uint16_t    udos_segment = 0;
    uint16_t    udos_segment_len = 0;
    /// @brief Speicheranforderung (Start/Ende/Kennzeichen, Kopfsektor ab Offset 122).
    uint16_t    udos_low_addr = 0;
    uint16_t    udos_high_addr   = 0;
    uint16_t    udos_stack_size = 0;
    /// @brief Zweite Laengenangabe (Kopfsektor Offset 17); 0 ist ein gueltiger Wert,
    ///        deshalb das Kennzeichen daneben.
    uint16_t    udos_block_len = 0;
    bool        udos_block_len_gesetzt = false;
    /// @brief „Bytes im letzten Satz" (Kopfsektor Offset 22); 0 = ausrechnen.
    uint16_t    udos_bytes_in_last = 0;
    /// @brief Kopfsektor Offset 44–47 (Bedeutung offen) und Erstellungsvermerk
    ///        (6 Zeichen, bei Systemdateien ein Versionstext wie "V 4.3 ").
    uint32_t    udos_extra = 0;
    std::string udos_created;
    /// @brief Datum der letzten Aenderung ("JJMMTT"); leer = heute.
    std::string udos_modified;

    // ── nur CP/M: die drei Attributbits, die eine Linux-Datei nicht mitbringt ─
    //
    // Sie stehen in den Hochbits des Dateityps.  Ohne sie wird aus einer
    // Systemdatei (`SYS`, im `DIR` unsichtbar) eine gewoehnliche Datei und aus
    // einer schreibgeschuetzten eine beschreibbare.
    bool cpm_read_only = false;
    bool cpm_system    = false;
    bool cpm_archived  = false;
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
     * @param read_only  **Vorgabe: schreibgeschuetzt.**  Beim blossen Lesen soll eine
     *                   Diskette gar nicht kaputtgehen koennen; der Schutz wirkt bis in
     *                   @ref DiskImage hinein (dessen `flush()` schreibt dann nichts,
     *                   auch nicht aus dem Destruktor).  Schreiben verlangt einen
     *                   bewussten Schritt: @ref setReadOnly(false).
     */
    static std::unique_ptr<DiskVolume> open(const std::string& path,
                                            const std::string& fs_name,
                                            const FormatCatalog& formats,
                                            const FsCatalog& fs_cat,
                                            std::string& err,
                                            bool read_only = true);

    /**
     * @brief **Neue, leere** Diskette anlegen: formatieren + Dateisystem initialisieren.
     *
     * Legt das Abbild in der Geometrie des Profils an (echte Adressmarken und CRCs)
     * und initialisiert darauf das Dateisystem — bei beidseitigem UDOS auf **beiden**
     * Seiten.  Die Datei wird sofort geschrieben.
     *
     * @param path     Zielpfad; die Endung bestimmt den Container
     * @param fs_name  Dateisystem aus dem Katalog (Pflicht — hier gibt es nichts zu erkennen)
     * @param label    Datentraegername (UDOS); "" = Vorgabe
     * @param boot_image  Pfad einer `.bin`-Datei mit dem **Bootabbild** fuer die
     *                    Systemspuren; "" = nicht bootfaehig (§ Bootabbild unten).
     *                    Passt sie nicht, wird **gar nichts angelegt** — die Pruefung
     *                    laeuft vor dem Formatieren.
     */
    static std::unique_ptr<DiskVolume> create(const std::string& path,
                                              const std::string& fs_name,
                                              const std::string& label,
                                              const FormatCatalog& formats,
                                              const FsCatalog& fs_cat,
                                              std::string& err,
                                              const std::string& boot_image = {});

    // ─── Bootabbild (Systemspuren) ───────────────────────────────────────────
    //
    // Bootfaehig wird eine Diskette nicht durch ihr Dateisystem, sondern durch die
    // **Systemspuren** davor: das Lade-ROM liest Spur 0 blind ein, bevor es irgendein
    // Dateisystem gibt.  Diese Spuren gehoeren keiner Datei und werden vom Dateisystem
    // nie angefasst — das Werkzeug behandelt sie deshalb als EIN Byteband:
    //
    //   * **CP/M** (CP/A, SCPX): alles vor dem Beginn des Dateisystems
    //     (@ref FsProfile::data_cyl / data_head) — bei `cpa780` 15104 Byte.
    //     Faengt das Dateisystem auf Zylinder 0 an (`cpa800`), gibt es keine
    //     Systemspuren und damit keine Bootfaehigkeit.
    //   * **UDOS**: die Spuren 0–2 (Urlader + Nukleus) UND die Bootspur 21
    //     (`doc/udos_diskettenformat.md` §8.6) — je Seite, denn jede Seite ist ein
    //     eigener Datentraeger.  Die Bootspur liegt HINTER den Dateispuren; im
    //     Abbild folgt sie deshalb hinten an (13312 Byte bei 26×128).  Ohne sie
    //     bricht der UDOS-Kaltstart mit „ERROR: 45" ab — der Urlader liest sie.
    //
    // Ein kuerzeres Abbild ist erlaubt (der Rest bleibt formatierte Leerspur); ein
    // laengeres ist ein Fehler, denn es wuerde in das Dateisystem hineinschreiben.

    /**
     * @brief Fassungsvermoegen der Systemspuren in Byte — **ohne** eine Diskette.
     *
     * Damit kann die Oberflaeche schon beim Auswaehlen sagen, ob dieses Dateisystem
     * ueberhaupt bootfaehig sein kann und wie gross das Abbild werden darf.
     * @return 0 = dieses Dateisystem hat keine Systemspuren.
     */
    static uint64_t bootAreaCapacity(const FsProfile& prof, const DiskFormat& fmt);

    /// @brief Fassungsvermoegen der Systemspuren dieser Diskette (0 = keine).
    uint64_t bootAreaSize(int volume = 0) const;

    /// @brief Systemspuren auslesen (Bootabbild aus einer vorhandenen Diskette holen).
    bool readBootImage(std::vector<uint8_t>& out, int volume = 0) const;

    /// @brief Wie @ref readBootImage, aber gleich in eine Datei.
    bool readBootImageToFile(const std::string& path, int volume = 0) const;

    /**
     * @brief Bootabbild in die Systemspuren schreiben.
     *
     * Verlangt eine beschreibbare Diskette; laenger als @ref bootAreaSize ist ein
     * Fehler und laesst die Diskette unveraendert.
     */
    bool writeBootImage(const std::vector<uint8_t>& img, int volume = 0);

    /// @brief Wie @ref writeBootImage, aber aus einer Datei.
    bool writeBootImageFile(const std::string& path, int volume = 0);

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

    /**
     * @brief Kopfsektorangaben einer vorhandenen UDOS-Datei aendern.
     *
     * Fuer die Oberflaeche: Typ, Eigenschaften, ENTRY, Speicherangaben … lassen sich
     * einzeln setzen, ohne die Datei neu zu schreiben.  Leere Felder in @p attrs
     * bleiben unveraendert.  Verlangt eine beschreibbare Diskette.
     */
    bool setAttributes(const FileRef& ref, const UdosAttrs& attrs);

    /**
     * @brief Attribute und Nutzerbereich einer vorhandenen CP/M-Datei aendern.
     *
     * Das Gegenstueck fuer die andere Dateisystemfamilie: R/O, SYS, ARCHIV und der
     * Nutzerbereich.  Nicht gesetzte Kennzeichen in @p attrs bleiben unveraendert;
     * verlangt eine beschreibbare Diskette.
     */
    bool setAttributes(const FileRef& ref, const CpmAttrs& attrs);

    // ─── Sektoransicht (Diskeditor, §19) ─────────────────────────────────────
    //
    // Eine Ebene UNTER dem Dateisystem: hier gibt es keine Dateien, nur Spuren,
    // Sektoren, Gaps und CRCs.  Genau das braucht ein Editor, der eine schadhafte
    // Diskette begutachten oder von Hand reparieren soll.

    /// @brief Ausdehnung des Mediums — was da ist, nicht was das Format vorsieht.
    uint8_t mediumCylinders() const;
    uint8_t mediumHeads()     const;

    /// @brief Eine Spur als lueckenlose Abschnittsfolge (Sektor/Gap/unformatiert).
    TrackView trackView(uint8_t cyl, uint8_t head) const;

    /**
     * @brief Nutzdaten und gespeicherte Daten-CRC eines Sektors.
     * @param index laufende Nummer in der Spur (aus @ref TrackSpan::index)
     */
    bool readSectorAt(uint8_t cyl, uint8_t head, int index,
                      std::vector<uint8_t>& out, uint16_t& crc) const;

    /// @brief Welche Daten-CRC gehoerte zu @p data?  Aendert nichts.
    bool sectorCrcFor(uint8_t cyl, uint8_t head, int index,
                      const std::vector<uint8_t>& data, uint16_t& out) const;

    /**
     * @brief Datenfeld eines Sektors ersetzen.
     *
     * @param crc_woertlich `nullptr` = CRC neu rechnen; sonst wird genau dieser Wert
     *        geschrieben — ein Sektor laesst sich damit absichtlich defekt lassen.
     *
     * Geschrieben wird ins Medium im Speicher; in die Datei kommt es erst mit
     * @ref flush (wie jede andere Aenderung, §5).
     */
    bool writeSectorAt(uint8_t cyl, uint8_t head, int index,
                       const std::vector<uint8_t>& data,
                       const uint16_t* crc_woertlich);

    /// @brief Bytes hinter der Daten-CRC (bei UDOS der 4-Byte-Kontrollblock).
    bool readSectorTail(uint8_t cyl, uint8_t head, int index,
                        std::vector<uint8_t>& out) const;

    /// @brief Sektor loeschen — sein Bereich wird wieder Gap (§19.4).
    bool eraseSectorAt(uint8_t cyl, uint8_t head, int index, uint16_t tail_bytes);

    /**
     * @brief Sektor anlegen; die Lage ergibt sich aus der ID (@ref newSectorPosition).
     * @param mfm  Verfahren; muss zur Spur passen, ausser sie ist noch markenlos.
     */
    bool createSector(uint8_t cyl, uint8_t head, const TrackCodec::NewSectorSpec& spec, bool mfm);

    /// @brief Wo laendete er, und wie viele Bytes belegt er?  Schreibt nichts —
    ///        damit die Oberflaeche VOR dem Anlegen fragen kann, was ueberschrieben wird.
    bool planSector(uint8_t cyl, uint8_t head, const TrackCodec::NewSectorSpec& spec, bool mfm,
                    uint32_t& von, uint32_t& laenge) const;

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

    /**
     * @brief Aenderungen in die gebundene Datei schreiben.
     *
     * Beim **ersten** Schreiben auf eine bestehende Datei wird eine Sicherungskopie
     * `<name>~` angelegt (§14.2) — fremde Diskettenabbilder sind oft Einzelstuecke.
     * Mit @ref setBackup abschaltbar.
     */
    bool flush();

    /// @brief Unter neuem Namen/Container speichern und **umbinden** (ab da wird dort
    ///        gearbeitet).  Auch bei Schreibschutz erlaubt — die Quelle bleibt heil.
    bool saveAs(const std::string& path);

    /// @brief Kopie schreiben, **ohne** umzubinden (Archivierung, Formatumwandlung).
    ///        Auch bei Schreibschutz erlaubt.
    bool exportImage(const std::string& path) const;

    // ─── Schreibschutz ───────────────────────────────────────────────────────

    /// @brief Schreibgeschuetzt?  Vorgabe beim Oeffnen: **ja**.
    bool readOnly() const { return read_only_; }

    /// @brief Ist der Schreibschutz UNAUFHEBBAR (nur gemessene Geometrie)?
    bool readOnlyForced() const { return nur_lesen_erzwungen_; }

    /**
     * @brief Schreibschutz setzen/aufheben.
     *
     * Wirkt zweifach: dieses Objekt weist jede aendernde Anforderung ab, und das
     * darunterliegende @ref DiskImage bekommt seinen eigenen Schreibschutz — selbst
     * ein Fehler hier kann die Datei dann nicht mehr anfassen.
     */
    void setReadOnly(bool ro);

    void setBackup(bool an) { backup_ = an; }
    bool backup() const     { return backup_; }

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
    /// @brief Nach der CP/A-Regel abgeleitetes Profil (steht in keinem Katalog).
    ///        Gesetzt, wenn kein benanntes Profil passte; @ref profile_ zeigt dann hierher.
    std::optional<FsProfile> abgeleitet_;
    /// @brief Aus dem Abbild VERMESSENE Geometrie (steht in keinem Katalog).
    ///        Gesetzt, wenn kein `formats:`-Eintrag passte; @ref format_ zeigt dann hierher.
    std::optional<DiskFormat> gemessenes_format_;
    DetectionResult    detection_;
    std::vector<Vol>   volumes_;
    bool               read_only_   = true;
    /// @brief Harter Schreibschutz: die Geometrie ist nur gemessen, nicht katalogisiert.
    ///        @ref setReadOnly(false) verweigert dann (siehe @ref gemessenes_format_).
    bool               nur_lesen_erzwungen_ = false;
    bool               backup_      = true;
    bool               backup_getan_= false;
    mutable std::string last_error_;
};
