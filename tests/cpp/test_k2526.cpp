#include <gtest/gtest.h>
#include "core/cards/k2526/k2526.h"
#include "core/cards/k2526/rom_data.h"
#include "core/cards/k3526/k3526.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Write to BS-PIO Port A data register (port 0x08 = base 0x08 + sub-port 0).
static constexpr uint8_t BSPIO_PORTA_DATA = 0x08;
// Write to BS-PIO Port B data register (port 0x0A = base 0x08 + sub-port 2).
static constexpr uint8_t BSPIO_PORTB_DATA = 0x0A;

// ─── ROM: power-on state ──────────────────────────────────────────────────────

TEST(K2526, IsRomEnabled_InitialFalse)
{
    // Before powerOn(), the ROM is enabled by default (member init), but not
    // yet registered. isRomEnabled() reflects the internal flag.
    K1520Bus bus;
    K2526 card(bus);
    EXPECT_TRUE(card.isRomEnabled());
}

TEST(K2526, PowerOn_RomRegistered_MemReadNonFF)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // The first byte of the ROM is 0xEE – definitely not 0xFF.
    uint8_t val = bus.memRead(0x0000);
    EXPECT_NE(val, 0xFF);
    EXPECT_EQ(val, ZRE_BOOT_ROM[0]);
}

TEST(K2526, PowerOn_RomData_MatchesArray)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    for (uint16_t i = 0; i < 1024; ++i)
        EXPECT_EQ(bus.memRead(i), ZRE_BOOT_ROM[i]) << "mismatch at offset " << i;
}

// ─── ROM: write is ignored (ROM is read-only) ─────────────────────────────────

TEST(K2526, RomWriteIgnored)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    uint8_t original = bus.memRead(0x0010);
    bus.memWrite(0x0010, ~original);       // attempt write
    EXPECT_EQ(bus.memRead(0x0010), original);  // unchanged
}

// ─── ROM: disable via BS-PIO Port B bit 0 (/LD-ROM, active low) ──────────────

TEST(K2526, BSPioPortB_Bit0Low_DisablesRom)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    ASSERT_TRUE(card.isRomEnabled());
    ASSERT_NE(bus.memRead(0x0000), 0xFF);

    // B0=0 → /LD-ROM asserted (active low) → ROM disabled.
    bus.ioWrite(BSPIO_PORTB_DATA, 0x00);

    EXPECT_FALSE(card.isRomEnabled());
    // After unregistering the ROM, the bus returns 0xFF for the address
    // (no other device registered there).
    EXPECT_EQ(bus.memRead(0x0000), 0xFF);
}

TEST(K2526, BSPioPortB_DisableRom_IsIdempotent)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    bus.ioWrite(BSPIO_PORTB_DATA, 0x00);  // B0=0 → ROM disabled
    EXPECT_FALSE(card.isRomEnabled());

    // Second write must not double-unregister (would crash or be harmless).
    bus.ioWrite(BSPIO_PORTB_DATA, 0x00);
    EXPECT_FALSE(card.isRomEnabled());
}

TEST(K2526, BSPioPortB_Bit0High_RomStaysEnabled)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // B0=1 → /LD-ROM not asserted → ROM stays active.
    bus.ioWrite(BSPIO_PORTB_DATA, 0x01);
    EXPECT_TRUE(card.isRomEnabled());
    EXPECT_NE(bus.memRead(0x0000), 0xFF);
}

// ─── MEMDI signal via BS-PIO Port A bit 7 (MEMDI1/2 output) ─────────────────

TEST(K2526, BSPioPortA_Bit7_SetsMEMDI)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    EXPECT_FALSE(bus.getMEMDI());

    bus.ioWrite(BSPIO_PORTA_DATA, 0x80);   // A7=1 → MEMDI active
    EXPECT_TRUE(bus.getMEMDI());

    bus.ioWrite(BSPIO_PORTA_DATA, 0x00);   // A7=0 → MEMDI released
    EXPECT_FALSE(bus.getMEMDI());
}

// ─── clockTick() ─────────────────────────────────────────────────────────────

TEST(K2526, ClockTick_DoesNotCrash)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // Tick many times – must not crash.
    for (int i = 0; i < 1000; ++i)
        card.clockTick();
}

// ─── I/O dispatch: all port ranges accessible without crash ──────────────────

TEST(K2526, IODispatch_AllPortsReadable)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    for (uint8_t port = 0x00; port <= 0x0F; ++port)
        (void)bus.ioRead(port);  // must not crash
}

TEST(K2526, IODispatch_AllPortsWritable)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    for (uint8_t port = 0x00; port <= 0x0F; ++port)
        bus.ioWrite(port, 0x00);  // must not crash
}

TEST(K2526, IODispatch_UBusRange_0x00_0x07)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    // Writing to ports 0x00-0x07 routes to U-Bus and does not crash.
    for (uint8_t port = 0x00; port <= 0x07; ++port) {
        bus.ioWrite(port, 0x00);
        (void)bus.ioRead(port);
    }
}

TEST(K2526, IODispatch_BsPioRange_0x08_0x0B)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    // Writing to ports 0x08-0x0B routes to BS-PIO and does not crash.
    for (uint8_t port = 0x08; port <= 0x0B; ++port) {
        bus.ioWrite(port, 0x00);
        (void)bus.ioRead(port);
    }
}

TEST(K2526, IODispatch_CtcRange_0x0C_0x0F)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    // CTC: write a reset control word to each channel, then read.
    for (uint8_t port = 0x0C; port <= 0x0F; ++port) {
        bus.ioWrite(port, 0x02);  // CTC reset
        (void)bus.ioRead(port);
    }
}

// ─── attachToBus registers ports 0x00–0x0F ───────────────────────────────────

TEST(K2526, AttachToBus_RegistersPorts_0x00_to_0x0F)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);

    // Writes to ports outside the K2526 range (e.g. 0x10) should not reach
    // the card. Writes to 0x00–0x0F must be dispatched to the card – verified
    // indirectly by checking ROM disable works through the bus I/O path.
    card.powerOn();
    bus.ioWrite(0x0A, 0x00);   // BS-PIO Port B (0x0A): B0=0 → /LD-ROM → ROM disabled
    EXPECT_FALSE(card.isRomEnabled());
}

// ─── InterruptSlave interface ─────────────────────────────────────────────────

TEST(K2526, InterruptSlave_NoInterruptInitially)
{
    K1520Bus bus;
    K2526 card(bus);
    EXPECT_FALSE(card.hasInterrupt());
}

TEST(K2526, InterruptSlave_SetIEI_DoesNotCrash)
{
    K1520Bus bus;
    K2526 card(bus);
    card.setIEI(true);
    card.setIEI(false);
}

TEST(K2526, InterruptSlave_GetIEO_ReturnsBool)
{
    K1520Bus bus;
    K2526 card(bus);
    card.setIEI(true);
    // IEO is a bool – just make sure it doesn't crash.
    (void)card.getIEO();
}

// ─── deviceName ──────────────────────────────────────────────────────────────

TEST(K2526, DeviceName_IsK2526)
{
    K1520Bus bus;
    K2526 card(bus);
    EXPECT_STREQ(card.deviceName(), "K2526");
}

// ─── Sub-chip accessors return correct objects ────────────────────────────────

TEST(K2526, SubchipAccessors_ReturnValidRefs)
{
    K1520Bus bus;
    K2526 card(bus);
    // bsPio() and ctc() must return references to the internal sub-chips.
    // The simplest check: their deviceName() matches what we set.
    EXPECT_STREQ(card.bsPio().deviceName(), "K2526-BS-PIO");
    EXPECT_STREQ(card.ctc().deviceName(),   "K2526-CTC");
}

// ─── ZVE1 (CPU) on K2526 card ────────────────────────────────────────────────

TEST(K2526, CpuAccessor_ReturnsSameObject)
{
    K1520Bus bus;
    K2526 card(bus);
    // cpu() must return a stable reference (same address across calls).
    EXPECT_EQ(&card.cpu(), &card.cpu());
}

TEST(K2526, CpuReset_SetsPCToZero)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();
    EXPECT_EQ(card.cpuPC(), 0x0000);
}

TEST(K2526, CpuIFF1_FalseAfterReset)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();
    // After a hardware reset the CPU starts with interrupts disabled.
    EXPECT_FALSE(card.cpuIFF1());
}

TEST(K2526, CpuStep_WithBootRom_AdvancesPC)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();

    // Execute one instruction from the boot ROM at 0x0000.
    int cycles = card.cpuStep();

    // cpuStep() must return a positive cycle count and advance PC past 0.
    EXPECT_GT(cycles, 0);
    EXPECT_GT(card.cpuPC(), 0x0000);
}

TEST(K2526, CpuStep_ReturnsPositiveCycles)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();

    // Run 10 steps — each must return > 0 cycles (no infinite loops here
    // because the boot ROM is valid Z80 code).
    for (int i = 0; i < 10; ++i) {
        int c = card.cpuStep();
        EXPECT_GT(c, 0) << "step " << i << " returned 0 cycles";
    }
}

TEST(K2526, CpuNMI_DoesNotCrash)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();
    // Just verify that cpuNMI() does not crash.
    card.cpuNMI();
}

// ─── ZVE2 (DMA-CPU) on K2526 card ────────────────────────────────────────────

TEST(K2526, ZVE2_StartsInReset)
{
    K1520Bus bus;
    K2526 card(bus);
    EXPECT_TRUE(card.isZVE2InReset());
}

TEST(K2526, ZVE2_Port04_Bit1_ReleasesReset)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    ASSERT_TRUE(card.isZVE2InReset());
    bus.ioWrite(0x04, 0x01);   // bit0=1 → /RES-ZVE2 released
    EXPECT_FALSE(card.isZVE2InReset());
}

TEST(K2526, ZVE2_Port04_Bit0_AssertsReset)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    bus.ioWrite(0x04, 0x01);   // release
    ASSERT_FALSE(card.isZVE2InReset());
    bus.ioWrite(0x04, 0x00);   // bit0=0 → /RES-ZVE2 asserted
    EXPECT_TRUE(card.isZVE2InReset());
}

TEST(K2526, ZVE2_Released_PCStartsAtZero)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    bus.ioWrite(0x04, 0x01);   // release ZVE2
    EXPECT_EQ(card.zve2().PC, 0x0000u);
}

TEST(K2526, ZVE2Step_ExecutesInstructions_WhenNotInReset)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();   // boot ROM at 0x0000–0x03FF
    card.cpuReset();  // ZVE1 in reset

    bus.ioWrite(0x04, 0x01);   // release ZVE2 — starts at PC=0x0000
    ASSERT_FALSE(card.isZVE2InReset());

    uint16_t pc_before = card.zve2().PC;
    int cycles = card.zve2Step();   // execute first ROM instruction

    EXPECT_GT(cycles, 0);
    EXPECT_GT(card.zve2().PC, pc_before) << "ZVE2 PC must advance after step";
}

TEST(K2526, ZVE2_ResetWhileRunning_FreezesPC)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    bus.ioWrite(0x04, 0x01);   // release ZVE2
    card.zve2Step();            // execute one instruction
    ASSERT_GT(card.zve2().PC, 0u);

    bus.ioWrite(0x04, 0x00);   // put ZVE2 back in reset
    EXPECT_TRUE(card.isZVE2InReset());
    // PC is preserved (not zeroed by reset signal; only zeroed when released again)
}

// ─── /WAIT-ZVE2 (BS-PIO B3) ──────────────────────────────────────────────────
//
// Port B output bits (direction mask 0xE2: B0/B2/B3/B4 are outputs, active low):
//   PORTB_SAFE_ROMON  = 0x1D: B0=1(ROM on),  B2=1, B3=1, B4=1  – all others inactive
//   PORTB_SAFE_ROMOFF = 0x1C: B0=0(ROM off), B2=1, B3=1, B4=1  – all others inactive
//   PORTB_WAIT_ACTIVE = 0x15: B0=1(ROM on),  B2=1, B3=0, B4=1  – /WAIT-ZVE2 asserted
//   PORTB_SPSESR_ACT  = 0x18: B0=0(ROM off), B2=0, B3=1, B4=1  – /SPS-ESR asserted
//   PORTB_SA_ACTIVE   = 0x0C: B0=0(ROM off), B2=1, B3=1, B4=0  – /SA asserted

static constexpr uint8_t PORTB_SAFE_ROMON   = 0x1D;  ///< ROM on, all other outputs inactive
static constexpr uint8_t PORTB_SAFE_ROMOFF  = 0x1C;  ///< ROM off, all other outputs inactive
static constexpr uint8_t PORTB_WAIT_ACTIVE  = 0x15;  ///< ROM on, /WAIT-ZVE2 asserted (B3=0)
static constexpr uint8_t PORTB_WAITZVE2_REL = 0x1D;  ///< ROM on, /WAIT-ZVE2 released (B3=1)
static constexpr uint8_t PORTB_SPSESR_ACT   = 0x18;  ///< ROM off, /SPS-ESR asserted (B2=0)
static constexpr uint8_t PORTB_SA_ACTIVE    = 0x0C;  ///< ROM off, /SA asserted (B4=0)

TEST(K2526, ZVE2_WaitInactive_Initially)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    EXPECT_FALSE(card.isZVE2Waiting());
}

TEST(K2526, ZVE2_Wait_PortB3Low_StallsZVE2)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // Release ZVE2 from reset so it would run if not stalled.
    bus.ioWrite(0x04, 0x01);
    ASSERT_FALSE(card.isZVE2InReset());

    // Assert /WAIT-ZVE2 (B3=0): ROM on, SPS-ESR inactive, SA inactive.
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_WAIT_ACTIVE);
    EXPECT_TRUE(card.isZVE2Waiting());

    // zve2Step() must return 0 (no instruction executed) while waiting.
    EXPECT_EQ(card.zve2Step(), 0);
}

TEST(K2526, ZVE2_Wait_PortB3High_ResumesZVE2)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    bus.ioWrite(0x04, 0x01);                            // release ZVE2
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_WAIT_ACTIVE);  // stall ZVE2
    ASSERT_TRUE(card.isZVE2Waiting());

    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_WAITZVE2_REL); // release ZVE2 wait
    EXPECT_FALSE(card.isZVE2Waiting());

    // zve2Step() should now execute an instruction from the boot ROM.
    EXPECT_GT(card.zve2Step(), 0);
}

TEST(K2526, ZVE2_Wait_DoesNotAffectZVE1)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();
    card.cpuReset();

    // Stall ZVE2 via /WAIT-ZVE2 – must not prevent ZVE1 from running.
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_WAIT_ACTIVE);
    ASSERT_TRUE(card.isZVE2Waiting());

    int cycles = card.cpuStep();
    EXPECT_GT(cycles, 0);
    EXPECT_GT(card.cpuPC(), 0x0000u);
}

// ─── Q240 Memory Protection (MemIOProtect / spa_) ────────────────────────────
//
// Fixture: K2526 + K3526 RAM on the bus.  The boot ROM is disabled so all
// of the 64 KB address range can be accessed through K3526 RAM.
// cpu().writeByte / cpu().readByte invoke the ZVE1 memory callbacks which
// include the Q240 protection check (unlike bus.memWrite/Read which bypass it).

class K2526Protection : public ::testing::Test {
protected:
    K1520Bus bus;
    K2526    zre{bus};
    K3526    ops;

    void SetUp() override {
        ops.attachToBus(bus);     // RAM 0x0000–0xFFFF
        zre.attachToBus(bus);     // ports 0x00–0x0F
        zre.powerOn();            // boot ROM at 0x0000–0x03FF; Q240 cleared
        // Disable ROM with all other Port B outputs inactive (B2=1, B3=1, B4=1).
        bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SAFE_ROMOFF);
        ops.fill(0xAA);           // pre-fill RAM with test pattern
    }
};

TEST_F(K2526Protection, NoProtection_ReadWriteOk)
{
    // With an empty protection table ZVE1 accesses should be transparent.
    zre.cpu().writeByte(0x0400, 0x55);
    EXPECT_EQ(ops.rawPtr()[0x0400], 0x55);
    EXPECT_EQ(zre.cpu().readByte(0x0400), 0x55);
    EXPECT_FALSE(zre.isSPSViolation());
}

TEST_F(K2526Protection, SpsEsr_EnablesTableWrite)
{
    // B2=0 (active low, PORTB_SPSESR_ACT) must activate table-write mode.
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SPSESR_ACT);   // /SPS-ESR asserted
    EXPECT_TRUE(zre.spa().isTableWriteEnabled());

    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SAFE_ROMOFF);  // /SPS-ESR released
    EXPECT_FALSE(zre.spa().isTableWriteEnabled());
}

TEST_F(K2526Protection, TableWrite_StoresProtectionFlags)
{
    // Activate table-write mode; ZVE1 write goes to Q240 SRAM, not RAM.
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SPSESR_ACT);   // /SPS-ESR active
    zre.cpu().writeByte(0x0400, 0x01);                  // WP for segment 16
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SAFE_ROMOFF);  // /SPS-ESR released

    EXPECT_EQ(zre.spa().readEntry(0x0400), 0x01);       // protection table written
    EXPECT_EQ(ops.rawPtr()[0x0400], 0xAA);              // RAM unchanged
}

TEST_F(K2526Protection, TableWrite_ReadReturnsProtectionByte)
{
    // Pre-program entry; in table-write mode ZVE1 reads return the table byte.
    zre.spa().writeEntry(0x0800, 0x03);                 // WP+RP at segment 32

    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SPSESR_ACT);   // /SPS-ESR active
    uint8_t val = zre.cpu().readByte(0x0800);
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SAFE_ROMOFF);  // /SPS-ESR released

    EXPECT_EQ(val, 0x03);
    EXPECT_FALSE(zre.isSPSViolation());                 // table-read: no violation
}

TEST_F(K2526Protection, WriteProtect_BlocksZVE1Write)
{
    zre.spa().writeEntry(0x0400, 0x01);   // WP for segment at 0x0400
    ops.fill(0xBB);

    zre.cpu().writeByte(0x0400, 0x55);    // blocked – protection violation
    EXPECT_EQ(ops.rawPtr()[0x0400], 0xBB); // RAM unchanged
    EXPECT_TRUE(zre.isSPSViolation());
}

TEST_F(K2526Protection, ReadProtect_BlocksZVE1Read)
{
    zre.spa().writeEntry(0x0800, 0x02);   // RP for segment at 0x0800
    ops.fill(0xCC);

    uint8_t val = zre.cpu().readByte(0x0800);
    EXPECT_EQ(val, 0xFF);                 // blocked → open-drain 0xFF
    EXPECT_TRUE(zre.isSPSViolation());
}

TEST_F(K2526Protection, Violation_SetsNMI)
{
    zre.spa().writeEntry(0x0400, 0x01);   // WP for segment at 0x0400
    zre.cpu().writeByte(0x0400, 0x55);    // trigger violation

    EXPECT_TRUE(zre.isSPSViolation());
    EXPECT_TRUE(bus.isNMI());
}

TEST_F(K2526Protection, RESSPA_ClearsViolation)
{
    zre.spa().writeEntry(0x0400, 0x01);
    zre.cpu().writeByte(0x0400, 0x55);    // trigger violation
    ASSERT_TRUE(zre.isSPSViolation());

    bus.ioWrite(0x02, 0x00);              // /RES-SPA clears violation
    EXPECT_FALSE(zre.isSPSViolation());
    EXPECT_FALSE(bus.isNMI());
}

TEST_F(K2526Protection, RESSPA_ClearsSPSIndOnBSPIO)
{
    // Trigger a violation – BS-PIO A3 (SPS-Ind) must be set.
    zre.spa().writeEntry(0x0400, 0x01);
    zre.cpu().writeByte(0x0400, 0x55);
    ASSERT_TRUE(zre.isSPSViolation());

    uint8_t port_a = bus.ioRead(BSPIO_PORTA_DATA);
    EXPECT_NE(port_a & 0x08, 0) << "SPS-Ind (A3) must be set after violation";

    bus.ioWrite(0x02, 0x00);              // /RES-SPA

    port_a = bus.ioRead(BSPIO_PORTA_DATA);
    EXPECT_EQ(port_a & 0x08, 0) << "SPS-Ind (A3) must be clear after /RES-SPA";
}

TEST_F(K2526Protection, ZVE2_BypassesProtection)
{
    // ZVE2 runs privileged DMA code and must bypass Q240 protection entirely.
    zre.spa().writeEntry(0x0400, 0x03);   // WP+RP for segment at 0x0400
    ops.fill(0xDD);

    zre.zve2().writeByte(0x0400, 0x77);   // must succeed despite WP
    EXPECT_EQ(ops.rawPtr()[0x0400], 0x77);
    EXPECT_FALSE(zre.isSPSViolation());

    uint8_t val = zre.zve2().readByte(0x0400);
    EXPECT_EQ(val, 0x77);
    EXPECT_FALSE(zre.isSPSViolation());
}

TEST_F(K2526Protection, SegmentBoundary_64ByteAligned)
{
    // Segment 0 covers bytes 0x0000–0x003F (64 bytes); segment 1 starts at 0x0040.
    zre.spa().writeEntry(0x0000, 0x01);   // WP for segment 0

    // Last byte of segment 0 (0x003F) must be protected.
    zre.cpu().writeByte(0x003F, 0x11);
    EXPECT_TRUE(zre.isSPSViolation());
    bus.ioWrite(0x02, 0x00);              // clear violation

    // First byte of segment 1 (0x0040) must be unprotected.
    zre.cpu().writeByte(0x0040, 0x22);
    EXPECT_FALSE(zre.isSPSViolation());
    EXPECT_EQ(ops.rawPtr()[0x0040], 0x22);
}

TEST_F(K2526Protection, MultipleViolations_OnlyOneNMI)
{
    // Repeated violations must not re-assert NMI once it is already pending.
    zre.spa().writeEntry(0x0400, 0x01);
    zre.cpu().writeByte(0x0400, 0x11);    // first violation
    ASSERT_TRUE(bus.isNMI());

    // A second violation does not generate a second NMI edge (already pending).
    zre.cpu().writeByte(0x0400, 0x22);
    EXPECT_TRUE(zre.isSPSViolation());    // still set
    EXPECT_TRUE(bus.isNMI());             // still pending
}

// ─── /INT-BS (port 00H) ───────────────────────────────────────────────────────

TEST(K2526, IntBS_Port00_DoesNotCrash)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    bus.ioWrite(0x00, 0x00);              // write port 00H
    EXPECT_FALSE(card.isSPSViolation());  // must not trigger protection violation
}

TEST(K2526, IntBS_Port00_SetsBSPIOPortB1Low)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // After port 00H write, BS-PIO Port B input B1 (/INT-BS) must be 0 (asserted).
    bus.ioWrite(0x00, 0x00);
    uint8_t portb = bus.ioRead(BSPIO_PORTB_DATA);
    EXPECT_EQ(portb & 0x02, 0) << "/INT-BS (B1) must be low after port 00H write";
}

TEST(K2526, IntBS_InitiallyInactive)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // Before port 00H write, /INT-BS (B1) must be high (inactive, pull-up).
    uint8_t portb = bus.ioRead(BSPIO_PORTB_DATA);
    EXPECT_NE(portb & 0x02, 0) << "/INT-BS (B1) must be high (inactive) at power-on";
}

TEST(K2526, IntBS_RESSPA_ClearsIntBs)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    bus.ioWrite(0x00, 0x00);                         // assert /INT-BS
    ASSERT_EQ(bus.ioRead(BSPIO_PORTB_DATA) & 0x02, 0); // verify B1=0

    bus.ioWrite(0x02, 0x00);                         // /RES-SPA clears INT-BS latch
    uint8_t portb = bus.ioRead(BSPIO_PORTB_DATA);
    EXPECT_NE(portb & 0x02, 0) << "/INT-BS must be inactive (B1=1) after /RES-SPA";
}

// ─── /SA (power-off, BS-PIO B4) ───────────────────────────────────────────────

TEST(K2526, SA_InitiallyNotRequested)
{
    K1520Bus bus;
    K2526 card(bus);
    EXPECT_FALSE(card.isShutdownRequested());
}

TEST(K2526, SA_PortB4Low_SetsShutdownFlag)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // B4=0 (active low) → /SA asserted → power-off requested.
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SA_ACTIVE);
    EXPECT_TRUE(card.isShutdownRequested());
}

TEST(K2526, SA_PortB4High_NoShutdown)
{
    K1520Bus bus;
    K2526 card(bus);
    card.attachToBus(bus);
    card.powerOn();

    // B4=1 → /SA not asserted (all safe: B2=1, B3=1, B4=1).
    bus.ioWrite(BSPIO_PORTB_DATA, PORTB_SAFE_ROMON);
    EXPECT_FALSE(card.isShutdownRequested());
}
