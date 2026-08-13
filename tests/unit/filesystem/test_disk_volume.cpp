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
    int n0 = 0, n1 = 0;
    for (const auto& e : fs::directory_iterator(ziel / "Side0")) { (void)e; ++n0; }
    for (const auto& e : fs::directory_iterator(ziel / "Side1")) { (void)e; ++n1; }
    EXPECT_EQ(n0, 46);
    EXPECT_EQ(n1, 21);
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

    ASSERT_TRUE(dv->insertAll(quelle.path(), TransferOptions{})) << dv->lastError();

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
    ASSERT_TRUE(dv->insertAll(quelle.path(), TransferOptions{})) << dv->lastError();

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
    EXPECT_TRUE(dv->insertAll(q.path(), TransferOptions{})) << dv->lastError();

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
