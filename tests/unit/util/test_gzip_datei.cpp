/**
 * @file test_gzip_datei.cpp
 * @brief GoogleTests für `core/util/gzip_datei.h` — gzip-Hülle um miniz (third_party/miniz).
 *
 * | Sollwert | Quelle |
 * |---|---|
 * | Kopf 1F 8B 08 FLG MTIME(4) XFL OS, FNAME/FCOMMENT nullterminiert, FEXTRA mit Länge | RFC 1952 §2.3 |
 * | Ende: CRC-32 und ISIZE (Länge mod 2³²), je LSB zuerst; mehrere Teile hintereinander erlaubt | RFC 1952 §2.2/§2.3.1 |
 * | `HALLO_GZ` = `gzip -9 hallo.txt` (GNU gzip 1.13, mit Dateiname im Kopf) | erzeugt 2026-10-09 |
 */
#include "core/util/gzip_datei.h"
#include "tests/support/temp_path.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace k1520;

namespace {

// `printf 'Hallo K1520\n' > hallo.txt; gzip -9 hallo.txt` — Kopf mit FNAME "hallo.txt"
const std::vector<uint8_t> HALLO_GZ = {
    0x1F, 0x8B, 0x08, 0x08, 0x8A, 0x1F, 0xC9, 0x6A, 0x02, 0x03, 0x68, 0x61, 0x6C, 0x6C, 0x6F, 0x2E,
    0x74, 0x78, 0x74, 0x00, 0xF3, 0x48, 0xCC, 0xC9, 0xC9, 0x57, 0xF0, 0x36, 0x34, 0x35, 0x32, 0xE0,
    0x02, 0x00, 0x45, 0xDE, 0x4D, 0x4D, 0x0C, 0x00, 0x00, 0x00};
const std::string HALLO = "Hallo K1520\n";

std::vector<uint8_t> abbild(size_t n)
{
    // wie eine Platte: lange E5-Strecken, dazwischen Daten
    std::vector<uint8_t> d(n, 0xE5);
    for (size_t i = 0; i < n; i += 4096)
        for (size_t j = 0; j < 512 && i + j < n; ++j) d[i + j] = static_cast<uint8_t>((i / 4096) * 7 + j * 13);
    return d;
}

std::vector<uint8_t> dateiInhalt(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(GzipDatei, LiestWasGnuGzipSchreibt)
{
    std::vector<uint8_t> aus;
    std::string f;
    ASSERT_TRUE(gzip::entpacken(HALLO_GZ.data(), HALLO_GZ.size(), aus, &f)) << f;
    EXPECT_EQ(std::string(aus.begin(), aus.end()), HALLO);
}

TEST(GzipDatei, RundreiseUndKleinerAlsRoh)
{
    const auto d = abbild(3'000'000);
    for (int stufe : {1, 6, 9}) {
        std::vector<uint8_t> z, r;
        ASSERT_TRUE(gzip::packen(d.data(), d.size(), z, stufe));
        EXPECT_TRUE(gzip::istGzip(z.data(), z.size()));
        EXPECT_LT(z.size(), d.size() / 4) << "Stufe " << stufe;
        ASSERT_TRUE(gzip::entpacken(z.data(), z.size(), r));
        EXPECT_EQ(r, d) << "Stufe " << stufe;
    }
}

TEST(GzipDatei, LeererInhalt)
{
    std::vector<uint8_t> z, r{1, 2, 3};
    ASSERT_TRUE(gzip::packen(nullptr, 0, z));
    ASSERT_TRUE(gzip::entpacken(z.data(), z.size(), r));
    EXPECT_TRUE(r.empty());
}

TEST(GzipDatei, MehrteiligeDateiWieCatAGzBGz)
{
    const std::vector<uint8_t> a(1000, 'a'), b = {'x', 'y', 'z'};
    std::vector<uint8_t> za, zb, r;
    ASSERT_TRUE(gzip::packen(a.data(), a.size(), za));
    ASSERT_TRUE(gzip::packen(b.data(), b.size(), zb));
    za.insert(za.end(), zb.begin(), zb.end());
    za.insert(za.end(), HALLO_GZ.begin(), HALLO_GZ.end());
    ASSERT_TRUE(gzip::entpacken(za.data(), za.size(), r));
    std::vector<uint8_t> soll = a;
    soll.insert(soll.end(), b.begin(), b.end());
    soll.insert(soll.end(), HALLO.begin(), HALLO.end());
    EXPECT_EQ(r, soll);
}

TEST(GzipDatei, BeschaedigtUndAbgeschnittenWerdenErkannt)
{
    const auto d = abbild(100'000);
    std::vector<uint8_t> z, r;
    ASSERT_TRUE(gzip::packen(d.data(), d.size(), z));
    std::string f;

    auto kaputt = z;
    kaputt[kaputt.size() - 6] ^= 0x01;   // CRC-32
    EXPECT_FALSE(gzip::entpacken(kaputt.data(), kaputt.size(), r, &f));
    EXPECT_NE(f.find("Prüfsumme"), std::string::npos) << f;

    auto kurz = z;
    kurz.resize(z.size() / 2);
    EXPECT_FALSE(gzip::entpacken(kurz.data(), kurz.size(), r, &f));
    EXPECT_TRUE(r.empty());

    auto ohne_ende = z;
    ohne_ende.resize(z.size() - 8);       // Deflate vollständig, CRC/ISIZE fehlen
    EXPECT_FALSE(gzip::entpacken(ohne_ende.data(), ohne_ende.size(), r, &f));

    auto muell = z;
    muell.push_back(0x42);
    EXPECT_FALSE(gzip::entpacken(muell.data(), muell.size(), r, &f));
    auto auffuellung = z;
    auffuellung.insert(auffuellung.end(), 512, 0x00);   // Nullbytes dahinter (Bandarchiv) sind erlaubt
    EXPECT_TRUE(gzip::entpacken(auffuellung.data(), auffuellung.size(), r, &f)) << f;
}

TEST(GzipDatei, SpeichernIstAtomarUndLadenErkenntDieArt)
{
    const std::string p = k1520test::tempPath("k1520_gzip_test.img.gz");
    const auto d = abbild(200'000);
    std::string f;
    ASSERT_TRUE(gzip::speichern(p, d.data(), d.size(), gzip::Art::Gzip, 1, &f)) << f;
    EXPECT_FALSE(std::filesystem::exists(p + ".tmp"));
    EXPECT_EQ(gzip::artVon(p), gzip::Art::Gzip);
    EXPECT_EQ(gzip::inhaltsGroesse(p), d.size());
    std::vector<uint8_t> r;
    gzip::Art art = gzip::Art::Roh;
    ASSERT_TRUE(gzip::laden(p, r, &art, &f)) << f;
    EXPECT_EQ(art, gzip::Art::Gzip);
    EXPECT_EQ(r, d);
    std::vector<uint8_t> anfang;
    ASSERT_TRUE(gzip::ladenAnfang(p, 512, anfang));
    EXPECT_EQ(anfang, std::vector<uint8_t>(d.begin(), d.begin() + 512));

    // Scheitert das Schreiben (hier: `<pfad>.tmp` ist ein Verzeichnis), bleibt das Original heil.
    std::filesystem::create_directory(p + ".tmp");
    const auto vorher = dateiInhalt(p);
    const std::vector<uint8_t> anders(1000, 0x11);
    EXPECT_FALSE(gzip::speichern(p, anders.data(), anders.size(), gzip::Art::Gzip, 1, &f));
    EXPECT_FALSE(f.empty());
    EXPECT_EQ(dateiInhalt(p), vorher);
    std::filesystem::remove(p + ".tmp");

    // Roh: dieselbe Datei wird roh überschrieben und roh gelesen
    ASSERT_TRUE(gzip::speichern(p, d.data(), d.size(), gzip::Art::Roh, 1, &f)) << f;
    EXPECT_EQ(gzip::artVon(p), gzip::Art::Roh);
    EXPECT_EQ(dateiInhalt(p), d);
    ASSERT_TRUE(gzip::laden(p, r, &art));
    EXPECT_EQ(art, gzip::Art::Roh);
    EXPECT_EQ(r, d);
    std::filesystem::remove(p);
}

TEST(GzipDatei, ArtNachEndung)
{
    EXPECT_EQ(gzip::artNachEndung("/x/platte.k5504.img.gz"), gzip::Art::Gzip);
    EXPECT_EQ(gzip::artNachEndung("PLATTE.IMG.GZ"), gzip::Art::Gzip);
    EXPECT_EQ(gzip::artNachEndung("platte.k5504.img"), gzip::Art::Roh);
    EXPECT_EQ(gzip::artNachEndung("gz"), gzip::Art::Roh);
}
