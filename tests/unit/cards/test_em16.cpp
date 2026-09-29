/**
 * @file test_em16.cpp
 * @brief Unit-Tests: EM-Steuerkarte mit U8001 — der 16-Bit-Mode (S4).
 *
 * Soll aus doc/design/17_a5120_16.md §7.3/§7.4/§7.5.  Der U880 wird hier nur als
 * E/A-Zugriffe (Bus) und M1-Takte (EM::onU880M1) nachgestellt, die Zeit über
 * EM::advance; die U8001-Programme sind mit z8kasm (tools/z8000/) assembliert.
 * Den Ablauf in der Maschine und unter CP/A prüfen test_em16_maschine.cpp und
 * tests/integration/test_em16_abl.cpp.
 */
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/em/em.h"
#include "core/cards/k3526/k3526.h"
#include "tools/z8000/z8k_asm.h"

namespace {

constexpr uint16_t kPA = 0xA8, kPB = 0xA9, kPAc = 0xAA, kPBc = 0xAB;
constexpr uint16_t kSt8 = 0xAC, kVek = 0xAD, kSt16 = 0xAE, kAttr = 0xAF;
constexpr uint8_t  SG0 = 1, N_RAMEN = 4, N_STOP = 8, RESET16 = 0x10, N_TRQ8 = 0x20;

struct Rig {
    K1520Bus bus;
    K3526    ops;
    EM       em;
    explicit Rig(EM::Config c = {}) : em(bus, c) {
        ops.attachToBus(bus);
        em.attachToBus();
        em.powerOn();
        bus.ioWrite(kPAc, 0xCF); bus.ioWrite(kPAc, 0xFF);          // A: alles Eingang
        bus.ioWrite(kPBc, 0xCF); bus.ioWrite(kPBc, 0x80);          // B: b7 Eingang
        portB(RESET16 | N_TRQ8);                                    // RAMEN, U8001 im Reset
        for (int p = 0; p < 16; ++p) attr(p, 0x0F);                 // alle Seiten aus
    }
    void portB(uint8_t v) { bus.ioWrite(kPB, v); }
    void attr(int page, uint8_t stored) { bus.ioWrite(uint16_t((page << 12) | kAttr), stored); }
    uint8_t pioA() { return bus.ioRead(kPA); }

    /// z8kasm (segmentiert); Adresse <<s>>o landet in der Zelle s·64K + o.
    void load(const std::string& src) {
        z8k::AsmOptions o; o.seg = em.u8001().isZ8001();
        z8k::AsmResult r = z8k::assemble(src, o);
        for (auto& e : r.errors) ADD_FAILURE() << "asm Zeile " << e.line << ": " << e.text;
        for (auto& kv : r.image) {
            const uint32_t a = kv.first;
            em.poke(((a >> 24) & 0x7F) * 0x10000u + (a & 0xFFFF), kv.second);
        }
    }
    void setW(uint32_t cell, uint16_t v) { em.poke(cell, uint8_t(v >> 8)); em.poke(cell + 1, uint8_t(v)); }
    uint16_t w(uint32_t cell) const { return uint16_t(em.peek(cell) << 8 | em.peek(cell + 1)); }
    /// Resetvektor in Segment @p s: FCW %C000, PC <<ps>>po.
    void vektor(uint32_t s, uint8_t ps, uint16_t po, uint16_t fcw = 0xC000) {
        setW(s * 0x10000 + 2, fcw); setW(s * 0x10000 + 4, uint16_t(ps << 8)); setW(s * 0x10000 + 6, po);
    }
    /// n Befehle des U880 zu je 8 Takten (mit M1).
    void lauf(int n, bool m1 = true) {
        for (int i = 0; i < n; ++i) { if (m1) em.onU880M1(); em.advance(8); }
    }
    void start() { portB(N_TRQ8 | N_STOP); }                         // RESET16 = 0, RAMEN
};

// Gemeinsamer Rahmen: Stapel, PSA bei <<0>>%0400, Mailbox %0080.
const char* kKopf =
    "  SEG\n"
    "  ORG <<0>>%0100\n"
    "  LDL RR14,#<<0>>%0FF0\n"
    "  LD R0,#%0000\n  LDCTL PSAPSEG,R0\n"
    "  LD R0,#%0400\n  LDCTL PSAPOFF,R0\n";

}  // namespace

// ─── Start aus Reset ─────────────────────────────────────────────────────────

/**
 * @test EM16/StartAusReset_VektorAusSegment0_Rechnet
 * @brief RESET16 = 0 ⇒ A29 fällt sofort (16-Bit-Mode), der U8001 holt FCW/PC aus
 *   Segment 0 (A33 gelöscht ⇒ Mode 0) — ein anderer Vektor in Segment 1 bleibt
 *   unberührt; das Ergebnis steht big-endian im EM-Speicher.
 */
TEST(EM16, StartAusReset_VektorAusSegment0_Rechnet) {
    Rig r;
    r.load(std::string(kKopf) +
           "  LD R1,#0\n  LD R2,#100\n"
           "S: ADD R1,R2\n  DJNZ R2,S\n"
           "  LD <<0>>%0080,R1\n"
           "  LDCTL R0,FCW\n  LD <<0>>%0082,R0\n"
           "  HALT\n"
           "  ORG <<0>>%0200\n  LD R0,#%DEAD\n  LD <<0>>%0084,R0\n  HALT\n");
    r.vektor(0, 0, 0x0100);
    r.vektor(1, 0, 0x0200);                          // falscher Vektor in Segment 1
    EXPECT_TRUE(r.em.mode8());
    r.start();
    EXPECT_FALSE(r.em.mode8());                      // /R sofort, ohne M1
    r.lauf(2000);
    EXPECT_TRUE(r.em.u8001().halted());
    EXPECT_EQ(r.w(0x0080), 5050);
    EXPECT_EQ(r.w(0x0082), 0xC000);
    EXPECT_EQ(r.w(0x0084), 0xFFFF);
    EXPECT_EQ(r.em.peek(0x0080), 0x13);              // gerade Adresse = oberes Byte
    EXPECT_FALSE(r.em.mode8());
}

/**
 * @test EM16/Zeit_U8001Laeuft4MHzGegen245MHz
 * @brief advance(2450 U880-Takte) = 1 ms ⇒ 4000 U8001-Takte (±1 Befehl).
 */
TEST(EM16, Zeit_U8001Laeuft4MHzGegen245MHz) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\nL: JR L\n");
    r.vektor(0, 0, 0x0100);
    r.start();
    r.em.advance(10);
    const uint64_t c0 = r.em.u8001().cycles;
    for (int i = 0; i < 245; ++i) r.em.advance(10);
    const int64_t d = int64_t(r.em.u8001().cycles - c0);
    EXPECT_NEAR(double(d), 4000.0, 7.0);
}

// ─── Rückweg in den 8-Bit-Mode ───────────────────────────────────────────────

/**
 * @test EM16/Rueckweg_Trq8_Mset_Busak_A29BeimNaechstenM1
 * @brief TRQ8 allein bewirkt nichts; erst MSET (µ0 → TREN) ⇒ BUSRQ ⇒ BUSAK ⇒ /R frei;
 *   A29 kippt erst mit dem nächsten M1 des U880.  TRQ8 zurück ⇒ sofort 16-Bit-Mode,
 *   der U8001 läuft hinter MSET weiter.
 */
TEST(EM16, Rueckweg_Trq8_Mset_Busak_A29BeimNaechstenM1) {
    Rig r;
    r.load(std::string(kKopf) +
           "  LD R0,#%1234\n  LD <<0>>%0080,R0\n"
           "W: MBIT\n  JR MI,W\n"        // S = 1: µI H = keine Anforderung
           "  MSET\n"
           "  MRES\n"
           "  LD R0,#%5678\n  LD <<0>>%0082,R0\n"
           "  HALT\n");
    r.vektor(0, 0, 0x0100);
    r.attr(4, 0x0C);                                  // Seite 4 → Zelle 0000 (PEN, WE)
    r.start();
    r.lauf(200);
    EXPECT_EQ(r.w(0x80), 0x1234);
    EXPECT_FALSE(r.em.tren());
    EXPECT_FALSE(r.em.drivesMemdi(0x4000));
    r.portB(N_STOP);                                   // /TRQ8 = 0
    r.lauf(200, false);                                // ohne M1
    EXPECT_TRUE(r.em.tren());
    EXPECT_TRUE(r.em.busRq16());
    EXPECT_TRUE(r.em.busAck16());
    EXPECT_EQ(r.pioA() & 0xC0, 0x80);                  // TREN = 1, 8/16 = 0
    EXPECT_FALSE(r.em.mode8());
    r.em.onU880M1();
    EXPECT_TRUE(r.em.mode8());
    EXPECT_EQ(r.pioA() & 0xC0, 0xC0);
    EXPECT_TRUE(r.em.drivesMemdi(0x4000));
    EXPECT_EQ(r.bus.memRead(0x4080), 0x12);
    EXPECT_EQ(r.bus.memRead(0x4081), 0x34);
    EXPECT_EQ(r.w(0x82), 0xFFFF);                      // U8001 steht
    r.portB(N_TRQ8 | N_STOP);                          // TRQ8 zurück
    EXPECT_FALSE(r.em.mode8());
    EXPECT_FALSE(r.em.drivesMemdi(0x4000));
    r.lauf(100);
    EXPECT_EQ(r.w(0x82), 0x5678);
    EXPECT_FALSE(r.em.tren());                         // MRES
}

// ─── A33/A35/Status-8 ────────────────────────────────────────────────────────

/**
 * @test EM16/A33A35_WortOutSchreibtBeide_NurAD7
 * @brief OUT an eine Adresse mit Bit 7 schreibt A33 (low) und A35 (high); Bit 7 = 0
 *   schreibt nichts; OUTB legt das Byte auf beide Hälften (Kern-Annahme, G2 misst).
 */
TEST(EM16, A33A35_WortOutSchreibtBeide_NurAD7) {
    Rig r;
    r.load(std::string(kKopf) +
           "  LD R0,#%A505\n  OUT %0080,R0\n  HALT\n"
           "  LD R0,#%5A06\n  OUT %017F,R0\n  HALT\n"
           "  LDB RL0,#%01\n  OUTB %0081,RL0\n  HALT\n"
           "  LD R0,#%7702\n  OUT %FF80,R0\n  HALT\n");
    r.vektor(0, 0, 0x0100);
    r.start();
    r.lauf(100);
    EXPECT_EQ(r.em.steuer16(), 0x05);
    EXPECT_EQ(r.bus.ioRead(kSt16), 0xA5);
    EXPECT_EQ(r.pioA() & 0x07, 0x05);                  // PIOA-0..2
    // HALT verlassen (Testabkürzung): Halt-Merker löschen, PC steht schon dahinter.
    Z8kRunState rs = r.em.u8001().runState(); rs.halted = false; r.em.u8001().setRunState(rs);
    r.lauf(100);
    EXPECT_EQ(r.em.steuer16(), 0x05);                  // %017F: Bit 7 = 0 → nichts
    EXPECT_EQ(r.bus.ioRead(kSt16), 0xA5);
    rs = r.em.u8001().runState(); rs.halted = false; r.em.u8001().setRunState(rs);
    r.lauf(100);
    EXPECT_EQ(r.em.steuer16(), 0x01);
    EXPECT_EQ(r.bus.ioRead(kSt16), 0x01);
    rs = r.em.u8001().runState(); rs.halted = false; r.em.u8001().setRunState(rs);
    r.lauf(100);
    EXPECT_EQ(r.em.steuer16(), 0x02);                  // %FF80: nur AD7 zählt
    EXPECT_EQ(r.bus.ioRead(kSt16), 0x77);
}

/**
 * @test EM16/Status8_WortInHighByte_InbNicht
 * @brief Status-8 (ACH) liegt beim E/A-Lesen auf AD8–15: Wort-IN sieht es im oberen
 *   Byte, INB (AD0–7) nicht; eine Adresse ohne Bit 7 liefert nichts.
 */
TEST(EM16, Status8_WortInHighByte_InbNicht) {
    Rig r;
    r.load(std::string(kKopf) +
           "  IN R0,%0080\n  LD <<0>>%0080,R0\n"
           "  INB RL1,%0080\n  LDB <<0>>%0083,RL1\n"
           "  IN R2,%0040\n  LD <<0>>%0084,R2\n  HALT\n");
    r.vektor(0, 0, 0x0100);
    r.bus.ioWrite(kSt8, 0xC3);
    r.start();
    r.lauf(200);
    EXPECT_EQ(r.em.peek(0x80), 0xC3);
    EXPECT_EQ(r.em.peek(0x83), 0xFF);
    EXPECT_EQ(r.w(0x84), 0xFFFF);
}

// ─── Interrupts ──────────────────────────────────────────────────────────────

/**
 * @test EM16/VI_VektorUndStatus8AlsKennung_LoeschtVi
 * @brief ADH setzt /VI (PIO A3 = 0); die VI-Quittung liest Vektor (low) + Status-8
 *   (high) als Kennung, löscht INT (PIO A3 = 1); Sprung über die Tabelle PSA+%3C+2·v.
 */
TEST(EM16, VI_VektorUndStatus8AlsKennung_LoeschtVi) {
    Rig r;
    r.load(std::string(kKopf) +
           "  EI VI\n"
           "L: JR L\n"
           "  ORG <<0>>%0300\n"
           "  LD R0,@RR14\n  LD <<0>>%0080,R0\n"
           "  INC R9\n  LD <<0>>%0082,R9\n  IRET\n"
           "  ORG <<0>>%0400+%3A\n  DW %C000\n"
           "  ORG <<0>>%0400+%3C+2*4\n  DW %0000,%0300\n"
           "  ORG <<0>>%0400+%3C+2*6\n  DW %0000,%0300\n");
    r.vektor(0, 0, 0x0100);
    r.start();
    r.lauf(100);
    EXPECT_EQ(r.pioA() & 0x08, 0x08);
    r.bus.ioWrite(kSt8, 0x5A);
    r.bus.ioWrite(kVek, 0x04);
    EXPECT_TRUE(r.em.viPending());
    EXPECT_EQ(r.pioA() & 0x08, 0x00);
    r.lauf(100);
    EXPECT_FALSE(r.em.viPending());
    EXPECT_EQ(r.pioA() & 0x08, 0x08);
    EXPECT_EQ(r.w(0x80), 0x5A04);
    r.bus.ioWrite(kSt8, 0xA5);
    r.bus.ioWrite(kVek, 0x06);
    r.lauf(100);
    EXPECT_EQ(r.w(0x80), 0xA506);
    EXPECT_EQ(r.w(0x82), 2);
}

/**
 * @test EM16/Int16_A33Bit4_PioA4_InterruptZumU880
 * @brief A33 Bit 4 → PIO A4; mit einer PIO im Bitbetrieb (Maske auf A4) fordert die
 *   EM-PIO einen Interrupt beim U880 an.
 */
TEST(EM16, Int16_A33Bit4_PioA4_InterruptZumU880) {
    Rig r;
    r.bus.ioWrite(kPAc, 0x40);                         // Vektor 40H
    r.bus.ioWrite(kPAc, 0xCF); r.bus.ioWrite(kPAc, 0xFF);
    r.bus.ioWrite(kPAc, 0xB7);                         // IE, ODER, High aktiv, Maske folgt
    r.bus.ioWrite(kPAc, 0xEF);                         // nur A4 überwacht
    r.load(std::string(kKopf) + "  LD R0,#%0010\n  OUT %0080,R0\n  HALT\n");
    r.vektor(0, 0, 0x0100);
    EXPECT_FALSE(r.em.hasInterrupt());
    r.start();
    r.lauf(100);
    EXPECT_EQ(r.pioA() & 0x10, 0x10);
    r.em.setIEI(true);
    EXPECT_TRUE(r.em.hasInterrupt());
    EXPECT_EQ(r.em.getVector(), 0x40);
}

/**
 * @test EM16/A53_NviNachStapelzugriffen
 * @brief Mit A33 Bit 3 zählt A53 Stapelzugriffe rückwärts; QD fällt nach
 *   (Vorlast − 7) Zugriffen ⇒ NVI.  Vorlast 15 ⇒ 8, Vorlast 12 ⇒ 5; ohne Bit 3 nie.
 */
TEST(EM16, A53_NviNachStapelzugriffen) {
    const std::string fw = std::string(kKopf) +
        "  LD R0,<<0>>%0090\n  OUT %0080,R0\n"       // A33 aus der Mailbox
        "  EI NVI\n  LD R4,#0\n"
        "L: INC R4\n  PUSH @RR14,R4\n  CP R4,#40\n  JR NZ,L\n"
        "  LD R0,#%FFFF\n  LD <<0>>%0080,R0\n  HALT\n"
        "  ORG <<0>>%0300\n  LD <<0>>%0080,R4\n  LD R0,#0\n  OUT %0080,R0\n  HALT\n"
        "  ORG <<0>>%0400+%32\n  DW %C000,%0000,%0300\n";
    for (auto [vorlast, bit3, soll] : {std::tuple{15, true, 8}, {12, true, 5}, {15, false, -1}}) {
        EM::Config c; c.a53_vorlast = uint8_t(vorlast);
        Rig r(c);
        r.load(fw);
        r.setW(0x90, bit3 ? 0x0008 : 0x0000);
        r.vektor(0, 0, 0x0100);
        r.start();
        r.lauf(3000);
        EXPECT_EQ(int16_t(r.w(0x80)), soll) << "Vorlast " << vorlast << " Bit3 " << bit3;
    }
}

// ─── Segmentweiche ───────────────────────────────────────────────────────────

/**
 * @test EM16/Segmentweiche_DreiModi
 * @brief Mode 0: Segment aus A33 Bit 5/6 (für alle Zugriffe); Mode 1: INSTR × N/S
 *   (System-Daten 0, System-Befehl 1 — LDR ist Programmspeicher —, Normal-Daten 2,
 *   Normal-Befehl 3); Mode 2: SN0/SN1.  Das Programm liegt in allen vier Segmenten.
 */
TEST(EM16, Segmentweiche_DreiModi) {
    Rig r;
    const std::string fw = std::string(kKopf) +
        // Mode 0: A33 = %20/%40/%60 → Segment 1/2/3 (auch Befehle — Kopien liegen dort)
        "  LD R1,#%A001\n  LD R0,#%0020\n  OUT %0080,R0\n  LD <<0>>%0F00,R1\n"
        "  LD R1,#%A002\n  LD R0,#%0040\n  OUT %0080,R0\n  LD <<0>>%0F00,R1\n"
        "  LD R1,#%A003\n  LD R0,#%0060\n  OUT %0080,R0\n  LD <<0>>%0F00,R1\n"
        // Mode 1, System: LD = Daten (0), LDR = Befehlsraum (1)
        "  LD R0,#%0080\n  OUT %0080,R0\n"
        "  LD R2,<<0>>%0F10\n  LDR R3,<<0>>%0F10\n"
        "  LD <<0>>%0F20,R2\n  LD <<0>>%0F22,R3\n"
        // Normalmodus: Daten (2), Befehlsraum (3); zurück per SC
        "  LD R0,#%8000\n  LDCTL FCW,R0\n"
        "  LD R4,<<0>>%0F10\n  LDR R5,<<0>>%0F10\n"
        "  LD R6,#%B222\n  LD <<0>>%0F02,R6\n"
        "  SC #1\n"
        "  LD <<0>>%0F24,R4\n  LD <<0>>%0F26,R5\n"
        // Mode 2: SN0/SN1
        "  LD R0,#%00C0\n  OUT %0080,R0\n"
        "  LD R1,#%C002\n  LD <<2>>%0F04,R1\n"
        "  LD R1,#%C003\n  LD <<3>>%0F04,R1\n"
        "  LD R1,#%C005\n  LD <<5>>%0F04,R1\n"
        "  LD R0,#%0000\n  OUT %0080,R0\n  HALT\n"
        // SC-Handler: Rahmen (4 Worte) verwerfen, weiter
        "  ORG <<0>>%0300\n  ADD R15,#8\n  JP <<0>>%0100+SCW\n"
        "  ORG <<0>>%0400+%1A\n  DW %C000,%0000,%0300\n";
    // SCW = Offset hinter SC: vorher einmal assemblieren, um ihn zu finden.
    z8k::AsmOptions o; o.seg = true;
    auto probe = z8k::assemble(std::string(fw).replace(fw.find("SCW"), 3, "0"), o);
    uint32_t scw = 0;
    for (auto& kv : probe.image)
        if ((kv.first & 0xFFFF) < 0x0300 && kv.second == 0x7F &&
            probe.image.count(kv.first + 1) && probe.image.at(kv.first + 1) == 0x01)
            scw = (kv.first & 0xFFFF) + 2 - 0x0100;
    ASSERT_NE(scw, 0u);
    std::string fw2 = fw;
    fw2.replace(fw2.find("SCW"), 3, std::to_string(scw));
    r.load(fw2);
    for (uint32_t s = 1; s < 4; ++s)                  // Kopien in Segment 1..3
        for (uint32_t a = 0; a < 0x1000; ++a) r.em.poke(s * 0x10000 + a, r.em.peek(a));
    for (uint32_t s = 0; s < 4; ++s) r.setW(s * 0x10000 + 0x0F10, uint16_t(0xB000 | s << 4));
    r.vektor(0, 0, 0x0100);
    r.start();
    r.lauf(3000);
    ASSERT_TRUE(r.em.u8001().halted());
    EXPECT_EQ(r.w(0x10000 + 0x0F00), 0xA001);
    EXPECT_EQ(r.w(0x20000 + 0x0F00), 0xA002);
    EXPECT_EQ(r.w(0x30000 + 0x0F00), 0xA003);
    EXPECT_EQ(r.w(0x0F20), 0xB000);                    // System-Daten → 0
    EXPECT_EQ(r.w(0x0F22), 0xB010);                    // System-Befehl → 1
    EXPECT_EQ(r.w(0x0F24), 0xB020);                    // Normal-Daten → 2
    EXPECT_EQ(r.w(0x0F26), 0xB030);                    // Normal-Befehl → 3
    EXPECT_EQ(r.w(0x20000 + 0x0F02), 0xB222);
    EXPECT_EQ(r.w(0x20000 + 0x0F04), 0xC002);
    EXPECT_EQ(r.w(0x30000 + 0x0F04), 0xC003);
    EXPECT_EQ(r.w(0x10000 + 0x0F04), 0xC005);          // SN = 5 → SN0 = 1, SN1 = 0
    EXPECT_EQ(r.em.segMode(), 0);
}

// ─── STOP, RESET16 ───────────────────────────────────────────────────────────

/**
 * @test EM16/Stop_HaeltDenU8001An
 * @brief /STOP = 0 (PIO B3) hält den U8001 an (Zähler in A35 steht), /STOP = 1 lässt
 *   ihn weiterlaufen.
 */
TEST(EM16, Stop_HaeltDenU8001An) {
    Rig r;
    r.load(std::string(kKopf) +
           "L: INC R5\n  LDB RH6,RL5\n  CLRB RL6\n  OUT %0080,R6\n  JR L\n");
    r.vektor(0, 0, 0x0100);
    r.start();
    r.lauf(50);
    const uint8_t a = r.bus.ioRead(kSt16);
    r.lauf(50);
    EXPECT_NE(r.bus.ioRead(kSt16), a);
    r.portB(N_TRQ8);                                   // /STOP = 0 (B3)
    r.lauf(20);
    EXPECT_TRUE(r.em.stop16());
    const uint8_t b = r.bus.ioRead(kSt16);
    r.lauf(200);
    EXPECT_EQ(r.bus.ioRead(kSt16), b);
    r.portB(N_TRQ8 | N_STOP);
    r.lauf(50);
    EXPECT_NE(r.bus.ioRead(kSt16), b);
}

/**
 * @test EM16/Reset16_LoeschtA33A34_Neustart
 * @brief RESET16 = 1 löscht A33 und A34, setzt A29 (8-Bit-Mode); RESET16 = 0 startet
 *   den U8001 neu aus dem Vektor.
 */
TEST(EM16, Reset16_LoeschtA33A34_Neustart) {
    Rig r;
    r.load(std::string(kKopf) +
           "  INC <<0>>%0080\n  LD R0,#%00E7\n  OUT %0080,R0\n  HALT\n");
    r.vektor(0, 0, 0x0100);
    r.setW(0x80, 0);
    r.start();
    r.lauf(100);
    r.bus.ioWrite(kVek, 0x22);
    EXPECT_EQ(r.em.steuer16(), 0xE7);
    EXPECT_EQ(r.em.segMode(), 2);
    r.portB(RESET16 | N_TRQ8);
    EXPECT_EQ(r.em.steuer16(), 0x00);
    EXPECT_FALSE(r.em.viPending());
    EXPECT_TRUE(r.em.mode8());
    r.start();
    r.lauf(100);
    EXPECT_EQ(r.w(0x80), 2);
}

// ─── EM064, Save-State ───────────────────────────────────────────────────────

/**
 * @test EM16/EM064_U8002_SegmenteGespiegelt
 * @brief EM064: U8002 (nichtsegmentiert, PC aus 0004), SG0/SG1 wirken nicht.
 */
TEST(EM16, EM064_U8002_SegmenteGespiegelt) {
    EM::Config c; c.variante = EM::Variante::EM064;
    Rig r(c);
    EXPECT_FALSE(r.em.u8001().isZ8001());
    z8k::AsmOptions o; o.seg = false;
    auto a = z8k::assemble("  NONSEG\n  ORG %0100\n  LD R0,#%0060\n  OUT %0080,R0\n"
                           "  LD R1,#%4242\n  LD %0F00,R1\n  HALT\n", o);
    for (auto& e : a.errors) ADD_FAILURE() << e.text;
    for (auto& kv : a.image) r.em.poke(kv.first & 0xFFFF, kv.second);
    r.setW(2, 0x4000); r.setW(4, 0x0100);
    r.start();
    r.lauf(100);
    EXPECT_EQ(r.w(0x0F00), 0x4242);
}

/**
 * @test EM16/SaveState_MittenImLaufGleicherFortgang
 * @brief serialize/deserialize mitten im Lauf (U8001 mit laufendem LDIR): die
 *   wiederhergestellte Karte läuft bitgleich weiter.
 */
TEST(EM16, SaveState_MittenImLaufGleicherFortgang) {
    auto baue = [](Rig& r) {
        r.load(std::string(kKopf) +
               "  LDL RR2,#<<0>>%1000\n  LDL RR4,#<<0>>%2000\n  LD R6,#%0800\n"
               "  LDIR @RR4,@RR2,R6\n"
               "  LD R0,#%A1C3\n  OUT %0080,R0\n  HALT\n");
        for (uint32_t i = 0; i < 0x1000; ++i) r.em.poke(0x1000 + i, uint8_t(i * 7));
        r.vektor(0, 0, 0x0100);
        r.start();
    };
    Rig a; baue(a);
    a.lauf(300);
    ASSERT_TRUE(a.em.u8001().inRepeat());
    std::vector<uint8_t> blob;
    a.em.serialize(blob);
    Rig b;
    const uint8_t* p = blob.data();
    ASSERT_TRUE(b.em.deserialize(p, blob.data() + blob.size()));
    EXPECT_EQ(p, blob.data() + blob.size());
    a.lauf(20000); b.lauf(20000);
    EXPECT_TRUE(a.em.u8001().halted());
    EXPECT_TRUE(b.em.u8001().halted());
    EXPECT_EQ(b.em.status16(), 0xA1);
    for (uint32_t i = 0; i < 0x1000; ++i) ASSERT_EQ(a.em.peek(0x2000 + i), b.em.peek(0x2000 + i));
    EXPECT_EQ(a.em.u8001().cycles, b.em.u8001().cycles);
    // Falsche Variante wird abgewiesen.
    EM::Config c; c.variante = EM::Variante::EM064;
    Rig d(c);
    p = blob.data();
    EXPECT_FALSE(d.em.deserialize(p, blob.data() + blob.size()));
}
