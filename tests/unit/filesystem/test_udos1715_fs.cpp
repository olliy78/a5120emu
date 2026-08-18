/**
 * @file test_udos1715_fs.cpp
 * @brief GoogleTests für @ref Udos1715FileSystem — UDOS1715/NDOS auf dem PC 1715.
 *
 * ### Woher die Sollwerte stammen
 * Die Erwartungen stehen **wörtlich im Systemhandbuch, das auf der Referenzdiskette
 * selbst liegt** (`doc/original_docs/UDOS1715_Systemhandbuch.txt`, extrahiert als Datei
 * `UDOS.TEXT`) und sind zusätzlich an der Diskette nachgemessen:
 *
 * | Sollwert | Quelle |
 * |---|---|
 * | Descriptor 16H/00, Zeigersektor 16H/02, 9 Records 05 0A 0F 04 09 0E 03 08 0D | Handbuch §1.2.1 |
 * | Belegungsplan 17H/00+01, 180H Byte, Zähler bei 177H/17AH/17BH/17CH | §1.3 |
 * | `UDOS-Sektor = (ID − 1) + Kopf · 16` | §2.6.2 |
 * | Zeigersektor: 125 Adressen, ADRCTR/BCKZGR/FORZGR bei FAH/FCH/FEH | §3.2.3 |
 * | Descriptorfelder, FIRSTBL bei 80H | §3.2.2 |
 * | Datenträger „SYSTEM": 1673 belegt, 887 frei, 67 Dateien | nachgemessen |
 *
 * Der schärfste Test ist @ref Udos1715Belegung.KarteUndDateienStimmenSektorgenau:
 * Er rechnet die Belegung aus **allen** Dateien nach und hält sie gegen den
 * Belegungsplan — in beide Richtungen, ohne Rest.
 *
 * @see core/filesystem/udos/udos1715_fs.h
 * @see doc/udos1715_diskettenformat.md
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/udos/udos1715_fs.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/temp_path.h"

namespace {

constexpr const char* kFixture = "udos1715_640k_pc1715_system.img";
/// @brief Zweite Ausprägung derselben Sitte: die WEGA-Startdiskette des
///        **Robotron P8000** (UDOS 2.2).  Sie unterscheidet sich in einem
///        einzigen Punkt vom PC 1715 — dem Füllmuster hinter dem Belegungsplan.
///
/// Sie liegt als **`.hfe`** vor, nicht als `.img`: 13 Sektoren tragen hinter der
/// Daten-CRC die **Schreibnaht** eines nachträglich überschriebenen Sektors
/// (`4E xx yy yy …`).  Inhaltlich ist das nichts, aber `rawCompatible()` sieht dort
/// zu Recht Bytes außerhalb der Nutzdaten und verweigert `.img` — eine Fixture, die
/// das Werkzeug selbst nicht schreiben würde, wäre ein schlechter Prüfstein.
constexpr const char* kFixtureP8000 = "udos1715_640k_p8000_wega.hfe";

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

/// @brief Eine geöffnete UDOS1715-Diskette (Lebensdauer gebündelt).
struct Diskette {
    std::unique_ptr<DiskImage>          disk;
    std::unique_ptr<SectorSpace>        space;
    std::unique_ptr<Udos1715FileSystem> fs;
    std::string                         error;
    explicit operator bool() const { return fs != nullptr; }
};

Diskette oeffne(const std::string& pfad, bool nur_lesen = true,
                const char* fsname = "udos1715") {
    Diskette d;
    const FsProfile* p = dateisysteme().find(fsname);
    if (!p) { d.error = "Dateisystem unbekannt"; return d; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { d.error = "Format unbekannt"; return d; }

    // .img braucht die Geometrie von aussen — es beschreibt sich nicht selbst.
    d.disk = DiskImage::open(pfad, *f, nur_lesen);
    if (!d.disk) { d.error = "Abbild nicht ladbar: " + pfad; return d; }
    d.space = std::make_unique<SectorSpace>(d.disk->medium(), *f);
    d.fs    = Udos1715FileSystem::mount(*d.space, *p, d.error);
    return d;
}

Diskette referenz() { return oeffne(fixture(kFixture)); }
/// @brief Die P8000-Diskette; als `.hfe` bringt sie ihre Geometrie selbst mit.
Diskette p8000(bool nur_lesen = true, const std::string& pfad = "") {
    Diskette d;
    const FsProfile* p = dateisysteme().find("udos1715");
    if (!p) { d.error = "Dateisystem unbekannt"; return d; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { d.error = "Format unbekannt"; return d; }
    d.disk = DiskImage::open(pfad.empty() ? fixture(kFixtureP8000) : pfad,
                             std::nullopt, nur_lesen);
    if (!d.disk) { d.error = "Abbild nicht ladbar"; return d; }
    d.space = std::make_unique<SectorSpace>(d.disk->medium(), *f);
    d.fs    = Udos1715FileSystem::mount(*d.space, *p, d.error);
    return d;
}

/// @brief Frisch angelegte Diskette in einer Temp-Datei (räumt sich weg).
struct Leerdiskette {
    explicit Leerdiskette(const char* fsname = "udos1715",
                          const std::string& label = "TESTDISK") {
        pfad = k1520test::tempPath("k1520_test_u1715_" + std::to_string(++zaehler_) + ".img");
        const FsProfile* p = dateisysteme().find(fsname);
        const DiskFormat* f = p ? formate().find(p->format) : nullptr;
        if (!f) { fehler = "Katalog unvollstaendig"; return; }
        {
            auto leer = DiskImage::create(pfad, *f, false, f->predominantEncoding());
            if (!leer || !leer->flush()) { fehler = "Leerabbild nicht anlegbar"; return; }
        }
        d.disk = DiskImage::open(pfad, *f, false);
        if (!d.disk) { fehler = "Leerabbild nicht ladbar"; return; }
        d.space = std::make_unique<SectorSpace>(d.disk->medium(), *f);
        d.fs    = Udos1715FileSystem::format(*d.space, *p, label, d.error);
        if (!d.fs) fehler = d.error;
    }
    ~Leerdiskette() { std::error_code ec; std::filesystem::remove(pfad, ec); }

    Udos1715FileSystem& fs() { return *d.fs; }
    explicit operator bool() const { return d.fs != nullptr; }

    Diskette    d;
    std::string pfad;
    std::string fehler;
    static inline int zaehler_ = 0;
};

/// @brief Alle Sektoren, die die Dateien der Diskette beanspruchen — unabhängig
///        nachgerechnet, nicht aus dem Belegungsplan gelesen.
std::set<std::pair<int, int>> belegungAusDenDateien(Udos1715FileSystem& fs,
                                                    const FsProfile& prof) {
    std::set<std::pair<int, int>> s;
    auto merke = [&](UdosPointer p) { s.insert({p.sector_index, p.track}); };

    for (const UdosDirEntry& e : fs.directory()) {
        UdosFileHeader hdr;
        if (!fs.readDescriptor(e.header, hdr)) continue;
        merke(e.header);

        std::vector<UdosPointer>          adressen;
        std::vector<Udos1715PointerBlock> bloecke;
        if (!fs.pointerBlocks(hdr, adressen, bloecke)) continue;

        UdosPointer zb = hdr.firstbl;
        for (const Udos1715PointerBlock& b : bloecke) { merke(zb); zb = b.forward; }

        const uint32_t je_rec = hdr.record_len <= 256 ? 1u : hdr.record_len / 256u;
        for (size_t i = 1; i < adressen.size(); ++i)
            for (uint32_t k = 0; k < je_rec; ++k)
                merke(UdosPointer{static_cast<uint8_t>(adressen[i].sector_index + k),
                                  adressen[i].track});
    }
    // Der Belegungsplan (Handbuch §1.2.1) gehört ebenfalls zu den festen Sektoren;
    // ebenso Spur 0 einer Systemdiskette — die zählen wir aus der Karte selbst dazu.
    s.insert({0, prof.bitmap_track});
    s.insert({1, prof.bitmap_track});
    return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Erkennung und Grundzüge
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715, ReferenzdisketteWirdErkannt) {
    const FsProfile* p = dateisysteme().find("udos1715");
    ASSERT_NE(p, nullptr);
    const DiskFormat* f = formate().find(p->format);
    ASSERT_NE(f, nullptr);

    auto disk = DiskImage::open(fixture(kFixture), *f, true);
    ASSERT_TRUE(disk);
    SectorSpace raum(disk->medium(), *f);

    std::string warum;
    EXPECT_TRUE(Udos1715FileSystem::looksLikeUdos1715(raum, *p, &warum)) << warum;
}

TEST(Udos1715, EineZdosDisketteWirdNichtVerwechselt) {
    // Gegenprobe: die A5120-UDOS-Diskette (26×128, ZDOS) darf nicht als UDOS1715
    // durchgehen — schon die Sektorgroesse passt nicht, und ihre Karte traegt den
    // ZDOS-Nachlauf statt Nullen.
    const FsProfile* p = dateisysteme().find("udos1715");
    ASSERT_NE(p, nullptr);
    const DiskFormat* f = formate().find(p->format);
    ASSERT_NE(f, nullptr);

    auto disk = DiskImage::open(fixture("udos_boot_scp.hfe"), std::nullopt, true);
    ASSERT_TRUE(disk);
    SectorSpace raum(disk->medium(), *f);
    std::string warum;
    EXPECT_FALSE(Udos1715FileSystem::looksLikeUdos1715(raum, *p, &warum));
    EXPECT_FALSE(warum.empty());
}

TEST(Udos1715, DatentraegerAngabenStimmen) {
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    const FsInfo i = d.fs->info();
    EXPECT_EQ(i.label, "SYSTEM");
    EXPECT_EQ(i.files, 67);
    // 80 Spuren à 32 Sektoren à 256 B (Handbuch §1.2).
    EXPECT_EQ(i.total_bytes, 80u * 32u * 256u);
    EXPECT_EQ(i.free_bytes,  887u * 256u);
    EXPECT_EQ(i.used_bytes, 1673u * 256u);
    for (const auto& w : i.warnings) ADD_FAILURE() << "unerwartete Warnung: " << w;

    EXPECT_EQ(d.fs->sectorsPerTrack(), 32) << "die Spur ist der ganze Zylinder (§1.1)";
    EXPECT_EQ(d.fs->trackCount(), 80);
}

TEST(Udos1715, BelegungsplanTraegtBeideZaehlerEcht) {
    // §3.1: anders als bei ZDOS ist der „belegt"-Zaehler kein Rechenrest aus einer
    // Konstanten, sondern die wirkliche Zahl.
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;
    const UdosBitmap& b = d.fs->bitmap();

    EXPECT_EQ(b.sitte(), UdosMapSitte::Ndos1715);
    EXPECT_EQ(b.sectorsPerTrack(), 0x20);
    EXPECT_EQ(b.trackCount(),      0x50);
    EXPECT_EQ(b.storedUsed(), 1673);
    EXPECT_EQ(b.storedFree(),  887);
    EXPECT_EQ(b.countUsed(),  1673);
    EXPECT_EQ(b.countFree(),   887);
    EXPECT_EQ(b.storedUsed() + b.storedFree(), 80 * 32);
}

// ─────────────────────────────────────────────────────────────────────────────
// Feste Lage der Systemstrukturen (Handbuch §1.2.1)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715, VerzeichnisLiegtWoDasHandbuchEsSagt) {
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    const UdosPointer desc = d.fs->directoryDescriptor();
    EXPECT_EQ(desc.sector_index, 0x00);
    EXPECT_EQ(desc.track,        0x16);

    UdosFileHeader hdr;
    ASSERT_TRUE(d.fs->readDescriptor(desc, hdr));
    EXPECT_EQ(hdr.typeName(), "D");
    EXPECT_EQ(hdr.propertyLetters(), "WELS");
    EXPECT_EQ(hdr.record_len, 256);
    EXPECT_EQ(hdr.record_count, 9);
    EXPECT_EQ(hdr.firstbl.sector_index, 0x02) << "Zeigersektor der DIRECTORY";
    EXPECT_EQ(hdr.firstbl.track,        0x16);

    std::vector<UdosPointer> kette;
    ASSERT_TRUE(d.fs->recordChain(hdr, kette));
    const std::vector<uint8_t> soll = {0x05, 0x0A, 0x0F, 0x04, 0x09, 0x0E, 0x03, 0x08, 0x0D};
    ASSERT_EQ(kette.size(), soll.size());
    for (size_t i = 0; i < soll.size(); ++i) {
        EXPECT_EQ(kette[i].sector_index, soll[i]) << "Datenrecord " << i + 1;
        EXPECT_EQ(kette[i].track, 0x16);
    }
}

TEST(Udos1715, ErsteEintragungDesErstenZeigersektorsIstDerDescriptor) {
    // §6 — daran haengt die ganze Kettenauswertung.
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    int geprueft = 0;
    for (const UdosDirEntry& e : d.fs->directory()) {
        UdosFileHeader hdr;
        ASSERT_TRUE(d.fs->readDescriptor(e.header, hdr)) << e.name;
        std::vector<UdosPointer>          adressen;
        std::vector<Udos1715PointerBlock> bloecke;
        ASSERT_TRUE(d.fs->pointerBlocks(hdr, adressen, bloecke)) << e.name;
        ASSERT_FALSE(adressen.empty()) << e.name;
        EXPECT_EQ(adressen.front(), e.header) << e.name << ": Stelle 0 ist der Descriptor";
        EXPECT_EQ(bloecke.front().back, e.header) << e.name << ": BCKZGR zeigt zurueck";
        EXPECT_EQ(bloecke.back().forward, (UdosPointer{})) << e.name << ": FORZGR ist FFFF";
        EXPECT_EQ(adressen.size() - 1, hdr.record_count) << e.name;
        ++geprueft;
    }
    EXPECT_EQ(geprueft, 67);
}

TEST(Udos1715, MehrAls124RecordsBrauchenZweiZeigersektoren) {
    // UDOS.TEXT hat 206 Records: 124 im ersten Zeigersektor (Stelle 0 ist der
    // Descriptor) + 82 im zweiten (§6).
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    UdosPointer desc{0xFF, 0xFF};
    for (const UdosDirEntry& e : d.fs->directory())
        if (e.name == "UDOS.TEXT") desc = e.header;
    ASSERT_FALSE(desc.end()) << "UDOS.TEXT steht nicht im Verzeichnis";

    UdosFileHeader hdr;
    ASSERT_TRUE(d.fs->readDescriptor(desc, hdr));
    EXPECT_EQ(hdr.record_count, 206);
    EXPECT_EQ(hdr.typeName(), "A");

    std::vector<UdosPointer>          adressen;
    std::vector<Udos1715PointerBlock> bloecke;
    ASSERT_TRUE(d.fs->pointerBlocks(hdr, adressen, bloecke));
    ASSERT_EQ(bloecke.size(), 2u);
    EXPECT_EQ(bloecke[0].entries.size(), kUdos1715PointersPerBlock);
    EXPECT_EQ(bloecke[1].entries.size(), 82u);
    EXPECT_EQ(bloecke[1].back, hdr.firstbl);
}

// ─────────────────────────────────────────────────────────────────────────────
// Der schärfste Test: Karte gegen die Dateien
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715Belegung, KarteUndDateienStimmenSektorgenau) {
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;
    const FsProfile* prof = dateisysteme().find("udos1715");
    ASSERT_NE(prof, nullptr);

    std::set<std::pair<int, int>> aus_karte;
    for (int t = 0; t < d.fs->trackCount(); ++t)
        for (int s = 0; s < d.fs->sectorsPerTrack(); ++s)
            if (d.fs->bitmap().used(static_cast<uint8_t>(t), static_cast<uint8_t>(s + 1)))
                aus_karte.insert({s, t});

    std::set<std::pair<int, int>> aus_dateien = belegungAusDenDateien(*d.fs, *prof);
    // Spur 0 traegt auf dieser Systemdiskette Urlader und BFOS (§2) — sie gehoert
    // keiner Datei und wird deshalb aus der Karte uebernommen.
    for (int s = 0; s < 16; ++s) aus_dateien.insert({s, 0});

    std::vector<std::pair<int, int>> nur_karte, nur_dateien;
    std::set_difference(aus_karte.begin(), aus_karte.end(),
                        aus_dateien.begin(), aus_dateien.end(),
                        std::back_inserter(nur_karte));
    std::set_difference(aus_dateien.begin(), aus_dateien.end(),
                        aus_karte.begin(), aus_karte.end(),
                        std::back_inserter(nur_dateien));

    EXPECT_TRUE(nur_karte.empty())
        << nur_karte.size() << " Sektoren stehen als belegt in der Karte, gehoeren aber "
           "keiner Datei (erster: Spur " << (nur_karte.empty() ? -1 : nur_karte[0].second)
        << " Sektor " << (nur_karte.empty() ? -1 : nur_karte[0].first) << ")";
    EXPECT_TRUE(nur_dateien.empty())
        << nur_dateien.size() << " Sektoren gehoeren einer Datei, stehen aber als frei "
           "in der Karte";
    EXPECT_EQ(aus_karte.size(), 1673u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lesen
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715, VerzeichnisNennt67DateienUndSpiegeltSecret) {
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    const std::vector<FileEntry> alle = d.fs->list();
    ASSERT_EQ(alle.size(), 67u);

    // Das SECRET-Bit steht doppelt: im Verzeichnisflag und im Descriptor (§4).
    for (const FileEntry& e : alle)
        EXPECT_EQ(e.hidden, e.attributes.find('S') != std::string::npos)
            << e.name << ": Verzeichnisflag und Eigenschaftsbyte widersprechen sich";

    // listNames() liefert dieselben Namen ohne die Descriptoren zu lesen.
    const std::vector<FileEntry> nur_namen = d.fs->listNames();
    ASSERT_EQ(nur_namen.size(), alle.size());
    for (size_t i = 0; i < alle.size(); ++i) {
        EXPECT_EQ(nur_namen[i].name, alle[i].name);
        EXPECT_FALSE(nur_namen[i].details_loaded);
        EXPECT_TRUE(alle[i].details_loaded);
    }
}

TEST(Udos1715, TypUndSubtypWerdenAlsBitfeldGelesen) {
    // §5.1: Bit 7 = P, Bits 0-3 = Subtyp.  `NDOS` und `ZDOS` sind P1.
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    std::map<std::string, std::string> typ;
    for (const FileEntry& e : d.fs->list()) typ[e.name] = e.type;

    EXPECT_EQ(typ["DIRECTORY"],   "D");
    EXPECT_EQ(typ["NDOS"],        "P1");
    EXPECT_EQ(typ["ZDOS"],        "P1");
    EXPECT_EQ(typ["OS"],          "P");
    EXPECT_EQ(typ["UDOS.TEXT"],   "A");
    EXPECT_EQ(typ["HELP.DAT.00"], "A");

    // Umkehrung — dieselbe Tabelle in beide Richtungen.
    EXPECT_EQ(udosTypeByte("P1", false), 0x81);
    EXPECT_EQ(udosTypeName(0x81), "P1");
    EXPECT_EQ(udosTypeByte("A", false), 0x20);
    EXPECT_EQ(udosTypeName(0x40), "D");
}

TEST(Udos1715, HandbuchLaesstSichLesen) {
    // Die Datei UDOS.TEXT IST das Handbuch, aus dem alle Sollwerte hier stammen —
    // sie zu lesen prueft den Lesepfad ueber zwei Zeigersektoren und 206 Records.
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    std::vector<uint8_t> inhalt;
    ASSERT_TRUE(d.fs->read("UDOS.TEXT", inhalt)) << d.fs->lastError();
    // 205 volle Records + 194 Byte im letzten (§5.3).
    EXPECT_EQ(inhalt.size(), 205u * 256u + 194u);
    const std::string anfang(inhalt.begin(), inhalt.begin() + 80);
    EXPECT_NE(anfang.find('*'), std::string::npos);
    const std::string ganz(inhalt.begin(), inhalt.end());
    EXPECT_NE(ganz.find("Universelles Disk-Operations-System"), std::string::npos);
    EXPECT_NE(ganz.find("Zeigersektor"), std::string::npos);
}

TEST(Udos1715, RecordUeberDieKopfgrenzeWirdRichtigZusammengesetzt) {
    // §5.3: ein 1024-B-Record belegt 4 Sektoren am Stueck — und darf dabei die
    // KOPFgrenze ueberschreiten, denn die Spur ist der ganze Zylinder.  `CAT` hat
    // genau so einen Record (ab Sektor 0DH über 10H hinweg).
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    UdosPointer desc{0xFF, 0xFF};
    for (const UdosDirEntry& e : d.fs->directory())
        if (e.name == "CAT") desc = e.header;
    ASSERT_FALSE(desc.end());

    UdosFileHeader hdr;
    ASSERT_TRUE(d.fs->readDescriptor(desc, hdr));
    EXPECT_EQ(hdr.record_len, 1024);

    std::vector<UdosPointer> kette;
    ASSERT_TRUE(d.fs->recordChain(hdr, kette));
    bool ueber_die_kopfgrenze = false;
    for (const UdosPointer& p : kette)
        if (p.sector_index < 16 && p.sector_index + 4 > 16) ueber_die_kopfgrenze = true;
    EXPECT_TRUE(ueber_die_kopfgrenze) << "erwartet: ein Record laeuft von Kopf 0 nach Kopf 1";

    std::vector<uint8_t> inhalt;
    ASSERT_TRUE(d.fs->read("CAT", inhalt)) << d.fs->lastError();
    EXPECT_EQ(inhalt.size(), 4u * 1024u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Anlegen und Schreiben
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715Schreiben, FrischeDisketteHatDie13FestenSektoren) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;

    const FsInfo i = l.fs().info();
    EXPECT_EQ(i.label, "TESTDISK");
    EXPECT_EQ(i.files, 1) << "nur die Verzeichnisdatei selbst";
    // §1.2.1: 13 Sektoren sind auf JEDER formatierten Diskette fest belegt.
    EXPECT_EQ(l.fs().bitmap().countUsed(), 13);
    EXPECT_EQ(l.fs().bitmap().countFree(), 80 * 32 - 13);
    for (const auto& w : i.warnings) ADD_FAILURE() << "unerwartete Warnung: " << w;

    const std::vector<FileEntry> alle = l.fs().list();
    ASSERT_EQ(alle.size(), 1u);
    EXPECT_EQ(alle[0].name, "DIRECTORY");
    EXPECT_EQ(alle[0].type, "D");
    EXPECT_EQ(alle[0].attributes, "WELS");
    EXPECT_TRUE(alle[0].hidden);
}

TEST(Udos1715Schreiben, SchreibenLesenLoeschenGehtRundUndLaesstDieKarteHeil) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;
    const FsProfile* prof = dateisysteme().find("udos1715");
    ASSERT_NE(prof, nullptr);

    const int frei_vorher = l.fs().bitmap().countFree();

    // Drei Dateien mit unterschiedlichem Zuschnitt: eine, die genau aufgeht, eine mit
    // Rest im letzten Record, und eine mit ZWEI Zeigersektoren (mehr als 124 Records).
    std::vector<uint8_t> glatt(4 * 256, 0xAB);
    std::vector<uint8_t> krumm(700, 0xCD);
    std::vector<uint8_t> lang(200 * 256 + 7, 0x5A);
    for (size_t i = 0; i < lang.size(); ++i) lang[i] = static_cast<uint8_t>(i * 7);

    WriteOptions wo;
    wo.udos_type = "P1";
    wo.udos_properties = "WS";
    wo.udos_entry = 0x4000;
    ASSERT_TRUE(l.fs().write("GLATT", glatt, wo)) << l.fs().lastError();
    WriteOptions wa;
    wa.udos_type = "A";
    ASSERT_TRUE(l.fs().write("KRUMM.TXT", krumm, wa)) << l.fs().lastError();
    ASSERT_TRUE(l.fs().write("LANG.DAT", lang, wa)) << l.fs().lastError();

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(l.fs().read("GLATT", zurueck)) << l.fs().lastError();
    EXPECT_EQ(zurueck, glatt);
    ASSERT_TRUE(l.fs().read("KRUMM.TXT", zurueck)) << l.fs().lastError();
    EXPECT_EQ(zurueck, krumm);
    ASSERT_TRUE(l.fs().read("LANG.DAT", zurueck)) << l.fs().lastError();
    EXPECT_EQ(zurueck, lang);

    // Kopfsektorangaben kommen zurueck, wie sie hineingingen.
    for (const FileEntry& e : l.fs().list()) {
        if (e.name != "GLATT") continue;
        EXPECT_EQ(e.type, "P1");
        EXPECT_EQ(e.attributes, "WS");
        EXPECT_EQ(e.entry_addr, 0x4000);
        EXPECT_TRUE(e.hidden) << "SECRET muss im Verzeichnis gespiegelt sein";
        EXPECT_EQ(e.size, glatt.size());
    }

    // Der Belegungsplan deckt sich sektorgenau mit dem, was die Dateien belegen.
    {
        std::set<std::pair<int, int>> karte;
        for (int t = 0; t < l.fs().trackCount(); ++t)
            for (int s = 0; s < l.fs().sectorsPerTrack(); ++s)
                if (l.fs().bitmap().used(static_cast<uint8_t>(t), static_cast<uint8_t>(s + 1)))
                    karte.insert({s, t});
        EXPECT_EQ(karte, belegungAusDenDateien(l.fs(), *prof));
    }

    // Loeschen gibt ALLES zurueck — Descriptor, Zeigersektoren und Datenrecords.
    ASSERT_TRUE(l.fs().erase("GLATT")) << l.fs().lastError();
    ASSERT_TRUE(l.fs().erase("KRUMM.TXT")) << l.fs().lastError();
    ASSERT_TRUE(l.fs().erase("LANG.DAT")) << l.fs().lastError();
    EXPECT_EQ(l.fs().list().size(), 1u);
    EXPECT_EQ(l.fs().bitmap().countFree(), frei_vorher)
        << "nach dem Loeschen muss jeder Sektor wieder frei sein";
}

TEST(Udos1715Schreiben, DasVerzeichnisWaechstUeberSeineNeunRecordsHinaus) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;

    const std::vector<uint8_t> inhalt(64, 0x11);
    for (int i = 0; i < 200; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "DATEI.NUMMER.%04d", i);
        ASSERT_TRUE(l.fs().write(name, inhalt, WriteOptions{}))
            << name << ": " << l.fs().lastError();
    }
    EXPECT_EQ(l.fs().list().size(), 201u) << "200 Dateien + DIRECTORY";

    UdosFileHeader hdr;
    ASSERT_TRUE(l.fs().readDescriptor(l.fs().directoryDescriptor(), hdr));
    EXPECT_GT(hdr.record_count, 9)
        << "das Verzeichnis muss gewachsen sein (§4)";

    // Und danach ist immer noch alles lesbar.
    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(l.fs().read("DATEI.NUMMER.0199", zurueck)) << l.fs().lastError();
    EXPECT_EQ(zurueck, inhalt);
}

TEST(Udos1715Schreiben, RecordlaengeWirdMitgeschriebenUndGeprueft) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;

    const std::vector<uint8_t> daten(3000, 0x77);
    WriteOptions wo;
    wo.udos_record_len = 1024;
    ASSERT_TRUE(l.fs().write("GROSS", daten, wo)) << l.fs().lastError();

    UdosPointer desc{0xFF, 0xFF};
    for (const UdosDirEntry& e : l.fs().directory())
        if (e.name == "GROSS") desc = e.header;
    ASSERT_FALSE(desc.end());
    UdosFileHeader hdr;
    ASSERT_TRUE(l.fs().readDescriptor(desc, hdr));
    EXPECT_EQ(hdr.record_len, 1024);
    EXPECT_EQ(hdr.block_len, 1024) << "bei 1024 ist die Blocklaenge deren Kopie";
    EXPECT_EQ(hdr.record_count, 3);
    EXPECT_EQ(hdr.bytes_in_last, 3000 - 2 * 1024);

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(l.fs().read("GROSS", zurueck));
    EXPECT_EQ(zurueck, daten);

    // Eine Recordlaenge, die NDOS nicht kennt, wird abgewiesen — nicht stillschweigend
    // gerundet.
    WriteOptions falsch;
    falsch.udos_record_len = 384;
    EXPECT_FALSE(l.fs().write("SCHIEF", daten, falsch));
    EXPECT_NE(l.fs().lastError().find("384"), std::string::npos) << l.fs().lastError();
}

TEST(Udos1715Schreiben, NamenMuessenMitEinemBuchstabenBeginnen) {
    // Handbuch §3.1 — anders als bei ZDOS ist das eine echte Regel.
    std::string warum;
    EXPECT_TRUE(Udos1715FileSystem::validName("HELP.DAT.00", &warum)) << warum;
    EXPECT_TRUE(Udos1715FileSystem::validName("A", &warum)) << warum;
    EXPECT_FALSE(Udos1715FileSystem::validName("1TEST", &warum));
    EXPECT_FALSE(Udos1715FileSystem::validName(".PROFIL", &warum));
    EXPECT_FALSE(Udos1715FileSystem::validName("MIT LEER", &warum));
    EXPECT_FALSE(Udos1715FileSystem::validName(std::string(33, 'A'), &warum));
}

TEST(Udos1715Schreiben, SystemspurenBleibenTabu) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;

    // Die Diskette bis zum Rand fuellen und danach pruefen, dass Verzeichnis- und
    // Belegungsplanspur unangetastet blieben.
    const std::vector<uint8_t> brocken(16 * 1024, 0x33);
    int geschrieben = 0;
    for (int i = 0; i < 200; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "F%03d", i);
        if (!l.fs().write(name, brocken, WriteOptions{})) break;
        ++geschrieben;
    }
    EXPECT_GT(geschrieben, 30) << "die Diskette fasst deutlich mehr";
    EXPECT_NE(l.fs().lastError().find("voll"), std::string::npos) << l.fs().lastError();

    // Spur 17H traegt weiterhin NUR die beiden Sektoren des Belegungsplans …
    for (uint8_t s = 2; s < 32; ++s)
        EXPECT_FALSE(l.fs().bitmap().used(0x17, static_cast<uint8_t>(s + 1)))
            << "Spur 17H Sektor " << int(s) << " wurde vergeben";
    // … und das Verzeichnis ist noch lesbar.
    EXPECT_GE(l.fs().list().size(), static_cast<size_t>(geschrieben));
}

// ─────────────────────────────────────────────────────────────────────────────
// Speichersegmente — die ganze Liste, nicht nur die ersten zwei
// ─────────────────────────────────────────────────────────────────────────────

TEST(Udos1715Segmente, TextformGehtInBeideRichtungen) {
    std::vector<std::pair<uint16_t, uint16_t>> segs;
    std::string warum;
    ASSERT_TRUE(udosParseSegments("4400+0041 8442+0026 876E+3EF1", segs, &warum)) << warum;
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0], std::make_pair(uint16_t(0x4400), uint16_t(0x0041)));
    EXPECT_EQ(segs[2], std::make_pair(uint16_t(0x876E), uint16_t(0x3EF1)));
    EXPECT_EQ(udosFormatSegments(segs), "4400+0041 8442+0026 876E+3EF1");

    // Komma und ':' sind ebenso erlaubt — das Beiblatt trennt mit Komma, die alte
    // CLI-Schreibweise war ANFANG:LAENGE.
    std::vector<std::pair<uint16_t, uint16_t>> b;
    ASSERT_TRUE(udosParseSegments("2600:1591,4000+0200", b, &warum)) << warum;
    ASSERT_EQ(b.size(), 2u);
    EXPECT_EQ(b[0], std::make_pair(uint16_t(0x2600), uint16_t(0x1591)));

    EXPECT_TRUE(udosParseSegments("", segs, &warum));
    EXPECT_TRUE(segs.empty());
    EXPECT_FALSE(udosParseSegments("4400", segs, &warum));
    EXPECT_FALSE(udosParseSegments("XYZ+1", segs, &warum));
}

TEST(Udos1715Segmente, MehrsegmentigeProgrammeWerdenGanzGelesen) {
    // Handbuch §3.2.2: „mehrere Segmente moeglich; abgeschlossen mit 00 00 00 00".
    Diskette d = referenz();
    ASSERT_TRUE(d) << d.error;

    std::map<std::string, std::string> segs;
    for (const FileEntry& e : d.fs->list()) segs[e.name] = e.segments;

    EXPECT_EQ(segs["ZLINK"],  "4000+06A7 62A7+0002 71E9+060B 7AF5+01C9 7FBE+0001 843F+4ABE");
    EXPECT_EQ(segs["IMAGER"], "4400+0041 8442+0026 876E+3EF1");
    EXPECT_EQ(segs["EDI"],    "4000+0D71 4FBE+19E3");
    EXPECT_EQ(segs["STATUS"], "4000+032D") << "eine Datei mit genau einem Segment";

    // Typ A/B/D fuehren dort Anwenderinhalt, keine Segmente (Handbuch §3.2.2:
    // „2AH…7FH nur bei P-Dateien vom System verwendet, sonst frei fuer Anwender").
    EXPECT_EQ(segs["UDOS.TEXT"], "") << "Typ A darf nicht als Segmentliste gelesen werden";
    EXPECT_EQ(segs["DIRECTORY"], "");
}

TEST(Udos1715Segmente, SechsSegmenteUeberlebenDasZurueckschreiben) {
    // Der eigentliche Punkt: mit nur Segment 1 + den vier Bytes dahinter kaeme
    // `ZLINK` mit zwei statt sechs Segmenten zurueck — und startete nicht mehr.
    Diskette quelle = referenz();
    ASSERT_TRUE(quelle) << quelle.error;
    std::vector<uint8_t> inhalt;
    ASSERT_TRUE(quelle.fs->read("ZLINK", inhalt)) << quelle.fs->lastError();

    std::string soll;
    uint16_t reclen = 0;
    for (const FileEntry& e : quelle.fs->list())
        if (e.name == "ZLINK") { soll = e.segments; reclen = e.record_len; }
    ASSERT_EQ(std::count(soll.begin(), soll.end(), '+'), 6) << soll;

    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;
    WriteOptions wo;
    wo.udos_type       = "P";
    wo.udos_record_len = reclen;
    wo.udos_segments   = soll;
    ASSERT_TRUE(l.fs().write("ZLINK", inhalt, wo)) << l.fs().lastError();

    for (const FileEntry& e : l.fs().list())
        if (e.name == "ZLINK") {
            EXPECT_EQ(e.segments, soll);
            EXPECT_EQ(e.segment_start, 0x4000) << "Segment 1 bleibt auch einzeln lesbar";
        }

    // Und ueber setAttributes ebenso.
    UdosAttrs a;
    a.set_segments = true;
    a.segments     = "1000+0100 2000+0200";
    ASSERT_TRUE(l.fs().setAttributes("ZLINK", a)) << l.fs().lastError();
    for (const FileEntry& e : l.fs().list())
        if (e.name == "ZLINK") EXPECT_EQ(e.segments, "1000+0100 2000+0200");
}

TEST(Udos1715Segmente, ZuVieleSegmenteWerdenAbgewiesen) {
    Leerdiskette l;
    ASSERT_TRUE(l) << l.fehler;

    std::string zu_viele;
    for (size_t i = 0; i <= kUdosMaxSegments; ++i) {
        if (!zu_viele.empty()) zu_viele += ' ';
        zu_viele += "1000+0010";
    }
    WriteOptions wo;
    wo.udos_type     = "P";
    wo.udos_segments = zu_viele;
    EXPECT_FALSE(l.fs().write("ZUVIEL", std::vector<uint8_t>(256, 1), wo));
    EXPECT_NE(l.fs().lastError().find("Segmente"), std::string::npos) << l.fs().lastError();
}

// ─────────────────────────────────────────────────────────────────────────────
// Der Robotron P8000 — dieselbe Sitte, ein anderes Füllmuster
// ─────────────────────────────────────────────────────────────────────────────
//
// Die WEGA-Startdiskette des P8000 (UDOS 2.2) trägt Descriptoren, Zeigersektoren
// und Verzeichnis genau dort, wo das PC-1715-Handbuch sie beschreibt.  Sie war
// trotzdem unlesbar, weil ihr Formatierer den Platz zwischen Belegungsplan und
// Zählern nicht mit `00` füllt, sondern mit dem **0x77-Rest der ZDOS-Sitte**.
// Die Trennung zu ZDOS trägt deshalb nicht mehr dieses Füllmuster, sondern der
// Zählerabgleich (`belegt + frei == Kapazität`) und die 0x33/0xF7 davor — die
// liegen bei ZDOS vor Offset 344 und damit hier mitten im Belegungsplan.

TEST(Udos1715P8000, WegaStartdisketteWirdErkannt) {
    const FsProfile* p = dateisysteme().find("udos1715");
    ASSERT_NE(p, nullptr);
    const DiskFormat* f = formate().find(p->format);
    ASSERT_NE(f, nullptr);

    auto disk = DiskImage::open(fixture(kFixtureP8000), std::nullopt, true);
    ASSERT_TRUE(disk);
    SectorSpace raum(disk->medium(), *f);

    std::string warum;
    EXPECT_TRUE(Udos1715FileSystem::looksLikeUdos1715(raum, *p, &warum)) << warum;

    // Und sie ist NICHT als rohes Sektorabbild darstellbar: 13 Sektoren tragen die
    // Schreibnaht eines ueberschriebenen Sektors hinter der Daten-CRC.  Das ist zwar
    // kein Inhalt, aber das Werkzeug kann es nicht wissen — und darf es nicht
    // stillschweigend wegwerfen.
    EXPECT_FALSE(disk->medium().rawCompatible());
    EXPECT_NE(disk->medium().rawIncompatibleReason(), "");
}

TEST(Udos1715P8000, DatentraegerAngabenStimmen) {
    Diskette d = p8000();
    ASSERT_TRUE(d) << d.error;

    const FsInfo i = d.fs->info();
    EXPECT_EQ(i.label, "WEGA-STARTDISKETTE");
    EXPECT_EQ(i.files, 42);
    EXPECT_EQ(i.total_bytes, 80u * 32u * 256u);
    EXPECT_EQ(i.used_bytes, 2533u * 256u);
    EXPECT_EQ(i.free_bytes,   27u * 256u);
    for (const auto& w : i.warnings) ADD_FAILURE() << "unerwartete Warnung: " << w;
}

TEST(Udos1715P8000, HinterDemBelegungsplanStehtDerZdosNachlauf) {
    // Das ist der Befund, an dem die Diskette scheiterte — er soll sichtbar bleiben.
    Diskette d = p8000();
    ASSERT_TRUE(d) << d.error;
    const std::vector<uint8_t>& roh = d.fs->bitmap().raw();
    ASSERT_EQ(roh.size(), kUdosBitmapBytes);

    EXPECT_EQ(d.fs->bitmap().sitte(), UdosMapSitte::Ndos1715);
    for (size_t i = 344; i < 348; ++i) EXPECT_EQ(roh[i], 0x00) << "Offset " << i;
    for (size_t i = 348; i < 375; ++i) EXPECT_EQ(roh[i], 0x77) << "Offset " << i;
    EXPECT_NE(roh[377], 0x00) << "Byte 179H ist beim P8000 belegt — es darf nicht "
                                 "als Erkennungsmerkmal dienen";

    // Und die Zähler sind trotzdem echt — daran haengt die Trennung zu ZDOS.
    EXPECT_EQ(d.fs->bitmap().storedUsed(), 2533);
    EXPECT_EQ(d.fs->bitmap().storedFree(),   27);
    EXPECT_EQ(d.fs->bitmap().countUsed(),  2533);
    EXPECT_EQ(d.fs->bitmap().countFree(),    27);
}

TEST(Udos1715P8000, KarteUndDateienStimmenSektorgenau) {
    Diskette d = p8000();
    ASSERT_TRUE(d) << d.error;
    const FsProfile* prof = dateisysteme().find("udos1715");
    ASSERT_NE(prof, nullptr);

    std::set<std::pair<int, int>> aus_karte;
    for (int t = 0; t < d.fs->trackCount(); ++t)
        for (int s = 0; s < d.fs->sectorsPerTrack(); ++s)
            if (d.fs->bitmap().used(static_cast<uint8_t>(t), static_cast<uint8_t>(s + 1)))
                aus_karte.insert({s, t});

    std::set<std::pair<int, int>> aus_dateien = belegungAusDenDateien(*d.fs, *prof);

    // Was die Karte darueber hinaus sperrt, ist der SYSTEMBEREICH — und der reicht
    // beim P8000 weiter als beim PC 1715.  Reserviert ist jeweils der ganze **Kopf 0**
    // (Sektoren 0…15) von vier Spuren; Kopf 1 derselben Spuren traegt gewoehnliche
    // Dateidaten.  Nachgemessen an der Diskette, nicht angenommen:
    //   Spur  0  Urlader und BFOS  — mit EINER Luecke: Sektor 1 steht als frei
    //                                (das Medium ist dort noch unbeschrieben)
    //   Spur 21  Bootspur (UDOS liest sie beim Kaltstart, s. doc/udos_diskettenformat.md)
    //   Spur 22  DIRECTORY-Spur    — ganz gesperrt, nicht nur die 11 benutzten Sektoren
    //   Spur 23  Belegungsplanspur — ebenso
    for (int s = 0; s < 16; ++s) {
        if (s != 1) aus_dateien.insert({s, 0});
        aus_dateien.insert({s, 21});
        aus_dateien.insert({s, 22});
        aus_dateien.insert({s, 23});
    }

    std::vector<std::pair<int, int>> nur_karte, nur_dateien;
    std::set_difference(aus_karte.begin(), aus_karte.end(),
                        aus_dateien.begin(), aus_dateien.end(),
                        std::back_inserter(nur_karte));
    std::set_difference(aus_dateien.begin(), aus_dateien.end(),
                        aus_karte.begin(), aus_karte.end(),
                        std::back_inserter(nur_dateien));

    EXPECT_TRUE(nur_karte.empty())
        << nur_karte.size() << " Sektoren stehen als belegt in der Karte, gehoeren aber "
           "keiner Datei und keinem Systembereich (erster: Spur "
        << (nur_karte.empty() ? -1 : nur_karte[0].second)
        << " Sektor " << (nur_karte.empty() ? -1 : nur_karte[0].first) << ")";
    EXPECT_TRUE(nur_dateien.empty())
        << nur_dateien.size() << " Sektoren gehoeren einer Datei, stehen aber als frei "
           "in der Karte";
    EXPECT_EQ(aus_karte.size(), 2533u);
}

TEST(Udos1715P8000, SchreibenLesenLoeschenLaesstDenNachlaufStehen) {
    // Rundlauf auf der echten Diskette: die 27 freien Sektoren reichen fuer eine
    // kleine Datei.  Danach muss der Belegungsplan wieder der alte sein — samt
    // seinem 0x77-Nachlauf, den wir beim Zurueckschreiben nicht platt buegeln duerfen.
    // Auf einer Kopie — die Fixture wird schreibend geoeffnet.
    const std::string kopie = k1520test::tempPath("k1520_test_p8000_wega.hfe");
    std::error_code ec;
    std::filesystem::copy_file(fixture(kFixtureP8000), kopie,
                               std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << ec.message();
    struct Weg { std::string p; ~Weg() { std::error_code e; std::filesystem::remove(p, e); } }
        weg{kopie};

    Diskette d = p8000(/*nur_lesen=*/false, kopie);
    ASSERT_TRUE(d) << d.error;

    const std::vector<uint8_t> vorher = d.fs->bitmap().raw();
    const std::vector<uint8_t> inhalt = {'P', '8', '0', '0', '0', 0x0D};

    WriteOptions wo;
    wo.udos_type = "A";
    ASSERT_TRUE(d.fs->write("TESTP8K", inhalt, wo)) << d.fs->lastError();

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(d.fs->read("TESTP8K", zurueck)) << d.fs->lastError();
    EXPECT_EQ(zurueck, inhalt);

    ASSERT_TRUE(d.fs->erase("TESTP8K")) << d.fs->lastError();
    EXPECT_EQ(d.fs->bitmap().raw(), vorher)
        << "Belegungsplan (inkl. Nachlauf und Byte 179H) hat sich veraendert";
    EXPECT_EQ(d.fs->info().files, 42);
}
