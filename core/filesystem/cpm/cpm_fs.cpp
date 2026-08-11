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

int CpmFileSystem::directoryFill() const {
    std::vector<uint8_t> roh;
    if (!readDirectory(roh) || roh.empty()) return -1;
    for (uint8_t b : roh)
        if (b != roh[0]) return -1;
    return roh[0];
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

// ─── Schreiben ───────────────────────────────────────────────────────────────

bool CpmFileSystem::writeAt(uint64_t offset, const uint8_t* src, size_t n) {
    const int start = space_.trackIndexOf(prof_.data_cyl, prof_.data_head);
    if (start < 0) return fail("Datenbereich nicht auffindbar");

    const uint64_t track_bytes = static_cast<uint64_t>(secs_per_track_) * sector_size_;
    SectorData sec;

    while (n > 0) {
        if (offset + 1 > data_bytes_) return fail("Schreiben ueber das Dateisystemende hinaus");

        const uint64_t ti    = offset / track_bytes;
        const uint64_t rest  = offset % track_bytes;
        const uint16_t lsec  = static_cast<uint16_t>(rest / sector_size_);
        const size_t   insec = static_cast<size_t>(rest % sector_size_);

        const SectorSpace::TrackRef t =
            space_.trackAt(static_cast<size_t>(start) + static_cast<size_t>(ti));
        const uint8_t id = static_cast<uint8_t>(t.first_id + skew_tab_[lsec]);

        // Teilsektor → lesen, aendern, zurueckschreiben.  Der Nachspann hinter der
        // Daten-CRC bleibt dabei unangetastet (leeres tail-Argument).
        if (!space_.readSector(t.cyl, t.head, id, sec)) return fail(space_.lastError());
        if (sec.data.size() < sector_size_)
            return fail("Sektor " + std::to_string(id) + " auf Spur "
                        + std::to_string(t.cyl) + "/" + std::to_string(t.head)
                        + " ist zu kurz");

        const size_t chunk = std::min(n, static_cast<size_t>(sector_size_) - insec);
        std::copy(src, src + chunk, sec.data.begin() + static_cast<long>(insec));
        if (!space_.writeSector(t.cyl, t.head, id, sec.data)) return fail(space_.lastError());

        src    += chunk;
        offset += chunk;
        n      -= chunk;
    }
    return true;
}

bool CpmFileSystem::writeDirEntry(int slot, const uint8_t* raw32) {
    if (slot < 0 || slot >= prof_.dir_entries) return fail("Verzeichnisplatz ausserhalb");
    return writeAt(static_cast<uint64_t>(slot) * 32, raw32, 32);
}

bool CpmFileSystem::validName(const std::string& name, std::string* why) {
    auto sag = [&](const std::string& m) { if (why) *why = m; return false; };

    const size_t punkt = name.find('.');
    const std::string basis = punkt == std::string::npos ? name : name.substr(0, punkt);
    const std::string typ   = punkt == std::string::npos ? "" : name.substr(punkt + 1);

    if (basis.empty())    return sag("Name ist leer");
    if (basis.size() > 8) return sag("Name '" + basis + "' ist laenger als 8 Zeichen");
    if (typ.size()   > 3) return sag("Typ '" + typ + "' ist laenger als 3 Zeichen");
    if (typ.find('.') != std::string::npos)
        return sag("CP/M kennt nur EINEN Punkt im Namen");

    // CP/M-Trennzeichen (siehe CP/M 2.2 Interface Guide) sind im Namen verboten.
    // Das LEERZEICHEN steht mit in der Liste: CP/M fuellt Name und Typ damit auf,
    // ein Name mit Leerzeichen ist also im Verzeichnis nicht mehr eindeutig
    // wiederzufinden und am CCP nicht eingebbar.
    static const std::string verboten = " <>.,;:=?*[]%|()/\\";
    for (char c : basis + typ) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) return sag("Name enthaelt ein Sonderzeichen");
        if (verboten.find(c) != std::string::npos)
            return sag(std::string("Zeichen '") + c + "' ist in CP/M-Namen nicht erlaubt");
        if (c >= 'a' && c <= 'z') return sag("CP/M-Namen sind grossgeschrieben");
    }
    return true;
}

std::string CpmFileSystem::toCpmName(const std::string& linux_name) {
    // Nur der Dateiname, ohne Pfad.
    std::string n = linux_name;
    const size_t schraeg = n.find_last_of("/\\");
    if (schraeg != std::string::npos) n = n.substr(schraeg + 1);

    const size_t punkt = n.find_last_of('.');
    std::string basis = punkt == std::string::npos ? n : n.substr(0, punkt);
    std::string typ   = punkt == std::string::npos ? "" : n.substr(punkt + 1);

    auto saeubere = [](std::string s, size_t max) {
        std::string o;
        for (char c : s) {
            const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            static const std::string verboten = " <>.,;:=?*[]%|()/\\";
            if (u < 0x20 || u > 0x7E || verboten.find(u) != std::string::npos) continue;
            o += u;
            if (o.size() == max) break;
        }
        return o;
    };
    basis = saeubere(basis, 8);
    typ   = saeubere(typ, 3);
    if (basis.empty()) basis = "UNNAMED";
    return typ.empty() ? basis : basis + "." + typ;
}

bool CpmFileSystem::erase(const std::string& name) {
    int gesucht_user = 0;
    std::string gesucht_name;
    splitUser(name, gesucht_user, gesucht_name);
    gesucht_name = upper(gesucht_name);

    std::vector<uint8_t> roh;
    if (!readDirectory(roh)) return false;

    bool etwas = false;
    for (int i = 0; i < prof_.dir_entries; ++i) {
        uint8_t* p = roh.data() + static_cast<size_t>(i) * 32;
        if (p[0] == 0xE5 || p[0] > 15) continue;
        if (p[0] != gesucht_user || upper(entryName(p)) != gesucht_name) continue;

        // CP/M loescht nur das Nutzerbyte — die Blockliste bleibt stehen und wird
        // beim naechsten Aufbau der Belegungskarte einfach nicht mehr gezaehlt.
        p[0] = 0xE5;
        if (!writeDirEntry(i, p)) return false;
        etwas = true;
    }
    if (!etwas) return fail("Datei '" + name + "' steht nicht im Verzeichnis");
    return true;
}

bool CpmFileSystem::write(const std::string& name, const std::vector<uint8_t>& data,
                          const WriteOptions& opt) {
    int ziel_user = 0;
    std::string ziel_name;
    splitUser(name, ziel_user, ziel_name);
    ziel_name = upper(ziel_name);

    std::string warum;
    if (!validName(ziel_name, &warum)) return fail("'" + ziel_name + "': " + warum);
    if (ziel_user < 0 || ziel_user > 15) return fail("Nutzerbereich muss 0..15 sein");

    // Vorhandene gleichnamige Datei?
    bool vorhanden = false;
    for (const CpmDirEntry& d : directory())
        if (!d.free() && d.user == ziel_user && upper(d.name) == ziel_name) vorhanden = true;
    if (vorhanden) {
        if (!opt.overwrite)
            return fail("'" + name + "' existiert bereits — Ueberschreiben nicht erlaubt");
        if (!erase(ziel_user == 0 ? ziel_name : std::to_string(ziel_user) + ":" + ziel_name))
            return false;
    }

    // Auf ganze Saetze auffuellen: Textdateien mit 0x1A (CP/M-Dateiende), sonst 0x00.
    std::vector<uint8_t> inhalt = data;
    const uint8_t fuell = opt.text ? 0x1A : 0x00;
    if (inhalt.size() % kRecordBytes)
        inhalt.resize((inhalt.size() / kRecordBytes + 1) * kRecordBytes, fuell);
    const uint32_t saetze = static_cast<uint32_t>(inhalt.size() / kRecordBytes);
    if (saetze == 0) return fail("leere Dateien kann CP/M nicht ablegen");

    const uint32_t bloecke_noetig =
        static_cast<uint32_t>((inhalt.size() + prof_.block_size - 1) / prof_.block_size);

    // Freie Bloecke sammeln (First Fit, aufsteigend).
    std::vector<bool> karte = allocationMap();
    std::vector<uint16_t> frei;
    for (uint32_t b = dir_blocks_; b < total_blocks_ && frei.size() < bloecke_noetig; ++b)
        if (!karte[b]) frei.push_back(static_cast<uint16_t>(b));
    if (frei.size() < bloecke_noetig) {
        uint32_t gesamt_frei = 0;
        for (uint32_t b = dir_blocks_; b < total_blocks_; ++b) if (!karte[b]) ++gesamt_frei;
        return fail("Diskette voll: '" + ziel_name + "' braucht "
                    + std::to_string(bloecke_noetig) + " Bloecke, frei sind "
                    + std::to_string(gesamt_frei));
    }

    // Freie Verzeichnisplaetze sammeln.
    const uint32_t plaetze_noetig =
        (bloecke_noetig + ptrs_per_entry_ - 1) / ptrs_per_entry_;
    std::vector<int> slots;
    {
        std::vector<uint8_t> roh;
        if (!readDirectory(roh)) return false;
        for (int i = 0; i < prof_.dir_entries && slots.size() < plaetze_noetig; ++i)
            if (roh[static_cast<size_t>(i) * 32] == 0xE5) slots.push_back(i);
    }
    if (slots.size() < plaetze_noetig)
        return fail("Verzeichnis voll: '" + ziel_name + "' braucht "
                    + std::to_string(plaetze_noetig) + " Eintraege");

    // ── Daten schreiben ──────────────────────────────────────────────────────
    for (uint32_t i = 0; i < bloecke_noetig; ++i) {
        const size_t ab   = static_cast<size_t>(i) * prof_.block_size;
        const size_t menge= std::min<size_t>(prof_.block_size, inhalt.size() - ab);
        std::vector<uint8_t> block(prof_.block_size, fuell);
        std::copy(inhalt.begin() + static_cast<long>(ab),
                  inhalt.begin() + static_cast<long>(ab + menge), block.begin());
        if (!writeAt(static_cast<uint64_t>(frei[i]) * prof_.block_size,
                     block.data(), block.size()))
            return false;
    }

    // ── Verzeichniseintraege schreiben ───────────────────────────────────────
    uint32_t rest_saetze = saetze;
    for (uint32_t g = 0; g < plaetze_noetig; ++g) {
        uint8_t e[32];
        std::fill(std::begin(e), std::end(e), static_cast<uint8_t>(0x00));

        e[0] = static_cast<uint8_t>(ziel_user);
        const size_t punkt = ziel_name.find('.');
        const std::string basis = punkt == std::string::npos ? ziel_name
                                                             : ziel_name.substr(0, punkt);
        const std::string typ   = punkt == std::string::npos ? "" : ziel_name.substr(punkt + 1);
        for (int i = 0; i < 8; ++i) e[1 + i] = i < (int)basis.size()
                                             ? static_cast<uint8_t>(basis[i]) : ' ';
        for (int i = 0; i < 3; ++i) e[9 + i] = i < (int)typ.size()
                                             ? static_cast<uint8_t>(typ[i]) : ' ';

        // Saetze dieses Verzeichnisplatzes und daraus EX/S2/RC.
        const uint32_t max_hier = 128u * ext_per_entry_;
        const uint32_t hier     = std::min(rest_saetze, max_hier);
        const uint32_t letzter_log_ext = g * ext_per_entry_ + (hier - 1) / 128;
        const uint32_t rc = hier - 128u * ((hier - 1) / 128);

        e[12] = static_cast<uint8_t>(letzter_log_ext & 0x1F);
        e[13] = 0;                                   // S1
        e[14] = static_cast<uint8_t>((letzter_log_ext >> 5) & 0x3F);
        e[15] = static_cast<uint8_t>(rc);

        for (uint32_t k = 0; k < ptrs_per_entry_; ++k) {
            const uint32_t idx = g * ptrs_per_entry_ + k;
            const uint16_t b   = idx < bloecke_noetig ? frei[idx] : 0;
            if (wide_ptr_) {
                e[16 + 2 * k]     = static_cast<uint8_t>(b & 0xFF);
                e[16 + 2 * k + 1] = static_cast<uint8_t>(b >> 8);
            } else {
                e[16 + k] = static_cast<uint8_t>(b);
            }
        }

        if (!writeDirEntry(slots[g], e)) return false;
        rest_saetze -= hier;
    }
    return true;
}

bool CpmFileSystem::wouldFit(const std::vector<PlannedFile>& files, FitReport& out) const {
    out = FitReport{};

    std::vector<bool> karte = allocationMap();
    const std::vector<CpmDirEntry> verz = directory();

    int plaetze_frei = 0;
    for (const CpmDirEntry& d : verz) if (d.free()) ++plaetze_frei;

    // Ersetzte gleichnamige Dateien geben Bloecke UND Verzeichnisplaetze zurueck.
    for (const PlannedFile& f : files) {
        int user = 0;
        std::string name;
        splitUser(f.name, user, name);
        name = upper(name);
        for (const CpmDirEntry& d : verz) {
            if (d.free() || d.user != user || upper(d.name) != name) continue;
            ++plaetze_frei;
            for (uint16_t b : d.blocks)
                if (b >= dir_blocks_ && b < total_blocks_) karte[b] = false;
        }
    }

    uint32_t frei = 0;
    for (uint32_t b = dir_blocks_; b < total_blocks_; ++b) if (!karte[b]) ++frei;

    uint32_t bloecke = 0;
    uint32_t plaetze = 0;
    for (const PlannedFile& f : files) {
        const uint64_t gerundet =
            ((f.size + kRecordBytes - 1) / kRecordBytes) * kRecordBytes;
        const uint32_t b = static_cast<uint32_t>(
            (gerundet + prof_.block_size - 1) / prof_.block_size);
        bloecke += b;
        plaetze += std::max(1u, (b + ptrs_per_entry_ - 1) / ptrs_per_entry_);
    }

    out.needed     = static_cast<uint64_t>(bloecke) * prof_.block_size;
    out.available  = static_cast<uint64_t>(frei)    * prof_.block_size;
    out.dir_needed = static_cast<int>(plaetze);
    out.dir_free   = plaetze_frei;
    out.fits       = (bloecke <= frei) && (static_cast<int>(plaetze) <= plaetze_frei);

    if (bloecke > frei)
        out.detail = std::to_string(files.size()) + " Dateien brauchen "
                   + std::to_string(bloecke) + " Bloecke (" + std::to_string(out.needed / 1024)
                   + " KB), frei sind " + std::to_string(frei) + " ("
                   + std::to_string(out.available / 1024) + " KB)";
    else if (static_cast<int>(plaetze) > plaetze_frei)
        out.detail = "Verzeichnis reicht nicht: " + std::to_string(plaetze)
                   + " Eintraege noetig, " + std::to_string(plaetze_frei) + " frei";
    return true;
}

bool CpmFileSystem::mkfs() {
    // Ein leeres CP/M-Dateisystem ist schlicht ein Verzeichnis voller 0xE5 — der
    // Datenbereich dahinter bleibt unberuehrt (so macht es auch CP/M selbst).
    const std::vector<uint8_t> leer(static_cast<size_t>(prof_.dir_entries) * 32, 0xE5);
    return writeAt(0, leer.data(), leer.size());
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
