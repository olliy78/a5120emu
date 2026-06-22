// Integrationstest für A5120Machine::captureState / restoreState (Snapshot/Reverse).
#include "core/machines/a5120/a5120.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>

namespace {
A5120Machine::MachineSnapshot* heapSnap(){ return new A5120Machine::MachineSnapshot(); }
std::string tmpStatePath(){ return std::string(::testing::TempDir()) + "k1520_state_test.bin"; }
}

TEST(MachineSnapshot, RamRoundTrip){
    A5120Machine m; m.powerOn();
    // freier RAM-Bereich oberhalb des ROM-Schattens
    for (uint16_t i=0;i<16;++i) m.memWriteDebug(0x6000+i, (uint8_t)(0xA0+i));

    A5120Machine::MachineSnapshot s;
    m.captureState(s);

    // RAM verändern …
    for (uint16_t i=0;i<16;++i) m.memWriteDebug(0x6000+i, 0x00);
    EXPECT_EQ(m.memReadDebug(0x6005), 0x00);

    // … und zurückrollen
    EXPECT_TRUE(m.restoreState(s));
    for (uint16_t i=0;i<16;++i)
        EXPECT_EQ(m.memReadDebug(0x6000+i), (uint8_t)(0xA0+i));
}

TEST(MachineSnapshot, RegistersRoundTrip){
    A5120Machine m; m.powerOn();
    Z80& z = m.cpuDebug();
    z.HL = 0x1234; z.BC = 0x5678; z.SP = 0x7F00; z.IFF1 = true; z.IM = 2;

    A5120Machine::MachineSnapshot s;
    m.captureState(s);

    z.HL = 0xFFFF; z.BC = 0x0000; z.SP = 0x0000; z.IFF1 = false; z.IM = 0;
    m.restoreState(s);

    EXPECT_EQ(m.cpuDebug().HL, 0x1234);
    EXPECT_EQ(m.cpuDebug().BC, 0x5678);
    EXPECT_EQ(m.cpuDebug().SP, 0x7F00);
    EXPECT_TRUE(m.cpuDebug().IFF1);
    EXPECT_EQ(m.cpuDebug().IM, 2);
}

TEST(MachineSnapshot, RestoreAfterRunningReproducesState){
    A5120Machine m; m.powerOn();
    // Run well past the boot-ROM unmap so capture & restore are both in the
    // post-ROM regime (the snapshot deliberately does not reproduce ROM mapping).
    m.run(400000);

    auto* s = heapSnap();
    m.captureState(*s);
    uint16_t pc_at_snap  = m.cpuPC();
    uint8_t  ram_at_snap = m.memReadDebug(0x6000);

    m.memWriteDebug(0x6000, 0x99);      // perturb RAM
    m.run(100000);                      // advance further

    EXPECT_TRUE(m.restoreState(*s));    // ROM mapping unchanged across this window
    EXPECT_EQ(m.cpuPC(), pc_at_snap);
    EXPECT_EQ(m.memReadDebug(0x6000), ram_at_snap);
    delete s;
}

// ── saveState / loadState (Persistenz auf Platte) ────────────────────────────

TEST(MachineSnapshot, SaveAndLoadStateRoundTrip){
    std::string path = tmpStatePath();
    A5120Machine a; a.powerOn(); a.run(400000);   // post-ROM
    a.cpuDebug().HL = 0x1234;
    for (uint16_t i=0;i<8;++i) a.memWriteDebug(0x6000+i, (uint8_t)(0x70+i));
    uint16_t pc = a.cpuPC();
    ASSERT_TRUE(a.saveState(path));

    // Frische Maschine: laden statt booten.
    A5120Machine b; b.powerOn(); b.run(400000);    // gleiche ROM-Phase (kein Mismatch)
    ASSERT_TRUE(b.loadState(path));
    EXPECT_EQ(b.cpuDebug().HL, 0x1234);
    EXPECT_EQ(b.cpuPC(), pc);
    for (uint16_t i=0;i<8;++i) EXPECT_EQ(b.memReadDebug(0x6000+i), (uint8_t)(0x70+i));
    std::remove(path.c_str());
}

TEST(MachineSnapshot, LoadStateRejectsMissingAndGarbage){
    A5120Machine m; m.powerOn();
    EXPECT_FALSE(m.loadState("/nonexistent/k1520/does-not-exist.bin"));
    // Eine Datei mit falscher Magic wird abgelehnt (Maschine unverändert).
    std::string path = tmpStatePath();
    { FILE* f = std::fopen(path.c_str(), "wb"); std::fputs("NOTASTATEFILE....", f); std::fclose(f); }
    EXPECT_FALSE(m.loadState(path));
    std::remove(path.c_str());
}
