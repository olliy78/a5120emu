/**
 * @file test_tastatur1715.cpp
 * @brief Tastatur des PC 1715 (U880 + S600, doc/pc1715/tastatur.md): Ruhe, Statusbyte + Code,
 *        Shift/LOCK/SI-SO, Entprellung, Bitrahmen, Autorepeat nur mit REP, Zeichenkette.
 *        Die Referenzwerte stammen aus §10 der Auswertung (gemessen am ROM).
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/peripherals/tastatur1715/tastatur1715.h"

namespace {
constexpr uint64_t D = Tastatur1715::DURCHLAUF_TAKTE;

struct Prueffeld {
    Tastatur1715 t;
    std::vector<uint8_t> bytes;
    std::vector<uint64_t> zeiten;  // Takte der Tastatur-CPU bei Rahmenende
    Prueffeld() {
        t.byteOut = [this](uint8_t b) { bytes.push_back(b); zeiten.push_back(t.tastaturTakte()); };
    }
    // Ruhezeit nach dem Einschalten: das ROM räumt erst seine Register auf
    void ruhe(uint64_t n = 4 * D) { t.runTastaturTakte(n); }
};
using Bytes = std::vector<uint8_t>;
}  // namespace

TEST(Tastatur1715, RuheSendetNichts) {
    Prueffeld f;
    f.t.runTastaturTakte(1'000'000);
    EXPECT_TRUE(f.bytes.empty());
    EXPECT_FALSE(f.t.ledSiSo());
    EXPECT_FALSE(f.t.ledLock());
}

TEST(Tastatur1715, TasteA_StatusUndCode) {
    Prueffeld f;
    f.ruhe();
    f.t.press(7, 5);  // a
    f.t.runTastaturTakte(8 * D);
    f.t.releaseAll();
    f.t.runTastaturTakte(4 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE0, 0x61}));
}

TEST(Tastatur1715, ShiftVariante) {
    Prueffeld f;
    f.ruhe();
    f.t.press(8, 1);  // Shift links
    f.t.runTastaturTakte(4 * D);
    f.t.press(7, 5);
    f.t.runTastaturTakte(8 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE2, 0x41}));
    // Shift rechts ebenso
    f.bytes.clear();
    f.t.releaseAll();
    f.t.runTastaturTakte(4 * D);
    f.t.press(8, 4);
    f.t.runTastaturTakte(4 * D);
    f.t.press(0, 2);  // 4 / $
    f.t.runTastaturTakte(8 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE2, 0x24}));
}

TEST(Tastatur1715, CtrlVeraendertNurDasStatusbyte) {
    Prueffeld f;
    f.ruhe();
    f.t.press(8, 0);
    f.t.runTastaturTakte(4 * D);
    f.t.press(0, 2);
    f.t.runTastaturTakte(8 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE1, 0x34}));
}

TEST(Tastatur1715, ZuKurzerDruckWirdEntprellt) {
    Prueffeld f;
    f.ruhe();
    f.t.press(7, 5);
    f.t.runTastaturTakte(2 * D);  // zwei Durchläufe genügen nicht (gemessen: 12 000 Takte)
    f.t.releaseAll();
    f.t.runTastaturTakte(10 * D);
    EXPECT_TRUE(f.bytes.empty());
    // Gegenprobe: lang genug
    f.t.press(7, 5);
    f.t.runTastaturTakte(6 * D);
    f.t.releaseAll();
    f.t.runTastaturTakte(4 * D);
    EXPECT_EQ(f.bytes.size(), 2u);
}

TEST(Tastatur1715, GedruecktIstEins_NichtGedruecktIstNull) {
    // Matrix von außen: die Abfrage liest nur gedrückte Positionen als 1
    Prueffeld f;
    f.t.press(12, 3);
    EXPECT_TRUE(f.t.isPressed(12, 3));
    EXPECT_FALSE(f.t.isPressed(12, 4));
    f.t.release(12, 3);
    EXPECT_FALSE(f.t.isPressed(12, 3));
    f.t.press(0, 0);
    f.t.press(12, 7);
    f.t.releaseAll();
    EXPECT_FALSE(f.t.isPressed(0, 0));
    EXPECT_FALSE(f.t.isPressed(12, 7));
}

TEST(Tastatur1715, Bitrahmen_StartStoppUndAbstand) {
    Prueffeld f;
    struct B { bool b; uint64_t t; };
    std::vector<B> bits;
    f.t.bitTrace = [&](bool b, uint64_t t) { bits.push_back({b, t}); };
    f.ruhe();
    f.t.press(7, 5);
    f.t.runTastaturTakte(8 * D);
    // 2 Rahmen à 13 OUT: 1 1 0 d0..d7 1 1
    ASSERT_EQ(bits.size(), 26u);
    for (int r = 0; r < 2; r++) {
        const B* x = &bits[r * 13];
        EXPECT_TRUE(x[0].b);
        EXPECT_TRUE(x[1].b);
        EXPECT_FALSE(x[2].b);  // Startbit
        const uint8_t soll = r == 0 ? 0xE0 : 0x61;
        uint8_t ist = 0;
        for (int i = 0; i < 8; i++) ist |= uint8_t(x[3 + i].b << i);
        EXPECT_EQ(ist, soll);
        EXPECT_TRUE(x[11].b);  // Stoppbit
        EXPECT_TRUE(x[12].b);
        for (int i = 3; i < 10; i++)
            EXPECT_EQ(x[i + 1].t - x[i].t, 32u) << "Rahmen " << r << " Bit " << i - 3;  // 45,7 µs bei 700 kHz
        EXPECT_EQ(x[11].t - x[10].t, 34u);
        EXPECT_EQ(x[12].t - x[11].t, 16u);
    }
    EXPECT_EQ(bits[15].t - bits[2].t, 493u);  // Startbit Rahmen 1 → Startbit Rahmen 2
    // Das Byte kommt erst beim Stoppbit: Zeitpunkt = Takt des ersten Stoppbits
    ASSERT_EQ(f.zeiten.size(), 2u);
    EXPECT_EQ(f.zeiten[0], bits[11].t);
    EXPECT_EQ(f.zeiten[1], bits[24].t);
    // ... und in Rechnertakten umgerechnet
    EXPECT_EQ(f.t.zeitLetztesByte(), bits[24].t * 2457600ull / 700000ull);
}

TEST(Tastatur1715, AutorepeatNurMitRep) {
    {
        Prueffeld f;  // ohne REP: auch nach 3 Mio Takten keine Wiederholung
        f.ruhe();
        f.t.press(7, 5);
        f.t.runTastaturTakte(3'000'000);
        EXPECT_EQ(f.bytes, (Bytes{0xE0, 0x61}));
    }
    Prueffeld f;  // mit REP
    f.ruhe();
    f.t.press(8, 6);
    f.t.runTastaturTakte(4 * D);
    f.t.press(7, 5);
    f.t.runTastaturTakte(2'000'000);
    ASSERT_GE(f.bytes.size(), 8u);
    for (size_t i = 0; i < f.bytes.size(); i += 2) {
        EXPECT_EQ(f.bytes[i], 0xE0);
        EXPECT_EQ(f.bytes[i + 1], 0x61);
    }
    // Erste Wiederholung ≈ 0,67–0,69 Mio Takte nach dem ersten Zeichen, dann alle ≈ 70 718
    const double d1 = double(f.zeiten[2] - f.zeiten[0]);
    EXPECT_NEAR(d1, 680'000, 30'000);
    const double d2 = double(f.zeiten[4] - f.zeiten[2]);
    EXPECT_NEAR(d2, 70'718, 300);
}

TEST(Tastatur1715, LedWechselBeiLockUndSiSo) {
    Prueffeld f;
    f.ruhe();
    // SI/SO schaltet mit dem Druck um, sendet selbst nichts
    f.t.press(8, 7);
    f.t.runTastaturTakte(5 * D);
    EXPECT_TRUE(f.t.ledSiSo());
    EXPECT_FALSE(f.t.ledLock());
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    EXPECT_TRUE(f.t.ledSiSo());
    EXPECT_TRUE(f.bytes.empty());
    // Zeichen trägt Statusbit 2
    f.t.press(7, 5);
    f.t.runTastaturTakte(8 * D);
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE4, 0x61}));
    // zweiter Druck schaltet zurück
    f.bytes.clear();
    f.t.press(8, 7);
    f.t.runTastaturTakte(5 * D);
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    EXPECT_FALSE(f.t.ledSiSo());
    // LOCK: erster Druck schaltet sofort ein, Großbuchstaben, Statusbit 3
    f.t.press(8, 5);
    f.t.runTastaturTakte(5 * D);
    EXPECT_TRUE(f.t.ledLock());
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    f.t.press(7, 5);
    f.t.runTastaturTakte(8 * D);
    EXPECT_EQ(f.bytes, (Bytes{0xE8, 0x41}));
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    // nächster Druck schaltet erst beim Loslassen aus
    f.t.press(8, 5);
    f.t.runTastaturTakte(5 * D);
    f.t.releaseAll();
    f.t.runTastaturTakte(3 * D);
    EXPECT_FALSE(f.t.ledLock());
}

TEST(Tastatur1715, TippeZeichenkette) {
    Prueffeld f;
    f.ruhe();
    f.t.tippe("Ab 1!\r");
    // A (Shift), b, Leer, 1, ! (Shift), Wagenrücklauf
    EXPECT_EQ(f.bytes, (Bytes{0xE2, 0x41, 0xE0, 0x62, 0xE0, 0x20, 0xE0, 0x31, 0xE2, 0x21, 0xE0, 0x9E}));
}

TEST(Tastatur1715, TastenZuordnung) {
    Tastatur1715::Taste k;
    ASSERT_TRUE(Tastatur1715::tasteFuer('a', k));
    EXPECT_EQ(k.spalte, 7); EXPECT_EQ(k.zeile, 5); EXPECT_FALSE(k.shift);
    ASSERT_TRUE(Tastatur1715::tasteFuer('A', k));
    EXPECT_EQ(k.spalte, 7); EXPECT_EQ(k.zeile, 5); EXPECT_TRUE(k.shift);
    ASSERT_TRUE(Tastatur1715::tasteFuer('$', k));
    EXPECT_EQ(k.spalte, 0); EXPECT_EQ(k.zeile, 2); EXPECT_TRUE(k.shift);
    ASSERT_TRUE(Tastatur1715::tasteFuer('\x1b', k));
    EXPECT_FALSE(k.shift);
    EXPECT_FALSE(Tastatur1715::tasteFuer('\x01', k));
    Tastatur1715 t;
    EXPECT_FALSE(t.pressKeyFor('\x01'));
}

// Die ganze Zeichentabelle gegen das ROM: jede Taste unverschoben und mit Shift
// (Tabelle in tastatur1715.cpp ← doc/pc1715/tastatur.md §6)
TEST(Tastatur1715, ZeichentabelleStimmtMitDemRomUeberein) {
    int geprueft = 0;
    for (int sp = 0; sp < 13; sp++)
        for (int ze = 0; ze < 8; ze++) {
            if (sp == 8 && ze != 2) continue;  // Sondertasten
            for (int shift = 0; shift < 2; shift++) {
                Prueffeld f;
                f.ruhe();
                if (shift) { f.t.press(8, 1); f.t.runTastaturTakte(4 * D); }
                f.t.press(sp, ze);
                f.t.runTastaturTakte(8 * D);
                if (f.bytes.empty()) continue;  // unbelegte Stellen senden nichts
                ASSERT_EQ(f.bytes.size(), 2u) << sp << "," << ze;
                // Rückwärts: erzeugt unser Zeichen für diesen Code dieselbe Position?
                const uint8_t code = f.bytes[1];
                if (code >= 0x20 && code < 0x7F) {
                    Tastatur1715::Taste k;
                    ASSERT_TRUE(Tastatur1715::tasteFuer(char(code), k)) << sp << "," << ze << " code " << int(code);
                    Prueffeld g;
                    g.ruhe();
                    g.t.tippe(std::string(1, char(code)));
                    EXPECT_EQ(g.bytes[1], code) << sp << "," << ze;
                    geprueft++;
                }
            }
        }
    EXPECT_GT(geprueft, 90);
}

TEST(Tastatur1715, TaktverhaeltnisZumRechner) {
    Tastatur1715 t;
    t.run(2'457'600);  // eine Rechnersekunde
    EXPECT_NEAR(double(t.tastaturTakte()), 700'000.0, 20.0);
    EXPECT_EQ(t.hostTakte(), 2'457'600u);
    Tastatur1715 schnell(nullptr, 1'400'000);
    schnell.run(2'457'600);
    EXPECT_NEAR(double(schnell.tastaturTakte()), 1'400'000.0, 20.0);
}

TEST(Tastatur1715, SaveStateRoundtrip) {
    Prueffeld a;
    a.ruhe();
    a.t.press(7, 5);
    a.t.runTastaturTakte(3 * D + 777);  // mitten in der Entprellung
    std::vector<uint8_t> blob;
    a.t.serialize(blob);

    Prueffeld b;
    const uint8_t* p = blob.data();
    ASSERT_TRUE(b.t.deserialize(p, blob.data() + blob.size()));
    EXPECT_EQ(p, blob.data() + blob.size());
    a.t.runTastaturTakte(6 * D);
    b.t.runTastaturTakte(6 * D);
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_FALSE(b.bytes.empty());
    // abgeschnitten
    const uint8_t* q = blob.data();
    EXPECT_FALSE(b.t.deserialize(q, blob.data() + 10));
}
