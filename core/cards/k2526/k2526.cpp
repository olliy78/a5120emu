/**
 * @file k2526.cpp
 * @brief K2526 ZRE – Zentrale Recheneinheit (CPU card) implementation.
 *
 * Implements the K2526 CPU card emulation for the Robotron K1520/A5120 system.
 * This file provides the full run-time behaviour of:
 *
 *  - ZVE1 (Haupt-CPU, Z80 cpu_): main processor with Q240 protection filtering on
 *    every memory access; I/O accesses pass unfiltered (I/O protection not implemented).
 *  - ZVE2 (DMA-CPU, Z80 zve2_): second Z80 sharing the bus; runs DMA programs
 *    while /BUSRQ is asserted; bypasses Q240 (privileged DMA context).
 *  - BS-PIO (Q301): all four output bits (B0–B4) are decoded:
 *      B0 /LD-ROM    → boot ROM enable/disable
 *      B2 /SPS-ESR   → Q240 protection table write mode
 *      B3 /WAIT-ZVE2 → ZVE2 stall (zve2Step returns 0)
 *      B4 /SA        → system power-off request
 *    Port A input bit A3 (SPS-Ind) is driven back by the Q240 on violation.
 *    Port B input bit B1 (/INT-BS) is driven by the port-00H write trigger.
 *  - CTC (Q302): clock-tick driven; supplies baud rates and timer interrupts.
 *  - Q240 (MemIOProtect spa_): 1024-segment protection table, violation raises NMI.
 *  - U-Bus registers: keyboard scan code/valid and lamp outputs.
 *
 * @note ZVE2 memory accesses bypass Q240 because the DMA program runs under OS
 *       control and always accesses memory with appropriate privileges.
 *
 * @see k2526.h for full class documentation.
 * @see doc/design/03_k2526_zre.md for hardware design reference.
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#include "core/cards/k2526/k2526.h"
#include "core/logger.h"

// ─── Construction ────────────────────────────────────────────────────────────

/**
 * @brief Delegate constructor: use default A5120 configuration.
 */
K2526::K2526(K1520Bus& bus)
    : K2526(bus, A5120Config{}) {}

/**
 * @brief Main constructor: wire ZVE1, ZVE2, and BS-PIO callbacks.
 *
 * Connects both CPUs to the K1520 bus.  ZVE1's readByte and writeByte
 * callbacks route through the Q240 protection table (spa_) before reaching
 * the bus; ZVE2's callbacks bypass protection.  Port A/B output callbacks
 * of the BS-PIO are registered so that hardware signal changes propagate
 * immediately when the OS programs the BS-PIO.
 *
 * @param bus K1520 system bus (must outlive this object)
 * @param cfg Hardware bridge configuration
 */
K2526::K2526(K1520Bus& bus, const A5120Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    // ── ZVE1 (Haupt-CPU) mit K1520-Bus und Q240-Schutz verdrahten ───────────
    // Alle Speicherzugriffe von ZVE1 werden durch die Q240-Schutzlogik (spa_)
    // gefiltert.  Lesezugriffe auf geschützte Segmente geben 0xFF zurück und
    // lösen eine NMI aus; Schreibzugriffe werden verworfen.
    // Wenn /SPS-ESR aktiv ist (spa_.isTableWriteEnabled()), gehen Lese-/
    // Schreibzugriffe in die Schutztabelle, nicht in den Speicher.
    cpu_.readByte = [this](uint16_t a) -> uint8_t {
        if (spa_.isTableWriteEnabled())
            return spa_.readEntry(a);          // table-programming mode: read table
        if (spa_.isReadProtected(a)) {
            handleSPSViolation();
            return 0xFF;                       // blocked access returns open-drain
        }
        return bus_.memRead(a);
    };
    cpu_.writeByte = [this](uint16_t a, uint8_t d) {
        if (spa_.isTableWriteEnabled()) {
            spa_.writeEntry(a, d);             // table-programming mode: write table
            return;
        }
        if (spa_.isWriteProtected(a)) {
            handleSPSViolation();
            return;                            // blocked write silently dropped
        }
        bus_.memWrite(a, d);
    };
    cpu_.readPort     = [this](uint16_t p)           { return bus_.ioRead(p & 0xFF); };
    cpu_.writePort    = [this](uint16_t p, uint8_t d){ bus_.ioWrite(p & 0xFF, d); };
    cpu_.retiCallback = [this]()                     { bus_.signalRETI(); };

    // ── ZVE2 (DMA-CPU) mit K1520-Bus verdrahten (ohne Q240-Schutz) ──────────
    // ZVE2 teilt denselben Bus mit ZVE1.  Der DMA-Code läuft unter OS-Kontrolle
    // und darf auf alle Speicherbereiche zugreifen (kein Q240-Filter).
    // Kein retiCallback: ZVE2 führt keine Interrupt-Service-Routinen aus.
    zve2_.readByte  = [this](uint16_t a)           { return bus_.memRead(a); };
    zve2_.writeByte = [this](uint16_t a, uint8_t d){ bus_.memWrite(a, d); };
    zve2_.readPort  = [this](uint16_t p)           { return bus_.ioRead(p & 0xFF); };
    zve2_.writePort = [this](uint16_t p, uint8_t d){ bus_.ioWrite(p & 0xFF, d); };

    // ── BS-PIO Port-A-Ausgangs-Callback: A7=MEMDI steuert Speichersperre ────
    bs_pio_.setPortAOutputCallback([this](uint8_t data) {
        bool memdi = (data >> 7) & 1;
        bus_.setMEMDI(memdi);
        LOG_DEBUG("K2526", "BS-PIO PortA out=0x%02X → MEMDI=%d", data, (int)memdi);
    });

    // ── BS-PIO Port-B-Ausgangs-Callback: alle Ausgangsbits dekodieren ────────
    bs_pio_.setPortBOutputCallback([this](uint8_t data) {
        onBSPIOPortBOut(data);
    });
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

/**
 * @brief Register this card's I/O ports on the bus (ports 00H–0FH).
 *
 * Must be called once before powerOn().  After this, the bus dispatches all
 * accesses to ports 0x00–0x0F to this card's ioRead()/ioWrite() handlers.
 *
 * @param bus K1520 bus to register on
 */
void K2526::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 16);
    LOG_DEBUG("K2526", "attachToBus: I/O 00H-0FH registriert");
}

/**
 * @brief Perform power-on initialisation.
 *
 * Maps the 1 KB boot EPROM at 0x0000–0x03FF, initialises the BS-PIO in
 * Mode 3 (Bit Control) with the hardware-defined direction masks, and resets
 * the Q240 protection table.
 *
 * BS-PIO direction masks (Originalschaltung Abschn. 6.10.2):
 *   Port A: 0x7F → A7 = output (MEMDI), A6–A0 = inputs
 *   Port B: 0xE2 → B0/B2/B3/B4 = outputs; B1/B5–B7 = inputs
 *
 * After reset the pull-up on /LD-ROM holds B0=1 → ROM active.
 * The Q240 protection table is cleared so all segments are unprotected.
 */
void K2526::powerOn()
{
    // ── Lade-ROM aktivieren ───────────────────────────────────────────────
    rom_enabled_ = true;
    bus_.registerMem(&rom_, 0x0000, 1024);
    LOG_INFO("K2526", "Power-on: Lade-ROM aktiv (0000H-03FFH)");

    // ── Q240 Schutztabelle zurücksetzen ────────────────────────────────────
    spa_.reset();
    sps_ind_      = false;
    port_a_inputs_= 0xFF;   // all input lines pull-high at power-on
    int_bs_active_= false;
    zve2_wait_    = false;
    shutdown_req_ = false;
    LOG_INFO("K2526", "Q240: Schutztabelle zurückgesetzt (alle Segmente ungeschützt)");

    // ── BS-PIO im Bitmode (Mode 3) initialisieren ─────────────────────────
    // Port A (08H Data, 09H Ctrl): Richtungsmaske 0x7F
    //   A7=MEMDI1/2 = Ausgang(0), A6-A0 = Eingänge(1) → 0111 1111 = 0x7F
    bs_pio_.ioWrite(1, 0xCF);  // Port A: Mode 3 (11xx1111)
    bs_pio_.ioWrite(1, 0x7F);  // Port A: Richtungsmaske

    // Port B (0AH Data, 0BH Ctrl): Richtungsmaske 0xE2
    //   B0=/LD-ROM     Ausgang(0), B1=/INT-BS    Eingang(1)
    //   B2=/SPS-ESR    Ausgang(0), B3=/WAIT-ZVE2 Ausgang(0)
    //   B4=/SA         Ausgang(0), B5-B7 Config  Eingänge(1)
    //   → 1110 0010 = 0xE2
    bs_pio_.ioWrite(3, 0xCF);  // Port B: Mode 3
    bs_pio_.ioWrite(3, 0xE2);  // Port B: Richtungsmaske

    // Konfigurationsbrücken (B5-B7) und /INT-BS (B1, inaktiv=1) einspeisen
    bs_pio_.portBWrite(buildPortBInputs());

    LOG_INFO("K2526", "BS-PIO Mode 3 init: PortA-dir=0x7F, PortB-dir=0xE2");
}

/**
 * @brief Advance the CTC by one system clock tick.
 *
 * Must be called once per CPU step from the machine run loop.  Drives all
 * four CTC channels for baud-rate generation and timer interrupts.
 */
void K2526::clockTick()
{
    ctc_.clockTick();
}

// ─── I/O-Dispatch (Originalschaltung Abschn. 6.9.2) ─────────────────────────
//
// Adressbelegung (AB7-AB4=0, AB3-AB0 unterscheiden):
//   00H–07H  U-Bus    (bidirektionale Treiber A40/41, Tastatur X5)
//   08H–0BH  BS-PIO   (Q301, AB3=1 AB2=0)
//   0CH–0FH  CTC      (Q302, AB3=1 AB2=1)

/**
 * @brief Handle an IN instruction to a K2526 I/O port (00H–0FH).
 *
 * Dispatches to U-Bus registers (00H–07H), BS-PIO (08H–0BH), or CTC (0CH–0FH).
 * U-Bus reads: port 01H returns keyboard valid flag, port 06H returns scan code.
 * All other U-Bus reads return 0xFF (open drain, no device connected).
 *
 * @param port Relative port number (0x00–0x0F, absolute = port because base=0x00)
 * @return Data byte from the selected sub-device or register
 */
uint8_t K2526::ioRead(uint8_t port)
{
    if (port <= 0x07) {
        // ── U-Bus ──────────────────────────────────────────────────────────
        uint8_t result = 0xFF;
        switch (port) {
            case 0x01:  // /UCS2: Tastengültigkeitsabfrage
                result = ubus_kbd_valid_;
                LOG_DEBUG("K2526", "U-Bus IN 01H (/UCS2 Kbd-Valid) => 0x%02X", result);
                break;
            case 0x06:  // /UCS1: Tastencode lesen
                result = ubus_kbd_code_;
                LOG_DEBUG("K2526", "U-Bus IN 06H (/UCS1 Kbd-Code)  => 0x%02X", result);
                break;
            default:
                LOG_DEBUG("K2526", "U-Bus IN port=0x%02X => 0xFF (n.i.)", port);
                break;
        }
        return result;
    } else if (port <= 0x0B) {
        // ── BS-PIO (Ports 08H–0BH) ─────────────────────────────────────────
        uint8_t result = bs_pio_.ioRead(port);
        LOG_DEBUG("K2526", "BS-PIO IN  port=0x%02X (sub=%u) => 0x%02X",
                  port, port & 0x03u, result);
        return result;
    } else if (port <= 0x0F) {
        // ── CTC (Ports 0CH–0FH) ────────────────────────────────────────────
        uint8_t result = ctc_.ioRead(port);
        LOG_DEBUG("K2526", "CTC    IN  port=0x%02X (ch=%u)  => 0x%02X",
                  port, port & 0x03u, result);
        return result;
    }
    return 0xFF;
}

/**
 * @brief Handle an OUT instruction to a K2526 I/O port (00H–0FH).
 *
 * Dispatches to U-Bus registers (00H–07H), BS-PIO (08H–0BH), or CTC (0CH–0FH).
 *
 * Special U-Bus write handlers:
 *   - 00H /INT-BS: pulse BS-PIO B1 low to request an OS-level change interrupt
 *   - 02H /RES-SPA: reset the Q240 protection violation indicator (SPS-Ind)
 *   - 04H /RES-ZVE2: bit0=0 → assert ZVE2 reset; bit0=1 → release ZVE2
 *
 * @param port Relative port number (0x00–0x0F)
 * @param data Byte written by CPU
 */
void K2526::ioWrite(uint8_t port, uint8_t data)
{
    if (port <= 0x07) {
        // ── U-Bus ──────────────────────────────────────────────────────────
        switch (port) {
            case 0x00:
                // /INT-BS: OS-Ebenen-Wechsel anfordern.
                // Aktiviert den /INT-BS-Eingang (B1=0) der BS-PIO, was einen
                // BS-PIO-Interrupt auslöst, sofern der OS ihn konfiguriert hat.
                // Der int_bs_active_-Zustand bleibt bis zum nächsten /RES-SPA erhalten.
                int_bs_active_ = true;
                bs_pio_.portBWrite(buildPortBInputs());
                LOG_INFO("K2526", "U-Bus OUT 00H (/INT-BS): OS-Ebenen-Wechsel angefordert");
                break;

            case 0x02:
                // /RES-SPA: Speicherschutz-Verletzungsindikator zurücksetzen.
                // Löscht SPS-Ind (A3) und die NMI-Anforderung des Q240.
                // Wird vom OS-NMI-Handler aufgerufen, um die Verletzung zu quittieren.
                spa_.resetViolation();
                sps_ind_       = false;
                int_bs_active_ = false;     // /INT-BS-Latch mit zurücksetzen
                updatePortAInputs();
                bus_.clearNMI();
                bs_pio_.portBWrite(buildPortBInputs());
                LOG_INFO("K2526", "U-Bus OUT 02H (/RES-SPA): SPS-Indikator zurückgesetzt");
                break;

            case 0x03:  // /UCS4: Fehlerlampe / INS-MODE-Anzeige
                ubus_error_lamp_ = data;
                LOG_DEBUG("K2526", "U-Bus OUT 03H (/UCS4 ErrLamp) data=0x%02X", data);
                break;

            case 0x04:
                // /RES-ZVE2: DMA-CPU reset (active-low, bit 0).
                // bit0=0 → /RES-ZVE2 assertiert → ZVE2 in Reset versetzen.
                // bit0=1 → /RES-ZVE2 freigegeben → ZVE2 starten (PC=0000H).
                if (!(data & 0x01) && !zve2_reset_) {
                    zve2_reset_ = true;
                    LOG_INFO("K2526", "ZVE2 in Reset versetzt (OUT 04H=0x%02X)", data);
                } else if ((data & 0x01) && zve2_reset_) {
                    zve2_reset_ = false;
                    zve2_wait_ = false;  // /RESET release also deasserts /WAIT
                    zve2_.reset();
                    LOG_INFO("K2526", "ZVE2 gestartet: PC=0000H (OUT 04H=0x%02X)", data);
                }
                break;

            case 0x05:  // /UCS3: Lampenansteuerung (Selektoren)
                ubus_lamp_ = data;
                LOG_DEBUG("K2526", "U-Bus OUT 05H (/UCS3 Lamps)  data=0x%02X", data);
                break;

            default:
                LOG_DEBUG("K2526", "U-Bus OUT port=0x%02X data=0x%02X (n.i.)", port, data);
                break;
        }
    } else if (port <= 0x0B) {
        // ── BS-PIO (Ports 08H–0BH) ─────────────────────────────────────────
        LOG_DEBUG("K2526", "BS-PIO OUT port=0x%02X (sub=%u) data=0x%02X",
                  port, port & 0x03u, data);
        bs_pio_.ioWrite(port, data);
    } else if (port <= 0x0F) {
        // ── CTC (Ports 0CH–0FH) ────────────────────────────────────────────
        LOG_DEBUG("K2526", "CTC    OUT port=0x%02X (ch=%u)  data=0x%02X",
                  port, port & 0x03u, data);
        ctc_.ioWrite(port, data);
    }
}

// ─── BS-PIO Port-B-Ausgangs-Handler ──────────────────────────────────────────
//
// Dekodiert alle Ausgangsbits von Port B nach dem folgenden Schema:
//   B0 /LD-ROM    (active low):  1=ROM aktiv,  0=ROM aus
//   B2 /SPS-ESR   (active low):  0=Q240 Schreibmodus, 1=normale Prüfung
//   B3 /WAIT-ZVE2 (active low):  0=ZVE2 wartet, 1=ZVE2 läuft
//   B4 /SA        (active low):  0=Netzausschaltung angefordert

/**
 * @brief BS-PIO Port B output callback – decode all output signals.
 *
 * This callback is invoked by the BS-PIO whenever the CPU writes to Port B
 * (port 0x0A, sub-port 2).  All output bits (B0, B2, B3, B4) are decoded
 * and the corresponding hardware state is updated.
 *
 * @param data Byte written to BS-PIO Port B output register (output bits only
 *             are meaningful; input bits B1/B5–B7 are ignored here)
 */
void K2526::onBSPIOPortBOut(uint8_t data)
{
    // ── B0: /LD-ROM – Lade-ROM ein-/ausblenden ────────────────────────────
    // /LD-ROM ist active-low: B0=1 → ROM aktiv, B0=0 → ROM abgeschaltet.
    // Idempotent implementiert: zweiter Write mit gleichem Zustand ist No-Op.
    bool rom_should_be_disabled = !(data & 0x01);
    if (rom_should_be_disabled && rom_enabled_) {
        rom_enabled_ = false;
        bus_.unregisterMem(&rom_);
        LOG_INFO("K2526", "Lade-ROM DEAKTIVIERT: BS-PIO B0=/LD-ROM=0 (PortB=0x%02X)", data);
    } else if (!rom_should_be_disabled && !rom_enabled_) {
        rom_enabled_ = true;
        bus_.registerMem(&rom_, 0x0000, 1024);
        LOG_INFO("K2526", "Lade-ROM REAKTIVIERT: BS-PIO B0=/LD-ROM=1 (PortB=0x%02X)", data);
    }

    // ── B2: /SPS-ESR – Q240 Schutztabellen-Schreibmodus ─────────────────
    // B2=0 (active low): ZVE1-Speicherzugriffe gehen in Schutztabelle.
    // B2=1: normaler Betrieb, Q240 prüft Zugriffe gegen Schutztabelle.
    bool sps_esr = !(data & 0x04);
    if (spa_.isTableWriteEnabled() != sps_esr) {
        spa_.enableTableWrite(sps_esr);
        LOG_INFO("K2526", "Q240 /SPS-ESR=%d: Schutztabellen-%s (PortB=0x%02X)",
                 (int)!sps_esr,
                 sps_esr ? "Schreibmodus aktiv" : "Prüfmodus aktiv",
                 data);
    }

    // ── B3: /WAIT-ZVE2 – ZVE2 anhalten/freigeben ─────────────────────────
    // B3=0 (active low): zve2Step() gibt 0 zurück (keine Ausführung).
    // B3=1: ZVE2 läuft normal.
    bool new_wait = !(data & 0x08);
    if (new_wait != zve2_wait_) {
        zve2_wait_ = new_wait;
        LOG_INFO("K2526", "ZVE2 /WAIT-ZVE2=%d: ZVE2 %s (PortB=0x%02X)",
                 (int)!new_wait, new_wait ? "angehalten" : "freigegeben", data);
    }

    // ── B4: /SA – Netzausschaltung ───────────────────────────────────────
    // B4=0 (active low): OS fordert Systemabschaltung an.
    if (!(data & 0x10) && !shutdown_req_) {
        shutdown_req_ = true;
        LOG_WARN("K2526", "BS-PIO B4=/SA aktiv (0x%02X) – Netzausschaltung!", data);
    }
}

// ─── Q240 Protection: Violation Handler ──────────────────────────────────────

/**
 * @brief Handle a ZVE1 memory-protection violation (Q240 logic).
 *
 * Called from the ZVE1 readByte/writeByte callbacks when a protected segment
 * is accessed.  Sets the SPS-Ind flag (BS-PIO A3 input) and asserts NMI so
 * the OS can handle the violation.  The NMI is cleared by /RES-SPA (port 02H).
 */
void K2526::handleSPSViolation()
{
    if (!sps_ind_) {
        sps_ind_ = true;
        spa_.setViolation();
        updatePortAInputs();
        bus_.assertNMI();
        LOG_WARN("K2526", "Q240 SPS-Verletzung! SPS-Ind gesetzt, NMI assertiert");
    }
}

/**
 * @brief Update BS-PIO Port A input bits from current internal state.
 *
 * BS-PIO Port A bits 0–6 are inputs latched by external hardware.
 * A3 (SPS-Ind) reflects the current Q240 violation state; all other input
 * bits are held high (pull-up, no hardware connected in the emulator).
 * Call this whenever sps_ind_ changes.
 */
void K2526::updatePortAInputs()
{
    if (sps_ind_)
        port_a_inputs_ |= 0x08;    // A3=1: SPS violation active
    else
        port_a_inputs_ &= ~0x08u;  // A3=0: no violation
    bs_pio_.portAWrite(port_a_inputs_);
}

/**
 * @brief Assemble the current BS-PIO Port B input byte.
 *
 * Port B input bits (direction mask bit = 1): B1 (/INT-BS), B5–B7 (Config).
 * B1 is driven low when the OS requests an OS-level change (port 00H write).
 * B5–B7 reflect the A5120 hardware bridge encoding in cfg_.config_bridges.
 *
 * @return Byte for bs_pio_.portBWrite(): B1 and B5–B7 set, B0/B2/B3/B4 = 0
 */
uint8_t K2526::buildPortBInputs() const
{
    uint8_t val = static_cast<uint8_t>((cfg_.config_bridges & 0x07) << 5);
    if (!int_bs_active_)
        val |= 0x02;   // B1=1 when /INT-BS inactive (pull-up)
    // B1=0 when int_bs_active_ (latch pulled low by port 00H write)
    return val;
}

// ─── InterruptSlave – Interne Daisy-Chain: IEI → CTC → BS-PIO → IEO ─────────
//
// Originalschaltung (Abb. 1):
//   CTC am Ende der 1. Prioritätenkette (Systembus)    – höhere Prio
//   BS-PIO am Ende der 2. Prioritätenkette (Koppelbus) – niedrigere Prio
//
// Emulatorvereinfachung: eine zusammengeführte Kette (CTC vor BS-PIO).

/**
 * @brief Set /IEI from the upstream interrupt chain.
 *
 * Propagates IEI through the internal CTC → BS-PIO daisy-chain.
 * Must be called by the bus or machine run loop whenever upstream IEI changes.
 *
 * @param iei true when this card is enabled by the upstream device
 */
void K2526::setIEI(bool iei)
{
    iei_in_ = iei;
    ctc_.setIEI(iei);
    bs_pio_.setIEI(ctc_.getIEO());
    LOG_TRACE("K2526", "setIEI(%d): CTC.IEI=%d → BS-PIO.IEI=%d",
              (int)iei, (int)iei, (int)ctc_.getIEO());
}

/**
 * @brief Get /IEO to pass to the downstream device in the interrupt chain.
 *
 * Returns false (blocking) if the BS-PIO (the lowest-priority device in the
 * internal chain) is currently servicing an interrupt; otherwise passes through.
 *
 * @return true to allow downstream devices to supply interrupts
 */
bool K2526::getIEO() const
{
    return bs_pio_.getIEO();
}

/**
 * @brief Check whether this card has a pending interrupt.
 *
 * @return true if CTC or BS-PIO has an un-acknowledged interrupt pending
 */
bool K2526::hasInterrupt() const
{
    bool ctc_int = ctc_.hasInterrupt();
    bool pio_int = bs_pio_.hasInterrupt();
    if (ctc_int || pio_int) {
        LOG_DEBUG("K2526", "hasInterrupt: CTC=%d BS-PIO=%d", (int)ctc_int, (int)pio_int);
    }
    return ctc_int || pio_int;
}

/**
 * @brief Return the active interrupt vector (Z80 Mode 2).
 *
 * CTC has higher priority; if it has a pending interrupt its vector is
 * returned.  Otherwise the BS-PIO vector is returned.
 *
 * @return 8-bit interrupt vector, or 0xFF if no interrupt pending
 */
uint8_t K2526::getVector() const
{
    if (ctc_.hasInterrupt()) {
        uint8_t v = ctc_.getVector();
        LOG_DEBUG("K2526", "getVector: CTC-Vektor=0x%02X", v);
        return v;
    }
    if (bs_pio_.hasInterrupt()) {
        uint8_t v = bs_pio_.getVector();
        LOG_DEBUG("K2526", "getVector: BS-PIO-Vektor=0x%02X", v);
        return v;
    }
    return 0xFF;
}

/**
 * @brief Propagate RETI to the internal interrupt chain.
 *
 * Clears the Interrupt Under Service (IUS) flag in CTC and BS-PIO so that
 * the next lower-priority interrupt can be acknowledged.
 */
void K2526::onRETI()
{
    ctc_.onRETI();
    bs_pio_.onRETI();
    LOG_DEBUG("K2526", "RETI: weitergeleitet an CTC und BS-PIO");
}
