/**
 * @file test_disktool_udos_roundtrip.cpp
 * @brief Kreuzprobe: das DiskTool schreibt UDOS — das ECHTE UDOS liest.
 *
 * Die Unit-Tests prüfen unsere Lesart des UDOS-Dateisystems gegen die am laufenden
 * System gemessenen Sollwerte der Dokumentation.  Diese Ebene prüft den **Schreibpfad**
 * gegen das Betriebssystem selbst: eine Datei wird mit @ref UdosFileSystem auf die
 * Systemdiskette geschrieben, danach bootet der Emulator sie und `CAT` bzw. `PRINT`
 * müssen die Datei finden und ihren Inhalt zeigen.
 *
 * Trifft die Satzverkettung, der Kopfsektor, der Verzeichniseintrag oder die
 * Belegungskarte auch nur um ein Byte daneben, meldet UDOS `POINTER CHECK ERROR`,
 * `BAD POINTER IN OS` oder listet die Datei gar nicht erst — es gibt keinen Weg,
 * das hier zufällig zu bestehen.
 *
 * Laufzeit: ein vollständiger UDOS-Kaltstart je Fall (zweistellige Sekunden) →
 * Label `format_integration`, läuft in der schnellen Regression NICHT mit
 * (`tools/dev.sh test-format`).
 *
 * ⚠ Die UDOS-Konsole ist schreibungsinvertiert: KLEIN tippen ergibt GROSS auf dem
 * Bildschirm (doc/analyse_udos.md §14.2) — Kommandos hier also klein schreiben.
 *
 * @see doc/design/13_k1520disktool.md §15
 * @see doc/udos_diskettenformat.md §8.4
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/udos/udos_fs.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

namespace {

constexpr long long kVramCheckEvery = 50'000;

bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles) {
    return k1520test::runSmallUntil(m, needle, max_cycles, kVramCheckEvery);
}

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        FormatCatalog k = FormatCatalog::loadDefault(&f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        FsCatalog k = FsCatalog::loadDefault(formate(), &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

/// @brief Eine Seite einer UDOS-Diskette zum Schreiben oeffnen.
struct Seite {
    std::unique_ptr<DiskImage>      disk;
    std::unique_ptr<SectorSpace>    space;
    std::unique_ptr<UdosFileSystem> fs;
    std::string                     error;
    explicit operator bool() const { return fs != nullptr; }
};

Seite oeffne(const std::string& pfad, uint8_t head) {
    Seite s;
    const FsProfile* p = dateisysteme().find("udos_ds77");
    if (!p) { s.error = "Dateisystem udos_ds77 unbekannt"; return s; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { s.error = "Format unbekannt"; return s; }
    s.disk = DiskImage::open(pfad, std::nullopt, false);
    if (!s.disk) { s.error = "Abbild nicht ladbar"; return s; }
    s.space = std::make_unique<SectorSpace>(s.disk->medium(), *f, head);
    s.fs    = UdosFileSystem::mount(*s.space, *p, head, s.error);
    return s;
}

/// @brief UDOS booten und das Datum setzen; danach steht der `%`-Prompt.
void booteUdos(A5120Machine& m) {
    ASSERT_TRUE(runSmallUntil(m, "Neues Datum", 120'000'000))
        << "UDOS-Datumsabfrage nie erschienen:\n" << vramText(m);
    typeString(m, "150388");                // formatiertes Feld, kein ENTER noetig
    ASSERT_TRUE(runSmallUntil(m, "UDOS BC.5120", 40'000'000))
        << "UDOS-Prompt nie erreicht:\n" << vramText(m);
    ASSERT_EQ(vramText(m).find("DISK INITIALIZATION ERROR"), std::string::npos)
        << "UDOS konnte die Diskette nicht einlesen — der Schreibpfad hat sie "
           "beschaedigt:\n" << vramText(m);
    runCycles(m, 3'000'000);
}

/// @brief Kommando eintippen.
///
/// **Vorher auslaufen lassen.** Solange UDOS noch Ausgabe scrollt, gehen Anschlaege
/// verloren (der K7637 ist eine 9600-Baud-Strecke, die Annahme haengt am Timer-
/// Interrupt) — aus `print pruef.txt` wurde so `EF.TXT` und damit `NONEXISTENT
/// COMMAND`.  Die Ruhephase ist deshalb Teil des Vertrags, nicht Kosmetik.
void kommando(A5120Machine& m, const std::string& cmd, long long ruhe = 20'000'000) {
    runCycles(m, ruhe);
    typeString(m, cmd);
    typeKey(m, QK_RETURN);
}

}  // namespace

/**
 * @test DiskToolUdosRoundtrip/GeschriebeneDateiIstFuerUdosLesbar
 * @brief DiskTool schreibt eine Textdatei → UDOS listet sie mit `CAT` und gibt sie
 *        mit `PRINT` aus.
 * @par Pass criterion  Name erscheint in der `CAT`-Ausgabe, Inhalt in der `PRINT`-Ausgabe,
 *      und beim Einlesen der Diskette gibt es keinen `DISK INITIALIZATION ERROR`.
 */
TEST(DiskToolUdosRoundtrip, GeschriebeneDateiIstFuerUdosLesbar) {
    TempDisk kopie("udos_boot_scp.hfe", "k1520_dt_udos_rt.hfe");

    // ── DiskTool schreibt ────────────────────────────────────────────────────
    // Seite 0 ist das Systemlaufwerk 0, von dem UDOS bootet und Kommandos laedt.
    const std::string zeile = "DAS DISKTOOL HAT DIESE ZEILE GESCHRIEBEN.";
    {
        Seite s = oeffne(kopie.path(), 0);
        ASSERT_TRUE(s) << s.error;

        std::string text;
        for (int i = 0; i < 3; ++i) text += zeile + "\r";
        const std::vector<uint8_t> daten(text.begin(), text.end());

        WriteOptions o;
        o.text = true;
        o.date = "260810";
        ASSERT_TRUE(s.fs->write("PRUEF.TXT", daten, o)) << s.fs->lastError();
        ASSERT_TRUE(s.disk->flush()) << s.disk->lastError();
    }

    // ── UDOS liest ───────────────────────────────────────────────────────────
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, kopie.path(), "udos_ds77", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();
    booteUdos(machine);

    kommando(machine, "cat");
    ASSERT_TRUE(runSmallUntil(machine, "PRUEF.TXT", 150'000'000))
        << "UDOS listet die vom DiskTool angelegte Datei nicht:\n" << vramText(machine);

    // CAT laeuft ueber beide Laufwerke — erst zu Ende scrollen lassen.
    kommando(machine, "print pruef.txt", 60'000'000);
    ASSERT_TRUE(runSmallUntil(machine, zeile, 150'000'000))
        << "UDOS gibt den Inhalt nicht aus — Satzverkettung oder Kopfsektor stimmen "
           "nicht:\n" << vramText(machine);

    EXPECT_EQ(vramText(machine).find("POINTER"), std::string::npos)
        << "UDOS meldet einen Zeigerfehler:\n" << vramText(machine);
}

/**
 * @test DiskToolUdosRoundtrip/GeloeschteDateiIstFuerUdosVerschwunden
 * @brief Nach `erase()` listet `CAT` die Datei nicht mehr, und der freie Platz stimmt.
 */
TEST(DiskToolUdosRoundtrip, GeloeschteDateiIstFuerUdosVerschwunden) {
    TempDisk kopie("udos_boot_scp.hfe", "k1520_dt_udos_del.hfe");

    // COMPARE ist eine gewoehnliche Datei der Systemdiskette (Typ P) — sie wird
    // vom Bootvorgang nicht gebraucht und eignet sich deshalb als Loeschopfer.
    {
        Seite s = oeffne(kopie.path(), 0);
        ASSERT_TRUE(s) << s.error;
        const int frei_vorher = s.fs->bitmap().countFree();
        ASSERT_TRUE(s.fs->erase("COMPARE")) << s.fs->lastError();
        EXPECT_GT(s.fs->bitmap().countFree(), frei_vorher);
        ASSERT_TRUE(s.disk->flush());
    }

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, kopie.path(), "udos_ds77", false));
    machine.powerOn();
    booteUdos(machine);

    // `STATUS` liest die Belegungskarte des laufenden Systems — sie muss zu unserer
    // Rechnung passen, sonst haetten wir die Karte falsch nachgefuehrt.
    kommando(machine, "status");
    ASSERT_TRUE(runSmallUntil(machine, "SECTORS AVAILABLE", 150'000'000))
        << vramText(machine);
    // COMPARE ist 1024 B = 8 Saetze a 128 B PLUS ihr Kopfsektor = 9 Sektoren.
    // 850 + 9 = 859 — und genau das zaehlt UDOS aus der von uns geschriebenen Karte.
    EXPECT_NE(vramText(machine).find("859 SECTORS AVAILABLE"), std::string::npos)
        << "UDOS zaehlt einen anderen freien Platz als wir eingetragen haben:\n"
        << vramText(machine);

    kommando(machine, "cat");
    ASSERT_TRUE(runSmallUntil(machine, "COPY.DISK", 150'000'000)) << vramText(machine);
    EXPECT_EQ(vramText(machine).find("COMPARE"), std::string::npos)
        << "die geloeschte Datei steht noch im Verzeichnis:\n" << vramText(machine);
}
