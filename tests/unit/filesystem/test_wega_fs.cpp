/**
 * @file test_wega_fs.cpp
 * @brief GoogleTests fuer @ref WegaFileSystem — das UNIX-System-III-Dateisystem des P8000.
 *
 * ### Woher die Sollwerte stammen
 * | Sollwert | Quelle |
 * |---|---|
 * | NICFREE 50, NICINOD 100, INOPB 8, 13×3-Byte-Adressen | `head/sys/param.h`, `ino.h` |
 * | s_isize = fsize/25 + 2 (1440 → 59), Inode 1 Bad-Block, Wurzel 040777 | `sa.mkfs.c` |
 * | Feldlage des Superblocks, s_m/s_n = 1/72 | nachgemessen an `w30root1.img` |
 * | Freiliste/Ankerbloecke, ialloc-Suche | `uts/sys/alloc.c` |
 *
 * Die Lieferdisketten (`~/Documents/K1520emu/Disketten/P8000/WEGA3.0/w30*.img`, nicht im
 * Repo) werden nur geprueft, wenn sie da sind (`K1520_WEGA_IMG` = Ordner uebersteuert).
 * Alles andere laeuft auf einer selbst angelegten Mini-Diskette bzw. einem Blockgeraet
 * im Speicher.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/wega/wega_fs.h"
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

/// Blockgeraet im Speicher — mit Schreibzaehler (Transaktionen pruefen).
class MemDev : public WegaBlockDev {
public:
    explicit MemDev(uint32_t n, std::shared_ptr<std::vector<uint8_t>> buf = nullptr)
        : buf_(buf ? buf : std::make_shared<std::vector<uint8_t>>(size_t(n) * 512, 0)), n_(n) {}
    uint32_t blocks() const override { return n_; }
    bool readBlock(uint32_t bn, uint8_t* d) const override {
        if (bn >= n_) return false;
        std::memcpy(d, buf_->data() + size_t(bn) * 512, 512);
        return true;
    }
    bool writeBlock(uint32_t bn, const uint8_t* s) override {
        if (bn >= n_) return false;
        std::memcpy(buf_->data() + size_t(bn) * 512, s, 512);
        ++schreibvorgaenge;
        return true;
    }
    std::shared_ptr<std::vector<uint8_t>> buf_;
    uint32_t n_;
    int schreibvorgaenge = 0;
};

std::vector<uint8_t> muster(size_t n, uint8_t start) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(start + i * 7 + i / 512);
    return v;
}

std::unique_ptr<WegaFileSystem> neuesFs(uint32_t bloecke,
                                        std::shared_ptr<std::vector<uint8_t>>* puffer = nullptr) {
    auto dev = std::make_unique<MemDev>(bloecke);
    if (puffer) *puffer = dev->buf_;
    std::string err;
    auto fs = WegaFileSystem::format(std::move(dev), "test", err, 0, 1, 72, 600000000u);
    EXPECT_TRUE(fs) << err;
    if (fs) fs->setClock([] { return 600000000u; });
    return fs;
}

int befundeAbWarnung(const FsCheckReport& r) { return r.zaehlerAb(FsSeverity::Warnung); }

std::string befundText(const FsCheckReport& r) { return r.alsText(); }

std::string wegaOrdner() {
    if (const char* e = std::getenv("K1520_WEGA_IMG")) return e;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Documents/K1520emu/Disketten/P8000/WEGA3.0" : "";
}

}  // namespace

// ─── Anlegen wie sa.mkfs ─────────────────────────────────────────────────────

TEST(WegaFs, MkfsWieSaMkfs) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    const wega::Superblock sb = fs->superblock();
    EXPECT_EQ(sb.isize, 59);                  // 1440/25 + 2 — wie die Lieferdisketten
    EXPECT_EQ(sb.fsize, 1440u);
    EXPECT_EQ(sb.m, 1);
    EXPECT_EQ(sb.n, 72);
    EXPECT_EQ(sb.fname, "test");
    EXPECT_EQ(sb.tinode, sb.inodes() - 2);    // Inode 1 (Bad-Block) und 2 (Wurzel)
    // Ein Block fuer die Wurzel — und s_tfree zaehlt die Endmarke 0 der Freiliste
    // mit (sa.mkfs `bfree(0)`), genau wie auf allen Lieferdisketten.
    EXPECT_EQ(sb.tfree, 1440u - 59u - 1u + 1u);
    wega::Dinode root;
    ASSERT_TRUE(fs->readInode(2, root));
    EXPECT_EQ(root.mode, wega::IFDIR | 0777);
    EXPECT_EQ(root.nlink, 2);
    EXPECT_EQ(root.size, 32u);
    wega::Dinode bad;
    ASSERT_TRUE(fs->readInode(1, bad));
    EXPECT_EQ(bad.mode, wega::IFREG);
    EXPECT_EQ(bad.nlink, 0);
    EXPECT_TRUE(WegaFileSystem::looksLikeWega(fs->device(), nullptr));
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    EXPECT_TRUE(r.ohneBefund()) << befundText(r);
}

TEST(WegaFs, ErkenntCpmUndLeeresNicht) {
    MemDev leer(1440);
    std::string why;
    EXPECT_FALSE(WegaFileSystem::looksLikeWega(leer, &why));
    MemDev e5(1440);
    std::fill(e5.buf_->begin(), e5.buf_->end(), 0xE5);
    EXPECT_FALSE(WegaFileSystem::looksLikeWega(e5, &why));
    EXPECT_NE(why.find("Superblock"), std::string::npos) << why;
}

// ─── Schreiben und Lesen ─────────────────────────────────────────────────────

TEST(WegaFs, RundreiseKleinMittelGross) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    // 0 B, 1 Block, direkt voll, einfach indirekt, doppelt indirekt (> 138 Bloecke)
    const std::map<std::string, size_t> groessen = {
        {"leer", 0}, {"klein", 100}, {"zehn", 5120}, {"indirekt", 40000}, {"doppelt", 80000}};
    for (const auto& [name, n] : groessen)
        ASSERT_TRUE(fs->write(name, muster(n, static_cast<uint8_t>(n)), {})) << fs->lastError();
    for (const auto& [name, n] : groessen) {
        std::vector<uint8_t> zurueck;
        ASSERT_TRUE(fs->read(name, zurueck)) << fs->lastError();
        EXPECT_EQ(zurueck, muster(n, static_cast<uint8_t>(n))) << name;
    }
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    EXPECT_TRUE(r.ohneBefund()) << befundText(r);
    // Freiliste ist dicht: Zaehler = Kette (wega.frei.zaehler waere sonst da)
}

TEST(WegaFs, VerzeichnisseUndPfade) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->makeDirectory("usr/lib/tmac")) << fs->lastError();
    ASSERT_TRUE(fs->write("usr/lib/tmac/tmac.s", muster(700, 3), {})) << fs->lastError();
    ASSERT_TRUE(fs->write("/etc/passwd", {'w','e','g','a',':','\n'}, {})) << fs->lastError();
    EXPECT_TRUE(fs->makeDirectory("usr/lib"));            // schon da = kein Fehler
    EXPECT_FALSE(fs->makeDirectory("etc/passwd/x"));       // Datei im Weg

    std::map<std::string, FileEntry> l;
    for (const FileEntry& e : fs->list()) l[e.name] = e;
    ASSERT_TRUE(l.count("usr/lib/tmac/tmac.s"));
    EXPECT_EQ(l["usr"].type, "d");
    EXPECT_EQ(l["usr"].unix_nlink, 3);                    // ., Eintrag im Elter, lib/..
    EXPECT_EQ(l["usr/lib/tmac"].attributes, "drwxr-xr-x");
    EXPECT_EQ(l["etc/passwd"].attributes, "-rw-r--r--");
    EXPECT_EQ(l["etc/passwd"].size, 6u);
    EXPECT_EQ(l["etc/passwd"].date, "1989-01-05 10:40");   // 600000000 s
    wega::Dinode root;
    ASSERT_TRUE(fs->readInode(2, root));
    EXPECT_EQ(root.nlink, 4);                             // ., .., usr/.., etc/..
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    EXPECT_TRUE(r.ohneBefund()) << befundText(r);
}

TEST(WegaFs, LoeschenGibtAllesZurueck) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    const wega::Superblock vorher = fs->superblock();
    ASSERT_TRUE(fs->write("a/b/gross", muster(90000, 1), {}));
    ASSERT_TRUE(fs->write("a/klein", muster(10, 1), {}));
    EXPECT_FALSE(fs->erase("a"));                          // nicht leer
    ASSERT_TRUE(fs->erase("a/b/gross")) << fs->lastError();
    ASSERT_TRUE(fs->erase("a/b")) << fs->lastError();
    ASSERT_TRUE(fs->erase("a/klein"));
    ASSERT_TRUE(fs->erase("a"));
    EXPECT_FALSE(fs->erase("a"));
    EXPECT_TRUE(fs->list().empty());
    const wega::Superblock nachher = fs->superblock();
    EXPECT_EQ(nachher.tfree, vorher.tfree);
    EXPECT_EQ(nachher.tinode, vorher.tinode);
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    EXPECT_TRUE(r.ohneBefund()) << befundText(r);
}

TEST(WegaFs, UeberschreibenNurMitOverwriteUndBehaeltDieInode) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("x", muster(3000, 1), {}));
    const uint32_t ino = fs->lookup("x");
    EXPECT_FALSE(fs->write("x", muster(10, 2), {}));
    WriteOptions w;
    w.overwrite = true;
    ASSERT_TRUE(fs->write("x", muster(70000, 2), w));
    EXPECT_EQ(fs->lookup("x"), ino);
    std::vector<uint8_t> z;
    ASSERT_TRUE(fs->read("x", z));
    EXPECT_EQ(z, muster(70000, 2));
    EXPECT_TRUE(fs->check(FsCheckLevel::Voll, true).ohneBefund());
}

TEST(WegaFs, VolleDisketteIstEineTransaktion) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(200, &puffer);      // 200 Bloecke: isize 10, 190 Datenbloecke
    ASSERT_TRUE(fs);
    const std::vector<uint8_t> vorher = *puffer;
    EXPECT_FALSE(fs->write("zu_gross", muster(200 * 512, 1), {}));
    EXPECT_NE(fs->lastError().find("Platz"), std::string::npos) << fs->lastError();
    EXPECT_EQ(*puffer, vorher) << "eine gescheiterte Operation darf nichts schreiben";
    FitReport fit;
    ASSERT_TRUE(fs->wouldFit({{"zu_gross", 200 * 512}}, fit));
    EXPECT_FALSE(fit.fits);
    ASSERT_TRUE(fs->wouldFit({{"passt", 50 * 512}}, fit));
    EXPECT_TRUE(fit.fits);
}

TEST(WegaFs, InodeListeVollUndZwischenspeicherLeer) {
    auto fs = neuesFs(200);               // 8 Bloecke Inode-Liste = 64 Inodes
    ASSERT_TRUE(fs);
    int n = 0;
    while (fs->write("f" + std::to_string(n), {}, {})) ++n;
    EXPECT_EQ(n, 62);                     // 64 − Bad-Block − Wurzel
    EXPECT_NE(fs->lastError().find("Inode"), std::string::npos);
    EXPECT_TRUE(fs->check(FsCheckLevel::Voll, true).ohneBefund());
}

TEST(WegaFs, NamenUeber14ZeichenWerdenAbgewiesen) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    EXPECT_TRUE(fs->write("vierzehn_zeich", {1}, {}));
    EXPECT_FALSE(fs->write("fuenfzehn_zeich", {1}, {}));
    EXPECT_NE(fs->lastError().find("14"), std::string::npos);
}

// ─── Pruefung: Schaeden werden gefunden ──────────────────────────────────────

TEST(WegaFsCheck, DoppeltBelegterBlockIstGefahr) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("a", muster(600, 1), {}));
    ASSERT_TRUE(fs->write("b", muster(600, 2), {}));
    wega::Dinode a, b;
    ASSERT_TRUE(fs->readInode(fs->lookup("a"), a));
    ASSERT_TRUE(fs->readInode(fs->lookup("b"), b));
    // b's zweiten Block auf a's ersten zeigen lassen (Inode direkt im Puffer setzen)
    const uint32_t ino = fs->lookup("b");
    b.addr[1] = a.addr[0];
    uint8_t* p = puffer->data() + (2 + (ino - 1) / 8) * 512 + ((ino - 1) % 8) * 64;
    b.in(p);
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    bool doppelt = false, verloren = false;
    for (const FsFinding& f : r.findings) {
        if (f.id == "wega.block.doppelt") { doppelt = true; EXPECT_EQ(f.severity, FsSeverity::Gefahr); }
        if (f.id == "wega.block.verloren") verloren = true;
    }
    EXPECT_TRUE(doppelt) << befundText(r);
    EXPECT_TRUE(verloren) << befundText(r);   // b's alter zweiter Block haengt nun frei
}

TEST(WegaFsCheck, BelegterBlockInDerFreilisteIstGefahr) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("a", muster(600, 1), {}));
    wega::Dinode a;
    ASSERT_TRUE(fs->readInode(fs->lookup("a"), a));
    wega::Superblock sb = fs->superblock();
    ASSERT_LT(sb.nfree, wega::kNicFree);
    sb.free[static_cast<size_t>(sb.nfree++)] = a.addr[0];
    ++sb.tfree;
    sb.in(puffer->data() + 512);
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    bool gefunden = false;
    for (const FsFinding& f : r.findings)
        if (f.id == "wega.block.frei_belegt") gefunden = true;
    EXPECT_TRUE(gefunden) << befundText(r);
    EXPECT_EQ(r.hoechste(), FsSeverity::Gefahr);
}

TEST(WegaFsCheck, VerweisAufFreieInodeUndFalscherLinkzaehler) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("a", muster(10, 1), {}));
    ASSERT_TRUE(fs->write("b", muster(10, 1), {}));
    const uint32_t ia = fs->lookup("a"), ib = fs->lookup("b");
    auto inodeZeiger = [&](uint32_t i) {
        return puffer->data() + (2 + (i - 1) / 8) * 512 + ((i - 1) % 8) * 64;
    };
    // a: Inode freigeben ohne den Eintrag zu loeschen; b: nlink 5
    wega::Dinode leer;
    leer.in(inodeZeiger(ia));
    wega::Dinode b = wega::Dinode::aus(inodeZeiger(ib));
    b.nlink = 5;
    b.in(inodeZeiger(ib));
    const FsCheckReport schnell = fs->check(FsCheckLevel::Schnell, false);
    bool zaehler = false;
    for (const FsFinding& f : schnell.findings) if (f.id == "wega.inode.zaehler") zaehler = true;
    EXPECT_TRUE(zaehler) << befundText(schnell);         // der billige Schatten
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    std::set<std::string> ids;
    for (const FsFinding& f : r.findings) ids.insert(f.id);
    EXPECT_TRUE(ids.count("wega.dir.freier_inode")) << befundText(r);
    EXPECT_TRUE(ids.count("wega.inode.links")) << befundText(r);
    EXPECT_TRUE(ids.count("wega.block.verloren")) << befundText(r);
}

TEST(WegaFsCheck, PruefungSchreibtNie) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("a/b", muster(5000, 1), {}));
    const std::vector<uint8_t> vorher = *puffer;
    fs->check(FsCheckLevel::Voll, true);
    fs->list();
    fs->info();
    EXPECT_EQ(*puffer, vorher);
}

// ─── Ueber DiskVolume: .img anlegen, put/get, Erkennung ──────────────────────

TEST(WegaDiskVolume, AnlegenErkennenRundreise) {
    const std::string img = k1520test::tempPath("wega720.img");
    const std::string quelle = k1520test::tempPath("wega_quelle");
    const std::string ziel = k1520test::tempPath("wega_ziel");
    fs::remove_all(quelle); fs::remove_all(ziel); fs::remove(img);
    fs::create_directories(fs::path(quelle) / "usr" / "leer");
    fs::create_directories(fs::path(quelle) / "bin");
    { std::ofstream(fs::path(quelle) / "bin" / "hallo", std::ios::binary).write("echo hallo\n", 11); }
    {
        const auto d = muster(123457, 9);
        std::ofstream(fs::path(quelle) / "usr" / "daten.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    }
    fs::permissions(fs::path(quelle) / "bin" / "hallo", fs::perms::owner_exec, fs::perm_options::add);

    std::string err;
    {
        auto dv = DiskVolume::create(img, "wega720", "probe", formate(), dateisysteme(), err);
        ASSERT_TRUE(dv) << err;
        TransferOptions t;
        ASSERT_TRUE(dv->insertAll(quelle, t)) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }
    EXPECT_EQ(fs::file_size(img), 737280u);
    auto dv = DiskVolume::open(img, "", formate(), dateisysteme(), err);
    ASSERT_TRUE(dv) << err;
    EXPECT_EQ(dv->detection().filesystem, "wega720");
    EXPECT_TRUE(dv->detection().unambiguous);
    EXPECT_TRUE(dv->istWega());
    std::map<std::string, FileEntry> l;
    for (const FileEntry& e : dv->list()) l[e.name] = e;
    EXPECT_EQ(l["usr/leer"].type, "d");
#ifdef _WIN32
    // Das Dateisystem des Wirts kennt kein Ausfuehrungsrecht (fs::permissions bleibt ohne Wirkung):
    // die Datei kommt mit 0644 auf die Diskette.
    EXPECT_EQ(l["bin/hallo"].attributes, "-rw-r--r--");
#else
    EXPECT_EQ(l["bin/hallo"].attributes, "-rwxr-xr-x");
#endif
    EXPECT_EQ(l["usr/daten.bin"].size, 123457u);
    ASSERT_TRUE(dv->extractAll(ziel, TransferOptions{})) << dv->lastError();
    {   // Geschlossen vor remove_all (Windows: "Sharing violation")
        std::ifstream a(fs::path(ziel) / "usr" / "daten.bin", std::ios::binary);
        std::vector<uint8_t> z((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        EXPECT_EQ(z, muster(123457, 9));
    }
    EXPECT_TRUE(fs::is_directory(fs::path(ziel) / "usr" / "leer"));
    const FsCheckReport& r = dv->check(FsCheckLevel::Voll, true);
    EXPECT_EQ(befundeAbWarnung(r), 0) << r.alsText();
    fs::remove_all(quelle); fs::remove_all(ziel); fs::remove(img);
}

// ─── Lieferdisketten (optional) ──────────────────────────────────────────────

TEST(WegaLieferdisketten, AlleLesbarUndOhneBefund) {
    const std::string ordner = wegaOrdner();
    std::error_code ec;
    if (ordner.empty() || !fs::is_directory(ordner, ec))
        GTEST_SKIP() << "WEGA-Abbilder nicht vorhanden: " << ordner;
    int geprueft = 0;
    for (const auto& e : fs::directory_iterator(ordner, ec)) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".img" || name.find("start") != std::string::npos
            || name.find("strt") != std::string::npos)
            continue;
        // Nie die Vorlage anfassen: schreibgeschuetzt oeffnen genuegt hier.
        std::string err;
        auto dv = DiskVolume::open(e.path().string(), "", formate(), dateisysteme(), err);
        ASSERT_TRUE(dv) << name << ": " << err;
        EXPECT_EQ(dv->detection().filesystem, "wega720") << name;
        bool contents = false;
        for (const FileEntry& f : dv->list()) if (f.name == "CONTENTS") contents = true;
        // doc1 ist reine Dokumentation ohne Inhaltsliste (wega_datentraeger.md §3)
        if (name.find("doc") == std::string::npos)
            EXPECT_TRUE(contents) << name << ": /CONTENTS fehlt";
        const FsCheckReport& r = dv->check(FsCheckLevel::Voll, true);
        EXPECT_EQ(befundeAbWarnung(r), 0) << name << "\n" << r.alsText();
        ++geprueft;
    }
    if (geprueft == 0) GTEST_SKIP() << "keine Dateisystemdisketten in " << ordner;
}

// ─── UNIX-Dateiangaben: chmod / chown / touch ────────────────────────────────

TEST(WegaFs, SetAttributesAendertModusBesitzerUndZeit) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    WriteOptions w;
    w.wega_mode = 0644; w.wega_mode_gesetzt = true;
    ASSERT_TRUE(fs->write("bin/x", muster(700, 1), w)) << fs->lastError();

    fs->setClock([] { return 600000123u; });
    UnixAttrs a;
    a.set_mode = true;  a.mode = 04755;
    a.set_uid = true;   a.uid = 12;
    a.set_gid = true;   a.gid = 7;
    a.set_mtime = true; a.mtime = 500000000u;
    ASSERT_TRUE(fs->setAttributes("bin/x", a)) << fs->lastError();

    wega::Dinode d;
    ASSERT_TRUE(fs->readInode(fs->lookup("bin/x"), d));
    EXPECT_EQ(d.mode, wega::IFREG | 04755);        // Dateiart bleibt
    EXPECT_EQ(d.uid, 12);
    EXPECT_EQ(d.gid, 7);
    EXPECT_EQ(d.mtime, 500000000u);
    EXPECT_EQ(d.ctime, 600000123u);                // wie chmod: Inode-Zeit = jetzt
    EXPECT_EQ(d.atime, 600000000u);                // unberuehrt
    EXPECT_EQ(d.size, 700u);
    EXPECT_EQ(d.nlink, 1);

    // Nur ein Feld: die uebrigen bleiben stehen
    UnixAttrs nur_gid;
    nur_gid.set_gid = true; nur_gid.gid = 3;
    ASSERT_TRUE(fs->setAttributes("bin/x", nur_gid));
    ASSERT_TRUE(fs->readInode(fs->lookup("bin/x"), d));
    EXPECT_EQ(d.gid, 3);
    EXPECT_EQ(d.uid, 12);
    EXPECT_EQ(d.mode, wega::IFREG | 04755);

    // Verzeichnisse gehen genauso
    UnixAttrs dir;
    dir.set_mode = true; dir.mode = 0700;
    ASSERT_TRUE(fs->setAttributes("bin", dir));
    ASSERT_TRUE(fs->readInode(fs->lookup("bin"), d));
    EXPECT_EQ(d.mode, wega::IFDIR | 0700);

    bool gefunden = false;
    for (const FileEntry& e : fs->list())
        if (e.name == "bin/x") {
            gefunden = true;
            EXPECT_EQ(e.attributes, "-rwsr-xr-x");
            EXPECT_EQ(e.unix_uid, 12);
            EXPECT_EQ(e.unix_mtime, 500000000u);
            EXPECT_EQ(e.unix_ctime, 600000123u);
            EXPECT_EQ(e.unix_atime, 600000000u);
        }
    EXPECT_TRUE(gefunden);
    const FsCheckReport r = fs->check(FsCheckLevel::Voll, true);
    EXPECT_TRUE(r.ohneBefund()) << befundText(r);
}

TEST(WegaFs, SetAttributesPruftVorDemSchreiben) {
    std::shared_ptr<std::vector<uint8_t>> puffer;
    auto fs = neuesFs(1440, &puffer);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("a", muster(100, 1), {}));
    const std::vector<uint8_t> vorher = *puffer;

    UnixAttrs a;
    a.set_mode = true; a.mode = 0170755;            // Dateiart aendern: verboten
    EXPECT_FALSE(fs->setAttributes("a", a));
    EXPECT_NE(fs->lastError().find("07777"), std::string::npos) << fs->lastError();

    UnixAttrs u;
    u.set_uid = true; u.uid = 70000;
    EXPECT_FALSE(fs->setAttributes("a", u));
    u.uid = -1;
    EXPECT_FALSE(fs->setAttributes("a", u));

    UnixAttrs ok;
    ok.set_mode = true; ok.mode = 0600;
    EXPECT_FALSE(fs->setAttributes("gibt/es/nicht", ok));
    EXPECT_NE(fs->lastError().find("nicht gefunden"), std::string::npos);
    EXPECT_EQ(*puffer, vorher) << "ein abgelehnter Aufruf darf nichts veraendern";
}

TEST(WegaFs, SchreibenMitZeitUndBesitzerAngabe) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    WriteOptions w;
    w.wega_owner_gesetzt = true; w.wega_uid = 0; w.wega_gid = 0;
    w.wega_mtime = 123456789u;
    ASSERT_TRUE(fs->write("f", muster(10, 1), w));
    wega::Dinode d;
    ASSERT_TRUE(fs->readInode(fs->lookup("f"), d));
    EXPECT_EQ(d.mtime, 123456789u);
    EXPECT_EQ(d.atime, 123456789u);
}

TEST(WegaFs, BlockListeNenntDateiUndVerwaltungsbloecke) {
    auto fs = neuesFs(1440);
    ASSERT_TRUE(fs);
    ASSERT_TRUE(fs->write("klein", muster(100, 1), {}));
    ASSERT_TRUE(fs->write("gross", muster(40000, 2), {}));   // einfach indirekt
    EXPECT_FALSE(fs->blockList("klein").empty());
    EXPECT_EQ(fs->blockList("klein").find('|'), std::string::npos);
    EXPECT_NE(fs->blockList("gross").find("| indirekt:"), std::string::npos)
        << fs->blockList("gross");
    EXPECT_TRUE(fs->blockList("gibt-es-nicht").empty());
}

// ─── Ueber DiskVolume: .fileinfo, Beiblatt, Rundreise ────────────────────────

namespace {

std::string leseText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void schreibeText(const fs::path& p, const std::string& t) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(t.data(), static_cast<std::streamsize>(t.size()));
}

std::map<std::string, FileEntry> eintraege(const DiskVolume& dv) {
    std::map<std::string, FileEntry> l;
    for (const FileEntry& e : dv.list()) l[e.name] = e;
    return l;
}

}  // namespace

TEST(WegaDiskVolume, FileinfoTraegtUnixAngabenUndSpieltSieZurueck) {
    const std::string img = k1520test::tempPath("wega_attr1.img");
    const std::string img2 = k1520test::tempPath("wega_attr2.img");
    const std::string quelle = k1520test::tempPath("wega_attr_quelle");
    const std::string ziel = k1520test::tempPath("wega_attr_ziel");
    for (const auto& p : {quelle, ziel}) fs::remove_all(p);
    fs::remove(img); fs::remove(img2);
    schreibeText(fs::path(quelle) / "bin" / "hallo", "echo hallo\n");
    schreibeText(fs::path(quelle) / "etc" / "passwd", "root::0:0::/:/bin/sh\n");
    fs::create_directories(fs::path(quelle) / "usr" / "leer");

    std::string err;
    auto dv = DiskVolume::create(img, "wega720", "probe", formate(), dateisysteme(), err);
    ASSERT_TRUE(dv) << err;
    ASSERT_TRUE(dv->insertAll(quelle, TransferOptions{})) << dv->lastError();

    auto setze = [&](const std::string& name, uint16_t mode, int uid, int gid, uint32_t mtime) {
        UnixAttrs a;
        a.set_mode = true; a.mode = mode;
        a.set_uid = true; a.uid = uid;
        a.set_gid = true; a.gid = gid;
        a.set_mtime = true; a.mtime = mtime;
        ASSERT_TRUE(dv->setAttributes(FileRef{0, name}, a)) << dv->lastError();
    };
    setze("bin/hallo", 04711, 5, 6, 400000000u);
    setze("etc/passwd", 0644, 0, 3, 400000001u);
    setze("usr", 0750, 9, 8, 400000002u);
    setze("usr/leer", 01777, 1, 2, 400000003u);

    ASSERT_TRUE(dv->extractAll(ziel, TransferOptions{})) << dv->lastError();

    // .fileinfo mit UNIX-Angaben — und keinem einzigen UDOS-/CP/M-Schluessel
    const std::string fi = leseText(fs::path(ziel) / "bin" / "hallo.fileinfo");
    EXPECT_NE(fi.find("fs=wega"), std::string::npos) << fi;
    EXPECT_NE(fi.find("name=bin/hallo"), std::string::npos) << fi;
    EXPECT_NE(fi.find("mode=0104711"), std::string::npos) << fi;
    EXPECT_NE(fi.find("uid=5"), std::string::npos) << fi;
    EXPECT_NE(fi.find("gid=6"), std::string::npos) << fi;
    EXPECT_NE(fi.find("mtime=400000000"), std::string::npos) << fi;
    for (const char* udos : {"typ=", "eig=", "satz=", "segment=", "mem=", "attr=", "UDOS"})
        EXPECT_EQ(fi.find(udos), std::string::npos) << udos << " in\n" << fi;

    // Verzeichnisse: `<ordner>.fileinfo` neben dem Ordner
    const std::string dfi = leseText(fs::path(ziel) / "usr.fileinfo");
    EXPECT_NE(dfi.find("mode=0040750"), std::string::npos) << dfi;
    EXPECT_NE(dfi.find("art=d"), std::string::npos) << dfi;

    // Beiblatt mit ALLEN Inodes, ohne UDOS-Begriffe
    const std::string bb = leseText(fs::path(ziel) / "wega-dateiangaben.txt");
    EXPECT_NE(bb.find("usr/leer art=d mode=0041777"), std::string::npos) << bb;
    EXPECT_NE(bb.find("bin/hallo art=- mode=0104711"), std::string::npos) << bb;
    EXPECT_EQ(bb.find("UDOS"), std::string::npos) << bb;

    // Rundreise: auf eine frische Diskette zurueck, Angaben muessen gleich sein
    auto dv2 = DiskVolume::create(img2, "wega720", "zwei", formate(), dateisysteme(), err);
    ASSERT_TRUE(dv2) << err;
    ASSERT_TRUE(dv2->insertAll(ziel, TransferOptions{})) << dv2->lastError();
    const auto l1 = eintraege(*dv), l2 = eintraege(*dv2);
    for (const char* n : {"bin/hallo", "etc/passwd", "usr", "usr/leer"}) {
        ASSERT_TRUE(l2.count(n)) << n;
        EXPECT_EQ(l2.at(n).unix_mode, l1.at(n).unix_mode) << n;
        EXPECT_EQ(l2.at(n).unix_uid, l1.at(n).unix_uid) << n;
        EXPECT_EQ(l2.at(n).unix_gid, l1.at(n).unix_gid) << n;
        EXPECT_EQ(l2.at(n).unix_mtime, l1.at(n).unix_mtime) << n;
    }
    // Zubehoer ist keine Nutzdatei
    for (const auto& [n, e] : l2) {
        EXPECT_EQ(n.find(".fileinfo"), std::string::npos) << n;
        EXPECT_EQ(n.find("wega-dateiangaben"), std::string::npos) << n;
    }
    const FsCheckReport& r = dv2->check(FsCheckLevel::Voll, true);
    EXPECT_EQ(befundeAbWarnung(r), 0) << r.alsText();

    dv.reset(); dv2.reset();
    for (const auto& p : {quelle, ziel}) fs::remove_all(p);
    fs::remove(img); fs::remove(img2);
}

TEST(WegaDiskVolume, FremdesFileinfoWirdUeberlesenNichtAlsUdosGedeutet) {
    const std::string img = k1520test::tempPath("wega_fremd.img");
    const std::string quelle = k1520test::tempPath("wega_fremd_quelle");
    fs::remove_all(quelle); fs::remove(img);
    schreibeText(fs::path(quelle) / "PROG", "x");
    schreibeText(fs::path(quelle) / "PROG.fileinfo",
                 "fs=udos\nname=PROG\ntyp=P\neig=W\nstart=4000\nsatz=1024\n");
    std::string err;
    auto dv = DiskVolume::create(img, "wega720", "fremd", formate(), dateisysteme(), err);
    ASSERT_TRUE(dv) << err;
    TransferOptions t;
    EXPECT_TRUE(dv->insert((fs::path(quelle) / "PROG").string(), FileRef{0, "PROG"}, t))
        << dv->lastError();
    const auto l = eintraege(*dv);
    ASSERT_TRUE(l.count("PROG"));
    EXPECT_EQ(l.at("PROG").unix_mode & 07777, 0644);     // Vorgabe, nicht UDOS-Eigenschaft "W"
    dv.reset();
    fs::remove_all(quelle); fs::remove(img);
}

TEST(WegaDiskVolume, UnixAngabenAufEinerNichtWegaDisketteWerdenAbgelehnt) {
    const std::string img = k1520test::tempPath("cpm_unix.img");
    fs::remove(img);
    std::string err;
    auto dv = DiskVolume::create(img, "cpa780", "x", formate(), dateisysteme(), err);
    ASSERT_TRUE(dv) << err;
    UnixAttrs a;
    a.set_mode = true; a.mode = 0755;
    EXPECT_FALSE(dv->setAttributes(FileRef{0, "X.COM"}, a));
    EXPECT_NE(dv->lastError().find("UNIX"), std::string::npos) << dv->lastError();
    EXPECT_EQ(dv->fsType(), FsType::Cpm);
    EXPECT_TRUE(dv->blockList(FileRef{0, "X.COM"}).empty());
    dv.reset();
    fs::remove(img);
}
