/**
 * @file raf.h
 * @brief RAM-Floppy-Karte RAF 128 / RAF 512 / RAF-2M (A5120, K8915, PRG 710).
 *
 * Quelle: doc/design/22_raf512.md §3, §5.1, AP-R1.  Die Masken der RAF128 und RAF-2M
 * sind nur durch RAFTEST.MAC belegt (`Pro_128`, `Pro_2M`), nicht durch eine Karte [?].
 *
 * @code
 *   basis    Datenport:  Bytezeiger im Sektor = A8–A14 (B & 7FH), A15 unbeachtet,
 *                        KEIN Autoinkrement (INIR/OTIR laufen rückwärts)
 *   basis+1  Steuerport: 16-Bit-Latch = (A8–A15 << 8) | D0–D7 = absolute Sektornummer
 *                        samt Sperrbits
 * @endcode
 *
 * Die Karte wertet den oberen Adressbus über K1520Bus::ioAddress() aus.  Kein Takt,
 * kein /WAIT, kein Interrupt, kein Speicherfenster.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <cstdint>
#include <string>
#include <vector>

class RAF : public BusDevice {
public:
    enum class Typ : uint8_t { RAF128, RAF512, RAF2M };

    struct Config {
        Typ     typ   = Typ::RAF512;
        uint8_t basis = 0x88;   ///< Datenport; Steuerregister basis+1.  Im Emulator überall
                                ///< 88H — abweichende Werte nur in Tests (§9, F3a)
    };

    static constexpr uint32_t SEKTORLAENGE = 128;

    RAF();
    explicit RAF(const Config& cfg);

    // ─── BusDevice ───────────────────────────────────────────────────────────
    /// @param port ABSOLUTE Portnummer (der Bus reicht sie so durch, nicht relativ).
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t d) override;
    const char* deviceName() const override { return "RAF"; }

    /// registerIO(this, basis, 2); wirft std::runtime_error bei belegtem Port.
    void attachToBus(K1520Bus& bus);
    void attachToBus(K1520Bus* bus) { attachToBus(*bus); }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /// Netz-Ein ohne Stand-by: Inhalt 00H, Latch FFFFH (gesperrt).
    void powerOn();
    /// /RESET: NUR Latch FFFFH — der Inhalt bleibt (Stand-by-Versorgung, §3.4).
    void reset();

    // ─── Eigenschaften ───────────────────────────────────────────────────────
    const Config& config() const { return cfg_; }
    uint32_t kapazitaet() const { return uint32_t(mem_.size()); }      ///< in Byte
    uint32_t sektoren() const   { return kapazitaet() / SEKTORLAENGE; }
    uint16_t sperrmaske() const { return sperre_; }
    uint16_t sektormaske() const { return sektor_; }
    uint16_t latch() const      { return latch_; }
    bool     gesperrt() const   { return (latch_ & sperre_) != 0; }

    // ─── Debug/Test ──────────────────────────────────────────────────────────
    /// Lineare Adresse (0 … kapazitaet()-1); außerhalb: 0xFF bzw. ignoriert.
    uint8_t peek(uint32_t adr) const;
    void    poke(uint32_t adr, uint8_t v);

    // ─── Save-State (Kartenseite; die Maschinen binden es seit AP-R2 ein) ────
    /// @param mit_inhalt false = nur Typ + Latch (Inhaltslänge 0) — für die Rückwärts-
    ///        Historie des Debuggers, die je Schritt einen Stand zieht (bis 2 MB je Stück).
    void saveState(std::vector<uint8_t>& out, bool mit_inhalt = true) const;
    /// false bei zu kurzem/falschem Block (dann ist nichts verändert).  Ein Block ohne
    /// Inhalt (Länge 0) setzt nur das Latch, der Inhalt bleibt.
    bool loadState(const uint8_t*& p, const uint8_t* end);

    // ─── Stand-by-Ablage (§7.1): Rohdatei, Größe muss exakt zur Kapazität passen ─
    bool ladeInhalt(const std::string& pfad);
    bool speichereInhalt(const std::string& pfad) const;

private:
    /// Index in mem_ für den aktuellen Zugriff (Latch + A8–A14), -1 bei Sperre.
    int64_t zelle() const;

    Config               cfg_;
    uint16_t             sperre_ = 0;
    uint16_t             sektor_ = 0;
    uint16_t             latch_  = 0xFFFF;
    K1520Bus*            bus_    = nullptr;
    std::vector<uint8_t> mem_;
};
