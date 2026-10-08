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

#include <cstdint>
#include <string>
#include <vector>

#include "core/machines/p8000/p8000.h"

namespace k1520test::p8000 {

inline constexpr long long kBatch = 5'000;

/// Zeile @p r des Terminals, rechts ohne Leerzeichen.
inline std::string zeile(const P8000Machine& m, int r) {
    std::string s = m.konsole().text(r);
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
        if (zeile(m, m.konsole().zeile()) == prompt) return true;
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

/// P8KS v4 → v3 (Kern-Terminal): die beiden letzten Byte des Konfigurationsabschnitts
/// (Terminalart, Teiler — v4 hängt sie ans Ende) entfernen, Version 3.  Für Tests älterer Stände.
inline void p8ksAufV3(std::vector<uint8_t>& v) {
    if (v.size() < 10 || v[4] != 4 || v[5] != 1) return;
    uint32_t n = uint32_t(v[6]) | uint32_t(v[7]) << 8 | uint32_t(v[8]) << 16 | uint32_t(v[9]) << 24;
    v.erase(v.begin() + 10 + n - 2, v.begin() + 10 + n);
    n -= 2;
    for (int i = 0; i < 4; ++i) v[size_t(6 + i)] = uint8_t(n >> (8 * i));
    v[4] = 3;
}

}  // namespace k1520test::p8000
