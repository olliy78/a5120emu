/**
 * @file test_pc1715_seriell.cpp
 * @brief PC 1715 Etappe 4 (doc/design/21_pc1715.md AP-4a): „Drucker X4" (SIO0-A, nur Sender)
 *        und „V.24 X5" (SIO0-B) am `SerialHub`, Leitung 107 an 2DH/2FH, V.24-Boot des Urladers
 *        S502 (BootV24, s502.prn) über Telnet, Drucken unter CP/A in eine Datei.
 *
 * Nie feste Ports: Server auf Port 0, der tatsächliche Port kommt aus dem Status.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/serial/hub.h"
#include "core/serial/net/socket.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;
using namespace k1520::serial;
using namespace k1520test::pc1715;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

std::string dateiInhalt(const std::string& pfad) {
    std::ifstream f(fs::u8path(pfad), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/// Nur die Wandler takten (für Tests, die die SIO von Hand bedienen).
struct Takter {
    Pc1715Machine& m;
    uint64_t z = 0;
    void lauf(uint64_t takte) {
        for (uint64_t e = z + takte; z < e;) { z += 64; m.serialHub()->takt(z); }
    }
};

/// CTC0 Kanal @p k auf Zeitgeber, Vorteiler 16, Zeitkonstante 1 (9600 Bd bei SIO ×16).
void ctc9600(Pc1715Machine& m, int k) {
    m.bus().ioWrite(uint8_t(0x08 + k), 0x05);
    m.bus().ioWrite(uint8_t(0x08 + k), 0x01);
}
/// SIO0-Steuerport @p steuer (0EH = A, 0FH = B): WR4 ×16/1 Stopp/ohne Parität, WR3 Rx 8 Bit,
/// WR5 Tx 8 Bit + RTS/DTR.
void sioInit(Pc1715Machine& m, uint8_t steuer) {
    for (uint8_t b : {uint8_t(4), uint8_t(0x44), uint8_t(3), uint8_t(0xC1), uint8_t(5), uint8_t(0xEA)})
        m.bus().ioWrite(steuer, b);
}
}  // namespace

TEST(Pc1715Seriell, HubNamenUndLeitungen) {
    stumm();
    Pc1715Machine m;
    auto* hub = m.serialHub();
    ASSERT_NE(hub, nullptr);
    ASSERT_EQ(hub->anzahl(), 2);
    const auto a = m.serielleAnschluesse();
    EXPECT_STREQ(a[0]->name(), "Drucker");
    EXPECT_STREQ(a[0]->stecker(), "X4");
    EXPECT_STREQ(a[1]->name(), "V.24");
    EXPECT_STREQ(a[1]->stecker(), "X5");
    EXPECT_TRUE(a[0]->v24());
    EXPECT_TRUE(a[1]->v24());
    EXPECT_EQ(m.festeSchnittstellen().size(), 1u);   // Tastatur bleibt fest an SIO-A
}

/// V.24 X5 mit Loop: ein Byte, das der Gast an SIO-B sendet, kommt im Empfänger von SIO-B an —
/// in der Zeichenzeit; Baud aus CTC0 K1 × SIO ×16.  Leitung 107 folgt DTR (Prüfstecker).
TEST(Pc1715Seriell, V24LoopSendenUndEmpfangenSamtLeitung107) {
    stumm();
    Pc1715Machine m;
    ctc9600(m, 1);
    auto* hub = m.serialHub();
    SerialKonfig k = hub->konfig(1);
    k.loop = true;
    ASSERT_TRUE(hub->konfigurieren(1, k));
    Takter t{m};
    t.lauf(20'000);
    EXPECT_EQ(m.bus().ioRead(0x2D) & 0x04, 0x04) << "107 AUS vor DTR";
    sioInit(m, 0x0F);                      // WR5 = EAH: DTR + RTS + Tx 8 Bit
    const auto f = m.serielleAnschluesse()[1]->format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 9600u);
    t.lauf(20'000);
    EXPECT_EQ(m.bus().ioRead(0x2D) & 0x04, 0x00) << "Loop: DTR → DSR (107 EIN = 0)";
    EXPECT_EQ(m.bus().ioRead(0x2F) & 0x04, 0x00);

    m.bus().ioWrite(0x0D, 0x5A);           // SIO-B Daten
    t.lauf(200'000);
    EXPECT_EQ(m.bus().ioRead(0x0F) & 1, 1) << "RR0 D0: Zeichen da";
    EXPECT_EQ(m.bus().ioRead(0x0D), 0x5A);
}

/// Drucker X4: was der Gast an SIO-A sendet, geht in die Datei; der Empfänger von SIO-A (Tastatur)
/// bleibt unberührt.  CTS (106) kommt mit der Verbindung.
TEST(Pc1715Seriell, DruckerSenderLandetInDerDatei) {
    stumm();
    const std::string pfad = k1520test::tempPath("pc1715_drucker_direkt.txt");
    fs::remove(fs::u8path(pfad));
    Pc1715Machine m;
    ctc9600(m, 0);
    sioInit(m, 0x0E);
    auto* hub = m.serialHub();
    SerialKonfig k = hub->konfig(0);
    k.betriebsart = Betriebsart::Datei;
    k.datei = pfad;
    ASSERT_TRUE(hub->konfigurieren(0, k));
    ASSERT_TRUE(hub->start(0));
    Takter t{m};
    t.lauf(10'000);
    EXPECT_NE(m.bus().ioRead(0x0E) & 0x20, 0) << "CTSA (106) mit Verbindung";
    for (char c : std::string("Hallo X4")) {
        m.bus().ioWrite(0x0C, uint8_t(c));
        t.lauf(30'000);
    }
    hub->stop(0);
    EXPECT_EQ(dateiInhalt(pfad), "Hallo X4");
    EXPECT_TRUE(m.zre().sio().channelA().rx_fifo.empty());
    fs::remove(fs::u8path(pfad));
}

/// Alter Unterbau: DFÜ-Rückruf = V.24 X5, Einspeisen geht in den Empfänger von SIO-B.
TEST(Pc1715Seriell, AlterUnterbauV24) {
    stumm();
    Pc1715Machine m;
    ctc9600(m, 1);
    sioInit(m, 0x0F);
    std::string gesehen;
    m.setDFUECallback([&](uint8_t b) { gesehen += char(b); });
    m.bus().ioWrite(0x0D, 'Q');
    Takter t{m};
    t.lauf(100'000);
    EXPECT_EQ(gesehen, "Q");
    m.dfueSend(0x42);
    EXPECT_EQ(m.bus().ioRead(0x0D), 0x42);
}

/// CP/A 1715: `^P` spiegelt die Bildschirmausgabe auf den Drucker (TTY: = SIO-A, Verfahren DTR,
/// fragt CTS) — `dir` erscheint in der Datei.
TEST(Pc1715Seriell, Cpa1715DruckenInDieDatei) {
    stumm();
    const std::string pfad = k1520test::tempPath("pc1715_cpa_drucker.txt");
    fs::remove(fs::u8path(pfad));
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_cpa1715_boot_4lw.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    SerialKonfig k = m.serialHub()->konfig(0);
    k.betriebsart = Betriebsart::Datei;
    k.datei = pfad;
    ASSERT_TRUE(m.serialHub()->konfigurieren(0, k));
    ASSERT_TRUE(m.serialHub()->start(0));
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    m.keyPress(0x10, false, true);    // ^P
    laufe(m, kHalten);
    m.keyRelease(0x10);
    laufe(m, kPause);
    tippeZeile(m, "dir");
    ASSERT_TRUE(laufeBisText(m, "A: @OS      COM", 30'000'000)) << bild(m);
    laufe(m, 20'000'000);
    m.serialHub()->stop(0);
    const std::string aus = dateiInhalt(pfad);
    EXPECT_NE(aus.find("A: M80      COM"), std::string::npos) << aus << "\n" << bild(m);
    fs::remove(fs::u8path(pfad));
}

/// V.24-Boot des Urladers S502 (BootV24, s502.prn): ohne Diskette erwartet er über SIO-B
/// 11H 11H, antwortet 12H 12H, lädt Pakete `0AH adr anz daten` und springt bei `AAH adr`.
/// Ein Telnet-Client auf „V.24 X5" speist ein Programm ein, das eine Kennung ins RAM schreibt.
TEST(Pc1715Seriell, V24BootLaedtUndStartetEinProgramm) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    auto* hub = m.serialHub();
    SerialKonfig k = hub->konfig(1);
    k.betriebsart = Betriebsart::Telnet;
    k.rolle = Rolle::Server;
    k.port = 0;
    ASSERT_TRUE(hub->konfigurieren(1, k));
    ASSERT_TRUE(hub->start(1));
    const uint16_t port = hub->status(1).port_aktiv;
    ASSERT_NE(port, 0);

    net::Fehler fe;
    auto ziele = net::aufloesen("127.0.0.1", port, &fe);
    ASSERT_FALSE(ziele.empty());
    net::Socket s = net::verbindenAlle(ziele, 2000, &fe);
    ASSERT_TRUE(s.gueltig());

    // Warten, bis der Urlader nach der Laufwerkssuche in BootV24 steht (Eintritt 05D7H).
    bool drin = false;
    for (long long t = 0; t < 40'000'000 && !drin; t += 5000) {
        m.run(5000);
        drin = m.cpuPC() >= 0x05D7 && m.cpuPC() < 0x0670;
    }
    ASSERT_TRUE(drin) << "PC = " << std::hex << m.cpuPC();

    // Telnet-Rahmen ohne 0DH/FFH (CR und IAC würden umgeschrieben):
    //   11 11 | 0A 00 80 06 00  3E 5A 32 00 90 76 | AA 00 80
    // Programm @8000H: LD A,5AH / LD (9000H),A / HALT.
    const std::string rahmen("\x11\x11\x0A\x00\x80\x06\x00\x3E\x5A\x32\x00\x90\x76\xAA\x00\x80", 16);
    // Die Bytes liegen im Wandler, bevor die Maschine weiterläuft (sie läuft viel schneller als
    // die Uhr); gelesen wird erst mit der Zeichenzeit.
    ASSERT_GT(net::senden(s.fd(), rahmen.data(), rahmen.size()).n, 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string zurueck;
    bool geladen = false;
    for (long long t = 0; t < 30'000'000 && !geladen; t += 20'000) {
        m.run(20'000);
        char buf[256];
        const auto e = net::empfangen(s.fd(), buf, sizeof buf);
        if (e.status == net::IoStatus::Ok && e.n > 0) zurueck.append(buf, e.n);
        geladen = m.zre().ramPeek(0x9000) == 0x5A;
    }
    EXPECT_TRUE(geladen) << "PC = " << std::hex << m.cpuPC();
    // Die Antwort geht über den I/O-Faden hinaus: nach dem Lauf noch eine Weile einsammeln.
    for (int i = 0; i < 50 && zurueck.find("\x12\x12") == std::string::npos; ++i) {
        m.run(20'000);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        char buf[256];
        const auto e = net::empfangen(s.fd(), buf, sizeof buf);
        if (e.status == net::IoStatus::Ok && e.n > 0) zurueck.append(buf, e.n);
    }
    EXPECT_NE(zurueck.find("\x12\x12"), std::string::npos) << "Antwort 12H 12H fehlt; empfangen " << zurueck.size() << " B, Hub gesendet "
        << hub->status(1).bytes_gesendet << ", Zustand " << int(hub->status(1).zustand);
    EXPECT_EQ(m.zre().ramPeek(0x8000), 0x3E);
    EXPECT_EQ(m.zre().ramPeek(0x8005), 0x76);
    hub->stop(1);
}
