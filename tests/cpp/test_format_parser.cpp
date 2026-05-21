/**
 * @file test_format_parser.cpp
 * @brief Unit tests for the disk format parser (FormatParser).
 *
 * @details
 * Emulator component under test: **FormatParser** (`core/peripherals/floppy_drive/format_parser.h`)
 *
 * FormatParser loads disk geometry descriptions (DiskFormat, TrackFormat) from either
 * built-in compiled-in data or external configuration files.  The A5120 uses two
 * standard formats:
 *  - **CPA780** – 80-track, two-sided disk with a special 128-byte boot track
 *    (26 sectors × 128 B on cyl 0/side 0) and 1024-byte data tracks (5 sectors × 1024 B).
 *  - **CPA800** – 80-track, two-sided disk with uniform 1024-byte tracks throughout.
 *
 * ## Test groups
 *
 * | Group                | What is tested                                             |
 * |----------------------|------------------------------------------------------------|
 * | Builtin formats      | Existence and content of CPA780 and CPA800                 |
 * | Geometry             | Sector count, bytes per sector for boot and data tracks    |
 * | totalBytes()         | Calculated image size matches physical disk capacity       |
 * | File parser          | Parsing .cfg syntax, multiple formats, error handling      |
 *
 * @see core/peripherals/floppy_drive/format_parser.h
 */

#include <gtest/gtest.h>
#include <algorithm>
#include "core/peripherals/floppy_drive/format_parser.h"
#include <fstream>
#include <filesystem>

/**
 * @test FormatParser/BuiltinFormats_NotEmpty
 * @brief builtinFormats() returns a non-empty list.
 * @par Pass criterion  builtinFormats().empty() == false.
 */
TEST(FormatParser, BuiltinFormats_NotEmpty) {
    auto fmts = FormatParser::builtinFormats();
    EXPECT_FALSE(fmts.empty());
}

/**
 * @test FormatParser/Builtin_CPA780_Exists
 * @brief The built-in CPA780 format is present and has exactly two track format entries.
 * @details Two entries: one for the boot track (cyl 0) and one for data tracks.
 * @par Pass criterion  Format named "cpa780" found; tracks.size() == 2.
 */
TEST(FormatParser, Builtin_CPA780_Exists) {
    auto fmts = FormatParser::builtinFormats();
    auto it = std::find_if(fmts.begin(), fmts.end(),
                           [](const DiskFormat& f){ return f.name == "cpa780"; });
    ASSERT_NE(it, fmts.end());
    EXPECT_EQ(it->tracks.size(), 2u);
}

/**
 * @test FormatParser/CPA780_BootTrackGeometry
 * @brief The CPA780 boot track (cyl 0, side 0) has 26 sectors of 128 bytes each.
 * @par Pass criterion  secs_per_track == 26; bytes_per_sec == 128.
 */
TEST(FormatParser, CPA780_BootTrackGeometry) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa780 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa780"; });

    const TrackFormat* boot = cpa780.findTrack(0, 0);  // cyl 0, head 0
    ASSERT_NE(boot, nullptr);
    EXPECT_EQ(boot->secs_per_track, 26);
    EXPECT_EQ(boot->bytes_per_sec,  128);
}

/**
 * @test FormatParser/CPA780_DataTrackGeometry
 * @brief CPA780 data tracks (cyl 10, side 1) have 5 sectors of 1024 bytes each.
 * @par Pass criterion  secs_per_track == 5; bytes_per_sec == 1024.
 */
TEST(FormatParser, CPA780_DataTrackGeometry) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa780 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa780"; });

    const TrackFormat* data = cpa780.findTrack(10, 1);  // cyl 10, head 1
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->secs_per_track, 5);
    EXPECT_EQ(data->bytes_per_sec,  1024);
}

/**
 * @test FormatParser/CPA800_UniformGeometry
 * @brief CPA800 uses a single uniform track format (5 sectors × 1024 bytes) for all tracks.
 * @par Pass criterion  tracks.size() == 1; secs_per_track == 5; bytes_per_sec == 1024.
 */
TEST(FormatParser, CPA800_UniformGeometry) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa800 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa800"; });
    EXPECT_EQ(cpa800.tracks.size(), 1u);
    const TrackFormat* t = cpa800.findTrack(0, 0);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->secs_per_track, 5);
    EXPECT_EQ(t->bytes_per_sec,  1024);
}

/**
 * @test FormatParser/TotalBytes_CPA800
 * @brief totalBytes() returns the correct total image size for CPA800.
 * @details 80 cylinders × 2 sides × 5 sectors × 1024 bytes = 819 200 bytes.
 * @par Pass criterion  totalBytes() == 819200.
 */
TEST(FormatParser, TotalBytes_CPA800) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa800 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa800"; });
    // 80 cyls × 2 heads × 5 sectors × 1024 B = 819200
    EXPECT_EQ(cpa800.totalBytes(), 819200u);
}

/**
 * @test FormatParser/ParseFile_BasicSyntax
 * @brief A minimal configuration file with one format and two track definitions is parsed correctly.
 * @details Writes a temporary .cfg file, parses it, and checks the resulting DiskFormat.
 * @par Pass criterion  One format with two TrackFormat entries; bytes_per_sec values match.
 */
TEST(FormatParser, ParseFile_BasicSyntax) {
    // Write a minimal config file
    auto tmp = std::filesystem::temp_directory_path() / "test_fmt.cfg";
    {
        std::ofstream f(tmp);
        f << "# comment\n";
        f << "[testfmt]\n";
        f << "track 0 2 0 1 26 128\n";
        f << "track 3 79 0 1 5 1024\n";
    }

    auto fmts = FormatParser::parseFile(tmp.string());
    ASSERT_EQ(fmts.size(), 1u);
    EXPECT_EQ(fmts[0].name, "testfmt");
    EXPECT_EQ(fmts[0].tracks.size(), 2u);
    EXPECT_EQ(fmts[0].tracks[0].bytes_per_sec, 128);
    EXPECT_EQ(fmts[0].tracks[1].bytes_per_sec, 1024);

    std::filesystem::remove(tmp);
}

/**
 * @test FormatParser/ParseFile_MultipleFormats
 * @brief Multiple format sections in one file are all parsed and returned in order.
 * @par Pass criterion  fmts.size() == 2; names are "fmt_a" and "fmt_b".
 */
TEST(FormatParser, ParseFile_MultipleFormats) {
    auto tmp = std::filesystem::temp_directory_path() / "test_fmt2.cfg";
    {
        std::ofstream f(tmp);
        f << "[fmt_a]\ntrack 0 79 0 0 16 256\n";
        f << "[fmt_b]\ntrack 0 77 0 0 16 256\n";
    }

    auto fmts = FormatParser::parseFile(tmp.string());
    EXPECT_EQ(fmts.size(), 2u);
    EXPECT_EQ(fmts[0].name, "fmt_a");
    EXPECT_EQ(fmts[1].name, "fmt_b");
    std::filesystem::remove(tmp);
}

/**
 * @test FormatParser/ParseFile_NonExistent_Throws
 * @brief Attempting to parse a non-existent file throws std::runtime_error.
 * @par Pass criterion  std::runtime_error is thrown.
 */
TEST(FormatParser, ParseFile_NonExistent_Throws) {
    EXPECT_THROW(FormatParser::parseFile("/nonexistent/file.cfg"), std::runtime_error);
}
