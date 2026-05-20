#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/eprom_device.h"
#include "core/cards/k2526/rom_data.h"
#include <cstdint>

// ─── K2526 ZRE – Zentrale Recheneinheit ──────────────────────────────────────
//
// CPU-Karte für das K1520-/A5120-System. Enthält laut Originaldokumentation:
//   - Lade-ROM  (1 KB EPROM A26, Adresse 0000H–03FFH, abschaltbar per SW)
//   - U-Bus     (Uni-Bus Tastatur-Interface, Ports 00H–07H)
//       01H /UCS2: Tastengültigkeitsabfrage (IN)
//       03H /UCS4: Fehlerlampe (OUT)
//       04H /RES-ZVE2: DMA-CPU Reset (OUT)
//       05H /UCS3: Lampenansteuerung (OUT)
//       06H /UCS1: Tastencode lesen (IN)
//   - BS-PIO    (Q301, Betriebssystem-PIO, Ports 08H–0BH, Bitmode/Mode 3)
//       Port A (Eingänge): /M1, /SUE, /NMI, SPS-Ind, /EBF, /WR, /RDY, MEMDI1/2
//       Port B: /LD-ROM(B0 OUT), /INT-BS(B1 IN), /SPS-ESR(B2 OUT),
//               /WAIT-ZVE2(B3 OUT), /SA(B4 OUT), Config(B5-B7 IN)
//   - CTC       (Q302, Zähler/Zeitgeber, Ports 0CH–0FH)
//       Kanal 0–3; ZC/TO2 intern mit CLK/TRG3 verdrahtet
//
// Prioritätenkette laut Originaldokumentation (Abb. 1):
//   1. Kette (Systembus): … → CTC → IEO1(Koppelbus) → IEI1 → BS-PIO → IEO1
//   CTC hat höhere Priorität als BS-PIO.
//
// Der Z80-Prozessor (ZVE1) liegt NICHT hier; die Machine instanziiert ihn
// separat und verdrahtet ihn mit dem K1520Bus.

class K2526 : public BusDevice, public InterruptSlave {
public:
    struct A5120Config {
        uint8_t io_base        = 0x00;   // Basis I/O-Adresse (fest 00H)
        uint8_t config_bridges = 0x00;   // BS-PIO B5-B7 Brücken-Codierung
    };

    explicit K2526(K1520Bus& bus);
    K2526(K1520Bus& bus, const A5120Config& cfg);

    // ─── BusDevice: Ports 00H–0FH ──────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K2526"; }

    // ─── InterruptSlave (interne Kette: IEI → CTC → BS-PIO → IEO) ────────
    // Vereinfachung gegenüber Original: beide Ketten in einer zusammengefasst.
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;
    void    onRETI() override;

    // ─── Lifecycle ─────────────────────────────────────────────────────────
    void attachToBus(K1520Bus& bus);
    void powerOn();
    void clockTick();

    // ─── State queries ─────────────────────────────────────────────────────
    bool isRomEnabled() const { return rom_enabled_; }

    // ─── Sub-Chip-Accessoren (für Maschinenverdrahtung und Tests) ───────────
    Z80PIO& bsPio() { return bs_pio_; }
    Z80CTC& ctc()   { return ctc_; }

    // ─── U-Bus-Schnittstelle (Tastatur-Eingabe von außen) ──────────────────
    void    setKbdCode(uint8_t code)  { ubus_kbd_code_  = code; }
    void    setKbdValid(uint8_t val)  { ubus_kbd_valid_ = val;  }

private:
    void onBSPIOPortBOut(uint8_t data);

    K1520Bus&   bus_;
    A5120Config cfg_;

    Z80PIO bs_pio_{"K2526-BS-PIO"};
    Z80CTC ctc_   {"K2526-CTC"};

    EPROMDevice<1024> rom_{ZRE_BOOT_ROM};

    bool    rom_enabled_ = true;
    bool    iei_in_      = false;

    // U-Bus-Register (Tastatur-Interface an Steckverbinder X5)
    uint8_t ubus_kbd_code_   = 0xFF;  // Port 06H IN: Tastencode (/UCS1)
    uint8_t ubus_kbd_valid_  = 0x00;  // Port 01H IN: Gültig? (/UCS2)
    uint8_t ubus_lamp_       = 0x00;  // Port 05H OUT: Lampen (/UCS3)
    uint8_t ubus_error_lamp_ = 0x00;  // Port 03H OUT: Fehlerlampe (/UCS4)
};
