/**
 * @file test_sertest_kopplung.cpp
 * @brief SertestKopplung.* — SERTEST.COM als Tester gegen SERTEST.COM als Gegenstelle auf
 *        einem zweiten Rechner (Entwurf 19 §14.6/§14.7/§14.8, AP-ST5): zwei Maschinen im
 *        selben Prozess über RFC 2217 (Server ↔ Client, Nullmodem-Kreuzung §6.4).
 *
 * Je Fall zwei Kaltstarts; die Gegenstelle läuft mit `sertest g <n>`, der Tester mit
 * `sertest t <n> /g /a` (nur der Gegenstellentest — ohne Loop fiele der Prüfstecker
 * ohnehin durch).  Geprüft werden die Ergebniszeilen (§14.3) beider Seiten: LEITUNGEN
 * (nur V.24, über den Leitungsspiegel der Gegenstelle) und ECHO (4096 Bytes, Bericht
 * ohne Empfangsfehler); FLUSS-HW/FLUSS-XON bleiben bis AP-ST6 `NICHT EINGEBAUT`, das
 * Ende deshalb `FEHLER`.
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
#include <string>
#include <thread>

#include "core/serial/hub.h"
#include "tests/system/sertest_hilfen.h"

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
constexpr double    kPhi     = 2'457'600.0;     ///< beide Maschinen
constexpr long long kFrist   = 150'000'000;     ///< ≈ 60 s Maschinenzeit je Lauf
constexpr long long kBoot    = 100'000'000;

/// Zwei Maschinen in gleichen Scheiben, gedrosselt.
template <class T, class G>
struct Paar {
    T& t;
    G& g;

    bool bis(const std::function<bool()>& fertig, long long frist = kFrist) {
        const auto t0 = Uhr::now();
        for (long long n = 0;;) {
            t.erfasse();
            g.erfasse();
            if (fertig()) return true;
            if (n >= frist) return false;
            n += t.lauf(kScheibe);
            g.lauf(kScheibe);
            const auto soll = std::chrono::duration<double>(n / kPhi / kFaktor);
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

template <class S>
::testing::AssertionResult boot(S& s) {
    if (!s.fehler().empty()) return ::testing::AssertionFailure() << s.fehler();
    if (!s.kaltstart()) return ::testing::AssertionFailure() << "kein Prompt\n" << s.bild();
    return ::testing::AssertionSuccess();
}

/// Ein Durchgang: Gegenstelle `sertest g <ng>` an @p g, Tester `sertest t <nt> /g /a` an
/// @p t; erwartet LEITUNGEN (@p v24: OK, sonst ENTFAELLT), ECHO OK.  Danach Ctrl+C an der
/// Gegenstelle, beide wieder am Prompt (für einen weiteren Durchgang).
template <class T, class G>
void durchgang(T& t, int nt, const std::string& nameT, G& g, int ng, const std::string& nameG,
               bool v24) {
    Paar<T, G> p{t, g};
    ASSERT_TRUE(t.neuerLauf()) << t.bild();
    ASSERT_TRUE(g.neuerLauf()) << g.bild();
    g.tippe("sertest g " + std::to_string(ng) + "\r");
    ASSERT_TRUE(p.bisText(g, "Gegenstelle an " + nameG + " bereit.")) << g.bild();
    t.tippe("sertest t " + std::to_string(nt) + " /g /a\r");
    ASSERT_TRUE(p.bisEnde()) << "Tester:\n" << t.bild() << "\nGegenstelle:\n" << g.bild();
    const std::string bt = t.bild();
    const auto& pt = t.protokoll();
    EXPECT_EQ(pt.wert(nameT, "LEITUNGEN").value_or("-"), v24 ? "OK" : "ENTFAELLT")
        << pt.text() << bt << "\nGegenstelle:\n" << g.bild();
    EXPECT_EQ(pt.wert(nameT, "ECHO").value_or("-"), "OK")
        << pt.text() << "Tester: " << lage(t, nt - 1) << "\nGegenstelle: " << lage(g, ng - 1);
    EXPECT_EQ(pt.wert(nameT, "FLUSS-XON").value_or("-"), "FEHLER NICHT EINGEBAUT") << pt.text();
    EXPECT_FALSE(pt.wert(nameT, "DATEN-LOOP")) << "nur /G\n" << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "FEHLER") << "FLUSS-* fehlen noch\n" << pt.text();
    EXPECT_EQ(bt.find("bestaetigt: NEIN"), std::string::npos) << bt;

    // Gegenstelle: einmal INTERRUPT OK, Abschnitt E ohne Empfangsfehler, zurück im Ruhezustand.
    ASSERT_TRUE(p.bisText(g, "Abschnitt fertig, Empfangsfehler 0000H")) << g.bild();
    EXPECT_EQ(g.protokoll().wert("", "INTERRUPT").value_or("-"), "OK") << g.protokoll().text();
    const std::string bg = g.bild();
    EXPECT_NE(bg.find("Abschnitt E: 1000H Bytes"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("verworfen"), std::string::npos) << bg;
    EXPECT_EQ(bg.find("Zeitueberlauf"), std::string::npos) << bg;

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

#else

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
 *        V.24 bestätigt niemand die Leitungen (`FEHLER RTS=…`), ECHO endet mit
 *        `FEHLER ZEITUEBERLAUF BESTAETIGUNG`; danach ist der Tester bedienbar.
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
    EXPECT_EQ(pt.wert("DFUE/V.24", "ECHO").value_or("-"), "FEHLER ZEITUEBERLAUF BESTAETIGUNG") << pt.text();
    EXPECT_EQ(pt.ende().value_or("-"), "FEHLER") << pt.text();
    EXPECT_NE(t.bild().find("bestaetigt: NEIN"), std::string::npos) << t.bild();
    ASSERT_TRUE(t.bisPrompt(kFrist)) << t.bild();
    EXPECT_TRUE(t.dirFindetSertest()) << t.bild();
    t.maschine().serialHub()->stopAlle();
    g.maschine().serialHub()->stopAlle();
}

#endif
