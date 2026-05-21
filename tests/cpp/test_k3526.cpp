/**
 * @file test_k3526.cpp
 * @brief Unit tests for the K3526 OPS RAM card emulation.
 *
 * @details
 * Emulator component under test: **K3526** (`core/cards/k3526/k3526.h`)
 *
 * The K3526 OPS (Operativspeicher) card provides the full 64 KiB working RAM
 * for the Robotron A5120.  The memory is divided into four 16 KiB groups:
 *  - Group 0: 0x0000–0x3FFF
 *  - Group 1: 0x4000–0x7FFF
 *  - Group 2: 0x8000–0xBFFF
 *  - Group 3: 0xC000–0xFFFF
 *
 * Each group can be independently disabled via setMemDI(), which causes reads
 * to return 0xFF (bus float) and silently drops writes.  This mirrors the
 * MEMI (memory inhibit) hardware present on the A5120 K1520 bus.
 *
 * ## Test groups
 *
 * | Group                  | What is tested                                          |
 * |------------------------|---------------------------------------------------------|
 * | Basic read/write       | All four groups, arbitrary addresses                    |
 * | Initial state          | Memory is zero-initialised after construction           |
 * | fill() / load()        | Bulk initialisation helpers                             |
 * | rawPtr()               | Direct pointer access for DMA / test helpers            |
 * | isWritable             | Always true for RAM                                     |
 * | setMemDI               | Per-group disable: reads 0xFF, writes dropped, re-enable|
 * | Group boundaries       | Last/first byte of adjacent groups are independent      |
 * | attachToBus            | Integration with K1520Bus, bus-level MEMDI              |
 *
 * @see core/cards/k3526/k3526.h
 * @see core/bus/k1520_bus.h
 */

#include <gtest/gtest.h>
#include "core/cards/k3526/k3526.h"

// ─── Basic read/write ─────────────────────────────────────────────────────────

/**
 * @test K3526/ReadWrite_Group0
 * @brief Read and write to the start of group 0 (0x0000–0x3FFF) works correctly.
 * @par Pass criterion  memRead(0x0000) == 0xAB after memWrite(0x0000, 0xAB).
 */
TEST(K3526, ReadWrite_Group0) {
    K3526 ops;
    ops.memWrite(0x0000, 0xAB);
    EXPECT_EQ(ops.memRead(0x0000), 0xAB);
}

/**
 * @test K3526/ReadWrite_Group1
 * @brief Read and write to group 1 (0x4000–0x7FFF) works correctly.
 * @par Pass criterion  memRead(0x4000) == 0xCD.
 */
TEST(K3526, ReadWrite_Group1) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCD);
    EXPECT_EQ(ops.memRead(0x4000), 0xCD);
}

/**
 * @test K3526/ReadWrite_Group2
 * @brief Read and write to group 2 (0x8000–0xBFFF) works correctly.
 * @par Pass criterion  memRead(0x8000) == 0x12.
 */
TEST(K3526, ReadWrite_Group2) {
    K3526 ops;
    ops.memWrite(0x8000, 0x12);
    EXPECT_EQ(ops.memRead(0x8000), 0x12);
}

/**
 * @test K3526/ReadWrite_Group3
 * @brief Read and write to group 3 (0xC000–0xFFFF) works correctly.
 * @par Pass criterion  memRead(0xC000) == 0x34.
 */
TEST(K3526, ReadWrite_Group3) {
    K3526 ops;
    ops.memWrite(0xC000, 0x34);
    EXPECT_EQ(ops.memRead(0xC000), 0x34);
}

/**
 * @test K3526/ReadWrite_Arbitrary
 * @brief Read and write at arbitrary non-group-start addresses across all groups works.
 * @par Pass criterion  Each written value is read back identically.
 */
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

/**
 * @test K3526/InitialState_Zero
 * @brief After construction all 65536 bytes are zero-initialised.
 * @par Pass criterion  memRead returns 0x00 at all sampled addresses (start/end of each group).
 */
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

/**
 * @test K3526/Fill_AllBytes
 * @brief fill(val) sets every byte in all 65536 positions to the given value.
 * @par Pass criterion  memRead at all group boundaries returns the fill value 0xFF.
 */
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

/**
 * @test K3526/Fill_Zero
 * @brief Filling with 0x00 after filling with 0xFF resets all bytes to zero.
 * @par Pass criterion  memRead returns 0x00 after fill(0xFF) then fill(0x00).
 */
TEST(K3526, Fill_Zero) {
    K3526 ops;
    ops.fill(0xFF);
    ops.fill(0x00);
    EXPECT_EQ(ops.memRead(0x1000), 0x00);
    EXPECT_EQ(ops.memRead(0xF000), 0x00);
}

// ─── load() ──────────────────────────────────────────────────────────────────

/**
 * @test K3526/Load_BasicData
 * @brief load() copies a byte array into memory starting at the given address.
 * @par Pass criterion  memRead returns the correct byte for each loaded position.
 */
TEST(K3526, Load_BasicData) {
    K3526 ops;
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    ops.load(0x2000, data, 4);
    EXPECT_EQ(ops.memRead(0x2000), 0x01);
    EXPECT_EQ(ops.memRead(0x2001), 0x02);
    EXPECT_EQ(ops.memRead(0x2002), 0x03);
    EXPECT_EQ(ops.memRead(0x2003), 0x04);
}

/**
 * @test K3526/Load_CrossGroupBoundary
 * @brief load() that spans a group boundary (0x3FFF–0x4001) writes to both groups correctly.
 * @par Pass criterion  rawPtr and memRead both reflect the loaded bytes across the boundary.
 */
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

/**
 * @test K3526/RawPtr_ReturnsValidPointer
 * @brief rawPtr() returns a non-null pointer that reflects the same data as memRead.
 * @par Pass criterion  rawPtr() != nullptr; rawPtr()[0x0010] == 0x42 after memWrite.
 */
TEST(K3526, RawPtr_ReturnsValidPointer) {
    K3526 ops;
    ops.memWrite(0x0010, 0x42);
    EXPECT_NE(ops.rawPtr(), nullptr);
    EXPECT_EQ(ops.rawPtr()[0x0010], 0x42);
}

// ─── isWritable ──────────────────────────────────────────────────────────────

/**
 * @test K3526/IsWritable_ReturnsTrue
 * @brief K3526 is always writable RAM (unlike ROM devices).
 * @par Pass criterion  isWritable() == true.
 */
TEST(K3526, IsWritable_ReturnsTrue) {
    K3526 ops;
    EXPECT_TRUE(ops.isWritable());
}

// ─── setMemDI: disable one group ─────────────────────────────────────────────

/**
 * @test K3526/MemDI_DisabledGroup_ReadReturns0xFF
 * @brief Reads from a disabled group return 0xFF (bus float).
 * @details setMemDI(1, true) disables group 1 (0x4000–0x7FFF).
 * @par Pass criterion  All addresses in group 1 return 0xFF while disabled.
 */
TEST(K3526, MemDI_DisabledGroup_ReadReturns0xFF) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCC); // write to group 1
    ops.setMemDI(1, true);       // disable group 1
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    EXPECT_EQ(ops.memRead(0x5000), 0xFF);
    EXPECT_EQ(ops.memRead(0x7FFF), 0xFF);
}

/**
 * @test K3526/MemDI_DisabledGroup_WriteIgnored
 * @brief Writes to a disabled group are silently discarded.
 * @par Pass criterion  After re-enable the address still contains 0x00 (the pre-fill value).
 */
TEST(K3526, MemDI_DisabledGroup_WriteIgnored) {
    K3526 ops;
    ops.fill(0x00);
    ops.setMemDI(1, true); // disable group 1 (0x4000-0x7FFF)
    ops.memWrite(0x5000, 0xBB);
    // Write was ignored; re-enable and verify still 0x00
    ops.setMemDI(1, false);
    EXPECT_EQ(ops.memRead(0x5000), 0x00);
}

/**
 * @test K3526/MemDI_DisabledGroup_OtherGroupsUnaffected
 * @brief Disabling one group does not affect reads or writes in other groups.
 * @par Pass criterion  Groups 0, 2, 3 remain readable; group 1 returns 0xFF.
 */
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

/**
 * @test K3526/MemDI_AllGroupsDisabled
 * @brief Disabling all four groups causes all reads to return 0xFF.
 * @par Pass criterion  memRead returns 0xFF at all group base addresses.
 */
TEST(K3526, MemDI_AllGroupsDisabled) {
    K3526 ops;
    ops.fill(0x55);
    for (int g = 0; g < 4; ++g) ops.setMemDI(g, true);

    EXPECT_EQ(ops.memRead(0x0000), 0xFF);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    EXPECT_EQ(ops.memRead(0x8000), 0xFF);
    EXPECT_EQ(ops.memRead(0xC000), 0xFF);
}

/**
 * @test K3526/MemDI_ReEnable
 * @brief Re-enabling a disabled group restores normal read access to the previously stored data.
 * @par Pass criterion  After setMemDI(1, false) the original 0xCC value is readable again.
 */
TEST(K3526, MemDI_ReEnable) {
    K3526 ops;
    ops.memWrite(0x4000, 0xCC);
    ops.setMemDI(1, true);
    EXPECT_EQ(ops.memRead(0x4000), 0xFF);
    ops.setMemDI(1, false);
    EXPECT_EQ(ops.memRead(0x4000), 0xCC); // data still intact
}

// ─── Group boundaries ─────────────────────────────────────────────────────────

/**
 * @test K3526/GroupBoundary_G0_LastByte
 * @brief The last byte of group 0 (0x3FFF) is addressable and readable.
 * @par Pass criterion  memRead(0x3FFF) == 0xAA after memWrite(0x3FFF, 0xAA).
 */
TEST(K3526, GroupBoundary_G0_LastByte) {
    K3526 ops;
    ops.memWrite(0x3FFF, 0xAA);
    EXPECT_EQ(ops.memRead(0x3FFF), 0xAA);
}

/**
 * @test K3526/GroupBoundary_G1_FirstByte
 * @brief The first byte of group 1 (0x4000) is addressable and readable.
 * @par Pass criterion  memRead(0x4000) == 0xBB after memWrite(0x4000, 0xBB).
 */
TEST(K3526, GroupBoundary_G1_FirstByte) {
    K3526 ops;
    ops.memWrite(0x4000, 0xBB);
    EXPECT_EQ(ops.memRead(0x4000), 0xBB);
}

/**
 * @test K3526/GroupBoundary_G0EndG1Start_Independent
 * @brief Bytes at 0x3FFF (end of group 0) and 0x4000 (start of group 1) belong to separate groups.
 * @details Disabling group 0 must not affect 0x4000, and vice versa.
 * @par Pass criterion  Each address responds only to its own group's MemDI state.
 */
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

/**
 * @test K3526/GroupBoundary_LastByteEachGroup
 * @brief The last byte of every group (0x3FFF, 0x7FFF, 0xBFFF, 0xFFFF) is accessible.
 * @par Pass criterion  Each last-byte address reads back the written value.
 */
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

/**
 * @test K3526/AttachToBus_ReadWriteViaStandardConfig
 * @brief After attaching to the K1520Bus, the full address range is accessible via bus calls.
 * @par Pass criterion  bus.memRead/Write works for representative addresses in all four groups.
 */
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

/**
 * @test K3526/AttachToBus_MemDI_ViaDirectControl
 * @brief Per-group MemDI disabling is reflected in bus-level reads when attached.
 * @par Pass criterion  bus.memRead(0x4000) == 0xFF while group 1 is disabled; 0xCC after re-enable.
 */
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

/**
 * @test K3526/AttachToBus_BusMemDI_BlocksAll
 * @brief The global bus-level MEMDI signal (bus.setMEMDI) blocks all memory reads.
 * @par Pass criterion  All bus.memRead calls return 0xFF after bus.setMEMDI(true).
 */
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

/**
 * @test K3526/AttachToBus_GroupBoundary_0x3FFF_vs_0x4000
 * @brief Addresses 0x3FFF and 0x4000 are independent via the bus interface.
 * @par Pass criterion  Both addresses read back their individually written values.
 */
TEST(K3526, AttachToBus_GroupBoundary_0x3FFF_vs_0x4000) {
    K3526 ops;
    K1520Bus bus;
    ops.attachToBus(bus);

    bus.memWrite(0x3FFF, 0xAA);
    bus.memWrite(0x4000, 0xBB);

    EXPECT_EQ(bus.memRead(0x3FFF), 0xAA);
    EXPECT_EQ(bus.memRead(0x4000), 0xBB);
}

/**
 * @test K3526/AttachToBus_CustomGroupConfig
 * @brief A non-standard group configuration (inverted group order) still maps addresses correctly.
 * @details Group base addresses are reversed (group 0 → 0xC000, group 3 → 0x0000) to verify
 *   that the configuration is respected by the bus registration.
 * @par Pass criterion  Each absolute address reads back its written value regardless of group order.
 */
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
