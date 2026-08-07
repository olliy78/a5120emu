/**
 * @file test_hardy.cpp
 * @brief Langsamer End-to-End-Regressionswächter: das HARDY-Hardware-Testprogramm
 *        (HARDY V.3/1, Humboldt-Universität) läuft im „Rechner"-Test OHNE
 *        Einfrieren durch und meldet KEINE Fehler.
 *
 * Eigenes Executable, per ctest-Label „format_integration" aus dem Default-
 * `tools/dev.sh test` ausgeschlossen (Boot bis A> + kompletter Rechner-Test →
 * einige Sekunden). Läuft mit `tools/dev.sh test-format` / `test-all`.
 *
 * Guard für die /MEMDI- + BS-PIO-Port-A-Bus-Strobe-Fixes (doc/analyse_hardy_memdi.md):
 *  - Das globale /MEMDI darf den Speicher NICHT gaten. HARDYs MEMDI-Test setzt
 *    /MEMDI (BS-PIO Port A Bit7) und führt direkt danach EI/RET sowie Stack-/
 *    BDOS-Operationen aus. Ein Read-Gate ließe die CPU 0xFF (=RST 38H) holen →
 *    Endlos-RST-38-Schleife (das ursprünglich gemeldete Einfrieren); ein
 *    Write-Gate blockierte die Stack-Writes, sobald HARDYs RDY-Test /MEMDI gesetzt
 *    zurücklässt → Hänger im BDOS-Print.
 *  - BS-PIO Port A muss die dynamischen Bus-Strobes /M1 (A0), /WR (A5), /RDY (A6)
 *    als aktiv präsentieren, sonst meldet der Rechner-Test „System - PIO kein
 *    INTERRUPT !" und eine leere „RDY bei:"-Zeile.
 *
 * Sauberes Rechner-Test-Ergebnis (verifiziert): „System - PIO   ok" und
 * „MEMDI1/2 aktiv:  RDY  bei:   00-FF" (RDY über den vollen Adressraum), KEIN
 * „kein INTERRUPT".
 */

#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"

#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

#include <cstdint>
#include <string>

using k1520test::diskPath;
using k1520test::pressKeyUntil;
using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::runSmallUntil;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

/**
 * @test Hardy/RechnerTestRunsCleanWithoutFreezing
 * @brief HARDY bootet, der „Rechner"-Test läuft komplett durch (kein RST-38-Freeze,
 *        kein Hänger) und meldet „System - PIO ok" + „RDY bei: 00-FF" statt Fehlern.
 *
 * Die 5×1024-Bootdiskette wird schreibgeschützt (wp) gemountet — der Rechner-Test
 * fasst nur RAM/Ports an, nicht die Diskette, sodass die committete Fixture
 * unangetastet bleibt.
 */
TEST(Hardy, RechnerTestRunsCleanWithoutFreezing) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("scpx17_5x1024_k5601_hardy.hfe"), "cpa780", /*wp=*/true))
        << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 250'000'000))
        << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 20'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);   // in die CONIN-Leseschleife einschwingen

    // HARDY starten → Copyright-Banner (wartet auf Leertaste).
    typeString(machine, "HARDY");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "R. Hartung", 30'000'000))
        << "HARDY startete nicht (kein Banner):\n" << vramText(machine);

    // Leertaste 1 → „Achtung / Dokumentation beachten"-Hinweis.
    ASSERT_TRUE(pressKeyUntil(machine, ' ', "Dokumentation", 6))
        << "Erster Dialog nicht bestätigt:\n" << vramText(machine);

    // Leertaste 2 → Testauswahl-Menü.
    ASSERT_TRUE(pressKeyUntil(machine, ' ', "1 - Rechner", 6))
        << "Testmenü nicht erreicht:\n" << vramText(machine);

    // Test 1 (Rechner) wählen → läuft die gesamte Rechner-Diagnose durch.
    ASSERT_TRUE(pressKeyUntil(machine, '1', "Rechner - Testprogramm", 6))
        << "Rechner-Test nicht gestartet:\n" << vramText(machine);
    // Auf die Gruppen-Tabelle warten — sie wird erst NACH System-PIO und beiden
    // MEMDI-RDY-Zeilen gedruckt, also ist der ganze Rechner-Test dann durch.
    ASSERT_TRUE(runSmallUntil(machine, "0000-3FFF", 60'000'000))
        << "Rechner-Test lief nicht durch (Freeze bei MEMDI/RST-38?):\n" << vramText(machine);

    const std::string screen = vramText(machine);
    // System-PIO-Selbsttest über /M1 → „ok", NICHT „kein INTERRUPT".
    EXPECT_NE(screen.find("System - PIO   ok"), std::string::npos)
        << "System-PIO nicht ok:\n" << screen;
    EXPECT_EQ(screen.find("kein INTERRUPT"), std::string::npos)
        << "System-PIO meldet „kein INTERRUPT\":\n" << screen;
    // MEMDI-RDY-Test über /WR+/RDY → RDY über den vollen Adressraum „00-FF".
    EXPECT_NE(screen.find("00-FF"), std::string::npos)
        << "RDY-Messung leer (kein „00-FF\"):\n" << screen;
}
