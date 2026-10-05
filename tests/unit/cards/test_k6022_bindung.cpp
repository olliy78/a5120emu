/**
 * @file test_k6022_bindung.cpp
 * @brief K6022: Leser aus Datei im Bandformat, Stanzer an eine Datei gebunden mit
 *        verzögertem Zurückschreiben (doc/design/23_lochstreifen.md §5, AP-L2).
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/cards/k6022/k6022.h"
#include "tests/support/temp_path.h"

using lochstreifen::Format;

namespace {

constexpr uint32_t HZ = 2'457'600;

std::vector<uint8_t> dateiInhalt(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void dateiAnlegen(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

/// Räumt eine Temp-Datei am Ende des Tests weg (auch wenn er scheitert).
struct TempDatei {
    std::string pfad;
    explicit TempDatei(const std::string& name) : pfad(k1520test::tempPath(name)) {
        std::error_code ec;
        std::filesystem::remove(pfad, ec);
    }
    ~TempDatei() {
        std::error_code ec;
        std::filesystem::remove(pfad, ec);
    }
};

struct K6022Bindung : ::testing::Test {
    K1520Bus bus;
    std::unique_ptr<K6022> k = std::make_unique<K6022>(HZ);
    void SetUp() override {
        k->attachToBus(bus);
        bus.setInterruptChain({&k->pioStanzer(), &k->pioLeser()});
        k->reset();
        // Stanzer wie `PTAPE.6022`: Tor A Betriebsart 0, Interrupt ein.
        for (uint8_t v : {0xFF, 0xF0}) bus.ioWrite(0xE3, v);
        for (uint8_t v : {0xEE, 0x0F, 0x83}) bus.ioWrite(0xE2, v);
    }
    /// @p n Takte laufen lassen, wie die Maschine: clockTick je Befehl, autoFlush je Scheibe.
    void lauf(long n) {
        while (n > 0) { k->clockTick(100); n -= 100; }
        k->autoFlush();
        bus.markIntDirty();
        bus.updateInterruptChain();
        if (bus.isINT()) { bus.interruptAcknowledge(); bus.signalRETI(); }
    }
    void stanze(const std::vector<uint8_t>& b) {
        for (uint8_t v : b) {
            bus.ioWrite(0xE0, v);
            lauf(HZ / 100);                      // 10 ms > 1/150 s: gestanzt, END quittiert
        }
    }
};

}  // namespace

/// Gebundener Stanzer: während des Stanzens bleibt die Datei, wie sie war; nach der
/// Stanzpause (0,5 s Maschinenzeit) steht das Band im gewählten Format darin.
TEST_F(K6022Bindung, StanzerSchreibtNachDerStanzpause) {
    TempDatei d("k6022_bindung.hex");
    std::string fehler;
    ASSERT_TRUE(k->stanzerBinden(d.pfad, Format::IntelHex, fehler)) << fehler;
    EXPECT_EQ(k->stanzerDatei(), d.pfad);
    EXPECT_EQ(k->stanzerFormat(), Format::IntelHex);
    const std::vector<uint8_t> leer = lochstreifen::schreiben({}, Format::IntelHex);
    EXPECT_EQ(dateiInhalt(d.pfad), leer) << "fehlende Datei wird leer angelegt";

    stanze({0x41, 0x42, 0x43});
    EXPECT_EQ(k->stanzbandLaenge(), 3u);
    EXPECT_EQ(dateiInhalt(d.pfad), leer) << "noch in der Stanzpause";
    lauf(HZ / 4);
    EXPECT_EQ(dateiInhalt(d.pfad), leer) << "0,25 s + 30 ms: Pause noch nicht um";
    lauf(HZ / 4);
    EXPECT_EQ(dateiInhalt(d.pfad), lochstreifen::schreiben({0x41, 0x42, 0x43}, Format::IntelHex));
    EXPECT_EQ(k->stanzerFehler(), "");
}

/// Eine vorhandene Datei ist der Bandanfang: weiterstanzen hängt an.
TEST_F(K6022Bindung, WeiterstanzenHaengtAnVorhandeneDateiAn) {
    TempDatei d("k6022_anhaengen.txt");
    dateiAnlegen(d.pfad, lochstreifen::schreiben({0x01, 0x02}, Format::AsciiArt));
    std::string fehler;
    ASSERT_TRUE(k->stanzerBinden(d.pfad, Format::AsciiArt, fehler)) << fehler;
    EXPECT_EQ(k->stanzband(), (std::vector<uint8_t>{0x01, 0x02}));
    stanze({0x03});
    lauf(HZ);
    EXPECT_EQ(dateiInhalt(d.pfad), lochstreifen::schreiben({0x01, 0x02, 0x03}, Format::AsciiArt));

    // Falsches Format: Bindung abgelehnt, die bestehende bleibt.
    TempDatei kaputt("k6022_kaputt.hex");
    dateiAnlegen(kaputt.pfad, {'n', 'e', 'i', 'n', '\n'});
    EXPECT_FALSE(k->stanzerBinden(kaputt.pfad, Format::IntelHex, fehler));
    EXPECT_NE(fehler.find("Zeile 1"), std::string::npos) << fehler;
    EXPECT_EQ(k->stanzerDatei(), d.pfad);
}

/// „Neues Band“ leert Band und gebundene Datei sofort.
TEST_F(K6022Bindung, NeuesBandLeertDieDatei) {
    TempDatei d("k6022_neu.ptp");
    dateiAnlegen(d.pfad, {0x11, 0x22, 0x33});
    std::string fehler;
    ASSERT_TRUE(k->stanzerBinden(d.pfad, Format::Roh, fehler)) << fehler;
    EXPECT_EQ(k->stanzbandLaenge(), 3u);
    ASSERT_TRUE(k->neuesBand());
    EXPECT_EQ(k->stanzbandLaenge(), 0u);
    EXPECT_EQ(std::filesystem::file_size(d.pfad), 0u);
}

/// Lösen schreibt sofort (ohne Pause) zurück; danach sammelt der Stanzer im Speicher.
/// Der Wechsel der Datei und das Zerstören der Karte schreiben ebenso.
TEST_F(K6022Bindung, LoesenWechselUndDestruktorSchreiben) {
    TempDatei a("k6022_loesen_a.ptp");
    TempDatei b("k6022_loesen_b.ptp");
    std::string fehler;
    ASSERT_TRUE(k->stanzerBinden(a.pfad, Format::Roh, fehler)) << fehler;
    stanze({0x55});
    ASSERT_TRUE(k->stanzerLoesen());
    EXPECT_EQ(dateiInhalt(a.pfad), (std::vector<uint8_t>{0x55}));
    EXPECT_EQ(k->stanzerDatei(), "");
    stanze({0x66});
    lauf(HZ);
    EXPECT_EQ(dateiInhalt(a.pfad), (std::vector<uint8_t>{0x55})) << "gelöst: Datei bleibt";

    ASSERT_TRUE(k->stanzerBinden(a.pfad, Format::Roh, fehler)) << fehler;
    stanze({0x77});
    ASSERT_TRUE(k->stanzerBinden(b.pfad, Format::Roh, fehler)) << fehler;   // Wechsel
    EXPECT_EQ(dateiInhalt(a.pfad), (std::vector<uint8_t>{0x55, 0x77}));
    stanze({0x88});
    k.reset();                                                             // Destruktor
    EXPECT_EQ(dateiInhalt(b.pfad), (std::vector<uint8_t>{0x88}));
}

/// Leser: Intel-HEX- und ASCII-Art-Band werden umgesetzt eingelegt; Datei und Format
/// sind abfragbar, ein Formatfehler kommt mit Zeilennummer.
TEST_F(K6022Bindung, LeserLiestIntelHexUndAsciiArt) {
    const std::vector<uint8_t> band = {0x00, 0x41, 0xFF, 0x93};
    for (Format f : {Format::IntelHex, Format::AsciiArt}) {
        TempDatei d(std::string("k6022_leser") + lochstreifen::formatEndungen(f).front());
        dateiAnlegen(d.pfad, lochstreifen::schreiben(band, f));
        std::string fehler;
        ASSERT_TRUE(k->bandEinlegenDatei(d.pfad, f, fehler)) << fehler;
        EXPECT_EQ(k->leserDatei(), d.pfad);
        EXPECT_EQ(k->leserFormat(), f);
        EXPECT_EQ(k->leserStand().laenge, band.size());

        // Über die PIO abholen (Betriebsart 1): Vorlauf, dann das Band.
        for (uint8_t v : {0xCF, 0xF0}) bus.ioWrite(0xE7, v);
        for (uint8_t v : {0xEC, 0x4F, 0x83}) bus.ioWrite(0xE6, v);
        std::vector<uint8_t> gelesen;
        bus.ioRead(0xE4);                                // erster RUF
        for (int i = 0; i < 16 + 4; ++i) {
            lauf(HZ / 500);
            gelesen.push_back(bus.ioRead(0xE4));
        }
        EXPECT_EQ(std::vector<uint8_t>(gelesen.end() - 4, gelesen.end()), band)
            << lochstreifen::formatName(f);
    }
    TempDatei kaputt("k6022_leser_kaputt.txt");
    dateiAnlegen(kaputt.pfad, {'|', ' ', 'O', ' ', '|', '\n'});
    std::string fehler;
    EXPECT_FALSE(k->bandEinlegenDatei(kaputt.pfad, Format::AsciiArt, fehler));
    EXPECT_NE(fehler.find("Zeile 1"), std::string::npos) << fehler;
    k->bandEntnehmen();
    EXPECT_EQ(k->leserDatei(), "");
}
