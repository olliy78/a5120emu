/**
 * @file test_floppy_drive2.cpp
 * @brief GoogleTests für FloppyDriveV2 (DriveProfile + gemountete DiskImage).
 *
 * Getestete Komponenten:
 *   - FloppyDriveV2::mount (Erfolg, Kompatibilitätsprüfungen)
 *   - FloppyDriveV2::track (Spur laden, nichtleer, Sektoren korrekt)
 *   - FloppyDriveV2::step  (Begrenzung 0 .. num_cyls-1)
 *   - Schreib-Roundtrip via mutableTrack + markTrackDirty + flush + readTrack
 *
 * @see core/peripherals/floppy_drive/floppy_drive2.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdint>

#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "tests/support/temp_path.h"

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/** Einfaches Format: 2 Zylinder, 1 Kopf, 2 Sektoren × 128 B. */
static DiskFormat makeFormat_2cyl_1head_2sec() {
    DiskFormat fmt;
    fmt.name = "test_2c1h2s";
    fmt.tracks.push_back({0, 1, 0, 0, 2, 128});
    return fmt;
}

/** Temporäre .img-Datei; alle Bytes mit Sektor-ID-Füller. */
static std::string makeTmpImg(const DiskFormat& fmt, const std::string& suffix = "") {
    const auto path = k1520test::tempPath(("k1520_drv2_" + fmt.name + suffix + ".img"));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                std::vector<uint8_t> buf(tf->bytes_per_sec, static_cast<uint8_t>(id));
                f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
        }
    }
    return path;
}

/** Öffnet ein rohes Sektorimage über die DiskImage-Fabrik. */
static std::unique_ptr<DiskImage> openImg(const std::string& path,
                                          const DiskFormat& fmt,
                                          bool wp = false) {
    return DiskImage::open(path, fmt, wp);
}

// ─── Gruppe 1: Mount ──────────────────────────────────────────────────────────

TEST(FloppyDriveV2, Mount_Erfolg) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    auto img = openImg(path, fmt);
    ASSERT_NE(img, nullptr);

    EXPECT_TRUE(drv.mount(std::move(img)));
    EXPECT_TRUE(drv.isMounted());
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Mount_KeinImage_Fehler) {
    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    EXPECT_FALSE(drv.mount(nullptr));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");
}

TEST(FloppyDriveV2, Mount_ZuVieleSpuren_Inkompatibel) {
    // Jenseits jeder Uebersetzung: 160 Zylinder erreicht das K5600.10 auch
    // schrittverdoppelt nicht (es kaeme hoechstens bis Spur 78).
    DiskFormat fmt160;
    fmt160.name = "test_160cyl";
    fmt160.tracks.push_back({0, 159, 0, 0, 2, 128});

    auto path = k1520test::tempPath("k1520_drv2_160cyl.img");
    {
        uint64_t sz = fmt160.totalBytes();
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> buf(sz, 0xE5);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    FloppyDriveV2 drv(builtinDriveProfile("K5600.10")); // nur 40 Zylinder
    auto img = openImg(path, fmt160);
    ASSERT_NE(img, nullptr);

    EXPECT_FALSE(drv.mount(std::move(img)));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Mount_FalschesVerfahren_Inkompatibel) {
    // MF3200 unterstützt nur FM; ein MFM-Image passt nicht.
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("MF3200")); // FM only
    // Das Format deklariert MFM (Default) → passt nicht zum FM-Laufwerk.
    auto img = openImg(path, fmt);
    ASSERT_NE(img, nullptr);

    EXPECT_FALSE(drv.mount(std::move(img)));
    EXPECT_FALSE(drv.isMounted());
    EXPECT_NE(std::string(drv.lastError()), "");

    std::filesystem::remove(path);
}

// ─── Gruppe 2: track() liefert korrekte Sektoren ─────────────────────────────

TEST(FloppyDriveV2, Track_NichtLeerNachMount) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    const TrackImage& spur = drv.track(0);
    EXPECT_FALSE(spur.empty()) << "Spur sollte Bytes enthalten";

    // parseTrack muss 2 Sektoren liefern
    auto parsed = TrackCodec::parseTrack(spur);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_TRUE(parsed[0].id_crc_ok);
    EXPECT_TRUE(parsed[0].data_crc_ok);
    EXPECT_TRUE(parsed[1].id_crc_ok);
    EXPECT_TRUE(parsed[1].data_crc_ok);

    // Erkennungsmuster: Sektor id=1 → alle Bytes 0x01
    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [](uint8_t b){ return b == 0x01; }));

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Track_UngueltigerKopf_LeereSpur) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    // Kopf 2 existiert nicht (nur Kopf 0 im Format), track() muss leer sein
    const TrackImage& spur = drv.track(2);
    EXPECT_TRUE(spur.empty());

    std::filesystem::remove(path);
}

// ─── Gruppe 3: step / seek ────────────────────────────────────────────────────

TEST(FloppyDriveV2, Step_InwardIncrement) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.currentCylinder(), 0u);
    drv.step(true);   // inward
    EXPECT_EQ(drv.currentCylinder(), 1u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_InwardBegrenztAufMaxCyl) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601")); // num_cyls=80
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    // Bis ans Ende fahren
    drv.seek(79);
    EXPECT_EQ(drv.currentCylinder(), 79u);

    // Weiterer Schritt inward darf 79 nicht überschreiten
    drv.step(true);
    EXPECT_EQ(drv.currentCylinder(), 79u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_OutwardBegrenztAuf0) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.currentCylinder(), 0u);
    drv.step(false);   // outward, bereits bei 0
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Step_InOutRoundtrip) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    drv.step(true);    // 0 → 1
    drv.step(true);    // 1 → 2
    EXPECT_EQ(drv.currentCylinder(), 2u);
    drv.step(false);   // 2 → 1
    EXPECT_EQ(drv.currentCylinder(), 1u);
    drv.step(false);   // 1 → 0
    EXPECT_EQ(drv.currentCylinder(), 0u);

    std::filesystem::remove(path);
}

// ─── Gruppe 4: Schreib-Roundtrip ─────────────────────────────────────────────

/**
 * Spur modifizieren via mutableTrack + markTrackDirty + flush, dann per
 * neuem RawSectorImage nachlesen — Änderung muss persistieren.
 */
TEST(FloppyDriveV2, SchreibRoundtrip_AenderungPersistiert) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt, "_rw");

    // Neue Spur mit abgeänderten Sektordaten bauen
    const uint8_t geaendert = 0xAB;
    std::vector<LogicalSector> sektoren;
    for (uint8_t id = 1; id <= 2; ++id) {
        LogicalSector ls;
        ls.cyl  = 0;
        ls.head = 0;
        ls.id   = id;
        ls.size = 128;
        ls.data.assign(128, (id == 1) ? geaendert : static_cast<uint8_t>(id));
        sektoren.push_back(std::move(ls));
    }
    TrackImage neueSpur = TrackCodec::buildTrack(sektoren, Encoding::MFM);

    // Laufwerk mounten, Spur ersetzen und zurückschreiben.
    {
        FloppyDriveV2 drv(builtinDriveProfile("K5601"));
        ASSERT_TRUE(drv.mount(openImg(path, fmt)));

        drv.mutableTrack(0) = neueSpur;
        drv.markTrackDirty(0);
        EXPECT_TRUE(drv.flush());
    }

    // Datei neu öffnen und nachlesen.
    auto check = openImg(path, fmt);
    ASSERT_NE(check, nullptr);

    const TrackImage& gelesen = check->readTrack(0, 0);
    ASSERT_FALSE(gelesen.empty());

    auto parsed = TrackCodec::parseTrack(gelesen);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [geaendert](uint8_t b){ return b == geaendert; }))
        << "Sektor 1 sollte alle Bytes " << (int)geaendert << " enthalten";

    std::filesystem::remove(path);
}

// ─── Gruppe 5: geometry() ────────────────────────────────────────────────────

TEST(FloppyDriveV2, Geometry_NachMount) {
    auto fmt  = makeFormat_2cyl_1head_2sec();
    auto path = makeTmpImg(fmt);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    DiskGeometry g = drv.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 1u);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Geometry_OhneMount_Leer) {
    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    DiskGeometry g = drv.geometry();
    EXPECT_EQ(g.num_cyls,  0u);
    EXPECT_EQ(g.num_heads, 0u);
}

// ─── Gruppe 6: Leerdiskette ──────────────────────────────────────────────────

/**
 * @test FloppyDriveV2/Mount_LeerdisketteWirdAkzeptiert
 * @brief Eine unformatierte Diskette muss sich in JEDES Laufwerk mounten lassen —
 *        sonst könnte das Gastsystem sie gar nicht erst formatieren.  Ihr
 *        Vorschlagsverfahren (hier MFM) darf dabei nicht gegen ein FM-Laufwerk
 *        geprüft werden, weil noch keine einzige Spur beschrieben ist.
 */
TEST(FloppyDriveV2, Mount_LeerdisketteWirdAkzeptiert) {
    FloppyDriveV2 drv(builtinDriveProfile("MF3200"));   // 77 Spuren, nur FM
    auto img = DiskImage::createBlank(77, 1, Encoding::MFM);
    ASSERT_NE(img, nullptr);

    EXPECT_TRUE(drv.mount(std::move(img))) << drv.lastError();
    EXPECT_TRUE(drv.isMounted());
    EXPECT_TRUE(drv.track(0).empty()) << "unformatierte Spur ist leer";
}

/**
 * @test FloppyDriveV2/WriteTrackAt_SchreibtInsMedium
 * @brief Der Vollspur-FORMAT-Pfad adressiert eine EXPLIZITE (cyl, head)-Position,
 *        weil der Kopf beim Commit schon weitergeschritten ist.
 */
TEST(FloppyDriveV2, WriteTrackAt_SchreibtInsMedium) {
    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(DiskImage::createBlank(80, 2, Encoding::MFM)));

    std::vector<LogicalSector> sektoren;
    for (uint8_t id = 1; id <= 2; ++id) {
        LogicalSector ls;
        ls.cyl = 5; ls.head = 1; ls.id = id; ls.size = 128;
        ls.data.assign(128, 0x7E);
        sektoren.push_back(std::move(ls));
    }
    const TrackImage spur = TrackCodec::buildTrack(sektoren, Encoding::MFM);

    EXPECT_TRUE(drv.writeTrackAt(5, 1, spur));
    ASSERT_NE(drv.image(), nullptr);
    auto parsed = TrackCodec::parseTrack(drv.image()->readTrack(5, 1));
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].data[0], 0x7E);

    // Schreibschutz sperrt den Pfad.
    drv.setWriteProtect(true);
    EXPECT_FALSE(drv.writeTrackAt(6, 0, spur));
}

// ─── Gruppe 6: Spurdichte — Diskette und Laufwerk passen nicht zusammen ──────
//
// 5,25″ kennt zwei Spurdichten: 48 tpi (40 Spuren) und 96 tpi (80).  Passt die
// Diskette nicht zum Laufwerk, wird sie nicht abgewiesen, sondern uebersetzt —
// und der Bediener bekommt einen Hinweis.  @see doc/design/09_floppy_drive.md §6.2

/** Rohes Sektorimage, in dem JEDER Sektor mit seiner Zylindernummer gefuellt ist. */
static std::string makeTmpImgCylMarked(const DiskFormat& fmt, const std::string& suffix) {
    const auto path = k1520test::tempPath("k1520_drv2_" + fmt.name + suffix + ".img");
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    for (uint8_t c = 0; c < fmt.numCylinders(); ++c) {
        for (uint8_t h = 0; h < fmt.numHeads(); ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                std::vector<uint8_t> buf(tf->bytes_per_sec, c);
                f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
        }
    }
    return path;
}

static DiskFormat makeFormat(const std::string& name, uint8_t cyls, uint8_t heads) {
    DiskFormat fmt;
    fmt.name = name;
    fmt.tracks.push_back({0, static_cast<uint8_t>(cyls - 1), 0,
                          static_cast<uint8_t>(heads - 1), 2, 128});
    return fmt;
}

/** Zylindernummer, die unter der aktuellen Kopfposition liegt (-1 = keine Spur). */
static int gelesenerZylinder(FloppyDriveV2& drv, uint8_t head = 0) {
    const TrackImage& spur = drv.track(head);
    if (spur.empty()) return -1;
    auto parsed = TrackCodec::parseTrack(spur);
    if (parsed.empty() || parsed[0].data.empty()) return -1;
    return parsed[0].data[0];
}

TEST(FloppyDriveV2, Doppelschritt_VierzigSpurDisketteImAchtzigSpurLaufwerk) {
    auto fmt  = makeFormat("test_40cyl", 40, 1);
    auto path = makeTmpImgCylMarked(fmt, "_ds");

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));   // 80 Zylinder, 96 tpi
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.pitch(), TrackPitch::DoubleStep);
    ASSERT_EQ(drv.notices().size(), 1u);
    EXPECT_EQ(drv.notices()[0], "Double Step aktiviert");

    // Spur n liegt auf Kopfposition 2n; dazwischen ist nichts.
    EXPECT_EQ(drv.mediumCylinder(0),  0);
    EXPECT_EQ(drv.mediumCylinder(1), -1);
    EXPECT_EQ(drv.mediumCylinder(78), 39);

    drv.seek(0);   EXPECT_EQ(gelesenerZylinder(drv),  0);
    drv.seek(2);   EXPECT_EQ(gelesenerZylinder(drv),  1);
    drv.seek(3);   EXPECT_EQ(gelesenerZylinder(drv), -1) << "zwischen zwei Spuren";
    drv.seek(78);  EXPECT_EQ(gelesenerZylinder(drv), 39);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Doppelschritt_AchtzigSpurDisketteBleibtUnveraendert) {
    auto fmt  = makeFormat("test_80cyl_direkt", 80, 1);
    auto path = makeTmpImgCylMarked(fmt, "_direkt");

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    EXPECT_EQ(drv.pitch(), TrackPitch::Direct);
    EXPECT_TRUE(drv.notices().empty());
    drv.seek(37);  EXPECT_EQ(gelesenerZylinder(drv), 37);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Halbschritt_AchtzigSpurDisketteImVierzigSpurLaufwerk) {
    auto fmt  = makeFormat("test_80cyl", 80, 1);
    auto path = makeTmpImgCylMarked(fmt, "_hs");

    FloppyDriveV2 drv(builtinDriveProfile("K5600.10"));  // 40 Zylinder, 48 tpi
    ASSERT_TRUE(drv.mount(openImg(path, fmt)))
        << "frueher abgewiesen; jetzt liest das Laufwerk jede zweite Spur: "
        << drv.lastError();

    EXPECT_EQ(drv.pitch(), TrackPitch::HalfStep);
    ASSERT_EQ(drv.notices().size(), 1u);
    EXPECT_EQ(drv.notices()[0], "Laufwerk liest nur jede zweite Spur");

    // Kopfposition n trifft Diskettenspur 2n — bei einer einseitigen
    // Doppelschritt-Diskette ist genau das die gewoehnliche 40-Spur-Diskette.
    drv.seek(0);   EXPECT_EQ(gelesenerZylinder(drv),  0);
    drv.seek(1);   EXPECT_EQ(gelesenerZylinder(drv),  2);
    drv.seek(39);  EXPECT_EQ(gelesenerZylinder(drv), 78);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, NurSeiteNull_ZweiseitigeDisketteImEinseitigenLaufwerk) {
    auto fmt  = makeFormat("test_80cyl_2k", 80, 2);
    auto path = makeTmpImgCylMarked(fmt, "_ss");

    FloppyDriveV2 drv(builtinDriveProfile("K5600.20"));  // 80 Zylinder, EIN Kopf
    ASSERT_TRUE(drv.mount(openImg(path, fmt)))
        << "frueher abgewiesen; jetzt ist Seite 0 benutzbar: " << drv.lastError();

    EXPECT_EQ(drv.pitch(), TrackPitch::Direct);
    EXPECT_TRUE(drv.side0Only());
    ASSERT_EQ(drv.notices().size(), 1u);
    EXPECT_EQ(drv.notices()[0], "Nur Seite 0 verwendbar");

    drv.seek(5);
    EXPECT_EQ(gelesenerZylinder(drv, 0), 5);
    EXPECT_TRUE(drv.track(1).empty()) << "diesen Kopf gibt es an dem Laufwerk nicht";

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, ZweiHinweiseGleichzeitig) {
    // Zweiseitige 80-Spur-Diskette im einseitigen 40-Spur-Laufwerk.
    auto fmt  = makeFormat("test_80cyl_2k_b", 80, 2);
    auto path = makeTmpImgCylMarked(fmt, "_beides");

    FloppyDriveV2 drv(builtinDriveProfile("K5600.10"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));

    ASSERT_EQ(drv.notices().size(), 2u);
    EXPECT_EQ(drv.notices()[0], "Laufwerk liest nur jede zweite Spur");
    EXPECT_EQ(drv.notices()[1], "Nur Seite 0 verwendbar");
    EXPECT_EQ(drv.noticeText(),
              "Laufwerk liest nur jede zweite Spur\nNur Seite 0 verwendbar");

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Doppelschritt_SchreibenZwischenDenSpurenWirdVerworfen) {
    auto fmt  = makeFormat("test_40cyl_w", 40, 1);
    auto path = makeTmpImgCylMarked(fmt, "_wverworfen");

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));
    ASSERT_EQ(drv.pitch(), TrackPitch::DoubleStep);

    std::vector<LogicalSector> sektoren;
    for (uint8_t id = 1; id <= 2; ++id) {
        LogicalSector ls;
        ls.cyl = 1; ls.head = 0; ls.id = id; ls.size = 128;
        ls.data.assign(128, 0x5A);
        sektoren.push_back(std::move(ls));
    }
    const TrackImage spur = TrackCodec::buildTrack(sektoren, Encoding::MFM);

    // Ungerade Kopfposition: dort liegt keine Spur → der Schreibstrom laeuft ins Leere.
    drv.seek(3);
    drv.mutableTrack(0) = spur;
    drv.markTrackDirty(0);
    EXPECT_FALSE(drv.writeTrackAt(3, 0, spur));

    // Keine Diskettenspur darf sich veraendert haben.
    ASSERT_NE(drv.image(), nullptr);
    for (uint8_t c = 0; c < 40; ++c) {
        auto parsed = TrackCodec::parseTrack(drv.image()->readTrack(c, 0));
        ASSERT_FALSE(parsed.empty());
        EXPECT_EQ(parsed[0].data[0], c) << "Spur " << (int)c << " wurde beschrieben";
    }

    // Gerade Position dagegen schreibt auf die getroffene Spur (Position 4 → Spur 2).
    EXPECT_TRUE(drv.writeTrackAt(4, 0, spur));
    auto nachher = TrackCodec::parseTrack(drv.image()->readTrack(2, 0));
    ASSERT_FALSE(nachher.empty());
    EXPECT_EQ(nachher[0].data[0], 0x5A);

    std::filesystem::remove(path);
}

TEST(FloppyDriveV2, Anpassung_WirdBeimUnmountZurueckgesetzt) {
    auto fmt  = makeFormat("test_40cyl_u", 40, 1);
    auto path = makeTmpImgCylMarked(fmt, "_unmount");

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(openImg(path, fmt)));
    ASSERT_FALSE(drv.notices().empty());

    drv.unmount();
    EXPECT_EQ(drv.pitch(), TrackPitch::Direct);
    EXPECT_FALSE(drv.side0Only());
    EXPECT_TRUE(drv.notices().empty());
    EXPECT_EQ(drv.noticeText(), "");

    std::filesystem::remove(path);
}

/**
 * @brief Der Gast darf die beiden Darstellungen derselben Diskette nicht unterscheiden.
 *
 * Ein und dieselbe physische Diskette — 40 Spuren auf 96-tpi-Radius — laesst sich auf
 * zwei Arten abbilden: als **80-Zylinder-Abbild**, in dem nur die geraden Zylinder
 * formatiert sind (so entsteht sie, wenn der Gast in diesem Laufwerk doppelschrittig
 * formatiert, `step: 2` im Katalog), oder als **40-Zylinder-Abbild**, wie es von einem
 * echten 48-tpi-Datentraeger kommt.  Unter dem Lesekopf muessen beide Bit fuer Bit
 * dasselbe liefern — sonst ist die Uebersetzung falsch.
 */
TEST(FloppyDriveV2, Doppelschritt_IstDieselbeDisketteWieEinDoppelschrittAbbild) {
    auto spurFuer = [](uint8_t nr) {
        std::vector<LogicalSector> sektoren;
        for (uint8_t id = 1; id <= 2; ++id) {
            LogicalSector ls;
            ls.cyl = nr; ls.head = 0; ls.id = id; ls.size = 128;
            ls.data.assign(128, nr);
            sektoren.push_back(std::move(ls));
        }
        return TrackCodec::buildTrack(sektoren, Encoding::MFM);
    };

    // (a) Doppelschritt-Abbild: 80 Zylinder, nur die geraden tragen Spuren.
    FloppyDriveV2 doppel(builtinDriveProfile("K5601"));
    ASSERT_TRUE(doppel.mount(DiskImage::createBlank(80, 1, Encoding::MFM)));
    ASSERT_EQ(doppel.pitch(), TrackPitch::Direct);
    for (uint8_t n = 0; n < 40; ++n)
        ASSERT_TRUE(doppel.writeTrackAt(static_cast<uint8_t>(n * 2), 0, spurFuer(n)));

    // (b) Dieselbe Diskette als 40-Zylinder-Abbild.
    FloppyDriveV2 kompakt(builtinDriveProfile("K5601"));
    ASSERT_TRUE(kompakt.mount(DiskImage::createBlank(40, 1, Encoding::MFM)));
    ASSERT_EQ(kompakt.pitch(), TrackPitch::DoubleStep);
    for (uint8_t n = 0; n < 40; ++n)
        ASSERT_TRUE(kompakt.writeTrackAt(static_cast<uint8_t>(n * 2), 0, spurFuer(n)));

    // Jede Kopfposition muss dasselbe liefern — auch die leeren dazwischen.
    for (uint8_t pos = 0; pos < 80; ++pos) {
        doppel.seek(pos);
        kompakt.seek(pos);
        EXPECT_EQ(doppel.track(0).bytes, kompakt.track(0).bytes)
            << "Kopfposition " << (int)pos;
    }
    // Gegenprobe, damit der Vergleich nicht zwei leere Disketten beglaubigt.
    doppel.seek(0);  EXPECT_FALSE(doppel.track(0).empty());
    kompakt.seek(0); EXPECT_FALSE(kompakt.track(0).empty());
    kompakt.seek(1); EXPECT_TRUE(kompakt.track(0).empty());
}
