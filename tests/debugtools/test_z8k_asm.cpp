// Assembler-Tests (tools/z8000/z8k_asm.h):
//  * Rundlauf Kodierung → Disassembler → Assembler über JEDE Tabellenzeile, beide Modi
//  * Assembler-Eigenschaften (Marken, Direktiven, Fehler)
//  * Nebenprobe gegen z8001asm.py an den EM256-Firmwares (tests/fixtures/z8000/)
#include "tools/z8000/z8k_asm.h"
#include "tools/z8000/z8k_disasm.h"

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace z8k;

#ifndef Z8K_FIXTURE_DIR
#error "Z8K_FIXTURE_DIR fehlt (tests/debugtools/CMakeLists.txt)"
#endif

namespace {

/// Repräsentative Operandenwerte für Zeile `in`, Operand i.  variant 1 = zweite Sorte
/// (kurze Segmentadresse, cc = T, leere Flagliste, Grenzwerte …).
Operand synth(const Insn& in, int i, bool seg, int variant) {
    const OpSpec& s = in.op[i];
    Operand o;
    o.kind = s.kind;
    const bool v1 = variant == 1;
    static const uint8_t rb[4] = {0x3, 0x9, 0xC, 0x5};
    static const uint8_t rw[4] = {3, 9, 12, 5};
    static const uint8_t rl[4] = {2, 6, 10, 14};
    static const uint8_t rq[4] = {4, 8, 12, 0};
    static const uint8_t irs[4] = {4, 10, 12, 6};
    static const uint8_t irn[4] = {5, 11, 13, 7};
    switch (s.kind) {
    case Kind::RB: o.reg = rb[i]; break;
    case Kind::RW: o.reg = v1 ? 15 : rw[i]; break;
    case Kind::RL: o.reg = rl[i]; break;
    case Kind::RQ: o.reg = rq[i]; break;
    case Kind::RP: o.reg = seg ? 6 : 7; break;
    case Kind::IR: o.reg = seg ? irs[i] : irn[i]; break;
    case Kind::IO: o.reg = uint8_t(i == 0 ? 7 : 13); break;
    case Kind::DA: case Kind::X:
        if (s.kind == Kind::X) o.reg = uint8_t(i == 0 ? 3 : 6);
        if (seg) { o.seg = 5; o.shortSeg = v1; o.value = v1 ? 0x34 : 0x1234; }
        else o.value = v1 ? 0xFFFE : 0x1234;
        break;
    case Kind::BA: o.reg = 6; o.value = v1 ? 0xFFF0 : 0x0018; break;
    case Kind::BX: o.reg = seg ? 4 : 5; o.reg2 = v1 ? 0 : 3; break;
    case Kind::RA16: o.disp = v1 ? -6 : 0x100; break;
    case Kind::RA8:  o.disp = v1 ? 20 : -6; break;
    case Kind::RA7:  o.disp = v1 ? 0 : -10; break;
    case Kind::RA12: o.disp = v1 ? -100 : 100; break;
    case Kind::IMB:  o.value = v1 ? 0xFF : 0x5A; break;
    case Kind::IMW:  o.value = v1 ? 0 : 0xED4D; break;
    case Kind::IML:  o.value = v1 ? 0xFFFFFFFFu : 0x0012ED4Du; break;
    case Kind::IM8:  o.value = v1 ? 0xFF : 0x42; break;
    case Kind::IM4:  o.value = v1 ? 15 : 9; break;
    case Kind::N16: case Kind::LDMN: o.value = v1 ? 16 : 5; break;
    case Kind::BIT:  o.value = in.has(Z8K_B) ? (v1 ? 7 : 5) : (v1 ? 15 : 13); break;
    case Kind::SHL:  { int mx = in.has(Z8K_B) ? 8 : in.has(Z8K_L) ? 32 : 16; o.disp = v1 ? mx : 3; o.value = uint32_t(o.disp); break; }
    case Kind::SHR:  { int mx = in.has(Z8K_B) ? 8 : in.has(Z8K_L) ? 32 : 16; o.disp = v1 ? -mx : -3; o.value = uint32_t(-o.disp); break; }
    case Kind::CC:   o.value = v1 ? 8 : 14; break;
    case Kind::FL:   o.value = v1 ? 0 : 0xA; break;
    case Kind::INT:  o.value = v1 ? 3 : 1; break;
    case Kind::CTL:  o.value = v1 ? 2 : 5; break;
    case Kind::PORT: o.value = v1 ? 0xFFFF : 0x00A8; break;
    case Kind::RAW:  o.value = s.f.width >= 16 ? (v1 ? 0xFFFF : 0x1234) : s.f.width >= 8 ? (v1 ? 0xFF : 0x3C) : (v1 ? 15 : 5); break;
    case Kind::LIT:  o.value = s.lit; break;
    case Kind::FLAGS: case Kind::None: break;
    }
    return o;
}

std::string hexWords(const std::vector<uint16_t>& w) {
    std::string s; char b[8];
    for (auto x : w) { std::snprintf(b, sizeof b, "%04X ", x); s += b; }
    return s;
}

std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ── Rundlauf über die ganze Tabelle ──────────────────────────────────────────

class Z8kRoundTrip : public ::testing::TestWithParam<bool> {};

TEST_P(Z8kRoundTrip, EveryRowEncodeDisasmAssemble) {
    const bool seg = GetParam();
    const uint8_t pcSeg = seg ? 3 : 0;
    const uint16_t pc = 0x1000;
    int checked = 0;
    for (auto& in : Table::get().insns()) {
        for (int variant = 0; variant < 2; ++variant) {
            Operand ops[4];
            for (int i = 0; i < in.nops; ++i) ops[i] = synth(in, i, seg, variant);
            std::vector<uint16_t> words;
            std::string err;
            ASSERT_TRUE(encode(in, ops, in.nops, seg, words, &err))
                << in.mn << " " << in.src->ops << " v" << variant << ": " << err;

            Decoded d;
            ASSERT_TRUE(decodeWords(words.data(), int(words.size()), seg, d))
                << in.mn << " " << in.src->ops << ": " << hexWords(words);
            ASSERT_EQ(d.insn, &in) << in.mn << " " << in.src->ops << " dekodiert als " << d.insn->mn
                                   << " " << d.insn->src->ops << ": " << hexWords(words);
            ASSERT_EQ(d.nwords, int(words.size())) << in.mn << " " << in.src->ops;

            std::string text = formatDecoded(d, pcSeg, uint16_t(pc + d.bytes()));
            std::vector<uint16_t> back;
            const Insn* chosen = nullptr;
            ASSERT_TRUE(assembleLine(text, seg, pcSeg, pc, back, err, nullptr, &chosen))
                << "'" << text << "' (" << in.src->ops << "): " << err;
            EXPECT_EQ(chosen, &in) << "'" << text << "' landet auf " << chosen->mn << " " << chosen->src->ops;
            EXPECT_EQ(hexWords(back), hexWords(words)) << "'" << text << "' (" << in.src->ops << ")";
            ++checked;
        }
    }
    EXPECT_EQ(checked, int(2 * kRowCount));
}

INSTANTIATE_TEST_SUITE_P(Modi, Z8kRoundTrip, ::testing::Values(false, true),
                         [](const auto& p) { return p.param ? "segmentiert" : "nichtsegmentiert"; });

// ── Assembler-Eigenschaften ──────────────────────────────────────────────────

TEST(Z8kAsm, LabelsDirectivesAndForwardReferences) {
    const char* src =
        "; Probe\n"
        "        ORG  %0100\n"
        "WERT    EQU  %ED4D\n"
        "START:  LD   R0,#WERT       ! Zilog-Kommentar !\n"
        "        LDB  RL0,#'A'\n"
        "        JR   Z,WEITER\n"
        "        CALL UP\n"
        "WEITER: DJNZ R1,START\n"
        "        LDR  R2,DATEN\n"
        "UP:     RET\n"
        "DATEN:  DW   1,%1234,WEITER\n"
        "        DB   'Hi',0\n"
        "        EVEN\n"
        "        DL   %12345678\n"
        "        DS   4\n"
        "ENDE:\n";
    AsmResult r = assemble(src);
    ASSERT_TRUE(r.ok()) << r.errors[0].line << ": " << r.errors[0].text;
    EXPECT_EQ(r.symbols["WERT"], 0xED4D);
    EXPECT_EQ(r.symbols["START"], 0x0100);
    uint32_t base = 0;
    auto img = r.flat(&base);
    EXPECT_EQ(base, 0x0100u);
    // DJNZ: disp = (PC_danach − Ziel)/2 = (0x010E − 0x0100)/2 = 7
    const std::vector<uint8_t> want2 = {
        0x21, 0x00, 0xED, 0x4D, 0xC8, 0x41, 0xE6, 0x02, 0x5F, 0x00, 0x01, 0x12,
        0xF1, 0x87,                     // DJNZ R1,START
        0x31, 0x02, 0x00, 0x02,         // 010E LDR R2,DATEN (0114): 0114 − 0112 = 2
        0x9E, 0x08,                     // 0112 RET
        0x00, 0x01, 0x12, 0x34, 0x01, 0x0C,   // 0114 DW
        'H', 'i', 0x00, 0x00,           // DB + EVEN
        0x12, 0x34, 0x56, 0x78,         // DL
        0, 0, 0, 0};                    // DS 4
    EXPECT_EQ(img, want2);
    EXPECT_EQ(r.symbols["ENDE"], 0x0100 + int64_t(want2.size()));
}

TEST(Z8kAsm, SegmentedAddressesAndSyntax) {
    const char* src =
        "        SEG\n"
        "        ORG  <<3>>%1000\n"
        "ZIEL:   CALL ZIEL\n"
        "        JP   NZ,|<<3>>%12|\n"
        "        LD   R4,<<5>>%231A(R3)\n"
        "        LDL  RR2,#ZIEL\n"
        "        LDA  RR4,RR2(#%10)\n"
        "        PUSH @RR14,R1\n"
        "        JR   ZIEL\n"
        "        LDB.L RH1,#%12\n";
    AsmResult r = assemble(src);
    ASSERT_TRUE(r.ok()) << r.errors[0].line << ": " << r.errors[0].text;
    uint32_t base = 0;
    auto img = r.flat(&base);
    EXPECT_EQ(base, (3u << 16) | 0x1000u);
    const std::vector<uint8_t> want = {
        0x5F, 0x00, 0x83, 0x00, 0x10, 0x00,        // CALL <<3>>%1000
        0x5E, 0x0E, 0x03, 0x12,                    // JP NZ,|<<3>>%12|
        0x61, 0x34, 0x85, 0x00, 0x23, 0x1A,        // LD R4,<<5>>%231A(R3)
        0x14, 0x02, 0x03, 0x00, 0x10, 0x00,        // LDL RR2,#<<3>>%1000
        0x34, 0x24, 0x00, 0x10,                    // LDA RR4,RR2(#%10)
        0x93, 0xE1,                                // PUSH @RR14,R1
        0xE8, 0xF1,                                // JR ZIEL: (0x101E → 0x1000) = −30/2 = −15
        0x20, 0x01, 0x12, 0x12,                    // LDB.L RH1,#%12
    };
    EXPECT_EQ(img, want);
}

TEST(Z8kAsm, DefaultsAndAliases) {
    struct { const char* src; bool seg; std::vector<uint16_t> w; } k[] = {
        {"INC R1", false, {0xA910}},
        {"DEC R1,#16", false, {0xAB1F}},
        {"RL R2", false, {0xB320}},
        {"RL R2,#2", false, {0xB322}},
        {"SRL R3", false, {0xB331, 0xFFFF}},
        {"SLL R3,#0", false, {0xB331, 0x0000}},
        {"SRAB RL0,#8", false, {0xB289, 0x00F8}},
        {"JP EQ,%1234", false, {0x5E06, 0x1234}},
        {"JR T,$", false, {0xE8FF}},
        {"RET T", false, {0x9E08}},
        {"SETFLG S,Z,C,P", false, {0x8DF1}},
        {"DI", false, {0x7C03}},
        {"EI NVI,VI", false, {0x7C04}},
        {"LDCTL NSP,R2", false, {0x7D2F}},
        {"ld r0 , # %ed4d", false, {0x2100, 0xED4D}},
        {"LD R1,0x1234", false, {0x6101, 0x1234}},
        {"LD R1,1234H", false, {0x6101, 0x1234}},
        {"LD R1,#%(2)1010", false, {0x2101, 0x000A}},
        {"LDL RR0,#SEG(<<5>>0)", true, {0x1400, 0x0000, 0x0005}},
    };
    for (auto& e : k) {
        std::vector<uint16_t> w; std::string err;
        ASSERT_TRUE(assembleLine(e.src, e.seg, 0, 0, w, err)) << e.src << ": " << err;
        EXPECT_EQ(hexWords(w), hexWords(e.w)) << e.src;
    }
}

TEST(Z8kAsm, ErrorsCarryLineNumbers) {
    const char* src =
        "        LD   R0,#1\n"
        "        FOO  R1\n"               // 2: unbekannt
        "        LDL  RR3,RR4\n"          // 3: ungerades Paar
        "        LD   R1,@R0\n"           // 4: R0 als Zeiger
        "        INC  R1,#17\n"           // 5: Bereich
        "        JR   WEIT\n"             // 6: unbekanntes Symbol
        "        LD   R1,@RR2\n"          // 7: nonseg braucht @Rn
        "        JR   FERN\n"             // 8: ausser Reichweite
        "        DS   400\n"
        "FERN:   NOP\n";
    AsmResult r = assemble(src);
    std::map<int, std::string> byLine;
    for (auto& e : r.errors) byLine[e.line] = e.text;
    EXPECT_EQ(byLine.size(), 7u);
    for (int l : {2, 3, 4, 5, 6, 7, 8}) EXPECT_TRUE(byLine.count(l)) << "Zeile " << l;
    EXPECT_NE(byLine[3].find("RR3"), std::string::npos) << byLine[3];
}

// ── EM256-Firmwares: Nebenprobe gegen z8001asm.py ────────────────────────────
//
// z8001asm.py (CPA_Workbench/tools/16bitTest) kodiert jede Adresse als EIN Wort
// und kennt nur Rn als Zeiger — also die nichtsegmentierte Form.  Deshalb wird hier
// nichtsegmentiert verglichen.  Die bekannten Abweichungen sind Fehler von
// z8001asm.py (Fundstelle im README von tools/z8000):
//   * PUSH/POP/PUSHL/POPL maskieren das Stapelregister auf gerade (@R15 → Feld 14).
namespace {
struct FwCase { const char* name; std::vector<uint32_t> diffOffsets; };
}

TEST(Z8kFirmware, AssemblesLikeZ8001AsmExceptKnownBugs) {
    const FwCase cases[] = {
        {"fw_add", {}}, {"fw_add32", {}}, {"fw_byte", {}}, {"fw_logic", {}},
        {"fw_loop", {}}, {"fw_march", {}}, {"fw_memrw", {}}, {"fw_stack", {77, 79, 81, 83}}, {"fw_sub", {}},
    };
    for (auto& c : cases) {
        std::string dir = Z8K_FIXTURE_DIR;
        std::string src = readFile(dir + "/" + c.name + ".s");
        std::string ref = readFile(dir + "/" + c.name + ".z8001asm.bin");
        ASSERT_FALSE(src.empty()) << c.name;
        ASSERT_FALSE(ref.empty()) << c.name;
        AsmOptions opt; opt.laxPointers = true;        // z8001asm schreibt @R7 und @RR8 gemischt
        AsmResult r = assemble(src, opt);
        ASSERT_TRUE(r.ok()) << c.name << ":" << r.errors[0].line << ": " << r.errors[0].text;
        uint32_t base = 1;
        auto img = r.flat(&base);
        EXPECT_EQ(base, 0u) << c.name;
        ASSERT_EQ(img.size(), ref.size()) << c.name;
        std::vector<uint32_t> diffs;
        for (size_t i = 0; i < img.size(); ++i)
            if (img[i] != uint8_t(ref[i])) diffs.push_back(uint32_t(i));
        EXPECT_EQ(diffs, c.diffOffsets) << c.name;
    }
}

// Die Firmwares so disassemblieren, wie der U8001 sie ausführt (FCW = %C000 →
// segmentiert): jedes Wort ab %0040 bis zum Ende ist ein bekannter Befehl.
TEST(Z8kFirmware, DisassemblesReadablySegmented) {
    for (const char* name : {"fw_add", "fw_add32", "fw_byte", "fw_logic", "fw_loop",
                             "fw_march", "fw_memrw", "fw_stack", "fw_sub"}) {
        std::string ref = readFile(std::string(Z8K_FIXTURE_DIR) + "/" + name + ".z8001asm.bin");
        const uint8_t* b = reinterpret_cast<const uint8_t*>(ref.data());
        // Reset-Vektor: FCW an 0002, PC-Segment 0004, PC-Offset 0006
        ASSERT_GE(ref.size(), 0x42u) << name;
        EXPECT_EQ((b[2] << 8) | b[3], 0xC000) << name;
        uint16_t start = uint16_t((b[6] << 8) | b[7]);
        EXPECT_EQ(start, 0x0040) << name;
        size_t pc = start;
        int unknown = 0, n = 0;
        while (pc + 1 < ref.size()) {
            Line l = disasmBytes(b, ref.size(), 0, uint16_t(pc), true, 0);
            if (!l.dec.insn) ++unknown;
            ++n;
            pc += size_t(l.bytes);
        }
        EXPECT_EQ(unknown, 0) << name;
        EXPECT_GT(n, 5) << name;
    }
}
