/**
 * @file disk_format.cpp
 * @brief Implementierung der DiskFormat-Hilfsfunktionen (reines Datenmodell).
 *
 * @see core/peripherals/floppy_drive/disk_format.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/disk_format.h"

#include <algorithm>

uint8_t DiskFormat::numHeads() const {
    uint8_t h = 0;
    for (const auto& t : tracks) h = std::max(h, static_cast<uint8_t>(t.head_last + 1));
    return h;
}

uint8_t DiskFormat::numCylinders() const {
    uint8_t c = 0;
    for (const auto& t : tracks) c = std::max(c, static_cast<uint8_t>(t.cyl_last + 1));
    return c;
}

uint8_t DiskFormat::physicalCylinders() const {
    const uint8_t n = numCylinders();
    if (n == 0) return 0;
    // Der letzte belegte Zylinder ist (n−1)·step; die Luecken danach gehoeren nicht dazu.
    return static_cast<uint8_t>((n - 1) * step + 1);
}

uint64_t DiskFormat::totalBytes() const {
    uint64_t total = 0;
    for (const auto& t : tracks) {
        const uint32_t ncyl  = t.cyl_last  - t.cyl_first  + 1;
        const uint32_t nhead = t.head_last - t.head_first + 1;
        total += static_cast<uint64_t>(ncyl) * nhead * t.trackBytes();
    }
    return total;
}

const TrackFormat* DiskFormat::findTrack(uint8_t cyl, uint8_t head) const {
    for (const auto& t : tracks)
        if (t.covers(cyl, head)) return &t;
    return nullptr;
}

bool DiskFormat::supportsDrive(const std::string& profile_name) const {
    return std::find(drives.begin(), drives.end(), profile_name) != drives.end();
}

Encoding DiskFormat::predominantEncoding() const {
    // Nach ABGEDECKTEN SPUREN gewichten, nicht nach Zahl der Bereichseinträge —
    // eine einzelne FM-Systemspur darf den MFM-Datenbereich nicht überstimmen.
    uint32_t fm = 0, mfm = 0;
    for (const auto& t : tracks) {
        const uint32_t n = static_cast<uint32_t>(t.cyl_last - t.cyl_first + 1)
                         * static_cast<uint32_t>(t.head_last - t.head_first + 1);
        (t.encoding == Encoding::FM ? fm : mfm) += n;
    }
    return fm > mfm ? Encoding::FM : Encoding::MFM;
}

bool DiskFormat::isMixedEncoding() const {
    if (tracks.empty()) return false;
    const Encoding first = tracks.front().encoding;
    for (const auto& t : tracks)
        if (t.encoding != first) return true;
    return false;
}
