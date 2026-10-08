/**
 * @file test_p8000_dram16.cpp
 * @brief P8000 16-Bit-Teil, Hauptspeicher: DRAM-Karten 256 K/1 M, Moduladressen und Grenzen,
 *        Byte-/Wortzugriff, Parität (doc/p8000/hw_16bit.md §6, doc/design/25_p8000.md §10.11 P10a).
 */

#include <gtest/gtest.h>
#include "core/cards/p8000/dram16.h"

#include <algorithm>

using K = P8000Dram16::Karte;

namespace {
P8000Dram16::Config cfg(std::vector<K> karten) {
    P8000Dram16::Config c;
    c.karten = std::move(karten);
    return c;
}
}  // namespace

// ─── Moduladressen und Grenzen ───────────────────────────────────────────────

TEST(P8000Dram16, Vorgabe_EineMegabyteKarteAufModul0) {
    P8000Dram16 d;
    EXPECT_TRUE(d.fehler().empty());
    EXPECT_EQ(d.gesamt(), 0x100000u);
    EXPECT_TRUE(d.gewaehlt(0x000000));
    EXPECT_TRUE(d.gewaehlt(0x0FFFFF));
    EXPECT_FALSE(d.gewaehlt(0x100000));
}

TEST(P8000Dram16, JedeModuladresse256K_BelegtGenauIhrViertelmegabyte) {
    for (int n = 0; n < 64; ++n) {
        P8000Dram16 d(cfg({K{K::Typ::K256, uint8_t(n)}}));
        ASSERT_TRUE(d.fehler().empty()) << n;
        const uint32_t b = uint32_t(n) << 18;
        EXPECT_TRUE(d.gewaehlt(b)) << n;
        EXPECT_TRUE(d.gewaehlt(b + 0x3FFFF)) << n;
        if (n > 0) EXPECT_FALSE(d.gewaehlt(b - 1)) << n;
        if (n < 63) EXPECT_FALSE(d.gewaehlt(b + 0x40000)) << n;
    }
}

TEST(P8000Dram16, JedeModuladresse1M_BelegtGenauIhrMegabyte) {
    for (int n = 0; n < 16; ++n) {
        P8000Dram16 d(cfg({K{K::Typ::M1, uint8_t(n)}}));
        ASSERT_TRUE(d.fehler().empty()) << n;
        const uint32_t b = uint32_t(n) << 20;
        EXPECT_TRUE(d.gewaehlt(b) && d.gewaehlt(b + 0xFFFFF)) << n;
        if (n > 0) EXPECT_FALSE(d.gewaehlt(b - 1)) << n;
        if (n < 15) EXPECT_FALSE(d.gewaehlt(b + 0x100000)) << n;
    }
}

TEST(P8000Dram16, VierKartenBisVierMegabyte_UnbelegtLiefertKeinenZyklus) {
    P8000Dram16 d(cfg({K{K::Typ::M1, 0}, K{K::Typ::M1, 1}, K{K::Typ::M1, 2}, K{K::Typ::M1, 3}}));
    ASSERT_TRUE(d.fehler().empty());
    EXPECT_EQ(d.gesamt(), 0x400000u);
    uint16_t w = 0x1234;
    EXPECT_FALSE(d.lesen(0x400000, w));
    EXPECT_EQ(w, 0x1234) << "ohne MEMSEL bleibt das Datum unberührt (offener Bus = Karte16)";
    EXPECT_FALSE(d.schreiben(0x7FFFFE, true, 0));
    EXPECT_TRUE(d.schreiben(0x3FFFFE, true, 0xBEEF));
    EXPECT_TRUE(d.lesen(0x3FFFFE, w));
    EXPECT_EQ(w, 0xBEEF);
}

TEST(P8000Dram16, A24UndHoeherWerdenNichtDekodiert) {
    P8000Dram16 d;
    EXPECT_TRUE(d.schreiben(0x01000002, true, 0x4711));   // A24 gibt es am Bus nicht
    uint16_t w = 0;
    EXPECT_TRUE(d.lesen(0x000002, w));
    EXPECT_EQ(w, 0x4711);
}

TEST(P8000Dram16, UeberlappungUndUngueltigeModuleWerdenAbgewiesen) {
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::M1, 0}, K{K::Typ::K256, 3}})).empty());
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::M1, 16}})).empty());
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::K256, 64}})).empty());
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::K256, 0}, K{K::Typ::K256, 1}, K{K::Typ::K256, 2},
                                          K{K::Typ::K256, 3}, K{K::Typ::K256, 4}})).empty());
    EXPECT_TRUE(P8000Dram16::pruefe(cfg({K{K::Typ::M1, 0}, K{K::Typ::K256, 4}})).empty());
    EXPECT_TRUE(P8000Dram16::pruefe(cfg({K{K::Typ::K256, 0}, K{K::Typ::K256, 2}})).empty())
        << "Lücken sind erlaubt [D1]";
    P8000Dram16 d(cfg({K{K::Typ::M1, 0}, K{K::Typ::M1, 0}}));
    EXPECT_FALSE(d.fehler().empty());
    EXPECT_EQ(d.gesamt(), 0u) << "ungültige Bestückung wird nicht gesteckt";
}

// ─── Byte/Wort ───────────────────────────────────────────────────────────────

TEST(P8000Dram16, WortIstBigEndian_ByteSchreibenWaehltDieBankNachA0) {
    P8000Dram16 d;
    d.schreiben(0x10000, true, 0x1122);
    uint16_t w = 0;
    d.lesen(0x10001, w);                                   // A0 egal beim Lesen
    EXPECT_EQ(w, 0x1122);
    EXPECT_EQ(d.peek(0x10000), 0x11);
    EXPECT_EQ(d.peek(0x10001), 0x22);
    d.schreiben(0x10001, false, 0xAABB);                   // ungerade: D0–7 = BB
    d.lesen(0x10000, w);
    EXPECT_EQ(w, 0x11BB);
    d.schreiben(0x10000, false, 0xCCDD);                   // gerade: D8–15 = CC
    d.lesen(0x10000, w);
    EXPECT_EQ(w, 0xCCBB);
}

TEST(P8000Dram16, NetzEinFuelltMitDemFuellwert_ResetLaesstDenInhalt) {
    P8000Dram16::Config c;
    c.fuellwert = 0xA5;
    P8000Dram16 d(c);
    EXPECT_EQ(d.peek(0x12345), 0xA5);
    d.poke(0x12345, 0x00);
    d.reset();
    EXPECT_EQ(d.peek(0x12345), 0x00);
    d.powerOn();
    EXPECT_EQ(d.peek(0x12345), 0xA5);
}

// ─── Parität ─────────────────────────────────────────────────────────────────

TEST(P8000Dram16, OhneFehlerbildNiePE) {
    P8000Dram16 d;
    uint16_t w;
    for (uint32_t a = 0; a < 0x100000; a += 0x1001) {
        d.schreiben(a, (a & 1) == 0, uint16_t(a));
        d.lesen(a, w);
        ASSERT_FALSE(d.pe()) << std::hex << a;
    }
    EXPECT_FALSE(d.ledFehler());
}

TEST(P8000Dram16, FehlerbildMeldetPEBeimLesenBeiderBytes_SchreibenHeiltNurSeinByte) {
    P8000Dram16 d;
    uint16_t w;
    ASSERT_TRUE(d.paritaetsfehlerSetzen(0x20001));
    d.lesen(0x20000, w);
    EXPECT_TRUE(d.pe()) << "Wortlesen prüft beide Bänke [D3]";
    d.lesen(0x20002, w);
    EXPECT_FALSE(d.pe()) << "PE− ist ein Pegel nur im fehlerhaften Zyklus";
    EXPECT_TRUE(d.ledFehler()) << "die LED der Karte hält";
    d.schreiben(0x20000, false, 0);     // High-Byte neu: Low-Byte bleibt falsch
    d.lesen(0x20000, w);
    EXPECT_TRUE(d.pe());
    d.schreiben(0x20001, false, 0);
    d.lesen(0x20000, w);
    EXPECT_FALSE(d.pe());
    d.setzeClrParitaet(false);
    EXPECT_TRUE(d.ledFehler());
    d.setzeClrParitaet(true);
    EXPECT_FALSE(d.ledFehler());
    EXPECT_FALSE(d.paritaetsfehlerSetzen(0x100000)) << "keine Karte";
}

TEST(P8000Dram16, ResetLoeschtDieLed) {
    P8000Dram16 d;
    uint16_t w;
    d.paritaetsfehlerSetzen(0x4);
    d.lesen(0x4, w);
    ASSERT_TRUE(d.ledFehler());
    d.reset();
    EXPECT_FALSE(d.ledFehler());
}

TEST(P8000Dram16, RefreshWirdNurGezaehlt) {
    P8000Dram16 d;
    d.poke(0, 0x5A);
    for (int i = 0; i < 1000; ++i) d.refresh();
    EXPECT_EQ(d.refreshZyklen(), 1000u);
    EXPECT_EQ(d.peek(0), 0x5A);
}

// ─── Save-State ──────────────────────────────────────────────────────────────

TEST(P8000Dram16, RundreiseIstBitgleich_UndAndereBestueckungWirdAbgelehnt) {
    P8000Dram16 a(cfg({K{K::Typ::M1, 0}, K{K::Typ::K256, 4}}));
    a.schreiben(0x0FFFFE, true, 0x1234);
    a.schreiben(0x13FFFF, false, 0x0099);
    a.paritaetsfehlerSetzen(0x100000);
    uint16_t w;
    a.lesen(0x100000, w);
    std::vector<uint8_t> s;
    a.serialize(s);
    P8000Dram16 b(cfg({K{K::Typ::M1, 0}, K{K::Typ::K256, 4}}));
    const uint8_t* p = s.data();
    ASSERT_TRUE(b.deserialize(p, s.data() + s.size()));
    EXPECT_EQ(p, s.data() + s.size());
    std::vector<uint8_t> s2;
    b.serialize(s2);
    EXPECT_EQ(s, s2);
    b.lesen(0x100000, w);
    EXPECT_TRUE(b.pe()) << "Fehlerbild gehört zum Stand";

    P8000Dram16 c;   // nur 1 MB
    p = s.data();
    EXPECT_FALSE(c.deserialize(p, s.data() + s.size()));
}

// ─── RAM-Karte 16 MB (2009) und Konfigurationstext (P23b, doc/p8000/ram_konfiguration.md) ──

TEST(P8000Dram16, RamKarte_LiegtAbNull_GroesseNachBestueckung) {
    const struct { K::Typ t; uint32_t g; } tab[] = {
        {K::Typ::R2, 0x200000}, {K::Typ::R4, 0x400000}, {K::Typ::R8, 0x800000}, {K::Typ::R16, 0x1000000}};
    for (const auto& e : tab) {
        P8000Dram16 d(cfg({K{e.t, 0}}));
        ASSERT_TRUE(d.fehler().empty());
        EXPECT_EQ(d.gesamt(), e.g);
        EXPECT_TRUE(d.gewaehlt(0));
        EXPECT_TRUE(d.gewaehlt(std::min<uint32_t>(e.g, 0x7F0000) - 2));
        if (e.g < 0x1000000) EXPECT_FALSE(d.gewaehlt(e.g)) << std::hex << e.g;
    }
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::R16, 1}})).empty()) << "keine Moduladresse";
}

TEST(P8000Dram16, RamKarte_U16SperrtSegment7F_NurAb8MB_UndNurUnterhalbA23) {
    P8000Dram16 d8(cfg({K{K::Typ::R8, 0}}));
    EXPECT_TRUE(d8.gewaehlt(0x7EFFFE));
    EXPECT_FALSE(d8.gewaehlt(0x7F0000));
    EXPECT_FALSE(d8.gewaehlt(0x7FFFFE));
    EXPECT_FALSE(d8.gewaehlt(0x800000));
    P8000Dram16 d16(cfg({K{K::Typ::R16, 0}}));
    EXPECT_FALSE(d16.gewaehlt(0x7F8000));
    EXPECT_TRUE(d16.gewaehlt(0x800000));
    EXPECT_TRUE(d16.gewaehlt(0xFF0000)) << "A23 = 1: der 8-MB-Term mit 7FH− gilt nur für A23 = 0";
    EXPECT_TRUE(d16.gewaehlt(0xFFFFFE));
    uint16_t w = 0x5555;
    EXPECT_FALSE(d16.lesen(0x7F1234, w));
    EXPECT_EQ(w, 0x5555) << "kein MEMSEL — offener Bus (Karte16)";
    EXPECT_FALSE(d16.schreiben(0x7F1234, true, 0));
    EXPECT_TRUE(d16.schreiben(0xFFFFFE, true, 0xCAFE));
    EXPECT_TRUE(d16.lesen(0xFFFFFE, w));
    EXPECT_EQ(w, 0xCAFE);
}

TEST(P8000Dram16, RamKarte_HatKeineParitaet) {
    P8000Dram16 d(cfg({K{K::Typ::R8, 0}}));
    EXPECT_FALSE(d.paritaetsfehlerSetzen(0x1000)) << "SIMMs ×8, PE− am Stecker frei";
    uint16_t w;
    d.lesen(0x1000, w);
    EXPECT_FALSE(d.pe());
    P8000Dram16 m(cfg({K{K::Typ::R2, 0}, K{K::Typ::M1, 2}}));   // daneben eine Robotron-Karte
    ASSERT_TRUE(m.fehler().empty());
    EXPECT_FALSE(m.paritaetsfehlerSetzen(0x100000));
    EXPECT_TRUE(m.paritaetsfehlerSetzen(0x200000));
}

TEST(P8000Dram16, RamKarte_UeberlapptJedeKarteUnterIhrerGroesse) {
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::R16, 0}, K{K::Typ::M1, 15}})).empty());
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::R2, 0}, K{K::Typ::R2, 0}})).empty()) << "zweimal ab 0";
    EXPECT_FALSE(P8000Dram16::pruefe(cfg({K{K::Typ::R4, 0}, K{K::Typ::K256, 15}})).empty());
    EXPECT_TRUE(P8000Dram16::pruefe(cfg({K{K::Typ::R4, 0}, K{K::Typ::K256, 16}})).empty());
    EXPECT_TRUE(P8000Dram16::pruefe(cfg({K{K::Typ::R8, 0}, K{K::Typ::M1, 8}})).empty());
}

TEST(P8000Dram16, Parse_LangUndKurzform) {
    std::vector<K> k;
    ASSERT_EQ(P8000Dram16::parse("4x256K", k), "");
    ASSERT_EQ(k.size(), 4u);
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(k[i].typ == K::Typ::K256 && k[i].modul == i);
    EXPECT_EQ(P8000Dram16::format(k), "256K@0+256K@1+256K@2+256K@3");
    ASSERT_EQ(P8000Dram16::parse("4\xC3\x97" "1m", k), "");
    EXPECT_EQ(P8000Dram16::format(k), "1M@0+1M@1+1M@2+1M@3");
    ASSERT_EQ(P8000Dram16::parse("16M", k), "");
    EXPECT_EQ(P8000Dram16::format(k), "16M@0");
    ASSERT_EQ(P8000Dram16::parse("1M", k), "");
    EXPECT_EQ(P8000Dram16::format(k), "1M@0");
    ASSERT_EQ(P8000Dram16::parse("1M@0+256K@4", k), "") << "rückwärtskompatibel";
    EXPECT_EQ(P8000Dram16::format(k), "1M@0+256K@4");
    ASSERT_EQ(P8000Dram16::parse("8M@0+1M@8", k), "");
    for (const char* schlecht : {"", "5x256K", "0x1M", "4x16M", "2M@1", "1M@16", "256K@64", "32M",
                                 "1M@0+1M@0", "1M@0+256K@3", "16M+1M@1", "1M@", "@0", "1M@0+",
                                 "256K@0+256K@1+256K@2+256K@3+256K@4"}) {
        std::vector<K> r = {K{}};
        EXPECT_FALSE(P8000Dram16::parse(schlecht, r).empty()) << schlecht;
        EXPECT_EQ(r.size(), 1u) << "bei Fehler unverändert: " << schlecht;
    }
    EXPECT_NE(P8000Dram16::parse("1M@0+256K@3", k).find("überlappen"), std::string::npos);
}

TEST(P8000Dram16, MaxSegment_WieMon16Testschritt70) {
    std::vector<K> k;
    auto ms = [&](const char* t) {
        EXPECT_EQ(P8000Dram16::parse(t, k), "") << t;
        return P8000Dram16::maxSegment(k);
    };
    EXPECT_EQ(ms("1M@0"), 0x0F);
    EXPECT_EQ(ms("4x256K"), 0x0F);
    EXPECT_EQ(ms("2x256K"), 0x07);
    EXPECT_EQ(ms("1x256K"), 0x03);
    EXPECT_EQ(ms("4x1M"), 0x3F);
    EXPECT_EQ(ms("2M"), 0x1F);
    EXPECT_EQ(ms("4M"), 0x3F);
    EXPECT_EQ(ms("8M"), 0x7E) << "U16: Segment 7FH fehlt";
    EXPECT_EQ(ms("16M"), 0x7E) << "MMU aus: A23 = 0, die obere Hälfte sieht MON16 nicht";
    EXPECT_EQ(ms("256K@0+256K@2"), 0x03) << "erstes Loch beendet die Suche";
    EXPECT_EQ(ms("256K@1"), -1) << "Segment 0 fehlt: FATAL";
}

TEST(P8000Dram16, RamKarte_RundreiseIstBitgleich) {
    P8000Dram16 a(cfg({K{K::Typ::R16, 0}}));
    a.schreiben(0xFFFFFE, true, 0x1234);
    a.schreiben(0x000101, false, 0x0077);
    std::vector<uint8_t> s;
    a.serialize(s);
    P8000Dram16 b(cfg({K{K::Typ::R16, 0}}));
    const uint8_t* p = s.data();
    ASSERT_TRUE(b.deserialize(p, s.data() + s.size()));
    EXPECT_EQ(b.peek(0xFFFFFE), 0x12);
    EXPECT_EQ(b.peek(0x000101), 0x77);
    P8000Dram16 c(cfg({K{K::Typ::R8, 0}}));
    p = s.data();
    EXPECT_FALSE(c.deserialize(p, s.data() + s.size())) << "andere Bestückung";
}
