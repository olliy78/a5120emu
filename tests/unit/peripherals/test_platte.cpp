/**
 * @file test_platte.cpp
 * @brief Winchesterlaufwerk am P8000-WDC (AP P13b): Typen/PAR, Geometrie aus der Größe, Abbild
 *        anlegen/öffnen, Spursynthese (Lage, Marken, CRC, Interleave, Kopfversatz), Zerlegung,
 *        Zurückschreiben, unformatiert, Mechanik, Save-State.  Quelle: core/peripherals/winchester/
 *        platte.h, doc/p8000/wdc_firmware.md §7–§10.
 */
#include "core/peripherals/winchester/platte.h"
#include "tests/support/temp_platte.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace k1520::winchester;
using k1520test::TempPlatte;

namespace {

constexpr int N = Platte::BYTES_JE_SPUR;
constexpr uint16_t M = Platte::MARKE;
constexpr uint8_t SC_TAB[18] = {1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17, 9, 18};

uint16_t crc(const std::vector<uint8_t>& b)
{
    uint16_t c = 0xFFFF;
    for (uint8_t x : b) c = crcCcitt(c, x);
    return c;
}

std::array<uint8_t, 512> muster(int z, int k, int s)
{
    std::array<uint8_t, 512> d{};
    for (int i = 0; i < 512; ++i) d[size_t(i)] = static_cast<uint8_t>(i * 7 + z * 3 + k * 5 + s * 11);
    return d;
}

/// Ein Datenfeld wie die Firmware es schreibt (eine Marke) ab @p pos in den Strom.
void datenfeldSchreiben(Platte& p, int kopf, int pos, const std::array<uint8_t, 512>& d, uint64_t t = 1)
{
    uint16_t c = 0xFFFF;
    auto w = [&](uint16_t v, bool inCrc = true) {
        if (inCrc) c = crcCcitt(c, static_cast<uint8_t>(v));
        p.schreibe(kopf, pos++ % N, v, t);
    };
    for (int i = 0; i < 11; ++i) w(0xFF, false);
    c = 0xFFFF;
    w(0xA1 | M);
    w(0xFB);
    for (uint8_t b : d) w(b);
    const uint16_t cc = c;
    w(static_cast<uint8_t>(cc >> 8), false);
    w(static_cast<uint8_t>(cc), false);
    for (int i = 0; i < 5; ++i) w(0xFF, false);
}

/// Kennfeld wie `ft_trk` (FF×18, A1×3, FE C C H S CRC, FF×10) ab @p pos.
void kennfeldSchreiben(std::vector<uint16_t>& s, int pos, int z, int k, int sek)
{
    std::vector<uint8_t> b = {0xA1, 0xA1, 0xA1, 0xFE, uint8_t(z), uint8_t(z >> 8), uint8_t(k), uint8_t(sek)};
    const uint16_t c = crc(b);
    for (int i = 0; i < 18; ++i) s[size_t(pos++ % N)] = 0xFF;
    for (int i = 0; i < 3; ++i) s[size_t(pos++ % N)] = 0xA1 | M;
    for (size_t i = 3; i < b.size(); ++i) s[size_t(pos++ % N)] = b[i];
    s[size_t(pos++ % N)] = c >> 8;
    s[size_t(pos++ % N)] = c & 0xFF;
    for (int i = 0; i < 10; ++i) s[size_t(pos++ % N)] = 0xFF;
}

}  // namespace

// ─── CRC, Typen, PAR ─────────────────────────────────────────────────────────

TEST(Platte, CrcCcittIstDerStandardMitStartwertFFFF)
{
    EXPECT_EQ(crc({'1', '2', '3', '4', '5', '6', '7', '8', '9'}), 0x29B1);
    // Kennfeld samt angehängter CRC ergibt 0 (so prüft die Kartenhardware)
    std::vector<uint8_t> id = {0xA1, 0xA1, 0xA1, 0xFE, 0, 0, 0, 1};
    const uint16_t c = crc(id);
    id.push_back(c >> 8); id.push_back(c & 0xFF);
    EXPECT_EQ(crc(id), 0);
}

TEST(Platte, TypentabelleWieSaFormat)
{
    const auto& t = typen();
    ASSERT_EQ(t.size(), 5u);
    const Typ* k = typNachName("K5504.50");
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(k->g.zylinder, 1024); EXPECT_EQ(k->g.koepfe, 5); EXPECT_EQ(k->g.sektoren, 18);
    EXPECT_EQ(k->g.bytes(), 47185920u);
    EXPECT_EQ(k->vorkomp, 1024); EXPECT_EQ(k->ramp, 1);
    EXPECT_EQ(k->ztk40, 203); EXPECT_EQ(k->ztk41, 209);
    EXPECT_EQ(k->zmn40, 251); EXPECT_EQ(k->zmx40, 253); EXPECT_EQ(k->zmn41, 241); EXPECT_EQ(k->zmx41, 243);
    EXPECT_EQ(typNachName("D5126")->g.zylinder, 615);
    EXPECT_EQ(typNachName("D5146")->g.koepfe, 8);
    EXPECT_EQ(typNachName("VS")->g.zylinder, 820);
    EXPECT_EQ(typNachName("WEGA31-AVR")->g.bytes(), 127180800u);   // wdc_firmware.md §11
    EXPECT_EQ(typNachName("gibtsnicht"), nullptr);
    for (const auto& x : t) EXPECT_EQ(std::strlen(x.kennung), 12u) << x.name;
}

TEST(Platte, ParSektorHatDieOffsetsDerFirmware)
{
    const auto s = parSektor(*typNachName("K5504.50"));
    EXPECT_EQ(std::memcmp(s.data(), "DEFEKT", 6), 0);
    EXPECT_EQ(s[6], 0); EXPECT_EQ(s[7], 0);
    EXPECT_EQ(s[8], 0xFF); EXPECT_EQ(s[9], 0xFF); EXPECT_EQ(s[10], 0xFF);
    EXPECT_EQ(std::memcmp(s.data() + 256, "PARMTR", 6), 0);
    EXPECT_EQ(std::memcmp(s.data() + 262, "ROB K5504.50", 12), 0);
    EXPECT_EQ(s[277] | (s[278] << 8), 1024);
    EXPECT_EQ(s[279], 5); EXPECT_EQ(s[280], 18);
    EXPECT_EQ(s[281] | (s[282] << 8), 1024);
    EXPECT_EQ(s[283], 1);
    EXPECT_EQ(s[290], 203); EXPECT_EQ(s[291], 209);
    EXPECT_EQ(s[292], 251); EXPECT_EQ(s[293], 253); EXPECT_EQ(s[294], 241); EXPECT_EQ(s[295], 243);
}

TEST(Platte, ParGeometrieNimmtJedenTypAn)
{
    for (const auto& t : typen()) {
        const auto s = parSektor(t);
        const auto g = parGeometrie(s.data());
        ASSERT_TRUE(g.has_value()) << t.name;
        EXPECT_TRUE(*g == t.g) << t.name;
    }
}

TEST(Platte, ParGeometrieVerwirftJedeVerletzteGrenzeWieTPar)
{
    const auto gut = parSektor(*typNachName("K5504.50"));
    struct Fall { const char* was; int off; int wert; };
    const Fall faelle[] = {
        {"Kenntext", 256, 'X'},          {"Name < 20H", 262, 0x1F},     {"Name > 7AH", 273, 0x7B},
        {"Zylinder 99", 277, 99},        {"Köpfe 1", 279, 1},            {"Köpfe 17", 279, 17},
        {"Sektoren 16", 280, 16},        {"Sektoren 19", 280, 19},      {"Ramp 0", 283, 0},
        {"Ramp 22", 283, 22},            {"ztk40 AFH", 290, 0xAF},      {"ztk40 = ztk41", 290, 209},
        {"zmx40 < zmn40", 293, 250},     {"zmx41 < zmn41", 295, 240},   {"zmn41 = zmn40", 294, 251},
    };
    for (const auto& f : faelle) {
        auto s = gut;
        s[size_t(f.off)] = static_cast<uint8_t>(f.wert);
        if (f.off == 277) s[278] = 0;
        EXPECT_FALSE(parGeometrie(s.data()).has_value()) << f.was;
    }
    auto s = gut;   // Zylinder 3000
    s[277] = 3000 & 0xFF; s[278] = 3000 >> 8;
    EXPECT_FALSE(parGeometrie(s.data()).has_value());
    s = gut;        // Vorkompensation > Zylinder
    s[281] = 1025 & 0xFF; s[282] = 1025 >> 8;
    EXPECT_FALSE(parGeometrie(s.data()).has_value());
    s = gut;        // ztk40 > F0H (mit ztk41 darüber)
    s[290] = 0xF1; s[291] = 0xF2;
    EXPECT_FALSE(parGeometrie(s.data()).has_value());
    s = gut;        // Grenzfälle, die gelten: 100 Zylinder, 2/16 Köpfe, 17 Sektoren, Ramp 21, ztk40 = B0H
    s[277] = 100; s[278] = 0; s[281] = 100; s[282] = 0; s[279] = 16; s[280] = 17; s[283] = 21; s[290] = 0xB0;
    ASSERT_TRUE(parGeometrie(s.data()).has_value());
    EXPECT_EQ(parGeometrie(s.data())->zylinder, 100);
}

TEST(Platte, GeometrieAusDerGroesseNurWennEindeutig)
{
    EXPECT_TRUE(*Platte::geometrieAusGroesse(47185920) == typNachName("K5504.50")->g);
    EXPECT_TRUE(*Platte::geometrieAusGroesse(22671360) == typNachName("D5126")->g);
    EXPECT_TRUE(*Platte::geometrieAusGroesse(127180800) == typNachName("WEGA31-AVR")->g);
    EXPECT_FALSE(Platte::geometrieAusGroesse(45342720).has_value());   // D5146 ≡ VS
    EXPECT_FALSE(Platte::geometrieAusGroesse(47185920 + 512).has_value());
    EXPECT_FALSE(Platte::geometrieAusGroesse(0).has_value());
}

// ─── Anlegen und Öffnen ──────────────────────────────────────────────────────

TEST(Platte, NeuesAbbildHatVolleGroesseParUndE5)
{
    TempPlatte tp;
    EXPECT_EQ(std::filesystem::file_size(tp.path()), 47185920u);
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp)) << p.fehler();
    EXPECT_TRUE(p.geometrie() == typNachName("K5504.50")->g);
    EXPECT_FALSE(p.parErgaenzt());
    std::array<uint8_t, 512> d{};
    ASSERT_TRUE(p.sektorLesen(0, 0, 1, d.data()));
    EXPECT_EQ(d, parSektor(*typNachName("K5504.50")));
    for (auto [z, k, s] : {std::array<int, 3>{0, 0, 2}, {1, 0, 1}, {1023, 4, 18}, {512, 2, 9}}) {
        ASSERT_TRUE(p.sektorLesen(z, k, s, d.data()));
        for (uint8_t b : d) ASSERT_EQ(b, 0xE5) << z << "/" << k << "/" << s;
    }
    EXPECT_FALSE(p.sektorLesen(1024, 0, 1, d.data()));
    EXPECT_FALSE(p.sektorLesen(0, 5, 1, d.data()));
    EXPECT_FALSE(p.sektorLesen(0, 0, 0, d.data()));
    EXPECT_FALSE(p.sektorLesen(0, 0, 19, d.data()));
    for (int z = 0; z < 1024; z += 97)
        for (int k = 0; k < 5; ++k) EXPECT_TRUE(p.formatiert(z, k));
}

TEST(Platte, AbbildlageIstLbaNachKennfeldSektornummer)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    const auto d = muster(3, 2, 7);
    ASSERT_TRUE(p.sektorSchreiben(3, 2, 7, d.data()));
    p.schliessen();
    std::ifstream f(tp.path(), std::ios::binary);
    f.seekg(((3 * 5 + 2) * 18 + 6) * 512);
    std::array<uint8_t, 512> r{};
    f.read(reinterpret_cast<char*>(r.data()), 512);
    EXPECT_EQ(r, d);
}

TEST(Platte, OeffnenScheitertBeiFehlenderDateiFalscherGroesseUndUnbekannterGeometrie)
{
    Platte p;
    EXPECT_FALSE(p.oeffnen(k1520test::tempPath("k1520_gibtsnicht.img")));
    EXPECT_FALSE(p.fehler().empty());

    auto leer = TempPlatte::leer("krumm.img");
    { std::ofstream f(leer.path(), std::ios::binary); f << std::string(512 * 7, '\0'); }
    EXPECT_FALSE(p.oeffnen(leer));   // kein PAR, Größe unbekannt
    EXPECT_NE(p.fehler().find("Geometrie"), std::string::npos);

    Platte::Config cfg;
    cfg.geometrie = Geometrie{100, 2, 18};
    EXPECT_FALSE(p.oeffnen(leer, cfg));   // feste Geometrie, Größe passt nicht
    EXPECT_NE(p.fehler().find("passt nicht"), std::string::npos);

    TempPlatte tp;                        // gültiger PAR, aber Datei gekürzt
    std::filesystem::resize_file(tp.path(), 47185920 - 512);
    EXPECT_FALSE(p.oeffnen(tp));
}

TEST(Platte, ParMitFesterGeometrieWirdBefolgt)
{
    auto leer = TempPlatte::leer("fest.img");
    { std::ofstream f(leer.path(), std::ios::binary); }
    std::filesystem::resize_file(leer.path(), uint64_t(100) * 2 * 17 * 512);
    Platte::Config cfg;
    cfg.geometrie = Geometrie{100, 2, 17};
    Platte p;
    ASSERT_TRUE(p.oeffnen(leer, cfg)) << p.fehler();
    EXPECT_EQ(p.geometrie().sektoren, 17);
    EXPECT_TRUE(p.parErgaenzt());     // Nullen sind kein PAR — ergänzt [P5]
}

TEST(Platte, AvrAbbildBekommtParNurImSpeicher)
{
    // WEGA-3.1-Abbild (AVR, 1380/10/18): Sektor 0 = Parameterblock, kein PAR (wdc_firmware.md §11)
    auto tp = TempPlatte::leer("avr.img");
    { std::ofstream f(tp.path(), std::ios::binary); f << "WDC_4.2" << '\0' << "WDC-Emulator"; }
    std::filesystem::resize_file(tp.path(), 127180800);
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp)) << p.fehler();
    EXPECT_TRUE(p.geometrie() == typNachName("WEGA31-AVR")->g);
    EXPECT_TRUE(p.parErgaenzt());
    // die Synthese liefert den PAR-Sektor, die Datei bleibt unberührt
    const auto par = parSektor(*typNachName("WEGA31-AVR"));
    const auto z = p.zerlege(0, 0, p.spur(0, 0));
    ASSERT_TRUE(z.formatiert);
    ASSERT_EQ(z.daten.count(1), 1u);
    EXPECT_EQ(std::memcmp(z.daten.at(1).data(), par.data(), 512), 0);
    p.flush();
    p.schliessen();
    std::ifstream f(tp.path(), std::ios::binary);
    char kopf[8];
    f.read(kopf, 8);
    EXPECT_EQ(std::string(kopf, 7), "WDC_4.2");
}

TEST(Platte, AvrAbbildUeberlagerungEndetWennDieFirmwareSektorEinsSchreibt)
{
    auto tp = TempPlatte::leer("avr2.img");
    { std::ofstream f(tp.path(), std::ios::binary); }
    std::filesystem::resize_file(tp.path(), 127180800);
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    ASSERT_TRUE(p.parErgaenzt());
    auto neu = parSektor(*typNachName("WEGA31-AVR"));
    neu[300] = 0x42;
    // Datenfeld von Sektor 1 (Kopf 0: Slot 0) überschreiben
    datenfeldSchreiben(p, 0, 41, neu);
    p.flush();
    EXPECT_FALSE(p.parErgaenzt());
    std::array<uint8_t, 512> d{};
    ASSERT_TRUE(p.sektorLesen(0, 0, 1, d.data()));
    EXPECT_EQ(d, neu);
}

// ─── Spursynthese ────────────────────────────────────────────────────────────

TEST(Platte, SyntheseLegtKennfelderUndDatenfelderAnDieFestenStellen)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    const auto d = muster(0, 0, 10);
    ASSERT_TRUE(p.sektorSchreiben(0, 0, 10, d.data()));
    const auto& s = p.spur(0, 0);
    ASSERT_EQ(s.size(), size_t(N));
    int marken = 0;
    for (uint16_t w : s) marken += (w & M) ? 1 : 0;
    EXPECT_EQ(marken, 18 * 4);
    for (int i = 0; i < 18; ++i) {
        const int b = i * Platte::SLOT;
        const int sek = SC_TAB[i];
        for (int j = 0; j < 18; ++j) ASSERT_EQ(s[size_t(b + j)], 0xFF);
        for (int j = 18; j < 21; ++j) ASSERT_EQ(s[size_t(b + j)], 0xA1 | M);
        EXPECT_EQ(s[size_t(b + 21)], 0xFE);
        EXPECT_EQ(s[size_t(b + 22)], 0); EXPECT_EQ(s[size_t(b + 23)], 0);
        EXPECT_EQ(s[size_t(b + 24)], 0); EXPECT_EQ(s[size_t(b + 25)], sek);
        std::vector<uint8_t> id;
        for (int j = 18; j < 28; ++j) id.push_back(static_cast<uint8_t>(s[size_t(b + j)]));
        EXPECT_EQ(crc(id), 0) << "Kennfeld " << sek;
        EXPECT_EQ(s[size_t(b + 52)], 0xA1 | M);
        EXPECT_EQ(s[size_t(b + 53)], 0xFB);
        std::vector<uint8_t> df;
        for (int j = 52; j < 568; ++j) df.push_back(static_cast<uint8_t>(s[size_t(b + j)]));
        EXPECT_EQ(crc(df), 0) << "Datenfeld " << sek;
        if (sek == 10)
            for (int j = 0; j < 512; ++j) ASSERT_EQ(s[size_t(b + 54 + j)], d[size_t(j)]);
        else if (sek == 1)
            EXPECT_EQ(s[size_t(b + 54)], 'D');   // Z0/K0/S1 = PAR/BTT („DEFEKT")
        else
            EXPECT_EQ(s[size_t(b + 54)], 0xE5);
    }
    // Ende hinter dem letzten Slot: Löschbyte
    for (int j = 18 * Platte::SLOT; j < N; ++j) EXPECT_EQ(s[size_t(j)], 0);
}

TEST(Platte, KopfversatzRotiertDieSektorfolgeJeKopfUmEinePosition)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    for (int k = 0; k < 5; ++k) {
        const auto& s = p.spur(7, k);
        for (int i = 0; i < 18; ++i) {
            const int b = i * Platte::SLOT;
            EXPECT_EQ(s[size_t(b + 22)] | (s[size_t(b + 23)] << 8), 7);
            EXPECT_EQ(s[size_t(b + 24)], k);
            EXPECT_EQ(s[size_t(b + 25)], SC_TAB[(i - k + 18) % 18]) << "Kopf " << k << " Slot " << i;
        }
    }
    // Kopf 1 beginnt mit 18 1 10 2 … (wdc_firmware.md §8)
    EXPECT_EQ(p.spur(0, 1)[25], 18);
    EXPECT_EQ(p.spur(0, 1)[Platte::SLOT + 25], 1);
}

TEST(Platte, SiebzehnSektorenNehmenDieErsten17DerTabelle)
{
    auto tp = TempPlatte::leer("s17.img");
    { std::ofstream f(tp.path(), std::ios::binary); }
    std::filesystem::resize_file(tp.path(), uint64_t(615) * 4 * 17 * 512);
    Platte::Config cfg;
    cfg.geometrie = Geometrie{615, 4, 17};
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp, cfg));
    const auto& s = p.spur(3, 2);
    std::vector<int> folge;
    for (int i = 0; i < 17; ++i) folge.push_back(s[size_t(i * Platte::SLOT + 25)]);
    for (int i = 0; i < 17; ++i) EXPECT_EQ(folge[size_t(i)], SC_TAB[(i - 2 + 17) % 17]);
    for (int i = 17 * Platte::SLOT; i < N; ++i) EXPECT_EQ(s[size_t(i)], 0);
    const auto z = p.zerlege(3, 2, s);
    EXPECT_TRUE(z.formatiert);
    EXPECT_EQ(z.daten.size(), 17u);
}

// ─── Zerlegung und Zurückschreiben ───────────────────────────────────────────

TEST(Platte, ZerlegungDerSyntheseLiefertAlleSektoren)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    for (int sek = 1; sek <= 18; ++sek) {
        const auto d = muster(5, 3, sek);
        ASSERT_TRUE(p.sektorSchreiben(5, 3, sek, d.data()));
    }
    const auto z = p.zerlege(5, 3, p.spur(5, 3));
    EXPECT_TRUE(z.formatiert);
    EXPECT_EQ(z.kennfelder, 18);
    ASSERT_EQ(z.daten.size(), 18u);
    for (int sek = 1; sek <= 18; ++sek) {
        const auto d = muster(5, 3, sek);
        EXPECT_EQ(std::memcmp(z.daten.at(sek).data(), d.data(), 512), 0) << sek;
    }
    // falscher Zylinder/Kopf: kein passendes Kennfeld
    EXPECT_EQ(p.zerlege(5, 2, p.spur(5, 3)).kennfelder, 0);
    EXPECT_EQ(p.zerlege(4, 3, p.spur(5, 3)).kennfelder, 0);
}

TEST(Platte, GeschriebenesDatenfeldLandetNachFlushImAbbild)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    const auto d = muster(0, 1, 18);   // Kopf 1, Slot 0 = Sektor 18
    datenfeldSchreiben(p, 1, 41, d);
    EXPECT_TRUE(p.schmutzig());
    std::array<uint8_t, 512> r{};
    ASSERT_TRUE(p.sektorLesen(0, 1, 18, r.data()));
    EXPECT_EQ(r[0], 0xE5);             // noch nicht zurückgeschrieben
    p.flush();
    EXPECT_FALSE(p.schmutzig());
    ASSERT_TRUE(p.sektorLesen(0, 1, 18, r.data()));
    EXPECT_EQ(r, d);
    ASSERT_TRUE(p.sektorLesen(0, 1, 1, r.data()));   // Nachbar unberührt
    EXPECT_EQ(r[0], 0xE5);
    // der Strom bleibt (eigen) und wird nicht neu synthetisiert
    EXPECT_EQ(p.spur(0, 1)[52], 0xA1 | M);
}

TEST(Platte, DatenfeldMitFalscherCrcLaesstDenAltenInhalt)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    const auto d = muster(0, 0, 1);
    datenfeldSchreiben(p, 0, 41, d);
    p.schreibe(0, 41 + 11 + 2 + 100, 0x00, 2);   // ein Datenbyte verfälschen
    p.flush();
    std::array<uint8_t, 512> r{};
    ASSERT_TRUE(p.sektorLesen(0, 0, 1, r.data()));
    EXPECT_EQ(r, parSektor(*typNachName("K5504.50")));
    EXPECT_TRUE(p.formatiert(0, 0));
}

TEST(Platte, GeloeschteSpurIstUnformatiertUndLiefertNullen)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    for (int i = 0; i < N; ++i) p.schreibe(2, i, 0x00, 5);   // Spur löschen (Kommando 84)
    p.flush();
    EXPECT_FALSE(p.formatiert(0, 2));
    EXPECT_TRUE(p.formatiert(0, 1));
    // Abbild unverändert
    std::array<uint8_t, 512> r{};
    ASSERT_TRUE(p.sektorLesen(0, 2, 5, r.data()));
    EXPECT_EQ(r[0], 0xE5);
    // neu synthetisiert (nach Zylinderwechsel) = nur 00
    p.schritt(true, 10); p.schritt(false, 20);
    for (uint16_t w : p.spur(0, 2)) ASSERT_EQ(w, 0);
}

TEST(Platte, FormatierenEinerUnformatiertenSpurInEigenerLage)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    p.setzeFormatiert(9, 0, false);
    for (int i = 0; i < 9; ++i) p.schritt(true, uint64_t(i));
    ASSERT_EQ(p.zylinder(), 9);
    for (uint16_t w : p.spur(9, 0)) ASSERT_EQ(w, 0);
    // „Firmware" schreibt Kennfelder im Abstand 567 ab Byte 20, Datenfelder dahinter
    std::vector<uint16_t> s(N, 0);
    std::array<int, 18> lage{};
    for (int i = 0; i < 18; ++i) {
        lage[size_t(i)] = 20 + i * 567;
        kennfeldSchreiben(s, lage[size_t(i)], 9, 0, SC_TAB[i]);
    }
    for (int i = 0; i < N; ++i) p.schreibe(0, i, s[size_t(i)], 100);
    for (int i = 0; i < 18; ++i) datenfeldSchreiben(p, 0, lage[size_t(i)] + 41, muster(9, 0, SC_TAB[i]), 100);
    p.flush();
    EXPECT_TRUE(p.formatiert(9, 0));
    std::array<uint8_t, 512> r{};
    for (int sek = 1; sek <= 18; ++sek) {
        ASSERT_TRUE(p.sektorLesen(9, 0, sek, r.data()));
        EXPECT_EQ(r, muster(9, 0, sek)) << sek;
    }
}

TEST(Platte, TeilweiseFormatierteSpurGiltAlsUnformatiert)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    std::vector<uint16_t> s(N, 0);
    for (int i = 0; i < 17; ++i) kennfeldSchreiben(s, i * 578, 0, 3, SC_TAB[i]);
    for (int i = 0; i < N; ++i) p.schreibe(3, i, s[size_t(i)], 1);
    const auto z = p.zerlege(0, 3, p.spur(0, 3));
    EXPECT_EQ(z.kennfelder, 17);
    EXPECT_FALSE(z.formatiert);
    p.flush();
    EXPECT_FALSE(p.formatiert(0, 3));
}

TEST(Platte, KennfeldUeberDieIndexnahtWirdErkannt)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    std::vector<uint16_t> s(p.spur(0, 4));
    // ganze Spur um 10 400 Byte drehen: Slot 0 überspannt die Naht
    std::vector<uint16_t> r(N);
    for (int i = 0; i < N; ++i) r[size_t((i + 10400) % N)] = s[size_t(i)];
    const auto z = p.zerlege(0, 4, r);
    EXPECT_TRUE(z.formatiert);
    EXPECT_EQ(z.daten.size(), 18u);
}

TEST(Platte, KopfJenseitsDerKopfzahlLiefertNichts)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    EXPECT_EQ(p.lies(5, 21), 0);
    p.schreibe(5, 21, 0xFE, 1);
    EXPECT_FALSE(p.schmutzig());
    EXPECT_EQ(p.lies(-1, 21), 0);
}

// ─── Mechanik und Zurückschreiben bei Bewegung/Pause ─────────────────────────

TEST(Platte, SchrittSpurNullSeekCompleteUndBereit)
{
    TempPlatte tp;
    Platte::Config cfg;
    cfg.startzeit_takte = 1000;
    cfg.seek_takte = 500;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp, cfg));
    EXPECT_FALSE(p.bereit(999));
    EXPECT_TRUE(p.bereit(1000));
    EXPECT_TRUE(p.spur0());
    EXPECT_TRUE(p.seekFertig(0));
    p.schritt(false, 2000);            // nach außen an Spur 0: bleibt
    EXPECT_EQ(p.zylinder(), 0);
    EXPECT_FALSE(p.seekFertig(2499));
    EXPECT_TRUE(p.seekFertig(2500));
    p.schritt(true, 3000);
    EXPECT_EQ(p.zylinder(), 1);
    EXPECT_FALSE(p.spur0());
    for (int i = 0; i < 2000; ++i) p.schritt(true, 4000);
    EXPECT_EQ(p.zylinder(), 1023);     // begrenzt
    EXPECT_FALSE(p.seekFertig(4499));
    EXPECT_TRUE(p.seekFertig(4500));
    Platte zu;
    EXPECT_FALSE(zu.bereit(1'000'000));
}

TEST(Platte, ZylinderwechselSchreibtGeaenderteSpurenZurueck)
{
    TempPlatte tp;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp));
    datenfeldSchreiben(p, 0, 41, muster(0, 0, 1));
    datenfeldSchreiben(p, 2, 41 + 578, muster(0, 2, 18));   // Kopf 2 Slot 1 = SC_TAB[17] = 18
    p.schritt(true, 50);
    EXPECT_FALSE(p.schmutzig());
    std::array<uint8_t, 512> r{};
    ASSERT_TRUE(p.sektorLesen(0, 0, 1, r.data()));
    EXPECT_EQ(r, muster(0, 0, 1));
    ASSERT_TRUE(p.sektorLesen(0, 2, 18, r.data()));
    EXPECT_EQ(r, muster(0, 2, 18));
}

TEST(Platte, AutoFlushErstNachDerSchreibpause)
{
    TempPlatte tp;
    Platte::Config cfg;
    cfg.flush_pause = 1000;
    Platte p;
    ASSERT_TRUE(p.oeffnen(tp, cfg));
    datenfeldSchreiben(p, 0, 41 + 578, muster(0, 0, 10), 5000);
    EXPECT_FALSE(p.autoFlush(5999));
    EXPECT_TRUE(p.schmutzig());
    EXPECT_TRUE(p.autoFlush(6000));
    EXPECT_FALSE(p.schmutzig());
    EXPECT_FALSE(p.autoFlush(9000));   // nichts mehr zu tun
    std::array<uint8_t, 512> r{};
    ASSERT_TRUE(p.sektorLesen(0, 0, 10, r.data()));
    EXPECT_EQ(r, muster(0, 0, 10));
}

TEST(Platte, SchliessenSchreibtZurueck)
{
    TempPlatte tp;
    {
        Platte p;
        ASSERT_TRUE(p.oeffnen(tp));
        datenfeldSchreiben(p, 4, 41, muster(0, 4, SC_TAB[(0 - 4 + 18) % 18]));
    }
    Platte q;
    ASSERT_TRUE(q.oeffnen(tp));
    std::array<uint8_t, 512> r{};
    const int sek = SC_TAB[(0 - 4 + 18) % 18];
    ASSERT_TRUE(q.sektorLesen(0, 4, sek, r.data()));
    EXPECT_EQ(r, muster(0, 4, sek));
}

// ─── Save-State ──────────────────────────────────────────────────────────────

TEST(Platte, SaveStateRundreise)
{
    TempPlatte tp;
    Platte::Config cfg;
    cfg.seek_takte = 77;
    Platte a;
    ASSERT_TRUE(a.oeffnen(tp, cfg));
    for (int i = 0; i < 3; ++i) a.schritt(true, 100);
    a.setzeFormatiert(500, 1, false);
    datenfeldSchreiben(a, 1, 41, muster(3, 1, 18), 200);   // eigen + schmutzig
    std::vector<uint8_t> st;
    a.serialize(st);

    Platte b;
    ASSERT_TRUE(b.oeffnen(tp, cfg));
    const uint8_t* p = st.data();
    ASSERT_TRUE(b.deserialize(p, st.data() + st.size()));
    EXPECT_EQ(p, st.data() + st.size());
    EXPECT_EQ(b.zylinder(), 3);
    EXPECT_FALSE(b.seekFertig(176));
    EXPECT_TRUE(b.seekFertig(177));
    EXPECT_FALSE(b.formatiert(500, 1));
    EXPECT_TRUE(b.schmutzig());
    EXPECT_EQ(b.spur(3, 1), a.spur(3, 1));
    std::vector<uint8_t> st2;
    b.serialize(st2);
    EXPECT_EQ(st, st2);

    // andere Geometrie ⇒ abgelehnt, Zustand unverändert
    TempPlatte andere("D5126", "andere.img");
    Platte c;
    ASSERT_TRUE(c.oeffnen(andere));
    p = st.data();
    EXPECT_FALSE(c.deserialize(p, st.data() + st.size()));
    EXPECT_EQ(c.zylinder(), 0);
    // abgeschnitten ⇒ abgelehnt
    Platte d;
    ASSERT_TRUE(d.oeffnen(tp, cfg));
    p = st.data();
    EXPECT_FALSE(d.deserialize(p, st.data() + st.size() / 2));
    EXPECT_EQ(d.zylinder(), 0);
}
