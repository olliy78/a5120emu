/**
 * @file test_mc80_tastatur.cpp
 * @brief Tastatur des MC80.3x auf Zeichenebene (AP M3): Präfixe, Zeichenabstand 9600 Bd 7O2,
 *        Anschlagabstand, Rückstau bei vollem Empfänger, Qt-Abbildung, Summer/Anzeige.
 *        Quelle: doc/mc80/egos.md §3.
 */

#include <gtest/gtest.h>
#include <vector>
#include "core/peripherals/mc80_tastatur/mc80_tastatur.h"

namespace {

struct Leitung {
    Mc80Tastatur t;
    std::vector<uint8_t> empfangen;
    std::vector<uint64_t> zeit;
    uint64_t jetzt = 0;
    bool frei = true;
    Leitung() {
        t.empfaengerFrei = [this] { return frei; };
        t.senden = [this](uint8_t b) { empfangen.push_back(b); zeit.push_back(jetzt); };
    }
    void lauf(uint64_t takte, int schritt = 16) {
        for (uint64_t i = 0; i < takte; i += static_cast<uint64_t>(schritt)) { jetzt += static_cast<uint64_t>(schritt); t.takt(schritt); }
    }
};

TEST(Mc80Tastatur, ZeichentastenMitPraefix1C) {
    Leitung l;
    l.t.tippe("a\n");
    l.lauf(200'000);
    EXPECT_EQ(l.empfangen, (std::vector<uint8_t>{0x1C, 'a', 0x1C, 0x11}));   // ENTER = 11H
    EXPECT_TRUE(l.t.leer());
}

TEST(Mc80Tastatur, ZeichenabstandUndAnschlagabstand) {
    Leitung l;
    l.t.tippe("ab");
    l.lauf(200'000, 1);
    ASSERT_EQ(l.empfangen.size(), 4u);
    // Präfix → Code: ein Zeichen (11 Bit × 256 Takte)
    EXPECT_EQ(l.zeit[1] - l.zeit[0], static_cast<uint64_t>(Mc80Tastatur::TAKTE_JE_ZEICHEN));
    // Code → nächster Anschlag: 20 ms [Annahme]
    EXPECT_EQ(l.zeit[2] - l.zeit[1], static_cast<uint64_t>(Mc80Tastatur::TAKTE_JE_TASTE));
}

TEST(Mc80Tastatur, VollerEmpfaengerStautZurueck) {
    Leitung l;
    l.frei = false;
    l.t.zeichen('x');
    l.lauf(100'000);
    EXPECT_TRUE(l.empfangen.empty());
    EXPECT_EQ(l.t.wartend(), 2u);
    l.frei = true;
    l.lauf(10'000);
    EXPECT_EQ(l.empfangen, (std::vector<uint8_t>{0x1C, 'x'}));
}

TEST(Mc80Tastatur, SondertastenAlsBitmaske) {
    Leitung l;
    l.t.sondertaste(0);
    l.t.sondertaste(6);
    l.t.sondertaste(7);
    l.t.sondertaste(8);   // gibt es nicht
    l.lauf(300'000);
    EXPECT_EQ(l.empfangen, (std::vector<uint8_t>{0x1E, 0x40, 0x1E, 0x01, 0x1E, 0x00}));
}

TEST(Mc80Tastatur, QtAbbildung) {
    Leitung l;
    EXPECT_TRUE(l.t.qtTaste('A', false, false));           // Kappe ohne Umschalter: klein
    EXPECT_TRUE(l.t.qtTaste('A', true, false));            // mit: groß
    EXPECT_TRUE(l.t.qtTaste('C', false, true));            // Strg+C = 03H
    EXPECT_TRUE(l.t.qtTaste(0x01000004, false, false));    // Return = ENTER
    EXPECT_TRUE(l.t.qtTaste(0x01000000, false, false));    // Escape = OFF
    EXPECT_TRUE(l.t.qtTaste(0x01000031, false, false));    // F2 = Sondertaste 1
    EXPECT_TRUE(l.t.qtTaste('1', true, false));            // Ziffern/Zeichen unverändert
    EXPECT_FALSE(l.t.qtTaste(0x01000012, false, false));   // Kursor links: Code unbekannt [?]
    l.lauf(1'000'000);
    EXPECT_EQ(l.empfangen, (std::vector<uint8_t>{0x1C, 'a', 0x1C, 'A', 0x1C, 0x03, 0x1C, 0x11,
                                                 0x1C, 0x13, 0x1E, 0x20, 0x1C, '1'}));
}

TEST(Mc80Tastatur, SummerUndAnzeigeVomRechner) {
    Mc80Tastatur t;
    t.vomRechner(0x07);
    t.vomRechner(0x87);   // 7 Bit
    EXPECT_EQ(t.summerZaehler(), 2u);
    t.vomRechner(0x14);
    EXPECT_TRUE(t.anzeige());
    t.vomRechner(0x12);
    EXPECT_FALSE(t.anzeige());
    t.vomRechner(0x41);   // ohne Wirkung
    EXPECT_EQ(t.summerZaehler(), 2u);
}

TEST(Mc80Tastatur, ResetLeertDieWarteschlange) {
    Leitung l;
    l.t.tippe("abc");
    l.t.reset();
    l.lauf(500'000);
    EXPECT_TRUE(l.empfangen.empty());
}

}  // namespace
