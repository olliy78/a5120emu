/**
 * @file test_pc1715_machine.cpp
 * @brief Pc1715Machine (AP-1b, doc/design/21_pc1715.md §10/§11): bauen, Netz-Ein, Rauchtest mit
 *        dem echten Urlader S502 (kopiert sich ins RAM unter dem ROM und blendet es mit
 *        OUT 28H aus), Bildaufbau alle 20 ms Maschinenzeit, PC 1715W abgelehnt.
 *        Ohne Floppy hängt der Urlader in der Laufwerkssuche bzw. im V.24-Boot — das
 *        untersucht AP-1c.
 */

#include <gtest/gtest.h>

#include <set>

#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/cards/pc1715_zre/rom_s502.h"

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
}  // namespace

TEST(Pc1715Maschine, BautMitBeidenBildschirmen) {
    stumm();
    Pc1715Machine a;
    EXPECT_EQ(a.machineType(), 3);
    EXPECT_EQ(a.fbWidth(), 640);
    EXPECT_EQ(a.fbHeight(), 300);
    Pc1715Machine::Config c;
    c.bild = Pc1715Zre::Bildschirm::K7221;
    Pc1715Machine b(c);
    EXPECT_EQ(b.fbWidth(), 512);
    EXPECT_EQ(b.fbHeight(), 255);
}

/// Seit AP-W3 baut die Variante PC 1715W; abgelehnt wird nur ein Bildschirm, den es dort nicht gibt.
TEST(Pc1715Maschine, Pc1715WBautUndLehntNurK7221Ab) {
    stumm();
    Pc1715Machine::Config c;
    c.variante = Pc1715Machine::Config::Variante::Pc1715W;
    Pc1715Machine m(c);
    EXPECT_TRUE(m.istW());
    EXPECT_EQ(m.fbWidth(), 640);
    EXPECT_EQ(m.fbHeight(), 288);
    c.bild = Pc1715Zre::Bildschirm::K7221;
    try {
        Pc1715Machine k(c);
        FAIL() << "PC 1715W mit K7221 hätte abgelehnt werden müssen";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("PC 1715W"), std::string::npos) << e.what();
    }
}

/**
 * @test Der echte S502 kopiert sich (LDIR auf dieselbe Adresse: Lesen ROM, Schreiben RAM) in das
 *       RAM unter dem ROM und blendet das ROM mit OUT (28H) aus; danach läuft er aus dem RAM.
 */
TEST(Pc1715Maschine, UrladerKopiertSichInsRamUndBlendetDasRomAus) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    EXPECT_EQ(m.cpuPC(), 0x0000);
    EXPECT_TRUE(m.zre().romEin());

    // OUT 28H beobachten.
    bool out28 = false;
    uint64_t out28_takt = 0;
    m.setBusTrace([&](bool io, bool rd, uint16_t a, uint8_t) {
        if (io && !rd && (a & 0xFF) == 0x28 && !out28) { out28 = true; out28_takt = m.totalCycles(); }
    });
    for (int i = 0; i < 30 && !out28; ++i) m.run(10'000);
    ASSERT_TRUE(out28) << "der Urlader muss das ROM mit OUT (28H) ausblenden";
    EXPECT_LT(out28_takt, 100'000u) << "gleich nach dem Kopieren";
    m.run(1000);
    EXPECT_FALSE(m.zre().romEin());

    // Kopie komplett: das RAM unter dem ROM trägt jetzt den Urladerinhalt (Programmteil).
    for (int a = 0; a < 0x400; ++a)
        ASSERT_EQ(m.zre().ramPeek(uint16_t(a)), PC1715_S502_URLADER[a]) << "Adresse " << a;
    // Kennung „121715 031285" an 000EH [ROM]
    std::string kennung;
    for (int a = 0x0B; a < 0x18; ++a) kennung += char(m.zre().ramPeek(uint16_t(a)));
    EXPECT_EQ(kennung, "121715 031285");
}

/**
 * @test Rauchtest mit dem echten S502, ohne Floppy (Befund AP-1b, Takte bei 2,458 MHz):
 *       ~0,04 Mio. OUT 28H (ROM aus) · ~4,0 Mio. Laufwerkssuche: PIOs 00H–07H programmiert,
 *       KRFD 20H/21H · ~13,2 Mio. Boot über V.24: CTC1 (09H) und SIO-B (0FH) · danach wartet
 *       er in der Empfangsschleife bei 0411H auf Zeichen.  Nichts davon ist Absturz; was die
 *       Laufwerkssuche genau erwartet, untersucht AP-1c/AP-2.
 */
TEST(Pc1715Maschine, UrladerLaeuftOhneFloppyBisZumV24Boot) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    std::set<uint16_t> ports;
    m.setBusTrace([&](bool io, bool, uint16_t a, uint8_t) { if (io) ports.insert(a & 0xFF); });
    int summe = 0;
    for (int i = 0; i < 100; ++i) summe += m.run(100'000);        // 10 Mio. Takte = 4 s
    EXPECT_GE(summe, 10'000'000);
    EXPECT_FALSE(m.zre().romEin());
    for (uint16_t p : {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x20, 0x21, 0x28})
        EXPECT_TRUE(ports.count(p)) << "Port " << p << " nach 10 Mio. Takten";
    EXPECT_FALSE(ports.count(0x0F)) << "V.24-Boot erst nach der Laufwerkssuche";
    for (int i = 0; i < 50; ++i) summe += m.run(100'000);         // weitere 2 s
    EXPECT_TRUE(ports.count(0x0F)) << "V.24-Boot (SIO-B) nach der Laufwerkssuche";
    EXPECT_TRUE(ports.count(0x09));
    EXPECT_LT(m.cpuPC(), 0x0800) << "Empfangsschleife im Urlader";
    EXPECT_NE(m.cpuPC(), 0x0000);
}

/// Alle 20 ms Maschinenzeit ein Bild: nach 3 Bildzeiten hat der 8275 frame() erlebt und der
/// Framebuffer ist als geändert markiert.
TEST(Pc1715Maschine, BildwechselAlle20msMaschinenzeit) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    m.fbClearDirty();
    m.run(int(Pc1715Zre::FRAME_TAKTE) - 2000);
    EXPECT_FALSE(m.fbDirty()) << "noch kein Bildwechsel";
    m.run(4000);
    EXPECT_TRUE(m.fbDirty());
    EXPECT_EQ(m.screenChar(0, 0), 0x20) << "8275 unprogrammiert: leere Zelle";
    // Text per Debug-Zugriff + 8275 programmieren (statt des Betriebssystems).
    for (int i = 0; i < 2048; ++i) m.memWriteDebug(uint16_t(0xF800 + i), 0x20);
    const char* t = "PC 1715";
    for (int i = 0; t[i]; ++i) m.memWriteDebug(uint16_t(0xF800 + i), uint8_t(t[i]));
    m.zre().cpu().writePort(0x34, 0x3E);
    m.zre().cpu().writePort(0x19, 0x00);
    for (uint8_t p : {0x4F, 0x57, 0x6B, 0x6D}) m.zre().cpu().writePort(0x18, p);
    m.zre().cpu().writePort(0x19, 0x20);
    m.run(int(Pc1715Zre::FRAME_TAKTE) + 10'000);
    std::string zeile;
    for (int c = 0; c < 7; ++c) zeile += char(m.screenChar(c, 0));
    EXPECT_EQ(zeile, "PC 1715");
}
