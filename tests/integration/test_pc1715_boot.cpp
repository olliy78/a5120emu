/**
 * @file test_pc1715_boot.cpp
 * @brief PC 1715 Etappe 2 (doc/design/21_pc1715.md AP-2): Floppy-Ansteuerung als K5122 in der
 *        Konfiguration „1715" (`/WAIT`), der echte Urlader S502 lädt Spur 0 Sektor 1 und das
 *        Betriebssystem startet bis zum Prompt — noch ohne Tastatur.
 *
 * Disketten nur über `TempDisk` (der Emulator öffnet sie schreibend).
 */

#include <gtest/gtest.h>

#include <string>

#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

std::string zeile(Pc1715Machine& m, int r) {
    std::string s;
    for (int c = 0; c < m.zre().textCols(); ++c) s += char(m.screenChar(c, r) & 0x7F);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string bild(Pc1715Machine& m) {
    std::string s;
    for (int r = 0; r < m.zre().maxZeilen(); ++r) s += zeile(m, r) + "\n";
    return s;
}

/// Läuft, bis eine Bildzeile mit @p prompt beginnt (Cursorzeile), höchstens @p grenze Takte.
bool laufeBisPrompt(Pc1715Machine& m, const std::string& prompt, long long grenze) {
    long long t = 0;
    while (t < grenze) {
        t += m.run(100'000);
        for (int r = 0; r < m.zre().maxZeilen(); ++r)
            if (zeile(m, r) == prompt) return true;
    }
    return false;
}

struct Gebootet {
    Pc1715Machine m;
    k1520test::TempDisk disk;
    explicit Gebootet(const char* fixture) : disk(fixture) {
        stumm();
        EXPECT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
        m.powerOn();
    }
};
}  // namespace

/// SCP 1715 V0006 (cpa800, 5 × 1024): Urlader → Systemspuren → `A>`.  Zugleich die
/// Bildprüfung: Banner in Zeile 1/2 im Zeichensatz S619, keine verschobenen Zeilen.
TEST(Pc1715Boot, Scp1715BisPrompt) {
    Gebootet g("pc1715_scp1715_v0006_boot.hfe");
    ASSERT_TRUE(laufeBisPrompt(g.m, "A>", 40'000'000)) << bild(g.m);
    EXPECT_EQ(zeile(g.m, 0), "");
    EXPECT_EQ(zeile(g.m, 1), "ROBOTRON 1715");
    EXPECT_EQ(zeile(g.m, 2), "SCP   VERS. 0006   -   03/08/87   -   48 KB");
    EXPECT_EQ(zeile(g.m, 4), "A>");
    for (int r = 5; r < 24; ++r) EXPECT_EQ(zeile(g.m, r), "") << "Zeile " << r << "\n" << bild(g.m);
    // Bildspeicher F800H wie beim CP/A (34H = 3EH), Zeichengenerator 1.
    EXPECT_EQ(g.m.zre().bildBasis(), 0xF800);
    EXPECT_FALSE(g.m.zre().bwsZg2());
    // Gerastert: im Framebuffer steht in der Bannerzeile Licht, in Zeile 10 nichts.
    const uint8_t* fb = g.m.framebuffer();
    const int w = g.m.fbWidth();
    auto hell = [&](int row) {
        int n = 0;
        for (int y = row * 12; y < row * 12 + 12; ++y)
            for (int x = 0; x < w; ++x) n += fb[y * w + x] != 0;
        return n;
    };
    EXPECT_GT(hell(1), 100);
    EXPECT_EQ(hell(10), 0);
}

/// SCP 1715 V0007 mit nachladbarem CCP, cpa640 (16 × 256).
TEST(Pc1715Boot, Scp1715V0007Cpa640BisPrompt) {
    Gebootet g("pc1715_scp1715_v0007_cpa640_boot.hfe");
    ASSERT_TRUE(laufeBisPrompt(g.m, "A>", 40'000'000)) << bild(g.m);
    EXPECT_EQ(zeile(g.m, 2), "SCP   VERS. 0007   -   01/11/88   -   50 KB");
    EXPECT_EQ(zeile(g.m, 3), "Betriebssystemversion mit nachladbarem CCP (TPA: 50 kByte)");
}

/// CP/A 1715 (Bootdiskette ohne Systemspuren, Bootkopf im Verzeichnis): bootet ohne
/// Uhrzeitfrage bis `A>`; der 8275 fährt 25 Zeilen, Zeile 24 ist die Statuszeile.
TEST(Pc1715Boot, Cpa1715BisPrompt) {
    Gebootet g("pc1715_cpa1715_boot_4lw.hfe");
    ASSERT_TRUE(laufeBisPrompt(g.m, "A>", 40'000'000)) << bild(g.m);
    EXPECT_EQ(zeile(g.m, 0), " CP/A, Version 24.05.88, TPA 100H - 0C205H");
    EXPECT_EQ(zeile(g.m, 1), "- mit BIOS-Monitor");
    EXPECT_EQ(zeile(g.m, 3), "A>");
    EXPECT_EQ(g.m.zre().crt().rows(), 25);
    EXPECT_NE(zeile(g.m, 24).find("**CP/A**"), std::string::npos) << bild(g.m);
}

/// CP/Z 2.2 (cpa640).
TEST(Pc1715Boot, Cpz22BisPrompt) {
    Gebootet g("pc1715_cpz22_boot.hfe");
    ASSERT_TRUE(laufeBisPrompt(g.m, "A>", 40'000'000)) << bild(g.m);
    EXPECT_EQ(zeile(g.m, 0), "52K CP/Z 2.2 ZOAZ MRL");
    EXPECT_EQ(zeile(g.m, 3), "vorhandene Laufwerke :  A,B");
}

/// UDOS 1715 bis `%`.  Hängt am MO-Register: UDOS schreibt `20H := FFH, 21H := 00H` und
/// wartet nach der Laufwerkswahl ohne weiteres 21H auf den Index (DB4–7 = /MO).
TEST(Pc1715Boot, Udos1715BisPrompt) {
    Gebootet g("pc1715_udos1715_system.hfe");
    ASSERT_TRUE(laufeBisPrompt(g.m, "%", 100000000)) << bild(g.m);
    EXPECT_NE(zeile(g.m, 0).find("Betriebssystem UDOS1715"), std::string::npos) << bild(g.m);
}

/// CP/A 1715, gebaut von der CPA_Workbench (Variante `pc_1715`, M80/LINKMT, Diskette `cpa800`
/// mit dem Bootkopf `F003H` aus `prebuilt/pc_1715`).  Wächter für AP-3b: die Workbench nahm für
/// den Diskettenbau früher immer den SYL-Kopf des A5120 — eine so gebaute Diskette bootet am
/// 1715 nicht (der Urlader S502 prüft das Wort F003H).  Zugleich trägt die Diskette `PCTEST.COM`.
TEST(Pc1715Boot, CpaWorkbenchBisPrompt) {
    Gebootet g("pc1715_cpa1715_workbench.hfe");
    // Die Vorgabe der Workbench fragt nach der Uhrzeit (unser BC-Boot-Fixture tut es nicht).
    ASSERT_TRUE(k1520test::pc1715::laufeBisText(g.m, "Uhrzeit in der Form HH:MM", 40'000'000)) << bild(g.m);
    EXPECT_NE(zeile(g.m, 0).find("CP/A, Version"), std::string::npos) << bild(g.m);
    k1520test::pc1715::tippeZeile(g.m, "12:00");
    ASSERT_TRUE(laufeBisPrompt(g.m, "A>", 40'000'000)) << bild(g.m);
    EXPECT_EQ(g.m.zre().crt().rows(), 25);
    EXPECT_NE(zeile(g.m, 24).find("**CP/A**"), std::string::npos) << bild(g.m);
}
