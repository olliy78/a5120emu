/**
 * @file test_mc80_asp.cpp
 * @brief ASP des MC80.3x (AP M3): Portlage SIO D0H/CTC D4H/PIO D8H (A-Steuer = D9H), EGOS-Init
 *        (egos.prn 03EBH ff.), Tastaturkanal SIO A (Rx-Vektor FFFCH, Senden), Kette SIO > CTC >
 *        PIO, CTC-K3-Anstoss 91H/01H ohne Doppelinterrupt.  Quelle: doc/mc80/hardware_3x.md §6.
 */

#include <gtest/gtest.h>
#include "core/cards/mc80/asp.h"

namespace {

struct Asp {
    K1520Bus bus;
    Mc80Asp asp;
    Asp() {
        asp.attachToBus(bus);
        bus.setInterruptChain({&asp});
        asp.reset();
    }
    void out(uint8_t p, uint8_t d) { bus.ioWrite(p, d); bus.markIntDirty(); bus.updateInterruptChain(); }
    uint8_t in(uint8_t p) { return bus.ioRead(p); }
    /// CON_INIT wie egos.prn 03EBH–041AH
    void egosInit() {
        for (uint8_t b : {0x47, 0x04}) out(0xD4, b);
        for (uint8_t b : {0x18, 0x02, 0xF0, 0x01, 0x04}) out(0xD3, b);
        for (uint8_t b : {0x18, 0x14, 0x4D, 0x03, 0x61, 0x05, 0xAA, 0x01, 0x18}) out(0xD1, b);
        out(0xD4, 0xE8);
        out(0xD7, 0x07);
        out(0xD7, 0x01);
    }
    bool intAn() { bus.markIntDirty(); bus.updateInterruptChain(); return bus.isINT(); }
};

TEST(Mc80Asp, EgosInitStelltSioAAuf9600Baud7O2) {
    Asp a;
    a.egosInit();
    const auto f = a.asp.sio().channelA().format();
    EXPECT_EQ(f.teiler, 16);
    EXPECT_EQ(f.stopp_halbe, 4);
    EXPECT_EQ(f.paritaet, 1);
    EXPECT_EQ(f.rx_bits, 7);
    EXPECT_EQ(f.tx_bits, 7);
    EXPECT_FALSE(a.intAn());
}

TEST(Mc80Asp, TastaturzeichenLoestDenVektorFFFCAus) {
    Asp a;
    a.egosInit();
    ASSERT_TRUE(a.asp.tastaturEmpfaengerFrei());
    a.asp.empfange('A');
    ASSERT_TRUE(a.intAn());
    EXPECT_EQ(a.bus.interruptAcknowledge(), 0xFC);   // WR2 F0H + Rx A (Status beeinflusst Vektor)
    EXPECT_EQ(a.in(0xD0) & 0x7F, 'A');
    a.bus.signalRETI();
    EXPECT_FALSE(a.intAn());
}

TEST(Mc80Asp, SendenAnDieTastatur) {
    Asp a;
    a.egosInit();
    EXPECT_FALSE(a.asp.hatGesendet());
    a.out(0xD0, 0x07);
    ASSERT_TRUE(a.asp.hatGesendet());
    EXPECT_EQ(a.asp.nimmGesendet(), 0x07);
    EXPECT_FALSE(a.asp.hatGesendet());
}

TEST(Mc80Asp, CtcK3Vektor_FFEE_UndAbschaltenVerwirftDieAnforderung) {
    Asp a;
    a.egosInit();
    a.out(0xD7, 0x91);            // SIO_A_ISR: K3 Zeitgeber, Interrupt ein, ZK 1 (×16)
    a.asp.clockTick(16);
    ASSERT_TRUE(a.intAn());
    EXPECT_EQ(a.bus.interruptAcknowledge(), 0xEE);
    a.asp.clockTick(80);          // TASTE_ISR: ~70 Takte bis OUT (D7H),01H — neue Anforderung
    a.out(0xD7, 0x01);            // Interrupt aus: verwirft sie (z80_ctc.h)
    a.bus.signalRETI();
    EXPECT_FALSE(a.intAn()) << "zweiter TASTE_ISR-Lauf = doppeltes Zeichen";
}

TEST(Mc80Asp, KetteSioVorCtcVorPio) {
    Asp a;
    a.egosInit();
    a.out(0xD7, 0x91);
    a.asp.clockTick(16);           // CTC K3 fordert an
    a.asp.empfange('x');           // SIO fordert an
    ASSERT_TRUE(a.intAn());
    EXPECT_EQ(a.bus.interruptAcknowledge(), 0xFC) << "SIO hat Vorrang";
    a.in(0xD0);
    a.bus.signalRETI();
    ASSERT_TRUE(a.intAn());
    EXPECT_EQ(a.bus.interruptAcknowledge(), 0xEE);
    a.out(0xD7, 0x03);             // K3 ruecksetzen
    a.bus.signalRETI();
    // PIO: A-Steuer ist D9H (nicht DAH)
    a.out(0xD9, 0xE0);             // Vektorwort (Bit 0 = 0)
    a.out(0xD9, 0x4F);             // Betriebsart 1 (Eingabe)
    a.out(0xD9, 0x87);             // Interrupt ein
    EXPECT_EQ(a.asp.pio().debugState().port[0].vector, 0xE0);
    EXPECT_EQ(a.asp.pio().debugState().port[0].mode, 1);
    EXPECT_EQ(a.asp.pio().debugState().port[1].vector, 0xFF) << "Kanal B unberuehrt";
}

}  // namespace
