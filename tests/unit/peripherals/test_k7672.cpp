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
 *        2AH/AAH darum; Strg+C = 1DH 2EH AEH 9DH; Enter 1CH, F1 3BH (Rücktaste:
 *        DcpRuecktasteIstKursorLinks).
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
    a.kbd.keyPress(0x01000030, false, false);   // F1
    a.kbd.keyRelease(0x01000030);
    EXPECT_EQ(alles(a), (std::vector<int>{0x1C, 0x9C, 0x3B, 0xBB}));
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

/**
 * @test K7672.DcpRuecktasteIstKursorLinks
 * @brief AP-E4m: die PC-Rücktaste trifft im DCP-Modus die Taste Kursor ← (Matrix 28H,
 *        Scancode CBH = Umschalt-Vorsatz + 4BH) — daraus macht das BIOS 08H (Tabelle
 *        DCC2H), und das BDOS löscht das Zeichen sichtbar.  Die Taste |←| (67H, 0EH)
 *        ergäbe 7FH (DC1CH) = Rubout mit Echo.  Entf = Taste DEL (47H, D3H: 2AH 53H).
 *        Qt-Code und Matrixposition liefern byteweise dasselbe.
 */
TEST(K7672, DcpRuecktasteIstKursorLinks)
{
    constexpr uint32_t QK_BACKSPACE = 0x01000003, QK_DELETE = 0x01000007,
                       QK_LEFT = 0x01000012;
    Aufbau a;
    dcp(a);
    a.kbd.keyPress(QK_BACKSPACE, false, false);
    a.kbd.keyRelease(QK_BACKSPACE);
    const std::vector<int> rueck = alles(a);
    EXPECT_EQ(rueck, (std::vector<int>{0x2A, 0x4B, 0xCB, 0xAA}));
    tippe(a, 0x28);                                   // Kursor ← der Nachbildung
    EXPECT_EQ(alles(a), rueck);
    a.kbd.keyPress(QK_LEFT, false, false);
    a.kbd.keyRelease(QK_LEFT);
    EXPECT_EQ(alles(a), rueck);

    a.kbd.keyPress(QK_DELETE, false, false);
    a.kbd.keyRelease(QK_DELETE);
    const std::vector<int> entf = alles(a);
    EXPECT_EQ(entf, (std::vector<int>{0x2A, 0x53, 0xD3, 0xAA}));
    tippe(a, 0x47);                                   // DEL der Nachbildung
    EXPECT_EQ(alles(a), entf);

    tippe(a, 0x67);                                   // |←|: Scancode 0EH ohne Vorsatz
    EXPECT_EQ(alles(a), (std::vector<int>{0x0E, 0x8E}));
    EXPECT_EQ(K7672::scpZeichen(0x67, false), 0x08) << "SCP-Modus: |←| = BS";
}

// ─── AP-T1a: Host-Tasten, die der Abdeckungsbau ungeprüft fand ───────────────

namespace {
constexpr uint32_t QK_TAB = 0x01000001, QK_BACKTAB = 0x01000002, QK_BACKSPACE = 0x01000003,
                   QK_SHIFT = 0x01000020, QK_CONTROL = 0x01000021, QK_ALT = 0x01000023,
                   QK_CAPSLOCK = 0x01000024, QK_LEFT = 0x01000012, QK_UP = 0x01000013,
                   QK_RIGHT = 0x01000014, QK_DOWN = 0x01000015, QK_PGUP = 0x01000016,
                   QK_PGDN = 0x01000017, QK_HOME = 0x01000010;

/// Die Taste der K7672-Matrix mit diesem Scancode, -1 = keine.  In der Firmwaretabelle
/// heißt Bit 7 „mit Vorsatz"; @p vorsatz verlangt zusätzlich den Vorsatz 2AH (nicht 1DH).
int matrixMit(uint8_t code, bool vorsatz) {
    const uint8_t soll = vorsatz ? static_cast<uint8_t>(code | 0x80) : code;
    for (int m = 0; m < 128; ++m)
        if (K7672::scancode(static_cast<uint8_t>(m)) == soll
            && K7672::vorsatzUmschalt(static_cast<uint8_t>(m)) == vorsatz)
            return m;
    return -1;
}
}  // namespace

/**
 * @test K7672.DcpKursortastenDesHostsSindTastenDerFirmware
 * @brief Die Host-Zuordnung in `tasteDcp` ist von Hand geschrieben ({Code, Umschalt});
 *        jede Kursortaste muss eine echte Taste der Matrix treffen, die in den
 *        Firmware-Tabellen (EPROM D3 0080H/0100H) denselben Scancode MIT Vorsatz 2AH
 *        trägt — und byteweise dasselbe senden wie diese Taste der Nachbildung.
 */
TEST(K7672, DcpKursortastenDesHostsSindTastenDerFirmware)
{
    const struct { uint32_t qk; uint8_t code; } tab[] = {
        {QK_LEFT, 0x4B}, {QK_RIGHT, 0x4D}, {QK_UP, 0x48}, {QK_DOWN, 0x50},
        {QK_PGUP, 0x49}, {QK_PGDN, 0x51},
    };
    for (const auto& z : tab) {
        Aufbau a;
        dcp(a);
        a.kbd.keyPress(z.qk, false, false);
        a.kbd.keyRelease(z.qk);
        const std::vector<int> host = alles(a);
        EXPECT_EQ(host, (std::vector<int>{0x2A, z.code, z.code | 0x80, 0xAA}))
            << std::hex << "Qt " << z.qk;
        const int m = matrixMit(z.code, true);
        ASSERT_GE(m, 0) << std::hex << "keine Matrixtaste mit Vorsatz für " << int(z.code);
        tippe(a, static_cast<uint8_t>(m));
        EXPECT_EQ(alles(a), host) << std::hex << "Matrix " << m;
    }
}

/**
 * @test K7672.DcpUmschalttastenDesHostsUndFeststellLampe
 * @brief Strg 1DH/9DH, Alt 38H/B8H, Feststell 3AH/BAH; die Feststelltaste des Hosts
 *        schaltet LED 21H Bit 7 beim DRÜCKEN um (Firmware 0303H) wie die Taste der
 *        Nachbildung.  Eine Taste ohne Zuordnung (Pos1) sendet nichts.
 */
TEST(K7672, DcpUmschalttastenDesHostsUndFeststellLampe)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress(QK_CONTROL, false, true);
    a.kbd.keyRelease(QK_CONTROL);
    a.kbd.keyPress(QK_ALT, false, false);
    a.kbd.keyRelease(QK_ALT);
    EXPECT_EQ(alles(a), (std::vector<int>{0x1D, 0x9D, 0x38, 0xB8}));

    EXPECT_EQ(a.kbd.leds() & 0x80, 0);
    a.kbd.keyPress(QK_CAPSLOCK, false, false);
    EXPECT_EQ(a.kbd.leds() & 0x80, 0x80);
    a.kbd.keyRelease(QK_CAPSLOCK);
    EXPECT_EQ(a.kbd.leds() & 0x80, 0x80) << "Loslassen schaltet nicht zurück";
    a.kbd.keyPress(QK_CAPSLOCK, false, false);
    a.kbd.keyRelease(QK_CAPSLOCK);
    EXPECT_EQ(a.kbd.leds() & 0x80, 0);
    EXPECT_EQ(alles(a), (std::vector<int>{0x3A, 0xBA, 0x3A, 0xBA}));

    a.kbd.keyPress(QK_HOME, false, false);
    a.kbd.keyRelease(QK_HOME);
    EXPECT_TRUE(alles(a).empty());
}

/**
 * @test K7672.DcpSteuerzeichenOhneBuchstabe
 * @brief `sendeZeichen` im DCP-Modus: 1CH…1FH sind Strg + ein Nicht-Buchstabe
 *        (BIOS-Tabelle DC0FH): 1CH = Strg+\ (27H), 1DH = Strg+] (1AH), 1EH = Strg+#
 *        (29H), 1FH = Strg+- (35H).  Ein Zeichen ohne Taste (80H) sendet nichts.
 */
TEST(K7672, DcpSteuerzeichenOhneBuchstabe)
{
    Aufbau a;
    dcp(a);
    const struct { uint8_t z, code; } tab[] = {{0x1C, 0x27}, {0x1D, 0x1A}, {0x1E, 0x29}, {0x1F, 0x35}};
    for (const auto& t : tab) {
        a.kbd.sendeZeichen(t.z);
        EXPECT_EQ(alles(a), (std::vector<int>{0x1D, t.code, t.code | 0x80, 0x9D}))
            << std::hex << int(t.z);
    }
    a.kbd.sendeZeichen(0x80);
    EXPECT_TRUE(alles(a).empty());
}

/**
 * @test K7672.ScpTabRuecktasteUndUnbekannteTaste
 * @brief SCP-Modus (Boot-ROM, Monitor): Tab und Umschalt+Tab ⇒ 09H, Rücktaste ⇒ 08H;
 *        eine Host-Taste ohne Zeichen (Umschalt allein, Kursor) sendet nichts.
 */
TEST(K7672, ScpTabRuecktasteUndUnbekannteTaste)
{
    Aufbau a;
    for (uint32_t k : {QK_TAB, QK_BACKTAB, QK_BACKSPACE, QK_SHIFT, QK_LEFT}) {
        a.kbd.keyPress(k, false, false);
        a.kbd.keyRelease(k);
    }
    EXPECT_EQ(alles(a), (std::vector<int>{0x09, 0x09, 0x08}));
}

// ─── Tastenwiederholung (AP-E4g) ──────────────────────────────────────────────
// Die Regeln stehen in der Firmware (k7672.h, „Tastenwiederholung“), die Zeit ist
// gerechnet [?].  Alle Fälle laufen in 1000-Takt-Schritten durch service() — wie die
// Laufschleife der Maschine — und nicht durch einen Aufruf, den nur der Test macht.

namespace {
constexpr uint64_t VERZ  = K7672::WDH_VERZOEGERUNG_DURCHLAEUFE * K7672::ABTASTDURCHLAUF_TAKTE;
constexpr uint64_t FOLGE = K7672::WDH_FOLGE_DURCHLAEUFE * K7672::ABTASTDURCHLAUF_TAKTE;

constexpr uint32_t QK_F1 = 0x01000030;

struct Ankunft { uint64_t zeit; int byte; };

/// @p takte fahren; liefert jedes eintreffende Byte mit dem Zeitpunkt (auf 1000 genau).
std::vector<Ankunft> fahre(Aufbau& a, uint64_t takte) {
    std::vector<Ankunft> v;
    for (uint64_t t = 0; t < takte; t += 1000) {
        a.laufe(1000);
        for (int b; (b = a.lies()) >= 0;) v.push_back({a.t, b});
    }
    return v;
}
std::vector<int> bytes(const std::vector<Ankunft>& v) {
    std::vector<int> r;
    for (const auto& x : v) r.push_back(x.byte);
    return r;
}
}  // namespace

/// DCP: gehaltenes 'a' ⇒ nach der Verzögerung der Drücken-Code wieder und wieder,
/// OHNE Loslassen dazwischen, im Abstand FOLGE; Loslassen beendet.
TEST(K7672, WiederholungDcpNachVerzoegerungUndRate)
{
    Aufbau a;
    dcp(a);
    fahre(a, 100'000);
    a.kbd.keyPress('a', false, false);
    const uint64_t t0 = a.t;
    const auto vor = fahre(a, VERZ - 10'000);
    EXPECT_EQ(bytes(vor), (std::vector<int>{0x1E})) << "vor Ablauf der Verzögerung nur der Druck";

    const auto v = fahre(a, 10'000 + 3 * FOLGE + 10'000);
    ASSERT_EQ(v.size(), 4u);
    for (const auto& x : v) EXPECT_EQ(x.byte, 0x1E) << "kein Loslassen zwischen den Wiederholungen";
    EXPECT_NEAR(double(v[0].zeit - t0), double(VERZ + K7672::ZEICHEN_TAKTE), 2000);
    for (size_t i = 1; i < v.size(); ++i)
        EXPECT_NEAR(double(v[i].zeit - v[i - 1].zeit), double(FOLGE), 2000) << i;
    EXPECT_TRUE(a.kbd.wiederholtGerade());

    a.kbd.keyRelease('a');
    EXPECT_FALSE(a.kbd.wiederholtGerade());
    EXPECT_EQ(bytes(fahre(a, 3 * FOLGE)), (std::vector<int>{0x9E})) << "danach nur der Loslass-Code";
}

/// Umschalt, Strg, Feststell und ALT haben keine Dauerfunktion (Bit 7 der Tastenart
/// fehlt) — gehalten kommt der Code genau einmal.  PF1 (Tastenart A6H) dagegen
/// wiederholt, und die Ziffer 1 (Tastenart 31H) wiederholt laut Firmware nicht [?].
TEST(K7672, WiederholungOhneModifikatorenAberMitFunktionstasten)
{
    {
        Aufbau a;
        dcp(a);
        a.kbd.keyPress(QK_F1, false, false);
        EXPECT_GE(fahre(a, VERZ + 2 * FOLGE).size(), 3u);
    }
    {
        Aufbau a;
        dcp(a);
        a.kbd.keyPress('1', false, false);
        EXPECT_EQ(fahre(a, VERZ + 2 * FOLGE).size(), 1u);
        EXPECT_FALSE(a.kbd.wiederholtGerade());
    }
    for (uint32_t k : {QK_SHIFT, QK_CONTROL, QK_ALT, QK_CAPSLOCK}) {
        Aufbau a;
        dcp(a);
        a.kbd.keyPress(k, false, false);
        const auto v = fahre(a, VERZ + 4 * FOLGE);
        EXPECT_EQ(v.size(), 1u) << std::hex << k;
        EXPECT_FALSE(a.kbd.wiederholtGerade()) << std::hex << k;
    }
}

/// Nur die zuletzt gedrückte Taste wiederholt.  Eine neue Taste beginnt mit voller
/// Verzögerung, ein Modifikator dazwischen stört nicht, das Loslassen einer ANDEREN
/// Taste auch nicht.
TEST(K7672, WiederholungNurDieZuletztGedrueckteTaste)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress('a', false, false);
    fahre(a, VERZ / 2);
    a.kbd.keyPress('b', false, false);
    const uint64_t tb = a.t;
    const auto v = fahre(a, VERZ + FOLGE + 10'000);
    // a-Druck wurde schon abgeholt; hier: b-Druck, dann (erst nach VERZ) b, b — nie a.
    ASSERT_GE(v.size(), 3u);
    EXPECT_EQ(v[0].byte, 0x30);
    for (size_t i = 1; i < v.size(); ++i) EXPECT_EQ(v[i].byte, 0x30) << "kein a mehr";
    EXPECT_GE(v[1].zeit - tb, VERZ) << "die neue Taste beginnt mit voller Verzögerung";

    // Loslassen der alten Taste und ein Modifikator ändern nichts.
    a.kbd.keyRelease('a');
    EXPECT_TRUE(a.kbd.wiederholtGerade());
    a.kbd.keyPress(QK_SHIFT, false, false);
    EXPECT_TRUE(a.kbd.wiederholtGerade());
    a.kbd.keyRelease('b');
    EXPECT_FALSE(a.kbd.wiederholtGerade());
}

/// SCP-Modus: die Firmware wiederholt dort das letzte Zeichen (0195H, `LD SIO,0EH`).
TEST(K7672, WiederholungScpWiederholtDasZeichen)
{
    Aufbau a;
    a.kbd.keyPress('x', false, false);
    const auto v = fahre(a, VERZ + 2 * FOLGE + 10'000);
    EXPECT_EQ(bytes(v), (std::vector<int>{'x', 'x', 'x', 'x'}));
    a.kbd.keyRelease('x');
    EXPECT_TRUE(fahre(a, 3 * FOLGE).empty());
    // Tab hat keine Dauerfunktion (Matrix 06H, Tastenart 09H).
    a.kbd.keyPress(QK_TAB, false, false);
    EXPECT_EQ(bytes(fahre(a, VERZ + 2 * FOLGE)), (std::vector<int>{0x09}));
}

/// Unter DC3 steht der Zähler still (0168H): weder wiederholt die Taste, noch läuft
/// die Verzögerung weiter.  Nach DC1 geht es mit dem Rest weiter.
TEST(K7672, WiederholungStehtUnterDc3Still)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress('a', false, false);
    fahre(a, VERZ - 20'000);                 // fast abgelaufen
    a.sende({DC3});
    EXPECT_TRUE(fahre(a, 3 * VERZ).empty()) << "gesperrt: nichts";
    a.sende({DC1});
    const auto v = fahre(a, 40'000);
    EXPECT_EQ(bytes(v), (std::vector<int>{0x1E})) << "nach XON der Rest der Verzögerung";
}

/// Physische Tasten (Bildschirmtastatur): Cursor ↑ (Matrix 79H) hat in der Firmware
/// einen Vorsatz — wiederholt wird Vorsatz + Code (0320H), Umschalt (16H) gar nicht.
TEST(K7672, WiederholungMatrixtasteMitVorsatz)
{
    Aufbau a;
    dcp(a);
    a.kbd.keyPress(K7672::QK_TASTE_BASE | 0x79, false, false);
    const auto v = fahre(a, VERZ + 10'000);
    EXPECT_EQ(bytes(v), (std::vector<int>{0x2A, 0x48, 0x2A, 0x48}));
    a.kbd.keyRelease(K7672::QK_TASTE_BASE | 0x79);
    EXPECT_FALSE(a.kbd.wiederholtGerade());

    Aufbau b;
    dcp(b);
    b.kbd.keyPress(K7672::QK_TASTE_BASE | 0x16, false, false);   // Umschalt
    EXPECT_EQ(fahre(b, VERZ + 2 * FOLGE).size(), 1u);
}

/// `ESC [?19h/20h/21h` wählen in der Firmware die Zeichentabellenseite (Register 2DH,
/// 0263H), NICHT die Wiederholung: Zeit und Wirkung bleiben, wie sie sind.
TEST(K7672, WiederholungHatMitEsc19Bis21Nichts)
{
    for (char stufe : {'9', '0', '1'}) {
        Aufbau a;
        dcp(a);
        a.sende({ESC, '[', '?', (stufe == '9') ? uint8_t('1') : uint8_t('2'), uint8_t(stufe), 'h'});
        a.kbd.keyPress('a', false, false);
        const uint64_t t0 = a.t;
        const auto v = fahre(a, VERZ + 10'000);
        ASSERT_EQ(v.size(), 2u) << stufe;
        EXPECT_NEAR(double(v[1].zeit - t0), double(VERZ + K7672::ZEICHEN_TAKTE), 2000) << stufe;
    }
}
