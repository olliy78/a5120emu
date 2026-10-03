/**
 * @file pc1715_input.h
 * @brief Bildschirm lesen, auf Text warten und tippen am PC 1715 (Tastatur1715 über SIO-A).
 *
 * Header-only: `k1520_testsupport` linkt die 1715-Bibliothek bewusst nicht mit.  Gegenstück zu
 * `keyboard.h`/`machine_run.h` (A5120Machine).  Die Batchgröße ist die der anderen Tastaturtests
 * (5 000 Takte); eine Taste braucht Entprellung (3 Abfragedurchläufe ≈ 50 000 Takte) und eine
 * Pause bis zum nächsten Anschlag — die Zeiten stehen hier zentral.
 */
#pragma once

#include <string>

#include "core/machines/pc1715/pc1715.h"

namespace k1520test::pc1715 {

inline constexpr long long kBatch = 5'000;
/// Taste gedrückt halten: sicher über Entprellung (≈ 50 000 Takte) plus Phase des Abfragedurchlaufs.
inline constexpr long long kHalten = 150'000;
/// Nach dem Loslassen: Rahmen laufen aus, Gast holt das Zeichen, Taste gilt als losgelassen.
inline constexpr long long kPause = 100'000;

inline std::string zeile(Pc1715Machine& m, int r) {
    std::string s;
    for (int c = 0; c < m.zre().textCols(); ++c) s += char(m.screenChar(c, r) & 0x7F);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

inline std::string bild(Pc1715Machine& m) {
    std::string s;
    for (int r = 0; r < m.zre().maxZeilen(); ++r) s += zeile(m, r) + "\n";
    return s;
}

inline void laufe(Pc1715Machine& m, long long takte) {
    for (long long t = 0; t < takte; t += kBatch) m.run(int(kBatch));
}

/// Läuft, bis @p nadel irgendwo im Bild steht (höchstens @p grenze Takte).
inline bool laufeBisText(Pc1715Machine& m, const std::string& nadel, long long grenze,
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

/// Läuft, bis eine Bildzeile genau @p prompt lautet (Prompt-Zeile des Betriebssystems).
inline bool laufeBisPrompt(Pc1715Machine& m, const std::string& prompt, long long grenze) {
    long long t = 0;
    while (t < grenze) {
        t += m.run(100'000);
        for (int r = 0; r < m.zre().maxZeilen(); ++r)
            if (zeile(m, r) == prompt) return true;
    }
    return false;
}

/// Zeichenkette über die Maschinen-API tippen (keyPress/keyRelease, ASCII wie der Gast es sieht).
inline void tippe(Pc1715Machine& m, const std::string& text) {
    for (char c : text) {
        const uint32_t k = (c == '\r' || c == '\n') ? 0x01000004u : uint8_t(c);
        m.keyPress(k, false, false);
        laufe(m, kHalten);
        m.keyRelease(k);
        laufe(m, kPause);
    }
}

/// Zeile tippen und mit Return abschließen.
inline void tippeZeile(Pc1715Machine& m, const std::string& text) { tippe(m, text + "\r"); }

}  // namespace k1520test::pc1715
