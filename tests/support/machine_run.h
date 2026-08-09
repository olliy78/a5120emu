/**
 * @file machine_run.h
 * @brief Die Maschine laufen lassen und auf ein Ereignis warten.
 *
 * Bis 2026-08-07 waren diese Funktionen in `test_boot_integration.cpp`,
 * `test_scpx_init.cpp` und `test_hardy.cpp` jeweils eigenständig implementiert —
 * gleicher Code, dreimal gepflegt.  Die **Batchgrößen sind Teil des Vertrags**
 * und nicht beliebig: der Tastatur-/Timerpfad ist zyklengenau, zu große Batches
 * verschieben die CTC-Phase so weit, dass Eingaben verlorengehen.
 */
#pragma once

#include <string>

#include "core/machines/a5120/a5120.h"

namespace k1520test {

/// Batch der „kleinen" Schleifen — Tastatur- und Timer-ISR-verträglich.
inline constexpr long long kSmallBatch = 5'000;
/// Batch der groben Schleifen (kein Tastaturbezug) — deutlich schneller.
inline constexpr long long kCoarseBatch = 100'000;

/// Feste Taktzahl in kleinen Batches abarbeiten (wartende Tasten werden geleert).
void runCycles(A5120Machine& m, long long cycles);

/// Läuft in KLEINEN Batches, bis @p needle im Textbildschirm steht.
///
/// Diese Variante ist für alles zu benutzen, was mit Tastatureingabe zu tun hat:
/// der K7637 modelliert eine 9600-Baud-Strecke, und die Zeichenannahme hängt am
/// Timer-Interrupt.  Mit `kCoarseBatch` driftet die Phase genug, um Anschläge zu
/// verschlucken (dieselbe Taktung benutzt `tools/kbd_test`).
///
/// @param check_every  Wie oft der Bildschirm gelesen wird.  Die MASCHINE läuft
///        unabhängig davon immer in `kSmallBatch`-Schritten — nur das Absuchen
///        des 2-KB-VRAM wird seltener.  Für lange Läufe (UDOS-Systemtests,
///        zweistellige Sekunden) spart ein größerer Wert spürbar Zeit, ohne das
///        Zeitverhalten der Maschine anzufassen.
bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles,
                   long long check_every = kSmallBatch);

/// Läuft in GROBEN Batches, bis @p needle im Textbildschirm steht.
/// Für reine Ausgabe-Meilensteine (Banner, Meldungen) ohne Tastaturbeteiligung.
bool runUntilVramContains(A5120Machine& m, const std::string& needle, long long max_cycles);

/// Läuft, bis ZVE1 den PC @p target erreicht — bei ABGEHÄNGTEM Boot-ROM.
///
/// Nutzt den Instruktions-Callback statt einer Abtastung, damit einmalige
/// Sprungziele (0x0437, 0x1800, 0x37A0) zuverlässig erwischt werden.
bool runUntilPC(A5120Machine& m, uint16_t target, long long max_cycles);

}  // namespace k1520test
