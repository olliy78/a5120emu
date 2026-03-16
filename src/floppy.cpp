// floppy.cpp - Floppy disk image implementation
#include "floppy.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>

constexpr int FloppyDisk::XLAT26[];

FloppyDisk::FloppyDisk()
    : loaded_(false), writeProtected_(false), modified_(false),
      sectorsPerTrack_(CPM_SECTORS_PER_TRACK), totalTracks_(TRACKS * SIDES) {
}

bool FloppyDisk::loadImage(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    image_.resize(fileSize);
    file.read(reinterpret_cast<char*>(image_.data()), fileSize);

    if (!file.good() && !file.eof()) return false;

    // Determine format based on file size
    if (fileSize == 819200) {
        // Standard 800KB: 80 tracks * 2 sides * 5 sectors * 1024 bytes
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK; // 40 logical
        totalTracks_ = TRACKS * SIDES;
    } else if (fileSize == 409600) {
        // Single sided 400KB
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
        totalTracks_ = TRACKS;
    } else if (fileSize <= 256256) {
        // SSSD: 77 tracks * 26 * 128 or similar
        sectorsPerTrack_ = 26;
        totalTracks_ = fileSize / (26 * 128);
    } else {
        // Generic: assume 128-byte sectors
        sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
        totalTracks_ = fileSize / (CPM_SECTORS_PER_TRACK * CPM_SECTOR_SIZE);
        if (totalTracks_ == 0) totalTracks_ = 1;
    }

    filename_ = filename;
    loaded_ = true;
    modified_ = false;
    return true;
}

bool FloppyDisk::saveImage(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(image_.data()), image_.size());
    return file.good();
}

bool FloppyDisk::createBlank(const std::string& filename) {
    image_.resize(DISK_SIZE, 0xE5); // E5 = CP/M empty marker
    filename_ = filename;
    loaded_ = true;
    modified_ = true;
    return saveImage(filename);
}

bool FloppyDisk::loadFromDirectory(const std::string& dirPath) {
    // Create a blank disk image and populate it with files from the directory
    image_.resize(DISK_SIZE, 0xE5);
    loaded_ = true;
    modified_ = true;
    sectorsPerTrack_ = CPM_SECTORS_PER_TRACK;
    totalTracks_ = TRACKS * SIDES;

    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return false;

    // DPB parameters for this format:
    // BSH=4, BLM=0F -> block size = 2048 bytes
    // DSM=194 -> 194 < 256, so use 8-bit block numbers (16 per dir entry)
    // EXM=1 -> each directory entry covers up to 32KB (16 blocks * 2KB)
    // DRM=127 -> max 128 directory entries
    // OFF=2 -> 2 system tracks
    // AL0=C0 -> first 2 blocks reserved for directory
    constexpr int blockSize = 2048;
    constexpr int maxDirEntries = 128;
    constexpr int dirBlocks = 2; // AL0=C0 means blocks 0,1 for directory
    constexpr int blocksPerEntry = 16; // 8-bit block numbers -> 16 per entry
    constexpr int recsPerEntry = blocksPerEntry * blockSize / CPM_SECTOR_SIZE; // 256

    int dataStartOffset = SYSTEM_TRACKS * sectorsPerTrack_ * CPM_SECTOR_SIZE;

    // Read files from directory
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> content;
    };
    std::vector<FileEntry> files;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;
        FileEntry fe;
        fe.name = entry.path().filename().string();
        fe.content.assign(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        files.push_back(std::move(fe));
    }

    // Allocate blocks to files and build directory entries
    uint8_t directory[maxDirEntries * 32];
    memset(directory, 0xE5, sizeof(directory));
    int dirEntry = 0;
    int currentBlock = dirBlocks; // First data block after directory

    for (const auto& fe : files) {
        if (dirEntry >= maxDirEntries) break;

        // Parse filename to 8.3 uppercase format
        std::string fname = fe.name;
        std::string base, ext;
        auto dot = fname.rfind('.');
        if (dot != std::string::npos) {
            base = fname.substr(0, dot);
            ext = fname.substr(dot + 1);
        } else {
            base = fname;
        }
        for (auto& c : base) c = toupper(c);
        for (auto& c : ext) c = toupper(c);
        while (base.size() < 8) base += ' ';
        while (ext.size() < 3) ext += ' ';
        base = base.substr(0, 8);
        ext = ext.substr(0, 3);

        int totalRecords = (fe.content.size() + CPM_SECTOR_SIZE - 1) / CPM_SECTOR_SIZE;
        if (totalRecords == 0) totalRecords = 1; // Minimum 1 record for empty files
        int blocksNeeded = (fe.content.size() + blockSize - 1) / blockSize;
        if (blocksNeeded == 0) blocksNeeded = 1;

        // Write file data to image
        for (int b = 0; b < blocksNeeded; b++) {
            int block = currentBlock + b;
            int imgOffset = dataStartOffset + block * blockSize;
            int srcOffset = b * blockSize;
            int bytesToWrite = std::min((int)fe.content.size() - srcOffset, blockSize);
            if (bytesToWrite > 0 && imgOffset + bytesToWrite <= (int)image_.size()) {
                memcpy(&image_[imgOffset], &fe.content[srcOffset], bytesToWrite);
            }
        }

        // Create directory entries for this file
        // Each entry covers up to 16 blocks (8-bit pointers)
        int blocksAllocated = 0;
        int recordsAllocated = 0;

        while (blocksAllocated < blocksNeeded && dirEntry < maxDirEntries) {
            uint8_t* dir = &directory[dirEntry * 32];
            int blocksThisEntry = std::min(blocksPerEntry, blocksNeeded - blocksAllocated);
            int recsThisEntry = std::min(totalRecords - recordsAllocated, recsPerEntry);

            dir[0] = 0;  // User 0
            memcpy(&dir[1], base.c_str(), 8);
            memcpy(&dir[9], ext.c_str(), 3);

            // Extent number: with EXM=1, each dir entry covers 2 logical extents
            int extentNum = recordsAllocated / 128; // Logical extent number
            dir[12] = extentNum & 0x1F;  // EX (low 5 bits of extent)
            dir[13] = 0;                 // S1
            dir[14] = (extentNum >> 5) & 0x3F;  // S2 (high bits of extent)

            // RC: records in the last extent of this entry
            if (blocksAllocated + blocksThisEntry < blocksNeeded) {
                // Not the last entry - full
                dir[15] = 128;
            } else {
                // Last entry - actual remaining records
                int rem = totalRecords % 128;
                dir[15] = (rem == 0) ? 128 : rem;
            }

            // Allocation map: 8-bit block numbers (16 entries, DSM < 256)
            for (int i = 0; i < blocksThisEntry; i++) {
                dir[16 + i] = (currentBlock + blocksAllocated + i) & 0xFF;
            }
            // Remaining entries stay 0 (no block allocated)
            for (int i = blocksThisEntry; i < blocksPerEntry; i++) {
                dir[16 + i] = 0;
            }

            blocksAllocated += blocksThisEntry;
            recordsAllocated += recsThisEntry;
            dirEntry++;
        }

        currentBlock += blocksNeeded;
    }

    // Write directory to disk (blocks 0 and 1, right after system tracks)
    int dirSize = std::min((int)sizeof(directory), dirBlocks * blockSize);
    memcpy(&image_[dataStartOffset], directory, dirSize);

    return true;
}

void FloppyDisk::logicalToPhysical(int logTrack, int logSector,
                                    int& physTrack, int& physSide,
                                    int& physSector, int& offset) const {
    // For A5120 800KB format:
    // Logical tracks 0-79 = side 0, tracks 80-159 = side 1
    // Each logical track has 40 sectors of 128 bytes
    // Physical: 5 sectors of 1024 bytes per track/side

    if (totalTracks_ > TRACKS) {
        // Double-sided
        physSide = logTrack / TRACKS;
        physTrack = logTrack % TRACKS;
    } else {
        physSide = 0;
        physTrack = logTrack;
    }

    // Map logical sector (0-39) to physical sector + offset
    int byteOffset = logSector * CPM_SECTOR_SIZE;
    physSector = byteOffset / SECTOR_SIZE;
    offset = byteOffset % SECTOR_SIZE;
}

bool FloppyDisk::readSector(int track, int sector, uint8_t* buffer) const {
    if (!loaded_) return false;
    if (track < 0 || track >= totalTracks_) return false;
    if (sector < 0 || sector >= sectorsPerTrack_) return false;

    size_t imageOffset = (size_t)track * sectorsPerTrack_ * CPM_SECTOR_SIZE
                       + (size_t)sector * CPM_SECTOR_SIZE;

    if (imageOffset + CPM_SECTOR_SIZE > image_.size()) {
        memset(buffer, 0xE5, CPM_SECTOR_SIZE);
        return true;
    }

    memcpy(buffer, &image_[imageOffset], CPM_SECTOR_SIZE);
    return true;
}

bool FloppyDisk::writeSector(int track, int sector, const uint8_t* buffer) {
    if (!loaded_ || writeProtected_) return false;
    if (track < 0 || track >= totalTracks_) return false;
    if (sector < 0 || sector >= sectorsPerTrack_) return false;

    size_t imageOffset = (size_t)track * sectorsPerTrack_ * CPM_SECTOR_SIZE
                       + (size_t)sector * CPM_SECTOR_SIZE;

    if (imageOffset + CPM_SECTOR_SIZE > image_.size()) {
        image_.resize(imageOffset + CPM_SECTOR_SIZE, 0xE5);
    }

    memcpy(&image_[imageOffset], buffer, CPM_SECTOR_SIZE);
    modified_ = true;
    return true;
}
