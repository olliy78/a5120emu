#include "core/peripherals/k7637/k7637.h"
#include <cstddef>
#include <algorithm>

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

        // The real K7637 acknowledges every command byte it receives by
        // returning its type/status byte (high nibble 0x8x).  The BIOS relies
        // on this for keyboard detection (reset 0x00 → type code) and for the
        // LED-control handshake (`lmpout` waits for it after each command).
        // A stray type-code byte read as a keystroke is harmless: it is not in
        // the K7637 scan-code table and decodes to 0 (ignored).
        sendByte(TYPE_CODE);

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
    // The real K7637 sends the *physical* key code from its ROM code table
    // (CTAB1/CTAB2); the A5120 BIOS keyboard driver then recodes the high codes
    // (>=0x80, plus 0xFF/0xFE) to its virtual codes via the `cp37` table in
    // bioskbdc.mac.  Printable ASCII (0x20..0x7E) is NOT in that table and is
    // forwarded unchanged.  So special keys must emit their physical K7637 code
    // (the left column of cp37), not a pre-translated ASCII value — otherwise
    // physically distinct keys collapse onto one code.
    //
    // In particular ET1 (the main Return key) and the numeric ENTER key are two
    // different keys on the K7637: ET1 sends 0xFF (recoded to CR) while ENTER
    // sends 0xC0 (recoded to the programmable pf0c code).
    switch (qt_keycode) {
        case QK_RETURN:    return 0xFF;   // ET1 (main Return)   → cp37: 0xFF→0x0D (CR)
        case QK_ENTER:     return 0xC0;   // numeric ENTER       → cp37: 0xC0→pf0c
        case QK_BACKSPACE: return 0x08;   // BS — ASCII control, passes through
        case QK_TAB:       return 0x9F;   // |<-| key            → cp37: 0x9F→0x09 (TAB)
        case QK_ESCAPE:    return 0xB3;   // DELL key (Ersatz ESC) → cp37: 0xB3→0x1B (ESC)
        case QK_DELETE:    return 0xBB;   // DELCH key           → cp37: 0xBB→spcdel
        case QK_UP:        return 0x94;   // cursor up           → cp37: 0x94→kcurup
        case QK_DOWN:      return 0x95;   // cursor down         → cp37: 0x95→kcurdw
        case QK_LEFT:      return 0x96;   // cursor left         → cp37: 0x96→kcurlf
        case QK_RIGHT:     return 0x97;   // cursor right        → cp37: 0x97→kcurri
        default: break;
    }

    // Function keys F1..F8 → physical codes 0xC1..0xC8 (cp37: 0xC1→pf1c …).
    if (qt_keycode >= QK_F1 && qt_keycode <= QK_F8) {
        return static_cast<uint8_t>(0xC1 + (qt_keycode - QK_F1));
    }

    // ── Printable ASCII (0x20 .. 0x7E) ───────────────────────────────────
    if (qt_keycode >= 0x20 && qt_keycode <= 0x7E) {
        if (ctrl) {
            // Ctrl+key → ASCII control code (the BIOS forwards codes <0x20).
            // NOTE: the real keyboard implements Ctrl as the ET2 key (physical
            // 0xFE on release, setting a one-shot flag for the next key); this
            // shortcut produces the same console byte without the two-key dance.
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
    // Model the 9600-baud serial line: the byte is not available to the host
    // until its transmission completes, and bytes serialise one after another.
    // service() releases them into the SIO RX FIFO once their time has come.
    uint64_t start   = std::max(cur_cycle_, next_tx_cycle_);
    uint64_t release = start + SERIAL_BYTE_CYCLES;
    tx_queue_.push_back({release, byte});
    next_tx_cycle_ = release;
}

void K7637::service(uint64_t now_cycles) {
    cur_cycle_ = now_cycles;
    // Deliver every byte whose serial transmission has completed.
    while (!tx_queue_.empty() && tx_queue_.front().first <= now_cycles) {
        uint8_t b = tx_queue_.front().second;
        tx_queue_.pop_front();
        if (sio_) pickChannel(*sio_, ch_idx_).rxByte(b);
    }
    // Then drain host→keyboard command bytes (their type-code acks are queued
    // by processTxCommands() → sendByte() and likewise delivered with delay).
    processTxCommands();
}
