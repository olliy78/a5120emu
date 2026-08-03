/**
 * @file test_disk_image_raw.cpp
 * @brief GoogleTests für RawSectorImage und DiskImage::open.
 *
 * Getestete Komponenten:
 *   - RawSectorImage: readTrack → TrackCodec::parseTrack (Bitgleichheit mit der .img-Datei)
 *   - RawSectorImage: geometry()
 *   - DiskImage::open (HFE-Signatur → nullptr; Raw → RawSectorImage)
 *
 * Der Bitgleichheits-Vergleich beweist, dass readTrack + parseTrack für jede
 * (cyl, head, id)-Kombination exakt die Nutzdaten an dem Byte-Offset liefert, den das
 * verschränkte Spurlayout (cyl außen, head innen) in der .img-Datei vorgibt — gegen die
 * Datei als Ground-Truth verglichen.
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
#include <cstring>

#include "core/peripherals/floppy_drive/raw_sector_image.h"
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

/**
 * @test RawSectorImage/WriteProtect_BlocktSchreiben
 * @brief Mit write_protect=true ist das Image nicht beschreibbar und writeTrack() scheitert.
 *        (Ersetzt die Schreibschutz-Abdeckung des entfernten FloppyDrive-Tests.)
 */
TEST(RawSectorImage, WriteProtect_BlocktSchreiben) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    RawSectorImage img(path, fmt, /*wp=*/true);
    ASSERT_TRUE(img.isOpen());
    EXPECT_FALSE(img.writable());

    // writeTrack auf eine (egal welche) Spur muss abgelehnt werden.
    TrackImage track = img.readTrack(0, 0);
    EXPECT_FALSE(img.writeTrack(0, 0, track)) << "Schreiben trotz Write-Protect";

    std::filesystem::remove(path);
}

// ─── Gruppe 2: readTrack → parseTrack bitgleich zur .img-Datei ───────────────

/**
 * @brief Byte-Offset von Sektor (cyl, head, id) in der .img — verschränktes Layout
 *        (cyl außen, head innen), 1-basiertes id.  Ground-Truth-Referenz, unabhängig
 *        von RawSectorImage::sectorOffset (deren Korrektheit hier gerade geprüft wird).
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

/**
 * Kern-Test: RawSectorImage::readTrack(cyl, head) → TrackCodec::parseTrack liefert für
 * jede (cyl, head, id)-Kombination exakt die Nutzdaten am Byte-Offset, den das
 * verschränkte Spurlayout in der .img-Datei vorgibt (Datei = Ground-Truth).
 */
TEST(RawSectorImage, ReadTrack_BitgleichMitImageDatei) {
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    RawSectorImage newImg(path, fmt, /*wp=*/false);
    ASSERT_TRUE(newImg.isOpen());

    // Rohe Datei-Bytes als Vergleichsbasis einlesen.
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> fileBytes{std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>()};
    ASSERT_EQ(fileBytes.size(), fmt.totalBytes());

    const uint8_t ncyls  = fmt.numCylinders();
    const uint8_t nheads = fmt.numHeads();

    for (uint8_t cyl = 0; cyl < ncyls; ++cyl) {
        for (uint8_t head = 0; head < nheads; ++head) {
            const TrackFormat* tf = fmt.findTrack(cyl, head);
            ASSERT_NE(tf, nullptr);

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

                // Erwartete Nutzdaten direkt aus der Datei am verschränkten Offset.
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

TEST(DiskImageOpen, RawVerfahren_KommtAusDemSpurbereich) {
    // Ein rohes .img trägt kein Verfahren in sich — es steht im DiskFormat, und zwar
    // PRO SPURBEREICH (§8.6).  Die synthetisierte Spur muss dieses Verfahren tragen.
    auto fmt  = makeSimpleFormat();
    auto path = makeTmpImg(fmt);

    auto mfm = DiskImage::open(path, fmt, false);                 // Default MFM
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
 *
 * Das Feld ist neu (§8.6.1) und wird vom ausgelieferten Katalog noch NICHT benutzt —
 * ohne Test bräche es unbemerkt, sobald jemand es einsetzt.  Betroffen sind die
 * Bereichsprüfung und die Offsetrechnung in `RawSectorImage::sectorOffset` sowie die
 * ID-Vergabe in `readTrack`.
 * @par Kriterium  Sektor-IDs sind 5 und 6; der Inhalt entspricht dem 1. bzw. 2. Sektor
 *                 der Datei (Erkennungsmuster 1 und 2), nicht dem 5. und 6.
 */
TEST(DiskImageOpen, FirstSectorId_VerschiebtSektorIDsUndOffsets) {
    // Datei mit dem 1-basierten Muster anlegen (Sektorinhalt == laufende Nummer).
    auto base = makeSimpleFormat();          // 2 Zyl × 1 Kopf × 2 × 128, IDs 1..2
    auto path = makeTmpImg(base);

    // Gleiche Geometrie, aber IDs beginnen bei 5.
    DiskFormat fmt = base;
    for (auto& t : fmt.tracks) t.first_sector_id = 5;

    auto img = DiskImage::open(path, fmt, /*wp=*/false);
    ASSERT_NE(img, nullptr);

    auto secs = TrackCodec::parseTrack(img->readTrack(0, 0));
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[0].id, 5);
    EXPECT_EQ(secs[1].id, 6);

    // Offsets sind unverändert: Sektor 5 liegt am Dateianfang (Muster 1), 6 dahinter (2).
    ASSERT_EQ(secs[0].data.size(), 128u);
    EXPECT_EQ(secs[0].data[0], 1);
    EXPECT_EQ(secs[1].data[0], 2);

    std::filesystem::remove(path);
}

/**
 * @test DiskImageOpen/RawMischdichte_VerfahrenJeSpur
 * @brief Ein Format mit FM-Systemspur und MFM-Datenspur liefert je Spur das richtige
 *        Verfahren (das eigentliche Ziel des YAML-Katalog-Umbaus, §8.6).
 * @par Kriterium  readTrack(0,0) ist FM, readTrack(1,0) ist MFM; beide mit gültigen CRCs.
 */
TEST(DiskImageOpen, RawMischdichte_VerfahrenJeSpur) {
    DiskFormat fmt;
    fmt.name = "test_mixed";
    fmt.tracks.push_back({0, 0, 0, 0, 2, 128,  Encoding::FM,  1});   // Systemspur
    fmt.tracks.push_back({1, 1, 0, 0, 2, 128,  Encoding::MFM, 1});   // Datenspur
    auto path = makeTmpImg(fmt);

    auto img = DiskImage::open(path, fmt, false);
    ASSERT_NE(img, nullptr);

    TrackImage sys = img->readTrack(0, 0);
    TrackImage dat = img->readTrack(1, 0);
    EXPECT_EQ(sys.encoding, Encoding::FM);
    EXPECT_EQ(dat.encoding, Encoding::MFM);

    // Beide Spuren sind trotz unterschiedlicher Verfahren gültig lesbar.
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

// ─── DiskImage::create ───────────────────────────────────────────────────────

TEST(DiskImageCreate, ImgMitFmt_LegtDateiInFormatGroesseAn) {
    auto fmt = makeSimpleFormat();                 // 2 Zyl × 1 Kopf × 2 × 128 = 512 B
    std::string path = std::filesystem::temp_directory_path() / "create_test.img";
    std::filesystem::remove(path);

    auto img = DiskImage::create(path, fmt, false);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(path), fmt.totalBytes());
    // Frisch angelegt → 0xE5 (leere CP/M-Sektoren).
    std::ifstream f(path, std::ios::binary);
    uint8_t b = 0; f.read(reinterpret_cast<char*>(&b), 1);
    EXPECT_EQ(b, 0xE5);
    auto g = img->geometry();
    EXPECT_EQ(g.num_cyls, 2);
    EXPECT_EQ(g.num_heads, 1);

    std::filesystem::remove(path);
}

TEST(DiskImageCreate, ImgOhneFmt_gibtNullptr) {
    std::string path = std::filesystem::temp_directory_path() / "create_nofmt.img";
    std::filesystem::remove(path);
    auto img = DiskImage::create(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);
}

TEST(DiskImageCreate, HfeOhneFmt_gibtNullptr) {
    std::string path = std::filesystem::temp_directory_path() / "create_nofmt.hfe";
    std::filesystem::remove(path);
    // .hfe braucht jetzt ein Format (Geometrie) → nullopt gibt nullptr.
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
    // Das Verfahren ist Eigenschaft des SPURBEREICHS (DiskFormat), nicht des
    // Aufrufs — create() baut jede Spur in ihrem eigenen Verfahren (§8.6).
    for (auto& t : fmt.tracks) t.encoding = enc;
    std::string path = std::filesystem::temp_directory_path() / "create_fmt.hfe";
    std::filesystem::remove(path);

    auto img = DiskImage::create(path, fmt, false, enc);
    ASSERT_NE(img, nullptr);
    auto g = img->geometry();
    EXPECT_EQ(g.num_cyls, 2);
    EXPECT_EQ(g.num_heads, 1);
    EXPECT_EQ(g.encoding, enc);

    // Spur trägt echte Marken; parseTrack liefert 2 Sektoren à 128 B = 0xE5, CRC ok.
    TrackImage t = img->readTrack(1, 0);
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

    // Öffnen (Modus 1) akzeptiert die formatierte Datei.
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
 *
 * Das ist der Kern des YAML-Katalog-Umbaus (§8.6) und zugleich das dort als **R3**
 * dokumentierte Risiko: HFE v1 trägt nur EIN Verfahren im Header, `BitCodec` kodiert
 * FM und MFM beide mit 16 Zellen/Byte, und die Spurkapazität (`side_len`) wird über
 * ALLE Spuren dimensioniert.  Die abweichende Spur hängt an der Pro-Spur-Erkennung
 * in `HfeImage::readTrack`.  Geprüft wird über den vollen Weg create → schließen →
 * `DiskImage::open` → `readTrack`, damit auch der Mount-Guard (`hasFormattedData`)
 * mitläuft.
 * @par Kriterium  Spur 0 kommt als FM zurück, Spur 1-2 als MFM; alle Sektoren mit
 *                 gültiger ID- und Daten-CRC und Nutzdaten 0xE5.
 */
TEST(DiskImageCreate, HfeMischdichte_BeideVerfahrenLesbar) {
    DiskFormat fmt;
    fmt.name = "test_mixed_hfe";
    // Systemspur klein/FM, Datenspuren groß/MFM — wie 8″-System-34.
    fmt.tracks.push_back({0, 0, 0, 0, 4,  128,  Encoding::FM,  1});
    fmt.tracks.push_back({1, 2, 0, 0, 2, 1024,  Encoding::MFM, 1});

    const std::string path =
        (std::filesystem::temp_directory_path() / "create_mixed.hfe").string();
    std::filesystem::remove(path);

    {
        auto img = DiskImage::create(path, fmt, /*wp=*/false, fmt.predominantEncoding());
        ASSERT_NE(img, nullptr);
    }   // schließen → flush

    // Wiederöffnen über die Fabrik (self-describing, kein DiskFormat nötig).
    auto img = DiskImage::open(path, std::nullopt, /*wp=*/false);
    ASSERT_NE(img, nullptr) << "Mischdichte-HFE wurde beim Öffnen abgelehnt";

    struct Erwartung { uint8_t cyl; Encoding enc; size_t nsecs; uint16_t size; };
    const Erwartung erw[] = {
        {0, Encoding::FM,  4,  128},
        {1, Encoding::MFM, 2, 1024},
        {2, Encoding::MFM, 2, 1024},
    };

    for (const auto& e : erw) {
        TrackImage t = img->readTrack(e.cyl, 0);
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

// Ein gap-leeres (unformatiertes) .hfe muss von open() abgelehnt werden (kein Hänger).
TEST(DiskImageCreate, OpenLehntGapLeeresHfeAb) {
    std::string path = std::filesystem::temp_directory_path() / "gap_blank.hfe";
    std::filesystem::remove(path);

    // Minimales HFE v1: 1 Zyl, 1 Kopf, MFM; Spurdaten komplett Gap (0x88).
    std::vector<uint8_t> file(512 * 3, 0x00);
    std::memcpy(file.data(), "HXCPICFE", 8);
    file[0x09] = 1;      // num_cyls
    file[0x0A] = 1;      // num_heads
    file[0x0B] = 0;      // MFM
    file[0x12] = 1;      // track_list_block = 1
    file[0x14] = 0xFF;   // write_allowed
    // LUT bei Block 1: offset_blocks=2, len_bytes=512.
    file[512 + 0] = 2; file[512 + 1] = 0;
    file[512 + 2] = 0; file[512 + 3] = 2;
    for (size_t i = 1024; i < file.size(); ++i) file[i] = 0x88;  // Gap
    { std::ofstream f(path, std::ios::binary | std::ios::trunc);
      f.write(reinterpret_cast<const char*>(file.data()), file.size()); }

    auto img = DiskImage::open(path, std::nullopt, false);
    EXPECT_EQ(img, nullptr);  // markenlos → abgelehnt statt Hänger

    std::filesystem::remove(path);
}
