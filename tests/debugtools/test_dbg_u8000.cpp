/**
 * @file test_dbg_u8000.cpp
 * @brief tools/dbg_u8000.h — Adressen, FCW-Text und Aufrufstapel des U8001-Kontexts.
 */
#include <gtest/gtest.h>
#include "tools/dbg_u8000.h"

using dbg16::Addr16;

TEST(DbgU8000Adresse, SegmentSchreibweiseDesDisassemblers) {
    Addr16 a;
    ASSERT_TRUE(dbg16::parseAddr("<<3>>%1234", 0, a));
    EXPECT_EQ(a.kind, Addr16::Seg);
    EXPECT_EQ(a.seg, 3);
    EXPECT_EQ(a.off, 0x1234);
    EXPECT_TRUE(a.explicitSeg);
    ASSERT_TRUE(dbg16::parseAddr("<<127>>0x10", 0, a));
    EXPECT_EQ(a.seg, 127);
    EXPECT_EQ(a.off, 0x10);
    EXPECT_FALSE(dbg16::parseAddr("<<128>>%0", 0, a));
    EXPECT_FALSE(dbg16::parseAddr("<<3>%0", 0, a));
}

TEST(DbgU8000Adresse, ZahlOhneSegmentNimmtDieVorgabe) {
    Addr16 a;
    ASSERT_TRUE(dbg16::parseAddr("%0208", 5, a));
    EXPECT_EQ(a.seg, 5);
    EXPECT_EQ(a.off, 0x0208);
    EXPECT_FALSE(a.explicitSeg);
    ASSERT_TRUE(dbg16::parseAddr("208h", 0, a));
    EXPECT_EQ(a.off, 0x0208);
    ASSERT_TRUE(dbg16::parseAddr("16", 0, a));
    EXPECT_EQ(a.off, 16);
}

TEST(DbgU8000Adresse, RohzelleImDram) {
    Addr16 a;
    ASSERT_TRUE(dbg16::parseAddr("em:1F000", 0, a));
    EXPECT_EQ(a.kind, Addr16::Raw);
    EXPECT_EQ(a.cell, 0x1F000u);
    EXPECT_FALSE(dbg16::parseAddr("em:xyz", 0, a));
}

TEST(DbgU8000Adresse, SymbolUeberDenRufer) {
    Addr16 a;
    auto sym = [](const std::string& n, long& v) {
        if (n == "START") { v = 0x30100; return true; }
        return false;
    };
    ASSERT_TRUE(dbg16::parseAddr("START", 0, a, sym));
    EXPECT_EQ(a.seg, 3);
    EXPECT_EQ(a.off, 0x0100);
    EXPECT_FALSE(dbg16::parseAddr("NIX", 0, a, sym));
}

TEST(DbgU8000, FcwText) {
    EXPECT_EQ(dbg16::fcwText(0xD8C0), "SEG SYS VIE NVIE [CZ----]");
    EXPECT_EQ(dbg16::fcwText(0x0000), "NONSEG NORM vi- nvi- [------]");
    EXPECT_EQ(dbg16::addrText(3, 0x12, true), "<<3>>%0012");
    EXPECT_EQ(dbg16::addrText(0, 0xABCD, false), "%ABCD");
}

TEST(DbgU8000, AufrufstapelFolgtCallUndRet) {
    dbg16::CallStack16 cs;
    cs.onInstruction(0x100, 0x2000);
    cs.noteCall(0x100, 0x104, 0x2000);           // CALL an 0100, SP 2000
    cs.onInstruction(0x400, 0x1FFC);             // im Unterprogramm, SP -4
    ASSERT_EQ(cs.frames().size(), 1u);
    EXPECT_EQ(cs.frames()[0].target, 0x400u);
    EXPECT_EQ(cs.frames()[0].ret, 0x104u);
    cs.noteCall(0x404, 0x408, 0x1FFC);
    cs.onInstruction(0x500, 0x1FF8);
    EXPECT_EQ(cs.frames().size(), 2u);
    cs.onInstruction(0x408, 0x1FFC);             // RET
    EXPECT_EQ(cs.frames().size(), 1u);
    cs.onInstruction(0x104, 0x2000);             // RET
    EXPECT_TRUE(cs.frames().empty());
}

TEST(DbgU8000, BedingterCallOhneSprungErzeugtKeinenRahmen) {
    dbg16::CallStack16 cs;
    cs.noteCall(0x100, 0x104, 0x2000);
    cs.onInstruction(0x104, 0x2000);             // Bedingung falsch: SP unverändert
    EXPECT_TRUE(cs.frames().empty());
}

// ── S5b ──────────────────────────────────────────────────────────────────────

TEST(DbgU8000Ausdruck, RegisterPaareFlagsUndKarte) {
    dbg16::RegView16 v;
    for (int i = 0; i < 16; ++i) v.r[i] = uint16_t(0x1100 * i + i);
    v.fcw = 0xC000 | 0x0080 | 0x0040;              // SEG SYS C Z
    v.pc = 0x0132; v.pcSeg = 3;
    long long x;
    ASSERT_TRUE(dbg16::reg16(v, "R3", x));    EXPECT_EQ(x, 0x3303);
    ASSERT_TRUE(dbg16::reg16(v, "RH1", x));   EXPECT_EQ(x, 0x11);
    ASSERT_TRUE(dbg16::reg16(v, "RL1", x));   EXPECT_EQ(x, 0x01);
    ASSERT_TRUE(dbg16::reg16(v, "RR2", x));   EXPECT_EQ(x, 0x22023303LL);     // R2 = oberes Wort
    ASSERT_TRUE(dbg16::reg16(v, "RQ4", x));   EXPECT_EQ(uint64_t(x), 0x4404550566067707ULL);
    EXPECT_FALSE(dbg16::reg16(v, "RR3", x));  // ungerade Paare gibt es nicht
    ASSERT_TRUE(dbg16::reg16(v, "C", x));     EXPECT_EQ(x, 1);
    ASSERT_TRUE(dbg16::reg16(v, "S", x));     EXPECT_EQ(x, 0);
    ASSERT_TRUE(dbg16::reg16(v, "SEG", x));   EXPECT_EQ(x, 1);
    ASSERT_TRUE(dbg16::reg16(v, "PCSEG", x)); EXPECT_EQ(x, 3);
    EXPECT_FALSE(dbg16::reg16(v, "A33", x));  // ohne Karte
    v.haveEm = true; v.a33 = 0x18; v.mode8 = false;
    ASSERT_TRUE(dbg16::reg16(v, "A33", x));   EXPECT_EQ(x, 0x18);
    ASSERT_TRUE(dbg16::reg16(v, "MODE", x));  EXPECT_EQ(x, 16);
}

TEST(DbgU8000Ausdruck, AdresswertInDreiFormen) {
    uint8_t s; uint16_t o;
    dbg16::decodeAddr(0x03001234LL, 9, s, o);  EXPECT_EQ(s, 3); EXPECT_EQ(o, 0x1234);  // wie in RRn
    dbg16::decodeAddr(0x31234LL, 9, s, o);     EXPECT_EQ(s, 3); EXPECT_EQ(o, 0x1234);  // <<3>>%1234
    dbg16::decodeAddr(0x1234LL, 9, s, o);      EXPECT_EQ(s, 9); EXPECT_EQ(o, 0x1234);  // Vorgabe
}

TEST(DbgU8000Symbole, ZeilenformatUndNaechstesSymbol) {
    dbg16::SymTab16 t;
    EXPECT_EQ(t.parseLine("# Kommentar"), 0);
    EXPECT_EQ(t.parseLine("C7A3 SELDSK"), 0);                   // Z80-Zeile
    EXPECT_EQ(t.parseLine("<<3>>%0100 START"), 1);
    EXPECT_EQ(t.parseLine("LOOP <<3>>%0104"), 1);
    EXPECT_EQ(t.parseLine("ENDE = <<0>>%0200"), 1);
    EXPECT_EQ(t.parseLine("<<300>>%0 X"), -1);
    uint32_t k;
    ASSERT_TRUE(t.find("LOOP", k));     EXPECT_EQ(k, 0x30104u);
    ASSERT_TRUE(t.find("START+%10", k)); EXPECT_EQ(k, 0x30110u);
    ASSERT_TRUE(t.find("START-2", k));  EXPECT_EQ(k, 0x300FEu);
    EXPECT_EQ(t.at(0x30100), "START");
    EXPECT_EQ(t.nearest(0x30106), "LOOP+%2");
    EXPECT_EQ(t.nearest(0x40106), "");                             // anderes Segment
    EXPECT_EQ(t.nearest(0x300FF), "");                             // davor gibt es nichts
    dbg16::Addr16 a;                                            // parseAddr mit Symbol
    ASSERT_TRUE(dbg16::parseAddr("LOOP", 0, a, [&](const std::string& n, long& v){
        uint32_t kk; if(!t.find(n,kk)) return false; v=long(kk); return true; }));
    EXPECT_EQ(a.seg, 3); EXPECT_EQ(a.off, 0x0104);
}

TEST(DbgU8000Watch, BereicheLogischUndRoh) {
    dbg16::Addr16 lo, hi;
    ASSERT_TRUE(dbg16::parseRange16("<<1>>%1000..%10FF", 0, lo, hi));
    EXPECT_EQ(lo.key(), 0x11000u); EXPECT_EQ(hi.key(), 0x110FFu);
    ASSERT_TRUE(dbg16::parseRange16("%2000", 4, lo, hi));
    EXPECT_EQ(lo.key(), 0x42000u); EXPECT_EQ(hi.key(), 0x42000u);
    EXPECT_FALSE(dbg16::parseRange16("<<1>>%1000..<<2>>%10FF", 0, lo, hi));   // zwei Segmente
    ASSERT_TRUE(dbg16::parseRange16("em:1F000..1F0FF", 0, lo, hi));
    EXPECT_EQ(lo.kind, dbg16::Addr16::Raw); EXPECT_EQ(lo.cell, 0x1F000u); EXPECT_EQ(hi.cell, 0x1F0FFu);
    ASSERT_TRUE(dbg16::parseRange16("em:10..em:0", 0, lo, hi));               // vertauscht
    EXPECT_EQ(lo.cell, 0u); EXPECT_EQ(hi.cell, 0x10u);
}

TEST(DbgU8000Snap, DiffZeigtRegisterKarteUndDramBereiche) {
    dbg16::EmSnap a, b;
    a.valid = b.valid = true;
    a.dram.assign(0x100, 0); b.dram = a.dram;
    a.state = b.state = "run";
    EXPECT_TRUE(dbg16::diffEm(a, b).empty());
    b.r[3] = 0xBEEF; b.pc = 0x0104; b.a33 = 0x08; b.a54 = true; b.pe = true; b.attr[2] = 5;
    b.dram[0x10] = 1; b.dram[0x11] = 2; b.dram[0x80] = 3;
    auto d = dbg16::diffEm(a, b);
    std::string all; for (auto& l : d) all += l + "\n";
    EXPECT_NE(all.find("R3 0000→BEEF"), std::string::npos) << all;
    EXPECT_NE(all.find("PC 0000→0104"), std::string::npos) << all;
    EXPECT_NE(all.find("A33 0→8"), std::string::npos) << all;
    EXPECT_NE(all.find("A54 0→1"), std::string::npos) << all;
    EXPECT_NE(all.find("A46/PE 0→1"), std::string::npos) << all;
    EXPECT_NE(all.find("A22: 2:0→5"), std::string::npos) << all;
    EXPECT_NE(all.find("em:00010..em:00011 (2 B)"), std::string::npos) << all;
    EXPECT_NE(all.find("3 EM-DRAM-Byte(s) in 2 Bereich(en)"), std::string::npos) << all;
}
