/**
 * @file test_serielle_kopplung.cpp
 * @brief SerielleKopplung.* — zwei A5120 im selben Prozess über echtes Loopback-TCP
 *        (Entwurf 19 §11, AP-S8).
 *
 * @details
 * Je Fall zwei `A5120Machine`, eine als Server, eine als Client am SerialHub; die
 * Gäste sind kleine Z80-Programme direkt im RAM (kein Disketten-Boot): der **Sender**
 * schreibt ein Muster in seiner Zeichenzeit, der **Empfänger** prüft es Byte für Byte
 * und macht alle 1024 Byte eine „Verarbeitungspause" von 100 ms Maschinenzeit, in der
 * er per XOFF bzw. RTS-Wegnahme anhält.  Was dabei verloren, verdoppelt oder vertauscht
 * würde, zählt er im RAM als Fehler.
 *
 * **1× gegen 10×:** die Maschinen laufen abwechselnd in EINEM Faden, je Runde die eine
 * 1 ms, die andere 10 ms Maschinenzeit — das Verhältnis hält damit auch unter Last
 * (`ctest -j`), wo ein uhrgesteuertes Drosseln es verlöre.  Der Sender ist jeweils
 * der schnelle: das ist der Fall, in dem Rückstau und Flusssteuerung tragen müssen
 * (Leitsätze 1–2).
 *
 * Zwei Binaries aus dieser Quelle: die Standardregression mit 4 KiB je Richtung
 * (`k1520_test_serielle_kopplung`, ~2 s je Fall) und die volle Fassung mit 64 KiB
 * (`…_voll`, `KOPPLUNG_VOLL`, Label `format_integration` → `tools/dev.sh test-format`).
 */
#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"
#include "core/serial/hub.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

using k1520::serial::Betriebsart;
using k1520::serial::Rolle;
using k1520::serial::SerialHub;
using k1520::serial::SerialKonfig;
using k1520::serial::Zustand;
using Uhr = std::chrono::steady_clock;

#ifdef KOPPLUNG_VOLL
constexpr uint32_t kBytes = 65536;
#else
constexpr uint32_t kBytes = 4096;
#endif

// ── Ein Z80-Kleinstassembler: genau die Befehle, die die Testgäste brauchen ──────

class Asm {
public:
    explicit Asm(uint16_t org) : org_(org) {}
    uint16_t hier() const { return static_cast<uint16_t>(org_ + code_.size()); }
    const std::vector<uint8_t>& code() const { return code_; }

    void b(std::initializer_list<int> bytes) { for (int x : bytes) code_.push_back(static_cast<uint8_t>(x)); }
    void w(uint16_t v) { b({v & 0xFF, v >> 8}); }
    void marke(const std::string& n) { marken_[n] = hier(); }

    // Relativsprung (opc = 18 JR, 20 NZ, 28 Z, 30 NC, 38 C) auf eine Marke.
    void jr(int opc, const std::string& ziel) {
        b({opc, 0});
        rel_.push_back({code_.size() - 1, ziel});
    }
    void call(const std::string& ziel) {
        b({0xCD, 0, 0});
        abs_.push_back({code_.size() - 2, ziel});
    }
    void jp(const std::string& ziel) {
        b({0xC3, 0, 0});
        abs_.push_back({code_.size() - 2, ziel});
    }
    std::vector<uint8_t> fertig() {
        for (auto& [pos, n] : rel_) {
            const int d = static_cast<int>(marken_.at(n)) - static_cast<int>(org_ + pos + 1);
            EXPECT_TRUE(d >= -128 && d <= 127) << "JR zu weit: " << n;
            code_[pos] = static_cast<uint8_t>(d);
        }
        for (auto& [pos, n] : abs_) {
            const uint16_t a = marken_.at(n);
            code_[pos] = a & 0xFF;
            code_[pos + 1] = a >> 8;
        }
        return code_;
    }

private:
    uint16_t org_;
    std::vector<uint8_t> code_;
    std::map<std::string, uint16_t> marken_;
    std::vector<std::pair<size_t, std::string>> rel_, abs_;
};

constexpr uint16_t kOrg    = 0x8000;
constexpr uint16_t kFertig = 0x9000;   ///< 1 = Programm am Ende
constexpr uint16_t kFehler = 0x9002;   ///< 16 Bit: Musterfehler beim Empfänger
constexpr uint16_t kTmp    = 0x9004;
constexpr uint16_t kStack  = 0xA000;

enum class Muster { Druckbar, Binaer };    // Druckbar: 20H..7EH (kein XON/XOFF, kein CR)
enum class Fluss { Keine, XonXoff, RtsCts };

struct Gast {
    uint8_t daten, steuer;    ///< SIO-Ports der Schnittstelle (DFÜ/V.24 50/51, DFÜ/IFSS 52/53)
    uint8_t ctcZk = 1;        ///< ZRE-CTC K0 Zeitkonstante: 1 → 9600 Bd, 2 → 4800 Bd (×16)
    Muster  muster = Muster::Binaer;
    Fluss   fluss = Fluss::Keine;
    uint32_t anzahl = kBytes; ///< 1..65536
};

/// Gemeinsamer Anfang: DI, Stapel, ZRE-CTC K0 als Baudtakt, SIO-Kanal initialisieren.
void anfang(Asm& a, const Gast& g, uint8_t wr3) {
    a.b({0xF3});                            // DI
    a.b({0x31}); a.w(kStack);               // LD SP,kStack
    a.b({0x3E, 0x05, 0xD3, 0x0C});          // ZRE-CTC K0: Zeitgeber, Vorteiler 16, ZK folgt
    a.b({0x3E, g.ctcZk, 0xD3, 0x0C});
    // SIO: Kanal-Reset, WR4 44H (×16, 1 Stopp, ohne Parität), WR3, WR5 EAH (DTR, 8 Bit,
    // Tx ein, RTS), WR1 00H (keine Interrupts).
    for (int x : {0x18, 0x04, 0x44, 0x03, int(wr3), 0x05, 0xEA, 0x01, 0x00})
        a.b({0x3E, x, 0xD3, g.steuer});
    a.b({0xAF, 0x32}); a.w(kFertig);        // XOR A / LD (kFertig),A
    a.b({0x21, 0, 0, 0x22}); a.w(kFehler);  // LD HL,0 / LD (kFehler),HL
    // HL = Musterzustand, DE = Restzahl (0 = 65536)
    a.b({0x21}); a.w(g.muster == Muster::Druckbar ? 0x0020 : 0x0000);
    a.b({0x11}); a.w(static_cast<uint16_t>(g.anzahl & 0xFFFF));
}

/// Unterprogramm „naechstes": A = nächstes Musterbyte, HL weiter (B wird benutzt).
void musterUp(Asm& a, Muster m) {
    a.marke("naechstes");
    if (m == Muster::Binaer) {
        a.b({0x7D, 0x84, 0x23, 0xC9});      // LD A,L / ADD A,H / INC HL / RET
    } else {
        a.b({0x7D, 0x47, 0x2C, 0x7D, 0xFE, 0x7F}); // LD A,L / LD B,A / INC L / LD A,L / CP 7FH
        a.jr(0x20, "n_ok");
        a.b({0x2E, 0x20});                  // LD L,20H
        a.marke("n_ok");
        a.b({0x78, 0xC9});                  // LD A,B / RET
    }
}

/// Warten auf TxEmpty (RR0 D2), danach Zeichen @p z senden.
void sendeZeichen(Asm& a, const Gast& g, uint8_t z, const std::string& m) {
    a.marke(m);
    a.b({0xDB, g.steuer, 0xE6, 0x04});
    a.jr(0x28, m);
    a.b({0x3E, z, 0xD3, g.daten});
}

std::vector<uint8_t> senderProgramm(const Gast& g) {
    Asm a(kOrg);
    // RTS/CTS: Auto Enables (WR3 D5) — der SIO-Sender gibt bei inaktivem /CTS nichts ab.
    anfang(a, g, g.fluss == Fluss::RtsCts ? 0xE1 : 0xC1);
    a.marke("schleife");
    if (g.fluss == Fluss::XonXoff) {        // XOFF der Gegenseite beachten
        a.b({0xDB, g.steuer, 0xE6, 0x01});
        a.jr(0x28, "kein_rx");
        a.b({0xDB, g.daten, 0xFE, 0x13});
        a.jr(0x20, "kein_rx");
        a.marke("auf_xon");
        a.b({0xDB, g.steuer, 0xE6, 0x01});
        a.jr(0x28, "auf_xon");
        a.b({0xDB, g.daten, 0xFE, 0x11});
        a.jr(0x20, "auf_xon");
        a.marke("kein_rx");
    }
    a.marke("tx");
    a.b({0xDB, g.steuer, 0xE6, 0x04});
    a.jr(0x28, "tx");
    a.call("naechstes");
    a.b({0xD3, g.daten});
    a.b({0x1B, 0x7A, 0xB3});                // DEC DE / LD A,D / OR E
    a.jr(0x20, "schleife");
    a.b({0x3E, 0x01, 0x32}); a.w(kFertig);
    a.marke("ende");
    a.jr(0x18, "ende");
    musterUp(a, g.muster);
    return a.fertig();
}

std::vector<uint8_t> empfaengerProgramm(const Gast& g) {
    Asm a(kOrg);
    anfang(a, g, 0xC1);
    a.marke("schleife");
    a.marke("rx");
    a.b({0xDB, g.steuer, 0xE6, 0x01});
    a.jr(0x28, "rx");
    a.b({0xDB, g.daten, 0x32}); a.w(kTmp);  // IN A,(daten) / LD (kTmp),A
    a.call("naechstes");
    a.b({0x47, 0x3A}); a.w(kTmp);           // LD B,A / LD A,(kTmp)
    a.b({0xB8});                            // CP B
    a.jr(0x28, "gut");
    a.b({0xE5, 0x2A}); a.w(kFehler);        // PUSH HL / LD HL,(kFehler)
    a.b({0x23, 0x22}); a.w(kFehler);        // INC HL / LD (kFehler),HL
    a.b({0xE1});                            // POP HL
    a.marke("gut");
    a.b({0x1B, 0x7A, 0xB3});                // DEC DE / LD A,D / OR E
    a.jr(0x28, "fertig");
    if (g.fluss != Fluss::Keine) {
        // alle 1024 Byte: anhalten, 100 ms Maschinenzeit „verarbeiten", weiter
        a.b({0x7B, 0xB7});                  // LD A,E / OR A
        a.jr(0x20, "schleife");
        a.b({0x7A, 0xE6, 0x03});            // LD A,D / AND 3
        a.jr(0x20, "schleife");
        if (g.fluss == Fluss::XonXoff) sendeZeichen(a, g, 0x13, "xoff");
        else a.b({0x3E, 0x05, 0xD3, g.steuer, 0x3E, 0xE8, 0xD3, g.steuer});   // WR5: RTS aus
        a.b({0xD5, 0x11}); a.w(9450);       // PUSH DE / LD DE,9450 (≈ 26 T je Runde)
        a.marke("pause");
        a.b({0x1B, 0x7A, 0xB3});
        a.jr(0x20, "pause");
        a.b({0xD1});                        // POP DE
        if (g.fluss == Fluss::XonXoff) sendeZeichen(a, g, 0x11, "xon");
        else a.b({0x3E, 0x05, 0xD3, g.steuer, 0x3E, 0xEA, 0xD3, g.steuer});   // WR5: RTS an
    }
    a.jp("schleife");
    a.marke("fertig");
    a.b({0x3E, 0x01, 0x32}); a.w(kFertig);
    a.marke("ende");
    a.jr(0x18, "ende");
    musterUp(a, g.muster);
    return a.fertig();
}

void laden(A5120Machine& m, const std::vector<uint8_t>& code) {
    for (size_t i = 0; i < code.size(); ++i)
        m.memWriteDebug(static_cast<uint16_t>(kOrg + i), code[i]);
    m.cpuDebug().PC = kOrg;
}

uint16_t wort(A5120Machine& m, uint16_t a) {
    return static_cast<uint16_t>(m.memReadDebug(a) | (m.memReadDebug(a + 1) << 8));
}

/// Server (Port vom System) und Client an Schnittstelle @p i zweier Maschinen verbinden.
void verbinden(A5120Machine& server, A5120Machine& client, int i, Betriebsart art,
               bool xonxoffServer, bool xonxoffClient) {
    SerialHub& hs = *server.serialHub();
    SerialHub& hc = *client.serialHub();
    SerialKonfig ks = hs.konfig(i);
    ks.betriebsart = art;
    ks.rolle = Rolle::Server;
    ks.port = 0;
    ks.xonxoff = xonxoffServer;
    ASSERT_TRUE(hs.konfigurieren(i, ks));
    ASSERT_TRUE(hs.start(i));
    const uint16_t port = hs.status(i).port_aktiv;
    ASSERT_NE(port, 0);

    SerialKonfig kc = hc.konfig(i);
    kc.betriebsart = art;
    kc.rolle = Rolle::Client;
    kc.host = "127.0.0.1";
    kc.port = port;
    kc.xonxoff = xonxoffClient;
    ASSERT_TRUE(hc.konfigurieren(i, kc));
    ASSERT_TRUE(hc.start(i));
    const auto frist = Uhr::now() + std::chrono::seconds(10);
    while (Uhr::now() < frist && (hs.status(i).zustand != Zustand::Verbunden ||
                                  hc.status(i).zustand != Zustand::Verbunden))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(hs.status(i).zustand, Zustand::Verbunden) << hs.status(i).meldung;
    ASSERT_EQ(hc.status(i).zustand, Zustand::Verbunden) << hc.status(i).meldung;
    // Telnet-Verhandlung (BINARY) abwarten, bevor Daten fließen — auf Loopback ein
    // Rundlauf; danach ist CR/NUL kein Thema mehr.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/// Abwechselnd laufen lassen: @p langsam 1 ms, @p schnell 10 ms Maschinenzeit je Runde,
/// bis der Empfänger fertig ist oder die Uhrfrist abläuft.
bool laufen(A5120Machine& langsam, A5120Machine& schnell, A5120Machine& empfaenger,
            std::chrono::seconds frist, const std::function<void()>& beobachten = {}) {
    constexpr int kMs = 2458;   // 1 ms bei 2,4576 MHz
    const auto ende = Uhr::now() + frist;
    while (Uhr::now() < ende) {
        for (int n = 0; n < 50; ++n) {
            langsam.run(kMs);
            schnell.run(10 * kMs);
        }
        if (beobachten) beobachten();
        if (empfaenger.memReadDebug(kFertig) == 1) return true;
    }
    return false;
}

std::string lage(A5120Machine& m, int i) {
    const auto st = m.serialHub()->status(i);
    return "zustand=" + std::to_string(int(st.zustand)) +
           " gesendet=" + std::to_string(st.bytes_gesendet) +
           " empfangen=" + std::to_string(st.bytes_empfangen) +
           " puffer s/e=" + std::to_string(st.puffer_senden) + "/" +
           std::to_string(st.puffer_empfangen) + " meldung=" + st.meldung;
}

constexpr auto kFrist = std::chrono::seconds(
#ifdef KOPPLUNG_VOLL
    280
#else
    50
#endif
);

}  // namespace

#ifdef KOPPLUNG_VOLL
#define KOPPLUNG_FALL(name) TEST(SerielleKopplung, name##_64KiB)
#else
#define KOPPLUNG_FALL(name) TEST(SerielleKopplung, name##_4KiB)
#endif

/**
 * @test SerielleKopplung.TelnetXonXoff_4KiB / _64KiB
 * @brief DFÜ/IFSS (A33-B, keine Steuerleitungen) über Telnet.  Server = Empfänger mit
 *        1× Takt und „XON/XOFF beachten", Client = Sender mit 10× Takt, der ein
 *        empfangenes XOFF bis zum XON befolgt.  Der Empfänger schickt alle 1024 Byte
 *        XOFF, „verarbeitet" 100 ms und schickt XON — kein Byte darf fehlen, sich
 *        verdoppeln oder vertauschen (Leitsätze 1–2, §6.3).
 */
KOPPLUNG_FALL(TelnetXonXoff)
{
    constexpr int kIfss = 1;
    A5120Machine empf, send;   // empf = Server 1×, send = Client 10×
    ASSERT_EQ(empf.serialHub()->info(kIfss).name, "DFÜ/IFSS");
    empf.powerOn();
    send.powerOn();
    Gast g{0x52, 0x53};
    g.muster = Muster::Druckbar;
    g.fluss = Fluss::XonXoff;
    laden(empf, empfaengerProgramm(g));
    laden(send, senderProgramm(g));
    verbinden(empf, send, kIfss, Betriebsart::Telnet, /*xonxoffServer=*/true, false);

    // Rückstau: während der Pausen liegt beim schnellen Sender etwas im Sendepuffer
    // oder beim Empfänger im Empfangspuffer — sonst hätte das XOFF nichts angehalten.
    uint32_t staumax = 0;
    ASSERT_TRUE(laufen(empf, send, empf, kFrist, [&] {
        staumax = std::max({staumax, empf.serialHub()->status(kIfss).puffer_empfangen,
                            send.serialHub()->status(kIfss).puffer_senden});
    })) << "Empfänger: " << lage(empf, kIfss) << "\nSender: " << lage(send, kIfss);
    EXPECT_GT(staumax, 0u);
    EXPECT_EQ(wort(empf, kFehler), 0) << "Musterfehler beim Empfänger";
    EXPECT_EQ(send.memReadDebug(kFertig), 1);
    EXPECT_EQ(send.serialHub()->status(kIfss).bytes_gesendet, kBytes);
    // + XOFF/XON des Empfängers in Gegenrichtung (je 1024 Byte ein Paar, das letzte entfällt)
    EXPECT_EQ(empf.serialHub()->status(kIfss).bytes_gesendet, 2 * (kBytes / 1024 - 1));
    empf.serialHub()->stopAlle();
    send.serialHub()->stopAlle();
}

/**
 * @test SerielleKopplung.Rfc2217RtsCts_4KiB / _64KiB
 * @brief DFÜ/V.24 über RFC 2217 mit Nullmodem-Kreuzung (§6.4).  Server = Sender mit
 *        10× Takt und SIO-*Auto Enables*, Client = Empfänger mit 1× Takt, der alle
 *        1024 Byte RTS wegnimmt (eigener Wandler hält sofort an, beim Server fällt über
 *        SET-CONTROL → CTS der Sender).  Binäres Muster mit allen 256 Werten — auch
 *        FFH (IAC-Verdopplung), 0DH, 11H/13H.  Gleiche Baud → kein Hinweis.
 */
KOPPLUNG_FALL(Rfc2217RtsCts)
{
    constexpr int kV24 = 0;
    A5120Machine send, empf;   // send = Server 10×, empf = Client 1×
    ASSERT_EQ(send.serialHub()->info(kV24).name, "DFÜ/V.24");
    send.powerOn();
    empf.powerOn();
    Gast g{0x50, 0x51};
    g.muster = Muster::Binaer;
    g.fluss = Fluss::RtsCts;
    laden(send, senderProgramm(g));
    laden(empf, empfaengerProgramm(g));
    verbinden(send, empf, kV24, Betriebsart::Rfc2217, false, false);

    // Die Flusssteuerung muss wirklich gegriffen haben: der Empfänger hatte RTS weg,
    // und beim Sender war CTS dadurch inaktiv (gekreuzt über SET-CONTROL).
    bool rtsWeg = false, ctsWeg = false;
    ASSERT_TRUE(laufen(empf, send, empf, kFrist, [&] {
        rtsWeg |= !empf.serialHub()->status(kV24).rts;
        ctsWeg |= !send.serialHub()->status(kV24).cts;
    })) << "Empfänger: " << lage(empf, kV24) << "\nSender: " << lage(send, kV24);
    EXPECT_TRUE(rtsWeg);
    EXPECT_TRUE(ctsWeg);
    EXPECT_EQ(wort(empf, kFehler), 0) << "Musterfehler beim Empfänger";
    EXPECT_EQ(send.serialHub()->status(kV24).bytes_gesendet, kBytes);
    EXPECT_EQ(empf.serialHub()->status(kV24).bytes_empfangen, kBytes);

    const auto ss = send.serialHub()->status(kV24);
    const auto se = empf.serialHub()->status(kV24);
    EXPECT_EQ(ss.baud_nenn, 9600u);
    EXPECT_EQ(se.baud_nenn, 9600u);
    EXPECT_FALSE(ss.baud_abweichend);
    EXPECT_FALSE(se.baud_abweichend);
    // Leitungen nach dem Lauf: beide Gäste haben RTS/DTR gesetzt → gekreuzt CTS/DSR/DCD an.
    EXPECT_TRUE(ss.rts && ss.dtr && ss.cts && ss.dsr && ss.dcd);
    EXPECT_TRUE(se.rts && se.dtr && se.cts && se.dsr && se.dcd);
    send.serialHub()->stopAlle();
    empf.serialHub()->stopAlle();
}

#ifndef KOPPLUNG_VOLL
/**
 * @test SerielleKopplung.UnterschiedlicheBaudWirdAngezeigt
 * @brief Leitsatz 3/4: Server-Gast 9600 Bd, Client-Gast 4800 Bd über RFC 2217 — die
 *        Daten kommen trotzdem an (byteweise Verbindung), beide Seiten zeigen
 *        `baud_abweichend` mit dem Wert der Gegenseite; keine Seite ändert die
 *        Gastbaud (der Gast ist maßgeblich).
 */
TEST(SerielleKopplung, UnterschiedlicheBaudWirdAngezeigt)
{
    constexpr int kV24 = 0;
    A5120Machine srv, cli;   // srv = Empfänger 9600 Bd, 1×; cli = Sender 4800 Bd, 10×
    srv.powerOn();
    cli.powerOn();
    Gast gs{0x50, 0x51};
    gs.anzahl = 256;
    Gast gc = gs;
    gc.ctcZk = 2;
    laden(srv, empfaengerProgramm(gs));
    laden(cli, senderProgramm(gc));
    verbinden(srv, cli, kV24, Betriebsart::Rfc2217, false, false);

    ASSERT_TRUE(laufen(srv, cli, srv, std::chrono::seconds(30)))
        << "Empfänger: " << lage(srv, kV24) << "\nSender: " << lage(cli, kV24);
    EXPECT_EQ(wort(srv, kFehler), 0);
    // SET-BAUDRATE ist um 100 ms Maschinenzeit entprellt — noch etwas laufen lassen.
    for (int n = 0; n < 50; ++n) { srv.run(2458 * 10); cli.run(2458 * 100); }
    const auto frist = Uhr::now() + std::chrono::seconds(5);
    auto ss = srv.serialHub()->status(kV24);
    auto sc = cli.serialHub()->status(kV24);
    while (Uhr::now() < frist && !(ss.baud_abweichend && sc.baud_abweichend)) {
        srv.run(2458);
        cli.run(24580);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ss = srv.serialHub()->status(kV24);
        sc = cli.serialHub()->status(kV24);
    }
    EXPECT_EQ(ss.baud_nenn, 9600u);
    EXPECT_EQ(sc.baud_nenn, 4800u);
    EXPECT_TRUE(ss.baud_abweichend);
    EXPECT_TRUE(sc.baud_abweichend);
    EXPECT_EQ(ss.baud_gegenseite, 4800u) << "Server: Anfrage des Clients";
    EXPECT_EQ(sc.baud_gegenseite, 9600u) << "Client: Antwort des Servers = Gastwert";
    srv.serialHub()->stopAlle();
    cli.serialHub()->stopAlle();
}
#endif
