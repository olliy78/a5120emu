// Unit-Tests für den k1520dbg-Ausdrucks-Evaluator (tools/expr_eval.h).
#include "tools/expr_eval.h"
#include <gtest/gtest.h>
#include <map>

using expreval::RegView;

namespace {
// Test-Harness: Register + Speicher + Symbole, plus bequeme ev()/evOk().
struct Env {
    RegView r;
    std::map<uint16_t,uint8_t>  mem;
    std::map<std::string,uint16_t> syms;
    expreval::ReadByte rb = [this](uint16_t a){ auto it=mem.find(a); return it==mem.end()?(uint8_t)0:it->second; };
    expreval::FindSym  fs = [this](const std::string& n, uint16_t& v){ auto it=syms.find(n);
        if(it==syms.end()) return false; v=it->second; return true; };
    long ev(const std::string& e){ bool ok; return expreval::eval(e, r, rb, fs, ok); }
    long evOk(const std::string& e, bool& ok){ return expreval::eval(e, r, rb, fs, ok); }
};
} // namespace

// ── Arithmetik + Präzedenz ───────────────────────────────────────────────────

TEST(ExprEval, ArithmeticPrecedence){
    Env e;
    EXPECT_EQ(e.ev("2+3*4"), 14);          // * vor +
    EXPECT_EQ(e.ev("2*3+4"), 10);
    EXPECT_EQ(e.ev("10-2-3"), 5);          // links-assoziativ
    EXPECT_EQ(e.ev("100/3/2"), 16);        // (100/3)/2 = 33/2
    EXPECT_EQ(e.ev("17%5"), 2);
    EXPECT_EQ(e.ev("(2+3)*4"), 20);        // Klammern
    EXPECT_EQ(e.ev("((1+2))"), 3);
}

TEST(ExprEval, UnaryMinusAndNot){
    Env e;
    EXPECT_EQ(e.ev("-5"), -5);
    EXPECT_EQ(e.ev("~0"), -1);
    EXPECT_EQ(e.ev("-2*3"), -6);           // (-2)*3
    EXPECT_EQ(e.ev("2*-3"), -6);
    EXPECT_EQ(e.ev("-(2+3)"), -5);
}

// ── Bit-Operationen + Shifts ─────────────────────────────────────────────────

TEST(ExprEval, BitwiseAndShift){
    Env e;
    EXPECT_EQ(e.ev("0xFF & 0x0F"), 0x0F);
    EXPECT_EQ(e.ev("0xF0 | 0x0F"), 0xFF);
    EXPECT_EQ(e.ev("0xFF ^ 0x0F"), 0xF0);
    EXPECT_EQ(e.ev("1 << 8"), 256);
    EXPECT_EQ(e.ev("0x100 >> 4"), 0x10);
    // Präzedenz: shift > | ; & > | ; + > <<
    EXPECT_EQ(e.ev("1 | 2 & 3"), 3);       // & vor | : 1 | (2&3)=1|2=3
    EXPECT_EQ(e.ev("1 + 1 << 4"), 32);     // (1+1)<<4
}

// ── Vergleiche (inkl. < vs << und <= vs <) ───────────────────────────────────

TEST(ExprEval, Comparisons){
    Env e;
    EXPECT_EQ(e.ev("5==5"), 1);  EXPECT_EQ(e.ev("5==4"), 0);
    EXPECT_EQ(e.ev("5!=4"), 1);  EXPECT_EQ(e.ev("5!=5"), 0);
    EXPECT_EQ(e.ev("3<5"), 1);   EXPECT_EQ(e.ev("5<3"), 0);
    EXPECT_EQ(e.ev("5<=5"), 1);  EXPECT_EQ(e.ev("6<=5"), 0);
    EXPECT_EQ(e.ev("6>5"), 1);   EXPECT_EQ(e.ev("5>=5"), 1);
    // shift binds tighter than comparison: 2<<3 == 16
    EXPECT_EQ(e.ev("2<<3 == 16"), 1);
    EXPECT_EQ(e.ev("8>>1 == 4"), 1);
    // a single '<' must not be eaten as part of '<<'
    EXPECT_EQ(e.ev("3 < 5"), 1);
    EXPECT_EQ(e.ev("5 > 3"), 1);
}

// ── Zahlenformate + Symbole ──────────────────────────────────────────────────

TEST(ExprEval, NumberFormatsAndSymbols){
    Env e;
    EXPECT_EQ(e.ev("0x10"), 16);
    EXPECT_EQ(e.ev("16"), 16);
    EXPECT_EQ(e.ev("10H"), 16);
    EXPECT_EQ(e.ev("0FFH"), 255);
    e.syms["FOO"] = 0x1234;
    EXPECT_EQ(e.ev("FOO"), 0x1234);
    EXPECT_EQ(e.ev("FOO+1"), 0x1235);      // Symbol + Offset via Operator
    EXPECT_EQ(e.ev("FOO & 0xFF"), 0x34);
}

// ── Register ─────────────────────────────────────────────────────────────────

TEST(ExprEval, Registers){
    Env e;
    e.r.AF = 0x2520; e.r.HL = 0x2402; e.r.SP = 0x7F00; e.r.BC = 0xDEAD;
    EXPECT_EQ(e.ev("A"), 0x25);  EXPECT_EQ(e.ev("F"), 0x20);
    EXPECT_EQ(e.ev("H"), 0x24);  EXPECT_EQ(e.ev("L"), 0x02);
    EXPECT_EQ(e.ev("B"), 0xDE);  EXPECT_EQ(e.ev("C"), 0xAD);
    EXPECT_EQ(e.ev("HL"), 0x2402); EXPECT_EQ(e.ev("SP"), 0x7F00);
    EXPECT_EQ(e.ev("HL+1"), 0x2403);
    EXPECT_EQ(e.ev("(HL & 0xFF) == 0x02"), 1);   // der Conditional-Smoke
}

// ── Speicher: (rr), [expr], [expr]w ──────────────────────────────────────────

TEST(ExprEval, MemoryAccess){
    Env e;
    e.r.HL = 0x6000;
    e.mem[0x6000] = 0x34; e.mem[0x6001] = 0x12;   // LE-Wort 0x1234
    e.mem[0x6005] = 0xAB;
    EXPECT_EQ(e.ev("[0x6000]"), 0x34);
    EXPECT_EQ(e.ev("[0x6000]w"), 0x1234);          // little-endian
    EXPECT_EQ(e.ev("(HL)"), 0x34);                 // Register-indirekt
    EXPECT_EQ(e.ev("[HL]"), 0x34);
    EXPECT_EQ(e.ev("[HL+5]"), 0xAB);               // Ausdrucks-Index
    EXPECT_EQ(e.ev("[0x6000]w + 1"), 0x1235);
}

// ── Fehlerfälle: ok=false ────────────────────────────────────────────────────

TEST(ExprEval, ErrorsSetOkFalse){
    Env e; bool ok;
    e.evOk("1+", ok);        EXPECT_FALSE(ok);     // fehlender Operand
    e.evOk("(1", ok);        EXPECT_FALSE(ok);     // unbalancierte Klammer
    e.evOk("1 2", ok);       EXPECT_FALSE(ok);     // trailing junk
    e.evOk("5/0", ok);       EXPECT_FALSE(ok);     // Division durch 0
    e.evOk("@@@", ok);       EXPECT_FALSE(ok);     // weder Zahl noch Symbol
    e.evOk("", ok);          EXPECT_FALSE(ok);     // leer
    // Gegenprobe: gültiger Ausdruck → ok
    e.evOk("1+2*3", ok);     EXPECT_TRUE(ok);
}
