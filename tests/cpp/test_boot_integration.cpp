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
#include "core/cards/k7024/chargen_zg1.h"   // CHARGEN_ZG1_LATIN — Pixelzeilen 0–7
#include "core/cards/k7024/chargen_zg2.h"   // CHARGEN_ZG2_LATIN — Pixelzeilen 8–11

#include <cstdint>
#include <filesystem>
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

// Run a fixed number of cycles in small batches (so queued keys are drained).
void runCycles(A5120Machine& m, long long cycles) {
    for (long long done = 0; done < cycles; done += 5000) m.run(5000);
}

// Like runUntilVramContains but in small (5000-cycle) batches — the keyboard
// path is timing-sensitive (timer-ISR-driven scan + 9600-baud serial), and the
// 100k-cycle batches drift the CTC timer/clock enough to disturb it.  Mirrors
// the cadence kbd_test uses.
bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long done = 0; done < max_cycles; done += 5000) {
        m.run(5000);
        if (vramText(m).find(needle) != std::string::npos) return true;
    }
    return false;
}

// Qt keycode for Return (matches K7637::QK_RETURN).
constexpr uint32_t QK_RETURN = 0x01000004;

// Press + release one key, giving the BIOS time to scan it (the K7637 models a
// 9600-baud link, so a key takes ~one byte-time to arrive at the SIO).
void typeKey(A5120Machine& m, uint32_t kc) {
    m.keyPress(kc, false, false);
    runCycles(m, 1'000'000);
    m.keyRelease(kc);
    runCycles(m, 300'000);
}
void typeString(A5120Machine& m, const std::string& s) {
    for (char c : s) typeKey(m, static_cast<uint8_t>(c));
}

}  // namespace

/**
 * @test BootIntegration/LoadsBootSectorsWithSYLSignature
 * @brief A5120 boots cpadisk_autofs_clock_noautoexec.img and ZVE2 DMAs the 4 boot sectors to 0x0400.
 *
 * Pass criteria:
 *   - ZVE2 signals completion ([0x03F8]=3) within the cycle budget.
 *   - RAM[0x0400..0x0402] == "SYL" (the boot-record signature ZVE1 checks at 0x01B6).
 *   - RAM[0x0400..0x05FF] (4×128 B) byte-for-byte equals the reference boot
 *     sectors disks/bootsec.bin[0:512] (== cpadisk_autofs_clock_noautoexec.img[0:512]).
 */
TEST(BootIntegration, LoadsBootSectorsWithSYLSignature) {
    const std::string img = diskPath("cpadisk_autofs_clock_noautoexec.img");
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
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
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", false));
    machine.powerOn();
    EXPECT_TRUE(runUntilPC(machine, 0x37A0, 40'000'000))
        << "OS entry 0x37A0 never reached — @OS.COM read did not complete";
}

// ─── cpadisk_autofs_noclk_noautoexec — same disk in two encodings (.img raw + .hfe HxC) ────────────
//
// disks/cpadisk_autofs_noclk_noautoexec.img (raw cpa780) and disks/cpadisk_autofs_noclk_noautoexec.hfe (HxC HFE v1, produced
// by real HxC tooling — track_encoding=0xFF, treated as MFM) are two encodings of
// the SAME disk: a CP/A system *without* a real-time clock.  These two tests prove
// that the new K5122 controller boots BOTH formats through every stage into the
// running CP/A OS.  The HFE case additionally exercises the BitCodec/HfeImage
// backend end-to-end on a *real* (not synthetic) disk image, and that it boots
// byte-equivalently to the raw image (same OS banner, same TPA size 0C405H).
//
// Pass milestone: the running OS prints its own banner "CP/A, Version 25.09.89"
// — only the loaded OS prints this, so it proves the @OS.COM handoff (JP 0x37A0)
// succeeded and the OS is executing — and then completes its BIOS cold-boot init
// ("TPA ist OK!" after the RAM test).  Both encodings reach the exact same screen.
//
// Da diese Disk *ohne* Uhr ist (kein "Bitte Uhrzeit eingeben!"-Prompt), läuft der
// Kaltstart direkt bis zum interaktiven CCP "A>" durch — die Tests stoppen aber
// früh bei "TPA ist OK!" (runUntilVramContains hält an), das ist als Boot-Nachweis
// ausreichend und schnell.

// Budget: "TPA ist OK!" appears well before this; runUntilVramContains stops early.
constexpr int kCpa02BudgetCycles = 90'000'000;

// Mount cpadisk_autofs_noclk_noautoexec by name.  Raw .img and self-describing .hfe both mount via the
// same call; for HFE the "cpa780" format name is looked up (must exist) but ignored
// by the HFE backend, which reads geometry/encoding from the file header.
void mountCpa02(A5120Machine& m, const char* name) {
    ASSERT_TRUE(m.mountDisk(0, diskPath(name), "cpa780", /*wp=*/false))
        << "could not mount " << name << ": " << m.lastError();
}

/**
 * @test BootIntegrationCpa02/ImgBootsIntoRunningCpaOs
 * @brief cpadisk_autofs_noclk_noautoexec.img boots through all stages into the running CP/A OS.
 */
TEST(BootIntegrationCpa02, ImgBootsIntoRunningCpaOs) {
    A5120Machine machine;
    mountCpa02(machine, "cpadisk_autofs_noclk_noautoexec.img");
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kCpa02BudgetCycles))
        << "cpadisk_autofs_noclk_noautoexec.img: OS BIOS cold-boot (RAM test) never completed — boot failed";
    const std::string screen = vramText(machine);
    EXPECT_NE(screen.find("CP/A, Version 25.09.89"), std::string::npos)
        << "running CP/A OS banner missing — @OS.COM handoff did not run the OS";
    EXPECT_NE(screen.find("TPA 100H - 0C405H"), std::string::npos)
        << "expected TPA size 0C405H (this disk's OS) not on screen";
}

// ── Display-Pfad end-to-end: gerenderter Framebuffer == Zeichengenerator(VRAM) ──
//
// Die Banner-Tests oben prüfen nur die VRAM-*Zeichencodes* (0xF800-Bytes), nicht
// die tatsächlich gerenderten Pixel. Ein falscher Zeichengenerator (die drei
// historischen Charset-Bugs) ließe sie alle grün. Dieser Test schließt die Lücke:
// nachdem das OS-Banner steht, wird für jede Zelle der Banner-Zeile der gerenderte
// Framebuffer-Block gegen die Chargen-Erwartung aus VRAM verglichen — Beweis, dass
// der komplette Pfad VRAM → renderChar → Framebuffer korrekt verdrahtet ist.

// Spiegelt K7024::chargenLookupLatin (dort static/intern): 8-Bit-Pixelzeile
// (Bit7 = links) für Code+Pixelzeile aus dem verifizierten v171/v172-Satz.
static uint8_t expectedGlyphRow(uint8_t charCode, int pixelRow) {
    uint8_t code = charCode & 0x7F;
    if (pixelRow >= 12 || code < 0x20) return 0x00;
    if (pixelRow < 8) return CHARGEN_ZG1_LATIN[code * 8 + pixelRow];
    return CHARGEN_ZG2_LATIN[code * 8 + (pixelRow - 8)];
}

/**
 * @test BootIntegrationCpa02/RenderedBannerMatchesCharacterGenerator
 * @brief Der gerenderte Framebuffer der OS-Banner-Zeile stimmt Pixel-für-Pixel mit dem Zeichengenerator überein.
 * @details Selbstkonsistenz-Prüfung des Display-Pfads: für die 80 Zellen der Zeile mit
 *   "CP/A, Version 25.09.89" wird jede der 12 Pixelzeilen aus dem Framebuffer mit
 *   chargenLookupLatin(VRAM-Code) verglichen (inkl. Cursor-Invertierung Zeilen 10–11).
 * @par Pass criterion  Alle 80×12 Pixelzeilen der Banner-Zeile == Zeichengenerator-Erwartung.
 */
TEST(BootIntegrationCpa02, RenderedBannerMatchesCharacterGenerator) {
    A5120Machine machine;
    mountCpa02(machine, "cpadisk_autofs_noclk_noautoexec.img");
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "CP/A, Version 25.09.89", kCpa02BudgetCycles))
        << "OS-Banner nie erschienen — Boot fehlgeschlagen";

    // Banner-Zeile (0–23) suchen, deren 80-Zeichen-Text das Banner enthält.
    int bannerRow = -1;
    for (int row = 0; row < 24 && bannerRow < 0; ++row) {
        std::string line;
        for (int col = 0; col < 80; ++col) {
            uint8_t c = machine.memReadDebug(static_cast<uint16_t>(0xF800 + row * 80 + col)) & 0x7F;
            line.push_back((c >= 0x20 && c < 0x7F) ? char(c) : ' ');
        }
        if (line.find("CP/A, Version 25.09.89") != std::string::npos) bannerRow = row;
    }
    ASSERT_GE(bannerRow, 0) << "Banner-Zeile in VRAM nicht lokalisiert";

    const uint8_t* fb = machine.framebuffer();
    int mismatches = 0;
    for (int col = 0; col < 80; ++col) {
        uint8_t vbyte  = machine.memReadDebug(static_cast<uint16_t>(0xF800 + bannerRow * 80 + col));
        uint8_t code   = vbyte & 0x7F;
        bool    cursor = (vbyte & 0x80) != 0;
        for (int pr = 0; pr < 12; ++pr) {
            uint8_t exp = expectedGlyphRow(code, pr);
            if (cursor && pr >= 10) exp ^= 0xFF;   // Cursor invertiert Zeilen 10–11
            int fb_y = bannerRow * 12 + pr;
            uint8_t got = 0;
            for (int px = 0; px < 8; ++px)
                if (fb[fb_y * 640 + col * 8 + px]) got |= static_cast<uint8_t>(1u << (7 - px));
            EXPECT_EQ(got, exp)
                << "Framebuffer weicht vom Zeichengenerator ab: Zelle col=" << col
                << " code=0x" << std::hex << int(code) << std::dec
                << " Pixelzeile " << pr;
            if (got != exp) ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << " Pixelzeilen der Banner-Zeile stimmen nicht mit dem Zeichengenerator überein";
}

/**
 * @test BootIntegrationCpa02/HfeBootsIntoRunningCpaOs
 * @brief cpadisk_autofs_noclk_noautoexec.hfe (HxC HFE v1) boots identically to the raw .img — proves the
 *        HFE/BitCodec backend boots a real disk through K5122 end-to-end.
 */
TEST(BootIntegrationCpa02, HfeBootsIntoRunningCpaOs) {
    A5120Machine machine;
    mountCpa02(machine, "cpadisk_autofs_noclk_noautoexec.hfe");
    machine.powerOn();

    ASSERT_TRUE(runUntilVramContains(machine, "TPA ist OK!", kCpa02BudgetCycles))
        << "cpadisk_autofs_noclk_noautoexec.hfe: OS BIOS cold-boot never completed — HFE boot path failed";
    const std::string screen = vramText(machine);
    EXPECT_NE(screen.find("CP/A, Version 25.09.89"), std::string::npos)
        << "running CP/A OS banner missing from HFE boot";
    EXPECT_NE(screen.find("TPA 100H - 0C405H"), std::string::npos)
        << "HFE boot reached a different OS than the raw image (TPA size differs)";
}

// ─── Booting from drives B: and C: (search starts at A:, lower drives empty) ──
//
// disks/cpadisk_autofs_clock_noautoexec.{img,hfe} are the same disk in two encodings, WITH an
// active real-time clock — so (unlike the no-clock disk above) the OS boots all the
// way to the "Bitte Uhrzeit eingeben!" time-entry prompt instead of straight to the
// CCP.  These tests additionally exercise the loader + the K5122
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

// Boot cpadisk_autofs_clock_noautoexec from a non-A: drive, leaving the lower drives empty (a
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
    bootClockFromDrive(1, "cpadisk_autofs_clock_noautoexec.img");
}
/** @test BootIntegrationDriveBC/ClockImg_FromDriveC  Raw image boots from C:. */
TEST(BootIntegrationDriveBC, ClockImg_FromDriveC) {
    bootClockFromDrive(2, "cpadisk_autofs_clock_noautoexec.img");
}
/** @test BootIntegrationDriveBC/ClockHfe_FromDriveB  HFE image boots from B:. */
TEST(BootIntegrationDriveBC, ClockHfe_FromDriveB) {
    bootClockFromDrive(1, "cpadisk_autofs_clock_noautoexec.hfe");
}
/** @test BootIntegrationDriveBC/ClockHfe_FromDriveC  HFE image boots from C:. */
TEST(BootIntegrationDriveBC, ClockHfe_FromDriveC) {
    bootClockFromDrive(2, "cpadisk_autofs_clock_noautoexec.hfe");
}

// ─── Voller CP/A-Boot bis zur Uhrzeit-Eingabe (per-Byte-/BUSRQ-Modell) ────────
//
// Kanonischer „bootet vollständig durch"-Test: cpadisk_autofs_clock_noautoexec (Boot-Disk MIT
// Echtzeituhr, Format = cpa780 mit aktiver autom. Formaterkennung im Config) wird
// auf A: (Laufwerk 0) gemountet und durchläuft ALLE Loader-Stufen (Boot-ROM →
// Sekundärlader → 3. Stufe @OS.COM → CP/A-Kaltstart) bis zur Aufforderung
// "Bitte Uhrzeit eingeben!".  Das ist der erwartete interaktive Endzustand des
// Kaltstarts und der Regressionswächter für das hardware-echte per-Byte-/BUSRQ-
// Modell der K5122 (07_k5122_afs.md §7.2): bricht eine Loader-Stufe oder die
// Bus-Verriegelung, erreicht die VRAM diesen Text nie.
//
// Von A: entfällt der 8212-Laufwerkssuchlauf (B:/C:-Tests oben), daher deutlich
// schneller als kClockBootBudget.
TEST(BootIntegration, FullBootReachesTimeEntryPrompt) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", /*wp=*/false))
        << "could not mount cpadisk_autofs_clock_noautoexec.img on A:: " << machine.lastError();
    machine.powerOn();
    EXPECT_TRUE(runUntilVramContains(machine, "Bitte Uhrzeit eingeben!", 40'000'000))
        << "voller CP/A-Kaltstart erreichte die Uhrzeit-Eingabe nie — eine Loader-Stufe "
           "oder die per-Byte-/BUSRQ-Verriegelung ist gebrochen";
}

// ─── Neustart aus dem laufenden Betrieb: Reset & Power-Cycle ────────────────
//
// Beide Wege der GUI (Reset-Taste, Power OFF→ON) starten die Maschine aus einem
// bereits gebooteten CP/A neu.  Der Knackpunkt ist die Speicher-Ausbaumessung des
// Lade-ROMs (0040H–005AH): sie schreibt ein Testbyte, lässt sich vom /WR-Strobe an
// BS-PIO Port A unterbrechen und prüft in der ISR, ob das Byte angekommen ist.
// Wurde /WR als DAUERND aktiv modelliert, kam der Interrupt schon beim `EI` davor,
// die ISR verglich den ALTEN Speicherinhalt — bei frischem DRAM (0xFF) unauffällig,
// nach einem Neustart aus dem Betrieb (echte Daten im RAM) meldete die Messung
// „kein Speicher" und der Neustart lief in Zufallscode.  Beide Tests booten daher
// erst vollständig und starten DANN neu.
namespace {
// Meilenstein des VOLLSTÄNDIGEN Neustarts (@OS.COM läuft und meldet sich).
constexpr const char* kRebootNeedle = "Bitte Uhrzeit eingeben!";

// Bildschirmspeicher vor dem Neustart unkenntlich machen.  Nötig, weil der
// Textbildschirm für die CPU schreib-only ist: Lesen (und damit vramText())
// bedient das K3526-Schattenram, das reset() NICHT löscht — ohne dieses Wischen
// würde der Text des vorigen Laufs sofort „gefunden" und der Test wäre blind.
void blankVram(A5120Machine& m) {
    for (int a = 0xF800; a <= 0xFFFF; ++a)
        m.memWriteDebug(static_cast<uint16_t>(a), 0x00);
    ASSERT_EQ(vramText(m).find(kRebootNeedle), std::string::npos)
        << "Bildschirm-Wischen wirkungslos — der Neustart-Test wäre blind";
}

// Die 62 Prüfzellen der ROM-Ausbaumessung (0800H, +400H …) mit „benutztem" RAM
// belegen.  Sie ist die Stelle, an der ein Neustart aus dem Betrieb scheiterte:
// die ISR vergleicht die Zelle gegen FFH, und stand ihr Interrupt VOR dem
// Testschreiben, entschied der Altinhalt über „Speicher da/nicht da".  Ein frisch
// gebootetes CP/A hinterlässt dort zufällig FFH — ohne dieses Verschmutzen liefe
// der Test auch mit dem Fehler durch.
void dirtyMemorySizingProbes(A5120Machine& m) {
    for (int i = 0; i < 62; ++i)
        m.memWriteDebug(static_cast<uint16_t>(0x0800 + i * 0x0400), 0x5A);
}

// Vollständiger Kaltstart bis zur Uhrzeit-Eingabe (gemeinsamer Vorlauf beider Tests).
void bootToTimePrompt(A5120Machine& m) {
    ASSERT_TRUE(m.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", /*wp=*/false))
        << "could not mount cpadisk_autofs_clock_noautoexec.img on A:: " << m.lastError();
    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, kRebootNeedle, 40'000'000))
        << "Kaltstart erreichte die Uhrzeit-Eingabe nie";
}
}  // namespace

TEST(BootIntegration, ResetFromRunningSystemRebootsFromRom) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();
    // Reset mitten im Betrieb — hier, während das CP/A-Bootsystem @OS.COM lädt.
    // Bewusst NICHT am fertigen Prompt: ein durchgebootetes CP/A hinterlässt in den
    // Prüfzellen der Ausbaumessung zufällig FFH und fängt sich selbst mit defekter
    // Messung noch über Restcode ab — der Fehler wäre unsichtbar.
    ASSERT_TRUE(runUntilVramContains(machine, "CP/A-Bootsystem", 40'000'000))
        << "Kaltstart erreichte das CP/A-Bootsystem nie";

    ASSERT_NO_FATAL_FAILURE(blankVram(machine));
    // Genau die Reihenfolge der GUI-Reset-Taste: Images neu mounten (setzt den
    // K5122-Laufwerkszustand zurück), dann reset().  RAM behält dabei den Inhalt
    // des laufenden OS — anders als beim Netz-Aus.
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", /*wp=*/false))
        << machine.lastError();
    machine.reset();
    // Die ganze Boot-Kette muss erneut laufen: Stufe 3 (0x1800) ist der eindeutige
    // Meilenstein — mit defekter Ausbaumessung endet der Neustart vorher in
    // Zufallscode (Stufe 1/2 werden noch erreicht).
    EXPECT_TRUE(runUntilPC(machine, 0x1800, 40'000'000))
        << "Reset aus dem laufenden CP/A erreichte Boot-Stufe 3 nicht (Ausbaumessung "
           "des Lade-ROMs auf benutztem RAM?)";
    EXPECT_TRUE(runUntilVramContains(machine, kRebootNeedle, 40'000'000))
        << "Reset aus dem laufenden CP/A bootete nicht durch:\n" << vramText(machine);
}

// Der vom Nutzer gemeldete Fall: Uhrzeit bestätigen, am `A>`-Prompt stehen, dann
// Reset bzw. Power OFF→ON.  Hier läuft der System-CTC (Q302) des OS mit eigener
// IM2-Vektorbasis (F8H) und freigegebenen Interrupts; ohne systemweiten /RESET
// aller Bausteine schickt der erste Timer-Interrupt nach dem `EI` des Lade-ROMs
// die neue Boot-Kette auf einen Fantasie-Vektor aus der ROM-Seite 0.
TEST(BootIntegration, RestartFromInteractivePromptRebootsFromRom) {
    for (bool power_cycle : {false, true}) {
        SCOPED_TRACE(power_cycle ? "Power OFF→ON" : "Reset-Taste");
        A5120Machine machine;
        ASSERT_NO_FATAL_FAILURE(bootToTimePrompt(machine));

        // Uhrzeit bestätigen → CP/A erreicht den interaktiven `A>`-Prompt.
        typeString(machine, "12:00:00");
        typeKey(machine, QK_RETURN);
        ASSERT_TRUE(runSmallUntil(machine, "A>", 20'000'000))
            << "CP/A erreichte nach der Uhrzeit-Eingabe den A>-Prompt nicht:\n"
            << vramText(machine);

        ASSERT_NO_FATAL_FAILURE(blankVram(machine));
        ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"),
                                      "cpa780", /*wp=*/false)) << machine.lastError();
        if (power_cycle) machine.powerOn(); else machine.reset();

        EXPECT_TRUE(runUntilPC(machine, 0x1800, 40'000'000))
            << "Neustart vom A>-Prompt erreichte Boot-Stufe 3 nicht (laufender "
               "System-CTC des alten OS nicht zurückgesetzt?)";
        EXPECT_TRUE(runUntilVramContains(machine, kRebootNeedle, 40'000'000))
            << "Neustart vom A>-Prompt bootete nicht durch:\n" << vramText(machine);
    }
}

TEST(BootIntegration, PowerCycleFromRunningOsRebootsFromRom) {
    A5120Machine machine;
    ASSERT_NO_FATAL_FAILURE(bootToTimePrompt(machine));

    dirtyMemorySizingProbes(machine);
    ASSERT_NO_FATAL_FAILURE(blankVram(machine));
    machine.powerOn();   // Netz aus/ein: DRAM verliert seinen Inhalt (0xFF)
    EXPECT_EQ(machine.memReadDebug(0x8000), 0xFF)
        << "powerOn() liess alten RAM-Inhalt stehen — kein echter Kaltstart";
    EXPECT_TRUE(runUntilPC(machine, 0x1800, 40'000'000))
        << "Power-Cycle aus dem laufenden CP/A erreichte Boot-Stufe 3 nicht";
    EXPECT_TRUE(runUntilVramContains(machine, kRebootNeedle, 40'000'000))
        << "Power-Cycle aus dem laufenden CP/A bootete nicht neu:\n" << vramText(machine);
}

// ─── Keyboard input at the interactive CCP (end-to-end) ──────────────────────
//
// Exercises the full keyboard path that the K7637 serial-latency fix unblocked
// (2026-06-18): GUI keyPress → key queue → K7637 9600-baud serial timing →
// K8025 SIO RX → BIOS keyboard ISR scan → keyboard buffer (0xF6D9) → CCP CONIN
// → echo + command processing.  Before the fix the keyboard's type-code acks
// appeared instantly and raced the LED handshake, flooding the buffer so no key
// reached the CCP.  This types the clock, then a command at the A> prompt and
// checks that it is echoed AND processed (unknown command → "XY7?").
//
// DISABLED: the fix is verified end-to-end manually and reproducibly with
//   build/kbd_test disks/cpadisk_autofs_clock_noautoexec.img "120000|Xy7"
// → screen shows `A>Xy7` then `XY7?`.  But the *gtest* harness cannot reproduce
// the interactive-CCP keyboard path reliably: the RTC clock in the status line
// drifts to garbage (e.g. "E6:DA:56" instead of advancing from 12:00:00) and the
// CCP then drops the command, while time-entry (a different OS input loop) does
// work.  This is a global-state / timer-ISR timing peculiarity of the test
// environment (the same one that blocked an automated `dir` test), not a
// keyboard regression.  Kept here, disabled, to document the intended check and
// re-enable once the harness clock issue is understood.  The serial-latency
// mechanism itself is regression-guarded by the K7637 unit tests.
TEST(KeyboardIntegration, DISABLED_TypeCommandAtCcpEchoesAndProcesses) {
    A5120Machine machine;
    machine.powerOn();   // power on BEFORE mounting (matches the kbd_test order)
    ASSERT_TRUE(machine.mountDisk(0, diskPath("cpadisk_autofs_clock_noautoexec.img"), "cpa780", /*wp=*/false))
        << "could not mount cpadisk_autofs_clock_noautoexec.img: " << machine.lastError();

    // 1. Boot to the time-entry prompt (small batches — see runSmallUntil).
    ASSERT_TRUE(runSmallUntil(machine, "Bitte Uhrzeit eingeben!", 40'000'000))
        << "time-entry prompt not reached";
    runCycles(machine, 2'000'000);   // settle into the time-input read

    // 2. Enter the time, confirm with Return — this proves digit keys work and
    //    advances the OS to the CCP.
    typeString(machine, "120000");
    typeKey(machine, QK_RETURN);

    // 3. Wait for the interactive A> prompt (after the post-login auto-start).
    ASSERT_TRUE(runSmallUntil(machine, "A>", 60'000'000))
        << "interactive A> prompt not reached after time entry";
    runCycles(machine, 12'000'000);  // let the auto-start settle back to A>

    // 4. Type a command mixing UPPER + lower + digit, then Return.
    typeString(machine, "Xy7");
    typeKey(machine, QK_RETURN);
    runCycles(machine, 25'000'000);  // let the CCP echo + process

    const std::string screen = vramText(machine);
    // Echo proves upper 'X', lower 'y' and digit '7' all reached the CCP as typed.
    EXPECT_NE(screen.find("Xy7"), std::string::npos)
        << "typed command was not echoed at the CCP — keyboard input is broken.\n"
        << "Screen:\n" << screen;
    // The CCP upper-cases and looks up the command; unknown → "<CMD>?".
    EXPECT_NE(screen.find("XY7?"), std::string::npos)
        << "CCP did not process the typed command (no unknown-command error).\n"
        << "Screen:\n" << screen;
}

// ─── SCPX 1526 — Boot + interaktives DIR/STAT/PIP (.COM-Laden über den Held-Bus) ─────
//
// disks/scpx_boot.hfe (SCPX 1526 V1.7, ROBOTRON-Loader / SYL-Format) bootet vollautomatisch
// bis zum interaktiven A>-Prompt (KEIN Uhrzeit-Prompt).  Dieser Test übt den *Laufzeit*-
// Lesepfad: nach dem Prompt werden drei Programme ausgeführt — DIR (CCP-Built-in) sowie
// STAT und PIP (transiente .COM-Dateien, die von Diskette geladen werden).
//
// Regressionswächter für den os-gated „gehaltenen Bus" (core/machines/a5120/a5120.cpp run(),
// doc/analyse_scpx_com_load.md §9.4b/§10): STAT/PIP luden früher NICHT (`SCPX ERR ON A: BAD
// SECTOR`), weil ZVE1 im Per-Byte-/BUSRQ-Modell in den Byte-Lücken des Laufzeit-Reads mitlief
// (Kopf-Divergenz / Matcher-INT-Korruption / verfrühter [0x0000]-Restore).  Sobald der Prompt
// (ZVE1 @E079) einmal erreicht ist, hält der Emulator /BUSRQ über den ganzen Laufzeit-Read
// (nur ZVE2 liest) — bricht das, meldet STAT wieder BAD SECTOR und "Space: 522k" erscheint nie.
//
// Anders als der DISABLED CP/A-Keyboard-Test unten ist die interaktive CCP-Eingabe hier stabil:
// SCPX hat keine driftende RTC-Statuszeile (der Grund, aus dem jener Test flakt).
TEST(ScpxIntegration, BootThenDirStatPipLoadComFiles) {
    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("scpx_boot.hfe"), "cpa780", /*wp=*/false))
        << "konnte scpx_boot.hfe nicht mounten: " << machine.lastError();
    machine.powerOn();

    // 1. Vollautomatischer Boot bis SCPX-Banner + A>-Prompt.
    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000))
        << "SCPX-Banner nie erschienen — Boot der scpx_boot.hfe gebrochen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000))
        << "A>-Prompt nach dem Banner nie erreicht";
    runCycles(machine, 2'000'000);   // in die CONIN-Leseschleife einschwingen

    // 2. DIR (CCP-Built-in): listet das Directory (BIOSG617 SYS ist ein distinktiver Eintrag).
    typeString(machine, "DIR");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "BIOSG617", 30'000'000))
        << "DIR listete das Directory nicht (Eintrag BIOSG617 SYS fehlt)";

    // 3. STAT.COM: wird geladen + ausgeführt → "A: R/W, Space: 522k".  Kern-Regressionscheck:
    //    scheitert der Held-Bus, endet STAT mit BAD SECTOR und diese Zeile erscheint nie.
    typeString(machine, "STAT");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "Space: 522k", 30'000'000))
        << "STAT lud/lief nicht (kein 'Space: 522k') — vermutlich BAD SECTOR: "
           "os-gated Held-Bus für Laufzeit-Reads gebrochen";

    // 4. PIP.COM: wird geladen → zeigt seinen '*'-Prompt (früher korrupt als '.>PIP').
    typeString(machine, "PIP");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "*", 30'000'000))
        << "PIP lud nicht (kein '*'-Prompt) — Laufzeit-.COM-Laden gebrochen";

    // STAT-Ausgabe steht weiterhin auf dem Schirm (nichts weggescrollt): finaler Sanity-Check.
    EXPECT_NE(vramText(machine).find("Space: 522k"), std::string::npos)
        << "STAT-Ausgabe unerwartet verschwunden:\n" << vramText(machine);
}

// ─── SCPX Laufzeit-SCHREIBEN: ERA löscht auf B: OHNE „BAD SECTOR" ─────────────
//
// Regressionswächter für den SCPX-Write-Fix (K5122 post_write_grace_ +
// releaseHeldRead-Guard, a5120 endPostWriteGrace + ZVE1-INT-Guard SP∈[EC00,EC10];
// s. doc/analyse_scpx_com_load.md §11).  VOR dem Fix meldete JEDER Laufzeit-Write
// auf B: „SCPX ERR ON B: BAD SECTOR" (der os-gated „gehaltene Bus" deckte nur den
// Read-, nicht den Post-Write-Verify-Pfad ab → ZVE2 blieb im Reset, ein Index-ISR
// kaperte den Handshake-Vektor).  Der Test bootet SCPX, löscht STAT.COM auf B:
// (schreibender Directory-Zugriff) und prüft, dass KEIN BAD SECTOR erscheint und die
// Datei tatsächlich weg ist (gefiltertes DIR → „NO FILE").
//
// Beide Laufwerke werden aus BESCHREIBBAREN TEMP-KOPIEN gemountet, damit die
// committete Fixture disks/scpx_boot.hfe garantiert unangetastet bleibt.
TEST(ScpxIntegration, EraDeletesFileOnDriveBWithoutBadSector) {
    namespace fs = std::filesystem;
    const std::string aPath = (fs::temp_directory_path() / "scpx_write_guard_A.hfe").string();
    const std::string bPath = (fs::temp_directory_path() / "scpx_write_guard_B.hfe").string();
    fs::copy_file(diskPath("scpx_boot.hfe"), aPath, fs::copy_options::overwrite_existing);
    fs::copy_file(diskPath("scpx_boot.hfe"), bPath, fs::copy_options::overwrite_existing);

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, aPath, "cpa780", /*wp=*/false)) << machine.lastError();
    ASSERT_TRUE(machine.mountDisk(1, bPath, "cpa780", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000))
        << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);   // in die CONIN-Leseschleife einschwingen

    // Baseline: STAT.COM liegt auf B: (gefiltertes DIR listet „B: STAT     COM" —
    // das Leerzeichen nach „B:" unterscheidet die Listing-Zeile vom Kommando-Echo).
    typeString(machine, "DIR B:STAT.COM");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "B: STAT", 30'000'000))
        << "STAT.COM nicht auf B: (Baseline gebrochen):\n" << vramText(machine);

    // Schreibender Zugriff: STAT.COM auf B: löschen.  Der eigentliche Regressionscheck.
    typeString(machine, "ERA B:STAT.COM");
    typeKey(machine, QK_RETURN);
    runCycles(machine, 30'000'000);   // Write + Post-Write-Verify abwarten
    EXPECT_EQ(vramText(machine).find("BAD SECTOR"), std::string::npos)
        << "ERA meldete BAD SECTOR — SCPX-Write-Fix gebrochen:\n" << vramText(machine);

    // Die Datei ist tatsächlich gelöscht: gefiltertes DIR → „NO FILE".
    typeString(machine, "DIR B:STAT.COM");
    typeKey(machine, QK_RETURN);
    EXPECT_TRUE(runSmallUntil(machine, "NO FILE", 30'000'000))
        << "STAT.COM nach ERA nicht gelöscht (kein 'NO FILE'):\n" << vramText(machine);

    machine.unmountDisk(0);
    machine.unmountDisk(1);
    std::error_code ec;
    fs::remove(aPath, ec);
    fs::remove(bPath, ec);
}

// ─── SCPX Fremdformat-Read friert NICHT ein (held-bus No-Progress-Watchdog) ──
//
// Regressionswächter für den No-Progress-Watchdog im gehaltenen Laufzeit-Read
// (a5120.cpp: kHeldReadWatchdogCycles / held_read_watchdog_; s.
// doc/analyse_scpx_5x1024_read.md).  SCPX ist Single-Format: ein aus einer
// 16×256-Systemdiskette gebootetes SCPX kann eine 5×1024-Diskette NICHT lesen
// (die Laufzeit-Leseroutine E9C8 liest fest 256-B-Datenfelder).  Der ZVE2-Matcher
// findet dann sein IDAM nie und signalisiert nie [EC0B]≠E8B5.  OHNE den Watchdog
// blieb ZVE1 dabei EWIG am Poll-Wait 0xE8B5 eingefroren (Rechner tot — das vom
// Anwender gemeldete Symptom).  MIT dem Watchdog wird der Bus nach ~3 Umdrehungen
// ohne Fortschritt freigegeben; ZVE1 nimmt seinen Index-Interrupt (Record-not-found)
// und der Zugriff TERMINIERT sauber mit „SCPX ERR ON B: BAD SECTOR" — das System
// bleibt bedienbar (echte HW-Semantik: der FM/MFM-Test darf nicht einfrieren).
//
// Trigger: leere 5×1024-Disk (createDisk cpa800) auf B:, dann DIR B: vom
// 16×256-System.  Kernprüfung: die BAD-SECTOR-Meldung erscheint (Read terminierte)
// UND das System reagiert danach noch (DIR A: listet weiter).  Ohne den Watchdog
// erschiene die Meldung nie → runSmallUntil liefe in sein Limit → Test rot.
TEST(ScpxIntegration, WrongFormatReadTerminatesInsteadOfFreezing) {
    namespace fs = std::filesystem;
    const std::string bPath =
        (fs::temp_directory_path() / "scpx_wrongfmt_guard_B.hfe").string();

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, diskPath("scpx_boot.hfe"), "cpa780", /*wp=*/false))
        << "konnte scpx_boot.hfe nicht mounten: " << machine.lastError();
    // Leere 5×1024-Diskette (Fremdformat für das 16×256-System) auf B: erzeugen+mounten.
    ASSERT_TRUE(machine.createDisk(1, bPath, "cpa800", /*wp=*/false)) << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 40'000'000))
        << "SCPX-Banner nie erschienen";
    ASSERT_TRUE(runSmallUntil(machine, "A>", 5'000'000)) << "A>-Prompt nie erreicht";
    runCycles(machine, 2'000'000);   // in die CONIN-Leseschleife einschwingen

    // 5×1024-Disk vom 16×256-System aus lesen → muss TERMINIEREN (BAD SECTOR),
    // nicht einfrieren.  OHNE Watchdog bliebe ZVE1 ewig @0xE8B5 → Meldung nie.
    typeString(machine, "DIR B:");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "BAD SECTOR", 40'000'000))
        << "Fremdformat-Read terminierte nicht — No-Progress-Watchdog gebrochen (Freeze):\n"
        << vramText(machine);

    // Das System läuft weiter (kein Dauer-Hänger): SCPX wiederholt den Fehl-Read
    // einige Male, dann kehrt ZVE1 in seine CONIN-Poll-Schleife (0xE079/E07x) zurück —
    // es bleibt NICHT am Held-Bus-Poll-Wait 0xE8B5 hängen (das wäre der Freeze).
    runCycles(machine, 40'000'000);   // SCPX-Retries auslaufen lassen
    EXPECT_NE(machine.cpuPC(), 0xE8B5)
        << "ZVE1 hängt weiter am Poll-Wait 0xE8B5 fest — Freeze nicht behoben";
    EXPECT_GE(machine.cpuPC(), 0xE079u);   // in der Prompt-/CONIN-Region, nicht im Read-Setup
    EXPECT_LE(machine.cpuPC(), 0xE0FFu)
        << "ZVE1 nach dem Fehler nicht zurück am Prompt (PC=0x" << std::hex << machine.cpuPC()
        << ") — System nicht wieder bedienbar";

    machine.unmountDisk(0);
    machine.unmountDisk(1);
    std::error_code ec;
    fs::remove(bPath, ec);
}

// ─── createDisk: laufwerkstyp-spezifisches Standardformat ────────────────────
//
// Ein leerer Formatname wählt je Slot-DriveProfile das passende Default-Format;
// die erzeugte, GÜLTIG FORMATIERTE Diskette wird gemountet.  Wir prüfen die
// resultierende .img-Größe (= Geometrie) je Laufwerkstyp.
namespace {
std::string tmpImg(const char* tag) {
    return (std::filesystem::temp_directory_path()
            / (std::string("k1520_create_default_") + tag + ".img")).string();
}
}  // namespace

TEST(CreateDiskDefault, K5601_DefaultIstCpa800) {
    const std::string path = tmpImg("k5601");
    std::filesystem::remove(path);
    A5120Machine machine;                       // Default: 4× K5601
    ASSERT_TRUE(machine.createDisk(0, path, /*format=*/"", /*wp=*/false))
        << machine.lastError();
    EXPECT_TRUE(machine.isDiskActive(0));
    EXPECT_EQ(std::filesystem::file_size(path), 80u * 2 * 5 * 1024);  // cpa800 = 800K
    std::filesystem::remove(path);
}

TEST(CreateDiskDefault, K560010_ss40_200K) {
    const std::string path = tmpImg("k560010");
    std::filesystem::remove(path);
    A5120Machine::Config cfg;
    cfg.drive_profiles = {"K5600.10", "K5601", "K5601", "K5601"};
    A5120Machine machine(cfg);
    ASSERT_TRUE(machine.createDisk(0, path, "", false)) << machine.lastError();
    EXPECT_EQ(std::filesystem::file_size(path), 40u * 5 * 1024);      // 200K
    std::filesystem::remove(path);
}

TEST(CreateDiskDefault, MF3200_fm_308K) {
    const std::string path = tmpImg("mf3200");
    std::filesystem::remove(path);
    A5120Machine::Config cfg;
    cfg.drive_profiles = {"MF3200", "K5601", "K5601", "K5601"};
    A5120Machine machine(cfg);
    ASSERT_TRUE(machine.createDisk(0, path, "", false)) << machine.lastError();
    EXPECT_EQ(std::filesystem::file_size(path), 77u * 4 * 1024);      // 308K
    std::filesystem::remove(path);
}

TEST(CreateDiskDefault, UnbekanntesFormat_gibtFalse) {
    const std::string path = tmpImg("bad");
    std::filesystem::remove(path);
    A5120Machine machine;
    EXPECT_FALSE(machine.createDisk(0, path, "gibt_es_nicht", false));
    std::filesystem::remove(path);
}
