/**
 * @file test_k52_codec.cpp
 * @brief `.k52`: verlustfrei, bitgleich hin und zurueck, strenge Pruefung (AP K3).
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/peripherals/kassette/k52_codec.h"

using namespace kassette;

namespace {

std::vector<uint8_t> daten(size_t n, uint8_t start = 0) {
    std::vector<uint8_t> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(start + i);
    return d;
}

/// Ein Medium mit allem, was verloren gehen koennte: beide Seiten, Bandmarke, falsche CRC,
/// fremde Praeambel, Schreibschutz, ungewoehnliche Dichte/Geschwindigkeit/Geometrie.
KassettenMedium voll() {
    Geometrie g;
    g.laenge_um = 60'000'000;
    g.vorspann_um = 800'000;
    g.marken_abstand_um = 300'000;
    g.marke_um = 15'000;
    g.initialluecke_um = 35'000;
    KassettenMedium m(g, 32'000, 190);
    m.haengeAn(Seite::A, 35'000, blockBauen(daten(128)));
    auto kaputt = blockBauen(daten(40, 9));
    kaputt[5] ^= 0xFF;
    m.haengeAn(Seite::A, 17'800, kaputt);
    m.haengeAn(Seite::A, 20'300, bandmarkeBytes());
    m.haengeAn(Seite::A, 250'000, blockBauen(daten(256, 3), 0xAB, 0x55));
    m.haengeAn(Seite::B, 35'000, blockBauen(daten(1, 0xEE)));
    m.setSchreibschutz(Seite::B, true);
    return m;
}

}  // namespace

TEST(K52Codec, HinUndZurueckIstBitgleich) {
    const KassettenMedium m = voll();
    const auto datei = k52::kodieren(m);
    std::string fehler;
    const auto zurueck = k52::dekodieren(datei, fehler);
    ASSERT_TRUE(zurueck.has_value()) << fehler;
    EXPECT_TRUE(zurueck->gleichInhalt(m));
    EXPECT_EQ(k52::kodieren(*zurueck), datei) << "kodieren(dekodieren(x)) == x";
}

TEST(K52Codec, LeeresBandHinUndZurueck) {
    const KassettenMedium m;
    const auto datei = k52::kodieren(m);
    std::string fehler;
    const auto z = k52::dekodieren(datei, fehler);
    ASSERT_TRUE(z.has_value()) << fehler;
    EXPECT_TRUE(z->gleichInhalt(m));
    EXPECT_EQ(k52::kodieren(*z), datei);
}

TEST(K52Codec, KopfEnthaeltKennungVersionUndGeschwindigkeit) {
    const auto datei = k52::kodieren(voll());
    ASSERT_GE(datei.size(), 12u);
    EXPECT_EQ(std::string(datei.begin(), datei.begin() + 4), "K52T");
    EXPECT_EQ(datei[4], k52::kVersion);
    EXPECT_EQ(datei[5], 2) << "Seiten";
    EXPECT_EQ(datei[6] | (datei[7] << 8), 190) << "Aufnahmegeschwindigkeit mm/s";
    EXPECT_TRUE(k52::istK52(datei));
    EXPECT_FALSE(k52::istK52({'K', '5', '2'}));
    EXPECT_FALSE(k52::istK52({'M', 'Z', 'x', 'y', 'z'}));
}

TEST(K52Codec, FalscheCrcFremdePraeambelUndSchreibschutzUeberleben) {
    std::string fehler;
    const auto z = k52::dekodieren(k52::kodieren(voll()), fehler);
    ASSERT_TRUE(z.has_value()) << fehler;
    EXPECT_FALSE(crcStimmt(z->block(Seite::A, 1).bytes)) << "kaputte CRC bleibt kaputt";
    EXPECT_EQ(z->block(Seite::A, 3).bytes.front(), 0xAB);
    EXPECT_EQ(z->block(Seite::A, 3).bytes.back(), 0x55);
    EXPECT_TRUE(z->block(Seite::A, 2).bandmarke());
    EXPECT_TRUE(z->schreibgeschuetzt(Seite::B));
    EXPECT_FALSE(z->schreibgeschuetzt(Seite::A));
    EXPECT_EQ(z->dichteMilli(), 32'000u);
    EXPECT_EQ(z->aufnahmeMmS(), 190u);
    EXPECT_EQ(z->geometrie().laenge_um, 60'000'000u);
}

TEST(K52Codec, DiePositionStehtNichtInDerDatei) {
    KassettenMedium m = voll();
    const auto a = k52::kodieren(m);
    m.setPosition(12'345'678);
    EXPECT_EQ(k52::kodieren(m), a);
}

TEST(K52Codec, WeistFalscheKennungUndVersionAb) {
    std::string fehler;
    EXPECT_FALSE(k52::dekodieren({}, fehler));
    EXPECT_FALSE(fehler.empty());
    auto d = k52::kodieren(voll());
    auto x = d;
    x[0] = 'X';
    fehler.clear();
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    EXPECT_NE(fehler.find("K52T"), std::string::npos);
    x = d;
    x[4] = 2;
    fehler.clear();
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    EXPECT_NE(fehler.find("Version"), std::string::npos);
    x = d;
    x[5] = 3;
    fehler.clear();
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    EXPECT_NE(fehler.find("Seiten"), std::string::npos);
}

TEST(K52Codec, JedeAbgeschnitteneFassungWirdAbgewiesen) {
    const auto d = k52::kodieren(voll());
    for (size_t n = 0; n < d.size(); ++n) {
        std::string fehler;
        const std::vector<uint8_t> teil(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_FALSE(k52::dekodieren(teil, fehler).has_value()) << "Laenge " << n;
        EXPECT_FALSE(fehler.empty()) << "Laenge " << n;
    }
}

TEST(K52Codec, ResteHinterDemInhaltWerdenAbgewiesen) {
    auto d = k52::kodieren(voll());
    d.push_back(0);
    std::string fehler;
    EXPECT_FALSE(k52::dekodieren(d, fehler));
    EXPECT_NE(fehler.find("Reste"), std::string::npos);
}

TEST(K52Codec, UnbrauchbareBandmasseUndPassendeBloeckeWerdenGeprueft) {
    auto d = k52::kodieren(KassettenMedium());
    // Laenge (Bytes 12..15) auf 0: kein Platz fuer Vorspann und Marken.
    auto x = d;
    for (int i = 12; i < 16; ++i) x[static_cast<size_t>(i)] = 0;
    std::string fehler;
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    EXPECT_NE(fehler.find("Bandma"), std::string::npos);
    // Dichte 0.
    x = d;
    for (int i = 8; i < 12; ++i) x[static_cast<size_t>(i)] = 0;
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    // Eine absurde Blockzahl auf Seite A scheitert an der Dateilaenge, ohne zu reservieren.
    x = d;
    const size_t seiteA = 4 + 2 + 2 + 4 + 20;  // hinter dem Kopf
    x[seiteA + 2] = 0xFF;
    x[seiteA + 3] = 0xFF;
    x[seiteA + 4] = 0xFF;
    x[seiteA + 5] = 0x7F;
    fehler.clear();
    EXPECT_FALSE(k52::dekodieren(x, fehler));
    EXPECT_FALSE(fehler.empty());
}

TEST(K52Codec, BlockDerNichtAufsBandPasstWirdAbgewiesen) {
    // Ein Band, dessen Luecke ueber das Magnetband hinausfuehrt, entsteht nur durch eine kaputte Datei.
    KassettenMedium m;
    m.haengeAn(Seite::A, 40'000, blockBauen(daten(10)));
    auto d = k52::kodieren(m);
    const size_t seiteA = 4 + 2 + 2 + 4 + 20;
    const size_t luecke = seiteA + 6;  // erste Luecke des ersten Blocks
    d[luecke + 0] = 0xFF;
    d[luecke + 1] = 0xFF;
    d[luecke + 2] = 0xFF;
    d[luecke + 3] = 0x7F;
    std::string fehler;
    EXPECT_FALSE(k52::dekodieren(d, fehler));
    EXPECT_NE(fehler.find("Block 0"), std::string::npos);
}
