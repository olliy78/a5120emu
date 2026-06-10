/**
 * @file test_hfe_image.cpp
 * @brief GoogleTests für HfeImage (HFE-v1-Backend) und DiskImage::open-Fabrik.
 *
 * Getestete Komponenten:
 *   1. Cross-Check: HfeImage::readTrack → parseTrack liefert dieselben Sektoren
 *      mit id_crc_ok && data_crc_ok wie RawSectorImage auf derselben Fixture.
 *   2. geometry(): num_cyls=2, num_heads=2, encoding=MFM.
 *   3. Write-Roundtrip: Byte ändern → writeTrack → flush → neu öffnen → parseTrack.
 *   4. Header + LUT unverändert nach Write.
 *   5. DiskImage::open-Fabrik liefert ein gültiges HfeImage für cpa_mini.hfe.
 *   6. FloppyDriveV2::mount mit HFE-Image (optional).
 *
 * Fixture-Pfad: FIXTURE_DIR (CMake-Define) / cpa_mini.hfe + cpa_mini.img
 * Geometrie: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B (MFM, 250 kbit/s, 300 U/min)
 *
 * @see core/peripherals/floppy_drive/hfe_image.h
 * @see tools/img_to_hfe.py  (erzeugt cpa_mini.hfe aus cpa_mini.img)
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
#include <cstdint>

#include "core/peripherals/floppy_drive/hfe_image.h"
#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/format_parser.h"

// ─── Pfade zu den Fixtures ───────────────────────────────────────────────────

#ifndef FIXTURE_DIR
#  define FIXTURE_DIR "tests/fixtures"
#endif

static const std::string kHfePath  = std::string(FIXTURE_DIR) + "/cpa_mini.hfe";
static const std::string kImgPath  = std::string(FIXTURE_DIR) + "/cpa_mini.img";

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/**
 * DiskFormat für die Mini-Fixture: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B.
 * Uniform (ein TrackFormat-Eintrag deckt alle Spuren ab).
 */
static DiskFormat makeMiniFormat() {
    DiskFormat fmt;
    fmt.name = "cpa_mini";
    // Uniform: cyl 0..1, head 0..1, 4 Sektoren × 128 B
    fmt.tracks.push_back({0, 1, 0, 1, 4, 128});
    return fmt;
}

// ─── Test 1: Cross-Check HFE ↔ Raw (Kern-Test) ───────────────────────────────

/**
 * Für jede (cyl, head) müssen HfeImage::readTrack→parseTrack und
 * RawSectorImage::readTrack→parseTrack identische Sektoren liefern:
 *   - gleiche Anzahl Sektoren
 *   - id_crc_ok && data_crc_ok für jeden Sektor
 *   - gleiche Sektordaten (bitgleich)
 */
TEST(HfeImage, CrossCheck_HFE_vs_Raw) {
    const auto fmt = makeMiniFormat();

    HfeImage       hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen()) << "HFE-Datei konnte nicht geöffnet werden: " << kHfePath;

    RawSectorImage raw(kImgPath, fmt, /*wp=*/false);
    ASSERT_TRUE(raw.isOpen()) << "IMG-Datei konnte nicht geöffnet werden: " << kImgPath;

    for (uint8_t cyl = 0; cyl < 2; ++cyl) {
        for (uint8_t head = 0; head < 2; ++head) {
            // ── HFE ──────────────────────────────────────────────────────────
            TrackImage hfe_track = hfe.readTrack(cyl, head);
            ASSERT_FALSE(hfe_track.empty())
                << "HFE readTrack(" << (int)cyl << "," << (int)head << ") leer";

            auto hfe_secs = TrackCodec::parseTrack(hfe_track);
            ASSERT_EQ(hfe_secs.size(), 4u)
                << "HFE Sektoranzahl cyl=" << (int)cyl << " head=" << (int)head;

            // ── Raw ───────────────────────────────────────────────────────────
            TrackImage raw_track = raw.readTrack(cyl, head);
            ASSERT_FALSE(raw_track.empty())
                << "Raw readTrack(" << (int)cyl << "," << (int)head << ") leer";

            auto raw_secs = TrackCodec::parseTrack(raw_track);
            ASSERT_EQ(raw_secs.size(), 4u)
                << "Raw Sektoranzahl cyl=" << (int)cyl << " head=" << (int)head;

            // ── Vergleich ─────────────────────────────────────────────────────
            for (size_t s = 0; s < 4; ++s) {
                EXPECT_TRUE(hfe_secs[s].id_crc_ok)
                    << "HFE ID-CRC fehler cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;
                EXPECT_TRUE(hfe_secs[s].data_crc_ok)
                    << "HFE Daten-CRC fehler cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;

                EXPECT_EQ(hfe_secs[s].id,   raw_secs[s].id)
                    << "Sektor-ID weicht ab cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;
                EXPECT_EQ(hfe_secs[s].size, raw_secs[s].size)
                    << "Sektorgröße weicht ab";
                EXPECT_EQ(hfe_secs[s].data, raw_secs[s].data)
                    << "Nutzdaten weichen ab cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)hfe_secs[s].id;
            }
        }
    }
}

// ─── Test 2: geometry() ──────────────────────────────────────────────────────

TEST(HfeImage, Geometry_KorrekteFelderMiniFixture) {
    HfeImage hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen());

    DiskGeometry g = hfe.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);
    EXPECT_TRUE(g.uniform);
}

// ─── Test 3: Write-Roundtrip ─────────────────────────────────────────────────

/**
 * Schreibt cpa_mini.hfe in /tmp, modifiziert secs[0].data[0] auf cyl=0,head=0,
 * schreibt zurück via writeTrack→flush, öffnet neu und verifiziert:
 *   - geändertes Byte sichtbar in parseTrack
 *   - andere Sektoren derselben Spur unverändert
 *   - Spur (0,1) unverändert
 */
TEST(HfeImage, WriteRoundtrip_ByteAendernUndVerifizieren) {
    // Temporäre Kopie anlegen
    const auto tmp_path = (std::filesystem::temp_directory_path()
                           / "cpa_mini_write_test.hfe").string();
    std::filesystem::copy_file(kHfePath, tmp_path,
                               std::filesystem::copy_options::overwrite_existing);

    // Ursprünglichen Wert merken
    uint8_t original_byte = 0;
    uint8_t modified_byte = 0;
    {
        HfeImage hfe_r(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_r.isOpen());
        TrackImage t = hfe_r.readTrack(0, 0);
        auto secs = TrackCodec::parseTrack(t);
        ASSERT_GE(secs.size(), 1u);
        ASSERT_FALSE(secs[0].data.empty());
        original_byte = secs[0].data[0];
        modified_byte = static_cast<uint8_t>(original_byte ^ 0xFF);
    }

    // Schreiben: secs[0].data[0] ändern
    {
        HfeImage hfe_w(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_w.isOpen());
        ASSERT_TRUE(hfe_w.writable());

        TrackImage t = hfe_w.readTrack(0, 0);
        uint32_t orig_bitcells = t.bitcells;
        auto secs = TrackCodec::parseTrack(t);
        ASSERT_GE(secs.size(), 1u);
        secs[0].data[0] = modified_byte;

        TrackImage t_new = TrackCodec::buildTrack(secs, Encoding::MFM);
        t_new.bitcells = orig_bitcells;   // ursprüngliche Bitzellenzahl erhalten

        ASSERT_TRUE(hfe_w.writeTrack(0, 0, t_new));
        ASSERT_TRUE(hfe_w.flush());
    }

    // Neu öffnen und verifizieren
    {
        HfeImage hfe_v(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_v.isOpen());

        // Geänderter Sektor (cyl=0, head=0)
        TrackImage t00 = hfe_v.readTrack(0, 0);
        auto secs00 = TrackCodec::parseTrack(t00);
        ASSERT_GE(secs00.size(), 1u);
        EXPECT_EQ(secs00[0].data[0], modified_byte)
            << "Geändertes Byte nicht gespeichert";

        // Andere Sektoren derselben Spur unverändert
        for (size_t i = 1; i < secs00.size(); ++i) {
            EXPECT_TRUE(secs00[i].id_crc_ok)   << "id_crc_ok Sektor " << i;
            EXPECT_TRUE(secs00[i].data_crc_ok) << "data_crc_ok Sektor " << i;
        }

        // Spur (0, 1) darf nicht verändert worden sein
        HfeImage hfe_ref(kHfePath, /*wp=*/false);
        ASSERT_TRUE(hfe_ref.isOpen());

        TrackImage t01_mod = hfe_v.readTrack(0, 1);
        TrackImage t01_ref = hfe_ref.readTrack(0, 1);
        auto secs01_mod = TrackCodec::parseTrack(t01_mod);
        auto secs01_ref = TrackCodec::parseTrack(t01_ref);
        ASSERT_EQ(secs01_mod.size(), secs01_ref.size());
        for (size_t i = 0; i < secs01_mod.size(); ++i) {
            EXPECT_EQ(secs01_mod[i].data, secs01_ref[i].data)
                << "Spur (0,1) Sektor " << i << " verändert";
        }
    }

    std::filesystem::remove(tmp_path);
}

// ─── Test 4: Header + LUT nach Write unverändert ─────────────────────────────

TEST(HfeImage, HeaderLUT_NachWrite_Unveraendert) {
    const auto tmp_path = (std::filesystem::temp_directory_path()
                           / "cpa_mini_header_test.hfe").string();
    std::filesystem::copy_file(kHfePath, tmp_path,
                               std::filesystem::copy_options::overwrite_existing);

    // Original-Header+LUT lesen (erste 2×512 = 1024 Bytes)
    std::vector<uint8_t> hdr_before(1024);
    {
        std::ifstream f(tmp_path, std::ios::binary);
        f.read(reinterpret_cast<char*>(hdr_before.data()), 1024);
    }

    // Schreiben
    {
        HfeImage hfe(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe.isOpen());
        TrackImage t = hfe.readTrack(0, 0);
        // Minimale Änderung: einfach dieselbe Spur zurückschreiben
        ASSERT_TRUE(hfe.writeTrack(0, 0, t));
        ASSERT_TRUE(hfe.flush());
    }

    // Header+LUT nach Write vergleichen
    std::vector<uint8_t> hdr_after(1024);
    {
        std::ifstream f(tmp_path, std::ios::binary);
        f.read(reinterpret_cast<char*>(hdr_after.data()), 1024);
    }

    EXPECT_EQ(hdr_before, hdr_after)
        << "Header oder LUT nach writeTrack/flush verändert";

    std::filesystem::remove(tmp_path);
}

// ─── Test 5: DiskImage::open-Fabrik ─────────────────────────────────────────

TEST(HfeImage, DiskImageOpen_HFE_LiefertGueltigesImage) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr) << "DiskImage::open lieferte nullptr für " << kHfePath;

    DiskGeometry g = img->geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);

    // readTrack muss für cyl=0,head=0 eine gültige Spur liefern
    TrackImage t = img->readTrack(0, 0);
    ASSERT_FALSE(t.empty());
    auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 4u);
    EXPECT_TRUE(secs[0].id_crc_ok);
    EXPECT_TRUE(secs[0].data_crc_ok);
}

// ─── Test 6: FloppyDriveV2::mount mit HFE-Image ──────────────────────────────

TEST(HfeImage, FloppyDriveV2_Mount_HFE_ErfolgreicherMount) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr);

    // mfs_525_ds80: 80 Zylindern × 2 Köpfe, MFM
    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(std::move(img)))
        << "FloppyDriveV2::mount fehlgeschlagen: " << drv.lastError();
    ASSERT_TRUE(drv.isMounted());

    // Spur lesen
    const TrackImage& t = drv.track(0);
    EXPECT_FALSE(t.empty());
}

// ─── Test 7: Schreibschutz-Flag ───────────────────────────────────────────────

TEST(HfeImage, WriteProtect_VerhindertSchreiben) {
    HfeImage hfe(kHfePath, /*wp=*/true);
    ASSERT_TRUE(hfe.isOpen());
    EXPECT_FALSE(hfe.writable());

    TrackImage t = hfe.readTrack(0, 0);
    EXPECT_FALSE(hfe.writeTrack(0, 0, t))
        << "writeTrack sollte bei Schreibschutz false liefern";
}

// ─── Test 8: Ungültige Spur → leeres TrackImage ───────────────────────────────

TEST(HfeImage, ReadTrack_UngueltigerZylinder_Leer) {
    HfeImage hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen());

    TrackImage t = hfe.readTrack(99, 0);
    EXPECT_TRUE(t.empty());
}

// ─── Test 9: HXCHFEV3-Signatur → nullptr (kein Rückschritt) ─────────────────

TEST(DiskImageOpen, HXCHFEV3_Signatur_gibtNullptr) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hfe_hxchfev3.img").string();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write("HXCHFEV3", 8);
        uint8_t pad[64] = {};
        f.write(reinterpret_cast<char*>(pad), sizeof(pad));
    }
    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}
