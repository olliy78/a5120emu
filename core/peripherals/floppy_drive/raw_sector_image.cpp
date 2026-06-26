/**
 * @file raw_sector_image.cpp
 * @brief Implementierung von RawSectorImage.
 *
 * Liest und schreibt .img-Dateien (reine Sektor-Nutzdaten) mithilfe eines
 * DiskFormat.  readTrack() synthetisiert aus den Rohdaten via TrackCodec::buildTrack
 * ein vollständiges TrackImage; writeTrack() parst das Image zurück und speichert
 * jeden Sektor an seinen Offset.
 *
 * Das Offset-/Interleave-Modell (Spuren verschränkt: cyl0/A, cyl0/B, cyl1/A, …)
 * entspricht dem ursprünglichen A5120-Diskettenlayout (vgl. doc/design/09_floppy_drive.md).
 *
 * @see core/peripherals/floppy_drive/raw_sector_image.h
 * @see doc/design/09_floppy_drive.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <fstream>

// ─── Konstruktor ─────────────────────────────────────────────────────────────

RawSectorImage::RawSectorImage(const std::string& path, DiskFormat fmt,
                               bool write_protect, Encoding enc)
    : path_(path)
    , fmt_(std::move(fmt))
    , write_protect_(write_protect)
    , enc_(enc)
{
    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        last_error_ = "Kann Datei nicht öffnen: " + path_;
        is_open_ = false;
    } else {
        is_open_ = true;
    }
}

// ─── Offset-Berechnung ───────────────────────────────────────────────────────

/**
 * Byte-Offset von Sektor (cyl, head, sector_id) in der .img-Datei.
 *
 * Spuren sind verschränkt: (cyl0,head0), (cyl0,head1), (cyl1,head0), …
 * sector_id ist 1-basiert.  Gibt -1 bei ungültigem cyl/head/id zurück.
 *
 * Verschränktes A5120-Diskettenlayout (vgl. doc/design/09_floppy_drive.md §2).
 */
int64_t RawSectorImage::sectorOffset(uint8_t cyl, uint8_t head,
                                     uint8_t sector_id) const {
    const TrackFormat* tf = fmt_.findTrack(cyl, head);
    if (!tf) return -1;
    if (sector_id < 1 || sector_id > tf->secs_per_track) return -1;

    // Alle Spur-Bytes vor (cyl, head) in Layout-Reihenfolge aufsummieren.
    uint64_t offset = 0;
    const uint8_t ncyls  = fmt_.numCylinders();
    const uint8_t nheads = fmt_.numHeads();

    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            if (c == cyl && h == head) {
                offset += static_cast<uint64_t>(sector_id - 1) * tf->bytes_per_sec;
                return static_cast<int64_t>(offset);
            }
            const TrackFormat* t = fmt_.findTrack(c, h);
            if (t) offset += t->trackBytes();
        }
    }
    return -1;
}

// ─── DiskImage-Schnittstelle ─────────────────────────────────────────────────

DiskGeometry RawSectorImage::geometry() const {
    DiskGeometry g;
    g.num_cyls  = fmt_.numCylinders();
    g.num_heads = fmt_.numHeads();
    g.encoding  = enc_;
    g.uniform   = (fmt_.tracks.size() == 1);
    return g;
}

TrackImage RawSectorImage::readTrack(uint8_t cyl, uint8_t head) {
    const TrackFormat* tf = fmt_.findTrack(cyl, head);
    if (!tf) return {};   // Spur existiert nicht → leer

    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        last_error_ = "Lesefehler: " + path_;
        return {};
    }

    // Alle Sektoren der Spur lesen und als LogicalSector sammeln.
    std::vector<LogicalSector> sektoren;
    sektoren.reserve(tf->secs_per_track);

    for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
        int64_t off = sectorOffset(cyl, head, id);

        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = head;
        ls.id   = id;
        ls.size = tf->bytes_per_sec;
        ls.data.assign(tf->bytes_per_sec, 0xFF);   // Füller bei fehlendem Inhalt

        if (off >= 0) {
            f.seekg(off);
            f.read(reinterpret_cast<char*>(ls.data.data()), tf->bytes_per_sec);
            // Fehlende Bytes bleiben 0xFF (initialisierter Füller).
        }

        sektoren.push_back(std::move(ls));
    }

    return TrackCodec::buildTrack(sektoren, enc_);
}

bool RawSectorImage::writeTrack(uint8_t cyl, uint8_t head,
                                const TrackImage& track) {
    if (write_protect_) {
        last_error_ = "Schreibschutz aktiv";
        return false;
    }

    // Spur parsen → LogicalSectors zurückgewinnen.
    auto sektoren = TrackCodec::parseTrack(track);

    std::fstream f(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {
        last_error_ = "Schreibfehler: " + path_;
        return false;
    }

    for (const auto& ls : sektoren) {
        // Nur Sektoren schreiben, deren ID gültig ist.
        if (ls.id == 0) continue;

        int64_t off = sectorOffset(cyl, head, ls.id);
        if (off < 0) continue;   // Ungültiger Offset → überspringen

        f.seekp(off);
        f.write(reinterpret_cast<const char*>(ls.data.data()),
                static_cast<std::streamsize>(ls.data.size()));
    }

    return f.good();
}
