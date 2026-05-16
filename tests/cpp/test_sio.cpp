#include <gtest/gtest.h>
#include "core/primitives/z80_sio.h"

// ─── RX path ─────────────────────────────────────────────────────────────────

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

TEST(Z80SIO, IEI_IEO_PassThrough_NoInterrupt) {
    Z80SIO sio;
    sio.setIEI(true);

    // No pending interrupt → IEO should pass through (true)
    EXPECT_TRUE(sio.getIEO());
}

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

TEST(Z80SIO, ChannelB_RX_TX) {
    Z80SIO sio;
    auto& chb = sio.channelB();

    chb.rxByte(0xCD);
    EXPECT_EQ(sio.ioRead(2), 0xCD);

    sio.ioWrite(2, 0xEF);
    EXPECT_TRUE(chb.txAvailable());
    EXPECT_EQ(chb.txGet(), 0xEF);
}
