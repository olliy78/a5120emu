/**
 * @file terminal.h
 * @brief P8000-Terminal (UB 8840 + 8275, Hardwarehandbuch Kap. 4) als reine Textschicht:
 *        ADM31- und VT100-Betrieb, 80 × 24 Zellen, zwei Zeichensätze, Tastatur → Bytes.
 *
 * @details
 * Unabhängig von jeder Maschine: Bytes vom Host `eingabe()`, Bytes zum Host
 * `hatAusgabe()/holeAusgabe()`, Bild über `zelle()`/`text()`.  Die Firmware des Terminals
 * wird NICHT emuliert (Entwurf 25 §7), nur ihr in `doc/p8000/hw_terminal.md` beschriebenes
 * Verhalten.
 *
 * **Attribute** sind — wie im Bildwiederholspeicher — Pseudozeichen (`TerminalZelle::feld`):
 * sie belegen eine Position (Leerzeichen am Schirm) und gelten bis zum Zeilenende (Feldregel).
 *
 * **Benannte Annahmen** (Handbuch widersprüchlich oder stumm; Tests halten sie fest):
 * - W2: ADM31-<FF>-Taste sendet 0CH (Tab. 4.3-6), nicht 09H der Referenzkarte.
 * - W3: <LINE DELETE> = `ESC R`, <LINE ERASE> = `ESC T` (Tab. 4.3-7).
 * - W6: VT100-`IL` fügt Leerzeilen VOR der Cursorzeile ein (ANSI); Cursor bleibt.
 * - NEL (`ESC E` VT100) scrollt nicht am Schirmende (Text: „endet am Bildschirmende");
 *   IND scrollt.
 * - Zeilenumbruch sofort nach dem 80. Zeichen, in beiden Moden; in der letzten Zeile Rollen.
 * - ADM31-<ESC> bleibt im Programm-Mode wirksam (sonst käme man nie heraus).
 * - Tabs fest alle 8 Spalten.  US (1FH), FS (1CH) und übrige Steuerzeichen: ohne Wirkung.
 * - <MODE>/<VIDEO> = Neuinitialisierung: Bild gelöscht, Meldung in Zeile 1, Cursor Zeile 2,
 *   ZG1, Programm-Mode aus.  Video-Attribute aus: Attributsequenzen werden verschluckt.
 * - Off-Line: Tastenbytes werden lokal angezeigt und nicht gesendet.
 * - VT100-SGR ist kumulativ (0 löscht), ADM31-`ESC G n` setzt den Zustand ab.
 * - Kein XON/XOFF vom Terminal: der Anschluss liefert nur bei freiem Empfänger (Entwurf 25 §10.6).
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace k1520 { struct ZAr; }

namespace k1520::p8000 {

enum class TerminalModus { ADM31, VT100 };

/// Attributbits einer Zelle bzw. eines Feldes.
enum TerminalAttr : uint8_t {
    ATTR_BLINK = 1, ATTR_INVERS = 2, ATTR_LEER = 4, ATTR_BOLD = 8, ATTR_UNTERSTRICH = 16,
};

struct TerminalZelle {
    uint8_t zeichen = ' ';
    uint8_t attr    = 0;       ///< nur bei `feld`: das ab hier geltende Attribut
    bool    feld    = false;   ///< Pseudozeichen (Attributwechsel), am Schirm ein Leerzeichen
    bool    zg2     = false;   ///< mit Zeichensatz 2 geschrieben
};

/// Tasten, die nicht schlicht ein Zeichen sind.
enum class TerminalTaste {
    VT, LF, FF, BS, HOME, HT, NL, CR, ESC, DEL,
    LINE_ERASE, PAGE_ERASE, LINE_INSERT, CHAR_INSERT, LINE_DELETE, CHAR_DELETE,
    TAB, BACKTAB,
    BREAK,                                  ///< Nullsignal, kein Zeichen
    MODE, VIDEO, ON_OFF, SI_SO,             ///< nur intern, kein Zeichen zum Host
};

class Terminal {
public:
    static constexpr int ZEILEN = 24;
    static constexpr int SPALTEN = 80;

    Terminal() { neuInitialisieren(); }

    // ── Host → Terminal ─────────────────────────────────────────────────────
    void eingabe(uint8_t byte);
    void eingabe(const std::string& s) { for (unsigned char c : s) eingabe(c); }

    // ── Terminal → Host ─────────────────────────────────────────────────────
    bool    hatAusgabe() const { return !aus_.empty(); }
    uint8_t holeAusgabe() { uint8_t b = aus_.front(); aus_.pop_front(); return b; }
    /// BREAK-Taste gedrückt und noch nicht vom Anschluss abgeholt.
    bool    breakAnstehend() const { return break_; }
    void    breakQuittieren() { break_ = false; }

    // ── Tastatur ────────────────────────────────────────────────────────────
    /// Zeichentaste: `c` ist das Zeichen, das Shift/Tastenbelegung schon ergeben haben.
    /// Mit `ctrl` entsteht ein Steuerzeichen (Tab. 4.3-5); Caps lock wirkt nur auf Buchstaben.
    void zeichenTaste(uint8_t c, bool ctrl = false);
    void taste(TerminalTaste t);
    void setzeCapsLock(bool an) { caps_ = an; }
    bool capsLock() const { return caps_; }

    // ── Zustand / Bild ──────────────────────────────────────────────────────
    TerminalModus modus() const { return modus_; }
    bool videoAttribute() const { return video_; }
    bool onLine() const { return online_; }
    bool programmMode() const { return programm_; }
    bool zeichensatz2() const { return zg2_; }
    int  zeile() const { return z_; }     ///< Cursor, 0-basiert
    int  spalte() const { return s_; }
    /// Anzahl BEL seit dem Einschalten (Piezophon).
    unsigned klingel() const { return klingel_; }

    const TerminalZelle& zelle(int z, int s) const { return bild_[z][s]; }
    /// Das an (z, s) wirksame Feldattribut (Feldregel: ab dem letzten Pseudozeichen der Zeile).
    uint8_t wirksamesAttribut(int z, int s) const;
    /// Zeile als Text (Pseudozeichen als Leerzeichen), immer 80 Zeichen.
    std::string text(int z) const;

    /// Einschaltzustand herstellen (wie <MODE>/<VIDEO>): Meldung, Cursor Zeile 2.
    void neuInitialisieren();

    /// Save-State (P8KS): Bild, Cursor, Modi, Parser-Zustand, unabgeholte Ausgabe.
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    using Zeile = std::array<TerminalZelle, SPALTEN>;

    void zeichenAusgeben(uint8_t c);
    void steuerzeichen(uint8_t c);
    void escZeichen(uint8_t c);
    void csiEnde(uint8_t f);
    void attributFeld(uint8_t attr);
    void weiter();                   ///< Cursor eine Spalte weiter, Umbruch/Rollen
    void zeilenvorschub();
    void bildLoeschen();
    void rollen();
    void zeichenLoeschen(int n);
    void zeichenEinfuegen(int n);
    void zeilenLoeschen(int n);
    void zeilenEinfuegen(int n);
    void loescheBisZeilenende();
    void loescheBisSchirmende();
    void loescheBisCursor();
    int  par(size_t i, int vorgabe) const;   ///< Parameter, 0/fehlt → vorgabe
    void aus(const std::string& s) { for (unsigned char c : s) aus_.push_back(c); }
    void meldung();
    void visit(k1520::ZAr& a);

    enum class Zustand { Boden, Esc, EscY, EscX, EscG, Csi };

    std::array<Zeile, ZEILEN> bild_{};
    int  z_ = 0, s_ = 0;
    int  gz_ = 0, gs_ = 0;           ///< gemerkte Cursorposition (ESC 7)
    TerminalModus modus_ = TerminalModus::ADM31;
    bool video_ = true, online_ = true, programm_ = false, zg2_ = false, caps_ = false;
    bool break_ = false;
    unsigned klingel_ = 0;
    uint8_t sgr_ = 0;                ///< kumulatives VT100-SGR
    Zustand zu_ = Zustand::Boden;
    int escY_ = 0;
    std::vector<int> par_;
    bool csiPrivat_ = false;
    std::deque<uint8_t> aus_;
};

}  // namespace k1520::p8000
