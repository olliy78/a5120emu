/**
 * @file test_p8000_machine.cpp
 * @brief P8000-Maschine, 8-Bit-Seite (doc/design/25_p8000.md §10.2/§10.11 AP P7a): Laufschleife
 *        mit MON8 3.1 bis zum Monitor-Prompt auf dem Kern-Terminal, Reset-/Netz-Ein-Weg (RESI),
 *        NMI-Taste, DMA hält die CPU, keine Zusatzkarten.
 */

#include <gtest/gtest.h>
#include <cstdio>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
constexpr long long MONITOR_GRENZE = 200'000'000;   // ≈ 50 s Maschinenzeit
}  // namespace

TEST(P8000Machine, Mon8_3_1_MeldetSichAmKernTerminal) {
    stumm();
    P8000Machine m;
    m.powerOn();
    ASSERT_TRUE(laufeBisText(m, "Press RETURN", MONITOR_GRENZE)) << bild(m);
    const std::string b = bild(m);
    EXPECT_NE(b.find("P8000 Hardwaretest U880 - Version 3.1"), std::string::npos) << b;
    EXPECT_NE(b.find("U880-Softwaremonitor Version 3.1 - Press RETURN"), std::string::npos) << b;
    EXPECT_EQ(b.find("ERROR"), std::string::npos) << b;
    EXPECT_GT(m.totalCycles(), 0u);
    std::printf("Monitor-Prompt nach %llu Takten\n", (unsigned long long)m.totalCycles());
}

TEST(P8000Machine, KeineZusatzkarten) {
    P8000Machine m;
    EXPECT_FALSE(m.zusatzkartenSteckbar());
    EXPECT_FALSE(m.installRaf(RAF::Typ::RAF512));
    EXPECT_FALSE(m.rafFehler().empty());
    EXPECT_FALSE(m.installK6022());
    EXPECT_FALSE(m.k6022Fehler().empty());
    EXPECT_EQ(m.raf(), nullptr);
    EXPECT_EQ(m.k6022(), nullptr);
}

TEST(P8000Machine, NetzEinUndResetTasteUnterscheidenSichInResi) {
    stumm();
    P8000Machine m;
    m.powerOn();
    EXPECT_TRUE(m.karte8().resi());
    EXPECT_EQ(m.cpuPC(), 0x0000);
    laufe(m, 200'000);
    EXPECT_NE(m.cpuPC(), 0x0000);
    m.reset();
    EXPECT_FALSE(m.karte8().resi()) << "Taste /RESP: RESI = 0";
    EXPECT_EQ(m.cpuPC(), 0x0000);
    EXPECT_FALSE(m.karte8().speicher().rffGesetzt()) << "RFF Q = 0 nach /RES";
}

TEST(P8000Machine, NmiTasteGehtOhneSechzehnBitKarteAnDenU880) {
    stumm();
    P8000Machine m;
    m.powerOn();
    ASSERT_TRUE(laufeBisText(m, "Press RETURN", MONITOR_GRENZE));
    EXPECT_TRUE(m.karte8().b7eff()) << "PIO0-B7 offen = Pull-up";
    bool nmi_gesehen = false;
    m.setCpuTraceCallback([&](const Z80& c) { if (c.PC == 0x0066) nmi_gesehen = true; });
    m.nmi();
    m.run(1000);
    m.setCpuTraceCallback(nullptr);
    EXPECT_TRUE(nmi_gesehen);
}

TEST(P8000Machine, StopHaeltDieLaufschleifeAn) {
    stumm();
    P8000Machine m;
    m.powerOn();
    m.stop();
    EXPECT_EQ(m.run(100'000), 0);
    m.clearStop();
    EXPECT_GT(m.run(100'000), 0);
}

TEST(P8000Machine, DmaHaeltDieCpu_UrladerLiegtNachReturnBei0C00) {
    stumm();
    k1520test::TempDisk disk("udosP8000_640k_wega.hfe");
    P8000Machine m;
    ASSERT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisText(m, "Press RETURN", MONITOR_GRENZE));
    // RETURN auf „Press RETURN" ⇒ Prompt '>'; RETURN am Prompt ⇒ BOOT (MON8 RASC).
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    // Während eines DMA-Bytes steht die CPU: der PC ändert sich nicht.
    tippe(m, "\r");
    uint64_t dma_schritte = 0, cpu_lief_mit = 0;
    const uint64_t ende = m.totalCycles() + 20'000'000;
    while (m.totalCycles() < ende && dma_schritte < 200) {
        const uint16_t pc = m.cpuPC();
        const bool dma = m.floppy8().dma().busRequest();
        m.run(1);
        if (dma) { ++dma_schritte; if (m.cpuPC() != pc) ++cpu_lief_mit; }
    }
    EXPECT_GE(dma_schritte, 200u) << bild(m);
    EXPECT_EQ(cpu_lief_mit, 0u);
    laufe(m, 2'000'000);
    // Urlader Z0/K0/S1 (138 B) bei 0C00H, Kennung 'P8000SYS' bei 0C80H (MON8 TESTSD)
    std::string kenn;
    for (int i = 0; i < 8; ++i) kenn += char(m.memReadDebug(uint16_t(0x0C80 + i)));
    EXPECT_EQ(kenn, "P8000SYS");
}
