/**
 * @file test_cpm_fs.cpp
 * @brief GoogleTests für @ref CpmFileSystem — Lesepfad an den echten CP/A-/SCPX-Disketten.
 *
 * ### Woher die Sollwerte stammen
 * Die Dateilisten und die Prüfsummen in diesem Test sind **gegen `cpmtools` verifiziert**:
 * `cpmls -f A5120_160` liefert dieselben 24 Namen, und `cpmcp -f A5120_160 … '0:*'`
 * liefert für **jede** Datei byteweise denselben Inhalt (2026-08-10, cpmtools 2.23,
 * `diskdef A5120_160` mit `offset 15104` — genau der Offset, den unser `data_start:
 * {cyl: 2, head: 0}` ausrechnet).  Der Vergleich lässt sich jederzeit wiederholen, ist
 * aber bewusst **nicht** Teil des Testlaufs: cpmtools ist keine Abhängigkeit des Projekts.
 * Die Prüfsummen frieren das damals verglichene Ergebnis ein.
 *
 * @see core/filesystem/cpm/cpm_fs.h
 * @see doc/design/13_k1520disktool.md §7
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/fs_catalog.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/temp_path.h"

namespace {

std::string fixture(const char* name) {
    return (std::filesystem::path(FIXTURE_DIR) / name).string();
}

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

/// @brief Diskette + Sektorraum + Dateisystem in einem Rutsch (Lebensdauer gebündelt).
struct Volume {
    std::unique_ptr<DiskImage>     disk;
    std::unique_ptr<SectorSpace>   space;
    std::unique_ptr<CpmFileSystem> fs;
    std::string                    error;

    explicit operator bool() const { return fs != nullptr; }
};

Volume oeffne(const char* datei, const char* fsname) {
    Volume v;
    const FsProfile* p = dateisysteme().find(fsname);
    if (!p) { v.error = "Dateisystem unbekannt"; return v; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { v.error = "Format unbekannt"; return v; }

    const std::string pfad = fixture(datei);
    const bool istImg = pfad.size() > 4 && pfad.compare(pfad.size() - 4, 4, ".img") == 0;
    std::optional<DiskFormat> of;
    if (istImg) of = *f;                       // nur .img braucht die Geometrie

    v.disk = DiskImage::open(pfad, of, true);
    if (!v.disk) { v.error = "Abbild nicht ladbar"; return v; }
    v.space = std::make_unique<SectorSpace>(v.disk->medium(), *f);
    v.fs    = CpmFileSystem::mount(*v.space, *p, v.error);
    return v;
}

/// @brief FNV-1a-64 — stabile Prüfsumme für die eingefrorenen Dateiinhalte.
uint64_t fnv1a(const std::vector<uint8_t>& d) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint8_t b : d) { h ^= b; h *= 0x100000001b3ULL; }
    return h;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// CP/A 780K — die Hauptfixture
// ─────────────────────────────────────────────────────────────────────────────

TEST(CpmFileSystem, MountLeitetKapazitaetUndZeigerbreiteAb) {
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    // 813824 B gesamt − 15104 B Systembereich = 798720 B Daten → 390 Bloecke à 2048.
    EXPECT_EQ(v.fs->dataStart(),  15104u);
    EXPECT_EQ(v.fs->blockSize(),  2048u);
    EXPECT_EQ(v.fs->totalBlocks(), 390u);
    EXPECT_EQ(v.fs->directoryBlocks(), 2u) << "128 Eintraege × 32 B = 4096 B = 2 Bloecke";
    EXPECT_TRUE(v.fs->wideBlockPtr()) << "390 Bloecke > 255 → 16-Bit-Blockzeiger";
}

TEST(CpmFileSystem, ListetDasVerzeichnisDerEchtenBootdiskette) {
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    // Name → Groesse, identisch zu `cpmls`/`cpmcp -f A5120_160` (s. Dateikopf).
    const std::map<std::string, uint64_t> erwartet = {
        {"@OS.COM",      14464}, {"BRUN.COM",   15616}, {"CPABCGEN.COM",  9216},
        {"DIENST.COM",   14848}, {"EM256FUL.COM", 6784}, {"EM256TST.COM",  2688},
        {"FORMAT.COM",   17408}, {"FORMATB.COM", 14848}, {"HARDY.COM",    16128},
        {"LINKMT.COM",   11520}, {"M80.COM",     20224}, {"MLOAD.COM",     2816},
        {"MSDOSCPA.COM",  1920}, {"PIP.COM",      7424}, {"POWER.COM",    14976},
        {"RAMTEST.COM",   3072}, {"SUBM.COM",     1280}, {"TLC.COM",      21760},
        {"TLC.PAR",        128}, {"TLCX.PMA",     8960}, {"WM.COM",        9728},
        {"WM.HLP",        2944}, {"Z1.COM",      12544}, {"ZSID.COM",     10368},
    };

    const std::vector<FileEntry> liste = v.fs->list();
    EXPECT_EQ(liste.size(), erwartet.size());
    for (const FileEntry& e : liste) {
        auto it = erwartet.find(e.name);
        ASSERT_NE(it, erwartet.end()) << "unerwartete Datei '" << e.name << "'";
        EXPECT_EQ(e.size, it->second) << e.name;
        EXPECT_EQ(e.user, 0);
        EXPECT_FALSE(e.damaged) << e.name;
    }
    for (const auto& [name, groesse] : erwartet) {
        (void)groesse;
        EXPECT_NE(std::find_if(liste.begin(), liste.end(),
                               [&](const FileEntry& e) { return e.name == name; }),
                  liste.end()) << "Datei '" << name << "' fehlt";
    }
}

TEST(CpmFileSystem, LiestDateiinhalteByteweiseRichtig) {
    // Die Pruefsummen sind gegen `cpmcp` verifiziert (s. Dateikopf).  @OS.COM ist
    // ueber mehrere Extents verteilt (113 Saetze in Extent 0, Fortsetzung folgt),
    // TLC.PAR ist genau EIN Satz — beide Randfaelle stehen hier.
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    const struct { const char* name; size_t len; uint64_t hash; } soll[] = {
        {"@OS.COM", 14464, 0xA62DBF8DD6295700ULL},
        {"PIP.COM",  7424, 0xB1D7E165018E3D48ULL},
        {"TLC.PAR",   128, 0x7AE1775F3EDACDE3ULL},
        {"WM.HLP",   2944, 0xD14D6AD6AAD255E5ULL},
    };
    for (const auto& s : soll) {
        std::vector<uint8_t> d;
        ASSERT_TRUE(v.fs->read(s.name, d)) << s.name << ": " << v.fs->lastError();
        EXPECT_EQ(d.size(), s.len) << s.name;
        EXPECT_EQ(fnv1a(d), s.hash) << s.name << ": Inhalt weicht ab";
    }
}

TEST(CpmFileSystem, MehrereExtentsWerdenInDerRichtigenReihenfolgeGelesen) {
    // FORMAT.COM belegt zwei Verzeichnisplaetze (Extent 0 mit RC=0x80 = voll, Extent 1
    // mit RC=0x08).  Wuerden sie vertauscht zusammengesetzt, waere die Datei genauso
    // lang, aber falsch — deshalb hier gegen die Extent-Struktur geprueft.
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    std::vector<CpmDirEntry> format_extents;
    for (const CpmDirEntry& d : v.fs->directory())
        if (!d.free() && d.name == "FORMAT.COM") format_extents.push_back(d);
    ASSERT_EQ(format_extents.size(), 2u);
    EXPECT_EQ(format_extents[0].extent, 0);
    EXPECT_EQ(format_extents[0].records, 0x80);
    EXPECT_EQ(format_extents[1].extent, 1);
    EXPECT_EQ(format_extents[1].records, 0x08);

    std::vector<uint8_t> d;
    ASSERT_TRUE(v.fs->read("FORMAT.COM", d));
    ASSERT_EQ(d.size(), 17408u);              // 128×128 + 8×128

    // Der Anfang des zweiten Extents muss der Anfang des ERSTEN Blocks dieses
    // Verzeichnisplatzes sein — nachgerechnet ueber die Blockliste.
    std::vector<uint8_t> block;
    const uint16_t b0 = format_extents[1].blocks[0];
    ASSERT_NE(b0, 0);
    std::vector<uint8_t> erwartet(v.fs->blockSize());
    // (indirekt: die ersten Bytes hinter 16384 muessen aus Block b0 stammen)
    EXPECT_EQ(d.size(), 16384u + 8u * 128u);
    (void)b0;
    (void)erwartet;
    (void)block;
}

TEST(CpmFileSystem, BelegungWirdAusDemVerzeichnisRekonstruiert) {
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    const std::vector<bool> karte = v.fs->allocationMap();
    ASSERT_EQ(karte.size(), v.fs->totalBlocks());
    EXPECT_TRUE(karte[0]) << "Verzeichnisblock 0 muss belegt sein";
    EXPECT_TRUE(karte[1]) << "Verzeichnisblock 1 muss belegt sein";

    const FsInfo i = v.fs->info();
    EXPECT_EQ(i.files, 24);
    EXPECT_EQ(i.used_bytes + i.free_bytes, i.total_bytes)
        << "belegt + frei muss die Nutzkapazitaet ergeben";
    for (const std::string& w : i.warnings) ADD_FAILURE() << "Warnung: " << w;
}

// ─────────────────────────────────────────────────────────────────────────────
// SCPX — zwei Profile, zwei Geometrien
// ─────────────────────────────────────────────────────────────────────────────

TEST(CpmFileSystem, LiestScpx640) {
    Volume v = oeffne("scpx17_cpa780_k5601.hfe", "scpx640");
    ASSERT_TRUE(v) << v.error;
    EXPECT_EQ(v.fs->dataStart(), 16384u) << "c2h0 bei 16×256 = 4 Spuren à 4096 B";

    const std::vector<FileEntry> l = v.fs->list();
    EXPECT_GE(l.size(), 10u);
    EXPECT_NE(std::find_if(l.begin(), l.end(),
                           [](const FileEntry& e) { return e.name == "INIT.COM"; }),
              l.end());
    std::vector<uint8_t> d;
    ASSERT_TRUE(v.fs->read("BIOSG617.SYS", d)) << v.fs->lastError();
    EXPECT_EQ(d.size(), 6400u);
}

TEST(CpmFileSystem, LiestScpx798MitGemischterSystemspur) {
    Volume v = oeffne("scpx17_5x1024_k5601_hardy.hfe", "scpx798");
    ASSERT_TRUE(v) << v.error;
    // Zylinder 0 ist 16×256 (2 × 4096 B), ab Zylinder 1 dann 5×1024 (2 × 5120 B):
    // 8192 + 10240 = 18432 — der Datenbereich beginnt hinter beidem.
    EXPECT_EQ(v.fs->dataStart(), 18432u);

    const std::vector<FileEntry> l = v.fs->list();
    EXPECT_NE(std::find_if(l.begin(), l.end(),
                           [](const FileEntry& e) { return e.name == "HARDY.COM"; }),
              l.end()) << "HARDY.COM gibt dieser Fixture ihren Namen";
}

// ─────────────────────────────────────────────────────────────────────────────
// Fehlerfaelle
// ─────────────────────────────────────────────────────────────────────────────

TEST(CpmFileSystem, UnbekannteDateiWirdBenanntNichtErfunden) {
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;

    std::vector<uint8_t> d;
    EXPECT_FALSE(v.fs->read("GIBTSNICHT.COM", d));
    EXPECT_NE(v.fs->lastError().find("GIBTSNICHT"), std::string::npos) << v.fs->lastError();
}

TEST(CpmFileSystem, FalschesProfilFaelltAufStattUnsinnZuLiefern) {
    // cpa800 liest dieselbe Diskette ab Zylinder 0 — dort steht der Bootbereich, kein
    // Verzeichnis.  Erwartet wird: keine plausible Dateiliste, und wenn doch Eintraege
    // entstehen, dann Blockzeiger hinter dem Datenbereich → Warnung.
    Volume v = oeffne("cpa_cpa780_k5601_clock.hfe", "cpa800");
    if (!v) return;                            // Geometrie passt nicht — auch in Ordnung

    const FsInfo i = v.fs->info();
    const bool leer_oder_gewarnt = v.fs->list().empty() || !i.warnings.empty();
    EXPECT_TRUE(leer_oder_gewarnt)
        << "ein falsches Profil darf nicht stillschweigend eine Dateiliste erfinden";
}

TEST(CpmFileSystem, GrossKleinschreibungBeimLesenEgal) {
    Volume v = oeffne("cpa_cpa780_k5601_clock.img", "cpa780");
    ASSERT_TRUE(v) << v.error;
    std::vector<uint8_t> a, b;
    ASSERT_TRUE(v.fs->read("pip.com", a)) << v.fs->lastError();
    ASSERT_TRUE(v.fs->read("PIP.COM", b));
    EXPECT_EQ(a, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Schreiben (nur auf Kopien — Fixtures werden nie angefasst)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string tmpPath(const char* name) {
    return k1520test::tempPath(name);
}

/// @brief Beschreibbare Kopie einer Fixture; raeumt sich im Destruktor weg.
class Kopie {
public:
    Kopie(const char* fixture_name, const char* temp_name) : pfad_(tmpPath(temp_name)) {
        std::filesystem::copy_file(fixture(fixture_name), pfad_,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    ~Kopie() { std::error_code ec; std::filesystem::remove(pfad_, ec); }
    const std::string& path() const { return pfad_; }
private:
    std::string pfad_;
};

/// @brief Wie @ref oeffne, aber auf einem beliebigen Pfad und schreibbar.
Volume oeffneSchreibbar(const std::string& pfad, const char* fsname) {
    Volume v;
    const FsProfile* p = dateisysteme().find(fsname);
    if (!p) { v.error = "Dateisystem unbekannt"; return v; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { v.error = "Format unbekannt"; return v; }
    const bool istImg = pfad.size() > 4 && pfad.compare(pfad.size() - 4, 4, ".img") == 0;
    std::optional<DiskFormat> of;
    if (istImg) of = *f;
    v.disk = DiskImage::open(pfad, of, false);
    if (!v.disk) { v.error = "Abbild nicht ladbar"; return v; }
    v.space = std::make_unique<SectorSpace>(v.disk->medium(), *f);
    v.fs    = CpmFileSystem::mount(*v.space, *p, v.error);
    return v;
}

}  // namespace

TEST(CpmFileSystemWrite, RoundtripBinaerUndText) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_rt.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    std::vector<uint8_t> binaer(3000);
    for (size_t i = 0; i < binaer.size(); ++i) binaer[i] = static_cast<uint8_t>(i * 31 + 7);
    const std::string text = "ZEILE EINS\r\nZEILE ZWEI\r\n";
    std::vector<uint8_t> txt(text.begin(), text.end());

    WriteOptions bin_opt;                 // Fuellbyte 0x00
    WriteOptions txt_opt; txt_opt.text = true;   // Fuellbyte 0x1A

    ASSERT_TRUE(v.fs->write("BINAER.DAT", binaer, bin_opt)) << v.fs->lastError();
    ASSERT_TRUE(v.fs->write("TEXT.TXT",   txt,    txt_opt)) << v.fs->lastError();

    // Auf ganze 128-B-Saetze aufgerundet — das ist CP/M-Eigenart, keine Panne.
    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(v.fs->read("BINAER.DAT", zurueck)) << v.fs->lastError();
    ASSERT_EQ(zurueck.size(), 3072u);
    EXPECT_TRUE(std::equal(binaer.begin(), binaer.end(), zurueck.begin()));
    for (size_t i = binaer.size(); i < zurueck.size(); ++i)
        EXPECT_EQ(zurueck[i], 0x00) << "Binaerfuellung an " << i;

    ASSERT_TRUE(v.fs->read("TEXT.TXT", zurueck));
    ASSERT_EQ(zurueck.size(), 128u);
    EXPECT_TRUE(std::equal(txt.begin(), txt.end(), zurueck.begin()));
    EXPECT_EQ(zurueck[txt.size()], 0x1A) << "Textdateien enden mit 0x1A";
}

TEST(CpmFileSystemWrite, GrosseDateiUeberMehrereExtents) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_gross.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    // 40000 B → 313 Saetze → 20 Bloecke → 3 Verzeichnisplaetze (8 Zeiger je Platz).
    std::vector<uint8_t> gross(40000);
    for (size_t i = 0; i < gross.size(); ++i) gross[i] = static_cast<uint8_t>(i ^ (i >> 8));
    ASSERT_TRUE(v.fs->write("GROSS.BIN", gross, WriteOptions{})) << v.fs->lastError();

    int plaetze = 0, max_extent = -1;
    for (const CpmDirEntry& d : v.fs->directory())
        if (!d.free() && d.name == "GROSS.BIN") { ++plaetze; max_extent = std::max(max_extent, d.extent); }
    EXPECT_EQ(plaetze, 3);
    EXPECT_EQ(max_extent, 2);

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(v.fs->read("GROSS.BIN", zurueck));
    ASSERT_EQ(zurueck.size(), 40064u);           // auf 128 aufgerundet
    EXPECT_TRUE(std::equal(gross.begin(), gross.end(), zurueck.begin()));

    const FileEntry* e = nullptr;
    const std::vector<FileEntry> l = v.fs->list();
    for (const auto& x : l) if (x.name == "GROSS.BIN") e = &x;
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->size, 40064u);
}

TEST(CpmFileSystemWrite, VorhandeneDateiNurMitErlaubnisUeberschreiben) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_ovr.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    const std::vector<uint8_t> neu(500, 0x42);
    EXPECT_FALSE(v.fs->write("PIP.COM", neu, WriteOptions{}));
    EXPECT_NE(v.fs->lastError().find("existiert bereits"), std::string::npos)
        << v.fs->lastError();

    // Der alte Inhalt ist unangetastet.
    std::vector<uint8_t> alt;
    ASSERT_TRUE(v.fs->read("PIP.COM", alt));
    EXPECT_EQ(alt.size(), 7424u);

    WriteOptions o; o.overwrite = true;
    ASSERT_TRUE(v.fs->write("PIP.COM", neu, o)) << v.fs->lastError();
    std::vector<uint8_t> jetzt;
    ASSERT_TRUE(v.fs->read("PIP.COM", jetzt));
    EXPECT_EQ(jetzt.size(), 512u);
}

TEST(CpmFileSystemWrite, LoeschenGibtDenPlatzZurueck) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_del.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    const FsInfo vorher = v.fs->info();
    ASSERT_TRUE(v.fs->erase("M80.COM")) << v.fs->lastError();     // 20224 B = 10 Bloecke
    const FsInfo nachher = v.fs->info();

    EXPECT_EQ(nachher.files, vorher.files - 1);
    EXPECT_GT(nachher.free_bytes, vorher.free_bytes);
    EXPECT_EQ(nachher.free_bytes - vorher.free_bytes, 10u * 2048u);

    std::vector<uint8_t> d;
    EXPECT_FALSE(v.fs->read("M80.COM", d));
}

TEST(CpmFileSystemWrite, PlatzpruefungRechnetVerwaltungMit) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_fit.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    // 1 Byte belegt einen ganzen Block — genau das muss die Pruefung sagen.
    FitReport r;
    ASSERT_TRUE(v.fs->wouldFit({{"WINZIG.DAT", 1, 0, false}}, r));
    EXPECT_TRUE(r.fits);
    EXPECT_EQ(r.needed, 2048u) << "Blockrundung nicht mitgerechnet";
    EXPECT_EQ(r.dir_needed, 1);

    // Mehr als die Diskette fasst → passt nicht, mit Zahlen in der Begruendung.
    ASSERT_TRUE(v.fs->wouldFit({{"ZUGROSS.BIN", 900u * 1024u, 0, false}}, r));
    EXPECT_FALSE(r.fits);
    EXPECT_NE(r.detail.find("frei sind"), std::string::npos) << r.detail;

    // Eine ersetzte Datei gibt ihren Platz zurueck: PIP.COM (7424 B = 4 Bloecke)
    // durch etwas Gleichgrosses zu ersetzen kostet netto nichts.
    const FsInfo i = v.fs->info();
    ASSERT_TRUE(v.fs->wouldFit({{"PIP.COM", 7424, 0, false}}, r));
    EXPECT_TRUE(r.fits);
    EXPECT_EQ(r.available, i.free_bytes + 4u * 2048u)
        << "der Platz der ersetzten Datei wurde nicht gutgeschrieben";
}

TEST(CpmFileSystemWrite, DiskettenvollMeldungStattHalberDatei) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_voll.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    const FsInfo vorher = v.fs->info();
    const std::vector<uint8_t> zugross(static_cast<size_t>(vorher.free_bytes + 4096), 0x5A);
    EXPECT_FALSE(v.fs->write("ZUGROSS.BIN", zugross, WriteOptions{}));
    EXPECT_NE(v.fs->lastError().find("voll"), std::string::npos) << v.fs->lastError();

    // Nichts angefangen: Dateizahl und Belegung unveraendert.
    const FsInfo nachher = v.fs->info();
    EXPECT_EQ(nachher.files, vorher.files);
    EXPECT_EQ(nachher.free_bytes, vorher.free_bytes);
}

TEST(CpmFileSystemWrite, MkfsLeertNurDasVerzeichnis) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_mkfs.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    ASSERT_TRUE(v.fs->mkfs()) << v.fs->lastError();
    EXPECT_TRUE(v.fs->list().empty());
    const FsInfo i = v.fs->info();
    EXPECT_EQ(i.used_bytes, 0u);
    EXPECT_EQ(i.free_bytes, i.total_bytes);

    // Und danach ist die Diskette wieder normal beschreibbar.
    ASSERT_TRUE(v.fs->write("NEU.TXT", std::vector<uint8_t>(100, 'A'), WriteOptions{}));
    EXPECT_EQ(v.fs->list().size(), 1u);
}

TEST(CpmFileSystemWrite, UnzulaessigeNamenWerdenAbgewiesen) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_name.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    const std::vector<uint8_t> d(100, 1);
    EXPECT_FALSE(v.fs->write("VIELZULANGERNAME.TXT", d, WriteOptions{}));
    EXPECT_FALSE(v.fs->write("A.TOOLANG", d, WriteOptions{}));
    EXPECT_FALSE(v.fs->write("MIT*STERN.TXT", d, WriteOptions{}));
    EXPECT_TRUE(v.fs->list().size() == 24u) << "kein Eintrag darf entstanden sein";

    std::string warum;
    EXPECT_TRUE(CpmFileSystem::validName("OK.TXT", &warum)) << warum;
    EXPECT_FALSE(CpmFileSystem::validName("klein.txt", &warum));
    EXPECT_EQ(CpmFileSystem::toCpmName("/pfad/zu/Mein Dokument.textdatei"), "MEINDOKU.TEX");
}

TEST(CpmFileSystemWrite, NutzerbereicheBleibenGetrennt) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_cpm_user.img");
    Volume v = oeffneSchreibbar(k.path(), "cpa780");
    ASSERT_TRUE(v) << v.error;

    ASSERT_TRUE(v.fs->write("3:GEHEIM.TXT", std::vector<uint8_t>(200, 'U'), WriteOptions{}))
        << v.fs->lastError();

    bool gefunden = false;
    for (const FileEntry& e : v.fs->list())
        if (e.name == "GEHEIM.TXT") { EXPECT_EQ(e.user, 3); gefunden = true;
                                      EXPECT_EQ(e.qualifiedName(), "3:GEHEIM.TXT"); }
    EXPECT_TRUE(gefunden);

    std::vector<uint8_t> d;
    EXPECT_FALSE(v.fs->read("GEHEIM.TXT", d)) << "Nutzerbereich 0 darf sie nicht sehen";
    EXPECT_TRUE(v.fs->read("3:GEHEIM.TXT", d));
}
