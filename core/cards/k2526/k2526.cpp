#include "core/cards/k2526/k2526.h"

// ─── Construction ────────────────────────────────────────────────────────────

K2526::K2526(K1520Bus& bus)
    : K2526(bus, A5120Config{}) {}

K2526::K2526(K1520Bus& bus, const A5120Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    // Register BS-PIO Port B output callback so ROM/MEMDI control writes are
    // intercepted immediately when the CPU writes to Port B data register.
    bs_pio_.setPortBOutputCallback([this](uint8_t data) {
        onBSPIOPortBOut(data);
    });
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void K2526::attachToBus(K1520Bus& bus)
{
    // Register this device to handle all 16 I/O ports (0x00–0x0F).
    bus.registerIO(this, cfg_.io_base, 16);
}

void K2526::powerOn()
{
    // Configure BS-PIO Port B as output mode (mode 0).
    // Control word: 0x0F = bits[3:0]=1111 (mode word) | bits[7:6]=00 (mode 0 = output).
    // This mirrors what the real boot ROM writes to the PIO at startup.
    bs_pio_.ioWrite(3, 0x0F);  // Port B ctrl → mode 0 (output)

    rom_enabled_ = true;
    bus_.registerMem(&rom_, 0x0000, 1024);
}

void K2526::clockTick()
{
    ctc_.clockTick();
}

// ─── I/O dispatch ────────────────────────────────────────────────────────────
//
// Port range → sub-chip mapping (using bits [3:2]):
//   0x00–0x03 (00) → BS-PIO
//   0x04–0x07 (01) → CTC
//   0x08–0x0B (10) → ZVE1-PIO
//   0x0C–0x0F (11) → ZVE2-PIO
//
// The sub-chip receives the low 2 bits as its internal port index.

uint8_t K2526::ioRead(uint8_t port)
{
    uint8_t sub_port = port & 0x03;
    switch ((port & 0x0C) >> 2) {
        case 0: return bs_pio_.ioRead(sub_port);
        case 1: return ctc_.ioRead(sub_port);
        case 2: return zve1_pio_.ioRead(sub_port);
        case 3: return zve2_pio_.ioRead(sub_port);
        default: break;
    }
    return 0xFF;
}

void K2526::ioWrite(uint8_t port, uint8_t data)
{
    uint8_t sub_port = port & 0x03;
    switch ((port & 0x0C) >> 2) {
        case 0: bs_pio_.ioWrite(sub_port, data);   break;
        case 1: ctc_.ioWrite(sub_port, data);       break;
        case 2: zve1_pio_.ioWrite(sub_port, data);  break;
        case 3: zve2_pio_.ioWrite(sub_port, data);  break;
        default: break;
    }
}

// ─── BS-PIO Port B output handler ────────────────────────────────────────────
//
// Port B bit assignments (outputs written by software):
//   bit 5 – ROM disable: set → unregister boot ROM from bus
//   bit 6 – /MEMDI: set → assert memory protection (bus.setMEMDI(true))
//   bit 7 – /SA: power-off (not emulated)

void K2526::onBSPIOPortBOut(uint8_t data)
{
    // Bit 5: ROM disable (active high – writing 1 disables ROM)
    if ((data & 0x20) && rom_enabled_) {
        rom_enabled_ = false;
        bus_.unregisterMem(&rom_);
    }

    // Bit 6: /MEMDI – memory protection enable
    bus_.setMEMDI((data & 0x40) != 0);
}

// ─── InterruptSlave – delegate to BS-PIO (head of local daisy chain) ─────────

void K2526::setIEI(bool iei)
{
    iei_in_ = iei;
    bs_pio_.setIEI(iei);
}

bool K2526::getIEO() const
{
    // Forward through bs_pio_ → ctc_ → zve1_ → zve2_ and return last IEO.
    // For now we expose bs_pio_'s IEO as the card's IEO.
    return bs_pio_.getIEO();
}

bool K2526::hasInterrupt() const
{
    return bs_pio_.hasInterrupt()  ||
           ctc_.hasInterrupt()     ||
           zve1_pio_.hasInterrupt() ||
           zve2_pio_.hasInterrupt();
}

uint8_t K2526::getVector() const
{
    if (bs_pio_.hasInterrupt())   return bs_pio_.getVector();
    if (ctc_.hasInterrupt())      return ctc_.getVector();
    if (zve1_pio_.hasInterrupt()) return zve1_pio_.getVector();
    if (zve2_pio_.hasInterrupt()) return zve2_pio_.getVector();
    return 0xFF;
}
