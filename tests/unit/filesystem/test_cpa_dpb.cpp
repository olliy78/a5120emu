/**
 * @file test_cpa_dpb.cpp
 * @brief GoogleTests für @ref CpaDpbRule — die nachgebildete CP/A-Formaterkennung.
 *
 * Zwei Sorten Prüfung:
 *
 * 1. **Vertrag mit `biosdsk.mac`.**  Die Tabellen `dtrsl0..3` sind abgeschrieben; ein
 *    Zahlendreher fällt sonst erst an einer echten Diskette auf.  Geprüft werden die
 *    Zeilen, die im Alltag vorkommen — und die 192→128-Kürzung, die im Original eine
 *    eigene Sonderbehandlung ist (`selddr`).
 * 2. **Gegenprobe an den committeten Disketten.**  Die Regel muss genau das ausrechnen,
 *    was für `cpa780` / `scpx798` von Hand NACHGEMESSEN und in `data/formats.yaml`
 *    eingetragen wurde.  Weichen beide voneinander ab, ist eines von beiden falsch.
 *
 * @see core/filesystem/cpm/cpa_dpb.h, doc/cpa_format_detection.md
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/filesystem/cpm/cpa_dpb.h"
#include "core/filesystem/fs_catalog.h"
#include "core/filesystem/geometry_probe.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/temp_path.h"

namespace {

std::string fixture(const char* name) {
    return (std::filesystem::path(FIXTURE_DIR) / name).string();
}

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        FormatCatalog k = FormatCatalog::load({K1520_FORMATS_DEFAULT}, &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        FsCatalog k = FsCatalog::load({K1520_FORMATS_DEFAULT}, formate(), &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

/// @brief Diskette öffnen, Geometrie erkennen, Regel anwenden.
struct Erkannt {
    std::unique_ptr<DiskImage>   disk;
    std::unique_ptr<SectorSpace> raum;
    const DiskFormat*            format = nullptr;
    CpaDpb                       dpb;
    std::string                  fehler;

    explicit operator bool() const { return format != nullptr && fehler.empty(); }
};

Erkannt erkenne(const std::string& pfad) {
    Erkannt e;
    e.disk = DiskImage::open(pfad, std::nullopt, /*write_protect=*/true);
    if (!e.disk) { e.fehler = "Abbild nicht ladbar: " + pfad; return e; }

    const auto treffer = GeometryProbe::matchAll(GeometryProbe::measure(e.disk->medium()),
                                                 formate().formats());
    if (treffer.empty()) { e.fehler = "keine Geometrie erkannt"; return e; }

    e.format = treffer.front().format;
    e.raum   = std::make_unique<SectorSpace>(e.disk->medium(), *e.format);
    if (!CpaDpbRule::derive(*e.format, *e.raum, e.dpb, &e.fehler)) e.format = nullptr;
    return e;
}

}  // namespace

// ─── 1. Tabellenwerte ────────────────────────────────────────────────────────

/**
 * @test Die abgeschriebenen `dtrsl`-Zeilen stimmen mit `biosdsk.mac` überein.
 * @par Kriterium  Stichproben aller vier Sektorlängen in der 80-Spur-DS-Zeile plus
 *                 die Eigenheiten: 256 B hat ein FESTES Offset, 512 B kennt auf 5″
 *                 gar kein Verzeichnis, 1024 B/80 DS trägt 192 Plätze.
 */
TEST(CpaDpb, TabellenzeilenEntsprechenDemBios) {
    // 128 B, 80 Tr. DS: 160,26,127,2*0+dslfo,dbl2k0
    const CpaDpbEntry e128 = CpaDpbRule::entry(0, CpaDpb::Ds80);
    EXPECT_EQ(e128.dir_entries, 128);
    EXPECT_EQ(e128.sys_tracks, 0);
    EXPECT_TRUE(e128.fixed_off);
    EXPECT_EQ(e128.block_size, 2048u);

    // 256 B, 80 Tr. DS: 160,32,127,2*4+dslfo,dbl2k0 — festes Offset, nie 0
    const CpaDpbEntry e256 = CpaDpbRule::entry(1, CpaDpb::Ds80);
    EXPECT_EQ(e256.dir_entries, 128);
    EXPECT_EQ(e256.sys_tracks, 4);
    EXPECT_TRUE(e256.fixed_off);

    // 512 B, 5″: dsldir=0 — CP/A legt darauf kein brauchbares Verzeichnis an
    EXPECT_EQ(CpaDpbRule::entry(2, CpaDpb::Ds80).dir_entries, 1);
    EXPECT_EQ(CpaDpbRule::entry(2, CpaDpb::Ss40).dir_entries, 1);
    // …auf 8″ dagegen schon: 77,36,127,2*2+dslvo
    EXPECT_EQ(CpaDpbRule::entry(2, CpaDpb::Fm8).dir_entries, 128);

    // 1024 B, 80 Tr. DS: 160,40,191,2*4+dslvo,dbl2k0 — die CP/A-Standardplatte
    const CpaDpbEntry e1k = CpaDpbRule::entry(3, CpaDpb::Ds80);
    EXPECT_EQ(e1k.dir_entries, 192);
    EXPECT_EQ(e1k.sys_tracks, 4);
    EXPECT_FALSE(e1k.fixed_off);
    EXPECT_EQ(e1k.block_size, 2048u);

    // 1024 B, 40 Tr. SS: 40,40,63,2*2+dslvo,dbl1k — die einzige 1-KB-Blockgröße hier
    EXPECT_EQ(CpaDpbRule::entry(3, CpaDpb::Ss40).block_size, 1024u);
    EXPECT_EQ(CpaDpbRule::entry(3, CpaDpb::Ss40).dir_entries, 64);
}

// ─── 2. Gegenprobe an echten Disketten ───────────────────────────────────────

/**
 * @test Die Regel reproduziert das von Hand nachgemessene Profil `cpa780`.
 * @par Kriterium  4 Systemspuren (= c2h0), 2-KB-Blöcke, **128** Plätze — die 192 aus
 *                 der Tabelle werden gekürzt, weil Systemspuren vorhanden sind.
 * @par Warum      Das ist der Beweis, dass die Regel nicht bloß plausibel rechnet,
 *                 sondern dasselbe wie die Handmessung an `cpa_cpa780_k5601_clock`.
 */
TEST(CpaDpb, ReproduziertNachgemessenesProfilCpa780) {
    Erkannt e = erkenne(fixture("cpa_cpa780_k5601_clock.hfe"));
    ASSERT_TRUE(e) << e.fehler;
    EXPECT_EQ(e.format->name, "cpa780");

    EXPECT_EQ(e.dpb.size_code, 3);
    EXPECT_EQ(e.dpb.row, CpaDpb::Ds80);
    EXPECT_EQ(e.dpb.sys_tracks, 4);
    EXPECT_EQ(e.dpb.data_cyl, 2);
    EXPECT_EQ(e.dpb.data_head, 0);
    EXPECT_EQ(e.dpb.block_size, 2048u);
    EXPECT_EQ(e.dpb.dir_entries, 128);
    EXPECT_EQ(e.dpb.skew, 0);

    const FsProfile* p = dateisysteme().find("cpa780");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(e.dpb.data_cyl,    p->data_cyl);
    EXPECT_EQ(e.dpb.data_head,   p->data_head);
    EXPECT_EQ(e.dpb.block_size,  p->block_size);
    EXPECT_EQ(e.dpb.dir_entries, p->dir_entries);
}

/**
 * @test Dasselbe für die SCPX-Diskette `scpx798` (Systemzylinder in 16×256).
 * @par Kriterium  Die gemischte Geometrie ändert nichts: Sektorlängencode kommt von der
 *                 DATENspur (logische Spur 3 = c1h1, 1024 B), also wieder 4/2048/128.
 */
TEST(CpaDpb, ReproduziertNachgemessenesProfilScpx798) {
    Erkannt e = erkenne(fixture("scpx17_5x1024_k5601_hardy.hfe"));
    ASSERT_TRUE(e) << e.fehler;

    const FsProfile* p = dateisysteme().find("scpx798");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(e.format->name, p->format);
    EXPECT_EQ(e.dpb.data_cyl,    p->data_cyl);
    EXPECT_EQ(e.dpb.data_head,   p->data_head);
    EXPECT_EQ(e.dpb.block_size,  p->block_size);
    EXPECT_EQ(e.dpb.dir_entries, p->dir_entries);
}

/**
 * @test Eine **fabrikfrische** 800K-Diskette bekommt 0 Systemspuren und 192 Plätze.
 * @par Kriterium  Spur 0 ist durchgehend 0xE5 → das BIOS sieht auf der ersten möglichen
 *                 Datenspur nach (`seldo0`), findet auch dort 0xE5 und entscheidet
 *                 „keine Systemspuren".  Ohne Systemspuren entfällt die Kürzung, es
 *                 bleiben die 192 Plätze, die FORMAT.COM im Menü selbst nennt
 *                 (doc/format.md §3.1, Wahl 0).
 */
TEST(CpaDpb, LeereAchthundertkDisketteHatKeineSystemspurenUnd192Plaetze) {
    const std::string pfad =
        k1520test::tempPath("k1520_cpa_dpb_leer.hfe");
    const DiskFormat* fmt = formate().find("cpa800");
    ASSERT_NE(fmt, nullptr);
    ASSERT_NE(DiskImage::create(pfad, *fmt, /*write_protect=*/false), nullptr);

    Erkannt e = erkenne(pfad);
    std::filesystem::remove(pfad);
    ASSERT_TRUE(e) << e.fehler;

    EXPECT_EQ(e.format->name, "cpa800");
    EXPECT_EQ(e.dpb.sys_tracks, 0);
    EXPECT_EQ(e.dpb.data_cyl, 0);
    EXPECT_EQ(e.dpb.dir_entries, 192);
    EXPECT_EQ(e.dpb.block_size, 2048u);
}

/**
 * @test 26×128-Disketten bekommen den Sektorversatz der Tabelle `xlt` (1,7,13,…).
 * @par Kriterium  `skew == 6` — genau diese Schrittweite erzeugt die Reihenfolge des
 *                 BIOS.  Fehlt sie, liest das Werkzeug die Sätze in falscher Folge.
 */
TEST(CpaDpb, HundertachtundzwanzigByteSpurenBekommenVersatzSechs) {
    const std::string pfad =
        k1520test::tempPath("k1520_cpa_dpb_128.hfe");
    const DiskFormat* fmt = formate().find("k5601_26x128");
    if (!fmt) fmt = formate().find("k5601_ss80_26x128");
    ASSERT_NE(fmt, nullptr) << "kein 26×128-Format im Katalog";
    ASSERT_NE(DiskImage::create(pfad, *fmt, /*write_protect=*/false), nullptr);

    Erkannt e = erkenne(pfad);
    std::filesystem::remove(pfad);
    ASSERT_TRUE(e) << e.fehler;

    EXPECT_EQ(e.dpb.size_code, 0);
    EXPECT_EQ(e.dpb.skew, 6);
}

/**
 * @test Eine Sektorlänge, die CP/A nicht kennt, wird abgelehnt statt geraten.
 * @par Kriterium  `derive` liefert false und nennt die Länge.
 */
TEST(CpaDpb, UnbekannteSektorlaengeWirdAbgelehnt) {
    DiskFormat fmt;
    fmt.name = "test_2048";
    fmt.tracks.push_back(TrackFormat{0, 9, 0, 0, 4, 2048, Encoding::MFM, 1});

    auto disk = DiskImage::createBlank(10, 1);
    ASSERT_NE(disk, nullptr);
    SectorSpace raum(disk->medium(), fmt);

    CpaDpb d;
    std::string warum;
    EXPECT_FALSE(CpaDpbRule::derive(fmt, raum, d, &warum));
    EXPECT_NE(warum.find("2048"), std::string::npos) << warum;
}
