/**
 * @file test_dmk_codec.cpp
 * @brief GoogleTests für DmkCodec (David Keil's Disk Image).
 *
 * Schwerpunkte:
 *   - Header-Aufbau (Zylinderzahl, Spurlänge, Seiten-/Dichte-Flags)
 *   - IDAM-Tabelle: Positionen + Dichte-Bit; Datenmarke wird beim Laden gefunden
 *   - FM-Byte-Verdopplung (und ihr Wegfall bei reinen SD-Disketten)
 *   - Roundtrip: Nutzdaten, Mischdichte, unformatierte Spuren, Gap-Anhänge
 *   - Erkennung (looksLikeDmk) und Container-Sniffing über ImageCodec::detect
 *
 * @see core/peripherals/floppy_drive/dmk_codec.h
 * @see doc/design/09_floppy_drive.md §4.3
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

#include "core/peripherals/floppy_drive/dmk_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/image_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"

namespace {

std::string tmpPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

TrackImage makeTrack(uint8_t cyl, uint8_t head, int nsec, uint16_t size,
                     Encoding enc, uint8_t fill) {
    std::vector<LogicalSector> secs;
    for (int i = 1; i <= nsec; ++i) {
        LogicalSector ls;
        ls.cyl  = cyl;
        ls.head = head;
        ls.id   = static_cast<uint8_t>(i);
        ls.size = size;
        ls.data.assign(size, static_cast<uint8_t>(fill + i));
        secs.push_back(std::move(ls));
    }
    return TrackCodec::buildTrack(secs, enc);
}

DiskMedium loadDmk(const std::string& path, DmkCodec::SourceInfo* info = nullptr) {
    DiskMedium m;
    std::string err;
    EXPECT_TRUE(DmkCodec::load(path, m, info, err)) << err;
    return m;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

// ─── Header ──────────────────────────────────────────────────────────────────

TEST(DmkCodec, Header_ZweiseitigMfm) {
    DiskMedium m(2, 2, Encoding::MFM);
    for (uint8_t c = 0; c < 2; ++c)
        for (uint8_t h = 0; h < 2; ++h)
            m.setTrack(c, h, makeTrack(c, h, 4, 256, Encoding::MFM, 0x10));

    const auto path = tmpPath("k1520_test_dmk_hdr.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const auto f = readFile(path);
    ASSERT_GE(f.size(), 16u);
    EXPECT_EQ(f[0], 0x00);                                  // nicht schreibgeschützt
    EXPECT_EQ(f[1], 2);                                     // 2 Zylinder
    const uint16_t track_len = static_cast<uint16_t>(f[2] | (f[3] << 8));
    EXPECT_GT(track_len, 128);
    EXPECT_EQ(track_len % 256, 0) << "Spurlänge auf 256 aufgerundet";
    EXPECT_EQ(f[4] & 0x10, 0) << "zweiseitig → Bit4 = 0";
    EXPECT_EQ(f[4] & 0x40, 0) << "MFM → kein SD-Flag";
    EXPECT_EQ(f.size(), 16u + 2u * 2u * track_len);

    std::filesystem::remove(path);
}

TEST(DmkCodec, Header_EinseitigReinesFmSetztSdFlag) {
    DiskMedium m(3, 1, Encoding::FM);
    for (uint8_t c = 0; c < 3; ++c)
        m.setTrack(c, 0, makeTrack(c, 0, 4, 128, Encoding::FM, 0x20));

    const auto path = tmpPath("k1520_test_dmk_fm.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const auto f = readFile(path);
    EXPECT_EQ(f[1], 3);
    EXPECT_EQ(f[4] & 0x10, 0x10) << "einseitig → Bit4 gesetzt";
    EXPECT_EQ(f[4] & 0x40, 0x40) << "reine FM-Diskette → SD-Flag, keine Verdopplung";

    std::filesystem::remove(path);
}

// ─── IDAM-Tabelle ────────────────────────────────────────────────────────────

TEST(DmkCodec, IdamTabelle_EnthaeltAlleSektorenMitDichteBit) {
    DiskMedium m(1, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 5, 256, Encoding::MFM, 0x30));

    const auto path = tmpPath("k1520_test_dmk_idam.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const auto f = readFile(path);
    const uint16_t track_len = static_cast<uint16_t>(f[2] | (f[3] << 8));

    int belegt = 0;
    for (size_t k = 0; k < 64; ++k) {
        const uint16_t e = static_cast<uint16_t>(f[16 + k * 2] | (f[16 + k * 2 + 1] << 8));
        if (e == 0) continue;
        ++belegt;
        EXPECT_TRUE(e & 0x8000) << "MFM-Sektor muss Bit15 tragen";
        const uint16_t off = e & 0x3FFF;
        EXPECT_GE(off, 128) << "Offset zählt ab Spuranfang (inkl. IDAM-Tabelle)";
        EXPECT_LT(off, track_len);
        EXPECT_EQ(f[16 + off], 0xFE) << "Eintrag muss auf das IDAM-Byte zeigen";
    }
    EXPECT_EQ(belegt, 5);

    std::filesystem::remove(path);
}

// ─── Roundtrip ───────────────────────────────────────────────────────────────

TEST(DmkCodec, Roundtrip_MfmNutzdatenUndMarken) {
    DiskMedium m(2, 2, Encoding::MFM);
    for (uint8_t c = 0; c < 2; ++c)
        for (uint8_t h = 0; h < 2; ++h)
            m.setTrack(c, h, makeTrack(c, h, 4, 256, Encoding::MFM,
                                       static_cast<uint8_t>(0x50 + c * 8 + h * 4)));

    const auto path = tmpPath("k1520_test_dmk_rt.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const DiskMedium back = loadDmk(path);
    ASSERT_EQ(back.numCylinders(), 2u);
    ASSERT_EQ(back.numHeads(),     2u);

    for (uint8_t c = 0; c < 2; ++c) {
        for (uint8_t h = 0; h < 2; ++h) {
            const auto vorher  = TrackCodec::parseTrack(m.track(c, h));
            const auto nachher = TrackCodec::parseTrack(back.track(c, h));
            ASSERT_EQ(nachher.size(), vorher.size())
                << "Spur " << int(c) << "/" << int(h);
            EXPECT_EQ(back.track(c, h).encoding, Encoding::MFM);
            for (size_t i = 0; i < vorher.size(); ++i) {
                EXPECT_EQ(nachher[i].id,   vorher[i].id);
                EXPECT_EQ(nachher[i].size, vorher[i].size);
                EXPECT_EQ(nachher[i].data, vorher[i].data);
                EXPECT_TRUE(nachher[i].id_crc_ok);
                EXPECT_TRUE(nachher[i].data_crc_ok);
            }
        }
    }

    std::filesystem::remove(path);
}

TEST(DmkCodec, Roundtrip_FmSpurenUeberstehenVerdopplung) {
    // Mischdichte erzwingt die Verdopplung der FM-Spuren (kein SD-Flag).
    DiskMedium m(2, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 4,  128, Encoding::FM,  0x60));
    m.setTrack(1, 0, makeTrack(1, 0, 2, 1024, Encoding::MFM, 0x70));

    const auto path = tmpPath("k1520_test_dmk_mixed.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const auto f = readFile(path);
    EXPECT_EQ(f[4] & 0x40, 0) << "gemischte Dichte → kein SD-Flag";

    const DiskMedium back = loadDmk(path);
    EXPECT_EQ(back.track(0, 0).encoding, Encoding::FM);
    EXPECT_EQ(back.track(1, 0).encoding, Encoding::MFM);

    auto fm  = TrackCodec::parseTrack(back.track(0, 0));
    auto mfm = TrackCodec::parseTrack(back.track(1, 0));
    ASSERT_EQ(fm.size(),  4u);
    ASSERT_EQ(mfm.size(), 2u);
    for (const auto& s : fm)  { EXPECT_TRUE(s.id_crc_ok); EXPECT_TRUE(s.data_crc_ok); }
    for (const auto& s : mfm) { EXPECT_TRUE(s.id_crc_ok); EXPECT_TRUE(s.data_crc_ok); }
    EXPECT_EQ(fm[0].data, TrackCodec::parseTrack(m.track(0, 0))[0].data);

    std::filesystem::remove(path);
}

TEST(DmkCodec, Roundtrip_ReinesFmOhneVerdopplung) {
    DiskMedium m(2, 1, Encoding::FM);
    for (uint8_t c = 0; c < 2; ++c)
        m.setTrack(c, 0, makeTrack(c, 0, 4, 128, Encoding::FM, 0x80));

    const auto path = tmpPath("k1520_test_dmk_sd.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const DiskMedium back = loadDmk(path);
    EXPECT_EQ(back.track(0, 0).encoding, Encoding::FM);
    auto secs = TrackCodec::parseTrack(back.track(0, 0));
    ASSERT_EQ(secs.size(), 4u);
    for (const auto& s : secs) { EXPECT_TRUE(s.id_crc_ok); EXPECT_TRUE(s.data_crc_ok); }

    std::filesystem::remove(path);
}

/**
 * @test DmkCodec/Roundtrip_AnhangHinterDatenCrcBleibtErhalten
 * @brief Der eigentliche Grund für `.dmk`/`.hfe`: der UDOS-Sektorkontrollblock hinter
 *        der Daten-CRC muss den Roundtrip überleben (ein `.img` verlöre ihn).
 */
TEST(DmkCodec, Roundtrip_AnhangHinterDatenCrcBleibtErhalten) {
    DiskMedium m(1, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 2, 128, Encoding::MFM, 0x90));

    size_t tail = 0;
    {
        TrackImage& t = m.mutableTrack(0, 0);
        const size_t dam = t.nextMark(0, MarkType::Data);
        ASSERT_NE(dam, SIZE_MAX);
        tail = dam + 1 + 128 + 2;
        ASSERT_LT(tail + 3, t.bytes.size());
        t.bytes[tail + 0] = 0x1A;
        t.bytes[tail + 1] = 0x2B;
        t.bytes[tail + 2] = 0x3C;
    }
    ASSERT_FALSE(m.rawCompatible());

    const auto path = tmpPath("k1520_test_dmk_tail.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const DiskMedium back = loadDmk(path);
    const TrackImage& t = back.track(0, 0);
    ASSERT_GT(t.bytes.size(), tail + 3);
    EXPECT_EQ(t.bytes[tail + 0], 0x1A);
    EXPECT_EQ(t.bytes[tail + 1], 0x2B);
    EXPECT_EQ(t.bytes[tail + 2], 0x3C);
    EXPECT_FALSE(back.rawCompatible()) << "Flag muss auch nach dem Roundtrip fallen";

    std::filesystem::remove(path);
}

TEST(DmkCodec, Roundtrip_UnformatierteSpurBleibtLeer) {
    DiskMedium m(3, 1, Encoding::MFM);
    m.setTrack(0, 0, makeTrack(0, 0, 4, 256, Encoding::MFM, 0xA0));
    // Spuren 1 und 2 bleiben unformatiert.

    const auto path = tmpPath("k1520_test_dmk_leer.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    const DiskMedium back = loadDmk(path);
    EXPECT_FALSE(back.track(0, 0).empty());
    EXPECT_TRUE(back.track(1, 0).empty()) << "Spur ohne IDAM = unformatiert";
    EXPECT_TRUE(back.track(2, 0).empty());
    EXPECT_TRUE(back.formatted());

    std::filesystem::remove(path);
}

TEST(DmkCodec, Leerdiskette_UeberstehtRoundtrip) {
    const DiskMedium leer(40, 1, Encoding::MFM);
    const auto path = tmpPath("k1520_test_dmk_blank.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, leer, err)) << err;

    const DiskMedium back = loadDmk(path);
    EXPECT_EQ(back.numCylinders(), 40u);
    EXPECT_EQ(back.numHeads(),      1u);
    EXPECT_FALSE(back.formatted());

    std::filesystem::remove(path);
}

// ─── Erkennung ───────────────────────────────────────────────────────────────

TEST(DmkCodec, LooksLikeDmk_ErkenntEigeneDatei) {
    DiskMedium m(2, 1, Encoding::MFM);
    for (uint8_t c = 0; c < 2; ++c)
        m.setTrack(c, 0, makeTrack(c, 0, 4, 256, Encoding::MFM, 0xB0));

    const auto path = tmpPath("k1520_test_dmk_detect.dmk");
    std::string err;
    ASSERT_TRUE(DmkCodec::save(path, m, err)) << err;

    EXPECT_TRUE(DmkCodec::looksLikeDmk(path));
    EXPECT_EQ(ImageCodec::detect(path), ContainerType::Dmk);
    EXPECT_EQ(ImageCodec::fromExtension("x.DMK"), ContainerType::Dmk);
    EXPECT_TRUE(ImageCodec::selfDescribing(ContainerType::Dmk));
    EXPECT_FALSE(ImageCodec::needsDiskFormat(ContainerType::Dmk));

    // Und über die Fabrik ohne DiskFormat öffenbar.
    auto img = DiskImage::open(path, std::nullopt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->container(), ContainerType::Dmk);
    EXPECT_EQ(img->diskFormat(), nullptr);

    std::filesystem::remove(path);
}

TEST(DmkCodec, LooksLikeDmk_LehntFremdeDateiAb) {
    const auto path = tmpPath("k1520_test_dmk_fremd.dmk");
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        const std::vector<uint8_t> muell(4096, 0x5A);
        f.write(reinterpret_cast<const char*>(muell.data()),
                static_cast<std::streamsize>(muell.size()));
    }
    EXPECT_FALSE(DmkCodec::looksLikeDmk(path));
    EXPECT_EQ(ImageCodec::detect(path), ContainerType::Img);
    std::filesystem::remove(path);
}
