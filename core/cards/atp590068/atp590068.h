/**
 * @file atp590068.h
 * @brief ATP 590068 — „Adapter für Tastatur und Programmierzusatz“ des PRG 710.
 *
 * Belegt (doc/design/20_prg710.md §3.4, doc/prg710/resident.md §5):
 * - **8279 an C8H (Daten) / C9H (Befehl/Status)** für die Tastatur K7609.
 * - **EPROMmer D0H–D3H: Attrappe mit Protokoll** — Lesen FFH, Schreiben wird
 *   mit `LOG_DEBUG` protokolliert (Etappe 7, §8.6).
 * Das Peripheriemodell (Tasten) hängt die Maschine an @ref kbc.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/i8279.h"
#include <cstdint>

class Atp590068 {
public:
    struct Config {
        uint8_t kbc_base = 0xC8;       ///< 8279 (Daten, Befehl/Status)
        uint8_t eprommer_base = 0xD0;  ///< EPROMmer-Attrappe (4 Ports)
    };

    Atp590068() : Atp590068(Config{}) {}
    explicit Atp590068(const Config& cfg);

    /** @brief Ports am Systembus anmelden. */
    void attachToBus(K1520Bus& bus);
    /** @brief /RESET. */
    void reset() { kbc_.reset(); }

    I8279& kbc() { return kbc_; }
    const I8279& kbc() const { return kbc_; }
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
    struct Eprommer : BusDevice {
        void ioWrite(uint8_t port, uint8_t d) override;
        const char* deviceName() const override { return "ATP 590068 EPROMmer (Attrappe)"; }
    };

    Config   cfg_;
    I8279    kbc_;
    Tastatur tastatur_{kbc_};
    Eprommer eprommer_;
};
