/**
 * @file test_cqm_codec.cpp
 * @brief GoogleTests fuer CqmCodec (CopyQM-Abbild, nur Lesen).
 *
 * Schwerpunkte:
 *   - synthetische Mini-CQMs (Kopf + RLE), die der Test selbst baut: Sektorinhalt,
 *     Wiederholung ueber die Spurgrenze, nicht gespeicherte Zylinder, Kommentar
 *   - Fehlerfaelle: kaputte Kennung, kaputtes Kopfpruefbyte, abgeschnittene Datei,
 *     falscher Pruefwert, Datenstrom laenger als die Geometrie
 *   - der Pruefwert mit CopyQMs `& 0x3F`-Tabellenfehler
 *   - Container-Erkennung (ImageCodec) und Verhalten in DiskImage: nie zurueckschreiben,
 *     saveAs() in .img/.hfe/.dmk geht
 *   - Gegenprobe an echten Abbildern: nur mit Umgebungsvariable `K1520_CQM_DIR`
 *     (Ordner mit *.cqm und den rohen *.tar-Gegenstuecken), sonst uebersprungen
 *
 * @see core/peripherals/floppy_drive/cqm_codec.h
 * @see doc/design/33_cqm.md, doc/merkposten/cqm.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/peripherals/floppy_drive/cqm_codec.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/peripherals/floppy_drive/image_codec.h"
#include "core/peripherals/floppy_drive/img_codec.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tests/support/temp_path.h"

namespace {

namespace fs = std::filesystem;

// ─── Baukasten fuer synthetische CQMs ────────────────────────────────────────

struct Geo {
    uint16_t ssz  = 256;
    uint16_t spt  = 4;
    uint16_t heads = 2;
    uint8_t  used = 3;
    uint8_t  total = 3;
    uint8_t  secbase = 0;
    uint8_t  density = 0;
    std::string comment;
};

void put16(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    v[o] = static_cast<uint8_t>(x); v[o + 1] = static_cast<uint8_t>(x >> 8);
}

size_t rawSize(const Geo& g) {
    return static_cast<size_t>(g.used) * g.heads * g.spt * g.ssz;
}

/// Rohbyte-Block (n > 0) und Wiederholungsblock (n < 0).
void lit(std::vector<uint8_t>& s, const std::vector<uint8_t>& b) {
    s.push_back(static_cast<uint8_t>(b.size())); s.push_back(static_cast<uint8_t>(b.size() >> 8));
    s.insert(s.end(), b.begin(), b.end());
}
void rep(std::vector<uint8_t>& s, int count, uint8_t value) {
    const int16_t n = static_cast<int16_t>(-count);
    s.push_back(static_cast<uint8_t>(n)); s.push_back(static_cast<uint8_t>(n >> 8));
    s.push_back(value);
}

/// Packt @p raw mit einem einfachen RLE (Folgen ab 4 gleichen Bytes werden Wiederholung).
std::vector<uint8_t> pack(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> s, rohl;
    auto flush = [&] { if (!rohl.empty()) { lit(s, rohl); rohl.clear(); } };
    for (size_t i = 0; i < raw.size();) {
        size_t j = i;
        while (j < raw.size() && raw[j] == raw[i] && j - i < 30000) ++j;
        if (j - i >= 4) { flush(); rep(s, static_cast<int>(j - i), raw[i]); i = j; }
        else            { rohl.push_back(raw[i]); ++i; if (rohl.size() >= 30000) flush(); }
    }
    flush();
    return s;
}

/// Kopf (133 Byte) + Kommentar + @p packed.  @p crc: nullopt = richtigen Wert aus @p raw.
std::vector<uint8_t> baue(const Geo& g, const std::vector<uint8_t>& packed,
                          const std::vector<uint8_t>& raw, int64_t crcOverride = -1) {
    std::vector<uint8_t> f(CqmCodec::kHeaderSize, 0);
    f[0] = 'C'; f[1] = 'Q'; f[2] = 0x14;
    put16(f, 3, g.ssz);
    put16(f, 0x10, g.spt);
    put16(f, 0x12, g.heads);
    const std::string d = "Testabbild";
    std::copy(d.begin(), d.end(), f.begin() + 0x1C);
    f[0x58] = 1; f[0x59] = g.density; f[0x5A] = g.used; f[0x5B] = g.total;
    const uint32_t crc = crcOverride >= 0 ? static_cast<uint32_t>(crcOverride)
                                          : CqmCodec::crc(raw.data(), raw.size());
    for (int i = 0; i < 4; ++i) f[0x5C + i] = static_cast<uint8_t>(crc >> (8 * i));
    put16(f, 0x6F, static_cast<uint32_t>(g.comment.size()));
    f[0x71] = g.secbase;
    uint8_t summe = 0;
    for (size_t i = 0; i < 0x84; ++i) summe = static_cast<uint8_t>(summe + f[i]);
    f[0x84] = static_cast<uint8_t>(-summe);
    f.insert(f.end(), g.comment.begin(), g.comment.end());
    f.insert(f.end(), packed.begin(), packed.end());
    return f;
}

/// Rohdaten mit pro Sektor erkennbarem Inhalt (laufende Sektornummer + Offset).
std::vector<uint8_t> muster(const Geo& g) {
    std::vector<uint8_t> raw(rawSize(g));
    for (size_t i = 0; i < raw.size(); ++i) {
        const size_t sektor = i / g.ssz;
        raw[i] = static_cast<uint8_t>((sektor * 7 + (i % g.ssz) / 3) & 0xFF);
    }
    return raw;
}

std::vector<uint8_t> standard(const Geo& g, std::vector<uint8_t>* rawOut = nullptr) {
    auto raw = muster(g);
    if (rawOut) *rawOut = raw;
    return baue(g, pack(raw), raw);
}

void schreibe(const std::string& path, const std::vector<uint8_t>& d) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

std::vector<uint8_t> lies(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

/// Alle Sektordaten eines Mediums in Reihenfolge Zylinder → Kopf → Sektor-ID.
std::vector<uint8_t> sektorStrom(const DiskMedium& m, uint8_t cyls) {
    std::vector<uint8_t> out;
    for (uint8_t c = 0; c < cyls; ++c)
        for (uint8_t h = 0; h < m.numHeads(); ++h) {
            auto secs = TrackCodec::parseTrack(m.peek(c, h));
            std::sort(secs.begin(), secs.end(),
                      [](const LogicalSector& a, const LogicalSector& b) { return a.id < b.id; });
            for (const auto& s : secs) {
                EXPECT_TRUE(s.id_crc_ok);
                EXPECT_TRUE(s.data_crc_ok);
                out.insert(out.end(), s.data.begin(), s.data.end());
            }
        }
    return out;
}

}  // namespace

// ─── Pruefwert ───────────────────────────────────────────────────────────────

/// @test CopyQM-Pruefwert: CRC-32 mit Tabellenfehler (`& 0x3F`), Startwert 0, kein Endxor
TEST(CqmCodec, PruefwertHatDenTabellenfehler) {
    const char* s = "123456789";
    // Gegenwert mit dem normalen CRC-32 waere 0xCBF43926 (Startwert ~0, Endxor) bzw.
    // mit Startwert 0 und ohne Endxor etwas anderes; hier der Wert des CopyQM-Verfahrens
    // (unabhaengig in Python nachgerechnet und an 142 echten Abbildern bestaetigt).
    EXPECT_EQ(CqmCodec::crc(reinterpret_cast<const uint8_t*>(s), 9), 0x1A27FDDFu);
    std::vector<uint8_t> b(256);
    for (int i = 0; i < 256; ++i) b[i] = static_cast<uint8_t>(i);
    EXPECT_EQ(CqmCodec::crc(b.data(), b.size()), 0xEE261DF7u);
    EXPECT_EQ(CqmCodec::crc(nullptr, 0), 0u);
}

// ─── Laden ───────────────────────────────────────────────────────────────────

/// @test Sektorinhalt, ID-Folge, CRCs und Geometrie eines synthetischen Abbilds
TEST(CqmCodec, LadetSektorenInReihenfolgeZylinderKopfSektor) {
    Geo g;
    std::vector<uint8_t> raw;
    const auto f = standard(g, &raw);
    DiskMedium m;
    CqmCodec::SourceInfo si;
    std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, &si, err)) << err;
    EXPECT_EQ(m.numCylinders(), 3);
    EXPECT_EQ(m.numHeads(), 2);
    EXPECT_EQ(si.sector_size, 256);
    EXPECT_EQ(si.secs_per_track, 4);
    EXPECT_TRUE(si.crc_checked);
    EXPECT_EQ(si.crc_stored, si.crc_computed);
    EXPECT_EQ(sektorStrom(m, 3), raw);

    // IDs 1..4, Zylinder-/Kopfnummer im ID-Feld
    const auto secs = TrackCodec::parseTrack(m.peek(2, 1));
    ASSERT_EQ(secs.size(), 4u);
    for (size_t i = 0; i < secs.size(); ++i) {
        EXPECT_EQ(secs[i].id, i + 1);
        EXPECT_EQ(secs[i].cyl, 2);
        EXPECT_EQ(secs[i].head, 1);
    }
    EXPECT_FALSE(m.dirty());
}

/// @test Erste Sektor-ID kommt aus Kopfbyte 0x71 (+1)
TEST(CqmCodec, ErsteSektorIdAusDemKopf) {
    Geo g; g.secbase = 4;
    DiskMedium m; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(standard(g), m, nullptr, err)) << err;
    const auto secs = TrackCodec::parseTrack(m.peek(0, 0));
    ASSERT_EQ(secs.size(), 4u);
    EXPECT_EQ(secs.front().id, 5);
    EXPECT_EQ(secs.back().id, 8);
}

/// @test Ein Wiederholungsblock darf ueber Sektor- und Spurgrenzen laufen
TEST(CqmCodec, WiederholungLaeuftUeberDieSpurgrenze) {
    Geo g;                     // Spur = 4 * 256 = 1024 B, 6 Spuren = 6144 B
    std::vector<uint8_t> raw(rawSize(g), 0x11);
    // 1500 x 0x55 beginnt mitten in Spur 0 (Offset 700) und endet in Spur 1.
    std::fill(raw.begin() + 700, raw.begin() + 2200, 0x55);
    std::vector<uint8_t> s;
    rep(s, 700, 0x11);
    rep(s, 1500, 0x55);
    rep(s, static_cast<int>(raw.size()) - 2200, 0x11);
    DiskMedium m; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(baue(g, s, raw), m, nullptr, err)) << err;
    EXPECT_EQ(sektorStrom(m, 3), raw);
}

/// @test Rohblock ueber die Spurgrenze und ein Nullblock (n = 0) ohne Wirkung
TEST(CqmCodec, RohblockUndNullblock) {
    Geo g; g.used = g.total = 1; g.heads = 1; g.spt = 2;   // 512 B
    std::vector<uint8_t> raw(512);
    for (int i = 0; i < 512; ++i) raw[i] = static_cast<uint8_t>(i * 5);
    std::vector<uint8_t> s;
    s.push_back(0); s.push_back(0);                                   // Nullblock
    lit(s, std::vector<uint8_t>(raw.begin(), raw.begin() + 300));
    s.push_back(0); s.push_back(0);
    lit(s, std::vector<uint8_t>(raw.begin() + 300, raw.end()));
    DiskMedium m; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(baue(g, s, raw), m, nullptr, err)) << err;
    EXPECT_EQ(sektorStrom(m, 1), raw);
}

/// @test Kommentar und Bezeichnung landen in SourceInfo; Etikett "** NONE **" wird leer
TEST(CqmCodec, KommentarUndBezeichnung) {
    Geo g; g.comment = "Hallo Welt";
    auto f = standard(g);
    DiskMedium m; CqmCodec::SourceInfo si; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, &si, err)) << err;
    EXPECT_EQ(si.comment, "Hallo Welt");
    EXPECT_EQ(si.description, "Testabbild");
    EXPECT_TRUE(si.label.empty());

    // Etikett "** NONE ** " (elf Byte, mit Leerzeichen, ohne NUL) gilt ebenfalls als leer.
    const std::string e = "** NONE ** ";
    std::copy(e.begin(), e.end(), f.begin() + 0x60);
    uint8_t summe = 0;
    for (size_t i = 0; i < 0x84; ++i) summe = static_cast<uint8_t>(summe + f[i]);
    f[0x84] = static_cast<uint8_t>(-summe);
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, &si, err)) << err;
    EXPECT_TRUE(si.label.empty());
}

/// @test Zylinder hinter den gespeicherten sind leer formatiert (0xF6), nicht unformatiert
TEST(CqmCodec, NichtGespeicherteZylinderSindLeerFormatiert) {
    Geo g; g.used = 2; g.total = 4;
    std::vector<uint8_t> raw;
    const auto f = standard(g, &raw);
    DiskMedium m; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, nullptr, err)) << err;
    ASSERT_EQ(m.numCylinders(), 4);
    for (uint8_t c = 2; c < 4; ++c)
        for (uint8_t h = 0; h < 2; ++h) {
            const auto secs = TrackCodec::parseTrack(m.peek(c, h));
            ASSERT_EQ(secs.size(), 4u);
            for (const auto& s : secs) {
                EXPECT_TRUE(s.data_crc_ok);
                EXPECT_EQ(s.data, std::vector<uint8_t>(256, 0xF6));
            }
        }
}

/// @test Pruefwert 0 (alte CopyQM-Versionen) wird nicht geprueft
TEST(CqmCodec, PruefwertNullWirdNichtGeprueft) {
    Geo g; std::vector<uint8_t> raw;
    const auto roh = muster(g);
    const auto f = baue(g, pack(roh), roh, 0);
    DiskMedium m; CqmCodec::SourceInfo si; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, &si, err)) << err;
    EXPECT_FALSE(si.crc_checked);
}

/// @test Bytes hinter dem Datenstrom werden toleriert und gezaehlt
TEST(CqmCodec, ReststueckHinterDemStromWirdGemeldet) {
    Geo g;
    auto f = standard(g);
    f.push_back(0x1A); f.push_back(0x00);
    DiskMedium m; CqmCodec::SourceInfo si; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(f, m, &si, err)) << err;
    EXPECT_EQ(si.trailing_bytes, 2u);
}

// ─── Fehlerfaelle ────────────────────────────────────────────────────────────

/// @test Kaputte Kennung
TEST(CqmCodec, KaputteKennungWirdAbgelehnt) {
    auto f = standard(Geo{});
    f[1] = 'X';
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(f, m, nullptr, err));
    EXPECT_NE(err.find("Kennung"), std::string::npos) << err;
    EXPECT_FALSE(CqmCodec::isCqm(f.data(), f.size()));
}

/// @test Kopfpruefbyte stimmt nicht (ein Kopfbyte veraendert)
TEST(CqmCodec, KaputtesKopfpruefbyteWirdAbgelehnt) {
    auto f = standard(Geo{});
    f[0x10] ^= 0x01;
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(f, m, nullptr, err));
    EXPECT_NE(err.find("Kopfpruefbyte"), std::string::npos) << err;
}

/// @test Zu kurz fuer den Kopf
TEST(CqmCodec, KopfAbgeschnitten) {
    auto f = standard(Geo{});
    f.resize(100);
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(f, m, nullptr, err));
}

/// @test Abgeschnittene Datei: im Rohblock, im Wiederholungsbyte, im Kommentar, vor Soll
TEST(CqmCodec, AbgeschnitteneDateiWirdAbgelehnt) {
    Geo g; g.comment = "Kommentar";
    const auto f = standard(g);
    // 1) mitten im Datenstrom
    for (size_t cut : {f.size() - 1, f.size() / 2, CqmCodec::kHeaderSize + g.comment.size() + 1,
                       CqmCodec::kHeaderSize + g.comment.size()}) {
        auto k = f; k.resize(cut);
        DiskMedium m; std::string err;
        EXPECT_FALSE(CqmCodec::loadBuffer(k, m, nullptr, err)) << "Schnitt bei " << cut;
        EXPECT_NE(err.find("abgeschnitten"), std::string::npos) << err;
    }
    // 2) Kommentar reicht ueber das Dateiende
    auto k = f; k.resize(CqmCodec::kHeaderSize + 3);
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(k, m, nullptr, err));
    EXPECT_NE(err.find("Kommentar"), std::string::npos) << err;
}

/// @test Wiederholungsblock ohne sein Byte am Dateiende
TEST(CqmCodec, WiederholungsblockOhneByte) {
    Geo g; g.used = g.total = 1; g.heads = 1; g.spt = 1;   // 256 B
    std::vector<uint8_t> raw(256, 0x22), s;
    rep(s, 256, 0x22);
    auto f = baue(g, s, raw);
    f.pop_back();                                          // das Wiederholungsbyte fehlt
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(f, m, nullptr, err));
    EXPECT_NE(err.find("abgeschnitten"), std::string::npos) << err;
}

/// @test Falscher Pruefwert: Laden scheitert, SourceInfo meldet beide Werte
TEST(CqmCodec, FalscherPruefwertWirdAbgelehnt) {
    Geo g;
    const auto raw = muster(g);
    const uint32_t richtig = CqmCodec::crc(raw.data(), raw.size());
    const auto f = baue(g, pack(raw), raw, static_cast<int64_t>(richtig ^ 0x00010000u));
    DiskMedium m; CqmCodec::SourceInfo si; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(f, m, &si, err));
    EXPECT_NE(err.find("Pruefwert"), std::string::npos) << err;
    EXPECT_EQ(si.crc_computed, richtig);
    EXPECT_NE(si.crc_stored, si.crc_computed);
}

/// @test Ein Datenbyte kippt (Strom bleibt gueltig): der Pruefwert faengt es — ausser in den
///       Bits 6 und 7, die der CopyQM-Tabellenfehler gar nicht erfasst (`& 0x3F`).
TEST(CqmCodec, KippenderDatenbyteFaelltAmPruefwertAuf) {
    Geo g; std::vector<uint8_t> raw;
    const auto f = standard(g, &raw);
    const size_t erstes = CqmCodec::kHeaderSize + 2 + 5;   // im ersten Rohblock (Sektor 0)
    ASSERT_LT(f[CqmCodec::kHeaderSize + 1], 0x80);          // positiver Zaehler = Rohblock
    for (uint8_t bit : {0x01, 0x02, 0x20}) {
        auto k = f; k[erstes] ^= bit;
        DiskMedium m; std::string err;
        EXPECT_FALSE(CqmCodec::loadBuffer(k, m, nullptr, err)) << "Bit " << int(bit);
    }
    for (uint8_t bit : {0x40, 0x80}) {
        auto k = f; k[erstes] ^= bit;
        DiskMedium m; std::string err;
        EXPECT_TRUE(CqmCodec::loadBuffer(k, m, nullptr, err)) << "Bit " << int(bit) << err;
    }
}

/// @test Datenstrom reicht ueber die Geometrie des Kopfs hinaus (Block ueberragt)
TEST(CqmCodec, StromLaengerAlsGeometrie) {
    Geo g; g.used = g.total = 1; g.heads = 1; g.spt = 1;   // 256 B
    std::vector<uint8_t> raw(256, 0x33), s;
    rep(s, 300, 0x33);
    DiskMedium m; std::string err;
    EXPECT_FALSE(CqmCodec::loadBuffer(baue(g, s, raw), m, nullptr, err));
}

/// @test Unplausible Geometrien im Kopf
TEST(CqmCodec, UnplausibleGeometrie) {
    for (int fall = 0; fall < 4; ++fall) {
        Geo g;
        if (fall == 0) g.ssz = 300;
        if (fall == 1) g.spt = 0;
        if (fall == 2) g.heads = 3;
        if (fall == 3) g.used = 0;
        const std::vector<uint8_t> leer;
        DiskMedium m; std::string err;
        EXPECT_FALSE(CqmCodec::loadBuffer(baue(g, leer, leer), m, nullptr, err)) << fall;
    }
}

/// @test save() meldet einen klaren Fehler
TEST(CqmCodec, SchreibenGibtEsNicht) {
    DiskMedium m(2, 2, Encoding::MFM);
    std::string err;
    EXPECT_FALSE(CqmCodec::save(k1520test::tempPath("k1520_test_cqm_save.cqm"), m, err));
    EXPECT_NE(err.find("nur gelesen"), std::string::npos) << err;
}

// ─── Container-Erkennung ─────────────────────────────────────────────────────

/// @test Endung, Kennung (Vorrang vor der Endung), Namen
TEST(CqmCodec, ImageCodecErkenntUndBenennt) {
    EXPECT_EQ(ImageCodec::fromExtension("x.cqm"), ContainerType::Cqm);
    EXPECT_EQ(ImageCodec::fromExtension("X.CQM"), ContainerType::Cqm);
    EXPECT_STREQ(ImageCodec::name(ContainerType::Cqm), "cqm");
    EXPECT_STREQ(ImageCodec::extension(ContainerType::Cqm), ".cqm");
    EXPECT_TRUE(ImageCodec::selfDescribing(ContainerType::Cqm));
    EXPECT_FALSE(ImageCodec::needsDiskFormat(ContainerType::Cqm));
    EXPECT_FALSE(ImageCodec::writable(ContainerType::Cqm));
    EXPECT_TRUE(ImageCodec::writable(ContainerType::Hfe));

    // Die Kennung gewinnt gegen eine falsche Endung.
    const std::string p = k1520test::tempPath("k1520_test_cqm_detect.bin");
    schreibe(p, standard(Geo{}));
    EXPECT_EQ(ImageCodec::detect(p), ContainerType::Cqm);
    // Eine .cqm ohne Kennung ist KEIN CopyQM (faellt auf .img zurueck, Laden scheitert dort).
    auto kaputt = standard(Geo{}); kaputt[0] = 'X';
    schreibe(p, kaputt);
    EXPECT_NE(ImageCodec::detect(p), ContainerType::Cqm);
    fs::remove(p);

    DiskMedium m; std::string err;
    const std::string q = k1520test::tempPath("k1520_test_cqm_load.cqm");
    schreibe(q, standard(Geo{}));
    EXPECT_TRUE(ImageCodec::load(q, ContainerType::Cqm, nullptr, m, err)) << err;
    EXPECT_FALSE(ImageCodec::save(q, ContainerType::Cqm, nullptr, m, err));
    fs::remove(q);
}

// ─── DiskImage: nur lesbar ───────────────────────────────────────────────────

/// @test DiskImage oeffnet .cqm, schreibt nie zurueck; saveAs() in .img/.hfe/.dmk geht
TEST(CqmDiskImage, SchreibtNieInDieCqmZurueck) {
    Geo g; g.comment = "Probe";
    std::vector<uint8_t> raw;
    const auto datei = standard(g, &raw);
    const std::string p = k1520test::tempPath("k1520_test_cqm_di.cqm");
    schreibe(p, datei);

    {
        auto img = DiskImage::open(p, std::nullopt, /*write_protect=*/false);
        ASSERT_TRUE(img);
        EXPECT_EQ(img->container(), ContainerType::Cqm);
        EXPECT_FALSE(img->bindingWritable());
        ASSERT_NE(img->cqmInfo(), nullptr);
        EXPECT_EQ(img->cqmInfo()->comment, "Probe");

        // Der Gast schreibt (Medium wird schmutzig) — Datei bleibt unberuehrt.
        auto t = img->readTrack(0, 0);
        t.bytes[t.bytes.size() / 2] ^= 0xFF;
        ASSERT_TRUE(img->writeTrack(0, 0, t));
        EXPECT_TRUE(img->medium().dirty());
        EXPECT_FALSE(img->autoFlush(1));
        EXPECT_FALSE(img->autoFlush(1 + kAutoFlushDelayCycles + 10));
        EXPECT_FALSE(img->flush());
        EXPECT_EQ(lies(p), datei);
    }   // Destruktor ruft flush() — auch dort darf nichts geschrieben werden
    EXPECT_EQ(lies(p), datei);

    // Umspeichern in andere Container geht und bindet um.
    auto img = DiskImage::open(p, std::nullopt, true);
    ASSERT_TRUE(img);
    const std::string hfe = k1520test::tempPath("k1520_test_cqm_di.hfe");
    const std::string dmk = k1520test::tempPath("k1520_test_cqm_di.dmk");
    ASSERT_TRUE(img->exportTo(hfe, std::nullopt)) << img->lastError();
    ASSERT_TRUE(img->exportTo(dmk, std::nullopt)) << img->lastError();
    EXPECT_FALSE(img->exportTo(p + ".neu.cqm", std::nullopt));
    EXPECT_FALSE(img->saveAs(p + ".neu.cqm", std::nullopt));
    EXPECT_EQ(img->container(), ContainerType::Cqm);   // gescheitertes saveAs bindet nicht um
    ASSERT_TRUE(img->saveAs(hfe, std::nullopt)) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Hfe);
    EXPECT_TRUE(img->bindingWritable());
    EXPECT_EQ(img->cqmInfo(), nullptr);

    auto zurueck = DiskImage::open(hfe, std::nullopt, true);
    ASSERT_TRUE(zurueck);
    EXPECT_EQ(sektorStrom(zurueck->medium(), 3), raw);

    // .img braucht ein Format; mit dem passenden Katalogformat nicht Teil dieses Tests —
    // die Gleichheit zu .img prueft der Echtdatenvergleich (K1520_CQM_DIR).
    fs::remove(p); fs::remove(hfe); fs::remove(dmk);
}

/// @test Geoeffnet mit write_protect=true bleibt es dabei
TEST(CqmDiskImage, SchreibschutzBleibtErhalten) {
    const std::string p = k1520test::tempPath("k1520_test_cqm_wp.cqm");
    schreibe(p, standard(Geo{}));
    auto img = DiskImage::open(p, std::nullopt, true);
    ASSERT_TRUE(img);
    EXPECT_FALSE(img->writable());
    fs::remove(p);
}

/// @test Kaputte CQM → open() liefert nullptr
TEST(CqmDiskImage, KaputteDateiOeffnetNicht) {
    const std::string p = k1520test::tempPath("k1520_test_cqm_bad.cqm");
    auto f = standard(Geo{});
    f.resize(f.size() - 7);
    schreibe(p, f);
    EXPECT_EQ(DiskImage::open(p, std::nullopt, true), nullptr);
    fs::remove(p);
}

// ─── Echte Abbilder (nur mit K1520_CQM_DIR) ──────────────────────────────────

namespace {

std::string cqmOrdner() {
    const char* e = std::getenv("K1520_CQM_DIR");
    return e ? e : "";
}

std::vector<fs::path> dateien(const fs::path& dir, const std::string& endung) {
    std::vector<fs::path> r;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == endung) r.push_back(e.path());
    std::sort(r.begin(), r.end());
    return r;
}

}  // namespace

/// @test Alle *.cqm im Ordner dekodieren fehlerfrei (Pruefwert, Laenge) und bauen ein
///       Medium, dessen Sektoren byteweise den entpackten Daten entsprechen.
TEST(CqmEchtdaten, AlleAbbilderDekodierenMitPruefwert) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    const auto liste = dateien(dir, ".cqm");
    ASSERT_FALSE(liste.empty());
    for (const auto& p : liste) {
        SCOPED_TRACE(p.string());
        const auto datei = lies(p.string());
        std::vector<uint8_t> raw;
        CqmCodec::SourceInfo si;
        std::string err;
        ASSERT_TRUE(CqmCodec::decodeBuffer(datei, raw, &si, err)) << err;
        EXPECT_TRUE(si.crc_checked);
        EXPECT_EQ(si.trailing_bytes, 0u);
        DiskMedium m;
        ASSERT_TRUE(CqmCodec::loadBuffer(datei, m, nullptr, err)) << err;
        EXPECT_EQ(sektorStrom(m, si.used_cyls), raw);
    }
}

/// @test Jede .tar ist eine Folge entpackter CQM-Abbilder (ariane.tar = ariane1+ariane2 …).
///       Einzige bekannte Abweichung: curspdoc (1 Bit, Pruefwert der CQM stimmt).
TEST(CqmEchtdaten, TarGegenstueckeStimmenByteweise) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";

    std::map<std::string, std::vector<uint8_t>> roh;
    std::map<std::string, std::vector<std::string>> nachAnfang;
    for (const auto& p : dateien(dir, ".cqm")) {
        std::vector<uint8_t> r; std::string err;
        ASSERT_TRUE(CqmCodec::decodeBuffer(lies(p.string()), r, nullptr, err)) << err;
        const std::string n = p.stem().string();
        nachAnfang[std::string(r.begin(), r.begin() + 512)].push_back(n);
        roh[n] = std::move(r);
    }

    int geprueft = 0;
    for (const auto& tp : dateien(dir, ".tar")) {
        SCOPED_TRACE(tp.string());
        const auto tar = lies(tp.string());
        const std::string name = tp.stem().string();
        size_t pos = 0;
        bool ok = true;
        while (pos < tar.size() && ok) {
            ok = false;
            auto it = nachAnfang.find(std::string(tar.begin() + pos, tar.begin() + pos + 512));
            if (it == nachAnfang.end()) break;
            for (const auto& n : it->second) {
                const auto& r = roh[n];
                if (pos + r.size() <= tar.size() && std::equal(r.begin(), r.end(), tar.begin() + pos)) {
                    pos += r.size(); ok = true; break;
                }
            }
        }
        if (ok && pos == tar.size()) { ++geprueft; continue; }

        // Sonderfall curspdoc: genau ein Byte weicht ab.
        ASSERT_EQ(name, "curspdoc") << "kein CQM-Gegenstueck gefunden, Stand bei Byte " << pos;
        const auto& r = roh.at(name);
        ASSERT_EQ(r.size(), tar.size());
        size_t diff = 0;
        for (size_t i = 0; i < r.size(); ++i) diff += (r[i] != tar[i]);
        EXPECT_EQ(diff, 1u);
        ++geprueft;
    }
    EXPECT_GT(geprueft, 0);
}

/// @test Das CQM-Medium ist spurweise bytegleich dem `.img`, das aus der .tar-Datei
///       (rohe 720K-Sektoren) mit dem Katalogformat k5601_9x512 entsteht.
TEST(CqmEchtdaten, MediumGleichImgAusDerTar) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    std::string fatal;
    const FormatCatalog kat = FormatCatalog::loadDefault(&fatal);
    const DiskFormat* fmt = kat.find("k5601_9x512");
    ASSERT_NE(fmt, nullptr) << fatal;

    int verglichen = 0;
    for (const auto& tp : dateien(dir, ".tar")) {
        const auto cq = fs::path(dir) / (tp.stem().string() + ".cqm");
        if (!fs::exists(cq) || fs::file_size(tp) != 737280) continue;   // nur Einzelabbilder
        SCOPED_TRACE(tp.string());
        if (tp.stem() == "curspdoc") continue;   // 1 Bit Abweichung der .tar, s. oben
        DiskMedium a, b; std::string err;
        ASSERT_TRUE(CqmCodec::load(cq.string(), a, nullptr, err)) << err;
        ASSERT_TRUE(ImgCodec::load(tp.string(), *fmt, b, err)) << err;
        ASSERT_EQ(a.numCylinders(), b.numCylinders());
        ASSERT_EQ(a.numHeads(), b.numHeads());
        for (uint8_t c = 0; c < a.numCylinders(); ++c)
            for (uint8_t h = 0; h < a.numHeads(); ++h) {
                ASSERT_EQ(a.peek(c, h).bytes, b.peek(c, h).bytes) << "Spur " << int(c) << "/" << int(h);
                ASSERT_EQ(a.peek(c, h).marks, b.peek(c, h).marks);
            }
        ++verglichen;
    }
    EXPECT_GT(verglichen, 0);
}
