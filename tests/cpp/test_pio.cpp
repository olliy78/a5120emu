#include <gtest/gtest.h>
#include "core/primitives/z80_pio.h"

// Helper: write mode-select control word for port A (port offset 1)
// Mode 0=Output: 0x0F, Mode 1=Input: 0x4F, Mode 2=Bidir: 0x8F, Mode 3=BitCtl: 0xCF
static constexpr uint8_t modeWord(uint8_t mode) {
    return static_cast<uint8_t>((mode << 6) | 0x0F);
}
// ICW: bit7=IE, bit6=AND, bit5=ActiveHigh, bit4=MaskFollows, bits3:0=0111
static constexpr uint8_t icw(bool ie, bool and_logic = false,
                              bool active_high = false, bool mask_follows = false) {
    return static_cast<uint8_t>(
        (ie          ? 0x80 : 0) |
        (and_logic   ? 0x40 : 0) |
        (active_high ? 0x20 : 0) |
        (mask_follows? 0x10 : 0) |
        0x07);
}

// ─── Mode 0 (Output) ─────────────────────────────────────────────────────────

TEST(Z80PIO, Mode0_CPUWrite_UpdatesOutputLatch) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));   // portA → output mode

    pio.ioWrite(0, 0x42);
    EXPECT_EQ(pio.portARead(), 0x42);
}

TEST(Z80PIO, Mode0_CPUWrite_FiresCallback) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));

    uint8_t received = 0;
    pio.setPortAOutputCallback([&](uint8_t d){ received = d; });

    pio.ioWrite(0, 0xBE);
    EXPECT_EQ(received, 0xBE);
}

TEST(Z80PIO, Mode0_CPURead_ReturnsOutputLatch) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));
    pio.ioWrite(0, 0xA5);
    EXPECT_EQ(pio.ioRead(0), 0xA5);
}

TEST(Z80PIO, Mode0_PortBWrite_FiresCallbackAndRead) {
    Z80PIO pio;
    pio.ioWrite(3, modeWord(0));   // portB → output mode

    uint8_t received = 0;
    pio.setPortBOutputCallback([&](uint8_t d){ received = d; });

    pio.ioWrite(2, 0x99);
    EXPECT_EQ(received, 0x99);
    EXPECT_EQ(pio.portBRead(), 0x99);
}

// ─── Mode 1 (Input) ──────────────────────────────────────────────────────────

TEST(Z80PIO, Mode1_Default_ExternalWrite_CPUReadsBack) {
    Z80PIO pio;
    // Default mode is 1 (Input), no mode change needed

    pio.portAWrite(0x55);
    EXPECT_EQ(pio.ioRead(0), 0x55);
}

TEST(Z80PIO, Mode1_ExplicitSet_ExternalWrite_CPUReadsBack) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));

    pio.portAWrite(0xDE);
    EXPECT_EQ(pio.ioRead(0), 0xDE);
}

TEST(Z80PIO, Mode1_CPUWrite_IsIgnored) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));

    uint8_t cb_called = 0;
    pio.setPortAOutputCallback([&](uint8_t){ ++cb_called; });

    pio.portAWrite(0xAA);
    pio.ioWrite(0, 0xFF);  // should be ignored in input mode
    EXPECT_EQ(pio.ioRead(0), 0xAA);
    EXPECT_EQ(cb_called, 0);
}

TEST(Z80PIO, Mode1_PortARead_ReturnsHighZ) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));
    // In input mode the PIO drives nothing onto pins
    EXPECT_EQ(pio.portARead(), 0xFF);
}

// ─── Mode 3 (BitCtl) ─────────────────────────────────────────────────────────

TEST(Z80PIO, Mode3_MixedDirection_ReadReturnsCorrectBits) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(3));        // mode 3
    pio.ioWrite(1, 0xF0);               // direction: upper 4 bits = input, lower 4 = output

    pio.ioWrite(0, 0xAA);               // CPU writes 0xAA to output latch
    pio.portAWrite(0x55);               // external drives 0x55 onto input pins

    // Expected: (0x55 & 0xF0) | (0xAA & 0x0F) = 0x50 | 0x0A = 0x5A
    EXPECT_EQ(pio.ioRead(0), 0x5A);
}

TEST(Z80PIO, Mode3_PortARead_ShowsOnlyOutputBits) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(3));
    pio.ioWrite(1, 0xF0);               // upper = input, lower = output

    pio.ioWrite(0, 0xAB);
    // portARead() reports what PIO drives: output_latch & ~direction = 0xAB & 0x0F = 0x0B
    EXPECT_EQ(pio.portARead(), 0x0B);
}

TEST(Z80PIO, Mode3_AllOutput_CallbackFires) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(3));
    pio.ioWrite(1, 0x00);               // all bits = output

    uint8_t received = 0;
    pio.setPortAOutputCallback([&](uint8_t d){ received = d; });

    pio.ioWrite(0, 0x7E);
    EXPECT_EQ(received, 0x7E);
    EXPECT_EQ(pio.portARead(), 0x7E);
}

// ─── Interrupt vector ─────────────────────────────────────────────────────────

TEST(Z80PIO, InterruptVector_SetAndRead) {
    Z80PIO pio;
    // Vector byte: bit0 must be 0. Use 0xAA (0b10101010, bit0=0).
    pio.ioWrite(1, 0xAA);
    EXPECT_EQ(pio.getInterruptVector(), 0xAA);
}

TEST(Z80PIO, InterruptVector_GetVector_WhenPending) {
    Z80PIO pio;
    pio.ioWrite(1, 0xC4);               // vector = 0xC4 (bit0=0)
    pio.ioWrite(1, modeWord(1));        // mode 1 (input)
    pio.ioWrite(1, icw(true));          // enable interrupt

    pio.setIEI(true);
    pio.portAWrite(0x01);               // triggers pending

    EXPECT_TRUE(pio.hasInterrupt());
    EXPECT_EQ(pio.getVector(), 0xC4);
}

TEST(Z80PIO, InterruptVector_NoInterrupt_ReturnsFF) {
    Z80PIO pio;
    pio.ioWrite(1, 0xC4);
    pio.setIEI(true);
    // No portAWrite called, no pending
    EXPECT_FALSE(pio.hasInterrupt());
    EXPECT_EQ(pio.getVector(), 0xFF);
}

// ─── IEI / IEO daisy chain ───────────────────────────────────────────────────

TEST(Z80PIO, DaisyChain_PortA_Pending_Blocks_PortB) {
    Z80PIO pio;
    // Set up port A: mode 1, IE enabled
    pio.ioWrite(1, modeWord(1));
    pio.ioWrite(1, icw(true));
    pio.ioWrite(1, 0x10);               // vector A = 0x10

    // Set up port B: mode 1, IE enabled
    pio.ioWrite(3, modeWord(1));
    pio.ioWrite(3, icw(true));
    pio.ioWrite(3, 0x20);               // vector B = 0x20

    pio.setIEI(true);

    pio.portAWrite(0x01);               // port A becomes pending

    EXPECT_TRUE(pio.hasInterrupt());
    EXPECT_FALSE(pio.getIEO());         // IEO blocked
    EXPECT_EQ(pio.getVector(), 0x10);  // port A wins
}

TEST(Z80PIO, DaisyChain_OnlyPortB_Pending) {
    Z80PIO pio;
    // Port A: mode 1, IE disabled (no pending)
    pio.ioWrite(1, modeWord(1));
    pio.ioWrite(1, 0x10);               // vector A = 0x10

    // Port B: mode 1, IE enabled
    pio.ioWrite(3, modeWord(1));
    pio.ioWrite(3, icw(true));
    pio.ioWrite(3, 0x20);               // vector B = 0x20

    pio.setIEI(true);

    pio.portBWrite(0x01);               // port B becomes pending

    EXPECT_TRUE(pio.hasInterrupt());
    EXPECT_FALSE(pio.getIEO());
    EXPECT_EQ(pio.getVector(), 0x20);  // port B vector
}

TEST(Z80PIO, DaisyChain_IEI_False_NoInterrupt) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));
    pio.ioWrite(1, icw(true));

    pio.setIEI(false);                  // upstream blocks us
    pio.portAWrite(0x42);

    EXPECT_FALSE(pio.hasInterrupt());   // IEI=false means we can't assert
    EXPECT_FALSE(pio.getIEO());         // IEO also false when IEI false
}

TEST(Z80PIO, DaisyChain_PassThrough_WhenNoPending) {
    Z80PIO pio;
    // No interrupts enabled, no pending
    pio.setIEI(true);
    EXPECT_FALSE(pio.hasInterrupt());
    EXPECT_TRUE(pio.getIEO());          // passes IEI through
}

// ─── Mode change via control word ─────────────────────────────────────────────

TEST(Z80PIO, ModeChange_Input_To_Output) {
    Z80PIO pio;
    // Default: mode 1 (input). External write visible to CPU.
    pio.portAWrite(0xBB);
    EXPECT_EQ(pio.ioRead(0), 0xBB);

    // Switch to output mode
    pio.ioWrite(1, modeWord(0));
    pio.ioWrite(0, 0x77);
    EXPECT_EQ(pio.ioRead(0), 0x77);
    EXPECT_EQ(pio.portARead(), 0x77);
}

TEST(Z80PIO, ModeChange_Output_To_Mode3) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));        // output
    pio.ioWrite(0, 0xFF);

    // Switch to mode 3: mode word followed by direction byte
    pio.ioWrite(1, modeWord(3));        // after this, next ctrl byte = direction
    pio.ioWrite(1, 0x0F);               // lower 4 bits = input, upper 4 = output

    pio.ioWrite(0, 0xCC);               // output latch
    pio.portAWrite(0x33);              // input latch

    // (0x33 & 0x0F) | (0xCC & 0xF0) = 0x03 | 0xC0 = 0xC3
    EXPECT_EQ(pio.ioRead(0), 0xC3);
}

TEST(Z80PIO, ModeChange_ResetsInterruptPending) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));
    pio.ioWrite(1, icw(true));
    pio.setIEI(true);
    pio.portAWrite(0x01);
    EXPECT_TRUE(pio.hasInterrupt());

    // Switching mode does NOT automatically clear pending; that's by design.
    // Verify mode change still takes effect for data path.
    pio.ioWrite(1, modeWord(0));        // switch to output
    pio.ioWrite(0, 0xAB);
    EXPECT_EQ(pio.portARead(), 0xAB);
}

// ─── isInterruptEnabled ───────────────────────────────────────────────────────

TEST(Z80PIO, IsInterruptEnabled_Default_False) {
    Z80PIO pio;
    EXPECT_FALSE(pio.isInterruptEnabled());
}

TEST(Z80PIO, IsInterruptEnabled_AfterICW) {
    Z80PIO pio;
    pio.ioWrite(1, icw(true));
    EXPECT_TRUE(pio.isInterruptEnabled());
}
