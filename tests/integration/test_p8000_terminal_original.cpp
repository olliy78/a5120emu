/**
 * @file test_p8000_terminal_original.cpp
 * @brief Variante „P8000 + P8000 Terminal" (AP P20c, Entwurf 25 §11, Entwurf 28): tty1 fest am
 *        Originalterminal Typ 2 + K7673.09 — Firmware P8T 5.0 auf dem Z8, Tasten als Matrixdruck.
 *
 * (a) Hardwaretest-Banner im Bildspeicher des Originals, `O U`-Dialog bis `:` wie M2
 *     (`test_p8000_monitor16`), getippt über K7673 → Z8-Firmware → tty1;
 * (c) Save-State-Rundreise der Variante am Prompt (bitgleich, beide laufen gleich weiter);
 * dazu: Matrixtasten der Oberfläche, Pixelbild, Ablehnung eines Kern-Stands.
 * Die M1/M2-Wächter (Kern-Terminal, Vorgabe) bleiben unverändert.
 */

#include <gtest/gtest.h>

#include <string>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";

P8000Machine::Config original(bool k16 = false) {
    P8000Machine::Config c;
    c.karte16 = k16;
    c.terminal = P8000Machine::Config::TerminalArt::Original;
    return c;
}

/// Getippt wird mit Haltezeit (80 ms + 80 ms Pause je Taste ≈ 640 000 Takte) — warten, bis die
/// Tastatur alles abgegeben hat.
void tastenFertig(P8000Machine& m, long long grenze = 400'000'000) {
    for (long long t = 0; t < grenze && !m.originalTerminal()->tastenFertig(); t += kBatch) m.run(int(kBatch));
}

void bisMonitor8(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    EXPECT_NE(bild(m).find("P8000 Hardwaretest U880 - Version 3.1"), std::string::npos) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 20'000'000)) << bild(m);
}
}  // namespace

/// (a) Bild kommt aus dem BWS des Terminalrechners (nicht aus dem Kern-Terminal), RETURN geht als
/// Matrixdruck durch die K7673 und die Firmware an tty1.
TEST(P8000TerminalOriginal, HardwaretestBannerImOriginalbildUndReturnUeberDieTastatur) {
    stumm();
    P8000Machine m(original());
    ASSERT_TRUE(m.hatOriginalTerminal());
    EXPECT_THROW((void)m.terminal(), std::logic_error);
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    auto* t = m.originalTerminal();
    ASSERT_NE(t, nullptr);
    // Text steht im BWS des Terminals, das Pixelbild ist nicht leer (Firmware + 8275 + ZG).
    bool ein = false;
    for (int z = 0; z < 24 && !ein; ++z) ein = t->hw().text(z).find("Hardwaretest U880") != std::string::npos;
    EXPECT_TRUE(ein) << bild(m);
    EXPECT_EQ(m.fbWidth(), 640);
    EXPECT_EQ(m.fbHeight(), 312);
    size_t hell = 0;
    for (int i = 0; i < m.fbWidth() * m.fbHeight(); ++i) hell += m.framebuffer()[i] ? 1 : 0;
    EXPECT_GT(hell, 1000u);
    EXPECT_EQ(m.festeSchnittstellen().front(), "Terminal P8000 Typ 2 (tty1)");
}

/// (a) M2 über das Original: UDOS ⇒ U8000-Monitor, NMI ⇒ Hardwaretest U8001, `O U`, `boot` ⇒ `:`.
TEST(P8000TerminalOriginal, OUDialogBisZumPromptUeberDieTastaturK7673) {
    stumm();
    k1520test::TempDisk disk{FIXTURE};
    P8000Machine m(original(true));
    ASSERT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    tippe(m, "\r");                                   // RETURN am `>` ⇒ BOOT
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << bild(m);
    laufe(m, 2'000'000);
    m.nmi();
    ASSERT_TRUE(laufeBisText(m, "MAXSEG=<0F>", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "*", 40'000'000)) << bild(m);
    tippeZeile(m, "O U");
    tastenFertig(m);
    ASSERT_TRUE(laufeBisText(m, "BOOTING FROM UDOS FLOPPY", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ">", 40'000'000)) << bild(m);
    tippeZeile(m, "boot");
    tastenFertig(m);
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
    EXPECT_NE(bild(m).find("Boot"), std::string::npos) << bild(m);
}

/// Matrixtasten der Oberfläche (`0x04000000 | Zeile << 8 | Spalte`): drücken, halten, loslassen.
TEST(P8000TerminalOriginal, MatrixtasteDerOberflaecheGehtAnDenRechner) {
    stumm();
    P8000Machine m(original());
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    const auto pos = m.originalTerminal()->positionFuer(std::vector<uint8_t>{0x1C});   // RETURN
    ASSERT_TRUE(pos.gueltig());
    const uint32_t kode = P8000Machine::matrixKode(pos.zeile, pos.spalte);
    m.keyPress(kode, false, false);
    laufe(m, 320'000);                                // 80 ms gehalten
    m.keyRelease(kode);
    laufe(m, 320'000);
    // Ein zweites `>` in der nächsten Zeile.
    ASSERT_TRUE(laufeBisPrompt(m, ">", 20'000'000)) << bild(m);
    int prompts = 0;
    for (int z = 0; z < 24; ++z) prompts += zeile(m, z) == ">" ? 1 : 0;
    EXPECT_GE(prompts, 2) << bild(m);
}

/// (c) Save-State-Rundreise am Prompt: Laden ist die Umkehrung des Speicherns, und beide Maschinen
/// laufen danach gleich weiter (Tastenfolge über die K7673 eingeschlossen).
TEST(P8000TerminalOriginal, SaveStateRundreiseAmPrompt) {
    stumm();
    P8000Machine a(original()), b(original());
    a.powerOn();
    b.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(a));
    tippe(a, "h");                                    // Taste mitten im Zeitplan sichern
    laufe(a, 200'000);
    const std::vector<uint8_t> stand = a.stateBytes();
    ASSERT_EQ(stand[4], P8000Machine::P8000_STAND);
    ASSERT_NE(b.stateBytes(), stand);
    ASSERT_TRUE(b.restoreStateBytes(stand)) << b.stateError();
    EXPECT_EQ(b.stateBytes(), stand);
    for (int runde = 0; runde < 3; ++runde) {
        laufe(a, 2'000'000);
        laufe(b, 2'000'000);
        ASSERT_EQ(a.stateBytes(), b.stateBytes()) << "Runde " << runde;
    }
    EXPECT_EQ(bild(a), bild(b));
    EXPECT_EQ(a.originalTerminal()->hw().pixel(), b.originalTerminal()->hw().pixel());

    // Ein Stand mit Kern-Terminal passt nicht (Fingerabdruck P8KS v4), die Maschine bleibt.
    P8000Machine kern;
    kern.powerOn();
    laufe(kern, 100'000);
    EXPECT_FALSE(b.restoreStateBytes(kern.stateBytes()));
    EXPECT_NE(b.stateError().find("Konfiguration"), std::string::npos) << b.stateError();
    EXPECT_EQ(a.stateBytes(), b.stateBytes());
}

// ─── (b) Mehrplatz: Rechner + zwei Terminal-Maschinen über Loopback-Telnet ───────────────────────

#include <chrono>
#include <functional>
#include <thread>

#include "core/machines/p8000/p8000_terminal_machine.h"

namespace {
using namespace k1520::serial;

/// Kanal @p name des Rechner-Hubs als Telnet-Server auf einem freien Loopback-Port; Rückgabe Port.
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

std::unique_ptr<P8000TerminalMachine> arbeitsplatz(uint16_t port) {
    P8000TerminalMachine::Config c;
    std::string fehler;
    const std::string text = "art=telnet,rolle=client,host=127.0.0.1,port=" + std::to_string(port) + ",verbinden=1";
    EXPECT_TRUE(P8000TerminalMachine::konfigAusText(text.c_str(), c, fehler)) << fehler;
    auto t = std::make_unique<P8000TerminalMachine>(c);
    t->powerOn();
    return t;
}

std::string termBild(const P8000TerminalMachine& t) {
    std::string s;
    for (int z = 0; z < 24; ++z) {
        std::string l = t.einheit().hw().text(z);
        while (!l.empty() && l.back() == ' ') l.pop_back();
        s += l + "\n";
    }
    return s;
}
}  // namespace

TEST(P8000TerminalOriginal, ZweiArbeitsplaetzeUeberLoopbackTelnet) {
    stumm();
    P8000Machine m(original());
    const uint16_t p0 = ttyAlsServer(m, "tty0"), p2 = ttyAlsServer(m, "tty2");
    ASSERT_NE(p0, 0);
    ASSERT_NE(p2, 0);
    auto t0 = arbeitsplatz(p0), t2 = arbeitsplatz(p2);
    // Verbindungsaufbau abwarten (I/O-Fäden beider Seiten).
    for (int i = 0; i < 200; ++i) {
        if (t0->serialHub()->status(0).zustand == Zustand::Verbunden &&
            t2->serialHub()->status(0).zustand == Zustand::Verbunden) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(t0->serialHub()->status(0).zustand, Zustand::Verbunden);
    ASSERT_EQ(t2->serialHub()->status(0).zustand, Zustand::Verbunden);

    m.powerOn();
    // Gleichschritt: 5 000 Rechnertakte (1,25 ms) ≙ 4 608 Z8-Takte je Terminal.
    auto schritt = [&] {
        m.run(int(kBatch));
        t0->run(4608);
        t2->run(4608);
    };
    auto bisText = [&](const std::function<std::string()>& b, const std::string& nadel, long long grenze) {
        for (long long t = 0; t < grenze; t += kBatch) {
            schritt();
            if (t % 100'000 == 0 && b().find(nadel) != std::string::npos) return true;
        }
        return b().find(nadel) != std::string::npos;
    };
    auto konsole = [&] { return bild(m); };
    auto bild0 = [&] { return termBild(*t0); };
    auto bild2 = [&] { return termBild(*t2); };
    ASSERT_TRUE(bisText(konsole, "Press RETURN", 200'000'000)) << bild(m);
    // Beide Arbeitsplätze haben ihre Einschaltmeldung (Firmware P8T 5.0 in eigener Maschine).
    EXPECT_NE(bild0().find("ADM31/9600 baud"), std::string::npos) << bild0();
    EXPECT_NE(bild2().find("ADM31/9600 baud"), std::string::npos) << bild2();
    m.keyPress('\r', false, false);
    ASSERT_TRUE(bisText(konsole, "\n>", 20'000'000)) << bild(m);
    // MON8 `PW <port> <byte>` (U880SM.S IOPORT): MON8 richtet nur tty1 ein — SIO0-A (tty0) und
    // SIO1-A (tty2) stehen nach dem Hardwaretest auf 5 Bit.  Also wie ein Betriebssystem: WR5 =
    // 68H (8 Bit, Sender an) über 25H bzw. 29H (Steuerregister Kanal A), dann je zwei Bytes ins Senderegister
    // (24H bzw. 28H).  Der Rechner gibt auf seinen ttys aus, die Arbeitsplätze zeigen es.
    // Die erste Eingabe am `>` nach dem Hardwaretest bootet (beobachtet, mit dem Kern-Terminal
    // ebenso — Gastverhalten): erst RETURN ⇒ „DISK ERROR“ abwarten, sonst gehen die Zeichen im Ladeversuch unter.
    m.keyPress('\r', false, false);
    ASSERT_TRUE(bisText(konsole, "DISK ERROR", 80'000'000)) << bild(m);
    for (int i = 0; i < 2000; ++i) schritt();
    for (const char* z : {"pw 25 05\r", "pw 25 68\r", "pw 29 05\r", "pw 29 68\r",
                          "pw 24 54\r", "pw 24 30\r", "pw 28 54\r", "pw 28 32\r"}) {
        for (const char* c = z; *c; ++c) m.keyPress(uint8_t(*c), false, false);
        for (long long t = 0; t < 40'000'000 && !m.originalTerminal()->tastenFertig(); t += kBatch) schritt();
        for (int i = 0; i < 400; ++i) schritt();
    }
    for (int i = 0; i < 20; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); schritt(); }   // I/O-Fäden
    ASSERT_TRUE(bisText(bild0, "T0", 4'000'000)) << bild0() << "\n--- Konsole\n" << bild(m);
    ASSERT_TRUE(bisText(bild2, "T2", 4'000'000)) << bild2() << "\n--- Konsole\n" << bild(m);
    EXPECT_EQ(bild0().find("T2"), std::string::npos);
    EXPECT_EQ(bild2().find("T0"), std::string::npos);

    // Rückweg: Taste „x“ am Arbeitsplatz an tty0 (K7673 → Firmware → Telnet → SIO0-A); MON8
    // `PR 24` liest das Empfangsregister und zeigt 78.
    t0->keyPress('x', false, false);
    for (long long t = 0; t < 8'000'000 && !t0->einheit().tastenFertig(); t += kBatch) schritt();
    for (int i = 0; i < 40; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); schritt(); }
    for (const char* c = "pr 24\r"; *c; ++c) m.keyPress(uint8_t(*c), false, false);
    for (long long t = 0; t < 40'000'000 && !m.originalTerminal()->tastenFertig(); t += kBatch) schritt();
    ASSERT_TRUE(bisText(konsole, "\n78", 4'000'000)) << bild(m);
}
