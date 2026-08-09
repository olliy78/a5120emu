/**
 * @file test_udos_format.cpp
 * @brief Langsame End-to-End-Regressionswächter für den UDOS-4.3-Schreibpfad:
 *        `FORMAT`, `COPY.DISK` und der komplette Bau einer bootfähigen Systemdiskette.
 *
 * Eigenes Executable mit ctest-Label „format_integration" (wie test_scpx_init.cpp),
 * damit die Tests aus dem Default-`tools/dev.sh test` herausfallen — sie booten UDOS,
 * formatieren 77 Spuren je Seite und lassen sie von UDOS selbst verifizieren
 * (~6 s / ~11 s / ~33 s).
 *
 * | Test | deckt ab |
 * |---|---|
 * | `FormatsDriveOneIntoUsableZdosDisk` | `FORMAT` einer Datenseite + Belegungskarte |
 * | `FormatsBrandNewBlankDiskette` | FRISCHE LEERDISKETTE (unformatiert) unter UDOS formatieren |
 * | `CopyDiskDuplicatesSystemDiskSectorBySector` | sektorweises Kopieren am Dateisystem vorbei |
 * | `BuildsBootableSystemDiskAndBootsFromIt` | LEERDISKETTE als `.dmk`, Systemspuren, Rückseite (Laufwerk 5), `MOVE`, Kaltstart von der selbstgebauten `.dmk` |
 *
 * Guard für **zwei** Fehler, die zusammen jedes Formatieren unter UDOS verhinderten:
 *
 * 1. **Laufwerksauswahl (K5122 Port 18H) nibbelvertauscht.** UDOS bildet sein
 *    Anwahlbyte als `LD A,77H / RLCA (LW+1)× / AND 0F0H` — also NUR das High-Nibble
 *    (`0xD0` = Laufwerk 1, alle /LCK aktiv).  Der Emulator las das High-Nibble als
 *    Motor und das (hier durchweg nullwertige) Low-Nibble als Select → „alle vier
 *    selektiert" → Laufwerk 0.  FORMAT beschrieb damit B:, verifizierte anschließend
 *    A: und meldete für JEDE Spur „DEFEKTIVE TRACK", am Ende `NOT FOR UDOS USEABLE`.
 *    Belegt an den Originalquellen, s. doc/design/07_k5122_afs.md §8.
 * 2. **Sektorkontrollblock ging beim Schreiben verloren.** FORMAT schreibt nach dem
 *    Spurformatieren Belegungskarte (Spur 23) und Verzeichnis (Spur 22) über den
 *    normalen Datenfeld-Schreibpfad; UDOS legt die Verkettung in 4 Bytes HINTER der
 *    Daten-CRC ab.  Ohne sie ist die frische Diskette nicht lesbar
 *    („POINTER CHECK ERROR CA", doc/udos_bug1.md).
 *
 * @see doc/udos_diskettenformat.md §12
 * @see doc/udos_bug1.md
 */

#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"

#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

#include <cstdint>
#include <filesystem>
#include <string>

using k1520test::diskPath;
using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::vramText;
// ⚠ UDOS-Konsole ist schreibungsinvertiert: KLEIN tippen ergibt GROSS auf dem
// Bildschirm (doc/analyse_udos.md §14.2) — Kommandos in dieser Datei also klein
// schreiben.
using k1520test::typeString;

namespace {

// UDOS-Laeufe dauern zweistellige Sekunden; der Bildschirm wird deshalb nur alle
// 50 000 Takte abgesucht statt nach jedem 5000er-Batch.  Die MASCHINE laeuft
// unveraendert in 5000er-Schritten — nur das Lesen des 2-KB-VRAM wird seltener.
constexpr long long kVramCheckEvery = 50'000;

bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles) {
    return k1520test::runSmallUntil(m, needle, max_cycles, kVramCheckEvery);
}

// Auf einen Prompt warten und die Antwort tippen.
void antworte(A5120Machine& m, const std::string& prompt, const std::string& antwort,
              long long budget = 30'000'000) {
    ASSERT_TRUE(runSmallUntil(m, prompt, budget))
        << "Prompt '" << prompt << "' nie erschienen:\n" << vramText(m);
    typeString(m, antwort);
    typeKey(m, QK_RETURN);
}

// Beschreibbare Kopie der UDOS-Bootdiskette unter eigenem Temp-Namen.
// TempDisk raeumt sie am Testende weg — auch wenn ein ASSERT vorher abbricht.
TempDisk legeSystemdiskette(const std::string& temp_name) {
    return TempDisk("udos_boot_scp.hfe", temp_name);
}

// UDOS booten und das Datum setzen; danach steht der `%`-Prompt.
void booteUdos(A5120Machine& m) {
    ASSERT_TRUE(runSmallUntil(m, "Neues Datum", 120'000'000))
        << "UDOS-Datumsabfrage nie erschienen:\n" << vramText(m);
    typeString(m, "150388");                // formatiertes Feld, kein ENTER noetig
    ASSERT_TRUE(runSmallUntil(m, "UDOS BC.5120", 40'000'000))
        << "UDOS-Prompt nie erreicht:\n" << vramText(m);
    // Ein sauberer Start heisst: BEIDE Disketten sind als ZDOS-Datentraeger eingelesen.
    ASSERT_EQ(vramText(m).find("DISK INITIALIZATION ERROR"), std::string::npos)
        << "UDOS konnte eine der beiden Disketten nicht einlesen:\n" << vramText(m);
    runCycles(m, 3'000'000);
}

// Wie booteUdos, aber OHNE die Forderung, dass beide Disketten einlesbar sind:
// in Laufwerk 1 liegt hier eine noch voellig unformatierte Leerdiskette, die UDOS
// zu Recht nicht als Datentraeger annimmt (genau die soll ja formatiert werden).
void booteUdosMitLeerdiskette(A5120Machine& m) {
    ASSERT_TRUE(runSmallUntil(m, "Neues Datum", 120'000'000))
        << "UDOS-Datumsabfrage nie erschienen:\n" << vramText(m);
    typeString(m, "150388");
    ASSERT_TRUE(runSmallUntil(m, "UDOS BC.5120", 60'000'000))
        << "UDOS-Prompt nie erreicht:\n" << vramText(m);
    runCycles(m, 3'000'000);
}

// `FORMAT`-Dialog fahren: SYSTEMDISK? / DRIVE? / ID? / READY?
// @p systemdisk "y" schreibt zusaetzlich die Urlader (Spur 0/1/2) und das Bootabbild
// (Spur 21) — nur damit wird die Diskette bootfaehig.
void formatiere(A5120Machine& m, const std::string& systemdisk,
                const std::string& laufwerk, const std::string& id) {
    typeString(m, "format");
    typeKey(m, QK_RETURN);
    antworte(m, "SYSTEMDISK", systemdisk);
    antworte(m, "DRIVE",      laufwerk);
    antworte(m, "ID?",        id);
    antworte(m, "READY",      "y");
    runCycles(m, 400'000'000);              // 77 Spuren formatieren + verifizieren
}

// `FORMAT` auf Laufwerk 1 fahren (Datentraegername TESTDISK, keine Systemdiskette).
void formatiereLaufwerk1(A5120Machine& m) {
    formatiere(m, "n", "1", "testdisk");
}

}  // namespace

/**
 * @test UdosFormat/FormatsDriveOneIntoUsableZdosDisk
 * @brief `FORMAT` formatiert Laufwerk 1 fehlerfrei; die frische Diskette meldet sich
 *        danach im laufenden System mit ihrem Namen und der korrekten Kapazität.
 *
 * Ablauf: UDOS 4.3 booten (A: = Kopie der Bootdiskette), Datum setzen, `FORMAT` mit
 * SYSTEMDISK=N / DRIVE=1 / ID=TESTDISK / READY=Y fahren, danach `STATUS`.
 *
 * B: ist ebenfalls eine Kopie der Bootdiskette — realitätsnah („gebrauchte Diskette
 * neu formatieren") und beidseitig UDOS, sodass UDOS beim Start auch das Rückseiten-
 * Laufwerk 5 sauber einliest.  Formatiert wird (DISKCON `41`, einseitig) nur Seite 0
 * = Laufwerk 1; Laufwerk 5 bleibt die alte Rückseite.
 *
 * Sollwerte: 77 Spuren × 26 Sektoren = 2002, davon 14 Systemsektoren (Urlader,
 * Bootabbild, Verzeichnis, Belegungskarte) → **1988 frei**
 * (doc/udos_diskettenformat.md §3/§4).
 */
TEST(UdosFormat, FormatsDriveOneIntoUsableZdosDisk) {
    TempDisk a = legeSystemdiskette("udos_format_guard_A.hfe");
    TempDisk b = legeSystemdiskette("udos_format_guard_B.hfe");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    // Emulator-Log auf ERROR drosseln — der Lauf erzeugt sonst Tausende
    // K5122-INFO-Zeilen (77 FORMAT-Writes + jeder Spur-Read).
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    ASSERT_TRUE(machine.mountDisk(1, bPath, "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    booteUdos(machine);

    // 77 Spuren formatieren + verifizieren.  Bei falscher Laufwerksauswahl liefe der
    // Verify gegen Laufwerk 0 und fuellte den Schirm mit „DEFEKTIVE TRACK".
    formatiereLaufwerk1(machine);
    const std::string nachFormat = vramText(machine);
    EXPECT_EQ(nachFormat.find("DEFEKTIVE TRACK"), std::string::npos)
        << "FORMAT meldete defekte Spuren:\n" << nachFormat;
    EXPECT_EQ(nachFormat.find("NOT FOR UDOS USEABLE"), std::string::npos)
        << "FORMAT verwarf die Diskette:\n" << nachFormat;

    // ── Gegenprobe im laufenden System ──────────────────────────────────────
    typeString(machine, "status");
    typeKey(machine, QK_RETURN);
    // Auf die FREI-Zeile warten (nicht auf „DRIVE 1“) — sonst trifft der Schnappschuss
    // die Zeile mitten im Aufbau.
    ASSERT_TRUE(runSmallUntil(machine, "1988 SECTORS AVAILABLE", 100'000'000))
        << "STATUS meldet fuer Laufwerk 1 nicht 77*26-14 = 1988 freie Sektoren "
           "(Belegungskarte falsch geschrieben?):\n" << vramText(machine);
    const std::string status = vramText(machine);
    EXPECT_NE(status.find("DRIVE 1   TESTDISK"), std::string::npos)
        << "Laufwerk 1 traegt nicht den frisch vergebenen Datentraegernamen:\n" << status;

    machine.unmountDisk(0);
    machine.unmountDisk(1);
}

/**
 * @test UdosFormat/CopyDiskDuplicatesSystemDiskSectorBySector
 * @brief `COPY.DISK` kopiert Laufwerk 0 sektorweise auf Laufwerk 1 — samt der
 *        Sektorkontrollblöcke, sodass das Duplikat ein lesbares Dateisystem hat.
 *
 * Der schärfste Test des Schreibpfads: 77 Spuren × 26 Sektoren werden gelesen und
 * ohne Umweg über das Dateisystem zurückgeschrieben.  Ginge dabei die Verkettung
 * verloren (doc/udos_bug1.md), wäre das Duplikat unbrauchbar; zeigte die
 * Laufwerksauswahl auf das falsche Laufwerk, kopierte UDOS die Bootdiskette auf
 * sich selbst.
 *
 * Damit ein erfolgreicher Kopiervorgang überhaupt nachweisbar ist, wird B: zuvor mit
 * `FORMAT` geleert (Datenträgername `TESTDISK`, 1988 frei).  Nach `COPY.DISK` muss
 * Laufwerk 1 exakt die Kennung von Laufwerk 0 tragen (`UDOS.SYS.4.3`, 850 frei) und
 * `CAT D=1` muss die kopierten Dateien auflisten.
 *
 * ⚠ **`ERROR C4 ON TRACK 33 DRIVE 00` ist hier ERWARTET und kein Emulatorfehler:**
 * die Referenzdiskette wurde von echter Hardware eingelesen und hat auf Spur 0x33=51,
 * Seite 0 einen physisch fehlenden Sektor (S13; die übrigen 25 sind CRC-sauber).
 * UDOS meldet den defekten Sektor und kopiert weiter — der Test prüft genau das.
 */
TEST(UdosFormat, CopyDiskDuplicatesSystemDiskSectorBySector) {
    TempDisk a = legeSystemdiskette("udos_copydisk_guard_A.hfe");
    TempDisk b = legeSystemdiskette("udos_copydisk_guard_B.hfe");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    ASSERT_TRUE(machine.mountDisk(1, bPath, "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    booteUdos(machine);

    // ── Ausgangslage schaffen: B: leer formatieren ──────────────────────────
    formatiereLaufwerk1(machine);
    typeString(machine, "status");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "1988 SECTORS AVAILABLE", 100'000'000))
        << "Vorbedingung: B: wurde nicht leer formatiert:\n" << vramText(machine);

    // ── COPY.DISK (ohne Parameter: Laufwerk 0 → Laufwerk 1) ─────────────────
    typeString(machine, "copy.disk");
    typeKey(machine, QK_RETURN);
    antworte(machine, "DRIVES READY", "y", 60'000'000);
    runCycles(machine, 250'000'000);        // 77 Spuren lesen + schreiben

    // ── Gegenprobe: Laufwerk 1 ist jetzt die Systemdiskette ─────────────────
    typeString(machine, "status");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "DRIVE 1   UDOS.SYS.4.3", 100'000'000))
        << "Laufwerk 1 traegt nach COPY.DISK nicht die Kennung der Quelle:\n"
        << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "1152 SECTORS USED", 20'000'000))
        << "Belegung von Laufwerk 1 stimmt nicht mit der Quelle ueberein:\n"
        << vramText(machine);

    // Das Dateisystem des Duplikats muss begehbar sein: CAT laeuft die Satzkette der
    // Datei DIRECTORY entlang — das gelingt nur, wenn die Sektorkontrollbloecke
    // mitkopiert wurden.  „OVR.PROG" ist der LETZTE Eintrag des Verzeichnisses, steht
    // also erst am Ende der Kette.
    typeString(machine, "cat d=1");
    typeKey(machine, QK_RETURN);
    EXPECT_TRUE(runSmallUntil(machine, "OVR.PROG", 100'000'000))
        << "CAT auf dem Duplikat kommt nicht bis zum letzten Verzeichniseintrag — "
           "Verkettung beim Kopieren verloren:\n" << vramText(machine);
    EXPECT_EQ(vramText(machine).find("ERROR CA"), std::string::npos)
        << "POINTER CHECK ERROR auf dem Duplikat:\n" << vramText(machine);

    machine.unmountDisk(0);
    machine.unmountDisk(1);
}

/**
 * @test UdosFormat/BuildsBootableSystemDiskAndBootsFromIt
 * @brief Der komplette „neue Systemdiskette bauen"-Workflow — von der **fabrikneuen
 *        Leerdiskette** bis zum Kaltstart von der selbstgebauten `.dmk`.
 *
 * Das UDOS-Gegenstück zu `ScpxInit.CreateFormatBThenPipCopyFromBootDisk` und
 * `tests/system/drivers/make_bootdisk.py`: alles, was eine Diskette zur Systemdiskette
 * macht, läuft im Emulator über den echten Schreibpfad — und zwar auf einem Medium,
 * das der Emulator selbst angelegt hat.
 *
 * 1. UDOS 4.3 von A: booten.
 * 2. B: ist eine **vom Floppy-Emulator angelegte Leerdiskette** (`createDisk` ohne
 *    Formatnamen: unformatiertes Medium in Laufwerksgeometrie 80×2), gebunden an eine
 *    temporäre **`.dmk`**-Datei.  Sie trägt beim Einlegen keine einzige Adressmarke —
 *    UDOS meldet sie beim Start folgerichtig als nicht initialisiert.
 * 3. B: **beidseitig** formatieren — Seite 0 (Laufwerk 1) mit `SYSTEMDISK? Y`, also
 *    inklusive Urlader (Spur 0/1/2) und Bootabbild (Spur 21), Seite 1 (Laufwerk 5)
 *    als reine Datenseite.  Auf einer Leerdiskette ist das der schärfere Fall: FORMAT
 *    muss die Spuren komplett neu schreiben, es gibt keine alte Struktur zum Aufsetzen.
 * 4. `STATUS` beweist, dass beide Seiten jetzt gültige, leere ZDOS-Datenträger sind
 *    (55 bzw. 14 belegte Sektoren).
 * 5. Alle Dateien mit `MOVE * S=… D=… P=&` hinüberkopieren (`P=&` ist nötig, sonst
 *    bleiben die als SECRET markierten Dateien liegen).
 * 6. `CAT` auf beiden Seiten der neuen Diskette.
 * 7. **Neue Maschine, die `.dmk` auf Laufwerk 0, Kaltstart** — sie muss UDOS 4.3
 *    hochfahren und sich selbst als `SYSDISK` / `SYSDISK.B` melden.  Damit hängt der
 *    komplette Lesepfad (Boot-ROM → SYL → OS) am DMK-Container.
 *
 * Sollwerte (doc/udos_diskettenformat.md §3/§4): frisch formatiert 14 belegte
 * Sektoren (Verzeichnis + Belegungskarte), als Systemseite zusätzlich 41 für Urlader
 * und Bootabbild → 55.
 */
TEST(UdosFormat, BuildsBootableSystemDiskAndBootsFromIt) {
    TempDisk a = legeSystemdiskette("udos_sysdisk_guard_A.hfe");
    TempDisk b = TempDisk::empty("udos_sysdisk_guard_B.dmk");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    {
        A5120Machine machine;
        ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();

        // ── 2. B: als fabrikneue Leerdiskette anlegen (Ziel-Container .dmk) ──
        ASSERT_TRUE(machine.createDisk(1, bPath, /*format_name=*/"", /*wp=*/false))
            << machine.lastError();
        ASSERT_EQ(machine.diskContainer(1), "dmk");
        ASSERT_TRUE(std::filesystem::exists(bPath));
        EXPECT_FALSE(machine.isDiskFormatted(1))
            << "frisch angelegte Diskette darf keine Adressmarke tragen";
        EXPECT_FALSE(machine.isDiskRawCompatible(1));
        EXPECT_EQ(machine.diskGeometry(1).num_cyls,  80u);
        EXPECT_EQ(machine.diskGeometry(1).num_heads,  2u);

        machine.powerOn();
        // Kein „beide Disketten eingelesen"-Nachweis: B: ist noch unformatiert.
        booteUdosMitLeerdiskette(machine);

        // ── 3. beide Seiten formatieren ─────────────────────────────────────
        formatiere(machine, "y", "1", "sysdisk");     // Seite 0 = Systemseite
        formatiere(machine, "n", "5", "sysdisk.b");   // Seite 1 = Datenseite

        // ── 4. Nachweis, dass aus der Leerdiskette ein Datentraeger wurde ────
        typeString(machine, "status");
        typeKey(machine, QK_RETURN);
        // Auf die LETZTE Zeile des Laufwerk-5-Blocks warten (1988 frei = 2002 - 14),
        // sonst trifft der Schnappschuss den Block mitten im Aufbau.
        ASSERT_TRUE(runSmallUntil(machine, "1988 SECTORS AVAILABLE", 150'000'000))
            << "Rueckseite wurde nicht leer formatiert:\n" << vramText(machine);
        const std::string leer = vramText(machine);
        ASSERT_NE(leer.find("DRIVE 5   SYSDISK.B"), std::string::npos)
            << "Rueckseite traegt nicht den neuen Datentraegernamen:\n" << leer;
        ASSERT_NE(leer.find("DRIVE 1   SYSDISK"), std::string::npos)
            << "Vorderseite wurde nicht formatiert:\n" << leer;
        EXPECT_NE(leer.find("55 SECTORS USED"), std::string::npos)
            << "Systemseite: erwartet 14 + 41 Systemsektoren belegt:\n" << leer;
        EXPECT_NE(leer.find("14 SECTORS USED"), std::string::npos)
            << "Datenseite: erwartet 14 belegte Sektoren:\n" << leer;
        EXPECT_TRUE(machine.isDiskFormatted(1));

        // ── 5. alles hinueberkopieren (P=& erfasst auch die SECRET-Dateien) ──
        typeString(machine, "move * s=0 d=1 p=&");
        typeKey(machine, QK_RETURN);
        runCycles(machine, 600'000'000);
        typeString(machine, "move * s=4 d=5 p=&");
        typeKey(machine, QK_RETURN);
        runCycles(machine, 600'000'000);

        // ── 6. Gegenprobe auf der neuen Diskette ────────────────────────────
        // „OVR.PROG" ist der letzte Verzeichniseintrag der Vorderseite, „HELP.DAT.04"
        // der letzte der Rueckseite — wer die sieht, ist die ganze Kette gelaufen.
        typeString(machine, "cat d=1 p=&");
        typeKey(machine, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(machine, "OVR.PROG", 200'000'000))
            << "Vorderseite der neuen Diskette unvollstaendig:\n" << vramText(machine);
        typeString(machine, "cat d=5 p=&");
        typeKey(machine, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(machine, "HELP.DAT.04", 200'000'000))
            << "Rueckseite der neuen Diskette unvollstaendig:\n" << vramText(machine);
        EXPECT_EQ(vramText(machine).find("ERROR C"), std::string::npos)
            << "Fehlermeldung beim Lesen der neuen Diskette:\n" << vramText(machine);

        // Der UDOS-Sektorkontrollblock hinter der Daten-CRC macht das Abbild
        // `.img`-untauglich — nur `.hfe`/`.dmk` koennen es verlustfrei halten.
        EXPECT_FALSE(machine.isDiskRawCompatible(1))
            << "UDOS-Systemdiskette wurde faelschlich als .img-tauglich gemeldet";

        machine.unmountDisk(0);
        machine.unmountDisk(1);   // flusht die neue Diskette in die .dmk
    }

    // ── 7. Von der selbstgebauten .dmk booten ───────────────────────────────
    {
        A5120Machine neu;
        ASSERT_TRUE(neu.mountDisk(0, bPath, "cpa780", /*wp=*/false)) << neu.lastError();
        ASSERT_EQ(neu.diskContainer(0), "dmk");
        neu.powerOn();

        ASSERT_TRUE(runSmallUntil(neu, "Neues Datum", 120'000'000))
            << "Die selbstgebaute .dmk-Systemdiskette bootet nicht:\n" << vramText(neu);
        typeString(neu, "150388");
        ASSERT_TRUE(runSmallUntil(neu, "UDOS BC.5120", 40'000'000))
            << "Kein UDOS-Prompt von der neuen .dmk-Diskette:\n" << vramText(neu);

        typeString(neu, "status");
        typeKey(neu, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(neu, "DRIVE 0   SYSDISK", 150'000'000))
            << "Das gebootete System erkennt seine eigene Diskette nicht:\n" << vramText(neu);
        // Auf die Rueckseiten-Zeile WARTEN statt sofort zu pruefen: STATUS baut den
        // Block Laufwerk fuer Laufwerk auf, und der Suchlauf laeuft ueber alle vier.
        EXPECT_TRUE(runSmallUntil(neu, "DRIVE 4   SYSDISK.B", 100'000'000))
            << "Rueckseite der neuen Diskette nicht eingelesen:\n" << vramText(neu);

        neu.unmountDisk(0);
    }
}

/**
 * @test UdosFormat/FormatsBrandNewBlankDiskette
 * @brief Der eigentliche Zweck des Medium-Umbaus (§8.7): eine **frisch angelegte,
 *        voellig unformatierte** Diskette laesst sich unter UDOS formatieren und ist
 *        danach ein brauchbarer ZDOS-Datentraeger.
 *
 * Vorher war das unmoeglich: `createDisk` musste eine bereits IBM-formatierte
 * Diskette anlegen (eine gap-leere `.hfe` liess den Controller haengen), und auf
 * einer solchen konnte UDOS seinen Sektorkontrollblock hinter der Daten-CRC nicht
 * unterbringen.  Jetzt ist die neue Diskette wirklich leer (keine Adressmarken,
 * Geometrie aus dem DriveProfile), der Controller streamt dafuer markenlosen
 * Gap-Flux, und FORMAT schreibt die Spuren komplett neu.
 *
 * Gegenprobe: `STATUS` meldet 1988 freie Sektoren (77x26 - 14 Systemsektoren) und
 * den frisch vergebenen Datentraegernamen; das Abbild ist anschliessend NICHT als
 * `.img` speicherbar, weil der UDOS-Anhang hinter der Daten-CRC dort verloren ginge.
 */
TEST(UdosFormat, FormatsBrandNewBlankDiskette) {
    TempDisk a = legeSystemdiskette("udos_blank_guard_A.hfe");
    TempDisk b = TempDisk::empty("udos_blank_guard_B.hfe");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();

    // LEERE Diskette anlegen: kein Formatname → unformatiertes Medium in
    // Laufwerksgeometrie (K5601 = 80x2), gebunden an eine .hfe.
    ASSERT_TRUE(machine.createDisk(1, bPath, /*format_name=*/"", /*wp=*/false))
        << machine.lastError();
    EXPECT_FALSE(machine.isDiskFormatted(1)) << "frische Diskette darf keine Marken tragen";
    EXPECT_FALSE(machine.isDiskRawCompatible(1));
    EXPECT_EQ(machine.diskGeometry(1).num_cyls, 80u);

    machine.powerOn();
    booteUdosMitLeerdiskette(machine);

    formatiere(machine, "n", "1", "blankdsk");
    const std::string nachFormat = vramText(machine);
    EXPECT_EQ(nachFormat.find("DEFEKTIVE TRACK"), std::string::npos)
        << "FORMAT meldete defekte Spuren auf der Leerdiskette:\n" << nachFormat;
    EXPECT_EQ(nachFormat.find("NOT FOR UDOS USEABLE"), std::string::npos)
        << "FORMAT verwarf die Leerdiskette:\n" << nachFormat;

    typeString(machine, "status");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "1988 SECTORS AVAILABLE", 100'000'000))
        << "STATUS meldet fuer die frisch formatierte Leerdiskette nicht 1988 freie "
           "Sektoren:\n" << vramText(machine);
    EXPECT_NE(vramText(machine).find("DRIVE 1   BLANKDSK"), std::string::npos)
        << "Laufwerk 1 traegt nicht den frisch vergebenen Datentraegernamen:\n"
        << vramText(machine);

    // Der UDOS-Sektorkontrollblock hinter der Daten-CRC macht das Abbild
    // unweigerlich `.img`-untauglich — genau dafuer gibt es das Flag.
    EXPECT_TRUE(machine.isDiskFormatted(1));
    EXPECT_FALSE(machine.isDiskRawCompatible(1))
        << "UDOS-Diskette wurde faelschlich als .img-tauglich gemeldet";

    machine.unmountDisk(0);
    machine.unmountDisk(1);
}

/**
 * @test UdosFormat/FormatsDisketteInsertedAtRuntime
 * @brief Laufwerk 1 ist beim Kaltstart LEER; die Diskette wird erst zur Laufzeit
 *        eingelegt und dann unter UDOS formatiert.
 *
 * Das ging vorher nicht: `/TRACK 00` war an eine eingelegte Diskette gekoppelt, ein
 * leeres Laufwerk meldete also nie seine Endlage und galt UDOS' Kaltstart-Suchlauf
 * als gar nicht vorhanden — `FORMAT` brach mit `ERROR C2` ab, ohne Laufwerk 1 auch
 * nur zu selektieren.  /TO ist laut K5122-Handbuch §4.1 (Tor B, B7) ein **Eingang
 * vom Laufwerk**, kein Diskettensignal.
 */
TEST(UdosFormat, FormatsDisketteInsertedAtRuntime) {
    TempDisk a = legeSystemdiskette("udos_runtime_A.hfe");
    TempDisk b = TempDisk::empty("udos_runtime_B.dmk");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", false)) << machine.lastError();
    machine.powerOn();
    booteUdosMitLeerdiskette(machine);        // Laufwerk 1 ist LEER

    // Erst jetzt die Diskette einlegen.
    ASSERT_TRUE(machine.createDisk(1, bPath, /*format_name=*/"", false)) << machine.lastError();
    ASSERT_FALSE(machine.isDiskFormatted(1));

    formatiere(machine, "n", "1", "latedisk");
    const std::string nachFormat = vramText(machine);
    ASSERT_EQ(nachFormat.find("ERROR C2"), std::string::npos)
        << "UDOS kennt das beim Kaltstart leere Laufwerk nicht:\n" << nachFormat;
    EXPECT_EQ(nachFormat.find("DEFEKTIVE TRACK"), std::string::npos) << nachFormat;
    EXPECT_EQ(nachFormat.find("NOT FOR UDOS USEABLE"), std::string::npos) << nachFormat;

    typeString(machine, "status");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "1988 SECTORS AVAILABLE", 100'000'000))
        << "Die zur Laufzeit eingelegte Diskette wurde nicht nutzbar formatiert:\n"
        << vramText(machine);
    EXPECT_NE(vramText(machine).find("DRIVE 1   LATEDISK"), std::string::npos)
        << vramText(machine);
    EXPECT_TRUE(machine.isDiskFormatted(1));

    machine.unmountDisk(0);
    machine.unmountDisk(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fremde Laufwerkstypen: `SET DISKCON` + FORMAT + MOVE + Kaltstart
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * @brief Ein zu pruefender UDOS-Laufwerkstyp (Sektorlaenge immer 128 = Ziffer 1).
 *
 * `diskcon` konfiguriert NUR Laufwerk 1 um — Laufwerk 0 bleibt `41`, dort liegt die
 * Bootdiskette.  `profil` ist das dazu passende physische Laufwerk am K5122-Slot 1
 * (@ref builtinDriveProfile); die Leerdiskette entsteht in dessen Geometrie.
 */
struct UdosLaufwerksTyp {
    const char* testname;       ///< Name im ctest-Eintrag
    const char* profil;         ///< DriveProfile fuer Slot 1 bzw. beim Kaltstart Slot 0
    const char* diskcon;        ///< Argument von `SET DISKCON=`
    uint8_t     cyls;           ///< erwartete Geometrie der Leerdiskette
    const char* frei;           ///< STATUS-Zeile nach dem Formatieren
    bool        passt_komplett; ///< reicht der Platz fuer ALLE Dateien der Quelle?
};

std::ostream& operator<<(std::ostream& os, const UdosLaufwerksTyp& t) {
    return os << t.testname;
}

class UdosLaufwerkstypen : public ::testing::TestWithParam<UdosLaufwerksTyp> {};

// `SET DISKCON=…` absetzen und die Ausfuehrung abwarten.
void setzeDiskcon(A5120Machine& m, const std::string& wert) {
    typeString(m, "set diskcon=" + wert);
    typeKey(m, QK_RETURN);
    runCycles(m, 30'000'000);
}

}  // namespace

/**
 * @test UdosLaufwerkstypen/BautBootfaehigeSystemdiskette
 * @brief Fuer jeden einseitigen UDOS-Laufwerkstyp: Leerdiskette → `SET DISKCON` →
 *        `FORMAT SYSTEMDISK? Y` → `MOVE` → **Kaltstart von der erzeugten Diskette**.
 *
 * Das UDOS-Gegenstueck zu `bootdisk_k5600_20_fmt1` (CP/A) und die Breitenpruefung zu
 * `BuildsBootableSystemDiskAndBootsFromIt` (das nur das Standardlaufwerk K5601 abdeckt).
 * Geprueft werden die drei Kombinationen, die nachweislich tragen:
 *
 * | `SET DISKCON` | UDOS-Typ | Laufwerk | Spuren | frei nach FORMAT |
 * |---|---|---|---|---|
 * | `41` | 5,25″ 80 Spuren SS | K5600.20 | 77 | 1947 = 77·26 − 55 |
 * | `31` | 5,25″ 40 Spuren    | K5600.10 | 40 |  985 = 40·26 − 55 |
 * | `41` | 5,25″ 80 Spuren SS | MF6400 (8″) | 77 | 1947 |
 *
 * Die dritte Zeile ist Absicht: sie faehrt den **8″-Laufwerkspfad** (77 Zylinder,
 * 360 min⁻¹) mit einem Typ-Nibble, das UDOS beherrscht.  Die 8″-eigenen Typen `11`/`21`
 * sind hier bewusst NICHT dabei — UDOS 4.3 schreibt fuer sie das Datenfeld ohne den
 * 4-Byte-Sektorkontrollblock, sodass die Verkettung auf der Zieldiskette fehlt
 * (`POINTER CHECK ERROR CA`).  Das ist Gastverhalten, kein Emulatorfehler: dieselbe
 * 5,25″-Hardware scheitert mit `SET DISKCON=21` genauso, waehrend das 8″-Laufwerk mit
 * `41` — also hier — sauber durchlaeuft.
 *
 * Bei 40 Spuren passen 985 freie Sektoren nicht fuer alle 1152 der Quelle; `MOVE` endet
 * dort regulaer mit `ERROR D3` (Diskette voll).  Bootfaehig ist die Diskette trotzdem,
 * und genau das prueft `passt_komplett == false`.
 */
TEST_P(UdosLaufwerkstypen, BautBootfaehigeSystemdiskette) {
    namespace fs = std::filesystem;
    const UdosLaufwerksTyp& typ = GetParam();

    TempDisk a = legeSystemdiskette(std::string("udos_typ_") + typ.testname + "_A.hfe");
    TempDisk b = TempDisk::empty(std::string("udos_typ_") + typ.testname + "_B.hfe");
    const std::string& aPath = a.path();
    const std::string& bPath = b.path();

    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    {   // ── Formatieren und befuellen ───────────────────────────────────────
        A5120Machine::Config cfg;
        cfg.drive_profiles = {"K5601", typ.profil, "K5601", "K5601"};
        A5120Machine machine(cfg);
        ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
        ASSERT_TRUE(machine.createDisk(1, bPath, /*format_name=*/"", /*wp=*/false))
            << machine.lastError();
        // Die Leerdiskette hat die Geometrie des LAUFWERKS, nicht die eines Katalogformats.
        EXPECT_EQ(machine.diskGeometry(1).num_cyls,  typ.cyls);
        EXPECT_EQ(machine.diskGeometry(1).num_heads, 1u)
            << "alle hier geprueften Laufwerke sind einseitig";
        EXPECT_FALSE(machine.isDiskFormatted(1));

        machine.powerOn();
        booteUdosMitLeerdiskette(machine);

        setzeDiskcon(machine, typ.diskcon);
        formatiere(machine, "y", "1", "sysdisk");     // Systemseite: Urlader + Bootabbild

        const std::string nachFormat = vramText(machine);
        ASSERT_EQ(nachFormat.find("DEFEKTIVE TRACK"), std::string::npos)
            << "FORMAT meldete defekte Spuren:\n" << nachFormat;
        ASSERT_EQ(nachFormat.find("NOT FOR UDOS USEABLE"), std::string::npos)
            << "FORMAT verwarf die Diskette:\n" << nachFormat;

        typeString(machine, "status");
        typeKey(machine, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(machine, typ.frei, 150'000'000))
            << "STATUS meldet nicht " << typ.frei << " (Spurzahl/Belegungskarte falsch?):\n"
            << vramText(machine);
        EXPECT_NE(vramText(machine).find("DRIVE 1   SYSDISK"), std::string::npos)
            << "Laufwerk 1 traegt nicht den frisch vergebenen Namen:\n" << vramText(machine);
        EXPECT_TRUE(machine.isDiskFormatted(1));

        // ── Alle Dateien hinueber (P=& erfasst auch die SECRET-Dateien) ─────
        // „OVR.PROG" ist der letzte Eintrag des Quellverzeichnisses — sobald MOVE ihn
        // auflistet, ist der Lauf durch (auch der auf 40 Spuren vorzeitig vollgelaufene).
        typeString(machine, "move * s=0 d=1 p=&");
        typeKey(machine, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(machine, "OVR.PROG", 1'500'000'000))
            << "MOVE kam nicht bis zum letzten Verzeichniseintrag:\n" << vramText(machine);
        runCycles(machine, 50'000'000);

        if (typ.passt_komplett) {
            EXPECT_EQ(vramText(machine).find("ERROR"), std::string::npos)
                << "MOVE meldete Fehler, obwohl der Platz reicht:\n" << vramText(machine);
            // Die Satzkette der neuen Diskette muss begehbar sein — das gelingt nur mit
            // mitkopierten Sektorkontrollbloecken (doc/udos_bug1.md).
            typeString(machine, "cat d=1 p=&");
            typeKey(machine, QK_RETURN);
            EXPECT_TRUE(runSmallUntil(machine, "OVR.PROG", 200'000'000))
                << "CAT auf der neuen Diskette kommt nicht bis zum letzten Eintrag:\n"
                << vramText(machine);
            EXPECT_EQ(vramText(machine).find("ERROR CA"), std::string::npos)
                << "POINTER CHECK auf der neuen Diskette:\n" << vramText(machine);
        }

        machine.unmountDisk(0);
        machine.unmountDisk(1);          // flusht die neue Diskette in die .hfe
    }

    {   // ── Kaltstart von der selbstgebauten Diskette ───────────────────────
        A5120Machine::Config cfg;
        cfg.drive_profiles = {typ.profil, "K5601", "K5601", "K5601"};
        A5120Machine neu(cfg);
        ASSERT_TRUE(neu.mountDisk(0, bPath, "cpa780", /*wp=*/false)) << neu.lastError();
        neu.powerOn();

        ASSERT_TRUE(runSmallUntil(neu, "Neues Datum", 200'000'000))
            << "Die selbstgebaute Systemdiskette bootet nicht:\n" << vramText(neu);
        typeString(neu, "150388");
        ASSERT_TRUE(runSmallUntil(neu, "UDOS BC.5120", 80'000'000))
            << "Kein UDOS-Prompt von der neuen Diskette:\n" << vramText(neu);

        typeString(neu, "status");
        typeKey(neu, QK_RETURN);
        EXPECT_TRUE(runSmallUntil(neu, "DRIVE 0   SYSDISK", 150'000'000))
            << "Das gebootete System erkennt seine eigene Diskette nicht:\n" << vramText(neu);

        neu.unmountDisk(0);
    }
}

INSTANTIATE_TEST_SUITE_P(
    Einseitig, UdosLaufwerkstypen,
    ::testing::Values(
        // Typ 4 — 5,25″, 80 Spuren, einseitig
        UdosLaufwerksTyp{"K5600_20", "K5600.20", "41 41 41 41", 80,
                         "1947 SECTORS AVAILABLE", true},
        // Typ 3 — 5,25″, 40 Spuren (FORMAT schaltet ueber `CP 30H` auf 40 Spuren um)
        UdosLaufwerksTyp{"K5600_10", "K5600.10", "41 31 41 41", 40,
                         "985 SECTORS AVAILABLE", false},
        // 8″-Laufwerk, von UDOS als 5,25″-80-Spur-Typ angesprochen (s. Testdoku)
        UdosLaufwerksTyp{"MF6400", "MF6400", "41 41 41 41", 77,
                         "1947 SECTORS AVAILABLE", true}),
    [](const ::testing::TestParamInfo<UdosLaufwerksTyp>& info) {
        return std::string(info.param.testname);
    });
