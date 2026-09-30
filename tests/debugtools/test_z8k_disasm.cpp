// Disassembler-Tests (tools/z8000/z8k_disasm.h): Beispiele aus dem Zilog Z8000 CPU
// User's Reference Manual (Kap. 5 Adressierungsarten, Kap. 6 Befehlsseiten), jeweils
// mit den Worten, die die Formatbilder des Handbuchs für diesen Befehl ergeben.
#include "tools/z8000/z8k_disasm.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace z8k;

namespace {
struct Ex {
    bool seg;
    uint8_t pcSeg;
    uint16_t pc;
    std::vector<uint16_t> w;
    const char* text;
    const char* where;       // Fundstelle im Handbuch
};

std::string dis(const Ex& e, int* bytes = nullptr) {
    auto rd = [&](uint16_t a) -> uint16_t {
        size_t i = size_t(uint16_t(a - e.pc)) / 2;
        return i < e.w.size() ? e.w[i] : 0xFFFF;
    };
    Line l = disasm(rd, e.pc, e.seg, e.pcSeg);
    if (bytes) *bytes = l.bytes;
    return l.text;
}
} // namespace

TEST(Z8kDisasm, ManualExamplesNonsegmented) {
    const Ex k[] = {
        {false, 0, 0x0000, {0x2100, 0xED4D}, "LD R0,#%ED4D", "Kap. 6 LD, IM"},
        {false, 0, 0x0000, {0xA123}, "LD R3,R2", "5.4.1 LD R2,R3 (Form)"},
        {false, 0, 0x0000, {0x2152}, "LD R2,@R5", "5.4.3 Beispiel IR"},
        {false, 0, 0x0000, {0xC255}, "LDB RH2,#%55", "5.4.2 Beispiel IM"},
        {false, 0, 0x0000, {0x6002, 0x5E23}, "LDB RH2,%5E23", "5.4.4 Beispiel DA"},
        {false, 0, 0x0000, {0x6134, 0x231A}, "LD R4,%231A(R3)", "5.4.5 Beispiel X"},
        {false, 0, 0x0202, {0x3102, 0x0002}, "LDR R2,%0208", "5.4.6 LDR R2,$+%6 (3102 0002 bei 0202)"},
        {false, 0, 0x0206, {0xE801}, "JR %020A", "5.4.6 Folgewort E801"},
        {false, 0, 0x0000, {0x3752, 0x0018}, "LDL R5(#%0018),RR2", "5.4.7 Beispiel BA"},
        {false, 0, 0x0000, {0x7152, 0x0300}, "LD R2,R5(R3)", "5.4.8 Beispiel BX"},
        {false, 0, 0x0000, {0x4102, 0x1254}, "ADD R2,%1254", "Kap. 6 ADD, Beispiel AUGEND"},
        {false, 0, 0x0000, {0x8131}, "ADD R1,R3", "Kap. 6 ADC, Beispiel"},
        {false, 0, 0x0000, {0xB520}, "ADC R0,R2", "Kap. 6 ADC, Beispiel"},
        {false, 0, 0x0000, {0xBD39}, "LDK R3,#9", "Kap. 6 LDK, Beispiel"},
        {false, 0, 0x0000, {0xB32D, 0x0008}, "SLAL RR2,#8", "Kap. 6 SLA, Beispiel"},
        {false, 0, 0x0000, {0xB86E, 0x0C9E}, "TRTDRB @R6,@R9,R12", "Kap. 6 TRTDRB, Beispiel"},
        {false, 0, 0x0000, {0x7C01}, "DI VI", "Kap. 6 DI, Beispiel"},
        {false, 0, 0x0000, {0x7C05}, "EI VI", "Kap. 6 EI, Beispiel"},
        {false, 0, 0x0000, {0x7C00}, "DI VI,NVI", "Kap. 6 DI, beide"},
        {false, 0, 0x0000, {0x8D07}, "NOP", "Kap. 6 NOP"},
        {false, 0, 0x0000, {0x7A00}, "HALT", "Kap. 6 HALT"},
        {false, 0, 0x0000, {0x7B00}, "IRET", "Kap. 6 IRET"},
        {false, 0, 0x0000, {0x9E08}, "RET", "Kap. 6 RET (cc = T)"},
        {false, 0, 0x0000, {0x9E0E}, "RET NZ", "Kap. 6 RET"},
        {false, 0, 0x0000, {0x5F00, 0x1234}, "CALL %1234", "Kap. 6 CALL, DA"},
        {false, 0, 0x0000, {0x7F2A}, "SC #%2A", "Kap. 6 SC"},
        {false, 0, 0x0000, {0x8D81}, "SETFLG C", "Kap. 6 SETFLG"},
        {false, 0, 0x0000, {0x8C31}, "LDCTLB RH3,FLAGS", "Kap. 6 LDCTLB"},
        {false, 0, 0x0000, {0x7D3D}, "LDCTL PSAP,R3", "Kap. 6 LDCTL (nonseg: PSAP)"},
        {false, 0, 0x0000, {0x1C31, 0x0405}, "LDM R4,@R3,#6", "Kap. 6 LDM"},
        {false, 0, 0x0000, {0xBB21, 0x0310}, "LDIR @R1,@R2,R3", "Kap. 6 LDIR"},
        {false, 0, 0x0000, {0x3A94, 0x00A9}, "INB RL1,%00A9", "Kap. 6 IN, DA"},
        {false, 0, 0x0000, {0xF281}, "DJNZ R2,%0000", "Kap. 6 DJNZ (Schleife auf sich selbst)"},
        {false, 0, 0x0100, {0xD001}, "CALR %0100", "Kap. 6 CALR, disp 1"},
        {false, 0, 0x0000, {0x2700, 0x0200}, "BIT R2,R0", "Kap. 6 BIT dynamisch"},
    };
    for (auto& e : k) {
        int bytes = 0;
        EXPECT_EQ(dis(e, &bytes), e.text) << e.where;
        EXPECT_EQ(bytes, int(e.w.size() * 2)) << e.where;
    }
}

TEST(Z8kDisasm, ManualExamplesSegmented) {
    const Ex k[] = {
        {true, 0, 0x0000, {0x2142}, "LD R2,@RR4", "5.5.3 Beispiel IR"},
        {true, 0, 0x0000, {0x6002, 0x0F23}, "LDB RH2,|<<15>>%23|", "5.5.4 Beispiel DA kurz"},
        {true, 0, 0x0000, {0x6134, 0x8500, 0x231A}, "LD R4,<<5>>%231A(R3)", "5.5.5 Beispiel X"},
        {true, 13, 0x0202, {0x3102, 0x0002}, "LDR R2,<<13>>%0208", "5.5.6 LDR R2,$+6 bei <<13>>0202"},
        {true, 0, 0x0000, {0x3742, 0x0018}, "LDL RR4(#%0018),RR2", "5.5.7 Beispiel BA"},
        {true, 0, 0x0000, {0x7142, 0x0300}, "LD R2,RR4(R3)", "5.5.8 Beispiel BX"},
        {true, 0, 0x0000, {0x5F00, 0x8300, 0x1234}, "CALL <<3>>%1234", "Kap. 6 CALL, SL"},
        {true, 0, 0x0000, {0x5E08, 0x0312}, "JP |<<3>>%12|", "Kap. 6 JP, SS"},
        {true, 0, 0x0000, {0x93E1}, "PUSH @RR14,R1", "Kap. 6 PUSH"},
        {true, 0, 0x0000, {0x7D5D}, "LDCTL PSAPOFF,R5", "Kap. 6 LDCTL (seg)"},
        {true, 0, 0x0000, {0x7640, 0x8100, 0x0010}, "LDA RR0,<<1>>%0010(R4)", "Kap. 6 LDA, X"},
    };
    for (auto& e : k) {
        int bytes = 0;
        EXPECT_EQ(dis(e, &bytes), e.text) << e.where;
        EXPECT_EQ(bytes, int(e.w.size() * 2)) << e.where;
    }
}

TEST(Z8kDisasm, UnknownAndTargets) {
    Ex u{false, 0, 0, {0x3600}, "", ""};
    int bytes = 0;
    EXPECT_EQ(dis(u, &bytes), ".WORD %3600");
    EXPECT_EQ(bytes, 2);

    // Sprungziel für den Debugger (step-over/where)
    auto rd = [](uint16_t a) -> uint16_t { return a == 0x0100 ? 0xE8FF : 0; };  // JR auf sich selbst
    Line l = disasm(rd, 0x0100, false);
    EXPECT_EQ(l.text, "JR %0100");
    EXPECT_TRUE(l.hasTarget); EXPECT_EQ(l.target, 0x0100);
    EXPECT_TRUE(l.dec.insn->has(Z8K_JUMP));
}
