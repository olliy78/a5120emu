/**
 * @file i8275.h
 * @brief Intel 8275 / КР580ВГ75 — CRT-Controller (Zeichenpuffer-Teil, ohne Taktgenauigkeit).
 *
 * Gebraucht vom PC 1715 (Ports 18H Parameter / 19H Befehl+Status, doc/design/21_pc1715.md
 * §3.4, §8.2).  Nachgebildet ist die *Logik* des Bausteins, nicht sein Zeitverhalten:
 * eine Methode @ref frame() erzeugt ein ganzes Bild, ruft dafür je Zeichenzeile die
 * DMA-Quelle der Maschine (@ref dmaRead) und liefert je Zeichenposition eine @ref Cell.
 * Das Rastern in Pixel ist Sache der Karte.
 *
 * - **Befehle** (Port 19H, Bit 7–5): 000 Reset (4 Parameter), 001 Start Display
 *   (SSSBB im Befehlsbyte), 010 Stop Display, 011 Read Light Pen (2 Leseparameter),
 *   100 Load Cursor (2 Parameter: Spalte, Zeile), 101/110 Interrupt ein/aus, 111 Preset Counters.
 * - **Reset-Parameter** (Datenblatt): P1 = S HHHHHHH (Zeichen/Zeile = H+1);
 *   P2 = VV RRRRRR (Vertikal-Retrace-Zeilen = V+1, Zeilen/Bild = R+1);
 *   P3 = UUUU LLLL (Unterstrichlinie U, Linien/Zeichenzeile = L+1);
 *   P4 = M F CC ZZZZ (M = versetzter Linienzähler, F = 0 transparent / 1 nicht transparent,
 *   CC = Cursorform 00 Block blinkend / 01 Unterstrich blinkend / 10 Block / 11 Unterstrich,
 *   ZZZZ: Horizontal-Retrace = 2·(Z+1) Zeichentakte).
 * - **Status** (Port 19H lesen): Bit 6 IE, 5 IR, 4 LP, 3 IC, 2 VE, 1 DU, 0 FO.  Lesen löscht
 *   IR/LP/IC/DU/FO.  IR entsteht am Bildende, wenn IE gesetzt ist.
 * - **Zeichencodes**: 0xxxxxxx Zeichen; 10URGGBH Feldattribut (gilt bis zum nächsten
 *   Feldattribut, am Bildanfang zurückgesetzt; nicht transparent = belegt eine Zelle,
 *   dargestellt leer; transparent = belegt keine Zelle, der Baustein holt ein Byte mehr);
 *   11CCCCBH Zeichenattribut (durchgereicht, Linienbild macht die Karte);
 *   F0 Zeilenende, F1 Zeilenende+Stop-DMA, F2 Bildende, F3 Bildende+Stop-DMA.
 *   Nach F0/F2 läuft der Rest-DMA weiter (Bytes gelesen und verworfen), nach F1 stoppt er für
 *   den Rest der Zeile, nach F3 für den Rest des Bildes.  F2/F3 lassen den Rest des Bildes leer.
 * - **Blinken**: Cursor halbperiodisch 16 Bilder, Zeichenblinken 32 Bilder [?: aus „Cursor
 *   Bildfrequenz/32, Zeichen /64" des Datenblatts abgeleitet, 50 % Tastgrad].
 *
 * Nicht nachgebildet: Lichtgriffel (liefert 0/0), Burst-Zeitverhalten (nur gespeichert),
 * FIFO-Überlauf (FO bleibt 0), DMA-Unterlauf nur, wenn keine DMA-Quelle gesetzt ist.
 */

#pragma once
#include <cstdint>
#include <functional>
#include <vector>

class I8275 {
public:
    static constexpr int MAX_COLS = 128;
    static constexpr int MAX_ROWS = 64;

    /// Eine Zeichenposition der Ausgabe.
    struct Cell {
        uint8_t code    = 0;      ///< Zeichencode (7 Bit)
        bool empty      = true;   ///< leer (VSP): nichts rastern (Attributzelle, Zeilen-/Bildrest)
        bool rvv        = false;  ///< Feldattribut R: invers
        bool lten       = false;  ///< Feldattribut U: unterstrichen
        bool gpa0       = false;  ///< Feldattribut GPA0
        bool gpa1       = false;  ///< Feldattribut GPA1
        bool hlgt       = false;  ///< Feldattribut H bzw. Zeichenattribut H: hell
        bool blink      = false;  ///< Attribut B (Rohbit; Phase siehe @ref charBlinkOn)
        bool cursor     = false;  ///< Cursor sichtbar an dieser Position (Phase berücksichtigt)
        bool charAttr   = false;  ///< Zeichenattribut 11CCCCBH
        uint8_t cca     = 0;      ///< CCCC des Zeichenattributs
    };

    /// DMA-Quelle: nächstes Byte aus dem Speicher der Maschine (Adresszähler bei der Maschine).
    std::function<uint8_t()> dmaRead;
    /// Bildbeginn (VRTC): die Maschine setzt ihren Adresszähler auf 0.
    std::function<void()> vrtc;
    /// IRQ-Ausgang hat den Pegel gewechselt.
    std::function<void(bool)> irqChanged;

    /// /RESET (Hardware): wie Reset-Befehl, dazu Cursor und Zähler.
    void reset();

    /// Port A0=0 (Parameter) bzw. A0=1 (Befehl) schreiben.
    void write(bool a0, uint8_t d);
    /// Port A0=0: Parameter lesen (Lichtgriffel), A0=1: Status (löscht IR/LP/IC/DU/FO).
    uint8_t read(bool a0);
    /// Status ohne Löschen (Debug).
    uint8_t peekStatus() const;

    /** @brief Ein Bild: VRTC, je Zeichenzeile DMA, Zellen füllen, Blinkzähler, IR. */
    void frame();

    // ── Ausgabe ──
    const Cell& cells(int row, int col) const;
    int  cols() const { return cols_; }
    int  rows() const { return rows_; }
    int  lineCount() const { return lines_; }          ///< Linien je Zeichenzeile (Zeichenhöhe)
    int  underlineLine() const { return underline_; }
    int  vrtcRows() const { return vrtc_rows_; }
    int  hrtcChars() const { return hrtc_; }
    bool spacedRows() const { return spaced_; }
    bool offsetLineCounter() const { return offset_lc_; }
    bool transparent() const { return !non_transparent_; }
    bool cursorUnderline() const { return (cursor_fmt_ & 1) != 0; }
    bool cursorBlinks() const { return (cursor_fmt_ & 2) == 0; }
    int  cursorCol() const { return cur_x_; }
    int  cursorRow() const { return cur_y_; }
    bool displayEnabled() const { return ve_; }
    bool charBlinkOn() const { return ((blink_cnt_ / 32) & 1) == 0; }
    bool configured() const { return configured_; }
    int  burstSpacing() const { return burst_space_; }  ///< Zeichentakte zwischen Bursts
    int  burstLength() const { return burst_len_; }     ///< Zyklen je DMA-Anforderung
    bool irq() const { return ir_; }
    bool interruptEnabled() const { return ie_; }
    /// DMA-Bytes, die das letzte frame() geholt hat (Wächter gegen verschobene Zeilen).
    int  lastFrameDmaBytes() const { return dma_bytes_; }

    // ── Save-State (Verdrahtung/Rückrufe nicht, Zellen werden im nächsten frame() neu gebildet) ──
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    enum Cmd { CMD_RESET, CMD_START, CMD_STOP, CMD_LPEN, CMD_CURSOR, CMD_IE, CMD_ID, CMD_PRESET };
    void command(uint8_t c);
    void setIr(bool v);
    void applyResetParams();
    uint8_t fetch();

    // Reset-Parameter (roh + abgeleitet)
    uint8_t  rp_[4] = {0, 0, 0, 0};
    bool     configured_ = false;
    int      cols_ = 0, rows_ = 0, lines_ = 0, underline_ = 0, vrtc_rows_ = 0, hrtc_ = 0;
    bool     spaced_ = false, offset_lc_ = false, non_transparent_ = false;
    uint8_t  cursor_fmt_ = 0;
    int      burst_space_ = 0, burst_len_ = 1;

    // Befehls-/Parameterzustand
    int      cmd_ = -1;       ///< laufender Befehl mit Parametern (-1 = keiner)
    int      pcount_ = 0;     ///< bisher übernommene/gelesene Parameter
    uint8_t  cur_x_tmp_ = 0;

    int      cur_x_ = 0, cur_y_ = 0;
    bool     ie_ = false, ir_ = false, lp_ = false, ic_ = false, ve_ = false, du_ = false, fo_ = false;
    uint32_t blink_cnt_ = 0;
    int      dma_bytes_ = 0;

    std::vector<Cell> cells_;
};
