/**
 * @file test_prg710_maschine.cpp
 * @brief Prg710Machine (AP-P1c, doc/design/20_prg710.md §10): beide Varianten bauen,
 *        Netz-Ein + Takte ohne Absturz, CPU läuft im ROM, Verdrahtung der
 *        Speicherverwaltung, Marken-FF-Polarität je Variante.
 *        Das Verhalten des echten ROMs bis „NKM-LOADER“ ist AP-P1d.
 */

#include <gtest/gtest.h>

#include "core/logger.h"
#include "core/machines/prg710/prg710.h"

namespace {
using V = Prg710Machine::Config::Variante;

Prg710Machine::Config cfgFuer(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return c;
}
}  // namespace

class Prg710Maschine : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test Prg710Maschine.BautLaeuftUndBleibtImRom
 * @brief Nach powerOn und einigen 10 000 Takten läuft die CPU (PC bewegt sich) und
 *        gehört noch dem ROM-Bereich oder seiner RAM-Kopie; kein Absturz.
 */
TEST_P(Prg710Maschine, BautLaeuftUndBleibtImRom) {
    Prg710Machine m(cfgFuer(GetParam()));
    EXPECT_EQ(m.machineType(), 1);
    m.powerOn();
    EXPECT_EQ(m.cpuPC(), 0x0000);
    EXPECT_EQ(m.screenChar(0, 0), 0x20) << "Bildschirm nach Netz-Ein gelöscht";
    int gelaufen = 0;
    for (int i = 0; i < 10; ++i) gelaufen += m.run(10'000);
    EXPECT_GE(gelaufen, 100'000);
    EXPECT_NE(m.cpuPC(), 0x0000);
    // Das ROM kopiert sich nach 1000H und läuft aus der RAM-Kopie weiter (§4a/§4b).
    EXPECT_LT(m.cpuPC(), 0x2000);
    EXPECT_EQ(m.speicher().attr(0), 0x10) << "Seite 0 auf OPS-RAM umgeschaltet";
    // Befund beim Bau: das echte ROM schreibt schon „NKM-LOADER" in Zeile 0 (Vertiefung: AP-P1d).
    std::string zeile;
    for (int c = 0; c < 10; ++c) zeile += static_cast<char>(m.screenChar(c, 0));
    EXPECT_EQ(zeile, "NKM-LOADER");
}

/**
 * @test Prg710Maschine.SpeicherverwaltungIstDerSpeicherweg
 * @brief Nach /RESET sieht die CPU das ROM bei 0000H und kein RAM bei 1000H (FFH, Leer? nein:
 *        OPS-RAM bei Abbildung aus ist unsichtbar → FFH).  Ein OUT nach E8H/EBH blendet
 *        OPS-RAM ein, VRAM ist nur bei E8H[F] = FFH sichtbar.
 */
TEST_P(Prg710Maschine, SpeicherverwaltungIstDerSpeicherweg) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    EXPECT_EQ(m.memReadDebug(0x0000), m.zre().memRead(0x0000));
    EXPECT_NE(m.memReadDebug(0x0000), 0xFF) << "ROM sichtbar";
    // VRAM: Schreiben über die CPU-Sicht erreicht die K7024 (Abbildung aus: Seite F = VRAM).
    m.memWriteDebug(0xF800, 'A');
    EXPECT_EQ(m.screenChar(0, 0), 'A');
    EXPECT_EQ(m.memReadDebug(0xF800), 'A') << "K7024 ohne Lesesperre";
    // Volle 16-Bit-E/A-Adresse: OUT (C),r mit B = 00H, C = E8H → Seite 0.
    EXPECT_EQ(m.speicher().freigabe(), 0);
    m.bus().ioWrite(0x00EB, 0x0F);
    m.bus().ioWrite(0x00E8, 0x10);   // Seite 0 = OPS-RAM
    EXPECT_EQ(m.speicher().freigabe(), 0x0F);
    m.memWriteDebug(0x0100, 0x5A);
    EXPECT_EQ(m.memReadDebug(0x0100), 0x5A) << "OPS-RAM in Seite 0";
    m.reset();
    EXPECT_NE(m.memReadDebug(0x0100), 0x5A) << "/RESET blendet das ROM wieder ein";
}

/**
 * @test Prg710Maschine.MarkenFFPolaritaetJeVariante
 * @brief Die K5122 trägt die Polarität des Marken-FF: 710 low-aktiv, 710-1 high-aktiv.
 */
TEST_P(Prg710Maschine, MarkenFFPolaritaetJeVariante) {
    Prg710Machine m(cfgFuer(GetParam()));
    m.powerOn();
    EXPECT_EQ(m.afs().mkeLowAktiv(), GetParam() == V::Prg710);
    EXPECT_FALSE(m.afs().markeErkannt());
    // Der Pegel an Tor B selbst: Wächter in test_k5122_wait.cpp (MkeLowAktiv_…).
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Maschine,
                         ::testing::Values(V::Prg710, V::Prg710_1));
