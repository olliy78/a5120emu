/**
 * @file k7023.cpp
 * @brief K7023 / K7023.01 (BAB1) – siehe k7023.h und doc/a5120_alt/k7023.md.
 */
#include "core/cards/k7023/k7023.h"
#include "core/cards/k7023/chargen_k7023_zg1.h"
#include "core/cards/k7023/chargen_k7023_zg2.h"
#include <algorithm>
#include <cstring>

K7023::K7023(K1520Bus& bus) : K7023(bus, Config{}) {}

K7023::K7023(K1520Bus& bus, const Config& cfg) : bus_(bus), cfg_(cfg)
{
    cfg_.vram_base &= 0xFC00;   // nur A10-A15 sind wählbar
    if (!cfg_.chargen_lines0_7)  cfg_.chargen_lines0_7  = CHARGEN_K7023_ZG1;
    if (!cfg_.chargen_lines8_15) cfg_.chargen_lines8_15 = CHARGEN_K7023_ZG2;
    // Einschaltzustand 0xFF wie bei der K7024: das Lade-ROM liest seinen Stapel bei FFFFH.
    std::fill(std::begin(vram_), std::end(vram_), uint8_t{0xFF});
    renderAll();
    dirty_ = true;
    attachToBus(bus);
}

void K7023::attachToBus(K1520Bus& bus) { bus.registerMem(this, cfg_.vram_base, kVramSize); }

uint8_t K7023::memRead(uint16_t addr)
{
    uint16_t off = addr - cfg_.vram_base;
    return off < kVramSize ? vram_[off] : 0xFF;
}

void K7023::memWrite(uint16_t addr, uint8_t data)
{
    uint16_t off = addr - cfg_.vram_base;
    if (off >= kVramSize) return;
    bool marke_alt = istMarke(vram_[off]);
    vram_[off] = data;
    // Eine Marke (alt oder neu) ändert den Zustand aller folgenden Zellen.
    if (cfg_.variante_01 && (marke_alt || istMarke(data))) renderAll();
    else renderCell(off);
    dirty_ = true;
}

uint8_t K7023::vramRead(int col, int row) const
{
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return 0;
    return vram_[row * kCols + col];
}

void K7023::vramWrite(int col, int row, uint8_t v)
{
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
    memWrite(cfg_.vram_base + row * kCols + col, v);
}

void K7023::clearScreen(uint8_t blank)
{
    std::fill(std::begin(vram_), std::end(vram_), blank);
    renderAll();
    dirty_ = true;
}

void K7023::setBlinkPhase(bool on)
{
    if (blink_on_ == on) return;
    blink_on_ = on;
    if (cfg_.cursor_blink) { renderAll(); dirty_ = true; }
}

void K7023::serialize(std::vector<uint8_t>& out) const
{
    out.insert(out.end(), vram_, vram_ + sizeof(vram_));
}

bool K7023::deserialize(const uint8_t*& p, const uint8_t* end)
{
    if (end - p < static_cast<long>(sizeof(vram_))) return false;
    std::memcpy(vram_, p, sizeof(vram_));
    p += sizeof(vram_);
    renderAll();
    dirty_ = true;
    return true;
}

void K7023::renderCell(int idx)
{
    const uint8_t v      = vram_[idx];
    const uint8_t code   = v & 0x7F;
    const bool    cursor = (v & 0x80) && (!cfg_.cursor_blink || blink_on_);
    const bool    hell   = !cfg_.variante_01 || hell_vor_[idx];
    const uint8_t pn     = cfg_.variante_01 ? cfg_.helligkeit_normal : 0xFF;
    const uint8_t pz     = hell ? 0xFF : pn;
    const int col = idx % kCols, row = idx / kCols;

    for (int line = 0; line < kCellH; ++line) {
        uint8_t bits = 0;
        uint8_t pix  = pz;
        if (line <= 10)
            bits = (line < 8 ? cfg_.chargen_lines0_7[code * 8 + line]
                             : cfg_.chargen_lines8_15[code * 8 + line - 8]) & 0xFE; // 8. Punkt dunkel
        else if (line >= 12 && cursor) { bits = 0xFF; pix = 0xFF; }  // Kursor stets hell
        uint8_t* dst = &framebuffer_[(row * kCellH + line) * kFbW + col * kCellW];
        for (int x = 0; x < kCellW; ++x)
            dst[x] = ((bits >> (7 - x)) & 1) ? pix : 0x00;
    }
}

void K7023::renderAll()
{
    bool hell = false;   // Anfangszustand: normal
    for (int i = 0; i < kVramSize; ++i) {
        hell_vor_[i] = hell;
        if (cfg_.variante_01 && istMarke(vram_[i])) hell = (vram_[i] & 0x02) != 0;  // DB1
        renderCell(i);
    }
}
