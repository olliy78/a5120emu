/**
 * @file test_hfe_image.cpp
 * @brief GoogleTests für HfeImage (HFE-v1-Backend) und DiskImage::open-Fabrik.
 *
 * Getestete Komponenten:
 *   1. Cross-Check: HfeImage::readTrack → parseTrack liefert dieselben Sektoren
 *      mit id_crc_ok && data_crc_ok wie RawSectorImage auf derselben Fixture.
 *   2. geometry(): num_cyls=2, num_heads=2, encoding=MFM.
 *   3. Write-Roundtrip: Byte ändern → writeTrack → flush → neu öffnen → parseTrack.
 *   4. Header + LUT unverändert nach Write.
 *   5. DiskImage::open-Fabrik liefert ein gültiges HfeImage für cpa_mini.hfe.
 *   6. FloppyDriveV2::mount mit HFE-Image (optional).
 *
 * Fixture-Pfad: FIXTURE_DIR (CMake-Define) / cpa_mini.hfe + cpa_mini.img
 * Geometrie: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B (MFM, 250 kbit/s, 300 U/min)
 *
 * @see core/peripherals/floppy_drive/hfe_image.h
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

#include "core/peripherals/floppy_drive/hfe_image.h"
#include "core/peripherals/floppy_drive/bit_codec.h"
#include "core/peripherals/floppy_drive/raw_sector_image.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/floppy_drive2.h"
#include "core/peripherals/floppy_drive/drive_profile.h"
#include "core/peripherals/floppy_drive/disk_format.h"

// ─── Pfade zu den Fixtures ───────────────────────────────────────────────────

#ifndef FIXTURE_DIR
#  define FIXTURE_DIR "tests/fixtures/disks"
#endif

static const std::string kHfePath  = std::string(FIXTURE_DIR) + "/cpa_mini.hfe";
static const std::string kImgPath  = std::string(FIXTURE_DIR) + "/cpa_mini.img";

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

/**
 * DiskFormat für die Mini-Fixture: 2 Zylinder × 2 Köpfe × 4 Sektoren × 128 B.
 * Uniform (ein TrackFormat-Eintrag deckt alle Spuren ab).
 */
static DiskFormat makeMiniFormat() {
    DiskFormat fmt;
    fmt.name = "cpa_mini";
    // Uniform: cyl 0..1, head 0..1, 4 Sektoren × 128 B
    fmt.tracks.push_back({0, 1, 0, 1, 4, 128});
    return fmt;
}

// ─── Test 1: Cross-Check HFE ↔ Raw (Kern-Test) ───────────────────────────────

/**
 * Für jede (cyl, head) müssen HfeImage::readTrack→parseTrack und
 * RawSectorImage::readTrack→parseTrack identische Sektoren liefern:
 *   - gleiche Anzahl Sektoren
 *   - id_crc_ok && data_crc_ok für jeden Sektor
 *   - gleiche Sektordaten (bitgleich)
 */
TEST(HfeImage, CrossCheck_HFE_vs_Raw) {
    const auto fmt = makeMiniFormat();

    HfeImage       hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen()) << "HFE-Datei konnte nicht geöffnet werden: " << kHfePath;

    RawSectorImage raw(kImgPath, fmt, /*wp=*/false);
    ASSERT_TRUE(raw.isOpen()) << "IMG-Datei konnte nicht geöffnet werden: " << kImgPath;

    for (uint8_t cyl = 0; cyl < 2; ++cyl) {
        for (uint8_t head = 0; head < 2; ++head) {
            // ── HFE ──────────────────────────────────────────────────────────
            TrackImage hfe_track = hfe.readTrack(cyl, head);
            ASSERT_FALSE(hfe_track.empty())
                << "HFE readTrack(" << (int)cyl << "," << (int)head << ") leer";

            auto hfe_secs = TrackCodec::parseTrack(hfe_track);
            ASSERT_EQ(hfe_secs.size(), 4u)
                << "HFE Sektoranzahl cyl=" << (int)cyl << " head=" << (int)head;

            // ── Raw ───────────────────────────────────────────────────────────
            TrackImage raw_track = raw.readTrack(cyl, head);
            ASSERT_FALSE(raw_track.empty())
                << "Raw readTrack(" << (int)cyl << "," << (int)head << ") leer";

            auto raw_secs = TrackCodec::parseTrack(raw_track);
            ASSERT_EQ(raw_secs.size(), 4u)
                << "Raw Sektoranzahl cyl=" << (int)cyl << " head=" << (int)head;

            // ── Vergleich ─────────────────────────────────────────────────────
            for (size_t s = 0; s < 4; ++s) {
                EXPECT_TRUE(hfe_secs[s].id_crc_ok)
                    << "HFE ID-CRC fehler cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;
                EXPECT_TRUE(hfe_secs[s].data_crc_ok)
                    << "HFE Daten-CRC fehler cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;

                EXPECT_EQ(hfe_secs[s].id,   raw_secs[s].id)
                    << "Sektor-ID weicht ab cyl=" << (int)cyl
                    << " head=" << (int)head << " idx=" << s;
                EXPECT_EQ(hfe_secs[s].size, raw_secs[s].size)
                    << "Sektorgröße weicht ab";
                EXPECT_EQ(hfe_secs[s].data, raw_secs[s].data)
                    << "Nutzdaten weichen ab cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)hfe_secs[s].id;
            }
        }
    }
}

// ─── Test 2: geometry() ──────────────────────────────────────────────────────

TEST(HfeImage, Geometry_KorrekteFelderMiniFixture) {
    HfeImage hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen());

    DiskGeometry g = hfe.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);
    EXPECT_TRUE(g.uniform);
}

// ─── Test 3: Write-Roundtrip ─────────────────────────────────────────────────

/**
 * Schreibt cpa_mini.hfe in /tmp, modifiziert secs[0].data[0] auf cyl=0,head=0,
 * schreibt zurück via writeTrack→flush, öffnet neu und verifiziert:
 *   - geändertes Byte sichtbar in parseTrack
 *   - andere Sektoren derselben Spur unverändert
 *   - Spur (0,1) unverändert
 */
TEST(HfeImage, WriteRoundtrip_ByteAendernUndVerifizieren) {
    // Temporäre Kopie anlegen
    const auto tmp_path = (std::filesystem::temp_directory_path()
                           / "cpa_mini_write_test.hfe").string();
    std::filesystem::copy_file(kHfePath, tmp_path,
                               std::filesystem::copy_options::overwrite_existing);

    // Ursprünglichen Wert merken
    uint8_t original_byte = 0;
    uint8_t modified_byte = 0;
    {
        HfeImage hfe_r(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_r.isOpen());
        TrackImage t = hfe_r.readTrack(0, 0);
        auto secs = TrackCodec::parseTrack(t);
        ASSERT_GE(secs.size(), 1u);
        ASSERT_FALSE(secs[0].data.empty());
        original_byte = secs[0].data[0];
        modified_byte = static_cast<uint8_t>(original_byte ^ 0xFF);
    }

    // Schreiben: secs[0].data[0] ändern
    {
        HfeImage hfe_w(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_w.isOpen());
        ASSERT_TRUE(hfe_w.writable());

        TrackImage t = hfe_w.readTrack(0, 0);
        uint32_t orig_bitcells = t.bitcells;
        auto secs = TrackCodec::parseTrack(t);
        ASSERT_GE(secs.size(), 1u);
        secs[0].data[0] = modified_byte;

        TrackImage t_new = TrackCodec::buildTrack(secs, Encoding::MFM);
        t_new.bitcells = orig_bitcells;   // ursprüngliche Bitzellenzahl erhalten

        ASSERT_TRUE(hfe_w.writeTrack(0, 0, t_new));
        ASSERT_TRUE(hfe_w.flush());
    }

    // Neu öffnen und verifizieren
    {
        HfeImage hfe_v(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe_v.isOpen());

        // Geänderter Sektor (cyl=0, head=0)
        TrackImage t00 = hfe_v.readTrack(0, 0);
        auto secs00 = TrackCodec::parseTrack(t00);
        ASSERT_GE(secs00.size(), 1u);
        EXPECT_EQ(secs00[0].data[0], modified_byte)
            << "Geändertes Byte nicht gespeichert";

        // Andere Sektoren derselben Spur unverändert
        for (size_t i = 1; i < secs00.size(); ++i) {
            EXPECT_TRUE(secs00[i].id_crc_ok)   << "id_crc_ok Sektor " << i;
            EXPECT_TRUE(secs00[i].data_crc_ok) << "data_crc_ok Sektor " << i;
        }

        // Spur (0, 1) darf nicht verändert worden sein
        HfeImage hfe_ref(kHfePath, /*wp=*/false);
        ASSERT_TRUE(hfe_ref.isOpen());

        TrackImage t01_mod = hfe_v.readTrack(0, 1);
        TrackImage t01_ref = hfe_ref.readTrack(0, 1);
        auto secs01_mod = TrackCodec::parseTrack(t01_mod);
        auto secs01_ref = TrackCodec::parseTrack(t01_ref);
        ASSERT_EQ(secs01_mod.size(), secs01_ref.size());
        for (size_t i = 0; i < secs01_mod.size(); ++i) {
            EXPECT_EQ(secs01_mod[i].data, secs01_ref[i].data)
                << "Spur (0,1) Sektor " << i << " verändert";
        }
    }

    std::filesystem::remove(tmp_path);
}

// ─── Test 4: Header + LUT nach Write unverändert ─────────────────────────────

TEST(HfeImage, HeaderLUT_NachWrite_Unveraendert) {
    const auto tmp_path = (std::filesystem::temp_directory_path()
                           / "cpa_mini_header_test.hfe").string();
    std::filesystem::copy_file(kHfePath, tmp_path,
                               std::filesystem::copy_options::overwrite_existing);

    // Original-Header+LUT lesen (erste 2×512 = 1024 Bytes)
    std::vector<uint8_t> hdr_before(1024);
    {
        std::ifstream f(tmp_path, std::ios::binary);
        f.read(reinterpret_cast<char*>(hdr_before.data()), 1024);
    }

    // Schreiben
    {
        HfeImage hfe(tmp_path, /*wp=*/false);
        ASSERT_TRUE(hfe.isOpen());
        TrackImage t = hfe.readTrack(0, 0);
        // Minimale Änderung: einfach dieselbe Spur zurückschreiben
        ASSERT_TRUE(hfe.writeTrack(0, 0, t));
        ASSERT_TRUE(hfe.flush());
    }

    // Header+LUT nach Write vergleichen
    std::vector<uint8_t> hdr_after(1024);
    {
        std::ifstream f(tmp_path, std::ios::binary);
        f.read(reinterpret_cast<char*>(hdr_after.data()), 1024);
    }

    EXPECT_EQ(hdr_before, hdr_after)
        << "Header oder LUT nach writeTrack/flush verändert";

    std::filesystem::remove(tmp_path);
}

// ─── Test 5: DiskImage::open-Fabrik ─────────────────────────────────────────

TEST(HfeImage, DiskImageOpen_HFE_LiefertGueltigesImage) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr) << "DiskImage::open lieferte nullptr für " << kHfePath;

    DiskGeometry g = img->geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 2u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);

    // readTrack muss für cyl=0,head=0 eine gültige Spur liefern
    TrackImage t = img->readTrack(0, 0);
    ASSERT_FALSE(t.empty());
    auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 4u);
    EXPECT_TRUE(secs[0].id_crc_ok);
    EXPECT_TRUE(secs[0].data_crc_ok);
}

// ─── Test 6: FloppyDriveV2::mount mit HFE-Image ──────────────────────────────

TEST(HfeImage, FloppyDriveV2_Mount_HFE_ErfolgreicherMount) {
    auto img = DiskImage::open(kHfePath, std::nullopt, false);
    ASSERT_NE(img, nullptr);

    // K5601: 80 Zylindern × 2 Köpfe, MFM
    FloppyDriveV2 drv(builtinDriveProfile("K5601"));
    ASSERT_TRUE(drv.mount(std::move(img)))
        << "FloppyDriveV2::mount fehlgeschlagen: " << drv.lastError();
    ASSERT_TRUE(drv.isMounted());

    // Spur lesen
    const TrackImage& t = drv.track(0);
    EXPECT_FALSE(t.empty());
}

// ─── Test 7: Schreibschutz-Flag ───────────────────────────────────────────────

TEST(HfeImage, WriteProtect_VerhindertSchreiben) {
    HfeImage hfe(kHfePath, /*wp=*/true);
    ASSERT_TRUE(hfe.isOpen());
    EXPECT_FALSE(hfe.writable());

    TrackImage t = hfe.readTrack(0, 0);
    EXPECT_FALSE(hfe.writeTrack(0, 0, t))
        << "writeTrack sollte bei Schreibschutz false liefern";
}

// ─── Test 8: Ungültige Spur → leeres TrackImage ───────────────────────────────

TEST(HfeImage, ReadTrack_UngueltigerZylinder_Leer) {
    HfeImage hfe(kHfePath, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen());

    TrackImage t = hfe.readTrack(99, 0);
    EXPECT_TRUE(t.empty());
}

// ─── Test 9: HXCHFEV3-Signatur → nullptr (kein Rückschritt) ─────────────────

TEST(DiskImageOpen, HXCHFEV3_Signatur_gibtNullptr) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hfe_hxchfev3.img").string();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write("HXCHFEV3", 8);
        uint8_t pad[64] = {};
        f.write(reinterpret_cast<char*>(pad), sizeof(pad));
    }
    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

// ─── Tests 10/11: real gelesene Disketten (Phasensprünge / Überabtastung) ─────
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

// Test 10: Felder mit eigener Bitphase (halbes Byte Versatz je Feld) müssen sich
// alle decodieren lassen — der Decoder muss an jeder Sync-Gruppe neu einrasten.
TEST(BitCodecDecode, PhasenversetzteFelderWerdenAlleDecodiert) {
    uint32_t bitcells = 0;
    const auto cells = buildMiniCells(bitcells);

    auto bits = hfeCellsToBits(cells, bitcells);
    const auto groups = findSyncGroups(bits);
    ASSERT_GE(groups.size(), 4u) << "Fixture liefert zu wenige Sync-Gruppen";

    // Vor jeder Gruppe ab der zweiten 8 Null-Zellen einschieben (von hinten, damit
    // die vorher ermittelten Positionen gültig bleiben): jedes Feld bekommt so eine
    // eigene, um ein halbes Byte verschobene Phase.
    for (size_t i = groups.size(); i-- > 1;)
        bits.insert(bits.begin() + static_cast<ptrdiff_t>(groups[i]), 8, false);

    const auto shifted = hfeBitsToCells(bits);
    const TrackImage t = BitCodec::decode(shifted, static_cast<uint32_t>(bits.size()),
                                          Encoding::MFM);
    expectFourGoodSectors(t, "phasenversetzt");
}

// Test 11: 2x überabgetastete Aufnahme (HFE-bitrate 500) — HfeImage muss sie auf die
// Nominalrate herunterrechnen und lesen können; schreibbar ist sie dann nicht.
TEST(HfeImage, UeberabgetasteteAufnahme500kbitWirdGelesen) {
    uint32_t bitcells = 0;
    const auto cells = buildMiniCells(bitcells);
    const auto bits  = hfeCellsToBits(cells, bitcells);

    // Jede Zelle → 2 Abtastwerte; die Flanke liegt auf dem Abtastraster des Lesegeräts.
    // (Ein Jitter von ±1 Abtastwert wäre bei 2 Abtastwerten je Zelle eine HALBE Zelle
    // und damit grundsätzlich nicht mehr rekonstruierbar — genau deshalb bleibt ein
    // 500-kbit/s-Export gegenüber einem 250-kbit/s-Export der zweite Weg: er trägt den
    // Restjitter des Lesegeräts als vereinzelte CRC-Fehler mit.)
    std::vector<bool> os(bits.size() * 2, false);
    for (size_t cell = 0; cell < bits.size(); ++cell)
        if (bits[cell]) os[cell * 2] = true;
    const auto os_cells = hfeBitsToCells(os);

    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hfe_oversampled.hfe").string();
    writeMiniHfe(path, os_cells, os_cells, /*bitrate=*/500);

    HfeImage hfe(path, /*wp=*/false);
    ASSERT_TRUE(hfe.isOpen()) << hfe.lastError();
    EXPECT_FALSE(hfe.writable())
        << "überabgetastete Images sind nicht treu zurückschreibbar";

    expectFourGoodSectors(hfe.readTrack(0, 0), "500 kbit/s Seite 0");
    expectFourGoodSectors(hfe.readTrack(0, 1), "500 kbit/s Seite 1");

    std::filesystem::remove(path);
}
