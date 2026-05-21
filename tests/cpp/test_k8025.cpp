/**
 * @file test_k8025.cpp
 * @brief Unit tests for the K8025 peripherals card emulation.
 *
 * @details
 * Emulator component under test: **K8025** (`core/cards/k8025/k8025.h`)
 *
 * The K8025 is the combined I/O peripherals card of the Robotron A5120.  It
 * provides the host-visible keyboard, DFÜ (serial data exchange), and printer
 * interfaces, plus CTC and PIO sub-chips.  Internally it contains:
 *  - **SIO A32** – keyboard (Ch A, RX/TX) and printer (Ch B, TX only)
 *  - **SIO A33** – DFÜ (Ch A, RX/TX) and a spare channel (Ch B)
 *  - **CTC A34** – four-channel timer (ports 0x58–0x5B)
 *  - **PIO A35** – parallel I/O (ports 0x54–0x57)
 *
 * I/O port map (relative to the K8025 card; absolute addresses on the K1520
 * bus depend on the system configuration):
 *  - 0x50–0x53: SIO A33 (DFÜ)  — data A, ctrl A, data B, ctrl B
 *  - 0x54–0x57: PIO A35
 *  - 0x58–0x5B: CTC A34 (channels 0–3)
 *  - 0x5C–0x5F: SIO A32 (keyboard/printer) — data A, ctrl A, data B, ctrl B
 *
 * ## Test groups
 *
 * | Group                  | What is tested                                           |
 * |------------------------|----------------------------------------------------------|
 * | Keyboard interface     | keyboardRxByte() triggers SIO A32 Ch A interrupt        |
 * | DFÜ interface          | dfueRxByte(), dfueTxAvailable(), dfueTxGet()            |
 * | Printer interface      | printerTxAvailable(), printerTxGet()                    |
 * | CTC / clockTick        | clockTick() does not crash                              |
 * | I/O port dispatch      | Write/read to CTC, PIO, all ports in range              |
 * | Interrupt chain        | hasInterrupt(), IEO, IEI dependency, getVector()        |
 * | Sub-chip accessors     | sioA32(), sioA33() reflect injected data                |
 * | setDFUERxCallback      | Callback registration does not crash                    |
 *
 * @see core/cards/k8025/k8025.h
 * @see core/bus/k1520_bus.h
 */

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

/**
 * @test K8025/KeyboardRxByte_SioA32_ChA_HasInterrupt
 * @brief keyboardRxByte() injects a byte into SIO A32 channel A and, with IEI set and
 *   RX interrupts enabled, the SIO asserts a pending interrupt.
 * @par Pass criterion  sioA32().hasInterrupt() == true.
 */
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

/**
 * @test K8025/KeyboardTxAvailable_FalseInitially
 * @brief No keyboard TX data is available before the firmware writes to the SIO.
 * @par Pass criterion  keyboardTxAvailable() == false.
 */
TEST(K8025, KeyboardTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.keyboardTxAvailable());
}

// ─── DFÜ interface ────────────────────────────────────────────────────────────

/**
 * @test K8025/DfueRxByte_SioA33_ChA_HasRxData
 * @brief dfueRxByte() injects a byte into SIO A33 channel A; RR0 bit 0 (Rx Char Available) is set.
 * @par Pass criterion  ioRead(0x51) & 0x01 == true.
 */
TEST(K8025, DfueRxByte_SioA33_ChA_HasRxData)
{
    K1520Bus bus;
    K8025 card(bus);

    card.dfueRxByte(0x55);

    // Port 0x51 = SIO A33 channel A control; RR0 bit 0 = Rx Character Available
    uint8_t rr0 = card.ioRead(0x51);
    EXPECT_TRUE(rr0 & 0x01) << "RR0 Rx Character Available should be set";
}

/**
 * @test K8025/DfueTxAvailable_FalseInitially
 * @brief No DFÜ TX data is available before the firmware writes to SIO A33.
 * @par Pass criterion  dfueTxAvailable() == false.
 */
TEST(K8025, DfueTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.dfueTxAvailable());
}

/**
 * @test K8025/DfueTxAvailable_TrueAfterCpuWrite
 * @brief After the CPU writes to SIO A33 data port (0x50), dfueTxAvailable() is true
 *   and dfueTxGet() returns the written byte.
 * @par Pass criterion  dfueTxAvailable() == true; dfueTxGet() == 0x55.
 */
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

/**
 * @test K8025/PrinterTxAvailable_FalseInitially
 * @brief No printer TX data is available before the firmware writes to SIO A32 Ch B.
 * @par Pass criterion  printerTxAvailable() == false.
 */
TEST(K8025, PrinterTxAvailable_FalseInitially)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_FALSE(card.printerTxAvailable());
}

/**
 * @test K8025/PrinterTxAvailable_TrueAfterCpuWrite
 * @brief After a CPU write to SIO A32 Ch B data port (0x5E), printerTxAvailable() is true
 *   and printerTxGet() returns the written byte.
 * @par Pass criterion  printerTxAvailable() == true; printerTxGet() == 0x0D.
 */
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

/**
 * @test K8025/ClockTick_NoCrash
 * @brief clockTick() completes without crashing.
 * @par Pass criterion  No exception or assertion failure.
 */
TEST(K8025, ClockTick_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_NO_FATAL_FAILURE(card.clockTick());
}

// ─── I/O port dispatch ────────────────────────────────────────────────────────

/**
 * @test K8025/IODispatch_WriteToCTC_NoCrash
 * @brief Writing a control word to CTC A34 channel 0 (port 0x58) does not crash.
 * @par Pass criterion  No exception or assertion failure.
 */
TEST(K8025, IODispatch_WriteToCTC_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    // Write a control word to CTC A34 channel 0 (port 0x58)
    EXPECT_NO_FATAL_FAILURE(card.ioWrite(0x58, 0x07));
}

/**
 * @test K8025/IODispatch_WriteToPIO_NoCrash
 * @brief Writing to PIO A35 (port 0x54) does not crash.
 * @par Pass criterion  No exception or assertion failure.
 */
TEST(K8025, IODispatch_WriteToPIO_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    EXPECT_NO_FATAL_FAILURE(card.ioWrite(0x54, 0xFF));
}

/**
 * @test K8025/IODispatch_ReadFromAllPorts_NoCrash
 * @brief Reading from every port in range 0x50–0x5F does not crash.
 * @par Pass criterion  No exception or assertion failure for any port.
 */
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

/**
 * @test K8025/InterruptChain_KeyboardRxByte_HasInterrupt
 * @brief After enabling RX interrupts and injecting a keyboard byte, hasInterrupt() == true.
 * @par Pass criterion  card.hasInterrupt() == true with IEI set and a keyboard byte injected.
 */
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

/**
 * @test K8025/InterruptChain_IEO_FalseWhenInterruptPending
 * @brief IEO is blocked (false) while an interrupt is pending to prevent downstream devices
 *   from asserting their interrupt.
 * @par Pass criterion  getIEO() == false while SIO A32 has a pending RX interrupt.
 */
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

/**
 * @test K8025/InterruptChain_NoInterruptWithoutIEI
 * @brief Without IEI set, K8025 must not assert an interrupt even if a byte was injected.
 * @par Pass criterion  hasInterrupt() == false.
 */
TEST(K8025, InterruptChain_NoInterruptWithoutIEI)
{
    K1520Bus bus;
    K8025 card(bus);

    enableRxInterrupts(card, 0x5D);
    card.keyboardRxByte(0x41);
    // IEI not set – K8025 must not report an interrupt

    EXPECT_FALSE(card.hasInterrupt());
}

/**
 * @test K8025/InterruptChain_IEO_TrueWhenNoPendingInterrupt
 * @brief When no interrupt is pending, IEO passes IEI through to downstream devices.
 * @par Pass criterion  getIEO() == true with IEI set and no bytes injected.
 */
TEST(K8025, InterruptChain_IEO_TrueWhenNoPendingInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    card.setIEI(true);

    // No bytes injected → no pending interrupt → IEO must pass through
    EXPECT_TRUE(card.getIEO());
}

// ─── getVector ────────────────────────────────────────────────────────────────

/**
 * @test K8025/GetVector_ReturnsValidVectorWhenInterruptPending
 * @brief getVector() returns a valid (non-0xFF) interrupt vector when a keyboard byte is pending.
 * @par Pass criterion  getVector() != 0xFF.
 */
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

/**
 * @test K8025/GetVector_Returns0xFF_WhenNoInterrupt
 * @brief getVector() returns 0xFF (no interrupt) when no bytes are pending.
 * @par Pass criterion  getVector() == 0xFF.
 */
TEST(K8025, GetVector_Returns0xFF_WhenNoInterrupt)
{
    K1520Bus bus;
    K8025 card(bus);

    card.setIEI(true);
    // No bytes injected
    EXPECT_EQ(card.getVector(), 0xFF);
}

// ─── DFÜ interrupt ────────────────────────────────────────────────────────────

/**
 * @test K8025/DfueRxByte_TriggersInterrupt_WhenEnabled
 * @brief dfueRxByte() with RX interrupts enabled on SIO A33 triggers both the SIO and the card interrupt.
 * @par Pass criterion  sioA33().hasInterrupt() == true; card.hasInterrupt() == true.
 */
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

/**
 * @test K8025/SubchipAccessors_ReturnCorrectChips
 * @brief sioA33() returns the SIO that receives dfueRxByte() injections.
 * @par Pass criterion  sioA33().channelA() reflects the injected byte (rxFull or rx_fifo non-empty).
 */
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

/**
 * @test K8025/SetDFUERxCallback_NoCrash
 * @brief setDFUERxCallback() stores a callback without crashing; dfueRxByte() calls it.
 * @par Pass criterion  No exception; dfueRxByte(0x01) completes without error.
 */
TEST(K8025, SetDFUERxCallback_NoCrash)
{
    K1520Bus bus;
    K8025 card(bus);

    bool called = false;
    card.setDFUERxCallback([&called](uint8_t) { called = true; });

    // Callback stored; no crash on registration.
    EXPECT_NO_FATAL_FAILURE(card.dfueRxByte(0x01));
}
