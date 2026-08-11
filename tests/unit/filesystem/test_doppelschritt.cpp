/**
 * @file test_doppelschritt.cpp
 * @brief GoogleTests für `DiskFormat::step` — die Doppelschritt-Austauschformate.
 *
 * Ein 96-tpi-Laufwerk (K5601, K5600.20) beschreibt für den Austausch mit einem
 * 48-tpi-K5600.10 nur **jeden zweiten** Zylinder.  Im Abbild sieht das aus wie eine
 * halb kaputte 80-Spur-Diskette — deshalb ist die Erkennung die eigentliche Arbeit,
 * und deshalb prüft dieser Test sie **in beide Richtungen**:
 *
 *  - eine Doppelschritt-Diskette darf **nur** vom `step: 2`-Format erkannt werden,
 *  - eine gewöhnliche 40-Spur-Diskette darf **nie** dafür gehalten werden.
 *
 * Dazu die Abbildung selbst: logische Spur `n` liegt physisch auf `2n`, trägt im
 * **ID-Feld** aber die logische Nummer `n` — nachgemessen an den von CP/A erzeugten
 * Abbildern (`out/formats/cpa/fmt_clock_B_U_4.hfe`: physisch c4h0 meldet `cyl=2`).
 *
 * @see doc/feature_requests/doppelschritt_disketten.md, doc/format.md §3.4
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/geometry_probe.h"
#include "core/filesystem/sector_space.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/peripherals/floppy_drive/track_codec.h"

namespace fs = std::filesystem;

namespace {

const FormatCatalog& katalog() {
    static FormatCatalog c = [] {
        std::string fatal;
        FormatCatalog k = FormatCatalog::load({K1520_FORMATS_DEFAULT}, &fatal);
        EXPECT_TRUE(fatal.empty()) << fatal;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string fatal;
        FsCatalog k = FsCatalog::load({K1520_FORMATS_DEFAULT}, katalog(), &fatal);
        EXPECT_TRUE(fatal.empty()) << fatal;
        return k;
    }();
    return c;
}

/// @brief Temporäre Datei, räumt sich weg.
class TempPfad {
public:
    explicit TempPfad(const char* name)
        : pfad_((fs::temp_directory_path() / name).string()) {}
    ~TempPfad() { std::error_code ec; fs::remove(pfad_, ec); }
    const std::string& get() const { return pfad_; }
private:
    std::string pfad_;
};

/// @brief Alle Formatnamen, die zu einer Messung passen.
std::vector<std::string> treffer(const DiskMedium& m) {
    std::vector<std::string> namen;
    for (const auto& t : GeometryProbe::matchAll(GeometryProbe::measure(m),
                                                 katalog().formats()))
        namen.push_back(t.format->name);
    return namen;
}

bool enthaelt(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ─── Das Datenmodell ─────────────────────────────────────────────────────────

/**
 * @test `step` rechnet logische Spuren in physische Zylinder um — und zurück.
 * @par Kriterium  40 logische Spuren mit `step: 2` belegen 79 Zylinder (nicht 80:
 *                 hinter der letzten Spur kommt keine Lücke mehr), und ein ungerader
 *                 Zylinder hat keine logische Entsprechung.
 */
TEST(Doppelschritt, RechnetLogischeSpurenInPhysischeZylinderUm) {
    const DiskFormat* f = katalog().find("k5601_ss40_16x256_dstep");
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(f->step, 2);
    EXPECT_EQ(f->numCylinders(), 40)      << "tracks: bleibt logisch";
    EXPECT_EQ(f->physicalCylinders(), 79) << "(40-1)*2+1";
    EXPECT_EQ(f->physicalCylinder(0), 0);
    EXPECT_EQ(f->physicalCylinder(2), 4);
    EXPECT_EQ(f->physicalCylinder(39), 78);
    EXPECT_EQ(f->logicalCylinder(4), 2);
    EXPECT_EQ(f->logicalCylinder(5), -1)  << "auf ungeraden Zylindern liegt nichts";

    const DiskFormat* einzel = katalog().find("k5601_ss40_16x256");
    ASSERT_NE(einzel, nullptr);
    EXPECT_EQ(einzel->step, 1);
    EXPECT_EQ(einzel->physicalCylinders(), 40);
    EXPECT_EQ(einzel->logicalCylinder(5), 5);
}

// ─── Schreiben: create lässt die ungeraden Zylinder frei ─────────────────────

/**
 * @test Eine neu angelegte Doppelschritt-Diskette lässt jeden zweiten Zylinder leer
 *       und trägt im ID-Feld die LOGISCHE Spurnummer.
 * @par Kriterium  Physisch c4h0 ist beschrieben und meldet `cyl=2`; c5h0 ist leer.
 * @par Warum      Genau so schreibt es CP/A.  Stünde dort der physische Zylinder,
 *                 verwürfe der Treiber des Gastsystems jeden Sektor als „falsche Spur".
 */
TEST(Doppelschritt, NeueDisketteLaesstJedenZweitenZylinderLeer) {
    TempPfad pfad("k1520_dstep_create.hfe");
    const DiskFormat* f = katalog().find("k5601_ss40_16x256_dstep");
    ASSERT_NE(f, nullptr);

    auto disk = DiskImage::create(pfad.get(), *f, /*write_protect=*/false);
    ASSERT_NE(disk, nullptr);
    const DiskMedium& m = disk->medium();

    EXPECT_EQ(m.numCylinders(), 79);
    for (uint8_t c = 0; c < 79; ++c) {
        const auto sek = TrackCodec::parseTrack(m.track(c, 0));
        if (c % 2 == 0) {
            ASSERT_EQ(sek.size(), 16u) << "physischer Zylinder " << int(c);
            EXPECT_EQ(sek.front().cyl, c / 2)
                << "im ID-Feld steht die LOGISCHE Spur, nicht der Zylinder";
        } else {
            EXPECT_TRUE(sek.empty()) << "physischer Zylinder " << int(c)
                                     << " muss unformatiert bleiben";
        }
    }
}

// ─── Erkennung, in beide Richtungen ──────────────────────────────────────────

/**
 * @test Eine Doppelschritt-Diskette wird **nur** vom Doppelschritt-Format erkannt.
 * @par Kriterium  Das `_dstep`-Format ist dabei, das gleichnamige Einzelschritt-Format
 *                 nicht — und auch kein 80-Spur-Format.
 */
TEST(Doppelschritt, WirdNurVomDoppelschrittFormatErkannt) {
    TempPfad pfad("k1520_dstep_erkennung.hfe");
    const DiskFormat* f = katalog().find("k5601_ss40_16x256_dstep");
    ASSERT_NE(f, nullptr);
    auto disk = DiskImage::create(pfad.get(), *f, /*write_protect=*/false);
    ASSERT_NE(disk, nullptr);

    const std::vector<std::string> t = treffer(disk->medium());
    ASSERT_FALSE(t.empty());
    EXPECT_EQ(t.front(), "k5601_ss40_16x256_dstep");
    EXPECT_FALSE(enthaelt(t, "k5601_ss40_16x256"))
        << "das Einzelschritt-Format darf nicht mitpassen";
    EXPECT_FALSE(enthaelt(t, "k5601_ss80_16x256"))
        << "ein 80-Spur-Format darf die Luecken nicht als Leerspuren durchgehen lassen";
}

/**
 * @test Eine gewöhnliche 40-Spur-Diskette wird **nicht** für Doppelschritt gehalten.
 * @par Kriterium  Das `_dstep`-Format ist NICHT unter den Treffern — sie hat keine
 *                 Lücken, und das Doppelschritt-Kriterium verlangt sie positiv.
 * @par Warum      Das ist die Verwechslungsgefahr aus §7 des Feature-Requests: ohne
 *                 diese Richtung läse das Werkzeug jede zweite Spur und lieferte Müll.
 */
TEST(Doppelschritt, EinzelschrittDisketteWirdNichtVerwechselt) {
    TempPfad pfad("k1520_dstep_einzel.hfe");
    const DiskFormat* f = katalog().find("k5601_ss40_16x256");
    ASSERT_NE(f, nullptr);
    auto disk = DiskImage::create(pfad.get(), *f, /*write_protect=*/false);
    ASSERT_NE(disk, nullptr);

    const std::vector<std::string> t = treffer(disk->medium());
    ASSERT_FALSE(t.empty());
    EXPECT_FALSE(enthaelt(t, "k5601_ss40_16x256_dstep"));
}

// ─── Adressierung: SectorSpace und .img rechnen dieselbe Abbildung ───────────

/**
 * @test Der @ref SectorSpace spricht logisch und schreibt physisch.
 * @par Kriterium  Ein über `writeSector(2, 0, 1, …)` geschriebener Sektor steht
 *                 anschliessend auf dem **physischen** Zylinder 4 im Medium.
 * @par Warum      Das ist die eine Stelle, an der die Dateisysteme Spuren adressieren;
 *                 stimmt sie, brauchen CP/M und UDOS keine Änderung.
 */
TEST(Doppelschritt, SectorSpaceSchreibtAufDenPhysischenZylinder) {
    TempPfad pfad("k1520_dstep_space.hfe");
    const DiskFormat* f = katalog().find("k5601_ss40_16x256_dstep");
    ASSERT_NE(f, nullptr);
    auto disk = DiskImage::create(pfad.get(), *f, /*write_protect=*/false);
    ASSERT_NE(disk, nullptr);

    SectorSpace raum(disk->medium(), *f);
    ASSERT_EQ(raum.trackCount(), 40u) << "der Raum ist logisch";
    EXPECT_EQ(raum.trackAt(2).cyl, 2);

    std::vector<uint8_t> muster(256, 0x5A);
    muster[0] = 0xC3;
    ASSERT_TRUE(raum.writeSector(2, 0, 1, muster)) << raum.lastError();

    const auto physisch = TrackCodec::parseTrack(disk->medium().track(4, 0));
    ASSERT_FALSE(physisch.empty());
    EXPECT_EQ(physisch.front().data[0], 0xC3)
        << "logische Spur 2 muss auf physischem Zylinder 4 gelandet sein";
    EXPECT_TRUE(TrackCodec::parseTrack(disk->medium().track(2, 0)).front().data[0] != 0xC3);
}

/**
 * @test Rundlauf über `.img`: das rohe Sektorabbild ist logisch (40 Spuren).
 * @par Kriterium  Die Datei ist genau `40·16·256` gross, und nach dem Zurücklesen
 *                 liegen die Daten wieder auf den geraden Zylindern.
 */
TEST(Doppelschritt, ImgRundlaufBehaeltDieAbbildung) {
    TempPfad quelle("k1520_dstep_q.hfe");
    TempPfad roh("k1520_dstep.img");
    const DiskFormat* f = katalog().find("k5601_ss40_16x256_dstep");
    ASSERT_NE(f, nullptr);

    {
        auto disk = DiskImage::create(quelle.get(), *f, /*write_protect=*/false);
        ASSERT_NE(disk, nullptr);
        SectorSpace raum(disk->medium(), *f);
        std::vector<uint8_t> muster(256, 0x11);
        muster[0] = 0x42;
        ASSERT_TRUE(raum.writeSector(7, 0, 3, muster)) << raum.lastError();
        ASSERT_TRUE(disk->saveAs(roh.get(), *f)) << disk->lastError();
    }

    EXPECT_EQ(fs::file_size(roh.get()), 40u * 16u * 256u);

    auto zurueck = DiskImage::open(roh.get(), *f, /*write_protect=*/true);
    ASSERT_NE(zurueck, nullptr);
    EXPECT_EQ(zurueck->medium().numCylinders(), 79);
    const auto sek = TrackCodec::parseTrack(zurueck->medium().track(14, 0));
    ASSERT_EQ(sek.size(), 16u);
    EXPECT_EQ(sek.front().cyl, 7);
    ASSERT_GE(sek.size(), 3u);
    EXPECT_EQ(sek[2].data[0], 0x42) << "Sektor 3 der logischen Spur 7";
}

// ─── Bis nach oben durch: mounten und beschreiben ────────────────────────────

/**
 * @test Eine Doppelschritt-Diskette lässt sich mounten und beschreiben.
 * @par Kriterium  @ref DiskVolume erkennt Format und (per CP/A-Regel) Dateisystem;
 *                 eine geschriebene Datei liest sich unverändert zurück.
 */
TEST(Doppelschritt, LaesstSichMountenUndBeschreiben) {
    TempPfad pfad("k1520_dstep_volume.hfe");
    TempPfad quelldatei("k1520_dstep_quelle.txt");
    const DiskFormat* f = katalog().find("k5601_ds40_5x1024_dstep");
    ASSERT_NE(f, nullptr);
    ASSERT_NE(DiskImage::create(pfad.get(), *f, /*write_protect=*/false), nullptr);

    { std::ofstream(quelldatei.get(), std::ios::binary) << "DOPPELSCHRITT"; }

    std::string err;
    auto dv = DiskVolume::open(pfad.get(), "", katalog(), dateisysteme(), err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_EQ(dv->detection().format, "k5601_ds40_5x1024_dstep");
    EXPECT_EQ(dv->detection().filesystem, "cpa_auto");
    EXPECT_TRUE(dv->list().empty());

    dv->setReadOnly(false);
    ASSERT_TRUE(dv->insert(quelldatei.get(), FileRef::parse("PROBE.TXT"), {}))
        << dv->lastError();
    ASSERT_EQ(dv->list().size(), 1u);
    EXPECT_EQ(dv->list().front().name, "PROBE.TXT");
    ASSERT_TRUE(dv->flush()) << dv->lastError();

    // Frisch von der Datei lesen — die Abbildung muss auch nach dem Umweg stimmen.
    auto wieder = DiskVolume::open(pfad.get(), "", katalog(), dateisysteme(), err);
    ASSERT_NE(wieder, nullptr) << err;
    ASSERT_EQ(wieder->list().size(), 1u);
    EXPECT_EQ(wieder->list().front().name, "PROBE.TXT");
}
