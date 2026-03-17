/**
 * @file terminal_ansi.cpp
 * @brief ANSI-Terminalmodus – Konsolenbasierte Bildschirmdarstellung
 *
 * Implementierung der AnsiTerminal-Klasse, die den A5120-Bildschirm
 * über ANSI-Escape-Sequenzen (VT100/xterm) in einer beliebigen
 * Textkonsole darstellt. Die Konsole wird im Raw-Modus betrieben,
 * um einzelne Tastendrücke ohne Zeilenpufferung zu empfangen.
 *
 * Funktionsweise:
 * - Bildschirmausgabe über ANSI-Escape-Codes mit Dirty-Tracking
 * - Tastatureingabe über nicht-blockierendes read() auf stdin
 * - Escape-Sequenzen (Pfeiltasten etc.) werden auf CPA-Steuercodes gemappt
 * - Batch-Modus für automatisierte Tests ohne stdin-Zugriff
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#include "terminal.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>

/** @brief Gesicherte Original-Terminalattribute, werden beim Shutdown wiederhergestellt. */
static struct termios origTermios;

/**
 * @brief Konstruktor – initialisiert den Zustand auf Standardwerte.
 *
 * Der Bildschirmpuffer wird mit 0 vorbelegt, Cursor auf Position (0,0) gesetzt.
 */
AnsiTerminal::AnsiTerminal()
    : quit_(false), cursorRow_(0), cursorCol_(0), initialized_(false) {
    memset(prevScreen_, 0, sizeof(prevScreen_));
}

/**
 * @brief Destruktor – stellt den ursprünglichen Terminalmodus sicher wieder her.
 */
AnsiTerminal::~AnsiTerminal() {
    if (initialized_) shutdown();
}

/**
 * @brief Versetzt die Konsole in den Raw-Modus.
 *
 * Deaktiviert Echo, kanonischen Modus, Signalverarbeitung und
 * Eingabeverarbeitung. Setzt VMIN und VTIME auf 0 für nicht-blockierendes
 * Lesen. Die aktuellen Einstellungen werden zuvor gesichert.
 */
void AnsiTerminal::rawMode() {
    tcgetattr(STDIN_FILENO, &origTermios);
    struct termios raw = origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/**
 * @brief Stellt den normalen ("cooked") Konsolenmodus wieder her.
 *
 * Setzt die vor rawMode() gesicherten Terminalattribute zurück.
 */
void AnsiTerminal::cookedMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

/**
 * @brief Initialisiert das ANSI-Terminal.
 *
 * Im Batch-Modus wird nur das initialized_-Flag gesetzt.
 * Im interaktiven Modus:
 * - Konsole in Raw-Modus versetzen
 * - Bildschirm löschen (ESC[2J)
 * - Cursor nach oben links (ESC[H)
 * - Cursor verstecken (ESC[?25l)
 * - Dirty-Tracking auf vollständige Neuzeichnung setzen
 */
void AnsiTerminal::init() {
    if (batchMode_) { initialized_ = true; return; }
    rawMode();
    // Clear screen, hide cursor, move home
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);
    memset(prevScreen_, -1, sizeof(prevScreen_)); // Force full redraw
    initialized_ = true;
}

/**
 * @brief Fährt das ANSI-Terminal herunter.
 *
 * Im interaktiven Modus:
 * - Cursor wieder sichtbar machen (ESC[?25h)
 * - Attribute zurücksetzen (ESC[0m)
 * - Bildschirm löschen und Cursor auf Home
 * - Cooked-Modus wiederherstellen
 */
void AnsiTerminal::shutdown() {
    if (!initialized_) return;
    if (batchMode_) { initialized_ = false; return; }
    // Show cursor, reset attributes, clear screen
    printf("\033[?25h\033[0m\033[2J\033[H");
    fflush(stdout);
    cookedMode();
    initialized_ = false;
}

/**
 * @brief Aktualisiert den Bildschirm über ANSI-Escape-Sequenzen.
 *
 * Vergleicht jedes Zeichen mit dem vorherigen Zustand (Dirty-Tracking).
 * Nur geänderte Positionen werden neu ausgegeben:
 * - Cursor wird an die Position gesetzt (ESC[row;colH)
 * - Bei Inversdarstellung (Bit 7 gesetzt): ESC[7m + Zeichen + ESC[0m
 * - Nicht-druckbare Zeichen (< 0x20, 0x7F) werden als Leerzeichen dargestellt
 *
 * Nach der Aktualisierung wird der Cursor auf seine Sollposition zurückgesetzt.
 *
 * @param screenBuf Bildschirmpuffer (1 Byte pro Zeichen, Bit 7 = invers).
 * @param rows Anzahl Zeilen.
 * @param cols Anzahl Spalten.
 */
void AnsiTerminal::updateScreen(const uint8_t* screenBuf, int rows, int cols) {
    bool anyChange = false;

    for (int r = 0; r < rows && r < 24; r++) {
        for (int c = 0; c < cols && c < 80; c++) {
            uint8_t raw = screenBuf[r * cols + c];
            uint8_t ch = raw & 0x7F;
            bool inverse = (raw & 0x80) != 0;

            // Map non-printable to space
            if (ch < 0x20 || ch == 0x7F) ch = ' ';

            int val = ch | (inverse ? 0x100 : 0);
            if (val != prevScreen_[r][c]) {
                anyChange = true;
                prevScreen_[r][c] = val;

                printf("\033[%d;%dH", r + 1, c + 1);
                if (inverse) {
                    printf("\033[7m%c\033[0m", ch);
                } else {
                    printf("%c", ch);
                }
            }
        }
    }

    if (anyChange) {
        // Restore cursor position
        printf("\033[%d;%dH", cursorRow_ + 1, cursorCol_ + 1);
        fflush(stdout);
    }
}

/**
 * @brief Setzt die Cursorposition im Terminal.
 *
 * Sendet die ANSI-Sequenz ESC[row;colH (1-basiert).
 *
 * @param row Zeile (0-basiert).
 * @param col Spalte (0-basiert).
 */
void AnsiTerminal::setCursor(int row, int col) {
    cursorRow_ = row;
    cursorCol_ = col;
    printf("\033[%d;%dH", row + 1, col + 1);
    fflush(stdout);
}

/**
 * @brief Gibt einen Signalton aus (BEL-Zeichen, ASCII 0x07).
 */
void AnsiTerminal::bell() {
    printf("\a");
    fflush(stdout);
}

/**
 * @brief Übersetzt Tasteneingaben in CPA-kompatible Steuercodes.
 *
 * Behandelt insbesondere mehrbyte-Escape-Sequenzen für Sondertasten:
 * - ESC[A (Pfeil hoch)    → 0x1A (^Z, CPA Cursor hoch)
 * - ESC[B (Pfeil runter)  → 0x18 (^X, CPA Cursor runter)
 * - ESC[C (Pfeil rechts)  → 0x15 (^U)
 * - ESC[D (Pfeil links)   → 0x08 (Backspace)
 * - ESC[H (Home)          → 0x01 (^A)
 * - ESC[F (End)           → 0x04 (^D)
 * - ESC[3~ (Entfernen)    → 0x7F (DEL)
 * - ESC[5~ (Bild hoch)    → 0x12 (^R)
 * - ESC[6~ (Bild runter)  → 0x03 (^C)
 *
 * Weitere Zuordnungen:
 * - Enter (LF/CR) → 0x0D (CR)
 * - Backspace (127) → 0x08
 * - Ctrl+C → Quit-Signal + 0x03
 *
 * @param ch Erstes gelesenes Byte.
 * @return CPA-kompatibler Tastencode.
 */
uint8_t AnsiTerminal::translateKey(int ch) {
    // Handle escape sequences for special keys
    if (ch == 27) { // ESC
        // Check for escape sequence
        uint8_t seq[4] = {0};
        int n = read(STDIN_FILENO, &seq[0], 1);
        if (n <= 0) return 27; // Just ESC

        if (seq[0] == '[') {
            n = read(STDIN_FILENO, &seq[1], 1);
            if (n <= 0) return 27;

            switch (seq[1]) {
                case 'A': return 0x1A; // Up -> ^Z (CPA cursor up)
                case 'B': return 0x18; // Down -> ^X (CPA cursor down)
                case 'C': return 0x15; // Right -> ^U ... actually map to ^D
                case 'D': return 0x08; // Left -> backspace
                case 'H': return 0x01; // Home
                case 'F': return 0x04; // End -> ^D
                case '3': // Delete
                    read(STDIN_FILENO, &seq[2], 1); // consume ~
                    return 0x7F;
                case '5': // PgUp
                    read(STDIN_FILENO, &seq[2], 1);
                    return 0x12; // ^R
                case '6': // PgDn
                    read(STDIN_FILENO, &seq[2], 1);
                    return 0x03; // ^C
                default: return 27;
            }
        }
        return 27;
    }

    // Map Enter -> CR
    if (ch == 10 || ch == 13) return 0x0D;

    // Map backspace
    if (ch == 127) return 0x08;

    // Map Tab
    if (ch == 9) return 0x09;

    // Ctrl+C -> exit signal
    if (ch == 3) {
        quit_ = true;
        return 0x03;
    }

    return (uint8_t)ch;
}

/**
 * @brief Prüft, ob eine Taste im Eingabepuffer verfügbar ist.
 *
 * Ruft zunächst pollEvents() auf, um ausstehende Eingaben zu lesen.
 *
 * @return true wenn mindestens eine Taste im Puffer liegt.
 */
bool AnsiTerminal::keyAvailable() {
    pollEvents();
    return !keyBuffer_.empty();
}

/**
 * @brief Liest die nächste Taste aus dem Eingabepuffer.
 *
 * Ruft bei leerem Puffer pollEvents() auf. Gibt 0 zurück, wenn auch
 * danach keine Taste verfügbar ist.
 *
 * @return ASCII-Code der Taste oder 0.
 */
uint8_t AnsiTerminal::getKey() {
    if (keyBuffer_.empty()) {
        pollEvents();
        if (keyBuffer_.empty()) return 0;
    }
    uint8_t ch = keyBuffer_.front();
    keyBuffer_.pop();
    return ch;
}

/**
 * @brief Liest ausstehende Tastatureingaben von stdin (nicht-blockierend).
 *
 * Führt einen nicht-blockierenden read() auf stdin durch und übersetzt
 * jedes gelesene Byte über translateKey() in einen CPA-Steuercode.
 * Im Batch-Modus wird nichts gelesen.
 */
void AnsiTerminal::pollEvents() {
    if (batchMode_) return; // Don't read stdin in batch mode
    // Non-blocking read
    uint8_t buf[16];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    for (int i = 0; i < n; i++) {
        uint8_t key = translateKey(buf[i]);
        if (key) keyBuffer_.push(key);
    }
}

/**
 * @brief Fügt Zeichen in den Tastaturpuffer ein (Key-Injection).
 *
 * Jedes Zeichen der Eingabezeichenkette wird einzeln als uint8_t
 * in den Puffer eingefügt.
 *
 * @param keys Einzufügende Zeichenkette.
 */
void AnsiTerminal::injectKeys(const std::string& keys) {
    for (char c : keys) {
        keyBuffer_.push(static_cast<uint8_t>(c));
    }
}

/**
 * @brief Gibt zurück, ob ein Beendigungssignal empfangen wurde.
 * @return true wenn Ctrl+C gedrückt wurde.
 */
bool AnsiTerminal::shouldQuit() const {
    return quit_;
}
