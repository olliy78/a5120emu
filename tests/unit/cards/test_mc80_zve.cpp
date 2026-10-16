/**
 * @file test_mc80_zve.cpp
 * @brief ZVE des MC80.3x (AP M2): EGOS-ROM 0000H–1FFFH, RAM C000H–FFFFH, Segmentregister =
 *        ZVE-PIO Tor B (85H), RAM-Steuerung 8CH/8DH als Schreibsenke, Schreibschutz FC00H
 *        (nach Reset aus), CTC 80H mit Interrupt.  doc/mc80/hardware_3x.md §2–§3, egos.md §1.
 */

#include <gtest/gtest.h>
#include <vector>

#include "core/cards/mc80/zve3x.h"
#include "core/cards/mc80/rom_zve3x.h"

namespace {

struct Mc80ZveTest : ::testing::Test {
    K1520Bus bus;
    Mc80Zve  zve{bus};
    std::vector<uint8_t> segmente;   // jede Meldung an die Speicherkarten
    void SetUp() override {
        zve.attachToBus(bus);
        bus.setInterruptChain({&zve});
        zve.setSegmentCallback([this](uint8_t m) { segmente.push_back(m); });
        zve.powerOn(0x00);
        segmente.clear();
    }
    /// Kleines Programm ins RAM legen und die CPU dort @p schritte Befehle laufen lassen.
    void fahre(uint16_t adr, std::initializer_list<uint8_t> code, int schritte) {
        uint16_t a = adr;
        for (uint8_t b : code) zve.ramPoke(a++, b);
        zve.cpu().PC = adr;
        for (int i = 0; i < schritte; ++i) zve.cpu().step();
    }
};

TEST_F(Mc80ZveTest, RomLiegtBei0000BisBei1FFF) {
    // EGOS: 0000H = JP 0776H (RESET) [egos.prn]
    EXPECT_EQ(bus.memRead(0x0000), 0xC3);
    EXPECT_EQ(bus.memRead(0x0001), 0x76);
    EXPECT_EQ(bus.memRead(0x0002), 0x07);
    for (uint16_t a : {0x0800, 0x1000, 0x1800, 0x1FFF})
        EXPECT_EQ(bus.memRead(a), MC80_ZVE_ROM[a]) << std::hex << a;
    bus.memWrite(0x0000, 0x00);   // ROM schluckt den Schreibzyklus
    EXPECT_EQ(bus.memRead(0x0000), 0xC3);
}

TEST_F(Mc80ZveTest, RamC000BisFFFFLesbarUndSchreibbar) {
    for (uint16_t a : {0xC000, 0xE123, 0xFBFF, 0xFC00, 0xFFFF}) {
        bus.memWrite(a, static_cast<uint8_t>(a ^ 0x5A));
        EXPECT_EQ(bus.memRead(a), static_cast<uint8_t>(a ^ 0x5A)) << std::hex << a;
    }
    EXPECT_EQ(bus.memRead(0x2000), 0xFF);   // nicht die ZVE
}

TEST_F(Mc80ZveTest, PowerOnBelegtRamVorResetErhaeltEs) {
    zve.powerOn(0xA5);
    EXPECT_EQ(bus.memRead(0xC000), 0xA5);
    bus.memWrite(0xD000, 0x12);
    zve.reset();
    EXPECT_EQ(bus.memRead(0xD000), 0x12);
}

TEST_F(Mc80ZveTest, SegmentregisterIstPioTorB) {
    // EGOS: OUT (87H),0FH (Tor B Betriebsart 0), dann OUT (85H),Einsernummer.
    bus.ioWrite(0x0087, 0x0F);
    bus.ioWrite(0x0085, 0x04);
    EXPECT_EQ(zve.segmentRegister(), 0x04);
    ASSERT_FALSE(segmente.empty());
    EXPECT_EQ(segmente.back(), 0x04);
}

TEST_F(Mc80ZveTest, TorANichtDasSegmentregister) {
    // Berichtigung M0: Tor A (84H/86H) bleibt dem Anwender.
    bus.ioWrite(0x0086, 0x0F);
    bus.ioWrite(0x0084, 0x02);
    EXPECT_EQ(zve.segmentRegister(), 0x00);
    EXPECT_TRUE(segmente.empty());
}

TEST_F(Mc80ZveTest, ResetSetztSegmentregisterAufNull) {
    bus.ioWrite(0x0087, 0x0F);
    bus.ioWrite(0x0085, 0x01);
    zve.reset();
    EXPECT_EQ(zve.segmentRegister(), 0x00);
    ASSERT_FALSE(segmente.empty());
    EXPECT_EQ(segmente.back(), 0x00);
}

TEST_F(Mc80ZveTest, CpuSchaltetDasSegmentMitOut85) {
    // LD A,0FH / OUT (87H),A / LD A,02H / OUT (85H),A
    fahre(0xC100, {0x3E, 0x0F, 0xD3, 0x87, 0x3E, 0x02, 0xD3, 0x85}, 4);
    EXPECT_EQ(zve.segmentRegister(), 0x02);
}

TEST_F(Mc80ZveTest, SchreibschutzNachResetAus) {
    EXPECT_FALSE(zve.schreibschutz());
    zve.setSchreibschutz(true);
    zve.reset();
    EXPECT_FALSE(zve.schreibschutz());
}

TEST_F(Mc80ZveTest, SchreibschutzMachtSchreibenAbFC00Wirkungslos) {
    bus.memWrite(0xFC00, 0x11);
    bus.memWrite(0xFBFF, 0x22);
    zve.setSchreibschutz(true);
    bus.memWrite(0xFC00, 0x33);
    bus.memWrite(0xFFFF, 0x44);
    bus.memWrite(0xFBFF, 0x55);   // unter dem Fenster: frei
    EXPECT_EQ(bus.memRead(0xFC00), 0x11);
    EXPECT_NE(bus.memRead(0xFFFF), 0x44);
    EXPECT_EQ(bus.memRead(0xFBFF), 0x55);
    zve.setSchreibschutz(false);
    bus.memWrite(0xFC00, 0x66);
    EXPECT_EQ(bus.memRead(0xFC00), 0x66);
}

TEST_F(Mc80ZveTest, RamSteuerung8CIstSchreibsenke) {
    // Die RESET-Schleife von EGOS: 256 × OUT (C),B mit C = 8CH, B = 00H, FFH … 01H [P 0780].
    // Danach beschreibt EGOS FC00H–FFFFH ungehindert → der Schutz bleibt aus.
    // LD BC,008CH / OUT (C),B / DJNZ -4 / LD A,77H / LD (FFFCH),A
    fahre(0xC200, {0x01, 0x8C, 0x00, 0xED, 0x41, 0x10, 0xFC, 0x3E, 0x77, 0x32, 0xFC, 0xFF},
          1 + 2 * 256 + 2);
    EXPECT_EQ(zve.ramSteuerAusgaben(), 256u);
    EXPECT_FALSE(zve.schreibschutz());
    EXPECT_EQ(bus.memRead(0xFFFC), 0x77);
    bus.ioWrite(0x008D, 0x00);
    EXPECT_FALSE(zve.schreibschutz());
    EXPECT_EQ(zve.ramSteuerAusgaben(), 257u);
    EXPECT_NE(bus.ioOwner(0x8C), nullptr);
    EXPECT_NE(bus.ioOwner(0x8D), nullptr);
}

TEST_F(Mc80ZveTest, CtcAuf80MitInterruptvektor) {
    bus.ioWrite(0x0080, 0xD8);          // Vektor (Bit 0 = 0) → Kanal 0..3 = D8/DA/DC/DE
    bus.ioWrite(0x0080, 0x87);          // Kanal 0: Interrupt, Zeitgeber, ÷16, ZK folgt
    bus.ioWrite(0x0080, 0x02);          // Zeitkonstante 2 → nach 32 Takten
    EXPECT_FALSE(zve.hasInterrupt());
    zve.clockTick(64);
    bus.markIntDirty();
    bus.updateInterruptChain();
    EXPECT_TRUE(zve.hasInterrupt());
    EXPECT_TRUE(bus.isINT());
    EXPECT_EQ(zve.getVector(), 0xD8);
}

TEST_F(Mc80ZveTest, Takt2457600Hz) {
    EXPECT_EQ(Mc80Zve::CPU_HZ, 2'457'600u);
}

}  // namespace
