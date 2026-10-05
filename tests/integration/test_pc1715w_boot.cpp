/**
 * @file test_pc1715w_boot.cpp
 * @brief PC 1715W Etappe 6+7 (doc/design/21_pc1715.md AP-W3): Urlader S550 → SCP-3.0-Lader →
 *        `SCP3.SYS` → `A>`; der Zeichensatz kommt aus dem ZG-RAM (vom Lader bzw. MODCS/LOADCS
 *        aus einer `.ZGF`-Datei geladen), das Bild aus dem Bild-RAM der CRT-Karte; Tastatur
 *        (`dir`) und INIT.COM (FORMAT TRACK über U8272 + UA858) auf einer Leerdiskette in B:.
 *
 * Disketten nur über `TempDisk` (der Emulator öffnet sie schreibend).
 */

#include <gtest/gtest.h>

#include <string>

#include "core/cards/pc1715w_speicher/pc1715w_bild.h"
#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"

using namespace k1520test::pc1715;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

Pc1715Machine::Config wConfig() {
    Pc1715Machine::Config c;
    c.variante = Pc1715Machine::Config::Variante::Pc1715W;
    return c;
}

struct Gebootet {
    Pc1715Machine m{wConfig()};
    k1520test::TempDisk disk;
    explicit Gebootet(const char* fixture) : disk(fixture) {
        stumm();
        EXPECT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
        m.powerOn();
    }
};

/// Bild ohne Leerzeilen (SCP 3.0 setzt nach manchen Kommandos eine Leerzeile).
std::string kompakt(Pc1715Machine& m) {
    std::string s;
    for (int r = 0; r < 24; ++r)
        if (const std::string z = zeile(m, r); !z.empty()) s += z + "\n";
    return s;
}

/// Läuft, bis auf die Kommandozeile `A>`@p kommando der neue Prompt folgt.
bool bisPromptNach(Pc1715Machine& m, const std::string& kommando, long long grenze) {
    const std::string nadel = "A>" + kommando + "\nA>\n";
    for (long long t = 0; t < grenze; t += 100'000) {
        laufe(m, 100'000);
        if (kompakt(m).find(nadel) != std::string::npos) return true;
    }
    return false;
}

/// Wartet, bis PROFILE.SUB (`modcs sc619.zgf[1]`) durch ist und der Prompt danach steht.
bool bisZumPrompt(Pc1715Machine& m) { return bisPromptNach(m, "modcs sc619.zgf[1]", 200'000'000); }

/// Prüfsumme eines ZG-RAMs (0 = ZG 1 bei 2000H, 1 = ZG 2 bei 2800H).
unsigned summe(Pc1715Machine& m, int zg) {
    unsigned x = 0;
    for (int i = 0; i < 0x800; ++i) x = x * 31 + m.speicherW()->zgRam(zg, i);
    return x;
}

int belegt(Pc1715Machine& m, int zg) {
    int n = 0;
    for (int i = 0; i < 0x800; ++i) n += m.speicherW()->zgRam(zg, i) != 0;
    return n;
}

/// Stimmt die gerasterte Zelle (@p col, @p row) Pixel für Pixel mit dem ZG-RAM überein?
bool zelleWieZgRam(Pc1715Machine& m, int col, int row) {
    const I8275::Cell& c = m.bildW()->crt().cells(row, col);
    const uint8_t code = c.code & 0x7F;
    const int zg = (m.bildW()->zg2() != c.gpa0) ? 1 : 0;   // 1AH Bit 4 XOR GPA0
    const uint8_t* fb = m.framebuffer();
    const int w = m.fbWidth();
    int an_gesamt = 0;
    for (int l = 0; l < Pc1715wBild::LINIEN; ++l) {
        uint8_t bits = m.speicherW()->zgRam(zg, l * 128 + code);
        if (c.rvv) bits = static_cast<uint8_t>(~bits);      // das Banner steht invers
        for (int x = 0; x < 8; ++x) {
            const bool an = fb[(row * Pc1715wBild::LINIEN + l) * w + col * 8 + x] != 0;
            if (an != (((bits >> (7 - x)) & 1) != 0)) return false;
            an_gesamt += an;
        }
    }
    return an_gesamt > 0 && an_gesamt < 96;   // ein leeres/volles Feld bewiese nichts
}
}  // namespace

/**
 * @test SCP 3.0 (cpa800, 5 × 1024): S550 → Lader (lädt SC619 nach 2000H, startet den 8275) →
 *       SCP3.SYS → PROFILE.SUB → `A>`.  Das Bild kommt aus dem Bild-RAM, gerastert mit dem
 *       ZG-RAM (Banner Pixel für Pixel).
 */
TEST(Pc1715wBoot, Scp30BisPrompt) {
    Gebootet g("pc1715w_scp30_system.hfe");
    ASSERT_TRUE(bisZumPrompt(g.m)) << bild(g.m);
    EXPECT_EQ(zeile(g.m, 0), " PC 1715W");
    EXPECT_EQ(zeile(g.m, 1), " SCP 3.0  (R-BWS)  V0003  -  28/03/89");
    EXPECT_GT(belegt(g.m, 0), 500) << "ZG 1 leer — der Lader hat keinen Zeichensatz geladen";
    ASSERT_EQ(g.m.screenChar(1, 0), 'P');
    EXPECT_TRUE(zelleWieZgRam(g.m, 1, 0));
    EXPECT_TRUE(zelleWieZgRam(g.m, 2, 1));   // „S" von SCP
}

/**
 * @test `.ZGF` laden: MODCS (über LOADCS.RSX, BDOS 113) schreibt einen Zeichensatz der Diskette
 *       in ZG 1 bzw. ZG 2 (Bank 0, 2000H/2800H); das Bild wird danach mit dem neuen Satz gerastert.
 */
TEST(Pc1715wBoot, ZgfDateiLaedtDenZeichensatz) {
    Gebootet g("pc1715w_scp30_system.hfe");
    ASSERT_TRUE(bisZumPrompt(g.m)) << bild(g.m);
    const unsigned zg1 = summe(g.m, 0);
    EXPECT_EQ(belegt(g.m, 1), 0) << "ZG 2 löscht der Lader";
    tippeZeile(g.m, "modcs sc644.zgf[1]");
    ASSERT_TRUE(bisPromptNach(g.m, "modcs sc644.zgf[1]", 30'000'000)) << bild(g.m);
    EXPECT_NE(summe(g.m, 0), zg1) << "SC644 hat ZG 1 nicht verändert";
    tippeZeile(g.m, "modcs sc602.zgf[2]");
    ASSERT_TRUE(bisPromptNach(g.m, "modcs sc602.zgf[2]", 30'000'000)) << bild(g.m);
    EXPECT_GT(belegt(g.m, 1), 500) << "SC602 nicht in ZG 2";
    laufe(g.m, 200'000);   // ein Bild weiter
    EXPECT_TRUE(zelleWieZgRam(g.m, 1, 0));
}

/// Tastatur unter SCP 3.0: `dir` listet die Systemdiskette.
TEST(Pc1715wBoot, TastaturDir) {
    Gebootet g("pc1715w_scp30_system.hfe");
    ASSERT_TRUE(bisZumPrompt(g.m)) << bild(g.m);
    tippeZeile(g.m, "dir");
    ASSERT_TRUE(laufeBisText(g.m, "A: PROFILE  SUB", 30'000'000)) << bild(g.m);
    const std::string b = bild(g.m);
    EXPECT_NE(b.find("SCP3     SYS"), std::string::npos) << b;
    EXPECT_NE(b.find("SC619    ZGF"), std::string::npos) << b;
    EXPECT_NE(b.find("INIT     COM"), std::string::npos) << b;
}

/**
 * @test INIT.COM („FORMATW“, U8272 FORMAT TRACK über die DMA) formatiert eine Leerdiskette in B:
 *       (5 × 1024 × 80 DS), danach `dir b:` = „No File“; PIP schreibt (WRITE DATA + SCAN EQUAL
 *       als Prüflesen) und `dir b:` zeigt die Datei.
 */
TEST(Pc1715wBoot, InitFormatiertLeerdisketteInB) {
    Gebootet g("pc1715w_scp30_system.hfe");
    auto b = k1520test::TempDisk::empty("pc1715w_init_b.hfe");
    ASSERT_TRUE(g.m.createDisk(1, b.path(), "", false)) << g.m.lastError();
    ASSERT_TRUE(bisZumPrompt(g.m)) << bild(g.m);
    tippeZeile(g.m, "init");
    ASSERT_TRUE(laufeBisText(g.m, "PLEASE ENTER DRIVE:", 30'000'000)) << bild(g.m);
    tippe(g.m, "b");   // INIT nimmt Einzeltasten ohne Return
    ASSERT_TRUE(laufeBisText(g.m, "PLEASE SELECT FORMAT:", 10'000'000)) << bild(g.m);
    tippe(g.m, "3");   // DD - DS 5 * 1024 * 80
    ASSERT_TRUE(laufeBisText(g.m, "ALL FILES WILL BE SCRATCHED", 10'000'000)) << bild(g.m);
    tippe(g.m, "y");
    ASSERT_TRUE(laufeBisText(g.m, "FORMATTING COMPLETE", 400'000'000)) << bild(g.m);
    EXPECT_NE(bild(g.m).find("TRACK ( 79 )"), std::string::npos) << bild(g.m);
    ASSERT_TRUE(laufeBisText(g.m, "SELECT FUNCTION:", 10'000'000)) << bild(g.m);
    tippe(g.m, "x");   // INIT beenden: Warmstart lädt CCP.COM nach
    laufe(g.m, 20'000'000);
    tippeZeile(g.m, "dir b:");
    ASSERT_TRUE(laufeBisText(g.m, "No File", 30'000'000)) << bild(g.m);
    tippeZeile(g.m, "pip b:=date.com");
    laufe(g.m, 40'000'000);
    tippeZeile(g.m, "dir b:");
    ASSERT_TRUE(laufeBisText(g.m, "B: DATE     COM", 30'000'000)) << bild(g.m);
    EXPECT_EQ(g.m.detectedFormatName(1), "cpa800");
}
