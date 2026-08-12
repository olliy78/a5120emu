/**
 * @file test_disktool_neue_disketten.cpp
 * @brief Kreuzprobe auf **frisch angelegten** Disketten: DiskTool schreibt, CP/A liest.
 *
 * `test_disktool_cpm_roundtrip.cpp` prüft den Schreibpfad auf der **Referenz**diskette
 * `cpa780` — einer Diskette, deren Dateisystemprofil von Hand nachgemessen wurde.  Die
 * beiden Fälle hier prüfen genau das, was dort nicht drankommt: Disketten, deren
 * Dateisystem das Werkzeug **selbst ausrechnen** muss.
 *
 *  1. **192 Verzeichnisplätze** auf einer 800K-Datendiskette.  Der Wert stand bis
 *     2026-08-11 mit 128 im Katalog — geschätzt.  FORMAT.COM nennt im Menü selbst 192
 *     (doc/format.md §3.1, Wahl 0), und die nachgebildete BIOS-Regel rechnet dasselbe
 *     aus (@ref CpaDpbRule).  Beweisen kann das nur das echte CP/A: schreibt das
 *     Werkzeug in einen Platz jenseits von 128, muss der Gast die Datei trotzdem sehen.
 *  2. **Doppelschritt** (`step: 2`).  Die logische Spur `n` liegt physisch auf `2n`.
 *     Dass diese Abbildung in sich schlüssig ist, prüfen die Unit-Tests; dass sie mit
 *     der des Gastsystems übereinstimmt, kann nur der Gast sagen.
 *
 * Beide Fälle hängen eine **neu angelegte** Diskette als B: an eine echte Bootdiskette.
 *
 * @see doc/design/13_k1520disktool.md §15, doc/feature_requests/doppelschritt_disketten.md
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"
#include "tests/support/temp_path.h"

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

/// @brief Temporäre Datei, räumt sich weg.
class TempPfad {
public:
    explicit TempPfad(const char* name)
        : pfad_(k1520test::tempPath(name)) {}
    ~TempPfad() { std::error_code ec; std::filesystem::remove(pfad_, ec); }
    const std::string& get() const { return pfad_; }
private:
    std::string pfad_;
};

bool kommando(A5120Machine& m, const std::string& cmd, const std::string& erwartet) {
    typeString(m, cmd);
    typeKey(m, k1520test::QK_RETURN);
    return runSmallUntil(m, erwartet, kInputBudget);
}

/// @brief Frisch formatierte Diskette anlegen und mit dem Werkzeug beschreiben.
void legeAnUndBeschreibe(const std::string& pfad, const char* format_name,
                         const std::string& dateiname, const std::string& inhalt,
                         std::string* erkanntes_fs) {
    const DiskFormat* f = formate().find(format_name);
    ASSERT_NE(f, nullptr) << format_name;
    ASSERT_NE(DiskImage::create(pfad, *f, /*write_protect=*/false), nullptr);

    TempPfad quelle("k1520_dt_neue_quelle.txt");
    { std::ofstream(quelle.get(), std::ios::binary) << inhalt; }

    std::string err;
    auto dv = DiskVolume::open(pfad, "", formate(), dateisysteme(), err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_EQ(dv->detection().format, format_name);
    if (erkanntes_fs) *erkanntes_fs = dv->detection().filesystem;

    dv->setReadOnly(false);
    TransferOptions o;
    o.text = true;
    ASSERT_TRUE(dv->insert(quelle.get(), FileRef::parse(dateiname), o)) << dv->lastError();
    ASSERT_TRUE(dv->flush()) << dv->lastError();
}

}  // namespace

/**
 * @test DiskToolNeueDisketten/CpaFindetDateiJenseitsVonPlatz128
 * @brief Eine 800K-Datendiskette hat **192** Verzeichnisplätze — CP/A muss eine Datei
 *        finden, die auf Platz 130 liegt.
 * @par Pass criterion  `TYPE B:SPAET.TXT` gibt den Inhalt aus.
 * @par Warum           Mit den früher eingetragenen 128 Plätzen läge Platz 130 für das
 *                      Werkzeug ausserhalb des Verzeichnisses; es schriebe die Datei
 *                      entweder gar nicht oder in einen Datenblock.  Der Gast entscheidet.
 */
TEST(DiskToolNeueDisketten, CpaFindetDateiJenseitsVonPlatz128) {
    TempDisk boot("cpa_cpa780_k5601_noclock.img", "k1520_dtn_boot.img");
    TempPfad daten("k1520_dtn_800k.hfe");

    const std::string inhalt = "PLATZ 130 IST ERREICHBAR";
    const DiskFormat* f = formate().find("cpa800");
    ASSERT_NE(f, nullptr);
    ASSERT_NE(DiskImage::create(daten.get(), *f, /*write_protect=*/false), nullptr);

    TempPfad quelle("k1520_dtn_quelle.txt");
    { std::ofstream(quelle.get(), std::ios::binary) << inhalt; }

    {
        std::string err;
        auto dv = DiskVolume::open(daten.get(), "", formate(), dateisysteme(), err);
        ASSERT_NE(dv, nullptr) << err;
        dv->setReadOnly(false);

        // 130 Dateien anlegen: die letzte liegt zwangsläufig hinter Platz 128.
        TransferOptions o;
        o.text = true;
        for (int i = 0; i < 129; ++i)
            ASSERT_TRUE(dv->insert(quelle.get(),
                                   FileRef::parse("F" + std::to_string(i) + ".TXT"), o))
                << "Platz " << i << ": " << dv->lastError();
        ASSERT_TRUE(dv->insert(quelle.get(), FileRef::parse("SPAET.TXT"), o))
            << dv->lastError();
        ASSERT_EQ(dv->list().size(), 130u);
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, boot.path(), "cpa780", /*wp=*/false))
        << machine.lastError();
    ASSERT_TRUE(machine.mountDisk(1, daten.get(), "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kBootBudget));
    ASSERT_TRUE(runSmallUntil(machine, "A>", kInputBudget)) << "kein CCP-Prompt";

    ASSERT_TRUE(kommando(machine, "TYPE B:SPAET.TXT", inhalt))
        << "CP/A findet den Eintrag hinter Platz 128 nicht — dann sind es doch nicht "
           "192 Verzeichnisplaetze:\n" << vramText(machine);
}

/**
 * @test DiskToolNeueDisketten/CpaLiestDoppelschrittDiskette
 * @brief Eine vom Werkzeug angelegte **Doppelschritt**-Diskette (§3.4 Format U 0) wird
 *        vom echten CP/A gelesen.
 * @par Pass criterion  `DIR B:` listet die Datei und `TYPE B:DSTEP.TXT` gibt sie aus.
 * @par Warum           Das ist der einzige Nachweis, dass logische Spur `n` bei uns und
 *                      beim Gast auf demselben physischen Zylinder liegt — und dass die
 *                      Spurnummer im ID-Feld die logische ist.  Alles andere prüft nur
 *                      unsere Rechnung gegen sich selbst.
 */
TEST(DiskToolNeueDisketten, CpaLiestDoppelschrittDiskette) {
    TempDisk boot("cpa_cpa780_k5601_noclock.img", "k1520_dtn_dstep_boot.img");
    TempPfad daten("k1520_dtn_dstep.hfe");

    const std::string inhalt = "DOPPELSCHRITT GELESEN VON CPA";
    std::string erkannt;
    ASSERT_NO_FATAL_FAILURE(legeAnUndBeschreibe(daten.get(), "k5601_ss40_5x1024_dstep",
                                                "DSTEP.TXT", inhalt, &erkannt));

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, boot.path(), "cpa780", /*wp=*/false))
        << machine.lastError();
    ASSERT_TRUE(machine.mountDisk(1, daten.get(), "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kBootBudget));
    ASSERT_TRUE(runSmallUntil(machine, "A>", kInputBudget)) << "kein CCP-Prompt";

    ASSERT_TRUE(kommando(machine, "DIR B:", "DSTEP"))
        << "CP/A sieht die Doppelschritt-Diskette nicht (Dateisystem war '" << erkannt
        << "'):\n" << vramText(machine);
    ASSERT_TRUE(kommando(machine, "TYPE B:DSTEP.TXT", inhalt))
        << "die Spurabbildung stimmt nicht mit der des Gastsystems ueberein:\n"
        << vramText(machine);
}
