/**
 * @file test_pio_handshake.cpp
 * @brief Wächter für die additive Handshake-/Pinpegel-Erweiterung des Z80PIO
 *        (P8000 P5c, doc/design/25_p8000.md §10.3): RDY-Ausgänge, /STB-Flanken
 *        Modus 0/1/2 nach Zilog-Datenblatt, Pinpegel mit Pull-up und Rücklesen.
 *        Das Vorgabeverhalten (setASTB/setBSTB/portAWrite) deckt test_pio.cpp ab.
 */
#include <gtest/gtest.h>
#include "core/primitives/z80_pio.h"

namespace {
constexpr uint8_t A_DAT = 0, A_CTL = 1, B_DAT = 2, B_CTL = 3;

void modus(Z80PIO& p, uint8_t ctl, int m) { p.ioWrite(ctl, static_cast<uint8_t>((m << 6) | 0x0F)); }
void ieAn(Z80PIO& p, uint8_t ctl, uint8_t vec) {
    p.ioWrite(ctl, vec);
    p.ioWrite(ctl, 0x83);
}
struct Fix {
    Z80PIO pio{"T"};
    Fix() { pio.setIEI(true); }
};
}  // namespace

TEST(PioHandshake, ResetUndModuswortSetzenRdy) {
    Fix f;
    EXPECT_FALSE(f.pio.ardy());
    modus(f.pio, A_CTL, 1);
    EXPECT_TRUE(f.pio.ardy()) << "Modus 1: Eingaberegister leer ⇒ RDY H";
    modus(f.pio, A_CTL, 0);
    EXPECT_FALSE(f.pio.ardy()) << "Modus 0: noch nichts geschrieben ⇒ RDY L";
    modus(f.pio, B_CTL, 3);
    EXPECT_FALSE(f.pio.brdy());
    modus(f.pio, A_CTL, 1);
    f.pio.reset();
    EXPECT_FALSE(f.pio.ardy());
}

TEST(PioHandshake, Modus0_SchreibenRdyHoch_SteigendeStbFlankeRdyTiefUndInterrupt) {
    Fix f;
    std::vector<bool> flanken;
    f.pio.setARdyCallback([&](bool v) { flanken.push_back(v); });
    modus(f.pio, A_CTL, 0);
    ieAn(f.pio, A_CTL, 0x20);
    f.pio.ioWrite(A_DAT, 0x5A);
    EXPECT_TRUE(f.pio.ardy());
    EXPECT_FALSE(f.pio.hasInterrupt());
    f.pio.setStbA(false);                       // fallende Flanke: nichts
    EXPECT_TRUE(f.pio.ardy());
    EXPECT_FALSE(f.pio.hasInterrupt());
    f.pio.setStbA(true);                        // steigend: Datum übernommen
    EXPECT_FALSE(f.pio.ardy());
    EXPECT_TRUE(f.pio.hasInterrupt());
    EXPECT_EQ(f.pio.getVector(), 0x20);
    ASSERT_EQ(flanken.size(), 2u);              // genau H, dann L
    EXPECT_TRUE(flanken[0]);
    EXPECT_FALSE(flanken[1]);
}

TEST(PioHandshake, Modus0_StbOhneDatumIstWirkungslos) {
    Fix f;
    modus(f.pio, A_CTL, 0);
    ieAn(f.pio, A_CTL, 0x20);
    f.pio.setStbA(false);
    f.pio.setStbA(true);
    EXPECT_FALSE(f.pio.hasInterrupt());
}

TEST(PioHandshake, Modus1_StbSteigendUebernimmtPinsRdyTiefBisCpuLiest) {
    Fix f;
    modus(f.pio, A_CTL, 1);
    ieAn(f.pio, A_CTL, 0x30);
    f.pio.setExternA(0xA7);
    f.pio.setStbA(false);
    EXPECT_TRUE(f.pio.ardy());
    EXPECT_FALSE(f.pio.hasInterrupt()) << "Übernahme/Interrupt erst an der steigenden Flanke";
    f.pio.setExternA(0x3C);                     // Daten dürfen sich bis zur Flanke ändern
    f.pio.setStbA(true);
    EXPECT_FALSE(f.pio.ardy());
    EXPECT_TRUE(f.pio.hasInterrupt());
    EXPECT_EQ(f.pio.getVector(), 0x30);
    EXPECT_EQ(f.pio.ioRead(A_DAT), 0x3C);
    EXPECT_TRUE(f.pio.ardy()) << "CPU-Lesen ⇒ RDY H";
    // Solange das Register voll ist, übernimmt ein weiterer Strobe nichts
    f.pio.setExternA(0x11);
    f.pio.setStbA(false);
    f.pio.setStbA(true);                        // RDY war H: übernimmt 0x11
    EXPECT_EQ(f.pio.ioRead(A_DAT), 0x11);
    f.pio.setExternA(0x22);
    f.pio.setStbA(false);
    f.pio.setStbA(true);                        // RDY ist jetzt H nach Lesen ⇒ übernimmt
    f.pio.setExternA(0x33);
    f.pio.setStbA(false);
    f.pio.setStbA(true);                        // RDY L (nicht gelesen) ⇒ ignoriert
    EXPECT_EQ(f.pio.ioRead(A_DAT), 0x22);
}

TEST(PioHandshake, Modus1_PortB) {
    Fix f;
    modus(f.pio, B_CTL, 1);
    ieAn(f.pio, B_CTL, 0x40);
    EXPECT_TRUE(f.pio.brdy());
    f.pio.setExternB(0x81);
    f.pio.setStbB(false);
    f.pio.setStbB(true);
    EXPECT_FALSE(f.pio.brdy());
    EXPECT_TRUE(f.pio.hasInterrupt());
    EXPECT_EQ(f.pio.getVector(), 0x40);
    EXPECT_EQ(f.pio.ioRead(B_DAT), 0x81);
    EXPECT_TRUE(f.pio.brdy());
}

TEST(PioHandshake, Modus2_AusgabeUeberARdyAstb_EingabeUeberBRdyBstb) {
    Fix f;
    modus(f.pio, A_CTL, 2);
    ieAn(f.pio, A_CTL, 0x10);
    ieAn(f.pio, B_CTL, 0x12);
    f.pio.ioWrite(B_CTL, 0xFF);                 // B: Modus 3 (erzwungen), alle Bits Eingang
    EXPECT_FALSE(f.pio.ardy());
    EXPECT_TRUE(f.pio.brdy());
    f.pio.ioWrite(A_DAT, 0xC3);
    EXPECT_TRUE(f.pio.ardy());
    f.pio.setStbA(false);
    EXPECT_EQ(f.pio.pinsA(0x00), 0xC3) << "Ausgabe nur bei /ASTB = L auf den Pins";
    f.pio.setStbA(true);
    EXPECT_EQ(f.pio.pinsA(0x00), 0x00);
    EXPECT_FALSE(f.pio.ardy());
    EXPECT_TRUE(f.pio.hasInterrupt());
    EXPECT_EQ(f.pio.getVector(), 0x10);
    // Eingabe
    f.pio.setExternA(0x6E);
    f.pio.setStbB(false);
    f.pio.setStbB(true);
    EXPECT_FALSE(f.pio.brdy());
    EXPECT_EQ(f.pio.ioRead(A_DAT), 0x6E);
    EXPECT_TRUE(f.pio.brdy());
}

TEST(PioHandshake, Modus3_StbOhneWirkung) {
    Fix f;
    modus(f.pio, A_CTL, 3);
    f.pio.ioWrite(A_CTL, 0xFF);
    ieAn(f.pio, A_CTL, 0x20);
    f.pio.setStbA(false);
    f.pio.setStbA(true);
    EXPECT_FALSE(f.pio.ardy());
    EXPECT_FALSE(f.pio.hasInterrupt());
}

TEST(PioHandshake, PinpegelOffeneEingaengeMitPullupUndRuecklesen) {
    Fix f;
    modus(f.pio, B_CTL, 3);
    f.pio.ioWrite(B_CTL, 0x0F);                 // B0–3 Eingang, B4–7 Ausgang
    f.pio.ioWrite(B_DAT, 0xA0);
    EXPECT_EQ(f.pio.pinsB(0xFF), 0xAF) << "offene Eingänge lesen H (Pull-up), Ausgänge ihr Register";
    EXPECT_EQ(f.pio.pinsB(0x00), 0xA0) << "ohne Pull-up lesen offene Eingänge L";
    f.pio.setExternB(0x04, 0x0F);               // Treiber zieht B0–3 auf 0100
    EXPECT_EQ(f.pio.pinsB(0xFF), 0xA4);
    EXPECT_EQ(f.pio.ioRead(B_DAT), 0xA4) << "CPU liest Eingänge von den Pins, Ausgänge zurück";
    f.pio.setExternB(0x00, 0x00);               // Treiber weg ⇒ wieder Pull-up
    EXPECT_EQ(f.pio.ioRead(B_DAT) & 0x0F, 0x0F);
}

TEST(PioHandshake, Modus3_PinaenderungLoestInterruptAus) {
    Fix f;
    modus(f.pio, B_CTL, 3);
    f.pio.ioWrite(B_CTL, 0xFF);
    f.pio.setExternB(0x00, 0xFF);               // alles L
    f.pio.ioWrite(B_CTL, 0x30);                 // Vektor 30
    f.pio.ioWrite(B_CTL, 0xB7);                 // IE, ODER, high, Maske folgt
    f.pio.ioWrite(B_CTL, 0xFE);                 // nur Bit 0 beteiligt
    EXPECT_FALSE(f.pio.hasInterrupt());
    f.pio.setExternB(0x01, 0xFF);
    EXPECT_TRUE(f.pio.hasInterrupt());
}

TEST(PioHandshake, OhneRueckrufUndStbVerhaeltSichDieAltenSchnittstelleWieBisher) {
    Fix f;
    modus(f.pio, A_CTL, 1);
    ieAn(f.pio, A_CTL, 0x20);
    f.pio.setASTB(false);                       // alte Schnittstelle: fallende Flanke ⇒ pending
    EXPECT_TRUE(f.pio.hasInterrupt());
}

TEST(PioHandshake, HandshakeZustandSichertUndLaedt) {
    Fix f;
    modus(f.pio, A_CTL, 0);
    f.pio.ioWrite(A_DAT, 1);
    f.pio.setExternB(0x55, 0xF0);
    f.pio.setPullupsB(0x0F);
    std::vector<uint8_t> v;
    f.pio.serializeHandshake(v);
    Fix g;
    const uint8_t* p = v.data();
    ASSERT_TRUE(g.pio.deserializeHandshake(p, v.data() + v.size()));
    EXPECT_TRUE(g.pio.ardy());
    EXPECT_EQ(g.pio.pinsB(0x0F), f.pio.pinsB(0x0F));
}
