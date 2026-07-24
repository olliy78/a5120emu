// Unit-Tests für die boot_trace --until-Bedingung (tools/until_cond.h).
#include "tools/until_cond.h"
#include <gtest/gtest.h>

using untilcond::UntilCond;
using untilcond::parse;

namespace {
UntilCond P(const std::string& s){ UntilCond u; parse(s, u); return u; }
}

// ── Parsen: Formen PC / [A] / [A]w ───────────────────────────────────────────

TEST(UntilCond, ParsesPcForm){
    UntilCond u; ASSERT_TRUE(parse("PC==0x0437", u));
    EXPECT_EQ(u.kind, UntilCond::PC);
    EXPECT_EQ(u.op, UntilCond::EQ);
    EXPECT_EQ(u.addr, 0x0437);
    EXPECT_EQ(u.text, "PC==0x0437");
    EXPECT_TRUE(parse("pc==0x100", u));   // klein geschrieben
    EXPECT_EQ(u.kind, UntilCond::PC);
}

TEST(UntilCond, ParsesMemByteAndWord){
    UntilCond u; ASSERT_TRUE(parse("[0x03F8]==3", u));
    EXPECT_EQ(u.kind, UntilCond::MEM);
    EXPECT_FALSE(u.word);
    EXPECT_EQ(u.addr, 0x03F8);
    EXPECT_EQ(u.val, 3);

    ASSERT_TRUE(parse("[0xD1BE]w!=0", u));
    EXPECT_EQ(u.kind, UntilCond::MEM);
    EXPECT_TRUE(u.word);
    EXPECT_EQ(u.addr, 0xD1BE);
    EXPECT_EQ(u.op, UntilCond::NE);
}

TEST(UntilCond, ValuesParseBase0){
    EXPECT_EQ(P("[0x10]==255").val, 255);
    EXPECT_EQ(P("[0x10]==0xFF").val, 0xFF);
    EXPECT_EQ(P("[16]==10").addr, 16);     // dezimale Adresse
}

// ── Operator-Erkennung: <= vs < , >= vs > ────────────────────────────────────

TEST(UntilCond, OperatorDisambiguation){
    EXPECT_EQ(P("[0x10]<5").op,  UntilCond::LT);
    EXPECT_EQ(P("[0x10]<=5").op, UntilCond::LE);
    EXPECT_EQ(P("[0x10]>5").op,  UntilCond::GT);
    EXPECT_EQ(P("[0x10]>=5").op, UntilCond::GE);
    EXPECT_EQ(P("[0x10]==5").op, UntilCond::EQ);
    EXPECT_EQ(P("[0x10]!=5").op, UntilCond::NE);
}

// ── Ungültige Eingaben ───────────────────────────────────────────────────────

TEST(UntilCond, RejectsBadInput){
    UntilCond u;
    EXPECT_FALSE(parse("[0x10]", u));      // kein Operator
    EXPECT_FALSE(parse("FOO==1", u));      // weder PC noch [A]
    EXPECT_FALSE(parse("[0x10==1", u));    // fehlende ]
    EXPECT_FALSE(parse("", u));
    EXPECT_EQ(u.kind, UntilCond::NONE);
}

// ── screen ~ "text" / /regex/  (feature_request_interactive_debug #1) ────────

TEST(UntilCond, ParsesScreenSubstring){
    UntilCond u;
    ASSERT_TRUE(parse("screen ~ \"A>\"", u));
    EXPECT_EQ(u.kind, UntilCond::SCREEN);
    EXPECT_EQ(u.pattern, "A>");
    // Ohne Tilde und ohne Anführungszeichen ebenso zulässig.
    UntilCond v; ASSERT_TRUE(parse("screen READY", v));
    EXPECT_EQ(v.pattern, "READY");
}

TEST(UntilCond, ScreenMatchSubstringAndRegex){
    std::string scr = "ROBOTRON LOADER    SCPX 1526 - V 1.7    A>";
    EXPECT_TRUE (P("screen ~ \"SCPX\"").screenMatch(scr));
    EXPECT_FALSE(P("screen ~ \"CP/A\"").screenMatch(scr));
    EXPECT_TRUE (P("screen ~ /V 1\\.[0-9]/").screenMatch(scr));
    EXPECT_FALSE(P("screen ~ /V 2\\.[0-9]/").screenMatch(scr));
}

TEST(UntilCond, RejectsEmptyScreenPattern){
    UntilCond u;
    EXPECT_FALSE(parse("screen ~ \"\"", u));
    EXPECT_FALSE(parse("screen", u));
    EXPECT_EQ(u.kind, UntilCond::NONE);
}

// ── Auswertung (compare) — alle Operatoren ───────────────────────────────────

TEST(UntilCond, CompareEvaluatesAllOperators){
    EXPECT_TRUE (P("[0]==5").compare(5));   EXPECT_FALSE(P("[0]==5").compare(4));
    EXPECT_TRUE (P("[0]!=5").compare(4));   EXPECT_FALSE(P("[0]!=5").compare(5));
    EXPECT_TRUE (P("[0]<5").compare(4));    EXPECT_FALSE(P("[0]<5").compare(5));
    EXPECT_TRUE (P("[0]>5").compare(6));    EXPECT_FALSE(P("[0]>5").compare(5));
    EXPECT_TRUE (P("[0]<=5").compare(5));   EXPECT_FALSE(P("[0]<=5").compare(6));
    EXPECT_TRUE (P("[0]>=5").compare(5));   EXPECT_FALSE(P("[0]>=5").compare(4));
}
