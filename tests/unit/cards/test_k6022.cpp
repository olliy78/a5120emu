/**
 * @file test_k6022.cpp
 * @brief ADA K6022 (PRG 710): Lochbandstanzer E0H–E3H und -leser E4H–E7H am SIF1000-
 *        Handschlag, so programmiert wie `PTAPE.6022` es tut (doc/design/20_prg710.md
 *        AP-P8b, doc/prg710/sif1000_fernschreiber.md §2).
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

#include "core/cards/k6022/k6022.h"
#include "tests/support/temp_path.h"

namespace {

constexpr uint32_t HZ = 2'457'600;

struct K6022Test : ::testing::Test {
    K1520Bus bus;
    K6022    k{HZ};
    void SetUp() override {
        k.attachToBus(bus);
        bus.setInterruptChain({&k.pioStanzer(), &k.pioLeser()});
        k.reset();
    }
    /// Initialisierung wie `PTAPE.6022` F115H–F158H (Anfrage 00H).
    void ptapeInit() {
        for (uint8_t v : {0xCF, 0xF0}) bus.ioWrite(0xE7, v);         // Leser B: Bitbetrieb, D4–7 ein
        for (uint8_t v : {0xEC, 0x4F, 0x83}) bus.ioWrite(0xE6, v);   // Leser A: Vektor, Eingabe, EI
        bus.ioRead(0xE4);                                            // RUF
        for (uint8_t v : {0xFF, 0xF0}) bus.ioWrite(0xE3, v);         // Stanzer B: Bitbetrieb
        for (uint8_t v : {0xEE, 0x3F, 0x83}) bus.ioWrite(0xE2, v);   // Stanzer A: Vektor, Ausgabe, EI
        bus.ioWrite(0xE1, 0x01);
        bus.ioWrite(0xE5, 0x03);
    }
    void takte(long n) {
        while (n > 0) { k.clockTick(100); n -= 100; }
        bus.markIntDirty();
        bus.updateInterruptChain();
    }
    /// Interrupt annehmen wie die CPU (Vektor) und mit RETI abschließen.
    int quittiere() {
        if (!bus.isINT()) return -1;
        const int v = bus.interruptAcknowledge();
        bus.signalRETI();
        return v;
    }
};

}  // namespace

/// Stanzen: Byte nach E0H → nach der Stanzzeit im Stanzband, END = Interrupt mit Vektor EEH,
/// STA (E1H D4–D7) = 0 — vorher nicht.
TEST_F(K6022Test, StanzerQuittiertMitEndUndVektorEE) {
    ptapeInit();
    takte(HZ / 100);                       // der Leser hat noch kein Band: kein Interrupt
    EXPECT_EQ(quittiere(), -1);
    bus.ioWrite(0xE0, 0xC1);
    takte(HZ / 1000);                      // 1 ms: der daro 1215 stanzt noch [150 Z/s]
    EXPECT_EQ(k.stanzbandLaenge(), 0u);
    EXPECT_EQ(quittiere(), -1);
    takte(HZ / 100);
    ASSERT_EQ(k.stanzbandLaenge(), 1u);
    EXPECT_EQ(k.stanzband()[0], 0xC1);
    EXPECT_EQ(quittiere(), 0xEE);
    EXPECT_EQ(bus.ioRead(0xE1) & 0x60, 0x00) << "STA: kein Fehler";
    EXPECT_EQ(bus.ioRead(0xE1) & 0x0F, 0x01) << "KOM-Ausgänge lesen sich zurück";
}

/// Ausgeschalteter Stanzer: kein END — der Treiber läuft in seine Frist.
TEST_F(K6022Test, AusgeschalteterStanzerQuittiertNicht) {
    ptapeInit();
    k.setStanzerEin(false);
    bus.ioWrite(0xE0, 0x41);
    takte(HZ);
    EXPECT_EQ(k.stanzbandLaenge(), 0u);
    EXPECT_EQ(quittiere(), -1);
}

/// Lesen: RUF (Lesen von E4H) → nach der Lesezeit liegt das nächste Byte an (END,
/// Vektor ECH).  Vorlauf aus Nullbytes, dann der Inhalt, dann Nachlauf; erst nachdem die
/// CPU das letzte Byte abgeholt hat, meldet STA D6 „Bandende“.
TEST_F(K6022Test, LeserLiefertVorlaufInhaltNachlaufUndBandende) {
    K6022::Config c;
    c.vorlauf = 2;
    c.nachlauf = 3;
    K1520Bus b2;
    K6022 kl(c, HZ);
    kl.attachToBus(b2);
    b2.setInterruptChain({&kl.pioStanzer(), &kl.pioLeser()});
    kl.reset();
    auto tick = [&](long n) { while (n > 0) { kl.clockTick(100); n -= 100; } b2.markIntDirty(); b2.updateInterruptChain(); };
    for (uint8_t v : {0xCF, 0xF0}) b2.ioWrite(0xE7, v);
    for (uint8_t v : {0xEC, 0x4F, 0x83}) b2.ioWrite(0xE6, v);
    tick(100);
    EXPECT_EQ(b2.ioRead(0xE5) & K6022::STA_BANDENDE, K6022::STA_BANDENDE) << "kein Band";

    kl.bandEinlegen({0xC1, 0x42});
    tick(100);
    EXPECT_EQ(b2.ioRead(0xE5) & K6022::STA_BANDENDE, 0);
    std::vector<int> gelesen;
    b2.ioRead(0xE4);                                       // erster RUF
    for (int i = 0; i < 7; ++i) {
        tick(HZ / 2000);                                   // 0,5 ms: noch unterwegs [1000 Z/s]
        ASSERT_FALSE(b2.isINT()) << i;
        tick(HZ / 1000);
        ASSERT_TRUE(b2.isINT()) << i;
        EXPECT_EQ(b2.interruptAcknowledge(), 0xEC);
        b2.signalRETI();
        gelesen.push_back(b2.ioRead(0xE4));                // Byte abholen = nächster RUF
    }
    EXPECT_EQ(gelesen, (std::vector<int>{0, 0, 0xC1, 0x42, 0, 0, 0}));
    auto s = kl.leserStand();
    EXPECT_EQ(s.gelesen, 2u);
    EXPECT_FALSE(s.bandende);
    tick(HZ / 100);                                        // RUF ohne Band dahinter
    EXPECT_FALSE(b2.isINT());
    EXPECT_TRUE(kl.leserStand().bandende);
    EXPECT_EQ(b2.ioRead(0xE5) & K6022::STA_BANDENDE, K6022::STA_BANDENDE);

    kl.bandEntnehmen();
    EXPECT_FALSE(kl.leserStand().eingelegt);
}

/// Band aus einer Datei, Stanzband in eine Datei; /RESET lässt beide Bänder liegen.
TEST_F(K6022Test, BaenderAlsDateiUndResetLaesstSieLiegen) {
    const std::string p = k1520test::tempPath("k6022_band.bin");
    {
        std::FILE* f = std::fopen(p.c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fputs("AB\r\n", f);
        std::fclose(f);
    }
    std::string fehler;
    ASSERT_TRUE(k.bandEinlegenDatei(p, fehler)) << fehler;
    EXPECT_EQ(k.leserStand().laenge, 4u);
    EXPECT_FALSE(k.bandEinlegenDatei(p + ".fehlt", fehler));
    EXPECT_FALSE(fehler.empty());

    ptapeInit();
    bus.ioWrite(0xE0, 0x55);
    takte(HZ / 50);
    k.reset();
    EXPECT_EQ(k.stanzbandLaenge(), 1u);
    EXPECT_TRUE(k.leserStand().eingelegt);
    ASSERT_TRUE(k.stanzbandSpeichern(p, fehler)) << fehler;
    EXPECT_EQ(std::filesystem::file_size(p), 1u);
    k.stanzbandLeeren();
    EXPECT_EQ(k.stanzbandLaenge(), 0u);
    std::filesystem::remove(p);
}
