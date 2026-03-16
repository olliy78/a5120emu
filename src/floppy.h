// floppy.h - Floppy disk image handler for A5120
// Supports raw disk images (800KB, 2 sides, 80 tracks, 5 sectors/track of 1024 bytes
// or 26 sectors/track of 128 bytes = CP/M standard)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class FloppyDisk {
public:
    // A5120 disk format parameters
    static constexpr int TRACKS = 80;
    static constexpr int SIDES = 2;
    static constexpr int SECTORS_PER_TRACK = 5;   // Physical sectors (1024 bytes each)
    static constexpr int SECTOR_SIZE = 1024;       // Physical sector size
    static constexpr int CPM_SECTOR_SIZE = 128;    // CP/M logical sector
    static constexpr int CPM_SECTORS_PER_TRACK = 40; // 5*1024/128 = 40 logical sectors/track
    static constexpr int DISK_SIZE = TRACKS * SIDES * SECTORS_PER_TRACK * SECTOR_SIZE; // 819200 bytes
    static constexpr int SYSTEM_TRACKS = 2;        // Reserved system tracks

    // CP/M compatible sector translation table (identity for this format)
    // Standard CPA interleave: 1,7,13,19,25,5,11,17,23,3,9,15,21,2,8,14,20,26,6,12,18,24,4,10,16,22
    static constexpr int XLAT26[] = {
        1,7,13,19,25,5,11,17,23,3,9,15,21,2,8,14,20,26,6,12,18,24,4,10,16,22
    };

    FloppyDisk();

    bool loadImage(const std::string& filename);
    bool saveImage(const std::string& filename) const;
    bool createBlank(const std::string& filename);

    // High-level access for directory-based disk images
    bool loadFromDirectory(const std::string& dirPath);

    // CP/M sector operations (128-byte logical sectors)
    // track: 0-159 (0-79 side 0, 80-159 side 1), sector: 0-based
    bool readSector(int track, int sector, uint8_t* buffer) const;
    bool writeSector(int track, int sector, const uint8_t* buffer);

    // Translate CP/M logical sector to physical position
    void logicalToPhysical(int logTrack, int logSector,
                           int& physTrack, int& physSide, int& physSector, int& offset) const;

    bool isLoaded() const { return loaded_; }
    bool isWriteProtected() const { return writeProtected_; }
    void setWriteProtected(bool wp) { writeProtected_ = wp; }
    bool isModified() const { return modified_; }

    // Access to raw image data
    const std::vector<uint8_t>& data() const { return image_; }

private:
    std::vector<uint8_t> image_;
    bool loaded_;
    bool writeProtected_;
    bool modified_;
    std::string filename_;
    int sectorsPerTrack_;    // Logical sectors per track
    int totalTracks_;        // Total logical tracks (both sides)
};
