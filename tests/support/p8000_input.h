/**
 * @file p8000_input.h
 * @brief Terminal lesen, auf Text warten und tippen am P8000 (Kern-Terminal an tty1).
 *
 * Header-only (wie `pc1715_input.h`): `k1520_testsupport` linkt die P8000-Bibliothek nicht mit.
 * Getippt wird in die Tastatur des Terminals; der Terminal-Anschluss liefert die Bytes im
 * Zeichentakt des Gastformats und nur bei freiem SIO-Empfänger — deshalb darf eine ganze
 * Zeile auf einmal eingereiht werden.  Batchgröße 5 000 Takte (9600 Bd 8N2 ≈ 4 600 Takte je
 * Zeichen, Entwurf 25 §10.11).
 */
#pragma once

#include <string>

#include "core/machines/p8000/p8000.h"

namespace k1520test::p8000 {

inline constexpr long long kBatch = 5'000;

/// Zeile @p r des Terminals, rechts ohne Leerzeichen.
inline std::string zeile(const P8000Machine& m, int r) {
    std::string s = m.terminal().text(r);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

/// Ganzes Terminalbild, Zeilen mit '\n' getrennt.
inline std::string bild(const P8000Machine& m) {
    std::string s;
    for (int r = 0; r < k1520::p8000::Terminal::ZEILEN; ++r) s += zeile(m, r) + "\n";
    return s;
}

inline void laufe(P8000Machine& m, long long takte) {
    for (long long t = 0; t < takte; t += kBatch) m.run(int(kBatch));
}

/// Läuft, bis @p nadel irgendwo im Bild steht (höchstens @p grenze Takte).
inline bool laufeBisText(P8000Machine& m, const std::string& nadel, long long grenze,
                         long long pruefe_alle = 100'000) {
    long long t = 0, seit = 0;
    while (t < grenze) {
        t += kBatch; seit += kBatch;
        m.run(int(kBatch));
        if (seit >= pruefe_alle) {
            seit = 0;
            if (bild(m).find(nadel) != std::string::npos) return true;
        }
    }
    return bild(m).find(nadel) != std::string::npos;
}

/// Läuft, bis die Cursorzeile genau @p prompt lautet (höchstens @p grenze Takte).
inline bool laufeBisPrompt(P8000Machine& m, const std::string& prompt, long long grenze) {
    long long t = 0;
    while (t < grenze) {
        t += kBatch;
        m.run(int(kBatch));
        if (zeile(m, m.terminal().zeile()) == prompt) return true;
    }
    return false;
}

/// Text tippen ('\r'/'\n' = Return); die Zeichen gehen im Zeichentakt hinaus.
inline void tippe(P8000Machine& m, const std::string& text) {
    for (char c : text) m.keyPress((c == '\r' || c == '\n') ? 0x01000004u : uint8_t(c), false, false);
    m.run(int(kBatch));
}

/// Zeile tippen und mit Return abschließen.
inline void tippeZeile(P8000Machine& m, const std::string& text) { tippe(m, text + "\r"); }

}  // namespace k1520test::p8000
