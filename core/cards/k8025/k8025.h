/**
 * @file k8025.h
 * @brief K8025 ASS – Anschlußsteuerung Seriell (serial interface card) emulation.
 *
 * Emulates the K8025 serial interface card for the Robotron K1520/A5120 system.
 * The physical card contains two Z80 SIOs, one Z80 CTC (baud-rate generator),
 * and one Z80 PIO (DIL-switch readout for DFÜ configuration).
 *
 * I/O port assignment (base 0x50):
 * @code
 *   0x50–0x53  SIO A33 (sio_dfue_):         DFÜ ch A (modem/host), ch B unused
 *   0x54–0x57  PIO A31 (pio_a31_):          DIL switch readout (DFÜ config input)
 *   0x58–0x5B  CTC A34 (ctc_a34_):          Baud-rate generator (4 channels)
 *   0x5C–0x5F  SIO A32 (sio_kbd_printer_):  ch A = keyboard K7637, ch B = printer
 * @endcode
 *
 * Internal interrupt priority (highest to lowest):
 * @code
 *   External IEI → SIO A33 → SIO A32 → CTC A34 → PIO A31 → External IEO
 * @endcode
 *
 * External interfaces:
 *   Keyboard : K7637 serial keyboard connected to SIO A32 channel A (connector X4)
 *   Printer  : Centronics-compatible printer on SIO A32 channel B (connector X3)
 *   DFÜ      : Data transmission unit (modem/host) on SIO A33 channel A (connector X1)
 *
 * @note clockTick() must be called from the machine run loop to drive the CTC
 *       baud-rate generator.  Without it the SIOs will never produce interrupts.
 *
 * @see doc/design/06_k8025_ass.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_sio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <cstdint>
#include <functional>

/**
 * @class K8025
 * @brief Emulation of the K8025 ASS (Anschlußsteuerung Seriell) serial interface card.
 *
 * Features:
 * - SIO A33: DFÜ (data-transmission) serial channel, injects/extracts bytes via callbacks
 * - SIO A32: Keyboard (channel A) and printer (channel B) serial channels
 * - CTC A34: Four-channel baud-rate generator; must be clocked by clockTick()
 * - PIO A31: DIL-switch readout for DFÜ configuration (input-only, read by BIOS)
 * - InterruptSlave: SIO A33 → SIO A32 → CTC A34 → PIO A31 daisy-chain
 *
 * Typical usage:
 * @code
 *   K1520Bus bus;
 *   K8025 ass(bus);
 *   bus.registerIO(&ass, 0x50, 16);  // ports 0x50–0x5F
 *   // in run loop:
 *   ass.clockTick();
 *   // inject a key from K7637:
 *   ass.keyboardRxByte(0x41);
 * @endcode
 */
class K8025 : public BusDevice, public InterruptSlave {
public:
    /** @brief Hardware configuration (I/O base address). */
    struct A5120Config {
        uint8_t io_base;             ///< Base I/O port (default 0x50)
        A5120Config() : io_base(0x50) {}
    };

    /**
     * @brief Construct a K8025 with default A5120 configuration.
     * @param bus K1520 system bus reference (unused but kept for interface uniformity)
     * @param cfg Hardware configuration (I/O base address)
     */
    K8025(K1520Bus& bus, const A5120Config& cfg = A5120Config{});

    // ─── BusDevice interface (I/O ports 0x50–0x5F) ────────────────────────

    /**
     * @brief Handle an IN instruction to a K8025 I/O port.
     *
     * Dispatches to SIO A33 (0x50–0x53), PIO A31 (0x54–0x57),
     * CTC A34 (0x58–0x5B), or SIO A32 (0x5C–0x5F).
     *
     * @param port Port address (0x50–0x5F)
     * @return Data byte from the selected sub-device
     */
    uint8_t     ioRead(uint8_t port) override;

    /**
     * @brief Handle an OUT instruction to a K8025 I/O port.
     *
     * Dispatches to SIO A33 (0x50–0x53), PIO A31 (0x54–0x57),
     * CTC A34 (0x58–0x5B), or SIO A32 (0x5C–0x5F).
     *
     * @param port Port address (0x50–0x5F)
     * @param data Byte written by CPU
     */
    void        ioWrite(uint8_t port, uint8_t data) override;

    /**
     * @brief Return the device name.
     * @return "K8025"
     */
    const char* deviceName() const override { return "K8025"; }

    // ─── InterruptSlave interface ──────────────────────────────────────────

    /**
     * @brief Set /IEI from the upstream interrupt chain.
     *
     * Propagates IEI through the internal SIO A33 → SIO A32 → CTC A34 → PIO A31 chain.
     *
     * @param iei true when upstream allows this card to interrupt
     */
    void    setIEI(bool iei) override;

    /**
     * @brief Get /IEO to pass to the downstream device.
     * @return true to pass interrupt downstream; false if this card blocks the chain
     */
    bool    getIEO() const override;

    /**
     * @brief Check whether this card has a pending interrupt.
     * @return true if any internal sub-device has a pending interrupt
     */
    bool    hasInterrupt() const override;

    /**
     * @brief Return the active interrupt vector from the highest-priority device.
     * @return 8-bit interrupt vector, or 0xFF if none
     */
    uint8_t getVector() const override;

    // ─── Keyboard interface (SIO A32, channel A, connector X4) ────────────

    /**
     * @brief Inject one byte received from the K7637 serial keyboard.
     *
     * The byte is pushed into the SIO A32 channel A RX FIFO.  If interrupt
     * enable is set, the SIO asserts /INT so the CPU can read it.
     *
     * @param byte Received byte from keyboard
     */
    void    keyboardRxByte(uint8_t byte);

    /**
     * @brief Check whether the CPU has sent a byte to the keyboard (LED commands).
     *
     * Returns true if SIO A32 channel A TX FIFO contains an outgoing byte.
     *
     * @return true if a TX byte is waiting
     */
    bool    keyboardTxAvailable();

    /**
     * @brief Retrieve one TX byte from the SIO A32 channel A TX FIFO.
     *
     * Call keyboardTxAvailable() first; behaviour is undefined if the FIFO is empty.
     *
     * @return Byte that the CPU sent to the keyboard
     */
    uint8_t keyboardTxGet();

    // ─── DFÜ interface (SIO A33, channel A, connector X1) ─────────────────

    /**
     * @brief Inject one byte received from the DFÜ (modem/host) interface.
     *
     * Pushes the byte into the SIO A33 channel A RX FIFO.
     * Also invokes dfue_rx_cb_ if a callback is registered.
     *
     * @param byte Received byte from DFÜ device
     */
    void    dfueRxByte(uint8_t byte);

    /**
     * @brief Check whether the CPU has sent a byte to the DFÜ interface.
     * @return true if a TX byte is waiting in SIO A33 channel A
     */
    bool    dfueTxAvailable();

    /**
     * @brief Retrieve one TX byte from the SIO A33 channel A TX FIFO.
     * @return Byte that the CPU sent to the DFÜ device
     */
    uint8_t dfueTxGet();

    /** @brief Callback type for serial byte reception. */
    using SerialCallback = std::function<void(uint8_t)>;

    /**
     * @brief Register a callback invoked when the DFÜ SIO receives a byte.
     *
     * @param cb Callback with signature void(uint8_t byte), or empty to disable
     */
    void    setDFUERxCallback(SerialCallback cb);

    // ─── Printer interface (SIO A32, channel B, connector X3) ─────────────

    /**
     * @brief Check whether the CPU has sent a byte to the printer.
     * @return true if a TX byte is waiting in SIO A32 channel B
     */
    bool    printerTxAvailable();

    /**
     * @brief Retrieve one TX byte from the SIO A32 channel B TX FIFO.
     * @return Byte that the CPU sent to the printer
     */
    uint8_t printerTxGet();

    // ─── CTC clock ─────────────────────────────────────────────────────────

    /**
     * @brief Advance the CTC A34 baud-rate generator by one system clock tick.
     *
     * Must be called once per CPU step from the A5120Machine run loop.
     * Without this the CTC counters never decrement and the SIO baud clocks
     * are never driven, preventing serial communication.
     */
    void    clockTick();

    // ─── Sub-chip accessors ─────────────────────────────────────────────────

    /**
     * @brief Return a reference to SIO A33 (DFÜ / modem channel).
     *
     * Used by A5120Machine wiring and unit tests.
     *
     * @return Reference to sio_dfue_ (Z80SIO)
     */
    Z80SIO& sioA33() { return sio_dfue_; }

    /**
     * @brief Return a reference to SIO A32 (keyboard + printer channels).
     *
     * Used by A5120Machine to connect the K7637 keyboard peripheral and by
     * unit tests to inspect SIO state.
     *
     * @return Reference to sio_kbd_printer_ (Z80SIO)
     */
    Z80SIO& sioA32() { return sio_kbd_printer_; }

    /**
     * @brief Return a reference to CTC A34 (baud-rate generator).
     *
     * Used by unit tests to inspect or manually drive CTC channel state.
     *
     * @return Reference to ctc_a34_ (Z80CTC)
     */
    Z80CTC& ctcA34() { return ctc_a34_; }

private:
    /**
     * @brief Propagate the current IEI value through the internal daisy chain.
     *
     * Must be called after any operation that may change interrupt state in
     * any sub-device.  Order (highest to lowest priority):
     *   SIO A33 → SIO A32 → CTC A34 → PIO A31
     */
    void updateInternalChain();

    A5120Config    cfg_;                              ///< Hardware configuration
    Z80SIO         sio_dfue_        {"K8025-SIO-A33"}; ///< DFÜ SIO (ports 0x50–0x53)
    Z80SIO         sio_kbd_printer_ {"K8025-SIO-A32"}; ///< Keyboard/printer SIO (ports 0x5C–0x5F)
    Z80CTC         ctc_a34_         {"K8025-CTC-A34"}; ///< Baud-rate CTC (ports 0x58–0x5B)
    Z80PIO         pio_a31_         {"K8025-PIO-A31"}; ///< DIL-switch PIO (ports 0x54–0x57)
    bool           iei_in_  = false;                   ///< Last IEI from upstream chain
    SerialCallback dfue_rx_cb_;                        ///< Optional callback on DFÜ RX byte
};
