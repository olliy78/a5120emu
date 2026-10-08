/**
 * @file test_p8000_wdc_rom_laufwerk.cpp
 * @brief AP P24: Tabelle „Firmware-Fassung → Laufwerkstyp" des Kerns gegen die BYTES der
 *        EPROM-Abzüge.  Bis WDC 4.0 stehen Köpfe, Zylinder−1, Vorkompensationsbeginn und
 *        Sektoren je Spur als Konstanten im Abzug (`doc/p8000/eproms/WDC/eprom_diffs.txt`, dort
 *        „Sektoren" als Zylinder falsch beschriftet: 0x03FF = 1023 = Zylinder − 1).  Die Fundstellen
 *        gelten für 3.0 … 3.4 und getrennt für 4.0; die Endung des Abzugsnamens (`_01/_02/_04/_05`)
 *        wählt das Laufwerk.
 */
#include "core/cards/p8000/rom_wdc.h"
#include "core/cards/p8000/wdc.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> abzug(const std::string& name)
{
    std::ifstream f(std::string(P8000_ABZUG_DIR) + "/WDC/" + name, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Fundstellen {
    int heads[2];
    int zyl_minus_1;     ///< Wort, LSB zuerst
    int vorkomp;         ///< Wort: Zylinder, ab dem vorkompensiert wird
    int sektoren[2];     ///< Operand von LD E,n
};
constexpr Fundstellen k3x = {{0x106, 0x129}, 0x125, 0x0F8, {0x10A, 0x123}};
constexpr Fundstellen k40 = {{0x12A, 0x14D}, 0x149, 0x11F, {0x12E, 0x147}};

unsigned wort(const std::vector<uint8_t>& r, int a) { return r[size_t(a)] | (r[size_t(a) + 1] << 8); }

/// Alle Parameter an den Fundstellen müssen zum Laufwerk der Endung passen.
void pruefe(const std::string& datei, const Fundstellen& f, int endung)
{
    SCOPED_TRACE(datei);
    const auto r = abzug(datei);
    ASSERT_EQ(r.size(), 4096u);
    const auto* t = P8000Wdc::romLaufwerkNachEndung(endung);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(r[size_t(f.heads[0])], t->g.koepfe);
    EXPECT_EQ(r[size_t(f.heads[1])], t->g.koepfe);
    EXPECT_EQ(wort(r, f.zyl_minus_1) + 1, t->g.zylinder);
    EXPECT_EQ(wort(r, f.vorkomp), t->vorkomp);
    for (int a : f.sektoren) {
        EXPECT_EQ(r[size_t(a) - 1], 0x1E) << "LD E,n erwartet";
        EXPECT_EQ(r[size_t(a)], t->g.sektoren);
    }
}

}  // namespace

TEST(P8000WdcRomLaufwerk, TabelleStimmtMitAllenAbzuegenDerSerie3Ueberein)
{
    for (const char* v : {"3.0_01", "3.0_02", "3.0_04", "3.2_04", "3.2_05", "3.3_04", "3.3_05", "3.4_04", "3.4_05"})
        pruefe(std::string("WDC_1_") + v, k3x, v[std::string(v).size() - 1] - '0');
}

TEST(P8000WdcRomLaufwerk, TabelleStimmtMitDemAbzug4_0Ueberein)
{
    pruefe("WDC_1_4.0_05", k40, 5);
}

TEST(P8000WdcRomLaufwerk, EingebundeneAbzuegeLiefernDieTabelle)
{
    // Dieselben Fundstellen im ERZEUGTEN Header, den der Emulator wirklich ausführt.
    const std::vector<uint8_t> a(std::begin(P8K_WDC_3_4_05), std::end(P8K_WDC_3_4_05));
    const std::vector<uint8_t> b(std::begin(P8K_WDC_4_0_05), std::end(P8K_WDC_4_0_05));
    const auto* t34 = P8000Wdc::romLaufwerk(P8000Wdc::Config::Firmware::V3_4_05);
    const auto* t40 = P8000Wdc::romLaufwerk(P8000Wdc::Config::Firmware::V4_0_05);
    ASSERT_NE(t34, nullptr);
    ASSERT_NE(t40, nullptr);
    EXPECT_EQ(a[size_t(k3x.heads[0])], t34->g.koepfe);
    EXPECT_EQ(wort(a, k3x.zyl_minus_1) + 1, t34->g.zylinder);
    EXPECT_EQ(a[size_t(k3x.sektoren[0])], t34->g.sektoren);
    EXPECT_EQ(b[size_t(k40.heads[0])], t40->g.koepfe);
    EXPECT_EQ(wort(b, k40.zyl_minus_1) + 1, t40->g.zylinder);
    EXPECT_EQ(b[size_t(k40.sektoren[0])], t40->g.sektoren);
    // K5504.50 = Anwender-Mitschnitt WDC_V.3.4.05: 1024 × 5 × 18 Sektoren
    EXPECT_EQ(std::string(t34->name), "K5504.50");
    EXPECT_EQ(t34->g.bytes(), 47'185'920u);
}

TEST(P8000WdcRomLaufwerk, Fassung4_2IstLaufwerksunabhaengig)
{
    EXPECT_EQ(P8000Wdc::romLaufwerk(P8000Wdc::Config::Firmware::V4_2), nullptr);
    EXPECT_EQ(P8000Wdc::romLaufwerkNachEndung(3), nullptr);
    EXPECT_STREQ(P8000Wdc::firmwareName(P8000Wdc::Config::Firmware::V3_4_05), "3.4.05");
}
