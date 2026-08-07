/**
 * @file keyboard.h
 * @brief Tastatureingabe in die laufende Maschine (K7637-Weg wie in der GUI).
 *
 * Die Wartezeiten sind Teil des Vertrags: der K7637 modelliert eine
 * 9600-Baud-Strecke, ein Zeichen braucht also seine Byte-Zeit bis zum SIO, und
 * das BIOS holt es per Timer-ISR ab.  Zu kurz gewartet, gehen Anschläge
 * verloren — deshalb sind die Werte hier zentral und nicht je Test kopiert.
 */
#pragma once

#include <cstdint>
#include <string>

#include "core/machines/a5120/a5120.h"

namespace k1520test {

/// Qt-Keycodes, die der Kern unverändert versteht (== K7637::QK_*).
inline constexpr uint32_t QK_RETURN = 0x01000004;

/// Takte, die eine Taste gedrückt bleibt bzw. nach dem Loslassen vergehen.
inline constexpr long long kKeyHoldCycles    = 1'000'000;
inline constexpr long long kKeyReleaseCycles =   300'000;

/// Eine Taste drücken und loslassen, mit BIOS-Abholzeit.
void typeKey(A5120Machine& m, uint32_t keycode);

/// Zeichenkette tippen (je Zeichen ein typeKey()).
void typeString(A5120Machine& m, const std::string& s);

/// Ctrl-<c> senden (K7637 rechnet selbst `& 0x1F`, z. B. Ctrl-C = 0x03).
void typeCtrl(A5120Machine& m, char c);

/// Taste wiederholt drücken, bis @p needle auf dem Schirm steht.
///
/// Für Programme, die die Tastatur per DIREKT-Poll lesen (HARDY, SCPX-Dialoge)
/// statt über den BDOS-Puffer: ein Anschlag geht verloren, wenn das Programm
/// gerade nicht pollt.  Sicher, solange auf den UNMITTELBAR nächsten Schirm
/// gewartet wird — ein angenommener Anschlag schaltet genau einmal weiter.
bool pressKeyUntil(A5120Machine& m, uint32_t keycode,
                   const std::string& needle, int attempts,
                   long long cycles_per_attempt = 8'000'000);

}  // namespace k1520test
