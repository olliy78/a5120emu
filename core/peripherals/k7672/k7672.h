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
 * Cursortasten = Umschalt + Ziffernblock (BIOS: `^H ^X ^D ^E`).
 *
 * **Tastenwiederholung** (AP-E4g) macht die Tastatur selbst, in beiden Modi, und
 * die Firmware belegt ihre Regeln (D2, Hauptschleife 0079H–0178H, Tastendruck 01D4H):
 * - **Welche Tasten:** Bit 7 der Tastenart (D3 0100H, Register 0DH; 01E0H–01E8H setzt
 *   dann FLAGS Bit 1).  Buchstaben, Leertaste, Cursor, Return, PF1–PF12, Ziffernblock …
 *   wiederholen; Umschalt, Strg, Feststell, ALT, `CL`, Tab, BREAK, CLEAR, RESET, ^S, MOD2
 *   und — auffällig **[?]** — die Ziffern 1 3 5 7 9 und ß nicht (@ref wiederholbar; deren
 *   Tastenart 31H/33H/…/7EH ist wie die der anderen ESC-Folgen-Tasten ein Zeichen, kein
 *   Typ mit Bit 7).  Am Gerät nachprüfen.
 * - **Nur die zuletzt gedrückte Taste** (22H); eine neue Taste beginnt von vorn
 *   (01E8H lädt den Zähler 2AH), Loslassen beendet (055AH).
 * - **Zeit:** Zähler 2AH zählt je ABTASTDURCHLAUF der Matrix (015CH–0173H) von A0H
 *   (= 160) herunter, danach alle 12H (= 18) Durchläufe ein Mal (019BH).  Er steht
 *   still, solange `DC3` gilt (0168H).  Die Dauer eines Durchlaufs ist NICHT aus der
 *   Firmware ablesbar, sondern gerechnet (@ref ABTASTDURCHLAUF_TAKTE).
 * - **Was:** im SCP-Modus das letzte Zeichen noch einmal (0195H `LD SIO,0EH`), im
 *   DCP-Modus der Drücken-Code noch einmal (0190H → 0320H) — ohne Loslassen dazwischen.
 * - **`ESC [?19h`/`?20h`/`?21h` sind KEINE Wiederholungsstufen** (README und Entwurf
 *   vermuteten das): 03ECH/03F4H/03FDH setzen Register 2DH, das bei 0263H–0270H die
 *   Seite der Zeichentabelle wählt (r8 = 3 + 2DH).  Mit der Wiederholung hat 2DH nichts
 *   zu tun; das Modell wertet die Folgen nicht aus.
 *
 * **Tasten der Nachbildung** (Bildschirmtastatur, AP-UI1): `QK_TASTE_BASE | Matrix`
 * spricht eine PHYSISCHE Taste an (Matrixposition 00H–7FH, Firmware-Register 22H).
 * Was sie sendet, steht in den Tabellen der Firmware (EPROM D3): im DCP-Modus der
 * Scancode aus D3 0080H — Bit 7 dort heißt „mit Vorsatz“: Umschalt (2AH) oder Strg
 * (1DH) je nach Bit 6 der Tastenart (D3 0100H), **kein** `E0` (Firmware 0326H–035FH,
 * 05C2H) —, im SCP-Modus das Zeichen aus D3 0400H (grund) / 0480H (umgeschaltet).
 * Tasten ohne Eintrag (FFH) senden nichts; `CL` (7DH) schaltet in der Firmware nur
 * den Tastenklick um (030BH) und sendet ebenfalls nichts.  Die Umschalt-/Strg-Flags
 * des Aufrufs gelten als gehaltene Taste (Umschalt bzw. Strg davor und danach).
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

    /// Zähler 2AH bei neuer Taste (Firmware 01E8H: `LD 2AH,#A0H`); abgelaufen nach
    /// A0H Durchläufen mit Zählschritt plus dem Durchlauf, der sendet.
    static constexpr unsigned WDH_VERZOEGERUNG_DURCHLAEUFE = 0xA0 + 1;
    /// Zähler 2AH nach dem Senden (019BH: `LD 2AH,#12H`) — 0173H zieht im selben
    /// Durchlauf schon 1 ab, also senden alle 12H Durchläufe.
    static constexpr unsigned WDH_FOLGE_DURCHLAEUFE = 0x12;
    /**
     * @brief Dauer EINES Matrix-Abtastdurchlaufs der Firmware in CPU-Takten (2,4576 MHz).
     *
     * **[?] gerechnet, nicht gemessen:** Z8-Takt 2,4576 MHz = Quarz, Befehlstakt
     * Quarz/2 (Z8-UART: 2 457 600/256 = 9600 Bd, README); die Abtastung ist 8 Zeilen ×
     * 16 Spalten × (RL, RLC, JR, DJNZ ≈ 34 Takte) plus Zeilenrahmen und Listenvergleich
     * ≈ 6 800 Befehlstakte ≈ 5,5 ms.  Daraus ≈ 0,9 s bis zur ersten und ≈ 10 Hz danach.
     * Messauftrag am Gerät: Zeit bis zur ersten Wiederholung und Abstand danach.
     */
    static constexpr uint64_t ABTASTDURCHLAUF_TAKTE = 13'500;

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

    /// Kennung einer physischen Taste (Matrixposition), siehe Klassenkopf.
    static constexpr uint32_t QK_TASTE_BASE = 0x03000000;
    /// Matrixposition der Feststelltaste (Firmware 0303H: schaltet LED 21H Bit 7).
    static constexpr uint8_t MATRIX_FESTSTELL = 0x26;
    /// Matrixposition von `CL` (Firmware 030BH: nur Tastenklick um, kein Code).
    static constexpr uint8_t MATRIX_KLICK = 0x7D;
    /// Matrixposition von `GRAPH` (SCP: schaltet 60H Bit 5 + LED 21H Bit 6 [?]).
    static constexpr uint8_t MATRIX_GRAPH = 0x1A;
    /// Scancode der Taste an Matrixposition @p m aus der Firmware (FFH = keiner;
    /// Bit 7 = mit Vorsatz, s. @ref vorsatzUmschalt).
    static uint8_t scancode(uint8_t m);
    /// Vorsatz einer Taste mit Bit 7 im Scancode: true = Umschalt (2AH), false = Strg (1DH).
    static bool vorsatzUmschalt(uint8_t m);
    /// SCP-Zeichen der Taste @p m (FFH = keins).
    static uint8_t scpZeichen(uint8_t m, bool umschalt);
    /// Wiederholt die Taste an Matrixposition @p m (Bit 7 der Tastenart, D3 0100H)?
    static bool wiederholbar(uint8_t m);
    /// Dasselbe für eine Taste, die nur als DCP-Scancode (ohne Bit 7) bekannt ist.
    static bool wiederholbarScancode(uint8_t code);
    /// Dasselbe für ein SCP-Zeichen (Host-Zeichen, auch Strg-Zeichen).
    static bool wiederholbarZeichen(uint8_t z);

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
    /**
     * @brief Anzeige-LEDs, Abbild des Firmware-Registers 21H (README „Protokoll
     *        Rechner → Tastatur“).  Nachgebildet sind nur zwei Bits:
     *        **Bit 3** = Senden frei (an nach `DC1`, aus nach `DC3` — die
     *        XON/XOFF-Lampe) und **Bit 0** = `ESC [?13h` / `ESC [?13l` **[?]**
     *        (welche Lampe das ist, sagt die Firmware nicht).  Einschalten und
     *        Neustart (`ESC c`, `ESC [2;0y`) löschen das Register mit dem
     *        Speicherlöscher 04H…7FH.
     *        Seit AP-UI1 außerdem **Bit 7** = `ESC [?11h/l` und die Feststelltaste
     *        (Firmware 0467H/0470H, 0303H — die Lampe CAPS) und **Bit 6** =
     *        `ESC [?18h/l` (048BH/0483H — vermutlich GRAPH **[?]**).
     */
    uint8_t  leds() const           { return leds_; }
    unsigned selbsttests() const    { return selbsttests_; }
    /// Läuft gerade eine Tastenwiederholung?  (Tests)
    bool     wiederholtGerade() const { return wdh_.aktiv; }
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
    /// Physische Taste (Matrixposition) drücken/loslassen — Firmware-Tabellen.
    void tasteMatrix(uint8_t m, bool gedrueckt, bool shift, bool ctrl);
    /// Wiederholung für diese Taste beginnen (ersetzt eine laufende) bzw. beenden.
    void wiederholungStart(uint32_t schluessel, std::string bytes);
    void wiederholungEnde(uint32_t schluessel);
    void wiederholungStopp() { wdh_ = Wiederholung{}; }
    void wiederholungTakt(uint64_t dt);

    Z80SIO* sio_   = nullptr;
    int     kanal_ = 1;

    Modus    modus_    = Modus::Scp;
    bool     gesperrt_ = false;       ///< Firmware 60H Bit2 (DC3)
    unsigned summer_   = 0;
    uint8_t  leds_     = 0;           ///< Firmware 21H (nur Bit 0 und Bit 3)
    unsigned selbsttests_ = 0;

    // DCP-Modus: was der Rechner über die Umschalttasten weiß, und je gedrückter
    // Host-Taste, was beim Loslassen zurückzunehmen ist.
    bool umschalt_unten_ = false;     ///< 2AH gesendet, AAH noch nicht
    bool strg_unten_     = false;     ///< 1DH gesendet, 9DH noch nicht
    struct Gedrueckt { uint32_t schluessel; uint8_t code; int umschalt; int strg; };
    std::deque<Gedrueckt> gedrueckt_;
    /// Tasten der Nachbildung: je gedrückter Matrixposition die Bytes des Loslassens.
    struct MatrixGedrueckt { uint8_t m; std::string loslassen; bool umschalt_taste; bool strg_taste; };
    std::deque<MatrixGedrueckt> matrix_gedrueckt_;

    bool        esc_aktiv_ = false;
    std::string esc_folge_;           ///< Zeichen nach ESC

    /// Tastenwiederholung: nur die zuletzt gedrückte Taste (Firmware 22H/2AH).
    struct Wiederholung {
        bool        aktiv = false;
        uint32_t    schluessel = 0;
        std::string bytes;            ///< was je Wiederholung noch einmal gesendet wird
        int64_t     rest = 0;         ///< Takte bis zur nächsten Wiederholung
    } wdh_;

    uint64_t jetzt_ = 0;
    uint64_t leitung_frei_ = 0;
    struct Byte { uint64_t faellig; uint8_t wert; };
    std::deque<Byte> unterwegs_;
};
