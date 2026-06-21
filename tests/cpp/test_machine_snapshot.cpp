// Integrationstest für A5120Machine::captureState / restoreState (Snapshot/Reverse).
#include "core/machines/a5120/a5120.h"
#include <gtest/gtest.h>

namespace {
A5120Machine::MachineSnapshot* heapSnap(){ return new A5120Machine::MachineSnapshot(); }
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
