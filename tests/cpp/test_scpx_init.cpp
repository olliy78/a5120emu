/**
 * @file test_scpx_init.cpp
 * @brief Langsamer End-to-End-Regressionswächter: SCPX INIT.COM formatiert
 *        Laufwerk A: vollständig OHNE „BAD TRACKS".
 *
 * Eigenes Executable (nicht in test_boot_integration.cpp), damit der Test über das
 * ctest-Label „format_integration" aus dem Default-`tools/dev.sh test` ausgeschlossen
 * werden kann (er formatiert+verifiziert 160 Spuren und läuft ~35 s).  Er läuft mit
 * `tools/dev.sh test-format` / `test-all`.
 *
 * Guard für den K5122-Index-Phasen-Fix (commitFormatTrack setzt index_cycle_acc_ = 0;
 * doc/analyse_scpx_init_verify_handoff.md).  INIT.COM ist SCPXs FORMAT.COM-Gegenstück:
 * es programmiert den K5122 DIREKT (kein BIOS-Call), formatiert alle 80 Zylinder ×2
 * Köpfe und VERIFIZIERT jede Spur.  Der Verify synchronisiert per Index-Interrupt-
 * Flag [12A8]: ZVE1 löscht es pro Spur (0x0EF0), ZVE2 verlangt es am Spur-Beginn clear
 * (0x1115) und wartet dann auf den Index (0x111B).  Lief der Index-Puls frei relativ
 * zum Byte-Takt, fiel er zwischen Clear und Check → [12A8] gesetzt → der „index-flag
 * muss clear"-Test scheiterte → BAD TRACKS auf Kopf 1 / ungeraden Zylindern.  Der Fix
 * koppelt die Index-Phase ans Spur-Ende (der Vollspur-FORMAT-Write endet auf echter HW
 * GENAU am Index), sodass der nächste Index wieder hinter den Check in INITs
 * Warteschleife fällt.  VOR dem Fix: „BAD TRACKS: 00,01,03,05,…"; danach: „- NO -".
 */

#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"

#include <cstdint>
#include <filesystem>
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

// Qt-Keycode für Return (== K7637::QK_RETURN).
constexpr uint32_t QK_RETURN = 0x01000004;

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

// Ctrl-<c> senden (K7637: Ctrl-Buchstabe → keycode & 0x1F, z. B. Ctrl-C = 0x03).
void typeCtrl(A5120Machine& m, char c) {
    m.keyPress(static_cast<uint8_t>(c), /*shift=*/false, /*ctrl=*/true);
    runCycles(m, 1'000'000);
    m.keyRelease(static_cast<uint8_t>(c));
    runCycles(m, 300'000);
}

}  // namespace

/**
 * @test ScpxInit/InitFormatsDriveAWithNoBadTracks
 * @brief SCPX INIT.COM formatiert+verifiziert alle 160 Spuren OHNE Bad-Track.
 *
 * A: wird aus einer BESCHREIBBAREN TEMP-KOPIE gemountet — INIT formatiert es, die
 * committete Fixture disks/scpx_boot.hfe bleibt unangetastet.
 */
TEST(ScpxInit, InitFormatsDriveAWithNoBadTracks) {
    namespace fs = std::filesystem;
    const std::string aPath = (fs::temp_directory_path() / "scpx_init_guard_A.hfe").string();
    fs::copy_file(diskPath("scpx_boot.hfe"), aPath, fs::copy_options::overwrite_existing);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000))
        << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);   // in die CONIN-Leseschleife einschwingen

    // INIT starten → Dialog: Laufwerk A:, Default-Format (DD-DS 16×256), Disk
    // einlegen, Scratch bestätigen.  Zwischen den Schritten auf den jeweils neuen
    // Prompt warten (VRAM akkumuliert, daher distinktive Zeilen).
    typeString(machine, "INIT");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "PLEASE ENTER DRIVE NAME", 30'000'000))
        << "INIT startete nicht (kein Laufwerks-Prompt):\n" << vramText(machine);

    typeString(machine, "A");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "HIT <ENTER> FOR DEFAULT", 20'000'000))
        << "Format-Menü nach Laufwerkswahl nicht erschienen:\n" << vramText(machine);

    typeKey(machine, QK_RETURN);   // Default-Format
    ASSERT_TRUE(runSmallUntil(machine, "PRESS <ENTER>", 20'000'000))
        << "Disk-einlegen-Prompt nicht erschienen:\n" << vramText(machine);

    typeKey(machine, QK_RETURN);   // Disk eingelegt
    ASSERT_TRUE(runSmallUntil(machine, "(Y/N)", 20'000'000))
        << "Scratch-Warnung nicht erschienen:\n" << vramText(machine);

    typeString(machine, "Y");
    typeKey(machine, QK_RETURN);   // Formatieren starten

    // Kernprüfung: INIT formatiert+verifiziert alle 160 Spuren und meldet KEINE
    // Bad-Tracks.  Ohne den Index-Phasen-Fix stünde hier „BAD TRACKS: 00,01,03,…".
    ASSERT_TRUE(runSmallUntil(machine, "FORMATTING COMPLETE", 300'000'000))
        << "INIT beendete das Formatieren nicht:\n" << vramText(machine);
    // Das Bad-Track-Verdikt (+ „ONCE MORE?"-Prompt) erscheint erst NACH „FORMATTING
    // COMPLETE".  Auf den ABSCHLIESSENDEN Prompt warten, damit die Verdikt-Zeile
    // garantiert vollständig gerendert ist (sonst steht erst „BAD TRACKS" ohne den
    // „ - NO - "-Zusatz auf dem Schirm → falscher Fehlalarm).
    ASSERT_TRUE(runSmallUntil(machine, "ONCE MORE", 10'000'000))
        << "INIT gab kein abschließendes Bad-Track-Verdikt aus:\n" << vramText(machine);
    EXPECT_NE(vramText(machine).find("BAD TRACKS: - NO -"), std::string::npos)
        << "INIT meldete BAD TRACKS — Index-Phasen-Fix (commitFormatTrack) gebrochen:\n"
        << vramText(machine);

    machine.unmountDisk(0);
    std::error_code ec;
    fs::remove(aPath, ec);
}

/**
 * @test ScpxInit/CreateFormatBThenPipCopyFromBootDisk
 * @brief Voller SCPX-Formatier-Workflow end-to-end: eine FRISCH erzeugte, leere `.hfe`
 *        wird mit INIT.COM formatiert und anschließend per PIP beschrieben.
 *
 * Ablauf:
 *   1. A: = beschreibbare Kopie der Bootdiskette (Quelle für STAT.COM).
 *   2. B: = frisch via `A5120Machine::createDisk(..., "k5601_16x256", ...)` erzeugte LEERE
 *      Diskette (Geometrie DD-DS 16×256 = 80 Zyl ×2 Köpfe ×16 Sekt ×256 B).
 *   3. SCPX booten, mit INIT B: **formatieren** (Default DD-DS 16×256) → `BAD TRACKS: - NO -`.
 *   4. Ctrl-C-Warmstart, damit CP/A B: nach dem Direkt-Format neu einloggt (sonst R/O).
 *   5. `PIP B:=A:STAT.COM` kopiert STAT.COM von der Bootdiskette auf die neue Diskette.
 *   6. `DIR B:` zeigt die kopierte Datei → Format+Schreiben+Lesen der neuen Diskette ok,
 *      KEIN `BAD SECTOR`.
 *
 * Beide Disketten kommen aus beschreibbaren TEMP-Dateien; die committete Fixture bleibt
 * unangetastet.  Langsam (INIT formatiert 160 Spuren) → Label `format_integration`.
 */
TEST(ScpxInit, CreateFormatBThenPipCopyFromBootDisk) {
    namespace fs = std::filesystem;
    const std::string aPath = (fs::temp_directory_path() / "scpx_pip_guard_A.hfe").string();
    const std::string bPath = (fs::temp_directory_path() / "scpx_pip_guard_B.hfe").string();
    fs::copy_file(diskPath("scpx_boot.hfe"), aPath, fs::copy_options::overwrite_existing);
    std::error_code ec;
    fs::remove(bPath, ec);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    // Leere Zieldiskette DD-DS 16×256 erzeugen UND auf B: mounten (createDisk mountet gleich).
    ASSERT_TRUE(machine.createDisk(1, bPath, "k5601_16x256", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000))
        << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);

    // ── INIT: Laufwerk B: formatieren (Default DD-DS 16×256) ─────────────────
    typeString(machine, "INIT");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "PLEASE ENTER DRIVE NAME", 30'000'000))
        << "INIT startete nicht:\n" << vramText(machine);
    typeString(machine, "B");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "HIT <ENTER> FOR DEFAULT", 20'000'000))
        << "Format-Menü nach Laufwerkswahl B: nicht erschienen:\n" << vramText(machine);
    typeKey(machine, QK_RETURN);   // Default-Format DD-DS 16×256
    ASSERT_TRUE(runSmallUntil(machine, "PRESS <ENTER>", 20'000'000))
        << "Disk-einlegen-Prompt nicht erschienen:\n" << vramText(machine);
    typeKey(machine, QK_RETURN);   // Disk eingelegt
    ASSERT_TRUE(runSmallUntil(machine, "(Y/N)", 20'000'000))
        << "Scratch-Warnung nicht erschienen:\n" << vramText(machine);
    typeString(machine, "Y");
    typeKey(machine, QK_RETURN);   // Formatieren starten

    ASSERT_TRUE(runSmallUntil(machine, "FORMATTING COMPLETE", 300'000'000))
        << "INIT beendete das Formatieren von B: nicht:\n" << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "ONCE MORE", 10'000'000))
        << "INIT gab kein Bad-Track-Verdikt aus:\n" << vramText(machine);
    ASSERT_NE(vramText(machine).find("BAD TRACKS: - NO -"), std::string::npos)
        << "INIT meldete BAD TRACKS beim Formatieren von B::\n" << vramText(machine);

    // ── INIT verlassen (ONCE MORE? → N) + Ctrl-C-Warmstart ───────────────────
    // Der Warmstart lässt CP/A B: nach dem BIOS-fremden Direkt-Format neu einloggen
    // (sonst bleibt B: als „gewechselte" Diskette R/O → PIP-Write scheiterte).
    typeString(machine, "N");
    typeKey(machine, QK_RETURN);
    runCycles(machine, 5'000'000);          // INIT kehrt zum CCP zurück
    typeCtrl(machine, 'C');                  // Warmstart → Laufwerke neu einloggen
    runCycles(machine, 5'000'000);

    // ── PIP: STAT.COM von A: auf die frisch formatierte B: kopieren ──────────
    typeString(machine, "PIP B:=A:STAT.COM");
    typeKey(machine, QK_RETURN);
    runCycles(machine, 40'000'000);          // Kopier-Read (A:) + Write (B:) abwarten
    EXPECT_EQ(vramText(machine).find("BAD SECTOR"), std::string::npos)
        << "PIP meldete BAD SECTOR beim Schreiben auf die frisch formatierte B::\n"
        << vramText(machine);

    // ── Verifikation: DIR B: listet die kopierte Datei ───────────────────────
    // Gefiltertes DIR listet „B: STAT     COM" (das Leerzeichen nach „B:" unterscheidet
    // die Listing-Zeile vom Kommando-Echo, vgl. ScpxIntegration.EraDeletes…).
    typeString(machine, "DIR B:STAT.COM");
    typeKey(machine, QK_RETURN);
    EXPECT_TRUE(runSmallUntil(machine, "B: STAT", 30'000'000))
        << "STAT.COM nach PIP nicht auf der neuen B: (Format/Write/Read-Pfad gebrochen):\n"
        << vramText(machine);

    machine.unmountDisk(0);
    machine.unmountDisk(1);
    fs::remove(aPath, ec);
    fs::remove(bPath, ec);
}
