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
#include "core/machines/k8915/k8915.h"

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

/// Nur die 24 SICHTBAREN Zeilen (0xF800–0xFF7F) als druckbares ASCII.
///
/// vramText() umfasst die ganzen 2 KB, also auch den Rest ab 0xFF80, in den das
/// CP/A-BIOS seine Statuszeile schreibt („A0\A>|0:780 |i00|l00|**CP/A** …") —
/// und zwar beim Kaltstart VOR seiner Tastaturinitialisierung.  Wer auf den
/// Prompt `A>` wartet, um danach zu tippen, muss hier suchen
/// (runSmallUntilVisible()), sonst tippt er in die Tastaturinitialisierung.
std::string visibleText(A5120Machine& m);

/// Wie vramText(), aber in 24 Zeilen à 80 Zeichen mit '\n' getrennt (Ausgabe in
/// Fehlermeldungen — so ist das Bild im Testprotokoll lesbar).
std::string vramLines(A5120Machine& m);

/// K8915: Bildspeicher der K7024 bei 1000H (doc/design/16_k8915.md §3.3), direkt
/// von der Karte gelesen — über die CPU wäre 1000H nur bei A8H-Bit0 = 0 sichtbar.
/// Bit 7 ist dort Cursorbit und wird ausgeblendet, sonst verschwände das Zeichen
/// unter dem Cursor aus jeder Suche.
std::string vramText(K8915Machine& m);
std::string vramLines(K8915Machine& m);

/// Bildschirmspeicher löschen (0x00), bevor `reset()` gerufen wird.
///
/// PFLICHT vor jedem Warten auf einen Schirmtext nach `A5120Machine::reset()`:
/// der K7024 ist am A5120 schreib-only (Lesesperre X15:1-X16:1), Lesen — und damit
/// vramText() — bedient das K3526-Schattenram unter 0xF800, und das überlebt die
/// Reset-Taste wie jedes RAM.  Ohne Wischen „findet" ein Warten auf `A>` sofort den
/// Prompt des VORIGEN Laufs, und was danach getippt wird, landet mitten im Neustart
/// (Boot-ROM/Lader, der Tastaturtreiber ist noch gar nicht installiert) und ist weg.
/// Nach `powerOn()` unnötig: dort ist das RAM 0xFF.
void wipeVram(A5120Machine& m);

}  // namespace k1520test
