/**
 * @file test_disk_image.cpp
 * @brief GoogleTests für DiskImage — Dateibindung, Autosave und „Speichern unter".
 *
 * Schwerpunkte:
 *   - createBlank(): echte Leerdiskette in Laufwerksgeometrie, ohne Datei
 *   - Autosave: geänderte Spuren landen VERZÖGERT in der gebundenen Datei
 *   - saveAs(): Containerwechsel (.hfe ⇄ .dmk ⇄ .img) inklusive Umbinden —
 *     ab dann folgt der Autosave der NEUEN Datei
 *   - `.img` als Ziel wird abgelehnt, solange das Medium nicht darstellbar ist
 *   - Für `.hfe`/`.dmk` wird KEIN Diskettenformat verlangt (self-describing)
 *
 * @see core/peripherals/floppy_drive/disk_image.h
 * @see doc/design/09_floppy_drive.md §6
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"

namespace {

std::string tmpPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

/// @brief 2 Zylinder × 1 Kopf × 2 × 128 B.
DiskFormat makeSimpleFormat() {
    DiskFormat fmt;
    fmt.name = "test_simple";
    fmt.tracks.push_back({0, 1, 0, 0, 2, 128});
    return fmt;
}

TrackImage makeTrack(uint8_t cyl, int nsec, uint16_t size, uint8_t fill) {
    std::vector<LogicalSector> secs;
    for (int i = 1; i <= nsec; ++i) {
        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = 0;
        ls.id   = static_cast<uint8_t>(i);
        ls.size = size;
        ls.data.assign(size, fill);
        secs.push_back(std::move(ls));
    }
    return TrackCodec::buildTrack(secs, Encoding::MFM);
}

}  // namespace

// ─── createBlank ─────────────────────────────────────────────────────────────

/**
 * @test DiskImageBlank/LiefertUnformatiertesMediumOhneDatei
 * @brief Der Kern der neuen Leerdiskette: Geometrie kommt vom Laufwerk, alle Spuren
 *        sind unformatiert, es gibt (noch) keine Datei.
 */
TEST(DiskImageBlank, LiefertUnformatiertesMediumOhneDatei) {
    auto img = DiskImage::createBlank(80, 2, Encoding::MFM);
    ASSERT_NE(img, nullptr);

    EXPECT_FALSE(img->hasFile());
    EXPECT_TRUE(img->path().empty());
    EXPECT_EQ(img->geometry().num_cyls,  80u);
    EXPECT_EQ(img->geometry().num_heads,  2u);
    EXPECT_FALSE(img->medium().formatted());
    EXPECT_TRUE(img->readTrack(0, 0).empty());
    EXPECT_FALSE(img->rawCompatible()) << "Leerdiskette darf nicht als .img gelten";
    EXPECT_TRUE(img->writable());

    // Ohne Bindung ist flush() ein No-op und darf nicht scheitern.
    EXPECT_TRUE(img->writeTrack(3, 1, makeTrack(3, 2, 128, 0xE5)));
    EXPECT_TRUE(img->flush());
}

TEST(DiskImageBlank, UnplausibleGeometrie_gibtNullptr) {
    EXPECT_EQ(DiskImage::createBlank(0, 2, Encoding::MFM), nullptr);
    EXPECT_EQ(DiskImage::createBlank(80, 0, Encoding::MFM), nullptr);
}

// ─── saveAs / Umbinden ───────────────────────────────────────────────────────

/**
 * @test DiskImageSaveAs/LeerdisketteNachHfeUndZurueck
 * @brief Eine Leerdiskette lässt sich als `.hfe` sichern; danach ist sie an diese
 *        Datei gebunden und der Autosave schreibt dorthin.
 */
TEST(DiskImageSaveAs, LeerdisketteNachHfeUndZurueck) {
    const auto path = tmpPath("k1520_test_di_blank.hfe");
    std::filesystem::remove(path);

    auto img = DiskImage::createBlank(40, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);
    ASSERT_TRUE(img->saveAs(path, std::nullopt)) << img->lastError();

    EXPECT_TRUE(img->hasFile());
    EXPECT_EQ(img->path(), path);
    EXPECT_EQ(img->container(), ContainerType::Hfe);
    EXPECT_TRUE(std::filesystem::exists(path));

    auto wieder = DiskImage::open(path, std::nullopt, false);
    ASSERT_NE(wieder, nullptr);
    EXPECT_EQ(wieder->geometry().num_cyls, 40u);
    EXPECT_FALSE(wieder->medium().formatted());

    std::filesystem::remove(path);
}

/**
 * @test DiskImageSaveAs/LehntImgFuerLeerdisketteAb
 * @brief „Ein Abspeichern dieses Abbildes als .img muss zu einer Fehlermeldung führen."
 */
TEST(DiskImageSaveAs, LehntImgFuerLeerdisketteAb) {
    const auto path = tmpPath("k1520_test_di_blank.img");
    std::filesystem::remove(path);

    auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);

    EXPECT_FALSE(img->saveAs(path, makeSimpleFormat()));
    EXPECT_NE(std::string(img->lastError()).find("unformatiert"), std::string::npos)
        << img->lastError();
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(img->hasFile()) << "gescheitertes saveAs darf nicht umbinden";
}

/**
 * @test DiskImageSaveAs/ImgOhneFormat_wirdAbgelehnt
 * @brief Das Diskettenformat ist NUR für `.img` nötig — und dort Pflicht.
 */
TEST(DiskImageSaveAs, ImgOhneFormat_wirdAbgelehnt) {
    auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);
    img->writeTrack(0, 0, makeTrack(0, 2, 128, 0x11));
    img->writeTrack(1, 0, makeTrack(1, 2, 128, 0x22));
    ASSERT_TRUE(img->rawCompatible());

    EXPECT_FALSE(img->saveAs(tmpPath("k1520_test_di_nofmt.img"), std::nullopt));
    EXPECT_NE(std::string(img->lastError()).find("Diskettenformat"), std::string::npos)
        << img->lastError();
}

/**
 * @test DiskImageSaveAs/ContainerwechselHfeNachDmkNachImg
 * @brief Dieselbe Diskette durch alle drei Container — die Nutzdaten überleben, und
 *        die Bindung folgt jeweils der neuen Datei.
 */
TEST(DiskImageSaveAs, ContainerwechselHfeNachDmkNachImg) {
    const auto hfe = tmpPath("k1520_test_di_chain.hfe");
    const auto dmk = tmpPath("k1520_test_di_chain.dmk");
    const auto img_p = tmpPath("k1520_test_di_chain.img");
    for (const auto& p : {hfe, dmk, img_p}) std::filesystem::remove(p);

    auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);
    img->writeTrack(0, 0, makeTrack(0, 2, 128, 0xAA));
    img->writeTrack(1, 0, makeTrack(1, 2, 128, 0xBB));

    ASSERT_TRUE(img->saveAs(hfe, std::nullopt)) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Hfe);

    ASSERT_TRUE(img->saveAs(dmk, std::nullopt)) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Dmk);
    EXPECT_EQ(img->path(), dmk);

    ASSERT_TRUE(img->saveAs(img_p, makeSimpleFormat())) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Img);
    ASSERT_NE(img->diskFormat(), nullptr);

    // Alle drei Dateien tragen dieselben Nutzdaten.
    struct Fall { std::string pfad; std::optional<DiskFormat> fmt; };
    const Fall faelle[] = {
        {hfe,   std::nullopt},
        {dmk,   std::nullopt},
        {img_p, makeSimpleFormat()},
    };
    for (const auto& f : faelle) {
        auto back = DiskImage::open(f.pfad, f.fmt, false);
        ASSERT_NE(back, nullptr) << f.pfad;
        auto s0 = TrackCodec::parseTrack(back->readTrack(0, 0));
        auto s1 = TrackCodec::parseTrack(back->readTrack(1, 0));
        ASSERT_EQ(s0.size(), 2u) << f.pfad;
        ASSERT_EQ(s1.size(), 2u) << f.pfad;
        EXPECT_EQ(s0[0].data[0], 0xAA) << f.pfad;
        EXPECT_EQ(s1[1].data[5], 0xBB) << f.pfad;
    }

    for (const auto& p : {hfe, dmk, img_p}) std::filesystem::remove(p);
}

// ─── Autosave ────────────────────────────────────────────────────────────────

/**
 * @test DiskImageAutoFlush/SchreibtErstNachDerRuhezeit
 * @brief Der Autosave fasst Schreibbursts zusammen: erst wenn die letzte Änderung
 *        @ref kAutoFlushDelayCycles Takte zurückliegt, geht sie in die Datei.
 */
TEST(DiskImageAutoFlush, SchreibtErstNachDerRuhezeit) {
    const auto path = tmpPath("k1520_test_di_autoflush.hfe");
    std::filesystem::remove(path);

    auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);
    img->writeTrack(0, 0, makeTrack(0, 2, 128, 0x11));
    ASSERT_TRUE(img->saveAs(path, std::nullopt)) << img->lastError();

    // Änderung, die noch nicht geschrieben sein darf.
    img->writeTrack(1, 0, makeTrack(1, 2, 128, 0x77));
    EXPECT_FALSE(img->autoFlush(1000)) << "erste Prüfung merkt sich nur den Zeitpunkt";
    EXPECT_FALSE(img->autoFlush(1000 + kAutoFlushDelayCycles / 2));
    {
        auto zwischen = DiskImage::open(path, std::nullopt, false);
        ASSERT_NE(zwischen, nullptr);
        EXPECT_TRUE(zwischen->readTrack(1, 0).empty())
            << "Änderung zu früh in der Datei";
    }

    // Nach Ablauf der Ruhezeit wird geschrieben.
    EXPECT_TRUE(img->autoFlush(1000 + kAutoFlushDelayCycles + 1));
    EXPECT_FALSE(img->medium().dirty());
    {
        auto danach = DiskImage::open(path, std::nullopt, false);
        ASSERT_NE(danach, nullptr);
        auto secs = TrackCodec::parseTrack(danach->readTrack(1, 0));
        ASSERT_EQ(secs.size(), 2u);
        EXPECT_EQ(secs[0].data[0], 0x77);
    }

    // Ohne weitere Änderung passiert nichts mehr.
    EXPECT_FALSE(img->autoFlush(1000 + 10 * kAutoFlushDelayCycles));

    std::filesystem::remove(path);
}

/**
 * @test DiskImageAutoFlush/DestruktorSchreibtAusstehendeAenderungen
 */
TEST(DiskImageAutoFlush, DestruktorSchreibtAusstehendeAenderungen) {
    const auto path = tmpPath("k1520_test_di_dtor.dmk");
    std::filesystem::remove(path);

    {
        auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
        ASSERT_NE(img, nullptr);
        img->writeTrack(0, 0, makeTrack(0, 2, 128, 0x33));
        ASSERT_TRUE(img->saveAs(path, std::nullopt)) << img->lastError();
        img->writeTrack(1, 0, makeTrack(1, 2, 128, 0x44));
        // KEIN flush() — der Destruktor muss es erledigen.
    }

    auto back = DiskImage::open(path, std::nullopt, false);
    ASSERT_NE(back, nullptr);
    auto secs = TrackCodec::parseTrack(back->readTrack(1, 0));
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[0].data[0], 0x44);

    std::filesystem::remove(path);
}

/**
 * @test DiskImageAutoFlush/SchreibgeschuetzteDisketteBleibtUnberuehrt
 */
TEST(DiskImageAutoFlush, SchreibgeschuetzteDisketteBleibtUnberuehrt) {
    const auto path = tmpPath("k1520_test_di_wp.hfe");
    std::filesystem::remove(path);

    auto img = DiskImage::createBlank(2, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);
    img->writeTrack(0, 0, makeTrack(0, 2, 128, 0x55));
    ASSERT_TRUE(img->saveAs(path, std::nullopt)) << img->lastError();

    img->setWriteProtect(true);
    EXPECT_FALSE(img->writeTrack(1, 0, makeTrack(1, 2, 128, 0x66)));
    EXPECT_FALSE(img->medium().dirty());

    auto back = DiskImage::open(path, std::nullopt, false);
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->readTrack(1, 0).empty());

    std::filesystem::remove(path);
}
