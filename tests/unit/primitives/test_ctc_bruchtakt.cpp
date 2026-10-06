/**
 * @file test_ctc_bruchtakt.cpp
 * @brief Wächter für den Bruchtakt-Zähleingang des Z80CTC (P8000 P5d,
 *        doc/design/25_p8000.md §10.6): 1,229 MHz (9,832 MHz ÷ 8) neben dem
 *        4-MHz-Systemtakt, ganzzahliger Phasenakkumulator.
 */
#include <gtest/gtest.h>
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"

namespace {
constexpr uint64_t HZ = 1229000, PHI = 4000000;

void zaehler(Z80CTC& c, int ch, uint8_t tc) {   // Zähler, fallende Flanke egal, ohne Interrupt
    c.ioWrite(static_cast<uint8_t>(ch), 0x45);
    c.ioWrite(static_cast<uint8_t>(ch), tc);
}
}  // namespace

TEST(CtcBruchtakt, EineMillionImpulseInDreiMillionenTakten) {
    Z80CTC ctc;
    int zcto = 0;
    ctc.setzeEingangsTakt(0, HZ, PHI);
    ctc.setZCTOCallback([&](int, bool l) { if (!l) ++zcto; });
    zaehler(ctc, 0, 1);                          // TC 1: ein ZC/TO je Impuls
    for (int i = 0; i < 3254679; ++i) ctc.clockTick();
    EXPECT_NEAR(zcto, 1000000, 1);
}

TEST(CtcBruchtakt, BatchMitFensternGleichEinzeltakten) {
    for (int fenster : {1, 7, 100, 4096}) {
        Z80CTC a, b;
        long za = 0, zb = 0;
        a.setzeEingangsTakt(1, HZ, PHI);
        b.setzeEingangsTakt(1, HZ, PHI);
        a.setZCTOCallback([&](int, bool l) { if (!l) ++za; });
        b.setZCTOCallback([&](int, bool l) { if (!l) ++zb; });
        zaehler(a, 1, 200);
        zaehler(b, 1, 200);
        for (int i = 0; i < 1000000; ++i) a.clockTick();
        for (int i = 0; i < 1000000; i += fenster) b.clockTick(std::min(fenster, 1000000 - i));
        EXPECT_EQ(za, zb) << "Fenster " << fenster;
        EXPECT_EQ(a.getCount(1), b.getCount(1)) << "Fenster " << fenster;
    }
}

TEST(CtcBruchtakt, ZaehlstandFolgtImBatchWeg) {
    Z80CTC ctc;
    ctc.setzeEingangsTakt(2, HZ, PHI);
    zaehler(ctc, 2, 255);
    ctc.clockTick(100000);                       // 30 725 Impulse ⇒ 255 − (30725 mod 255)
    const int impulse = static_cast<int>(100000ull * HZ / PHI);
    int soll = 255 - impulse % 255;
    EXPECT_EQ(ctc.getCount(2), soll);
}

TEST(CtcBruchtakt, ZeichenzeitNeunTausendSechshundertBaud) {
    Z80CTC ctc;
    ctc.setzeEingangsTakt(0, HZ, PHI);
    zaehler(ctc, 0, 4);                          // ZK 4 ⇒ ZC/TO = 1 229 000/4; ÷2 (Baud-FF) × SIO ÷16
    const uint64_t q16 = ctc.teilerTakteQ16(0);
    // 9600 Bd 8N1 ≈ 1 229 000/4/2/16 = 9601,6 Bd; Zeichen = 10 Bit
    const auto f = k1520::serial::serialFormatRechnenQ16(32, 8, 0, 2, q16, PHI);
    ASSERT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    const double exakt = 10.0 * 32 * 4 * static_cast<double>(PHI) / HZ;
    EXPECT_NEAR(static_cast<double>(f.zeichen_takte), exakt, 1.0);
    // Die ganzzahlige Auskunft bleibt für alte Aufrufer gerundet verfügbar
    EXPECT_EQ(ctc.teilerTakte(0), 13u);
}

TEST(CtcBruchtakt, KaskadeUeberQ16Quelle) {
    Z80CTC ctc;
    ctc.setzeEingangsTakt(2, HZ, PHI);
    zaehler(ctc, 2, 2);
    ctc.setzeEingangsQuelleQ16(3, [&] { return ctc.teilerTakteQ16(2); });
    zaehler(ctc, 3, 5);
    const double soll = 10.0 * PHI / HZ * 65536.0;
    EXPECT_NEAR(static_cast<double>(ctc.teilerTakteQ16(3)), soll, 10.0);
}

TEST(CtcBruchtakt, VorgabeUnveraendertGanzzahligeAuskunftUndKeinZaehlen) {
    Z80CTC ctc;
    ctc.setzeEingangsPeriode(0, 16);
    zaehler(ctc, 0, 3);
    EXPECT_EQ(ctc.teilerTakte(0), 48u);
    EXPECT_EQ(ctc.teilerTakteQ16(0), 48u << 16);
    int zcto = 0;
    ctc.setZCTOCallback([&](int, bool l) { if (!l) ++zcto; });
    ctc.clockTick(100000);                       // ohne Bruchtakt zählt nur clkTrg()
    EXPECT_EQ(zcto, 0);
    EXPECT_EQ(ctc.getCount(0), 3);
    ctc.setzeEingangsTakt(0, HZ, PHI);
    ctc.setzeEingangsTakt(0, 0, 0);              // wieder aus
    ctc.clockTick(100000);
    EXPECT_EQ(zcto, 0);
}
