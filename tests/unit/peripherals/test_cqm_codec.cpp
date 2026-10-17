/**
 * @file test_cqm_codec.cpp
 * @brief GoogleTests fuer CqmCodec (CopyQM-Abbild, Lesen und Schreiben).
 *
 * Schwerpunkte:
 *   - synthetische Mini-CQMs (Kopf + RLE), die der Test selbst baut: Sektorinhalt,
 *     Wiederholung ueber die Spurgrenze, nicht gespeicherte Zylinder, Kommentar
 *   - Fehlerfaelle: kaputte Kennung, kaputtes Kopfpruefbyte, abgeschnittene Datei,
 *     falscher Pruefwert, Datenstrom laenger als die Geometrie
 *   - der Pruefwert mit CopyQMs `& 0x3F`-Tabellenfehler
 *   - Container-Erkennung (ImageCodec) und Verhalten in DiskImage: Autoflush, flush,
 *     saveAs, create; nicht darstellbare Medien werden abgelehnt, die Datei bleibt heil
 *   - Schreiben: Roundtrip Medium → .cqm → Medium ueber viele Geometrien/Muster, RLE-
 *     Laufgrenzen, Erhalt der Kopffelder, Ablehnung nicht darstellbarer Medien
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

/// @test Eine unformatierte Diskette laesst sich nicht als .cqm speichern (klarer Grund,
///       Zieldatei unberuehrt)
TEST(CqmCodec, UnformatiertWirdAbgelehnt) {
    DiskMedium m(2, 2, Encoding::MFM);
    std::string err;
    const std::string p = k1520test::tempPath("k1520_test_cqm_save.cqm");
    fs::remove(p);
    EXPECT_FALSE(CqmCodec::save(p, m, err));
    EXPECT_NE(err.find("unformatiert"), std::string::npos) << err;
    EXPECT_FALSE(fs::exists(p));
}

// ─── Schreiben ───────────────────────────────────────────────────────────────

namespace {

/// Unabhaengiger RLE-Entpacker nach Formatbeschreibung (nicht der des Codecs).
std::vector<uint8_t> entpacke(const std::vector<uint8_t>& s) {
    std::vector<uint8_t> out;
    size_t p = 0;
    while (p + 2 <= s.size()) {
        const int16_t n = static_cast<int16_t>(s[p] | (s[p + 1] << 8));
        p += 2;
        if (n < 0) { out.insert(out.end(), static_cast<size_t>(-n), s[p]); ++p; }
        else { out.insert(out.end(), s.begin() + p, s.begin() + p + n); p += n; }
    }
    EXPECT_EQ(p, s.size());
    return out;
}

enum class Muster { Zaehler, Nullen, Zufall, LangeLaeufe, Leer };

std::vector<uint8_t> fuellung(Muster m, size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 12345u;
    for (size_t i = 0; i < n; ++i) {
        switch (m) {
            case Muster::Zaehler:     v[i] = static_cast<uint8_t>((i / 512) * 7 + (i % 512) / 3); break;
            case Muster::Nullen:      v[i] = 0; break;
            case Muster::Zufall:      x = x * 1664525u + 1013904223u; v[i] = static_cast<uint8_t>(x >> 24); break;
            case Muster::LangeLaeufe: v[i] = static_cast<uint8_t>(i / 3000); break;   // Laeufe ueber Sektorgrenzen
            case Muster::Leer:        v[i] = 0xE5; break;
        }
    }
    return v;
}

/// Medium aus @p raw (Zylinder → Kopf → Sektor, IDs ab @p base), wie CqmCodec/ImgCodec es baut.
DiskMedium baueMedium(unsigned cyls, unsigned heads, unsigned spt, unsigned ssz,
                      const std::vector<uint8_t>& raw, unsigned base = 1) {
    DiskMedium m(static_cast<uint8_t>(cyls), static_cast<uint8_t>(heads), Encoding::MFM);
    size_t off = 0;
    for (unsigned c = 0; c < cyls; ++c)
        for (unsigned h = 0; h < heads; ++h) {
            std::vector<LogicalSector> secs;
            for (unsigned i = 0; i < spt; ++i) {
                LogicalSector ls;
                ls.cyl = static_cast<uint8_t>(c); ls.head = static_cast<uint8_t>(h);
                ls.id = static_cast<uint8_t>(base + i); ls.size = static_cast<uint16_t>(ssz);
                ls.data.assign(raw.begin() + off, raw.begin() + off + ssz);
                off += ssz;
                secs.push_back(std::move(ls));
            }
            m.setTrack(static_cast<uint8_t>(c), static_cast<uint8_t>(h),
                       TrackCodec::buildTrack(secs, Encoding::MFM));
        }
    m.clearDirty();
    return m;
}

void gleichesMedium(const DiskMedium& a, const DiskMedium& b) {
    ASSERT_EQ(a.numCylinders(), b.numCylinders());
    ASSERT_EQ(a.numHeads(), b.numHeads());
    for (uint8_t c = 0; c < a.numCylinders(); ++c)
        for (uint8_t h = 0; h < a.numHeads(); ++h) {
            ASSERT_EQ(a.peek(c, h).bytes, b.peek(c, h).bytes) << "Spur " << int(c) << "/" << int(h);
            ASSERT_EQ(a.peek(c, h).marks, b.peek(c, h).marks);
        }
}

}  // namespace

/// @test RLE-Packer: Laufgrenzen (Blockhoechstlaenge 32767), Literal/Wiederholung gemischt
TEST(CqmWrite, PackRleLaufgrenzen) {
    for (size_t n : {1u, 2u, 4u, 5u, 6u, 254u, 255u, 256u, 257u, 32766u, 32767u, 32768u,
                     32769u, 65534u, 65535u, 65536u, 65537u, 70000u}) {
        SCOPED_TRACE(n);
        // Lauf allein, Lauf zwischen Literalen, Lauf am Ende
        for (int variante = 0; variante < 3; ++variante) {
            std::vector<uint8_t> raw;
            if (variante >= 1) raw = {1, 2, 3};
            raw.insert(raw.end(), n, 0xAA);
            if (variante == 1) raw.insert(raw.end(), {4, 5, 6, 6, 7});
            const auto p = CqmCodec::packRle(raw);
            EXPECT_EQ(entpacke(p), raw);
            // Kein Block laenger als 32767, kein Nullblock
            size_t q = 0;
            while (q + 2 <= p.size()) {
                const int16_t b = static_cast<int16_t>(p[q] | (p[q + 1] << 8));
                EXPECT_NE(b, 0);
                EXPECT_NE(b, static_cast<int16_t>(-32768));
                q += 2 + (b < 0 ? 1 : static_cast<size_t>(b));
            }
            EXPECT_EQ(q, p.size());
        }
    }
    // Zufall bleibt (fast) so gross wie die Eingabe; Nullen werden winzig.
    const auto z = fuellung(Muster::Zufall, 100000, 1);
    EXPECT_EQ(entpacke(CqmCodec::packRle(z)), z);
    EXPECT_LT(CqmCodec::packRle(z).size(), z.size() + 20);
    EXPECT_LE(CqmCodec::packRle(std::vector<uint8_t>(737280, 0)).size(), 3u * 23);
}

/// @test Roundtrip Medium → .cqm → Medium (Spurbytes und Marken gleich) ueber Geometrien/Muster
TEST(CqmWrite, RoundtripViele) {
    struct G { unsigned c, h, spt, ssz; };
    const G geos[] = {{80, 2, 9, 512}, {80, 2, 16, 256}, {80, 2, 5, 1024}, {80, 1, 9, 512},
                      {40, 2, 9, 512}, {40, 1, 8, 512}, {80, 2, 15, 512}, {80, 2, 18, 512}};
    const Muster muster[] = {Muster::Zaehler, Muster::Nullen, Muster::Zufall,
                             Muster::LangeLaeufe, Muster::Leer};
    const std::string p = k1520test::tempPath("k1520_test_cqm_rt.cqm");
    int nr = 0;
    for (const G& g : geos)
        for (Muster mu : muster) {
            SCOPED_TRACE(std::to_string(g.c) + "x" + std::to_string(g.h) + "x"
                         + std::to_string(g.spt) + "x" + std::to_string(g.ssz)
                         + " Muster " + std::to_string(static_cast<int>(mu)));
            const auto raw = fuellung(mu, size_t(g.c) * g.h * g.spt * g.ssz, ++nr);
            const DiskMedium m = baueMedium(g.c, g.h, g.spt, g.ssz, raw);
            std::string err;
            ASSERT_TRUE(CqmCodec::save(p, m, err)) << err;
            EXPECT_FALSE(fs::exists(p + ".k1520tmp"));
            DiskMedium m2;
            CqmCodec::SourceInfo si;
            ASSERT_TRUE(CqmCodec::load(p, m2, &si, err)) << err;
            // Pruefwert 0 (z. B. nur Nullbytes: CRC mit Startwert 0) gilt beim Lesen als
            // "nicht gesetzt" — Eigenheit des Formats, kein Fehler.
            EXPECT_EQ(si.crc_checked, si.crc_stored != 0);
            EXPECT_EQ(si.sector_size, g.ssz);
            EXPECT_EQ(si.secs_per_track, g.spt);
            EXPECT_EQ(si.heads, g.h);
            EXPECT_EQ(si.used_cyls, g.c);
            gleichesMedium(m, m2);
            std::vector<uint8_t> raw2;
            ASSERT_TRUE(CqmCodec::decodeBuffer(lies(p), raw2, nullptr, err)) << err;
            EXPECT_EQ(raw2, raw);
        }
    fs::remove(p);
}

/// @test Standardkopf einer neuen Datei und Pruefung gegen die Kopfregeln
TEST(CqmWrite, StandardkopfEinerNeuenDatei) {
    const auto raw = fuellung(Muster::Zaehler, 80 * 2 * 9 * 512, 3);
    const DiskMedium m = baueMedium(80, 2, 9, 512, raw);
    std::vector<uint8_t> datei;
    CqmCodec::SourceInfo si;
    std::string err;
    ASSERT_TRUE(CqmCodec::encode(m, nullptr, datei, &si, err)) << err;
    EXPECT_TRUE(CqmCodec::isCqm(datei.data(), datei.size()));
    EXPECT_EQ(si.description, "720K Double-Sided");
    EXPECT_EQ(si.label, "");
    EXPECT_EQ(si.comment, "Image file created by k1520emu");
    EXPECT_EQ(datei[2], 0x14);
    EXPECT_EQ(si.first_sector_id, 1);
    EXPECT_EQ(si.used_cyls, 80);
    EXPECT_EQ(si.total_cyls, 80);
    EXPECT_EQ(si.crc_computed, CqmCodec::crc(raw.data(), raw.size()));

    // Einseitig: Bezeichnung benennt es
    const DiskMedium e = baueMedium(40, 1, 9, 512, fuellung(Muster::Leer, 40 * 9 * 512, 1));
    ASSERT_TRUE(CqmCodec::encode(e, nullptr, datei, &si, err)) << err;
    EXPECT_EQ(si.description, "180K Single-Sided");
    EXPECT_EQ(si.heads, 1);
}

/// @test Kopffelder des Originals bleiben erhalten (Kommentar, Bezeichnung, Etikett,
///       Interleave/Skew, Zeit/Datum, Quellaufwerk, unbekannte Bytes)
TEST(CqmWrite, KopffelderBleibenErhalten) {
    Geo g; g.comment = "Mein Kommentar\x1A";
    std::vector<uint8_t> raw;
    auto orig = standard(g, &raw);
    // Zusatzfelder in den Kopf: Etikett, Zeit/Datum, Interleave/Skew, Quellaufwerk, Unbekanntes
    const std::string et = "MEINE-DISK ";
    std::copy(et.begin(), et.end(), orig.begin() + 0x60);
    orig[0x6B] = 0x34; orig[0x6C] = 0x12; orig[0x6D] = 0x78; orig[0x6E] = 0x56;
    orig[0x74] = 3; orig[0x75] = 2; orig[0x76] = 2;
    orig[0x08] = 0xAB; orig[0x7F] = 0xCD;
    uint8_t sum = 0;
    for (size_t i = 0; i < 0x84; ++i) sum = static_cast<uint8_t>(sum + orig[i]);
    orig[0x84] = static_cast<uint8_t>(-sum);

    DiskMedium m; CqmCodec::SourceInfo vorlage; std::string err;
    ASSERT_TRUE(CqmCodec::loadBuffer(orig, m, &vorlage, err)) << err;

    std::vector<uint8_t> neu;
    CqmCodec::SourceInfo si;
    ASSERT_TRUE(CqmCodec::encode(m, &vorlage, neu, &si, err)) << err;
    EXPECT_EQ(si.comment, "Mein Kommentar");          // ^Z beim Lesen abgeschnitten …
    EXPECT_EQ(si.comment_raw, vorlage.comment_raw);   // … aber unveraendert mitgeschrieben
    EXPECT_EQ(si.label, "MEINE-DISK");
    EXPECT_EQ(si.description, "Testabbild");
    EXPECT_EQ(si.interleave, 3);
    EXPECT_EQ(si.skew, 2);
    EXPECT_EQ(si.dos_time, 0x1234);
    EXPECT_EQ(si.dos_date, 0x5678);
    // Byte fuer Byte gleich bis auf Pruefwert (gleiche Daten → sogar der gleiche) und Kopfbyte
    for (size_t i = 0; i < CqmCodec::kHeaderSize; ++i) EXPECT_EQ(neu[i], orig[i]) << "Kopfbyte " << i;

    // Geaenderter Inhalt: Pruefwert zieht nach, der Rest bleibt
    auto secs = TrackCodec::parseTrack(m.peek(0, 0));
    secs[0].data[0] ^= 0x0F;
    m.setTrack(0, 0, TrackCodec::buildTrack(secs, Encoding::MFM));
    ASSERT_TRUE(CqmCodec::encode(m, &si, neu, &si, err)) << err;
    EXPECT_NE(std::vector<uint8_t>(neu.begin() + 0x5C, neu.begin() + 0x60),
              std::vector<uint8_t>(orig.begin() + 0x5C, orig.begin() + 0x60));
    EXPECT_EQ(neu[0x08], 0xAB);
    EXPECT_EQ(neu[0x7F], 0xCD);
    EXPECT_EQ(si.label, "MEINE-DISK");
    EXPECT_EQ(si.crc_checked, true);

    // Ueber Datei: save(path, …, vorlage) → load liefert dieselben Felder
    const std::string p = k1520test::tempPath("k1520_test_cqm_kopf.cqm");
    ASSERT_TRUE(CqmCodec::save(p, m, &vorlage, nullptr, err)) << err;
    DiskMedium m2; CqmCodec::SourceInfo s2;
    ASSERT_TRUE(CqmCodec::load(p, m2, &s2, err)) << err;
    EXPECT_EQ(s2.comment, "Mein Kommentar");
    EXPECT_EQ(s2.skew, 2);
    fs::remove(p);

    // Andere Geometrie → Bezeichnung/Interleave neu, Kommentar und Etikett bleiben
    const DiskMedium e = baueMedium(40, 1, 9, 512, fuellung(Muster::Leer, 40 * 9 * 512, 1));
    ASSERT_TRUE(CqmCodec::encode(e, &vorlage, neu, &si, err)) << err;
    EXPECT_EQ(si.description, "180K Single-Sided");
    EXPECT_EQ(si.comment, "Mein Kommentar");
    EXPECT_EQ(si.label, "MEINE-DISK");
}

/// @test Erste Sektor-ID ungleich 1 bleibt erhalten (Kopfbyte 0x71)
TEST(CqmWrite, ErsteSektorIdWirdGeschrieben) {
    const auto raw = fuellung(Muster::Zaehler, 3 * 2 * 4 * 256, 9);
    const DiskMedium m = baueMedium(3, 2, 4, 256, raw, /*base=*/17);
    std::vector<uint8_t> datei; CqmCodec::SourceInfo si; std::string err;
    ASSERT_TRUE(CqmCodec::encode(m, nullptr, datei, &si, err)) << err;
    EXPECT_EQ(si.first_sector_id, 17);
    EXPECT_EQ(datei[0x71], 16);
    DiskMedium m2;
    ASSERT_TRUE(CqmCodec::loadBuffer(datei, m2, nullptr, err)) << err;
    gleichesMedium(m, m2);
}

/// @test Am Ende unformatierte Zylinder: nur die beschriebenen werden gespeichert
TEST(CqmWrite, UnformatierteEndzylinderWerdenNichtGespeichert) {
    const auto raw = fuellung(Muster::Zaehler, 3 * 2 * 4 * 256, 9);
    DiskMedium m = baueMedium(3, 2, 4, 256, raw);
    DiskMedium gross(5, 2, Encoding::MFM);
    for (uint8_t c = 0; c < 3; ++c)
        for (uint8_t h = 0; h < 2; ++h) gross.setTrack(c, h, m.peek(c, h));
    gross.clearDirty();
    std::vector<uint8_t> datei; CqmCodec::SourceInfo si; std::string err;
    ASSERT_TRUE(CqmCodec::encode(gross, nullptr, datei, &si, err)) << err;
    EXPECT_EQ(si.used_cyls, 3);
    EXPECT_EQ(si.total_cyls, 5);
    std::vector<uint8_t> r;
    ASSERT_TRUE(CqmCodec::decodeBuffer(datei, r, nullptr, err)) << err;
    EXPECT_EQ(r, raw);
}

/// @test Nicht darstellbare Medien werden abgelehnt; die vorhandene Zieldatei bleibt
///       unveraendert, es bleibt keine Nachbardatei liegen
TEST(CqmWrite, NichtDarstellbaresWirdAbgelehnt) {
    const std::string p = k1520test::tempPath("k1520_test_cqm_ab.cqm");
    const auto alt = standard(Geo{});
    const auto raw = fuellung(Muster::Zaehler, 4 * 2 * 4 * 256, 5);
    struct Fall { std::string name; std::string stichwort; DiskMedium m; };
    std::vector<Fall> faelle;

    {   // Daten-CRC kaputt (Datenbyte gekippt)
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        TrackImage t = m.peek(1, 0);
        t.bytes[TrackCodec::parseTrack(t)[1].data_pos + 10] ^= 0x01;
        m.setTrack(1, 0, t);
        faelle.push_back({"crc", "Spur 1/0", std::move(m)});
    }
    {   // gemischte Sektorgroessen innerhalb einer Spur
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        std::vector<LogicalSector> secs;
        for (unsigned i = 1; i <= 2; ++i) {
            LogicalSector ls; ls.cyl = 2; ls.head = 0; ls.id = static_cast<uint8_t>(i);
            ls.size = (i == 1 ? 256 : 512); ls.data.assign(ls.size, 1);
            secs.push_back(ls);
        }
        m.setTrack(2, 0, TrackCodec::buildTrack(secs, Encoding::MFM));
        faelle.push_back({"gemischt", "Sektorgroessen", std::move(m)});
    }
    {   // andere Sektorzahl auf einer Spur
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        std::vector<LogicalSector> secs;
        for (unsigned i = 1; i <= 3; ++i) {
            LogicalSector ls; ls.cyl = 3; ls.head = 1; ls.id = static_cast<uint8_t>(i);
            ls.size = 256; ls.data.assign(256, 1);
            secs.push_back(ls);
        }
        m.setTrack(3, 1, TrackCodec::buildTrack(secs, Encoding::MFM));
        faelle.push_back({"spt", "Geometrie", std::move(m)});
    }
    {   // Luecke in den Sektor-IDs
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        std::vector<LogicalSector> secs;
        for (unsigned id : {1u, 2u, 4u, 5u}) {
            LogicalSector ls; ls.cyl = 0; ls.head = 1; ls.id = static_cast<uint8_t>(id);
            ls.size = 256; ls.data.assign(256, 1);
            secs.push_back(ls);
        }
        m.setTrack(0, 1, TrackCodec::buildTrack(secs, Encoding::MFM));
        faelle.push_back({"ids", "lueckenlos", std::move(m)});
    }
    {   // unformatierte Spur mitten drin
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        m.setTrack(1, 1, TrackImage{});
        faelle.push_back({"loch", "unformatiert", std::move(m)});
    }
    {   // FM-Spur
        DiskMedium m = baueMedium(4, 2, 4, 256, raw);
        std::vector<LogicalSector> secs;
        for (unsigned i = 1; i <= 4; ++i) {
            LogicalSector ls; ls.cyl = 1; ls.head = 1; ls.id = static_cast<uint8_t>(i);
            ls.size = 256; ls.data.assign(256, 1);
            secs.push_back(ls);
        }
        m.setTrack(1, 1, TrackCodec::buildTrack(secs, Encoding::FM));
        faelle.push_back({"fm", "FM", std::move(m)});
    }
    {   // komplett unformatiert
        faelle.push_back({"leer", "unformatiert", DiskMedium(4, 2, Encoding::MFM)});
    }
    for (auto& f : faelle) {
        SCOPED_TRACE(f.name);
        schreibe(p, alt);
        std::string err;
        EXPECT_FALSE(CqmCodec::save(p, f.m, err));
        EXPECT_NE(err.find(f.stichwort), std::string::npos) << err;
        // Die Meldung nennt die Ursache ausdruecklich (Oberflaechen zeigen sie unveraendert).
        if (f.name == "gemischt" || f.name == "spt" || f.name == "ids")
            EXPECT_NE(err.find("unterstuetzt keine gemischten Formate"), std::string::npos) << err;
        if (f.name == "crc")
            EXPECT_NE(err.find("Sektorkontrollbloecke von UDOS"), std::string::npos) << err;
        if (f.name == "fm")
            EXPECT_NE(err.find("nur MFM"), std::string::npos) << err;
        EXPECT_EQ(lies(p), alt);
        EXPECT_FALSE(fs::exists(p + ".k1520tmp"));
    }
    fs::remove(p);
}

/// @test Schreiben ueber ein nicht schreibbares Verzeichnis scheitert sauber
TEST(CqmWrite, UnbeschreibbaresZielGibtFehler) {
    const DiskMedium m = baueMedium(2, 2, 4, 256, fuellung(Muster::Leer, 2 * 2 * 4 * 256, 1));
    std::string err;
    EXPECT_FALSE(CqmCodec::save("/nicht/vorhanden/x.cqm", m, err));
    EXPECT_FALSE(err.empty());
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
    EXPECT_TRUE(ImageCodec::writable(ContainerType::Cqm));
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
    EXPECT_TRUE(ImageCodec::save(q, ContainerType::Cqm, nullptr, m, err)) << err;
    DiskMedium m2;
    EXPECT_TRUE(ImageCodec::load(q, ContainerType::Cqm, nullptr, m2, err)) << err;
    fs::remove(q);
}

// ─── DiskImage: schreibbar ───────────────────────────────────────────────────

namespace {

/// Sektor (c,h,id) der Spur durch @p fuell ersetzen (Spur neu gebaut, CRCs stimmen).
TrackImage mitSektor(const DiskMedium& m, uint8_t c, uint8_t h, uint8_t id, uint8_t fuell) {
    auto secs = TrackCodec::parseTrack(m.peek(c, h));
    for (auto& s : secs)
        if (s.id == id) std::fill(s.data.begin(), s.data.end(), fuell);
    return TrackCodec::buildTrack(secs, Encoding::MFM);
}

}  // namespace

/// @test DiskImage oeffnet .cqm beschreibbar; Autoflush schreibt nach der Schreibpause,
///       Kopfdaten (Kommentar) bleiben, saveAs() in .hfe/.dmk bindet um
TEST(CqmDiskImage, AutoflushSchreibtNachDerSchreibpauseUndBehaeltDenKopf) {
    Geo g; g.comment = "Probe";
    std::vector<uint8_t> raw;
    const auto datei = standard(g, &raw);
    const std::string p = k1520test::tempPath("k1520_test_cqm_di.cqm");
    schreibe(p, datei);

    {
        auto img = DiskImage::open(p, std::nullopt, /*write_protect=*/false);
        ASSERT_TRUE(img);
        EXPECT_EQ(img->container(), ContainerType::Cqm);
        EXPECT_TRUE(img->bindingWritable());
        ASSERT_NE(img->cqmInfo(), nullptr);
        EXPECT_EQ(img->cqmInfo()->comment, "Probe");

        // Der Gast schreibt Sektor 2 der Spur 1/1 (Medium wird schmutzig) …
        ASSERT_TRUE(img->writeTrack(1, 1, mitSektor(img->medium(), 1, 1, 2, 0x5A)));
        EXPECT_TRUE(img->medium().dirty());
        EXPECT_FALSE(img->autoFlush(1));                           // Uhr startet
        EXPECT_FALSE(img->autoFlush(1 + kAutoFlushDelayCycles / 2));   // noch nicht
        EXPECT_EQ(lies(p), datei);
        EXPECT_TRUE(img->autoFlush(1 + kAutoFlushDelayCycles + 10));   // Pause vorbei
        EXPECT_FALSE(img->medium().dirty());
        EXPECT_NE(lies(p), datei);
    }

    // Neu geladen: geaenderter Sektor da, Rest gleich, Kommentar erhalten.
    std::vector<uint8_t> raw2;
    CqmCodec::SourceInfo si;
    std::string err;
    ASSERT_TRUE(CqmCodec::decodeBuffer(lies(p), raw2, &si, err)) << err;
    EXPECT_EQ(si.comment, "Probe");
    EXPECT_EQ(raw2.size(), raw.size());
    auto erwartet = raw;
    // Spur 1/1 = vierte Spur (c*2+h = 3), Sektor-ID 2 = Index 1 (secbase 0 → IDs 1..4)
    const size_t off = (3 * g.spt + 1) * g.ssz;
    std::fill(erwartet.begin() + off, erwartet.begin() + off + g.ssz, 0x5A);
    EXPECT_EQ(raw2, erwartet);

    // Umspeichern in andere Container geht und bindet um.
    auto img = DiskImage::open(p, std::nullopt, true);
    ASSERT_TRUE(img);
    const std::string hfe = k1520test::tempPath("k1520_test_cqm_di.hfe");
    const std::string dmk = k1520test::tempPath("k1520_test_cqm_di.dmk");
    ASSERT_TRUE(img->exportTo(hfe, std::nullopt)) << img->lastError();
    ASSERT_TRUE(img->exportTo(dmk, std::nullopt)) << img->lastError();
    ASSERT_TRUE(img->saveAs(hfe, std::nullopt)) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Hfe);
    EXPECT_TRUE(img->bindingWritable());
    EXPECT_EQ(img->cqmInfo(), nullptr);

    // … und wieder zurueck in ein .cqm: die Kopfdaten stammen aus dem .hfe-Weg nicht mehr,
    // der Inhalt ist aber verlustfrei.
    const std::string q = k1520test::tempPath("k1520_test_cqm_di2.cqm");
    ASSERT_TRUE(img->saveAs(q, std::nullopt)) << img->lastError();
    EXPECT_EQ(img->container(), ContainerType::Cqm);
    ASSERT_NE(img->cqmInfo(), nullptr);
    EXPECT_EQ(img->cqmInfo()->comment, "Image file created by k1520emu");
    std::vector<uint8_t> raw3;
    ASSERT_TRUE(CqmCodec::decodeBuffer(lies(q), raw3, nullptr, err)) << err;
    EXPECT_EQ(raw3, erwartet);
    fs::remove(p); fs::remove(hfe); fs::remove(dmk); fs::remove(q);
}

/// @test Ein Medium, das sich nicht als Sektorstrom darstellen laesst: Datei bleibt
///       unveraendert, kein Teilzustand (keine Nachbardatei), Maschine laeuft weiter
TEST(CqmDiskImage, NichtDarstellbaresMediumLaesstDieDateiHeil) {
    const auto datei = standard(Geo{});
    const std::string p = k1520test::tempPath("k1520_test_cqm_nodarst.cqm");
    schreibe(p, datei);
    {
        auto img = DiskImage::open(p, std::nullopt, false);
        ASSERT_TRUE(img);
        // Gast formatiert Spur 0/0 anders (2 Sektoren zu 1024) → ungleichmaessige Geometrie.
        std::vector<LogicalSector> secs;
        for (uint8_t i = 1; i <= 2; ++i) {
            LogicalSector ls; ls.cyl = 0; ls.head = 0; ls.id = i; ls.size = 1024;
            ls.data.assign(1024, 0x11);
            secs.push_back(ls);
        }
        ASSERT_TRUE(img->writeTrack(0, 0, TrackCodec::buildTrack(secs, Encoding::MFM)));
        EXPECT_FALSE(img->autoFlush(1));
        EXPECT_FALSE(img->autoFlush(1 + kAutoFlushDelayCycles + 10));
        EXPECT_TRUE(img->saveFailed());
        EXPECT_FALSE(img->flush());
        EXPECT_NE(std::string(img->lastError()).find("Geometrie"), std::string::npos)
            << img->lastError();
        EXPECT_EQ(lies(p), datei);
        EXPECT_FALSE(fs::exists(p + ".k1520tmp"));
    }   // Destruktor: flush scheitert still, Datei bleibt
    EXPECT_EQ(lies(p), datei);
    EXPECT_FALSE(fs::exists(p + ".k1520tmp"));
    fs::remove(p);
}

/// @test flush() einer beschreibbaren .cqm und neue Datei per create() mit Format
TEST(CqmDiskImage, FlushUndCreateMitFormat) {
    std::string fatal;
    const FormatCatalog kat = FormatCatalog::loadDefault(&fatal);
    const DiskFormat* fmt = kat.find("k5601_9x512");
    ASSERT_NE(fmt, nullptr) << fatal;

    const std::string p = k1520test::tempPath("k1520_test_cqm_create.cqm");
    fs::remove(p);
    {
        auto img = DiskImage::create(p, *fmt, /*write_protect=*/false);
        ASSERT_TRUE(img);
        EXPECT_EQ(img->container(), ContainerType::Cqm);
        EXPECT_EQ(img->diskFormat(), nullptr);
        ASSERT_NE(img->cqmInfo(), nullptr);
        EXPECT_EQ(img->cqmInfo()->description, "720K Double-Sided");
        EXPECT_EQ(img->cqmInfo()->label, "");
        ASSERT_TRUE(img->writeTrack(5, 0, mitSektor(img->medium(), 5, 0, 3, 0x77)));
        EXPECT_TRUE(img->flush());
    }
    std::vector<uint8_t> raw;
    CqmCodec::SourceInfo si;
    std::string err;
    ASSERT_TRUE(CqmCodec::decodeBuffer(lies(p), raw, &si, err)) << err;
    EXPECT_EQ(si.used_cyls, 80);
    EXPECT_EQ(si.heads, 2);
    ASSERT_EQ(raw.size(), 737280u);
    std::vector<uint8_t> erwartet(737280, 0xE5);
    const size_t off = (10 * 9 + 2) * 512;
    std::fill(erwartet.begin() + off, erwartet.begin() + off + 512, 0x77);
    EXPECT_EQ(raw, erwartet);
    // Die Datei ist klein (RLE), nicht 720 KB.
    EXPECT_LT(fs::file_size(p), 20000u);

    // Ohne Format (leer/unformatiert) wird nichts angelegt.
    const std::string q = k1520test::tempPath("k1520_test_cqm_create2.cqm");
    fs::remove(q);
    EXPECT_EQ(DiskImage::create(q, std::nullopt, false), nullptr);
    EXPECT_FALSE(fs::exists(q));
    fs::remove(p);
}

/// @test Geoeffnet mit write_protect=true bleibt es dabei
TEST(CqmDiskImage, SchreibschutzBleibtErhalten) {
    const std::string p = k1520test::tempPath("k1520_test_cqm_wp.cqm");
    schreibe(p, standard(Geo{}));
    auto img = DiskImage::open(p, std::nullopt, true);
    ASSERT_TRUE(img);
    EXPECT_FALSE(img->writable());
    const auto vorher = lies(p);
    auto t = img->readTrack(0, 0);
    EXPECT_FALSE(img->writeTrack(0, 0, t));    // Schreibschutz weist ab
    EXPECT_TRUE(img->flush());
    EXPECT_EQ(lies(p), vorher);
    img->setWriteProtect(false);               // frei aufhebbar, kein Zwang
    EXPECT_TRUE(img->writable());
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

/// @test Alle Abbilder: laden → speichern (mit Kopfvorlage) → laden; Mediuminhalt, dekodierte
///       Sektordaten und Kopffelder gleich.  Byte-Identitaet der Datei ist NICHT verlangt
///       (anderer RLE-Schnitt), wohl aber die Annahme durch den Leser samt Pruefwert.
///       Eine Gegenpruefung mit libdsk ist hier nicht moeglich (nicht eingebunden).
TEST(CqmEchtdaten, SpeichernUndWiederLadenIstVerlustfrei) {
    const std::string dir = cqmOrdner();
    if (dir.empty()) GTEST_SKIP() << "K1520_CQM_DIR nicht gesetzt";
    const std::string tmp = k1520test::tempPath("k1520_test_cqm_echt_rt.cqm");
    int n = 0;
    for (const auto& p : dateien(dir, ".cqm")) {
        SCOPED_TRACE(p.string());
        const auto datei = lies(p.string());
        std::vector<uint8_t> raw1;
        CqmCodec::SourceInfo s1;
        std::string err;
        ASSERT_TRUE(CqmCodec::decodeBuffer(datei, raw1, &s1, err)) << err;
        DiskMedium a;
        ASSERT_TRUE(CqmCodec::loadBuffer(datei, a, &s1, err)) << err;

        ASSERT_TRUE(CqmCodec::save(tmp, a, &s1, nullptr, err)) << err;
        const auto datei2 = lies(tmp);
        std::vector<uint8_t> raw2;
        CqmCodec::SourceInfo s2;
        ASSERT_TRUE(CqmCodec::decodeBuffer(datei2, raw2, &s2, err)) << err;
        EXPECT_EQ(raw2, raw1);
        EXPECT_TRUE(s2.crc_checked);
        EXPECT_EQ(s2.trailing_bytes, 0u);
        // Kopf: alles gleich bis auf Pruefwert (0x5C..0x5F, falls Original keinen hatte)
        // und Kopfpruefbyte
        EXPECT_EQ(s2.comment_raw, s1.comment_raw);
        EXPECT_EQ(s2.description, s1.description);
        EXPECT_EQ(s2.label, s1.label);
        EXPECT_EQ(s2.interleave, s1.interleave);
        EXPECT_EQ(s2.skew, s1.skew);
        for (size_t i = 0; i < CqmCodec::kHeaderSize; ++i) {
            if (i >= 0x5C && i < 0x60) continue;
            if (i == 0x84) continue;
            ASSERT_EQ(s2.raw_header[i], s1.raw_header[i]) << "Kopfbyte " << i;
        }
        DiskMedium b;
        ASSERT_TRUE(CqmCodec::loadBuffer(datei2, b, nullptr, err)) << err;
        gleichesMedium(a, b);
        // Die neue Datei darf nicht groesser sein als Original + 3 % (kompakter Schnitt)
        EXPECT_LE(datei2.size(), datei.size() + datei.size() / 30 + 64);
        ++n;
    }
    EXPECT_GE(n, 1);
    fs::remove(tmp);
}
