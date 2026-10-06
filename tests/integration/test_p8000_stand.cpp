/**
 * @file test_p8000_stand.cpp
 * @brief P8000 Save-State P8KS v1 (doc/design/25_p8000.md §10.2, AP P7e).
 *
 * Wächter `P8000Stand.RundreiseIstBitgleich`: Lauf bis mitten ins Laden des Betriebssystems
 * (UA858 hält die CPU, U8272 liest, Terminal und SIO sind in Bewegung), speichern, in eine
 * zweite Maschine mit demselben Abbild laden, beide N Takte weiterlaufen lassen — der Zustand
 * beider ist danach Byte für Byte derselbe (`stateBytes()` umfasst alle Speicher, CPU, Bausteine,
 * Terminal; zusätzlich Speicher in U880-Sicht und Bild).
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>
#include <string>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"
#include "tests/support/temp_path.h"

using namespace k1520test::p8000;

namespace {
constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";

struct Rechner {
    k1520test::TempDisk disk{FIXTURE};
    P8000Machine m;
    Rechner() {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        EXPECT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
        m.powerOn();
    }
};

/// Hardwaretest → `>` → BOOT, dann bis die UA858 den Bus hält (Betriebssystem wird geladen).
void bisMittenImLaden(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippe(m, "\r");
    for (long long t = 0; t < 30'000'000; t += kBatch) {
        m.run(int(kBatch));
        if (m.floppy8().dma().busRequest()) return;
    }
    FAIL() << "UA858 hielt den Bus nie — Laden nicht erreicht\n" << bild(m);
}

std::string ersterUnterschied(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return "Länge " + std::to_string(a.size()) + " ≠ " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return "erstes abweichendes Byte bei Versatz " + std::to_string(i);
    return "gleich";
}
}  // namespace

TEST(P8000Stand, RundreiseIstBitgleich) {
    Rechner a, b;
    ASSERT_NO_FATAL_FAILURE(bisMittenImLaden(a.m));

    const std::vector<uint8_t> stand = a.m.stateBytes();
    ASSERT_GT(stand.size(), 70'000u);   // 64 K DRAM + Bausteine
    ASSERT_NE(b.m.stateBytes(), stand) << "frische Maschine darf nicht schon dem Stand gleichen";
    ASSERT_TRUE(b.m.restoreStateBytes(stand)) << b.m.stateError();
    EXPECT_EQ(b.m.stateBytes(), stand) << "Laden ist nicht die Umkehrung des Speicherns: "
                                       << ersterUnterschied(b.m.stateBytes(), stand);
    EXPECT_EQ(b.m.cpuPC(), a.m.cpuPC());

    // Beide weiter; mehrere Mio. Takte decken die ganze Ladephase ab (DMA, FDC, Interrupts, Terminal).
    for (int runde = 0; runde < 3; ++runde) {
        laufe(a.m, 2'000'000);
        laufe(b.m, 2'000'000);
        const auto sa = a.m.stateBytes(), sb = b.m.stateBytes();
        ASSERT_EQ(sa, sb) << "Runde " << runde << ": " << ersterUnterschied(sa, sb);
    }
    EXPECT_EQ(a.m.cpuPC(), b.m.cpuPC());
    EXPECT_EQ(a.m.totalCycles(), b.m.totalCycles());
    EXPECT_EQ(bild(a.m), bild(b.m));
    for (int addr = 0; addr < 0x10000; ++addr)
        ASSERT_EQ(a.m.memReadDebug(uint16_t(addr)), b.m.memReadDebug(uint16_t(addr))) << addr;
    EXPECT_NE(a.m.totalCycles(), 0u);
}

/// Die Rundreise überlebt auch den Weg über eine Datei.
TEST(P8000Stand, DateiRundreise) {
    Rechner a, b;
    ASSERT_NO_FATAL_FAILURE(bisMittenImLaden(a.m));
    const std::string pfad = k1520test::tempPath("p8ks_stand.bin");
    ASSERT_TRUE(a.m.saveState(pfad));
    ASSERT_TRUE(b.m.loadState(pfad)) << b.m.stateError();
    std::remove(pfad.c_str());
    EXPECT_EQ(a.m.stateBytes(), b.m.stateBytes());
}

/// Andere Konfiguration (ROM-Satz) ⇒ abgelehnt, Maschine bleibt unverändert; ein beschädigter
/// Zustand ebenso (alter Stand wird wiederhergestellt).
TEST(P8000Stand, AbweichendeKonfigurationUndDefektWerdenAbgelehnt) {
    Rechner a;
    ASSERT_NO_FATAL_FAILURE(bisMittenImLaden(a.m));
    const std::vector<uint8_t> stand = a.m.stateBytes();

    P8000Machine::Config cfg;
    cfg.mon8 = P8000Machine::Config::Mon8::V3_0;
    P8000Machine anders(cfg);
    anders.powerOn();
    const auto vorher = anders.stateBytes();
    EXPECT_FALSE(anders.restoreStateBytes(stand));
    EXPECT_NE(anders.stateError().find("Konfiguration"), std::string::npos) << anders.stateError();
    EXPECT_EQ(anders.stateBytes(), vorher);

    P8000Machine gleich;
    gleich.powerOn();
    const auto v2 = gleich.stateBytes();
    auto kaputt = stand;
    kaputt.resize(kaputt.size() - 40);                  // Abschnitt „Maschine" abgeschnitten
    EXPECT_FALSE(gleich.restoreStateBytes(kaputt));
    EXPECT_EQ(gleich.stateBytes(), v2);
    auto ohneKennung = stand;
    ohneKennung[0] = 'X';
    EXPECT_FALSE(gleich.restoreStateBytes(ohneKennung));
    EXPECT_NE(gleich.stateError().find("P8KS"), std::string::npos);
    EXPECT_EQ(gleich.stateBytes(), v2);
}
