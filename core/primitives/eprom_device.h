/**
 * @file eprom_device.h
 * @brief Erasable Programmable Read-Only Memory (EPROM) Emulation - Header File
 *
 * Provides a read-only memory device implementation backed by compile-time
 * constant arrays. EPROMDevice is used throughout the A5120 emulator for
 * storing firmware, character generators, lookup tables, and other read-only
 * data that would be stored in physical EPROMs in the real hardware.
 *
 * Key Features:
 * - Template-based design for compile-time size specification
 * - Zero runtime overhead - data embedded directly in binary
 * - True ROM behavior - writes are silently ignored
 * - Open-drain bus behavior - returns 0xFF for out-of-bounds reads
 * - MemDevice interface compatible with K1520Bus memory mapping
 *
 * Implementation Notes:
 * - Data is stored as const pointers to compile-time constant arrays
 * - Template parameter N specifies array size at compile time
 * - Compiler can optimize based on known size
 * - Write operations are no-ops, matching real EPROM behavior
 * - Out-of-bounds reads return 0xFF (typical open-bus behavior)
 *
 * Typical Usage in A5120:
 * - K2526 Bootstrap ROM: System initialization code (2 KB)
 * - K7024 Character Generator ROM: Display font data (2 KB)
 * - K7637 Keyboard Scan Code Tables: Key mapping tables (1 KB)
 * - K8025 Extension ROM: Optional firmware extensions
 *
 * Design Rationale:
 * By using a template with compile-time constant arrays, the ROM data
 * is embedded directly in the executable with no runtime allocation or
 * initialization overhead. This is more efficient than runtime file loading
 * and ensures the ROM data is always available.
 *
 * Example Usage:
 * @code
 *   // Define ROM data (typically in rom_k2526.h)
 *   constexpr uint8_t K2526_BOOTSTRAP_ROM[2048] = {
 *       0xC3, 0x00, 0x01,  // JP 0x0100 (boot vector)
 *       // ... rest of bootstrap code
 *   };
 *
 *   // Create EPROM device and register with bus
 *   EPROMDevice<2048> bootstrap_rom(K2526_BOOTSTRAP_ROM);
 *   bus.registerMem(&bootstrap_rom, 0x0000, 2048);
 * @endcode
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Robotron A5120 Technical Documentation (Memory Map, K2526 Bootstrap ROM)
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <cstddef>

/**
 * @class EPROMDevice
 * @brief Read-only memory region (ROM) backed by a compile-time constant array.
 * 
 * This template class provides a MemDevice implementation where the actual data
 * is a const array embedded at compile time. All write operations are silently
 * ignored (ROM behavior).
 * 
 * The template parameter N is the size of the array, allowing the compiler to
 * optimize for the specific size and embed the data directly in the binary.
 * 
 * Example usage:
 * ```cpp
 * // Declare ROM data (typically in a separate .h file like rom_data.h)
 * constexpr uint8_t K2526_ROM_DATA[1024] = { ... };
 * 
 * // Create EPROM device
 * EPROMDevice<1024> bootstrap_rom(K2526_ROM_DATA);
 * bus.registerMem(&bootstrap_rom, 0x0000, 1024);
 * ```
 */
template<size_t N>
class EPROMDevice : public MemDevice {
public:
    /**
     * @brief Create an EPROM device backed by a const array.
     * 
     * The lifetime of `data` must exceed the lifetime of this EPROMDevice.
     * Typically, data comes from a constexpr array defined at file scope.
     * 
     * @param data Reference to compile-time const array of size N
     */
    explicit EPROMDevice(const uint8_t (&data)[N]) : data_(data) {}

    /**
     * @brief Read one byte from ROM.
     * 
     * Returns 0xFF if address is out of bounds (open-drain behavior).
     * 
     * @param addr Relative address within EPROM (0 = device base)
     * @return Data byte at that address
     */
    uint8_t memRead(uint16_t addr) override {
        return (addr < N) ? data_[addr] : 0xFF;
    }

    /**
     * @brief Attempt to write to ROM.
     * 
     * Write operations on ROM are silently ignored (no-op).
     * This is true ROM behavior: writes do not change the data.
     * 
     * @param addr Relative address (ignored)
     * @param data Byte to write (ignored)
     */
    void memWrite(uint16_t, uint8_t) override {}

    /**
     * @brief Check if this memory is writable.
     * 
     * @return Always false for EPROM (read-only)
     */
    bool isWritable() const override { return false; }

    /**
     * @brief Get the size of this EPROM in bytes.
     * 
     * @return N (the template size parameter)
     */
    size_t size() const { return N; }

private:
    const uint8_t* data_;  ///< Pointer to compile-time const array
};
