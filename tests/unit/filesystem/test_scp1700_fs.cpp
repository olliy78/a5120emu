/**
 * @file test_scp1700_fs.cpp
 * @brief GoogleTests für die SCP1700-Diskette des A7100 (CP/M-86).
 *
 * ### Woher die Sollwerte stammen
 * Alles an der Referenzdiskette gemessen (Greaseweazle F1, 300 min⁻¹, 2026-08-18);
 * die Fixture ist dieselbe Aufnahme (`tests/fixtures/README.md`).
 *
 * | Sollwert | Quelle |
 * |---|---|
 * | c0h0 = FM, 16×128, **halbe** Datenrate (Flusszeiten 4/8 µs) | Rohfluss gemessen |
 * | alle übrigen Spuren = MFM, 16×256 (4/6/8 µs) | Rohfluss gemessen |
 * | Verzeichnis ab c2h0, 128 Plätze, 2048-B-Blöcke, 16-Bit-Zeiger | Verzeichnis gelesen |
 * | 312 Blöcke (höchster benutzter 311) über 156 Datenspuren | nachgerechnet |
 * | 46 Dateien, 2048 Byte frei | Datenträger gelesen |
 *
 * Das CP/A-BIOS kennt diese Disketten übrigens: „Jeder Fehler in Spur 0, Sektor 1
 * führt zur Annahme von Systemspuren (A7100-System mit 5" FM und 8272-Hardware mit
 * 128er Sektorlänge)" — `biosdsk.mac`, s. `disks/cpa_cpa780_k5601_clock.prn`.
 *
 * @see doc/scp1700_diskettenformat.md
 * @see core/filesystem/cpm/cpm_fs.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/geometry_probe.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kFixture = "scp1700_640k_a7100_system.hfe";

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

/// @brief Oeffnen OHNE Vorgabe — die Erkennung soll selbst darauf kommen.
std::unique_ptr<DiskVolume> oeffne(const std::string& pfad, std::string& err) {
    return DiskVolume::open(pfad, "", formate(), dateisysteme(), err);
}

/// @brief Temporaerer Ordner, raeumt sich weg.
class TempOrdner {
public:
    explicit TempOrdner(const char* name) : pfad_(k1520test::tempPath(name)) {
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

/// @brief Arbeitskopie der Fixture (das Original wird nie beschrieben).
class Arbeitskopie {
public:
    explicit Arbeitskopie(const char* name)
        : pfad_(k1520test::tempPath(std::string("k1520_test_") + name)) {
        fs::copy_file(fixture(kFixture), pfad_, fs::copy_options::overwrite_existing);
    }
    ~Arbeitskopie() { std::error_code ec; fs::remove(pfad_, ec); }
    const std::string& pfad() const { return pfad_; }
private:
    std::string pfad_;
};

}  // namespace

/**
 * @test Scp1700.WirdOhneVorgabeErkannt
 * @brief Format und Dateisystem stehen ohne `--fs` fest.
 *
 * Die Datenspuren allein (16×256 MFM) passten auch zu `cpa640`, `k5601_16x256` und
 * damit zu UDOS1715; die **FM-Bootspur** ist das Unterscheidungsmerkmal, und nur
 * `scp1700_640` beschreibt sie.
 */
TEST(Scp1700, WirdOhneVorgabeErkannt) {
    std::string err;
    auto dv = oeffne(fixture(kFixture), err);
    ASSERT_TRUE(dv) << err;
    EXPECT_EQ(dv->detection().format,     "scp1700_640");
    EXPECT_EQ(dv->detection().filesystem, "scp1700");
    EXPECT_EQ(dv->volumeCount(), 1) << "eine CP/M-86-Diskette ist EIN Dateisystem";
}

/**
 * @test Scp1700.BootspurIstFmMitHalberDatenrate
 * @brief c0h0 trägt FM mit 128-B-Sektoren und **halber** Rate, alles Übrige MFM.
 *
 * Der Faktor an der Spur (@ref TrackImage::cell_factor) ist keine Zierde: ohne ihn
 * ginge die Bootspur beim Zurückschreiben mit doppelter Rate auf die Diskette.
 */
TEST(Scp1700, BootspurIstFmMitHalberDatenrate) {
    const DiskFormat* f = formate().find("scp1700_640");
    ASSERT_NE(f, nullptr);
    auto img = DiskImage::open(fixture(kFixture), *f, /*write_protect=*/true);
    ASSERT_TRUE(img);

    const TrackImage& boot = img->medium().track(0, 0);
    EXPECT_EQ(boot.encoding, Encoding::FM);
    EXPECT_EQ(boot.cell_factor, 2) << "125 kbit/s — halbe Rate";

    const TrackImage& daten = img->medium().track(0, 1);
    EXPECT_EQ(daten.encoding, Encoding::MFM);
    EXPECT_EQ(daten.cell_factor, 1);

    const auto boot_secs = TrackCodec::parseTrack(boot);
    ASSERT_FALSE(boot_secs.empty());
    EXPECT_EQ(boot_secs.front().size, 128);

    const auto daten_secs = TrackCodec::parseTrack(daten);
    ASSERT_EQ(daten_secs.size(), 16u);
    EXPECT_EQ(daten_secs.front().size, 256);
}

/**
 * @test Scp1700.BootspurZaehltVerschiedeneSektorenNichtDoppelte
 * @brief Die Bootspur wurde über die Umdrehung hinaus beschrieben — 16 IDs, 19 Marken.
 *
 * Am Ende der Spur steht ein zweites Exemplar der Sektoren 1…4.  Für den Abgleich
 * mit einem Format zählen die VERSCHIEDENEN IDs; nach der rohen Zahl passte die
 * Diskette zu keinem Format (@ref MeasuredTrack::uniqueSectors).
 */
TEST(Scp1700, BootspurZaehltVerschiedeneSektorenNichtDoppelte) {
    const DiskFormat* f = formate().find("scp1700_640");
    ASSERT_NE(f, nullptr);
    auto img = DiskImage::open(fixture(kFixture), *f, true);
    ASSERT_TRUE(img);

    const auto gemessen = GeometryProbe::measure(img->medium());
    auto it = std::find_if(gemessen.begin(), gemessen.end(),
                           [](const MeasuredTrack& t) { return t.cyl == 0 && t.head == 0; });
    ASSERT_NE(it, gemessen.end());
    EXPECT_GT(it->sectors, it->uniqueSectors()) << "Doppelgaenger am Spurende";
    EXPECT_LE(it->uniqueSectors(), 16) << "mehr als 16 verschiedene gibt es nicht";
    EXPECT_EQ(it->encoding, Encoding::FM);
}

/**
 * @test Scp1700.VerzeichnisUndInhaltStimmen
 * @brief Dateien, Größen und der Inhalt einer Textdatei.
 */
TEST(Scp1700, VerzeichnisUndInhaltStimmen) {
    std::string err;
    auto dv = oeffne(fixture(kFixture), err);
    ASSERT_TRUE(dv) << err;

    const std::vector<FileEntry> dateien = dv->list();
    EXPECT_EQ(dateien.size(), 46u);

    auto suche = [&](const char* name) -> const FileEntry* {
        auto it = std::find_if(dateien.begin(), dateien.end(),
                               [&](const FileEntry& e) { return e.name == name; });
        return it == dateien.end() ? nullptr : &*it;
    };
    // Der Liefersatz eines CP/M-86: Betriebssystem, Assembler, Binder, Debugger.
    ASSERT_NE(suche("SCP.SYS"),    nullptr);
    ASSERT_NE(suche("RASM86.CMD"), nullptr);
    ASSERT_NE(suche("LINK86.CMD"), nullptr);
    ASSERT_NE(suche("DDT86.CMD"),  nullptr);
    EXPECT_EQ(suche("SCP.SYS")->size,    18048u);
    EXPECT_EQ(suche("DOKERG.TXT")->size,   512u);

    TempOrdner ziel("k1520_test_scp1700_inhalt");
    ASSERT_TRUE(dv->extract(FileRef{0, "DOKERG.TXT"}, (ziel / "DOKERG.TXT").string(),
                            TransferOptions{})) << dv->lastError();
    std::ifstream f((ziel / "DOKERG.TXT").string(), std::ios::binary);
    ASSERT_TRUE(f);
    std::string text(24, '\0');
    f.read(text.data(), 24);
    EXPECT_EQ(text, "Bei der Arbeit mit einem");

    // 2 KB frei — die Diskette ist praktisch voll.
    EXPECT_EQ(dv->volumeInfo(0).free_bytes, 2048u);
}

/**
 * @test Scp1700.SchreibenUndZurueckschreibenBleibtLesbar
 * @brief Datei einfügen, Datei speichern, Diskette neu öffnen — alles noch da.
 *
 * Der Punkt ist nicht die Datei, sondern der Rundlauf durch den HFE-Codec: die
 * gemischte Diskette muss ihn überstehen, und die FM-Bootspur ihre halbe Rate
 * behalten.
 */
TEST(Scp1700, SchreibenUndZurueckschreibenBleibtLesbar) {
    Arbeitskopie kopie("scp1700_schreiben.hfe");

    TempOrdner quelle("k1520_test_scp1700_quelle");
    const std::string text = "PROBE VOM WERKZEUG\r\n";
    std::ofstream(quelle / "PROBE.TXT", std::ios::binary) << text;
    {
        std::string err;
        auto dv = oeffne(kopie.pfad(), err);
        ASSERT_TRUE(dv) << err;
        dv->setReadOnly(false);
        // Platz schaffen: die Diskette ist bis auf 2 KB voll.
        ASSERT_TRUE(dv->erase(FileRef{0, "DOKERG.TXT"})) << dv->lastError();
        ASSERT_TRUE(dv->insert((quelle / "PROBE.TXT").string(), FileRef{0, "PROBE.TXT"},
                               TransferOptions{})) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }

    std::string err;
    auto neu = oeffne(kopie.pfad(), err);
    ASSERT_TRUE(neu) << err;
    EXPECT_EQ(neu->detection().format,     "scp1700_640");
    EXPECT_EQ(neu->detection().filesystem, "scp1700");

    const std::vector<FileEntry> dateien = neu->list();
    EXPECT_EQ(dateien.size(), 46u) << "eine geloescht, eine geschrieben";
    EXPECT_NE(std::find_if(dateien.begin(), dateien.end(),
                           [](const FileEntry& e) { return e.name == "PROBE.TXT"; }),
              dateien.end());

    TempOrdner zurueck("k1520_test_scp1700_zurueck");
    ASSERT_TRUE(neu->extract(FileRef{0, "PROBE.TXT"}, (zurueck / "PROBE.TXT").string(),
                             TransferOptions{})) << neu->lastError();
    std::ifstream g((zurueck / "PROBE.TXT").string(), std::ios::binary);
    ASSERT_TRUE(g);
    std::string gelesen(text.size(), '\0');
    g.read(gelesen.data(), static_cast<std::streamsize>(text.size()));
    EXPECT_EQ(gelesen, text);

    // Und die Bootspur laeuft immer noch mit halber Rate.
    const DiskFormat* f = formate().find("scp1700_640");
    ASSERT_NE(f, nullptr);
    auto img = DiskImage::open(kopie.pfad(), *f, true);
    ASSERT_TRUE(img);
    EXPECT_EQ(img->medium().track(0, 0).encoding, Encoding::FM);
    EXPECT_EQ(img->medium().track(0, 0).cell_factor, 2);
}
