/**
 * @file test_mc80_vis2a.cpp
 * @brief VIS 2a des MC80.3x (AP M3): Bildpunkte ueber `OUT (C),r` (Y auf A8–A15), Bildhaelfte,
 *        IN-Register (Polaritaet [?]), Bildkommandos Loeschen/Aus/Ein/Rollen, Anzeige mit Y = 0
 *        unten.  Quellen: doc/mc80/hardware_3x.md §5, egos.md §1.8/§1.9.
 */

#include <gtest/gtest.h>
#include "core/cards/mc80/vis2a.h"

namespace {

struct Vis {
    K1520Bus bus;
    Mc80Vis2a vis{bus};
    Vis() { vis.attachToBus(bus); vis.powerOn(); }
    /// `OUT (C),r` mit B = @p b, C = @p c, r = @p r
    void out(uint8_t c, uint8_t b, uint8_t r) { bus.ioWrite(static_cast<uint16_t>(b << 8 | c), r); }
    uint8_t in(uint8_t c, uint8_t b) { return bus.ioRead(static_cast<uint16_t>(b << 8 | c)); }
    uint8_t fb(int x, int zeile_von_oben) const { return vis.framebuffer()[zeile_von_oben * 512 + x]; }
};

TEST(Mc80Vis2a, NachDemEinschaltenDunkelUndEin) {
    Vis v;
    EXPECT_TRUE(v.vis.bildEin());
    EXPECT_EQ(v.vis.rollzaehler(), 0);
    for (int i = 0; i < 512 * 256; ++i) ASSERT_EQ(v.vis.framebuffer()[i], 0);
}

TEST(Mc80Vis2a, PunktHellUndDunkelMitYAusDemHochbyte) {
    Vis v;
    v.out(0xC1, 0x10, 0x20);   // Funktion 1: hell, Y = 10H, X = 20H, linke Haelfte
    EXPECT_TRUE(v.vis.punkt(0x20, 0x10));
    EXPECT_FALSE(v.vis.punkt(0x20, 0x11));
    // Y = 0 ist unten: Y = 10H steht in der Anzeigezeile 255 − 16 = 239
    EXPECT_EQ(v.fb(0x20, 239), 0xFF);
    v.out(0xC0, 0x10, 0x20);   // Funktion 0: dunkel
    EXPECT_FALSE(v.vis.punkt(0x20, 0x10));
    EXPECT_EQ(v.fb(0x20, 239), 0x00);
}

TEST(Mc80Vis2a, Bit3WaehltDieRechteBildhaelfte) {
    Vis v;
    v.out(0xC9, 0x00, 0x05);   // rechte Haelfte, X = 5 → Bildspalte 261
    EXPECT_TRUE(v.vis.punkt(256 + 5, 0));
    EXPECT_FALSE(v.vis.punkt(5, 0));
    EXPECT_EQ(v.fb(261, 255), 0xFF);
}

TEST(Mc80Vis2a, NurDieKarteAufC0BisCFAntwortet) {
    Vis v;
    v.out(0xB1, 0x00, 0x00);
    v.out(0xD1, 0x00, 0x00);
    EXPECT_FALSE(v.vis.punkt(0, 0));
    v.out(0xCF, 0x00, 0x00);   // Funktion 7: nicht belegt
    EXPECT_FALSE(v.vis.punkt(256, 0));
}

// [?] Polaritaet: Festlegung (vis2a.h) — Funktion 3 liefert den Bildpunkt, 2 den negierten;
// nur so ist die Routine 4C40H der MC80.31E-Segmente ein „Bild invertieren“.
TEST(Mc80Vis2a, LesenFunktion3LiefertDenBildpunkt) {
    Vis v;
    v.out(0xC1, 0x40, 0x07);
    v.out(0xC3, 0x40, 0x07);
    EXPECT_EQ(v.in(0xC3, 0x40) & 1, 1);
    v.out(0xC2, 0x40, 0x07);
    EXPECT_EQ(v.in(0xC3, 0x40) & 1, 0);
    v.out(0xC3, 0x41, 0x07);   // dunkler Nachbar
    EXPECT_EQ(v.in(0xC3, 0x41), 0xFE);
    // IN an jedem Port der Karte; die Ausgabe hat den Punkt nicht veraendert
    EXPECT_EQ(v.in(0xC0, 0x00) & 1, 0);
    EXPECT_TRUE(v.vis.punkt(7, 0x40));
}

TEST(Mc80Vis2a, InvertierenWieDieSegmentRoutine4C40) {
    Vis v;
    v.out(0xC1, 0x00, 0x00);
    // seg_20_4000-h.bin 4C46H ff.: OUT C3 ×2, IN, Bit 0 = 1 → dunkel, sonst hell
    for (int y : {0, 1}) {
        for (int x : {0, 1}) {
            v.out(0xC3, static_cast<uint8_t>(y), static_cast<uint8_t>(x));
            v.out(0xC3, static_cast<uint8_t>(y), static_cast<uint8_t>(x));
            const bool b0 = v.in(0xC3, static_cast<uint8_t>(y)) & 1;
            v.out(b0 ? 0xC0 : 0xC1, static_cast<uint8_t>(y), static_cast<uint8_t>(x));
        }
    }
    EXPECT_FALSE(v.vis.punkt(0, 0));
    EXPECT_TRUE(v.vis.punkt(1, 0));
    EXPECT_TRUE(v.vis.punkt(0, 1));
}

TEST(Mc80Vis2a, LoeschenDunkelUndHellSetztDenRollzaehlerZurueck) {
    Vis v;
    v.out(0xC4, 0x1A, 0);      // rollen, R = −10
    EXPECT_EQ(v.vis.rollzaehler(), 0xF6);
    v.out(0xC5, 0x00, 0);      // B = 00H, C0 = 1: hell loeschen
    EXPECT_EQ(v.vis.rollzaehler(), 0);
    EXPECT_TRUE(v.vis.punkt(100, 100));
    EXPECT_EQ(v.fb(0, 0), 0xFF);
    v.out(0xCC, 0x04, 0);      // Bit 3 egal, B = 04H: dunkel loeschen (n & 3 = 0)
    EXPECT_FALSE(v.vis.punkt(100, 100));
    EXPECT_EQ(v.fb(511, 255), 0x00);
}

TEST(Mc80Vis2a, BildAusUndEinLaesstDenSpeicherStehen) {
    Vis v;
    v.out(0xC1, 0x00, 0x00);
    v.out(0xC4, 0x01, 0);      // aus
    EXPECT_FALSE(v.vis.bildEin());
    EXPECT_EQ(v.fb(0, 255), 0x00);
    EXPECT_TRUE(v.vis.punkt(0, 0));
    v.out(0xC5, 0x06, 0);      // ein (n & 3 = 2)
    EXPECT_EQ(v.fb(0, 255), 0xFF);
}

TEST(Mc80Vis2a, RollenNachObenVerschiebtDieAnzeigeUndFuelltUnten) {
    Vis v;
    v.out(0xC1, 0x0A, 0x03);   // Punkt in Y = 10 → Anzeigezeile 245
    v.out(0xC1, 0xFF, 0x04);   // Punkt in der obersten Zeile
    EXPECT_EQ(v.fb(3, 245), 0xFF);
    v.out(0xC4, 0x1A, 0);      // 10 Zeilen nach oben, Hintergrund dunkel (EGOS ROL 0050H)
    EXPECT_EQ(v.vis.rollzaehler(), 0xF6);
    EXPECT_EQ(v.fb(3, 235), 0xFF) << "Inhalt 10 Zeilen hoeher";
    EXPECT_EQ(v.fb(3, 245), 0x00);
    // die oberste Zeile ist unten wieder erschienen und geloescht
    EXPECT_FALSE(v.vis.punkt(4, 0xFF));
    for (int x = 0; x < 512; ++x) ASSERT_EQ(v.fb(x, 255), 0);
    // EGOS rechnet Y = F8H − 10·Zeile + R: Zeile 0 liegt jetzt bei Y = EEH, oberste Anzeigezeile
    v.out(0xC1, static_cast<uint8_t>(0xF8 + 0xF6 + 7), 0x00);
    EXPECT_EQ(v.fb(0, 0), 0xFF);
}

TEST(Mc80Vis2a, RollenNachUntenHellUndZaehlerLoeschen) {
    Vis v;
    v.out(0xC1, 0x0A, 0x00);   // Y = 10 → Anzeigezeile 245
    v.out(0xC5, 0x2A, 0);      // 10 nach unten, neue Zeilen oben hell
    EXPECT_EQ(v.vis.rollzaehler(), 10);
    for (int x = 0; x < 512; ++x) ASSERT_EQ(v.fb(x, 0), 0xFF);
    EXPECT_EQ(v.fb(0, 255), 0xFF) << "10 Zeilen nach unten gewandert";
    EXPECT_EQ(v.fb(1, 255), 0x00);
    // die alte unterste Zeile (Y = 0) ist unten hinausgerollt und oben hell wiedergekommen
    EXPECT_TRUE(v.vis.punkt(300, 0));
    v.out(0xC4, 0x20, 0);      // n = 0: Rollzaehler loeschen
    EXPECT_EQ(v.vis.rollzaehler(), 0);
    v.out(0xC4, 0x10, 0);
    EXPECT_EQ(v.vis.rollzaehler(), 0);
    v.out(0xC4, 0x50, 0);      // Lichtstift: ohne Wirkung
    EXPECT_EQ(v.vis.rollzaehler(), 0);
}

TEST(Mc80Vis2a, RevisionUndDirtyFolgenJederAenderung) {
    Vis v;
    v.vis.clearDirty();
    const auto r = v.vis.revision();
    v.out(0xC0, 0x00, 0x00);   // schon dunkel: keine Aenderung
    EXPECT_FALSE(v.vis.dirty());
    EXPECT_EQ(v.vis.revision(), r);
    v.out(0xC1, 0x00, 0x00);
    EXPECT_TRUE(v.vis.dirty());
    EXPECT_GT(v.vis.revision(), r);
}

}  // namespace
