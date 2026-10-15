/**
 * @file test_mc80_segmente.cpp
 * @brief MC80.31E (AP M4): EPROM-Segmente 10H/20H am Segmentfenster.  Die Programme aus
 *        doc/EPROMS/MC80/segmente/ werden von EGOS über seine eigene Kette gefunden
 *        (Kettenglied `FE adrL adrH seg` = Segmentwechsel) und laufen aus dem EPROM.
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <string>

#include "core/machines/mc80/mc80.h"

namespace {

struct Egos31 {
    std::unique_ptr<Mc80Machine> m;
    Egos31() {
        Mc80Machine::Config c;
        c.variante = Mc80Machine::Config::Variante::Mc8031;
        m = std::make_unique<Mc80Machine>(c);
    }
    void fahre(uint64_t takte) {
        const uint64_t ziel = m->totalCycles() + takte;
        while (m->totalCycles() < ziel) m->run(5000);
    }
    void tippe(const char* text, uint64_t nach = 2'000'000) {
        m->tastatur().tippe(text);
        while (!m->tastatur().leer()) m->run(5000);
        fahre(nach);
    }
    std::string bild() const {
        std::string s;
        for (int z = 0; z < 25; ++z) s += std::to_string(z) + ": " + m->textZeile(z) + "\n";
        return s;
    }
    int hell() const {
        int h = 0;
        for (int i = 0; i < m->fbWidth() * m->fbHeight(); ++i) h += m->framebuffer()[i] != 0;
        return h;
    }
};

/// Nach INIT ein Kettenglied `FE adrL adrH seg` an den Anfang des freien RAM-Blocks (4000H,
/// Segment 1) legen: EGOS' eigene Kette (egos.md §1.3, [P 097D]) läuft dann im EPROM-Segment
/// weiter.  Wie die Kette am Gerät in die EPROM-Segmente führt, ist aus den Abzügen nicht
/// ersichtlich [?] -- kein ROM-Abzug enthält ein Glied nach 10H/20H.
void verkette(Egos31& e, uint16_t adr, uint8_t seg)
{
    e.fahre(4'000'000);
    e.tippe("init");
    e.tippe("\n", 4'000'000);
    auto& spe = e.m->spe(0);
    spe.ramPoke(0x4000, 0xFE);
    spe.ramPoke(0x4001, adr & 0xFF);
    spe.ramPoke(0x4002, adr >> 8);
    spe.ramPoke(0x4003, seg);
}

struct Lauf { int imFenster = 0; uint16_t pcMin = 0xFFFF, pcMax = 0; };

/// Kommando tippen, Eingabe auslösen, dann @p befehle Befehle einzeln fahren und die
/// Programmzähler im Segmentfenster einsammeln.
Lauf starte(Egos31& e, const char* kdo, int befehle)
{
    e.tippe(kdo, 500'000);
    e.m->tastatur().tippe("\n");
    while (!e.m->tastatur().leer()) e.m->run(5000);
    Lauf l;
    for (int i = 0; i < befehle; ++i) {
        e.m->run(1);
        const uint16_t pc = e.m->cpuPC();
        if (pc >= 0x4000 && pc < 0xC000) {
            ++l.imFenster;
            l.pcMin = std::min(l.pcMin, pc);
            l.pcMax = std::max(l.pcMax, pc);
        }
    }
    return l;
}

TEST(Mc80Segmente, NurDieVariante31HatEpromSegmente) {
    EXPECT_EQ(Mc80Machine{}.epromSegmente(), 0);   // .30
    Egos31 e;
    ASSERT_EQ(e.m->epromSegmente(), 2);
    EXPECT_EQ(e.m->epromSegment(0).segmentBit(), 0x10);
    EXPECT_EQ(e.m->epromSegment(1).segmentBit(), 0x20);
}

TEST(Mc80Segmente, TetrisLaeuftAusSegment10H) {
    Egos31 e;
    verkette(e, 0x4000, 0x10);
    const Lauf l = starte(e, "te", 400'000);
    EXPECT_GT(l.imFenster, 100'000) << "Programmzähler nicht im EPROM-Segment";
    EXPECT_GE(l.pcMin, 0x4000);
    EXPECT_LT(l.pcMax, 0x5000);   // Abzug d = 4000H–4FFFH
    // Vorspann des Spiels (Text per Mustervergleich aus dem Bild)
    EXPECT_NE(e.m->textZeile(2).find("(C) de*sw 88"), std::string::npos) << e.bild();
    // Segment 10H ist eingeschaltet, das EPROM nicht beschreibbar
    EXPECT_TRUE(e.m->epromSegment(0).segmentAktiv());
    const uint8_t vorher = e.m->epromSegment(0).ramLesen(0x4100);
    e.m->epromSegment(0).ramSchreiben(0x4100, static_cast<uint8_t>(~vorher));
    EXPECT_EQ(e.m->epromSegment(0).ramLesen(0x4100), vorher);
}

TEST(Mc80Segmente, HasiLaeuftAusSegment20H) {
    Egos31 e;
    verkette(e, 0x4000, 0x20);
    const int vorher = e.hell();
    const Lauf l = starte(e, "hasi", 400'000);
    EXPECT_GT(l.imFenster, 100'000);
    EXPECT_GE(l.pcMin, 0x4000);
    EXPECT_LT(l.pcMax, 0x5000);   // Abzug f = 4000H–4FFFH
    EXPECT_GT(e.hell(), vorher + 10'000) << "Spielfeld nicht gezeichnet";
}

TEST(Mc80Segmente, BasicKommandoLaeuftAnUndMeldetFehlendenBereich) {
    // 'BA' (Abzug c, 6000H in Segment 10H): der Eigenbezug der Sprungziele (CALL 6154H)
    // und der Lauf 6006H..6156H belegen die Lage.  Ohne BASIC-Arbeitsbereich endet es mit
    // ERROR %C7 -- BASIC selbst ist damit NICHT nachgewiesen (Abnahmeergebnis M4).
    Egos31 e;
    verkette(e, 0x6000, 0x10);
    const Lauf l = starte(e, "ba", 400'000);
    EXPECT_GT(l.imFenster, 5);
    EXPECT_GE(l.pcMin, 0x6000);
    EXPECT_LT(l.pcMax, 0x7000);
    EXPECT_NE(e.m->textZeile(23).find("ERROR"), std::string::npos) << e.bild();
}

}  // namespace
