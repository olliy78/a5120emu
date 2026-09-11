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
    /// @return true, wenn mindestens ein Kommando-Byte verarbeitet wurde
    ///         (kann den SIO-Interruptzustand ändern).
    ///
    /// **Kommandos erkennt die K7637 an der Zahl der Impulsflanken, nicht am
    /// Bytewert** (Handbuch §2.2.3): die empfangenen Flanken zählen einen auf
    /// 15 voreingestellten Zähler herunter, und dessen Stand ist das Kommando.
    /// Die Bytes der Handbuchtabelle (00H, 20H, 44H, 52H, 55H) sind nur die
    /// übliche Schreibweise — jedes Byte mit derselben Flankenzahl wirkt gleich.
    /// Ein Zählerstand 10 (ein einzelnes 55H) ist das **Vorkommando**: dann
    /// zählt das nächste Byte weiter mit.
    ///
    /// CP/A schickt je GEÄNDERTEM Bit seines Lampenpuffers ein Kommando
    /// (BIOS-Routine `kbdmd2`, „Routine fuer K7637"): Selektor 0…3 → 52H bzw.
    /// 55H 20H/44H/52H, INS-Modus → 55H 55H, Fehlerlampe → 20H.  Das passt zu
    /// der umschaltenden Wirkung der Kommandos.
    bool processTxCommands();

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
    /// @return true, wenn ein Empfangsbyte an den SIO zugestellt oder ein
    ///         Kommando verarbeitet wurde (→ Interrupt-Chain neu bewerten).
    bool service(uint64_t now_cycles);

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

    /**
     * @brief Hardware-Reset: Tastenwiederholung, LED-/Kommandozustand und die
     *        noch nicht zugestellten seriellen Bytes verwerfen.
     *
     * Nach einem System-Reset wird auch der SIO zurückgesetzt; ein halb
     * gesendetes Byte in der Warteschlange wäre dann verwaist.  Die Verbindung
     * (sio_/ch_idx_) bleibt bestehen.
     */
    void reset() {
        pressed_key_ = 0; pressed_scancode_ = 0;
        shift_ = ctrl_ = false;
        repeat_delay_ms_ = repeat_period_ms_ = 0;
        led_mask_ = 0; edge_acc_ = 0; beep_until_cycle_ = 0;
        tx_queue_.clear();
        cur_cycle_ = 0; next_tx_cycle_ = 0;
    }

    // ── Anzeigen und akustisches Signal ───────────────────────────────────
    // Die Tastatur hat acht Leuchtdioden (K7637-Doku §2.1, §2.2.3, §2.3):
    // fünf frei belegbare Funktionsanzeigen G00…G04, die blinkende
    // Fehleranzeige G53, die Betriebsanzeige E54 (leuchtet, solange die
    // Tastatur Spannung hat) und die LOCK-Anzeige C99 (folgt dem
    // Umschaltfeststeller, also der Tastatur selbst — kein Kommando).
    // Nur die ersten sechs schaltet der Rechner; sie stehen hier.  Die Codes
    // sind die der Zuordnungstabelle im Handbuch.
    static constexpr uint8_t LED_G00   = 0x01;
    static constexpr uint8_t LED_G01   = 0x02;
    static constexpr uint8_t LED_G02   = 0x04;
    static constexpr uint8_t LED_G03   = 0x08;
    static constexpr uint8_t LED_G04   = 0x10;
    static constexpr uint8_t LED_ERROR = 0x20;   // G53 — blinkt, wenn gesetzt

    /// Eingeschaltete Anzeigen als Bitmaske (LED_G00 … LED_ERROR).
    uint8_t leds() const { return led_mask_; }
    /// Läuft gerade das akustische Signal (≈1 s nach Kommando 44H)?
    bool beeping() const { return beep_until_cycle_ > cur_cycle_; }

    /**
     * @brief Welchen physischen Tastencode erzeugt dieser Qt-Tastencode?
     *
     * Dieselbe Abbildung, die :meth:`keyPress` benutzt — nur ohne Maschine und
     * ohne Seiteneffekt.  Für Tests und für die Fehlersuche an der
     * Bedienoberfläche („welche Taste schickt die Tastatur wirklich?").
     */
    static uint8_t codeFor(int qt_keycode, bool shift = false, bool ctrl = false) {
        return translateKey(qt_keycode, shift, ctrl);
    }

    /**
     * @brief Fallende Flanken im seriellen Rahmen eines Bytes.
     *
     * Ruhepegel 1, Startbit 0, acht Datenbits (LSB zuerst), Stoppbit 1.  Die
     * Tastatur erkennt Kommandos **allein an dieser Zahl** (s. @ref
     * processTxCommands), nicht am Bytewert.
     */
    static int fallingEdges(uint8_t byte);

private:
    // Translate a keycode + modifiers to the A5120 scancode byte.  Zustandslos
    // (nur Konstanten), deshalb statisch — s. codeFor().
    static uint8_t translateKey(int qt_keycode, bool shift, bool ctrl);

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

    // ── Anzeigen / Kommandodekodierung ────────────────────────────────────
    uint8_t  led_mask_         = 0;   // LED_G00 … LED_ERROR
    uint8_t  edge_acc_         = 0;   // Flanken seit dem letzten Kommando
    uint64_t beep_until_cycle_ = 0;   // akustisches Signal läuft bis …

    /// Dauer des akustischen Signals: ≈1 s bei 2,5 MHz ZVE1-Takt.
    static constexpr uint64_t BEEP_CYCLES = 2500000;

    /// Ein empfangenes Kommandobyte auswerten (Flankenzählung).
    void applyCommandByte(uint8_t byte);

    // ── Serial-transmit timing (keyboard → host) ──────────────────────────
    // One byte at 9600 baud (1 start + 8 data + 1 stop) = 10 bit-times.
    // At the A5120's 2.5 MHz ZVE1 clock that is ~2604 cycles per byte.
    static constexpr uint64_t SERIAL_BYTE_CYCLES = 2604;
    std::deque<std::pair<uint64_t, uint8_t>> tx_queue_;  // (release_cycle, byte)
    uint64_t cur_cycle_      = 0;   // last cycle stamp seen via service()
    uint64_t next_tx_cycle_  = 0;   // earliest cycle the line is free again

public:
    // Rohcode-Fluchtweg für eine Bedienoberfläche, die die ECHTE Tastatur
    // nachbildet: `QK_RAW_BASE | <Byte>` sendet genau dieses Byte als
    // physischen K7637-Code (z.B. 0xB9 = CE, 0xCA = PF10, 0xA0 = SEL0).
    // Über die Qt-Abbildung unten sind nur die Tasten erreichbar, die eine
    // PC-Tastatur auch hat — die Bildschirmtastatur hat alle.
    // Der Bereich liegt über den Qt::Key_*-Werten (0x0100_0000), kollidiert
    // also weder mit ihnen noch mit druckbarem ASCII.
    static constexpr int QK_RAW_BASE  = 0x02000000;

private:
    // ── Qt keycode constants (no Qt headers needed) ───────────────────────
    static constexpr int QK_ESCAPE    = 0x01000000;
    static constexpr int QK_TAB       = 0x01000001;
    static constexpr int QK_BACKTAB   = 0x01000002;   // Umschalt+Tab
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
    static constexpr int QK_F12       = 0x0100003B;
};
