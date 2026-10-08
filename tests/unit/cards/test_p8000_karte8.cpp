/**
 * @file test_p8000_karte8.cpp
 * @brief 8-Bit-Rechnerkarte des P8000 ohne Floppy: E/A-Dekoder, Kette, Reset/RESI, NMI-Weiche,
 *        Baudtakt und das Rig mit MON8 3.1 (doc/p8000/schaltplan_8bit.md, doc/design/25_p8000.md
 *        §10.11 AP P5e).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/karte8.h"
#include "core/cards/p8000/rom_mon8.h"
#include <array>
#include <string>
#include <vector>

namespace {

struct P8000Karte8_ : ::testing::Test {
    K1520Bus     bus;
    P8000Karte8  k{bus};
    std::array<std::string, 4> tty;   ///< was die Kanäle gesendet haben (Wandler-Stub: sofort)

    void SetUp() override { k.powerOn(); }

    /// Wandler-Stub: gesendete Zeichen aller vier Kanäle einsammeln.
    void abholen() {
        for (int i = 0; i < 4; ++i)
            while (k.anschluss(i).senderHatZeichen())
                tty[i] += static_cast<char>(k.anschluss(i).senderNimm());
    }
    /// Bis @p max Takte laufen; Abbruch, sobald @p text auf tty @p kanal erscheint.
    bool laufeBisText(int kanal, const std::string& text, long max) {
        long t = 0;
        while (t < max) {
            const int n = k.schritt();
            if (n == 0) return false;
            k.takt(n);
            t += n;
            abholen();
            if (tty[kanal].find(text) != std::string::npos) return true;
        }
        return false;
    }
    void out(uint8_t port, uint8_t d) { bus.ioWrite(port, d); }
    uint8_t in(uint8_t port) { return bus.ioRead(port); }
};

}  // namespace

// ─── Dekodertabelle (P2a §2) ─────────────────────────────────────────────────

TEST_F(P8000Karte8_, CtcTore_08bis0B_CTC0_und_2CbisF_CTC1_JeAdresseEinKanal) {
    for (int c = 0; c < 4; ++c) {
        out(uint8_t(0x08 + c), 0x07);   // Steuerwort: Zeitgeber, Zeitkonstante folgt
        EXPECT_EQ(k.ctc0().debugState().ch[c].control, 0x07) << c;
        EXPECT_NE(k.ctc1().debugState().ch[c].control, 0x07) << c;
        out(uint8_t(0x2C + c), 0x07);
        EXPECT_EQ(k.ctc1().debugState().ch[c].control, 0x07) << c;
    }
}

TEST_F(P8000Karte8_, PioTore_AdressbitA0_CD_A1_BA) {
    // Moduswort 0FH (Ausgabe) an die Steuertore: A = Basis+1, B = Basis+3
    struct P { uint8_t basis; Z80PIO* p; };
    for (P x : {P{0x0C, &k.pio0()}, P{0x18, &k.pio1()}, P{0x1C, &k.pio2()}}) {
        EXPECT_EQ(x.p->debugState().port[0].mode, 1);
        out(uint8_t(x.basis + 1), 0x0F);
        EXPECT_EQ(x.p->debugState().port[0].mode, 0) << int(x.basis);
        EXPECT_EQ(x.p->debugState().port[1].mode, 1) << int(x.basis);
        out(uint8_t(x.basis + 3), 0x0F);
        EXPECT_EQ(x.p->debugState().port[1].mode, 0) << int(x.basis);
        out(x.basis, 0x5A);                       // Datentor A
        EXPECT_EQ(x.p->portARead(), 0x5A);
        out(uint8_t(x.basis + 2), 0xA5);          // Datentor B
        EXPECT_EQ(x.p->portBRead(), 0xA5);
    }
}

TEST_F(P8000Karte8_, SioTore_24H_SIO0_28H_SIO1_A0_CD_A1_BA) {
    struct S { uint8_t basis; Z80SIO* s; };
    for (S x : {S{0x24, &k.sio0()}, S{0x28, &k.sio1()}}) {
        out(uint8_t(x.basis + 1), 0x03); out(uint8_t(x.basis + 1), 0xC1);   // WR3 Kanal A
        out(uint8_t(x.basis + 3), 0x03); out(uint8_t(x.basis + 3), 0x41);   // WR3 Kanal B
        EXPECT_EQ(x.s->channelA().wr[3], 0xC1) << int(x.basis);
        EXPECT_EQ(x.s->channelB().wr[3], 0x41) << int(x.basis);
    }
}

TEST_F(P8000Karte8_, Latches_10H_14H_VierfachGespiegelt_NurSchreibend) {
    std::vector<std::pair<int, uint8_t>> ereignisse;
    k.setLatchRueckruf([&](int l, uint8_t w) { ereignisse.push_back({l, w}); });
    for (int i = 0; i < 4; ++i) out(uint8_t(0x10 + i), uint8_t(0x10 + i));
    for (int i = 0; i < 4; ++i) out(uint8_t(0x14 + i), uint8_t(0x20 + i));
    ASSERT_EQ(ereignisse.size(), 8u);
    for (int i = 0; i < 4; ++i) { EXPECT_EQ(ereignisse[i].first, 0); EXPECT_EQ(ereignisse[4 + i].first, 1); }
    EXPECT_EQ(k.latch1(), 0x13);
    EXPECT_EQ(k.latch2(), 0x23);
    // nicht lesbar (DS8282 nur Ausgänge), auch nicht im Spiegel
    for (int i = 0; i < 8; ++i) EXPECT_EQ(in(uint8_t(0x10 + i)), 0xFF) << i;
}

TEST_F(P8000Karte8_, LatchStartwertAusConfig) {
    K1520Bus b2;
    P8000Karte8::Config c;
    c.latch_start = 0xA5;
    P8000Karte8 k2(b2, c);
    k2.powerOn();
    EXPECT_EQ(k2.latch1(), 0xA5);
    EXPECT_EQ(k2.latch2(), 0xA5);
    k2.reset();   // /RES setzt die DS8282 nicht zurück (kein Reset-Pin)
    EXPECT_EQ(k2.latch1(), 0xA5);
}

TEST_F(P8000Karte8_, FdcTore_20H_bis23H_Haken_22H_23H_Spiegeln) {
    std::vector<std::pair<int, uint8_t>> w;
    k.fdcLesen = [](uint8_t r) -> uint8_t { return uint8_t(0x80 + r); };
    k.fdcSchreiben = [&](uint8_t r, uint8_t v) { w.push_back({r, v}); };
    for (int i = 0; i < 4; ++i) { EXPECT_EQ(in(uint8_t(0x20 + i)), 0x80 + (i & 1)); out(uint8_t(0x20 + i), uint8_t(i)); }
    ASSERT_EQ(w.size(), 4u);
    EXPECT_EQ(w[2].first, 0);
    EXPECT_EQ(w[3].first, 1);
}

TEST_F(P8000Karte8_, OhneFdcUndLuecken_LesenFF_SchreibenVerschluckt) {
    // 20H–23H ohne Haken, 30H–3FH (RES1..3 nur an X13, DMA-Platzhalter), 40H–FFH (E3 = NOR(A6,A7))
    for (int p : {0x20, 0x21, 0x22, 0x23, 0x30, 0x34, 0x38, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F})
        EXPECT_EQ(in(uint8_t(p)), 0xFF) << p;
    for (int p = 0x40; p <= 0xFF; ++p) {
        out(uint8_t(p), 0x00);
        EXPECT_EQ(in(uint8_t(p)), 0xFF) << p;
    }
    EXPECT_EQ(bus.ioOwner(0x40), nullptr);
    EXPECT_EQ(bus.ioOwner(0xFF), nullptr);
    out(0x3C, 0x12);   // DMA-Platzhalter: ohne Wirkung
    EXPECT_EQ(in(0x3C), 0xFF);
}

TEST_F(P8000Karte8_, Speichertore_00bis07_BleibenBeiSpeicher8) {
    EXPECT_FALSE(k.speicher().rffGesetzt());
    in(0x05);
    EXPECT_TRUE(k.speicher().rffGesetzt());
    out(0x01, 0x03);   // ADP: Zelle aus A12–A15 der E/A-Adresse = 0
    EXPECT_EQ(k.speicher().adp(0), 0x03);
}

TEST_F(P8000Karte8_, E_A_AdresseA8bisA15_WirdNichtAusgewertet) {
    std::vector<uint8_t> w;
    k.setLatchRueckruf([&](int, uint8_t v) { w.push_back(v); });
    bus.ioWrite(0xAB10, 0x77);   // OUT (C),r mit B = ABH
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(k.latch1(), 0x77);
}

// ─── Interruptkette (P2a §3) ─────────────────────────────────────────────────

TEST_F(P8000Karte8_, KetteDmaPio2Ctc0Sio0Sio1Pio0Pio1Ctc1) {
    // jeder Baustein mit eigenem Vektor und einer anstehenden Anforderung
    auto pioInt = [&](uint8_t basis, Z80PIO& p, uint8_t vec) {
        out(uint8_t(basis + 1), vec);                  // Vektor
        out(uint8_t(basis + 1), 0x0F | (1 << 6));      // Modus 1 (Eingabe)
        out(uint8_t(basis + 1), 0x83);                 // Interruptfreigabe
        p.portAWrite(0x42);                            // Daten bereit ⇒ Anforderung
    };
    pioInt(0x1C, k.pio2(), 0x10);
    pioInt(0x0C, k.pio0(), 0x50);
    pioInt(0x18, k.pio1(), 0x60);

    auto ctcInt = [&](uint8_t basis, Z80CTC& c, uint8_t vec) {
        out(basis, vec);                               // Vektor (D0 = 0)
        out(basis, 0x85);                              // Zeitgeber, Interrupt, TC folgt, Vorteiler 16
        out(basis, 0x01);
        k.takt(0);
        c.clockTick(64);
    };
    ctcInt(0x08, k.ctc0(), 0x20);
    ctcInt(0x2C, k.ctc1(), 0x70);

    // SIO: Empfangsinterrupt, Vektor aus WR2 (Kanal B)
    auto sioInt = [&](uint8_t basis, Z80SIO& s, uint8_t vec) {
        out(uint8_t(basis + 3), 0x02); out(uint8_t(basis + 3), vec);        // WR2
        out(uint8_t(basis + 1), 0x01); out(uint8_t(basis + 1), 0x18);       // WR1 A: Rx-Int alle
        out(uint8_t(basis + 1), 0x03); out(uint8_t(basis + 1), 0xC1);       // WR3: Rx an
        s.channelA().rxByte(0x55);
    };
    sioInt(0x24, k.sio0(), 0x30);
    sioInt(0x28, k.sio1(), 0x40);

    std::vector<int> folge;
    for (int i = 0; i < 7; ++i) {
        bus.updateInterruptChain();
        ASSERT_TRUE(bus.isINT()) << i;
        folge.push_back(bus.interruptAcknowledge());
        bus.signalRETI();
    }
    // Vektor des SIO ohne „Status beeinflusst Vektor" = WR2-Basis
    EXPECT_EQ(folge, (std::vector<int>{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}));
}

/// Wächter P22 (Merkposten p8000 Nr. 40): nach EI nimmt der U880 erst nach dem FOLGENDEN Befehl
/// einen Interrupt an.  `EI; RETI` am Ende jeder ISR der Koppelsoftware — ohne die Sperre fiel ein
/// anstehender Interrupt zwischen beide, RETI lief nie, und unter Last wuchs der Stapel in den Code.
TEST_F(P8000Karte8_, NachEiErstNachDemFolgendenBefehlEinInterrupt) {
    // `EI; RETI` (FB ED 4D) aus dem EPROM (nach Reset überall eingeblendet).
    uint16_t ei = 0;
    for (uint32_t a = 0; a + 2 < 0x2000 && !ei; ++a)
        if (k.speicher().peek(uint16_t(a)) == 0xFB && k.speicher().peek(uint16_t(a + 1)) == 0xED &&
            k.speicher().peek(uint16_t(a + 2)) == 0x4D)
            ei = uint16_t(a);
    ASSERT_NE(ei, 0);
    // CTC0 Kanal 0 fordert an (Vektor 20H).
    out(0x08, 0x20);
    out(0x08, 0x85);
    out(0x08, 0x01);
    k.takt(0);
    k.ctc0().clockTick(64);
    bus.updateInterruptChain();
    ASSERT_TRUE(bus.isINT());
    Z80& c = k.cpu();
    c.IM = 2;
    c.I = 0x00;
    c.IFF1 = c.IFF2 = false;
    c.PC = ei;
    c.SP = 0xF000;
    ASSERT_GT(k.schritt(), 0);                     // EI
    EXPECT_TRUE(c.IFF1);
    EXPECT_EQ(c.PC, uint16_t(ei + 1));
    ASSERT_GT(k.schritt(), 0);                     // RETI läuft — noch keine Annahme
    EXPECT_NE(c.PC, uint16_t(ei + 1)) << "Interrupt zwischen EI und RETI angenommen";
    EXPECT_EQ(c.SP, 0xF002) << "RETI hat den Stapel nicht abgebaut";
    EXPECT_TRUE(c.IFF1);
    bus.updateInterruptChain();
    EXPECT_TRUE(bus.isINT()) << "Anforderung steht noch";
    ASSERT_GT(k.schritt(), 0);                     // jetzt angenommen
    EXPECT_FALSE(c.IFF1);
}

// ─── RESET / RESI / NMI (P2a §4) ─────────────────────────────────────────────

TEST_F(P8000Karte8_, ResiPowerOnEinsTasteNull_AnPio2A7) {
    auto a7 = [&] {
        out(0x1D, 0xCF); out(0x1D, 0x80);   // Bitbetrieb, nur A7 Eingang (wie MON8)
        return (in(0x1C) & 0x80) != 0;
    };
    EXPECT_TRUE(k.resi());
    EXPECT_TRUE(a7());
    k.reset();
    EXPECT_FALSE(k.resi());
    EXPECT_FALSE(a7());      // PIO nach /PM1-Reset wieder Eingabemodus; Neuprogrammierung ⇒ 0
    k.powerOn();
    EXPECT_TRUE(k.resi());
    EXPECT_TRUE(a7());
}

TEST_F(P8000Karte8_, ResetSetztRffBausteineUndCpuZurueck_LatchesBleiben) {
    in(0x04);                         // RFF Q = 1
    out(0x0F, 0x0F);                  // PIO0 B Ausgabe
    out(0x08, 0x07);
    out(0x10, 0x99);
    k.cpu().PC = 0x1234;
    k.reset();
    EXPECT_FALSE(k.speicher().rffGesetzt());
    EXPECT_EQ(k.cpu().PC, 0x0000);
    EXPECT_EQ(k.pio0().debugState().port[1].mode, 1);
    EXPECT_NE(k.ctc0().debugState().ch[0].control, 0x07);
    EXPECT_EQ(k.latch1(), 0x99);      // DS8282 ohne Reset-Pin
}

TEST_F(P8000Karte8_, NmiWeicheFolgtPio0B7) {
    int an16 = 0;
    k.setNmiU8000Rueckruf([&] { ++an16; });
    // nach Reset: B7 Eingang ⇒ Pull-up ⇒ 1 ⇒ NMI an den U880
    EXPECT_TRUE(k.b7eff());
    k.nmiTaste();
    EXPECT_TRUE(bus.isNMI());
    EXPECT_EQ(an16, 0);
    bus.clearNMI();
    // B7 als Ausgang = 0: Taste geht an die 16-Bit-Karte, der U880 bekommt keinen NMI
    out(0x0F, 0xCF); out(0x0F, 0x7F);   // Bitbetrieb, B0–B6 Eingang, B7 Ausgang
    out(0x0E, 0x00);
    EXPECT_FALSE(k.b7eff());
    k.nmiTaste();
    EXPECT_FALSE(bus.isNMI());
    EXPECT_EQ(an16, 1);
    // B7 = 1 ausgegeben ⇒ wieder U880
    out(0x0E, 0x80);
    EXPECT_TRUE(k.b7eff());
    k.nmiTaste();
    EXPECT_TRUE(bus.isNMI());
    EXPECT_EQ(an16, 1);
}

TEST_F(P8000Karte8_, NmiTasteLaeuftBisInDenU880) {
    // NMI-Annahme: Einsprung 0066H (EPROM ist nach Reset überall), dort steht MON8 3.1 `JP 4000H`
    k.nmiTaste();
    EXPECT_GT(k.schritt(), 0);
    EXPECT_FALSE(bus.isNMI());
    EXPECT_EQ(k.cpu().PC, 0x4000);
    EXPECT_FALSE(k.cpu().IFF1);
}

// ─── Baudtakt (P2a §5) ───────────────────────────────────────────────────────

TEST_F(P8000Karte8_, BaudTty1_CTC1K1_Teilt1229000DurchZkUndZwei) {
    // CTC1 K1: Zähler (D6), Zeitkonstante folgt (D2), Steuerwort (D0); ZK = 1
    out(0x2D, 0x47); out(0x2D, 0x01);
    // SIO0 B: ×32, 1 Stoppbit, 8 Bit senden
    out(0x27, 0x04); out(0x27, 0x84);
    out(0x27, 0x05); out(0x27, 0x68);
    const auto f = k.anschluss(1).format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 19200u);
    // 10 Bit × 32 × 2 × (4 000 000 / 1 229 000) = 2082,9 Takte
    EXPECT_NEAR(double(f.zeichen_takte), 2082.9, 1.0);
    // ZK = 2 ⇒ 9600
    out(0x2D, 0x47); out(0x2D, 0x02);
    EXPECT_EQ(k.anschluss(1).format().baud_nenn, 9600u);
}

TEST_F(P8000Karte8_, BaudTty3_CTC0K0_Tty0_2_CTC1K0_K2) {
    out(0x08, 0x47); out(0x08, 0x01);       // CTC0 K0 ⇒ tty3
    out(0x2C, 0x47); out(0x2C, 0x01);       // CTC1 K0 ⇒ tty0
    out(0x2E, 0x47); out(0x2E, 0x01);       // CTC1 K2 ⇒ tty2
    for (uint8_t b : {uint8_t(0x25), uint8_t(0x27), uint8_t(0x29), uint8_t(0x2B)}) {
        out(b, 0x04); out(b, 0x84); out(b, 0x05); out(b, 0x68);
    }
    for (int t : {0, 2, 3}) EXPECT_EQ(k.anschluss(t).format().baud_nenn, 19200u) << t;
    EXPECT_FALSE(k.anschluss(1).format().gueltig);   // CTC1 K1 nicht programmiert
}

TEST_F(P8000Karte8_, ZaehlerAm1229MHzEingangLaeuftMitPhi) {
    // Zähler-Kanal, ZK = 100 ⇒ ein ZC/TO je 100 Eingangsimpulse (je 3,2547 Takte)
    int ereignisse = 0;
    k.ctc1().setZCTOCallback([&](int c, bool lvl) { if (c == 0 && lvl) ++ereignisse; });
    out(0x2C, 0x47); out(0x2C, 100);
    for (long t = 0; t < 325'468; ++t) k.takt(1);   // 100 000 Impulse = 1 000 ZC/TO-Impulse
    EXPECT_NEAR(ereignisse, 1000, 1);
}

// ─── Konfiguration / Rig mit MON8 3.1 ────────────────────────────────────────

TEST_F(P8000Karte8_, EpromNachResetUeberall_BeginntMitDemMonitor) {
    EXPECT_EQ(bus.memRead(0x0000), P8K_MON8_3_1[0]);
    EXPECT_EQ(bus.memRead(0x2000), P8K_MON8_3_1[0]);
}

TEST_F(P8000Karte8_, Mon8_3_1_MeldetSichAufTty1MitDemHardwaretestBanner) {
    const bool ok = laufeBisText(1, "P8000 Hardwaretest U880 - Version 3.1", 60'000'000);
    EXPECT_TRUE(ok) << "tty1: [" << tty[1] << "]  PC=" << std::hex << k.cpu().PC
                    << " tty0=[" << tty[0] << "] tty2=[" << tty[2] << "] tty3=[" << tty[3] << "]";
    // die Konsole ist tty1; die übrigen Kanäle bleiben stumm
    EXPECT_TRUE(tty[0].empty() && tty[2].empty() && tty[3].empty());
}

TEST_F(P8000Karte8_, Mon8_3_1_HardwaretestLaeuftBisZumMonitorPromptNurFloppyUndDmaFehlen) {
    ASSERT_TRUE(laufeBisText(1, "Press RETURN", 120'000'000)) << tty[1];
    // Ohne FDC (P5f) und DMA (P5f) melden 25/26/27 Fehler; CTC, SIO, PIO, Speicher und Latches
    // sind in Ordnung (Fehler 16 war die CTC nach Software-Reset, s. test_ctc).
    std::string rest = tty[1];
    for (size_t p = rest.find("ERROR "); p != std::string::npos; p = rest.find("ERROR ", p + 1)) {
        const std::string nr = rest.substr(p + 6, 2);
        EXPECT_TRUE(nr == "25" || nr == "26" || nr == "27") << "ERROR " << nr;
    }
}
