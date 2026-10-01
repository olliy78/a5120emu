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
 *   0x50–0x53  SIO A33 (sio_dfue_):         DFÜ ch A = V.24 (X6), ch B = IFSS (X5)
 *   0x54–0x57  PIO A31 (pio_a31_):          DIL switch readout (DFÜ config input)
 *   0x58–0x5B  CTC A34 (ctc_a34_):          Baud-rate generator (4 channels)
 *   0x5C–0x5F  SIO A32 (sio_kbd_printer_):  ch A = keyboard K7637 (X4), ch B = printer (X3)
 * @endcode
 *
 * Internal interrupt priority (highest to lowest):
 * @code
 *   External IEI → SIO A33 → SIO A32 → CTC A34 → PIO A31 → External IEO
 * @endcode
 *
 * External interfaces (Transkription §2.3/§2.4; X1/X2 sind der Rechnerbus):
 *   Keyboard : K7637 serial keyboard on SIO A32 channel A (connector X4 — laut
 *              Kartendoku der Zusatzdrucker; im A5120 hängt dort die Tastatur)
 *   Printer  : Hauptdrucker (IFSS) on SIO A32 channel B (connector X3), Takt CTC A34 K0
 *   DFÜ/V.24 : SIO A33 channel A (connector X6) mit Steuerleitungen
 *              (/CTSA = V106 ∧ V107, /DCDA = V109 ∧ V107, RTSA = V105, DTRA = V108),
 *              Takt W1:7 = ZRE-CTC K0 (gezeichnet) / CTC A34 K2
 *   DFÜ/IFSS : SIO A33 channel B (connector X5), Takt RxTxCB über X7–X8 = ZRE-CTC K0
 *              (gezeichnet) / X8–X9 = CTC A34 K1; DCDB = V107 der V.24 (Abfrage der
 *              Betriebsbereitschaft allein)
 *
 * Nach außen geht jede einstellbare Schnittstelle über einen `SerialAnschluss`
 * (Entwurf 19 §3.1, §5.1, AP-S5): @ref anschluss.  Die Tastatur bleibt fest verdrahtet.
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
#include "core/serial/anschluss.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

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
    /** @brief Hardware configuration — mirrors physical jumper/DIP settings. */
    struct A5120Config {
        uint8_t io_base;   ///< Base I/O port (default 0x50)
        /**
         * DIL-Schalter A41 value as read by the CPU via Register A31 (port 0x54).
         * Encoding: EIN (closed) = 0, AUS (open) = 1; bit 0 = SW1, bit 7 = SW8.
         *
         * Default 0xAE = 0b10101110:
         *   SW1=EIN(0) Schirmerdung,  SW2=AUS(1), SW3=AUS(1), SW4=AUS(1),
         *   SW5=EIN(0) 1024B-block,   SW6=AUS(1), SW7=EIN(0) 1024B-block,
         *   SW8=AUS(1) → 9600 Baud, 1024-Byte Blocklänge (SIOS 1526 Standard).
         */
        uint8_t dil_a41;
        A5120Config() : io_base(0x50), dil_a41(0xAE) {}
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

    /// @brief Anfordernder Baustein (Debugger): SIO A33 (DFÜ), SIO A32 (Tastatur) oder CTC A34.
    const char* intDeviceName() const override {
        if (sio_dfue_.hasInterrupt())         return "K8025 SIO-A33 (DFUE)";
        if (sio_kbd_printer_.hasInterrupt())  return "K8025 SIO-A32 (Tastatur)";
        if (ctc_a34_.hasInterrupt())          return "K8025 CTC-A34";
        return "K8025";
    }

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

    // ─── Schnittstellen nach außen (Entwurf 19 §3.1, AP-S5) ─────────────────

    /// Einstellbare Schnittstellen in der Reihenfolge, in der die Maschine sie im
    /// `SerialHub` anmeldet (= Index der C-ABI).
    enum Schnittstelle : int { DfueV24 = 0, DfueIfss = 1, Drucker = 2, SchnittstellenAnzahl = 3 };

    /// Anschluss je Schnittstelle (lebt so lange wie die Karte).
    k1520::serial::SerialAnschluss& anschluss(Schnittstelle s);

    /// Fester Name der Tastaturschnittstelle (nicht einstellbar, Leitsatz 7).
    static constexpr const char* TASTATUR_NAME = "Tastatur K7637 (X4)";

    /**
     * @brief Takt der ZRE-CTC K0 (K2526, Ports 0CH) als Quelle der Brücken W1:7 und
     *        X7–X8: Maschinentakte je ZC/TO0-Impuls, 0 = unbekannt.
     *
     * Die Maschine verdrahtet damit, was über den Rechnerbus (ZC/TO, X1) kommt; im
     * Emulator geht ZC/TO0 schon über den Koppelbus an CLK/TRG0–3 der CTC A34 —
     * deren Eingangsperiode wird mit derselben Quelle bekannt gemacht (§6.2).
     */
    void setzeZreTakt(Z80CTC::PeriodenQuelle quelle);

    /// Ein Anschluss hat SIO-Zustand geändert (Zeichen genommen/zugestellt, Leitung,
    /// Break) — die Maschine bewertet dann die Interruptkette neu.  Liest und löscht.
    bool nimmSeriellGeaendert() { const bool g = seriell_geaendert_; seriell_geaendert_ = false; return g; }

    /** @brief Callback type for serial bytes (alter Unterbau, Entwurf 19 §8). */
    using SerialCallback = std::function<void(uint8_t)>;

    /**
     * @brief Alter Unterbau für Tests/`k1520_serial_*`: Abnehmer der Bytes, die der
     *        Gast an Schnittstelle @p s SENDET.  Das Byte kommt in seiner Zeichenzeit
     *        (der Wandler taktet), aber nur, solange weder ein Transport noch der Loop
     *        den Stecker belegt — sonst geht es dorthin und der Abnehmer schweigt.
     */
    void setAbnehmer(Schnittstelle s, SerialCallback cb);
    /// Ein Byte von außen in den Empfänger von @p s (alter Unterbau).  Belegt ein
    /// Transport/Loop den Stecker, geht es ins Leere.
    void einspeisen(Schnittstelle s, uint8_t byte);

    // ─── DFÜ interface (SIO A33, channel A, connector X6) — Kartentest-Zugriff ───

    /**
     * @brief Inject one byte received from the DFÜ (modem/host) interface.
     *
     * Pushes the byte into the SIO A33 channel A RX FIFO.
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

    /**
     * @brief Gleichbedeutend mit `setAbnehmer(DfueV24, cb)`: Bytes, die der Gast an die
     *        DFÜ/V.24 sendet (aus Sicht des Rechners draußen „empfangen" — daher der
     *        Name).  Bis AP-S5 wurde der Rückruf gespeichert, aber nie gerufen.
     */
    void    setDFUERxCallback(SerialCallback cb) { setAbnehmer(DfueV24, std::move(cb)); }

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
     * @brief Advance the CTC A34 baud-rate generator by @p ticks clock cycles.
     *
     * Call once per CPU step with the number of T-states the instruction took.
     * Without this the CTC counters never decrement and the SIO baud clocks
     * are never driven, preventing serial communication.
     *
     * @param ticks Number of system clock cycles to advance (default 1).
     */
    /// @return true, wenn der Baud-CTC in diesem Fenster eine ZC/TO-Flanke
    ///         erzeugte (→ Aufrufer markiert die Interrupt-Chain dirty).
    bool    clockTick(int ticks = 1);

    // ─── Sub-chip accessors ─────────────────────────────────────────────────

    /**
     * @brief Return a reference to SIO A33 (DFÜ / modem channel).
     *
     * Used by A5120Machine wiring and unit tests.
     *
     * @return Reference to sio_dfue_ (Z80SIO)
     */
    Z80SIO& sioA33() { return sio_dfue_; }
    /// @brief Const-Variante (Diagnose: `ivt`, `dev sio2`).
    const Z80SIO& sioA33() const { return sio_dfue_; }

    /**
     * @brief Hardware-Reset (/RESET des K1520-Backplane): Baudraten-CTC und beide
     *        SIOs in den Einschaltzustand.  Bus-/Koppelbus-Verdrahtung bleibt.
     */
    void reset() {
        ctc_a34_.reset();
        sio_kbd_printer_.reset();
        sio_dfue_.reset();
    }

    /**
     * @brief Return a reference to SIO A32 (keyboard + printer channels).
     *
     * Used by A5120Machine to connect the K7637 keyboard peripheral and by
     * unit tests to inspect SIO state.
     *
     * @return Reference to sio_kbd_printer_ (Z80SIO)
     */
    Z80SIO& sioA32() { return sio_kbd_printer_; }
    /** @brief const overload (snapshot serialisation from a const machine). */
    const Z80SIO& sioA32() const { return sio_kbd_printer_; }

    /**
     * @brief Return a reference to CTC A34 (baud-rate generator).
     *
     * Used by unit tests to inspect or manually drive CTC channel state.
     *
     * @return Reference to ctc_a34_ (Z80CTC)
     */
    Z80CTC& ctcA34() { return ctc_a34_; }
    /** @brief const overload (snapshot serialisation from a const machine). */
    const Z80CTC& ctcA34() const { return ctc_a34_; }

private:
    /**
     * @brief Propagate the current IEI value through the internal daisy chain.
     *
     * Must be called after any operation that may change interrupt state in
     * any sub-device.  Order (highest to lowest priority):
     *   SIO A33 → SIO A32 → CTC A34 → PIO A31
     */
    void updateInternalChain();

    class Anschluss;   // k8025.cpp
    friend class Anschluss;

    A5120Config    cfg_;                              ///< Hardware configuration
    Z80SIO         sio_dfue_        {"K8025-SIO-A33"}; ///< DFÜ SIO (ports 0x50–0x53)
    Z80SIO         sio_kbd_printer_ {"K8025-SIO-A32"}; ///< Keyboard/printer SIO (ports 0x5C–0x5F)
    Z80CTC         ctc_a34_         {"K8025-CTC-A34"}; ///< Baud-rate CTC (ports 0x58–0x5B)
    Z80PIO         pio_a31_         {"K8025-PIO-A31"}; ///< DIL-switch PIO (ports 0x54–0x57)
    bool           iei_in_  = false;                   ///< Last IEI from upstream chain
    Z80CTC::PeriodenQuelle zre_takt_;                  ///< ZRE-CTC K0 (W1:7, X7–X8)
    bool           seriell_geaendert_ = false;
    std::array<std::unique_ptr<Anschluss>, SchnittstellenAnzahl> anschluesse_;

public:
    ~K8025();
};
