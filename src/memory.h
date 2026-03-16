// memory.h - A5120 Memory Subsystem
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Memory {
public:
    static constexpr size_t SIZE = 65536;
    static constexpr uint16_t SCREEN_START = 0xF800;
    static constexpr uint16_t SCREEN_END   = 0xFFFF;
    static constexpr size_t SCREEN_SIZE    = 2048;      // 0x800
    static constexpr int SCREEN_COLS       = 80;
    static constexpr int SCREEN_ROWS       = 24;
    static constexpr size_t SCREEN_CHARS   = 1920;      // 80*24

    Memory();

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t val);

    // Bulk operations
    void load(uint16_t addr, const uint8_t* data, size_t len);
    void load(uint16_t addr, const std::vector<uint8_t>& data);
    bool loadFile(const std::string& filename, uint16_t addr);
    void clear();
    void fill(uint16_t start, uint16_t end, uint8_t val);

    // Direct access (for screen update etc.)
    const uint8_t* data() const { return ram_; }
    uint8_t* data() { return ram_; }

    // Screen buffer access
    const uint8_t* screenBuffer() const { return &ram_[SCREEN_START]; }
    uint8_t getScreenChar(int row, int col) const;

private:
    uint8_t ram_[SIZE];
};
