// Unit-Tests für den MACRO-80-.prn-Listing-Parser (tools/prn_listing.h).
//
// Deckt die Trennung "emittierte Code-/Daten-Zeile" vs. "equ/aset/leer" ab sowie
// die Quellfeld-Erkennung (mit Label / label-los hinter Tab) und die db/DB-Falle.
#include "tools/prn_listing.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <string>
#include "tests/support/temp_path.h"

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
    // Temp-Verzeichnis des Systems: "/tmp" gibt es unter Windows nicht (dort
    // laege es als C:\tmp\… und der ofstream scheiterte lautlos).
    const std::string tmp =
        k1520test::tempPath("k1520_prn_offset_test.prn");
    { std::ofstream f(tmp);
      f << "  D200    C3 E890               BIOS00: JP\tkaltst\n"; }

    prnlst::Listing a; ASSERT_EQ(a.load(tmp), 1);
    EXPECT_NE(a.find(0xD200), nullptr);
    EXPECT_EQ(a.find(0xD000), nullptr);

    prnlst::Listing b; ASSERT_EQ(b.load(tmp, -0x200), 1);   // reloziert: D200 → D000
    EXPECT_EQ(b.find(0xD200), nullptr);
    ASSERT_NE(b.find(0xD000), nullptr);
    EXPECT_EQ(*b.find(0xD000), "BIOS00: JP kaltst");

    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

// --- Objektbytes (für den Versatz-Abgleich `@auto`) --------------------------

TEST(PrnListing, ExtractsObjectBytes){
    uint16_t a; std::string src; std::vector<uint8_t> b;
    // MACRO-80 druckt 16-Bit-Operanden als EIN Wort: "C3 E860" = C3 60 E8.
    ASSERT_TRUE(parseLine("  D100    C3 E860               BIOS00: JP\tkaltst", a, src, &b));
    EXPECT_EQ(a, 0xD100);
    EXPECT_EQ(b, (std::vector<uint8_t>{0xC3,0x60,0xE8}));
}

TEST(PrnListing, ExtractsObjectBytesOnePerColumn){
    uint16_t a; std::string src; std::vector<uint8_t> b;
    // Selbst erzeugte Listings (zre.prn) drucken jedes Byte einzeln.
    ASSERT_TRUE(parseLine("0001  01 00 08      \tLD BC,0800h\t\t;[ZVE1] Zaehler", a, src, &b));
    EXPECT_EQ(a, 0x0001);
    EXPECT_EQ(b, (std::vector<uint8_t>{0x01,0x00,0x08}));
}

TEST(PrnListing, LoadCollectsBytesUnderRuntimeAddresses){
    const std::string p =
        k1520test::tempPath("k1520_prn_bytes_test.prn");
    { std::ofstream f(p); f << "  0100    C3 0605               START: JP\tX\n"; }
    prnlst::Listing l;
    ASSERT_EQ(l.load(p, 0x0400, /*want_bytes=*/true), 1);
    ASSERT_EQ(l.bytes_by_addr.size(), 3u);
    EXPECT_EQ(l.bytes_by_addr[0x0500], 0xC3);
    EXPECT_EQ(l.bytes_by_addr[0x0501], 0x05);
    EXPECT_EQ(l.bytes_by_addr[0x0502], 0x06);
    std::filesystem::remove(p);
}

// --- Quellbereich, Byteabgleich, nächstes Label (§8a AP-E4d, K8915) ----------

TEST(PrnListing, SplitSpecRangeKennt_Versatz_Bereich_UndBeides){
    std::string p; long off; int lo, hi;
    ASSERT_TRUE(prnlst::splitSpecRange("rom.prn", p, off, lo, hi));
    EXPECT_EQ(p, "rom.prn"); EXPECT_EQ(off, 0); EXPECT_EQ(lo, -1); EXPECT_EQ(hi, -1);

    ASSERT_TRUE(prnlst::splitSpecRange("rom.prn@0xF000", p, off, lo, hi));
    EXPECT_EQ(off, 0xF000); EXPECT_EQ(lo, -1);

    ASSERT_TRUE(prnlst::splitSpecRange("rom.prn@0xF000:00D0-03FF", p, off, lo, hi));
    EXPECT_EQ(p, "rom.prn"); EXPECT_EQ(off, 0xF000); EXPECT_EQ(lo, 0x00D0); EXPECT_EQ(hi, 0x03FF);

    ASSERT_TRUE(prnlst::splitSpecRange("rom.prn@:0x0400-9FFH", p, off, lo, hi));
    EXPECT_EQ(off, 0); EXPECT_EQ(lo, 0x0400); EXPECT_EQ(hi, 0x09FF);

    EXPECT_FALSE(prnlst::splitSpecRange("rom.prn@", p, off, lo, hi));
    EXPECT_FALSE(prnlst::splitSpecRange("rom.prn@0xF000:03FF-00D0", p, off, lo, hi)) << "BIS < VON";
    EXPECT_FALSE(prnlst::splitSpecRange("rom.prn@0xF000:00D0", p, off, lo, hi)) << "ohne BIS";
    EXPECT_FALSE(prnlst::splitSpecRange("rom.prn@0xF000:zz-10", p, off, lo, hi));
}

TEST(PrnListing, LoadNurQuellbereichUnterVersatz){
    const std::string p = k1520test::tempPath("k1520_prn_range_test.prn");
    { std::ofstream f(p);
      f << "0000  F3            \tDI\t\t;Reset\n"
        << "00D0  3E 01         \tLD A,01H\t;Selbsttest\n"
        << "0400  C3 00 04      \tJP 0400H\t;Lader\n"; }
    prnlst::Listing l;
    ASSERT_EQ(l.load(p, 0xF000, false, 0x00D0, 0x03FF), 1);
    EXPECT_NE(l.find(0xF0D0), nullptr);
    EXPECT_EQ(l.find(0xF000), nullptr) << "0000H liegt außerhalb des Quellbereichs";
    EXPECT_EQ(l.find(0xF400), nullptr);
    std::filesystem::remove(p);
}

TEST(PrnListing, MatchesVergleichtDieObjektbytesDerZeile){
    const std::string p = k1520test::tempPath("k1520_prn_match_test.prn");
    { std::ofstream f(p);
      f << "0100  C3 00 04      \tJP 0400H\n"
        << "0103  00            \tNOP\n"; }
    prnlst::Listing ohne;  ASSERT_EQ(ohne.load(p), 2);
    EXPECT_TRUE(ohne.matches(0x0100, [](uint16_t){ return uint8_t(0xFF); }))
        << "ohne Objektbytes gibt es nichts zu prüfen";

    prnlst::Listing l;     ASSERT_EQ(l.load(p, 0, true), 2);
    uint8_t mem[0x200] = {};
    mem[0x100] = 0xC3; mem[0x101] = 0x00; mem[0x102] = 0x04; mem[0x103] = 0x00;
    auto rd = [&](uint16_t a){ return mem[a & 0x1FF]; };
    EXPECT_TRUE(l.matches(0x0100, rd));
    EXPECT_TRUE(l.matches(0x0103, rd));
    mem[0x102] = 0x05;                                    // anderer Code (RAM statt ROM)
    EXPECT_FALSE(l.matches(0x0100, rd));
    EXPECT_TRUE(l.matches(0x0103, rd)) << "die Nachbarzeile ist nicht betroffen";
    std::filesystem::remove(p);
}

TEST(PrnListing, LabelNearLiefertNameUndAbstand){
    const std::string p = k1520test::tempPath("k1520_prn_label_test.prn");
    { std::ofstream f(p);
      f << "E960  F5            ISR:\tPUSH AF\n"
        << "E961  DB 16         \tIN A,(16H)\n"
        << "E963  77            \tLD (HL),A\n"; }
    prnlst::Listing l; ASSERT_EQ(l.load(p), 3);
    EXPECT_EQ(l.labelNear(0xE960), "ISR");
    EXPECT_EQ(l.labelNear(0xE963), "ISR+3");
    EXPECT_EQ(l.labelNear(0xE95F), "");
    EXPECT_EQ(l.labelNear(0xE963, 2), "") << "außer Reichweite";
    std::filesystem::remove(p);
}

TEST(PrnListing, LabelNearKenntAllein_stehendeLabelzeilen){
    // Die selbst erzeugten K8915-Listings setzen das Label auf eine eigene Zeile.
    const std::string p = k1520test::tempPath("k1520_prn_label2_test.prn");
    { std::ofstream f(p);
      f << "L055E:\n"
        << "055E  DB 12         \tIN A,(12H)\n"
        << "0560  E6 02         \tAND 02H\n"; }
    prnlst::Listing l; ASSERT_EQ(l.load(p), 2);
    EXPECT_EQ(l.labelNear(0x0560), "L055E+2");
    EXPECT_EQ(prnlst::labelOf(*l.find(0x055E)), "") << "by_addr bleibt unverändert";
    std::filesystem::remove(p);
}
