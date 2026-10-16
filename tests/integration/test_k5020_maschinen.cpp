/**
 * @file test_k5020_maschinen.cpp
 * @brief Kassettenkarte K5020 als Option in den Maschinen (doc/design/29_kassette.md §2, AP K5):
 *        Stecken nach dem Anlegen, zu spaet, doppelt, Tor belegt, ohne Karte antwortet niemand —
 *        und mit echtem Z80-Code in der Maschine einen Block auf ein Band schreiben (Bytes ueber
 *        die Daten-PIO, Interrupt IM 2) und sein Echo (Lese-nach-Schreiben) ueber Daten-PIO B
 *        zuruecklesen.  Basis je Maschine: A5120 (auch .16) 30H, K8915 D0H, PRG 710 30H;
 *        am PC 1715 nur mechanisch (Ausweichbasis D0H — 30H liegt unter LT107/BWS).
 */
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/kassette/kassetten_medium.h"

namespace {

using kassette::KassettenMedium;
using kassette::Seite;

// ── Lage des Gastprogramms (in allen Maschinen RAM, ausserhalb von ROM und Bildspeicher) ──
constexpr uint16_t kCode   = 0x4000;
constexpr uint16_t kIsrA   = 0x4200;   // Daten-PIO A (Ausgabe), Vektor E0H
constexpr uint16_t kIsrB   = 0x4280;   // Daten-PIO B (Eingabe),  Vektor E2H
constexpr uint16_t kSende  = 0x4300;   // Tabelle der zu schreibenden Bytes
constexpr uint16_t kTxp    = 0x7000;   // Zeiger auf das naechste Sendebyte
constexpr uint16_t kTxn    = 0x7002;   // noch zu sendende Bytes
constexpr uint16_t kFertig = 0x7003;   // 1 = Programm zu Ende
constexpr uint16_t kRxp    = 0x7004;   // Zeiger in den Empfangspuffer
constexpr uint16_t kEmpf   = 0x7100;   // Empfangspuffer (Echo)
constexpr uint8_t  kI      = 0x7F;     // Vektortabelle 7FE0H/7FE2H

using Bytes = std::vector<uint8_t>;

void ein(Bytes& p, std::initializer_list<uint8_t> b) { p.insert(p.end(), b); }
void ausgabe(Bytes& p, uint8_t port, uint8_t wert) { ein(p, {0x3E, wert, 0xD3, port}); }   // LD A,n ; OUT (port),A
/// Warteschleife: LD BC,n ; L: DEC BC ; LD A,B ; OR C ; JR NZ,L  (≈ 26 Takte je Zaehler)
void warte(Bytes& p, uint16_t n) { ein(p, {0x01, uint8_t(n), uint8_t(n >> 8), 0x0B, 0x78, 0xB1, 0x20, 0xFB}); }

/// Gastprogramm: Geraet 1 hochfahren, aufnahmebereit, Block senden, Echo einsammeln, abschalten.
Bytes programm(uint8_t b, size_t n) {
    const uint8_t da = b, db = uint8_t(b + 1), dac = uint8_t(b + 2), dbc = uint8_t(b + 3);
    const uint8_t sa = uint8_t(b + 4), sb = uint8_t(b + 5), sac = uint8_t(b + 6), sbc = uint8_t(b + 7);
    Bytes p;
    ein(p, {0xF3, 0x31, 0x00, 0x7F, 0x3E, kI, 0xED, 0x47, 0xED, 0x5E});   // DI ; LD SP ; LD I ; IM 2
    // Steuer-PIO wie das Lade-ROM (zre.prn 0298–02B5): Tor A Bit 7 Eingang, Tor B nur Bit 1 Ausgang
    ausgabe(p, sac, 0x7F); ausgabe(p, sbc, 0x7F);
    ausgabe(p, sa, 0x03);  ausgabe(p, sb, 0x04);
    ausgabe(p, sbc, 0xFF); ausgabe(p, sbc, 0xFD);
    ausgabe(p, sac, 0xFF); ausgabe(p, sac, 0x80);
    ausgabe(p, sa, 0x09);  warte(p, 30'000);                       // RES1 + AWA1, bereit nach 0,3 s
    ausgabe(p, sb, 0x02);  ausgabe(p, sa, 0x19); warte(p, 6'500);  // AUF, dann VOR; 60 ms Vorluecke
    // Daten-PIO: A Ausgabe (Vektor E0H), B Eingabe (Vektor E2H), beide mit Interrupt
    ausgabe(p, dac, 0xE0); ausgabe(p, dac, 0x0F); ausgabe(p, dac, 0x83);
    ausgabe(p, dbc, 0xE2); ausgabe(p, dbc, 0x4F); ausgabe(p, dbc, 0x83);
    ein(p, {0xDB, db});                                            // Blindeingabe: BRDY
    ein(p, {0x21, uint8_t(kSende + 1), uint8_t((kSende + 1) >> 8), 0x22, uint8_t(kTxp), uint8_t(kTxp >> 8)});
    ein(p, {0x3E, uint8_t(n - 1), 0x32, uint8_t(kTxn), uint8_t(kTxn >> 8)});
    ein(p, {0x21, uint8_t(kEmpf), uint8_t(kEmpf >> 8), 0x22, uint8_t(kRxp), uint8_t(kRxp >> 8)});
    ein(p, {0x3A, uint8_t(kSende), uint8_t(kSende >> 8), 0xD3, da});   // erstes Byte von Hand
    // ISR A ruft die weiteren Bytes ab; wenn keines mehr kommt, ist der Block gesendet.
    ein(p, {0xFB});                                                // EI
    const size_t schleife = p.size();
    ein(p, {0x3A, uint8_t(kFertig), uint8_t(kFertig >> 8), 0xB7});   // LD A,(fertig) ; OR A (kein HALT: Wettlauf mit der ISR)
    ein(p, {0x28, uint8_t(schleife - (p.size() + 2))});                    // JR Z,schleife
    warte(p, 3'000);                                               // Echo (≈ 10 ms hinter dem Schreibkopf)
    ausgabe(p, sb, 0x00); ausgabe(p, sa, 0x09); warte(p, 2'000);   // AUF aus, Band anhalten
    ein(p, {0xF3, 0x3E, 0x02, 0x32, uint8_t(kFertig), uint8_t(kFertig >> 8), 0x76});   // DI ; fertig=2 ; HALT
    return p;
}

/// ISR A: naechstes Sendebyte; ohne weiteres Byte "fertig" setzen.
Bytes isrA(uint8_t da) {
    Bytes p;
    ein(p, {0xF5, 0xE5});                                                       // PUSH AF ; PUSH HL
    ein(p, {0x3A, uint8_t(kTxn), uint8_t(kTxn >> 8), 0xB7, 0x28, 0x13});         // LD A,(txn) ; OR A ; JR Z,ende
    ein(p, {0x2A, uint8_t(kTxp), uint8_t(kTxp >> 8), 0x7E, 0xD3, da});           // LD HL,(txp) ; LD A,(HL) ; OUT (DA),A
    ein(p, {0x23, 0x22, uint8_t(kTxp), uint8_t(kTxp >> 8)});                     // INC HL ; LD (txp),HL
    ein(p, {0x3A, uint8_t(kTxn), uint8_t(kTxn >> 8), 0x3D, 0x32, uint8_t(kTxn), uint8_t(kTxn >> 8)});   // DEC (txn)
    ein(p, {0x18, 0x05});                                                        // JR weiter
    ein(p, {0x3E, 0x01, 0x32, uint8_t(kFertig), uint8_t(kFertig >> 8)});         // ende: fertig = 1
    ein(p, {0xE1, 0xF1, 0xFB, 0xED, 0x4D});                                      // weiter: POP HL ; POP AF ; EI ; RETI
    return p;
}

/// ISR B: ein Echobyte in den Puffer.
Bytes isrB(uint8_t db) {
    Bytes p;
    ein(p, {0xF5, 0xE5});
    ein(p, {0x2A, uint8_t(kRxp), uint8_t(kRxp >> 8), 0xDB, db, 0x77, 0x23});     // LD HL,(rxp) ; IN A,(DB) ; LD (HL),A ; INC HL
    ein(p, {0x22, uint8_t(kRxp), uint8_t(kRxp >> 8)});
    ein(p, {0xE1, 0xF1, 0xFB, 0xED, 0x4D});
    return p;
}

void poke(K1520Machine& m, uint16_t a, const Bytes& b) {
    for (size_t i = 0; i < b.size(); ++i) m.memWriteDebug(uint16_t(a + i), b[i]);
}

/// Block aus 6 Bytes: Praeambel AAH, Daten, CRC, Postambel AAH.
Bytes testBlock() { return kassette::blockBauen({0x11, 0x22, 0x33, 0x44}); }

/// Gast schreibt den Block auf Band 1, liest das Echo; danach liegt der Block im Medium.
void schreibenUndEcho(K1520Machine& m, Z80& cpu, uint8_t basis) {
    ASSERT_NE(m.k5020(), nullptr) << m.k5020Fehler();
    auto band = std::make_shared<KassettenMedium>();
    auto& lw = m.k5020()->laufwerk(0);
    ASSERT_TRUE(lw.kassetteEinlegen(band));
    ASSERT_TRUE(lw.positionSetzen(30'000'000));   // 30 m: im Magnetband, hinter der BOT-Marke
    const Bytes blk = testBlock();
    poke(m, kCode, programm(basis, blk.size()));
    poke(m, kIsrA, isrA(basis));
    poke(m, kIsrB, isrB(uint8_t(basis + 1)));
    poke(m, kSende, blk);
    poke(m, uint16_t(kI << 8 | 0xE0), {uint8_t(kIsrA), uint8_t(kIsrA >> 8), uint8_t(kIsrB), uint8_t(kIsrB >> 8)});
    for (uint16_t a : {kFertig, kTxn}) m.memWriteDebug(a, 0);
    cpu.PC = kCode;
    cpu.halted = false;
    for (long long t = 0; t < 8'000'000 && m.memReadDebug(kFertig) != 2; t += m.run(1000)) {}
    ASSERT_EQ(m.memReadDebug(kFertig), 2)
        << "Gastprogramm wurde nicht fertig: PC=" << std::hex << cpu.PC << " halted=" << cpu.halted
        << " fertig=" << int(m.memReadDebug(kFertig)) << " txn=" << int(m.memReadDebug(kTxn))
        << " rx=" << (m.memReadDebug(kRxp) | m.memReadDebug(uint16_t(kRxp + 1)) << 8)
        << " blocks=" << band->anzahlBloecke(Seite::A);
    EXPECT_EQ(m.k5020()->verloreneBytes(), 0u);
    ASSERT_EQ(band->anzahlBloecke(Seite::A), 1u) << "kein Block auf dem Band";
    EXPECT_EQ(band->block(Seite::A, 0).bytes, blk);
    Bytes echo;
    for (size_t i = 0; i < blk.size(); ++i) echo.push_back(m.memReadDebug(uint16_t(kEmpf + i)));
    EXPECT_EQ(echo, blk) << "Lese-nach-Schreiben ueber Daten-PIO B (Vektor E2H)";
}

/// Ein Stoerer auf einem der Tore.
struct Belegt : BusDevice {
    uint8_t ioRead(uint8_t) override { return 0x00; }
    void ioWrite(uint8_t, uint8_t) override {}
    const char* deviceName() const override { return "Stoerer"; }
};

/// Die Bestueckungsfaelle je Maschine; @p neu liefert eine frische Maschine, @p basis die Torbasis.
template <class Neu>
void pruefeBestueckung(Neu neu, uint8_t basis) {
    const uint8_t ende = uint8_t(basis + 7);
    {   // Ohne Option: niemand auf den Toren
        auto m = neu();
        m->powerOn();
        EXPECT_EQ(m->k5020(), nullptr);
        EXPECT_EQ(m->bus().ioOwner(basis), nullptr);
        EXPECT_EQ(m->bus().ioOwner(ende), nullptr);
    }
    {   // Stecken, doppelt, vor dem ersten Lauf erlaubt
        auto m = neu();
        ASSERT_TRUE(m->installK5020()) << m->k5020Fehler();
        ASSERT_NE(m->k5020(), nullptr);
        EXPECT_TRUE(m->k5020Fehler().empty());
        EXPECT_NE(m->bus().ioOwner(basis), nullptr);
        EXPECT_NE(m->bus().ioOwner(ende), nullptr);
        EXPECT_EQ(m->k5020()->config().basis, basis);
        EXPECT_FALSE(m->installK5020());
        EXPECT_NE(m->k5020Fehler().find("bereits"), std::string::npos) << m->k5020Fehler();
        m->powerOn();
        m->run(100);   // laeuft mit Karte
        m->reset();
        EXPECT_NE(m->k5020(), nullptr);
    }
    {   // Nach dem ersten Lauf ist die Bestueckung fest
        auto m = neu();
        m->powerOn();
        m->run(100);
        EXPECT_FALSE(m->installK5020());
        EXPECT_NE(m->k5020Fehler().find("vor dem ersten"), std::string::npos) << m->k5020Fehler();
        EXPECT_EQ(m->k5020(), nullptr);
        EXPECT_EQ(m->bus().ioOwner(basis), nullptr);
    }
    {   // Tor belegt: false mit Grund, KEIN Tor angemeldet
        auto m = neu();
        Belegt stoerer;
        m->bus().registerIO(&stoerer, uint8_t(basis + 5), 1);
        EXPECT_FALSE(m->installK5020());
        char tor[8];
        std::snprintf(tor, sizeof tor, "%02XH", basis + 5);
        EXPECT_NE(m->k5020Fehler().find(tor), std::string::npos) << m->k5020Fehler();
        EXPECT_EQ(m->k5020(), nullptr);
        EXPECT_EQ(m->bus().ioOwner(basis), nullptr) << "halb angemeldete Karte";
    }
    {   // K5020 und K6022 vertragen sich (verschiedene Tore, beide Ketten)
        auto m = neu();
        EXPECT_TRUE(m->installK6022());
        EXPECT_TRUE(m->installK5020()) << m->k5020Fehler();
        m->powerOn();
        m->run(1000);
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
using K5020Maschine = Leise;

}  // namespace

TEST_F(K5020Maschine, A5120) {
    pruefeBestueckung([] { return std::make_unique<A5120Machine>(); }, 0x30);
}
TEST_F(K5020Maschine, A5120_16) {
    pruefeBestueckung([] { return std::make_unique<A5120Machine>(em256()); }, 0x30);
}
TEST_F(K5020Maschine, K8915) {
    pruefeBestueckung([] { return std::make_unique<K8915Machine>(); }, 0xD0);
}
TEST_F(K5020Maschine, Prg710) {
    pruefeBestueckung([] { return std::make_unique<Prg710Machine>(prg(Prg710Machine::Config::Variante::Prg710)); }, 0x30);
}
TEST_F(K5020Maschine, Prg710_1) {
    pruefeBestueckung([] { return std::make_unique<Prg710Machine>(prg(Prg710Machine::Config::Variante::Prg710_1)); }, 0x30);
}
/// PC 1715: nur mechanisch (Ausweichbasis D0H, kein Gastcode).
TEST_F(K5020Maschine, Pc1715) {
    pruefeBestueckung([] { return std::make_unique<Pc1715Machine>(); }, 0xD0);
}

/**
 * @test K5020Maschine.*_Z80SchreibtBlockUndLiestEcho
 * @brief Gastcode auf der CPU: Geraet hochfahren, aufnahmebereit, Block ueber Daten-PIO A (IM-2-Interrupt
 *        E0H) schreiben, Echo ueber Daten-PIO B (E2H) lesen — der Block liegt danach auf dem Band.
 */
TEST_F(K5020Maschine, A5120_Z80SchreibtBlockUndLiestEcho) {
    A5120Machine m;
    ASSERT_TRUE(m.installK5020());
    m.powerOn();
    schreibenUndEcho(m, m.cpuDebug(), 0x30);
}

TEST_F(K5020Maschine, A5120_16_Z80SchreibtBlockUndLiestEcho) {
    A5120Machine m(em256());
    ASSERT_TRUE(m.installK5020());
    m.powerOn();
    schreibenUndEcho(m, m.cpuDebug(), 0x30);
}

TEST_F(K5020Maschine, K8915_Z80SchreibtBlockUndLiestEcho) {
    K8915Machine m;
    ASSERT_TRUE(m.installK5020());
    m.powerOn();
    m.zre().setReg(0x87);   // alles RAM (wie K6022Maschine.K8915_Z80StanztUndLiest)
    schreibenUndEcho(m, m.zre().cpu(), 0xD0);
}

TEST_F(K5020Maschine, Prg710_1_Z80SchreibtBlockUndLiestEcho) {
    Prg710Machine m(prg(Prg710Machine::Config::Variante::Prg710_1));
    ASSERT_TRUE(m.installK5020());
    m.powerOn();
    schreibenUndEcho(m, m.zre().cpu(), 0x30);
}
