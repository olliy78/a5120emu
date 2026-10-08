/**
 * @file test_p8000_wdc_maschine.cpp
 * @brief P8000 mit WDC an der 16-Bit-PIO2 (doc/design/25_p8000.md §10.11 AP P13d): Hardwaretest
 *        U8001 mit echter WDC-Firmware 4.2, Plattenlampe, Save-State P8KS v3, Konfigurationsregeln.
 *
 * Ohne WDC meldet der Hardwaretest U8001 ERROR 52/53/54 mit Rückgabe C1 (P11); mit WDC und einer
 * Platte mit gültigem PAR/BTT-Sektor keinen Fehler.  Platten nur über `TempPlatte`.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/p8000_input.h"
#include "tests/support/temp_platte.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

P8000Machine::Config mitWdc(const std::string& platte) {
    P8000Machine::Config c;
    c.karte16 = true;
    c.wdc = P8000Machine::Config::Wdc::V4_2;
    c.platte = platte;
    return c;
}

/// `x` im U880-Monitor ⇒ U8000-Monitor; NMI ⇒ Hardwaretest bis `MAXSEG`.
void bisMaxseg(P8000Machine& m, bool* lampe = nullptr) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippeZeile(m, "x");
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 40'000'000)) << bild(m);
    laufe(m, 2'000'000);
    m.nmi();
    for (long long t = 0; t < 400'000'000; t += kBatch) {
        m.run(int(kBatch));
        if (lampe && (m.panelLamps() & 4)) *lampe = true;
        if (t % 1'000'000 == 0 && bild(m).find("MAXSEG=<0F>") != std::string::npos) return;
    }
    FAIL() << bild(m);
}
}  // namespace

TEST(P8000WdcMaschine, HardwaretestU8001BestehtMitWdcUndPlatte) {
    stumm();
    k1520test::TempPlatte platte;
    P8000Machine m(mitWdc(platte.path()));
    ASSERT_NE(m.wdc(), nullptr);
    EXPECT_EQ(m.hdPath(0), platte.path());
    m.powerOn();
    EXPECT_TRUE(m.wdc()->imReset());                     // PIO2-B5 offen ⇒ Pull-up ⇒ RST
    bool lampe = false;
    ASSERT_NO_FATAL_FAILURE(bisMaxseg(m, &lampe));
    EXPECT_FALSE(m.wdc()->imReset());                    // MON16 `_disk_in` gibt B5 = 0 aus
    // ohne WDC: *** ERROR 52/53/54 (C1).  („DISK ERROR" nach `x` meldet MON8 ohne Diskette — Vorbestand.)
    EXPECT_EQ(bild(m).find("*** ERROR"), std::string::npos) << bild(m);
    EXPECT_TRUE(lampe) << "Plattenlampe (panelLamps Bit 2) nie an";
}

/// Ohne PAR-Sektor meldet die Firmware den Init-Fehler 38 + LW 0 beim ersten Kommando — MON16
/// zeigt ihn als ERROR 52 mit Rückgabe 39 (so beginnt auch install_WEGA_3.1.log).
TEST(P8000WdcMaschine, OhneParMeldetDerHardwaretestDenInitFehler39) {
    stumm();
    k1520test::TempPlatte platte;
    {
        std::fstream f(platte.path(), std::ios::in | std::ios::out | std::ios::binary);
        const std::string e5(512, char(0xE5));
        f.write(e5.data(), std::streamsize(e5.size()));
    }
    P8000Machine::Config c = mitWdc(platte.path());
    c.platte_par_ergaenzen = false;
    P8000Machine m(c);
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMaxseg(m));
    EXPECT_NE(bild(m).find("*** ERROR 52   39"), std::string::npos) << bild(m);
}

/// P8KS v3: Rundreise mitten in der WDC-Arbeit (Hardwaretest U8001) bitgleich.
TEST(P8000WdcMaschine, StandV3RundreiseIstBitgleich) {
    stumm();
    k1520test::TempPlatte pa("K5504.50", "stand_a.img"), pb("K5504.50", "stand_b.img");
    P8000Machine a(mitWdc(pa.path())), b(mitWdc(pb.path()));
    a.powerOn();
    b.powerOn();
    ASSERT_TRUE(laufeBisText(a, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(a);
    tippe(a, "\r");
    ASSERT_TRUE(laufeBisPrompt(a, ">", 4'000'000)) << bild(a);
    tippeZeile(a, "x");
    ASSERT_TRUE(laufeBisText(a, "Press NMI", 40'000'000)) << bild(a);
    laufe(a, 2'000'000);
    a.nmi();
    // bis der WDC aus dem Reset ist und arbeitet
    for (long long t = 0; t < 300'000'000 && a.wdc()->imReset(); t += kBatch) a.run(int(kBatch));
    ASSERT_FALSE(a.wdc()->imReset()) << bild(a);
    laufe(a, 3'000'000);
    const std::vector<uint8_t> s = a.stateBytes();
    EXPECT_EQ(s[4], P8000Machine::P8000_STAND);
    ASSERT_TRUE(b.restoreStateBytes(s)) << b.stateError();
    EXPECT_EQ(b.stateBytes(), s);
    laufe(a, 6'000'000);
    laufe(b, 6'000'000);
    EXPECT_EQ(a.stateBytes(), b.stateBytes());
    EXPECT_EQ(a.wdc()->cpu().PC, b.wdc()->cpu().PC);

    // Ohne Platte an LW 0 passt der Stand nicht (der WDC-Abschnitt trägt die Plattenmechanik).
    P8000Machine::Config c = mitWdc("");
    P8000Machine ohne(c);
    EXPECT_FALSE(ohne.restoreStateBytes(s));
    EXPECT_NE(ohne.stateError().find("WDC"), std::string::npos) << ohne.stateError();
}

TEST(P8000WdcMaschine, KonfigurationUndPlattenschnittstelle) {
    stumm();
    P8000Machine::Config c;
    c.wdc = P8000Machine::Config::Wdc::V4_2;              // ohne 16-Bit-Karte
    EXPECT_THROW(P8000Machine{c}, std::invalid_argument);
    P8000Machine::Config d;
    d.karte16 = true;
    d.platte = "/gibt/es/nicht.img";                       // Platte ohne WDC
    EXPECT_THROW(P8000Machine{d}, std::invalid_argument);
    d.wdc = P8000Machine::Config::Wdc::V4_2;               // WDC, aber Datei fehlt
    EXPECT_THROW(P8000Machine{d}, std::invalid_argument);

    P8000Machine m(mitWdc(""));
    EXPECT_EQ(m.hdPath(0), "");
    k1520test::TempPlatte neu = k1520test::TempPlatte::leer("p8000_hd_neu.img");
    ASSERT_TRUE(m.hdCreate(1, neu.path(), "D5126")) << m.hdError();
    EXPECT_EQ(m.hdPath(1), neu.path());
    ASSERT_NE(m.platte(1), nullptr);
    EXPECT_EQ(m.platte(1)->geometrie().zylinder, 615);
    EXPECT_EQ(m.wdc()->platte(1), m.platte(1));
    EXPECT_FALSE(m.hdMount(2, neu.path(), true));          // kein Schreibschutz an der Winchester
    EXPECT_FALSE(m.hdCreate(0, neu.path(), "quatsch"));
    EXPECT_TRUE(m.hdFlush());
    EXPECT_TRUE(m.hdUnmount(1));
    EXPECT_EQ(m.wdc()->platte(1), nullptr);
    EXPECT_FALSE(m.hdUnmount(1));
    EXPECT_FALSE(m.hdLed(0));

    // v2-Stand einer Maschine mit 16-Bit-Karte ohne WDC: im Konfigurationsabschnitt fehlen WDC-Byte und
    // WDC-Takt (5 Byte) — er lädt weiter.
    {
        P8000Machine::Config k;
        k.karte16 = true;
        P8000Machine q(k), z(k);
        q.powerOn();
        laufe(q, 500'000);
        std::vector<uint8_t> v = q.stateBytes();
        p8ksAufV3(v);                                         // v4 → v3 (Terminalbytes am Ende weg)
        ASSERT_EQ(v[4], 3);
        uint32_t n = uint32_t(v[6]) | uint32_t(v[7]) << 8 | uint32_t(v[8]) << 16 | uint32_t(v[9]) << 24;
        ASSERT_EQ(v[10 + n - 5], 0);                          // wdc = Aus
        v.erase(v.begin() + 10 + n - 5, v.begin() + 10 + n);
        n -= 5;
        for (int i = 0; i < 4; ++i) v[size_t(6 + i)] = uint8_t(n >> (8 * i));
        v[4] = 2;
        ASSERT_TRUE(z.restoreStateBytes(v)) << z.stateError();
        EXPECT_EQ(z.stateBytes(), q.stateBytes());
    }

    // Ein v2-Stand (ohne WDC-Abschnitt) lädt nicht in eine Maschine mit WDC.
    std::vector<uint8_t> s = m.stateBytes();
    s[4] = 2;
    EXPECT_FALSE(m.restoreStateBytes(s));
    EXPECT_NE(m.stateError().find("v2"), std::string::npos) << m.stateError();
}
