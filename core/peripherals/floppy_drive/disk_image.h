/**
 * @file disk_image.h
 * @brief DiskImage – die *gemountete* Diskette: internes Medium + Dateibindung.
 *
 * `DiskImage` ist seit dem Medium-Umbau (2026-08-05) **konkret**: es hält ein
 * @ref DiskMedium (das vollständige, bitstrom-orientierte Abbild im Speicher) und die
 * Bindung an eine Image-Datei.  Die früheren dateigebundenen Backends
 * (`RawSectorImage`, `HfeImage`) sind durch die Container-Codecs (@ref ImageCodec)
 * ersetzt; der Controller sieht weiterhin nur @ref TrackImage.
 *
 * Verhalten der Bindung:
 *   - **Autosave**: geänderte Spuren werden **verzögert** (≈ 0,5 s Maschinenzeit) in die
 *     gebundene Datei zurückgeschrieben, sodass die Datei dem Abbild stets mit leichtem
 *     Zeitversatz entspricht (@ref autoFlush).
 *   - **Umbinden**: @ref saveAs schreibt das Medium in einen **beliebigen** Container
 *     und bindet die Diskette ab da an die neue Datei.
 *   - **Ohne Datei**: eine frisch angelegte Leerdiskette (@ref createBlank) lebt nur im
 *     Speicher, bis sie das erste Mal gespeichert wird.
 *
 * @see doc/design/09_floppy_drive.md §6
 * @see doc/K1520_architecture.md §8.7
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_format.h"
#include "disk_medium.h"
#include "image_codec.h"
#include "track_image.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

/// @brief Maschinentakte, die eine Änderung „ruhen" muss, bevor der Autosave zuschlägt.
///        ≈ 0,5 s bei 2,45 MHz — fasst Schreibbursts (FORMAT, COPY) zusammen.
inline constexpr uint64_t kAutoFlushDelayCycles = 1'225'000;

/**
 * @class DiskImage
 * @brief Gemountete Diskette: @ref DiskMedium + Dateibindung + Schreibschutz.
 */
class DiskImage {
public:
    DiskImage() = default;
    /// @brief Schreibt ausstehende Änderungen noch weg (Unmount, Maschinenende).
    ~DiskImage() { flush(); }

    DiskImage(const DiskImage&)            = delete;
    DiskImage& operator=(const DiskImage&) = delete;

    // ─── Fabriken ────────────────────────────────────────────────────────────

    /**
     * @brief Vorhandene Image-Datei laden und binden.
     *
     * Der Container wird per @ref ImageCodec::detect bestimmt (HFE-Signatur, DMK-Header,
     * sonst `.img`).  @p fmt ist **nur** für `.img` nötig — `.hfe`/`.dmk` sind
     * self-describing.
     *
     * @return DiskImage oder nullptr bei Fehler (Datei/Format/Signatur).
     */
    static std::unique_ptr<DiskImage> open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect);

    /**
     * @brief **Echte Leerdiskette**: unformatiertes Medium in Laufwerksgeometrie, ohne Datei.
     *
     * Alle Spuren sind leer (keine Adressmarken).  Der Controller streamt dafür
     * markenlosen Gap-Flux, das Gastsystem läuft in den Index-Timeout und kann die
     * Diskette formatieren — inklusive Fremdformaten wie UDOS, die Nutzdaten hinter
     * die Daten-CRC hängen.
     *
     * @param num_cyls    Zylinder des aufnehmenden Laufwerks (K5601 80, K5600.10 40, …)
     * @param num_heads   Köpfe des aufnehmenden Laufwerks
     * @param default_enc Vorschlagsverfahren (Container-Header); je Spur überschreibbar
     */
    static std::unique_ptr<DiskImage> createBlank(uint8_t num_cyls, uint8_t num_heads,
                                                  Encoding default_enc = Encoding::MFM);

    /**
     * @brief **Vorformatierte** Leerdiskette anlegen und als Datei binden.
     *
     * Erzeugt je Spur eine gültige IBM-Spur (echte IDAM/DATA-Felder mit CRC, Nutzdaten
     * `0xE5`) nach @p fmt und schreibt sie in den durch die Endung bestimmten Container.
     * Für eine **unformatierte** Diskette stattdessen @ref createBlank benutzen.
     *
     * @param path          Zielpfad (`.hfe` / `.dmk` / sonst `.img`); wird überschrieben
     * @param fmt           DiskFormat (Pflicht: Geometrie + Sektorlayout)
     * @param write_protect Schreibschutz des Ergebnisses
     * @param enc           Rückfallverfahren für Formate ohne Spurbereichsangabe
     */
    static std::unique_ptr<DiskImage> create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect,
                                             Encoding enc = Encoding::MFM);

    // ─── Medium ──────────────────────────────────────────────────────────────

    DiskMedium&       medium()       { return medium_; }
    const DiskMedium& medium() const { return medium_; }

    DiskGeometry geometry() const { return medium_.geometry(); }

    /// @brief Darf das Gastsystem schreiben? (kein Schreibschutz)
    bool writable() const { return !write_protect_; }
    void setWriteProtect(bool wp) { write_protect_ = wp; }

    /// @brief Darf das Medium als rohes Sektorimage gespeichert werden? (§5 des Feinentwurfs)
    bool rawCompatible() const { return medium_.rawCompatible(); }

    /// @brief Spur (cyl, head) — Referenz auf das interne Abbild.
    const TrackImage& readTrack(uint8_t cyl, uint8_t head) const {
        return medium_.track(cyl, head);
    }

    /// @brief Spur ersetzen; markiert sie als geändert (Autosave übernimmt später).
    bool writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track);

    // ─── Dateibindung ────────────────────────────────────────────────────────

    bool                 hasFile()   const { return !path_.empty(); }
    const std::string&   path()      const { return path_; }
    ContainerType        container() const { return container_; }
    /// @brief Gebundenes DiskFormat (nur bei `.img` gesetzt), sonst nullptr.
    const DiskFormat*    diskFormat() const {
        return disk_format_.has_value() ? &disk_format_.value() : nullptr;
    }
    /// @brief false, wenn die Quelle nicht zurückgeschrieben werden darf
    ///        (überabgetasteter Flux-Mitschnitt, schreibgeschützte Datei).
    bool bindingWritable() const { return binding_writable_; }

    /**
     * @brief Geänderte Spuren in die gebundene Datei schreiben.
     * @return true auch dann, wenn nichts zu tun war (keine Bindung / nicht schmutzig).
     */
    bool flush();

    /**
     * @brief Verzögerter Autosave — schreibt nach einer **Schreibpause**.
     *
     * Flusht, sobald seit der letzten Spuränderung mindestens
     * @ref kAutoFlushDelayCycles Takte vergangen sind (erkannt über
     * @ref DiskMedium::revision).  Ein laufender Formatier-/Kopiervorgang schiebt den
     * Zeitpunkt also vor sich her, statt die ganze Datei dutzendfach neu zu codieren.
     * Wird aus der Laufschleife der Maschine aufgerufen.
     *
     * @param now_cycles aktueller Stand der Maschinenuhr
     * @return true, wenn tatsächlich geschrieben wurde
     */
    bool autoFlush(uint64_t now_cycles);

    /**
     * @brief Unter neuem Namen/Container speichern und **umbinden**.
     *
     * Der Container ergibt sich aus der Endung von @p path.  @p fmt wird nur für `.img`
     * gebraucht; dort wird zusätzlich geprüft, ob das Medium überhaupt sektorweise
     * darstellbar ist (@ref rawCompatible) und zum Format passt.
     *
     * @return false mit Grund in @ref lastError (z. B. UDOS-Anhang, unformatierte Diskette).
     */
    bool saveAs(const std::string& path, std::optional<DiskFormat> fmt);

    /**
     * @brief Kopie in einen beliebigen Container schreiben — **ohne umzubinden**.
     *
     * Wie @ref saveAs, aber die Diskette bleibt an ihre bisherige Datei gebunden und
     * ihr Dirty-Zustand unveraendert.  Fuer Exporte, die den Arbeitsstand nicht
     * antasten sollen (Archivierung, Formatumwandlung zur Ansicht).
     */
    bool exportTo(const std::string& path, std::optional<DiskFormat> fmt);

    const char* lastError() const { return last_error_.c_str(); }

private:
    DiskMedium    medium_;
    std::string   path_;                              ///< "" = nur im Speicher
    ContainerType container_       = ContainerType::Hfe;
    std::optional<DiskFormat> disk_format_;           ///< nur bei .img
    bool          write_protect_   = false;
    bool          binding_writable_ = true;
    std::string   last_error_;

    /// Maschinentakt der zuletzt BEOBACHTETEN Spuränderung (0 = sauber) …
    uint64_t      dirty_since_        = 0;
    /// … und die dabei gesehene @ref DiskMedium::revision (erkennt weiteres Schreiben).
    uint64_t      last_seen_revision_ = 0;
};
