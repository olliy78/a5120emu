/**
 * @file test_p8000_terminal_diff.cpp
 * @brief Differenzialtest (AP P20a, Entwurf 28 §8): die Fälle des vereinfachten Kern-Terminals
 *        (test_p8000_terminal.cpp, doc/p8000/hw_terminal.md §3–§6) laufen DURCH DIE ORIGINAL-
 *        FIRMWARE P8T 5.0 — Zeichen vom Rechner über die serielle Leitung (9600 Bd, XON/XOFF
 *        beachtet), Tasten über die K7673 (Matrix).  Jeder Fall prüft die Erwartung am Original
 *        und vergleicht mit dem Kern-Terminal; Abweichungen stehen beim Fall
 *        („ABWEICHUNG": das Original gewinnt, auch gegen das Handbuch).
 */
#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/peripherals/p8000_terminal_hw/terminal_einheit.h"

using namespace k1520::p8000;
using T = TerminalTaste;

namespace {

constexpr uint64_t MS = P8000TerminalHw::Z8_HZ / 1000;

/// Originalterminal am Prüfstand: Rechner sendet mit 9600 Bd und hält bei XOFF an.
struct Orig {
    P8000TerminalEinheit e;
    std::deque<uint8_t> host;
    bool xoff = false;
    std::string antwort;          ///< vom Terminal gesendet (ohne 00H, ohne XON/XOFF)
    int xoffs = 0, xons = 0;

    void abholen() {
        auto& hw = e.hw();
        while (hw.hatAusgabe()) {
            const TerminalSendung s = hw.holeAusgabe();
            if (s.brk || s.byte == 0) continue;
            if (s.byte == 0x13) { xoff = true; ++xoffs; continue; }
            if (s.byte == 0x11) { xoff = false; ++xons; continue; }
            antwort += char(s.byte);
        }
    }
    /// Zeichen senden (mit XON/XOFF) und danach @p nachMs laufen lassen.
    void eingabe(const std::string& s, uint64_t nachMs = 30) {
        for (unsigned char c : s) host.push_back(c);
        auto& hw = e.hw();
        uint64_t sicherung = 0;
        while (!host.empty() && ++sicherung < 2'000'000) {
            if (!xoff && hw.hostLeitungFrei() <= e.takte()) { hw.hostByte(host.front()); host.pop_front(); }
            e.laufe(P8000TerminalHw::BIT_TAKTE);
            abholen();
        }
        e.laufeMs(nachMs);
        EXPECT_TRUE(e.ruheAbwarten()) << "Firmware ruht nicht";
        abholen();
    }
    void taste(T t) { ASSERT_TRUE(e.taste(t)) << int(t); e.tastenAbwarten(); e.ruheAbwarten(); abholen(); }
    void zeichen(uint8_t c, bool ctrl = false) {
        ASSERT_TRUE(e.zeichenTaste(c, ctrl)) << int(c);
        e.tastenAbwarten();
        e.ruheAbwarten();
        abholen();
    }
    std::string nimm() { std::string s = antwort; antwort.clear(); return s; }
    std::string zl(int z) const {
        std::string s = e.hw().text(z);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
    int zeile() const { return e.hw().cursorZeile(); }
    int spalte() const { return e.hw().cursorSpalte(); }
};

/// Eingeschaltetes Original (AA von der Tastatur, Meldung steht) — einmal gebaut, dann kopiert.
const std::vector<uint8_t>& grundzustand() {
    static std::vector<uint8_t> st;
    if (st.empty()) {
        P8000TerminalEinheit e;
        e.laufeMs(1000);
        e.serialize(st);
    }
    return st;
}

void laden(Orig& o) {
    const auto& st = grundzustand();
    const uint8_t* p = st.data();
    ASSERT_TRUE(o.e.deserialize(p, st.data() + st.size()));
}

/// Wie `frisch()` im Kern-Terminal-Test: leeres Bild, Cursor oben links.
void frisch(Orig& o, TerminalModus m = TerminalModus::ADM31) {
    laden(o);
    if (m == TerminalModus::VT100) {
        o.taste(T::MODE);
        o.eingabe("\x1b[2J\x1b[H");
    } else {
        o.eingabe("\x1b*\x1e");
    }
    o.nimm();
}
void vt(Orig& o) { frisch(o, TerminalModus::VT100); }

Terminal kern(TerminalModus m = TerminalModus::ADM31) {
    Terminal t;
    if (m == TerminalModus::VT100) { t.taste(T::MODE); t.eingabe("\x1b[2J\x1b[H"); }
    else t.eingabe("\x1b*\x1e");
    return t;
}

std::string kzl(const Terminal& t, int z) {
    std::string s = t.text(z);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

#define CURSOR(o, z, s) do { EXPECT_EQ((o).zeile(), (z)); EXPECT_EQ((o).spalte(), (s)); } while (0)

/// Bild beider Terminals vergleichen (Zeilen und Cursor).  Rückgabe: Anzahl abweichender Zeilen.
int unterschiede(const Orig& o, const Terminal& k, bool cursorAuch = true) {
    int n = 0;
    for (int z = 0; z < 24; ++z) n += o.zl(z) != kzl(k, z);
    if (cursorAuch) n += (o.zeile() != k.zeile() || o.spalte() != k.spalte());
    return n;
}

void fuelleZeilen(Orig& o, TerminalModus m) {
    std::string s;
    for (int z = 0; z < 24; ++z) {
        if (m == TerminalModus::ADM31) s += "\x1b=" + std::string(1, char(0x20 + z)) + " ";
        else s += "\x1b[" + std::to_string(z + 1) + ";1H";
        s += "Z" + std::to_string(z);
    }
    o.eingabe(s);
}
void fuelleZeilen(Terminal& t) {
    for (int z = 0; z < 24; ++z) {
        if (t.modus() == TerminalModus::ADM31) t.eingabe("\x1b=" + std::string(1, char(0x20 + z)) + " ");
        else t.eingabe("\x1b[" + std::to_string(z + 1) + ";1H");
        t.eingabe("Z" + std::to_string(z));
    }
}

}  // namespace

// ═════ Einschalten / Betriebsarten (§1) ════════════════════════════════════

TEST(P8000TerminalDiff, EinschaltmeldungAdm31)
{
    Orig o;
    laden(o);
    // ABWEICHUNG: das Original zeigt „ADM31/9600 baud/Video Attr. on (c)zft/keaw".
    EXPECT_EQ(o.zl(0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
    CURSOR(o, 1, 0);
}

TEST(P8000TerminalDiff, ModeTasteSchaltetUmUndInitialisiertNeu)
{
    Orig o;
    laden(o);
    o.eingabe("hallo");
    o.taste(T::MODE);
    EXPECT_EQ(o.zl(0), "VT100/9600 baud/Video Attr. on (c)zft/keaw");
    EXPECT_EQ(o.zl(1), "");
    CURSOR(o, 1, 0);
    EXPECT_EQ(o.nimm(), "");   // kein Zeichen zum Host
    o.taste(T::MODE);
    EXPECT_EQ(o.zl(0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
}

TEST(P8000TerminalDiff, VideoTasteSchaltetAttributeAus)
{
    Orig o;
    laden(o);
    o.taste(T::VIDEO);
    EXPECT_EQ(o.zl(0), "ADM31/9600 baud/Video Attr. off (c)zft/keaw");
    o.eingabe("\x1b" "G4x");   // Attributsequenz wird verschluckt, belegt keine Position
    EXPECT_FALSE(o.e.hw().zelle(1, 0).feld);
    EXPECT_EQ(o.zl(1), "x");
    o.taste(T::VIDEO);
    EXPECT_EQ(o.zl(0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
}

TEST(P8000TerminalDiff, ZeichensatzUmschaltungSiSo)
{
    Orig o;
    frisch(o);
    o.eingabe("a");
    o.taste(T::SI_SO);
    EXPECT_TRUE(o.e.hw().zeichensatz2());
    o.eingabe("b");
    EXPECT_EQ(o.nimm(), "");
    // ABWEICHUNG (Kern: Zeichensatz je Zeichen): das RS-Flipflop wählt das 2716 für das GANZE
    // Bild — auch das vorher geschriebene „a" erscheint aus Zeichensatz 2.
    EXPECT_TRUE(o.e.hw().zelle(0, 0).zg2);
    EXPECT_TRUE(o.e.hw().zelle(0, 1).zg2);
    // ABWEICHUNG (Kern: <MODE> wählt wieder ZG1): MODE lässt den Zeichensatz stehen
    // (TOUT1 schreibt nur Meldung + Bild neu, kein Strobe 4000H).
    o.taste(T::MODE);
    EXPECT_TRUE(o.e.hw().zeichensatz2());
    o.taste(T::SI_SO);
    EXPECT_FALSE(o.e.hw().zeichensatz2());
}

TEST(P8000TerminalDiff, OnOffLineLokaleAnzeige)
{
    Orig o;
    frisch(o);
    o.taste(T::ON_OFF);
    o.zeichen('q');
    o.taste(T::CR);
    EXPECT_EQ(o.nimm(), "");
    EXPECT_EQ(o.zl(0), "q");
    o.taste(T::ON_OFF);
    o.zeichen('q');
    EXPECT_EQ(o.nimm(), "q");
}

// ═════ Tastatur §3 (über die K7673-Matrix) ═════════════════════════════════

TEST(P8000TerminalDiff, TastaturZeichenUndDel)
{
    Orig o;
    frisch(o);
    o.zeichen('A');
    o.zeichen('5');
    o.taste(T::DEL);
    EXPECT_EQ(o.nimm(), std::string("A5\x7F"));
}

TEST(P8000TerminalDiff, TastaturCapsLockNurBuchstaben)
{
    Orig o;
    frisch(o);
    o.e.matrixTaste(o.e.positionFuer({0x3A}));   // CAPS LOCK (rastet in der Firmware)
    o.e.tastenAbwarten();
    o.zeichen('a');
    o.zeichen('1');
    EXPECT_EQ(o.nimm(), "A1");
    EXPECT_EQ(o.e.tastatur().leds() & 2, 2);     // LED P2.5 der Tastatur
}

TEST(P8000TerminalDiff, TastaturCtrlTabelle45)
{
    Orig o;
    frisch(o);
    // ABWEICHUNG (Kern: CTRL-@ sendet 00H): SIO_OUT unterdrückt NUL — es geht nichts hinaus.
    o.zeichen('@', true);
    EXPECT_EQ(o.nimm(), "");
    for (char c : std::string("AGMZ")) {
        o.zeichen(uint8_t(c), true);
        o.zeichen(uint8_t(c + 32), true);
        EXPECT_EQ(o.nimm(), std::string(2, char(c - 'A' + 1))) << c;
    }
    o.zeichen('[', true);
    o.zeichen('\\', true);
    o.zeichen(']', true);
    o.zeichen('^', true);
    o.zeichen('_', true);
    EXPECT_EQ(o.nimm(), "\x1b\x1c\x1d\x1e\x1f");
}

namespace {
struct Zeile36 { TerminalTaste taste; const char* adm; const char* vt; };
}

TEST(P8000TerminalDiff, TastaturSteuerzeichenTasten_Tab436)
{
    // W2 entschieden: <FF> (Cursor rechts) sendet 0CH (Tab. 4.3-6), nicht 09H der Referenzkarte.
    // NL hat die K7673.09 nicht (keine Taste mit dieser Wirkung).
    const Zeile36 tab[] = {
        {T::VT, "\x0B", "\x1b[A"}, {T::LF, "\x0A", "\x1b[B"}, {T::FF, "\x0C", "\x1b[C"},
        {T::BS, "\x08", "\x1b[D"}, {T::HOME, "\x1E", "\x1b[H"},
        {T::HT, "\x09", "\x09"}, {T::CR, "\x0D", "\x0D"}, {T::ESC, "\x1b", "\x1b"},
    };
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Orig o;
        frisch(o, m);
        for (const auto& z : tab) {
            o.taste(z.taste);
            EXPECT_EQ(o.nimm(), m == TerminalModus::ADM31 ? z.adm : z.vt) << int(z.taste);
        }
    }
    EXPECT_FALSE(Orig().e.positionFuer(T::NL).gueltig());
}

TEST(P8000TerminalDiff, TastaturFunktionstasten_Tab437)
{
    // W3 entschieden: <LINE DELETE> = ESC R.  ABWEICHUNG: die K7673.09 hat KEINE eigene TAB-
    // Funktionstaste — TAB ist die HT-Taste (09H auch im VT100-Betrieb, nicht ESC [ I) — und
    // keine LINE-ERASE-Taste.
    const Zeile36 tab[] = {
        {T::PAGE_ERASE, "\x1bY", "\x1b[J"}, {T::LINE_INSERT, "\x1b" "E", "\x1b[L"},
        {T::CHAR_INSERT, "\x1bQ", "\x1b[@"}, {T::LINE_DELETE, "\x1bR", "\x1b[M"},
        {T::CHAR_DELETE, "\x1bW", "\x1b[P"}, {T::TAB, "\x09", "\x09"}, {T::BACKTAB, "\x1bI", "\x1b[Z"},
    };
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Orig o;
        frisch(o, m);
        for (const auto& z : tab) {
            o.taste(z.taste);
            EXPECT_EQ(o.nimm(), m == TerminalModus::ADM31 ? z.adm : z.vt) << int(z.taste);
        }
    }
    EXPECT_FALSE(Orig().e.positionFuer(T::LINE_ERASE).gueltig());
}

TEST(P8000TerminalDiff, BreakTasteSendetNullMitHalberBaudrate)
{
    Orig o;
    frisch(o);
    ASSERT_TRUE(o.e.taste(T::BREAK));
    o.e.tastenAbwarten();
    bool brk = false;
    auto& hw = o.e.hw();
    while (hw.hatAusgabe()) {
        const TerminalSendung s = hw.holeAusgabe();
        if (s.brk) { brk = true; EXPECT_EQ(s.byte, 0); EXPECT_EQ(s.dauer, 2 * 11 * P8000TerminalHw::BIT_TAKTE); }
        else EXPECT_EQ(s.byte, 0) << "nur das Break-Zeichen";
    }
    EXPECT_TRUE(brk);
}

// ═════ Steuerzeichen §4 ════════════════════════════════════════════════════

TEST(P8000TerminalDiff, SichtbareZeichenUndZeilenumbruch)
{
    Orig o;
    frisch(o);
    o.eingabe(std::string(80, 'x') + "y");
    EXPECT_EQ(o.zl(0), std::string(80, 'x'));   // Spalte 80 ist beschreibbar (das 80H weicht)
    EXPECT_EQ(o.zl(1), "y");
    CURSOR(o, 1, 1);
    Terminal k = kern();
    k.eingabe(std::string(80, 'x') + "y");
    EXPECT_EQ(unterschiede(o, k), 0);
}

TEST(P8000TerminalDiff, RollenNachDer24Zeile)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b=" "7 " "unten" + std::string(75, '.'));
    EXPECT_EQ(o.zl(22).substr(0, 5), "unten");
    EXPECT_EQ(o.zl(23), "");
    CURSOR(o, 23, 0);
    // Rollen = Zeilentabelle drehen (nicht Speicher verschieben)
    EXPECT_EQ(o.e.hw().zeilenAdresse(0), 0x1050);
    EXPECT_EQ(o.e.hw().zeilenAdresse(23), 0x1000);
}

TEST(P8000TerminalDiff, Bel)
{
    Orig o;
    frisch(o);
    o.eingabe("\x07");
    EXPECT_EQ(o.e.hw().klingel(), 1u);
    CURSOR(o, 0, 0);
    Orig v;
    vt(v);
    const unsigned k0 = v.e.hw().klingel();
    v.eingabe("\x07");
    EXPECT_EQ(v.e.hw().klingel(), k0 + 1);
}

TEST(P8000TerminalDiff, BsAdm31BisSchirmanfangMitUmbruch)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b=" "\x21 " "\b");
    CURSOR(o, 0, 79);
    o.eingabe("\x1e\b");
    CURSOR(o, 0, 0);
}

TEST(P8000TerminalDiff, BsVt100BisZeilenanfang)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[2;1H\b");
    CURSOR(o, 1, 0);
    o.eingabe("ab\b");
    CURSOR(o, 1, 1);
}

TEST(P8000TerminalDiff, HtAdm31NaechsterTabUmbruchUndRollen)
{
    Orig o;
    frisch(o);
    o.eingabe("\t");
    CURSOR(o, 0, 8);
    o.eingabe("ab\t");
    CURSOR(o, 0, 16);
    o.eingabe("\x1b=" "\x20o\t");
    CURSOR(o, 1, 0);
    o.eingabe("\x1b=" "7o\t");
    CURSOR(o, 23, 0);
}

TEST(P8000TerminalDiff, HtVt100BisZeilenende)
{
    Orig o;
    vt(o);
    o.eingabe("\t");
    CURSOR(o, 0, 8);
    o.eingabe("\x1b[1;79H\t\t");
    CURSOR(o, 0, 79);
}

TEST(P8000TerminalDiff, LfAbwaertsSpalteBleibtUndRollt)
{
    Orig o;
    frisch(o);
    o.eingabe("ab\n");
    CURSOR(o, 1, 2);
    fuelleZeilen(o, TerminalModus::ADM31);
    o.eingabe("\x1b=" "7 \n");
    EXPECT_EQ(o.zl(0), "Z1");
    EXPECT_EQ(o.zl(22), "Z23");
    EXPECT_EQ(o.zl(23), "");
}

TEST(P8000TerminalDiff, VtAufwaertsEndetInZeile1)
{
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Orig o;
        frisch(o, m);
        o.eingabe("\n\n\x0b");
        CURSOR(o, 1, 0);
        o.eingabe("\x0b\x0b\x0b");
        CURSOR(o, 0, 0);
    }
}

TEST(P8000TerminalDiff, FfAdm31EinRechtsMitUmbruch)
{
    Orig o;
    frisch(o);
    o.eingabe("\x0c\x0c");
    CURSOR(o, 0, 2);
    o.eingabe("\x1b=" "\x20n\x0c");
    CURSOR(o, 0, 79);
    o.eingabe("\x0c");
    CURSOR(o, 1, 0);
}

TEST(P8000TerminalDiff, FfVt100BisZeilenende)
{
    Orig o;
    vt(o);
    o.eingabe(std::string(100, '\x0c'));
    CURSOR(o, 0, 79);
}

TEST(P8000TerminalDiff, CrUndRsHome)
{
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Orig o;
        frisch(o, m);
        o.eingabe("abc\n\r");
        CURSOR(o, 1, 0);
        o.eingabe("abc\n\x1e");
        CURSOR(o, 0, 0);
    }
}

TEST(P8000TerminalDiff, SteuerzeichenSindUnsichtbarProgrammModeZeigtAlle)
{
    Orig o;
    frisch(o);
    o.eingabe("\x01\x02" "a");
    EXPECT_EQ(o.zl(0), "a");
    o.eingabe("\x1b" "U\x01\x1f" "b");
    EXPECT_EQ(o.e.hw().bwsByte(0, 1), 0x01);
    EXPECT_EQ(o.e.hw().bwsByte(0, 2), 0x1f);
    // ABWEICHUNG (Kern: ESC u wirkt unsichtbar): im Programm-Mode wird ESC und das folgende
    // Zeichen ANGEZEIGT und wirkt trotzdem (Programm-Mode aus) — danach ist 01H wieder unsichtbar.
    o.eingabe("\x1b" "u" "\x01");
    EXPECT_EQ(o.e.hw().bwsByte(0, 4), 0x1b);
    EXPECT_EQ(o.e.hw().bwsByte(0, 5), 'u');
    CURSOR(o, 0, 6);
}

// ═════ ADM31-Sequenzen §5 ══════════════════════════════════════════════════

TEST(P8000TerminalDiff, Adm31_CBT_ESC_I)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b=" "\x20" "3\x1b" "I");
    CURSOR(o, 0, 16);
    o.eingabe("\x1b" "I");
    CURSOR(o, 0, 8);
    o.eingabe("\x1b" "I\x1b" "I\x1b" "I");
    CURSOR(o, 0, 0);
}

TEST(P8000TerminalDiff, Adm31_CHT_ESC_i)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b" "i");
    CURSOR(o, 0, 8);
    // ABWEICHUNG (Kern: endet am Bildschirmende): ESC i in Spalte 80 der letzten Zeile bricht wie
    // HT in die nächste Zeile um und rollt.
    o.eingabe("oben");
    o.eingabe("\x1b=" "7o\x1b" "i");
    CURSOR(o, 23, 0);
    EXPECT_EQ(o.zl(0), "");          // „oben" ist hinausgerollt
    o.eingabe("\x1b" "i");
    CURSOR(o, 23, 8);
}

TEST(P8000TerminalDiff, Adm31_CDE_ESC_W)
{
    Orig o;
    frisch(o);
    o.eingabe("abcdef\x1b=" "\x20\x22\x1b" "W");
    EXPECT_EQ(o.zl(0), "abdef");
    CURSOR(o, 0, 2);
}

TEST(P8000TerminalDiff, Adm31_CIN_ESC_Q)
{
    Orig o;
    frisch(o);
    o.eingabe("abcdef\x1b=" "\x20\x22\x1b" "Q");
    EXPECT_EQ(o.zl(0), "ab cdef");
    o.eingabe("\x1b=" "\x20\x20" + std::string(77, 'z'));
    o.eingabe("\x1b=" "\x20\x20\x1b" "Q");
    EXPECT_EQ(o.e.hw().text(0)[0], ' ');
    EXPECT_EQ(o.e.hw().text(0).size(), 80u);
}

TEST(P8000TerminalDiff, Adm31_HVP_ESC_Gleich)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b=" "\x20\x20");
    CURSOR(o, 0, 0);
    o.eingabe("\x1b=" "7o");
    CURSOR(o, 23, 79);
    o.eingabe("\x1b=" "~~");
    CURSOR(o, 23, 79);                     // zu groß → letzte Position
    // ABWEICHUNG (Kern: 7FH als Koordinate): der Empfang verwirft DEL (IRP30) — „ESC = DEL DEL"
    // wartet danach noch auf zwei Zeichen.
    o.eingabe("\x1b=" "\x7f\x7f");
    CURSOR(o, 23, 79);
    o.eingabe("  ");
    CURSOR(o, 0, 0);
    o.eingabe("\x1b=" "  ");
    CURSOR(o, 0, 0);
    o.eingabe("\x1b=" "\x30\x40");
    CURSOR(o, 16, 32);
}

TEST(P8000TerminalDiff, Adm31_LDE_ESC_R)
{
    Orig o;
    frisch(o);
    fuelleZeilen(o, TerminalModus::ADM31);
    o.eingabe("\x1b=" "\"\x28\x1b" "R");
    EXPECT_EQ(o.zl(2), "Z3");
    EXPECT_EQ(o.zl(23), "");
    CURSOR(o, 2, 0);
}

TEST(P8000TerminalDiff, Adm31_LER_ESC_T)
{
    Orig o;
    frisch(o);
    o.eingabe("abcdef\x1b=" "\x20\x22\x1b" "T");
    EXPECT_EQ(o.zl(0), "ab");
    CURSOR(o, 0, 2);
}

TEST(P8000TerminalDiff, Adm31_LIN_ESC_E)
{
    Orig o;
    frisch(o);
    fuelleZeilen(o, TerminalModus::ADM31);
    o.eingabe("\x1b=" "\"\x28\x1b" "E");
    EXPECT_EQ(o.zl(2), "");
    EXPECT_EQ(o.zl(3), "Z2");
    EXPECT_EQ(o.zl(23), "Z22");
    CURSOR(o, 2, 0);
}

TEST(P8000TerminalDiff, Adm31_PER_ESC_Y)
{
    Orig o;
    frisch(o);
    fuelleZeilen(o, TerminalModus::ADM31);
    o.eingabe("\x1b=" "\"\x21\x1b" "Y");
    EXPECT_EQ(o.zl(1), "Z1");
    EXPECT_EQ(o.zl(2), "Z");
    EXPECT_EQ(o.zl(3), "");
    CURSOR(o, 2, 1);
}

TEST(P8000TerminalDiff, Adm31_PMN_PMF)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b" "U" "\x01");
    EXPECT_EQ(o.e.hw().bwsByte(0, 0), 0x01);   // Programm-Mode: Steuerzeichen sichtbar
    o.eingabe("\x1b" "X" "\x02");
    CURSOR(o, 0, 3);                           // ESC und X werden im Programm-Mode angezeigt …
    o.eingabe("\x02");
    CURSOR(o, 0, 3);                           // … danach ist er aus: 02H unsichtbar
}

TEST(P8000TerminalDiff, Adm31_SDE_CursorBleibt)
{
    for (const char* seq : {"\x1b*", "\x1b:"}) {
        Orig o;
        frisch(o);
        fuelleZeilen(o, TerminalModus::ADM31);
        o.eingabe("\x1b=" "\x25\x25");
        o.eingabe(seq);
        for (int z = 0; z < 24; ++z) EXPECT_EQ(o.zl(z), "") << z;
        CURSOR(o, 5, 5);
    }
}

TEST(P8000TerminalDiff, Adm31_SGR_AlleParameter_Tab439)
{
    // Rohbytes im BWS (8275-Feldattribut 10URGGBH).  ABWEICHUNG (Kern: Attribut „leer"):
    // G1/G3 schreiben ein LEERZEICHEN statt eines Attributs; G5/G7 nur invers (90H) —
    // „leer" kennt das Original nicht.
    const std::pair<char, int> tab[] = {
        {'0', 0x80}, {'1', 0x20}, {'2', 0x82}, {'3', 0x20}, {'4', 0x90}, {'5', 0x90}, {'6', 0x92}, {'7', 0x90},
    };
    for (auto [p, b] : tab) {
        Orig o;
        frisch(o);
        o.eingabe(std::string("\x1b" "G") + p + "x");
        EXPECT_EQ(o.e.hw().bwsByte(0, 0), b) << p;
        EXPECT_EQ(o.e.hw().zelle(0, 0).feld, (b & 0x80) != 0) << p;
        EXPECT_EQ(o.e.hw().text(0)[0], ' ');
        EXPECT_EQ(o.e.hw().zelle(0, 1).zeichen, 'x');
        // 8275: Attributzelle leer, „x" mit dem Feldattribut
        EXPECT_EQ(o.e.hw().bildZelle(0, 0).empty, (b & 0x80) != 0) << p;
        EXPECT_EQ(o.e.hw().bildZelle(0, 1).rvv, (b & 0x90) == 0x90) << p;
        EXPECT_EQ(o.e.hw().bildZelle(0, 1).blink, (b & 0x82) == 0x82) << p;
    }
}

// ═════ Attribute: Feldregel ════════════════════════════════════════════════

TEST(P8000TerminalDiff, FeldregelAttributWirktNurInnerhalbDerZeile)
{
    Orig o;
    frisch(o);
    o.eingabe("a\x1b" "G4" "bc\n\r" "d");
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 0), 0);
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 3), ATTR_INVERS);
    // Spalte 79 trägt das 80H der Firmware: Feld endet dort (8275: bis zum nächsten Attribut)
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 78), ATTR_INVERS);
    EXPECT_EQ(o.e.hw().wirksamesAttribut(1, 0), 0);
    EXPECT_TRUE(o.e.hw().bildZelle(0, 3).rvv);
    EXPECT_TRUE(o.e.hw().bildZelle(0, 78).rvv);
    EXPECT_FALSE(o.e.hw().bildZelle(1, 0).rvv);
}

TEST(P8000TerminalDiff, FeldAufhebungDurchGleichesAttribut0UndUeberschreiben)
{
    Orig o;
    frisch(o);
    o.eingabe("\x1b" "G2" "ab\x1b" "G0" "cd");
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 2), ATTR_BLINK);
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 4), 0);
    o.eingabe("\x1e" "x");
    EXPECT_FALSE(o.e.hw().zelle(0, 0).feld);
    EXPECT_EQ(o.e.hw().wirksamesAttribut(0, 2), 0);
}

// ═════ VT100-Sequenzen §6 ══════════════════════════════════════════════════

TEST(P8000TerminalDiff, Vt100_CBT)
{
    Orig o;
    vt(o);
    // ABWEICHUNG (Kern/ANSI: voriger Tabstopp 16): die Firmware rechnet (Spalte − 8) & F8H —
    // aus Spalte 20 wird 9, aus 17 (Tabstopp) ebenfalls 9.  ADM31-ESC I rechnet richtig.
    o.eingabe("\x1b[1;20H\x1b[Z");
    CURSOR(o, 0, 8);
    o.eingabe("\x1b[1;17H\x1b[Z");
    CURSOR(o, 0, 8);
    o.eingabe("\x1b[1;20H\x1b[2Z");
    CURSOR(o, 0, 0);
    o.eingabe("\x1b[9Z");
    CURSOR(o, 0, 0);
}

TEST(P8000TerminalDiff, Vt100_CHT)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[I");
    CURSOR(o, 0, 8);
    o.eingabe("\x1b[3I");
    CURSOR(o, 0, 32);
    o.eingabe("\x1b[99I");
    CURSOR(o, 0, 79);
}

TEST(P8000TerminalDiff, Vt100_CUB_CUD_CUF_CUU)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[5;10H");
    CURSOR(o, 4, 9);
    o.eingabe("\x1b[2D");  CURSOR(o, 4, 7);
    o.eingabe("\x1b[D");   CURSOR(o, 4, 6);
    o.eingabe("\x1b[0D");  CURSOR(o, 4, 5);
    o.eingabe("\x1b[99D"); CURSOR(o, 4, 0);
    o.eingabe("\x1b[3B");  CURSOR(o, 7, 0);
    o.eingabe("\x1b[99B"); CURSOR(o, 23, 0);
    o.eingabe("\x1b[4C");  CURSOR(o, 23, 4);
    o.eingabe("\x1b[99C"); CURSOR(o, 23, 79);
    o.eingabe("\x1b[2A");  CURSOR(o, 21, 79);
    o.eingabe("\x1b[99A"); CURSOR(o, 0, 79);
}

TEST(P8000TerminalDiff, Vt100_CUP_HVP)
{
    for (const char* f : {"H", "f"}) {
        Orig o;
        vt(o);
        o.eingabe(std::string("\x1b[7;12") + f);
        CURSOR(o, 6, 11);
        o.eingabe(std::string("\x1b[") + f);
        CURSOR(o, 0, 0);
        o.eingabe(std::string("\x1b[;5") + f);
        CURSOR(o, 0, 4);
        o.eingabe(std::string("\x1b[99;99") + f);
        CURSOR(o, 23, 79);
        o.eingabe(std::string("\x1b[0;0") + f);
        CURSOR(o, 0, 0);
    }
}

TEST(P8000TerminalDiff, Vt100_DCH)
{
    Orig o;
    vt(o);
    o.eingabe("abcdef\x1b[1;2H\x1b[P");
    EXPECT_EQ(o.zl(0), "acdef");
    o.eingabe("\x1b[2P");
    EXPECT_EQ(o.zl(0), "aef");
    CURSOR(o, 0, 1);
}

TEST(P8000TerminalDiff, Vt100_DL)
{
    Orig o;
    vt(o);
    fuelleZeilen(o, TerminalModus::VT100);
    o.eingabe("\x1b[3;5H\x1b[2M");
    EXPECT_EQ(o.zl(1), "Z1");
    EXPECT_EQ(o.zl(2), "Z4");
    EXPECT_EQ(o.zl(22), "");
    EXPECT_EQ(o.zl(23), "");
    CURSOR(o, 2, 4);
}

TEST(P8000TerminalDiff, Vt100_DSR_CursorPositionsReport)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[12;34H\x1b[6n");
    EXPECT_EQ(o.nimm(), "\x1b[12;34R");
    o.eingabe("\x1b[H\x1b[6n");
    EXPECT_EQ(o.nimm(), "\x1b[1;1R");
    o.eingabe("\x1b[5n");
    EXPECT_EQ(o.nimm(), "");
}

TEST(P8000TerminalDiff, Vt100_ED)
{
    for (int mode : {-1, 0, 1, 2}) {
        Orig o;
        vt(o);
        fuelleZeilen(o, TerminalModus::VT100);
        o.eingabe("\x1b[3;2H");
        o.eingabe(mode < 0 ? "\x1b[J" : "\x1b[" + std::to_string(mode) + "J");
        if (mode <= 0) {
            EXPECT_EQ(o.zl(1), "Z1");
            EXPECT_EQ(o.zl(2), "Z");
            EXPECT_EQ(o.zl(3), "");
        } else if (mode == 1) {
            EXPECT_EQ(o.zl(0), "");
            EXPECT_EQ(o.zl(1), "");
            EXPECT_EQ(o.zl(2), "");
            EXPECT_EQ(o.zl(3), "Z3");
        } else {
            for (int z = 0; z < 24; ++z) EXPECT_EQ(o.zl(z), "");
        }
        CURSOR(o, 2, 1);
    }
}

TEST(P8000TerminalDiff, Vt100_EL)
{
    for (int mode : {-1, 0, 1, 2}) {
        Orig o;
        vt(o);
        o.eingabe("abcdef\x1b[1;3H");
        o.eingabe(mode < 0 ? "\x1b[K" : "\x1b[" + std::to_string(mode) + "K");
        if (mode <= 0) EXPECT_EQ(o.zl(0), "ab");
        else if (mode == 1) EXPECT_EQ(o.zl(0), "   def");
        else EXPECT_EQ(o.zl(0), "");
        CURSOR(o, 0, 2);
    }
}

TEST(P8000TerminalDiff, Vt100_ICH)
{
    Orig o;
    vt(o);
    o.eingabe("abcdef\x1b[1;2H\x1b[2@");
    EXPECT_EQ(o.zl(0), "a  bcdef");
    CURSOR(o, 0, 1);
    o.eingabe("\x1b[1;1H" + std::string(80, 'x') + "\x1b[1;1H\x1b[@");
    EXPECT_EQ(o.e.hw().text(0)[0], ' ');
    EXPECT_EQ(o.e.hw().text(0)[79], 'x');
}

TEST(P8000TerminalDiff, Vt100_IL_VorDerCursorzeile_W6)
{
    // W6 entschieden am Original:
    Orig o;
    vt(o);
    fuelleZeilen(o, TerminalModus::VT100);
    o.eingabe("\x1b[3;5H\x1b[2L");
    EXPECT_EQ(o.zl(1), "Z1");
    EXPECT_EQ(o.zl(2), "");
    EXPECT_EQ(o.zl(3), "");
    EXPECT_EQ(o.zl(4), "Z2");
    EXPECT_EQ(o.zl(23), "Z21");
    CURSOR(o, 2, 4);
}

TEST(P8000TerminalDiff, Vt100_IND_ESC_D)
{
    Orig o;
    vt(o);
    fuelleZeilen(o, TerminalModus::VT100);
    o.eingabe("\x1b[5;7H");
    o.eingabe("\x1b" "D");
    CURSOR(o, 5, 6);
    o.eingabe("\x1b[24;3H\x1b" "D");
    EXPECT_EQ(o.zl(0), "Z1");
    EXPECT_EQ(o.zl(23), "");
    CURSOR(o, 23, 2);
}

TEST(P8000TerminalDiff, Vt100_NEL_ESC_E_ohneRollen)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[2;9H\x1b" "E");
    CURSOR(o, 2, 0);
    o.eingabe("\x1b[24;9H\x1b" "E");
    CURSOR(o, 23, 0);
}

TEST(P8000TerminalDiff, Vt100_RI_ESC_M)
{
    Orig o;
    vt(o);
    fuelleZeilen(o, TerminalModus::VT100);
    o.eingabe("\x1b[3;4H\x1b" "M");
    CURSOR(o, 1, 3);
    o.eingabe("\x1b[1;4H\x1b" "M");
    EXPECT_EQ(o.zl(0), "");
    EXPECT_EQ(o.zl(1), "Z0");
    EXPECT_EQ(o.zl(23), "Z22");
    CURSOR(o, 0, 3);
}

TEST(P8000TerminalDiff, Vt100_SGR_Tab4310)
{
    const std::pair<const char*, int> tab[] = {
        {"\x1b[m", 0x80}, {"\x1b[0m", 0x80}, {"\x1b[1m", 0x81}, {"\x1b[4m", 0xA0},
        {"\x1b[5m", 0x82}, {"\x1b[7m", 0x90}, {"\x1b[1;5;7m", 0x93},
    };
    for (auto [seq, b] : tab) {
        Orig o;
        vt(o);
        o.eingabe(std::string(seq) + "x");
        EXPECT_EQ(o.e.hw().bwsByte(0, 0), b) << seq;
        EXPECT_EQ(o.e.hw().zelle(0, 1).zeichen, 'x');
    }
    // ABWEICHUNG (Kern: SGR kumulativ): jede Folge setzt das Attribut neu (81, 82, 80).
    Orig o;
    vt(o);
    o.eingabe("\x1b[1m\x1b[5m\x1b[0m");
    EXPECT_EQ(o.e.hw().bwsByte(0, 0), 0x81);
    EXPECT_EQ(o.e.hw().bwsByte(0, 1), 0x82);
    EXPECT_EQ(o.e.hw().bwsByte(0, 2), 0x80);
}

TEST(P8000TerminalDiff, Vt100_SaveRestoreCursor)
{
    Orig o;
    vt(o);
    o.eingabe("\x1b[5;9H\x1b" "7\x1b[1;1H");
    o.eingabe("\x1b" "8");
    CURSOR(o, 4, 8);
}

TEST(P8000TerminalDiff, Vt100_SteuerzeichenInnerhalbEinerFolge)
{
    Orig o;
    vt(o);
    // ABWEICHUNG (Kern: CR läuft mit, die Folge bleibt intakt): CR wird ausgeführt, der Rest
    // der Folge („;4H") verschluckt — der Cursor bleibt am Zeilenanfang, nichts erscheint.
    o.eingabe("\x1b[3\r;4H");
    CURSOR(o, 0, 0);
    EXPECT_EQ(o.zl(0), "");
    o.eingabe("x");
    EXPECT_EQ(o.zl(0), "x");
}

TEST(P8000TerminalDiff, Adm31UndVt100DeutenEscEUnterschiedlich)
{
    Orig a;
    frisch(a);
    a.eingabe("abc\x1b" "E");
    EXPECT_EQ(a.zl(0), "");
    Orig v;
    vt(v);
    v.eingabe("abc\x1b" "E");
    EXPECT_EQ(v.zl(0), "abc");
    CURSOR(v, 1, 0);
}

// ═════ Firmware-Eigenschaften ohne Gegenstück im Kern-Terminal ══════════════

TEST(P8000TerminalDiff, XoffBei36ZeichenImPufferXonWennLeer)
{
    Orig o;
    frisch(o);
    // Löschen dauert ≈ 65 ms (≈ 62 Zeichenzeiten): fünfmal ESC * und Text dahinter füllt den Puffer
    std::string s;
    for (int i = 0; i < 5; ++i) s += "\x1b*";
    s += std::string(60, 'q');
    o.eingabe(s);
    EXPECT_GE(o.xoffs, 1);
    EXPECT_EQ(o.xons, o.xoffs);
    EXPECT_EQ(o.zl(0), std::string(60, 'q'));   // nichts verloren, weil der Rechner anhält
}

TEST(P8000TerminalDiff, OhneXoffBeachtungGehenZeichenVerloren)
{
    Orig o;
    frisch(o);
    auto& hw = o.e.hw();
    for (int i = 0; i < 5; ++i) { hw.hostByte(0x1b); hw.hostByte('*'); }
    for (int i = 0; i < 60; ++i) hw.hostByte('q');
    o.e.ruheAbwarten(5000);
    // Puffer 45: nach dem Überlauf verwirft die Firmware bis der Puffer leer ist (Fehlermerker)
    EXPECT_LT(o.zl(0).size(), 60u);
}

TEST(P8000TerminalDiff, EmpfangBit7GeloeschtNulUndDelVerworfen)
{
    Orig o;
    frisch(o);
    o.eingabe(std::string("\xEC\x00\x6F\x7F\xE7\x69", 6));
    EXPECT_EQ(o.zl(0), "logi");
}

TEST(P8000TerminalDiff, TastenwiederholungKommtVonDerTastatur)
{
    Orig o;
    frisch(o);
    const MatrixTaste a = o.e.positionFuer({0x1E});
    o.e.tastatur().druecke(a.zeile, a.spalte);
    o.e.laufeMs(1200);
    o.e.tastatur().loslassen(a.zeile, a.spalte);
    o.e.laufeMs(200);
    o.abholen();
    const std::string s = o.nimm();
    EXPECT_GE(s.size(), 6u);   // 1 + Wiederholungen nach 500 ms, dann alle ≈ 100 ms
    EXPECT_EQ(s.find_first_not_of('a'), std::string::npos);
}

// ═════ Bildvergleich Original ↔ Kern-Terminal (gleiche Eingabe, gleiches Bild?) ════════════

namespace {
struct Vergleich { const char* name; TerminalModus modus; std::string eingabe; bool gleich; const char* grund; };
}

/// Jede Eingabe der Fälle oben geht durch BEIDE Terminals; verglichen werden alle 24 Zeilen und
/// der Cursor.  `gleich = false` nur mit Begründung (die Erwartung am Original steht im Fall oben).
TEST(P8000TerminalDiff, BildvergleichMitDemKernTerminal)
{
    using M = TerminalModus;
    std::string zeilen, zeilenVt;
    for (int z = 0; z < 24; ++z) {
        zeilen += "\x1b=" + std::string(1, char(0x20 + z)) + " Z" + std::to_string(z);
        zeilenVt += "\x1b[" + std::to_string(z + 1) + ";1HZ" + std::to_string(z);
    }
    const Vergleich faelle[] = {
        {"Umbruch", M::ADM31, std::string(80, 'x') + "y", true, ""},
        {"Rollen", M::ADM31, "\x1b=7 unten" + std::string(75, '.'), true, ""},
        {"BS ADM31", M::ADM31, "\x1b=! \b", true, ""},
        {"BS VT100", M::VT100, "\x1b[2;1H\bab\b", true, ""},
        {"HT ADM31", M::ADM31, "\tab\t\x1b= o\t\x1b=7o\t", true, ""},
        {"HT VT100", M::VT100, "\t\x1b[1;79H\t\t", true, ""},
        {"LF", M::ADM31, zeilen + "\x1b=7 \n", true, ""},
        {"VT", M::ADM31, "\n\n\x0b\x0b\x0b\x0b", true, ""},
        {"FF ADM31", M::ADM31, "\x0c\x0c\x1b= n\x0c\x0c", true, ""},
        {"FF VT100", M::VT100, std::string(100, '\x0c'), true, ""},
        {"CR RS", M::VT100, "abc\n\rabc\n\x1e", true, ""},
        {"Steuerzeichen", M::ADM31, "\x01\x02" "a", true, ""},
        {"Programm-Mode", M::ADM31, "\x1bU\x01\x1f" "b\x1bu\x01", false, "ESC u wird im Programm-Mode angezeigt"},
        {"CBT ADM31", M::ADM31, "\x1b= 3\x1bI\x1bI", true, ""},
        {"CHT ADM31", M::ADM31, "\x1bi\x1bi", true, ""},
        {"CHT Ende", M::ADM31, "\x1b=7o\x1bi", false, "ESC i in Spalte 80 der letzten Zeile rollt"},
        {"CDE", M::ADM31, "abcdef\x1b= \"\x1bW", true, ""},
        {"CIN", M::ADM31, "abcdef\x1b= \"\x1bQ", true, ""},
        {"HVP", M::ADM31, "\x1b=~~x\x1b=0@", true, ""},
        {"LDE", M::ADM31, zeilen + "\x1b=\"(\x1bR", true, ""},
        {"LER", M::ADM31, "abcdef\x1b= \"\x1bT", true, ""},
        {"LIN", M::ADM31, zeilen + "\x1b=\"(\x1b" "E", true, ""},
        {"PER", M::ADM31, zeilen + "\x1b=\"!\x1bY", true, ""},
        {"SDE *", M::ADM31, zeilen + "\x1b=%%\x1b*", true, ""},
        {"SDE :", M::ADM31, zeilen + "\x1b=%%\x1b:", true, ""},
        {"CBT VT100", M::VT100, "\x1b[1;20H\x1b[Z", false, "(Spalte - 8) & F8H"},
        {"CHT VT100", M::VT100, "\x1b[I\x1b[3I\x1b[99I", true, ""},
        {"CUx", M::VT100, "\x1b[5;10H\x1b[2D\x1b[D\x1b[0D\x1b[99D\x1b[3B\x1b[99B\x1b[4C\x1b[99C\x1b[2A\x1b[99A", true, ""},
        {"CUP", M::VT100, "\x1b[7;12H\x1b[;5H", true, ""},
        {"HVP f", M::VT100, "\x1b[99;99f", true, ""},
        {"DCH", M::VT100, "abcdef\x1b[1;2H\x1b[P\x1b[2P", true, ""},
        {"DL", M::VT100, zeilenVt + "\x1b[3;5H\x1b[2M", true, ""},
        {"ED 0", M::VT100, zeilenVt + "\x1b[3;2H\x1b[J", true, ""},
        {"ED 1", M::VT100, zeilenVt + "\x1b[3;2H\x1b[1J", true, ""},
        {"ED 2", M::VT100, zeilenVt + "\x1b[3;2H\x1b[2J", true, ""},
        {"EL 0", M::VT100, "abcdef\x1b[1;3H\x1b[K", true, ""},
        {"EL 1", M::VT100, "abcdef\x1b[1;3H\x1b[1K", true, ""},
        {"EL 2", M::VT100, "abcdef\x1b[1;3H\x1b[2K", true, ""},
        {"ICH", M::VT100, "abcdef\x1b[1;2H\x1b[2@", true, ""},
        {"IL (W6)", M::VT100, zeilenVt + "\x1b[3;5H\x1b[2L", true, ""},
        {"IND", M::VT100, zeilenVt + "\x1b[24;3H\x1b" "D", true, ""},
        {"NEL", M::VT100, "\x1b[24;9H\x1b" "E", true, ""},
        {"RI", M::VT100, zeilenVt + "\x1b[1;4H\x1b" "M", true, ""},
        {"DECSC/RC", M::VT100, "\x1b[5;9H\x1b" "7\x1b[1;1H\x1b" "8", true, ""},
        {"Folge mit CR", M::VT100, "\x1b[3\r;4H", false, "CR bricht die Folge ab"},
        {"ESC E ADM31", M::ADM31, "abc\x1b" "E", true, ""},
        {"ESC E VT100", M::VT100, "abc\x1b" "E", true, ""},
    };
    for (const auto& f : faelle) {
        Orig o;
        frisch(o, f.modus);
        o.eingabe(f.eingabe);
        Terminal k = kern(f.modus);
        k.eingabe(f.eingabe);
        const int n = unterschiede(o, k);
        if (f.gleich) {
            EXPECT_EQ(n, 0) << f.name;
            if (n)
                for (int z = 0; z < 24; ++z)
                    if (o.zl(z) != kzl(k, z)) ADD_FAILURE() << f.name << " Zeile " << z << ": Original '" << o.zl(z) << "' Kern '" << kzl(k, z) << "'";
            if (n) ADD_FAILURE() << f.name << " Cursor Original " << o.zeile() << "," << o.spalte() << " Kern " << k.zeile() << "," << k.spalte();
        } else {
            EXPECT_GT(n, 0) << f.name << " — als Abweichung geführt (" << f.grund << "), ist aber gleich";
        }
    }
}
