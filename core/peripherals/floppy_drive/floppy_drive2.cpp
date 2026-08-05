/**
 * @file floppy_drive2.cpp
 * @brief Implementierung von FloppyDriveV2 (Profil + Kopfposition + gemountete Diskette).
 *
 * @see core/peripherals/floppy_drive/floppy_drive2.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/floppy_drive2.h"

namespace {
const TrackImage kLeer{};
TrackImage       g_dummy{};
}  // namespace

// ─── Konstruktor ─────────────────────────────────────────────────────────────

FloppyDriveV2::FloppyDriveV2(DriveProfile profile)
    : profile_(std::move(profile))
{}

// ─── Mount / Unmount ─────────────────────────────────────────────────────────

bool FloppyDriveV2::mount(std::unique_ptr<DiskImage> img, bool write_protect) {
    if (!img) {
        last_error_ = "kein Image";
        return false;
    }

    const DiskGeometry geo = img->geometry();

    // Geometrie gegen das physische Laufwerk pruefen.
    // Ueberzaehlige Zylinder jenseits von profile_.num_cyls sind zulaessig, SOLANGE sie
    // unformatiert sind: ein physisches Laufwerk erreicht sie nicht (step() clampt auf
    // num_cyls-1), sie werden also nie gelesen.  Viele HFE-Images haengen 1–3 leere
    // Gap-Spuren an.  Traegt ein solcher Zylinder dagegen echte Marken, ist es ein
    // echter Kapazitaets-Konflikt → ablehnen.
    if (geo.num_cyls > profile_.num_cyls) {
        for (uint8_t c = profile_.num_cyls; c < geo.num_cyls; ++c) {
            for (uint8_t h = 0; h < geo.num_heads; ++h) {
                for (MarkType m : img->readTrack(c, h).marks) {
                    if (m == MarkType::Id || m == MarkType::Data) {
                        last_error_ = "zu viele Spuren für Laufwerk";
                        return false;
                    }
                }
            }
        }
    }
    if (geo.num_heads > profile_.num_heads) {
        last_error_ = "zu viele Köpfe";
        return false;
    }
    // Verfahren nur pruefen, wenn die Diskette ueberhaupt formatiert ist — eine
    // LEERDISKETTE traegt nur einen Vorschlagswert und passt in jedes Laufwerk.
    if (img->medium().formatted() && !profile_.supports(geo.encoding)) {
        last_error_ = "Verfahren vom Laufwerk nicht unterstützt";
        return false;
    }

    image_ = std::move(img);
    image_->setWriteProtect(write_protect);
    write_protect_ = write_protect;
    cur_cyl_       = 0;
    return true;
}

void FloppyDriveV2::unmount() {
    flush();
    image_.reset();
    cur_cyl_ = 0;
}

void FloppyDriveV2::setWriteProtect(bool wp) {
    write_protect_ = wp;
    if (image_) image_->setWriteProtect(wp);
}

// ─── Spurwahl ─────────────────────────────────────────────────────────────────

bool FloppyDriveV2::step(bool inward) {
    if (!image_) return false;
    if (inward) {
        if (cur_cyl_ < profile_.num_cyls - 1) ++cur_cyl_;
    } else {
        if (cur_cyl_ > 0) --cur_cyl_;
    }
    return true;
}

bool FloppyDriveV2::seek(uint8_t cyl) {
    if (!image_) return false;
    cur_cyl_ = (cyl < profile_.num_cyls) ? cyl
                                         : static_cast<uint8_t>(profile_.num_cyls - 1);
    return true;
}

// ─── Spurzugriff (direkt auf dem Medium) ──────────────────────────────────────

const TrackImage& FloppyDriveV2::track(uint8_t head) const {
    if (head > 1 || !image_) return kLeer;
    return image_->readTrack(cur_cyl_, head);
}

void FloppyDriveV2::markTrackDirty(uint8_t head) {
    if (head > 1 || !image_) return;
    image_->medium().markDirty(cur_cyl_, head);
}

TrackImage& FloppyDriveV2::mutableTrack(uint8_t head) {
    if (head > 1 || !image_) { g_dummy = {}; return g_dummy; }
    return image_->medium().mutableTrack(cur_cyl_, head);
}

// ─── Persistenz ───────────────────────────────────────────────────────────────

bool FloppyDriveV2::flush() {
    if (!image_)        return true;
    if (write_protect_) return true;   // nichts zurueckzuschreiben
    return image_->flush();
}

bool FloppyDriveV2::autoFlush(uint64_t now_cycles) {
    if (!image_ || write_protect_) return false;
    return image_->autoFlush(now_cycles);
}

bool FloppyDriveV2::writeTrackAt(uint8_t cyl, uint8_t head, const TrackImage& track) {
    if (head > 1 || !image_ || write_protect_) return false;
    return image_->writeTrack(cyl, head, track);
}

// ─── Geometrie ────────────────────────────────────────────────────────────────

DiskGeometry FloppyDriveV2::geometry() const {
    if (image_) return image_->geometry();
    return {};
}
