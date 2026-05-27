/**
 * @file k7024.cpp
 * @brief K7024 ABS – Abbildsteuerung Bildschirm (screen controller card) implementation.
 *
 * Implements the K7024 video card for the Robotron A5120 system.
 * The card provides a 2 KB character-cell VRAM mapped at 0xF800–0xFFFF and
 * renders the 80×24 character grid into a 640×288 monochrome pixel framebuffer.
 *
 * Character rendering:
 *   Each VRAM byte encodes one character cell: bits [6:0] = ASCII code,
 *   bit 7 = cursor (inverts pixel rows 10–11).  The character generator
 *   EPROM provides 8×8 glyphs for codes 0x20–0x7F; codes 0x00–0x1F render
 *   as blank.  Each 8×8 glyph is padded to 8×12 pixels (rows 8–11 blank).
 *
 * EPROM address formula:
 * @code
 *   addr = (charCode - 0x20) * 8 + pixelRow   for charCode 0x20..0x7F
 * @endcode
 *
 * @see k7024.h
 * @see doc/design/05_k7024_abs.md
 * @author Olaf Krieger
 * @date 2024–2025
 * @license MIT License
 */

#include "core/cards/k7024/k7024.h"
#include "core/cards/k7024/charset_latin.h"
#include "core/cards/k7024/charset_cyrillic.h"
#include <algorithm>

// ─── Character-generator lookup ───────────────────────────────────────────────

/**
 * @brief Look up one pixel row of a character glyph in the character-generator ROM.
 *
 * The EPROM stores 96 printable characters starting at code 0x20 (space).
 * Each character occupies 8 consecutive bytes, one per pixel row 0–7.
 * Characters 0x00–0x1F and pixel rows 8–11 always return 0x00 (blank).
 *
 * @param charCode  ASCII character code (0x00–0x7F)
 * @param pixelRow  Pixel row within the cell (0–11; rows 8–11 always blank)
 * @param charset   Pointer to the 1024-byte character generator ROM data
 * @return 8-bit pixel row bitmap (bit 7 = leftmost pixel)
 */
static inline uint8_t chargenLookup(uint8_t charCode, int pixelRow,
                                    const uint8_t* charset)
{
    if (pixelRow >= 8 || charCode < 0x20) return 0x00;
    uint16_t addr = static_cast<uint16_t>((charCode - 0x20) * 8 + pixelRow);
    return (addr < 1024) ? charset[addr] : 0x00;
}

// ─── K7024 implementation ─────────────────────────────────────────────────────

/**
 * @brief Construct a K7024 screen controller and register its VRAM on the bus.
 *
 * Initialises VRAM with pseudo-random bytes to simulate the power-on noise
 * visible on real hardware, performs an initial full render into the framebuffer,
 * and registers the 2 KB VRAM region on the K1520 bus.
 *
 * @param bus K1520 system bus reference (must outlive this object)
 * @param cfg Hardware configuration: VRAM base address and character set selection
 */
K7024::K7024(K1520Bus& bus, const A5120Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    // Real DRAM powers on to an indeterminate but typically 0xFF state.
    // The boot ROM relies on this: the Z80 stack starts at 0xFFFF (in K7024
    // VRAM), and RET M at ROM[0x0016] pops [0xFFFF] as PCL.  With 0xFF the
    // CPU lands at 0xEEFF (K3526 RAM = 0xFF = RST 38h) → boot entry 0x0038.
    std::fill(std::begin(vram_), std::end(vram_), uint8_t{0xFF});

    renderAll();
    dirty_ = true;
    attachToBus(bus);
}

/**
 * @brief Register the 2 KB VRAM region with the K1520 bus.
 *
 * Maps this device as a MemDevice at [cfg_.vram_base_hi << 8, +0x0800).
 * For the standard A5120 configuration this is 0xF800–0xFFFF.
 *
 * K7024 must be registered *after* K3526 so the bus dispatches reads and
 * writes in this range to K7024 rather than the underlying RAM.
 *
 * @param bus K1520 system bus to register on
 */
void K7024::attachToBus(K1520Bus& bus)
{
    uint16_t base = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    bus.registerMem(this, base, 0x0800);   // 2048 bytes: 0xF800..0xFFFF
}

// ─── MemDevice interface ──────────────────────────────────────────────────────

/**
 * @brief Read one byte from VRAM.
 *
 * Returns the character code stored at @p addr in the VRAM backing array.
 * Returns 0xFF for addresses outside the 2 KB VRAM window.
 *
 * @param addr Bus address in the range 0xF800–0xFFFF
 * @return Character code at the VRAM location, or 0xFF if out of range
 */
uint8_t K7024::memRead(uint16_t addr)
{
    uint16_t base   = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    uint16_t offset = addr - base;
    if (offset >= 2048) return 0xFF;
    return vram_[offset];
}

/**
 * @brief Write one byte to VRAM and re-render the affected character cell.
 *
 * Stores @p data in the VRAM backing array at @p addr, then re-renders the
 * character cell that was modified, marks the framebuffer dirty, and (in
 * console mode) queues a TextChange record for the ANSI output path.
 * Writes to offsets ≥ 80×24 = 1920 (unused VRAM tail) are stored but not
 * rendered.
 *
 * @param addr Bus address in the range 0xF800–0xFFFF
 * @param data Character code to write
 */
void K7024::memWrite(uint16_t addr, uint8_t data)
{
    uint16_t base   = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    uint16_t offset = addr - base;
    if (offset >= 2048) return;

    vram_[offset] = data;

    int col = static_cast<int>(offset % 80);
    int row = static_cast<int>(offset / 80);

    // Only the first 80x24 bytes are visible screen memory. The remaining
    // bytes in the 2 KB VRAM window are not rendered to the framebuffer.
    if (row >= 24) {
        return;
    }

    renderChar(col, row);
    dirty_ = true;

    if (console_mode_) {
        char ch = static_cast<char>(data & 0x7F);
        if (ch < 0x20) ch = ' ';
        text_changes_.push_back({col, row, ch});
    }
}

// ─── Framebuffer interface ────────────────────────────────────────────────────

/**
 * @brief Return a pointer to the rendered 640×288 pixel framebuffer.
 *
 * The buffer is laid out in row-major order (640 bytes per row).
 * Each byte is 0x00 (black) or 0xFF (white).
 *
 * @return Read-only pointer to the 640×288-byte framebuffer (valid for object lifetime)
 */
const uint8_t* K7024::getFramebuffer() const { return framebuffer_; }

/**
 * @brief Check whether the framebuffer has unseen changes.
 * @return true if at least one VRAM write has occurred since the last fbClearDirty()
 */
bool           K7024::fbDirty()  const        { return dirty_; }

/**
 * @brief Clear the dirty flag after the framebuffer has been presented to the display.
 */
void           K7024::fbClearDirty()          { dirty_ = false; }

// ─── CLI / console mode ───────────────────────────────────────────────────────

/**
 * @brief Enable or disable console (ANSI terminal) mode.
 *
 * When enabled, each VRAM write additionally queues a TextChange in
 * text_changes_ so the ANSI terminal output path can update only the
 * changed cells without re-reading the entire framebuffer.
 *
 * @param enabled true to activate console mode; false to disable it
 */
void K7024::setConsoleMode(bool enabled) { console_mode_ = enabled; }

/**
 * @brief Dequeue one text change from the console-mode change queue.
 *
 * Pops the front of the text_changes_ deque and fills the output parameters.
 * Returns false and leaves the output parameters unmodified if the queue is empty.
 *
 * @param x  [out] Column index of the changed cell (0–79)
 * @param y  [out] Row index of the changed cell (0–23)
 * @param ch [out] New character code (printable ASCII, control codes mapped to ' ')
 * @return true if a change was dequeued; false if the queue is empty
 */
bool K7024::pollTextChange(int& x, int& y, char& ch)
{
    if (text_changes_.empty()) return false;
    auto& tc = text_changes_.front();
    x = tc.x;  y = tc.y;  ch = tc.ch;
    text_changes_.pop_front();
    return true;
}

// ─── Direct VRAM helpers (for tests) ─────────────────────────────────────────

/**
 * @brief Read a character code directly from VRAM, bypassing the bus.
 *
 * Accesses the flat VRAM array directly at row × 80 + col.
 * No bounds check on col and row – callers must ensure validity.
 *
 * @param col Column index (0–79)
 * @param row Row index    (0–23)
 * @return Character code stored at the cell
 */
uint8_t K7024::vramRead(int col, int row) const
{
    return vram_[row * 80 + col];
}

/**
 * @brief Write a character code directly to VRAM and re-render the cell.
 *
 * Stores @p ch at row × 80 + col in the VRAM array, calls renderChar(),
 * and marks the framebuffer dirty.  Silently ignores out-of-range
 * (col, row) coordinates.
 *
 * @param col Column index (0–79)
 * @param row Row index    (0–23)
 * @param ch  Character code to write
 */
void K7024::vramWrite(int col, int row, uint8_t ch)
{
    if (col < 0 || col >= 80 || row < 0 || row >= 24) return;

    int offset = row * 80 + col;
    vram_[offset] = ch;
    renderChar(col, row);
    dirty_ = true;
}

// ─── Rendering ────────────────────────────────────────────────────────────────

/**
 * @brief Render one character cell into the framebuffer.
 *
 * Reads the VRAM byte at (col, row), extracts the 7-bit character code and
 * the cursor flag (bit 7), looks up the 8×12 glyph via chargenLookup(), and
 * writes pixels into framebuffer_ at the correct screen position.
 *
 * Pixel rows 10–11 of the cell are inverted when the cursor flag is set,
 * producing an underline cursor.
 *
 * @param col Column index (0–79); silently ignored if out of range
 * @param row Row index    (0–23); silently ignored if out of range
 */
void K7024::renderChar(int col, int row)
{
    if (col < 0 || col >= 80 || row < 0 || row >= 24) return;

    const uint8_t* charset = cfg_.use_cyrillic ? CHARSET_CYRILLIC : CHARSET_LATIN;

    uint8_t vbyte    = vram_[row * 80 + col];
    uint8_t charCode = vbyte & 0x7F;
    bool    cursor   = (vbyte & 0x80) != 0;

    for (int pixRow = 0; pixRow < 12; ++pixRow) {
        int     fb_y       = row * 12 + pixRow;
        uint8_t eprom_byte = chargenLookup(charCode, pixRow, charset);

        for (int pixCol = 0; pixCol < 8; ++pixCol) {
            int     bit   = (eprom_byte >> (7 - pixCol)) & 1;
            uint8_t pixel = bit ? 0xFF : 0x00;

            // Cursor: invert pixel rows 10–11 of this character cell
            if (cursor && pixRow >= 10) pixel ^= 0xFF;

            framebuffer_[fb_y * 640 + col * 8 + pixCol] = pixel;
        }
    }
}

/**
 * @brief Re-render the entire 80×24 character grid into the framebuffer.
 *
 * Iterates over all cells and calls renderChar() for each.  Used once during
 * construction to produce the initial framebuffer content from the randomised
 * VRAM.
 */
void K7024::renderAll()
{
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 80; ++col)
            renderChar(col, row);
}
