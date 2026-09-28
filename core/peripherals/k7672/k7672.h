/**
 * @file k7672.h
 * @brief Tastatur K7672.03 (Folientastatur mit Z8, seriell 9600 Bd) — Modell auf
 *        Protokollebene für den K8915.
 *
 * Grundlage ist die ausgewertete Firmware (doc/EPROMS/K7672/README.md), keine
 * Z8-Nachbildung.  Nachgebildet:
 *
 * - **Rechner → Tastatur** (SIO2-B des K8915 sendet): `DC3` sperrt das Senden,
 *   `DC1` gibt es frei, `BEL` = Summer, `ESC` + Folge aus der Befehlstabelle
 *   06F8H.  Wirkung haben `ESC c` (Neustart + Selbsttest, danach `DC1`),
 *   `ESC [2;1y` (Selbsttest, danach `DC1`), `ESC [2;0y` (Neustart ohne Test,
 *   sofort `DC1`) und `ESC [?22h` (DCP-Modus); die übrigen Folgen der Tabelle
 *   werden erkannt und verworfen.
 * - **Tastatur → Rechner** im **SCP-Modus**: ein Zeichen je Taste.
 * - Jedes gesendete Byte kommt erst nach einer **Zeichenzeit** (9600 Bd, 10 Bit)
 *   im Empfänger der SIO an (Vorbild K7637::service).
 *
 * Zwei Feinheiten aus der Firmware, auf die das Boot-ROM baut:
 * - `DC1` kommt **nur auf Befehl**, nicht beim Einschalten (@ref powerOn).
 * - `ESC c` springt nach 000EH, **hinter** das `CLR 60H` des Einschaltens, und
 *   löscht dort 60H bis auf Bit3/Bit4 (`AND 60H,#18H`) — ein vorher gesendetes
 *   `DC3` ist danach aufgehoben.  Das ROM schickt vor dem KEY-Test `DC3`.
 *
 * **DCP-Modus** (`ESC [?22h`, PC/XT-Scancodes Satz 1, Firmware 0320H–035FH): je
 * Taste Drücken = Code, Loslassen = Code | 80H; Umschalt (2AH/36H), Strg (1DH) und
 * Feststell (3AH) sind eigene Tasten.  Die Zuordnung Host-Zeichen → Taste folgt der
 * **Tastentabelle des BIOS SCPX 8915 V5.3** (DC1CH Grundbelegung, DCA5H Umschaltung,
 * DCD5H/DCC2H Ziffernblock, DC0FH Strg) — also der DIN-Belegung, die der Rechner selbst
 * annimmt: ein Zeichen, das auf der K7672 Umschalt braucht, bekommt es; eines, das keine
 * braucht, wird ohne gesendet, auch wenn der Host Umschalt hält (@ref tasteFuer).
 * Kleinbuchstaben ohne, Großbuchstaben mit Umschalt — vorausgesetzt, die Feststelltaste
 * (3AH, im BIOS ein Wechselschalter) ist nicht eingerastet; ihren Zustand kennt das
 * Modell nicht.
 * Cursortasten = Umschalt + Ziffernblock (BIOS: `^H ^X ^D ^E`).  Tastenwiederholung
 * macht die echte K7672 selbst (`ESC [?19h`); hier nicht nachgebildet.
 *
 * @see doc/design/16_k8915.md §3.5, §8a AP-E2
 */

#pragma once
#include "core/primitives/z80_sio.h"
#include <cstdint>
#include <deque>
#include <string>

class K7672 {
public:
    enum class Modus : uint8_t { Scp, Dcp };

    /// 10 Bit (Start, 8 Daten, Stopp) bei 9600 Bd, gemessen in Takten der
    /// K8915-CPU (2,4576 MHz) — 256 Takte je Bit.
    static constexpr uint64_t ZEICHEN_TAKTE = 10 * 256;
    /**
     * @brief Dauer des Tastatur-Selbsttests bis zum `DC1`, in CPU-Takten.
     *
     * **Annahme** (≈ 50 ms): der Test liegt im nicht auswertbaren Teil D3 der
     * Firmware (README „Offene Punkte“ 1).  Das Boot-ROM wartet ≈ 0,85 Mio. Takte
     * (≈ 0,35 s) auf die Antwort — jede Dauer darunter besteht.
     */
    static constexpr uint64_t SELBSTTEST_TAKTE = 122'880;

    K7672() = default;

    /** @brief An einen SIO-Kanal anschließen (@p kanal 0 = A, 1 = B). */
    void connect(Z80SIO& sio, int kanal);

    /** @brief Netz-Ein der Tastatur: Einschaltzustand, Selbsttest, **kein** `DC1`. */
    void powerOn();

    /**
     * @brief Je Befehl aus der Laufschleife: fällige Bytes zustellen, Bytes des
     *        Rechners abholen und auswerten.
     * @return true, wenn ein Byte zugestellt oder abgeholt wurde (Kette neu bewerten).
     */
    bool service(uint64_t now_cycles);

    /**
     * @brief Taste gedrückt (Host-Tastencode wie bei K7637: druckbares ASCII direkt,
     *        Sondertasten als Qt::Key_*).  Im SCP-Modus ein Zeichen, gesperrt bei `DC3`.
     */
    void keyPress(uint32_t qt_keycode, bool shift, bool ctrl);
    void keyRelease(uint32_t qt_keycode);

    /** @brief Ein fertiges Zeichen senden, als hätte die Tastatur es erzeugt (Tests,
     *         Werkzeuge).  Unterliegt `DC3` wie eine Taste. */
    void sendeZeichen(uint8_t ch);

    /** @brief Welches Zeichen sendet der SCP-Modus für diesen Host-Tastencode? (0 = keins) */
    static uint8_t zeichenFuer(uint32_t qt_keycode, bool shift, bool ctrl);

    /// Eine Taste im DCP-Modus: Scancode (Satz 1) und ob Umschalt/Strg dazugehört.
    struct DcpTaste { uint8_t code = 0; bool umschalt = false; bool strg = false; };
    /**
     * @brief Welche Taste erzeugt unter dem BIOS SCPX 8915 V5.3 das Zeichen @p ch?
     * @return false, wenn es keine gibt (dann sendet der DCP-Modus nichts).
     */
    static bool tasteFuer(uint8_t ch, DcpTaste& t);

    // ─── Zustand ─────────────────────────────────────────────────────────────
    Modus    modus() const          { return modus_; }
    bool     sendenGesperrt() const { return gesperrt_; }
    unsigned summerZaehler() const  { return summer_; }   ///< empfangene `BEL`
    unsigned selbsttests() const    { return selbsttests_; }
    /// Liegen noch Bytes auf der Leitung zum Rechner?  (Tests: „alles getippt“)
    bool sendetNoch() const         { return !unterwegs_.empty(); }

private:
    void empfangeVomRechner(uint8_t b);
    void befehl(const std::string& folge);
    void neustart(bool mit_test);
    void selbsttest();                ///< DC1 nach SELBSTTEST_TAKTE (Firmware 06B6H)
    void sende(uint8_t b, uint64_t ab = 0);
    void tasteDcp(uint32_t qt_keycode, bool gedrueckt, bool shift, bool ctrl);
    void sendeCode(uint8_t b);        ///< Scancode senden (gesperrt bei DC3)
    /// Drücken samt Anpassung von Umschalt/Strg; merkt sich, was beim Loslassen zu tun ist.
    void dcpDruecken(uint32_t schluessel, const DcpTaste& t);
    void dcpLoslassen(uint32_t schluessel);

    Z80SIO* sio_   = nullptr;
    int     kanal_ = 1;

    Modus    modus_    = Modus::Scp;
    bool     gesperrt_ = false;       ///< Firmware 60H Bit2 (DC3)
    unsigned summer_   = 0;
    unsigned selbsttests_ = 0;

    // DCP-Modus: was der Rechner über die Umschalttasten weiß, und je gedrückter
    // Host-Taste, was beim Loslassen zurückzunehmen ist.
    bool umschalt_unten_ = false;     ///< 2AH gesendet, AAH noch nicht
    bool strg_unten_     = false;     ///< 1DH gesendet, 9DH noch nicht
    struct Gedrueckt { uint32_t schluessel; uint8_t code; int umschalt; int strg; };
    std::deque<Gedrueckt> gedrueckt_;

    bool        esc_aktiv_ = false;
    std::string esc_folge_;           ///< Zeichen nach ESC

    uint64_t jetzt_ = 0;
    uint64_t leitung_frei_ = 0;
    struct Byte { uint64_t faellig; uint8_t wert; };
    std::deque<Byte> unterwegs_;
};
