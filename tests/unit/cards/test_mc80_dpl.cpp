/**
 * @file test_mc80_dpl.cpp
 * @brief Displaylogik DPL des MC80.2x (AP M11): Tore, Zeilenautomat (Zeichentakt, Ruecklauf,
 *        Analogbahn), Bild aus den PIO-Ausgaengen, Tastenmatrix, BREAK, Ersatz-Zeichengenerator.
 *        Zeitannahmen [?] stehen in core/cards/mc80/dpl.h und werden hier festgehalten.
 */

#include <gtest/gtest.h>
#include <set>
#include <string>

#include "core/cards/mc80/dpl.h"

namespace {

constexpr int PERIODE = 159;   // 64,5 µs bei 2,4576 MHz = 158,5 Takte (auf Takte gerundet)

void takte(Mc80Dpl& d, int n)
{
    while (n > 0) {
        const int t = n < 4 ? n : 4;
        d.clockTick(t);
        n -= t;
    }
}

/// BSYS-Einrichtung (TII/INI): K0 Zaehler mit Interrupt ZK 1, K2 Zaehler ZK 33, K1 Ruecklauf D0H,
/// PIO A/B Ausgabe.
void einrichten(Mc80Dpl& d)
{
    d.setIEI(true);
    d.ioWrite(0xF0, 0x00);                          // Vektorbasis
    d.ioWrite(0xF0, 0xC7); d.ioWrite(0xF0, 0x01);
    d.ioWrite(0xF2, 0xC7); d.ioWrite(0xF2, 0x21);
    d.ioWrite(0xF1, 0x47); d.ioWrite(0xF1, 0xD0);
    d.ioWrite(0xF5, 0x0F);
    d.ioWrite(0xF7, 0x0F);
}

/// Bis zum naechsten Zeichentakt (hoechstens @p max Takte).
bool bisZeichentakt(Mc80Dpl& d, int max = 20'000)
{
    const uint64_t z0 = d.zeichentakte();
    for (int i = 0; i < max; ++i) {
        d.clockTick(1);
        if (d.zeichentakte() != z0) return true;
    }
    return false;
}

}  // namespace

TEST(Mc80Dpl, ToreF0BisF7)
{
    Mc80Dpl d;
    K1520Bus bus;
    d.attachToBus(bus);
    for (uint8_t p = 0xF0; p <= 0xF7; ++p) EXPECT_EQ(bus.ioOwner(p), &d) << std::hex << int(p);
    EXPECT_EQ(bus.ioOwner(0xEF), nullptr);
    EXPECT_EQ(bus.ioOwner(0xF8), nullptr);
    bus.ioWrite(0xF2, 0x47);
    bus.ioWrite(0xF2, 0x21);
    EXPECT_EQ(bus.ioRead(0xF2), 0x21);              // CTC K2 liest den Zaehlerstand
}

TEST(Mc80Dpl, ZeichentaktUndRuecklauf)
{
    Mc80Dpl d;
    einrichten(d);
    // Nach /RESET Ruecklauf; K1 (Φ/4, ZK D0H) beendet ihn nach 832 Takten.
    takte(d, 800);
    EXPECT_EQ(d.zeichentakte(), 0u);
    ASSERT_TRUE(bisZeichentakt(d, 300));
    // Abstand zweier Zeichentakte = 64,5 µs.
    const uint64_t z = d.zeichentakte();
    takte(d, PERIODE - 3);
    EXPECT_EQ(d.zeichentakte(), z);
    takte(d, 4);
    EXPECT_EQ(d.zeichentakte(), z + 1);
    // Jeder Zeichentakt fordert K0 (DPI) an.
    EXPECT_TRUE(d.ctc().debugState().ch[0].intPending);
    // Nach 33 Takten ZC/TO2 (TSI angefordert) und Ruecklauf: 832 Takte kein Zeichentakt.
    while (!d.ctc().debugState().ch[2].intPending) takte(d, 1);
    const uint64_t n = d.zeichentakte();
    takte(d, 800);
    EXPECT_EQ(d.zeichentakte(), n);
    takte(d, 32 + PERIODE + 2);
    EXPECT_EQ(d.zeichentakte(), n + 1);
}

TEST(Mc80Dpl, ZeileAusDenPioAusgaengen)
{
    Mc80Dpl d;
    einrichten(d);
    takte(d, 900);                                  // Ruecklauf vorbei
    // TSI: Zeilenlage auf PIO B (Zeile 2 → BWS-Adresse 40H → CPL = BFH), Leerzeichen auf PIO A.
    d.ioWrite(0xF6, 0xBF);
    d.ioWrite(0xF4, 0x20);
    ASSERT_TRUE(bisZeichentakt(d));                 // Schritt 0: Leerzeichen bei Strahlvorlauf
    const std::string text = "HALLO MC80";
    for (char c : text) {                           // DPI: naechstes Zeichen nach jedem Takt
        d.ioWrite(0xF4, static_cast<uint8_t>(c >= 0x40 ? c - 0x40 : c));
        ASSERT_TRUE(bisZeichentakt(d));
    }
    for (size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(d.zeichen(2, static_cast<int>(i)), text[i]) << i;
        EXPECT_EQ(d.zeichenAusBild(2, static_cast<int>(i)), text[i]) << i;   // Mustervergleich
    }
    EXPECT_EQ(d.zelle(2, 0), 0x08);                 // 'H' = 48H → 6-Bit-Code 08H
    EXPECT_EQ(d.zelle(0, 0), Mc80Dpl::NIE);         // nie gezeichnet
    EXPECT_EQ(d.zeichen(0, 0), ' ');
    // Framebuffer: 'H' in Zeile 2, Spalte 0 → Zelle ab x = 16, y = 64; linke Glyphenspalte voll.
    const uint8_t* fb = d.framebuffer();
    EXPECT_EQ(fb[(64 + 9) * Mc80Dpl::BREITE + 16 + 3], 0xFF);
    EXPECT_EQ(fb[(64 + 9) * Mc80Dpl::BREITE + 16 + 3 + 4], 0x00);
    EXPECT_EQ(Mc80Dpl::BREITE, 528);
    EXPECT_EQ(Mc80Dpl::HOEHE, 256);
}

TEST(Mc80Dpl, AnalogbahnErsetztDieFolgendeZeile)
{
    Mc80Dpl d;
    einrichten(d);
    takte(d, 900);
    d.ioWrite(0xF6, 0xFF);                          // Textzeile 0
    d.ioWrite(0xF4, 0x20);
    while (!d.ctc().debugState().ch[2].intPending) takte(d, 1);   // TSI → Ruecklauf
    // ANL: Z-Signal analog, erste Position, dann auf TI1 (Ende des Ruecklaufs) warten.
    d.ioWrite(0xF4, 0x80);
    d.ioWrite(0xF6, 0x10);
    d.ioWrite(0xF1, 0xC1);
    const uint64_t b0 = d.bahnen();
    while (!d.ctc().debugState().ch[1].intPending) takte(d, 1);
    // OTIR: alle 21 Takte ein Wert (Rampe 10H, 11H, …).
    for (int i = 0; i < 254; ++i) {
        d.ioWrite(0xF6, static_cast<uint8_t>(0x11 + i / 2));
        takte(d, 21);
    }
    d.ioWrite(0xF4, 0x20);                          // zurueck auf Text (TI0)
    EXPECT_EQ(d.bahnen(), b0 + 1);
    ASSERT_TRUE(d.analogZeile(1));
    EXPECT_FALSE(d.analogZeile(0));
    const auto& k = d.kurve(1);
    ASSERT_GE(k.size(), 240u);
    EXPECT_LE(k.size(), 252u);                      // die letzten Werte fallen rechts heraus [?]
    EXPECT_EQ(k.front().x, 0);
    EXPECT_EQ(k.front().y, 0x10);
    for (size_t i = 1; i < k.size(); ++i) EXPECT_GE(k[i].x, k[i - 1].x);   // Treppenzug
    EXPECT_LT(k.back().x, Mc80Dpl::BREITE);
    // Bildpunkt des ersten Werts: y = 10H → Bildzeile 255 − 16.
    EXPECT_EQ(d.framebuffer()[(255 - 0x10) * Mc80Dpl::BREITE + 0], 0xFF);
    EXPECT_EQ(d.zeichen(1, 0), ' ');                // Analogzeile hat keinen Text
}

TEST(Mc80Dpl, TastenmatrixUeberPioB)
{
    Mc80Dpl d;
    einrichten(d);
    d.setTaste(0x11, true);                         // 'A': Zeile 10H, Bit 1
    d.setTaste(0x37, true);                         // UC: Zeile 01H, Bit 7
    EXPECT_TRUE(d.taste(0x11));
    d.ioWrite(0xF7, 0xFF);                          // PIO B Betriebsart 3, alles Eingang
    d.ioWrite(0xF7, 0xFF);
    d.ioWrite(0xF4, 0x10);
    EXPECT_EQ(d.ioRead(0xF6), 0x02);
    d.ioWrite(0xF4, 0x01);
    EXPECT_EQ(d.ioRead(0xF6), 0x80);
    d.ioWrite(0xF4, 0x40);
    EXPECT_EQ(d.ioRead(0xF6), 0x00);
    d.ioWrite(0xF4, 0x11);                          // zwei Zeilen zugleich: ODER
    EXPECT_EQ(d.ioRead(0xF6), 0x82);
    d.alleTastenLos();
    EXPECT_EQ(d.ioRead(0xF6), 0x00);
}

TEST(Mc80Dpl, BreakTasteZaehltK3)
{
    Mc80Dpl d;
    einrichten(d);
    d.ioWrite(0xF3, 0xC7);                          // [HB VI-6]: OUT F3H,C7H / OUT F3H,01H
    d.ioWrite(0xF3, 0x01);
    EXPECT_FALSE(d.ctc().debugState().ch[3].intPending);
    d.breakTaste();
    EXPECT_TRUE(d.ctc().debugState().ch[3].intPending);
}

TEST(Mc80Dpl, ErsatzZeichengeneratorEindeutig)
{
    // 64 Codes, alle Bilder verschieden (sonst taugt der Mustervergleich nicht); Leerzeichen leer.
    std::set<std::string> bilder;
    for (int c = 0; c < 64; ++c) {
        const uint8_t* g = Mc80Dpl::glyphe(static_cast<uint8_t>(c));
        bilder.insert(std::string(reinterpret_cast<const char*>(g), 5));
    }
    EXPECT_EQ(bilder.size(), 64u);
    const uint8_t* leer = Mc80Dpl::glyphe(0x20);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(leer[i], 0);
    EXPECT_EQ(Mc80Dpl::glyphe(0x01), Mc80Dpl::glyphe(0x41));   // 6 Bit: Bit 6 ohne Wirkung
}

TEST(Mc80Dpl, RetiGehoertDemKanalInBedienung)
{
    // TSI (K2) in Bedienung, K0 fordert schon wieder an: retiAnnehmen() loescht K2.
    Mc80Dpl d;
    einrichten(d);
    takte(d, 900);
    while (!d.ctc().debugState().ch[2].intPending) {
        takte(d, 1);
        if (d.ctc().debugState().ch[0].intPending) { d.getVector(); d.retiAnnehmen(); }
    }
    while (d.ctc().debugState().ch[0].intPending) { d.getVector(); d.retiAnnehmen(); }
    EXPECT_EQ(d.getVector(), 0x04);                 // TSI
    d.ctc().clkTrg(0, true);                        // K0 fordert an, waehrend der TSI laeuft
    d.ctc().clkTrg(0, false);
    ASSERT_TRUE(d.ctc().debugState().ch[0].intPending);
    EXPECT_TRUE(d.retiAnnehmen());
    EXPECT_TRUE(d.ctc().debugState().ch[0].intPending);
    EXPECT_FALSE(d.ctc().inBedienung());
    EXPECT_FALSE(d.retiAnnehmen());                 // nichts mehr in Bedienung
}
