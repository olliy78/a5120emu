/**
 * @file test_z8000.cpp
 * @brief U8001/U8002-Primitive (core/primitives/z8000.h) gegen das Zilog Z8000 CPU
 *        User's Reference Manual — je Befehlsgruppe, Testprogramme mit z8kasm (S2).
 *
 * | Gruppe          | Inhalt                                                        |
 * |-----------------|---------------------------------------------------------------|
 * | Reset           | FCW/PC aus 0002/0004[/0006], Segment 0, Systemmodus, MO       |
 * | Register        | Bänke R14/R15 (Tabelle 4.1), RR/RQ, ungerade Paare            |
 * | Arithmetik      | Handbuch-Beispiele, Flags, DAB, MULT/MULTL, DIV/DIVL alle Fälle|
 * | Logik/Bit/Schieben | AND/OR/XOR/COM/TEST/TSET/BIT/RES/SET/Rotate/Shift/RLDB     |
 * | Laden/Stapel    | LD-Arten seg/unseg, LDA/LDAR/LDR, LDM, EX, PUSH/POP, LDPS     |
 * | Sprünge         | JP/JR/CALL/CALR/RET/DJNZ, Takte genommen/nicht genommen       |
 * | Blockbefehle    | LDIR/LDD/CPIR/CPSD/TRTIRB/INIR/OTDR, Wiederholung, Unterbrechung |
 * | Traps           | privilegiert, SC, EPA (EPA=0 Trap, EPA=1 NOP), Rahmen, PC     |
 * | Interrupts      | NMI (Flanke), VI (Vektortabelle), NVI, Masken, IRET, Priorität|
 * | Bus             | Status je Zugriff, N/S, SN, Byte-Hälften, E/A, Refresh        |
 * | Pins            | HALT, STOP, BUSREQ, RESET-Leitung, µI/µ0 (MBIT/MREQ/MSET/MRES)|
 * | Z8002           | FCW ohne SEG, PSA mit 2-Wort-Einträgen                        |
 */
#include "tests/unit/primitives/z8k_rig.h"

using z8ktest::Rig;
using M = Z8000::Model;

namespace {

constexpr uint16_t SEGSYS = 0xC000;     // FCW: segmentiert, System
constexpr uint16_t SYS = 0x4000;        // FCW: nichtsegmentiert, System

/// Nichtsegmentiertes Programm ab %0100 (Z8002 oder Z8001 unseg), läuft bis HALT.
Rig& runNonseg(Rig& r, const std::string& body, uint16_t fcw = SYS) {
    r.load("  NONSEG\n  ORG %0100\n" + body + "\n  HALT\n", false);
    r.boot(fcw, 0, 0x0100);
    r.runToHalt();
    return r;
}

/// Segmentiertes Programm ab <<0>>%0100, Systemstapel RR14 = <<0>>%F000.
Rig& runSeg(Rig& r, const std::string& body, uint16_t fcw = SEGSYS) {
    r.load("  SEG\n  ORG <<0>>%0100\n" + body + "\n  HALT\n", true);
    r.boot(fcw, 0, 0x0100);
    r.cpu.R14[1] = 0x0000; r.cpu.R15[1] = 0xF000;
    r.runToHalt();
    return r;
}

bool F(const Rig& r, uint16_t f) { return (r.cpu.fcw & f) != 0; }

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Reset
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Reset, Z8001LiestFcwSegmentOffsetAusSegment0) {
    Rig r;
    r.setW(0, 2, 0xC000); r.setW(0, 4, 0x0300); r.setW(0, 6, 0x1234);
    r.cpu.reset();
    int c = r.cpu.step();
    EXPECT_EQ(r.cpu.fcw, 0xC000);
    EXPECT_EQ(r.cpu.pcSeg, 3);
    EXPECT_EQ(r.cpu.pc, 0x1234);
    EXPECT_GT(c, 0);
    ASSERT_EQ(r.log.size(), 3u);
    const uint16_t addrs[3] = {2, 4, 6};
    for (int i = 0; i < 3; ++i) {
        const auto& cy = r.log[size_t(i)].first;
        EXPECT_EQ(cy.st, Z8kStatus::MemInstr);  // Systemprogrammspeicher (§7.4)
        EXPECT_TRUE(cy.system);
        EXPECT_TRUE(cy.read);
        EXPECT_TRUE(cy.word);
        EXPECT_EQ(cy.seg, 0);
        EXPECT_EQ(cy.addr, addrs[i]);
    }
}

TEST(Z8000Reset, Z8002LiestFcwUndPcUndLoeschtSeg) {
    Rig r(M::Z8002);
    r.setW(0, 2, 0xC000); r.setW(0, 4, 0x0400);
    r.cpu.reset();
    r.cpu.step();
    EXPECT_EQ(r.cpu.fcw, 0x4000);      // SEG gibt es auf der Z8002 nicht
    EXPECT_EQ(r.cpu.pc, 0x0400);
    EXPECT_EQ(r.log.size(), 2u);
}

TEST(Z8000Reset, ResetLeitungHaeltDieCpuFestUndMoIstInaktiv) {
    Rig r;
    r.setW(0, 2, SEGSYS); r.setW(0, 6, 0x0100);
    r.setW(0, 0x0100, 0x7B08);   // MSET
    r.boot(SEGSYS, 0, 0x0100);
    r.cpu.step();
    EXPECT_TRUE(r.cpu.moActive());
    r.cpu.setResetLine(true);
    r.log.clear();
    for (int i = 0; i < 5; ++i) EXPECT_EQ(r.cpu.step(), 1);
    EXPECT_TRUE(r.log.empty());           // festgehalten: kein Buszyklus
    r.cpu.setResetLine(false);
    r.cpu.step();
    EXPECT_FALSE(r.cpu.moActive());       // nach Reset MO = H (§7.4)
    EXPECT_EQ(r.cpu.pc, 0x0100);
}

TEST(Z8000Reset, RefreshIstNachResetAus) {
    Rig r;
    r.cpu.refresh = 0x8200;
    r.boot(SEGSYS, 0, 0x100);
    EXPECT_EQ(r.cpu.refresh & 0x8000, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Register
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Register, Tabelle41StapelzeigerBaenke) {
    Rig r;
    r.cpu.R14[0] = 0x1111; r.cpu.R14[1] = 0x2222;
    r.cpu.R15[0] = 0x3333; r.cpu.R15[1] = 0x4444;
    auto look = [&](uint16_t fcw) { r.cpu.fcw = fcw; return uint32_t(r.cpu.r(14)) << 16 | r.cpu.r(15); };
    EXPECT_EQ(look(0xC000), 0x22224444u);  // System seg
    EXPECT_EQ(look(0x4000), 0x11114444u);  // System unseg: R14 normal
    EXPECT_EQ(look(0x8000), 0x11113333u);  // Normal seg
    EXPECT_EQ(look(0x0000), 0x11113333u);  // Normal unseg
}

TEST(Z8000Register, ByteUndLangregister) {
    Rig r;
    r.cpu.fcw = SEGSYS;
    r.cpu.setR(2, 0x1234); r.cpu.setR(3, 0x5678);
    EXPECT_EQ(r.cpu.rb(2), 0x12);       // RH2
    EXPECT_EQ(r.cpu.rb(10), 0x34);      // RL2
    EXPECT_EQ(r.cpu.rr(2), 0x12345678u);
    r.cpu.setRQ(0, 0x0102030405060708ull);
    EXPECT_EQ(r.cpu.r(0), 0x0102); EXPECT_EQ(r.cpu.r(3), 0x0708);
}

TEST(Z8000Register, UngeradesRegisterpaarAlsZeigerIstDasGeradePaar) {
    // §8: „ungerade Register als Zeiger im segmentierten Modus".  Kodiert @RR3
    // (Feld 3); gelesen wird RR2 — wie MAME (Bit 0 des Feldes ignoriert).
    Rig r;
    r.setW(0, 0x100, 0x2134);           // LD R4,@RR3
    r.setW(0, 0x102, 0x7A00);           // HALT
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setR(2, 0x0100); r.cpu.setR(3, 0x0500); r.cpu.setR(4, 0);
    r.setW(1, 0x0500, 0xBEEF);          // <<1>>%0500 = RR2
    r.runToHalt();
    EXPECT_EQ(r.cpu.r(4), 0xBEEF);
}

// ═════════════════════════════════════════════════════════════════════════════
// Arithmetik
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Arith, AdcLangeAdditionHandbuch) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R0,#%0000\n  LD R1,#%FFFF\n  LD R2,#%4320\n  LD R3,#%0001\n"
                 "  ADD R1,R3\n  ADC R0,R2");
    EXPECT_EQ(r.cpu.r(0), 0x4321);
    EXPECT_EQ(r.cpu.r(1), 0x0000);
}

TEST(Z8000Arith, AddFlagsUeberlaufUndUebertrag) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R0,#%7FFF\n  ADD R0,#1");
    EXPECT_EQ(r.cpu.r(0), 0x8000);
    EXPECT_TRUE(F(r, Z8000::F_PV)); EXPECT_TRUE(F(r, Z8000::F_S)); EXPECT_FALSE(F(r, Z8000::F_C));
    Rig r2(M::Z8002);
    runNonseg(r2, "  LD R0,#%FFFF\n  ADD R0,#1");
    EXPECT_EQ(r2.cpu.r(0), 0);
    EXPECT_TRUE(F(r2, Z8000::F_C)); EXPECT_TRUE(F(r2, Z8000::F_Z)); EXPECT_FALSE(F(r2, Z8000::F_PV));
}

TEST(Z8000Arith, SubUndCpBorgen) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R0,#1\n  SUB R0,#2");
    EXPECT_EQ(r.cpu.r(0), 0xFFFF);
    EXPECT_TRUE(F(r, Z8000::F_C)); EXPECT_TRUE(F(r, Z8000::F_S));
    // CPB @R5,#3 mit [0400] = 2: C=1, S=1, Z=0, V=0 (Handbuch)
    Rig r2(M::Z8002);
    r2.setB(0, 0x400, 2);
    runNonseg(r2, "  LD R5,#%0400\n  CPB @R5,#3");
    EXPECT_TRUE(F(r2, Z8000::F_C)); EXPECT_TRUE(F(r2, Z8000::F_S));
    EXPECT_FALSE(F(r2, Z8000::F_Z)); EXPECT_FALSE(F(r2, Z8000::F_PV));
}

TEST(Z8000Arith, ByteArithmetikSetztDundH) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RL0,#%0F\n  ADDB RL0,#1");
    EXPECT_TRUE(F(r, Z8000::F_H)); EXPECT_FALSE(F(r, Z8000::F_D));
    Rig r2(M::Z8002);
    runNonseg(r2, "  LDB RL0,#%10\n  SUBB RL0,#1");
    EXPECT_TRUE(F(r2, Z8000::F_H)); EXPECT_TRUE(F(r2, Z8000::F_D));
    // Wortbefehle lassen D/H stehen
    Rig r3(M::Z8002);
    runNonseg(r3, "  LDB RL0,#%10\n  SUBB RL0,#1\n  LD R1,#%000F\n  ADD R1,#1");
    EXPECT_TRUE(F(r3, Z8000::F_H)); EXPECT_TRUE(F(r3, Z8000::F_D));
}

TEST(Z8000Arith, DabNachAdditionUndSubtraktion) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RL0,#%15\n  ADDB RL0,#%27\n  DAB RL0\n"      // 15+27 = 42 (Handbuch)
                 "  LDB RL1,#%42\n  SUBB RL1,#%15\n  DAB RL1\n"      // 42-15 = 27
                 "  LDB RL2,#%99\n  ADDB RL2,#%01\n  DAB RL2");      // 99+01 = 00, C
    EXPECT_EQ(r.cpu.rb(8), 0x42);
    EXPECT_EQ(r.cpu.rb(9), 0x27);
    EXPECT_EQ(r.cpu.rb(10), 0x00);
    EXPECT_TRUE(F(r, Z8000::F_C)); EXPECT_TRUE(F(r, Z8000::F_Z));
}

TEST(Z8000Arith, DabTabelleVollstaendig) {
    // Jede Zeile der Handbuchtabelle (Kap. 6, DAB): alle Bytes der Bereiche.
    struct Row { bool sub; bool c; int hiLo, hiHi; bool h; int loLo, loHi; uint8_t add; bool cAfter; };
    const Row rows[] = {
        {false, 0, 0x0, 0x9, 0, 0x0, 0x9, 0x00, 0}, {false, 0, 0x0, 0x8, 0, 0xA, 0xF, 0x06, 0},
        {false, 0, 0x0, 0x9, 1, 0x0, 0x3, 0x06, 0}, {false, 0, 0xA, 0xF, 0, 0x0, 0x9, 0x60, 1},
        {false, 0, 0x9, 0xF, 0, 0xA, 0xF, 0x66, 1}, {false, 0, 0xA, 0xF, 1, 0x0, 0x3, 0x66, 1},
        {false, 1, 0x0, 0x2, 0, 0x0, 0x9, 0x60, 1}, {false, 1, 0x0, 0x2, 0, 0xA, 0xF, 0x66, 1},
        {true, 0, 0x0, 0x9, 0, 0x0, 0x9, 0x00, 0},  {true, 0, 0x0, 0x8, 1, 0x6, 0xF, 0xFA, 0},
        {true, 1, 0x7, 0xF, 0, 0x0, 0x9, 0xA0, 1},  {true, 1, 0x6, 0xF, 1, 0x6, 0xF, 0x9A, 1},
    };
    Rig r(M::Z8002);
    r.setW(0, 0x100, 0xB080);   // DAB RL0
    r.boot(SYS, 0, 0x100);
    for (const Row& row : rows)
        for (int hi = row.hiLo; hi <= row.hiHi; ++hi)
            for (int lo = row.loLo; lo <= row.loHi; ++lo) {
                uint8_t v = uint8_t(hi << 4 | lo);
                r.cpu.fcw = uint16_t(SYS | (row.sub ? Z8000::F_D : 0) | (row.c ? Z8000::F_C : 0) |
                                     (row.h ? Z8000::F_H : 0));
                r.cpu.pc = 0x100;
                r.cpu.setRB(8, v);
                r.cpu.step();
                EXPECT_EQ(r.cpu.rb(8), uint8_t(v + row.add)) << std::hex << int(v) << " sub=" << row.sub;
                EXPECT_EQ(F(r, Z8000::F_C), row.cAfter) << std::hex << int(v);
            }
}

TEST(Z8000Arith, IncDecLassenCarryStehen) {
    Rig r(M::Z8002);
    runNonseg(r, "  SETFLG C\n  LD R10,#%002A\n  DEC R10\n  LD R1,#%7FFF\n  INC R1,#1");
    EXPECT_EQ(r.cpu.r(10), 0x0029);     // Handbuch DEC
    EXPECT_EQ(r.cpu.r(1), 0x8000);
    EXPECT_TRUE(F(r, Z8000::F_C));
    EXPECT_TRUE(F(r, Z8000::F_PV));
}

TEST(Z8000Arith, NegHandbuchUndGrenzwert) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R8,#%051F\n  NEG R8");
    EXPECT_EQ(r.cpu.r(8), 0xFAE1);
    EXPECT_TRUE(F(r, Z8000::F_C));
    Rig r2(M::Z8002);
    runNonseg(r2, "  LD R8,#%8000\n  NEG R8");
    EXPECT_EQ(r2.cpu.r(8), 0x8000);
    EXPECT_TRUE(F(r2, Z8000::F_PV));
    Rig r3(M::Z8002);
    runNonseg(r3, "  CLR R8\n  NEG R8");
    EXPECT_FALSE(F(r3, Z8000::F_C)); EXPECT_TRUE(F(r3, Z8000::F_Z));
}

TEST(Z8000Arith, Mult) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R1,#-3\n  LD R2,#100\n  MULT RR0,R2");
    EXPECT_EQ(r.cpu.rr(0), 0xFFFFFED4u);
    EXPECT_TRUE(F(r, Z8000::F_S)); EXPECT_FALSE(F(r, Z8000::F_C)); EXPECT_FALSE(F(r, Z8000::F_PV));
    Rig r2(M::Z8002);
    runNonseg(r2, "  LD R1,#300\n  MULT RR0,#300");
    EXPECT_EQ(r2.cpu.rr(0), 90000u);
    EXPECT_TRUE(F(r2, Z8000::F_C));
}

TEST(Z8000Arith, MultlUndTakte) {
    Rig r(M::Z8002);
    r.load("  NONSEG\n  ORG %0100\n  MULTL RQ0,RR4\n  HALT\n", false);
    r.boot(SYS, 0, 0x100);
    r.cpu.setRR(2, 0x00010000); r.cpu.setRR(4, 0x00010000);
    int c = r.cpu.step();
    EXPECT_EQ(r.cpu.rq(0), 0x0000000100000000ull);
    EXPECT_TRUE(F(r, Z8000::F_C));
    EXPECT_EQ(c, 282 + 7 * 0);          // untere 16 Bit des Multiplikanden: keine Eins
    r.cpu.pc = 0x100; r.cpu.setRR(2, 0x00000007); r.cpu.setRR(4, 3);
    c = r.cpu.step();
    EXPECT_EQ(r.cpu.rq(0), 21u);
    EXPECT_EQ(c, 282 + 7 * 3);
}

TEST(Z8000Arith, DivHandbuchbeispiel) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R0,#0\n  LD R1,#%22\n  LD R3,#6\n  DIV RR0,R3");
    EXPECT_EQ(r.cpu.rr(0), 0x00040005u);
    EXPECT_FALSE(F(r, Z8000::F_PV)); EXPECT_FALSE(F(r, Z8000::F_C));
}

TEST(Z8000Arith, DivAlleVierFaelle) {
    auto div = [](int32_t dividend, int16_t divisor, uint32_t& rr0, uint16_t& fcw, int& cyc) {
        Rig r(M::Z8002);
        r.load("  NONSEG\n  ORG %0100\n  DIV RR0,R3\n  HALT\n", false);
        r.boot(SYS, 0, 0x100);
        r.cpu.setRR(0, uint32_t(dividend)); r.cpu.setR(3, uint16_t(divisor));
        cyc = r.cpu.step();
        rr0 = r.cpu.rr(0); fcw = r.cpu.fcw;
    };
    uint32_t v; uint16_t f; int c;
    div(-7, 2, v, f, c);                           // Fall 1, Rest mit Vorzeichen des Dividenden
    EXPECT_EQ(v, 0xFFFFFFFDu);                     // R0 = -1 (Rest), R1 = -3
    EXPECT_TRUE(f & Z8000::F_S); EXPECT_FALSE(f & Z8000::F_PV);
    EXPECT_EQ(c, 107);
    div(1234, 0, v, f, c);                         // Fall 2: /0
    EXPECT_EQ(v, 1234u);
    EXPECT_TRUE(f & Z8000::F_PV); EXPECT_TRUE(f & Z8000::F_Z);
    EXPECT_FALSE(f & Z8000::F_C); EXPECT_FALSE(f & Z8000::F_S);
    EXPECT_EQ(c, 107 - 94);
    div(200000, 1, v, f, c);                       // Fall 3: Ziel bleibt
    EXPECT_EQ(v, 200000u);
    EXPECT_TRUE(f & Z8000::F_PV); EXPECT_FALSE(f & Z8000::F_C); EXPECT_FALSE(f & Z8000::F_Z);
    div(40000, 1, v, f, c);                        // Fall 4: Quotient ohne Vorzeichenbit
    EXPECT_EQ(v, 0x00009C40u);
    EXPECT_TRUE(f & Z8000::F_PV); EXPECT_TRUE(f & Z8000::F_C); EXPECT_FALSE(f & Z8000::F_S);
    div(-40000, 1, v, f, c);
    EXPECT_EQ(v & 0xFFFF, uint32_t(uint16_t(-40000)));
    EXPECT_TRUE(f & Z8000::F_C); EXPECT_TRUE(f & Z8000::F_S);
}

TEST(Z8000Arith, Divl) {
    Rig r(M::Z8002);
    r.load("  NONSEG\n  ORG %0100\n  DIVL RQ0,RR4\n  HALT\n", false);
    r.boot(SYS, 0, 0x100);
    r.cpu.setRQ(0, 100); r.cpu.setRR(4, 7);
    r.cpu.step();
    EXPECT_EQ(r.cpu.rr(2), 14u); EXPECT_EQ(r.cpu.rr(0), 2u);
    r.cpu.pc = 0x100; r.cpu.setRQ(0, uint64_t(-100)); r.cpu.setRR(4, 7);
    r.cpu.step();
    EXPECT_EQ(r.cpu.rr(2), uint32_t(-14)); EXPECT_EQ(r.cpu.rr(0), uint32_t(-2));
    EXPECT_TRUE(F(r, Z8000::F_S));
    r.cpu.pc = 0x100; r.cpu.setRQ(0, 0x8000000000000000ull); r.cpu.setRR(4, 0xFFFFFFFF);  // Überlauf
    int c = r.cpu.step();
    EXPECT_TRUE(F(r, Z8000::F_PV)); EXPECT_FALSE(F(r, Z8000::F_C));
    EXPECT_EQ(c, 744 - 693);
    r.cpu.pc = 0x100; r.cpu.setRR(4, 0);
    c = r.cpu.step();
    EXPECT_TRUE(F(r, Z8000::F_Z)); EXPECT_TRUE(F(r, Z8000::F_PV));
    EXPECT_EQ(c, 744 - 714);
}

TEST(Z8000Arith, ExtendSign) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDL RR2,#%12345678\n  EXTS RR2\n  LD R4,#%0080\n  EXTSB R4\n"
                 "  LDL RR8,#0\n  LDL RR10,#%80000000\n  EXTSL RQ8");
    EXPECT_EQ(r.cpu.rr(2), 0x00005678u);   // Handbuch
    EXPECT_EQ(r.cpu.r(4), 0xFF80);
    EXPECT_EQ(r.cpu.rr(8), 0xFFFFFFFFu);
}

// ═════════════════════════════════════════════════════════════════════════════
// Logik, Bits, Schieben
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Logic, AndbHandbuchParitaet) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RL3,#%E7\n  RESFLG C,Z,S,P\n  ANDB RL3,#%CE");
    EXPECT_EQ(r.cpu.rb(11), 0xC6);
    EXPECT_TRUE(F(r, Z8000::F_S)); EXPECT_TRUE(F(r, Z8000::F_PV));
    EXPECT_FALSE(F(r, Z8000::F_Z)); EXPECT_FALSE(F(r, Z8000::F_C));
}

TEST(Z8000Logic, WortLogikLaesstPStehen) {
    Rig r(M::Z8002);
    runNonseg(r, "  SETFLG P\n  LD R1,#%00FF\n  XOR R1,#%00FF\n  COM R2");
    EXPECT_EQ(r.cpu.r(1), 0);
    EXPECT_TRUE(F(r, Z8000::F_PV));        // XOR/COM (Wort): P unverändert
}

TEST(Z8000Logic, BitResSetStatischUndDynamisch) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RH2,#%B2\n  BITB RH2,#0\n  LD R5,#%FFFF\n  LD R6,#3\n  RES R5,R6\n"
                 "  LD R7,#0\n  SET R7,#15");
    EXPECT_TRUE(F(r, Z8000::F_Z));          // Handbuch BITB
    EXPECT_EQ(r.cpu.r(5), 0xFFF7);
    EXPECT_EQ(r.cpu.r(7), 0x8000);
}

TEST(Z8000Logic, TsetUndTest) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R1,#%0300\n  CLRB @R1\n  TSETB @R1\n  TCC MI,R9\n  TSETB @R1\n  TCC MI,R10\n"
                 "  TESTB @R1");
    EXPECT_EQ(r.b(0, 0x300), 0xFF);
    EXPECT_EQ(r.cpu.r(9) & 1, 0);
    EXPECT_EQ(r.cpu.r(10) & 1, 1);
    EXPECT_TRUE(F(r, Z8000::F_S)); EXPECT_TRUE(F(r, Z8000::F_PV));
}

TEST(Z8000Shift, RotierenHandbuchUndV) {
    Rig r(M::Z8002);
    runNonseg(r, "  RESFLG C\n  LD R0,#%800F\n  RLC R0,#2");
    EXPECT_EQ(r.cpu.r(0), 0x003D);          // Handbuch RLC
    EXPECT_FALSE(F(r, Z8000::F_C));
    EXPECT_TRUE(F(r, Z8000::F_PV));          // Vorzeichen hat gewechselt
    Rig r2(M::Z8002);
    runNonseg(r2, "  LDB RL0,#%01\n  RRB RL0\n  LD R1,#%4000\n  RR R1,#2");
    EXPECT_EQ(r2.cpu.rb(8), 0x80);
    EXPECT_EQ(r2.cpu.r(1), 0x1000);
    EXPECT_FALSE(F(r2, Z8000::F_C));
}

TEST(Z8000Shift, StatischHandbuchSlal) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDL RR2,#%1234ABCD\n  SLAL RR2,#8");
    EXPECT_EQ(r.cpu.rr(2), 0x34ABCD00u);
    EXPECT_FALSE(F(r, Z8000::F_C));
    EXPECT_TRUE(F(r, Z8000::F_PV));   // Vorzeichen änderte sich unterwegs (Bit 31 von 0x92.. usw.)
}

TEST(Z8000Shift, RechtsArithmetischUndLogisch) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R1,#%8003\n  SRA R1,#1\n  LD R2,#%8003\n  SRL R2,#2\n  LDB RH3,#%81\n  SRAB RH3,#7");
    EXPECT_EQ(r.cpu.r(1), 0xC001);
    EXPECT_EQ(r.cpu.r(2), 0x2000);
    EXPECT_EQ(r.cpu.rb(3), 0xFF);
    EXPECT_FALSE(F(r, Z8000::F_PV));   // SRA: V gelöscht
    EXPECT_FALSE(F(r, Z8000::F_C));    // zuletzt hinausgeschoben: Bit 6 von %81 = 0
}

TEST(Z8000Shift, DynamischRichtungAusVorzeichen) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R1,#%00F0\n  LD R2,#-4\n  SDL R1,R2\n  LD R3,#%0001\n  LD R4,#15\n  SDA R3,R4\n"
                 "  LDL RR6,#%80000000\n  LD R8,#-31\n  SDAL RR6,R8");
    EXPECT_EQ(r.cpu.r(1), 0x000F);
    EXPECT_EQ(r.cpu.r(3), 0x8000);
    EXPECT_EQ(r.cpu.rr(6), 0xFFFFFFFFu);
}

TEST(Z8000Shift, TakteJeStelle) {
    Rig r(M::Z8002);
    r.load("  NONSEG\n  ORG %0100\n  SLL R1,#5\n  SDL R1,R2\n  HALT\n", false);
    r.boot(SYS, 0, 0x100);
    r.cpu.setR(2, uint16_t(-3));
    EXPECT_EQ(r.cpu.step(), 13 + 3 * 5);
    EXPECT_EQ(r.cpu.step(), 15 + 3 * 3);
}

TEST(Z8000Shift, Ziffernrotieren) {
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RL1,#%34\n  LDB RL2,#%56\n  RLDB RL1,RL2\n  LDB RH1,#%34\n  LDB RH2,#%56\n  RRDB RH1,RH2");
    EXPECT_EQ(r.cpu.rb(9), 0x35);  EXPECT_EQ(r.cpu.rb(10), 0x64);
    EXPECT_EQ(r.cpu.rb(1), 0x36);  EXPECT_EQ(r.cpu.rb(2), 0x45);
}

// ═════════════════════════════════════════════════════════════════════════════
// Laden, Stapel
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Load, LdaSegmentiertHandbuch) {
    Rig r;
    runSeg(r, "  LD R4,#%20\n  LDA RR2,<<3>>%0008(R4)\n  LDL RR4,#%01000020\n  LDA RR6,RR4(#8)");
    EXPECT_EQ(r.cpu.rr(2), 0x03000028u);
    EXPECT_EQ(r.cpu.rr(6), 0x01000028u);
}

TEST(Z8000Load, AdressierungsartenSegmentiert) {
    Rig r;
    r.setW(5, 0x1234, 0x1111);
    r.setW(5, 0x0012, 0x2222);
    r.setW(6, 0x0104, 0x3333);
    r.setW(6, 0x0110, 0x4444);
    r.setW(5, 0x1240, 0x5555);
    runSeg(r, "  LD R1,<<5>>%1234\n  LD R2,|<<5>>%12|\n  LDL RR8,#%06000100\n  LD R3,RR8(#4)\n"
              "  LD R10,#%10\n  LD R4,RR8(R10)\n  LD R5,<<5>>%1230(R10)");
    EXPECT_EQ(r.cpu.r(1), 0x1111);
    EXPECT_EQ(r.cpu.r(2), 0x2222);
    EXPECT_EQ(r.cpu.r(3), 0x3333);
    EXPECT_EQ(r.cpu.r(4), 0x4444);
    EXPECT_EQ(r.cpu.r(5), 0x5555);
}

TEST(Z8000Load, SpeichernUndSofortwerte) {
    Rig r;
    runSeg(r, "  LDL RR2,#%02000300\n  LD R4,#%ABCD\n  LD @RR2,R4\n  LDB RR2(#3),RL4\n"
              "  LD <<2>>%0400,#%5555\n  LDL RR6,#%11223344\n  LDL <<2>>%0500,RR6\n  LDB RH4,#%77\n");
    EXPECT_EQ(r.w(2, 0x300), 0xABCD);
    EXPECT_EQ(r.b(2, 0x303), 0xCD);
    EXPECT_EQ(r.w(2, 0x400), 0x5555);
    EXPECT_EQ(r.w(2, 0x500), 0x1122); EXPECT_EQ(r.w(2, 0x502), 0x3344);
    EXPECT_EQ(r.cpu.r(4), 0x77CD);
}

TEST(Z8000Load, LdrLiestMitProgrammstatus) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LDR R1,DATEN\n  HALT\nDATEN: DW %4242\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.step();
    EXPECT_EQ(r.cpu.r(1), 0x4242);
    ASSERT_GE(r.log.size(), 3u);
    EXPECT_EQ(r.log[0].first.st, Z8kStatus::MemInstrFirst);
    EXPECT_EQ(r.log[1].first.st, Z8kStatus::MemInstr);   // Verschiebung
    EXPECT_EQ(r.log[2].first.st, Z8kStatus::MemInstr);   // Daten: „Program Reference" (1100)
}

TEST(Z8000Load, LdmUndEx) {
    Rig r;
    r.setW(0, 0x800, 1); r.setW(0, 0x802, 2); r.setW(0, 0x804, 3); r.setW(0, 0x806, 4);
    runSeg(r, "  LDL RR2,#%00000800\n  LDM R8,@RR2,#4\n  LD R1,#%AAAA\n  EX R1,@RR2\n  LDL RR4,#%00000900\n"
              "  LDM @RR4,R8,#2\n  LD R6,#1\n  LD R7,#2\n  EX R6,R7");
    EXPECT_EQ(r.cpu.r(8), 1); EXPECT_EQ(r.cpu.r(11), 4);
    EXPECT_EQ(r.cpu.r(1), 1); EXPECT_EQ(r.w(0, 0x800), 0xAAAA);
    EXPECT_EQ(r.w(0, 0x900), 1); EXPECT_EQ(r.w(0, 0x902), 2);
    EXPECT_EQ(r.cpu.r(6), 2); EXPECT_EQ(r.cpu.r(7), 1);
}

TEST(Z8000Load, PushPopHandbuchUndStatus) {
    Rig r(M::Z8002);
    r.setW(0, 0x1000, 0x0055);
    runNonseg(r, "  LD R12,#%1000\n  LD R3,#%22\n  POP R3,@R12");
    EXPECT_EQ(r.cpu.r(3), 0x0055); EXPECT_EQ(r.cpu.r(12), 0x1002);
    // Stapelstatus nur, wenn der Zeiger der Stapelzeiger ist (§5.4.3).
    Rig s;
    s.load("  SEG\n  ORG <<0>>%0100\n  PUSH @RR14,#%1234\n  PUSH @RR2,#%5678\n  PUSHL @RR14,RR4\n  POPL RR6,@RR14\n  HALT\n", true);
    s.boot(SEGSYS, 0, 0x100);
    s.cpu.R14[1] = 0; s.cpu.R15[1] = 0xF000;
    s.cpu.setRR(2, 0x00002000); s.cpu.setRR(4, 0xDEADBEEF);
    s.cpu.step();
    EXPECT_EQ(s.count(Z8kStatus::MemStack), 1);
    EXPECT_EQ(s.w(0, 0xEFFE), 0x1234);
    s.log.clear(); s.cpu.step();
    EXPECT_EQ(s.count(Z8kStatus::MemStack), 0);
    EXPECT_EQ(s.count(Z8kStatus::MemData), 1);
    EXPECT_EQ(s.w(0, 0x1FFE), 0x5678);
    s.cpu.step(); s.cpu.step();
    EXPECT_EQ(s.cpu.rr(6), 0xDEADBEEFu);
    EXPECT_EQ(s.cpu.R15[1], 0xEFFE);
}

TEST(Z8000Load, LdpsSegmentiertUndUnsegmentiert) {
    Rig r;
    r.setW(0, 0x802, 0xC000); r.setW(0, 0x804, 0x0200); r.setW(0, 0x806, 0x0300);
    r.setW(2, 0x300, 0x7A00);   // HALT
    runSeg(r, "  LDL RR2,#%00000800\n  LDPS @RR2");
    EXPECT_EQ(r.cpu.pcSeg, 2);
    EXPECT_EQ(r.cpu.pc, 0x0302);
    Rig u(M::Z8002);
    u.setW(0, 0x800, 0x4000); u.setW(0, 0x802, 0x0300);
    u.setW(0, 0x300, 0x7A00);
    runNonseg(u, "  LD R2,#%0800\n  LDPS @R2");
    EXPECT_EQ(u.cpu.pc, 0x0302);
}

TEST(Z8000Load, LdctlSteuerregister) {
    Rig r;
    runSeg(r, "  LD R0,#%1F00\n  LDCTL PSAPSEG,R0\n  LD R0,#%12AB\n  LDCTL PSAPOFF,R0\n"
              "  LD R0,#%5555\n  LDCTL NSPOFF,R0\n  LD R0,#%0700\n  LDCTL NSPSEG,R0\n"
              "  LDCTL R1,FCW\n  LDCTL R2,PSAPOFF\n  LDCTL R3,NSPOFF\n  LDCTLB RL4,FLAGS\n"
              "  LD R0,#%C0F0\n  LDCTL FCW,R0\n");
    EXPECT_EQ(r.cpu.psapSeg, 0x1F00);
    EXPECT_EQ(r.cpu.psapOff, 0x1200);
    EXPECT_EQ(r.cpu.R15[0], 0x5555);
    EXPECT_EQ(r.cpu.R14[0], 0x0700);
    EXPECT_EQ(r.cpu.r(2), 0x1200);
    EXPECT_EQ(r.cpu.r(3), 0x5555);
    EXPECT_EQ(r.cpu.fcw, 0xC0F0);
}

TEST(Z8000Load, FlagbefehleUndDiEi) {
    Rig r;
    runSeg(r, "  SETFLG C,Z,S,P\n  COMFLG Z\n  RESFLG S\n  EI VI,NVI\n  DI VI", SEGSYS);
    EXPECT_TRUE(F(r, Z8000::F_C)); EXPECT_FALSE(F(r, Z8000::F_Z));
    EXPECT_FALSE(F(r, Z8000::F_S)); EXPECT_TRUE(F(r, Z8000::F_PV));
    EXPECT_FALSE(F(r, Z8000::FCW_VIE)); EXPECT_TRUE(F(r, Z8000::FCW_NVIE));  // Handbuch DI VI
}

// ═════════════════════════════════════════════════════════════════════════════
// Sprünge
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Jump, CallHandbuchUnsegmentiert) {
    Rig r(M::Z8002);
    r.setW(0, 0x1000, 0x5F00); r.setW(0, 0x1002, 0x2520);   // CALL %2520
    r.setW(0, 0x2520, 0x7A00);
    r.boot(SYS, 0, 0x1000);
    r.cpu.R15[1] = 0x3002;
    int c = r.cpu.step();
    EXPECT_EQ(r.cpu.R15[1], 0x3000);
    EXPECT_EQ(r.w(0, 0x3000), 0x1004);
    EXPECT_EQ(r.cpu.pc, 0x2520);
    EXPECT_EQ(c, 12);
}

TEST(Z8000Jump, CallRetSegmentiertUeberSegmentgrenze) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  CALL <<4>>%0200\n  HALT\n  ORG <<4>>%0200\n  LD R1,#7\n  RET\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    int c = r.cpu.step();
    EXPECT_EQ(c, 20);
    EXPECT_EQ(r.cpu.pcSeg, 4);
    EXPECT_EQ(r.w(0, 0xEFFC), 0x0000);   // PC-Segment
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0106);   // PC-Offset
    r.runToHalt();
    EXPECT_EQ(r.cpu.pcSeg, 0);
    EXPECT_EQ(r.cpu.r(1), 7);
    EXPECT_EQ(r.cpu.R15[1], 0xF000);
}

TEST(Z8000Jump, JrDjnzCalrUndTakte) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R0,#0\n  LD R1,#5\nL1: ADD R0,R1\n  DJNZ R1,L1\n  CALR UP\n  JR ENDE\nUP: INC R0,#16\n  RET\nENDE:");
    EXPECT_EQ(r.cpu.r(0), 15 + 16);
    Rig t(M::Z8002);
    t.load("  NONSEG\n  ORG %0100\n  JR Z,%0100\n  JP NZ,%0100\n  RET Z\n  HALT\n", false);
    t.boot(SYS, 0, 0x100);
    t.cpu.fcw = SYS;   // Z = 0
    EXPECT_EQ(t.cpu.step(), 6);   // JR nicht genommen
    EXPECT_EQ(t.cpu.step(), 7);   // JP DA genommen (7/7)
    t.cpu.pc = 0x106;
    EXPECT_EQ(t.cpu.step(), 7);   // RET nicht genommen
}

TEST(Z8000Jump, JpIndirektSegmentiert) {
    Rig r;
    r.setW(3, 0x0040, 0x7A00);
    runSeg(r, "  LDL RR4,#%03000040\n  JP @RR4");
    EXPECT_EQ(r.cpu.pcSeg, 3);
    EXPECT_EQ(r.cpu.pc, 0x42);
}

TEST(Z8000Jump, UnsegmentiertBleibtImPcSegment) {
    // Z8001 unsegmentiert: jede Adresse bekommt das Segment des PC (§4.3.2).
    Rig r;
    r.load("  NONSEG\n  ORG %0100\n  LD R1,%0200\n  HALT\n", false);
    for (uint16_t a = 0x100; a < 0x110; a += 2) r.setW(3, a, r.w(0, a));
    r.setW(3, 0x200, 0x9999);
    r.boot(SYS, 3, 0x100);
    r.cpu.step();
    EXPECT_EQ(r.cpu.r(1), 0x9999);
    for (auto& e : r.log) EXPECT_EQ(e.first.seg, 3);
}

// ═════════════════════════════════════════════════════════════════════════════
// Blockbefehle
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Block, LddHandbuch) {
    Rig r(M::Z8002);
    r.setW(0, 0x404A, 0xFFFF);
    runNonseg(r, "  LD R1,#%202A\n  LD R2,#%404A\n  LD R3,#5\n  LDD @R1,@R2,R3");
    EXPECT_EQ(r.w(0, 0x202A), 0xFFFF);
    EXPECT_EQ(r.cpu.r(1), 0x2028); EXPECT_EQ(r.cpu.r(2), 0x4048); EXPECT_EQ(r.cpu.r(3), 4);
    EXPECT_FALSE(F(r, Z8000::F_PV));
}

TEST(Z8000Block, LdirKopiertUndTakte) {
    Rig r;
    for (int i = 0; i < 8; ++i) r.setB(1, uint16_t(0x100 + i), uint8_t(i + 1));
    r.load("  SEG\n  ORG <<0>>%0100\n  LDIRB @RR2,@RR4,R6\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setRR(2, 0x02000200); r.cpu.setRR(4, 0x01000100); r.cpu.setR(6, 8);
    uint64_t c = r.runToHalt() - 8;   // HALT
    EXPECT_EQ(c, uint64_t(11 + 9 * 8));
    for (int i = 0; i < 8; ++i) EXPECT_EQ(r.b(2, uint16_t(0x200 + i)), i + 1);
    EXPECT_EQ(r.cpu.r(6), 0);
    EXPECT_TRUE(F(r, Z8000::F_PV));
    EXPECT_EQ(r.cpu.rr(2), 0x02000208u);
    EXPECT_EQ(r.count(Z8kStatus::MemInstrFirst), 2);   // LDIRB wird nur einmal geholt (+ HALT)
}

TEST(Z8000Block, CpdbHandbuch) {
    Rig r(M::Z8002);
    r.setB(0, 0x4001, 0x00);
    runNonseg(r, "  LDB RH0,#%FF\n  LD R1,#%4001\n  LD R3,#5\n  CPDB RH0,@R1,R3,EQ");
    EXPECT_FALSE(F(r, Z8000::F_Z));
    EXPECT_EQ(r.cpu.r(1), 0x4000); EXPECT_EQ(r.cpu.r(3), 4);
}

TEST(Z8000Block, CpsdbHandbuch) {
    Rig r(M::Z8002);
    r.setB(0, 0x2000, 0xFF); r.setB(0, 0x3000, 0x00);
    runNonseg(r, "  LD R2,#%2000\n  LD R3,#%3000\n  LD R4,#1\n  CPSDB @R2,@R3,R4,UGE");
    EXPECT_TRUE(F(r, Z8000::F_Z)); EXPECT_TRUE(F(r, Z8000::F_PV));
    EXPECT_EQ(r.cpu.r(2), 0x1FFF); EXPECT_EQ(r.cpu.r(3), 0x2FFF);
}

TEST(Z8000Block, CpirbFindetZeichen) {
    Rig r(M::Z8002);
    const char* s = "ABC\rDEF";
    for (int i = 0; s[i]; ++i) r.setB(0, uint16_t(0x500 + i), uint8_t(s[i]));
    runNonseg(r, "  LD R1,#%0500\n  LD R3,#7\n  LDB RL0,#%0D\n  CPIRB RL0,@R1,R3,EQ");
    EXPECT_TRUE(F(r, Z8000::F_Z));
    EXPECT_EQ(r.cpu.r(1), 0x504);
    EXPECT_EQ(r.cpu.r(3), 3);
    EXPECT_FALSE(F(r, Z8000::F_PV));
}

TEST(Z8000Block, TranslateUndTest) {
    Rig r(M::Z8002);
    for (int i = 0; i < 256; ++i) r.setB(0, uint16_t(0x1000 + i), uint8_t(i ^ 0x20));
    r.setB(0, 0x2000, 'a'); r.setB(0, 0x2001, 'b');
    for (int i = 0; i < 256; ++i) r.setB(0, uint16_t(0x1100 + i), 0);
    r.setB(0, uint16_t(0x1100 + ','), 9);
    const char* s = "ab,c";
    for (int i = 0; s[i]; ++i) r.setB(0, uint16_t(0x3000 + i), uint8_t(s[i]));
    runNonseg(r, "  LD R2,#%2000\n  LD R3,#%1000\n  LD R4,#2\n  TRIRB @R2,@R3,R4\n"
                 "  LD R5,#%3000\n  LD R6,#%1100\n  LD R7,#4\n  TRTIRB @R5,@R6,R7");
    EXPECT_EQ(r.b(0, 0x2000), 'A'); EXPECT_EQ(r.b(0, 0x2001), 'B');
    EXPECT_EQ(r.cpu.r(5), 0x3003);        // stoppt hinter dem Komma
    EXPECT_EQ(r.cpu.rb(1), 9);            // RH1 = Übersetzungswert
    EXPECT_FALSE(F(r, Z8000::F_Z));
    EXPECT_EQ(r.cpu.r(7), 1);
}

TEST(Z8000Block, InibHandbuchUndSindrbHandbuch) {
    Rig r(M::Z8002);
    r.io[0x0229] = 0x00B9;
    r.sio[0x0AFC] = 0x5A00;               // Spezial-E/A: Byte auf AD8..15
    runNonseg(r, "  LD R4,#%4000\n  LD R6,#%0229\n  LD R0,#%16\n  INIB @R4,@R6,R0\n"
                 "  LD R1,#%202A\n  LD R2,#%0AFC\n  LD R3,#8\n  SINDRB @R1,@R2,R3");
    EXPECT_EQ(r.b(0, 0x4000), 0xB9); EXPECT_EQ(r.cpu.r(4), 0x4001); EXPECT_EQ(r.cpu.r(0), 0x15);
    for (int a = 0x2023; a <= 0x202A; ++a) EXPECT_EQ(r.b(0, uint16_t(a)), 0x5A);
    EXPECT_EQ(r.cpu.r(1), 0x2022); EXPECT_EQ(r.cpu.r(3), 0); EXPECT_TRUE(F(r, Z8000::F_PV));
}

TEST(Z8000Block, IndSegmentiertHandbuch) {
    Rig r;
    r.io[0x0228] = 0x7788;
    runSeg(r, "  LDL RR4,#%02004000\n  LD R6,#%0228\n  LD R0,#%16\n  IND @RR4,@R6,R0");
    EXPECT_EQ(r.w(2, 0x4000), 0x7788);
    EXPECT_EQ(r.cpu.rr(4), 0x02003FFEu);
    EXPECT_EQ(r.cpu.r(0), 0x15);
}

TEST(Z8000Block, OtirbGibtAus) {
    Rig r(M::Z8002);
    r.setB(0, 0x600, 1); r.setB(0, 0x601, 2); r.setB(0, 0x602, 3);
    runNonseg(r, "  LD R1,#%0041\n  LD R2,#%0600\n  LD R3,#3\n  OTIRB @R1,@R2,R3");
    ASSERT_EQ(r.ioWrites.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(r.ioWrites[size_t(i)].first.addr, 0x41);
        EXPECT_EQ(r.ioWrites[size_t(i)].second & 0xFF, i + 1);
    }
}

TEST(Z8000Block, LdirIstUnterbrechbarUndLaeuftNachIretWeiter) {
    Rig r;
    for (int i = 0; i < 6; ++i) r.setW(1, uint16_t(0x100 + 2 * i), uint16_t(0x1000 + i));
    r.load("  SEG\n  ORG <<0>>%0100\n  LDIR @RR2,@RR4,R6\n  HALT\n"
           "  ORG <<0>>%2000\n  INC R9\n  IRET\n", true);
    r.boot(0xD000, 0, 0x100);                    // SEG | SN | VIE
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.cpu.psapOff = 0x1000;
    r.setW(0, 0x103A, SEGSYS);                   // VI-FCW
    r.setW(0, 0x103C + 2 * 4, 0x0000); r.setW(0, 0x103E + 2 * 4, 0x2000);   // Vektor 4
    r.cpu.setRR(2, 0x02000200); r.cpu.setRR(4, 0x01000100); r.cpu.setR(6, 6);
    r.cpu.step(); r.cpu.step(); r.cpu.step();     // drei Durchläufe
    EXPECT_TRUE(r.cpu.inRepeat());
    EXPECT_EQ(r.cpu.r(6), 3);
    r.ackValue = 0x0004;
    r.cpu.setVI(true);
    int c = r.cpu.step();                         // Interrupt statt 4. Durchlauf
    r.cpu.setVI(false);
    EXPECT_FALSE(r.cpu.inRepeat());
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0100);            // gesicherter PC = der LDIR selbst
    EXPECT_EQ(r.w(0, 0xEFF8), 0x0004);            // Kennung
    EXPECT_GE(c, 7);
    r.runToHalt();
    EXPECT_EQ(r.cpu.r(9), 1);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(r.w(2, uint16_t(0x200 + 2 * i)), 0x1000 + i);
    EXPECT_EQ(r.cpu.r(6), 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Traps
// ═════════════════════════════════════════════════════════════════════════════

namespace {
/// Z8001 mit PSA bei <<0>>%1000, Systemstapel <<0>>%F000, Normalstapel <<0>>%8000.
void trapRig(Rig& r, const std::string& main, uint16_t fcw) {
    r.load("  SEG\n  ORG <<0>>%0100\n" + main + "\n  ORG <<0>>%3000\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.psapSeg = 0; r.cpu.psapOff = 0x1000;
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.cpu.R14[0] = 0; r.cpu.R15[0] = 0x8000;
    for (uint16_t e : {Z8000::PSA_EPA, Z8000::PSA_PRIV, Z8000::PSA_SC, Z8000::PSA_NMI, Z8000::PSA_NVI})
        r.psa(0, 0x1000, e, SEGSYS, 0, 0x3000);
    r.cpu.fcw = fcw;
}
} // namespace

TEST(Z8000Trap, PrivilegierterBefehlImNormalmodus) {
    Rig r;
    trapRig(r, "  IN R1,@R2", 0x8000);    // segmentiert, Normal
    const uint16_t w0 = r.w(0, 0x100);
    r.cpu.step();
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.cpu.fcw, SEGSYS);
    EXPECT_EQ(r.cpu.R15[1], 0xF000 - 8);         // Systemstapel
    EXPECT_EQ(r.cpu.R15[0], 0x8000);             // Normalstapel unberührt
    EXPECT_EQ(r.w(0, 0xEFF8), w0);               // Kennung = erstes Befehlswort
    EXPECT_EQ(r.w(0, 0xEFFA), 0x8000);           // alte FCW
    EXPECT_EQ(r.w(0, 0xEFFC), 0x0000);           // PC-Segment
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0102);           // Wort hinter dem ersten
    EXPECT_EQ(r.count(Z8kStatus::Io), 0);        // nicht ausgeführt
    // Normalmodus-Zugriffe laufen mit N/S = Normal, der Trap-Rahmen mit System
    EXPECT_FALSE(r.log[0].first.system);
    for (auto& e : r.log) if (e.first.st == Z8kStatus::MemStack) EXPECT_TRUE(e.first.system);
}

TEST(Z8000Trap, SystemCall) {
    Rig r;
    trapRig(r, "  SC #%42", 0x8000);
    int c = r.cpu.step();
    EXPECT_EQ(c, 39);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.w(0, 0xEFF8), 0x7F42);
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0102);           // nächster Befehl
    // IRET zurück in den Normalmodus
    r.setW(0, 0x3000, 0x7B00);                   // IRET
    r.cpu.step();
    EXPECT_EQ(r.cpu.fcw, 0x8000);
    EXPECT_EQ(r.cpu.pc, 0x0102);
    EXPECT_EQ(r.cpu.R15[1], 0xF000);
}

TEST(Z8000Trap, EpaBefehlOhneEpaBitTrapt) {
    Rig r;
    trapRig(r, "  DW %8E12,%3456\n  HALT", SEGSYS);
    r.cpu.step();
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.w(0, 0xEFF8), 0x8E12);
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0102);           // zweites Wort des Befehls
}

TEST(Z8000Trap, EpaBefehlMitEpaBitIstNopDerRichtigenLaenge) {
    Rig r;
    trapRig(r, "  DW %4E01,%3456,%8200,%1234\n  HALT", uint16_t(SEGSYS | Z8000::FCW_EPA));
    r.cpu.step();
    EXPECT_EQ(r.cpu.pc, 0x0108);                 // 2 Worte + lange Segmentadresse
    r.cpu.step();
    EXPECT_TRUE(r.cpu.halted());
}

TEST(Z8000Trap, UnbelegteKodierungMeldetUndLaeuftWeiter) {
    int n = 0; uint16_t w0 = 0;
    for (uint32_t w = 0; w < 0x10000; ++w) {
        z8k::Table::get().candidates(uint16_t(w), n);
        if (n == 0) { w0 = uint16_t(w); break; }
    }
    ASSERT_EQ(n, 0);
    Rig r;
    r.setW(0, 0x100, w0); r.setW(0, 0x102, 0x7A00);
    r.boot(SEGSYS, 0, 0x100);
    int seen = 0;
    r.cpu.onIllegal = [&](uint8_t, uint16_t pc, uint16_t w) { ++seen; EXPECT_EQ(pc, 0x100); EXPECT_EQ(w, w0); };
    r.runToHalt();
    EXPECT_EQ(seen, 1);
    EXPECT_EQ(r.cpu.illegalCount(), 1u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Interrupts
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Irq, VektorInterruptRahmenQuittungUndIret) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  NOP\n  NOP\n  HALT\n  ORG <<0>>%2000\n  INC R5\n  IRET\n", true);
    r.boot(0xD000, 0, 0x100);
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.cpu.psapOff = 0x1000;
    r.setW(0, 0x103A, SEGSYS);
    r.setW(0, uint16_t(0x103C + 2 * 0x12), 0x0000);
    r.setW(0, uint16_t(0x103E + 2 * 0x12), 0x2000);
    r.ackValue = 0xAB12;                        // Status-8 (high) + Vektor (low)
    r.cpu.setVI(true);
    r.cpu.step();
    r.cpu.setVI(false);
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.cpu.fcw, SEGSYS);
    EXPECT_EQ(r.w(0, 0xEFF8), 0xAB12);
    EXPECT_EQ(r.w(0, 0xEFFA), 0xD000);
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0100);
    // Reihenfolge auf dem Bus: Scheinholen, Quittung, 4 Stapelschreibzugriffe, 3 PSA-Lesezugriffe
    ASSERT_GE(r.log.size(), 9u);
    EXPECT_EQ(r.log[0].first.st, Z8kStatus::MemInstrFirst);
    EXPECT_EQ(r.log[1].first.st, Z8kStatus::ViAck);
    EXPECT_TRUE(r.log[1].first.system);
    EXPECT_EQ(r.count(Z8kStatus::MemStack), 4);
    for (size_t i = 6; i < 9; ++i) EXPECT_EQ(r.log[i].first.st, Z8kStatus::MemInstr);
    r.runToHalt();
    EXPECT_EQ(r.cpu.r(5), 1);
    EXPECT_EQ(r.cpu.fcw, 0xD000);
    EXPECT_EQ(r.cpu.R15[1], 0xF000);
}

TEST(Z8000Irq, MaskierteInterruptsWerdenNichtAngenommen) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  NOP\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);                    // VIE = NVIE = 0
    r.cpu.setVI(true); r.cpu.setNVI(true);
    r.runToHalt();
    EXPECT_EQ(r.count(Z8kStatus::ViAck) + r.count(Z8kStatus::NviAck), 0);
}

TEST(Z8000Irq, NmiIstFlankengetriggertUndHatVorrang) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  NOP\n  NOP\n  NOP\n  HALT\n  ORG <<0>>%2000\n  INC R7\n  IRET\n", true);
    r.boot(0xD800, 0, 0x100);                    // VIE + NVIE
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.cpu.psapOff = 0x1000;
    r.psa(0, 0x1000, Z8000::PSA_NMI, SEGSYS, 0, 0x2000);
    r.psa(0, 0x1000, Z8000::PSA_NVI, SEGSYS, 0, 0x2000);
    r.cpu.setNMI(true);
    r.cpu.setNVI(true);
    r.cpu.step();
    EXPECT_EQ(r.count(Z8kStatus::NmiAck), 1);
    EXPECT_EQ(r.count(Z8kStatus::NviAck), 0);
    r.cpu.setNVI(false);
    r.runToHalt();
    EXPECT_EQ(r.count(Z8kStatus::NmiAck), 1);   // Pegel hält, aber keine neue Flanke
    EXPECT_EQ(r.cpu.r(7), 1);
}

TEST(Z8000Irq, HaltWartetAufInterrupt) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  HALT\n  LD R3,#3\n  HALT\n  ORG <<0>>%2000\n  IRET\n", true);
    r.boot(0xC800, 0, 0x100);                    // NVIE
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.cpu.psapOff = 0x1000;
    r.psa(0, 0x1000, Z8000::PSA_NVI, SEGSYS, 0, 0x2000);
    EXPECT_EQ(r.cpu.step(), 8);
    EXPECT_TRUE(r.cpu.halted());
    EXPECT_EQ(r.cpu.step(), 3);
    r.cpu.setNVI(true);
    r.cpu.step();
    r.cpu.setNVI(false);
    EXPECT_FALSE(r.cpu.halted());
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0102);           // Befehl hinter HALT
    r.runToHalt();
    EXPECT_EQ(r.cpu.r(3), 3);
}

TEST(Z8000Irq, Z8002VektortabelleUndRahmen) {
    Rig r(M::Z8002);
    r.load("  NONSEG\n  ORG %0100\n  NOP\n  HALT\n  ORG %2000\n  INC R5\n  IRET\n", false);
    r.boot(0x5000, 0, 0x100);                    // SN | VIE
    r.cpu.R15[1] = 0xF000;
    r.cpu.psapOff = 0x0800;
    r.setW(0, 0x081C, SYS);                      // VI-FCW
    r.setW(0, uint16_t(0x081E + 2 * 5), 0x2000); // Vektor 5
    r.ackValue = 0x0005;
    r.cpu.setVI(true);
    r.cpu.step();
    r.cpu.setVI(false);
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.cpu.R15[1], 0xF000 - 6);         // PC, FCW, Kennung
    EXPECT_EQ(r.w(0, 0xEFFA), 0x0005);
    EXPECT_EQ(r.w(0, 0xEFFC), 0x5000);
    EXPECT_EQ(r.w(0, 0xEFFE), 0x0100);
    r.runToHalt();
    EXPECT_EQ(r.cpu.r(5), 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Bus
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Bus, StatusJeZugriffSegmentiert) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R1,<<5>>%1234\n  LD R2,@RR8\n  LDB RH3,@RR14\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setRR(8, 0x06000300);
    r.cpu.R14[1] = 0x0700; r.cpu.R15[1] = 0x0401;
    r.cpu.step();
    ASSERT_EQ(r.log.size(), 4u);
    EXPECT_EQ(r.log[0].first.st, Z8kStatus::MemInstrFirst);
    EXPECT_EQ(r.log[1].first.st, Z8kStatus::MemInstr);
    EXPECT_EQ(r.log[2].first.st, Z8kStatus::MemInstr);
    EXPECT_EQ(r.log[3].first.st, Z8kStatus::MemData);
    EXPECT_EQ(r.log[3].first.seg, 5);
    EXPECT_EQ(r.log[3].first.addr, 0x1234);
    r.log.clear(); r.cpu.step();
    EXPECT_EQ(r.log[1].first.st, Z8kStatus::MemData);
    EXPECT_EQ(r.log[1].first.seg, 6);
    r.log.clear(); r.cpu.step();
    EXPECT_EQ(r.log[1].first.st, Z8kStatus::MemStack);
    EXPECT_FALSE(r.log[1].first.word);
    EXPECT_EQ(r.log[1].first.seg, 7);
    EXPECT_EQ(r.log[1].first.addr, 0x0401);      // Byte: A0 bleibt
}

TEST(Z8000Bus, ByteHaelftenSpeicherUndEa) {
    Rig r;
    r.setW(0, 0x400, 0x1234);
    r.io[0x00A9] = 0x12AB;
    r.sio[0x00A8] = 0xCD34;
    runSeg(r, "  LDB RH1,<<0>>%0400\n  LDB RL1,<<0>>%0401\n  LD R2,#%00A9\n  INB RL3,@R2\n"
              "  SINB RH3,%00A8\n  LDB RL4,#%5A\n  OUTB @R2,RL4\n  LD R5,#%BEEF\n  OUT %00AF,R5\n"
              "  LDB <<0>>%0501,RL4");
    EXPECT_EQ(r.cpu.r(1), 0x1234);
    EXPECT_EQ(r.cpu.rb(11), 0xAB);               // Standard-E/A: AD0..7
    EXPECT_EQ(r.cpu.rb(3), 0xCD);                // Spezial-E/A: AD8..15
    ASSERT_EQ(r.ioWrites.size(), 2u);
    EXPECT_FALSE(r.ioWrites[0].first.word);
    EXPECT_EQ(r.ioWrites[0].second, 0x5A5A);     // Byte auf beiden Hälften (Annahme §8)
    EXPECT_EQ(r.ioWrites[1].second, 0xBEEF);
    EXPECT_EQ(r.ioWrites[1].first.addr, 0x00AF);
    bool seen = false;
    for (auto& e : r.log)
        if (!e.first.read && e.first.isMemory() && !e.first.word && e.first.addr == 0x0501) {
            EXPECT_EQ(e.second, 0x5A5A); seen = true;   // Speicher: beide Hälften (§9.4.2)
        }
    EXPECT_TRUE(seen);
}

/**
 * @test Z8000Bus/EaByteLageNachA0
 * @brief Ein E/A-Byte wird nach A0 gelesen wie ein Speicherbyte: gerade Portadresse
 *   AD8..15, ungerade AD0..7 — auch dort, wo das Handbuch die Adresse verbietet
 *   (Standard gerade, Spezial ungerade).  Am A5120.16 belegt: `INB RL0,%0080` liest
 *   Status-8 von AD8..15.
 */
TEST(Z8000Bus, EaByteLageNachA0) {
    Rig r;
    r.io[0x0080] = 0xC3FF;
    r.sio[0x00A9] = 0x1122;
    runSeg(r, "  INB RL3,%0080\n  SINB RH3,%00A9");
    EXPECT_EQ(r.cpu.rb(11), 0xC3);               // Standard, gerade: AD8..15
    EXPECT_EQ(r.cpu.rb(3), 0x22);                // Spezial, ungerade: AD0..7
}

TEST(Z8000Bus, IoByteNurEineHaelfteKonfigurierbar) {
    Z8000::Config cfg; cfg.ioByteOnBothHalves = false;
    Z8000 cpu(cfg);
    std::vector<uint16_t> out;
    uint16_t prog[] = {0x3A46, 0x00AD, 0x7A00};     // OUTB %00AD,RH4
    cpu.read = [&](const Z8kBusCycle& c) -> uint16_t {
        if (c.addr == 2) return 0xC000;
        if (c.addr == 4 || c.addr == 6) return c.addr == 6 ? 0x0100 : 0;
        if (c.addr >= 0x100 && c.addr < 0x106) return prog[(c.addr - 0x100) / 2];
        return 0;
    };
    cpu.write = [&](const Z8kBusCycle& c, uint16_t v) { if (c.st == Z8kStatus::Io) out.push_back(v); };
    cpu.reset(); cpu.step();
    cpu.setRB(4, 0x77);
    cpu.step();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 0x0077);
}

TEST(Z8000Bus, WortzugriffeSindGerade) {
    Rig r;
    r.setW(0, 0x600, 0x4455);
    runSeg(r, "  LD R1,<<0>>%0601");
    EXPECT_EQ(r.cpu.r(1), 0x4455);
}

TEST(Z8000Bus, RefreshZyklen) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R0,#%8200\n  LDCTL REFRESH,R0\n"
           "  NOP\n  NOP\n  NOP\n  NOP\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.runToHalt();
    int n = 0; uint16_t last = 0xFFFF;
    for (auto& e : r.log)
        if (e.first.st == Z8kStatus::Refresh) {
            if (n) EXPECT_EQ(e.first.addr, uint16_t((last + 2) & 0x1FE));
            last = e.first.addr; ++n;
        }
    EXPECT_GT(n, 5);                             // Periode 4 Takte (RATE = 1)
}

// ═════════════════════════════════════════════════════════════════════════════
// Pins: STOP, BUSREQ, Multi-Micro
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Pins, StopNachDemErstenWortUndRefresh) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R1,#%1234\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setStop(true);
    r.cpu.step();
    EXPECT_TRUE(r.cpu.stopped());
    EXPECT_EQ(r.count(Z8kStatus::MemInstrFirst), 1);
    for (int i = 0; i < 4; ++i) r.cpu.step();
    EXPECT_EQ(r.count(Z8kStatus::MemInstrFirst), 1);
    EXPECT_EQ(r.count(Z8kStatus::Refresh), 5);
    EXPECT_EQ(r.cpu.r(1), 0);
    r.cpu.setStop(false);
    r.cpu.step();
    EXPECT_FALSE(r.cpu.stopped());
    EXPECT_EQ(r.cpu.r(1), 0x1234);
    EXPECT_EQ(r.count(Z8kStatus::Refresh), 6);   // noch einer, dann weiter (§8.4)
    EXPECT_EQ(r.count(Z8kStatus::MemInstrFirst), 1);
}

TEST(Z8000Pins, BusRequestTrenntVomBus) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R1,#%1234\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    int acks = 0;
    r.cpu.onBusAck = [&](bool on) { acks += on ? 1 : 0; };
    r.cpu.setBusReq(true);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(r.cpu.step(), 1);
    EXPECT_TRUE(r.cpu.busAck());
    EXPECT_TRUE(r.log.empty());
    EXPECT_EQ(acks, 1);
    r.cpu.setBusReq(false);
    r.cpu.step();
    EXPECT_FALSE(r.cpu.busAck());
    EXPECT_EQ(r.cpu.r(1), 0x1234);
}

TEST(Z8000Pins, MultiMicroMsetMresMbit) {
    Rig r;
    std::vector<bool> mo;
    r.cpu.onMO = [&](bool a) { mo.push_back(a); };
    r.load("  SEG\n  ORG <<0>>%0100\n  MSET\n  MBIT\n  TCC MI,R1\n  MRES\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setMI(true);                           // µI aktiv (L)
    r.runToHalt();
    EXPECT_EQ(mo, (std::vector<bool>{true, false}));
    EXPECT_EQ(r.cpu.r(1) & 1, 0);                // S = 0: µI aktiv
    EXPECT_FALSE(r.cpu.moActive());
}

TEST(Z8000Pins, MreqGewaehrtUndAbgelehnt) {
    // Nicht gewährt: µI bleibt inaktiv → S=0, Z=1, MO zurück auf H
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R2,#5\n  MREQ R2\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.step();
    int c = r.cpu.step();
    EXPECT_EQ(c, 12 + 7 * 5);
    EXPECT_TRUE(F(r, Z8000::F_Z)); EXPECT_FALSE(F(r, Z8000::F_S));
    EXPECT_FALSE(r.cpu.moActive());
    EXPECT_EQ(r.cpu.r(2), 0);
    // Gewährt: die Gegenseite zieht µI, sobald MO aktiv wird
    Rig g;
    g.cpu.onMO = [&](bool a) { g.cpu.setMI(a); };
    g.load("  SEG\n  ORG <<0>>%0100\n  LD R2,#5\n  MREQ R2\n  HALT\n", true);
    g.boot(SEGSYS, 0, 0x100);
    g.cpu.setMI(false);
    g.runToHalt();
    EXPECT_TRUE(F(g, Z8000::F_Z)); EXPECT_TRUE(F(g, Z8000::F_S));
    EXPECT_TRUE(g.cpu.moActive());
    // Belegt: µI schon aktiv → S=0, Z=0
    Rig b;
    b.load("  SEG\n  ORG <<0>>%0100\n  LD R2,#5\n  MREQ R2\n  HALT\n", true);
    b.boot(SEGSYS, 0, 0x100);
    b.cpu.setMI(true);
    b.runToHalt();
    EXPECT_FALSE(F(b, Z8000::F_Z)); EXPECT_FALSE(F(b, Z8000::F_S));
    EXPECT_EQ(b.cpu.r(2), 5);
}

// ═════════════════════════════════════════════════════════════════════════════
// Takte (Stichproben gegen Anhang C / Befehlsseiten)
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Cycles, Stichproben) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  LD R1,R2\n  LD R1,|<<0>>%20|\n  LD R1,<<0>>%0020\n"
           "  LDL RR2,@RR4\n  PUSH @RR14,R1\n  DJNZ R1,$\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.setRR(4, 0x00000200); r.cpu.R15[1] = 0xF000;
    EXPECT_EQ(r.cpu.step(), 3);
    EXPECT_EQ(r.cpu.step(), 10);   // seg kurz
    EXPECT_EQ(r.cpu.step(), 12);   // seg lang
    EXPECT_EQ(r.cpu.step(), 11);
    EXPECT_EQ(r.cpu.step(), 9);
    r.cpu.setR(1, 1);
    EXPECT_EQ(r.cpu.step(), 11);   // DJNZ
}

// ═════════════════════════════════════════════════════════════════════════════
// Festlegungen, die das MAME-Orakel geschärft hat (README „Befunde")
// ═════════════════════════════════════════════════════════════════════════════

TEST(Z8000Befund, DabNeunundsechzigPlusVierundsechzig) {
    // 96 + 64 = 160: binär %FA, H = 0, C = 0 → Handbuch-Zeile „0, 9-F, 0, A-F → 66, C=1".
    // MAME (Stand 741d827a) liefert %00/C=0 — bekannte Abweichung im Orakel.
    Rig r(M::Z8002);
    runNonseg(r, "  LDB RL0,#%96\n  ADDB RL0,#%64\n  DAB RL0");
    EXPECT_EQ(r.cpu.rb(8), 0x60);
    EXPECT_TRUE(F(r, Z8000::F_C));
}

TEST(Z8000Befund, PcUeberlaeuftImSegment) {
    // Adressrechnung ohne Übertrag ins Segment (Handbuch §5.5); MAME trägt über.
    Rig r;
    r.setW(2, 0xFFFE, 0x8D07);                   // NOP an <<2>>%FFFE
    r.setW(2, 0x0000, 0x7A00);                   // HALT an <<2>>%0000
    r.boot(SEGSYS, 2, 0xFFFE);
    r.runToHalt();
    EXPECT_EQ(r.cpu.pcSeg, 2);
    EXPECT_EQ(r.cpu.pc, 0x0002);
}

TEST(Z8000Befund, UngeraderStapelzeigerWirdBeimPushGerade) {
    Rig r;
    r.load("  SEG\n  ORG <<0>>%0100\n  PUSH @RR14,#%1234\n  POP R1,@RR14\n  HALT\n", true);
    r.boot(SEGSYS, 0, 0x100);
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0x2001;
    r.cpu.step();
    EXPECT_EQ(r.cpu.R15[1], 0x1FFE);             // 0x2001 − 2 − Bit 0
    EXPECT_EQ(r.w(0, 0x1FFE), 0x1234);
    r.cpu.step();
    EXPECT_EQ(r.cpu.r(1), 0x1234);
    EXPECT_EQ(r.cpu.R15[1], 0x2000);
}

TEST(Z8000Befund, Z8002KenntKeinNspseg) {
    Rig r(M::Z8002);
    runNonseg(r, "  LD R14,#%1111\n  LD R0,#%2222\n  LDCTL NSPSEG,R0\n  LDCTL R1,NSPSEG");
    EXPECT_EQ(r.cpu.r(14), 0x1111);
    EXPECT_EQ(r.cpu.r(1), 0);
}

TEST(Z8000Befund, GesichertesPcSegmentwortOhneBit15) {
    Rig r;
    r.load("  SEG\n  ORG <<5>>%0100\n  CALL <<5>>%0200\n  ORG <<5>>%0200\n  HALT\n", true);
    r.boot(SEGSYS, 5, 0x100);
    r.cpu.R14[1] = 0; r.cpu.R15[1] = 0xF000;
    r.runToHalt();
    EXPECT_EQ(r.w(0, 0xEFFC), 0x0500);           // 0sss ssss 0000 0000 (MAME: %8500)
}

TEST(Z8000Zustand, AblaufzustandMittenImLdirUebertragbar) {
    Rig a;
    for (int i = 0; i < 4; ++i) a.setW(1, uint16_t(0x100 + 2 * i), uint16_t(0xA0 + i));
    a.load("  SEG\n  ORG <<0>>%0100\n  LDIR @RR2,@RR4,R6\n  HALT\n", true);
    a.boot(SEGSYS, 0, 0x100);
    a.cpu.setRR(2, 0x02000200); a.cpu.setRR(4, 0x01000100); a.cpu.setR(6, 4);
    a.cpu.step(); a.cpu.step();
    ASSERT_TRUE(a.cpu.inRepeat());
    // Zweite CPU mit Registern + Ablaufzustand der ersten, gleicher Speicher
    Rig b;
    b.mem = a.mem;
    for (int i = 0; i < 14; ++i) b.cpu.Rg[i] = a.cpu.Rg[i];
    b.cpu.R14[1] = a.cpu.R14[1]; b.cpu.R15[1] = a.cpu.R15[1];
    b.cpu.fcw = a.cpu.fcw; b.cpu.pc = a.cpu.pc; b.cpu.pcSeg = a.cpu.pcSeg;
    b.cpu.setRunState(a.cpu.runState());
    EXPECT_TRUE(b.cpu.inRepeat());
    b.runToHalt();
    for (int i = 0; i < 4; ++i) EXPECT_EQ(b.w(2, uint16_t(0x200 + 2 * i)), 0xA0 + i);
    EXPECT_EQ(b.count(Z8kStatus::MemInstrFirst), 1);   // nur HALT neu geholt
}
