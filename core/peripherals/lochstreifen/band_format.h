/**
 * @file band_format.h
 * @brief Dateiformate eines 8-Spur-Lochstreifens: Roh, Intel HEX, ASCII-Art
 *        (doc/design/23_lochstreifen.md §4, AP-L2).
 *
 * Ein Band ist eine Folge von Sprossen, je acht Datenspuren = ein Byte, Spur 1 = Bit 0.
 * Die drei Formate sind nur verschiedene Schreibweisen derselben Bytefolge:
 *
 * - **Roh** (`.ptp`, `.bin`): ein Byte je Sprosse, ohne Kopf — Austausch mit anderen
 *   Emulatoren.  Vorgabe, und was übrig bleibt, wenn keins der anderen passt.
 * - **Intel HEX** (`.hex`, `.ihx`): Adresse = Bandposition, Lücken lesen sich als 00H.
 * - **ASCII-Art** (`.txt`, `.tape`): das Lochbild, eine Zeile je Sprosse, von oben nach
 *   unten laufend; zum Ansehen und zum Bearbeiten im Texteditor.
 *
 * Die Formatkennung ist eine **stabile Zahl** (Roh = 0, Intel HEX = 1, ASCII-Art = 2) —
 * die C-ABI reicht sie als `K1520_PTAPE_FMT_*` durch (AP-L3); nicht umnummerieren.
 *
 * Geschrieben wird immer mit LF (Datei binär), gelesen werden LF und CRLF.
 * Reine Funktionen ohne Zustand, aus jedem Faden aufrufbar.
 */

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lochstreifen {

/// Formatkennung — die Zahlenwerte sind Teil der C-ABI (AP-L3).
enum class Format : int {
    Roh      = 0,
    IntelHex = 1,
    AsciiArt = 2,
};

/// Alle Formate in Kennungsreihenfolge (für Auswahllisten).
std::vector<Format> alleFormate();
/// Kennung → Format; nullopt bei unbekannter Zahl.
std::optional<Format> formatAusZahl(int kennung);

/// Anzeigename („Roh“, „Intel HEX“, „ASCII-Art“).
const char* formatName(Format f);
/// Dateiendungen des Formats, klein und mit Punkt; die erste ist die übliche.
std::vector<std::string> formatEndungen(Format f);
/// Vorauswahl nach der Endung von @p pfad (Groß-/Kleinschreibung egal); sonst Roh.
Format formatAusEndung(const std::string& pfad);

/**
 * @brief Format eines Dateiinhalts erraten (Vorauswahl im Dialog des Lesers).
 *
 * Intel HEX: jede nichtleere Zeile beginnt mit `:` (mindestens eine).
 * ASCII-Art: die erste Zeile, die weder leer noch Kommentar (`;`) ist, hat die Form
 * `|…|`, oder es gibt nur Kommentare (leeres Band).  Sonst — auch für eine leere
 * Datei — Roh.
 */
Format erkennen(const std::vector<uint8_t>& inhalt);

/**
 * @brief Dateiinhalt in die Bytes des Bandes umsetzen.
 * @return nullopt und @p fehler (mit Zeilennummer, ab 1), wenn der Inhalt nicht zum
 *         Format passt.  Roh gelingt immer.
 */
std::optional<std::vector<uint8_t>> lesen(const std::vector<uint8_t>& inhalt, Format f,
                                          std::string& fehler);

/// Band als Dateiinhalt im Format @p f.  Ein leeres Band ergibt eine gültige Datei.
std::vector<uint8_t> schreiben(const std::vector<uint8_t>& band, Format f);

/// Längstes Band, das Intel HEX liest (schützt vor Satzart-04-Adressen im GiB-Bereich).
constexpr std::size_t kMaxBandLaenge = 16u * 1024u * 1024u;

}  // namespace lochstreifen
