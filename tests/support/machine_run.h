/**
 * @file machine_run.h
 * @brief Die Maschine laufen lassen und auf ein Ereignis warten.
 *
 * Bis 2026-08-07 waren diese Funktionen in `test_boot_integration.cpp`,
 * `test_scpx_init.cpp` und `test_hardy.cpp` jeweils eigenstaendig implementiert —
 * gleicher Code, dreimal gepflegt.  Die **Batchgroessen sind Teil des Vertrags**
 * und nicht beliebig: der Tastatur-/Timerpfad ist zyklengenau, zu grosse Batches
 * verschieben die CTC-Phase so weit, dass Eingaben verlorengehen.
 */
#pragma once

#include <string>

#include "core/machines/a5120/a5120.h"

namespace k1520test {

/// Batch der „kleinen" Schleifen — Tastatur- und Timer-ISR-vertraeglich.
inline constexpr long long kSmallBatch = 5'000;
/// Batch der groben Schleifen (kein Tastaturbezug) — deutlich schneller.
inline constexpr long long kCoarseBatch = 100'000;

/// Feste Taktzahl in kleinen Batches abarbeiten (wartende Tasten werden geleert).
void runCycles(A5120Machine& m, long long cycles);

/// Laeuft in KLEINEN Batches, bis @p needle im Textbildschirm steht.
///
/// Diese Variante ist fuer alles zu benutzen, was mit Tastatureingabe zu tun hat:
/// der K7637 modelliert eine 9600-Baud-Strecke, und die Zeichenannahme haengt am
/// Timer-Interrupt.  Mit `kCoarseBatch` driftet die Phase genug, um Anschlaege zu
/// verschlucken (dieselbe Taktung benutzt `tools/kbd_test`).
///
/// @param check_every  Wie oft der Bildschirm gelesen wird.  Die MASCHINE laeuft
///        unabhaengig davon immer in `kSmallBatch`-Schritten — nur das Absuchen
///        des 2-KB-VRAM wird seltener.  Fuer lange Laeufe (UDOS-Systemtests,
///        zweistellige Sekunden) spart ein groesserer Wert spuerbar Zeit, ohne das
///        Zeitverhalten der Maschine anzufassen.
bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles,
                   long long check_every = kSmallBatch);

/// Wie runSmallUntil(), sucht aber nur in den 24 sichtbaren Zeilen (visibleText()).
///
/// Fuer das Warten auf den Prompt `A>`, bevor getippt wird: die CP/A-Statuszeile
/// hinter Zeile 24 enthaelt ebenfalls „A>" und steht schon, BEVOR das BIOS die
/// Tastatur initialisiert (SIO-Kanal-Reset, Reset-Byte 00H an die K7637) — ein
/// Anschlag in diesem Fenster wird vom Gast verworfen (Waechter KbdNachReset.*).
bool runSmallUntilVisible(A5120Machine& m, const std::string& needle, long long max_cycles);

/// Laeuft in GROBEN Batches, bis @p needle im Textbildschirm steht.
/// Fuer reine Ausgabe-Meilensteine (Banner, Meldungen) ohne Tastaturbeteiligung.
bool runUntilVramContains(A5120Machine& m, const std::string& needle, long long max_cycles);

/// Laeuft, bis ZVE1 den PC @p target erreicht — bei ABGEHAENGTEM Boot-ROM.
///
/// Nutzt den Instruktions-Callback statt einer Abtastung, damit einmalige
/// Sprungziele (0x0437, 0x1800, 0x37A0) zuverlaessig erwischt werden.
bool runUntilPC(A5120Machine& m, uint16_t target, long long max_cycles);

}  // namespace k1520test
