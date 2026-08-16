/**
 * @file disk_image.cpp
 * @brief Implementierung von DiskImage (Medium + Dateibindung + Autosave).
 *
 * @see core/peripherals/floppy_drive/disk_image.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/dmk_codec.h"
#include "core/peripherals/floppy_drive/hfe_codec.h"
#include "core/peripherals/floppy_drive/img_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

#include <utility>

namespace {

/// @brief Logische Sektoren einer Spur mit Nutzdaten 0xE5 (leere CP/M-Sektoren).
std::vector<LogicalSector> leereSektoren(const TrackFormat& tf, uint8_t cyl, uint8_t head) {
    std::vector<LogicalSector> secs;
    secs.reserve(tf.secs_per_track);
    for (uint8_t i = 0; i < tf.secs_per_track; ++i) {
        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = head;
        ls.id   = static_cast<uint8_t>(tf.first_sector_id + i);
        ls.size = tf.bytes_per_sec;
        ls.data.assign(tf.bytes_per_sec, 0xE5);
        secs.push_back(std::move(ls));
    }
    return secs;
}

}  // namespace

// ─── Fabriken ────────────────────────────────────────────────────────────────

std::unique_ptr<DiskImage> DiskImage::open(const std::string& path,
                                           std::optional<DiskFormat> fmt,
                                           bool write_protect) {
    auto img = std::make_unique<DiskImage>();
    img->container_     = ImageCodec::detect(path);
    img->write_protect_ = write_protect;

    std::string err;
    bool ok = false;

    switch (img->container_) {
        case ContainerType::Hfe: {
            HfeCodec::SourceInfo info;
            ok = HfeCodec::load(path, img->medium_, &info, err);
            // Ein ueberabgetasteter Flux-Mitschnitt wird beim Laden auf die Nominalrate
            // quantisiert — treu zurueckschreiben laesst er sich nicht.  Schreibzugriffe
            // bleiben im Medium, der Autosave schweigt; saveAs() in eine NEUE Datei geht.
            img->binding_writable_ = info.write_allowed && !info.oversampled;
            break;
        }
        case ContainerType::Dmk: {
            DmkCodec::SourceInfo info;
            ok = DmkCodec::load(path, img->medium_, &info, err);
            img->binding_writable_ = info.write_allowed;
            break;
        }
        default:
            if (!fmt.has_value()) return nullptr;   // .img braucht die Geometrie
            ok = ImageCodec::load(path, ContainerType::Img, &fmt.value(), img->medium_, err);
            img->disk_format_ = fmt;
            break;
    }

    if (!ok) return nullptr;

    img->path_ = path;
    return img;
}

std::unique_ptr<DiskImage> DiskImage::createBlank(uint8_t num_cyls, uint8_t num_heads,
                                                  Encoding default_enc) {
    if (num_cyls == 0 || num_heads == 0) return nullptr;
    auto img = std::make_unique<DiskImage>();
    img->medium_ = DiskMedium(num_cyls, num_heads, default_enc);
    // Keine Dateibindung: das Medium lebt bis zum ersten saveAs() nur im Speicher.
    return img;
}

std::unique_ptr<DiskImage> DiskImage::create(const std::string& path,
                                             std::optional<DiskFormat> fmt,
                                             bool write_protect, Encoding enc) {
    if (!fmt.has_value()) return nullptr;

    const uint8_t num_cyls  = fmt->numCylinders();
    const uint8_t num_heads = fmt->numHeads();
    if (num_cyls == 0 || num_heads == 0) return nullptr;

    auto img = std::make_unique<DiskImage>();
    // Beim Doppelschritt ist das Medium fast doppelt so breit wie das Format tief:
    // die uebersprungenen Zylinder bleiben unformatiert.
    img->medium_ = DiskMedium(fmt->physicalCylinders(), num_heads,
                              fmt->tracks.empty() ? enc : fmt->predominantEncoding());

    // Je Spurbereich eine gueltige IBM-Spur bauen (Verfahren PRO Bereich → Mischdichte).
    // Die Spurnummer im ID-Feld ist die LOGISCHE — so schreibt es auch das Gastsystem.
    for (uint8_t c = 0; c < num_cyls; ++c) {
        const uint8_t pc = fmt->physicalCylinder(c);
        for (uint8_t h = 0; h < num_heads; ++h) {
            const TrackFormat* tf = fmt->findTrack(c, h);
            if (!tf) continue;   // Spur existiert im Format nicht → bleibt unformatiert
            img->medium_.setTrack(
                pc, h, TrackCodec::buildTrack(leereSektoren(*tf, c, h), tf->encoding));
        }
    }

    img->container_ = ImageCodec::fromExtension(path);
    if (img->container_ == ContainerType::Img) img->disk_format_ = fmt;

    std::string err;
    if (!ImageCodec::save(path, img->container_, img->diskFormat(), img->medium_, err))
        return nullptr;

    img->medium_.clearDirty();
    img->path_          = path;
    img->write_protect_ = write_protect;
    return img;
}

std::unique_ptr<DiskImage> DiskImage::openPhysical(const TrackSyncSpec& spec) {
    auto img = std::unique_ptr<DiskImage>(new DiskImage());
    // Der Synchronisierer setzt Geometrie und Verfahren und erklaert ALLE Spuren fuer
    // unbekannt — gelesen wird erst beim ersten Zugriff (doc/design/14 §4).
    img->sync_          = std::make_shared<TrackSync>(spec, img->medium_);
    img->write_protect_ = !spec.writable;
    // Keine Dateibindung: der Autosave schweigt, saveAs() legt bei Bedarf eine an.
    img->binding_writable_ = false;
    return img;
}

// ─── Schreibpfad ─────────────────────────────────────────────────────────────

bool DiskImage::writeTrack(uint8_t cyl, uint8_t head, const TrackImage& track) {
    if (write_protect_) {
        last_error_ = "Schreibschutz aktiv";
        return false;
    }
    if (cyl >= medium_.numCylinders() || head >= medium_.numHeads()) {
        last_error_ = "Spur ausserhalb der Geometrie";
        return false;
    }
    medium_.setTrack(cyl, head, track);
    return true;
}

// ─── Persistenz ──────────────────────────────────────────────────────────────

bool DiskImage::flush() {
    // Physische Diskette: „speichern" heisst, auf die Rueckfuehrung zu warten — und
    // die gilt erst als geglueckt, wenn jede Spur zurueckGELESEN wurde (§7.1).  Der
    // Grund muss durchgereicht werden, sonst steht der Bediener vor einem
    // „Speichern fehlgeschlagen" ohne Spurnummer.
    if (sync_) {
        if (sync_->flushPending()) { last_error_.clear(); return true; }
        last_error_ = sync_->lastError().empty()
                          ? "Die Diskette liess sich nicht vollstaendig beschreiben."
                          : sync_->lastError();
        return false;
    }
    if (!medium_.dirty()) { dirty_since_ = 0; return true; }
    if (write_protect_)   return true;   // Schreibschutz: nichts zurueckschreiben
    if (!hasFile())       return true;   // reines Speichermedium — nichts zu tun
    if (!binding_writable_) {
        last_error_ = "Quelldatei ist nur lesbar: " + path_;
        return false;
    }

    std::string err;
    if (!ImageCodec::save(path_, container_, diskFormat(), medium_, err)) {
        last_error_ = err;
        return false;
    }
    medium_.clearDirty();
    dirty_since_ = 0;
    return true;
}

bool DiskImage::autoFlush(uint64_t now_cycles) {
    if (!hasFile() || !binding_writable_) return false;
    if (!medium_.dirty()) { dirty_since_ = 0; return false; }

    // Auf eine SCHREIBPAUSE warten, nicht bloss auf Ablauf einer Frist: solange das
    // Gastsystem weiterschreibt (FORMAT/COPY fassen hunderte Spuren an), steigt die
    // Revision und die Uhr wird neu gestellt.  Sonst wuerde die ganze Image-Datei
    // mitten im Lauf dutzendfach neu codiert.
    const uint64_t rev = medium_.revision();
    if (rev != last_seen_revision_ || dirty_since_ == 0) {
        last_seen_revision_ = rev;
        dirty_since_        = now_cycles ? now_cycles : 1;
        return false;
    }
    if (now_cycles < dirty_since_ + kAutoFlushDelayCycles) return false;

    return flush();
}

bool DiskImage::exportTo(const std::string& path, std::optional<DiskFormat> fmt) {
    const ContainerType ziel = ImageCodec::fromExtension(path);

    // Eine Abbilddatei ist eine Aussage ueber die GANZE Diskette.  Bei einer physischen
    // Quelle muessen die noch unbekannten Spuren also vorher gelesen werden — sonst
    // schriebe die Datei sie als Fuellbytes fest (doc/design/14 §4.2).  Das ist der
    // einzige Ort, an dem der Kern von sich aus einen vollstaendigen Lauf anstoesst.
    if (sync_ && !medium_.complete()) {
        if (!sync_->loadAll()) {
            last_error_ = "Die Diskette konnte nicht vollstaendig gelesen werden: " +
                          sync_->lastError();
            return false;
        }
    }

    if (ImageCodec::needsDiskFormat(ziel)) {
        if (!fmt.has_value()) {
            last_error_ = "Ein rohes Sektorimage (.img) braucht die Angabe eines "
                          "Diskettenformats.";
            return false;
        }
        // Hier — wo der Bediener das Format selbst waehlt — wird die Geometrie
        // streng geprueft; der Autosave in eine bereits gebundene .img bleibt
        // tolerant (s. ImgCodec::save).  Die Darstellbarkeitspruefung (Gap-Anhaenge,
        // unformatierte Diskette) meldet ImgCodec::save weiter unten mit der
        // aussagekraeftigeren Begruendung.
        if (medium_.rawCompatible()) {
            const std::string mismatch = ImgCodec::mismatchReason(*fmt, medium_);
            if (!mismatch.empty()) { last_error_ = mismatch; return false; }
        }
    }

    std::string err;
    if (!ImageCodec::save(path, ziel, fmt.has_value() ? &fmt.value() : nullptr,
                          medium_, err)) {
        last_error_ = err;
        return false;
    }
    last_error_.clear();
    return true;
}

bool DiskImage::saveAs(const std::string& path, std::optional<DiskFormat> fmt) {
    if (!exportTo(path, fmt)) return false;

    // Ab jetzt zeigt die Bindung auf die neue Datei — Autosave folgt dorthin.
    path_             = path;
    container_        = ImageCodec::fromExtension(path);
    disk_format_      = (container_ == ContainerType::Img) ? fmt : std::nullopt;
    binding_writable_ = true;
    medium_.clearDirty();
    dirty_since_      = 0;
    return true;
}
