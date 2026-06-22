#pragma once
#include "core/primitives/z80_sio.h"
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

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

    // Per-instruction service: advance the serial-transmit timing (release any
    // keyboard→host bytes whose 9600-baud transmission has completed) and drain
    // host→keyboard command bytes.  @p now_cycles is the ZVE1 cycle counter.
    //
    // Real-HW fidelity: the K7637 talks to the K8025 SIO over a 9600-baud IFSS
    // link, so every byte (type-code ack AND key code) takes ~one byte-time to
    // arrive — it is NOT available the same instruction the command was sent.
    // Without this delay the type-code acks appear instantly and the OS timer
    // ISR's keyboard scan races the foreground LED-handshake for the same SIO
    // RX byte; the loser reads an empty FIFO (0xFF → recoded to CR) and pollutes
    // the keyboard buffer until it overflows and drops real keys.
    void service(uint64_t now_cycles);

    // ── Snapshot serialisation (savestate/loadstate) ──────────────────────
    // Append the restorable keyboard state (key-repeat, LED/command latches,
    // pending serial-TX queue + timing) to @p out. The SIO connection
    // (sio_/ch_idx_) is NOT serialised — it is re-established by connect(), so
    // a deserialise into an already-connected keyboard keeps working. Together
    // with Z80SIO::serialize this lets a loadstate resume with a working
    // keyboard (the byte stream + the SIO it feeds are both consistent).
    void serialize(std::vector<uint8_t>& out) const;
    // Restore state previously written by serialize(); advances @p p. Returns
    // false on truncation.
    bool deserialize(const uint8_t*& p, const uint8_t* end);

    // ── LED / lock state ──────────────────────────────────────────────────
    bool capsLock()   const { return caps_lock_;   }
    bool scrollLock() const { return scroll_lock_; }
    bool numLock()    const { return num_lock_;    }

private:
    // Translate a keycode + modifiers to the A5120 scancode byte.
    uint8_t translateKey(int qt_keycode, bool shift, bool ctrl) const;

    // Inject one byte into the connected SIO channel RX FIFO.
    void sendByte(uint8_t byte);

    // K7637 type/status byte.  The real keyboard returns a byte whose high
    // nibble (0x8x) identifies it as a K7637 in response to every command it
    // receives (reset 0x00, LED control, …).  The BIOS keyboard-detection
    // routine (`coityp` in bioskbdc.mac) sends a reset and waits for exactly
    // this answer (`and 0F0h` / `cp typc37` with typc37=80h); without it the
    // BIOS mis-detects the keyboard as a parallel K7606 and never reads the
    // SIO ports, so no key ever reaches the OS.  The LED routine (`lmpout`)
    // likewise polls for this acknowledge after every command byte.
    static constexpr uint8_t TYPE_CODE = 0x80;

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

    // ── Serial-transmit timing (keyboard → host) ──────────────────────────
    // One byte at 9600 baud (1 start + 8 data + 1 stop) = 10 bit-times.
    // At the A5120's 2.5 MHz ZVE1 clock that is ~2604 cycles per byte.
    static constexpr uint64_t SERIAL_BYTE_CYCLES = 2604;
    std::deque<std::pair<uint64_t, uint8_t>> tx_queue_;  // (release_cycle, byte)
    uint64_t cur_cycle_      = 0;   // last cycle stamp seen via service()
    uint64_t next_tx_cycle_  = 0;   // earliest cycle the line is free again

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
