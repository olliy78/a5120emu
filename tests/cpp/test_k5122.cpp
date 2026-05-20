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

TEST_F(K5122Test, MountValidDisk_DriveBecomesActive) {
    auto path = tmpImage();
    EXPECT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, MountNonexistentFile_ReturnsFalse) {
    EXPECT_FALSE(card.mountDisk(0, "/nonexistent/disk.img", fmt));
    EXPECT_FALSE(card.isDiskActive(0));
}

TEST_F(K5122Test, UnmountDisk_DriveBecomesInactive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(card.isDiskActive(0));

    card.unmountDisk(0);
    EXPECT_FALSE(card.isDiskActive(0));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, UnmountNotMounted_NocrashRetursTrue) {
    // Calling unmountDisk on a drive with no disk should not crash.
    EXPECT_TRUE(card.unmountDisk(0));
    EXPECT_FALSE(card.isDiskActive(0));
}

// ─── 2. Write-protect flag ───────────────────────────────────────────────────

TEST_F(K5122Test, MountWithWriteProtect_IsWriteProtected) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/true));
    EXPECT_TRUE(card.isDiskWriteProtected(0));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, MountWithoutWriteProtect_IsNotWriteProtected) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/false));
    EXPECT_FALSE(card.isDiskWriteProtected(0));
    std::filesystem::remove(path);
}

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

TEST_F(K5122Test, MountDriveOutOfRange_ReturnsFalse) {
    auto path = tmpImage();
    EXPECT_FALSE(card.mountDisk(-1, path, fmt));
    EXPECT_FALSE(card.mountDisk(4,  path, fmt));
    std::filesystem::remove(path);
}

TEST_F(K5122Test, IsDiskActiveOutOfRange_ReturnsFalse) {
    EXPECT_FALSE(card.isDiskActive(-1));
    EXPECT_FALSE(card.isDiskActive(4));
}

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

TEST_F(K5122Test, StatusPortB_NotMounted_RDYLInactive) {
    // Drive 0 not mounted; after selecting it, /RDYL (bit 0) should be 1.
    card.ioWrite(0x18, 0x00);           // select drive 0
    uint8_t status = card.ioRead(0x12); // read ctrl Port B
    EXPECT_NE(status & 0x01, 0) << "Expected /RDYL=1 (not ready) when no disk mounted";
}

TEST_F(K5122Test, StatusPortB_Mounted_RDYLActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);           // select drive 0
    uint8_t status = card.ioRead(0x12);
    EXPECT_EQ(status & 0x01, 0) << "Expected /RDYL=0 (ready) when disk is mounted";
    std::filesystem::remove(path);
}

TEST_F(K5122Test, StatusPortB_WriteProtected_WPActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/true));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    // /WP = bit 5; active low → 0 when write protected.
    EXPECT_EQ(status & 0x20, 0) << "Expected /WP=0 (write-protected) in status";
    std::filesystem::remove(path);
}

TEST_F(K5122Test, StatusPortB_NotWriteProtected_WPInactive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt, /*write_protect=*/false));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    EXPECT_NE(status & 0x20, 0) << "Expected /WP=1 (not write-protected) in status";
    std::filesystem::remove(path);
}

TEST_F(K5122Test, StatusPortB_AtTrack0_FWActive) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);
    uint8_t status = card.ioRead(0x12);
    // /FW = bit 6; active low → 0 when at track 0 (initial position).
    EXPECT_EQ(status & 0x40, 0) << "Expected /FW=0 (at track 0) after mount";
    std::filesystem::remove(path);
}

// ─── 7. Step and ctrl signal sequence (no crash) ─────────────────────────────

TEST_F(K5122Test, StepPulse_WithMountedDrive_DoesNotCrash) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);       // select drive 0

    // Step inward: /ST=0 (bit 7 = 0 = step pulse), MR/SD=0 (bit 5 = 0 = inward).
    // 0x5F = 0b01011111 → bit 7=0, bit 5=0 (inward), all others inactive.
    card.ioWrite(0x10, 0xFF);       // all high (inactive), establishes prev state
    card.ioWrite(0x10, 0x5F);       // /ST = 0 (step pulse), MR/SD = 0 (inward)
    card.ioWrite(0x10, 0xFF);       // release

    // After one inward step from track 0, /FW (bit 6) should be 1 (not at track 0).
    uint8_t status = card.ioRead(0x12);
    EXPECT_NE(status & 0x40, 0) << "Expected /FW=1 (not at track 0) after inward step";

    std::filesystem::remove(path);
}

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

TEST_F(K5122Test, DMA_ReadMode_SectorDataAvailableAfterDmaUpdate) {
    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    card.ioWrite(0x18, 0x00);

    card.ioWrite(0x10, 0xFF);
    card.ioWrite(0x10, 0xF7);   // /STR falling edge (read)
    card.ioWrite(0x10, 0xFF);

    card.dmaUpdate();

    // After dmaUpdate() the sector buffer is filled; the first byte from port
    // 0x16 must be consistent across two reads (i.e. buffer advances correctly).
    uint8_t b0 = card.ioRead(0x16);
    uint8_t b1 = card.ioRead(0x16);
    // For a blank image (0xE5) both bytes must equal 0xE5.
    EXPECT_EQ(b0, 0xE5) << "first sector byte should be 0xE5 (blank image)";
    EXPECT_EQ(b1, 0xE5) << "second sector byte should be 0xE5 (blank image)";
    std::filesystem::remove(path);
}

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

TEST_F(K5122Test, DmaUpdate_NoPending_DoesNotCrash) {
    // Calling dmaUpdate() when no DMA is pending must be a no-op.
    EXPECT_FALSE(bus.isBUSRQ());
    card.dmaUpdate();
    EXPECT_FALSE(bus.isBUSRQ());
}

// ─── 8. InterruptSlave interface ─────────────────────────────────────────────

TEST_F(K5122Test, InterruptSlave_SetIEI_NocrashGetIEO) {
    card.setIEI(true);
    // With no interrupts pending, IEO should propagate through.
    EXPECT_TRUE(card.getIEO());
    EXPECT_FALSE(card.hasInterrupt());
}

TEST_F(K5122Test, InterruptSlave_IEIFalse_IEOFalse) {
    card.setIEI(false);
    EXPECT_FALSE(card.getIEO());
}

// ─── 9. drive() accessor ─────────────────────────────────────────────────────

TEST_F(K5122Test, DriveAccessor_ReturnsValidReference) {
    // Just verify the accessor doesn't crash and returns the same object.
    FloppyDrive& d0 = card.drive(0);
    EXPECT_FALSE(d0.isMounted());

    auto path = tmpImage();
    ASSERT_TRUE(card.mountDisk(0, path, fmt));
    EXPECT_TRUE(d0.isMounted());   // accessor returns same object as mountDisk operates on
    std::filesystem::remove(path);
}
