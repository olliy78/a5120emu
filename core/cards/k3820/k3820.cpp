/**
 * @file k3820.cpp
 * @brief PFS K3820 — Adressauswahl, Sockel, Sperre über /MEMDI1/2.
 * @see k3820.h, doc/k8915g2/karten.md §3, doc/design/24_k8915_varianten.md AP-V11
 */

#include "core/cards/k3820/k3820.h"
#include "core/logger.h"
#include <algorithm>
#include <stdexcept>

K3820::Config K3820::Config::ausBruecken(uint8_t bruecken)
{
    Config c;
    c.startadresse = uint16_t((bruecken & 0x0F) << 12);
    return c;
}

K3820::K3820() : K3820(Config{}) {}

K3820::K3820(const Config& cfg) : cfg_(cfg)
{
    if (cfg_.startadresse & 0x0FFF)
        throw std::invalid_argument("K3820: Startadresse muss ein Vielfaches von 4 KB sein");
    for (auto& c : chip_) c.fill(0xFF);
}

void K3820::attachToBus(K1520Bus& bus)
{
    bus_ = &bus;
    sperreNeuBewerten();
}

void K3820::anmelden()
{
    // intern = AB12…AB15 − Start (4 Bit, Übertrag verworfen): ein Fenster ab D000H
    // läuft über FFFFH hinaus und belegt 0000–0FFFH mit [?, Subtrahierer 74LS83].
    const uint32_t start = cfg_.startadresse;
    const uint32_t ende  = start + kGroesse;
    if (ende <= 0x10000u) {
        bus_->registerMem(this, uint16_t(start), kGroesse);
    } else {
        bus_->registerMem(this, uint16_t(start), uint16_t(0x10000u - start));
        bus_->registerMem(this, 0x0000, uint16_t(ende - 0x10000u));
    }
}

bool K3820::gesperrt() const
{
    switch (cfg_.memdi) {
        case MemdiLeitung::Memdi1: return memdi1_;
        case MemdiLeitung::Memdi2: return memdi2_;
        default:                   return false;   // Bus-/MEMDI: je Zugriff im Bus
    }
}

void K3820::sperreNeuBewerten()
{
    if (!bus_) return;
    const bool soll = !gesperrt();
    if (soll == angemeldet_) return;
    if (soll) anmelden();
    else      bus_->unregisterMem(this);
    angemeldet_ = soll;
    LOG_DEBUG("K3820", "%04X: %s", cfg_.startadresse, soll ? "freigegeben" : "gesperrt (/MEMDI1/2)");
}

void K3820::setMemdi1(bool aktiv) { memdi1_ = aktiv; sperreNeuBewerten(); }
void K3820::setMemdi2(bool aktiv) { memdi2_ = aktiv; sperreNeuBewerten(); }

uint8_t K3820::memRead(uint16_t addr)
{
    const uint16_t rel = uint16_t(addr - cfg_.startadresse);
    if (rel >= kGroesse) return 0xFF;   // nicht gewählt (AB14K/AB15K ≠ 0)
    return chip_[rel >> 10][rel & (kChipSize - 1)];
}

void K3820::setChip(int idx, const uint8_t* data, std::size_t len)
{
    if (idx < 0 || idx >= kChips || len > kChipSize)
        throw std::out_of_range("K3820::setChip: Sockel 0…15, höchstens 1 KB");
    chip_[idx].fill(0xFF);
    if (data && len) std::copy(data, data + len, chip_[idx].begin());
    belegt_[idx] = true;
}

void K3820::leereChip(int idx)
{
    if (idx < 0 || idx >= kChips) throw std::out_of_range("K3820::leereChip: Sockel 0…15");
    chip_[idx].fill(0xFF);
    belegt_[idx] = false;
}

void K3820::ladeAbbild(const uint8_t* data, std::size_t len)
{
    if (len > kGroesse) throw std::out_of_range("K3820::ladeAbbild: höchstens 16 KB");
    for (int i = 0; i < kChips; ++i) {
        const std::size_t ab = std::size_t(i) * kChipSize;
        if (ab >= len) { leereChip(i); continue; }
        setChip(i, data + ab, std::min<std::size_t>(kChipSize, len - ab));
    }
}

bool K3820::chipBelegt(int idx) const { return idx >= 0 && idx < kChips && belegt_[idx]; }

uint8_t K3820::peek(uint16_t rel) const
{
    return rel < kGroesse ? chip_[rel >> 10][rel & (kChipSize - 1)] : 0xFF;
}
