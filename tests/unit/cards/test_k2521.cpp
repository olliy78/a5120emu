/**
 * @file test_k2521.cpp
 * @brief ZRE K2521 (PRG 710/710-1): Speicherfenster, CTC 80H/PIO 84H, Interruptreihenfolge,
 *        Kaskade ZC/TO2 → CLK/TRG3 und die volle 16-Bit-E/A-Adresse am Bus.
 *        doc/design/20_prg710.md §3.1, §4a, AP-P1a.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "core/cards/k2521/k2521.h"
#include "core/cards/k2521/rom_prg710.h"
#include "core/cards/k2521/rom_prg710_1.h"
#include "core/cards/k2521/rom_k8915g2.h"
#include "core/cards/k2521/rom_k8915g2_repariert.h"

namespace {

struct K2521Test : ::testing::Test {
    K1520Bus bus;
    K2521    zre{bus, K2521::Config::prg710()};
    void SetUp() override {
        zre.attachToBus(bus);
        bus.setInterruptChain({&zre});
        zre.powerOn(0x00);
    }
};

/// Hört auf einen Port und merkt sich die volle E/A-Adresse des Zyklus.
struct Lauscher : BusDevice {
    uint16_t adr = 0;
    uint8_t  wert = 0;
    K1520Bus& b;
    explicit Lauscher(K1520Bus& bus) : b(bus) {}
    uint8_t ioRead(uint8_t) override { adr = b.ioAddress(); return 0x5A; }
    void ioWrite(uint8_t, uint8_t d) override { adr = b.ioAddress(); wert = d; }
    const char* deviceName() const override { return "Lauscher"; }
};

}  // namespace

TEST_F(K2521Test, SpeicherfensterRomLeerFassungRam) {
    EXPECT_TRUE(zre.belegt(0x0000));
    EXPECT_TRUE(zre.belegt(0x0FFF));
    EXPECT_FALSE(zre.belegt(0x1000));
    for (uint16_t a : {0x0000, 0x0123, 0x03FF})
        EXPECT_EQ(zre.memRead(a), PRG710_ZRE_ROM[a]) << a;
    for (uint16_t a : {0x0400, 0x07FF, 0x0BFF})
        EXPECT_EQ(zre.memRead(a), 0xFF) << "leere U555-Fassung bei " << a;
    zre.memWrite(0x0C00, 0x12);
    zre.memWrite(0x0FFF, 0x34);
    EXPECT_EQ(zre.memRead(0x0C00), 0x12);
    EXPECT_EQ(zre.memRead(0x0FFF), 0x34);
    EXPECT_EQ(zre.ramPeek(0), 0x12);
    EXPECT_EQ(zre.memRead(0x1000), 0xFF);
}

TEST_F(K2521Test, RomIstSchreibgeschuetzt) {
    const uint8_t v = zre.memRead(0x0010);
    zre.memWrite(0x0010, uint8_t(~v));
    zre.memWrite(0x0500, 0x00);
    EXPECT_EQ(zre.memRead(0x0010), v);
    EXPECT_EQ(zre.memRead(0x0500), 0xFF);
}

TEST(K2521Rom, BeideFassungenSindEingebaut) {
    K1520Bus bus;
    K2521 a(bus, K2521::Config::prg710());
    K2521 b(bus, K2521::Config::prg710_1());
    EXPECT_EQ(a.config().rom_len, 1024u);
    EXPECT_EQ(b.config().rom_len, 1024u);
    // Beide Lader beginnen mit LD SP,0D00H [ROM §4a]; die Fassungen unterscheiden sich sonst.
    EXPECT_EQ(a.memRead(0x0000), PRG710_ZRE_ROM[0]);
    EXPECT_EQ(b.memRead(0x0000), PRG710_1_ZRE_ROM[0]);
    EXPECT_NE(0, std::memcmp(PRG710_ZRE_ROM, PRG710_1_ZRE_ROM, 1024));
}

TEST(K2521Rom, BestuecktesRomBisBFFUndDarueberRam) {
    // Drei bestückte Fassungen: Länge wird auf 0C00H begrenzt.
    static uint8_t gross[0x1000];
    for (auto& b : gross) b = 0x77;
    K1520Bus bus;
    K2521::Config c;
    c.rom = gross; c.rom_len = sizeof gross;
    K2521 z(bus, c);
    z.powerOn(0x00);
    EXPECT_EQ(z.memRead(0x0BFF), 0x77);
    EXPECT_EQ(z.memRead(0x0C00), 0x00) << "ab 0C00H ist RAM";
}

TEST_F(K2521Test, CtcAn80UndPioAn84SindErreichbar) {
    // CTC: Vektorbasis E0H an Kanal 0, dann Kanal 1 lesen = Zählerstand (Zeitkonstante).
    bus.ioWrite(0x80, 0xE0);
    bus.ioWrite(0x81, 0x45);        // Zähler, TC folgt
    bus.ioWrite(0x81, 0x07);
    EXPECT_EQ(bus.ioRead(0x81), 0x07);

    // PIO Port A, Ausgabebetrieb (Modus 0): Datenport 84H → Ausgangsleitungen.
    bus.ioWrite(0x86, 0x0F);        // Steuer A: Modus 0 (Ausgabe)
    bus.ioWrite(0x84, 0xA5);
    EXPECT_EQ(zre.pio().portARead(), 0xA5);
    // Port B: 85H = Daten, 87H = Steuer.
    bus.ioWrite(0x87, 0x0F);
    bus.ioWrite(0x85, 0x3C);
    EXPECT_EQ(zre.pio().portBRead(), 0x3C);
    // Unbelegt in der Karte: FFH.
    EXPECT_EQ(zre.ioRead(0x88), 0xFF);
}

TEST_F(K2521Test, InterruptReihenfolgeCtcVorPio) {
    bus.ioWrite(0x80, 0xE0);                // CTC-Vektorbasis
    bus.ioWrite(0x80, 0x85);                // Kanal 0: Timer, IE, Vorteiler 16, TC folgt
    bus.ioWrite(0x80, 5);                   // 80 Takte
    bus.ioWrite(0x86, 0x90);                // PIO-A-Vektor
    bus.ioWrite(0x86, 0x4F);                // Modus 1 (Eingabe)
    bus.ioWrite(0x86, 0x87);                // Interrupt frei
    bus.updateInterruptChain();

    zre.clockTick(80);
    zre.pio().portAWrite(0x01);             // Strobe: Daten bereit
    ASSERT_TRUE(zre.ctc().hasInterrupt());
    ASSERT_TRUE(zre.pio().hasInterrupt());
    EXPECT_FALSE(zre.getIEO());

    EXPECT_EQ(bus.interruptAcknowledge(), 0xE0) << "CTC muss zuerst quittieren";
    bus.signalRETI();
    EXPECT_EQ(bus.interruptAcknowledge(), 0x90) << "danach die PIO";
    bus.signalRETI();
    EXPECT_FALSE(zre.hasInterrupt());
    EXPECT_TRUE(zre.getIEO());
}

TEST_F(K2521Test, IeiHoechstePrioritaetIgnoriertSystemIei) {
    zre.setIEI(false);
    bus.ioWrite(0x80, 0xE0);
    bus.ioWrite(0x80, 0x85);
    bus.ioWrite(0x80, 1);
    zre.clockTick(16);
    bus.updateInterruptChain();
    EXPECT_TRUE(zre.hasInterrupt());
    EXPECT_FALSE(zre.getIEO());             // CTC hat angefordert, Kette gesperrt

    K1520Bus b2;
    K2521::Config c = K2521::Config::prg710();
    c.iei_quelle = K2521::IeiQuelle::System;
    K2521 z(b2, c);
    z.setIEI(false);
    EXPECT_FALSE(z.getIEO()) << "bei System-IEI=0 gibt die Karte die Kette nicht frei";
}

TEST_F(K2521Test, KaskadeTo2ZaehltKanal3) {
    // Kanal 2: Zeitgeber, Vorteiler 16, TC = 2 → ZC/TO alle 32 Takte, ohne Interrupt.
    bus.ioWrite(0x80, 0xE0);
    bus.ioWrite(0x82, 0x05);
    bus.ioWrite(0x82, 0x02);
    // Kanal 3: Zähler (CLK/TRG3 = ZC/TO2), fallende Flanke, IE, TC = 3.
    bus.ioWrite(0x83, 0xC5);
    bus.ioWrite(0x83, 0x03);

    zre.clockTick(32 * 2 + 4);
    EXPECT_FALSE(zre.ctc().hasInterrupt()) << "erst zwei Nulldurchgänge";
    zre.clockTick(32);
    bus.updateInterruptChain();
    ASSERT_TRUE(zre.ctc().hasInterrupt()) << "dritter Nulldurchgang zählt Kanal 3 aus";
    EXPECT_EQ(zre.getVector(), 0xE6);       // Basis E0H | Kanal 3 << 1
}

TEST(K2521Kaskade, OhneBrueckeKeinDurchgriff) {
    K1520Bus bus;
    K2521::Config c = K2521::Config::prg710();
    c.kaskade_to2_clk3 = false;
    K2521 z(bus, c);
    z.attachToBus(bus);
    bus.setInterruptChain({&z});
    z.powerOn(0);
    bus.ioWrite(0x80, 0xE0);
    bus.ioWrite(0x82, 0x05);
    bus.ioWrite(0x82, 0x02);
    bus.ioWrite(0x83, 0xC5);
    bus.ioWrite(0x83, 0x03);
    z.clockTick(32 * 6);
    bus.updateInterruptChain();
    EXPECT_FALSE(z.ctc().hasInterrupt());
}

TEST_F(K2521Test, ZctoNachAussen) {
    int n[4] = {};
    zre.setZCTOCallback([&](int ch, bool) { ++n[ch]; });
    bus.ioWrite(0x82, 0x05);
    bus.ioWrite(0x82, 0x02);
    zre.clockTick(64);
    EXPECT_GE(n[2], 1);
}

TEST_F(K2521Test, VolleEaAdresseKommtAmBusAn) {
    Lauscher l(bus);
    bus.registerIO(&l, 0xE8, 4);

    // Bus direkt.
    bus.ioWrite(0x30E8, 0x0F);
    EXPECT_EQ(l.adr, 0x30E8);

    // Über die CPU: OUT (C),A mit BC = 70E8H und OUT (E8H),A mit A = F0H (→ AB8–15 = A).
    const uint8_t prog[] = {
        0x01, 0xE8, 0x70,       // LD BC,70E8H
        0x3E, 0x0F,             // LD A,0FH
        0xED, 0x79,             // OUT (C),A
        0x3E, 0xF0,             // LD A,F0H
        0xD3, 0xE8,             // OUT (E8H),A
        0xDB, 0xE8,             // IN A,(E8H)
        0x76                    // HALT
    };
    zre.setSpeicherweg([&](uint16_t a) { return a < sizeof prog ? prog[a] : uint8_t(0x76); },
                       [](uint16_t, uint8_t) {});
    zre.cpu().reset();

    for (int i = 0; i < 3; ++i) zre.cpu().step();   // LD BC, LD A, OUT (C),A
    EXPECT_EQ(l.adr, 0x70E8) << "OUT (C),r legt BC auf AB0–15";
    EXPECT_EQ(l.wert, 0x0F);

    for (int i = 0; i < 2; ++i) zre.cpu().step();   // LD A, OUT (n),A
    EXPECT_EQ(l.adr, 0xF0E8) << "OUT (n),A legt A auf AB8–15";
    EXPECT_EQ(l.wert, 0xF0);

    zre.cpu().step();                                // IN A,(n): ebenfalls A·256+n
    EXPECT_EQ(l.adr, 0xF0E8);
}

TEST_F(K2521Test, SpeicherwegVorgabeIstDerSystembus) {
    struct Sys : MemDevice {
        uint16_t ra = 0, wa = 0; uint8_t wd = 0;
        uint8_t memRead(uint16_t a) override { ra = a; return 0xAB; }
        void memWrite(uint16_t a, uint8_t d) override { wa = a; wd = d; }
    } sys;
    bus.registerMem(&sys, 0x0000, 0x8000);
    EXPECT_EQ(zre.cpu().readByte(0x0100), 0xAB);     // NICHT das ROM der Karte
    EXPECT_EQ(sys.ra, 0x0100);
    zre.cpu().writeByte(0x0C10, 0x42);
    EXPECT_EQ(sys.wa, 0x0C10);
    EXPECT_EQ(sys.wd, 0x42);

    // Umgeleitet auf die Karte selbst:
    zre.setSpeicherweg([&](uint16_t a) { return zre.memRead(a); },
                       [&](uint16_t a, uint8_t d) { zre.memWrite(a, d); });
    EXPECT_EQ(zre.cpu().readByte(0x0000), PRG710_ZRE_ROM[0]);
    zre.cpu().writeByte(0x0C10, 0x99);
    EXPECT_EQ(zre.memRead(0x0C10), 0x99);
}

// ─── K8915 Gen 2: ROM 175/176/177 (doc/design/24_k8915_varianten.md, AP-V4) ──

namespace {
/// 24-Bit-Summe der ersten 3FDH Byte von Baustein @p i (0 = 175 … 2 = 177).
uint32_t bausteinSumme(const uint8_t* rom, int i) {
    const uint8_t* b = rom + i * 0x400;
    uint32_t s = 0;
    for (int k = 0; k < 0x3FD; ++k) s += b[k];
    return s & 0xFFFFFF;
}
/// Gespeicherte Summe = die letzten 3 Byte des Bausteins (hoch..tief).
uint32_t bausteinGespeichert(const uint8_t* rom, int i) {
    const uint8_t* b = rom + i * 0x400;
    return (uint32_t(b[0x3FD]) << 16) | (uint32_t(b[0x3FE]) << 8) | b[0x3FF];
}
}  // namespace

/// Der Abzug wie gelesen (F9): 177 trägt das gekippte Bit, die Summe stimmt nicht.  Der
/// Abzug ist NICHT die Vorgabe (siehe K8915Gen2VorgabeIstRepariertesRom), bleibt aber
/// als Beleg eingebettet und wird nie geändert.
TEST(K2521Rom, K8915Gen2AbzugUnveraendert) {
    EXPECT_EQ(sizeof(K8915G2_ZRE_ROM), 0x0C00u);
    EXPECT_EQ(bausteinSumme(K8915G2_ZRE_ROM, 0), bausteinGespeichert(K8915G2_ZRE_ROM, 0));  // 175
    EXPECT_EQ(bausteinSumme(K8915G2_ZRE_ROM, 1), bausteinGespeichert(K8915G2_ZRE_ROM, 1));  // 176
    EXPECT_EQ(bausteinSumme(K8915G2_ZRE_ROM, 2), 0x00A680u);   // 177: Prüfsummenfehler im Abzug
    EXPECT_EQ(bausteinGespeichert(K8915G2_ZRE_ROM, 2), 0x00A67Cu);
    EXPECT_EQ(K8915G2_ZRE_ROM[0x0A33], 0x04);   // F9: Byte im Abzug bleibt unverändert
}

/// Vorgabe = Abzug mit genau einem geänderten Byte 0A33H := 00H (F9 gelöst, 2026-10-05);
/// danach stimmt die 24-Bit-Summe JEDES der drei Bausteine.
TEST(K2521Rom, K8915Gen2VorgabeIstRepariertesRom) {
    const auto cfg = K2521::Config::k8915g2();
    ASSERT_NE(cfg.rom, nullptr);
    // Inhalt, nicht Zeiger: `static constexpr` im Kopf ⇒ eine Kopie je Übersetzungseinheit.
    EXPECT_EQ(std::memcmp(cfg.rom, K8915G2_ZRE_ROM_REPARIERT, sizeof(K8915G2_ZRE_ROM_REPARIERT)), 0);
    EXPECT_EQ(cfg.rom_len, 0x0C00u);
    ASSERT_EQ(sizeof(K8915G2_ZRE_ROM_REPARIERT), sizeof(K8915G2_ZRE_ROM));
    EXPECT_TRUE(cfg.kaskade_to0_clk1);
    EXPECT_TRUE(cfg.kaskade_to1_clk2);
    EXPECT_TRUE(cfg.kaskade_to2_clk3);
    EXPECT_EQ(cfg.iei_quelle, K2521::IeiQuelle::System);

    std::vector<size_t> abweichend;
    for (size_t a = 0; a < sizeof(K8915G2_ZRE_ROM); ++a)
        if (cfg.rom[a] != K8915G2_ZRE_ROM[a]) abweichend.push_back(a);
    ASSERT_EQ(abweichend, std::vector<size_t>{0x0A33}) << "genau ein Byte, 0A33H";
    EXPECT_EQ(cfg.rom[0x0A33], 0x00);

    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(bausteinSumme(cfg.rom, i), bausteinGespeichert(cfg.rom, i)) << "Baustein " << 175 + i;
    EXPECT_EQ(bausteinGespeichert(cfg.rom, 2), 0x00A67Cu);

    K1520Bus bus;
    K2521 zre(bus, cfg);
    EXPECT_EQ(zre.memRead(0x0000), 0xF3);       // DI
    EXPECT_EQ(zre.memRead(0x0A33), 0x00);
}
