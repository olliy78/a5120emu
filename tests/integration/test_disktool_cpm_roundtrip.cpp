/**
 * @file test_disktool_cpm_roundtrip.cpp
 * @brief Kreuzprobe: das DiskTool schreibt — das ECHTE CP/A liest.
 *
 * Alle Unit-Tests des Dateisystems prüfen unsere Lesart gegen unsere eigene Lesart.
 * Diese Ebene prüft sie gegen das Betriebssystem, für das das Format gemacht wurde:
 * eine Datei wird mit @ref CpmFileSystem auf eine echte CP/A-Bootdiskette geschrieben,
 * danach bootet der Emulator diese Diskette und `TYPE` gibt den Inhalt aus.  Geht dabei
 * irgendetwas an Blockzuordnung, Extent-Nummer, Satzzahl oder Sektorversatz daneben,
 * fällt es hier auf — und nur hier.
 *
 * Die Gegenrichtung (CP/A schreibt, DiskTool liest) steht als zweiter Fall darunter.
 *
 * @see doc/design/13_k1520disktool.md §15
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::diskPath;
using k1520test::runSmallUntil;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

namespace {

constexpr int kBootBudget  = 90'000'000;
constexpr int kInputBudget = 40'000'000;

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

/// @brief Diskette öffnen und ein CP/M-Volume darüber aufsetzen (Lebensdauer gebündelt).
struct Volume {
    std::unique_ptr<DiskImage>     disk;
    std::unique_ptr<SectorSpace>   space;
    std::unique_ptr<CpmFileSystem> fs;
    std::string                    error;
    explicit operator bool() const { return fs != nullptr; }
};

Volume oeffne(const std::string& pfad, const char* fsname, bool schreibgeschuetzt) {
    Volume v;
    const FsProfile* p = dateisysteme().find(fsname);
    if (!p) { v.error = "Dateisystem '" + std::string(fsname) + "' unbekannt"; return v; }
    const DiskFormat* f = formate().find(p->format);
    if (!f) { v.error = "Format unbekannt"; return v; }

    const bool istImg = pfad.size() > 4 && pfad.compare(pfad.size() - 4, 4, ".img") == 0;
    std::optional<DiskFormat> of;
    if (istImg) of = *f;                        // nur .img braucht die Geometrie

    v.disk = DiskImage::open(pfad, of, schreibgeschuetzt);
    if (!v.disk) { v.error = "Abbild nicht ladbar"; return v; }
    v.space = std::make_unique<SectorSpace>(v.disk->medium(), *f);
    v.fs    = CpmFileSystem::mount(*v.space, *p, v.error);
    return v;
}

/// @brief Kommando am CP/A-Prompt eingeben und auf eine Ausgabe warten.
bool kommando(A5120Machine& m, const std::string& cmd, const std::string& erwartet) {
    typeString(m, cmd);
    typeKey(m, k1520test::QK_RETURN);
    return runSmallUntil(m, erwartet, kInputBudget);
}

}  // namespace

/**
 * @test DiskToolCpmRoundtrip/GeschriebeneDateiIstFuerCpaLesbar
 * @brief DiskTool schreibt PRUEF.TXT → CP/A bootet dieselbe Diskette und `TYPE` gibt sie aus.
 * @par Pass criterion  Der Inhalt steht nach `TYPE PRUEF.TXT` auf dem Bildschirm,
 *      und `DIR` listet die Datei.
 */
TEST(DiskToolCpmRoundtrip, GeschriebeneDateiIstFuerCpaLesbar) {
    TempDisk kopie("cpa_cpa780_k5601_noclock.img", "k1520_dt_roundtrip.img");

    // ── DiskTool schreibt ────────────────────────────────────────────────────
    const std::string inhalt = "DISKTOOL HAT DIESE ZEILE GESCHRIEBEN";
    {
        Volume v = oeffne(kopie.path(), "cpa780", /*wp=*/false);
        ASSERT_TRUE(v) << v.error;

        std::vector<uint8_t> daten(inhalt.begin(), inhalt.end());
        daten.push_back('\r');
        daten.push_back('\n');

        WriteOptions o;
        o.text = true;
        ASSERT_TRUE(v.fs->write("PRUEF.TXT", daten, o)) << v.fs->lastError();
        ASSERT_TRUE(v.disk->flush()) << v.disk->lastError();
    }

    // ── CP/A liest ───────────────────────────────────────────────────────────
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, kopie.path(), "cpa780", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kBootBudget))
        << "die beschriebene Diskette bootet nicht mehr — der Schreibpfad hat das "
           "Systemabbild beschaedigt";
    ASSERT_TRUE(runSmallUntil(machine, "A>", kInputBudget)) << "kein CCP-Prompt";

    ASSERT_TRUE(kommando(machine, "TYPE PRUEF.TXT", inhalt))
        << "CP/A findet die vom DiskTool geschriebene Datei nicht oder liest sie "
           "anders:\n" << vramText(machine);

    EXPECT_NE(vramText(machine).find(inhalt), std::string::npos);
}

/**
 * @test DiskToolCpmRoundtrip/CpaSiehtDieDateiImVerzeichnis
 * @brief `DIR` des laufenden CP/A listet die vom DiskTool angelegte Datei.
 */
TEST(DiskToolCpmRoundtrip, CpaSiehtDieDateiImVerzeichnis) {
    TempDisk kopie("cpa_cpa780_k5601_noclock.img", "k1520_dt_dir.img");
    {
        Volume v = oeffne(kopie.path(), "cpa780", false);
        ASSERT_TRUE(v) << v.error;
        std::vector<uint8_t> daten(300, 'X');
        ASSERT_TRUE(v.fs->write("NEUDATEI.BIN", daten, WriteOptions{})) << v.fs->lastError();
        ASSERT_TRUE(v.disk->flush());
    }

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, kopie.path(), "cpa780", false));
    machine.powerOn();
    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kBootBudget));
    ASSERT_TRUE(runSmallUntil(machine, "A>", kInputBudget));

    ASSERT_TRUE(kommando(machine, "DIR", "NEUDATEI"))
        << "DIR zeigt die neue Datei nicht:\n" << vramText(machine);
}

/**
 * @test DiskToolCpmRoundtrip/GeloeschteDateiIstFuerCpaVerschwunden
 * @brief Nach `erase()` findet CP/A die Datei nicht mehr.
 */
TEST(DiskToolCpmRoundtrip, GeloeschteDateiIstFuerCpaVerschwunden) {
    TempDisk kopie("cpa_cpa780_k5601_noclock.img", "k1520_dt_erase.img");
    {
        Volume v = oeffne(kopie.path(), "cpa780", false);
        ASSERT_TRUE(v) << v.error;
        ASSERT_TRUE(v.fs->erase("PIP.COM")) << v.fs->lastError();
        ASSERT_TRUE(v.disk->flush());
    }

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, kopie.path(), "cpa780", false));
    machine.powerOn();
    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kBootBudget));
    ASSERT_TRUE(runSmallUntil(machine, "A>", kInputBudget));

    // CP/A meldet eine unbekannte Datei mit dem Namen und einem Fragezeichen.
    ASSERT_TRUE(kommando(machine, "PIP", "PIP?"))
        << "PIP.COM ist noch da — erase() hat den Verzeichniseintrag nicht "
           "geloescht:\n" << vramText(machine);
}
