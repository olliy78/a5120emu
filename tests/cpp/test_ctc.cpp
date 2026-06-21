/**
 * @file test_ctc.cpp
 * @brief Unit tests for the Z80 CTC (Counter/Timer Circuit) emulation.
 *
 * This test suite verifies the correctness of the Z80CTC emulation against
 * the U857D CTC technical specification used in the Robotron A5120.
 *
 * ## Control Word Bit Reference (U857D manual, Bild 3)
 *
 * | Bit | Mask | Name             | 0 = ...          | 1 = ...           |
 * |-----|------|------------------|------------------|-------------------|
 * | D7  | 0x80 | CTRL_INT_EN      | IRQ disabled     | IRQ enabled       |
 * | D6  | 0x40 | CTRL_COUNTER     | Timer mode       | **Counter mode**  |
 * | D5  | 0x20 | CTRL_PRESCALE256 | Prescaler ÷16    | Prescaler ÷256    |
 * | D4  | 0x10 | CTRL_EDGE_RISE   | Falling edge     | Rising edge       |
 * | D3  | 0x08 | CTRL_CLK_TRG     | Timer auto-start | CLK/TRG trigger   |
 * | D2  | 0x04 | CTRL_TC_FOLLOWS  | No TC write      | TC write follows  |
 * | D1  | 0x02 | CTRL_RESET       | No reset         | Software reset    |
 * | D0  | 0x01 | (control flag)   | Vector write     | Control word      |
 *
 * ## Common Control Byte Quick-Reference
 *
 * | Value | Meaning                                          |
 * |-------|--------------------------------------------------|
 * | 0x05  | Timer, ÷16, TC follows                           |
 * | 0x25  | Timer, ÷256, TC follows                          |
 * | 0x85  | Timer, ÷16, IRQ enabled, TC follows              |
 * | 0x1D  | Timer, ÷16, CLK/TRG trigger, rising edge, TC follows |
 * | 0x45  | Counter, falling edge, TC follows                |
 * | 0x55  | Counter, rising edge, TC follows                 |
 *
 * ## Test Organisation
 *
 * - Timer mode: basic ZC/TO timing, auto-reload, pulse shape, prescaler
 * - Interrupt: generation and IEI masking
 * - Counter mode: edge counting, edge polarity
 * - ZCTOCallback: channel identification, early-call guard
 * - Interrupt vector: calculation and idle state
 * - Daisy-chain (IEI/IEO): priority blocking and passthrough
 * - I/O read: counter readback and TC=256 wrapping
 * - Helper getters: getCount, isTimerMode
 * - Control word reset: counter clear, timer stop, interrupt clear
 * - Multi-channel independence
 * - CLK/TRG timer trigger
 *
 * @see core/primitives/z80_ctc.h
 * @see doc/trascripted/CTC_U875D.md
 * @see Robotron A5120 U857D Technical Manual
 */

#include <gtest/gtest.h>
#include "core/primitives/z80_ctc.h"

// =============================================================================
// Helpers
// =============================================================================

/**
 * @brief Write a control word followed immediately by a time constant to one channel.
 *
 * This mirrors the two-step programming sequence required by the U857D:
 *  1. Write a control word with D0=1 and D2=1 (TC_FOLLOWS).
 *  2. Write the time constant (any 8-bit value; 0 is interpreted as 256).
 *
 * @param ctc  CTC instance to program.
 * @param ch   Channel index (0–3).
 * @param ctrl Control byte — must have D0=1 (control word) and D2=1 (TC follows).
 * @param tc   Time constant (1–255 = literal; 0 = 256).
 */
static void configChannel(Z80CTC& ctc, int ch, uint8_t ctrl, uint8_t tc) {
    ctc.ioWrite(static_cast<uint8_t>(ch), ctrl);
    ctc.ioWrite(static_cast<uint8_t>(ch), tc);
}

// =============================================================================
// Timer mode — basic timing
// =============================================================================

/**
 * @brief Timer fires ZC/TO after exactly (prescaler × TC) system clock ticks.
 *
 * Configuration: Timer mode, prescaler ÷16 (D5=0), TC=10, no IRQ.
 *   Control byte: 0x05  (D2=1 TC_FOLLOWS | D0=1 ctrl flag)
 *
 * Expected: ZC/TO pulse (falling edge callback) fires on tick 160 = 16 × 10.
 * - After 159 ticks: no pulse.
 * - After tick 160:  exactly one pulse.
 */
TEST(Z80CTC, TimerMode_ZCTOFiresAfter160Ticks) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int /*ch*/, bool level) {
        if (!level) ++zcto_count;  // count falling edges (pulse start) only
    });

    // Timer mode, prescaler ÷16, TC follows — control byte = 0x05
    configChannel(ctc, 0, 0x05, 10);

    for (int i = 0; i < 159; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 0);

    ctc.clockTick();  // tick 160 — zero crossing
    EXPECT_EQ(zcto_count, 1);
}

/**
 * @brief Timer automatically reloads the time constant after each ZC/TO.
 *
 * Configuration: Timer mode, prescaler ÷16, TC=10.
 *   Control byte: 0x05
 *
 * The hardware reloads the counter from the time-constant register on every
 * zero crossing, so the period repeats indefinitely.
 *
 * Expected: after 320 ticks (2 × 160) exactly 2 ZC/TO pulses have fired.
 */
TEST(Z80CTC, TimerMode_AutoReload) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++zcto_count; });

    configChannel(ctc, 0, 0x05, 10);  // prescaler ÷16, TC=10

    for (int i = 0; i < 320; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 2);
}

/**
 * @brief ZC/TO pulse is exactly one clock cycle wide.
 *
 * The U857D generates a one-clock-cycle low pulse on ZC/TO at zero crossing.
 * The implementation models this with a callback:
 *   - callback(ch, false): pulse starts (falling edge) at zero crossing tick.
 *   - callback(ch, true):  pulse ends  (rising edge) on the *next* tick.
 *
 * Configuration: Timer mode, prescaler ÷16, TC=1.
 *   Control byte: 0x05 — fires every 16 ticks.
 *
 * Expected:
 *   - event[0] = (channel=0, level=false) at tick 16.
 *   - event[1] = (channel=0, level=true)  at tick 17.
 */
TEST(Z80CTC, TimerMode_ZCTOPulseHighAfterOneTick) {
    Z80CTC ctc;
    std::vector<std::pair<int,bool>> events;
    ctc.setZCTOCallback([&](int ch, bool level) { events.push_back({ch, level}); });

    configChannel(ctc, 0, 0x05, 1);  // prescaler ÷16, TC=1 → fires every 16 ticks

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    ASSERT_GE(static_cast<int>(events.size()), 1u);
    EXPECT_EQ(events[0].first, 0);      // channel 0
    EXPECT_FALSE(events[0].second);     // falling edge (pulse start)

    ctc.clockTick();  // tick 17 — pulse end
    ASSERT_GE(static_cast<int>(events.size()), 2u);
    EXPECT_TRUE(events[1].second);      // rising edge (pulse end)
}

// =============================================================================
// Timer mode — prescaler ÷256
// =============================================================================

/**
 * @brief Prescaler ÷256 divides the system clock by 256 before the counter.
 *
 * Configuration: Timer mode, prescaler ÷256 (D5=1), TC=2, no IRQ.
 *   Control byte: 0x25  (D5=1 PRESCALE256 | D2=1 TC_FOLLOWS | D0=1)
 *   Total period: 2 × 256 = 512 system clock ticks.
 *
 * Expected:
 *   - After 511 ticks: no ZC/TO pulse.
 *   - After tick 512:  exactly one pulse.
 */
TEST(Z80CTC, TimerMode_Prescaler256) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++zcto_count; });

    // Timer mode, prescaler ÷256 (D5=1), TC follows — control byte = 0x25
    configChannel(ctc, 0, 0x25, 2);  // 2 × 256 = 512 ticks

    for (int i = 0; i < 511; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 0);
    ctc.clockTick();
    EXPECT_EQ(zcto_count, 1);
}

// =============================================================================
// Timer mode — interrupt generation
// =============================================================================

/**
 * @brief Timer generates an interrupt when the counter reaches zero.
 *
 * Prerequisites:
 *   - IRQ must be enabled in the control word (D7=1).
 *   - IEI must be asserted (setIEI(true)).
 *
 * Configuration: Timer mode, prescaler ÷16, IRQ enabled, TC=5.
 *   Control byte: 0x85  (D7=1 INT_EN | D2=1 TC_FOLLOWS | D0=1)
 *   Interrupt vector base set to 0x20 via vector write (D0=0) to channel 0.
 *   Period: 5 × 16 = 80 ticks.
 *
 * Expected:
 *   - hasInterrupt() is false before the zero crossing.
 *   - hasInterrupt() is true after tick 80.
 */
TEST(Z80CTC, TimerMode_InterruptGenerated) {
    Z80CTC ctc;
    // Program interrupt vector base: write to ch0 with D0=0 (vector write, not control)
    ctc.ioWrite(0, 0x20);  // vec_base = 0x20

    // Timer mode, prescaler ÷16, IRQ enabled — control byte = 0x85
    configChannel(ctc, 0, 0x85, 5);  // period: 5 × 16 = 80 ticks

    ctc.setIEI(true);
    EXPECT_FALSE(ctc.hasInterrupt());

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_TRUE(ctc.hasInterrupt());
}

/**
 * @brief Interrupt is suppressed when IEI is de-asserted.
 *
 * Even if the channel fires a ZC/TO and has IRQ enabled, hasInterrupt()
 * must return false whenever the upstream IEI signal is low.
 *
 * Configuration: Timer mode, prescaler ÷16, IRQ enabled, TC=1.
 *   Control byte: 0x85 — fires after 16 ticks.
 *
 * Expected: hasInterrupt() stays false after fire, because IEI=false.
 */
TEST(Z80CTC, TimerMode_NoInterruptWhenIEILow) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 1);  // IRQ enabled, TC=1, period = 16 ticks
    ctc.setIEI(false);
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_FALSE(ctc.hasInterrupt());
}

// =============================================================================
// Counter mode
// =============================================================================

/**
 * @brief Counter mode fires ZC/TO after exactly TC matching CLK/TRG edges.
 *
 * In Counter mode (D6=1) the channel decrements on each qualified external
 * pulse edge rather than on system clock ticks.  Edge polarity is selected
 * by D4: 0 = falling, 1 = rising.
 *
 * Configuration: Counter mode (D6=1), rising edge (D4=1), TC=5.
 *   Control byte: 0x55  (D6=1 COUNTER | D4=1 EDGE_RISE | D2=1 TC_FOLLOWS | D0=1)
 *
 * Expected:
 *   - After 4 rising edges: no ZC/TO.
 *   - After 5th rising edge: ZC/TO fires once.
 */
TEST(Z80CTC, CounterMode_ZCTOAfterNPulses) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // Counter mode, rising edge, TC follows — control byte = 0x55
    configChannel(ctc, 1, 0x55, 5);

    for (int i = 0; i < 4; ++i) ctc.clkTrg(1, true);
    EXPECT_EQ(fired, 0);

    ctc.clkTrg(1, true);  // 5th rising edge → zero crossing
    EXPECT_EQ(fired, 1);
}

/**
 * @brief Counter mode ignores edges of the wrong polarity.
 *
 * When rising edge is selected (D4=1), falling edges must be completely
 * ignored; only rising edges advance the counter.
 *
 * Configuration: Counter mode (D6=1), rising edge (D4=1), TC=3.
 *   Control byte: 0x55
 *
 * Expected:
 *   - 3 falling edges: counter unchanged, no ZC/TO.
 *   - 3 rising edges: counter reaches zero, ZC/TO fires once.
 */
TEST(Z80CTC, CounterMode_FallingEdgeIgnoredWhenRisingSelected) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // Counter mode, rising edge selected — control byte = 0x55
    configChannel(ctc, 0, 0x55, 3);

    ctc.clkTrg(0, false);  // falling edge: ignored
    ctc.clkTrg(0, false);
    ctc.clkTrg(0, false);
    EXPECT_EQ(fired, 0);

    ctc.clkTrg(0, true);   // rising edges: counted
    ctc.clkTrg(0, true);
    ctc.clkTrg(0, true);   // 3rd → zero crossing
    EXPECT_EQ(fired, 1);
}

/**
 * @brief Counter mode can be configured to count falling (negative) edges.
 *
 * When D4=0, falling edges are the active counting edge; rising edges are
 * ignored.
 *
 * Configuration: Counter mode (D6=1), falling edge (D4=0), TC=3.
 *   Control byte: 0x45  (D6=1 COUNTER | D2=1 TC_FOLLOWS | D0=1)
 *
 * Expected: after exactly 3 falling edges the ZC/TO fires once.
 */
TEST(Z80CTC, CounterMode_FallingEdge) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // Counter mode, falling edge (D4=0), TC follows — control byte = 0x45
    configChannel(ctc, 2, 0x45, 3);

    ctc.clkTrg(2, false);
    ctc.clkTrg(2, false);
    ctc.clkTrg(2, false);  // 3rd → zero crossing
    EXPECT_EQ(fired, 1);
}

// =============================================================================
// ZCTOCallback details
// =============================================================================

/**
 * @brief ZC/TO callback receives the correct channel index.
 *
 * When multiple channels are present the callback must identify which channel
 * fired so the caller can route the pulse to the right destination (e.g. baud
 * rate clock for SIO).
 *
 * Configuration: Timer mode, prescaler ÷16, TC=1 on **channel 2**.
 *   Control byte: 0x05
 *
 * Expected: callback is invoked with channel index 2.
 */
TEST(Z80CTC, ZCTOCallback_ReceivesCorrectChannel) {
    Z80CTC ctc;
    int cb_channel = -1;
    ctc.setZCTOCallback([&](int ch, bool level) {
        if (!level) cb_channel = ch;  // record channel on falling edge
    });

    configChannel(ctc, 2, 0x05, 1);  // channel 2, prescaler ÷16, TC=1
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_EQ(cb_channel, 2);
}

/**
 * @brief ZC/TO callback is not invoked before the zero crossing.
 *
 * The callback must remain silent during the entire run-up to zero.
 * Spurious callbacks before the expected tick indicate an off-by-one error.
 *
 * Configuration: Timer mode, prescaler ÷16, TC=10.
 *   Control byte: 0x05 — fires at tick 160.
 *
 * Expected: callback not called after 159 ticks.
 */
TEST(Z80CTC, ZCTOCallback_NotCalledBeforeFire) {
    Z80CTC ctc;
    bool called = false;
    ctc.setZCTOCallback([&](int, bool) { called = true; });

    configChannel(ctc, 0, 0x05, 10);
    for (int i = 0; i < 159; ++i) ctc.clockTick();
    EXPECT_FALSE(called);
}

// =============================================================================
// Interrupt vector calculation
// =============================================================================

/**
 * @brief Interrupt vector encodes the channel number in bits D2-D1.
 *
 * The U857D produces vectors of the form: vec_base | (channel << 1).
 * The vector base occupies bits D7-D3 and is programmed by writing to any
 * channel port with D0=0 (vector write); channel 0 is conventionally used.
 *
 * Scenario:
 *   - Vector base set to 0x20 (write 0x20 to ch0 with D0=0).
 *   - All 4 channels configured with IRQ enabled and TC=1 (period = 16 ticks).
 *   - After 16 ticks all channels fire simultaneously.
 *   - getVector() returns the highest-priority pending channel each call.
 *   - The acknowledged channel's int_pending is cleared; the next call returns
 *     the next-priority channel.
 *
 * Expected vectors: ch0→0x20, ch1→0x22, ch2→0x24, ch3→0x26.
 */
TEST(Z80CTC, InterruptVector_Calculation) {
    Z80CTC ctc;
    ctc.setIEI(true);

    // Program vector base 0x20 — D0=0 marks this as a vector write (not control)
    ctc.ioWrite(0, 0x20);

    // Configure all 4 channels: Timer mode, ÷16, IRQ enabled, TC=1
    for (int i = 0; i < 4; ++i) {
        configChannel(ctc, i, 0x85, 1);  // control = 0x85, TC = 1
    }

    // Tick 16 times: all channels reach zero and assert int_pending
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    // Channel 0 has the highest priority; its vector = 0x20 | (0 << 1) = 0x20
    EXPECT_EQ(ctc.getVector(), 0x20);

    // Reset ch0 so its pending is cleared; ch1 becomes the next winner
    ctc.ioWrite(0, 0x03);  // software reset ch0
    EXPECT_EQ(ctc.getVector(), 0x22);  // ch1: 0x20 | (1 << 1)

    ctc.ioWrite(1, 0x03);  // reset ch1
    EXPECT_EQ(ctc.getVector(), 0x24);  // ch2: 0x20 | (2 << 1)

    ctc.ioWrite(2, 0x03);  // reset ch2
    EXPECT_EQ(ctc.getVector(), 0x26);  // ch3: 0x20 | (3 << 1)
}

/**
 * @brief getVector() returns 0xFF when no channel has a pending interrupt.
 *
 * 0xFF is the sentinel value indicating no interrupt is pending — it must
 * never be a valid CTC vector, since all legal vectors have D0=0.
 */
TEST(Z80CTC, InterruptVector_NoInterruptReturns0xFF) {
    Z80CTC ctc;
    ctc.setIEI(true);
    EXPECT_EQ(ctc.getVector(), 0xFF);
}

// =============================================================================
// IEI / IEO daisy-chain
// =============================================================================

/**
 * @brief IEO goes low when any channel has a pending interrupt.
 *
 * In the Z80 daisy-chain, a device must pull IEO low to prevent lower-priority
 * devices from stealing the interrupt acknowledge cycle.
 *
 * Scenario: ch0 fires with IRQ enabled and IEI=true.
 *
 * Expected: hasInterrupt()=true and getIEO()=false after zero crossing.
 */
TEST(Z80CTC, DaisyChain_IEOLowWhenPending) {
    Z80CTC ctc;
    ctc.ioWrite(0, 0x20);  // vector base = 0x20

    // ch0: Timer mode, ÷16, IRQ enabled, TC=1 → fires after 16 ticks
    configChannel(ctc, 0, 0x85, 1);
    ctc.setIEI(true);

    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_TRUE(ctc.hasInterrupt());   // CTC is requesting an interrupt
    EXPECT_FALSE(ctc.getIEO());        // downstream devices are blocked
}

/**
 * @brief IEO passes IEI through when no channel is pending.
 *
 * When the CTC has no interrupt pending it must transparently pass IEI to its
 * IEO output, allowing downstream devices to interrupt.
 *
 * Scenario: ch0 configured but not yet fired (TC=10, 0 ticks elapsed).
 *
 * Expected: hasInterrupt()=false and getIEO()=true.
 */
TEST(Z80CTC, DaisyChain_IEOHighWhenNoPending) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 10);  // IRQ enabled, TC=10
    ctc.setIEI(true);

    EXPECT_FALSE(ctc.hasInterrupt());
    EXPECT_TRUE(ctc.getIEO());
}

/**
 * @brief Channel 0 interrupt takes priority over channel 1.
 *
 * When both ch0 and ch1 have pending interrupts simultaneously, getVector()
 * must return ch0's vector because it occupies the highest-priority slot in
 * the internal IEI/IEO chain (ch0 > ch1 > ch2 > ch3).
 *
 * Scenario: both ch0 and ch1 fired at the same tick with IRQ enabled.
 *
 * Expected: getVector() returns ch0's vector 0x20, not ch1's 0x22.
 */
TEST(Z80CTC, DaisyChain_Ch0BlocksCh1) {
    Z80CTC ctc;
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);  // vector base = 0x20

    // Both channels: Timer mode, ÷16, IRQ enabled, TC=1 → fire at tick 16
    configChannel(ctc, 0, 0x85, 1);
    configChannel(ctc, 1, 0x85, 1);

    for (int i = 0; i < 16; ++i) ctc.clockTick();

    // Both channels pending; ch0 must win
    EXPECT_EQ(ctc.getVector(), 0x20);  // ch0 vector, not ch1 (0x22)
}

/**
 * @brief IEI=false blocks all interrupt requests and forces IEO=false.
 *
 * When the upstream device de-asserts IEI, no CTC channel may request an
 * interrupt (hasInterrupt()=false) and IEO is also pulled low (IEO follows IEI
 * when the CTC cannot grant interrupts).
 *
 * Scenario: ch0 configured with IRQ enabled, fires after 16 ticks, IEI=false.
 *
 * Expected: hasInterrupt()=false, getIEO()=false.
 */
TEST(Z80CTC, DaisyChain_IEILowBlocksAll) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 1);  // IRQ enabled, TC=1
    ctc.setIEI(false);
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_FALSE(ctc.hasInterrupt());
    EXPECT_FALSE(ctc.getIEO());
}

// =============================================================================
// ioRead — counter readback
// =============================================================================

/**
 * @brief ioRead() returns the live down-counter value of the channel.
 *
 * The CPU can read the current counter state at any time without interrupting
 * the count.  Each prescaler overflow decrements the counter by 1.
 *
 * Configuration: Timer mode, prescaler ÷16, TC=10.
 *   Control byte: 0x05
 *   Counter starts at 10 and decrements by 1 every 16 ticks.
 *
 * Expected:
 *   - At tick 0 (just after programming): ioRead = 10.
 *   - After 16 ticks: ioRead = 9.
 *   - After 32 ticks: ioRead = 8.
 */
TEST(Z80CTC, IoRead_ReturnsCurrentCount) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);  // Timer, ÷16, TC=10

    EXPECT_EQ(ctc.ioRead(0), 10);

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 9);

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 8);
}

/**
 * @brief TC=0 means 256; ioRead wraps to 0 because the return type is uint8_t.
 *
 * The U857D interprets a programmed time constant of 0 as 256 (full 8-bit
 * range).  Internally the counter stores 256, but ioRead casts it to uint8_t,
 * which returns 0 (256 mod 256).  getCount() returns the raw value (256).
 *
 * Configuration: Timer mode, prescaler ÷16, TC=0 (→ 256).
 *   Control byte: 0x05
 *
 * Expected: ioRead(0)=0, getCount(0)=256.
 */
TEST(Z80CTC, IoRead_WrapsToZeroForTC256) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 0);  // TC=0 is interpreted as 256

    EXPECT_EQ(ctc.ioRead(0), 0);      // uint8_t: 256 & 0xFF = 0
    EXPECT_EQ(ctc.getCount(0), 256);  // raw value
}

// =============================================================================
// getCount / isTimerMode helpers
// =============================================================================

/**
 * @brief getCount() reflects the decrementing counter in real time.
 *
 * getCount() is a direct accessor for the internal counter; it bypasses the
 * uint8_t cast of ioRead() and is used by the test harness to observe state.
 *
 * Configuration: Timer mode, prescaler ÷16, TC=5 on channel 3.
 *   Control byte: 0x05
 *
 * Expected:
 *   - Initially: getCount(3) = 5.
 *   - After 16 ticks: getCount(3) = 4.
 */
TEST(Z80CTC, GetCount_TimerDecrementsCorrectly) {
    Z80CTC ctc;
    configChannel(ctc, 3, 0x05, 5);

    EXPECT_EQ(ctc.getCount(3), 5);
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.getCount(3), 4);
}

/**
 * @brief isTimerMode() returns true for a channel in Timer mode.
 *
 * D6=0 in the control register selects Timer mode.
 *
 * Configuration: Timer mode (D6=0), prescaler ÷16, TC=1.
 *   Control byte: 0x05
 */
TEST(Z80CTC, IsTimerMode_True) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 1);  // timer mode: D6=0
    EXPECT_TRUE(ctc.isTimerMode(0));
}

/**
 * @brief isTimerMode() returns false for a channel in Counter mode.
 *
 * D6=1 in the control register selects Counter mode.  The function must
 * return false for any channel where CTRL_COUNTER is set.
 *
 * Configuration: Counter mode (D6=1), falling edge, TC=1.
 *   Control byte: 0x45  (D6=1 COUNTER | D2=1 TC_FOLLOWS | D0=1)
 */
TEST(Z80CTC, IsTimerMode_FalseForCounter) {
    Z80CTC ctc;
    // Counter mode (D6=1), falling edge (D4=0), TC follows — control byte = 0x45
    configChannel(ctc, 0, 0x45, 1);
    EXPECT_FALSE(ctc.isTimerMode(0));
}

// =============================================================================
// Control word reset (D1=1)
// =============================================================================

/**
 * @brief Software reset (D1=1) clears the counter to zero immediately.
 *
 * Writing a control word with D1=1 (CTRL_RESET) aborts the current count and
 * sets the counter to 0.  The channel will not restart until it receives a new
 * control/TC sequence.
 *
 * Sequence:
 *   1. Configure ch0: Timer, ÷16, TC=10.
 *   2. Run 48 ticks → counter = 10 − 3 = 7 (48 / 16 = 3 decrements).
 *   3. Write reset (0x03 = D1|D0).
 *   4. ioRead should return 0 and getCount should be ≤ 0.
 */
TEST(Z80CTC, ControlWord_ResetClearsCounter) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);

    // Run until counter reaches 7 (3 prescaler overflows at 16 ticks each)
    for (int i = 0; i < 48; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 7);

    // Software reset: D1=1 (CTRL_RESET), D0=1 (ctrl flag) → 0x03
    ctc.ioWrite(0, 0x03);
    EXPECT_EQ(ctc.ioRead(0), 0);                                    // counter zeroed
    EXPECT_FALSE(ctc.isTimerMode(0) && ctc.getCount(0) > 0);        // not counting
}

/**
 * @brief After a software reset, further clock ticks do not advance the counter.
 *
 * The reset stops the timer; the channel must remain idle until re-programmed.
 *
 * Sequence:
 *   1. Configure ch0: Timer, ÷16, TC=10.
 *   2. Run 16 ticks → counter = 9.
 *   3. Write reset (0x03).
 *   4. Run 160 more ticks — counter must stay at 0.
 */
TEST(Z80CTC, ControlWord_ResetStopsTimer) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);
    for (int i = 0; i < 16; ++i) ctc.clockTick();  // counter → 9

    ctc.ioWrite(0, 0x03);                           // reset
    for (int i = 0; i < 160; ++i) ctc.clockTick(); // timer must not run

    EXPECT_EQ(ctc.ioRead(0), 0);  // still 0, not reloaded
}

/**
 * @brief Software reset clears a pending interrupt.
 *
 * Resetting a channel with an active interrupt pending must clear int_pending
 * so that hasInterrupt() returns false immediately after the reset write.
 *
 * Sequence:
 *   1. Configure ch0: Timer, ÷16, IRQ enabled, TC=1.
 *   2. Run 16 ticks → interrupt pending.
 *   3. Write reset (0x03) → int_pending must clear.
 */
TEST(Z80CTC, ControlWord_ResetClearsPendingInterrupt) {
    Z80CTC ctc;
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);               // vector base = 0x20
    configChannel(ctc, 0, 0x85, 1);    // IRQ enabled, TC=1, period = 16 ticks

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_TRUE(ctc.hasInterrupt());

    ctc.ioWrite(0, 0x03);               // reset clears int_pending
    EXPECT_FALSE(ctc.hasInterrupt());
}

// =============================================================================
// Multi-channel independence
// =============================================================================

/**
 * @brief Two channels run independently with different time constants.
 *
 * Each channel has its own prescaler and counter; they must not interfere.
 *
 * Configuration:
 *   - ch0: Timer, ÷16, TC=5  → fires every 80 ticks.
 *   - ch1: Timer, ÷16, TC=10 → fires every 160 ticks.
 *
 * Expected after 80 ticks:  ch0 fired once, ch1 not yet.
 * Expected after 160 ticks: ch0 fired twice, ch1 fired once.
 */
TEST(Z80CTC, MultipleChannels_IndependentTiming) {
    Z80CTC ctc;
    int ch0_fires = 0, ch1_fires = 0;
    ctc.setZCTOCallback([&](int ch, bool level) {
        if (!level) {
            if (ch == 0) ++ch0_fires;
            if (ch == 1) ++ch1_fires;
        }
    });

    configChannel(ctc, 0, 0x05, 5);   // period: 5 × 16 = 80 ticks
    configChannel(ctc, 1, 0x05, 10);  // period: 10 × 16 = 160 ticks

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_EQ(ch0_fires, 1);
    EXPECT_EQ(ch1_fires, 0);

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_EQ(ch0_fires, 2);
    EXPECT_EQ(ch1_fires, 1);
}

// =============================================================================
// CLK/TRG trigger mode (timer started by an external pulse)
// =============================================================================

/**
 * @brief In CLK/TRG trigger mode the timer starts only on a qualified external edge.
 *
 * When D3=1 (CTRL_CLK_TRG) is set in Timer mode the channel does not start
 * automatically after loading the time constant.  Instead it waits for a
 * qualifying CLK/TRG edge (polarity selected by D4) and then runs for exactly
 * one period (one-shot behaviour: stops after the first ZC/TO).
 *
 * Configuration: Timer mode (D6=0), prescaler ÷16 (D5=0),
 *                CLK/TRG trigger (D3=1), rising edge (D4=1), TC=2.
 *   Control byte: 0x1D  (D4=1 EDGE_RISE | D3=1 CLK_TRG | D2=1 TC_FOLLOWS | D0=1)
 *   Period after trigger: 2 × 16 = 32 ticks.
 *
 * Expected:
 *   - 64 clock ticks before trigger: no ZC/TO (timer not running).
 *   - Rising edge on CLK/TRG: timer starts.
 *   - After 31 more ticks: no ZC/TO yet.
 *   - After tick 32 (relative to trigger): ZC/TO fires once; timer stops.
 */
TEST(Z80CTC, TimerMode_ClkTrgStartsTimer) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // Timer mode, ÷16, CLK/TRG trigger on rising edge, TC follows — 0x1D
    configChannel(ctc, 0, 0x1D, 2);  // 2 × 16 = 32 ticks after trigger

    // Before trigger: clock ticks have no effect (timer not running)
    for (int i = 0; i < 64; ++i) ctc.clockTick();
    EXPECT_EQ(fired, 0);

    // Rising edge arms and starts the one-shot timer
    ctc.clkTrg(0, true);
    for (int i = 0; i < 31; ++i) ctc.clockTick();
    EXPECT_EQ(fired, 0);              // not yet at tick 32

    ctc.clockTick();                  // tick 32 relative to trigger → zero crossing
    EXPECT_EQ(fired, 1);
}

// =============================================================================
// Spurious-interrupt regression (A5120 real-time-clock runaway)
// =============================================================================

/**
 * @brief A channel blocked by a higher-priority channel's IUS must NOT assert /INT.
 *
 * Regression for the A5120 real-time-clock runaway: while the keyboard timer
 * (ch2) was Under Service (IUS set, awaiting its RETI), the cascaded 1 s clock
 * channel (ch3) fired.  ch3 then had int_pending=true but iei=false (blocked by
 * ch2's IUS in the internal daisy chain).  getVector() correctly skipped ch3 and
 * returned 0xFF — but hasInterrupt() used the raw "any int_pending" test and so
 * returned true.  The bus therefore asserted /INT, the CPU acknowledged, and read
 * the spurious 0xFF vector every instruction; CP/A's handler for it bumped the
 * clock by 1 s each time, running it ~1000x too fast.
 *
 * Contract: hasInterrupt() must be consistent with getVector() — it may only be
 * true when a channel can actually be vectored (int_pending && iei && !ius).
 */
TEST(Z80CTC, NoSpuriousInterruptWhenLowerChannelBlockedByHigherIUS) {
    Z80CTC ctc;
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);  // vector base 0x20 (D0=0 → vector write)

    // ch2: timer, IRQ enabled, ÷16, TC=1 → fires after 16 ticks.
    configChannel(ctc, 2, 0x85, 1);
    // ch3: counter, IRQ enabled, falling edge, TC=1 → fires on one CLK/TRG edge.
    configChannel(ctc, 3, 0xC5, 1);

    // ch2 reaches zero and requests an interrupt.
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    ctc.setIEI(true);                       // propagate daisy chain (run loop does this)
    EXPECT_TRUE(ctc.hasInterrupt());
    EXPECT_EQ(ctc.getVector(), 0x24);       // ch2 acknowledged → ch2.ius set

    // While ch2 is Under Service, the cascaded ch3 fires (1 s tick).
    ctc.clkTrg(3, false);                   // falling edge → ch3 counter 1→0 → int_pending
    ctc.setIEI(true);                       // re-propagate: ch2.ius blocks ch3.iei

    // ch3 is pending but blocked — it must NOT pull /INT, and there is no vector.
    EXPECT_FALSE(ctc.hasInterrupt());       // was true before the fix → spurious 0xFF
    EXPECT_EQ(ctc.getVector(), 0xFF);

    // ch2's ISR returns: RETI clears ch2.ius, unblocking ch3.
    ctc.onRETI();
    ctc.setIEI(true);                       // re-propagate

    // Now ch3 is serviceable and delivers its own vector exactly once.
    EXPECT_TRUE(ctc.hasInterrupt());
    EXPECT_EQ(ctc.getVector(), 0x26);       // ch3: 0x20 | (3 << 1)
}

// =============================================================================
// debugState() — read-only snapshot for the debugger's `dev ctc` command
// =============================================================================

/**
 * @brief debugState reflects control/TC/intEn plus the IUS/IEI/IEO daisy-chain state.
 */
TEST(Z80CTC, DebugStateReflectsChannelAndDaisyChain) {
    Z80CTC ctc("test");
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);                   // vector base 0x20

    configChannel(ctc, 2, 0x85, 1);         // ch2: timer, IRQ enabled, ÷16, TC=1
    {
        auto d = ctc.debugState();
        EXPECT_EQ(d.vecBase, 0x20);
        EXPECT_TRUE(d.ieo);                 // nothing in service yet → IEO high
        EXPECT_EQ(d.ch[2].control, 0x85);
        EXPECT_EQ(d.ch[2].timeConst, 1);
        EXPECT_TRUE(d.ch[2].intEn);         // D7 set
        EXPECT_FALSE(d.ch[2].intPending);
        EXPECT_FALSE(d.ch[2].ius);
        EXPECT_FALSE(d.ch[0].intEn);        // ch0 untouched (reset)
    }

    // Fire ch2: interrupt now PENDING (before ack) → IEO drops to block downstream.
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    ctc.setIEI(true);
    {
        auto d = ctc.debugState();
        EXPECT_TRUE(d.ch[2].intPending);
        EXPECT_FALSE(d.ch[2].ius);
        EXPECT_FALSE(d.ieo);                // pending → chain blocked (iei_ && !anyPending)
    }

    // Acknowledge → ch2 goes Under Service (ius), pending cleared.
    EXPECT_EQ(ctc.getVector(), 0x24);
    {
        auto d = ctc.debugState();
        EXPECT_TRUE(d.ch[2].ius);           // under service
        EXPECT_FALSE(d.ch[2].intPending);   // cleared on acknowledge
        EXPECT_TRUE(d.ieo);                 // no longer pending → IEO high again
    }
}
