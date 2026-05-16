#include "z80_ctc.h"

Z80CTC::Z80CTC(const std::string& name) : name_(name) {}

void Z80CTC::setZCTOCallback(ZCTOCallback cb) {
    zcto_cb_ = std::move(cb);
}

void Z80CTC::ioWrite(uint8_t port, uint8_t data) {
    int ch = port & 0x03;
    Channel& c = ch_[ch];

    if (c.awaiting_tc) {
        c.timeConst   = data;
        c.awaiting_tc = false;
        c.counter     = (data == 0) ? 256 : static_cast<int>(data);
        c.prescaleCnt = 0;

        bool counter_mode = (c.control & CTRL_COUNTER) != 0;
        bool clk_trg_mode = (c.control & CTRL_CLK_TRG) != 0;

        if (counter_mode) {
            c.running = true;
        } else if (!clk_trg_mode) {
            c.running = true;
        }
        return;
    }

    if ((data & 0x01) == 0) {
        if (ch == 0) {
            vec_base_ = data & 0xF8;
        }
        return;
    }

    c.control     = data;
    c.awaiting_tc = (data & CTRL_TC_FOLLOWS) != 0;

    if (data & CTRL_RESET) {
        c.running     = false;
        c.counter     = 0;
        c.prescaleCnt = 0;
        c.int_pending = false;
        c.zcto_active = false;
    }
}

uint8_t Z80CTC::ioRead(uint8_t port) {
    int ch = port & 0x03;
    return static_cast<uint8_t>(ch_[ch].counter & 0xFF);
}

void Z80CTC::clockTick() {
    for (int i = 0; i < 4; i++) {
        Channel& c = ch_[i];

        if (c.zcto_active) {
            c.zcto_active = false;
            if (zcto_cb_) zcto_cb_(i, true);
        }

        if (!c.running || (c.control & CTRL_COUNTER)) continue;

        int prescale = (c.control & CTRL_PRESCALE256) ? 256 : 16;
        if (++c.prescaleCnt >= prescale) {
            c.prescaleCnt = 0;
            if (--c.counter <= 0) {
                c.counter = (c.timeConst == 0) ? 256 : static_cast<int>(c.timeConst);
                fireZCTO(i);
            }
        }
    }
}

void Z80CTC::clkTrg(int channel, bool rising_edge) {
    Channel& c = ch_[channel];
    bool want_rising = (c.control & CTRL_EDGE_RISE) != 0;
    if (rising_edge != want_rising) return;

    if (c.control & CTRL_COUNTER) {
        if (!c.running) return;
        if (--c.counter <= 0) {
            c.counter = (c.timeConst == 0) ? 256 : static_cast<int>(c.timeConst);
            fireZCTO(channel);
        }
    } else if (c.control & CTRL_CLK_TRG) {
        if (!c.running && !c.awaiting_tc && c.timeConst != 0) {
            c.running     = true;
            c.prescaleCnt = 0;
        }
    }
}

void Z80CTC::fireZCTO(int ch) {
    Channel& c = ch_[ch];
    c.zcto_active = true;
    if (zcto_cb_) zcto_cb_(ch, false);

    if (c.control & CTRL_INT_EN) c.int_pending = true;

    if (c.control & CTRL_CLK_TRG) c.running = false;
}

bool Z80CTC::anyPending() const {
    for (const auto& c : ch_) {
        if (c.int_pending) return true;
    }
    return false;
}

void Z80CTC::setIEI(bool v) { iei_ = v; }

bool Z80CTC::hasInterrupt() const {
    return iei_ && anyPending();
}

bool Z80CTC::getIEO() const {
    return iei_ && !anyPending();
}

uint8_t Z80CTC::getVector() const {
    for (int i = 0; i < 4; i++) {
        if (ch_[i].int_pending) {
            return vec_base_ | static_cast<uint8_t>(i << 1);
        }
    }
    return 0xFF;
}

int Z80CTC::getCount(int channel) const {
    return ch_[channel].counter;
}

bool Z80CTC::isTimerMode(int channel) const {
    return (ch_[channel].control & CTRL_COUNTER) == 0;
}
