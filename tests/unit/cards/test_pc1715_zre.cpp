/**
 * @file test_pc1715_zre.cpp
 * @brief ZRE des PC 1715 (AP-1b, doc/design/21_pc1715.md §3, §8.2): ROM-Overlay, Portdekodierung,
 *        BWS-Register 34H, 8275-DMA aus dem Haupt-RAM samt Rastern (beide Bildschirmformate,
 *        Zeichengenerator-Wahl, Feldattribute).
 */

#include <gtest/gtest.h>

#include "core/cards/pc1715_zre/pc1715_zre.h"
#include "core/cards/pc1715_zre/rom_s502.h"
#include "core/cards/pc1715_zre/chargen_s619.h"
#include "core/cards/pc1715_zre/chargen_s602.h"
#include "core/cards/pc1715_zre/chargen_s641.h"
#include "core/cards/pc1715_zre/chargen_s643.h"
#include "core/cards/pc1715_zre/chargen_s605.h"

namespace {

using B = Pc1715Zre::Bildschirm;

struct ZreFixture {
    K1520Bus bus;
    Pc1715Zre zre;
    static Pc1715Zre::Config konfig(B bild, Pc1715Zre::Zeichensatz zg, Pc1715Zre::ZgSatz satz) {
        Pc1715Zre::Config c{bild, zg};
        c.zg_satz = satz;
        return c;
    }
    explicit ZreFixture(B bild = B::K7222, Pc1715Zre::Zeichensatz zg = Pc1715Zre::Zeichensatz::S619,
                        Pc1715Zre::ZgSatz satz = Pc1715Zre::ZgSatz::Deutsch)
        : zre(bus, konfig(bild, zg, satz)) {
        zre.attachToBus(bus);
        bus.setInterruptChain({&zre});
        zre.powerOn(0x00);
    }

    /// 8275 mit den Parametersätzen des BIOS (§3.4) programmieren und Start Display (20H).
    void crtStart() {
        const uint8_t p80[4] = {0x4F, 0x57, 0x6B, 0x6D};   // K7222.25: 80 × 24, 12 Linien
        const uint8_t p64[4] = {0x3F, 0x4F, 0x6E, 0x6B};   // K7221.25: 64 × 16, 15 Linien
        bus.ioWrite(0x19, 0x00);
        for (uint8_t p : (zre.config().bild == B::K7222 ? p80 : p64)) bus.ioWrite(0x18, p);
        bus.ioWrite(0x19, 0x20);
    }
    /// Bildspeicher mit Leerzeichen füllen (nicht transparent: jede Zelle belegt ein Byte).
    void bildLoeschen(uint16_t basis) {
        for (int i = 0; i < 2048; ++i) zre.ramPoke(uint16_t(basis + i), 0x20);
    }
    void text(uint16_t basis, int row, int col, const char* s) {
        const int cols = zre.textCols();
        for (int i = 0; s[i]; ++i) zre.ramPoke(uint16_t(basis + row * cols + col + i), uint8_t(s[i]));
    }
    /// Pixelwert (Zeile y, Spalte x) des Framebuffers.
    uint8_t px(int x, int y) const { return zre.framebuffer()[y * zre.fbWidth() + x]; }
    /// Zeile l des Zeichens an (row, col) als 8-Bit-Muster der hellen Pixel.
    uint8_t zeile(int row, int col, int l) const {
        uint8_t m = 0;
        for (int x = 0; x < 8; ++x)
            if (px(col * 8 + x, row * zre.zeichenLinien() + l) != 0) m |= uint8_t(0x80 >> x);
        return m;
    }
};

}  // namespace

// ── ROM-Overlay ──────────────────────────────────────────────────────────────

TEST(Pc1715Zre, RomOverlayLesenRomSchreibenRam) {
    ZreFixture f;
    EXPECT_TRUE(f.zre.romEin()) << "nach /RESET ist das ROM ein";
    EXPECT_EQ(f.zre.memRead(0x0000), PC1715_S502_URLADER[0]);
    EXPECT_EQ(f.zre.memRead(0x07FF), PC1715_S502_URLADER[0x7FF]);

    // Schreiben unter dem ROM landet im RAM; gelesen wird weiter das ROM.
    const uint8_t rom10 = PC1715_S502_URLADER[0x10];
    f.zre.memWrite(0x0010, uint8_t(~rom10));
    EXPECT_EQ(f.zre.ramPeek(0x0010), uint8_t(~rom10));
    EXPECT_EQ(f.zre.memRead(0x0010), rom10);

    // OUT 28H–2BH blendet das ROM aus: das RAM darunter wird sichtbar.
    for (uint8_t p : {0x28, 0x29, 0x2A, 0x2B}) {
        f.bus.ioWrite(0x24, 0);                      // erst ein
        ASSERT_TRUE(f.zre.romEin());
        f.bus.ioWrite(p, 0);
        EXPECT_FALSE(f.zre.romEin()) << "OUT " << int(p);
        EXPECT_EQ(f.zre.memRead(0x0010), uint8_t(~rom10));
    }
    // OUT 24H–27H blendet es wieder ein.
    for (uint8_t p : {0x24, 0x25, 0x26, 0x27}) {
        f.bus.ioWrite(0x28, 0);
        f.bus.ioWrite(p, 0xFF);
        EXPECT_TRUE(f.zre.romEin()) << "OUT " << int(p);
        EXPECT_EQ(f.zre.memRead(0x0010), rom10);
    }
    // Oberhalb 0800H nie ein Overlay.
    f.zre.memWrite(0x0800, 0x5A);
    EXPECT_EQ(f.zre.memRead(0x0800), 0x5A);
}

TEST(Pc1715Zre, ResetBlendetDasRomWiederEin) {
    ZreFixture f;
    f.bus.ioWrite(0x28, 0);
    f.zre.memWrite(0x0000, 0xAA);
    ASSERT_FALSE(f.zre.romEin());
    f.zre.reset();
    EXPECT_TRUE(f.zre.romEin());
    EXPECT_EQ(f.zre.memRead(0x0000), PC1715_S502_URLADER[0]);
    EXPECT_EQ(f.zre.ramPeek(0x0000), 0xAA) << "das RAM bleibt beim /RESET";
}

TEST(Pc1715Zre, CpuLiestAusDemOverlay) {
    // LD HL,0000H; LD (0800H),HL ist egal — es genügt: CPU holt den ersten Befehl aus dem ROM.
    ZreFixture f;
    EXPECT_EQ(f.zre.cpu().readByte(0x0000), PC1715_S502_URLADER[0]);
    f.zre.cpu().writeByte(0x0001, 0x77);
    EXPECT_EQ(f.zre.cpu().readByte(0x0001), PC1715_S502_URLADER[1]);
    EXPECT_EQ(f.zre.ramPeek(0x0001), 0x77);
}

// ── Portdekodierung ──────────────────────────────────────────────────────────

TEST(Pc1715Zre, SioAdressierungA0KanalA1Steuer) {
    // §1.2.4: 0CH Daten A, 0DH Daten B, 0EH Steuer A, 0FH Steuer B.
    ZreFixture f;
    // Statusregister je Kanal: Steuer A (0EH) und Steuer B (0FH) liefern unabhängig RR0.
    EXPECT_EQ(f.bus.ioRead(0x0E), f.zre.sio().ioRead(1));
    EXPECT_EQ(f.bus.ioRead(0x0F), f.zre.sio().ioRead(3));
    // Daten A/B: Senden an 0CH landet im Sender von Kanal A, an 0DH in dem von Kanal B.
    f.bus.ioWrite(0x0E, 0x05); f.bus.ioWrite(0x0E, 0x68);   // WR5: TxEnable, 8 Bit
    f.bus.ioWrite(0x0F, 0x05); f.bus.ioWrite(0x0F, 0x68);
    f.bus.ioWrite(0x0C, 0x41);
    EXPECT_TRUE(f.zre.sio().channelA().txAvailable());
    EXPECT_FALSE(f.zre.sio().channelB().txAvailable());
    f.bus.ioWrite(0x0D, 0x42);
    EXPECT_TRUE(f.zre.sio().channelB().txAvailable());
    EXPECT_EQ(f.zre.sio().channelB().txGet(), 0x42);
}

TEST(Pc1715Zre, CtcAn08bis0B) {
    ZreFixture f;
    f.bus.ioWrite(0x08, 0x05);   // Kanal 0: Zeitgeber, Zeitkonstante folgt
    f.bus.ioWrite(0x08, 0x10);
    // Gelesen wird der Zählerstand; er läuft nach der Konfiguration (≠ FFH-Platzhalter)
    f.zre.clockTick(100);
    EXPECT_NE(f.bus.ioRead(0x08), 0xFF);
}

TEST(Pc1715Zre, UnbelegtLiestFFSchreibenWirdVerschluckt) {
    ZreFixture f;
    for (uint8_t p : {0x40, 0x7F, 0xC0, 0xFF, 0x10, 0x14, 0x38}) {
        EXPECT_EQ(f.bus.ioRead(p), 0xFF) << "Port " << int(p);
        f.bus.ioWrite(p, 0x12);
    }
    EXPECT_TRUE(f.zre.romEin());
}

/// Servicehandbuch §1.2.8.5 (Grundgerät) und §1.3.3 (Zusatzkarte 2 × V.24): 2CH–2FH sind
/// LT107/LT111 der Zusatzkarte (DB0 = Kanal A, DB2 = B), 30H–33H DB1 die 111 am X5.
TEST(Pc1715Zre, Leitungen107Und111) {
    ZreFixture f;
    EXPECT_EQ(f.bus.ioRead(0x2D), 0xFF) << "Zusatzkarte nicht bestückt: 107 AUS = 1";
    EXPECT_EQ(f.bus.ioRead(0x2F), 0xFF);
    f.bus.ioWrite(0x2C, 0x01);
    EXPECT_TRUE(f.zre.lt111(0));
    EXPECT_FALSE(f.zre.lt111(1));
    f.bus.ioWrite(0x2E, 0x04);
    EXPECT_FALSE(f.zre.lt111(0));
    EXPECT_TRUE(f.zre.lt111(1));
    EXPECT_FALSE(f.zre.lt111X5()) << "2CH/2EH berühren X5 nicht";
    f.bus.ioWrite(0x30, 0x02);
    EXPECT_TRUE(f.zre.lt111X5());
    EXPECT_TRUE(f.zre.lt111(1)) << "30H berührt die Zusatzkarte nicht";
    f.bus.ioWrite(0x33, 0x00);
    EXPECT_FALSE(f.zre.lt111X5());
}

// ── BWS-Register ─────────────────────────────────────────────────────────────

TEST(Pc1715Zre, BwsRegisterBasisUndZeichengenerator) {
    {
        ZreFixture f(B::K7222);
        f.bus.ioWrite(0x34, 0x3E);                // BIOS: high(F800H SHR 2) = 3EH
        EXPECT_EQ(f.zre.bildBasis(), 0xF800);
        EXPECT_FALSE(f.zre.bwsZg2());
        f.bus.ioWrite(0x34, 0x3E | 0x40);         // crt2zs: OR 40H
        EXPECT_EQ(f.zre.bildBasis(), 0xF800);
        EXPECT_TRUE(f.zre.bwsZg2());
        f.bus.ioWrite(0x37, 0x3F);                // DB0 = A10 wird beim 80×24 vom Zähler geliefert
        EXPECT_EQ(f.zre.bildBasis(), 0xF800);
        f.bus.ioWrite(0x35, 0x10);
        EXPECT_EQ(f.zre.bildBasis(), 0x4000);
    }
    {
        ZreFixture f(B::K7221);
        f.bus.ioWrite(0x34, 0x3F);                // 64×16: DB0 = A10
        EXPECT_EQ(f.zre.bildBasis(), 0xFC00);
    }
    ZreFixture g;
    g.bus.ioWrite(0x34, 0x7E);
    g.zre.reset();
    EXPECT_EQ(g.zre.bwsRegister(), 0) << "/RESET löscht das BWS-Register";
}

// ── 8275: DMA aus dem Haupt-RAM, Rastern ─────────────────────────────────────

TEST(Pc1715Zre, Bild80x24TextAusRamMitBasisAus34H) {
    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.text(0xF800, 2, 1, "HALLO");
    f.text(0xF800, 23, 79, "Z");
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    EXPECT_EQ(f.zre.fbWidth(), 640);
    EXPECT_EQ(f.zre.fbHeight(), 300);
    f.zre.frame();

    EXPECT_EQ(f.zre.crt().cols(), 80);
    EXPECT_EQ(f.zre.crt().rows(), 24);
    EXPECT_EQ(f.zre.crt().lineCount(), 12);
    EXPECT_EQ(f.zre.screenChar(1, 2), 'H');
    EXPECT_EQ(f.zre.screenChar(5, 2), 'O');
    EXPECT_EQ(f.zre.screenChar(79, 23), 'Z');
    EXPECT_EQ(f.zre.screenChar(0, 0), 0x20);
    EXPECT_EQ(f.zre.screenChar(6, 2), 0x20);
    EXPECT_TRUE(f.zre.fbDirty());

    // Pixel: Zeichen 'H' = ROM[Linie·80H + 48H] (S619), MSB = linkes Pixel; alle 12 Linien.
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(f.zeile(2, 1, l), PC1715_S619_ZG1[l * 0x80 + 'H']) << "Linie " << l;
    EXPECT_EQ(f.zeile(2, 1, 5), 0x7E) << "Querstrich des H";
    EXPECT_EQ(f.px(8 + 1, 2 * 12 + 5), 0xB0) << "normale Helligkeit";
    EXPECT_EQ(f.px(8 + 0, 2 * 12 + 5), 0) << "linkes Pixel dunkel";
    // Ein Leerzeichen bleibt dunkel (der Cursor sitzt bei 0/0 und ist hier egal).
    for (int l = 0; l < 12; ++l) EXPECT_EQ(f.zeile(5, 5, l), 0);
}

TEST(Pc1715Zre, Statuszeile25IstImFramebufferGerastert) {
    // CP/A programmiert 25 Zeilen (Parameter 0x58 statt 0x57): die 25. ist die Statuszeile und
    // liegt unterhalb der 24 Textzeilen im Framebuffer (AP-5a: 640 × 300).
    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.text(0xF800, 24, 0, "H");
    f.bus.ioWrite(0x34, 0x3E);
    f.bus.ioWrite(0x19, 0x00);
    for (uint8_t p : {0x4F, 0x58, 0x6B, 0x6D}) f.bus.ioWrite(0x18, p);
    f.bus.ioWrite(0x19, 0x20);
    f.zre.frame();
    EXPECT_EQ(f.zre.crt().rows(), 25);
    EXPECT_EQ(f.zre.screenChar(0, 24), 'H');
    EXPECT_EQ(f.zre.fbHeight(), 25 * 12);
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(f.zeile(24, 0, l), PC1715_S619_ZG1[l * 0x80 + 'H']) << "Linie " << l;
}

TEST(Pc1715Zre, DmaAdresseIstBasisPlusZaehlerZaehlerBeiVrtcNull) {
    // Der Zähler beginnt bei jedem Bild bei 0: zwei Bilder zeigen dasselbe.
    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.text(0xF800, 0, 3, "AB");
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    f.zre.frame();
    f.zre.frame();
    EXPECT_EQ(f.zre.screenChar(3, 0), 'A');
    EXPECT_EQ(f.zre.screenChar(4, 0), 'B');
    // Ein anderes Basisregister zeigt anderen Speicher.
    f.text(0x4000, 1, 0, "XY");
    f.bus.ioWrite(0x34, 0x10);
    f.zre.frame();
    EXPECT_EQ(f.zre.screenChar(0, 1), 'X');
    EXPECT_EQ(f.zre.screenChar(3, 0), 0) << "anderer Speicher (0x4000..): Zelle Code 0, nicht 'A'";
}

TEST(Pc1715Zre, Bild64x16Mit15LinienUndDunklenLinien12bis14) {
    ZreFixture f(B::K7221);
    f.bildLoeschen(0xFC00);
    f.text(0xFC00, 1, 2, "H");
    f.bus.ioWrite(0x34, 0x3F);
    f.crtStart();
    EXPECT_EQ(f.zre.fbWidth(), 512);
    EXPECT_EQ(f.zre.fbHeight(), 255);
    f.zre.frame();
    EXPECT_EQ(f.zre.crt().cols(), 64);
    EXPECT_EQ(f.zre.crt().rows(), 16);
    EXPECT_EQ(f.zre.crt().lineCount(), 15);
    EXPECT_EQ(f.zre.screenChar(2, 1), 'H');
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(f.zeile(1, 2, l), PC1715_S619_ZG1[l * 0x80 + 'H']) << "Linie " << l;
    for (int l = 12; l < 15; ++l) EXPECT_EQ(f.zeile(1, 2, l), 0) << "Linie " << l << " dunkel";
    // Zeilenhöhe 15: die nächste Zeichenzeile beginnt bei y = 30.
    f.text(0xFC00, 2, 0, "H");
    f.zre.frame();
    EXPECT_EQ(f.zeile(2, 0, 5), 0x7E);
}

TEST(Pc1715Zre, ZeichengeneratorWahlDb6XorGpa0) {
    // Zeichen 24H ($) unterscheidet S619 und S602.
    constexpr int C = 0x24;
    uint8_t z1[12], z2[12];
    bool unterschied = false;
    for (int l = 0; l < 12; ++l) {
        z1[l] = PC1715_S619_ZG1[l * 0x80 + C];
        z2[l] = PC1715_S602_ZG2[l * 0x80 + C];
        unterschied |= z1[l] != z2[l];
    }
    ASSERT_TRUE(unterschied);

    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.zre.ramPoke(0xF800 + 3 * 80 + 1, C);
    // Zeile 4: Feldattribut GPA0 (84H) in Spalte 0, danach das Zeichen
    f.zre.ramPoke(0xF800 + 4 * 80 + 0, 0x84);
    f.zre.ramPoke(0xF800 + 4 * 80 + 1, C);
    f.bus.ioWrite(0x34, 0x3E);       // DB6 = 0: ZG1 (S619)
    f.crtStart();
    f.zre.frame();
    for (int l = 0; l < 12; ++l) {
        EXPECT_EQ(f.zeile(3, 1, l), z1[l]) << "DB6=0, GPA0=0: S619, Linie " << l;
        EXPECT_EQ(f.zeile(4, 1, l), z2[l]) << "DB6=0, GPA0=1: S602, Linie " << l;
    }
    f.bus.ioWrite(0x34, 0x3E | 0x40);   // DB6 = 1: ZG2
    f.zre.frame();
    for (int l = 0; l < 12; ++l) {
        EXPECT_EQ(f.zeile(3, 1, l), z2[l]) << "DB6=1, GPA0=0: S602, Linie " << l;
        EXPECT_EQ(f.zeile(4, 1, l), z1[l]) << "DB6=1, GPA0=1: S619, Linie " << l;
    }
}

TEST(Pc1715Zre, ConfigZeichensatzVorgabeS602) {
    ZreFixture f(B::K7222, Pc1715Zre::Zeichensatz::S602);
    f.bildLoeschen(0xF800);
    f.zre.ramPoke(0xF800 + 3 * 80 + 1, 0x24);
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    f.zre.frame();
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(f.zeile(3, 1, l), PC1715_S602_ZG2[l * 0x80 + 0x24]) << l;
}

// AP-6: die Bestückung der ZG-EPROMs.  Je Satz wird ein Zeichen gerastert, das sich zwischen den
// Bausteinen unterscheidet, und gegen den richtigen Abzug geprüft; der Vorgabesatz bleibt
// bitgleich zum bisherigen Verhalten (S619 bei DB6 = 0, S602 bei GPA0).
namespace {
struct ZgFall { Pc1715Zre::ZgSatz satz; const uint8_t* zg1; const uint8_t* zg2; int code; };
void pruefeZgSatz(const ZgFall& f) {
    ZreFixture z(B::K7222, Pc1715Zre::Zeichensatz::S619, f.satz);
    z.bildLoeschen(0xF800);
    z.zre.ramPoke(0xF800 + 3 * 80 + 1, uint8_t(f.code));
    z.zre.ramPoke(0xF800 + 4 * 80 + 0, 0x84);               // GPA0 → anderer Baustein
    z.zre.ramPoke(0xF800 + 4 * 80 + 1, uint8_t(f.code));
    z.bus.ioWrite(0x34, 0x3E);                               // DB6 = 0: ZG1
    z.crtStart();
    z.zre.frame();
    bool anders = false;
    for (int l = 0; l < 12; ++l) {
        EXPECT_EQ(z.zeile(3, 1, l), f.zg1[l * 0x80 + f.code]) << "ZG1, Linie " << l;
        EXPECT_EQ(z.zeile(4, 1, l), f.zg2[l * 0x80 + f.code]) << "ZG2, Linie " << l;
        anders |= f.zg1[l * 0x80 + f.code] != f.zg2[l * 0x80 + f.code];
    }
    EXPECT_TRUE(anders) << "Prüfzeichen muss sich zwischen den Bausteinen unterscheiden";
}
}  // namespace

TEST(Pc1715Zre, ZgSatzDeutschIstS619UndS602) {
    pruefeZgSatz({Pc1715Zre::ZgSatz::Deutsch, PC1715_S619_ZG1, PC1715_S602_ZG2, 0x5B});   // [ / Ä
}
TEST(Pc1715Zre, ZgSatzPolnischIstS641UndS619) {
    pruefeZgSatz({Pc1715Zre::ZgSatz::Polnisch, PC1715_S641_ZG1, PC1715_S619_ZG1, 0x5B});
}
TEST(Pc1715Zre, ZgSatzKyrillischIstS643UndS605) {
    pruefeZgSatz({Pc1715Zre::ZgSatz::Kyrillisch, PC1715_S643_ZG1, PC1715_S605_ZG2, 0x41});  // A: ZG1 und ZG2 verschieden
}
TEST(Pc1715Zre, ZgSatzKyrillischBeiDb6Eins) {
    // Zeichen 61H ist in S643 kyrillisch, in S619 lateinisch: DB6 = 1 wählt den anderen Baustein
    ZreFixture z(B::K7222, Pc1715Zre::Zeichensatz::S619, Pc1715Zre::ZgSatz::Kyrillisch);
    z.bildLoeschen(0xF800);
    z.zre.ramPoke(0xF800 + 3 * 80 + 1, 0x61);
    z.bus.ioWrite(0x34, 0x3E | 0x40);
    z.crtStart();
    z.zre.frame();
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(z.zeile(3, 1, l), PC1715_S605_ZG2[l * 0x80 + 0x61]) << l;
}

TEST(Pc1715Zre, FeldattributeInversUnterstrichenHellBlinken) {
    ZreFixture f;
    f.bildLoeschen(0xF800);
    auto z = [&](int row, int col, uint8_t b) { f.zre.ramPoke(uint16_t(0xF800 + row * 80 + col), b); };
    z(5, 0, 0x90); z(5, 1, 'H');          // Invers (R)
    z(6, 0, 0xA0); z(6, 1, 'H');          // Unterstreichen (U)
    z(7, 0, 0x81); z(7, 1, 'H');          // Hell (H)
    z(8, 0, 0x82); z(8, 1, 'H');          // Blinken (B)
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    f.zre.frame();

    const int ul = f.zre.crt().underlineLine();
    EXPECT_EQ(ul, 6);
    for (int l = 0; l < 12; ++l)
        EXPECT_EQ(f.zeile(5, 1, l), uint8_t(~PC1715_S619_ZG1[l * 0x80 + 'H'])) << "invers, Linie " << l;
    for (int l = 0; l < 12; ++l) {
        const uint8_t erw = l == ul ? 0xFF : PC1715_S619_ZG1[l * 0x80 + 'H'];
        EXPECT_EQ(f.zeile(6, 1, l), erw) << "unterstrichen, Linie " << l;
    }
    EXPECT_EQ(f.px(8 + 1, 7 * 12 + 5), 0xFF) << "hell";
    EXPECT_EQ(f.px(8 + 1, 5 * 12 + 5), 0) << "invers: im Strich dunkel";
    EXPECT_EQ(f.zeile(8, 1, 5), 0x7E) << "Blinken: erste Phase sichtbar";
    for (int i = 0; i < 40; ++i) f.zre.frame();
    EXPECT_EQ(f.zeile(8, 1, 5), 0) << "Blinken: zweite Phase dunkel";
    EXPECT_EQ(f.zre.screenChar(1, 8), 'H') << "der Code bleibt";
}

TEST(Pc1715Zre, CursorBlockInvers) {
    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    f.bus.ioWrite(0x19, 0x80);     // Load Cursor: Spalte, Zeile
    f.bus.ioWrite(0x18, 10);
    f.bus.ioWrite(0x18, 4);
    f.zre.frame();
    EXPECT_EQ(f.zeile(4, 10, 3), 0xFF) << "Blockcursor auf Leerzeichen: ganze Zelle hell";
    EXPECT_EQ(f.zeile(4, 11, 3), 0x00);
}

TEST(Pc1715Zre, AnzeigeAusBleibtDunkel) {
    ZreFixture f;
    f.bildLoeschen(0xF800);
    f.text(0xF800, 0, 1, "HALLO");
    f.bus.ioWrite(0x34, 0x3E);
    f.crtStart();
    f.zre.frame();
    ASSERT_EQ(f.zre.screenChar(1, 0), 'H');
    f.bus.ioWrite(0x19, 0x40);     // Stop Display
    f.zre.frame();
    for (int l = 0; l < 12; ++l) EXPECT_EQ(f.zeile(0, 1, l), 0);
    EXPECT_EQ(f.zre.screenChar(1, 0), 0x20);
}
