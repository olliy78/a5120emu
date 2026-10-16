/**
 * @file senke.h
 * @brief Schreibsenke fuer E/A-Tore ohne Nachbildung (MC80.2x, AP M11).
 *
 * Belegt im MC80.21/.22 (doc/mc80/hardware_2x.md §3/§4/§8):
 *   - **00H–0FH „intern belegt“**, darunter 01H/02H = Schreibfreigabe/-sperre der Steckeinheit
 *     „4 KByte dRAM mit Schreibsperre“ (U202).  Ob die OPS K3525 der .21/.22 die Sperre hat, sagt
 *     das Handbuch nicht [?]; das Betriebssystem gibt beim Start nur frei (RSP, 60 × OUT 01H) und
 *     sperrt nie → die Sperre wird nicht nachgebildet, die Ausgaben werden nur gezaehlt.
 *   - **E0H–E8H EPROM-Programmiereinheit (PPE)**: ohne Schaltplan nur als Senke (Entwurf 31 §6:
 *     erst in einer spaeteren Etappe).
 *
 * Schreiben wird verworfen (und gezaehlt), Lesen liefert FFH [? offener Datenbus].  Die Tore sind
 * angemeldet, damit keine Zusatzkarte (z. B. die K6022 auf E0H–E7H) dort steckbar ist.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstdint>
#include <string>

class Mc80Senke : public BusDevice {
public:
    Mc80Senke(std::string name, uint8_t basis, uint8_t anzahl)
        : name_(std::move(name)), basis_(basis), anzahl_(anzahl) {}

    void attachToBus(K1520Bus& bus) { bus.registerIO(this, basis_, anzahl_); }

    uint8_t ioRead(uint8_t) override { return 0xFF; }
    void    ioWrite(uint8_t port, uint8_t data) override
    {
        ++schreibzugriffe_[port];
        letzter_[port] = data;
    }
    const char* deviceName() const override { return name_.c_str(); }

    /// Zahl der Schreibzugriffe auf @p port (Diagnose, Tests).
    uint32_t schreibzugriffe(uint8_t port) const { return schreibzugriffe_[port]; }
    uint8_t  letzterWert(uint8_t port) const { return letzter_[port]; }

private:
    std::string name_;
    uint8_t basis_, anzahl_;
    std::array<uint32_t, 256> schreibzugriffe_{};
    std::array<uint8_t, 256> letzter_{};
};
