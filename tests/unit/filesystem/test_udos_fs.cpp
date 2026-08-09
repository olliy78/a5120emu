/**
 * @file test_udos_fs.cpp
 * @brief GoogleTests für @ref UdosFileSystem und @ref UdosBitmap.
 *
 * ### Woher die Sollwerte stammen
 * Alle Erwartungen dieses Tests stehen **wörtlich in `doc/udos_diskettenformat.md`** und
 * sind dort am **laufenden UDOS** gemessen (`CAT`, `STATUS`, `EXTRACT`), nicht abgeleitet:
 *
 * | Sollwert | Quelle |
 * |---|---|
 * | 69 Verzeichniseinträge, davon 39 ohne SECRET | §5 (`CAT` ohne `P=&` listet 39 von 69) |
 * | Seite 0: 850 freie Sektoren · Seite 1: 1310 | §2 (`STATUS` beider Laufwerke) |
 * | `SD`: Typ P1, 7 Sätze à 128 B, ENTRY E800, Kopf auf Spur 31 Sektor 9 | §6 (`EXTRACT SD`) |
 * | Datenträgername `UDOS.SYS.4.3` beidseitig | §2 |
 * | Belegungskarte: Nachlauf 11×0x33 · 0xF7 · 27×0x77 | §4 (aus `FORMATPC.MAC`) |
 *
 * Damit prüft dieser Test unsere Lesart nicht gegen sich selbst, sondern gegen das
 * Betriebssystem, das die Diskette geschrieben hat.
 *
 * @see core/filesystem/udos/udos_fs.h
 * @see doc/udos_diskettenformat.md
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/udos/udos_fs.h"
#include "core/peripherals/floppy_drive/disk_image.h"

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

/// @brief Eine Seite der UDOS-Referenzdiskette (Lebensdauer gebündelt).
struct Seite {
    std::unique_ptr<DiskImage>      disk;
    std::unique_ptr<SectorSpace>    space;
    std::unique_ptr<UdosFileSystem> fs;
    std::string                     error;
    explicit operator bool() const { return fs != nullptr; }
};

Seite oeffne(uint8_t head, const char* datei = "udos_boot_scp.hfe",
             const char* fsname = "udos_ds77") {
    Seite s;
    const FsProfile* p = dateisysteme().find(fsname);
    if (!p) { s.error = "Dateisystem unbekannt"; return s; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { s.error = "Format unbekannt"; return s; }

    s.disk = DiskImage::open(fixture(datei), std::nullopt, true);
    if (!s.disk) { s.error = "Abbild nicht ladbar"; return s; }
    s.space = std::make_unique<SectorSpace>(s.disk->medium(), *f, head);
    s.fs    = UdosFileSystem::mount(*s.space, *p, head, s.error);
    return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Belegungskarte
// ─────────────────────────────────────────────────────────────────────────────

TEST(UdosBitmap, BitlageIstMsbZuerst) {
    // §4.1: 4 Byte = 32 Bit, MSB zuerst.  E0 00 00 3F → Sektoren 1,2,3 belegt.
    UdosBitmap b = UdosBitmap::makeEmpty(26, 77, "TEST");
    b.setUsed(0, 1, true);
    b.setUsed(0, 2, true);
    b.setUsed(0, 3, true);
    EXPECT_EQ(b.raw()[kUdosBitmapFirstTrack + 0], 0xE0);
    EXPECT_EQ(b.raw()[kUdosBitmapFirstTrack + 1], 0x00);
    EXPECT_EQ(b.raw()[kUdosBitmapFirstTrack + 2], 0x00);
    EXPECT_EQ(b.raw()[kUdosBitmapFirstTrack + 3], 0x3F) << "die 6 ueberzaehligen Bits";

    // FC 00 FF 3F → Sektoren 1–6 und 17–24
    UdosBitmap c = UdosBitmap::makeEmpty(26, 77, "TEST");
    for (uint8_t s = 1; s <= 6; ++s)   c.setUsed(1, s, true);
    for (uint8_t s = 17; s <= 24; ++s) c.setUsed(1, s, true);
    EXPECT_EQ(c.raw()[kUdosBitmapFirstTrack + 4], 0xFC);
    EXPECT_EQ(c.raw()[kUdosBitmapFirstTrack + 5], 0x00);
    EXPECT_EQ(c.raw()[kUdosBitmapFirstTrack + 6], 0xFF);
}

TEST(UdosBitmap, KarteDerEchtenDisketteIstGueltigUndZaehlbar) {
    Seite s0 = oeffne(0);
    ASSERT_TRUE(s0) << s0.error;
    const UdosBitmap& b = s0.fs->bitmap();

    std::string warum;
    EXPECT_TRUE(b.looksValid(26, 0, &warum)) << warum;
    EXPECT_EQ(b.label(), "UDOS.SYS.4.3");
    EXPECT_EQ(b.sectorsPerTrack(), 26);
    EXPECT_EQ(b.trackCount(), 77);

    // §2/§4.2: `STATUS` des laufenden UDOS meldet fuer Laufwerk 0 genau 850 freie
    // Sektoren — und der GESPEICHERTE Freizaehler stimmt hier ausnahmsweise.
    EXPECT_EQ(b.countFree(), 850);
    EXPECT_EQ(b.storedFree(), 850);

    // Der „belegt"-Zaehler dagegen ergibt mit dem Freizaehler die Konstante 2464 aus
    // FORMATPC.MAC und passt NICHT zur Kapazitaet 77×26 = 2002 — genau wie dokumentiert.
    EXPECT_EQ(b.storedUsed() + b.storedFree(), kUdosCounterConstant);
    EXPECT_NE(b.storedUsed(), b.countUsed())
        << "wenn der Zaehler ploetzlich stimmt, ist die Doku (§4.2) zu pruefen";
}

TEST(UdosBitmap, LeereKarteTraegtDenNachlaufAusFormatpcMac) {
    const UdosBitmap b = UdosBitmap::makeEmpty(26, 77, "NEU");
    std::string warum;
    EXPECT_TRUE(b.looksValid(26, 0, &warum)) << warum;
    EXPECT_EQ(b.label(), "NEU");
    EXPECT_EQ(b.countFree(), 77 * 26) << "eine frische Karte ist vollstaendig frei";
    for (size_t i = 336; i < 347; ++i) EXPECT_EQ(b.raw()[i], 0x33) << i;
    EXPECT_EQ(b.raw()[347], 0xF7);
    for (size_t i = 348; i < 375; ++i) EXPECT_EQ(b.raw()[i], 0x77) << i;
}

// ─────────────────────────────────────────────────────────────────────────────
// Erkennung
// ─────────────────────────────────────────────────────────────────────────────

TEST(UdosFileSystem, ErkenntBeideSeitenAlsUdos) {
    for (uint8_t h = 0; h < 2; ++h) {
        Seite s = oeffne(h);
        ASSERT_TRUE(s.space != nullptr);
        const FsProfile* p = dateisysteme().find("udos_ds77");
        ASSERT_NE(p, nullptr);
        std::string warum;
        EXPECT_TRUE(UdosFileSystem::looksLikeUdos(*s.space, *p, h, &warum))
            << "Seite " << int(h) << ": " << warum;
    }
}

TEST(UdosFileSystem, ErkenntEineCpaDisketteNichtAlsUdos) {
    const FsProfile* p = dateisysteme().find("udos_ds77");
    ASSERT_NE(p, nullptr);
    auto disk = DiskImage::open(fixture("cpa_cpa780_k5601_clock.hfe"), std::nullopt, true);
    ASSERT_NE(disk, nullptr);
    const DiskFormat* f = formate().find("cpa780");
    ASSERT_NE(f, nullptr);
    SectorSpace raum(disk->medium(), *f, 0);

    std::string warum;
    EXPECT_FALSE(UdosFileSystem::looksLikeUdos(raum, *p, 0, &warum))
        << "eine CP/A-Diskette darf nicht als UDOS durchgehen";
    EXPECT_FALSE(warum.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Verzeichnis und Dateien
// ─────────────────────────────────────────────────────────────────────────────

TEST(UdosFileSystem, BeideSeitenSindEigeneDatentraegerMitEigenemBestand) {
    Seite s0 = oeffne(0);
    Seite s1 = oeffne(1);
    ASSERT_TRUE(s0) << s0.error;
    ASSERT_TRUE(s1) << s1.error;

    const FsInfo i0 = s0.fs->info();
    const FsInfo i1 = s1.fs->info();

    // §2: beide Seiten heissen gleich und haben trotzdem getrennte Bestaende.
    EXPECT_EQ(i0.label, "UDOS.SYS.4.3");
    EXPECT_EQ(i1.label, "UDOS.SYS.4.3");
    EXPECT_EQ(i0.free_bytes / 128, 850u)  << "STATUS DRIVE 0: 850 SECTORS AVAILABLE";
    EXPECT_EQ(i1.free_bytes / 128, 1310u) << "STATUS DRIVE 4: 1310 SECTORS AVAILABLE";
    EXPECT_EQ(i0.total_bytes / 128, 77u * 26u);

    EXPECT_NE(i0.files, i1.files) << "die Seiten tragen verschiedene Dateien";
    EXPECT_EQ(i0.files + i1.files, 69) << "§5: 69 Dateien auf dem Referenzdatentraeger";
}

TEST(UdosFileSystem, GeheimeUndSichtbareEintraegeWieCatSieZaehlt) {
    // §5: `CAT` ohne P=& listet exakt die Eintraege mit geloeschtem Bit 7 — 39 von 69.
    int gesamt = 0, sichtbar = 0;
    for (uint8_t h = 0; h < 2; ++h) {
        Seite s = oeffne(h);
        ASSERT_TRUE(s) << s.error;
        for (const UdosDirEntry& d : s.fs->directory()) {
            ++gesamt;
            if (!d.secret) ++sichtbar;
        }
    }
    EXPECT_EQ(gesamt, 69);
    EXPECT_EQ(sichtbar, 39);
}

TEST(UdosFileSystem, KopfsektorVonSdStimmtMitExtractUeberein) {
    // §6: `%EXTRACT SD` meldet RECORD COUNT=0007, RECORD LENGTH=0080,
    // BYTES IN LAST RECORD=0080, ENTRY=E800 — und der Kopfsektor liegt auf
    // Spur 31 Sektor 9.
    Seite s0 = oeffne(0);
    ASSERT_TRUE(s0) << s0.error;

    const UdosDirEntry* sd = nullptr;
    const std::vector<UdosDirEntry> verz = s0.fs->directory();
    for (const auto& d : verz) if (d.name == "SD") sd = &d;
    ASSERT_NE(sd, nullptr) << "Datei SD nicht im Verzeichnis";

    EXPECT_EQ(sd->header.track, 31);
    EXPECT_EQ(sd->header.sectorId(), 9);

    UdosFileHeader hdr;
    ASSERT_TRUE(s0.fs->readHeader(sd->header, hdr)) << s0.fs->lastError();
    EXPECT_EQ(hdr.typeName(), "P1");
    EXPECT_EQ(hdr.record_count, 7);
    EXPECT_EQ(hdr.record_len, 128);
    EXPECT_EQ(hdr.bytes_in_last, 128);
    EXPECT_EQ(hdr.entry_addr, 0xE800);
    EXPECT_EQ(hdr.length(), 7u * 128u);
}

TEST(UdosFileSystem, LiestEineTextdateiVollstaendig) {
    Seite s1 = oeffne(1);
    ASSERT_TRUE(s1) << s1.error;

    std::vector<uint8_t> d;
    ASSERT_TRUE(s1.fs->read("HELP.DAT.00", d)) << s1.fs->lastError();
    EXPECT_EQ(d.size(), 9919u) << "Laenge nach §7.1 (letzter Satz gekuerzt)";

    // §9: Typ A ist Text, Zeilenende CR.  Der Anfang ist die Ueberschrift der
    // mitgelieferten Systemdokumentation.
    const std::string text(d.begin(), d.begin() + std::min<size_t>(d.size(), 200));
    EXPECT_NE(text.find("HELP"), std::string::npos) << text;
    EXPECT_NE(text.find("VERFUEGBAR"), std::string::npos) << text;

    // Keine Nullbytes: eine korrekt zusammengesetzte Textdatei besteht aus Text.
    EXPECT_EQ(std::count(d.begin(), d.end(), '\0'), 0);
}

TEST(UdosFileSystem, SatzlaengeGroesserAlsEinSektorWirdRichtigZusammengesetzt) {
    // §7: ein Satz belegt satzlen/128 PHYSISCH AUFEINANDERFOLGENDE Sektoren derselben
    // Spur.  Wuerde das ignoriert, kaeme bei jeder Datei mit Satzlaenge > 128 Datenmuell
    // heraus — hier wird eine solche Datei gesucht und gelesen.
    bool geprueft = false;
    for (uint8_t h = 0; h < 2 && !geprueft; ++h) {
        Seite s = oeffne(h);
        ASSERT_TRUE(s) << s.error;
        for (const UdosDirEntry& d : s.fs->directory()) {
            UdosFileHeader hdr;
            if (!s.fs->readHeader(d.header, hdr)) continue;
            if (hdr.record_len <= 128 || hdr.record_count < 2) continue;

            std::vector<uint8_t> inhalt;
            ASSERT_TRUE(s.fs->read(d.name, inhalt))
                << d.name << " (Satzlaenge " << hdr.record_len << "): " << s.fs->lastError();
            EXPECT_EQ(inhalt.size(), hdr.length()) << d.name;

            std::vector<UdosPointer> kette;
            ASSERT_TRUE(s.fs->recordChain(hdr, kette));
            EXPECT_EQ(kette.size(), hdr.record_count)
                << d.name << ": Kettenlaenge weicht von der Satzanzahl ab";
            geprueft = true;
            break;
        }
    }
    EXPECT_TRUE(geprueft) << "keine Datei mit Satzlaenge > 128 gefunden";
}

TEST(UdosFileSystem, JedeDateiDerDisketteIstLesbar) {
    // Der Gesamtnachweis: ueber alle 69 Dateien laeuft die Kette sauber durch, und
    // die zusammengesetzte Laenge stimmt mit der Kopfsektor-Rechnung ueberein.
    int gelesen = 0;
    for (uint8_t h = 0; h < 2; ++h) {
        Seite s = oeffne(h);
        ASSERT_TRUE(s) << s.error;
        for (const FileEntry& e : s.fs->list()) {
            EXPECT_FALSE(e.damaged) << "Seite " << int(h) << " " << e.name;
            std::vector<uint8_t> d;
            ASSERT_TRUE(s.fs->read(e.name, d))
                << "Seite " << int(h) << " " << e.name << ": " << s.fs->lastError();
            EXPECT_EQ(d.size(), e.size) << e.name;
            ++gelesen;
        }
    }
    EXPECT_EQ(gelesen, 69);
}

TEST(UdosFileSystem, UnbekannteDateiWirdBenannt) {
    Seite s0 = oeffne(0);
    ASSERT_TRUE(s0) << s0.error;
    std::vector<uint8_t> d;
    EXPECT_FALSE(s0.fs->read("GIBTSNICHT", d));
    EXPECT_NE(s0.fs->lastError().find("GIBTSNICHT"), std::string::npos) << s0.fs->lastError();
}

TEST(UdosFileSystem, VerzeichnisIstSelbstEineDateiVomTypD) {
    // §5: das Verzeichnis ist eine gewoehnliche Datei DIRECTORY, ihr Kopfsektor liegt
    // auf Spur 22 Sektor 1 — der einzige feste Einstiegspunkt des Dateisystems.
    Seite s0 = oeffne(0);
    ASSERT_TRUE(s0) << s0.error;

    EXPECT_EQ(s0.fs->directoryHeader().track, 22);
    EXPECT_EQ(s0.fs->directoryHeader().sectorId(), 1);

    UdosFileHeader hdr;
    ASSERT_TRUE(s0.fs->readHeader(s0.fs->directoryHeader(), hdr));
    EXPECT_EQ(hdr.typeName(), "D");
}
