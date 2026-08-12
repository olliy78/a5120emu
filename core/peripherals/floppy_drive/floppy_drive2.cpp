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

#include "core/logger.h"

#include <algorithm>

namespace {
const TrackImage kLeer{};
TrackImage       g_dummy{};

// ─── Spurdichte (nur 5,25″) ──────────────────────────────────────────────────
//
// 48 tpi = 40 Spuren über den ganzen Radius, 96 tpi = 80.  Beides wird an der
// Spurzahl abgelesen.  Die Toleranz nach oben (45 statt 40) faengt die 1–3 leeren
// Gap-Spuren ab, die viele HFE-Abbilder anhaengen; die Untergrenze 35 sorgt dafuer,
// dass nur eine VOLLE Diskette so eingeordnet wird — ein Abbild mit ein paar Spuren
// ist ein Bruchstueck und wird nicht zur 48-tpi-Diskette erklaert.
// Dass eine 40-Zylinder-Diskette nur die aeussere Haelfte einer 96-tpi-Diskette waere,
// gibt es dagegen nicht — so beschreibt kein Laufwerk eine Diskette.
constexpr uint8_t k48tpiMin = 35;
constexpr uint8_t k48tpiMax = 45;
constexpr uint8_t k96tpiMin = 70;

TrackPitch trackPitchFor(const DiskGeometry& geo, const DriveProfile& prof) {
    // 8″ kennt nur eine Dichte (77 Spuren) — dort gibt es nichts zu uebersetzen.
    if (prof.medium_inch != 5 || geo.num_cyls == 0) return TrackPitch::Direct;
    const bool disk_48tpi  = geo.num_cyls >= k48tpiMin && geo.num_cyls <= k48tpiMax;
    const bool disk_96tpi  = geo.num_cyls >= k96tpiMin;
    const bool drive_96tpi = prof.num_cyls >= k96tpiMin;
    const bool drive_48tpi = prof.num_cyls <= k48tpiMax;
    if (disk_48tpi && drive_96tpi) return TrackPitch::DoubleStep;
    if (disk_96tpi && drive_48tpi) return TrackPitch::HalfStep;
    return TrackPitch::Direct;
}

/// @brief Bis zu welchem Diskettenzylinder reicht der Kopf dieses Laufwerks?
int reachableCylinders(TrackPitch pitch, const DriveProfile& prof) {
    switch (pitch) {
        case TrackPitch::DoubleStep: return (prof.num_cyls + 1) / 2;
        case TrackPitch::HalfStep:   return std::min(255, prof.num_cyls * 2);
        case TrackPitch::Direct:     break;
    }
    return prof.num_cyls;
}
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

    // Spurdichte und Seitenzahl der Diskette gegen das Laufwerk stellen.  Beides ist
    // kein Ausschlusskriterium, sondern bestimmt, WIE die Diskette gelesen wird.
    const TrackPitch pitch      = trackPitchFor(geo, profile_);
    const bool       side0_only = geo.num_heads > profile_.num_heads;
    const int        reach      = reachableCylinders(pitch, profile_);

    // Spuren mit Daten jenseits der Reichweite des Kopfes sind ein echter
    // Kapazitaetskonflikt → ablehnen.  Ueberzaehlige Zylinder sind dagegen zulaessig,
    // solange sie unformatiert sind: das Laufwerk erreicht sie nie (step() clampt auf
    // num_cyls-1).  Viele HFE-Abbilder haengen 1–3 leere Gap-Spuren an.
    // Bei DoubleStep/HalfStep sind die uebersprungenen Spuren dazwischen ausdruecklich
    // KEIN Konflikt — sie sind der Sinn der Uebersetzung.
    if (geo.num_cyls > reach) {
        for (int c = reach; c < geo.num_cyls; ++c) {
            for (uint8_t h = 0; h < geo.num_heads; ++h) {
                for (MarkType m : img->readTrack(static_cast<uint8_t>(c), h).marks) {
                    if (m == MarkType::Id || m == MarkType::Data) {
                        last_error_ = "Diskette hat " + std::to_string(geo.num_cyls)
                                    + " Spuren mit Daten, Laufwerk '" + profile_.name
                                    + "' erreicht nur " + std::to_string(reach);
                        return false;
                    }
                }
            }
        }
    }
    // Verfahren nur pruefen, wenn die Diskette ueberhaupt formatiert ist — eine
    // LEERDISKETTE traegt nur einen Vorschlagswert und passt in jedes Laufwerk.
    if (img->medium().formatted() && !profile_.supports(geo.encoding)) {
        last_error_ = std::string("Diskette ist ")
                    + (geo.encoding == Encoding::FM ? "FM" : "MFM")
                    + "-aufgezeichnet, Laufwerk '" + profile_.name
                    + "' beherrscht das nicht";
        return false;
    }

    image_ = std::move(img);
    image_->setWriteProtect(write_protect);
    write_protect_   = write_protect;
    cur_cyl_         = 0;
    pitch_           = pitch;
    side0_only_      = side0_only;
    last_lost_write_ = -1;

    // Je Einschraenkung ein kurzer Satz — die Oberflaeche zeigt sie am Laufwerk an.
    notices_.clear();
    if (pitch_ == TrackPitch::DoubleStep) notices_.emplace_back("Double Step aktiviert");
    if (pitch_ == TrackPitch::HalfStep)
        notices_.emplace_back("Laufwerk liest nur jede zweite Spur");
    if (side0_only_) notices_.emplace_back("Nur Seite 0 verwendbar");
    for (const auto& n : notices_)
        LOG_INFO("Floppy", "Laufwerk '%s' (%d Spuren, %d Koepfe) + Diskette (%d/%d): %s",
                 profile_.name.c_str(), profile_.num_cyls, profile_.num_heads,
                 geo.num_cyls, geo.num_heads, n.c_str());
    return true;
}

void FloppyDriveV2::unmount() {
    flush();
    image_.reset();
    cur_cyl_         = 0;
    pitch_           = TrackPitch::Direct;
    side0_only_      = false;
    last_lost_write_ = -1;
    notices_.clear();
}

std::string FloppyDriveV2::noticeText() const {
    std::string out;
    for (const auto& n : notices_) {
        if (!out.empty()) out += '\n';
        out += n;
    }
    return out;
}

void FloppyDriveV2::setWriteProtect(bool wp) {
    write_protect_ = wp;
    if (image_) image_->setWriteProtect(wp);
}

// ─── Spurwahl ─────────────────────────────────────────────────────────────────

bool FloppyDriveV2::step(bool inward) {
    if (!profile_.present) return false;
    if (inward) {
        if (cur_cyl_ < profile_.num_cyls - 1) ++cur_cyl_;
    } else {
        if (cur_cyl_ > 0) --cur_cyl_;
    }
    return true;
}

bool FloppyDriveV2::seek(uint8_t cyl) {
    if (!profile_.present) return false;
    cur_cyl_ = (cyl < profile_.num_cyls) ? cyl
                                         : static_cast<uint8_t>(profile_.num_cyls - 1);
    return true;
}

// ─── Spurzugriff (direkt auf dem Medium) ──────────────────────────────────────

int FloppyDriveV2::mediumCylinder(uint8_t pos) const {
    switch (pitch_) {
        case TrackPitch::DoubleStep:
            // Zwischen zwei Spuren der 48-tpi-Diskette liegt eine Kopfposition, unter
            // der nichts ist — deshalb muss der Gast schrittverdoppelt lesen.
            return (pos % 2 == 0) ? pos / 2 : -1;
        case TrackPitch::HalfStep:
            return pos * 2;
        case TrackPitch::Direct:
            break;
    }
    return pos;
}

void FloppyDriveV2::warnLostWrite(uint8_t head) const {
    const int key = (cur_cyl_ << 1) | (head & 1);
    if (key == last_lost_write_) return;   // nicht je Byte dieselbe Zeile
    last_lost_write_ = key;
    LOG_WARN("Floppy", "Schreibzugriff verworfen: unter Kopfposition %d/Kopf %d liegt "
                       "auf dieser Diskette keine Spur (%s)",
             cur_cyl_, head,
             pitch_ == TrackPitch::DoubleStep ? "Double Step"
                                              : (side0_only_ ? "nur Seite 0" : "ausser Reichweite"));
}

const TrackImage& FloppyDriveV2::track(uint8_t head) const {
    if (!image_ || !headReachable(head)) return kLeer;
    const int cyl = mediumCylinder(cur_cyl_);
    if (cyl < 0) return kLeer;
    return image_->readTrack(static_cast<uint8_t>(cyl), head);
}

void FloppyDriveV2::markTrackDirty(uint8_t head) {
    if (!image_) return;
    const int cyl = headReachable(head) ? mediumCylinder(cur_cyl_) : -1;
    if (cyl < 0) { warnLostWrite(head); return; }
    image_->medium().markDirty(static_cast<uint8_t>(cyl), head);
}

TrackImage& FloppyDriveV2::mutableTrack(uint8_t head) {
    const int cyl = (image_ && headReachable(head)) ? mediumCylinder(cur_cyl_) : -1;
    if (cyl < 0) {
        // Der Schreibstrom laeuft in einen Papierkorb: auf der Diskette gibt es hier
        // keine Spur.  Ein richtig eingestellter Gast schreibt nie hierher.
        if (image_) warnLostWrite(head);
        g_dummy = {};
        return g_dummy;
    }
    return image_->medium().mutableTrack(static_cast<uint8_t>(cyl), head);
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
    // @p cyl ist eine KOPFPOSITION (der Vollspur-FORMAT-Commit merkt sie sich, weil der
    // Kopf inzwischen weitergeschritten ist) — also durch dieselbe Uebersetzung schicken.
    if (!image_ || write_protect_) return false;
    const int medium_cyl = headReachable(head) ? mediumCylinder(cyl) : -1;
    if (medium_cyl < 0) {
        LOG_WARN("Floppy", "Vollspur-Schreiben verworfen: Kopfposition %d/Kopf %d liegt "
                           "auf dieser Diskette nicht", cyl, head);
        return false;
    }
    return image_->writeTrack(static_cast<uint8_t>(medium_cyl), head, track);
}

// ─── Geometrie ────────────────────────────────────────────────────────────────

DiskGeometry FloppyDriveV2::geometry() const {
    if (image_) return image_->geometry();
    return {};
}
