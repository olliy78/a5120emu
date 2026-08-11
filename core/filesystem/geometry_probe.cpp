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

std::vector<MeasuredTrack> measure(const DiskMedium& medium) {
    std::vector<MeasuredTrack> out;
    for (uint8_t c = 0; c < medium.numCylinders(); ++c) {
        for (uint8_t h = 0; h < medium.numHeads(); ++h) {
            MeasuredTrack mt;
            mt.cyl  = c;
            mt.head = h;

            const TrackImage& t = medium.track(c, h);
            mt.encoding = t.encoding;

            const std::vector<LogicalSector> secs = TrackCodec::parseTrack(t);
            if (secs.empty()) { out.push_back(mt); continue; }

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

            out.push_back(mt);
        }
    }
    return out;
}

int lastFormattedCylinder(const std::vector<MeasuredTrack>& tracks) {
    int last = -1;
    for (const auto& t : tracks)
        if (t.formatted) last = std::max(last, static_cast<int>(t.cyl));
    return last;
}

// ─── abgleichen ──────────────────────────────────────────────────────────────

GeometryMatch match(const std::vector<MeasuredTrack>& tracks, const DiskFormat& fmt) {
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
                                    const std::vector<DiskFormat>& formats) {
    std::vector<GeometryMatch> treffer;
    for (const DiskFormat& f : formats) {
        GeometryMatch m = match(tracks, f);
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

}  // namespace GeometryProbe
