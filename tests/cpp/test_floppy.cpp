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

TEST(FloppyDrive, MountValidImage) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    EXPECT_TRUE(drv.mount(path, fmt));
    EXPECT_TRUE(drv.isMounted());
    EXPECT_FALSE(drv.isWriteProtect());

    std::filesystem::remove(path);
}

TEST(FloppyDrive, MountNonexistent_Fails) {
    FloppyDrive drv;
    auto fmt = getCPA780();
    EXPECT_FALSE(drv.mount("/nonexistent/disk.img", fmt));
    EXPECT_FALSE(drv.isMounted());
}

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

TEST(FloppyDrive, ReadDataSector_CPA780) {
    auto fmt  = getCPA780();
    auto path = makeTmpImage(fmt);

    FloppyDrive drv;
    ASSERT_TRUE(drv.mount(path, fmt));

    auto data = drv.readSector(10, 0, 3);
    EXPECT_EQ(data.size(), 1024u);

    std::filesystem::remove(path);
}

TEST(FloppyDrive, ReadSector_NotMounted_Empty) {
    FloppyDrive drv;
    auto data = drv.readSector(0, 0, 1);
    EXPECT_TRUE(data.empty());
}

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
