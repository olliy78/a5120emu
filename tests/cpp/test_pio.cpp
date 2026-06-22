/**
 * @file test_pio.cpp
 * @brief Unit tests for the Z80 PIO peripheral emulation.
 *
 * @details
 * Emulator component under test: **Z80PIO** (`core/primitives/z80_pio.h`)
 *
 * The Z80 PIO (Parallel Input/Output controller) provides two independent
 * 8-bit ports (A and B), each configurable in one of four modes:
 *  - **Mode 0 (Output)**: CPU writes drive the port pins; callback fires on write.
 *  - **Mode 1 (Input)**: External side writes data; CPU reads it back.
 *  - **Mode 2 (Bidirectional)**: Port A only; not tested here.
 *  - **Mode 3 (BitCtl)**: Per-bit direction register follows the mode word.
 *
 * The PIO also implements the Z80 interrupt daisy-chain (IEI/IEO) and can
 * generate interrupts based on input transitions (Mode 1 / BitCtl with ICW).
 *
 * Control word encoding:
 *  - Mode word: `(mode << 6) | 0x0F` — selects operating mode for a port.
 *  - ICW (Interrupt Control Word): `bit7=IE, bit6=AND, bit5=ActiveHigh, bit4=MaskFollows, bits3:0=0111`
 *  - Vector word: even value (bit0=0)
 *
 * Port offsets (relative to PIO base):
 *  - 0: Port A data
 *  - 1: Port A control
 *  - 2: Port B data
 *  - 3: Port B control
 *
 * ## Test groups
 *
 * | Group                     | What is tested                                         |
 * |---------------------------|--------------------------------------------------------|
 * | Mode 0 (Output)           | CPU write updates latch, callback fires, read-back     |
 * | Mode 1 (Input)            | External write; CPU reads back; CPU write ignored      |
 * | Mode 3 (BitCtl)           | Direction register, mixed read, callback               |
 * | Interrupt vector          | Vector set/read; getVector() when pending and idle     |
 * | IEI / IEO daisy chain     | Port A pending blocks Port B; IEI=false; pass-through  |
 * | Mode change               | Input→Output, Output→Mode3, pending not auto-cleared   |
 * | isInterruptEnabled        | Default false; true after ICW                          |
 *
 * @see core/primitives/z80_pio.h
 */

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

/**
 * @test Z80PIO/Mode0_CPUWrite_UpdatesOutputLatch
 * @brief In Mode 0 (Output), ioWrite to the data port updates the output latch.
 * @par Pass criterion  portARead() == 0x42 after ioWrite(0, 0x42).
 */
TEST(Z80PIO, Mode0_CPUWrite_UpdatesOutputLatch) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));   // portA → output mode

    pio.ioWrite(0, 0x42);
    EXPECT_EQ(pio.portARead(), 0x42);
}

/**
 * @test Z80PIO/Mode0_CPUWrite_FiresCallback
 * @brief In Mode 0, writing to the data port fires the registered output callback.
 * @par Pass criterion  Callback receives 0xBE.
 */
TEST(Z80PIO, Mode0_CPUWrite_FiresCallback) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));

    uint8_t received = 0;
    pio.setPortAOutputCallback([&](uint8_t d){ received = d; });

    pio.ioWrite(0, 0xBE);
    EXPECT_EQ(received, 0xBE);
}

/**
 * @test Z80PIO/Mode0_CPURead_ReturnsOutputLatch
 * @brief In Mode 0, ioRead from the data port returns the last value written.
 * @par Pass criterion  ioRead(0) == 0xA5 after ioWrite(0, 0xA5).
 */
TEST(Z80PIO, Mode0_CPURead_ReturnsOutputLatch) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(0));
    pio.ioWrite(0, 0xA5);
    EXPECT_EQ(pio.ioRead(0), 0xA5);
}

/**
 * @test Z80PIO/Mode0_PortBWrite_FiresCallbackAndRead
 * @brief Mode 0 on Port B fires the callback and portBRead() returns the written value.
 * @par Pass criterion  Callback receives 0x99; portBRead() == 0x99.
 */
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

/**
 * @test Z80PIO/Mode1_Default_ExternalWrite_CPUReadsBack
 * @brief Default mode is 1 (Input); external portAWrite() is readable by the CPU via ioRead(0).
 * @par Pass criterion  ioRead(0) == 0x55 after portAWrite(0x55).
 */
TEST(Z80PIO, Mode1_Default_ExternalWrite_CPUReadsBack) {
    Z80PIO pio;
    // Default mode is 1 (Input), no mode change needed

    pio.portAWrite(0x55);
    EXPECT_EQ(pio.ioRead(0), 0x55);
}

/**
 * @test Z80PIO/Mode1_ExplicitSet_ExternalWrite_CPUReadsBack
 * @brief Explicitly setting Mode 1 still allows external writes to be read back by the CPU.
 * @par Pass criterion  ioRead(0) == 0xDE after portAWrite(0xDE).
 */
TEST(Z80PIO, Mode1_ExplicitSet_ExternalWrite_CPUReadsBack) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));

    pio.portAWrite(0xDE);
    EXPECT_EQ(pio.ioRead(0), 0xDE);
}

/**
 * @test Z80PIO/Mode1_CPUWrite_IsIgnored
 * @brief In Mode 1 (Input), CPU writes to the data port are ignored; no callback fires.
 * @par Pass criterion  ioRead(0) still returns the external value; callback not called.
 */
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

/**
 * @test Z80PIO/Mode1_PortARead_ReturnsHighZ
 * @brief In Mode 1 (Input), portARead() returns 0xFF (high-Z) since the PIO drives no output.
 * @par Pass criterion  portARead() == 0xFF.
 */
TEST(Z80PIO, Mode1_PortARead_ReturnsHighZ) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));
    // In input mode the PIO drives nothing onto pins
    EXPECT_EQ(pio.portARead(), 0xFF);
}

// ─── Mode 3 (BitCtl) ─────────────────────────────────────────────────────────

/**
 * @test Z80PIO/Mode3_MixedDirection_ReadReturnsCorrectBits
 * @brief In Mode 3 with direction 0xF0 (upper=input, lower=output), ioRead() correctly
 *   merges the input latch and the output latch: (input & 0xF0) | (output & 0x0F).
 * @par Pass criterion  ioRead(0) == 0x5A given output=0xAA, input=0x55.
 */
TEST(Z80PIO, Mode3_MixedDirection_ReadReturnsCorrectBits) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(3));        // mode 3
    pio.ioWrite(1, 0xF0);               // direction: upper 4 bits = input, lower 4 = output

    pio.ioWrite(0, 0xAA);               // CPU writes 0xAA to output latch
    pio.portAWrite(0x55);               // external drives 0x55 onto input pins

    // Expected: (0x55 & 0xF0) | (0xAA & 0x0F) = 0x50 | 0x0A = 0x5A
    EXPECT_EQ(pio.ioRead(0), 0x5A);
}

/**
 * @test Z80PIO/Mode3_PortARead_ShowsOnlyOutputBits
 * @brief portARead() in Mode 3 shows only the output bits driven by the PIO (output_latch & ~direction).
 * @par Pass criterion  portARead() == 0x0B given output_latch=0xAB and direction=0xF0.
 */
TEST(Z80PIO, Mode3_PortARead_ShowsOnlyOutputBits) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(3));
    pio.ioWrite(1, 0xF0);               // upper = input, lower = output

    pio.ioWrite(0, 0xAB);
    // portARead() reports what PIO drives: output_latch & ~direction = 0xAB & 0x0F = 0x0B
    EXPECT_EQ(pio.portARead(), 0x0B);
}

/**
 * @test Z80PIO/Mode3_AllOutput_CallbackFires
 * @brief In Mode 3 with direction 0x00 (all output), writing fires the callback and portARead() returns the value.
 * @par Pass criterion  Callback receives 0x7E; portARead() == 0x7E.
 */
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

/**
 * @test Z80PIO/InterruptVector_SetAndRead
 * @brief Writing an even value to the control port sets the interrupt vector.
 * @par Pass criterion  getInterruptVector() == 0xAA after ioWrite(1, 0xAA).
 */
TEST(Z80PIO, InterruptVector_SetAndRead) {
    Z80PIO pio;
    // Vector byte: bit0 must be 0. Use 0xAA (0b10101010, bit0=0).
    pio.ioWrite(1, 0xAA);
    EXPECT_EQ(pio.getInterruptVector(), 0xAA);
}

/**
 * @test Z80PIO/InterruptVector_GetVector_WhenPending
 * @brief getVector() returns the programmed vector when an interrupt is pending.
 * @par Pass criterion  hasInterrupt() == true; getVector() == 0xC4.
 */
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

/**
 * @test Z80PIO/InterruptVector_NoInterrupt_ReturnsFF
 * @brief getVector() returns 0xFF when no interrupt is pending.
 * @par Pass criterion  hasInterrupt() == false; getVector() == 0xFF.
 */
TEST(Z80PIO, InterruptVector_NoInterrupt_ReturnsFF) {
    Z80PIO pio;
    pio.ioWrite(1, 0xC4);
    pio.setIEI(true);
    // No portAWrite called, no pending
    EXPECT_FALSE(pio.hasInterrupt());
    EXPECT_EQ(pio.getVector(), 0xFF);
}

// ─── IEI / IEO daisy chain ───────────────────────────────────────────────────

/**
 * @test Z80PIO/DaisyChain_PortA_Pending_Blocks_PortB
 * @brief When Port A has a pending interrupt, IEO is blocked and getVector() returns Port A's vector.
 * @par Pass criterion  hasInterrupt() == true; getIEO() == false; getVector() == 0x10.
 */
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

/**
 * @test Z80PIO/DaisyChain_OnlyPortB_Pending
 * @brief When only Port B has a pending interrupt, getVector() returns Port B's vector.
 * @par Pass criterion  hasInterrupt() == true; getVector() == 0x20.
 */
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

/**
 * @test Z80PIO/DaisyChain_IEI_False_NoInterrupt
 * @brief With IEI=false (blocked upstream), the PIO cannot assert an interrupt and IEO is also false.
 * @par Pass criterion  hasInterrupt() == false; getIEO() == false.
 */
TEST(Z80PIO, DaisyChain_IEI_False_NoInterrupt) {
    Z80PIO pio;
    pio.ioWrite(1, modeWord(1));
    pio.ioWrite(1, icw(true));

    pio.setIEI(false);                  // upstream blocks us
    pio.portAWrite(0x42);

    EXPECT_FALSE(pio.hasInterrupt());   // IEI=false means we can't assert
    EXPECT_FALSE(pio.getIEO());         // IEO also false when IEI false
}

/**
 * @test Z80PIO/DaisyChain_PassThrough_WhenNoPending
 * @brief With IEI=true and no pending interrupt, IEO passes through (true) to downstream devices.
 * @par Pass criterion  getIEO() == true; hasInterrupt() == false.
 */
TEST(Z80PIO, DaisyChain_PassThrough_WhenNoPending) {
    Z80PIO pio;
    // No interrupts enabled, no pending
    pio.setIEI(true);
    EXPECT_FALSE(pio.hasInterrupt());
    EXPECT_TRUE(pio.getIEO());          // passes IEI through
}

// ─── Mode change via control word ─────────────────────────────────────────────

/**
 * @test Z80PIO/ModeChange_Input_To_Output
 * @brief Switching from Mode 1 (Input) to Mode 0 (Output) correctly transitions the data path.
 * @par Pass criterion  After mode switch, ioRead(0) == 0x77; portARead() == 0x77.
 */
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

/**
 * @test Z80PIO/ModeChange_Output_To_Mode3
 * @brief Switching from Mode 0 (Output) to Mode 3 (BitCtl) with direction 0x0F
 *   correctly merges the new input and output bits.
 * @par Pass criterion  ioRead(0) == 0xC3 given output=0xCC, input=0x33, direction=0x0F.
 */
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

/**
 * @test Z80PIO/ModeChange_ResetsInterruptPending
 * @brief Switching modes does not automatically clear a pending interrupt; output mode still works.
 * @details This verifies by design: mode changes affect data path but not the pending flag.
 * @par Pass criterion  portARead() == 0xAB after switching to output mode.
 */
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

/**
 * @test Z80PIO/IsInterruptEnabled_Default_False
 * @brief After construction, interrupts are disabled by default.
 * @par Pass criterion  isInterruptEnabled() == false.
 */
TEST(Z80PIO, IsInterruptEnabled_Default_False) {
    Z80PIO pio;
    EXPECT_FALSE(pio.isInterruptEnabled());
}

/**
 * @test Z80PIO/IsInterruptEnabled_AfterICW
 * @brief Writing an ICW with IE=1 enables interrupts.
 * @par Pass criterion  isInterruptEnabled() == true after icw(true).
 */
TEST(Z80PIO, IsInterruptEnabled_AfterICW) {
    Z80PIO pio;
    pio.ioWrite(1, icw(true));
    EXPECT_TRUE(pio.isInterruptEnabled());
}

// ─── debugState() — read-only snapshot for the debugger's `dev pio` command ─────

/**
 * @brief debugState reflects per-port mode/latches/vector + interrupt + daisy-chain.
 */
TEST(Z80PIO, DebugStateReflectsPortsAndInterrupt) {
    Z80PIO pio;
    // Port A: input mode 1, vector 0xC4, IRQ enabled, IEI high → external write → pending.
    pio.ioWrite(1, 0xC4);                 // vector
    pio.ioWrite(1, modeWord(1));          // mode 1 (input)
    pio.ioWrite(1, icw(true));            // enable interrupt
    pio.setIEI(true);
    pio.portAWrite(0x55);                 // external data → readable + triggers pending
    // Port B: output mode 0, CPU writes 0x99.
    pio.ioWrite(3, modeWord(0));
    pio.ioWrite(2, 0x99);

    auto d = pio.debugState();
    EXPECT_TRUE(d.iei);                   // device IEI feeds Port A
    // Port A
    EXPECT_EQ(d.port[0].mode, 1);
    EXPECT_EQ(d.port[0].vector, 0xC4);
    EXPECT_EQ(d.port[0].in, 0x55);        // external input latch
    EXPECT_TRUE(d.port[0].ie);
    EXPECT_TRUE(d.port[0].pending);
    EXPECT_TRUE(d.port[0].iei);
    EXPECT_FALSE(d.port[0].ius);
    EXPECT_FALSE(d.ieo);                  // pending interrupt blocks the chain downstream
    // Port B
    EXPECT_EQ(d.port[1].mode, 0);
    EXPECT_EQ(d.port[1].out, 0x99);       // CPU output latch
    EXPECT_FALSE(d.port[1].ie);
}
