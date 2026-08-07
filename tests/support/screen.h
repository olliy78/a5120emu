/**
 * @file screen.h
 * @brief Textbildschirm des K7024 als Zeichenkette lesen.
 *
 * Der gerenderte Framebuffer taugt für Prüfungen nicht (Pixel, Zeichengenerator,
 * Attribute); das Bildwiederholram dagegen ist unmittelbar lesbar.  Alle
 * Textprüfungen der Integrations- und Systemtests laufen darüber.
 */
#pragma once

#include <string>

#include "core/machines/a5120/a5120.h"

namespace k1520test {

/// Textbildwiederholram des K7024: 0xF800–0xFFFF, 80×24 Zeichen (2 KB).
inline constexpr uint16_t kVramBase = 0xF800;
inline constexpr uint16_t kVramEnd  = 0xFFFF;
inline constexpr int      kVramCols = 80;
inline constexpr int      kVramRows = 24;

/// Das gesamte Text-VRAM als druckbares ASCII (nicht Druckbares → ' ').
///
/// Ohne Zeilenumbrüche — Suchen mit `find()` laufen dadurch über Zeilengrenzen
/// hinweg, was für Meldungen erwünscht ist, die umbrechen können.
std::string vramText(A5120Machine& m);

/// Wie vramText(), aber in 24 Zeilen à 80 Zeichen mit '\n' getrennt (Ausgabe in
/// Fehlermeldungen — so ist das Bild im Testprotokoll lesbar).
std::string vramLines(A5120Machine& m);

}  // namespace k1520test
