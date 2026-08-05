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

/**
 * @class FloppyDriveV2
 * @brief Physisches Laufwerk (Profil) + Kopfposition + gemountete Diskette.
 */
class FloppyDriveV2 {
public:
    /// @brief Profil = physisches Laufwerk am Slot (Maschinenkonfiguration, §3.A).
    explicit FloppyDriveV2(DriveProfile profile = {});

    /**
     * @brief Mountet eine Diskette.  Prüft Geometrie + Verfahren gegen das DriveProfile.
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
    DriveProfile               profile_;
    std::unique_ptr<DiskImage> image_;
    bool        write_protect_ = false;
    uint8_t     cur_cyl_       = 0;
    std::string last_error_;
};
