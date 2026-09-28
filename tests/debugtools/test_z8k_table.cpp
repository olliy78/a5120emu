// Unit-Tests der U8001/U8002-Befehlstabelle (tools/z8000/z8k_table.h, z8k_codec.h):
// Aufbau, Eindeutigkeit, Takte/Merkmale gegen das Zilog-Handbuch (Anhang C).
#include "tools/z8000/z8k_codec.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

using namespace z8k;

namespace {
const Insn* findRow(const char* mn, const char* ops, const char* w0 = nullptr) {
    for (auto& in : Table::get().insns())
        if (!std::strcmp(in.mn, mn) && !std::strcmp(in.src->ops, ops) && (!w0 || !std::strcmp(in.src->w0, w0)))
            return &in;
    return nullptr;
}
} // namespace

TEST(Z8kTable, BuildsAllRows) {
    const Table& t = Table::get();                 // wirft bei Formfehlern einer Zeile
    EXPECT_EQ(t.insns().size(), kRowCount);
    EXPECT_GE(kRowCount, 430u);
    std::set<std::string> mn;
    for (auto& in : t.insns()) mn.insert(in.mn);
    // 110 Grundbefehle der Zilog-Übersicht + B/L-Varianten + 6 EPA-Schablonen
    EXPECT_GE(mn.size(), 190u);
}

// Zu jedem ersten Befehlswort darf höchstens EINE Zeile ohne zweites Wort passen,
// und Zeilen mit zweitem Wort müssen sich dort widerspruchsfrei unterscheiden.
TEST(Z8kTable, TableIsUnambiguous) {
    const Table& t = Table::get();
    for (uint32_t w = 0; w < 65536; ++w) {
        int n = 0;
        const uint16_t* c = t.candidates(uint16_t(w), n);
        if (n <= 1) continue;
        for (int i = 0; i < n; ++i) {
            const Insn& a = t.at(c[i]);
            ASSERT_TRUE(a.hasW1) << "Wort " << std::hex << w << ": " << a.mn << " ohne 2. Wort neben anderen Zeilen";
            for (int j = i + 1; j < n; ++j) {
                const Insn& b = t.at(c[j]);
                uint16_t both = a.mask1 & b.mask1;
                ASSERT_NE((a.match1 ^ b.match1) & both, 0)
                    << "Wort " << std::hex << w << ": " << a.mn << " (" << a.src->ops << ") und "
                    << b.mn << " (" << b.src->ops << ") nicht unterscheidbar";
            }
        }
    }
}

// Die Opcode-Map (Handbuch Anhang) — Stichproben der ersten Befehlsworte.
TEST(Z8kTable, OpcodeMapSamples) {
    struct { uint16_t w; const char* mn; } k[] = {
        {0x0012, "ADDB"}, {0x8112, "ADD"}, {0x1012, "CPL"}, {0x1112, "PUSHL"}, {0x9712, "POP"},
        {0x3012, "LDB"}, {0x3002, "LDRB"}, {0x3402, "LDAR"}, {0x7A00, "HALT"}, {0x7B00, "IRET"},
        {0x7B08, "MSET"}, {0x7B09, "MRES"}, {0x7B0A, "MBIT"}, {0x7B3D, "MREQ"}, {0x7C00, "DI"},
        {0x7C04, "EI"}, {0x7D2A, "LDCTL"}, {0x7F12, "SC"}, {0x8D07, "NOP"}, {0x8D81, "SETFLG"},
        {0x8D43, "RESFLG"}, {0x8D25, "COMFLG"}, {0x8C11, "LDCTLB"}, {0x8C19, "LDCTLB"},
        {0x9E08, "RET"}, {0xAF3E, "TCC"}, {0xAE3E, "TCCB"}, {0xB030, "DAB"}, {0xB130, "EXTSB"},
        {0xB13A, "EXTS"}, {0xB137, "EXTSL"}, {0xB412, "ADCB"}, {0xB512, "ADC"}, {0xB612, "SBCB"},
        {0xB712, "SBC"}, {0xBC12, "RRDB"}, {0xBD39, "LDK"}, {0xBE12, "RLDB"}, {0xC255, "LDB"},
        {0xD123, "CALR"}, {0xE801, "JR"}, {0xF281, "DJNZ"}, {0xF201, "DBJNZ"}, {0x3C12, "INB"},
        {0x3D12, "IN"}, {0x3E12, "OUTB"}, {0x3F12, "OUT"}, {0x3910, "LDPS"}, {0x7610, "LDA"},
        {0x1E18, "JP"}, {0x1F10, "CALL"}, {0x5F00, "CALL"}, {0x0D19, "PUSH"},
    };
    const Table& t = Table::get();
    for (auto& e : k) {
        int n = 0;
        const uint16_t* c = t.candidates(e.w, n);
        ASSERT_GE(n, 1) << std::hex << e.w;
        EXPECT_STREQ(t.at(c[0]).mn, e.mn) << std::hex << e.w;
    }
    // Reservierte Kodierungen der Opcode-Map bleiben unbelegt
    for (uint16_t w : {0x3600, 0x3800, 0x7800, 0x7E00, 0x9D00, 0x9F00, 0xB900, 0xBF00}) {
        int n = 0; t.candidates(w, n);
        EXPECT_EQ(n, 0) << std::hex << w;
    }
    // Feld 0 unterscheidet IM/IR, DA/X, BA/RA
    int n = 0;
    EXPECT_STREQ(t.at(t.candidates(0x2100, n)[0]).src->ops, "RW:d,IMW");
    EXPECT_STREQ(t.at(t.candidates(0x2150, n)[0]).src->ops, "RW:d,IR:s");
    EXPECT_STREQ(t.at(t.candidates(0x6100, n)[0]).src->ops, "RW:d,DA");
    EXPECT_STREQ(t.at(t.candidates(0x6150, n)[0]).src->ops, "RW:d,X:s");
}

// Takte laut Handbuch (Anhang C und Befehlsseiten) — je Gruppe eine Stichprobe.
TEST(Z8kTable, CyclesFromManual) {
    struct { const char* mn; const char* ops; int ns, ss, sl; } k[] = {
        {"ADD",  "RW:d,RW:s", 4, 4, 4},       {"ADD", "RW:d,DA", 9, 10, 12},
        {"ADDL", "RL:d,X:s", 16, 16, 19},     {"ADC", "RW:d,RW:s", 5, 5, 5},
        {"LD",   "RW:d,RW:s", 3, 3, 3},       {"LDL", "RL:d,RL:s", 5, 5, 5},
        {"LD",   "X:d,RW:s", 12, 12, 15},     {"LDL", "DA,RL:s", 14, 15, 17},
        {"LD",   "RW:d,BA:s", 14, 14, 14},    {"LDL", "RL:d,BX:s:x", 17, 17, 17},
        {"LDB",  "RB:d,IM8:i", 5, 5, 5},      {"LDA", "RP:d,DA", 12, 13, 15},
        {"LDAR", "RP:d,RA16", 15, 15, 15},    {"LDK", "RW:d,IM4:i", 5, 5, 5},
        {"CALL", "DA", 12, 18, 20},           {"CALL", "IR:d", 10, 15, 15},
        {"CALR", "RA12:d", 10, 15, 15},       {"JR", "CC:c,RA8:d", 6, 6, 6},
        {"DJNZ", "RW:r,RA7:d", 11, 11, 11},   {"IRET", "", 13, 16, 16},
        {"SC",   "IM8:i", 33, 39, 39},        {"MULT", "RL:d,X:s", 72, 72, 75},
        {"DIVL", "RQ:d,DA", 745, 746, 748},   {"DIV", "RL:d,X:s", 109, 109, 112},
        {"PUSHL","IR:d,X:s", 21, 21, 24},     {"POPL", "DA,IR:s", 23, 23, 25},
        {"LDPS", "X:s", 17, 20, 23},          {"TESTL","DA", 16, 17, 19},
        {"TSET", "X:d", 15, 15, 18},          {"EXB", "RB:d,DA", 15, 16, 18},
        {"IN",   "RW:d,PORT", 12, 12, 12},    {"INIR", "IR:d,IO:s,RW:r", 11, 11, 11},
        {"CPSIR","IR:d,IR:s,RW:r,CC:c", 11, 11, 11}, {"RLDB", "RB:l,RB:s", 9, 9, 9},
    };
    for (auto& e : k) {
        const Insn* in = findRow(e.mn, e.ops);
        ASSERT_NE(in, nullptr) << e.mn << " " << e.ops;
        EXPECT_EQ(in->cyc[0], e.ns) << e.mn << " " << e.ops;
        EXPECT_EQ(in->cyc[1], e.ss) << e.mn << " " << e.ops;
        EXPECT_EQ(in->cyc[2], e.sl) << e.mn << " " << e.ops;
    }
    // Formeln: Grundwert + perN·n
    EXPECT_EQ(findRow("LDIR", "IR:d,IR:s,RW:r")->perN, 9);
    EXPECT_EQ(findRow("OTIR", "IO:d,IR:s,RW:r")->perN, 10);
    EXPECT_EQ(findRow("TRTIRB", "IR:d,IR:s,RW:r")->perN, 14);
    EXPECT_EQ(findRow("SLA", "RW:d,SHL:c")->cyc[0], 13);
    EXPECT_EQ(findRow("SLA", "RW:d,SHL:c")->perN, 3);
    EXPECT_EQ(findRow("SDAL", "RL:d,RW:s")->cyc[0], 15);
    EXPECT_EQ(findRow("LDM", "RW:d,DA,LDMN:n")->cyc[2], 17);
    EXPECT_EQ(findRow("MULTL", "RQ:d,RL:s")->perN, 7);
    EXPECT_EQ(findRow("HALT", "")->perN, 3);
    // Rotieren: 6 Takte um 1, 7 um 2
    EXPECT_EQ(findRow("RL", "RW:d,#1")->cyc[0], 6);
    EXPECT_EQ(findRow("RRCB", "RB:d,#2")->cyc[0], 7);
    // Bedingt: genommen / nicht genommen
    const Insn* jp = findRow("JP", "CC:c,IR:d");
    EXPECT_EQ(jp->cyc[1], 15); EXPECT_EQ(jp->alt[1], 7);
    const Insn* ret = findRow("RET", "CC:c");
    EXPECT_EQ(ret->cyc[0], 10); EXPECT_EQ(ret->cyc[1], 13); EXPECT_EQ(ret->alt[0], 7);
}

TEST(Z8kTable, PrivilegedExactlyTheSystemInstructions) {
    const std::set<std::string> priv = {
        "IN","INB","SIN","SINB","OUT","OUTB","SOUT","SOUTB",
        "INI","INIB","INIR","INIRB","SINI","SINIB","SINIR","SINIRB",
        "IND","INDB","INDR","INDRB","SIND","SINDB","SINDR","SINDRB",
        "OUTI","OUTIB","OTIR","OTIRB","SOUTI","SOUTIB","SOTIR","SOTIRB",
        "OUTD","OUTDB","OTDR","OTDRB","SOUTD","SOUTDB","SOTDR","SOTDRB",
        "HALT","IRET","MSET","MRES","MBIT","MREQ","DI","EI","LDCTL","LDPS"};
    for (auto& in : Table::get().insns())
        EXPECT_EQ(in.has(Z8K_PRIV), priv.count(in.mn) == 1) << in.mn;
}

// Dekodieren über den Rückruf: nur so viele Worte wie nötig, in Reihenfolge.
TEST(Z8kCodec, FetchesOnlyWhatItNeeds) {
    const uint16_t prog[] = {0x5F00, 0x8300, 0x1234, 0xFFFF};
    std::vector<int> asked;
    Decoded d;
    ASSERT_TRUE(decode([&](int i) { asked.push_back(i); return prog[i]; }, true, d));
    EXPECT_EQ(asked, (std::vector<int>{0, 1, 2}));
    EXPECT_STREQ(d.insn->mn, "CALL");
    EXPECT_EQ(d.op[0].seg, 3); EXPECT_EQ(d.op[0].value, 0x1234u); EXPECT_FALSE(d.op[0].shortSeg);
    EXPECT_EQ(d.cycles(), 20);                       // SL
    const uint16_t shortForm[] = {0x5F00, 0x0312};
    ASSERT_TRUE(decodeWords(shortForm, 2, true, d));
    EXPECT_TRUE(d.op[0].shortSeg); EXPECT_EQ(d.op[0].value, 0x12u);
    EXPECT_EQ(d.cycles(), 18);                       // SS
    ASSERT_TRUE(decodeWords(shortForm, 2, false, d));
    EXPECT_EQ(d.nwords, 2); EXPECT_EQ(d.op[0].value, 0x0312u);
    EXPECT_EQ(d.cycles(), 12);                       // NS
}

// Jedes erste Wort ist dekodierbar oder sauber „unbekannt" — nie ein Absturz,
// und die Zahl der unbelegten Worte ist bekannt (Doku README).
TEST(Z8kCodec, EveryFirstWordDecodesOrIsUnknown) {
    int unknown = 0;
    for (uint32_t w = 0; w < 65536; ++w) {
        for (bool seg : {false, true}) {
            const uint16_t words[6] = {uint16_t(w), 0, 0, 0, 0, 0};
            Decoded d;
            bool ok = decodeWords(words, 6, seg, d);
            if (!ok && !seg) ++unknown;
            if (ok) { EXPECT_GE(d.nwords, 1); EXPECT_LE(d.nwords, 5); }
        }
    }
    EXPECT_GT(unknown, 0);
    EXPECT_LT(unknown, 8000);
    RecordProperty("unbelegte_erste_Worte", unknown);
}
