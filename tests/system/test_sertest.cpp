/**
 * @file test_sertest.cpp
 * @brief Sertest.* — SERTEST.COM (Serial Test, Entwurf 19 §14) unter CP/A (A5120) und
 *        SCPX 8915 V5.3 (K8915), AP-ST2: Erkennung + Schnittstellenliste, Kurzhilfe,
 *        Ergebniszeilen, Ctrl+C hinterlässt ein bedienbares System; AP-ST3: SIO-/CTC-
 *        Schicht, Interrupt; AP-ST4: Prüfsteckertest (DATEN-LOOP, LEITUNGEN-LOOP) gegen
 *        den Rx/Tx-Loop, Gegenfall ohne Loop, Ctrl+C mitten im DATEN-LOOP.
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

#ifndef K1520_SERTEST_V02
#error "K1520_SERTEST_V02 fehlt (tests/fixtures/cpm/SERTEST_V02.COM)"
#endif

using namespace sertest;
using k1520::serial::SerialStatus;

namespace {

std::string format(const SerialStatus& st);

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
    EXPECT_TRUE(hatZeile(b, "Serial Test V0.3  (c) 2026 Olaf Krieger")) << b;
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
    EXPECT_NE(b.find("/M:A, /M:K, /M:P, /M:W, /M:R bzw. /M:S"), std::string::npos) << b;
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

/// `sertest t <n> /a`: Ergebniszeilen nach §14.3 bis `SERTEST ENDE`.  Ohne Gegenstelle
/// (A5120: kein Kabel; K8915: Loop an — die eigene Ankündigung kommt zurück und darf
/// nicht als Bestätigung gelten) melden ECHO und FLUSS-XON den Zeitüberlauf der
/// Bestätigung; der DATEN-LOOP hängt am Loop der Maschine (A5120 aus, K8915 an).
/// Geprüft wird das Format, und dass ein für IFSS nicht geltender Teil `ENTFAELLT`
/// meldet.  Das Ende bleibt `FEHLER` — begründet, nicht vorläufig: die
/// Gegenstellenteile können ohne zweiten Rechner nicht gelingen (seit AP-ST6 gibt es
/// keinen Platzhalter mehr; `ENDE OK` prüft `SertestKopplung.*`).
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
    EXPECT_EQ(p.wert(ifss, "LEITUNGEN").value_or("-"), "ENTFAELLT") << p.text();
    for (const Zeile& z : p.zeilen()) {
        if (z.teil == "ENDE") continue;
        EXPECT_EQ(z.name, ifss) << z.roh;
        EXPECT_TRUE(z.wert == "OK" || z.wert == "ENTFAELLT" || z.wert.rfind("FEHLER ", 0) == 0) << z.roh;
    }
    // Ohne Gegenstelle: Zeitüberlauf der Bestätigung, also Ende FEHLER.
    EXPECT_EQ(p.wert(ifss, "ECHO").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.wert(ifss, "FLUSS-XON").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    for (const Zeile& z : p.zeilen()) EXPECT_EQ(z.wert.find("NICHT EINGEBAUT"), std::string::npos) << z.roh;
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

// ─── Prüfsteckertest (AP-ST4) ────────────────────────────────────────────────

/// Rx/Tx-Loop (= Prüfstecker, §6.5) an allen Schnittstellen der Maschine setzen.
template <class S>
void loopAlle(S& s, bool an) {
    auto* hub = s.maschine().serialHub();
    for (int i = 0; i < hub->anzahl(); ++i) {
        auto k = hub->konfig(i);
        k.loop = an;
        ASSERT_TRUE(hub->konfigurieren(i, k)) << i;
    }
}

/// Erwartete Rohzeile des LEITUNGEN-LOOP je Kombination (RTS, DTR) → (CTS, DCD).
std::string leitungsZeile(int rts, int dtr, int cts, int dcd) {
    char b[80];
    std::snprintf(b, sizeof b, "  RTS=%d DTR=%d  CTS=%d DCD=%d  erwartet  CTS=%d DCD=%d  RR0=", rts,
                  dtr, cts, dcd, cts, dcd);
    return b;
}

/// Mit Loop: `sertest t <n> /p /a` an jeder Schnittstelle nacheinander → DATEN-LOOP OK,
/// LEITUNGEN-LOOP OK (V.24, Rohzeilen nach der Erwartungstabelle @p erwartet = CTS/DCD je
/// Kombination 00, 10, 01, 11) bzw. ENTFAELLT, Ende OK.  Danach `DIR`: am A5120 teilt der
/// Drucker die SIO mit der Tastatur.
template <class S>
void pruefeLoopAlle(S& s, const std::vector<std::string>& namen, const std::vector<bool>& v24,
                    const int (&erwartet)[4][2]) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);
    for (size_t i = 0; i < namen.size(); ++i) {
        SCOPED_TRACE("Schnittstelle " + std::to_string(i + 1) + " " + namen[i]);
        ASSERT_TRUE(s.neuerLauf()) << s.bild();
        s.tippe("sertest t " + std::to_string(i + 1) + " /p /a\r");
        ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
        const std::string b = s.bild();
        ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
        const auto& p = s.protokoll();
        EXPECT_EQ(p.wert(namen[i], "DATEN-LOOP").value_or("-"), "OK") << p.text() << b;
        EXPECT_EQ(p.wert(namen[i], "LEITUNGEN-LOOP").value_or("-"), v24[i] ? "OK" : "ENTFAELLT")
            << p.text() << b;
        EXPECT_FALSE(p.wert(namen[i], "ECHO")) << "nur /P\n" << p.text();
        EXPECT_EQ(p.ende().value_or("-"), "OK") << p.text();
        if (v24[i])
            for (int k = 0; k < 4; ++k)
                EXPECT_NE(b.find(leitungsZeile(k & 1, k >> 1, erwartet[k][0], erwartet[k][1])),
                          std::string::npos)
                    << "Kombination " << k << "\n" << b;
    }
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// Ohne Loop (Kabel ab): an der V.24 @p nr meldet DATEN-LOOP `KEIN ECHO` beim ersten
/// Zeichen, LEITUNGEN-LOOP FEHLER (alle Eingänge aus), Ende FEHLER.
template <class S>
void pruefeOhneLoop(S& s, int nr, const std::string& name) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t " + std::to_string(nr) + " /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    const auto& p = s.protokoll();
    EXPECT_EQ(p.wert(name, "DATEN-LOOP").value_or("-"), "FEHLER KEIN ECHO BEI 00H") << p.text();
    EXPECT_EQ(p.wert(name, "LEITUNGEN-LOOP").value_or("-").rfind("FEHLER RTS=", 0), 0u) << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

/// Ctrl+C mitten im DATEN-LOOP: Abbruch, Schnittstelle auf der BIOS-Vorgabe
/// (@p vorgabe wie `format()`), danach `DIR`.
template <class S>
void pruefeCtrlCImDatenLoop(S& s, int nr, const std::string& vorgabe) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);
    s.tippe("sertest t " + std::to_string(nr) + " /p /a\r");
    ASSERT_TRUE(s.bis("Daten-Loop: 256 Zeichen", kFrist)) << s.bild();
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_NE(s.bild().find("Abbruch mit Ctrl+C."), std::string::npos) << s.bild();
    EXPECT_TRUE(s.protokoll().zeilen().empty()) << "abgebrochen vor dem Urteil\n" << s.protokoll().text();
    EXPECT_EQ(format(s.maschine().serialHub()->status(nr - 1)), vorgabe);
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
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
/// Kanal auf 9600 8N1 mit den Leitungen @p waehrend[n-1] (an IFSS meldet der Wandler keine;
/// an V.24 spiegelt die Gegenstelle ihre Eingänge — ohne Kabel alles aus, mit Loop
/// RTS/DTR an), nach Ctrl+C auf der BIOS-Vorgabe @p vorgabe[n-1] (leer = unverändert
/// gegenüber vorher).  Zuletzt `DIR` — die Tastatur lebt noch.
template <class S>
void pruefeProgrammierungUndVorgabe(S& s, const std::vector<std::string>& namen,
                                    const std::vector<std::string>& waehrend,
                                    const std::vector<std::string>& vorgabe) {
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    for (size_t i = 0; i < namen.size(); ++i) {
        SCOPED_TRACE("Schnittstelle " + std::to_string(i + 1) + " " + namen[i]);
        const std::string vorher = format(hubStatus(s, static_cast<int>(i)));
        s.tippe("sertest g " + std::to_string(i + 1) + "\r");
        ASSERT_TRUE(s.bis("Gegenstelle an " + namen[i] + " bereit.", kFrist)) << s.bild();
        for (int k = 0; k < 20; ++k) s.lauf();
        EXPECT_EQ(format(hubStatus(s, static_cast<int>(i))), waehrend[i]);
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
    // Mehr Zeichen als SIO-FIFO (3) + ein Interrupt: bis AP-ST5 reichte die K8025 das
    // RETI nicht weiter, vier Zeichen kamen trotzdem alle an (1 abgeholt + 3 im FIFO).
    const uint8_t zeichen[] = {'x', 'y', 0x00, 0xFF, 'a', 'b', 0x1B, 0x55};
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
    auto a = zerlege("SERTEST DFUE/V.24 DATEN-LOOP: FEHLER KEIN ECHO BEI 00H");
    ASSERT_TRUE(a);
    EXPECT_EQ(a->name, "DFUE/V.24");
    EXPECT_EQ(a->teil, "DATEN-LOOP");
    EXPECT_EQ(a->wert, "FEHLER KEIN ECHO BEI 00H");
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

TEST(Sertest, A5120_PruefsteckerMitLoopAnAllenSchnittstellenOk) {
    SertestA5120 s;
    const int erwartet[4][2] = {{0, 0}, {0, 0}, {0, 1}, {1, 1}};   // CTS = V106∧V107, DCD = V109∧V107
    pruefeLoopAlle(s, {"DFUE/V.24", "DFUE/IFSS", "Drucker"}, {true, false, false}, erwartet);
}

TEST(Sertest, A5120_PruefsteckerOhneLoopMeldetFehler) {
    SertestA5120 s;
    pruefeOhneLoop(s, 1, "DFUE/V.24");
}

TEST(Sertest, A5120_CtrlCImDatenLoopLaesstSystemBedienbar) {
    SertestA5120 s;
    pruefeCtrlCImDatenLoop(s, 1, "9600 8N1 rts=1 dtr=1");
}

TEST(Sertest, A5120_GegenstelleProgrammiert9600_8N1UndStelltDieBiosVorgabeHer) {
    SertestA5120 s;
    // V.24 ohne Kabel: Leitungsspiegel → RTS/DTR aus.
    pruefeProgrammierungUndVorgabe(s, {"DFUE/V.24", "DFUE/IFSS", "Drucker"},
                                   {"9600 8N1 rts=0 dtr=0", "9600 8N1 rts=0 dtr=0", "9600 8N1 rts=0 dtr=0"},
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

TEST(Sertest, K8915_PruefsteckerMitLoopAnAllenSchnittstellenOk) {
    SertestK8915 s;
    const int erwartet[4][2] = {{0, 0}, {0, 0}, {1, 1}, {1, 1}};   // CTS = V107∧(¬RTS∨V106) = DTR
    pruefeLoopAlle(s, {"Drucker/IFSS1", "V.24", "DFUE/IFSS2"}, {false, true, false}, erwartet);
}

TEST(Sertest, K8915_PruefsteckerOhneLoopMeldetFehler) {
    SertestK8915 s;
    pruefeOhneLoop(s, 2, "V.24");
}

TEST(Sertest, K8915_CtrlCImDatenLoopLaesstSystemBedienbar) {
    SertestK8915 s;
    pruefeCtrlCImDatenLoop(s, 2, "0 5N1,5 (ungueltig) rts=0 dtr=0");   // Kanal- + CTC-Reset
}

TEST(Sertest, K8915_GegenstelleProgrammiert9600_8N1UndStelltDieBiosVorgabeHer) {
    SertestK8915 s;
    // V.24 mit Loop (Vorgabe am K8915): der Spiegel hält RTS/DTR an (CTS = V106 ∧ V107 bei
    // gesetztem RTS, DCD = DTR).
    pruefeProgrammierungUndVorgabe(s, {"Drucker/IFSS1", "V.24", "DFUE/IFSS2"},
                                   {"9600 8N1 rts=0 dtr=0", "9600 8N1 rts=1 dtr=1", "9600 8N1 rts=0 dtr=0"},
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

// ─── PC 1715 / CP/A 1715 (V0.2) ──────────────────────────────────────────────
//
// Hardware (doc/merkposten/pc1715.md, BIOS biopcsio.mac): SIO0 bei 0CH–0FH (AB0 = Kanal,
// AB1 = Steuer).  Kanal A: Sender = Drucker X4 (102/103/106), Empfänger = Tastatur;
// Kanal B = V.24 X5.  Der Drucker hat keinen Empfangsweg: DATEN-LOOP prüft nur das
// Senden, LEITUNGEN-LOOP den Break (103 → 106 am Prüfstecker 330-042).

TEST(Sertest, Pc1715_ErkenntRechnerUndListetSchnittstellen) {
    SertestPc1715 s;
    pruefeListe(s, "PC 1715 (ZRE)",
                {"  1  Drucker        SIO0 Kanal A      nur Senden (X4)",
                 "  2  V.24           SIO0 Kanal B      V.24 (X5)"},
                "  -  Tastatur S600  SIO0 Kanal A      (Empfaenger)");
}

TEST(Sertest, Pc1715_VorgabeMitMPUeberstimmtDieErkennung) {
    SertestPc1715 s;
    ASSERT_TRUE(startBisRolle(s, "sertest /m:p"));
    EXPECT_TRUE(hatZeile(s.bild(), "Rechner: PC 1715 (ZRE) (vorgegeben)")) << s.bild();
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}

TEST(Sertest, Pc1715_FalscheKommandozeileGibtKurzhilfe) {
    SertestPc1715 s;
    pruefeKurzhilfe(s);
}

TEST(Sertest, Pc1715_CtrlCAnDerRollenfrageLaesstSystemBedienbar) {
    SertestPc1715 s;
    pruefeCtrlCRolle(s);
}

TEST(Sertest, Pc1715_GegenstelleAmDruckerGehtNicht) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest g 1\r");
    ASSERT_TRUE(s.bis("Der Drucker kann nur senden", kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

TEST(Sertest, Pc1715_CtrlCInDerGegenstelleLaesstSystemBedienbar) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest g 2\r");
    ASSERT_TRUE(s.bis("Gegenstelle an V.24 bereit.", kFrist)) << s.bild();
    for (int i = 0; i < 100; ++i) s.lauf();
    EXPECT_NE(letzteZeile(s.bild()), "A>") << s.bild();
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// Mit Loop (= Prüfsteckern 330-042 am X4, 320-032 am X5): Drucker DATEN-LOOP OK (nur
/// Senden), LEITUNGEN-LOOP OK mit den drei Break-Zeilen; V.24 beide OK mit den
/// Rohzeilen CTS = RTS, DCD (107) = DTR.  Danach lebt die Tastatur.
TEST(Sertest, Pc1715_PruefsteckerMitLoopAnBeidenSchnittstellenOk) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);

    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    std::string b = s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("Drucker", "DATEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.wert("Drucker", "LEITUNGEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.ende().value_or("-"), "OK") << p.text();
    for (const char* z : {"  Break=0  CTS=0  erwartet  CTS=0  RR0=", "  Break=1  CTS=1  erwartet  CTS=1  RR0="})
        EXPECT_NE(b.find(z), std::string::npos) << z << "\n" << b;

    const int erwartet[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};   // CTS = RTS, DCD = DTR
    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 2 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    b = s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(p.wert("V.24", "DATEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.wert("V.24", "LEITUNGEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.ende().value_or("-"), "OK") << p.text();
    for (int k = 0; k < 4; ++k)
        EXPECT_NE(b.find(leitungsZeile(k & 1, k >> 1, erwartet[k][0], erwartet[k][1])), std::string::npos)
            << "Kombination " << k << "\n" << b;
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// Ohne Prüfstecker: V.24 wie an den anderen Maschinen (KEIN ECHO); der Drucker sendet
/// trotzdem (DATEN-LOOP OK), aber CTS folgt dem Break nicht (FEHLER BREAK=1).
TEST(Sertest, Pc1715_PruefsteckerOhneLoopMeldetFehler) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("Drucker", "DATEN-LOOP").value_or("-"), "OK") << p.text();
    EXPECT_EQ(p.wert("Drucker", "LEITUNGEN-LOOP").value_or("-"), "FEHLER Break=1") << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 2 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(p.wert("V.24", "DATEN-LOOP").value_or("-"), "FEHLER KEIN ECHO BEI 00H") << p.text();
    EXPECT_EQ(p.wert("V.24", "LEITUNGEN-LOOP").value_or("-").rfind("FEHLER RTS=", 0), 0u) << p.text();
}

/// Ctrl+C mitten im DATEN-LOOP der V.24: Kanal- und CTC-Reset (Zustand nach dem Kaltstart).
TEST(Sertest, Pc1715_CtrlCImDatenLoopLaesstSystemBedienbar) {
    SertestPc1715 s;
    pruefeCtrlCImDatenLoop(s, 2, "0 5N1,5 (ungueltig) rts=0 dtr=0");
}

/// Der Drucker teilt die SIO mit der Tastatur: nach dem Test steht CTC0 K0 auf 9600 Bd
/// und WR5 auf der BIOS-Vorgabe (8 Bit, DTR + RTS) — und die Tastatur geht weiter.
TEST(Sertest, Pc1715_DruckerLaesstDieTastaturLebenUndStelltDieBiosVorgabeHer) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(format(hubStatus(s, 0)), "9600 8N1 rts=1 dtr=1");
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

TEST(Sertest, Pc1715_V24EmpfaengtImInterrupt) {
    SertestPc1715 s;
    pruefeInterrupt(s, 2, "V.24", 0xF700);
}

/// Ohne Gegenstelle am Tester V.24: Format der Zeilen, ECHO/FLUSS-XON Zeitüberlauf.
TEST(Sertest, Pc1715_TesterAutomatikOhneGegenstelle) {
    SertestPc1715 s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t 2 /g /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("V.24", "ECHO").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.wert("V.24", "FLUSS-XON").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

/// Die BISHERIGE V0.1 kennt den PC 1715 nicht (Fixture `tests/fixtures/cpm/SERTEST_V01.COM`):
/// Banner V0.1, `Rechner nicht erkannt` — und das System bleibt bedienbar.
TEST(Sertest, Pc1715_AlteV01ErkenntDenRechnerNicht) {
    SertestPc1715 s(K1520_SERTEST_V01);
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest\r");
    ASSERT_TRUE(s.bis("Rechner nicht erkannt", kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(hatZeile(s.bild(), "Serial Test V0.1  (c) 2026 Olaf Krieger")) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

// ─── PC 1715W / SCP 3.0 = CP/M 3 (V0.3) ──────────────────────────────────────
//
// Hardware wie der 1715 (SIO0 0CH–0FH, AB0 = Kanal, AB1 = Steuer; CTC0 08H–0BH; Kanal A =
// Drucker-Sender + Tastatur-Empfänger, Kanal B = V.24), aber 3,9936 MHz, CTC im
// Zählerbetrieb (57H, ZK 13 — so rechnet das SCP-3.0-BIOS 9600 Bd) und CP/M 3 mit Bänken
// (BDOS 12 = 31H).  Der Emulator verdrahtet CLK/TRG0/1 am 1715W noch nicht: das Format der
// Schnittstellen gilt dort als ungültig (Wandler läuft mit dem Ersatzformat 9600 8N1) —
// deshalb prüfen die Fälle die CTC-Programmierung selbst (`ctcZaehler`).

namespace {
/// CTC0 K1 (V.24) bzw. K0 (Drucker) im Zählerbetrieb mit Zeitkonstante @p zk?
::testing::AssertionResult ctcZaehler(SertestPc1715W& s, int kanal, int zk) {
    const Z80CTC& c = s.maschine().zre().ctc();
    if (c.isTimerMode(kanal))
        return ::testing::AssertionFailure() << "CTC0 K" << kanal << " im Zeitgeberbetrieb";
    if (c.getCount(kanal) != zk)
        return ::testing::AssertionFailure() << "CTC0 K" << kanal << " Zaehlerstand " << c.getCount(kanal)
                                             << " statt ZK " << zk;
    return ::testing::AssertionSuccess();
}
}  // namespace

TEST(Sertest, Pc1715W_ErkenntRechnerUndListetSchnittstellen) {
    SertestPc1715W s;
    pruefeListe(s, "PC 1715W (ZRE)",
                {"  1  Drucker        SIO0 Kanal A      nur Senden (X4)",
                 "  2  V.24           SIO0 Kanal B      V.24 (X5)"},
                "  -  Tastatur S600  SIO0 Kanal A      (Empfaenger)");
}

TEST(Sertest, Pc1715W_VorgabeMitMWUeberstimmtDieErkennung) {
    SertestPc1715W s;
    ASSERT_TRUE(startBisRolle(s, "sertest /m:w"));
    EXPECT_TRUE(hatZeile(s.bild(), "Rechner: PC 1715W (ZRE) (vorgegeben)")) << s.bild();
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}

TEST(Sertest, Pc1715W_FalscheKommandozeileGibtKurzhilfe) {
    SertestPc1715W s;
    pruefeKurzhilfe(s);
}

TEST(Sertest, Pc1715W_CtrlCAnDerRollenfrageLaesstSystemBedienbar) {
    SertestPc1715W s;
    pruefeCtrlCRolle(s);
}

TEST(Sertest, Pc1715W_GegenstelleAmDruckerGehtNicht) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest g 1\r");
    ASSERT_TRUE(s.bis("Der Drucker kann nur senden", kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

TEST(Sertest, Pc1715W_CtrlCInDerGegenstelleLaesstSystemBedienbar) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest g 2\r");
    ASSERT_TRUE(s.bis("Gegenstelle an V.24 bereit.", kFrist)) << s.bild();
    for (int i = 0; i < 100; ++i) s.lauf();
    EXPECT_NE(letzteZeile(s.bild()), "A>") << s.bild();
    s.ctrlC();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// Wie am 1715: Drucker DATEN-LOOP (nur Senden) und LEITUNGEN-LOOP (Break → CTS) OK, V.24
/// beide OK mit CTS = RTS, DCD (107) = DTR; danach lebt die Tastatur.
TEST(Sertest, Pc1715W_PruefsteckerMitLoopAnBeidenSchnittstellenOk) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);

    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    std::string b = s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("Drucker", "DATEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.wert("Drucker", "LEITUNGEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.ende().value_or("-"), "OK") << p.text();
    for (const char* z : {"  Break=0  CTS=0  erwartet  CTS=0  RR0=", "  Break=1  CTS=1  erwartet  CTS=1  RR0="})
        EXPECT_NE(b.find(z), std::string::npos) << z << "\n" << b;

    const int erwartet[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};   // CTS = RTS, DCD = DTR
    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 2 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    b = s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(p.wert("V.24", "DATEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.wert("V.24", "LEITUNGEN-LOOP").value_or("-"), "OK") << p.text() << b;
    EXPECT_EQ(p.ende().value_or("-"), "OK") << p.text();
    for (int k = 0; k < 4; ++k)
        EXPECT_NE(b.find(leitungsZeile(k & 1, k >> 1, erwartet[k][0], erwartet[k][1])), std::string::npos)
            << "Kombination " << k << "\n" << b;
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

TEST(Sertest, Pc1715W_PruefsteckerOhneLoopMeldetFehler) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("Drucker", "DATEN-LOOP").value_or("-"), "OK") << p.text();
    EXPECT_EQ(p.wert("Drucker", "LEITUNGEN-LOOP").value_or("-"), "FEHLER Break=1") << p.text();
    ASSERT_TRUE(s.neuerLauf()) << s.bild();
    s.tippe("sertest t 2 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(p.wert("V.24", "DATEN-LOOP").value_or("-"), "FEHLER KEIN ECHO BEI 00H") << p.text();
    EXPECT_EQ(p.wert("V.24", "LEITUNGEN-LOOP").value_or("-").rfind("FEHLER RTS=", 0), 0u) << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

/// Ctrl+C mitten im DATEN-LOOP: Kanalreset B und CTC0 K1 wieder auf dem Wert nach der
/// BIOS-Initialisierung (57H, ZK 13); die Tastatur lebt.
TEST(Sertest, Pc1715W_CtrlCImDatenLoopLaesstSystemBedienbar) {
    SertestPc1715W s;
    pruefeCtrlCImDatenLoop(s, 2, "0 8N1 (ungueltig) rts=0 dtr=0");
    EXPECT_TRUE(ctcZaehler(s, 1, 13));
}

/// Während des Tests: Zählerbetrieb mit ZK 13 am Baudkanal (Drucker K0, V.24 K1), SIO ×16 —
/// das ist die 9600-Bd-Rechnung des SCP-3.0-BIOS (153,6 kHz = 1,9968 MHz / 13).
TEST(Sertest, Pc1715W_ProgrammiertDenBaudtaktWieDasBios) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);
    s.tippe("sertest t 2 /p /a\r");
    ASSERT_TRUE(s.bis("Daten-Loop: 256 Zeichen", kFrist)) << s.bild();
    EXPECT_TRUE(ctcZaehler(s, 1, 13));
    EXPECT_EQ(s.maschine().zre().sio().channelB().format().teiler, 16);
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    EXPECT_EQ(s.protokoll().ende().value_or("-"), "OK") << s.protokoll().text();
}

/// Der Drucker teilt die SIO mit der Tastatur: nach dem Test WR5 = BIOS-Wert 68H (rts/dtr aus)
/// und die Tastatur geht weiter.
TEST(Sertest, Pc1715W_DruckerLaesstDieTastaturLebenUndStelltDieBiosVorgabeHer) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, true);
    s.tippe("sertest t 1 /p /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    EXPECT_EQ(s.protokoll().ende().value_or("-"), "OK") << s.protokoll().text();
    EXPECT_FALSE(hubStatus(s, 0).rts);
    EXPECT_FALSE(hubStatus(s, 0).dtr);
    EXPECT_TRUE(s.dirFindetSertest()) << s.bild();
}

/// Empfang im Interrupt trotz Bankumschaltung: Vektor 30H in der Tabelle bei I = F3H, die
/// ISR läuft in der TPA-Bank, die BDOS-Aufrufe des Hauptprogramms sperren die SIO (BDOSW).
TEST(Sertest, Pc1715W_V24EmpfaengtImInterrupt) {
    SertestPc1715W s;
    pruefeInterrupt(s, 2, "V.24", 0xF300);
}

TEST(Sertest, Pc1715W_TesterAutomatikOhneGegenstelle) {
    SertestPc1715W s;
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t 2 /g /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("V.24", "ECHO").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.wert("V.24", "FLUSS-XON").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

/// Die VORHERIGE V0.2 (`tests/fixtures/cpm/SERTEST_V02.COM`) kennt den 1715W nicht: sie hält
/// ihn für einen PC 1715 (gleiche SIO bei 0EH/0FH, 40H/50H leer) und rechnet mit 2,4576 MHz
/// und Zeitgeberbetrieb.  Das ist der Grund für /M:W und die Erkennung über BDOS 12 in V0.3.
TEST(Sertest, Pc1715W_AlteV02HaeltDenRechnerFuerEinenPc1715) {
    SertestPc1715W s(K1520_SERTEST_V02);
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest\r");
    ASSERT_TRUE(s.bis("T/G", kFrist)) << s.bild();
    EXPECT_TRUE(hatZeile(s.bild(), "Serial Test V0.2  (c) 2026 Olaf Krieger")) << s.bild();
    EXPECT_TRUE(hatZeile(s.bild(), "Rechner: PC 1715 (ZRE)")) << s.bild();
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}

// ─── PRG 710 / PRG 710-1 / SCPX (V0.3) ───────────────────────────────────────
//
// K2521-ZRE + ASS K8025 (50H–5FH wie am A5120): V.24 = A33 Kanal A (Takt CTC A34 K2),
// IFSS-Hauptdrucker A32-B (nur 710), ZIFSS A32-A, Takt CTC A34 K0.  Am 710-1 trägt A32-B die
// Tastatur K7672 (tabu), am 710 sitzt die Tastatur am 8279.  Eigener Vektor D0H (I = DFH).
// Die beiden Varianten laufen mit denselben Fällen (TEST_P); die Unterschiede sind die Liste
// und die Erkennung.

namespace {
using PV = prg710test::V;

struct PrgFall {
    PV v;
    const char* rechner;
    std::vector<std::string> eintraege;
    const char* tastatur;
    std::vector<std::string> namen;
    std::vector<bool> v24;
};

const PrgFall& prgFall(PV v) {
    static const PrgFall f0{PV::Prg710, "PRG 710 (K2521, K8025)",
        {"  1  V.24           SIO A33 Kanal A   V.24 (X4)",
         "  2  IFSS Hauptdr.  SIO A32 Kanal B   IFSS (X6)",
         "  3  ZIFSS          SIO A32 Kanal A   IFSS (X5)"},
        "  -  Tastatur K7609 8279 C8H/C9H      (Tastatur)",
        {"V.24", "IFSS Hauptdrucker", "ZIFSS"}, {true, false, false}};
    static const PrgFall f1{PV::Prg710_1, "PRG 710-1 (K2521, K8025)",
        {"  1  V.24           SIO A33 Kanal A   V.24 (X4)",
         "  2  ZIFSS          SIO A32 Kanal A   IFSS (X5)"},
        "  -  Tastatur K7672 SIO A32 Kanal B   (Tastatur)",
        {"V.24", "ZIFSS"}, {true, false}};
    return v == PV::Prg710 ? f0 : f1;
}
}  // namespace

class SertestPrgP : public ::testing::TestWithParam<PV> {};

TEST_P(SertestPrgP, ErkenntRechnerUndListetSchnittstellen) {
    SertestPrg s(GetParam());
    const PrgFall& f = prgFall(GetParam());
    pruefeListe(s, f.rechner, f.eintraege, f.tastatur);
}

TEST_P(SertestPrgP, VorgabeUeberstimmtDieErkennung) {
    // /M:R = 710, /M:S = 710-1 — auch die jeweils andere Fassung lässt sich vorgeben.
    SertestPrg s(GetParam());
    const PrgFall& f = prgFall(GetParam());
    ASSERT_TRUE(startBisRolle(s, GetParam() == PV::Prg710 ? "sertest /m:r" : "sertest /m:s"));
    EXPECT_TRUE(hatZeile(s.bild(), std::string("Rechner: ") + f.rechner + " (vorgegeben)")) << s.bild();
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}

TEST_P(SertestPrgP, FalscheKommandozeileGibtKurzhilfe) {
    SertestPrg s(GetParam());
    pruefeKurzhilfe(s);
}

TEST_P(SertestPrgP, CtrlCAnDerRollenfrageLaesstSystemBedienbar) {
    SertestPrg s(GetParam());
    pruefeCtrlCRolle(s);
}

TEST_P(SertestPrgP, CtrlCInDerGegenstelleLaesstSystemBedienbar) {
    SertestPrg s(GetParam());
    pruefeCtrlCGegenstelle(s, "V.24");
}

/// Mit Loop: V.24 mit der K8025-Erwartung (CTS = RTS ∧ DTR, DCD = DTR), IFSS ohne Leitungen;
/// danach `DIR` — am 710-1 teilt der ZIFSS die SIO A32 mit der Tastatur (Kanal B).
TEST_P(SertestPrgP, PruefsteckerMitLoopAnAllenSchnittstellenOk) {
    SertestPrg s(GetParam());
    const PrgFall& f = prgFall(GetParam());
    const int erwartet[4][2] = {{0, 0}, {0, 0}, {0, 1}, {1, 1}};
    pruefeLoopAlle(s, f.namen, f.v24, erwartet);
}

TEST_P(SertestPrgP, PruefsteckerOhneLoopMeldetFehler) {
    SertestPrg s(GetParam());
    pruefeOhneLoop(s, 1, "V.24");
}

/// Ctrl+C im DATEN-LOOP der V.24: Kanalreset + BIOS-Werte (B152V24/B17272V2: Zeitgeber 07H/
/// ZK 1, WR4 4CH = 2 Stoppbits, WR5 E8H = DTR).
TEST_P(SertestPrgP, CtrlCImDatenLoopStelltDieBiosVorgabeHer) {
    SertestPrg s(GetParam());
    pruefeCtrlCImDatenLoop(s, 1, "9600 8N2 rts=0 dtr=1");
}

TEST_P(SertestPrgP, V24EmpfaengtImInterrupt) {
    SertestPrg s(GetParam());
    pruefeInterrupt(s, 1, "V.24", 0xDF00);
}

/// Der ZIFSS (A32 Kanal A) empfängt im Interrupt; am 710-1 bleibt die Tastatur (A32-B) am Leben.
TEST_P(SertestPrgP, ZifssEmpfaengtImInterruptUndLaesstDieTastaturLeben) {
    SertestPrg s(GetParam());
    const int nr = static_cast<int>(prgFall(GetParam()).namen.size());
    pruefeInterrupt(s, nr, "ZIFSS", 0xDF00);
}

TEST_P(SertestPrgP, TesterAutomatikOhneGegenstelle) {
    SertestPrg s(GetParam());
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    loopAlle(s, false);
    s.tippe("sertest t 1 /g /a\r");
    ASSERT_TRUE(s.bisEnde(kFrist)) << s.bild();
    ASSERT_TRUE(s.bisPrompt(kFrist)) << s.bild();
    auto& p = s.protokoll();
    EXPECT_EQ(p.wert("V.24", "ECHO").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << p.text();
    EXPECT_EQ(p.ende().value_or("-"), "FEHLER") << p.text();
}

INSTANTIATE_TEST_SUITE_P(Prg, SertestPrgP, ::testing::Values(PV::Prg710, PV::Prg710_1),
                         [](const ::testing::TestParamInfo<PV>& i) {
                             return std::string(prg710test::name(i.param));
                         });

/// Die VORHERIGE V0.2 kennt die PRG nicht: sie erkennt die K8025 bei 50H samt SIO A32 als A5120
/// (Banner V0.2, `Rechner: A5120 (K8025)`) — Grund für /M:R, /M:S und die ZRE-CTC-Probe in V0.3.
TEST(Sertest, Prg710_1_AlteV02HaeltDenRechnerFuerEinenA5120) {
    SertestPrg s(PV::Prg710_1, K1520_SERTEST_V02);
    ASSERT_TRUE(s.fehler().empty()) << s.fehler();
    ASSERT_TRUE(s.kaltstart()) << s.bild();
    s.tippe("sertest\r");
    ASSERT_TRUE(s.bis("T/G", kFrist)) << s.bild();
    EXPECT_TRUE(hatZeile(s.bild(), "Serial Test V0.2  (c) 2026 Olaf Krieger")) << s.bild();
    EXPECT_TRUE(hatZeile(s.bild(), "Rechner: A5120 (K8025)")) << s.bild();
    s.ctrlC();
    EXPECT_TRUE(s.bisPrompt(kFrist)) << s.bild();
}
