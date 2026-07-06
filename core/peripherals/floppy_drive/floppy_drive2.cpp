/**
 * @file floppy_drive2.cpp
 * @brief Implementierung von FloppyDriveV2.
 *
 * Physisches Laufwerk (DriveProfile) + gemountetes DiskImage + 1-Spur-Cache je
 * Kopf.  Der Cache wird bei Zylinderwechsel oder Mount/Unmount invalidiert;
 * dirty Spuren werden vor dem Invalidieren zurückgeschrieben.
 *
 * @see core/peripherals/floppy_drive/floppy_drive2.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/floppy_drive2.h"

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

    DiskGeometry geo = img->geometry();

    // Geometrie gegen das physische Laufwerk prüfen.
    if (geo.num_cyls > profile_.num_cyls) {
        last_error_ = "zu viele Spuren für Laufwerk";
        return false;
    }
    if (geo.num_heads > profile_.num_heads) {
        last_error_ = "zu viele Köpfe";
        return false;
    }
    if (!profile_.supports(geo.encoding)) {
        last_error_ = "Verfahren vom Laufwerk nicht unterstützt";
        return false;
    }

    image_        = std::move(img);
    write_protect_ = write_protect;
    cur_cyl_      = 0;

    // Cache invalidieren.
    for (auto& c : cache_) { c.valid = false; c.dirty = false; }

    return true;
}

void FloppyDriveV2::unmount() {
    flush();
    image_.reset();
    cur_cyl_ = 0;
    for (auto& c : cache_) { c.valid = false; c.dirty = false; }
}

// ─── Spurwahl ─────────────────────────────────────────────────────────────────

bool FloppyDriveV2::step(bool inward) {
    if (!image_) return false;

    // Dirty-Spuren der aktuellen Position zurückschreiben, bevor gewechselt wird.
    flush();
    for (auto& c : cache_) { c.valid = false; }

    if (inward) {
        if (cur_cyl_ < profile_.num_cyls - 1) ++cur_cyl_;
    } else {
        if (cur_cyl_ > 0) --cur_cyl_;
    }
    return true;
}

bool FloppyDriveV2::seek(uint8_t cyl) {
    if (!image_) return false;

    const uint8_t ziel = (cyl < profile_.num_cyls) ? cyl
                                                    : static_cast<uint8_t>(profile_.num_cyls - 1);
    if (ziel == cur_cyl_) return true;

    flush();
    for (auto& c : cache_) { c.valid = false; }

    cur_cyl_ = ziel;
    return true;
}

// ─── Spur-Cache ───────────────────────────────────────────────────────────────

void FloppyDriveV2::ensureCached(uint8_t head) {
    if (head > 1) return;

    CachedTrack& ct = cache_[head];

    // Cache gültig und noch auf demselben Zylinder?
    if (ct.valid && ct.cyl == cur_cyl_) return;

    // Veralteten dirty-Eintrag zuerst zurückschreiben.
    if (ct.valid && ct.dirty && image_ && !write_protect_) {
        image_->writeTrack(ct.cyl, head, ct.img);
        ct.dirty = false;
    }

    // Neue Spur laden.
    if (image_) {
        ct.img   = image_->readTrack(cur_cyl_, head);
    } else {
        ct.img   = {};
    }
    ct.cyl   = cur_cyl_;
    ct.valid = true;
    ct.dirty = false;
}

const TrackImage& FloppyDriveV2::track(uint8_t head) {
    static const TrackImage leer{};
    if (head > 1) return leer;
    ensureCached(head);
    return cache_[head].img;
}

void FloppyDriveV2::markTrackDirty(uint8_t head) {
    if (head > 1) return;
    ensureCached(head);
    cache_[head].dirty = true;
}

TrackImage& FloppyDriveV2::mutableTrack(uint8_t head) {
    static TrackImage leer_mut{};
    if (head > 1) return leer_mut;
    ensureCached(head);
    return cache_[head].img;
}

// ─── Flush ────────────────────────────────────────────────────────────────────

bool FloppyDriveV2::flush() {
    if (!image_) return true;

    bool ok = true;
    for (uint8_t h = 0; h < 2; ++h) {
        CachedTrack& ct = cache_[h];
        if (!ct.valid || !ct.dirty) continue;
        if (write_protect_)         continue;

        if (!image_->writeTrack(ct.cyl, h, ct.img)) {
            ok = false;
        } else {
            ct.dirty = false;
        }
    }
    // Backend auf den Host persistieren: RawSectorImage schreibt bereits in
    // writeTrack() synchron (flush() = no-op); HfeImage sammelt die Spuren intern
    // und schreibt die Datei erst hier (flush() ist no-op, wenn nichts dirty ist).
    if (!image_->flush()) ok = false;
    return ok;
}

bool FloppyDriveV2::writeTrackAt(uint8_t cyl, uint8_t head, const TrackImage& track) {
    if (head > 1 || !image_ || write_protect_) return false;

    bool ok = image_->writeTrack(cyl, head, track);

    // Cache kohärent halten: hält der Cache (head) genau diesen Zylinder, ersetzen;
    // sonst unangetastet lassen (Image ist die Wahrheit, beim nächsten readTrack frisch).
    CachedTrack& ct = cache_[head];
    if (ct.valid && ct.cyl == cyl) {
        ct.img   = track;
        ct.dirty = false;
    }
    return ok;
}

// ─── Geometrie ────────────────────────────────────────────────────────────────

DiskGeometry FloppyDriveV2::geometry() const {
    if (image_) return image_->geometry();
    return {};
}
