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
