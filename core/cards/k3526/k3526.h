/**
 * @file k3526.h
 * @brief K3526 OPS – Operationsspeicher (64 KB DRAM card) emulation.
 *
 * Emulates the K3526 main memory card for the Robotron K1520/A5120 system.
 * The physical card provides 64 KB of DRAM organised as four independent
 * 16 KB groups.  Each group can be individually disabled via the /MEMDI
 * signal, which causes reads to return 0xFF and writes to be silently dropped.
 *
 * Architecture:
 * - Four 16 KB groups; each mapped to a 16 KB-aligned address region
 * - All groups share one flat 64 KB backing array
 * - Each group registers as a separate MemDevice region on the K1520Bus
 * - /MEMDI source per group: main K1520 bus signal or Koppelbus signal
 *   (in this emulation the source selector is a configuration flag only;
 *   both bus signals map to the same setMemDI() call)
 *
 * A5120 standard memory map:
 * @code
 *   Group 0: 0x0000–0x3FFF  (overlaid by K2526 boot ROM at 0x0000–0x03FF on reset)
 *   Group 1: 0x4000–0x7FFF
 *   Group 2: 0x8000–0xBFFF
 *   Group 3: 0xC000–0xFFFF  (top 2 KB overlaid by K7024 VRAM at 0xF800–0xFFFF)
 * @endcode
 *
 * Bus registration order matters:
 *   K3526 registers at the lowest priority.  K2526 (ROM) and K7024 (VRAM)
 *   register on top; memRead dispatches to the last device whose range covers
 *   the address, so the ROM/VRAM shadows the underlying RAM for reads.
 *   memWrite dispatches to the last *writable* device, so writes to 0x0000–
 *   0x03FF always reach K3526 even while the ROM is mapped for reads.
 *
 * @see doc/design/04_k3526_ops.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <array>
#include "core/bus/k1520_bus.h"

/**
 * @brief Per-group configuration for a K3526 memory group.
 *
 * Each of the four 16 KB RAM groups has an independent base address and
 * a /MEMDI source selector used during hardware initialisation.
 */
struct K3526GroupConfig {
    uint16_t base_addr;    ///< 16 KB-aligned start address (0x0000 / 0x4000 / 0x8000 / 0xC000)
    bool     memdi_source; ///< false = /MEMDI from K1520 bus, true = /MEMDI1/2 from Koppelbus
};

/**
 * @class K3526
 * @brief Emulation of the K3526 OPS (Operationsspeicher) 64 KB DRAM card.
 *
 * Features:
 * - 64 KB flat backing array, split into four 16 KB MemDevice regions
 * - Each group can be independently disabled via setMemDI()
 * - isWritable() returns true, allowing writes even when the boot ROM shadows
 *   the lower address range for reads
 * - Utility helpers fill() and load() for test setup and boot-ROM shadowing
 *
 * Typical usage:
 * @code
 *   K1520Bus bus;
 *   K3526 ops;
 *   ops.attachToBus(bus);   // registers groups 0–3 on the bus
 * @endcode
 */
class K3526 : public MemDevice {
public:
    /**
     * @brief Standard A5120 configuration: four groups filling the full 64 KB space.
     *
     * All groups use the K1520 bus /MEMDI signal (memdi_source = false).
     */
    struct A5120Config {
        K3526GroupConfig groups[4] = {
            {0x0000, false},   ///< Group 0: 0x0000–0x3FFF
            {0x4000, false},   ///< Group 1: 0x4000–0x7FFF
            {0x8000, false},   ///< Group 2: 0x8000–0xBFFF
            {0xC000, false}    ///< Group 3: 0xC000–0xFFFF
        };
    };

    /**
     * @brief Construct a K3526 with standard A5120 memory configuration.
     */
    K3526();

    /**
     * @brief Construct a K3526 with explicit group configuration.
     * @param cfg Group base addresses and /MEMDI source selectors
     */
    explicit K3526(const A5120Config& cfg);

    // ─── MemDevice interface ────────────────────────────────────────────────

    /**
     * @brief Read one byte from the K3526 memory array.
     *
     * If the group covering @p addr has /MEMDI asserted (group_memdi_[g] = true),
     * returns 0xFF without accessing the backing array.
     *
     * @param addr 16-bit bus address (0x0000–0xFFFF)
     * @return Data byte, or 0xFF if the group is disabled
     */
    uint8_t memRead(uint16_t addr) override;

    /**
     * @brief Write one byte to the K3526 memory array.
     *
     * If the group covering @p addr has /MEMDI asserted, the write is silently
     * dropped (write-protect).
     *
     * @param addr 16-bit bus address
     * @param data Byte to write
     */
    void    memWrite(uint16_t addr, uint8_t data) override;

    /**
     * @brief Report that this device is writable.
     *
     * Always returns true so that K1520Bus::memWrite() can route writes to
     * K3526 even when a read-only device (e.g. boot ROM) is registered in
     * the same address range.
     *
     * @return true
     */
    bool    isWritable() const override { return true; }

    // ─── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Register all four memory groups on the K1520 bus.
     *
     * Each group is registered as a separate 16 KB region via
     * bus.registerMem().  Must be called before the system runs.
     *
     * @param bus K1520 system bus to register on
     */
    void attachToBus(K1520Bus& bus);

    // ─── /MEMDI group control ────────────────────────────────────────────────

    /**
     * @brief Enable or disable a memory group via the /MEMDI signal.
     *
     * When @p disabled is true, reads from the group return 0xFF and writes
     * are ignored, simulating the bus-float condition produced by /MEMDI.
     *
     * @param group   Group index 0–3
     * @param disabled true to assert /MEMDI for the group (disable access)
     */
    void setMemDI(int group, bool disabled);

    // ─── Utility helpers ────────────────────────────────────────────────────

    /**
     * @brief Fill the entire backing array with a constant byte.
     *
     * Useful for test initialisation or RAM clear.
     *
     * @param value Byte to write to all 65536 locations (default 0x00)
     */
    void fill(uint8_t value = 0x00);

    /**
     * @brief Load a data block into the backing array at a given offset.
     *
     * Copies @p len bytes from @p data into the backing array starting at
     * @p addr.  No group-disable check is performed (direct write).
     *
     * @param addr Start address in the backing array
     * @param data Source data pointer
     * @param len  Number of bytes to copy
     */
    void load(uint16_t addr, const uint8_t* data, uint16_t len);

    /**
     * @brief Return a read-only pointer to the raw backing array.
     *
     * Useful for test inspection or framebuffer export.
     *
     * @return Pointer to the 65536-byte backing array
     */
    const uint8_t* rawPtr() const;

private:
    A5120Config                cfg_;              ///< Group base addresses and /MEMDI sources
    std::array<uint8_t, 65536> mem_{};            ///< Flat 64 KB backing array
    bool                       group_memdi_[4]{}; ///< true = group is disabled (/MEMDI asserted)
};
