/**
 * @file test_floppy.cpp
 * @brief Unit tests for the FloppyDrive peripheral emulation.
 *
 * @details
 * Emulator component under test: **FloppyDrive** (`core/peripherals/floppy_drive/floppy_drive.h`)
 *
 * FloppyDrive emulates a single 5.25" floppy disk drive as used in the Robotron A5120.
 * It models:
 *  - Disk image mounting and unmounting (with write-protect option)
 *  - Sector-based read/write using format-specific geometry
 *  - Cylinder seek (seekTrack) and step-pulse-based head positioning
 *  - An activity flag that signals ongoing disk access
 *
 * ## Test groups
 *
 * | Group             | What is tested                                              |
 * |-------------------|-------------------------------------------------------------|
 * | Mount / unmount   | Valid image, non-existent path, write-protect, unmount      |
 * | Read sector       | Boot sector, data sector, unmounted drive, invalid sector   |
 * | Write sector      | Write and read-back, write-protect blocking                 |
 * | Seek / step       | seekTrack(), step(inward/outward), currentCylinder()        |
 * | Activity flag     | Set after read, cleared by clearActive()                    |
 *
 * @see core/peripherals/floppy_drive/floppy_drive.h
 * @see core/peripherals/floppy_drive/format_parser.h
 */

#include <gtest/gtest.h>
#include <algorithm>
#include "core/peripherals/floppy_drive/floppy_drive.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include <filesystem>
#include <fstream>

static std::string makeTmpImage(const DiskFormat& fmt) {
    auto path = (std::filesystem::temp_directory_path() /
                 ("k1520_test_" + fmt.name + ".img")).string();
    uint64_t sz = fmt.totalBytes();
    std::ofstream f(path, std::ios::binary);
    std::vector<uint8_t> buf(sz, 0xE5);
    f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return path;
}

static DiskFormat getCPA780() {
    auto fmts = FormatParser::builtinFormats();
    return *std::find_if(fmts.begin(), fmts.end(),
                         [](const DiskFormat& f){ return f.name == "cpa780"; });
}

static DiskFormat getCPA800() {
    auto fmts = FormatParser::builtinFormats();
    return *std::find_if(fmts.begin(), fmts.end(),
                         [](const DiskFormat& f){ return f.name == "cpa800"; });
}

// ─── Mount / unmount ──────────────────────────────────────────────────────

/**
 * @test FloppyDrive/MountValidImage
 * @brief Mounting a valid disk image succeeds and marks the drive as mounted.
 * @details Creates a temporary CPA780 image file; calls mount().
 * @par Pass criterion  isMounted() == true; isWriteProtect() == false.
 */
TEST(FloppyDrive, MountValidImage) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    EXPECT_TRUE(drv.mount(path, fmt));
    EXPECT_TRUE(drv.isMounted());
    EXPECT_FALSE(drv.isWriteProtect());

    std::filesystem::remove(path);
}

/**
 * @test FloppyDrive/MountNonexistent_Fails
 * @brief Mounting a non-existent image file returns false and leaves the drive unmounted.
 * @par Pass criterion  mount() == false; isMounted() == false.
 */
TEST(FloppyDrive, MountNonexistent_Fails) {
    FloppyDrive drv;
    auto fmt = getCPA780();
    EXPECT_FALSE(drv.mount("/nonexistent/disk.img", fmt));
    EXPECT_FALSE(drv.isMounted());
}

/**
 * @test FloppyDrive/Unmount
 * @brief unmount() leaves the drive in an unmounted state.
 * @par Pass criterion  isMounted() == false after unmount().
 */
TEST(FloppyDrive, Unmount) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);
    FloppyDrive drv;
    drv.mount(path, fmt);
    drv.unmount();
    EXPECT_FALSE(drv.isMounted());
    std::filesystem::remove(path);
}

// ─── Read sector ─────────────────────────────────────────────────────────

/**
 * @test FloppyDrive/ReadBootSector_CPA780
 * @brief Reading track 0 / side 0 / sector 1 of a CPA780 image returns 128 bytes filled with 0xE5.
 * @details CPA780 boot track has 26 sectors of 128 bytes each; blank sectors are filled with 0xE5.
 * @par Pass criterion  data.size() == 128; data[0] == 0xE5.
 */
TEST(FloppyDrive, ReadBootSector_CPA780) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    ASSERT_TRUE(drv.mount(path, fmt));

    auto data = drv.readSector(0, 0, 1);
    EXPECT_EQ(data.size(), 128u);
    EXPECT_EQ(data[0], 0xE5);  // filled with 0xE5

    std::filesystem::remove(path);
}

/**
 * @test FloppyDrive/ReadDataSector_CPA780
 * @brief Reading a data sector (track 10, side 1, sector 3) returns 1024 bytes.
 * @details CPA780 data tracks have 5 sectors of 1024 bytes each.
 * @par Pass criterion  data.size() == 1024.
 */
TEST(FloppyDrive, ReadDataSector_CPA780) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    ASSERT_TRUE(drv.mount(path, fmt));

    auto data = drv.readSector(10, 0, 3);
    EXPECT_EQ(data.size(), 1024u);

    std::filesystem::remove(path);
}

/**
 * @test FloppyDrive/ReadSector_NotMounted_Empty
 * @brief Reading from an unmounted drive returns an empty vector.
 * @par Pass criterion  data.empty() == true.
 */
TEST(FloppyDrive, ReadSector_NotMounted_Empty) {
    FloppyDrive drv;
    auto data = drv.readSector(0, 0, 1);
    EXPECT_TRUE(data.empty());
}

/**
 * @test FloppyDrive/ReadSector_InvalidSectorId_Empty
 * @brief Requesting a sector number beyond the format's maximum returns an empty vector.
 * @details CPA780 boot track has max 26 sectors (IDs 1–26); sector ID 27 is out of range.
 * @par Pass criterion  data.empty() == true.
 */
TEST(FloppyDrive, ReadSector_InvalidSectorId_Empty) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);
    FloppyDrive drv;
    drv.mount(path, fmt);
    auto data = drv.readSector(0, 0, 27);  // max is 26
    EXPECT_TRUE(data.empty());
    std::filesystem::remove(path);
}

// ─── Write sector ─────────────────────────────────────────────────────────

/**
 * @test FloppyDrive/WriteAndReadBack
 * @brief Data written to a sector can be read back with identical content.
 * @details Writes 1024 bytes of 0xAA to track 5, side 1, sector 2 (CPA800 format).
 * @par Pass criterion  readback[0] == 0xAA; readback[511] == 0xAA; readback.size() == 1024.
 */
TEST(FloppyDrive, WriteAndReadBack) {
    auto fmt  = getCPA800();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    ASSERT_TRUE(drv.mount(path, fmt));

    std::vector<uint8_t> sector(1024, 0xAA);
    EXPECT_TRUE(drv.writeSector(5, 1, 2, sector));

    auto readback = drv.readSector(5, 1, 2);
    ASSERT_EQ(readback.size(), 1024u);
    EXPECT_EQ(readback[0], 0xAA);
    EXPECT_EQ(readback[511], 0xAA);

    std::filesystem::remove(path);
}

/**
 * @test FloppyDrive/WriteProtect_Blocks
 * @brief Writing to a write-protected drive returns false and leaves the disk unchanged.
 * @par Pass criterion  writeSector() == false on a drive mounted with write_protect=true.
 */
TEST(FloppyDrive, WriteProtect_Blocks) {
    auto fmt  = getCPA800();
    auto path = makeTmpImage(fmt);
    FloppyDrive drv;
    ASSERT_TRUE(drv.mount(path, fmt, true));  // write-protected

    std::vector<uint8_t> sector(1024, 0xBB);
    EXPECT_FALSE(drv.writeSector(0, 0, 1, sector));

    std::filesystem::remove(path);
}

// ─── Seek / step ──────────────────────────────────────────────────────────

/**
 * @test FloppyDrive/SeekAndStep
 * @brief seekTrack() sets the cylinder directly; step(inward) and step(outward) change it by ±1.
 * @par Pass criterion  currentCylinder() == 40 after seek; 41 after inward step; 40 after outward step.
 */
TEST(FloppyDrive, SeekAndStep) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);
    FloppyDrive drv;
    drv.mount(path, fmt);

    drv.seekTrack(40);
    EXPECT_EQ(drv.currentCylinder(), 40);

    drv.step(true);
    EXPECT_EQ(drv.currentCylinder(), 41);

    drv.step(false);
    EXPECT_EQ(drv.currentCylinder(), 40);

    std::filesystem::remove(path);
}

// ─── Activity flag ───────────────────────────────────────────────────────

/**
 * @test FloppyDrive/ActivitySetAfterRead
 * @brief The activity flag is set after a read operation and cleared by clearActive().
 * @par Pass criterion  isActive() == true after readSector(); false after clearActive().
 */
TEST(FloppyDrive, ActivitySetAfterRead) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);
    FloppyDrive drv;
    drv.mount(path, fmt);

    EXPECT_FALSE(drv.isActive());
    drv.readSector(0, 0, 1);
    EXPECT_TRUE(drv.isActive());
    drv.clearActive();
    EXPECT_FALSE(drv.isActive());

    std::filesystem::remove(path);
}
