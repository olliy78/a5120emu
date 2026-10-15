// tests/unit/cards/test_k7023.cpp
// K7023 / K7023.01 (BAB1): Adressierung, Kursor, Helligkeitsmarken, Zeichensatz-Goldbild.
// Beleg: doc/a5120_alt/k7023.md (Paragraphen in den Kommentaren).
#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "core/bus/k1520_bus.h"
#include "core/cards/k7023/k7023.h"
#include "core/cards/k7023/chargen_k7023_zg1.h"
#include "core/cards/k7023/chargen_k7023_zg2.h"

namespace {

K7023::Config cfg01()
{
    K7023::Config c;
    c.variante_01 = true;
    return c;
}

// Pixel (x,y) innerhalb der Zelle (col,row)
uint8_t px(const K7023& k, int col, int row, int x, int y)
{
    return k.getFramebuffer()[(row * 16 + y) * 512 + col * 8 + x];
}

// Zellmuster als '#'/'.'-Zeilen (Linien 0..lines-1, 8 Spalten)
std::vector<std::string> muster(const K7023& k, int col, int row, int lines)
{
    std::vector<std::string> r;
    for (int y = 0; y < lines; ++y) {
        std::string s;
        for (int x = 0; x < 8; ++x) s += px(k, col, row, x, y) ? '#' : '.';
        r.push_back(s);
    }
    return r;
}

std::vector<uint8_t> datei(const std::string& n)
{
    std::ifstream f(std::string(K7023_ABZUG_DIR) + "/" + n, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace

TEST(K7023, GeometrieUndAdresse)
{
    K1520Bus bus;
    K7023 k(bus);
    EXPECT_EQ(k.fbWidth(), 512);
    EXPECT_EQ(k.fbHeight(), 256);
    // Offset = Zeile*64 + Spalte, Vorgabe FC00H (§3, §4, §10)
    bus.memWrite(0xFC00 + 3 * 64 + 5, 0x41);
    EXPECT_EQ(k.vramRead(5, 3), 0x41);
    EXPECT_EQ(bus.memRead(0xFC00 + 3 * 64 + 5), 0x41);
    EXPECT_EQ(bus.memRead(0xFFFF), 0xFF);   // Einschaltzustand
}

TEST(K7023, KeineLesesperre)
{
    K1520Bus bus;
    K7023 k(bus);
    EXPECT_TRUE(k.isReadable());
    EXPECT_TRUE(k.isWritable());
}

TEST(K7023, BasisadresseEinstellbarImKilobyteRaster)
{
    K1520Bus bus;
    K7023::Config c;
    c.vram_base = 0x4000;
    K7023 k(bus, c);
    bus.memWrite(0x4000 + 64, 0x42);
    EXPECT_EQ(k.vramRead(0, 1), 0x42);
    bus.memWrite(0x43FF, 0x43);
    EXPECT_EQ(k.vramRead(63, 15), 0x43);
    // Brückencodierung: nur A10-A15 wählbar
    K1520Bus bus2;
    K7023::Config c2;
    c2.vram_base = 0x4123;
    K7023 k2(bus2, c2);
    EXPECT_EQ(k2.config().vram_base, 0x4000);
}

TEST(K7023, ZeichengeneratorAbzuegeEingebettet)
{
    auto z1 = datei("BAB1-46-2.bin"), z2 = datei("BAB1-47-1.bin");
    ASSERT_EQ(z1.size(), 1024u);
    ASSERT_EQ(z2.size(), 1024u);
    for (int i = 0; i < 1024; ++i) {
        ASSERT_EQ(CHARGEN_K7023_ZG1[i], z1[i]) << i;
        ASSERT_EQ(CHARGEN_K7023_ZG2[i], z2[i]) << i;
    }
}

// Goldbilder aus §6 (selbst aus den Abzügen gezogen), Linien 0..10
TEST(K7023, GoldbildGroßA)
{
    K1520Bus bus;
    K7023 k(bus);
    k.vramWrite(0, 0, 0x41);
    const std::vector<std::string> soll = {
        "........", "...#....", "..#.#...", ".#...#..", "#.....#.", "#.....#.",
        "#######.", "#.....#.", "#.....#.", "#.....#.", "........"};
    EXPECT_EQ(muster(k, 0, 0, 11), soll);
}

TEST(K7023, GoldbildKleinGMitUnterlaenge)
{
    K1520Bus bus;
    K7023 k(bus);
    k.vramWrite(1, 0, 0x67);
    const std::vector<std::string> soll = {
        "........", "........", "........", "........", ".###.#..", "#...##..",
        "#....#..", "#...##..", ".###.#..", ".....#..", ".####..."};
    EXPECT_EQ(muster(k, 1, 0, 11), soll);   // Linie 10 stammt aus dem 2. EPROM
}

TEST(K7023, GoldbildUnterstrichUndTilde)
{
    K1520Bus bus;
    K7023 k(bus);
    k.vramWrite(0, 0, 0x5F);
    k.vramWrite(1, 0, 0x7E);
    EXPECT_EQ(muster(k, 0, 0, 11)[10], "#######.");
    EXPECT_EQ(muster(k, 1, 0, 11)[1], ".######.");
}

TEST(K7023, AchterPunktUndLinien11Bis15SindDunkel)
{
    K1520Bus bus;
    K7023 k(bus);
    k.clearScreen(0x20);
    for (int c = 0; c < 64; ++c) k.vramWrite(c, 0, 0x7E - (c % 3));
    for (int y = 0; y < 16; ++y)
        for (int c = 0; c < 64; ++c) {
            EXPECT_EQ(px(k, c, 0, 7, y), 0);
            if (y >= 11) EXPECT_EQ(px(k, c, 0, 3, y), 0);
        }
}

TEST(K7023, Codes00Bis1FUnd7FSindLeer)
{
    K1520Bus bus;
    K7023 k(bus);
    for (int code : {0x00, 0x03, 0x10, 0x1F, 0x7F}) {
        k.vramWrite(0, 0, static_cast<uint8_t>(code));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 8; ++x) EXPECT_EQ(px(k, 0, 0, x, y), 0) << code;
    }
}

TEST(K7023, KursorBalkLinien12Bis15VolleBreiteUndStetsHell)
{
    K1520Bus bus;
    K7023 k(bus);   // .00
    k.vramWrite(2, 1, 0x80 | 'A');
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x) {
            bool balken = y >= 12;
            if (balken) EXPECT_EQ(px(k, 2, 1, x, y), 0xFF) << x << "," << y;
        }
    // Zeichen bleibt daneben unverändert sichtbar (Linie 6 von 'A')
    EXPECT_EQ(muster(k, 2, 1, 11)[6], "#######.");
    // ohne Bit 7 kein Balken
    k.vramWrite(2, 1, 'A');
    EXPECT_EQ(px(k, 2, 1, 3, 13), 0);
}

TEST(K7023, KursorBlinkenFolgtDerPhase)
{
    K1520Bus bus;
    K7023::Config c;
    c.cursor_blink = true;
    K7023 k(bus, c);
    k.vramWrite(0, 0, 0x80 | ' ');
    EXPECT_EQ(px(k, 0, 0, 0, 14), 0xFF);
    k.setBlinkPhase(false);
    EXPECT_EQ(px(k, 0, 0, 0, 14), 0);
    k.setBlinkPhase(true);
    EXPECT_EQ(px(k, 0, 0, 0, 14), 0xFF);
}

TEST(K7023, VarianteNullNullKenntNurEineHelligkeit)
{
    K1520Bus bus;
    K7023 k(bus);
    k.clearScreen(0x20);
    k.vramWrite(0, 0, 0x04);      // Marke: ohne Wirkung
    k.vramWrite(1, 0, 'A');
    EXPECT_EQ(px(k, 1, 0, 3, 1), 0xFF);
    k.vramWrite(0, 0, 0x09);
    EXPECT_EQ(px(k, 1, 0, 3, 1), 0xFF);
}

TEST(K7023, Variante01Helligkeit)
{
    K1520Bus bus;
    K7023 k(bus, cfg01());
    const uint8_t normal = k.config().helligkeit_normal;
    k.clearScreen(0x20);
    k.vramWrite(0, 0, 'A');
    k.vramWrite(2, 0, 'A');
    EXPECT_EQ(px(k, 0, 0, 3, 1), normal);       // Anfangszustand normal
    k.vramWrite(1, 0, 0x07);                    // hell ein (DB1 = 1)
    EXPECT_EQ(px(k, 0, 0, 3, 1), normal);       // davor unverändert
    EXPECT_EQ(px(k, 2, 0, 3, 1), 0xFF);         // danach hell
    // Marke selbst ist dunkel
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x) EXPECT_EQ(px(k, 1, 0, x, y), 0);
    // läuft über das Zeilenende weiter
    k.vramWrite(0, 1, 'A');
    EXPECT_EQ(px(k, 0, 1, 3, 1), 0xFF);
    // 09H schaltet zurück
    k.vramWrite(3, 0, 0x09);
    k.vramWrite(4, 0, 'A');
    EXPECT_EQ(px(k, 4, 0, 3, 1), normal);
    EXPECT_EQ(px(k, 0, 1, 3, 1), normal);       // Folgezellen wurden nachgezogen
    // Überschreiben der Marke 07H durch ein Zeichen nimmt "hell" zurück
    k.vramWrite(3, 0, 0x20);
    EXPECT_EQ(px(k, 4, 0, 3, 1), 0xFF);         // 07H steht noch bei 1 -> wieder hell
    k.vramWrite(1, 0, 0x20);
    EXPECT_EQ(px(k, 4, 0, 3, 1), normal);
}

TEST(K7023, Variante01MarkenFamilie04Bis0F)
{
    K1520Bus bus;
    K7023 k(bus, cfg01());
    const uint8_t normal = k.config().helligkeit_normal;
    for (int code = 0x04; code <= 0x0F; ++code) {
        k.clearScreen(0x20);
        k.vramWrite(0, 0, static_cast<uint8_t>(code));
        k.vramWrite(1, 0, 'A');
        EXPECT_EQ(px(k, 1, 0, 3, 1), (code & 2) ? 0xFF : normal) << code;
    }
    // Außerhalb der Familie: keine Marke (00-03, 10H, 20H ...)
    for (int code : {0x00, 0x01, 0x02, 0x03, 0x10, 0x1F}) {
        k.clearScreen(0x20);
        k.vramWrite(0, 0, static_cast<uint8_t>(code));
        k.vramWrite(1, 0, 'A');
        EXPECT_EQ(px(k, 1, 0, 3, 1), normal) << code;
    }
}

TEST(K7023, Variante01KursorIstImmerHell)
{
    K1520Bus bus;
    K7023 k(bus, cfg01());
    k.clearScreen(0x20);
    k.vramWrite(0, 0, 0x80 | ' ');
    EXPECT_EQ(px(k, 0, 0, 0, 13), 0xFF);
}

TEST(K7023, SerialisierungRoundTrip)
{
    K1520Bus bus;
    K7023 k(bus, cfg01());
    k.vramWrite(5, 5, 0x07);
    k.vramWrite(6, 5, 'g');
    std::vector<uint8_t> blob;
    k.serialize(blob);
    ASSERT_EQ(blob.size(), 1024u);

    K1520Bus bus2;
    K7023 k2(bus2, cfg01());
    const uint8_t* p = blob.data();
    ASSERT_TRUE(k2.deserialize(p, blob.data() + blob.size()));
    EXPECT_EQ(k2.vramRead(6, 5), 'g');
    EXPECT_EQ(px(k2, 6, 5, 1, 5), px(k, 6, 5, 1, 5));
    const uint8_t* q = blob.data();
    EXPECT_FALSE(k2.deserialize(q, blob.data() + 10));
}

TEST(K7023, DirtyFlag)
{
    K1520Bus bus;
    K7023 k(bus);
    k.fbClearDirty();
    EXPECT_FALSE(k.fbDirty());
    bus.memWrite(0xFC00, 'x');
    EXPECT_TRUE(k.fbDirty());
}
