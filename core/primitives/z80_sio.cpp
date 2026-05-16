#include "z80_sio.h"

static constexpr size_t RX_FIFO_DEPTH = 3;

// ─── Channel ─────────────────────────────────────────────────────────────────

void Z80SIO::Channel::reset() {
    for (auto& r : wr) r = 0;
    rr0     = 0x04;
    rr1     = 0x01;
    reg_ptr = 0;
    rx_fifo.clear();
    tx_buf.reset();
    irq_rx = irq_tx = irq_ext = false;
}

void Z80SIO::Channel::updateRR0() {
    rr0 = 0x00;
    if (!rx_fifo.empty()) rr0 |= 0x01;  // Rx Character Available
    if (!tx_buf.has_value()) rr0 |= 0x04; // Tx Buffer Empty
    if (cts_) rr0 |= 0x20;
}

bool Z80SIO::Channel::rxIntEnabled() const {
    // WR1 bits[3:2]: 10 or 11 = interrupt on all received
    uint8_t rx_mode = (wr[1] >> 2) & 0x03;
    return rx_mode >= 2;
}

bool Z80SIO::Channel::txIntEnabled() const {
    return (wr[1] & 0x02) != 0;
}

void Z80SIO::Channel::rxByte(uint8_t byte) {
    if (rx_fifo.size() < RX_FIFO_DEPTH) {
        rx_fifo.push_back(byte);
        if (rx_fifo.size() == 1) {
            // Parity error detection not emulated — clear parity/framing/overrun
        }
    } else {
        rr1 |= 0x08; // Overrun
    }
    updateRR0();
    if (rxIntEnabled())
        irq_rx = true;
    // First-received mode: interrupt only on first byte in empty FIFO
    uint8_t rx_mode = (wr[1] >> 2) & 0x03;
    if (rx_mode == 1 && rx_fifo.size() == 1)
        irq_rx = true;
}

bool Z80SIO::Channel::txAvailable() const {
    return tx_buf.has_value();
}

uint8_t Z80SIO::Channel::txGet() {
    uint8_t b = tx_buf.value_or(0xFF);
    tx_buf.reset();
    rr1 |= 0x01;  // All Sent
    updateRR0();
    if (txIntEnabled())
        irq_tx = true;
    return b;
}

bool Z80SIO::Channel::rxFull() const {
    return rx_fifo.size() >= RX_FIFO_DEPTH;
}

void Z80SIO::Channel::setClockSource(std::function<void()> clk_callback) {
    clk_cb_ = std::move(clk_callback);
}

void Z80SIO::Channel::setRTS(bool rts) {
    rts_ = rts;
    wr[5] = rts ? (wr[5] | 0x02) : (wr[5] & ~0x02);
}

bool Z80SIO::Channel::getCTS() const {
    return cts_;
}

// ─── Z80SIO ──────────────────────────────────────────────────────────────────

Z80SIO::Z80SIO(const std::string& name) : name_(name) {
    ch_a_.reset();
    ch_b_.reset();
}

bool Z80SIO::channelHasInterrupt(const Channel& ch) const {
    return ch.irq_rx || ch.irq_tx || ch.irq_ext;
}

bool Z80SIO::hasInterrupt() const {
    if (!iei_) return false;
    return channelHasInterrupt(ch_a_) || channelHasInterrupt(ch_b_);
}

bool Z80SIO::getIEO() const {
    // IEO is blocked (low) when this device has a pending acknowledged interrupt
    if (iei_ && (channelHasInterrupt(ch_a_) || channelHasInterrupt(ch_b_)))
        return false;
    return true;
}

void Z80SIO::setIEI(bool iei) {
    iei_ = iei;
}

uint8_t Z80SIO::getVector() const {
    if (!iei_) return 0xFF;

    // Base vector from WR2 of channel B
    uint8_t vec = ch_b_.wr[2];

    // Determine which interrupt is active (A > B, Rx > Tx > Ext)
    // Bits [3:1] of vector encode interrupt source per Z80 SIO spec
    if (ch_a_.irq_rx)  return (vec & 0xF1) | 0x0C;  // 110 = Ch A Rx
    if (ch_a_.irq_tx)  return (vec & 0xF1) | 0x08;  // 100 = Ch A Tx
    if (ch_a_.irq_ext) return (vec & 0xF1) | 0x00;  // 000 = Ch A Ext/Status
    if (ch_b_.irq_rx)  return (vec & 0xF1) | 0x04;  // 010 = Ch B Rx
    if (ch_b_.irq_tx)  return (vec & 0xF1) | 0x0A;  // 101 = Ch B Tx (spec)
    if (ch_b_.irq_ext) return (vec & 0xF1) | 0x02;  // 001 = Ch B Ext/Status

    return vec;
}

void Z80SIO::writeControl(Channel& ch, uint8_t data, bool is_b) {
    if (ch.reg_ptr == 0) {
        uint8_t reg_sel = data & 0x07;
        uint8_t cmd     = (data >> 3) & 0x07;

        switch (cmd) {
            case 2: // Reset Status
                ch.rr1 &= ~(0x1C);
                ch.irq_ext = false;
                break;
            case 4: // Reset Int Pending
                ch.irq_rx = ch.irq_tx = ch.irq_ext = false;
                break;
            case 5: // Reset Tx Interrupt
                ch.irq_tx = false;
                break;
            case 6: // Error Reset
                ch.rr1 &= ~(0x1C);
                break;
            default:
                break;
        }

        if (reg_sel != 0)
            ch.reg_ptr = reg_sel;
        // else reg_ptr stays 0 (or point high handled separately)
    } else {
        ch.wr[ch.reg_ptr] = data;
        if (ch.reg_ptr == 5) {
            ch.rts_ = (data & 0x02) != 0;
        }
        ch.reg_ptr = 0;
    }
}

uint8_t Z80SIO::readControl(Channel& ch, bool is_b) const {
    uint8_t reg = ch.reg_ptr;
    ch.reg_ptr  = 0;  // reading resets pointer (mutable)

    switch (reg) {
        case 0: return ch.rr0;
        case 1: return ch.rr1;
        case 2:
            // Channel B returns modified vector; channel A returns raw WR2 of B
            if (is_b) return getVector();
            return ch_b_.wr[2];
        default: return 0xFF;
    }
}

uint8_t Z80SIO::ioRead(uint8_t port) {
    switch (port & 0x03) {
        case 0: { // Ch A data
            if (!ch_a_.rx_fifo.empty()) {
                uint8_t b = ch_a_.rx_fifo.front();
                ch_a_.rx_fifo.pop_front();
                ch_a_.irq_rx = false;
                ch_a_.updateRR0();
                return b;
            }
            return 0xFF;
        }
        case 1: // Ch A control
            return readControl(ch_a_, false);
        case 2: { // Ch B data
            if (!ch_b_.rx_fifo.empty()) {
                uint8_t b = ch_b_.rx_fifo.front();
                ch_b_.rx_fifo.pop_front();
                ch_b_.irq_rx = false;
                ch_b_.updateRR0();
                return b;
            }
            return 0xFF;
        }
        case 3: // Ch B control
            return readControl(ch_b_, true);
    }
    return 0xFF;
}

void Z80SIO::ioWrite(uint8_t port, uint8_t data) {
    switch (port & 0x03) {
        case 0: // Ch A data (TX)
            ch_a_.tx_buf = data;
            ch_a_.rr1 &= ~0x01;  // Clear All Sent
            ch_a_.updateRR0();
            ch_a_.irq_tx = false; // TX buffer now full, clear old TX int
            break;
        case 1: // Ch A control
            writeControl(ch_a_, data, false);
            break;
        case 2: // Ch B data (TX)
            ch_b_.tx_buf = data;
            ch_b_.rr1 &= ~0x01;
            ch_b_.updateRR0();
            ch_b_.irq_tx = false;
            break;
        case 3: // Ch B control
            writeControl(ch_b_, data, true);
            break;
    }
}
