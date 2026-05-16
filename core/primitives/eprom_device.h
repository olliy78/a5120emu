#pragma once
#include "../bus/k1520_bus.h"
#include <cstddef>

// Fixed-size EPROM backed by a constexpr uint8_t array.
// Writes are silently ignored (ROM semantics).
template<size_t N>
class EPROMDevice : public MemDevice {
public:
    explicit EPROMDevice(const uint8_t (&data)[N]) : data_(data) {}

    uint8_t memRead(uint16_t addr) override {
        return (addr < N) ? data_[addr] : 0xFF;
    }

    void memWrite(uint16_t, uint8_t) override {}  // ROM: ignored

    bool isWritable() const override { return false; }

    size_t size() const { return N; }

private:
    const uint8_t* data_;
};
