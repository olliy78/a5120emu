#include <gtest/gtest.h>
#include <algorithm>
#include "core/peripherals/floppy_drive/format_parser.h"
#include <fstream>
#include <filesystem>

TEST(FormatParser, BuiltinFormats_NotEmpty) {
    auto fmts = FormatParser::builtinFormats();
    EXPECT_FALSE(fmts.empty());
}

TEST(FormatParser, Builtin_CPA780_Exists) {
    auto fmts = FormatParser::builtinFormats();
    auto it = std::find_if(fmts.begin(), fmts.end(),
                           [](const DiskFormat& f){ return f.name == "cpa780"; });
    ASSERT_NE(it, fmts.end());
    EXPECT_EQ(it->tracks.size(), 2u);
}

TEST(FormatParser, CPA780_BootTrackGeometry) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa780 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa780"; });

    const TrackFormat* boot = cpa780.findTrack(0, 0);  // cyl 0, head 0
    ASSERT_NE(boot, nullptr);
    EXPECT_EQ(boot->secs_per_track, 26);
    EXPECT_EQ(boot->bytes_per_sec,  128);
}

TEST(FormatParser, CPA780_DataTrackGeometry) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa780 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa780"; });

    const TrackFormat* data = cpa780.findTrack(10, 1);  // cyl 10, head 1
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->secs_per_track, 5);
    EXPECT_EQ(data->bytes_per_sec,  1024);
}

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

TEST(FormatParser, TotalBytes_CPA800) {
    auto fmts = FormatParser::builtinFormats();
    auto& cpa800 = *std::find_if(fmts.begin(), fmts.end(),
                                  [](const DiskFormat& f){ return f.name == "cpa800"; });
    // 80 cyls × 2 heads × 5 sectors × 1024 B = 819200
    EXPECT_EQ(cpa800.totalBytes(), 819200u);
}

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

TEST(FormatParser, ParseFile_NonExistent_Throws) {
    EXPECT_THROW(FormatParser::parseFile("/nonexistent/file.cfg"), std::runtime_error);
}
