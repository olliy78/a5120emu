/**
 * @file img_codec.cpp
 * @brief Implementierung von ImgCodec (rohes Sektorimage `.img`).
 *
 * @see core/peripherals/floppy_drive/img_codec.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/img_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <fstream>
#include <vector>

namespace {

/// @brief Ganze Datei in einen Puffer lesen.
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

// ─── Offset-Modell ───────────────────────────────────────────────────────────

int64_t ImgCodec::sectorOffset(const DiskFormat& fmt, uint8_t cyl, uint8_t head,
                               uint8_t sector_id) {
    const TrackFormat* tf = fmt.findTrack(cyl, head);
    if (!tf) return -1;

    const int first = tf->first_sector_id;
    if (sector_id < first || sector_id >= first + tf->secs_per_track) return -1;

    // Alle Spur-Bytes vor (cyl, head) in Layout-Reihenfolge aufsummieren.
    uint64_t offset = 0;
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();

    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            if (c == cyl && h == head) {
                offset += static_cast<uint64_t>(sector_id - first) * tf->bytes_per_sec;
                return static_cast<int64_t>(offset);
            }
            const TrackFormat* t = fmt.findTrack(c, h);
            if (t) offset += t->trackBytes();
        }
    }
    return -1;
}

// ─── Laden ───────────────────────────────────────────────────────────────────

bool ImgCodec::load(const std::string& path, const DiskFormat& fmt,
                    DiskMedium& out, std::string& err) {
    std::vector<uint8_t> datei;
    if (!leseDatei(path, datei, err)) return false;

    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    if (ncyls == 0 || nheads == 0) {
        err = "Format '" + fmt.name + "' hat keine Spurbereiche";
        return false;
    }

    // Beim Doppelschritt braucht das Medium die PHYSISCHE Ausdehnung; die
    // uebersprungenen Zylinder bleiben unformatiert — genau so, wie ein
    // 96-tpi-Laufwerk die Diskette beschreibt.
    out = DiskMedium(fmt.physicalCylinders(), nheads, fmt.predominantEncoding());

    for (uint8_t c = 0; c < ncyls; ++c) {
        const uint8_t pc = fmt.physicalCylinder(c);
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;   // Spur existiert im Format nicht → bleibt unformatiert

            std::vector<LogicalSector> sektoren;
            sektoren.reserve(tf->secs_per_track);
            for (uint8_t i = 0; i < tf->secs_per_track; ++i) {
                const uint8_t id  = static_cast<uint8_t>(tf->first_sector_id + i);
                const int64_t off = sectorOffset(fmt, c, h, id);

                LogicalSector ls;
                ls.cyl  = c;
                ls.head = h;
                ls.id   = id;
                ls.size = tf->bytes_per_sec;
                // Fuellbyte fuer fehlende Datei-Bytes (Image kuerzer als die Geometrie).
                ls.data.assign(tf->bytes_per_sec, 0xFF);
                if (off >= 0) {
                    for (uint16_t b = 0; b < tf->bytes_per_sec; ++b) {
                        const size_t src = static_cast<size_t>(off) + b;
                        if (src >= datei.size()) break;
                        ls.data[b] = datei[src];
                    }
                }
                sektoren.push_back(std::move(ls));
            }

            // Verfahren kommt aus dem SPURBEREICH (Mischdichte: FM-Systemspur +
            // MFM-Datenspuren) — nicht aus dem Gesamtformat.
            out.setTrack(pc, h, TrackCodec::buildTrack(sektoren, tf->encoding));
        }
    }

    out.clearDirty();
    return true;
}

// ─── Geometrie-Abgleich ──────────────────────────────────────────────────────

std::string ImgCodec::mismatchReason(const DiskFormat& fmt, const DiskMedium& in) {
    const uint8_t ncyls  = fmt.physicalCylinders();
    const uint8_t nheads = fmt.numHeads();
    if (in.numCylinders() > ncyls) {
        // Ueberzaehlige Spuren sind nur zulaessig, solange sie unformatiert sind.
        for (uint8_t c = ncyls; c < in.numCylinders(); ++c)
            for (uint8_t h = 0; h < in.numHeads(); ++h)
                if (!in.track(c, h).empty())
                    return "Format '" + fmt.name + "' hat nur "
                           + std::to_string(ncyls) + " Spuren, Diskette hat Daten auf Spur "
                           + std::to_string(c);
    }
    if (in.numHeads() > nheads)
        return "Format '" + fmt.name + "' ist "
               + (nheads == 1 ? std::string("einseitig") : std::to_string(nheads) + "-koepfig")
               + ", Diskette hat " + std::to_string(in.numHeads()) + " Seiten";

    for (uint8_t c = 0; c < ncyls && c < in.numCylinders(); ++c) {
        const int lc = fmt.logicalCylinder(c);
        for (uint8_t h = 0; h < nheads && h < in.numHeads(); ++h) {
            const TrackImage& t = in.track(c, h);
            if (t.empty()) continue;
            const TrackFormat* tf =
                lc < 0 ? nullptr : fmt.findTrack(static_cast<uint8_t>(lc), h);
            if (!tf)
                return "Format '" + fmt.name + "' kennt Spur " + std::to_string(c)
                       + "/" + std::to_string(h) + " nicht";

            const auto sektoren = TrackCodec::parseTrack(t);
            if (sektoren.size() != tf->secs_per_track)
                return "Spur " + std::to_string(c) + "/" + std::to_string(h) + ": "
                       + std::to_string(sektoren.size()) + " Sektoren, Format '"
                       + fmt.name + "' erwartet " + std::to_string(tf->secs_per_track);
            for (const LogicalSector& s : sektoren) {
                if (s.size != tf->bytes_per_sec)
                    return "Spur " + std::to_string(c) + "/" + std::to_string(h)
                           + ": Sektor " + std::to_string(s.id) + " hat "
                           + std::to_string(s.size) + " B, Format '" + fmt.name
                           + "' erwartet " + std::to_string(tf->bytes_per_sec);
                if (s.id < tf->first_sector_id
                    || s.id >= tf->first_sector_id + tf->secs_per_track)
                    return "Spur " + std::to_string(c) + "/" + std::to_string(h)
                           + ": Sektor-ID " + std::to_string(s.id)
                           + " liegt ausserhalb des Formats '" + fmt.name + "'";
            }
        }
    }
    return "";
}

// ─── Speichern ───────────────────────────────────────────────────────────────

bool ImgCodec::save(const std::string& path, const DiskFormat& fmt,
                    const DiskMedium& in, std::string& err) {
    const std::string grund = in.rawIncompatibleReason();
    if (!grund.empty()) {
        err = "Diskette laesst sich nicht als .img speichern (" + grund
              + ") — Gap-Anhaenge/unformatierte Spuren gehen dabei verloren. "
                "Bitte .hfe oder .dmk waehlen.";
        return false;
    }

    // Der Geometrie-Abgleich (mismatchReason) laeuft BEWUSST nicht hier, sondern in
    // DiskImage::saveAs — also dort, wo der Bediener das Format selbst waehlt.  Beim
    // Autosave in eine bereits gebundene .img gilt weiter das alte, tolerante
    // Verhalten: Sektoren, die das Format nicht kennt, werden uebersprungen (ein
    // laufender FORMAT-Vorgang darf die Datei nicht zerreissen).
    const uint64_t total = fmt.totalBytes();
    if (total == 0) { err = "Format '" + fmt.name + "' hat Groesse 0"; return false; }

    // Vollstaendigen Puffer aufbauen (Fuellbyte 0xE5 = leerer CP/M-Sektor) und in
    // einem Rutsch schreiben — die Datei wird immer komplett neu erzeugt.
    std::vector<uint8_t> datei(static_cast<size_t>(total), 0xE5);

    for (uint8_t c = 0; c < fmt.numCylinders(); ++c) {
        const uint8_t pc = fmt.physicalCylinder(c);
        if (pc >= in.numCylinders()) break;
        for (uint8_t h = 0; h < fmt.numHeads(); ++h) {
            const TrackImage& t = in.track(pc, h);
            if (t.empty()) continue;
            for (const LogicalSector& s : TrackCodec::parseTrack(t)) {
                const int64_t off = sectorOffset(fmt, c, h, s.id);
                if (off < 0) continue;
                for (size_t b = 0; b < s.data.size(); ++b) {
                    const size_t dst = static_cast<size_t>(off) + b;
                    if (dst >= datei.size()) break;
                    datei[dst] = s.data[b];
                }
            }
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { err = "Kann Datei nicht schreiben: " + path; return false; }
    f.write(reinterpret_cast<const char*>(datei.data()),
            static_cast<std::streamsize>(datei.size()));
    if (!f) { err = "Schreibfehler (unvollstaendig): " + path; return false; }
    return true;
}
