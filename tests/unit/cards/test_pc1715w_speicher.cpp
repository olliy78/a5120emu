/**
 * @file test_pc1715w_speicher.cpp
 * @brief Speicher des PC 1715W (AP-W1, doc/pc1715/pc1715w_hardware.md §2, §3): BR 24H, 74S287-Abbildung,
 *        getrennte Lese-/Schreibbank, Bank 0 „Hintergrund“, ZG-RAM (.ZGF), Bildquelle mit 8275.
 */

#include <gtest/gtest.h>

#include "core/cards/pc1715w_speicher/pc1715w_bild.h"
#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/cards/pc1715w_speicher/rom_s550.h"

namespace {

struct Fix {
    K1520Bus bus;
    Pc1715wSpeicher spk;
    Pc1715wBild bild{spk};
    Fix() {
        spk.attachToBus(bus);
        bild.attachToBus(bus);
        spk.powerOn(0x00);
    }
    void br(uint8_t v) { bus.ioWrite(0x24, v); }
};

/// Erwartete Tabelle aus doc/pc1715/pc1715w_hardware.md §2.2: Block (0 = B1 … 3 = B4) je Bank und
/// 16-K-Viertel (AB15–AB14), -1 = kein RAM.  Bewusst von Hand, nicht aus dem PROM gerechnet.
constexpr int ERWARTET[8][4] = {
    {-1, 0, 0, 0},   // 0 Hintergrund
    { 0, 0, 0, 0},   // 1 System
    { 1, 1, 1, 0},   // 2 TPA
    { 2, 2, 2, 0},   // 3 RAM-Disk
    { 3, 3, 3, 0},   // 4 RAM-Disk
    { 1, 2, 3, 0},   // 5 Mischsicht
    {-1,-1,-1,-1},   // 6
    {-1,-1,-1,-1},   // 7
};

/// 2048-Byte-ZGF-Abbild (Byte = Linie·128 + Code) mit „A“ (41H) nach SC619.ZGF (Handbuch §3).
std::array<uint8_t, 2048> zgfMitA() {
    std::array<uint8_t, 2048> z{};
    const uint8_t linie[10] = {0x00, 0x18, 0x24, 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42};
    for (int l = 0; l < 10; ++l) z[l * 128 + 0x41] = linie[l];
    return z;
}

void ladeZgf(Fix& f, uint16_t basis, const std::array<uint8_t, 2048>& z) {
    for (int i = 0; i < 2048; ++i) f.bus.memWrite(uint16_t(basis + i), z[i]);
}

/// 8275 wie der Lader: 80 × 24, 12 Linien, nicht transparent, Start Display.
void crtStart(Fix& f) {
    f.bus.ioWrite(0x19, 0x00);
    for (uint8_t p : {0x4F, 0x57, 0x6B, 0x6D}) f.bus.ioWrite(0x18, p);
    f.bus.ioWrite(0x19, 0x20);
}

void bildLeer(Fix& f) {
    for (int i = 0; i < 2048; ++i) f.bus.memWrite(uint16_t(0x3000 + i), 0x20);
}

bool pixel(const Pc1715wBild& b, int x, int y) { return b.framebuffer()[y * b.fbWidth() + x] != 0; }

}  // namespace

TEST(Pc1715wSpeicher, ResetIstBank0MitS550BeiNull) {
    Fix f;
    EXPECT_EQ(f.spk.bankRegister(), 0x00);
    for (int i = 0; i < 2048; ++i) ASSERT_EQ(f.bus.memRead(uint16_t(i)), PC1715W_S550_URLADER[i]) << i;
    EXPECT_EQ(f.bus.memRead(0x0800), PC1715W_S550_URLADER[0]);   // 2 K gespiegelt
    f.br(0x35);
    f.spk.reset();
    EXPECT_EQ(f.spk.bankRegister(), 0x00);
    EXPECT_EQ(f.bus.memRead(0x0000), PC1715W_S550_URLADER[0]);
}

TEST(Pc1715wSpeicher, Tabelle74S287FuerAlleBaenkeUndSeiten) {
    Fix f;
    for (int bank = 0; bank < 8; ++bank)
        for (int seite = 0; seite < 16; ++seite)
            EXPECT_EQ(f.spk.blockVon(bank, seite), ERWARTET[bank][seite >> 2])
                << "Bank " << bank << " Seite " << seite;
}

TEST(Pc1715wSpeicher, AbbildungWirktDurchBusUndBankRegister) {
    Fix f;
    // Je Bank/Viertel ein Marker über den Bus schreiben (Lese- = Schreibbank), Block rückprüfen.
    for (int bank = 1; bank < 6; ++bank) {
        f.br(uint8_t((bank << 4) | bank));
        for (int q = 0; q < 4; ++q) {
            const uint16_t a = uint16_t(q * 0x4000 + 0x123);
            f.bus.memWrite(a, uint8_t(0xA0 + bank * 4 + q));
            EXPECT_EQ(f.spk.blockRam(ERWARTET[bank][q], a), uint8_t(0xA0 + bank * 4 + q))
                << "Bank " << bank << " Viertel " << q;
        }
    }
    // Bank 6/7: lesen FFH, Schreiben verpufft
    f.br(0x66);
    f.bus.memWrite(0x5000, 0x12);
    EXPECT_EQ(f.bus.memRead(0x5000), 0xFF);
    f.br(0x77);
    EXPECT_EQ(f.bus.memRead(0x0000), 0xFF);
    for (int b = 0; b < 4; ++b) EXPECT_NE(f.spk.blockRam(b, 0x5000), 0x12);
}

TEST(Pc1715wSpeicher, CxxxBisFxxxIstInBank0Bis5GemeinsamB1) {
    Fix f;
    f.br(0x11);
    f.bus.memWrite(0xEC00, 0x5A);
    for (int bank = 0; bank <= 5; ++bank) {
        f.br(uint8_t(bank));
        EXPECT_EQ(f.bus.memRead(0xEC00), 0x5A) << bank;
    }
    // Bank 0 und 1 teilen sich ab 4000H dieselbe Zelle (B1)
    f.br(0x11);
    f.bus.memWrite(0x4000, 0x77);
    f.br(0x00);
    EXPECT_EQ(f.bus.memRead(0x4000), 0x77);
}

TEST(Pc1715wSpeicher, LesenUndSchreibenGetrennteBaenkeInterbankKopie) {
    Fix f;
    f.br(0x22);                                   // Bank 2 (TPA): B2
    for (int i = 0; i < 16; ++i) f.bus.memWrite(uint16_t(0x1000 + i), uint8_t(0x40 + i));
    f.br(0x32);                                   // Schreibbank 3 (B3), Lesebank 2 (B2)
    for (int i = 0; i < 16; ++i) f.bus.memWrite(uint16_t(0x2000 + i), f.bus.memRead(uint16_t(0x1000 + i)));
    f.br(0x33);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(f.bus.memRead(uint16_t(0x2000 + i)), uint8_t(0x40 + i));
    f.br(0x22);
    EXPECT_EQ(f.bus.memRead(0x2000), 0x00);       // Quelle nicht verändert, Ziel nicht dort
    EXPECT_EQ(f.bus.memRead(0x1000), 0x40);
}

TEST(Pc1715wSpeicher, UrladerAblaufS550KopiertNachBank1) {
    Fix f;
    // Wie S550 0024–003C: aus Bank 0 (Lesen S550) nach 4000H (B1), BR = 11H, zurück nach 0000H.
    for (int i = 0; i < 0x40; ++i) f.bus.memWrite(uint16_t(0x4000 + i), f.bus.memRead(uint16_t(i)));
    f.br(0x11);
    for (int i = 0; i < 0x40; ++i) f.bus.memWrite(uint16_t(i), f.bus.memRead(uint16_t(0x4000 + i)));
    for (int i = 0; i < 0x40; ++i) ASSERT_EQ(f.bus.memRead(uint16_t(i)), PC1715W_S550_URLADER[i]);
    f.br(0x00);   // zurück: wieder das EPROM, der Block B1 behält die Kopie
    EXPECT_EQ(f.spk.blockRam(0, 0x0000), PC1715W_S550_URLADER[0]);
}

TEST(Pc1715wSpeicher, Bank0HintergrundSchreibenUndOffeneBereiche) {
    Fix f;
    f.bus.memWrite(0x0010, 0x99);                 // ROM: verpufft
    EXPECT_EQ(f.bus.memRead(0x0010), PC1715W_S550_URLADER[0x10]);
    f.bus.memWrite(0x1000, 0x99);                 // offen
    EXPECT_EQ(f.bus.memRead(0x1000), 0xFF);
    f.bus.memWrite(0x3000, 0xC1);
    EXPECT_EQ(f.spk.bildRam(0), 0xC1);
    EXPECT_EQ(f.bus.memRead(0x3800), 0xC1);       // Spiegel [?]
    f.bus.memWrite(0x2005, 0x11);
    f.bus.memWrite(0x2805, 0x22);
    EXPECT_EQ(f.spk.zgRam(0, 5), 0x11);
    EXPECT_EQ(f.spk.zgRam(1, 5), 0x22);
    // Lesebank ≠ 0: der Hintergrund ist nicht sichtbar, Schreibbank 0 trifft ihn trotzdem
    f.br(0x01);
    EXPECT_EQ(f.bus.memRead(0x3000), 0x00);       // B1, nicht das Bild-RAM
    f.bus.memWrite(0x3001, 0xD2);                 // Schreibbank 0
    EXPECT_EQ(f.spk.bildRam(1), 0xD2);
}

TEST(Pc1715wSpeicher, MemdiSperrtAlleCas) {
    Fix f;
    f.br(0x11);
    f.bus.memWrite(0x8000, 0x66);
    EXPECT_EQ(f.bus.memRead(0x8000), 0x66);
    f.spk.setMemdi(true);
    EXPECT_EQ(f.bus.memRead(0x8000), 0xFF);
    f.bus.memWrite(0x8000, 0x77);                 // verpufft
    for (int b = 1; b < 16; ++b) EXPECT_EQ(f.spk.blockVon(1, b), -1);
    f.spk.setMemdi(false);
    EXPECT_EQ(f.bus.memRead(0x8000), 0x66);
}

TEST(Pc1715wSpeicher, BankRegisterIstUeberSpiegeladressenErreichbar) {
    Fix f;
    f.bus.ioWrite(0x27, 0x11);
    EXPECT_EQ(f.spk.bankRegister(), 0x11);
    EXPECT_EQ(f.spk.leseBank(), 1);
    EXPECT_EQ(f.spk.schreibBank(), 1);
    f.br(0xF5);                                   // Bit 3/7 ohne Wirkung auf die Bank
    EXPECT_EQ(f.spk.leseBank(), 5);
    EXPECT_EQ(f.spk.schreibBank(), 7);
}

TEST(Pc1715wBild, ZgRamLadenUndEinZeichenRastern) {
    Fix f;
    ladeZgf(f, 0x2000, zgfMitA());
    EXPECT_EQ(f.spk.zgRam(0, 1 * 128 + 0x41), 0x18);
    crtStart(f);
    bildLeer(f);
    f.bus.memWrite(0x3000 + 5, 0x41);             // Zelle (Spalte 5, Zeile 0)
    f.bild.frame();
    EXPECT_EQ(f.bild.screenChar(5, 0), 0x41);
    EXPECT_EQ(f.bild.screenChar(4, 0), 0x20);
    // Linie 1 = 18H: Pixel 3 und 4 (Bit 7 = links), Spalte 5 → x = 40 + 3, 40 + 4
    for (int x = 0; x < 8; ++x) EXPECT_EQ(pixel(f.bild, 40 + x, 1), x == 3 || x == 4) << x;
    // Linie 6 = 7EH: Pixel 1–6
    for (int x = 0; x < 8; ++x) EXPECT_EQ(pixel(f.bild, 40 + x, 6), x >= 1 && x <= 6) << x;
    EXPECT_FALSE(pixel(f.bild, 100, 1));   // (Zelle 0,0 trägt den Cursor)
    EXPECT_EQ(f.bild.framebuffer()[1 * f.bild.fbWidth() + 43], 0xB0);
}

TEST(Pc1715wBild, BildRamZellenInFramebufferZeilenUndAdresszaehler) {
    Fix f;
    ladeZgf(f, 0x2000, zgfMitA());
    crtStart(f);
    bildLeer(f);
    f.bus.memWrite(0x3000 + 80 * 23 + 79, 0x41);  // letzte Zelle: 3730H + 79 = 377FH
    f.bild.frame();
    EXPECT_EQ(f.bild.screenChar(79, 23), 0x41);
    EXPECT_TRUE(pixel(f.bild, 79 * 8 + 3, 23 * 12 + 1));
    EXPECT_EQ(f.bild.zaehler(), 1920);            // 80 × 24 Bytes geholt
    // VRTC: nächstes Bild beginnt wieder bei 0
    f.bild.frame();
    EXPECT_EQ(f.bild.zaehler(), 1920);
    EXPECT_EQ(f.bild.crt().lastFrameDmaBytes(), 1920);
}

TEST(Pc1715wBild, Port1BHSetztDenAdresszaehlerZurueck) {
    Fix f;
    crtStart(f);
    bildLeer(f);
    f.bild.frame();
    EXPECT_NE(f.bild.zaehler(), 0);
    f.bus.ioWrite(0x1B, 0x00);
    EXPECT_EQ(f.bild.zaehler(), 0);
}

TEST(Pc1715wBild, ZeichensatzWahl1AHXorGpa0) {
    Fix f;
    auto a1 = zgfMitA();
    std::array<uint8_t, 2048> a2{};
    a2[1 * 128 + 0x41] = 0xFF;                    // ZG 2: „A“ Linie 1 voll
    ladeZgf(f, 0x2000, a1);
    ladeZgf(f, 0x2800, a2);
    crtStart(f);
    bildLeer(f);
    f.bus.memWrite(0x3001, 0x41);                 // Spalte 1, Zeile 0, GPA0 = 0
    f.bus.memWrite(0x3002, 0x84);                 // Feldattribut GPA0 = 1 (belegt Spalte 2)
    f.bus.memWrite(0x3003, 0x41);                 // Spalte 3, GPA0 = 1
    f.bild.frame();
    // 1AH = 00H: Spalte 1 → ZG 1, Spalte 3 (GPA0) → ZG 2
    EXPECT_FALSE(f.bild.zg2());
    EXPECT_TRUE(pixel(f.bild, 8 + 3, 1));
    EXPECT_FALSE(pixel(f.bild, 8 + 0, 1));
    EXPECT_TRUE(pixel(f.bild, 24 + 0, 1));
    // 1AH Bit 4 = 1: vertauscht
    f.bus.ioWrite(0x1A, 0x10);
    EXPECT_TRUE(f.bild.zg2());
    f.bild.frame();
    EXPECT_TRUE(pixel(f.bild, 8 + 0, 1));         // Spalte 1 jetzt ZG 2 (voll)
    EXPECT_FALSE(pixel(f.bild, 24 + 0, 1));       // Spalte 3 jetzt ZG 1
    EXPECT_TRUE(pixel(f.bild, 24 + 3, 1));
    // andere Bits von 1AH zählen nicht
    f.bus.ioWrite(0x1A, 0xEF);
    EXPECT_FALSE(f.bild.zg2());
}

TEST(Pc1715wBild, ResetSetztZgWahlUndZaehlerZurueck) {
    Fix f;
    f.bus.ioWrite(0x1A, 0x10);
    f.bild.reset();
    EXPECT_FALSE(f.bild.zg2());
    EXPECT_EQ(f.bild.zaehler(), 0);
}
