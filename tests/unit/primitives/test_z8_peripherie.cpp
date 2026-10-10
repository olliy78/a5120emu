/**
 * @file test_z8_peripherie.cpp
 * @brief Z8-Primitive: Reset, Interrupts (IRQ/IMR/IPR, Flanken, Rangfolge), Zähler T0/T1
 *        (Vorteiler, Einzel-/Dauerbetrieb, TIN-Arten, TOUT), UART (Rahmen, Zeit, Parität,
 *        Puffer), Ports 0–3 (Arten, offener Drain, Handshake), externer Bus, Save-State —
 *        gegen Zilog UM0016.  Abdeckung: doc/p8000/z8_abdeckung.md.
 */
#include "tests/unit/primitives/z8_rig.h"

#include <algorithm>
#include <bitset>

using z8test::Rig;
using z8test::SerielleQuelle;

namespace {

/// LD reg,#v als eigener Befehl (prog-Bus 0F00H…), danach läuft die CPU in NOPs (FFH).
int ld(Rig& rg, uint8_t r, uint8_t v) { return rg.eins({0xE6, r, v}, 0x0F00); }

/// NOPs, bis @p cond oder @p max Takte; Rückgabe: Zeitpunkt (takte) beim Eintreten.
template <class F> uint64_t bis(Rig& rg, F cond, uint64_t max = 2000000) {
    const uint64_t t0 = rg.cpu.takte;
    while (!cond() && rg.cpu.takte - t0 < max) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_TRUE(cond()) << "Bedingung nicht erreicht";
    return rg.cpu.takte;
}

/// Interrupts scharf: EI, Vektoren IRQn → 0200H + 10H·n (dort IRET), Stapel 70H.
void irqBereit(Rig& rg) {
    for (int n = 0; n < 6; ++n) {
        rg.prog[size_t(2 * n)] = 0x02; rg.prog[size_t(2 * n + 1)] = uint8_t(n << 4);
        rg.prog[size_t(0x200 + 16 * n)] = 0xBF;
    }
    rg.cpu.sp = 0x70;
    rg.eins({0x9F}, 0x0F10);       // EI
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Reset
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Reset, SteuerregisterWieTable12AllzweckregisterBleiben) {
    Rig rg;
    rg.cpu.reg[0x40] = 0x5A;
    rg.cpu.rp = 0x70; rg.cpu.imr = 0xFF; rg.cpu.pc = 0x1234;
    rg.cpu.setResetLine(true);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(rg.cpu.step(), 1);       // gehalten: nichts läuft
    EXPECT_EQ(rg.cpu.pc, 0x1234);
    rg.cpu.setResetLine(false);
    EXPECT_EQ(rg.cpu.step(), 5);
    EXPECT_EQ(rg.cpu.pc, 0x000C);
    EXPECT_EQ(rg.cpu.rp, 0);
    EXPECT_EQ(rg.cpu.imr, 0x7F);                                    // nur Bit 7 gelöscht
    EXPECT_EQ(rg.cpu.p01m(), 0x4D);
    EXPECT_EQ(rg.cpu.p2m(), 0xFF);
    EXPECT_EQ(rg.cpu.p3m(), 0x00);
    EXPECT_EQ(rg.cpu.tmr(), 0x00);
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0x00);
    EXPECT_FALSE(rg.cpu.irqFreigegeben());
    EXPECT_EQ(rg.cpu.zaehler(1).pre & 3, 0);                        // T1 extern, Einzeldurchlauf
    EXPECT_EQ(rg.cpu.reg[0x40], 0x5A);
    EXPECT_EQ(rg.cpu.portPegel(3) & 0xF0, 0xF0);                    // P34–P37 = 1
    EXPECT_TRUE(rg.cpu.stapelIntern());
}

TEST(Z8Reset, FassungenProgrammbusgrenze) {
    EXPECT_EQ(Z8Config::ub8840().programmbus, 0x1000);
    EXPECT_EQ(Z8Config::ub8820().programmbus, 0x0800);
    EXPECT_EQ(Z8Config::z8681().programmbus, 0);
    Rig rg(Z8Config::ub8820()); rg.boot();
    rg.extProg[0x0800] = 0xFF;
    rg.bus.clear();
    rg.cpu.pc = 0x07FF; rg.cpu.step(); rg.cpu.step();
    ASSERT_EQ(rg.bus.size(), 1u);                                   // 07FF intern, 0800 extern
    EXPECT_EQ(rg.bus[0].c.adresse & 0x00FF, 0x00);
    EXPECT_EQ(rg.bus[0].c.art, Z8Zugriff::Holen);
}

// ═════════════════════════════════════════════════════════════════════════════
// Interrupts
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Irq, IrqRegisterBisZumErstenEiGesperrt) {
    Rig rg; rg.boot();
    ld(rg, 0xFA, 0x3F);
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
    rg.cpu.setPin(3, 2, false);                     // P32 ↓ → IRQ0, aber gesperrt
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
    ld(rg, 0xFB, 0x80);                             // IMR.7 setzen reicht nicht (UM0016 S. 102)
    rg.cpu.setPin(3, 2, true); rg.cpu.setPin(3, 2, false);
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
    rg.eins({0x9F}, 0x0F10);                        // EI
    rg.eins({0x8F}, 0x0F11);                        // DI (abgefragter Betrieb)
    rg.cpu.setPin(3, 2, true); rg.cpu.setPin(3, 2, false);
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0x01);
    ld(rg, 0xFA, 0xFF);                             // Bit 6/7 reserviert: lesen 0
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0x3F);
}

TEST(Z8Irq, FlankenP30bisP33NurFallend) {
    const int irqVonBit[4] = {3, 2, 0, 1};          // P30→IRQ3, P31→IRQ2, P32→IRQ0, P33→IRQ1
    for (int b = 0; b < 4; ++b) {
        Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
        rg.cpu.setPin(3, b, true);
        EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
        rg.cpu.setPin(3, b, false);
        EXPECT_EQ(rg.cpu.regSicht(0xFA), 1 << irqVonBit[b]) << "P3" << b;
        ld(rg, 0xFA, 0);
        rg.cpu.setPin(3, b, true);                  // steigend: nichts
        EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
        EXPECT_EQ(rg.cpu.portPegel(3) & 0x0F, 0x0F);
    }
}

TEST(Z8Irq, AnnahmeStapelrahmenVektorTakte) {
    Rig rg; rg.boot(); irqBereit(rg);
    ld(rg, 0xFB, 0x84);                             // IMR: global + IRQ2
    rg.cpu.flags = 0xA5;
    rg.cpu.pc = 0x0345;
    ld(rg, 0xFA, 0x04);                             // Software-Interrupt (UM0016 S. 104)
    rg.cpu.pc = 0x0345;
    std::vector<int> gesehen;
    rg.cpu.onInterrupt = [&](int n, uint16_t, uint16_t) { gesehen.push_back(n); };
    EXPECT_EQ(rg.cpu.step(), 24);
    EXPECT_EQ(rg.cpu.pc, 0x0220);
    EXPECT_EQ(rg.cpu.imr, 0x04);                    // IMR.7 gelöscht
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0);
    EXPECT_EQ(rg.cpu.sp, 0x6D);
    EXPECT_EQ(rg.r(0x6D), 0xA5);                    // FLAGS
    EXPECT_EQ(rg.r(0x6E), 0x03);                    // PCH
    EXPECT_EQ(rg.r(0x6F), 0x45);                    // PCL
    EXPECT_EQ(gesehen, std::vector<int>{2});
    rg.cpu.flags = 0;
    rg.cpu.step();                                  // IRET
    EXPECT_EQ(rg.cpu.pc, 0x0345);
    EXPECT_EQ(rg.cpu.flags, 0xA5);
    EXPECT_EQ(rg.cpu.imr, 0x84);
}

TEST(Z8Irq, MaskeUndGlobalesFreigabebit) {
    Rig rg; rg.boot(); irqBereit(rg);
    ld(rg, 0xFB, 0x80 | 0x3B);                      // alle ausser IRQ2
    ld(rg, 0xFA, 0x04);
    rg.cpu.pc = 0x0800; rg.cpu.step();
    EXPECT_EQ(rg.cpu.pc, 0x0801);                   // maskiert
    ld(rg, 0xFB, 0x04);                             // Bit 7 aus
    rg.cpu.pc = 0x0800; rg.cpu.step();
    EXPECT_EQ(rg.cpu.pc, 0x0801);
    EXPECT_EQ(rg.cpu.regSicht(0xFA), 0x04);         // Anforderung bleibt stehen (abgefragt)
}

namespace {
/// UM0016 Table 19/20: Rangfolge als Liste.
std::vector<int> refRang(uint8_t ipr) {
    std::vector<int> A = {5, 3}, B = {2, 0}, C = {1, 4};
    if (ipr & 0x20) A = {3, 5};
    if (ipr & 0x04) B = {0, 2};
    if (ipr & 0x02) C = {4, 1};
    const int g = ((ipr >> 4) & 1) << 2 | ((ipr >> 3) & 1) << 1 | (ipr & 1);
    std::vector<std::vector<int>> o;
    switch (g) {
    case 1: o = {C, A, B}; break;
    case 2: o = {A, B, C}; break;
    case 3: o = {A, C, B}; break;
    case 4: o = {B, C, A}; break;
    case 5: o = {C, B, A}; break;
    case 6: o = {B, A, C}; break;
    case 0: o = {C, A, B}; break;   // reserviert, Annahme [Z8-I2]
    default: o = {B, A, C}; break;  // reserviert, Annahme [Z8-I2]
    }
    std::vector<int> r;
    for (auto& v : o) r.insert(r.end(), v.begin(), v.end());
    return r;
}
}  // namespace

TEST(Z8Irq, RangfolgeAlleIprWerteAlleAnforderungsmengen) {
    Rig rg; rg.boot(); irqBereit(rg);
    int faelle = 0;
    for (int ipr = 0; ipr < 64; ++ipr) {
        const std::vector<int> rang = refRang(uint8_t(ipr));
        for (int p = 1; p < 64; ++p) {
            ld(rg, 0xF9, uint8_t(ipr));
            ld(rg, 0xFA, 0x00);                       // Reste der vorigen Runde weg (IMR.7 = 0)
            ld(rg, 0xFB, 0xBF);
            ld(rg, 0xFA, uint8_t(p));
            rg.cpu.sp = 0x70;
            rg.cpu.pc = 0x0800;
            rg.cpu.step();
            int erwartet = -1;
            for (int n : rang) if (p & (1 << n)) { erwartet = n; break; }
            ASSERT_EQ(rg.cpu.pc, 0x0200 + 16 * erwartet) << "IPR=" << ipr << " IRQ=" << p;
            ASSERT_EQ(rg.cpu.regSicht(0xFA), p & ~(1 << erwartet));
            ++faelle;
        }
    }
    EXPECT_EQ(faelle, 64 * 63);
}

TEST(Z8Irq, ScheinholenUndVektorAmExternenBus) {
    Rig rg(Z8Config::z8681());                      // ROM-los: Vektoren vom Bus
    rg.boot();
    ld(rg, 0xF8, 0xB6);
    rg.extProg[0x0008] = 0x12; rg.extProg[0x0009] = 0x34;
    rg.extProg[0x0F10] = 0x9F;
    rg.cpu.pc = 0x0F10; rg.cpu.step();              // EI
    rg.cpu.sp = 0x70;
    rg.cpu.imr = 0x90; rg.cpu.irqReg = 0x10;
    rg.cpu.pc = 0x4444;
    rg.bus.clear();
    rg.cpu.step();
    EXPECT_EQ(rg.cpu.pc, 0x1234);
    ASSERT_EQ(rg.bus.size(), 3u);
    EXPECT_EQ(rg.bus[0].c.art, Z8Zugriff::Scheinholen);
    EXPECT_EQ(rg.bus[0].c.adresse, 0x4444);
    EXPECT_EQ(rg.bus[1].c.art, Z8Zugriff::Vektor);
    EXPECT_EQ(rg.bus[1].c.adresse, 0x0008);
    EXPECT_EQ(rg.bus[2].c.adresse, 0x0009);
}

// ═════════════════════════════════════════════════════════════════════════════
// Zähler
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Zaehler, T0EinzeldurchlaufZeitIstVierMalPmalT) {
    for (int p : {1, 3, 7, 64}) for (int t : {1, 2, 5, 256}) {
        Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
        ld(rg, 0xF5, uint8_t((p & 63) << 2));        // PRE0, Einzeldurchlauf
        ld(rg, 0xF4, uint8_t(t & 255));
        ld(rg, 0xF1, 0x03);                          // laden + zählen
        const uint64_t start = rg.cpu.takte;         // Schreiben wirkt am Befehlsende
        const uint64_t t1 = bis(rg, [&] { return (rg.cpu.regSicht(0xFA) & 0x10) != 0; });
        const uint64_t soll = uint64_t(4 * p * t);
        EXPECT_GE(t1 - start, soll) << "p=" << p << " t=" << t;
        EXPECT_LT(t1 - start, soll + 6) << "p=" << p << " t=" << t;
        EXPECT_EQ(rg.cpu.tmr() & 0x02, 0);            // Einzeldurchlauf: Freigabe gelöscht
        EXPECT_EQ(rg.cpu.regSicht(0xF4), 0);          // steht auf 00
    }
}

TEST(Z8Zaehler, DauerbetriebPeriodeUndZaehlerstandLesen) {
    Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
    ld(rg, 0xF5, uint8_t(3 << 2 | 1));               // ÷3, Dauer
    ld(rg, 0xF4, 10);
    ld(rg, 0xF1, 0x03);
    const uint64_t start = rg.cpu.takte;
    uint64_t letzt = rg.cpu.takte;
    while (rg.cpu.takte - start < 120 * 100) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    letzt = rg.cpu.takte;
    // Endwerte = (vergangene Zeit) / (4·3·10)
    EXPECT_EQ(rg.cpu.zaehler(0).abgelaufen, (letzt - start) / 120);
    EXPECT_NE(rg.cpu.tmr() & 0x02, 0);
    // Stand lesen läuft herunter: jede Änderung ist −1 bzw. 1 → 10 (Nachladen)
    int alt = rg.cpu.regSicht(0xF4), aenderungen = 0;
    while (aenderungen < 25) {
        rg.cpu.pc = 0x0800; rg.cpu.step();
        const int neu = rg.cpu.regSicht(0xF4);
        if (neu == alt) continue;
        EXPECT_TRUE(neu == alt - 1 || (alt == 1 && neu == 10) || (alt == 1 && neu == 0)) << alt << "→" << neu;
        alt = neu == 0 ? 10 : neu;
        ++aenderungen;
    }
}

TEST(Z8Zaehler, AnhaltenUndWeiterzaehlenNeuLadenWaehrendDesLaufs) {
    Rig rg; rg.boot();
    ld(rg, 0xF5, uint8_t(1 << 2));
    ld(rg, 0xF4, 200);
    ld(rg, 0xF1, 0x03);
    for (int i = 0; i < 20; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    ld(rg, 0xF1, 0x00);                               // anhalten
    const uint8_t stand = rg.cpu.regSicht(0xF4);
    EXPECT_LT(stand, 200);
    for (int i = 0; i < 20; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_EQ(rg.cpu.regSicht(0xF4), stand);
    ld(rg, 0xF1, 0x02);                               // weiter ab Stand
    for (int i = 0; i < 3; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_LT(rg.cpu.regSicht(0xF4), stand);
    ld(rg, 0xF1, 0x03);                               // Laden = Neustart (Software-Retrigger)
    EXPECT_GE(rg.cpu.regSicht(0xF4), 198);
    EXPECT_EQ(rg.cpu.tmr(), 0x02);                    // Ladebit liest sich 0
}

TEST(Z8Zaehler, T1InternUndExternerTaktUeberTin) {
    {   // intern (PRE1.1 = 1)
        Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
        ld(rg, 0xF3, uint8_t(2 << 2 | 0x02));
        ld(rg, 0xF2, 3);
        ld(rg, 0xF1, 0x0C);
        const uint64_t s = rg.cpu.takte;
        const uint64_t e = bis(rg, [&] { return (rg.cpu.regSicht(0xFA) & 0x20) != 0; });
        EXPECT_GE(e - s, 24u); EXPECT_LT(e - s, 30u);
    }
    {   // extern: jede fallende TIN-Flanke ist ein Vorteilertakt; IRQ2 je Flanke
        Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
        ld(rg, 0xF3, uint8_t(2 << 2));               // ÷2, extern, Einzeldurchlauf
        ld(rg, 0xF2, 3);
        ld(rg, 0xF1, 0x0C);                           // TIN-Art 00: externer Takt
        for (int i = 0; i < 6; ++i) {
            EXPECT_EQ(rg.cpu.regSicht(0xFA) & 0x20, 0) << i;
            rg.cpu.setPin(3, 1, false);
            EXPECT_NE(rg.cpu.regSicht(0xFA) & 0x04, 0);
            ld(rg, 0xFA, 0);
            rg.cpu.setPin(3, 1, true);
            if (i == 5) break;
            rg.cpu.pc = 0x0800; rg.cpu.step();
        }
        // 6 Flanken = 2·3 → Endwert
        rg.cpu.setPin(3, 1, false);   // (eine weitere ändert nichts mehr: Freigabe aus)
        EXPECT_EQ(rg.cpu.zaehler(1).abgelaufen, 1u);
    }
}

TEST(Z8Zaehler, T1TorTriggerRetrigger) {
    {   // Tor: zählt nur bei TIN = H
        Rig rg; rg.boot();
        rg.cpu.setPin(3, 1, false);
        ld(rg, 0xF3, uint8_t(1 << 2 | 1));
        ld(rg, 0xF2, 0);
        ld(rg, 0xF1, 0x1C);                           // Torbetrieb, laden, zählen
        for (int i = 0; i < 50; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        EXPECT_EQ(rg.cpu.regSicht(0xF2), 0);          // 256 = 00: nichts gezählt
        rg.cpu.setPin(3, 1, true);
        for (int i = 0; i < 10; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        const uint8_t s = rg.cpu.regSicht(0xF2);
        EXPECT_GT(s, 0); EXPECT_LT(s, 0xF5);
        rg.cpu.setPin(3, 1, false);
        for (int i = 0; i < 10; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        EXPECT_EQ(rg.cpu.regSicht(0xF2), s);
    }
    {   // Trigger: erste Flanke startet, weitere nicht
        Rig rg; rg.boot();
        ld(rg, 0xF3, uint8_t(1 << 2));
        ld(rg, 0xF2, 100);
        ld(rg, 0xF1, 0x2C);
        for (int i = 0; i < 10; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        EXPECT_EQ(rg.cpu.regSicht(0xF2), 100);
        rg.cpu.setPin(3, 1, false); rg.cpu.setPin(3, 1, true);
        for (int i = 0; i < 10; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        const uint8_t s = rg.cpu.regSicht(0xF2);
        EXPECT_LT(s, 100);
        rg.cpu.setPin(3, 1, false); rg.cpu.setPin(3, 1, true);
        EXPECT_EQ(rg.cpu.regSicht(0xF2), s);          // nicht nachgetriggert
    }
    {   // Retrigger: jede Flanke lädt neu
        Rig rg; rg.boot();
        ld(rg, 0xF3, uint8_t(1 << 2));
        ld(rg, 0xF2, 100);
        ld(rg, 0xF1, 0x3C);
        rg.cpu.setPin(3, 1, false); rg.cpu.setPin(3, 1, true);
        for (int i = 0; i < 10; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        EXPECT_LT(rg.cpu.regSicht(0xF2), 100);
        rg.cpu.setPin(3, 1, false);
        EXPECT_EQ(rg.cpu.regSicht(0xF2), 100);
    }
}

TEST(Z8Zaehler, ToutAnP36) {
    Rig rg; rg.boot();
    ld(rg, 0xF5, uint8_t(1 << 2 | 1));
    ld(rg, 0xF4, 5);                                  // Endwert alle 20 Takte
    ld(rg, 0xF1, 0x43);                               // TOUT = T0, laden, zählen
    EXPECT_NE(rg.cpu.portPegel(3) & 0x40, 0);         // TOUT beim Laden = 1
    int wechsel = 0; bool alt = true;
    const uint64_t s = rg.cpu.takte;
    while (rg.cpu.takte - s < 400) {
        rg.cpu.pc = 0x0800; rg.cpu.step();
        const bool p = rg.cpu.portPegel(3) & 0x40;
        if (p != alt) { ++wechsel; alt = p; }
    }
    EXPECT_GE(wechsel, 18); EXPECT_LE(wechsel, 21);
    ld(rg, 0x03, 0x00);                               // P3-Schreiben ändert P36 nicht
    EXPECT_EQ((rg.cpu.portPegel(3) & 0x40) != 0, rg.cpu.tout());
    ld(rg, 0xF1, 0x02);                               // TOUT aus: P36 = Ausgangsregister
    EXPECT_EQ(rg.cpu.portPegel(3) & 0x40, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// UART
// ═════════════════════════════════════════════════════════════════════════════

namespace {
/// Terminal-Einstellung: 7,3728 MHz, PRE0 = 3 (Dauer), T0 = 2 ⇒ 9600 Bd (UM0016 Table 24).
void uart9600(Rig& rg, uint8_t p3m = 0x41) {
    ld(rg, 0xF5, 0x0D);
    ld(rg, 0xF4, 0x02);
    ld(rg, 0xF7, p3m);
    ld(rg, 0xF1, 0x03);
}
constexpr uint64_t BIT = 4 * 3 * 2 * 16;   // 384 interne Takte = 104,2 µs bei 3,6864 MHz
}  // namespace

TEST(Z8Uart, SendenRahmenStartAchtDatenZweiStoppUndIrq4) {
    Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
    uart9600(rg);
    EXPECT_NE(rg.cpu.portPegel(3) & 0x80, 0);         // Ruhe = 1
    ld(rg, 0xF0, 0xA5);
    const uint64_t s = rg.cpu.takte;
    std::vector<int> bits;
    for (int b = 0; b < 11; ++b) {
        while (rg.cpu.takte - s < uint64_t(b) * BIT + BIT / 2) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        bits.push_back((rg.cpu.portPegel(3) >> 7) & 1);
    }
    // Start 0, Daten A5 LSB zuerst = 1 0 1 0 0 1 0 1, Stopp 1 1
    EXPECT_EQ(bits, (std::vector<int>{0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1}));
    const uint64_t e = bis(rg, [&] { return (rg.cpu.regSicht(0xFA) & 0x10) != 0; });
    // Das ÷16 wird beim Schreiben neu gesetzt, die T0-Endwerte (alle 24 Takte) laufen frei:
    // das Startbit dauert 15–16 Endwerte (UM0016 S. 120).
    EXPECT_GE(e - s, 11 * BIT - 24); EXPECT_LT(e - s, 11 * BIT + 6);
    EXPECT_EQ(rg.gesendet, std::vector<uint8_t>{0xA5});
}

TEST(Z8Uart, SendenMitUngeraderParitaet) {
    for (uint8_t d : {uint8_t(0x41), uint8_t(0x43), uint8_t(0x00), uint8_t(0x7F)}) {
        Rig rg; rg.boot();
        uart9600(rg, 0xC1);
        ld(rg, 0xF0, d);
        const uint64_t s = rg.cpu.takte;
        while (rg.cpu.takte - s < 8 * BIT + BIT / 2) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
        const int p = (rg.cpu.portPegel(3) >> 7) & 1;   // Bit 7 = Parität
        const int einsen = static_cast<int>(std::bitset<7>(d & 0x7F).count()) + p;
        EXPECT_EQ(einsen % 2, 1) << int(d);
    }
}

TEST(Z8Uart, EmpfangenZeitpunktPufferUndUeberschreiben) {
    Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
    SerielleQuelle q(BIT);
    rg.cpu.p30Quelle = [&](uint64_t t) { return q.pegel(t); };
    uart9600(rg);
    const uint64_t s = rg.cpu.takte + 1000;
    q.senden(s, 0x4B);
    q.senden(s + 10 * BIT, 0x6D);                     // direkt dahinter (1 Stoppbit)
    const uint64_t e = bis(rg, [&] { return (rg.cpu.regSicht(0xFA) & 0x08) != 0; });
    // IRQ3 in der Stoppbitmitte (9,5 Bitzeiten); Abtastung zu T0-Endwerten (BIT/16)
    EXPECT_GE(e - s, 9 * BIT + BIT / 2 - BIT / 16);
    EXPECT_LT(e - s, 9 * BIT + BIT / 2 + BIT / 8);
    EXPECT_EQ(rg.cpu.regSicht(0xF0), 0x4B);
    ld(rg, 0xFA, 0x00);
    // Doppelpuffer: während das zweite Zeichen einläuft, bleibt das erste lesbar
    while (rg.cpu.takte - s < 14 * BIT) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_EQ(rg.cpu.regSicht(0xF0), 0x4B);
    bis(rg, [&] { return (rg.cpu.regSicht(0xFA) & 0x08) != 0; });
    EXPECT_EQ(rg.cpu.regSicht(0xF0), 0x6D);           // ungelesen überschrieben (kein Merker)
    EXPECT_EQ(rg.empfangen, (std::vector<uint8_t>{0x4B, 0x6D}));
}

TEST(Z8Uart, FalschesStartbitWirdVerworfen) {
    Rig rg; rg.boot();
    uart9600(rg);
    // kurzer Einbruch (1/4 Bit) über den Pin
    rg.cpu.setPin(3, 0, false);
    for (uint64_t s = rg.cpu.takte; rg.cpu.takte - s < BIT / 4;) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    rg.cpu.setPin(3, 0, true);
    for (uint64_t s = rg.cpu.takte; rg.cpu.takte - s < 12 * BIT;) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_TRUE(rg.empfangen.empty());
}

TEST(Z8Uart, EmpfangParitaetsfehlerInBit7) {
    for (int fehler = 0; fehler < 2; ++fehler) {
        Rig rg; rg.boot();
        SerielleQuelle q(BIT);
        rg.cpu.p30Quelle = [&](uint64_t t) { return q.pegel(t); };
        uart9600(rg, 0xC1);
        const uint8_t d = 0x31;                       // drei Einsen → Parität 0 für ungerade
        const uint8_t roh = uint8_t(d | (fehler ? 0x80 : 0x00));
        q.senden(rg.cpu.takte + 100, roh);
        bis(rg, [&] { return !rg.empfangen.empty(); });
        EXPECT_EQ(rg.empfangen[0], uint8_t(d | (fehler ? 0x80 : 0)));
    }
}

TEST(Z8Uart, SeriellerBetriebUnterdruecktT0Irq4UndP30Irq3) {
    Rig rg; rg.boot(); irqBereit(rg); rg.eins({0x8F}, 0x0F11);
    uart9600(rg);
    for (int i = 0; i < 300; ++i) { rg.cpu.pc = 0x0800; rg.cpu.step(); }
    EXPECT_EQ(rg.cpu.regSicht(0xFA) & 0x10, 0);       // T0 läuft, aber kein IRQ4
    EXPECT_GT(rg.cpu.zaehler(0).abgelaufen, 10u);
    rg.cpu.setPin(3, 0, false);
    EXPECT_EQ(rg.cpu.regSicht(0xFA) & 0x08, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Ports
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Port, Port0NibbleArtenPort1Byte) {
    Rig rg; rg.boot();
    rg.cpu.setPort(0, 0xA5); rg.cpu.setPort(1, 0x3C);
    rg.eins({0xE4, 0x00, 0x40}); EXPECT_EQ(rg.r(0x40), 0xA5);       // Eingang
    rg.eins({0xE4, 0x01, 0x40}); EXPECT_EQ(rg.r(0x40), 0x3C);
    ld(rg, 0xF8, 0x4C);                                             // P0L Ausgang, P0H Eingang, P1 Eingang
    ld(rg, 0x00, 0x7E);
    rg.eins({0xE4, 0x00, 0x40}); EXPECT_EQ(rg.r(0x40), 0xAE);       // hi Pins A, lo Ausgang E
    EXPECT_EQ(rg.cpu.portTreibt(0), 0x0F);
    ld(rg, 0xF8, 0x04);                                             // alles Ausgang
    ld(rg, 0x01, 0x99);
    EXPECT_EQ(rg.cpu.portPegel(1), 0x99);
    EXPECT_EQ(rg.cpu.portTreibt(1), 0xFF);
    rg.eins({0xE4, 0x00, 0x40}); EXPECT_EQ(rg.r(0x40), 0x7E);
    // Rückruf bei Änderung
    bool gemeldet = false;
    for (auto& e : rg.portLog) gemeldet |= e.first == 1 && e.second == 0x99;
    EXPECT_TRUE(gemeldet);
}

TEST(Z8Port, Port2BitweiseOffenerDrainUndGegentakt) {
    Rig rg; rg.boot();
    ld(rg, 0xF6, 0x0F);                               // P20–P23 Eingang, P24–P27 Ausgang
    ld(rg, 0x02, 0xA0);
    rg.cpu.setPort(2, 0x5F);                          // aussen: P26 auf 0 gezogen
    // offener Drain (P3M.0 = 0): Ausgang 1 liest den Pin
    rg.eins({0xE4, 0x02, 0x40});
    EXPECT_EQ(rg.r(0x40), uint8_t(0x0F | (0xA0 & 0x5F)));
    EXPECT_EQ(rg.cpu.portTreibt(2), 0x50);            // nur die 0-Bits werden getrieben
    ld(rg, 0xF7, 0x01);                               // Gegentakt
    rg.eins({0xE4, 0x02, 0x40});
    EXPECT_EQ(rg.r(0x40), 0xAF);
    EXPECT_EQ(rg.cpu.portTreibt(2), 0xF0);
}

TEST(Z8Port, Port3LesenUndSchreiben) {
    Rig rg; rg.boot();
    rg.cpu.setPort(3, 0xF5);
    ld(rg, 0x03, 0x3F);                               // nur P34–P37 werden geschrieben
    rg.eins({0xE4, 0x03, 0x40});
    EXPECT_EQ(rg.r(0x40), 0x35);
    EXPECT_EQ(rg.cpu.portPegel(3) & 0xF0, 0x30);
}

TEST(Z8Port, HandshakeEingangPort0UndAusgangPort2) {
    Rig rg; rg.boot();
    // Port 0 Eingang mit Handshake: DAV0 = P32 (Eingang), RDY0 = P35 (Ausgang)
    ld(rg, 0x03, 0xF0);
    ld(rg, 0xF8, 0x4D);
    ld(rg, 0xF7, 0x04);
    EXPECT_NE(rg.cpu.portPegel(3) & 0x20, 0);         // RDY0 = 1 (bereit)
    rg.cpu.setPort(0, 0x12);
    rg.cpu.setPin(3, 2, false);                        // DAV ↓: gelatcht, RDY ↓
    EXPECT_EQ(rg.cpu.portPegel(3) & 0x20, 0);
    rg.cpu.setPort(0, 0x99);                           // neue Daten, noch nicht gelesen
    rg.cpu.setPin(3, 2, true);
    rg.cpu.setPin(3, 2, false);                        // zweiter DAV: geschützt
    rg.eins({0xE4, 0x00, 0x40});
    EXPECT_EQ(rg.r(0x40), 0x12);
    EXPECT_EQ(rg.cpu.portPegel(3) & 0x20, 0);          // DAV noch 0: RDY bleibt 0
    rg.cpu.setPin(3, 2, true);
    EXPECT_NE(rg.cpu.portPegel(3) & 0x20, 0);          // gelesen und DAV = 1: RDY ↑
    // Port 2 Ausgang mit Handshake: RDY2 = P31, DAV2 = P36
    ld(rg, 0xF6, 0x00);
    ld(rg, 0xF7, 0x21);
    EXPECT_NE(rg.cpu.portPegel(3) & 0x40, 0);          // DAV2 = 1
    rg.cpu.setPin(3, 1, true);                          // Gerät bereit
    ld(rg, 0x02, 0x5A);
    EXPECT_EQ(rg.cpu.portPegel(3) & 0x40, 0);           // DAV ↓
    EXPECT_EQ(rg.cpu.portPegel(2), 0x5A);
    rg.cpu.setPin(3, 1, false);                          // Gerät nimmt: RDY ↓ → DAV ↑
    EXPECT_NE(rg.cpu.portPegel(3) & 0x40, 0);
    ld(rg, 0x02, 0x66);                                  // RDY = 0: DAV bleibt 1
    EXPECT_NE(rg.cpu.portPegel(3) & 0x40, 0);
}

TEST(Z8Port, HandshakePort1UeberP33P34) {
    Rig rg; rg.boot();
    ld(rg, 0xF8, 0x4D);                                  // P1 Eingang
    ld(rg, 0xF7, 0x18);
    rg.cpu.setPort(1, 0x77);
    rg.cpu.setPin(3, 3, false);
    EXPECT_EQ(rg.cpu.portPegel(3) & 0x10, 0);            // RDY1 ↓
    rg.cpu.setPort(1, 0x00);
    rg.eins({0xE4, 0x01, 0x40});
    EXPECT_EQ(rg.r(0x40), 0x77);
}

// ═════════════════════════════════════════════════════════════════════════════
// Externer Bus
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Bus, HolenExternErweitertesTimingUndWartetakte) {
    Rig rg; rg.boot();
    ld(rg, 0xF8, 0x96);                                  // Terminal: A8–A15, AD, Stapel intern
    rg.extProg[0x2000] = 0xFF;                           // NOP extern
    rg.cpu.pc = 0x2000;
    rg.bus.clear();
    EXPECT_EQ(rg.cpu.step(), 6);
    ASSERT_EQ(rg.bus.size(), 1u);
    EXPECT_EQ(rg.bus[0].c.art, Z8Zugriff::Holen);
    EXPECT_FALSE(rg.bus[0].c.erweitert);
    ld(rg, 0xF8, 0xB6);                                  // + erweitertes Timing
    rg.cpu.pc = 0x2000;
    EXPECT_EQ(rg.cpu.step(), 7);
    rg.wartetakte = 2;
    rg.cpu.pc = 0x2000;
    EXPECT_EQ(rg.cpu.step(), 9);
}

TEST(Z8Bus, HochohmigerBusUnterdruecktZyklen) {
    Rig rg; rg.boot();
    ld(rg, 0xF8, 0x9E);                                  // Terminal-DMA: P1 hochohmig
    rg.cpu.rp = 0x20; rg.r(0x22) = 0x10; rg.r(0x23) = 0x00;
    rg.extDaten[0x1000] = 0x42;
    rg.bus.clear();
    rg.eins({0x82, 0x42});                               // LDE r4,@rr2
    EXPECT_TRUE(rg.bus.empty());
    EXPECT_TRUE(rg.cpu.letzterZyklus().hochohmig);
    EXPECT_EQ(rg.r(0x24), 0xFF);
    ld(rg, 0xF8, 0x96);
    rg.eins({0x82, 0x42});
    EXPECT_EQ(rg.r(0x24), 0x42);
}

// ═════════════════════════════════════════════════════════════════════════════
// Save-State
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8Stand, RundreiseMittenImSendenUndZaehlenIstBitgleich) {
    auto aufsetzen = [](Rig& rg) {
        rg.boot(); irqBereit(rg);
        rg.load(" ORG 0800H\nL: INC r0\n JR L\n");
        uart9600(rg);
        ld(rg, 0xF3, 0x0B); ld(rg, 0xF2, 7); ld(rg, 0xF1, 0x0F);
        ld(rg, 0xFB, 0xB0);
        ld(rg, 0xF0, 0x55);
        rg.cpu.pc = 0x0800;
    };
    Rig a; aufsetzen(a);
    for (int i = 0; i < 3000; ++i) a.cpu.step();
    std::vector<uint8_t> st;
    a.cpu.serialize(st);
    Rig b; b.cpu.reg[0x10] = 0x99;
    b.prog = a.prog; b.extProg = a.extProg; b.extDaten = a.extDaten;   // Speicher gehört zur Karte
    const uint8_t* p = st.data();
    ASSERT_TRUE(b.cpu.deserialize(p, st.data() + st.size()));
    EXPECT_EQ(p, st.data() + st.size());
    for (int i = 0; i < 5000; ++i) { a.cpu.step(); b.cpu.step(); }
    std::vector<uint8_t> sa, sb;
    a.cpu.serialize(sa); b.cpu.serialize(sb);
    EXPECT_EQ(sa, sb);
    // Fehlerhafte Daten werden abgewiesen
    std::vector<uint8_t> kaputt(st.begin(), st.begin() + 10);
    const uint8_t* q = kaputt.data();
    EXPECT_FALSE(b.cpu.deserialize(q, kaputt.data() + kaputt.size()));
    kaputt[0] = 99; q = kaputt.data();
    EXPECT_FALSE(b.cpu.deserialize(q, kaputt.data() + kaputt.size()));
}

TEST(Z8Sicht, SichtAendertNichtsUndZeigtAlles) {
    Rig rg; rg.boot();
    uart9600(rg);
    ld(rg, 0xF0, 0x41);
    const Z8Sicht s1 = rg.cpu.sicht();
    const Z8Sicht s2 = rg.cpu.sicht();
    EXPECT_EQ(s1.takte, s2.takte);
    EXPECT_EQ(s1.p3m, 0x41);
    EXPECT_EQ(s1.t[0].pre, 0x0D);
    EXPECT_NE(s1.txSchieber, 0);
    EXPECT_EQ(rg.cpu.regSicht(0xF5), 0x0D);              // Sicht zeigt auch Nur-Schreib-Register
}
