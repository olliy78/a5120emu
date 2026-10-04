/**
 * @file test_raf_a5120.cpp
 * @brief Gastnachweis RAF am A5120 und A5120.16 (doc/design/22_raf512.md §8 AP-R5): die
 *        Original-Treiber `RAFCPM.COM` (Laufwerk M:, 11.10.87) und `RAF512.COM`
 *        (Laufwerk P:, DKt 26.07.08) unter CP/A gegen die Nachbildung der Karte.
 *
 * Die Treiber kennen den Emulator nicht: Kapazitätstest (`raftst`, §3.3), DPB-Wahl,
 * `RAFvalid.SYS`-Kennung und die BIOS-Einbindung laufen so, wie sie am Gerät liefen.
 * Geprüft wird über das, was der Treiber selbst auf den Schirm schreibt (Texte wörtlich
 * aus `RAFCPM.COM`/`RAF512.COM`), und über `PIP`/`DIR` des laufenden CP/A.
 *
 * Disketten: `cpa_cpa780_k5601_noclock.img` (BIOS ohne EM) bzw. `…-em256.img` (BIOS mit
 * `em256 = 1`, M: im EM) — als TempDisk, die Treiber legt `DiskVolume::insert` darauf.
 */
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::runSmallUntil;
using k1520test::runSmallUntilVisible;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramLines;
using k1520test::visibleText;

namespace {

constexpr long long kBootBudget  = 90'000'000;
constexpr long long kBefehl      = 120'000'000;   // je Kommando bis zum nächsten `A>`
const char*         kCpa         = "cpa_cpa780_k5601_noclock.img";
const char*         kCpaEm256    = "cpa_cpa780_k5601_noclock-em256.img";

// Texte wörtlich aus RAFCPM.COM / RAF512.COM.
const char* kKapazitaet = "RAF-Gesamtkapazitaet 512K Bytes (32 Spuren zu 128 Sektoren)";
const char* kUndef      = "RAF ist undefiniert, es folgt Loeschen Directory";
const char* kGeladen    = "RAF ist noch wie bei letzter Benutzung geladen!";
const char* kKeine      = "Keine RAF-Karte vorhanden!";

/// Treiber @p datei aus tests/fixtures/raf/ auf die CP/A-Diskette @p pfad legen.
::testing::AssertionResult treiberAufDiskette(const std::string& pfad, const std::string& datei) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(pfad, "cpa780", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    FileRef ref;
    ref.name = datei;
    if (!vol->insert(std::string(RAF_FIXTURE_DIR) + "/" + datei, ref, TransferOptions{}))
        return ::testing::AssertionFailure() << vol->lastError();
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

/// Letzte nicht leere SICHTBARE Bildzeile, rechts gestutzt.
std::string letzteZeile(A5120Machine& m) {
    const std::string t = visibleText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        zeile.erase(zeile.find_last_not_of(' ') + 1);
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Bis die letzte Zeile `A>` ist und 1 Mio. Takte so bleibt (Ausgabe fertig).
bool bisPrompt(A5120Machine& m, long long frist) {
    long long ruhig = 0;
    for (long long d = 0; d < frist; d += 100'000) {
        k1520test::runCycles(m, 100'000);
        ruhig = (letzteZeile(m) == "A>") ? ruhig + 100'000 : 0;
        if (ruhig >= 1'000'000) return true;
    }
    return false;
}

/// Kommando tippen, RETURN, bis zum nächsten `A>` — die getippte Zeile `A>KOMMANDO`
/// ist selbst nicht `A>`, ein alter Prompt zählt also nicht.
bool kommando(A5120Machine& m, const std::string& cmd) {
    typeString(m, cmd);
    typeKey(m, k1520test::QK_RETURN);
    return bisPrompt(m, kBefehl);
}

bool steht(A5120Machine& m, const std::string& s) {
    return k1520test::vramText(m).find(s) != std::string::npos;
}

/// Netz-Ein bis zum Prompt (Diskette steckt schon).
void kaltstart(A5120Machine& m) {
    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << vramLines(m);
}

/// Reset-Taste (systemweiter /RESET) bis zum Prompt; Schattenram vorher wischen
/// (k1520test::wipeVram, Wächter KbdNachReset.*).
void resetTaste(A5120Machine& m) {
    k1520test::wipeVram(m);
    m.reset();
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << vramLines(m);
}

/// Verzeichniseintrag `RAFvalid.SYS` (User 32 = 20H) in Spur 0 der Karte.  Die Karte
/// zählt nicht selbst weiter, `OTIR`/`INIR` laufen mit B rückwärts (§3.2): Byte i eines
/// Sektors, wie das BIOS ihn sieht, liegt in der Karte bei Sektor × 128 + 127 − i.
bool kennungInSpur0(const RAF& raf) {
    auto byte = [&](uint32_t sektor, uint32_t i) { return raf.peek(sektor * 128 + 127 - i); };
    const std::string name = "RAFvalidSYS";
    for (uint32_t sek = 0; sek < 128; ++sek)
        for (uint32_t e = 0; e < 128; e += 32) {
            if (byte(sek, e) != 0x20) continue;
            bool gleich = true;
            for (size_t i = 0; i < name.size() && gleich; ++i)
                gleich = (byte(sek, e + 1 + uint32_t(i)) & 0x7F) == uint8_t(name[i]);
            if (gleich) return true;
        }
    return false;
}

struct Aufbau {
    TempDisk                      disk;
    std::unique_ptr<A5120Machine> m;
    Aufbau(const char* fixture, const char* tmp, const A5120Machine::Config& cfg, bool raf,
           const char* treiber)
        : disk(fixture, tmp), m(std::make_unique<A5120Machine>(cfg)) {
        EXPECT_TRUE(treiberAufDiskette(disk.path(), treiber));
        if (raf) EXPECT_TRUE(m->installRaf(RAF::Typ::RAF512)) << m->rafFehler();
        EXPECT_TRUE(m->mountDisk(0, disk.path(), "cpa780", false)) << m->lastError();
    }
};

}  // namespace

class RafA5120 : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test RafA5120.RafcpmLegtMAnUndUeberstehtReset
 * @brief `RAFCPM` auf der RAF512: 512 K / 32 Spuren, „undefiniert … Loeschen Directory",
 *        M: installiert; `PIP M:=A:P*.COM`, `DIR M:` sieht beide Dateien.  Reset-Taste →
 *        `RAFCPM` erneut → „noch wie bei letzter Benutzung geladen", Dateien da.  Netz-Ein
 *        → wieder „undefiniert", M: leer.
 */
TEST_F(RafA5120, RafcpmLegtMAnUndUeberstehtReset) {
    Aufbau x(kCpa, "k1520_raf_a5120_m.img", {}, true, "RAFCPM.COM");
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));

    ASSERT_TRUE(kommando(m, "RAFCPM")) << vramLines(m);
    EXPECT_TRUE(steht(m, "Nachladbare RAF-Installation, Version 11.10.87 ohne Parity"))
        << vramLines(m);
    EXPECT_TRUE(steht(m, kKapazitaet)) << vramLines(m);
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    EXPECT_TRUE(steht(m, "RAF als Laufwerk M: installiert")) << vramLines(m);
    EXPECT_FALSE(steht(m, kKeine)) << vramLines(m);

    ASSERT_TRUE(kommando(m, "PIP M:=A:P*.COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: PIP      COM")) << vramLines(m);
    EXPECT_TRUE(steht(m, "POWER    COM")) << vramLines(m);
    // Die Kennung `RAFvalid.SYS` (User 32) steht im Verzeichnis in Spur 0 — in der Karte.
    EXPECT_TRUE(kennungInSpur0(*m.raf()));
    EXPECT_TRUE(m.raf()->gesperrt()) << "Treiber sperrt nach jedem Zugriff (B = FFH)";

    ASSERT_NO_FATAL_FAILURE(resetTaste(m));
    ASSERT_TRUE(kommando(m, "RAFCPM")) << vramLines(m);
    EXPECT_TRUE(steht(m, kGeladen)) << vramLines(m);
    EXPECT_FALSE(steht(m, kUndef)) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: PIP      COM")) << "nach RESET\n" << vramLines(m);
    EXPECT_TRUE(steht(m, "POWER    COM")) << vramLines(m);

    ASSERT_NO_FATAL_FAILURE(kaltstart(m));   // Netz-Ein ohne Pufferung
    ASSERT_TRUE(kommando(m, "RAFCPM")) << vramLines(m);
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_FALSE(steht(m, "M: PIP      COM")) << "nach Netz-Ein\n" << vramLines(m);
}

/**
 * @test RafA5120.RafcpmOhneKarte
 * @brief Ohne gesteckte RAF: „Keine RAF-Karte vorhanden!", zurück am Prompt.
 */
TEST_F(RafA5120, RafcpmOhneKarte) {
    Aufbau x(kCpa, "k1520_raf_a5120_ohne.img", {}, false, "RAFCPM.COM");
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    ASSERT_TRUE(kommando(m, "RAFCPM")) << vramLines(m);
    EXPECT_TRUE(steht(m, kKeine)) << vramLines(m);
    EXPECT_FALSE(steht(m, "installiert")) << vramLines(m);
}

/**
 * @test RafA5120.A5120_16Em256UndRaf512Nebeneinander
 * @brief A5120.16 (EM256) + RAF512, BIOS mit `em256 = 1`: das BIOS legt M: im EM an,
 *        `RAF512` die RAF daneben als P:.  Je eine Datei auf M: und P:, beide `DIR`
 *        sehen ihre Datei und nur die eigene.
 */
TEST_F(RafA5120, A5120_16Em256UndRaf512Nebeneinander) {
    A5120Machine::Config cfg;
    cfg.em = A5120Machine::Config::Em::em256;
    Aufbau x(kCpaEm256, "k1520_raf_a5120_em.img", cfg, true, "RAF512.COM");
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    EXPECT_TRUE(steht(m, "Erweiterungsmodul als RAM-Floppy M: mit 256 kByte")) << vramLines(m);

    ASSERT_TRUE(kommando(m, "RAF512")) << vramLines(m);
    EXPECT_TRUE(steht(m, "Nachladbare RAF-Installation, (DKt) v.26.07.08 ohne Parity"))
        << vramLines(m);
    EXPECT_TRUE(steht(m, kKapazitaet)) << vramLines(m);
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    EXPECT_TRUE(steht(m, "RAF als Laufwerk P: installiert")) << vramLines(m);

    ASSERT_TRUE(kommando(m, "PIP P:=A:PIP.COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "PIP M:=A:POWER.COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR P:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "P: PIP      COM")) << vramLines(m);
    EXPECT_FALSE(steht(m, "P: POWER    COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: POWER    COM")) << vramLines(m);
    EXPECT_FALSE(steht(m, "M: PIP      COM")) << vramLines(m);
}
