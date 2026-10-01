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
#include "core/serial/sio_format.h"

using k1520::serial::SerialFormat;
using k1520::serial::Taktquelle;

// ─── Anschluss je Schnittstelle (Entwurf 19 §3.1, §5.1, AP-S5) ──────────────
//
// Brücken laut Transkription §2.3.1 / §3.1 („gezeichnet" = Index 0):
//   DFÜ/V.24  (A33-A, X6): W1:7   gezeichnet ZRE-CTC K0, nicht gezeichnet CTC A34 K2
//   DFÜ/IFSS  (A33-B, X5): X7–X8  ZRE-CTC K0,           X8–X9          CTC A34 K1
//   Drucker   (A32-B, X3): fest CTC A34 K0 (§2.4)
// Leitungen der V.24 (§2.3.2): /CTSA = V106 ∧ V107, /DCDA = V109 ∧ V107, DCDB = V107;
// die IFSS-Kanäle haben keine Steuerleitungen — ihre /CTS- und /DCD-Eingänge (außer
// DCDB) bleiben, wie sie seit AP-S3 sind: inaktiv (RR0 D5/D3 = 0, der Gast sieht
// dasselbe RR0 wie vor AP-S5; der Stromlaufplan der K8025 liegt nicht vor).

class K8025::Anschluss : public k1520::serial::SerialAnschluss {
public:
    Anschluss(K8025& k, Schnittstelle s) : k_(k), s_(s) {}

    const char* name() const override {
        switch (s_) {
            case DfueV24:  return "DFÜ/V.24";
            case DfueIfss: return "DFÜ/IFSS";
            default:       return "Drucker";
        }
    }
    const char* stecker() const override {
        switch (s_) {
            case DfueV24:  return "X6";
            case DfueIfss: return "X5";
            default:       return "X3";
        }
    }
    bool v24() const override { return s_ == DfueV24; }
    std::vector<Taktquelle> taktquellen() const override {
        switch (s_) {
            case DfueV24:  return {{"ZRE-CTC K0 (W1:7)"}, {"CTC A34 K2 (W1:7 versetzt)"}};
            case DfueIfss: return {{"ZRE-CTC K0 (X7–X8)"}, {"CTC A34 K1 (X8–X9)"}};
            default:       return {};
        }
    }
    void waehleTaktquelle(int i) override { quelle_ = (i == 1) ? 1 : 0; }

    SerialFormat format() const override {
        return k1520::serial::serialFormatAusSio(kanal(), ctcTakte());
    }
    bool senderHatZeichen() const override { return kanal().senderHatZeichen(); }
    uint8_t senderNimm() override {
        uint8_t b = kanal().txGet();
        // Die Leitung trägt nur die programmierten Datenbits (WR5 D6–5).
        const uint8_t bits = kanal().format().tx_bits;
        if (bits < 8) b &= static_cast<uint8_t>((1u << bits) - 1);
        k_.seriell_geaendert_ = true;   // Tx leer → Tx-Interrupt möglich
        if (!belegt_ && abnehmer_) abnehmer_(b);
        return b;
    }
    bool empfaengerFrei() const override { return kanal().empfaengerFrei(); }
    void empfange(uint8_t b) override {
        kanal().rxByte(b);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    bool rts() const override { return kanal().rts(); }
    bool dtr() const override { return kanal().dtr(); }
    void setzeEingaenge(bool cts, bool dsr, bool dcd) override {
        if (s_ != DfueV24) return;   // IFSS: keine Steuerleitungen
        // Kartenlogik A23 (§2.3.2): V106/V109 nur mit V107 wirksam; V107 zusätzlich
        // allein an DCDB.
        k_.sio_dfue_.channelA().setzeCTS(cts && dsr);
        k_.sio_dfue_.channelA().setzeDCD(dcd && dsr);
        k_.sio_dfue_.channelB().setzeDCD(dsr);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    bool breakGesendet() const override { return kanal().breakSenden(); }
    void breakEmpfang(bool aktiv) override {
        kanal().setzeBreakEmpfang(aktiv);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    void leitungBelegt(bool belegt) override { belegt_ = belegt; }

    // Alter Unterbau (K8025::setAbnehmer/einspeisen).
    void setAbnehmer(SerialCallback cb) { abnehmer_ = std::move(cb); }
    void einspeisen(uint8_t b) {
        if (belegt_) return;   // Transport/Loop am Stecker: ins Leere (§8)
        empfange(b);
    }

private:
    Z80SIO::Channel& kanal() const {
        switch (s_) {
            case DfueV24:  return k_.sio_dfue_.channelA();
            case DfueIfss: return k_.sio_dfue_.channelB();
            default:       return k_.sio_kbd_printer_.channelB();
        }
    }
    uint64_t zreTakte() const { return k_.zre_takt_ ? k_.zre_takt_() : 0; }
    uint64_t ctcTakte() const {
        switch (s_) {
            case DfueV24:  return quelle_ == 0 ? zreTakte() : k_.ctc_a34_.teilerTakte(2);
            case DfueIfss: return quelle_ == 0 ? zreTakte() : k_.ctc_a34_.teilerTakte(1);
            default:       return k_.ctc_a34_.teilerTakte(0);
        }
    }

    K8025&         k_;
    Schnittstelle  s_;
    int            quelle_ = 0;
    bool           belegt_ = false;
    SerialCallback abnehmer_;
};

K8025::~K8025() = default;

k1520::serial::SerialAnschluss& K8025::anschluss(Schnittstelle s)
{
    return *anschluesse_[static_cast<size_t>(s)];
}

void K8025::setzeZreTakt(Z80CTC::PeriodenQuelle quelle)
{
    zre_takt_ = quelle;
    // ZC/TO0 der ZRE liegt im Emulator an CLK/TRG0–3 der CTC A34 (Koppelbus, A5120Machine).
    for (int k = 0; k < 4; ++k) ctc_a34_.setzeEingangsQuelle(k, quelle);
}

void K8025::setAbnehmer(Schnittstelle s, SerialCallback cb)
{
    anschluesse_[static_cast<size_t>(s)]->setAbnehmer(std::move(cb));
}

void K8025::einspeisen(Schnittstelle s, uint8_t byte)
{
    anschluesse_[static_cast<size_t>(s)]->einspeisen(byte);
}

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
    for (int i = 0; i < SchnittstellenAnzahl; ++i)
        anschluesse_[static_cast<size_t>(i)] =
            std::make_unique<Anschluss>(*this, static_cast<Schnittstelle>(i));
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
 * daisy chain.
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
bool K8025::clockTick(int ticks)
{
    // Advance by the CPU T-states elapsed (see K2526::clockTick): the baud-rate
    // CTC divides the system clock, so it must tick once per clock cycle, not
    // once per instruction.
    // updateInternalChain() NICHT pro Instruktion: die interne Daisy-Chain
    // (iei-Propagation der 3 Teilbausteine) wird ohnehin bei jedem Bus-Chain-Walk
    // über setIEI() aufgefrischt (der Walk ruft setIEI VOR hasInterrupt/getIEO).
    // Der Walk läuft nur noch, wenn die Chain dirty ist — dann ist der interne
    // Zustand frisch.  Der Baud-CTC ohne INT_EN feuert ständig, ändert aber keinen
    // Interrupt (clockTick(int) liefert dann false → kein unnötiges Dirty).
    return ctc_a34_.clockTick(ticks);   // batched, semantik-identisch
}
