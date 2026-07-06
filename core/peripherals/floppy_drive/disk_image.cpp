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
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
// Prüft, ob ein geöffnetes Image FORMATIERTE Daten trägt: dekodiert die ersten Spuren
// und sucht mindestens eine Adressmarke (IDAM/DAM).  Fängt gap-leere/unformatierte
// .hfe-Dateien ab (sonst hängt der Controller beim Lesen einer markenlosen Spur).
// Raw/.img synthetisiert Marken immer → besteht stets.
bool hasFormattedData(DiskImage& img) {
    const DiskGeometry g = img.geometry();
    if (g.num_cyls == 0 || g.num_heads == 0) return false;
    const uint8_t maxc = std::min<uint8_t>(g.num_cyls, 3);
    for (uint8_t c = 0; c < maxc; ++c) {
        for (uint8_t h = 0; h < g.num_heads; ++h) {
            const TrackImage t = img.readTrack(c, h);
            for (MarkType m : t.marks)
                if (m == MarkType::Id || m == MarkType::Data) return true;
        }
    }
    return false;
}
}  // namespace

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
        // Leere/gap-lose (unformatierte) Datei ablehnen statt hängen zu lassen.
        if (!hasFormattedData(*img)) return nullptr;
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
    if (!hasFormattedData(*img)) return nullptr;

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

// Leeres HFE-v1-Template (Header + LUT + Gap-Spurdaten) im Verfahren @p enc.
// Geometrie: num_cyls × num_heads; side_len HFE-Bytes je Spurseite (muss die größte
// formatierte Spur fassen).  Wird von create() als Rohling für das anschließende
// spurweise Formatieren benutzt.  Vgl. tools/img_to_hfe.py --blank.
bool writeBlankHfe(const std::string& path, uint8_t num_cyls, uint8_t num_heads,
                   uint32_t side_len, Encoding enc) {
    const uint32_t track_len   = side_len * num_heads;    // beide Seiten
    const uint32_t track_blocks = (track_len + 511) / 512;
    const uint32_t track_len_pad = track_blocks * 512;

    std::vector<uint8_t> hdr(512, 0x00);
    std::memcpy(hdr.data(), "HXCPICFE", 8);
    hdr[0x08] = 0;           // formatrevision v1
    hdr[0x09] = num_cyls;
    hdr[0x0A] = num_heads;
    hdr[0x0B] = (enc == Encoding::FM) ? 2 : 0;  // 2 = ISOIBM_FM, 0 = ISOIBM_MFM
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

// Logische Sektoren einer Spur (cyl, head) mit Nutzdaten 0xE5 (leere CP/M-Sektoren).
std::vector<LogicalSector> emptySectors(const TrackFormat& tf, uint8_t cyl, uint8_t head) {
    std::vector<LogicalSector> secs;
    secs.reserve(tf.secs_per_track);
    for (uint8_t id = 1; id <= tf.secs_per_track; ++id) {
        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = head;
        ls.id   = id;
        ls.size = tf.bytes_per_sec;
        ls.data.assign(tf.bytes_per_sec, 0xE5);
        secs.push_back(std::move(ls));
    }
    return secs;
}

// Formatiert je Spur eine gültige IBM-Spur (IDAM/DATA/CRC, Daten 0xE5) in ein leeres
// HFE-Template und schreibt sie über den erprobten HFE-Schreibpfad zurück.
std::unique_ptr<DiskImage> createFormattedHfe(const std::string& path,
                                              const DiskFormat& fmt,
                                              bool write_protect, Encoding enc) {
    const uint8_t num_cyls  = fmt.numCylinders();
    const uint8_t num_heads = fmt.numHeads();
    if (num_cyls == 0 || num_heads == 0) return nullptr;

    // 1. Alle Spuren bauen; größte Spurlänge merken.
    std::vector<TrackImage> tracks(static_cast<size_t>(num_cyls) * num_heads);
    size_t max_track_bytes = 0;
    for (uint8_t c = 0; c < num_cyls; ++c) {
        for (uint8_t h = 0; h < num_heads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;   // Spur existiert nicht → bleibt Gap
            TrackImage t = TrackCodec::buildTrack(emptySectors(*tf, c, h), enc);
            max_track_bytes = std::max(max_track_bytes, t.bytes.size());
            tracks[static_cast<size_t>(c) * num_heads + h] = std::move(t);
        }
    }
    if (max_track_bytes == 0) return nullptr;

    // 2. HFE-Bytes je Seite: 1 Datenbyte = 16 Bitzellen = 2 HFE-Bytes; Marge + auf 256 auf.
    uint32_t side_len = static_cast<uint32_t>(max_track_bytes) * 2 + 256;
    side_len = (side_len + 255) / 256 * 256;

    // 3. Leeres Template (Header trägt das Verfahren) …
    if (!writeBlankHfe(path, num_cyls, num_heads, side_len, enc)) return nullptr;

    // 4. … und je Spur den formatierten Track hineinschreiben (schreibbar öffnen).
    {
        HfeImage fmt_img(path, /*write_protect=*/false);
        if (!fmt_img.isOpen()) return nullptr;
        for (uint8_t c = 0; c < num_cyls; ++c) {
            for (uint8_t h = 0; h < num_heads; ++h) {
                TrackImage& t = tracks[static_cast<size_t>(c) * num_heads + h];
                if (t.bytes.empty()) continue;
                if (!fmt_img.writeTrack(c, h, t)) return nullptr;
            }
        }
        if (!fmt_img.flush()) return nullptr;
    }

    // Fertig formatierte Datei mit dem gewünschten Schreibschutz zurückgeben.
    auto img = std::make_unique<HfeImage>(path, write_protect);
    if (!img->isOpen()) return nullptr;
    return img;
}

}  // namespace

std::unique_ptr<DiskImage> DiskImage::create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect, Encoding enc) {
    // Beide Zieltypen brauchen jetzt die Geometrie (formatierte Leerdiskette).
    if (!fmt.has_value()) return nullptr;

    // HFE (.hfe): gültig formatiertes Bitzellen-Image im Verfahren enc.
    if (endsWithCI(path, ".hfe"))
        return createFormattedHfe(path, *fmt, write_protect, enc);

    // Raw .img: rohes Sektorimage in Format-Größe, 0xE5-gefüllt.
    if (!writeBlankImg(path, *fmt)) return nullptr;
    auto img = std::make_unique<RawSectorImage>(path, *fmt, write_protect, enc);
    if (!img->isOpen()) return nullptr;
    return img;
}
