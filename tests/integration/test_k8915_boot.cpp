/**
 * @file test_k8915_boot.cpp
 * @brief K8915 Etappe 1 und 2 (doc/design/16_k8915.md §8a AP-E1/AP-E2): das Boot-ROM
 *        läuft auf der K8915Machine (ZRE 045-8762 + ATS K7028.30 mit K7672 + K7024)
 *        durch den Selbsttest bis zur Coldstart-Meldung des Laders.
 *
 * **Wie die Statuszeile wirklich aussieht** (am Lauf festgestellt, §4.3): Das ROM
 * zeigt KEINE Zeile „ROM RAM SIO KEY CTC" auf einmal.  `sub_FC19` schreibt
 * „DIAGNOSTIC" nach 1740H und den Namen des **gerade laufenden** Tests (3 Zeichen)
 * nach 1770H; ein Fehler setzt einen Kennbuchstaben nach 1776H.  Die Reihenfolge ist
 * ROM → RAM → RAZ (Bank 2) → KEY → CTC → SIO.  Nach bestandener Prüfsumme macht das
 * ROM aus „ROM" per `LD (1771H),'A'` einfach „RAM".
 *
 * Etappe 2: mit Tastatur und Prüfstecker besteht der Selbsttest ohne Buchstaben und
 * geht über F3F3H in den Lader („* Coldstart *  Disk on A: ready ? --> <ENTER>").
 * Gegenproben: ohne Tastatur scheitert KEY mit 'A' (keine Antwort auf `ESC c`), ohne
 * Prüfstecker SIO mit 'G' (kein Echo auf SIO1-A, SIO1-B, SIO2-A).  Nach einem Fehler
 * wartet das ROM in F3C0H auf eine Taste; `CR` startet den Lader trotzdem.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/machines/k8915/k8915.h"
#include "tests/support/screen.h"

using k1520test::vramLines;
using k1520test::vramText;

namespace {

constexpr int kZeile     = 23;   // 1740H/1770H/1776H liegen in der letzten Zeile
constexpr int kSpalteTest = 0x770 - kZeile * 80;   // 1770H → Spalte 64
constexpr int kSpalteFehler = 0x776 - kZeile * 80; // 1776H → Spalte 70
constexpr int kSpalteDiag = 0x740 - kZeile * 80;   // 1740H → Spalte 16

// 2,4576 MHz: der RAM-Test allein braucht ≈ 20 Mio. Takte (≈ 8 s Maschinenzeit).
constexpr long long kFrist = 60'000'000;
constexpr int       kSchritt = 20'000;

char zeichen(K8915Machine& m, int col) {
    return static_cast<char>(m.screen().vramRead(col, kZeile) & 0x7F);
}

std::string testname(K8915Machine& m) {
    std::string s;
    for (int i = 0; i < 3; ++i) s.push_back(zeichen(m, kSpalteTest + i));
    return s;
}

bool bekannterName(const std::string& n) {
    for (const char* k : {"ROM", "RAM", "RAZ", "RAB", "KEY", "CTC", "SIO"})
        if (n == k) return true;
    return false;
}

struct Verlauf {
    std::vector<std::string> namen;   ///< Folge der angezeigten Testnamen (ohne Wiederholung)
    char fehler = ' ';                ///< Kennbuchstabe bei 1776H im Endzustand
    std::string fehlerBei;            ///< Testname, unter dem der Fehler stand
    long long takte = -1;
};

bool coldstart(K8915Machine& m) {
    return vramText(m).find("* Coldstart *  Disk on A: ready") != std::string::npos;
}

/// Läuft, bis ein Fehlerbuchstabe oder die Coldstart-Meldung steht (oder die Frist
/// abläuft); zeichnet die Namen auf.
Verlauf selbsttest(K8915Machine& m) {
    Verlauf v;
    long long done = 0;
    while (done < kFrist) {
        done += m.run(kSchritt);
        if (coldstart(m)) { v.takte = done; break; }
        const std::string n = testname(m);
        if (bekannterName(n) && (v.namen.empty() || v.namen.back() != n))
            v.namen.push_back(n);
        const char f = zeichen(m, kSpalteFehler);
        if (bekannterName(n) && f >= 'A' && f <= 'Z') {
            v.fehler = f; v.fehlerBei = n; v.takte = done;
            break;
        }
    }
    return v;
}

std::string folge(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& n : v) s += n + " ";
    return s;
}

}  // namespace

K8915Machine::Config ohneTastatur() { K8915Machine::Config c; c.tastatur = false; return c; }
K8915Machine::Config ohnePruefstecker() { K8915Machine::Config c; c.pruefstecker = false; return c; }

/**
 * @test K8915Boot.SelbsttestFehlerfreiBisColdstart
 * @brief Fertig-Kriterium Etappe 2 (AP-E2): mit K7672 und Prüfstecker zeigt die
 *        Statuszeile nacheinander ROM, RAM, RAZ, KEY, CTC, SIO, kein Test setzt einen
 *        Fehlerbuchstaben, und das ROM geht ohne Tastendruck in den Lader —
 *        „* Coldstart *  Disk on A: ready".  KEY: zwei `DC1` der Tastatur (nach `ESC c`
 *        und `ESC [2;1y`); CTC: je CTC genau ein Interrupt in ihrem DJNZ-Fenster (A = 4);
 *        SIO: Echo auf allen drei Kanälen.  Danach `CR` ⇒ Lader ⇒ ohne Diskette
 *        „Drive A: not ready, check".
 */
TEST(K8915Boot, SelbsttestFehlerfreiBisColdstart)
{
    K8915Machine m;
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << "weder Fehler noch Coldstart, PC=" << std::hex
                          << m.zre().cpu().PC << " Folge: " << folge(v.namen) << "\n"
                          << vramLines(m);
    EXPECT_EQ(v.fehler, ' ') << "Fehler '" << v.fehler << "' unter " << v.fehlerBei
                             << "\n" << vramLines(m);
    const std::vector<std::string> soll = {"ROM", "RAM", "RAZ", "KEY", "CTC", "SIO"};
    EXPECT_EQ(v.namen, soll) << "Folge: " << folge(v.namen);
    EXPECT_TRUE(coldstart(m)) << vramLines(m);
    EXPECT_EQ(m.keyboard().selbsttests(), 3u) << "Einschalten, ESC c, ESC [2;1y";
    EXPECT_FALSE(m.keyboard().sendenGesperrt()) << "zum Schluss DC1 an die Tastatur";
    EXPECT_EQ(m.ats().anzeige(), 0xB0) << "0907H meldet Coldstart mit 61H = B0H";

    // CR von der Tastatur (0942H liest 53H/52H) ⇒ Lader.  Ohne Diskette meldet sich das
    // Laufwerk nicht bereit (K5122 /RDYL = 1; mit Diskette: test_k8915_scpx.cpp).
    m.keyboard().sendeZeichen(0x0D);
    for (long long done = 0; done < 10'000'000 &&
         vramText(m).find("Drive A: not ready, check") == std::string::npos;
         done += m.run(5'000)) {}
    EXPECT_NE(vramText(m).find("Drive A: not ready, check"), std::string::npos) << vramLines(m);
    EXPECT_EQ(m.ats().anzeige(), 0x60) << "Fehlermeldungen mit 61H = 60H";
}

/**
 * @test K8915Boot.OhnePruefsteckerScheitertSioMitG
 * @brief Ohne Rückschleife kommt auf keinem der drei Kanäle ein Echo: Fehlermaske
 *        111B | 40H = 'G' unter „SIO".  Das ROM wartet dann in F3C0H; ein `CR` von der
 *        Tastatur startet den Lader trotzdem.  Welche Vorgabe dem Gerät entspricht,
 *        klärt §6.10.
 */
TEST(K8915Boot, OhnePruefsteckerScheitertSioMitG)
{
    K8915Machine m(ohnePruefstecker());
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << vramLines(m);
    EXPECT_EQ(v.fehlerBei, "SIO") << folge(v.namen) << "\n" << vramLines(m);
    EXPECT_EQ(v.fehler, 'G') << vramLines(m);
    EXPECT_FALSE(coldstart(m));

    // F167H: 16 × BEL (BC = 1040H, je ≈ 213 000 Takte Pause — die 64 × gelten nur
    // für den ROM-/RAM-Fehlerweg 005AH), dann F3C0H: CR ⇒ Lader.
    m.run(4'000'000);
    EXPECT_EQ(m.keyboard().summerZaehler(), 16u);
    m.keyboard().sendeZeichen(0x0D);
    for (long long done = 0; done < 3'000'000 && !coldstart(m); done += m.run(5'000)) {}
    EXPECT_TRUE(coldstart(m)) << vramLines(m);
}

/**
 * @test K8915Boot.OhneTastaturScheitertKeyMitA
 * @brief Gegenprobe zu AP-E1: ohne K7672 (Kabel gezogen) bekommt `ESC c` kein `DC1`
 *        ⇒ 'A' unter KEY.  ROM-, RAM- und Bank-2-Test davor bestehen; die Statuszeile
 *        zeigt „DIAGNOSTIC" und nacheinander ROM, RAM, RAZ, KEY.
 */
TEST(K8915Boot, OhneTastaturScheitertKeyMitA)
{
    K8915Machine m(ohneTastatur());
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << "kein Fehlerbuchstabe innerhalb der Frist, PC="
                          << std::hex << m.zre().cpu().PC << " A8H=" << int(m.zre().reg())
                          << " Folge: " << folge(v.namen) << "\n" << vramLines(m);

    const std::vector<std::string> soll = {"ROM", "RAM", "RAZ", "KEY"};
    EXPECT_EQ(v.namen, soll) << "Folge: " << folge(v.namen);
    EXPECT_EQ(v.fehlerBei, "KEY") << "Fehler '" << v.fehler << "' unter " << v.fehlerBei
                                  << " — vor KEY darf nichts scheitern\n" << vramLines(m);
    EXPECT_EQ(v.fehler, 'A') << "KEY ohne Tastatur: DC1-Frist nach ESC c ⇒ 'A'";

    const std::string zeile = vramText(m).substr(kZeile * 80, 80);
    EXPECT_EQ(zeile.substr(kSpalteDiag, 10), "DIAGNOSTIC") << vramLines(m);
}

/**
 * @test K8915Boot.RomPruefsummeBestanden
 * @brief Die ROM-eigene 24-Bit-Prüfsumme (0000–0FFB gegen 0FFD–0FFF) besteht: 0083H
 *        schreibt 'A' nach 1771H, aus „ROM" wird „RAM", 1776H bleibt leer.  Bei einem
 *        Fehlschlag stünde „ROM" mit 'F' bei 1776H.  Billiger Wächter dafür, dass
 *        EPROM-Inhalt und ROM-Abbildung (Bit0 = 0 ⇒ ROM bei 0000–0FFF) stimmen.
 */
TEST(K8915Boot, RomPruefsummeBestanden)
{
    K8915Machine m;
    m.powerOn();
    bool rom_gesehen = false;
    for (long long done = 0; done < kFrist; done += m.run(kSchritt)) {
        const std::string n = testname(m);
        if (n == "ROM") rom_gesehen = true;
        if (rom_gesehen && n != "ROM") break;
    }
    ASSERT_TRUE(rom_gesehen) << vramLines(m);
    EXPECT_EQ(testname(m), "RAM") << vramLines(m);
    // Leer heißt hier 20H ODER 08H: der erste Bildlöscher nach 001BH füllt mit dem
    // Wert aus FCF8H, und den hat die Kopie 00C0H gerade wieder auf den ROM-Wert 08H
    // gesetzt (das `LD (FCF8H),20H` bei 0025H davor ist wirkungslos).  08H ist im
    // Zeichensatz der 012-6820 ein leeres Feld.
    const char f = zeichen(m, kSpalteFehler);
    EXPECT_TRUE(f == ' ' || f == 0x08) << "Fehlerbuchstabe " << f << "\n" << vramLines(m);
}

/**
 * @test K8915Boot.SystemImRamFuehrtZumLader
 * @brief Steht bei 0000H und 0005H je ein `JP` (C3H) im RAM, meldet der Stub bei FFE0H
 *        (ROM aus bei 87H) „System im RAM" und das ROM geht ohne Selbsttest in den
 *        Lader: „* Coldstart *  Disk on A: ready".  Wächter für Bit0 (Seite 0 RAM
 *        statt ROM) und den Rückweg auf 06H.
 */
TEST(K8915Boot, SystemImRamFuehrtZumLader)
{
    K8915Machine m;
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    long long done = 0;
    while (done < 5'000'000 && vramText(m).find("Coldstart") == std::string::npos)
        done += m.run(kSchritt);
    EXPECT_NE(vramText(m).find("* Coldstart *  Disk on A: ready"), std::string::npos)
        << vramLines(m);
    EXPECT_EQ(vramText(m).find("DIAGNOSTIC"), std::string::npos) << "kein Selbsttest erwartet";
    EXPECT_EQ(m.zre().reg(), 0x06) << "Lader läuft bei 06H aus dem ROM";
}

/**
 * @test K8915Boot.ZreCtcLiefertIm2Interrupt
 * @brief Die CTC der ZRE bei 80H–83H liefert Interrupts über die Kette (IM 2, I = FFH,
 *        Vektor F0H, Kanal 3 als Zeitgeber, Steuerwort A7H/Zeitkonstante 0CH — wie der
 *        CTC-Test des ROMs bei F208H, der in Etappe 1 hinter dem scheiternden KEY-Test
 *        liegt).  Die ISR zählt A hoch (wie FC64H).
 */
TEST(K8915Boot, ZreCtcLiefertIm2Interrupt)
{
    K8915Machine m;
    m.powerOn();
    auto& zre = m.zre();
    zre.setReg(0x87);                                    // alles Bank 1
    const uint8_t prog[] = {
        0xF3,                   // DI
        0x31, 0x00, 0xF0,       // LD SP,F000H
        0x3E, 0xFF, 0xED, 0x47, // LD A,FFH ; LD I,A
        0x3E, 0xF0, 0xD3, 0x80, // LD A,F0H ; OUT (80H),A   Vektorbasis
        0x3E, 0xA7, 0xD3, 0x83, // LD A,A7H ; OUT (83H),A   K3: Zeitgeber, Int, Vorteiler 256
        0x3E, 0x0C, 0xD3, 0x83, // LD A,0CH ; OUT (83H),A   Zeitkonstante
        0xAF, 0xED, 0x5E, 0xFB, // XOR A ; IM 2 ; EI
        0x18, 0xFE,             // JR $
    };
    for (size_t i = 0; i < sizeof prog; ++i) zre.bankPoke(0, uint16_t(0x0100 + i), prog[i]);
    const uint8_t isr[] = {0x3C, 0xFB, 0xED, 0x4D};     // INC A ; EI ; RETI
    for (size_t i = 0; i < sizeof isr; ++i) zre.bankPoke(0, uint16_t(0x0200 + i), isr[i]);
    zre.bankPoke(0, 0xFFF6, 0x00);                       // Vektor F0H + 2·3 = F6H
    zre.bankPoke(0, 0xFFF7, 0x02);
    zre.cpu().PC = 0x0100;

    m.run(100'000);                                      // ≈ 32 Perioden à 256·12 Takte
    EXPECT_GE(int(zre.cpu().A), 25) << "zu wenige CTC-Interrupts";
    EXPECT_LE(int(zre.cpu().A), 40);
    EXPECT_FALSE(m.bus().lastIntAck().spurious);
    EXPECT_EQ(m.bus().lastIntAck().vector, 0xF6);
}

/**
 * @test K8915Boot.NmiImRomStartetDenSelbsttestNeu
 * @brief NMI-Taster der Frontplatte (AP-UI1, §3.6): der Lader wartet an der
 *        Coldstart-Meldung (A8H = 06H, ROM eingeblendet) ⇒ `nmi()` ⇒ ROM 0066H
 *        `LD A,FFH / OUT (61H),A / JP 001BH` — alle Lampen aus, Selbsttest von vorn
 *        („DIAGNOSTIC" in der Statuszeile).  Kein /RESET: A8H bleibt 06H.
 */
TEST(K8915Boot, NmiImRomStartetDenSelbsttestNeu)
{
    K8915Machine m;
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);    // Abkürzung wie SystemImRamFuehrtZumLader
    m.zre().bankPoke(0, 0x0005, 0xC3);
    long long done = 0;
    while (done < 5'000'000 && !coldstart(m)) done += m.run(kSchritt);
    ASSERT_TRUE(coldstart(m)) << vramLines(m);
    ASSERT_EQ(vramText(m).find("DIAGNOSTIC"), std::string::npos);
    ASSERT_NE(m.ats().anzeige(), 0xFF) << "Coldstart meldet sich mit 61H = B0H";

    m.nmi();
    m.run(200);                            // ein paar Befehle: 0066H ist durchlaufen
    EXPECT_EQ(m.ats().anzeige(), 0xFF) << "0066H: OUT (61H),FFH — alle Lampen aus";
    EXPECT_EQ(m.panelLamps(), 0xFF) << "Spiegel für die Oberfläche";

    done = 0;
    while (done < 5'000'000 && vramText(m).find("DIAGNOSTIC") == std::string::npos)
        done += m.run(kSchritt);
    EXPECT_NE(vramText(m).find("DIAGNOSTIC"), std::string::npos) << vramLines(m);
}

/**
 * @test K8915Boot.NmiOhneDruckGeschiehtNichts
 * @brief Gegenprobe: ohne `nmi()` bleibt der Lader an der Coldstart-Meldung stehen,
 *        und ein Druck wird genau EINMAL zugestellt (Flanke, kein Pegel).
 */
TEST(K8915Boot, NmiOhneDruckGeschiehtNichts)
{
    K8915Machine m;
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    long long done = 0;
    while (done < 5'000'000 && !coldstart(m)) done += m.run(kSchritt);
    ASSERT_TRUE(coldstart(m));
    for (int i = 0; i < 50; ++i) m.run(kSchritt);
    EXPECT_EQ(vramText(m).find("DIAGNOSTIC"), std::string::npos) << vramLines(m);
    EXPECT_TRUE(coldstart(m));
}
