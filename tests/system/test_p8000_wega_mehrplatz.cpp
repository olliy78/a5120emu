/**
 * @file test_p8000_wega_mehrplatz.cpp
 * @brief P8000 Mehrplatz-Abnahme (AP P22, doc/design/25_p8000.md §11): WEGA 3.0 von der Platte
 *        mit der Administrator-Konsole am ORIGINALTERMINAL (Variante „P8000 + P8000 Terminal",
 *        tty1: K7673 → Z8-Firmware P8T 5.0 → SIO0-B) und drei reinen Arbeitsplätzen (tty0, tty2, tty4)
 *        („P8000 Terminal" = `P8000TerminalMachine`, Typ 2 + K7673) als Telnet-Clients am
 *        `SerialHub` des Rechners (Loopback, freie Ports).
 *
 * Ausgangspunkt ist die installierte Platte des Zwischenstands `p15_7_sync`
 * (`test_p8000_wega_install.cpp`, `tests/support/p8000_wega.h`) — KEIN Save-State: Kaltstart wie
 * am Gerät (Netz ein → MON8 → UDOS-Koppelsoftware → MON16 → NMI → AUTOBOOT → `boot` →
 * `md(0,16000)wega` → Mehrbenutzerbetrieb).  Fehlt der Zwischenstand, wird übersprungen.
 *
 * Alles wird getippt (Matrixdruck an der K7673, 80 ms halten + 80 ms Pause je Zeichen) und aus dem
 * Bildspeicher des jeweiligen Terminals gelesen — nichts über Abkürzungen am Gast vorbei.
 * Gleichschritt: 5 000 Rechnertakte (1,25 ms) ≙ 4 608 Z8-Takte je Arbeitsplatz.
 *
 * Ablauf und gesehene Bildschirmtexte: doc/p8000/mehrplatz_abnahme.md.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "core/machines/p8000/p8000_terminal_machine.h"
#include "core/serial/hub.h"
#include "tests/support/p8000_input.h"
#include "tests/support/p8000_wega.h"

using namespace k1520test::p8000;
using namespace k1520::serial;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

/// Arbeitsplatz-Kanäle (Vorgabe tty0/tty2 = SIO0-A/SIO1-A der 8-Bit-Seite über die Koppelsoftware,
/// tty4 = SIO0-A der 16-Bit-Seite direkt im WEGA-Kern;
/// `K1520_P22_TTYS=tty4,tty5` o. ä. zum Nachstellen).
std::vector<std::string> arbeitsplatzTtys() {
    std::string s = "tty0,tty2,tty4";
    if (const char* e = std::getenv("K1520_P22_TTYS"); e && *e) s = e;
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= s.size()) {
        size_t k = s.find(',', p);
        if (k == std::string::npos) k = s.size();
        if (k > p) v.push_back(s.substr(p, k - p));
        p = k + 1;
    }
    return v;
}

std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

/// Ein Terminal der Anlage: die Konsole (Originalterminal am Rechner) oder ein Arbeitsplatz.
struct Platz {
    std::string name;                                  ///< "console", "tty0", …
    P8000Machine* rechner = nullptr;                   ///< nur Konsole
    std::unique_ptr<P8000TerminalMachine> t;           ///< nur Arbeitsplatz

    std::string zeile(int z) const {
        return rtrim(rechner ? rechner->konsole().text(z) : t->einheit().hw().text(z));
    }
    int cursor() const { return rechner ? rechner->konsole().zeile() : t->einheit().hw().cursorZeile(); }
    std::string cursorZeile() const { return zeile(cursor()); }
    std::string bild() const {
        std::string s;
        for (int z = 0; z < 24; ++z) s += zeile(z) + "\n";
        return s;
    }
    void taste(uint32_t code) {
        if (rechner) rechner->keyPress(code, false, false);
        else t->keyPress(code, false, false);
    }
    bool tastenFertig() const {
        return rechner ? rechner->originalTerminal()->tastenFertig() : t->einheit().tastenFertig();
    }
};

struct Anlage {
    WegaLauf l;
    std::deque<Platz> plaetze;   ///< [0] = Konsole (deque: Verweise bleiben gültig)

    P8000Machine& m() { return *l.m; }
    Platz& konsole() { return plaetze[0]; }

    void schritt() {
        l.m->run(int(kBatch));
        for (size_t i = 1; i < plaetze.size(); ++i) plaetze[i].t->run(4608);
    }
    void laufe(long long takte) {
        for (long long t = 0; t < takte; t += kBatch) schritt();
    }
    /// Wartet, bis @p p @p nadel im Bild zeigt.
    bool bisText(Platz& p, const std::string& nadel, long long grenze) {
        for (long long t = 0, seit = 0; t < grenze; t += kBatch, seit += kBatch) {
            schritt();
            if (seit >= 100'000) {
                seit = 0;
                if (p.bild().find(nadel) != std::string::npos) return true;
            }
            melde(t);
        }
        return p.bild().find(nadel) != std::string::npos;
    }
    /// Wartet, bis die Cursorzeile von @p p auf eine der @p fragen endet; Index oder −1.
    int frage(Platz& p, const std::vector<std::string>& fragen, long long grenze) {
        auto passt = [&]() -> int {
            const std::string z = p.cursorZeile();
            for (size_t i = 0; i < fragen.size(); ++i) {
                const std::string f = rtrim(fragen[i]);
                if (endetMit(z, f)) return int(i);
            }
            return -1;
        };
        for (long long t = 0, seit = 0; t < grenze; t += kBatch, seit += kBatch) {
            schritt();
            if (seit >= 100'000) {
                seit = 0;
                if (int i = passt(); i >= 0) return i;
            }
            melde(t);
        }
        return passt();
    }
    void melde(long long t) {
        if (t > 0 && t % 400'000'000 == 0) {
            std::string z = konsole().cursorZeile();
            std::fprintf(stderr, "  [%6.0f Mio. Takte] %s\n", double(m().totalCycles()) / 1e6, z.c_str());
        }
    }
    /// Tippt @p text an @p p ('\r' = RETURN) und läuft, bis die Tastatur alles abgegeben hat.
    void tippe(Platz& p, const std::string& text) {
        for (char c : text) p.taste((c == '\r' || c == '\n') ? 0x01000004u : uint8_t(c));
        for (long long t = 0; t < 400'000'000 && !p.tastenFertig(); t += kBatch) schritt();
    }
    void tippeZeile(Platz& p, const std::string& text) { tippe(p, text + "\r"); }

    /// Shell-Prompt am Zeilenende: `#n` (Superuser, csh), `%n` (csh), `$`/`#` (sh)?
    static bool istPrompt(const std::string& z) {
        static const std::regex re(R"((^|\s)([#%][0-9]+|[$#%])$)");
        return std::regex_search(z, re);
    }
    /// Kommando an @p p tippen (nicht warten).
    void gib(Platz& p, const std::string& cmd) {
        for (char c : cmd + "\r") p.taste(c == '\r' ? 0x01000004u : uint8_t(c));
    }
    /// Läuft, bis jede Station in @p ps wieder am Prompt steht (Cursorzeile = Prompt, Tasten fertig).
    bool bisPrompts(const std::vector<Platz*>& ps, long long grenze) {
        auto fertig = [&] {
            for (Platz* p : ps)
                if (!p->tastenFertig() || !istPrompt(p->cursorZeile())) return false;
            return true;
        };
        laufe(400'000);
        for (long long t = 0, seit = 0; t < grenze; t += kBatch, seit += kBatch) {
            schritt();
            if (seit >= 100'000) {
                seit = 0;
                if (fertig()) return true;
            }
            melde(t);
        }
        return fertig();
    }
    /// Kommando an @p p und auf den nächsten Prompt warten.
    ::testing::AssertionResult kommando(Platz& p, const std::string& cmd, long long grenze = 800'000'000) {
        if (!istPrompt(p.cursorZeile()))
            return ::testing::AssertionFailure() << p.name << ": kein Prompt\n" << p.bild();
        gib(p, cmd);
        if (!bisPrompts({&p}, grenze))
            return ::testing::AssertionFailure() << p.name << ": '" << cmd << "' ohne Prompt\n" << p.bild();
        return ::testing::AssertionSuccess();
    }
    /// Anmelden an @p p (getty-Meldung muss nicht stehen: RETURN holt sie).
    ::testing::AssertionResult anmelden(Platz& p, const std::string& name, const std::string& pw) {
        if (frage(p, {"login:"}, 40'000'000) < 0) {
            tippe(p, "\r");
            if (frage(p, {"login:"}, 400'000'000) < 0)
                return ::testing::AssertionFailure() << p.name << ": kein login:\n" << p.bild();
        }
        laufe(200'000);
        tippeZeile(p, name);
        if (!pw.empty()) {
            if (frage(p, {"Password:"}, 400'000'000) < 0)
                return ::testing::AssertionFailure() << p.name << ": kein Password:\n" << p.bild();
            laufe(200'000);
            tippeZeile(p, pw);
        }
        for (long long t = 0; t < 2'000'000'000LL; t += 1'000'000) {
            laufe(1'000'000);
            if (p.tastenFertig() && istPrompt(p.cursorZeile())) return ::testing::AssertionSuccess();
        }
        return ::testing::AssertionFailure() << p.name << ": kein Prompt nach der Anmeldung\n" << p.bild();
    }
};

/// Kanal @p name des Rechner-Hubs als Telnet-Server auf einem freien Loopback-Port.
uint16_t ttyAlsServer(P8000Machine& m, const std::string& name) {
    SerialHub* hub = m.serialHub();
    for (int i = 0; i < hub->anzahl(); ++i) {
        if (hub->info(i).name != name) continue;
        SerialKonfig k = hub->konfig(i);
        k.betriebsart = Betriebsart::Telnet;
        k.rolle = Rolle::Server;
        k.host = "127.0.0.1";
        k.port = 0;
        k.loop = false;
        if (!hub->konfigurieren(i, k) || !hub->start(i)) return 0;
        return hub->status(i).port_aktiv;
    }
    return 0;
}

/// Netz ein bis zur Anmeldung an der Konsole (wie `KaltstartVonDerPlatteBisZurAnmeldung`).
void kaltstartBisLogin(Anlage& a) {
    P8000Machine& m = a.m();
    Platz& k = a.konsole();
    m.powerOn();
    ASSERT_TRUE(a.bisText(k, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << k.bild();
    a.tippe(k, "\r");
    ASSERT_GE(a.frage(k, {">"}, 20'000'000), 0) << k.bild();
    a.tippe(k, "\r");
    ASSERT_TRUE(a.bisText(k, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << k.bild();
    a.laufe(2'000'000);
    m.nmi();
    ASSERT_TRUE(a.bisText(k, "MAXSEG=<0F>", 400'000'000)) << k.bild();
    // boot0.md aus Block 0 startet WEGA SELBST: es zeigt „> boot", „Boot", „: md(0,16000)wega"
    // ohne Eingabe (automatischer Start, von /etc/new.install eingerichtet).  Wer am `>` tippt,
    // trifft nur ein Fenster von Millisekunden — hier wird nichts getippt (Befund P22).
    ASSERT_TRUE(a.bisText(k, "WEGA Kernel -- Release 3.2", 2'000'000'000LL)) << k.bild();
    for (;;) {
        const int i = a.frage(k, {"Enter Date (MM/DD/YY or <cr>):", "Enter Time (HH:MM):", "login:", "#1"},
                              8'000'000'000LL);
        ASSERT_GE(i, 0) << k.bild();
        if (i == 2) break;
        a.laufe(200'000);
        if (i == 3) { a.tippeZeile(k, "init 2"); continue; }
        a.tippeZeile(k, i == 0 ? "" : "21:10");
    }
}
}  // namespace

TEST(P8000WegaMehrplatz, KonsoleAmOriginalterminalUndArbeitsplaetzeUeberTelnet) {
    stumm();
    if (!stufeDa("p15_7_sync")) GTEST_SKIP() << "kein Zwischenstand p15_7_sync (tools/dev.sh test-wega)";
    const auto ttys = arbeitsplatzTtys();
    Anlage a;
    ASSERT_TRUE(kopiere(stufenPfad("p15_7_sync", "platte.img"), a.l.platte));
    ASSERT_TRUE(kopiere(stufenPfad("p15_7_sync", "start.hfe"), a.l.start));
    P8000Machine::Config c = wegaConfig(a.l.platte, false);
    c.terminal = P8000Machine::Config::TerminalArt::Original;
    a.l.m = std::make_unique<P8000Machine>(c);
    P8000Machine& m = a.m();
    ASSERT_TRUE(m.mountDisk(0, a.l.start, m.defaultFormatName(0), false)) << m.lastError();
    a.plaetze.push_back(Platz{"console", &m, nullptr});
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_NO_FATAL_FAILURE(kaltstartBisLogin(a));
    std::fprintf(stderr, "  [login: an der Konsole nach %.1f s Maschinenzeit]\n", double(m.totalCycles()) / 4e6);

    Platz& k = a.konsole();
    ASSERT_TRUE(a.anmelden(k, "wega", "root"));
    std::fprintf(stderr, "  [Konsole angemeldet nach %.1f s Maschinenzeit]\n", double(m.totalCycles()) / 4e6);

    // Arbeitsplätze: Kanäle als Telnet-Server (freie Loopback-Ports), je ein „P8000 Terminal" als
    // Client.  Eingeschaltet wird das Terminal ERST mit stehender Verbindung — so trifft sein
    // Einschalt-00H (Merkposten 30) den laufenden getty, wie am Gerät.
    for (const std::string& tty : ttys) {
        const uint16_t port = ttyAlsServer(m, tty);
        ASSERT_NE(port, 0) << tty;
        P8000TerminalMachine::Config tc;
        std::string fehler;
        const std::string text = "art=telnet,rolle=client,host=127.0.0.1,port=" + std::to_string(port) + ",verbinden=1";
        ASSERT_TRUE(P8000TerminalMachine::konfigAusText(text.c_str(), tc, fehler)) << fehler;
        Platz p;
        p.name = tty;
        p.t = std::make_unique<P8000TerminalMachine>(tc);
        for (int i = 0; i < 300 && p.t->serialHub()->status(0).zustand != Zustand::Verbunden; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_EQ(p.t->serialHub()->status(0).zustand, Zustand::Verbunden) << tty;
        p.t->powerOn();
        a.plaetze.push_back(std::move(p));
    }
    std::vector<Platz*> alle;
    for (Platz& p : a.plaetze) alle.push_back(&p);
    for (size_t i = 1; i < a.plaetze.size(); ++i) {
        ASSERT_TRUE(a.bisText(a.plaetze[i], "9600 baud", 40'000'000)) << a.plaetze[i].bild();
    }
    a.laufe(2'000'000);   // Tasten nimmt das Terminal ≈ 150 ms nach der Meldung an (Merkposten 35)
    for (size_t i = 1; i < a.plaetze.size(); ++i) {
        ASSERT_TRUE(a.anmelden(a.plaetze[i], "wega", "root"));
        std::fprintf(stderr, "  [%s angemeldet nach %.1f s Maschinenzeit]\n", a.plaetze[i].name.c_str(),
                     double(m.totalCycles()) / 4e6);
    }

    // `who` an der Konsole zeigt alle drei Anmeldungen.
    ASSERT_TRUE(a.kommando(k, "who"));
    {
        const std::string b = k.bild();
        EXPECT_NE(b.find("wega     console"), std::string::npos) << b;
        for (const std::string& tty : ttys) EXPECT_NE(b.find("wega     " + tty), std::string::npos) << b;
    }

    // Parallele Last: an allen Stationen gleichzeitig ein Kommando.
    for (Platz* p : alle) a.gib(*p, "ls -l /bin");
    ASSERT_TRUE(a.bisPrompts(alle, 4'000'000'000LL));
    for (Platz* p : alle) {
        // Letzte Zeile vollständig: kein Zeichen verloren (XON/XOFF der Firmware bei 36 Zeichen
        // Puffer gegen den Dauerstrom von drei gleichzeitigen Ausgaben).
        EXPECT_NE(p->bild().find("-rwxr-x--x 1 bin      system     7346 May 30 08:00 write\n"), std::string::npos)
            << p->name << "\n" << p->bild();
    }
    for (Platz* p : alle) a.gib(*p, "date");
    ASSERT_TRUE(a.bisPrompts(alle, 800'000'000));
    for (Platz* p : alle) EXPECT_NE(p->bild().find("MES"), std::string::npos) << p->name << "\n" << p->bild();
    for (Platz* p : alle) a.gib(*p, "who am i");
    ASSERT_TRUE(a.bisPrompts(alle, 800'000'000));
    for (Platz* p : alle) {
        const std::string soll = p == &k ? "console" : p->name;
        EXPECT_NE(p->bild().find(soll), std::string::npos) << p->name << "\n" << p->bild();
    }

    // Nachricht von einem Arbeitsplatz an den anderen (write).
    Platz& p1 = a.plaetze[1];
    Platz& p2 = a.plaetze[a.plaetze.size() - 1];
    ASSERT_TRUE(a.kommando(p1, "echo Gruss von " + p1.name + " | write wega " + p2.name));
    ASSERT_TRUE(a.bisText(p2, "Gruss von " + p1.name, 400'000'000)) << p2.bild() << "\n--- " << p1.name << "\n" << p1.bild();

    // Abmelden und wieder anmelden (getty startet neu, Flag „c" in /etc/inittab).
    a.gib(p1, "exit");
    ASSERT_GE(a.frage(p1, {"login:"}, 800'000'000), 0) << p1.bild();
    ASSERT_TRUE(a.anmelden(p1, "wega", "root"));
    ASSERT_TRUE(a.kommando(k, "who"));
    EXPECT_NE(k.bild().find("wega     " + p1.name), std::string::npos) << k.bild();

    // Saubere Abschaltung: Puffer zurückschreiben.
    ASSERT_TRUE(a.kommando(k, "sync;sync"));
    a.laufe(400'000'000);
    m.hdFlush();
    for (Platz* p : alle) std::fprintf(stderr, "--- %s\n%s", p->name.c_str(), p->bild().c_str());
    std::fprintf(stderr, "  [Abnahme fertig: %.1f s Maschinenzeit, %.0f s Rechenzeit]\n", double(m.totalCycles()) / 4e6,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}
