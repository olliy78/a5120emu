/**
 * @file k3526.cpp
 * @brief K3526 OPS – Operationsspeicher (64 KB DRAM card) implementation.
 *
 * Provides 64 KB of main memory for the Robotron K1520/A5120 system,
 * organised as four independent 16 KB groups.  Each group can be individually
 * disabled via the /MEMDI signal, emulating the bus-float condition produced
 * when the hardware disables a memory bank.
 *
 * Bus registration:
 *   Each group registers itself as a separate MemDevice region on the K1520Bus.
 *   K3526 must be registered before K2526 (ROM) and K7024 (VRAM) so that those
 *   devices can shadow their address ranges for reads, while K3526 still accepts
 *   writes (isWritable() always returns true).
 *
 * @see k3526.h
 * @see doc/design/04_k3526_ops.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#include "core/cards/k3526/k3526.h"
#include <cstring>

/** @brief Construct with default A5120 memory layout (four 16 KB groups, 0x0000–0xFFFF). */
K3526::K3526() : K3526(A5120Config{}) {}

/**
 * @brief Construct with an explicit group configuration.
 *
 * Initialises the flat backing array to zero.
 *
 * @param cfg Group base addresses and /MEMDI source selectors
 */
K3526::K3526(const A5120Config& cfg) : cfg_(cfg) {
    // Real DRAM powers on to an indeterminate state, typically 0xFF.
    // The boot ROM relies on this: after an intentional RET P at 0x000A the
    // CPU lands in uninitialized RAM and hits 0xFF (RST 38h) which vectors
    // back into the ROM's real boot entry at 0x0038.  With 0x00 (NOP) the
    // CPU never returns to ROM and the boot sequence stalls.
    mem_.fill(0xFF);
}

/**
 * @brief Read one byte from the backing array, respecting the group /MEMDI state.
 *
 * Finds which of the four 16 KB groups owns @p addr, then:
 * - Returns 0xFF (bus-float) if that group's /MEMDI is asserted.
 * - Otherwise returns the stored byte.
 *
 * The address is guaranteed to fall within a registered group range after
 * attachToBus() is called; the loop still validates explicitly for safety.
 *
 * @param addr 16-bit bus address (0x0000–0xFFFF)
 * @return Stored byte, or 0xFF if the group is disabled
 */
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

/**
 * @brief Write one byte to the backing array, respecting the group /MEMDI state.
 *
 * Finds which 16 KB group owns @p addr and writes @p data to the backing array.
 * If that group's /MEMDI is asserted, the write is silently dropped.
 * Writes to an address outside all registered groups are silently ignored.
 *
 * @param addr 16-bit bus address
 * @param data Byte to write
 */
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

/**
 * @brief Register all four 16 KB memory groups on the K1520 bus.
 *
 * Each group occupies a 0x4000-byte range starting at its configured base
 * address.  Must be called before the system starts executing code.
 *
 * @param bus K1520 system bus to register on
 */
void K3526::attachToBus(K1520Bus& bus) {
    for (int i = 0; i < 4; ++i) {
        bus.registerMem(this, cfg_.groups[i].base_addr, 0x4000);
    }
}

/**
 * @brief Enable or disable a memory group via the /MEMDI signal.
 *
 * When @p disabled is true the group is put into bus-float mode: reads
 * return 0xFF and writes are ignored.  Setting @p disabled to false
 * restores normal access.
 *
 * @param group   Group index 0–3
 * @param disabled true to assert /MEMDI (disable), false to release it
 */
void K3526::setMemDI(int group, bool disabled) {
    if (group >= 0 && group < 4)
        group_memdi_[group] = disabled;
}

/**
 * @brief Fill the entire 64 KB backing array with a constant byte.
 *
 * Bypasses group /MEMDI state – all locations are written unconditionally.
 * Useful for test initialisation or RAM clear operations.
 *
 * @param value Byte to write to all 65536 locations (default 0x00)
 */
void K3526::fill(uint8_t value) {
    mem_.fill(value);
}

/**
 * @brief Load a data block directly into the backing array.
 *
 * Copies @p len bytes from @p data starting at @p addr.  Bypasses all
 * group /MEMDI checks – the write is unconditional.  Typically used to
 * pre-load ROM content or test vectors.
 *
 * @param addr  Start address in the 65536-byte backing array
 * @param data  Source data pointer
 * @param len   Number of bytes to copy
 */
void K3526::load(uint16_t addr, const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; ++i)
        mem_[static_cast<uint16_t>(addr + i)] = data[i];
}

/**
 * @brief Return a read-only pointer to the raw 64 KB backing array.
 *
 * Useful for unit-test inspection, framebuffer export, or snapshot saves.
 *
 * @return Pointer to the 65536-byte backing array (valid for object lifetime)
 */
const uint8_t* K3526::rawPtr() const {
    return mem_.data();
}

/**
 * @brief Overwrite the entire 64 KB backing array (snapshot restore).
 *
 * Symmetric to rawPtr(): copies 65536 bytes from @p data directly into the
 * backing array (no group-disable check).  Used by A5120Machine::restoreState.
 *
 * @param data Pointer to 65536 source bytes
 */
void K3526::restore(const uint8_t* data) {
    std::memcpy(mem_.data(), data, mem_.size());
}
