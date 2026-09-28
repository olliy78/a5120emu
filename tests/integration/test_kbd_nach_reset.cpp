/**
 * @file test_kbd_nach_reset.cpp
 * @brief Die erste Eingabe nach der Reset-Taste — was der Gast verwirft und was ein
 *        Test beim Warten auf den Prompt beachten muss.
 *
 * Befund aus AP S1 (2026-09-28): „die erste Eingabe nach reset() geht verloren".
 * KEIN Emulatorfehler; zwei Fallen im Test, und dahinter echtes Gastverhalten:
 *
 *  1. Der K7024 ist am A5120 schreib-only (Lesesperre); vramText() liest das
 *     K3526-Schattenram unter 0xF800, und das behält über reset() den alten Schirm.
 *     Ohne k1520test::wipeVram() ist jedes Warten sofort erfüllt.
 *  2. vramText() umfasst auch den Rest hinter Zeile 24, in den das CP/A-BIOS seine
 *     Statuszeile „A0\A>|0:780 …**CP/A**" schreibt — ~0,15 Mio. Takte BEVOR es die
 *     Tastatur initialisiert und ~7,6 Mio. Takte vor dem Prompt (gemessen nach
 *     reset(), @OS.COM der S1-Diskette: Statuszeile @12,60 M, `OUT 5DH` Kanal-Reset 18H @12,75 M,
 *     `OUT 5CH,00H` Tastatur-Reset @12,75 M, Prompt @20,20 M).
 *  3. Gastverhalten (bioskbdc.mac, Tastaturerkennung): `k37par: db 0,18h`
 *     (SIO-Kanal-Reset — leert den Empfänger), `xor a / out (c),a` (Reset an die
 *     Tastatur), danach `coityp` — ein Zeichen, das kein Typcode ist, wird gelesen
 *     und verworfen.  Ein Anschlag VOR dieser Initialisierung geht damit auch am
 *     echten Gerät verloren; einer danach wird gepuffert (Vorwegtippen geht).
 */
#include <gtest/gtest.h>

#include <string>

#include "core/machines/a5120/a5120.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using namespace k1520test;

namespace {

constexpr int kBootBudget  = 90'000'000;
constexpr int kInputBudget = 40'000'000;
const char*   kFixture     = "cpa_cpa780_k5601_noclock.img";
const char*   kDirTreffer  = "A: PIP      COM";

bool kommando(A5120Machine& m, const std::string& cmd, const std::string& erwartet) {
    typeString(m, cmd);
    typeKey(m, QK_RETURN);
    return runSmallUntil(m, erwartet, kInputBudget);
}

void kaltstartBisZumPrompt(A5120Machine& m, TempDisk& a) {
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa780", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kInputBudget)) << vramLines(m);
}

}  // namespace

/**
 * @test KbdNachReset/SchattenramBehaeltDenAltenSchirm
 * @brief Unmittelbar nach reset() liefert vramText() noch den Schirm des vorigen
 *        Laufs — deshalb wipeVram() vor reset().  Fällt der Test, löscht reset()
 *        das Schattenram, und der Helfer wäre zu überdenken.
 */
TEST(KbdNachReset, SchattenramBehaeltDenAltenSchirm) {
    TempDisk a(kFixture, "k1520_kbdrst_schatten.img");
    A5120Machine m;
    ASSERT_NO_FATAL_FAILURE(kaltstartBisZumPrompt(m, a));

    m.reset();
    EXPECT_NE(vramText(m).find("TPA ist OK!"), std::string::npos)
        << "Schattenram nach reset() leer — wipeVram() nicht mehr nötig?";
    wipeVram(m);
    EXPECT_EQ(vramText(m).find("TPA ist OK!"), std::string::npos);
}

/**
 * @test KbdNachReset/ErsteEingabeNachResetWirdAusgefuehrt
 * @brief Nach Kaltstart UND nach (zweimal) Reset-Taste wird das erste Kommando am
 *        sichtbaren Prompt ausgeführt — ohne vorgeschaltete Leerzeile.
 */
TEST(KbdNachReset, ErsteEingabeNachResetWirdAusgefuehrt) {
    TempDisk a(kFixture, "k1520_kbdrst.img");
    A5120Machine m;
    ASSERT_NO_FATAL_FAILURE(kaltstartBisZumPrompt(m, a));
    ASSERT_TRUE(kommando(m, "DIR PIP.COM", kDirTreffer))
        << "erste Eingabe nach Kaltstart:\n" << vramLines(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));

    for (int runde = 1; runde <= 2; ++runde) {
        SCOPED_TRACE("Reset-Taste Nr. " + std::to_string(runde));
        wipeVram(m);
        m.reset();
        // Nach der Reset-Taste meldet sich CP/A OHNE Banner und RAM-Test — nur `A>`.
        ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
        ASSERT_TRUE(kommando(m, "DIR PIP.COM", kDirTreffer))
            << "erste Eingabe nach reset() nicht ausgeführt:\n" << vramLines(m);
        ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));
    }
}

/**
 * @test KbdNachReset/AnschlagVorDerTastaturinitialisierungVerwirftDerGast
 * @brief Gastverhalten festhalten: wer schon bei der Statuszeile tippt, verliert den
 *        ersten Anschlag an die Tastaturinitialisierung des BIOS; die folgenden
 *        (nach der Initialisierung) puffert CP/A bis zum Prompt.
 */
TEST(KbdNachReset, AnschlagVorDerTastaturinitialisierungVerwirftDerGast) {
    // Das @OS.COM dieser Diskette (bios_org.mac, S1) schreibt die Statuszeile vor der
    // Tastaturinitialisierung — daran lief S1 auf.
    TempDisk a("cpa_cpa780_k5601_noclock-em256.img", "k1520_kbdrst_frueh.img");
    A5120Machine m;
    ASSERT_NO_FATAL_FAILURE(kaltstartBisZumPrompt(m, a));

    wipeVram(m);
    m.reset();
    ASSERT_TRUE(runSmallUntil(m, "A>", kBootBudget)) << vramLines(m);
    ASSERT_EQ(visibleText(m).find("A>"), std::string::npos)
        << "`A>` steht schon sichtbar — die Statuszeile kommt nicht mehr zuerst:\n"
        << vramLines(m);

    typeString(m, "DIR PIP.COM");
    typeKey(m, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(m, "IR?", kInputBudget))
        << "erwartet: 'D' verworfen, Rest gepuffert → CCP meldet `IR?`\n" << vramLines(m);
    EXPECT_NE(visibleText(m).find("A>IR PIP.COM"), std::string::npos) << vramLines(m);
}
