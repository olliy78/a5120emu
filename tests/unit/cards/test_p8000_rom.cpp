/**
 * @file test_p8000_rom.cpp
 * @brief Die erzeugten ROM-Header des P8000 stimmen mit den Binärabzügen in
 *        doc/p8000/eproms/ überein (doc/design/25_p8000.md §10.5, AP P5b).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/rom_mon8.h"
#include "core/cards/p8000/rom_mon16.h"
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

// ─── MON16 (AP P10a) ─────────────────────────────────────────────────────────

namespace {
/// Prüfsumme wie MON16 `p.test.s` CRC_TEST (jedes zweite Byte ab @p start, @p n Bytes).
uint16_t mon16Crc(const uint8_t* img, unsigned start, unsigned n) {
    auto rr = [](uint8_t x) { return uint8_t((x >> 1) | (x << 7)); };
    uint8_t h = 0xFF, l = 0xFF;
    for (unsigned i = 0, a = start; i < n; ++i, a += 2) {
        uint8_t r = uint8_t(img[a] ^ h); h = r;
        r = uint8_t(rr(rr(rr(rr(r)))) & 0x0F); r ^= h; h = r;
        r = rr(rr(rr(r))); const uint8_t h0 = r; r &= 0x1F; r ^= l; l = r;
        r = uint8_t(rr(h0) & 0xF0); r ^= l; l = r;
        r = uint8_t(h0 & 0xE0); r ^= h; h = l; l = r;
    }
    return uint16_t((h << 8) | l);
}

/// Feld = 1H/1L verschränkt (0000–1FFF), dann 2H/2L (2000–3FFF); gerade Adresse = H = D8–15.
template <size_t N>
void pruefe16(const uint8_t (&feld)[N], const std::string& version) {
    ASSERT_EQ(N, 16384u) << version;
    const char* teil[4] = {"1H", "1L", "2H", "2L"};
    for (int t = 0; t < 4; ++t) {
        const auto e = abzug("16BIT/MON16_" + std::string(teil[t]) + "_" + version);
        ASSERT_EQ(e.size(), 4096u) << version << teil[t];
        const size_t basis = (t / 2) * 0x2000 + (t % 2);
        for (size_t i = 0; i < e.size(); ++i)
            ASSERT_EQ(feld[basis + 2 * i], e[i]) << version << " " << teil[t] << " Byte " << i;
    }
    // Die Monitor-Selbstprüfung (Testschritt 40) muss mit dem Feld bestehen.
    auto w = [&](unsigned a) { return uint16_t((feld[a] << 8) | feld[a + 1]); };
    EXPECT_EQ(mon16Crc(feld, 0x0000, 0x1000), w(0x3FF0)) << version << " 1H";
    EXPECT_EQ(mon16Crc(feld, 0x0001, 0x1000), w(0x3FF4)) << version << " 1L";
    EXPECT_EQ(mon16Crc(feld, 0x2000, 0x0FF8), w(0x3FF8)) << version << " 2H";
    EXPECT_EQ(mon16Crc(feld, 0x2001, 0x0FF8), w(0x3FFC)) << version << " 2L";
    // Reset-Vektor (P2b §2): FCW C000, Segment 0
    EXPECT_EQ(w(0x0002), 0xC000) << version;
    EXPECT_EQ(w(0x0004), 0x8000) << version;
}
}  // namespace

TEST(P8000Rom, Mon16HeaderGleichDenVierEinzelabzuegen) {
    pruefe16(P8K_MON16_3_0, "3.0");
    pruefe16(P8K_MON16_3_1, "3.1");
    pruefe16(P8K_MON16_3_3, "3.3");
}

TEST(P8000Rom, Mon16_3_3_ZusammengefuehrterAbzugIstFalsch) {
    // full/MON16_3.3 (pofo.de) weicht in 2L ab 3001H ab; die Prüfsumme 2L stimmt dort nicht —
    // deshalb ist rom_mon16.h aus den Einzelabzügen erzeugt.  Wächter gegen ein „Vereinfachen".
    const auto full = abzug("16BIT/full/MON16_3.3");
    ASSERT_EQ(full.size(), 16384u);
    EXPECT_NE(mon16Crc(full.data(), 0x2001, 0x0FF8), uint16_t((full[0x3FFC] << 8) | full[0x3FFD]));
    EXPECT_NE(0, std::memcmp(full.data(), P8K_MON16_3_3, 16384));
}
