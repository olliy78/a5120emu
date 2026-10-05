/**
 * @file pc1715w_speicher.h
 * @brief Speicher des PC 1715W: 256 KB DRAM (B1–B4), Bankregister BR (24H), /CAS-Dekoder
 *        74S287 und Bank 0 „Hintergrund“ (S550, ZG-RAM, Bild-RAM der CRT-Karte).
 *
 * Quelle: doc/pc1715/pc1715w_hardware.md §2 (AP-W0), doc/design/21_pc1715.md §4, AP-W1.
 *
 * @code
 *   BR (24H–27H, nach /RESET 00H)  Bit 2–0 Lesebank, Bit 6–4 Schreibbank (Bit 3/7 ohne Wirkung)
 *   74S287:  Adresse A0–A6 = AB12–AB18 (Seite 0–15, Bank 0–7), A7 = /MEMDI (1 = frei);
 *            Ausgang 4 Bit aktiv low = /CAS1…/CAS4 = Block B1…B4, 0FH = kein Block.
 *   Bank 0, 0000–3FFF (/EXTRAM, nur wenn der PROM keinen Block wählt):
 *     0000–0FFF S550 (2 K, gespiegelt; nur Lesebank 0, Schreiben verpufft)
 *     1000–1FFF offen (FFH)         2000–27FF ZG-RAM 1     2800–2FFF ZG-RAM 2
 *     3000–37FF Bild-RAM (2 K)      3800–3FFF Spiegel des Bild-RAM [?]
 * @endcode
 *
 * Lesezyklen (CPU **und** DMA) benutzen die Lesebank, Schreibzyklen die Schreibbank — bei
 * jedem Zugriff neu, deshalb als @ref MemDevice über den ganzen Adressraum angemeldet
 * (@ref attachToBus); die Bank gehört nicht in die Seitentabelle des Busses.
 * Bank 6/7 und offene Bereiche lesen FFH [?], Schreiben verpufft.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstdint>
#include <vector>

class Pc1715wSpeicher : public BusDevice, public MemDevice {
public:
    static constexpr uint8_t PORT_BR = 0x24;   ///< 24H–27H (je vier Adressen gespiegelt)

    Pc1715wSpeicher();

    // ─── Bus: BR als E/A-Tor, Speicher über den ganzen Adressraum ────────────
    uint8_t     ioRead(uint8_t) override { return 0xFF; }          ///< BR ist nur schreibbar
    void        ioWrite(uint8_t, uint8_t d) override { ioWrite24(d); }
    const char* deviceName() const override { return "PC1715W-Speicher"; }
    uint8_t     memRead(uint16_t a) override  { return read(a); }
    void        memWrite(uint16_t a, uint8_t d) override { write(a, d); }

    /** @brief BR-Tor 24H–27H und 0000–FFFF anmelden (zwei Bereiche: die Größe ist 16 Bit). */
    void attachToBus(K1520Bus& bus) {
        bus.registerIO(this, PORT_BR, 4);
        bus.registerMem(this, 0x0000, 0x8000);
        bus.registerMem(this, 0x8000, 0x8000);
    }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void reset();                          ///< /RESET: BR = 00H; RAM bleibt
    void powerOn(uint8_t fill = 0x00);     ///< alle RAM-Blöcke, ZG-/Bild-RAM füllen, dann reset()

    // ─── Speicherzugriff (Lesebank bzw. Schreibbank) ─────────────────────────
    uint8_t read(uint16_t addr) const;
    void    write(uint16_t addr, uint8_t d);

    void    ioWrite24(uint8_t d) { br_ = d; }
    uint8_t bankRegister() const { return br_; }
    int     leseBank() const     { return br_ & 7; }
    int     schreibBank() const  { return (br_ >> 4) & 7; }

    /// /MEMDI-Eingang: true = ein Steckplatz zieht /MEMDI, alle /CAS gesperrt (A7 des PROM = 0).
    void setMemdi(bool aktiv) { memdi_ = aktiv; }
    bool memdi() const        { return memdi_; }

    /**
     * @brief Ergebnis der 74S287-Abbildung: Block 0–3 (= B1–B4) oder -1 (kein /CAS).
     * @param bank 0–7 (AB18–AB16), @param seite 0–15 (AB15–AB12)
     */
    int  blockVon(int bank, int seite) const { return block_[memdi_ ? 0 : 1][bank & 7][seite & 15]; }

    // ─── Direkter Zugriff (Bildaufbau, Tests, Debugger) ──────────────────────
    uint8_t&       bildRam(int i)             { return bild_[i & 0x7FF]; }
    const uint8_t& bildRam(int i) const       { return bild_[i & 0x7FF]; }
    /// ZG-RAM @p zg (0 = ZG 1 bei 2000H, 1 = ZG 2 bei 2800H), Byte = Linie·128 + Code.
    uint8_t&       zgRam(int zg, int i)       { return zg_[zg & 1][i & 0x7FF]; }
    const uint8_t& zgRam(int zg, int i) const { return zg_[zg & 1][i & 0x7FF]; }
    /// RAM-Block 0–3 (B1–B4), Adresse 0–FFFF.
    uint8_t&       blockRam(int b, uint16_t a)       { return ram_[b & 3][a]; }
    const uint8_t& blockRam(int b, uint16_t a) const { return ram_[b & 3][a]; }

private:
    enum class Hintergrund : uint8_t { Rom, Offen, Zg1, Zg2, Bild };
    static Hintergrund hintergrundVon(uint16_t a);

    std::array<std::vector<uint8_t>, 4> ram_;       ///< B1–B4 à 64 KB
    std::array<uint8_t, 0x800> zg_[2];
    std::array<uint8_t, 0x800> bild_;
    int8_t  block_[2][8][16];                       ///< [/MEMDI frei][Bank][Seite] -> Block / -1
    uint8_t br_ = 0;
    bool    memdi_ = false;
};
