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
    card.ioWrite(0x10, 0xF7);   // /STR falling edge (read)
    card.ioWrite(0x10, 0xFF);

    card.dmaUpdate();

    // The read buffer is the rotating track as a continuous IDAM stream, each
    // sector being [00 FE cyl head sec size CRC CRC <128 data> CRC CRC] (138 B).
    EXPECT_EQ(card.ioRead(0x16), 0x00) << "byte0 = sync/gap";
    EXPECT_EQ(card.ioRead(0x16), 0xFE) << "byte1 = IDAM address mark";
    card.ioRead(0x16);                                    // byte2 = cylinder
    card.ioRead(0x16);                                    // byte3 = head
    EXPECT_EQ(card.ioRead(0x16), 0x01) << "byte4 = sector ID (1-based)";
    card.ioRead(0x16);                                    // byte5 = size code
    card.ioRead(0x16);                                    // byte6 = IDAM CRC
    card.ioRead(0x16);                                    // byte7 = IDAM CRC
    // Payload starts at offset 8; the blank image is filled with 0xE5.
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
    EXPECT_EQ(card.ioRead(0x16), 0x00) << "byte0 = sync";
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
