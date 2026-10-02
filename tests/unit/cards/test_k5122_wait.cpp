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
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "core/bus/k1520_bus.h"
#include "core/cards/k5122/k5122.h"
#include "core/logger.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/disk_image.h"
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
 * @test K5122Wait.MkeLowAktiv_InvertiertNurTorBBit1
 * @brief PRG 710: Marken-FF an Tor B Bit1 low-aktiv (Ruhe 1, erkannt 0), AP-P1c.  Die
 *        Vorgabe (high-aktiv) bleibt unverändert; `markeErkannt()` ist weiter logisch.
 */
TEST_F(K5122Wait, MkeLowAktiv_InvertiertNurTorBBit1) {
    EXPECT_FALSE(card.mkeLowAktiv()) << "Vorgabe high-aktiv";
    card.setMkeLowAktiv(true);
    scharf();
    EXPECT_FALSE(card.markeErkannt());
    EXPECT_EQ(card.ioRead(0x12) & 0x02, 0x02) << "Ruhepegel high";
    ASSERT_GE(bisMke(), 0);
    EXPECT_TRUE(card.markeErkannt());
    EXPECT_EQ(card.ioRead(0x12) & 0x02, 0x00) << "erkannt = low";
    card.ioWrite(0x10, 0x8D);                      // /STR = 1: zurück
    EXPECT_EQ(card.ioRead(0x12) & 0x02, 0x02);
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
 * @test K5122Wait.AusSektorenGebauteSpurHatNormluecken
 * @brief Eine Spur ohne eigene Aufzeichnung (hier aus einem `.img`, `bitcells` = 0) liegt
 *        mit den Normlücken von FORMAT.COM unter dem Kopf: 3 × A1, hinter dem Kennfeld
 *        22 × 4E + 12 × 00.  Mit den knappen `gapsFor()`-Lücken (11 × 4E) wäre die
 *        A1-Gruppe des Datenfelds schon vorbei, wenn das BIOS nach (F780H) − 6 Lückenbytes
 *        neu scharf macht — `.img`-Disketten gäben „BAD SECTOR“ (AP-E4f).
 */
TEST_F(K5122Wait, AusSektorenGebauteSpurHatNormluecken) {
    ASSERT_EQ(card.drive(0).track(0).bitcells, 0u);
    scharf();
    ASSERT_GE(bisMke(), 0);
    EXPECT_EQ(lies(), 0xA1);
    EXPECT_EQ(lies(), 0xA1);
    EXPECT_EQ(lies(), 0xA1);
    EXPECT_EQ(lies(), 0xFE);
    for (int i = 0; i < 4 + 2; ++i) lies();          // C H R N + CRC
    for (int i = 0; i < 22; ++i) EXPECT_EQ(lies(), 0x4E) << i;
    for (int i = 0; i < 12; ++i) EXPECT_EQ(lies(), 0x00) << i;
    EXPECT_EQ(lies(), 0xA1);
}

// ─── Ganze Spur schreiben (FORMAT.COM des K8915, AP-E4f) ─────────────────────

namespace {

/// Eine Leerdiskette (unformatiert, 80 × 2) in Laufwerk 0 — der Ausgangszustand von
/// FORMAT.COM.  Sonst wie K5122Wait.
class K5122WaitFormat : public ::testing::Test {
protected:
    K1520Bus bus;
    K5122    card{bus, {builtinDriveProfile("K5601"), builtinDriveProfile("K5601"),
                        builtinDriveProfile("none"), builtinDriveProfile("none")}, kHz};

    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        card.setSynchronisation(K5122::Synchronisation::Wait);
        ASSERT_TRUE(card.mountDisk(0, DiskImage::createBlank(80, 2), false));
        card.ioWrite(0x10, 0xBB);
        card.ioWrite(0x18, 0xEE);
        lauf(20'000);
    }
    void lauf(int takte) { card.update(takte); }
    uint8_t lies() {
        const uint8_t b = card.ioRead(0x16);
        if (const int w = bus.takeWaitCycles()) card.update(w);
        return b;
    }
    void schreib(uint8_t b) {
        card.ioWrite(0x14, b);
        if (const int w = bus.takeWaitCycles()) card.update(w);
    }
    /// Bis kurz hinter den nächsten Indexpuls — dort setzt FORMAT.COM im Index-ISR ein.
    void bisIndex() {
        int vorher = card.debugState().indexAccum;
        for (int t = 0; t < 600'000; t += 10) {
            lauf(10);
            const int jetzt = card.debugState().indexAccum;
            if (jetzt < vorher) return;
            vorher = jetzt;
        }
        FAIL() << "kein Index";
    }
    long bisMke(long frist = 600'000) {
        for (long t = 0; t < frist; t += 10) {
            if (card.markeErkannt()) return t;
            lauf(10);
        }
        return -1;
    }

    /// Der Rohstrom einer Spur, wie FORMAT.COM (Verfahren 24, 5 × 1024) ihn vorbaut:
    /// Lücke, Indexmarke C2 C2 C2 FC, je Sektor 3 × A1 + Kennfeld mit CRC, Lücke 2
    /// (22 × 4E + 12 × 00), 3 × A1 + Datenfeld mit CRC, Lücke 3 (116 × 4E).
    static std::vector<uint8_t> rohstrom(uint8_t c, uint8_t h, uint8_t fuell) {
        std::vector<uint8_t> v;
        auto n = [&](uint8_t b, int k) { for (int i = 0; i < k; ++i) v.push_back(b); };
        auto crc = [&](size_t von) {
            const uint16_t x = TrackCodec::crc16(v.data() + von, v.size() - von, 0xFF, 0xFF);
            v.push_back(static_cast<uint8_t>(x >> 8));
            v.push_back(static_cast<uint8_t>(x & 0xFF));
        };
        n(0x4E, 80); n(0x00, 12); n(0xC2, 3); v.push_back(0xFC); n(0x4E, 50);
        for (uint8_t s = 1; s <= 5; ++s) {
            n(0x00, 12);
            const size_t k = v.size();
            n(0xA1, 3); v.push_back(0xFE); v.push_back(c); v.push_back(h); v.push_back(s);
            v.push_back(3);
            crc(k);
            n(0x4E, 22); n(0x00, 12);
            const size_t d = v.size();
            n(0xA1, 3); v.push_back(0xFB); n(static_cast<uint8_t>(fuell + s), 1024);
            crc(d);
            n(0x4E, 116);
        }
        return v;
    }

    /// FORMAT.COM-Schreibkern (1AEDH): am Index `/WE` = 0 mit dem Schreibsteuerwort
    /// (90H = Kopf 1, 94H = Kopf 0), Strom Byte für Byte, Lücke bis zum nächsten Index,
    /// dann `/WE` = `/STR` = 1.  Liefert, wie viele Bytes geschrieben wurden.
    size_t spurSchreiben(const std::vector<uint8_t>& strom, uint8_t steuerwort) {
        bisIndex();
        card.ioWrite(0x10, steuerwort);
        size_t n = 0;
        for (uint8_t b : strom) { schreib(b); ++n; }
        int vorher = card.debugState().indexAccum;
        for (;;) {                                     // Lücke bis zum Index (3580H)
            schreib(0x4E); ++n;
            const int jetzt = card.debugState().indexAccum;
            if (jetzt < vorher) break;
            vorher = jetzt;
        }
        card.ioWrite(0x10, 0xBF);
        return n;
    }
};

/// Erste Position von @p muster in der Spur (SIZE_MAX = keine).
size_t finde(const TrackImage& t, const std::vector<uint8_t>& muster) {
    const auto it = std::search(t.bytes.begin(), t.bytes.end(), muster.begin(), muster.end());
    return it == t.bytes.end() ? SIZE_MAX : static_cast<size_t>(it - t.bytes.begin());
}

}  // namespace

/**
 * @test K5122WaitFormat.GanzeSpurLiegtByteFuerByteAufDerScheibe
 * @brief FORMAT.COM des K8915 schreibt eine ganze Spur im `/WAIT`-Betrieb: jedes Byte
 *        wartet auf sein Fenster, die Spur geht beim Loslassen von `/WE` ins Medium —
 *        an die Seite, die das Schreibsteuerwort wählt (/FR), und **so, wie sie
 *        geschrieben wurde**: Indexmarke, 3 × A1, Lücke 2 mit 22 × 4E bleiben stehen
 *        (FORMAT.COMs Prüf-Lesen vergleicht die ganze Spur).  Die Sektoren sind
 *        lesbar, beide CRCs stimmen, die andere Seite bleibt leer.
 */
TEST_F(K5122WaitFormat, GanzeSpurLiegtByteFuerByteAufDerScheibe) {
    const std::vector<uint8_t> strom = rohstrom(0, 0, 0x40);
    const size_t n = spurSchreiben(strom, 0x94);             // /WE = 0, /STR = 0, Kopf 0
    EXPECT_GT(n, strom.size());
    EXPECT_LE(n, 6400u) << "eine Umdrehung, nicht mehr";

    const TrackImage& t = card.drive(0).track(0);
    ASSERT_FALSE(t.empty());
    EXPECT_EQ(t.encoding, Encoding::MFM);
    const auto sek = TrackCodec::parseTrack(t);
    ASSERT_EQ(sek.size(), 5u);
    for (uint8_t s = 1; s <= 5; ++s) {
        const auto& x = sek[s - 1];
        EXPECT_EQ(x.id, s);
        EXPECT_EQ(x.size, 1024);
        EXPECT_TRUE(x.id_crc_ok);
        EXPECT_TRUE(x.data_crc_ok);
        EXPECT_EQ(x.data[0], 0x40 + s);
    }
    const size_t iam = finde(t, {0xC2, 0xC2, 0xC2, 0xFC});
    ASSERT_NE(iam, SIZE_MAX) << "Indexmarke fehlt";
    EXPECT_EQ(t.marks[iam + 3], MarkType::Index);
    const size_t k1 = finde(t, {0xA1, 0xA1, 0xA1, 0xFE, 0x00, 0x00, 0x01, 0x03});
    ASSERT_NE(k1, SIZE_MAX);
    EXPECT_EQ(t.marks[k1 + 3], MarkType::Id);
    for (size_t i = 0; i < 22; ++i) EXPECT_EQ(t.bytes[k1 + 10 + i], 0x4E) << i;
    EXPECT_EQ(t.bytes[k1 + 32], 0x00) << "Lücke 2 hat genau 22 × 4E";
    EXPECT_TRUE(card.drive(0).track(1).empty()) << "Kopf 1 unberührt";

    const auto st = card.debugState();
    EXPECT_TRUE(st.waitBetrieb);
    EXPECT_EQ(st.waitSpuren, 1u);
    EXPECT_FALSE(st.writeMode);
}

/**
 * @test K5122WaitFormat.PruefLesenWieFormatCom
 * @brief Das Prüf-Lesen von FORMAT.COM (ISR 1B5CH): MK = 1 (Steuerwort 87H) erkennt die
 *        MFM-Indexmarke — das erste Byte nach MKE ist das erkannte C2, dann C2 C2 FC;
 *        MK = 0 (85H) erkennt A1 — dann A1 A1 A1 FE und das Kennfeld samt CRC, danach
 *        die geschriebene Lücke.  Die Byteperiode folgt der Spur (MFM, 78 Takte), nicht
 *        dem MK-Bit.
 */
TEST_F(K5122WaitFormat, PruefLesenWieFormatCom) {
    const std::vector<uint8_t> strom = rohstrom(0, 0, 0x40);
    spurSchreiben(strom, 0x94);

    bisIndex();
    card.ioWrite(0x10, 0x87);                                  // MK = 1, /STR = 0, MR = 0
    ASSERT_GE(bisMke(), 0) << "keine Indexmarke erkannt";
    EXPECT_EQ(lies(), 0xC2);
    EXPECT_EQ(lies(), 0xC2);
    EXPECT_EQ(lies(), 0xC2);
    EXPECT_EQ(lies(), 0xFC);
    for (int i = 0; i < 50; ++i) ASSERT_EQ(lies(), 0x4E) << i;
    card.ioRead(0x16);
    EXPECT_EQ(bus.takeWaitCycles(), kByte) << "MFM-Byteperiode auch bei MK = 1";

    card.ioWrite(0x10, 0xA7);                                  // MR = 1
    card.ioWrite(0x10, 0xAF);                                  // /STR = 1
    card.ioWrite(0x10, 0x85);                                  // MK = 0: A1 suchen
    ASSERT_GE(bisMke(), 0);
    const std::vector<uint8_t> soll = {0xA1, 0xA1, 0xA1, 0xFE, 0x00, 0x00, 0x01, 0x03};
    for (uint8_t b : soll) EXPECT_EQ(lies(), b);
    lies(); lies();                                            // ID-CRC
    for (int i = 0; i < 22; ++i) EXPECT_EQ(lies(), 0x4E) << i;
    EXPECT_EQ(lies(), 0x00);
}

/**
 * @test K5122WaitFormat.DatenfeldDanachAnOrtUndStelle
 * @brief Nach dem Formatieren schreibt das BIOS (DISGEN) ein Datenfeld: der Sektor
 *        bekommt die neuen Daten mit neuer CRC — die Spur behält Länge, Indexmarke und
 *        Lücken (TrackCodec::writeSector statt Neubau).
 */
TEST_F(K5122WaitFormat, DatenfeldDanachAnOrtUndStelle) {
    spurSchreiben(rohstrom(0, 0, 0x40), 0x94);
    const std::vector<uint8_t> vorher = card.drive(0).track(0).bytes;

    // BIOS-Weg: scharf, Kennfeld lesen, Lücke abzählen, /WE = 0, Datenfeld, /WE = 1.
    uint8_t sektor = 0;
    for (int versuch = 0; versuch < 10 && sektor != 3; ++versuch) {
        card.ioWrite(0x10, 0xA5);
        card.ioWrite(0x10, 0x85);
        ASSERT_GE(bisMke(), 0);
        uint8_t b = lies();
        while (b == 0xA1) b = lies();
        if (b != 0xFE) continue;
        lies(); lies(); sektor = lies(); lies();
    }
    ASSERT_EQ(sektor, 3);
    for (int i = 0; i < 2 + 22; ++i) lies();
    card.ioWrite(0x10, 0x84);
    for (int i = 0; i < 12; ++i) schreib(0x00);
    for (int i = 0; i < 3; ++i) schreib(0xA1);
    schreib(0xFB);
    for (int i = 0; i < 1024; ++i) schreib(0x77);
    schreib(0x12); schreib(0x34);
    schreib(0x4E);
    card.ioWrite(0x10, 0x85);

    const TrackImage& t = card.drive(0).track(0);
    ASSERT_EQ(t.bytes.size(), vorher.size()) << "Spurlänge bleibt";
    EXPECT_NE(finde(t, {0xC2, 0xC2, 0xC2, 0xFC}), SIZE_MAX) << "Indexmarke bleibt";
    for (const auto& s : TrackCodec::parseTrack(t)) {
        EXPECT_TRUE(s.data_crc_ok) << int(s.id);
        EXPECT_EQ(s.data[0], s.id == 3 ? 0x77 : 0x40 + s.id) << int(s.id);
    }
    const size_t k1 = finde(t, {0xA1, 0xA1, 0xA1, 0xFE, 0x00, 0x00, 0x01, 0x03});
    ASSERT_NE(k1, SIZE_MAX);
    EXPECT_EQ(std::vector<uint8_t>(t.bytes.begin(), t.bytes.begin() + static_cast<long>(k1)),
              std::vector<uint8_t>(vorher.begin(), vorher.begin() + static_cast<long>(k1)));
}

/**
 * @test K5122WaitFormat.SeiteAusDemSchreibsteuerwortUndSchreibschutz
 * @brief 90H (/FR = 0) schreibt Kopf 1, Kopf 0 bleibt leer.  Auf einer
 *        schreibgeschützten Diskette geht nichts auf die Scheibe (FORMAT.COM fragt /WP
 *        selbst ab, die Karte verwirft trotzdem).
 */
TEST_F(K5122WaitFormat, SeiteAusDemSchreibsteuerwortUndSchreibschutz) {
    spurSchreiben(rohstrom(0, 1, 0x10), 0x90);
    EXPECT_TRUE(card.drive(0).track(0).empty());
    const auto sek = TrackCodec::parseTrack(card.drive(0).track(1));
    ASSERT_EQ(sek.size(), 5u);
    EXPECT_EQ(sek[0].head, 1);

    card.setWriteProtect(0, true);
    spurSchreiben(rohstrom(0, 0, 0x10), 0x94);
    EXPECT_TRUE(card.drive(0).track(0).empty()) << "schreibgeschützt: nichts geschrieben";
    EXPECT_EQ(card.debugState().waitSpuren, 1u);
}

/**
 * @test K5122WaitFormat.WaehrendDesSchreibensZeigtDebugStateDenTransfer
 * @brief Befund E4d: im `/WAIT`-Betrieb bedeuten `transferring`/`writeMode` Datenfluss
 *        zum Lesen bzw. Schreiben (nicht das Streaming des BusRq-Wegs) — sonst sähe
 *        der Debugger nie einen Transfer.
 */
TEST_F(K5122WaitFormat, WaehrendDesSchreibensZeigtDebugStateDenTransfer) {
    EXPECT_FALSE(card.debugState().transferring) << "/STR = 1";
    card.ioWrite(0x10, 0x85);
    EXPECT_TRUE(card.debugState().transferring);
    EXPECT_FALSE(card.debugState().writeMode);
    card.ioWrite(0x10, 0x84);                                  // /WE = 0
    schreib(0x4E); schreib(0x4E);
    auto st = card.debugState();
    EXPECT_TRUE(st.writeMode);
    EXPECT_FALSE(st.transferring);
    EXPECT_EQ(st.waitSchreibBytes, 2u);
    card.ioWrite(0x10, 0x8D);                                  // /STR = 1 beendet
    st = card.debugState();
    EXPECT_FALSE(st.writeMode);
    EXPECT_FALSE(st.transferring);
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
