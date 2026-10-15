/**
 * @file egos_text.h
 * @brief Textrückgewinnung aus dem VIS-2a-Bild des MC80.3x per Mustervergleich gegen den
 *        EGOS-Zeichengenerator (für `k1520_screen_char` und Tests).
 *
 * Die VIS 2a kennt keine Zeichen — EGOS zeichnet sie Spalte für Spalte aus seinem
 * Zeichengenerator (doc/mc80/egos.md §1.8, egos.prn 008CH/0133H):
 *   - Zeigertabelle ab (FB40H) (nach RESET 0CA4H), je Zeichen 20H… ein Wort; Breite =
 *     low(Zeiger[c+1]) − low(Zeiger[c]) (Proportionalschrift),
 *   - je Spalte ein Musterbyte, **Bit 0 = unterste Bildzeile**,
 *   - doppelt hohe Zeichen (FB42H Bit 4, Einschaltmeldung): jeder Punkt 2 × 2, auf der
 *     Basis der Zeile z und in die Zeile darüber reichend — @ref egosText ordnet sie Zeile z
 *     zu und lässt z − 1 leer,
 *   - Textzeile z liegt bei Y = (F8H − 10·z + Rollzähler) & FEH … +7, X ab 0 über beide
 *     Hälften durchgehend (0…511).
 *
 * Gelesen wird der Zeichengenerator über @p mem (CPU-Sicht), damit auch ein vom Anwender
 * umgestellter Zeiger (FB40H) gilt.  Erkannt wird je Position das längste passende Muster
 * (Leerzeichen nur, wenn nichts anderes passt); eine nicht erkannte belegte Spalte ergibt
 * „?“.  Grafik wird nicht zurückgewonnen.  Ergebnis ohne Leerzeichen am Ende.
 */

#pragma once
#include "core/cards/mc80/vis2a.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mc80 {

constexpr int EGOS_TEXTZEILEN = 25;

/// Text der EGOS-Textzeile @p zeile (0 = oben), nur normale Zeichenhöhe.
std::string egosTextzeile(const Mc80Vis2a& vis, const std::function<uint8_t(uint16_t)>& mem,
                          int zeile);

/// Alle 25 Textzeilen, mit doppelt hohen Zeichen.
std::vector<std::string> egosText(const Mc80Vis2a& vis, const std::function<uint8_t(uint16_t)>& mem);

}  // namespace mc80
