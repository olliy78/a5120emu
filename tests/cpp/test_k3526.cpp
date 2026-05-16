#include <gtest/gtest.h>
#include "core/cards/k3526/k3526.h"

// ─── Basic read/write ─────────────────────────────────────────────────────────

TEST(K3526, ReadWrite_Group0) {
    K3526 ops;
    ops.memWrite(0x0000, 0xAB);
    EXPECT_EQ(ops.memRead(0x0000), 0xAB);
}

TEST(K3526, ReadWrite_Group1) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCD);
    EXPECT_EQ(ops.memRead(0x4000), 0xCD);
}

TEST(K3526, ReadWrite_Group2) {
    K3526 ops;
    ops.memWrite(0x8000, 0x12);
    EXPECT_EQ(ops.memRead(0x8000), 0x12);
}

TEST(K3526, ReadWrite_Group3) {
    K3526 ops;
    ops.memWrite(0xC000, 0x34);
    EXPECT_EQ(ops.memRead(0xC000), 0x34);
}

TEST(K3526, ReadWrite_Arbitrary) {
    K3526 ops;
    ops.memWrite(0x1234, 0x56);
    EXPECT_EQ(ops.memRead(0x1234), 0x56);

    ops.memWrite(0x5678, 0x9A);
    EXPECT_EQ(ops.memRead(0x5678), 0x9A);

    ops.memWrite(0xABCD, 0xEF);
    EXPECT_EQ(ops.memRead(0xABCD), 0xEF);
}

// ─── Initial state ────────────────────────────────────────────────────────────

TEST(K3526, InitialState_Zero) {
    K3526 ops;
    // Default construction zeroes memory
    EXPECT_EQ(ops.memRead(0x0000), 0x00);
    EXPECT_EQ(ops.memRead(0x4000), 0x00);
    EXPECT_EQ(ops.memRead(0x8000), 0x00);
    EXPECT_EQ(ops.memRead(0xC000), 0x00);
    EXPECT_EQ(ops.memRead(0xFFFF), 0x00);
}

// ─── fill() ──────────────────────────────────────────────────────────────────

TEST(K3526, Fill_AllBytes) {
    K3526 ops;
    ops.fill(0xFF);
    EXPECT_EQ(ops.memRead(0x0000), 0xFF);
    EXPECT_EQ(ops.memRead(0x3FFF), 0xFF);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    EXPECT_EQ(ops.memRead(0x7FFF), 0xFF);
    EXPECT_EQ(ops.memRead(0x8000), 0xFF);
    EXPECT_EQ(ops.memRead(0xFFFF), 0xFF);
}

TEST(K3526, Fill_Zero) {
    K3526 ops;
    ops.fill(0xFF);
    ops.fill(0x00);
    EXPECT_EQ(ops.memRead(0x1000), 0x00);
    EXPECT_EQ(ops.memRead(0xF000), 0x00);
}

// ─── load() ──────────────────────────────────────────────────────────────────

TEST(K3526, Load_BasicData) {
    K3526 ops;
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    ops.load(0x2000, data, 4);
    EXPECT_EQ(ops.memRead(0x2000), 0x01);
    EXPECT_EQ(ops.memRead(0x2001), 0x02);
    EXPECT_EQ(ops.memRead(0x2002), 0x03);
    EXPECT_EQ(ops.memRead(0x2003), 0x04);
}

TEST(K3526, Load_CrossGroupBoundary) {
    K3526 ops;
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    // Load at the end of group 0 / start of group 1
    ops.load(0x3FFF, data, 3);
    EXPECT_EQ(ops.rawPtr()[0x3FFF], 0xAA);
    EXPECT_EQ(ops.rawPtr()[0x4000], 0xBB);
    EXPECT_EQ(ops.rawPtr()[0x4001], 0xCC);
    // Verify readable through memRead (group dispatch)
    EXPECT_EQ(ops.memRead(0x3FFF), 0xAA);
    EXPECT_EQ(ops.memRead(0x4000), 0xBB);
    EXPECT_EQ(ops.memRead(0x4001), 0xCC);
}

// ─── rawPtr() ────────────────────────────────────────────────────────────────

TEST(K3526, RawPtr_ReturnsValidPointer) {
    K3526 ops;
    ops.memWrite(0x0010, 0x42);
    EXPECT_NE(ops.rawPtr(), nullptr);
    EXPECT_EQ(ops.rawPtr()[0x0010], 0x42);
}

// ─── isWritable ──────────────────────────────────────────────────────────────

TEST(K3526, IsWritable_ReturnsTrue) {
    K3526 ops;
    EXPECT_TRUE(ops.isWritable());
}

// ─── setMemDI: disable one group ─────────────────────────────────────────────

TEST(K3526, MemDI_DisabledGroup_ReadReturns0xFF) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCC); // write to group 1
    ops.setMemDI(1, true);       // disable group 1
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    EXPECT_EQ(ops.memRead(0x5000), 0xFF);
    EXPECT_EQ(ops.memRead(0x7FFF), 0xFF);
}

TEST(K3526, MemDI_DisabledGroup_WriteIgnored) {
    K3526 ops;
    ops.fill(0x00);
    ops.setMemDI(1, true); // disable group 1 (0x4000-0x7FFF)
    ops.memWrite(0x5000, 0xBB);
    // Write was ignored; re-enable and verify still 0x00
    ops.setMemDI(1, false);
    EXPECT_EQ(ops.memRead(0x5000), 0x00);
}

TEST(K3526, MemDI_DisabledGroup_OtherGroupsUnaffected) {
    K3526 ops;
    ops.memWrite(0x0100, 0x11); // group 0
    ops.memWrite(0x4100, 0x22); // group 1
    ops.memWrite(0x8100, 0x33); // group 2
    ops.memWrite(0xC100, 0x44); // group 3

    ops.setMemDI(1, true); // disable only group 1

    EXPECT_EQ(ops.memRead(0x0100), 0x11); // group 0 unaffected
    EXPECT_EQ(ops.memRead(0x4100), 0xFF); // group 1 disabled
    EXPECT_EQ(ops.memRead(0x8100), 0x33); // group 2 unaffected
    EXPECT_EQ(ops.memRead(0xC100), 0x44); // group 3 unaffected
}

TEST(K3526, MemDI_AllGroupsDisabled) {
    K3526 ops;
    ops.fill(0x55);
    for (int g = 0; g < 4; ++g) ops.setMemDI(g, true);

    EXPECT_EQ(ops.memRead(0x0000), 0xFF);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    EXPECT_EQ(ops.memRead(0x8000), 0xFF);
    EXPECT_EQ(ops.memRead(0xC000), 0xFF);
}

TEST(K3526, MemDI_ReEnable) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCC);
    ops.setMemDI(1, true);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    ops.setMemDI(1, false);
    EXPECT_EQ(ops.memRead(0x4000), 0xCC); // data still intact
}

// ─── Group boundaries ─────────────────────────────────────────────────────────

TEST(K3526, GroupBoundary_G0_LastByte) {
    K3526 ops;
    ops.memWrite(0x3FFF, 0xAA);
    EXPECT_EQ(ops.memRead(0x3FFF), 0xAA);
}

TEST(K3526, GroupBoundary_G1_FirstByte) {
    K3526 ops;
    ops.memWrite(0x4000, 0xBB);
    EXPECT_EQ(ops.memRead(0x4000), 0xBB);
}

TEST(K3526, GroupBoundary_G0EndG1Start_Independent) {
    K3526 ops;
    ops.memWrite(0x3FFF, 0xAA);
    ops.memWrite(0x4000, 0xBB);

    // Disable group 0 only – 0x4000 still readable
    ops.setMemDI(0, true);
    EXPECT_EQ(ops.memRead(0x3FFF), 0xFF);
    EXPECT_EQ(ops.memRead(0x4000), 0xBB);

    // Re-enable group 0, disable group 1 – 0x3FFF still readable
    ops.setMemDI(0, false);
    ops.setMemDI(1, true);
    EXPECT_EQ(ops.memRead(0x3FFF), 0xAA);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
}

TEST(K3526, GroupBoundary_LastByteEachGroup) {
    K3526 ops;
    ops.memWrite(0x3FFF, 0x01);
    ops.memWrite(0x7FFF, 0x02);
    ops.memWrite(0xBFFF, 0x03);
    ops.memWrite(0xFFFF, 0x04);
    EXPECT_EQ(ops.memRead(0x3FFF), 0x01);
    EXPECT_EQ(ops.memRead(0x7FFF), 0x02);
    EXPECT_EQ(ops.memRead(0xBFFF), 0x03);
    EXPECT_EQ(ops.memRead(0xFFFF), 0x04);
}

// ─── attachToBus integration ──────────────────────────────────────────────────

TEST(K3526, AttachToBus_ReadWriteViaStandardConfig) {
    K3526 ops;
    K1520Bus bus;
    ops.attachToBus(bus);

    bus.memWrite(0x1000, 0x42);
    EXPECT_EQ(bus.memRead(0x1000), 0x42);

    bus.memWrite(0x5000, 0x84);
    EXPECT_EQ(bus.memRead(0x5000), 0x84);

    bus.memWrite(0xA000, 0xF0);
    EXPECT_EQ(bus.memRead(0xA000), 0xF0);

    bus.memWrite(0xE000, 0x0F);
    EXPECT_EQ(bus.memRead(0xE000), 0x0F);
}

TEST(K3526, AttachToBus_MemDI_ViaDirectControl) {
    K3526 ops;
    K1520Bus bus;
    ops.attachToBus(bus);

    bus.memWrite(0x4000, 0xCC);
    ops.setMemDI(1, true); // group 1 disabled at card level
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);

    ops.setMemDI(1, false);
    EXPECT_EQ(bus.memRead(0x4000), 0xCC);
}

TEST(K3526, AttachToBus_BusMemDI_BlocksAll) {
    K3526 ops;
    K1520Bus bus;
    ops.attachToBus(bus);

    bus.memWrite(0x0000, 0x11);
    bus.memWrite(0x4000, 0x22);
    bus.setMEMDI(true); // global bus-level disable

    EXPECT_EQ(bus.memRead(0x0000), 0xFF);
    EXPECT_EQ(bus.memRead(0x4000), 0xFF);
}

TEST(K3526, AttachToBus_GroupBoundary_0x3FFF_vs_0x4000) {
    K3526 ops;
    K1520Bus bus;
    ops.attachToBus(bus);

    bus.memWrite(0x3FFF, 0xAA);
    bus.memWrite(0x4000, 0xBB);

    EXPECT_EQ(bus.memRead(0x3FFF), 0xAA);
    EXPECT_EQ(bus.memRead(0x4000), 0xBB);
}

TEST(K3526, AttachToBus_CustomGroupConfig) {
    // Test with a non-standard configuration where groups are re-ordered
    K3526::A5120Config cfg;
    cfg.groups[0] = {0xC000, false};
    cfg.groups[1] = {0x8000, false};
    cfg.groups[2] = {0x4000, false};
    cfg.groups[3] = {0x0000, false};

    K3526 ops(cfg);
    K1520Bus bus;
    ops.attachToBus(bus);

    // Data should still be accessible at absolute addresses
    bus.memWrite(0x0100, 0x11);
    bus.memWrite(0x4100, 0x22);
    bus.memWrite(0x8100, 0x33);
    bus.memWrite(0xC100, 0x44);

    EXPECT_EQ(bus.memRead(0x0100), 0x11);
    EXPECT_EQ(bus.memRead(0x4100), 0x22);
    EXPECT_EQ(bus.memRead(0x8100), 0x33);
    EXPECT_EQ(bus.memRead(0xC100), 0x44);
}
