/**
 * @file test_k8915_format_verfahren.cpp
 * @brief K8915, AP-E4f Schritt 3: jedes zweiseitige 80-Zylinder-Verfahren von
 *        FORMAT.COM (Hilfetabelle `[H]ELP`, §4.4 in doc/design/16_k8915.md) auf einer
 *        Leerdiskette in B:, Umfang Smoke (Zylinder 00–02, beide Seiten).
 *
 * Geprüft wird, was FORMAT.COM selbst prüft (sein Prüf-Lesen vergleicht die ganze Spur
 * ⇒ „FUNCTION COMPLETE“ ohne „ERROR“; das KROS-Verfahren 33 fragt stattdessen nach dem
 * Informationsblock auf Spur 0 und geht mit `N` direkt zu EXIT) und was danach auf dem
 * Medium steht: je Spur die
 * Sektorzahl und -größe der Tabelle, beide CRCs gültig, und die Indexmarke genau bei den
 * Verfahren „WITH INDEXMARK“ (Fussnote 4) — die ohne (Fussnote 3) prüfen ihre erste
 * Spur also ohne MK = 1.  Label `format_matrix` (`tools/dev.sh test-matrix`), wie die
 * Menü-Matrix des A5120.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"
#include "tests/system/k8915_bedienung.h"

using k1520test::TempDisk;
using k1520test::vramLines;
using namespace k8915test;

namespace {

struct Verfahren {
    const char* code;
    int         sektoren;
    int         groesse;
    bool        indexmarke;
    bool        kros = false;   ///< Fussnote 1: fragt danach nach dem Informationsblock
};

void PrintTo(const Verfahren& v, std::ostream* os) { *os << v.code; }

class K8915FormatVerfahren : public ::testing::TestWithParam<Verfahren> {};

bool hatIndexmarke(const TrackImage& t) {
    return std::find(t.marks.begin(), t.marks.end(), MarkType::Index) != t.marks.end();
}

}  // namespace

TEST_P(K8915FormatVerfahren, SpurenNullBisZweiBeideSeiten)
{
    const Verfahren v = GetParam();
    TempDisk a("k8915scpx_boot1.hfe");
    TempDisk b = TempDisk::empty(std::string("k8915_verfahren_") + v.code + ".hfe");
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
    ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);

    FormatLauf f;
    f.verfahren = v.code;
    f.letzte    = "02";
    const std::string bild = formatiere(m, f, 60'000'000);
    if (v.kros)   // Fussnote 1: Frage nach dem Informationsblock statt „COMPLETE“
        ASSERT_NE(bild.find("TRACK 0 WITH 128 BYTE/SECTOR: N"), std::string::npos) << bild;
    else
        ASSERT_NE(bild.find("FUNCTION COMPLETE"), std::string::npos) << bild;
    EXPECT_EQ(bild.find("ERROR"), std::string::npos) << bild;
    EXPECT_EQ(m.afs().debugState().waitSpuren, 6u);

    const DiskImage* img = m.afs().drive(1).image();
    ASSERT_NE(img, nullptr);
    for (uint8_t zyl = 0; zyl <= 2; ++zyl)
        for (uint8_t kopf = 0; kopf <= 1; ++kopf) {
            const TrackImage& t = img->medium().track(zyl, kopf);
            ASSERT_FALSE(t.empty()) << "C" << int(zyl) << " H" << int(kopf);
            const auto sek = TrackCodec::parseTrack(t);
            ASSERT_EQ(static_cast<int>(sek.size()), v.sektoren)
                << "C" << int(zyl) << " H" << int(kopf);
            for (const auto& s : sek) {
                EXPECT_EQ(s.size, v.groesse);
                EXPECT_TRUE(s.id_crc_ok) << "C" << int(zyl) << " H" << int(kopf) << " S" << int(s.id);
                EXPECT_TRUE(s.data_crc_ok) << "C" << int(zyl) << " H" << int(kopf) << " S" << int(s.id);
                EXPECT_EQ(s.cyl, zyl);
            }
            EXPECT_EQ(hatIndexmarke(t), v.indexmarke) << "C" << int(zyl) << " H" << int(kopf);
        }
    EXPECT_TRUE(img->medium().track(3, 0).empty()) << "LAST TRACK 02: Zylinder 3 bleibt leer";
}

INSTANTIATE_TEST_SUITE_P(
    ZweiseitigAchtzigZylinder, K8915FormatVerfahren,
    ::testing::Values(Verfahren{"33", 26, 128, false, true},  // KROS 5110
                      Verfahren{"22", 16, 256, true},
                      Verfahren{"37", 16, 256, false},
                      Verfahren{"23", 9, 512, true},
                      Verfahren{"24", 5, 1024, true},
                      Verfahren{"31", 5, 1024, false}),
    [](const ::testing::TestParamInfo<Verfahren>& i) { return std::string("V") + i.param.code; });
