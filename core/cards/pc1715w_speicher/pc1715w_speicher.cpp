/**
 * @file pc1715w_speicher.cpp
 * @brief Speicher des PC 1715W — siehe pc1715w_speicher.h.
 */

#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/cards/pc1715w_speicher/rom_74s287.h"
#include "core/cards/pc1715w_speicher/rom_s550.h"
#include <algorithm>

Pc1715wSpeicher::Pc1715wSpeicher()
{
    for (auto& b : ram_) b.assign(0x10000, 0);
    // Abbildung unmittelbar aus den Daten des 74S287-Abzugs (nicht aus einer Formel):
    // PROM-Adresse = (/MEMDI frei) << 7 | Bank << 4 | Seite; Ausgang aktiv low, Bit n = /CAS(n+1).
    for (int frei = 0; frei < 2; ++frei)
        for (int bank = 0; bank < 8; ++bank)
            for (int seite = 0; seite < 16; ++seite) {
                const uint8_t o = PC1715W_74S287_CAS[(frei << 7) | (bank << 4) | seite] & 0x0F;
                int blk = -1;
                for (int n = 3; n >= 0; --n)
                    if (!(o & (1 << n))) blk = n;   // mehrere aktiv (im Abzug nicht vorhanden): niedrigster
                block_[frei][bank][seite] = static_cast<int8_t>(blk);
            }
    powerOn(0x00);
}

void Pc1715wSpeicher::reset() { br_ = 0x00; }

void Pc1715wSpeicher::powerOn(uint8_t fill)
{
    for (auto& b : ram_) std::fill(b.begin(), b.end(), fill);
    for (auto& z : zg_) z.fill(fill);
    bild_.fill(fill);
    memdi_ = false;
    reset();
}

Pc1715wSpeicher::Hintergrund Pc1715wSpeicher::hintergrundVon(uint16_t a)
{
    switch (a >> 11) {                           // 2-K-Fenster des Bereichs 0000–3FFF
    case 0: case 1: return Hintergrund::Rom;     // 0000–0FFF
    case 2: case 3: return Hintergrund::Offen;   // 1000–1FFF
    case 4:         return Hintergrund::Zg1;     // 2000–27FF
    case 5:         return Hintergrund::Zg2;     // 2800–2FFF
    default:        return Hintergrund::Bild;    // 3000–3FFF (3800 gespiegelt [?])
    }
}

uint8_t Pc1715wSpeicher::read(uint16_t addr) const
{
    const int bank = br_ & 7;
    const int blk = blockVon(bank, addr >> 12);
    if (blk >= 0) return ram_[blk][addr];
    // Kein /CAS: /EXTRAM = Bank 0, 0000–3FFF; bei gezogenem /MEMDI ist der Bus fremd belegt.
    if (bank == 0 && addr < 0x4000 && !memdi_) {
        switch (hintergrundVon(addr)) {
        case Hintergrund::Rom:   return PC1715W_S550_URLADER[addr & 0x7FF];
        case Hintergrund::Zg1:   return zg_[0][addr & 0x7FF];
        case Hintergrund::Zg2:   return zg_[1][addr & 0x7FF];
        case Hintergrund::Bild:  return bild_[addr & 0x7FF];
        case Hintergrund::Offen: break;
        }
    }
    return 0xFF;
}

void Pc1715wSpeicher::write(uint16_t addr, uint8_t d)
{
    const int bank = (br_ >> 4) & 7;
    const int blk = blockVon(bank, addr >> 12);
    if (blk >= 0) { ram_[blk][addr] = d; return; }
    if (bank == 0 && addr < 0x4000 && !memdi_) {
        switch (hintergrundVon(addr)) {
        case Hintergrund::Zg1:  zg_[0][addr & 0x7FF] = d; break;
        case Hintergrund::Zg2:  zg_[1][addr & 0x7FF] = d; break;
        case Hintergrund::Bild: bild_[addr & 0x7FF] = d; break;
        default: break;                          // ROM/offen: verpufft
        }
    }
}
