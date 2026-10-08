/**
 * @file wega_platte.cpp
 * @brief P8000-Plattenabbild mit WEGA-Partitionen — Umsetzung (Entwurf 27 §1.1).
 */

#include "core/filesystem/wega/wega_platte.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

const std::vector<WegaPlatte::Partition>& WegaPlatte::standardTabelle() {
    // uts/h/mdsize.h: USR 13000, SWAP 3000, ROOT 7000, TMP 4000, Z 60732;
    // uts/conf/wpar.c: O_SWAP = USR, O_ROOT = USR+SWAP, O_TMP = …, O_Z = ….
    static const std::vector<Partition> t = {
        {0,     0, 13000, "/usr"},
        {1, 13000,  3000, "swap"},
        {2, 16000,  7000, "/"},
        {3, 23000,  4000, "/tmp"},
        {4, 27000, 60732, "/z"},
    };
    return t;
}

namespace {
bool parGueltig(const uint8_t* s, int& zyl, int& koepfe, int& sek, std::string& typ,
                std::string* why) {
    if (std::memcmp(s, "DEFEKT", 6) != 0 || std::memcmp(s + 256, "PARMTR", 6) != 0) {
        if (why) *why = "Z0/K0/S1 traegt keinen PAR-Sektor (DEFEKT/PARMTR)";
        return false;
    }
    zyl    = s[277] | s[278] << 8;
    koepfe = s[279];
    sek    = s[280];
    typ.assign(reinterpret_cast<const char*>(s + 262), 12);
    while (!typ.empty() && (typ.back() == '\0' || typ.back() == ' ')) typ.pop_back();
    if (zyl < 100 || zyl > 2999 || koepfe < 2 || koepfe > 16 || (sek != 17 && sek != 18)) {
        if (why) *why = "PAR-Werte ausserhalb der Firmwaregrenzen";
        return false;
    }
    return true;
}
} // namespace

bool WegaPlatte::istPlatte(const std::string& pfad, std::string* why) {
    std::ifstream f(pfad, std::ios::binary);
    if (!f) { if (why) *why = "nicht lesbar"; return false; }
    uint8_t s[512] = {};
    if (!f.read(reinterpret_cast<char*>(s), 512)) { if (why) *why = "kuerzer als ein Sektor"; return false; }
    int z, k, n; std::string t;
    return parGueltig(s, z, k, n, t, why);
}

int64_t WegaPlatte::offsetVon(uint32_t block) const {
    if (koepfe_ <= 0 || sek_ <= 0) return -1;
    // Block → Spur ab Zylinder 1 (Zylinder 0 = PAR/BTT), dann Spurschlupf der BTT
    const uint64_t spur_im_blockraum = block / static_cast<uint32_t>(sek_);
    const uint32_t sektor = block % static_cast<uint32_t>(sek_);
    uint64_t spur = spur_im_blockraum + static_cast<uint64_t>(koepfe_);
    for (uint32_t defekt : btt_)
        if (defekt <= spur) ++spur;
    const uint64_t off = (spur * static_cast<uint64_t>(sek_) + sektor) * 512u;
    if (off + 512 > buf_.size()) return -1;
    return static_cast<int64_t>(off);
}

std::unique_ptr<WegaPlatte> WegaPlatte::open(const std::string& pfad, std::string& err,
                                             bool read_only) {
    std::unique_ptr<WegaPlatte> p(new WegaPlatte);
    p->pfad_ = pfad;
    p->read_only_ = read_only;
    {
        std::ifstream f(pfad, std::ios::binary);
        if (!f) { err = "nicht lesbar: " + pfad; return nullptr; }
        p->buf_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (p->buf_.size() < 512 * 4) { err = "zu klein fuer ein Plattenabbild"; return nullptr; }
    std::string why;
    if (!parGueltig(p->buf_.data(), p->zyl_, p->koepfe_, p->sek_, p->typ_, &why)) {
        err = why;
        return nullptr;
    }
    // BTT: Anzahl Bytes LSB zuerst @6, dann je 3 Byte Zylinder (BE) + Kopf, Ende FF FF FF
    const uint8_t* s = p->buf_.data();
    const int n = s[6] | s[7] << 8;
    for (int i = 0; i + 3 <= n && i < 120; i += 3) {
        const uint8_t* e = s + 8 + i;
        if (e[0] == 0xFF && e[1] == 0xFF && e[2] == 0xFF) break;
        p->btt_.push_back(static_cast<uint32_t>((e[0] << 8 | e[1]) * p->koepfe_ + e[2]));
    }
    std::sort(p->btt_.begin(), p->btt_.end());

    WegaPlatte* roh = p.get();
    std::string gruende;
    for (const Partition& t : standardTabelle()) {
        if (t.rolle == "swap") continue;
        auto dev = std::make_unique<WegaSpeicherDev>(
            p->buf_, t.bloecke,
            [roh, t](uint32_t bn) { return roh->offsetVon(t.start + bn); },
            [roh] { roh->dirty_ = true; });
        // Schreibschutz eine Ebene tiefer: das Geraet selbst weist ab.
        struct Geschuetzt : WegaBlockDev {
            std::unique_ptr<WegaSpeicherDev> d; const WegaPlatte* pl;
            uint32_t blocks() const override { return d->blocks(); }
            bool readBlock(uint32_t b, uint8_t* x) const override { return d->readBlock(b, x); }
            bool writeBlock(uint32_t b, const uint8_t* x) override {
                return !pl->readOnly() && d->writeBlock(b, x);
            }
        };
        auto g = std::make_unique<Geschuetzt>();
        g->d = std::move(dev);
        g->pl = roh;
        // Passt die Partition ueberhaupt auf diese Platte?
        if (p->offsetVon(t.start + t.bloecke - 1) < 0) {
            gruende += " md" + std::to_string(t.md) + ": ausserhalb der Platte;";
            continue;
        }
        auto wfs = WegaFileSystem::mount(std::move(g), why);
        if (!wfs) { gruende += " md" + std::to_string(t.md) + ": " + why + ";"; continue; }
        p->vols_.push_back({t, std::move(wfs)});
    }
    if (p->vols_.empty()) {
        err = "Plattenabbild (" + p->typ_ + ") ohne WEGA-Dateisystem in der Standardtabelle:"
            + gruende;
        return nullptr;
    }
    return p;
}

int WegaPlatte::volumeFromDir(const std::string& name) const {
    std::string n = name;
    if (n.rfind("md", 0) == 0) n = n.substr(2);
    for (size_t v = 0; v < vols_.size(); ++v)
        if (std::to_string(vols_[v].p.md) == n) return static_cast<int>(v);
    return -1;
}

bool WegaPlatte::flush() {
    if (!dirty_) return true;
    if (read_only_) { err_ = "Das Plattenabbild ist schreibgeschuetzt"; return false; }
    std::error_code ec;
    if (backup_ && !backup_getan_ && fs::exists(pfad_, ec)) {
        fs::copy_file(pfad_, pfad_ + "~", fs::copy_options::overwrite_existing, ec);
        if (ec) { err_ = "Sicherungskopie " + pfad_ + "~ nicht anlegbar: " + ec.message(); return false; }
        backup_getan_ = true;
    }
    std::ofstream f(pfad_, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char*>(buf_.data()), static_cast<std::streamsize>(buf_.size()))) {
        err_ = "nicht schreibbar: " + pfad_;
        return false;
    }
    dirty_ = false;
    return true;
}
