/**
 * @file ram_device.h
 * @brief Dynamic Random Access Memory (RAM) Emulation - Header File
 *
 * Provides a writable memory device implementation using a dynamically allocated
 * byte array. RAMDevice is used throughout the A5120 emulator for main system
 * RAM, video RAM (VRAM), and any other volatile memory regions that require
 * read and write access.
 *
 * Key Features:
 * - Dynamic allocation at construction time
 * - Full read/write access with bounds checking
 * - MemDevice interface compatible with K1520Bus memory mapping
 * - Utility methods for initialization and bulk loading
 * - Direct memory access for testing and debugging
 * - Open-drain bus behavior for out-of-bounds reads
 *
 * Implementation Notes:
 * - Backed by std::vector<uint8_t> for automatic memory management
 * - Initialized to 0x00 on construction (simulating power-on state)
 * - Bounds-checked access: out-of-range reads return 0xFF
 * - Out-of-range writes are silently ignored
 * - Thread-safety: not thread-safe, assumes single-threaded access
 *
 * Typical Usage in A5120:
 * - K3526 Main System RAM: 64 KB of working memory
 * - K7024 Video RAM (VRAM): 16 KB for character display buffer
 * - K8025 Extension RAM: Optional additional memory on expansion cards
 * - Workspace memory for user programs and operating system
 *
 * Memory Initialization:
 * RAM is initialized to 0x00 on construction, but real DRAM contains
 * unpredictable values at power-on. For more realistic testing, use
 * fill() with random values or specific test patterns.
 *
 * Direct Access:
 * The data() method provides raw pointer access for efficiency in
 * specific cases (bulk operations, testing, debugger access). Use
 * with caution - direct access bypasses bounds checking.
 *
 * Example Usage:
 * @code
 *   // Create 64 KB main RAM
 *   RAMDevice main_ram(65536);
 *   bus.registerMem(&main_ram, 0x0000, 65536);
 *
 *   // Initialize with test pattern
 *   main_ram.fill(0xAA);
 *
 *   // Load program into RAM at 0x0100
 *   uint8_t program[] = { 0x3E, 0x01, 0xC9 };  // LD A,1 : RET
 *   main_ram.load(program, 0x0100, sizeof(program));
 *
 *   // Direct access for testing
 *   uint8_t* ram_ptr = main_ram.data();
 *   assert(ram_ptr[0x0100] == 0x3E);
 * @endcode
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Robotron A5120 Technical Documentation (Memory Map, K3526 RAM Card)
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
