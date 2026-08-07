/**
 * @file disk_medium.cpp
 * @brief Implementierung von DiskMedium (internes Diskettenabbild).
 *
 * @see core/peripherals/floppy_drive/disk_medium.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/disk_medium.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <utility>

namespace {
/// Statische Rückfallwerte für Zugriffe außerhalb der Geometrie.
const TrackImage kLeer{};
TrackImage       g_dummy{};

/// @brief Zählt ein Byte als reiner Gap-Füller hinter der Daten-CRC?
///
/// 0x4E = MFM-Gap, 0xFF = FM-Gap, 0x00 = Sync/Null-Auslauf.  Alles andere ist
/// Nutzinhalt, den ein rohes Sektorimage nicht speichern kann.
bool istGapFueller(uint8_t b) {
    return b == 0x4E || b == 0xFF || b == 0x00;
}
}  // namespace

// ─── Konstruktion / Geometrie ────────────────────────────────────────────────

DiskMedium::DiskMedium(uint8_t num_cyls, uint8_t num_heads, Encoding default_enc)
    : default_enc_(default_enc) {
    resize(num_cyls, num_heads);
}

void DiskMedium::resize(uint8_t num_cyls, uint8_t num_heads) {
    if (num_cyls == num_cyls_ && num_heads == num_heads_) return;

    const size_t n = static_cast<size_t>(num_cyls) * num_heads;
    std::vector<TrackImage> neu(n);
    std::vector<uint8_t>    neu_dirty(n, 0);

    // Vorhandene Spuren an ihrer (cyl, head)-Position übernehmen.
    for (uint8_t c = 0; c < num_cyls && c < num_cyls_; ++c) {
        for (uint8_t h = 0; h < num_heads && h < num_heads_; ++h) {
            neu[static_cast<size_t>(c) * num_heads + h] = std::move(tracks_[index(c, h)]);
            neu_dirty[static_cast<size_t>(c) * num_heads + h] = dirty_[index(c, h)];
        }
    }

    num_cyls_  = num_cyls;
    num_heads_ = num_heads;
    tracks_    = std::move(neu);
    dirty_     = std::move(neu_dirty);
    raw_ok_.assign(n, -1);
}

DiskGeometry DiskMedium::geometry() const {
    DiskGeometry g;
    g.num_cyls  = num_cyls_;
    g.num_heads = num_heads_;
    g.encoding  = default_enc_;

    // uniform = alle nicht-leeren Spuren tragen dasselbe Verfahren und dieselbe Länge.
    bool   erster = true;
    bool   gleich = true;
    Encoding enc  = default_enc_;
    size_t   len  = 0;
    for (const TrackImage& t : tracks_) {
        if (t.empty()) continue;
        if (erster) { enc = t.encoding; len = t.size(); erster = false; continue; }
        if (t.encoding != enc || t.size() != len) { gleich = false; break; }
    }
    g.uniform = gleich;
    return g;
}

// ─── Spurzugriff ─────────────────────────────────────────────────────────────

const TrackImage& DiskMedium::track(uint8_t cyl, uint8_t head) const {
    if (!valid(cyl, head)) return kLeer;
    return tracks_[index(cyl, head)];
}

TrackImage& DiskMedium::mutableTrack(uint8_t cyl, uint8_t head) {
    if (!valid(cyl, head)) { g_dummy = {}; return g_dummy; }
    markDirty(cyl, head);
    return tracks_[index(cyl, head)];
}

void DiskMedium::setTrack(uint8_t cyl, uint8_t head, TrackImage t) {
    if (!valid(cyl, head)) return;
    tracks_[index(cyl, head)] = std::move(t);
    markDirty(cyl, head);
}

void DiskMedium::markDirty(uint8_t cyl, uint8_t head) {
    if (!valid(cyl, head)) return;
    dirty_[index(cyl, head)]  = 1;
    raw_ok_[index(cyl, head)] = -1;   // Tauglichkeit neu bestimmen
    dirty_any_                = true;
    ++revision_;                      // Autosave: Schreibpause erkennen
}

bool DiskMedium::trackDirty(uint8_t cyl, uint8_t head) const {
    return valid(cyl, head) && dirty_[index(cyl, head)] != 0;
}

void DiskMedium::clearDirty() {
    for (auto& d : dirty_) d = 0;
    dirty_any_ = false;
}

// ─── Zustandsabfragen ────────────────────────────────────────────────────────

bool DiskMedium::formatted() const {
    for (const TrackImage& t : tracks_)
        for (MarkType m : t.marks)
            if (m == MarkType::Id || m == MarkType::Data) return true;
    return false;
}

bool DiskMedium::computeRawCompatible(const TrackImage& t) {
    if (t.empty()) return false;   // unformatierte Spur ist in .img nicht darstellbar

    const auto sektoren = TrackCodec::parseTrack(t);
    if (sektoren.empty()) return false;

    for (const LogicalSector& s : sektoren) {
        if (!s.id_crc_ok || !s.data_crc_ok) return false;
        // Bytes hinter der Daten-CRC: nur Gap zulässig.  Alles andere (z. B. der
        // UDOS-Sektorkontrollblock) ginge beim Speichern als .img verloren.
        for (uint8_t b : s.tail)
            if (!istGapFueller(b)) return false;
    }
    return true;
}

bool DiskMedium::trackRawCompatible(uint8_t cyl, uint8_t head) const {
    if (!valid(cyl, head)) return false;
    const size_t i = index(cyl, head);
    if (raw_ok_[i] < 0)
        raw_ok_[i] = computeRawCompatible(tracks_[i]) ? 1 : 0;
    return raw_ok_[i] != 0;
}

bool DiskMedium::rawCompatible() const {
    return rawIncompatibleReason().empty();
}

std::string DiskMedium::rawIncompatibleReason() const {
    if (tracks_.empty())  return "kein Medium";
    if (!formatted())     return "unformatiert";
    for (uint8_t c = 0; c < num_cyls_; ++c)
        for (uint8_t h = 0; h < num_heads_; ++h) {
            // Leere Spuren sind erlaubt (werden im .img zu Füllbytes); nur beschriebene
            // Spuren muessen sich verlustfrei auf Sektor-Nutzdaten abbilden lassen.
            if (track(c, h).empty()) continue;
            if (!trackRawCompatible(c, h))
                return "Spur " + std::to_string(c) + "/" + std::to_string(h);
        }
    return "";
}
