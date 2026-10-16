/**
 * @file test_kassetten_laufwerk.cpp
 * @brief Kassettenlaufwerk: Bereitschaft, Bandlauf mit Rampen, AEB/BMS, Lesen vor-/rueckwaerts,
 *        Schreiben mit Echo, Umspulen, Schreibschutz, Datei mit verzoegertem Zurueckschreiben
 *        (doc/kassette/k5200.md, doc/kassette/treiber.md, AP K3).
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/peripherals/kassette/k52_codec.h"
#include "core/peripherals/kassette/kassetten_laufwerk.h"
#include "tests/support/temp_path.h"

using namespace kassette;

namespace {

constexpr Seite A = Seite::A;

std::vector<uint8_t> daten(size_t n, uint8_t start = 0) {
    std::vector<uint8_t> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(start + i);
    return d;
}

std::shared_ptr<KassettenMedium> dreiBloecke() {
    auto m = std::make_shared<KassettenMedium>();
    m->haengeAn(A, m->geometrie().initialluecke_um, blockBauen(daten(100, 0)));
    m->haengeAn(A, 20'300, blockBauen(daten(100, 100)));
    m->haengeAn(A, 20'300, blockBauen(daten(100, 200)));
    return m;
}

/// Laufwerk + Kassette, nach dem Einlegen reserviert und angewaehlt.
struct Lauf {
    KassettenLaufwerk d;
    std::shared_ptr<KassettenMedium> m;

    explicit Lauf(std::shared_ptr<KassettenMedium> medium, ProfilId p = ProfilId::K5200_38) : d(p), m(std::move(medium)) {
        d.kassetteEinlegen(m);
    }
    void bereitMachen() {
        d.reservieren(true);
        d.anwaehlen(true);
        d.advance(0.31);
    }
    /// @p sekunden in Schritten von 0,5 ms laufen lassen und alle Bytes einsammeln.
    std::vector<uint8_t> laufe(double sekunden, std::vector<uint8_t>* ziel = nullptr) {
        std::vector<uint8_t> aus;
        std::vector<uint8_t>& o = ziel ? *ziel : aus;
        for (double t = 0; t < sekunden; t += 0.0005) {
            d.advance(0.0005);
            while (auto b = d.holeByte()) o.push_back(*b);
        }
        return aus;
    }
    /// Laufen, bis die Kopfstelle @p um_ziel (µm, vorwaerts) erreicht ist.
    std::vector<uint8_t> laufeBis(int64_t um_ziel, double hoechstens = 20.0) {
        std::vector<uint8_t> o;
        for (double t = 0; t < hoechstens && d.positionMm() * 1000.0 < static_cast<double>(um_ziel); t += 0.0005) {
            d.advance(0.0005);
            while (auto b = d.holeByte()) o.push_back(*b);
        }
        return o;
    }
    void halt() {
        d.vor(false);
        d.rueck(false);
        d.advance(0.2);
    }
};

std::vector<uint8_t> verkettet(const KassettenMedium& m, size_t von, size_t bis) {
    std::vector<uint8_t> r;
    for (size_t i = von; i < bis; ++i) r.insert(r.end(), m.block(A, i).bytes.begin(), m.block(A, i).bytes.end());
    return r;
}

uint8_t bitrev(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (b & (1 << i)) r |= static_cast<uint8_t>(0x80 >> i);
    return r;
}

/// Einen Block ab der aktuellen Kopfstelle schreiben (Laufwerk bereit, AUF an, Band laeuft).  Liefert das Echo.
std::vector<uint8_t> schreibe(Lauf& l, const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> echo;
    for (uint8_t b : bytes) {
        EXPECT_TRUE(l.d.schreibeByte(b));
        l.d.advance(l.d.profil().byteZeitS());
        while (auto e = l.d.holeByte()) echo.push_back(*e);
    }
    l.laufe(0.04, &echo);
    return echo;
}

}  // namespace

// ─── Profile ─────────────────────────────────────────────────────────────────

TEST(KassettenLaufwerk, ProfileHabenDieWerteDerUnterlagen) {
    const LaufwerksProfil& p38 = profil(ProfilId::K5200_38);
    EXPECT_DOUBLE_EQ(p38.v_arbeit_mm_s, 381.0);
    EXPECT_DOUBLE_EQ(p38.v_such_mm_s, 1500.0);
    EXPECT_DOUBLE_EQ(p38.bitrate_bit_s, 12000.0);
    EXPECT_TRUE(p38.azv_38);
    EXPECT_NEAR(p38.byteZeitS() * 1e6, 666.7, 0.1);
    const LaufwerksProfil& p19 = profil(ProfilId::K5200_19);
    EXPECT_DOUBLE_EQ(p19.bitrate_bit_s, 6000.0);
    EXPECT_FALSE(p19.azv_38);
    EXPECT_DOUBLE_EQ(profil(ProfilId::PK3).v_arbeit_mm_s, 254.0);
    EXPECT_DOUBLE_EQ(profil(ProfilId::SK1200).v_arbeit_mm_s, 47.6);
    EXPECT_DOUBLE_EQ(profil(ProfilId::LW1200).v_arbeit_mm_s, 47.6);
    EXPECT_EQ(alleProfile().size(), 5u);
}

TEST(KassettenLaufwerk, ProfilNachNameOhneGrossKleinschreibung) {
    ASSERT_NE(profilNachName("k5200-19"), nullptr);
    EXPECT_EQ(profilNachName("k5200-19")->id, ProfilId::K5200_19);
    EXPECT_EQ(profilNachName("PK3")->id, ProfilId::PK3);
    EXPECT_EQ(profilNachName("lw1200")->id, ProfilId::LW1200);
    EXPECT_EQ(profilNachName("gibtsnicht"), nullptr);
    for (ProfilId id : alleProfile()) EXPECT_EQ(profilNachName(profil(id).name)->id, id);
}

// ─── Bereitschaft ────────────────────────────────────────────────────────────

TEST(KassettenLaufwerk, OhneKassetteBleibtDasLaufwerkNichtBereit) {
    KassettenLaufwerk d;
    d.reservieren(true);
    d.anwaehlen(true);
    d.advance(1.0);
    EXPECT_TRUE(d.nib());
    EXPECT_TRUE(d.kng());
    EXPECT_FALSE(d.kassetteGeladen());
}

TEST(KassettenLaufwerk, ReservierenMachtNachAnlaufzeitBereit) {
    Lauf l(dreiBloecke());
    EXPECT_TRUE(l.d.nib());
    EXPECT_TRUE(l.d.kng()) << "ohne RES gilt die Kassette nicht als arbeitsbereit";
    l.d.reservieren(true);
    l.d.advance(0.29);
    EXPECT_TRUE(l.d.nib()) << "T1 = 300 ms";
    EXPECT_FALSE(l.d.kng());
    l.d.advance(0.02);
    EXPECT_FALSE(l.d.nib());
    EXPECT_TRUE(l.d.bereit());
    l.d.reservieren(false);
    EXPECT_TRUE(l.d.nib());
    l.d.reservieren(true);
    l.d.advance(0.1);
    EXPECT_TRUE(l.d.nib()) << "der Anlauf beginnt nach Freigabe von vorn";
}

TEST(KassettenLaufwerk, VorUndRueckZugleichMeldetFehlerBisRESFaellt) {
    Lauf l(dreiBloecke());
    l.bereitMachen();
    ASSERT_FALSE(l.d.nib());
    l.d.vor(true);
    l.d.rueck(true);
    EXPECT_TRUE(l.d.fehler());
    EXPECT_TRUE(l.d.nib());
    EXPECT_TRUE(l.d.kng());
    l.d.vor(false);
    l.d.rueck(false);
    l.d.advance(1.0);
    EXPECT_TRUE(l.d.nib()) << "bleibt bis RES zurueckgenommen wird";
    l.d.reservieren(false);
    EXPECT_FALSE(l.d.fehler());
    l.bereitMachen();
    EXPECT_FALSE(l.d.nib());
}

TEST(KassettenLaufwerk, OhneAnwahlBewegenSichBandsignaleNicht) {
    Lauf l(dreiBloecke());
    l.d.reservieren(true);
    l.d.advance(0.4);
    l.d.vor(true);
    l.d.advance(0.5);
    EXPECT_EQ(l.d.geschwindigkeitMmS(), 0.0);
    l.d.anwaehlen(true);
    l.d.advance(0.1);
    EXPECT_GT(l.d.geschwindigkeitMmS(), 0.0);
}

TEST(KassettenLaufwerk, SeiteSchreibsperreUndGeschwindigkeitswahlAlsLeitungen) {
    Lauf l(dreiBloecke());
    EXPECT_FALSE(l.d.seiteB());
    EXPECT_FALSE(l.d.schreibsperre());
    l.m->setSchreibschutz(A, true);
    EXPECT_TRUE(l.d.schreibsperre());
    EXPECT_TRUE(l.d.azv38());
    KassettenLaufwerk d19(ProfilId::K5200_19);
    EXPECT_FALSE(d19.azv38());
}

// ─── Bandlauf ────────────────────────────────────────────────────────────────

TEST(KassettenLaufwerk, AnlaufInEinerStartzeitUndBremsweg) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(30'000'000);
    l.bereitMachen();
    const double p0 = l.d.positionMm();
    l.d.vor(true);
    l.d.advance(0.018);
    EXPECT_NEAR(l.d.geschwindigkeitMmS(), 381.0, 0.01) << "t_start = 18 ms";
    EXPECT_NEAR(l.d.positionMm() - p0, 3.43, 0.02) << "Startweg ½·v·t (BD: 2,8 ± 1,5 mm)";
    l.d.advance(0.1);
    EXPECT_NEAR(l.d.geschwindigkeitMmS(), 381.0, 0.01);
    const double p1 = l.d.positionMm();
    l.d.vor(false);
    l.d.advance(0.1);
    EXPECT_EQ(l.d.geschwindigkeitMmS(), 0.0);
    EXPECT_NEAR(l.d.positionMm() - p1, 4.0, 0.05) << "Stoppweg 4,0 ± 1,0 mm";
}

TEST(KassettenLaufwerk, SuchlaufMit1500MmProSekunde) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(30'000'000);
    l.bereitMachen();
    l.d.hge(true);
    l.d.vor(true);
    l.d.advance(0.3);
    EXPECT_NEAR(l.d.geschwindigkeitMmS(), 1500.0, 0.01);
    const double p0 = l.d.positionMm();
    l.d.advance(1.0);
    EXPECT_NEAR(l.d.positionMm() - p0, 1500.0, 0.5);
    l.d.vor(false);
    l.d.advance(0.5);
    EXPECT_EQ(l.d.geschwindigkeitMmS(), 0.0);
}

TEST(KassettenLaufwerk, RichtungswechselLaeuftUeberDenStillstand) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(30'000'000);
    l.bereitMachen();
    l.d.vor(true);
    l.d.advance(0.1);
    ASSERT_GT(l.d.geschwindigkeitMmS(), 300.0);
    l.d.vor(false);
    l.d.rueck(true);
    l.d.advance(0.005);
    EXPECT_GT(l.d.geschwindigkeitMmS(), 0.0) << "erst bremsen";
    l.d.advance(0.2);
    EXPECT_NEAR(l.d.geschwindigkeitMmS(), -381.0, 0.01);
}

TEST(KassettenLaufwerk, AebWechseltAnVorspannUndBotMarke) {
    Lauf l(dreiBloecke());
    l.bereitMachen();
    EXPECT_TRUE(l.d.aebAktiv()) << "Anfangsvorspann";
    l.d.vor(true);
    // Lichtschranke liegt 14 mm vor der Kopfmitte: AEB faellt, wenn Kopf + 14 mm den Vorspann verlaesst.
    auto warteAufAeb = [&](bool soll) {
        for (int i = 0; i < 20000 && l.d.aebAktiv() != soll; ++i) l.d.advance(0.0005);
        ASSERT_EQ(l.d.aebAktiv(), soll);
    };
    warteAufAeb(false);
    EXPECT_NEAR(l.d.positionMm(), 1000.0 - 14.0, 1.0);
    warteAufAeb(true);
    EXPECT_NEAR(l.d.positionMm(), 1450.0 - 14.0, 1.0) << "BOT-Marke 450 mm hinter dem Vorspann";
    warteAufAeb(false);
    EXPECT_NEAR(l.d.positionMm(), 1470.0 - 14.0, 1.0) << "Marke ist 20 mm lang";
}

TEST(KassettenLaufwerk, BandendeHaeltDasBandAn) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(int64_t(m->geometrie().laenge_um) - 1'000'000);
    Lauf l(m);
    l.bereitMachen();
    l.d.vor(true);
    l.d.advance(4.0);
    EXPECT_EQ(l.d.geschwindigkeitMmS(), 0.0);
    EXPECT_DOUBLE_EQ(l.d.positionMm(), m->geometrie().laenge_um / 1000.0);
    EXPECT_TRUE(l.d.aebAktiv()) << "Endvorspann";
}

// ─── Blockmarkierung und Lesen ───────────────────────────────────────────────

TEST(KassettenLaufwerk, BmsIstEinsUeberDemBlockUndNullInDerLuecke) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) - 15'000);
    l.bereitMachen();
    EXPECT_FALSE(l.d.bms()) << "Band steht";
    l.d.vor(true);
    bool in_block = false, in_luecke = false;
    for (int i = 0; i < 2000; ++i) {
        l.d.advance(0.0005);
        const int64_t p = static_cast<int64_t>(l.d.positionMm() * 1000.0);
        const int b = l.m->blockBei(A, p);
        if (b == 1) {
            EXPECT_TRUE(l.d.bms());
            in_block = true;
        } else if (b == -1 && p > l.m->blockEnde(A, 0) + 1000 && p < l.m->blockStart(A, 1) - 1000) {
            EXPECT_FALSE(l.d.bms());
            in_luecke = true;
        }
    }
    EXPECT_TRUE(in_block);
    EXPECT_TRUE(in_luecke);
}

TEST(KassettenLaufwerk, VorwaertsLesenLiefertBlockBytesInSchreibreihenfolge) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) - 30'000);
    l.bereitMachen();
    l.d.vor(true);
    const auto gelesen = l.laufeBis(l.m->blockEnde(A, 2) + 5'000);
    EXPECT_EQ(gelesen, verkettet(*l.m, 0, 3));
}

TEST(KassettenLaufwerk, BytesKommenImBittaktDerBandlaenge) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) - 30'000);
    l.bereitMachen();
    l.d.vor(true);
    l.laufeBis(l.m->blockStart(A, 0));
    l.laufe(0.0001);  // erstes Byte noch unterwegs
    // Ein Byte dauert 666,7 µs; nach 10 ms muessen etwa 15 da sein, nie mehr als 16.
    std::vector<uint8_t> o;
    l.laufe(0.010, &o);
    EXPECT_GE(o.size(), 13u);
    EXPECT_LE(o.size(), 16u);
}

TEST(KassettenLaufwerk, EinBlockWirdNurGelesenWennDerKopfSeinenAnfangUeberfaehrt) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) + 5'000);  // mitten in Block 0
    l.bereitMachen();
    l.d.vor(true);
    const auto gelesen = l.laufeBis(l.m->blockEnde(A, 2) + 5'000);
    EXPECT_EQ(gelesen, verkettet(*l.m, 1, 3)) << "Block 0 wird uebergangen";
}

TEST(KassettenLaufwerk, AnhaltenMitteImBlockLiefertNurDieBisdahinKommendenBytes) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) - 30'000);
    l.bereitMachen();
    l.d.vor(true);
    auto o = l.laufeBis(l.m->blockStart(A, 0) + 10'000);
    l.halt();
    while (auto b = l.d.holeByte()) o.push_back(*b);
    EXPECT_GT(o.size(), 20u);
    EXPECT_LT(o.size(), 104u);
    EXPECT_TRUE(std::equal(o.begin(), o.end(), l.m->block(A, 0).bytes.begin()));
    // Nach dem Stillstand beginnt ein erneuter Lauf nicht mitten im Block weiterzulesen.
    l.d.vor(true);
    std::vector<uint8_t> weiter = l.laufeBis(l.m->blockEnde(A, 0) - 5'000);
    EXPECT_TRUE(weiter.empty());
}

TEST(KassettenLaufwerk, RueckwaertsLesenLiefertInvertierteBitgedrehteBytesVonHintenNachVorn) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockEnde(A, 2) + 30'000);
    l.bereitMachen();
    l.d.rueck(true);
    std::vector<uint8_t> gelesen;
    for (int i = 0; i < 20000 && l.d.positionMm() * 1000.0 > static_cast<double>(l.m->blockStart(A, 0) - 5'000); ++i) {
        l.d.advance(0.0005);
        while (auto b = l.d.holeByte()) gelesen.push_back(*b);
    }
    std::vector<uint8_t> soll;
    for (int i = 2; i >= 0; --i) {
        const auto& b = l.m->block(A, static_cast<size_t>(i)).bytes;
        for (size_t k = b.size(); k-- > 0;) soll.push_back(static_cast<uint8_t>(~bitrev(b[k])));
    }
    EXPECT_EQ(gelesen, soll);
}

TEST(KassettenLaufwerk, BandmarkeKommtRueckwaertsAlsAAFFFFAA) {
    auto m = std::make_shared<KassettenMedium>();
    m->haengeAn(A, 40'000, blockBauen(daten(10)));
    m->haengeAn(A, 20'000, bandmarkeBytes());
    m->setPosition(m->blockEnde(A, 1) + 30'000);
    Lauf l(m);
    l.bereitMachen();
    l.d.rueck(true);
    std::vector<uint8_t> gelesen;
    for (int i = 0; i < 4000 && l.d.positionMm() * 1000.0 > static_cast<double>(m->blockStart(A, 1) - 5'000); ++i) {
        l.d.advance(0.0005);
        while (auto b = l.d.holeByte()) gelesen.push_back(*b);
    }
    EXPECT_EQ(std::vector<uint8_t>(gelesen.begin(), gelesen.begin() + 4), (std::vector<uint8_t>{0xAA, 0xFF, 0xFF, 0xAA}));
}

TEST(KassettenLaufwerk, SuchlaufLiefertKeineDatenAberBmsJeBlock) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->botEnde());
    l.bereitMachen();
    l.d.hge(true);
    l.d.vor(true);
    int flanken = 0;
    bool vorher = false;
    std::vector<uint8_t> o;
    for (int i = 0; i < 20000 && l.d.positionMm() * 1000.0 < static_cast<double>(l.m->blockEnde(A, 2) + 5'000); ++i) {
        l.d.advance(0.0005);
        const bool b = l.d.bms();
        if (b && !vorher) ++flanken;
        vorher = b;
        while (auto x = l.d.holeByte()) o.push_back(*x);
    }
    EXPECT_EQ(flanken, 3) << "ein BMS-Impuls je vorbeilaufendem Block";
    EXPECT_TRUE(o.empty()) << "bei HGE ist der Kopf abgeschwenkt";
}

TEST(KassettenLaufwerk, OhneAnwahlOderMitAufzeichnenKommenKeineLesedaten) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 0) - 30'000);
    l.d.reservieren(true);
    l.d.advance(0.4);
    l.d.vor(true);  // nicht angewaehlt: Band steht
    EXPECT_TRUE(l.laufe(0.5).empty());
    l.d.anwaehlen(true);
    l.d.advance(0.01);
    l.d.wiedergabe(false);
    EXPECT_TRUE(l.laufe(0.3).empty()) << "WID aus: WGB gesperrt";
}

// ─── Umspulen ────────────────────────────────────────────────────────────────

TEST(KassettenLaufwerk, UmspulenLaeuftSelbsttaetigBisZumVorspannUndBlendetAebAus) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(10'000'000);
    l.bereitMachen();
    l.d.umspulen();
    EXPECT_TRUE(l.d.umspulenAktiv());
    EXPECT_TRUE(l.d.nib()) << "waehrend des Umspulens nicht bereit";
    EXPECT_FALSE(l.d.aebAktiv()) << "AEB ist ausgeblendet";
    l.d.reservieren(false);  // RES darf danach fallen: das Geraet spult zu Ende
    l.d.anwaehlen(false);
    double t = 0;
    while (l.d.umspulenAktiv() && t < 30.0) {
        l.d.advance(0.01);
        t += 0.01;
    }
    EXPECT_FALSE(l.d.umspulenAktiv());
    EXPECT_NEAR(t, 9.0 / 1.5, 1.0) << "9 m bis zum Vorspann bei 1,5 m/s";
    l.d.advance(1.0);
    EXPECT_EQ(l.d.geschwindigkeitMmS(), 0.0);
    EXPECT_LT(l.d.positionMm(), 1000.0) << "Kopf steht im Vorspann";
    l.d.reservieren(true);
    l.d.advance(0.31);
    EXPECT_TRUE(l.d.aebAktiv());
}

TEST(KassettenLaufwerk, UmspulenEinerVollenSeiteBleibtUnterDerRomFrist) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(m->eotAnfang());
    Lauf l(m);
    l.bereitMachen();
    l.d.umspulen();
    double t = 0;
    while (l.d.umspulenAktiv() && t < 200.0) {
        l.d.advance(0.01);
        t += 0.01;
    }
    EXPECT_LT(t, 136.0) << "Gesamtfrist des Lade-ROMs (treiber.md §6.1)";
    EXPECT_NEAR(t, 57.0, 3.0) << "86 m / 1,5 m/s ≈ 57 s (k5200.md)";
}

TEST(KassettenLaufwerk, UmspulenOhneAnwahlWirdIgnoriert) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(5'000'000);
    l.d.reservieren(true);
    l.d.advance(0.4);
    l.d.umspulen();
    EXPECT_FALSE(l.d.umspulenAktiv());
}

// ─── Schreiben ───────────────────────────────────────────────────────────────

TEST(KassettenLaufwerk, SchreibenLegtBlockAnDerKopfstelleAbUndLiefertEcho) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(m->botEnde() + 20'000);
    Lauf l(m);
    l.bereitMachen();
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.d.advance(0.05);
    const auto bytes = blockBauen(daten(50, 0x20));

    const int64_t start = static_cast<int64_t>(l.d.positionMm() * 1000.0);
    ASSERT_TRUE(l.d.schreibeByte(bytes[0]));
    EXPECT_EQ(l.d.bytesBereit(), 0u) << "das Echo kommt erst am Lesespalt, 3,81 mm (≈ 10 ms) spaeter";
    l.d.advance(l.d.profil().byteZeitS());
    for (size_t i = 1; i < bytes.size(); ++i) {
        ASSERT_TRUE(l.d.schreibeByte(bytes[i]));
        l.d.advance(l.d.profil().byteZeitS());
    }
    std::vector<uint8_t> echo;
    while (auto e = l.d.holeByte()) echo.push_back(*e);
    l.laufe(0.04, &echo);
    l.d.vor(false);
    l.d.advance(0.2);
    l.d.aufzeichnen(false);
    l.d.advance(0.01);

    ASSERT_EQ(m->anzahlBloecke(A), 1u);
    EXPECT_EQ(m->block(A, 0).bytes, bytes);
    EXPECT_NEAR(static_cast<double>(m->blockStart(A, 0)), static_cast<double>(start), 2.0);
    EXPECT_TRUE(crcStimmt(m->block(A, 0).bytes));
    EXPECT_EQ(echo, bytes) << "Lese-nach-Schreiben: alles kommt zurueck, auch Prae- und Postambel";
}

TEST(KassettenLaufwerk, EchoFehltBeiAusgeschalteterWiedergabe) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(m->botEnde() + 20'000);
    Lauf l(m);
    l.bereitMachen();
    l.d.wiedergabe(false);
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.d.advance(0.05);
    const auto echo = schreibe(l, blockBauen(daten(10)));
    l.halt();
    l.d.aufzeichnen(false);
    l.d.advance(0.01);
    EXPECT_TRUE(echo.empty());
    EXPECT_EQ(m->anzahlBloecke(A), 1u);
}

TEST(KassettenLaufwerk, ZweiBloeckeHintereinanderMitLuecke) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(m->botEnde() + 20'000);
    Lauf l(m);
    l.bereitMachen();
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.d.advance(0.05);
    const auto b1 = blockBauen(daten(30, 1)), b2 = blockBauen(daten(30, 2));
    schreibe(l, b1);
    l.laufe(0.04);  // Luecke ≈ 15 mm
    schreibe(l, b2);
    l.halt();
    l.d.aufzeichnen(false);
    l.d.advance(0.01);
    ASSERT_EQ(m->anzahlBloecke(A), 2u);
    EXPECT_EQ(m->block(A, 0).bytes, b1);
    EXPECT_EQ(m->block(A, 1).bytes, b2);
    EXPECT_GT(m->block(A, 1).luecke_vor_um, 10'000u);
}

TEST(KassettenLaufwerk, SchreibenUeberBestehendenBlockErsetztNurDiesen) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 1) - 15'000);
    l.bereitMachen();
    const auto alt = *l.m;
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.laufeBis(l.m->blockStart(A, 1));
    const auto neu = blockBauen(daten(100, 0x55));
    schreibe(l, neu);
    l.halt();
    l.d.aufzeichnen(false);
    l.d.advance(0.01);
    ASSERT_EQ(l.m->anzahlBloecke(A), 3u);
    EXPECT_EQ(l.m->block(A, 0).bytes, alt.block(A, 0).bytes);
    EXPECT_EQ(l.m->block(A, 1).bytes, neu);
    EXPECT_EQ(l.m->block(A, 2).bytes, alt.block(A, 2).bytes);
    EXPECT_EQ(l.m->blockStart(A, 2), alt.blockStart(A, 2));
}

TEST(KassettenLaufwerk, AufzeichnenOhneDatenLoeschtDasUeberfahreneBandstueck) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(l.m->blockStart(A, 1) - 15'000);
    l.bereitMachen();
    const auto alt = *l.m;
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.laufeBis(l.m->blockEnde(A, 1) + 3'000);
    l.halt();
    l.d.aufzeichnen(false);
    ASSERT_EQ(l.m->anzahlBloecke(A), 2u);
    EXPECT_EQ(l.m->block(A, 0).bytes, alt.block(A, 0).bytes);
    EXPECT_EQ(l.m->block(A, 1).bytes, alt.block(A, 2).bytes);
    EXPECT_EQ(l.m->blockStart(A, 1), alt.blockStart(A, 2));
}

TEST(KassettenLaufwerk, SchreibenNurBeiAufzeichnenUndArbeitsgeschwindigkeit) {
    auto m = std::make_shared<KassettenMedium>();
    m->setPosition(m->botEnde() + 20'000);
    Lauf l(m);
    l.bereitMachen();
    EXPECT_FALSE(l.d.schreibeByte(0xAA)) << "AUF fehlt";
    l.d.aufzeichnen(true);
    EXPECT_FALSE(l.d.schreibeByte(0xAA)) << "Band steht";
    l.d.vor(true);
    l.d.hge(true);
    l.d.advance(0.5);
    EXPECT_FALSE(l.d.schreibeByte(0xAA)) << "bei HGE ist der Kopf abgeschwenkt";
    l.d.hge(false);
    l.d.advance(0.5);
    EXPECT_TRUE(l.d.schreibeByte(0xAA));
    l.d.rueck(false);
    l.d.vor(false);
    l.d.advance(0.1);
    l.d.rueck(true);
    l.d.advance(0.3);
    EXPECT_FALSE(l.d.schreibeByte(0xAA)) << "rueckwaerts wird nicht geschrieben";
}

TEST(KassettenLaufwerk, SchreibschutzVerhindertSchreibenUndLoeschen) {
    Lauf l(dreiBloecke());
    l.m->setSchreibschutz(A, true);
    l.d.positionSetzen(l.m->blockStart(A, 1) - 15'000);
    l.bereitMachen();
    const auto alt = *l.m;
    l.d.aufzeichnen(true);
    l.d.vor(true);
    l.laufeBis(l.m->blockStart(A, 1) + 2'000);
    EXPECT_TRUE(l.d.schreibsperre());
    EXPECT_FALSE(l.d.schreibeByte(0xAA));
    l.laufeBis(l.m->blockEnde(A, 2) + 1'000);
    l.halt();
    EXPECT_TRUE(l.m->gleichInhalt(alt));
}

TEST(KassettenLaufwerk, SchreibschutzGiltJeSeite) {
    auto m = std::make_shared<KassettenMedium>();
    m->setSchreibschutz(A, true);
    Lauf l(m);
    EXPECT_TRUE(l.d.schreibsperre());
    ASSERT_TRUE(l.d.wenden());
    EXPECT_TRUE(l.d.seiteB());
    EXPECT_FALSE(l.d.schreibsperre());
}

// ─── Kassette wechseln ───────────────────────────────────────────────────────

TEST(KassettenLaufwerk, AuswerfenNurWennNichtReserviertUndBandSteht) {
    Lauf l(dreiBloecke());
    l.bereitMachen();
    EXPECT_FALSE(l.d.kassetteAuswerfen()) << "Entladetaste ist verriegelt, solange RES gesetzt ist";
    l.d.reservieren(false);
    l.d.reservieren(true);
    l.d.vor(true);
    l.d.advance(0.5);
    l.d.reservieren(false);
    EXPECT_TRUE(l.d.kassetteAuswerfen() || l.d.geschwindigkeitMmS() != 0.0);
    l.d.advance(0.5);
    EXPECT_TRUE(l.d.kassetteAuswerfen());
    EXPECT_FALSE(l.d.kassetteGeladen());
    EXPECT_FALSE(l.d.kassetteAuswerfen());
}

TEST(KassettenLaufwerk, DiePositionBleibtBeiDerKassette) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(5'000'000);
    EXPECT_DOUBLE_EQ(l.d.positionMm(), 5000.0);
    l.bereitMachen();
    l.d.vor(true);
    l.d.advance(1.0);
    l.halt();
    const double pos = l.d.positionMm();
    l.d.reservieren(false);
    ASSERT_TRUE(l.d.kassetteAuswerfen());
    EXPECT_EQ(l.m->positionUm(), static_cast<int64_t>(pos * 1000.0 + 0.5));
    KassettenLaufwerk zweites;
    ASSERT_TRUE(zweites.kassetteEinlegen(l.m));
    EXPECT_NEAR(zweites.positionMm(), pos, 0.001);
    EXPECT_FALSE(zweites.kassetteEinlegen(std::make_shared<KassettenMedium>())) << "es liegt schon eine";
    EXPECT_FALSE(KassettenLaufwerk().kassetteEinlegen(nullptr));
}

TEST(KassettenLaufwerk, WendenSpiegeltDieKopfstelle) {
    Lauf l(dreiBloecke());
    l.d.positionSetzen(10'000'000);
    l.d.reservieren(true);
    EXPECT_FALSE(l.d.wenden()) << "nicht bei RES";
    l.d.reservieren(false);
    ASSERT_TRUE(l.d.wenden());
    EXPECT_TRUE(l.d.seiteB());
    EXPECT_DOUBLE_EQ(l.d.positionMm(), (88'000'000 - 10'000'000) / 1000.0);
}

// ─── Datei ───────────────────────────────────────────────────────────────────

namespace {

std::vector<uint8_t> dateiInhalt(const std::string& pfad) {
    std::ifstream f(pfad, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct TempK52 {
    std::string pfad;
    explicit TempK52(const std::string& name) : pfad(k1520test::tempPath(name)) { std::remove(pfad.c_str()); }
    ~TempK52() {
        std::remove(pfad.c_str());
        std::remove((pfad + ".tmp").c_str());
    }
};

}  // namespace

TEST(KassettenLaufwerkDatei, NeueDateiWirdAngelegtUndSpaeterZurueckgeschrieben) {
    TempK52 t("k1520_test_kass_neu.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    EXPECT_TRUE(d.gebunden());
    EXPECT_TRUE(std::filesystem::exists(t.pfad));
    std::string f2;
    auto leer = k52::dekodieren(dateiInhalt(t.pfad), f2);
    ASSERT_TRUE(leer.has_value()) << f2;
    EXPECT_EQ(leer->anzahlBloecke(A), 0u);

    d.positionSetzen(d.medium()->botEnde() + 20'000);
    d.reservieren(true);
    d.anwaehlen(true);
    d.advance(0.31);
    d.aufzeichnen(true);
    d.vor(true);
    d.advance(0.05);
    const auto bytes = blockBauen(daten(40, 7));
    for (uint8_t b : bytes) {
        ASSERT_TRUE(d.schreibeByte(b));
        d.advance(d.profil().byteZeitS());
    }
    d.advance(0.04);
    d.vor(false);
    d.advance(0.2);
    d.aufzeichnen(false);
    d.advance(0.0);
    ASSERT_EQ(d.medium()->anzahlBloecke(A), 1u);

    // Der Block wurde schon beim Schreiben abgelegt; faellig wird die Datei erst nach ≥ 0,5 s Pause.
    double zeit = 0;
    bool geschrieben = false;
    while (zeit < 2.0 && !(geschrieben = d.autoFlush())) {
        d.advance(0.01);
        zeit += 0.01;
    }
    EXPECT_TRUE(geschrieben);
    EXPECT_LE(zeit, 0.61) << "spaetestens 0,5 s nach der letzten Aenderung";
    EXPECT_FALSE(d.autoFlush()) << "und nur einmal";
    EXPECT_EQ(dateiInhalt(t.pfad), k52::kodieren(*d.medium())) << "Datei ist bitgleich zum Medium";
}

TEST(KassettenLaufwerkDatei, WeitereAenderungSchiebtDasZurueckschreibenHinaus) {
    TempK52 t("k1520_test_kass_pause.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(4)));
    d.advance(0.4);
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(4)));
    d.advance(0.4);
    d.advance(0.4);
    EXPECT_FALSE(d.autoFlush()) << "die zweite Aenderung beginnt die Pause von vorn";
    d.advance(0.2);
    EXPECT_TRUE(d.autoFlush());
}

TEST(KassettenLaufwerkDatei, VorhandeneDateiWirdGelesen) {
    TempK52 t("k1520_test_kass_alt.k52");
    {
        KassettenMedium m;
        m.haengeAn(A, 40'000, blockBauen(daten(9)));
        m.setSchreibschutz(Seite::B, true);
        const auto d = k52::kodieren(m);
        std::ofstream f(t.pfad, std::ios::binary);
        f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    }
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    ASSERT_EQ(d.medium()->anzahlBloecke(A), 1u);
    EXPECT_EQ(d.medium()->block(A, 0).bytes, blockBauen(daten(9)));
    EXPECT_TRUE(d.medium()->schreibgeschuetzt(Seite::B));
    EXPECT_FALSE(d.autoFlush());
}

TEST(KassettenLaufwerkDatei, KaputteDateiWirdAbgewiesenUndBleibtUnberuehrt) {
    TempK52 t("k1520_test_kass_kaputt.k52");
    {
        std::ofstream f(t.pfad, std::ios::binary);
        f << "das ist keine Kassette";
    }
    KassettenLaufwerk d;
    std::string fehler;
    EXPECT_FALSE(d.dateiBinden(t.pfad, fehler));
    EXPECT_NE(fehler.find(t.pfad), std::string::npos) << fehler;
    EXPECT_FALSE(d.kassetteGeladen());
    EXPECT_EQ(dateiInhalt(t.pfad).size(), std::string("das ist keine Kassette").size());
}

TEST(KassettenLaufwerkDatei, AuswerfenSchreibtSofortZurueck) {
    TempK52 t("k1520_test_kass_ausw.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(6)));
    ASSERT_TRUE(d.kassetteAuswerfen());
    EXPECT_FALSE(d.gebunden());
    std::string f2;
    auto z = k52::dekodieren(dateiInhalt(t.pfad), f2);
    ASSERT_TRUE(z.has_value()) << f2;
    EXPECT_EQ(z->anzahlBloecke(A), 1u);
}

TEST(KassettenLaufwerkDatei, SchreibenInUnbeschreibbarenOrtMeldetDenFehlerUndHaeltDieKassetteFest) {
    TempK52 t("k1520_test_kass_fehl.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(6)));
    // Das Ziel wird zu einem Verzeichnis: Umbenennen der Nebendatei darauf scheitert.
    std::remove(t.pfad.c_str());
    std::filesystem::create_directory(t.pfad);
    EXPECT_FALSE(d.speichern(fehler));
    EXPECT_FALSE(fehler.empty());
    EXPECT_FALSE(d.kassetteAuswerfen()) << "Aenderungen nicht gesichert: nicht auswerfen";
    EXPECT_TRUE(d.kassetteGeladen());
    EXPECT_TRUE(d.kassetteAuswerfen(true)) << "erzwungen geht es";
    std::filesystem::remove_all(t.pfad);
}

TEST(KassettenLaufwerkDatei, LoesenSchreibtZurueckUndBehaeltDieKassette) {
    TempK52 t("k1520_test_kass_loes.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(6)));
    ASSERT_TRUE(d.dateiLoesen(fehler)) << fehler;
    EXPECT_FALSE(d.gebunden());
    EXPECT_TRUE(d.kassetteGeladen());
    EXPECT_EQ(dateiInhalt(t.pfad), k52::kodieren(*d.medium()));
    d.medium()->haengeAn(A, 40'000, blockBauen(daten(6)));
    d.advance(2.0);
    EXPECT_FALSE(d.autoFlush()) << "nicht mehr gebunden: nichts zu schreiben";
}

TEST(KassettenLaufwerkDatei, PositionAendertDieDateiNicht) {
    TempK52 t("k1520_test_kass_pos.k52");
    KassettenLaufwerk d;
    std::string fehler;
    ASSERT_TRUE(d.dateiBinden(t.pfad, fehler)) << fehler;
    d.reservieren(true);
    d.anwaehlen(true);
    d.advance(0.31);
    d.vor(true);
    d.advance(2.0);
    d.advance(1.0);
    EXPECT_FALSE(d.autoFlush());
}
