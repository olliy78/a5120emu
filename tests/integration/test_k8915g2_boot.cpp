/**
 * @file test_k8915g2_boot.cpp
 * @brief K8915 Generation 2 (doc/design/24_k8915_varianten.md AP-V6a): ZRE K2521 mit dem
 *        ROM 175/176/177, Speicher K3528 (A8H), sonst die Karten des V3 — vom Netz-Ein
 *        über den Selbsttest zur Kaltstartmeldung und mit der V3-Systemdiskette 901 bis
 *        zum Prompt.
 *
 * Ablauf und Belege: doc/k8915g2/zre_rom.md §4 (Selbsttest ROM → KEY → CTC → SIO → RAM,
 * Statuszeile wie V3: „DIAGNOSTIC“ bei 1740H, Testname 1770H, Kennbuchstabe 1776H) und §5
 * (Bootablauf).  **F9 (gelöst 2026-10-05):** der Abzug 177 trägt die Summe 00A67CH,
 * errechnet wird 00A680H (gekipptes Bit bei 0A33H).  Die Vorgabe des Kerns ist die
 * REPARIERTE Fassung (0A33H = 00H) — der Selbsttest läuft durch.  Den Abzug, wie er ist
 * (Selbsttestfehler „ROM“ mit `C`), setzt nur der Fall AbzugRomFehlerC… über
 * `Config::gen2_rom`; der Repo-Abzug bleibt unverändert.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/cards/k2521/rom_k8915g2.h"
#include "core/machines/k8915/k8915.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"
#include "tests/support/temp_path.h"

using k1520test::TempDisk;
using k1520test::vramLines;
using k1520test::vramText;

namespace {

constexpr int kZeile        = 23;                   // 1740H/1770H/1776H: letzte Zeile
constexpr int kSpalteTest   = 0x770 - kZeile * 80;  // 1770H → Spalte 64
constexpr int kSpalteFehler = 0x776 - kZeile * 80;  // 1776H → Spalte 70
constexpr long long kFrist  = 60'000'000;
constexpr int kSchritt      = 20'000;

/// @p abzug: statt der Vorgabe (repariertes 177) den unveränderten Abzug (F9, 0A33H = 04H).
K8915Machine::Config gen2(bool abzug = false) {
    K8915Machine::Config c;
    c.generation = K8915Machine::Generation::Gen2;
    if (abzug) c.gen2_rom = K8915G2_ZRE_ROM;
    return c;
}

char zeichen(K8915Machine& m, int col) {
    return static_cast<char>(m.screen().vramRead(col, kZeile) & 0x7F);
}

std::string testname(K8915Machine& m) {
    std::string s;
    for (int i = 0; i < 3; ++i) s.push_back(zeichen(m, kSpalteTest + i));
    return s;
}

bool bekannterName(const std::string& n) {
    for (const char* k : {"ROM", "KEY", "CTC", "SIO", "RAM"})
        if (n == k) return true;
    return false;
}

bool enthaelt(K8915Machine& m, const std::string& s) {
    return vramText(m).find(s) != std::string::npos;
}

bool coldstart(K8915Machine& m) { return enthaelt(m, "* Coldstart *  Disk on A: ready"); }

/// Läuft, bis @p text im Bild steht; false nach @p frist Takten.
bool bis(K8915Machine& m, const std::string& text, long long frist, int schritt = kSchritt) {
    for (long long t = 0; t < frist; t += m.run(schritt))
        if (enthaelt(m, text)) return true;
    return enthaelt(m, text);
}

struct Verlauf {
    std::vector<std::string> namen;   ///< Folge der angezeigten Testnamen
    char fehler = ' ';                ///< Kennbuchstabe bei 1776H im Endzustand
    std::string fehlerBei;            ///< Testname, unter dem der Fehler stand
    long long takte = -1;
};

/// Läuft bis Fehlerbuchstabe oder Kaltstartmeldung (oder Frist); zeichnet die Namen auf.
Verlauf selbsttest(K8915Machine& m) {
    Verlauf v;
    long long done = 0;
    while (done < kFrist) {
        done += m.run(kSchritt);
        if (coldstart(m)) { v.takte = done; break; }
        const std::string n = testname(m);
        if (bekannterName(n) && (v.namen.empty() || v.namen.back() != n)) v.namen.push_back(n);
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

/// Letzte nicht leere Bildzeile, rechts ohne Leerzeichen.
std::string letzteZeile(K8915Machine& m) {
    const std::string t = vramText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        while (!zeile.empty() && (zeile.back() == ' ' || zeile.back() == '\0')) zeile.pop_back();
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Vorgabe-ROM: Selbsttest ohne Fehler, ohne Tastendruck zur Kaltstartmeldung.
void selbsttestZurKaltstartmeldung(K8915Machine& m) {
    const Verlauf v = selbsttest(m);
    ASSERT_EQ(v.fehler, ' ') << "Fehler '" << v.fehler << "' unter " << v.fehlerBei << "\n"
                             << vramLines(m);
    ASSERT_TRUE(coldstart(m)) << folge(v.namen) << "\n" << vramLines(m);
}

}  // namespace

/**
 * @test K8915Gen2Boot.AbzugRomFehlerCDannCrZurKaltstartmeldung
 * @brief Der Abzug, wie gelesen (F9, per `Config::gen2_rom` gesetzt — NICHT die Vorgabe):
 *        der Selbsttest endet bei „ROM“ mit `C` (Baustein 3 = 177), ERROR-Lampe
 *        (61H = 7FH), 16 × BEL; `CR` ⇒ „\* Coldstart \*“ + „Disk on A: ready“.
 */
TEST(K8915Gen2Boot, AbzugRomFehlerCDannCrZurKaltstartmeldung)
{
    K8915Machine m(gen2(true));
    ASSERT_EQ(m.generation(), K8915Machine::Generation::Gen2);
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << "PC=" << std::hex << m.cpuPC() << " A8H=" << int(m.ops().reg())
                          << " Folge: " << folge(v.namen) << "\n" << vramLines(m);
    EXPECT_EQ(v.fehlerBei, "ROM") << folge(v.namen) << "\n" << vramLines(m);
    EXPECT_EQ(v.fehler, 'C') << "Summe 177: 00A680H statt 00A67CH\n" << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "DIAGNOSTIC")) << vramLines(m);
    EXPECT_FALSE(coldstart(m));

    m.run(4'000'000);
    EXPECT_EQ(m.ats().anzeige(), 0x7F) << "ERROR-Lampe";
    EXPECT_EQ(m.panelLamps(), 0x7F);
    EXPECT_GE(m.bellCount(), 16u) << "16 × BEL";

    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 3'000'000, 5'000)) << vramLines(m);
    EXPECT_EQ(m.ats().anzeige(), 0xB0) << "0907H meldet mit 61H = B0H";
    // 0EH, nicht 06H: der Stub (0400H) hat vor dem Kopierer 0EH gesetzt, und erst der
    // RAM-Test schaltet über 87H auf 06H — den überspringt der ROM-Fehler.
    EXPECT_EQ(m.ops().reg(), 0x0E) << "ROM-Fehler: kein RAM-Test, A8H bleibt 0EH vom Stub";
}

/**
 * @test K8915Gen2Boot.VorgabeSelbsttestFehlerfreiBisColdstart
 * @brief Vorgabe-ROM (repariertes 177, 0A33H = 00H, F9 gelöst): ROM → KEY → CTC → SIO → RAM
 *        ohne Fehlerbuchstaben, ohne Tastendruck zur Kaltstartmeldung; A8H am Ende 06H
 *        (FDDC/FDE2 nach dem RAM-Test).  KEY: zwei `DC1` der K7672; CTC: drei CTCs (K2521,
 *        ATS-CTC1, ATS-CTC2) quittiert, A = 4; SIO: Echo auf drei Kanälen, drei Runden;
 *        RAM: 64 KB durchgehend (87H), auch unter ROM, K2521-RAM und Bild.
 */
TEST(K8915Gen2Boot, VorgabeSelbsttestFehlerfreiBisColdstart)
{
    K8915Machine m(gen2());
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << "PC=" << std::hex << m.cpuPC() << " Folge: " << folge(v.namen)
                          << "\n" << vramLines(m);
    EXPECT_EQ(v.fehler, ' ') << "Fehler '" << v.fehler << "' unter " << v.fehlerBei << "\n"
                             << vramLines(m);
    const std::vector<std::string> soll = {"ROM", "KEY", "CTC", "SIO", "RAM"};
    EXPECT_EQ(v.namen, soll) << "Folge: " << folge(v.namen);
    EXPECT_TRUE(coldstart(m)) << vramLines(m);
    EXPECT_EQ(m.ops().reg(), 0x06);
    EXPECT_EQ(m.keyboard().selbsttests(), 3u) << "Einschalten, ESC c, ESC [2;1y";
    EXPECT_FALSE(m.keyboard().sendenGesperrt()) << "zum Schluss DC1 an die Tastatur";
    EXPECT_EQ(m.ats().anzeige(), 0xB0);
}

/**
 * @test K8915Gen2Boot.OhnePruefsteckerScheitertSio
 * @brief Ohne Rückschleife auf SIO1-A, SIO1-B, SIO2-A: Fehler unter „SIO“, Bitmaske
 *        111B | 40H = `G` (wie V3).  Danach `CR` ⇒ Kaltstartmeldung.
 */
TEST(K8915Gen2Boot, OhnePruefsteckerScheitertSio)
{
    K8915Machine::Config c = gen2();
    c.pruefstecker = false;
    K8915Machine m(c);
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << vramLines(m);
    const std::vector<std::string> soll = {"ROM", "KEY", "CTC", "SIO"};
    EXPECT_EQ(v.namen, soll) << "Folge: " << folge(v.namen);
    EXPECT_EQ(v.fehlerBei, "SIO") << vramLines(m);
    EXPECT_EQ(v.fehler, 'G') << vramLines(m);
    m.run(4'000'000);
    EXPECT_EQ(m.keyboard().summerZaehler(), 16u);
    m.keyboard().sendeZeichen(0x0D);
    EXPECT_TRUE(bis(m, "* Coldstart *", 3'000'000, 5'000)) << vramLines(m);
}

/**
 * @test K8915Gen2Boot.OhneTastaturScheitertKeyMitA
 * @brief Ohne K7672 bekommt `ESC c` kein `DC1` ⇒ `A` unter „KEY“; ROM besteht davor.
 */
TEST(K8915Gen2Boot, OhneTastaturScheitertKeyMitA)
{
    K8915Machine::Config c = gen2();
    c.tastatur = false;
    K8915Machine m(c);
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_GE(v.takte, 0) << vramLines(m);
    const std::vector<std::string> soll = {"ROM", "KEY"};
    EXPECT_EQ(v.namen, soll) << "Folge: " << folge(v.namen);
    EXPECT_EQ(v.fehlerBei, "KEY") << vramLines(m);
    EXPECT_EQ(v.fehler, 'A') << vramLines(m);
}

/**
 * @test K8915Gen2Boot.SystemImRamFuehrtZumLader
 * @brief `C3` bei 0000H und 0005H im K3528-RAM, Reset: der Stub bei FFE0H (8FH) meldet
 *        „System im RAM“, A8H = 0EH, und das ROM geht **ohne** Selbsttest zur
 *        Kaltstartmeldung (zre_rom.md §5 Schritt 2/3).
 */
TEST(K8915Gen2Boot, SystemImRamFuehrtZumLader)
{
    K8915Machine m(gen2());
    m.powerOn();
    m.ops().ramPoke(0x0000, 0xC3);
    m.ops().ramPoke(0x0005, 0xC3);
    m.reset();
    EXPECT_EQ(m.ops().reg(), 0x00) << "/RESET ⇒ A8H := 00H";
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "DIAGNOSTIC")) << "kein Selbsttest erwartet\n" << vramLines(m);
    EXPECT_EQ(m.ops().reg(), 0x0E) << "Weg über den Stub: A8H = 0EH";
}

/**
 * @test K8915Gen2Boot.EOderEscCStartetNeu
 * @brief An der Kaltstartmeldung führen `E` und `ESC c` zu `JP 03F3H` = Neubeginn bei
 *        0400H (neu gegenüber V3, ZRE 0956–096B); ohne System im RAM ⇒ Selbsttest
 *        erscheint wieder.
 */
TEST(K8915Gen2Boot, EOderEscCStartetNeu)
{
    for (const std::vector<uint8_t>& tasten : {std::vector<uint8_t>{'E'},
                                               std::vector<uint8_t>{0x1B, 'c'}}) {
        SCOPED_TRACE(tasten.size() == 1 ? "E" : "ESC c");
        K8915Machine m(gen2());
        m.powerOn();
        selbsttestZurKaltstartmeldung(m);
        ASSERT_FALSE(enthaelt(m, "DIAGNOSTIC")) << "Kaltstart löscht das Bild";
        for (uint8_t t : tasten) m.keyboard().sendeZeichen(t);
        EXPECT_TRUE(bis(m, "DIAGNOSTIC", 5'000'000, 5'000)) << vramLines(m);
    }
}

/**
 * @test K8915Gen2Boot.NmiImRomWirkungslos
 * @brief 0066H = `RETN` im ROM 175: ein NMI während des Selbsttests (unter „KEY“) wird
 *        angenommen und ändert nichts — der Selbsttest läuft fehlerfrei bis zur
 *        Kaltstartmeldung (V3: Selbsttest von vorn).
 */
TEST(K8915Gen2Boot, NmiImRomWirkungslos)
{
    K8915Machine m(gen2());
    m.powerOn();
    int nmi_einsprung = 0;
    m.setCpuTraceCallback([&](const Z80& c) { if (c.PC == 0x0066) ++nmi_einsprung; });
    for (long long t = 0; t < kFrist && testname(m) != "KEY"; t += m.run(kSchritt)) {}
    ASSERT_EQ(testname(m), "KEY") << vramLines(m);
    const uint8_t lampen = m.ats().anzeige();
    m.nmi();
    m.run(200);
    EXPECT_EQ(nmi_einsprung, 1) << "NMI angenommen (0066H)";
    EXPECT_EQ(m.ats().anzeige(), lampen) << "kein OUT (61H) im NMI-Weg";

    Verlauf v = selbsttest(m);
    EXPECT_EQ(v.fehler, ' ') << "Fehler '" << v.fehler << "' unter " << v.fehlerBei << "\n"
                             << vramLines(m);
    EXPECT_TRUE(coldstart(m)) << vramLines(m);
    EXPECT_EQ(nmi_einsprung, 1);
}

/**
 * @test K8915Gen2Boot.RamTestSiehtUnterRomUndBild
 * @brief Der RAM-Test (A8H = 87H) legt `ED 45` (RETN) nach 0066H — ins K3528-RAM UNTER
 *        dem ROM; danach ist das Bild wieder eingeblendet (06H) und der ROM-Inhalt bei
 *        0066H ist unverändert sichtbar.  Wächter für Bit0/Bit7 der K3528 und die
 *        ausgeblendete K7024 bei 87H (zerstörungsfreier Test über 0068–FDC7H).
 */
TEST(K8915Gen2Boot, RamTestSiehtUnterRomUndBild)
{
    K8915Machine m(gen2());
    m.powerOn();
    const Verlauf v = selbsttest(m);
    ASSERT_TRUE(coldstart(m)) << "Fehler '" << v.fehler << "' unter " << v.fehlerBei << "\n"
                              << vramLines(m);
    EXPECT_EQ(m.ops().ramPeek(0x0066), 0xED);
    EXPECT_EQ(m.ops().ramPeek(0x0067), 0x45);
    EXPECT_EQ(m.ops().ortVon(0x0066), K3528::Quelle::Zre) << "06H: ROM wieder bei 0000H";
    EXPECT_EQ(m.ops().ortVon(0x1000), K3528::Quelle::Bus) << "06H: Bild bei 1000H";
    EXPECT_EQ(m.memReadDebug(0x0066), 0xED) << "ROM 175 hat bei 0066H selbst ED 45";
}

/**
 * @test K8915Gen2Scpx.LaedtDieV3SystemdisketteBisZumPrompt
 * @brief Diskette 901 (V3-System SCPX 8915 V5.3, `TempDisk`): Selbsttest ohne Fehler →
 *        Kaltstartmeldung → `CR` → der mit dem V3 byteidentische Lader liest C000–EFEFH
 *        und springt nach D600H → stabiles `A>`; danach listet `dir` die Diskette.
 *        **[?] F13:** der Autostart `rade` (Bank 2) darf scheitern — geprüft wird der
 *        Prompt danach, nicht RADE.
 */
TEST(K8915Gen2Scpx, LaedtDieV3SystemdisketteBisZumPrompt)
{
    TempDisk a("k8915scpx_boot1.hfe");
    K8915Machine m(gen2());
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    m.powerOn();
    selbsttestZurKaltstartmeldung(m);
    if (HasFatalFailure()) return;

    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "SCPX 8915", 150'000'000, 100'000)) << vramLines(m);
    // Stabiles „A>“: nach dem Autostart mehrfach hintereinander die letzte Zeile.
    int stabil = 0;
    for (long long t = 0; t < 200'000'000 && stabil < 20; t += m.run(100'000))
        stabil = (letzteZeile(m) == "A>" && !m.keyboard().sendetNoch() &&
                  m.memReadDebug(0xF150) == 0) ? stabil + 1 : 0;
    ASSERT_GE(stabil, 20) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "A>rade")) << "Autostart aus dem Tastaturpuffer\n" << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "SCPX 8915  V 5.3")) << vramLines(m);
    EXPECT_EQ(m.ops().reg(), 0x87) << "BIOS im Betrieb: alles RAM";
    // Beobachtet (2026-10-05): RADE meldet „no RAM-device configurated or fatal RAM-error“
    // — keine Bank 2 (F13).  Bewusst NICHT geprüft: das Ergebnis ist eine offene Frage.

    for (char ch : std::string("dir")) m.keyboard().sendeZeichen(static_cast<uint8_t>(ch));
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "A>dir", 10'000'000, 100'000)) << vramLines(m);
    for (long long t = 0; t < 60'000'000 && letzteZeile(m) != "A>"; t += m.run(100'000)) {}
    EXPECT_EQ(letzteZeile(m), "A>") << vramLines(m);
    for (const char* n : {"A: RADE     COM", "DISGEN   COM", "FORMAT   COM", "TPINSCPA COM",
                          "***901   VOL"})
        EXPECT_TRUE(enthaelt(m, n)) << n << "\n" << vramLines(m);
}
