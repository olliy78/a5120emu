/**
 * @file ram_device.h
 * @brief Dynamic RAM (DRAM) emulation for memory-mapped devices.
 * 
 * Provides a simple array-based RAM implementation that can be registered
 * with the K1520Bus for memory access. Used for main memory (K3526) and
 * VRAM (K7024), or any other RAM-based memory device.
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <vector>
#include <cstring>

/**
 * @class RAMDevice
 * @brief Writable memory region (RAM) for the K1520 bus.
 * 
 * This is a simple linear array of bytes that implements the MemDevice
 * interface. The array is allocated dynamically at construction time.
 * Reads and writes are simple array accesses with bounds checking.
 * 
 * Typical usage:
 * ```cpp
 * RAMDevice main_mem(65536);  // 64 KB
 * bus.registerMem(&main_mem, 0x0400, 0xF800);
 * ```
 */
class RAMDevice : public MemDevice {
public:
    /**
     * @brief Create a RAM device of given size.
     * 
     * The RAM is initialized to 0x00.
     * 
     * @param size Number of bytes to allocate (e.g., 65536 for 64 KB)
     */
    explicit RAMDevice(uint16_t size) : mem_(size, 0x00) {}

    /**
     * @brief Read one byte from RAM.
     * 
     * Returns 0xFF if address is out of bounds (open-drain bus behavior).
     * 
     * @param addr Relative address within this device (0 = device base)
     * @return Data byte at that address
     */
    uint8_t memRead(uint16_t addr) override {
        return (addr < mem_.size()) ? mem_[addr] : 0xFF;
    }

    /**
     * @brief Write one byte to RAM.
     * 
     * Write is silently ignored if address is out of bounds.
     * 
     * @param addr Relative address
     * @param data Byte to write
     */
    void memWrite(uint16_t addr, uint8_t data) override {
        if (addr < mem_.size()) mem_[addr] = data;
    }

    /**
     * @brief Check if this memory is writable.
     * @return Always true for RAM
     */
    bool isWritable() const override { return true; }

    /**
     * @brief Get raw pointer to RAM array.
     * 
     * Useful for direct access in tests or initialization.
     * WARNING: Do not write through this pointer from multiple threads!
     * 
     * @return Pointer to first byte of RAM
     */
    uint8_t* data() { return mem_.data(); }
    
    /**
     * @brief Get const pointer to RAM array.
     * @return Const pointer to first byte
     */
    const uint8_t* data() const { return mem_.data(); }
    
    /**
     * @brief Get size of RAM in bytes.
     * @return Number of bytes allocated
     */
    uint16_t size() const { return static_cast<uint16_t>(mem_.size()); }

    /**
     * @brief Fill entire RAM with a value (default 0x00).
     * 
     * Useful for test setup or startup initialization.
     * 
     * @param value Byte value to fill with (default 0x00)
     */
    void fill(uint8_t value = 0x00) {
        std::fill(mem_.begin(), mem_.end(), value);
    }

    /**
     * @brief Load binary data into RAM at offset.
     * 
     * Copies `len` bytes from `src` into this RAM starting at `offset`.
     * The operation is silently ignored if it would exceed RAM size.
     * 
     * @param src Source buffer (typically from loaded file or EPROM data)
     * @param offset Offset in this RAM where data starts
     * @param len Number of bytes to copy
     */
    void load(const uint8_t* src, uint16_t offset, uint16_t len) {
        if (offset + len <= mem_.size())
            std::memcpy(mem_.data() + offset, src, len);
    }

private:
    std::vector<uint8_t> mem_;  ///< Linear array of RAM bytes
};
