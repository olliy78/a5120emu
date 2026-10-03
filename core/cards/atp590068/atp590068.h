/**
 * @file atp590068.h
 * @brief ATP 590068 — „Adapter für Tastatur und Programmierzusatz“ des PRG 710.
 *
 * Belegt (doc/design/20_prg710.md §3.4, doc/prg710/resident.md §5):
 * - **8279 an C8H (Daten) / C9H (Befehl/Status)** für die Tastatur K7609.
 * - **EPROMmer D0H–D4H** (AP-P7b): PIO + Steuerregister + virtueller Sockel,
 *   @ref Eprommer590068 (doc/prg710/eprommer.md).
 * Das Peripheriemodell (Tasten) hängt die Maschine an @ref kbc.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/i8279.h"
#include "core/cards/atp590068/eprommer590068.h"
#include <cstdint>

class Atp590068 {
public:
    struct Config {
        uint8_t kbc_base = 0xC8;       ///< 8279 (Daten, Befehl/Status)
        uint8_t eprommer_base = 0xD0;  ///< EPROMmer (PIO D0H–D3H, Steuerregister D4H)
    };

    Atp590068() : Atp590068(Config{}) {}
    explicit Atp590068(const Config& cfg);

    /** @brief Tastatur und EPROMmer am Systembus anmelden. */
    void attachToBus(K1520Bus& bus) { attachTastatur(bus); attachEprommer(bus); }
    /** @brief Nur den 8279 (C8H/C9H) anmelden. */
    void attachTastatur(K1520Bus& bus);
    /** @brief Nur den EPROMmer (D0H–D4H) anmelden. */
    void attachEprommer(K1520Bus& bus) { eprommer_.attachToBus(bus); }
    /** @brief /RESET. */
    void reset() { kbc_.reset(); eprommer_.reset(); }

    I8279& kbc() { return kbc_; }
    const I8279& kbc() const { return kbc_; }
    Eprommer590068& eprommer() { return eprommer_; }
    const Config& config() const { return cfg_; }

private:
    struct Tastatur : BusDevice {
        explicit Tastatur(I8279& k) : kbc(k) {}
        uint8_t ioRead(uint8_t port) override { return (port & 1) ? kbc.readStatus() : kbc.readData(); }
        void ioWrite(uint8_t port, uint8_t d) override {
            if (port & 1) kbc.writeCommand(d); else kbc.writeData(d);
        }
        const char* deviceName() const override { return "ATP 590068 8279"; }
        I8279& kbc;
    };

    Config   cfg_;
    I8279    kbc_;
    Tastatur tastatur_{kbc_};
    Eprommer590068 eprommer_;
};
