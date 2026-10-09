/**
 * @file test_sertest_kopplung.cpp
 * @brief SertestKopplung.* — SERTEST.COM als Tester gegen SERTEST.COM als Gegenstelle auf
 *        einem zweiten Rechner (Entwurf 19 §14.6/§14.7/§14.8, AP-ST5): zwei Maschinen im
 *        selben Prozess über RFC 2217 (Server ↔ Client, Nullmodem-Kreuzung §6.4).
 *
 * Je Fall zwei Kaltstarts; die Gegenstelle läuft mit `sertest g <n>`, der Tester mit
 * `sertest t <n> /g /a` (nur der Gegenstellentest — ohne Loop fiele der Prüfstecker
 * ohnehin durch).  Geprüft werden die Ergebniszeilen (§14.3) beider Seiten: LEITUNGEN
 * (nur V.24, über den Leitungsspiegel der Gegenstelle), ECHO (4096 Bytes, Bericht ohne
 * Empfangsfehler), FLUSS-HW (nur V.24: Gegenstelle bremst mit RTS, Tester sendet mit
 * Auto Enables) und FLUSS-XON (Gegenstelle bremst mit XOFF/XON, „XON/XOFF beachten" an
 * ihrem Wandler) — Ende OK (AP-ST6).  Dazu, am Wandler beobachtet, dass wirklich gebremst
 * wurde und der Sender des Testers bei fehlendem CTS stand.
 *
 * **Uhr- und Maschinenzeit:** die Bytes laufen verlustfrei mit Rückstau durch die Wandler
 * (Maschinenzeit), die Steuerleitungen und das Netz aber durch die I/O-Fäden der beiden
 * Hubs (Uhrzeit).  Die Paarschleife (`Paar::bis`) lässt beide Maschinen deshalb in
 * gleichen Scheiben abwechselnd laufen und drosselt sie auf höchstens kFaktor-fache
 * Echtzeit — sonst lägen unter `ctest -j` die 2-s-Fristen des Testers (Maschinenzeit) in
 * der Größenordnung einer Faden-Weckzeit.
 *
 * Zwei Binaries aus dieser Quelle: `k1520_test_sertest_kopplung` (schneller Fall
 * A5120 ↔ A5120 V.24, Standardregression) und `…_lang` (`SERTEST_KOPPLUNG_LANG`, alle
 * übrigen, Label `format_integration` → `tools/dev.sh test-format`).
 */
#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include "core/serial/hub.h"
#include "core/serial/wandler.h"
#include "tests/system/sertest_hilfen.h"

#ifndef K1520_SERTEST_V01
#error "K1520_SERTEST_V01 fehlt (tests/fixtures/cpm/SERTEST_V01.COM)"
#endif
#ifndef K1520_SERTEST_V02
#error "K1520_SERTEST_V02 fehlt (tests/fixtures/cpm/SERTEST_V02.COM)"
#endif

using namespace sertest;
using k1520::serial::Betriebsart;
using k1520::serial::Rolle;
using k1520::serial::SerialHub;
using k1520::serial::SerialKonfig;
using k1520::serial::Zustand;
using Uhr = std::chrono::steady_clock;

namespace {

constexpr long long kScheibe = 10'000;          ///< Takte je Maschine und Runde (≈ 4 ms)
constexpr double    kFaktor  = 8.0;             ///< höchstens 8× Echtzeit gekoppelt
// φ je Maschine: `T::kPhi` (A5120, K8915, PC 1715 2,4576 MHz; PC 1715W 3,9936 MHz).
constexpr long long kFrist   = 150'000'000;     ///< ≈ 60 s Maschinenzeit je Lauf
constexpr long long kBoot    = 100'000'000;

/// Zwei Maschinen in Scheiben gleicher MASCHINENZEIT, gedrosselt.  Laufen sie mit
/// verschiedenem Takt (PC 1715W gegen PC 1715), bekommt die Gegenseite entsprechend mehr
/// bzw. weniger Takte je Runde: beide Wandler takten in Maschinentakten, aber die
/// Steuerleitungen und das Netz laufen in Uhrzeit.
template <class T, class G>
struct Paar {
    T& t;
    G& g;
    static constexpr long long kScheibeG = static_cast<long long>(kScheibe * G::kPhi / T::kPhi + 0.5);

    bool bis(const std::function<bool()>& fertig, long long frist = kFrist) {
        const auto t0 = Uhr::now();
        for (long long n = 0;;) {
            t.erfasse();
            g.erfasse();
            if (fertig()) return true;
            if (n >= frist) return false;
            n += t.lauf(kScheibe);
            g.lauf(kScheibeG);
            const auto soll = std::chrono::duration<double>(n / T::kPhi / kFaktor);
            const auto ist = Uhr::now() - t0;
            if (ist + std::chrono::milliseconds(2) < soll)
                std::this_thread::sleep_for(soll - ist);
        }
    }
    /// Bis der Tester `SERTEST ENDE …` gemeldet hat.
    bool bisEnde() { return bis([&] { return t.protokoll().ende().has_value(); }); }
    /// Bis @p text im Bild von @p s steht.
    template <class S>
    bool bisText(S& s, const std::string& text) {
        return bis([&] { return s.bild().find(text) != std::string::npos; });
    }
};

/// Schnittstelle @p i der Maschine @p s für RFC 2217 einstellen und starten.  Am K8915 ist
/// der Loop vorgegeben (der ROM-Selbsttest braucht ihn) — Loop und Verbindung schließen
/// sich aus (§6.5): erst `start` mit Loop muss scheitern, dann Loop aus.
template <class S>
void einstellen(S& s, int i, Rolle rolle, uint16_t port) {
    SerialHub& h = *s.maschine().serialHub();
    SerialKonfig k = h.konfig(i);
    if (k.loop) {
        ASSERT_FALSE(h.start(i)) << "Start bei Loop muss gesperrt sein";
        k.loop = false;
    }
    k.betriebsart = Betriebsart::Rfc2217;
    k.rolle = rolle;
    k.host = "127.0.0.1";
    k.port = port;
    k.xonxoff = false;
    ASSERT_TRUE(h.konfigurieren(i, k));
    ASSERT_TRUE(h.start(i)) << h.status(i).meldung;
}

/// Server @p is an @p server mit Client @p ic an @p client verbinden (Port vom System).
template <class A, class B>
void koppeln(A& server, int is, B& client, int ic) {
    ASSERT_NO_FATAL_FAILURE(einstellen(server, is, Rolle::Server, 0));
    SerialHub& hs = *server.maschine().serialHub();
    const uint16_t port = hs.status(is).port_aktiv;
    ASSERT_NE(port, 0);
    ASSERT_NO_FATAL_FAILURE(einstellen(client, ic, Rolle::Client, port));
    SerialHub& hc = *client.maschine().serialHub();
    const auto frist = Uhr::now() + std::chrono::seconds(10);
    while (Uhr::now() < frist && (hs.status(is).zustand != Zustand::Verbunden ||
                                  hc.status(ic).zustand != Zustand::Verbunden))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(hs.status(is).zustand, Zustand::Verbunden) << hs.status(is).meldung;
    ASSERT_EQ(hc.status(ic).zustand, Zustand::Verbunden) << hc.status(ic).meldung;
}

/// Wandlerstand einer Schnittstelle (für Fehlermeldungen).
template <class S>
std::string lage(S& s, int i) {
    const auto st = s.maschine().serialHub()->status(i);
    return "zustand=" + std::to_string(int(st.zustand)) + " gesendet=" +
           std::to_string(st.bytes_gesendet) + " empfangen=" + std::to_string(st.bytes_empfangen) +
           " puffer s/e=" + std::to_string(st.puffer_senden) + "/" +
           std::to_string(st.puffer_empfangen) + " rts=" + std::to_string(st.rts) +
           " dtr=" + std::to_string(st.dtr) + " cts=" + std::to_string(st.cts) +
           " dcd=" + std::to_string(st.dcd) + " " + st.meldung;
}

/// CPU-Stand der 1715-Familie (PC, SP, IFF1) für Fehlermeldungen; für die anderen leer.
template <class S>
auto cpuLage(S& s, int) -> decltype(s.maschine().zre().cpu().PC, std::string()) {
    const auto& c = s.maschine().zre().cpu();
    char b[80];
    std::snprintf(b, sizeof b, " CPU PC=%04X SP=%04X IFF1=%d", c.PC, c.SP, int(c.IFF1));
    return b;
}
template <class S>
std::string cpuLage(S&, long) { return {}; }

template <class S>
::testing::AssertionResult boot(S& s) {
    if (!s.fehler().empty()) return ::testing::AssertionFailure() << s.fehler();
    if (!s.kaltstart()) return ::testing::AssertionFailure() << "kein Prompt\n" << s.bild();
    return ::testing::AssertionSuccess();
}

/// Schalter „XON/XOFF beachten" (§6.3) am Wandler der Schnittstelle @p i.  Wirkt sofort,
/// auch bei laufender Verbindung (keine gesperrte Einstellung, §4).
template <class S>
void xonxoff(S& s, int i, bool an) {
    SerialHub& h = *s.maschine().serialHub();
    SerialKonfig k = h.konfig(i);
    k.xonxoff = an;
    ASSERT_TRUE(h.konfigurieren(i, k));
}

/// Was die Paarschleife während der FLUSS-Abschnitte beobachtet (§14.7 Schritte 3/4).
struct FlussBeobachtung {
    bool abschnittH = false, abschnittX = false;   ///< Ankündigung an der Gegenstelle gesehen
    // FLUSS-HW, am Wandler des Testers: Runden mit CTS aus am Anfang UND Ende, und was
    // der Tester in diesen Runden trotzdem abgab — mit Auto Enables (WR3 D5) höchstens das
    // Zeichen, das beim Fallen von CTS schon im Sender stand.
    int      ctsAusPhasen = 0;
    long     ctsAusRunden = 0;
    uint64_t gesendetBeiCtsAus = 0;
    bool     vorher = false;
    k1520::serial::WandlerSicht alt;
    // FLUSS-XON, am Wandler der Gegenstelle: der Halt „XON/XOFF beachten" griff.
    bool xoffHaltGesehen = false;
    // Gegenstelle: fertige Abschnittszeilen, „verworfen"/„Zeitueberlauf" gesehen.
    std::set<std::string> fertig;
    bool verworfen = false, zeitueberlauf = false;
};

/// `Abschnitt fertig, Empfangsfehler xxxxH[, gebremst yyyyH]` — vollständige Zeile?
/// Liefert {ue, bz} (bz = -1 ohne Bremszähler, Abschnitt E).
inline std::optional<std::pair<int, int>> fertigZeile(const std::string& z) {
    static const std::regex re(
        "^Abschnitt fertig, Empfangsfehler ([0-9A-F]{4})H(, gebremst ([0-9A-F]{4})H)?$");
    std::smatch m;
    if (!std::regex_match(z, m, re)) return std::nullopt;
    const int ue = std::stoi(m[1].str(), nullptr, 16);
    const int bz = m[3].matched ? std::stoi(m[3].str(), nullptr, 16) : -1;
    return std::make_pair(ue, bz);
}

/// Ein Durchgang: Gegenstelle `sertest g <ng>` an @p g, Tester `sertest t <nt> /g /a` an
/// @p t; erwartet LEITUNGEN (@p v24: OK, sonst ENTFAELLT), ECHO OK, FLUSS-HW (@p v24: OK,
/// sonst ENTFAELLT), FLUSS-XON OK, Ende OK.  Für FLUSS-XON wird „XON/XOFF beachten" am
/// Wandler der Gegenstelle eingeschaltet, sobald sie den Abschnitt X ankündigt (§14.8) —
/// vorher nicht: ECHO und FLUSS-HW übertragen alle Bytewerte, ein zurückgeschicktes 13H
/// hielte sonst den eigenen Empfang an.  Danach Ctrl+C an der Gegenstelle, beide wieder
/// am Prompt (für einen weiteren Durchgang).
template <class T, class G>
void durchgang(T& t, int nt, const std::string& nameT, G& g, int ng, const std::string& nameG,
               bool v24) {
    Paar<T, G> p{t, g};
    ASSERT_TRUE(t.neuerLauf()) << t.bild();
    ASSERT_TRUE(g.neuerLauf()) << g.bild();
    g.tippe("sertest g " + std::to_string(ng) + "\r");
    ASSERT_TRUE(p.bisText(g, "Gegenstelle an " + nameG + " bereit.")) << g.bild();
    t.tippe("sertest t " + std::to_string(nt) + " /g /a\r");

    k1520::serial::Wandler& wt = t.maschine().serialHub()->wandler(nt - 1);
    k1520::serial::Wandler& wg = g.maschine().serialHub()->wandler(ng - 1);
    FlussBeobachtung f;
    const bool ende = p.bis([&] {
        const std::string gb = g.bild();
        for (const std::string& z : zeilenAus(gb)) {
            if (fertigZeile(z)) f.fertig.insert(z);
            if (z.find("verworfen") != std::string::npos) f.verworfen = true;
            if (z.find("Zeitueberlauf") != std::string::npos) f.zeitueberlauf = true;
        }
        if (!f.abschnittH && gb.find("Abschnitt H:") != std::string::npos) f.abschnittH = true;
        if (!f.abschnittX && gb.find("Abschnitt X:") != std::string::npos) {
            f.abschnittX = true;
            xonxoff(g, ng - 1, true);
        }
        if (f.abschnittH && !f.abschnittX) {
            const auto n = wt.sicht();
            if (f.vorher && !n.cts) {
                if (f.alt.cts) ++f.ctsAusPhasen;
                else {
                    ++f.ctsAusRunden;
                    f.gesendetBeiCtsAus += n.bytes_gesendet - f.alt.bytes_gesendet;
                }
            }
            f.alt = n;
            f.vorher = true;
        }
        if (f.abschnittX && wg.sicht().xoffHalt) f.xoffHaltGesehen = true;
        return t.protokoll().ende().has_value();
    });
    xonxoff(g, ng - 1, false);
    ASSERT_TRUE(ende) << "Tester:\n" << t.bild() << "\nGegenstelle:\n" << g.bild();

    const std::string bt = t.bild();
    const auto& pt = t.protokoll();
    const std::string lageT = "\nTester: " + lage(t, nt - 1) + cpuLage(t, 0) + "\nGegenstelle: " +
                              lage(g, ng - 1) + cpuLage(g, 0);
    EXPECT_EQ(pt.wert(nameT, "LEITUNGEN").value_or("-"), v24 ? "OK" : "ENTFAELLT")
        << pt.text() << bt << "\nGegenstelle:\n" << g.bild();
    EXPECT_EQ(pt.wert(nameT, "ECHO").value_or("-"), "OK") << pt.text() << lageT;
    EXPECT_EQ(pt.wert(nameT, "FLUSS-HW").value_or("-"), v24 ? "OK" : "ENTFAELLT")
        << pt.text() << lageT << "\nGegenstelle:\n" << g.bild();
    EXPECT_EQ(pt.wert(nameT, "FLUSS-XON").value_or("-"), "OK")
        << pt.text() << lageT << "\nGegenstelle:\n" << g.bild();
    EXPECT_FALSE(pt.wert(nameT, "DATEN-LOOP")) << "nur /G\n" << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "OK") << pt.text();
    EXPECT_EQ(bt.find("bestaetigt: NEIN"), std::string::npos) << bt;

    // FLUSS-HW: die Gegenstelle hat wirklich gebremst (CTS des Testers fiel), und der
    // Sender des Testers hielt an (Auto Enables) — sonst wäre der Rückstau nur in den
    // Wandlerpuffern gelandet und der Test grün geblieben.
    if (v24) {
        EXPECT_TRUE(f.abschnittH);
        EXPECT_GT(f.ctsAusPhasen, 0) << "CTS am Tester fiel nie";
        EXPECT_GT(f.ctsAusRunden, 0);
        EXPECT_LE(f.gesendetBeiCtsAus, uint64_t(f.ctsAusPhasen))
            << "Tester sendete bei fehlendem CTS weiter (Auto Enables?) phasen="
            << f.ctsAusPhasen << " runden=" << f.ctsAusRunden;
    } else {
        EXPECT_FALSE(f.abschnittH) << "FLUSS-HW an IFSS";
    }
    // FLUSS-XON: XOFF der Gegenstelle hielt ihren eigenen Empfang an (§6.3).
    EXPECT_TRUE(f.abschnittX);
    EXPECT_TRUE(f.xoffHaltGesehen) << "Gegenstelle sandte nie XOFF";

    // Gegenstelle: einmal INTERRUPT OK, alle Abschnitte ohne Empfangsfehler, in H/X
    // gebremst, nichts verworfen, kein Zeitüberlauf.
    EXPECT_EQ(g.protokoll().wert("", "INTERRUPT").value_or("-"), "OK") << g.protokoll().text();
    bool eGesehen = false, gebremst = false;
    for (const std::string& z : f.fertig) {
        const auto w = fertigZeile(z);
        EXPECT_EQ(w->first, 0) << z;
        if (w->second < 0) eGesehen = true;
        else gebremst = gebremst || w->second > 0;
    }
    EXPECT_TRUE(eGesehen) << g.bild();
    EXPECT_TRUE(gebremst) << g.bild();
    EXPECT_FALSE(f.verworfen) << g.bild();
    EXPECT_FALSE(f.zeitueberlauf) << g.bild();

    ASSERT_TRUE(t.bisPrompt(kFrist)) << t.bild();
    for (int r = 0; r < 100; ++r) g.lauf();   // Bild der Gegenstelle steht (SCPX rollt unter DI)
    g.ctrlC();
    ASSERT_TRUE(g.bisPrompt(kFrist)) << g.bild();
}

}  // namespace

#ifndef SERTEST_KOPPLUNG_LANG

/**
 * @test SertestKopplung.A5120_V24_LeitungenUndEcho
 * @brief Schneller Fall der Standardregression: A5120 (Tester, Client) ↔ A5120
 *        (Gegenstelle, Server) an DFÜ/V.24 — LEITUNGEN über den Leitungsspiegel, ECHO.
 */
TEST(SertestKopplung, A5120_V24_LeitungenUndEcho) {
    SertestA5120 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 0, t, 0));
    durchgang(t, 1, "DFUE/V.24", g, 1, "DFUE/V.24", true);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715_V24_LeitungenUndEcho
 * @brief Schneller Fall der Standardregression, V0.3: PC 1715 (Tester, Client) ↔ PC 1715
 *        (Gegenstelle, Server) an der V.24 (SIO0 Kanal B; CP/A 1715).  Leitungen: CTS = RTS,
 *        107 (an /DCDA) = DTR über das Nullmodemkabel des Hubs.
 */
TEST(SertestKopplung, Pc1715_V24_LeitungenUndEcho) {
    SertestPc1715 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 1));
    durchgang(t, 2, "V.24", g, 2, "V.24", true);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715W_V24_LeitungenUndEcho
 * @brief Schneller Fall der Standardregression, V0.3: PC 1715W (Tester, Client) ↔ PC 1715W
 *        (Gegenstelle, Server) an der V.24 (SIO0 Kanal B; SCP 3.0 = CP/M 3).  Die
 *        Gegenstelle empfängt im Interrupt (Vektor 30H, ISR in der TPA-Bank, BDOS-Aufrufe
 *        mit gesperrter SIO), alle Abschnitte ohne Empfangsfehler.
 */
TEST(SertestKopplung, Pc1715W_V24_LeitungenUndEcho) {
    SertestPc1715W t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 1));
    durchgang(t, 2, "V.24", g, 2, "V.24", true);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

#else

/**
 * @test SertestKopplung.Pc1715W_DruckerSendetAnDieGegenstelle
 * @brief PC 1715W Drucker (X4, nur Senden) → PC 1715W V.24 als Gegenstelle, V0.3 auf beiden:
 *        SENDEN OK am Tester, an der Gegenstelle 1000H Bytes ohne Empfangsfehler.
 */
TEST(SertestKopplung, Pc1715W_DruckerSendetAnDieGegenstelle) {
    SertestPc1715W t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 0));
    Paar<SertestPc1715W, SertestPc1715W> p{t, g};
    ASSERT_TRUE(t.neuerLauf());
    g.tippe("sertest g 2\r");
    ASSERT_TRUE(p.bisText(g, "Gegenstelle an V.24 bereit.")) << g.bild();
    t.tippe("sertest t 1 /g /a\r");
    ASSERT_TRUE(p.bisEnde()) << "Tester:\n" << t.bild() << "\nGegenstelle:\n" << g.bild();
    const auto& pt = t.protokoll();
    EXPECT_EQ(pt.wert("Drucker", "LEITUNGEN").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_EQ(pt.wert("Drucker", "SENDEN").value_or("-"), "OK") << pt.text() << t.bild();
    EXPECT_EQ(pt.wert("Drucker", "FLUSS-HW").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_EQ(pt.wert("Drucker", "FLUSS-XON").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_FALSE(pt.wert("Drucker", "ECHO")) << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "OK") << pt.text();
    ASSERT_TRUE(p.bis([&] {
        for (const std::string& z : zeilenAus(g.bild()))
            if (fertigZeile(z)) return true;
        return false;
    })) << "Gegenstelle:\n" << g.bild();
    const std::string bg = g.bild();
    EXPECT_NE(bg.find("Abschnitt E: 1000H Bytes"), std::string::npos) << bg;
    EXPECT_NE(bg.find("Abschnitt fertig, Empfangsfehler 0000H"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("Zeitueberlauf"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("verworfen"), std::string::npos) << bg;
    EXPECT_EQ(t.maschine().serialHub()->status(0).bytes_gesendet, 1u + 6u + 4096u);
    ASSERT_TRUE(t.bisPrompt(kFrist)) << t.bild();
    for (int r = 0; r < 100; ++r) g.lauf();
    g.ctrlC();
    ASSERT_TRUE(g.bisPrompt(kFrist)) << g.bild();
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715W_MitPc1715UndDerAltenV02_BeideRichtungen
 * @brief PC 1715W mit V0.3 ↔ PC 1715 mit der BISHERIGEN V0.2 (`SERTEST_V02.COM`, CP/A 1715) an
 *        der V.24, beide Richtungen.  Die Maschinen laufen mit verschiedenem Takt (3,9936 gegen
 *        2,4576 MHz), beide stellen 9600 Bd ein (1715W: Zähler 57H/ZK 13, 1715: Zeitgeber
 *        17H/ZK 1) — die Leitungserwartung ist dieselbe (CTS = RTS, 107 = DTR).
 */
TEST(SertestKopplung, Pc1715W_MitPc1715UndDerAltenV02_BeideRichtungen) {
    SertestPc1715W w;
    SertestPc1715 p(K1520_SERTEST_V02);
    ASSERT_TRUE(boot(w));
    ASSERT_TRUE(boot(p));
    ASSERT_NO_FATAL_FAILURE(koppeln(p, 1, w, 1));
    {
        SCOPED_TRACE("PC 1715W (V0.3) Tester, PC 1715 (V0.2) Gegenstelle");
        durchgang(w, 2, "V.24", p, 2, "V.24", true);
    }
    {
        SCOPED_TRACE("PC 1715 (V0.2) Tester, PC 1715W (V0.3) Gegenstelle");
        durchgang(p, 2, "V.24", w, 2, "V.24", true);
    }
    w.maschine().serialHub()->stopAlle();
    p.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715W_MitA5120UndDerAltenV01_BeideRichtungen
 * @brief PC 1715W mit V0.3 ↔ A5120 mit der BISHERIGEN V0.1 (`SERTEST_V01.COM`, CP/A) an der
 *        V.24, beide Richtungen.  Die Leitungserwartungen der Fassungen sind verschieden
 *        (1715W: CTS = RTS; A5120: CTS = V106 ∧ V107), die LEITUNGEN-Schrittfolge geht an
 *        beiden auf.
 */
TEST(SertestKopplung, Pc1715W_MitA5120UndDerAltenV01_BeideRichtungen) {
    SertestPc1715W w;
    SertestA5120 a(K1520_SERTEST_V01);
    ASSERT_TRUE(boot(w));
    ASSERT_TRUE(boot(a));
    ASSERT_NO_FATAL_FAILURE(koppeln(a, 0, w, 1));
    {
        SCOPED_TRACE("PC 1715W (V0.3) Tester, A5120 (V0.1) Gegenstelle");
        durchgang(w, 2, "V.24", a, 1, "DFUE/V.24", true);
    }
    {
        SCOPED_TRACE("A5120 (V0.1) Tester, PC 1715W (V0.3) Gegenstelle");
        durchgang(a, 1, "DFUE/V.24", w, 2, "V.24", true);
    }
    a.maschine().serialHub()->stopAlle();
    w.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715_DruckerSendetAnDieGegenstelle
 * @brief PC 1715 Drucker (X4, nur Senden) → PC 1715 V.24 als Gegenstelle: SENDEN OK am
 *        Tester (LEITUNGEN/FLUSS-* ENTFAELLT); das Ergebnis liest man an der Gegenstelle:
 *        „Abschnitt E: 1000H Bytes“, „Abschnitt fertig, Empfangsfehler 0000H“.  Am
 *        Wandler des Testers: 1 Weckzeichen + 6 Ankündigung + 4096 Nutzbytes.
 */
TEST(SertestKopplung, Pc1715_DruckerSendetAnDieGegenstelle) {
    SertestPc1715 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 0));
    Paar<SertestPc1715, SertestPc1715> p{t, g};
    ASSERT_TRUE(t.neuerLauf());
    g.tippe("sertest g 2\r");
    ASSERT_TRUE(p.bisText(g, "Gegenstelle an V.24 bereit.")) << g.bild();
    t.tippe("sertest t 1 /g /a\r");
    ASSERT_TRUE(p.bisEnde()) << "Tester:\n" << t.bild() << "\nGegenstelle:\n" << g.bild();
    const auto& pt = t.protokoll();
    EXPECT_EQ(pt.wert("Drucker", "LEITUNGEN").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_EQ(pt.wert("Drucker", "SENDEN").value_or("-"), "OK") << pt.text() << t.bild();
    EXPECT_EQ(pt.wert("Drucker", "FLUSS-HW").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_EQ(pt.wert("Drucker", "FLUSS-XON").value_or("-"), "ENTFAELLT") << pt.text();
    EXPECT_FALSE(pt.wert("Drucker", "ECHO")) << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "OK") << pt.text();
    ASSERT_TRUE(p.bis([&] {
        for (const std::string& z : zeilenAus(g.bild()))
            if (fertigZeile(z)) return true;
        return false;
    })) << "Gegenstelle:\n" << g.bild();
    const std::string bg = g.bild();
    EXPECT_NE(bg.find("Abschnitt E: 1000H Bytes"), std::string::npos) << bg;
    EXPECT_NE(bg.find("Abschnitt fertig, Empfangsfehler 0000H"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("Zeitueberlauf"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("verworfen"), std::string::npos) << bg;
    EXPECT_EQ(t.maschine().serialHub()->status(0).bytes_gesendet, 1u + 6u + 4096u);
    ASSERT_TRUE(t.bisPrompt(kFrist)) << t.bild();
    for (int r = 0; r < 100; ++r) g.lauf();
    g.ctrlC();
    ASSERT_TRUE(g.bisPrompt(kFrist)) << g.bild();
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Pc1715_MitA5120UndDerAltenV01_BeideRichtungen
 * @brief Gemischte Fassungen an der V.24: PC 1715 mit V0.3 ↔ A5120 mit der BISHERIGEN V0.1
 *        (`tests/fixtures/cpm/SERTEST_V01.COM`).  Erst 1715 Tester / A5120 Gegenstelle,
 *        dann umgekehrt.  Beide Seiten 9600 8N1; die Leitungserwartungen sind verschieden
 *        (1715: CTS = RTS; A5120: CTS = V106 ∧ V107), die Schrittfolge der LEITUNGEN
 *        (00, DTR, RTS+DTR, DTR, 00) lässt beide aufgehen.
 */
TEST(SertestKopplung, Pc1715_MitA5120UndDerAltenV01_BeideRichtungen) {
    SertestPc1715 p;
    SertestA5120 a(K1520_SERTEST_V01);
    ASSERT_TRUE(boot(p));
    ASSERT_TRUE(boot(a));
    ASSERT_NO_FATAL_FAILURE(koppeln(a, 0, p, 1));
    {
        SCOPED_TRACE("PC 1715 (V0.3) Tester, A5120 (V0.1) Gegenstelle");
        durchgang(p, 2, "V.24", a, 1, "DFUE/V.24", true);
    }
    {
        SCOPED_TRACE("A5120 (V0.1) Tester, PC 1715 (V0.3) Gegenstelle");
        durchgang(a, 1, "DFUE/V.24", p, 2, "V.24", true);
    }
    a.maschine().serialHub()->stopAlle();
    p.maschine().serialHub()->stopAlle();
}

// ─── PRG 710 / PRG 710-1 (SCPX V1.5 / V1.7), V0.3 ────────────────────────────
// Hub-Index: 0 = V.24 (A33-A); 710: 1 = IFSS Hauptdrucker (A32-B), 2 = ZIFSS (A32-A);
// 710-1: 1 = ZIFSS (A32-A).  K8025 wie am A5120 (CTS = V106 ∧ V107).

/**
 * @test SertestKopplung.Prg710_1_V24_MitPrg710_1 / Prg710_V24_MitPrg710
 * @brief V.24: PRG 710-1 (Tester, Client) ↔ PRG 710-1 (Gegenstelle, Server) und PRG 710 ↔
 *        PRG 710 — beide Varianten haben dieselbe K8025-Schnittstelle; LEITUNGEN, ECHO,
 *        FLUSS-HW, FLUSS-XON OK.
 */
TEST(SertestKopplung, Prg710_1_V24_MitPrg710_1) {
    SertestPrg t(prg710test::V::Prg710_1), g(prg710test::V::Prg710_1);
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 0, t, 0));
    durchgang(t, 1, "V.24", g, 1, "V.24", true);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

TEST(SertestKopplung, Prg710_V24_MitPrg710) {
    SertestPrg t(prg710test::V::Prg710), g(prg710test::V::Prg710);
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 0, t, 0));
    durchgang(t, 1, "V.24", g, 1, "V.24", true);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/// Gemischt: PRG 710 (V1.5, 8279) ↔ PRG 710-1 (V1.7, K7672) an der V.24, beide Richtungen.
TEST(SertestKopplung, Prg710_V24_MitPrg710_1_BeideRichtungen) {
    SertestPrg a(prg710test::V::Prg710), b(prg710test::V::Prg710_1);
    ASSERT_TRUE(boot(a));
    ASSERT_TRUE(boot(b));
    ASSERT_NO_FATAL_FAILURE(koppeln(a, 0, b, 0));
    {
        SCOPED_TRACE("PRG 710 Tester, PRG 710-1 Gegenstelle");
        durchgang(a, 1, "V.24", b, 1, "V.24", true);
    }
    {
        SCOPED_TRACE("PRG 710-1 Tester, PRG 710 Gegenstelle");
        durchgang(b, 1, "V.24", a, 1, "V.24", true);
    }
    a.maschine().serialHub()->stopAlle();
    b.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Prg710_1_Zifss_MitPrg710_1
 * @brief ZIFSS (A32 Kanal A) ↔ ZIFSS am 710-1: ECHO und FLUSS-XON über die IFSS; die Tastatur
 *        K7672 an A32 Kanal B bleibt unberührt (beide Rechner danach bedienbar).  Dann am 710 der
 *        IFSS-Hauptdrucker (A32 Kanal B) gegen den ZIFSS (A32 Kanal A) eines anderen 710.
 */
TEST(SertestKopplung, Prg710_1_Zifss_MitPrg710_1) {
    SertestPrg t(prg710test::V::Prg710_1), g(prg710test::V::Prg710_1);
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 1));
    durchgang(t, 2, "ZIFSS", g, 2, "ZIFSS", false);
    EXPECT_TRUE(t.dirFindetSertest()) << t.bild();
    EXPECT_TRUE(g.dirFindetSertest()) << g.bild();
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

TEST(SertestKopplung, Prg710_IfssHauptdruckerGegenZifss) {
    SertestPrg t(prg710test::V::Prg710), g(prg710test::V::Prg710);
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 2, t, 1));   // Gegenstelle ZIFSS (A32-A), Tester IFSS Haupt (A32-B)
    durchgang(t, 2, "IFSS Hauptdrucker", g, 3, "ZIFSS", false);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Prg710_1_MitPc1715UndDerAltenV02_BeideRichtungen
 * @brief PRG 710-1 mit V0.3 ↔ PC 1715 mit der BISHERIGEN V0.2 (`SERTEST_V02.COM`, CP/A 1715) an
 *        der V.24, beide Richtungen (PRG: CTS = V106 ∧ V107 wie A5120; 1715: CTS = RTS).
 */
TEST(SertestKopplung, Prg710_1_MitPc1715UndDerAltenV02_BeideRichtungen) {
    SertestPrg r(prg710test::V::Prg710_1);
    SertestPc1715 p(K1520_SERTEST_V02);
    ASSERT_TRUE(boot(r));
    ASSERT_TRUE(boot(p));
    ASSERT_NO_FATAL_FAILURE(koppeln(r, 0, p, 1));
    {
        SCOPED_TRACE("PRG 710-1 (V0.3) Tester, PC 1715 (V0.2) Gegenstelle");
        durchgang(r, 1, "V.24", p, 2, "V.24", true);
    }
    {
        SCOPED_TRACE("PC 1715 (V0.2) Tester, PRG 710-1 (V0.3) Gegenstelle");
        durchgang(p, 2, "V.24", r, 1, "V.24", true);
    }
    r.maschine().serialHub()->stopAlle();
    p.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.Prg710_MitA5120UndDerAltenV01_BeideRichtungen
 * @brief PRG 710 mit V0.3 ↔ A5120 mit der BISHERIGEN V0.1 (`SERTEST_V01.COM`, CP/A) an der V.24
 *        (gleiche K8025-Logik), beide Richtungen.  Danach PRG ZIFSS (710-1) ↔ A5120 DFÜ/IFSS.
 */
TEST(SertestKopplung, Prg710_MitA5120UndDerAltenV01_BeideRichtungen) {
    SertestPrg r(prg710test::V::Prg710);
    SertestA5120 a(K1520_SERTEST_V01);
    ASSERT_TRUE(boot(r));
    ASSERT_TRUE(boot(a));
    ASSERT_NO_FATAL_FAILURE(koppeln(a, 0, r, 0));
    {
        SCOPED_TRACE("PRG 710 (V0.3) Tester, A5120 (V0.1) Gegenstelle");
        durchgang(r, 1, "V.24", a, 1, "DFUE/V.24", true);
    }
    {
        SCOPED_TRACE("A5120 (V0.1) Tester, PRG 710 (V0.3) Gegenstelle");
        durchgang(a, 1, "DFUE/V.24", r, 1, "V.24", true);
    }
    a.maschine().serialHub()->stopAlle();
    r.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.A5120_Ifss_Echo
 * @brief A5120 ↔ A5120 an DFÜ/IFSS: LEITUNGEN ENTFAELLT, ECHO OK.  Der Tester ist hier der
 *        Server.
 */
TEST(SertestKopplung, A5120_Ifss_Echo) {
    SertestA5120 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(t, 1, g, 1));
    durchgang(t, 2, "DFUE/IFSS", g, 2, "DFUE/IFSS", false);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.K8915_V24UndIfss2
 * @brief K8915 ↔ K8915: V.24 (LEITUNGEN mit der Probe der K8915-Gegenstelle, ECHO), danach
 *        in derselben Sitzung DFÜ/IFSS2 (an der SIO der Tastatur, Vektor des BIOS).
 */
TEST(SertestKopplung, K8915_V24UndIfss2) {
    SertestK8915 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 1, t, 1));
    durchgang(t, 2, "V.24", g, 2, "V.24", true);
    ASSERT_NO_FATAL_FAILURE(koppeln(t, 2, g, 2));
    durchgang(t, 3, "DFUE/IFSS2", g, 3, "DFUE/IFSS2", false);
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.A5120_K8915_V24_BeideRichtungen
 * @brief Gemischt an der V.24: erst A5120 als Tester gegen K8915 als Gegenstelle (an der
 *        A5120 ist CTS = V106 ∧ V107 — sie sieht jede RTS-Flanke der Gegenstelle, also
 *        auch eine falsche Probe), dann umgekehrt.  A5120 = Server, K8915 = Client.
 */
TEST(SertestKopplung, A5120_K8915_V24_BeideRichtungen) {
    SertestA5120 a;
    SertestK8915 k;
    ASSERT_TRUE(boot(a));
    ASSERT_TRUE(boot(k));
    ASSERT_NO_FATAL_FAILURE(koppeln(a, 0, k, 1));
    {
        SCOPED_TRACE("A5120 Tester, K8915 Gegenstelle");
        durchgang(a, 1, "DFUE/V.24", k, 2, "V.24", true);
    }
    {
        SCOPED_TRACE("K8915 Tester, A5120 Gegenstelle");
        durchgang(k, 2, "V.24", a, 1, "DFUE/V.24", true);
    }
    a.maschine().serialHub()->stopAlle();
    k.maschine().serialHub()->stopAlle();
}

/**
 * @test SertestKopplung.OhneGegenstelleZeitueberlauf
 * @brief Gegenfall: verbunden, aber auf dem anderen Rechner läuft SERTEST nicht — an der
 *        V.24 bestätigt niemand die Leitungen (`FEHLER RTS=…`), ECHO, FLUSS-HW und
 *        FLUSS-XON enden mit `FEHLER ZEITUEBERLAUF BESTAETIGUNG` (Auto Enables erst nach
 *        der Bestätigung — sonst hieße es hier `SENDER BLOCKIERT`); danach ist der
 *        Tester bedienbar.
 */
TEST(SertestKopplung, OhneGegenstelleZeitueberlauf) {
    SertestA5120 t, g;
    ASSERT_TRUE(boot(t));
    ASSERT_TRUE(boot(g));
    ASSERT_NO_FATAL_FAILURE(koppeln(g, 0, t, 0));
    Paar<SertestA5120, SertestA5120> p{t, g};
    t.tippe("sertest t 1 /g /a\r");
    ASSERT_TRUE(p.bisEnde()) << t.bild();
    const auto& pt = t.protokoll();
    EXPECT_EQ(pt.wert("DFUE/V.24", "LEITUNGEN").value_or("-").rfind("FEHLER RTS=", 0), 0u) << pt.text();
    for (const char* teil : {"ECHO", "FLUSS-HW", "FLUSS-XON"})
        EXPECT_EQ(pt.wert("DFUE/V.24", teil).value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG")
            << teil << "\n" << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "FEHLER") << pt.text();
    EXPECT_NE(t.bild().find("bestaetigt: NEIN"), std::string::npos) << t.bild();
    ASSERT_TRUE(t.bisPrompt(kFrist)) << t.bild();
    EXPECT_TRUE(t.dirFindetSertest()) << t.bild();
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

#endif
