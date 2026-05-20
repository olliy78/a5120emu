/**
 * @file k7024.h
 * @brief K7024 ABS – Abbildsteuerung Bildschirm (screen controller card) emulation.
 *
 * Emulates the K7024 video card for the Robotron A5120 system.
 * The physical card provides a 2 KB character-cell VRAM and a character
 * generator EPROM.  The emulation renders the character grid into a
 * 640×288 monochrome pixel framebuffer for display output.
 *
 * Hardware overview:
 * @code
 *   VRAM     : 2048 bytes mapped at 0xF800–0xFFFF (bus-visible)
 *   Screen   : 80 columns × 24 rows = 1920 character cells (bytes used)
 *   Cell size: 8×12 pixels
 *   Output   : 640×288 pixel monochrome framebuffer (0x00 = black, 0xFF = white)
 *   Charset  : ASCII 0x20–0x7F from built-in character generator (Latin or Cyrillic)
 * @endcode
 *
 * Memory registration:
 *   K7024 registers itself as a writable MemDevice at 0xF800–0xFFFF.
 *   Because K3526 also covers 0xC000–0xFFFF, K7024 must be registered
 *   *after* K3526 so the bus dispatches reads/writes to K7024 for that range.
 *
 * Rendering:
 *   Every write to VRAM triggers renderChar() for the affected cell.
 *   The framebuffer is marked dirty and can be polled with fbDirty().
 *
 * Console mode:
 *   When enabled, text changes are also queued in a deque so that a
 *   terminal (ANSI) output path can consume them without reading the
 *   full framebuffer.
 *
 * @see doc/design/05_k7024_abs.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#pragma once
#include <cstdint>
#include <deque>
#include "core/bus/k1520_bus.h"

/**
 * @class K7024
 * @brief Emulation of the K7024 ABS (Abbildsteuerung Bildschirm) screen controller card.
 *
 * Features:
 * - 2 KB VRAM at configurable base (default 0xF800–0xFFFF)
 * - Renders characters into a 640×288 pixel monochrome framebuffer
 * - Two character sets: Latin (default) and Cyrillic
 * - Console mode: text changes queued for ANSI terminal output
 * - Direct VRAM helpers for unit test access (vramRead / vramWrite)
 *
 * Typical usage:
 * @code
 *   K1520Bus bus;
 *   K7024 screen(bus);          // registers VRAM on bus in constructor
 *   screen.attachToBus(bus);    // (no-op if already registered in constructor)
 *   const uint8_t* fb = screen.getFramebuffer();  // 640×288 bytes
 * @endcode
 */
class K7024 : public MemDevice {
public:
    /** @brief Hardware configuration for the K7024. */
    struct A5120Config {
        uint8_t vram_base_hi; ///< High byte of VRAM base address (default 0xF8 → 0xF800)
        bool    use_cyrillic; ///< true to use Cyrillic character generator EPROM
        A5120Config() : vram_base_hi(0xF8), use_cyrillic(false) {}
    };

    /**
     * @brief Construct a K7024 and register its VRAM on the bus.
     *
     * Calls attachToBus() internally; no need to call it again after construction.
     *
     * @param bus K1520 system bus reference (must outlive this object)
     * @param cfg Hardware configuration (VRAM base address, character set)
     */
    explicit K7024(K1520Bus& bus, const A5120Config& cfg = A5120Config{});

    // ─── MemDevice interface (VRAM 0xF800–0xFFFF) ──────────────────────────

    /**
     * @brief Read one byte from VRAM.
     *
     * Returns the character code stored at @p addr in the VRAM array.
     *
     * @param addr Bus address in range 0xF800–0xFFFF
     * @return Character code at that VRAM location
     */
    uint8_t memRead(uint16_t addr) override;

    /**
     * @brief Write one byte to VRAM and re-render the affected character cell.
     *
     * Stores the new character code and calls renderChar() for the cell,
     * sets the dirty flag, and (in console mode) queues a TextChange.
     *
     * @param addr Bus address in range 0xF800–0xFFFF
     * @param data Character code to write
     */
    void    memWrite(uint16_t addr, uint8_t data) override;

    /**
     * @brief Report that VRAM is writable.
     * @return true (VRAM is read/write)
     */
    bool    isWritable() const override { return true; }

    // ─── Bus registration ────────────────────────────────────────────────────

    /**
     * @brief Register VRAM with the K1520 bus.
     *
     * Called internally by the constructor.  Exposed publicly so it can be
     * called explicitly when creating the K7024 before bus registration order
     * matters (K7024 must be registered after K3526).
     *
     * @param bus K1520 system bus to register on
     */
    void attachToBus(K1520Bus& bus);

    // ─── Framebuffer access ──────────────────────────────────────────────────

    /**
     * @brief Return a pointer to the rendered pixel framebuffer.
     *
     * The framebuffer is a flat array of 640×288 bytes (width-major order).
     * Each byte is 0x00 (black) or 0xFF (white).
     *
     * @return Pointer to 640×288-byte framebuffer (valid for object lifetime)
     */
    const uint8_t* getFramebuffer() const;

    /**
     * @brief Framebuffer width in pixels.
     * @return 640
     */
    int fbWidth()  const { return 640; }

    /**
     * @brief Framebuffer height in pixels.
     * @return 288
     */
    int fbHeight() const { return 288; }

    /**
     * @brief Check whether the framebuffer has changed since the last fbClearDirty().
     * @return true if any VRAM write has occurred since the last clear
     */
    bool fbDirty() const;

    /**
     * @brief Clear the dirty flag after the framebuffer has been consumed.
     */
    void fbClearDirty();

    // ─── Console mode ─────────────────────────────────────────────────────────

    /**
     * @brief Enable or disable console (ANSI terminal) mode.
     *
     * In console mode, each VRAM write also queues a TextChange record so
     * that the terminal output path can update only the changed character
     * cells without re-reading the full framebuffer.
     *
     * @param enabled true to activate console mode
     */
    void setConsoleMode(bool enabled);

    /**
     * @brief Dequeue one text change from the console-mode queue.
     *
     * @param x  [out] Column (0–79)
     * @param y  [out] Row    (0–23)
     * @param ch [out] New character code
     * @return true if a change was dequeued, false if the queue is empty
     */
    bool pollTextChange(int& x, int& y, char& ch);

    // ─── Direct VRAM helpers (for tests, bypass bus) ─────────────────────────

    /**
     * @brief Read a character code directly from VRAM without going through the bus.
     *
     * @param col Column 0–79
     * @param row Row    0–23
     * @return Character code at the cell
     */
    uint8_t vramRead(int col, int row) const;

    /**
     * @brief Write a character code directly to VRAM and re-render the cell.
     *
     * @param col  Column 0–79
     * @param row  Row    0–23
     * @param ch   Character code to write
     */
    void    vramWrite(int col, int row, uint8_t ch);

private:
    /**
     * @brief Render one character cell into the framebuffer.
     *
     * Looks up the character code in vram_[], finds the 8×12 glyph in the
     * character generator, and writes pixels into framebuffer_ at the
     * correct (col, row) position.
     *
     * @param col Column index 0–79
     * @param row Row index    0–23
     */
    void renderChar(int col, int row);

    /**
     * @brief Re-render the entire 80×24 character grid.
     *
     * Called once during construction to initialise the framebuffer.
     */
    void renderAll();

    K1520Bus&   bus_;           ///< K1520 system bus reference
    A5120Config cfg_;           ///< Hardware configuration (VRAM base, charset)
    uint8_t     vram_[2048]{};  ///< 2 KB VRAM backing array (character codes)
    uint8_t     framebuffer_[640 * 288]{}; ///< Rendered pixel framebuffer (0/0xFF per pixel)
    bool        dirty_        = false;     ///< true if framebuffer has unseen changes
    bool        console_mode_ = false;     ///< true = also queue changes for ANSI output

    /** @brief Record of a single character-cell change (for console mode). */
    struct TextChange { int x, y; char ch; };
    std::deque<TextChange> text_changes_;  ///< Pending text changes for console output
};
