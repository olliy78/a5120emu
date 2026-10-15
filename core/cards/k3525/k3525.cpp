/**
 * @file k3525.cpp
 * @brief K3525 – 16-KB-dRAM-Karte (s. k3525.h).
 */

#include "core/cards/k3525/k3525.h"
#include <cstring>
#include <stdexcept>

K3525::K3525() : K3525(Config{}) {}

K3525::K3525(const Config& cfg) : cfg_(cfg) {
    if (cfg.base_addr % kRaster != 0 || cfg.base_addr + kGroesse > 0x10000u)
        throw std::invalid_argument("K3525: Basisadresse muss im 4-KB-Raster liegen und 16 KB passen");
    mem_.fill(0xFF);   // wie K3526: ungeladenes dRAM liest 0xFF
}

uint8_t K3525::memRead(uint16_t addr) {
    if (!inRange(addr) || memdi_) return 0xFF;
    return mem_[addr - cfg_.base_addr];
}

void K3525::memWrite(uint16_t addr, uint8_t data) {
    if (!inRange(addr) || memdi_) return;
    // Auf Bus-/MEMDI gebrückt: ein Vorrangspeicher (z. B. EM) hat den Zyklus übernommen.
    if (!cfg_.memdi_source && bus_ && bus_->memdiActive()) return;
    mem_[addr - cfg_.base_addr] = data;
}

void K3525::attachToBus(K1520Bus& bus) {
    bus_ = &bus;
    bus.registerMem(this, cfg_.base_addr, kGroesse);
}

void K3525::fill(uint8_t value) { mem_.fill(value); }

void K3525::load(uint16_t offset, const uint8_t* data, uint16_t len) {
    for (uint32_t i = 0; i < len && offset + i < kGroesse; ++i)
        mem_[offset + i] = data[i];
}

void K3525::restore(const uint8_t* data) { std::memcpy(mem_.data(), data, mem_.size()); }
