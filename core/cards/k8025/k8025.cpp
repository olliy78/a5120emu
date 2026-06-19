/**
 * @file k8025.cpp
 * @brief K8025 ASS – Anschlußsteuerung Seriell (serial interface card) implementation.
 *
 * Implements the K8025 serial interface card for the Robotron K1520/A5120 system.
 * The card aggregates two Z80 SIOs, one Z80 CTC, and one Z80 PIO behind a
 * 16-port I/O window (default 0x50–0x5F) and exposes them as a single
 * BusDevice + InterruptSlave.
 *
 * I/O port assignment (base 0x50):
 * @code
 *   0x50–0x53  SIO A33 (sio_dfue_):         DFÜ – modem/host serial channel
 *   0x54–0x57  PIO A31 (pio_a31_):          DIL-switch readout (DFÜ config)
 *   0x58–0x5B  CTC A34 (ctc_a34_):          Baud-rate generator (4 channels)
 *   0x5C–0x5F  SIO A32 (sio_kbd_printer_):  Keyboard (ch A) + Printer (ch B)
 * @endcode
 *
 * Internal interrupt priority (highest → lowest):
 *   SIO A33 → SIO A32 → CTC A34 → [PIO A31 → External IEO]
 *
 * @see k8025.h
 * @see doc/design/06_k8025_ass.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#include "core/cards/k8025/k8025.h"

// ─── Constructor ──────────────────────────────────────────────────────────────

/**
 * @brief Construct a K8025 serial interface card.
 *
 * Registers the 16-port I/O window on the K1520 bus and propagates the
 * initial IEI state through the internal daisy chain.
 *
 * @param bus K1520 system bus reference (registers I/O ports io_base–io_base+15)
 * @param cfg Hardware configuration (I/O base address, default 0x50)
 */
K8025::K8025(K1520Bus& bus, const A5120Config& cfg)
    : cfg_(cfg)
{
    bus.registerIO(this, cfg_.io_base, 16);
    // Pre-load Register A31 (U212) with the A41 DIP-switch state so the
    // BIOS reads the correct baud rate and block size from port 0x54.
    pio_a31_.portAWrite(cfg_.dil_a41);
    updateInternalChain();
}

// ─── Internal daisy-chain propagation ────────────────────────────────────────

/**
 * @brief Propagate the current IEI value through the internal interrupt daisy chain.
 *
 * Sets the IEI input for each sub-device in priority order:
 * @code
 *   External IEI → SIO A33 → SIO A32 → CTC A34 (→ PIO A31 excluded from chain)
 * @endcode
 *
 * Must be called after any I/O read, I/O write, or external setIEI() call that
 * may have changed interrupt state in one of the sub-devices.
 */
void K8025::updateInternalChain()
{
    // Priority: SIO A33 (highest) → SIO A32 → CTC A34 (lowest) → /IEO out
    sio_dfue_.setIEI(iei_in_);
    sio_kbd_printer_.setIEI(sio_dfue_.getIEO());
    ctc_a34_.setIEI(sio_kbd_printer_.getIEO());
}

// ─── BusDevice ───────────────────────────────────────────────────────────────

/**
 * @brief Handle an IN instruction to a K8025 I/O port.
 *
 * Dispatches the read to the appropriate sub-device based on the relative
 * offset from io_base:
 * - rel 0–3  → SIO A33 (DFÜ, ports 0x50–0x53)
 * - rel 4–7  → PIO A31 (DIL switch, ports 0x54–0x57)
 * - rel 8–11 → CTC A34 (baud-rate, ports 0x58–0x5B)
 * - rel 12–15→ SIO A32 (keyboard/printer, ports 0x5C–0x5F)
 *
 * Updates the internal daisy chain after the read.
 *
 * @param port Port address (io_base to io_base+15)
 * @return Data byte from the selected sub-device
 */
uint8_t K8025::ioRead(uint8_t port)
{
    uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    uint8_t sub = port & 0x03;

    uint8_t result;
    if      (rel < 4)  result = sio_dfue_.ioRead(sub);           // 0x50–0x53
    else if (rel < 8)  result = pio_a31_.ioRead(sub);             // 0x54–0x57
    else if (rel < 12) result = ctc_a34_.ioRead(sub);             // 0x58–0x5B
    else               result = sio_kbd_printer_.ioRead(sub);     // 0x5C–0x5F

    updateInternalChain();
    return result;
}

/**
 * @brief Handle an OUT instruction to a K8025 I/O port.
 *
 * Dispatches the write to the appropriate sub-device based on the relative
 * offset from io_base:
 * - rel 0–3  → SIO A33 (DFÜ, ports 0x50–0x53)
 * - rel 4–7  → PIO A31 (DIL switch, ports 0x54–0x57)
 * - rel 8–11 → CTC A34 (baud-rate, ports 0x58–0x5B)
 * - rel 12–15→ SIO A32 (keyboard/printer, ports 0x5C–0x5F)
 *
 * Updates the internal daisy chain after the write.
 *
 * @param port Port address (io_base to io_base+15)
 * @param data Byte written by CPU
 */
void K8025::ioWrite(uint8_t port, uint8_t data)
{
    uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    uint8_t sub = port & 0x03;

    if      (rel < 4)  sio_dfue_.ioWrite(sub, data);             // 0x50–0x53
    else if (rel < 8)  pio_a31_.ioWrite(sub, data);               // 0x54–0x57
    else if (rel < 12) ctc_a34_.ioWrite(sub, data);               // 0x58–0x5B
    else               sio_kbd_printer_.ioWrite(sub, data);       // 0x5C–0x5F

    updateInternalChain();
}

// ─── InterruptSlave ───────────────────────────────────────────────────────────

/**
 * @brief Set /IEI from the upstream interrupt chain.
 *
 * Stores the incoming IEI value and propagates it through the internal
 * SIO A33 → SIO A32 → CTC A34 chain via updateInternalChain().
 *
 * @param iei true when the upstream device allows this card to interrupt
 */
void K8025::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

/**
 * @brief Return /IEO to pass to the downstream device in the daisy chain.
 *
 * Returns false when any internal sub-device has a pending interrupt that
 * would block downstream propagation.  Returns false unconditionally when
 * iei_in_ is false (card cannot interrupt).
 *
 * @return true to pass the enable signal downstream; false if the chain is blocked
 */
bool K8025::getIEO() const
{
    // If our IEI is not asserted, block IEO.
    if (!iei_in_) return false;
    // IEO is blocked when any chip in our chain has an active interrupt.
    // Using hasInterrupt() works because updateInternalChain() has set each
    // chip's iei_ correctly, so hasInterrupt() already considers priority.
    return !sio_dfue_.hasInterrupt()
        && !sio_kbd_printer_.hasInterrupt()
        && !ctc_a34_.hasInterrupt();
}

/**
 * @brief Check whether any internal sub-device has a pending interrupt.
 *
 * Returns false when iei_in_ is false (prevents reporting interrupts when
 * the card is blocked from the upstream chain).
 *
 * @return true if SIO A33, SIO A32, or CTC A34 has a pending interrupt
 */
bool K8025::hasInterrupt() const
{
    if (!iei_in_) return false;
    return sio_dfue_.hasInterrupt()
        || sio_kbd_printer_.hasInterrupt()
        || ctc_a34_.hasInterrupt();
}

/**
 * @brief Return the interrupt vector from the highest-priority active device.
 *
 * Priority: SIO A33 > SIO A32 > CTC A34.  After updateInternalChain() the
 * lower-priority chips have their iei_ deasserted when a higher-priority chip
 * has a pending interrupt, so their hasInterrupt() returns false automatically.
 *
 * @return 8-bit interrupt vector from the active device, or 0xFF if none
 */
uint8_t K8025::getVector() const
{
    // Priority: SIO A33 first, then SIO A32, then CTC A34.
    // After updateInternalChain(), the lower-priority chips have iei_=false
    // when a higher-priority chip has a pending interrupt, so their
    // hasInterrupt() returns false automatically.
    if (sio_dfue_.hasInterrupt())        return sio_dfue_.getVector();
    if (sio_kbd_printer_.hasInterrupt()) return sio_kbd_printer_.getVector();
    if (ctc_a34_.hasInterrupt())         return ctc_a34_.getVector();
    return 0xFF;
}

// ─── Keyboard interface ───────────────────────────────────────────────────────

/**
 * @brief Inject one byte received from the K7637 serial keyboard.
 *
 * Pushes @p byte into the SIO A32 channel A RX FIFO.  If interrupt mode is
 * enabled on that channel, the SIO asserts /INT so the CPU can read the byte.
 * Updates the internal daisy chain after injection.
 *
 * @param byte Received byte from keyboard (K7637 scan code or ASCII)
 */
void K8025::keyboardRxByte(uint8_t byte)
{
    sio_kbd_printer_.channelA().rxByte(byte);
    updateInternalChain();
}

/**
 * @brief Check whether SIO A32 channel A has an outgoing byte for the keyboard.
 *
 * Returns true if the CPU has written a byte to SIO A32 channel A TX (e.g.
 * to control keyboard LEDs).
 *
 * @return true if a TX byte is waiting in the FIFO
 */
bool K8025::keyboardTxAvailable()
{
    return sio_kbd_printer_.channelA().txAvailable();
}

/**
 * @brief Retrieve one TX byte from SIO A32 channel A (keyboard LED command).
 *
 * Pops and returns the next byte from the SIO A32 channel A TX FIFO.
 * Behaviour is undefined if txAvailable() returns false.
 *
 * @return Byte that the CPU sent to the keyboard
 */
uint8_t K8025::keyboardTxGet()
{
    return sio_kbd_printer_.channelA().txGet();
}

// ─── DFÜ interface ────────────────────────────────────────────────────────────

/**
 * @brief Inject one byte received from the DFÜ (modem/host) interface.
 *
 * Pushes @p byte into the SIO A33 channel A RX FIFO and updates the internal
 * daisy chain.  Also invokes dfue_rx_cb_ if a callback is registered.
 *
 * @param byte Received byte from the DFÜ device (modem or remote host)
 */
void K8025::dfueRxByte(uint8_t byte)
{
    sio_dfue_.channelA().rxByte(byte);
    updateInternalChain();
}

/**
 * @brief Check whether SIO A33 channel A has an outgoing byte for the DFÜ interface.
 * @return true if a TX byte is waiting in the SIO A33 channel A FIFO
 */
bool K8025::dfueTxAvailable()
{
    return sio_dfue_.channelA().txAvailable();
}

/**
 * @brief Retrieve one TX byte from SIO A33 channel A (DFÜ transmit byte).
 *
 * Pops and returns the next byte from the SIO A33 channel A TX FIFO.
 * Behaviour is undefined if dfueTxAvailable() returns false.
 *
 * @return Byte that the CPU sent to the DFÜ device
 */
uint8_t K8025::dfueTxGet()
{
    return sio_dfue_.channelA().txGet();
}

/**
 * @brief Register a callback invoked whenever the DFÜ SIO receives a byte.
 *
 * The callback is called from within dfueRxByte() after the byte is pushed
 * into the RX FIFO.  Pass an empty std::function to disable the callback.
 *
 * @param cb Callback with signature void(uint8_t), or empty to disable
 */
void K8025::setDFUERxCallback(SerialCallback cb)
{
    dfue_rx_cb_ = std::move(cb);
}

// ─── Printer interface ────────────────────────────────────────────────────────

/**
 * @brief Check whether SIO A32 channel B has an outgoing byte for the printer.
 *
 * Returns true when the CPU has written a character to SIO A32 channel B TX.
 *
 * @return true if a TX byte is waiting in the SIO A32 channel B FIFO
 */
bool K8025::printerTxAvailable()
{
    return sio_kbd_printer_.channelB().txAvailable();
}

/**
 * @brief Retrieve one TX byte from SIO A32 channel B (printer output byte).
 *
 * Pops and returns the next byte from the SIO A32 channel B TX FIFO.
 * Behaviour is undefined if printerTxAvailable() returns false.
 *
 * @return Byte that the CPU sent to the printer
 */
uint8_t K8025::printerTxGet()
{
    return sio_kbd_printer_.channelB().txGet();
}

// ─── CTC clock tick ───────────────────────────────────────────────────────────

/**
 * @brief Advance CTC A34 by one system clock tick.
 *
 * Must be called once per CPU step from the A5120Machine run loop.  Without
 * this call the CTC counter channels never decrement, the SIO baud clocks are
 * never driven, and no serial interrupts will be generated.
 *
 * Updates the internal daisy chain after the tick in case the CTC raised an
 * interrupt.
 */
void K8025::clockTick(int ticks)
{
    // Advance by the CPU T-states elapsed (see K2526::clockTick): the baud-rate
    // CTC divides the system clock, so it must tick once per clock cycle, not
    // once per instruction.
    for (int i = 0; i < ticks; ++i)
        ctc_a34_.clockTick();
    updateInternalChain();
}
