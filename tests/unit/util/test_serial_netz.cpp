/**
 * @file test_serial_netz.cpp
 * @brief Unit-Tests der Socket-Hülle (`core/serial/net/socket.h`, Entwurf 19 §7, §11).
 *
 * Nur Loopback, keine Namensauflösung nach außen.  **Nie feste Ports**: die Tests
 * laufen mit `ctest -j` parallel — Port 0 bzw. die Suche ab einem zufälligen hohen
 * Port, der tatsächliche Port kommt aus dem Ergebnis.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/serial/net/socket.h"

using namespace k1520::serial::net;

namespace {

/// Zufälliger hoher Port für die Suchtests (Bereich unterhalb der Ephemeral-Ports).
int zufallsPort() {
    static std::mt19937 g{std::random_device{}()};
    return 20000 + static_cast<int>(g() % 10000);
}

/// Wartet bis zu `ms`, bis `fd` lesbar ist.
bool lesbarBinnen(SockFd fd, int ms) {
    std::vector<PollEintrag> pe(1);
    pe[0].fd    = fd;
    pe[0].lesen = true;
    return warten(pe, ms) > 0 && pe[0].lesbar;
}

/// Verbindet `host:port` und nimmt am Lauscher an; beide Seiten verbunden oder Fehlschlag.
struct Paar {
    Socket client, server;
};

Paar verbindeUndNimmAn(const Lauscher& l, const std::string& host) {
    Paar p;
    Fehler f;
    auto ziele = aufloesen(host, l.port, &f);
    EXPECT_FALSE(ziele.empty()) << f.text;
    if (ziele.empty()) return p;
    p.client = verbindenAlle(ziele, 2000, &f);
    EXPECT_TRUE(p.client.gueltig()) << f.text;
    if (!p.client) return p;
    // Die Verbindung steht, sobald connect zurück ist; accept kann trotzdem einen
    // Augenblick später sehen — kurz warten statt raten.
    for (int i = 0; i < 50 && !p.server; ++i) {
        p.server = annehmen(l.sock.fd());
        if (!p.server) lesbarBinnen(l.sock.fd(), 20);
    }
    EXPECT_TRUE(p.server.gueltig());
    return p;
}

/// Kleiner Echo-Austausch client → server → client.
void pruefeAustausch(Paar& p) {
    ASSERT_TRUE(p.client && p.server);
    const char hallo[] = "hallo\xFF\x00z";
    IoErgebnis s = senden(p.client.fd(), hallo, sizeof hallo);
    ASSERT_EQ(s.status, IoStatus::Ok);
    ASSERT_EQ(s.n, sizeof hallo);

    ASSERT_TRUE(lesbarBinnen(p.server.fd(), 2000));
    char buf[32];
    IoErgebnis r = empfangen(p.server.fd(), buf, sizeof buf);
    ASSERT_EQ(r.status, IoStatus::Ok);
    ASSERT_EQ(r.n, sizeof hallo);
    EXPECT_EQ(std::string(buf, r.n), std::string(hallo, sizeof hallo));

    ASSERT_EQ(senden(p.server.fd(), "x", 1).status, IoStatus::Ok);
    ASSERT_TRUE(lesbarBinnen(p.client.fd(), 2000));
    r = empfangen(p.client.fd(), buf, sizeof buf);
    EXPECT_EQ(r.status, IoStatus::Ok);
    EXPECT_EQ(r.n, 1u);
}

} // namespace

TEST(SerialNetz, NetzStartenIstMehrfachAufrufbar) {
    EXPECT_TRUE(netzStarten());
    EXPECT_TRUE(netzStarten());
}

TEST(SerialNetz, LauschenMitPortNullLiefertTatsaechlichenPort) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock.gueltig()) << l.fehler.text;
    EXPECT_GT(l.port, 0);
}

TEST(SerialNetz, LoopbackIPv4) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock) << l.fehler.text;
    Paar p = verbindeUndNimmAn(l, "127.0.0.1");
    pruefeAustausch(p);
}

TEST(SerialNetz, LoopbackIPv6) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock) << l.fehler.text;
    if (!l.dualStack) GTEST_SKIP() << "kein IPv6 auf diesem Rechner (Rückfall 0.0.0.0)";

    Fehler f;
    auto ziele = aufloesen("[::1]", l.port, &f);
    ASSERT_FALSE(ziele.empty()) << f.text;
    EXPECT_TRUE(ziele[0].ist6());
    Socket c = verbindenAlle(ziele, 2000, &f);
    if (!c) GTEST_SKIP() << "::1 nicht erreichbar: " << f.text;   // IPv6 im Kernel, aber ohne Loopback

    Socket s;
    for (int i = 0; i < 50 && !s; ++i) {
        s = annehmen(l.sock.fd());
        if (!s) lesbarBinnen(l.sock.fd(), 20);
    }
    Paar p{std::move(c), std::move(s)};
    pruefeAustausch(p);
}

TEST(SerialNetz, DualStackNimmtAuchIPv4AufDemSelbenSocketAn) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock) << l.fehler.text;
    if (!l.dualStack) GTEST_SKIP() << "kein IPv6";
    // Der Dual-Stack-Socket (V6ONLY=0) muss auch v4-Clients bedienen, sonst hörte
    // eine Gegenstelle mit „127.0.0.1" ins Leere.
    Paar p = verbindeUndNimmAn(l, "127.0.0.1");
    pruefeAustausch(p);
}

TEST(SerialNetz, AufloesenLiefertLiteraleOhneNetz) {
    Fehler f;
    auto v4 = aufloesen("127.0.0.1", 1234, &f);
    ASSERT_EQ(v4.size(), 1u) << f.text;
    EXPECT_FALSE(v4[0].ist6());
    EXPECT_EQ(v4[0].text, "127.0.0.1");

    auto v6 = aufloesen("::1", 1234, &f);
    if (v6.empty()) GTEST_SKIP() << "Resolver kennt kein IPv6: " << f.text;
    EXPECT_TRUE(v6[0].ist6());
    EXPECT_EQ(v6[0].text, "::1");
}

TEST(SerialNetz, AufloesenLehntUngueltigenHostAb) {
    Fehler f;
    EXPECT_TRUE(aufloesen("ho st", 1, &f).empty());
    EXPECT_NE(f.code, 0);
}

TEST(SerialNetz, BelegterPortSchiebtDieSucheUmEinsWeiter) {
    // Erst einen Port belegen (Port 0 → tatsächlicher Port P), dann ab P suchen:
    // die Suche darf P nicht bekommen und muss bei P+1 (oder dem nächsten freien) landen.
    Lauscher a = lauschen(0);
    ASSERT_TRUE(a.sock) << a.fehler.text;

    Lauscher b = lauschenMitSuche(a.port);
    ASSERT_TRUE(b.sock) << b.fehler.text;
    EXPECT_GT(b.port, a.port);

    // Direkter Versuch auf den belegten Port meldet „belegt", nicht irgendeinen Fehler.
    Lauscher c = lauschen(a.port);
    EXPECT_FALSE(c.sock);
    EXPECT_TRUE(c.fehler.belegt) << c.fehler.code << " " << c.fehler.text;
}

TEST(SerialNetz, BelegtBleibtBelegtAuchWennNurIPv4DenPortHaelt) {
    // Ein reiner IPv4-Belegter darf vom Dual-Stack-Bind nicht als „frei" durchrutschen.
    Lauscher a = lauschen(0);
    ASSERT_TRUE(a.sock);
    const int p = a.port;
    Lauscher b = lauschen(p);
    EXPECT_FALSE(b.sock);
    EXPECT_TRUE(b.fehler.belegt);
}

TEST(SerialNetz, PortpruefungFrei) {
    // Zufälliger hoher Port; ist er zufällig belegt, liefert die Prüfung einen Vorschlag —
    // der Test akzeptiert das, prüft aber dann die Gegenprobe.
    const int p = zufallsPort();
    PortPruefung r = portPruefen(p);
    ASSERT_GT(r.vorschlag, 0) << r.fehler.text;
    EXPECT_GE(r.vorschlag, p);
    EXPECT_EQ(r.frei, r.vorschlag == p);
    // Die Prüfung hält nichts fest: der Vorschlag ist danach wirklich bindbar.
    EXPECT_TRUE(lauschen(r.vorschlag).sock);
}

TEST(SerialNetz, PortpruefungBelegtLiefertNaechstenFreien) {
    Lauscher a = lauschen(0);
    ASSERT_TRUE(a.sock) << a.fehler.text;

    PortPruefung r = portPruefen(a.port);
    EXPECT_FALSE(r.frei);
    EXPECT_GT(r.vorschlag, a.port);
    // Nur Prüfen, nicht Lauschen: der Vorschlag ist danach noch bindbar (§7.4a).
    Lauscher b = lauschen(r.vorschlag);
    EXPECT_TRUE(b.sock) << b.fehler.text;
}

TEST(SerialNetz, PortpruefungOhneFreienPortLiefertNull) {
    // Der höchste Port 65535 belegt, Prüfung ab 65535: nichts darüber → 0.
    Lauscher a = lauschen(65535);
    if (!a.sock) GTEST_SKIP() << "Port 65535 nicht zu belegen (von anderem Lauf gehalten): " << a.fehler.text;
    PortPruefung r = portPruefen(65535);
    EXPECT_FALSE(r.frei);
    EXPECT_EQ(r.vorschlag, 0);
    EXPECT_TRUE(r.fehler.code == 0);   // „alles belegt" ist kein Systemfehler
}

TEST(SerialNetz, UngueltigePortwerte) {
    EXPECT_FALSE(lauschen(-1).sock);
    EXPECT_FALSE(lauschen(65536).sock);
    EXPECT_FALSE(lauschenMitSuche(0).sock);
    EXPECT_EQ(portPruefen(0).vorschlag, 0);
}

TEST(SerialNetz, VerbindenAnGeschlossenenPortScheitertMitFehler) {
    // Port beschaffen, Lauscher schließen → dort hört niemand mehr (Loopback: sofort RST).
    int port;
    { Lauscher l = lauschen(0); ASSERT_TRUE(l.sock); port = l.port; }

    Fehler f;
    auto ziele = aufloesen("127.0.0.1", port, &f);
    ASSERT_FALSE(ziele.empty());
    const auto t0 = std::chrono::steady_clock::now();
    Socket c = verbindenAlle(ziele, 1000, &f);
    const auto dauer = std::chrono::steady_clock::now() - t0;
    EXPECT_FALSE(c.gueltig());
    EXPECT_NE(f.code, 0);
    EXPECT_LT(dauer, std::chrono::milliseconds(1500));   // Frist wird eingehalten
}

TEST(SerialNetz, EmpfangenMeldetWartenUndGeschlossen) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock);
    Paar p = verbindeUndNimmAn(l, "127.0.0.1");
    ASSERT_TRUE(p.client && p.server);

    char b[8];
    EXPECT_EQ(empfangen(p.server.fd(), b, sizeof b).status, IoStatus::Warten);   // nichts da, nicht blockierend

    p.client.schliessen();
    ASSERT_TRUE(lesbarBinnen(p.server.fd(), 2000));
    EXPECT_EQ(empfangen(p.server.fd(), b, sizeof b).status, IoStatus::Geschlossen);
}

TEST(SerialNetz, AnnehmenOhneAnfrageLiefertNichts) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock);
    Fehler f;
    EXPECT_FALSE(annehmen(l.sock.fd(), &f).gueltig());
    EXPECT_EQ(f.code, 0);   // „nichts da" ist kein Fehler
}

TEST(SerialNetz, WarteZeitgrenzeWirdEingehalten) {
    Lauscher l = lauschen(0);
    ASSERT_TRUE(l.sock);
    std::vector<PollEintrag> pe(1);
    pe[0].fd    = l.sock.fd();
    pe[0].lesen = true;
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(warten(pe, 50), 0);
    EXPECT_GE(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(40));
    EXPECT_FALSE(pe[0].lesbar);
}

TEST(SerialNetz, WeckerWecktDenWartendenFaden) {
    Wecker w;
    ASSERT_TRUE(w.gueltig());

    std::vector<PollEintrag> pe(1);
    pe[0].fd    = w.lesefd();
    pe[0].lesen = true;
    EXPECT_EQ(warten(pe, 0), 0);   // ruhig, solange niemand weckt

    // Aus einem anderen Faden wecken, während der Hauptfaden (lang) wartet.
    std::thread t([&w] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        w.wecken();
    });
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(warten(pe, 5000), 1);
    EXPECT_TRUE(pe[0].lesbar);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(2000));
    t.join();

    // Mehrfaches Wecken läuft nicht über, und leeren stellt die Ruhe wieder her.
    for (int i = 0; i < 3000; ++i) w.wecken();
    w.leeren();
    EXPECT_EQ(warten(pe, 0), 0);
}

TEST(SerialNetz, PollUeberspringtUngueltigeEintraege) {
    Wecker w;
    ASSERT_TRUE(w.gueltig());
    std::vector<PollEintrag> pe(2);
    pe[1].fd    = w.lesefd();
    pe[1].lesen = true;   // pe[0] bleibt ohne Socket
    w.wecken();
    EXPECT_EQ(warten(pe, 1000), 1);
    EXPECT_FALSE(pe[0].lesbar);
    EXPECT_TRUE(pe[1].lesbar);
}
