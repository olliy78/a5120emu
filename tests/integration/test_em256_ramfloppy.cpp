/**
 * @file test_em256_ramfloppy.cpp
 * @brief Abnahme S1 (doc/design/17_a5120_16.md §3): Original-CP/A-BIOS mit `em256 equ 1`
 *        erkennt das EM256 und benutzt die RAM-Floppy M:.
 *
 * Fixture `cpa_cpa780_k5601_noclock-em256.img`: @OS.COM aus `bios_org.mac` der
 * CPA-Workbench (em256 = 1, em256adr = 4000H, modadr = A8H), nur Laufwerke/Uhr/
 * Kaltstartkommando an die Testmaschine angepasst (tests/fixtures/README.md).
 * Die EM-Routinen (`biosremc.mac` emina, `biosrem.mac` ramon/ramoff) sind original —
 * Software, die am echten A5120.16 lief, ist damit der unabhängige Beleg der 8-Bit-Seite.
 */
#include <gtest/gtest.h>

#include <memory>
#include <optional>
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

using k1520test::runSmallUntil;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

namespace {

constexpr int kBootBudget  = 90'000'000;
constexpr int kInputBudget = 40'000'000;
const char*   kFixture     = "cpa_cpa780_k5601_noclock-em256.img";

A5120Machine::Config mitEm(A5120Machine::Config::Em em) {
    A5120Machine::Config c;
    c.em = em;
    return c;
}

bool kommando(A5120Machine& m, const std::string& cmd, const std::string& erwartet) {
    typeString(m, cmd);
    typeKey(m, k1520test::QK_RETURN);
    return runSmallUntil(m, erwartet, kInputBudget);
}

/// Bildschirm löschen lassen wäre BIOS-abhängig — stattdessen auf eine Marke warten,
/// die erst NACH dem Kommando entstehen kann: den nächsten Prompt hinter der Ausgabe.
bool kommandoBisPrompt(A5120Machine& m, const std::string& cmd, const std::string& erwartet) {
    return kommando(m, cmd, erwartet) && runSmallUntil(m, "A>", kInputBudget);
}

std::vector<uint8_t> dateiVonDiskette(const std::string& pfad, const std::string& name) {
    std::string f;
    static FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static FsCatalog fs = FsCatalog::loadDefault(fk, &f);
    const FsProfile* p = fs.find("cpa780");
    const DiskFormat* df = fk.find(p->format);
    auto disk  = DiskImage::open(pfad, std::optional<DiskFormat>(*df), true);
    if (!disk) return {};
    SectorSpace space(disk->medium(), *df);
    std::string err;
    auto vol = CpmFileSystem::mount(space, *p, err);
    std::vector<uint8_t> out;
    if (vol) vol->read(name, out);
    return out;
}

}  // namespace

/**
 * @test Em256RamFloppy/KaltstartErkenntKarteUndBenutztM
 * @brief Kaltstart meldet „16-Bit Erweiterungsmodul als RAM-Floppy M: mit 256 kByte";
 *        M: ist frisch angelegt, PIP hin und zurück liefert die Datei bitgleich, und
 *        nach der Reset-Taste findet das BIOS seine Kennung wieder (M: bleibt).
 */
TEST(Em256RamFloppy, KaltstartErkenntKarteUndBenutztM) {
    TempDisk a(kFixture, "k1520_em256_a.img");
    A5120Machine m(mitEm(A5120Machine::Config::Em::em256));
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa780", false)) << m.lastError();
    m.powerOn();

    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget)) << vramText(m);
    EXPECT_NE(vramText(m).find("Erweiterungsmodul als RAM-Floppy M: mit 256 kByte"),
              std::string::npos) << vramText(m);

    // Kennungseintrag (User 31) im Verzeichnis von M: — steht im EM-DRAM, Segment 0.
    const EM* em = m.em();
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(em->peek(0), 0x1F);
    EXPECT_EQ(em->peek(1), '@');

    ASSERT_TRUE(kommandoBisPrompt(m, "PIP M:=A:PIP.COM", "PIP M:=A:PIP.COM")) << vramText(m);
    ASSERT_TRUE(kommando(m, "DIR M:", "M: PIP      COM")) << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));
    ASSERT_TRUE(kommandoBisPrompt(m, "PIP A:ZURUECK.COM=M:PIP.COM", "ZURUECK.COM=M:PIP.COM"))
        << vramText(m);
    ASSERT_TRUE(kommando(m, "DIR A:ZURUECK.COM", "A: ZURUECK  COM")) << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));

    // Reset-Taste: das BIOS findet seine Kennung und legt M: NICHT neu an.
    // Schirm vorher wischen — sonst stünde der alte Prompt noch im Schattenram
    // (k1520test::wipeVram, Wächter KbdNachReset.*).
    // Nach der Reset-Taste meldet sich CP/A ohne Banner/RAM-Test, nur mit `A>` —
    // gewartet wird auf den SICHTBAREN Prompt, nicht auf die Statuszeile (die steht
    // schon vor der Tastaturinitialisierung, s. KbdNachReset.*).
    k1520test::wipeVram(m);
    m.reset();
    ASSERT_TRUE(k1520test::runSmallUntilVisible(m, "A>", kBootBudget)) << vramText(m);
    ASSERT_TRUE(kommando(m, "DIR M:", "M: PIP      COM"))
        << "M: nach der Reset-Taste leer — Kennung nicht wiedergefunden:\n" << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));

    ASSERT_TRUE(m.flushDisks());
    const auto original = dateiVonDiskette(a.path(), "PIP.COM");
    const auto zurueck  = dateiVonDiskette(a.path(), "ZURUECK.COM");
    ASSERT_FALSE(original.empty());
    EXPECT_EQ(original, zurueck) << "PIP A:→M:→A: hat die Datei verändert";
}

/**
 * @test Em256RamFloppy/OhneKarteMeldetBiosFragezeichen
 * @brief Dasselbe BIOS ohne EM: Erkennung schlägt fehl, Kaltstart zeigt „??? kByte",
 *        das System läuft trotzdem bis zum Prompt.
 */
TEST(Em256RamFloppy, OhneKarteMeldetBiosFragezeichen) {
    TempDisk a(kFixture, "k1520_em256_ohne.img");
    A5120Machine m;
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa780", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramText(m);
    ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));
    EXPECT_NE(vramText(m).find("RAM-Floppy ?? mit ??? kByte"), std::string::npos)
        << vramText(m);
}
