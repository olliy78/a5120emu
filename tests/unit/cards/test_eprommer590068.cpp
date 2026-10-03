/**
 * @file test_eprommer590068.cpp
 * @brief EPROMmer der ATP 590068 mit virtuellem Sockel (doc/design/20_prg710.md AP-P7b,
 *        Befund doc/prg710/eprommer.md).  Die Portfolgen sind die von `PROG` V3.1.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/bus/k1520_bus.h"
#include "core/cards/atp590068/eprommer590068.h"
#include "tests/support/temp_path.h"

namespace {
using Typ = Eprommer590068::Typ;

struct Aufbau {
    K1520Bus       bus;
    Eprommer590068 e;
    uint8_t        zre_pio_a = 0x01;   // 84H Bit 0: 1 = U2716
    uint64_t       takt = 0;

    Aufbau() {
        e.attachToBus(bus);
        e.setTypwahl([this] { return zre_pio_a; });
        e.setZeit([this] { return takt; });
        // PROG 61B9: PIO A Betriebsart 3 Eingang, B Betriebsart 3 Ausgang.
        out(0xD2, 0xFF); out(0xD2, 0xFF); out(0xD3, 0xFF); out(0xD3, 0x00);
    }
    void    out(uint8_t p, uint8_t d) { bus.ioWrite(p, d); }
    uint8_t in(uint8_t p) { return bus.ioRead(p); }

    /// PROG 6C59: Einschalten (U555: Bit 3 vor Bit 4).
    uint8_t einschalten(bool u555) {
        uint8_t e4 = u555 ? 0x08 : 0x00;
        out(0xD4, e4);
        e4 |= 0x10;
        out(0xD4, e4);
        return e4;
    }
    /// PROG 6C33: lesen.
    uint8_t lies(uint16_t a, uint8_t e4) {
        out(0xD4, uint8_t((e4 & 0x1F) | ((a >> 8) << 5)));
        out(0xD1, uint8_t(a));
        return in(0xD0);
    }
    /// PROG 6DCA: Port A auf Ausgang; 6DC3: zurück.
    void datenAus()   { out(0xD2, 0xCF); out(0xD2, 0x00); out(0xD3, 0xCF); out(0xD3, 0x00); }
    void datenEin()   { out(0xD2, 0xCF); out(0xD2, 0xFF); out(0xD3, 0xCF); out(0xD3, 0x00); }
    /// PROG 6C09 + 6B4D: Adresse und Daten anlegen, Impuls von @p breite Takten.
    void impuls(uint16_t a, uint8_t d, uint8_t& e4, uint64_t breite) {
        e4 = uint8_t((e4 & 0x1F) | ((a >> 8) << 5));
        out(0xD4, e4);
        out(0xD1, uint8_t(a));
        out(0xD0, d);
        out(0xD4, uint8_t(e4 | 0x04));
        takt += breite;
        out(0xD4, e4);
        takt += 100;
    }
};

std::string alles(const Eprommer590068& e) {
    std::string s;
    for (auto& z : e.protokoll()) s += z + "\n";
    return s;
}
}  // namespace

TEST(Eprommer590068, LeererSockelUndOhneVersorgungLiestFF) {
    Aufbau a;
    EXPECT_FALSE(a.e.steckt());
    const uint8_t e4 = a.einschalten(false);
    EXPECT_EQ(a.lies(0x123, e4), 0xFF);
    std::vector<uint8_t> d(2048);
    for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(i * 7);
    ASSERT_TRUE(a.e.einlegen(d, Typ::Keiner, ""));
    EXPECT_EQ(a.e.typ(), Typ::U2716);
    a.out(0xD4, 0x00);                                    // Versorgung aus
    EXPECT_EQ(a.lies(0x123, 0x00), 0xFF);
}

TEST(Eprommer590068, LiestAdresseAusPortBUndSteuerregister) {
    Aufbau a;
    std::vector<uint8_t> d(2048);
    for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t((i >> 3) ^ i);
    ASSERT_TRUE(a.e.einlegen(d, Typ::U2716, "x.bin"));
    const uint8_t e4 = a.einschalten(false);
    for (uint16_t adr : {0x000, 0x0FF, 0x100, 0x2AB, 0x5A5, 0x7FF})
        EXPECT_EQ(a.lies(adr, e4), d[adr]) << std::hex << adr;
    EXPECT_EQ(a.e.adresse(), 0x7FF);
    EXPECT_EQ(a.e.leseZugriffe(), 6u);
}

TEST(Eprommer590068, PioIstBaAnAb0CdAnAb1) {
    // D2H/D3H sind Steuerwörter: nach „CF 00“ an D3H treibt Port B (D1H) die Adresse.
    Aufbau a;
    a.out(0xD1, 0x5A);
    const auto st = a.e.pio().debugState();
    EXPECT_EQ(st.port[1].mode, 3);
    EXPECT_EQ(st.port[1].dir, 0x00);
    EXPECT_EQ(st.port[1].out, 0x5A);
    EXPECT_EQ(st.port[0].dir, 0xFF);                      // A: Eingang (Lesen)
}

TEST(Eprommer590068, BrenntNurEinsZuNullUndNurMitSpannung) {
    Aufbau a;
    a.e.einlegenLeer(Typ::U2716);
    uint8_t e4 = a.einschalten(false);
    a.datenAus();
    // Impuls ohne Programmierspannung: keine Wirkung.
    a.impuls(0x010, 0x00, e4, 125'000);
    EXPECT_EQ(a.e.inhalt()[0x010], 0xFF);
    // PROG 6B0D–6B15: Bit 3, dann Bit 0+1.
    e4 |= 0x08; a.out(0xD4, e4);
    e4 |= 0x03; a.out(0xD4, e4);
    a.impuls(0x010, 0xA5, e4, 125'000);
    a.impuls(0x7FE, 0x3C, e4, 125'000);
    a.impuls(0x010, 0xF0, e4, 125'000);                   // zweites Brennen: nur 1 → 0
    auto z = a.e.inhalt();
    EXPECT_EQ(z[0x010], 0xA5 & 0xF0);
    EXPECT_EQ(z[0x7FE], 0x3C);
    EXPECT_TRUE(a.e.geaendert());
    // Port A auf Eingang: Leitungen offen = 1 → nichts brennt.
    a.datenEin();
    a.impuls(0x020, 0x00, e4, 125'000);
    EXPECT_EQ(a.e.inhalt()[0x020], 0xFF);
    a.out(0xD4, 0x10);                                    // Programmierspannung aus
    const std::string p = alles(a.e);
    EXPECT_NE(p.find("Programmierspannung ein"), std::string::npos) << p;
    EXPECT_NE(p.find("Programmierphase: 4 Impulse (4 wirksam) an 3 Adressen"), std::string::npos) << p;
    EXPECT_NE(p.find("2 Byte geändert"), std::string::npos) << p;
    EXPECT_NE(p.find("50.9 ms"), std::string::npos) << p;
    EXPECT_NE(p.find("ohne Wirkung"), std::string::npos) << p;
}

TEST(Eprommer590068, FalscherTypLiestAberBrenntNicht) {
    Aufbau a;
    a.zre_pio_a = 0x00;                                   // eingestellt U555 (1 KB)
    std::vector<uint8_t> d(2048, 0x11);
    d[0x400] = 0x22;
    ASSERT_TRUE(a.e.einlegen(d, Typ::U2716, ""));
    uint8_t e4 = a.einschalten(true);
    EXPECT_EQ(a.lies(0x000, e4), 0x11);
    a.datenAus();
    e4 |= 0x03; a.out(0xD4, e4);
    a.impuls(0x000, 0x00, e4, 1'970);
    EXPECT_EQ(a.e.inhalt()[0], 0x11);
    const std::string p = alles(a.e);
    EXPECT_NE(p.find("Typ eingestellt: U555"), std::string::npos) << p;
    EXPECT_NE(p.find("Lesen mit falschem Typ"), std::string::npos) << p;
    EXPECT_NE(p.find("Brennen mit falschem Typ"), std::string::npos) << p;
}

TEST(Eprommer590068, U555HundertDurchlaeufeUndImpulsbreite) {
    Aufbau a;
    a.zre_pio_a = 0x00;
    a.e.einlegenLeer(Typ::U555);
    EXPECT_EQ(a.e.inhalt().size(), 1024u);
    uint8_t e4 = a.einschalten(true);
    a.datenAus();
    e4 |= 0x03; a.out(0xD4, e4);
    for (int n = 0; n < 100; ++n)
        for (uint16_t adr = 0; adr < 4; ++adr) a.impuls(adr, uint8_t(0x10 + adr), e4, 1'970);
    a.impuls(0x3FF, 0x00, e4, 100);                      // zu kurz: Hinweis, brennt trotzdem
    a.out(0xD4, 0x18);
    auto z = a.e.inhalt();
    EXPECT_EQ(z[3], 0x13);
    EXPECT_EQ(z[0x3FF], 0x00);
    const std::string p = alles(a.e);
    EXPECT_NE(p.find("401 Impulse (401 wirksam) an 5 Adressen, je Adresse 1–100"), std::string::npos) << p;
    EXPECT_NE(p.find("Hinweis: Impulsbreite 0.04 ms"), std::string::npos) << p;
}

TEST(Eprommer590068, SteuerwortAnPortBNachDemBrennenIstFolgenlos) {
    // PROG 6B76/6C9E: nach OTIR steht C auf D3H — OUT (C),A/E gehen an das Steuerwort B.
    Aufbau a;
    a.e.einlegenLeer(Typ::U2716);
    a.out(0xD3, 0x00);
    a.out(0xD3, 0x18);
    a.out(0xD1, 0x42);
    EXPECT_EQ(a.e.pio().debugState().port[1].out, 0x42);
    EXPECT_EQ(a.e.pio().debugState().port[1].dir, 0x00);
}

TEST(Eprommer590068, AbbildDateiEinlegenSpeichernUndLoeschen) {
    const std::string pfad = k1520test::tempPath("eprom_abbild.bin");
    {
        std::ofstream f(pfad, std::ios::binary);
        for (int i = 0; i < 1000; ++i) f.put(char(i));
    }
    Eprommer590068 e;
    std::string fehler;
    ASSERT_TRUE(e.einlegenDatei(pfad, Typ::Keiner, &fehler)) << fehler;
    EXPECT_EQ(e.typ(), Typ::U555);                        // ≤ 1 KB
    EXPECT_EQ(e.inhalt().size(), 1024u);
    EXPECT_EQ(e.inhalt()[999], uint8_t(999));
    EXPECT_EQ(e.inhalt()[1000], 0xFF);                    // aufgefüllt
    EXPECT_EQ(e.datei(), pfad);
    e.uvLoeschen();
    EXPECT_TRUE(e.geaendert());
    ASSERT_TRUE(e.speichern("", &fehler)) << fehler;
    EXPECT_FALSE(e.geaendert());
    EXPECT_EQ(std::filesystem::file_size(pfad), 1024u);
    // Zu groß für den Typ.
    std::vector<uint8_t> gross(2048);
    EXPECT_FALSE(e.einlegen(gross, Typ::U555, "", &fehler));
    EXPECT_NE(fehler.find("fasst 1024"), std::string::npos);
    e.entnehmen();
    EXPECT_FALSE(e.steckt());
    EXPECT_FALSE(e.speichern("", &fehler));
    std::remove(pfad.c_str());
}

TEST(Eprommer590068, ProtokollNeuLiefertJedeZeileEinmal) {
    Aufbau a;
    a.e.einlegenLeer(Typ::U2716);
    auto n1 = a.e.protokollNeu();
    ASSERT_FALSE(n1.empty());
    EXPECT_TRUE(a.e.protokollNeu().empty());
    a.e.entnehmen();
    auto n2 = a.e.protokollNeu();
    ASSERT_EQ(n2.size(), 1u);
    EXPECT_NE(n2[0].find("entnommen"), std::string::npos);
}

TEST(Eprommer590068, ResetSchaltetSpannungenAb) {
    Aufbau a;
    a.e.einlegenLeer(Typ::U2716);
    a.out(0xD4, 0x13);
    a.e.reset();
    EXPECT_EQ(a.e.steuerregister(), 0);
    EXPECT_NE(alles(a.e).find("/RESET"), std::string::npos);
    EXPECT_TRUE(a.e.steckt());                            // der Sockel bleibt
}
