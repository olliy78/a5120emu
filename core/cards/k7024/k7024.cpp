#include "core/cards/k7024/k7024.h"
#include "core/cards/k7024/charset_latin.h"
#include "core/cards/k7024/charset_cyrillic.h"

// ─── Character-generator lookup ───────────────────────────────────────────────
//
// The physical EPROM stores 96 printable characters starting at code 0x20 (space).
// Each character occupies 8 consecutive bytes (one byte per pixel row 0–7).
//
// EPROM address = (charCode - 0x20) * 8 + pixelRow   for charCode 0x20..0x7F
//
// Characters 0x00–0x1F (control codes) are not in the ROM → render as blank.
// Pixel rows 8–11 of every 12-line cell are always blank except for the cursor.

static inline uint8_t chargenLookup(uint8_t charCode, int pixelRow,
                                    const uint8_t* charset)
{
    if (pixelRow >= 8 || charCode < 0x20) return 0x00;
    uint16_t addr = static_cast<uint16_t>((charCode - 0x20) * 8 + pixelRow);
    return (addr < 1024) ? charset[addr] : 0x00;
}

// ─── K7024 implementation ─────────────────────────────────────────────────────

K7024::K7024(K1520Bus& bus, const A5120Config& cfg)
    : bus_(bus), cfg_(cfg)
{
    renderAll();
    attachToBus(bus);
}

void K7024::attachToBus(K1520Bus& bus)
{
    uint16_t base = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    bus.registerMem(this, base, 0x0800);   // 2048 bytes: 0xF800..0xFFFF
}

// ─── MemDevice interface ──────────────────────────────────────────────────────

uint8_t K7024::memRead(uint16_t addr)
{
    uint16_t base   = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    uint16_t offset = addr - base;
    if (offset >= 2048) return 0xFF;
    return vram_[offset];
}

void K7024::memWrite(uint16_t addr, uint8_t data)
{
    uint16_t base   = static_cast<uint16_t>(cfg_.vram_base_hi) << 8;
    uint16_t offset = addr - base;
    if (offset >= 2048) return;

    vram_[offset] = data;

    int col = static_cast<int>(offset % 80);
    int row = static_cast<int>(offset / 80);

    renderChar(col, row);
    dirty_ = true;

    if (console_mode_) {
        char ch = static_cast<char>(data & 0x7F);
        if (ch < 0x20) ch = ' ';
        text_changes_.push_back({col, row, ch});
    }
}

// ─── Framebuffer interface ────────────────────────────────────────────────────

const uint8_t* K7024::getFramebuffer() const { return framebuffer_; }
bool           K7024::fbDirty()  const        { return dirty_; }
void           K7024::fbClearDirty()          { dirty_ = false; }

// ─── CLI / console mode ───────────────────────────────────────────────────────

void K7024::setConsoleMode(bool enabled) { console_mode_ = enabled; }

bool K7024::pollTextChange(int& x, int& y, char& ch)
{
    if (text_changes_.empty()) return false;
    auto& tc = text_changes_.front();
    x = tc.x;  y = tc.y;  ch = tc.ch;
    text_changes_.pop_front();
    return true;
}

// ─── Direct VRAM helpers (for tests) ─────────────────────────────────────────

uint8_t K7024::vramRead(int col, int row) const
{
    return vram_[row * 80 + col];
}

void K7024::vramWrite(int col, int row, uint8_t ch)
{
    int offset = row * 80 + col;
    vram_[offset] = ch;
    renderChar(col, row);
    dirty_ = true;
}

// ─── Rendering ────────────────────────────────────────────────────────────────

void K7024::renderChar(int col, int row)
{
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

void K7024::renderAll()
{
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 80; ++col)
            renderChar(col, row);
}
