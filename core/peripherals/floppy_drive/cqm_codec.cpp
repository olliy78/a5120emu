/**
 * @file cqm_codec.cpp
 * @brief Implementierung von CqmCodec (CopyQM-Abbild, nur Lesen).
 *
 * @see core/peripherals/floppy_drive/cqm_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/cqm_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <array>
#include <fstream>
#include <utility>

namespace {

// Kopfoffsets (cqm_codec.h)
constexpr size_t kOffSecSize  = 0x03;
constexpr size_t kOffSpt      = 0x10;
constexpr size_t kOffHeads    = 0x12;
constexpr size_t kOffDescr    = 0x1C;
constexpr size_t kDescrLen    = 60;
constexpr size_t kOffBlind    = 0x58;
constexpr size_t kOffDensity  = 0x59;
constexpr size_t kOffUsedCyl  = 0x5A;
constexpr size_t kOffTotCyl   = 0x5B;
constexpr size_t kOffCrc      = 0x5C;
constexpr size_t kOffLabel    = 0x60;
constexpr size_t kLabelLen    = 11;
constexpr size_t kOffTime     = 0x6B;
constexpr size_t kOffDate     = 0x6D;
constexpr size_t kOffCmtLen   = 0x6F;
constexpr size_t kOffSecBase  = 0x71;
constexpr size_t kOffInterlv  = 0x74;
constexpr size_t kOffSkew     = 0x75;

/// Obergrenze der entpackten Nutzdaten — schuetzt vor einem kaputten Kopf, der
/// Gigabytes anfordern wuerde (die groesste echte Diskette hat 2,88 MB).
constexpr size_t kMaxRaw = 16u * 1024u * 1024u;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// @brief NUL-terminierte Zeichenkette fester Feldlaenge.
std::string festerText(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

/// @brief Tabelle des reflektierten CRC-32 (0xEDB88320).
const std::array<uint32_t, 256>& crcTabelle() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
            a[i] = c;
        }
        return a;
    }();
    return t;
}

bool leseDatei(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "Kann Datei nicht oeffnen: " + path; return false; }
    const auto n = static_cast<size_t>(f.tellg());
    out.resize(n);
    f.seekg(0);
    if (n && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n))) {
        err = "Lesefehler: " + path;
        return false;
    }
    return true;
}

}  // namespace

// ─── Pruefwert / Erkennung ───────────────────────────────────────────────────

uint32_t CqmCodec::crc(const uint8_t* data, size_t n) {
    const auto& t = crcTabelle();
    uint32_t c = 0;
    // CopyQM-Tabellenfehler: Index nur 6 Bit breit (libdsk drvqm.c, drv_qm_update_crc).
    for (size_t i = 0; i < n; ++i) c = t[(data[i] ^ c) & 0x3F] ^ (c >> 8);
    return c;
}

bool CqmCodec::isCqm(const uint8_t* d, size_t n) {
    if (n < kHeaderSize || d[0] != 'C' || d[1] != 'Q') return false;
    uint8_t summe = 0;
    for (size_t i = 0; i < kHeaderSize; ++i) summe = static_cast<uint8_t>(summe + d[i]);
    return summe == 0;
}

bool CqmCodec::isCqm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t kopf[kHeaderSize];
    if (!f.read(reinterpret_cast<char*>(kopf), kHeaderSize)) return false;
    return isCqm(kopf, kHeaderSize);
}

// ─── Laden ───────────────────────────────────────────────────────────────────

bool CqmCodec::decodeBuffer(const std::vector<uint8_t>& d, std::vector<uint8_t>& raw,
                            SourceInfo* info, std::string& err) {
    SourceInfo si;
    if (d.size() < kHeaderSize || d[0] != 'C' || d[1] != 'Q') {
        err = "Keine CopyQM-Datei (Kennung 'CQ' fehlt)";
        return false;
    }
    if (!isCqm(d.data(), d.size())) {
        err = "CopyQM-Kopf beschaedigt (Kopfpruefbyte stimmt nicht)";
        return false;
    }

    si.sector_size     = u16(&d[kOffSecSize]);
    si.secs_per_track  = u16(&d[kOffSpt]);
    si.heads           = u16(&d[kOffHeads]);
    si.blind           = d[kOffBlind];
    si.density         = d[kOffDensity];
    si.used_cyls       = d[kOffUsedCyl];
    si.total_cyls      = d[kOffTotCyl];
    si.crc_stored      = u32(&d[kOffCrc]);
    si.dos_time        = u16(&d[kOffTime]);
    si.dos_date        = u16(&d[kOffDate]);
    si.first_sector_id = static_cast<uint8_t>(d[kOffSecBase] + 1);
    si.interleave      = d[kOffInterlv];
    si.skew            = d[kOffSkew];
    si.description     = festerText(&d[kOffDescr], kDescrLen);
    si.label           = festerText(&d[kOffLabel], kLabelLen);
    // Das Etikettfeld ist 11 Byte ohne Pflicht-NUL ("** NONE ** " mit Leerzeichen kommt vor).
    while (!si.label.empty() && si.label.back() == ' ') si.label.pop_back();
    if (si.label == "** NONE **") si.label.clear();
    const size_t cmt   = u16(&d[kOffCmtLen]);

    const uint16_t sz = si.sector_size;
    if (!(sz == 128 || sz == 256 || sz == 512 || sz == 1024 || sz == 2048 || sz == 4096)) {
        err = "CopyQM: unplausible Sektorgroesse " + std::to_string(sz);
        return false;
    }
    if (si.secs_per_track < 1 || si.secs_per_track > 64
        || si.first_sector_id + si.secs_per_track - 1 > 255) {
        err = "CopyQM: unplausible Sektorzahl je Spur " + std::to_string(si.secs_per_track);
        return false;
    }
    if (si.heads < 1 || si.heads > 2) {
        err = "CopyQM: unplausible Seitenzahl " + std::to_string(si.heads);
        return false;
    }
    if (si.used_cyls < 1) {
        err = "CopyQM: keine gespeicherten Zylinder";
        return false;
    }
    if (si.total_cyls < si.used_cyls) si.total_cyls = si.used_cyls;

    if (kHeaderSize + cmt > d.size()) {
        err = "CopyQM: Datei abgeschnitten (Kommentar reicht ueber das Dateiende)";
        return false;
    }
    si.comment.assign(reinterpret_cast<const char*>(&d[kHeaderSize]), cmt);
    // Ein Kommentar ist meist NUL- oder ^Z-terminiert; nur den Text davor melden.
    while (!si.comment.empty() && (si.comment.back() == '\0' || si.comment.back() == 0x1A))
        si.comment.pop_back();

    const size_t erwartet = static_cast<size_t>(si.used_cyls) * si.heads
                          * si.secs_per_track * sz;
    if (erwartet > kMaxRaw) {
        err = "CopyQM: Geometrie unplausibel gross (" + std::to_string(erwartet) + " Byte)";
        return false;
    }

    // RLE-Strom: i16 n; n<0 → naechstes Byte -n-mal; n>0 → n Rohbytes; 0 → nichts.
    raw.clear();
    raw.reserve(erwartet);
    size_t p = kHeaderSize + cmt;
    while (raw.size() < erwartet) {
        if (p + 2 > d.size()) {
            err = "CopyQM: Datei abgeschnitten (Datenstrom endet nach "
                  + std::to_string(raw.size()) + " von " + std::to_string(erwartet)
                  + " Byte)";
            return false;
        }
        const int n = static_cast<int16_t>(u16(&d[p]));
        p += 2;
        if (n < 0) {
            if (p >= d.size()) {
                err = "CopyQM: Datei abgeschnitten (Wiederholungsblock ohne Byte)";
                return false;
            }
            const size_t cnt = static_cast<size_t>(-n);
            if (raw.size() + cnt > erwartet) {
                err = "CopyQM: Datenstrom laenger als die Geometrie des Kopfs";
                return false;
            }
            raw.insert(raw.end(), cnt, d[p]);
            ++p;
        } else if (n > 0) {
            const size_t cnt = static_cast<size_t>(n);
            if (p + cnt > d.size()) {
                err = "CopyQM: Datei abgeschnitten (Rohblock reicht ueber das Dateiende)";
                return false;
            }
            if (raw.size() + cnt > erwartet) {
                err = "CopyQM: Datenstrom laenger als die Geometrie des Kopfs";
                return false;
            }
            raw.insert(raw.end(), d.begin() + static_cast<std::ptrdiff_t>(p),
                       d.begin() + static_cast<std::ptrdiff_t>(p + cnt));
            p += cnt;
        }
    }
    si.trailing_bytes = d.size() - p;

    // Pruefwert 0 = alte CopyQM-Version ohne Pruefwert → nicht pruefbar.
    si.crc_computed = crc(raw.data(), raw.size());
    if (si.crc_stored != 0) {
        si.crc_checked = true;
        if (si.crc_stored != si.crc_computed) {
            err = "CopyQM: Pruefwert stimmt nicht (Datei beschaedigt)";
            if (info) *info = si;
            return false;
        }
    }

    if (info) *info = si;
    return true;
}

bool CqmCodec::loadBuffer(const std::vector<uint8_t>& datei, DiskMedium& out,
                          SourceInfo* info, std::string& err) {
    SourceInfo si;
    std::vector<uint8_t> raw;
    if (!decodeBuffer(datei, raw, &si, err)) {
        if (info) *info = si;
        return false;
    }

    const uint8_t ncyls  = si.total_cyls;
    const uint8_t nheads = static_cast<uint8_t>(si.heads);
    out = DiskMedium(ncyls, nheads, Encoding::MFM);
    // 5,25″-HD (15 x 512) dreht mit 360 U/min, alles andere hier mit 300 (siehe
    // doc/design/33_cqm.md); nur fuer die Zellenzahl einer spaeteren .hfe von Belang.
    out.setNominalRpm((si.density == 1 && si.secs_per_track == 15 && si.sector_size == 512)
                          ? 360 : 300);

    size_t off = 0;
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            std::vector<LogicalSector> sektoren;
            sektoren.reserve(si.secs_per_track);
            const bool gespeichert = c < si.used_cyls;
            for (uint16_t i = 0; i < si.secs_per_track; ++i) {
                LogicalSector ls;
                ls.cyl  = c;
                ls.head = h;
                ls.id   = static_cast<uint8_t>(si.first_sector_id + i);
                ls.size = si.sector_size;
                if (gespeichert) {
                    ls.data.assign(raw.begin() + static_cast<std::ptrdiff_t>(off),
                                   raw.begin() + static_cast<std::ptrdiff_t>(off + si.sector_size));
                    off += si.sector_size;
                } else {
                    // Nicht gespeicherte Zylinder: leer formatiert (0xF6, wie CopyQM
                    // und libdsk sie auffuellen).
                    ls.data.assign(si.sector_size, 0xF6);
                }
                sektoren.push_back(std::move(ls));
            }
            out.setTrack(c, h, TrackCodec::buildTrack(sektoren, Encoding::MFM));
        }
    }
    out.clearDirty();
    if (info) *info = si;
    return true;
}

bool CqmCodec::load(const std::string& path, DiskMedium& out, SourceInfo* info,
                    std::string& err) {
    std::vector<uint8_t> datei;
    if (!leseDatei(path, datei, err)) return false;
    if (!loadBuffer(datei, out, info, err)) { err += ": " + path; return false; }
    return true;
}

bool CqmCodec::save(const std::string& path, const DiskMedium&, std::string& err) {
    err = "CopyQM-Abbilder (.cqm) koennen nur gelesen werden — bitte als .hfe, .dmk oder "
          ".img speichern: " + path;
    return false;
}
