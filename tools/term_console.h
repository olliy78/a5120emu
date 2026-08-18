/**
 * @file term_console.h
 * @brief Der A5120 als Konsolenanwendung: Terminal-Rohmodus, Tastenabbildung,
 *        differenzielles Zeichnen des 80x24-Textbildschirms.
 *
 * Damit lässt sich `k1520dbg` als **interaktiver Emulator** benutzen: tippen wie
 * an der Maschine, während die Haltepunkte scharf bleiben. Genau das kann sonst
 * nichts — die Oberfläche hat keine Haltepunkte, der Debugger hatte keine lebende
 * Eingabe.
 *
 * Warum das überhaupt verlustfrei geht: der K7024 ist ein **Zeichenbildschirm**,
 * kein Pixelgerät (`core/cards/k7024/k7024.cpp`). Jede VRAM-Zelle ab 0xF800
 * trägt in Bit [6:0] den Zeichencode und in Bit 7 den Cursor; Codes unter 0x20
 * sind leer. Ein Terminal kann den Schirm also Zelle für Zelle wiedergeben — es
 * geht nichts verloren.
 *
 * Aufbau (alles header-only, wie der übrige Werkzeugkasten):
 *   - RawMode      RAII: Terminal in den Rohmodus und zuverlässig zurück
 *   - Keyboard     nichtblockierendes Lesen + Escape-Sequenzen -> K7637-Codes
 *   - ScreenDiff   zeichnet nur geänderte Zellen (sonst flimmert es bei 50 Hz)
 *
 * Getestet über ein Pseudoterminal: `tests/python/test_dbg_konsole.py`.
 *
 * @license MIT
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
// `core/util/os_compat.h` haelt sich bewusst von <windows.h> fern, weil es Makros in
// jeden Uebersetzungsvorgang schleppt.  Hier geht es nicht ohne (Konsolenmodus,
// VT-Ausgabe), also wird es so eng wie moeglich gefasst.  NOGDI ist dabei kein
// Zierrat: wingdi.h definiert `ERROR` als Makro und zerlegt damit `Level::ERROR`
// des Loggers — genau daran scheiterte der Cross-Bau am 2026-08-19
// ("expected unqualified-id before numeric constant").
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  define NOGDI
#  include <conio.h>
#  include <windows.h>
#  ifdef ERROR                 // falls windows.h schon vorher ohne NOGDI kam
#    undef ERROR
#  endif
#else
#  include <termios.h>
#  include <unistd.h>
#  include <sys/select.h>
#endif

namespace k1520term {

// ─── Tastencodes ─────────────────────────────────────────────────────────────
// Sonderwerte von Keyboard::read(); alles >= 0 ist ein Code für
// A5120Machine::keyPress() (ASCII 0x20..0x7E oder eine K7637::QK_*-Konstante).
constexpr int KEY_NONE   = -1;   ///< nichts angeschlagen
constexpr int KEY_LEAVE  = -2;   ///< Ctrl-] — zurück in den Debugger

// K7637-Konstanten hier gespiegelt, damit der Header allein übersetzbar bleibt
// (dieselben Werte wie core/peripherals/k7637/k7637.h — dort steht die Herkunft).
constexpr int QK_ESCAPE    = 0x01000000;
constexpr int QK_TAB       = 0x01000001;
constexpr int QK_BACKSPACE = 0x01000003;
constexpr int QK_RETURN    = 0x01000004;
constexpr int QK_DELETE    = 0x01000007;
constexpr int QK_LEFT      = 0x01000012;
constexpr int QK_UP        = 0x01000013;
constexpr int QK_RIGHT     = 0x01000014;
constexpr int QK_DOWN      = 0x01000015;
constexpr int QK_F1        = 0x01000030;

// ─── Rohmodus ────────────────────────────────────────────────────────────────

/**
 * @brief Schaltet das Terminal in den Rohmodus und beim Zerstören zurück.
 *
 * Drei Dinge werden bewusst abgeschaltet:
 *   - **ICANON/ECHO** — sonst käme jede Taste erst nach Enter an, und das
 *     Terminal schriebe sie zusätzlich selbst auf den Schirm.
 *   - **ISIG** — Ctrl-C gehört dem GAST (CP/M-Programme brechen damit ab). Ohne
 *     das bräche stattdessen der Debugger den Lauf ab. Der Rückweg ist Ctrl-].
 *   - **IXON** — Ctrl-S/Ctrl-Q sind Zeichen wie andere auch, keine Flusssteuerung.
 */
class RawMode {
public:
    RawMode() { enable(); }
    ~RawMode() { disable(); }
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

    bool ok() const { return ok_; }

    void enable() {
        if (active_) return;
#if defined(_WIN32)
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE out = GetStdHandle(STD_ERROR_HANDLE);
        if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &saved_in_)) { ok_ = false; return; }
        DWORD mode = saved_in_;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(in, mode);
        // Ohne VIRTUAL_TERMINAL_PROCESSING druckt die Konsole die ANSI-Sequenzen
        // als Text, statt sie auszuführen (Windows 10 1511+ kann es).
        if (GetConsoleMode(out, &saved_out_)) {
            SetConsoleMode(out, saved_out_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            out_saved_ = true;
        }
#else
        if (!isatty(STDIN_FILENO)) { ok_ = false; return; }
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) { ok_ = false; return; }
        struct termios raw = saved_;
        raw.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG);
        raw.c_iflag &= ~(unsigned)(IXON | ICRNL);
        raw.c_cc[VMIN]  = 0;      // nichtblockierend: liefern, was da ist
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) { ok_ = false; return; }
#endif
        active_ = true; ok_ = true;
    }

    void disable() {
        if (!active_) return;
#if defined(_WIN32)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_in_);
        if (out_saved_) SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), saved_out_);
#else
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
#endif
        active_ = false;
    }

private:
    bool active_ = false, ok_ = false;
#if defined(_WIN32)
    DWORD saved_in_ = 0, saved_out_ = 0; bool out_saved_ = false;
#else
    struct termios saved_ {};
#endif
};

// ─── Tastatur ────────────────────────────────────────────────────────────────

/**
 * @brief Liest höchstens eine Taste, nichtblockierend, und bildet sie ab.
 *
 * Die Krux sind die **Escape-Sequenzen**: ein Pfeil oben kommt als `ESC [ A`,
 * die ESC-TASTE als einzelnes `ESC`. Unterscheiden lässt sich das nur über die
 * Zeit — kommt nach einem ESC innerhalb einer kurzen Frist nichts nach, war es
 * die Taste. Deshalb der Puffer über Aufrufe hinweg plus @ref kEscFrames.
 */
class Keyboard {
public:
    /// Wie viele Aufrufe ein einzelnes ESC warten darf, bevor es als ESC-TASTE
    /// gilt. Bei 20 ms Rahmentakt sind zwei Aufrufe ~40 ms — länger als jede
    /// Sequenz braucht, kürzer als ein Mensch merkt.
    static constexpr int kEscFrames = 2;

    int read() {
        fill();
        if (buf_.empty()) { esc_wait_ = 0; return KEY_NONE; }

        const unsigned char c = buf_[0];

        if (c == 0x1D) { drop(1); return KEY_LEAVE; }          // Ctrl-]

        if (c == 0x1B) {                                       // ESC oder Sequenz
            int consumed = 0;
            const int key = decodeEscape(consumed);
            if (key != KEY_NONE) { drop(consumed); esc_wait_ = 0; return key; }
            // Unvollständig: noch etwas warten, dann als ESC-Taste werten.
            if (buf_.size() == 1 && ++esc_wait_ >= kEscFrames) {
                drop(1); esc_wait_ = 0; return QK_ESCAPE;
            }
            if (buf_.size() > 1 && esc_wait_ >= kEscFrames) {   // unbekannte Sequenz
                drop(buf_.size()); esc_wait_ = 0; return KEY_NONE;
            }
            ++esc_wait_;
            return KEY_NONE;
        }

        drop(1);
        esc_wait_ = 0;
        switch (c) {
            case 0x0D: case 0x0A: return QK_RETURN;
            case 0x09:            return QK_TAB;
            case 0x7F: case 0x08: return QK_BACKSPACE;
            default:              return (int)c;   // ASCII inkl. Ctrl-Zeichen
        }
    }

private:
    std::vector<unsigned char> buf_;
    int esc_wait_ = 0;

    void drop(size_t n) { buf_.erase(buf_.begin(), buf_.begin() + (ptrdiff_t)n); }

    /// Alles einsammeln, was gerade anliegt (nie blockieren).
    void fill() {
#if defined(_WIN32)
        while (_kbhit()) {
            int ch = _getch();
            // Sondertasten meldet die Konsole als 0x00/0xE0 + Scancode. In die
            // gemeinsame Escape-Form uebersetzen, damit decodeEscape() greift.
            if (ch == 0x00 || ch == 0xE0) {
                int sc = _getch();
                const char* seq = nullptr;
                switch (sc) {
                    case 72: seq = "\x1b[A"; break; case 80: seq = "\x1b[B"; break;
                    case 77: seq = "\x1b[C"; break; case 75: seq = "\x1b[D"; break;
                    case 83: seq = "\x1b[3~"; break;
                    default: break;
                }
                if (sc >= 59 && sc <= 66) {           // F1..F8
                    char f[6]; snprintf(f, sizeof f, "\x1bO%c", 'P' + (sc - 59));
                    for (const char* q = f; *q; ++q) buf_.push_back((unsigned char)*q);
                    continue;
                }
                if (seq) for (const char* q = seq; *q; ++q) buf_.push_back((unsigned char)*q);
                continue;
            }
            buf_.push_back((unsigned char)ch);
        }
#else
        for (;;) {
            unsigned char tmp[64];
            ssize_t n = ::read(STDIN_FILENO, tmp, sizeof tmp);
            if (n <= 0) break;
            buf_.insert(buf_.end(), tmp, tmp + n);
            if ((size_t)n < sizeof tmp) break;
        }
#endif
    }

    /// Versucht, am Pufferanfang eine vollständige Sequenz zu erkennen.
    /// @return Tastencode und @p consumed, oder KEY_NONE wenn (noch) unvollständig.
    int decodeEscape(int& consumed) {
        if (buf_.size() < 2) return KEY_NONE;
        const unsigned char b1 = buf_[1];

        if (b1 == 'O' && buf_.size() >= 3) {            // ESC O P..S = F1..F4
            const unsigned char f = buf_[2];
            if (f >= 'P' && f <= 'S') { consumed = 3; return QK_F1 + (f - 'P'); }
            return KEY_NONE;
        }
        if (b1 != '[') return KEY_NONE;
        if (buf_.size() < 3) return KEY_NONE;

        const unsigned char b2 = buf_[2];
        switch (b2) {                                   // ESC [ A..D = Pfeile
            case 'A': consumed = 3; return QK_UP;
            case 'B': consumed = 3; return QK_DOWN;
            case 'C': consumed = 3; return QK_RIGHT;
            case 'D': consumed = 3; return QK_LEFT;
            default: break;
        }
        // ESC [ <zahl> ~   (Entf, F5..F8)
        size_t i = 2; int num = 0;
        while (i < buf_.size() && buf_[i] >= '0' && buf_[i] <= '9') { num = num*10 + (buf_[i]-'0'); ++i; }
        if (i >= buf_.size()) return KEY_NONE;          // Zahl noch nicht zu Ende
        if (buf_[i] != '~') return KEY_NONE;
        consumed = (int)i + 1;
        switch (num) {
            case 3:  return QK_DELETE;
            case 15: return QK_F1 + 4;                  // F5
            case 17: return QK_F1 + 5;
            case 18: return QK_F1 + 6;
            case 19: return QK_F1 + 7;                  // F8
            default: return KEY_NONE;
        }
    }
};

// ─── Bildschirm ──────────────────────────────────────────────────────────────

/**
 * @brief Zeichnet den 80x24-Textbildschirm und schreibt nur, was sich änderte.
 *
 * Ein Vollbild je Rahmen (50 Hz x 1920 Zellen) lässt jedes Terminal flimmern und
 * flutet eine SSH-Strecke. Deshalb wird der letzte Stand gehalten und nur die
 * geänderte Zelle neu gesetzt.
 */
class ScreenDiff {
public:
    static constexpr int kCols = 80;
    static constexpr int kRows = 24;

    /// Erzwingt beim nächsten Zeichnen ein Vollbild (nach Wiedereintritt).
    void invalidate() { last_.assign(kCols * kRows, 0xFFu); cursor_ = -1; }

    /**
     * @param cell  liefert das VRAM-Byte einer Zelle: cell(row, col)
     * @param out   Ziel (der Debugger schreibt alles auf stderr)
     * @param status Zeile 25 — Hinweiszeile; leer = nicht zeichnen
     */
    template <class CellFn>
    void render(CellFn cell, FILE* out, const std::string& status = std::string()) {
        if (last_.size() != (size_t)kCols * kRows) invalidate();
        std::string o;
        int cursor_at = -1;

        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                const uint8_t raw = cell(r, c);
                if (raw & 0x80u) cursor_at = r * kCols + c;   // Bit 7 = Cursor
                uint8_t ch = raw & 0x7Fu;
                if (ch < 0x20u) ch = ' ';                     // < 0x20 zeigt der K7024 leer
                const size_t idx = (size_t)r * kCols + c;
                if (last_[idx] == ch) continue;
                last_[idx] = ch;
                char pos[24];
                snprintf(pos, sizeof pos, "\x1b[%d;%dH", r + 1, c + 1);
                o += pos;
                o += (char)ch;
            }
        }

        if (!status.empty() && status != last_status_) {
            last_status_ = status;
            o += "\x1b[25;1H\x1b[7m";                        // invers
            std::string s = status;
            if (s.size() > (size_t)kCols) s.resize(kCols);
            s.resize(kCols, ' ');
            o += s;
            o += "\x1b[0m";
        }

        if (cursor_at != cursor_ || !o.empty()) {
            cursor_ = cursor_at;
            char pos[24];
            if (cursor_at >= 0) snprintf(pos, sizeof pos, "\x1b[%d;%dH",
                                         cursor_at / kCols + 1, cursor_at % kCols + 1);
            else                snprintf(pos, sizeof pos, "\x1b[25;%dH", kCols);
            o += pos;
        }

        if (!o.empty()) { fwrite(o.data(), 1, o.size(), out); fflush(out); }
    }

    /// Vollbild löschen und Cursor nach Hause (beim Eintritt in den Konsolenmodus).
    static void clear(FILE* out) { fputs("\x1b[2J\x1b[H", out); fflush(out); }

private:
    std::vector<uint8_t> last_;
    std::string last_status_;
    int cursor_ = -1;
};

}  // namespace k1520term
