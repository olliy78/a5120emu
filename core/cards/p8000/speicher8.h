/**
 * @file speicher8.h
 * @brief ADP-gebanktes Speichergerät der 8-Bit-Rechnerkarte des P8000 (P 8000/8.1).
 *
 * Quelle: doc/p8000/schaltplan_8bit.md §1 (Rang 1), doc/design/25_p8000.md §10.1/§10.2, AP P5a.
 *
 * @code
 *   ADP  (D25, MH7489)  16 Zellen à 3 Bit, Zelle = A12–A15, Bit0 PROM_SEL, Bit1 SRAM_SEL, Bit2 DRAM_SEL
 *   RFF  (6D11)         Reset: Q = 0; jeder Zugriff auf 04H–07H (IN oder OUT): Q = 1 (bleibt bis /RES)
 *   Q = 0               PROM wird überall selektiert, SRAM/DRAM nie (ADP-Ausgänge gesperrt)
 *   Q = 1               je 4-K-Seite genau das, was die ADP-Zelle sagt (auch mehreres oder nichts)
 *   EPROM               2 × 4 K an A0–A11; EPROM 1 bei A12 = 0, EPROM 2 bei A12 = 1 (A13–A15 egal)
 *   SRAM                2 K (4 × U214), A10 wählt das Paar, A11 und A12–A15 egal
 *   DRAM                64 K, Adresse = A0–A15
 * @endcode
 *
 * Das Gerät belegt 0000–FFFF am K1520Bus (als generischer Z80-Bus, wie beim PC 1715) und
 * E/A 00H–07H (00H–03H nur schreibend wirksam = WEADP, 04H–07H = RES_RFF).  Die Zelle beim
 * Schreiben kommt aus A12–A15 der E/A-Adresse (`K1520Bus::ioAddress()`): nur OUT (C),r mit
 * B = Seite·10H ist sinnvoll; bei OUT (n),A liegt A auf A8–A15 (Falle, s. Test).
 *
 * Nicht belegt/Annahmen (Schaltplan §1.2 Pkt. 4, 5, Messfragen 2 und 3):
 *  - keine Bank selektiert: Lesen liefert FFH [Annahme, offener Bus]
 *  - Mehrfachselektion: Lesen = UND der beteiligten Bänke [Annahme, Buskonflikt], Schreiben
 *    in alle beteiligten RAM-Bänke (belegt: /WE gemeinsam)
 *  - ADP-Startwert nach Netz-Ein: Config::adp_start [real unbestimmt]; /RES lässt den ADP-Inhalt
 *  - /MEMDI wird nicht nachgebildet (auf der Karte treibt es niemand)
 *
 * Wartetakte: jeder Zugriff, der PROM-CE aktiv macht (auch M1, auch DMA, auch Schreiben ins
 * Leere des EPROMs), kostet 2 Takte (Handbuch; Taktung im Plan nicht eindeutig).  Der Z80 kennt
 * kein /WAIT: die Laufschleife holt die angefallenen Takte mit nimmWartetakte() ab.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/util/zustand.h"
#include <array>
#include <cstdint>
#include <vector>

class P8000Speicher8 : public MemDevice, public BusDevice {
public:
    struct Config {
        uint8_t adp_start    = 0x00;   ///< ADP-Inhalt nach Netz-Ein (alle 16 Zellen)
        uint8_t ram_fuellwert = 0x00;  ///< SRAM/DRAM nach Netz-Ein
    };

    static constexpr uint8_t PORT_ADP     = 0x00;   ///< 00H–03H WEADP
    static constexpr uint8_t PORT_RES_RFF = 0x04;   ///< 04H–07H RES_RFF
    static constexpr int     EPROM_GROESSE = 0x2000;
    static constexpr int     WARTETAKTE_JE_ZUGRIFF = 2;

    P8000Speicher8() : P8000Speicher8(Config{}) {}
    explicit P8000Speicher8(const Config& cfg);

    /** @brief EPROM-Inhalt 8 K (Bytes 0000–0FFF = EPROM 1, 1000–1FFF = EPROM 2); kürzer ⇒ mit FFH aufgefüllt. */
    void setRom(const uint8_t* daten, size_t n);

    /** @brief 0000–FFFF (zwei Bereiche, die Größe ist 16 Bit) und E/A 00H–07H anmelden. */
    void attachToBus(K1520Bus& bus);

    // ─── MemDevice / BusDevice ───────────────────────────────────────────────
    uint8_t     memRead(uint16_t addr) override;
    void        memWrite(uint16_t addr, uint8_t d) override;
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "P8000-Speicher8"; }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief /RES: RFF Q = 0; ADP- und RAM-Inhalt bleiben. */
    void reset();
    /** @brief Netz-Ein: ADP = adp_start, RAM füllen, dann reset(). */
    void powerOn();

    // ─── Wartetakte ──────────────────────────────────────────────────────────
    /** @brief Seit dem letzten Abruf angefallene Wartetakte; setzt den Zähler zurück. */
    uint32_t nimmWartetakte() { const uint32_t w = warte_; warte_ = 0; return w; }
    uint64_t epromZugriffe() const { return eprom_zugriffe_; }

    // ─── Zustand / Diagnose (ohne Nebenwirkung) ──────────────────────────────
    bool    rffGesetzt() const { return rff_; }                 ///< Q = 1: ADP wirkt
    uint8_t adp(int seite) const { return adp_[seite & 15]; }
    /** @brief Aktive Selects für @p addr: Bit0 PROM, Bit1 SRAM, Bit2 DRAM (berücksichtigt RFF). */
    uint8_t selekt(uint16_t addr) const { return rff_ ? adp_[addr >> 12] : uint8_t(1); }
    /** @brief Lesen ohne Wartetakt-Zählung (Debugger, Tests). */
    uint8_t peek(uint16_t addr) const;

    uint8_t& dram(uint16_t a)             { return dram_[a]; }
    uint8_t& sram(uint16_t a)             { return sram_[a & 0x7FF]; }

    // ─── Save-State (P8KS, Entwurf 25 §10.2): ADP, RFF, SRAM, DRAM, Wartezähler; das EPROM nicht ──
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    void visit(k1520::ZAr& a);
    Config cfg_;
    K1520Bus* bus_ = nullptr;   ///< für ioAddress() (A12–A15 beim ADP-Schreiben)
    std::array<uint8_t, EPROM_GROESSE> rom_;
    std::array<uint8_t, 0x800>   sram_{};
    std::array<uint8_t, 0x10000> dram_{};
    std::array<uint8_t, 16>      adp_{};
    bool     rff_ = false;
    uint32_t warte_ = 0;
    uint64_t eprom_zugriffe_ = 0;
};
