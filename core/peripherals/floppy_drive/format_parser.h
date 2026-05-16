#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Describes the geometry of a range of tracks (cylinder+head pairs).
struct TrackFormat {
    uint8_t  cyl_first     = 0;
    uint8_t  cyl_last      = 0;
    uint8_t  head_first    = 0;
    uint8_t  head_last     = 1;
    uint8_t  secs_per_track = 0;
    uint16_t bytes_per_sec  = 0;

    uint32_t trackBytes() const {
        return static_cast<uint32_t>(secs_per_track) * bytes_per_sec;
    }
};

struct DiskFormat {
    std::string              name;
    std::vector<TrackFormat> tracks;

    uint8_t  numHeads()    const;
    uint8_t  numCylinders() const;
    uint64_t totalBytes()  const;

    // Returns the TrackFormat entry that covers (cyl, head), or nullptr.
    const TrackFormat* findTrack(uint8_t cyl, uint8_t head) const;
};

class FormatParser {
public:
    // Parse a cpaFormates.cfg-style file.
    static std::vector<DiskFormat> parseFile(const std::string& path);

    // Built-in formats (always available, no file needed).
    static std::vector<DiskFormat> builtinFormats();
};
