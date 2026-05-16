#pragma once
#include <cstdint>
#include <array>
#include "core/bus/k1520_bus.h"

// ─── K3526 Group Configuration ────────────────────────────────────────────────
// Each of the 4 RAM groups has an independent 16KB-aligned base address and
// a MEMDI source selector (false = main K1520 bus /MEMDI, true = Koppelbus).
struct K3526GroupConfig {
    uint16_t base_addr;    // 16KB-aligned: 0x0000, 0x4000, 0x8000, or 0xC000
    bool     memdi_source; // false = /MEMDI (K1520 bus), true = /MEMDI1/2 (Koppelbus)
};

// ─── K3526 OPS – Operationsspeicher ───────────────────────────────────────────
// 64 KB DRAM card for the K1520 bus, split into 4 independent 16KB groups.
// Each group is registered as a separate MemDevice region on K1520Bus.
// All four groups share one flat 64KB backing array.
class K3526 : public MemDevice {
public:
    // Standard A5120 configuration: four groups filling the full 64KB address space.
    struct A5120Config {
        K3526GroupConfig groups[4] = {
            {0x0000, false},
            {0x4000, false},
            {0x8000, false},
            {0xC000, false}
        };
    };

    K3526();
    explicit K3526(const A5120Config& cfg);

    // MemDevice interface – called by K1520Bus with the absolute bus address.
    uint8_t memRead(uint16_t addr) override;
    void    memWrite(uint16_t addr, uint8_t data) override;
    bool    isWritable() const override { return true; }

    // Register all four groups on the bus (one registerMem call per group).
    void attachToBus(K1520Bus& bus);

    // Control the /MEMDI signal for a specific group.
    // disabled=true: reads return 0xFF, writes are ignored (bus-float / write-protect).
    void setMemDI(int group, bool disabled);

    // Utility helpers (for tests and initial state setup).
    void           fill(uint8_t value = 0x00);
    void           load(uint16_t addr, const uint8_t* data, uint16_t len);
    const uint8_t* rawPtr() const;

private:
    A5120Config              cfg_;
    std::array<uint8_t, 65536> mem_{};
    bool                     group_memdi_[4] = {};  // true = group is disabled
};
