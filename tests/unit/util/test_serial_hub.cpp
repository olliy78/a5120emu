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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/serial/hub.h"
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
