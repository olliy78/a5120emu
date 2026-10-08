/**
 * @file test_p8000_ram_konfig.cpp
 * @brief P8000 16-Bit-Karte mit den realen RAM-Ausbauten (P23b, doc/p8000/ram_konfiguration.md):
 *        MMU-Zugriff jenseits 8 MB auf der RAM-Karte 16 MB, Segment 7FH (U16), MAXSEG des
 *        MON16-Hardwaretests je Ausbau.  Die beiden langen Hardwaretests (4 x 1 MB ~10 s,
 *        16 MB ~20 s) gibt es nur in der Fassung RAM_LANG (Runde `tools/dev.sh test-format`).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/karte16.h"

#include <string>

using L = P8000MmuLogik16;

namespace {

P8000Karte16::Config mitDram(const char* text) {
    P8000Karte16::Config c;
    EXPECT_EQ(P8000Dram16::parse(text, c.dram.karten), "") << text;
    return c;
}

/// MON16 3.1 Hardwaretest bis zum Prompt nach MAXSEG; liefert die Ausgabe an tty5.
std::string hardwaretest(P8000Karte16& k, uint64_t max) {
    k.setResetEingang(false);
    std::string t;
    auto bis = [&](const std::string& text, uint64_t n) {
        const uint64_t ende = k.zeit() + n;
        while (k.zeit() < ende) {
            k.schritt();
            for (int i = 0; i < 4; ++i)
                while (k.anschluss(i).senderHatZeichen()) {
                    const char ch = char(k.anschluss(i).senderNimm());
                    if (i == 1) t += ch;
                }
            if (t.find(text) != std::string::npos) return true;
        }
        return false;
    };
    if (!bis("Press NMI", 20'000'000)) return t;
    k.nmiTaste();
    if (!bis("MAXSEG=<", max)) return t;
    const size_t p = t.find("MAXSEG=<");
    for (int i = 0; i < 1000 && t.find('>', p) == std::string::npos; ++i) bis(">", 1'000'000);
    return t;
}

void nurWdcFehlt(const std::string& t) {
    for (size_t p = t.find("ERROR "); p != std::string::npos; p = t.find("ERROR ", p + 1)) {
        const std::string nr = t.substr(p + 6, 2);
        EXPECT_TRUE(nr == "52" || nr == "53" || nr == "54") << t.substr(p, 40);
    }
    EXPECT_EQ(t.find("FATAL"), std::string::npos) << t;
}

/// Alle drei MMUs: Segment @p seg mit Basis @p basis (×256), Grenze FFH, lesen/schreiben.
void segmentAufBasis(P8000Karte16& k, uint8_t seg, uint16_t basis) {
    for (uint16_t mmu : {uint16_t(0x00FC), uint16_t(0x00FA), uint16_t(0x00F6)}) {
        auto sout = [&](uint8_t cmd, uint8_t v) {
            Z8kBusCycle c;
            c.st = Z8kStatus::SpecialIo; c.word = false; c.read = false; c.addr = uint16_t((cmd << 8) | mmu);
            k.busSchreiben(c, uint16_t(v << 8 | v));
        };
        sout(Z8010::CMD_MR, uint8_t(0xD0 | (mmu == 0x00FC ? 0 : mmu == 0x00FA ? 1 : 2)));
        sout(Z8010::CMD_SAR, seg);
        for (uint8_t b : {uint8_t(basis >> 8), uint8_t(basis), uint8_t(0xFF), uint8_t(0)})
            sout(Z8010::CMD_SDR, b);
    }
    Z8kBusCycle c;
    c.st = Z8kStatus::Io; c.system = true; c.word = false; c.read = false; c.addr = L::P_SCR;
    k.busSchreiben(c, uint16_t(L::SCR_MMU_ON << 8 | L::SCR_MMU_ON));
}

Z8kBusCycle datum(uint8_t seg, uint16_t off, bool lesen) {
    Z8kBusCycle c;
    c.st = Z8kStatus::MemData; c.system = true; c.word = true; c.read = lesen; c.seg = seg; c.addr = off;
    return c;
}

}  // namespace

TEST(P8000Karte16Ram, MmuErreichtDieObereHaelfteDer16MBKarte_Segment7FBleibtStumm) {
    P8000Karte16 k(mitDram("16M"));
    ASSERT_TRUE(k.dram().fehler().empty());
    segmentAufBasis(k, 5, 0x8000);                  // phys. 800000H — jenseits der 8-MB-Grenze
    k.busSchreiben(datum(5, 0x1234, false), 0xBEEF);
    EXPECT_EQ(k.dram().peek(0x801234), 0xBE);
    EXPECT_EQ(k.busLesen(datum(5, 0x1234, true)), 0xBEEF);
    segmentAufBasis(k, 6, 0xFFFF);                  // letzte Seite FFFF00H
    k.busSchreiben(datum(6, 0x00FE, false), 0x1357);
    EXPECT_EQ(k.dram().peek(0xFFFFFF), 0x57);
    segmentAufBasis(k, 7, 0x7F00);                  // Segment 7FH: U16 sperrt
    k.busSchreiben(datum(7, 0x0010, false), 0x2468);
    EXPECT_EQ(k.busLesen(datum(7, 0x0010, true)), 0xFFFF) << "kein MEMSEL — offener Bus";
}

TEST(P8000Karte16Ram, OberhalbDerBestueckungKeinZyklus_8MBKarte) {
    P8000Karte16 k(mitDram("8M"));
    segmentAufBasis(k, 5, 0x8000);
    k.busSchreiben(datum(5, 0x0000, false), 0xBEEF);
    EXPECT_EQ(k.busLesen(datum(5, 0x0000, true)), 0xFFFF);
    EXPECT_FALSE(k.dram().gewaehlt(0x800000));
}

TEST(P8000Karte16Ram, Hardwaretest_VierMal256K_MaxSeg0F) {
    P8000Karte16 k(mitDram("4x256K"));
    const std::string t = hardwaretest(k, 600'000'000);
    EXPECT_NE(t.find("MAXSEG=<0F>"), std::string::npos) << t;
    nurWdcFehlt(t);
}

TEST(P8000Karte16Ram, Hardwaretest_ZweiMal256K_MaxSeg07) {
    // Anwenderprotokoll mit MAXSEG=<07>: zwei 256-KB-Karten (512 KB)
    P8000Karte16 k(mitDram("2x256K"));
    const std::string t = hardwaretest(k, 600'000'000);
    EXPECT_NE(t.find("MAXSEG=<07>"), std::string::npos) << t;
    nurWdcFehlt(t);
}

#if RAM_LANG
TEST(P8000Karte16Ram, Hardwaretest_VierMal1M_MaxSeg3F) {
    P8000Karte16 k(mitDram("4x1M"));
    const std::string t = hardwaretest(k, 3'000'000'000);
    EXPECT_NE(t.find("MAXSEG=<3F>"), std::string::npos) << t;
    nurWdcFehlt(t);
}

TEST(P8000Karte16Ram, Hardwaretest_16MBKarte_MaxSeg7E) {
    // MMU aus: A23 = 0 ⇒ MON16 sieht nur die untere Hälfte, U16 nimmt Segment 7FH weg.
    P8000Karte16 k(mitDram("16M"));
    const std::string t = hardwaretest(k, 6'000'000'000);
    EXPECT_NE(t.find("MAXSEG=<7E>"), std::string::npos) << t;
    nurWdcFehlt(t);
}
#endif  // RAM_LANG
