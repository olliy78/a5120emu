/**
 * @test I8275.* — CRT-Controller 8275 (doc/design/21_pc1715.md §3.4): Reset-Parametersätze,
 *       Start/Cursor/Status, Sondercodes F0–F3 (Zahl der DMA-Bytes!), Feldattribute,
 *       Cursor-Blinken, Interrupt am Bildende.
 */

#include <gtest/gtest.h>
#include "core/primitives/i8275.h"
#include <deque>

namespace {

struct Rig {
    I8275 crt;
    std::deque<uint8_t> src;   ///< DMA-Quelle; leer → 0x20 (Leerzeichen)
    int gelesen = 0;
    int vrtcs = 0;
    Rig() {
        crt.dmaRead = [this]() -> uint8_t {
            ++gelesen;
            if (src.empty()) return 0x20;
            uint8_t b = src.front(); src.pop_front(); return b;
        };
        crt.vrtc = [this] { ++vrtcs; };
    }
    void params(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        crt.write(true, 0x00);
        crt.write(false, a); crt.write(false, b); crt.write(false, c); crt.write(false, d);
    }
    void start() { crt.write(true, 0x20); }
    // Ein Bild mit n Zeichen je Zeile aus ASCII-Text füllen
    void fill(int n, const char* z) { for (int i = 0; i < n; ++i) src.push_back(z[i]); }
};

} // namespace

// Display 1 (K7221.25): 3F 4F 6E 6B
//  P1 3F: S=0, H=3Fh=63          → 64 Zeichen je Zeile
//  P2 4F: VV=01 → 2 Retrace-Zeilen, RRRRRR=0Fh=15 → 16 Zeilen je Bild
//  P3 6E: U=6 Unterstrichlinie, L=0Eh=14 → 15 Linien je Zeichenzeile
//  P4 6B: 0110 1011: M=0, F=1 nicht transparent, CC=10 Block nicht blinkend, ZZZZ=0Bh=11 → 24 Takte
TEST(I8275, ResetParameterDisplay1_64x16)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    EXPECT_TRUE(r.crt.configured());
    EXPECT_EQ(r.crt.cols(), 64);
    EXPECT_EQ(r.crt.rows(), 16);
    EXPECT_EQ(r.crt.lineCount(), 15);
    EXPECT_EQ(r.crt.underlineLine(), 6);
    EXPECT_EQ(r.crt.vrtcRows(), 2);
    EXPECT_EQ(r.crt.hrtcChars(), 24);
    EXPECT_FALSE(r.crt.transparent());
    EXPECT_FALSE(r.crt.cursorBlinks());
    EXPECT_FALSE(r.crt.cursorUnderline());
}

// Display 2 (K7222.25): 4F 57 6B 6D
//  P1 4F: 4Fh=79 → 80 Zeichen;  P2 57: VV=01 → 2, RRRRRR=17h=23 → 24 Zeilen
//  P3 6B: U=6, L=0Bh=11 → 12 Linien;  P4 6D: F=1, CC=10, ZZZZ=0Dh=13 → 28 Takte
TEST(I8275, ResetParameterDisplay2_80x24)
{
    Rig r;
    r.params(0x4F, 0x57, 0x6B, 0x6D);
    EXPECT_EQ(r.crt.cols(), 80);
    EXPECT_EQ(r.crt.rows(), 24);
    EXPECT_EQ(r.crt.lineCount(), 12);
    EXPECT_EQ(r.crt.underlineLine(), 6);
    EXPECT_EQ(r.crt.hrtcChars(), 28);
}

TEST(I8275, ResetMitCursorformUndTransparentBit)
{
    Rig r;
    r.params(0x4F, 0x57, 0x6B, 0x0D);   // F=0 transparent, CC=00 Block blinkend
    EXPECT_TRUE(r.crt.transparent());
    EXPECT_TRUE(r.crt.cursorBlinks());
    EXPECT_FALSE(r.crt.cursorUnderline());
    r.params(0x4F, 0x57, 0x6B, 0x3D);   // CC=11 Unterstrich nicht blinkend
    EXPECT_FALSE(r.crt.cursorBlinks());
    EXPECT_TRUE(r.crt.cursorUnderline());
}

TEST(I8275, StartDisplayUndStatus)
{
    Rig r;
    EXPECT_EQ(r.crt.read(true), 0);
    r.crt.write(true, 0x20);            // vor Reset: unzulässig → IC
    EXPECT_EQ(r.crt.read(true) & 0x08, 0x08);
    EXPECT_EQ(r.crt.read(true) & 0x08, 0);     // Lesen löscht IC
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();                           // 20H: 1 Zyklus, 0 Takte Abstand
    EXPECT_EQ(r.crt.burstLength(), 1);
    EXPECT_EQ(r.crt.burstSpacing(), 0);
    EXPECT_EQ(r.crt.read(true) & 0x04, 0x04);  // VE
    r.crt.write(true, 0x40);             // Stop Display
    EXPECT_EQ(r.crt.read(true) & 0x04, 0);
}

TEST(I8275, StartDisplayBurstFelder)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.crt.write(true, 0x20 | (3 << 2) | 2);   // SSS=3 → 21 Takte, BB=2 → 4 Zyklen
    EXPECT_EQ(r.crt.burstSpacing(), 21);
    EXPECT_EQ(r.crt.burstLength(), 4);
}

TEST(I8275, LoadCursor)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.crt.write(true, 0x80);
    r.crt.write(false, 5); r.crt.write(false, 3);
    EXPECT_EQ(r.crt.cursorCol(), 5);
    EXPECT_EQ(r.crt.cursorRow(), 3);
    r.start();
    r.crt.frame();
    EXPECT_TRUE(r.crt.cells(3, 5).cursor);
    EXPECT_FALSE(r.crt.cells(3, 4).cursor);
}

TEST(I8275, ParameterOhneBefehlSetztIC)
{
    Rig r;
    r.crt.write(false, 0x12);
    EXPECT_EQ(r.crt.read(true) & 0x08, 0x08);
}

TEST(I8275, FrameLiestCharsMalZeilenUndRuftVrtc)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src.push_back('A'); r.src.push_back('B');
    r.crt.frame();
    EXPECT_EQ(r.vrtcs, 1);
    EXPECT_EQ(r.gelesen, 64 * 16);
    EXPECT_EQ(r.crt.cells(0, 0).code, 'A');
    EXPECT_EQ(r.crt.cells(0, 1).code, 'B');
    EXPECT_FALSE(r.crt.cells(0, 1).empty);
    EXPECT_EQ(r.crt.cells(1, 0).code, 0x20);
}

TEST(I8275, OhneStartKeinDmaUndLeereZellen)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.crt.frame();
    EXPECT_EQ(r.gelesen, 0);
    EXPECT_TRUE(r.crt.cells(0, 0).empty);
}

// F0: Zeilenende, Rest-DMA läuft weiter → je Zeile genau 64 Bytes, Rest leer.
TEST(I8275, F0ZeilenendeLiestRestDerZeileWeiter)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {'X', 0xF0, 'Y', 'Z'};      // 'Y','Z' werden gelesen und verworfen
    r.crt.frame();
    EXPECT_EQ(r.gelesen, 64 * 16);
    EXPECT_EQ(r.crt.cells(0, 0).code, 'X');
    EXPECT_TRUE(r.crt.cells(0, 1).empty);
    EXPECT_TRUE(r.crt.cells(0, 2).empty);
    EXPECT_TRUE(r.crt.cells(0, 3).empty);
    EXPECT_EQ(r.crt.cells(1, 0).code, 0x20);   // nächste Zeile beginnt hinter den 64 Bytes
}

// F1: Zeilenende + Stop-DMA → in dieser Zeile nur bis einschließlich F1 gelesen;
// die nächste Zeile beginnt direkt mit dem folgenden Byte.
TEST(I8275, F1ZeilenendeStopDmaLiestNichtWeiter)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {'X', 0xF1, 'Y'};
    r.crt.frame();
    EXPECT_EQ(r.gelesen, 2 + 64 * 15);
    EXPECT_EQ(r.crt.cells(0, 0).code, 'X');
    EXPECT_TRUE(r.crt.cells(0, 1).empty);
    EXPECT_EQ(r.crt.cells(1, 0).code, 'Y');
}

// F2: Bildende, DMA läuft weiter (alle 16 Zeilen × 64 Bytes), Rest des Bildes leer.
TEST(I8275, F2BildendeLiestWeiterUndLeertRest)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {'X', 0xF2, 'Y'};
    r.crt.frame();
    EXPECT_EQ(r.gelesen, 64 * 16);
    EXPECT_EQ(r.crt.cells(0, 0).code, 'X');
    EXPECT_TRUE(r.crt.cells(0, 1).empty);
    for (int zeile = 1; zeile < 16; ++zeile) EXPECT_TRUE(r.crt.cells(zeile, 0).empty);
}

// F3: Bildende + Stop-DMA → nach F3 keine DMA-Bytes mehr.
TEST(I8275, F3BildendeStopDmaHaeltDmaAn)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {'X', 0xF3, 'Y'};
    r.crt.frame();
    EXPECT_EQ(r.gelesen, 2);
    EXPECT_EQ(r.crt.lastFrameDmaBytes(), 2);
    EXPECT_TRUE(r.crt.cells(1, 0).empty);
    // nächstes Bild beginnt wieder normal (VRTC)
    r.src = {'Q'};
    r.crt.frame();
    EXPECT_EQ(r.vrtcs, 2);
    EXPECT_EQ(r.crt.cells(0, 0).code, 'Q');
}

// Feldattribut 10URGGBH, nicht transparent (F=1): Attributbyte belegt eine Zelle (leer).
TEST(I8275, FeldattributNichtTransparentBelegtEineZelle)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {'A', 0x80 | 0x10 | 0x01 /*R,H*/, 'B', 'C'};
    r.crt.frame();
    EXPECT_FALSE(r.crt.cells(0, 0).rvv);
    EXPECT_TRUE(r.crt.cells(0, 1).empty);
    EXPECT_EQ(r.crt.cells(0, 2).code, 'B');
    EXPECT_TRUE(r.crt.cells(0, 2).rvv);
    EXPECT_TRUE(r.crt.cells(0, 2).hlgt);
    EXPECT_TRUE(r.crt.cells(0, 3).rvv);
    EXPECT_EQ(r.gelesen, 64 * 16);
}

// Transparent (F=0): Attribut belegt keine Zelle, der Baustein holt ein Byte mehr.
TEST(I8275, FeldattributTransparentBelegtKeineZelle)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x0B);   // F=0
    r.start();
    r.src = {'A', 0x80 | 0x20 | 0x04 /*U,GPA0*/, 'B'};
    r.crt.frame();
    EXPECT_EQ(r.crt.cells(0, 0).code, 'A');
    EXPECT_EQ(r.crt.cells(0, 1).code, 'B');
    EXPECT_TRUE(r.crt.cells(0, 1).lten);
    EXPECT_TRUE(r.crt.cells(0, 1).gpa0);
    EXPECT_EQ(r.gelesen, 64 * 16 + 1);  // ein Byte mehr (nur Zeile 0 betroffen)
}

TEST(I8275, FeldattributGiltUeberZeilenUndWirdAmBildanfangZurueckgesetzt)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {0x80 | 0x20 /*U*/};         // Zelle (0,0) = Attribut, danach Leerzeichen
    r.crt.frame();
    EXPECT_TRUE(r.crt.cells(0, 5).lten);
    EXPECT_TRUE(r.crt.cells(7, 5).lten);      // über Zeilengrenzen
    r.src.clear();
    r.crt.frame();                            // neues Bild ohne Attribut
    EXPECT_FALSE(r.crt.cells(0, 5).lten);
}

TEST(I8275, ZeichenattributWirdDurchgereicht)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.src = {static_cast<uint8_t>(0xC0 | (0x5 << 2) | 0x03)};   // 11 0101 1 1
    r.crt.frame();
    const auto& c = r.crt.cells(0, 0);
    EXPECT_TRUE(c.charAttr);
    EXPECT_EQ(c.cca, 0x5);
    EXPECT_TRUE(c.blink);
    EXPECT_TRUE(c.hlgt);
}

TEST(I8275, CursorBlinktAlle16Bilder)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x0B);   // CC=00: Block blinkend
    r.start();
    r.crt.write(true, 0x80); r.crt.write(false, 0); r.crt.write(false, 0);
    bool sicht[40];
    for (int i = 0; i < 40; ++i) { r.crt.frame(); sicht[i] = r.crt.cells(0, 0).cursor; }
    for (int i = 0; i < 16; ++i) EXPECT_TRUE(sicht[i]) << i;
    for (int i = 16; i < 32; ++i) EXPECT_FALSE(sicht[i]) << i;
    EXPECT_TRUE(sicht[32]);
}

// P8000-Terminal (Firmware RESET_DISPLAY sendet E0H zweimal): Preset Counters setzt den
// Bildzähler zurück — die Blinkphasen beginnen von vorn.
TEST(I8275, PresetCountersSetztDieBlinkzaehlerZurueck)
{
    Rig r;
    r.params(0x4F, 0x97, 0xCC, 0x5A);   // P8000-Terminal: 80x24, 13 Linien, Unterstrich blinkend
    r.start();
    r.crt.write(true, 0x80); r.crt.write(false, 0); r.crt.write(false, 0);
    for (int i = 0; i < 40; ++i) r.crt.frame();
    EXPECT_FALSE(r.crt.charBlinkOn());
    r.crt.write(true, 0xE0);
    EXPECT_TRUE(r.crt.charBlinkOn());
    r.crt.frame();
    EXPECT_TRUE(r.crt.cells(0, 0).cursor);   // Bild 0 der Cursorphase: sichtbar
    EXPECT_TRUE(r.crt.cursorUnderline());
    EXPECT_TRUE(r.crt.cursorBlinks());
}

TEST(I8275, ZeichenBlinkphase32Bilder)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    for (int i = 0; i < 32; ++i) r.crt.frame();
    EXPECT_FALSE(r.crt.charBlinkOn());     // 32 Bilder gezählt → zweite Hälfte
    for (int i = 0; i < 32; ++i) r.crt.frame();
    EXPECT_TRUE(r.crt.charBlinkOn());
}

TEST(I8275, NichtBlinkenderCursorBleibtSichtbar)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);   // CC=10
    r.start();
    for (int i = 0; i < 40; ++i) { r.crt.frame(); EXPECT_TRUE(r.crt.cells(0, 0).cursor); }
}

TEST(I8275, InterruptAmBildendeWennIE)
{
    Rig r;
    std::deque<bool> pegel;
    r.crt.irqChanged = [&](bool v) { pegel.push_back(v); };
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.crt.frame();
    EXPECT_FALSE(r.crt.irq());               // IE aus → kein IR
    r.crt.write(true, 0xA0);                 // Enable Interrupt
    EXPECT_EQ(r.crt.read(true) & 0x40, 0x40);
    r.crt.frame();
    EXPECT_TRUE(r.crt.irq());
    EXPECT_EQ(r.crt.read(true) & 0x20, 0x20);   // IR sichtbar …
    EXPECT_FALSE(r.crt.irq());                  // … und durch Lesen gelöscht
    EXPECT_EQ(r.crt.read(true) & 0x20, 0);
    ASSERT_EQ(pegel.size(), 2u);
    EXPECT_TRUE(pegel[0]);
    EXPECT_FALSE(pegel[1]);
    r.crt.write(true, 0xC0);                    // Disable Interrupt
    r.crt.frame();
    EXPECT_FALSE(r.crt.irq());
}

TEST(I8275, ResetBefehlSperrtInterruptUndDisplay)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.start();
    r.crt.write(true, 0xA0);
    r.crt.write(true, 0x00);                    // Reset-Befehl
    EXPECT_EQ(r.crt.peekStatus() & 0x44, 0);
}

TEST(I8275, LichtgriffelLesenLiefertZweiParameter)
{
    Rig r;
    r.params(0x3F, 0x4F, 0x6E, 0x6B);
    r.crt.write(true, 0x60);
    EXPECT_EQ(r.crt.read(false), 0);
    EXPECT_EQ(r.crt.read(false), 0);
    EXPECT_EQ(r.crt.read(false), 0xFF);          // Befehl beendet
}

TEST(I8275, SaveStateRoundtrip)
{
    Rig r;
    r.params(0x4F, 0x57, 0x6B, 0x6D);
    r.start();
    r.crt.write(true, 0xA0);
    r.crt.write(true, 0x80); r.crt.write(false, 7); r.crt.write(false, 9);
    r.crt.frame(); r.crt.frame();
    std::vector<uint8_t> blob;
    r.crt.serialize(blob);

    Rig n;
    const uint8_t* p = blob.data();
    ASSERT_TRUE(n.crt.deserialize(p, blob.data() + blob.size()));
    EXPECT_EQ(p, blob.data() + blob.size());
    EXPECT_EQ(n.crt.cols(), 80);
    EXPECT_EQ(n.crt.rows(), 24);
    EXPECT_EQ(n.crt.cursorCol(), 7);
    EXPECT_EQ(n.crt.cursorRow(), 9);
    EXPECT_TRUE(n.crt.displayEnabled());
    EXPECT_TRUE(n.crt.interruptEnabled());
    EXPECT_EQ(n.crt.peekStatus(), r.crt.peekStatus());
}
