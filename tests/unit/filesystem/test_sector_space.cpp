/**
 * @file test_sector_space.cpp
 * @brief GoogleTests für @ref SectorSpace — die Sicht der Dateisysteme auf das Medium.
 *
 * Die tragende Zusage des Entwurfs ist, dass der **lineare** Raum byteweise dem
 * `.img`-Abbild derselben Diskette entspricht (doc/design/13_k1520disktool.md §5).  Das
 * lässt sich hier hart prüfen, weil dieselbe CP/A-Diskette als `.img` **und** als `.hfe`
 * committet ist: `cpa_cpa780_k5601_clock.img` / `.hfe`.
 *
 * @see core/filesystem/sector_space.h
 * @see doc/design/13_k1520disktool.md §5
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/sector_space.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/temp_path.h"

namespace {

std::string fixture(const char* name) {
    return (std::filesystem::path(FIXTURE_DIR) / name).string();
}

std::string tmpPath(const char* name) {
    return k1520test::tempPath(name);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

/// @brief Der ausgelieferte Katalog — dieselbe Quelle wie für den Emulator.
const FormatCatalog& katalog() {
    static FormatCatalog k = [] {
        std::string err;
        FormatCatalog c = FormatCatalog::loadDefault(&err);
        EXPECT_TRUE(err.empty()) << "formats.yaml nicht ladbar: " << err;
        return c;
    }();
    return k;
}

const DiskFormat& cpa780() {
    const DiskFormat* f = katalog().find("cpa780");
    EXPECT_NE(f, nullptr) << "Format cpa780 fehlt im Katalog";
    return *f;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Geometrie und Offsets
// ─────────────────────────────────────────────────────────────────────────────

TEST(SectorSpace, GroesseEntsprichtDemFormat) {
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    ASSERT_NE(img, nullptr);

    SectorSpace raum(img->medium(), cpa780());
    EXPECT_EQ(raum.size(), cpa780().totalBytes());
    EXPECT_EQ(raum.size(), 813824u) << "cpa780: 3×26×128 + 5×1024 + 78×2×5×1024";
}

TEST(SectorSpace, DateisystemBeginntBeiZylinder2Offset15104) {
    // Der Rechenweg aus doc/design/13_k1520disktool.md §6.2: die drei 128-B-Systemseiten
    // (3 × 3328) plus die 1024-B-Spur c1h1 (5120) ergeben 15104 — genau der `offset`,
    // den cpmtools in seiner diskdefs für A5120_160 stehen hat.
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    ASSERT_NE(img, nullptr);
    SectorSpace raum(img->medium(), cpa780());

    EXPECT_EQ(raum.offsetOf(0, 0), 0);
    EXPECT_EQ(raum.offsetOf(0, 1), 3328);
    EXPECT_EQ(raum.offsetOf(1, 0), 6656);
    EXPECT_EQ(raum.offsetOf(1, 1), 9984);
    EXPECT_EQ(raum.offsetOf(2, 0), 15104);

    // Sektor-Offsets innerhalb einer Spur folgen der ID.
    EXPECT_EQ(raum.offsetOf(2, 0, 1), 15104);
    EXPECT_EQ(raum.offsetOf(2, 0, 5), 15104 + 4 * 1024);
    EXPECT_EQ(raum.offsetOf(2, 0, 6), -1) << "Sektor 6 gibt es auf 5×1024 nicht";
}

TEST(SectorSpace, MeldetGemischteGeometrieKorrekt) {
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    ASSERT_NE(img, nullptr);
    SectorSpace raum(img->medium(), cpa780());

    EXPECT_EQ(raum.sectorSize(0, 0), 128);
    EXPECT_EQ(raum.sectorsPerTrack(0, 0), 26);
    EXPECT_EQ(raum.sectorSize(1, 1), 1024);
    EXPECT_EQ(raum.sectorsPerTrack(1, 1), 5);
    EXPECT_EQ(raum.firstSectorId(0, 0), 1);
    EXPECT_EQ(raum.sectorSize(80, 0), 0) << "Spur ausserhalb der Geometrie";
}

// ─────────────────────────────────────────────────────────────────────────────
// Der lineare Raum IST das .img — an einer echten Diskette geprüft
// ─────────────────────────────────────────────────────────────────────────────

TEST(SectorSpace, LinearerRaumIstBytegleichZumImgAbbild) {
    const std::string pfad = fixture("cpa_cpa780_k5601_clock.img");
    const std::vector<uint8_t> datei = readFile(pfad);
    ASSERT_EQ(datei.size(), 813824u);

    auto img = DiskImage::open(pfad, cpa780(), true);
    ASSERT_NE(img, nullptr);
    SectorSpace raum(img->medium(), cpa780());
    ASSERT_EQ(raum.size(), datei.size());

    std::vector<uint8_t> gelesen(datei.size());
    ASSERT_TRUE(raum.read(0, gelesen.data(), gelesen.size())) << raum.lastError();
    EXPECT_EQ(gelesen, datei);
}

TEST(SectorSpace, HfeUndImgDerselbenDisketteLiefernDenselbenRaum) {
    // Die eigentliche Zusage: das Dateisystem sieht dieselben Bytes, egal in welchem
    // Container die Diskette steckt.
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    auto hfe = DiskImage::open(fixture("cpa_cpa780_k5601_clock.hfe"), std::nullopt, true);
    ASSERT_NE(img, nullptr);
    ASSERT_NE(hfe, nullptr);

    SectorSpace a(img->medium(), cpa780());
    SectorSpace b(hfe->medium(), cpa780());
    ASSERT_EQ(a.size(), b.size());

    std::vector<uint8_t> va(a.size()), vb(b.size());
    ASSERT_TRUE(a.read(0, va.data(), va.size())) << a.lastError();
    ASSERT_TRUE(b.read(0, vb.data(), vb.size())) << b.lastError();
    EXPECT_EQ(va, vb) << ".hfe und .img derselben Diskette weichen im linearen Raum ab";
}

TEST(SectorSpace, LesenUeberSektorUndSpurgrenzen) {
    const std::vector<uint8_t> datei = readFile(fixture("cpa_cpa780_k5601_clock.img"));
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    ASSERT_NE(img, nullptr);
    SectorSpace raum(img->medium(), cpa780());

    // Über die Grenze 128-B-Sektor → nächster Sektor …
    std::vector<uint8_t> a(300);
    ASSERT_TRUE(raum.read(100, a.data(), a.size()));
    EXPECT_TRUE(std::equal(a.begin(), a.end(), datei.begin() + 100));

    // … über die Spurgrenze c1h0 → c1h1 (128 B → 1024 B, Offset 9984) …
    std::vector<uint8_t> b(4096);
    ASSERT_TRUE(raum.read(9984 - 64, b.data(), b.size()));
    EXPECT_TRUE(std::equal(b.begin(), b.end(), datei.begin() + 9984 - 64));

    // … und exakt bis zum letzten Byte.
    std::vector<uint8_t> c(16);
    ASSERT_TRUE(raum.read(raum.size() - c.size(), c.data(), c.size()));
    EXPECT_TRUE(std::equal(c.begin(), c.end(), datei.end() - 16));

    // Ein Byte darüber hinaus ist ein Fehler, kein Absturz.
    uint8_t eins = 0;
    EXPECT_FALSE(raum.read(raum.size(), &eins, 1));
}

TEST(SectorSpace, PhysischerZugriffLiefertDieselbenBytes) {
    const std::vector<uint8_t> datei = readFile(fixture("cpa_cpa780_k5601_clock.img"));
    auto img = DiskImage::open(fixture("cpa_cpa780_k5601_clock.img"), cpa780(), true);
    ASSERT_NE(img, nullptr);
    SectorSpace raum(img->medium(), cpa780());

    SectorData sec;
    ASSERT_TRUE(raum.readSector(0, 0, 1, sec)) << raum.lastError();
    EXPECT_EQ(sec.data.size(), 128u);
    EXPECT_TRUE(sec.ok()) << "Bootsektor der Fixture ist CRC-defekt";
    EXPECT_TRUE(std::equal(sec.data.begin(), sec.data.end(), datei.begin()));

    ASSERT_TRUE(raum.readSector(2, 0, 3, sec));
    EXPECT_EQ(sec.data.size(), 1024u);
    EXPECT_TRUE(std::equal(sec.data.begin(), sec.data.end(), datei.begin() + 15104 + 2 * 1024));

    EXPECT_FALSE(raum.readSector(2, 0, 9, sec)) << "Sektor 9 gibt es dort nicht";
    EXPECT_FALSE(raum.readSector(99, 0, 1, sec)) << "Spur 99 gibt es nicht";
}

TEST(SectorSpace, EchteDisketteIstDurchgaengigCrcGesund) {
    // Zugleich ein Gesundheitsnachweis der Fixture: läuft das Dateisystem später auf
    // CRC-Fehler, liegt es nicht am Medium.
    auto hfe = DiskImage::open(fixture("cpa_cpa780_k5601_clock.hfe"), std::nullopt, true);
    ASSERT_NE(hfe, nullptr);
    SectorSpace raum(hfe->medium(), cpa780());

    int gepruef = 0, defekt = 0;
    for (uint8_t c = 0; c < 80; ++c)
        for (uint8_t h = 0; h < 2; ++h) {
            const uint8_t first = raum.firstSectorId(c, h);
            const uint8_t n     = raum.sectorsPerTrack(c, h);
            for (uint8_t i = 0; i < n; ++i) {
                SectorData sec;
                ASSERT_TRUE(raum.readSector(c, h, static_cast<uint8_t>(first + i), sec))
                    << "Spur " << int(c) << "/" << int(h) << " Sektor " << int(first + i);
                ++gepruef;
                if (!sec.ok()) ++defekt;
            }
        }
    EXPECT_EQ(gepruef, 3 * 26 + 157 * 5) << "Sektorzahl der cpa780-Geometrie";
    EXPECT_EQ(defekt, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kopf-Filter (UDOS: eine Seite = ein Datenträger)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SectorSpace, KopfFilterBeschraenktDenRaumAufEineSeite) {
    auto hfe = DiskImage::open(fixture("cpa_cpa780_k5601_clock.hfe"), std::nullopt, true);
    ASSERT_NE(hfe, nullptr);

    SectorSpace seite0(hfe->medium(), cpa780(), 0);
    SectorSpace seite1(hfe->medium(), cpa780(), 1);
    SectorSpace ganz(hfe->medium(), cpa780());

    EXPECT_EQ(seite0.size() + seite1.size(), ganz.size());
    EXPECT_EQ(seite0.offsetOf(0, 0), 0);
    EXPECT_EQ(seite0.offsetOf(0, 1), -1) << "Kopf 1 gehoert nicht zu Seite 0";
    EXPECT_EQ(seite1.offsetOf(0, 1), 0) << "Seite 1 beginnt bei ihrem eigenen Offset 0";

    // Seite 0 von cpa780: c0h0 (26×128) + c1h0 (26×128) + c2..c79 h0 (5×1024)
    EXPECT_EQ(seite0.size(), 2u * 3328 + 78u * 5120);
}

// ─────────────────────────────────────────────────────────────────────────────
// Schreiben (nur auf Kopien — Fixtures werden nie angefasst)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SectorSpace, SchreibenUndZurueckgelesen) {
    const std::string kopie = tmpPath("k1520_test_ss_write.hfe");
    std::filesystem::copy_file(fixture("cpa_cpa780_k5601_clock.hfe"), kopie,
                               std::filesystem::copy_options::overwrite_existing);
    {
        auto disk = DiskImage::open(kopie, std::nullopt, false);
        ASSERT_NE(disk, nullptr);
        SectorSpace raum(disk->medium(), cpa780());

        std::vector<uint8_t> muster(1024);
        for (size_t i = 0; i < muster.size(); ++i) muster[i] = static_cast<uint8_t>(i * 7);

        ASSERT_TRUE(raum.writeSector(40, 1, 2, muster)) << raum.lastError();

        SectorData zurueck;
        ASSERT_TRUE(raum.readSector(40, 1, 2, zurueck));
        EXPECT_EQ(zurueck.data, muster);
        EXPECT_TRUE(zurueck.ok()) << "CRC nach dem Schreiben nicht nachgezogen";

        // Linearer Zugriff sieht dieselbe Änderung.
        const int64_t off = raum.offsetOf(40, 1, 2);
        ASSERT_GE(off, 0);
        std::vector<uint8_t> linear(1024);
        ASSERT_TRUE(raum.read(static_cast<uint64_t>(off), linear.data(), linear.size()));
        EXPECT_EQ(linear, muster);
    }
    std::filesystem::remove(kopie);
}

TEST(SectorSpace, LinearesSchreibenUeberSektorgrenzeHinweg) {
    const std::string kopie = tmpPath("k1520_test_ss_write_lin.hfe");
    std::filesystem::copy_file(fixture("cpa_cpa780_k5601_clock.hfe"), kopie,
                               std::filesystem::copy_options::overwrite_existing);
    {
        auto disk = DiskImage::open(kopie, std::nullopt, false);
        ASSERT_NE(disk, nullptr);
        SectorSpace raum(disk->medium(), cpa780());

        // Über die 128-B-Sektorgrenze in der Systemspur: ab Byte 100, 300 Bytes lang.
        std::vector<uint8_t> muster(300);
        for (size_t i = 0; i < muster.size(); ++i) muster[i] = static_cast<uint8_t>(0xA0 + i);
        ASSERT_TRUE(raum.write(100, muster.data(), muster.size())) << raum.lastError();

        std::vector<uint8_t> zurueck(300);
        ASSERT_TRUE(raum.read(100, zurueck.data(), zurueck.size()));
        EXPECT_EQ(zurueck, muster);

        // Die Nachbarbytes DAVOR und DANACH bleiben unangetastet …
        SectorData s0, s4;
        ASSERT_TRUE(raum.readSector(0, 0, 1, s0));
        ASSERT_TRUE(raum.readSector(0, 0, 4, s4));
        EXPECT_TRUE(s0.ok());
        EXPECT_TRUE(s4.ok());
    }
    std::filesystem::remove(kopie);
}

TEST(SectorSpace, SchreibenAufNichtVorhandeneSpurScheitertSauber) {
    const std::string kopie = tmpPath("k1520_test_ss_write_bad.hfe");
    std::filesystem::copy_file(fixture("cpa_cpa780_k5601_clock.hfe"), kopie,
                               std::filesystem::copy_options::overwrite_existing);
    {
        auto disk = DiskImage::open(kopie, std::nullopt, false);
        ASSERT_NE(disk, nullptr);
        SectorSpace raum(disk->medium(), cpa780());

        EXPECT_FALSE(raum.writeSector(99, 0, 1, std::vector<uint8_t>(1024, 0)));
        EXPECT_FALSE(raum.lastError().empty());
        EXPECT_FALSE(raum.writeSector(2, 0, 1, std::vector<uint8_t>(128, 0)))
            << "falsche Sektorlaenge";
    }
    std::filesystem::remove(kopie);
}
