/**
 * @file floppy_drive2.h
 * @brief FloppyDriveV2 – DiskImage-basiertes Laufwerk mit DriveProfile und Track-Cache.
 *
 * Nachfolger des alten @ref FloppyDrive für die neue Streaming-Architektur.  Hält ein
 * @ref DiskImage (statt Pfad+DiskFormat+Inline-IO), kennt sein @ref DriveProfile
 * (welches physische Laufwerk am Slot hängt) und cached die aktuelle Spur je Kopf.
 * Der Controller (@ref K5122v2) bezieht über @ref track() einen fertigen
 * @ref TrackImage und kennt keine Sektoren/Offsets mehr.
 *
 * Der bestehende @ref FloppyDrive bleibt unverändert; diese Klasse existiert parallel,
 * bis die neue Karte die alte in einer Maschinenkonfiguration ablöst.
 *
 * @see doc/refactoring_floppy_emulator.md §8
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
 * @brief Physisches Laufwerk (Profil) + gemountetes DiskImage + 1-Spur-Cache je Kopf.
 */
class FloppyDriveV2 {
public:
    /// @brief Profil = physisches Laufwerk am Slot (Maschinenkonfiguration, §3.A).
    explicit FloppyDriveV2(DriveProfile profile = {});

    /**
     * @brief Mountet ein Image.  Prüft Geometrie + Encoding gegen das DriveProfile.
     * @return false (Grund über lastError()) bei Inkompatibilität oder leerem Image.
     */
    bool mount(std::unique_ptr<DiskImage> img, bool write_protect = false);
    void unmount();                       ///< flush() + Reset

    bool isMounted()      const { return image_ != nullptr; }
    bool isWriteProtect() const { return write_protect_; }
    void setWriteProtect(bool wp) { write_protect_ = wp; }
    const char* lastError() const { return last_error_.c_str(); }

    bool    step(bool inward);            ///< begrenzt durch profile_.num_cyls
    bool    seek(uint8_t cyl);
    uint8_t currentCylinder() const { return cur_cyl_; }

    const DriveProfile& profile() const { return profile_; }
    /// @brief Index-Periode in Z80-Takten aus profile_.rpm.
    int indexPeriodCycles(uint32_t cpu_hz) const { return profile_.indexPeriodCycles(cpu_hz); }

    /**
     * @brief Aktuelle Spur (cur_cyl_, @p head) als TrackImage.  Lazy decodiert + gecached.
     * @return Referenz auf die gecachte Spur (leer, wenn nichts gemountet/Spur fehlt).
     */
    const TrackImage& track(uint8_t head);

    /// @brief Markiert die gecachte Spur (head) als verändert.
    void markTrackDirty(uint8_t head);
    /// @brief Direkter, modifizierbarer Zugriff auf die gecachte Spur (für Schreib-Patches).
    TrackImage& mutableTrack(uint8_t head);

    /// @brief Schreibt dirty-Spur(en) via DiskImage::writeTrack zurück.
    bool flush();

    DiskGeometry geometry() const;

private:
    /// @brief Lädt die Spur (cur_cyl_, head) in den Cache, falls nötig.
    void ensureCached(uint8_t head);

    DriveProfile               profile_;
    std::unique_ptr<DiskImage> image_;
    bool        write_protect_ = false;
    uint8_t     cur_cyl_       = 0;
    std::string last_error_;

    /// 1-Spur-Cache je Kopf (Lesekopf liest jeweils eine Spur).
    struct CachedTrack { TrackImage img; uint8_t cyl = 0xFF; bool dirty = false; bool valid = false; };
    CachedTrack cache_[2];
};
