// Unit-Tests für den boot_trace --coverage/--diff CSV-Parser + Run-Diff
// (tools/coverage_diff.h).
#include "tools/coverage_diff.h"
#include <gtest/gtest.h>
#include <sstream>

using covdiff::CovMap;

namespace {
CovMap parse(const std::string& csv){ CovMap m; std::istringstream s(csv); covdiff::parseCsv(s, m); return m; }
}

TEST(CoverageDiff, ParsesCpuPcHitsAndSkipsHeader){
    CovMap m = parse("cpu,pc,hits\nZVE1,0x0135,42\nZVE1,0x0140,1\nZVE2,0x01DD,7\n");
    ASSERT_EQ(m.count("ZVE1"), 1u);
    ASSERT_EQ(m.count("ZVE2"), 1u);
    EXPECT_EQ(m["ZVE1"][0x0135], 42);
    EXPECT_EQ(m["ZVE1"][0x0140], 1);
    EXPECT_EQ(m["ZVE2"][0x01DD], 7);
    EXPECT_EQ(m["ZVE1"].size(), 2u);
}

TEST(CoverageDiff, IgnoresBlankAndCrlf){
    CovMap m = parse("cpu,pc,hits\r\nZVE1,0x0100,5\r\n\nZVE1,0x0101,6\r\n");
    EXPECT_EQ(m["ZVE1"][0x0100], 5);
    EXPECT_EQ(m["ZVE1"][0x0101], 6);
    EXPECT_EQ(m["ZVE1"].size(), 2u);
}

TEST(CoverageDiff, DiffOnlyAOnlyBCommon){
    CovMap a = parse("cpu,pc,hits\nZVE1,0x0100,1\nZVE1,0x0200,1\nZVE1,0x0300,1\n");
    CovMap b = parse("cpu,pc,hits\nZVE1,0x0200,1\nZVE1,0x0300,1\nZVE1,0x0400,1\n");
    auto d = covdiff::diff(a, b);
    ASSERT_EQ(d.count("ZVE1"), 1u);
    const auto& z = d["ZVE1"];
    EXPECT_EQ(z.a_count, 3u);
    EXPECT_EQ(z.b_count, 3u);
    EXPECT_EQ(z.common, 2u);                          // 0x0200, 0x0300
    ASSERT_EQ(z.only_a.size(), 1u); EXPECT_EQ(z.only_a[0], 0x0100);
    ASSERT_EQ(z.only_b.size(), 1u); EXPECT_EQ(z.only_b[0], 0x0400);
    EXPECT_TRUE(z.changed.empty());                   // same hit counts on common
}

TEST(CoverageDiff, DiffDetectsHitCountChanges){
    CovMap a = parse("cpu,pc,hits\nZVE1,0x0100,1\nZVE1,0x0200,5\n");
    CovMap b = parse("cpu,pc,hits\nZVE1,0x0100,1\nZVE1,0x0200,9\n");
    auto z = covdiff::diff(a, b)["ZVE1"];
    EXPECT_EQ(z.common, 2u);
    EXPECT_TRUE(z.only_a.empty()); EXPECT_TRUE(z.only_b.empty());
    ASSERT_EQ(z.changed.size(), 1u); EXPECT_EQ(z.changed[0], 0x0200);   // 5 vs 9
}

TEST(CoverageDiff, DiffHandlesCpuPresentInOnlyOneRun){
    CovMap a = parse("cpu,pc,hits\nZVE1,0x0100,1\nZVE2,0x01DD,3\n");
    CovMap b = parse("cpu,pc,hits\nZVE1,0x0100,1\n");
    auto d = covdiff::diff(a, b);
    ASSERT_EQ(d.count("ZVE2"), 1u);
    EXPECT_EQ(d["ZVE2"].a_count, 1u);
    EXPECT_EQ(d["ZVE2"].b_count, 0u);
    ASSERT_EQ(d["ZVE2"].only_a.size(), 1u); EXPECT_EQ(d["ZVE2"].only_a[0], 0x01DD);
}
