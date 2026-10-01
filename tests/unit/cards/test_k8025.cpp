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
#include <vector>
#include "core/cards/k8025/k8025.h"
#include "core/serial/wandler.h"

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

/**
 * @test K8025/RetiGibtDenSioWiederFrei
 * @brief Nach Quittung (IUS gesetzt) fordert die DFÜ-SIO erst wieder an, wenn das RETI
 *   über die Karte bei ihr ankommt — bis AP-ST5 reichte die K8025 das RETI nicht weiter,
 *   jeder zweite Empfangsinterrupt blieb aus (SERTEST-Gegenstelle am A5120).
 * @par Pass criterion  zweites Zeichen ohne RETI: keine Anforderung; nach
 *   `bus.signalRETI()`: Anforderung, Vektor erneut quittierbar.
 */
TEST(K8025, RetiGibtDenSioWiederFrei)
{
    K1520Bus bus;
    K8025 card(bus);
    bus.setInterruptChain({&card});
    card.ioWrite(0x51, 0x01);   // WR1: Empfangsinterrupt bei JEDEM Zeichen (D4–D3 = 10,
    card.ioWrite(0x51, 0x10);   // wie SERTEST; enableRxInterrupts' 0CH ist „erstes Zeichen")
    card.setIEI(true);

    card.dfueRxByte(0x41);
    ASSERT_TRUE(card.hasInterrupt());
    (void)card.getVector();                 // Quittung: IUS
    (void)card.ioRead(0x50);                // Zeichen abholen
    card.dfueRxByte(0x42);
    card.setIEI(true);
    EXPECT_FALSE(card.hasInterrupt()) << "unter Bedienung fordert der Kanal nicht an";

    bus.signalRETI();
    card.setIEI(true);
    EXPECT_TRUE(card.hasInterrupt()) << "RETI muss bei der SIO A33 ankommen";
    (void)card.getVector();
    EXPECT_EQ(card.ioRead(0x50), 0x42);
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

// ─── A5120Config: DIL-Schalter A41 ───────────────────────────────────────────

/**
 * @test K8025/Config_DilA41_DefaultIs0xAE
 * @brief Default A5120Config has dil_a41 == 0xAE (9600 Baud, 1024B blocks).
 * @par Pass criterion  A5120Config{}.dil_a41 == 0xAE.
 */
TEST(K8025, Config_DilA41_DefaultIs0xAE)
{
    K8025::A5120Config cfg;
    EXPECT_EQ(cfg.dil_a41, 0xAE);
}

/**
 * @test K8025/PIOPortA_ReturnsA41Value
 * @brief Reading port 0x54 (Register A31 Port A) returns the A41 DIP switch value (0xAE).
 * @details The BIOS reads port 0x54 to determine baud rate (9600) and block size (1024B).
 * @par Pass criterion  ioRead(0x54) == 0xAE.
 */
TEST(K8025, PIOPortA_ReturnsA41Value)
{
    K1520Bus bus;
    K8025 card(bus);
    EXPECT_EQ(card.ioRead(0x54), 0xAE);
}

/**
 * @test K8025/Config_CustomDilA41_IsReturned
 * @brief A custom dil_a41 value in A5120Config is returned when reading port 0x54.
 * @par Pass criterion  ioRead(0x54) == custom value.
 */
TEST(K8025, Config_CustomDilA41_IsReturned)
{
    K1520Bus bus;
    K8025::A5120Config cfg;
    cfg.dil_a41 = 0x12;
    K8025 card(bus, cfg);
    EXPECT_EQ(card.ioRead(0x54), 0x12);
}

// ─── setDFUERxCallback ────────────────────────────────────────────────────────

/**
 * @test K8025/SetDFUERxCallback_NoCrash
 * @brief setDFUERxCallback() stores a callback without crashing (seit AP-S5 = Abnehmer
 *        der DFÜ/V.24, s. K8025Seriell.AlterUnterbau).
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

// ─── K8025Seriell: Anschlüsse nach außen (Entwurf 19 §3.1, AP-S5) ───────────

namespace {

/// Kanal @p ctrl (SIO-Steuerport) auf 8N1 ×16, Tx/Rx ein.
void sio8N1(K8025& card, uint8_t ctrl) {
    for (uint8_t b : {0x18, 0x04, 0x44, 0x03, 0xC1, 0x05, 0x68}) card.ioWrite(ctrl, b);
}

}  // namespace

/**
 * @test K8025Seriell.Kanalzuordnung
 * @brief DFÜ/V.24 = A33-A (X6, mit Steuerleitungen), DFÜ/IFSS = A33-B (X5, bis AP-S5
 *        als „unused" geführt), Drucker = A32-B (X3); die Tastatur (A32-A, X4) ist
 *        fest und hat keinen Anschluss.  Taktquellen: W1:7 bzw. X7–X9, Drucker fest.
 */
TEST(K8025Seriell, Kanalzuordnung)
{
    K1520Bus bus;
    K8025 card(bus);
    auto& v24  = card.anschluss(K8025::DfueV24);
    auto& ifss = card.anschluss(K8025::DfueIfss);
    auto& dr   = card.anschluss(K8025::Drucker);
    EXPECT_STREQ(v24.name(), "DFÜ/V.24");
    EXPECT_STREQ(v24.stecker(), "X6");
    EXPECT_TRUE(v24.v24());
    EXPECT_STREQ(ifss.name(), "DFÜ/IFSS");
    EXPECT_STREQ(ifss.stecker(), "X5");
    EXPECT_FALSE(ifss.v24());
    EXPECT_STREQ(dr.name(), "Drucker");
    EXPECT_STREQ(dr.stecker(), "X3");
    EXPECT_FALSE(dr.v24());
    EXPECT_STREQ(K8025::TASTATUR_NAME, "Tastatur K7637 (X4)");
    ASSERT_EQ(v24.taktquellen().size(), 2u);
    EXPECT_EQ(v24.taktquellen()[0].name, "ZRE-CTC K0 (W1:7)");
    ASSERT_EQ(ifss.taktquellen().size(), 2u);
    EXPECT_EQ(ifss.taktquellen()[1].name, "CTC A34 K1 (X8–X9)");
    EXPECT_TRUE(dr.taktquellen().empty());

    sio8N1(card, 0x51);
    sio8N1(card, 0x53);
    sio8N1(card, 0x5F);
    card.ioWrite(0x50, 0x31);
    card.ioWrite(0x52, 0x32);
    card.ioWrite(0x5E, 0x33);
    EXPECT_EQ(v24.senderNimm(), 0x31);
    EXPECT_EQ(ifss.senderNimm(), 0x32);
    EXPECT_EQ(dr.senderNimm(), 0x33);
    ifss.empfange(0x44);
    EXPECT_EQ(card.ioRead(0x52), 0x44);
    dr.empfange(0x45);
    EXPECT_EQ(card.ioRead(0x5E), 0x45);
    EXPECT_TRUE(card.nimmSeriellGeaendert());
}

/**
 * @test K8025Seriell.Taktquelle
 * @brief Gezeichnete Stellung = ZRE-CTC K0 (über `setzeZreTakt`), versetzt = CTC A34
 *        K2 (V.24) bzw. K1 (IFSS); Drucker fest CTC A34 K0.
 */
TEST(K8025Seriell, Taktquelle)
{
    K1520Bus bus;
    K8025 card(bus);
    uint64_t zre = 32;   // ZRE-CTC K0: 32 Takte je Impuls → ×16 = 4800 Bd
    card.setzeZreTakt([&] { return zre; });
    sio8N1(card, 0x51);
    sio8N1(card, 0x53);
    sio8N1(card, 0x5F);
    auto& v24  = card.anschluss(K8025::DfueV24);
    auto& ifss = card.anschluss(K8025::DfueIfss);
    auto& dr   = card.anschluss(K8025::Drucker);
    EXPECT_EQ(v24.format().baud_nenn, 4800u);
    EXPECT_EQ(ifss.format().baud_nenn, 4800u);
    EXPECT_FALSE(dr.format().gueltig) << "CTC A34 K0 noch nicht programmiert";

    // CTC A34 als Zeitgeber (Vorteiler 16): K0 ZK 1 = 9600, K1 ZK 4 = 2400, K2 ZK 8 = 1200.
    for (auto [port, zk] : {std::pair<uint8_t, uint8_t>{0x58, 1}, {0x59, 4}, {0x5A, 8}}) {
        card.ioWrite(port, 0x07);
        card.ioWrite(port, zk);
    }
    EXPECT_EQ(dr.format().baud_nenn, 9600u);
    v24.waehleTaktquelle(1);
    ifss.waehleTaktquelle(1);
    EXPECT_EQ(v24.format().baud_nenn, 1200u);
    EXPECT_EQ(ifss.format().baud_nenn, 2400u);
    v24.waehleTaktquelle(0);
    zre = 16;
    EXPECT_EQ(v24.format().baud_nenn, 9600u);

    // Die CTC A34 zählt im Emulator ZC/TO0 der ZRE (Koppelbus): ihre Eingangsperiode
    // kommt aus derselben Quelle.  Zähler (47H), ZK 2 → 2 × 16 Takte.
    card.ioWrite(0x58, 0x47);
    card.ioWrite(0x58, 2);
    EXPECT_EQ(card.ctcA34().teilerTakte(0), 32u);
}

/**
 * @test K8025Seriell.V24Verknuepfungen
 * @brief Kartenlogik A23 (Transkription §2.3.2): /CTSA = V106 ∧ V107, /DCDA = V109 ∧
 *        V107, V107 zusätzlich allein an DCDB; RTSA/DTRA aus WR5.  Der IFSS-Anschluss
 *        treibt keine Leitungen.
 */
TEST(K8025Seriell, V24Verknuepfungen)
{
    K1520Bus bus;
    K8025 card(bus);
    auto& v24 = card.anschluss(K8025::DfueV24);
    auto rr0 = [&](uint8_t ctrl) { card.ioWrite(ctrl, 0x00); return card.ioRead(ctrl); };
    EXPECT_EQ(rr0(0x51) & 0x28, 0x00);
    EXPECT_EQ(rr0(0x53) & 0x28, 0x00);

    v24.setzeEingaenge(/*cts*/ true, /*dsr*/ false, /*dcd*/ true);
    EXPECT_EQ(rr0(0x51) & 0x28, 0x00) << "ohne V107 weder CTSA noch DCDA";
    EXPECT_EQ(rr0(0x53) & 0x08, 0x00);
    v24.setzeEingaenge(true, true, false);
    EXPECT_EQ(rr0(0x51) & 0x28, 0x20) << "CTSA = V106 ∧ V107";
    EXPECT_EQ(rr0(0x53) & 0x08, 0x08) << "DCDB = V107";
    v24.setzeEingaenge(false, true, true);
    EXPECT_EQ(rr0(0x51) & 0x28, 0x08) << "DCDA = V109 ∧ V107";

    card.anschluss(K8025::DfueIfss).setzeEingaenge(true, true, true);
    card.anschluss(K8025::Drucker).setzeEingaenge(true, true, true);
    EXPECT_EQ(rr0(0x53) & 0x20, 0x00) << "CTSB bleibt inaktiv";
    EXPECT_EQ(rr0(0x5F) & 0x28, 0x00) << "A32-B ohne Steuerleitungen";
    EXPECT_EQ(rr0(0x5D) & 0x28, 0x00) << "Tastaturkanal unberührt";

    EXPECT_FALSE(v24.rts());
    card.ioWrite(0x51, 0x05);
    card.ioWrite(0x51, 0xEA);
    EXPECT_TRUE(v24.rts());
    EXPECT_TRUE(v24.dtr());
}

/**
 * @test K8025Seriell.TastaturUnberuehrt
 * @brief Wandler an allen drei Anschlüssen (auch mit Loop) fassen den Tastaturkanal
 *        A32-A nie an: ein LED-Kommando des BIOS bleibt für die K7637 liegen.
 */
TEST(K8025Seriell, TastaturUnberuehrt)
{
    K1520Bus bus;
    K8025 card(bus);
    k1520::serial::Wandler w0(card.anschluss(K8025::DfueV24));
    k1520::serial::Wandler w1(card.anschluss(K8025::DfueIfss));
    k1520::serial::Wandler w2(card.anschluss(K8025::Drucker));
    k1520::serial::WandlerEinstellung e;
    e.loop = true;
    w0.einstellen(e);
    w1.einstellen(e);
    w2.einstellen(e);
    card.ioWrite(0x5C, 0x8F);
    for (uint64_t z = 0; z < 200'000; z += 16) { w0.takt(z); w1.takt(z); w2.takt(z); }
    EXPECT_TRUE(card.keyboardTxAvailable());
    EXPECT_EQ(card.keyboardTxGet(), 0x8F);
}

/**
 * @test K8025Seriell.AlterUnterbau
 * @brief `setAbnehmer`/`einspeisen` (hinter `K1520Machine::setDFUECallback`/`dfueSend`,
 *        `setPrinterCallback`/`printerSend`): das Byte kommt über den Wandler in seiner
 *        Zeichenzeit; belegt ein Transport den Stecker, schweigen beide.
 */
TEST(K8025Seriell, AlterUnterbau)
{
    K1520Bus bus;
    K8025 card(bus);
    card.setzeZreTakt([] { return uint64_t{16}; });
    sio8N1(card, 0x51);
    std::vector<uint8_t> ab;
    card.setDFUERxCallback([&](uint8_t b) { ab.push_back(b); });
    k1520::serial::Wandler w(card.anschluss(K8025::DfueV24));
    uint64_t z = 0;
    auto laufe = [&](uint64_t n) { for (const uint64_t e = z + n; z < e; z += 16) w.takt(z); };
    laufe(64);
    card.ioWrite(0x50, 0x41);
    laufe(10);
    EXPECT_TRUE(ab.empty()) << "erst nach dem Blick des Wandlers";
    laufe(3000);
    EXPECT_EQ(ab, std::vector<uint8_t>{0x41});
    card.einspeisen(K8025::DfueV24, 0x55);
    EXPECT_EQ(card.ioRead(0x50), 0x55);

    w.anbinden();
    laufe(64);
    card.ioWrite(0x50, 0x42);
    laufe(3000);
    EXPECT_EQ(ab.size(), 1u) << "Transport angebunden: Rückruf ins Leere";
    card.einspeisen(K8025::DfueV24, 0x66);
    EXPECT_EQ(card.ioRead(0x50), 0x55) << "Einspeisen ins Leere";
}

/**
 * @test K8025Seriell.BreakInBeideRichtungen
 * @brief Break des Gastes (WR5 D4) erscheint am Anschluss; ein Break vom Wandler setzt
 *        RR0 D7 des richtigen Kanals (AP-T1b).
 */
TEST(K8025Seriell, BreakInBeideRichtungen)
{
    K1520Bus bus;
    K8025 card(bus);
    auto rr0 = [&](uint8_t ctrl) { card.ioWrite(ctrl, 0x10); return card.ioRead(ctrl); };
    struct Fall { K8025::Schnittstelle k; uint8_t ctrl; };
    for (Fall f : {Fall{K8025::DfueV24, 0x51}, Fall{K8025::DfueIfss, 0x53},
                   Fall{K8025::Drucker, 0x5F}}) {
        auto& an = card.anschluss(f.k);
        EXPECT_FALSE(an.breakGesendet());
        card.ioWrite(f.ctrl, 0x05);
        card.ioWrite(f.ctrl, 0x78);   // WR5: Tx ein, 8 Bit, Break
        EXPECT_TRUE(an.breakGesendet()) << int(f.ctrl);
        card.ioWrite(f.ctrl, 0x05);
        card.ioWrite(f.ctrl, 0x68);
        EXPECT_FALSE(an.breakGesendet());

        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x00);
        an.breakEmpfang(true);
        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x80) << int(f.ctrl);
        an.breakEmpfang(false);
        EXPECT_EQ(rr0(f.ctrl) & 0x80, 0x00);
    }
    EXPECT_EQ(rr0(0x5D) & 0x80, 0x00) << "Tastaturkanal unberührt";
}
