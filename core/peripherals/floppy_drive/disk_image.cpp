/**
 * @file disk_image.cpp
 * @brief Implementierung von DiskImage::open (Format-Erkennung + Fabrik).
 *
 * Erkennt HFE-Dateien an der Signatur "HXCPICFE" und öffnet sie als HfeImage.
 * Alle anderen Dateien werden als Raw-Sektorimages behandelt, wenn ein
 * DiskFormat mitgeliefert wird.
 *
 * @see core/peripherals/floppy_drive/disk_image.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/hfe_image.h"
#include "core/peripherals/floppy_drive/raw_sector_image.h"

#include <cstring>
#include <fstream>

std::unique_ptr<DiskImage> DiskImage::open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect,
                                           TrackLayout layout) {
    // Erste 8 Bytes lesen für Signaturerkennung.
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;

    char sig[8] = {};
    f.read(sig, sizeof(sig));

    // HFE-v1-Signatur: HfeImage öffnen.
    if (std::memcmp(sig, "HXCPICFE", 8) == 0) {
        auto img = std::make_unique<HfeImage>(path, write_protect);
        if (!img->isOpen()) return nullptr;
        return img;
    }

    // HXCHFEV3 (HFE v3) — noch nicht implementiert.
    if (std::memcmp(sig, "HXCHFEV3", 8) == 0) {
        return nullptr;
    }

    // Raw-Sektorimage: DiskFormat muss übergeben werden.
    if (!fmt.has_value()) return nullptr;

    auto img = std::make_unique<RawSectorImage>(path, *fmt, write_protect,
                                                Encoding::MFM, layout);
    if (!img->isOpen()) return nullptr;

    return img;
}
