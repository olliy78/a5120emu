/**
 * @file test_a5120_ausfuehrungen.cpp
 * @brief A5120-Ausführungen im Kern (doc/design/30_a5120_fruehe_varianten.md, AP A5).
 *
 * `A5120Machine::Config::ausfuehrung`: `Neu` (Vorgabe, unverändert), `Alt` (4× K3525,
 * K7023.01 16×64 ab FC00H, K7636 am U-Bus der K2526, Brücke Port 0AH Bit 6), `Kmbg` (wie
 * `Alt` + K5020 fest), `Acht` (abgelehnt).  Geprüft wird die Bestückung am Bus — das Booten
 * eines Betriebssystems ist Sache von AP A6/A7.
 */
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "core/machines/a5120/a5120.h"

namespace {

using A = A5120Machine::Config::Ausfuehrung;

std::unique_ptr<A5120Machine> baue(A a) {
    auto m = std::make_unique<A5120Machine>(A5120Machine::Config::fuerAusfuehrung(a));
    m->powerOn();
    return m;
}

/// Port 0AH (BS-PIO B) in Betriebsart 3, alle Leitungen Eingang — wie CP/A beim Kaltstart
/// die Wickelbrücke liest (bioscrt.mac: Bit 6 unterscheidet BAB1 und BAB2/3).
uint8_t port0A(A5120Machine& m) {
    m.bus().ioWrite(0x0B, 0xCF);
    m.bus().ioWrite(0x0B, 0xFF);
    return m.bus().ioRead(0x0A);
}

}  // namespace

TEST(A5120Ausfuehrung, VorgabeIstNeuUndUnveraendert) {
    A5120Machine::Config c;
    EXPECT_EQ(c.ausfuehrung, A::Neu);
    A5120Machine m;
    m.powerOn();
    EXPECT_EQ(m.ausfuehrung(), A::Neu);
    EXPECT_EQ(m.k7023(), nullptr);
    EXPECT_EQ(m.k7636(), nullptr);
    EXPECT_EQ(m.k3525(0), nullptr);
    EXPECT_EQ(m.k5020(), nullptr);
    EXPECT_EQ(m.fbWidth(), 640);
    EXPECT_EQ(m.fbHeight(), 288);
    EXPECT_EQ(port0A(m) & 0x40, 0) << "Neu: Brücke Port 0AH Bit 6 offen (BAB2, K7024)";
    // K7024 bei F800H wie bisher.
    m.memWriteDebug(0xF800, 'N');
    EXPECT_EQ(m.screenChar(0, 0), 'N');
    // Vorgabelaufwerke 4× K5601 (Neu unverändert).
    EXPECT_EQ(A5120Machine::Config::fuerAusfuehrung(A::Neu).drive_profiles,
              A5120Machine::Config{}.drive_profiles);
}

TEST(A5120Ausfuehrung, AltHatK7023BeiFC00UndBildschirm512x256) {
    auto m = baue(A::Alt);
    ASSERT_NE(m->k7023(), nullptr);
    EXPECT_TRUE(m->k7023()->config().variante_01) << "Kartenreihe „5¼″ alt“: K7023.01";
    EXPECT_EQ(m->fbWidth(), 512);
    EXPECT_EQ(m->fbHeight(), 256);
    EXPECT_NE(m->framebuffer(), nullptr);
    m->memWriteDebug(0xFC00, 'A');
    m->memWriteDebug(0xFC00 + 15 * 64 + 63, 'Z');
    EXPECT_EQ(m->screenChar(0, 0), 'A');
    EXPECT_EQ(m->screenChar(63, 15), 'Z');
    EXPECT_EQ(m->screenChar(64, 0), 0) << "ausserhalb 64×16";
    EXPECT_EQ(m->memReadDebug(0xFC00), 'A') << "K7023 hat keine Lesesperre";
    // F800H–FBFFH ist am alten Gerät gewöhnliches RAM (K3525 ab C000H), kein Bildspeicher.
    m->memWriteDebug(0xF800, 0x5A);
    EXPECT_EQ(m->memReadDebug(0xF800), 0x5A);
    EXPECT_EQ(m->screenChar(0, 0), 'A');
}

TEST(A5120Ausfuehrung, AltHatVierK3525Zu16KB) {
    auto m = baue(A::Alt);
    for (int i = 0; i < 4; ++i) {
        ASSERT_NE(m->k3525(i), nullptr) << i;
        EXPECT_EQ(m->k3525(i)->config().base_addr, i * 0x4000) << i;
    }
    EXPECT_EQ(m->k3525(4), nullptr);
    // Über dem Lade-ROM (0000–03FFH) jede Karte beschreiben und zurücklesen.
    for (uint16_t a : {0x0400, 0x4000, 0x7FFF, 0x8000, 0xC000, 0xFBFF}) {
        m->memWriteDebug(a, uint8_t(a >> 8) ^ 0xA5);
        EXPECT_EQ(m->memReadDebug(a), uint8_t(a >> 8) ^ 0xA5) << std::hex << a;
    }
    EXPECT_EQ(m->k3525(1)->rawPtr()[0], 0x40 ^ 0xA5) << "4000H liegt auf der zweiten Karte";
}

TEST(A5120Ausfuehrung, AltSetztDieBrueckeBab1) {
    auto m = baue(A::Alt);
    EXPECT_NE(port0A(*m) & 0x40, 0) << "Port 0AH Bit 6 = 1: CP/A wählt BAB1 (K7023)";
}

TEST(A5120Ausfuehrung, AltTastaturK7636AmUBus) {
    auto m = baue(A::Alt);
    ASSERT_NE(m->k7636(), nullptr);
    // Nach dem Einschalten liegt das Typzeichen 80H bereit (Port 01H Bit 3 = 0).
    EXPECT_EQ(m->bus().ioRead(0x01) & 0x08, 0);
    EXPECT_EQ(m->bus().ioRead(0x06), K7636::TYP);
    EXPECT_EQ(m->bus().ioRead(0x01), K7636::STATUS_RUHE);
    // Der Tastenweg der Maschine (Warteschlange → run()) geht an die K7636, nicht an die K7637.
    m->keyPress('j', false, false);
    m->run(1);
    m->k7636()->service(m->machineCycles() + K7636::ABSTAND_CODES);
    EXPECT_EQ(m->bus().ioRead(0x01) & 0x08, 0);
    EXPECT_EQ(m->bus().ioRead(0x06), 'j');
}

TEST(A5120Ausfuehrung, AltLampenmaskeDerK7636) {
    auto m = baue(A::Alt);
    EXPECT_EQ(m->keyboardLeds(), 0);
    m->bus().ioWrite(0x03, 0x9F);   // wie das Lade-ROM: Signal + Fehlerlampe
    const uint8_t leds = m->keyboardLeds();
    EXPECT_NE(leds & 0x20, 0) << "Bit 5 = Fehlerlampe";
    EXPECT_NE(leds & 0x80, 0) << "Bit 7 = akustisches Signal";
    EXPECT_EQ(leds & 0x0F, 0) << "Wahllampen aus";
}

TEST(A5120Ausfuehrung, AltLaufwerkeDreimalK5600_10) {
    const auto c = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    EXPECT_EQ(c.drive_profiles[0], "K5600.10");
    EXPECT_EQ(c.drive_profiles[1], "K5600.10");
    EXPECT_EQ(c.drive_profiles[2], "K5600.10");
    EXPECT_EQ(c.drive_profiles[3], "none");
    auto m = baue(A::Alt);
    EXPECT_FALSE(m->defaultFormatName(0).empty());
    EXPECT_TRUE(m->defaultFormatName(3).empty()) << "Steckplatz 4 unbestückt";
}

TEST(A5120Ausfuehrung, AltHatKeineK5020KmbgHatSieFest) {
    auto alt = baue(A::Alt);
    EXPECT_EQ(alt->k5020(), nullptr);
    A5120Machine kmbg(A5120Machine::Config::fuerAusfuehrung(A::Kmbg));
    ASSERT_NE(kmbg.k5020(), nullptr);
    EXPECT_NE(kmbg.k7023(), nullptr);
    EXPECT_NE(kmbg.k7636(), nullptr);
    EXPECT_FALSE(kmbg.installK5020()) << "zweite K5020 auf 30H";
    EXPECT_NE(kmbg.k5020Fehler().find("bereits"), std::string::npos);
    kmbg.powerOn();
    EXPECT_NE(kmbg.bus().ioOwner(0x30), nullptr);
}

TEST(A5120Ausfuehrung, AchtUndEmAmAltenGeraetWerdenAbgelehnt) {
    EXPECT_THROW(A5120Machine(A5120Machine::Config::fuerAusfuehrung(A::Acht)),
                 std::invalid_argument);
    auto c = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    c.em = A5120Machine::Config::Em::em256;
    EXPECT_THROW(A5120Machine{c}, std::invalid_argument);
}

TEST(A5120Ausfuehrung, AltZwischenstandNimmtRamUndBildspeicherMit) {
    auto m = baue(A::Alt);
    m->memWriteDebug(0x5000, 0x11);
    m->memWriteDebug(0xC123, 0x22);
    m->memWriteDebug(0xFC10, 'X');
    A5120Machine::MachineSnapshot s;
    m->captureState(s);
    EXPECT_EQ(s.ram[0x5000], 0x11);
    EXPECT_EQ(s.ram[0xC123], 0x22);
    m->memWriteDebug(0x5000, 0x99);
    m->memWriteDebug(0xC123, 0x99);
    m->memWriteDebug(0xFC10, 'Y');
    ASSERT_TRUE(m->restoreState(s));
    EXPECT_EQ(m->memReadDebug(0x5000), 0x11);
    EXPECT_EQ(m->memReadDebug(0xC123), 0x22);
    EXPECT_EQ(m->screenChar(16, 0), 'X');
}

TEST(A5120Ausfuehrung, AltLaeuftDasLadeRomOhneDiskette) {
    // Ohne Diskette fährt das Lade-ROM seine Laufwerks-/Kassettenschleife; die Maschine
    // muss dabei einfach laufen (kein Absturz, kein Hänger im Laufweg).
    auto m = baue(A::Alt);
    long long n = 0;
    for (int i = 0; i < 40; ++i) n += m->run(100'000);
    EXPECT_GE(n, 4'000'000);
}
