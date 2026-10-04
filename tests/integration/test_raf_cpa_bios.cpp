/**
 * @file test_raf_cpa_bios.cpp
 * @brief CP/A-BIOS mit eingebautem RAF-Treiber (doc/design/22_raf512.md §8 AP-R7): `M:` steht
 *        nach dem Kaltstart ohne Nachladen bereit.
 *
 * Diskette `cpa_cpa780_k5601_noclock-raf.img` — `cpa_cpa780_k5601_noclock.img` mit einem
 * `@OS.COM` aus `bios_org.mac` mit `raf = 1`, `rafpar = 1` (Software-Parität, 127 Sektoren
 * je Spur), `raf_d = 88H`, `raf_nb = 4` (Bau: tests/fixtures/README.md).  Das BIOS misst die
 * Karte selbst (`raftst`, §3.3), wählt den DPB nach der Kapazität und meldet im
 * Kaltstartblock „- RAF mit Parity als RAM-Floppy M:" samt Kapazitätszeile (Texte wörtlich
 * aus `bioscld3.mac`/`biosrafi.mac`).  Die drei Kartentypen prüfen die DPB-Wahl:
 * RAF128 → 8 Spuren/127 kByte (1-K-Blöcke), RAF512 → 32/508 (2 K), RAF-2M → 128/2032 (4 K).
 */
#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <string>

#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::runSmallUntilVisible;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramLines;
using k1520test::visibleText;

namespace {

constexpr long long kBootBudget = 90'000'000;
constexpr long long kFormat     = 600'000'000;    // nichtzerstörende Formatierung, RAF-2M
constexpr long long kBefehl     = 120'000'000;
const char*         kCpaRaf     = "cpa_cpa780_k5601_noclock-raf.img";

// Texte wörtlich aus bioscld3.mac / biosrafi.mac (rafpar = 1).
const char* kBlock   = "- RAF mit Parity als RAM-Floppy M:";
const char* kOhne    = "- RAF mit Parity als RAM-Floppy ??";
const char* kUndef   = "M: ist undefiniert, es folgt nichtzerstoerende Formatierung ...";
const char* kGeladen = "M: ist noch wie bei letzter Benutzung geladen!";

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

bool kommando(A5120Machine& m, const std::string& cmd) {
    typeString(m, cmd);
    typeKey(m, k1520test::QK_RETURN);
    return bisPrompt(m, kBefehl);
}

bool steht(A5120Machine& m, const std::string& s) {
    return k1520test::vramText(m).find(s) != std::string::npos;
}

/// Netz-Ein bis zum Prompt; die Formatierung von M: läuft VOR dem Prompt.
void kaltstart(A5120Machine& m) {
    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kFormat)) << vramLines(m);
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << vramLines(m);
}

/// Reset-Taste (systemweiter /RESET) bis zum Prompt; Schattenram vorher wischen.
void resetTaste(A5120Machine& m) {
    k1520test::wipeVram(m);
    m.reset();
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kFormat)) << vramLines(m);
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << vramLines(m);
}

/// Kennung `RFPvalid.SYS` (User 32) in Spur 0 der Karte.  Die Paritätsfassung liest mit
/// `IN D,(C)` / `DJNZ` ab B = 128: Byte 0 des Puffers liegt bei Index 0, Byte k ≥ 1 bei
/// Index 128 − k (§3.2, kein Autoinkrement).
bool kennungInSpur0(const RAF& raf) {
    auto byte = [&](uint32_t sek, uint32_t i) {
        return raf.peek(sek * 128 + (i == 0 ? 0 : 128 - i));
    };
    const std::string name = "RFPvalidSYS";
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
    Aufbau(const char* tmp, bool raf, RAF::Typ typ = RAF::Typ::RAF512)
        : disk(kCpaRaf, tmp), m(std::make_unique<A5120Machine>()) {
        if (raf) EXPECT_TRUE(m->installRaf(typ)) << m->rafFehler();
        EXPECT_TRUE(m->mountDisk(0, disk.path(), "cpa780", false)) << m->lastError();
    }
};

}  // namespace

class RafCpaBios : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test RafCpaBios.MStehtBereitUndUeberstehtReset
 * @brief RAF512: Kaltstart meldet M: mit 508 kByte / 32 Spuren und formatiert („undefiniert");
 *        `PIP M:=A:PIP.COM`, `DIR M:`.  Reset-Taste → „noch wie bei letzter Benutzung
 *        geladen!", die Datei ist noch da.  Netz-Ein → wieder „undefiniert", M: leer.
 */
TEST_F(RafCpaBios, MStehtBereitUndUeberstehtReset) {
    Aufbau x("k1520_raf_cpabios_m.img", true);
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    EXPECT_TRUE(steht(m, kBlock)) << vramLines(m);
    EXPECT_TRUE(steht(m, "  508 kByte (   32 Spuren zu 127 Sektoren)")) << vramLines(m);
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    EXPECT_FALSE(steht(m, kGeladen)) << vramLines(m);
    EXPECT_TRUE(kennungInSpur0(*m.raf()));
    EXPECT_TRUE(m.raf()->gesperrt()) << "Treiber sperrt nach jedem Zugriff (B = FFH)";

    ASSERT_TRUE(kommando(m, "PIP M:=A:PIP.COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: PIP      COM")) << vramLines(m);

    ASSERT_NO_FATAL_FAILURE(resetTaste(m));
    EXPECT_TRUE(steht(m, kGeladen)) << vramLines(m);
    EXPECT_FALSE(steht(m, kUndef)) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: PIP      COM")) << "nach RESET\n" << vramLines(m);

    ASSERT_NO_FATAL_FAILURE(kaltstart(m));   // Netz-Ein ohne Pufferung
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_FALSE(steht(m, "M: PIP      COM")) << "nach Netz-Ein\n" << vramLines(m);
}

/**
 * @test RafCpaBios.OhneKarteKeinM
 * @brief Dieselbe Diskette ohne gesteckte RAF: „RAM-Floppy ??" (das BIOS trägt M: aus der
 *        DPH-Tabelle aus), keine Formatierung, keine Kennungsmeldung.
 */
TEST_F(RafCpaBios, OhneKarteKeinM) {
    Aufbau x("k1520_raf_cpabios_ohne.img", false);
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    EXPECT_TRUE(steht(m, kOhne)) << vramLines(m);
    EXPECT_FALSE(steht(m, kBlock)) << vramLines(m);
    EXPECT_FALSE(steht(m, "ist undefiniert")) << vramLines(m);
    EXPECT_FALSE(steht(m, kGeladen)) << vramLines(m);
}

struct KapazitaetFall {
    RAF::Typ    typ;
    const char* name;
    const char* zeile;
};
void PrintTo(const KapazitaetFall& f, std::ostream* os) { *os << f.name; }

class RafCpaBiosKapazitaet : public RafCpaBios,
                             public ::testing::WithParamInterface<KapazitaetFall> {};

/**
 * @test RafCpaBiosKapazitaet.DpbNachKapazitaet
 * @brief Je Kartentyp die Kapazitätszeile des Kaltstarts (DPB-Wahl des BIOS, §3.3) und ein
 *        benutzbares M: (`PIP`, `DIR`).
 */
TEST_P(RafCpaBiosKapazitaet, DpbNachKapazitaet) {
    const auto& f = GetParam();
    Aufbau x((std::string("k1520_raf_cpabios_") + f.name + ".img").c_str(), true, f.typ);
    A5120Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    EXPECT_TRUE(steht(m, kBlock)) << vramLines(m);
    EXPECT_TRUE(steht(m, f.zeile)) << vramLines(m);
    EXPECT_TRUE(steht(m, kUndef)) << vramLines(m);
    ASSERT_TRUE(kommando(m, "PIP M:=A:PIP.COM")) << vramLines(m);
    ASSERT_TRUE(kommando(m, "DIR M:")) << vramLines(m);
    EXPECT_TRUE(steht(m, "M: PIP      COM")) << vramLines(m);
}

INSTANTIATE_TEST_SUITE_P(
    Typen, RafCpaBiosKapazitaet,
    ::testing::Values(
        KapazitaetFall{RAF::Typ::RAF128, "raf128", "  127 kByte (    8 Spuren zu 127 Sektoren)"},
        KapazitaetFall{RAF::Typ::RAF512, "raf512", "  508 kByte (   32 Spuren zu 127 Sektoren)"},
        KapazitaetFall{RAF::Typ::RAF2M, "raf2m", " 2032 kByte (  128 Spuren zu 127 Sektoren)"}),
    [](const ::testing::TestParamInfo<KapazitaetFall>& i) { return std::string(i.param.name); });
