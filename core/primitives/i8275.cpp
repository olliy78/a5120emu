/**
 * @file i8275.cpp
 * @brief Intel 8275 CRT-Controller — siehe i8275.h.
 */

#include "core/primitives/i8275.h"
#include "core/logger.h"

namespace {
const I8275::Cell kLeer{};
}

void I8275::reset()
{
    configured_ = false;
    cmd_ = -1; pcount_ = 0;
    ie_ = ve_ = false;
    lp_ = ic_ = du_ = fo_ = false;
    cur_x_ = cur_y_ = 0;
    blink_cnt_ = 0;
    cells_.clear();
    cols_ = rows_ = 0;
    setIr(false);
}

void I8275::setIr(bool v)
{
    if (ir_ == v) return;
    ir_ = v;
    if (irqChanged) irqChanged(v);
}

void I8275::applyResetParams()
{
    spaced_  = (rp_[0] & 0x80) != 0;
    cols_    = (rp_[0] & 0x7F) + 1;
    vrtc_rows_ = (rp_[1] >> 6) + 1;
    rows_    = (rp_[1] & 0x3F) + 1;
    underline_ = rp_[2] >> 4;
    lines_   = (rp_[2] & 0x0F) + 1;
    offset_lc_ = (rp_[3] & 0x80) != 0;
    non_transparent_ = (rp_[3] & 0x40) != 0;
    cursor_fmt_ = (rp_[3] >> 4) & 3;
    hrtc_    = 2 * ((rp_[3] & 0x0F) + 1);
    cells_.assign(static_cast<size_t>(rows_) * cols_, Cell{});
    configured_ = true;
    LOG_DEBUG("I8275", "Reset: %dx%d, %d Linien, Unterstrich %d, %s, Cursor %d", cols_, rows_,
              lines_, underline_, non_transparent_ ? "nicht transparent" : "transparent", cursor_fmt_);
}

void I8275::write(bool a0, uint8_t d)
{
    if (a0) { command(d); return; }
    switch (cmd_) {
    case CMD_RESET:
        rp_[pcount_++] = d;
        if (pcount_ == 4) { applyResetParams(); cmd_ = -1; }
        break;
    case CMD_CURSOR:
        if (pcount_ == 0) { cur_x_tmp_ = d; pcount_ = 1; }
        else { cur_x_ = cur_x_tmp_; cur_y_ = d; cmd_ = -1; }
        break;
    default:
        ic_ = true;   // Parameter ohne Befehl
        LOG_DEBUG("I8275", "Parameter %02X ohne Befehl", d);
        break;
    }
}

void I8275::command(uint8_t c)
{
    cmd_ = -1; pcount_ = 0;
    switch (c >> 5) {
    case CMD_RESET:
        ve_ = false; ie_ = false; configured_ = false;
        cmd_ = CMD_RESET;
        break;
    case CMD_START:
        if (!configured_) { ic_ = true; break; }
        burst_len_   = 1 << (c & 3);
        burst_space_ = ((c >> 2) & 7) * 7;
        ve_ = true;
        break;
    case CMD_STOP:
        ve_ = false;
        break;
    case CMD_LPEN:
        cmd_ = CMD_LPEN;
        lp_ = false;
        break;
    case CMD_CURSOR:
        cmd_ = CMD_CURSOR;
        break;
    case CMD_IE:
        ie_ = true;
        break;
    case CMD_ID:
        ie_ = false;
        break;
    case CMD_PRESET:
        blink_cnt_ = 0;
        break;
    }
}

uint8_t I8275::peekStatus() const
{
    return static_cast<uint8_t>((ie_ ? 0x40 : 0) | (ir_ ? 0x20 : 0) | (lp_ ? 0x10 : 0) |
                                (ic_ ? 0x08 : 0) | (ve_ ? 0x04 : 0) | (du_ ? 0x02 : 0) |
                                (fo_ ? 0x01 : 0));
}

uint8_t I8275::read(bool a0)
{
    if (a0) {
        uint8_t s = peekStatus();
        lp_ = ic_ = du_ = fo_ = false;
        setIr(false);
        return s;
    }
    if (cmd_ == CMD_LPEN) {      // Lichtgriffel nicht nachgebildet: Spalte/Zeile 0
        if (++pcount_ == 2) cmd_ = -1;
        return 0;
    }
    return 0xFF;
}

const I8275::Cell& I8275::cells(int row, int col) const
{
    if (row < 0 || col < 0 || row >= rows_ || col >= cols_) return kLeer;
    return cells_[static_cast<size_t>(row) * cols_ + col];
}

uint8_t I8275::fetch()
{
    ++dma_bytes_;
    if (!dmaRead) { du_ = true; return 0; }
    return dmaRead();
}

void I8275::frame()
{
    dma_bytes_ = 0;
    if (!configured_) return;
    if (vrtc) vrtc();
    for (auto& c : cells_) c = Cell{};
    if (!ve_) return;

    // Feldattribut: gilt bis zum nächsten, am Bildanfang zurückgesetzt
    bool fRvv = false, fLten = false, fG0 = false, fG1 = false, fBlink = false, fHlgt = false;
    bool bildEnde = false, bildStop = false;

    for (int row = 0; row < rows_; ++row) {
        if (bildStop) break;                       // F3: kein DMA mehr bis zum nächsten Bild
        Cell* z = &cells_[static_cast<size_t>(row) * cols_];
        int pos = 0, geholt = 0;
        const int kappe = 2 * cols_;               // Schutz gegen Endlosfolgen von Attributen (transparent)
        bool zeilenEnde = false;

        while (pos < cols_ && geholt < kappe) {
            uint8_t b = fetch(); ++geholt;
            if (bildEnde || zeilenEnde) {          // Rest-DMA nach F0/F2: gelesen, verworfen
                ++pos;
                continue;
            }
            if (b >= 0xF0 && b <= 0xF3) {          // Sondercodes: bit0 = Stop-DMA, bit1 = Bildende
                zeilenEnde = true;
                if (b & 2) bildEnde = true;
                if (b & 1) {                       // F1/F3: DMA stoppt sofort
                    if (b & 2) bildStop = true;
                    break;
                }
                ++pos;                             // das Sondercode-Byte selbst belegt eine Position
                continue;
            }
            if ((b & 0xC0) == 0x80) {              // Feldattribut 10URGGBH
                fLten  = (b & 0x20) != 0;
                fRvv   = (b & 0x10) != 0;
                fG1    = (b & 0x08) != 0;
                fG0    = (b & 0x04) != 0;
                fBlink = (b & 0x02) != 0;
                fHlgt  = (b & 0x01) != 0;
                if (non_transparent_) ++pos;       // belegt eine Zelle, bleibt leer
                continue;
            }
            Cell& c = z[pos++];
            c.rvv = fRvv; c.lten = fLten; c.gpa0 = fG0; c.gpa1 = fG1; c.blink = fBlink; c.hlgt = fHlgt;
            if ((b & 0xC0) == 0xC0) {              // Zeichenattribut 11CCCCBH (durchgereicht)
                c.charAttr = true;
                c.cca   = (b >> 2) & 0x0F;
                c.blink = (b & 2) != 0;
                c.hlgt  = (b & 1) != 0;
            } else {
                c.code  = b & 0x7F;
                c.empty = false;
            }
        }
        // Rest der Zeile bleibt leer (Cell{} ist leer).
    }

    if (cur_y_ < rows_ && cur_x_ < cols_) {
        bool sichtbar = !cursorBlinks() || (((blink_cnt_ / 16) & 1) == 0);
        if (sichtbar) cells_[static_cast<size_t>(cur_y_) * cols_ + cur_x_].cursor = true;
    }
    ++blink_cnt_;
    if (ie_) setIr(true);
}

void I8275::serialize(std::vector<uint8_t>& out) const
{
    for (uint8_t b : rp_) out.push_back(b);
    out.push_back(configured_ ? 1 : 0);
    out.push_back(static_cast<uint8_t>(cur_x_));
    out.push_back(static_cast<uint8_t>(cur_y_));
    out.push_back(static_cast<uint8_t>(burst_space_));
    out.push_back(static_cast<uint8_t>(burst_len_));
    out.push_back(static_cast<uint8_t>((ie_ ? 1 : 0) | (ir_ ? 2 : 0) | (lp_ ? 4 : 0) | (ic_ ? 8 : 0) |
                                       (ve_ ? 16 : 0) | (du_ ? 32 : 0) | (fo_ ? 64 : 0)));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(blink_cnt_ >> (8 * i)));
}

bool I8275::deserialize(const uint8_t*& p, const uint8_t* end)
{
    if (end - p < 4 + 6 + 4) return false;
    for (auto& b : rp_) b = *p++;
    bool cfg = *p++ != 0;
    cur_x_ = *p++; cur_y_ = *p++;
    burst_space_ = *p++; burst_len_ = *p++;
    uint8_t f = *p++;
    ie_ = f & 1; ir_ = f & 2; lp_ = f & 4; ic_ = f & 8; ve_ = f & 16; du_ = f & 32; fo_ = f & 64;
    blink_cnt_ = 0;
    for (int i = 0; i < 4; ++i) blink_cnt_ |= static_cast<uint32_t>(*p++) << (8 * i);
    cmd_ = -1; pcount_ = 0;
    if (cfg) applyResetParams(); else { configured_ = false; cells_.clear(); cols_ = rows_ = 0; }
    return true;
}
