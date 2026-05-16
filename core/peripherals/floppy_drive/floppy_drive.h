#pragma once
#include "format_parser.h"
#include <string>
#include <vector>
#include <cstdint>

class FloppyDrive {
public:
    FloppyDrive() = default;

    // Mount a disk image file using the given format.
    bool mount(const std::string& image_path, const DiskFormat& fmt,
               bool write_protect = false);

    void unmount();

    bool isMounted()      const { return mounted_; }
    bool isWriteProtect() const { return write_protect_; }
    void setWriteProtect(bool wp) { write_protect_ = wp; }

    const DiskFormat& format() const { return fmt_; }
    const std::string& imagePath() const { return image_path_; }

    // Seek to cylinder; step() moves in/out by one track.
    bool seekTrack(uint8_t cyl);
    bool step(bool inward);         // inward=true → higher cylinder number
    uint8_t currentCylinder() const { return cur_cyl_; }

    // Read/write a sector by (cylinder, head, sector_id).
    // sector_id is 1-based (physical sector number on disk).
    std::vector<uint8_t> readSector(uint8_t cyl, uint8_t head,
                                     uint8_t sector_id);
    bool writeSector(uint8_t cyl, uint8_t head, uint8_t sector_id,
                     const std::vector<uint8_t>& data);

    // Activity flag (set during read/write, cleared by caller)
    bool isActive() const { return active_; }
    void clearActive()    { active_ = false; }

private:
    // Returns the byte offset into the image for sector (cyl, head, sid).
    // Returns -1 if the address is invalid.
    int64_t sectorOffset(uint8_t cyl, uint8_t head, uint8_t sector_id) const;

    DiskFormat  fmt_;
    std::string image_path_;
    bool        mounted_       = false;
    bool        write_protect_ = false;
    bool        active_        = false;
    uint8_t     cur_cyl_       = 0;
};
