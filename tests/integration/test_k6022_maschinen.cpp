/**
 * @file test_k6022_maschinen.cpp
 * @brief Lochstreifen-Karte K6022 als Option in allen Maschinen (doc/design/23_lochstreifen.md
 *        §3, AP-L1): Stecken nach dem Anlegen, zu spät, doppelt, Tor belegt, ohne Karte
 *        antwortet auf E0H–E7H niemand — und mit echtem Z80-Code in der Maschine ein Byte
 *        über den Stanzer (E0H) ausgeben und eines über den Leser (E4H) einlesen, beide
 *        Male über den END-Interrupt (IM 2) der hinten angehängten PIOs.
 *
 * PIO-Programmierung wie `PTAPE.6022` (tests/unit/cards/test_k6022.cpp).  Der PRG 710
 * fährt den Gasttreiber unter UDOS in `Prg710Lochband.*`; hier läuft er zusätzlich mit
 * demselben Z80-Schnipsel wie A5120 und K8915.
 */
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/prg710/prg710.h"

namespace {

// ── Lage des Schnipsels (in allen Maschinen RAM, außerhalb von ROM und Bildspeicher) ──
constexpr uint16_t kCode   = 0x4000;
constexpr uint16_t kIsrSt  = 0x4100;   // END Stanzer (Vektor EEH): Merker 55H
constexpr uint16_t kIsrLe  = 0x4110;   // END Leser   (Vektor ECH): nur EI/RETI
constexpr uint16_t kMerker = 0x7000;   // 55H = Stanzer hat quittiert
constexpr uint16_t kByte   = 0x7001;   // erstes Nicht-Null-Byte vom Leser
constexpr uint8_t  kI      = 0x7F;     // Vektortabelle 7FECH/7FEEH

/// Z80-Programm (von Hand assembliert, Kommentar = Quelle).
const std::vector<uint8_t> kProg = {
    0xF3,                               // DI
    0x31, 0x00, 0x7F,                   // LD SP,7F00H
    0x3E, kI, 0xED, 0x47,               // LD A,7FH ; LD I,A
    0xED, 0x5E,                         // IM 2
    // Stanzer (PTAPE.6022): B Bitbetrieb D4–D7 ein, A Vektor EEH, Betriebsart 0, EI
    0x3E, 0xFF, 0xD3, 0xE3, 0x3E, 0xF0, 0xD3, 0xE3,
    0x3E, 0xEE, 0xD3, 0xE2, 0x3E, 0x3F, 0xD3, 0xE2, 0x3E, 0x83, 0xD3, 0xE2,
    0x3E, 0x01, 0xD3, 0xE1,             // KOM
    // Leser: B Bitbetrieb, A Vektor ECH, Betriebsart 1, EI
    0x3E, 0xCF, 0xD3, 0xE7, 0x3E, 0xF0, 0xD3, 0xE7,
    0x3E, 0xEC, 0xD3, 0xE6, 0x3E, 0x4F, 0xD3, 0xE6, 0x3E, 0x83, 0xD3, 0xE6,
    0x3E, 0x03, 0xD3, 0xE5,             // KOM
    // Ein Byte stanzen und auf END warten
    0x3E, 0xC1, 0xD3, 0xE0,             // LD A,C1H ; OUT (E0H),A   → RUF
    0xFB, 0x76,                         // EI ; HALT                 → END, ISR setzt 55H
    // Lesen: erster RUF, dann je END ein Byte holen (= nächster RUF), Vorlauf überlesen
    0xDB, 0xE4,                         // IN A,(E4H)                → RUF
    0xFB, 0x76,                         // L: EI ; HALT
    0xDB, 0xE4,                         //    IN A,(E4H)
    0xB7, 0x28, 0xF9,                   //    OR A ; JR Z,L
    0x32, uint8_t(kByte), uint8_t(kByte >> 8),   // LD (7001H),A
    0xF3, 0x76,                         // DI ; HALT
};
const std::vector<uint8_t> kStIsr = {
    0xF5, 0x3E, 0x55, 0x32, uint8_t(kMerker), uint8_t(kMerker >> 8),   // PUSH AF ; LD A,55H ; LD (7000H),A
    0xF1, 0xFB, 0xED, 0x4D,                                              // POP AF ; EI ; RETI
};
const std::vector<uint8_t> kLeIsr = {0xFB, 0xED, 0x4D};                   // EI ; RETI

void lade(K1520Machine& m) {
    auto poke = [&](uint16_t a, const std::vector<uint8_t>& b) {
        for (size_t i = 0; i < b.size(); ++i) m.memWriteDebug(uint16_t(a + i), b[i]);
    };
    poke(kCode, kProg);
    poke(kIsrSt, kStIsr);
    poke(kIsrLe, kLeIsr);
    poke(uint16_t(kI << 8 | 0xEC), {uint8_t(kIsrLe), uint8_t(kIsrLe >> 8)});
    poke(uint16_t(kI << 8 | 0xEE), {uint8_t(kIsrSt), uint8_t(kIsrSt >> 8)});
    m.memWriteDebug(kMerker, 0);
    m.memWriteDebug(kByte, 0);
}

/// Stanzen und Lesen über E0H/E4H mit Gastcode; @p cpu = PC/halted der Maschinen-CPU.
void stanzenUndLesen(K1520Machine& m, Z80& cpu) {
    ASSERT_NE(m.k6022(), nullptr);
    m.k6022()->bandEinlegen({0x5A, 0x00});
    lade(m);
    cpu.PC = kCode;
    cpu.halted = false;
    // 1 Byte stanzen (150 Z/s ≈ 16 000 Takte) + 16 Vorlauf-Bytes lesen (1000 Z/s).
    for (long long t = 0; t < 2'000'000 && m.memReadDebug(kByte) == 0; t += m.run(1000)) {}
    EXPECT_EQ(m.memReadDebug(kMerker), 0x55) << "kein END-Interrupt vom Stanzer (Vektor EEH)";
    ASSERT_EQ(m.k6022()->stanzbandLaenge(), 1u);
    EXPECT_EQ(m.k6022()->stanzband()[0], 0xC1);
    EXPECT_EQ(m.memReadDebug(kByte), 0x5A) << "Leser lieferte das Band nicht (Vektor ECH)";
    EXPECT_EQ(m.k6022()->leserStand().gelesen, 1u);
}

/// Ein Störer auf einem der Tore.
struct Belegt : BusDevice {
    uint8_t ioRead(uint8_t) override { return 0x00; }
    void ioWrite(uint8_t, uint8_t) override {}
    const char* deviceName() const override { return "Stoerer"; }
};

/// Die Bestückungsfälle je Maschine; @p neu liefert eine frische Maschine.
template <class Neu>
void pruefeBestueckung(Neu neu) {
    // ── Ohne Option: niemand auf E0H–E7H ──────────────────────────────────────
    {
        auto m = neu();
        m->powerOn();
        EXPECT_EQ(m->k6022(), nullptr);
        for (int p = 0xE0; p <= 0xE7; ++p) {
            EXPECT_EQ(m->bus().ioOwner(uint8_t(p)), nullptr) << std::hex << p;
            EXPECT_EQ(m->bus().ioRead(uint16_t(p)), 0xFF) << std::hex << p;
        }
    }
    // ── Stecken, doppelt, nach dem Reset fest ─────────────────────────────────
    {
        auto m = neu();
        ASSERT_TRUE(m->installK6022()) << m->k6022Fehler();
        ASSERT_NE(m->k6022(), nullptr);
        EXPECT_TRUE(m->k6022Fehler().empty());
        EXPECT_NE(m->bus().ioOwner(0xE0), nullptr);
        EXPECT_NE(m->bus().ioOwner(0xE7), nullptr);
        EXPECT_FALSE(m->installK6022());                           // zweite Karte
        EXPECT_NE(m->k6022Fehler().find("bereits"), std::string::npos) << m->k6022Fehler();
        m->powerOn();                                              // vor dem ersten Lauf erlaubt
        // Leser ohne Band: STA D6 = Bandende (Tor B des Lesers, E5H) — die Karte antwortet.
        m->bus().ioWrite(0xE7, 0xCF);
        m->bus().ioWrite(0xE7, 0xF0);
        m->run(100);
        EXPECT_EQ(m->bus().ioRead(0xE5) & K6022::STA_BANDENDE, K6022::STA_BANDENDE);
        m->reset();
        EXPECT_NE(m->k6022(), nullptr);
    }
    // ── Nach dem ersten Lauf bzw. Reset ist die Bestückung fest ───────────────
    {
        auto m = neu();
        m->powerOn();
        m->run(100);
        EXPECT_FALSE(m->installK6022());
        EXPECT_NE(m->k6022Fehler().find("vor dem ersten"), std::string::npos) << m->k6022Fehler();
        EXPECT_EQ(m->k6022(), nullptr);
        EXPECT_EQ(m->bus().ioOwner(0xE0), nullptr);
    }
    {
        auto m = neu();
        m->reset();
        EXPECT_FALSE(m->installK6022());
        EXPECT_EQ(m->k6022(), nullptr);
    }
    // ── Tor belegt: false mit Grund, KEIN Tor angemeldet ──────────────────────
    {
        auto m = neu();
        Belegt stoerer;
        m->bus().registerIO(&stoerer, 0xE5, 1);
        EXPECT_FALSE(m->installK6022());
        EXPECT_NE(m->k6022Fehler().find("E5H"), std::string::npos) << m->k6022Fehler();
        EXPECT_EQ(m->k6022(), nullptr);
        EXPECT_EQ(m->bus().ioOwner(0xE0), nullptr) << "halb angemeldete Karte";
        EXPECT_EQ(m->bus().ioOwner(0xE5), &stoerer);
    }
}

A5120Machine::Config em256() {
    A5120Machine::Config c;
    c.em = A5120Machine::Config::Em::em256;
    return c;
}
Prg710Machine::Config prg(Prg710Machine::Config::Variante v) {
    Prg710Machine::Config c;
    c.variante = v;
    return c;
}

struct Leise : ::testing::Test {
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::WARN);
    }
};
using K6022Maschine = Leise;

}  // namespace

TEST_F(K6022Maschine, A5120) {
    pruefeBestueckung([] { return std::make_unique<A5120Machine>(); });
}
TEST_F(K6022Maschine, A5120_16) {
    pruefeBestueckung([] { return std::make_unique<A5120Machine>(em256()); });
}
TEST_F(K6022Maschine, K8915) {
    pruefeBestueckung([] { return std::make_unique<K8915Machine>(); });
}
TEST_F(K6022Maschine, Prg710) {
    pruefeBestueckung([] { return std::make_unique<Prg710Machine>(prg(Prg710Machine::Config::Variante::Prg710)); });
}
TEST_F(K6022Maschine, Prg710_1) {
    pruefeBestueckung([] { return std::make_unique<Prg710Machine>(prg(Prg710Machine::Config::Variante::Prg710_1)); });
}

/**
 * @test K6022Maschine.A5120_Z80StanztUndLiest
 * @brief Gastcode auf der ZVE1: OUT (E0H) stanzt, END kommt als IM-2-Interrupt EEH über die
 *        hinten angehängte Stanzer-PIO; IN (E4H) liest das Band nach dem Vorlauf (ECH).
 */
TEST_F(K6022Maschine, A5120_Z80StanztUndLiest) {
    A5120Machine m;
    ASSERT_TRUE(m.installK6022());
    m.powerOn();
    stanzenUndLesen(m, m.cpuDebug());
}

TEST_F(K6022Maschine, A5120_16_Z80StanztUndLiest) {
    A5120Machine m(em256());
    ASSERT_TRUE(m.installK6022());
    m.powerOn();
    stanzenUndLesen(m, m.cpuDebug());
}

TEST_F(K6022Maschine, K8915_Z80StanztUndLiest) {
    K8915Machine m;
    ASSERT_TRUE(m.installK6022());
    m.powerOn();
    m.zre().setReg(0x87);   // alles RAM (wie K8915Boot.ZreCtcLiefertIm2Interrupt)
    stanzenUndLesen(m, m.zre().cpu());
}

/// Der PRG 710 mit der K6022 VOR dem Fernschreiber (Stellung wie bis AP-L1).
TEST_F(K6022Maschine, Prg710_1_Z80StanztUndLiest) {
    Prg710Machine m(prg(Prg710Machine::Config::Variante::Prg710_1));
    ASSERT_TRUE(m.installK6022());
    m.powerOn();
    stanzenUndLesen(m, m.zre().cpu());
}
