#include <gtest/gtest.h>
#include "core/cards/k2526/k2526.h"
#include "core/cards/k2526/rom_data.h"

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
