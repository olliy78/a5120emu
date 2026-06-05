/**
 * @file test_boot_integration.cpp
 * @brief End-to-end boot integration test for the full A5120 machine.
 *
 * Boots a real A5120Machine from a disk image and verifies that the ZVE1↔ZVE2
 * boot ROM successfully drives the K5122 DMA: the boot sector's "SYL" signature
 * and all four boot sectors are copied into RAM at 0x0400.
 *
 * This exercises the whole stack — K2526 (ZVE1+ZVE2 + boot ROM), K1520 bus,
 * K3526 RAM, and the K5122 continuous-track-stream DMA — together, unlike the
 * per-card unit tests.
 *
 * The disk directory is provided at compile time via A5120_TEST_DISK_DIR
 * (set in CMakeLists.txt to ${CMAKE_SOURCE_DIR}/disks).
 */

#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifndef A5120_TEST_DISK_DIR
#define A5120_TEST_DISK_DIR "."
#endif

namespace {

constexpr uint16_t kLoadAddr     = 0x0400;  // boot ROM load address (ZVE2 → RAM)
constexpr uint16_t kDoneFlagAddr = 0x03F8;  // ZVE1↔ZVE2 handshake flag
constexpr uint8_t  kDoneValue    = 0x03;    // ZVE2 wrote "all sectors copied"
constexpr int      kBootSectors  = 4;       // ROM [0x07F2]=4 → loads sectors 1..4
constexpr int      kSectorBytes  = 128;     // cpa780 boot track: 128-byte sectors
constexpr int      kBootBytes    = kBootSectors * kSectorBytes;  // 512

std::string diskPath(const char* name) {
    return std::string(A5120_TEST_DISK_DIR) + "/" + name;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/**
 * @brief Run the machine until ZVE2 signals DMA completion ([0x03F8]==3).
 *
 * Steps in small batches and stops as soon as the done-flag is set, so the
 * loaded boot code (which starts at 0x0437 right after) cannot overwrite the
 * just-loaded sectors before we inspect them.
 *
 * @return true if completion was observed within the cycle budget.
 */
bool runUntilDmaComplete(A5120Machine& m, int max_cycles = 3'000'000) {
    constexpr int batch = 2000;
    for (int done = 0; done < max_cycles; done += batch) {
        m.run(batch);
        if (m.memReadDebug(kDoneFlagAddr) == kDoneValue)
            return true;
    }
    return false;
}

}  // namespace

/**
 * @test BootIntegration/LoadsBootSectorsWithSYLSignature
 * @brief A5120 boots cpadisk01.img and ZVE2 DMAs the 4 boot sectors to 0x0400.
 *
 * Pass criteria:
 *   - ZVE2 signals completion ([0x03F8]=3) within the cycle budget.
 *   - RAM[0x0400..0x0402] == "SYL" (the boot-record signature ZVE1 checks at 0x01B6).
 *   - RAM[0x0400..0x05FF] (4×128 B) byte-for-byte equals the reference boot
 *     sectors disks/bootsec.bin[0:512] (== cpadisk01.img[0:512]).
 */
TEST(BootIntegration, LoadsBootSectorsWithSYLSignature) {
    const std::string img = diskPath("cpadisk01.img");
    const std::string ref = diskPath("bootsec.bin");

    std::vector<uint8_t> expected = readFile(ref);
    ASSERT_GE(expected.size(), static_cast<size_t>(kBootBytes))
        << "reference boot image not found or too small: " << ref;

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, img, "cpa780", /*wp=*/false))
        << "could not mount " << img << ": " << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runUntilDmaComplete(machine))
        << "ZVE2 never signalled DMA completion ([0x03F8]=3) — boot DMA failed";

    // Signature bytes the boot ROM validates before jumping into loaded code.
    EXPECT_EQ(machine.memReadDebug(kLoadAddr + 0), 'S');
    EXPECT_EQ(machine.memReadDebug(kLoadAddr + 1), 'Y');
    EXPECT_EQ(machine.memReadDebug(kLoadAddr + 2), 'L');

    // All four boot sectors must match the reference exactly.
    int mismatches = 0;
    int first_bad  = -1;
    for (int i = 0; i < kBootBytes; ++i) {
        uint8_t got = machine.memReadDebug(static_cast<uint16_t>(kLoadAddr + i));
        if (got != expected[static_cast<size_t>(i)]) {
            if (first_bad < 0) first_bad = i;
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << "/" << kBootBytes << " loaded bytes differ from "
        << ref << "; first at offset " << first_bad;
}

/**
 * @test BootIntegration/ReachesLoadedBootCode
 * @brief After the DMA, ZVE1 validates the signature and jumps into loaded code.
 *
 * Pass criterion: ZVE1's PC reaches the loaded code region (>= 0x0400, < VRAM)
 * with the boot ROM unmapped — i.e. the bootstrap left the ROM and is executing
 * the freshly loaded boot record (entry at 0x0437).
 */
TEST(BootIntegration, ReachesLoadedBootCode) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();

    bool reached = false;
    for (int done = 0; done < 3'000'000; done += 2000) {
        machine.run(2000);
        uint16_t pc = machine.cpuPC();
        if (!machine.isRomEnabled() && pc >= 0x0400 && pc < 0xF800) {
            reached = true;
            break;
        }
    }
    EXPECT_TRUE(reached)
        << "ZVE1 never reached loaded boot code (PC>=0x0400) — boot did not complete";
}
