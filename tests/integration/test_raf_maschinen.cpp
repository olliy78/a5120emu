/**
 * @file test_raf_maschinen.cpp
 * @brief RAM-Floppy RAF in den drei Maschinen (doc/design/22_raf512.md §5.2, AP-R2):
 *        Bestückung nach dem Anlegen, 88H/89H über den Systembus der Maschine (A8–A15
 *        wie bei `OUT (C),r`), Reset erhält / Netz-Ein verwirft, Save-State v8 des A5120
 *        samt Ablehnung, A5120.16 + RAF.  Kein Gastcode — der kommt in AP-R5.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/prg710/prg710.h"
#include "tests/support/temp_path.h"

namespace {

// Wie das BIOS: Steuerregister 89H = (B << 8) | A, Datenport 88H mit B = Bytezeiger.
void latch(K1520Bus& bus, uint16_t l) { bus.ioWrite(uint16_t((l & 0xFF00) | 0x89), uint8_t(l)); }
void schreibe(K1520Bus& bus, uint8_t b, uint8_t v) { bus.ioWrite(uint16_t(b << 8 | 0x88), v); }
uint8_t lies(K1520Bus& bus, uint8_t b) { return bus.ioRead(uint16_t(b << 8 | 0x88)); }

/// Die drei Fälle je Maschine; @p neu liefert eine frische Maschine (ohne RAF).
template <class Neu>
void pruefeMaschine(Neu neu) {
    // ── Ohne Option: niemand antwortet auf 88H/89H ─────────────────────────────
    {
        auto m = neu();
        m->powerOn();
        EXPECT_EQ(m->raf(), nullptr);
        latch(m->bus(), 0x0003);
        schreibe(m->bus(), 0x05, 0x5A);
        EXPECT_EQ(lies(m->bus(), 0x05), 0xFF);
        EXPECT_EQ(m->bus().ioRead(0x0089), 0xFF);
    }
    // ── Mit Option: Schreiben/Lesen, Reset erhält, Netz-Ein verwirft ─────────
    {
        auto m = neu();
        ASSERT_TRUE(m->installRaf(RAF::Typ::RAF512)) << m->rafFehler();
        ASSERT_NE(m->raf(), nullptr);
        EXPECT_EQ(m->raf()->kapazitaet(), 512u * 1024u);
        EXPECT_FALSE(m->installRaf(RAF::Typ::RAF512));            // zweite Karte
        EXPECT_FALSE(m->rafFehler().empty());
        m->powerOn();                                              // vor dem ersten Lauf erlaubt

        EXPECT_EQ(lies(m->bus(), 0x00), 0xFF);                     // nach Netz-Ein gesperrt
        latch(m->bus(), 0x0123);
        schreibe(m->bus(), 0x00, 0xA5);
        schreibe(m->bus(), 0x7F, 0x3C);
        EXPECT_EQ(lies(m->bus(), 0x00), 0xA5);
        EXPECT_EQ(lies(m->bus(), 0x7F), 0x3C);
        EXPECT_EQ(m->raf()->peek(0x123 * 128 + 0x7F), 0x3C);

        m->reset();                                                // systemweiter /RESET
        EXPECT_TRUE(m->raf()->gesperrt());
        EXPECT_EQ(lies(m->bus(), 0x00), 0xFF);
        latch(m->bus(), 0x0123);
        EXPECT_EQ(lies(m->bus(), 0x00), 0xA5);                     // Inhalt überlebt
        EXPECT_EQ(lies(m->bus(), 0x7F), 0x3C);

        EXPECT_FALSE(m->installRaf(RAF::Typ::RAF128));             // nach dem Reset fest
        EXPECT_NE(m->rafFehler().find("vor dem ersten"), std::string::npos) << m->rafFehler();

        m->powerOn();                                              // Netz-Ein ohne Pufferung
        latch(m->bus(), 0x0123);
        EXPECT_EQ(lies(m->bus(), 0x00), 0x00);
        EXPECT_EQ(lies(m->bus(), 0x7F), 0x00);
    }
    // ── Nach dem ersten Lauf ist die Bestückung fest ───────────────────────────
    {
        auto m = neu();
        m->powerOn();
        m->run(100);
        EXPECT_FALSE(m->installRaf(RAF::Typ::RAF512));
        EXPECT_EQ(m->raf(), nullptr);
    }
}

std::unique_ptr<A5120Machine> a5120(A5120Machine::Config c = {}) {
    return std::make_unique<A5120Machine>(c);
}

A5120Machine::Config em256() {
    A5120Machine::Config c;
    c.em = A5120Machine::Config::Em::em256;
    return c;
}

/// A5120 mit RAF512, Sektor 0042H beschrieben, Latch offen.
std::unique_ptr<A5120Machine> a5120MitDaten(A5120Machine::Config c = {},
                                            RAF::Typ typ = RAF::Typ::RAF512) {
    auto m = a5120(c);
    EXPECT_TRUE(m->installRaf(typ));
    m->powerOn();
    latch(m->bus(), 0x0042);
    for (int i = 0; i < 128; ++i) schreibe(m->bus(), uint8_t(i), uint8_t(0x80 + i));
    return m;
}

}  // namespace

TEST(RafMaschine, A5120) { pruefeMaschine([] { return a5120(); }); }

TEST(RafMaschine, K8915) {
    pruefeMaschine([] { return std::make_unique<K8915Machine>(); });
}

TEST(RafMaschine, Prg710) {
    pruefeMaschine([] { return std::make_unique<Prg710Machine>(); });
}

TEST(RafMaschine, Prg710_1) {
    pruefeMaschine([] {
        Prg710Machine::Config c;
        c.variante = Prg710Machine::Config::Variante::Prg710_1;
        return std::make_unique<Prg710Machine>(c);
    });
}

TEST(RafMaschine, A5120_16) { pruefeMaschine([] { return a5120(em256()); }); }

/**
 * @test RafMaschine/A5120_16_EmUndRafKoexistieren
 * @brief Entwurf 22 §4: EM-Steuerkarte A8H–AFH und RAF 88H/89H nebeneinander — beide
 *        antworten, keiner stört den anderen.
 */
TEST(RafMaschine, A5120_16_EmUndRafKoexistieren) {
    auto m = a5120MitDaten(em256());
    ASSERT_NE(m->em(), nullptr);
    m->bus().ioWrite(0x40AF, 0x0C);                    // A22[4] ← 0CH (Seite = AB12–15)
    EXPECT_EQ(m->em()->attribute(4), 0x0C);
    EXPECT_EQ(m->bus().ioRead(0x40AF), 0xF3);          // invertiert zurück, DB4–7 offen
    EXPECT_EQ(lies(m->bus(), 0x05), 0x85);             // RAF unberührt, Latch noch offen
    latch(m->bus(), 0x0043);
    schreibe(m->bus(), 0x00, 0x11);
    EXPECT_EQ(m->em()->attribute(4), 0x0C);            // EM unberührt
}

/**
 * @test RafMaschine/SaveStateRundreise
 * @brief Save-State v8: Inhalt und Latch kommen in einer gleich bestückten Maschine an —
 *        auch zusammen mit dem EM-Block (A5120.16).
 */
TEST(RafMaschine, SaveStateRundreise) {
    for (const bool mit_em : {false, true}) {
        SCOPED_TRACE(mit_em ? "A5120.16" : "A5120");
        const auto cfg = mit_em ? em256() : A5120Machine::Config{};
        auto a = a5120MitDaten(cfg);
        if (mit_em) a->bus().ioWrite(0x40AF, 0x0C);
        const std::string pfad = k1520test::tempPath("k1520_raf_state.bin");
        ASSERT_TRUE(a->saveState(pfad));

        auto b = a5120(cfg);
        ASSERT_TRUE(b->installRaf(RAF::Typ::RAF512));
        b->powerOn();
        ASSERT_TRUE(b->loadState(pfad)) << b->stateError();
        EXPECT_EQ(b->raf()->latch(), 0x0042);
        for (int i = 0; i < 128; ++i) ASSERT_EQ(lies(b->bus(), uint8_t(i)), uint8_t(0x80 + i));
        if (mit_em) EXPECT_EQ(b->em()->attribute(4), 0x0C);
        std::remove(pfad.c_str());
    }
}

/**
 * @test RafMaschine/SaveStateMitRafWirdOhnePassendeRafAbgelehnt
 * @brief Ein Stand mit RAF in eine Maschine ohne RAF oder mit anderer Bauart: false, Grund
 *        in stateError(), und NICHTS übernommen (auch kein RAM).
 */
TEST(RafMaschine, SaveStateMitRafWirdOhnePassendeRafAbgelehnt) {
    auto a = a5120MitDaten();
    a->memWriteDebug(0x8000, 0x11);
    const std::string pfad = k1520test::tempPath("k1520_raf_state_ab.bin");
    ASSERT_TRUE(a->saveState(pfad));

    auto ohne = a5120();
    ohne->powerOn();
    ohne->memWriteDebug(0x8000, 0x22);
    EXPECT_FALSE(ohne->loadState(pfad));
    EXPECT_NE(ohne->stateError().find("RAF 512"), std::string::npos) << ohne->stateError();
    EXPECT_NE(ohne->stateError().find("keine RAF"), std::string::npos) << ohne->stateError();
    EXPECT_EQ(ohne->memReadDebug(0x8000), 0x22);

    auto andere = a5120();
    ASSERT_TRUE(andere->installRaf(RAF::Typ::RAF2M));
    andere->powerOn();
    andere->memWriteDebug(0x8000, 0x33);
    EXPECT_FALSE(andere->loadState(pfad));
    EXPECT_NE(andere->stateError().find("RAF-2M"), std::string::npos) << andere->stateError();
    EXPECT_EQ(andere->memReadDebug(0x8000), 0x33);
    EXPECT_EQ(andere->raf()->peek(0x42 * 128), 0x00);
    std::remove(pfad.c_str());

    // Umgekehrt: ein Stand OHNE RAF lädt in eine Maschine mit RAF, die RAF bleibt.
    const std::string pfad2 = k1520test::tempPath("k1520_raf_state_ohne.bin");
    ASSERT_TRUE(ohne->saveState(pfad2));
    auto mit = a5120MitDaten();
    ASSERT_TRUE(mit->loadState(pfad2)) << mit->stateError();
    EXPECT_EQ(mit->raf()->peek(0x42 * 128 + 5), 0x85);
    std::remove(pfad2.c_str());
}

/**
 * @test RafMaschine/SaveStateV7LaedtOhneRafAngabe
 * @brief Ein Stand v7 (vor der RAF) lädt weiter; eine gesteckte RAF behält ihren Inhalt.
 *        Der v7-Stand entsteht aus einem v8-Stand ohne RAF: Versionsbyte 7, RAF-Teil
 *        (Länge 4 B + Kennbyte 0) abgeschnitten.
 */
TEST(RafMaschine, SaveStateV7LaedtOhneRafAngabe) {
    auto ohne = a5120();
    ohne->powerOn();
    ohne->memWriteDebug(0x8000, 0x44);
    const std::string pfad = k1520test::tempPath("k1520_raf_state_v7.bin");
    ASSERT_TRUE(ohne->saveState(pfad));
    std::vector<char> d;
    {
        std::ifstream f(pfad, std::ios::binary);
        d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    ASSERT_GT(d.size(), 13u);
    ASSERT_EQ(d[7], 8);                                 // nach "K1520SS"
    d[7] = 7;
    d.resize(d.size() - 5);
    {
        std::ofstream f(pfad, std::ios::binary | std::ios::trunc);
        f.write(d.data(), std::streamsize(d.size()));
    }
    auto mit = a5120MitDaten();
    ASSERT_TRUE(mit->loadState(pfad)) << mit->stateError();
    EXPECT_EQ(mit->memReadDebug(0x8000), 0x44);
    EXPECT_EQ(mit->raf()->peek(0x42 * 128 + 5), 0x85);
    std::remove(pfad.c_str());
}

/**
 * @test RafMaschine/SnapshotOhneRafInhaltStelltNurDasLatchZurueck
 * @brief Die Rückwärts-Historie des Debuggers (`rs`) zieht Stände OHNE RAF-Inhalt
 *        (bis 2 MB je Stück); benannte Snapshots mit.  Hier beides am Maschinen-API.
 */
TEST(RafMaschine, SnapshotOhneRafInhaltStelltNurDasLatchZurueck) {
    auto m = a5120MitDaten();
    A5120Machine::MachineSnapshot leicht, voll;
    m->captureState(leicht, false);
    m->captureState(voll);
    EXPECT_LT(leicht.raf_state.size(), 16u);
    EXPECT_GT(voll.raf_state.size(), 512u * 1024u);

    latch(m->bus(), 0x0042);
    schreibe(m->bus(), 0x00, 0xEE);
    latch(m->bus(), 0x0099);
    ASSERT_TRUE(m->restoreState(leicht));
    EXPECT_EQ(m->raf()->latch(), 0x0042);
    EXPECT_EQ(lies(m->bus(), 0x00), 0xEE);              // Inhalt NICHT zurückgedreht
    ASSERT_TRUE(m->restoreState(voll));
    EXPECT_EQ(lies(m->bus(), 0x00), 0x80);              // benannter Snapshot: ganz zurück
}
