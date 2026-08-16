/**
 * @file track_view.cpp
 * @brief Umsetzung von @ref scanTrack — Spur → Abschnittsfolge.
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "track_view.h"

#include <algorithm>

namespace {

/// @brief Ein Sektorabschnitt aus einem geparsten Sektor (ohne Winkel).
TrackSpan ausSektor(const LogicalSector& s, int index) {
    TrackSpan sp;
    sp.kind        = TrackSpan::Kind::Sector;
    sp.index       = index;
    sp.cyl         = s.cyl;
    sp.head        = s.head;
    sp.id          = s.id;
    sp.size        = s.size;
    sp.id_crc_ok   = s.id_crc_ok;
    sp.data_crc_ok = s.data_crc_ok;
    sp.deleted     = s.deleted;
    // Nur `data`, nicht `tail`: der UDOS-Kontrollblock ist auch auf einer leeren
    // Diskette belegt (Dateiverkettung) — mit ihm saehe dort nichts leer aus.
    sp.blank       = !s.data.empty()
                  && std::all_of(s.data.begin(), s.data.end(),
                                 [&](uint8_t b) { return b == s.data.front(); });
    return sp;
}

}  // namespace

TrackView scanTrack(const TrackImage& track) {
    TrackView v;
    v.encoding = track.encoding;
    v.bytes    = track.bytes.size();

    // Eine Spur ohne Bytes gibt es in dieser Geometrie schlicht nicht.
    TrackSpan ganze_runde;                       // [0,1) am Stueck
    ganze_runde.kind = TrackSpan::Kind::Unformatted;
    if (track.bytes.empty()) {
        v.spans.push_back(ganze_runde);
        return v;
    }
    v.exists = true;

    const double n = static_cast<double>(track.bytes.size());
    const std::vector<LogicalSector> sektoren = TrackCodec::parseTrack(track);

    // Markenloser Gap-Fluss (createBlank) zählt als unformatiert, nicht als ein
    // einziger riesiger Gap: der Unterschied ist, ob ein Gast hier formatieren muss.
    if (sektoren.empty()) {
        v.spans.push_back(ganze_runde);
        return v;
    }
    v.formatted = true;

    // Sektoren als Intervalle [sync_pos, end_pos) einsammeln, nach Lage sortiert.
    struct Bereich { size_t von, bis; TrackSpan span; };
    std::vector<Bereich> bereiche;
    for (size_t i = 0; i < sektoren.size(); ++i) {
        const LogicalSector& s = sektoren[i];
        if (s.sync_pos == SIZE_MAX) continue;
        // Ein Sektor ohne (vollständiges) Datenfeld endet hinter seinem ID-Feld —
        // sonst verschluckte er den Rest der Spur.
        const size_t bis = (s.end_pos != SIZE_MAX) ? s.end_pos
                         : std::min(s.id_pos + 1 + 4 + 2, track.bytes.size());
        bereiche.push_back({s.sync_pos, bis, ausSektor(s, static_cast<int>(i))});
        ++v.sectors;
    }
    std::sort(bereiche.begin(), bereiche.end(),
              [](const Bereich& a, const Bereich& b) { return a.von < b.von; });

    auto gap = [&](size_t von, size_t bis) {
        if (bis <= von) return;
        TrackSpan sp;
        sp.kind  = TrackSpan::Kind::Gap;
        sp.start = static_cast<double>(von) / n;
        sp.end   = static_cast<double>(bis) / n;
        v.spans.push_back(sp);
    };

    size_t pos = 0;
    for (const Bereich& b : bereiche) {
        // Überlappende Sektoren (kaputte Spur) werden nicht doppelt gezeichnet —
        // der Abschnitt beginnt dann erst dort, wo der vorige aufhörte.
        const size_t von = std::max(b.von, pos);
        const size_t bis = std::max(b.bis, von);
        if (bis <= pos) continue;
        gap(pos, von);

        TrackSpan sp = b.span;
        sp.start = static_cast<double>(von) / n;
        sp.end   = static_cast<double>(std::min(bis, track.bytes.size())) / n;
        v.spans.push_back(sp);

        // Läuft der Sektor über den Index hinaus, gehört sein Rest an den ANFANG der
        // Runde — als zweites Teilstück mit derselben Nummer (s. TrackView-Doku).
        if (bis > track.bytes.size()) {
            TrackSpan naht = b.span;
            naht.start = 0.0;
            naht.end   = static_cast<double>(bis - track.bytes.size()) / n;
            v.spans.insert(v.spans.begin(), naht);
        }
        pos = std::min(bis, track.bytes.size());
    }
    gap(pos, track.bytes.size());

    // Die Naht vorn kann den ersten Gap überdecken — die Liste bleibt sortiert und
    // überschneidungsfrei, indem das erste Teilstück den folgenden Gap kürzt.
    for (size_t i = 1; i < v.spans.size(); ++i)
        if (v.spans[i].start < v.spans[i - 1].end)
            v.spans[i].start = v.spans[i - 1].end;
    v.spans.erase(std::remove_if(v.spans.begin(), v.spans.end(),
                                 [](const TrackSpan& s) { return s.end <= s.start; }),
                  v.spans.end());
    return v;
}
