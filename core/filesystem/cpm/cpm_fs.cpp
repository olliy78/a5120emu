/**
 * @file cpm_fs.cpp
 * @brief Umsetzung von @ref CpmFileSystem — Lesepfad.
 *
 * @see doc/design/13_k1520disktool.md §7
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/cpm/cpm_fs.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace {

/// @brief Ein logischer CP/M-Extent fasst immer 128 Saetze à 128 B = 16 KB.
constexpr uint32_t kExtentBytes  = 16384;
constexpr uint32_t kRecordBytes  = 128;

/// @brief "NAME    TYP" → "NAME.TYP" (Hochbits sind Attribute und gehoeren nicht zum Namen).
std::string entryName(const uint8_t* p) {
    std::string name, typ;
    for (int i = 0; i < 8;  ++i) name += static_cast<char>(p[1 + i] & 0x7F);
    for (int i = 0; i < 3;  ++i) typ  += static_cast<char>(p[9 + i] & 0x7F);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!typ.empty()  && typ.back()  == ' ') typ.pop_back();
    return typ.empty() ? name : name + "." + typ;
}

/// @brief Grossschreibung fuer den Namensvergleich (CP/M kennt keine Kleinbuchstaben).
std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

/// @brief "3:NAME.TYP" aufspalten; ohne Praefix ist es Nutzerbereich 0.
void splitUser(const std::string& in, int& user, std::string& name) {
    user = 0;
    name = in;
    const size_t k = in.find(':');
    if (k == std::string::npos || k == 0 || k > 2) return;
    bool nur_ziffern = true;
    for (size_t i = 0; i < k; ++i)
        if (!std::isdigit(static_cast<unsigned char>(in[i]))) nur_ziffern = false;
    if (!nur_ziffern) return;
    user = std::stoi(in.substr(0, k));
    name = in.substr(k + 1);
}

}  // namespace

// ─── Aufsetzen ───────────────────────────────────────────────────────────────

CpmFileSystem::CpmFileSystem(SectorSpace& space, const FsProfile& prof)
    : space_(space), prof_(prof) {}

std::unique_ptr<CpmFileSystem> CpmFileSystem::mount(SectorSpace& space,
                                                    const FsProfile& prof,
                                                    std::string& err) {
    if (prof.type != FsType::Cpm) {
        err = "Profil '" + prof.name + "' ist kein CP/M-Dateisystem";
        return nullptr;
    }

    const int start = space.trackIndexOf(prof.data_cyl, prof.data_head);
    if (start < 0) {
        err = "data_start c" + std::to_string(prof.data_cyl) + "h"
            + std::to_string(prof.data_head) + " liegt nicht im Sektorraum";
        return nullptr;
    }

    std::unique_ptr<CpmFileSystem> fs(new CpmFileSystem(space, prof));

    // Der Datenbereich muss einheitlich sein — CP/M rechnet mit EINER Sektorgroesse
    // je Diskette.  Die gemischte Geometrie einer Bootdiskette liegt vollstaendig
    // VOR data_start (cpa780: drei 128-B-Seiten + eine 1024-B-Spur).
    const SectorSpace::TrackRef erste = space.trackAt(static_cast<size_t>(start));
    fs->secs_per_track_ = erste.sectors;
    fs->sector_size_    = erste.sector_size;

    for (size_t i = static_cast<size_t>(start); i < space.trackCount(); ++i) {
        const SectorSpace::TrackRef t = space.trackAt(i);
        if (t.sectors != fs->secs_per_track_ || t.sector_size != fs->sector_size_) {
            err = "Datenbereich ist nicht einheitlich: Spur c" + std::to_string(t.cyl)
                + "h" + std::to_string(t.head) + " hat " + std::to_string(t.sectors)
                + "×" + std::to_string(t.sector_size) + ", erwartet "
                + std::to_string(fs->secs_per_track_) + "×"
                + std::to_string(fs->sector_size_);
            return nullptr;
        }
        fs->data_bytes_ += t.bytes;
    }

    fs->data_start_   = erste.start;
    fs->total_blocks_ = static_cast<uint32_t>(fs->data_bytes_ / prof.block_size);
    if (fs->total_blocks_ == 0) {
        err = "Datenbereich ist kleiner als ein Block";
        return nullptr;
    }

    // Ab 256 Bloecken sind die Blockzeiger 16 Bit breit — das ergibt sich aus der
    // Kapazitaet und steht deshalb NICHT im Profil (so haelt es auch CP/M selbst).
    fs->wide_ptr_       = (fs->total_blocks_ > 255);
    fs->ptrs_per_entry_ = fs->wide_ptr_ ? 8 : 16;
    fs->ext_per_entry_  = std::max<uint32_t>(
        1, (fs->ptrs_per_entry_ * prof.block_size) / kExtentBytes);

    const uint32_t dir_bytes = static_cast<uint32_t>(prof.dir_entries) * 32;
    fs->dir_blocks_ = (dir_bytes + prof.block_size - 1) / prof.block_size;
    if (fs->dir_blocks_ >= fs->total_blocks_) {
        err = "Verzeichnis (" + std::to_string(prof.dir_entries)
            + " Eintraege) passt nicht in den Datenbereich";
        return nullptr;
    }

    fs->buildSkewTable();
    return fs;
}

void CpmFileSystem::buildSkewTable() {
    const uint16_t n = secs_per_track_;
    skew_tab_.assign(n, 0);
    if (prof_.skew == 0) {
        for (uint16_t i = 0; i < n; ++i) skew_tab_[i] = static_cast<uint8_t>(i);
        return;
    }
    // Klassische CP/M-Versatztabelle (wie in cpmtools): jeder Schritt springt um
    // `skew` weiter, belegte Plaetze werden uebersprungen.
    std::vector<bool> belegt(n, false);
    uint16_t j = 0;
    for (uint16_t i = 0; i < n; ++i) {
        while (belegt[j]) j = static_cast<uint16_t>((j + 1) % n);
        skew_tab_[i] = static_cast<uint8_t>(j);
        belegt[j] = true;
        j = static_cast<uint16_t>((j + prof_.skew) % n);
    }
}

// ─── Lesen ───────────────────────────────────────────────────────────────────

bool CpmFileSystem::readAt(uint64_t offset, uint8_t* dst, size_t n) const {
    const int start = space_.trackIndexOf(prof_.data_cyl, prof_.data_head);
    if (start < 0) return fail("Datenbereich nicht auffindbar");

    const uint64_t track_bytes = static_cast<uint64_t>(secs_per_track_) * sector_size_;
    SectorData sec;

    while (n > 0) {
        if (offset + 1 > data_bytes_) return fail("Lesen ueber das Dateisystemende hinaus");

        const uint64_t ti    = offset / track_bytes;
        const uint64_t rest  = offset % track_bytes;
        const uint16_t lsec  = static_cast<uint16_t>(rest / sector_size_);
        const size_t   insec = static_cast<size_t>(rest % sector_size_);

        const SectorSpace::TrackRef t =
            space_.trackAt(static_cast<size_t>(start) + static_cast<size_t>(ti));
        const uint8_t id = static_cast<uint8_t>(t.first_id + skew_tab_[lsec]);

        if (!space_.readSector(t.cyl, t.head, id, sec))
            return fail(space_.lastError());
        if (sec.data.size() < sector_size_)
            return fail("Sektor " + std::to_string(id) + " auf Spur "
                        + std::to_string(t.cyl) + "/" + std::to_string(t.head)
                        + " ist zu kurz");

        const size_t chunk = std::min(n, static_cast<size_t>(sector_size_) - insec);
        std::copy(sec.data.begin() + static_cast<long>(insec),
                  sec.data.begin() + static_cast<long>(insec + chunk), dst);
        dst    += chunk;
        offset += chunk;
        n      -= chunk;
    }
    return true;
}

bool CpmFileSystem::readBlock(uint16_t block, std::vector<uint8_t>& out) const {
    if (block >= total_blocks_)
        return fail("Blocknummer " + std::to_string(block) + " liegt hinter dem Datenbereich ("
                    + std::to_string(total_blocks_) + " Bloecke)");
    const size_t alt = out.size();
    out.resize(alt + prof_.block_size);
    return readAt(static_cast<uint64_t>(block) * prof_.block_size, out.data() + alt,
                  prof_.block_size);
}

bool CpmFileSystem::readDirectory(std::vector<uint8_t>& out) const {
    out.assign(static_cast<size_t>(prof_.dir_entries) * 32, 0xE5);
    return readAt(0, out.data(), out.size());
}

// ─── Verzeichnis ─────────────────────────────────────────────────────────────

std::vector<CpmDirEntry> CpmFileSystem::directory() const {
    std::vector<CpmDirEntry> result;
    std::vector<uint8_t> roh;
    if (!readDirectory(roh)) return result;

    for (int i = 0; i < prof_.dir_entries; ++i) {
        const uint8_t* p = roh.data() + static_cast<size_t>(i) * 32;

        CpmDirEntry e;
        e.index = i;
        e.user  = p[0];
        if (e.user == 0xE5) { result.push_back(e); continue; }

        e.name      = entryName(p);
        e.extent    = (p[12] & 0x1F) + (p[14] & 0x3F) * 32;
        e.records   = p[15];
        e.read_only = (p[ 9] & 0x80) != 0;
        e.system    = (p[10] & 0x80) != 0;
        e.archived  = (p[11] & 0x80) != 0;

        if (wide_ptr_)
            for (int k = 0; k < 8; ++k)
                e.blocks.push_back(static_cast<uint16_t>(p[16 + 2 * k] | (p[17 + 2 * k] << 8)));
        else
            for (int k = 0; k < 16; ++k) e.blocks.push_back(p[16 + k]);

        result.push_back(e);
    }
    return result;
}

std::vector<FileEntry> CpmFileSystem::list() const {
    // Extents derselben Datei zusammenfassen: Schluessel ist (Nutzerbereich, Name).
    struct Sammler {
        FileEntry ent;
        int       max_extent = -1;
        uint8_t   rc_of_max  = 0;
    };
    std::map<std::pair<int, std::string>, Sammler> nach_datei;

    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;      // frei oder Passwort-/Etikett-Satz
        auto& s = nach_datei[{d.user, d.name}];
        if (s.ent.name.empty()) {
            s.ent.name       = d.name;
            s.ent.user       = d.user;
            s.ent.hidden     = d.system;
            s.ent.attributes = std::string(d.read_only ? "RO " : "")
                             + (d.system   ? "SYS " : "")
                             + (d.archived ? "ARC"  : "");
            while (!s.ent.attributes.empty() && s.ent.attributes.back() == ' ')
                s.ent.attributes.pop_back();
        }
        if (d.extent > s.max_extent) { s.max_extent = d.extent; s.rc_of_max = d.records; }
    }

    std::vector<FileEntry> out;
    for (auto& [schluessel, s] : nach_datei) {
        // Klassische CP/M-2.2-Groessenrechnung: der hoechste Extent bestimmt die Laenge.
        s.ent.size = static_cast<uint64_t>(s.max_extent) * kExtentBytes
                   + static_cast<uint64_t>(s.rc_of_max)  * kRecordBytes;
        out.push_back(s.ent);
        (void)schluessel;
    }
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.user != b.user) return a.user < b.user;
        return a.name < b.name;
    });
    return out;
}

bool CpmFileSystem::read(const std::string& name, std::vector<uint8_t>& out) {
    int gesucht_user = 0;
    std::string gesucht_name;
    splitUser(name, gesucht_user, gesucht_name);
    gesucht_name = upper(gesucht_name);

    // Alle Extents der Datei einsammeln, nach Extent-Nummer sortiert.
    std::vector<CpmDirEntry> extents;
    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;
        if (d.user != gesucht_user || upper(d.name) != gesucht_name) continue;
        extents.push_back(d);
    }
    if (extents.empty())
        return fail("Datei '" + name + "' steht nicht im Verzeichnis");

    std::sort(extents.begin(), extents.end(),
              [](const CpmDirEntry& a, const CpmDirEntry& b) { return a.extent < b.extent; });

    int      max_extent = extents.back().extent;
    uint8_t  rc_of_max  = extents.back().records;
    const uint64_t laenge = static_cast<uint64_t>(max_extent) * kExtentBytes
                          + static_cast<uint64_t>(rc_of_max)  * kRecordBytes;

    out.clear();
    out.reserve(static_cast<size_t>(laenge));

    for (const CpmDirEntry& d : extents) {
        // Ein Verzeichnisplatz deckt ext_per_entry_ logische Extents ab; seine Bloecke
        // beginnen beim ERSTEN davon.
        const uint64_t start = static_cast<uint64_t>(d.extent & ~(ext_per_entry_ - 1))
                             * kExtentBytes;
        if (out.size() < start) out.resize(static_cast<size_t>(start), 0x00);

        for (uint16_t b : d.blocks) {
            if (b == 0) {                       // Loch (sparse) — CP/M nutzt Block 0 nie
                out.resize(out.size() + prof_.block_size, 0x00);
                continue;
            }
            if (!readBlock(b, out)) return false;
        }
    }

    if (out.size() > laenge) out.resize(static_cast<size_t>(laenge));
    return true;
}

// ─── Zustand ─────────────────────────────────────────────────────────────────

std::vector<bool> CpmFileSystem::allocationMap() const {
    std::vector<bool> karte(total_blocks_, false);
    for (uint32_t b = 0; b < dir_blocks_; ++b) karte[b] = true;   // Verzeichnis
    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;
        for (uint16_t b : d.blocks)
            if (b != 0 && b < total_blocks_) karte[b] = true;
    }
    return karte;
}

FsInfo CpmFileSystem::info() const {
    FsInfo i;
    const std::vector<bool> karte = allocationMap();
    uint32_t belegt = 0;
    for (bool b : karte) if (b) ++belegt;

    i.total_bytes = static_cast<uint64_t>(total_blocks_ - dir_blocks_) * prof_.block_size;
    i.used_bytes  = static_cast<uint64_t>(belegt - dir_blocks_) * prof_.block_size;
    i.free_bytes  = static_cast<uint64_t>(total_blocks_ - belegt) * prof_.block_size;
    i.files       = static_cast<int>(list().size());

    // Blockzeiger, die hinter den Datenbereich zeigen, sind ein sicheres Zeichen fuer
    // ein falsch gewaehltes Profil — genau deshalb gehoeren sie in den Bericht.
    int wild = 0;
    for (const CpmDirEntry& d : directory()) {
        if (d.free() || d.user > 15) continue;
        for (uint16_t b : d.blocks) if (b >= total_blocks_) ++wild;
    }
    if (wild)
        i.warnings.push_back(std::to_string(wild) + " Blockzeiger zeigen hinter den "
                             "Datenbereich — vermutlich das falsche Dateisystemprofil");
    return i;
}
