/**
 * @file test_img_codec.cpp
 * @brief GoogleTests für ImgCodec (rohes Sektorimage) und die DiskImage-Fabriken.
 *
 * Getestete Komponenten:
 *   - ImgCodec::load  → DiskMedium (Bitgleichheit mit der .img-Datei als Ground-Truth)
 *   - ImgCodec::save  → Rückschreiben, inkl. Ablehnung nicht darstellbarer Medien
 *   - ImgCodec::mismatchReason (Geometrie-/Sektorlayout-Abgleich)
 *   - DiskImage::open (Container-Sniffing) und DiskImage::create (vorformatiert)
 *
 * Der Bitgleichheits-Vergleich beweist, dass load + parseTrack für jede
 * (cyl, head, id)-Kombination exakt die Nutzdaten an dem Byte-Offset liefert, den das
 * verschränkte Spurlayout (cyl außen, head innen) in der .img-Datei vorgibt.
 *
 * @see core/peripherals/floppy_drive/img_codec.h
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
#include <cstring>

#include "core/peripherals/floppy_drive/img_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "core/peripherals/floppy_drive/disk_format.h"

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
                       / "k1520_test_img_codec.img").string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);

    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c) {
        for (uint8_t h = 0; h < nheads; ++h) {
            const TrackFormat* tf = fmt.findTrack(c, h);
            if (!tf) continue;
            for (uint8_t id = 1; id <= tf->secs_per_track; ++id) {
                // Erkennungsmuster: alle Bytes == Sektor-ID
                std::vector<uint8_t> buf(tf->bytes_per_sec, static_cast<uint8_t>(id));
                f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
        }
    }
    return path;
}

/// @brief Lädt ein .img in ein Medium (kurz).
static DiskMedium loadImg(const std::string& path, const DiskFormat& fmt) {
    DiskMedium m;
    std::string err;
    EXPECT_TRUE(ImgCodec::load(path, fmt, m, err)) << err;
    return m;
}

// ─── Gruppe 1: Geometrie ─────────────────────────────────────────────────────

TEST(ImgCodec, Geometry_KorrekteFelderSimpleFormat) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    const DiskMedium m = loadImg(path, fmt);
    const DiskGeometry g = m.geometry();
    EXPECT_EQ(g.num_cyls,  2u);
    EXPECT_EQ(g.num_heads, 1u);
    EXPECT_EQ(g.encoding,  Encoding::MFM);
    EXPECT_TRUE(g.uniform) << "Uniform sollte true sein (ein TrackFormat-Eintrag)";

    std::filesystem::remove(path);
}

/**
 * @test ImgCodec/WriteProtect_BlocktSchreiben
 * @brief Mit write_protect=true ist die Diskette nicht beschreibbar und writeTrack()
 *        scheitert.
 */
TEST(ImgCodec, WriteProtect_BlocktSchreiben) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    auto img = DiskImage::open(path, fmt, /*wp=*/true);
    ASSERT_NE(img, nullptr);
    EXPECT_FALSE(img->writable());
    EXPECT_FALSE(img->writeTrack(0, 0, img->readTrack(0, 0)))
        << "Schreiben trotz Write-Protect";

    std::filesystem::remove(path);
}

// ─── Gruppe 2: load → parseTrack bitgleich zur .img-Datei ────────────────────

/**
 * @brief Byte-Offset von Sektor (cyl, head, id) in der .img — verschränktes Layout
 *        (cyl außen, head innen), 1-basiertes id.  Ground-Truth-Referenz, unabhängig
 *        von ImgCodec::sectorOffset (deren Korrektheit hier gerade geprüft wird).
 */
static uint64_t expectedOffset(const DiskFormat& fmt, uint8_t cyl, uint8_t head, uint8_t id) {
    uint64_t off = 0;
    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();
    for (uint8_t c = 0; c < ncyls; ++c)
        for (uint8_t h = 0; h < nheads; ++h) {
            if (c == cyl && h == head)
                return off + static_cast<uint64_t>(id - 1) * fmt.findTrack(c, h)->bytes_per_sec;
            if (const TrackFormat* t = fmt.findTrack(c, h)) off += t->trackBytes();
        }
    return off;
}

TEST(ImgCodec, Load_BitgleichMitImageDatei) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    const DiskMedium m = loadImg(path, fmt);

    // Rohe Datei-Bytes als Vergleichsbasis einlesen.  Der Datenstrom lebt in
    // einem eigenen Block: Windows verweigert das remove() am Ende, solange
    // noch ein Griff auf die Datei offen ist ("used by another process").
    std::vector<uint8_t> fileBytes;
    {
        std::ifstream f(path, std::ios::binary);
        fileBytes.assign(std::istreambuf_iterator<char>(f),
                         std::istreambuf_iterator<char>());
    }
    ASSERT_EQ(fileBytes.size(), fmt.totalBytes());

    for (uint8_t cyl = 0; cyl < fmt.numCylinders(); ++cyl) {
        for (uint8_t head = 0; head < fmt.numHeads(); ++head) {
            const TrackFormat* tf = fmt.findTrack(cyl, head);
            ASSERT_NE(tf, nullptr);

            const TrackImage& track = m.track(cyl, head);
            ASSERT_FALSE(track.empty())
                << "Spur (" << (int)cyl << "," << (int)head << ") leer";

            auto parsed = TrackCodec::parseTrack(track);
            ASSERT_EQ(parsed.size(), static_cast<size_t>(tf->secs_per_track));

            for (const auto& ls : parsed) {
                EXPECT_TRUE(ls.id_crc_ok);
                EXPECT_TRUE(ls.data_crc_ok);

                const uint64_t off = expectedOffset(fmt, cyl, head, ls.id);
                ASSERT_LE(off + ls.data.size(), fileBytes.size());
                std::vector<uint8_t> expected(fileBytes.begin() + off,
                                              fileBytes.begin() + off + ls.data.size());
                EXPECT_EQ(ls.data, expected)
                    << "Nutzdaten weichen ab bei cyl=" << (int)cyl
                    << " head=" << (int)head << " id=" << (int)ls.id;
            }
        }
    }

    std::filesystem::remove(path);
}

TEST(ImgCodec, Load_ErkennungsmusterErhalten) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    const DiskMedium m = loadImg(path, fmt);
    auto parsed = TrackCodec::parseTrack(m.track(0, 0));
    ASSERT_EQ(parsed.size(), 2u);

    EXPECT_EQ(parsed[0].id, 1u);
    EXPECT_TRUE(std::all_of(parsed[0].data.begin(), parsed[0].data.end(),
                            [](uint8_t b){ return b == 0x01; }));
    EXPECT_EQ(parsed[1].id, 2u);
    EXPECT_TRUE(std::all_of(parsed[1].data.begin(), parsed[1].data.end(),
                            [](uint8_t b){ return b == 0x02; }));

    std::filesystem::remove(path);
}

// ─── Gruppe 3: save ──────────────────────────────────────────────────────────

/**
 * @test ImgCodec/SaveLoad_Roundtrip
 * @brief Medium → .img → Medium erhält alle Nutzdaten.
 */
TEST(ImgCodec, SaveLoad_Roundtrip) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    DiskMedium m = loadImg(path, fmt);

    // Ein Byte ändern und zurückschreiben.
    {
        auto secs = TrackCodec::parseTrack(m.track(1, 0));
        ASSERT_EQ(secs.size(), 2u);
        secs[1].data[7] = 0x5A;
        m.setTrack(1, 0, TrackCodec::buildTrack(secs, Encoding::MFM));
    }

    const auto out = (std::filesystem::temp_directory_path()
                      / "k1520_test_img_roundtrip.img").string();
    std::string err;
    ASSERT_TRUE(ImgCodec::save(out, fmt, m, err)) << err;
    EXPECT_EQ(std::filesystem::file_size(out), fmt.totalBytes());

    const DiskMedium back = loadImg(out, fmt);
    auto secs = TrackCodec::parseTrack(back.track(1, 0));
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[1].data[7], 0x5A);
    EXPECT_EQ(secs[0].data[0], 0x01);

    std::filesystem::remove(path);
    std::filesystem::remove(out);
}

/**
 * @test ImgCodec/Save_LehntUnformatiertesMediumAb
 * @brief Eine Leerdiskette lässt sich nicht als rohes Sektorimage speichern —
 *        `.img` kann „nicht formatiert" nicht ausdrücken.
 */
TEST(ImgCodec, Save_LehntUnformatiertesMediumAb) {
    const DiskMedium leer(2, 1, Encoding::MFM);
    const auto out = (std::filesystem::temp_directory_path()
                      / "k1520_test_img_blank.img").string();
    std::filesystem::remove(out);

    std::string err;
    EXPECT_FALSE(ImgCodec::save(out, makeSimpleFormat(), leer, err));
    EXPECT_NE(err.find("unformatiert"), std::string::npos) << err;
    EXPECT_FALSE(std::filesystem::exists(out));
}

/**
 * @test ImgCodec/Save_LehntAnhangHinterDatenCrcAb
 * @brief Ein Sektor mit Nutzdaten hinter der Daten-CRC (UDOS-Sektorkontrollblock)
 *        ist nicht als `.img` speicherbar — genau dieser Anhang ginge verloren.
 */
TEST(ImgCodec, Save_LehntAnhangHinterDatenCrcAb) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    DiskMedium m = loadImg(path, fmt);

    // Hinter die Daten-CRC des ersten Sektors einen Zeiger-Anhang schreiben.
    {
        TrackImage& t = m.mutableTrack(0, 0);
        const size_t dam = t.nextMark(0, MarkType::Data);
        ASSERT_NE(dam, SIZE_MAX);
        const size_t tail = dam + 1 + 128 + 2;   // Marke + Daten + CRC
        ASSERT_LT(tail + 4, t.bytes.size());
        t.bytes[tail + 0] = 0x12;
        t.bytes[tail + 1] = 0x34;
    }

    EXPECT_FALSE(m.trackRawCompatible(0, 0));
    EXPECT_FALSE(m.rawCompatible());

    const auto out = (std::filesystem::temp_directory_path()
                      / "k1520_test_img_tail.img").string();
    std::filesystem::remove(out);
    std::string err;
    EXPECT_FALSE(ImgCodec::save(out, fmt, m, err));
    EXPECT_NE(err.find("Spur 0/0"), std::string::npos) << err;

    std::filesystem::remove(path);
}

/**
 * @test ImgCodec/MismatchReason_MeldetAbweichendeSektorzahl
 */
TEST(ImgCodec, MismatchReason_MeldetAbweichendeSektorzahl) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    const DiskMedium m = loadImg(path, fmt);

    EXPECT_EQ(ImgCodec::mismatchReason(fmt, m), "");

    DiskFormat anders = fmt;
    anders.name = "test_anders";
    anders.tracks[0].secs_per_track = 4;
    const std::string grund = ImgCodec::mismatchReason(anders, m);
    EXPECT_NE(grund, "");
    EXPECT_NE(grund.find("Sektoren"), std::string::npos) << grund;

    std::filesystem::remove(path);
}

// ─── Gruppe 4: DiskImage::open ───────────────────────────────────────────────

TEST(DiskImageOpen, HFE_Signatur_kurzeDatei_gibtNullptr) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hxcpicfe.img").string();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write("HXCPICFE", 8);
        uint8_t pad[64] = {};
        f.write(reinterpret_cast<char*>(pad), sizeof(pad));
    }
    // Signatur → HFE-Codec; die Datei ist aber viel zu kurz → Fehler.
    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, HXCHFEV3_Signatur_gibtNullptr) {
    const auto path = (std::filesystem::temp_directory_path()
                       / "k1520_test_hxchfev3.img").string();
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

TEST(DiskImageOpen, RawOhneFmt_gibtNullptr) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    auto img  = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

TEST(DiskImageOpen, RawMitFmt_liefertGeoeffnetesImage) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);
    auto img  = DiskImage::open(path, fmt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(img->writable());
    EXPECT_TRUE(img->hasFile());
    EXPECT_EQ(img->container(), ContainerType::Img);
    ASSERT_NE(img->diskFormat(), nullptr);
    EXPECT_EQ(img->diskFormat()->name, "test_simple");
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

TEST(DiskImageOpen, RawVerfahren_KommtAusDemSpurbereich) {
    // Ein rohes .img trägt kein Verfahren in sich — es steht im DiskFormat, und zwar
    // PRO SPURBEREICH (§8.6).  Die synthetisierte Spur muss dieses Verfahren tragen.
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    auto mfm = DiskImage::open(path, fmt, false);
    ASSERT_NE(mfm, nullptr);
    EXPECT_EQ(mfm->geometry().encoding, Encoding::MFM);
    EXPECT_EQ(mfm->readTrack(0, 0).encoding, Encoding::MFM);

    for (auto& t : fmt.tracks) t.encoding = Encoding::FM;
    auto fm = DiskImage::open(path, fmt, false);
    ASSERT_NE(fm, nullptr);
    EXPECT_EQ(fm->geometry().encoding, Encoding::FM);
    EXPECT_EQ(fm->readTrack(0, 0).encoding, Encoding::FM);

    std::filesystem::remove(path);
}

/**
 * @test DiskImageOpen/FirstSectorId_VerschiebtSektorIDsUndOffsets
 * @brief Ein Spurbereich mit `first_sector: 5` liefert die IDs 5..6 und trifft dabei
 *        dieselben Byte-Offsets wie die 1-basierte Variante.
 */
TEST(DiskImageOpen, FirstSectorId_VerschiebtSektorIDsUndOffsets) {
    auto base = makeSimpleFormat();          // 2 Zyl × 1 Kopf × 2 × 128, IDs 1..2
    auto path = makeTmpImg(base);

    DiskFormat fmt = base;
    for (auto& t : fmt.tracks) t.first_sector_id = 5;

    auto img = DiskImage::open(path, fmt, /*wp=*/false);
    ASSERT_NE(img, nullptr);

    auto secs = TrackCodec::parseTrack(img->readTrack(0, 0));
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[0].id, 5);
    EXPECT_EQ(secs[1].id, 6);

    ASSERT_EQ(secs[0].data.size(), 128u);
    EXPECT_EQ(secs[0].data[0], 1);
    EXPECT_EQ(secs[1].data[0], 2);

    std::filesystem::remove(path);
}

/**
 * @test DiskImageOpen/RawMischdichte_VerfahrenJeSpur
 * @brief Ein Format mit FM-Systemspur und MFM-Datenspur liefert je Spur das richtige
 *        Verfahren (das eigentliche Ziel des YAML-Katalog-Umbaus, §8.6).
 */
TEST(DiskImageOpen, RawMischdichte_VerfahrenJeSpur) {
    DiskFormat fmt;
    fmt.name = "test_mixed";
    fmt.tracks.push_back({0, 0, 0, 0, 2, 128,  Encoding::FM,  1});   // Systemspur
    fmt.tracks.push_back({1, 1, 0, 0, 2, 128,  Encoding::MFM, 1});   // Datenspur
    auto path = makeTmpImg(fmt);

    auto img = DiskImage::open(path, fmt, false);
    ASSERT_NE(img, nullptr);

    const TrackImage& sys = img->readTrack(0, 0);
    const TrackImage& dat = img->readTrack(1, 0);
    EXPECT_EQ(sys.encoding, Encoding::FM);
    EXPECT_EQ(dat.encoding, Encoding::MFM);

    for (const TrackImage* t : {&sys, &dat}) {
        auto secs = TrackCodec::parseTrack(*t);
        ASSERT_EQ(secs.size(), 2u);
        for (const auto& s : secs) {
            EXPECT_TRUE(s.id_crc_ok);
            EXPECT_TRUE(s.data_crc_ok);
        }
    }

    std::filesystem::remove(path);
}

// ─── Gruppe 5: DiskImage::create (vorformatiert) ─────────────────────────────

TEST(DiskImageCreate, ImgMitFmt_LegtDateiInFormatGroesseAn) {
    auto fmt = makeSimpleFormat();                 // 2 Zyl × 1 Kopf × 2 × 128 = 512 B
    std::string path = (std::filesystem::temp_directory_path() / "create_test.img").string();
    std::filesystem::remove(path);

    auto img = DiskImage::create(path, fmt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(path), fmt.totalBytes());
    // Frisch angelegt → 0xE5 (leere CP/M-Sektoren).  Eigener Block, damit die
    // Datei vor dem remove() am Ende wieder zu ist (Windows, s.o.).
    {
        std::ifstream f(path, std::ios::binary);
        uint8_t b = 0; f.read(reinterpret_cast<char*>(&b), 1);
        EXPECT_EQ(b, 0xE5);
    }
    auto g = img->geometry();
    EXPECT_EQ(g.num_cyls, 2);
    EXPECT_EQ(g.num_heads, 1);

    std::filesystem::remove(path);
}

TEST(DiskImageCreate, ImgOhneFmt_gibtNullptr) {
    std::string path = (std::filesystem::temp_directory_path() / "create_nofmt.img").string();
    std::filesystem::remove(path);
    auto img = DiskImage::create(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
}

TEST(DiskImageCreate, HfeOhneFmt_gibtNullptr) {
    std::string path = (std::filesystem::temp_directory_path() / "create_nofmt.hfe").string();
    std::filesystem::remove(path);
    // create() ist der VORFORMATIERTE Weg und braucht das Sektorlayout.  Für eine
    // unformatierte Leerdiskette gibt es createBlank().
    auto img = DiskImage::create(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
    std::filesystem::remove(path);
}

// Zählt IDAM/DAM-Marken einer Spur.
static size_t countMarks(const TrackImage& t) {
    size_t n = 0;
    for (MarkType m : t.marks)
        if (m == MarkType::Id || m == MarkType::Data) ++n;
    return n;
}

// Gemeinsame Prüfung: frisch erzeugtes .hfe ist gültig formatiert (Marken vorhanden,
// parseTrack liefert die erwarteten 0xE5-Sektoren) und übersteht open().
static void checkFormattedHfe(Encoding enc) {
    auto fmt = makeSimpleFormat();  // 2 Zyl × 1 Kopf × 2 × 128
    for (auto& t : fmt.tracks) t.encoding = enc;
    std::string path = (std::filesystem::temp_directory_path() / "create_fmt.hfe").string();
    std::filesystem::remove(path);

    auto img = DiskImage::create(path, fmt, false, enc);
    ASSERT_NE(img, nullptr);
    auto g = img->geometry();
    EXPECT_EQ(g.num_cyls, 2);
    EXPECT_EQ(g.num_heads, 1);
    EXPECT_EQ(g.encoding, enc);

    const TrackImage& t = img->readTrack(1, 0);
    EXPECT_GT(countMarks(t), 0u);
    auto secs = TrackCodec::parseTrack(t);
    ASSERT_EQ(secs.size(), 2u);
    for (const auto& s : secs) {
        EXPECT_EQ(s.size, 128);
        EXPECT_TRUE(s.data_crc_ok);
        EXPECT_TRUE(s.id_crc_ok);
        ASSERT_EQ(s.data.size(), 128u);
        EXPECT_TRUE(std::all_of(s.data.begin(), s.data.end(),
                                [](uint8_t b){ return b == 0xE5; }));
    }

    // Öffnen akzeptiert die formatierte Datei.
    auto reopened = DiskImage::open(path, std::nullopt, false);
    EXPECT_NE(reopened, nullptr);

    std::filesystem::remove(path);
}

TEST(DiskImageCreate, HfeMfm_GueltigFormatiert) { checkFormattedHfe(Encoding::MFM); }
TEST(DiskImageCreate, HfeFm_GueltigFormatiert)  { checkFormattedHfe(Encoding::FM); }

/**
 * @test DiskImageCreate/HfeMischdichte_BeideVerfahrenLesbar
 * @brief Eine Mischdichte-Diskette (FM-Systemspur + MFM-Datenspuren) lässt sich als
 *        .hfe ANLEGEN und danach spurweise korrekt zurücklesen.
 */
TEST(DiskImageCreate, HfeMischdichte_BeideVerfahrenLesbar) {
    DiskFormat fmt;
    fmt.name = "test_mixed_hfe";
    fmt.tracks.push_back({0, 0, 0, 0, 4,  128,  Encoding::FM,  1});
    fmt.tracks.push_back({1, 2, 0, 0, 2, 1024,  Encoding::MFM, 1});

    const std::string path =
        (std::filesystem::temp_directory_path() / "create_mixed.hfe").string();
    std::filesystem::remove(path);

    {
        auto img = DiskImage::create(path, fmt, /*wp=*/false, fmt.predominantEncoding());
        ASSERT_NE(img, nullptr);
    }   // schließen → flush

    auto img = DiskImage::open(path, std::nullopt, /*wp=*/false);
    ASSERT_NE(img, nullptr) << "Mischdichte-HFE wurde beim Öffnen abgelehnt";

    struct Erwartung { uint8_t cyl; Encoding enc; size_t nsecs; uint16_t size; };
    const Erwartung erw[] = {
        {0, Encoding::FM,  4,  128},
        {1, Encoding::MFM, 2, 1024},
        {2, Encoding::MFM, 2, 1024},
    };

    for (const auto& e : erw) {
        const TrackImage& t = img->readTrack(e.cyl, 0);
        EXPECT_GT(countMarks(t), 0u) << "Spur " << int(e.cyl) << " ohne Adressmarken";
        EXPECT_EQ(t.encoding, e.enc)  << "Spur " << int(e.cyl) << ": falsches Verfahren";

        auto secs = TrackCodec::parseTrack(t);
        ASSERT_EQ(secs.size(), e.nsecs) << "Spur " << int(e.cyl);
        for (const auto& s : secs) {
            EXPECT_EQ(s.size, e.size);
            EXPECT_TRUE(s.id_crc_ok)   << "Spur " << int(e.cyl) << " Sektor " << int(s.id);
            EXPECT_TRUE(s.data_crc_ok) << "Spur " << int(e.cyl) << " Sektor " << int(s.id);
            EXPECT_TRUE(std::all_of(s.data.begin(), s.data.end(),
                                    [](uint8_t b){ return b == 0xE5; }));
        }
    }

    std::filesystem::remove(path);
}
