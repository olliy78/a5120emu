/**
 * @file mc80_tastatur.h
 * @brief Abgesetzte Tastatur des MC80.3x (eigener U881) auf ZEICHENEBENE an SIO A der ASP.
 *
 * Quellen: doc/mc80/egos.md §3 (Tastaturprotokoll aus der ISR 044BH/046FH),
 * doc/mc80/hardware_3x.md §6.3.  Die Firmware der Tastatur liegt nicht vor; nachgebildet
 * wird, was EGOS erwartet:
 *
 *   - 9600 Bd 7O2 → ein Zeichen = 11 Bit = 11 × 256 Systemtakte (2,4576 MHz).
 *   - **Präfix** 1CH–1FH gilt für alle folgenden Zeichen (FB7CH).  Das Modell sendet es vor
 *     JEDEM Anschlag [Annahme]: das Kommando INIT setzt FB7CH zurück, eine nur bei Wechsel
 *     sendende Tastatur wäre danach stumm, bis ein anderes Präfix käme.
 *   - 1CH: Zeichentasten so, wie die Kappe es sagt (Kleinbuchstabe ohne Umschalter).  EGOS
 *     tauscht Groß/Klein in der Vorgabe (FB7FH Bit 4) — „a“ erscheint also als „A“.
 *     Steuerzeichen (00H–1BH) laufen ebenfalls unter 1CH.
 *   - **ENTER = 11H**, **OFF/Abbruch = 13H** [egos.md §3: Kommandoschleife 07FBH].
 *   - 1EH + Bitmaske: Sondertaste n (0…7) — Maske 40H >> n, n = 7 → 00H.
 *   - 1FH + Code: Funktions-/Kursortasten (EGOS: Code | 80H) [?: Codes je Taste unbekannt].
 *   - Rechner → Tastatur: 07H Summer (Zähler), 14H/12H Anzeige an/aus [?: welche LED].
 *
 * **Anschlagabstand [Annahme]:** zwischen zwei TASTEN liegen mindestens 20 ms Maschinenzeit
 * (≈ 50 Anschläge/s; Präfix und Code folgen dagegen im Zeichenabstand).  EGOS wertet das
 * Zeichen erst im nachgeordneten CTC-K3-Interrupt aus (FB45H) und zeichnet das Echo — käme
 * die nächste Taste schneller, überschriebe sie FB45H.  Eine echte Tastatur ist nie schneller.
 *
 * Wiederholung: macht die echte Tastatur selbst; hier nicht nachgebildet (ein keyPress = ein
 * Anschlag).
 */

#pragma once
#include <cstdint>
#include <deque>
#include <functional>

class Mc80Tastatur {
public:
    static constexpr int TAKTE_JE_ZEICHEN = 11 * 256;    ///< 9600 Bd bei 2,4576 MHz, 7O2
    static constexpr int TAKTE_JE_TASTE   = 49'152;      ///< 20 ms [Annahme]

    static constexpr uint8_t PRAEFIX_ZEICHEN  = 0x1C;
    static constexpr uint8_t PRAEFIX_EBENE2   = 0x1D;
    static constexpr uint8_t PRAEFIX_SONDER   = 0x1E;
    static constexpr uint8_t PRAEFIX_FUNKTION = 0x1F;
    static constexpr uint8_t ENTER = 0x11;
    static constexpr uint8_t OFF   = 0x13;

    /// Leitung zum Rechner: frei? / Zeichen übergeben.
    std::function<bool()>        empfaengerFrei;
    std::function<void(uint8_t)> senden;

    void reset();   ///< RES-Taste/Netz ein: Warteschlange leer

    /// Zeichentaste (Präfix 1CH): Code so, wie er gesendet wird.
    void zeichen(uint8_t code);
    /// Sondertaste 0…7 (Präfix 1EH + Bitmaske).
    void sondertaste(int n);
    /// Funktions-/Kursortaste (Präfix 1FH + Code) [?].
    void funktionstaste(uint8_t code);
    /// Text tippen: jedes Zeichen als Zeichentaste, '\n'/'\r' = ENTER.
    void tippe(const char* text);

    /**
     * Taste der Oberfläche (Qt-Code oder ASCII wie k1520_key_press).  Buchstaben: ohne
     * Umschalter klein, mit groß (Tastenkappe), Strg+Buchstabe = Code & 1FH; Return/Enter
     * = ENTER, Escape = OFF, Rückschritt = 08H [?], Tab = 09H, F1…F8 = Sondertaste 0…7.
     * Unbekanntes wird verworfen.  @return true, wenn ein Anschlag entstand.
     */
    bool qtTaste(uint32_t keycode, bool shift, bool ctrl);

    /// Maschinentakte verstreichen lassen; liefert Zeichen aus, wenn die Leitung frei ist.
    /// @return true, wenn ein Zeichen übergeben wurde.
    bool takt(int takte);

    /// Zeichen vom Rechner (SIO A Senden).
    void vomRechner(uint8_t b);

    bool     leer() const { return queue_.empty(); }
    size_t   wartend() const { return queue_.size(); }
    uint32_t summerZaehler() const { return summer_; }
    bool     anzeige() const { return anzeige_; }

private:
    struct Eintrag { uint8_t byte; bool tastenanfang; };
    void anschlag(uint8_t praefix, uint8_t code);

    std::deque<Eintrag> queue_;
    int64_t  bis_zeichen_ = 0;  ///< Takte bis zum nächsten möglichen Zeichen
    int64_t  bis_taste_ = 0;    ///< Takte bis zur nächsten möglichen Taste
    uint32_t summer_ = 0;
    bool     anzeige_ = false;
};
