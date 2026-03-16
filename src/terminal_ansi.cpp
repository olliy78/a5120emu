// terminal_ansi.cpp - ANSI terminal implementation (console mode)
#include "terminal.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>

static struct termios origTermios;

AnsiTerminal::AnsiTerminal()
    : quit_(false), cursorRow_(0), cursorCol_(0), initialized_(false) {
    memset(prevScreen_, 0, sizeof(prevScreen_));
}

AnsiTerminal::~AnsiTerminal() {
    if (initialized_) shutdown();
}

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

void AnsiTerminal::cookedMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

void AnsiTerminal::init() {
    rawMode();
    // Clear screen, hide cursor, move home
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);
    memset(prevScreen_, -1, sizeof(prevScreen_)); // Force full redraw
    initialized_ = true;
}

void AnsiTerminal::shutdown() {
    if (!initialized_) return;
    // Show cursor, reset attributes, clear screen
    printf("\033[?25h\033[0m\033[2J\033[H");
    fflush(stdout);
    cookedMode();
    initialized_ = false;
}

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

void AnsiTerminal::setCursor(int row, int col) {
    cursorRow_ = row;
    cursorCol_ = col;
    printf("\033[%d;%dH", row + 1, col + 1);
    fflush(stdout);
}

void AnsiTerminal::bell() {
    printf("\a");
    fflush(stdout);
}

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

bool AnsiTerminal::keyAvailable() {
    pollEvents();
    return !keyBuffer_.empty();
}

uint8_t AnsiTerminal::getKey() {
    if (keyBuffer_.empty()) {
        pollEvents();
        if (keyBuffer_.empty()) return 0;
    }
    uint8_t ch = keyBuffer_.front();
    keyBuffer_.pop();
    return ch;
}

void AnsiTerminal::pollEvents() {
    // Non-blocking read
    uint8_t buf[16];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    for (int i = 0; i < n; i++) {
        uint8_t key = translateKey(buf[i]);
        if (key) keyBuffer_.push(key);
    }
}

bool AnsiTerminal::shouldQuit() const {
    return quit_;
}
