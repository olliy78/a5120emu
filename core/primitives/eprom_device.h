/**
 * @file eprom_device.h
 * @brief Erasable Programmable Read-Only Memory (EPROM) emulation.
 * 
 * Provides a read-only memory device backed by a compile-time constant array.
 * Used for ROM data like bootstrap code (K2526), character generators (K7024),
 * and keyboard scan code tables (K7637).
 * 
 * EPROMDevice is a template class where the data is passed as a template argument,
 * allowing the entire ROM data to be embedded in the compiled binary with zero overhead.
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
