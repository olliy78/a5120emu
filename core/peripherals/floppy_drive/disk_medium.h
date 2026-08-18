/**
 * @file disk_medium.h
 * @brief DiskMedium – DAS interne, bitstrom-orientierte Diskettenabbild.
 *
 * Der Emulator hält eine gemountete Diskette **vollständig im Speicher**: je (Zylinder,
 * Kopf) eine @ref TrackImage, also der decodierte Vollumdrehungsstrom mit Gaps, Sync,
 * Adressmarken (@ref TrackImage::marks), Datenfeldern und **echten CRCs**.  Dateiformate
 * (`.img` / `.hfe` / `.dmk`) sind nur noch **Container-Codecs** davor (@ref ImageCodec);
 * sie arbeiten nie direkt auf der Datei, während das Gastsystem läuft.
 *
 * Daraus folgt, was mit den alten dateigebundenen Backends nicht ging:
 *   - Fremd-Dateisysteme bleiben erhalten (UDOS hängt je Sektor einen Kontrollblock
 *     **hinter** die Daten-CRC — ein rohes Sektorimage verliert ihn, s. @ref rawCompatible);
 *   - das Containerformat ist nachträglich wechselbar (`DiskImage::saveAs`);
 *   - eine **echte Leerdiskette** (unformatierte Spuren) ist darstellbar und kann vom
 *     Gastsystem formatiert werden.
 *
 * @see doc/design/09_floppy_drive.md
 * @see doc/K1520_architecture.md §8.7
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @enum TrackState
 * @brief Verhältnis einer Spur im Abbild zu ihrem Original.
 *
 * Bei einer **datei**gebundenen Diskette gibt es @ref Unknown nicht: der Container-Codec
 * füllt beim Laden jede Spur.  Der Zustand wird erst bei einer **physischen** Diskette
 * interessant, die spurweise nachgeladen wird
 * (doc/design/14_physische_diskette.md §4, doc/design/09_floppy_drive.md §12.2).
 */
enum class TrackState : uint8_t {
    Unknown,  ///< noch nie vom Original gelesen — der Inhalt ist **bedeutungslos**
    Clean,    ///< gelesen und seither nicht geändert
    Dirty     ///< im Abbild geändert, noch nicht zurückgeschrieben
};

/**
 * @class TrackLoader
 * @brief Nachlader für unbekannte Spuren — die Brücke zum echten Laufwerk.
 *
 * Implementiert von @ref TrackSync.  Das Medium kennt hiervon nur die zwei Methoden;
 * es weiß nichts über Warteschlangen, Fäden, USB oder Greaseweazle.
 */
class TrackLoader {
public:
    virtual ~TrackLoader() = default;

    /// @brief Spur beschaffen — **blockiert**, bis sie da ist (oder es scheitert).
    /// @return false, wenn sie nicht beschafft werden konnte (Zeitüberschreitung,
    ///         Gerätefehler, Abmeldung).  Der Aufrufer bekommt dann die leere Spur.
    virtual bool ensureLoaded(uint8_t cyl, uint8_t head) = 0;

    /// @brief Meldung, dass eine Spur im Abbild geändert wurde (Rückführung anmelden).
    virtual void trackChanged(uint8_t cyl, uint8_t head) = 0;
};

/**
 * @struct DiskGeometry
 * @brief Geometrieabfrage für UI/Validierung (Zylinder × Köpfe + vorherrschendes Verfahren).
 */
struct DiskGeometry {
    uint8_t  num_cyls  = 0;
    uint8_t  num_heads = 0;
    bool     uniform   = false;          ///< true = alle Spuren gleich (Info für UI)
    Encoding encoding  = Encoding::MFM;  ///< vorherrschendes Verfahren der Diskette
};

/**
 * @class DiskMedium
 * @brief Alle Spuren einer Diskette als @ref TrackImage, mit Dirty- und `.img`-Tauglichkeit.
 *
 * Speicherbedarf: 80 × 2 Spuren à ~6,3 KB ≈ 1 MB je Laufwerk — unkritisch.
 * Spur-Index = `cyl * num_heads + head`.
 */
class DiskMedium {
public:
    DiskMedium() = default;
    /// @brief Leeres (unformatiertes) Medium in der angegebenen Geometrie.
    DiskMedium(uint8_t num_cyls, uint8_t num_heads, Encoding default_enc);

    /**
     * @brief Geometrie ändern; vorhandene Spuren bleiben an ihrer (cyl, head)-Position.
     *
     * Wird von den Codecs beim Laden benutzt (die Datei bestimmt die Geometrie) und
     * beim Anlegen einer Leerdiskette (das DriveProfile bestimmt sie).
     */
    void resize(uint8_t num_cyls, uint8_t num_heads);

    uint8_t  numCylinders()    const { return num_cyls_; }
    uint8_t  numHeads()        const { return num_heads_; }
    Encoding defaultEncoding() const { return default_enc_; }
    void     setDefaultEncoding(Encoding e) { default_enc_ = e; }

    /// @brief Geometrie + vorherrschendes Verfahren (für UI/Mount-Prüfung).
    DiskGeometry geometry() const;

    /**
     * @brief Spur (cyl, head); leeres TrackImage, wenn außerhalb der Geometrie.
     *
     * **Der Nachladepunkt.**  Hängt ein @ref TrackLoader am Medium und ist die Spur
     * @ref TrackState::Unknown, wird sie hier beschafft — der Aufruf **blockiert** so
     * lange (≈ 0,5–0,8 s an einem echten Laufwerk).  Das ist gewollt: es ist die einzige
     * Stelle, durch die jeder Verbraucher geht (Controller über @ref FloppyDriveV2,
     * DiskTool über `DiskVolume`, Erkennung über `GeometryProbe`).
     *
     * Für medienweite Reihenläufe **@ref peek benutzen** — sonst zieht eine beiläufige
     * Statusabfrage die ganze Diskette ein (doc/design/09_floppy_drive.md §12.3).
     */
    const TrackImage& track(uint8_t cyl, uint8_t head) const;

    /// @brief Wie @ref track, aber **ohne** je nachzuladen (Reihenläufe, Codecs, Anzeige).
    const TrackImage& peek(uint8_t cyl, uint8_t head) const;

    /**
     * @brief Modifizierbarer Zugriff auf eine Spur; markiert sie sofort als geändert.
     *
     * Der Aufrufer darf die Spur in-place patchen (Schreibpfad des Controllers).
     * Liegt (cyl, head) außerhalb der Geometrie, wird eine Dummy-Spur geliefert.
     */
    TrackImage& mutableTrack(uint8_t cyl, uint8_t head);

    /// @brief Spur ersetzen (Formatierlauf, Codec-Ladepfad); markiert sie als geändert.
    void setTrack(uint8_t cyl, uint8_t head, TrackImage t);

    /// @brief Spur als geändert markieren (nach externem Patch über @ref mutableTrack).
    void markDirty(uint8_t cyl, uint8_t head);

    bool dirty() const { return dirty_any_; }
    bool trackDirty(uint8_t cyl, uint8_t head) const;
    /// @brief Alle Dirty-Bits löschen (nach erfolgreichem Speichern).
    void clearDirty();

    /**
     * @brief Änderungszähler — steigt bei **jeder** Spuränderung.
     *
     * Der Autosave (@ref DiskImage::autoFlush) erkennt daran, ob seit der letzten
     * Prüfung noch geschrieben wurde, und wartet damit auf eine **Schreibpause**
     * statt blind alle n Takte die ganze Datei neu zu codieren.  Ohne das schriebe
     * ein Formatier-/Kopierlauf (hunderte Spuren am Stück) die Image-Datei dutzendfach.
     */
    uint64_t revision() const { return revision_; }

    // ─── Spurzustand (physische Quelle, doc/design/14_physische_diskette.md) ────

    /// @brief Nachlader anmelden/abmelden; @p loader darf nullptr sein (Dateibindung).
    void setLoader(TrackLoader* loader) { loader_ = loader; }
    TrackLoader* loader() const { return loader_; }

    TrackState state(uint8_t cyl, uint8_t head) const;

    /// @brief true, wenn **keine** Spur mehr unbekannt ist.
    ///
    /// Nur dann sind @ref formatted und @ref rawCompatible endgültige Aussagen über die
    /// Diskette; solange nicht, sind sie Aussagen über das bisher Gelesene.
    bool complete() const;

    /// @brief Zahl der noch unbekannten Spuren (Anzeige „x von y gelesen").
    size_t unknownCount() const;

    /**
     * @brief Alle Spuren für unbekannt erklären — beim Binden an eine physische Quelle.
     *
     * Der Inhalt bleibt stehen (er ist ohnehin bedeutungslos), damit die Geometrie und
     * die Puffer nicht neu gebaut werden müssen.
     */
    void markAllUnknown();

    /**
     * @brief Ladepfad des Nachladers: Inhalt setzen und die Spur als @ref TrackState::Clean
     *        führen — **ohne** Dirty-Markierung und ohne @ref revision zu erhöhen.
     *
     * Der Unterschied zu @ref setTrack ist der springende Punkt: eine frisch **gelesene**
     * Spur ist nicht geändert, sie darf also nicht zurückgeschrieben werden.
     */
    void loadTrack(uint8_t cyl, uint8_t head, TrackImage t);

    /// @brief Eine einzelne Spur für sauber erklären (Rückführung erledigt).
    void clearTrackDirty(uint8_t cyl, uint8_t head);

    /**
     * @brief Zustand aus einer Momentaufnahme zurückholen (Transaktions-Rücknahme).
     *
     * Anders als eine schlichte Zuweisung markiert dies jede Spur, deren Inhalt sich
     * dadurch **ändert**, als geändert.  Bei einer physischen Diskette ist das
     * unerlässlich: was schon auf der echten Diskette steht, holt keine Kopie im
     * Speicher zurück — nur die Rückführung stellt es richtig
     * (doc/design/09_floppy_drive.md §11.3).
     */
    void restoreFrom(const DiskMedium& snapshot);

    /// @brief Trägt mindestens eine Spur Adressmarken? (false = unformatierte Leerdiskette)
    ///
    /// Urteilt über das **bisher Bekannte** — siehe @ref complete.
    bool formatted() const;

    /**
     * @brief Ist diese Spur als rohes Sektorimage (`.img`) darstellbar?
     *
     * Kriterien (siehe doc/design/09_floppy_drive.md §5):
     *   1. mindestens ein Sektor (@ref TrackCodec::parseTrack),
     *   2. alle ID- und Daten-CRCs gültig,
     *   3. hinter jeder Daten-CRC ausschließlich Gap-Füllbytes (0x4E / 0xFF / 0x00).
     *
     * Punkt 3 ist der Auslöser: UDOS schreibt dort seinen Sektorkontrollblock
     * (Verkettungszeiger + eigene CRC), der in einem `.img` ersatzlos verschwände.
     * Das Ergebnis wird je Spur gecacht und bei Änderung der Spur verworfen.
     */
    bool trackRawCompatible(uint8_t cyl, uint8_t head) const;

    /**
     * @brief Darf das gesamte Medium als `.img` gespeichert werden?
     *
     * true, wenn das Medium überhaupt @ref formatted ist **und jede nicht-leere Spur**
     * @ref trackRawCompatible ist.  **Leere (unformatierte) Spuren sind zulässig** —
     * sie erscheinen im `.img` als Füllbytes; viele echte Images tragen ein bis drei
     * leere Zusatzspuren am Ende.  Eine **komplett** unformatierte Diskette (Leerdiskette)
     * ist dagegen nie `.img`-fähig: ein rohes Sektorimage kann „nicht formatiert" nicht
     * ausdrücken.
     */
    bool rawCompatible() const;

    /// @brief Grund, warum das Medium nicht `.img`-fähig ist (für Fehlermeldungen).
    /// @return "" wenn alles passt, sonst z. B. "Spur 12/1" oder "unformatiert".
    std::string rawIncompatibleReason() const;

private:
    size_t index(uint8_t cyl, uint8_t head) const {
        return static_cast<size_t>(cyl) * num_heads_ + head;
    }
    bool valid(uint8_t cyl, uint8_t head) const {
        return cyl < num_cyls_ && head < num_heads_;
    }
    /// @brief Rechnet @ref trackRawCompatible für eine Spur aus (ohne Cache).
    static bool computeRawCompatible(const TrackImage& t);

    uint8_t  num_cyls_    = 0;
    uint8_t  num_heads_   = 0;
    Encoding default_enc_ = Encoding::MFM;

    std::vector<TrackImage> tracks_;
    std::vector<uint8_t>    dirty_;       ///< je Spur 0/1
    /// je Spur 0/1 — 0 = @ref TrackState::Unknown.  Vorgabe **1**: eine aus einer Datei
    /// geladene oder frisch angelegte Diskette ist vollständig bekannt.
    std::vector<uint8_t>    known_;
    TrackLoader*            loader_    = nullptr;   ///< nullptr = Dateibindung
    bool                    dirty_any_ = false;
    uint64_t                revision_  = 0;   ///< +1 je Spuränderung (Autosave-Ruhepause)

    /// Tri-state-Cache für @ref trackRawCompatible: -1 unbekannt, 0 nein, 1 ja.
    mutable std::vector<int8_t> raw_ok_;
};
