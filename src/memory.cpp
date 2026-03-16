// memory.cpp - A5120 Memory Implementation
#include "memory.h"
#include <fstream>

Memory::Memory() {
    clear();
}

uint8_t Memory::read(uint16_t addr) const {
    return ram_[addr];
}

void Memory::write(uint16_t addr, uint8_t val) {
    ram_[addr] = val;
}

void Memory::load(uint16_t addr, const uint8_t* data, size_t len) {
    size_t maxLen = SIZE - addr;
    if (len > maxLen) len = maxLen;
    memcpy(&ram_[addr], data, len);
}

void Memory::load(uint16_t addr, const std::vector<uint8_t>& data) {
    load(addr, data.data(), data.size());
}

bool Memory::loadFile(const std::string& filename, uint16_t addr) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t maxLen = SIZE - addr;
    if (fileSize > maxLen) fileSize = maxLen;

    file.read(reinterpret_cast<char*>(&ram_[addr]), fileSize);
    return file.good() || file.eof();
}

void Memory::clear() {
    memset(ram_, 0, SIZE);
}

void Memory::fill(uint16_t start, uint16_t end, uint8_t val) {
    for (uint32_t i = start; i <= end && i < SIZE; i++) {
        ram_[i] = val;
    }
}

uint8_t Memory::getScreenChar(int row, int col) const {
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS)
        return ' ';
    return ram_[SCREEN_START + row * SCREEN_COLS + col] & 0x7F;
}
