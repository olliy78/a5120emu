// terminal_sdl.cpp - SDL2 graphical terminal implementation
#ifdef USE_SDL
#include "terminal.h"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include "font8x8.h"

SDLTerminal::SDLTerminal()
    : quit_(false), initialized_(false),
      window_(nullptr), renderer_(nullptr), font_(nullptr),
      screenTexture_(nullptr), screenPixels_(nullptr),
      cursorRow_(0), cursorCol_(0), cursorVisible_(true), cursorBlinkTime_(0) {
    memset(prevScreen_, -1, sizeof(prevScreen_));
}

SDLTerminal::~SDLTerminal() {
    if (initialized_) shutdown();
}

void SDLTerminal::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    int winW = 80 * 8;   // 640
    int winH = 24 * 16;  // 384

    window_ = SDL_CreateWindow("Robotron A5120 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window_) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    renderer_ = SDL_CreateRenderer((SDL_Window*)window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer((SDL_Window*)window_, -1, SDL_RENDERER_SOFTWARE);
    }

    SDL_RenderSetLogicalSize((SDL_Renderer*)renderer_, winW, winH);

    // Create screen texture for fast rendering
    screenTexture_ = SDL_CreateTexture((SDL_Renderer*)renderer_,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, winW, winH);

    // Allocate pixel buffer
    screenPixels_ = new uint32_t[winW * winH];
    memset(screenPixels_, 0, winW * winH * sizeof(uint32_t));

    // Force full redraw on first update
    memset(prevScreen_, -1, sizeof(prevScreen_));

    cursorBlinkTime_ = SDL_GetTicks();

    // Enable text input for proper keyboard handling (handles @, umlauts, etc.)
    SDL_StartTextInput();

    initialized_ = true;
}

void SDLTerminal::shutdown() {
    if (!initialized_) return;
    SDL_StopTextInput();
    if (screenTexture_) { SDL_DestroyTexture((SDL_Texture*)screenTexture_); screenTexture_ = nullptr; }
    delete[] screenPixels_; screenPixels_ = nullptr;
    if (renderer_) { SDL_DestroyRenderer((SDL_Renderer*)renderer_); renderer_ = nullptr; }
    if (window_) { SDL_DestroyWindow((SDL_Window*)window_); window_ = nullptr; }
    SDL_Quit();
    initialized_ = false;
}

void SDLTerminal::renderCharToBuffer(int row, int col, uint8_t ch, bool inverse) {
    if (!screenPixels_) return;

    const int screenW = 640;
    int startX = col * 8;
    int startY = row * 16;

    uint32_t fg = inverse ? 0xFF000000 : 0xFF00FF00; // Black or Green (ARGB)
    uint32_t bg = inverse ? 0xFF00FF00 : 0xFF000000; // Green or Black

    const uint8_t* glyph = (ch >= 0x20 && ch < 0x7F) ? font8x8_basic[ch - 0x20] : nullptr;

    for (int y = 0; y < 8; y++) {
        uint8_t rowData = glyph ? glyph[y] : 0;
        int py0 = (startY + y * 2) * screenW + startX;
        int py1 = (startY + y * 2 + 1) * screenW + startX;
        for (int x = 0; x < 8; x++) {
            uint32_t color = (rowData & (1 << x)) ? fg : bg;
            screenPixels_[py0 + x] = color;
            screenPixels_[py1 + x] = color;
        }
    }
}

void SDLTerminal::updateScreen(const uint8_t* screenBuf, int rows, int cols) {
    if (!renderer_ || !screenTexture_ || !screenPixels_) return;

    // Update cursor blink (500ms period)
    uint32_t now = SDL_GetTicks();
    if (now - cursorBlinkTime_ > 500) {
        cursorVisible_ = !cursorVisible_;
        cursorBlinkTime_ = now;
    }

    bool anyChange = false;

    for (int row = 0; row < rows && row < 24; row++) {
        for (int col = 0; col < cols && col < 80; col++) {
            uint8_t raw = screenBuf[row * cols + col];
            uint8_t ch = raw & 0x7F;
            bool isCursor = (raw & 0x80) != 0;
            if (ch < 0x20 || ch == 0x7F) ch = ' ';

            bool inverse = isCursor && cursorVisible_;
            int val = ch | (inverse ? 0x100 : 0);

            if (val != prevScreen_[row][col]) {
                prevScreen_[row][col] = val;
                anyChange = true;
                renderCharToBuffer(row, col, ch, inverse);
            }
        }
    }

    if (anyChange) {
        auto* r = (SDL_Renderer*)renderer_;
        SDL_UpdateTexture((SDL_Texture*)screenTexture_, NULL, screenPixels_, 640 * sizeof(uint32_t));
        SDL_RenderCopy(r, (SDL_Texture*)screenTexture_, NULL, NULL);
        SDL_RenderPresent(r);
    }
}

void SDLTerminal::setCursor(int row, int col) {
    cursorRow_ = row;
    cursorCol_ = col;
}

void SDLTerminal::bell() {
    if (!renderer_) return;
    auto* r = (SDL_Renderer*)renderer_;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 64);
    SDL_Rect full = {0, 0, 640, 384};
    SDL_RenderFillRect(r, &full);
    SDL_RenderPresent(r);
    SDL_Delay(50);
    // Force full redraw on next update
    memset(prevScreen_, -1, sizeof(prevScreen_));
}

uint8_t SDLTerminal::mapUmlaut(const char* text) {
    // DIN 66003 / German ISO 646 mapping (standard for German CP/M systems)
    unsigned char c0 = (unsigned char)text[0];
    unsigned char c1 = (unsigned char)text[1];

    if (c0 == 0xC3) {
        switch (c1) {
            case 0xA4: return '{';  // ä
            case 0xB6: return '|';  // ö
            case 0xBC: return '}';  // ü
            case 0x84: return '[';  // Ä
            case 0x96: return '\\'; // Ö
            case 0x9C: return ']';  // Ü
            case 0x9F: return '~';  // ß
            default: break;
        }
    }
    return 0;
}

uint8_t SDLTerminal::translateSDLKey(int sym, int mod) {
    bool ctrl = (mod & KMOD_CTRL) != 0;

    // Control key combinations
    if (ctrl) {
        if (sym >= SDLK_a && sym <= SDLK_z) {
            return (uint8_t)(sym - SDLK_a + 1);
        }
    }

    // Non-printable / control keys only - printable chars handled by SDL_TEXTINPUT
    switch (sym) {
        case SDLK_RETURN:    return 0x0D;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB:       return 0x09;
        case SDLK_ESCAPE:    return 0x1B;
        case SDLK_DELETE:    return 0x7F;
        case SDLK_UP:        return 0x1A; // CPA: ^Z = cursor up
        case SDLK_DOWN:      return 0x18; // CPA: ^X = cursor down
        case SDLK_LEFT:      return 0x13; // CPA: ^S = cursor left
        case SDLK_RIGHT:     return 0x04; // CPA: ^D = cursor right
        case SDLK_HOME:      return 0x01;
        case SDLK_PAGEUP:    return 0x12; // ^R
        case SDLK_PAGEDOWN:  return 0x03; // ^C
        default: return 0;
    }
}

bool SDLTerminal::keyAvailable() {
    pollEvents();
    return !keyBuffer_.empty();
}

uint8_t SDLTerminal::getKey() {
    if (keyBuffer_.empty()) {
        pollEvents();
        if (keyBuffer_.empty()) return 0;
    }
    uint8_t ch = keyBuffer_.front();
    keyBuffer_.pop();
    return ch;
}

void SDLTerminal::pollEvents() {
    if (!initialized_) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                quit_ = true;
                break;
            case SDL_KEYDOWN: {
                // Only handle non-printable / control keys here
                uint8_t key = translateSDLKey(event.key.keysym.sym, event.key.keysym.mod);
                if (key) keyBuffer_.push(key);
                break;
            }
            case SDL_TEXTINPUT: {
                // Handle all printable characters via text input
                // This correctly handles @, umlauts, and all keyboard layouts
                const char* text = event.text.text;
                unsigned char c0 = (unsigned char)text[0];

                if (c0 < 0x80) {
                    // ASCII character
                    if (c0 >= 0x20 && c0 < 0x7F) {
                        keyBuffer_.push(c0);
                    }
                } else {
                    // Multi-byte UTF-8 (German umlauts etc.)
                    uint8_t mapped = mapUmlaut(text);
                    if (mapped) keyBuffer_.push(mapped);
                }
                break;
            }
            default:
                break;
        }
    }
}

bool SDLTerminal::shouldQuit() const {
    return quit_;
}

#endif // USE_SDL
