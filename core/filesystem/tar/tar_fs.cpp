/**
 * @file tar_fs.cpp
 * @brief Implementierung des schreibgeschuetzten Tar-Archivs auf der Diskette.
 *
 * @see core/filesystem/tar/tar_fs.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/tar/tar_fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace tar {
namespace {

bool nullblock(const uint8_t* b) {
    for (uint32_t i = 0; i < kBlock; ++i) if (b[i]) return false;
    return true;
}

/// Oktalfeld: fuehrende Leerzeichen/Nullen, Ende bei NUL oder Leerzeichen.
bool oktal(const uint8_t* f, size_t n, uint64_t& out) {
    size_t i = 0;
    while (i < n && f[i] == ' ') ++i;
    uint64_t v = 0;
    bool da = false;
    for (; i < n && f[i] >= '0' && f[i] <= '7'; ++i) { v = v * 8 + (f[i] - '0'); da = true; }
    for (; i < n; ++i) if (f[i] != 0 && f[i] != ' ') return false;
    out = v;
    return da;
}

uint64_t oktalOder0(const uint8_t* f, size_t n) {
    uint64_t v = 0;
    return oktal(f, n, v) ? v : 0;
}

std::string text(const uint8_t* f, size_t n) {
    size_t l = 0;
    while (l < n && f[l]) ++l;
    return std::string(reinterpret_cast<const char*>(f), l);
}

bool druckbar(const std::string& s) {
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return false;
    return true;
}

} // namespace

bool kopfLesen(const uint8_t* b, Kopf& k, std::string* warum) {
    auto nein = [&](const char* w) { if (warum) *warum = w; return false; };
    if (nullblock(b)) return nein("Nullblock");
    uint64_t soll = 0;
    if (!oktal(b + 148, 8, soll)) return nein("Pruefsummenfeld ist keine Oktalzahl");
    uint32_t su = 256, ss = 256;     // Feld zaehlt als 8 Leerzeichen; vorzeichenlose / vorzeichenbehaftete Summe
    for (uint32_t i = 0; i < kBlock; ++i) {
        if (i >= 148 && i < 156) continue;
        su += b[i];
        ss += static_cast<uint32_t>(static_cast<int8_t>(b[i]));   // alte tar auf char-signed Maschinen
    }
    if (soll != su && soll != ss) return nein("Kopfpruefsumme stimmt nicht");

    Kopf t;
    t.name = text(b, 100);
    if (std::memcmp(b + 257, "ustar", 5) == 0) {
        const std::string pre = text(b + 345, 155);
        if (!pre.empty()) t.name = pre + "/" + t.name;
    }
    if (t.name.empty() || !druckbar(t.name)) return nein("Name leer oder nicht druckbar");

    if (b[124] & 0x80) {             // GNU: Groesse binaer (base-256)
        uint64_t v = b[124] & 0x7F;
        for (int i = 125; i < 136; ++i) v = (v << 8) | b[i];
        t.size = v;
    } else if (!oktal(b + 124, 12, t.size)) {
        return nein("Groessenfeld ist keine Oktalzahl");
    }
    if (t.size > (1ULL << 34)) return nein("Groesse unplausibel");

    t.typ = b[156] ? static_cast<char>(b[156]) : '0';
    if (!((t.typ >= '0' && t.typ <= '7') || t.typ == 'x' || t.typ == 'g' || t.typ == 'L' || t.typ == 'K'))
        return nein("unbekannter Typ");
    t.mode  = static_cast<uint32_t>(oktalOder0(b + 100, 8));
    t.uid   = static_cast<uint32_t>(oktalOder0(b + 108, 8));
    t.gid   = static_cast<uint32_t>(oktalOder0(b + 116, 8));
    t.mtime = static_cast<uint32_t>(oktalOder0(b + 136, 12));
    t.linkname = text(b + 157, 100);
    k = t;
    return true;
}

} // namespace tar

using tar::kBlock;

bool TarSpaceQuelle::ort(uint64_t off, FsRecoverOrt& out) const {
    for (size_t i = 0; i < s_.trackCount(); ++i) {
        const SectorSpace::TrackRef t = s_.trackAt(i);
        if (off >= t.start && off < t.start + t.bytes && t.sector_size) {
            out.cyl    = t.cyl;
            out.head   = t.head;
            out.sector = t.first_id + static_cast<int>((off - t.start) / t.sector_size);
            return true;
        }
    }
    return false;
}

bool TarSpeicherQuelle::read(uint64_t off, uint8_t* dst, size_t n) const {
    if (off + n > d_.size()) return false;
    std::memcpy(dst, d_.data() + off, n);
    return true;
}

namespace {

/// Pfad bereinigen: kein fuehrendes '/', keine "." und leeren Teile, ".." wird "__".
std::string bereinige(const std::string& roh, bool& geaendert) {
    geaendert = !roh.empty() && roh[0] == '/';
    std::string out, t;
    for (char c : roh + "/") {
        if (c != '/') { t += c; continue; }
        if (t == "..") { t = "__"; geaendert = true; }
        if (!t.empty() && t != ".") out += (out.empty() ? "" : "/") + t;
        t.clear();
    }
    return out;
}

std::string datumText(uint32_t t) {
    if (t == 0) return {};
    const int64_t tage = t / 86400, rest = t % 86400;
    int64_t z = tage + 719468;
    const int64_t era = z / 146097, doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) ++y;
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", static_cast<int>(y), static_cast<int>(m),
                  static_cast<int>(d), static_cast<int>(rest / 3600), static_cast<int>(rest % 3600 / 60));
    return b;
}

std::string modeText(char art, uint32_t mode) {
    std::string s(1, art);
    static const char* rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) s += (mode & (0400u >> i)) ? rwx[i] : '-';
    if (mode & 04000) s[3] = (mode & 0100) ? 's' : 'S';
    if (mode & 02000) s[6] = (mode & 0010) ? 's' : 'S';
    if (mode & 01000) s[9] = (mode & 0001) ? 't' : 'T';
    return s;
}

/// Art-Buchstabe und Dateiart-Bits (S_IF*) zum tar-Typ.
void art(char typ, char& buchstabe, uint16_t& bits) {
    switch (typ) {
        case '5': buchstabe = 'd'; bits = 0040000; break;
        case '2': buchstabe = 'l'; bits = 0120000; break;
        case '3': buchstabe = 'c'; bits = 0020000; break;
        case '4': buchstabe = 'b'; bits = 0060000; break;
        case '6': buchstabe = 'p'; bits = 0010000; break;
        default:  buchstabe = '-'; bits = 0100000; break;
    }
}

} // namespace

TarFileSystem::Scan TarFileSystem::scanAb(const TarQuelle& q, uint64_t start) {
    Scan s;
    uint64_t p = start;
    const uint64_t gesamt = q.size();
    std::string lang_name, lang_link;
    uint8_t b[kBlock];
    for (;;) {
        if (p + kBlock > gesamt) { s.ende = Ende::Streamende; s.ende_off = p; break; }
        if (!q.read(p, b, kBlock)) { s.ende = Ende::Streamende; s.ende_off = p; break; }
        if (tar::nullblock(b)) { s.ende = Ende::Nullblock; s.ende_off = p; break; }
        tar::Kopf k;
        if (!tar::kopfLesen(b, k)) {
            if (p == start) return s;          // schon der erste Kopf taugt nicht
            s.ende = Ende::Pruefsumme; s.ende_off = p; break;
        }
        s.ok = true;
        const uint64_t daten = p + kBlock;
        const bool mit_daten = k.typ == '0' || k.typ == '7' || k.typ == 'L' || k.typ == 'K'
                            || k.typ == 'x' || k.typ == 'g';
        const uint64_t soll = mit_daten ? k.size : 0;
        const uint64_t ist  = std::min<uint64_t>(soll, gesamt > daten ? gesamt - daten : 0);
        const uint64_t next = daten + (soll + kBlock - 1) / kBlock * kBlock;

        if (k.typ == 'L' || k.typ == 'K') {
            std::vector<uint8_t> d(static_cast<size_t>(std::min<uint64_t>(ist, 4096)));
            if (!d.empty()) q.read(daten, d.data(), d.size());
            std::string n(d.begin(), d.end());
            n.erase(std::find(n.begin(), n.end(), '\0'), n.end());
            (k.typ == 'L' ? lang_name : lang_link) = n;
        } else if (k.typ != 'x' && k.typ != 'g') {
            Eintrag e;
            e.k = k;
            if (!lang_name.empty()) e.k.name = lang_name;
            if (!lang_link.empty()) e.k.linkname = lang_link;
            lang_name.clear(); lang_link.clear();
            if (e.k.typ == '0' || e.k.typ == '7') {
                if (!e.k.name.empty() && e.k.name.back() == '/') e.k.typ = '5';   // V7: Verzeichnis am '/'
            }
            e.kopf_off = p; e.daten_off = daten; e.ist = ist;
            e.k.size = soll;
            e.abgeschnitten = ist < soll;
            e.name = bereinige(e.k.name, e.pfad_geaendert);
            if (!e.name.empty()) s.e.push_back(std::move(e));
            if (ist < soll) { s.ende = Ende::Abgeschnitten; s.ende_off = daten; break; }
        }
        p = next;
    }
    return s;
}

TarFileSystem::Scan TarFileSystem::scanAlles(const TarQuelle& q) {
    uint8_t b[kBlock];
    tar::Kopf k;
    if (q.size() >= kBlock && q.read(0, b, kBlock) && tar::kopfLesen(b, k)) {
        Scan s = scanAb(q, 0);
        if (s.ok) {
            // doppelte Namen zaehlen
            std::map<std::string, int> n;
            for (const Eintrag& e : s.e) ++n[e.name];
            for (const auto& [name, c] : n) s.doppelt += c - 1;
            return s;
        }
    }
    // Fortsetzung: erster 512-ausgerichteter Kopf, ab dem der Strom sauber auslaeuft.
    for (uint64_t off = kBlock; off + kBlock <= q.size(); off += kBlock) {
        if (!q.read(off, b, kBlock) || !tar::kopfLesen(b, k)) continue;
        Scan s = scanAb(q, off);
        if (!s.ok || s.e.empty() || s.ende == Ende::Pruefsumme) continue;
        s.anfang = off;
        std::map<std::string, int> n;
        for (const Eintrag& e : s.e) ++n[e.name];
        for (const auto& [name, c] : n) s.doppelt += c - 1;
        return s;
    }
    return Scan{};
}

std::unique_ptr<TarFileSystem> TarFileSystem::mount(std::unique_ptr<TarQuelle> q, std::string& why) {
    if (!q) { why = "kein Datenstrom"; return nullptr; }
    if (!looksLikeTar(*q, &why)) return nullptr;
    return std::unique_ptr<TarFileSystem>(new TarFileSystem(std::move(q)));
}

bool TarFileSystem::looksLikeTar(const TarQuelle& q, std::string* why) {
    const Scan s = scanAlles(q);
    if (s.ok) return true;
    if (why) *why = "kein tar-Kopfblock bei Byte 0 und keiner, hinter dem ein tar-Strom sauber ausliefe";
    return false;
}

uint64_t TarFileSystem::fortsetzungAnfang() const { return scanAlles(*q_).anfang; }

const TarFileSystem::Eintrag* TarFileSystem::finde(const Scan& s, const std::string& name) const {
    const Eintrag* r = nullptr;
    for (const Eintrag& e : s.e) if (e.name == name) r = &e;     // der letzte gewinnt
    return r;
}

std::vector<FileEntry> TarFileSystem::list() const {
    std::vector<FileEntry> out;
    const Scan s = scanAlles(*q_);
    if (!s.ok) return out;
    if (s.anfang > 0) {
        FileEntry f;
        f.name = tar::kFortsetzungName;
        f.size = s.anfang;
        f.type = "-";
        f.unix_mode = 0100644;
        f.attributes = modeText('-', 0644);
        out.push_back(f);
    }
    std::map<std::string, size_t> letzter;
    for (size_t i = 0; i < s.e.size(); ++i) letzter[s.e[i].name] = i;
    for (size_t i = 0; i < s.e.size(); ++i) {
        const Eintrag& e = s.e[i];
        if (letzter[e.name] != i) continue;
        FileEntry f;
        char a; uint16_t bits;
        art(e.k.typ, a, bits);
        f.name = e.name;
        f.type = std::string(1, a);
        f.size = e.ist;
        if (e.k.typ == '1') {                  // harter Link: Inhalt des Ziels
            bool g = false;
            const Eintrag* z = finde(s, bereinige(e.k.linkname, g));
            f.size = z ? z->ist : 0;
        }
        if (e.k.typ == '5' || e.k.typ == '3' || e.k.typ == '4' || e.k.typ == '6') f.size = 0;
        if (e.k.typ == '2') f.size = e.k.linkname.size();
        f.unix_mode  = static_cast<uint16_t>(bits | (e.k.mode & 07777));
        f.attributes = modeText(a, e.k.mode);
        f.unix_uid   = static_cast<int>(e.k.uid);
        f.unix_gid   = static_cast<int>(e.k.gid);
        f.unix_nlink = e.k.typ == '1' ? 2 : 1;
        f.unix_inode = static_cast<uint32_t>(i + 1);
        f.unix_mtime = e.k.mtime;
        f.date       = datumText(e.k.mtime);
        if (e.abgeschnitten) { f.damaged = true; f.declared_size = e.k.size; }
        out.push_back(std::move(f));
    }
    return out;
}

bool TarFileSystem::lies(const Scan& s, const Eintrag& e, std::vector<uint8_t>& out, int tiefe) const {
    out.clear();
    if (e.k.typ == '5') return fail(e.name + " ist ein Verzeichnis");
    if (e.k.typ == '3' || e.k.typ == '4' || e.k.typ == '6')
        return fail(e.name + " ist eine Geraetedatei/FIFO und hat keinen Inhalt");
    if (e.k.typ == '2') { out.assign(e.k.linkname.begin(), e.k.linkname.end()); return true; }
    if (e.k.typ == '1') {
        bool g = false;
        const Eintrag* z = tiefe < 8 ? finde(s, bereinige(e.k.linkname, g)) : nullptr;
        if (!z) return fail(e.name + ": Linkziel '" + e.k.linkname + "' steht nicht im Strom");
        return lies(s, *z, out, tiefe + 1);
    }
    out.resize(static_cast<size_t>(e.ist));
    if (e.ist && !q_->read(e.daten_off, out.data(), out.size()))
        return fail(e.name + ": Datenstrom nicht lesbar");
    return true;
}

bool TarFileSystem::read(const std::string& name, std::vector<uint8_t>& out) {
    const Scan s = scanAlles(*q_);
    if (!s.ok) return fail("kein tar-Strom");
    if (s.anfang > 0 && name == tar::kFortsetzungName) {
        out.resize(static_cast<size_t>(s.anfang));
        return q_->read(0, out.data(), out.size()) || fail("Datenstrom nicht lesbar");
    }
    bool g = false;
    const Eintrag* e = finde(s, bereinige(name, g));
    if (!e) return fail("Datei nicht gefunden: " + name);
    return lies(s, *e, out, 0);
}

bool TarFileSystem::firstSector(const std::string& name, FsRecoverOrt& out) const {
    const Scan s = scanAlles(*q_);
    if (s.anfang > 0 && name == tar::kFortsetzungName) return q_->ort(0, out);
    bool g = false;
    const Eintrag* e = finde(s, bereinige(name, g));
    return e && q_->ort(e->kopf_off, out);
}

bool TarFileSystem::write(const std::string&, const std::vector<uint8_t>&, const WriteOptions&) {
    return fail("tar-Disketten sind nur lesbar");
}
bool TarFileSystem::erase(const std::string&) { return fail("tar-Disketten sind nur lesbar"); }
bool TarFileSystem::wouldFit(const std::vector<PlannedFile>&, FitReport& out) const {
    out = FitReport{};
    out.detail = "tar-Disketten sind nur lesbar";
    return true;
}
bool TarFileSystem::mkfs() { return fail("tar-Disketten sind nur lesbar"); }

FsInfo TarFileSystem::info() const {
    FsInfo i;
    const Scan s = scanAlles(*q_);
    i.total_bytes = q_->size();
    uint64_t belegt = s.anfang;
    for (const Eintrag& e : s.e) belegt += kBlock + (e.ist + kBlock - 1) / kBlock * kBlock;
    i.used_bytes = std::min(belegt, i.total_bytes);
    i.free_bytes = i.total_bytes - i.used_bytes;     // Rest hinter dem Archivende (Altbestand)
    int n = 0;
    for (const FileEntry& e : list()) if (e.type != "d") ++n;
    i.files = n;
    return i;
}

FsCheckReport TarFileSystem::check(FsCheckLevel level, bool nachladen) const {
    (void)nachladen;
    FsCheckReport r;
    r.level = level;
    FsFindings f(r);
    r.schritt("tar.schritt.kopfbloecke", "Kopfbloecke, Pruefsummen und Strom");
    const Scan s = scanAlles(*q_);
    if (!s.ok) {
        f.add("tar.kein_strom", FsSeverity::Fehler, FsLayer::Verwaltung, "(Strom)",
              "Kein tar-Strom gefunden");
        r.schrittEnde();
        return r;
    }
    if (s.anfang > 0)
        f.add("tar.fortsetzung", FsSeverity::Info, FsLayer::Verwaltung, "Byte 0",
              "Die Diskette beginnt mitten in einer Datei (" + std::to_string(s.anfang)
              + " Byte Anfangsstueck, als " + tar::kFortsetzungName + " lesbar); der Strom setzt "
              "bei Byte " + std::to_string(s.anfang) + " wieder auf.  Mit den vorigen Disketten "
              "zusammen: tar-satz");
    if (s.ende == Ende::Pruefsumme)
        f.add("tar.pruefsumme", FsSeverity::Fehler, FsLayer::Verwaltung,
              "Byte " + std::to_string(s.ende_off),
              "Kopfblock ohne gueltige Pruefsumme — der Rest des Stroms ist nicht lesbar");
    if (s.ende == Ende::Abgeschnitten && !s.e.empty()) {
        const Eintrag& e = s.e.back();
        f.add("tar.abgeschnitten", FsSeverity::Warnung, FsLayer::Verwaltung, e.name,
              "Die letzte Datei ist abgeschnitten: " + std::to_string(e.ist) + " von "
              + std::to_string(e.k.size) + " Byte auf dieser Diskette; der Strom setzt sich "
              "auf der naechsten Diskette fort (tar-satz).  Extrahiert wird das Teilstueck");
    }
    if (s.doppelt > 0)
        f.add("tar.doppelt", FsSeverity::Info, FsLayer::Verwaltung, "(Namen)",
              std::to_string(s.doppelt) + " Name(n) kommen mehrfach vor — gezeigt wird je Name "
              "die letzte Fassung, wie bei tar -x");
    int ge = 0;
    for (const Eintrag& e : s.e) if (e.pfad_geaendert) ++ge;
    if (ge > 0)
        f.add("tar.pfad", FsSeverity::Info, FsLayer::Verwaltung, "(Namen)",
              std::to_string(ge) + " Name(n) mit fuehrendem '/' oder '..' — beim Auflisten "
              "relativ gemacht ('..' wird '__')");
    r.schrittEnde();
    return r;
}
