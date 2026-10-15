/**
 * @file spe.cpp
 * @brief SPE 2 des MC80.3x und Segmentfenster 4000H–BFFFH.
 * @see spe.h, doc/mc80/hardware_3x.md §3.2
 */

#include "core/cards/mc80/spe.h"
#include "core/cards/mc80/rom_spe2.h"
#include "core/logger.h"
#include <algorithm>

Mc80Spe2::Config Mc80Spe2::Config::mc8030()
{
    Config c;
    c.rom     = MC80_SPE2_ROM;
    c.rom_len = sizeof(MC80_SPE2_ROM);
    return c;
}

Mc80Spe2::Config Mc80Spe2::Config::nurRam(uint8_t bit)
{
    Config c;
    c.rom_block   = false;
    c.segment_bit = bit;
    c.io_base     = 0;
    return c;
}

Mc80Spe2::Mc80Spe2(const Config& cfg) : cfg_(cfg), ram_(RAM_GROESSE, 0x00)
{
    if (cfg_.rom_len > ROM_GROESSE) cfg_.rom_len = ROM_GROESSE;
}

void Mc80Spe2::attachToBus(K1520Bus& bus)
{
    bus_ = &bus;
    if (cfg_.io_base) bus.registerIO(this, cfg_.io_base, 4);
    if (cfg_.rom_block) bus.registerMem(this, ROM_BASIS, ROM_GROESSE);
}

void Mc80Spe2::powerOn(uint8_t fill)
{
    std::fill(ram_.begin(), ram_.end(), fill);
    reset();
}

void Mc80Spe2::reset()
{
    // Auslieferung ohne Anfangsinitialisierung: nach /RESET alles frei (egos.md §1.5).
    rom_sperre_.reset();
    ram_sperre_.reset();
}

void Mc80Spe2::ioWrite(uint8_t port, uint8_t)
{
    // A0 = 1 Freigabe / 0 Sperre, A1 = 0 ROM / 1 RAM; A10–A15 = 1-KB-Block [SPE2 S.2–6].
    const uint16_t adr = bus_ ? bus_->ioAddress() : port;
    const int block    = (adr >> 10) & 63;
    const bool frei    = port & 0x01;
    auto& sperre       = (port & 0x02) ? ram_sperre_ : rom_sperre_;
    sperre.set(block, !frei);
    LOG_DEBUG("MC80-SPE2", "%s-Block %04XH %s", (port & 0x02) ? "RAM" : "ROM",
              block << 10, frei ? "frei" : "gesperrt");
}

uint8_t Mc80Spe2::memRead(uint16_t a)
{
    if (a < ROM_BASIS || a >= ROM_BASIS + ROM_GROESSE) return 0xFF;
    if (rom_sperre_.test(a >> 10)) return 0xFF;                 // treibt den Bus nicht
    const uint16_t off = a - ROM_BASIS;
    return (cfg_.rom && off < cfg_.rom_len) ? cfg_.rom[off] : LEER;
}

bool Mc80Spe2::ramLiest(uint16_t a) const
{
    if (!ram_frei_ || a < RAM_BASIS || a >= RAM_BASIS + RAM_GROESSE) return false;
    return !(cfg_.sperrart == Sperrart::Zugriff && ram_sperre_.test(a >> 10));
}

void Mc80Spe2::ramSchreiben(uint16_t a, uint8_t d)
{
    if (!ram_frei_ || a < RAM_BASIS || a >= RAM_BASIS + RAM_GROESSE) return;
    if (ram_sperre_.test(a >> 10)) return;   // beide Sperrarten verhindern das Schreiben
    ram_[a - RAM_BASIS] = d;
}

uint8_t Mc80Segmentfenster::memRead(uint16_t a)
{
    for (auto* k : karten_)
        if (k->ramLiest(a)) return k->ramLesen(a);
    return 0xFF;
}

void Mc80Segmentfenster::memWrite(uint16_t a, uint8_t d)
{
    for (auto* k : karten_) k->ramSchreiben(a, d);
}

void Mc80EpromSegment::abzug(uint16_t adresse, const uint8_t* daten, uint16_t laenge)
{
    abzuege_.push_back({adresse, daten, laenge});
}

const Mc80EpromSegment::Abzug* Mc80EpromSegment::suche(uint16_t a) const
{
    for (const auto& z : abzuege_)
        if (a >= z.adresse && a < z.adresse + z.laenge) return &z;
    return nullptr;
}

bool Mc80EpromSegment::ramLiest(uint16_t a) const { return frei_ && suche(a) != nullptr; }

uint8_t Mc80EpromSegment::ramLesen(uint16_t a) const
{
    const Abzug* z = suche(a);
    return z ? z->daten[a - z->adresse] : 0xFF;
}
