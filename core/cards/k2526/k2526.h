/**
 * @file k2526.h
 * @brief K2526 ZRE – Zentrale Recheneinheit (CPU card) emulation.
 *
 * Emulates the K2526 CPU card for the Robotron K1520/A5120 system.
 * The physical card contains two Z80 CPUs (ZVE1 main, ZVE2 DMA), a 1 KB
 * boot EPROM, a Z80 PIO (BS-PIO, Q301), a Z80 CTC (Q302), and U-Bus
 * keyboard/lamp interface logic.  In this emulation the Z80 CPU (ZVE1)
 * lives outside this class — it is owned by A5120Machine and wired to
 * K1520Bus directly.  This class manages all on-card I/O peripherals.
 *
 * I/O port assignment (AB7–AB4 = 0, base 0x00, range 0x00–0x0F):
 * @code
 *   00H W  /INT-BS (BS-PIO interrupt routing, not implemented)
 *   01H R  /UCS2: keyboard valid flag (U-Bus)
 *   02H W  /RES-SPA: memory-protect reset (not implemented)
 *   03H W  /UCS4: error lamp control (U-Bus)
 *   04H W  /RES-ZVE2: DMA-CPU reset (not implemented)
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
 *     A2 /NMI     – NMI source (input)
 *     A3 SPS-Ind  – memory-protect violation indicator (input)
 *     A4 /EBF     – single-step end (input)
 *     A5 /WR      – memory test WR signal (input)
 *     A6 /RDY     – memory/peripheral ready (input)
 *     A7 MEMDI1/2 – memory-disable output (active high → bus_.setMEMDI(true))
 *   Port B (mixed):
 *     B0 /LD-ROM  – boot ROM control (output): 1=ROM active, 0=ROM disabled
 *     B1 /INT-BS  – OS-level change request (input)
 *     B2 /SPS-ESR – memory-protect RAM write enable (output)
 *     B3 /WAIT-ZVE2 – WAIT for DMA CPU (output)
 *     B4 /SA      – power-off signal (output)
 *     B5–B7 Config – hardware bridge readout (input)
 * @endcode
 *
 * /LD-ROM polarity:
 *   B0 = 1 (pull-up at power-on) → ROM mapped at 0x0000–0x03FF (read-only)
 *   B0 = 0 (BIOS writes this)   → ROM unmapped; 0x0000 accessible as RAM
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
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/eprom_device.h"
#include "core/cards/k2526/rom_data.h"
#include <cstdint>

/**
 * @class K2526
 * @brief Emulation of the K2526 ZRE (Zentrale Recheneinheit) CPU card.
 *
 * Features:
 * - Boot EPROM (1 KB, 0x0000–0x03FF), disabled by BS-PIO Port B bit 0 (/LD-ROM)
 * - BS-PIO (Q301): controls MEMDI1/2 (Port A bit 7) and system config bridges
 * - CTC (Q302): 4-channel timer, baud-rate source; ZC/TO2 feeds CLK/TRG3 internally
 * - U-Bus registers: keyboard scan-code / valid-flag inputs, lamp outputs
 * - InterruptSlave: combined CTC → BS-PIO daisy-chain for the K1520 interrupt system
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
     * @brief BS-PIO Port B output callback – handles /LD-ROM and other outputs.
     *
     * Called whenever the CPU writes to BS-PIO Port B (port 0x0A).
     * Bit 0 (/LD-ROM, active low): 1 → ROM stays mapped, 0 → unmap ROM.
     * Implements idempotent unmapping: a second B0=0 write is a no-op.
     *
     * @param data Byte written to BS-PIO Port B output register
     */
    void onBSPIOPortBOut(uint8_t data);

    K1520Bus&   bus_;           ///< K1520 system bus reference
    A5120Config cfg_;           ///< Hardware configuration (bridge bits, I/O base)

    Z80PIO bs_pio_{"K2526-BS-PIO"};     ///< Q301: BS-PIO (Betriebssystem-PIO), ports 08H–0BH
    Z80CTC ctc_   {"K2526-CTC"};        ///< Q302: CTC (Zähler/Zeitgeber),       ports 0CH–0FH

    EPROMDevice<1024> rom_{ZRE_BOOT_ROM};  ///< 1 KB boot EPROM at 0x0000–0x03FF

    bool    rom_enabled_ = true;    ///< true while EPROM is registered on the bus
    bool    iei_in_      = false;   ///< Last IEI value received from upstream chain

    // U-Bus registers (keyboard/lamp interface, connector X5)
    uint8_t ubus_kbd_code_   = 0xFF;  ///< Port 06H IN:  keyboard scan code (/UCS1)
    uint8_t ubus_kbd_valid_  = 0x00;  ///< Port 01H IN:  key valid flag      (/UCS2)
    uint8_t ubus_lamp_       = 0x00;  ///< Port 05H OUT: status lamp output  (/UCS3)
    uint8_t ubus_error_lamp_ = 0x00;  ///< Port 03H OUT: error  lamp output  (/UCS4)
};
