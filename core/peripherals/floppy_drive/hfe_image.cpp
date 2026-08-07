/**
 * @file hfe_image.cpp
 * @brief HfeImage – DiskImage-Backend für HFE v1 ("HXCPICFE"), MFM und FM.
 *
 * Öffnet eine HFE-v1-Datei, liest den Header und die Track-LUT vollständig in
 * den Speicher (file_), de-interleavt die 256-Byte-Blöcke bei readTrack() und
 * decodiert den Bitzellen-Strom per @ref BitCodec::decode.  writeTrack() encodiert
 * zurück und re-interleavt die Seite in-place in file_; flush() speichert file_
 * nach path_.
 *
 * HFE-v1-Spurlayout: je Zylinder ein LUT-Eintrag (4 B: u16 offset [Blöcke],
 * u16 track_len [Bytes beider Seiten]).  Die Spurdaten liegen ab offset*512 als
 * verschränkte 256-Byte-Blöcke [S0_256][S1_256][S0_256][S1_256]…
 *
 * @see core/peripherals/floppy_drive/hfe_image.h
 * @see core/peripherals/floppy_drive/bit_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/hfe_image.h"
#include "core/peripherals/floppy_drive/bit_codec.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace {
/// Nominale Zellrate des K1520-Floppy-Stacks (Double Density) in kbit/s.
constexpr uint16_t kNominalBitrate = 250;
}

// ─── Konstruktor ─────────────────────────────────────────────────────────────

HfeImage::HfeImage(const std::string& path, bool write_protect)
    : path_(path)
    , write_protect_(write_protect)
{
    // Gesamte Datei in file_ laden.
    std::ifstream f(path_, std::ios::binary | std::ios::ate);
    if (!f) {
        last_error_ = "Kann HFE-Datei nicht öffnen: " + path_;
        return;
    }

    const auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < 512) {
        last_error_ = "HFE-Datei zu kurz (< 512 B): " + path_;
        return;
    }

    file_.resize(file_size);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(file_.data()),
                static_cast<std::streamsize>(file_size))) {
        last_error_ = "Lesefehler bei HFE-Datei: " + path_;
        file_.clear();
        return;
    }

    // ── Header parsen ────────────────────────────────────────────────────────

    // Signatur prüfen ("HXCPICFE")
    if (std::memcmp(file_.data(), "HXCPICFE", 8) != 0) {
        last_error_ = "Keine gültige HFE-v1-Signatur (erwartet: HXCPICFE): " + path_;
        file_.clear();
        return;
    }

    // formatrevision == 0 (v1)
    if (file_[0x08] != 0) {
        last_error_ = "Nicht unterstützte HFE-Formatversion: "
                      + std::to_string(file_[0x08]);
        file_.clear();
        return;
    }

    num_tracks_         = file_[0x09];
    num_sides_          = file_[0x0A];
    const uint8_t enc_byte = file_[0x0B];
    encoding_           = (enc_byte == 2) ? Encoding::FM : Encoding::MFM;

    // bitrate und rpm (little-endian u16)
    bitrate_ = static_cast<uint16_t>(file_[0x0C] | (file_[0x0D] << 8));
    rpm_     = static_cast<uint16_t>(file_[0x0E] | (file_[0x0F] << 8));

    track_list_block_   = static_cast<uint16_t>(file_[0x12] | (file_[0x13] << 8));
    hfe_write_allowed_  = (file_[0x14] == 0xFF);

    // Überabtastung erkennen.  Der K1520-Floppy-Stack (K5122 + 5,25″/8″-Laufwerke) kennt
    // ausschließlich Double Density mit 250 kbit/s Zellrate; ein HFE, das eine höhere Rate
    // meldet, ist keine HD-Diskette, sondern dieselbe DD-Diskette mit mehrfacher Abtastung
    // (typisch Greaseweazle-Export mit 500 kbit/s).  readTrack() rechnet sie herunter.
    if (bitrate_ >= kNominalBitrate + kNominalBitrate / 2)
        oversample_ = (bitrate_ + kNominalBitrate / 2) / kNominalBitrate;

    // ── LUT einlesen ─────────────────────────────────────────────────────────

    const size_t lut_off = static_cast<size_t>(track_list_block_) * 512;
    if (lut_off + static_cast<size_t>(num_tracks_) * 4 > file_size) {
        last_error_ = "HFE-LUT liegt außerhalb der Datei: " + path_;
        file_.clear();
        return;
    }

    lut_.resize(num_tracks_);
    for (uint8_t t = 0; t < num_tracks_; ++t) {
        const size_t e = lut_off + t * 4;
        lut_[t].offset_blocks = static_cast<uint16_t>(file_[e]     | (file_[e + 1] << 8));
        lut_[t].len_bytes     = static_cast<uint16_t>(file_[e + 2] | (file_[e + 3] << 8));
    }

    is_open_ = true;
}

// ─── geometry() ──────────────────────────────────────────────────────────────

DiskGeometry HfeImage::geometry() const {
    DiskGeometry g;
    g.num_cyls  = num_tracks_;
    g.num_heads = num_sides_;
    g.uniform   = true;
    g.encoding  = encoding_;
    return g;
}

// ─── readSideBytes() ─────────────────────────────────────────────────────────

/**
 * De-interleavet die HFE-Spurseiten-Bytes für (cyl, head).
 *
 * HFE-Layout: verschränkte 256-Byte-Blöcke [S0_256][S1_256][S0_256]…
 * De-Interleave Seite head:
 *   for blk: copy file_[track_start + blk*512 + head*256 : +min(256, rest)]
 */
std::vector<uint8_t> HfeImage::readSideBytes(uint8_t cyl, uint8_t head,
                                              uint32_t& bitcells_out) {
    const TrackEntry& e     = lut_[cyl];
    const size_t track_start = static_cast<size_t>(e.offset_blocks) * 512;
    const uint32_t side_len  = e.len_bytes / num_sides_;   // Bytes je Seite

    bitcells_out = side_len * 8;

    // Seitenaufteilung im 512-Byte-Block: HFE verschränkt zwei Seiten zu je 256 B
    // ([S0_256][S1_256]).  Bei EINSEITIGEN Medien (num_sides_==1, z. B. 8″-SD) gibt es
    // keine zweite Seite — die Spur liegt dann KONTINUIERLICH (volle 512 B/Block), sonst
    // würden 256 B/Block über das Spurende hinaus in die Folgespur lesen (Sektor-Doppelung).
    const uint32_t slot     = (num_sides_ == 1) ? 512u : 256u;
    const size_t   head_off = (num_sides_ == 1) ? 0u : static_cast<size_t>(head) * 256;

    std::vector<uint8_t> side;
    side.reserve(side_len);

    uint32_t done = 0;
    for (uint32_t blk = 0; done < side_len; ++blk) {
        const size_t blk_start = track_start
                                 + static_cast<size_t>(blk) * 512
                                 + head_off;
        const uint32_t remaining = side_len - done;
        const uint32_t copy_len  = (remaining < slot) ? remaining : slot;

        if (blk_start + copy_len > file_.size()) break;  // Datei kürzer als erwartet

        side.insert(side.end(),
                    file_.begin() + static_cast<ptrdiff_t>(blk_start),
                    file_.begin() + static_cast<ptrdiff_t>(blk_start + copy_len));
        done += copy_len;
    }

    return side;
}

// ─── writeSideBytes() ────────────────────────────────────────────────────────

/**
 * Schreibt die Spurseiten-Bytes für (cyl, head) zurück in file_ (in-place).
 *
 * Nur die Seite head wird überschrieben; die jeweils andere Seite bleibt.
 * Falls side kürzer als track_len/2 → mit 0x88 auffüllen; länger → abschneiden.
 * Header und LUT bleiben unverändert.
 */
bool HfeImage::writeSideBytes(uint8_t cyl, uint8_t head,
                               const std::vector<uint8_t>& side) {
    const TrackEntry& e      = lut_[cyl];
    const size_t track_start = static_cast<size_t>(e.offset_blocks) * 512;
    const uint32_t side_len  = e.len_bytes / num_sides_;

    // Siehe readSideBytes(): einseitige Medien (num_sides_==1) speichern die Spur
    // KONTINUIERLICH (volle 512 B/Block), doppelseitige verschränkt (256 B/Seite).
    const uint32_t slot     = (num_sides_ == 1) ? 512u : 256u;
    const size_t   head_off = (num_sides_ == 1) ? 0u : static_cast<size_t>(head) * 256;

    uint32_t src_off = 0;    // Byte-Offset in side
    for (uint32_t blk = 0; src_off < side_len; ++blk) {
        const size_t blk_start = track_start
                                 + static_cast<size_t>(blk) * 512
                                 + head_off;
        const uint32_t remaining = side_len - src_off;
        const uint32_t copy_len  = (remaining < slot) ? remaining : slot;

        if (blk_start + copy_len > file_.size()) return false;

        for (uint32_t i = 0; i < slot; ++i) {
            const size_t dst = blk_start + i;
            if (dst >= file_.size()) break;
            if (i < copy_len) {
                const uint32_t src_idx = src_off + i;
                file_[dst] = (src_idx < side.size())
                             ? side[src_idx]
                             : static_cast<uint8_t>(0x88);   // Gap-Füller
            } else {
                // Hinteres Segment: Gap bis Blockende (letzter Block kürzer)
                file_[dst] = 0x88;
            }
        }
        src_off += copy_len;
    }
    return true;
}

// ─── readTrack() ─────────────────────────────────────────────────────────────

TrackImage HfeImage::readTrack(uint8_t cyl, uint8_t head) {
    if (!is_open_)            return {};
    if (cyl  >= num_tracks_)  return {};
    if (head >= num_sides_)   return {};

    uint32_t bitcells = 0;
    auto side = readSideBytes(cyl, head, bitcells);

    // Überabgetastete Aufnahme (bitrate > Nominalrate) auf die Nominalrate quantisieren,
    // sonst belegt jede MFM-Zelle mehrere Bits und kein Sync-Zellwort passt (s. Konstruktor).
    if (oversample_ > 1)
        side = BitCodec::downsampleCells(side, bitcells, oversample_, bitcells);

    // Per-Spur-Verfahrenserkennung für MISCHDICHTE-Medien (z. B. 8″-DD System-34:
    // FM-Systemspuren + MFM-Datenspuren).  HFE v1 trägt nur EIN Header-Verfahren; FM-
    // und MFM-Sync-Zellworte sind aber disjunkt, sodass ein Decode im falschen Verfahren
    // KEINE Adressmarke findet.  Erst mit dem Header-Verfahren decodieren; bringt das
    // keine IDAM/DAM (leere/gap-Spur ODER Fremdverfahren), das andere Verfahren versuchen
    // und die Variante mit Marken übernehmen.  Reine FM-/MFM-Disks bleiben unverändert.
    auto marks_count = [](const TrackImage& t) {
        size_t n = 0;
        for (MarkType m : t.marks)
            if (m == MarkType::Id || m == MarkType::Data) ++n;
        return n;
    };
    TrackImage t = BitCodec::decode(side, bitcells, encoding_);
    if (marks_count(t) == 0) {
        const Encoding other = (encoding_ == Encoding::MFM) ? Encoding::FM : Encoding::MFM;
        TrackImage alt = BitCodec::decode(side, bitcells, other);
        if (marks_count(alt) > 0)
            t = std::move(alt);
    }
    return t;
}

// ─── writeTrack() ────────────────────────────────────────────────────────────

bool HfeImage::writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) {
    if (!writable())          return false;
    if (cyl  >= num_tracks_)  return false;
    if (head >= num_sides_)   return false;

    // Ziel-Bitzellenzahl: aus TrackImage oder aus LUT ableiten.
    const uint32_t side_len_bytes = lut_[cyl].len_bytes / num_sides_;
    const uint32_t target_bits    = (track.bitcells != 0)
                                    ? track.bitcells
                                    : side_len_bytes * 8;

    auto side = BitCodec::encode(track, target_bits);
    if (!writeSideBytes(cyl, head, side)) return false;

    dirty_ = true;
    return true;
}

// ─── flush() ─────────────────────────────────────────────────────────────────

bool HfeImage::flush() {
    if (!dirty_)       return true;
    if (!writable())   return false;

    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f) {
        last_error_ = "Schreibfehler bei HFE-Datei: " + path_;
        return false;
    }
    f.write(reinterpret_cast<const char*>(file_.data()),
            static_cast<std::streamsize>(file_.size()));
    if (!f) {
        last_error_ = "Schreibfehler (unvollständig): " + path_;
        return false;
    }

    dirty_ = false;
    return true;
}
