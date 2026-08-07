// Unit-Tests für den MACRO-80-Quelltext-Assembler (tools/mac_listing.h).
//
// Kernanspruch: aus reinem `.MAC`-Quelltext (ORG 0, keine Adressspalte) dieselbe
// Adresse→Quellzeile-Tabelle bauen, die prn_listing.h aus einem .prn-Listing zieht —
// inklusive korrekter Opcode-LÄNGEN (sonst verschiebt sich alles Folgende) und der
// Objektbytes für den Versatz-Abgleich.
#include "tools/mac_listing.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>

namespace {

// Eine temporäre Quelldatei mit dem übergebenen Inhalt anlegen.
std::string writeTmp(const std::string& name, const std::string& body){
    std::string p = std::string(::testing::TempDir()) + name;
    std::ofstream f(p); f << body; f.close();
    return p;
}

// Kurzform: assemblieren und Tabelle + Bytes liefern.
struct Asm {
    prnlst::Listing lst;
    maclst::Result  res;
    maclst::Image   img;
    bool ok = false;
};
Asm run(const std::string& body, long off = 0, bool anchors = true){
    Asm a;
    std::string p = writeTmp("mac_listing_test.mac", body);
    a.ok = maclst::assemble(p, off, a.lst, a.res, &a.img, anchors);
    return a;
}

// Die Bytes ab @p addr als Vektor.
std::vector<uint8_t> bytesAt(const maclst::Image& img, uint16_t addr, int n){
    std::vector<uint8_t> v;
    for (int i=0;i<n;++i){
        auto it = img.byte.find((uint16_t)(addr+i));
        v.push_back(it==img.byte.end()? 0xEE : it->second);
    }
    return v;
}

} // namespace

// ─── Zahlen + Ausdrücke ──────────────────────────────────────────────────────
TEST(MacListing, ParsesMacro80Numbers){
    long v;
    EXPECT_TRUE (maclst::parseNum("0FFH", v)); EXPECT_EQ(v, 255);
    EXPECT_TRUE (maclst::parseNum("12H",  v)); EXPECT_EQ(v, 0x12);
    EXPECT_TRUE (maclst::parseNum("1011B",v)); EXPECT_EQ(v, 11);
    EXPECT_TRUE (maclst::parseNum("123",  v)); EXPECT_EQ(v, 123);
    EXPECT_TRUE (maclst::parseNum("'A'",  v)); EXPECT_EQ(v, 'A');
    EXPECT_FALSE(maclst::parseNum("M0EB8",v));      // Symbol, keine Zahl
    EXPECT_FALSE(maclst::parseNum("FFH",  v));      // MACRO-80: Hex braucht führende Ziffer
}

TEST(MacListing, EvaluatesExpressions){
    maclst::SymTab s{{"BASE", 0x1000}, {"N", 4}};
    EXPECT_EQ(maclst::evalExpr("BASE+2*N", s, 0), 0x1008);
    EXPECT_EQ(maclst::evalExpr("0F0H AND 3CH", s, 0), 0x30);
    EXPECT_EQ(maclst::evalExpr("$+3", s, 0x200), 0x203);
    bool unres=false;
    maclst::evalExpr("NOTHERE", s, 0, &unres);
    EXPECT_TRUE(unres);
}

// ─── Opcode-Längen (der eigentliche Zweck) ───────────────────────────────────
TEST(MacListing, ComputesInstructionLengthsAndAddresses){
    // Genau die Sequenz, deren Längen in der UDOS-Analyse von Hand gezählt wurden.
    auto a = run(
        "\tLD\tSP,0D00H\n"       // 3  @0000
        "\tCALL\tM028C\n"        // 3  @0003
        "\tLD\tHL,0C3FBH\n"      // 3  @0006
        "\tLD\t(M0EB8),HL\n"     // 3  @0009
        "\tPUSH\tIY\n"           // 2  @000C
        "\tNOP\n"                // 1  @000E
        , 0, /*anchors=*/false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.res.unknown, 0);
    EXPECT_NE(a.lst.find(0x0000), nullptr);
    EXPECT_NE(a.lst.find(0x0003), nullptr);
    EXPECT_NE(a.lst.find(0x0006), nullptr);
    EXPECT_NE(a.lst.find(0x0009), nullptr);
    EXPECT_NE(a.lst.find(0x000C), nullptr);
    ASSERT_NE(a.lst.find(0x000E), nullptr);
    EXPECT_NE(a.lst.find(0x000E)->find("NOP"), std::string::npos);
}

TEST(MacListing, EncodesRepresentativeInstructions){
    auto a = run(
        "\tLD\tA,05H\n"          // 3E 05
        "\tLD\tBC,1234H\n"       // 01 34 12
        "\tJP\t0ABCDH\n"         // C3 CD AB
        "\tRST\t38H\n"           // FF
        "\tBIT\t7,A\n"           // CB 7F
        "\tIM\t2\n"              // ED 5E
        "\tIN\tA,(12H)\n"        // DB 12
        "\tOUT\t(04H),A\n"       // D3 04
        "\tLD\t(IY+10),B\n"      // FD 70 0A
        "\tBIT\t6,(IY+11)\n"     // FD CB 0B 76
        "\tEX\tAF,AF'\n"         // 08
        "\tEXX\n"                // D9
        "\tLDIR\n"               // ED B0
        "\tADD\tA,07H\n"         // C6 07
        "\tAND\t0FH\n"           // E6 0F
        "\tSUB\tB\n"             // 90
        "\tEX\t(SP),HL\n"        // E3
        , 0, false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.res.unknown, 0);
    const std::vector<uint8_t> want = {
        0x3E,0x05, 0x01,0x34,0x12, 0xC3,0xCD,0xAB, 0xFF, 0xCB,0x7F, 0xED,0x5E,
        0xDB,0x12, 0xD3,0x04, 0xFD,0x70,0x0A, 0xFD,0xCB,0x0B,0x76, 0x08, 0xD9,
        0xED,0xB0, 0xC6,0x07, 0xE6,0x0F, 0x90, 0xE3 };
    EXPECT_EQ(bytesAt(a.img, 0x0000, (int)want.size()), want);
}

TEST(MacListing, EncodesRelativeJumpsRelativeToTheirOwnAddress){
    auto a = run(
        "START:\tNOP\n"
        "\tJR\tSTART\n"          // 18 FD  (zurück über 1 NOP + 2 Bytes JR)
        "\tDJNZ\tSTART\n"        // 10 FB
        , 0, false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(bytesAt(a.img, 0x0001, 4), (std::vector<uint8_t>{0x18,0xFD,0x10,0xFB}));
}

// ─── Direktiven ──────────────────────────────────────────────────────────────
TEST(MacListing, HandlesOrgEquDsDbDw){
    auto a = run(
        "SIZE\tEQU\t4\n"
        "\tORG\t0100H\n"
        "\tDB\t1,2,'AB'\n"       // 4 Bytes @0100
        "\tDW\t1234H\n"          // 2 Bytes @0104
        "\tDS\tSIZE\n"           // 4 Bytes Lücke
        "\tNOP\n"                // @010A
        , 0, false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(bytesAt(a.img, 0x0100, 6), (std::vector<uint8_t>{1,2,'A','B',0x34,0x12}));
    ASSERT_NE(a.lst.find(0x010A), nullptr);
    EXPECT_NE(a.lst.find(0x010A)->find("NOP"), std::string::npos);
}

TEST(MacListing, StringsKeepSemicolonsAndCommas){
    auto a = run("\tDB\t'A;B,C'\t;Kommentar\n", 0, false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(bytesAt(a.img, 0x0000, 5), (std::vector<uint8_t>{'A',';','B',',','C'}));
}

TEST(MacListing, AddressOffsetRelocatesTheTable){
    auto a = run("\tNOP\n\tNOP\n", 0x0700, false);
    ASSERT_TRUE(a.ok);
    EXPECT_NE(a.lst.find(0x0700), nullptr);
    EXPECT_NE(a.lst.find(0x0701), nullptr);
    EXPECT_EQ(a.lst.find(0x0000), nullptr);
}

// ─── Mxxxx-Adressanker ───────────────────────────────────────────────────────
TEST(MacListing, MxxxxLabelsActAsAddressAnchors){
    // Der Anker zieht den Adresszähler auf die im Labelnamen codierte Adresse —
    // ein einzelner nicht erkannter Befehl verschiebt damit nicht alles Folgende.
    auto a = run(
        "\tNOP\n"
        "M0100:\tNOP\n"
        "\tNOP\n", 0, /*anchors=*/true);
    ASSERT_TRUE(a.ok);
    EXPECT_GE(a.res.anchors, 1);
    EXPECT_GE(a.res.resyncs, 1);
    EXPECT_NE(a.lst.find(0x0100), nullptr);
    EXPECT_NE(a.lst.find(0x0101), nullptr);
}

TEST(MacListing, AnchorsCanBeDisabled){
    auto a = run("\tNOP\nM0100:\tNOP\n", 0, /*anchors=*/false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.res.resyncs, 0);
    EXPECT_NE(a.lst.find(0x0001), nullptr);
    EXPECT_EQ(a.lst.find(0x0100), nullptr);
}

TEST(MacListing, ReportsMissingFile){
    prnlst::Listing l; maclst::Result r;
    EXPECT_FALSE(maclst::assemble("/nonexistent/nope.mac", 0, l, r));
    EXPECT_FALSE(r.error.empty());
}

// ─── Versatz-Abgleich (@auto) ────────────────────────────────────────────────
TEST(MacListing, FindsLoadOffsetInMemory){
    auto a = run(
        "\tLD\tA,05H\n\tADD\tA,07H\n\tAND\t0FH\n\tOUT\t(04H),A\n"
        "\tLD\tA,0AAH\n\tIN\tA,(12H)\n\tXOR\t55H\n\tNOP\n\tNOP\n", 0, false);
    ASSERT_TRUE(a.ok);
    // Speicher: dasselbe Assemblat, aber nach 0x0700 verschoben.
    std::vector<uint8_t> mem(0x10000, 0x00);
    for (auto& kv : a.img.byte) mem[(uint16_t)(kv.first + 0x0700)] = kv.second;
    auto m = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 8);
    EXPECT_TRUE(m.found);
    EXPECT_EQ(m.offset, 0x0700);
    EXPECT_EQ(m.matched, maclst::fixedByteCount(a.img));
}

TEST(MacListing, ReportsDivergenceInOtherwiseMatchingBuild){
    auto a = run(
        "\tLD\tA,05H\n\tADD\tA,07H\n\tAND\t0FH\n\tOUT\t(04H),A\n"
        "\tLD\tA,0AAH\n\tIN\tA,(12H)\n\tXOR\t55H\n\tNOP\n\tNOP\n", 0, false);
    ASSERT_TRUE(a.ok);
    std::vector<uint8_t> mem(0x10000, 0x00);
    for (auto& kv : a.img.byte) mem[(uint16_t)(kv.first + 0x0100)] = kv.second;
    mem[0x0100 + 0x000D] ^= 0xFF;                 // ein Byte weicht ab (anderer Build)
    auto m = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 8);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.offset, 0x0100);
    EXPECT_EQ(m.matched, maclst::fixedByteCount(a.img) - 1);
}

TEST(MacListing, NoOffsetWhenCodeIsAbsent){
    auto a = run("\tLD\tA,05H\n\tADD\tA,07H\n\tAND\t0FH\n\tXOR\t55H\n", 0, false);
    ASSERT_TRUE(a.ok);
    std::vector<uint8_t> mem(0x10000, 0xFF);
    auto m = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 6);
    EXPECT_FALSE(m.found);
}

TEST(MacListing, DetectsSourceFileSuffix){
    EXPECT_TRUE (maclst::isSourceFile("DEBUBC43.MAC"));
    EXPECT_TRUE (maclst::isSourceFile("foo.asm"));
    EXPECT_FALSE(maclst::isSourceFile("bios.prn"));
}

TEST(MacListing, VerdictReflectsMatchQuality){
    // Nur teilweise passende Quelle (anderer Build) muss als solche gemeldet werden —
    // „passt die Quelle überhaupt zu diesem Image?" ist die eigentliche Frage.
    auto a = run(
        "\tLD\tA,05H\n\tADD\tA,07H\n\tAND\t0FH\n\tOUT\t(04H),A\n"
        "\tLD\tA,0AAH\n\tIN\tA,(12H)\n\tXOR\t55H\n\tNOP\n\tNOP\n"
        "\tLD\tB,09H\n\tDEC\tB\n\tCP\t0C3H\n\tSCF\n\tCCF\n\tHALT\n", 0, false);
    ASSERT_TRUE(a.ok);
    std::vector<uint8_t> mem(0x10000, 0x00);
    for (auto& kv : a.img.byte) mem[(uint16_t)(kv.first + 0x0200)] = kv.second;
    auto exact = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 8);
    ASSERT_TRUE(exact.found);
    EXPECT_EQ(exact.offset, 0x0200);
    EXPECT_DOUBLE_EQ(exact.ratio, 1.0);
    EXPECT_STREQ(exact.verdict(), "identischer Build");

    // Hälfte des Codes verändern → gleiches Programm, anderer Build.
    for (uint16_t x = 0x0210; x < 0x0220; ++x) mem[x] ^= 0xFF;
    auto partial = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 8);
    ASSERT_TRUE(partial.found);
    EXPECT_EQ(partial.offset, 0x0200);
    EXPECT_LT(partial.ratio, 1.0);
    EXPECT_GT(partial.ratio, 0.25);
}

TEST(MacListing, PrefersGloballyBestOffsetOverFirstAnchorHit){
    // Ein Ankerfenster kann auch an einer Zufallsstelle passen. Erst die
    // Gesamtbewertung findet den echten Ladeversatz.
    auto a = run(
        "\tLD\tA,05H\n\tADD\tA,07H\n\tAND\t0FH\n\tOUT\t(04H),A\n"
        "\tLD\tA,0AAH\n\tIN\tA,(12H)\n\tXOR\t55H\n\tNOP\n\tNOP\n"
        "\tLD\tB,09H\n\tDEC\tB\n\tCP\t0C3H\n\tSCF\n\tCCF\n\tHALT\n", 0, false);
    ASSERT_TRUE(a.ok);
    std::vector<uint8_t> mem(0x10000, 0x00);
    for (auto& kv : a.img.byte) mem[(uint16_t)(kv.first + 0x4000)] = kv.second;   // vollständig
    // Teilkopie an anderer Stelle (nur die ersten 10 Bytes) als Ablenkung.
    for (auto& kv : a.img.byte) if (kv.first < 10) mem[(uint16_t)(kv.first + 0x0800)] = kv.second;
    auto m = maclst::findOffset(a.img, [&](uint16_t x){ return mem[x]; }, 8);
    ASSERT_TRUE(m.found);
    EXPECT_EQ(m.offset, 0x4000);
}

// ─── Vollständiger Rundlauf gegen den Disassembler ───────────────────────────
// Der Encoder wird aus tools/z80dis_min.h abgeleitet; diese Invariante prüft die
// von Hand geschriebene Hälfte (Schlüssel-Normalisierung, Operanden-Klassifikation,
// Byte-Patching) über den GANZEN Befehlssatz statt an Stichproben:
//   Disassemblat einer Kodierung → als Quellzeile assemblieren → wieder
//   disassemblieren MUSS denselben Text ergeben.
// (Redundante DD/FD-Präfixe kanonisiert der Assembler bewusst weg — deshalb wird
// der TEXT verglichen, nicht die Ausgangsbytes.)
TEST(MacListing, RoundTripsEveryDecodableInstruction){
    auto disasmOf = [](const std::vector<uint8_t>& b){
        auto i = z80dis::decode([&](uint16_t a)->uint8_t{ return a<b.size()? b[a] : 0; }, 0);
        return std::string(i.text);
    };
    int tested = 0;
    std::vector<std::string> failures;
    auto check = [&](std::vector<uint8_t> tpl){
        auto ins = z80dis::decode([&](uint16_t a)->uint8_t{ return a<tpl.size()? tpl[a] : 0; }, 0);
        std::string text = ins.text;
        if (text.rfind("NOP*",0)==0) return;              // undefinierter ED-Opcode
        if (ins.len > (int)tpl.size()) return;
        auto a = run("\t" + text + "\n", 0, false);
        ASSERT_TRUE(a.ok);
        std::vector<uint8_t> got;
        for (uint16_t i=0;;++i){ auto it=a.img.byte.find(i); if(it==a.img.byte.end()) break;
                                 got.push_back(it->second); }
        ++tested;
        std::string back = got.empty()? std::string("<nichts>") : disasmOf(got);
        if (a.res.unknown || got.empty() || back != text)
            failures.push_back(text + " → \"" + back + "\"");
    };
    for (int op=0; op<256; ++op){
        if (op==0xCB||op==0xED||op==0xDD||op==0xFD) continue;
        check({(uint8_t)op,0x05,0x06,0x07});
    }
    for (int op=0; op<256; ++op) check({0xCB,(uint8_t)op});
    for (int op=0; op<256; ++op) check({0xED,(uint8_t)op,0x05,0x06});
    for (uint8_t pfx : {0xDDu,0xFDu}){
        for (int op=0; op<256; ++op){ if(op==0xCB) continue; check({pfx,(uint8_t)op,0x05,0x06}); }
        for (int op=0; op<256; ++op) if((op&7)==6) check({pfx,0xCB,0x05,(uint8_t)op});
    }
    EXPECT_GT(tested, 1000);                              // der Befehlssatz ist abgedeckt
    std::string msg;
    for (size_t i=0;i<failures.size() && i<20;++i) msg += "\n  " + failures[i];
    EXPECT_TRUE(failures.empty()) << failures.size() << " Rundlauf-Fehler:" << msg;
}

// `SET` ist doppelt belegt: Pseudobefehl `NAME SET wert` vs. Z80-Befehl `SET b,r`.
// Ohne Label ist der Z80-Befehl gemeint — sonst verschwindet er spurlos aus dem
// Assemblat (gefunden vom Rundlauf-Test oben; in DEBUBC43.MAC verschob das die
// Adressen um 7 Stellen, bis ein Mxxxx-Anker sie zurückzog).
TEST(MacListing, SetIsInstructionWithoutLabelAndPseudoOpWithOne){
    auto a = run("\tSET\t0,B\n"          // CB C0
                 "\tRES\t7,A\n"          // CB BF
                 "\tSET\t3,(IY+2)\n"     // FD CB 02 DE
                 , 0, false);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.res.unknown, 0);
    EXPECT_EQ(bytesAt(a.img, 0x0000, 8),
              (std::vector<uint8_t>{0xCB,0xC0, 0xCB,0xBF, 0xFD,0xCB,0x02,0xDE}));

    // MIT Label bleibt es der wertzuweisende Pseudobefehl (kein Objektcode).
    auto b = run("ZAEHLER\tSET\t7\n\tLD\tA,ZAEHLER\n", 0, false);
    ASSERT_TRUE(b.ok);
    EXPECT_EQ(bytesAt(b.img, 0x0000, 2), (std::vector<uint8_t>{0x3E,0x07}));
}
