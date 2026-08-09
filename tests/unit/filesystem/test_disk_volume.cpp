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
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"

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

/// @brief Temporaerer Ordner, raeumt sich weg.
class TempOrdner {
public:
    explicit TempOrdner(const char* name)
        : pfad_((fs::temp_directory_path() / name).string()) {
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
        : pfad_((fs::temp_directory_path() / temp_name).string()) {
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

    int n0 = 0, n1 = 0;
    for (const auto& e : fs::directory_iterator(ziel / "Side0")) { (void)e; ++n0; }
    for (const auto& e : fs::directory_iterator(ziel / "Side1")) { (void)e; ++n1; }
    EXPECT_EQ(n0, 47);
    EXPECT_EQ(n1, 22);

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
    auto dv = oeffne(k.path(), "", err);
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
    auto dv = oeffne(fixture("udos_boot_scp.hfe"), "", err);
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
    auto dv = oeffne(k.path(), "cpa780", err);
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
    auto dv = oeffne(k.path(), "cpa780", err);
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
    auto dv = oeffne(k.path(), "cpa780", err);
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
    auto dv = oeffne(k.path(), "cpa780", err);
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
