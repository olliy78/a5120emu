#pragma once
#include "../bus/k1520_bus.h"
#include <vector>
#include <cstring>

class RAMDevice : public MemDevice {
public:
    explicit RAMDevice(uint16_t size) : mem_(size, 0x00) {}

    uint8_t memRead(uint16_t addr) override {
        return (addr < mem_.size()) ? mem_[addr] : 0xFF;
    }

    void memWrite(uint16_t addr, uint8_t data) override {
        if (addr < mem_.size()) mem_[addr] = data;
    }

    bool isWritable() const override { return true; }

    uint8_t* data() { return mem_.data(); }
    const uint8_t* data() const { return mem_.data(); }
    uint16_t size() const { return static_cast<uint16_t>(mem_.size()); }

    void fill(uint8_t value = 0x00) {
        std::fill(mem_.begin(), mem_.end(), value);
    }

    void load(const uint8_t* src, uint16_t offset, uint16_t len) {
        if (offset + len <= mem_.size())
            std::memcpy(mem_.data() + offset, src, len);
    }

private:
    std::vector<uint8_t> mem_;
};
