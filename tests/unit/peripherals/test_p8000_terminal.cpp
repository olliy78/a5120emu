/**
 * @file test_p8000_terminal.cpp
 * @brief P8000-Terminal (doc/p8000/hw_terminal.md §3–§6, Entwurf 25 P6): je Zeile der
 *        Tabellen ein Fall; Widersprüche W2/W3/W6 als benannte Annahme (siehe terminal.h).
 */

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal.h"
#include "core/peripherals/p8000_terminal/terminal_anschluss.h"

using namespace k1520::p8000;
using T = TerminalTaste;

namespace {

/// Terminal im gewünschten Modus mit leerem Bild, Cursor oben links.
Terminal frisch(TerminalModus m = TerminalModus::ADM31) {
    Terminal t;
    if (m == TerminalModus::VT100) {
        t.taste(T::MODE);
        t.eingabe("\x1b[2J\x1b[H");
    } else {
        t.eingabe("\x1b*\x1e");
    }
    return t;
}
Terminal vt() { return frisch(TerminalModus::VT100); }

std::string alsText(Terminal& t) {
    std::string s;
    while (t.hatAusgabe()) s += static_cast<char>(t.holeAusgabe());
    return s;
}

/// Zeile ohne Randleerzeichen.
std::string zl(const Terminal& t, int z) {
    std::string s = t.text(z);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void cursor(const Terminal& t, int z, int s) {
    EXPECT_EQ(t.zeile(), z);
    EXPECT_EQ(t.spalte(), s);
}

/// Bild mit Zeilen "Z0".."Z23" in Spalte 0.. füllen.
void fuelleZeilen(Terminal& t) {
    for (int z = 0; z < 24; ++z) {
        if (t.modus() == TerminalModus::ADM31)
            t.eingabe("\x1b=" + std::string(1, static_cast<char>(0x20 + z)) + " ");
        else
            t.eingabe("\x1b[" + std::to_string(z + 1) + ";1H");
        t.eingabe("Z" + std::to_string(z));
    }
}

}  // namespace

// ── Einschalten / Betriebsarten (§1) ────────────────────────────────────────

TEST(P8000Terminal, EinschaltmeldungAdm31)
{
    Terminal t;
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on");
    cursor(t, 1, 0);
    EXPECT_EQ(t.modus(), TerminalModus::ADM31);
    EXPECT_FALSE(t.zeichensatz2());
    EXPECT_TRUE(t.onLine());
}

TEST(P8000Terminal, ModeTasteSchaltetUmUndInitialisiertNeu)
{
    Terminal t;
    t.eingabe("hallo");
    t.taste(T::MODE);
    EXPECT_EQ(t.modus(), TerminalModus::VT100);
    EXPECT_EQ(zl(t, 0), "VT100/9600 baud/Video Attr. on");
    EXPECT_EQ(zl(t, 1), "");
    cursor(t, 1, 0);
    EXPECT_FALSE(t.hatAusgabe());   // kein Zeichen zum Host
    t.taste(T::MODE);
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on");
}

TEST(P8000Terminal, VideoTasteSchaltetAttributeAus)
{
    Terminal t;
    t.taste(T::VIDEO);
    EXPECT_FALSE(t.videoAttribute());
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. off");
    t.eingabe("\x1b" "G4x");   // Attributsequenz wird verschluckt, belegt keine Position
    EXPECT_FALSE(t.zelle(1, 0).feld);
    EXPECT_EQ(zl(t, 1), "x");
    t.taste(T::VIDEO);
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on");
}

TEST(P8000Terminal, ZeichensatzUmschaltungSiSo)
{
    Terminal t = frisch();
    t.eingabe("a");
    t.taste(T::SI_SO);
    EXPECT_TRUE(t.zeichensatz2());
    t.eingabe("b");
    EXPECT_FALSE(t.zelle(0, 0).zg2);
    EXPECT_TRUE(t.zelle(0, 1).zg2);
    EXPECT_FALSE(t.hatAusgabe());
    t.taste(T::MODE);   // Neuinitialisierung: wieder ZG1
    EXPECT_FALSE(t.zeichensatz2());
}

TEST(P8000Terminal, OnOffLineLokaleAnzeige)
{
    Terminal t = frisch();
    t.taste(T::ON_OFF);
    EXPECT_FALSE(t.onLine());
    t.zeichenTaste('q');
    t.taste(T::CR);
    EXPECT_FALSE(t.hatAusgabe());
    EXPECT_EQ(zl(t, 0), "q");
    t.taste(T::ON_OFF);
    t.zeichenTaste('q');
    EXPECT_EQ(alsText(t), "q");
}

// ── Tastatur §3 ─────────────────────────────────────────────────────────────

TEST(P8000Terminal, TastaturZeichenUndDel)
{
    Terminal t = frisch();
    t.zeichenTaste('A');
    t.zeichenTaste('5');
    t.taste(T::DEL);
    EXPECT_EQ(alsText(t), std::string("A5\x7F"));
}

TEST(P8000Terminal, TastaturCapsLockNurBuchstaben)
{
    Terminal t = frisch();
    t.setzeCapsLock(true);
    t.zeichenTaste('a');
    t.zeichenTaste('1');
    EXPECT_EQ(alsText(t), "A1");
}

TEST(P8000Terminal, TastaturCtrlTabelle45)
{
    Terminal t = frisch();
    t.zeichenTaste('@', true);
    EXPECT_EQ(alsText(t), std::string(1, '\0'));
    for (char c = 'A'; c <= 'Z'; ++c) {   // CTRL a/A … z/Z = 01 … 1A, Klein- wie Großbuchstabe
        t.zeichenTaste(static_cast<uint8_t>(c), true);
        t.zeichenTaste(static_cast<uint8_t>(c + 32), true);
        EXPECT_EQ(alsText(t), std::string(2, static_cast<char>(c - 'A' + 1))) << c;
    }
    t.zeichenTaste('[', true);
    t.zeichenTaste('\\', true);
    t.zeichenTaste(']', true);
    t.zeichenTaste('^', true);
    t.zeichenTaste('_', true);
    EXPECT_EQ(alsText(t), "\x1b\x1c\x1d\x1e\x1f");
}

namespace {
struct Zeile36 { TerminalTaste taste; const char* adm; const char* vt; };
}

TEST(P8000Terminal, TastaturSteuerzeichenTasten_Tab436)
{
    const Zeile36 tab[] = {
        {T::VT, "\x0B", "\x1b[A"}, {T::LF, "\x0A", "\x1b[B"},
        {T::FF, "\x0C", "\x1b[C"},   // W2: 0CH, nicht 09H der Referenzkarte
        {T::BS, "\x08", "\x1b[D"}, {T::HOME, "\x1E", "\x1b[H"},
        {T::HT, "\x09", "\x09"}, {T::NL, "\x08", "\x08"},
        {T::CR, "\x0D", "\x0D"}, {T::ESC, "\x1b", "\x1b"},
    };
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Terminal t = frisch(m);
        for (const auto& z : tab) {
            t.taste(z.taste);
            EXPECT_EQ(alsText(t), m == TerminalModus::ADM31 ? z.adm : z.vt)
                << static_cast<int>(z.taste);
        }
    }
}

TEST(P8000Terminal, TastaturFunktionstasten_Tab437)
{
    const Zeile36 tab[] = {
        {T::LINE_ERASE, "\x1bT", "\x1b[K"}, {T::PAGE_ERASE, "\x1bY", "\x1b[J"},
        {T::LINE_INSERT, "" "E", "\x1b[L"}, {T::CHAR_INSERT, "\x1bQ", "\x1b[@"},
        {T::LINE_DELETE, "\x1bR", "\x1b[M"},   // W3: ESC R, nicht ESC T der Referenzkarte
        {T::CHAR_DELETE, "\x1bW", "\x1b[P"},
        {T::TAB, "\x09", "\x1b[I"}, {T::BACKTAB, "\x1bI", "\x1b[Z"},
    };
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Terminal t = frisch(m);
        for (const auto& z : tab) {
            t.taste(z.taste);
            EXPECT_EQ(alsText(t), m == TerminalModus::ADM31 ? z.adm : z.vt)
                << static_cast<int>(z.taste);
        }
    }
}

TEST(P8000Terminal, BreakTasteSendetKeinZeichen)
{
    Terminal t = frisch();
    t.taste(T::BREAK);
    EXPECT_TRUE(t.breakAnstehend());
    EXPECT_FALSE(t.hatAusgabe());
}

// ── Steuerzeichen §4 ────────────────────────────────────────────────────────

TEST(P8000Terminal, SichtbareZeichenUndZeilenumbruch)
{
    Terminal t = frisch();
    t.eingabe(std::string(80, 'x') + "y");
    EXPECT_EQ(zl(t, 1), "y");
    cursor(t, 1, 1);
}

TEST(P8000Terminal, RollenNachDer24Zeile)
{
    Terminal t = frisch();
    t.eingabe("\x1b=" "7 ");   // Zeile 24, Spalte 1
    t.eingabe("unten");
    t.eingabe(std::string(75, '.'));   // Zeile füllen → Umbruch am Ende der letzten Zeile
    EXPECT_EQ(zl(t, 22).substr(0, 5), "unten");
    EXPECT_EQ(zl(t, 23), "");
    cursor(t, 23, 0);
}

TEST(P8000Terminal, Bel)
{
    Terminal t = frisch();
    t.eingabe("\x07");
    EXPECT_EQ(t.klingel(), 1u);
    cursor(t, 0, 0);
    Terminal v = vt();
    v.eingabe("\x07");
    EXPECT_EQ(v.klingel(), 1u);
}

TEST(P8000Terminal, BsAdm31BisSchirmanfangMitUmbruch)
{
    Terminal t = frisch();
    t.eingabe("\x1b=" "\x21 ");   // Zeile 2, Spalte 1
    t.eingabe("\b");
    cursor(t, 0, 79);
    t.eingabe("\x1e\b");
    cursor(t, 0, 0);
}

TEST(P8000Terminal, BsVt100BisZeilenanfang)
{
    Terminal t = vt();
    t.eingabe("\x1b[2;1H\b");
    cursor(t, 1, 0);
    t.eingabe("ab\b");
    cursor(t, 1, 1);
}

TEST(P8000Terminal, HtAdm31NaechsterTabUmbruchUndRollen)
{
    Terminal t = frisch();
    t.eingabe("\t");
    cursor(t, 0, 8);
    t.eingabe("ab\t");
    cursor(t, 0, 16);
    t.eingabe("\x1b=" "\x20o\t");   // Spalte 79
    cursor(t, 1, 0);
    t.eingabe("\x1b=" "7o\t");      // letzte Zeile, Spalte 79 → Rollen
    cursor(t, 23, 0);
}

TEST(P8000Terminal, HtVt100BisZeilenende)
{
    Terminal t = vt();
    t.eingabe("\t");
    cursor(t, 0, 8);
    t.eingabe("\x1b[1;79H\t\t");
    cursor(t, 0, 79);
}

TEST(P8000Terminal, LfAbwaertsSpalteBleibtUndRollt)
{
    Terminal t = frisch();
    t.eingabe("ab\n");
    cursor(t, 1, 2);
    fuelleZeilen(t);
    t.eingabe("\x1b=" "7 \n");
    EXPECT_EQ(zl(t, 0), "Z1");
    EXPECT_EQ(zl(t, 22), "Z23");
    EXPECT_EQ(zl(t, 23), "");
}

TEST(P8000Terminal, VtAufwaertsEndetInZeile1)
{
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Terminal t = frisch(m);
        t.eingabe("\n\n\x0b");
        cursor(t, 1, 0);
        t.eingabe("\x0b\x0b\x0b");
        cursor(t, 0, 0);
    }
}

TEST(P8000Terminal, FfAdm31EinRechtsMitUmbruch)
{
    Terminal t = frisch();
    t.eingabe("\x0c\x0c");
    cursor(t, 0, 2);
    t.eingabe("\x1b=" "\x20n\x0c");   // Spalte 79 (0-basiert 78) → 80
    cursor(t, 0, 79);
    t.eingabe("\x0c");
    cursor(t, 1, 0);
}

TEST(P8000Terminal, FfVt100BisZeilenende)
{
    Terminal t = vt();
    t.eingabe(std::string(100, '\x0c'));
    cursor(t, 0, 79);
}

TEST(P8000Terminal, CrUndRsHome)
{
    for (auto m : {TerminalModus::ADM31, TerminalModus::VT100}) {
        Terminal t = frisch(m);
        t.eingabe("abc\n\r");
        cursor(t, 1, 0);
        t.eingabe("abc\n\x1e");
        cursor(t, 0, 0);
    }
}

TEST(P8000Terminal, SteuerzeichenSindUnsichtbarProgrammModeZeigtAlle)
{
    Terminal t = frisch();
    t.eingabe("\x01\x02" "a");
    EXPECT_EQ(zl(t, 0), "a");
    t.eingabe("\x1b" "U\x01\x1f" "b");
    EXPECT_EQ(t.zelle(0, 1).zeichen, 0x01);
    EXPECT_EQ(t.zelle(0, 2).zeichen, 0x1f);
    EXPECT_TRUE(t.programmMode());
    t.eingabe("\x1b" "u" "\x01");
    EXPECT_FALSE(t.programmMode());
    cursor(t, 0, 4);
}

// ── ADM31-Sequenzen §5 ──────────────────────────────────────────────────────

TEST(P8000Terminal, Adm31_CBT_ESC_I)
{
    Terminal t = frisch();
    t.eingabe("\x1b=" "\x20" "3\x1b" "I");   // Spalte 20 (0-basiert 19)
    cursor(t, 0, 16);
    t.eingabe("\x1b" "I");
    cursor(t, 0, 8);
    t.eingabe("\x1b" "I\x1b" "I\x1b" "I");
    cursor(t, 0, 0);   // endet an HOME
}

TEST(P8000Terminal, Adm31_CHT_ESC_i)
{
    Terminal t = frisch();
    t.eingabe("\x1b" "i");
    cursor(t, 0, 8);
    t.eingabe("\x1b=" "7o\x1b" "i\x1b" "i");   // letzte Zeile, Spalte 79 → Bildschirmende
    cursor(t, 23, 79);
}

TEST(P8000Terminal, Adm31_CDE_ESC_W)
{
    Terminal t = frisch();
    t.eingabe("abcdef\x1b=" "\x20\x22\x1b" "W");   // Cursor auf 'c'
    EXPECT_EQ(zl(t, 0), "abdef");
    cursor(t, 0, 2);
}

TEST(P8000Terminal, Adm31_CIN_ESC_Q)
{
    Terminal t = frisch();
    t.eingabe("abcdef\x1b=" "\x20\x22\x1b" "Q");
    EXPECT_EQ(zl(t, 0), "ab cdef");
    t.eingabe("\x1b=" "\x20\x20" + std::string(77, 'z'));   // Zeile bis zum Ende füllen
    t.eingabe("\x1b=" "\x20\x20\x1b" "Q");
    EXPECT_EQ(t.text(0)[0], ' ');
    EXPECT_EQ(t.text(0).size(), 80u);   // letztes Zeichen ging verloren, Zeile bleibt 80 breit
}

TEST(P8000Terminal, Adm31_HVP_ESC_Gleich)
{
    Terminal t = frisch();
    t.eingabe("\x1b=" "\x20\x20");   // Zeile 1, Spalte 1
    cursor(t, 0, 0);
    t.eingabe("\x1b=" "7o");         // Zeile 24 = 37H, Spalte 80 = 6FH
    cursor(t, 23, 79);
    t.eingabe("\x1b=" "\x7f\x7f");   // zu groß → letzte Position
    cursor(t, 23, 79);
    t.eingabe("\x1b=" "  ");         // 20H/20H zurück
    cursor(t, 0, 0);
    t.eingabe("\x1b=" "\x30\x40");
    cursor(t, 16, 32);
}

TEST(P8000Terminal, Adm31_LDE_ESC_R)
{
    Terminal t = frisch();
    fuelleZeilen(t);
    t.eingabe("\x1b=" "\"\x28\x1b" "R");   // Zeile 3, Spalte 9
    EXPECT_EQ(zl(t, 2), "Z3");
    EXPECT_EQ(zl(t, 23), "");
    cursor(t, 2, 0);
}

TEST(P8000Terminal, Adm31_LER_ESC_T)
{
    Terminal t = frisch();
    t.eingabe("abcdef\x1b=" "\x20\x22\x1b" "T");
    EXPECT_EQ(zl(t, 0), "ab");
    cursor(t, 0, 2);
}

TEST(P8000Terminal, Adm31_LIN_ESC_E)
{
    Terminal t = frisch();
    fuelleZeilen(t);
    t.eingabe("\x1b=" "\"\x28\x1b" "E");
    EXPECT_EQ(zl(t, 2), "");
    EXPECT_EQ(zl(t, 3), "Z2");
    EXPECT_EQ(zl(t, 23), "Z22");   // letzte Zeile (Z23) ging verloren
    cursor(t, 2, 0);
}

TEST(P8000Terminal, Adm31_PER_ESC_Y)
{
    Terminal t = frisch();
    fuelleZeilen(t);
    t.eingabe("\x1b=" "\"\x21\x1b" "Y");   // Zeile 3, Spalte 2
    EXPECT_EQ(zl(t, 1), "Z1");
    EXPECT_EQ(zl(t, 2), "Z");
    EXPECT_EQ(zl(t, 3), "");
    cursor(t, 2, 1);
}

TEST(P8000Terminal, Adm31_PMN_PMF)
{
    Terminal t = frisch();
    t.eingabe("\x1b" "U");
    EXPECT_TRUE(t.programmMode());
    t.eingabe("\x1b" "X");
    EXPECT_FALSE(t.programmMode());
    t.eingabe("\x1b" "U\x1b" "u");
    EXPECT_FALSE(t.programmMode());
}

TEST(P8000Terminal, Adm31_SDE_CursorBleibt)
{
    for (const char* seq : {"\x1b*", "\x1b:"}) {
        Terminal t = frisch();
        fuelleZeilen(t);
        t.eingabe("\x1b=" "\x25\x25");
        t.eingabe(seq);
        for (int z = 0; z < 24; ++z) EXPECT_EQ(zl(t, z), "");
        cursor(t, 5, 5);
    }
}

TEST(P8000Terminal, Adm31_SGR_AlleParameter_Tab439)
{
    const std::pair<char, uint8_t> tab[] = {
        {'0', 0}, {'1', ATTR_LEER}, {'2', ATTR_BLINK}, {'3', ATTR_LEER},
        {'4', ATTR_INVERS}, {'5', ATTR_LEER | ATTR_INVERS},
        {'6', ATTR_BLINK | ATTR_INVERS}, {'7', ATTR_LEER | ATTR_INVERS},
    };
    for (auto [p, a] : tab) {
        Terminal t = frisch();
        t.eingabe(std::string("\x1b" "G") + p + "x");
        EXPECT_TRUE(t.zelle(0, 0).feld) << p;
        EXPECT_EQ(t.zelle(0, 0).attr, a) << p;
        EXPECT_EQ(t.text(0)[0], ' ');       // Pseudozeichen = Leerzeichen
        EXPECT_EQ(t.zelle(0, 1).zeichen, 'x');
        EXPECT_EQ(t.wirksamesAttribut(0, 1), a) << p;
    }
}

// ── Attribute: Feldregel ────────────────────────────────────────────────────

TEST(P8000Terminal, FeldregelAttributWirktNurInnerhalbDerZeile)
{
    Terminal t = frisch();
    t.eingabe("a\x1b" "G4" "bc\n\r" "d");
    EXPECT_EQ(t.wirksamesAttribut(0, 0), 0);
    EXPECT_EQ(t.wirksamesAttribut(0, 3), ATTR_INVERS);
    EXPECT_EQ(t.wirksamesAttribut(0, 79), ATTR_INVERS);   // bis Zeilenende
    EXPECT_EQ(t.wirksamesAttribut(1, 0), 0);               // nächste Zeile unberührt
}

TEST(P8000Terminal, FeldAufhebungDurchGleichesAttribut0UndUeberschreiben)
{
    Terminal t = frisch();
    t.eingabe("\x1b" "G2" "ab\x1b" "G0" "cd");
    EXPECT_EQ(t.wirksamesAttribut(0, 2), ATTR_BLINK);
    EXPECT_EQ(t.wirksamesAttribut(0, 4), 0);
    t.eingabe("\x1e" "x");   // sichtbares Zeichen überschreibt das Pseudozeichen
    EXPECT_FALSE(t.zelle(0, 0).feld);
    EXPECT_EQ(t.wirksamesAttribut(0, 2), 0);
}

// ── VT100-Sequenzen §6 ──────────────────────────────────────────────────────

TEST(P8000Terminal, Vt100_CBT)
{
    Terminal t = vt();
    t.eingabe("\x1b[1;20H\x1b[Z");
    cursor(t, 0, 16);
    t.eingabe("\x1b[2Z");
    cursor(t, 0, 0);
    t.eingabe("\x1b[9Z");
    cursor(t, 0, 0);   // endet in Spalte 1
}

TEST(P8000Terminal, Vt100_CHT)
{
    Terminal t = vt();
    t.eingabe("\x1b[I");
    cursor(t, 0, 8);
    t.eingabe("\x1b[3I");
    cursor(t, 0, 32);
    t.eingabe("\x1b[99I");
    cursor(t, 0, 79);
}

TEST(P8000Terminal, Vt100_CUB_CUD_CUF_CUU)
{
    Terminal t = vt();
    t.eingabe("\x1b[5;10H");
    cursor(t, 4, 9);
    t.eingabe("\x1b[2D");  cursor(t, 4, 7);
    t.eingabe("\x1b[D");   cursor(t, 4, 6);
    t.eingabe("\x1b[0D");  cursor(t, 4, 5);   // 0 → 1
    t.eingabe("\x1b[99D"); cursor(t, 4, 0);
    t.eingabe("\x1b[3B");  cursor(t, 7, 0);
    t.eingabe("\x1b[99B"); cursor(t, 23, 0);
    t.eingabe("\x1b[4C");  cursor(t, 23, 4);
    t.eingabe("\x1b[99C"); cursor(t, 23, 79);
    t.eingabe("\x1b[2A");  cursor(t, 21, 79);
    t.eingabe("\x1b[99A"); cursor(t, 0, 79);
}

TEST(P8000Terminal, Vt100_CUP_HVP)
{
    for (const char* f : {"H", "f"}) {
        Terminal t = vt();
        t.eingabe(std::string("\x1b[7;12") + f);
        cursor(t, 6, 11);
        t.eingabe(std::string("\x1b[") + f);
        cursor(t, 0, 0);   // Default 1;1
        t.eingabe(std::string("\x1b[;5") + f);
        cursor(t, 0, 4);
        t.eingabe(std::string("\x1b[99;99") + f);
        cursor(t, 23, 79);
        t.eingabe(std::string("\x1b[0;0") + f);
        cursor(t, 0, 0);
    }
}

TEST(P8000Terminal, Vt100_DCH)
{
    Terminal t = vt();
    t.eingabe("abcdef\x1b[1;2H\x1b[P");
    EXPECT_EQ(zl(t, 0), "acdef");
    t.eingabe("\x1b[2P");
    EXPECT_EQ(zl(t, 0), "aef");
    cursor(t, 0, 1);
}

TEST(P8000Terminal, Vt100_DL)
{
    Terminal t = vt();
    fuelleZeilen(t);
    t.eingabe("\x1b[3;5H\x1b[2M");
    EXPECT_EQ(zl(t, 1), "Z1");
    EXPECT_EQ(zl(t, 2), "Z4");
    EXPECT_EQ(zl(t, 22), "");
    EXPECT_EQ(zl(t, 23), "");
    cursor(t, 2, 4);   // Cursorposition bleibt
}

TEST(P8000Terminal, Vt100_DSR_CursorPositionsReport)
{
    Terminal t = vt();
    t.eingabe("\x1b[12;34H\x1b[6n");
    EXPECT_EQ(alsText(t), "\x1b[12;34R");
    t.eingabe("\x1b[H\x1b[6n");
    EXPECT_EQ(alsText(t), "\x1b[1;1R");
    t.eingabe("\x1b[5n");   // nicht beschrieben → keine Antwort
    EXPECT_FALSE(t.hatAusgabe());
}

TEST(P8000Terminal, Vt100_ED)
{
    for (int mode : {-1, 0, 1, 2}) {   // -1 = ohne Parameter (= 0)
        Terminal t = vt();
        fuelleZeilen(t);
        t.eingabe("\x1b[3;2H");
        t.eingabe(mode < 0 ? "\x1b[J" : "\x1b[" + std::to_string(mode) + "J");
        if (mode <= 0) {
            EXPECT_EQ(zl(t, 1), "Z1");
            EXPECT_EQ(zl(t, 2), "Z");
            EXPECT_EQ(zl(t, 3), "");
        } else if (mode == 1) {
            EXPECT_EQ(zl(t, 0), "");
            EXPECT_EQ(zl(t, 1), "");
            EXPECT_EQ(zl(t, 2), "");   // „Z2": beide Zeichen liegen bis zum Cursor
            EXPECT_EQ(zl(t, 3), "Z3");
        } else {
            for (int z = 0; z < 24; ++z) EXPECT_EQ(zl(t, z), "");
        }
        cursor(t, 2, 1);   // Cursor bleibt
    }
}

TEST(P8000Terminal, Vt100_EL)
{
    for (int mode : {-1, 0, 1, 2}) {
        Terminal t = vt();
        t.eingabe("abcdef\x1b[1;3H");
        t.eingabe(mode < 0 ? "\x1b[K" : "\x1b[" + std::to_string(mode) + "K");
        if (mode <= 0) EXPECT_EQ(zl(t, 0), "ab");
        else if (mode == 1) EXPECT_EQ(zl(t, 0), "   def");
        else EXPECT_EQ(zl(t, 0), "");
        cursor(t, 0, 2);
    }
}

TEST(P8000Terminal, Vt100_ICH)
{
    Terminal t = vt();
    t.eingabe("abcdef\x1b[1;2H\x1b[2@");
    EXPECT_EQ(zl(t, 0), "a  bcdef");
    cursor(t, 0, 1);
    t.eingabe("\x1b[1;1H" + std::string(80, 'x') + "\x1b[1;1H\x1b[@");
    EXPECT_EQ(t.text(0)[0], ' ');
    EXPECT_EQ(t.text(0)[79], 'x');
}

TEST(P8000Terminal, Vt100_IL_VorDerCursorzeile_W6)
{
    Terminal t = vt();
    fuelleZeilen(t);
    t.eingabe("\x1b[3;5H\x1b[2L");
    EXPECT_EQ(zl(t, 1), "Z1");
    EXPECT_EQ(zl(t, 2), "");
    EXPECT_EQ(zl(t, 3), "");
    EXPECT_EQ(zl(t, 4), "Z2");
    EXPECT_EQ(zl(t, 23), "Z21");   // Überlauf unten verloren
    cursor(t, 2, 4);
}

TEST(P8000Terminal, Vt100_IND_ESC_D)
{
    Terminal t = vt();
    fuelleZeilen(t);
    t.eingabe("\x1b[5;7H");
    t.eingabe("\x1b" "D");
    cursor(t, 5, 6);
    t.eingabe("\x1b[24;3H\x1b" "D");
    EXPECT_EQ(zl(t, 0), "Z1");   // erste Zeile ging verloren
    EXPECT_EQ(zl(t, 23), "");
    cursor(t, 23, 2);
}

TEST(P8000Terminal, Vt100_NEL_ESC_E_ohneRollen)
{
    Terminal t = vt();
    t.eingabe("\x1b[2;9H\x1b" "E");
    cursor(t, 2, 0);
    t.eingabe("\x1b[24;9H\x1b" "E");
    cursor(t, 23, 0);   // Annahme: endet am Bildschirmende
}

TEST(P8000Terminal, Vt100_RI_ESC_M)
{
    Terminal t = vt();
    fuelleZeilen(t);
    t.eingabe("\x1b[3;4H\x1b" "M");
    cursor(t, 1, 3);
    t.eingabe("\x1b[1;4H\x1b" "M");
    EXPECT_EQ(zl(t, 0), "");
    EXPECT_EQ(zl(t, 1), "Z0");
    EXPECT_EQ(zl(t, 23), "Z22");   // letzte Zeile ging verloren
    cursor(t, 0, 3);
}

TEST(P8000Terminal, Vt100_SGR_Tab4310)
{
    const std::pair<const char*, uint8_t> tab[] = {
        {"\x1b[m", 0}, {"\x1b[0m", 0}, {"\x1b[1m", ATTR_BOLD},
        {"\x1b[4m", ATTR_UNTERSTRICH}, {"\x1b[5m", ATTR_BLINK}, {"\x1b[7m", ATTR_INVERS},
        {"\x1b[1;5;7m", ATTR_BOLD | ATTR_BLINK | ATTR_INVERS},   // bis zu 3 Parameter
    };
    for (auto [seq, a] : tab) {
        Terminal t = vt();
        t.eingabe(std::string(seq) + "x");
        EXPECT_TRUE(t.zelle(0, 0).feld);
        EXPECT_EQ(t.zelle(0, 0).attr, a) << seq;
        EXPECT_EQ(t.zelle(0, 1).zeichen, 'x');
    }
    Terminal t = vt();   // kumulativ, 0 hebt alles auf
    t.eingabe("\x1b[1m\x1b[5m\x1b[0m");
    EXPECT_EQ(t.zelle(0, 0).attr, ATTR_BOLD);
    EXPECT_EQ(t.zelle(0, 1).attr, ATTR_BOLD | ATTR_BLINK);
    EXPECT_EQ(t.zelle(0, 2).attr, 0);
}

TEST(P8000Terminal, Vt100_SaveRestoreCursor)
{
    Terminal t = vt();
    t.eingabe("\x1b[5;9H\x1b" "7\x1b[1;1H");
    t.eingabe("\x1b" "8");
    cursor(t, 4, 8);
}

TEST(P8000Terminal, Vt100_SteuerzeichenInnerhalbEinerFolge)
{
    Terminal t = vt();
    t.eingabe("\x1b[3\r;4H");   // CR läuft mit, die Folge bleibt intakt
    cursor(t, 2, 3);
}

TEST(P8000Terminal, Adm31UndVt100DeutenEscEUnterschiedlich)
{
    Terminal a = frisch();
    a.eingabe("abc\x1b" "E");
    EXPECT_EQ(zl(a, 0), "");        // LIN: Leerzeile eingefügt
    Terminal v = vt();
    v.eingabe("abc\x1b" "E");
    EXPECT_EQ(zl(v, 0), "abc");     // NEL
    cursor(v, 1, 0);
}

// ── Anschluss ───────────────────────────────────────────────────────────────

namespace {

/// Gegenseite einer Karte: der Gast sendet aus `gast`, empfängt in `empfangen`.
struct KarteStub : k1520::serial::SerialAnschluss {
    std::deque<uint8_t> gast;
    std::vector<uint8_t> empfangen;
    bool frei = true;
    bool brk = false;
    int  breakWechsel = 0;
    k1520::serial::SerialFormat fmt;

    const char* name() const override { return "tty1"; }
    const char* stecker() const override { return "X5"; }
    bool v24() const override { return true; }
    k1520::serial::SerialFormat format() const override { return fmt; }
    bool senderHatZeichen() const override { return !gast.empty(); }
    uint8_t senderNimm() override { uint8_t b = gast.front(); gast.pop_front(); return b; }
    bool empfaengerFrei() const override { return frei; }
    void empfange(uint8_t b) override { empfangen.push_back(b); }
    void breakEmpfang(bool a) override { brk = a; ++breakWechsel; }
};

k1520::serial::SerialFormat format9600(uint64_t phi, uint8_t daten = 8) {
    return k1520::serial::serialFormatRechnen(1, daten, 0, 4, phi / 9600 / 16 * 16, phi);
}

}  // namespace

TEST(P8000TerminalAnschluss, ZeichenKommenImZeichentaktAnDerAn)
{
    KarteStub k;
    k.fmt.gueltig = true; k.fmt.zeichen_takte = 1000; k.fmt.baud_nenn = 9600; k.fmt.daten = 8;
    Terminal t = frisch();
    TerminalAnschluss a(k, t);
    for (char c : std::string("abc")) k.gast.push_back(static_cast<uint8_t>(c));
    a.takt(1);
    EXPECT_EQ(zl(t, 0), "a");
    a.takt(999);
    EXPECT_EQ(zl(t, 0), "a");   // erst nach einer Zeichenzeit das nächste
    a.takt(1);
    EXPECT_EQ(zl(t, 0), "ab");
    a.takt(1000);
    EXPECT_EQ(zl(t, 0), "abc");
    EXPECT_FALSE(a.baudAbweichend());
}

/// Firmware `p8t.init.s` (serielle Eingabe): 00H/7FH verworfen, Bit 7 ausgeblendet — WEGA-getty
/// sendet „login:" mit Software-Parität in Bit 7 (P15).
TEST(P8000TerminalAnschluss, Bit7WirdAusgeblendetNulUndDelVerworfen)
{
    KarteStub k;
    k.fmt.gueltig = true; k.fmt.zeichen_takte = 10; k.fmt.baud_nenn = 9600; k.fmt.daten = 8;
    Terminal t = frisch();
    TerminalAnschluss a(k, t);
    for (uint8_t b : {uint8_t(0xEC), uint8_t(0x00), uint8_t(0x6F), uint8_t(0x7F), uint8_t(0xE7), uint8_t(0x69)})
        k.gast.push_back(b);
    for (int i = 0; i < 6; ++i) a.takt(10);
    EXPECT_EQ(zl(t, 0), "logi");
}

TEST(P8000TerminalAnschluss, TastenbytesNurBeiFreiemEmpfaengerKeinUeberlauf)
{
    KarteStub k;
    k.fmt.gueltig = true; k.fmt.zeichen_takte = 100;
    Terminal t = frisch();
    TerminalAnschluss a(k, t);
    t.zeichenTaste('x'); t.zeichenTaste('y'); t.zeichenTaste('z');
    k.frei = false;
    for (int i = 0; i < 10; ++i) a.takt(100);
    EXPECT_TRUE(k.empfangen.empty());
    k.frei = true;
    a.takt(100);
    a.takt(100);
    a.takt(100);
    EXPECT_EQ(std::string(k.empfangen.begin(), k.empfangen.end()), "xyz");
    EXPECT_FALSE(t.hatAusgabe());
}

TEST(P8000TerminalAnschluss, DsrAntwortLaeuftZumHost)
{
    KarteStub k;
    k.fmt.gueltig = true; k.fmt.zeichen_takte = 10;
    Terminal t = vt();
    TerminalAnschluss a(k, t);
    for (char c : std::string("\x1b[6n")) k.gast.push_back(static_cast<uint8_t>(c));
    for (int i = 0; i < 40; ++i) a.takt(10);
    EXPECT_EQ(std::string(k.empfangen.begin(), k.empfangen.end()), "\x1b[1;1R");
}

TEST(P8000TerminalAnschluss, BreakEineZeichenzeit)
{
    KarteStub k;
    k.fmt.gueltig = true; k.fmt.zeichen_takte = 100;
    Terminal t = frisch();
    TerminalAnschluss a(k, t);
    t.taste(T::BREAK);
    a.takt(1);
    EXPECT_TRUE(k.brk);
    a.takt(50);
    EXPECT_TRUE(k.brk);
    a.takt(60);
    EXPECT_FALSE(k.brk);
    EXPECT_EQ(k.breakWechsel, 2);
}

TEST(P8000TerminalAnschluss, GastformatAbweichendWirdGemeldetZeichenTrotzdemGezeigt)
{
    KarteStub k;
    k.fmt = format9600(4'000'000, 7);
    ASSERT_TRUE(k.fmt.gueltig);
    Terminal t = frisch();
    TerminalAnschluss a(k, t);
    EXPECT_TRUE(a.baudAbweichend());
    k.gast.push_back('q');
    a.takt(1);
    EXPECT_EQ(zl(t, 0), "q");
    k.fmt = format9600(4'000'000, 8);
    EXPECT_FALSE(a.baudAbweichend());
}

TEST(P8000TerminalAnschluss, OhneGueltigesFormatGilt9600Mit11Bit)
{
    KarteStub k;   // fmt.gueltig = false
    Terminal t = frisch();
    TerminalAnschluss a(k, t, 4'000'000);
    k.gast.push_back('a'); k.gast.push_back('b');
    a.takt(1);
    EXPECT_EQ(zl(t, 0), "a");
    a.takt(4582);   // 11 × 4 000 000 / 9600 = 4583 Takte je Zeichen
    EXPECT_EQ(zl(t, 0), "a");
    a.takt(1);
    EXPECT_EQ(zl(t, 0), "ab");
}
