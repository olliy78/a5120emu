#pragma once
#include <cstdint>
#include <deque>
#include "core/bus/k1520_bus.h"

// K7024 ABS – Abbildsteuerung Bildschirm (Screen Controller Card)
//
// Emulates the A5120 video card:
//   VRAM  : 2048 bytes at 0xF800–0xFFFF (80×24 = 1920 bytes used)
//   Output: 640×288 pixel monochrome framebuffer (8px × 12px per cell)
//   EPROM : 1024-byte character generator (characters 0x20–0x7F)

class K7024 : public MemDevice {
public:
    struct A5120Config {
        uint8_t vram_base_hi;  // VRAM at 0xF800
        bool    use_cyrillic;  // which EPROM charset
        A5120Config() : vram_base_hi(0xF8), use_cyrillic(false) {}
    };

    explicit K7024(K1520Bus& bus, const A5120Config& cfg = A5120Config{});

    // MemDevice (VRAM at 0xF800..0xFFFF)
    uint8_t memRead(uint16_t addr) override;
    void    memWrite(uint16_t addr, uint8_t data) override;
    bool    isWritable() const override { return true; }

    // Register VRAM with bus (called from constructor)
    void attachToBus(K1520Bus& bus);

    // Framebuffer access (640×288 bytes, 0=black / 0xFF=white)
    const uint8_t* getFramebuffer() const;
    int            fbWidth()  const { return 640; }
    int            fbHeight() const { return 288; }
    bool           fbDirty()  const;
    void           fbClearDirty();

    // CLI / console mode: queue text changes instead of (only) rendering
    void setConsoleMode(bool enabled);
    bool pollTextChange(int& x, int& y, char& ch);

    // Direct VRAM helpers for tests (bypass bus)
    uint8_t  vramRead(int col, int row) const;
    void     vramWrite(int col, int row, uint8_t ch);

private:
    void renderChar(int col, int row);
    void renderAll();

    K1520Bus&   bus_;
    A5120Config cfg_;
    uint8_t     vram_[2048]{};
    uint8_t     framebuffer_[640 * 288]{};
    bool        dirty_        = false;
    bool        console_mode_ = false;

    struct TextChange { int x, y; char ch; };
    std::deque<TextChange> text_changes_;
};
