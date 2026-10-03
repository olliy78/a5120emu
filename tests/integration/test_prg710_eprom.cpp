/**
 * @file test_prg710_eprom.cpp
 * @brief PRG 710 / 710-1 EPROMmer (doc/design/20_prg710.md AP-P7b, doc/prg710/eprommer.md):
 *        das Programm `PROG` (UDOS, V3.1) bedient den virtuellen Sockel der ATP 590068.
 *
 * Wächter für Lesen UND Brennen in einem Lauf: `C` (PROM kopieren) liest das eingelegte
 * Quell-PROM in den Puffer 4000H, fordert „COPY-PROM STECKEN“ — der Test legt ein leeres
 * PROM in denselben Sockel —, brennt und vergleicht.  Das gebrannte Abbild muss dem Quell-
 * Abbild bytegleich sein; dazu meldet `E` (Löschkontrolle) „PROM GELOESCHT“ bzw.
 * „PROM NICHT GELOESCHT“.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/machines/prg710/prg710.h"
#include "tests/support/fixtures.h"
#include "tests/system/prg710_bedienung.h"

using namespace prg710test;
using Typ = Eprommer590068::Typ;

namespace {

std::unique_ptr<Prg710Machine> maschine(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return std::make_unique<Prg710Machine>(c);
}

/// Prüfmuster: Daten in den ersten und letzten 100H Byte (A8–A10 werden gebraucht), Rest FFH
/// (das 2716-Verfahren pulst FFH nicht — kürzt den Lauf).
std::vector<uint8_t> muster(size_t n) {
    std::vector<uint8_t> d(n, 0xFF);
    for (size_t i = 0; i < 0x100; ++i) {
        d[i]         = uint8_t(i * 37 + 11);
        d[n - 1 - i] = uint8_t(i ^ 0x5A);
    }
    d[n / 2] = 0x00;
    return d;
}

/// Bis UDOS am `%` steht und `PROG` seine Eröffnungsfrage stellt.
void progStarten(Prg710Machine& m, k1520test::TempDisk& disk) {
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(udosBisDatum(m)) << bild(m);
    tippe(m, "021086");
    ASSERT_TRUE(bisUdosPrompt(m, kBefehl)) << bild(m);
    tippe(m, "PROG");
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "ERWUENSCHT (J/N) ?", kBefehl)) << bild(m);
    lauf(m, 1'000'000);
}

/// Antwort auf eine Frage (Einzeltaste ohne ET), wenn die Frage steht.
void antworte(Prg710Machine& m, const std::string& frage, const std::string& antwort,
              long long frist = kBefehl) {
    ASSERT_TRUE(bisText(m, frage, frist)) << "Frage \"" << frage << "\" kam nicht\n" << bild(m);
    lauf(m, 1'000'000);
    tippe(m, antwort);
}

/// Eröffnungsfrage: Werte ändern auf 1 Byte Breite, @p kb K-Byte, keine Negation.
void typWaehlen(Prg710Machine& m, const char* kb) {
    tippe(m, "J");
    antworte(m, "VERARBEITUNGSBREITE 1 ODER 2 BYTE ?", "1");
    antworte(m, "PROMGROESSE 1 ODER 2 K-BYTE ?", kb);
    antworte(m, "NEGATION ? (J/N)", "N");
}

/// `C`: Quell-PROM gesteckt → lesen → leeres PROM stecken → brennen → vergleichen.
void kopieren(Prg710Machine& m, Typ typ, long long brennfrist) {
    tippe(m, "C");
    antworte(m, "QUELL-PROM GESTECKT ?", "J");
    ASSERT_TRUE(bisText(m, "COPY-PROM STECKEN", kBefehl)) << bild(m);
    m.eprommer().entnehmen();
    m.eprommer().einlegenLeer(typ);
    antworte(m, "COPY-PROM STECKEN", "J");
    antworte(m, "PROM VOLLSTAENDIG PROGRAMMIEREN ?", "J");
    ASSERT_TRUE(bisText(m, "PROGRAMMIERUNG BEENDET", brennfrist)) << bild(m);
    ASSERT_TRUE(bisText(m, "WEITEREN PROM COPIEREN", kBefehl)) << bild(m);
}

}  // namespace

class Prg710Eprom : public ::testing::TestWithParam<V> {};

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Eprom, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return std::string(name(i.param)); });

/**
 * @test Prg710Eprom.KopiertU2716Bytegleich
 * @brief Vorgabe von `PROG` = 2 KB (U2716): `E` am leeren Sockel → „PROM GELOESCHT“, am
 *        Quell-PROM → „NICHT GELOESCHT“; `C` brennt ein leeres U2716 bytegleich zum
 *        Quell-Abbild (ein Impuls ≈ 50,8 ms je Byte ≠ FFH).
 */
TEST_P(Prg710Eprom, KopiertU2716Bytegleich) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    auto m = maschine(GetParam());
    m->eprommer().einlegenLeer(Typ::U2716);
    ASSERT_NO_FATAL_FAILURE(progStarten(*m, disk));
    tippe(*m, "N");
    lauf(*m, 2'000'000);
    tippe(*m, "E");
    ASSERT_TRUE(bisText(*m, "PROM GELOESCHT", kBefehl)) << bild(*m);

    const auto quelle = muster(2048);
    ASSERT_TRUE(m->eprommer().einlegen(quelle, Typ::U2716, "quelle.bin"));
    lauf(*m, 2'000'000);
    tippe(*m, "E");
    ASSERT_TRUE(bisText(*m, "PROM NICHT GELOESCHT", kBefehl)) << bild(*m);
    lauf(*m, 2'000'000);

    ASSERT_NO_FATAL_FAILURE(kopieren(*m, Typ::U2716, 400'000'000));
    EXPECT_EQ(bild(*m).find("PROM-FEHLER    ADRESSE"), std::string::npos) << bild(*m);
    EXPECT_EQ(m->eprommer().inhalt(), quelle);
    EXPECT_TRUE(m->eprommer().geaendert());
    std::string p;
    for (auto& z : m->eprommer().protokoll()) p += z + "\n";
    EXPECT_NE(p.find("Typ eingestellt: U2716"), std::string::npos) << p;
    EXPECT_NE(p.find("Programmierphase:"), std::string::npos) << p;
    EXPECT_EQ(p.find("Hinweis"), std::string::npos) << "Impulsbreite im Datenblatt\n" << p;
}

/**
 * @test Prg710Eprom.KopiertU555Bytegleich
 * @brief Typ 1 KB (U555, 2708-artig) über die Eröffnungsfrage: 84H Bit 0 = 0, 100 Durchläufe
 *        mit ≈ 0,8-ms-Impulsen; Ergebnis bytegleich, Impulsbreite im Datenblatt (0,1–1 ms).
 */
TEST_P(Prg710Eprom, KopiertU555Bytegleich) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    auto m = maschine(GetParam());
    const auto quelle = muster(1024);
    ASSERT_TRUE(m->eprommer().einlegen(quelle, Typ::U555, "quelle.bin"));
    ASSERT_NO_FATAL_FAILURE(progStarten(*m, disk));
    ASSERT_NO_FATAL_FAILURE(typWaehlen(*m, "1"));
    lauf(*m, 2'000'000);
    EXPECT_EQ(m->eprommer().eingestellterTyp(), Typ::U555) << bild(*m);

    ASSERT_NO_FATAL_FAILURE(kopieren(*m, Typ::U555, 800'000'000));
    EXPECT_EQ(bild(*m).find("PROM-FEHLER    ADRESSE"), std::string::npos) << bild(*m);
    EXPECT_EQ(m->eprommer().inhalt(), quelle);
    std::string p;
    for (auto& z : m->eprommer().protokoll()) p += z + "\n";
    EXPECT_NE(p.find("102400 Impulse (102400 wirksam) an 1024 Adressen, je Adresse 100, Impuls 0.80 ms"), std::string::npos) << p;
    EXPECT_EQ(p.find("Hinweis"), std::string::npos) << p;
}

/**
 * @test Prg710Eprom.FalscherTypBrenntNicht
 * @brief Eingestellt 2 KB, gesteckt ein leeres U555: das Brennen bleibt ohne Wirkung, der
 *        Vergleich meldet PROM-Fehler, das Protokoll nennt den falschen Typ (Modell `[?]`).
 */
TEST_P(Prg710Eprom, FalscherTypBrenntNicht) {
    if (GetParam() == V::Prg710_1) GTEST_SKIP() << "Bedienweg wie am 710, einmal genügt";
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    auto m = maschine(GetParam());
    auto quelle = std::vector<uint8_t>(2048, 0xFF);
    quelle[5] = 0x12;
    ASSERT_TRUE(m->eprommer().einlegen(quelle, Typ::U2716, ""));
    ASSERT_NO_FATAL_FAILURE(progStarten(*m, disk));
    tippe(*m, "N");
    lauf(*m, 2'000'000);
    tippe(*m, "C");
    antworte(*m, "QUELL-PROM GESTECKT ?", "J");
    ASSERT_TRUE(bisText(*m, "COPY-PROM STECKEN", kBefehl)) << bild(*m);
    m->eprommer().einlegenLeer(Typ::U555);
    antworte(*m, "COPY-PROM STECKEN", "J");
    antworte(*m, "PROM VOLLSTAENDIG PROGRAMMIEREN ?", "J");
    ASSERT_TRUE(bisText(*m, "PROGRAMMIERUNG BEENDET", 200'000'000)) << bild(*m);
    lauf(*m, 20'000'000);
    EXPECT_NE(bild(*m).find("PROM-FEHLER"), std::string::npos) << bild(*m);
    EXPECT_EQ(m->eprommer().inhalt(), std::vector<uint8_t>(1024, 0xFF));
    std::string p;
    for (auto& z : m->eprommer().protokoll()) p += z + "\n";
    EXPECT_NE(p.find("Brennen mit falschem Typ"), std::string::npos) << p;
}
