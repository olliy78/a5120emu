#include "core/peripherals/k7637/k7637.h"
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

// Return a reference to the connected SIO channel.
static Z80SIO::Channel& pickChannel(Z80SIO& sio, int idx) {
    return (idx == 0) ? sio.channelA() : sio.channelB();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void K7637::connect(Z80SIO& sio, int channel_idx) {
    sio_    = &sio;
    ch_idx_ = channel_idx;
}

void K7637::keyPress(int qt_keycode, bool shift, bool ctrl) {
    uint8_t code = translateKey(qt_keycode, shift, ctrl);

    // Record the held key for auto-repeat.
    pressed_key_      = qt_keycode;
    pressed_scancode_ = code;
    shift_ = shift;
    ctrl_  = ctrl;
    repeat_delay_ms_  = REPEAT_DELAY_MS;
    repeat_period_ms_ = 0;

    sendByte(code);
}

void K7637::keyRelease(int qt_keycode) {
    if (pressed_key_ == qt_keycode) {
        pressed_key_      = 0;
        pressed_scancode_ = 0;
        repeat_delay_ms_  = 0;
        repeat_period_ms_ = 0;
    }
}

void K7637::tick(int ms_elapsed) {
    if (pressed_key_ != 0) {
        if (repeat_delay_ms_ > 0) {
            // Still in the initial delay phase.
            repeat_delay_ms_ -= ms_elapsed;
            if (repeat_delay_ms_ <= 0) {
                // Delay expired: fire the first auto-repeat.
                sendByte(pressed_scancode_);
                // Switch to period phase; absorb any overshoot.
                repeat_period_ms_ = REPEAT_PERIOD_MS + repeat_delay_ms_;
                if (repeat_period_ms_ <= 0) {
                    // Overshoot ate a whole period too – fire again and reset.
                    sendByte(pressed_scancode_);
                    repeat_period_ms_ = REPEAT_PERIOD_MS;
                }
                repeat_delay_ms_ = 0; // signal: we are now in period phase
            }
        } else {
            // Period phase.
            repeat_period_ms_ -= ms_elapsed;
            while (repeat_period_ms_ <= 0) {
                sendByte(pressed_scancode_);
                repeat_period_ms_ += REPEAT_PERIOD_MS;
            }
        }
    }

    // Drain any command bytes the K8025 sent to us.
    processTxCommands();
}

void K7637::processTxCommands() {
    if (!sio_) return;
    Z80SIO::Channel& ch = pickChannel(*sio_, ch_idx_);

    while (ch.txAvailable()) {
        uint8_t byte = ch.txGet();

        if (expect_second_byte_) {
            expect_second_byte_ = false;
            // Handle second byte of a two-byte command.
            if (first_cmd_byte_ == 0x52) {
                // LED control byte: bits encode which LEDs to set.
                // Bit mapping from K7637 documentation:
                //   bit 0 = CAPS LOCK, bit 1 = SCROLL LOCK, bit 2 = NUM LOCK
                caps_lock_   = (byte & 0x01) != 0;
                scroll_lock_ = (byte & 0x02) != 0;
                num_lock_    = (byte & 0x04) != 0;
            }
            // Extended (0x55) command: just consume; we don't act on it.
        } else {
            switch (byte) {
                case 0x00:
                    // Software reset: clear LED state.
                    caps_lock_   = false;
                    scroll_lock_ = false;
                    num_lock_    = false;
                    expect_second_byte_ = false;
                    break;

                case 0x20:
                    // Error LED blink toggle – nothing to store in this impl.
                    break;

                case 0x44:
                    // Acoustic signal (beep) – acknowledged, no action.
                    break;

                case 0x52:
                    // LED control: next byte carries the LED bitmask.
                    first_cmd_byte_     = byte;
                    expect_second_byte_ = true;
                    break;

                case 0x55:
                    // Extended command: next byte is the sub-command.
                    first_cmd_byte_     = byte;
                    expect_second_byte_ = true;
                    break;

                default:
                    // Unknown command – ignore.
                    break;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

uint8_t K7637::translateKey(int qt_keycode, bool shift, bool ctrl) const {
    // ── Special / non-printable keys ─────────────────────────────────────
    switch (qt_keycode) {
        case QK_RETURN:    return 0x0D;
        case QK_ENTER:     return 0x0D;
        case QK_BACKSPACE: return 0x08;
        case QK_TAB:       return 0x09;
        case QK_ESCAPE:    return 0x1B;
        case QK_DELETE:    return 0x7F;
        case QK_UP:        return 0x1E;
        case QK_DOWN:      return 0x1F;
        case QK_LEFT:      return 0x1C;
        case QK_RIGHT:     return 0x1D;
        default: break;
    }

    // ── Function keys F1–F8 ───────────────────────────────────────────────
    if (qt_keycode >= QK_F1 && qt_keycode <= QK_F8) {
        return static_cast<uint8_t>(0x80 + (qt_keycode - QK_F1));
    }

    // ── Printable ASCII (0x20 .. 0x7E) ───────────────────────────────────
    if (qt_keycode >= 0x20 && qt_keycode <= 0x7E) {
        if (ctrl) {
            // Standard ASCII control code: strip bits 6+5.
            return static_cast<uint8_t>(qt_keycode & 0x1F);
        }
        // Shift is presumed already encoded in the keycode value supplied by
        // the caller (e.g. 'A' = 0x41, 'a' = 0x61).
        return static_cast<uint8_t>(qt_keycode);
    }

    // Unknown / unhandled key.
    return 0x00;
}

void K7637::sendByte(uint8_t byte) {
    if (!sio_) return;
    pickChannel(*sio_, ch_idx_).rxByte(byte);
}
