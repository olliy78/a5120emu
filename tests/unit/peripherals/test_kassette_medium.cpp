/**
 * @file test_kassette_medium.cpp
 * @brief Kassettenband als Medium: CRC-Hilfen, Geometrie, Blockfolge, Ueberschreiben, Wenden
 *        (doc/design/29_kassette.md §5, doc/kassette/treiber.md, AP K3).
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/peripherals/kassette/kassetten_medium.h"

using namespace kassette;

namespace {

std::vector<uint8_t> daten(size_t n, uint8_t start = 0) {
    std::vector<uint8_t> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(start + i);
    return d;
}

/// Medium mit drei Bloecken à 100 Datenbytes auf Seite A.
KassettenMedium dreiBloecke() {
    KassettenMedium m;
    m.haengeAn(Seite::A, m.geometrie().initialluecke_um, blockBauen(daten(100, 0)));
    m.haengeAn(Seite::A, 20'300, blockBauen(daten(100, 100)));
    m.haengeAn(Seite::A, 20'300, blockBauen(daten(100, 200)));
    return m;
}

}  // namespace

// ─── CRC und Blockhilfen ─────────────────────────────────────────────────────

TEST(KassettenMedium, Crc16ArcPruefwert) {
    const std::string s = "123456789";
    EXPECT_EQ(crc16_arc(reinterpret_cast<const uint8_t*>(s.data()), s.size()), 0xBB3D);
    EXPECT_EQ(crc16_arc(nullptr, 0), 0x0000);
}

TEST(KassettenMedium, Crc16ArcAbschnittsweise) {
    const auto d = daten(50);
    const uint16_t ganz = crc16_arc(d);
    const uint16_t teil = crc16_arc(d.data() + 20, 30, crc16_arc(d.data(), 20));
    EXPECT_EQ(ganz, teil);
}

TEST(KassettenMedium, BlockAufbauPraeambelDatenCrcPostambel) {
    const auto d = daten(5, 0x10);
    const auto b = blockBauen(d);
    ASSERT_EQ(b.size(), 5u + 4u);
    EXPECT_EQ(b.front(), 0xAA);
    EXPECT_EQ(b.back(), 0xAA);
    EXPECT_TRUE(std::equal(d.begin(), d.end(), b.begin() + 1));
    const uint16_t crc = crc16_arc(d);
    EXPECT_EQ(b[6], crc & 0xFF) << "niedriges Byte zuerst";
    EXPECT_EQ(b[7], crc >> 8);
    EXPECT_TRUE(crcStimmt(b));
}

TEST(KassettenMedium, BandmarkeIstBlockOhneDaten) {
    const auto m = bandmarkeBytes();
    EXPECT_EQ(m, (std::vector<uint8_t>{0xAA, 0x00, 0x00, 0xAA}));
    EXPECT_TRUE(istBandmarke(m));
    EXPECT_TRUE(crcStimmt(m));
    EXPECT_FALSE(istBandmarke(blockBauen(daten(1))));
    EXPECT_FALSE(istBandmarke({0xAA, 0x00, 0x00}));
    Block b;
    b.bytes = m;
    EXPECT_TRUE(b.bandmarke());
}

TEST(KassettenMedium, FalscheCrcUndFremdePraeambelLassenSichSpeichern) {
    KassettenMedium m;
    auto kaputt = blockBauen(daten(20));
    kaputt[3] ^= 0x01;  // Datenbyte kippt: CRC stimmt nicht mehr
    EXPECT_FALSE(crcStimmt(kaputt));
    auto fremd = blockBauen(daten(20), 0xAB, 0x55);  // EGOS/K5261 lassen ABH als Praeambel zu
    ASSERT_TRUE(m.haengeAn(Seite::A, 40'000, kaputt));
    ASSERT_TRUE(m.haengeAn(Seite::A, 20'000, fremd));
    EXPECT_EQ(m.block(Seite::A, 0).bytes, kaputt);
    EXPECT_EQ(m.block(Seite::A, 1).bytes, fremd);
    EXPECT_EQ(m.block(Seite::A, 1).bytes.front(), 0xAB);
}

// ─── Geometrie ───────────────────────────────────────────────────────────────

TEST(KassettenMedium, BlockLaengeAusDichte) {
    KassettenMedium m;
    // 256 Bytes · 8 Bit / 31,5 Bit/mm = 65,016 mm
    EXPECT_NEAR(static_cast<double>(m.laengeVonBytes(256)), 65016.0, 1.0);
    EXPECT_EQ(m.laengeVonBytes(0), 0);
    KassettenMedium dicht(Geometrie{}, 32'000);
    EXPECT_EQ(dicht.laengeVonBytes(4), 1000) << "4 Byte = 32 Bit bei 32 Bit/mm = 1 mm";
}

TEST(KassettenMedium, MarkenUndVorspannLiegenInDerRichtigenReihenfolge) {
    KassettenMedium m;
    const Geometrie& g = m.geometrie();
    EXPECT_EQ(m.botAnfang(), g.vorspann_um + g.marken_abstand_um);
    EXPECT_EQ(m.botEnde(), m.botAnfang() + g.marke_um);
    EXPECT_EQ(m.magnetbandEnde(), int64_t(g.laenge_um) - g.vorspann_um);
    EXPECT_LT(m.botEnde(), m.eotAnfang());
    EXPECT_EQ(m.eotEnde() + g.marken_abstand_um, m.magnetbandEnde());
}

TEST(KassettenMedium, AebAktivInVorspannUndAnDenMarken) {
    KassettenMedium m;
    EXPECT_TRUE(m.aebAktiv(0)) << "Anfangsvorspann";
    EXPECT_TRUE(m.aebAktiv(m.geometrie().vorspann_um - 1));
    EXPECT_FALSE(m.aebAktiv(m.geometrie().vorspann_um)) << "Magnetband";
    EXPECT_FALSE(m.aebAktiv(m.botAnfang() - 1));
    EXPECT_TRUE(m.aebAktiv(m.botAnfang())) << "BOT-Marke";
    EXPECT_TRUE(m.aebAktiv(m.botEnde() - 1));
    EXPECT_FALSE(m.aebAktiv(m.botEnde()));
    EXPECT_FALSE(m.aebAktiv(10'000'000));
    EXPECT_TRUE(m.aebAktiv(m.eotAnfang())) << "EOT-Marke";
    EXPECT_FALSE(m.aebAktiv(m.eotEnde()));
    EXPECT_TRUE(m.aebAktiv(m.magnetbandEnde())) << "Endvorspann";
    EXPECT_TRUE(m.aebAktiv(m.geometrie().laenge_um - 1));
}

TEST(KassettenMedium, ErsterBlockLiegtHinterDemVorspannWeitGenugFuerDasLadeRom) {
    // treiber.md §6.1: das ROM scharf schaltet ≈ 163 mm hinter dem Vorspann; der erste Block muss dahinter liegen.
    const KassettenMedium m = dreiBloecke();
    EXPECT_GT(m.blockStart(Seite::A, 0) - m.geometrie().vorspann_um, 163'000);
}

// ─── Blockfolge ──────────────────────────────────────────────────────────────

TEST(KassettenMedium, AnhaengenLegtBloeckeMitLueckenHintereinander) {
    const KassettenMedium m = dreiBloecke();
    ASSERT_EQ(m.anzahlBloecke(Seite::A), 3u);
    EXPECT_EQ(m.anzahlBloecke(Seite::B), 0u);
    const int64_t laenge = m.laengeVonBytes(104);
    EXPECT_EQ(m.blockStart(Seite::A, 0), m.botEnde() + m.geometrie().initialluecke_um);
    EXPECT_EQ(m.blockEnde(Seite::A, 0), m.blockStart(Seite::A, 0) + laenge);
    EXPECT_EQ(m.blockStart(Seite::A, 1), m.blockEnde(Seite::A, 0) + 20'300);
    EXPECT_EQ(m.blockStart(Seite::A, 2), m.blockEnde(Seite::A, 1) + 20'300);
    EXPECT_EQ(m.datenEnde(Seite::A), m.blockEnde(Seite::A, 2));
    EXPECT_EQ(m.datenEnde(Seite::B), m.botEnde()) << "leere Seite";
}

TEST(KassettenMedium, SuchenNachPosition) {
    const KassettenMedium m = dreiBloecke();
    const Seite A = Seite::A;
    EXPECT_EQ(m.blockBei(A, m.blockStart(A, 1)), 1);
    EXPECT_EQ(m.blockBei(A, m.blockEnde(A, 1) - 1), 1);
    EXPECT_EQ(m.blockBei(A, m.blockEnde(A, 1)), -1) << "Luecke";
    EXPECT_EQ(m.blockBei(A, 0), -1);
    EXPECT_EQ(m.ersterBlockAb(A, 0), 0u);
    EXPECT_EQ(m.ersterBlockAb(A, m.blockStart(A, 1)), 1u);
    EXPECT_EQ(m.ersterBlockAb(A, m.blockStart(A, 1) + 1), 2u);
    EXPECT_EQ(m.ersterBlockAb(A, m.blockStart(A, 2) + 1), 3u);
    EXPECT_EQ(m.letzterBlockBis(A, m.blockEnde(A, 1)), 1);
    EXPECT_EQ(m.letzterBlockBis(A, m.blockEnde(A, 1) - 1), 0);
    EXPECT_EQ(m.letzterBlockBis(A, m.blockEnde(A, 0) - 1), -1);
    EXPECT_EQ(m.letzterBlockBis(A, 100'000'000), 2);
}

TEST(KassettenMedium, AnhaengenWeistUnzulaessigesAb) {
    KassettenMedium m;
    EXPECT_FALSE(m.haengeAn(Seite::A, 0, {}));
    EXPECT_FALSE(m.haengeAn(Seite::A, 0, std::vector<uint8_t>(kMaxBlockBytes + 1, 0)));
    EXPECT_TRUE(m.haengeAn(Seite::A, 0, std::vector<uint8_t>(kMaxBlockBytes, 0)));
    EXPECT_FALSE(m.haengeAn(Seite::A, 90'000'000, blockBauen(daten(10)))) << "hinter dem Magnetbandende";
    EXPECT_EQ(m.anzahlBloecke(Seite::A), 1u);
}

// ─── Schreiben und Loeschen ───────────────────────────────────────────────────

TEST(KassettenMedium, SchreibenGleicherLaengeErsetztInPlaceUndLaesstNachbarnStehen) {
    KassettenMedium m = dreiBloecke();
    const int64_t s1 = m.blockStart(Seite::A, 1), s2 = m.blockStart(Seite::A, 2);
    const auto neu = blockBauen(daten(100, 77));
    ASSERT_TRUE(m.schreibeBlock(Seite::A, s1, neu));
    ASSERT_EQ(m.anzahlBloecke(Seite::A), 3u);
    EXPECT_EQ(m.block(Seite::A, 1).bytes, neu);
    EXPECT_EQ(m.blockStart(Seite::A, 1), s1);
    EXPECT_EQ(m.blockStart(Seite::A, 2), s2) << "spaetere Bloecke behalten ihre Lage auf dem Band";
}

TEST(KassettenMedium, SchreibenInDieLueckeFuegtEin) {
    KassettenMedium m = dreiBloecke();
    const Seite A = Seite::A;
    const int64_t s2 = m.blockStart(A, 2);
    const auto kurz = blockBauen(daten(10, 9));
    const int64_t ort = m.blockEnde(A, 1) + 5'000;
    ASSERT_TRUE(m.schreibeBlock(A, ort, kurz));
    ASSERT_EQ(m.anzahlBloecke(A), 4u);
    EXPECT_EQ(m.blockStart(A, 2), ort);
    EXPECT_EQ(m.block(A, 2).bytes, kurz);
    EXPECT_EQ(m.blockStart(A, 3), s2);
}

TEST(KassettenMedium, UeberlappendesSchreibenZerstoertDieBetroffenenBloecke) {
    KassettenMedium m = dreiBloecke();
    const Seite A = Seite::A;
    const int64_t s2 = m.blockStart(A, 2);
    // Ein langer Block ab Mitte von Block 1 reicht in Block 2 hinein: beide gehen verloren.
    const int64_t ort = (m.blockStart(A, 1) + m.blockEnde(A, 1)) / 2;
    ASSERT_TRUE(m.schreibeBlock(A, ort, blockBauen(daten(200, 5))));
    ASSERT_EQ(m.anzahlBloecke(A), 2u) << "Block 0 und der neue; 1 und 2 sind ueberschrieben";
    EXPECT_EQ(m.blockStart(A, 1), ort);
    EXPECT_LT(m.blockEnde(A, 0), ort);
    EXPECT_GT(m.blockEnde(A, 1), s2);
}

TEST(KassettenMedium, SchreibenWeistAuszerhalbDesBeschreibbarenBandsAb) {
    KassettenMedium m = dreiBloecke();
    const auto rev = m.revision();
    EXPECT_FALSE(m.schreibeBlock(Seite::A, m.botEnde() - 1, blockBauen(daten(4)))) << "auf der BOT-Marke";
    EXPECT_FALSE(m.schreibeBlock(Seite::A, m.magnetbandEnde() - 10, blockBauen(daten(100)))) << "ragt ueber das Magnetband";
    EXPECT_FALSE(m.schreibeBlock(Seite::A, m.botEnde(), {}));
    EXPECT_EQ(m.revision(), rev) << "ein abgewiesenes Schreiben aendert nichts";
    EXPECT_EQ(m.anzahlBloecke(Seite::A), 3u);
}

TEST(KassettenMedium, LoeschenZerstoertNurUeberlappendeBloecke) {
    KassettenMedium m = dreiBloecke();
    const Seite A = Seite::A;
    const int64_t s2 = m.blockStart(A, 2);
    const int64_t b0 = m.block(A, 0).luecke_vor_um;
    EXPECT_EQ(m.loesche(A, m.blockStart(A, 1) + 100, m.blockStart(A, 1) + 200), 1u);
    ASSERT_EQ(m.anzahlBloecke(A), 2u);
    EXPECT_EQ(m.block(A, 0).luecke_vor_um, b0);
    EXPECT_EQ(m.blockStart(A, 1), s2) << "der Nachfolger bleibt, wo er war";
    EXPECT_EQ(m.loesche(A, m.blockEnde(A, 0), m.blockStart(A, 1)), 0u) << "nur Luecke: nichts zu loeschen";
    EXPECT_EQ(m.loesche(A, 10, 10), 0u);
}

TEST(KassettenMedium, LeereSeiteBehaeltSchreibschutz) {
    KassettenMedium m = dreiBloecke();
    m.setSchreibschutz(Seite::A, true);
    m.leereSeite(Seite::A);
    EXPECT_EQ(m.anzahlBloecke(Seite::A), 0u);
    EXPECT_TRUE(m.schreibgeschuetzt(Seite::A));
    EXPECT_FALSE(m.schreibgeschuetzt(Seite::B)) << "Schreibsperre gilt je Seite";
}

// ─── Wenden, Position, Revision ──────────────────────────────────────────────

TEST(KassettenMedium, WendenSpiegeltDiePositionUndTauschtDieSeite) {
    KassettenMedium m = dreiBloecke();
    m.haengeAn(Seite::B, 50'000, blockBauen(daten(10)));
    m.setPosition(10'000'000);
    EXPECT_EQ(m.aktiveSeite(), Seite::A);
    m.wenden();
    EXPECT_EQ(m.aktiveSeite(), Seite::B);
    EXPECT_EQ(m.positionUm(), int64_t(m.geometrie().laenge_um) - 10'000'000);
    m.wenden();
    EXPECT_EQ(m.aktiveSeite(), Seite::A);
    EXPECT_EQ(m.positionUm(), 10'000'000) << "zweimal wenden = Ausgangsstellung";
    EXPECT_EQ(m.anzahlBloecke(Seite::A), 3u);
    EXPECT_EQ(m.anzahlBloecke(Seite::B), 1u) << "die Seiten haben getrennten Inhalt";
}

TEST(KassettenMedium, PositionWirdBegrenzt) {
    KassettenMedium m;
    m.setPosition(-5);
    EXPECT_EQ(m.positionUm(), 0);
    m.setPosition(1'000'000'000);
    EXPECT_EQ(m.positionUm(), int64_t(m.geometrie().laenge_um));
}

TEST(KassettenMedium, RevisionZaehltInhaltNichtPosition) {
    KassettenMedium m;
    const auto r0 = m.revision();
    m.setPosition(1234);
    m.wenden();
    m.wenden();
    EXPECT_EQ(m.revision(), r0);
    m.haengeAn(Seite::A, 40'000, blockBauen(daten(4)));
    EXPECT_GT(m.revision(), r0);
    const auto r1 = m.revision();
    m.setSchreibschutz(Seite::B, true);
    EXPECT_GT(m.revision(), r1);
    const auto r2 = m.revision();
    m.setSchreibschutz(Seite::B, true);
    EXPECT_EQ(m.revision(), r2) << "gleicher Wert ist keine Aenderung";
}

TEST(KassettenMedium, GleichInhaltIgnoriertDiePosition) {
    KassettenMedium a = dreiBloecke(), b = dreiBloecke();
    b.setPosition(77);
    EXPECT_TRUE(a.gleichInhalt(b));
    b.setSchreibschutz(Seite::A, true);
    EXPECT_FALSE(a.gleichInhalt(b));
}
