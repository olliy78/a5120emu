/**
 * @file test_p8000_speicher8.cpp
 * @brief ADP-gebankter Speicher der P8000-8-Bit-Karte (doc/p8000/schaltplan_8bit.md §1,
 *        doc/design/25_p8000.md §10.11 AP P5a).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/speicher8.h"

namespace {

struct P8000Speicher8_ : ::testing::Test {
    K1520Bus        bus;
    P8000Speicher8  sp;
    uint8_t         rom[0x2000];

    void SetUp() override {
        // EPROM 1 = 10H + (Offset>>8) , EPROM 2 = 80H + (Offset>>8); Byte 0 = Kennung
        for (int i = 0; i < 0x1000; ++i) rom[i] = 0x10 + (i >> 8);
        for (int i = 0; i < 0x1000; ++i) rom[0x1000 + i] = 0x80 + (i >> 8);
        sp.setRom(rom, sizeof rom);
        sp.attachToBus(bus);
        sp.powerOn();
    }
    /// OUT (C),r mit B = Seite·10H, C = Port
    void outC(int seite, uint8_t port, uint8_t d) { bus.ioWrite(uint16_t(seite << 12 | port), d); }
    void adp(int seite, uint8_t sel) { outC(seite, 0x00, sel); }
    void rffRaus() { bus.ioRead(0x0004); }
    void alleSeiten(uint8_t sel) { for (int s = 0; s < 16; ++s) adp(s, sel); }
};

}  // namespace

TEST_F(P8000Speicher8_, NachResetIstJedeSeiteEpromMit8KSpiegel) {
    // auch mit gesetztem ADP wirkt es nicht, solange RFF Q = 0 ist
    alleSeiten(0x04);
    for (int seite = 0; seite < 16; ++seite) {
        const uint16_t a = uint16_t(seite << 12) | 0x0123;
        EXPECT_EQ(bus.memRead(a), seite & 1 ? 0x81 : 0x11) << "Seite " << seite;
    }
    EXPECT_EQ(bus.memRead(0x0000), 0x10);
    EXPECT_EQ(bus.memRead(0x2000), 0x10);   // Periode 8 K
    EXPECT_EQ(bus.memRead(0xE000), 0x10);
    EXPECT_EQ(bus.memRead(0xF0FF), 0x80);
    // Schreiben geht ins Leere (kein RAM selektiert)
    bus.memWrite(0x4000, 0x55);
    EXPECT_EQ(bus.memRead(0x4000), 0x10);
    EXPECT_FALSE(sp.rffGesetzt());
}

TEST_F(P8000Speicher8_, EpromHaelfteFolgtA12DesFensters) {
    adp(0x8, 0x01);   // PROM_SEL auf 8000H: A12 = 0 ⇒ EPROM 1 (nicht frei verschiebbar)
    adp(0x3, 0x01);   // 3000H: A12 = 1 ⇒ EPROM 2
    rffRaus();
    EXPECT_EQ(bus.memRead(0x8000), 0x10);
    EXPECT_EQ(bus.memRead(0x8200), 0x12);
    EXPECT_EQ(bus.memRead(0x3000), 0x80);
    EXPECT_EQ(bus.memRead(0x3F00), 0x8F);
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);   // nichts selektiert
}

TEST_F(P8000Speicher8_, AdpZelleAusB_InhaltAusA) {
    // OUT (C),r: B = Seite·10H wählt die Zelle, Inhalt = D0–D2 des Datenbytes
    outC(0x5, 0x00, 0x06);
    EXPECT_EQ(sp.adp(5), 0x06);
    EXPECT_EQ(sp.adp(4), 0x00);
    outC(0x5, 0x02, 0xFF);                  // 00H–03H spiegeln, Bit 3–7 fallen weg
    EXPECT_EQ(sp.adp(5), 0x07);
    // OUT (n),A: A liegt auf A8–A15 ⇒ Zelle = A >> 4, Inhalt = A & 7 aus demselben Byte
    bus.ioWrite(uint16_t(0xA5 << 8 | 0x00), 0xA5);
    EXPECT_EQ(sp.adp(0xA), 0x05);
    // 04H–07H schreibt den ADP nicht
    outC(0x1, 0x04, 0x07);
    EXPECT_EQ(sp.adp(1), 0x00);
    // Lesen von 00H–03H ändert nichts und liefert FFH
    EXPECT_EQ(bus.ioRead(uint16_t(0x5000)), 0xFF);
    EXPECT_EQ(sp.adp(5), 0x07);
}

TEST_F(P8000Speicher8_, RffDurchInOderOutUndNurEinmalWirksam) {
    alleSeiten(0x04);
    EXPECT_FALSE(sp.rffGesetzt());
    bus.ioWrite(0x0004, 0x00);   // OUT genügt ebenso wie IN
    EXPECT_TRUE(sp.rffGesetzt());
    bus.memWrite(0x1000, 0x42);
    EXPECT_EQ(bus.memRead(0x1000), 0x42);   // DRAM
    // zweiter Zugriff: wirkungslos, kein Zurückfallen
    bus.ioRead(0x0004);
    bus.ioWrite(0x0005, 0x00);
    EXPECT_TRUE(sp.rffGesetzt());
    EXPECT_EQ(bus.memRead(0x1000), 0x42);
    // ADP bleibt auch danach beschreibbar (WEADP), RFF bleibt
    adp(1, 0x01);
    EXPECT_EQ(bus.memRead(0x1000), 0x80);
    // /RES: RFF zurück, ADP und RAM bleiben
    sp.reset();
    EXPECT_FALSE(sp.rffGesetzt());
    EXPECT_EQ(sp.adp(1), 0x01);
    EXPECT_EQ(sp.dram(0x1000), 0x42);
    EXPECT_EQ(bus.memRead(0x1000), 0x80);   // wieder EPROM überall
}

TEST_F(P8000Speicher8_, SramZweimalJe4KSeite) {
    adp(0x2, 0x02);
    adp(0x9, 0x02);
    rffRaus();
    bus.memWrite(0x2010, 0xA1);
    EXPECT_EQ(bus.memRead(0x2810), 0xA1);   // A11 nicht ausgewertet: 2 K zweimal je 4 K
    EXPECT_EQ(bus.memRead(0x2010), 0xA1);
    bus.memWrite(0x27FF, 0xA2);
    EXPECT_EQ(bus.memRead(0x2FFF), 0xA2);
    // dieselben 2 K liegen hinter jeder SRAM-Seite (A12–A15 nicht ausgewertet)
    EXPECT_EQ(bus.memRead(0x9010), 0xA1);
    EXPECT_EQ(bus.memRead(0x9810), 0xA1);
    EXPECT_EQ(sp.sram(0x010), 0xA1);
}

TEST_F(P8000Speicher8_, MehrfachselektionLiestUndSchreibtAlle) {
    // SRAM + DRAM: Schreiben in beide, Lesen = UND (Low gewinnt) [Annahme]
    adp(0x6, 0x06);
    rffRaus();
    bus.memWrite(0x6005, 0xF0);
    EXPECT_EQ(sp.sram(0x005), 0xF0);
    EXPECT_EQ(sp.dram(0x6005), 0xF0);
    sp.dram(0x6005) = 0x3C;
    EXPECT_EQ(bus.memRead(0x6005), 0xF0 & 0x3C);
    // PROM + DRAM: Schreiben nur ins DRAM, Lesen = ROM UND DRAM
    adp(0x7, 0x05);
    bus.memWrite(0x7002, 0x0F);
    EXPECT_EQ(sp.dram(0x7002), 0x0F);
    EXPECT_EQ(bus.memRead(0x7002), 0x80 & 0x0F);
    // alle drei
    adp(0xB, 0x07);
    bus.memWrite(0xB100, 0xAA);
    EXPECT_EQ(sp.sram(0x100), 0xAA);
    EXPECT_EQ(sp.dram(0xB100), 0xAA);
    EXPECT_EQ(bus.memRead(0xB100), uint8_t(0xAA & 0x81));
}

TEST_F(P8000Speicher8_, LeereSeiteLiestFF) {
    adp(0xC, 0x00);
    rffRaus();
    EXPECT_EQ(bus.memRead(0xC000), 0xFF);
    bus.memWrite(0xC000, 0x12);             // geht nirgendwohin
    EXPECT_EQ(bus.memRead(0xC000), 0xFF);
    EXPECT_EQ(sp.dram(0xC000), 0x00);
    // Die E/A-Lücken lesen ebenfalls FF, nichts belegt
    EXPECT_EQ(bus.ioRead(0x0000), 0xFF);
}

TEST_F(P8000Speicher8_, EpromZugriffKostetZweiWartetakte) {
    EXPECT_EQ(sp.nimmWartetakte(), 0u);
    bus.memRead(0x0000);                    // nach Reset: EPROM (auch M1/DMA zählen gleich)
    EXPECT_EQ(sp.nimmWartetakte(), 2u);
    bus.memRead(0x0001);
    bus.memRead(0x0002);
    EXPECT_EQ(sp.nimmWartetakte(), 4u);
    EXPECT_EQ(sp.nimmWartetakte(), 0u);     // Abruf setzt zurück
    adp(0x0, 0x04);
    adp(0x1, 0x02);
    rffRaus();
    bus.memRead(0x0000);                    // DRAM
    bus.memRead(0x1000);                    // SRAM
    EXPECT_EQ(sp.nimmWartetakte(), 0u);
    EXPECT_EQ(sp.epromZugriffe(), 3u);
    sp.peek(0x2000);                        // peek zählt nicht
    EXPECT_EQ(sp.nimmWartetakte(), 0u);
}
