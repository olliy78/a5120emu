#include "core/cards/k3526/k3526.h"

K3526::K3526() : K3526(A5120Config{}) {}

K3526::K3526(const A5120Config& cfg) : cfg_(cfg) {
    mem_.fill(0x00);
}

// Find which configured group owns `addr`, then apply per-group MEMDI.
// The bus already dispatched the call to this device, so `addr` is guaranteed
// to fall within one of the registered group ranges (assuming attachToBus was
// called). The loop still validates this explicitly for safety.
uint8_t K3526::memRead(uint16_t addr) {
    for (int i = 0; i < 4; ++i) {
        uint32_t base = cfg_.groups[i].base_addr;
        if (addr >= base && addr < base + 0x4000u) {
            if (group_memdi_[i]) return 0xFF;
            return mem_[addr];
        }
    }
    return 0xFF; // address not covered by any group
}

void K3526::memWrite(uint16_t addr, uint8_t data) {
    for (int i = 0; i < 4; ++i) {
        uint32_t base = cfg_.groups[i].base_addr;
        if (addr >= base && addr < base + 0x4000u) {
            if (group_memdi_[i]) return; // write disabled
            mem_[addr] = data;
            return;
        }
    }
    // address not covered by any group – silently ignore
}

void K3526::attachToBus(K1520Bus& bus) {
    for (int i = 0; i < 4; ++i) {
        bus.registerMem(this, cfg_.groups[i].base_addr, 0x4000);
    }
}

void K3526::setMemDI(int group, bool disabled) {
    if (group >= 0 && group < 4)
        group_memdi_[group] = disabled;
}

void K3526::fill(uint8_t value) {
    mem_.fill(value);
}

void K3526::load(uint16_t addr, const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; ++i)
        mem_[static_cast<uint16_t>(addr + i)] = data[i];
}

const uint8_t* K3526::rawPtr() const {
    return mem_.data();
}
