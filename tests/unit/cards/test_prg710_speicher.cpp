/**
 * @file test_prg710_speicher.cpp
 * @brief Speicherverwaltung E8H–EBH + OPS (PRG 710): Arbeitsmodell §4b, Punkte 1–4,
 *        und der nachgespielte Ablauf des echten Boot-ROMs.  doc/design/20_prg710.md AP-P1b.
 */

#include <gtest/gtest.h>

#include "core/cards/prg710_speicher/prg710_speicher.h"

namespace {

using Q = Prg710Speicher::Quelle;

struct Prg710Speicher_ : ::testing::Test {
    K1520Bus        bus;
    Prg710Speicher  sp{bus};
    uint8_t zre_[0x1000]{};
    uint8_t vram_[0x800]{};
    void SetUp() override {
        sp.attachToBus(bus);
        sp.powerOn(0x00);
        for (auto& b : zre_) b = 0xAA;
        for (auto& b : vram_) b = 0xBB;
        sp.setZreWeg([this](uint16_t a) { return zre_[a]; },
                     [this](uint16_t a, uint8_t d) { zre_[a] = d; });
        sp.setVramWeg([this](uint16_t a) { return vram_[a - 0xF800]; },
                      [this](uint16_t a, uint8_t d) { vram_[a - 0xF800] = d; });
    }
    /// OUT (C),r mit B = Seite·10H, C = Port
    void outC(int seite, uint8_t port, uint8_t d) { bus.ioWrite(uint16_t(seite << 12 | port), d); }
    /// OUT (n),A: A (= Daten) liegt auf A8–A15
    void outN(uint8_t port, uint8_t a) { bus.ioWrite(uint16_t(a << 8 | port), a); }
    void freigabe(uint8_t v) { bus.ioWrite(0x00EB, v); }
};

}  // namespace

TEST_F(Prg710Speicher_, NachResetZreUndVram) {
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre);
    EXPECT_EQ(sp.ortVon(0x0FFF).quelle, Q::Zre);
    EXPECT_EQ(sp.ortVon(0x1000).quelle, Q::Ops);
    EXPECT_EQ(sp.ortVon(0xF7FF).quelle, Q::Ops);
    EXPECT_EQ(sp.ortVon(0xF800).quelle, Q::Vram);
    EXPECT_EQ(sp.memRead(0x0123), 0xAA);
    EXPECT_EQ(sp.memRead(0xF800), 0xBB);
    sp.memWrite(0x2000, 0x42);
    EXPECT_EQ(sp.opsPeek(0x2000), 0x42);
    EXPECT_EQ(sp.freigabe(), 0);
}

TEST_F(Prg710Speicher_, RegisterWirkenErstMitFreigabe) {
    outC(0, 0xE8, 0x10);                       // RAM in Seite 0 — bei EBH = 0 ohne Wirkung
    EXPECT_EQ(sp.attr(0), 0x10);
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre);
    freigabe(0x0F);
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Ops);
    freigabe(0x00);
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre);
}

TEST_F(Prg710Speicher_, SeiteNullRamOderZre) {
    freigabe(0x0F);
    outC(0, 0xE8, 0x10);
    sp.memWrite(0x0C10, 0x77);
    EXPECT_EQ(sp.opsPeek(0x0C10), 0x77);
    EXPECT_EQ(zre_[0x0C10], 0xAA);
    outC(0, 0xE8, 0x0F);
    EXPECT_EQ(sp.memRead(0x0C10), 0xAA);
    sp.memWrite(0x0C10, 0x11);
    EXPECT_EQ(zre_[0x0C10], 0x11);
}

TEST_F(Prg710Speicher_, SeiteFVramOderRam) {
    freigabe(0x0F);
    outC(0xF, 0xE8, 0xFF);
    EXPECT_EQ(sp.ortVon(0xF800).quelle, Q::Vram);
    EXPECT_EQ(sp.ortVon(0xF7FF).quelle, Q::Ops);   // F000–F7FF bleibt RAM
    EXPECT_EQ(sp.ortVon(0xF000).quelle, Q::Ops);
    outC(0xF, 0xE8, 0xF0);
    outC(0xF, 0xEA, 0x0F);   // Identität, wie das ROM sie schreibt
    EXPECT_EQ(sp.ortVon(0xF800).quelle, Q::Ops);
    EXPECT_EQ(sp.ortVon(0xF7FF).quelle, Q::Ops);
    sp.memWrite(0xFFFF, 0x5A);
    EXPECT_EQ(sp.opsPeek(0xFFFF), 0x5A);
}

TEST_F(Prg710Speicher_, AndereSeitenMitSystemkarteSindLeer) {
    freigabe(0x0F);
    outC(5, 0xE8, 0x01);
    EXPECT_EQ(sp.ortVon(0x5000).quelle, Q::Leer);
    EXPECT_EQ(sp.memRead(0x5000), 0xFF);
    sp.memWrite(0x5000, 0x12);   // verpufft
    EXPECT_EQ(sp.opsPeek(0x5000), 0x00);
}

TEST_F(Prg710Speicher_, EahVerschiebtEineSeite) {
    freigabe(0x0F);
    sp.opsPoke(0x3123, 0xC3);
    outC(7, 0xEA, 0x03);   // logische Seite 7 → physische Seite 3
    EXPECT_EQ(sp.seite(7), 3);
    EXPECT_EQ(sp.memRead(0x7123), 0xC3);
    sp.memWrite(0x7124, 0x99);
    EXPECT_EQ(sp.opsPeek(0x3124), 0x99);
    outC(7, 0xEA, 0xF5);   // nur 4 Bit
    EXPECT_EQ(sp.seite(7), 5);
}

TEST_F(Prg710Speicher_, RegisterAdresseAusA12bisA15) {
    outC(3, 0xE8, 0x10);        // OUT (C),r: B = 30H
    EXPECT_EQ(sp.attr(3), 0x10);
    outN(0xE8, 0xF0);           // OUT (E8H),A mit A = F0H → Seite F
    EXPECT_EQ(sp.attr(0xF), 0xF0);
    EXPECT_EQ(sp.attr(0), 0x00);
    outN(0xE8, 0xFF);
    EXPECT_EQ(sp.attr(0xF), 0xFF);
}

TEST_F(Prg710Speicher_, LesenLiefertFFundE9Harmlos) {
    EXPECT_EQ(bus.ioRead(0x00E8), 0xFF);
    EXPECT_EQ(bus.ioRead(0x00EB), 0xFF);
    bus.ioWrite(0x00E9, 0x55);
    EXPECT_EQ(sp.freigabe(), 0);
}

TEST_F(Prg710Speicher_, ResetLoeschtRegisterNichtDenRam) {
    freigabe(0x0F);
    outC(0, 0xE8, 0x10);
    outC(2, 0xEA, 0x04);
    sp.memWrite(0x0100, 0x66);
    sp.reset();
    EXPECT_EQ(sp.freigabe(), 0);
    EXPECT_EQ(sp.attr(0), 0);
    EXPECT_EQ(sp.seite(2), 0);
    EXPECT_EQ(sp.opsPeek(0x0100), 0x66);
    EXPECT_EQ(sp.ortVon(0x0100).quelle, Q::Zre);
}

// Ablauf des echten ROMs (§4a/§4b): 00 → EB, 16 × (EA, E8), 0F → E8[0], 0F → EB, Kopie, 10 → E8[0]
TEST_F(Prg710Speicher_, AblaufDesEchtenRoms) {
    bus.ioWrite(0x00EB, 0x00);
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre);
    for (int s = 0; s < 16; ++s) {
        outC(s, 0xEA, uint8_t(s));
        // Seite 0 bekommt 10H schon im ersten Durchlauf, während das ROM noch aus ihr läuft
        outC(s, 0xE8, s == 0 ? 0x10 : (s == 15 ? 0xF0 : 0x10));
        EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre) << "EBH = 0: ROM läuft weiter, Seite " << s;
    }
    outC(0, 0xE8, 0x0F);
    bus.ioWrite(0x00EB, 0x0F);
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Zre);
    EXPECT_EQ(sp.ortVon(0xF800).quelle, Q::Ops);   // Seite F = F0H → RAM (VRAM aus)
    // Kopie des ROMs nach 1000H (OPS-RAM)
    for (int i = 0; i < 0x400; ++i) sp.memWrite(uint16_t(0x1000 + i), uint8_t(i));
    EXPECT_EQ(sp.opsPeek(0x1010), 0x10);
    outC(0, 0xE8, 0x10);   // Seite 0 → RAM
    EXPECT_EQ(sp.ortVon(0x0000).quelle, Q::Ops);
    EXPECT_EQ(sp.ortVon(0x0000).offset, 0x0000);
    outC(0xF, 0xE8, 0xFF);   // VRAM sichtbar
    EXPECT_EQ(sp.ortVon(0xF800).quelle, Q::Vram);
}
