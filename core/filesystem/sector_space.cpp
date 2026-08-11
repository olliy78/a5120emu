/**
 * @file sector_space.cpp
 * @brief Umsetzung von @ref SectorSpace.
 *
 * @see doc/design/13_k1520disktool.md §5
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/sector_space.h"

#include <algorithm>

// ─── Aufbau ──────────────────────────────────────────────────────────────────

SectorSpace::SectorSpace(DiskMedium& medium, const DiskFormat& fmt, uint8_t head_filter)
    : medium_(medium), fmt_(fmt), head_filter_(head_filter) {

    // Layout-Reihenfolge des .img-Codecs: Zylinder aussen, Kopf innen.  Nur Spuren, die
    // das Format kennt UND die zum Kopf-Filter passen, gehoeren zum Raum.
    const uint8_t ncyls  = fmt_.numCylinders();
    const uint8_t nheads = fmt_.numHeads();

    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            if (head_filter_ != kAllHeads && h != head_filter_) continue;
            const TrackFormat* tf = fmt_.findTrack(c, h);
            if (!tf) continue;

            Slot s;
            s.cyl   = c;
            s.phys  = fmt_.physicalCylinder(c);
            s.head  = h;
            s.start = total_bytes_;
            s.bytes = tf->trackBytes();
            s.tf    = tf;
            total_bytes_ += s.bytes;
            slots_.push_back(s);
        }
    }

    cache_.resize(slots_.size());
    cache_gueltig_.assign(slots_.size(), 0);
    cache_revision_ = medium_.revision();
}

// ─── Spur-Nachschlag ─────────────────────────────────────────────────────────

int SectorSpace::slotOf(uint8_t cyl, uint8_t head) const {
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].cyl == cyl && slots_[i].head == head) return static_cast<int>(i);
    return -1;
}

int SectorSpace::slotAt(uint64_t offset) const {
    if (offset >= total_bytes_) return -1;
    // Slots sind nach start_ aufsteigend und lueckenlos — binaere Suche.
    size_t lo = 0, hi = slots_.size();
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (slots_[mid].start <= offset) lo = mid; else hi = mid;
    }
    return static_cast<int>(lo);
}

const std::vector<LogicalSector>& SectorSpace::sectors(int slot) const {
    // Jede Aenderung am Medium (auch durch Dritte) erhoeht die Revision — dann ist der
    // gesamte Zwischenspeicher wertlos.  Lieber grob verwerfen als falsch antworten.
    if (medium_.revision() != cache_revision_) {
        std::fill(cache_gueltig_.begin(), cache_gueltig_.end(), 0);
        cache_revision_ = medium_.revision();
    }
    if (!cache_gueltig_[static_cast<size_t>(slot)]) {
        const Slot& s = slots_[static_cast<size_t>(slot)];
        cache_[static_cast<size_t>(slot)] =
            TrackCodec::parseTrack(medium_.track(s.phys, s.head));
        cache_gueltig_[static_cast<size_t>(slot)] = 1;
    }
    return cache_[static_cast<size_t>(slot)];
}

// ─── Spuren des Raums ────────────────────────────────────────────────────────

SectorSpace::TrackRef SectorSpace::trackAt(size_t i) const {
    TrackRef r;
    if (i >= slots_.size()) return r;
    const Slot& s   = slots_[i];
    r.cyl           = s.cyl;
    r.head          = s.head;
    r.start         = s.start;
    r.bytes         = s.bytes;
    r.sectors       = s.tf->secs_per_track;
    r.sector_size   = s.tf->bytes_per_sec;
    r.first_id      = s.tf->first_sector_id;
    return r;
}

// ─── Geometrie-Auskuenfte ────────────────────────────────────────────────────

uint16_t SectorSpace::sectorSize(uint8_t cyl, uint8_t head) const {
    const int s = slotOf(cyl, head);
    return s < 0 ? 0 : slots_[static_cast<size_t>(s)].tf->bytes_per_sec;
}

uint8_t SectorSpace::sectorsPerTrack(uint8_t cyl, uint8_t head) const {
    const int s = slotOf(cyl, head);
    return s < 0 ? 0 : slots_[static_cast<size_t>(s)].tf->secs_per_track;
}

uint8_t SectorSpace::firstSectorId(uint8_t cyl, uint8_t head) const {
    const int s = slotOf(cyl, head);
    return s < 0 ? 0 : slots_[static_cast<size_t>(s)].tf->first_sector_id;
}

bool SectorSpace::trackFormatted(uint8_t cyl, uint8_t head) const {
    const int s = slotOf(cyl, head);
    if (s < 0) return false;
    return !sectors(s).empty();
}

// ─── physisch ────────────────────────────────────────────────────────────────

bool SectorSpace::readSector(uint8_t cyl, uint8_t head, uint8_t id, SectorData& out) const {
    const int slot = slotOf(cyl, head);
    if (slot < 0) {
        last_error_ = "Spur " + std::to_string(cyl) + "/" + std::to_string(head)
                    + " gehoert nicht zu diesem Datentraeger";
        return false;
    }
    for (const LogicalSector& ls : sectors(slot)) {
        if (ls.id != id) continue;
        out.data        = ls.data;
        out.tail        = ls.tail;
        out.id_crc_ok   = ls.id_crc_ok;
        out.data_crc_ok = ls.data_crc_ok;
        return true;
    }
    last_error_ = "Sektor " + std::to_string(id) + " auf Spur " + std::to_string(cyl)
                + "/" + std::to_string(head) + " nicht gefunden";
    return false;
}

bool SectorSpace::writeSector(uint8_t cyl, uint8_t head, uint8_t id,
                              const std::vector<uint8_t>& data,
                              const std::vector<uint8_t>& tail) {
    const int slot = slotOf(cyl, head);
    if (slot < 0) {
        last_error_ = "Spur " + std::to_string(cyl) + "/" + std::to_string(head)
                    + " gehoert nicht zu diesem Datentraeger";
        return false;
    }

    // mutableTrack() markiert die Spur sofort als geaendert — auch wenn writeSector
    // gleich scheitert.  Das ist bewusst konservativ: eine ueberfluessig als schmutzig
    // markierte Spur kostet einen Rueckschreibvorgang, eine verpasste kostet Daten.
    TrackImage& t = medium_.mutableTrack(slots_[static_cast<size_t>(slot)].phys, head);
    if (!TrackCodec::writeSector(t, id, data, tail)) {
        last_error_ = "Sektor " + std::to_string(id) + " auf Spur " + std::to_string(cyl)
                    + "/" + std::to_string(head) + " nicht beschreibbar (fehlt, falsche "
                      "Laenge oder unformatiert)";
        return false;
    }
    return true;
}

// ─── linear ──────────────────────────────────────────────────────────────────

int64_t SectorSpace::offsetOf(uint8_t cyl, uint8_t head) const {
    const int s = slotOf(cyl, head);
    return s < 0 ? -1 : static_cast<int64_t>(slots_[static_cast<size_t>(s)].start);
}

int64_t SectorSpace::offsetOf(uint8_t cyl, uint8_t head, uint8_t id) const {
    const int s = slotOf(cyl, head);
    if (s < 0) return -1;
    const Slot& sl = slots_[static_cast<size_t>(s)];
    const int first = sl.tf->first_sector_id;
    if (id < first || id >= first + sl.tf->secs_per_track) return -1;
    return static_cast<int64_t>(sl.start
         + static_cast<uint64_t>(id - first) * sl.tf->bytes_per_sec);
}

bool SectorSpace::read(uint64_t offset, uint8_t* dst, size_t n) const {
    if (offset + n > total_bytes_) {
        last_error_ = "Lesen ueber das Ende des Datentraegers hinaus";
        return false;
    }
    SectorData sec;
    while (n > 0) {
        const int slot = slotAt(offset);
        if (slot < 0) { last_error_ = "Offset ausserhalb des Datentraegers"; return false; }
        const Slot& sl = slots_[static_cast<size_t>(slot)];

        const uint64_t lokal   = offset - sl.start;
        const uint16_t secSize = sl.tf->bytes_per_sec;
        const uint8_t  id      = static_cast<uint8_t>(sl.tf->first_sector_id + lokal / secSize);
        const size_t   inSec   = static_cast<size_t>(lokal % secSize);

        if (!readSector(sl.cyl, sl.head, id, sec)) return false;
        if (sec.data.size() < secSize) {
            last_error_ = "Sektor " + std::to_string(id) + " auf Spur "
                        + std::to_string(sl.cyl) + "/" + std::to_string(sl.head)
                        + " ist kuerzer als das Format angibt";
            return false;
        }

        const size_t chunk = std::min(n, static_cast<size_t>(secSize) - inSec);
        std::copy(sec.data.begin() + static_cast<long>(inSec),
                  sec.data.begin() + static_cast<long>(inSec + chunk), dst);
        dst    += chunk;
        offset += chunk;
        n      -= chunk;
    }
    return true;
}

bool SectorSpace::write(uint64_t offset, const uint8_t* src, size_t n) {
    if (offset + n > total_bytes_) {
        last_error_ = "Schreiben ueber das Ende des Datentraegers hinaus";
        return false;
    }
    SectorData sec;
    while (n > 0) {
        const int slot = slotAt(offset);
        if (slot < 0) { last_error_ = "Offset ausserhalb des Datentraegers"; return false; }
        const Slot& sl = slots_[static_cast<size_t>(slot)];

        const uint64_t lokal   = offset - sl.start;
        const uint16_t secSize = sl.tf->bytes_per_sec;
        const uint8_t  id      = static_cast<uint8_t>(sl.tf->first_sector_id + lokal / secSize);
        const size_t   inSec   = static_cast<size_t>(lokal % secSize);
        const size_t   chunk   = std::min(n, static_cast<size_t>(secSize) - inSec);

        // Teilsektor → lesen, aendern, zurueckschreiben.  Der Nachspann bleibt dabei
        // stehen (leeres tail-Argument), sonst verloere eine UDOS-Diskette bei jedem
        // Schreiben ihre Verkettung.
        if (!readSector(sl.cyl, sl.head, id, sec)) return false;
        if (sec.data.size() < secSize) {
            last_error_ = "Sektor " + std::to_string(id) + " auf Spur "
                        + std::to_string(sl.cyl) + "/" + std::to_string(sl.head)
                        + " ist kuerzer als das Format angibt";
            return false;
        }
        std::copy(src, src + chunk, sec.data.begin() + static_cast<long>(inSec));
        if (!writeSector(sl.cyl, sl.head, id, sec.data)) return false;

        src    += chunk;
        offset += chunk;
        n      -= chunk;
    }
    return true;
}
