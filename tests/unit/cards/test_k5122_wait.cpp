/**
 * @file test_k5122_wait.cpp
 * @brief K5122 im `/WAIT`-Betrieb (K8915): drehgekoppelter Datenweg, Wartetakte,
 *        Marken-FF (MKE) und Datenfeld-Schreiben.
 *
 * Nachgestellt wird, was die Laufschleife der K8915Machine tut: ein Zugriff auf den
 * Datenport trägt Wartetakte beim Bus ein (@ref K1520Bus::addWaitCycles), die Schleife
 * holt sie ab und schreibt die Zeit mit @ref K5122::update fort.  Die Tests fahren
 * dieselbe Folge wie Boot-ROM und BIOS des K8915 (doc/design/16_k8915.md §4.3/§4.4):
 * Laufwerk wählen, `/STR` = 0 und MR (Bit5) = 0 ⇒ scharf, auf MKE warten, Kopf
 * ausgerollt lesen.
 *
 * @see core/cards/k5122/k5122_wait.cpp, doc/design/07_k5122_afs.md §7.7
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <fstream>
#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/k5122/k5122.h"
#include "core/logger.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tests/support/temp_path.h"

namespace {

constexpr uint32_t kHz     = 2'457'600;   // K8915
constexpr int      kByte   = 78;          // 2 457 600 / 31 250 (MFM)

/// 2 Zylinder, 2 Köpfe, 5 × 1024 B MFM (cpa800-artig).  Muster je Sektor:
/// Füllbyte = Zylinder·16 + Kopf·8 + Sektor.
DiskFormat format5x1024() {
    DiskFormat f;
    f.name = "wait_5x1024";
    TrackFormat t;
    t.cyl_first = 0; t.cyl_last = 1; t.head_first = 0; t.head_last = 1;
    t.secs_per_track = 5; t.bytes_per_sec = 1024; t.encoding = Encoding::MFM;
    f.tracks.push_back(t);
    return f;
}

uint8_t muster(int c, int h, int s) { return static_cast<uint8_t>(c * 16 + h * 8 + s); }

std::string abbild(const DiskFormat& f) {
    const std::string pfad = k1520test::tempPath("k1520_wait_5x1024.img");
    std::ofstream o(pfad, std::ios::binary | std::ios::trunc);
    for (int c = 0; c < 2; ++c)
        for (int h = 0; h < 2; ++h)
            for (int s = 1; s <= 5; ++s) {
                std::vector<char> b(1024, static_cast<char>(muster(c, h, s)));
                o.write(b.data(), static_cast<std::streamsize>(b.size()));
            }
    (void)f;
    return pfad;
}

class K5122Wait : public ::testing::Test {
protected:
    K1520Bus   bus;
    K5122      card{bus, {builtinDriveProfile("K5601"), builtinDriveProfile("K5601"),
                          builtinDriveProfile("none"), builtinDriveProfile("none")}, kHz};
    DiskFormat fmt = format5x1024();
    std::string pfad;

    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        card.setSynchronisation(K5122::Synchronisation::Wait);
        pfad = abbild(fmt);
        ASSERT_TRUE(card.mountDisk(0, pfad, fmt, false));
        card.ioWrite(0x10, 0xBB);          // Grundstellung: /STR = 1, MR = 1
        card.ioWrite(0x18, 0xEE);          // Laufwerk 0 wählen, Motor an
        lauf(20'000);                      // Anlauf (2 ms) abwarten
    }
    void TearDown() override { std::remove(pfad.c_str()); }

    /// Zeit vergehen lassen, wie die Laufschleife es tut.
    void lauf(int takte) { card.update(takte); }

    /// Ein Zugriff auf den Datenport; die Wartetakte werden sofort verbucht.
    uint8_t lies(int* warte = nullptr) {
        const uint8_t b = card.ioRead(0x16);
        const int w = bus.takeWaitCycles();
        if (warte) *warte = w;
        if (w) card.update(w);
        return b;
    }
    void schreib(uint8_t b) {
        card.ioWrite(0x14, b);
        const int w = bus.takeWaitCycles();
        if (w) card.update(w);
    }

    /// Scharf machen wie ROM/BIOS: Lesesteuerwort 85H (/STR = 0, MR = 0, Kopf 0, MFM).
    void scharf(uint8_t steuerwort = 0x85) {
        card.ioWrite(0x10, static_cast<uint8_t>(steuerwort | 0x20));   // MR = 1: Marken-FF rücksetzen
        card.ioWrite(0x10, steuerwort);                                 // MR = 0: scharf
    }
    /// In Schritten von 10 Takten bis zum MKE; liefert die Wartezeit (−1 = nie).
    long bisMke(long frist = 600'000) {
        for (long t = 0; t < frist; t += 10) {
            if (card.markeErkannt()) return t;
            lauf(10);
        }
        return -1;
    }
    /// Sektorkopf nach MKE lesen: A1… überspringen, dann FE C H R N.
    std::vector<uint8_t> kopf() {
        uint8_t b = lies();
        while (b == 0xA1) b = lies();
        std::vector<uint8_t> k{b};
        for (int i = 0; i < 4; ++i) k.push_back(lies());
        return k;
    }
};

}  // namespace

/**
 * @test K5122Wait.LesenWartetGenauEineByteperiode
 * @brief Zwei Zugriffe ohne Zeit dazwischen: der zweite wartet genau bis zur nächsten
 *        Bytegrenze (78 Takte bei 2,4576 MHz, MFM) — die CPU steht per `/WAIT`.
 */
TEST_F(K5122Wait, LesenWartetGenauEineByteperiode) {
    scharf();
    // Erster Zugriff irgendwo in einem Bytefenster: das zuletzt fertige Byte liegt
    // noch im Daten-PIO (kein Warten), der zweite wartet bis zum Fensterende, ab dem
    // dritten steht die CPU genau eine Byteperiode.
    int w1 = -1, w2 = -1, w3 = -1, w4 = -1;
    lies(&w1);
    lies(&w2);
    lies(&w3);
    lies(&w4);
    EXPECT_EQ(w1, 0);
    EXPECT_GT(w2, 0);
    EXPECT_LE(w2, kByte);
    EXPECT_EQ(w3, kByte);
    EXPECT_EQ(w4, kByte);
}

/**
 * @test K5122Wait.OhneStrKeinWarten
 * @brief Bei `/STR` = 1 fließen keine Daten: der Datenport liefert sofort den alten
 *        Inhalt, ohne Wartetakte (BIOS E902H leert ihn so) — sonst hinge die CPU.
 */
TEST_F(K5122Wait, OhneStrKeinWarten) {
    int w = -1;
    lies(&w);
    EXPECT_EQ(w, 0);
    lies(&w);
    EXPECT_EQ(w, 0);
}

/**
 * @test K5122Wait.MkeErstNachSyncGruppeUndDannDerKopf
 * @brief Nach dem Scharfmachen ist MKE (12H Bit1) 0, bis eine Sync-Gruppe unter dem
 *        Kopf durch ist; der erste Zugriff danach liefert A1, dann FE und den Kopf.
 *        MR = 1 setzt MKE zurück; neu scharf findet den NÄCHSTEN Sektor.
 */
TEST_F(K5122Wait, MkeErstNachSyncGruppeUndDannDerKopf) {
    scharf();
    EXPECT_FALSE(card.markeErkannt());
    EXPECT_EQ(card.ioRead(0x12) & 0x02, 0x00);
    ASSERT_GE(bisMke(), 0) << "kein MKE binnen einer Umdrehung";
    EXPECT_EQ(card.ioRead(0x12) & 0x02, 0x02) << "MKE auf Tor B Bit1";

    const std::vector<uint8_t> k1 = kopf();
    ASSERT_EQ(k1[0], 0xFE);
    EXPECT_EQ(k1[1], 0);   // Zylinder
    EXPECT_EQ(k1[2], 0);   // Kopf
    EXPECT_EQ(k1[4], 3);   // 1024 B

    card.ioWrite(0x10, 0xA5);                      // MR = 1
    EXPECT_FALSE(card.markeErkannt()) << "MR setzt das Marken-FF zurück";
    card.ioWrite(0x10, 0x85);                      // MR = 0: neu scharf
    EXPECT_FALSE(card.markeErkannt());
    ASSERT_GE(bisMke(), 0);
    const std::vector<uint8_t> dam = kopf();       // nächste Marke = Datenfeld (FB)
    EXPECT_EQ(dam[0], 0xFB);
    EXPECT_EQ(dam[1], muster(0, 0, k1[3]));

    card.ioWrite(0x10, 0xA5);
    card.ioWrite(0x10, 0x85);
    ASSERT_GE(bisMke(), 0);
    const std::vector<uint8_t> k2 = kopf();
    ASSERT_EQ(k2[0], 0xFE);
    EXPECT_EQ(k2[3], k1[3] % 5 + 1) << "der nächste Sektor in Drehfolge";

    card.ioWrite(0x10, 0x8D);                      // /STR = 1
    EXPECT_FALSE(card.markeErkannt()) << "/STR = 1 setzt das Marken-FF zurück";
}

/**
 * @test K5122Wait.MkeLoestDenPioInterruptAus
 * @brief PIO1 B wie das BIOS (Mode 3, Richtung F3H, 37H/Maske FDH = Bit1 bei high,
 *        83H frei, Vektor F2H): MKE ⇒ Interrupt mit F2H — und erst dann.  Das
 *        Freigabewort 83H darf „bei high“ nicht umstellen (Z80 PIO: nur D7).
 */
TEST_F(K5122Wait, MkeLoestDenPioInterruptAus) {
    card.setIEI(true);
    for (uint8_t b : {0xF2, 0xCF, 0xF3, 0x37, 0xFD}) card.ioWrite(0x13, b);
    card.ioWrite(0x10, 0xBB);
    card.ioWrite(0x13, 0x83);
    EXPECT_FALSE(card.hasInterrupt()) << "Freigabe ohne MKE darf nichts auslösen";
    scharf();
    EXPECT_FALSE(card.hasInterrupt());
    ASSERT_GE(bisMke(), 0);
    EXPECT_TRUE(card.hasInterrupt());
    EXPECT_EQ(card.getVector(), 0xF2);
}

/**
 * @test K5122Wait.UeberlaufVerliertBytes
 * @brief Die Scheibe wartet nicht: wer zu spät kommt, bekommt das zuletzt fertig
 *        gewordene Byte ohne Warten, die dazwischen sind verloren (Handbuch §5.7).
 */
TEST_F(K5122Wait, UeberlaufVerliertBytes) {
    scharf();
    ASSERT_GE(bisMke(), 0);
    EXPECT_EQ(lies(), 0xA1);
    lauf(kByte * 10);                          // zehn Bytes verpasst
    int w = -1;
    lies(&w);
    EXPECT_EQ(w, 0) << "ein fertiges Byte liegt schon im Daten-PIO";
    lies(&w);
    EXPECT_GT(w, 0) << "das nächste kommt erst am Fensterende";
    EXPECT_LE(w, kByte);
}

/**
 * @test K5122Wait.DatenfeldSchreibenUndZuruecklesen
 * @brief Wie der Schreibpfad des BIOS (EA94H): Kopf lesen, Lücke abzählen, `/WE` = 0,
 *        Sync 00H, 3 × A1H, FBH, 1024 Datenbytes, CRC, `/WE` = 1.  Der Sektor, dessen
 *        Kopf zuletzt durchlief, trägt danach die neuen Daten — und liest sich im
 *        selben Transfer zurück.
 */
TEST_F(K5122Wait, DatenfeldSchreibenUndZuruecklesen) {
    scharf();
    ASSERT_GE(bisMke(), 0);
    const std::vector<uint8_t> k = kopf();
    ASSERT_EQ(k[0], 0xFE);
    const uint8_t sektor = k[3];
    for (int i = 0; i < 2 + 22; ++i) lies();         // CRC + Lücke 2
    card.ioWrite(0x10, 0x84);                         // /WE = 0 (sonst wie 85H)
    for (int i = 0; i < 12; ++i) schreib(0x00);
    for (int i = 0; i < 3; ++i) schreib(0xA1);
    schreib(0xFB);
    for (int i = 0; i < 1024; ++i) schreib(0x5A);
    schreib(0x12); schreib(0x34);                     // CRC (rechnet der Emulator neu)
    schreib(0x4E);
    card.ioWrite(0x10, 0x85);                         // /WE = 1 ⇒ übernehmen

    const auto sektoren = TrackCodec::parseTrack(card.drive(0).track(0));
    bool gefunden = false;
    for (const auto& s : sektoren) {
        if (s.id != sektor) {
            EXPECT_EQ(s.data[0], muster(0, 0, s.id)) << "fremder Sektor verändert";
            continue;
        }
        gefunden = true;
        ASSERT_EQ(s.data.size(), 1024u);
        EXPECT_EQ(s.data[0], 0x5A);
        EXPECT_EQ(s.data[1023], 0x5A);
        EXPECT_TRUE(s.data_crc_ok);
    }
    EXPECT_TRUE(gefunden);

    // Im selben Transfer zurücklesen: dieser Sektor, nächste Umdrehung.
    for (int runde = 0; runde < 12; ++runde) {
        card.ioWrite(0x10, 0xA5);
        card.ioWrite(0x10, 0x85);
        ASSERT_GE(bisMke(), 0);
        const std::vector<uint8_t> m = kopf();
        if (m[0] == 0xFE && m[3] == sektor) {
            card.ioWrite(0x10, 0xA5);
            card.ioWrite(0x10, 0x85);
            ASSERT_GE(bisMke(), 0);
            const std::vector<uint8_t> d = kopf();
            EXPECT_EQ(d[0], 0xFB);
            EXPECT_EQ(d[1], 0x5A);
            return;
        }
    }
    FAIL() << "geschriebener Sektor nicht wiedergefunden";
}

/**
 * @test K5122Wait.BusRqWegUnberuehrt
 * @brief Vorgabe ist `/BUSRQ` (A5120): ohne Umschalten gibt es weder MKE noch Wartetakte.
 */
TEST(K5122WaitVorgabe, BusRqWegUnberuehrt) {
    K1520Bus bus;
    K5122 card{bus};
    EXPECT_EQ(card.synchronisation(), K5122::Synchronisation::BusRq);
    card.ioWrite(0x10, 0x85);
    card.ioRead(0x16);
    EXPECT_EQ(bus.takeWaitCycles(), 0);
    EXPECT_FALSE(card.markeErkannt());
}
