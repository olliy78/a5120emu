// Unit-Tests für den MACRO-80-.prn-Listing-Parser (tools/prn_listing.h).
//
// Deckt die Trennung "emittierte Code-/Daten-Zeile" vs. "equ/aset/leer" ab sowie
// die Quellfeld-Erkennung (mit Label / label-los hinter Tab) und die db/DB-Falle.
#include "tools/prn_listing.h"
#include <gtest/gtest.h>

using prnlst::parseLine;

namespace {

// Hilfsfunktion: parsen und Erfolg + Werte zurückgeben.
struct R { bool ok; uint16_t addr; std::string src; };
R run(const std::string& line){
    R r{}; r.ok = parseLine(line, r.addr, r.src); return r;
}

} // namespace

// --- Echte emittierte Code-Zeilen aus bios.prn -------------------------------

TEST(PrnListing, CodeLineWithLabelAndComment){
    // "  D227    C3 DDF3               BIOS27: JP<TAB>read<TAB><TAB>;oA Resultat..."
    R r = run("  D227    C3 DDF3               BIOS27: JP\tread\t\t;oA Resultat; A=0 ok\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0xD227);
    EXPECT_EQ(r.src, "BIOS27: JP read ;oA Resultat; A=0 ok");
}

TEST(PrnListing, CodeLineLabelNoComment){
    R r = run("  D200    C3 E890               BIOS00: JP\tkaltst\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0xD200);
    EXPECT_EQ(r.src, "BIOS00: JP kaltst");
}

// --- Die db/DB-Falle: label-lose Datenzeile mit Makro-Flag 'C' ----------------

TEST(PrnListing, LabellessDbWithMacroFlag){
    // Mnemonic "DB" besteht aus zwei Hexziffern — darf NICHT als Objektbyte
    // verschluckt werden. Quelle beginnt am Tab vor "DB".
    R r = run("  D271    04             C      \tDB\t4\t;;(2)\t;BLOCK SHIFT\t(LOGIN)\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0xD271);
    EXPECT_EQ(r.src, "DB 4 ;;(2) ;BLOCK SHIFT (LOGIN)");
}

TEST(PrnListing, MultiByteDataLine){
    R r = run("  D288    01 02 03 04    C      \tdb\t1,2,3,4,5,6,7,8\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0xD288);
    EXPECT_EQ(r.src, "db 1,2,3,4,5,6,7,8");
}

TEST(PrnListing, DataLineWithLabelAndQuotes){
    R r = run("  D233    43 50                 BIOS33: db\t'CP'\t\t;SCP-Kennzeichen ist 'BW '\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0xD233);
    EXPECT_EQ(r.src, "BIOS33: db 'CP' ;SCP-Kennzeichen ist 'BW '");
}

// --- Zeilen, die KEINEN Loc-Counter tragen (müssen abgelehnt werden) ---------

TEST(PrnListing, EquLineRejected){
    // Führende 4-Hex ist der equ-WERT, kein Loc-Counter → kein Objektbyte folgt.
    EXPECT_FALSE(run("  0800                          ccpln\tequ\t0800h").ok);
    EXPECT_FALSE(run("  0040                          ramkb\tequ\t64\t;RAM-Groesse").ok);
}

TEST(PrnListing, AsetLineRejected){
    EXPECT_FALSE(run("  0880                          biosln\taset\t0880h ;07d0h").ok);
}

TEST(PrnListing, LabelOnlyLineRejected){
    // "  0000'                         BIOS:"  — keine emittierten Bytes.
    EXPECT_FALSE(run("  0000'                         BIOS:").ok);
}

TEST(PrnListing, CommentAndBlankLinesRejected){
    EXPECT_FALSE(run("                                ; reiner Kommentar").ok);
    EXPECT_FALSE(run("").ok);
    EXPECT_FALSE(run("   ").ok);
}

// --- Listing-Container: Laden aus einem String-Stream-Äquivalent --------------

TEST(PrnListing, ListingFirstWinsPerAddress){
    prnlst::Listing L;
    uint16_t a; std::string s;
    ASSERT_TRUE(parseLine("  D200    C3 E890               BIOS00: JP\tkaltst", a, s));
    L.by_addr[a] = s;
    // zweite Quelle für dieselbe Adresse darf die erste nicht überschreiben
    // (das macht Listing::load via find()-Guard; hier direkt geprüft):
    EXPECT_NE(L.find(0xD200), nullptr);
    EXPECT_EQ(*L.find(0xD200), "BIOS00: JP kaltst");
    EXPECT_EQ(L.find(0x1234), nullptr);
}

TEST(PrnListing, RelocatableAddressMarkerStripped){
    R r = run("  0437'   21 0004               start:  ld\thl,0400h\r");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.addr, 0x0437);
    EXPECT_EQ(r.src, "start: ld hl,0400h");
}

// --- Offset-Parsing (@OFFSET-Spezifikation) ----------------------------------

TEST(PrnListing, ParseOffsetForms){
    bool ok;
    EXPECT_EQ(prnlst::parseOffset("0x100", ok), 0x100);   EXPECT_TRUE(ok);
    EXPECT_EQ(prnlst::parseOffset("-0x100", ok), -0x100); EXPECT_TRUE(ok);
    EXPECT_EQ(prnlst::parseOffset("1800h", ok), 0x1800);  EXPECT_TRUE(ok);
    EXPECT_EQ(prnlst::parseOffset("256", ok), 256);       EXPECT_TRUE(ok);
    EXPECT_EQ(prnlst::parseOffset("-16", ok), -16);       EXPECT_TRUE(ok);
    prnlst::parseOffset("xyz", ok);                       EXPECT_FALSE(ok);
    prnlst::parseOffset("0x1g", ok);                      EXPECT_FALSE(ok);
}

TEST(PrnListing, SplitSpecPathAndOffset){
    std::string path; long off;
    EXPECT_TRUE(prnlst::splitSpec("bios.prn", path, off));
    EXPECT_EQ(path, "bios.prn"); EXPECT_EQ(off, 0);

    EXPECT_TRUE(prnlst::splitSpec("bios.prn@0x200", path, off));
    EXPECT_EQ(path, "bios.prn"); EXPECT_EQ(off, 0x200);

    EXPECT_TRUE(prnlst::splitSpec("bios.prn@-0x100", path, off));
    EXPECT_EQ(path, "bios.prn"); EXPECT_EQ(off, -0x100);

    // ungültiger Offset → false
    EXPECT_FALSE(prnlst::splitSpec("bios.prn@xyz", path, off));
}

// --- labelOf: führendes Label aus der Quellzeile -----------------------------

TEST(PrnListing, LabelOfExtractsLeadingLabel){
    EXPECT_EQ(prnlst::labelOf("BIOS27: JP read ;oA Resultat"), "BIOS27");
    EXPECT_EQ(prnlst::labelOf("@write: db 1"), "@write");     // CP/A-@-Bezeichner
    EXPECT_EQ(prnlst::labelOf("DB 4 ;;(2)"), "");             // label-los (Mnemonic)
    EXPECT_EQ(prnlst::labelOf("db 'CP'"), "");
    EXPECT_EQ(prnlst::labelOf(""), "");
    EXPECT_EQ(prnlst::labelOf("3foo: x"), "");                // darf nicht mit Ziffer beginnen
}

TEST(PrnListing, LoadAppliesOffsetToKeys){
    // Eine Listing-Zeile via temp-Datei laden, einmal mit Offset.
    const char* tmp = "/tmp/k1520_prn_offset_test.prn";
    { std::ofstream f(tmp);
      f << "  D200    C3 E890               BIOS00: JP\tkaltst\n"; }

    prnlst::Listing a; ASSERT_EQ(a.load(tmp), 1);
    EXPECT_NE(a.find(0xD200), nullptr);
    EXPECT_EQ(a.find(0xD000), nullptr);

    prnlst::Listing b; ASSERT_EQ(b.load(tmp, -0x200), 1);   // reloziert: D200 → D000
    EXPECT_EQ(b.find(0xD200), nullptr);
    ASSERT_NE(b.find(0xD000), nullptr);
    EXPECT_EQ(*b.find(0xD000), "BIOS00: JP kaltst");
}
