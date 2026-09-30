/**
 * @file test_k7672.cpp
 * @brief Tastatur K7672 (doc/EPROMS/K7672/README.md, doc/design/16_k8915.md §3.5):
 *        Protokoll auf SIO-Ebene — DC1 nur auf Befehl, DC3-Sperre, Befehlsfolgen,
 *        9600-Bd-Latenz, DCP-Zustand.
 */

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

#include "core/peripherals/k7672/k7672.h"
#include "core/primitives/z80_sio.h"

namespace {

struct Aufbau {
    Z80SIO sio;
    K7672  kbd;
    uint64_t t = 0;
    Aufbau() { kbd.connect(sio, 1); kbd.powerOn(); }
    Z80SIO::Channel& ch() { return sio.channelB(); }
    /// Rechner sendet (OUT 52H) und die Tastatur holt ab.
    void sende(std::initializer_list<uint8_t> bs) {
        for (uint8_t b : bs) { sio.ioWrite(2, b); kbd.service(t); }
    }
    void laufe(uint64_t takte) { t += takte; kbd.service(t); }
    int  lies() { return ch().rx_fifo.empty() ? -1 : sio.ioRead(2); }
};

constexpr uint8_t DC1 = 0x11, DC3 = 0x13, ESC = 0x1B;

}  // namespace

TEST(K7672, KeinDc1BeimEinschalten)
{
    Aufbau a;
    a.laufe(10'000'000);
    EXPECT_EQ(a.lies(), -1);
    EXPECT_EQ(a.kbd.selbsttests(), 1u);
    EXPECT_EQ(a.kbd.modus(), K7672::Modus::Scp);
}

/// ESC c: Neustart + Selbsttest, DC1 nach Testdauer + Zeichenzeit.
TEST(K7672, EscCLiefertDc1NachSelbsttest)
{
    Aufbau a;
    a.sende({ESC, 'c'});
    a.laufe(K7672::SELBSTTEST_TAKTE + K7672::ZEICHEN_TAKTE - 1);
    EXPECT_EQ(a.lies(), -1);
    a.laufe(1);
    EXPECT_EQ(a.lies(), DC1);
    EXPECT_EQ(a.kbd.selbsttests(), 2u);
}

/// Die Folge des Boot-ROMs (F1BEH): DC3, ESC c, DC1 abwarten, ESC [2;1y byteweise
/// mit Pausen, DC1 abwarten, DC1.  Jede Antwort binnen der ROM-Frist (≈ 0,85 Mio.).
TEST(K7672, FolgeDesBootRoms)
{
    Aufbau a;
    a.sende({DC3});
    EXPECT_TRUE(a.kbd.sendenGesperrt());
    a.sende({ESC});
    a.laufe(213'000);
    a.sende({'c'});
    EXPECT_FALSE(a.kbd.sendenGesperrt()) << "ESC c löscht 60H bis auf Bit3/4 — auch DC3";
    a.laufe(850'000);
    EXPECT_EQ(a.lies(), DC1);
    for (uint8_t b : {ESC, uint8_t('['), uint8_t('2'), uint8_t(';'), uint8_t('1'), uint8_t('y')}) {
        a.sende({b});
        a.laufe(213'000);
    }
    a.laufe(850'000);
    EXPECT_EQ(a.lies(), DC1);
    EXPECT_EQ(a.lies(), -1) << "genau eine Antwort";
    a.sende({DC1});
    EXPECT_FALSE(a.kbd.sendenGesperrt());
}

TEST(K7672, Esc2Semikolon0yOhneTestSofortDc1)
{
    Aufbau a;
    a.sende({ESC, '[', '2', ';', '0', 'y'});
    a.laufe(K7672::ZEICHEN_TAKTE);
    EXPECT_EQ(a.lies(), DC1);
    EXPECT_EQ(a.kbd.selbsttests(), 1u) << "kein weiterer Selbsttest";
}

/// DC3 sperrt Tasten, DC1 gibt frei; Zeichen im SCP-Modus mit Zeichenzeit.
TEST(K7672, Dc3SperrtTastenScpZeichen)
{
    Aufbau a;
    a.sende({DC3});
    a.kbd.keyPress('a', false, false);
    a.laufe(100'000);
    EXPECT_EQ(a.lies(), -1);
    a.sende({DC1});
    a.kbd.keyPress('A', true, false);
    a.kbd.keyPress(0x01000004, false, false);        // Qt::Key_Return
    a.kbd.keyPress('c', false, true);                // Strg+C
    a.kbd.keyPress(0x01000007, false, false);        // Qt::Key_Delete ⇒ DEL (Diagnosemodus)
    a.laufe(K7672::ZEICHEN_TAKTE);
    EXPECT_EQ(a.lies(), 'A');
    EXPECT_EQ(a.lies(), -1) << "9600 Bd: eins nach dem anderen";
    a.laufe(3 * K7672::ZEICHEN_TAKTE);
    EXPECT_EQ(a.lies(), 0x0D);
    EXPECT_EQ(a.lies(), 0x03);
    EXPECT_EQ(a.lies(), 0x7F);
}

/// ESC [?22h ⇒ DCP-Modus (Scancodes: Etappe 3, hier nur der Zustand); ESC c zurück.
/// Unbekannte Folgen und die übrigen Tabellenbefehle bleiben ohne Antwort.
TEST(K7672, DcpZustandUndUnbekannteFolgen)
{
    Aufbau a;
    a.sende({ESC, '[', '?', '2', '2', 'h'});
    EXPECT_EQ(a.kbd.modus(), K7672::Modus::Dcp);
    a.sende({ESC, '[', 'x'});                         // unbekannt
    a.sende({ESC, '[', '5', 'n'});                    // Status: Text fehlt im Dump
    a.laufe(2'000'000);
    EXPECT_EQ(a.lies(), -1);
    a.sende({ESC, 'c'});
    EXPECT_EQ(a.kbd.modus(), K7672::Modus::Scp);
    a.sende({0x07, 0x07});
    EXPECT_EQ(a.kbd.summerZaehler(), 2u);
}

// ─── DCP-Modus: Scancodes Satz 1 (AP-E3b) ─────────────────────────────────────

namespace {
/// Alle bis jetzt gesendeten Bytes abholen (Zeit genug für jede Zeichenzeit).
std::vector<int> alles(Aufbau& a) {
    std::vector<int> v;
    for (int i = 0; i < 64; ++i) {
        a.laufe(K7672::ZEICHEN_TAKTE);
        const int b = a.lies();
        if (b >= 0) v.push_back(b);
    }
    return v;
}
void dcp(Aufbau& a) { a.sende({ESC, '[', '?', '2', '2', 'h'}); }
}  // namespace

/**
 * @test K7672.DcpBuchstabeDrueckenUndLoslassen
 * @brief Host-Taste 'a' ⇒ 1EH beim Drücken, 9EH beim Loslassen.  DIN-Belegung nach
 *        der BIOS-Tabelle DC1CH: 'z' auf 15H, 'y' auf 2CH; 'Y' = Umschalt + 2CH.
 */
TEST(K7672, DcpBuchstabeDrueckenUndLoslassen)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress('a', false, false);
    a.kbd.keyRelease('a');
    a.kbd.keyPress('z', false, false);
    a.kbd.keyRelease('z');
    a.kbd.keyPress('Y', true, false);   // Grossbuchstabe: mit Umschalt (BIOS DAC8H)
    a.kbd.keyRelease('Y');
    EXPECT_EQ(alles(a), (std::vector<int>{0x1E, 0x9E, 0x15, 0x95, 0x2A, 0x2C, 0xAC, 0xAA}));
}

/**
 * @test K7672.DcpUmschaltUndStrg
 * @brief Ein Zeichen, das auf der K7672 Umschalt braucht ('!' = Umschalt + 1), bekommt
 *        2AH/AAH darum; Strg+C = 1DH 2EH AEH 9DH; Enter 1CH, Rück 0EH, F1 3BH.
 */
TEST(K7672, DcpUmschaltUndStrg)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress('!', true, false);
    a.kbd.keyRelease('!');
    EXPECT_EQ(alles(a), (std::vector<int>{0x2A, 0x02, 0x82, 0xAA}));
    a.kbd.keyPress('c', false, true);   // so liefert die Oberfläche Strg+C (keyboard.py)
    a.kbd.keyRelease('c');
    EXPECT_EQ(alles(a), (std::vector<int>{0x1D, 0x2E, 0xAE, 0x9D}));
    a.kbd.keyPress(0x01000004, false, false);   // Return
    a.kbd.keyRelease(0x01000004);
    a.kbd.keyPress(0x01000003, false, false);   // Backspace
    a.kbd.keyRelease(0x01000003);
    a.kbd.keyPress(0x01000030, false, false);   // F1
    a.kbd.keyRelease(0x01000030);
    EXPECT_EQ(alles(a), (std::vector<int>{0x1C, 0x9C, 0x0E, 0x8E, 0x3B, 0xBB}));
}

/**
 * @test K7672.DcpUmschalttasteSelbstUndGehalteneUmschaltung
 * @brief Die Umschalttaste des Hosts meldet sich als 2AH/AAH.  Hält der Host Umschalt
 *        für ein Zeichen, das ohne Umschalt entsteht ('+' liegt auf 1BH), wird sie
 *        für diese Taste losgelassen und danach wieder gedrückt.
 */
TEST(K7672, DcpUmschalttasteSelbstUndGehalteneUmschaltung)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress(0x01000020, true, false);   // Shift
    a.kbd.keyPress('+', true, false);
    a.kbd.keyRelease('+');
    a.kbd.keyRelease(0x01000020);
    EXPECT_EQ(alles(a), (std::vector<int>{0x2A, 0xAA, 0x1B, 0x9B, 0x2A, 0xAA}));
}

/**
 * @test K7672.DcpSendeZeichenTipptDieTaste
 * @brief `sendeZeichen` im DCP-Modus (Tests, Werkzeuge): das Zeichen wird getippt —
 *        "d" ⇒ 20H A0H, CR ⇒ 1CH 9CH; DC3 sperrt auch hier.
 */
TEST(K7672, DcpSendeZeichenTipptDieTaste)
{
    Aufbau a;
    dcp(a);
    a.kbd.sendeZeichen('d');
    a.kbd.sendeZeichen(0x0D);
    EXPECT_EQ(alles(a), (std::vector<int>{0x20, 0xA0, 0x1C, 0x9C}));
    a.sende({DC3});
    a.kbd.sendeZeichen('d');
    EXPECT_TRUE(alles(a).empty());
}

/// AP-E4b: Anzeige-LEDs als Abbild des Firmware-Registers 21H — Bit 3 folgt DC1/DC3
/// (XON/XOFF-Lampe), Bit 0 `ESC [?13h`/`l`; `ESC c` löscht 21H (Speicherlöscher
/// 04H…7FH); `BEL` zählt den Summer fortlaufend, auch über einen Neustart.
TEST(K7672, LedsRegister21HUndSummer)
{
    Aufbau a;
    EXPECT_EQ(a.kbd.leds(), 0x00) << "Einschalten löscht 21H";
    a.sende({DC1});
    EXPECT_EQ(a.kbd.leds(), 0x08);
    a.sende({ESC, '[', '?', '1', '3', 'h'});
    EXPECT_EQ(a.kbd.leds(), 0x09);
    a.sende({DC3});
    EXPECT_EQ(a.kbd.leds(), 0x01);
    a.sende({ESC, '[', '?', '1', '3', 'l'});
    EXPECT_EQ(a.kbd.leds(), 0x00);
    a.sende({DC1, ESC, '[', '?', '1', '3', 'h'});
    EXPECT_EQ(a.kbd.leds(), 0x09);
    a.sende({0x07, 0x07});
    EXPECT_EQ(a.kbd.summerZaehler(), 2u);
    a.sende({ESC, 'c'});
    EXPECT_EQ(a.kbd.leds(), 0x00) << "Neustart löscht 21H";
    a.sende({0x07});
    EXPECT_EQ(a.kbd.summerZaehler(), 3u) << "Summerzähler läuft über den Neustart weiter";
}

// ─── Physische Tasten der Nachbildung (AP-UI1) ──────────────────────────────

namespace {
constexpr uint32_t T(uint8_t m) { return K7672::QK_TASTE_BASE | m; }
void tippe(Aufbau& a, uint8_t m, bool shift = false, bool ctrl = false) {
    a.kbd.keyPress(T(m), shift, ctrl);
    a.kbd.keyRelease(T(m));
}
}  // namespace

/**
 * @test K7672.MatrixDcpScancodesAusDerFirmware
 * @brief Die Bildschirmtastatur spricht Matrixpositionen an; im DCP-Modus kommt der
 *        Scancode aus D3 0080H: A (20H) = 1EH, ß (05H) = 0CH, RETURN (38H) = 1CH,
 *        PF1 (3EH) = 3BH, Umschalttaste (36H) = 2AH.  Bit 7 im Scancode heißt Vorsatz:
 *        ↑ (79H, C8H, Tastenart ED = Umschalt) = 2A 48 / C8 AA, ^S (7EH, C5H, Art 08
 *        = Strg) = 1D 45 / 9D C5 — kein E0 (Firmware 0326H–035FH, 05C2H).
 */
TEST(K7672, MatrixDcpScancodesAusDerFirmware)
{
    Aufbau a;
    dcp(a);
    tippe(a, 0x20);
    tippe(a, 0x05);
    tippe(a, 0x38);
    tippe(a, 0x3E);
    EXPECT_EQ(alles(a), (std::vector<int>{0x1E, 0x9E, 0x0C, 0x8C, 0x1C, 0x9C, 0x3B, 0xBB}));
    tippe(a, 0x79);
    EXPECT_EQ(alles(a), (std::vector<int>{0x2A, 0x48, 0xC8, 0xAA}));
    tippe(a, 0x7E);
    EXPECT_EQ(alles(a), (std::vector<int>{0x1D, 0x45, 0x9D, 0xC5}));
    a.kbd.keyPress(T(0x36), false, false);   // Umschalttaste selbst
    tippe(a, 0x20);
    a.kbd.keyRelease(T(0x36));
    EXPECT_EQ(alles(a), (std::vector<int>{0x2A, 0x1E, 0x9E, 0xAA}));
}

/**
 * @test K7672.MatrixUmschaltUndStrgDerNachbildung
 * @brief Die rastenden Umschalt-/Strg-Tasten der Bildschirmtastatur kommen als Flags
 *        und wirken wie gehaltene Tasten: 2AH davor, AAH danach; Strg 1DH/9DH.
 */
TEST(K7672, MatrixUmschaltUndStrgDerNachbildung)
{
    Aufbau a;
    dcp(a);
    tippe(a, 0x20, /*shift=*/true);
    EXPECT_EQ(alles(a), (std::vector<int>{0x2A, 0x1E, 0x9E, 0xAA}));
    tippe(a, 0x31, false, /*ctrl=*/true);    // C
    EXPECT_EQ(alles(a), (std::vector<int>{0x1D, 0x2E, 0xAE, 0x9D}));
}

/**
 * @test K7672.MatrixKlickUndUnbelegteTastenSendenNichts
 * @brief `CL` (7DH) schaltet in der Firmware nur den Tastenklick um (030BH), eine
 *        Matrixposition ohne Taste (0CH, FFH) sendet ebenfalls nichts.
 */
TEST(K7672, MatrixKlickUndUnbelegteTastenSendenNichts)
{
    Aufbau a;
    dcp(a);
    tippe(a, K7672::MATRIX_KLICK);
    tippe(a, 0x0C);
    EXPECT_TRUE(alles(a).empty());
}

/**
 * @test K7672.MatrixFeststellSchaltetCapsLampe
 * @brief Firmware 0303H: die Feststelltaste (26H) schaltet LED 21H Bit 7 (CAPS) beim
 *        Drücken um und sendet 3AH/BAH; `ESC [?11h/l` setzt bzw. löscht dasselbe Bit,
 *        `ESC [?18h/l` Bit 6 (GRAPH [?]).
 */
TEST(K7672, MatrixFeststellSchaltetCapsLampe)
{
    Aufbau a;
    dcp(a);
    EXPECT_EQ(a.kbd.leds() & 0x80, 0);
    tippe(a, K7672::MATRIX_FESTSTELL);
    EXPECT_EQ(alles(a), (std::vector<int>{0x3A, 0xBA}));
    EXPECT_EQ(a.kbd.leds() & 0x80, 0x80);
    tippe(a, K7672::MATRIX_FESTSTELL);
    EXPECT_EQ(a.kbd.leds() & 0x80, 0);
    a.sende({ESC, '[', '?', '1', '1', 'h'});
    EXPECT_EQ(a.kbd.leds() & 0x80, 0x80);
    a.sende({ESC, '[', '?', '1', '1', 'l'});
    EXPECT_EQ(a.kbd.leds() & 0x80, 0);
    a.sende({ESC, '[', '?', '1', '8', 'h'});
    EXPECT_EQ(a.kbd.leds() & 0x40, 0x40);
    a.sende({ESC, '[', '?', '1', '8', 'l'});
    EXPECT_EQ(a.kbd.leds() & 0x40, 0);
}

/**
 * @test K7672.MatrixScpZeichenAusDerFirmware
 * @brief SCP-Modus (Boot-ROM): Zeichen aus D3 0400H/0480H — A = 'a', mit Umschalt 'A',
 *        ß = E1H, RETURN = CR; die Feststellung macht nur Buchstaben groß; eine
 *        Funktionstaste (PF1, FFH) sendet nichts.
 */
TEST(K7672, MatrixScpZeichenAusDerFirmware)
{
    Aufbau a;
    tippe(a, 0x20);
    tippe(a, 0x20, true);
    tippe(a, 0x05);
    tippe(a, 0x38);
    tippe(a, 0x3E);
    EXPECT_EQ(alles(a), (std::vector<int>{'a', 'A', 0xE1, 0x0D}));
    tippe(a, K7672::MATRIX_FESTSTELL);       // Feststellung ein
    tippe(a, 0x20);
    tippe(a, 0x00);                          // '1' bleibt '1'
    EXPECT_EQ(alles(a), (std::vector<int>{'A', '1'}));
}
