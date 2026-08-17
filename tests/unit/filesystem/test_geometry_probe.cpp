/**
 * @file test_geometry_probe.cpp
 * @brief GoogleTests für @ref GeometryProbe — Stufe 1 der Formaterkennung.
 *
 * Der Kern des Tests sind die **echten** Disketten des Projekts: jede muss erkannt
 * werden, und zwar mit dem richtigen Format an erster Stelle.  Umgekehrt muss ein
 * Abbild, zu dem es keinen Katalogeintrag gibt, **abgelehnt** werden — mit einer
 * Meldung, die die gemessene Geometrie nennt (doc/design/13_k1520disktool.md §12.3).
 *
 * @see core/filesystem/geometry_probe.h
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/filesystem/geometry_probe.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"

namespace {

std::string fixture(const char* name) {
    return (std::filesystem::path(FIXTURE_DIR) / name).string();
}

const FormatCatalog& katalog() {
    static FormatCatalog c = [] {
        std::string fatal;
        FormatCatalog k = FormatCatalog::load({K1520_FORMATS_DEFAULT}, &fatal);
        EXPECT_TRUE(fatal.empty()) << fatal;
        return k;
    }();
    return c;
}

/// @brief Fixture öffnen (self-describing) und vermessen.
std::vector<MeasuredTrack> vermessen(const char* name) {
    auto disk = DiskImage::open(fixture(name), std::nullopt, true);
    EXPECT_NE(disk, nullptr) << name;
    if (!disk) return {};
    return GeometryProbe::measure(disk->medium());
}

/// @brief Bestes erkanntes Format (leer, wenn keines passt).
std::string bestesFormat(const std::vector<MeasuredTrack>& m) {
    auto t = GeometryProbe::matchAll(m, katalog().formats());
    return t.empty() ? std::string{} : t.front().format->name;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Die echten Disketten des Projekts
// ─────────────────────────────────────────────────────────────────────────────

TEST(GeometryProbe, ErkenntCpaBootdiskette) {
    const auto m = vermessen("cpa_cpa780_k5601_clock.hfe");
    ASSERT_FALSE(m.empty());

    auto t = GeometryProbe::matchAll(m, katalog().formats());
    ASSERT_FALSE(t.empty()) << "cpa780-Bootdiskette wurde nicht erkannt";
    EXPECT_EQ(t.front().format->name, "cpa780");
    EXPECT_EQ(t.size(), 1u) << "die gemischte Geometrie ist eindeutig";
    EXPECT_TRUE(t.front().remarks().empty()) << t.front().remarks();
}

TEST(GeometryProbe, ErkenntGemischteScpxDiskette) {
    // Zylinder 0 in 16×256, ab Zylinder 1 in 5×1024 — dafür gibt es seit dem
    // DiskTool den Katalogeintrag scpx798 (vorher passte KEIN Format).
    const auto m = vermessen("scpx17_5x1024_k5601_hardy.hfe");
    ASSERT_FALSE(m.empty());
    EXPECT_EQ(bestesFormat(m), "scpx798");
}

TEST(GeometryProbe, ErkenntUdosDoppelseitigTrotzAltbestandUndSchaden) {
    // Der Referenzdatenträger ist ein echtes Fundstück: 77 UDOS-Spuren, dahinter drei
    // Spuren 9×512 aus einem früheren Leben, und auf c51h0 fehlt ein Sektor.
    // Beides darf die Erkennung nicht kippen — es wird gemeldet, nicht bestraft.
    const auto m = vermessen("udos_boot_scp.hfe");
    ASSERT_FALSE(m.empty());

    auto t = GeometryProbe::matchAll(m, katalog().formats());
    ASSERT_FALSE(t.empty()) << "UDOS-Referenzdiskette wurde nicht erkannt";
    EXPECT_EQ(t.front().format->name, "udos_ds77");
    EXPECT_GT(t.front().stray_tracks, 0)  << "Altbestand hinter Spur 76 nicht bemerkt";
    EXPECT_EQ(t.front().defect_tracks, 1) << "die Spur mit fehlendem Sektor";
    EXPECT_NE(t.front().remarks().find("Altbestand"), std::string::npos)
        << t.front().remarks();
}

TEST(GeometryProbe, MischtSeitenzahlNichtDurcheinander) {
    // Ohne die Kopfprüfung „passt" jedes doppelseitige Format auch auf eine
    // einseitige Diskette — die halbe Diskette wäre dann schlicht erfunden.
    auto disk = DiskImage::open(fixture("cpa_cpa780_k5601_clock.hfe"), std::nullopt, true);
    ASSERT_NE(disk, nullptr);
    const auto m = GeometryProbe::measure(disk->medium());

    const DiskFormat* ss = katalog().find("k5601_ss80_26x128");
    ASSERT_NE(ss, nullptr);
    // Umgekehrter Fall: DS-Format auf SS-Medium (hier über ein künstliches Medium).
    DiskMedium einseitig(80, 1, Encoding::MFM);
    einseitig.setTrack(0, 0, disk->medium().track(0, 0));
    const auto m1 = GeometryProbe::measure(einseitig);

    const DiskFormat* ds = katalog().find("k5601_26x128");
    ASSERT_NE(ds, nullptr);
    const GeometryMatch g = GeometryProbe::match(m1, *ds);
    EXPECT_FALSE(g.ok);
    EXPECT_NE(g.reason.find("koepfig"), std::string::npos) << g.reason;
    (void)ss;
    (void)m;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ablehnung mit Diagnose
// ─────────────────────────────────────────────────────────────────────────────

TEST(GeometryProbe, LehntUnbekannteGeometrieAbUndBeschreibtSie) {
    // cpa_mini ist eine synthetische Mini-Diskette (2 Zylinder, 4×128) — dafür gibt
    // es bewusst kein Format.  Erwartet wird: kein Treffer, aber eine Beschreibung,
    // die als Vorlage für einen Katalogeintrag taugt.
    const auto m = vermessen("cpa_mini.hfe");
    ASSERT_FALSE(m.empty());
    EXPECT_TRUE(bestesFormat(m).empty()) << "Mini-Diskette darf zu keinem Format passen";

    const std::string text = GeometryProbe::describe(m);
    EXPECT_NE(text.find("4 Sektoren à 128 B"), std::string::npos) << text;
    EXPECT_NE(text.find("mfm"), std::string::npos) << text;
    EXPECT_NE(text.find("Zylinder 0-1"), std::string::npos) << text;
}

TEST(GeometryProbe, LeerdisketteWirdAlsSolcheGemeldet) {
    DiskMedium leer(80, 2, Encoding::MFM);
    const auto m = GeometryProbe::measure(leer);
    ASSERT_FALSE(m.empty());
    EXPECT_EQ(GeometryProbe::lastFormattedCylinder(m), -1);

    const DiskFormat* f = katalog().find("cpa800");
    ASSERT_NE(f, nullptr);
    const GeometryMatch g = GeometryProbe::match(m, *f);
    EXPECT_FALSE(g.ok);
    EXPECT_NE(g.reason.find("unformatiert"), std::string::npos) << g.reason;
}

TEST(GeometryProbe, ZuVieleSektorenSindEinAnderesFormat) {
    // Zu WENIGE Sektoren sind ein Schaden, zu viele nicht — sonst würde eine
    // 16×256-Diskette als 15×256-Format durchgehen.
    auto disk = DiskImage::open(fixture("scpx17_cpa780_k5601.hfe"), std::nullopt, true);
    ASSERT_NE(disk, nullptr);
    const auto m = GeometryProbe::measure(disk->medium());

    const DiskFormat* kleiner = katalog().find("k5601_ss40_15x256");
    ASSERT_NE(kleiner, nullptr);
    const GeometryMatch g = GeometryProbe::match(m, *kleiner);
    EXPECT_FALSE(g.ok) << "16 Sektoren duerfen nicht als 15×256 durchgehen";
}

TEST(GeometryProbe, MehrdeutigkeitIstNormalUndWirdVollstaendigGemeldet) {
    // formats.yaml enthält geometrisch identische Einträge (cpa640 ≡ k5601_16x256).
    // Beide MÜSSEN gemeldet werden — welches Dateisystem darauf liegt, entscheidet
    // Stufe 2, nicht die Geometrie.
    const auto m = vermessen("scpx17_cpa780_k5601.hfe");
    ASSERT_FALSE(m.empty());

    auto t = GeometryProbe::matchAll(m, katalog().formats());
    ASSERT_GE(t.size(), 2u);
    std::vector<std::string> namen;
    for (const auto& x : t) namen.push_back(x.format->name);
    EXPECT_NE(std::find(namen.begin(), namen.end(), "cpa640"), namen.end());
    EXPECT_NE(std::find(namen.begin(), namen.end(), "k5601_16x256"), namen.end());
    // Beste zuerst: die ohne Altbestand.
    EXPECT_EQ(t.front().stray_tracks, 0);
}

/**
 * @test Eine EINZELNE beschaedigte Spur macht die Diskette nicht unlesbar.
 * @par Kriterium  Eine Spur, deren Sektor-IDs auch untereinander lueckenhaft sind, gilt
 *                 als Schaden (`defect_tracks`) — das Format passt weiter.  Eine Spur mit
 *                 LUECKENLOSEN IDs an anderer Stelle ist dagegen ein anderes Format und
 *                 schliesst aus.
 * @par Warum      Nachgemessen an `fmt_clock…_DS_H` (Schneider D, IDs 0xC1-0xC9): auf
 *                 einem von 40 Zylindern hatte der Parser Gap-Bytes (0x4E) fuer eine
 *                 Adressmarke gehalten.  Ohne diese Unterscheidung waere die ganze
 *                 Diskette „passt zu keinem Format" — wegen einer kaputten Spur.
 */
TEST(GeometryProbe, EinzelneBeschaedigteSpurIstEinSchadenKeinAnderesFormat) {
    const DiskFormat* f = katalog().find("k5601_ss40_9x512_id193");
    ASSERT_NE(f, nullptr);

    std::vector<MeasuredTrack> m;
    for (uint8_t c = 0; c < 40; ++c) {
        MeasuredTrack t;
        t.cyl = c; t.head = 0; t.formatted = true;
        t.sectors = 9; t.sector_size = 512; t.first_id = 193;
        t.ids_dense = true; t.encoding = Encoding::MFM;
        m.push_back(t);
    }

    ASSERT_TRUE(GeometryProbe::match(m, *f).ok) << GeometryProbe::match(m, *f).reason;

    // Kaputte Spur: falsche erste ID UND Luecken zwischen den IDs.
    m[16].first_id  = 78;
    m[16].ids_dense = false;
    const GeometryMatch beschaedigt = GeometryProbe::match(m, *f);
    EXPECT_TRUE(beschaedigt.ok) << beschaedigt.reason;
    EXPECT_EQ(beschaedigt.defect_tracks, 1);

    // Dieselbe Abweichung mit lueckenlosen IDs ist ein anderes Format.
    m[16].ids_dense = true;
    EXPECT_FALSE(GeometryProbe::match(m, *f).ok);
}

TEST(GeometryProbe, EinFehlenderErsterSektorIstEinSchadenKeinAnderesFormat) {
    // Genau so sieht eine Spur aus, der beim Formatieren der Sektor 1 misslang:
    // WENIGER Sektoren, lueckenlos, und entsprechend spaeter beginnend.  Auf
    // `mixed_udos_ss40_over_cpa800.hfe` traegt Spur 25 die IDs 2…26 statt 1…26 —
    // daran fiel die Erkennung der ganzen Diskette durch, obwohl 39 Spuren stimmen.
    const DiskFormat* f = katalog().find("k5601_ss40_26x128");
    ASSERT_NE(f, nullptr);

    std::vector<MeasuredTrack> m;
    for (uint8_t c = 0; c < 40; ++c) {
        MeasuredTrack t;
        t.cyl = c; t.head = 0; t.formatted = true;
        t.sectors = 26; t.sector_size = 128; t.first_id = 1;
        t.ids_dense = true; t.encoding = Encoding::MFM;
        m.push_back(t);
    }
    ASSERT_TRUE(GeometryProbe::match(m, *f).ok);

    m[25].sectors  = 25;                     // Sektor 1 fehlt
    m[25].first_id = 2;
    const GeometryMatch mit_luecke = GeometryProbe::match(m, *f);
    EXPECT_TRUE(mit_luecke.ok) << mit_luecke.reason;
    EXPECT_EQ(mit_luecke.defect_tracks, 1) << "der Schaden wird verschwiegen";

    // Volle Sektorzahl bei verschobenem Anfang bleibt ein ANDERES Format: dort
    // zaehlt die Diskette wirklich anders, es fehlt nichts.
    m[25].sectors  = 26;
    m[25].first_id = 2;
    EXPECT_FALSE(GeometryProbe::match(m, *f).ok);
}
