/**
 * @file test_ass590069.cpp
 * @brief ASS 590069 (PRG 710): Fernschreiber an SIO-Kanal B (C5H/C7H), programmiert wie das
 *        SCPX-BIOS `B17172FS` (E2F4H), Ausgabe als Text über den Anschluss
 *        (doc/design/20_prg710.md AP-P8c, doc/prg710/sif1000_fernschreiber.md §3).
 */

#include <gtest/gtest.h>

#include <string>

#include "core/cards/ass590069/ass590069.h"

namespace {

struct Ass590069Test : ::testing::Test {
    K1520Bus  bus;
    Ass590069 k;
    void SetUp() override {
        k.attachToBus(bus);
        k.reset();
    }
    /// BIOS E2F4H: 2 Byte an CDH (CTC K1), 8 Byte an C7H (SIO B).
    void biosInit() {
        for (uint8_t v : {0x07, 0x18}) bus.ioWrite(0xCD, v);
        for (uint8_t v : {0x04, 0xF8, 0x01, 0x00, 0x03, 0x01, 0x05, 0x88}) bus.ioWrite(0xC7, v);
    }
    /// BIOS E35BH: auf RR0 D2 warten, dann Zeichen; der Wandler/die Karte nehmen ab.
    std::string sende(std::initializer_list<uint8_t> codes) {
        std::string aus;
        auto& a = k.anschluss();
        for (uint8_t c : codes) {
            long frist = 0;
            while (!(bus.ioRead(0xC7) & 0x04)) {   // Sender belegt: Karte bzw. „Wandler“ takten
                k.clockTick(1000);
                if (a.senderHatZeichen()) aus += static_cast<char>(a.senderNimm());
                if ((frist += 1000) > 10'000'000) return aus + "<FRIST>";
            }
            bus.ioWrite(0xC5, c);
        }
        for (long n = 0; n < 1'000'000; n += 1000) {
            k.clockTick(1000);
            if (a.senderHatZeichen()) aus += static_cast<char>(a.senderNimm());
        }
        return aus;
    }
};

}  // namespace

/// Die Tabelle des BIOS: ITA2 in Buchstaben- und Ziffernlage, und zurück.
TEST(Ass590069Ita2, TabelleAusDemBios) {
    EXPECT_EQ(Ass590069::ita2NachAscii(0x03, false), 'A');
    EXPECT_EQ(Ass590069::ita2NachAscii(0x03, true), '-');
    EXPECT_EQ(Ass590069::ita2NachAscii(0x08, false), '\r');
    EXPECT_EQ(Ass590069::ita2NachAscii(0x02, true), '\n');
    EXPECT_EQ(Ass590069::ita2NachAscii(0x16, true), '0');
    EXPECT_EQ(Ass590069::ita2NachAscii(0x1B, false), 0) << "Ziffernumschaltung druckt nicht";
    EXPECT_EQ(Ass590069::ita2NachAscii(0x00, false), 0);
    EXPECT_EQ(Ass590069::asciiNachIta2('a'), 0x03);
    EXPECT_EQ(Ass590069::asciiNachIta2('1'), 0x17);
    EXPECT_EQ(Ass590069::asciiNachIta2('@'), -1);
}

/// Format wie programmiert: 5 Bit, 1½ Stopp, ×64 an CTC K1 (φ/16/24) = 100 Bd; nach außen
/// gemeldet als 8 Bit im Zeichentakt der Leitung (7,5 Bit × 24 576 Takte).
TEST_F(Ass590069Test, BiosProgrammiert100Baud) {
    biosInit();
    const auto f = k.anschluss().format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 100u);
    EXPECT_EQ(f.daten, 8);
    EXPECT_EQ(f.zeichen_takte, 184'320u);
    EXPECT_STREQ(k.anschluss().name(), "Fernschreiber");
    EXPECT_FALSE(k.anschluss().v24());
}

/// „Bu“/„Zi“ verbrauchen ihre Zeichenzeit, erscheinen aber nicht; die Lage gilt weiter.
TEST_F(Ass590069Test, UmschaltungWirdAusgefuehrtNichtGedruckt) {
    biosInit();
    // Bu A B Zi 1 2 Bu C CR LF  (ITA2: A=03 B=19 1=17 2=13 C=0E CR=08 LF=02)
    const std::string aus = sende({0x1F, 0x03, 0x19, 0x1B, 0x17, 0x13, 0x1F, 0x0E, 0x08, 0x02});
    EXPECT_EQ(aus, "AB12C\r\n");
}

/// Empfangsrichtung: ein Zeichen vom Hub kommt als ITA2-Code im Empfänger an.
TEST_F(Ass590069Test, EmpfangLegtIta2InDenEmpfaenger) {
    biosInit();
    ASSERT_TRUE(k.anschluss().empfaengerFrei());
    k.anschluss().empfange('E');
    EXPECT_EQ(bus.ioRead(0xC7) & 0x01, 0x01);   // RR0 D0: Zeichen da
    EXPECT_EQ(bus.ioRead(0xC5), 0x01);
}
