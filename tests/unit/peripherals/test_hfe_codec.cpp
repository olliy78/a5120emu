/**
 * @file test_hfe_codec.cpp
 * @brief GoogleTests für HfeCodec (HFE-v1-Container) und die DiskImage-Fabrik.
 *
 * Getestete Komponenten:
 *   1. Cross-Check: HfeCodec::load liefert dieselben Sektoren mit gültigen CRCs
 *      wie ImgCodec::load auf derselben Fixture.
 *   2. Geometrie (num_cyls=2, num_heads=2, MFM).
 *   3. Save/Load-Roundtrip inklusive Änderung — Header/LUT werden neu berechnet.
 *   4. Neuanlage OHNE Vorlage (Medium → .hfe) und unformatierte Spuren.
 *   5. DiskImage::open akzeptiert `.hfe`, auch unformatiert (Leerdiskette).
 *   6. FloppyDriveV2::mount mit HFE-Diskette.
 *   7. Real gelesene Aufnahmen: Phasensprünge je Feld, Überabtastung 500 kbit/s.
 *
 * Fixture-Pfad: FIXTURE_DIR (CMake-Define) / cpa_mini.hfe + cpa_mini.img
 * Geometrie: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B (MFM, 250 kbit/s, 300 U/min)
 *
 * @see core/peripherals/floppy_drive/hfe_codec.h
 * @see tools/img_to_hfe.py  (erzeugt cpa_mini.hfe aus cpa_mini.img)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

#include "core/peripherals/floppy_drive/hfe_codec.h"
#include "core/peripherals/floppy_drive/img_codec.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/disk_format.h"
#include "tests/support/temp_path.h"

// ─── Pfade zu den Fixtures ───────────────────────────────────────────────────

#ifndef FIXTURE_DIR
#  define FIXTURE_DIR "tests/fixtures"
#endif

static const std::string kHfePath = std::string(FIXTURE_DIR) + "/cpa_mini.hfe";
static const std::string kImgPath = std::string(FIXTURE_DIR) + "/cpa_mini.img";

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/**
 * DiskFormat für die Mini-Fixture: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B.
 */
static DiskFormat makeMiniFormat() {
    DiskFormat fmt;
    fmt.name = "cpa_mini";
    fmt.tracks.push_back({0, 1, 0, 1, 4, 128});
    return fmt;
}

static DiskMedium loadHfe(const std::string& path, HfeCodec::SourceInfo* info = nullptr) {
    DiskMedium m;
    std::string err;
    EXPECT_TRUE(HfeCodec::load(path, m, info, err)) << err;
    return m;
}

// ─── Test 1: Cross-Check HFE ↔ Raw (Kern-Test) ───────────────────────────────

TEST(HfeCodec, CrossCheck_HFE_vs_Raw) {
    const auto fmt = makeMiniFormat();

    const DiskMedium hfe = loadHfe(kHfePath);
    DiskMedium raw;
    std::string err;
    ASSERT_TRUE(ImgCodec::load(kImgPath, fmt, raw, err)) << err;

    for (uint8_t cyl = 0; cyl < 2; ++cyl) {
        for (uint8_t head = 0; head < 2; ++head) {
            const TrackImage& hfe_track = hfe.track(cyl, head);
            ASSERT_FALSE(hfe_track.empty())
                << "HFE-Spur (" << (int)cyl << "," << (int)head << ") leer";
            auto hfe_secs = TrackCodec::parseTrack(hfe_track);
            ASSERT_EQ(hfe_secs.size(), 4u);

            auto raw_secs = TrackCodec::parseTrack(raw.track(cyl, head));
            ASSERT_EQ(raw_secs.size(), 4u);

            for (size_t s = 0; s < 4; ++s) {
                EXPECT_TRUE(hfe_secs[s].id_crc_ok)
                    << "HFE ID-CRC cyl=" << (int)cyl << " head=" << (int)head << " idx=" << s;
                EXPECT_TRUE(hfe_secs[s].data_crc_ok)
                    << "HFE Daten-CRC cyl=" << (int)cyl << " head=" << (int)head << " idx=" << s;
                EXPECT_EQ(hfe_secs[s].id,   raw_secs[s].id);
                EXPECT_EQ(hfe_secs[s].size, raw_secs[s].size);
                EXPECT_EQ(hfe_secs[s].data, raw_secs[s].data)
                    << "Nutzdaten weichen ab cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)hfe_secs[s].id;
            }
        }
    }
}

// ─── Test 2: geometry() ──────────────────────────────────────────────────────

TEST(HfeCodec, Geometry_KorrekteFelderMiniFixture) {
    const DiskGeometry g = loadHfe(kHfePath).geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);
    EXPECT_TRUE(g.uniform);
}

// ─── Test 3: Save/Load-Roundtrip ─────────────────────────────────────────────

/**
 * @test HfeCodec/SaveLoad_RoundtripMitAenderung
 * @brief Medium ändern → speichern → neu laden: die Änderung ist da, die übrigen
 *        Spuren sind unverändert.  Header und LUT werden dabei NEU berechnet — das
 *        ist der Unterschied zum früheren In-place-Backend und die Voraussetzung
 *        dafür, dass ein Medium beliebiger Herkunft als .hfe geschrieben werden kann.
 */
TEST(HfeCodec, SaveLoad_RoundtripMitAenderung) {
    DiskMedium m = loadHfe(kHfePath);

    uint8_t modified = 0;
    {
        auto secs = TrackCodec::parseTrack(m.track(0, 0));
        ASSERT_GE(secs.size(), 1u);
        modified = static_cast<uint8_t>(secs[0].data[0] ^ 0xFF);
        secs[0].data[0] = modified;
        m.setTrack(0, 0, TrackCodec::buildTrack(secs, Encoding::MFM));
    }

    const auto tmp = k1520test::tempPath("k1520_test_hfe_roundtrip.hfe");
    std::string err;
    ASSERT_TRUE(HfeCodec::save(tmp, m, err)) << err;

    const DiskMedium back = loadHfe(tmp);
    EXPECT_EQ(back.numCylinders(), 2u);
    EXPECT_EQ(back.numHeads(),     2u);

    auto secs00 = TrackCodec::parseTrack(back.track(0, 0));
    ASSERT_GE(secs00.size(), 1u);
    EXPECT_EQ(secs00[0].data[0], modified) << "Geändertes Byte nicht gespeichert";
    for (const auto& s : secs00) {
        EXPECT_TRUE(s.id_crc_ok);
        EXPECT_TRUE(s.data_crc_ok);
    }

    // Spur (0,1) unverändert gegenüber der Ausgangs-Fixture.
    const DiskMedium ref = loadHfe(kHfePath);
    auto mod01 = TrackCodec::parseTrack(back.track(0, 1));
    auto ref01 = TrackCodec::parseTrack(ref.track(0, 1));
    ASSERT_EQ(mod01.size(), ref01.size());
    for (size_t i = 0; i < mod01.size(); ++i)
        EXPECT_EQ(mod01[i].data, ref01[i].data) << "Spur (0,1) Sektor " << i << " verändert";

    std::filesystem::remove(tmp);
}

// ─── Test 4: Neuanlage ohne Vorlage / unformatierte Spuren ───────────────────

/**
 * @test HfeCodec/SaveOhneVorlage_BemisstSpurlaengeSelbst
 * @brief Ein frisch gebautes Medium (nie aus einer .hfe geladen) lässt sich direkt
 *        als .hfe schreiben; die Spurlänge folgt der längsten Spur.
 */
TEST(HfeCodec, SaveOhneVorlage_BemisstSpurlaengeSelbst) {
    DiskMedium m(3, 1, Encoding::MFM);
    for (uint8_t c = 0; c < 3; ++c) {
        std::vector<LogicalSector> secs;
        for (uint8_t s = 1; s <= 2; ++s) {
            LogicalSector ls;
            ls.cyl = c; ls.head = 0; ls.id = s; ls.size = 1024;
            ls.data.assign(1024, static_cast<uint8_t>(0x40 + c * 4 + s));
            secs.push_back(std::move(ls));
        }
        m.setTrack(c, 0, TrackCodec::buildTrack(secs, Encoding::MFM));
    }

    const auto tmp = k1520test::tempPath("k1520_test_hfe_neu.hfe");
    std::string err;
    ASSERT_TRUE(HfeCodec::save(tmp, m, err)) << err;

    const DiskMedium back = loadHfe(tmp);
    EXPECT_EQ(back.numCylinders(), 3u);
    EXPECT_EQ(back.numHeads(),     1u);
    for (uint8_t c = 0; c < 3; ++c) {
        auto secs = TrackCodec::parseTrack(back.track(c, 0));
        ASSERT_EQ(secs.size(), 2u) << "Spur " << int(c);
        for (const auto& s : secs) {
            EXPECT_EQ(s.size, 1024);
            EXPECT_TRUE(s.id_crc_ok);
            EXPECT_TRUE(s.data_crc_ok);
        }
    }

    std::filesystem::remove(tmp);
}

/**
 * @test HfeCodec/LeerdisketteBleibtUnformatiert
 * @brief Eine komplett leere Diskette überlebt den Roundtrip als **unformatiertes**
 *        Medium — sie muss mountbar bleiben, damit das Gastsystem sie formatieren kann.
 */
TEST(HfeCodec, LeerdisketteBleibtUnformatiert) {
    const DiskMedium leer(40, 1, Encoding::MFM);

    const auto tmp = k1520test::tempPath("k1520_test_hfe_leer.hfe");
    std::string err;
    ASSERT_TRUE(HfeCodec::save(tmp, leer, err)) << err;

    const DiskMedium back = loadHfe(tmp);
    EXPECT_EQ(back.numCylinders(), 40u);
    EXPECT_EQ(back.numHeads(),      1u);
    EXPECT_FALSE(back.formatted());
    EXPECT_TRUE(back.track(0, 0).empty());
    EXPECT_FALSE(back.rawCompatible());

    // Und die Fabrik akzeptiert sie (früher lehnte sie markenlose Images ab).
    auto img = DiskImage::open(tmp, std::nullopt, false);
    ASSERT_NE(img, nullptr) << "Leerdiskette wurde beim Öffnen abgelehnt";
    EXPECT_FALSE(img->rawCompatible());

    std::filesystem::remove(tmp);
}

// ─── Test 5: DiskImage::open-Fabrik ──────────────────────────────────────────

TEST(HfeCodec, DiskImageOpen_HFE_LiefertGueltigesImage) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr) << "DiskImage::open lieferte nullptr für " << kHfePath;

    EXPECT_EQ(img->container(), ContainerType::Hfe);
    EXPECT_EQ(img->diskFormat(), nullptr) << ".hfe ist self-describing";

    DiskGeometry g = img->geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);

    const TrackImage& t = img->readTrack(0, 0);
    ASSERT_FALSE(t.empty());
    auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 4u);
    EXPECT_TRUE(secs[0].id_crc_ok);
    EXPECT_TRUE(secs[0].data_crc_ok);
}

// ─── Test 6: FloppyDriveV2::mount mit HFE-Diskette ───────────────────────────

TEST(HfeCodec, FloppyDriveV2_Mount_HFE_ErfolgreicherMount) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr);

    FloppyDriveV2 drv(builtinDriveProfile("K5601"));   // 80 Zylinder × 2 Köpfe, MFM
    ASSERT_TRUE(drv.mount(std::move(img)))
        << "FloppyDriveV2::mount fehlgeschlagen: " << drv.lastError();
    ASSERT_TRUE(drv.isMounted());
    EXPECT_FALSE(drv.track(0).empty());
}

// ─── Test 7: Schreibschutz + ungültige Spur ──────────────────────────────────

TEST(HfeCodec, WriteProtect_VerhindertSchreiben) {
    auto img = DiskImage::open(kHfePath, std::nullopt, /*wp=*/true);
    ASSERT_NE(img, nullptr);
    EXPECT_FALSE(img->writable());
    EXPECT_FALSE(img->writeTrack(0, 0, img->readTrack(0, 0)));
}

TEST(HfeCodec, ReadTrack_UngueltigerZylinder_Leer) {
    const DiskMedium m = loadHfe(kHfePath);
    EXPECT_TRUE(m.track(99, 0).empty());
}

// ─── Tests 8/9: real gelesene Disketten (Phasensprünge / Überabtastung) ──────
//
// Von echter Hardware eingelesene HFEs (Greaseweazle & Co.) verletzen zwei stille
// Annahmen, die synthetisch erzeugte Images erfüllen:
//   (a) Die ganze Spur liegt NICHT in einer Bytephase.  Jedes Feld wurde einzeln
//       geschrieben; Schreib-Splices und Drehzahl-Jitter verschieben die Folgefelder
//       um beliebige Zellzahlen (auch halbe Bytes).
//   (b) Manche Exporte tasten eine DD-Diskette mit doppelter Rate ab (Header-bitrate
//       500 statt 250 kbit/s) — jede Zelle belegt dann zwei Bits.
// Beides ergab früher „Spur ohne Marken"; die folgenden Fixtures halten das fest.

/// Zellstrom (HFE-Konvention: LSB-first je Byte) → Bitfolge in Leserichtung.
static std::vector<bool> hfeCellsToBits(const std::vector<uint8_t>& cells, uint32_t n) {
    std::vector<bool> b;
    b.reserve(n);
    for (uint32_t i = 0; i < n && i / 8 < cells.size(); ++i)
        b.push_back(((cells[i / 8] >> (i % 8)) & 1u) != 0);
    return b;
}

/// Bitfolge → Zellstrom in HFE-Konvention.
static std::vector<uint8_t> hfeBitsToCells(const std::vector<bool>& b) {
    std::vector<uint8_t> c((b.size() + 7) / 8, 0x00);
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i]) c[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    return c;
}

/// Startpositionen aller A1-/C2-Sync-Gruppen (aufeinanderfolgende Sync-Zellworte).
static std::vector<size_t> findSyncGroups(const std::vector<bool>& b) {
    static const bool kA1[16] = {0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1};  // 0x4489
    static const bool kC2[16] = {0,1,0,1,0,0,1,0,0,0,1,0,0,1,0,0};  // 0x5224
    auto isSync = [&](size_t p) {
        if (p + 16 > b.size()) return false;
        bool a1 = true, c2 = true;
        for (int i = 0; i < 16; ++i) {
            if (b[p + i] != kA1[i]) a1 = false;
            if (b[p + i] != kC2[i]) c2 = false;
        }
        return a1 || c2;
    };
    std::vector<size_t> groups;
    for (size_t p = 0; p + 16 <= b.size(); ++p)
        if (isSync(p) && (p < 16 || !isSync(p - 16)))
            groups.push_back(p);
    return groups;
}

/// Schreibt ein HFE v1 mit einem Zylinder × 2 Seiten aus fertigen Zellströmen.
static void writeMiniHfe(const std::string& path,
                         const std::vector<uint8_t>& side0,
                         const std::vector<uint8_t>& side1,
                         uint16_t bitrate) {
    const size_t side_len = std::max(side0.size(), side1.size());
    const size_t blocks   = (side_len + 255) / 256;      // 256 B je Seite und Block
    const size_t padded   = blocks * 256;

    std::vector<uint8_t> file(512 * 2, 0xFF);            // Header + LUT-Block
    std::memcpy(file.data(), "HXCPICFE", 8);
    file[0x08] = 0;                                       // formatrevision (v1)
    file[0x09] = 1;                                       // number_of_track
    file[0x0A] = 2;                                       // number_of_side
    file[0x0B] = 0;                                       // ISOIBM_MFM
    file[0x0C] = static_cast<uint8_t>(bitrate & 0xFF);
    file[0x0D] = static_cast<uint8_t>(bitrate >> 8);
    file[0x0E] = 300 & 0xFF; file[0x0F] = 300 >> 8;       // rpm
    file[0x12] = 1; file[0x13] = 0;                       // track_list_offset = Block 1
    file[0x14] = 0xFF;                                    // write_allowed

    const uint16_t len_bytes = static_cast<uint16_t>(padded * 2);
    file[512 + 0] = 2; file[512 + 1] = 0;                 // offset_blocks = 2
    file[512 + 2] = static_cast<uint8_t>(len_bytes & 0xFF);
    file[512 + 3] = static_cast<uint8_t>(len_bytes >> 8);

    file.resize(512 * 2 + blocks * 512, 0x00);
    for (size_t blk = 0; blk < blocks; ++blk) {
        for (size_t i = 0; i < 256; ++i) {
            const size_t src = blk * 256 + i;
            if (src < side0.size()) file[1024 + blk * 512 + i]       = side0[src];
            if (src < side1.size()) file[1024 + blk * 512 + 256 + i] = side1[src];
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
}

/// 4 Sektoren × 128 B als Zellstrom (Referenz für beide Tests).
static std::vector<uint8_t> buildMiniCells(uint32_t& bitcells_out) {
    std::vector<LogicalSector> secs;
    for (uint8_t s = 1; s <= 4; ++s) {
        LogicalSector ls;
        ls.cyl = 0; ls.head = 0; ls.id = s; ls.size = 128;
        ls.data.assign(128, static_cast<uint8_t>(0xA0 + s));
        secs.push_back(std::move(ls));
    }
    const TrackImage t = TrackCodec::buildTrack(secs, Encoding::MFM);
    // Reichlich Reserve, damit encode() nichts abschneidet.
    const uint32_t target = static_cast<uint32_t>(t.bytes.size() * 16 + 4096);
    bitcells_out = target;
    return BitCodec::encode(t, target);
}

/// Prüft, dass die Spur alle 4 Sektoren mit gültigen CRCs liefert.
static void expectFourGoodSectors(const TrackImage& t, const char* what) {
    const auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 4u) << what << ": nicht alle Sektoren gefunden";
    for (size_t i = 0; i < secs.size(); ++i) {
        EXPECT_EQ(secs[i].id, static_cast<uint8_t>(i + 1)) << what;
        EXPECT_TRUE(secs[i].id_crc_ok)   << what << ": ID-CRC Sektor " << (i + 1);
        EXPECT_TRUE(secs[i].data_crc_ok) << what << ": Daten-CRC Sektor " << (i + 1);
        ASSERT_EQ(secs[i].data.size(), 128u) << what;
        EXPECT_EQ(secs[i].data[0], static_cast<uint8_t>(0xA0 + i + 1)) << what;
    }
}

// Test 8: Felder mit eigener Bitphase (halbes Byte Versatz je Feld) müssen sich
// alle decodieren lassen — der Decoder muss an jeder Sync-Gruppe neu einrasten.
TEST(BitCodecDecode, PhasenversetzteFelderWerdenAlleDecodiert) {
    uint32_t bitcells = 0;
    const auto cells = buildMiniCells(bitcells);

    auto bits = hfeCellsToBits(cells, bitcells);
    const auto groups = findSyncGroups(bits);
    ASSERT_GE(groups.size(), 4u) << "Fixture liefert zu wenige Sync-Gruppen";

    for (size_t i = groups.size(); i-- > 1;)
        bits.insert(bits.begin() + static_cast<ptrdiff_t>(groups[i]), 8, false);

    const auto shifted = hfeBitsToCells(bits);
    const TrackImage t = BitCodec::decode(shifted, static_cast<uint32_t>(bits.size()),
                                          Encoding::MFM);
    expectFourGoodSectors(t, "phasenversetzt");
}

// Test 9: 2x überabgetastete Aufnahme (HFE-bitrate 500) — der Codec muss sie auf die
// Nominalrate herunterrechnen und lesen können; die Bindung ist dann nur lesend, ein
// „Speichern unter" in eine neue Datei bleibt aber möglich.
TEST(HfeCodec, UeberabgetasteteAufnahme500kbitWirdGelesen) {
    uint32_t bitcells = 0;
    const auto cells = buildMiniCells(bitcells);
    const auto bits  = hfeCellsToBits(cells, bitcells);

    std::vector<bool> os(bits.size() * 2, false);
    for (size_t cell = 0; cell < bits.size(); ++cell)
        if (bits[cell]) os[cell * 2] = true;
    const auto os_cells = hfeBitsToCells(os);

    const auto path = k1520test::tempPath("k1520_test_hfe_oversampled.hfe");
    writeMiniHfe(path, os_cells, os_cells, /*bitrate=*/500);

    HfeCodec::SourceInfo info;
    const DiskMedium m = loadHfe(path, &info);
    EXPECT_TRUE(info.oversampled);

    expectFourGoodSectors(m.track(0, 0), "500 kbit/s Seite 0");
    expectFourGoodSectors(m.track(0, 1), "500 kbit/s Seite 1");

    // Die Bindung ist nur lesend — ein Rückschreiben in die Originalrate gibt es nicht.
    auto img = DiskImage::open(path, std::nullopt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_FALSE(img->bindingWritable());

    std::filesystem::remove(path);
}

/**
 * @test HfeCodec/UeberabtastungTrotzFalscherHeaderRate
 * @brief Ein überabgetasteter Mitschnitt wird auch dann gelesen, wenn der Header
 *        eine FALSCHE Bitrate meldet.
 *
 * Reale Greaseweazle-Exporte tragen im Header gelegentlich Unsinn — beobachtet an
 * einer echten Leerdiskette: `bitrate = 311` kbit/s bei tatsächlich ~2,4-facher
 * Abtastung.  Die frühere Erkennung schloss allein aus dem Header-Wert auf den
 * Faktor und ließ alles unter 375 kbit/s ungefiltert durch → der Decoder fand
 * keine einzige Adressmarke.  Jetzt entscheidet der INHALT: der Faktor, unter dem
 * Marken auftauchen, gewinnt.
 */
TEST(HfeCodec, UeberabtastungTrotzFalscherHeaderRate) {
    uint32_t bitcells = 0;
    const auto cells = buildMiniCells(bitcells);
    const auto bits  = hfeCellsToBits(cells, bitcells);

    // 2x ueberabtasten (wie ein Flux-Reader mit doppelter Rate).
    std::vector<bool> os(bits.size() * 2, false);
    for (size_t cell = 0; cell < bits.size(); ++cell)
        if (bits[cell]) os[cell * 2] = true;
    const auto os_cells = hfeBitsToCells(os);

    const auto path = k1520test::tempPath("k1520_test_hfe_falsche_rate.hfe");
    // Header meldet 311 kbit/s — unter der alten 375er-Schwelle, also "keine
    // Ueberabtastung", obwohl der Inhalt doppelt abgetastet ist.
    writeMiniHfe(path, os_cells, os_cells, /*bitrate=*/311);

    HfeCodec::SourceInfo info;
    const DiskMedium m = loadHfe(path, &info);
    EXPECT_TRUE(info.oversampled) << "Ueberabtastung nicht am Inhalt erkannt";
    expectFourGoodSectors(m.track(0, 0), "311-kbit/s-Header, 2x abgetastet");
    expectFourGoodSectors(m.track(0, 1), "311-kbit/s-Header, Seite 1");

    std::filesystem::remove(path);
}
