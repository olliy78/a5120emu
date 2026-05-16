#include "floppy_drive.h"
#include <fstream>
#include <stdexcept>

bool FloppyDrive::mount(const std::string& path, const DiskFormat& fmt,
                        bool write_protect) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    fmt_          = fmt;
    image_path_   = path;
    mounted_      = true;
    write_protect_ = write_protect;
    cur_cyl_      = 0;
    return true;
}

void FloppyDrive::unmount() {
    mounted_     = false;
    image_path_  = {};
    fmt_         = {};
    cur_cyl_     = 0;
    active_      = false;
}

bool FloppyDrive::seekTrack(uint8_t cyl) {
    if (!mounted_) return false;
    cur_cyl_ = cyl;
    return true;
}

bool FloppyDrive::step(bool inward) {
    if (!mounted_) return false;
    if (inward && cur_cyl_ < 79) ++cur_cyl_;
    else if (!inward && cur_cyl_ > 0) --cur_cyl_;
    return true;
}

// Returns the byte offset of sector (cyl, head, sector_id) in the image.
// The image layout interleaves tracks: (cyl0,head0), (cyl0,head1), (cyl1,head0), ...
// sector_id is 1-based.
int64_t FloppyDrive::sectorOffset(uint8_t cyl, uint8_t head,
                                   uint8_t sector_id) const {
    const TrackFormat* tf = fmt_.findTrack(cyl, head);
    if (!tf) return -1;
    if (sector_id < 1 || sector_id > tf->secs_per_track) return -1;

    // Sum all track bytes before (cyl, head) in layout order.
    uint64_t offset = 0;
    uint8_t ncyls = fmt_.numCylinders();
    uint8_t nheads = fmt_.numHeads();

    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            if (c == cyl && h == head) {
                // add sector offset within this track
                offset += static_cast<uint64_t>(sector_id - 1) * tf->bytes_per_sec;
                return static_cast<int64_t>(offset);
            }
            const TrackFormat* t = fmt_.findTrack(c, h);
            if (t) offset += t->trackBytes();
        }
    }
    return -1;
}

std::vector<uint8_t> FloppyDrive::readSector(uint8_t cyl, uint8_t head,
                                              uint8_t sector_id) {
    if (!mounted_) return {};
    const TrackFormat* tf = fmt_.findTrack(cyl, head);
    if (!tf || sector_id < 1 || sector_id > tf->secs_per_track) return {};

    int64_t off = sectorOffset(cyl, head, sector_id);
    if (off < 0) return {};

    std::ifstream f(image_path_, std::ios::binary);
    if (!f) return {};
    f.seekg(off);

    std::vector<uint8_t> data(tf->bytes_per_sec, 0xFF);
    f.read(reinterpret_cast<char*>(data.data()), tf->bytes_per_sec);
    active_ = true;
    return data;
}

bool FloppyDrive::writeSector(uint8_t cyl, uint8_t head, uint8_t sector_id,
                               const std::vector<uint8_t>& data) {
    if (!mounted_ || write_protect_) return false;
    const TrackFormat* tf = fmt_.findTrack(cyl, head);
    if (!tf || sector_id < 1 || sector_id > tf->secs_per_track) return false;
    if (data.size() != tf->bytes_per_sec) return false;

    int64_t off = sectorOffset(cyl, head, sector_id);
    if (off < 0) return false;

    std::fstream f(image_path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return false;
    f.seekp(off);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    active_ = true;
    return f.good();
}
