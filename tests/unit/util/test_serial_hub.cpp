/**
 * @file test_serial_hub.cpp
 * @brief `SerialHub` über echtes Loopback-TCP (Entwurf 19 §7, §11): Telnet- und
 *        RFC-2217-Rundlauf zwischen zwei Hubs, Dauerversuch des Clients, Abweisen am
 *        belegten Server, Portsuche/Wiederaufnahme, Sperrregel, Loop, Datei.
 *
 * **Nie feste Ports** (ctest -j): Server auf Port 0 bzw. ab einem zufälligen hohen Port,
 * der tatsächliche Port kommt aus dem Status.  Gewartet wird auf ZUSTÄNDE mit Frist,
 * nicht mit festen Pausen — die Maschine „läuft" dabei im Testfaden (`Uhrwerk`).
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include "core/serial/hub.h"
#include "core/serial/transport.h"
#include "core/serial/rfc2217_codec.h"
#include "core/serial/net/socket.h"
#include "tests/support/temp_path.h"
#include "tests/unit/util/attrappe_anschluss.h"

using namespace k1520::serial;
using k1520test::AttrappeAnschluss;
using Uhr = std::chrono::steady_clock;
using std::chrono::milliseconds;

namespace {

/// Eine „Maschine": Attrappe + Hub (die Attrappe lebt länger als der Hub).
struct Seite {
    AttrappeAnschluss a;
    SerialHub hub;
    Seite() { hub.registriere(a); }
    SerialStatus st() const { return hub.status(0); }
    Zustand zustand() const { return st().zustand; }
};

/// Der Emulationsfaden der Tests: taktet alle Seiten, der Gast liest sein FIFO.
struct Uhrwerk {
    std::vector<Seite*> seiten;
    uint64_t z = 0;
    void schritt() {
        for (int i = 0; i < 100; ++i) {
            z += 64;
            for (Seite* s : seiten) {
                s->hub.takt(z);
                s->a.lies();
            }
        }
    }
    /// Bis `bedingung` gilt, höchstens `ms` Uhrzeit.
    bool warte(const std::function<bool()>& bedingung, int ms) {
        const auto ende = Uhr::now() + milliseconds(ms);
        while (Uhr::now() < ende) {
            schritt();
            if (bedingung()) return true;
            std::this_thread::sleep_for(milliseconds(1));
        }
        return bedingung();
    }
    void laufe(int ms) { warte([] { return false; }, ms); }
};

SerialKonfig server(Betriebsart b, uint16_t port = 0) {
    SerialKonfig k;
    k.betriebsart = b;
    k.rolle       = Rolle::Server;
    k.port        = port;
    return k;
}

SerialKonfig client(Betriebsart b, uint16_t port) {
    SerialKonfig k;
    k.betriebsart = b;
    k.rolle       = Rolle::Client;
    k.host        = "127.0.0.1";
    k.port        = port;
    return k;
}

int zufallsPort() {
    static std::mt19937 g{std::random_device{}()};
    return 20000 + static_cast<int>(g() % 20000);
}

/// Ein Port, auf dem gerade niemand lauscht.
uint16_t freierPort() {
    const auto p = net::portPruefen(zufallsPort());
    return static_cast<uint16_t>(p.vorschlag);
}

/// Server A und Client B verbinden (Betriebsart b); false = Zeitüberschreitung.
bool verbinde(Uhrwerk& u, Seite& a, Seite& b, Betriebsart art) {
    if (!a.hub.konfigurieren(0, server(art)) || !a.hub.start(0)) return false;
    const uint16_t port = a.st().port_aktiv;
    if (!port) return false;
    if (!b.hub.konfigurieren(0, client(art, port)) || !b.hub.start(0)) return false;
    return u.warte([&] {
        return a.zustand() == Zustand::Verbunden && b.zustand() == Zustand::Verbunden;
    }, 5000);
}

std::string dateiInhalt(const std::string& pfad) {
    std::ifstream f(std::filesystem::u8path(pfad), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

// ─── Rundlauf zwischen zwei Hubs ───────────────────────────────────────────

TEST(SerialHub, TelnetRundlaufZwischenZweiHubs) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Telnet));
    EXPECT_FALSE(a.st().gegenstelle.empty());
    EXPECT_FALSE(b.st().gegenstelle.empty());
    EXPECT_EQ(b.st().rolle, Rolle::Client);
    u.laufe(100);   // Telnet-Verhandlung (BINARY) abwarten: danach ist CR durchsichtig

    const std::string hin = std::string("Hallo\xFF\r\n Welt\x00!", 15);
    a.a.sende(hin);
    b.a.sende("zurueck");
    ASSERT_TRUE(u.warte([&] {
        return b.a.gelesenText() == hin && a.a.gelesenText() == "zurueck";
    }, 5000)) << "B: '" << b.a.gelesenText() << "' A: '" << a.a.gelesenText() << "'";
    EXPECT_EQ(a.st().bytes_gesendet, hin.size());
    EXPECT_EQ(b.st().bytes_empfangen, hin.size());
    // Telnet: Eingänge = „verbunden".
    EXPECT_TRUE(a.a.cts);
    EXPECT_TRUE(a.a.dcd);
    EXPECT_EQ(a.a.ueberlauf, 0u);
    EXPECT_EQ(b.st().baud_gegenseite, 0u);   // bei Telnet nicht erkennbar
    for (const SerialStatus& st : {a.st(), b.st()}) {   // AP-S11: ebenso Format und Leitungen
        EXPECT_FALSE(st.format_gegenseite_bekannt);
        EXPECT_FALSE(st.format_abweichend);
        EXPECT_EQ(st.daten_gegenseite, 0);
        EXPECT_EQ(st.leitungen_gegenseite_bekannt, 0);
        EXPECT_EQ(st.leitungen_gegenseite, 0);
    }
}

TEST(SerialHub, Rfc2217RundlaufMitNullmodemLeitungenUndBaudhinweis) {
    Seite a, b;
    a.a.rtsAus = true;
    a.a.dtrAus = false;                                  // Server-Gast 9600
    b.a.rtsAus = false;
    b.a.dtrAus = true;
    b.a.fmt    = serialFormatRechnen(16, 8, 0, 2, 128);  // Client-Gast 1200
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Rfc2217));

    // Nullmodem: B.RTS → A.CTS, B.DTR → A.DSR/DCD und umgekehrt.
    EXPECT_TRUE(u.warte([&] {
        return !a.a.cts && a.a.dsr && a.a.dcd && b.a.cts && !b.a.dsr && !b.a.dcd;
    }, 3000)) << "A cts/dsr/dcd " << a.a.cts << a.a.dsr << a.a.dcd << " B " << b.a.cts
              << b.a.dsr << b.a.dcd;

    // Baudunterschied: beide Seiten zeigen ihn an (Client nach 100 ms Entprellung).
    EXPECT_TRUE(u.warte([&] {
        const auto sa = a.st(), sb = b.st();
        return sa.baud_gegenseite == 1200 && sa.baud_abweichend &&
               sb.baud_gegenseite == 9600 && sb.baud_abweichend;
    }, 3000)) << a.st().baud_gegenseite << " / " << b.st().baud_gegenseite;
    EXPECT_EQ(a.st().baud_nenn, 9600u);   // die Anfrage hat den Gast nicht verändert

    // B hebt RTS → A sieht CTS; Daten fließen in beide Richtungen.
    b.a.rtsAus = true;
    ASSERT_TRUE(u.warte([&] { return a.a.cts; }, 3000));
    a.a.sende("vom Server");
    b.a.sende("vom Client");
    ASSERT_TRUE(u.warte([&] {
        return b.a.gelesenText() == "vom Server" && a.a.gelesenText() == "vom Client";
    }, 5000)) << "B: '" << b.a.gelesenText() << "' A: '" << a.a.gelesenText() << "'";
}

// AP-S11: Format und Leitungen der Gegenseite (Server: Wunsch/RTS+DTR des Clients,
// Client: Antwort/CTS+DSR+DCD+RI des Servers).
TEST(SerialHub, Rfc2217ZeigtFormatUndLeitungenDerGegenseite) {
    Seite a, b;
    a.a.rtsAus = true;                                   // Server-Gast 9600 8N1, RTS an, DTR aus
    a.a.dtrAus = false;
    b.a.rtsAus = false;                                  // Client-Gast 1200 7E1, RTS aus, DTR an
    b.a.dtrAus = true;
    b.a.fmt    = serialFormatRechnen(16, 7, 2, 2, 128);
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Rfc2217));

    using namespace k1520::serial::gegenleitung;
    EXPECT_TRUE(u.warte([&] {
        const auto sa = a.st(), sb = b.st();
        // Das Format des Clients kommt erst nach der Entprellung (100 ms) richtig an.
        return sa.format_gegenseite_bekannt && sa.daten_gegenseite == 7 &&
               sa.paritaet_gegenseite == 2 && sb.format_gegenseite_bekannt &&
               sa.leitungen_gegenseite_bekannt == (RTS | DTR) &&
               (sb.leitungen_gegenseite_bekannt & CTS);
    }, 3000));
    const SerialStatus sa = a.st(), sb = b.st();
    // Server sieht den Wunsch des Clients: 7E1 (Parität 2 = gerade, 2 halbe Stoppbits).
    EXPECT_EQ(sa.daten_gegenseite, 7);
    EXPECT_EQ(sa.paritaet_gegenseite, 2);
    EXPECT_EQ(sa.stopp_halbe_gegenseite, 2);
    EXPECT_TRUE(sa.format_abweichend);
    // Client sieht die Antwort des Servers: 8N1.
    EXPECT_EQ(sb.daten_gegenseite, 8);
    EXPECT_EQ(sb.paritaet_gegenseite, 0);
    EXPECT_EQ(sb.stopp_halbe_gegenseite, 2);
    EXPECT_TRUE(sb.format_abweichend);
    // Leitungen: Server sieht RTS (aus) und DTR (an) des Clients, sonst nichts.
    EXPECT_EQ(sa.leitungen_gegenseite, DTR);
    // Client sieht CTS (= Server-RTS) an, DSR/DCD (= Server-DTR) aus; RI ruht.
    EXPECT_EQ(sb.leitungen_gegenseite_bekannt, CTS | DSR | DCD | RI);
    EXPECT_EQ(sb.leitungen_gegenseite, CTS);

    // Gleiches Format auf beiden Seiten: keine Abweichung mehr.
    b.a.fmt = a.a.fmt;
    EXPECT_TRUE(u.warte([&] { return !a.st().format_abweichend && !b.st().format_abweichend; }, 3000))
        << a.st().format_abweichend << b.st().format_abweichend;
    EXPECT_EQ(a.st().daten_gegenseite, 8);
    EXPECT_EQ(a.st().format_gegenseite_bekannt, true);
}

// ─── Portwahl, Sperren, Loop ───────────────────────────────────────────────

TEST(SerialHub, BelegterPortVonHandSuchtWeiterAutomatischKommtNurEinVorschlag) {
    net::Lauscher belegt = net::lauschenMitSuche(zufallsPort());
    ASSERT_TRUE(belegt.sock);
    const auto p = static_cast<uint16_t>(belegt.port);

    Seite s;
    ASSERT_TRUE(s.hub.konfigurieren(0, server(Betriebsart::Telnet, p)));
    ASSERT_TRUE(s.hub.start(0));
    EXPECT_EQ(s.zustand(), Zustand::Lauscht);
    EXPECT_GT(s.st().port_aktiv, p);
    s.hub.stop(0);
    EXPECT_EQ(s.zustand(), Zustand::Aus);

    EXPECT_FALSE(s.hub.startAuto(0));
    const SerialStatus st = s.st();
    EXPECT_EQ(st.zustand, Zustand::Aus);
    EXPECT_GT(st.port_vorschlag, p);
    EXPECT_NE(st.meldung.find("belegt"), std::string::npos) << st.meldung;
    EXPECT_EQ(s.hub.konfig(0).port, p);   // das Feld trägt die Oberfläche nach

    SerialKonfig k = s.hub.konfig(0);
    k.port = st.port_vorschlag;
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    if (!s.hub.startAuto(0)) GTEST_SKIP() << "Vorschlag inzwischen belegt (fremder Prozess)";
    EXPECT_EQ(s.st().port_aktiv, st.port_vorschlag);
    EXPECT_EQ(s.st().port_vorschlag, 0u);
}

TEST(SerialHub, ImBetriebSindNurDieGesperrtenFelderGesperrt) {
    Seite s;
    ASSERT_TRUE(s.hub.konfigurieren(0, server(Betriebsart::Telnet)));
    ASSERT_TRUE(s.hub.start(0));
    SerialKonfig k = s.hub.konfig(0);
    k.port = 4711;
    EXPECT_FALSE(s.hub.konfigurieren(0, k));
    EXPECT_EQ(s.hub.konfig(0).port, 0u);   // nichts übernommen
    k = s.hub.konfig(0);
    k.betriebsart = Betriebsart::Rfc2217;
    EXPECT_FALSE(s.hub.konfigurieren(0, k));
    k = s.hub.konfig(0);
    k.xonxoff        = true;
    k.rtscts_bruecke = true;
    k.taktquelle     = 1;
    EXPECT_TRUE(s.hub.konfigurieren(0, k));
    EXPECT_TRUE(s.hub.wandler(0).einstellung().xonxoff);
    EXPECT_EQ(s.zustand(), Zustand::Lauscht);
    k.taktquelle = 2;   // gibt es nicht
    EXPECT_FALSE(s.hub.konfigurieren(0, k));
    s.hub.stop(0);
    k = s.hub.konfig(0);
    k.port = 4711;
    EXPECT_TRUE(s.hub.konfigurieren(0, k));
}

TEST(SerialHub, LoopBeendetDieVerbindungUndSperrtDenStart) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Telnet));
    SerialKonfig k = a.hub.konfig(0);
    k.loop = true;
    ASSERT_TRUE(a.hub.konfigurieren(0, k));
    EXPECT_EQ(a.zustand(), Zustand::Aus);   // Server hört auf zu lauschen
    EXPECT_FALSE(a.hub.start(0));
    EXPECT_FALSE(a.hub.startAuto(0));
    EXPECT_EQ(a.zustand(), Zustand::Aus);
    // Der Client merkt die Trennung und geht in den Dauerversuch.
    // … und zwar sofort, nicht erst, wenn der I/O-Faden des Servers von selbst aufwacht.
    EXPECT_TRUE(u.warte([&] { return b.zustand() == Zustand::Verbindet; }, 500));
    // Loop wirkt: der Gast hört sich selbst.
    a.a.sende("echo");
    EXPECT_TRUE(u.warte([&] { return a.a.gelesenText() == "echo"; }, 3000));
}

TEST(SerialHub, UngueltigerHostIstEinFehler) {
    Seite s;
    SerialKonfig k = client(Betriebsart::Telnet, 5000);
    k.host = "host:5000";
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    EXPECT_FALSE(s.hub.start(0));
    EXPECT_EQ(s.zustand(), Zustand::Fehler);
    EXPECT_FALSE(s.st().meldung.empty());
}

// ─── Client-Dauerversuch (§7.1) ────────────────────────────────────────────

TEST(SerialClientDauerversuch, VerbindetBeimNaechstenTaktNachdemDerServerStartet) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    const uint16_t port = freierPort();
    ASSERT_NE(port, 0);
    ASSERT_TRUE(b.hub.konfigurieren(0, client(Betriebsart::Telnet, port)));
    ASSERT_TRUE(b.hub.start(0));
    EXPECT_EQ(b.zustand(), Zustand::Verbindet);

    u.laufe(2500);   // Server erst nach 2,5 s
    SerialStatus sb = b.st();
    EXPECT_EQ(sb.zustand, Zustand::Verbindet);
    EXPECT_GE(sb.versuche, 2u);
    EXPECT_LE(sb.versuche, 4u);   // alle 1000 ms, nicht öfter
    EXPECT_FALSE(sb.meldung.empty()) << "letzter Grund fehlt";

    ASSERT_TRUE(a.hub.konfigurieren(0, server(Betriebsart::Telnet, port)));
    ASSERT_TRUE(a.hub.start(0));
    if (a.st().port_aktiv != port) GTEST_SKIP() << "Port inzwischen fremd belegt";
    const auto t0 = Uhr::now();
    ASSERT_TRUE(u.warte([&] { return b.zustand() == Zustand::Verbunden; }, 3000));
    EXPECT_LE(Uhr::now() - t0, milliseconds(1500)) << "nicht beim nächsten Takt verbunden";
    EXPECT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 1000));
    EXPECT_EQ(b.st().versuche, 0u);
    EXPECT_TRUE(b.st().meldung.empty());
}

TEST(SerialClientDauerversuch, VerbindetErneutNachdemDerServerGetrenntHat) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Telnet));
    const uint16_t port = a.st().port_aktiv;

    a.hub.stop(0);
    ASSERT_TRUE(u.warte([&] { return b.zustand() == Zustand::Verbindet; }, 3000));
    EXPECT_FALSE(b.st().meldung.empty());

    SerialKonfig k = a.hub.konfig(0);
    k.port = port;
    ASSERT_TRUE(a.hub.konfigurieren(0, k));
    ASSERT_TRUE(a.hub.start(0));
    if (a.st().port_aktiv != port) GTEST_SKIP() << "Port inzwischen fremd belegt";
    EXPECT_TRUE(u.warte([&] {
        return b.zustand() == Zustand::Verbunden && a.zustand() == Zustand::Verbunden;
    }, 3000));
}

TEST(SerialClientDauerversuch, TrennenBeendetDieVersucheSofort) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    const uint16_t port = freierPort();
    ASSERT_NE(port, 0);
    ASSERT_TRUE(b.hub.konfigurieren(0, client(Betriebsart::Telnet, port)));
    ASSERT_TRUE(b.hub.start(0));
    u.laufe(200);
    b.hub.stop(0);
    EXPECT_EQ(b.zustand(), Zustand::Aus);   // sofort, nicht nach dem laufenden Versuch
    EXPECT_EQ(b.st().versuche, 0u);

    ASSERT_TRUE(a.hub.konfigurieren(0, server(Betriebsart::Telnet, port)));
    ASSERT_TRUE(a.hub.start(0));
    u.laufe(1500);   // länger als ein Versuchsabstand
    EXPECT_EQ(a.zustand(), Zustand::Lauscht);
    EXPECT_EQ(b.zustand(), Zustand::Aus);
}

TEST(SerialClientDauerversuch, ZweiterClientWirdAbgewiesenDerErsteBleibt) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    ASSERT_TRUE(verbinde(u, a, b, Betriebsart::Telnet));
    const std::string gegen = a.st().gegenstelle;
    const uint16_t port = a.st().port_aktiv;

    net::Fehler f;
    auto ziele = net::aufloesen("127.0.0.1", port, &f);
    ASSERT_FALSE(ziele.empty()) << f.text;
    net::Socket zweiter = net::verbindenAlle(ziele, 2000, &f);
    ASSERT_TRUE(zweiter) << f.text;

    // Der Server nimmt an, schreibt „belegt" und schließt.
    std::string empfangen;
    bool zu = false;
    const auto ende = Uhr::now() + milliseconds(3000);
    while (!zu && Uhr::now() < ende) {
        u.schritt();
        std::vector<net::PollEintrag> pe(1);
        pe[0].fd    = zweiter.fd();
        pe[0].lesen = true;
        net::warten(pe, 5);
        char buf[256];
        const auto r = net::empfangen(zweiter.fd(), buf, sizeof buf);
        if (r.status == net::IoStatus::Ok) empfangen.append(buf, r.n);
        else if (r.status != net::IoStatus::Warten) zu = true;
    }
    EXPECT_TRUE(zu) << "zweiter Client nicht getrennt";
    EXPECT_NE(empfangen.find("belegt"), std::string::npos) << empfangen;

    // Der erste bleibt verbunden und bedient.
    EXPECT_EQ(a.zustand(), Zustand::Verbunden);
    EXPECT_EQ(a.st().gegenstelle, gegen);
    EXPECT_EQ(b.zustand(), Zustand::Verbunden);
    u.laufe(100);
    a.a.sende("noch da");
    EXPECT_TRUE(u.warte([&] { return b.a.gelesenText() == "noch da"; }, 3000));
}

// ─── Datei (§6.6) ──────────────────────────────────────────────────────────

TEST(SerialDatei, SchreibtUeberschreibtUndHaengtBeimWiederaufnehmenAn) {
    const std::string pfad = k1520test::tempPath("k1520_test_serial_datei.txt");
    std::filesystem::remove(std::filesystem::u8path(pfad));
    Seite s;
    Uhrwerk u{{&s}};
    SerialKonfig k;
    k.betriebsart = Betriebsart::Datei;
    k.datei       = pfad;
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    ASSERT_TRUE(s.hub.start(0));
    EXPECT_EQ(s.zustand(), Zustand::Verbunden);
    s.a.sende("abc");
    // Ohne Beenden: spätestens nach 0,5 s steht es in der Datei.
    EXPECT_TRUE(u.warte([&] { return dateiInhalt(pfad) == "abc"; }, 3000)) << dateiInhalt(pfad);
    EXPECT_TRUE(s.a.cts);   // Eingänge wie „verbunden"
    s.hub.stop(0);
    EXPECT_EQ(s.zustand(), Zustand::Aus);

    // Wiederaufnahme: anhängend.
    ASSERT_TRUE(s.hub.startAuto(0));
    s.a.sende("def");
    ASSERT_TRUE(u.warte([&] { return s.a.gastSendet.empty(); }, 3000));
    s.hub.stop(0);   // schreibt den Rest
    EXPECT_EQ(dateiInhalt(pfad), "abcdef");

    // Start von Hand: überschreibend.
    ASSERT_TRUE(s.hub.start(0));
    s.a.sende("g");
    ASSERT_TRUE(u.warte([&] { return s.a.gastSendet.empty(); }, 3000));
    s.hub.stop(0);
    EXPECT_EQ(dateiInhalt(pfad), "g");
    EXPECT_TRUE(s.a.gelesen.empty());   // Empfang bleibt leer
    std::filesystem::remove(std::filesystem::u8path(pfad));
}

TEST(SerialDatei, NichtOeffnbareDateiIstEinFehler) {
    Seite s;
    SerialKonfig k;
    k.betriebsart = Betriebsart::Datei;
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    EXPECT_FALSE(s.hub.start(0));   // keine Datei gewählt
    EXPECT_EQ(s.zustand(), Zustand::Fehler);

    k.datei = k1520test::tempPath("k1520_test_gibt_es_nicht") + "/unter/druck.txt";
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    EXPECT_FALSE(s.hub.start(0));
    EXPECT_EQ(s.zustand(), Zustand::Fehler);
    EXPECT_FALSE(s.st().meldung.empty());
}

TEST(SerialDatei, SchreibfehlerSetztDenZustandFehler) {
#if defined(_WIN32)
    GTEST_SKIP() << "/dev/full gibt es nur unter POSIX";
#else
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "kein /dev/full";
    Seite s;
    Uhrwerk u{{&s}};
    SerialKonfig k;
    k.betriebsart = Betriebsart::Datei;
    k.datei       = "/dev/full";
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    ASSERT_TRUE(s.hub.startAuto(0));
    s.a.sende("x");
    ASSERT_TRUE(u.warte([&] { return s.zustand() == Zustand::Fehler; }, 3000));
    EXPECT_NE(s.st().meldung.find("Schreibfehler"), std::string::npos) << s.st().meldung;
#endif
}

// ─── Fehlerwege und Gegenseiten ohne Hub (AP-T1b) ──────────────────────────
//
// Die Rundläufe oben sprechen Hub gegen Hub — beide Seiten benehmen sich.  Hier ist die
// Gegenseite ein roher Socket im Testfaden: sie schickt zerrissene oder unsinnige
// Telnet-Sequenzen, bricht mitten im Rückstau ab oder spricht RFC 2217 Befehl für Befehl.

namespace {

/// Roher TCP-Client im Testfaden (nicht blockierend).
struct RohClient {
    net::Socket s;
    explicit RohClient(uint16_t port) {
        net::Fehler f;
        auto ziele = net::aufloesen("127.0.0.1", port, &f);
        if (!ziele.empty()) s = net::verbindenAlle(ziele, 2000, &f);
    }
    /// Alles hinausschicken (bei vollem Socketpuffer: weiter takten, nicht verwerfen).
    bool schick(const std::string& d, Uhrwerk* u = nullptr) {
        size_t weg = 0;
        const auto ende = Uhr::now() + milliseconds(5000);
        while (weg < d.size() && Uhr::now() < ende) {
            const auto r = net::senden(s.fd(), d.data() + weg, d.size() - weg);
            if (r.status == net::IoStatus::Ok) weg += r.n;
            else if (r.status != net::IoStatus::Warten) return false;
            else if (u) u->schritt();
        }
        return weg == d.size();
    }
    bool schick(const std::vector<uint8_t>& v, Uhrwerk* u = nullptr) {
        return schick(std::string(v.begin(), v.end()), u);
    }
    /// Was gerade da ist; `zu` wird gesetzt, wenn die Gegenseite getrennt hat.
    std::string hole(bool* zu = nullptr) {
        std::string r;
        char buf[4096];
        while (true) {
            const auto e = net::empfangen(s.fd(), buf, sizeof buf);
            if (e.status == net::IoStatus::Ok && e.n > 0) { r.append(buf, e.n); continue; }
            if (e.status != net::IoStatus::Warten && zu) *zu = true;
            break;
        }
        return r;
    }
};

/// Server der Betriebsart `b` auf einem freien Port starten; 0 = gescheitert.
uint16_t starteServer(Seite& s, Betriebsart b) {
    if (!s.hub.konfigurieren(0, server(b)) || !s.hub.start(0)) return 0;
    return s.st().port_aktiv;
}

/// Ein Muster ohne 0xFF (IAC) und ohne CR — durchsichtig auch ohne BINARY.
std::string muster(size_t n) {
    std::string m(n, '\0');
    for (size_t i = 0; i < n; ++i) m[i] = static_cast<char>('a' + (i * 7 + i / 26) % 26);
    return m;
}

/// Schnelles Format (1 Takt je Bit, 10 Takte je Zeichen): je `takt` ein Zeichen je
/// Richtung — hält die Rückstau-Tests kurz.
SerialFormat schnell() { return serialFormatRechnen(1, 8, 0, 2, 1); }

}  // namespace

TEST(SerialHubGegenseite, TelnetZerrisseneUndUnsinnigeSequenzenKommenSauberAn) {
    Seite a;
    Uhrwerk u{{&a}};
    const uint16_t port = starteServer(a, Betriebsart::Telnet);
    ASSERT_NE(port, 0);
    RohClient c(port);
    ASSERT_TRUE(c.s);
    ASSERT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));

    // Jedes Stück einzeln über die Leitung, damit der Codec wirklich mitten in einer
    // Sequenz neu ansetzen muss.
    auto stueck = [&](const std::string& s) {
        ASSERT_TRUE(c.schick(s));
        u.laufe(20);
    };
    stueck("A\xFF");                 // IAC IAC = 0xFF, zerrissen
    stueck("\xFF" "B");
    stueck("\xFF");                  // WILL ECHO in drei Stücken
    stueck("\xFB");
    stueck("\x01" "C");
    const std::string sb = std::string("\xFF\xFA\x99", 3) + std::string(3000, 'x') + "\xFF\xF0";
    stueck(sb.substr(0, 1500));      // Unterverhandlung über SUB_MAX, zerrissen
    stueck(sb.substr(1500));
    stueck("\xFF\xF1" "D");          // NOP
    stueck("\xFF\xFD\x63" "E");      // DO <unbekannte Option> → WONT, keine Nutzdaten
    ASSERT_TRUE(u.warte([&] { return a.a.gelesenText() == "A\xFF" "BCDE"; }, 3000))
        << "Gast las: '" << a.a.gelesenText() << "'";
    EXPECT_EQ(a.zustand(), Zustand::Verbunden);
    EXPECT_EQ(a.a.ueberlauf, 0u);

    // Antwort auf DO 0x63: WONT 0x63 (Q-Methode, kein Schweigen).
    std::string antwort;
    u.warte([&] { antwort += c.hole(); return antwort.find("\xFF\xFC\x63") != std::string::npos; },
            2000);
    EXPECT_NE(antwort.find("\xFF\xFC\x63"), std::string::npos);
}

TEST(SerialHubGegenseite, AbbruchImRueckstauVerliertNichtsUndDerServerLauschtWeiter) {
    Seite a;
    a.a.fmt = schnell();
    Uhrwerk u{{&a}};
    const uint16_t port = starteServer(a, Betriebsart::Telnet);
    ASSERT_NE(port, 0);
    RohClient c(port);
    ASSERT_TRUE(c.s);
    ASSERT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));

    // Der Gast liest NICHT: FIFO voll → Empfangspuffer voll → der Socket wird nicht mehr
    // gelesen (Rückstau).  12 KiB passen sonst in keinen Puffer des Wandlers.
    const std::string m = muster(12000);
    ASSERT_TRUE(c.schick(m));
    uint64_t z = u.z;
    auto taktOhneLesen = [&](int ms) {
        const auto ende = Uhr::now() + milliseconds(ms);
        while (Uhr::now() < ende) {
            for (int i = 0; i < 100; ++i) a.hub.takt(z += 64);
            std::this_thread::sleep_for(milliseconds(1));
        }
    };
    taktOhneLesen(300);
    EXPECT_EQ(a.st().puffer_empfangen, Wandler::PUFFER);
    EXPECT_EQ(a.a.fifo.size(), 3u);
    EXPECT_EQ(a.a.ueberlauf, 0u);

    // Die Gegenseite legt auf, während der Rückstau steht — sauber (FIN): erst die
    // Telnet-Verhandlung des Servers lesen, sonst schickt close() mit ungelesenen Daten
    // ein RST, und Winsock verwirft dann, was noch im Socketpuffer des Servers liegt
    // (das ist der Fall ResetImRueckstauTrenntSauber).
    c.hole();
    c.s.schliessen();
    taktOhneLesen(200);

    // Jetzt liest der Gast: ALLES kommt an, in Reihenfolge, danach lauscht der Server.
    u.z = z;
    ASSERT_TRUE(u.warte([&] {
        return a.a.gelesen.size() >= m.size() && a.zustand() == Zustand::Lauscht;
    }, 5000)) << "gelesen " << a.a.gelesen.size() << " Zustand " << int(a.zustand());
    EXPECT_EQ(a.a.gelesenText(), m);
    EXPECT_EQ(a.a.ueberlauf, 0u);
    EXPECT_EQ(a.st().port_aktiv, port);
    EXPECT_TRUE(a.st().gegenstelle.empty());
    EXPECT_FALSE(a.a.cts) << "Kabel ab: Eingänge inaktiv";

    // Und der nächste Client wird bedient.
    RohClient c2(port);
    ASSERT_TRUE(c2.s);
    EXPECT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));
}

TEST(SerialHubGegenseite, ResetImRueckstauTrenntSauber) {
    Seite a;
    a.a.fmt = schnell();
    Uhrwerk u{{&a}};
    const uint16_t port = starteServer(a, Betriebsart::Telnet);
    ASSERT_NE(port, 0);
    RohClient c(port);
    ASSERT_TRUE(c.s);
    ASSERT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));
    const std::string m = muster(12000);
    ASSERT_TRUE(c.schick(m));
    uint64_t z = u.z;
    for (int k = 0; k < 200; ++k) {
        for (int i = 0; i < 100; ++i) a.hub.takt(z += 64);
        std::this_thread::sleep_for(milliseconds(1));
    }
    ASSERT_EQ(a.st().puffer_empfangen, Wandler::PUFFER);

    // RST statt FIN (SO_LINGER 0): was der Kern noch nicht ausgeliefert hat, darf
    // verloren gehen — aber nichts verdreht, kein Überlauf, der Server lauscht weiter.
    struct linger l{};
    l.l_onoff  = 1;
    l.l_linger = 0;
#if defined(_WIN32)
    ::setsockopt(static_cast<SOCKET>(c.s.fd()), SOL_SOCKET, SO_LINGER,
                 reinterpret_cast<const char*>(&l), sizeof l);
#else
    ::setsockopt(static_cast<int>(c.s.fd()), SOL_SOCKET, SO_LINGER, &l, sizeof l);
#endif
    c.s.schliessen();
    u.z = z;
    ASSERT_TRUE(u.warte([&] { return a.zustand() == Zustand::Lauscht; }, 5000));
    u.laufe(100);
    const std::string g = a.a.gelesenText();
    EXPECT_GE(g.size(), Wandler::PUFFER) << "der Empfangspuffer wird noch zugestellt";
    EXPECT_EQ(g, m.substr(0, g.size()));
    EXPECT_EQ(a.a.ueberlauf, 0u);
    RohClient c2(port);
    ASSERT_TRUE(c2.s);
    EXPECT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));
}

TEST(SerialHubGegenseite, Rfc2217ServerBeantwortetJedenBefehlMitDemGastwert) {
    namespace r2 = ::serial::rfc2217;
    using A = ::serial::Rfc2217Ereignis::Art;
    Seite a;
    a.a.rtsAus = true;
    a.a.dtrAus = true;
    a.a.fmt = serialFormatRechnen(16, 7, 2, 4, 16);   // 9600 7E2
    Uhrwerk u{{&a}};
    const uint16_t port = starteServer(a, Betriebsart::Rfc2217);
    ASSERT_NE(port, 0);
    RohClient c(port);
    ASSERT_TRUE(c.s);
    ::serial::Rfc2217Codec k(::serial::TelnetRolle::Client);
    k.start();
    ASSERT_TRUE(c.schick(k.nimmAusgabe()));
    ASSERT_TRUE(u.warte([&] { return a.zustand() == Zustand::Verbunden; }, 3000));

    std::vector<::serial::Rfc2217Ereignis> ev;
    std::string daten;
    auto pumpe = [&] {
        const std::string roh = c.hole();
        k.eingabe(reinterpret_cast<const uint8_t*>(roh.data()), roh.size());
        for (auto& e : k.nimmEreignisse()) ev.push_back(e);
        const auto nd = k.nimmNutzdaten();
        daten.append(nd.begin(), nd.end());
        const auto aus = k.nimmAusgabe();
        if (!aus.empty()) c.schick(aus);
    };
    auto antwort = [&](A art, uint32_t* wert = nullptr, std::string* text = nullptr) {
        const bool ok = u.warte([&] {
            pumpe();
            for (auto& e : ev)
                if (e.art == art && e.antwort) return true;
            return false;
        }, 3000);
        if (!ok) return false;
        for (auto it = ev.begin(); it != ev.end(); ++it)
            if (it->art == art && it->antwort) {
                if (wert) *wert = it->wert;
                if (text) *text = it->text;
                ev.erase(it);
                break;
            }
        return true;
    };
    auto befehl = [&] { ASSERT_TRUE(c.schick(k.nimmAusgabe())); };
    uint32_t w = 0;
    std::string t;

    k.sendeSignatur("");
    befehl();
    ASSERT_TRUE(antwort(A::Signatur, nullptr, &t));
    EXPECT_EQ(t.rfind("k1520emu ", 0), 0u) << t;

    // Formatwünsche der Gegenseite ändern NICHTS — Antwort = Gastwert (Leitsatz 4).
    k.sendeDatenbits(5);
    befehl();
    ASSERT_TRUE(antwort(A::Datenbits, &w));
    EXPECT_EQ(w, 7u);
    k.sendeParitaet(1);   // NONE gewünscht
    befehl();
    ASSERT_TRUE(antwort(A::Paritaet, &w));
    EXPECT_EQ(w, r2::paritaetNetz(2));   // EVEN
    k.sendeStoppbits(0);   // Abfrage
    befehl();
    ASSERT_TRUE(antwort(A::Stoppbits, &w));
    EXPECT_EQ(w, 2u);
    EXPECT_EQ(a.st().daten, 7u);

    // Fluss: Hardware gewünscht → Einstellung des Anwenders (keine) zurück.
    k.sendeSteuerung(r2::FLUSS_HARDWARE);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::FLUSS_KEINE);
    k.sendeSteuerung(r2::EINFLUSS_HARDWARE);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::EINFLUSS_KEINE);

    // Leitungen: sein DTR weg → unser DSR/DCD weg; Abfragen melden den Stand.
    k.sendeSteuerung(r2::DTR_AUS);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::DTR_AUS);
    EXPECT_TRUE(u.warte([&] { return !a.a.dsr && !a.a.dcd && a.a.cts; }, 2000));
    k.sendeSteuerung(r2::DTR_ABFRAGE);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::DTR_AUS);
    k.sendeSteuerung(r2::RTS_ABFRAGE);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::RTS_EIN);   // gilt als aktiv bis zum ersten SET-CONTROL dazu

    // Break der Gegenseite erreicht den Gast; die Abfrage meldet den des Gastes.
    k.sendeSteuerung(r2::BREAK_EIN);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_TRUE(u.warte([&] { return a.a.brkEin; }, 2000));
    k.sendeSteuerung(r2::BREAK_ABFRAGE);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_EQ(w, r2::BREAK_AUS);
    k.sendeSteuerung(r2::BREAK_AUS);
    befehl();
    ASSERT_TRUE(antwort(A::Steuerung, &w));
    EXPECT_TRUE(u.warte([&] { return !a.a.brkEin; }, 2000));

    // Break des Gastes → NOTIFY-LINESTATE, sobald die Maske es zulässt.
    k.sendeLineStateMaske(0x10);
    befehl();
    ASSERT_TRUE(antwort(A::LineStateMaske, &w));
    EXPECT_EQ(w, 0x10u);
    k.sendeModemStateMaske(0xF0);
    befehl();
    ASSERT_TRUE(antwort(A::ModemStateMaske, &w));
    EXPECT_EQ(w, 0xF0u);
    a.a.brkAus = true;
    ASSERT_TRUE(antwort(A::LineState, &w));
    EXPECT_EQ(w & 0x10u, 0x10u);
    a.a.brkAus = false;

    // FLOWCONTROL-SUSPEND: der Server hält seine Sendung an, RESUME gibt sie frei.
    k.sendeFlussHalt(true);
    befehl();
    ASSERT_TRUE(antwort(A::FlussHalt));
    a.a.sende("angehalten");
    u.warte([&] { pumpe(); return false; }, 300);
    EXPECT_EQ(daten, "") << "trotz SUSPEND gesendet";
    EXPECT_EQ(a.a.gastSendet.size(), 0u) << "der Gast wartet nicht — es liegt im Sendepuffer";
    k.sendeFlussHalt(false);
    befehl();
    ASSERT_TRUE(antwort(A::FlussWeiter));
    EXPECT_TRUE(u.warte([&] { pumpe(); return daten == "angehalten"; }, 3000)) << daten;

    // PURGE wird bestätigt.
    k.sendeLeeren(r2::PURGE_BEIDE);
    befehl();
    ASSERT_TRUE(antwort(A::Leeren, &w));
    EXPECT_EQ(w, r2::PURGE_BEIDE);

    // Unvollständiger COM-PORT-Befehl (Baud mit 2 statt 4 Byte) bringt nichts durcheinander.
    ASSERT_TRUE(c.schick(std::string("\xFF\xFA\x2C\x01\x00\x25\xFF\xF0", 8)));
    k.sende(reinterpret_cast<const uint8_t*>("weiter"), 6);
    befehl();
    EXPECT_TRUE(u.warte([&] { return a.a.gelesenText() == "weiter"; }, 3000))
        << a.a.gelesenText();
    EXPECT_EQ(a.zustand(), Zustand::Verbunden);
    EXPECT_EQ(a.st().baud_nenn, 9600u);
}

TEST(SerialHubGegenseite, ClientUeberIpv6NenntDieGegenstelleInKlammern) {
    Seite a, b;
    Uhrwerk u{{&a, &b}};
    {
        // Probe an einem eigenen Lauscher (nicht am Server — der nähme sie als Client).
        net::Lauscher probe = net::lauschen(0);
        net::Fehler f;
        const auto ziele = net::aufloesen("::1", probe.port, &f);
        if (!probe.sock || ziele.empty() || !net::verbindenAlle(ziele, 1000, &f))
            GTEST_SKIP() << "kein IPv6-Loopback";
    }
    const uint16_t port = starteServer(a, Betriebsart::Telnet);
    ASSERT_NE(port, 0);
    SerialKonfig k = client(Betriebsart::Telnet, port);
    k.host = "::1";
    ASSERT_TRUE(b.hub.konfigurieren(0, k));
    ASSERT_TRUE(b.hub.start(0));
    ASSERT_TRUE(u.warte([&] {
        return a.zustand() == Zustand::Verbunden && b.zustand() == Zustand::Verbunden;
    }, 5000));
    EXPECT_EQ(a.st().gegenstelle.rfind("[::1]:", 0), 0u) << a.st().gegenstelle;
    EXPECT_EQ(b.st().gegenstelle, "[::1]:" + std::to_string(port));
}

TEST(SerialClientDauerversuch, UnaufloesbarerNameBleibtImVersuchUndNenntDenGrund) {
    Seite b;
    Uhrwerk u{{&b}};
    // .invalid ist reserviert (RFC 2606) und wird nie aufgelöst.
    SerialKonfig k = client(Betriebsart::Telnet, 5000);
    k.host = "k1520-test.invalid";
    ASSERT_TRUE(b.hub.konfigurieren(0, k));
    ASSERT_TRUE(b.hub.start(0));
    ASSERT_TRUE(u.warte([&] { return !b.st().meldung.empty(); }, 8000));
    const SerialStatus s = b.st();
    EXPECT_EQ(s.zustand, Zustand::Verbindet) << "ein Namensfehler ist kein FEHLER (DHCP, §7.1)";
    EXPECT_NE(s.meldung.find("nicht auflösbar"), std::string::npos) << s.meldung;
    b.hub.stop(0);
    EXPECT_EQ(b.zustand(), Zustand::Aus);
}

TEST(SerialDatei, SchreibfehlerBeimBeendenBleibtSichtbar) {
#if defined(_WIN32)
    GTEST_SKIP() << "/dev/full gibt es nur unter POSIX";
#else
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "kein /dev/full";
    Seite s;
    Uhrwerk u{{&s}};
    SerialKonfig k;
    k.betriebsart = Betriebsart::Datei;
    k.datei       = "/dev/full";
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    ASSERT_TRUE(s.hub.startAuto(0));
    s.a.sende("x");
    // Sofort beenden, bevor die 0,5 s um sind: der Rest geht beim Schließen hinaus —
    // und dessen Fehler darf nicht verschluckt werden.
    ASSERT_TRUE(u.warte([&] { return s.a.gastSendet.empty(); }, 2000));
    s.hub.stop(0);
    EXPECT_EQ(s.zustand(), Zustand::Fehler);
    EXPECT_NE(s.st().meldung.find("Schreibfehler"), std::string::npos) << s.st().meldung;
    EXPECT_EQ(s.st().port_aktiv, 0u);
    // Ein neuer Start ist möglich (FEHLER ist kein aktiver Zustand).
    k.datei = k1520test::tempPath("k1520_test_serial_nach_fehler.txt");
    ASSERT_TRUE(s.hub.konfigurieren(0, k));
    EXPECT_TRUE(s.hub.start(0));
    s.hub.stop(0);
    std::filesystem::remove(std::filesystem::u8path(k.datei));
#endif
}

/// Faden-Modell (hub.h): Bedienung aus JEDEM Faden.  Ein dritter Faden fragt Status,
/// Einstellung und Info ab, während der Testfaden Verbindungen auf- und abbaut und der
/// Emulationsfaden taktet.  Unter `-fsanitize=thread` gefahren (AP-T1b), sonst ein
/// schlichter Rauchtest gegen Verklemmen.
TEST(SerialHubNebenlaeufig, StatusAusDrittemFadenWaehrendAufUndAbbau) {
    Seite a, b;
    std::atomic<bool> halt{false};
    std::atomic<uint64_t> abfragen{0};
    std::thread emu([&] {
        uint64_t z = 0;
        while (!halt.load()) {
            for (int i = 0; i < 100; ++i) {
                z += 64;
                a.hub.takt(z);
                b.hub.takt(z);
            }
            std::this_thread::sleep_for(milliseconds(1));
        }
    });
    std::thread dritter([&] {
        while (!halt.load()) {
            for (Seite* s : {&a, &b}) {
                const SerialStatus st = s->hub.status(0);
                (void)s->hub.konfig(0);
                (void)s->hub.info(0);
                (void)s->hub.wandler(0).sicht();
                if (st.zustand == Zustand::Fehler) ADD_FAILURE() << st.meldung;
            }
            ++abfragen;
        }
    });
    auto warte = [&](const std::function<bool()>& f, int ms) {
        const auto ende = Uhr::now() + milliseconds(ms);
        while (Uhr::now() < ende) {
            if (f()) return true;
            std::this_thread::sleep_for(milliseconds(2));
        }
        return f();
    };
    int verbunden = 0;
    for (int runde = 0; runde < 4; ++runde) {
        const Betriebsart art = runde % 2 ? Betriebsart::Rfc2217 : Betriebsart::Telnet;
        ASSERT_TRUE(a.hub.konfigurieren(0, server(art)));
        ASSERT_TRUE(a.hub.start(0));
        ASSERT_TRUE(b.hub.konfigurieren(0, client(art, a.st().port_aktiv)));
        ASSERT_TRUE(b.hub.start(0));
        if (warte([&] {
                return a.zustand() == Zustand::Verbunden && b.zustand() == Zustand::Verbunden;
            }, 5000))
            ++verbunden;
        // Abwechselnd die eine oder die andere Seite trennen lassen.
        if (runde % 2) b.hub.stop(0); else a.hub.stop(0);
        a.hub.stopAlle();
        b.hub.stopAlle();
    }
    halt = true;
    emu.join();
    dritter.join();
    EXPECT_EQ(verbunden, 4);
    EXPECT_GT(abfragen.load(), 10u);
}
