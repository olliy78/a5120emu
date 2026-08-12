/**
 * @file floppy_drive2.h
 * @brief FloppyDriveV2 – physisches Laufwerk (DriveProfile) + gemountete Diskette.
 *
 * Hält ein @ref DriveProfile (welches physische Laufwerk am Slot hängt), die mechanische
 * Kopfposition und die gemountete @ref DiskImage.  Der Controller (@ref K5122) bezieht
 * über @ref track() einen fertigen @ref TrackImage und kennt keine Sektoren/Offsets.
 *
 * **Kein Spur-Cache mehr:** seit dem Medium-Umbau (2026-08-05) liegt die gesamte
 * Diskette als @ref DiskMedium im Speicher; das Laufwerk referenziert sie direkt.
 * Das Zurückschreiben in die Datei übernimmt der Autosave der @ref DiskImage.
 *
 * @see doc/design/09_floppy_drive.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_image.h"
#include "drive_profile.h"
#include "track_image.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @enum TrackPitch
 * @brief Verhältnis der **Spurdichte** von Diskette und Laufwerk.
 *
 * 5,25″ kennt zwei Dichten: 48 tpi (40 Spuren über den ganzen Radius) und 96 tpi
 * (80 Spuren).  Welche eine Diskette trägt, verrät ihre Spurzahl; welche das
 * Laufwerk abfährt, steht im @ref DriveProfile.  Stimmen sie nicht überein, ist das
 * **kein Fehler**, sondern ein Übersetzungsverhältnis zwischen Kopfposition und
 * Diskettenspur — genau so, wie eine 40-Spur-Diskette in einem 80-Spur-Laufwerk
 * seit jeher „schrittverdoppelt" gelesen wird.
 *
 * @see doc/design/09_floppy_drive.md §7.2
 */
enum class TrackPitch : uint8_t {
    Direct,      ///< gleiche Dichte — Kopfposition n liegt über Diskettenspur n
    DoubleStep,  ///< 48-tpi-Diskette im 96-tpi-Laufwerk: Position 2n = Spur n
    HalfStep     ///< 96-tpi-Diskette im 48-tpi-Laufwerk: Position n = Spur 2n
};

/**
 * @class FloppyDriveV2
 * @brief Physisches Laufwerk (Profil) + Kopfposition + gemountete Diskette.
 */
class FloppyDriveV2 {
public:
    /// @brief Profil = physisches Laufwerk am Slot (Maschinenkonfiguration, §3.A).
    explicit FloppyDriveV2(DriveProfile profile = {});

    /**
     * @brief Mountet eine Diskette und **passt sie ans Laufwerk an**.
     *
     * Eine Diskette, die nicht zur Spurdichte oder Seitenzahl des Laufwerks passt,
     * wird nicht abgewiesen, sondern übersetzt (@ref TrackPitch, @ref side0Only) —
     * das Laufwerk tut, was die Mechanik täte.  Was dabei eingeschränkt ist, steht
     * anschließend in @ref notices() und gehört vor die Augen des Bedieners.
     *
     * Abgewiesen wird nur, was das Laufwerk wirklich nicht kann: ein Verfahren, das
     * es nicht beherrscht, und Spuren mit Daten jenseits seiner Reichweite.
     *
     * @return false (Grund über lastError()) bei Inkompatibilität oder leerem Zeiger.
     */
    bool mount(std::unique_ptr<DiskImage> img, bool write_protect = false);
    void unmount();                       ///< flush() + Reset

    bool isMounted()      const { return image_ != nullptr; }
    bool isWriteProtect() const { return write_protect_; }
    void setWriteProtect(bool wp);
    const char* lastError() const { return last_error_.c_str(); }
    void setLastError(std::string msg) { last_error_ = std::move(msg); }

    /// @brief Gemountete Diskette (nullptr, wenn leer) — für Speichern-unter/Statusabfragen.
    DiskImage*       image()       { return image_.get(); }
    const DiskImage* image() const { return image_.get(); }

    bool    step(bool inward);            ///< begrenzt durch profile_.num_cyls
    bool    seek(uint8_t cyl);
    uint8_t currentCylinder() const { return cur_cyl_; }

    /**
     * @brief Snapshot-Restore: setzt die mechanische Kopfposition (@p cyl) direkt.
     *
     * Anders als seek() wird NICHT geflusht: beim loadstate gehört der bisherige
     * Zustand zu einer verworfenen Sitzung.
     */
    void restoreHeadPosition(uint8_t cyl) {
        cur_cyl_ = (cyl < profile_.num_cyls) ? cyl
                                             : static_cast<uint8_t>(profile_.num_cyls - 1);
    }

    const DriveProfile& profile() const { return profile_; }
    /// @brief Index-Periode in Z80-Takten aus profile_.rpm.
    int indexPeriodCycles(uint32_t cpu_hz) const { return profile_.indexPeriodCycles(cpu_hz); }

    // ── Anpassung der Diskette ans Laufwerk (beim Mount festgelegt) ───────────

    /// @brief Spurdichte-Verhältnis der eingelegten Diskette zu diesem Laufwerk.
    TrackPitch pitch() const { return pitch_; }
    /// @brief true = zweiseitige Diskette in einem einseitigen Laufwerk (Kopf 1 fehlt).
    bool side0Only() const { return side0_only_; }
    /// @brief Je ein kurzer Satz pro Einschränkung; leer = die Diskette passt.
    const std::vector<std::string>& notices() const { return notices_; }
    /// @brief Dieselben Sätze als ein Text (Zeilen durch `\n` getrennt).
    std::string noticeText() const;

    /**
     * @brief Welche **Diskettenspur** liegt unter der Kopfposition @p pos?
     *
     * Der einzige Ort, an dem @ref TrackPitch ausgerechnet wird; alle Spurzugriffe
     * gehen hier durch.  Bei `DoubleStep` liegt zwischen zwei Diskettenspuren eine
     * Kopfposition, unter der **nichts** ist — der Fall, für den der Gast
     * schrittverdoppeln muss.
     *
     * @return Zylindernummer auf der Diskette, oder **-1**, wenn dort keine Spur liegt.
     */
    int mediumCylinder(uint8_t pos) const;

    /// @brief Erreicht dieses Laufwerk den Kopf @p head überhaupt?
    bool headReachable(uint8_t head) const { return head < 2 && head < profile_.num_heads; }

    /**
     * @brief Aktuelle Spur (cur_cyl_, @p head) als TrackImage — Referenz aufs Medium.
     * @return leeres TrackImage, wenn nichts gemountet oder die Spur unformatiert ist.
     */
    const TrackImage& track(uint8_t head) const;

    /// @brief Markiert die Spur (cur_cyl_, head) als verändert.
    void markTrackDirty(uint8_t head);
    /// @brief Direkter, modifizierbarer Zugriff auf die aktuelle Spur (Schreib-Patches).
    TrackImage& mutableTrack(uint8_t head);

    /// @brief Geänderte Spuren sofort in die gebundene Datei schreiben.
    bool flush();
    /// @brief Verzögerter Autosave (@ref DiskImage::autoFlush).
    bool autoFlush(uint64_t now_cycles);

    /**
     * @brief Schreibt eine fertige Spur an eine EXPLIZITE (cyl, head)-Position.
     *
     * Nötig beim Vollspur-FORMAT, bei dem der Kopf zum Commit-Zeitpunkt bereits zur
     * nächsten Spur weitergeschritten ist.
     *
     * @return false bei nicht gemountetem / schreibgeschütztem Laufwerk.
     */
    bool writeTrackAt(uint8_t cyl, uint8_t head, const TrackImage& track);

    DiskGeometry geometry() const;

private:
    /// @brief Warnt **einmal je Position/Kopf**, wenn ein Schreibzugriff ins Leere geht.
    void warnLostWrite(uint8_t head) const;

    DriveProfile               profile_;
    std::unique_ptr<DiskImage> image_;
    bool        write_protect_ = false;
    uint8_t     cur_cyl_       = 0;
    std::string last_error_;

    // Beim Mount festgelegte Anpassung an dieses Laufwerk.
    TrackPitch               pitch_      = TrackPitch::Direct;
    bool                     side0_only_ = false;
    std::vector<std::string> notices_;
    mutable int              last_lost_write_ = -1;  ///< (pos<<1|head) der letzten Warnung
};
