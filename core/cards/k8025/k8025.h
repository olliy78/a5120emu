#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_sio.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <cstdint>
#include <functional>

// K8025 ASS – Anschlußsteuerung Seriell (Serial Interface Card)
//
// Hardware composition:
//   SIO A33 (ports 0x50–0x53): DFÜ channel A, unused channel B
//   PIO A31 (ports 0x54–0x57): DIL switch readout for DFÜ config
//   CTC A34 (ports 0x58–0x5B): baud rate generator
//   SIO A32 (ports 0x5C–0x5F): channel A = keyboard K7637, channel B = printer
//
// Internal interrupt chain: SIO A33 → SIO A32 → CTC A34 → /IEO out

class K8025 : public BusDevice, public InterruptSlave {
public:
    struct A5120Config {
        uint8_t io_base;
        A5120Config() : io_base(0x50) {}
    };

    K8025(K1520Bus& bus, const A5120Config& cfg = A5120Config{});

    // BusDevice: 16 ports (0x50–0x5F)
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K8025"; }

    // InterruptSlave (daisy-chain)
    void    setIEI(bool iei) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // Keyboard interface (SIO A32, channel A, X4)
    void    keyboardRxByte(uint8_t byte);   // inject byte from K7637
    bool    keyboardTxAvailable();          // byte waiting for keyboard (LED cmds)
    uint8_t keyboardTxGet();

    // DFÜ interface (SIO A33, channel A)
    using SerialCallback = std::function<void(uint8_t)>;
    void    dfueRxByte(uint8_t byte);       // inject byte from modem/host
    bool    dfueTxAvailable();              // CPU sent a byte to modem
    uint8_t dfueTxGet();
    void    setDFUERxCallback(SerialCallback cb);

    // Printer interface (SIO A32, channel B, X3)
    bool    printerTxAvailable();
    uint8_t printerTxGet();

    // CTC clock tick – call once per system-clock period from the machine loop
    void    clockTick();

    // Sub-chip accessors (useful for testing and wiring)
    Z80SIO& sioA33() { return sio_dfue_; }
    Z80SIO& sioA32() { return sio_kbd_printer_; }
    Z80CTC& ctcA34() { return ctc_a34_; }

private:
    // Propagate IEI through the internal daisy chain.
    // Must be called after any operation that may change interrupt state.
    void updateInternalChain();

    A5120Config    cfg_;
    Z80SIO         sio_dfue_        {"K8025-SIO-A33"};
    Z80SIO         sio_kbd_printer_ {"K8025-SIO-A32"};
    Z80CTC         ctc_a34_         {"K8025-CTC-A34"};
    Z80PIO         pio_a31_         {"K8025-PIO-A31"};
    bool           iei_in_  = false;
    SerialCallback dfue_rx_cb_;
};
