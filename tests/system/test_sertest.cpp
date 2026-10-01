/**
 * @file test_sertest.cpp
 * @brief Sertest.* — SERTEST.COM (Serial Test, Entwurf 19 §14) unter CP/A (A5120) und
 *        SCPX 8915 V5.3 (K8915), AP-ST2: Erkennung + Schnittstellenliste, Kurzhilfe,
 *        Ergebniszeilen, Ctrl+C hinterlässt ein bedienbares System.
 *
 * Gerüst und Hilfen: `tests/system/sertest_hilfen.h`.  Jeder Fall bootet seine eigene
 * Maschine (ctest startet jeden Fall als eigenen Prozess — ein in der Suite geteilter
 * Boot spart dort nichts).
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/serial/hub.h"
#include "tests/system/sertest_hilfen.h"

using namespace sertest;
using k1520::serial::SerialStatus;

namespace {

constexpr long long kFrist = 100'000'000;   // ≈ 40 s Maschinenzeit

/// `sertest` starten, bis zur T/G-Frage.
template <class S>
::testing::AssertionResult startBisRolle(S& s, const std::string& cmd = "sertest") {
    if (!s.fehler().empty()) return ::testing::AssertionFailure() << s.fehler();
    if (!s.kaltstart()) return ::testing::AssertionFailure() << "kein Prompt\n" << s.bild();
    s.tippe(cmd + "\r");
    if (!s.bis("T/G", kFrist)) return ::testing::AssertionFailure() << "keine T/G-Frage\n" << s.bild();
    return ::testing::AssertionSuccess();
}

/// Kopf, Rechner, alle Listeneinträge, Tastatur gekennzeichnet.
template <class S>
void pruefeListe(S& s, const std::string& rechner, const std::vector<std::string>& eintraege,
                 const std::string& tastatur) {
    ASSERT_TRUE(startBisRolle(s));
    const std::string b = s.bild();
    EXPECT_TRUE(hatZeile(b, "Serial Test V0.1  (c) 2026 Olaf Krieger")) << b;
    EXPECT_TRUE(hatZeile(b, "Rechner: " + rechner))
        << "Erkennung ohne /M: (und ohne „(vorgegeben)“)\n" << b;
    EXPECT_TRUE(hatZeile(b, "Schnittstellen:")) << b;
    for (const std::string& e : eintraege) EXPECT_TRUE(hatZeile(b, e)) << e << "\n" << b;
    EXPECT_TRUE(hatZeile(b, tastatur)) << b;
    // Ctrl+C an der Rollenfrage beendet sauber.
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}

template <class S>
void pruefeKurzhilfe(S& s) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest x\r");
    ASSERT_TRUE(s.bis("Aufruf:", kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << "Kurzhilfe ⇒ Ende\n" << s.bild();
    const std::string b = s.bild();
    EXPECT_NE(b.find("SERTEST T n [/P] [/G] [/A]"), std::string::npos) << b;
    EXPECT_NE(b.find("/M:A bzw. /M:K"), std::string::npos) << b;
    EXPECT_EQ(b.find("T/G"), std::string::npos) << "keine Rollenfrage\n" << b;
    EXPECT_TRUE(s.protokoll().zeilen().empty()) << s.protokoll().text();
}

/// Ctrl+C an der T/G-Frage, danach `DIR`.
template <class S>
void pruefeCtrlCRolle(S& s) {
    ASSERT_TRUE(startBisRolle(s));
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// `sertest g <n>` (Gegenstelle läuft bis Ctrl+C), dann Ctrl+C, danach `DIR`.
template <class S>
void pruefeCtrlCGegenstelle(S& s, const std::string& schnittstelle) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest g 1\r");
    ASSERT_TRUE(s.bis("Gegenstelle an " + schnittstelle + " bereit.", kFrist)) << s.bild();
    // Sie läuft weiter, bis Ctrl+C kommt — kein Prompt in dieser Zeit.
    for (int i = 0; i < 100; ++i) s.lauf();
    EXPECT_NE(letzteZeile(s.bild()), "A>") << s.bild();
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// `sertest t <n> /a`: Ergebniszeilen nach §14.3 bis `SERTEST ENDE`.  Die Prüfschritte
/// sind noch Platzhalter (ST1: `FEHLER NICHT EINGEBAUT`) — geprüft wird das Format, und
/// dass ein für IFSS nicht geltender Teil `ENTFAELLT` meldet.
template <class S>
void pruefeErgebniszeilen(S& s, const std::string& nr, const std::string& ifss) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest t " + nr + " /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    const auto& p = s.protokoll();
    for (const char* teil : {"DATEN-LOOP", "LEITUNGEN-LOOP", "LEITUNGEN", "ECHO", "FLUSS-HW", "FLUSS-XON"})
        EXPECT_TRUE(p.wert(ifss, teil)) << teil << "\n" << p.text();
    EXPECT_EQ(p.wert(ifss, "LEITUNGEN-LOOP").value_or("-"), "ENTFAELLT") << p.text();
    EXPECT_EQ(p.wert(ifss, "FLUSS-HW").value_or("-"), "ENTFAELLT") << p.text();
    for (const Zeile& z : p.zeilen()) {
        if (z.teil == "ENDE") continue;
        EXPECT_EQ(z.name, ifss) << z.roh;
        EXPECT_TRUE(z.wert == "OK" || z.wert == "ENTFAELLT" || z.wert.rfind("FEHLER ", 0) == 0) << z.roh;
    }
    // Solange ein Teil nicht eingebaut ist, darf das Ende nicht OK heißen.
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

// ─── SIO-/CTC-Schicht (AP-ST3) ───────────────────────────────────────────────

/// Format und Leitungen einer Schnittstelle, wie der Wandler sie aus SIO/CTC liest.
std::string format(const SerialStatus& st) {
    char b[80];
    std::snprintf(b, sizeof b, "%u %d%c%s%s rts=%d dtr=%d", st.baud_nenn, st.daten,
                  "NOE?"[st.paritaet & 3], st.stopp_halbe == 2 ? "1" : st.stopp_halbe == 4 ? "2" : "1,5",
                  st.format_gueltig ? "" : " (ungueltig)", st.rts, st.dtr);
    return b;
}

template <class S>
SerialStatus hubStatus(S& s, int i) { return s.maschine().serialHub()->status(i); }

/// `sertest g <n>` für jede Schnittstelle nacheinander: während der Gegenstelle steht der
/// Kanal auf 9600 8N1 (V.24 zusätzlich mit RTS und DTR — an IFSS meldet der Wandler keine
/// Leitungen), nach Ctrl+C auf der BIOS-Vorgabe @p vorgabe[n-1] (leer = unverändert
/// gegenüber vorher).  Zuletzt `DIR` — die Tastatur lebt noch.
template <class S>
void pruefeProgrammierungUndVorgabe(S& s, const std::vector<std::string>& namen,
                                    const std::vector<bool>& v24,
                                    const std::vector<std::string>& vorgabe) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    for (size_t i = 0; i < namen.size(); ++i) {
        SCOPED_TRACE("Schnittstelle " + std::to_string(i + 1) + " " + namen[i]);
        const std::string vorher = format(hubStatus(s, static_cast<int>(i)));
        s.tippe("sertest g " + std::to_string(i + 1) + "\r");
        ASSERT_TRUE(s.bis("Gegenstelle an " + namen[i] + " bereit.", kFrist)) << s.bild();
        for (int k = 0; k < 20; ++k) s.lauf();
        EXPECT_EQ(format(hubStatus(s, static_cast<int>(i))),
                  v24[i] ? "9600 8N1 rts=1 dtr=1" : "9600 8N1 rts=0 dtr=0");
        s.ctrlC();
        ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
        const std::string nachher = format(hubStatus(s, static_cast<int>(i)));
        EXPECT_EQ(nachher, vorgabe[i].empty() ? vorher : vorgabe[i]) << "vorher: " << vorher;
    }
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// `sertest g <n>`: ein Zeichen von außen löst den Empfangsinterrupt aus
/// (`SERTEST INTERRUPT OK`); nach Ctrl+C ist die Vektortabelle (I·256) wieder wie vorher
/// und das System bedienbar.  @p seite = I-Register des BIOS.
template <class S>
void pruefeInterrupt(S& s, int nr, const std::string& name, uint16_t seite) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    auto* hub = s.maschine().serialHub();
    const int i = nr - 1;
    auto k = hub->konfig(i);
    k.loop = false;   // K8915 startet mit Loop
    ASSERT_TRUE(hub->konfigurieren(i, k));
    auto tabelle = [&] {
        std::vector<uint8_t> t;
        for (int a = 0; a < 256; ++a) t.push_back(s.maschine().memReadDebug(static_cast<uint16_t>(seite + a)));
        return t;
    };
    const auto vorher = tabelle();

    s.tippe("sertest g " + std::to_string(nr) + "\r");
    ASSERT_TRUE(s.bis("Gegenstelle an " + name + " bereit.", kFrist)) << s.bild();
    for (int r = 0; r < 20; ++r) s.lauf();
    EXPECT_NE(tabelle(), vorher) << "eingehängt";
    EXPECT_FALSE(s.protokoll().wert("", "INTERRUPT")) << s.protokoll().text();
    auto& w = hub->wandler(i);
    w.anbinden();
    const uint8_t zeichen[] = {'x', 'y', 0x00, 0xFF};
    ASSERT_EQ(w.fernGib(zeichen, sizeof zeichen), sizeof zeichen);
    ASSERT_TRUE(s.bis("SERTEST INTERRUPT OK", kFrist)) << s.bild();
    for (int r = 0; r < 200 && hubStatus(s, i).bytes_empfangen < sizeof zeichen; ++r) s.lauf();
    EXPECT_EQ(hubStatus(s, i).bytes_empfangen, sizeof zeichen);

    // Ctrl+C erst, wenn das Bild steht: das SCPX-BIOS rollt unter DI (≈ 39 000 Takte),
    // vier Tastaturbytes in dieser Zeit liefen auch am Gerät über (Strg bliebe „unten").
    for (int r = 0; r < 100; ++r) s.lauf();
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(s.protokoll().wert("", "INTERRUPT").value_or("-"), "OK") << s.protokoll().text();
    EXPECT_EQ(tabelle(), vorher) << "Vektortabelle nach Ctrl+C wie vor dem Programm";
    // Ein weiteres Zeichen nach dem Aushängen darf nichts mehr auslösen.
    const uint8_t noch[] = {'z'};
    w.fernGib(noch, 1);
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
    w.abbinden();
}

}  // namespace

// ─── Zerlegen der Ergebniszeilen (ohne Maschine) ─────────────────────────────

TEST(Sertest, ZerlegtErgebniszeilen) {
    auto a = zerlege("SERTEST DFUE/V.24 DATEN-LOOP: FEHLER NICHT EINGEBAUT");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->name, "DFUE/V.24");
    EXPECT_EQ(a->teil, "DATEN-LOOP");
    EXPECT_EQ(a->wert, "FEHLER NICHT EINGEBAUT");
    auto e = zerlege("SERTEST ENDE OK");
    ASSERT_TRUE(e);
    EXPECT_EQ(e->name, "");
    EXPECT_EQ(e->teil, "ENDE");
    EXPECT_EQ(e->wert, "OK");
    EXPECT_FALSE(zerlege("  SERTEST T n [/P] [/G] [/A]   Tester an Schnittstelle n"));
    EXPECT_FALSE(zerlege("A>sertest t 1 /a"));

    // Gilt erst nach kStabil Takten ununterbrochen im Bild, dann genau einmal;
    // ein Zwischenstand (halb geschrieben, beim Rollen zerrissen) gilt nie.
    constexpr long long T = SertestProtokoll::kStabil;
    SertestProtokoll p;
    p.erfasse("SERTEST Drucker ECHO: OK\nxyz\nSERTEST ENDE FEH\n", 0);
    EXPECT_TRUE(p.zeilen().empty());
    p.erfasse("SERTEST Drucker ECHO: OK\nSERTEST ENDE FEHLER\n", T / 2);
    EXPECT_TRUE(p.zeilen().empty());
    p.erfasse("SERTEST Drucker ECHO: OK\nSERTEST ENDE FEHLER\n", T);
    EXPECT_EQ(p.zeilen().size(), 1u) << p.text();
    p.erfasse("xyz\nSERTEST Drucker ECHO: OK\nSERTEST ENDE FEHLER\n", 3 * T / 2);
    EXPECT_EQ(p.zeilen().size(), 2u) << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER");
    EXPECT_FALSE(p.wert("", "ENDE FEH")) << p.text();
}

// ─── A5120 / CP/A ────────────────────────────────────────────────────────────

TEST(Sertest, A5120_ErkenntRechnerUndListetSchnittstellen) {
    SertestA5120 s;
    pruefeListe(s, "A5120 (K8025)",
                {"  1  DFUE/V.24      SIO A33 Kanal A   V.24",
                 "  2  DFUE/IFSS      SIO A33 Kanal B   IFSS",
                 "  3  Drucker        SIO A32 Kanal B   IFSS"},
                "  -  Tastatur K7637 SIO A32 Kanal A   (Tastatur)");
}

TEST(Sertest, A5120_FalscheKommandozeileGibtKurzhilfe) {
    SertestA5120 s;
    pruefeKurzhilfe(s);
}

TEST(Sertest, A5120_CtrlCAnDerRollenfrageLaesstSystemBedienbar) {
    SertestA5120 s;
    pruefeCtrlCRolle(s);
}

TEST(Sertest, A5120_CtrlCInDerGegenstelleLaesstSystemBedienbar) {
    SertestA5120 s;
    pruefeCtrlCGegenstelle(s, "DFUE/V.24");
}

TEST(Sertest, A5120_TesterAutomatikLiefertErgebniszeilen) {
    SertestA5120 s;
    pruefeErgebniszeilen(s, "2", "DFUE/IFSS");
}

TEST(Sertest, A5120_GegenstelleProgrammiert9600_8N1UndStelltDieBiosVorgabeHer) {
    SertestA5120 s;
    pruefeProgrammierungUndVorgabe(s, {"DFUE/V.24", "DFUE/IFSS", "Drucker"}, {true, false, false},
                                   {"9600 8N1 rts=1 dtr=1", "", "9600 7O1 rts=0 dtr=0"});
}

TEST(Sertest, A5120_DruckerEmpfaengtImInterruptNebenDerTastatur) {
    SertestA5120 s;
    pruefeInterrupt(s, 3, "Drucker", 0xF700);
}

TEST(Sertest, A5120_DfueV24EmpfaengtImInterrupt) {
    SertestA5120 s;
    pruefeInterrupt(s, 1, "DFUE/V.24", 0xF700);
}

// ─── K8915 / SCPX 8915 ───────────────────────────────────────────────────────

TEST(Sertest, K8915_ErkenntRechnerUndListetSchnittstellen) {
    SertestK8915 s;
    pruefeListe(s, "K8915 (K7028)",
                {"  1  Drucker/IFSS1  SIO1 Kanal B      IFSS",
                 "  2  V.24           SIO1 Kanal A      V.24",
                 "  3  DFUE/IFSS2     SIO2 Kanal A      IFSS"},
                "  -  Tastatur K7672 SIO2 Kanal B      (Tastatur)");
}

TEST(Sertest, K8915_FalscheKommandozeileGibtKurzhilfe) {
    SertestK8915 s;
    pruefeKurzhilfe(s);
}

TEST(Sertest, K8915_CtrlCAnDerRollenfrageLaesstSystemBedienbar) {
    SertestK8915 s;
    pruefeCtrlCRolle(s);
}

TEST(Sertest, K8915_CtrlCInDerGegenstelleLaesstSystemBedienbar) {
    SertestK8915 s;
    pruefeCtrlCGegenstelle(s, "Drucker/IFSS1");
}

TEST(Sertest, K8915_TesterAutomatikLiefertErgebniszeilen) {
    SertestK8915 s;
    pruefeErgebniszeilen(s, "1", "Drucker/IFSS1");
}

TEST(Sertest, K8915_GegenstelleProgrammiert9600_8N1UndStelltDieBiosVorgabeHer) {
    SertestK8915 s;
    pruefeProgrammierungUndVorgabe(s, {"Drucker/IFSS1", "V.24", "DFUE/IFSS2"}, {false, true, false},
                                   {"9600 7O1 rts=0 dtr=0", "", ""});
}

TEST(Sertest, K8915_DfueIfss2EmpfaengtImInterruptNebenDerTastatur) {
    SertestK8915 s;
    pruefeInterrupt(s, 3, "DFUE/IFSS2", 0xFF00);
}

TEST(Sertest, K8915_V24EmpfaengtImInterrupt) {
    SertestK8915 s;
    pruefeInterrupt(s, 2, "V.24", 0xFF00);
}
