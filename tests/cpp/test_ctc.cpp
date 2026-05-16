#include <gtest/gtest.h>
#include "core/primitives/z80_ctc.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Write a control word then a time constant to a channel.
// ctrl: raw control byte (D0=1 assumed, D2=1 for TC to follow).
// The caller must set D2=1 in ctrl so that the TC write is accepted.
static void configChannel(Z80CTC& ctc, int ch, uint8_t ctrl, uint8_t tc) {
    ctc.ioWrite(static_cast<uint8_t>(ch), ctrl);
    ctc.ioWrite(static_cast<uint8_t>(ch), tc);
}

// ─── Timer mode ──────────────────────────────────────────────────────────────

// prescaler=16, TC=10 → ZC/TO fires after exactly 160 system ticks.
TEST(Z80CTC, TimerMode_ZCTOFiresAfter160Ticks) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int /*ch*/, bool level) {
        if (!level) ++zcto_count;  // count falling edges only
    });

    // D7=0 no IRQ, D6=0 auto, D4=0 prescaler/16, D3=0 timer, D2=1 TC follows, D0=1
    configChannel(ctc, 0, 0x05, 10);

    for (int i = 0; i < 159; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 0);

    ctc.clockTick();  // tick 160
    EXPECT_EQ(zcto_count, 1);
}

// Timer continues reloading after first ZC/TO.
TEST(Z80CTC, TimerMode_AutoReload) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++zcto_count; });

    configChannel(ctc, 0, 0x05, 10);  // prescaler/16, TC=10

    for (int i = 0; i < 320; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 2);
}

// ZC/TO pulse: callback(ch, false) then callback(ch, true) on next tick.
TEST(Z80CTC, TimerMode_ZCTOPulseHighAfterOneTick) {
    Z80CTC ctc;
    std::vector<std::pair<int,bool>> events;
    ctc.setZCTOCallback([&](int ch, bool level) { events.push_back({ch, level}); });

    configChannel(ctc, 0, 0x05, 1);  // prescaler/16, TC=1 → fires every 16 ticks

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    // Tick 16: falling edge generated
    ASSERT_GE(static_cast<int>(events.size()), 1u);
    EXPECT_EQ(events[0].first, 0);
    EXPECT_FALSE(events[0].second);

    ctc.clockTick();  // tick 17: rising edge should be generated
    ASSERT_GE(static_cast<int>(events.size()), 2u);
    EXPECT_TRUE(events[1].second);
}

// ─── Timer mode, prescaler 256 ────────────────────────────────────────────────

TEST(Z80CTC, TimerMode_Prescaler256) {
    Z80CTC ctc;
    int zcto_count = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++zcto_count; });

    // D4=1 prescaler/256, D3=0 timer, D2=1 TC follows, D0=1 → 0x15
    configChannel(ctc, 0, 0x15, 2);  // 2 × 256 = 512 ticks

    for (int i = 0; i < 511; ++i) ctc.clockTick();
    EXPECT_EQ(zcto_count, 0);
    ctc.clockTick();
    EXPECT_EQ(zcto_count, 1);
}

// ─── Timer mode interrupt ─────────────────────────────────────────────────────

TEST(Z80CTC, TimerMode_InterruptGenerated) {
    Z80CTC ctc;
    // Set interrupt vector base: write to ch0 with D0=0
    ctc.ioWrite(0, 0x20);  // vec_base = 0x20

    // D7=1 IRQ, D4=0 prescaler/16, D3=0 timer, D2=1 TC follows, D0=1 → 0x85
    configChannel(ctc, 0, 0x85, 5);  // 5 × 16 = 80 ticks

    ctc.setIEI(true);
    EXPECT_FALSE(ctc.hasInterrupt());

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_TRUE(ctc.hasInterrupt());
}

// Interrupt is NOT generated when IEI is false.
TEST(Z80CTC, TimerMode_NoInterruptWhenIEILow) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 1);  // IRQ enabled, TC=1
    ctc.setIEI(false);
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_FALSE(ctc.hasInterrupt());
}

// ─── Counter mode ────────────────────────────────────────────────────────────

// ZC/TO fires after exactly TC CLK/TRG rising-edge pulses.
TEST(Z80CTC, CounterMode_ZCTOAfterNPulses) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // D5=1 rising edge, D3=1 counter, D2=1 TC follows, D0=1 → 0x2D
    configChannel(ctc, 1, 0x2D, 5);

    for (int i = 0; i < 4; ++i) ctc.clkTrg(1, true);
    EXPECT_EQ(fired, 0);

    ctc.clkTrg(1, true);
    EXPECT_EQ(fired, 1);
}

// Wrong edge is ignored in counter mode.
TEST(Z80CTC, CounterMode_FallingEdgeIgnoredWhenRisingSelected) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    configChannel(ctc, 0, 0x2D, 3);  // rising edge selected

    ctc.clkTrg(0, false);  // falling: ignored
    ctc.clkTrg(0, false);
    ctc.clkTrg(0, false);
    EXPECT_EQ(fired, 0);

    ctc.clkTrg(0, true);
    ctc.clkTrg(0, true);
    ctc.clkTrg(0, true);
    EXPECT_EQ(fired, 1);
}

// Counter mode with falling edge selected.
TEST(Z80CTC, CounterMode_FallingEdge) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // D5=0 falling edge, D3=1 counter, D2=1 TC follows, D0=1 → 0x0D
    configChannel(ctc, 2, 0x0D, 3);

    ctc.clkTrg(2, false);
    ctc.clkTrg(2, false);
    ctc.clkTrg(2, false);
    EXPECT_EQ(fired, 1);
}

// ─── ZCTOCallback ────────────────────────────────────────────────────────────

TEST(Z80CTC, ZCTOCallback_ReceivesCorrectChannel) {
    Z80CTC ctc;
    int cb_channel = -1;
    ctc.setZCTOCallback([&](int ch, bool level) {
        if (!level) cb_channel = ch;
    });

    configChannel(ctc, 2, 0x05, 1);  // channel 2, prescaler/16, TC=1
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_EQ(cb_channel, 2);
}

TEST(Z80CTC, ZCTOCallback_NotCalledBeforeFire) {
    Z80CTC ctc;
    bool called = false;
    ctc.setZCTOCallback([&](int, bool) { called = true; });

    configChannel(ctc, 0, 0x05, 10);
    for (int i = 0; i < 159; ++i) ctc.clockTick();
    EXPECT_FALSE(called);
}

// ─── Interrupt vector calculation ────────────────────────────────────────────

TEST(Z80CTC, InterruptVector_Calculation) {
    Z80CTC ctc;
    ctc.setIEI(true);

    // Set vector base 0x20 (write to ch0, D0=0)
    ctc.ioWrite(0, 0x20);

    // Configure all 4 channels with IRQ enabled, TC=1
    for (int i = 0; i < 4; ++i) {
        configChannel(ctc, i, 0x85, 1);  // IRQ, prescaler/16, timer, TC=1
    }

    // Tick all channels to fire
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    // Highest priority channel is 0; its vector is vec_base | (0<<1) = 0x20
    EXPECT_EQ(ctc.getVector(), 0x20);

    // Clear ch0 pending and check ch1
    // Re-write ch0 control with reset to clear pending
    ctc.ioWrite(0, 0x03);  // reset ch0
    EXPECT_EQ(ctc.getVector(), 0x22);  // ch1: 0x20 | (1<<1)

    ctc.ioWrite(1, 0x03);  // reset ch1
    EXPECT_EQ(ctc.getVector(), 0x24);  // ch2

    ctc.ioWrite(2, 0x03);  // reset ch2
    EXPECT_EQ(ctc.getVector(), 0x26);  // ch3
}

TEST(Z80CTC, InterruptVector_NoInterruptReturns0xFF) {
    Z80CTC ctc;
    ctc.setIEI(true);
    EXPECT_EQ(ctc.getVector(), 0xFF);
}

// ─── IEI/IEO daisy chain ─────────────────────────────────────────────────────

// When ch0 has a pending interrupt (IEI active), IEO goes low → blocks downstream.
TEST(Z80CTC, DaisyChain_IEOLowWhenPending) {
    Z80CTC ctc;
    ctc.ioWrite(0, 0x20);  // vector base

    configChannel(ctc, 0, 0x85, 1);  // ch0: IRQ enabled, TC=1
    ctc.setIEI(true);

    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_TRUE(ctc.hasInterrupt());
    EXPECT_FALSE(ctc.getIEO());  // downstream is blocked
}

// When no channel is pending, IEO passes IEI through.
TEST(Z80CTC, DaisyChain_IEOHighWhenNoPending) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 10);
    ctc.setIEI(true);

    EXPECT_FALSE(ctc.hasInterrupt());
    EXPECT_TRUE(ctc.getIEO());
}

// Ch0 pending blocks ch1 from becoming the winning vector.
TEST(Z80CTC, DaisyChain_Ch0BlocksCh1) {
    Z80CTC ctc;
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);  // vector base 0x20

    configChannel(ctc, 0, 0x85, 1);  // ch0: IRQ, TC=1
    configChannel(ctc, 1, 0x85, 1);  // ch1: IRQ, TC=1

    for (int i = 0; i < 16; ++i) ctc.clockTick();

    // Both channels have pending interrupts; ch0 wins
    EXPECT_EQ(ctc.getVector(), 0x20);  // ch0 vector, not 0x22
}

// IEI=false: hasInterrupt false, IEO=false (blocked input propagates blocked output).
TEST(Z80CTC, DaisyChain_IEILowBlocksAll) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x85, 1);
    ctc.setIEI(false);
    for (int i = 0; i < 16; ++i) ctc.clockTick();

    EXPECT_FALSE(ctc.hasInterrupt());
    EXPECT_FALSE(ctc.getIEO());
}

// ─── ioRead returns current count ────────────────────────────────────────────

TEST(Z80CTC, IoRead_ReturnsCurrentCount) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);  // timer, prescaler/16, TC=10

    EXPECT_EQ(ctc.ioRead(0), 10);

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 9);

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 8);
}

TEST(Z80CTC, IoRead_WrapsToZeroForTC256) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 0);  // TC=0 means 256

    // getCount returns 256 initially, but ioRead returns uint8_t → 0
    EXPECT_EQ(ctc.ioRead(0), 0);
    EXPECT_EQ(ctc.getCount(0), 256);
}

// ─── getCount / isTimerMode helpers ──────────────────────────────────────────

TEST(Z80CTC, GetCount_TimerDecrementsCorrectly) {
    Z80CTC ctc;
    configChannel(ctc, 3, 0x05, 5);

    EXPECT_EQ(ctc.getCount(3), 5);
    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.getCount(3), 4);
}

TEST(Z80CTC, IsTimerMode_True) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 1);  // timer mode
    EXPECT_TRUE(ctc.isTimerMode(0));
}

TEST(Z80CTC, IsTimerMode_FalseForCounter) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x0D, 1);  // counter mode (D3=1, D2=1 TC follows)
    EXPECT_FALSE(ctc.isTimerMode(0));
}

// ─── Control word reset ───────────────────────────────────────────────────────

TEST(Z80CTC, ControlWord_ResetClearsCounter) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);

    // Run a few ticks so the counter has decremented
    for (int i = 0; i < 48; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.ioRead(0), 7);  // 10 - 3 decrements (48/16=3)

    // Write control word with reset (D1=1, D0=1) = 0x03
    ctc.ioWrite(0, 0x03);
    EXPECT_EQ(ctc.ioRead(0), 0);  // counter cleared
    EXPECT_FALSE(ctc.isTimerMode(0) && ctc.getCount(0) > 0);  // not running
}

// Reset stops the timer: further ticks don't change the counter.
TEST(Z80CTC, ControlWord_ResetStopsTimer) {
    Z80CTC ctc;
    configChannel(ctc, 0, 0x05, 10);
    for (int i = 0; i < 16; ++i) ctc.clockTick();  // counter → 9

    ctc.ioWrite(0, 0x03);  // reset
    for (int i = 0; i < 160; ++i) ctc.clockTick();  // should not run

    EXPECT_EQ(ctc.ioRead(0), 0);  // still 0, not reloaded
}

// Reset also clears a pending interrupt.
TEST(Z80CTC, ControlWord_ResetClearsPendingInterrupt) {
    Z80CTC ctc;
    ctc.setIEI(true);
    ctc.ioWrite(0, 0x20);               // vector base
    configChannel(ctc, 0, 0x85, 1);    // IRQ enabled, TC=1

    for (int i = 0; i < 16; ++i) ctc.clockTick();
    EXPECT_TRUE(ctc.hasInterrupt());

    ctc.ioWrite(0, 0x03);  // reset clears pending
    EXPECT_FALSE(ctc.hasInterrupt());
}

// ─── Multiple channels independent ───────────────────────────────────────────

TEST(Z80CTC, MultipleChannels_IndependentTiming) {
    Z80CTC ctc;
    int ch0_fires = 0, ch1_fires = 0;
    ctc.setZCTOCallback([&](int ch, bool level) {
        if (!level) {
            if (ch == 0) ++ch0_fires;
            if (ch == 1) ++ch1_fires;
        }
    });

    configChannel(ctc, 0, 0x05, 5);   // 5×16=80 ticks
    configChannel(ctc, 1, 0x05, 10);  // 10×16=160 ticks

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_EQ(ch0_fires, 1);
    EXPECT_EQ(ch1_fires, 0);

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_EQ(ch0_fires, 2);
    EXPECT_EQ(ch1_fires, 1);
}

// ─── CLK/TRG trigger mode (timer started by external pulse) ─────────────────

TEST(Z80CTC, TimerMode_ClkTrgStartsTimer) {
    Z80CTC ctc;
    int fired = 0;
    ctc.setZCTOCallback([&](int, bool level) { if (!level) ++fired; });

    // D6=1 CLK/TRG trigger, D4=0 prescaler/16, D3=0 timer, D2=1 TC follows, D0=1
    // D5=1 rising edge trigger → 0x65
    configChannel(ctc, 0, 0x65, 2);  // 2×16=32 ticks after trigger

    // Without trigger, clock ticks do nothing
    for (int i = 0; i < 64; ++i) ctc.clockTick();
    EXPECT_EQ(fired, 0);

    // Rising edge starts the timer (one-shot)
    ctc.clkTrg(0, true);
    for (int i = 0; i < 31; ++i) ctc.clockTick();
    EXPECT_EQ(fired, 0);
    ctc.clockTick();  // tick 32 after trigger
    EXPECT_EQ(fired, 1);
}
