/**
 * @file k3528.h
 * @brief K3528 — 64-KB-DRAM-Karte der K8915 Gen 2 mit Speichersteuerregister A8H–ABH (8212).
 *
 * Herkunft: doc/k8915g2/karten.md §2, doc/k8915g2/zre_rom.md §3.1/§8,
 * doc/design/24_k8915_varianten.md R1–R3.
 *
 * **Hypothese [?, F2/F20]:** welche Karte das Gerät des Anwenders wirklich trägt und auf
 * welchem Port ihr Register liegt, ist offen; belegt ist nur das Verhalten aus dem ROM
 * (A8H = 06H/0EH/87H/8FH).  Die Brücken stehen deshalb als @ref Belegung, Vorgabe
 * „V3-kompatibel“ (wie die 045-8762: Seite 0 = Bit 0, Seite 1 = Bit 1, Seiten 2+3 = Bit 2,
 * /MEMDI = Bit 7).
 *
 * **Speicherpfad wie K8915Zre:** die Karte meldet ihren Speicher NICHT über
 * K1520Bus::registerMem an.  Die CPU der ZRE fragt zuerst @ref memRead/@ref memWrite;
 * die Karte entscheidet je 4-KB-Slot, ob K3528, die ZRE (K2521, über @ref setZreWeg) oder
 * der Systembus antwortet.  Ein von K3528 oder ZRE bedienter Zugriff erscheint NICHT auf
 * dem Systembus (Annahme wie V3).
 *
 * Vorrang: K3528-Seite gewählt → K3528; sonst 0000–0FFF und kein /MEMDI → ZRE; sonst Bus.
 * Das Register ist nur schreibbar (Lesen → FFH, offener Datenbus [?]).
 * Nicht modelliert: 1-KB-Ausblendadresse, Ausbau .10/.20 (48/32 KB), Bank 2.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstdint>
#include <functional>

class K3528 : public BusDevice {
public:
    /** @brief Bitnummern des Registers (0xFF = nicht verdrahtet). */
    struct Belegung {
        uint8_t seite0 = 0;          ///< 0000–3FFF
        uint8_t seite1 = 1;          ///< 4000–7FFF
        uint8_t seite23 = 2;         ///< 8000–FFFF
        uint8_t memdi_bit = 7;       ///< /MEMDI: sperrt die ZRE
        bool    memdi_aktiv_h = true;   ///< /MEMDI aktiv bei Bit = 1
        uint8_t memdi1_bit = 3;      ///< /MEMDI1: ohne Verbraucher
        bool    memdi1_aktiv_h = false; ///< /MEMDI1 aktiv bei Bit = 0

        /// Wie die 045-8762 (Vorgabe).
        static Belegung v3kompatibel() { return Belegung{}; }
        /// Handdraht X3:37→38: /MEMDI an Bit 0 (karten.md §2.2) [?, F23].
        static Belegung memdiAnBit0() { Belegung b; b.memdi_bit = 0; return b; }
    };

    struct Config {
        uint8_t  reg_base = 0xA8;   ///< Register, belegt 4 Ports (A0/A1 nicht dekodiert)
        Belegung belegung{};
    };

    enum class Quelle : uint8_t { Ram, Zre, Bus };

    explicit K3528(K1520Bus& bus);
    K3528(K1520Bus& bus, const Config& cfg);

    // ─── BusDevice (Register A8H–ABH) ────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "K3528"; }

    /** @brief Nur I/O (4 Ports) anmelden, KEIN registerMem. */
    void attachToBus(K1520Bus& bus);
    /** @brief /RESET: Register := 00H (CLR des 8212); RAM bleibt. */
    void reset();
    /** @brief Netz-Ein: RAM mit @p fill vorbelegen, dann reset(). */
    void powerOn(uint8_t fill = 0x00);

    uint8_t reg() const { return reg_; }
    /** @brief Register setzen wie ein `OUT (A8H)`. */
    void    setReg(uint8_t v);
    bool    memdi()  const { return memdi_; }
    bool    memdi1() const { return memdi1_; }

    /** @brief Zugriffsweg zur ZRE (K2521, 0000–0FFF). */
    using LeseFn    = std::function<uint8_t(uint16_t)>;
    using SchreibFn = std::function<void(uint16_t, uint8_t)>;
    void setZreWeg(LeseFn lesen, SchreibFn schreiben);

    /** @brief Wer antwortet unter @p addr im aktuellen Speicherbild? */
    Quelle ortVon(uint16_t addr) const { return map_[addr >> 12]; }

    /** @brief Speicherzugriff aus Sicht der CPU. */
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);

    uint8_t ramPeek(uint16_t addr) const { return ram_[addr]; }
    void    ramPoke(uint16_t addr, uint8_t v) { ram_[addr] = v; }

    /** @brief Beobachter für RAM- und ZRE-Zugriffe (nicht für den Bus), Signatur wie K1520Bus::BusTrace. */
    void setMemTrace(K1520Bus::BusTrace cb) { mem_trace_ = std::move(cb); }

    const Config& config() const { return cfg_; }

private:
    static bool bit(uint8_t r, uint8_t nr) { return nr < 8 && ((r >> nr) & 1); }
    void rebuildMap();

    K1520Bus& bus_;
    Config    cfg_;

    std::array<uint8_t, 0x10000> ram_{};
    uint8_t reg_    = 0x00;
    bool    memdi_  = false;
    bool    memdi1_ = false;
    std::array<Quelle, 16> map_{};

    LeseFn    zre_lesen_;
    SchreibFn zre_schreiben_;
    K1520Bus::BusTrace mem_trace_;
};
