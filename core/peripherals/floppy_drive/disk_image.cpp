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

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>

std::unique_ptr<DiskImage> DiskImage::open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect) {
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
                                                Encoding::MFM);
    if (!img->isOpen()) return nullptr;

    return img;
}

// ─── create (neue leere Image-Datei anlegen) ────────────────────────────────

namespace {

// Endet @p path (case-insensitiv) auf @p suffix?
bool endsWithCI(const std::string& path, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (path.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(path[path.size() - n + i]))
            != std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

// Little-endian u16 in einen Puffer schreiben.
void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// Leeres, formatierbares HFE-v1-MFM-Template (Header + LUT + Gap-Spurdaten).
// Geometrie: num_cyls × num_heads; side_len HFE-Bytes je Spurseite (großzügig für
// den größten §3-Track 5×1024 B).  Vgl. tools/img_to_hfe.py --blank.
bool writeBlankHfe(const std::string& path, uint8_t num_cyls, uint8_t num_heads,
                   uint32_t side_len) {
    const uint32_t track_len   = side_len * num_heads;    // beide Seiten
    const uint32_t track_blocks = (track_len + 511) / 512;
    const uint32_t track_len_pad = track_blocks * 512;

    std::vector<uint8_t> hdr(512, 0x00);
    std::memcpy(hdr.data(), "HXCPICFE", 8);
    hdr[0x08] = 0;           // formatrevision v1
    hdr[0x09] = num_cyls;
    hdr[0x0A] = num_heads;
    hdr[0x0B] = 0;           // ISOIBM_MFM
    put16(hdr, 0x0C, 250);   // bitrate kbit/s
    put16(hdr, 0x0E, 300);   // rpm
    hdr[0x10] = 0;           // iface
    hdr[0x11] = 1;           // dnu
    put16(hdr, 0x12, 1);     // track_list_block = 1
    hdr[0x14] = 0xFF;        // write_allowed
    hdr[0x15] = 0xFF;        // single_step
    for (size_t i = 0x16; i < 512; ++i) hdr[i] = 0xFF;

    std::vector<uint8_t> lut(512, 0xFF);
    uint32_t blk = 2;        // Spurdaten ab Block 2
    for (uint8_t c = 0; c < num_cyls; ++c) {
        put16(lut, c * 4 + 0, static_cast<uint16_t>(blk));
        put16(lut, c * 4 + 2, static_cast<uint16_t>(track_len));
        blk += track_blocks;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(hdr.data()), 512);
    f.write(reinterpret_cast<const char*>(lut.data()), 512);
    const std::vector<uint8_t> gap(track_len_pad, 0x88);   // HFE-Gap
    for (uint8_t c = 0; c < num_cyls; ++c)
        f.write(reinterpret_cast<const char*>(gap.data()), track_len_pad);
    return static_cast<bool>(f);
}

// Rohes .img in DiskFormat-Größe, mit 0xE5 (leere CP/M-Sektoren) gefüllt.
bool writeBlankImg(const std::string& path, const DiskFormat& fmt) {
    const uint64_t total = fmt.totalBytes();
    if (total == 0) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::vector<uint8_t> buf(64 * 1024, 0xE5);
    uint64_t written = 0;
    while (written < total) {
        const uint64_t n = std::min<uint64_t>(buf.size(), total - written);
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(n));
        written += n;
    }
    return static_cast<bool>(f);
}

}  // namespace

std::unique_ptr<DiskImage> DiskImage::create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect) {
    // HFE (.hfe): leeres, formatagnostisches Template der K5601-Geometrie (80×2).
    // fmt wird nicht benötigt (HFE ist selbstbeschreibend).
    if (endsWithCI(path, ".hfe")) {
        if (!writeBlankHfe(path, /*num_cyls=*/80, /*num_heads=*/2,
                           /*side_len=*/12500))
            return nullptr;
        auto img = std::make_unique<HfeImage>(path, write_protect);
        if (!img->isOpen()) return nullptr;
        return img;
    }

    // Raw .img: DiskFormat ERFORDERLICH (Größe + Geometrie).
    if (!fmt.has_value()) return nullptr;
    if (!writeBlankImg(path, *fmt)) return nullptr;
    auto img = std::make_unique<RawSectorImage>(path, *fmt, write_protect,
                                                Encoding::MFM);
    if (!img->isOpen()) return nullptr;
    return img;
}
