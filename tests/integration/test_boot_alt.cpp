/**
 * @file test_boot_alt.cpp
 * @brief CP/A auf dem A5120 „5¼″ alt“ (doc/design/30_a5120_fruehe_varianten.md, AP A6).
 *
 * Ausfuehrung `Alt`: K7023.01 (64×16, Bildspeicher FC00H), K7636 am U-Bus der K2526, 4× K3525,
 * Wickelbruecke Port 0AH Bit 6 = BAB1.  Das CP/A-BIOS vom 25.09.89 auf den vorhandenen
 * Disketten erkennt beides beim Kaltstart selbst (bioscrt.mac: Port 0AH Bit 6 → BAB1;
 * bioskbdc.mac: Typcode 80H → Tastaturtabelle `km36`) — die Diskette bleibt unveraendert.
 */
#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <string>

#include "core/machines/a5120/a5120.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"

namespace {

using A = A5120Machine::Config::Ausfuehrung;
using k1520test::kSmallBatch;
using k1520test::QK_RETURN;
using k1520test::TempDisk;

/// Bildschirm der K7023 als Text, 16 Zeilen à 64 Zeichen mit '\n' (Bit 7 = Kursor ausgeblendet).
std::string bab1Text(A5120Machine& m) {
    std::string s;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 64; ++c) {
            const uint8_t ch = m.screenChar(c, r) & 0x7F;
            s.push_back((ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ');
        }
        s.push_back('\n');
    }
    return s;
}

/// Alle Zeilen, die je auf dem BAB1-Bild standen — der Kaltstartbericht ist laenger als
/// 16 Zeilen, Banner und Tastaturzeile sind beim Prompt schon hinausgeschoben.
using Gesehen = std::set<std::string>;

void merke(A5120Machine& m, Gesehen* g) {
    if (!g) return;
    std::istringstream in(bab1Text(m));
    for (std::string z; std::getline(in, z);) g->insert(z);
}

bool gesehen(const Gesehen& g, const std::string& s) {
    for (const auto& z : g)
        if (z.find(s) != std::string::npos) return true;
    return false;
}

/// In kleinen Scheiben laufen (Tastaturweg), bis @p needle auf dem BAB1-Bild steht.
bool laufeBis(A5120Machine& m, const std::string& needle, long long max_cycles,
              Gesehen* g = nullptr) {
    for (long long n = 0; n < max_cycles; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) != 0) continue;
        merke(m, g);
        if (bab1Text(m).find(needle) != std::string::npos) return true;
    }
    merke(m, g);
    return bab1Text(m).find(needle) != std::string::npos;
}

/// Bis zum Prompt `A>` (am Zeilenanfang), dann `dir` ueber die K7636 tippen.
void bootUndDir(A5120Machine& m) {
    Gesehen g;
    ASSERT_TRUE(laufeBis(m, "\nA>", 80'000'000, &g)) << "kein A> auf BAB1:\n" << bab1Text(m);
    EXPECT_TRUE(gesehen(g, "CP/A, Version 25.09.89")) << "kein Banner auf BAB1";
    EXPECT_TRUE(gesehen(g, "- Tastatur: K7636")) << "BIOS hat die K7636 nicht erkannt";
    // Nichts darf auf dem K7024-Platz F800H–FBFFH landen (BAB2 waere der falsche Treiber).
    int bab2 = 0;
    for (int ad = 0xF800; ad < 0xFC00; ++ad)
        if (m.memReadDebug(static_cast<uint16_t>(ad)) == 'A' &&
            m.memReadDebug(static_cast<uint16_t>(ad + 1)) == '>') ++bab2;
    EXPECT_EQ(bab2, 0) << "A> im BAB2-Bereich — BIOS hat die Bruecke nicht als BAB1 gelesen";

    k1520test::runCycles(m, 5'000'000);   // Tastaturinitialisierung abschliessen
    k1520test::typeString(m, "dir");
    k1520test::typeKey(m, QK_RETURN);
    ASSERT_TRUE(laufeBis(m, "A>dir", 40'000'000))
        << "Eingabe ueber die K7636 kommt nicht an:\n" << bab1Text(m);
    ASSERT_TRUE(laufeBis(m, "A: @OS      COM", 40'000'000))
        << "dir liefert kein Verzeichnis:\n" << bab1Text(m);
}

}  // namespace

/**
 * @brief Abnahme AP A6: CP/A (Systemdiskette ohne Uhr, BIOS 25.09.89) kommt auf dem alten Geraet
 *        bis `A>`; das Bild steht im Bildspeicher FC00H, `dir` wird ueber die K7636 getippt.
 *
 * Die Laufwerke sind hier K5601 — so wie viele alte Geraete im Feld umgeruestet waren
 * (Entwurf 30 §1); die Ausfuehrungsvorgabe 3× K5600.10 prueft der Fall darunter.
 */
TEST(BootIntegrationAlt, CpaBab1BisPrompt) {
    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    cfg.drive_profiles = {"K5601", "K5601", "K5601", "none"};
    A5120Machine m(cfg);
    m.powerOn();
    TempDisk a("cpa_cpa780_k5601_noclock.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa780", false)) << m.lastError();
    bootUndDir(m);
}

/**
 * @brief Dasselbe auf der Ausfuehrungsvorgabe 3× K5600.10 (40 Spuren, einseitig) von einer
 *        40-Spur-Systemdiskette.  Sie ist mit FORMAT.COM + CPABCGEN aus der Combo-Diskette
 *        erzeugt (tests/fixtures/README.md); ihr `@OS.COM` ist das unveraenderte BIOS 25.09.89
 *        `combo5zoll` (meldet A: als K5601 — die Formaterkennung liest die 40-Spur-Diskette
 *        trotzdem richtig).
 */
TEST(BootIntegrationAlt, CpaAufK5600_10BisPrompt) {
    A5120Machine m(A5120Machine::Config::fuerAusfuehrung(A::Alt));
    m.powerOn();
    TempDisk a("cpa_combo5zoll_noclock_k5600_10_boot.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) << m.lastError();
    bootUndDir(m);
}
