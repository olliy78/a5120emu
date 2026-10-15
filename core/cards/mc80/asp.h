/**
 * @file asp.h
 * @brief ASP (Peripherieanschluss 50300.3050) des MC80.3x: SIO D0H–D3H, CTC D4H–D7H,
 *        PIO D8H–DBH; SIO A = Tastatur (IFSS XS502), SIO B = IFSS, PIO = IFSP.
 *
 * Quellen: doc/mc80/hardware_3x.md §6 (AP M0), doc/mc80/egos.md §3 (AP M1).
 *
 * **Ports** [BDA S.18]: D0 SIO-A-Daten, D1 SIO-A-Steuer, D2/D3 Kanal B; D4–D7 CTC K0–K3;
 * D8 PIO-A-Daten, **D9 PIO-A-Steuer**, DA/DB Kanal B (anders als bei der ZVE-PIO).  Die
 * Bausteine zählen genauso (0 = A-Daten, 1 = A-Steuer, 2 = B-Daten, 3 = B-Steuer).
 *
 * **Interruptkette in der Karte:** IEI → SIO → CTC → PIO → IEO [BDA S.18, HW88 Bl.6].
 * EGOS setzt SIO-WR2 = F0H (Status beeinflusst Vektor: Rx A = FFFCH) und CTC-Vektor E8H
 * (K3 = FFEEH, K1 = FFEAH).
 *
 * **Takte:** CTC K0/K1 zählen den halben Systemtakt und liefern über je ein ÷2-Flipflop den
 * ×16-Takt der SIO A/B (hardware_3x.md §6.2).  Die SIO arbeitet hier auf **Zeichenebene**
 * (wie bei den übrigen Maschinen): Zeichen kommen über @ref empfange an, gesendete holt
 * die Tastatur über @ref nimmGesendet ab; die Zeichendauer setzt die Gegenstelle.  Die
 * Zählerkanäle K0–K2 bekommen deshalb keinen Eingangstakt; K3 (Zeitgeber) läuft im
 * Systemtakt.
 *
 * Kanal A: /DCD und /CTS sind fest aktiv (die Tastatur steht immer; WR3 = 61H verlangt
 * Auto-Enables).  Kanal B: offen (kein IFSS-Gerät) [Annahme].
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_sio.h"
#include <cstdint>

class Mc80Asp : public BusDevice, public InterruptSlave {
public:
    struct Config {
        uint8_t io_base = 0xD0;   ///< AB4–AB7 per Wickel, Vorgabe D0H–DFH
    };

    Mc80Asp();
    explicit Mc80Asp(const Config& cfg);

    void attachToBus(K1520Bus& bus);
    void reset();
    /// Systemtakte; true = Interruptzustand kann sich geändert haben.
    bool clockTick(int ticks) { return ctc_.clockTick(ticks); }

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80 ASP"; }

    // ─── InterruptSlave ──────────────────────────────────────────────────────
    void        setIEI(bool iei) override;
    bool        getIEO() const override { return pio_.getIEO(); }
    bool        hasInterrupt() const override;
    uint8_t     getVector() const override;
    void        onRETI() override;
    const char* intDeviceName() const override;

    // ─── Tastaturkanal (SIO A) ───────────────────────────────────────────────
    /// Empfänger frei (Rx-FIFO nicht voll, Rx ein)?
    bool tastaturEmpfaengerFrei() const;
    /// Zeichen der Tastatur an SIO A übergeben.
    void empfange(uint8_t b);
    /// Hat der Rechner an SIO A ein Zeichen gesendet?  Dann holen (7 Bit).
    bool hatGesendet() const;
    uint8_t nimmGesendet();

    Z80SIO& sio() { return sio_; }
    Z80CTC& ctc() { return ctc_; }
    Z80PIO& pio() { return pio_; }

private:
    void updateInternalChain();

    Config cfg_;
    Z80SIO sio_{"MC80-ASP-SIO"};
    Z80CTC ctc_{"MC80-ASP-CTC"};
    Z80PIO pio_{"MC80-ASP-PIO"};
    bool   iei_in_ = false;
};
