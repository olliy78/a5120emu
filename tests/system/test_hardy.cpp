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

#include <cstdint>
#include <string>

#ifndef A5120_TEST_DISK_DIR
#define A5120_TEST_DISK_DIR "."
#endif

namespace {

std::string diskPath(const char* name) {
    return std::string(A5120_TEST_DISK_DIR) + "/" + name;
}

// 2 KB Text-VRAM (0xF800–0xFFFF) als druckbares ASCII (nicht-druckbar → ' ').
std::string vramText(A5120Machine& m) {
    std::string s;
    s.reserve(0x800);
    for (int a = 0xF800; a <= 0xFFFF; ++a) {
        uint8_t c = m.memReadDebug(static_cast<uint16_t>(a));
        s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
    }
    return s;
}

// In kleinen Batches laufen (Tastatur-/Timer-ISR-Timing) bis @p needle im VRAM steht.
bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long done = 0; done < max_cycles; done += 5000) {
        m.run(5000);
        if (vramText(m).find(needle) != std::string::npos) return true;
    }
    return false;
}

void runCycles(A5120Machine& m, long long cycles) {
    for (long long done = 0; done < cycles; done += 5000) m.run(5000);
}

constexpr uint32_t QK_RETURN = 0x01000004;  // == K7637::QK_RETURN

// Eine Taste drücken+loslassen mit BIOS-Scan-Zeit (K7637 modelliert 9600 Baud).
void typeKey(A5120Machine& m, uint32_t kc) {
    m.keyPress(kc, false, false);
    runCycles(m, 1'000'000);
    m.keyRelease(kc);
    runCycles(m, 300'000);
}
void typeString(A5120Machine& m, const std::string& s) {
    for (char c : s) typeKey(m, static_cast<uint8_t>(c));
}

// HARDYs Dialoge lesen die Tastatur per DIREKT-Poll (kein BDOS-Puffer): eine Taste
// geht verloren, wenn HARDY im Moment des Anschlags nicht gerade pollt. Daher die
// Taste wiederholt drücken, bis der Folge-Screen (@p needle) erscheint. Da EIN
// akzeptierter Anschlag genau EINEN Screen weiterschaltet, ist das Nachdrücken
// sicher, solange auf den unmittelbar nächsten Screen gewartet wird.
bool pressKeyUntil(A5120Machine& m, uint32_t kc, const std::string& needle, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        typeKey(m, kc);
        if (runSmallUntil(m, needle, 8'000'000)) return true;
    }
    return false;
}

}  // namespace

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
