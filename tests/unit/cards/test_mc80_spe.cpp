/**
 * @file test_mc80_spe.cpp
 * @brief SPE 2 des MC80.3x + Segmentfenster (AP M2): ROM 2000H–27FFH, Leerbereich
 *        2800H–3FFFH = 00H, ROM-Block nicht segmentabhängig, RAM-Segment 4000H–BFFFH über das
 *        Segmentregister, 1-KB-Sperren B0H–B3H (A10–A15), nach Reset frei.
 *        doc/mc80/hardware_3x.md §3.2/§4, doc/mc80/egos.md §1 Punkte 4–5.
 */

#include <gtest/gtest.h>

#include "core/cards/mc80/spe.h"
#include "core/cards/mc80/rom_spe2.h"
#include "core/cards/mc80/rom_segmente.h"

namespace {

struct Mc80SpeTest : ::testing::Test {
    K1520Bus bus;
    Mc80Spe2 spe1{Mc80Spe2::Config::mc8030()};
    Mc80Spe2 spe2{Mc80Spe2::Config::nurRam(0x02)};
    Mc80Segmentfenster fenster;
    void SetUp() override {
        spe1.attachToBus(bus);
        spe2.attachToBus(bus);
        fenster.anmelden(&spe1);
        fenster.anmelden(&spe2);
        fenster.attachToBus(bus);
        spe1.powerOn(0x00);
        spe2.powerOn(0x00);
        segment(0x00);
    }
    void segment(uint8_t m) { spe1.setSegmentRegister(m); spe2.setSegmentRegister(m); }
};

TEST_F(Mc80SpeTest, RomBlock2000Bis27FF) {
    EXPECT_EQ(sizeof(MC80_SPE2_ROM), 0x800u);
    for (uint16_t a : {0x2000, 0x2004, 0x23FF, 0x2400, 0x25BD, 0x27FF})
        EXPECT_EQ(bus.memRead(a), MC80_SPE2_ROM[a - 0x2000]) << std::hex << a;
    bus.memWrite(0x2000, static_cast<uint8_t>(~MC80_SPE2_ROM[0]));
    EXPECT_EQ(bus.memRead(0x2000), MC80_SPE2_ROM[0]);
}

TEST_F(Mc80SpeTest, LeerbereichLiest00) {
    // M1: nur mit 00H läuft die ROM-Kette über 2048 leere Objekte bis 4000H weiter.
    for (uint32_t a = 0x2800; a < 0x4000; ++a)
        ASSERT_EQ(bus.memRead(static_cast<uint16_t>(a)), 0x00) << std::hex << a;
}

TEST_F(Mc80SpeTest, RomBlockHaengtNichtAmSegmentregister) {
    for (uint8_t m : {0x00, 0x01, 0x02, 0x80}) {
        segment(m);
        EXPECT_EQ(bus.memRead(0x2000), MC80_SPE2_ROM[0]) << int(m);
    }
}

TEST_F(Mc80SpeTest, OhneSegmentLiestDasFensterFFUndSchreibtNicht) {
    bus.memWrite(0x4000, 0x12);
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);
    segment(0x01);
    EXPECT_EQ(bus.memRead(0x4000), 0x00);   // nichts angekommen
}

TEST_F(Mc80SpeTest, SegmentumschaltungWaehltDieKarte) {
    segment(0x01);
    bus.memWrite(0x4000, 0x11);
    bus.memWrite(0xBFFF, 0x1F);
    segment(0x02);
    EXPECT_EQ(bus.memRead(0x4000), 0x00);
    bus.memWrite(0x4000, 0x22);
    bus.memWrite(0xBFFF, 0x2F);
    segment(0x01);
    EXPECT_EQ(bus.memRead(0x4000), 0x11);
    EXPECT_EQ(bus.memRead(0xBFFF), 0x1F);
    segment(0x02);
    EXPECT_EQ(bus.memRead(0x4000), 0x22);
    EXPECT_EQ(bus.memRead(0xBFFF), 0x2F);
    segment(0x04);   // Segment 3: keine Karte
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);
    EXPECT_EQ(spe1.ramPeek(0x4000), 0x11);
    EXPECT_EQ(spe2.ramPeek(0x4000), 0x22);
}

TEST_F(Mc80SpeTest, FensterGrenzen) {
    segment(0x01);
    EXPECT_EQ(bus.memRead(0x3FFF), 0x00);        // Leerbereich, nicht RAM
    bus.memWrite(0x3FFF, 0x99);
    EXPECT_EQ(spe1.ramPeek(0x4000), 0x00);
    bus.memWrite(0xC000, 0x99);                   // gehört der ZVE (hier: niemand)
    EXPECT_EQ(spe1.ramPeek(0xBFFF), 0x00);
}

TEST_F(Mc80SpeTest, RomBlockSperrenUndFreigebenMitA10BisA15) {
    // OUT (C),r mit C = B0H, B = 20H → Block 2000H–23FFH gesperrt [SPE2 S.4–6].
    bus.ioWrite(0x20B0, 0x00);
    EXPECT_TRUE(spe1.romBlockGesperrt(0x2000 >> 10));
    EXPECT_EQ(bus.memRead(0x2000), 0xFF);
    EXPECT_EQ(bus.memRead(0x2400), MC80_SPE2_ROM[0x400]);
    bus.ioWrite(0x20B1, 0x00);
    EXPECT_EQ(bus.memRead(0x2000), MC80_SPE2_ROM[0]);
}

TEST_F(Mc80SpeTest, RamBlockSperreIstVorgabeSchreibschutz) {
    segment(0x01);
    bus.memWrite(0x4400, 0x5A);
    bus.ioWrite(0x44B2, 0x00);   // RAM 4400H–47FFH sperren
    bus.memWrite(0x4400, 0xA5);
    bus.memWrite(0x4800, 0x77);  // Nachbarblock frei
    EXPECT_EQ(bus.memRead(0x4400), 0x5A);   // lesbar, nicht beschrieben
    EXPECT_EQ(bus.memRead(0x4800), 0x77);
    bus.ioWrite(0x44B3, 0x00);
    bus.memWrite(0x4400, 0xA5);
    EXPECT_EQ(bus.memRead(0x4400), 0xA5);
}

TEST(Mc80Spe, ZugriffssperreTreibtDenBusNicht) {
    K1520Bus bus;
    auto c = Mc80Spe2::Config::mc8030();
    c.sperrart = Mc80Spe2::Sperrart::Zugriff;
    Mc80Spe2 spe{c};
    Mc80Segmentfenster f;
    spe.attachToBus(bus);
    f.anmelden(&spe);
    f.attachToBus(bus);
    spe.powerOn(0x33);
    spe.setSegmentRegister(0x01);
    bus.ioWrite(0x40B2, 0x00);
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);
    EXPECT_EQ(bus.memRead(0x4400), 0x33);
}

TEST_F(Mc80SpeTest, NachResetAllesFreiRamBleibt) {
    segment(0x01);
    bus.memWrite(0x5000, 0x42);
    bus.ioWrite(0x20B0, 0x00);
    bus.ioWrite(0x50B2, 0x00);
    spe1.reset();
    EXPECT_FALSE(spe1.romBlockGesperrt(0x2000 >> 10));
    EXPECT_FALSE(spe1.ramBlockGesperrt(0x5000 >> 10));
    EXPECT_EQ(bus.memRead(0x2000), MC80_SPE2_ROM[0]);
    EXPECT_EQ(bus.memRead(0x5000), 0x42);
}

TEST_F(Mc80SpeTest, ZweiteKarteOhneRomUndOhneTore) {
    EXPECT_EQ(bus.ioOwner(0xB0), &spe1);
    EXPECT_EQ(bus.ioOwner(0xB3), &spe1);
    EXPECT_FALSE(spe2.config().rom_block);
    EXPECT_EQ(spe2.config().io_base, 0);
}


// ─── EPROM-Segmente 10H/20H (AP M4, MC80.31E) ────────────────────────────────────────────
struct Mc80EpromSegmentTest : ::testing::Test {
    K1520Bus bus;
    Mc80Spe2 ram{Mc80Spe2::Config::mc8030()};
    Mc80EpromSegment s10{0x10};
    Mc80EpromSegment s20{0x20};
    Mc80Segmentfenster fenster;
    void SetUp() override {
        s10.abzug(0x4000, MC80_SEG10_D, sizeof(MC80_SEG10_D));
        s20.abzug(0x4000, MC80_SEG20_F, sizeof(MC80_SEG20_F));
        s20.abzug(0x5000, MC80_SEG20_G, sizeof(MC80_SEG20_G));
        ram.attachToBus(bus);
        fenster.anmelden(&ram);
        fenster.anmelden(&s10);
        fenster.anmelden(&s20);
        fenster.attachToBus(bus);
        ram.powerOn(0x00);
        segment(0x00);
    }
    void segment(uint8_t m) { ram.setSegmentRegister(m); s10.setSegmentRegister(m); s20.setSegmentRegister(m); }
};

TEST_F(Mc80EpromSegmentTest, NurDasGewaehlteSegmentIstSichtbar) {
    segment(0x00);
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);                 // keins gewählt: offener Bus
    segment(0x10);
    EXPECT_EQ(bus.memRead(0x4000), MC80_SEG10_D[0]);
    EXPECT_EQ(bus.memRead(0x4FFF), MC80_SEG10_D[0xFFF]);
    EXPECT_EQ(bus.memRead(0x5000), 0xFF);                 // 10H hat hier (im Test) keinen Abzug
    segment(0x20);
    EXPECT_EQ(bus.memRead(0x4000), MC80_SEG20_F[0]);
    EXPECT_EQ(bus.memRead(0x5123), MC80_SEG20_G[0x123]);
    segment(0x01);                                        // RAM-Segment 1: 00H aus dem dRAM
    EXPECT_EQ(bus.memRead(0x4000), 0x00);
}

TEST_F(Mc80EpromSegmentTest, EpromLiestNurUndWirdNieBeschrieben) {
    segment(0x10);
    bus.memWrite(0x4000, static_cast<uint8_t>(~MC80_SEG10_D[0]));
    EXPECT_EQ(bus.memRead(0x4000), MC80_SEG10_D[0]);
    // Schreiben bei gewähltem EPROM-Segment erreicht auch das (abgewählte) RAM nicht
    segment(0x01);
    EXPECT_EQ(bus.memRead(0x4000), 0x00);
    // RAM und EPROM zugleich gewählt: das Schreiben trifft das RAM, gelesen wird nach
    // Anmeldereihenfolge (RAM zuerst) -- am Gerät wäre das ein Buskonflikt.
    segment(0x11);
    bus.memWrite(0x4001, 0x5A);
    segment(0x01);
    EXPECT_EQ(bus.memRead(0x4001), 0x5A);
    segment(0x10);
    EXPECT_EQ(bus.memRead(0x4001), MC80_SEG10_D[1]);
}

TEST_F(Mc80EpromSegmentTest, AbzuegeSindDieEingebettetenQuellen) {
    // d = Tetris (bytegleich mit doc/mc80/spiele/tetris_4000_4fff.bin): Kette 'T','E',FFH bei 4003H
    segment(0x10);
    EXPECT_EQ(bus.memRead(0x4000), 0x01);
    EXPECT_EQ(bus.memRead(0x4003), 'T');
    EXPECT_EQ(bus.memRead(0x4004), 'E');
    // f = HASI
    segment(0x20);
    EXPECT_EQ(bus.memRead(0x4003), 'H');
    EXPECT_EQ(bus.memRead(0x4006), 'I');
}

}  // namespace
