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

// ─────────────────────────────────────────────────────────────────────────────
// Schreiben (nur auf Kopien — Fixtures werden nie angefasst)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// @brief Beschreibbare Kopie einer Fixture; raeumt sich weg.
class Kopie {
public:
    Kopie(const char* fixture_name, const char* temp_name)
        : pfad_((std::filesystem::temp_directory_path() / temp_name).string()) {
        std::filesystem::copy_file(fixture(fixture_name), pfad_,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    ~Kopie() { std::error_code ec; std::filesystem::remove(pfad_, ec); }
    const std::string& path() const { return pfad_; }
private:
    std::string pfad_;
};

Seite oeffneSchreibbar(const std::string& pfad, uint8_t head) {
    Seite s;
    const FsProfile* p = dateisysteme().find("udos_ds77");
    if (!p) { s.error = "Dateisystem unbekannt"; return s; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { s.error = "Format unbekannt"; return s; }
    s.disk = DiskImage::open(pfad, std::nullopt, false);
    if (!s.disk) { s.error = "Abbild nicht ladbar"; return s; }
    s.space = std::make_unique<SectorSpace>(s.disk->medium(), *f, head);
    s.fs    = UdosFileSystem::mount(*s.space, *p, head, s.error);
    return s;
}

}  // namespace

TEST(UdosFileSystemWrite, RoundtripBinaerUndText) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_rt.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    std::vector<uint8_t> binaer(9000);
    for (size_t i = 0; i < binaer.size(); ++i) binaer[i] = static_cast<uint8_t>(i * 13 + 5);
    const std::string txt = "ZEILE EINS\rZEILE ZWEI\r";
    const std::vector<uint8_t> text(txt.begin(), txt.end());

    WriteOptions bin; bin.date = "260810";
    WriteOptions asc; asc.date = "260810"; asc.text = true;

    ASSERT_TRUE(s.fs->write("GROSS.BIN", binaer, bin)) << s.fs->lastError();
    ASSERT_TRUE(s.fs->write("KURZ.TEXT", text,  asc))  << s.fs->lastError();

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(s.fs->read("GROSS.BIN", zurueck)) << s.fs->lastError();
    ASSERT_EQ(zurueck.size(), binaer.size()) << "Laenge muss auf das Byte stimmen";
    EXPECT_EQ(zurueck, binaer);

    ASSERT_TRUE(s.fs->read("KURZ.TEXT", zurueck));
    ASSERT_GE(zurueck.size(), text.size());
    EXPECT_TRUE(std::equal(text.begin(), text.end(), zurueck.begin()));

    // Metadaten wie von UDOS erwartet.
    for (const FileEntry& e : s.fs->list()) {
        if (e.name == "GROSS.BIN") { EXPECT_EQ(e.type, "B"); EXPECT_EQ(e.size, 9000u);
                                     EXPECT_EQ(e.date, "260810"); }
        if (e.name == "KURZ.TEXT") { EXPECT_EQ(e.type, "A"); }
    }
}

TEST(UdosFileSystemWrite, KetteUndKopfsektorSindKonsistent) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_kette.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    std::vector<uint8_t> d(700);          // 6 Saetze (5×128 + 60) → 6 Saetze
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i);
    WriteOptions o; o.date = "260810";
    ASSERT_TRUE(s.fs->write("KETTE.DAT", d, o)) << s.fs->lastError();

    const UdosDirEntry* e = nullptr;
    const std::vector<UdosDirEntry> verz = s.fs->directory();
    for (const auto& x : verz) if (x.name == "KETTE.DAT") e = &x;
    ASSERT_NE(e, nullptr);

    UdosFileHeader hdr;
    ASSERT_TRUE(s.fs->readHeader(e->header, hdr)) << s.fs->lastError();
    EXPECT_EQ(hdr.record_len, 128) << "Satzlaenge 128 ist die getroffene Festlegung";
    EXPECT_EQ(hdr.record_count, 6u) << "700 B = 6 Saetze";
    EXPECT_EQ(hdr.bytes_in_last, 700u - 5u * 128u);
    EXPECT_EQ(hdr.length(), 700u);

    // §6/§7: der Rueckwaertszeiger des Kopfsektors zeigt auf den Verzeichnissektor,
    // in dem sein Eintrag steht — genau der, den directory() geliefert hat.
    EXPECT_TRUE(hdr.directory_sector == e->record)
        << "Kopfsektor zeigt auf einen anderen Verzeichnissatz";

    std::vector<UdosPointer> kette;
    ASSERT_TRUE(s.fs->recordChain(hdr, kette)) << s.fs->lastError();
    EXPECT_EQ(kette.size(), 6u) << "die Kette muss so lang sein wie die Satzanzahl";
    EXPECT_TRUE(kette.back() == hdr.last_record) << "letzter Satz im Kopf falsch";

    // Jeder Satz liegt auf einer Spur, die ein Werkzeug beschreiben darf (§8.6).
    for (const UdosPointer& p : kette)
        EXPECT_FALSE(s.fs->reservedTrack(p.track))
            << "Satz auf Systemspur " << int(p.track) << " abgelegt";
    EXPECT_FALSE(s.fs->reservedTrack(e->header.track));
}

TEST(UdosFileSystemWrite, BelegungWirdExaktNachgefuehrt) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_bits.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    const int frei_vorher = s.fs->bitmap().countFree();
    ASSERT_EQ(frei_vorher, 850);

    // 1000 B = 8 Saetze + 1 Kopfsektor = 9 Sektoren.
    WriteOptions o; o.date = "260810";
    ASSERT_TRUE(s.fs->write("NEUN.DAT", std::vector<uint8_t>(1000, 0x42), o))
        << s.fs->lastError();
    EXPECT_EQ(s.fs->bitmap().countFree(), frei_vorher - 9);

    // Jeder Sektor der Datei muss in der Karte als belegt stehen (§11: 0 Verstoesse).
    // Den Vektor HALTEN — directory() liefert per Wert; ein Zeiger in das Ergebnis
    // der Schleife zeigt sonst auf ein bereits zerstoertes Temporaer.
    const std::vector<UdosDirEntry> verz = s.fs->directory();
    const UdosDirEntry* e = nullptr;
    for (const auto& x : verz) if (x.name == "NEUN.DAT") e = &x;
    ASSERT_NE(e, nullptr);
    UdosFileHeader hdr;
    ASSERT_TRUE(s.fs->readHeader(e->header, hdr));
    std::vector<UdosPointer> kette;
    ASSERT_TRUE(s.fs->recordChain(hdr, kette));
    EXPECT_TRUE(s.fs->bitmap().used(e->header.track, e->header.sectorId()));
    for (const UdosPointer& p : kette)
        EXPECT_TRUE(s.fs->bitmap().used(p.track, p.sectorId()))
            << "Satz auf Spur " << int(p.track) << " Sektor " << int(p.sectorId())
            << " ist nicht als belegt eingetragen";

    // Der gespeicherte Freizaehler wird mitgefuehrt (§4.2: 2464 − frei).
    EXPECT_EQ(s.fs->bitmap().storedFree(), frei_vorher - 9);
    EXPECT_EQ(s.fs->bitmap().storedUsed() + s.fs->bitmap().storedFree(),
              kUdosCounterConstant);

    ASSERT_TRUE(s.fs->erase("NEUN.DAT")) << s.fs->lastError();
    EXPECT_EQ(s.fs->bitmap().countFree(), frei_vorher) << "Loeschen gibt alles zurueck";
}

TEST(UdosFileSystemWrite, VerzeichnisWaechstWennEinSatzVollIst) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_dirwachs.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    UdosFileHeader vorher;
    ASSERT_TRUE(s.fs->readHeader(s.fs->directoryHeader(), vorher));
    const uint16_t saetze_vorher = vorher.record_count;

    // Namen mit 32 Zeichen: je Eintrag 35 Byte → nach spaetestens vier Eintraegen
    // muss ein 128-B-Satz ueberlaufen.
    WriteOptions o; o.date = "260810";
    for (int i = 0; i < 12; ++i) {
        std::string name = "LANGERNAME.FUER.DEN.TEST.NR" + std::to_string(100 + i);
        ASSERT_LE(name.size(), 32u);
        ASSERT_TRUE(s.fs->write(name, std::vector<uint8_t>(64, 'x'), o))
            << name << ": " << s.fs->lastError();
    }

    UdosFileHeader nachher;
    ASSERT_TRUE(s.fs->readHeader(s.fs->directoryHeader(), nachher));
    EXPECT_GT(nachher.record_count, saetze_vorher)
        << "die Verzeichnisdatei haette wachsen muessen";

    // Alle zwoelf stehen im Verzeichnis und sind lesbar.
    int gefunden = 0;
    for (const FileEntry& e : s.fs->list())
        if (e.name.rfind("LANGERNAME.FUER.DEN.TEST.NR", 0) == 0) {
            ++gefunden;
            std::vector<uint8_t> d;
            EXPECT_TRUE(s.fs->read(e.name, d)) << e.name << ": " << s.fs->lastError();
        }
    EXPECT_EQ(gefunden, 12);

    // Und die Kette der Verzeichnisdatei ist weiterhin schluessig.
    std::vector<UdosPointer> kette;
    ASSERT_TRUE(s.fs->recordChain(nachher, kette)) << s.fs->lastError();
    EXPECT_EQ(kette.size(), nachher.record_count);
}

TEST(UdosFileSystemWrite, UeberschreibenNurMitErlaubnis) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_ovr.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    WriteOptions o; o.date = "260810";
    ASSERT_TRUE(s.fs->write("EINMAL.DAT", std::vector<uint8_t>(300, 1), o));
    EXPECT_FALSE(s.fs->write("EINMAL.DAT", std::vector<uint8_t>(300, 2), o));
    EXPECT_NE(s.fs->lastError().find("existiert bereits"), std::string::npos)
        << s.fs->lastError();

    const int frei = s.fs->bitmap().countFree();
    o.overwrite = true;
    ASSERT_TRUE(s.fs->write("EINMAL.DAT", std::vector<uint8_t>(300, 2), o))
        << s.fs->lastError();
    EXPECT_EQ(s.fs->bitmap().countFree(), frei)
        << "gleich grosse Ersetzung darf netto keinen Platz kosten";

    std::vector<uint8_t> d;
    ASSERT_TRUE(s.fs->read("EINMAL.DAT", d));
    EXPECT_EQ(d, std::vector<uint8_t>(300, 2));
}

TEST(UdosFileSystemWrite, DieVerzeichnisdateiSelbstIstGeschuetzt) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_dirdel.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    EXPECT_FALSE(s.fs->erase("DIRECTORY"));
    EXPECT_NE(s.fs->lastError().find("Verzeichnisdatei"), std::string::npos)
        << s.fs->lastError();
}

TEST(UdosFileSystemWrite, UnzulaessigeNamenUndLeereDateien) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_name.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    const std::vector<uint8_t> d(100, 1);
    WriteOptions o; o.date = "260810";
    EXPECT_FALSE(s.fs->write("", d, o));
    EXPECT_FALSE(s.fs->write(std::string(33, 'A'), d, o)) << "33 Zeichen sind zu viel";
    EXPECT_FALSE(s.fs->write("MIT LEERZEICHEN", d, o));
    EXPECT_FALSE(s.fs->write("OK.NAME", {}, o)) << "leere Dateien kann UDOS nicht";

    std::string warum;
    EXPECT_TRUE(UdosFileSystem::validName("NOTE.TO.UDOS.4.3", &warum)) << warum;
    EXPECT_TRUE(UdosFileSystem::validName(std::string(32, 'A'), &warum)) << warum;
    EXPECT_FALSE(UdosFileSystem::validName("a b", &warum));
}

TEST(UdosFileSystemWrite, PlatzpruefungRechnetKopfsektorenMit) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_fit.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    FitReport r;
    ASSERT_TRUE(s.fs->wouldFit({{"A", 1, 0, false}}, r));
    EXPECT_TRUE(r.fits);
    EXPECT_EQ(r.needed, 2u * 128u) << "1 Byte = 1 Satz + 1 Kopfsektor";

    ASSERT_TRUE(s.fs->wouldFit({{"ZUGROSS", 200u * 1024u, 0, false}}, r));
    EXPECT_FALSE(r.fits);
    EXPECT_NE(r.detail.find("frei sind"), std::string::npos) << r.detail;

    // Der freie Platz zaehlt die Systemspuren NICHT mit (§8.6) — er darf also nie
    // groesser sein als das, was die Karte insgesamt frei meldet.
    EXPECT_LE(r.available / 128, static_cast<uint64_t>(s.fs->bitmap().countFree()));
}

TEST(UdosFileSystemWrite, DiskettenvollHinterlaesstNichts) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_udos_voll.hfe");
    Seite s = oeffneSchreibbar(k.path(), 0);
    ASSERT_TRUE(s) << s.error;

    const int frei = s.fs->bitmap().countFree();
    const size_t dateien = s.fs->list().size();

    WriteOptions o; o.date = "260810";
    EXPECT_FALSE(s.fs->write("ZUGROSS.DAT",
                             std::vector<uint8_t>(static_cast<size_t>(frei + 10) * 128, 7), o));
    EXPECT_NE(s.fs->lastError().find("voll"), std::string::npos) << s.fs->lastError();
    EXPECT_EQ(s.fs->bitmap().countFree(), frei) << "belegte Bits nicht zurueckgegeben";
    EXPECT_EQ(s.fs->list().size(), dateien);
}

TEST(UdosFileSystemWrite, MkfsLegtEinBenutzbaresDateisystemAn) {
    // Frisch formatierte, leere UDOS-Diskette: DiskImage::create erzeugt die
    // Spuren (echte Marken/CRC), format() legt Karte und Verzeichnisdatei an.
    const std::string pfad =
        (std::filesystem::temp_directory_path() / "k1520_test_udos_mkfs.hfe").string();
    {
        const DiskFormat* f = formate().find("udos_ds77");
        ASSERT_NE(f, nullptr);
        auto leer = DiskImage::create(pfad, *f, false, Encoding::MFM);
        ASSERT_NE(leer, nullptr);
        ASSERT_TRUE(leer->flush());
    }

    const FsProfile* p = dateisysteme().find("udos_ds77");
    ASSERT_NE(p, nullptr);
    const DiskFormat* f = formate().find(p->format);

    auto disk = DiskImage::open(pfad, std::nullopt, false);
    ASSERT_NE(disk, nullptr);
    SectorSpace raum(disk->medium(), *f, 0);

    std::string err;
    auto fs = UdosFileSystem::format(raum, *p, 0, "NEUE.DISKETTE", err);
    ASSERT_NE(fs, nullptr) << err;

    // Sie sieht fuer die Erkennung wie eine UDOS-Seite aus …
    std::string warum;
    EXPECT_TRUE(UdosFileSystem::looksLikeUdos(raum, *p, 0, &warum)) << warum;

    // … traegt genau die Verzeichnisdatei …
    const std::vector<FileEntry> liste = fs->list();
    ASSERT_EQ(liste.size(), 1u);
    EXPECT_EQ(liste[0].name, "DIRECTORY");
    EXPECT_EQ(liste[0].type, "D");
    EXPECT_TRUE(liste[0].hidden) << "DIRECTORY ist geheim (SECRET)";

    // … und ist danach normal beschreibbar.
    WriteOptions o; o.date = "260810";
    ASSERT_TRUE(fs->write("ERSTE.DATEI", std::vector<uint8_t>(500, 0x5A), o))
        << fs->lastError();
    std::vector<uint8_t> d;
    ASSERT_TRUE(fs->read("ERSTE.DATEI", d));
    EXPECT_EQ(d, std::vector<uint8_t>(500, 0x5A));

    const FsInfo i = fs->info();
    EXPECT_EQ(i.label, "NEUE.DISKETTE");
    EXPECT_EQ(i.total_bytes / 128, 77u * 26u);
    EXPECT_TRUE(i.warnings.empty()) << (i.warnings.empty() ? "" : i.warnings[0]);

    // Erneutes Mounten (ueber den normalen Weg) muss gelingen.
    auto wieder = UdosFileSystem::mount(raum, *p, 0, err);
    EXPECT_NE(wieder, nullptr) << err;

    std::error_code ec;
    std::filesystem::remove(pfad, ec);
}
