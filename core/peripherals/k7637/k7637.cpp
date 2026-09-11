#include "core/peripherals/k7637/k7637.h"
#include <cstddef>
#include <cstring>
#include <type_traits>
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

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot serialisation
// ─────────────────────────────────────────────────────────────────────────────
namespace {
template <class T> void putPod(std::vector<uint8_t>& o, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "POD only");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), p, p + sizeof(T));
}
template <class T> bool getPod(const uint8_t*& p, const uint8_t* end, T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "POD only");
    if (static_cast<size_t>(end - p) < sizeof(T)) return false;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
}
}  // namespace

void K7637::serialize(std::vector<uint8_t>& out) const {
    putPod(out, pressed_key_);
    putPod(out, pressed_scancode_);
    putPod(out, shift_);
    putPod(out, ctrl_);
    putPod(out, repeat_delay_ms_);
    putPod(out, repeat_period_ms_);
    putPod(out, led_mask_);
    putPod(out, edge_acc_);
    putPod(out, beep_until_cycle_);
    putPod(out, cur_cycle_);
    putPod(out, next_tx_cycle_);
    uint32_t n = static_cast<uint32_t>(tx_queue_.size());
    putPod(out, n);
    for (const auto& e : tx_queue_) { putPod(out, e.first); putPod(out, e.second); }
}

bool K7637::deserialize(const uint8_t*& p, const uint8_t* end) {
    bool ok = true;
    ok = ok && getPod(p, end, pressed_key_);
    ok = ok && getPod(p, end, pressed_scancode_);
    ok = ok && getPod(p, end, shift_);
    ok = ok && getPod(p, end, ctrl_);
    ok = ok && getPod(p, end, repeat_delay_ms_);
    ok = ok && getPod(p, end, repeat_period_ms_);
    ok = ok && getPod(p, end, led_mask_);
    ok = ok && getPod(p, end, edge_acc_);
    ok = ok && getPod(p, end, beep_until_cycle_);
    ok = ok && getPod(p, end, cur_cycle_);
    ok = ok && getPod(p, end, next_tx_cycle_);
    uint32_t n = 0;
    ok = ok && getPod(p, end, n);
    tx_queue_.clear();
    for (uint32_t i = 0; i < n && ok; ++i) {
        uint64_t rel = 0; uint8_t byte = 0;
        ok = ok && getPod(p, end, rel);
        ok = ok && getPod(p, end, byte);
        if (ok) tx_queue_.emplace_back(rel, byte);
    }
    return ok;
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

int K7637::fallingEdges(uint8_t byte) {
    // Leitung: Ruhe 1 → Startbit 0 (eine fallende Flanke) → d0…d7 (LSB zuerst)
    // → Stoppbit 1 (steigend, zählt nicht).
    int edges = 1;
    int prev  = 0;                       // Startbit
    for (int i = 0; i < 8; ++i) {
        const int bit = (byte >> i) & 1;
        if (prev == 1 && bit == 0) ++edges;
        prev = bit;
    }
    return edges;
}

void K7637::applyCommandByte(uint8_t byte) {
    // Der Zähler D7:1 ist auf 15 voreingestellt und zählt je Flanke herunter;
    // sein (negierter) Stand ist das Kommando.  Über zwei Bytes hinweg zählt er
    // weiter — daher die Zweibyte-Kommandos mit dem Vorkommando 55H.
    edge_acc_ = static_cast<uint8_t>(edge_acc_ + fallingEdges(byte));
    const uint8_t counter = static_cast<uint8_t>((15 - edge_acc_) & 0x0F);

    if (counter == 10) return;           // Vorkommando 55H: zweites Byte abwarten
    edge_acc_ = 0;

    switch (counter) {
        case 14:                          // 00H      Software-RESET
        case 9:                           // 55H 00H  Grundzustand
            // Grundzustand nach Handbuch §2.1: alle Funktionsanzeigen aus
            // (die Betriebsanzeige E54 hängt an der Spannung, nicht am Kommando).
            led_mask_         = 0;
            beep_until_cycle_ = 0;
            break;

        case 13:                          // 20H      Fehleranzeige blinken an/aus
            led_mask_ ^= LED_ERROR;
            // „Beim Einschalten Erzeugung eines akustischen Signals von ca. 1 s".
            if (led_mask_ & LED_ERROR) beep_until_cycle_ = cur_cycle_ + BEEP_CYCLES;
            break;

        case 12:                          // 44H      akustisches Signal ≈1 s
            beep_until_cycle_ = cur_cycle_ + BEEP_CYCLES;
            break;

        // Die fünf LED-Kommandos schalten UM ("vorheriger Zustand wird negiert").
        case 11: led_mask_ ^= LED_G00; break;   // 52H
        case 8:  led_mask_ ^= LED_G01; break;   // 55H 20H
        case 7:  led_mask_ ^= LED_G02; break;   // 55H 44H
        case 6:  led_mask_ ^= LED_G03; break;   // 55H 52H
        case 5:  led_mask_ ^= LED_G04; break;   // 55H 55H

        default: break;                   // kein gültiges Kommando
    }
}

bool K7637::processTxCommands() {
    if (!sio_) return false;
    Z80SIO::Channel& ch = pickChannel(*sio_, ch_idx_);

    bool touched = false;
    while (ch.txAvailable()) {
        touched = true;
        uint8_t byte = ch.txGet();

        // Die echte K7637 quittiert ein als gültig erkanntes Kommando mit dem
        // Zeichen TYP (hohes Nibble 0x8x).  Wir quittieren JEDES Byte: das BIOS
        // wartet nach jedem gesendeten Byte auf diese Antwort — bei der
        // Tastaturerkennung (`coityp`: Reset senden, Typcode erwarten) wie im
        // LED-Handschlag (`lmpout`).  Ein Typcode-Byte, das als Taste gelesen
        // wird, ist harmlos: es steht in keiner Codetabelle und liefert 0.
        sendByte(TYPE_CODE);

        applyCommandByte(byte);
    }
    return touched;
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
    // Rohcode: die Bildschirmtastatur bildet die echte K7637 nach und kennt
    // für jede Taste ihren physischen Code (auch für die, die eine PC-Tastatur
    // gar nicht hat — CE, PA1..PA3, SEL0..3, EREOF, …).  Unverändert senden.
    if ((qt_keycode & ~0xFF) == QK_RAW_BASE) {
        return static_cast<uint8_t>(qt_keycode & 0xFF);
    }

    switch (qt_keycode) {
        case QK_RETURN:    return 0xFF;   // ET1 (main Return)   → cp37: 0xFF→0x0D (CR)
        case QK_ENTER:     return 0xC0;   // numeric ENTER       → cp37: 0xC0→pf0c
        // Rückschritt und ESC liegen auf den Tasten, die im Gast auch das tun:
        // DEL CH löscht ein Zeichen rückwärts, die ESC-Taste schickt 0x1B
        // (ASCII, wird durchgereicht — in cp37 steht sie nicht).  Am laufenden
        // CP/A nachgemessen; 0xB3 (DEL L) wäre nach cp37 ebenfalls ESC, aber
        // dann leuchtet auf der Bildschirmtastatur die falsche Taste auf, und
        // andere Betriebssysteme kodieren 0xB3 anders.
        case QK_BACKSPACE: return 0xBB;   // DEL CH              → cp37: 0xBB→spcdel
        case QK_TAB:       return 0x9F;   // |<-| key            → cp37: 0x9F→0x09 (TAB)
        case QK_ESCAPE:    return 0x1B;   // ESC-Taste (ASCII)
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

bool K7637::service(uint64_t now_cycles) {
    cur_cycle_ = now_cycles;
    bool touched = false;
    // Deliver every byte whose serial transmission has completed.
    while (!tx_queue_.empty() && tx_queue_.front().first <= now_cycles) {
        uint8_t b = tx_queue_.front().second;
        tx_queue_.pop_front();
        if (sio_) { pickChannel(*sio_, ch_idx_).rxByte(b); touched = true; }
    }
    // Then drain host→keyboard command bytes (their type-code acks are queued
    // by processTxCommands() → sendByte() and likewise delivered with delay).
    touched |= processTxCommands();
    return touched;
}
