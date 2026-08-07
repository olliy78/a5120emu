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

    /// @brief Spur (cyl, head); leeres TrackImage, wenn außerhalb der Geometrie.
    const TrackImage& track(uint8_t cyl, uint8_t head) const;

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

    /// @brief Trägt mindestens eine Spur Adressmarken? (false = unformatierte Leerdiskette)
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
    bool                    dirty_any_ = false;
    uint64_t                revision_  = 0;   ///< +1 je Spuränderung (Autosave-Ruhepause)

    /// Tri-state-Cache für @ref trackRawCompatible: -1 unbekannt, 0 nein, 1 ja.
    mutable std::vector<int8_t> raw_ok_;
};
