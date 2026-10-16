/**
 * @file k3525.h
 * @brief K3525 – 16-KB-dRAM-Steckeinheit (OPS) der fruehen A5120 („5¼″ alt“).
 *
 * Herkunft: doc/k8915g2/karten.md §5 (Auswertung der Betriebsdokumentation
 * Speichersteckeinheiten K3520/K3521/K3525/K3820, gedr. S. 17–21), Entwurf 30 §2/§4.
 *
 * Belegt: 16 KB dynamisches RAM (32 × Q250, 4K×1); KEINE eigene Refresh-Steuerung
 * (Refresh ueber AB0–AB6 und /RFSH vom Bus — im Modell ohne Wirkung, der Inhalt
 * verfaellt nicht); Startadresse ueber Brueckenfelder X8/X9 im 4-KB-Raster, die 16 KB
 * liegen zusammenhaengend; /MEMDI1/2 ueber X6/X7 sperrend; WAIT-Bruecke X10–X11
 * (WAIT im M1) bzw. X11–X12 (keine WAIT-Bildung).
 * Vier Karten mit Basis 0000/4000/8000/C000 ergeben 64 KB (Kartenreihe „5¼″ alt“).
 *
 * [?] Die genaue Zuordnung der X6/X7-Stellungen zu MEMDI1/MEMDI2 ist nicht gelesen;
 * das Modell kennt nur die Wahl „Bus-/MEMDI“ ↔ „MEMDI der Rueckverdrahtung“ wie die
 * K3526.  Die WAIT-Bruecke wird nur als Konfiguration gefuehrt (@ref Config::wait_im_m1);
 * der Emulator kennt keine Wait-Zustaende einer Speicherkarte.
 *
 * Verhalten wie K3526: gesperrt → Lesen 0xFF, Schreiben verworfen; isWritable() = true,
 * damit das Lade-ROM der K2526 das RAM beim Lesen ueberdecken darf, Schreiben aber
 * durchkommt.  Power-on-Inhalt 0xFF (wie K3526; das Lade-ROM verlaesst sich darauf).
 *
 * Nicht in A5120Machine eingebaut (AP A5).
 */

#pragma once
#include <array>
#include <cstdint>
#include "core/bus/k1520_bus.h"

class K3525 : public MemDevice {
public:
    static constexpr uint32_t kGroesse = 0x4000;   ///< 16 KB
    static constexpr uint32_t kRaster  = 0x1000;   ///< Startadresse im 4-KB-Raster (X8/X9)

    struct Config {
        uint16_t base_addr    = 0x0000;  ///< 4-KB-ausgerichtet, base+16 KB <= 64 KB
        bool     memdi_source = false;   ///< false = Bus-/MEMDI, true = MEMDI1/2 der Rueckverdrahtung
        bool     wait_im_m1   = false;   ///< X10–X11 geschlossen; false = X11–X12 (keine WAIT-Bildung)
    };

    K3525();
    /** @throws std::invalid_argument bei unausgerichteter oder zu hoher Basisadresse. */
    explicit K3525(const Config& cfg);

    uint8_t memRead(uint16_t addr) override;
    void    memWrite(uint16_t addr, uint8_t data) override;
    bool    isWritable() const override { return true; }

    /** @brief Belegt 16 KB ab Basisadresse auf dem Bus (merkt sich den Bus fuer Bus-/MEMDI). */
    void attachToBus(K1520Bus& bus);

    /** @brief /MEMDI1/2 der Rueckverdrahtung: gesperrt → Lesen 0xFF, Schreiben verworfen. */
    void setMemDI(bool disabled) { memdi_ = disabled; }
    bool memDI() const { return memdi_; }

    const Config& config() const { return cfg_; }

    void fill(uint8_t value = 0x00);
    /** @brief Direktes Laden (umgeht /MEMDI); @p offset ist relativ zur Kartenbasis. */
    void load(uint16_t offset, const uint8_t* data, uint16_t len);
    const uint8_t* rawPtr() const { return mem_.data(); }
    /** @brief Inhalt zurueckschreiben (Zwischenstand), 16384 Byte. */
    void restore(const uint8_t* data);

private:
    bool inRange(uint16_t addr) const {
        return addr >= cfg_.base_addr && addr < cfg_.base_addr + kGroesse;
    }
    Config                          cfg_;
    const K1520Bus*                 bus_ = nullptr;
    std::array<uint8_t, kGroesse>   mem_{};
    bool                            memdi_ = false;
};
