/**
 * @file geometry_probe.cpp
 * @brief Umsetzung von @ref GeometryProbe.
 *
 * @see doc/design/13_k1520disktool.md §12.1
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/geometry_probe.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace {

const char* encName(Encoding e) { return e == Encoding::FM ? "fm" : "mfm"; }

std::string tp(uint8_t cyl, uint8_t head) {
    return "c" + std::to_string(cyl) + "h" + std::to_string(head);
}

}  // namespace

std::string GeometryMatch::remarks() const {
    std::string s;
    auto add = [&](const std::string& t) { if (!s.empty()) s += ", "; s += t; };
    if (stray_tracks)  add(std::to_string(stray_tracks)
                           + " beschriebene Spuren hinter dem Format (Altbestand)");
    if (gap_tracks)    add(std::to_string(gap_tracks) + " Luecken zwischen den Spuren");
    if (defect_tracks) add(std::to_string(defect_tracks)
                           + (defect_tracks == 1 ? " Spur mit fehlenden Sektoren"
                                                 : " Spuren mit fehlenden Sektoren"));
    if (empty_tracks)  add(std::to_string(empty_tracks) + " unformatierte Spuren");
    if (crc_errors)    add(std::to_string(crc_errors) + " Sektoren mit CRC-Fehler");
    return s;
}

namespace GeometryProbe {

// ─── messen ──────────────────────────────────────────────────────────────────

namespace {

/// Eine einzelne Spur vermessen — die gemeinsame Mitte beider Messwege.
MeasuredTrack messeSpur(const DiskMedium& medium, uint8_t c, uint8_t h) {
    MeasuredTrack mt;
    mt.cyl  = c;
    mt.head = h;

    const TrackImage& t = medium.track(c, h);
    mt.encoding = t.encoding;

    const std::vector<LogicalSector> secs = TrackCodec::parseTrack(t);
    if (secs.empty()) return mt;

    mt.formatted = true;
    mt.sectors   = static_cast<uint8_t>(std::min<size_t>(secs.size(), 255));

    // Einheitliche Sektorgroesse? (0 = uneinheitlich → passt zu keinem Format)
    mt.sector_size = secs.front().size;
    for (const auto& s : secs)
        if (s.size != mt.sector_size) { mt.sector_size = 0; break; }

    uint8_t lo = 0xFF, hi = 0;
    for (const auto& s : secs) {
        lo = std::min(lo, s.id);
        hi = std::max(hi, s.id);
        if (!s.id_crc_ok || !s.data_crc_ok) ++mt.crc_errors;
    }
    mt.first_id  = lo;
    mt.id_cyl    = secs.front().cyl;
    mt.ids_dense = (static_cast<int>(hi) - static_cast<int>(lo) + 1
                    == static_cast<int>(secs.size()));
    return mt;
}

}  // namespace

std::vector<std::pair<uint8_t, uint8_t>> probeTracks(uint8_t num_cyls,
                                                     uint8_t num_heads) {
    if (num_cyls == 0 || num_heads == 0) return {};
    const uint8_t letzter = static_cast<uint8_t>(num_cyls - 1);
    auto begrenzt = [&](int x) {
        return static_cast<uint8_t>(std::clamp(x, 0, static_cast<int>(letzter)));
    };

    // Kopf 0 traegt die Arbeit: Systemspuren 0–3 (3 ist die CP/A-Sonde), die Mitte,
    // und zwei Spuren am inneren Rand fuer „40 oder 80".
    std::vector<uint8_t> c = {0, 1, 2, 3,
                              begrenzt(num_cyls / 2),
                              begrenzt(letzter - 2),
                              begrenzt(letzter - 1)};
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());

    std::vector<std::pair<uint8_t, uint8_t>> out;
    out.reserve(c.size() + 1);
    for (uint8_t x : c) out.emplace_back(x, 0);
    // Auf Kopf 1 genuegt EINE Sonde — sie sagt ein- oder zweiseitig, mehr ist dort
    // nicht zu holen.  Sie zu verdoppeln kostete am echten Laufwerk 4 s je Zylinder.
    if (num_heads > 1) out.emplace_back(0, 1);
    return out;
}

std::vector<MeasuredTrack> measureSample(const DiskMedium& medium) {
    const uint8_t ncyl  = medium.numCylinders();
    const uint8_t nhead = medium.numHeads();
    if (ncyl == 0 || nhead == 0) return {};

    std::map<std::pair<uint8_t, uint8_t>, MeasuredTrack> gemessen;
    auto hole = [&](uint8_t c, uint8_t h) -> const MeasuredTrack& {
        auto it = gemessen.find({c, h});
        if (it == gemessen.end())
            it = gemessen.emplace(std::pair<uint8_t, uint8_t>{c, h},
                                  messeSpur(medium, c, h)).first;
        return it->second;
    };

    // 1. Systemspuren.  Zylinder 3 ist die Sonde des CP/A-BIOS (`dlgint`) und trennt
    //    die meisten Formate; 0–2 tragen bei gemischter Geometrie (cpa780) die
    //    128-B-Spuren, die sonst niemand sieht.
    for (uint8_t c = 0; c < 4 && c < ncyl; ++c) hole(c, 0);
    if (nhead > 1) hole(0, 1);

    // 2. Die AUSDEHNUNG suchen — der entscheidende Schritt.
    //
    //    Ohne sie urteilt die Stichprobe ueber eine Diskette, deren Ende sie nicht
    //    kennt: sondiert sie fest bei 77/78 und sind die unformatiert (Altbestand
    //    einer kuerzeren Formatierung), gilt die Diskette als 41 Zylinder lang, und
    //    `udos_ss77` faellt mit „deklariert 77, beschrieben 41" durch — die
    //    Stichprobe nahm dann `k5601_ss40_26x128`.  Die Suche kostet ~log2(n) Spuren.
    //
    //    „Beschrieben" wird dabei ueber ZWEI benachbarte Zylinder geprueft: bei einer
    //    Doppelschritt-Diskette ist jeder zweite leer, und eine Suche, die einen
    //    ungeraden Zylinder als Ende naehme, halbierte die Diskette.
    auto beschrieben = [&](uint8_t c) {
        if (hole(c, 0).formatted) return true;
        if (c + 1 < ncyl && hole(static_cast<uint8_t>(c + 1), 0).formatted) return true;
        return false;
    };

    int lo = -1;                       // hoechster als beschrieben BEKANNTER Zylinder
    for (uint8_t c = 0; c < 4 && c < ncyl; ++c)
        if (hole(c, 0).formatted) lo = c;
    if (lo >= 0) {
        const uint8_t letzter = static_cast<uint8_t>(ncyl - 1);
        if (beschrieben(letzter)) {
            lo = letzter;
        } else {
            int hi = letzter;          // bekannt: dort endet es NICHT
            while (hi - lo > 1) {
                const int mitte = lo + (hi - lo) / 2;
                if (beschrieben(static_cast<uint8_t>(mitte))) lo = mitte;
                else                                          hi = mitte;
            }
        }
        // 3. Rund um das Ende genau hinsehen — dort entscheidet sich „40 oder 80",
        //    und auf Kopf 1, ob die Diskette wirklich beidseitig beschrieben ist.
        hole(static_cast<uint8_t>(lo), 0);
        if (lo > 0) hole(static_cast<uint8_t>(lo - 1), 0);
        if (nhead > 1) hole(static_cast<uint8_t>(lo), 1);
        // 4. Eine Sonde in der Mitte — findet einen Wechsel der Sektorgroesse, den
        //    weder Anfang noch Ende zeigen.
        hole(static_cast<uint8_t>(lo / 2), 0);
    }

    std::vector<MeasuredTrack> out;
    out.reserve(gemessen.size());
    for (auto& [wo, t] : gemessen) { (void)wo; out.push_back(t); }
    return out;
}

std::vector<MeasuredTrack> measureTracks(
        const DiskMedium& medium,
        const std::vector<std::pair<uint8_t, uint8_t>>& tracks) {
    std::vector<MeasuredTrack> out;
    out.reserve(tracks.size());
    for (const auto& [c, h] : tracks) {
        if (c >= medium.numCylinders() || h >= medium.numHeads()) continue;
        out.push_back(messeSpur(medium, c, h));
    }
    return out;
}

std::vector<MeasuredTrack> measure(const DiskMedium& medium) {
    std::vector<MeasuredTrack> out;
    for (uint8_t c = 0; c < medium.numCylinders(); ++c)
        for (uint8_t h = 0; h < medium.numHeads(); ++h)
            out.push_back(messeSpur(medium, c, h));
    return out;
}

int lastFormattedCylinder(const std::vector<MeasuredTrack>& tracks) {
    int last = -1;
    for (const auto& t : tracks)
        if (t.formatted) last = std::max(last, static_cast<int>(t.cyl));
    return last;
}

// ─── abgleichen ──────────────────────────────────────────────────────────────

GeometryMatch match(const std::vector<MeasuredTrack>& tracks, const DiskFormat& fmt,
                    bool stichprobe) {
    GeometryMatch m;
    m.format = &fmt;

    const int letzter = lastFormattedCylinder(tracks);
    if (letzter < 0) {
        m.reason = "Medium ist vollstaendig unformatiert (Leerdiskette)";
        return m;
    }

    // Regel 6: Kopfzahl des Mediums.  Ein zweiköpfiges Format kann auf einer
    // einseitigen Diskette nicht liegen — ohne diese Prüfung „passt" z. B.
    // k5601_ds40_26x128 auf jede einseitige 40-Spur-UDOS-Diskette.
    uint8_t medium_heads = 0;
    for (const auto& t : tracks)
        medium_heads = std::max(medium_heads, static_cast<uint8_t>(t.head + 1));
    if (fmt.numHeads() > medium_heads) {
        m.reason = "Format ist " + std::to_string(fmt.numHeads()) + "-koepfig, das Medium hat "
                 + std::to_string(medium_heads) + " Seite(n)";
        return m;
    }

    // Und die Gegenrichtung: traegt eine Seite BESCHRIEBENE Spuren, die das Format
    // gar nicht kennt, dann beschreibt es diese Diskette nicht.  Das war bisher nur
    // ein Zaehlnachteil (`empty_tracks`, der letzte Rang der Sortierung) — und damit
    // eine Frage der Menge: bei einer VOLLmessung verliert ein einseitiges Format
    // gegen 80 leere Spuren, bei einer STICHPROBE nur gegen eine.  So gewann
    // `k5601_ss80_16x256` gegen `cpa640` auf einer beidseitigen Diskette.  Als harte
    // Regel ist es von der Zahl der angesehenen Spuren unabhaengig.
    for (const auto& t : tracks) {
        if (t.formatted && t.head >= fmt.numHeads()) {
            m.reason = "Format kennt nur Seite 0, die Diskette ist auf Seite "
                     + std::to_string(t.head) + " beschrieben (Spur " + tp(t.cyl, t.head) + ")";
            return m;
        }
    }

    // Doppelschritt (`step: 2`): logische Spur n liegt auf physischem Zylinder 2n, die
    // dazwischen MUESSEN leer sein.  Das ist ein POSITIVES Kriterium — sonst passte ein
    // Doppelschritt-Format auch auf eine gewoehnliche 40-Spur-Diskette, deren Spuren
    // luecklos liegen.  Zweite, staerkere Probe: die Spurnummer im ID-Feld ist die
    // logische; auf einer Einzelschritt-Diskette stimmt sie mit dem Zylinder ueberein.
    if (fmt.step > 1) {
        int belegt = 0, luecken = 0;
        for (const auto& t : tracks) {
            if (t.cyl % fmt.step != 0) {
                if (t.formatted) {
                    m.reason = "Spur " + tp(t.cyl, t.head) + " ist beschrieben — bei "
                               "Doppelschritt muss jeder zweite Zylinder leer sein";
                    return m;
                }
                if (t.cyl < letzter) ++luecken;
                continue;
            }
            if (!t.formatted) continue;
            const int erwartet = t.cyl / fmt.step;
            // Hinter dem Format liegt Altbestand — darueber urteilt Regel 3, nicht hier.
            if (erwartet >= fmt.numCylinders()) continue;
            ++belegt;
            if (t.id_cyl != erwartet) {
                m.reason = "Spur " + tp(t.cyl, t.head) + " traegt im ID-Feld die Spurnummer "
                         + std::to_string(t.id_cyl) + ", ein Doppelschritt-Format erwartet "
                         + std::to_string(erwartet);
                return m;
            }
        }
        if (belegt == 0 || luecken == 0) {
            m.reason = "keine Doppelschritt-Luecken gefunden";
            return m;
        }
    }

    for (const auto& t : tracks) {
        const int lcyl = fmt.logicalCylinder(t.cyl);
        const TrackFormat* tf =
            lcyl < 0 ? nullptr : fmt.findTrack(static_cast<uint8_t>(lcyl), t.head);

        // Beim Doppelschritt sind die uebersprungenen Zylinder KEINE Luecke, sondern
        // Teil des Formats — oben schon geprueft, hier nur nicht mitzaehlen.
        if (lcyl < 0) continue;

        if (!t.formatted) {
            // Regel 2: leere Spuren am ENDE sind normal, Luecken MITTENDRIN nicht.
            // Spuren, die das Format ohnehin nicht kennt, sind KEINE Luecke — sonst
            // fiele jedes einseitige Format auf einem zweikoepfigen Medium durch
            // (die ganze zweite Seite ist dort unformatiert).
            if (!tf)                  ++m.empty_tracks;
            else if (t.cyl < letzter) ++m.gap_tracks;
            else                      ++m.empty_tracks;
            continue;
        }

        // Regel 3: beschriebene Spur ausserhalb des Formats = Altbestand.
        if (!tf) {
            ++m.stray_tracks;
            continue;
        }

        // Regel 1: Sektorgroesse, erste ID und Verfahren muessen stimmen.
        if (t.sector_size == 0) {
            m.reason = "Spur " + tp(t.cyl, t.head) + " hat uneinheitliche Sektorgroessen";
            return m;
        }
        if (t.sector_size != tf->bytes_per_sec) {
            m.reason = "Spur " + tp(t.cyl, t.head) + ": gemessen "
                     + std::to_string(t.sectors) + "×" + std::to_string(t.sector_size)
                     + ", Format sagt " + std::to_string(tf->secs_per_track) + "×"
                     + std::to_string(tf->bytes_per_sec);
            return m;
        }
        if (t.encoding != tf->encoding) {
            m.reason = "Spur " + tp(t.cyl, t.head) + ": Verfahren " + encName(t.encoding)
                     + ", Format sagt " + encName(tf->encoding);
            return m;
        }
        if (t.first_id != tf->first_sector_id) {
            // Eine abweichende erste ID ist normalerweise ein ANDERES Format — es sei
            // denn, die IDs der Spur sind auch untereinander lueckenhaft: dann hat der
            // Parser Gap-Bytes fuer eine Adressmarke gehalten, die Spur ist also
            // beschaedigt.  Das ist ein Schaden wie „zu wenige Sektoren" (Regel 4) und
            // darf die ganze Diskette nicht unlesbar machen.
            if (t.ids_dense) {
                m.reason = "Spur " + tp(t.cyl, t.head) + ": erste Sektor-ID "
                         + std::to_string(t.first_id) + ", Format sagt "
                         + std::to_string(tf->first_sector_id);
                return m;
            }
            ++m.defect_tracks;
            continue;
        }

        // Regel 4: zu wenige Sektoren = Schaden, zu viele = anderes Format.
        if (t.sectors > tf->secs_per_track) {
            m.reason = "Spur " + tp(t.cyl, t.head) + ": " + std::to_string(t.sectors)
                     + " Sektoren, das Format erlaubt nur "
                     + std::to_string(tf->secs_per_track);
            return m;
        }
        if (t.sectors < tf->secs_per_track) ++m.defect_tracks;

        m.crc_errors = static_cast<uint16_t>(
            std::min<int>(0xFFFF, m.crc_errors + t.crc_errors));
    }

    // Regel 4b: EIN paar beschaedigte Spuren sind ein Schaden — die halbe Diskette
    // nicht.  Ohne diese Schranke passt jedes Format auf jede Diskette mit WENIGER
    // Sektoren je Spur (7×512 galt als „k5601_ss40_9x512 mit 40 defekten Spuren").
    int beschrieben = 0;
    for (const auto& t : tracks)
        if (t.formatted && fmt.logicalCylinder(t.cyl) >= 0) ++beschrieben;
    // ... aber NUR ueber eine Vollmessung.  Ein Verhaeltnis ueber acht angesehene
    // Spuren ist keine Aussage ueber die Diskette: bei `udos_ss77` liegen 2 von 7
    // Sondenspuren ausserhalb (die Altbestandsspuren 77/78) — 28 %, also „anderes
    // Format", waehrend es ueber die ganze Diskette 3 von 80 sind (3,75 %).  So
    // verwarf die Stichprobe das richtige Format und nahm ein zu kleines.
    if (!stichprobe && m.defect_tracks > 2 && m.defect_tracks * 4 > beschrieben) {
        m.reason = std::to_string(m.defect_tracks) + " von " + std::to_string(beschrieben)
                 + " beschriebenen Spuren weichen ab — das ist kein Schaden, sondern ein "
                   "anderes Format";
        return m;
    }

    // Luecken zwischen beschriebenen Spuren: eine Doppelschritt-Diskette.  Der
    // Katalog kann sie nicht ausdruecken (Spurbereiche sind zusammenhaengend), und
    // ein „passendes" Format waere schlicht falsch — also ablehnen und es sagen.
    if (m.gap_tracks > 2) {
        m.reason = std::to_string(m.gap_tracks) + " unformatierte Spuren ZWISCHEN "
                   "beschriebenen — sieht nach Doppelschritt aus (nur jeder zweite "
                   "Zylinder beschrieben)";
        return m;
    }

    // Ein Format, das deutlich mehr Zylinder deklariert als beschrieben sind, ist nicht
    // gemeint — sonst „passt" ein 80-Spur-Format auch auf eine 40-Spur-Diskette.
    // Altbestand ist nur glaubwuerdig, solange es WENIGE Spuren sind — und diese
    // Schranke gilt immer, nicht nur bei zu kleinem Zylinderbereich.  Sonst passt
    // ein einseitiges Format auf eine beidseitig beschriebene Diskette: die ganze
    // zweite Seite gilt dann als „Altbestand" (cpa390 auf einer cpa780-Diskette).
    if (m.stray_tracks > 8) {
        m.reason = std::to_string(m.stray_tracks) + " beschriebene Spuren liegen "
                   "ausserhalb des Formats — das ist zu viel fuer Altbestand";
        return m;
    }

    // PHYSISCHE Ausdehnung: ein 40-Spur-Doppelschritt-Format belegt 79 Zylinder.  Ohne
    // das passte es auch auf eine gewoehnliche 40-Spur-Diskette (halb so breit).
    const int deklariert = static_cast<int>(fmt.physicalCylinders());
    if (deklariert < letzter + 1) {
        // hier bereits ueber stray_tracks abgesichert
    } else {
        m.slack_cyls = static_cast<uint16_t>(deklariert - (letzter + 1));
        if (m.slack_cyls > 3) {
            m.reason = "Format deklariert " + std::to_string(deklariert)
                     + " Zylinder, beschrieben sind nur " + std::to_string(letzter + 1);
            return m;
        }
    }

    m.ok = true;
    return m;
}

std::vector<GeometryMatch> matchAll(const std::vector<MeasuredTrack>& tracks,
                                    const std::vector<DiskFormat>& formats,
                                    bool stichprobe) {
    std::vector<GeometryMatch> treffer;
    for (const DiskFormat& f : formats) {
        GeometryMatch m = match(tracks, f, stichprobe);
        if (m.ok) treffer.push_back(m);
    }
    std::stable_sort(treffer.begin(), treffer.end(),
                     [](const GeometryMatch& a, const GeometryMatch& b) {
                         if (a.gap_tracks   != b.gap_tracks)   return a.gap_tracks   < b.gap_tracks;
                         if (a.stray_tracks != b.stray_tracks) return a.stray_tracks < b.stray_tracks;
                         if (a.slack_cyls   != b.slack_cyls)   return a.slack_cyls   < b.slack_cyls;
                         if (a.defect_tracks!= b.defect_tracks)return a.defect_tracks< b.defect_tracks;
                         return a.empty_tracks < b.empty_tracks;
                     });
    return treffer;
}

// ─── beschreiben ─────────────────────────────────────────────────────────────

std::string describe(const std::vector<MeasuredTrack>& tracks) {
    if (tracks.empty()) return "  (kein Medium)";

    uint8_t ncyls = 0, nheads = 0;
    int crc = 0;
    for (const auto& t : tracks) {
        ncyls  = std::max(ncyls,  static_cast<uint8_t>(t.cyl + 1));
        nheads = std::max(nheads, static_cast<uint8_t>(t.head + 1));
        crc   += t.crc_errors;
    }

    std::ostringstream os;
    os << "  Zylinder 0-" << int(ncyls - 1) << ", Koepfe 0-" << int(nheads - 1) << "\n";

    // Gleichartige aufeinanderfolgende Spuren zu Bereichen zusammenfassen — dieselbe
    // Darstellung wie ein `tracks:`-Eintrag in formats.yaml.
    auto gleich = [](const MeasuredTrack& a, const MeasuredTrack& b) {
        return a.formatted == b.formatted && a.sectors == b.sectors
            && a.sector_size == b.sector_size && a.first_id == b.first_id
            && a.encoding == b.encoding;
    };

    size_t i = 0;
    while (i < tracks.size()) {
        size_t j = i;
        while (j + 1 < tracks.size() && gleich(tracks[i], tracks[j + 1])) ++j;

        os << "  " << tp(tracks[i].cyl, tracks[i].head);
        if (j != i) os << ".." << tp(tracks[j].cyl, tracks[j].head);
        if (!tracks[i].formatted) {
            os << " : unformatiert";
        } else {
            os << " : " << int(tracks[i].sectors) << " Sektoren à "
               << tracks[i].sector_size << " B, IDs " << int(tracks[i].first_id) << "-"
               << int(tracks[i].first_id + tracks[i].sectors - 1) << ", "
               << encName(tracks[i].encoding);
            if (!tracks[i].ids_dense) os << " (IDs mit Luecken)";
        }
        os << "\n";
        i = j + 1;
    }
    if (crc > 0) os << "  " << crc << " Sektoren mit CRC-Fehler\n";
    return os.str();
}

// ─── aus der Messung ein Format bauen ────────────────────────────────────────

std::optional<DiskFormat> synthesize(const std::vector<MeasuredTrack>& tracks,
                                     std::string* why) {
    auto nein = [&](const std::string& t) -> std::optional<DiskFormat> {
        if (why) *why = t;
        return std::nullopt;
    };

    uint8_t nheads = 0, ncyls = 0;
    for (const auto& t : tracks) {
        nheads = std::max(nheads, static_cast<uint8_t>(t.head + 1));
        ncyls  = std::max(ncyls,  static_cast<uint8_t>(t.cyl + 1));
        if (t.formatted && t.sector_size == 0)
            return nein("Spur " + tp(t.cyl, t.head) + " hat uneinheitliche Sektorgroessen");
    }
    const int letzter = lastFormattedCylinder(tracks);
    if (letzter < 0 || nheads == 0) return nein("Medium ist vollstaendig unformatiert");

    // Gestalt einer Spur — der Vergleichsschluessel fuer das Zusammenfassen.
    struct Gestalt {
        bool     leer = true;
        uint8_t  sectors = 0;
        uint16_t size = 0;
        uint8_t  first_id = 1;
        Encoding enc = Encoding::MFM;
        bool operator==(const Gestalt& o) const {
            if (leer || o.leer) return leer == o.leer;
            return sectors == o.sectors && size == o.size
                && first_id == o.first_id && enc == o.enc;
        }
    };

    std::vector<std::vector<Gestalt>> zylinder(
        static_cast<size_t>(letzter) + 1, std::vector<Gestalt>(nheads));
    for (const auto& t : tracks) {
        if (t.cyl > letzter) continue;                   // Altbestand dahinter
        Gestalt& g = zylinder[t.cyl][t.head];
        g.leer = !t.formatted;
        if (t.formatted) {
            g.sectors  = t.sectors;
            g.size     = t.sector_size;
            g.first_id = t.first_id;
            g.enc      = t.encoding;
        }
    }

    auto zylinderLeer = [&](int c) {
        for (const Gestalt& g : zylinder[static_cast<size_t>(c)])
            if (!g.leer) return false;
        return true;
    };

    // Doppelschritt: KEIN ungerader Zylinder beschrieben, aber Luecken dazwischen.
    uint8_t step = 1;
    if (letzter >= 2) {
        bool ungerade_leer = true, luecke = false;
        for (int c = 1; c <= letzter; c += 2) {
            if (!zylinderLeer(c)) ungerade_leer = false; else luecke = true;
        }
        if (ungerade_leer && luecke && (letzter % 2) == 0) step = 2;
    }

    // Loecher, die kein Doppelschritt sind, machen den linearen Raum unbrauchbar.
    for (int c = 0; c <= letzter; c += step)
        if (zylinderLeer(c))
            return nein("Zylinder " + std::to_string(c) + " ist unformatiert, die "
                        "Spuren davor und dahinter nicht — ein solcher Datentraeger "
                        "laesst sich nicht als zusammenhaengendes Dateisystem lesen");

    DiskFormat fmt;
    fmt.name        = "(gemessen)";
    fmt.description = "aus dem Abbild vermessen, kein Katalogeintrag";
    fmt.step        = step;

    // Zylinder mit gleichem Kopf-Muster zusammenfassen, darin die Koepfe — so
    // entstehen echte Rechtecke auch bei gemischter Geometrie.
    for (int c = 0; c <= letzter; c += step) {
        int bis = c;
        while (bis + step <= letzter
               && zylinder[static_cast<size_t>(bis + step)] == zylinder[static_cast<size_t>(c)])
            bis += step;

        const auto& muster = zylinder[static_cast<size_t>(c)];
        for (uint8_t h = 0; h < nheads; ++h) {
            if (muster[h].leer) continue;
            uint8_t h_bis = h;
            while (h_bis + 1 < nheads && muster[h_bis + 1] == muster[h]) ++h_bis;

            TrackFormat tf;
            tf.cyl_first       = static_cast<uint8_t>(c / step);
            tf.cyl_last        = static_cast<uint8_t>(bis / step);
            tf.head_first      = h;
            tf.head_last       = h_bis;
            tf.secs_per_track  = muster[h].sectors;
            tf.bytes_per_sec   = muster[h].size;
            tf.first_sector_id = muster[h].first_id;
            tf.encoding        = muster[h].enc;
            fmt.tracks.push_back(tf);

            h = h_bis;
        }
        c = bis;
    }

    if (fmt.tracks.empty()) return nein("keine beschriebene Spur gefunden");
    return fmt;
}

}  // namespace GeometryProbe
