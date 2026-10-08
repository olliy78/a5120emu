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
    // Suffix ":unformatiert": Laufwerk wie neu — durchgehend E5, auch Z0/K0/S1 (kein Parametersatz).
    {
        k1520test::TempPlatte roh = k1520test::TempPlatte::leer("p8000_hd_roh.img");
        ASSERT_TRUE(m.hdCreate(2, roh.path(), "D5126:unformatiert")) << m.hdError();
        std::ifstream f(roh.path(), std::ios::binary);
        std::string erste(512, 0);
        f.read(erste.data(), 512);
        EXPECT_EQ(erste, std::string(512, char(0xE5)));
        EXPECT_FALSE(m.hdCreate(0, roh.path(), "quatsch:unformatiert"));
        EXPECT_TRUE(m.hdUnmount(2));
        // ohne Suffix bleibt es beim Parametersatz auf Z0/K0/S1
        ASSERT_TRUE(m.hdCreate(2, roh.path(), "D5126")) << m.hdError();
        std::ifstream g(roh.path(), std::ios::binary);
        g.read(erste.data(), 512);
        EXPECT_NE(erste, std::string(512, char(0xE5)));
        EXPECT_TRUE(m.hdUnmount(2));
    }
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

// ─── P24: Firmware ↔ Plattentyp ──────────────────────────────────────────────

namespace {
P8000Machine::Config mitFirmware(P8000Machine::Config::Wdc w) {
    P8000Machine::Config c = mitWdc("");
    c.wdc = w;
    return c;
}
}  // namespace

/// Bis 4.0 legt das EPROM das Laufwerk fest: eine Datei anderer Größe wird mit Klartext abgewiesen.
TEST(P8000WdcMaschine, P24Firmware3_4_05WeistFalscheGroesseMitKlartextAb) {
    stumm();
    P8000Machine m(mitFirmware(P8000Machine::Config::Wdc::V3_4_05));
    k1520test::TempPlatte klein("D5126", "p24_klein.img");        // 615/4/18 = 22 671 360 B
    EXPECT_FALSE(m.hdMount(0, klein.path()));
    EXPECT_EQ(m.hdError(),
              "Firmware 3.4.05 gehört zu K5504.50 (1024/5/18 = 47 185 920 B), die Datei hat 22 671 360 B");
    EXPECT_EQ(m.hdPath(0), "");
    EXPECT_FALSE(m.hdCreate(0, klein.path(), "D5126"));            // anderer Typ als das ROM
    EXPECT_NE(m.hdError().find("passt nicht"), std::string::npos) << m.hdError();

    k1520test::TempPlatte gut("K5504.50", "p24_gut.img");
    EXPECT_TRUE(m.hdMount(0, gut.path())) << m.hdError();
    EXPECT_EQ(m.platte(0)->geometrie(), (k1520::winchester::Geometrie{1024, 5, 18}));
    EXPECT_FALSE(m.platte(0)->parErgaenzt());                      // die 3.x liest nie einen PAR-Sektor

    // Typ leer = der des ROMs
    k1520test::TempPlatte neu = k1520test::TempPlatte::leer("p24_neu.img");
    EXPECT_TRUE(m.hdCreate(1, neu.path(), "")) << m.hdError();
    EXPECT_EQ(m.platte(1)->geometrie().zylinder, 1024);
}

TEST(P8000WdcMaschine, P24Firmware4_0_05GiltDasselbeUndPlattentypWiderspruchWirdAbgewiesen) {
    stumm();
    P8000Machine m(mitFirmware(P8000Machine::Config::Wdc::V4_0_05));
    k1520test::TempPlatte vs("VS", "p24_vs.img");
    EXPECT_FALSE(m.hdMount(0, vs.path()));
    EXPECT_NE(m.hdError().find("Firmware 4.0.05 gehört zu K5504.50"), std::string::npos) << m.hdError();

    P8000Machine::Config c = mitFirmware(P8000Machine::Config::Wdc::V3_4_05);
    c.platte_typ = "D5146";                                        // widerspricht dem ROM
    EXPECT_THROW(P8000Machine{c}, std::invalid_argument);
    c.platte_typ = "K5504.50";                                     // stimmt: zulässig
    EXPECT_NO_THROW(P8000Machine{c});
}

/// 4.2 liest den PAR von der Platte: widerspricht er dem eingestellten Typ, wird abgewiesen.
TEST(P8000WdcMaschine, P24Firmware4_2PruefParGegenKonfiguriertenTyp) {
    stumm();
    k1520test::TempPlatte p("K5504.50", "p24_par.img");
    {   // Parametersatz einer D5126 auf einer Datei in K5504-Größe
        const auto par = k1520::winchester::parSektor(*k1520::winchester::typNachName("D5126"));
        std::fstream f(p.path(), std::ios::in | std::ios::out | std::ios::binary);
        f.write(reinterpret_cast<const char*>(par.data()), std::streamsize(par.size()));
    }
    P8000Machine::Config c = mitWdc("");
    c.platte_typ = "K5504.50";
    P8000Machine m(c);
    EXPECT_FALSE(m.hdMount(0, p.path()));
    EXPECT_NE(m.hdError().find("Parametersatz der Platte nennt 615/4/18, eingestellt ist K5504.50"),
              std::string::npos) << m.hdError();
    // ohne festgelegten Typ gilt der PAR (und scheitert hier an der Dateigröße — vorhandenes Verhalten)
    P8000Machine ohne(mitWdc(""));
    EXPECT_FALSE(ohne.hdMount(0, p.path()));
    EXPECT_NE(ohne.hdError().find("passt nicht"), std::string::npos) << ohne.hdError();
}

/// Eine unformatierte Platte bekommt beim späteren Anschließen KEINEN erzeugten PAR (Programmvorgabe).
TEST(P8000WdcMaschine, P24UnformatiertePlatteBleibtBeimAnschliessenOhnePar) {
    stumm();
    P8000Machine::Config c = mitWdc("");
    c.platte_par_ergaenzen = false;                                // Programmvorgabe (plattepar=aus)
    P8000Machine m(c);
    k1520test::TempPlatte neu = k1520test::TempPlatte::leer("p24_unf.img");
    ASSERT_TRUE(m.hdCreate(0, neu.path(), "K5504.50:unformatiert")) << m.hdError();
    ASSERT_TRUE(m.hdUnmount(0));
    ASSERT_TRUE(m.hdMount(0, neu.path())) << m.hdError();          // „Anschließen…" derselben Datei
    EXPECT_FALSE(m.platte(0)->parErgaenzt());
    std::array<uint8_t, 512> s{};
    ASSERT_TRUE(m.platte(0)->sektorLesen(0, 0, 1, s.data()));
    EXPECT_EQ(s[0], 0xE5);
    EXPECT_EQ(s[256], 0xE5);
}
