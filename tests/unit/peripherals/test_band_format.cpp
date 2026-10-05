/**
 * @file test_band_format.cpp
 * @brief Bandformate des Lochstreifens: Roh, Intel HEX, ASCII-Art
 *        (doc/design/23_lochstreifen.md §4, AP-L2).
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/peripherals/lochstreifen/band_format.h"

using lochstreifen::Format;

namespace {

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }
std::string text(const std::vector<uint8_t>& b) { return {b.begin(), b.end()}; }

std::vector<uint8_t> alleWerte() {
    std::vector<uint8_t> v;
    for (int i = 0; i < 256; ++i) v.push_back(static_cast<uint8_t>(i));
    return v;
}

/// Hin und zurück: schreiben → erkennen → lesen ergibt dasselbe Band.
void rundweg(const std::vector<uint8_t>& band, Format f) {
    const std::vector<uint8_t> datei = lochstreifen::schreiben(band, f);
    if (!band.empty() || f != Format::Roh) EXPECT_EQ(lochstreifen::erkennen(datei), f);
    std::string fehler;
    const auto zurueck = lochstreifen::lesen(datei, f, fehler);
    ASSERT_TRUE(zurueck.has_value()) << fehler;
    EXPECT_EQ(*zurueck, band);
}

}  // namespace

TEST(Lochstreifenformat, KennungenSindStabil) {
    // Die Zahlen gehen über die C-ABI (AP-L3) — nicht umnummerieren.
    EXPECT_EQ(static_cast<int>(Format::Roh), 0);
    EXPECT_EQ(static_cast<int>(Format::IntelHex), 1);
    EXPECT_EQ(static_cast<int>(Format::AsciiArt), 2);
    EXPECT_EQ(lochstreifen::formatAusZahl(2), Format::AsciiArt);
    EXPECT_FALSE(lochstreifen::formatAusZahl(3).has_value());
    EXPECT_STREQ(lochstreifen::formatName(Format::IntelHex), "Intel HEX");
}

TEST(Lochstreifenformat, RohHinUndZurueck) {
    rundweg(alleWerte(), Format::Roh);
    rundweg({}, Format::Roh);
    EXPECT_EQ(lochstreifen::schreiben(alleWerte(), Format::Roh), alleWerte());
}

TEST(Lochstreifenformat, IntelHexHinUndZurueck) {
    rundweg(alleWerte(), Format::IntelHex);
    rundweg({}, Format::IntelHex);
    rundweg({0x00}, Format::IntelHex);            // Nullen am Ende zählen mit
    EXPECT_EQ(text(lochstreifen::schreiben({}, Format::IntelHex)), ":00000001FF\n");
    EXPECT_EQ(text(lochstreifen::schreiben({0x41, 0x42}, Format::IntelHex)),
              ":0200000041427B\n:00000001FF\n");
}

TEST(Lochstreifenformat, IntelHexUeber64KiBMitSatzart04) {
    std::vector<uint8_t> band(0x10000 + 0x123);
    for (std::size_t i = 0; i < band.size(); ++i) band[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
    const std::string t = text(lochstreifen::schreiben(band, Format::IntelHex));
    EXPECT_NE(t.find("\n:020000040001F9\n:10000000"), std::string::npos);
    rundweg(band, Format::IntelHex);
}

TEST(Lochstreifenformat, IntelHexLueckenSatzartenUndCrlf) {
    // Lücke vor 0010H und zwischen den Sätzen = 00H; 03/05 werden überlesen, 02 setzt
    // eine Segmentbasis (×16), nach 01 zählt nichts mehr.
    const std::string t =
        ":0400000300001234B3\r\n"
        ":0100100041AE\r\n"
        "\r\n"
        ":020000020001FB\r\n"
        ":0100020042BB\r\n"                       // 0x10 + 2 = Position 0x12
        ":0400000500001234B1\r\n"
        ":00000001FF\r\n"
        ":01000000FFxx\r\n";                      // hinter dem Ende: ungelesen
    std::string fehler;
    const auto band = lochstreifen::lesen(bytes(t), Format::IntelHex, fehler);
    ASSERT_TRUE(band.has_value()) << fehler;
    std::vector<uint8_t> soll(0x13, 0x00);
    soll[0x10] = 0x41;
    soll[0x12] = 0x42;
    EXPECT_EQ(*band, soll);
    EXPECT_EQ(lochstreifen::erkennen(bytes(t)), Format::IntelHex);
}

TEST(Lochstreifenformat, IntelHexFehlerMitZeilennummer) {
    std::string fehler;
    EXPECT_FALSE(lochstreifen::lesen(bytes(":0100000041BE\n:0100010042BD\n"), Format::IntelHex, fehler));
    EXPECT_NE(fehler.find("Zeile 2"), std::string::npos) << fehler;
    EXPECT_NE(fehler.find("Prüfsumme"), std::string::npos) << fehler;

    EXPECT_TRUE(lochstreifen::lesen(bytes(":00000001FF\r\nhallo\r\n"), Format::IntelHex, fehler))
        << "nach Satzart 01 wird nichts mehr gelesen";
    EXPECT_FALSE(lochstreifen::lesen(bytes("\n\n0100000041BE\n"), Format::IntelHex, fehler));
    EXPECT_NE(fehler.find("Zeile 3"), std::string::npos) << fehler;
    EXPECT_FALSE(lochstreifen::lesen(bytes(":0200000041BD\n"), Format::IntelHex, fehler));
    EXPECT_NE(fehler.find("Zeile 1"), std::string::npos) << fehler;
    EXPECT_FALSE(lochstreifen::lesen(bytes(":00000006FA\n"), Format::IntelHex, fehler));
    EXPECT_NE(fehler.find("Satzart 06"), std::string::npos) << fehler;
}

/// Goldwert: das Bild, das ein Mensch im Editor sieht.  Ändert sich hier etwas, ist
/// §4 des Entwurfs mitzuziehen.
TEST(Lochstreifenformat, AsciiArtGoldwert) {
    const std::string soll =
        "; K1520-Lochstreifen, 8 Spuren, ASCII-Art (doc/design/23_lochstreifen.md)\n"
        "; Eine Zeile = eine Sprosse, der Streifen laeuft von oben nach unten.\n"
        "; O = Loch, . = kein Loch, o = Transportloch; rechts Wert (hex) und Zeichen.\n"
        ";\n"
        ";    8 7 6 5 4   3 2 1\n"
        ";  +-------------------+\n"
        "   | . . . . . o . . . |  00\n"
        "   | . O . . . o . . O |  41  A\n"
        "   | O . . O . o . O O |  93\n"
        ";  +-------------------+\n";
    EXPECT_EQ(text(lochstreifen::schreiben({0x00, 0x41, 0x93}, Format::AsciiArt)), soll);
}

TEST(Lochstreifenformat, AsciiArtHinUndZurueck) {
    rundweg(alleWerte(), Format::AsciiArt);
    rundweg({}, Format::AsciiArt);
}

TEST(Lochstreifenformat, AsciiArtTolerantBeimLesen) {
    // X * # als Loch, Leerzeichen als kein Loch, CRLF, kein Hexwert, Einrückung beliebig.
    const std::string t =
        "; Handarbeit\r\n"
        "\r\n"
        "|   X       o   #   |\r\n"
        "      | * . . . . o . . . |  80\r\n"
        "| . . . . . . . . . |\r\n";            // Transportloch vergessen: egal
    std::string fehler;
    const auto band = lochstreifen::lesen(bytes(t), Format::AsciiArt, fehler);
    ASSERT_TRUE(band.has_value()) << fehler;
    EXPECT_EQ(*band, (std::vector<uint8_t>{0x42, 0x80, 0x00}));
    EXPECT_EQ(lochstreifen::erkennen(bytes(t)), Format::AsciiArt);
}

TEST(Lochstreifenformat, AsciiArtFehlerMitZeilennummer) {
    std::string fehler;
    // Lochbild 41, daneben 42: das Lochbild zählt, der Widerspruch ist ein Fehler.
    EXPECT_FALSE(lochstreifen::lesen(bytes(";\n   | . O . . . o . . O |  41  A\n"
                                           "   | . O . . . o . . O |  42  B\n"),
                                     Format::AsciiArt, fehler));
    EXPECT_NE(fehler.find("Zeile 3"), std::string::npos) << fehler;
    EXPECT_NE(fehler.find("41"), std::string::npos) << fehler;

    EXPECT_FALSE(lochstreifen::lesen(bytes("| . O . . o . . O |\n"), Format::AsciiArt, fehler));
    EXPECT_NE(fehler.find("Zeile 1"), std::string::npos) << fehler;   // eine Spalte zu wenig
    EXPECT_FALSE(lochstreifen::lesen(bytes(";\r\n| . O . . . o . . Q |\r\n"), Format::AsciiArt, fehler));
    EXPECT_NE(fehler.find("Zeile 2"), std::string::npos) << fehler;
    EXPECT_NE(fehler.find("Spur 1"), std::string::npos) << fehler;
    EXPECT_FALSE(lochstreifen::lesen(bytes("| . O . . . o . .O  |\n"), Format::AsciiArt, fehler));
    EXPECT_FALSE(lochstreifen::lesen(bytes("| . . . . . o . . . |\nText\n"), Format::AsciiArt, fehler));
    EXPECT_NE(fehler.find("Zeile 2"), std::string::npos) << fehler;
    EXPECT_FALSE(lochstreifen::lesen(bytes("| . . . . . o . . . |  zz\n"), Format::AsciiArt, fehler));
}

TEST(Lochstreifenformat, Erkennung) {
    EXPECT_EQ(lochstreifen::erkennen({}), Format::Roh);
    EXPECT_EQ(lochstreifen::erkennen({0x00, 0x3A, 0xFF}), Format::Roh);
    EXPECT_EQ(lochstreifen::erkennen(bytes(":00000001FF\nNOTIZ\n")), Format::Roh);
    EXPECT_EQ(lochstreifen::erkennen(bytes("; nur Text\nHALLO\n")), Format::Roh);
    EXPECT_EQ(lochstreifen::erkennen(bytes("\r\n:00000001FF\r\n")), Format::IntelHex);
    EXPECT_EQ(lochstreifen::erkennen(lochstreifen::schreiben({}, Format::AsciiArt)), Format::AsciiArt);
}

TEST(Lochstreifenformat, Endungen) {
    EXPECT_EQ(lochstreifen::formatAusEndung("/a/b/band.HEX"), Format::IntelHex);
    EXPECT_EQ(lochstreifen::formatAusEndung("C:\\x\\band.ihx"), Format::IntelHex);
    EXPECT_EQ(lochstreifen::formatAusEndung("band.txt"), Format::AsciiArt);
    EXPECT_EQ(lochstreifen::formatAusEndung("band.Tape"), Format::AsciiArt);
    EXPECT_EQ(lochstreifen::formatAusEndung("band.ptp"), Format::Roh);
    EXPECT_EQ(lochstreifen::formatAusEndung("band"), Format::Roh);
    EXPECT_EQ(lochstreifen::formatAusEndung("ordner.hex/band"), Format::Roh);
    EXPECT_EQ(lochstreifen::formatEndungen(Format::Roh).front(), ".ptp");
    for (Format f : lochstreifen::alleFormate())
        for (const std::string& e : lochstreifen::formatEndungen(f))
            EXPECT_EQ(lochstreifen::formatAusEndung("x" + e), f) << e;
}
