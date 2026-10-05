/**
 * @file k3528.cpp
 * @brief K3528 (K8915 Gen 2) — Speicherbild je Registerwert A8H.
 * @see k3528.h, doc/design/24_k8915_varianten.md R3
 */

#include "core/cards/k3528/k3528.h"
#include "core/logger.h"

K3528::K3528(K1520Bus& bus) : K3528(bus, Config{}) {}

K3528::K3528(K1520Bus& bus, const Config& cfg) : bus_(bus), cfg_(cfg)
{
    rebuildMap();
}

void K3528::attachToBus(K1520Bus& bus)
{
    // A1/A0 nicht dekodiert ⇒ das Register liegt vierfach (A8H–ABH).
    bus.registerIO(this, cfg_.reg_base, 4);
}

void K3528::powerOn(uint8_t fill)
{
    ram_.fill(fill);
    reset();
}

void K3528::reset() { setReg(0x00); }

uint8_t K3528::ioRead(uint8_t)
{
    // Ausgaberegister (Takt aus /IORQ ∧ /WR): ein IN liest den offenen Datenbus [?].
    return 0xFF;
}

void K3528::ioWrite(uint8_t, uint8_t data) { setReg(data); }

void K3528::setReg(uint8_t v)
{
    reg_ = v;
    const auto& b = cfg_.belegung;
    memdi_  = (bit(v, b.memdi_bit)  == b.memdi_aktiv_h)  && b.memdi_bit  < 8;
    memdi1_ = (bit(v, b.memdi1_bit) == b.memdi1_aktiv_h) && b.memdi1_bit < 8;
    rebuildMap();
    LOG_DEBUG("K3528", "A8H := %02X  /MEMDI=%d /MEMDI1=%d", v, (int)memdi_, (int)memdi1_);
}

void K3528::rebuildMap()
{
    const auto& b = cfg_.belegung;
    const bool seite[4] = { bit(reg_, b.seite0), bit(reg_, b.seite1),
                            bit(reg_, b.seite23), bit(reg_, b.seite23) };
    for (int s = 0; s < 16; ++s) {
        if (seite[s >> 2])                    map_[s] = Quelle::Ram;
        else if (s == 0 && !memdi_)           map_[s] = Quelle::Zre;
        else                                  map_[s] = Quelle::Bus;
    }
}

void K3528::setZreWeg(LeseFn lesen, SchreibFn schreiben)
{
    zre_lesen_ = std::move(lesen);
    zre_schreiben_ = std::move(schreiben);
}

uint8_t K3528::memRead(uint16_t addr)
{
    uint8_t v;
    switch (map_[addr >> 12]) {
        case Quelle::Ram: v = ram_[addr]; break;
        case Quelle::Zre: v = zre_lesen_ ? zre_lesen_(addr) : 0xFF; break;
        case Quelle::Bus:
        default:          return bus_.memRead(addr);   // Bus meldet selbst
    }
    if (mem_trace_) mem_trace_(false, true, addr, v);
    return v;
}

void K3528::memWrite(uint16_t addr, uint8_t data)
{
    switch (map_[addr >> 12]) {
        case Quelle::Ram: ram_[addr] = data; break;
        case Quelle::Zre: if (zre_schreiben_) zre_schreiben_(addr, data); break;
        case Quelle::Bus:
        default:          bus_.memWrite(addr, data); return;   // Bus meldet selbst
    }
    if (mem_trace_) mem_trace_(false, false, addr, data);
}
