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
 * @test Z80SIO.LeererEmpfaengerLiefertLetztesByte
 * @brief Die echte U856/Z80-SIO hat am Datenregister keinen eigenen "leer"-Zustand:
 *        Es ist der Ausgang des Empfangs-FIFO, kein separat abschaltbarer Bustreiber.
 *        Ist der FIFO leer, liefert ein Lesezugriff daher das zuletzt empfangene
 *        Byte, nicht FFH (`doc/design/16_k8915.md` §8a AP-E4c, Befund AP-E2: das
 *        K8915-BIOS liest `LISTST` ohne RR0-Prüfung und erwartet dort das letzte
 *        XON/XOFF-Byte).  Nach Reset (vor dem ersten empfangenen Byte) ist der
 *        Ruhewert 00H — eine Annahme, das Datenblatt macht dazu keine Aussage.
 * @par Pass criterion  Direkt nach Reset liefert ein leerer Kanal 00H; nach dem
 *      Abholen des einzigen empfangenen Bytes liefert ein erneutes Lesen dasselbe
 *      Byte erneut (nicht FFH); das gilt für beide Kanäle unabhängig voneinander.
 */
TEST(Z80SIO, LeererEmpfaengerLiefertLetztesByte) {
    Z80SIO sio;

    // Direkt nach Reset, noch nie ein Byte empfangen.
    EXPECT_EQ(sio.ioRead(0), 0x00);
    EXPECT_EQ(sio.ioRead(2), 0x00);

    sio.channelA().rxByte(0x13);   // XOFF
    EXPECT_EQ(sio.ioRead(0), 0x13);
    // FIFO jetzt leer — ein erneutes Lesen liefert weiter 0x13, nicht FFH.
    EXPECT_EQ(sio.ioRead(0), 0x13);
    EXPECT_EQ(sio.ioRead(0), 0x13);

    sio.channelA().rxByte(0x11);   // XON überschreibt das zuletzt empfangene Byte
    EXPECT_EQ(sio.ioRead(0), 0x11);
    EXPECT_EQ(sio.ioRead(0), 0x11);

    // Kanal B unabhängig davon weiterhin auf seinem eigenen Ruhewert.
    EXPECT_EQ(sio.ioRead(2), 0x00);
    sio.channelB().rxByte(0xAA);
    EXPECT_EQ(sio.ioRead(2), 0xAA);
    EXPECT_EQ(sio.ioRead(2), 0xAA);
    EXPECT_EQ(sio.ioRead(0), 0x11) << "Kanal A unverändert";
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
    sio.ioWrite(3, 0x01); // WR0: select WR1 (Ch B)
    sio.ioWrite(3, 0x04); // WR1 Bit2 = status affects vector

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
    sio.ioWrite(3, 0x01); sio.ioWrite(3, 0x0C); // Ch B WR1 (+ status affects vector)

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

/**
 * @test Z80SIO/StatusAffectsVector_ZilogKodierung
 * @brief Mit WR1 Bit2 (Kanal B) ersetzen die Bits 3…1 die Quelle nach Zilog:
 *        B-Tx 000, B-Ext 001, B-Rx 010, A-Tx 100, A-Ext 101, A-Rx 110.  Das BIOS des
 *        K8915 (SIO2-B, WR1 = 17H, WR2 = D0H) erwartet seine Tx-Routine bei D0H und den
 *        Empfang bei D4H (doc/design/16_k8915.md §4.4, AP-E3).
 */
TEST(Z80SIO, StatusAffectsVector_ZilogKodierung) {
    Z80SIO sio;
    sio.setIEI(true);
    sio.ioWrite(3, 0x02); sio.ioWrite(3, 0xD0);   // WR2 = D0H
    sio.ioWrite(3, 0x01); sio.ioWrite(3, 0x17);   // Ch B WR1 = 17H (Ext, Tx, SAV, Rx alle)

    sio.channelB().rxByte(0x1E);
    EXPECT_EQ(sio.getVector(), 0xD4) << "Kanal B Empfang";
    sio.onRETI();
    sio.setIEI(true);

    sio.ioWrite(2, 0x41);                          // Senden …
    sio.channelB().txGet();                        // … Puffer geleert → Tx-Interrupt
    ASSERT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.getVector(), 0xD0) << "Kanal B Tx-Puffer leer";
}

/**
 * @test Z80SIO/OhneStatusAffectsVector_BleibtDerVektor
 * @brief Ohne WR1 Bit2 liefert die SIO den Vektor aus WR2 unverändert.
 */
TEST(Z80SIO, OhneStatusAffectsVector_BleibtDerVektor) {
    Z80SIO sio;
    sio.setIEI(true);
    sio.ioWrite(3, 0x02); sio.ioWrite(3, 0x60);
    sio.ioWrite(1, 0x01); sio.ioWrite(1, 0x08);   // Ch A: Rx-Interrupt, kein SAV
    sio.channelA().rxByte(0x01);
    EXPECT_EQ(sio.getVector(), 0x60);
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

/**
 * @test SIO/SerializeRoundTrip
 * @brief serialize() → deserialize() restores both channels (registers, RX FIFO,
 *        TX buffer) and the IEI line into a fresh chip. Underpins savestate so a
 *        loadstate resumes with a working keyboard SIO.
 * @par Pass criterion  All distinctive fields match and the blob is fully consumed.
 */
TEST(Z80SIO, SerializeRoundTrip) {
    Z80SIO a;
    a.channelA().rxByte(0x11);
    a.channelA().rxByte(0x22);
    a.channelA().rxByte(0x33);
    a.channelA().wr[1]        = 0x1C;
    a.channelA().rx_enable    = true;
    a.channelA().rx_int_mode  = SIORxIntMode::ALL_CHARS;
    a.channelB().tx_buf       = 0x99;
    a.channelB().wr[2]        = 0x40;
    a.setIEI(true);

    std::vector<uint8_t> blob;
    a.serialize(blob);
    ASSERT_FALSE(blob.empty());

    Z80SIO b;                         // power-on defaults
    const uint8_t* p   = blob.data();
    const uint8_t* end = p + blob.size();
    ASSERT_TRUE(b.deserialize(p, end));
    EXPECT_EQ(p, end);                // consumed exactly

    ASSERT_EQ(b.channelA().rx_fifo.size(), 3u);
    EXPECT_EQ(b.channelA().rx_fifo.front(), 0x11);
    EXPECT_EQ(b.channelA().rx_fifo.back(),  0x33);
    EXPECT_EQ(b.channelA().wr[1], 0x1C);
    EXPECT_TRUE(b.channelA().rx_enable);
    EXPECT_EQ((int)b.channelA().rx_int_mode, (int)SIORxIntMode::ALL_CHARS);
    ASSERT_TRUE(b.channelB().tx_buf.has_value());
    EXPECT_EQ(*b.channelB().tx_buf, 0x99);
    EXPECT_EQ(b.channelB().wr[2], 0x40);
    EXPECT_TRUE(b.debugState().iei);
}

/**
 * @test SIO/DeserializeRejectsTruncatedBlob
 * @brief deserialize() reports failure on a short buffer rather than reading OOB.
 */
TEST(Z80SIO, DeserializeRejectsTruncatedBlob) {
    Z80SIO a;
    std::vector<uint8_t> blob;
    a.serialize(blob);
    blob.resize(blob.size() / 2);     // truncate
    Z80SIO b;
    const uint8_t* p   = blob.data();
    const uint8_t* end = p + blob.size();
    EXPECT_FALSE(b.deserialize(p, end));
}

// ─── AP-S3: Format-/Leitungsabfrage, Auto Enables, Break (doc/design/19 §6.2/§6.4) ──

namespace {
/// Schreibt ein Steuerregister WRn des Kanals (A: Port 1, B: Port 3).
void schreibeWR(Z80SIO& sio, bool kanalB, uint8_t reg, uint8_t wert) {
    const uint8_t port = kanalB ? 3 : 1;
    sio.ioWrite(port, reg);      // WR0: Zeiger
    sio.ioWrite(port, wert);
}
}  // namespace

/**
 * @test Z80SIO/Format_AusWR3WR4WR5
 * @brief Teiler, Stoppbits, Parität, Tx- und Rx-Bits kommen aus den Registern.
 */
TEST(Z80SIO, Format_AusWR3WR4WR5) {
    Z80SIO sio;
    schreibeWR(sio, false, 4, 0x44);     // ×16, 1 Stoppbit, keine Parität
    schreibeWR(sio, false, 3, 0xC1);     // Rx 8 Bit, Rx an
    schreibeWR(sio, false, 5, 0x68);     // Tx 8 Bit, Tx an
    auto f = sio.channelA().format();
    EXPECT_EQ(f.teiler, 16);
    EXPECT_EQ(f.stopp_halbe, 2);
    EXPECT_EQ(f.paritaet, 0);
    EXPECT_EQ(f.tx_bits, 8);
    EXPECT_EQ(f.rx_bits, 8);
    EXPECT_TRUE(f.asynchron());

    schreibeWR(sio, true, 4, 0xCF);      // ×64, 2 Stoppbits, gerade Parität
    schreibeWR(sio, true, 3, 0x40);      // Rx 7 Bit
    schreibeWR(sio, true, 5, 0x40);      // Tx 6 Bit
    f = sio.channelB().format();
    EXPECT_EQ(f.teiler, 64);
    EXPECT_EQ(f.stopp_halbe, 4);
    EXPECT_EQ(f.paritaet, 2);
    EXPECT_EQ(f.tx_bits, 6);
    EXPECT_EQ(f.rx_bits, 7);

    schreibeWR(sio, true, 4, 0x89);      // ×32, 1½ Stoppbits, ungerade Parität
    schreibeWR(sio, true, 3, 0x80);      // Rx 6 Bit
    schreibeWR(sio, true, 5, 0x00);      // Tx 5 Bit
    f = sio.channelB().format();
    EXPECT_EQ(f.teiler, 32);
    EXPECT_EQ(f.stopp_halbe, 3);
    EXPECT_EQ(f.paritaet, 1);
    EXPECT_EQ(f.tx_bits, 5);
    EXPECT_EQ(f.rx_bits, 6);
}

/**
 * @test Z80SIO/Format_NachKanalResetUndSynchron
 * @brief Kanalreset (WR0 Befehl 3) löscht die WRs → ×1, synchron; das Format folgt
 *        den Registern, nicht den abgeleiteten Feldern.
 */
TEST(Z80SIO, Format_NachKanalResetUndSynchron) {
    Z80SIO sio;
    schreibeWR(sio, false, 4, 0x44);
    sio.ioWrite(1, 0x18);                // Kanalreset A
    auto f = sio.channelA().format();
    EXPECT_EQ(f.teiler, 1);
    EXPECT_EQ(f.stopp_halbe, 0);
    EXPECT_FALSE(f.asynchron());
}

/**
 * @test Z80SIO/Format_RtsDtrBreakAusWR5
 * @brief RTS (D1), DTR (D7) und Break senden (D4) sind als Ausgänge abfragbar.
 */
TEST(Z80SIO, Format_RtsDtrBreakAusWR5) {
    Z80SIO sio;
    auto& a = sio.channelA();
    EXPECT_FALSE(a.rts()); EXPECT_FALSE(a.dtr()); EXPECT_FALSE(a.breakSenden());
    schreibeWR(sio, false, 5, 0xEA);     // DTR, Tx 8, Tx an, RTS
    EXPECT_TRUE(a.rts()); EXPECT_TRUE(a.dtr()); EXPECT_FALSE(a.breakSenden());
    schreibeWR(sio, false, 5, 0x78);     // Break, kein RTS/DTR
    EXPECT_FALSE(a.rts()); EXPECT_FALSE(a.dtr()); EXPECT_TRUE(a.breakSenden());
}

/**
 * @test Z80SIO/Leitungen_VorgabeInaktivUndRr0UnveraendertGegenueberFrueher
 * @brief Nach dem Einschalten sind /CTS und /DCD inaktiv — RR0 zeigt wie bisher 04H
 *        (nur „Sendepuffer leer“). Das ist die Vorgabe, auf der alle heutigen Pfade laufen.
 */
TEST(Z80SIO, Leitungen_VorgabeInaktivUndRr0UnveraendertGegenueberFrueher) {
    Z80SIO sio;
    EXPECT_FALSE(sio.channelA().cts());
    EXPECT_FALSE(sio.channelA().dcd());
    EXPECT_EQ(sio.ioRead(1), 0x04);
    EXPECT_EQ(sio.ioRead(3), 0x04);
}

/**
 * @test Z80SIO/Leitungen_OhneExtInterruptFolgtRr0DemEingang
 * @brief Ohne Ext/Status-Freigabe (WR1 D0) zeigen RR0 D5/D3 den Eingang direkt, kein Interrupt.
 */
TEST(Z80SIO, Leitungen_OhneExtInterruptFolgtRr0DemEingang) {
    Z80SIO sio;
    sio.setIEI(true);
    auto& a = sio.channelA();
    a.setzeCTS(true);
    EXPECT_EQ(sio.ioRead(1) & 0x20, 0x20);
    a.setzeDCD(true);
    EXPECT_EQ(sio.ioRead(1) & 0x08, 0x08);
    a.setzeCTS(false);
    EXPECT_EQ(sio.ioRead(1) & 0x28, 0x08);
    EXPECT_FALSE(sio.hasInterrupt());
}

/**
 * @test Z80SIO/Leitungen_ExtStatusInterruptBeiFlankeUndLatch
 * @brief Mit WR1 D0 löst jede Änderung von /CTS oder /DCD einen Ext/Status-Interrupt aus;
 *        RR0 hält den Zustand zur Zeit der Änderung fest, bis „Reset Ext/Status“ (Befehl 2).
 */
TEST(Z80SIO, Leitungen_ExtStatusInterruptBeiFlankeUndLatch) {
    Z80SIO sio;
    sio.setIEI(true);
    schreibeWR(sio, true, 2, 0x40);      // Vektorbasis
    schreibeWR(sio, true, 1, 0x04);      // B: status affects vector
    schreibeWR(sio, false, 1, 0x01);     // A: Ext/Status-Interrupt frei
    auto& a = sio.channelA();

    a.setzeCTS(false);                   // keine Änderung → nichts
    EXPECT_FALSE(sio.hasInterrupt());

    a.setzeCTS(true);                    // Flanke
    EXPECT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.ioRead(1) & 0x20, 0x20);
    a.setzeCTS(false);                   // während des Latch: RR0 bleibt stehen
    EXPECT_EQ(sio.ioRead(1) & 0x20, 0x20);
    EXPECT_EQ(sio.getVector(), 0x4A);    // A Ext/Status = 101

    sio.ioWrite(1, 0x10);                // Reset Ext/Status → Latch frei, aktueller Stand
    EXPECT_EQ(sio.ioRead(1) & 0x20, 0x00);
    sio.onRETI();
    EXPECT_FALSE(sio.hasInterrupt());

    a.setzeDCD(true);                    // DCD-Flanke löst ebenso aus
    EXPECT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.ioRead(1) & 0x08, 0x08);
}

/**
 * @test Z80SIO/Leitungen_KanalresetBehaeltDenEingang
 * @brief Ein Kanalreset löscht Register und Latch, nicht aber den Pegel am Eingang.
 */
TEST(Z80SIO, Leitungen_KanalresetBehaeltDenEingang) {
    Z80SIO sio;
    sio.channelB().setzeCTS(true);
    sio.ioWrite(3, 0x18);                // Kanalreset B
    EXPECT_TRUE(sio.channelB().cts());
    EXPECT_EQ(sio.ioRead(3), 0x24);
}

/**
 * @test Z80SIO/AutoEnables_SenderWartetAufCts
 * @brief WR3 D5: der Sender gibt bei inaktivem /CTS nichts ab; ohne Auto Enables
 *        ist /CTS ein freier Eingang.
 */
TEST(Z80SIO, AutoEnables_SenderWartetAufCts) {
    Z80SIO sio;
    auto& a = sio.channelA();
    sio.ioWrite(0, 0x41);
    EXPECT_TRUE(a.senderHatZeichen());   // ohne Auto Enables: CTS egal
    EXPECT_TRUE(a.txAvailable());

    schreibeWR(sio, false, 3, 0xE1);     // Rx 8, Auto Enables, Rx an
    EXPECT_FALSE(a.senderHatZeichen());
    EXPECT_FALSE(a.txAvailable());
    a.setzeCTS(true);
    EXPECT_TRUE(a.senderHatZeichen());
    EXPECT_EQ(a.txGet(), 0x41);
    EXPECT_FALSE(a.senderHatZeichen());  // Puffer leer
}

/**
 * @test Z80SIO/AutoEnables_EmpfaengerNurBeiDcd
 * @brief WR3 D5: der Empfänger nimmt nur bei aktivem /DCD auf; `empfaengerFrei()`
 *        meldet zusätzlich einen vollen FIFO.
 */
TEST(Z80SIO, AutoEnables_EmpfaengerNurBeiDcd) {
    Z80SIO sio;
    auto& b = sio.channelB();
    EXPECT_TRUE(b.empfaengerFrei());     // ohne Auto Enables: DCD egal
    schreibeWR(sio, true, 3, 0xE1);
    EXPECT_FALSE(b.empfaengerFrei());
    b.rxByte(0x55);                      // ohne DCD verworfen, wie am Gerät
    EXPECT_EQ(sio.ioRead(3) & 0x01, 0x00);
    b.setzeDCD(true);
    EXPECT_TRUE(b.empfaengerFrei());
    b.rxByte(1); b.rxByte(2); b.rxByte(3);
    EXPECT_FALSE(b.empfaengerFrei());    // FIFO voll
    EXPECT_EQ(sio.ioRead(2), 1);
    EXPECT_TRUE(b.empfaengerFrei());
}

/**
 * @test Z80SIO/Break_EmpfangSetztRr0D7MitInterruptAnAnfangUndEnde
 * @brief Ein empfangenes Break setzt RR0 D7 und löst Ext/Status aus; das Ende ebenso.
 */
TEST(Z80SIO, Break_EmpfangSetztRr0D7MitInterruptAnAnfangUndEnde) {
    Z80SIO sio;
    sio.setIEI(true);
    schreibeWR(sio, false, 1, 0x01);
    auto& a = sio.channelA();
    a.setzeBreakEmpfang(true);
    EXPECT_EQ(sio.ioRead(1) & 0x80, 0x80);
    EXPECT_TRUE(sio.hasInterrupt());
    (void)sio.getVector();
    sio.ioWrite(1, 0x10);                // Reset Ext/Status
    sio.onRETI();
    EXPECT_EQ(sio.ioRead(1) & 0x80, 0x80);   // Break dauert an
    EXPECT_FALSE(sio.hasInterrupt());
    a.setzeBreakEmpfang(false);          // Ende des Break
    EXPECT_TRUE(sio.hasInterrupt());
    sio.ioWrite(1, 0x10);
    EXPECT_EQ(sio.ioRead(1) & 0x80, 0x00);
}

/**
 * @test Z80SIO/Leitungen_SerializeRoundTrip
 * @brief Eingänge und Ext/Status-Latch überstehen eine Momentaufnahme.
 */
TEST(Z80SIO, Leitungen_SerializeRoundTrip) {
    Z80SIO a;
    schreibeWR(a, false, 1, 0x01);
    a.channelA().setzeCTS(true);         // latcht
    a.channelA().setzeCTS(false);
    a.channelB().setzeDCD(true);
    std::vector<uint8_t> blob; a.serialize(blob);
    Z80SIO b;
    const uint8_t* p = blob.data();
    ASSERT_TRUE(b.deserialize(p, blob.data() + blob.size()));
    EXPECT_FALSE(b.channelA().cts());
    EXPECT_EQ(b.ioRead(1) & 0x20, 0x20); // noch gelatcht
    EXPECT_TRUE(b.channelB().dcd());
    EXPECT_EQ(b.ioRead(3) & 0x08, 0x08);
}

/**
 * @test Z80SIO/StatusAffectsVector_AlleSechsQuellen
 * @brief AP-T1a: der Fall oben prüft nur B-Rx und B-Tx; hier die übrigen vier Quellen
 *        nach Zilog — A-Rx 110, A-Tx 100, A-Ext/Status 101, B-Ext/Status 001.  Genau
 *        A-Ext und B-Tx waren bis AP-E3 vertauscht.
 */
TEST(Z80SIO, StatusAffectsVector_AlleSechsQuellen) {
    Z80SIO sio;
    sio.setIEI(true);
    sio.ioWrite(3, 0x02); sio.ioWrite(3, 0xD0);   // WR2 = D0H
    sio.ioWrite(3, 0x01); sio.ioWrite(3, 0x17);   // Ch B WR1: Ext, Tx, SAV, Rx
    // Ch A WR1 = 17H wie B (D2 wirkt nur an B): D4–3 = 10 UND D3–2 = 01 — Rx-Interrupt
    // frei, gleich ob man D4–3 (Datenblatt) oder D3–2 (rxIntEnabled(), bekannter
    // Befund aus AP-S3, Entwurf 19 §12.1) liest.  Mit 13H läge der Test auf dem Befund.
    sio.ioWrite(1, 0x01); sio.ioWrite(1, 0x17);
    auto quittiere = [&] { sio.onRETI(); sio.setIEI(true); };

    sio.channelA().rxByte(0x01);
    EXPECT_EQ(sio.getVector(), 0xDC) << "A-Rx 110";
    quittiere();
    sio.ioRead(0);                                  // Zeichen abholen

    sio.ioWrite(0, 0x41);
    sio.channelA().txGet();
    ASSERT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.getVector(), 0xD8) << "A-Tx 100";
    quittiere();

    sio.channelA().setzeCTS(true);
    ASSERT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.getVector(), 0xDA) << "A-Ext/Status 101";
    quittiere();

    sio.channelB().setzeDCD(true);
    ASSERT_TRUE(sio.hasInterrupt());
    EXPECT_EQ(sio.getVector(), 0xD2) << "B-Ext/Status 001";
}
