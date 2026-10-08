/**
 * @file terminal_tasten.h
 * @brief Tastenkodes der Oberfläche → Terminaltaste (P8000, Kern- wie Originalterminal).
 *
 * Kodes (k1520_key_press / k1520_term_key): druckbares ASCII und Steuerzeichen 01H–1FH, die
 * Qt-Kodes Return/Enter/Escape/Backspace/Tab/Delete, die Pfeiltasten (<BS> <VT> <FF> <LF>), Pos1,
 * Shift+Tab (BACKTAB) und `0x02000000 + TerminalTaste` für Tasten ohne Qt-Gegenstück.  Caps lock
 * (0x02000100/0x02000101) und Matrixkodes (0x04000000 | Zeile << 8 | Spalte) behandelt der
 * Aufrufer, weil sie vom Terminal abhängen.
 */
#pragma once

#include <cstdint>

#include "core/peripherals/p8000_terminal/terminal.h"

namespace k1520::p8000 {

/// Kode @p code an @p g (`taste(TerminalTaste)`, `zeichenTaste(c, ctrl)`) geben; false = kein
/// solcher Kode (oder ein Kode, den der Aufrufer selbst behandeln muss).
template <class G>
bool qtTasteAnTerminal(G& g, uint32_t code, bool ctrl) {
    using T = TerminalTaste;
    switch (code) {
        case 0x01000004: case 0x01000005: g.taste(T::CR);  return true;   // Return/Enter
        case 0x01000000: g.taste(T::ESC); return true;                    // Escape
        case 0x01000003: g.taste(T::BS);  return true;                    // Backspace
        case 0x01000001: g.taste(T::HT);  return true;                    // Tab
        case 0x01000007: g.taste(T::DEL); return true;                    // Delete
        // Cursortasten des Terminals (<BS> <VT> <FF> <LF>) und <HOME> (Tab. 4.3-6) sowie Shift+Tab
        case 0x01000012: g.taste(T::BS);  return true;                    // Pfeil links
        case 0x01000013: g.taste(T::VT);  return true;                    // Pfeil hoch
        case 0x01000014: g.taste(T::FF);  return true;                    // Pfeil rechts
        case 0x01000015: g.taste(T::LF);  return true;                    // Pfeil runter
        case 0x01000010: g.taste(T::HOME); return true;                   // Pos1
        case 0x01000002: g.taste(T::BACKTAB); return true;                // Shift+Tab
        default: break;
    }
    if (code >= 0x02000000 && code <= 0x02000000 + uint32_t(T::SI_SO)) {
        g.taste(static_cast<T>(code - 0x02000000));
        return true;
    }
    if (code == '\r' || code == '\n') { g.taste(T::CR); return true; }
    if (code < 0x80) { g.zeichenTaste(static_cast<uint8_t>(code), ctrl); return true; }
    return false;
}

}  // namespace k1520::p8000
