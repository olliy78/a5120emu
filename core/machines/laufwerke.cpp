/**
 * @file laufwerke.cpp
 * @brief Laufwerksverwaltung (gemeinsam für A5120 und K8915) — aus A5120Machine
 *        herausgelöst, Verhalten unverändert.  @see laufwerke.h
 */

#include "core/machines/laufwerke.h"
#include "core/filesystem/geometry_probe.h"
#include "core/logger.h"
#include <cstdio>
#include <optional>
#include <stdexcept>

Laufwerke::Laufwerke(K5122& afs, const std::array<DriveProfile, 4>& profile)
    : afs_(afs), profile_(profile)
{
    // Diskettenformate aus data/formats.yaml laden (§8.6).  Fehlt die Datei oder ist
    // sie syntaktisch kaputt, kann die Maschine keine Diskette mounten/anlegen — das
    // ist ein Startabbruch mit klarer Meldung, kein stiller Weiterlauf.  Einzelne
    // FEHLERHAFTE Formatdefinitionen sind dagegen nicht fatal: sie werden übersprungen
    // und über formatCatalog().issues() gemeldet.
    std::string fatal;
    katalog_ = FormatCatalog::loadDefault(&fatal);
    if (!fatal.empty()) throw std::runtime_error(fatal);

    // Übersprungene Definitionen zusätzlich auf stderr — eine Konfigurationspanne
    // muss sichtbar sein, auch wenn das Logging aus ist oder in eine Datei geht.
    for (const auto& issue : katalog_.issues()) {
        LOG_WARN("Formate", "%s", issue.c_str());
        std::fprintf(stderr, "[Formatkatalog] %s\n", issue.c_str());
    }
}

void Laufwerke::autoFlush(uint64_t total_cycles) {
    // Verzoegertes Zurueckschreiben geaenderter Spuren in die gebundene Image-Datei:
    // das interne Abbild ist die Wahrheit, die Datei folgt ihm mit leichtem Zeitversatz
    // (doc/design/09_floppy_drive.md §6.1).
    if (total_cycles < naechste_pruefung_) return;
    naechste_pruefung_ = total_cycles + kPruefAbstand;
    std::lock_guard<std::mutex> lk(mutex_);
    afs_.autoFlushDisks(total_cycles);
}

bool Laufwerke::mountDiskImage(int drive, std::unique_ptr<DiskImage> img, bool wp) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }
    if (!profile_[drive].present) {
        last_error_ = "Kein Laufwerk an Slot " + std::to_string(drive);
        return false;
    }
    if (!img) { last_error_ = "kein Abbild uebergeben"; return false; }

    std::lock_guard<std::mutex> lk(mutex_);
    if (afs_.mountDisk(drive, std::move(img), wp)) { last_error_.clear(); return true; }
    const std::string drv_err = afs_.drive(drive).lastError();
    last_error_ = drv_err.empty() ? "Mounten fehlgeschlagen" : drv_err;
    return false;
}

bool Laufwerke::mountDisk(int drive, const std::string& path,
                              const std::string& format_name, bool wp) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }
    if (!profile_[drive].present) {
        last_error_ = "Kein Laufwerk an Slot " + std::to_string(drive);
        return false;
    }

    const DiskFormat* fmt = katalog_.find(format_name);
    if (!fmt) {
        last_error_ = "Unbekanntes Format: " + format_name;
        return false;
    }
    // BEWUSST KEINE drives:-Prüfung beim Mounten eines VORHANDENEN Images:
    //  - bei self-describing Containern (.hfe) ist der Formatname nur ein Platzhalter,
    //    die Geometrie kommt aus der Datei (so mountet z. B. tools/format_driver alle
    //    Slots nominell als "cpa780");
    //  - der Laufwerkstyp ist auf der A5120 reine BIOS-Software, Combo-Boot-Disketten
    //    betreiben an B:/C: bewusst Fremdtypen (CLAUDE.md, doc/format.md §11).
    // Die Kompatibilität wird dort erzwungen, wo das Format die Struktur wirklich
    // bestimmt: in createDisk() und in der angebotenen Auswahl (compatibleFormats()).
    std::lock_guard<std::mutex> lk(mutex_);
    if (afs_.mountDisk(drive, path, *fmt, wp)) { last_error_.clear(); return true; }

    // Grund aus dem Laufwerk übernehmen (Geometrie-/Verfahrenskonflikt); wurde das
    // Image gar nicht erst geöffnet, ist die Laufwerks-Meldung leer → Fallback.
    const std::string drv_err = afs_.drive(drive).lastError();
    last_error_ = drv_err.empty()
                      ? ("Image konnte nicht geöffnet werden: " + path)
                      : drv_err;
    return false;
}

bool Laufwerke::createDisk(int drive, const std::string& path,
                              const std::string& format_name, bool write_protect) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }

    const DriveProfile& prof = profile_[drive];
    if (!prof.present) {
        last_error_ = "Kein Laufwerk an Slot " + std::to_string(drive);
        return false;
    }

    std::unique_ptr<DiskImage> img;

    if (format_name.empty()) {
        // ── Echte Leerdiskette ──────────────────────────────────────────────
        // Geometrie kommt vom LAUFWERK (nicht von einem Format): eine unformatierte
        // Diskette hat kein Sektorlayout.  Sie wird anschließend vom Gastsystem
        // formatiert — inklusive Fremdformaten, die Nutzdaten hinter die Daten-CRC
        // hängen (UDOS-Sektorkontrollblock), was ein .img nicht speichern könnte.
        if (!path.empty()
            && ImageCodec::fromExtension(path) == ContainerType::Img) {
            last_error_ = "Eine leere Diskette kann nicht als rohes Sektorimage (.img) "
                          "angelegt werden — bitte .hfe oder .dmk waehlen (oder ein "
                          "Diskettenformat angeben, um vorformatiert anzulegen).";
            return false;
        }

        // Vorschlagsverfahren des Laufwerks (reines FM-Laufwerk → FM, sonst MFM);
        // je Spur überschreibt es der Formatierlauf ohnehin.
        const Encoding enc = (prof.supports_mfm ? Encoding::MFM : Encoding::FM);
        img = DiskImage::createBlank(prof.num_cyls, prof.num_heads, enc);
        if (!img) {
            last_error_ = "createDisk: Laufwerksgeometrie unbrauchbar ("
                          + prof.name + ")";
            return false;
        }
        // Sofort in die Zieldatei schreiben, damit sie ab dem ersten Moment existiert
        // und der Autosave eine Bindung hat.  Leerer Pfad = nur im Speicher.
        if (!path.empty() && !img->saveAs(path, std::nullopt)) {
            last_error_ = std::string("createDisk: ") + img->lastError();
            return false;
        }
        img->setWriteProtect(write_protect);
    } else {
        // ── Vorformatierte Diskette nach Katalogformat ───────────────────────
        const DiskFormat* fmt = katalog_.find(format_name);
        if (!fmt) {
            last_error_ = "createDisk: unbekanntes Format '" + format_name + "'";
            return false;
        }
        if (!fmt->supportsDrive(prof.name)) {
            last_error_ = "createDisk: Format '" + format_name + "' passt nicht zum Laufwerk '"
                          + prof.name + "'";
            return false;
        }
        // Verfahren kommt aus dem FORMAT (pro Spurbereich).  Für den Container-Header
        // und rohe .img zählt das vorherrschende Verfahren.
        img = DiskImage::create(path, *fmt, write_protect, fmt->predominantEncoding());
        if (!img) {
            last_error_ = "createDisk fehlgeschlagen (Format '" + fmt->name + "'): " + path;
            return false;
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    if (afs_.mountDisk(drive, std::move(img), write_protect)) { last_error_.clear(); return true; }
    const std::string drv_err = afs_.drive(drive).lastError();
    last_error_ = drv_err.empty()
                      ? ("createDisk: Mounten fehlgeschlagen: " + path)
                      : drv_err;
    return false;
}

bool Laufwerke::saveDiskAs(int drive, const std::string& path,
                              const std::string& format_name) {
    if (drive < 0 || drive > 3) { last_error_ = "Invalid drive"; return false; }
    if (path.empty())           { last_error_ = "Kein Zielpfad angegeben"; return false; }

    std::lock_guard<std::mutex> lk(mutex_);
    DiskImage* img = afs_.drive(drive).image();
    if (!img) {
        last_error_ = "Kein Datentraeger in Laufwerk " + std::to_string(drive);
        return false;
    }

    // Das Diskettenformat wird NUR fuer das rohe Sektorimage gebraucht — .hfe/.dmk
    // sind self-describing.
    std::optional<DiskFormat> fmt;
    if (ImageCodec::fromExtension(path) == ContainerType::Img) {
        if (format_name.empty()) {
            last_error_ = "Speichern als .img braucht die Angabe eines Diskettenformats.";
            return false;
        }
        const DiskFormat* f = katalog_.find(format_name);
        if (!f) {
            last_error_ = "Unbekanntes Format: " + format_name;
            return false;
        }
        fmt = *f;
    }

    if (img->saveAs(path, fmt)) { last_error_.clear(); return true; }
    last_error_ = img->lastError();
    return false;
}

bool Laufwerke::isDiskRawCompatible(int drive) const {
    if (drive < 0 || drive > 3) return false;
    const DiskImage* img = afs_.drive(drive).image();
    return img && img->rawCompatible();
}

std::string Laufwerke::diskPath(int drive) const {
    if (drive < 0 || drive > 3) return "";
    const DiskImage* img = afs_.drive(drive).image();
    return img ? img->path() : "";
}

std::string Laufwerke::diskContainer(int drive) const {
    if (drive < 0 || drive > 3) return "";
    const DiskImage* img = afs_.drive(drive).image();
    if (!img || !img->hasFile()) return "";
    return ImageCodec::name(img->container());
}

std::string Laufwerke::diskNotice(int drive) const {
    if (drive < 0 || drive > 3) return "";
    if (!afs_.drive(drive).isMounted()) return "";
    return afs_.drive(drive).noticeText();
}

DiskGeometry Laufwerke::diskGeometry(int drive) const {
    if (drive < 0 || drive > 3) return {};
    const DiskImage* img = afs_.drive(drive).image();
    return img ? img->geometry() : DiskGeometry{};
}

bool Laufwerke::isDiskFormatted(int drive) const {
    if (drive < 0 || drive > 3) return false;
    const DiskImage* img = afs_.drive(drive).image();
    return img && img->medium().formatted();
}

namespace {

/// Stehen zwei Treffer der Geometrie-Erkennung auf demselben Rang?
bool gleichPlatziert(const GeometryMatch& a, const GeometryMatch& b) {
    return a.gap_tracks   == b.gap_tracks   && a.stray_tracks == b.stray_tracks
        && a.slack_cyls   == b.slack_cyls   && a.defect_tracks == b.defect_tracks
        && a.empty_tracks == b.empty_tracks;
}

/// Die Spurbelegung eines Formats als Zeichenkette — sein Sektorraum, aufgeloest.
///
/// Zwei Katalogeintraege mit gleicher Zeichenkette liefern byteweise dasselbe
/// `.img`; sie sind fuer die Erkennung dasselbe Format unter zwei Namen.
std::string sektorraum(const DiskFormat* f) {
    if (!f) return "";
    std::string s = "step" + std::to_string(f->step) + ";";
    for (uint8_t c = 0; c < f->physicalCylinders(); ++c) {
        for (uint8_t h = 0; h < f->numHeads(); ++h) {
            const TrackFormat* t = f->findTrack(c, h);
            if (!t) { s += "-;"; continue; }
            s += std::to_string(t->secs_per_track) + "x"
               + std::to_string(t->bytes_per_sec)  + "@"
               + std::to_string(t->first_sector_id)
               + (t->encoding == Encoding::FM ? "f" : "m") + ";";
        }
    }
    return s;
}

}  // namespace

std::string Laufwerke::detectedFormatName(int drive) const {
    if (drive < 0 || drive > 3) return "";
    std::lock_guard<std::mutex> lk(mutex_);
    const DiskImage* img = afs_.drive(drive).image();
    if (!img) return "";

    // Rohes Sektorabbild: hier gibt es nichts zu messen.  Ein `.img` traegt keine
    // Adressmarken — seine Geometrie ist die beim Einlegen ERKLAERTE, und genau die
    // steht am Abbild (DiskImage::diskFormat() ist nur fuer `.img` besetzt).
    if (const DiskFormat* erklaert = img->diskFormat()) return erklaert->name;

    // Eine Diskette, die ihre Spuren erst bei Bedarf holt (physisches Laufwerk),
    // wird NICHT vermessen: GeometryProbe::measure() geht ueber DiskMedium::track()
    // und zoege damit die ganze Scheibe ein (doc/merkposten/physische_diskette.md).
    // Sobald der Vorausleser sie vollstaendig im Speicher hat, misst es sich umsonst.
    const DiskMedium& med = img->medium();
    if (med.loader() != nullptr && !med.complete()) return "";

    const std::vector<MeasuredTrack> gemessen = GeometryProbe::measure(med);
    const std::vector<GeometryMatch> treffer =
        GeometryProbe::matchAll(gemessen, katalog_.formats());
    if (treffer.empty()) return "";

    // Zwei gleich gut platzierte Treffer heissen „unbekannt", nicht „der erste":
    // die Rangfolge in matchAll() entschiede sonst per Katalogreihenfolge.
    //
    // ABER nur, wenn sie auch verschiedene Disketten BESCHREIBEN.  `cpa640` und
    // `k5601_16x256` sind bis auf den Namen derselbe Eintrag (80×2×16×256 MFM) —
    // da ist nichts geraten: beide Namen bezeichnen denselben Sektorraum, und
    // genau der ist es, was ein `.img`-Export festhaelt.  Verglichen wird deshalb
    // die aufgeloeste Spurbelegung, nicht die Bereichsliste: dieselbe Geometrie
    // laesst sich im Katalog verschieden zerlegen.
    for (size_t i = 1; i < treffer.size(); ++i) {
        if (!gleichPlatziert(treffer[0], treffer[i])) break;
        if (sektorraum(treffer[0].format) != sektorraum(treffer[i].format)) return "";
    }
    return treffer.front().format ? treffer.front().format->name : std::string();
}

bool Laufwerke::flushDisks() {
    std::lock_guard<std::mutex> lk(mutex_);
    return afs_.flushDisks();
}

std::string Laufwerke::defaultFormatName(int drive) const {
    if (drive < 0 || drive > 3) return "";
    const DiskFormat* f = katalog_.defaultFor(profile_[drive]);
    return f ? f->name : "";
}

std::vector<std::string> Laufwerke::compatibleFormats(int drive) const {
    std::vector<std::string> out;
    if (drive < 0 || drive > 3) return out;

    // Kompatibilität ist jetzt EXPLIZIT im Katalog deklariert (`drives:`), keine
    // Geometrie-Heuristik mehr — das Standardformat des Slots steht an erster Stelle.
    for (const DiskFormat* f : katalog_.forDrive(profile_[drive]))
        out.push_back(f->name);
    return out;
}

std::string Laufwerke::formatDescription(const std::string& format_name) const {
    const DiskFormat* f = katalog_.find(format_name);
    return f ? f->description : "";
}

bool Laufwerke::unmountDisk(int drive) {
    if (drive < 0 || drive > 3) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    return afs_.unmountDisk(drive);
}

bool Laufwerke::isDiskActive(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskActive(drive);
}

bool Laufwerke::isDiskWriteProtected(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDiskWriteProtected(drive);
}

bool Laufwerke::isDiskLedOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isDriveLedOn(drive);
}

bool Laufwerke::isMotorOn(int drive) const {
    if (drive < 0 || drive > 3) return false;
    return afs_.isMotorOn(drive);
}

bool Laufwerke::isHeadLoaded() const {
    return afs_.isHeadLoaded();
}

void Laufwerke::setDiskWriteProtect(int drive, bool wp) {
    if (drive < 0 || drive > 3) return;
    afs_.setWriteProtect(drive, wp);
}

