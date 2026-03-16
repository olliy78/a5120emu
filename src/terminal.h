// terminal.h - Terminal display for A5120 (console + SDL window modes)
#pragma once
#include <cstdint>
#include <string>
#include <queue>
#include <mutex>

// Abstract terminal interface
class Terminal {
public:
    virtual ~Terminal() = default;

    // Display
    virtual void init() = 0;
    virtual void shutdown() = 0;
    virtual void updateScreen(const uint8_t* screenBuf, int rows, int cols) = 0;
    virtual void setCursor(int row, int col) = 0;
    virtual void bell() = 0;

    // Keyboard
    virtual bool keyAvailable() = 0;
    virtual uint8_t getKey() = 0;
    virtual void pollEvents() = 0;

    // Lifecycle
    virtual bool shouldQuit() const = 0;
};

// ANSI terminal (console mode - works in any terminal emulator)
class AnsiTerminal : public Terminal {
public:
    AnsiTerminal();
    ~AnsiTerminal() override;

    void init() override;
    void shutdown() override;
    void updateScreen(const uint8_t* screenBuf, int rows, int cols) override;
    void setCursor(int row, int col) override;
    void bell() override;
    bool keyAvailable() override;
    uint8_t getKey() override;
    void pollEvents() override;
    bool shouldQuit() const override;

private:
    bool quit_;
    int prevScreen_[24][80];
    int cursorRow_, cursorCol_;
    bool initialized_;
    std::queue<uint8_t> keyBuffer_;

    void rawMode();
    void cookedMode();
    uint8_t translateKey(int ch);
};

// SDL2 window terminal (graphical mode)
class SDLTerminal : public Terminal {
public:
    SDLTerminal();
    ~SDLTerminal() override;

    void init() override;
    void shutdown() override;
    void updateScreen(const uint8_t* screenBuf, int rows, int cols) override;
    void setCursor(int row, int col) override;
    void bell() override;
    bool keyAvailable() override;
    uint8_t getKey() override;
    void pollEvents() override;
    bool shouldQuit() const override;

private:
    bool quit_;
    bool initialized_;
    void* window_;          // SDL_Window*
    void* renderer_;        // SDL_Renderer*
    void* font_;            // internal font data
    void* screenTexture_;   // SDL_Texture* for fast rendering
    uint32_t* screenPixels_; // pixel buffer for texture
    int prevScreen_[24][80]; // dirty tracking
    std::queue<uint8_t> keyBuffer_;
    int cursorRow_, cursorCol_;
    bool cursorVisible_;
    uint32_t cursorBlinkTime_;

    void renderCharToBuffer(int row, int col, uint8_t ch, bool inverse);
    uint8_t translateSDLKey(int sym, int mod);
    uint8_t mapUmlaut(const char* text);
};
