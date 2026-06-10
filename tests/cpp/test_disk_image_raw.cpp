/**
 * @file test_disk_image_raw.cpp
 * @brief GoogleTests für RawSectorImage und DiskImage::open.
 *
 * Getestete Komponenten:
 *   - RawSectorImage: readTrack → TrackCodec::parseTrack (Bitgleichheit mit alter FloppyDrive::readSector)
 *   - RawSectorImage: geometry()
 *   - DiskImage::open (HFE-Signatur → nullptr; Raw → RawSectorImage)
 *
 * Der Bitgleichheits-Vergleich beweist, dass readTrack + parseTrack dieselben
 * Nutzdaten liefert wie die alte FloppyDrive::readSector auf derselben Datei.
 *
 * @see core/peripherals/floppy_drive/raw_sector_image.h
 * @see core/peripherals/floppy_drive/disk_image.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>

#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/floppy_drive.h"
#include "core/peripherals/floppy_drive/format_parser.h"

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/**
 * Einfaches DiskFormat: 2 Zylinder, 1 Kopf, 2 Sektoren × 128 B.
 */
static DiskFormat makeSimpleFormat() {
    DiskFormat fmt;
    fmt.name = "test_simple";
    fmt.tracks.push_back({0, 1, 0, 0, 2, 128});
    return fmt;
}

/**
 * Temporäre .img-Datei anlegen.  Jeder Sektor wird mit seinem sektor-id-Wert
 * in allen Bytes gefüllt (Erkennungsmuster für Roundtrip-Tests).
 */
static std::string makeTmpImg(const DiskFormat& fmt) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_raw_sector.img").string();
    const uint64_t sz = fmt.totalBytes();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);

    // Sektoren zeilenweise mit ihrem 1-basierten ID-Wert füllen.
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                // Erkennungsmuster: alle Bytes == Sektor-ID
                std::vector<uint8_t> buf(tf->bytes_per_sec,
                                         static_cast<uint8_t>(id));
                f.write(reinterpret_cast<const char*>(buf.data()),
                        buf.size());
            }
        }
    }
    (void)sz;
    return path;
}

// ─── Gruppe 1: geometry() ────────────────────────────────────────────────────

TEST(RawSectorImage, Geometry_KorrekteFelderSimpleFormat) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    RawSectorImage img(path, fmt, /*wp=*/false);
    ASSERT_TRUE(img.isOpen());

    DiskGeometry g = img.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 1u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);
    EXPECT_TRUE(g.uniform) << "Uniform sollte true sein (ein TrackFormat-Eintrag)";

    std::filesystem::remove(path);
}

// ─── Gruppe 2: readTrack → parseTrack bitgleich zu FloppyDrive::readSector ───

/**
 * Kern-Test: RawSectorImage::readTrack(cyl, head) → TrackCodec::parseTrack liefert
 * für jede (cyl, head, id)-Kombination exakt dieselben Nutzdaten wie
 * FloppyDrive::readSector auf derselben Datei.
 */
TEST(RawSectorImage, ReadTrack_BitgleichMitAlterFloppyDrive) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    // Neue Implementierung
    RawSectorImage newImg(path, fmt, /*wp=*/false);
    ASSERT_TRUE(newImg.isOpen());

    // Alte Implementierung
    FloppyDrive oldDrv;
    ASSERT_TRUE(oldDrv.mount(path, fmt));

    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();

    for (uint8_t cyl = 0; cyl < ncyls; ++cyl) {
        for (uint8_t head = 0; head < nheads; ++head) {
            const TrackFormat* tf = fmt.findTrack(cyl, head);
            ASSERT_NE(tf, nullptr);

            // Neues Backend: komplette Spur → parsen → Sektoren
            TrackImage track = newImg.readTrack(cyl, head);
            ASSERT_FALSE(track.empty())
                << "readTrack(" << (int)cyl << "," << (int)head << ") leer";

            auto parsed = TrackCodec::parseTrack(track);
            ASSERT_EQ(parsed.size(), static_cast<size_t>(tf->secs_per_track))
                << "Sektoranzahl weicht ab bei cyl=" << (int)cyl
                << " head=" << (int)head;

            for (const auto& ls : parsed) {
                EXPECT_TRUE(ls.id_crc_ok)
                    << "ID-CRC fehler bei cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)ls.id;
                EXPECT_TRUE(ls.data_crc_ok)
                    << "Daten-CRC fehler bei cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)ls.id;

                // Vergleich mit alter FloppyDrive::readSector
                auto altDaten = oldDrv.readSector(cyl, head, ls.id);
                ASSERT_EQ(altDaten.size(), ls.data.size())
                    << "Datenmenge weicht ab bei cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)ls.id;
                EXPECT_EQ(ls.data, altDaten)
                    << "Nutzdaten weichen ab bei cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)ls.id;
            }
        }
    }

    std::filesystem::remove(path);
}

// ─── Gruppe 3: Erkennungsmuster-Validierung ───────────────────────────────────

/**
 * Jeder Sektor-ID-Wert muss im parseTrack-Ergebnis als data[0] erscheinen
 * (wegen des Erkennungsmusters aus makeTmpImg).
 */
TEST(RawSectorImage, ReadTrack_ErkennungsmusterErhalten) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    RawSectorImage img(path, fmt, /*wp=*/false);
    ASSERT_TRUE(img.isOpen());

    // Spur (0, 0): Sektoren id=1 (alle 0x01) und id=2 (alle 0x02)
    TrackImage track = img.readTrack(0, 0);
    ASSERT_FALSE(track.empty());

    auto parsed = TrackCodec::parseTrack(track);
    ASSERT_EQ(parsed.size(), 2u);

    // Sektoren sind in Spurreihenfolge id=1, id=2
    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [](uint8_t b){ return b == 0x01; }))
        << "Sektor 1: nicht alle Bytes == 0x01";

    EXPECT_EQ(parsed[1].id, 2u);
    EXPECT_TRUE(std::all_of(parsed[1].data.begin(), parsed[1].data.end(),
                            [](uint8_t b){ return b == 0x02; }))
        << "Sektor 2: nicht alle Bytes == 0x02";

    std::filesystem::remove(path);
}

// ─── Gruppe 4: DiskImage::open ────────────────────────────────────────────────

TEST(DiskImageOpen, HFE_Signatur_gibtNullptr) {
    // Datei mit HFE-Signatur anlegen
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hxcpicfe.img").string();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        const char sig[] = "HXCPICFE";
        f.write(sig, 8);
        // Rest mit 0-Bytes füllen
        uint8_t pad[64] = {};
        f.write(reinterpret_cast<char*>(pad), sizeof(pad));
    }
    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr) << "HFE-Signatur sollte nullptr liefern";
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, HXCHFEV3_Signatur_gibtNullptr) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hxchfev3.img").string();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        const char sig[] = "HXCHFEV3";
        f.write(sig, 8);
        uint8_t pad[64] = {};
        f.write(reinterpret_cast<char*>(pad), sizeof(pad));
    }
    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, RawOhneFmt_gibtNullptr) {
    // Keine HFE-Signatur, aber auch kein Format → nullptr
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    auto img  = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, RawMitFmt_liefertOefenesImage) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    auto img  = DiskImage::open(path, fmt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(img->writable());
    auto g = img->geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 1u);
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, NichtExistenteDatei_gibtNullptr) {
    auto fmt = makeSimpleFormat();
    auto img = DiskImage::open("/nicht/vorhanden.img", fmt, false);
    EXPECT_EQ(img, nullptr);
}
