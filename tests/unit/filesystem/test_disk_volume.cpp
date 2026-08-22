/**
 * @file test_disk_volume.cpp
 * @brief GoogleTests für @ref DiskVolume — die Diskette als Ganzes.
 *
 * Geprüft werden die vier Zusagen aus doc/design/13_k1520disktool.md §1:
 * beidseitiges UDOS ist EIN Datenträger mit `Side0/`+`Side1/`, die Ansicht ist
 * immer frisch, „passt nicht" schreibt gar nicht erst, und ein Abbild ohne
 * Katalogeintrag wird mit Diagnose abgelehnt.
 *
 * @see core/filesystem/disk_volume.h
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {

std::string fixture(const char* name) {
    return (fs::path(FIXTURE_DIR) / name).string();
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

std::unique_ptr<DiskVolume> oeffne(const std::string& pfad, const std::string& fs_name,
                                   std::string& err) {
    return DiskVolume::open(pfad, fs_name, formate(), dateisysteme(), err);
}

/// @brief Oeffnen UND den Schreibschutz aufheben — in der Oberflaeche der Haken
///        „Nur lesen", hier ein Aufruf.  Absichtlich ein eigener Schritt.
std::unique_ptr<DiskVolume> oeffneSchreibbar(const std::string& pfad,
                                             const std::string& fs_name,
                                             std::string& err) {
    auto v = DiskVolume::open(pfad, fs_name, formate(), dateisysteme(), err);
    if (v) v->setReadOnly(false);
    return v;
}

/// @brief Temporaerer Ordner, raeumt sich weg.
class TempOrdner {
public:
    explicit TempOrdner(const char* name)
        : pfad_(k1520test::tempPath(name)) {
        std::error_code ec;
        fs::remove_all(pfad_, ec);
        fs::create_directories(pfad_, ec);
    }
    ~TempOrdner() { std::error_code ec; fs::remove_all(pfad_, ec); }
    const std::string& path() const { return pfad_; }
    fs::path operator/(const std::string& s) const { return fs::path(pfad_) / s; }
private:
    std::string pfad_;
};

/// @brief Beschreibbare Kopie einer Fixture.
class Kopie {
public:
    Kopie(const char* fixture_name, const char* temp_name)
        : pfad_(k1520test::tempPath(temp_name)) {
        fs::copy_file(fixture(fixture_name), pfad_, fs::copy_options::overwrite_existing);
    }
    ~Kopie() { std::error_code ec; fs::remove(pfad_, ec); }
    const std::string& path() const { return pfad_; }
private:
    std::string pfad_;
};

void schreibe(const fs::path& p, const std::string& inhalt) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << inhalt;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Erkennung beim Öffnen
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, ErkenntCpaBootdisketteVonSelbst) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_EQ(dv->detection().format, "cpa780");
    EXPECT_EQ(dv->detection().filesystem, "cpa780");
    EXPECT_TRUE(dv->detection().unambiguous);
    EXPECT_EQ(dv->volumeCount(), 1) << "eine CP/M-Diskette ist EIN Dateisystem";
    EXPECT_TRUE(dv->volumeDir(0).empty()) << "bei einem Volume ist der Ordner flach";
    EXPECT_EQ(dv->list().size(), 24u);
}

TEST(DiskVolume, ErkenntUdosUndOeffnetBeideSeitenAlsEinenDatentraeger) {
    std::string err;
    auto dv = oeffne(fixture("udos_boot_scp.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_EQ(dv->detection().filesystem, "udos_ds77");
    EXPECT_EQ(dv->volumeCount(), 2) << "beide Seiten gehoeren zu EINER Diskette";
    EXPECT_EQ(dv->volumeDir(0), "Side0");
    EXPECT_EQ(dv->volumeDir(1), "Side1");

    // Eine Liste ueber beide Seiten, jede Datei mit ihrer Seite.
    const std::vector<FileEntry> alle = dv->list();
    EXPECT_EQ(alle.size(), 69u);
    int s0 = 0, s1 = 0;
    for (const FileEntry& e : alle) (e.volume == 0 ? s0 : s1)++;
    EXPECT_EQ(s0, 47);
    EXPECT_EQ(s1, 22);

    EXPECT_EQ(dv->volumeInfo(0).free_bytes / 128, 850u);
    EXPECT_EQ(dv->volumeInfo(1).free_bytes / 128, 1310u);

    // Der Altbestand hinter Spur 76 wird gemeldet, nicht verschwiegen.
    EXPECT_NE(dv->detection().remarks.find("Altbestand"), std::string::npos)
        << dv->detection().remarks;
}

TEST(DiskVolume, LiestEineDisketteMitFremderSyncSitte) {
    // Diese Diskette wurde an einem ANDEREN K1520-Rechner (K5601) beschrieben.  Sein
    // Controller schreibt die Sync-Gruppe vor dem Datenfeld mit nur ein bis zwei
    // echten Sync-Marken (die übrigen 0xA1 regulär kodiert) und rechnet die ID-CRC
    // OHNE die A1-Präambel.  Bis der Decoder die Gruppe wie ein echter Datenseparator
    // liest (erstes Byte, das kein 0xA1 ist, IST die Marke), fand er für JEDEN
    // beschriebenen Sektor kein Datenfeld: die Diskette galt als unerkannt, weil die
    // Belegungskarte auf Spur 23 „kuerzer als 128 B" war.
    std::string err;
    auto dv = oeffne(fixture("udos_ds77_k5601_fremdsync.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_EQ(dv->detection().filesystem, "udos_ds77");
    EXPECT_EQ(dv->volumeCount(), 2);

    const std::vector<FileEntry> alle = dv->list();
    EXPECT_EQ(alle.size(), 46u) << "die Diskette traegt 34 + 12 Dateien";
    int s0 = 0, s1 = 0;
    for (const FileEntry& e : alle) (e.volume == 0 ? s0 : s1)++;
    EXPECT_EQ(s0, 34);
    EXPECT_EQ(s1, 12);

    // Die abweichende ID-CRC-Sitte ist ein DIALEKT, kein Schaden: sie wird wie die
    // Standard-Sitte akzeptiert (TrackCodec::mfmFieldCrcOk), sonst wären alle 4004
    // Sektoren im Diskeditor rot, obwohl an ihnen nichts fehlt.
    EXPECT_EQ(dv->detection().remarks.find("CRC"), std::string::npos)
        << "der CRC-Dialekt wird als Schaden gemeldet: " << dv->detection().remarks;
}

TEST(DiskVolume, LehntAbbildOhneKatalogeintragMitDiagnoseAb) {
    std::string err;
    auto dv = oeffne(fixture("cpa_mini.hfe"), "", err);
    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("passt zu keinem Format"), std::string::npos) << err;
    EXPECT_NE(err.find("4 Sektoren"), std::string::npos)
        << "die Meldung muss die gemessene Geometrie nennen:\n" << err;
}

/**
 * @test Eine Geometrie OHNE Katalogprofil wird trotzdem geoeffnet — per CP/A-Regel.
 * @par Kriterium  `k5601_ss80_26x128` hat keinen `filesystems:`-Eintrag; die Diskette
 *                 laesst sich dennoch oeffnen, das Dateisystem heisst `cpa_auto`, und
 *                 die Meldung sagt, woher die Werte stammen.
 * @par Warum      Das ist der Kern des Rueckfalls: der Katalog nennt nur die Handvoll
 *                 Disketten, die man staendig braucht; alles andere rechnet die Regel
 *                 aus, mit der auch das CP/A-BIOS arbeitet.
 */
TEST(DiskVolume, GeometrieOhneProfilWirdUeberDieCpaRegelGeoeffnet) {
    const std::string pfad =
        k1520test::tempPath("k1520_dv_ohne_profil.hfe");
    const DiskFormat* fmt = formate().find("k5601_ss80_26x128");
    ASSERT_NE(fmt, nullptr);
    ASSERT_TRUE(dateisysteme().forFormat(fmt->name).empty())
        << "der Test setzt voraus, dass diese Geometrie KEIN benanntes Profil hat";
    ASSERT_NE(DiskImage::create(pfad, *fmt, /*write_protect=*/false), nullptr);

    std::string err;
    auto dv = oeffne(pfad, "", err);
    std::error_code ec;
    fs::remove(pfad, ec);

    ASSERT_NE(dv, nullptr) << err;
    EXPECT_EQ(dv->detection().format, "k5601_ss80_26x128");
    EXPECT_EQ(dv->detection().filesystem, "cpa_auto");
    EXPECT_NE(dv->detection().remarks.find("CP/A-Regel"), std::string::npos)
        << dv->detection().remarks;
    EXPECT_TRUE(dv->list().empty()) << "frisch formatiert = keine Dateien";
}

/**
 * @test Ein benanntes Profil geht dem abgeleiteten IMMER vor.
 * @par Kriterium  Die 780K-Bootdiskette meldet `cpa780`, nicht `cpa_auto` — obwohl die
 *                 Regel dasselbe ausrechnen wuerde.
 */
TEST(DiskVolume, BenanntesProfilGehtDemAbgeleitetenVor) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_EQ(dv->detection().filesystem, "cpa780");
}

/**
 * @test `--fs cpa_auto` erzwingt die Regel, auch wo ein Katalogprofil passen wuerde.
 */
TEST(DiskVolume, CpaAutoLaesstSichAusdruecklichAnfordern) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.hfe"), "cpa_auto", err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_EQ(dv->detection().format, "cpa780");
    EXPECT_EQ(dv->detection().filesystem, "cpa_auto");
    EXPECT_EQ(dv->list().size(), 24u) << "dieselben Dateien wie mit dem Profil cpa780";
}

/**
 * @test Eine MS-DOS-Diskette wird als solche benannt, nicht als „unbekannt" abgetan.
 * @par Kriterium  Die Meldung nennt FAT und die OEM-Kennung.
 * @par Warum      FORMAT.COM legt auf Wunsch DOS-Disketten an (die Menuepunkte mit
 *                 `{MSDOS}`, doc/format.md §3.3).  Wer so eine Diskette einlegt, soll
 *                 erfahren, WAS darauf liegt — nicht bloss, dass es nicht geht.
 */
TEST(DiskVolume, MsDosDisketteWirdAlsSolcheGemeldet) {
    const std::string pfad = k1520test::tempPath("k1520_dv_fat.hfe");
    const DiskFormat* fmt = formate().find("k5601_9x512");
    ASSERT_NE(fmt, nullptr);
    {
        auto disk = DiskImage::create(pfad, *fmt, /*write_protect=*/false);
        ASSERT_NE(disk, nullptr);
        SectorSpace raum(disk->medium(), *fmt);
        std::vector<uint8_t> boot(512, 0x00);
        boot[0] = 0xEB; boot[1] = 0x34; boot[2] = 0x90;          // JMP SHORT / NOP
        const char* oem = "CP/A1188";
        for (int i = 0; i < 8; ++i) boot[3 + i] = static_cast<uint8_t>(oem[i]);
        boot[11] = 0x00; boot[12] = 0x02;                        // 512 Bytes/Sektor
        boot[13] = 2;                                            // Sektoren/Cluster
        boot[21] = 0xF9;                                         // Medienkennung
        ASSERT_TRUE(raum.writeSector(0, 0, 1, boot));
        ASSERT_TRUE(disk->flush());
    }

    std::string err;
    auto dv = oeffne(pfad, "", err);
    std::error_code ec;
    fs::remove(pfad, ec);

    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("MS-DOS"), std::string::npos) << err;
    EXPECT_NE(err.find("CP/A1188"), std::string::npos) << err;
}

/**
 * @test **Jedes** Format des Katalogs laesst sich anlegen, wiedererkennen und mounten.
 * @par Kriterium  Fuer jeden `formats:`-Eintrag: `DiskImage::create` → `DiskVolume::open`
 *                 ohne `--fs` liefert ein Volume, und die erkannte Geometrie ist genau
 *                 die, mit der angelegt wurde.
 * @par Warum      Das ist die Zusage „alle Formate sind mountbar" als Waechter.  Er
 *                 faellt, sobald ein neuer Katalogeintrag eine Geometrie beschreibt, die
 *                 die Erkennung anschliessend nicht wiederfindet (Ueberdeckung durch
 *                 einen anderen Eintrag, unzulaessige Sektorlaenge, fehlender Kopf) —
 *                 und er kostet nichts, weil alles im Speicher passiert.
 */
TEST(DiskVolume, JedesKatalogformatLaesstSichAnlegenUndWiederOeffnen) {
    const std::string pfad = k1520test::tempPath("k1520_dv_alle.hfe");
    int geprueft = 0;

    for (const DiskFormat& f : formate().formats()) {
        std::error_code ec;
        fs::remove(pfad, ec);
        ASSERT_NE(DiskImage::create(pfad, f, /*write_protect=*/false), nullptr)
            << "Format '" << f.name << "' laesst sich nicht anlegen";

        std::string err;
        auto dv = oeffne(pfad, "", err);
        EXPECT_NE(dv, nullptr) << "Format '" << f.name << "': " << err;
        if (dv) {
            // Geometrisch identische Eintraege sind normal (cpa640 ≡ k5601_16x256),
            // und ein Format darf bis zu drei Zylinder mehr deklarieren als beschrieben
            // sind (GeometryProbe, slack_cyls) — cpa624 (78 Zyl.) wird deshalb auch von
            // cpa640 (80 Zyl.) erkannt.  Kopfzahl und Schrittweite muessen stimmen.
            const DiskFormat* e = formate().find(dv->detection().format);
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->numHeads(), f.numHeads()) << f.name;
            EXPECT_EQ(e->step, f.step) << f.name;
            EXPECT_LE(std::abs(int(e->physicalCylinders()) - int(f.physicalCylinders())), 3)
                << "Format '" << f.name << "' wurde als '" << dv->detection().format
                << "' erkannt — zu weit auseinander";
        }
        ++geprueft;
    }

    std::error_code ec;
    fs::remove(pfad, ec);
    EXPECT_GT(geprueft, 50) << "der Katalog ist unerwartet klein";
}

TEST(DiskVolume, UdosAlsImgWirdAbgelehnt) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.img"), "udos_ds77", err);
    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("img"), std::string::npos) << err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Side0/Side1 — Ordnerstruktur
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, ExtrahiertBeideSeitenInSideOrdner) {
    std::string err;
    auto dv = oeffne(fixture("udos_boot_scp.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner ziel("k1520_test_dv_extract");
    ASSERT_TRUE(dv->extractAll(ziel.path(), TransferOptions{})) << dv->lastError();

    ASSERT_TRUE(fs::is_directory(ziel / "Side0"));
    ASSERT_TRUE(fs::is_directory(ziel / "Side1"));

    // Je Seite EINE Datei weniger als im Verzeichnis (47/22): die UDOS-Datei
    // DIRECTORY (Typ D) ist Dateisystemstruktur und wird zwar gelistet, aber
    // nicht extrahiert — sonst waere sie beim Zurueckschreiben ein Fremdkoerper.
    // Daneben liegt je Datei ihr `.fileinfo` (die Kopfsektorangaben, die eine
    // Linux-Datei nicht traegt) — es wird getrennt gezaehlt, sonst pruefte der
    // Test nur noch, dass irgendetwas im Ordner liegt.
    auto zaehle = [](const fs::path& ordner, int& nutz, int& angaben) {
        for (const auto& e : fs::directory_iterator(ordner)) {
            if (e.path().extension() == ".fileinfo") ++angaben; else ++nutz;
        }
    };
    int n0 = 0, n1 = 0, a0 = 0, a1 = 0;
    zaehle(ziel / "Side0", n0, a0);
    zaehle(ziel / "Side1", n1, a1);
    EXPECT_EQ(n0, 46);
    EXPECT_EQ(n1, 21);
    EXPECT_EQ(a0, 46) << "zu jeder Datei gehoert ihre Angabendatei";
    EXPECT_EQ(a1, 21);
    EXPECT_FALSE(fs::exists(ziel / "Side0" / "DIRECTORY"));

    // Stichprobe: der Inhalt ist der der Diskette.
    ASSERT_TRUE(fs::exists(ziel / "Side1" / "HELP.DAT.00"));
    EXPECT_EQ(fs::file_size(ziel / "Side1" / "HELP.DAT.00"), 9919u);
}

TEST(DiskVolume, ExtrahiertFlachWennDieDisketteEinDateisystemHat) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.img"), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner ziel("k1520_test_dv_flach");
    ASSERT_TRUE(dv->extractAll(ziel.path(), TransferOptions{})) << dv->lastError();

    EXPECT_FALSE(fs::exists(ziel / "Side0")) << "eine CP/M-Diskette hat keine Seitenordner";
    EXPECT_TRUE(fs::exists(ziel / "PIP.COM"));
    int n = 0;
    for (const auto& e : fs::directory_iterator(ziel.path())) { (void)e; ++n; }
    EXPECT_EQ(n, 24);
}

TEST(DiskVolume, FehlenderSideOrdnerIstEinFehlerUndAendertNichts) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_side.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_EQ(dv->volumeCount(), 2);

    TempOrdner quelle("k1520_test_dv_nurside0");
    schreibe(quelle / "Side0" / "NEU", "Inhalt");

    EXPECT_FALSE(dv->insertAll(quelle.path(), TransferOptions{}));
    EXPECT_NE(dv->lastError().find("Side1/"), std::string::npos) << dv->lastError();
    EXPECT_NE(dv->lastError().find("2 Seiten"), std::string::npos) << dv->lastError();
    EXPECT_FALSE(dv->dirty()) << "die Diskette darf nicht angefasst worden sein";
}

TEST(DiskVolume, LoseDateienNebenDenSideOrdnernSindEinFehler) {
    std::string err;
    auto dv = oeffneSchreibbar(fixture("udos_boot_scp.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner quelle("k1520_test_dv_lose");
    schreibe(quelle / "Side0" / "A", "x");
    schreibe(quelle / "Side1" / "B", "y");
    schreibe(quelle / "HERRENLOS.TXT", "wohin damit?");

    EXPECT_FALSE(dv->insertAll(quelle.path(), TransferOptions{}));
    EXPECT_NE(dv->lastError().find("HERRENLOS.TXT"), std::string::npos) << dv->lastError();
    EXPECT_FALSE(dv->dirty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Transaktion und Platzprüfung
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, StapelPasstNichtUndSchreibtDeshalbGarNichts) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_voll.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    const uint64_t frei_vorher = dv->volumeInfo(0).free_bytes;

    TempOrdner quelle("k1520_test_dv_zuviel");
    // Drei Dateien, die zusammen mehr als der freie Platz sind.
    const std::string gross(static_cast<size_t>(frei_vorher / 2 + 4096), 'X');
    schreibe(quelle / "A.BIN", gross);
    schreibe(quelle / "B.BIN", gross);

    EXPECT_FALSE(dv->insertAll(quelle.path(), TransferOptions{}));
    EXPECT_NE(dv->lastError().find("Es wurde nichts geschrieben"), std::string::npos)
        << dv->lastError();
    EXPECT_FALSE(dv->dirty()) << "die Diskette wurde trotzdem angefasst";
    EXPECT_EQ(dv->volumeInfo(0).free_bytes, frei_vorher);
    EXPECT_EQ(dv->list().size(), 24u);
}

TEST(DiskVolume, StapelSchreibtUndDieAnsichtIstSofortAktuell) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_ok.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    const size_t vorher = dv->list().size();

    TempOrdner quelle("k1520_test_dv_gut");
    schreibe(quelle / "eins.txt", "Zeile eins\n");
    schreibe(quelle / "zwei.dat", std::string(5000, 'Z'));

    ASSERT_TRUE(dv->insertAll(quelle.path(), TransferOptions{})) << dv->lastError();

    // §9.3: kein Aktualisieren noetig — list() liest neu.
    const std::vector<FileEntry> nachher = dv->list();
    EXPECT_EQ(nachher.size(), vorher + 2);
    EXPECT_NE(std::find_if(nachher.begin(), nachher.end(),
                           [](const FileEntry& e) { return e.name == "EINS.TXT"; }),
              nachher.end());
    EXPECT_NE(std::find_if(nachher.begin(), nachher.end(),
                           [](const FileEntry& e) { return e.name == "ZWEI.DAT"; }),
              nachher.end());
    EXPECT_TRUE(dv->dirty());
}

TEST(DiskVolume, CheckFitUrteiltOhneZuSchreiben) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_fit.img");
    std::string err;
    auto dv = oeffne(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner klein("k1520_test_dv_fit_klein");
    schreibe(klein / "winzig.txt", "kurz");
    std::string bericht;
    EXPECT_TRUE(dv->checkFit(klein.path(), bericht)) << bericht;
    EXPECT_EQ(bericht, "passt");
    EXPECT_FALSE(dv->dirty());

    TempOrdner gross("k1520_test_dv_fit_gross");
    schreibe(gross / "riesig.bin", std::string(900u * 1024u, 'Q'));
    EXPECT_FALSE(dv->checkFit(gross.path(), bericht));
    EXPECT_NE(bericht.find("frei sind"), std::string::npos) << bericht;
    EXPECT_FALSE(dv->dirty());
}

TEST(DiskVolume, RoundtripUeberDieDateiEbene) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_rt.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner quelle("k1520_test_dv_rt_q");
    const std::string inhalt(2500, 'M');
    schreibe(quelle / "RUND.BIN", inhalt);
    ASSERT_TRUE(dv->insertAll(quelle.path(), TransferOptions{})) << dv->lastError();

    TempOrdner ziel("k1520_test_dv_rt_z");
    ASSERT_TRUE(dv->extract(FileRef{0, "RUND.BIN"}, (ziel / "RUND.BIN").string(),
                            TransferOptions{})) << dv->lastError();

    std::ifstream f((ziel / "RUND.BIN").string(), std::ios::binary);
    const std::string zurueck((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    // CP/M rundet auf 128-B-Saetze auf — der Anfang muss byteweise stimmen.
    ASSERT_GE(zurueck.size(), inhalt.size());
    EXPECT_EQ(zurueck.compare(0, inhalt.size(), inhalt), 0);
}

TEST(DiskVolume, TextmodusSetztZeilenendenUm) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_text.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner quelle("k1520_test_dv_text_q");
    schreibe(quelle / "TEXT.TXT", "eins\nzwei\ndrei\n");

    TransferOptions o;
    o.text = true;
    ASSERT_TRUE(dv->insert((quelle / "TEXT.TXT").string(), FileRef{0, "TEXT.TXT"}, o))
        << dv->lastError();

    TempOrdner ziel("k1520_test_dv_text_z");
    ASSERT_TRUE(dv->extract(FileRef{0, "TEXT.TXT"}, (ziel / "TEXT.TXT").string(), o));

    std::ifstream f((ziel / "TEXT.TXT").string(), std::ios::binary);
    const std::string zurueck((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    EXPECT_EQ(zurueck, "eins\nzwei\ndrei\n")
        << "Hin- und Rueckweg muessen sich aufheben (LF ↔ CR LF, 0x1A-Ende)";
}

// ─────────────────────────────────────────────────────────────────────────────
// CP/M-Beiblatt: Nutzerbereich und Attributbits ueberleben den Rundlauf
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, CpmBeiblattTraegtNutzerbereichUndAttributeDurchDenRundlauf) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_cpm_beiblatt.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    // Eine Systemdatei im Nutzerbereich 3 — beides steht in keiner Linux-Datei.
    TempOrdner vorher("k1520_test_dv_cpm_bb_v");
    schreibe(vorher / "SYSTEM.COM", std::string(500, 'S'));
    ASSERT_TRUE(dv->insert((vorher / "SYSTEM.COM").string(), FileRef{0, "3:SYSTEM.COM"},
                           TransferOptions{})) << dv->lastError();
    CpmAttrs a;
    a.set_read_only = true; a.read_only = true;
    a.set_system    = true; a.system    = true;
    ASSERT_TRUE(dv->setAttributes(FileRef{0, "3:SYSTEM.COM"}, a)) << dv->lastError();

    // Herausholen: die Datei heisst auf Linux "3_SYSTEM.COM", das Beiblatt nennt
    // den echten Namen und die Attribute.
    TempOrdner ordner("k1520_test_dv_cpm_bb_o");
    ASSERT_TRUE(dv->extractAll(ordner.path(), TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(fs::exists(ordner / "3_SYSTEM.COM"));
    ASSERT_TRUE(fs::exists(ordner / "cpm-dateiangaben.txt"))
        << "ohne Beiblatt gingen Nutzerbereich und Attribute verloren";

    // Und in eine frische Diskette zurueck.
    Kopie leer("cpa_cpa780_k5601_clock.img", "k1520_test_dv_cpm_bb_ziel.img");
    auto ziel = oeffneSchreibbar(leer.path(), "cpa780", err);
    ASSERT_NE(ziel, nullptr) << err;
    ASSERT_TRUE(ziel->insertAll(ordner.path(), TransferOptions{})) << ziel->lastError();

    bool gefunden = false;
    for (const FileEntry& e : ziel->list())
        if (e.name == "SYSTEM.COM") {
            gefunden = true;
            EXPECT_EQ(e.user, 3) << "der Nutzerbereich kommt aus dem Beiblatt";
            EXPECT_EQ(e.attributes, "RO SYS");
        }
    EXPECT_TRUE(gefunden);

    // Das Beiblatt selbst darf NICHT als Datei auf der Diskette landen.
    for (const FileEntry& e : ziel->list())
        EXPECT_EQ(e.name.find("CPM-DATE"), std::string::npos) << e.name;
}

TEST(DiskVolume, CpmBeiblattEntstehtNurWennEsEtwasZuSagenGibt) {
    // Eine Diskette ohne Nutzerbereiche und ohne gesetzte Attribute braucht keines —
    // ein leeres Beiblatt waere nur ein Ratsel im Ordner.
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_cpm_bb_leer.img");
    std::string err;
    auto dv = oeffne(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner ordner("k1520_test_dv_cpm_bb_leer_o");
    ASSERT_TRUE(dv->extractAll(ordner.path(), TransferOptions{})) << dv->lastError();
    EXPECT_FALSE(fs::exists(ordner / "cpm-dateiangaben.txt"));
}

/// @test Der Rundlauf `extractAll` → `insertAll` erhält den UDOS-Kopfsektor —
///       **auch die Felder, die 0 sind.**
///
/// Bis 2026-08-21 verlor er zwei davon, und zwar an derselben Ursache: der
/// Schreibpfad las die 0 als „nicht angegeben" und setzte einen Ersatzwert.
///
///   * „Bytes im letzten Satz" (Offset 22): aus 0 wurde die volle Satzlänge.
///     Gleichbedeutend, aber nicht dasselbe Byte.
///   * LOW/HIGH ADDRESS und STACK SIZE (Offset 122/124/126): waren alle drei 0,
///     lief der Schreibblock gar nicht — und der Kopfsektor behielt seine
///     0xFF-Vorbelegung.  **Aus 0000 wurde FFFF**, und das ist bei einer
///     PROGRAMMdatei kein Schönheitsfehler: UDOS weist sie beim Starten mit
///     `MEMORY PROTECT VIOLATION` ab (doc/udos_diskettenformat.md §14), und unsere
///     eigene Prüfung meldet sie als `udos.kopf.speicher`.
///
/// Aufgefallen ist es nie, weil der vorhandene Rundlauftest den **Dateiinhalt**
/// vergleicht.  Dieser hier vergleicht die Kopfsektorangaben, und zwar über ALLE
/// Dateien der Referenzdiskette — auf ihr haben 11 Dateien alle drei Speicherwerte
/// auf 0 und eine „letzter Satz" = 0.
TEST(DiskVolume, UdosRundlaufErhaeltAuchDieNullenImKopfsektor) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_udos_kopf.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    // Sollstand: Name → die Kopfsektorangaben, die eine Linux-Datei NICHT mitbringt.
    struct Kopf {
        std::string typ, attrs, erstellt, geaendert, segmente;
        uint16_t entry = 0, satz = 0, block = 0, rest = 0;
        uint16_t low = 0, high = 0, stack = 0;
        uint32_t zusatz = 0;
    };
    std::map<std::string, Kopf> vorher;
    int mit_null_speicher = 0, mit_null_rest = 0;
    for (const FileEntry& e : dv->list()) {
        if (e.type == "D") continue;         // die Verzeichnisdatei wandert nicht mit
        const std::string schluessel = dv->volumeDir(e.volume) + "/" + e.name;
        vorher[schluessel] = Kopf{e.type, e.attributes, e.created, e.date, e.segments,
                                  e.entry_addr, e.record_len, e.block_len,
                                  e.bytes_in_last, e.low_addr, e.high_addr,
                                  e.stack_size, e.extra};
        if (e.low_addr == 0 && e.high_addr == 0 && e.stack_size == 0)
            ++mit_null_speicher;
        if (e.bytes_in_last == 0) ++mit_null_rest;
    }
    ASSERT_FALSE(vorher.empty());
    // Ohne diese beiden bewiese der Test nichts — er liefe an der Sache vorbei.
    ASSERT_GT(mit_null_speicher, 0) << "keine Datei mit LOW/HIGH/STACK = 0";
    ASSERT_GT(mit_null_rest, 0)     << "keine Datei mit „letzter Satz\" = 0";

    TempOrdner ordner("k1520_test_dv_udos_kopf_o");
    ASSERT_TRUE(dv->extractAll(ordner.path(), TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(fs::exists(ordner / "udos-dateiangaben.txt"))
        << "ohne Beiblatt ist der Rundlauf von vornherein verloren";

    // Alles löschen und aus dem Ordner zurückspielen.
    for (const FileEntry& e : dv->list()) {
        if (e.type == "D") continue;
        ASSERT_TRUE(dv->erase(FileRef{e.volume, e.name})) << dv->lastError();
    }
    ASSERT_TRUE(dv->insertAll(ordner.path(), TransferOptions{})) << dv->lastError();

    int geprueft = 0;
    for (const FileEntry& e : dv->list()) {
        if (e.type == "D") continue;
        {
            const std::string schluessel = dv->volumeDir(e.volume) + "/" + e.name;
            const auto it = vorher.find(schluessel);
            ASSERT_NE(it, vorher.end()) << schluessel << " ist neu dazugekommen";
            const Kopf& a = it->second;
            EXPECT_EQ(a.typ,   e.type)          << schluessel;
            EXPECT_EQ(a.attrs, e.attributes)    << schluessel;
            EXPECT_EQ(a.entry, e.entry_addr)    << schluessel;
            EXPECT_EQ(a.satz,  e.record_len)    << schluessel;
            EXPECT_EQ(a.block, e.block_len)     << schluessel;
            EXPECT_EQ(a.rest,  e.bytes_in_last) << schluessel << " — „letzter Satz\"";
            EXPECT_EQ(a.low,   e.low_addr)      << schluessel << " — LOW ADDRESS";
            EXPECT_EQ(a.high,  e.high_addr)     << schluessel << " — HIGH ADDRESS";
            EXPECT_EQ(a.stack, e.stack_size)    << schluessel << " — STACK SIZE";
            EXPECT_EQ(a.zusatz, e.extra)        << schluessel;
            EXPECT_EQ(a.segmente, e.segments)   << schluessel;
            EXPECT_EQ(a.erstellt,  e.created)   << schluessel;
            EXPECT_EQ(a.geaendert, e.date)      << schluessel;
            ++geprueft;
        }
    }
    EXPECT_EQ(vorher.size(), static_cast<size_t>(geprueft))
        << "es sind nicht alle Dateien zurückgekommen";
}

TEST(DiskVolume, SideNPraefixImDateinamen) {
    EXPECT_EQ(FileRef::parse("Side1/HELP.DAT.00").volume, 1);
    EXPECT_EQ(FileRef::parse("Side1/HELP.DAT.00").name, "HELP.DAT.00");
    EXPECT_EQ(FileRef::parse("side0/X").volume, 0) << "Grossschreibung ist beim Lesen egal";
    EXPECT_EQ(FileRef::parse("PIP.COM").name, "PIP.COM");
    EXPECT_EQ(FileRef::parse("PIP.COM", 1).volume, 1) << "ohne Praefix gilt die Vorgabe";
}

// ─────────────────────────────────────────────────────────────────────────────
// Der ganze Anwenderfall auf einer beidseitigen UDOS-Diskette
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, UdosStapelUeberBeideSeitenHinUndZurueck) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_udos_stapel.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_EQ(dv->volumeCount(), 2);

    const size_t vorher = dv->list().size();

    // Ordner mit genau den geforderten Unterverzeichnissen.
    TempOrdner quelle("k1520_test_dv_udos_q");
    schreibe(quelle / "Side0" / "VON.SEITE.NULL", std::string(700, 'A'));
    schreibe(quelle / "Side1" / "VON.SEITE.EINS", std::string(300, 'B'));

    // Neue Dateien bringen keine Kopfsektorangaben mit; seit dem `.fileinfo`
    // raet das Werkzeug sie nicht mehr, sondern verlangt sie ausdruecklich
    // (doc/bug_disktool_Programmdatei.md §2.2, Fall 4).  Das ist hier `--type A`.
    TransferOptions neu;
    neu.udos_type = "A";
    ASSERT_TRUE(dv->insertAll(quelle.path(), neu)) << dv->lastError();

    // Jede Datei liegt auf IHRER Seite — und nur dort.
    const std::vector<FileEntry> nachher = dv->list();
    EXPECT_EQ(nachher.size(), vorher + 2);
    int auf0 = 0, auf1 = 0;
    for (const FileEntry& e : nachher) {
        if (e.name == "VON.SEITE.NULL") { ++auf0; EXPECT_EQ(e.volume, 0); }
        if (e.name == "VON.SEITE.EINS") { ++auf1; EXPECT_EQ(e.volume, 1); }
    }
    EXPECT_EQ(auf0, 1);
    EXPECT_EQ(auf1, 1);

    // Und zurueck: extractAll legt sie wieder in Side0/ bzw. Side1/ ab.
    TempOrdner ziel("k1520_test_dv_udos_z");
    ASSERT_TRUE(dv->extractAll(ziel.path(), TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(fs::exists(ziel / "Side0" / "VON.SEITE.NULL"));
    ASSERT_TRUE(fs::exists(ziel / "Side1" / "VON.SEITE.EINS"));
    EXPECT_EQ(fs::file_size(ziel / "Side0" / "VON.SEITE.NULL"), 700u);
    EXPECT_EQ(fs::file_size(ziel / "Side1" / "VON.SEITE.EINS"), 300u);
    EXPECT_FALSE(fs::exists(ziel / "Side1" / "VON.SEITE.NULL"))
        << "die Seiten duerfen nicht vermischt werden";
}

TEST(DiskVolume, UdosGleicherNameAufBeidenSeitenBleibtGetrennt) {
    // Auf udos_boot_scp.hfe ist das der Normalfall (CODE liegt auf beiden Seiten) —
    // ohne FileRef mit Seitenangabe waere die Datei nicht mehr eindeutig.
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_udos_gleich.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner quelle("k1520_test_dv_udos_gleich_q");
    schreibe(quelle / "Side0" / "GLEICH.NAME", "Seite null");
    schreibe(quelle / "Side1" / "GLEICH.NAME", "Seite eins");
    // Neue Dateien bringen keine Kopfsektorangaben mit; seit dem `.fileinfo`
    // raet das Werkzeug sie nicht mehr, sondern verlangt sie ausdruecklich
    // (doc/bug_disktool_Programmdatei.md §2.2, Fall 4).  Das ist hier `--type A`.
    TransferOptions neu;
    neu.udos_type = "A";
    ASSERT_TRUE(dv->insertAll(quelle.path(), neu)) << dv->lastError();

    TempOrdner ziel("k1520_test_dv_udos_gleich_z");
    ASSERT_TRUE(dv->extract(FileRef::parse("Side0/GLEICH.NAME"),
                            (ziel / "a").string(), TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(dv->extract(FileRef::parse("Side1/GLEICH.NAME"),
                            (ziel / "b").string(), TransferOptions{})) << dv->lastError();

    auto lies = [&](const char* n) {
        std::ifstream f((ziel / n).string(), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    };
    EXPECT_EQ(lies("a").substr(0, 10), "Seite null");
    EXPECT_EQ(lies("b").substr(0, 10), "Seite eins");
}

// ─────────────────────────────────────────────────────────────────────────────
// Schreibschutz — beim blossen Lesen soll nichts kaputtgehen koennen
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, WirdSchreibgeschuetztGeoeffnet) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.img"), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_TRUE(dv->readOnly()) << "Vorgabe beim Oeffnen ist Schreibschutz";

    // Lesen geht uneingeschraenkt …
    EXPECT_EQ(dv->list().size(), 24u);

    // … Aendern nicht, und die Meldung sagt, was zu tun ist.
    TempOrdner q("k1520_test_dv_ro_q");
    schreibe(q / "NEU.TXT", "Inhalt");
    EXPECT_FALSE(dv->insert((q / "NEU.TXT").string(), FileRef{0, "NEU.TXT"},
                            TransferOptions{}));
    EXPECT_NE(dv->lastError().find("schreibgeschuetzt"), std::string::npos)
        << dv->lastError();
    EXPECT_FALSE(dv->erase(FileRef{0, "PIP.COM"}));
    EXPECT_FALSE(dv->insertAll(q.path(), TransferOptions{}));
    EXPECT_FALSE(dv->dirty()) << "nichts davon darf das Medium angefasst haben";
}

TEST(DiskVolume, SchreibgeschuetztesOeffnenLaesstDieDateiUnberuehrt) {
    // Der eigentliche Zweck: eine Diskette, die man nur ansieht, muss danach
    // BYTEGLEICH sein — auch wenn das Programm dazwischen abstuerzt oder das
    // Objekt einfach zerstoert wird (DiskImage::flush laeuft aus dem Destruktor).
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_ro_datei.img");
    const auto vorher = fs::last_write_time(k.path());
    std::vector<char> inhalt_vorher;
    {
        std::ifstream f(k.path(), std::ios::binary);
        inhalt_vorher.assign(std::istreambuf_iterator<char>(f),
                             std::istreambuf_iterator<char>());
    }

    {
        std::string err;
        auto dv = oeffne(k.path(), "cpa780", err);
        ASSERT_NE(dv, nullptr) << err;
        (void)dv->list();
        TempOrdner ziel("k1520_test_dv_ro_ziel");
        EXPECT_TRUE(dv->extractAll(ziel.path(), TransferOptions{}));
    }   // Destruktor: DiskImage::flush() darf hier nichts schreiben

    std::vector<char> inhalt_nachher;
    {
        std::ifstream f(k.path(), std::ios::binary);
        inhalt_nachher.assign(std::istreambuf_iterator<char>(f),
                              std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(inhalt_vorher, inhalt_nachher);
    EXPECT_EQ(vorher, fs::last_write_time(k.path())) << "die Datei wurde angefasst";
}

TEST(DiskVolume, NeuAngelegteDisketteIstBeschreibbar) {
    // Der Schreibschutz schuetzt FREMDE Abbilder beim Lesen — ein gerade selbst
    // angelegtes Werkstueck waere damit nur laestig.
    const std::string pfad =
        k1520test::tempPath("k1520_test_dv_neu_rw.hfe");
    std::string err;
    auto dv = DiskVolume::create(pfad, "udos_ds77", "FRISCH", formate(),
                                 dateisysteme(), err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_FALSE(dv->readOnly());

    TempOrdner q("k1520_test_dv_neu_q");
    schreibe(q / "Side0" / "A.DAT", "x");
    schreibe(q / "Side1" / "B.DAT", "y");
    // Neue Dateien bringen keine Kopfsektorangaben mit; seit dem `.fileinfo`
    // raet das Werkzeug sie nicht mehr, sondern verlangt sie ausdruecklich
    // (doc/bug_disktool_Programmdatei.md §2.2, Fall 4).  Das ist hier `--type A`.
    TransferOptions neu;
    neu.udos_type = "A";
    EXPECT_TRUE(dv->insertAll(q.path(), neu)) << dv->lastError();

    // Erst schliessen, dann loeschen: ~DiskImage() flusht, sonst legt die eben
    // beschriebene Diskette die Datei NACH dem remove() wieder an.
    dv.reset();
    std::error_code ec;
    fs::remove(pfad, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Speichern unter / Exportieren
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, ExportSchreibtEineKopieOhneUmzubinden) {
    std::string err;
    auto dv = oeffne(fixture("cpa_cpa780_k5601_clock.hfe"), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;
    const std::string quelle = dv->path();

    const std::string ziel =
        k1520test::tempPath("k1520_test_dv_export.dmk");
    ASSERT_TRUE(dv->exportImage(ziel)) << dv->lastError();

    EXPECT_EQ(dv->path(), quelle) << "Export darf die Bindung NICHT umhaengen";
    EXPECT_TRUE(fs::exists(ziel));

    // Die Kopie traegt denselben Inhalt — im anderen Container.
    std::string err2;
    auto kopie = oeffne(ziel, "cpa780", err2);
    ASSERT_NE(kopie, nullptr) << err2;
    EXPECT_EQ(kopie->list().size(), dv->list().size());

    std::error_code ec;
    fs::remove(ziel, ec);
}

TEST(DiskVolume, UdosLaesstSichNichtAlsImgAblegen) {
    std::string err;
    auto dv = oeffne(fixture("udos_boot_scp.hfe"), "", err);
    ASSERT_NE(dv, nullptr) << err;

    const std::string ziel =
        k1520test::tempPath("k1520_test_dv_udos_export.img");
    EXPECT_FALSE(dv->exportImage(ziel));
    EXPECT_NE(dv->lastError().find("Daten-CRC"), std::string::npos) << dv->lastError();
    EXPECT_FALSE(fs::exists(ziel)) << "es darf nicht einmal eine Ruine entstehen";

    // Als .dmk dagegen schon.
    const std::string dmk =
        k1520test::tempPath("k1520_test_dv_udos_export.dmk");
    EXPECT_TRUE(dv->exportImage(dmk)) << dv->lastError();
    std::error_code ec;
    fs::remove(dmk, ec);
}

TEST(DiskVolume, SpeichernUnterBindetUmUndBleibtSchreibbar) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_saveas_q.img");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    const std::string ziel =
        k1520test::tempPath("k1520_test_dv_saveas.hfe");
    ASSERT_TRUE(dv->saveAs(ziel)) << dv->lastError();
    EXPECT_EQ(dv->path(), ziel) << "ab jetzt wird an der neuen Datei gearbeitet";

    TempOrdner q("k1520_test_dv_saveas_ordner");
    schreibe(q / "NACH.TXT", "danach geschrieben");
    ASSERT_TRUE(dv->insert((q / "NACH.TXT").string(), FileRef{0, "NACH.TXT"},
                           TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(dv->flush()) << dv->lastError();

    std::error_code ec;
    fs::remove(ziel, ec);
    fs::remove(ziel + "~", ec);
}

/**
 * @test Eine Geometrie, die in KEINEM Katalogeintrag steht, wird trotzdem gelesen.
 * @par Kriterium  Das Abbild oeffnet, `detection().format` ist `(gemessen)`, die
 *                 Anzeige sagt warum — und der Schreibschutz ist **unaufhebbar**.
 * @par Warum      Ohne das musste man erst einen `formats:`-Eintrag schreiben, nur um
 *                 eine fremde Diskette anzusehen.  Geschrieben wird trotzdem nicht: die
 *                 Geometrie ist gemessen, nicht belegt, und fremde Abbilder sind meist
 *                 Einzelstuecke (doc/design/13_k1520disktool.md §12.4).
 */
TEST(DiskVolume, UnbekannteGeometrieWirdVermessenUndSchreibgeschuetztGeoeffnet) {
    const std::string pfad = k1520test::tempPath("k1520_dv_fremd.hfe");

    // 7×512 auf 40 Spuren einseitig — bewusst in keinem Katalogeintrag.
    DiskFormat fremd;
    fremd.name = "nicht_im_katalog";
    fremd.tracks.push_back(TrackFormat{0, 39, 0, 0, 7, 512, Encoding::MFM, 1});
    ASSERT_EQ(formate().find("nicht_im_katalog"), nullptr);
    ASSERT_NE(DiskImage::create(pfad, fremd, /*write_protect=*/false), nullptr);

    std::string err;
    auto dv = oeffne(pfad, "", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_EQ(dv->detection().format, "(gemessen)");
    EXPECT_EQ(dv->detection().filesystem, "cpa_auto");
    EXPECT_NE(dv->detection().remarks.find("gemessene Geometrie"), std::string::npos)
        << dv->detection().remarks;

    // Der Schreibschutz laesst sich NICHT aufheben.
    EXPECT_TRUE(dv->readOnly());
    EXPECT_TRUE(dv->readOnlyForced());
    dv->setReadOnly(false);
    EXPECT_TRUE(dv->readOnly()) << "eine geratene Geometrie darf nie beschreibbar werden";
    EXPECT_NE(dv->lastError().find("gemessen"), std::string::npos) << dv->lastError();

    std::error_code ec;
    fs::remove(pfad, ec);
}

/**
 * @test Loecher, die kein Doppelschritt sind, werden weiter abgewiesen.
 * @par Kriterium  Fehlt MITTENDRIN ein einzelner Zylinder, kommt die Diagnose statt
 *                 eines locherigen Sektorraums.
 */
TEST(DiskVolume, LochInDerMitteWirdNichtVermessen) {
    const std::string pfad = k1520test::tempPath("k1520_dv_loch.hfe");
    DiskFormat fremd;
    fremd.name = "loch";
    fremd.tracks.push_back(TrackFormat{0, 4,  0, 0, 7, 512, Encoding::MFM, 1});
    fremd.tracks.push_back(TrackFormat{6, 20, 0, 0, 7, 512, Encoding::MFM, 1});
    ASSERT_NE(DiskImage::create(pfad, fremd, /*write_protect=*/false), nullptr);

    std::string err;
    auto dv = oeffne(pfad, "", err);
    std::error_code ec;
    fs::remove(pfad, ec);

    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("Zylinder 5"), std::string::npos) << err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bootabbild — die Systemspuren vor dem Dateisystem
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @test Das Fassungsvermoegen der Systemspuren ist ein Vertrag.
 * @par Kriterium  Es ergibt sich aus Geometrie + Beginn des Dateisystems und darf
 *                 sich nicht unbemerkt verschieben — wer hier einen Wert aendert,
 *                 macht jedes vorhandene Bootabbild unbrauchbar.  cpa780 = 15104
 *                 ist zugleich das Offset, das cpmtools als `offset` fuehrt.
 */
TEST(DiskVolume, SystemspurenFassenEineFesteZahlBytes) {
    struct Fall { const char* fs; uint64_t bytes; };
    // cpa800 beginnt auf Zylinder 0 — eine Datendiskette kann nicht bootfaehig sein.
    const Fall faelle[] = {
        {"cpa780",    15104},   // c0h0 + c0h1 + c1h0 (je 26×128) + c1h1 (5×1024)
        {"scpx640",   16384},   // 4 × 16×256
        {"scpx798",   18432},   // 2 × 16×256 + 2 × 5×1024
        {"udos_ds77", 13728},   // Spuren 0–2 + Bootspur 21, je Sektor 128 + 4 Byte Kontrollblock
        {"udos_ss40", 13728},
        {"cpa800",        0},
    };
    for (const Fall& f : faelle) {
        const FsProfile* p = dateisysteme().find(f.fs);
        ASSERT_NE(p, nullptr) << f.fs;
        const DiskFormat* g = formate().find(p->format);
        ASSERT_NE(g, nullptr) << p->format;
        EXPECT_EQ(DiskVolume::bootAreaCapacity(*p, *g), f.bytes) << f.fs;
    }
}

/**
 * @test Eine angelegte Diskette traegt das mitgegebene Bootabbild Byte fuer Byte.
 * @par Kriterium  Was aus einer echten Bootdiskette herauskommt, geht unveraendert
 *                 wieder hinein — sonst ist das Abbild wertlos.  Der Rest der
 *                 Systemspuren bleibt Leerdiskette (0xE5).
 */
/**
 * @test DiskVolume/KurzesBootabbildLaesstDenRestDerSystemspurenInRuhe
 * @brief `boot-put` schreibt so viele Sektoren, wie das Abbild Bytes hat — nicht mehr.
 *
 * Der Nachbar darüber prüft das auf einer FRISCHEN Diskette (dahinter steht 0xE5).
 * Hier ist die schärfere Frage gestellt: auf einer **bestehenden** Diskette muss der
 * Rest der Systemspuren *unverändert* bleiben, nicht bloss leer sein.  Daran hängt,
 * ob ein Bootabbild ein Werkzeug ist, das man gezielt einsetzen kann — oder eine
 * Abrissbirne für den ganzen Systembereich.
 *
 * @par Kriterium  Ein Abbild über 5 Sätze fasst genau 5 Sektoren der Spur 0 an; die
 *                 übrigen 21 dieser Spur und die Spuren 1, 2 und 21 bleiben Byte für
 *                 Byte, wie sie waren.
 */
TEST(DiskVolume, KurzesBootabbildLaesstDenRestDerSystemspurenInRuhe) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_bootkurz.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    // Sollstand: alle Sektoren der vier Systemspuren, Seite 0.
    auto lies = [&](uint8_t cyl, int idx) {
        std::vector<uint8_t> d;
        uint16_t crc = 0;
        dv->readSectorAt(cyl, 0, idx, d, crc);
        return d;
    };
    std::map<std::pair<uint8_t, int>, std::vector<uint8_t>> vorher;
    for (uint8_t cyl : {0, 1, 2, 21})
        for (int i = 0; i < 26; ++i) vorher[std::make_pair(cyl, i)] = lies(cyl, i);
    const std::vector<uint8_t> erster = vorher[std::make_pair(uint8_t{0}, 0)];
    ASSERT_FALSE(erster.empty()) << "Systemspur nicht lesbar";

    // Ein Abbild über 5 Sätze (128 B Daten + 4 B Nachspann je Satz).
    const std::vector<uint8_t> kurz(5 * (128 + 4), 0x5A);
    ASSERT_TRUE(dv->writeBootImage(kurz)) << dv->lastError();

    int angefasst = 0, fremd = 0;
    for (uint8_t cyl : {0, 1, 2, 21}) {
        for (int i = 0; i < 26; ++i) {
            if (lies(cyl, i) == vorher[std::make_pair(cyl, i)]) continue;
            ++angefasst;
            if (!(cyl == 0 && i < 5)) ++fremd;
        }
    }
    EXPECT_EQ(angefasst, 5) << "es wurden mehr Sektoren geschrieben als das Abbild fasst";
    EXPECT_EQ(fremd, 0) << "ausserhalb der ersten fünf Sektoren wurde etwas verändert";
}

TEST(DiskVolume, BootabbildGehtUnveraendertInDieSystemspuren) {
    std::string err;
    auto quelle = oeffne(fixture("cpa_cpa780_k5601_clock.img"), "cpa780", err);
    ASSERT_NE(quelle, nullptr) << err;

    std::vector<uint8_t> boot;
    ASSERT_TRUE(quelle->readBootImage(boot)) << quelle->lastError();
    ASSERT_EQ(boot.size(), 15104u);

    // Die ersten 512 Byte sind der committete Bootsektor — dieselbe Datei, die
    // test_boot_integration als Vergleich benutzt.
    std::ifstream b(fixture("bootsec_cpa780.bin"), std::ios::binary);
    ASSERT_TRUE(b.good());
    const std::vector<uint8_t> sektor((std::istreambuf_iterator<char>(b)),
                                      std::istreambuf_iterator<char>());
    ASSERT_EQ(sektor.size(), 512u);
    EXPECT_TRUE(std::equal(sektor.begin(), sektor.end(), boot.begin()));

    // Nur die halbe Systemspur schreiben — der Rest muss Leerdiskette bleiben.
    const std::string bin = k1520test::tempPath("k1520_dv_boot.bin");
    const std::vector<uint8_t> halb(boot.begin(), boot.begin() + 3328);
    std::ofstream(bin, std::ios::binary)
        .write(reinterpret_cast<const char*>(halb.data()), 3328);

    const std::string pfad = k1520test::tempPath("k1520_dv_boot.hfe");
    std::error_code ec;
    fs::remove(pfad, ec);
    auto neu = DiskVolume::create(pfad, "cpa780", "", formate(), dateisysteme(), err, bin);
    ASSERT_NE(neu, nullptr) << err;
    EXPECT_EQ(neu->bootAreaSize(), 15104u);

    std::vector<uint8_t> zurueck;
    ASSERT_TRUE(neu->readBootImage(zurueck)) << neu->lastError();
    ASSERT_EQ(zurueck.size(), 15104u);
    EXPECT_TRUE(std::equal(halb.begin(), halb.end(), zurueck.begin()));
    EXPECT_TRUE(std::all_of(zurueck.begin() + 3328, zurueck.end(),
                            [](uint8_t x) { return x == 0xE5; }))
        << "hinter dem Bootabbild steht keine Leerdiskette mehr";

    neu.reset();
    fs::remove(pfad, ec);
    fs::remove(bin, ec);
}

/**
 * @test Ein zu grosses Bootabbild legt GAR NICHTS an.
 * @par Kriterium  Geprueft wird vor dem Formatieren — sonst bliebe eine halbfertige
 *                 Diskette liegen, und die Meldung nennt beide Zahlen.
 */
TEST(DiskVolume, ZuGrossesBootabbildLegtKeineDisketteAn) {
    const std::string bin = k1520test::tempPath("k1520_dv_zugross.bin");
    { std::ofstream f(bin, std::ios::binary);
      const std::vector<uint8_t> x(15105, 0x5A);
      f.write(reinterpret_cast<const char*>(x.data()), 15105); }

    const std::string pfad = k1520test::tempPath("k1520_dv_zugross.hfe");
    std::error_code ec;
    fs::remove(pfad, ec);

    std::string err;
    auto dv = DiskVolume::create(pfad, "cpa780", "", formate(), dateisysteme(), err, bin);
    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("15105"), std::string::npos) << err;
    EXPECT_NE(err.find("15104"), std::string::npos) << err;
    EXPECT_FALSE(fs::exists(pfad)) << "die Diskette wurde trotz Fehler angelegt";
    fs::remove(bin, ec);
}

/**
 * @test Ohne Systemspuren gibt es kein Bootabbild — mit Begruendung.
 * @par Kriterium  `cpa800` beginnt auf Zylinder 0; die Meldung sagt genau das,
 *                 statt bloss „passt nicht".
 */
TEST(DiskVolume, DateisystemOhneSystemspurenLehntBootabbildAb) {
    const std::string bin = k1520test::tempPath("k1520_dv_kb.bin");
    { std::ofstream f(bin, std::ios::binary); f << "SYL"; }

    const std::string pfad = k1520test::tempPath("k1520_dv_kb.hfe");
    std::error_code ec;
    fs::remove(pfad, ec);

    std::string err;
    auto dv = DiskVolume::create(pfad, "cpa800", "", formate(), dateisysteme(), err, bin);
    EXPECT_EQ(dv, nullptr);
    EXPECT_NE(err.find("Systemspuren"), std::string::npos) << err;
    EXPECT_NE(err.find("Zylinder 0"), std::string::npos) << err;
    EXPECT_FALSE(fs::exists(pfad));
    fs::remove(bin, ec);
}

/**
 * @test Eine schreibgeschuetzt geoeffnete Diskette nimmt kein Bootabbild an.
 * @par Kriterium  Der Schreibschutz gilt fuer die Systemspuren genauso wie fuer
 *                 Dateien — sonst waere er an der interessantesten Stelle wirkungslos.
 */
TEST(DiskVolume, SchreibgeschuetzteDisketteNimmtKeinBootabbild) {
    Kopie k("cpa_cpa780_k5601_clock.hfe", "k1520_dv_bootro.hfe");
    std::string err;
    auto dv = oeffne(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_FALSE(dv->writeBootImage(std::vector<uint8_t>(128, 0x11)));
    EXPECT_NE(dv->lastError().find("schreibgeschuetzt"), std::string::npos)
        << dv->lastError();
}

/**
 * @test UDOS-Kopfsektorangaben überleben den Rundlauf über den Linux-Ordner.
 * @par Kriterium  Typ, Eigenschaften, Startadresse, Satzlänge und das Speicherabbild
 *                 (Ladeadresse + Länge) stehen NICHT in der Datei — sie kommen über
 *                 das Beiblatt zurück.  Ohne sie wird aus `ZDOS` (P1, 1024er Sätze,
 *                 lädt 5521 Byte nach 2600H) eine gewöhnliche Binärdatei, und die
 *                 Diskette bootet nicht mehr.
 */
TEST(DiskVolume, UdosKopfsektorangabenUeberlebenDenRundlauf) {
    std::string err;
    auto quelle = oeffne(fixture("udos_boot_scp.hfe"), "udos_ds77", err);
    ASSERT_NE(quelle, nullptr) << err;

    TempOrdner ordner("k1520_dv_udosmeta");
    ASSERT_TRUE(quelle->extractAll(ordner.path(), TransferOptions{})) << quelle->lastError();
    ASSERT_TRUE(fs::exists(ordner / "udos-dateiangaben.txt")) << "Beiblatt fehlt";

    std::map<std::string, FileEntry> vorher;
    for (const FileEntry& e : quelle->list()) vorher[e.name] = e;
    quelle.reset();

    const std::string pfad = k1520test::tempPath("k1520_dv_udosmeta.hfe");
    std::error_code ec;
    fs::remove(pfad, ec);
    auto neu = DiskVolume::create(pfad, "udos_ds77", "UDOS.SYS.4.3",
                                  formate(), dateisysteme(), err);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_TRUE(neu->insertAll(ordner.path(), TransferOptions{})) << neu->lastError();

    int geprueft = 0;
    for (const FileEntry& e : neu->list()) {
        const auto it = vorher.find(e.name);
        if (it == vorher.end() || e.type == "D") continue;
        const FileEntry& q = it->second;
        EXPECT_EQ(e.type,       q.type)       << e.name;
        EXPECT_EQ(e.attributes, q.attributes) << e.name;
        EXPECT_EQ(e.entry_addr, q.entry_addr) << e.name;
        EXPECT_EQ(e.record_len, q.record_len) << e.name;
        EXPECT_EQ(e.segment_start,  q.segment_start)  << e.name;
        EXPECT_EQ(e.segment_len,  q.segment_len)  << e.name;
        EXPECT_EQ(e.size,       q.size)       << e.name;
        ++geprueft;
    }
    EXPECT_GT(geprueft, 40) << "es wurden kaum Dateien verglichen";

    neu.reset();
    fs::remove(pfad, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sektoransicht und Sektoreditor (§19) — auf der Ebene, die die C-ABI benutzt
// ─────────────────────────────────────────────────────────────────────────────

TEST(DiskVolume, SektoransichtLiefertDieSpurenDesMediums) {
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_sekt.img");
    std::string err;
    auto dv = oeffne(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    EXPECT_GT(dv->mediumCylinders(), 0);
    EXPECT_EQ(dv->mediumHeads(), 2);

    const TrackView v = dv->trackView(0, 0);
    EXPECT_TRUE(v.exists);
    EXPECT_TRUE(v.formatted);
    EXPECT_EQ(v.sectors, 26);
    ASSERT_FALSE(v.spans.empty());

    // Eine Spur ausserhalb der Ausdehnung gibt es schlicht nicht.
    const TrackView weg = dv->trackView(static_cast<uint8_t>(dv->mediumCylinders()), 0);
    EXPECT_FALSE(weg.exists);

    std::vector<uint8_t> daten;
    uint16_t crc = 0;
    ASSERT_TRUE(dv->readSectorAt(0, 0, 0, daten, crc)) << dv->lastError();
    EXPECT_EQ(daten.size(), 128u);
    uint16_t soll = 0;
    ASSERT_TRUE(dv->sectorCrcFor(0, 0, 0, daten, soll));
    EXPECT_EQ(crc, soll) << "ein heiler Sektor traegt die CRC, die zu ihm gehoert";
}

TEST(DiskVolume, SchreibgeschuetzteDisketteLehntJedeSektoraenderungAb) {
    // Der Schreibschutz muss ALLE neuen Wege sperren — einer, der durchrutscht,
    // faellt beim blossen Ansehen einer fremden Diskette nicht auf.
    Kopie k("cpa_cpa780_k5601_clock.img", "k1520_test_dv_sekt_ro.img");
    std::string err;
    auto dv = oeffne(k.path(), "cpa780", err);          // schreibgeschuetzt geoeffnet
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_TRUE(dv->readOnly());

    const std::vector<uint8_t> daten(128, 0x42);
    TrackCodec::NewSectorSpec spec;
    spec.id = 200;
    spec.gap_before = 24;

    EXPECT_FALSE(dv->writeSectorAt(0, 0, 0, daten, nullptr));
    EXPECT_NE(dv->lastError().find("schreibgeschuetzt"), std::string::npos)
        << dv->lastError();
    EXPECT_FALSE(dv->writeSectorTail(0, 0, 0, {0xFF, 0xFF, 0xFF, 0xFF}));
    EXPECT_FALSE(dv->eraseSectorAt(0, 0, 0, 0));
    EXPECT_FALSE(dv->createSector(0, 0, spec, true));

    // Lesen bleibt erlaubt — und die Diskette ist unveraendert.
    std::vector<uint8_t> zurueck;
    uint16_t crc = 0;
    EXPECT_TRUE(dv->readSectorAt(0, 0, 0, zurueck, crc));
    EXPECT_EQ(dv->trackView(0, 0).sectors, 26);
}

TEST(DiskVolume, NachspannSchreibenLaesstDatenUndCrcInRuhe) {
    // Bei UDOS ist der Nachspann die Dateiverkettung.  Sie zu aendern darf weder die
    // Nutzdaten anfassen noch einen absichtlich defekten Sektor heilen.
    Kopie k("udos_boot_scp.hfe", "k1520_test_dv_tail.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    std::vector<uint8_t> daten;
    uint16_t crc = 0;
    ASSERT_TRUE(dv->readSectorAt(22, 0, 0, daten, crc)) << dv->lastError();

    // Sektor absichtlich defekt machen …
    const uint16_t falsch = 0xBEEF;
    ASSERT_TRUE(dv->writeSectorAt(22, 0, 0, daten, &falsch)) << dv->lastError();
    ASSERT_FALSE(dv->trackView(22, 0).spans[0].data_crc_ok);

    // … dann NUR die Verkettung aendern.
    const std::vector<uint8_t> neu = {0x07, 0x15, 0xFF, 0xFF};
    ASSERT_TRUE(dv->writeSectorTail(22, 0, 0, neu)) << dv->lastError();

    std::vector<uint8_t> anhang;
    ASSERT_TRUE(dv->readSectorTail(22, 0, 0, anhang));
    ASSERT_GE(anhang.size(), 4u);
    EXPECT_TRUE(std::equal(neu.begin(), neu.end(), anhang.begin()));

    std::vector<uint8_t> zurueck;
    uint16_t crc2 = 0;
    ASSERT_TRUE(dv->readSectorAt(22, 0, 0, zurueck, crc2));
    EXPECT_EQ(zurueck, daten) << "die Nutzdaten bleiben";
    EXPECT_EQ(crc2, falsch)   << "und die absichtlich falsche CRC bleibt falsch";

    // Ein zu langer Nachspann wird abgelehnt, statt in die naechste Marke zu laufen.
    EXPECT_FALSE(dv->writeSectorTail(22, 0, 0, std::vector<uint8_t>(64, 0xAA)));
}

TEST(DiskVolume, SektorAnlegenUndLoeschenUeberDieFassade) {
    Kopie k("cpa_cpa780_k5601_clock.hfe", "k1520_test_dv_sekt_neu.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "cpa780", err);
    ASSERT_NE(dv, nullptr) << err;

    const TrackView vorher = dv->trackView(0, 0);
    const int anzahl = vorher.sectors;
    const TrackSpan* dreizehn = nullptr;
    for (const TrackSpan& s : vorher.spans)
        if (s.kind == TrackSpan::Kind::Sector && s.id == 13) dreizehn = &s;
    ASSERT_NE(dreizehn, nullptr);
    const int index = dreizehn->index;
    const double lage = dreizehn->start;

    ASSERT_TRUE(dv->eraseSectorAt(0, 0, index, 0)) << dv->lastError();
    EXPECT_EQ(dv->trackView(0, 0).sectors, anzahl - 1);

    // Der Gap, der auf dieser Spur ueblich ist — so wie die Oberflaeche ihn misst.
    const TrackView luecke = dv->trackView(0, 0);
    std::vector<uint32_t> gaps;
    for (const TrackSpan& s : luecke.spans)
        if (s.kind == TrackSpan::Kind::Gap)
            gaps.push_back(static_cast<uint32_t>((s.end - s.start) * luecke.bytes + 0.5));
    std::sort(gaps.begin(), gaps.end());
    ASSERT_FALSE(gaps.empty());

    TrackCodec::NewSectorSpec spec;
    spec.id = 13;
    spec.gap_before = static_cast<uint16_t>(gaps[gaps.size() / 2]);
    uint32_t von = 0, laenge = 0;
    ASSERT_TRUE(dv->planSector(0, 0, spec, true, von, laenge));
    ASSERT_TRUE(dv->createSector(0, 0, spec, true)) << dv->lastError();

    const TrackView nachher = dv->trackView(0, 0);
    EXPECT_EQ(nachher.sectors, anzahl);
    bool gefunden = false;
    for (const TrackSpan& s : nachher.spans) {
        if (s.kind != TrackSpan::Kind::Sector || s.id != 13) continue;
        gefunden = true;
        EXPECT_TRUE(s.ok()) << "ein neu angelegter Sektor ist sofort gueltig";
        EXPECT_NEAR(s.start, lage, 1e-9) << "wieder an derselben Stelle";
        // Die Vorhersage muss sich mit der Wirklichkeit decken.
        EXPECT_NEAR(s.start * nachher.bytes, von, 1.0);
    }
    EXPECT_TRUE(gefunden);
}

// ─────────────────────────────────────────────────────────────────────────────
// `.fileinfo` — die Angaben JE DATEI (doc/bug_disktool_Programmdatei.md)
//
// Ohne diese Wächter wäre der Fehler in einem Jahr wieder da — er WAR schon
// einmal da, und niemand hat es bemerkt, weil nur der Dateiinhalt verglichen
// wurde.  Genau darin liegt seine Bosheit: die Bytes stimmen, es gibt keine
// Meldung, und die Dateisystemprüfung kann ihn grundsätzlich nicht sehen.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// @brief Der Eintrag zu @p name auf Seite @p volume — sonst ein leerer.
FileEntry eintrag(const DiskVolume& dv, int volume, const std::string& name) {
    for (const FileEntry& e : dv.list())
        if (e.volume == volume && e.name == name) return e;
    return {};
}

}  // namespace

TEST(DiskToolFileinfo, EinzelneProgrammdateiUeberlebtDenRundlauf) {
    // Genau der Fall aus dem Fehlerbericht §1: EINE Programmdatei herausholen und
    // auf eine zweite Diskette zurückschreiben.  Vorher wurde daraus Typ B mit
    // 128er-Sätzen ohne Startadresse und LOW/HIGH = FFFF — eine Datei, die UDOS
    // mit MEMORY PROTECT VIOLATION abweist.
    Kopie q("udos_boot_scp.hfe", "k1520_test_fi_quelle.hfe");
    Kopie z("udos_boot_scp.hfe", "k1520_test_fi_ziel.hfe");
    std::string err;
    auto quelle = oeffneSchreibbar(q.path(), "", err);
    ASSERT_NE(quelle, nullptr) << err;
    auto ziel = oeffneSchreibbar(z.path(), "", err);
    ASSERT_NE(ziel, nullptr) << err;

    const FileEntry vorher = eintrag(*quelle, 0, "ACTIVATE");
    ASSERT_EQ(vorher.type, "P") << "die Fixture trägt keine Programmdatei mehr";

    TempOrdner o("k1520_test_fi_o");
    ASSERT_TRUE(quelle->extract(FileRef{0, "ACTIVATE"}, (o / "ACTIVATE").string(),
                                TransferOptions{})) << quelle->lastError();
    ASSERT_TRUE(fs::exists(o / "ACTIVATE.fileinfo"))
        << "ohne die Angabendatei ist der Rundlauf von vornherein verloren";

    ASSERT_TRUE(ziel->erase(FileRef{0, "ACTIVATE"})) << ziel->lastError();
    ASSERT_TRUE(ziel->insert((o / "ACTIVATE").string(), FileRef{0, "ACTIVATE"},
                             TransferOptions{})) << ziel->lastError();

    const FileEntry nachher = eintrag(*ziel, 0, "ACTIVATE");
    EXPECT_EQ(nachher.type,          vorher.type);
    EXPECT_EQ(nachher.attributes,    vorher.attributes);
    EXPECT_EQ(nachher.entry_addr,    vorher.entry_addr);
    EXPECT_EQ(nachher.record_len,    vorher.record_len);
    EXPECT_EQ(nachher.block_len,     vorher.block_len);
    EXPECT_EQ(nachher.bytes_in_last, vorher.bytes_in_last);
    EXPECT_EQ(nachher.segment_start, vorher.segment_start);
    EXPECT_EQ(nachher.segment_len,   vorher.segment_len);
    EXPECT_EQ(nachher.segments,      vorher.segments);
    EXPECT_EQ(nachher.low_addr,      vorher.low_addr)   << "LOW ADDRESS";
    EXPECT_EQ(nachher.high_addr,     vorher.high_addr)  << "HIGH ADDRESS";
    EXPECT_EQ(nachher.stack_size,    vorher.stack_size) << "STACK SIZE";
    EXPECT_EQ(nachher.extra,         vorher.extra);
    EXPECT_EQ(nachher.created,       vorher.created);
    EXPECT_EQ(nachher.date,          vorher.date);
}

TEST(DiskToolFileinfo, JedeExtrahierteDateiHatEinFileinfo) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_fi_alle.hfe");
    std::string err;
    auto dv = oeffne(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_alle_o");
    ASSERT_TRUE(dv->extractAll(o.path(), TransferOptions{})) << dv->lastError();

    int gezaehlt = 0;
    for (const FileEntry& e : dv->list()) {
        if (e.type == "D") continue;          // Dateisystemstruktur, keine Nutzdatei
        const fs::path datei = o / dv->volumeDir(e.volume) / e.name;
        ASSERT_TRUE(fs::exists(datei)) << datei.string();
        EXPECT_TRUE(fs::exists(datei.string() + ".fileinfo"))
            << "ohne Angabendatei: " << datei.string();
        ++gezaehlt;
    }
    EXPECT_GT(gezaehlt, 0);
}

TEST(DiskToolFileinfo, CpmSchreibtNurAufVerlangenEinFileinfo) {
    // Bei CP/M ist „Nutzerbereich 0, keine Attribute" der Normalfall — es geht
    // nichts verloren, und eine Zubehördatei je Datei wäre blosser Ballast.  Also
    // Vorgabe AUS; wer eine Sammlung führt, schaltet sie ein.
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_cpm.hfe");
    std::string err;
    auto dv = oeffne(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_FALSE(dv->cpmFileinfo()) << "die Vorgabe muss AUS sein";

    TempOrdner aus("k1520_test_fi_cpm_aus");
    ASSERT_TRUE(dv->extractAll(aus.path(), TransferOptions{})) << dv->lastError();
    int mit_angaben = 0;
    for (const auto& e : fs::directory_iterator(aus.path()))
        if (e.path().extension() == ".fileinfo") ++mit_angaben;
    EXPECT_EQ(mit_angaben, 0);

    dv->setCpmFileinfo(true);
    TempOrdner an("k1520_test_fi_cpm_an");
    ASSERT_TRUE(dv->extractAll(an.path(), TransferOptions{})) << dv->lastError();
    for (const FileEntry& e : dv->list()) {
        std::string datei = e.qualifiedName();
        std::replace(datei.begin(), datei.end(), ':', '_');
        EXPECT_TRUE(fs::exists((an / datei).string() + ".fileinfo")) << datei;
    }
}

TEST(DiskToolFileinfo, CpmBrauchtKeineAngaben) {
    // Die Gegenrichtung und ein Wächter gegen Übereifer (§2.4): eine Datei OHNE
    // jede Angabe auf eine CP/M-Diskette einfügen GELINGT, landet im Nutzerbereich
    // 0 ohne Attribute und ist kein Sonderfall.
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_cpm_put.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_cpm_put_o");
    schreibe(o / "NEU.TXT", "ohne jede Angabe");
    ASSERT_TRUE(dv->insert((o / "NEU.TXT").string(), FileRef{0, "NEU.TXT"},
                           TransferOptions{})) << dv->lastError();
    EXPECT_EQ(dv->lastInsertHindernis(), InsertHindernis::Kein);

    const FileEntry e = eintrag(*dv, 0, "NEU.TXT");
    EXPECT_EQ(e.user, 0);
    EXPECT_TRUE(e.attributes.empty()) << e.attributes;
}

TEST(DiskToolFileinfo, UdosOhneAngabenWirdAbgelehnt) {
    // Geraten wird nicht: was ohne Typ und Satzlänge entstünde, wäre eine Datei,
    // die nicht läuft — und man sähe es ihr nicht an.
    Kopie k("udos_boot_scp.hfe", "k1520_test_fi_ohne.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_ohne_o");
    schreibe(o / "NACKT", "keine Angaben weit und breit");
    EXPECT_FALSE(dv->insert((o / "NACKT").string(), FileRef{0, "NACKT"},
                            TransferOptions{}));
    EXPECT_EQ(dv->lastInsertHindernis(), InsertHindernis::AngabenFehlen);
    EXPECT_NE(dv->lastError().find("--type"), std::string::npos)
        << "die Meldung muss den Ausweg nennen: " << dv->lastError();

    // Mit ausdrücklichen Angaben geht es durch — sie gehen allem vor.
    TransferOptions mit;
    mit.udos_type = "B";
    mit.udos_record_len = 128;
    EXPECT_TRUE(dv->insert((o / "NACKT").string(), FileRef{0, "NACKT"}, mit))
        << dv->lastError();
}

TEST(DiskToolFileinfo, FileinfoSchlaegtSammelbeiblatt) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_fi_vorrang.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_vorrang_o");
    schreibe(o / "STREIT", "Inhalt");
    // Das Sammelbeiblatt sagt A/128, das `.fileinfo` daneben P1/1024.
    schreibe(o / "udos-dateiangaben.txt",
             "STREIT typ=A eig=- start=0000 satz=128 block=128 rest=0 "
             "segment=0000:0 mem=0000:0000:0000 zusatz=0 erst=- geaend=-\n");
    schreibe(o / "STREIT.fileinfo",
             "fs=udos\nname=STREIT\ntyp=P1\neig=WS\nstart=4000\nsatz=1024\n"
             "block=1024\nmem=4000:43FF:0080\nzusatz=0\n");

    ASSERT_TRUE(dv->insert((o / "STREIT").string(), FileRef{0, "STREIT"},
                           TransferOptions{})) << dv->lastError();
    const FileEntry e = eintrag(*dv, 0, "STREIT");
    EXPECT_EQ(e.type, "P1");
    EXPECT_EQ(e.record_len, 1024);
    EXPECT_EQ(e.entry_addr, 0x4000);
    EXPECT_EQ(e.low_addr, 0x4000);
}

TEST(DiskToolFileinfo, StapelUeberspringtZubehoerUndZaehltEs) {
    Kopie k("udos_boot_scp.hfe", "k1520_test_fi_stapel.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_EQ(dv->volumeCount(), 2);

    TempOrdner o("k1520_test_fi_stapel_o");
    schreibe(o / "Side0" / "EINS", "Inhalt eins");
    schreibe(o / "Side0" / "EINS.fileinfo",
             "fs=udos\nname=EINS\ntyp=A\neig=-\nstart=0000\nsatz=128\n");
    schreibe(o / "Side1" / "ZWEI", "Inhalt zwei");
    schreibe(o / "Side1" / "ZWEI.fileinfo",
             "fs=udos\nname=ZWEI\ntyp=B\neig=-\nstart=0000\nsatz=128\n");

    const size_t vorher = dv->list().size();
    ASSERT_TRUE(dv->insertAll(o.path(), TransferOptions{})) << dv->lastError();
    EXPECT_EQ(dv->list().size(), vorher + 2)
        << "die .fileinfo sind als Dateien auf der Diskette gelandet";
    EXPECT_EQ(dv->lastAccessoryCount(), 2);
    EXPECT_EQ(eintrag(*dv, 0, "EINS").type, "A");
    EXPECT_EQ(eintrag(*dv, 1, "ZWEI").type, "B");
    for (const FileEntry& e : dv->list())
        EXPECT_EQ(e.name.find(".fileinfo"), std::string::npos) << e.name;
}

TEST(DiskToolFileinfo, NurZubehoerImOrdnerIstEineMeldung) {
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_leer.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_leer_o");
    schreibe(o / "X.fileinfo", "fs=cpm\nname=X\nattr=-\n");
    EXPECT_FALSE(dv->insertAll(o.path(), TransferOptions{}));
    EXPECT_NE(dv->lastError().find("nichts einzufuegen"), std::string::npos)
        << dv->lastError();
}

TEST(DiskToolFileinfo, EinzelneAngabendateiWirdAbgelehnt) {
    // Fast sicher ein Versehen — jemand hat in der Auswahl die falsche der beiden
    // gleichnamigen Zeilen erwischt.  Es GIBT einen zulässigen Grund dafür,
    // deshalb ein Schalter und kein Verbot (§2.5).
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_einzeln.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_einzeln_o");
    schreibe(o / "X.fileinfo", "fs=cpm\nname=X\nattr=-\n");
    EXPECT_FALSE(dv->insert((o / "X.fileinfo").string(), FileRef{0, "X.fileinfo"},
                            TransferOptions{}));
    EXPECT_EQ(dv->lastInsertHindernis(), InsertHindernis::Zubehoerdatei);
    EXPECT_NE(dv->lastError().find("Angabendatei"), std::string::npos)
        << dv->lastError();

    TransferOptions doch;
    doch.zubehoer_als_datei = true;
    EXPECT_TRUE(dv->insert((o / "X.fileinfo").string(), FileRef{0, "X.FIL"}, doch))
        << dv->lastError();
}

TEST(DiskToolFileinfo, FremdesDateisystemImFileinfoIstEineMeldung) {
    // `fs=` ist kein Zierat: ein .fileinfo einer CP/M-Datei darf nicht
    // stillschweigend als UDOS-Angabe gelesen werden — und umgekehrt.
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_fremd.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;

    TempOrdner o("k1520_test_fi_fremd_o");
    schreibe(o / "Y.TXT", "Inhalt");
    schreibe(o / "Y.TXT.fileinfo", "fs=udos\nname=Y.TXT\ntyp=P\nsatz=1024\n");
    EXPECT_FALSE(dv->insert((o / "Y.TXT").string(), FileRef{0, "Y.TXT"},
                            TransferOptions{}));
    EXPECT_NE(dv->lastError().find("passen nicht zusammen"), std::string::npos)
        << dv->lastError();
}

TEST(DiskToolFileinfo, CpmNutzerbereichKommtAusDemFileinfoZurueck) {
    Kopie k("cpa_cpa780_k5601_noclock.hfe", "k1520_test_fi_user.hfe");
    std::string err;
    auto dv = oeffneSchreibbar(k.path(), "", err);
    ASSERT_NE(dv, nullptr) << err;
    dv->setCpmFileinfo(true);

    TempOrdner o("k1520_test_fi_user_o");
    schreibe(o / "3_GEHEIM.TXT", "im Bereich 3, System, nur lesen");
    schreibe(o / "3_GEHEIM.TXT.fileinfo",
             "fs=cpm\nname=3:GEHEIM.TXT\nattr=RS\n");
    ASSERT_TRUE(dv->insert((o / "3_GEHEIM.TXT").string(),
                           FileRef{0, "3_GEHEIM.TXT"}, TransferOptions{}))
        << dv->lastError();
    EXPECT_EQ(dv->lastInsertedName(), "3:GEHEIM.TXT");

    const FileEntry e = eintrag(*dv, 0, "GEHEIM.TXT");
    EXPECT_EQ(e.user, 3);
    EXPECT_NE(e.attributes.find("RO"),  std::string::npos) << e.attributes;
    EXPECT_NE(e.attributes.find("SYS"), std::string::npos) << e.attributes;
}
