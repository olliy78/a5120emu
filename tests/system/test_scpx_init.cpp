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

#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

#include <cstdint>
#include <string>

using k1520test::diskPath;
using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::runSmallUntil;
using k1520test::TempDisk;
using k1520test::typeCtrl;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

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
    TempDisk a("scpx17_cpa780_k5601.hfe");                    // A: beschreibbare Kopie
    TempDisk b = TempDisk::empty("scpx_pip_guard_B.hfe");     // B: wird erst erzeugt
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

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
}

/**
 * @test ScpxInit/Builds5x1024SystemViaInitModfSyspAndBoots
 * @brief Kompletter „5×1024-Systemdiskette erzeugen"-Workflow end-to-end.
 *
 * Deckt die im Merkzettel [[project_scpx_5x1024_read_freeze]] beschriebene EINZIG
 * gültige Nutzung von 5×1024 ab: NICHT per MODF ein 16×256-System umbiegen, sondern
 * mit SYSP ein System FÜR 5×1024 generieren.  Ablauf (alle Prompts am realen SCPX-
 * Dialog verifiziert):
 *   1. A: = beschreibbare Kopie von scpx17_cpa780_k5601.hfe; B: = frische Leerdisk (createDisk).
 *   2. INIT B: mit **Option 3 = DD-DS 5×1024** formatieren → `BAD TRACKS: - NO -`.
 *   3. MODF B: auf Format 3 (5×1024) umstellen; Wechsel auf B: löst EINMALIG
 *      `SCPX ERR ON B: BAD SECTOR` aus (erwartet — Diskettenwechsel-Erkennung), nach
 *      <ENTER> geht es weiter; `DIR` zeigt `NO FILE` (leere, LESBARE 5×1024-Disk).
 *   4. **SYSP** generiert ein 5×1024-System nach B:: Tastatur K7637 (2), Bildschirm
 *      80×24 (2), 3 Laufwerke, alle Typ 5 (5,25″ DS 80), Typangabe J, Format 1
 *      (5×1024), Drucker 1 (IFSS), Angaben J, Ausgabe-Lw B, Diskette eingelegt J
 *      → `Systemdiskette ist generiert !`.
 *   5. SYSP beenden (nochmals? N, anderes? N), Ctrl-C-Warmstart, `PIP B:=A:*.*`
 *      kopiert ALLE Dateien (bis SYSP.COM, keine `BAD SECTOR`).
 *   6. Von der frisch generierten B:-Disk (bPath) in einer NEUEN Maschine booten →
 *      SCPX-Banner + `A>`  (der Boot-Lader liest die 5×1024-Systemspuren).
 *   7. Nach einmaliger BAD-SECTOR-Quittung: `DIR` listet die kopierten Dateien
 *      (STAT/INIT), `STAT` läuft (`A: R/W`) → das generierte 5×1024-System läuft.
 *
 * Langsam (2× Vollformat 160 Spuren + SYSP + PIP) → Label `format_integration`.
 * Zugleich ein starker Wächter für die zyklengenaue Peripherie (K5122-Index-IRQ,
 * CTC/Tastatur-Timing) bei Höchstgeschwindigkeit.
 */
TEST(ScpxInit, Builds5x1024SystemViaInitModfSyspAndBoots) {
    TempDisk a("scpx17_cpa780_k5601.hfe");                    // A: beschreibbare Kopie
    TempDisk b = TempDisk::empty("scpx_5x1024_B.hfe");        // B: wird erst erzeugt
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    // B: physisch 80-Zyl-DS-Blank (INIT formatiert die Sektorstruktur direkt auf 5×1024 um).
    ASSERT_TRUE(machine.createDisk(1, bPath, "k5601_16x256", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000)) << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);

    // ── 2. INIT B: Option 3 = DD-DS 5×1024 ───────────────────────────────────
    typeString(machine, "INIT"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "PLEASE ENTER DRIVE NAME", 30'000'000)) << vramText(machine);
    typeString(machine, "B"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "HIT <ENTER> FOR DEFAULT", 20'000'000)) << vramText(machine);
    typeString(machine, "3"); typeKey(machine, QK_RETURN);          // Option 3 = DD-DS 5×1024
    ASSERT_TRUE(runSmallUntil(machine, "PRESS <ENTER>", 20'000'000)) << vramText(machine);
    typeKey(machine, QK_RETURN);                                    // Disk eingelegt
    ASSERT_TRUE(runSmallUntil(machine, "(Y/N)", 20'000'000)) << vramText(machine);
    typeString(machine, "Y"); typeKey(machine, QK_RETURN);          // Scratch bestätigen
    ASSERT_TRUE(runSmallUntil(machine, "FORMATTING COMPLETE", 300'000'000)) << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "ONCE MORE", 10'000'000)) << vramText(machine);
    EXPECT_NE(vramText(machine).find("BAD TRACKS: - NO -"), std::string::npos)
        << "INIT (Opt 3, 5×1024) meldete BAD TRACKS:\n" << vramText(machine);
    typeString(machine, "N"); typeKey(machine, QK_RETURN);          // INIT verlassen
    ASSERT_TRUE(runSmallUntil(machine, "A>", 10'000'000)) << vramText(machine);
    runCycles(machine, 2'000'000);

    // ── 3. MODF B: → Format 3, dann auf B: wechseln (einmalig BAD SECTOR) ─────
    typeString(machine, "MODF"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "PLEASE ENTER DRIVE NAME", 8'000'000)) << vramText(machine);
    typeString(machine, "B"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "HIT <ENTER> FOR DEFAULT", 8'000'000)) << vramText(machine);
    typeString(machine, "3"); typeKey(machine, QK_RETURN);          // Format 3 = 5×1024
    ASSERT_TRUE(runSmallUntil(machine, "PLACE DISK INTO DRIVE B", 8'000'000)) << vramText(machine);
    typeKey(machine, QK_RETURN);                                    // Disk eingelegt → MODF fertig
    ASSERT_TRUE(runSmallUntil(machine, "A>", 8'000'000)) << vramText(machine);
    runCycles(machine, 2'000'000);
    typeString(machine, "B:"); typeKey(machine, QK_RETURN);         // auf B: wechseln
    runCycles(machine, 12'000'000);
    // Der erste Zugriff auf B: quittierte hier bis 2026-08-04 EINMALIG
    // `SCPX ERR ON B: BAD SECTOR`; seither laeuft er sauber durch.  Beides wird
    // akzeptiert — massgeblich ist, dass B: DANACH lesbar ist (`DIR` → `NO FILE`).
    //
    // Zugeordnet per git-bisect + gezielter Mutation: der Ausloeser ist Fix 3 aus
    // 76a959a — /STR=1 DURCH ZVE2 beendet dessen Busbesitz jetzt SOFORT (K5122-Doku
    // §5.5), statt erst ueber die /STR=1-Abtastung ~2 Byteperioden spaeter.  In
    // diesem Fenster lief ZVE2 frueher eine Instruktion zu weit (bei UDOS zerschrieb
    // das nachweislich den CRC-Puffer).  Die einmalige BAD-SECTOR-Meldung beim
    // Diskettenwechsel war ein Artefakt genau dieses Fensters.
    //
    // ⚠ Das ist NICHT die SCPX-Single-Format-Eigenschaft aus
    // doc/analyse_scpx_5x1024_read.md §4 — die gilt unveraendert und wird von
    // `ScpxIntegration.WrongFormatReadTerminatesInsteadOfFreezing` (Default-Suite)
    // weiterhin scharf geprueft: OHNE MODF liefert eine 5×1024-Diskette am
    // 16×256-System nach wie vor `BAD SECTOR`.  Hier ist B: per MODF Option 3
    // korrekt auf 5×1024 umgestellt, der Zugriff DARF also gelingen.
    if (vramText(machine).find("BAD SECTOR") != std::string::npos) {
        typeKey(machine, QK_RETURN);                                // BAD SECTOR quittieren
        runCycles(machine, 5'000'000);
    }
    typeString(machine, "DIR"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "NO FILE", 15'000'000))
        << "frische 5×1024-B: nicht lesbar (kein NO FILE):\n" << vramText(machine);

    // ── 4. SYSP: 5×1024-System nach B: generieren ────────────────────────────
    typeString(machine, "A:"); typeKey(machine, QK_RETURN);
    runCycles(machine, 3'000'000);
    typeString(machine, "SYSP"); typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "Tastatur-Typ :", 8'000'000)) << vramText(machine);
    typeString(machine, "2"); typeKey(machine, QK_RETURN);          // Tastatur K7637
    ASSERT_TRUE(runSmallUntil(machine, "Format :", 4'000'000)) << vramText(machine);
    typeString(machine, "2"); typeKey(machine, QK_RETURN);          // Bildschirm 80×24
    ASSERT_TRUE(runSmallUntil(machine, "Anzahl FD-Laufwerke", 4'000'000)) << vramText(machine);
    typeString(machine, "3"); typeKey(machine, QK_RETURN);          // 3 Laufwerke
    ASSERT_TRUE(runSmallUntil(machine, "Typ Lw A", 4'000'000)) << vramText(machine);
    typeString(machine, "5"); typeKey(machine, QK_RETURN);          // Lw A Typ 5
    ASSERT_TRUE(runSmallUntil(machine, "Typ Lw B", 4'000'000)) << vramText(machine);
    typeString(machine, "5"); typeKey(machine, QK_RETURN);          // Lw B Typ 5
    ASSERT_TRUE(runSmallUntil(machine, "Typ Lw C", 4'000'000)) << vramText(machine);
    typeString(machine, "5"); typeKey(machine, QK_RETURN);          // Lw C Typ 5
    ASSERT_TRUE(runSmallUntil(machine, "Typangabe richtig", 4'000'000)) << vramText(machine);
    typeString(machine, "J"); typeKey(machine, QK_RETURN);          // Typangabe richtig
    ASSERT_TRUE(runSmallUntil(machine, "Kennziffer (0 oder 1)", 4'000'000)) << vramText(machine);
    typeString(machine, "1"); typeKey(machine, QK_RETURN);          // Format 1 = 5×1024
    ASSERT_TRUE(runSmallUntil(machine, "Druckertyp :", 4'000'000)) << vramText(machine);
    typeString(machine, "1"); typeKey(machine, QK_RETURN);          // Drucker 1 = IFSS
    ASSERT_TRUE(runSmallUntil(machine, "Typangabe richtig", 4'000'000)) << vramText(machine);
    typeString(machine, "J"); typeKey(machine, QK_RETURN);          // Druckerangabe richtig
    ASSERT_TRUE(runSmallUntil(machine, "Ausgabe-Lw", 4'000'000)) << vramText(machine);
    typeString(machine, "B"); typeKey(machine, QK_RETURN);          // Ausgabe auf Lw B
    ASSERT_TRUE(runSmallUntil(machine, "Diskette eingelegt", 4'000'000)) << vramText(machine);
    typeString(machine, "J"); typeKey(machine, QK_RETURN);          // Diskette eingelegt → generieren
    ASSERT_TRUE(runSmallUntil(machine, "Systemdiskette ist generiert", 90'000'000))
        << "SYSP generierte das 5×1024-System nicht:\n" << vramText(machine);

    // ── 5. SYSP beenden, Warmstart, alle Dateien kopieren ────────────────────
    ASSERT_TRUE(runSmallUntil(machine, "nochmals generiert", 5'000'000)) << vramText(machine);
    typeString(machine, "N"); typeKey(machine, QK_RETURN);          // gleiches System nochmal? Nein
    ASSERT_TRUE(runSmallUntil(machine, "anderes System", 5'000'000)) << vramText(machine);
    typeString(machine, "N"); typeKey(machine, QK_RETURN);          // anderes System? Nein → A>
    ASSERT_TRUE(runSmallUntil(machine, "A>", 10'000'000)) << vramText(machine);
    runCycles(machine, 3'000'000);
    typeCtrl(machine, 'C');                                         // Warmstart → B: neu einloggen
    runCycles(machine, 5'000'000);
    typeString(machine, "PIP B:=A:*.*"); typeKey(machine, QK_RETURN);
    // PIP listet die kopierten Dateien mit Punkt (FILENAME.EXT); SYSP.COM ist die letzte.
    ASSERT_TRUE(runSmallUntil(machine, "SYSP.COM", 120'000'000))
        << "PIP kopierte nicht alle Dateien nach B::\n" << vramText(machine);
    runCycles(machine, 5'000'000);

    machine.unmountDisk(0);
    machine.unmountDisk(1);                                         // bPath (B:) flushen/schliessen

    // ── 6./7. Von der neu generierten 5×1024-Systemdiskette booten ───────────
    A5120Machine boot;
    ASSERT_TRUE(boot.mountDisk(0, bPath, "cpa780", /*wp=*/false)) << boot.lastError();
    boot.powerOn();
    ASSERT_TRUE(runSmallUntil(boot, "A>", 80'000'000))
        << "generierte 5×1024-Systemdiskette bootete nicht bis A>:\n" << vramText(boot);
    runCycles(boot, 3'000'000);

    // Erster Diskzugriff nach Boot: einmalige BAD-SECTOR-Meldung → quittieren.
    typeString(boot, "DIR"); typeKey(boot, QK_RETURN);
    runCycles(boot, 15'000'000);
    typeKey(boot, QK_RETURN);                                       // evtl. BAD SECTOR quittieren
    runCycles(boot, 5'000'000);
    typeString(boot, "DIR"); typeKey(boot, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(boot, "STAT     COM", 25'000'000))
        << "DIR listete die kopierten Dateien nicht (System nicht lesbar):\n" << vramText(boot);
    runCycles(boot, 10'000'000);                                   // DIR fertig, zurück zum A>-Prompt

    // STAT lädt STAT.COM von der 5×1024-Disk und meldet den Laufwerksstatus.  Der
    // erste Zugriff löst nochmals einmalig BAD SECTOR aus (Diskettenwechsel-Erkennung);
    // das ist eine „retry"-Aufforderung — nach <ENTER> läuft STAT durch und zeigt den
    // Status `A: R/W, Space: …`.
    typeString(boot, "STAT"); typeKey(boot, QK_RETURN);
    runCycles(boot, 25'000'000);
    typeKey(boot, QK_RETURN);                                       // BAD SECTOR quittieren → STAT läuft durch
    ASSERT_TRUE(runSmallUntil(boot, "R/W", 30'000'000))
        << "STAT lief nicht auf dem generierten 5×1024-System (.COM-Laden gebrochen):\n" << vramText(boot);

    boot.unmountDisk(0);
}
