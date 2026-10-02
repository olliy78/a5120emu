/**
 * @test K7609.* — Host-Taste → 8279-Code (doc/prg710/k7609_codes.csv): physische Taste,
 *       Zeichen, Buchstaben mit Umschaltstufe, Sondertasten, Strg-Einmaltaste, Zustellung
 *       in den FIFO ohne Verlust.
 */

#include <gtest/gtest.h>
#include "core/peripherals/k7609/k7609.h"

namespace {
constexpr uint32_t QK_ESCAPE = 0x01000000, QK_TAB = 0x01000001, QK_RETURN = 0x01000004,
                   QK_LEFT = 0x01000012, QK_UP = 0x01000013, QK_SHIFT = 0x01000020;
using K = K7609;
std::vector<uint8_t> codes(uint32_t k, bool shift = false, bool ctrl = false) {
    uint8_t c[2];
    const int n = K::codesFuer(k, shift, ctrl, c);
    return std::vector<uint8_t>(c, c + n);
}
}  // namespace

TEST(K7609, ReturnIstEt1UndEscapeEt2)
{
    EXPECT_EQ(codes(QK_RETURN), std::vector<uint8_t>{0x37}) << "ET1 = CR = Starttaste des Boot-ROMs";
    EXPECT_EQ(codes(QK_ESCAPE), std::vector<uint8_t>{0x38});
    EXPECT_EQ(codes(QK_TAB), std::vector<uint8_t>{0x3C});
}

TEST(K7609, CursorTasten)
{
    EXPECT_EQ(codes(QK_UP), std::vector<uint8_t>{0x19});
    EXPECT_EQ(codes(QK_LEFT), std::vector<uint8_t>{0x3A});
}

TEST(K7609, BuchstabeIstPhysischeTasteStufeAusShift)
{
    EXPECT_EQ(codes('Q'), std::vector<uint8_t>{0x20});
    EXPECT_EQ(codes('q'), std::vector<uint8_t>{0x20}) << "Groß/Klein macht die Umschaltstufe, nicht das Zeichen";
    EXPECT_EQ(codes('Q', true), std::vector<uint8_t>{0x60});
    EXPECT_EQ(codes('M'), std::vector<uint8_t>{0x36});
}

TEST(K7609, ZiffernUndSatzzeichenSuchenDieTaste)
{
    EXPECT_EQ(codes('1'), std::vector<uint8_t>{0x00});
    EXPECT_EQ(codes(':'), std::vector<uint8_t>{0x40}) << "':' steht auf Taste 1, Umschaltstufe";
    EXPECT_EQ(codes('/'), std::vector<uint8_t>{0x41});
    EXPECT_EQ(codes('9', true), std::vector<uint8_t>{0x0C}) << "Zeichen bestimmt die Stufe, nicht shift";
    EXPECT_EQ(codes(' '), std::vector<uint8_t>{K::CODE_LEERTASTE});
}

TEST(K7609, PhysischeTasteMitUndOhneUmschalten)
{
    EXPECT_EQ(codes(K::QK_TASTE_BASE | 0x28), std::vector<uint8_t>{0x28}) << "Strg-Einmaltaste/CASE";
    EXPECT_EQ(codes(K::QK_TASTE_BASE | 0x28, true), std::vector<uint8_t>{0x68});
    EXPECT_EQ(codes(K::QK_TASTE_BASE | 0x10), std::vector<uint8_t>{0x10});
}

TEST(K7609, BitSiebenKommtNieVor)
{
    for (uint32_t k = 0x20; k < 0x7F; ++k)
        for (bool s : {false, true})
            for (uint8_t c : codes(k, s, true)) EXPECT_EQ(c & 0x80, 0) << k;
}

TEST(K7609, StrgStelltDieEinmaltasteVoran)
{
    EXPECT_EQ(codes('C', false, true), (std::vector<uint8_t>{0x28, 0x32}));
}

TEST(K7609, UmschaltTasteUndUnbekanntesGebenNichts)
{
    EXPECT_TRUE(codes(QK_SHIFT).empty());
    EXPECT_TRUE(codes(0x01000030).empty()) << "F1";
    EXPECT_TRUE(codes('~' + 1).empty());
}

TEST(K7609, ZustellungInDenFifoOhneVerlust)
{
    I8279 kbc;
    K7609 k;
    k.connect(&kbc);
    for (int i = 0; i < 12; ++i) k.keyPress('A', false, false);
    EXPECT_TRUE(k.service());
    EXPECT_EQ(kbc.count(), 8u);
    EXPECT_TRUE(k.sendetNoch());
    for (int i = 0; i < 8; ++i) EXPECT_EQ(kbc.readData(), 0x29);
    EXPECT_TRUE(k.service());
    EXPECT_EQ(kbc.count(), 4u);
    EXPECT_FALSE(k.sendetNoch());
    EXPECT_EQ(kbc.readStatus() & I8279::ST_UEBERLAUF, 0) << "nichts ging über den Rand";
}

TEST(K7609, LoslassenErzeugtKeinenCode)
{
    I8279 kbc;
    K7609 k;
    k.connect(&kbc);
    k.keyPress(QK_RETURN, false, false);
    k.keyRelease(QK_RETURN);
    k.service();
    EXPECT_EQ(kbc.count(), 1u);
}
