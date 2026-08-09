/**
 * @file disk_volume.cpp
 * @brief Umsetzung von @ref DiskVolume.
 *
 * @see doc/design/13_k1520disktool.md §9, §12
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/disk_volume.h"

#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/geometry_probe.h"
#include "core/filesystem/udos/udos_fs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string kleinbuchstaben(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string endung(const std::string& pfad) {
    const size_t p = pfad.find_last_of('.');
    if (p == std::string::npos) return {};
    return kleinbuchstaben(pfad.substr(p + 1));
}

bool leseDatei(const std::string& pfad, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(pfad, std::ios::binary);
    if (!f) { err = "Datei nicht lesbar: " + pfad; return false; }
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool schreibeDatei(const std::string& pfad, const std::vector<uint8_t>& d, std::string& err) {
    std::ofstream f(pfad, std::ios::binary);
    if (!f) { err = "Datei nicht schreibbar: " + pfad; return false; }
    if (!d.empty()) f.write(reinterpret_cast<const char*>(d.data()),
                            static_cast<std::streamsize>(d.size()));
    if (!f) { err = "Schreibfehler: " + pfad; return false; }
    return true;
}

/// @brief Diskette → Linux: CR LF und einzelnes CR werden LF, ab 0x1A ist Schluss.
std::vector<uint8_t> nachLinuxText(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t c = in[i];
        if (c == 0x1A) break;                       // CP/M-Dateiende
        if (c == '\r') {
            if (i + 1 < in.size() && in[i + 1] == '\n') continue;   // CR LF → LF
            out.push_back('\n');                                     // einzelnes CR → LF
            continue;
        }
        out.push_back(c);
    }
    return out;
}

/// @brief Linux → Diskette: LF wird CR LF (CR LF bleibt CR LF).
std::vector<uint8_t> nachDiskettenText(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 16);
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t c = in[i];
        if (c == '\n' && (i == 0 || in[i - 1] != '\r')) out.push_back('\r');
        out.push_back(c);
    }
    return out;
}

}  // namespace

// ─── FileRef ─────────────────────────────────────────────────────────────────

FileRef FileRef::parse(const std::string& text, int default_volume) {
    FileRef r;
    r.volume = default_volume;
    r.name   = text;

    const size_t schraeg = text.find('/');
    if (schraeg == std::string::npos) return r;

    const std::string kopf = kleinbuchstaben(text.substr(0, schraeg));
    if (kopf.size() >= 5 && kopf.compare(0, 4, "side") == 0) {
        bool ziffern = true;
        for (size_t i = 4; i < kopf.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(kopf[i]))) ziffern = false;
        if (ziffern) {
            r.volume = std::stoi(kopf.substr(4));
            r.name   = text.substr(schraeg + 1);
        }
    }
    return r;
}

// ─── Aufbau / Abbau ──────────────────────────────────────────────────────────

DiskVolume::~DiskVolume() = default;

std::string DiskVolume::volumeDir(int v) const {
    if (volumes_.size() <= 1) return {};
    return "Side" + std::to_string(v);
}

int DiskVolume::volumeFromDir(const std::string& dir_name) const {
    for (size_t v = 0; v < volumes_.size(); ++v)
        if (kleinbuchstaben(volumeDir(static_cast<int>(v))) == kleinbuchstaben(dir_name))
            return static_cast<int>(v);
    return -1;
}

std::unique_ptr<DiskVolume> DiskVolume::open(const std::string& path,
                                             const std::string& fs_name,
                                             const FormatCatalog& formats,
                                             const FsCatalog& fs_cat,
                                             std::string& err) {
    std::unique_ptr<DiskVolume> dv(new DiskVolume);
    dv->path_ = path;

    const std::string ext   = endung(path);
    const bool        istImg = (ext == "img");

    // ── Fall A: Dateisystem vorgegeben ───────────────────────────────────────
    if (!fs_name.empty()) {
        dv->profile_ = fs_cat.find(fs_name);
        if (!dv->profile_) {
            err = "Dateisystem '" + fs_name + "' steht nicht im Katalog";
            return nullptr;
        }
        dv->format_ = formats.find(dv->profile_->format);
        if (!dv->format_) {
            err = "Format '" + dv->profile_->format + "' steht nicht im Katalog";
            return nullptr;
        }
        if (!dv->profile_->allowsContainer(ext)) {
            err = "Dateisystem '" + fs_name + "' kann nicht als ." + ext + " vorliegen"
                + (dv->profile_->type == FsType::Udos && ext == "img"
                       ? " (der UDOS-Sektorkontrollblock steht hinter der Daten-CRC)" : "");
            return nullptr;
        }
        std::optional<DiskFormat> of;
        if (istImg) of = *dv->format_;
        dv->disk_ = DiskImage::open(path, of, false);
        if (!dv->disk_) { err = "Abbild nicht ladbar: " + path; return nullptr; }
        dv->detection_.format     = dv->format_->name;
        dv->detection_.filesystem = dv->profile_->name;
    } else {
        // ── Fall B: erkennen ─────────────────────────────────────────────────
        // .hfe/.dmk sind selbstbeschreibend; .img braucht fuer JEDEN Versuch die
        // Geometrie — dort kommen nur Formate passender Dateigroesse in Frage.
        std::vector<const DiskFormat*> kandidaten;

        if (istImg) {
            std::error_code ec;
            const uint64_t groesse = fs::file_size(path, ec);
            if (ec) { err = "Datei nicht lesbar: " + path; return nullptr; }
            for (const DiskFormat& f : formats.formats())
                if (f.totalBytes() == groesse) kandidaten.push_back(&f);
            if (kandidaten.empty()) {
                err = "Kein Format in data/formats.yaml hat die Groesse dieses Abbilds ("
                    + std::to_string(groesse) + " Byte).";
                return nullptr;
            }
            // Mit dem ersten Kandidaten oeffnen; die Stufe-2-Probe entscheidet gleich.
            std::optional<DiskFormat> of = *kandidaten.front();
            dv->disk_ = DiskImage::open(path, of, false);
        } else {
            dv->disk_ = DiskImage::open(path, std::nullopt, false);
        }
        if (!dv->disk_) { err = "Abbild nicht ladbar: " + path; return nullptr; }

        if (!istImg) {
            // Stufe 1: Geometrie messen und gegen den Katalog abgleichen.
            const std::vector<MeasuredTrack> gemessen =
                GeometryProbe::measure(dv->disk_->medium());
            const std::vector<GeometryMatch> treffer =
                GeometryProbe::matchAll(gemessen, formats.formats());
            if (treffer.empty()) {
                err = "Das Abbild passt zu keinem Format in data/formats.yaml.\nGemessen:\n"
                    + GeometryProbe::describe(gemessen);
                return nullptr;
            }
            for (const GeometryMatch& m : treffer) kandidaten.push_back(m.format);
            dv->detection_.remarks = treffer.front().remarks();
        }

        // Stufe 2: Dateisysteme der Kandidaten positiv nachweisen.
        std::vector<const FsProfile*> passend;
        for (const DiskFormat* f : kandidaten) {
            for (const FsProfile* p : fs_cat.forFormat(f->name)) {
                if (!p->allowsContainer(ext)) continue;

                SectorSpace probe(dv->disk_->medium(), *f);
                if (p->type == FsType::Udos) {
                    std::string warum;
                    if (!UdosFileSystem::looksLikeUdos(probe, *p, 0, &warum)) continue;
                } else {
                    std::string warum;
                    SectorSpace raum(dv->disk_->medium(), *f);
                    auto cpm = CpmFileSystem::mount(raum, *p, warum);
                    if (!cpm) continue;
                    // Positivprobe: das Verzeichnis muss plausibel sein.
                    int gut = 0, schlecht = 0;
                    for (const CpmDirEntry& d : cpm->directory()) {
                        if (d.free()) { ++gut; continue; }
                        if (d.user > 15) { ++schlecht; continue; }
                        bool ok = !d.name.empty();
                        for (char c : d.name)
                            if (c < 0x20 || c > 0x7E || (c >= 'a' && c <= 'z')) ok = false;
                        for (uint16_t b : d.blocks)
                            if (b >= cpm->totalBlocks()) ok = false;
                        if (d.records > 0x80) ok = false;
                        ok ? ++gut : ++schlecht;
                    }
                    if (schlecht > 0 || gut == 0) continue;
                }
                passend.push_back(p);
                if (!dv->format_) dv->format_ = f;
            }
            if (!passend.empty()) break;      // bestplatzierte Geometrie gewinnt
        }

        if (passend.empty()) {
            err = "Die Geometrie ist erkannt (" + std::string(kandidaten.front()->name)
                + "), aber kein bekanntes Dateisystem liegt darauf. Mit --fs laesst sich "
                  "eines erzwingen.";
            return nullptr;
        }

        dv->profile_ = passend.front();
        dv->detection_.format      = dv->format_->name;
        dv->detection_.filesystem  = dv->profile_->name;
        dv->detection_.unambiguous = (passend.size() == 1);
        for (size_t i = 1; i < passend.size(); ++i)
            dv->detection_.alternatives.push_back(passend[i]->name);
    }

    // ── Volumes aufsetzen ────────────────────────────────────────────────────
    const uint8_t koepfe = dv->disk_->medium().numHeads();

    if (dv->profile_->type == FsType::Udos && dv->profile_->sides_separate) {
        // Jede Seite ist ein eigener Datentraeger (§2).  Eine Seite, die keine
        // gueltige Karte traegt, ist schlicht nicht beschrieben — dann bleibt es
        // bei einem Volume und der Ordner ist flach.
        for (uint8_t h = 0; h < koepfe; ++h) {
            Vol v;
            v.head  = h;
            v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *dv->format_, h);
            std::string warum;
            auto u = UdosFileSystem::mount(*v.space, *dv->profile_, h, warum);
            if (!u) {
                if (h == 0) { err = "Seite 0: " + warum; return nullptr; }
                break;                       // Seite 1 unbeschrieben → einseitig
            }
            v.fs = std::move(u);
            dv->volumes_.push_back(std::move(v));
        }
    } else {
        Vol v;
        v.head  = SectorSpace::kAllHeads;
        v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *dv->format_);
        std::string warum;
        if (dv->profile_->type == FsType::Udos) {
            auto u = UdosFileSystem::mount(*v.space, *dv->profile_, 0, warum);
            if (!u) { err = warum; return nullptr; }
            v.fs = std::move(u);
        } else {
            auto c = CpmFileSystem::mount(*v.space, *dv->profile_, warum);
            if (!c) { err = warum; return nullptr; }
            v.fs = std::move(c);
        }
        dv->volumes_.push_back(std::move(v));
    }

    return dv;
}

// ─── Auskunft ────────────────────────────────────────────────────────────────

FsInfo DiskVolume::volumeInfo(int v) const {
    if (!valid(v)) return {};
    return volumes_[static_cast<size_t>(v)].fs->info();
}

std::vector<FileEntry> DiskVolume::list() const {
    // §9.3: jedes Mal frisch aus dem Medium — kein zwischengespeicherter Stand.
    std::vector<FileEntry> out;
    for (size_t v = 0; v < volumes_.size(); ++v)
        for (FileEntry e : volumes_[v].fs->list()) {
            e.volume = static_cast<int>(v);
            out.push_back(std::move(e));
        }
    return out;
}

// ─── Einzeloperationen ───────────────────────────────────────────────────────

bool DiskVolume::extract(const FileRef& ref, const std::string& dest_path,
                         const TransferOptions& opt) {
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    std::vector<uint8_t> d;
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->read(ref.name, d))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());

    if (opt.text) d = nachLinuxText(d);
    if (opt.dry_run) return true;

    std::error_code ec;
    if (!opt.overwrite && fs::exists(dest_path, ec))
        return fail("Zieldatei existiert bereits: " + dest_path);

    std::string err;
    if (!schreibeDatei(dest_path, d, err)) return fail(err);
    return true;
}

bool DiskVolume::insert(const std::string& src_path, const FileRef& ref,
                        const TransferOptions& opt) {
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    std::vector<uint8_t> d;
    std::string err;
    if (!leseDatei(src_path, d, err)) return fail(err);
    if (opt.text) d = nachDiskettenText(d);
    if (opt.dry_run) return true;

    WriteOptions wo;
    wo.overwrite = opt.overwrite;
    wo.text      = opt.text;
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->write(ref.name, d, wo))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

bool DiskVolume::erase(const FileRef& ref) {
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->erase(ref.name))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

// ─── Stapeloperationen ───────────────────────────────────────────────────────

bool DiskVolume::extractAll(const std::string& dest_dir, const TransferOptions& opt) {
    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) return fail("Zielordner nicht anlegbar: " + dest_dir);

    for (int v = 0; v < volumeCount(); ++v) {
        const std::string unter = volumeDir(v);
        const fs::path    ziel  = unter.empty() ? fs::path(dest_dir)
                                                : fs::path(dest_dir) / unter;
        // Auch eine LEERE Seite bekommt ihren Ordner — das ist die ehrliche Auskunft.
        fs::create_directories(ziel, ec);
        if (ec) return fail("Ordner nicht anlegbar: " + ziel.string());

        for (const FileEntry& e : volumes_[static_cast<size_t>(v)].fs->list()) {
            FileRef r{v, e.qualifiedName()};
            // Nutzerbereich-Praefix ("3:NAME") ist im Dateinamen unbrauchbar.
            std::string datei = e.qualifiedName();
            std::replace(datei.begin(), datei.end(), ':', '_');
            if (!extract(r, (ziel / datei).string(), opt)) return false;
        }
    }
    return true;
}

bool DiskVolume::sammleQuelldateien(const std::string& src_dir,
                                    std::vector<std::vector<std::string>>& je_volume) const {
    je_volume.assign(volumes_.size(), {});

    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) return fail("Kein Ordner: " + src_dir);

    if (volumes_.size() == 1) {
        for (const auto& e : fs::directory_iterator(src_dir, ec)) {
            if (e.is_directory()) continue;         // Unterordner kennt CP/M nicht
            je_volume[0].push_back(e.path().string());
        }
        return true;
    }

    // Mehrere Seiten: der Ordner MUSS genau die SideN/ tragen (§9.1).
    std::vector<std::string> fehlend, lose;
    for (int v = 0; v < volumeCount(); ++v)
        if (!fs::is_directory(fs::path(src_dir) / volumeDir(v), ec))
            fehlend.push_back(volumeDir(v));

    for (const auto& e : fs::directory_iterator(src_dir, ec)) {
        if (!e.is_directory()) { lose.push_back(e.path().filename().string()); continue; }
        if (volumeFromDir(e.path().filename().string()) < 0)
            lose.push_back(e.path().filename().string() + "/");
    }

    if (!fehlend.empty()) {
        std::string liste;
        for (int v = 0; v < volumeCount(); ++v) {
            if (!liste.empty()) liste += " und ";
            liste += volumeDir(v) + "/";
        }
        std::string fehlt;
        for (const auto& f : fehlend) { if (!fehlt.empty()) fehlt += ", "; fehlt += f + "/"; }
        return fail("Der Ordner " + src_dir + " muss die Unterverzeichnisse " + liste
                    + " enthalten (die Diskette hat " + std::to_string(volumeCount())
                    + " Seiten). Es fehlt: " + fehlt);
    }
    if (!lose.empty()) {
        std::string namen;
        for (size_t i = 0; i < lose.size() && i < 5; ++i) {
            if (!namen.empty()) namen += ", ";
            namen += lose[i];
        }
        if (lose.size() > 5) namen += ", …";
        return fail("Neben den SideN-Ordnern liegen weitere Eintraege in " + src_dir
                    + " (" + namen + "). Auf welche Seite sie gehoeren, ist nicht "
                      "erkennbar — bitte einsortieren.");
    }

    for (int v = 0; v < volumeCount(); ++v) {
        const fs::path unter = fs::path(src_dir) / volumeDir(v);
        for (const auto& e : fs::directory_iterator(unter, ec)) {
            if (e.is_directory())
                return fail("Unterverzeichnisse kennt das Dateisystem nicht: "
                            + e.path().string());
            je_volume[static_cast<size_t>(v)].push_back(e.path().string());
        }
    }
    return true;
}

bool DiskVolume::checkFit(const std::string& src_dir, std::string& bericht) const {
    bericht.clear();
    std::vector<std::vector<std::string>> je_volume;
    if (!sammleQuelldateien(src_dir, je_volume)) { bericht = last_error_; return false; }

    bool passt = true;
    for (size_t v = 0; v < volumes_.size(); ++v) {
        std::vector<PlannedFile> plan;
        for (const std::string& p : je_volume[v]) {
            std::error_code ec;
            PlannedFile f;
            f.name   = CpmFileSystem::toCpmName(p);   // UDOS nimmt den Namen unveraendert
            if (volumes_[v].fs && profile_->type == FsType::Udos)
                f.name = fs::path(p).filename().string();
            f.size   = fs::file_size(p, ec);
            f.volume = static_cast<int>(v);
            plan.push_back(f);
        }
        FitReport r;
        if (!volumes_[v].fs->wouldFit(plan, r)) { bericht = volumes_[v].fs->lastError();
                                                  return false; }
        if (!r.fits) {
            passt = false;
            const std::string wo = volumeDir(static_cast<int>(v));
            bericht += (wo.empty() ? std::string("Diskette") : "Seite " + std::to_string(v))
                     + ": " + r.detail + "\n";
        }
    }
    if (passt) bericht = "passt";
    return passt;
}

bool DiskVolume::insertAll(const std::string& src_dir, const TransferOptions& opt) {
    std::vector<std::vector<std::string>> je_volume;
    if (!sammleQuelldateien(src_dir, je_volume)) return false;

    // Schritt 2: urteilen — VOR der ersten Aenderung.
    std::string bericht;
    if (!checkFit(src_dir, bericht))
        return fail(bericht + "Es wurde nichts geschrieben.");
    if (opt.dry_run) return true;

    // Schritt 3: ausfuehren, mit Ruecknahme.  Die Momentaufnahme des Mediums kostet
    // ~1 MB je Diskette — billig gegenueber einer halb beschriebenen Diskette.
    const DiskMedium sicherung = disk_->medium();

    for (size_t v = 0; v < volumes_.size(); ++v) {
        for (const std::string& quelle : je_volume[v]) {
            const std::string name = profile_->type == FsType::Udos
                                   ? fs::path(quelle).filename().string()
                                   : CpmFileSystem::toCpmName(quelle);
            TransferOptions o = opt;
            o.overwrite = true;          // im Stapel ersetzt der Ordner den Bestand
            if (!insert(quelle, FileRef{static_cast<int>(v), name}, o)) {
                const std::string grund = last_error_;
                disk_->medium() = sicherung;         // Ruecknahme
                return fail(grund + " — die Diskette wurde nicht veraendert.");
            }
        }
    }
    return true;
}

// ─── Dateibindung ────────────────────────────────────────────────────────────

bool DiskVolume::dirty() const { return disk_ && disk_->medium().dirty(); }

bool DiskVolume::flush() {
    if (!disk_) return fail("keine Diskette geoeffnet");
    if (!disk_->flush()) return fail(disk_->lastError());
    return true;
}

bool DiskVolume::saveAs(const std::string& path) {
    if (!disk_) return fail("keine Diskette geoeffnet");
    std::optional<DiskFormat> of;
    if (endung(path) == "img") {
        if (profile_ && !profile_->allow_img)
            return fail("Dieses Dateisystem kann nicht als .img gespeichert werden "
                        "(der UDOS-Sektorkontrollblock steht hinter der Daten-CRC).");
        if (format_) of = *format_;
    }
    if (!disk_->saveAs(path, of)) return fail(disk_->lastError());
    path_ = path;
    return true;
}
