/**
 * @file test_k5122.cpp
 * @brief Unit tests for the K5122 AFS floppy controller card emulation.
 *
 * @details
 * Emulator component under test: **K5122** (`core/cards/k5122/k5122.h`)
 *
 * The K5122 AFS (Anschaltung Flexible Speicher) card is the floppy disk
 * controller of the Robotron A5120.  It connects up to four 5.25" floppy
 * drives to the K1520 bus and communicates with them via two Z80-PIO chips
 * (Ctrl-PIO and Data-PIO) plus a DMA channel using the BUSRQ/BUSAK protocol.
 *
 * The BIOS uses the following I/O port map:
 *  - 0x10/0x11  Ctrl-PIO Port A data/ctrl  (step, head-load, write-enable, /STR)
 *  - 0x12/0x13  Ctrl-PIO Port B data/ctrl  (status: /RDYL, /WP, /FW, /INDX)
 *  - 0x14/0x15  Data-PIO Port A data/ctrl  (write data byte)
 *  - 0x16/0x17  Data-PIO Port B data/ctrl  (read data byte)
 *  - 0x18       Drive-select register (/LCK1–/LCK4, write-only)
 *
 * ## Test groups
 *
 * | Group                     | What is tested                                            |
 * |---------------------------|-----------------------------------------------------------|
 * | Mount / unmount lifecycle | mountDisk, unmountDisk, isDiskActive                      |
 * | Write-protect flag        | mountDisk with WP, setWriteProtect, isDiskWriteProtected  |
 * | Drive index bounds        | Out-of-range indices rejected                             |
 * | Port 0x18 drive-select    | No crash on valid/invalid drive selection codes           |
 * | PIO port I/O routing      | Ctrl and Data PIO read/write do not crash                 |
 * | Status Port B             | /RDYL, /WP, /FW bits reflect drive state                  |
 * | Step / ctrl signals       | Step pulse, /STR-pulse sequences do not crash             |
 * | DMA protocol              | BUSRQ assert/release on /STR edge, dmaUpdate(), ZVE2 flow |
 * | InterruptSlave interface  | IEI/IEO pass-through when no interrupt pending            |
 * | drive() accessor          | Returns stable reference to the correct FloppyDrive       |
 *
 * @see core/cards/k5122/k5122.h
 * @see core/bus/k1520_bus.h
 * @see core/peripherals/floppy_drive/floppy_drive.h
 */

// tests/cpp/test_k5122.cpp
// Unit tests for the K5122 AFS floppy controller card.
//
// Tests are practical and focus on what the CPA BIOS requires:
//   - Drive mount/unmount lifecycle
//   - isDiskActive / isDiskWriteProtected queries
//   - Port 0x18 drive-select write (no crash)
//   - PIO Port A/B read/write routing (no crash)
//   - Write-protect flag propagation
//   - Mounting a non-existent file returns false

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/bus/k1520_bus.h"
#include "core/cards/k5122/k5122.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/logger.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Return the built-in "cpa780" disk format.
static DiskFormat getCPA780() {
    auto fmts = FormatParser::builtinFormats();
    auto it   = std::find_if(fmts.begin(), fmts.end(),
                              [](const DiskFormat& f){ return f.name == "cpa780"; });
    EXPECT_NE(it, fmts.end()) << "cpa780 format not found in builtinFormats()";
    return *it;
}

// Create a temporary disk image of the given format and return its path.
static std::string makeTmpImage(const DiskFormat& fmt) {
    auto path = (std::filesystem::temp_directory_path() /
                 ("k5122_test_" + fmt.name + ".img")).string();
    uint64_t sz = fmt.totalBytes();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    std::vector<uint8_t> buf(sz, 0xE5);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    return path;
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class K5122Test : public ::testing::Test {
protected:
    K1520Bus bus;
    K5122    card{bus};
    DiskFormat fmt = getCPA780();

    // Keep the multi-hundred-read stability tests quiet: drop the emulator log
    // to ERROR at runtime (exercises the new gated Logger; the >>> READ INFO line
    // would otherwise print once per doReadSector).
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }

    // Returns a fresh temp image path; caller is responsible for cleanup.
    std::string tmpImage() { return makeTmpImage(fmt); }
};

// ─── 1. Mount / unmount lifecycle ────────────────────────────────────────────

/**
 * @test K5122Test/MountValidDisk_DriveBecomesActive
 * @brief Mounting a valid disk image activates the drive.
 * @par Pass criterion  isDiskActive(0) == true after successful mountDisk().
 */
TEST_F(K5122Test, MountValidDisk_DriveBecomesActive) {
    auto path = tmpImage();
    EXPECT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/MountNonexistentFile_ReturnsFalse
 * @brief Mounting a non-existent file path returns false and leaves the drive inactive.
 * @par Pass criterion  mountDisk() == false; isDiskActive(0) == false.
 */
TEST_F(K5122Test, MountNonexistentFile_ReturnsFalse) {
    EXPECT_FALSE(card.mountDisk(0, "/nonexistent/disk.img", fmt));
    EXPECT_FALSE(card.isDiskActive(0));
}

/**
 * @test K5122Test/UnmountDisk_DriveBecomesInactive
 * @brief unmountDisk() deactivates a previously mounted drive.
 * @par Pass criterion  isDiskActive(0) == false after unmountDisk(0).
 */
TEST_F(K5122Test, UnmountDisk_DriveBecomesInactive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(card.isDiskActive(0));

    card.unmountDisk(0);
    EXPECT_FALSE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/UnmountNotMounted_NocrashRetursTrue
 * @brief Calling unmountDisk() on a drive with no disk does not crash.
 * @par Pass criterion  No exception; isDiskActive(0) == false.
 */
TEST_F(K5122Test, UnmountNotMounted_NocrashRetursTrue) {
    // Calling unmountDisk on a drive with no disk should not crash.
    EXPECT_TRUE(card.unmountDisk(0));
    EXPECT_FALSE(card.isDiskActive(0));
}

// ─── 2. Write-protect flag ───────────────────────────────────────────────────

/**
 * @test K5122Test/MountWithWriteProtect_IsWriteProtected
 * @brief Mounting with write_protect=true sets the write-protect status.
 * @par Pass criterion  isDiskWriteProtected(0) == true.
 */
TEST_F(K5122Test, MountWithWriteProtect_IsWriteProtected) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/true));
    EXPECT_TRUE(card.isDiskWriteProtected(0));
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/MountWithoutWriteProtect_IsNotWriteProtected
 * @brief Mounting with write_protect=false leaves the drive writable.
 * @par Pass criterion  isDiskWriteProtected(0) == false.
 */
TEST_F(K5122Test, MountWithoutWriteProtect_IsNotWriteProtected) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/false));
    EXPECT_FALSE(card.isDiskWriteProtected(0));
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/SetWriteProtect_ChangesFlag
 * @brief setWriteProtect() can change the write-protect state after mounting.
 * @par Pass criterion  Flag toggles correctly with true/false.
 */
TEST_F(K5122Test, SetWriteProtect_ChangesFlag) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/false));
    EXPECT_FALSE(card.isDiskWriteProtected(0));

    card.setWriteProtect(0, true);
    EXPECT_TRUE(card.isDiskWriteProtected(0));

    card.setWriteProtect(0, false);
    EXPECT_FALSE(card.isDiskWriteProtected(0));
    std::filesystem::remove(path);
}

// ─── 3. Drive index bounds ───────────────────────────────────────────────────

/**
 * @test K5122Test/MountDriveOutOfRange_ReturnsFalse
 * @brief Drive indices outside 0–3 are rejected; mountDisk() returns false.
 * @par Pass criterion  mountDisk(-1, ...) == false; mountDisk(4, ...) == false.
 */
TEST_F(K5122Test, MountDriveOutOfRange_ReturnsFalse) {
    auto path = tmpImage();
    EXPECT_FALSE(card.mountDisk(-1, path, fmt));
    EXPECT_FALSE(card.mountDisk(4,  path, fmt));
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/IsDiskActiveOutOfRange_ReturnsFalse
 * @brief isDiskActive() returns false for out-of-range drive indices.
 * @par Pass criterion  isDiskActive(-1) == false; isDiskActive(4) == false.
 */
TEST_F(K5122Test, IsDiskActiveOutOfRange_ReturnsFalse) {
    EXPECT_FALSE(card.isDiskActive(-1));
    EXPECT_FALSE(card.isDiskActive(4));
}

/**
 * @test K5122Test/MultipleDrivesMountIndependently
 * @brief Two drives can be independently mounted without affecting each other.
 * @par Pass criterion  isDiskActive(0) and isDiskActive(1) == true; 2 and 3 remain false.
 */
TEST_F(K5122Test, MultipleDrivesMountIndependently) {
    auto path0 = makeTmpImage(fmt);
    auto path1 = makeTmpImage(fmt);

    EXPECT_TRUE(card.mountDisk(0, path0, fmt));
    EXPECT_TRUE(card.mountDisk(1, path1, fmt));

    EXPECT_TRUE(card.isDiskActive(0));
    EXPECT_TRUE(card.isDiskActive(1));
    EXPECT_FALSE(card.isDiskActive(2));
    EXPECT_FALSE(card.isDiskActive(3));

    std::filesystem::remove(path0);
    std::filesystem::remove(path1);
}

// ─── 4. Port 0x18 drive-select (no crash) ────────────────────────────────────

/**
 * @test K5122Test/DriveSelectPort_WritesDoNotCrash
 * @brief Writing drive-select codes 0x00–0x03 to port 0x18 does not crash.
 * @details Port 0x18 is write-only; reading it returns 0xFF.
 * @par Pass criterion  No exception; ioRead(0x18) == 0xFF.
 */
TEST_F(K5122Test, DriveSelectPort_WritesDoNotCrash) {
    // /LCK1 = 0 → select drive 0
    card.ioWrite(0x18, 0x00);
    // /LCK2 = 0 → select drive 1
    card.ioWrite(0x18, 0x01);
    // /LCK3 = 0 → select drive 2
    card.ioWrite(0x18, 0x02);
    // /LCK4 = 0 → select drive 3
    card.ioWrite(0x18, 0x03);
    // Reading 0x18 returns 0xFF (write-only register)
    EXPECT_EQ(card.ioRead(0x18), 0xFF);
}

// ─── 5. PIO Port A / B I/O routing (no crash) ────────────────────────────────

TEST_F(K5122Test, CtrlPIO_PortADataWrite_DoesNotCrash) {
    // Write various ctrl signals to Port A data register.
    card.ioWrite(0x10, 0xFF);   // all signals inactive
    card.ioWrite(0x10, 0xBF);   // /HL = 0 (head load)
    card.ioWrite(0x10, 0x7F);   // /ST = 0 (step pulse)
    card.ioWrite(0x10, 0xFF);   // release
}

TEST_F(K5122Test, CtrlPIO_PortACtrlWrite_DoesNotCrash) {
    // Send a mode-0 (output) control word to ctrl Port A.
    card.ioWrite(0x11, 0x0F);   // mode 0 = output
}

TEST_F(K5122Test, CtrlPIO_PortBDataRead_DoesNotCrash) {
    // Reading Port B should always return a byte without crashing.
    uint8_t v = card.ioRead(0x12);
    (void)v;
}

TEST_F(K5122Test, DataPIO_PortAWrite_DoesNotCrash) {
    card.ioWrite(0x14, 0xAA);
    card.ioWrite(0x14, 0x55);
}

TEST_F(K5122Test, DataPIO_PortBRead_DoesNotCrash) {
    uint8_t v = card.ioRead(0x16);
    (void)v;
}

// ─── 6. Status Port B reflects drive state ───────────────────────────────────

/**
 * @test K5122Test/StatusPortB_NotMounted_RDYLInactive
 * @brief Without a mounted disk, /RDYL (Ctrl Port B bit 0) is high (not ready).
 * @details Select drive 0 via port 0x18; read Ctrl Port B (0x12).
 * @par Pass criterion  status & 0x01 != 0  (/RDYL = 1 = inactive).
 */
TEST_F(K5122Test, StatusPortB_NotMounted_RDYLInactive) {
    // Drive 0 not mounted; after selecting it, /RDYL (bit 0) should be 1.
    card.ioWrite(0x18, 0x00);           // select drive 0
    uint8_t status = card.ioRead(0x12); // read ctrl Port B
    EXPECT_NE(status & 0x01, 0) << "Expected /RDYL=1 (not ready) when no disk mounted";
}

/**
 * @test K5122Test/StatusPortB_Mounted_RDYLActive
 * @brief With a mounted disk /RDYL (bit 0) is low (ready).
 * @par Pass criterion  status & 0x01 == 0  (/RDYL = 0 = active/ready).
 */
TEST_F(K5122Test, StatusPortB_Mounted_RDYLActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);           // select drive 0
    uint8_t status = card.ioRead(0x12);
    EXPECT_EQ(status & 0x01, 0) << "Expected /RDYL=0 (ready) when disk is mounted";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/StatusPortB_WriteProtected_WPActive
 * @brief Write-protected drive: /WP (bit 5) is low (active-low, write-protected).
 * @par Pass criterion  status & 0x20 == 0.
 */
TEST_F(K5122Test, StatusPortB_WriteProtected_WPActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/true));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    // /WP = bit 5; active low → 0 when write protected.
    EXPECT_EQ(status & 0x20, 0) << "Expected /WP=0 (write-protected) in status";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/StatusPortB_NotWriteProtected_WPInactive
 * @brief Non-write-protected drive: /WP (bit 5) is high (inactive).
 * @par Pass criterion  status & 0x20 != 0.
 */
TEST_F(K5122Test, StatusPortB_NotWriteProtected_WPInactive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/false));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    EXPECT_NE(status & 0x20, 0) << "Expected /WP=1 (not write-protected) in status";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/StatusPortB_AtTrack0_TOActive
 * @brief After mounting, the head is at track 0: /TO (bit 7, /TRACK 00) is low.
 * @par Pass criterion  status & 0x80 == 0  (/TO = 0 = at track 0).
 */
TEST_F(K5122Test, StatusPortB_AtTrack0_TOActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    // /TO = /TRACK 00 = bit 7 (per K5122 hardware doc); active low → 0 when the
    // head is at track 0 (initial position after mount). bit 6 is /FW (Fault).
    EXPECT_EQ(status & 0x80, 0) << "Expected /TO=0 (at track 0) after mount";
    std::filesystem::remove(path);
}

// ─── 7. Step and ctrl signal sequence (no crash) ─────────────────────────────

/**
 * @test K5122Test/StepInward_LeavesTrack0
 * @brief One inward step pulse moves the head off track 0 (/TO bit 7 goes high).
 * @details MR/SD (bit 5) step direction matches the boot ROM: bit5=1 = inward
 *   (toward higher cylinders), as the loaded boot record uses (0x29). A step
 *   inward from track 0 reaches cylinder 1, so /TO (bit 7, /TRACK 00) becomes 1.
 * @par Pass criterion  /TO=0 at track 0; /TO=1 after one inward step.
 */
TEST_F(K5122Test, StepInward_LeavesTrack0) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);       // select drive 0

    EXPECT_EQ(card.ioRead(0x12) & 0x80, 0) << "should start at track 0 (/TO=0)";

    // Step inward: /ST pulse (bit 7 falling edge), MR/SD=1 (bit 5 = 1 = inward).
    // 0x7F = 0b01111111 → bit 7=0 (step), bit 5=1 (inward).
    card.ioWrite(0x10, 0xFF);       // all high (inactive), establishes prev state
    card.ioWrite(0x10, 0x7F);       // /ST = 0 (step pulse), MR/SD = 1 (inward)
    card.ioWrite(0x10, 0xFF);       // release

    // After one inward step the head is at cylinder 1: /TO (bit 7) = 1.
    EXPECT_NE(card.ioRead(0x12) & 0x80, 0) << "Expected /TO=1 (off track 0) after inward step";

    std::filesystem::remove(path);
}

/**
 * @test K5122Test/STRPulse_ReadMode_DoesNotCrash
 * @brief A /STR falling edge in read mode (/WE=1) followed by dmaUpdate() does not crash.
 * @details Simulates the ZVE1 BIOS sequence: /STR low → dmaUpdate → read byte from port 0x16.
 * @par Pass criterion  No crash; byte readable from port 0x16.
 */
TEST_F(K5122Test, STRPulse_ReadMode_DoesNotCrash) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    // /WE=1 (bit 0) = read mode; /STR (bit 3) falling edge = start read
    card.ioWrite(0x10, 0xFF);       // all inactive
    card.ioWrite(0x10, 0xF7);       // /STR = 0, /WE = 1 (read mode): triggers BUSRQ
    card.ioWrite(0x10, 0xFF);       // release

    // DMA completes (sector loaded into buffer) then BUSRQ is released.
    card.dmaUpdate();

    // Now drain a sector byte from data Port B (port 0x16).
    uint8_t b = card.ioRead(0x16);
    (void)b;   // value may be 0xFF if format returns no data; just no crash
    std::filesystem::remove(path);
}

// ─── 10. DMA protocol (BUSRQ/BUSAK) ─────────────────────────────────────────

/**
 * @test K5122Test/DMA_ReadMode_BUSRQAssertedOnSTR
 * @brief A /STR falling edge in read mode (/WE=1) causes the controller to assert BUSRQ.
 * @details The K5122 asserts BUSRQ to halt the CPU and take bus ownership for DMA transfer.
 * @par Pass criterion  bus.isBUSRQ() == true after the /STR edge.
 */
TEST_F(K5122Test, DMA_ReadMode_BUSRQAssertedOnSTR) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    EXPECT_FALSE(bus.isBUSRQ());

    card.ioWrite(0x10, 0xFF);   // prev: all inactive
    card.ioWrite(0x10, 0xF7);   // /STR falling edge, /WE=1 = read mode
    card.ioWrite(0x10, 0xFF);   // release

    EXPECT_TRUE(bus.isBUSRQ()) << "BUSRQ must be asserted after /STR edge";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_ReadMode_BUSRQReleasedAfterDmaUpdate
 * @brief dmaUpdate() completes the DMA transfer and releases BUSRQ.
 * @par Pass criterion  bus.isBUSRQ() == false after dmaUpdate().
 */
TEST_F(K5122Test, DMA_ReadMode_BUSRQReleasedAfterDmaUpdate) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF7);   // /STR falling edge (read)
    card.ioWrite(0x10, 0xFF);

    ASSERT_TRUE(bus.isBUSRQ());
    card.dmaUpdate();
    EXPECT_FALSE(bus.isBUSRQ()) << "BUSRQ must be released after dmaUpdate()";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_ReadMode_SectorDataAvailableAfterDmaUpdate
 * @brief After a read /STR the port-0x16 stream is the track as an IDAM stream.
 * @details Each sector is [00 FE cyl head sec size CRC CRC <128 data> CRC CRC];
 *          the blank image payload (offset 8) is 0xE5.
 * @par Pass criterion  IDAM header bytes match and payload at offset 8 is 0xE5.
 */
TEST_F(K5122Test, DMA_ReadMode_SectorDataAvailableAfterDmaUpdate) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF7);   // /STR falling edge (read) → present sector 0 IDAM field

    card.dmaUpdate();

    // Address-mark field model: after /STR the port-0x16 stream is the current
    // sector's IDAM field [A1 FE cyl head sec size] followed by gap.  Pulsing MK
    // (ctrl Port A bit1, 0→1) re-syncs to the next address-mark field (the DATA
    // field [A1 FB <128 data> CRC CRC]).  See K5122 §14.6.
    EXPECT_EQ(card.ioRead(0x16), 0xA1) << "IDAM byte0 = 0xA1 sync";
    EXPECT_EQ(card.ioRead(0x16), 0xFE) << "IDAM byte1 = IDAM address mark";
    card.ioRead(0x16);                                    // cylinder
    card.ioRead(0x16);                                    // head
    EXPECT_EQ(card.ioRead(0x16), 0x01) << "sector ID (1-based)";
    card.ioRead(0x16);                                    // size code

    // Pulse MK (0→1) with /STR held low to advance to the DATA field.
    card.ioWrite(0x10, 0xF5);   // MK=0, /STR=0
    card.ioWrite(0x10, 0xF7);   // MK=1 (rising) → advance IDAM → DATA

    EXPECT_EQ(card.ioRead(0x16), 0xA1) << "DATA byte0 = 0xA1 sync";
    EXPECT_EQ(card.ioRead(0x16), 0xFB) << "DATA byte1 = data address mark";
    // Payload follows; the blank image is filled with 0xE5.
    EXPECT_EQ(card.ioRead(0x16), 0xE5) << "first payload byte (blank image)";
    EXPECT_EQ(card.ioRead(0x16), 0xE5) << "second payload byte (blank image)";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_WriteMode_BUSRQAssertedOnSTR
 * @brief A /STR falling edge in write mode (/WE=0) also asserts BUSRQ.
 * @par Pass criterion  bus.isBUSRQ() == true after the write-mode /STR edge.
 */
TEST_F(K5122Test, DMA_WriteMode_BUSRQAssertedOnSTR) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    // Fill write buffer via port 0x14 (Data PIO Port A)
    for (int i = 0; i < 4; ++i)
        card.ioWrite(0x14, static_cast<uint8_t>(0xAA + i));

    // /WE=0 (bit 0) = write mode; /STR falling edge triggers BUSRQ
    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF6);   // /STR=0, /WE=0 = write mode
    card.ioWrite(0x10, 0xFF);

    EXPECT_TRUE(bus.isBUSRQ()) << "BUSRQ must be asserted for write DMA";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_WriteMode_BUSRQReleasedAfterDmaUpdate
 * @brief dmaUpdate() in write mode commits the sector and releases BUSRQ.
 * @par Pass criterion  bus.isBUSRQ() == false after write dmaUpdate().
 */
TEST_F(K5122Test, DMA_WriteMode_BUSRQReleasedAfterDmaUpdate) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    for (int i = 0; i < 4; ++i)
        card.ioWrite(0x14, 0xCC);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF6);   // /STR=0, /WE=0
    card.ioWrite(0x10, 0xFF);

    ASSERT_TRUE(bus.isBUSRQ());
    card.dmaUpdate();
    EXPECT_FALSE(bus.isBUSRQ()) << "BUSRQ released after write dmaUpdate()";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DmaUpdate_NoPending_DoesNotCrash
 * @brief Calling dmaUpdate() when no DMA is in progress is a safe no-op.
 * @par Pass criterion  bus.isBUSRQ() remains false; no crash.
 */
TEST_F(K5122Test, DmaUpdate_NoPending_DoesNotCrash) {
    // Calling dmaUpdate() when no DMA is pending must be a no-op.
    EXPECT_FALSE(bus.isBUSRQ());
    card.dmaUpdate();
    EXPECT_FALSE(bus.isBUSRQ());
}

// ─── 8. InterruptSlave interface ─────────────────────────────────────────────

/**
 * @test K5122Test/InterruptSlave_SetIEI_NocrashGetIEO
 * @brief With IEI=true and no pending interrupt, IEO passes through (true).
 * @par Pass criterion  getIEO() == true; hasInterrupt() == false.
 */
TEST_F(K5122Test, InterruptSlave_SetIEI_NocrashGetIEO) {
    card.setIEI(true);
    // With no interrupts pending, IEO should propagate through.
    EXPECT_TRUE(card.getIEO());
    EXPECT_FALSE(card.hasInterrupt());
}

/**
 * @test K5122Test/InterruptSlave_IEIFalse_IEOFalse
 * @brief With IEI=false, IEO is also false (daisy-chain blocked upstream).
 * @par Pass criterion  getIEO() == false when setIEI(false).
 */
TEST_F(K5122Test, InterruptSlave_IEIFalse_IEOFalse) {
    card.setIEI(false);
    EXPECT_FALSE(card.getIEO());
}

// ─── 9. drive() accessor ─────────────────────────────────────────────────────

/**
 * @test K5122Test/DriveAccessor_ReturnsValidReference
 * @brief drive(0) returns a stable reference to the same FloppyDrive object operated on by mountDisk.
 * @par Pass criterion  d0.isMounted() == false before mount; true after mount.
 */
TEST_F(K5122Test, DriveAccessor_ReturnsValidReference) {
    // Just verify the accessor doesn't crash and returns the same object.
    FloppyDrive& d0 = card.drive(0);
    EXPECT_FALSE(d0.isMounted());

    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(d0.isMounted());   // accessor returns same object as mountDisk operates on
    std::filesystem::remove(path);
}

// ─── 11. Immediate sector fill on /STR (new ZVE2-compatible design) ──────────

// With the redesigned DMA flow, doReadSector() is called immediately when /STR
// falls (not deferred to dmaUpdate()). This allows ZVE2 to drain the buffer via
// its INIR program. ZVE1 byte-polling also works via dmaUpdate() fallback.

/**
 * @test K5122Test/DMA_Read_SectorImmediatelyAvailableAfterSTR
 * @brief The track IDAM stream is filled synchronously on the /STR edge.
 * @details BUSRQ is asserted and the stream is readable via port 0x16 before
 *          dmaUpdate() is called.
 * @par Pass criterion  ioRead(0x16) returns the IDAM header right after /STR.
 */
TEST_F(K5122Test, DMA_Read_SectorImmediatelyAvailableAfterSTR) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF7);   // /STR falling edge, /WE=1 (read mode)
    card.ioWrite(0x10, 0xFF);

    // BUSRQ asserted and the track stream is already in the buffer.
    ASSERT_TRUE(bus.isBUSRQ());
    EXPECT_EQ(card.ioRead(0x16), 0xA1) << "byte0 = 0xA1 sync / address-mark prefix";
    EXPECT_EQ(card.ioRead(0x16), 0xFE) << "byte1 = IDAM mark, readable immediately after /STR";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_Read_BUSRQHeldUntilCompletion
 * @brief In the continuous-stream model BUSRQ is held while the track streams and
 *        released only on completion via endDmaTransfer().
 * @details The buffer no longer auto-releases when it drains — the rotating track
 *          wraps and keeps streaming. The machine releases the bus when ZVE2
 *          signals completion ([0x03F8]=3); endDmaTransfer() models that handover.
 * @par Pass criterion  BUSRQ stays asserted across a full-track read; released
 *                      only after endDmaTransfer().
 */
TEST_F(K5122Test, DMA_Read_BUSRQHeldUntilCompletion) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF7);   // /STR falling edge (read mode)
    card.ioWrite(0x10, 0xFF);

    ASSERT_TRUE(bus.isBUSRQ());

    // Read well past one sector; BUSRQ must remain held (stream wraps, no auto-release).
    for (int i = 0; i < 512; ++i) card.ioRead(0x16);
    EXPECT_TRUE(bus.isBUSRQ()) << "BUSRQ stays held while the track streams";

    card.endDmaTransfer();
    EXPECT_FALSE(bus.isBUSRQ()) << "endDmaTransfer() releases BUSRQ on completion";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/DMA_Write_ZVE2Style_CommitViaSeccondSTR
 * @brief ZVE2 write DMA flow: ZVE1 asserts BUSRQ; ZVE2 fills the buffer; second /STR commits the sector.
 * @details
 *   1. ZVE1: /STR+/WE=0 → assertBUSRQ, sector buffer cleared.
 *   2. ZVE2: writes 128 bytes to port 0x14.
 *   3. ZVE2: second /STR+/WE=0 edge (while BUSRQ already held) → doWriteSector + releaseBUSRQ.
 * @par Pass criterion  bus.isBUSRQ() == false after the ZVE2 commit /STR edge.
 */
TEST_F(K5122Test, DMA_Write_ZVE2Style_CommitViaSeccondSTR) {
    // Simulate ZVE2 write DMA:
    //   ZVE1: /STR+/WE=0 → assertBUSRQ, sector_buf_ cleared
    //   ZVE2: writes bytes via port 0x14
    //   ZVE2: writes /STR+/WE=0 again (bus already held) → doWriteSector + releaseBUSRQ
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    // ZVE1 triggers write DMA (/WE=0, /STR falling)
    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF6);   // /STR=0, /WE=0 (write mode)
    card.ioWrite(0x10, 0xFF);
    ASSERT_TRUE(bus.isBUSRQ());

    // ZVE2 writes sector bytes to port 0x14
    for (int i = 0; i < 128; ++i)
        card.ioWrite(0x14, static_cast<uint8_t>(0xAB));

    // ZVE2 commits: writes /STR=0 while BUSRQ is held → doWriteSector + releaseBUSRQ
    // (bus_.isBUSRQ() is true, so handleCtrlPortAWrite takes the ZVE2 commit path)
    card.ioWrite(0x10, 0xFF);   // prev state for ZVE2's ctrl_a
    card.ioWrite(0x10, 0xF6);   // /STR falling, /WE=0 → ZVE2 commit
    card.ioWrite(0x10, 0xFF);

    EXPECT_FALSE(bus.isBUSRQ()) << "BUSRQ must be released after ZVE2 write commit";
    std::filesystem::remove(path);
}

// ─── 11. 1024B continuous-read stability (3rd-stage @OS.COM data area) ─────────
//
// The 3rd-stage CP/A loader reads the 1024B data area (cpa780 cyl 2+, size_code 3)
// with the CONTINUOUS stream model: doReadSector() builds one full-sector block
// [A1 FE cyl head sec size][27 gap][A1 FB][1024 data][CRC CRC][8 gap] = 1069 bytes,
// and ioRead(0x16) auto-steps to the next sector on over-read.  The boot fails
// because reads are not stable across REPEATED access — the loader re-reads the
// same sector (fresh /STR → doReadSector) and sweeps sectors, and a later read of
// an already-read sector must return the identical, correct field.  These tests
// drive that pattern directly (deterministic; the emulator has no RNG, so the
// value is exercising the read-over-read STATE, not repetition for chance).

// Geometry constants for the cpa780 1024B data area (cyl 2+).
static constexpr int      kSecs      = 5;       // sectors per 1024B track
static constexpr uint16_t kSecLen    = 1024;
static constexpr size_t   kFieldLen  = 6 + 27 + 2 + kSecLen + 2 + 8;  // 1069

// Distinct per-(cyl,head,sec) data byte so a served field unambiguously
// identifies which physical sector was streamed.
static uint8_t dataByte(int cyl, int head, int sec) {
    return static_cast<uint8_t>(0x40 + (cyl << 4) + (head << 3) + sec);
}

// Seek the head inward to `target` cylinder via /ST pulses (updates the K5122's
// own current_cyl_ tracking through doStep()).  `head_bit5`=1 keeps head 0.
static void seekToCyl(K5122& card, int target) {
    for (int c = 0; c < target; ++c) {
        card.ioWrite(0x10, 0xFF);   // prev high (establish /ST=1)
        card.ioWrite(0x10, 0x7F);   // /ST=0 (step), MR/SD=1 (inward)
        card.ioWrite(0x10, 0xFF);   // release
    }
}

// Issue a read /STR falling edge (read mode) selecting `head` → fresh
// doReadSector.  SIDE-SELECT is bit2 (/FR): bit2=1 → head 0, bit2=0 → head 1
// (NOT bit5, which is the step-direction/MK signal and toggles during re-sync).
// Like the real loader, the /STR pulse keeps every other bit (incl. the side
// select) stable and only toggles /STR (bit3); the "release" word preserves it.
static void strobeRead(K5122& card, int head = 0) {
    const uint8_t hi   = (head == 0) ? 0xFF : 0xFB;  // /STR=1; bit2: head0=1, head1=0
    const uint8_t low  = (head == 0) ? 0xF7 : 0xF3;  // /STR=0; bit2 held
    card.ioWrite(0x10, hi);         // /STR=1 (side-select bit2 set)
    card.ioWrite(0x10, low);        // /STR=0 → doReadSector
    card.ioWrite(0x10, hi);         // /STR=1 (head bit preserved)
}

// Read n bytes from data Port B (0x16).
static std::vector<uint8_t> readBytes(K5122& card, size_t n) {
    std::vector<uint8_t> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.push_back(card.ioRead(0x16));
    return v;
}

// Fill (cyl, head) sectors 1..5 each with a distinct constant byte.
static void writeDistinctDataTrack(K5122& card, int cyl, int head) {
    for (int sec = 1; sec <= kSecs; ++sec) {
        std::vector<uint8_t> data(kSecLen, dataByte(cyl, head, sec));
        ASSERT_TRUE(card.drive(0).writeSector(static_cast<uint8_t>(cyl),
                                              static_cast<uint8_t>(head),
                                              static_cast<uint8_t>(sec), data));
    }
}

// Assert `f` is a well-formed continuous field for (cyl, head, sec).
static void expectFieldForSector(const std::vector<uint8_t>& f, int cyl, int head, int sec) {
    ASSERT_EQ(f.size(), kFieldLen);
    EXPECT_EQ(f[0], 0xA1) << "IDAM sync";
    EXPECT_EQ(f[1], 0xFE) << "IDAM mark";
    EXPECT_EQ(f[2], static_cast<uint8_t>(cyl))  << "IDAM cyl";
    EXPECT_EQ(f[3], static_cast<uint8_t>(head)) << "IDAM head";
    EXPECT_EQ(f[4], static_cast<uint8_t>(sec))  << "IDAM sector id";
    EXPECT_EQ(f[5], 3) << "IDAM size code (1024B)";
    EXPECT_EQ(f[33], 0xA1) << "DATA sync";
    EXPECT_EQ(f[34], 0xFB) << "DATA mark";
    const uint8_t want = dataByte(cyl, head, sec);
    for (size_t i = 0; i < kSecLen; ++i)
        ASSERT_EQ(f[35 + i], want) << "data byte " << i
            << " of cyl " << cyl << " head " << head << " sec " << sec;
    for (size_t i = 0; i < 8; ++i)
        EXPECT_EQ(f[kFieldLen - 8 + i], 0x4E) << "trailing gap " << i;
}

static constexpr uint8_t kDataCyl  = 2;
static constexpr uint8_t kDataHead = 0;

/**
 * @test K5122Test/Continuous1024_Sector1_StableAcrossRestrobes
 * @brief Re-reading sector 1 via repeated fresh /STR yields the identical field.
 * @details Reproduces the loader re-reading the same data sector. Catches a
 *   cross-read state leak (field_sector_/field_pos_/sector_buf_ not reset) that
 *   would make a later read serve the wrong sector or a shifted field — the
 *   '@OS.COM' boot symptom (a re-read times out / mis-syncs).
 */
TEST_F(K5122Test, Continuous1024_Sector1_StableAcrossRestrobes) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, kDataCyl, kDataHead);
    seekToCyl(card, kDataCyl);

    strobeRead(card, kDataHead);
    std::vector<uint8_t> ref = readBytes(card, kFieldLen);
    expectFieldForSector(ref, kDataCyl, kDataHead, 1);

    for (int i = 0; i < 256; ++i) {
        strobeRead(card, kDataHead);
        std::vector<uint8_t> f = readBytes(card, kFieldLen);
        ASSERT_EQ(f, ref) << "sector-1 field differs on re-read #" << i
                          << " — read-over-read state leak";
    }
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_AutoStepsThroughSectorsAndWraps
 * @brief Over-reading auto-steps through all 5 sectors in order and wraps to 1.
 * @par Pass criterion  Each consecutive 1069-byte block is the next sector
 *   (1,2,3,4,5,1,2,…) with correct IDAM id and data, across several wraps.
 */
TEST_F(K5122Test, Continuous1024_AutoStepsThroughSectorsAndWraps) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, kDataCyl, kDataHead);
    seekToCyl(card, kDataCyl);

    strobeRead(card, kDataHead);
    for (int i = 0; i < kSecs * 4; ++i) {       // 4 full revolutions
        int expect_sec = (i % kSecs) + 1;
        std::vector<uint8_t> f = readBytes(card, kFieldLen);
        SCOPED_TRACE("revolution byte-block #" + std::to_string(i));
        expectFieldForSector(f, kDataCyl, kDataHead, expect_sec);
    }
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_RestrobeResetsToSector1
 * @brief A fresh /STR always resets the stream to sector 1, regardless of how
 *   far a previous read auto-stepped.
 * @details Directly targets the observed bug where doReadSector served sector 2+
 *   (stale field_sector_) right after a re-strobe. Advances a varying number of
 *   sectors, then re-strobes and asserts sector 1 is served again — 100×.
 */
TEST_F(K5122Test, Continuous1024_RestrobeResetsToSector1) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, kDataCyl, kDataHead);
    seekToCyl(card, kDataCyl);

    for (int round = 0; round < 100; ++round) {
        strobeRead(card, kDataHead);
        // Advance a round-dependent number of sectors via over-read auto-step.
        int advance = round % (kSecs + 2);       // 0..6 sectors (incl. wrap)
        readBytes(card, kFieldLen * (advance + 1));
        // Fresh /STR must reset to sector 1.
        strobeRead(card, kDataHead);
        std::vector<uint8_t> f = readBytes(card, kFieldLen);
        SCOPED_TRACE("round " + std::to_string(round) + " advance " + std::to_string(advance));
        expectFieldForSector(f, kDataCyl, kDataHead, 1);
    }
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_Head1_ReadsCorrectSide
 * @brief Reading cyl 2 HEAD 1 (the @OS.COM data side, exact error location
 *   'RU;T,Si,Se=020101') serves the head-1 IDAM + data, stable across re-reads.
 * @details The real boot fails reporting cyl 2 head 1. This drives a head-1
 *   1024B read directly and checks the IDAM head byte and side data are correct.
 */
TEST_F(K5122Test, Continuous1024_Head1_ReadsCorrectSide) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, /*cyl=*/2, /*head=*/0);
    writeDistinctDataTrack(card, /*cyl=*/2, /*head=*/1);
    seekToCyl(card, 2);

    // Head 1 read.
    strobeRead(card, /*head=*/1);
    std::vector<uint8_t> ref = readBytes(card, kFieldLen);
    expectFieldForSector(ref, /*cyl=*/2, /*head=*/1, /*sec=*/1);

    // Stable across re-reads, and not confused with head 0.
    for (int i = 0; i < 64; ++i) {
        strobeRead(card, /*head=*/1);
        ASSERT_EQ(readBytes(card, kFieldLen), ref) << "head-1 re-read #" << i;
    }
    // Switching back to head 0 must serve head-0 data, not stale head-1.
    strobeRead(card, /*head=*/0);
    expectFieldForSector(readBytes(card, kFieldLen), /*cyl=*/2, /*head=*/0, /*sec=*/1);
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_CrossCylinderWithSeek
 * @brief Seeking across cylinders (2→3→4) and re-reading reads each cylinder's
 *   own data — the real boot's read pattern sweeps cylinders between reads.
 * @par Pass criterion  After seeking to cyl C, the served IDAM cyl == C and the
 *   data is cyl C's data; repeated forward/back seeks stay consistent.
 */
TEST_F(K5122Test, Continuous1024_CrossCylinderWithSeek) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    for (int c = 2; c <= 4; ++c) writeDistinctDataTrack(card, c, /*head=*/0);

    auto readCyl = [&](int cyl) {
        strobeRead(card, /*head=*/0);
        expectFieldForSector(readBytes(card, kFieldLen), cyl, /*head=*/0, /*sec=*/1);
    };
    auto stepInward = [&]{ card.ioWrite(0x10,0xFF); card.ioWrite(0x10,0x7F); card.ioWrite(0x10,0xFF); };
    auto stepOutward = [&]{ card.ioWrite(0x10,0xFF); card.ioWrite(0x10,0x5F); card.ioWrite(0x10,0xFF); };  // bit5=0 outward

    seekToCyl(card, 2);
    readCyl(2);
    stepInward(); readCyl(3);
    stepInward(); readCyl(4);
    stepOutward(); readCyl(3);
    stepOutward(); readCyl(2);
    // A few sweeps like the loader's retry/recalibrate pattern.
    for (int r = 0; r < 20; ++r) {
        stepInward(); readCyl(3);
        stepInward(); readCyl(4);
        stepOutward(); stepOutward(); readCyl(2);
    }
    std::filesystem::remove(path);
}

// ─── 12. Control-line semantics: step direction + head-select latch ───────────
//
// These cover the ctrl-Port-A control lines that were only exercised indirectly
// before (step OUTWARD was never tested; the head-select latch only via the
// continuous-read tests).  head 1 is first used by the @OS.COM data read, so the
// head-select path had no direct regression guard.

/**
 * @test K5122Test/Step_Outward_DecrementsCylinder_ClampsAtTrack0
 * @brief Outward /ST pulses (MR/SD bit5=0) move the head toward track 0 and clamp.
 * @par Pass criterion  /TO (ctrl Port B bit7) is 0 at track 0, 1 after stepping
 *   in, 0 again after stepping back out, and stays 0 when over-stepping out.
 */
TEST_F(K5122Test, Step_Outward_DecrementsCylinder_ClampsAtTrack0) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    auto atTrack0 = [&]{ return (card.ioRead(0x12) & 0x80) == 0; };       // /TO=0
    auto stepIn   = [&]{ card.ioWrite(0x10,0xFF); card.ioWrite(0x10,0x7F); card.ioWrite(0x10,0xFF); }; // bit5=1
    auto stepOut  = [&]{ card.ioWrite(0x10,0xFF); card.ioWrite(0x10,0x5F); card.ioWrite(0x10,0xFF); }; // bit5=0

    EXPECT_TRUE(atTrack0());
    stepIn(); stepIn();                    // → cyl 2
    EXPECT_FALSE(atTrack0());
    stepOut(); stepOut();                  // → cyl 0
    EXPECT_TRUE(atTrack0());
    stepOut();                             // over-step: must clamp at track 0
    EXPECT_TRUE(atTrack0()) << "stepping out at track 0 must clamp, not underflow";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/ReadStrobe_LatchesHeadFromBit2
 * @brief A read /STR latches the side-select from bit2 (/FR) into the served
 *   IDAM: bit2=1 → head 0, bit2=0 → head 1.  (bit5 is the step-direction/MK
 *   signal, NOT the side select.)  Directly guards the head-select path the
 *   @OS.COM read (first-ever head-1 access) depends on.
 * @par Pass criterion  IDAM head byte (field[3]) == the head selected by bit2.
 */
TEST_F(K5122Test, ReadStrobe_LatchesHeadFromBit2) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, 2, 0);
    writeDistinctDataTrack(card, 2, 1);
    seekToCyl(card, 2);

    strobeRead(card, /*head=*/0);
    EXPECT_EQ(readBytes(card, 6)[3], 0) << "/STR with bit2=1 must serve head 0";

    strobeRead(card, /*head=*/1);
    EXPECT_EQ(readBytes(card, 6)[3], 1) << "/STR with bit2=0 must serve head 1";
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_Head1_MK1ToggleKeepsHead1
 * @brief Side-select must follow bit2 (/FR), NOT bit5: the 3rd-stage loader
 *   re-syncs the 1024B data with MK toggles 0xB1<->0x81 that flip bit5 (and bit4)
 *   every write while holding the side-select bit2=0 (head 1) stable.  Driving
 *   the head off bit5 made these toggles flip current_head_ to head 0 mid-read,
 *   so the cyl2/head1 @OS.COM read served head-0 data → IDAM never matched →
 *   timeout 'U' (the real boot bug; head 1 is first used by @OS.COM).
 * @par Pass criterion  After 20 bit5-flipping MK toggles mid-read (bit2 held 0),
 *   the streamed data still contains the head-1 marker byte and never head-0's.
 */
TEST_F(K5122Test, Continuous1024_Head1_MKToggleKeepsHead1) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, 2, 0);
    writeDistinctDataTrack(card, 2, 1);
    seekToCyl(card, 2);

    // Start a head-1 read (side-select bit2=0) and HOLD /STR low.
    card.ioWrite(0x10, 0xFB);   // /STR=1, bit2=0 (head 1)
    card.ioWrite(0x10, 0xF3);   // /STR=0 falling → doReadSector(head 1), held low
    ASSERT_EQ(readBytes(card, 6)[3], 1) << "head-1 IDAM expected";

    // MK re-sync toggles, /STR held low (bit3=0), side-select bit2=0 held (head 1).
    // 0xB1 has bit5=1, 0x81 has bit5=0 — the bit5 flip must NOT change the head.
    std::vector<uint8_t> streamed;
    for (int i = 0; i < 20; ++i) {
        card.ioWrite(0x10, 0xB1); streamed.push_back(card.ioRead(0x16));
        card.ioWrite(0x10, 0x81); streamed.push_back(card.ioRead(0x16));
    }
    for (auto b : readBytes(card, 64)) streamed.push_back(b);

    const uint8_t h1 = dataByte(2, 1, 1);   // head-1 data marker
    const uint8_t h0 = dataByte(2, 0, 1);   // head-0 data marker
    bool sawH1 = false, sawH0 = false;
    for (uint8_t b : streamed) { if (b == h1) sawH1 = true; if (b == h0) sawH0 = true; }
    EXPECT_TRUE(sawH1)  << "head-1 data must still stream after bit5 MK toggles";
    EXPECT_FALSE(sawH0) << "head-0 data must NOT appear — head wrongly driven off bit5";
    std::filesystem::remove(path);
}

// ─── 13. ZVE2 access-pattern replay over data containing 0xA1 bytes ────────────
//
// The Continuous1024_* tests above read the served field DIRECTLY (ioRead in a
// loop) and so never exercise the loader's actual byte-wise IDAM search.  The
// real @OS.COM boot fails (cyl 3 head 0 sec 2, error 'S') ONLY when ZVE2 must
// traverse a sector whose DATA contains 0xA1 bytes to reach the wanted sector:
// a 0xA1 data byte is indistinguishable from the A1 address-mark sync in the flat
// byte stream, so the loader's A1/FE search derails.  On hardware the loader
// re-syncs the data separator with MK1 (ctrl Port A bit4) between marks, which
// our model now honours (resyncToNextMark()).  These tests replay ZVE2's exact
// port sequence so the field model is verified against the pattern that fails.

// Faithful replay of the 3rd-stage ZVE2 1024B read routine (loader 0x1F7D IDAM
// search + 0x2038 count-based data read).  Drives the K5122 exactly as ZVE2 does
// (IN A,(16H) per byte; MK1 re-sync via OUT(10H) 0xB5→0x85) and returns the wanted
// sector's 1024 data bytes, or empty{} if the search never locks onto the wanted
// IDAM within the retry budget — which is precisely the real 'S' boot failure.
static std::vector<uint8_t> zve2ReadSector1024(K5122& card, uint8_t wantCyl,
                                               uint8_t wantHead, uint8_t wantSec) {
    auto IN  = [&]{ return card.ioRead(0x16); };
    auto MK1 = [&]{ card.ioWrite(0x10, 0xB5); card.ioWrite(0x10, 0x85); }; // bit4 1→0
    int  idamRetry = 0x3C;                        // [1ED7] mismatch budget
    long guard = 0;                               // the no-FE path has no counter on
    const long kGuard = 16L * (long)kFieldLen;    // HW; bound it like the watchdog

    card.ioWrite(0x10, 0xBD);                     // 1F99 OUT(10),BD
    IN();                                         // 1FB1 throwaway/echo read
    for (;;) {
        if (++guard > kGuard) return {};
        uint8_t b = IN();                         // 1FB7
        while (b == 0xA1) { if (++guard > kGuard) return {}; b = IN(); }  // skip A1 run
        uint8_t cyl = IN();                       // 1FBD (byte after the FE candidate)
        if (b != 0xFE) { MK1(); IN(); continue; } // 1FBF no-FE → 1FA9 MK1 + 1FB1
        uint8_t head = IN();                      // 1FC2
        uint8_t sec  = IN();                      // 1FC7
        uint8_t size = IN();                      // 1FCC
        IN();                                     // 1FD1 (gap byte)
        if (cyl != wantCyl || head != wantHead || sec != wantSec || size != 3) {
            MK1();                                // 2023 → 1FA4 → 1FA9 MK1
            if (--idamRetry == 0) return {};
            IN();                                 // 1FB1 throwaway
            continue;
        }
        // IDAM matched → count-based data read (loader 0x1FD6..0x2061).
        IN();                                     // 1FD6
        for (int i = 0; i < 17; ++i) IN();        // 1FDA ×17
        IN();                                     // 1FDE
        IN();                                     // 2038
        for (int i = 0; i < 6; ++i) IN();         // 203C ×6
        MK1();                                    // 2040 (re-sync to DATA mark)
        IN();                                     // 204C
        uint8_t d = IN();                         // 204E
        while (d == 0xA1) d = IN();               // skip A1 sync up to the data mark
        std::vector<uint8_t> data;
        data.reserve(kSecLen);
        for (int i = 0; i < (int)kSecLen; ++i) data.push_back(IN());  // INIR ×4 = 1024
        return data;
    }
}

// Sector data that carries 0xA1 bytes (like the real cyl3/head0/sec1 the loader
// must read past to reach sec2) — including a worst-case A1 FE pair that looks
// like an IDAM start mid-data.  None of these may be mistaken for an address mark.
static std::vector<uint8_t> a1LadenSector(int cyl, int head, int sec) {
    std::vector<uint8_t> d(kSecLen, dataByte(cyl, head, sec));
    for (size_t i = 7; i < kSecLen; i += 53) d[i] = 0xA1;
    d[0] = 0xA1; d[1] = 0xFE;
    return d;
}
static void writeA1LadenTrack(K5122& card, int cyl, int head) {
    for (int sec = 1; sec <= kSecs; ++sec) {
        auto d = a1LadenSector(cyl, head, sec);
        ASSERT_TRUE(card.drive(0).writeSector((uint8_t)cyl,(uint8_t)head,(uint8_t)sec,d));
    }
}

/**
 * @test K5122Test/Continuous1024_ZVE2Replay_FindsEverySectorWithA1Data
 * @brief Replaying ZVE2's IDAM search + MK1 re-sync finds EVERY sector of a track
 *   whose data contains 0xA1 bytes, and returns each sector's exact data.
 * @details This is the @OS.COM cyl3/head0/sec2 'S' failure as a unit test: before
 *   resyncToNextMark() the loader's byte-wise search traversed the data, where a
 *   0xA1 data byte (sec1 has them) was mistaken for an address-mark sync, so sec2's
 *   IDAM was never located and ZVE2 spun (→ 'S').  With MK1 modelled as a data-
 *   separator re-lock that jumps mark-to-mark (skipping data), the search is
 *   deterministic and data-independent.
 */
TEST_F(K5122Test, Continuous1024_ZVE2Replay_FindsEverySectorWithA1Data) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeA1LadenTrack(card, /*cyl=*/3, /*head=*/0);   // cyl 3 = first seek in @OS.COM
    seekToCyl(card, 3);

    for (int sec = 1; sec <= kSecs; ++sec) {
        strobeRead(card, /*head=*/0);
        card.ioRead(0x16);                            // loader 0x1F61 read-setup pre-read
        auto data = zve2ReadSector1024(card, 3, 0, (uint8_t)sec);
        ASSERT_FALSE(data.empty())
            << "ZVE2 never located cyl3/head0/sec" << sec
            << " — a 0xA1 data byte derailed the IDAM search (the @OS.COM 'S' stall)";
        EXPECT_EQ(data, a1LadenSector(3, 0, sec))
            << "wrong data returned for cyl3/head0/sec" << sec;
    }
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_ZVE2Replay_NoA1DataControl
 * @brief Control: with A1-free data (like the cyl 2 directory that read fine) the
 *   ZVE2 replay also finds every sector — isolating the failure to A1-in-data.
 */
TEST_F(K5122Test, Continuous1024_ZVE2Replay_NoA1DataControl) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeDistinctDataTrack(card, /*cyl=*/3, /*head=*/0);   // no 0xA1 bytes
    seekToCyl(card, 3);

    for (int sec = 1; sec <= kSecs; ++sec) {
        strobeRead(card, /*head=*/0);
        card.ioRead(0x16);
        auto data = zve2ReadSector1024(card, 3, 0, (uint8_t)sec);
        ASSERT_FALSE(data.empty()) << "ZVE2 lost cyl3/head0/sec" << sec << " (A1-free)";
        EXPECT_EQ(data, std::vector<uint8_t>(kSecLen, dataByte(3, 0, sec)));
    }
    std::filesystem::remove(path);
}

/**
 * @test K5122Test/Continuous1024_MK1ResyncJumpsToNextAddressMark
 * @brief An MK1 strobe (ctrl Port A bit4 falling edge, 0xB5→0x85) advances the
 *   1024B continuous stream to the NEXT structural address mark — IDAM → DATA mark
 *   → next sector's IDAM — skipping the data bytes, regardless of their content.
 * @details This is the deterministic core of the @OS.COM fix: the loader re-syncs
 *   its data separator with MK1, so the field model must too.  Without it (MK1
 *   inert) a 0xA1 *data* byte is mistaken for an address-mark sync and the IDAM
 *   search derails (cyl3/head0/sec2 → 'S').  The test holds /STR low like the real
 *   loader and pulses MK1, asserting the served bytes are the next mark — even
 *   though the sector data here is laden with 0xA1/0xFE pairs.
 */
TEST_F(K5122Test, Continuous1024_MK1ResyncJumpsToNextAddressMark) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    writeA1LadenTrack(card, /*cyl=*/3, /*head=*/0);   // data full of 0xA1/0xFE
    seekToCyl(card, 3);

    auto mk1 = [&]{ card.ioWrite(0x10, 0xB5); card.ioWrite(0x10, 0x85); };  // bit4 1→0
    auto in  = [&]{ return card.ioRead(0x16); };

    // Start a head-0 read and HOLD /STR low (bit3=0), as the loader does during
    // the whole ZVE2 read — so the MK1 toggles carry no spurious /STR edge.
    card.ioWrite(0x10, 0xFF);          // /STR=1, bit2=1 (head 0)
    card.ioWrite(0x10, 0xF7);          // /STR=0 → doReadSector, held low

    // Read the sec-1 IDAM header.
    EXPECT_EQ(in(), 0xA1); EXPECT_EQ(in(), 0xFE);
    EXPECT_EQ(in(), 3);  EXPECT_EQ(in(), 0);  EXPECT_EQ(in(), 1);  EXPECT_EQ(in(), 3);

    // MK1 → must re-lock on the DATA address mark of the same sector (A1 FB),
    // NOT serve gap/data bytes.
    mk1();
    EXPECT_EQ(in(), 0xA1) << "MK1 did not re-sync to the DATA address mark";
    EXPECT_EQ(in(), 0xFB) << "expected DATA mark 0xFB after MK1 re-sync";

    // MK1 again → must re-lock on the NEXT sector's IDAM (sec 2), skipping all the
    // 1024 A1-laden data bytes of sector 1.
    mk1();
    EXPECT_EQ(in(), 0xA1); EXPECT_EQ(in(), 0xFE);
    EXPECT_EQ(in(), 3) << "IDAM cyl"; EXPECT_EQ(in(), 0) << "IDAM head";
    EXPECT_EQ(in(), 2) << "MK1 re-sync must reach sector 2's IDAM, not be lost in "
                          "sector 1's A1-laden data";
    EXPECT_EQ(in(), 3) << "IDAM size code";
    std::filesystem::remove(path);
}
