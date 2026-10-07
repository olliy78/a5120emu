/**
 * @file test_p8000_karte16.cpp
 * @brief 16-Bit-Rechnerkarte des P8000 ohne Kopplungsgegenseite und ohne WDC: E/A-Dekoder,
 *        Reset/SCR, RETI, Trap-Register, Single-Step-Zähler, NMI-Identifier, Parität, Wartetakte,
 *        Save-State und das Rig mit MON16 3.1 (doc/p8000/schaltplan_16bit.md, doc/design/25_p8000.md
 *        §10.11 AP P10b/P10c).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/rom_mon16.h"
#include <array>
#include <string>
#include <vector>

using L = P8000MmuLogik16;

namespace {

constexpr uint16_t CODE_MMU = 0x00FC;

struct P8000Karte16_ : ::testing::Test {
    P8000Karte16 k;
    std::array<std::string, 4> tty;   ///< tty4–tty7 (Wandler-Stub: sofort abholen)

    /// Kopplungs-Stub der 8-Bit-Seite: PIO0-B7 = 0 gibt den U8001 frei (X3:B1 RESET inaktiv).
    void freigeben() { k.setResetEingang(false); }

    void abholen() {
        for (int i = 0; i < 4; ++i)
            while (k.anschluss(i).senderHatZeichen())
                tty[i] += static_cast<char>(k.anschluss(i).senderNimm());
    }
    bool laufeBisText(int i, const std::string& text, uint64_t max) {
        const uint64_t ende = k.zeit() + max;
        while (k.zeit() < ende) {
            k.schritt();
            abholen();
            if (tty[i].find(text) != std::string::npos) return true;
        }
        return false;
    }
    static Z8kBusCycle io(uint16_t port, bool lesen) {
        Z8kBusCycle c;
        c.st = Z8kStatus::Io; c.system = true; c.word = false; c.read = lesen; c.addr = port;
        return c;
    }
    void out(uint16_t port, uint8_t d) { k.busSchreiben(io(port, false), uint16_t(d << 8 | d)); }
    uint8_t in(uint16_t port) { return uint8_t(k.busLesen(io(port, true))); }
    static Z8kBusCycle mem(Z8kStatus st, uint8_t seg, uint16_t off, bool lesen, bool wort = true) {
        Z8kBusCycle c;
        c.st = st; c.system = true; c.word = wort; c.read = lesen; c.seg = seg; c.addr = off;
        return c;
    }
    uint16_t lies(Z8kStatus st, uint8_t seg, uint16_t off) { return k.busLesen(mem(st, seg, off, true)); }
    void schreib(uint8_t seg, uint16_t off, uint16_t w) { k.busSchreiben(mem(Z8kStatus::MemData, seg, off, false), w); }
    void sout(uint16_t mmu, uint8_t cmd, uint8_t v) {
        Z8kBusCycle c;
        c.st = Z8kStatus::SpecialIo; c.word = false; c.read = false; c.addr = uint16_t((cmd << 8) | (mmu & 0xFF));
        k.busSchreiben(c, uint16_t(v << 8 | v));
    }
    /// U8001 aus dem Reset holen (Resetsequenz aus dem EPROM), danach Register frei setzbar.
    void cpuBereit() {
        freigeben();
        k.schritt();
        ASSERT_EQ(k.cpu().lastException().kind, Z8kException::Reset);
    }
    /// Code ab SRAM-Offset (Segment 0, 4000H + @p off), Wörter big-endian.
    void code(uint16_t off, std::initializer_list<uint16_t> w) {
        for (uint16_t x : w) { k.sram(off) = uint8_t(x >> 8); k.sram(uint16_t(off + 1)) = uint8_t(x); off += 2; }
    }
    /// PSA im SRAM (4400H): Eintrag @p e (Byteoffset Z8002-Zählung) → FCW, Segment 0, Offset.
    void psa(uint16_t e, uint16_t fcw, uint16_t pc) {
        const uint16_t b = uint16_t(0x400 + e * 2);
        k.sram(uint16_t(b + 2)) = uint8_t(fcw >> 8); k.sram(uint16_t(b + 3)) = uint8_t(fcw);
        k.sram(uint16_t(b + 4)) = 0; k.sram(uint16_t(b + 5)) = 0;
        k.sram(uint16_t(b + 6)) = uint8_t(pc >> 8); k.sram(uint16_t(b + 7)) = uint8_t(pc);
        k.cpu().psapSeg = 0;
        k.cpu().psapOff = 0x4400;
    }
};


}  // namespace


// ─── E/A-Dekoder (Bl. 7, Handbuch Tab. 3.7-10) ───────────────────────────────

TEST_F(P8000Karte16_, CtcTore_JeKanalEineUngeradeAdresse) {
    for (int c = 0; c < 4; ++c) {
        out(uint16_t(0xFFA9 + 2 * c), 0x07);
        EXPECT_EQ(k.ctc0().debugState().ch[c].control, 0x07) << c;
        EXPECT_NE(k.ctc1().debugState().ch[c].control, 0x07) << c;
        out(uint16_t(0xFFB1 + 2 * c), 0x07);
        EXPECT_EQ(k.ctc1().debugState().ch[c].control, 0x07) << c;
    }
}

TEST_F(P8000Karte16_, PioTore_A1IstPort_A2IstSteuerwort) {
    Z80PIO* p[3] = {&k.pio0(), &k.pio1(), &k.pio2()};
    for (int i = 0; i < 3; ++i) {
        const uint16_t b = uint16_t(0xFF91 + 8 * i);
        out(uint16_t(b + 4), 0x0F);   // A: Modus 0
        out(uint16_t(b + 6), 0x0F);   // B: Modus 0
        out(uint16_t(b + 0), uint8_t(0x11 * (i + 1)));
        out(uint16_t(b + 2), uint8_t(0x22 * (i + 1)));
        EXPECT_EQ(p[i]->portARead(), uint8_t(0x11 * (i + 1))) << i;
        EXPECT_EQ(p[i]->portBRead(), uint8_t(0x22 * (i + 1))) << i;
        EXPECT_EQ(in(uint16_t(b + 0)), uint8_t(0x11 * (i + 1))) << i;
    }
}

TEST_F(P8000Karte16_, SioTore_A1IstKanal_A2IstSteuerwort_Tty4bis7) {
    for (int i = 0; i < 4; ++i) {
        const uint16_t d = uint16_t(0xFF81 + (i / 2) * 8 + (i & 1) * 2), c = uint16_t(d + 4);
        out(c, 0x05); out(c, 0x68);            // WR5: Sender ein, 8 Bit
        out(d, uint8_t('A' + i));
        ASSERT_TRUE(k.anschluss(i).senderHatZeichen()) << i;
        EXPECT_EQ(k.anschluss(i).senderNimm(), uint8_t('A' + i)) << i;
        for (int j = 0; j < 4; ++j) EXPECT_FALSE(k.anschluss(j).senderHatZeichen()) << i << "/" << j;
        EXPECT_EQ(std::string(k.anschluss(i).name()), "tty" + std::to_string(4 + i));
    }
}

TEST_F(P8000Karte16_, GeradeUndFremdeToreSindOffen_KeineSpiegel) {
    std::vector<uint8_t> vorher, nachher;
    k.serialize(vorher);
    for (uint32_t port = 0xFF00; port <= 0xFFFF; ++port) {
        const bool belegt = (port & 1) && port >= 0xFF81;
        if (belegt) continue;
        EXPECT_EQ(in(uint16_t(port)), 0xFF) << std::hex << port;
        out(uint16_t(port), 0x00);
    }
    for (uint32_t port = 0x0001; port < 0xFF00; port += 0x0102) {   // Stichprobe: A15–A7 ≠ 1
        EXPECT_EQ(in(uint16_t(port)), 0xFF) << std::hex << port;
        out(uint16_t(port), 0x00);
    }
    k.serialize(nachher);
    EXPECT_EQ(vorher, nachher) << "ein unbelegtes Tor hat Zustand verändert";
}

TEST_F(P8000Karte16_, PeripherieLiegtAmLowByte_AD8bis15Offen) {
    out(0xFFA9, 0x07);
    out(0xFFA9, 0x80);
    const uint16_t w = k.busLesen(io(0xFFA9, true));
    EXPECT_EQ(w >> 8, 0xFF);
}

// ─── Reset, SCR, PIO-Reset nach Index ────────────────────────────────────────

TEST_F(P8000Karte16_, NetzEin_HaeltDieCpuImResetBisX3B1Faellt) {
    EXPECT_TRUE(k.inReset());
    for (int i = 0; i < 100; ++i) EXPECT_EQ(k.schritt(), 1);
    EXPECT_EQ(k.cpu().lastException().cycle, 0u);
    freigeben();
    EXPECT_FALSE(k.inReset());
    k.schritt();   // Resetsequenz
    EXPECT_EQ(k.cpu().fcw, 0xC000) << "FCW aus 0002 (P2b §2)";
    EXPECT_EQ(k.cpu().pcSeg, 0);
    EXPECT_EQ(k.cpu().pc, uint16_t((P8K_MON16_3_1[6] << 8) | P8K_MON16_3_1[7]));
}

TEST_F(P8000Karte16_, Mreset_SetztScrAufNull_LiestBit4bis7Eins) {
    freigeben();
    out(L::P_SCR, 0x0F);
    EXPECT_EQ(in(L::P_SCR), 0xFF);
    k.setResetEingang(true);
    EXPECT_EQ(k.mmu().scr(), 0);
    EXPECT_EQ(in(L::P_SCR), 0xF0);
    out(L::P_SCR, 0x05);
    k.setResetEingang(false);
    EXPECT_EQ(in(L::P_SCR), 0xF5) << "nur die Flanke setzt zurück";
}

TEST_F(P8000Karte16_, Index4_PiosUeberlebenResetDurchDieAchtBitSeite_TresetSetztSieZurueck) {
    out(0xFF95, 0x0F);
    out(0xFF91, 0x5A);
    k.setResetEingang(true);
    k.setResetEingang(false);
    EXPECT_EQ(k.pio0().portARead(), 0x5A);
    out(L::P_SOFTRESET, 0);
    EXPECT_EQ(k.pio0().portARead(), 0x5A) << "SOFTRESET hängt nicht an PIORESET−";
    k.setTresetEingang(true);
    EXPECT_NE(k.pio0().portARead(), 0x5A);
}

TEST(P8000Karte16, Index1_PiosHaengenAnMreset) {
    P8000Karte16::Config c;
    c.index = P8000Karte16::Config::Index::I1;
    P8000Karte16 k(c);
    k.setResetEingang(false);
    Z8kBusCycle w;
    w.st = Z8kStatus::Io; w.word = false; w.read = false;
    w.addr = 0xFF95; k.busSchreiben(w, 0x0F0F);
    w.addr = 0xFF91; k.busSchreiben(w, 0x5A5A);
    ASSERT_EQ(k.pio0().portARead(), 0x5A);
    k.setResetEingang(true);
    EXPECT_NE(k.pio0().portARead(), 0x5A);
}

TEST_F(P8000Karte16_, Softreset_StartetDieCpuNeuAusDemResetvektor) {
    cpuBereit();
    k.cpu().pc = 0x4000;
    out(L::P_SCR, 0x04);
    out(L::P_SOFTRESET, 0);
    EXPECT_EQ(k.mmu().scr(), 0);
    k.schritt();
    EXPECT_EQ(k.cpu().lastException().kind, Z8kException::Reset);
    EXPECT_EQ(k.cpu().fcw, 0xC000);
}

// ─── Speicher: On-Board, Hauptspeicher, Wartetakt ────────────────────────────

TEST_F(P8000Karte16_, OnBoardNurSegment0_EpromSramLeer_DahinterHauptspeicher) {
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x0002), 0xC000);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x2000), uint16_t((P8K_MON16_3_1[0x2000] << 8) | P8K_MON16_3_1[0x2001]));
    schreib(0, 0x4002, 0x1234);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x5802), 0x1234) << "SRAM 4-fach gespiegelt";
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x6000), 0xFFFF) << "6000–7FFF leer";
    schreib(0, 0x0000, 0xBEEF);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x0000), 0x0000) << "EPROM nimmt nichts an";
    EXPECT_EQ(k.dram().peek(0), 0x00) << "und der Hauptspeicher darunter auch nicht";
    schreib(0, 0x8000, 0x4711);
    EXPECT_EQ(k.dram().peek(0x8000), 0x47);
    schreib(1, 0x0002, 0x0815);
    EXPECT_EQ(k.dram().peek(0x10002), 0x08) << "MMU aus: SN·64K + Offset";
    out(L::P_SCR, L::SCR_BDMEM_AUS);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x0000), 0x0000);
    schreib(0, 0x0000, 0xBEEF);
    EXPECT_EQ(k.dram().peek(0), 0xBE);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0x10, 0x0000), 0xFFFF) << "1 MB: Segment 10H ohne Karte";
}

TEST_F(P8000Karte16_, BytezugriffeSramUndDram_NachA0) {
    Z8kBusCycle c = mem(Z8kStatus::MemData, 0, 0x4011, false, false);
    k.busSchreiben(c, 0x7777);
    c.addr = 0x4010;
    k.busSchreiben(c, 0x3333);
    EXPECT_EQ(lies(Z8kStatus::MemData, 0, 0x4010), 0x3377);
    c.seg = 2; c.addr = 0x0005;
    k.busSchreiben(c, 0x9999);
    EXPECT_EQ(k.dram().peek(0x20005), 0x99);
    EXPECT_EQ(k.dram().peek(0x20004), 0x00);
}

TEST_F(P8000Karte16_, OnBoardKostetEinenWartetakt_HauptspeicherKeinen) {
    cpuBereit();
    code(0x000, {0x8D07, 0x8D07});              // NOP; NOP im SRAM
    k.dram().poke(0x18000, 0x8D); k.dram().poke(0x18001, 0x07);
    k.cpu().fcw = 0x4000;                         // nichtsegmentiert, System
    k.cpu().pcSeg = 0; k.cpu().pc = 0x4000;
    const int onBoard = k.schritt();
    k.cpu().pcSeg = 1; k.cpu().pc = 0x8000;       // Segment 1 = Hauptspeicher 018000H
    const int haupt = k.schritt();
    EXPECT_EQ(onBoard, haupt + 1);
}

TEST_F(P8000Karte16_, RefreshZyklenGehenAnDenDram) {
    Z8kBusCycle c;
    c.st = Z8kStatus::Refresh;
    for (int i = 0; i < 5; ++i) k.busLesen(c);
    EXPECT_EQ(k.dram().refreshZyklen(), 5u);
}

// ─── Interrupt, RETI, Kette ──────────────────────────────────────────────────

TEST_F(P8000Karte16_, Kette_Ctc0VorPio2_QuittungLiefertVektorAmLowByte) {
    out(0xFFA1 + 6, 0x40);           // PIO2-B Vektor 40H
    out(0xFFA9, 0x20);               // CTC0 Vektorbasis 20H
    out(0xFFAB, 0x87); out(0xFFAB, 0x01);   // CTC0 K1 Zeitgeber mit Int, TC 1
    out(0xFFB1, 0x28);               // CTC1 Vektorbasis 28H
    out(0xFFB5, 0x87); out(0xFFB5, 0x01);
    for (int i = 0; i < 300; ++i) k.peripherieBus().markIntDirty(), k.laufeBis(k.zeit());   // nichts
    k.ctc0().clockTick(64); k.ctc1().clockTick(64);
    k.peripherieBus().markIntDirty();
    Z8kBusCycle ack;
    ack.st = Z8kStatus::ViAck;
    EXPECT_EQ(k.busLesen(ack), 0xFF22) << "CTC0 K1 zuerst";
    EXPECT_TRUE(k.ctc0().debugState().ch[1].ius);
    // Vorbestand der Primitive (P5e Abweichung 3): getIEO sperrt nach der Quittung nicht bis
    // RETI — CTC1 meldet sich trotz CTC0 in Bedienung.  Hier nur die Reihenfolge festgehalten.
    EXPECT_EQ(k.busLesen(ack) & 0xFF, 0x2C) << "danach CTC1 K2";
}

TEST_F(P8000Karte16_, RetiNurAlsFolgeEd4dAnFFE1) {
    out(0xFFA9, 0x20);
    out(0xFFAB, 0x87); out(0xFFAB, 0x01);
    k.ctc0().clockTick(64);
    k.peripherieBus().markIntDirty();
    Z8kBusCycle ack;
    ack.st = Z8kStatus::ViAck;
    ASSERT_EQ(k.busLesen(ack) & 0xFF, 0x22);
    ASSERT_TRUE(k.ctc0().debugState().ch[1].ius);
    out(L::P_RETI, 0xED); out(L::P_RETI, 0x00); out(L::P_RETI, 0x4D);
    EXPECT_TRUE(k.ctc0().debugState().ch[1].ius) << "ED 00 4D ist kein RETI";
    out(L::P_RETI, 0x4D);
    EXPECT_TRUE(k.ctc0().debugState().ch[1].ius) << "4D allein auch nicht";
    out(L::P_RETI, 0xED); out(L::P_RETI, 0x4D);
    EXPECT_FALSE(k.ctc0().debugState().ch[1].ius);
}

TEST_F(P8000Karte16_, SioBefehl38AnKanalA_BeendetDieBedienungVonKanalB) {
    // MON16 PTY_INT/SIO_ISR: Kanal B in Bedienung, RETI über WR0 = 38H an Kanal A.
    out(0xFF87, 0x02); out(0xFF87, 0x10);   // WR2 Vektor 10H (Kanal B)
    out(0xFF87, 0x01); out(0xFF87, 0x06);   // WR1: Tx-Int, Status beeinflusst Vektor
    out(0xFF87, 0x05); out(0xFF87, 0x68);
    out(0xFF83, 'x');                        // Sender B
    while (k.anschluss(1).senderHatZeichen()) k.anschluss(1).senderNimm();
    k.peripherieBus().markIntDirty();
    Z8kBusCycle ack;
    ack.st = Z8kStatus::ViAck;
    ASSERT_EQ(k.busLesen(ack) & 0xFF, 0x10) << "Kanal B Tx leer";
    out(0xFF87, 0x28);                       // Reset Tx Int Pending
    out(0xFF85, 0x38);                       // RETI an Kanal A
    out(0xFF8D, 0x02); out(0xFF8F, 0x02); out(0xFF8F, 0x30);   // SIO1-B Vektor 30H
    out(0xFF8F, 0x01); out(0xFF8F, 0x02);
    out(0xFF8F, 0x05); out(0xFF8F, 0x68);
    out(0xFF8B, 'y');
    while (k.anschluss(3).senderHatZeichen()) k.anschluss(3).senderNimm();
    k.peripherieBus().markIntDirty();
    EXPECT_EQ(k.busLesen(ack) & 0xFF, 0x30) << "SIO0 gibt die Kette wieder frei";
}

// ─── Single-Step-Zähler CTC0 K3, Systemuhr CTC1 K2 → K3 ──────────────────────

TEST_F(P8000Karte16_, Ctc0K3ZaehltJedenStapelzyklus_UndNurDiese) {
    out(0xFFAF, 0x47); out(0xFFAF, 10);   // Zähler, TC 10
    for (int i = 0; i < 3; ++i) lies(Z8kStatus::MemStack, 0, 0x4100);
    schreib(0, 0x4100, 0);                // Datenzyklus zählt nicht
    k.busSchreiben(mem(Z8kStatus::MemStack, 0, 0x4100, false), 0);   // Stapel schreiben zählt
    lies(Z8kStatus::MemInstrFirst, 0, 0x0000);
    lies(Z8kStatus::MemData, 0, 0x4100);
    EXPECT_EQ(k.ctc0().getCount(3), 6);
}

TEST_F(P8000Karte16_, Ctc0K3_ZaehltDieStapelzyklenEinesIret) {
    // MON16 p.brk.s: K3 im Zählermodus, IRET liest 4 Wörter vom Stapel (Z8001 segmentiert).
    cpuBereit();
    code(0x000, {0x7B00});                // IRET
    k.cpu().fcw = 0xC000;
    k.cpu().pc = 0x4000;
    k.cpu().R14[1] = 0x0000; k.cpu().R15[1] = 0x4200;
    code(0x200, {0x0000, 0xC000, 0x0000, 0x4000});   // Kennung, FCW, PC-Segment, PC
    out(0xFFAF, 0x47); out(0xFFAF, 10);
    k.schritt();
    EXPECT_EQ(k.ctc0().getCount(3), 10 - 4);
}

TEST_F(P8000Karte16_, Systemuhr_Ctc1K2TaktetK3) {
    out(0xFFB5, 0x47); out(0xFFB5, 2);    // K2: Zähler am BUS BAUD CLK, TC 2
    out(0xFFB7, 0x47); out(0xFFB7, 100);  // K3: Zähler an ZC/TO2
    freigeben();
    k.cpu().setResetLine(true);           // CPU aus dem Spiel, nur die Zeit läuft
    for (int i = 0; i < 3255; ++i) k.peripherieBus().markIntDirty(), k.ctc1().clockTick(1), k.ctc1().clockTick(0);
    // 3255 Takte ≈ 1000 Baudimpulse ⇒ ~500 ZC/TO2 ⇒ K3 mehrfach durchgelaufen
    EXPECT_GT(k.ctc1().teilerTakteQ16(3), 0u) << "K3 kennt seine Eingangsperiode über die Kaskade";
}

// ─── NMI, Identifier, Parität ────────────────────────────────────────────────

TEST_F(P8000Karte16_, NmiIdentifier_Bit0IstDerTastenpegel_Rest0_OberesByteOffen) {
    Z8kBusCycle ack;
    ack.st = Z8kStatus::NmiAck;
    EXPECT_EQ(k.busLesen(ack), 0xFFF0);
    k.nmiTaste();
    EXPECT_EQ(k.busLesen(ack), 0xFFF1);
    freigeben();
    k.laufeBis(k.zeit() + 30);
    EXPECT_EQ(k.busLesen(ack), 0xFFF0) << "nach 24 Takten ist der Impuls vorbei [K2]";
    k.setManualNmi(true);
    EXPECT_EQ(k.busLesen(ack), 0xFFF1);
}

TEST_F(P8000Karte16_, NmiTaste_CpuNimmtNmiMitIdentifierAn) {
    cpuBereit();
    code(0x000, {0x8D07, 0x8D07, 0x8D07});
    psa(Z8000::PSA_NMI, 0x4000, 0x4100);
    k.cpu().fcw = 0x4000; k.cpu().pc = 0x4000; k.cpu().R14[1] = 0; k.cpu().R15[1] = 0x4300;
    k.nmiTaste();
    k.schritt();
    EXPECT_EQ(k.cpu().lastException().kind, Z8kException::Nmi);
    EXPECT_EQ(k.cpu().lastException().id, 0xFFF1);
    EXPECT_EQ(k.cpu().pc, 0x4100);
}

TEST_F(P8000Karte16_, ParitaetsFehler_NurBeiScrBit3_HaeltBisScrBit3Null_LoestNmiAus) {
    k.dram().paritaetsfehlerSetzen(0x8001);
    lies(Z8kStatus::MemData, 0, 0x8000);
    EXPECT_FALSE(k.paritaetsFF()) << "SCR.3 = 0: nicht angenommen";
    out(L::P_SCR, L::SCR_PARITAET);
    lies(Z8kStatus::MemData, 0, 0x8002);
    EXPECT_FALSE(k.paritaetsFF());
    EXPECT_FALSE(k.cpu().nmiPending());
    lies(Z8kStatus::MemData, 0, 0x8000);
    EXPECT_TRUE(k.paritaetsFF());
    EXPECT_TRUE(k.cpu().nmiPending());
    Z8kBusCycle ack;
    ack.st = Z8kStatus::NmiAck;
    EXPECT_EQ(k.busLesen(ack), 0xFFF4);
    lies(Z8kStatus::MemData, 0, 0x8002);
    EXPECT_TRUE(k.paritaetsFF()) << "gespeichert";
    out(L::P_SCR, 0x00);
    EXPECT_FALSE(k.paritaetsFF());
    EXPECT_FALSE(k.dram().ledFehler()) << "CL_PAR− löscht auch die LED der Karte";
    EXPECT_EQ(k.busLesen(ack), 0xFFF0);
}

// ─── Segmenttrap: TRPL/IF1L am Kartenbus, /SEGT an die CPU ───────────────────

TEST_F(P8000Karte16_, Segmenttrap_SchreibenAufNurLeseSegment_TrplIf1lUndQuittung) {
    cpuBereit();
    // Code-MMU (System-Mode wählt sie): Segment 5 nur lesbar, Basis 010000H.
    sout(CODE_MMU, Z8010::CMD_MR, 0xD0);   // ID 0
    sout(0x00FA, Z8010::CMD_MR, 0xD1);     // Data ID 1
    sout(0x00F6, Z8010::CMD_MR, 0xD2);     // Stack ID 2
    sout(CODE_MMU, Z8010::CMD_SAR, 5);
    for (uint8_t b : {uint8_t(0x01), uint8_t(0x00), uint8_t(0xFF), uint8_t(Z8010::ATTR_RD)})
        sout(CODE_MMU, Z8010::CMD_SDR, b);
    out(L::P_SCR, L::SCR_MMU_ON);           // On-Board bleibt an (Code im SRAM)
    code(0x020, {0x8D07, 0x2F20, 0x8D07});  // 4020 NOP; 4022 LD @RR2,R0; NOP
    psa(Z8000::PSA_SEGT, 0xC000, 0x4100);
    k.cpu().fcw = 0xC000; k.cpu().pcSeg = 0; k.cpu().pc = 0x4020;
    k.cpu().R14[1] = 0; k.cpu().R15[1] = 0x4300;
    k.cpu().Rg[0] = 0xAAAA; k.cpu().Rg[2] = 0x0500; k.cpu().Rg[3] = 0x1234;
    k.schritt();
    k.schritt();
    EXPECT_TRUE(k.cpu().segtLine()) << "/SEGT liegt an der CPU";
    EXPECT_EQ(in(L::P_TRPL), 0x34) << "Offset-Low des verletzenden Zyklus";
    EXPECT_EQ(in(L::P_IF1L), 0x22) << "erstes Wort DES verletzenden Befehls";
    EXPECT_EQ(k.dram().peek(0x10034), 0x00) << "SUP: nicht geschrieben";
    k.schritt();
    EXPECT_EQ(k.cpu().lastException().kind, Z8kException::SegmentTrap);
    EXPECT_EQ((k.cpu().lastException().id >> 8) & 7, 1) << "MON16 SEGMENT_TRAP: 1 = Code-MMU";
    EXPECT_FALSE(k.cpu().segtLine()) << "Quittung nimmt /SEGT zurück";
    EXPECT_EQ(k.cpu().pc, 0x4100);
}

// ─── Baudtakt ────────────────────────────────────────────────────────────────

TEST_F(P8000Karte16_, Tty5Mit57_02UndX32_Gibt9600Baud) {
    out(0xFFAB, 0x57); out(0xFFAB, 0x02);   // MON16 INIT_CTC0_1
    for (uint8_t b : {0x18, 0x14, 0x8C, 0x03, 0xC1, 0x05, 0x68}) out(0xFF87, b);   // ITAB_SIO0_B
    const auto f = k.anschluss(1).format();
    // 1 229 000 / 2 / 2 / 32 = 9 601,6 Bd; 8N2 = 11 Bit ⇒ 4 582 Takte je Zeichen
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    EXPECT_NEAR(double(f.zeichen_takte), 4582.0, 2.0);
}

// ─── Save-State ──────────────────────────────────────────────────────────────

TEST_F(P8000Karte16_, Rundreise_IstBitgleich_UndStelltSegtWieder) {
    freigeben();
    ASSERT_TRUE(laufeBisText(1, "Press NMI", 20'000'000));
    k.nmiTaste();
    k.laufeBis(k.zeit() + 3'000'000);
    std::vector<uint8_t> a;
    k.serialize(a);
    P8000Karte16 b;
    const uint8_t* p = a.data();
    ASSERT_TRUE(b.deserialize(p, a.data() + a.size()));
    EXPECT_EQ(p, a.data() + a.size());
    for (int n = 0; n < 3; ++n) {
        k.laufeBis(k.zeit() + 1'000'000);
        b.laufeBis(b.zeit() + 1'000'000);
        while (k.anschluss(1).senderHatZeichen()) k.anschluss(1).senderNimm();
        while (b.anschluss(1).senderHatZeichen()) b.anschluss(1).senderNimm();
    }
    std::vector<uint8_t> sa, sb;
    k.serialize(sa);
    b.serialize(sb);
    EXPECT_TRUE(sa == sb) << "Stand nach 3 Mio. Takten weicht ab";

    // /SEGT steht nicht im Z8kRunState — die MMU-Logik stellt ihn wieder her.
    k.mmu().mmu(L::Mmu::Code);   // nur Zugriff
    P8000Karte16 c;
    sout(CODE_MMU, Z8010::CMD_MR, 0xD0);
    sout(CODE_MMU, Z8010::CMD_SAR, 7);
    for (uint8_t x : {uint8_t(0x07), uint8_t(0x00), uint8_t(0x00), uint8_t(Z8010::ATTR_RD)})
        sout(CODE_MMU, Z8010::CMD_SDR, x);
    out(L::P_SCR, L::SCR_MMU_ON);
    schreib(7, 0x0010, 0);
    ASSERT_TRUE(k.cpu().segtLine());
    std::vector<uint8_t> s2;
    k.serialize(s2);
    p = s2.data();
    ASSERT_TRUE(c.deserialize(p, s2.data() + s2.size()));
    EXPECT_TRUE(c.cpu().segtLine());
}
