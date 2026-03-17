/**
 * @file terminal.h
 * @brief Terminalabstraktion für den Robotron A5120 Emulator
 *
 * Definiert eine abstrakte Terminal-Schnittstelle sowie zwei konkrete
 * Implementierungen für die Bildschirmdarstellung und Tastatureingabe:
 *
 * - **AnsiTerminal**: Konsolenmodus – nutzt ANSI-Escape-Sequenzen zur
 *   Darstellung in jedem gängigen Terminalemulator. Geeignet für
 *   Headless-Betrieb, SSH-Sitzungen und Debugging.
 *
 * - **SDLTerminal**: Grafikmodus – nutzt SDL2 für eine pixelgenaue
 *   Darstellung mit 8×8-Bitmap-Font in Grün-auf-Schwarz-Optik
 *   (originalgetreue Monitordarstellung). Unterstützt Cursor-Blinken,
 *   deutsche Umlaute und korrekte Tastaturzuordnung.
 *
 * Beide Implementierungen bieten:
 * - Dirty-Tracking für effiziente Bildschirmaktualisierung
 * - Tastaturpuffer mit Key-Injection für automatisierte Abläufe
 * - Batch-Modus für Tests ohne Benutzerinteraktion
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#pragma once
#include <cstdint>
#include <string>
#include <queue>
#include <mutex>

/**
 * @class Terminal
 * @brief Abstrakte Basisklasse für die Terminaldarstellung.
 *
 * Definiert die Schnittstelle, die jede Terminal-Implementierung
 * bereitstellen muss: Bildschirmausgabe, Cursor-Steuerung, Tastatureingabe
 * und Lebenszyklusverwaltung.
 */
class Terminal {
public:
    virtual ~Terminal() = default;

    // =========================================================================
    /// @name Bildschirmausgabe
    // =========================================================================
    ///@{

    /** @brief Initialisiert das Terminal (Fenster öffnen, Raw-Modus setzen etc.). */
    virtual void init() = 0;

    /** @brief Fährt das Terminal herunter und gibt alle Ressourcen frei. */
    virtual void shutdown() = 0;

    /**
     * @brief Aktualisiert die Bildschirmdarstellung aus dem Zeichenpuffer.
     *
     * Nur geänderte Zeichen werden neu gezeichnet (Dirty-Tracking).
     *
     * @param screenBuf Zeiger auf den Bildschirmspeicher (1 Byte pro Zeichen).
     *                  Bit 7: Inversdarstellung/Cursor, Bits 0–6: ASCII-Zeichen.
     * @param rows Anzahl der Zeilen (max. 24).
     * @param cols Anzahl der Spalten (max. 80).
     */
    virtual void updateScreen(const uint8_t* screenBuf, int rows, int cols) = 0;

    /**
     * @brief Setzt die Cursorposition.
     * @param row Zeile (0-basiert).
     * @param col Spalte (0-basiert).
     */
    virtual void setCursor(int row, int col) = 0;

    /** @brief Gibt einen akustischen Signalton aus (BEL-Zeichen). */
    virtual void bell() = 0;

    ///@}

    // =========================================================================
    /// @name Tastatureingabe
    // =========================================================================
    ///@{

    /** @brief Prüft, ob eine Taste im Puffer verfügbar ist. */
    virtual bool keyAvailable() = 0;

    /**
     * @brief Liest die nächste Taste aus dem Eingabepuffer.
     * @return ASCII-Code der Taste oder 0, wenn keine Taste verfügbar ist.
     */
    virtual uint8_t getKey() = 0;

    /** @brief Fragt das Betriebssystem/SDL nach neuen Eingabeereignissen ab. */
    virtual void pollEvents() = 0;

    /**
     * @brief Fügt Zeichen in den Tastaturpuffer ein (Key-Injection).
     *
     * Ermöglicht automatisierte Eingaben, z. B. für Boot-Kommandos oder Tests.
     *
     * @param keys Zeichenkette, deren Zeichen einzeln in den Puffer eingefügt werden.
     */
    virtual void injectKeys(const std::string& keys) = 0;

    ///@}

    // =========================================================================
    /// @name Betriebsmodi
    // =========================================================================
    ///@{

    /**
     * @brief Aktiviert oder deaktiviert den Batch-Modus.
     *
     * Im Batch-Modus wird nicht von stdin gelesen – nur injizierte Tasten
     * werden verarbeitet. Nützlich für automatisierte Tests.
     *
     * @param batch true zum Aktivieren, false zum Deaktivieren.
     */
    virtual void setBatchMode(bool batch) { batchMode_ = batch; }

    /** @brief Gibt zurück, ob der Batch-Modus aktiv ist. */
    bool isBatchMode() const { return batchMode_; }

    ///@}

    // =========================================================================
    /// @name Lebenszyklus
    // =========================================================================
    ///@{

    /** @brief Gibt true zurück, wenn das Terminal beendet werden soll (Quit-Signal). */
    virtual bool shouldQuit() const = 0;

    ///@}

protected:
    bool batchMode_ = false;  ///< True wenn Batch-Modus aktiv (keine stdin-Eingabe)
};

/**
 * @class AnsiTerminal
 * @brief Terminal-Implementierung für ANSI-kompatible Konsolen.
 *
 * Nutzt ANSI-Escape-Sequenzen (VT100/xterm) für:
 * - Cursor-Positionierung: ESC[row;colH
 * - Inversdarstellung: ESC[7m / ESC[0m
 * - Bildschirm löschen: ESC[2J
 * - Cursor ein-/ausblenden: ESC[?25h / ESC[?25l
 *
 * Die Konsole wird in den Raw-Modus versetzt (keine Zeilenpufferung,
 * kein Echo), um einzelne Tastendrücke sofort zu empfangen.
 * Escape-Sequenzen für Pfeiltasten werden auf CPA-Steuercodes abgebildet.
 */
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
    void injectKeys(const std::string& keys) override;
    bool shouldQuit() const override;

private:
    bool quit_;                        ///< Quit-Signal wurde empfangen
    int prevScreen_[24][80];           ///< Dirty-Tracking: vorheriger Bildschirminhalt
    int cursorRow_, cursorCol_;        ///< Aktuelle Cursorposition
    bool initialized_;                 ///< Terminal wurde initialisiert
    std::queue<uint8_t> keyBuffer_;    ///< Tastatureingabepuffer

    /** @brief Versetzt die Konsole in den Raw-Modus (kein Echo, kein Zeilenpuffer). */
    void rawMode();

    /** @brief Stellt den normalen Konsolenmodus wieder her. */
    void cookedMode();

    /**
     * @brief Übersetzt einen gelesenen Tastenwert in einen CPA-Steuercode.
     *
     * Behandelt Escape-Sequenzen für Pfeiltasten, Pos1, Entf, Bild↑/↓
     * und bildet sie auf die im CPA-Betriebssystem üblichen Steuercodes ab.
     *
     * @param ch Gelesenes Zeichen (erstes Byte der Sequenz).
     * @return CPA-kompatibler Tastencode.
     */
    uint8_t translateKey(int ch);
};

/**
 * @class SDLTerminal
 * @brief Terminal-Implementierung mit SDL2-Grafikausgabe.
 *
 * Stellt den A5120-Bildschirm als Pixelgrafik in einem SDL2-Fenster dar:
 * - 640×384 Pixel (80×24 Zeichen à 8×16 Pixel, Verdopplung der 8×8-Fontzeilen)
 * - Grün-auf-Schwarz-Farbgebung (originalgetreu)
 * - Cursor-Blinken mit 500 ms Periode
 * - Bell-Effekt durch kurzen Bildschirmblitz
 *
 * Tastatureingabe erfolgt über SDL_KEYDOWN (Sondertasten, Strg-Kombinationen)
 * und SDL_TEXTINPUT (druckbare Zeichen inkl. Umlaute über mapUmlaut()).
 * Deutsche Umlaute werden gemäß DIN 66003 / ISO 646-DE auf die entsprechenden
 * ASCII-Zeichen abgebildet ({ } [ ] | \ ~).
 */
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
    void injectKeys(const std::string& keys) override;
    bool shouldQuit() const override;

private:
    bool quit_;                        ///< Quit-Signal wurde empfangen
    bool initialized_;                 ///< SDL wurde initialisiert
    void* window_;                     ///< SDL_Window* – Hauptfenster
    void* renderer_;                   ///< SDL_Renderer* – Rendering-Kontext
    void* font_;                       ///< Reserviert für zukünftige Fontdaten
    void* screenTexture_;              ///< SDL_Texture* – Bildschirmtextur für schnelles Rendering
    uint32_t* screenPixels_;           ///< ARGB-Pixelpuffer für die Bildschirmtextur
    int prevScreen_[24][80];           ///< Dirty-Tracking: vorheriger Bildschirminhalt
    std::queue<uint8_t> keyBuffer_;    ///< Tastatureingabepuffer
    int cursorRow_, cursorCol_;        ///< Aktuelle Cursorposition
    bool cursorVisible_;               ///< Aktueller Blink-Zustand des Cursors
    uint32_t cursorBlinkTime_;         ///< Zeitstempel des letzten Blink-Wechsels (SDL_GetTicks)

    /**
     * @brief Rendert ein einzelnes Zeichen in den Pixelpuffer.
     *
     * Verwendet den 8×8-Bitmap-Font und verdoppelt jede Zeile vertikal
     * (ergibt 8×16 Pixel pro Zeichen).
     *
     * @param row Zeichenzeile (0–23).
     * @param col Zeichenspalte (0–79).
     * @param ch  ASCII-Zeichen (0x20–0x7E).
     * @param inverse true für Inversdarstellung (Schwarz auf Grün).
     */
    void renderCharToBuffer(int row, int col, uint8_t ch, bool inverse);

    /**
     * @brief Übersetzt SDL-Tastencodes in CPA-Steuercodes.
     *
     * Behandelt nur nicht-druckbare Tasten (Pfeile, Return, Backspace etc.)
     * und Strg-Kombinationen. Druckbare Zeichen werden über SDL_TEXTINPUT
     * separat verarbeitet.
     *
     * @param sym SDL-Tastensymbol (SDLK_*).
     * @param mod Modifikator-Flags (KMOD_*).
     * @return CPA-kompatibler Tastencode oder 0 wenn nicht zugeordnet.
     */
    uint8_t translateSDLKey(int sym, int mod);

    /**
     * @brief Konvertiert UTF-8-kodierte deutsche Umlaute in ISO 646-DE-Zeichen.
     *
     * Zuordnungsübersicht (DIN 66003):
     * - ä → {, ö → |, ü → }
     * - Ä → [, Ö → \\, Ü → ]
     * - ß → ~
     *
     * @param text UTF-8-kodierter Text (mind. 2 Byte).
     * @return ISO 646-DE-Zeichen oder 0 bei unbekanntem Umlaut.
     */
    uint8_t mapUmlaut(const char* text);
};
