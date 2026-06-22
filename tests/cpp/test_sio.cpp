/**
 * @file test_sio.cpp
 * @brief Unit tests for the Z80 SIO serial controller emulation.
 *
 * @details
 * Emulator component under test: **Z80SIO** (`core/primitives/z80_sio.h`)
 *
 * The Z80 SIO (Serial Input/Output controller) provides two independent full-duplex
 * serial channels (A and B).  Each channel has:
 *  - An RX FIFO (depth 3) filled by rxByte() on the external/emulator side.
 *  - A TX buffer filled by the CPU via ioWrite(data_port, byte).
 *  - Status register RR0: bit 0 = Rx Character Available, bit 2 = TX Buffer Empty.
 *  - Interrupt generation: TX empty (after txGet()) and RX received (WR1 mode).
 *
 * Interrupt vector is written to WR2 (on Ch B control port) and encodes the source
 * in bits[3:1] following the Z80 SIO specification:
 *  - Ch A Rx: 110 → 0x0C
 *  - Ch B Rx: 100 → 0x08
 *  - Ch A Tx: 000 → 0x00
 *
 * The daisy-chain interface (setIEI, getIEO, hasInterrupt) follows the Z80 interrupt
 * priority scheme.
 *
 * Port offsets (relative to SIO base):
 *  - 0: Channel A data
 *  - 1: Channel A control (RR0/RR1 read; WR0/WR1/WR2 write via register pointer)
 *  - 2: Channel B data
 *  - 3: Channel B control
 *
 * ## Test groups
 *
 * | Group                     | What is tested                                         |
 * |---------------------------|--------------------------------------------------------|
 * | RX path                   | rxByte() sets RR0 bit 0; ioRead clears it when FIFO empty |
 * | TX path                   | txAvailable(); txGet() consumes byte                   |
 * | RX FIFO                   | Multiple bytes in order; full detection; overflow       |
 * | TX interrupt              | After txGet(), TX empty interrupt fires if WR1 enabled  |
 * | RX interrupt              | All-received mode: interrupt on every rxByte()          |
 * | Interrupt vector (WR2)    | Base vector with encoded source bits                    |
 * | Priority (Ch A over B)    | Ch A vector wins when both channels pending             |
 * | IEI/IEO pass-through      | IEO passes IEI when no pending; blocked when pending    |
 * | RR0 TX Buffer Empty bit   | Tracks TX buffer fill/drain state                      |
 * | Channel B                 | Channel B data and control work symmetrically           |
 *
 * @see core/primitives/z80_sio.h
 */

#include <gtest/gtest.h>
#include "core/primitives/z80_sio.h"

// ─── RX path ─────────────────────────────────────────────────────────────────

/**
 * @test Z80SIO/RX_ByteSetsBit0_ReadClearsBit0WhenEmpty
 * @brief rxByte() sets RR0 bit 0 (Rx Character Available); reading the data port clears it when FIFO empty.
 * @par Pass criterion  RR0 bit 0 == 1 after rxByte(); data == 0xAB from ioRead(0); RR0 bit 0 == 0 after.
 */
TEST(Z80SIO, RX_ByteSetsBit0_ReadClearsBit0WhenEmpty) {
    Z80SIO sio;
    auto& ch = sio.channelA();

    // Before injection: RR0 bit0 clear
    EXPECT_EQ(sio.ioRead(1) & 0x01, 0);

    ch.rxByte(0xAB);

    // RR0 bit0 should be set
    EXPECT_EQ(sio.ioRead(1) & 0x01, 1);

    // Read the data
    uint8_t b = sio.ioRead(0);
    EXPECT_EQ(b, 0xAB);

    // RR0 bit0 cleared after FIFO empty
    EXPECT_EQ(sio.ioRead(1) & 0x01, 0);
}

// ─── TX path ─────────────────────────────────────────────────────────────────

/**
 * @test Z80SIO/TX_WriteData_AvailableThenGet
 * @brief A CPU write to the data port makes txAvailable() true; txGet() returns the byte and clears availability.
 * @par Pass criterion  txAvailable() == true; txGet() == 0x55; txAvailable() == false.
 */
TEST(Z80SIO, TX_WriteData_AvailableThenGet) {
    Z80SIO sio;
    auto& ch = sio.channelA();

    EXPECT_FALSE(ch.txAvailable());

    sio.ioWrite(0, 0x55);

    EXPECT_TRUE(ch.txAvailable());
    EXPECT_EQ(ch.txGet(), 0x55);
    EXPECT_FALSE(ch.txAvailable());
}

// ─── Multiple RX bytes (FIFO) ─────────────────────────────────────────────────

/**
 * @test Z80SIO/RX_FIFO_MultipleBytes
 * @brief Three bytes injected via rxByte() are delivered in FIFO order by ioRead(0).
 * @par Pass criterion  Reads return 0x11, 0x22, 0x33 in order; RR0 bit 0 clear after last read.
 */
TEST(Z80SIO, RX_FIFO_MultipleBytes) {
    Z80SIO sio;
    auto& ch = sio.channelA();

    ch.rxByte(0x11);
    ch.rxByte(0x22);
    ch.rxByte(0x33);

    EXPECT_EQ(sio.ioRead(0), 0x11);
    EXPECT_EQ(sio.ioRead(0), 0x22);
    EXPECT_EQ(sio.ioRead(0), 0x33);

    // FIFO empty
    EXPECT_EQ(sio.ioRead(1) & 0x01, 0);
}

/**
 * @test Z80SIO/RX_FIFO_Full
 * @brief After three bytes the FIFO is full; a fourth byte causes RR1 overrun bit to be set.
 * @par Pass criterion  rxFull() == true after 3 bytes; RR1 bit 3 (overrun) set after 4th byte.
 */
TEST(Z80SIO, RX_FIFO_Full) {
    Z80SIO sio;
    auto& ch = sio.channelA();

    ch.rxByte(0x01);
    ch.rxByte(0x02);
    ch.rxByte(0x03);

    EXPECT_TRUE(ch.rxFull());

    // 4th byte overflows — sets overrun in RR1
    ch.rxByte(0x04);
    sio.ioRead(1); // read RR0 (reg_ptr=0)
    // set reg_ptr to 1 to read RR1
    sio.ioWrite(1, 0x01); // WR0: point to register 1
    uint8_t rr1 = sio.ioRead(1);
    EXPECT_TRUE(rr1 & 0x08); // overrun bit
}

// ─── TX interrupt enable ──────────────────────────────────────────────────────

/**
 * @test Z80SIO/TX_Interrupt_EnabledAfterTxGet
 * @brief When TX interrupt is enabled (WR1 bit 1), hasInterrupt() becomes true after txGet().
 * @details The TX interrupt fires when the TX buffer transitions from full to empty.
 * @par Pass criterion  hasInterrupt() == false before txGet(); == true after txGet().
 */
TEST(Z80SIO, TX_Interrupt_EnabledAfterTxGet) {
    Z80SIO sio;
    sio.setIEI(true);

    // Enable TX interrupt: WR0 points to WR1, then write WR1 with bit1=1
    sio.ioWrite(1, 0x01); // WR0: select WR1
    sio.ioWrite(1, 0x02); // WR1: Tx interrupt enable

    // Write TX byte
    sio.ioWrite(0, 0x42);
    auto& ch = sio.channelA();
    EXPECT_FALSE(sio.hasInterrupt()); // not yet: TX buffer filled, no int until consumed

    // External side consumes the byte — triggers TX empty interrupt
    ch.txGet();
    EXPECT_TRUE(sio.hasInterrupt());
}

// ─── RX interrupt (all-received mode) ────────────────────────────────────────

/**
 * @test Z80SIO/RX_Interrupt_AllReceivedMode
 * @brief With WR1 bits[3:2] = 10 (interrupt on all received), rxByte() triggers hasInterrupt().
 * @par Pass criterion  hasInterrupt() == false before rxByte(); == true after rxByte().
 */
TEST(Z80SIO, RX_Interrupt_AllReceivedMode) {
    Z80SIO sio;
    sio.setIEI(true);

    // WR1 bits[3:2] = 10 → interrupt on all received characters
    sio.ioWrite(1, 0x01); // select WR1
    sio.ioWrite(1, 0x08); // bits[3:2]=10

    EXPECT_FALSE(sio.hasInterrupt());

    sio.channelA().rxByte(0x7F);
    EXPECT_TRUE(sio.hasInterrupt());
}

// ─── Interrupt vector from WR2 ────────────────────────────────────────────────

/**
 * @test Z80SIO/InterruptVector_FromWR2
 * @brief The interrupt vector encodes the WR2 base and the source type in bits[3:1].
 * @details Ch A Rx interrupt has status bits 110 in bits[3:1]; WR2 base = 0x60.
 * @par Pass criterion  (vec & 0xF1) matches (0x60 & 0xF1); (vec & 0x0E) == 0x0C.
 */
TEST(Z80SIO, InterruptVector_FromWR2) {
    Z80SIO sio;
    sio.setIEI(true);

    // Write WR2 on channel B (port 3 = Ch B control)
    sio.ioWrite(3, 0x02); // WR0: select WR2
    sio.ioWrite(3, 0x60); // WR2 = 0x60

    // Enable RX int on channel A
    sio.ioWrite(1, 0x01);
    sio.ioWrite(1, 0x08);
    sio.channelA().rxByte(0x01);

    // Vector should be based on 0x60 with status bits for Ch A Rx
    uint8_t vec = sio.getVector();
    EXPECT_EQ(vec & 0xF1, 0x60 & 0xF1); // base preserved in upper bits and bit0
    // Ch A Rx = bits[3:1] = 110 → 0x0C
    EXPECT_EQ(vec & 0x0E, 0x0C);
}

// ─── Channel A priority over B ────────────────────────────────────────────────

/**
 * @test Z80SIO/Priority_ChA_OverChB
 * @brief When both channels have pending interrupts, Channel A wins and its vector is returned.
 * @par Pass criterion  hasInterrupt() == true; (getVector() & 0x0E) == 0x0C (Ch A Rx).
 */
TEST(Z80SIO, Priority_ChA_OverChB) {
    Z80SIO sio;
    sio.setIEI(true);

    // Enable RX int on both channels
    sio.ioWrite(1, 0x01); sio.ioWrite(1, 0x08); // Ch A WR1
    sio.ioWrite(3, 0x01); sio.ioWrite(3, 0x08); // Ch B WR1

    // Set WR2 (B ctrl port)
    sio.ioWrite(3, 0x02); // select WR2
    sio.ioWrite(3, 0x00); // vector base = 0

    sio.channelA().rxByte(0xAA);
    sio.channelB().rxByte(0xBB);

    EXPECT_TRUE(sio.hasInterrupt());

    // Vector should reflect Ch A (higher priority)
    uint8_t vec = sio.getVector();
    EXPECT_EQ(vec & 0x0E, 0x0C); // Ch A Rx = 110 in bits[3:1]
}

// ─── IEI/IEO pass-through ────────────────────────────────────────────────────

/**
 * @test Z80SIO/IEI_IEO_PassThrough_NoInterrupt
 * @brief With IEI=true and no pending interrupt, IEO passes through (true).
 * @par Pass criterion  getIEO() == true.
 */
TEST(Z80SIO, IEI_IEO_PassThrough_NoInterrupt) {
    Z80SIO sio;
    sio.setIEI(true);

    // No pending interrupt → IEO should pass through (true)
    EXPECT_TRUE(sio.getIEO());
}

/**
 * @test Z80SIO/IEI_IEO_Blocked_WhenInterruptPending
 * @brief IEO is false when an interrupt is pending (to block downstream devices).
 * @par Pass criterion  hasInterrupt() == true; getIEO() == false.
 */
TEST(Z80SIO, IEI_IEO_Blocked_WhenInterruptPending) {
    Z80SIO sio;
    sio.setIEI(true);

    // Enable RX interrupt
    sio.ioWrite(1, 0x01);
    sio.ioWrite(1, 0x08);
    sio.channelA().rxByte(0x01);

    EXPECT_TRUE(sio.hasInterrupt());
    // IEO blocked
    EXPECT_FALSE(sio.getIEO());
}

/**
 * @test Z80SIO/IEI_False_NoInterruptPropagated
 * @brief With IEI=false, the SIO cannot assert an interrupt; IEO is true (pass-through logic).
 * @par Pass criterion  hasInterrupt() == false; getIEO() == true.
 */
TEST(Z80SIO, IEI_False_NoInterruptPropagated) {
    Z80SIO sio;
    sio.setIEI(false);

    sio.ioWrite(1, 0x01);
    sio.ioWrite(1, 0x08);
    sio.channelA().rxByte(0x55);

    EXPECT_FALSE(sio.hasInterrupt());
    // IEO passes through when IEI=false (not our turn)
    EXPECT_TRUE(sio.getIEO());
}

// ─── TX Buffer Empty bit in RR0 ──────────────────────────────────────────────

/**
 * @test Z80SIO/RR0_TxBufferEmpty_Tracking
 * @brief RR0 bit 2 (TX Buffer Empty) is initially set, cleared on write, and set again after txGet().
 * @par Pass criterion  Bit 2 == 1 initially; == 0 after write; == 1 after txGet().
 */
TEST(Z80SIO, RR0_TxBufferEmpty_Tracking) {
    Z80SIO sio;

    // Initially empty
    EXPECT_TRUE(sio.ioRead(1) & 0x04);

    // Write TX byte — buffer full
    sio.ioWrite(0, 0x99);
    EXPECT_FALSE(sio.ioRead(1) & 0x04);

    // External reads byte — buffer empty again
    sio.channelA().txGet();
    EXPECT_TRUE(sio.ioRead(1) & 0x04);
}

// ─── Channel B data / control ─────────────────────────────────────────────────

/**
 * @test Z80SIO/ChannelB_RX_TX
 * @brief Channel B works symmetrically: rxByte() data readable via ioRead(2); ioWrite(2) puts TX data in Ch B.
 * @par Pass criterion  ioRead(2) == 0xCD after rxByte(0xCD); chb.txGet() == 0xEF after ioWrite(2, 0xEF).
 */
TEST(Z80SIO, ChannelB_RX_TX) {
    Z80SIO sio;
    auto& chb = sio.channelB();

    chb.rxByte(0xCD);
    EXPECT_EQ(sio.ioRead(2), 0xCD);

    sio.ioWrite(2, 0xEF);
    EXPECT_TRUE(chb.txAvailable());
    EXPECT_EQ(chb.txGet(), 0xEF);
}

// ─── debugState() — read-only snapshot for the debugger's `dev sio` command ─────

/**
 * @brief debugState reflects per-channel rr0/rx-queue/tx-busy/vector + daisy-chain.
 */
TEST(Z80SIO, DebugStateReflectsChannelBasics) {
    Z80SIO sio;
    sio.setIEI(true);
    sio.channelA().rxByte(0xAB);          // RX FIFO: 1 byte queued, RR0 bit0 set
    sio.ioWrite(0, 0xEF);                 // CPU writes Ch A data → TX buffer busy
    sio.ioWrite(3, 0x02);                 // Ch B WR0: point to WR2 (vector)
    sio.ioWrite(3, 0x40);                 // WR2 = interrupt vector base

    auto d = sio.debugState();
    EXPECT_TRUE(d.iei);                    // device IEI
    EXPECT_TRUE(d.ieo);                    // no interrupt pending yet → chain open
    // Channel A
    EXPECT_EQ(d.ch[0].rr0 & 0x01, 0x01);  // Rx Character Available
    EXPECT_EQ(d.ch[0].rxQueued, 1u);
    EXPECT_TRUE(d.ch[0].txBusy);          // tx_buf set, not yet consumed
    EXPECT_TRUE(d.ch[0].iei);             // IEI propagated to Ch A (highest priority)
    EXPECT_FALSE(d.ch[0].irqRx);          // RX int mode disabled by default
    // Channel B holds the programmed vector (WR2)
    EXPECT_EQ(d.ch[1].wr2, 0x40);
}

/**
 * @brief When TX interrupt fires, debugState shows irqTx and the chain blocks (IEO low).
 */
TEST(Z80SIO, DebugStateReflectsTxInterrupt) {
    Z80SIO sio;
    sio.setIEI(true);
    sio.ioWrite(1, 0x01);                 // Ch A WR0: point to WR1
    sio.ioWrite(1, 0x02);                 // WR1: Tx interrupt enable
    sio.ioWrite(0, 0x55);                 // fill TX buffer
    EXPECT_FALSE(sio.debugState().ch[0].irqTx);
    sio.channelA().txGet();               // consume → TX-empty interrupt fires

    auto d = sio.debugState();
    EXPECT_TRUE(d.ch[0].irqTx);           // TX interrupt pending
    EXPECT_FALSE(d.ch[0].txBusy);         // buffer consumed
    EXPECT_FALSE(d.ieo);                  // pending interrupt blocks downstream
}
