/**
 * @file k2526.h
 * @brief K2526 ZRE – Zentrale Recheneinheit (CPU card) emulation.
 *
 * Emulates the K2526 CPU card for the Robotron K1520/A5120 system.
 * The physical card contains two Z80 CPUs (ZVE1 main, ZVE2 DMA), a 1 KB
 * boot EPROM, a Z80 PIO (BS-PIO, Q301), a Z80 CTC (Q302), U-Bus keyboard/
 * lamp interface logic, and the Q240 Speicher-/E/A-Schutz (memory/IO
 * protection logic).
 *
 * ZVE1 is owned by this class (cpu_ member) and exposed via
 * cpuStep()/cpuReset()/cpuInterrupt() etc.  ZVE2 is a fully emulated Z80
 * DMA-CPU (zve2_ member) that shares the K1520 bus with ZVE1.  While
 * /BUSRQ is asserted and ZVE2 is not in reset, the run loop calls
 * zve2Step() to execute DMA transfer code.
 *
 * I/O port assignment (AB7–AB4 = 0, base 0x00, range 0x00–0x0F):
 * @code
 *   00H W  /INT-BS trigger: asserting OS-level-change interrupt via BS-PIO B1
 *   01H R  /UCS2: keyboard valid flag (U-Bus)
 *   02H W  /RES-SPA: memory-protect violation reset (clears SPS-Ind / NMI)
 *   03H W  /UCS4: error lamp control (U-Bus)
 *   04H W  /RES-ZVE2: DMA-CPU reset (bit0=0 → reset; bit0=1 → run from PC=0)
 *   05H W  /UCS3: status lamp control (U-Bus)
 *   06H R  /UCS1: keyboard scan code (U-Bus)
 *   07H —  reserved
 *   08H R/W  BS-PIO Port A data
 *   09H R/W  BS-PIO Port A control
 *   0AH R/W  BS-PIO Port B data
 *   0BH R/W  BS-PIO Port B control
 *   0CH R/W  CTC channel 0
 *   0DH R/W  CTC channel 1
 *   0EH R/W  CTC channel 2
 *   0FH R/W  CTC channel 3
 * @endcode
 *
 * BS-PIO (Q301) port directions (Mode 3 – Bit Control):
 * @code
 *   Port A direction 0x7F: bit7 = output (MEMDI1/2), bits 6–0 = inputs
 *     A0 /M1      – single-step trigger (input)
 *     A1 /SUE     – supply voltage monitor (input)
 *     A2 /NMI     – NMI source (input, driven by Q240 on violation)
 *     A3 SPS-Ind  – memory-protect violation indicator (input, from Q240)
 *     A4 /EBF     – single-step end (input)
 *     A5 /WR      – memory test WR signal (input)
 *     A6 /RDY     – memory/peripheral ready (input)
 *     A7 MEMDI1/2 – memory-disable output (active high → bus_.setMEMDI(true))
 *   Port B (mixed):
 *     B0 /LD-ROM    – boot ROM control (output): 1=ROM active, 0=ROM disabled
 *     B1 /INT-BS    – OS-level change request (input from external latch)
 *     B2 /SPS-ESR   – memory-protect table write enable (output to Q240)
 *     B3 /WAIT-ZVE2 – WAIT signal for ZVE2 DMA-CPU (output, active low)
 *     B4 /SA        – Sonderausgang: system power-off request (output)
 *     B5–B7 Config  – hardware bridge readout (input)
 * @endcode
 *
 * /LD-ROM polarity:
 *   B0 = 1 (pull-up at power-on) → ROM mapped at 0x0000–0x03FF (read-only)
 *   B0 = 0 (BIOS writes this)   → ROM unmapped; 0x0000 accessible as RAM
 *
 * Q240 Memory Protection:
 *   The Q240 chip divides the 64 KB address space into 1024 segments of
 *   64 bytes each.  Each segment has independent read-protect (bit 1) and
 *   write-protect (bit 0) flags stored in the Q240's internal 1 KB SRAM.
 *   When /SPS-ESR (B2) is asserted (active low), CPU memory writes go to
 *   the protection table instead of actual memory; when inactive, normal
 *   accesses are checked against the table.  A violation sets SPS-Ind
 *   (BS-PIO A3) and triggers NMI.  /RES-SPA (port 02H) clears the indicator.
 *
 * Interrupt priority within the K2526 card (combined into one chain):
 *   External IEI → CTC (higher priority) → BS-PIO → External IEO
 *
 * @see doc/design/03_k2526_zre.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/eprom_device.h"
#include "core/cards/k2526/rom_data.h"
#include <array>
#include <cstdint>

/**
 * @class MemIOProtect
 * @brief Q240 Speicher-/E/A-Schutz (Memory/IO Protection) emulation.
 *
 * The Q240 chip divides the 64 KB address space into 1024 segments of
 * 64 bytes each and protects individual segments from unauthorised read
 * and/or write access.  Protection is stored in an internal 1 KB SRAM
 * (one byte per segment): bit 0 = write-protect, bit 1 = read-protect.
 *
 * Operation modes:
 * @code
 *   /SPS-ESR inactive (B2=1): normal operation
 *     – CPU memory accesses are checked against the protection table
 *     – Violation: SPS-Ind flag set, NMI asserted
 *   /SPS-ESR active   (B2=0): protection-table programming mode
 *     – CPU writes to address A → protection table entry[A>>6] = data
 *     – CPU reads  from address A → protection table entry[A>>6]
 *     – Regular memory is bypassed; protection is NOT enforced
 * @endcode
 *
 * Protection byte layout per segment (index = addr >> 6):
 * @code
 *   bit 0: write-protect (WP) – 1 = write blocked
 *   bit 1: read-protect  (RP) – 1 = read  blocked
 * @endcode
 *
 * Usage in K2526:
 * @code
 *   // Program protection table (e.g. protect segment at 0x0400):
 *   spa_.enableTableWrite(true);       // /SPS-ESR asserted (B2=0)
 *   spa_.writeEntry(0x0400, 0x01);     // WP only, no RP
 *   spa_.enableTableWrite(false);      // /SPS-ESR released
 *
 *   // Check access (called from CPU callbacks):
 *   if (spa_.isWriteProtected(addr)) { handleSPSViolation(); return; }
 *
 *   // Reset violation indicator (/RES-SPA, port 02H):
 *   spa_.resetViolation();
 * @endcode
 *
 * @see K2526::handleSPSViolation()
 * @see doc/design/03_k2526_zre.md Section 7
 */
class MemIOProtect {
public:
    /// Number of 64-byte segments in the full 64 KB address space.
    static constexpr int SEGMENTS = 1024;

    /**
     * @brief Enable or disable protection-table write mode (/SPS-ESR).
     *
     * When enabled (active = true), CPU memory accesses go to the
     * protection table rather than actual memory (table programming mode).
     *
     * @param active true = /SPS-ESR asserted (table write enabled)
     */
    void enableTableWrite(bool active) { table_write_enabled_ = active; }

    /**
     * @brief Query whether protection-table write mode is active.
     * @return true when /SPS-ESR is asserted and table writes are enabled
     */
    bool isTableWriteEnabled() const { return table_write_enabled_; }

    /**
     * @brief Reset violation indicator without clearing the protection table.
     *
     * Called by /RES-SPA (port 02H) to acknowledge a protection violation
     * and allow the Z80 to resume normal operation after the NMI handler.
     */
    void resetViolation() { sps_ind_ = false; }

    /**
     * @brief Full power-on reset: clear all protection entries and violation flag.
     *
     * Called at powerOn() to ensure a clean protection state at system start.
     */
    void reset() {
        prot_table_.fill(0x00);
        sps_ind_             = false;
        table_write_enabled_ = false;
    }

    /**
     * @brief Check whether a memory address is write-protected.
     * @param addr 16-bit bus address
     * @return true if segment containing @p addr has WP bit set
     */
    bool isWriteProtected(uint16_t addr) const {
        return (prot_table_[addr >> 6] & 0x01) != 0;
    }

    /**
     * @brief Check whether a memory address is read-protected.
     * @param addr 16-bit bus address
     * @return true if segment containing @p addr has RP bit set
     */
    bool isReadProtected(uint16_t addr) const {
        return (prot_table_[addr >> 6] & 0x02) != 0;
    }

    /**
     * @brief Write a protection byte to the table (table-write mode only).
     *
     * Stores @p flags at the table entry for the segment containing @p addr.
     * Should only be called when isTableWriteEnabled() is true; K2526
     * ensures this via the CPU callback.
     *
     * @param addr 16-bit address – determines segment index (addr >> 6)
     * @param flags Protection flags: bit0=WP, bit1=RP
     */
    void writeEntry(uint16_t addr, uint8_t flags) {
        prot_table_[addr >> 6] = flags;
    }

    /**
     * @brief Read a protection byte from the table (for readback in table-write mode).
     * @param addr 16-bit address – determines segment index (addr >> 6)
     * @return Protection byte stored for that segment
     */
    uint8_t readEntry(uint16_t addr) const {
        return prot_table_[addr >> 6];
    }

    /**
     * @brief Set the SPS violation indicator flag (called on access violation).
     */
    void setViolation() { sps_ind_ = true; }

    /**
     * @brief Query whether a protection violation has been detected.
     * @return true if a violation occurred since the last resetViolation() call
     */
    bool isSPSViolation() const { return sps_ind_; }

private:
    /// 1024-entry protection table: one byte per 64-byte segment (bit0=WP, bit1=RP).
    std::array<uint8_t, SEGMENTS> prot_table_{};
    bool table_write_enabled_ = false;  ///< true when /SPS-ESR is asserted
    bool sps_ind_             = false;  ///< true after a protection violation
};

/**
 * @class K2526
 * @brief Emulation of the K2526 ZRE (Zentrale Recheneinheit) CPU card.
 *
 * Features:
 * - Boot EPROM (1 KB, 0x0000–0x03FF), disabled by BS-PIO Port B bit 0 (/LD-ROM)
 * - BS-PIO (Q301): controls MEMDI1/2 (Port A bit 7), /INT-BS (B1 input),
 *   /SPS-ESR (B2 output), /WAIT-ZVE2 (B3 output), /SA (B4 output), and
 *   hardware-bridge readout (B5–B7 inputs)
 * - CTC (Q302): 4-channel timer, baud-rate source; ZC/TO2 feeds CLK/TRG3
 * - ZVE1: main Z80 CPU; accesses memory via Q240 protection check
 * - ZVE2: second Z80 DMA-CPU, shares the K1520 bus with ZVE1; stalled by
 *   /WAIT-ZVE2 (B3) and held in reset by port 0x04 bit0=0
 * - Q240 (MemIOProtect): 1024-segment memory protection; violation raises
 *   SPS-Ind (BS-PIO A3) and asserts NMI; /RES-SPA (port 02H) clears it
 * - U-Bus registers: keyboard scan-code / valid-flag inputs, lamp outputs
 * - InterruptSlave: combined CTC → BS-PIO daisy-chain for the K1520 interrupt system
 *
 * ZVE2 DMA protocol:
 *   Port 0x04 (/RES-ZVE2, active-low): writing bit0=0 holds ZVE2 in reset,
 *   writing bit0=1 releases ZVE2 (starts/resumes from PC=0).
 *   BS-PIO B3 (/WAIT-ZVE2, active-low): writing B3=0 stalls ZVE2 (zve2Step()
 *   returns 0), B3=1 resumes ZVE2.
 *   While bus_.isBUSRQ() is true and ZVE2 is not in reset and not waiting,
 *   the A5120Machine run loop calls zve2Step() instead of afs_.dmaUpdate().
 *   ZVE2 shares all memory and I/O with ZVE1 (same bus_ callbacks).
 *
 * Q240 protection (MemIOProtect spa_):
 *   Protection table is written when /SPS-ESR (B2=0) is asserted.  ZVE1 memory
 *   accesses are checked against the table on every read/write.  ZVE2 bypasses
 *   protection (it runs privileged DMA code under OS control).
 *
 * Typical usage:
 * @code
 *   K1520Bus bus;
 *   K2526 zre(bus);
 *   zre.attachToBus(bus);   // register I/O ports 0x00–0x0F
 *   zre.powerOn();           // map boot ROM at 0x0000–0x03FF
 *   // in run loop:
 *   zre.clockTick();         // advance CTC
 * @endcode
 */
class K2526 : public BusDevice, public InterruptSlave {
public:
    /** @brief Hardware configuration (bridge settings, fixed I/O base). */
    struct A5120Config {
        uint8_t io_base        = 0x00;   ///< I/O base address (fixed 0x00 in A5120)
        uint8_t config_bridges = 0x00;   ///< BS-PIO B5–B7 hardware bridge encoding
    };

    /**
     * @brief Construct a K2526 with default A5120 configuration.
     * @param bus K1520 system bus reference (must outlive this object)
     */
    explicit K2526(K1520Bus& bus);

    /**
     * @brief Construct a K2526 with explicit configuration.
     * @param bus K1520 system bus reference
     * @param cfg Hardware bridge/base configuration
     */
    K2526(K1520Bus& bus, const A5120Config& cfg);

    // ─── BusDevice interface (I/O ports 0x00–0x0F) ────────────────────────

    /**
     * @brief Handle an IN instruction to a K2526 I/O port.
     *
     * Dispatches to U-Bus registers (0x00–0x07), BS-PIO (0x08–0x0B),
     * or CTC (0x0C–0x0F).
     *
     * @param port Relative port number (0x00–0x0F)
     * @return Data byte read from the selected sub-device
     */
    uint8_t     ioRead(uint8_t port) override;

    /**
     * @brief Handle an OUT instruction to a K2526 I/O port.
     *
     * Dispatches to U-Bus registers (0x00–0x07), BS-PIO (0x08–0x0B),
     * or CTC (0x0C–0x0F).  Writing to BS-PIO Port B (0x0A) updates
     * the ROM enable state and MEMDI signal.
     *
     * @param port Relative port number (0x00–0x0F)
     * @param data Byte written by CPU
     */
    void        ioWrite(uint8_t port, uint8_t data) override;

    /**
     * @brief Return the device name.
     * @return "K2526"
     */
    const char* deviceName() const override { return "K2526"; }

    // ─── InterruptSlave interface (CTC → BS-PIO daisy-chain) ──────────────

    /**
     * @brief Set /IEI from the upstream interrupt chain.
     *
     * Propagates the IEI signal through the internal CTC → BS-PIO chain.
     *
     * @param iei true when this card is enabled by the upstream device
     */
    void    setIEI(bool iei) override;

    /**
     * @brief Get /IEO to pass to the downstream device.
     *
     * Returns false (blocking) if CTC or BS-PIO has a pending interrupt
     * that is currently being serviced; otherwise passes through.
     *
     * @return true to pass interrupt downstream, false to block chain
     */
    bool    getIEO() const override;

    /**
     * @brief Check whether this card has a pending interrupt.
     * @return true if CTC or BS-PIO has an un-acknowledged interrupt
     */
    bool    hasInterrupt() const override;

    /**
     * @brief Return the active interrupt vector (Z80 Mode 2).
     * @return Interrupt vector from the highest-priority pending source
     */
    uint8_t getVector() const override;

    /**
     * @brief Propagate RETI to the internal interrupt chain.
     *
     * Clears the Interrupt Under Service (IUS) flag in CTC or BS-PIO
     * so that the next lower-priority interrupt can be acknowledged.
     */
    void    onRETI() override;

    // ─── Lifecycle ─────────────────────────────────────────────────────────

    /**
     * @brief Register this card's I/O ports on the bus.
     *
     * Registers 16 ports (0x00–0x0F) on the provided bus.
     * Sets up the BS-PIO Port A and Port B output callbacks.
     * Must be called before powerOn().
     *
     * @param bus K1520 system bus to register on
     */
    void attachToBus(K1520Bus& bus);

    /**
     * @brief Perform power-on initialisation (enable boot ROM).
     *
     * Maps the 1 KB EPROM at 0x0000–0x03FF on the bus.
     * Sets rom_enabled_ = true.  Call this at system power-on and again
     * on every system reset to re-enable the boot ROM.
     */
    void powerOn();

    /**
     * @brief Advance CTC timers by one system clock tick.
     *
     * Must be called once per CPU step from the machine run loop.
     * Drives the CTC channels for baud-rate generation and timer interrupts.
     */
    void clockTick();

    // ─── State queries ─────────────────────────────────────────────────────

    /**
     * @brief Query whether the boot ROM is currently mapped.
     *
     * true  → EPROM is registered on the bus at 0x0000–0x03FF.
     * false → EPROM is unmapped; reads from 0x0000 reach the K3526 RAM.
     *
     * @return Current /LD-ROM enable state
     */
    bool isRomEnabled() const { return rom_enabled_; }

    // ─── Sub-chip accessors ─────────────────────────────────────────────────

    /**
     * @brief Return a reference to the main CPU (ZVE1 / U880D).
     *
     * The Z80 (ZVE1) is physically on the K2526 card.  Exposed for:
     *   - A5120Machine run loop (step, interrupt delivery)
     *   - Unit tests that inspect CPU register state
     *   - Trace callbacks (cpu().traceCallback = ...)
     *
     * @return Reference to the internal Z80 instance
     */
    Z80&     cpu()  { return cpu_; }

    /**
     * @brief Execute one Z80 instruction (ZVE1 step).
     * @return Number of T-cycles consumed by the instruction
     */
    int      cpuStep()                { return cpu_.step(); }

    /**
     * @brief Reset ZVE1 to address 0x0000.
     *
     * Called on /RESET or at power-on (after powerOn() enables the boot ROM).
     */
    void     cpuReset()               { cpu_.reset(); }

    /**
     * @brief Deliver a maskable interrupt (INT) to ZVE1.
     * @param vec Interrupt vector byte (combined with I register in Mode 2)
     */
    void     cpuInterrupt(uint8_t v)  { cpu_.interrupt(v); }

    /**
     * @brief Deliver a non-maskable interrupt (NMI) to ZVE1.
     */
    void     cpuNMI()                 { cpu_.nmi(); }

    /**
     * @brief Read ZVE1's interrupt-enable flag 1 (IFF1).
     * @return true when ZVE1 will accept a maskable interrupt
     */
    bool     cpuIFF1()  const         { return cpu_.IFF1; }

    /**
     * @brief Read ZVE1's current program counter (for diagnostics).
     * @return Current PC value
     */
    uint16_t cpuPC()    const         { return cpu_.PC; }

    /**
     * @brief Return a reference to the BS-PIO (Q301).
     *
     * Used by A5120Machine to wire the CTC ZC/TO2 → CLK/TRG3 connection
     * and by unit tests to inspect internal PIO state.
     *
     * @return Reference to the internal Z80PIO instance
     */
    Z80PIO& bsPio() { return bs_pio_; }

    /**
     * @brief Return a reference to the CTC (Q302).
     *
     * Used by A5120Machine to wire the Koppelbus ZC/TO signal and by
     * unit tests to inspect CTC timer state.
     *
     * @return Reference to the internal Z80CTC instance
     */
    Z80CTC& ctc()   { return ctc_; }

    // ─── ZVE2 DMA-CPU interface ────────────────────────────────────────────

    /**
     * @brief Execute one ZVE2 instruction (DMA-CPU step).
     *
     * Called by the A5120Machine run loop while /BUSRQ is asserted and
     * ZVE2 is not in reset and not waiting (/WAIT-ZVE2 inactive).
     * Returns 0 without executing if /WAIT-ZVE2 is active (BS-PIO B3=0).
     * ZVE2 shares the K1520 bus with ZVE1.
     *
     * @return Number of T-cycles consumed, or 0 if ZVE2 is in WAIT state
     */
    int  zve2Step() { return zve2_wait_ ? 0 : zve2_.step(); }

    /**
     * @brief Check whether ZVE2 is held in reset.
     *
     * @return true if ZVE2 is in reset (port 0x04 bit0 = 0)
     */
    bool isZVE2InReset() const { return zve2_reset_; }

    /**
     * @brief Check whether ZVE2 is stalled by /WAIT-ZVE2.
     *
     * BS-PIO B3=0 (/WAIT-ZVE2 asserted) prevents ZVE2 from executing
     * instructions.  The OS uses this to synchronize bus access with ZVE2.
     *
     * @return true if /WAIT-ZVE2 is asserted (ZVE2 stalled)
     */
    bool isZVE2Waiting() const { return zve2_wait_; }

    /**
     * @brief Direct access to ZVE2 for unit tests.
     * @return Reference to the ZVE2 Z80 instance
     */
    Z80& zve2() { return zve2_; }

    // ─── Q240 Memory Protection interface ─────────────────────────────────

    /**
     * @brief Check whether a Q240 memory-protection violation is pending.
     *
     * Returns true after any ZVE1 memory access was blocked by the protection
     * table.  Cleared by writing port 02H (/RES-SPA).
     *
     * @return true if a violation occurred and has not been reset
     */
    bool isSPSViolation() const { return sps_ind_; }

    /**
     * @brief Direct access to the Q240 protection table for unit tests.
     *
     * Tests use this to pre-program protection entries without going through
     * the full /SPS-ESR write cycle.
     *
     * @return Reference to the MemIOProtect instance
     */
    MemIOProtect& spa() { return spa_; }

    // ─── /SA power-off interface ────────────────────────────────────────────

    /**
     * @brief Check whether the OS has requested a system power-off.
     *
     * When BS-PIO B4=0 (/SA asserted), the OS is requesting a power-off.
     * The host application queries this flag and terminates the emulation loop.
     *
     * @return true if /SA was asserted by the OS via BS-PIO B4
     */
    bool isShutdownRequested() const { return shutdown_req_; }

    // ─── U-Bus interface (keyboard input from outside) ─────────────────────

    /**
     * @brief Inject a keyboard scan code into the U-Bus register (/UCS1).
     *
     * The CPU reads this value via IN port 0x06.
     *
     * @param code Keyboard scan code byte
     */
    void setKbdCode(uint8_t code)  { ubus_kbd_code_  = code; }

    /**
     * @brief Set the keyboard valid flag in the U-Bus register (/UCS2).
     *
     * The CPU reads this value via IN port 0x01 to check whether a
     * new key code is available.
     *
     * @param val Valid flag byte (non-zero = key available)
     */
    void setKbdValid(uint8_t val)  { ubus_kbd_valid_ = val;  }

private:
    /**
     * @brief BS-PIO Port B output callback – decodes all output signals.
     *
     * Called whenever the CPU writes to BS-PIO Port B (port 0x0A).
     * Handles all output bits of Port B:
     *   - B0 /LD-ROM:    1 → ROM mapped,  0 → ROM unmapped
     *   - B2 /SPS-ESR:   0 → Q240 table-write mode active
     *   - B3 /WAIT-ZVE2: 0 → ZVE2 stalled, 1 → ZVE2 running
     *   - B4 /SA:        0 → system power-off requested
     *
     * @param data Byte written to BS-PIO Port B output register
     */
    void onBSPIOPortBOut(uint8_t data);

    /**
     * @brief Handle a ZVE1 memory-protection violation (Q240 logic).
     *
     * Sets the SPS-Ind violation flag, updates the BS-PIO Port A input
     * (A3=1), and asserts NMI to allow the OS to handle the violation.
     * Called from the ZVE1 readByte/writeByte callbacks when a protected
     * segment is accessed.
     */
    void handleSPSViolation();

    /**
     * @brief Update BS-PIO Port A input bits from current internal state.
     *
     * BS-PIO Port A bits 0–6 are inputs driven by external hardware.
     * A3 (SPS-Ind) is driven by the Q240; this method latches the current
     * SPS violation state into the PIO so the CPU can read it via IN 08H.
     * Call whenever sps_ind_ or port_a_inputs_ changes.
     */
    void updatePortAInputs();

    /**
     * @brief Build the current BS-PIO Port B input byte.
     *
     * Assembles the value for Port B input bits (B1, B5–B7) from the
     * current internal state (int_bs_active_, cfg_.config_bridges).
     * Port B output bits (B0, B2, B3, B4) are handled separately by the
     * PIO's output latch.
     *
     * @return Byte suitable for bs_pio_.portBWrite()
     */
    uint8_t buildPortBInputs() const;

    K1520Bus&   bus_;           ///< K1520 system bus reference
    A5120Config cfg_;           ///< Hardware configuration (bridge bits, I/O base)

    Z80    cpu_;                            ///< ZVE1: Haupt-Z80-CPU (U880D, 2.4576 MHz)
    Z80    zve2_;                           ///< ZVE2: DMA-CPU (second Z80, shares bus_)
    Z80PIO bs_pio_{"K2526-BS-PIO"};        ///< Q301: BS-PIO (Betriebssystem-PIO), ports 08H–0BH
    Z80CTC ctc_   {"K2526-CTC"};           ///< Q302: CTC (Zähler/Zeitgeber),       ports 0CH–0FH

    EPROMDevice<1024> rom_{ZRE_BOOT_ROM};  ///< 1 KB boot EPROM at 0x0000–0x03FF

    MemIOProtect spa_{};         ///< Q240: Speicher-/E/A-Schutz (1024 segments × 64 bytes)

    bool    rom_enabled_ = true;    ///< true while EPROM is registered on the bus
    bool    iei_in_      = false;   ///< Last IEI value received from upstream chain
    bool    zve2_reset_  = true;    ///< true = ZVE2 held in reset (port 0x04 bit0=0)
    bool    zve2_wait_   = false;   ///< true = ZVE2 stalled (/WAIT-ZVE2 B3=0)
    bool    sps_ind_     = false;   ///< true = Q240 violation occurred (SPS-Ind, A3)
    bool    shutdown_req_= false;   ///< true = /SA asserted (B4=0, power-off request)
    bool    int_bs_active_= false;  ///< true = /INT-BS active (B1=0 input to BS-PIO)

    uint8_t port_a_inputs_ = 0xFF;  ///< Current Port A input values (bits 0–6)

    // U-Bus registers (keyboard/lamp interface, connector X5)
    uint8_t ubus_kbd_code_   = 0xFF;  ///< Port 06H IN:  keyboard scan code (/UCS1)
    uint8_t ubus_kbd_valid_  = 0x00;  ///< Port 01H IN:  key valid flag      (/UCS2)
    uint8_t ubus_lamp_       = 0x00;  ///< Port 05H OUT: status lamp output  (/UCS3)
    uint8_t ubus_error_lamp_ = 0x00;  ///< Port 03H OUT: error  lamp output  (/UCS4)
};
