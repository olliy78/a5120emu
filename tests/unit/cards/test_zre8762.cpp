/**
 * @file test_zre8762.cpp
 * @brief ZRE 045-8762 (K8915): Speicherbild je Wert des Registers A8H nach
 *        doc/design/16_k8915.md §4.2a, /MEMDI und /MEMDI1, Brückenfeld als Konfiguration.
 *
 * Jede Zeile der Tabelle „Speicherbild" in §4.2a ist ein Fall: was an 0000H,
 * 1000H, 4000H, 8000H und C000H liegt (ROM / Systembus / Bank 1 / Bank 2 Viertel n).
 */

#include <gtest/gtest.h>

#include "core/cards/zre8762/rom_data.h"
#include "core/cards/zre8762/zre8762.h"

namespace {

using Q = K8915Zre::Quelle;

/// Ein Gerät auf dem Systembus, das über den ganzen Adressraum antwortet und jeden
/// Zugriff zählt — so ist „geht an den Bus" direkt beobachtbar.
struct BusSpeicher : MemDevice {
    uint8_t mem[0x10000]{};
    int     reads = 0, writes = 0;
    uint8_t memRead(uint16_t a) override { ++reads; return mem[a]; }
    void    memWrite(uint16_t a, uint8_t d) override { ++writes; mem[a] = d; }
};

struct Fixture : ::testing::Test {
    K1520Bus    bus;
    K8915Zre    zre{bus};
    BusSpeicher sys;
    void SetUp() override {
        // Zwei Hälften, damit die Größe in 16 Bit passt.
        bus.registerMem(&sys, 0x0000, 0x8000);
        bus.registerMem(&sys, 0x8000, 0x8000);
        zre.attachToBus(bus);
        zre.powerOn(0x00);
    }
    void out(uint8_t v) { bus.ioWrite(0xA8, v); }
};

struct Zeile {
    uint8_t reg;
    Q       q[5];            // 0000, 1000, 4000, 8000, C000
    int     viertel;         // Bank-2-Viertel im Fenster (nur wenn q[2] == Bank2)
};

constexpr uint16_t kAdr[5] = {0x0000, 0x1000, 0x4000, 0x8000, 0xC000};

const char* name(Q q) {
    switch (q) {
        case Q::Rom:   return "ROM";
        case Q::Bank1: return "Bank1";
        case Q::Bank2: return "Bank2";
        case Q::Bus:   return "Bus";
    }
    return "?";
}

}  // namespace

/**
 * @test ZRE8762.SpeicherbildJeRegisterwert
 * @brief Tabelle „Speicherbild" aus §4.2a, Zeile für Zeile.
 */
TEST_F(Fixture, SpeicherbildJeRegisterwert)
{
    const Zeile tab[] = {
        {0x00, {Q::Rom,   Q::Bus,   Q::Bus,   Q::Bus,   Q::Bus  }, -1},  // Reset
        {0x8E, {Q::Rom,   Q::Bus,   Q::Bank1, Q::Bank1, Q::Bank1}, -1},
        {0x06, {Q::Rom,   Q::Bus,   Q::Bank1, Q::Bank1, Q::Bank1}, -1},
        {0x87, {Q::Bank1, Q::Bank1, Q::Bank1, Q::Bank1, Q::Bank1}, -1},
        {0x8F, {Q::Bank1, Q::Bank1, Q::Bank1, Q::Bank1, Q::Bank1}, -1},
        {0x44, {Q::Rom,   Q::Bus,   Q::Bank2, Q::Bank1, Q::Bank1},  0},  // RAM-Test des ROMs
        {0x54, {Q::Rom,   Q::Bus,   Q::Bank2, Q::Bank1, Q::Bank1},  1},
        {0x64, {Q::Rom,   Q::Bus,   Q::Bank2, Q::Bank1, Q::Bank1},  2},
        {0x74, {Q::Rom,   Q::Bus,   Q::Bank2, Q::Bank1, Q::Bank1},  3},
        {0x45, {Q::Bank1, Q::Bank1, Q::Bank2, Q::Bank1, Q::Bank1},  0},  // RADE.COM
        {0x55, {Q::Bank1, Q::Bank1, Q::Bank2, Q::Bank1, Q::Bank1},  1},
        {0x65, {Q::Bank1, Q::Bank1, Q::Bank2, Q::Bank1, Q::Bank1},  2},
        {0x75, {Q::Bank1, Q::Bank1, Q::Bank2, Q::Bank1, Q::Bank1},  3},
        {0x8D, {Q::Bank1, Q::Bank1, Q::Bus,   Q::Bank1, Q::Bank1}, -1},  // RAM-Karte am Bus
        {0x0E, {Q::Rom,   Q::Bus,   Q::Bank1, Q::Bank1, Q::Bank1}, -1},  // RADE-Maschinenprobe
    };
    for (const auto& z : tab) {
        out(z.reg);
        for (int i = 0; i < 5; ++i) {
            const auto ort = zre.ortVon(kAdr[i]);
            EXPECT_EQ(ort.quelle, z.q[i])
                << "A8H=" << std::hex << int(z.reg) << " @" << kAdr[i]
                << ": " << name(ort.quelle) << " statt " << name(z.q[i]);
            if (z.q[i] == Q::Bank2)
                EXPECT_EQ(ort.offset, uint32_t(z.viertel) << 14)
                    << "A8H=" << std::hex << int(z.reg) << ": falsches Viertel von Bank 2";
            if (z.q[i] == Q::Bank1)
                EXPECT_EQ(ort.offset, kAdr[i]) << "Bank 1 ist identisch abgebildet";
        }
    }
}

/**
 * @test ZRE8762.RomGrenzeLiegtBei1000H
 * @brief Das ROM deckt nur 0000–0FFF (D9: A12 = A13 = 0); 0FFFH ist ROM, 1000H ist Bus
 *        — dort erscheint die K7024.
 */
TEST_F(Fixture, RomGrenzeLiegtBei1000H)
{
    out(0x06);
    EXPECT_EQ(zre.memRead(0x0000), K8915_ZRE_BOOT_ROM[0]);          // DI
    EXPECT_EQ(zre.memRead(0x0FFF), K8915_ZRE_BOOT_ROM[0x0FFF]);     // Prüfsumme
    EXPECT_EQ(zre.ortVon(0x0FFF).quelle, Q::Rom);
    EXPECT_EQ(zre.ortVon(0x1000).quelle, Q::Bus);
    EXPECT_EQ(zre.ortVon(0x3FFF).quelle, Q::Bus);
}

/**
 * @test ZRE8762.GewaehlterSpeicherErscheintNichtAmBus
 * @brief Adressen, die die Karte bedient, gehen nicht auf den Systembus (D12 → D28);
 *        nicht gewählte schon.  ROM ist schreibgeschützt.
 */
TEST_F(Fixture, GewaehlterSpeicherErscheintNichtAmBus)
{
    out(0x87);
    sys.reads = sys.writes = 0;
    zre.memWrite(0x1234, 0x5A);
    EXPECT_EQ(zre.memRead(0x1234), 0x5A);
    EXPECT_EQ(zre.bankPeek(0, 0x1234), 0x5A);
    EXPECT_EQ(sys.reads + sys.writes, 0) << "Bank-1-Zugriff darf den Bus nicht erreichen";

    out(0x06);                                   // 1000H jetzt Bus (K7024 im Gerät)
    zre.memWrite(0x1234, 0xA5);
    EXPECT_EQ(sys.writes, 1);
    EXPECT_EQ(sys.mem[0x1234], 0xA5);
    EXPECT_EQ(zre.memRead(0x1234), 0xA5);
    EXPECT_EQ(sys.reads, 1);
    EXPECT_EQ(zre.bankPeek(0, 0x1234), 0x5A) << "Bank 1 darunter bleibt unberührt";

    zre.memWrite(0x0000, 0x00);                  // ROM: folgenlos
    EXPECT_EQ(zre.memRead(0x0000), K8915_ZRE_BOOT_ROM[0]);
    EXPECT_EQ(zre.bankPeek(0, 0x0000), 0x00);
}

/**
 * @test ZRE8762.Bank2ViertelSindGetrennt
 * @brief Wie der RAM-Test des ROMs (F119H): je Viertel ein eigenes Kennbyte bei 4000H,
 *        danach jedes wieder lesen.  Bank 1 bei 4000H bleibt unberührt.
 */
TEST_F(Fixture, Bank2ViertelSindGetrennt)
{
    out(0x87);
    zre.memWrite(0x4000, 0xEE);                  // Bank 1
    for (uint8_t v = 0x44; v < 0x80; v += 0x10) { out(v); zre.memWrite(0x4000, v); }
    for (uint8_t v = 0x44; v < 0x80; v += 0x10) { out(v); EXPECT_EQ(zre.memRead(0x4000), v); }
    for (int q = 0; q < 4; ++q)
        EXPECT_EQ(zre.bankPeek(1, uint16_t(q << 14)), 0x44 + 0x10 * q);
    out(0x87);
    EXPECT_EQ(zre.memRead(0x4000), 0xEE);
}

/**
 * @test ZRE8762.MemdiUndMemdi1
 * @brief /MEMDI aktiv bei Bit7 = 1 (X9 → X8, invertiert), /MEMDI1 aktiv bei Bit3 = 0
 *        (X27 → X12, gekreuzt, ohne Inverter).  /MEMDI geht auf den Bus.
 */
TEST_F(Fixture, MemdiUndMemdi1)
{
    struct { uint8_t reg; bool memdi, memdi1; } tab[] = {
        {0x00, false, true}, {0x8E, true, false}, {0x06, false, true},
        {0x87, true, true},  {0x8F, true, false}, {0x44, false, true},
        {0x45, false, true}, {0x8D, true, false}, {0x0E, false, false},
    };
    for (const auto& z : tab) {
        out(z.reg);
        EXPECT_EQ(zre.memdi(),  z.memdi)  << "A8H=" << std::hex << int(z.reg);
        EXPECT_EQ(zre.memdi1(), z.memdi1) << "A8H=" << std::hex << int(z.reg);
        EXPECT_EQ(bus.getMEMDI(), z.memdi);
    }
}

/**
 * @test ZRE8762.ResetLoeschtRegister
 * @brief /RESET = CLR des DS8212 ⇒ 00H (ROM, Rest Bus), RAM bleibt; das Register ist
 *        auf A8H–ABH gespiegelt (A1/A0 nicht dekodiert) und liest FFH.
 */
TEST_F(Fixture, ResetLoeschtRegister)
{
    bus.ioWrite(0xAB, 0x87);                     // Spiegel
    EXPECT_EQ(zre.reg(), 0x87);
    EXPECT_EQ(bus.ioRead(0xA8), 0xFF);
    zre.memWrite(0x0100, 0x42);
    zre.reset();
    EXPECT_EQ(zre.reg(), 0x00);
    EXPECT_EQ(zre.ortVon(0x0000).quelle, Q::Rom);
    EXPECT_EQ(zre.ortVon(0xC000).quelle, Q::Bus);
    EXPECT_EQ(zre.bankPeek(0, 0x0100), 0x42);
    EXPECT_EQ(zre.cpu().PC, 0x0000);
}

/**
 * @test ZRE8762.BrueckenfeldIstKonfiguration
 * @brief Das Feld X8–X27 wirkt als Konfiguration: mit der „geraden" Verdrahtung
 *        (X27 → X26, Seite 3 an Bit3) verlöre der Stub bei FFE0H unter 87H seinen
 *        Code — genau das Argument aus §4.2a, warum diese Stellung nicht bestückt ist.
 */
TEST(ZRE8762Config, BrueckenfeldIstKonfiguration)
{
    K1520Bus bus;
    K8915Zre::Config cfg;
    cfg.feld.x26_seite3 = 27;
    cfg.feld.x12_memdi1 = 13;
    K8915Zre zre(bus, cfg);
    zre.attachToBus(bus);
    zre.powerOn();
    bus.ioWrite(0xA8, 0x87);
    EXPECT_EQ(zre.ortVon(0xC000).quelle, K8915Zre::Quelle::Bus);
    EXPECT_EQ(zre.ortVon(0x8000).quelle, K8915Zre::Quelle::Bank1);
    EXPECT_FALSE(zre.memdi1()) << "X13 = Bit7 direkt: 87H ⇒ H ⇒ /MEMDI1 inaktiv";
}

/**
 * @test ZRE8762.CtcAnPort80HUndRegisterNurSchreibbar
 * @brief AP-T1a: die CTC der ZRE liest und schreibt über 80H–83H (Zeitkonstante
 *        zurück als Zählerstand), das A8H-Register (A8H–ABH) liest den offenen Bus.
 */
TEST_F(Fixture, CtcAnPort80HUndRegisterNurSchreibbar)
{
    bus.ioWrite(0x81, 0x07);                     // Kanal 1: Zeitgeber, ZK folgt
    bus.ioWrite(0x81, 0x30);
    EXPECT_EQ(bus.ioRead(0x81), 0x30);
    EXPECT_TRUE(zre.ctc().isTimerMode(1));
    EXPECT_EQ(zre.reg(), 0x00) << "CTC-Zugriffe fassen das Register nicht an";
    for (uint8_t p = 0xA8; p <= 0xAB; ++p) EXPECT_EQ(bus.ioRead(p), 0xFF);
}

/**
 * @test ZRE8762Config.OffeneKlemmeLiestH
 * @brief Eine Klemme ohne Draht (Nummer außerhalb X9–X27) ist ein offener TTL-Eingang
 *        und liest H — z. B. Seite 2 dann IMMER aus Bank 1, auch nach /RESET (A8H = 0).
 */
TEST(ZRE8762Config, OffeneKlemmeLiestH)
{
    K1520Bus bus;
    K8915Zre::Config cfg;
    cfg.feld.x22_seite2 = 0;                     // Draht gezogen
    K8915Zre zre(bus, cfg);
    zre.attachToBus(bus);
    zre.powerOn();
    EXPECT_EQ(zre.ortVon(0x8000).quelle, K8915Zre::Quelle::Bank1);
    EXPECT_EQ(zre.ortVon(0xC000).quelle, K8915Zre::Quelle::Bus) << "Seite 3 unberührt";
}
