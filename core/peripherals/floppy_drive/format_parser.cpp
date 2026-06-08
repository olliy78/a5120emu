#include "format_parser.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

// ─── DiskFormat helpers ───────────────────────────────────────────────────

uint8_t DiskFormat::numHeads() const {
    uint8_t h = 0;
    for (auto& t : tracks) h = std::max(h, static_cast<uint8_t>(t.head_last + 1));
    return h;
}

uint8_t DiskFormat::numCylinders() const {
    uint8_t c = 0;
    for (auto& t : tracks) c = std::max(c, static_cast<uint8_t>(t.cyl_last + 1));
    return c;
}

uint64_t DiskFormat::totalBytes() const {
    uint64_t total = 0;
    for (auto& t : tracks) {
        uint32_t ncyl  = t.cyl_last  - t.cyl_first  + 1;
        uint32_t nhead = t.head_last - t.head_first + 1;
        total += static_cast<uint64_t>(ncyl) * nhead * t.trackBytes();
    }
    return total;
}

const TrackFormat* DiskFormat::findTrack(uint8_t cyl, uint8_t head) const {
    for (auto& t : tracks)
        if (cyl  >= t.cyl_first  && cyl  <= t.cyl_last &&
            head >= t.head_first && head <= t.head_last)
            return &t;
    return nullptr;
}

// ─── Parser ───────────────────────────────────────────────────────────────

// Trims leading/trailing whitespace.
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Expected format (similar to cpaFormates.cfg):
//
//   [format_name]
//   track cyl_first cyl_last head_first head_last secs bps
//   track ...
//
//   [next_format]
//   ...
//
// Lines starting with '#' or ';' are comments.

std::vector<DiskFormat> FormatParser::parseFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("FormatParser: cannot open " + path);

    std::vector<DiskFormat> result;
    DiskFormat* cur = nullptr;
    std::string line;

    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            result.push_back({});
            cur = &result.back();
            cur->name = line.substr(1, line.size() - 2);
            continue;
        }

        if (!cur) continue;

        if (line.rfind("track", 0) == 0) {
            std::istringstream ss(line.substr(5));
            TrackFormat tf;
            int cf, cl, hf, hl, sp, bps;
            if (ss >> cf >> cl >> hf >> hl >> sp >> bps) {
                tf.cyl_first      = cf;
                tf.cyl_last       = cl;
                tf.head_first     = hf;
                tf.head_last      = hl;
                tf.secs_per_track = sp;
                tf.bytes_per_sec  = bps;
                cur->tracks.push_back(tf);
            }
        }
    }
    return result;
}

// ─── Built-in formats ─────────────────────────────────────────────────────

std::vector<DiskFormat> FormatParser::builtinFormats() {
    std::vector<DiskFormat> fmts;

    // cpa780: mixed geometry with an ASYMMETRIC transition mid-cylinder.
    // Sides are interleaved (cyl0/A, cyl0/B, cyl1/A, cyl1/B, cyl2/A, ...).
    // The system area is THREE physical 128B sides — cyl 0 (both sides) plus
    // cyl 1 side A — and the 1024B data area begins at cyl 1 side B:
    //   cyl0 A (0x0000) stage-1 boot + SYL   | 128B
    //   cyl0 B (0x0D00) filler (0x53)         | 128B
    //   cyl1 A (0x1A00) stage-2 loader + SYL  | 128B
    //   cyl1 B (0x2700) first data track      | 1024B  (filler/reserved)
    //   cyl2 A (0x3B00) CP/M directory        | 1024B  ("@OS     COM" ...)
    //   cyl2 B (0x5000) @OS.COM data ...       | 1024B
    // 3 × 3328 + 5120 = 0x3B00, so the directory lands exactly on the cyl 2
    // side-A boundary; the 3rd-stage loader reads its data area at physical
    // cyl 2 (IDAM cyl=2, size_code=3/1024B).  Modelling cyl 1 side B as 128B
    // (a clean 2-cylinder boot area) would shift the whole data area 0x700
    // earlier and misalign the @OS.COM allocation blocks.
    {
        DiskFormat f;
        f.name = "cpa780";
        f.tracks.push_back({0, 0, 0, 1, 26,  128});  // cyl 0, both sides: system
        f.tracks.push_back({1, 1, 0, 0, 26,  128});  // cyl 1 side A: stage-2
        f.tracks.push_back({1, 1, 1, 1,  5, 1024});  // cyl 1 side B: first data track
        f.tracks.push_back({2, 79, 0, 1, 5, 1024});  // cyl 2-79: data + file system
        fmts.push_back(std::move(f));
    }

    // cpa800: all tracks 5×1024B (no special boot tracks)
    {
        DiskFormat f;
        f.name = "cpa800";
        f.tracks.push_back({0, 79, 0, 1, 5, 1024});
        fmts.push_back(std::move(f));
    }

    // cpa640: 80 tracks, 1 head, 16×256B
    {
        DiskFormat f;
        f.name = "cpa640";
        f.tracks.push_back({0, 79, 0, 0, 16, 256});
        fmts.push_back(std::move(f));
    }

    // cpa624: 78 tracks, 1 head, 16×256B (reduced capacity)
    {
        DiskFormat f;
        f.name = "cpa624";
        f.tracks.push_back({0, 77, 0, 0, 16, 256});
        fmts.push_back(std::move(f));
    }

    // cpa200: single-sided 80 tracks, 5×1024B
    {
        DiskFormat f;
        f.name = "cpa200";
        f.tracks.push_back({0, 79, 0, 0, 5, 1024});
        fmts.push_back(std::move(f));
    }

    // cpa200_boot: single-sided, boot tracks 26×128B, data 5×1024B
    {
        DiskFormat f;
        f.name = "cpa200_boot";
        f.tracks.push_back({0, 2, 0, 0, 26, 128});
        f.tracks.push_back({3, 79, 0, 0, 5, 1024});
        fmts.push_back(std::move(f));
    }

    // scpx780: same geometry as cpa780 (SCPX variant)
    {
        DiskFormat f;
        f.name = "scpx780";
        f.tracks.push_back({0, 2, 0, 1, 26, 128});
        f.tracks.push_back({3, 79, 0, 1, 5, 1024});
        fmts.push_back(std::move(f));
    }

    // scpx780_b: SCPX variant B
    {
        DiskFormat f;
        f.name = "scpx780_b";
        f.tracks.push_back({0, 2, 0, 1, 26, 128});
        f.tracks.push_back({3, 79, 0, 1, 5, 1024});
        fmts.push_back(std::move(f));
    }

    return fmts;
}
