/**
 * @file test_tastatur_k7673.cpp
 * @brief Tastatur K7673.09 (AP P20b, Entwurf 28 §5): Verhaltensmodell gegen die ORIGINAL-
 *        Firmware auf dem Z8-Kern (Differenzialtest), Leitungsprotokoll, Wiederholung,
 *        3-Tasten-Grenze, LEDs, Überlauf, Save-State.
 *
 * Der Firmware-Prüfstand ist der aus der Praxisprobe P19c (test_z8_firmware.cpp): Matrix an
 * P0/P1 (Zeile = P2.0–2), Sendefreigabe P30 = 0, P31 = 0; Takt P36 / Daten P37 werden an
 * steigenden Taktflanken dekodiert.  Gleiche Zeitbasis: interne Takte bei gleichem Quarz.
 */
#include <gtest/gtest.h>

#include <cstdlib>
#include <set>
#include <vector>

#include "core/peripherals/p8000_terminal_hw/rom_k7673.h"
#include "core/peripherals/p8000_terminal_hw/tastatur_k7673.h"
#include "core/primitives/z8.h"

using namespace k1520::p8000;

namespace {

struct Byte { uint8_t wert; uint64_t start; };   ///< Zeit = steigende Flanke des Startbits

/// Dekoder an der Leitung (wie das Terminal: steigende Flanke, ¬Daten, Startbit zuerst).
struct Dekoder {
    bool takt = false, imRahmen = false;
    int bit = 0;
    uint8_t wert = 0;
    uint64_t start = 0;
    std::vector<Byte> bytes;
    void pegel(uint64_t t, bool neuTakt, bool d) {
        if (neuTakt && !takt) {
            if (!imRahmen) { if (!d) { imRahmen = true; bit = 0; wert = 0; start = t; } }
            else {
                if (!d) wert = uint8_t(wert | (1u << bit));
                if (++bit == 8) { bytes.push_back({wert, start}); imRahmen = false; }
            }
        }
        takt = neuTakt;
    }
};

struct Firmware {
    Z8 cpu{Z8Config::ub8820()};
    std::set<std::pair<int, int>> gedrueckt;
    Dekoder dek;
    uint8_t p2 = 0;
    Firmware() {
        cpu.programmLesen = [](uint16_t a) { return a < 2048 ? K7673_09[a] : uint8_t(0xFF); };
        cpu.portEingang = [this](int port, uint8_t) -> uint8_t {
            const int zeile = cpu.portPegel(2) & 7;
            uint8_t v = 0xFF;
            for (auto& k : gedrueckt)
                if (k.first == zeile && (k.second >> 3) == port) v = uint8_t(v & ~(1u << (k.second & 7)));
            return v;
        };
        cpu.portAusgang = [this](int port, uint8_t pegel, uint8_t) {
            if (port == 3) dek.pegel(cpu.takte, pegel & 0x40, pegel & 0x80);
            if (port == 2) p2 = pegel;
        };
        cpu.setPort(3, 0xF0);
        cpu.reset();
    }
    void lauf(uint64_t bis) { cpu.laufeBis(bis); }
    uint64_t takte() const { return cpu.takte; }
};

struct Modell {
    TastaturK7673 k;
    Dekoder dek;
    void lauf(uint64_t bis) {
        k.laufeBis(bis);
        for (auto& f : k.holeFlanken(bis)) dek.pegel(f.takt, f.taktPegel, f.daten);
    }
};

std::vector<uint8_t> werte(const std::vector<Byte>& b) {
    std::vector<uint8_t> v;
    for (auto& x : b) v.push_back(x.wert);
    return v;
}

std::string hex(const std::vector<uint8_t>& v) {
    std::string s;
    char b[4];
    for (uint8_t x : v) { std::snprintf(b, sizeof b, "%02X ", x); s += b; }
    return s;
}

/// Ein Ablauf: Liste (Zeit, Zeile, Spalte, gedrückt) — auf beiden Seiten gleich ausgeführt.
struct Schritt { uint64_t t; int z, s; bool an; };

void ablauf(const std::vector<Schritt>& folge, uint64_t ende, Firmware& fw, Modell& m) {
    for (const auto& s : folge) {
        fw.lauf(s.t);
        m.lauf(s.t);
        if (s.an) { fw.gedrueckt.insert({s.z, s.s}); m.k.druecke(s.z, s.s); }
        else { fw.gedrueckt.erase({s.z, s.s}); m.k.loslassen(s.z, s.s); }
    }
    fw.lauf(ende);
    m.lauf(ende);
}

constexpr uint64_t MS = 4000;   // 8 MHz Quarz ⇒ 4 MHz intern

}  // namespace

TEST(TastaturK7673Diff, EinschaltenSendetAAWieDieFirmware)
{
    Firmware fw;
    Modell m;
    fw.lauf(50 * MS);
    m.lauf(50 * MS);
    ASSERT_EQ(werte(fw.dek.bytes), std::vector<uint8_t>{0xAA});
    ASSERT_EQ(werte(m.dek.bytes), std::vector<uint8_t>{0xAA});
    EXPECT_EQ(fw.dek.bytes[0].start, m.dek.bytes[0].start);
}

/// Jede der 128 Matrixpositionen: drücken (300 ms), loslassen — gleiche Codefolge
/// (Make, E0, Folgen, Break) und Zeitpunkte im Rahmen der Abtastung.
TEST(TastaturK7673Diff, JedeMatrixpositionGleicheCodefolge)
{
    for (int z = 0; z < 8; ++z)
        for (int s = 0; s < 16; ++s) {
            Firmware fw;
            Modell m;
            ablauf({{100 * MS, z, s, true}, {400 * MS, z, s, false}}, 700 * MS, fw, m);
            const auto a = werte(fw.dek.bytes), b = werte(m.dek.bytes);
            EXPECT_EQ(hex(a), hex(b)) << "Zeile " << z << " Spalte " << s;
            if (a.size() == b.size())
                for (size_t i = 0; i < a.size(); ++i) {
                    const int64_t d = int64_t(fw.dek.bytes[i].start) - int64_t(m.dek.bytes[i].start);
                    EXPECT_LT(std::llabs(d), 2 * MS)   /* Abweichung ≤ 1,5 ms: Sendeschleife nur als Konstante */ << "Zeile " << z << " Spalte " << s << " Byte " << i;
                }
        }
}

TEST(TastaturK7673Diff, WiederholungDerZuletztGedruecktenTaste)
{
    Firmware fw;
    Modell m;
    ablauf({{100 * MS, 0, 0, true}, {1600 * MS, 0, 0, false}}, 1800 * MS, fw, m);
    const auto a = werte(fw.dek.bytes), b = werte(m.dek.bytes);
    EXPECT_EQ(hex(a), hex(b));
    // AA, Make, nach ≈ 500 ms alle ≈ 100 ms (am Rundenende) wiederholt, Break
    int makes = 0;
    for (uint8_t x : b) makes += x == 0x02;
    EXPECT_GE(makes, 9);
    EXPECT_LE(makes, 12);
    // erste Wiederholung frühestens 50 Ticks (500 ms) nach dem Make
    std::vector<uint64_t> t;
    for (auto& x : m.dek.bytes) if (x.wert == 0x02) t.push_back(x.start);
    ASSERT_GE(t.size(), 2u);
    EXPECT_GE(t[1] - t[0], 500 * MS);
    EXPECT_LT(t[1] - t[0], 560 * MS);
    for (size_t i = 2; i < t.size(); ++i) { EXPECT_GE(t[i] - t[i - 1], 100 * MS); EXPECT_LT(t[i] - t[i - 1], 160 * MS); }
}

TEST(TastaturK7673Diff, ZweiTastenWiederholtWirdNurDieLetzte)
{
    Firmware fw;
    Modell m;
    ablauf({{100 * MS, 1, 6, true}, {200 * MS, 2, 0, true}, {1000 * MS, 2, 0, false}, {1200 * MS, 1, 6, false}},
           1500 * MS, fw, m);
    EXPECT_EQ(hex(werte(fw.dek.bytes)), hex(werte(m.dek.bytes)));
}

TEST(TastaturK7673Diff, MehrAlsDreiTastenWerdenVerworfen)
{
    Firmware fw;
    Modell m;
    ablauf({{100 * MS, 1, 0, true}, {100 * MS, 1, 1, true}, {100 * MS, 1, 2, true}, {100 * MS, 1, 3, true},
            {400 * MS, 1, 3, false}, {700 * MS, 1, 0, false}, {700 * MS, 1, 1, false}, {700 * MS, 1, 2, false}},
           1000 * MS, fw, m);
    const auto b = werte(m.dek.bytes);
    EXPECT_EQ(hex(werte(fw.dek.bytes)), hex(b));
    // vier Tasten: nichts; nach dem Loslassen der vierten: die drei Makes, dann die drei Breaks
    EXPECT_EQ(hex(b), hex({0xAA, 0x10, 0x12, 0x14, 0x90, 0x92, 0x94}));
}

TEST(TastaturK7673Diff, PauseMitUndOhne1DTaste)
{
    Firmware fw;
    Modell m;
    ablauf({{100 * MS, 0, 14, true}, {300 * MS, 0, 14, false},                        // PAUSE allein
            {500 * MS, 0, 6, true}, {700 * MS, 0, 14, true}, {900 * MS, 0, 14, false},   // mit 1DH
            {1100 * MS, 0, 6, false}, {1300 * MS, 5, 8, true}, {1500 * MS, 5, 8, false}}, // „00"-Taste
           1800 * MS, fw, m);
    const auto b = werte(m.dek.bytes);
    EXPECT_EQ(hex(werte(fw.dek.bytes)), hex(b));
    EXPECT_EQ(hex(b), hex({0xAA, 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5,
                           0x1D, 0xE0, 0x46, 0xE0, 0xC6, 0x9D,
                           0xE1, 0x52, 0xE1, 0x52, 0xE1, 0xD2, 0xE1, 0xD2}));
}

TEST(TastaturK7673Diff, LedTastenSchaltenEinmalJeDruck)
{
    Firmware fw;
    Modell m;
    ablauf({{100 * MS, 7, 12, true}, {300 * MS, 7, 12, false},     // ON/OFF
            {500 * MS, 2, 6, true}, {1600 * MS, 2, 6, false},      // CAPS LOCK lange: Wiederholung, LED einmal
            {1800 * MS, 7, 14, true}, {2000 * MS, 7, 14, false},   // MODE
            {2200 * MS, 7, 12, true}, {2400 * MS, 7, 12, false}},  // ON/OFF wieder aus
           2700 * MS, fw, m);
    EXPECT_EQ(hex(werte(fw.dek.bytes)), hex(werte(m.dek.bytes)));
    EXPECT_EQ(m.k.leds(), (fw.p2 >> 4) & 7);
    EXPECT_EQ(m.k.leds(), 0x06);   // CAPS + MODE an, ON/OFF zweimal = aus
}

TEST(TastaturK7673, LeitungsprotokollEinesBytes)
{
    TastaturK7673 k;
    k.laufeBis(50 * MS);
    auto f = k.holeFlanken(50 * MS);
    // Ruhe nach dem Byte: Takt 0, Daten 1
    ASSERT_FALSE(f.empty());
    EXPECT_FALSE(f.back().taktPegel);
    EXPECT_TRUE(f.back().daten);
    // genau 1 + 8 + 1 steigende Flanken (Vorlauf mit Daten 1, Startbit, 8 Datenbits)
    int steigend = 0;
    bool t = true;
    for (auto& x : f) { if (x.taktPegel && !t) ++steigend; t = x.taktPegel; }
    EXPECT_EQ(steigend, 10);
}

TEST(TastaturK7673, TabelleAusDemAbzug)
{
    TastaturK7673 k;
    EXPECT_EQ(k.makeFolge(0, 0), std::vector<uint8_t>{0x02});          // „1"
    EXPECT_EQ(k.makeFolge(0, 8), (std::vector<uint8_t>{0xE0, 0x4D}));  // Cursor rechts
    EXPECT_EQ(k.makeFolge(0, 14), (std::vector<uint8_t>{0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5}));
    EXPECT_EQ(k.makeFolge(2, 0), std::vector<uint8_t>{0x1E});          // „a"
    EXPECT_EQ(k.makeFolge(7, 7), std::vector<uint8_t>{0x39});          // Leertaste
    int belegt = 0;
    for (int z = 0; z < 8; ++z) for (int s = 0; s < 16; ++s) belegt += (k.merker(z, s) | k.code(z, s)) != 0;
    EXPECT_EQ(belegt, 105);   // Befund §4
}

TEST(TastaturK7673, UeberlaufMarkeFF)
{
    TastaturK7673 k;
    for (int i = 0; i < 20; ++i) k.einreihen(uint8_t(i));
    EXPECT_EQ(k.pufferBelegung(), 17u);   // AA + 15 Bytes + FF
    k.laufeBis(400 * MS);
    const auto& g = k.gesendet();
    ASSERT_EQ(g.size(), 17u);
    EXPECT_EQ(g.front(), 0xAA);
    EXPECT_EQ(g[15], 14);
    EXPECT_EQ(g.back(), 0xFF);
}

TEST(TastaturK7673, SaveStateSetztGenauFort)
{
    TastaturK7673 a;
    a.laufeBis(100 * MS);
    a.druecke(2, 0);
    a.laufeBis(150 * MS);
    std::vector<uint8_t> st;
    a.serialize(st);
    TastaturK7673 b;
    const uint8_t* p = st.data();
    ASSERT_TRUE(b.deserialize(p, st.data() + st.size()));
    a.laufeBis(900 * MS);
    b.laufeBis(900 * MS);
    auto fa = a.holeFlanken(900 * MS), fb = b.holeFlanken(900 * MS);
    ASSERT_EQ(fa.size(), fb.size());
    for (size_t i = 0; i < fa.size(); ++i) EXPECT_EQ(fa[i].takt, fb[i].takt);
}
