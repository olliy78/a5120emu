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

// ── Per-stage milestone helpers ─────────────────────────────────────────────
//
// The A5120 boot is a chain of stages, each handing off to the next:
//   Stage 0  ZRE boot ROM + ZVE2 DMA → "SYL" boot record at 0x0400, JP 0x0437
//   Stage 1  chained loader @0x0437  → banner "Bootloader, Version 24.02.87"
//   Stage 2  secondary loader        → loads tracks, JP 0x1800 into stage 3
//   Stage 3  CP/A bootsystem @0x1800 → banner "CP/A-Bootsystem …", reads @OS.COM
//            from the 1024B data area to 0x3780 and JP 0x37A0 into the OS.
// Each test below pins one stage's milestone so a regression localises to a stage.

// Run until ZVE1's PC equals `target` (with the boot ROM unmapped, i.e. executing
// loaded code), using the per-instruction trace callback so transient one-shot
// jump targets (0x0437, 0x1800, 0x37A0) are reliably caught — not just sampled.
bool runUntilPC(A5120Machine& m, uint16_t target, int max_cycles) {
    bool reached = false;
    m.setCpuTraceCallback([&](const Z80& z) {
        if (z.PC == target && !m.isRomEnabled()) reached = true;
    });
    constexpr int batch = 100'000;
    for (int done = 0; done < max_cycles && !reached; done += batch)
        m.run(batch);
    m.setCpuTraceCallback({});   // drop the lambda (it captures locals)
    return reached;
}

// Read the 2 KB text VRAM (0xF800–0xFFFF) as printable ASCII (non-printables → ' ').
std::string vramText(A5120Machine& m) {
    std::string s;
    s.reserve(0x800);
    for (int a = 0xF800; a <= 0xFFFF; ++a) {
        uint8_t c = m.memReadDebug(static_cast<uint16_t>(a));
        s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
    }
    return s;
}

// Run up to `max_cycles`, returning true as soon as `needle` appears in VRAM.
bool runUntilVramContains(A5120Machine& m, const std::string& needle, int max_cycles) {
    constexpr int batch = 100'000;
    for (int done = 0; done < max_cycles; done += batch) {
        m.run(batch);
        if (vramText(m).find(needle) != std::string::npos) return true;
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

// ─── Stage 1: chained loader @0x0437 + "Bootloader" banner ────────────────────

/**
 * @test BootIntegration/Stage1_ReachesChainedLoaderEntry
 * @brief After the boot ROM hands off, ZVE1 executes the stage-1 loader at 0x0437.
 * @par Pass criterion  ZVE1 PC reaches 0x0437 with the ROM unmapped.
 */
TEST(BootIntegration, Stage1_ReachesChainedLoaderEntry) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilPC(machine, 0x0437, 5'000'000))
        << "stage-1 loader entry 0x0437 never reached — boot ROM handoff failed";
}

/**
 * @test BootIntegration/Stage1_PrintsBootloaderBanner
 * @brief The stage-1 loader prints its banner to the text VRAM.
 * @par Pass criterion  VRAM contains "Version 24.02.87" within the cycle budget.
 */
TEST(BootIntegration, Stage1_PrintsBootloaderBanner) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilVramContains(machine, "Version 24.02.87", 5'000'000))
        << "stage-1 banner 'Bootloader, Version 24.02.87' never appeared in VRAM";
}

// ─── Stage 2: secondary loader → stage 3 entry (0x1800) ───────────────────────

/**
 * @test BootIntegration/Stage2_ReachesThirdStageEntry
 * @brief The secondary loader loads its tracks and jumps into stage 3 at 0x1800.
 * @par Pass criterion  ZVE1 PC reaches 0x1800 (ROM unmapped) within the budget.
 */
TEST(BootIntegration, Stage2_ReachesThirdStageEntry) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilPC(machine, 0x1800, 8'000'000))
        << "stage-3 entry 0x1800 never reached — secondary loader did not complete";
}

// ─── Stage 3: CP/A bootsystem — banner, @OS.COM load, OS handoff ──────────────

/**
 * @test BootIntegration/Stage3_PrintsCpaBootBanner
 * @brief Stage 3 (CP/A bootsystem) prints its banner.
 * @par Pass criterion  VRAM contains "CP/A-Bootsystem" within the budget.
 */
TEST(BootIntegration, Stage3_PrintsCpaBootBanner) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilVramContains(machine, "CP/A-Bootsystem", 8'000'000))
        << "stage-3 banner 'CP/A-Bootsystem …' never appeared in VRAM";
}

/**
 * @test BootIntegration/Stage3_StartsLoadingOsCom
 * @brief Stage 3 reads @OS.COM from the 1024B data area into RAM at 0x3780
 *        (OS entry 0x37A0). This asserts the load STARTS — the first records
 *        arrive as real code — which currently works even though the full load
 *        does not yet complete (see DISABLED_Stage3_FullyLoadsAndJumpsToOs).
 * @par Pass criterion  At least 64 non-zero bytes appear in [0x3780, 0x3C00)
 *        and the OS entry word at 0x37A0 is non-zero.
 */
TEST(BootIntegration, Stage3_StartsLoadingOsCom) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();

    // Run well into the @OS.COM read phase.
    for (int done = 0; done < 15'000'000; done += 100'000) machine.run(100'000);

    int nonzero = 0;
    for (int a = 0x3780; a < 0x3C00; ++a)
        if (machine.memReadDebug(static_cast<uint16_t>(a)) != 0) ++nonzero;
    EXPECT_GE(nonzero, 64)
        << "no @OS.COM data loaded at 0x3780 — data-area read never delivered records";
    EXPECT_NE(machine.memReadDebug(0x37A0), 0)
        << "OS entry area 0x37A0 is empty — @OS.COM did not start loading";
}

/**
 * @test BootIntegration/Stage3_FullyLoadsAndJumpsToOs
 * @brief End-to-end: stage 3 reads ALL of @OS.COM from the 1024B data area and
 *        jumps into the OS at 0x37A0.  Previously stalled: a 1024B data-area read
 *        derailed on cyl 3 head 0 sec 2 (error 'S') because the loader's byte-wise
 *        IDAM search mistook a 0xA1 *data* byte (cyl3/head0/sec1 has them) for an
 *        address-mark sync.  Fixed by modelling the loader's MK1 (ctrl Port A
 *        bit4) strobe as a data-separator re-lock that jumps mark-to-mark, so the
 *        search skips the data bytes (K5122::resyncToNextMark()).  The full
 *        @OS.COM now loads and ZVE1 reaches the OS entry; the screen shows the
 *        running CP/A banner ("CP/A, Version 25.09.89, TPA …").
 * @par Pass criterion  ZVE1 PC reaches the OS entry 0x37A0 (ROM unmapped).
 */
TEST(BootIntegration, Stage3_FullyLoadsAndJumpsToOs) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk01.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilPC(machine, 0x37A0, 40'000'000))
        << "OS entry 0x37A0 never reached — @OS.COM read did not complete";
}

// ─── cpadisk_02 — same disk in two encodings (.img raw + .hfe HxC) ────────────
//
// disks/cpadisk_02.img (raw cpa780) and disks/cpadisk_02.hfe (HxC HFE v1, produced
// by real HxC tooling — track_encoding=0xFF, treated as MFM) are two encodings of
// the SAME disk: a CP/A system *without* a real-time clock.  These two tests prove
// that the new K5122v2 controller boots BOTH formats through every stage into the
// running CP/A OS.  The HFE case additionally exercises the BitCodec/HfeImage
// backend end-to-end on a *real* (not synthetic) disk image, and that it boots
// byte-equivalently to the raw image (same OS banner, same TPA size 0C405H).
//
// Pass milestone: the running OS prints its own banner "CP/A, Version 25.09.89"
// — only the loaded OS prints this, so it proves the @OS.COM handoff (JP 0x37A0)
// succeeded and the OS is executing — and then completes its BIOS cold-boot init
// ("TPA ist OK!" after the RAM test).  Both encodings reach the exact same screen.
//
// KNOWN LIMIT — the disks' intended end state (auto-`dir` listing + interactive
// prompt) is currently NOT reached: right after "TPA ist OK!" the OS spins in a
// 16-bit divide routine (sub_C800 @0xC800) called with the divisor [0xD1BE]==0,
// so DE never grows and the loop never terminates.  Both .img and .hfe hang at the
// identical PC with the identical screen, i.e. this is a clock-/OS-runtime issue
// (the disks are "ohne Uhr"), NOT a floppy/boot data-path difference between the
// formats.  When that is resolved, tighten these tests to assert the dir output +
// prompt.  Diagnosis: doc/refactoring_floppy_emulator.md §15.5.

// Budget: "TPA ist OK!" appears well before this; runUntilVramContains stops early.
constexpr int kCpa02BudgetCycles = 90'000'000;

// Mount cpadisk_02 by name.  Raw .img and self-describing .hfe both mount via the
// same call; for HFE the "cpa780" format name is looked up (must exist) but ignored
// by the HFE backend, which reads geometry/encoding from the file header.
void mountCpa02(A5120Machine& m, const char* name) {
    ASSERT_TRUE(m.mountDisk(0, diskPath(name), "cpa780", /*wp=*/false))
        << "could not mount " << name << ": " << m.lastError();
}

/**
 * @test BootIntegrationCpa02/ImgBootsIntoRunningCpaOs
 * @brief cpadisk_02.img boots through all stages into the running CP/A OS.
 */
TEST(BootIntegrationCpa02, ImgBootsIntoRunningCpaOs) {
    A5120Machine machine;
    mountCpa02(machine, "cpadisk_02.img");
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kCpa02BudgetCycles))
        << "cpadisk_02.img: OS BIOS cold-boot (RAM test) never completed — boot failed";
    const std::string screen = vramText(machine);
    EXPECT_NE(screen.find("CP/A, Version 25.09.89"), std::string::npos)
        << "running CP/A OS banner missing — @OS.COM handoff did not run the OS";
    EXPECT_NE(screen.find("TPA 100H - 0C405H"), std::string::npos)
        << "expected TPA size 0C405H (this disk's OS) not on screen";
}

/**
 * @test BootIntegrationCpa02/HfeBootsIntoRunningCpaOs
 * @brief cpadisk_02.hfe (HxC HFE v1) boots identically to the raw .img — proves the
 *        HFE/BitCodec backend boots a real disk through K5122v2 end-to-end.
 */
TEST(BootIntegrationCpa02, HfeBootsIntoRunningCpaOs) {
    A5120Machine machine;
    mountCpa02(machine, "cpadisk_02.hfe");
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kCpa02BudgetCycles))
        << "cpadisk_02.hfe: OS BIOS cold-boot never completed — HFE boot path failed";
    const std::string screen = vramText(machine);
    EXPECT_NE(screen.find("CP/A, Version 25.09.89"), std::string::npos)
        << "running CP/A OS banner missing from HFE boot";
    EXPECT_NE(screen.find("TPA 100H - 0C405H"), std::string::npos)
        << "HFE boot reached a different OS than the raw image (TPA size differs)";
}

// ─── Booting from drives B: and C: (search starts at A:, lower drives empty) ──
//
// disks/cpadisk_mitUhr_01.{img,hfe} are the same disk in two encodings, WITH an
// active real-time clock — so (unlike cpadisk_02, §15.5) the OS does not hang in
// the post-boot divide loop and boots all the way to the "Bitte Uhrzeit eingeben!"
// time-entry prompt.  These tests additionally exercise the loader + the K5122v2
// 8212 drive-select for drives OTHER than A:: the disk is mounted on B: (drive 1)
// or C: (drive 2) with the lower drives left empty.  The ZRE boot ROM's drive-
// detect loop (0x0110) rotates the 8212 select 0xEE→0xDD→0xBB→0x77 (A:→B:→C:→D:,
// L0140) and boots from the first drive that holds a disk — so the empty lower
// drives are skipped and the disk in B:/C: is found.
//
// Budget: each empty lower drive costs the ROM ~80 not-ready retries with a step
// delay (a few million cycles), so C: (two empty drives below it) reaches the
// prompt later than B:.  runUntilVramContains stops as soon as the prompt appears.
constexpr int kClockBootBudget = 90'000'000;

// Boot cpadisk_mitUhr_01 from a non-A: drive, leaving the lower drives empty (a
// fresh A5120Machine has nothing mounted — exactly the configuration to exercise
// the drive search).  Passes when the OS reaches the time-entry prompt.
void bootClockFromDrive(int drive, const char* name) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(drive, diskPath(name), "cpa780", /*wp=*/false))
        << "could not mount " << name << " on drive " << drive
        << ": " << machine.lastError();
    machine.powerOn();
    EXPECT_TRUE(runUntilVramContains(machine, "Bitte Uhrzeit eingeben!", kClockBootBudget))
        << name << " on drive " << drive
        << " never reached the time-entry prompt — boot from a non-A: drive failed";
}

/** @test BootIntegrationDriveBC/ClockImg_FromDriveB  Raw image boots from B:. */
TEST(BootIntegrationDriveBC, ClockImg_FromDriveB) {
    bootClockFromDrive(1, "cpadisk_mitUhr_01.img");
}
/** @test BootIntegrationDriveBC/ClockImg_FromDriveC  Raw image boots from C:. */
TEST(BootIntegrationDriveBC, ClockImg_FromDriveC) {
    bootClockFromDrive(2, "cpadisk_mitUhr_01.img");
}
/** @test BootIntegrationDriveBC/ClockHfe_FromDriveB  HFE image boots from B:. */
TEST(BootIntegrationDriveBC, ClockHfe_FromDriveB) {
    bootClockFromDrive(1, "cpadisk_mitUhr_01.hfe");
}
/** @test BootIntegrationDriveBC/ClockHfe_FromDriveC  HFE image boots from C:. */
TEST(BootIntegrationDriveBC, ClockHfe_FromDriveC) {
    bootClockFromDrive(2, "cpadisk_mitUhr_01.hfe");
}
