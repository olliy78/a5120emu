#include <gtest/gtest.h>
#include "core/cards/k8025/k8025.h"

// Helper: enable RX interrupts on a SIO channel by writing WR1.
// Writes 0x01 to ctrl port (select register 1), then 0x0C (all-receive mode).
static void enableRxInterrupts(K8025& card, uint8_t ctrl_port)
{
    card.ioWrite(ctrl_port, 0x01);  // select WR1
    card.ioWrite(ctrl_port, 0x0C);  // WR1: interrupt on all received characters
}

// ─── Keyboard interface ───────────────────────────────────────────────────────

TEST(K8025, KeyboardRxByte_SioA32_ChA_HasInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    // Enable RX interrupts on SIO A32 channel A (keyboard, ctrl port = 0x5D)
    enableRxInterrupts(card, 0x5D);

    card.setIEI(true);
    card.keyboardRxByte(0x41);  // inject 'A'

    EXPECT_TRUE(card.sioA32().hasInterrupt());
}

TEST(K8025, KeyboardTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.keyboardTxAvailable());
}

// ─── DFÜ interface ────────────────────────────────────────────────────────────

TEST(K8025, DfueRxByte_SioA33_ChA_HasRxData)
{
    K1520Bus bus;
    K8025 card(bus);

    card.dfueRxByte(0x55);

    // Port 0x51 = SIO A33 channel A control; RR0 bit 0 = Rx Character Available
    uint8_t rr0 = card.ioRead(0x51);
    EXPECT_TRUE(rr0 & 0x01) << "RR0 Rx Character Available should be set";
}

TEST(K8025, DfueTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.dfueTxAvailable());
}

TEST(K8025, DfueTxAvailable_TrueAfterCpuWrite)
{
    K1520Bus bus;
    K8025 card(bus);

    // CPU writes a byte to SIO A33 channel A data port (0x50)
    card.ioWrite(0x50, 0x55);

    EXPECT_TRUE(card.dfueTxAvailable());
    EXPECT_EQ(card.dfueTxGet(), 0x55);
}

// ─── Printer interface ────────────────────────────────────────────────────────

TEST(K8025, PrinterTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.printerTxAvailable());
}

TEST(K8025, PrinterTxAvailable_TrueAfterCpuWrite)
{
    K1520Bus bus;
    K8025 card(bus);

    // CPU writes to SIO A32 channel B data port (0x5E = 0x5C base + 2)
    card.ioWrite(0x5E, 0x0D);

    EXPECT_TRUE(card.printerTxAvailable());
    EXPECT_EQ(card.printerTxGet(), 0x0D);
}

// ─── CTC ─────────────────────────────────────────────────────────────────────

TEST(K8025, ClockTick_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_NO_FATAL_FAILURE(card.clockTick());
}

// ─── I/O port dispatch ────────────────────────────────────────────────────────

TEST(K8025, IODispatch_WriteToCTC_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    // Write a control word to CTC A34 channel 0 (port 0x58)
    EXPECT_NO_FATAL_FAILURE(card.ioWrite(0x58, 0x07));
}

TEST(K8025, IODispatch_WriteToPIO_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_NO_FATAL_FAILURE(card.ioWrite(0x54, 0xFF));
}

TEST(K8025, IODispatch_ReadFromAllPorts_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    for (uint8_t port = 0x50; port <= 0x5F; ++port) {
        EXPECT_NO_FATAL_FAILURE(card.ioRead(port))
            << "ioRead failed at port " << std::hex << static_cast<int>(port);
    }
}

// ─── Interrupt chain ─────────────────────────────────────────────────────────

TEST(K8025, InterruptChain_KeyboardRxByte_HasInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    // Enable RX interrupts on SIO A32 channel A (keyboard)
    enableRxInterrupts(card, 0x5D);

    card.keyboardRxByte(0x41);
    card.setIEI(true);

    EXPECT_TRUE(card.hasInterrupt());
}

TEST(K8025, InterruptChain_IEO_FalseWhenInterruptPending)
{
    K1520Bus bus;
    K8025 card(bus);

    // Enable RX interrupts on SIO A32 channel A (keyboard)
    enableRxInterrupts(card, 0x5D);

    card.setIEI(true);
    card.keyboardRxByte(0x41);

    // IEO must be blocked while an interrupt is pending
    EXPECT_FALSE(card.getIEO());
}

TEST(K8025, InterruptChain_NoInterruptWithoutIEI)
{
    K1520Bus bus;
    K8025 card(bus);

    enableRxInterrupts(card, 0x5D);
    card.keyboardRxByte(0x41);
    // IEI not set – K8025 must not report an interrupt

    EXPECT_FALSE(card.hasInterrupt());
}

TEST(K8025, InterruptChain_IEO_TrueWhenNoPendingInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    card.setIEI(true);

    // No bytes injected → no pending interrupt → IEO must pass through
    EXPECT_TRUE(card.getIEO());
}

// ─── getVector ────────────────────────────────────────────────────────────────

TEST(K8025, GetVector_ReturnsValidVectorWhenInterruptPending)
{
    K1520Bus bus;
    K8025 card(bus);

    enableRxInterrupts(card, 0x5D);
    card.setIEI(true);
    card.keyboardRxByte(0x41);

    uint8_t vec = card.getVector();
    EXPECT_NE(vec, 0xFF);
}

TEST(K8025, GetVector_Returns0xFF_WhenNoInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    card.setIEI(true);
    // No bytes injected
    EXPECT_EQ(card.getVector(), 0xFF);
}

// ─── DFÜ interrupt ────────────────────────────────────────────────────────────

TEST(K8025, DfueRxByte_TriggersInterrupt_WhenEnabled)
{
    K1520Bus bus;
    K8025 card(bus);

    // Enable RX interrupts on SIO A33 channel A (DFÜ, ctrl port = 0x51)
    enableRxInterrupts(card, 0x51);

    card.setIEI(true);
    card.dfueRxByte(0x55);

    EXPECT_TRUE(card.sioA33().hasInterrupt());
    EXPECT_TRUE(card.hasInterrupt());
}

// ─── Sub-chip accessor ────────────────────────────────────────────────────────

TEST(K8025, SubchipAccessors_ReturnCorrectChips)
{
    K1520Bus bus;
    K8025 card(bus);

    // Writing to SIO A33 via the card and reading via the accessor must be
    // consistent: inject a byte and verify the accessor reflects it.
    card.dfueRxByte(0xAB);
    EXPECT_TRUE(card.sioA33().channelA().rxFull() ||
                !card.sioA33().channelA().rx_fifo.empty());
}

// ─── setDFUERxCallback ────────────────────────────────────────────────────────

TEST(K8025, SetDFUERxCallback_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    bool called = false;
    card.setDFUERxCallback([&called](uint8_t) { called = true; });

    // Callback stored; no crash on registration.
    EXPECT_NO_FATAL_FAILURE(card.dfueRxByte(0x01));
}
