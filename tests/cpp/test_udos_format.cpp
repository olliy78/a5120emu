/**
 * @file test_udos_format.cpp
 * @brief Langsamer End-to-End-Regressionswächter: UDOS 4.3 FORMAT formatiert
 *        Laufwerk 1 zu einer benutzbaren ZDOS-Diskette.
 *
 * Eigenes Executable mit ctest-Label „format_integration" (wie test_scpx_init.cpp),
 * damit der Test aus dem Default-`tools/dev.sh test` herausfällt — er bootet UDOS,
 * formatiert 77 Spuren und lässt sie von UDOS selbst verifizieren (~15 s).
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

void runCycles(A5120Machine& m, long long cycles) {
    for (long long done = 0; done < cycles; done += 5000) m.run(5000);
}

// In kleinen Batches laufen (Tastatur-/Timer-ISR-Timing) bis @p needle im VRAM steht.
bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long done = 0; done < max_cycles; done += 50'000) {
        runCycles(m, 50'000);
        if (vramText(m).find(needle) != std::string::npos) return true;
    }
    return false;
}

constexpr uint32_t QK_RETURN = 0x01000004;

void typeKey(A5120Machine& m, uint32_t kc) {
    m.keyPress(kc, false, false);
    runCycles(m, 1'000'000);
    m.keyRelease(kc);
    runCycles(m, 300'000);
}

// ⚠ UDOS-Konsole ist schreibungsinvertiert: KLEIN tippen ergibt GROSS auf dem
// Bildschirm (doc/analyse_udos.md §14.2) — Kommandos hier also klein schreiben.
void typeString(A5120Machine& m, const std::string& s) {
    for (char c : s) typeKey(m, static_cast<uint8_t>(c));
}

// Auf einen Prompt warten und die Antwort tippen.
void antworte(A5120Machine& m, const std::string& prompt, const std::string& antwort,
              long long budget = 30'000'000) {
    ASSERT_TRUE(runSmallUntil(m, prompt, budget))
        << "Prompt '" << prompt << "' nie erschienen:\n" << vramText(m);
    typeString(m, antwort);
    typeKey(m, QK_RETURN);
}

// A: und B: als beschreibbare Kopien der UDOS-Bootdiskette anlegen.
void legeDisketten(const std::string& aPath, const std::string& bPath) {
    namespace fs = std::filesystem;
    fs::copy_file(diskPath("udos_boot_scp.hfe"), aPath, fs::copy_options::overwrite_existing);
    fs::copy_file(diskPath("udos_boot_scp.hfe"), bPath, fs::copy_options::overwrite_existing);
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

// `FORMAT` auf Laufwerk 1 fahren (Datentraegername TESTDISK, keine Systemdiskette).
void formatiereLaufwerk1(A5120Machine& m) {
    typeString(m, "format");
    typeKey(m, QK_RETURN);
    antworte(m, "SYSTEMDISK", "n");
    antworte(m, "DRIVE",      "1");
    antworte(m, "ID?",        "testdisk");
    antworte(m, "READY",      "y");
    runCycles(m, 400'000'000);              // 77 Spuren formatieren + verifizieren
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
    namespace fs = std::filesystem;
    const std::string aPath = (fs::temp_directory_path() / "udos_format_guard_A.hfe").string();
    const std::string bPath = (fs::temp_directory_path() / "udos_format_guard_B.hfe").string();
    legeDisketten(aPath, bPath);

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
    std::error_code ec;
    fs::remove(aPath, ec);
    fs::remove(bPath, ec);
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
    namespace fs = std::filesystem;
    const std::string aPath = (fs::temp_directory_path() / "udos_copydisk_guard_A.hfe").string();
    const std::string bPath = (fs::temp_directory_path() / "udos_copydisk_guard_B.hfe").string();
    legeDisketten(aPath, bPath);

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
    std::error_code ec;
    fs::remove(aPath, ec);
    fs::remove(bPath, ec);
}
