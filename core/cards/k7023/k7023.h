/**
 * @file k7023.h
 * @brief K7023 / K7023.01 ABS (BAB1) – Anschlusssteuerung Bildschirm 16 x 64 (fruehe A5120).
 *
 * Reine Speicherkarte (keine E/A, keine Interrupts): 1 KB Bildspeicher an einer
 * 1-KB-Adresse (Vorgabe FC00H, alle sechs Bruecken geschlossen), 16 Zeilen x 64 Zeichen,
 * Zelle 8 x 16 Punkte, Bild 512 x 256.  Beleg und alle Offenpunkte: doc/a5120_alt/k7023.md.
 *
 * Festlegungen (Belegstelle jeweils im Merkblatt):
 *  - Offset = Zeile * 64 + Spalte; Bit 0-6 = Zeichencode, Bit 7 = Kursor (§3).
 *  - KEINE Lesesperre (anders als K7024.40): isReadable() == true (§2).
 *  - Zeichengenerator: 2 x 1 KB, Linien 0-7 aus BAB1-46-2, 8-15 aus BAB1-47-1;
 *    Adresse = Code * 8 + Linie; D7 = linker Punkt, D0 (8. Punkt) stets dunkel (§6).
 *  - Linien 0-10 aus dem Abzug, Linie 11 leer, Linien 12-15: Kursorbalken ueber die volle
 *    Zellbreite (§6, §7); der Kursor ist stets hell.
 *  - Nur K7023.01: Marken 04H-0FH im Bildspeicher setzen (DB1 = 1) bzw. loeschen (DB1 = 0)
 *    die Helligkeit "hell"; der Zustand laeuft zeilenuebergreifend, Anfang = normal.
 *    Die Marke selbst ist dunkel (Abzug liefert fuer Codes < 20H nichts).  Zuruecksetzen am
 *    Zeilen-/Bildende ist am Original nicht geklaert [?] -> hier: kein Zuruecksetzen.
 *  - Blinkteiler des Kursors ist unbekannt [?]; Blinken wird deshalb nur als Phase
 *    (setBlinkPhase) bereitgestellt, getaktet vom Aufrufer.
 *
 * Pixelwerte: normal = Config::helligkeit_normal, hell = 0xFF.  Die K7023 (.00) kennt nur
 * eine Stufe und liefert 0xFF.
 *
 * Registrierung: NACH den RAM-Karten, damit der Bus der Karte den Vorrang gibt (wie K7024).
 */
#pragma once
#include <cstdint>
#include <vector>
#include "core/bus/k1520_bus.h"

class K7023 : public MemDevice {
public:
    static constexpr int kCols = 64, kRows = 16, kCellW = 8, kCellH = 16;
    static constexpr int kFbW = kCols * kCellW;     ///< 512
    static constexpr int kFbH = kRows * kCellH;     ///< 256
    static constexpr int kVramSize = kCols * kRows; ///< 1024

    struct Config {
        uint16_t vram_base = 0xFC00;   ///< 1-KB-Raster (A10-A15 = Bruecken/Schalter); FC00H = alle zu
        bool     variante_01 = false;  ///< true = K7023.01 (zwei Helligkeitsstufen, Marken)
        bool     cursor_blink = false; ///< X21/S7: false = ruhend, true = blinkend (Phase: setBlinkPhase)
        uint8_t  helligkeit_normal = 0xB4; ///< Pixelwert "normal" der .01 (hell = 0xFF)
        /// Zeichengenerator Linien 0-7 / 8-15 (je 1024 B); nullptr = eingebaute Abzuege BAB1-46-2 / -47-1.
        const uint8_t* chargen_lines0_7  = nullptr;
        const uint8_t* chargen_lines8_15 = nullptr;
    };

    explicit K7023(K1520Bus& bus);                       ///< Vorgabe: K7023 (.00) bei FC00H
    K7023(K1520Bus& bus, const Config& cfg);

    // MemDevice
    uint8_t memRead(uint16_t addr) override;
    void    memWrite(uint16_t addr, uint8_t data) override;
    bool    isWritable() const override { return true; }
    bool    isReadable() const override { return true; }   ///< keine Lesesperre (Merkblatt §2)

    void attachToBus(K1520Bus& bus);

    const uint8_t* getFramebuffer() const { return framebuffer_; }
    int  fbWidth()  const { return kFbW; }
    int  fbHeight() const { return kFbH; }
    bool fbDirty() const { return dirty_; }
    void fbClearDirty() { dirty_ = false; }

    /// Bildspeicherbyte der Zelle (Quelle fuer `k1520_screen_char`: Bit 0-6 Zeichen, Bit 7 Kursor).
    uint8_t vramRead(int col, int row) const;
    void    vramWrite(int col, int row, uint8_t v);
    void    clearScreen(uint8_t blank = 0x20);

    /// Kursorphase bei cursor_blink (true = Balken sichtbar). Ohne Blinken wirkungslos.
    void setBlinkPhase(bool on);

    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

    const Config& config() const { return cfg_; }

private:
    static bool istMarke(uint8_t v) { return (v & 0x7F) >= 0x04 && (v & 0x7F) <= 0x0F; }
    void renderCell(int idx);
    void renderAll();

    K1520Bus& bus_;
    Config    cfg_;
    uint8_t   vram_[kVramSize]{};
    bool      hell_vor_[kVramSize]{};   ///< Helligkeitszustand VOR der Zelle (nur .01)
    uint8_t   framebuffer_[kFbW * kFbH]{};
    bool      dirty_ = false;
    bool      blink_on_ = true;
};
