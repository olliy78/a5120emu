#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/eprom_device.h"
#include "core/cards/k2526/rom_data.h"
#include <cstdint>

// ─── K2526 ZRE – Zentrale Recheneinheit ──────────────────────────────────────
//
// CPU card for the K1520 / A5120 system. Contains:
//   - Boot ROM (1 KB EPROM, address 0x0000–0x03FF, disabled by software)
//   - BS-PIO  (Betriebssystem-PIO, ports 0x00–0x03)
//   - CTC     (system timer,        ports 0x04–0x07)
//   - ZVE1-PIO (Z80 run control,   ports 0x08–0x0B)
//   - ZVE2-PIO (alternate control,  ports 0x0C–0x0F)
//
// The Z80 CPU itself is NOT owned here; the machine instantiates it
// separately and wires it to the bus.

class K2526 : public BusDevice, public InterruptSlave {
public:
    struct A5120Config {
        uint8_t io_base = 0x00;  // Base I/O address (fixed 0x00)
    };

    explicit K2526(K1520Bus& bus);
    K2526(K1520Bus& bus, const A5120Config& cfg);

    // ─── BusDevice: handles ports 0x00–0x0F ───────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K2526"; }

    // ─── InterruptSlave (daisy-chain, delegates to BS-PIO) ─────────────────
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // ─── Lifecycle ─────────────────────────────────────────────────────────
    // Register ROM and I/O ports with the bus.
    void attachToBus(K1520Bus& bus);

    // Enable ROM (called at power-on / reset).
    void powerOn();

    // Forward system clock to CTC.
    void clockTick();

    // ─── State queries ─────────────────────────────────────────────────────
    bool isRomEnabled() const { return rom_enabled_; }

    // ─── Sub-chip accessors (for machine wiring and tests) ─────────────────
    Z80PIO& bsPio()   { return bs_pio_; }
    Z80CTC& ctc()     { return ctc_; }
    Z80PIO& zve1Pio() { return zve1_pio_; }
    Z80PIO& zve2Pio() { return zve2_pio_; }

private:
    // Called when the CPU writes to BS-PIO Port B.
    void onBSPIOPortBOut(uint8_t data);

    K1520Bus&   bus_;
    A5120Config cfg_;

    Z80PIO bs_pio_  {"K2526-BS-PIO"};
    Z80CTC ctc_     {"K2526-CTC"};
    Z80PIO zve1_pio_{"K2526-ZVE1"};
    Z80PIO zve2_pio_{"K2526-ZVE2"};

    EPROMDevice<1024> rom_{ZRE_BOOT_ROM};

    bool rom_enabled_ = true;
    bool iei_in_      = false;
};
