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
