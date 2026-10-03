/**
 * @file k7609.h
 * @brief Tastatur K7609 des PRG 710 — passive Reedkontakt-Matrix hinter dem 8279.
 *
 * Abgetastet wird vom 8279 der ATP 590068, nicht von der Tastatur selbst (§3.4/§3.10);
 * das Modell setzt deshalb nur **Host-Taste → 8279-Code** `S C RRR CCC` um
 * (`doc/prg710/k7609_codes.csv`): Bit 6 = Umschaltstufe, Bit 7 (Strg) kommt in keiner
 * Tabelle vor und wird nie gesetzt.  **Ein Code je Tastendruck**, kein Loslass-Code,
 * keine Hardware-Wiederholung (die macht die Oberfläche bzw. das OS).
 *
 * **Host-Tastencodes** (`keyPress(k, shift, ctrl)`, wie die übrigen Maschinen):
 * - `QK_TASTE_BASE | c` — **physische Taste** der Matrix (Code 00H–3FH ohne Umschaltbit;
 *   `shift` setzt Bit 6).  Für die Bildschirmtastatur: erreicht jede Taste, auch die
 *   ohne Zeichen (Merker/Umschalter 10H/18H/28H, S1–S9, CL).
 * - Druckbares ASCII (20H–7EH) = **Zeichen**: Ziffern/Satzzeichen suchen die Taste, auf
 *   der das Zeichen steht (Umschaltstufe ergibt sich daraus, `shift` bleibt unbeachtet);
 *   **Buchstaben** gelten als physische Taste, Groß-/Kleinschreibung unbeachtet, die
 *   Stufe liefert `shift` — wie am Gerät, wo erst das OS aus Taste + Umschaltung + eigenen
 *   Merkern (UDOS: ohne Umschalt GROSS, SCPX: klein) das Zeichen macht.  Leertaste = 16H [?].
 * - Qt-Sondertasten: Return/Enter = ET1 (37H, CR), Escape = ET2/ST (38H), Tab = 3CH,
 *   Backspace = 08H [?], Cursor hoch/rechts/links/runter = 19H/39H/3AH/3BH.
 * - `ctrl` stellt die Strg-Einmaltaste (28H) vor den Code (zwei Codes).
 * Alles andere (Umschalt, Strg selbst, F-Tasten, …) ergibt keinen Code.
 */

#pragma once
#include "core/primitives/i8279.h"
#include <cstdint>
#include <deque>

class K7609 {
public:
    /// Kennung einer physischen Taste, wie K7672::QK_TASTE_BASE.
    static constexpr uint32_t QK_TASTE_BASE = 0x03000000;
    static constexpr uint8_t CODE_ET1 = 0x37, CODE_ET2 = 0x38, CODE_STRG1 = 0x28,
                             CODE_LEERTASTE = 0x16, CODE_TAB = 0x3C, CODE_BS = 0x08,
                             CODE_HOCH = 0x19, CODE_RECHTS = 0x39, CODE_LINKS = 0x3A,
                             CODE_RUNTER = 0x3B, UMSCHALT_BIT = 0x40;

    /** @brief An den 8279 anschließen (nullptr = Kabel gezogen). */
    void connect(I8279* kbc) { kbc_ = kbc; }

    /** @brief Taste gedrückt; Codes werden eingereiht, @ref service stellt sie zu. */
    void keyPress(uint32_t k, bool shift, bool ctrl);
    /** @brief Loslassen erzeugt keinen Code (§3.10) — nur der Vollständigkeit halber. */
    void keyRelease(uint32_t) {}

    /**
     * @brief Eingereihte Codes in den FIFO des 8279 geben, solange Platz ist (eine
     *        Taste geht nie verloren, ein Mensch tippt langsamer als die Abtastung).
     * @return true, wenn mindestens ein Code zugestellt wurde.
     */
    bool service();

    void powerOn() { wartend_.clear(); }
    bool sendetNoch() const { return !wartend_.empty(); }

    /**
     * @brief Codes (ohne Strg-Vorsatz) für einen Host-Tastencode.
     * @param out Platz für zwei Codes (Strg-Einmaltaste, Taste)
     * @return Anzahl 0–2
     */
    static int codesFuer(uint32_t k, bool shift, bool ctrl, uint8_t out[2]);

    /// Unverschobenes/verschobenes Zeichen der Taste @p code (0 = keins), aus der UDOS-Tabelle.
    static uint8_t zeichen(uint8_t code, bool umschalt);

private:
    I8279* kbc_ = nullptr;
    std::deque<uint8_t> wartend_;
};
