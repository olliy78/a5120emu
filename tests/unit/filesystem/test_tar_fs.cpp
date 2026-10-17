/**
 * @file test_tar_fs.cpp
 * @brief GoogleTests fuer Tar-Disketten (@ref TarFileSystem) — ein `tar`-Strom ohne Dateisystem.
 *
 * Alles Synthetische laeuft immer: Mini-Tar-Stroeme werden in ein 720-K-`.img` bzw. ein
 * `.cqm` gelegt und ueber @ref DiskVolume geoeffnet.  Die Echtdaten-Gegenprobe
 * (`K1520_CQM_DIR` = Ordner mit den P8000-`.cqm`/`.tar`) wird sonst uebersprungen und
 * vergleicht mit dem `tar` des Systems — als Gegenprobe, nicht als Abhaengigkeit.
 *
 * @see core/filesystem/tar/tar_fs.h, doc/design/33_cqm.md
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/tar/tar_fs.h"
#include "core/peripherals/floppy_drive/cqm_codec.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        FormatCatalog k = FormatCatalog::load({K1520_FORMATS_DEFAULT}, &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        FsCatalog k = FsCatalog::load({K1520_FORMATS_DEFAULT}, formate(), &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

constexpr size_t kDisk = 737280;   // 720K

// ─── Baukasten ───────────────────────────────────────────────────────────────

void oktal(uint8_t* f, size_t n, uint64_t v) {      // n-1 Ziffern + NUL
    std::string s(n - 1, '0');
    for (size_t i = n - 1; i-- > 0 && v; v /= 8) s[i] = static_cast<char>('0' + v % 8);
    std::memcpy(f, s.data(), n - 1);
    f[n - 1] = 0;
}

struct Datei {
    std::string name;
    std::string inhalt;
    char        typ = '0';
    uint32_t    mode = 0644, uid = 7, gid = 3, mtime = 600000000;
    std::string link;
    std::string prefix;       // ustar
    bool        ustar = false;
    uint64_t    groesse = ~0ULL;   // ~0 = inhalt.size()
};

std::vector<uint8_t> kopf(const Datei& d, bool kaputt = false) {
    std::vector<uint8_t> h(512, 0);
    std::memcpy(h.data(), d.name.data(), std::min<size_t>(d.name.size(), 100));
    oktal(h.data() + 100, 8, d.mode);
    oktal(h.data() + 108, 8, d.uid);
    oktal(h.data() + 116, 8, d.gid);
    oktal(h.data() + 124, 12, d.groesse == ~0ULL ? d.inhalt.size() : d.groesse);
    oktal(h.data() + 136, 12, d.mtime);
    h[156] = static_cast<uint8_t>(d.typ);
    std::memcpy(h.data() + 157, d.link.data(), std::min<size_t>(d.link.size(), 100));
    if (d.ustar) {
        std::memcpy(h.data() + 257, "ustar\0" "00", 8);
        std::memcpy(h.data() + 345, d.prefix.data(), std::min<size_t>(d.prefix.size(), 155));
    }
    std::memset(h.data() + 148, ' ', 8);
    uint32_t s = 0;
    for (uint8_t b : h) s += b;
    oktal(h.data() + 148, 7, s);     // 6 Ziffern + NUL, dann Leerzeichen wie GNU tar
    h[154] = 0; h[155] = ' ';
    if (kaputt) h[148] = '7';
    return h;
}

void haenge(std::vector<uint8_t>& s, const Datei& d, bool kaputt = false) {
    const auto h = kopf(d, kaputt);
    s.insert(s.end(), h.begin(), h.end());
    const bool daten = d.typ == '0' || d.typ == 'L';
    if (!daten) return;
    s.insert(s.end(), d.inhalt.begin(), d.inhalt.end());
    s.resize((s.size() + 511) / 512 * 512, 0);
}

void ende(std::vector<uint8_t>& s) { s.resize(s.size() + 1024, 0); }

/// Stroem auf Diskettenlaenge bringen; der Rest ist Altbestand (0xE5 wie formatiert).
std::vector<uint8_t> aufDisk(std::vector<uint8_t> s, uint8_t rest = 0xE5) {
    s.resize(kDisk, rest);
    return s;
}

void schreibeDatei(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

std::string imgMit(const std::string& name, const std::vector<uint8_t>& strom) {
    const std::string p = k1520test::tempPath(name);
    schreibeDatei(p, strom);
    return p;
}

std::unique_ptr<DiskVolume> oeffne(const std::string& p, bool schreibend = false,
                                   bool roh = true) {
    std::string err;
    auto v = DiskVolume::open(p, "", formate(), dateisysteme(), err, !schreibend, roh);
    EXPECT_TRUE(v) << err;
    return v;
}

const FileEntry* finde(const std::vector<FileEntry>& l, const std::string& n) {
    for (const FileEntry& e : l) if (e.name == n) return &e;
    return nullptr;
}

bool hatBefund(const FsCheckReport& r, const std::string& id) {
    for (const FsFinding& f : r.findings) if (f.id == id) return true;
    return false;
}

std::vector<uint8_t> beispiel() {
    std::vector<uint8_t> s;
    haenge(s, {"dir/", "", '5', 0755});
    haenge(s, {"dir/hallo.txt", "Hallo Welt\n", '0', 0640, 25, 1, 612345678});
    haenge(s, {"gross.bin", std::string(1300, 'x')});
    Datei l; l.name = "dir/zweitname"; l.typ = '1'; l.link = "dir/hallo.txt"; l.mode = 0640;
    haenge(s, l);
    Datei u; u.name = "u.txt"; u.inhalt = "ustar"; u.ustar = true; u.prefix = "lang/praefix";
    haenge(s, u);
    ende(s);
    return s;
}

}  // namespace

// ─── Erkennung und Lesen ─────────────────────────────────────────────────────

/// @test Gueltiger Strom: erkannt als tar720, Dateien, Verzeichnis, Link, ustar-Praefix.
TEST(TarFs, ErkenntUndListetDenStrom) {
    const std::string p = imgMit("k1520_test_tar_a.img", aufDisk(beispiel()));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    EXPECT_EQ(v->detection().filesystem, "tar720");
    EXPECT_TRUE(v->istTar());
    EXPECT_TRUE(v->istUnix());
    EXPECT_FALSE(v->istWega());
    const auto l = v->list();
    ASSERT_EQ(l.size(), 5u);
    const FileEntry* d = finde(l, "dir");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->type, "d");
    const FileEntry* h = finde(l, "dir/hallo.txt");
    ASSERT_TRUE(h);
    EXPECT_EQ(h->size, 11u);
    EXPECT_EQ(h->unix_uid, 25);
    EXPECT_EQ(h->unix_gid, 1);
    EXPECT_EQ(h->unix_mode, 0100640);
    EXPECT_EQ(h->attributes, "-rw-r-----");
    EXPECT_EQ(h->unix_mtime, 612345678u);
    EXPECT_FALSE(h->damaged);
    const FileEntry* z = finde(l, "dir/zweitname");
    ASSERT_TRUE(z);
    EXPECT_EQ(z->size, 11u);               // harter Link: Inhalt des Ziels
    EXPECT_TRUE(finde(l, "lang/praefix/u.txt"));
    EXPECT_TRUE(v->checkReport().zaehlerAb(FsSeverity::Warnung) == 0);
    fs::remove(p);
}

/// @test Inhalt lesen, auch ueber den Link; Verzeichnis ist kein Inhalt.
TEST(TarFs, LiestInhaltUndLinks) {
    const std::string p = imgMit("k1520_test_tar_b.img", aufDisk(beispiel()));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    const std::string z = k1520test::tempPath("k1520_test_tar_b_aus");
    fs::remove_all(z);
    TransferOptions t;
    ASSERT_TRUE(v->extractAll(z, t)) << v->lastError();
    std::ifstream f(fs::path(z) / "dir" / "hallo.txt");
    std::stringstream ss; ss << f.rdbuf();
    EXPECT_EQ(ss.str(), "Hallo Welt\n");
    std::ifstream f2(fs::path(z) / "dir" / "zweitname");
    std::stringstream s2; s2 << f2.rdbuf();
    EXPECT_EQ(s2.str(), "Hallo Welt\n");
    EXPECT_EQ(fs::file_size(fs::path(z) / "gross.bin"), 1300u);
    // .fileinfo: fs=tar plus UNIX-Angaben
    std::ifstream fi(fs::path(z) / "dir" / "hallo.txt.fileinfo");
    std::stringstream si; si << fi.rdbuf();
    EXPECT_NE(si.str().find("fs=tar"), std::string::npos);
    EXPECT_NE(si.str().find("mode=0100640"), std::string::npos);
    EXPECT_NE(si.str().find("uid=25"), std::string::npos);
    EXPECT_EQ(si.str().find("\nteilstueck="), std::string::npos);
    fs::remove_all(z);
    fs::remove(p);
}

/// @test GNU-Langname ('L') und absolute/'..'-Pfade werden relativ gemacht.
TEST(TarFs, LangeNamenUndUnsichtbarePfade) {
    std::vector<uint8_t> s;
    const std::string lang(150, 'a');
    Datei l; l.name = "././@LongLink"; l.typ = 'L'; l.inhalt = "verz/" + lang + std::string(1, '\0');
    haenge(s, l);
    haenge(s, {"gekuerzt", "lang"});
    haenge(s, {"/abs/pfad.txt", "A"});
    haenge(s, {"../../boese.txt", "B"});
    ende(s);
    const std::string p = imgMit("k1520_test_tar_c.img", aufDisk(s));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    const auto li = v->list();
    EXPECT_TRUE(finde(li, "verz/" + lang));
    EXPECT_TRUE(finde(li, "abs/pfad.txt"));
    EXPECT_TRUE(finde(li, "__/__/boese.txt"));
    EXPECT_TRUE(hatBefund(v->checkReport(), "tar.pfad"));
    fs::remove(p);
}

/// @test Abgeschnittene letzte Datei: Teilstueck lesbar, gekennzeichnet, Befund tar.abgeschnitten.
TEST(TarFs, AbgeschnittenIstTeilstueckKeinFehler) {
    // Kopf von gross.dat verspricht mehr Bytes, als die Diskette noch hat.
    Datei riesig; riesig.name = "gross.dat"; riesig.inhalt = ""; riesig.groesse = kDisk * 2;
    std::vector<uint8_t> r;
    haenge(r, {"vorn.txt", "kurz"});                  // Kopf + 1 Datenblock = 1024 Byte
    const auto rh = kopf(riesig);
    r.insert(r.end(), rh.begin(), rh.end());
    const std::string p3 = imgMit("k1520_test_tar_d3.img", aufDisk(r, 'g'));
    auto v = oeffne(p3);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    const auto l = v->list();
    const FileEntry* e = finde(l, "gross.dat");
    ASSERT_TRUE(e);
    EXPECT_TRUE(e->damaged);
    EXPECT_EQ(e->declared_size, kDisk * 2);
    EXPECT_EQ(e->size, kDisk - 1536u);     // vorn.txt (1024) + Kopf von gross.dat (512) davor
    EXPECT_TRUE(hatBefund(v->checkReport(), "tar.abgeschnitten"));
    EXPECT_EQ(v->checkReport().zaehlerAb(FsSeverity::Fehler), 0);
    const std::string z = k1520test::tempPath("k1520_test_tar_d_aus");
    fs::remove_all(z);
    TransferOptions to;
    ASSERT_TRUE(v->extractAll(z, to)) << v->lastError();
    EXPECT_EQ(fs::file_size(fs::path(z) / "gross.dat"), e->size);
    std::ifstream fi(fs::path(z) / "gross.dat.fileinfo");
    std::stringstream si; si << fi.rdbuf();
    EXPECT_NE(si.str().find("\nteilstueck="), std::string::npos);
    fs::remove_all(z);
    fs::remove(p3);
}

/// @test Hartlink, dessen Ziel auf einer anderen Diskette liegt: leere, als unvollstaendig
/// markierte Datei + Befund `tar.linkziel` — der Auszug bricht NICHT ab (er verlore sonst
/// alle uebrigen Dateien der Diskette).
TEST(TarFs, HartlinkOhneZielBrichtDenAuszugNichtAb) {
    std::vector<uint8_t> s;
    haenge(s, {"a.txt", "eins\n"});
    Datei l; l.name = "bin/zweitname"; l.typ = '1'; l.link = "bin/fehlt"; l.mode = 0755;
    haenge(s, l);
    ende(s);
    const std::string p = imgMit("k1520_test_tar_linkziel.img", aufDisk(s));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    const auto liste = v->list();
    const FileEntry* e = finde(liste, "bin/zweitname");
    ASSERT_TRUE(e);
    EXPECT_TRUE(e->damaged);
    EXPECT_EQ(e->size, 0u);
    EXPECT_TRUE(hatBefund(v->checkReport(), "tar.linkziel"));
    EXPECT_EQ(v->checkReport().zaehlerAb(FsSeverity::Fehler), 0);
    const std::string z = k1520test::tempPath("k1520_test_tar_linkziel_aus");
    fs::remove_all(z);
    TransferOptions to;
    ASSERT_TRUE(v->extractAll(z, to)) << v->lastError();
    EXPECT_EQ(fs::file_size(fs::path(z) / "bin" / "zweitname"), 0u);
    EXPECT_TRUE(fs::exists(fs::path(z) / "a.txt"));
    fs::remove_all(z);
    fs::remove(p);
}

/// @test Diskette 2..n beginnt mitten in einer Datei: als Fortsetzung erkannt.
TEST(TarFs, FortsetzungWirdErkannt) {
    std::vector<uint8_t> s(1536);           // Anfangsstueck: 3 Bloecke Dateiinhalt (Text)
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<uint8_t>('a' + (i * 7) % 26);
    haenge(s, {"neu1.txt", "eins"});
    haenge(s, {"neu2.txt", "zwei"});
    ende(s);
    const std::string p = imgMit("k1520_test_tar_e.img", aufDisk(s));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    EXPECT_EQ(v->detection().filesystem, "tar720");
    const auto l = v->list();
    const FileEntry* a = finde(l, "_fortsetzung_anfang.bin");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->size, 1536u);
    EXPECT_TRUE(finde(l, "neu1.txt"));
    EXPECT_TRUE(finde(l, "neu2.txt"));
    EXPECT_TRUE(hatBefund(v->checkReport(), "tar.fortsetzung"));
    fs::remove(p);
}

/// @test Pruefsummenfehler mitten im Strom: Befund, der Anfang bleibt lesbar.
///       Schon der erste Kopf kaputt: kein tar.
TEST(TarFs, Pruefsummenfehler) {
    std::vector<uint8_t> s;
    haenge(s, {"gut.txt", "gut"});
    haenge(s, {"schlecht.txt", "schlecht"}, /*kaputt=*/true);
    ende(s);
    const std::string p = imgMit("k1520_test_tar_f.img", aufDisk(s));
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    EXPECT_TRUE(finde(v->list(), "gut.txt"));
    EXPECT_FALSE(finde(v->list(), "schlecht.txt"));
    EXPECT_TRUE(hatBefund(v->checkReport(), "tar.pruefsumme"));

    std::vector<uint8_t> t;
    haenge(t, {"schlecht.txt", "schlecht"}, true);
    ende(t);
    const std::string p2 = imgMit("k1520_test_tar_f2.img", aufDisk(t));
    auto v2 = oeffne(p2);
    ASSERT_TRUE(v2);
    EXPECT_FALSE(v2->hasFileSystem());
    fs::remove(p); fs::remove(p2);
}

/// @test Daten ohne tar-Kopf werden NICHT erkannt.
TEST(TarFs, NichtTarDatenWerdenNichtErkannt) {
    std::vector<uint8_t> text(kDisk);
    for (size_t i = 0; i < text.size(); ++i) text[i] = static_cast<uint8_t>('a' + (i * 7) % 26);
    for (const auto& d : {std::vector<uint8_t>(kDisk, 0xE5), std::vector<uint8_t>(kDisk, 0), text}) {
        const std::string p = imgMit("k1520_test_tar_g.img", d);
        auto v = oeffne(p);
        ASSERT_TRUE(v);
        EXPECT_FALSE(v->hasFileSystem() && v->istTar());
        fs::remove(p);
    }
}

/// @test Ein echtes Dateisystem schlaegt tar: WEGA-Diskette mit tar-Kopf im Bootblock bleibt WEGA;
///       eine CP/M-Diskette bleibt CP/M.
TEST(TarFs, EchteDateisystemeGehenVor) {
    const std::string p = k1520test::tempPath("k1520_test_tar_h.img");
    std::string err;
    {
        auto w = DiskVolume::create(p, "wega720", "test", formate(), dateisysteme(), err);
        ASSERT_TRUE(w) << err;
        ASSERT_TRUE(w->flush());
    }
    {   // tar-Kopf in Block 0 (der Bootblock gehoert WEGA nicht)
        auto strom = std::vector<uint8_t>();
        std::ifstream f(p, std::ios::binary);
        strom.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        const auto h = kopf({"trug.txt", "x"});
        std::copy(h.begin(), h.end(), strom.begin());
        schreibeDatei(p, strom);
    }
    auto v = oeffne(p);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->detection().filesystem, "wega720");
    EXPECT_FALSE(v->istTar());
    fs::remove(p);

    const std::string c = k1520test::tempPath("k1520_test_tar_i.img");
    auto cpm = DiskVolume::create(c, "cpa800", "", formate(), dateisysteme(), err);
    ASSERT_TRUE(cpm) << err;
    ASSERT_TRUE(cpm->flush());
    cpm.reset();
    auto v2 = oeffne(c);
    ASSERT_TRUE(v2);
    EXPECT_EQ(v2->detection().filesystem, "cpa800");
    fs::remove(c);
}

/// @test Nur lesbar: put/rm/mkdir scheitern mit klarer Meldung, setReadOnly(false) wirkt nicht.
TEST(TarFs, NurLesbar) {
    const std::string p = imgMit("k1520_test_tar_j.img", aufDisk(beispiel()));
    auto v = oeffne(p, /*schreibend=*/true);
    ASSERT_TRUE(v);
    EXPECT_TRUE(v->readOnly());
    EXPECT_TRUE(v->readOnlyForced());
    v->setReadOnly(false);
    EXPECT_TRUE(v->readOnly());
    EXPECT_NE(v->lastError().find("tar"), std::string::npos);
    const std::string q = k1520test::tempPath("k1520_test_tar_j.txt");
    schreibeDatei(q, {'a'});
    EXPECT_FALSE(v->insert(q, FileRef{0, "neu.txt"}, TransferOptions{}));
    EXPECT_NE(v->lastError().find("nur lesbar"), std::string::npos);
    EXPECT_FALSE(v->erase(FileRef{0, "gross.bin"}));
    EXPECT_FALSE(v->makeDirectory(FileRef{0, "x"}));
    std::string err;
    EXPECT_EQ(DiskVolume::create(k1520test::tempPath("k1520_test_tar_j2.img"), "tar720", "",
                                 formate(), dateisysteme(), err), nullptr);
    fs::remove(p); fs::remove(q);
}

/// @test Mehrdisketten-Satz: der Strom setzt sich ueber die Grenze fort, die Datei wird ganz.
TEST(TarFs, SatzVerkettetDieStroeme) {
    std::string gross(900000, 0);
    for (size_t i = 0; i < gross.size(); ++i) gross[i] = static_cast<char>('A' + i % 23);
    std::vector<uint8_t> strom;
    haenge(strom, {"eins.txt", "erste Datei"});
    haenge(strom, {"gross.dat", gross});
    haenge(strom, {"letzte.txt", "letzte Datei"});
    ende(strom);
    ASSERT_GT(strom.size(), kDisk);
    ASSERT_LT(strom.size(), 2 * kDisk);
    std::vector<uint8_t> d1(strom.begin(), strom.begin() + kDisk);
    std::vector<uint8_t> d2(strom.begin() + kDisk, strom.end());
    d2.resize(kDisk, 0xE5);
    const std::string p1 = imgMit("k1520_test_tar_k1.img", d1);
    const std::string p2 = imgMit("k1520_test_tar_k2.img", d2);

    {   // einzeln: Diskette 1 abgeschnitten, Diskette 2 Fortsetzung
        auto a = oeffne(p1);
        ASSERT_TRUE(a);
        EXPECT_TRUE(hatBefund(a->checkReport(), "tar.abgeschnitten"));
        auto b = oeffne(p2);
        ASSERT_TRUE(b);
        ASSERT_TRUE(b->hasFileSystem());
        EXPECT_TRUE(finde(b->list(), "_fortsetzung_anfang.bin"));
    }
    std::string err;
    auto v = DiskVolume::openTarSatz({p1, p2}, formate(), dateisysteme(), err);
    ASSERT_TRUE(v) << err;
    EXPECT_EQ(v->checkReport().zaehlerAb(FsSeverity::Warnung), 0);
    EXPECT_TRUE(v->readOnly());
    const auto l = v->list();
    ASSERT_EQ(l.size(), 3u);
    const FileEntry* g = finde(l, "gross.dat");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->size, gross.size());
    EXPECT_FALSE(g->damaged);
    const std::string z = k1520test::tempPath("k1520_test_tar_k_aus");
    fs::remove_all(z);
    ASSERT_TRUE(v->extractAll(z, TransferOptions{})) << v->lastError();
    std::ifstream f(fs::path(z) / "gross.dat", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    EXPECT_EQ(ss.str(), gross);
    fs::remove_all(z);
    fs::remove(p1); fs::remove(p2);

    std::string e2;
    EXPECT_EQ(DiskVolume::openTarSatz({"/nicht/da.img"}, formate(), dateisysteme(), e2), nullptr);
    EXPECT_FALSE(e2.empty());
}

// ─── .cqm ────────────────────────────────────────────────────────────────────

namespace {

/// Minimales 720K-CopyQM (9x512, 2 Seiten, 80 Zylinder) aus @p raw.
std::vector<uint8_t> baueCqm(const std::vector<uint8_t>& raw, const std::string& kommentar = "") {
    std::vector<uint8_t> f(CqmCodec::kHeaderSize, 0);
    f[0] = 'C'; f[1] = 'Q'; f[2] = 0x14;
    f[3] = 0; f[4] = 2;                     // 512
    f[0x10] = 9; f[0x12] = 2;
    const std::string d = "720K Double-Sided";
    std::copy(d.begin(), d.end(), f.begin() + 0x1C);
    f[0x58] = 1; f[0x5A] = 80; f[0x5B] = 80;
    const uint32_t crc = CqmCodec::crc(raw.data(), raw.size());
    for (int i = 0; i < 4; ++i) f[0x5C + i] = static_cast<uint8_t>(crc >> (8 * i));
    f[0x6F] = static_cast<uint8_t>(kommentar.size());
    const std::string et = "** NONE ** ";
    std::copy(et.begin(), et.end(), f.begin() + 0x60);
    f[0x74] = 3; f[0x75] = 2;               // Interleave 3, Skew 2 (nur gemeldet)
    uint8_t summe = 0;
    for (size_t i = 0; i < 0x84; ++i) summe = static_cast<uint8_t>(summe + f[i]);
    f[0x84] = static_cast<uint8_t>(-summe);
    f.insert(f.end(), kommentar.begin(), kommentar.end());
    for (size_t i = 0; i < raw.size();) {
        const size_t n = std::min<size_t>(raw.size() - i, 30000);
        f.push_back(static_cast<uint8_t>(n)); f.push_back(static_cast<uint8_t>(n >> 8));
        f.insert(f.end(), raw.begin() + i, raw.begin() + i + n);
        i += n;
    }
    return f;
}

}  // namespace

/// @test Erzwungener Schreibschutz eines .cqm (Luecke aus dem CQM-Bericht): auch mit
///       read_only=false geoeffnet bleibt es schreibgeschuetzt und unaufhebbar.
TEST(CqmDiskVolume, SchreibschutzIstErzwungen) {
    const std::string p = k1520test::tempPath("k1520_test_cqmvol.cqm");
    schreibeDatei(p, baueCqm(aufDisk(beispiel()), "Kommentar"));
    auto v = oeffne(p, /*schreibend=*/true);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->hasFileSystem());
    EXPECT_EQ(v->detection().filesystem, "tar720");
    EXPECT_TRUE(v->readOnly());
    EXPECT_TRUE(v->readOnlyForced());
    EXPECT_EQ(v->containerName(), "cqm");
    v->setReadOnly(false);
    EXPECT_TRUE(v->readOnly());
    EXPECT_FALSE(v->lastError().empty());
    ASSERT_NE(v->cqmInfo(), nullptr);
    EXPECT_EQ(v->cqmInfo()->comment, "Kommentar");
    EXPECT_FALSE(v->flush() && v->dirty());
    fs::remove(p);

    // auch ein .cqm mit anderem Inhalt (leere Diskette): nicht aufhebbar
    const std::string q = k1520test::tempPath("k1520_test_cqmvol2.cqm");
    schreibeDatei(q, baueCqm(std::vector<uint8_t>(kDisk, 0xE5)));
    auto w = oeffne(q, true);
    ASSERT_TRUE(w);
    EXPECT_TRUE(w->readOnly());
    EXPECT_TRUE(w->readOnlyForced());
    w->setReadOnly(false);
    EXPECT_TRUE(w->readOnly());
    EXPECT_NE(w->lastError().find("nur lesbar"), std::string::npos);
    fs::remove(q);
}

// ─── Echtdaten (K1520_CQM_DIR) ───────────────────────────────────────────────

namespace {

std::string cqmOrdner() {
    const char* e = std::getenv("K1520_CQM_DIR");
    return e ? e : "";
}

struct TarZeile { std::string perm, name; uint64_t size = 0; };

/// `tar -tvf` des Systems (Gegenprobe).  Harte Links ('h…') und Verzeichnisse werden ausgelassen.
std::vector<TarZeile> systemTar(const std::string& tarDatei, bool& laeuft) {
    std::vector<TarZeile> r;
    const std::string cmd = "LC_ALL=C tar -tvf '" + tarDatei + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    laeuft = pipe != nullptr;
    if (!pipe) return r;
    char zeile[4096];
    while (std::fgets(zeile, sizeof zeile, pipe)) {
        std::istringstream is(zeile);
        TarZeile z; std::string og, datum, zeit;
        is >> z.perm >> og >> z.size >> datum >> zeit;
        std::getline(is, z.name);
        if (!z.name.empty() && z.name[0] == ' ') z.name.erase(0, 1);
        while (!z.name.empty() && (z.name.back() == '\n' || z.name.back() == '\r')) z.name.pop_back();
        if (z.perm.empty() || z.perm[0] == 'h' || z.perm[0] == 'd') continue;
        if (!z.name.empty() && z.name[0] == '/') z.name.erase(0, z.name.find_first_not_of('/'));
        while (z.name.rfind("./", 0) == 0) z.name.erase(0, 2);
        r.push_back(z);
    }
    pclose(pipe);
    return r;
}

std::vector<std::string> cqms(const std::string& dir, const std::vector<std::string>& namen) {
    std::vector<std::string> r;
    for (const auto& n : namen) r.push_back((fs::path(dir) / (n + ".cqm")).string());
    return r;
}

/// Vergleich Namen+Groessen+Modus gegen GNU tar.
void vergleicheMitSystemTar(const std::vector<FileEntry>& ours, const std::string& tarDatei) {
    bool laeuft = false;
    const auto soll = systemTar(tarDatei, laeuft);
    if (!laeuft) return;
    std::vector<TarZeile> ist;
    for (const FileEntry& e : ours) {
        if (e.type != "-" || e.name == tar::kFortsetzungName) continue;
        if (e.unix_nlink == 2) continue;                      // Hardlink
        ist.push_back({e.attributes, e.name, e.size});
    }
    ASSERT_EQ(ist.size(), soll.size()) << tarDatei;
    for (size_t i = 0; i < soll.size(); ++i) {
        EXPECT_EQ(ist[i].name, soll[i].name) << tarDatei << " #" << i;
        EXPECT_EQ(ist[i].size, soll[i].size) << soll[i].name;
        EXPECT_EQ(ist[i].perm, soll[i].perm) << soll[i].name;
    }
}

}  // namespace

/// @test Alle bisher "nichts erkannten" tar-Disketten werden erkannt; nur die DOS-Diskette bleibt.
TEST(TarEchtdaten, AlleTarDisketten) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    int tarN = 0;
    std::set<std::string> nichts;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".cqm") continue;
        std::string err;
        auto v = DiskVolume::open(e.path().string(), "", formate(), dateisysteme(), err, true, true);
        ASSERT_TRUE(v) << e.path() << ": " << err;
        if (!v->hasFileSystem()) nichts.insert(e.path().stem().string());
        else if (v->istTar()) ++tarN;
    }
    EXPECT_EQ(tarN, 84);
    EXPECT_EQ(nichts, (std::set<std::string>{"wdos_dcp"}));
}

/// @test Einzelabbilder stimmen mit GNU `tar -tvf` der .tar-Gegenstuecke ueberein.
TEST(TarEchtdaten, EinzelabbilderGleichGnuTar) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    int geprueft = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".cqm") continue;
        const fs::path t = fs::path(dir) / (e.path().stem().string() + ".tar");
        if (!fs::exists(t) || fs::file_size(t) != kDisk) continue;
        std::string err;
        auto v = DiskVolume::open(e.path().string(), "", formate(), dateisysteme(), err, true, true);
        ASSERT_TRUE(v) << err;
        if (!v->istTar()) continue;
        SCOPED_TRACE(e.path().string());
        vergleicheMitSystemTar(v->list(), t.string());
        ++geprueft;
    }
    EXPECT_GT(geprueft, 20);
}

/// @test Jeder Mehrdisketten-Satz entpackt ohne Befund und stimmt mit GNU tar auf der Gesamt-.tar.
TEST(TarEchtdaten, SaetzeGleichGnuTarOhneBefund) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    const std::map<std::string, std::vector<std::string>> saetze = {
        {"ariane", {"ariane1", "ariane2"}},
        {"konsul", {"konsul1", "konsul2", "konsul3"}},
        {"lcc", {"lcc1", "lcc2"}},
        {"psc", {"psc1", "psc2"}},
        {"w11data", {"w11data1", "w11data2", "w11data3", "w11data4", "w11data5", "w11data6", "w11data7"}},
        {"w21pasc", {"w21pasc1", "w21pasc2"}},
        {"wegaf77", {"wegaf771", "wegaf772"}},
        {"wegbas", {"wegbas12", "wegbas22"}},
    };
    for (const auto& [satz, teile] : saetze) {
        SCOPED_TRACE(satz);
        const fs::path t = fs::path(dir) / (satz + ".tar");
        std::string err;
        auto v = DiskVolume::openTarSatz(cqms(dir, teile), formate(), dateisysteme(), err);
        ASSERT_TRUE(v) << err;
        EXPECT_EQ(v->checkReport().zaehlerAb(FsSeverity::Warnung), 0) << v->checkReport().alsText();
        for (const FileEntry& e : v->list()) EXPECT_FALSE(e.damaged) << e.name;
        if (fs::exists(t)) vergleicheMitSystemTar(v->list(), t.string());
        const std::string z = k1520test::tempPath("k1520_test_tar_satz_" + satz);
        fs::remove_all(z);
        ASSERT_TRUE(v->extractAll(z, TransferOptions{})) << v->lastError();
        fs::remove_all(z);
    }
}
