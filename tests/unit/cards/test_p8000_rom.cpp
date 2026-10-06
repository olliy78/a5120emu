/**
 * @file test_p8000_rom.cpp
 * @brief Die erzeugten ROM-Header des P8000 stimmen mit den Binärabzügen in
 *        doc/p8000/eproms/ überein (doc/design/25_p8000.md §10.5, AP P5b).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/rom_mon8.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> abzug(const std::string& name) {
    std::ifstream f(std::string(P8000_ABZUG_DIR) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/// Feld = EPROM 1 (A12 = 0) gefolgt von EPROM 2 (A12 = 1)
template <size_t N>
void pruefe(const uint8_t (&feld)[N], const std::string& version) {
    const auto e1 = abzug("8BIT/MON8_1_" + version);
    const auto e2 = abzug("8BIT/MON8_2_" + version);
    ASSERT_EQ(e1.size(), 4096u) << version;
    ASSERT_EQ(e2.size(), 4096u) << version;
    ASSERT_EQ(N, e1.size() + e2.size()) << version;
    EXPECT_TRUE(std::equal(e1.begin(), e1.end(), feld)) << version << " EPROM 1";
    EXPECT_TRUE(std::equal(e2.begin(), e2.end(), feld + e1.size())) << version << " EPROM 2";
}

}  // namespace

TEST(P8000Rom, HeaderGleichAbzug) {
    pruefe(P8K_MON8_3_0, "3.0");
    pruefe(P8K_MON8_3_1, "3.1");
    pruefe(P8K_MON8_3_1_NUR8BIT, "3.1_NUR8BIT");
    pruefe(P8K_MON8_2_1_NUR8BIT, "2.1_NUR8BIT");
    // die beiden Fassungen unterscheiden sich wirklich
    EXPECT_NE(0, std::memcmp(P8K_MON8_3_0, P8K_MON8_3_1, sizeof P8K_MON8_3_0));
}
