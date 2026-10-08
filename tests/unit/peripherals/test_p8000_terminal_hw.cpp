/**
 * @file test_p8000_terminal_hw.cpp
 * @brief Terminalrechner Typ 2 mit Original-Firmware P8T 5.0 (AP P20a, Entwurf 28):
 *        Einschaltbild (BWS, Zellen, Pixel), 8275-Programmierung, Zeilen-DMA, Strobes,
 *        Watchdog, Tastatur-Schieberegister, serielle Leitung, Save-State.
 */
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "core/peripherals/p8000_terminal_hw/terminal_hw.h"
#include "core/peripherals/p8000_terminal_hw/rom_p8t.h"

using namespace k1520::p8000;

namespace {

constexpr uint64_t MS = P8000TerminalHw::Z8_HZ / 1000;

std::string zl(const P8000TerminalHw& t, int z) {
    std::string s = t.text(z);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

/// Eingeschaltet, Tastatur hat AA gemeldet, Meldung steht.
void bereit(P8000TerminalHw& t) {
    t.laufeBis(t.takte() + 800 * MS);
    t.tastaturByte(0xAA);
    t.laufeBis(t.takte() + 100 * MS);
}

}  // namespace

TEST(P8000TerminalHw, EinschaltmeldungImBildspeicher)
{
    P8000TerminalHw t;
    bereit(t);
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
    for (int z = 1; z < 24; ++z) EXPECT_EQ(zl(t, z), "") << z;
    EXPECT_EQ(t.cursorZeile(), 1);
    EXPECT_EQ(t.cursorSpalte(), 0);
    EXPECT_EQ(t.watchdogResets(), 0u);
}

TEST(P8000TerminalHw, Programmierung8275WieFirmware)
{
    P8000TerminalHw t;
    t.crtProtokollieren = true;
    t.einschalten();
    bereit(t);
    ASSERT_GE(t.crtProtokoll.size(), 10u);
    // Reset 00 + 4F 97 CC 5A, Preset E0 E0, Start 20, Load Cursor 80 + Spalte + Zeile
    const uint8_t soll[][2] = {{1, 0x00}, {0, 0x4F}, {0, 0x97}, {0, 0xCC}, {0, 0x5A}, {1, 0xE0}, {1, 0xE0}};
    for (size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(t.crtProtokoll[i].a0, soll[i][0] != 0) << i;
        EXPECT_EQ(t.crtProtokoll[i].wert, soll[i][1]) << i;
        EXPECT_FALSE(t.crtProtokoll[i].lesen);
    }
    const I8275& c = t.crt();
    EXPECT_EQ(c.cols(), 80);
    EXPECT_EQ(c.rows(), 24);
    EXPECT_EQ(c.lineCount(), 13);
    EXPECT_EQ(c.underlineLine(), 12);
    EXPECT_EQ(c.vrtcRows(), 3);
    EXPECT_EQ(c.hrtcChars(), 22);
    EXPECT_FALSE(c.transparent());
    EXPECT_TRUE(c.cursorUnderline());
    EXPECT_TRUE(c.cursorBlinks());
    EXPECT_TRUE(c.displayEnabled());
    EXPECT_FALSE(c.interruptEnabled());     // ENABLE INTERRUPT sendet die Firmware nie
    EXPECT_EQ(c.burstSpacing(), 0);
    EXPECT_EQ(c.burstLength(), 1);
    bool start = false, cursor = false;
    for (size_t i = 0; i < t.crtProtokoll.size(); ++i) {
        start |= t.crtProtokoll[i].a0 && t.crtProtokoll[i].wert == 0x20;
        cursor |= t.crtProtokoll[i].a0 && t.crtProtokoll[i].wert == 0x80;
    }
    EXPECT_TRUE(start);
    EXPECT_TRUE(cursor);
}

TEST(P8000TerminalHw, ZeilenDmaBedientJedeAnforderungUndBildHat62Komma8Hz)
{
    P8000TerminalHw t;
    bereit(t);
    const uint64_t b0 = t.bilder(), a0 = t.dmaAnforderungen(), d0 = t.dmaBedient(), t0 = t.takte();
    t.laufeBis(t0 + 1000 * MS);
    const uint64_t bilder = t.bilder() - b0;
    // 17,998 MHz / 8 / (102 · 13 · 27) = 62,84 Hz
    EXPECT_GE(bilder, 62u);
    EXPECT_LE(bilder, 63u);
    EXPECT_EQ(t.dmaAnforderungen() - a0, (t.dmaBedient() - d0));
    EXPECT_GE(t.dmaAnforderungen() - a0, (bilder - 1) * 24);
    // Zeilentabelle 1780H: 24 Einträge, Zeile n bei 1000H + 50H·n (nach dem Einschalten)
    for (int z = 0; z < 24; ++z) EXPECT_EQ(t.zeilenAdresse(z), 0x1000 + 0x50 * z) << z;
}

TEST(P8000TerminalHw, ZellenDes8275UndCursor)
{
    P8000TerminalHw t;
    bereit(t);
    const std::string m = "ADM31/9600 baud/Video Attr. on (c)zft/keaw";
    for (size_t i = 0; i < m.size(); ++i) {
        EXPECT_FALSE(t.bildZelle(0, int(i)).empty) << i;
        EXPECT_EQ(t.bildZelle(0, int(i)).code, uint8_t(m[i])) << i;
    }
    // Spalte 79 jeder Zeile: Feldattribut 80H (Firmware CLEAR_LINE) → leer
    EXPECT_EQ(t.bwsByte(0, 79), 0x80);
    EXPECT_TRUE(t.bildZelle(0, 79).empty);
    EXPECT_TRUE(t.zelle(0, 79).feld);
    // Cursor blinkt (Unterstrich): über 1 s mal sichtbar, mal nicht, immer an (1, 0)
    int an = 0, aus = 0;
    for (int i = 0; i < 64; ++i) {
        t.laufeBis(t.takte() + 16 * MS);
        bool irgendwo = false;
        for (int z = 0; z < 24; ++z)
            for (int s = 0; s < 80; ++s)
                if (t.bildZelle(z, s).cursor) { irgendwo = true; EXPECT_EQ(z, 1); EXPECT_EQ(s, 0); }
        (irgendwo ? an : aus)++;
    }
    EXPECT_GT(an, 10);
    EXPECT_GT(aus, 10);
}

TEST(P8000TerminalHw, PixelbildAusDemZeichengenerator)
{
    P8000TerminalHw t;
    bereit(t);
    ASSERT_EQ(t.pixelBreite(), 640);
    ASSERT_EQ(t.pixelHoehe(), 312);
    // Zelle (0,0) = 'A' aus P8TEZS: Rasterzeile l = Byte 41H·16 + l, Bit 7 links
    for (int l = 0; l < 12; ++l) {
        const uint8_t soll = P8T_ZG_EZS[0x41 * 16 + l];
        for (int b = 0; b < 8; ++b)
            EXPECT_EQ(t.pixel()[size_t(l) * 640 + size_t(b)] != 0, (soll & (0x80 >> b)) != 0) << l << "/" << b;
    }
    // leere Zeile 5 ohne Punkte
    for (int l = 0; l < 13; ++l)
        for (int x = 0; x < 640; ++x) ASSERT_EQ(t.pixel()[size_t(5 * 13 + l) * 640 + size_t(x)], 0);
}

TEST(P8000TerminalHw, ErrorTastaturBeiFC)
{
    P8000TerminalHw t;
    t.laufeBis(800 * MS);
    t.tastaturByte(0xFC);
    t.laufeBis(t.takte() + 100 * MS);
    std::string alles;
    for (int z = 0; z < 24; ++z) alles += t.text(z);
    EXPECT_NE(alles.find("Error Tastatur"), std::string::npos) << alles;
}

TEST(P8000TerminalHw, OhneTastaturBleibtDieFirmwareInDerTastaturwarte)
{
    P8000TerminalHw t;
    t.laufeBis(1500 * MS);
    EXPECT_EQ(zl(t, 0), "ADM31/9600 baud/Video Attr. on (c)zft/keaw");
    // Hostzeichen werden noch nicht angezeigt (Hauptschleife nicht erreicht)
    t.hostByte('X');
    t.laufeBis(t.takte() + 50 * MS);
    for (int z = 1; z < 24; ++z) EXPECT_EQ(zl(t, z), "");
    EXPECT_EQ(t.watchdogResets(), 0u);   // Bild läuft weiter, VSYN kommt
}

TEST(P8000TerminalHw, HostzeichenKlingelUndSchieberegister)
{
    P8000TerminalHw t;
    bereit(t);
    for (char c : std::string("Hallo\x07")) t.hostByte(uint8_t(c));
    t.laufeBis(t.takte() + 50 * MS);
    EXPECT_EQ(zl(t, 1), "Hallo");
    EXPECT_EQ(t.klingel(), 1u);
    EXPECT_EQ(t.cursorZeile(), 1);
    EXPECT_EQ(t.cursorSpalte(), 5);
    // Tastatur: Byte voll → IRQ1 → Firmware liest und setzt per C000H zurück
    t.tastaturByte(0x1E);   // „a" Make
    EXPECT_TRUE(t.tastaturRegisterVoll());
    t.laufeBis(t.takte() + 5 * MS);
    EXPECT_FALSE(t.tastaturRegisterVoll());
    t.tastaturByte(0x9E);   // Break
    t.laufeBis(t.takte() + 20 * MS);
    std::string gesendet;
    while (t.hatAusgabe()) { auto s = t.holeAusgabe(); if (s.byte) gesendet += char(s.byte); }
    EXPECT_EQ(gesendet, "a");
}

TEST(P8000TerminalHw, TastaturLeitungBitweise)
{
    P8000TerminalHw t;
    bereit(t);
    // Wie die K7673: Ruhe Takt 0/Daten 1, Startbit Daten 0, Bits invertiert, LSB zuerst,
    // Daten stabil an der steigenden Flanke.
    auto bit = [&](bool daten) {
        t.tastaturLeitung(false, daten);
        t.laufeBis(t.takte() + 100);
        t.tastaturLeitung(true, daten);
        t.laufeBis(t.takte() + 100);
    };
    t.tastaturLeitung(true, true);   // Vorlauf: Flanke mit Daten 1 schiebt 0 ein
    t.tastaturLeitung(false, true);
    bit(false);                       // Startbit
    const uint8_t b = 0x1F;           // „s"
    for (int i = 0; i < 8; ++i) bit(!((b >> i) & 1));
    t.tastaturLeitung(false, true);
    t.laufeBis(t.takte() + 5 * MS);
    t.tastaturByte(0x9F);
    t.laufeBis(t.takte() + 20 * MS);
    std::string gesendet;
    while (t.hatAusgabe()) { auto s = t.holeAusgabe(); if (s.byte) gesendet += char(s.byte); }
    EXPECT_EQ(gesendet, "s");
}

TEST(P8000TerminalHw, WatchdogSetztEineHaengendeFirmwareZurueck)
{
    // Firmware, die sofort mit gesperrten Interrupts kreist: kein VSYN → Reset alle 100 ms
    std::vector<uint8_t> fw(4096, 0xFF);
    fw[0x0C] = 0x8F;                  // DI
    fw[0x0D] = 0x8B; fw[0x0E] = 0xFE; // JR $
    P8000TerminalHwConfig c;
    c.firmware = fw.data(); c.firmwareGroesse = fw.size();
    P8000TerminalHw t(c);
    t.laufeBis(1050 * MS);
    EXPECT_EQ(t.watchdogResets(), 10u);
    // Original-Firmware: kein Reset im Dauerlauf
    P8000TerminalHw o;
    bereit(o);
    o.laufeBis(o.takte() + 2000 * MS);
    EXPECT_EQ(o.watchdogResets(), 0u);
}

TEST(P8000TerminalHw, SaveStateSetztGenauFort)
{
    P8000TerminalHw a;
    bereit(a);
    for (char c : std::string("abc")) a.hostByte(uint8_t(c));
    a.laufeBis(a.takte() + 2 * MS);   // mitten im Empfang
    std::vector<uint8_t> st;
    a.serialize(st);
    P8000TerminalHw b;
    const uint8_t* p = st.data();
    ASSERT_TRUE(b.deserialize(p, st.data() + st.size()));
    EXPECT_EQ(p, st.data() + st.size());
    a.laufeBis(a.takte() + 100 * MS);
    b.laufeBis(b.takte() + 100 * MS);
    EXPECT_EQ(a.takte(), b.takte());
    EXPECT_EQ(zl(a, 1), "abc");
    EXPECT_EQ(zl(b, 1), "abc");
    EXPECT_EQ(a.ram(), b.ram());
    EXPECT_EQ(a.pixel(), b.pixel());
    EXPECT_EQ(a.z8().pc, b.z8().pc);
}
