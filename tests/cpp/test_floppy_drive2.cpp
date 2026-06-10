/**
 * @file test_floppy_drive2.cpp
 * @brief GoogleTests für FloppyDriveV2 (DiskImage + DriveProfile + Track-Cache).
 *
 * Getestete Komponenten:
 *   - FloppyDriveV2::mount (Erfolg, Kompatibilitätsprüfungen)
 *   - FloppyDriveV2::track (Spur laden, nichtleer, Sektoren korrekt)
 *   - FloppyDriveV2::step  (Begrenzung 0 .. num_cyls-1)
 *   - Schreib-Roundtrip via mutableTrack + markTrackDirty + flush + readTrack
 *
 * @see core/peripherals/floppy_drive/floppy_drive2.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdint>

#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/peripherals/floppy_drive/drive_profile.h"

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/** Einfaches Format: 2 Zylinder, 1 Kopf, 2 Sektoren × 128 B. */
static DiskFormat makeFormat_2cyl_1head_2sec() {
    DiskFormat fmt;
    fmt.name = "test_2c1h2s";
    fmt.tracks.push_back({0, 1, 0, 0, 2, 128});
    return fmt;
}

/** Temporäre .img-Datei; alle Bytes mit Sektor-ID-Füller. */
static std::string makeTmpImg(const DiskFormat& fmt, const std::string& suffix = "") {
    const auto path = (std::filesystem::temp_directory_path()
                       / ("k1520_drv2_" + fmt.name + suffix + ".img")).string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                std::vector<uint8_t> buf(tf->bytes_per_sec, static_cast<uint8_t>(id));
                f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
        }
    }
    return path;
}

/** Öffnet ein RawSectorImage über die DiskImage-Fabrik. */
static std::unique_ptr<DiskImage> openImg(const std::string& path,
                                          const DiskFormat& fmt,
                                          bool wp = false) {
    return DiskImage::open(path, fmt, wp);
}

// ─── Gruppe 1: Mount ──────────────────────────────────────────────────────────

TEST(FloppyDriveV2, Mount_Erfolg) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    auto img = openImg(path, fmt);
    ASSERT_NE(img, nullptr);

    EXPECT_TRUE(drv.mount(std::move(img)));
    EXPECT_TRUE(drv.isMounted());
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Mount_KeinImage_Fehler) {
    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    EXPECT_FALSE(drv.mount(nullptr));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");
}

TEST(FloppyDriveV2, Mount_ZuVieleSpuren_Inkompatibel) {
    // ss_525_40 hat nur 40 Zylinder; ein 80-Zylinderformat ist zu groß.
    auto fmt  = makeFormat_2cyl_1head_2sec();
    // Neues Format mit 80 Zylindern bauen
    DiskFormat fmt80;
    fmt80.name = "test_80cyl";
    fmt80.tracks.push_back({0, 79, 0, 0, 2, 128});

    auto path = (std::filesystem::temp_directory_path()
                 / "k1520_drv2_80cyl.img").string();
    {
        uint64_t sz = fmt80.totalBytes();
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> buf(sz, 0xE5);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    FloppyDriveV2 drv(builtinDriveProfile("ss_525_40")); // nur 40 Zylinder
    auto img = openImg(path, fmt80);
    ASSERT_NE(img, nullptr);

    EXPECT_FALSE(drv.mount(std::move(img)));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Mount_FalschesVerfahren_Inkompatibel) {
    // mf3200_8_ss77 unterstützt nur FM; ein MFM-Image passt nicht.
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mf3200_8_ss77")); // FM only
    // RawSectorImage mit MFM (Default-Encoding) anlegen
    auto img = std::make_unique<RawSectorImage>(path, fmt, false, Encoding::MFM);
    ASSERT_TRUE(img->isOpen());

    EXPECT_FALSE(drv.mount(std::move(img)));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");

    std::filesystem::remove(path);
}

// ─── Gruppe 2: track() liefert korrekte Sektoren ─────────────────────────────

TEST(FloppyDriveV2, Track_NichtLeerNachMount) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    const TrackImage& spur = drv.track(0);
    EXPECT_FALSE(spur.empty()) << "Spur sollte Bytes enthalten";

    // parseTrack muss 2 Sektoren liefern
    auto parsed = TrackCodec::parseTrack(spur);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_TRUE(parsed[0].id_crc_ok);
    EXPECT_TRUE(parsed[0].data_crc_ok);
    EXPECT_TRUE(parsed[1].id_crc_ok);
    EXPECT_TRUE(parsed[1].data_crc_ok);

    // Erkennungsmuster: Sektor id=1 → alle Bytes 0x01
    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [](uint8_t b){ return b == 0x01; }));

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Track_UngueltigerKopf_LeereSpur) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    // Kopf 2 existiert nicht (nur Kopf 0 im Format), track() muss leer sein
    const TrackImage& spur = drv.track(2);
    EXPECT_TRUE(spur.empty());

    std::filesystem::remove(path);
}

// ─── Gruppe 3: step / seek ────────────────────────────────────────────────────

TEST(FloppyDriveV2, Step_InwardIncrement) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.currentCylinder(), 0u);
    drv.step(true);   // inward
    EXPECT_EQ(drv.currentCylinder(), 1u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_InwardBegrenztAufMaxCyl) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80")); // num_cyls=80
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    // Bis ans Ende fahren
    drv.seek(79);
    EXPECT_EQ(drv.currentCylinder(), 79u);

    // Weiterer Schritt inward darf 79 nicht überschreiten
    drv.step(true);
    EXPECT_EQ(drv.currentCylinder(), 79u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_OutwardBegrenztAuf0) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.currentCylinder(), 0u);
    drv.step(false);   // outward, bereits bei 0
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_InOutRoundtrip) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    drv.step(true);    // 0 → 1
    drv.step(true);    // 1 → 2
    EXPECT_EQ(drv.currentCylinder(), 2u);
    drv.step(false);   // 2 → 1
    EXPECT_EQ(drv.currentCylinder(), 1u);
    drv.step(false);   // 1 → 0
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

// ─── Gruppe 4: Schreib-Roundtrip ─────────────────────────────────────────────

/**
 * Spur modifizieren via mutableTrack + markTrackDirty + flush, dann per
 * neuem RawSectorImage nachlesen — Änderung muss persistieren.
 */
TEST(FloppyDriveV2, SchreibRoundtrip_AenderungPersistiert) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt, "_rw");

    // Neue Spur mit abgeänderten Sektordaten bauen
    const uint8_t geaendert = 0xAB;
    std::vector<LogicalSector> sektoren;
    for (uint8_t id = 1; id <= 2; ++id) {
        LogicalSector ls;
        ls.cyl  = 0;
        ls.head = 0;
        ls.id   = id;
        ls.size = 128;
        ls.data.assign(128, (id == 1) ? geaendert : static_cast<uint8_t>(id));
        sektoren.push_back(std::move(ls));
    }
    TrackImage neueSpur = TrackCodec::buildTrack(sektoren, Encoding::MFM);

    // Laufwerk mounten, Spur ersetzen und zurückschreiben.
    {
        FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
        ASSERT_TRUE(drv.mount(openImg(path, fmt)));

        drv.mutableTrack(0) = neueSpur;
        drv.markTrackDirty(0);
        EXPECT_TRUE(drv.flush());
    }

    // Neues Backend öffnen und nachlesen.
    RawSectorImage check(path, fmt, /*wp=*/false);
    ASSERT_TRUE(check.isOpen());

    TrackImage gelesen = check.readTrack(0, 0);
    ASSERT_FALSE(gelesen.empty());

    auto parsed = TrackCodec::parseTrack(gelesen);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [geaendert](uint8_t b){ return b == geaendert; }))
        << "Sektor 1 sollte alle Bytes " << (int)geaendert << " enthalten";

    std::filesystem::remove(path);
}

// ─── Gruppe 5: geometry() ────────────────────────────────────────────────────

TEST(FloppyDriveV2, Geometry_NachMount) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    DiskGeometry g = drv.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 1u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Geometry_OhneMount_Leer) {
    FloppyDriveV2 drv(builtinDriveProfile("mfs_525_ds80"));
    DiskGeometry g = drv.geometry();
    EXPECT_EQ(g.num_cyls,  0u);
    EXPECT_EQ(g.num_heads, 0u);
}
