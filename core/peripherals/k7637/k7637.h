#pragma once
#include "core/primitives/z80_sio.h"
#include <cstdint>

// K7637 – Serial keyboard peripheral for the A5120 / K1520 system.
//
// The real K7637 has its own Z80 CPU and communicates with the K8025 SIO
// over a 9600-baud IFSS (current-loop) serial link.  In the emulator we
// bypass the serial layer and inject/consume bytes directly via the SIO
// Channel API.
class K7637 {
public:
    K7637() = default;

    // Connect to a Z80SIO instance.
    // channel_idx: 0 = Channel A (keyboard RX), 1 = Channel B
    void connect(Z80SIO& sio, int channel_idx);

    // Called by the GUI or a test: translate a keycode to an A5120 scancode
    // and inject it into the SIO receive FIFO.
    // qt_keycode: for printable keys pass the ASCII value (0x20..0x7E);
    //             for special keys use the Qt::Key_xxx integer constants.
    void keyPress(int qt_keycode, bool shift, bool ctrl);
    void keyRelease(int qt_keycode);

    // Advance the key-repeat state machine.
    // Call periodically (e.g. every ~10 ms of simulated time).
    void tick(int ms_elapsed);

    // Drain command bytes the K8025 sent to the keyboard (LED control,
    // beep, …).  Call whenever sio.channelX().txAvailable() is true.
    void processTxCommands();

    // ── LED / lock state ──────────────────────────────────────────────────
    bool capsLock()   const { return caps_lock_;   }
    bool scrollLock() const { return scroll_lock_; }
    bool numLock()    const { return num_lock_;    }

private:
    // Translate a keycode + modifiers to the A5120 scancode byte.
    uint8_t translateKey(int qt_keycode, bool shift, bool ctrl) const;

    // Inject one byte into the connected SIO channel RX FIFO.
    void sendByte(uint8_t byte);

    // ── SIO connection ────────────────────────────────────────────────────
    Z80SIO* sio_    = nullptr;
    int     ch_idx_ = 0;         // 0 = Channel A, 1 = Channel B

    // ── Key-repeat state ──────────────────────────────────────────────────
    int     pressed_key_      = 0;   // currently held Qt keycode (0 = none)
    uint8_t pressed_scancode_ = 0;
    bool    shift_ = false;
    bool    ctrl_  = false;
    int     repeat_delay_ms_  = 0;   // countdown to first auto-repeat
    int     repeat_period_ms_ = 0;   // countdown between subsequent repeats

    static constexpr int REPEAT_DELAY_MS  = 500;
    static constexpr int REPEAT_PERIOD_MS = 100;

    // ── LED / command state ───────────────────────────────────────────────
    bool caps_lock_   = false;
    bool scroll_lock_ = false;
    bool num_lock_    = false;

    bool    expect_second_byte_ = false;
    uint8_t first_cmd_byte_     = 0;

    // ── Qt keycode constants (no Qt headers needed) ───────────────────────
    static constexpr int QK_ESCAPE    = 0x01000000;
    static constexpr int QK_TAB       = 0x01000001;
    static constexpr int QK_BACKSPACE = 0x01000003;
    static constexpr int QK_RETURN    = 0x01000004;
    static constexpr int QK_ENTER     = 0x01000005;
    static constexpr int QK_DELETE    = 0x01000007;
    static constexpr int QK_LEFT      = 0x01000012;
    static constexpr int QK_UP        = 0x01000013;
    static constexpr int QK_RIGHT     = 0x01000014;
    static constexpr int QK_DOWN      = 0x01000015;
    static constexpr int QK_F1        = 0x01000030;
    static constexpr int QK_F8        = 0x01000037;
};
