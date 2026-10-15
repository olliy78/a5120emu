/**
 * @file k52_codec.h
 * @brief `.k52` — das verlustfreie eigene Dateiformat eines Kassettenbands
 *        (doc/design/29_kassette.md §5, AP K3).
 *
 * Reine Funktionen ohne Zustand.  Aufbau (alles little-endian, Version 1):
 * ```
 * "K52T"  u8 Version(=1)  u8 Seiten(=2)  u16 Aufnahmegeschwindigkeit [mm/s]  u32 Dichte [1/1000 Bit/mm]
 * 5 × u32 Geometrie: Länge, Vorspann, Markenabstand, Markenlänge, Initiallücke   (µm)
 * je Seite (A, B):  u8 Schreibschutz  u8 0  u32 Blockzahl  { u32 Lücke [µm]  u16 Länge  Länge × u8 }…
 * ```
 * Die Blockbytes sind die rohen Bytes vom Band (Präambel bis Postambel, auch mit falscher CRC).
 * Nicht enthalten ist die Kopfposition: sie gehört zur Kassette im Laufwerk, nicht zur Datei.
 * Hin und zurück ist bitgleich (`kodieren(dekodieren(x)) == x`).  Ein unbekannte Version, ein
 * abgeschnittener oder durch Reste verlängerter Inhalt, ein Block, der nicht aufs Band passt,
 * wird abgewiesen — nie halb gelesen.
 */

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/peripherals/kassette/kassetten_medium.h"

namespace kassette::k52 {

constexpr uint8_t kVersion = 1;

/// Medium → Dateiinhalt.
std::vector<uint8_t> kodieren(const KassettenMedium& m);

/// Dateiinhalt → Medium.  nullopt und @p fehler (deutscher Satz), wenn der Inhalt kein gültiges `.k52` ist.
std::optional<KassettenMedium> dekodieren(const std::vector<uint8_t>& inhalt, std::string& fehler);

/// Beginnt @p inhalt mit der `.k52`-Kennung?  (Erkennung für Dialoge/CLI.)
bool istK52(const std::vector<uint8_t>& inhalt);

}  // namespace kassette::k52
