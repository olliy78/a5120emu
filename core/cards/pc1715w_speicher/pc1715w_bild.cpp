/**
 * @file pc1715w_bild.cpp
 * @brief Bildquelle des PC 1715W — siehe pc1715w_bild.h.
 */

#include "core/cards/pc1715w_speicher/pc1715w_bild.h"
#include <algorithm>

namespace {
constexpr uint8_t PIXEL_NORMAL = 0xB0;   // wie Pc1715Zre
constexpr uint8_t PIXEL_HELL   = 0xFF;
}

Pc1715wBild::Pc1715wBild(Pc1715wSpeicher& speicher) : spk_(speicher)
{
    fb_.assign(static_cast<size_t>(FB_W) * FB_H, 0);
    crt_.dmaRead = [this]() -> uint8_t {
        const uint8_t b = spk_.bildRam(zaehler_);
        zaehler_ = (zaehler_ + 1) & 0x7FF;
        return b;
    };
    crt_.vrtc = [this]() { zaehler_ = 0; };
    crt_.reset();
}

void Pc1715wBild::reset()
{
    crt_.reset();
    zg2_ = false;
    zaehler_ = 0;
    std::fill(fb_.begin(), fb_.end(), 0);
}

uint8_t Pc1715wBild::ioRead(uint8_t port)
{
    switch (port & 3) {
    case 0:  return crt_.read(false);   // (von keiner Software benutzt)
    case 1:  return crt_.read(true);    // Status
    default: return 0xFF;               // 1AH/1BH nur schreibbar
    }
}

void Pc1715wBild::ioWrite(uint8_t port, uint8_t d)
{
    switch (port & 3) {
    case 0: crt_.write(false, d); break;
    case 1: crt_.write(true, d);  break;
    case 2: zg2_ = (d & 0x10) != 0; break;   // Flipflop A07 (D = DB4)
    case 3: zaehler_ = 0; break;             // /ZRES, Wert gleichgültig
    }
}

void Pc1715wBild::frame()
{
    crt_.frame();   // ruft vrtc (Zähler 0) und je Zeichenzeile dmaRead
    rastern();
    fb_dirty_ = true;
}

uint8_t Pc1715wBild::screenChar(int col, int row) const
{
    const I8275::Cell& c = crt_.cells(row, col);
    return c.empty ? 0x20 : c.code;
}

void Pc1715wBild::rastern()
{
    std::fill(fb_.begin(), fb_.end(), 0);
    const int rows = std::min(crt_.rows(), ROWS);
    const int cols = std::min(crt_.cols(), COLS);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) zeichneZelle(r, c, crt_.cells(r, c));
}

void Pc1715wBild::zeichneZelle(int row, int col, const I8275::Cell& z)
{
    if (z.empty && !z.cursor) return;
    const bool unsichtbar = z.empty || (z.blink && !crt_.charBlinkOn());
    const int  zg = (zg2_ != z.gpa0) ? 1 : 0;                  // 1AH Bit 4 XOR GPA0
    const uint8_t hell = z.hlgt ? PIXEL_HELL : PIXEL_NORMAL;
    const int ul = crt_.underlineLine();

    for (int l = 0; l < LINIEN; ++l) {
        const uint8_t bits = z.empty ? 0 : spk_.zgRam(zg, l * 0x80 + (z.code & 0x7F));
        uint8_t* zeile = &fb_[static_cast<size_t>(row * LINIEN + l) * FB_W + col * 8];
        for (int x = 0; x < 8; ++x) {
            bool an = ((bits >> (7 - x)) & 1) != 0;
            if (z.rvv) an = !an;
            if (z.lten && l == ul) an = true;
            if (unsichtbar) an = false;
            if (z.cursor) {
                if (crt_.cursorUnderline()) { if (l == ul) an = true; }
                else an = !an;
            }
            zeile[x] = an ? hell : 0;
        }
    }
}
