#include "z80_pio.h"

Z80PIO::Z80PIO(const std::string& name) : name_(name) {}

// ─── Control word decoder ─────────────────────────────────────────────────────
// Real Z80-PIO control protocol:
//   bit0=0                  → interrupt vector byte
//   bits[3:0]=0x0F (1111)   → mode control word, mode = bits[7:6]
//   bits[3:0]=0x07 (0111)   → interrupt control word (ICW)
//                             bit7=IE, bit6=AND, bit5=ActiveHigh, bit4=MaskFollows
//   State EXPECT_DIRECTION  → I/O direction register (after mode-3 word)
//   State EXPECT_MASK       → interrupt mask register (after ICW with bit4=1)

void Z80PIO::writeCtrl(Port& p, uint8_t data) {
    if (p.ctrl_state == CtrlState::EXPECT_DIRECTION) {
        p.direction  = data;
        p.ctrl_state = CtrlState::IDLE;
        return;
    }
    if (p.ctrl_state == CtrlState::EXPECT_MASK) {
        p.int_mask   = data;
        p.ctrl_state = CtrlState::IDLE;
        return;
    }

    if ((data & 0x01) == 0) {
        p.int_vector = data;
        return;
    }
    if ((data & 0x0F) == 0x0F) {
        p.mode = (data >> 6) & 0x03;
        if (p.mode == 3)
            p.ctrl_state = CtrlState::EXPECT_DIRECTION;
        return;
    }
    if ((data & 0x0F) == 0x07) {
        p.ie               = (data >> 7) & 1;
        p.int_and          = (data >> 6) & 1;
        p.int_active_high  = (data >> 5) & 1;
        if ((data >> 4) & 1)
            p.ctrl_state = CtrlState::EXPECT_MASK;
        return;
    }
}

// ─── Data port: CPU read ──────────────────────────────────────────────────────

uint8_t Z80PIO::readDataCPU(const Port& p) const {
    switch (p.mode) {
        case 0: return p.output_latch;
        case 1: return p.input_latch;
        case 2: return p.input_latch;
        case 3: return (p.input_latch  &  p.direction)
                     | (p.output_latch & ~p.direction);
        default: return 0xFF;
    }
}

// ─── External pin read: what the PIO drives onto pins ─────────────────────────

uint8_t Z80PIO::readPins(const Port& p) const {
    switch (p.mode) {
        case 0: return p.output_latch;
        case 1: return 0xFF;                           // pins are inputs, PIO drives nothing
        case 2: return p.output_latch;
        case 3: return p.output_latch & ~p.direction;  // only output-configured bits
        default: return 0xFF;
    }
}

// ─── Data port: CPU write ─────────────────────────────────────────────────────

void Z80PIO::writeDataCPU(Port& p, uint8_t data, PortCallback& cb) {
    if (p.mode == 1) return;   // input mode: CPU writes silently ignored
    p.output_latch = data;
    if (cb) cb(data);
}

// ─── Interrupt condition check ────────────────────────────────────────────────

void Z80PIO::checkInterrupt(Port& p, uint8_t new_input) {
    if (!p.ie) return;
    if (p.mode == 1 || p.mode == 2) {
        p.pending = true;
        return;
    }
    if (p.mode == 3) {
        uint8_t masked = new_input & p.int_mask;
        bool triggered;
        if (p.int_and)
            triggered = p.int_active_high ? (masked == p.int_mask) : (masked == 0);
        else
            triggered = p.int_active_high ? (masked != 0) : (masked != p.int_mask);
        if (triggered) p.pending = true;
    }
}

// ─── BusDevice ───────────────────────────────────────────────────────────────

uint8_t Z80PIO::ioRead(uint8_t port) {
    switch (port & 0x03) {
        case 0: return readDataCPU(porta_);
        case 1: return 0xFF;   // control port read: status not implemented
        case 2: return readDataCPU(portb_);
        case 3: return 0xFF;
        default: return 0xFF;
    }
}

void Z80PIO::ioWrite(uint8_t port, uint8_t data) {
    switch (port & 0x03) {
        case 0: writeDataCPU(porta_, data, cb_a_); break;
        case 1: writeCtrl(porta_, data);            break;
        case 2: writeDataCPU(portb_, data, cb_b_); break;
        case 3: writeCtrl(portb_, data);            break;
    }
}

// ─── InterruptSlave (daisy chain) ─────────────────────────────────────────────
// Port A has higher priority than Port B.
// IEI propagates to Port B only when Port A has no pending interrupt.

void Z80PIO::setIEI(bool v) {
    porta_.iei = v;
    portb_.iei = v && !porta_.pending;
}

bool Z80PIO::getIEO() const {
    if (porta_.iei && porta_.pending) return false;
    if (portb_.iei && portb_.pending) return false;
    return porta_.iei;
}

bool Z80PIO::hasInterrupt() const {
    return (porta_.iei && porta_.pending) || (portb_.iei && portb_.pending);
}

uint8_t Z80PIO::getVector() const {
    if (porta_.iei && porta_.pending) return porta_.int_vector;
    if (portb_.iei && portb_.pending) return portb_.int_vector;
    return 0xFF;
}

// ─── External port pins ──────────────────────────────────────────────────────

void Z80PIO::portAWrite(uint8_t data) {
    porta_.input_latch = data;
    checkInterrupt(porta_, data);
}

uint8_t Z80PIO::portARead() const { return readPins(porta_); }

void Z80PIO::setASTB(bool) {}   // simplified: strobe handled implicitly by portAWrite

void Z80PIO::portBWrite(uint8_t data) {
    portb_.input_latch = data;
    checkInterrupt(portb_, data);
}

uint8_t Z80PIO::portBRead() const { return readPins(portb_); }

void Z80PIO::setBSTB(bool) {}

// ─── Status accessors ────────────────────────────────────────────────────────

bool    Z80PIO::isInterruptEnabled() const { return porta_.ie || portb_.ie; }
uint8_t Z80PIO::getInterruptVector() const { return porta_.int_vector; }

// ─── Callbacks ───────────────────────────────────────────────────────────────

void Z80PIO::setPortAOutputCallback(PortCallback cb) { cb_a_ = std::move(cb); }
void Z80PIO::setPortBOutputCallback(PortCallback cb) { cb_b_ = std::move(cb); }
