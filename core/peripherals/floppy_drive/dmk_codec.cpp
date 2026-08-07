/**
 * @file dmk_codec.cpp
 * @brief Implementierung von DmkCodec (David Keil's Disk Image ⇄ DiskMedium).
 *
 * @see core/peripherals/floppy_drive/dmk_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/dmk_codec.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace {

constexpr size_t   kHeaderBytes = 16;    ///< Datei-Header
constexpr size_t   kIdamBytes   = 128;   ///< IDAM-Tabelle je Spur
constexpr size_t   kIdamSlots   = 64;    ///< 64 × u16
constexpr uint16_t kIdamMfm     = 0x8000;
constexpr uint16_t kIdamOffMask = 0x3FFF;
constexpr uint8_t  kOptSingleSided = 0x10;
constexpr uint8_t  kOptSingleDens  = 0x40;

/// @brief Maximaler Abstand IDAM→DAM, in dem die Datenmarke gesucht wird.
///
/// Norm-Gap2 ist 22 B (MFM) bzw. 11 B (FM); mit ID-Feld (4 B) + CRC (2 B) + Sync
/// bleibt man deutlich unter 60.  Ein enges Fenster verhindert, dass ein 0xFB in
/// den Nutzdaten als Datenmarke missdeutet wird.
constexpr size_t kDamSuchfenster = 60;

uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

void wr16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off]     = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

bool leseDatei(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "Kann DMK-Datei nicht oeffnen: " + path; return false; }
    const auto n = static_cast<size_t>(f.tellg());
    out.resize(n);
    f.seekg(0);
    if (n && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n))) {
        err = "Lesefehler bei DMK-Datei: " + path;
        return false;
    }
    return true;
}

/// @brief Header-Plausibilitaet (DMK hat keine Signatur).
/// @param kopf      mindestens die ersten 16 Bytes der Datei
/// @param dateigroesse tatsaechliche Dateigroesse
bool headerPlausibel(const std::vector<uint8_t>& kopf, size_t dateigroesse) {
    if (kopf.size() < kHeaderBytes) return false;
    if (kopf[0] != 0x00 && kopf[0] != 0xFF) return false;

    const uint8_t  n_tracks  = kopf[1];
    const uint16_t track_len = static_cast<uint16_t>(kopf[2] | (kopf[3] << 8));
    if (n_tracks == 0 || n_tracks > 96)                return false;
    if (track_len <= kIdamBytes || track_len > 0x4000) return false;
    for (size_t i = 5; i <= 11; ++i) if (kopf[i] != 0) return false;

    const uint8_t n_sides = (kopf[4] & kOptSingleSided) ? 1 : 2;
    const size_t  soll    = kHeaderBytes
                            + static_cast<size_t>(n_tracks) * n_sides * track_len;
    return dateigroesse >= soll;
}

/// @brief Ist @p b eine Datenadressmarke (DAM / geloeschte DAM)?
bool istDam(uint8_t b) { return b >= 0xF8 && b <= 0xFB; }

}  // namespace

// ─── Erkennung ───────────────────────────────────────────────────────────────

bool DmkCodec::looksLikeDmk(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto n = static_cast<size_t>(f.tellg());
    if (n < kHeaderBytes) return false;

    std::vector<uint8_t> kopf(kHeaderBytes);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(kopf.data()), kHeaderBytes)) return false;
    return headerPlausibel(kopf, n);
}

// ─── Laden ───────────────────────────────────────────────────────────────────

bool DmkCodec::load(const std::string& path, DiskMedium& out, SourceInfo* info,
                    std::string& err) {
    std::vector<uint8_t> file;
    if (!leseDatei(path, file, err)) return false;
    if (!headerPlausibel(file, file.size())) {
        err = "Kein plausibler DMK-Header: " + path;
        return false;
    }

    const uint8_t  n_tracks  = file[1];
    const uint16_t track_len = rd16(file, 2);
    const uint8_t  opts      = file[4];
    const uint8_t  n_sides   = (opts & kOptSingleSided) ? 1 : 2;
    const bool     sd_only   = (opts & kOptSingleDens) != 0;

    out = DiskMedium(n_tracks, n_sides, sd_only ? Encoding::FM : Encoding::MFM);

    for (uint8_t c = 0; c < n_tracks; ++c) {
        for (uint8_t h = 0; h < n_sides; ++h) {
            const size_t idx  = static_cast<size_t>(c) * n_sides + h;
            const size_t base = kHeaderBytes + idx * track_len;
            if (base + track_len > file.size()) continue;

            // 1. IDAM-Tabelle einlesen.
            struct Idam { size_t off; bool mfm; };
            std::vector<Idam> idams;
            bool irgendein_mfm = false;
            for (size_t k = 0; k < kIdamSlots; ++k) {
                const uint16_t e = rd16(file, base + k * 2);
                if (e == 0) continue;
                const size_t off = e & kIdamOffMask;
                if (off < kIdamBytes || off >= track_len) continue;
                const bool mfm = (e & kIdamMfm) != 0;
                irgendein_mfm  = irgendein_mfm || mfm;
                idams.push_back({off, mfm});
            }

            // Keine Adressmarke → unformatierte Spur (leeres TrackImage).
            if (idams.empty()) { out.setTrack(c, h, {}); continue; }

            // 2. Verfahren + Byte-Verdopplung.  Reine FM-Spuren speichert DMK mit
            //    doppelten Bytes, sofern die Diskette nicht als SD-only deklariert ist.
            const Encoding enc      = irgendein_mfm ? Encoding::MFM : Encoding::FM;
            const bool     doppelt  = (enc == Encoding::FM) && !sd_only;
            const size_t   schritt  = doppelt ? 2 : 1;

            const size_t roh_start = base + kIdamBytes;
            const size_t roh_len   = track_len - kIdamBytes;

            TrackImage t;
            t.encoding = enc;
            t.bytes.reserve(roh_len / schritt);
            for (size_t i = 0; i < roh_len; i += schritt)
                t.bytes.push_back(file[roh_start + i]);
            t.marks.assign(t.bytes.size(), MarkType::None);

            // 3. Marken setzen: IDAM aus der Tabelle, DAM per Suche im Gap dahinter.
            for (size_t k = 0; k < idams.size(); ++k) {
                const size_t pos = (idams[k].off - kIdamBytes) / schritt;
                if (pos >= t.bytes.size()) continue;
                t.marks[pos] = MarkType::Id;

                // Suchgrenze: bis zur naechsten IDAM, hoechstens kDamSuchfenster.
                size_t grenze = std::min(pos + kDamSuchfenster, t.bytes.size());
                if (k + 1 < idams.size()) {
                    const size_t next = (idams[k + 1].off - kIdamBytes) / schritt;
                    grenze = std::min(grenze, next);
                }
                for (size_t p = pos + 1; p < grenze; ++p) {
                    if (!istDam(t.bytes[p])) continue;
                    // MFM: die Datenmarke steht hinter der A1-A1-A1-Sync; ohne diese
                    // Pruefung koennte ein Gap-/CRC-Byte als Marke durchgehen.
                    if (enc == Encoding::MFM) {
                        if (p < 3 || t.bytes[p - 1] != 0xA1 || t.bytes[p - 2] != 0xA1
                            || t.bytes[p - 3] != 0xA1)
                            continue;
                    }
                    t.marks[p] = MarkType::Data;
                    break;
                }
            }

            // 4. Indexmarke (nur MFM zuverlaessig erkennbar: C2 C2 C2 FC).
            if (enc == Encoding::MFM) {
                const size_t erste_idam = (idams.front().off - kIdamBytes) / schritt;
                for (size_t p = 3; p + 1 < erste_idam && p < t.bytes.size(); ++p) {
                    if (t.bytes[p] == 0xFC && t.bytes[p - 1] == 0xC2
                        && t.bytes[p - 2] == 0xC2 && t.bytes[p - 3] == 0xC2) {
                        t.marks[p] = MarkType::Index;
                        break;
                    }
                }
            }

            out.setTrack(c, h, std::move(t));
        }
    }

    out.clearDirty();
    if (info) info->write_allowed = (file[0] != 0xFF);
    return true;
}

// ─── Speichern ───────────────────────────────────────────────────────────────

bool DmkCodec::save(const std::string& path, const DiskMedium& in, std::string& err) {
    const uint8_t n_cyls  = in.numCylinders();
    const uint8_t n_sides = in.numHeads();
    if (n_cyls == 0 || n_sides == 0) { err = "Leeres Medium"; return false; }
    if (n_sides > 2) { err = "DMK unterstuetzt hoechstens 2 Seiten"; return false; }

    // SD-only: alle belegten Spuren sind FM → ohne Byte-Verdopplung schreiben
    // (halbiert die Dateigroesse und entspricht dem Header-Bit 6).
    bool nur_fm  = true;
    bool belegt  = false;
    for (uint8_t c = 0; c < n_cyls; ++c)
        for (uint8_t h = 0; h < n_sides; ++h) {
            const TrackImage& t = in.track(c, h);
            if (t.empty()) continue;
            belegt = true;
            if (t.encoding != Encoding::FM) nur_fm = false;
        }
    const bool sd_only = belegt && nur_fm;

    auto verdoppelt = [&](const TrackImage& t) {
        return t.encoding == Encoding::FM && !sd_only;
    };

    // Spurlaenge: 128 B IDAM-Tabelle + laengster Rohstrom, aufgerundet auf 256.
    size_t max_roh = 0;
    for (uint8_t c = 0; c < n_cyls; ++c)
        for (uint8_t h = 0; h < n_sides; ++h) {
            const TrackImage& t = in.track(c, h);
            max_roh = std::max(max_roh, t.size() * (verdoppelt(t) ? 2u : 1u));
        }
    if (max_roh == 0) max_roh = 6250;   // leere Diskette: nominale MFM-Spurlaenge

    size_t track_len = kIdamBytes + max_roh;
    track_len = (track_len + 255) / 256 * 256;
    if (track_len > 0x4000) {
        err = "Spur zu lang fuer DMK (" + std::to_string(track_len) + " B)";
        return false;
    }

    std::vector<uint8_t> hdr(kHeaderBytes, 0x00);
    hdr[0] = 0x00;                                     // nicht schreibgeschuetzt
    hdr[1] = n_cyls;
    wr16(hdr, 2, static_cast<uint16_t>(track_len));
    hdr[4] = static_cast<uint8_t>((n_sides == 1 ? kOptSingleSided : 0)
                                  | (sd_only ? kOptSingleDens : 0));

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { err = "Kann DMK-Datei nicht schreiben: " + path; return false; }
    f.write(reinterpret_cast<const char*>(hdr.data()), kHeaderBytes);

    std::vector<uint8_t> spur(track_len);
    for (uint8_t c = 0; c < n_cyls; ++c) {
        for (uint8_t h = 0; h < n_sides; ++h) {
            const TrackImage& t   = in.track(c, h);
            const bool        dbl = verdoppelt(t);
            const uint8_t     gap = (t.encoding == Encoding::FM) ? 0xFF : 0x4E;

            std::fill(spur.begin(), spur.begin() + kIdamBytes, uint8_t{0});
            std::fill(spur.begin() + kIdamBytes, spur.end(), t.empty() ? uint8_t{0xFF} : gap);

            if (!t.empty()) {
                // Rohstrom (ggf. verdoppelt) ablegen.
                size_t w = kIdamBytes;
                for (size_t i = 0; i < t.bytes.size() && w < track_len; ++i) {
                    spur[w++] = t.bytes[i];
                    if (dbl && w < track_len) spur[w++] = t.bytes[i];
                }
                // IDAM-Tabelle aus den Markenpositionen.
                size_t slot = 0;
                for (size_t i = 0; i < t.marks.size() && slot < kIdamSlots; ++i) {
                    if (t.marks[i] != MarkType::Id) continue;
                    const size_t off = kIdamBytes + i * (dbl ? 2 : 1);
                    if (off > kIdamOffMask || off >= track_len) break;
                    uint16_t e = static_cast<uint16_t>(off);
                    if (t.encoding == Encoding::MFM) e |= kIdamMfm;
                    wr16(spur, slot * 2, e);
                    ++slot;
                }
            }

            f.write(reinterpret_cast<const char*>(spur.data()),
                    static_cast<std::streamsize>(track_len));
        }
    }

    if (!f) { err = "Schreibfehler (unvollstaendig): " + path; return false; }
    return true;
}
