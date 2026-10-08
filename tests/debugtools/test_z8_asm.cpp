/**
 * @file test_z8_asm.cpp
 * @brief Z8-Disassembler/-Assembler (tools/z8/z8_disasm.h, z8_asm.h): Rundlauf über ALLE
 *        Opcodes und Operandenbytes, Handbuchkodierungen (Zilog UM0016), Direktiven, Fehler.
 */
#include "tools/z8/z8_asm.h"
#include "tools/z8/z8_disasm.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

std::vector<uint8_t> as(const std::string& t, uint16_t pc = 0x100) {
    std::vector<uint8_t> b; std::string err;
    EXPECT_TRUE(z8asm::befehl(t, pc, b, err)) << t << ": " << err;
    return b;
}
std::string dis(std::vector<uint8_t> b, uint16_t pc = 0x100) {
    b.resize(3, 0);
    return z8dis::disasm([&](uint16_t a) { return b[uint16_t(a - pc)]; }, pc).text;
}

}  // namespace

TEST(Z8Asm, HandbuchKodierungen) {
    const struct { const char* t; std::vector<uint8_t> b; } f[] = {
        {"ADD 43H,08H", {0x04, 0x08, 0x43}},
        {"LD r15,#34H", {0xFC, 0x34}},
        {"LD r14,34H", {0xE8, 0x34}},
        {"LD 34H,r14", {0xE9, 0x34}},
        {"LD r13,@r12", {0xE3, 0xDC}},
        {"LD @r13,r12", {0xF3, 0xDC}},
        {"LD 34H,45H", {0xE4, 0x45, 0x34}},
        {"LD 34H,@45H", {0xE5, 0x45, 0x34}},
        {"LD 34H,#0A4H", {0xE6, 0x34, 0xA4}},
        {"LD @r14,#0FCH", {0xE7, 0xEE, 0xFC}},
        {"LD @34H,45H", {0xF5, 0x45, 0x34}},
        {"LD r10,24H(r0)", {0xC7, 0xA0, 0x24}},
        {"LD 0F0H(r0),r10", {0xD7, 0xA0, 0xF0}},
        {"LDC r2,@rr6", {0xC2, 0x26}},
        {"LDC @rr6,r2", {0xD2, 0x26}},
        {"LDCI @r2,@rr6", {0xC3, 0x26}},
        {"LDCI @rr6,@r2", {0xD3, 0x26}},
        {"LDE r2,@rr6", {0x82, 0x26}},
        {"LDE @rr6,r2", {0x92, 0x26}},
        {"LDEI @r2,@rr6", {0x83, 0x26}},
        {"LDEI @rr6,@r2", {0x93, 0x26}},
        {"CALL 3521H", {0xD6, 0x35, 0x21}},
        {"CALL @0A4H", {0xD4, 0xA4}},
        {"DEC r10", {0x00, 0xEA}},
        {"DEC @0B3H", {0x01, 0xB3}},
        {"DA 5FH", {0x40, 0x5F}},
        {"SRP #0F0H", {0x31, 0xF0}},
        {"SRP #70H", {0x31, 0x70}},
        {"DECW rr6", {0x80, 0xE6}},
        {"INCW @34H", {0xA1, 0x34}},
        {"JP @rr4", {0x30, 0xE4}},
        {"JP NZ,1234H", {0xED, 0x12, 0x34}},
        {"JP 1234H", {0x8D, 0x12, 0x34}},
        {"JR C,0100H", {0x7B, 0xFE}},
        {"DJNZ r6,0100H", {0x6A, 0xFE}},
        {"INC r3", {0x3E}},
        {"INC 45H", {0x20, 0x45}},
        {"ADD r1,r2", {0x02, 0x12}},
        {"ADC r1,@r2", {0x13, 0x12}},
        {"SUB 45H,#12H", {0x26, 0x45, 0x12}},
        {"SBC @45H,#12H", {0x37, 0x45, 0x12}},
        {"OR IMR,#80H", {0x46, 0xFB, 0x80}},
        {"TM IRQ,#10H", {0x76, 0xFA, 0x10}},
        {"PUSH RP", {0x70, 0xFD}},
        {"POP @r2", {0x51, 0xE2}},
        {"EI", {0x9F}}, {"DI", {0x8F}}, {"RET", {0xAF}}, {"IRET", {0xBF}},
        {"RCF", {0xCF}}, {"SCF", {0xDF}}, {"CCF", {0xEF}}, {"NOP", {0xFF}},
    };
    for (auto& e : f) EXPECT_EQ(as(e.t), e.b) << e.t;
}

TEST(Z8Asm, RundlaufAlleOpcodesAlleOperandenbytes) {
    // disasm → Text → asm → disasm: gleicher Text, nicht länger (kürzere Form zulässig,
    // z. B. ADD 40H,41H mit Ex-Feldern → ADD r0,r1).
    int faelle = 0, kuerzer = 0;
    for (int op = 0; op < 256; ++op) {
        const z8::Insn& in = z8::tabelle()[uint8_t(op)];
        if (in.mn == z8::Mn::Ungueltig) continue;
        const int n1 = in.len >= 2 ? 256 : 1;
        for (int b1 = 0; b1 < n1; ++b1) {
            for (int b2 : {0x00, 0x12, 0x7F, 0x80, 0xE5, 0xFC}) {
                if (in.len < 3 && b2) continue;
                std::vector<uint8_t> b = {uint8_t(op), uint8_t(b1), uint8_t(b2)};
                const std::string t = dis(b, 0x0100);
                std::vector<uint8_t> c; std::string err;
                ASSERT_TRUE(z8asm::befehl(t, 0x0100, c, err)) << std::hex << op << " " << b1 << " '" << t << "': " << err;
                ASSERT_LE(int(c.size()), int(in.len)) << t;
                ASSERT_EQ(dis(c, 0x0100), t) << std::hex << op << " " << b1;
                if (int(c.size()) == in.len) {
                    std::vector<uint8_t> orig(b.begin(), b.begin() + in.len);
                    // gleiche Länge, aber andere Bytes nur bei gleichwertigen Formen (LD R,r = r9 vs E4)
                    if (c != orig) ++kuerzer;
                } else ++kuerzer;
                ++faelle;
            }
        }
    }
    EXPECT_GT(faelle, 40000);
    EXPECT_LT(kuerzer, faelle / 4);
}

TEST(Z8Asm, DirektivenMarkenAusdruecke) {
    const auto r = z8asm::assemble(
        "BWS   EQU 1000H\n"
        "      ORG 0CH\n"
        "START: LD r2,#HI\n"
        "      LD r3,#BWS+5-1\n"
        "LOOP: DJNZ r2,LOOP\n"
        "      JP START\n"
        "      JR VOR\n"
        "VOR:  NOP\n"
        "TXT:  DB 'AB',0DH,'x'\n"
        "      DW TXT, 0BEEFH\n"
        "HI    EQU 12H\n"
        "      LD r4,#'A'   ; Kommentar ; mit Semikolon\n");
    ASSERT_TRUE(r.ok()) << r.fehler[0].zeile << ": " << r.fehler[0].text;
    const auto v = r.flach(0x0C);
    const std::vector<uint8_t> soll = {
        0x2C, 0x12,             // LD r2,#12H (vorwärts aufgelöst)
        0x3C, 0x04,             // LD r3,#04H (BWS+4 gekappt auf 8 Bit)
        0x2A, 0xFE,             // DJNZ r2,LOOP
        0x8D, 0x00, 0x0C,       // JP START
        0x8B, 0x00,             // JR VOR
        0xFF,                   // NOP
        'A', 'B', 0x0D, 'x',
        0x00, 0x18, 0xBE, 0xEF,
        0x4C, 'A'};
    EXPECT_EQ(v, soll);
    EXPECT_EQ(r.marken.at("TXT"), 0x18);
}

TEST(Z8Asm, FehlerMitZeile) {
    const auto r = z8asm::assemble(" ORG 0\n LD r1,r2,r3\n FOO r1\n JR WEIT\n ORG 200H\nWEIT: NOP\n ADD 300H,#1\n");
    ASSERT_EQ(r.fehler.size(), 4u);
    EXPECT_EQ(r.fehler[0].zeile, 2);
    EXPECT_EQ(r.fehler[1].zeile, 3);
    EXPECT_NE(r.fehler[1].text.find("unbekannter Befehl"), std::string::npos);
    EXPECT_EQ(r.fehler[2].zeile, 4);
    EXPECT_NE(r.fehler[2].text.find("Reichweite"), std::string::npos);
    EXPECT_EQ(r.fehler[3].zeile, 7);
}

TEST(Z8Disasm, SchreibweiseUndSprungziele) {
    EXPECT_EQ(dis({0xE6, 0xF8, 0x96}), "LD P01M,#96H");
    EXPECT_EQ(dis({0x82, 0x42}), "LDE r4,@rr2");
    EXPECT_EQ(dis({0x31, 0xF0}), "SRP #0F0H");
    EXPECT_EQ(dis({0xD7, 0xA0, 0xF0}), "LD SIO(r0),r10");
    EXPECT_EQ(dis({0x8B, 0xFE}), "JR 0100H");
    EXPECT_EQ(dis({0x0F}), "DB 0FH");
    EXPECT_EQ(dis({0x80, 0x34}), "DECW 34H");
    EXPECT_EQ(dis({0x80, 0xE4}), "DECW rr4");
    std::vector<uint8_t> b = {0xD6, 0x12, 0x34};
    const auto e = z8dis::disasm([&](uint16_t a) { return b[a]; }, 0);
    EXPECT_TRUE(e.ruft); EXPECT_TRUE(e.hatZiel); EXPECT_EQ(e.ziel, 0x1234);
    b = {0xAF, 0, 0};
    EXPECT_TRUE(z8dis::disasm([&](uint16_t a) { return b[a]; }, 0).endet);
    b = {0x8D, 0x12, 0x34};
    EXPECT_TRUE(z8dis::disasm([&](uint16_t a) { return b[a]; }, 0).endet);
    b = {0x6D, 0x12, 0x34};
    EXPECT_FALSE(z8dis::disasm([&](uint16_t a) { return b[a]; }, 0).endet);
}
