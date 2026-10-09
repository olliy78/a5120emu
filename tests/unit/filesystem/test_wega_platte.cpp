/**
 * @file test_wega_platte.cpp
 * @brief GoogleTests für @ref WegaPlatte — P8000-Plattenabbild mit WEGA-Partitionen.
 *
 * | Sollwert | Quelle |
 * |---|---|
 * | PAR in Z0/K0/S1: "DEFEKT" @0, "PARMTR" @256, Zyl. @277 (LSB zuerst), Köpfe @279, Sektoren @280 | `doc/p8000/wdc_firmware.md` §7 |
 * | WDC-Block 0 = Zylinder 1; jede BTT-Spur ≤ Zielspur schiebt um eine Spur | ebd. §6 |
 * | md0 /usr 0/13000, md2 / 16000/7000, md3 /tmp 23000/4000, md4 /z 27000/60732 | `uts/conf/wpar.c`, `uts/h/mdsize.h` |
 *
 * Die installierte Platte aus dem P15-Lauf (`~/.cache/k1520emu/p8000_wega/
 * p15_7_sync.platte.img`) wird nur GELESEN und nur geprüft, wenn sie da ist.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/wega/wega_platte.h"
#include "core/util/gzip_datei.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {

constexpr int kZyl = 400, kKoepfe = 2, kSek = 17;   // 399·34 = 13566 Bloecke > md0

/// Leeres Plattenabbild mit PAR (und optional BTT-Eintraegen).
std::vector<uint8_t> leerePlatte(const std::vector<std::pair<int, int>>& btt = {}) {
    std::vector<uint8_t> b(size_t(kZyl) * kKoepfe * kSek * 512, 0xE5);
    uint8_t* s = b.data();
    std::memset(s, 0, 512);
    std::memcpy(s, "DEFEKT", 6);
    s[6] = static_cast<uint8_t>(btt.size() * 3);
    size_t o = 8;
    for (const auto& [z, k] : btt) {
        s[o++] = static_cast<uint8_t>(z >> 8); s[o++] = static_cast<uint8_t>(z); s[o++] = static_cast<uint8_t>(k);
    }
    s[o] = s[o + 1] = s[o + 2] = 0xFF;
    std::memcpy(s + 256, "PARMTR", 6);
    std::memcpy(s + 262, "TEST PLATTE ", 12);
    s[277] = kZyl & 0xFF; s[278] = kZyl >> 8; s[279] = kKoepfe; s[280] = kSek;
    return b;
}

void schreibe(const std::string& pfad, const std::vector<uint8_t>& b) {
    std::ofstream(pfad, std::ios::binary).write(reinterpret_cast<const char*>(b.data()),
                                                static_cast<std::streamsize>(b.size()));
}

}  // namespace

TEST(WegaPlatte, OhnePARKeinePlatte) {
    const std::string p = k1520test::tempPath("keine_platte.img");
    schreibe(p, std::vector<uint8_t>(4096, 0xE5));
    std::string why;
    EXPECT_FALSE(WegaPlatte::istPlatte(p, &why));
    EXPECT_NE(why.find("PAR"), std::string::npos);
    fs::remove(p);
}

TEST(WegaPlatte, BlockZuOffsetMitDefektspur) {
    const std::string p = k1520test::tempPath("btt_platte.img");
    // Defekt: Zylinder 1 Kopf 1 = Spur 3 (Spur 2 = erste Spur des Blockraums)
    std::vector<uint8_t> b = leerePlatte({{1, 1}});
    // md0 auf der verschobenen Lage anlegen, damit die Platte eingehaengt wird
    {
        auto off = [](uint32_t bn) -> int64_t {
            uint64_t spur = bn / kSek + kKoepfe;
            if (spur >= 3) ++spur;
            return static_cast<int64_t>((spur * kSek + bn % kSek) * 512);
        };
        std::string err;
        auto fs = WegaFileSystem::format(
            std::make_unique<WegaSpeicherDev>(b, 13000, off, nullptr), "usr", err, 0, 3, 34);
        ASSERT_TRUE(fs) << err;
    }
    schreibe(p, b);
    ASSERT_TRUE(WegaPlatte::istPlatte(p));
    std::string err;
    auto pl = WegaPlatte::open(p, err);
    ASSERT_TRUE(pl) << err;
    EXPECT_EQ(pl->defektspuren(), 1u);
    EXPECT_EQ(pl->offsetVon(0), 2 * kSek * 512);           // Zylinder 1, Kopf 0
    EXPECT_EQ(pl->offsetVon(kSek), 4 * kSek * 512);        // Spur 3 ist defekt → Spur 4
    ASSERT_EQ(pl->volumeCount(), 1);                       // md2… liegen hinter der Platte
    EXPECT_EQ(pl->volumeDir(0), "md0");
    EXPECT_EQ(pl->fs(0).superblock().fsize, 13000u);
    fs::remove(p);
}

TEST(WegaPlatte, SchreibschutzUndRundreise) {
    const std::string p = k1520test::tempPath("rund_platte.img");
    std::vector<uint8_t> b = leerePlatte();
    {
        auto off = [](uint32_t bn) -> int64_t { return (int64_t(bn) + kKoepfe * kSek) * 512; };
        std::string err;
        ASSERT_TRUE(WegaFileSystem::format(
            std::make_unique<WegaSpeicherDev>(b, 13000, off, nullptr), "usr", err, 0, 3, 34)) << err;
    }
    schreibe(p, b);
    std::string err;
    {
        auto pl = WegaPlatte::open(p, err);                 // Vorgabe: schreibgeschuetzt
        ASSERT_TRUE(pl) << err;
        EXPECT_FALSE(pl->fs(0).write("x", {1, 2, 3}, {}));
        EXPECT_FALSE(pl->dirty());
    }
    std::vector<uint8_t> inhalt(80000);
    for (size_t i = 0; i < inhalt.size(); ++i) inhalt[i] = static_cast<uint8_t>(i * 13);
    {
        auto pl = WegaPlatte::open(p, err, /*read_only=*/false);
        ASSERT_TRUE(pl) << err;
        ASSERT_TRUE(pl->fs(0).write("lib/neu", inhalt, {})) << pl->fs(0).lastError();
        EXPECT_TRUE(pl->dirty());
        ASSERT_TRUE(pl->flush()) << pl->lastError();
    }
    EXPECT_TRUE(fs::exists(p + "~"));                      // Sicherung vor dem 1. Schreiben
    auto pl = WegaPlatte::open(p, err);
    ASSERT_TRUE(pl) << err;
    std::vector<uint8_t> z;
    ASSERT_TRUE(pl->fs(0).read("lib/neu", z));
    EXPECT_EQ(z, inhalt);
    EXPECT_TRUE(pl->fs(0).check(FsCheckLevel::Voll, true).ohneBefund());
    // PAR/Zylinder 0 bleibt unberuehrt
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> neu((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(std::equal(neu.begin(), neu.begin() + kKoepfe * kSek * 512, b.begin()));
    // Unter Windows verweigert remove() eine noch offene Datei ("Sharing violation").
    f.close();
    pl.reset();
    fs::remove(p);
    fs::remove(p + "~");
}

TEST(WegaPlatte, GepacktesAbbildBleibtGepackt) {
    const std::string p = k1520test::tempPath("rund_platte.img.gz");
    std::vector<uint8_t> b = leerePlatte();
    {
        auto off = [](uint32_t bn) -> int64_t { return (int64_t(bn) + kKoepfe * kSek) * 512; };
        std::string err;
        ASSERT_TRUE(WegaFileSystem::format(
            std::make_unique<WegaSpeicherDev>(b, 13000, off, nullptr), "usr", err, 0, 3, 34)) << err;
    }
    ASSERT_TRUE(k1520::gzip::speichern(p, b.data(), b.size(), k1520::gzip::Art::Gzip));
    EXPECT_TRUE(WegaPlatte::istPlatte(p));
    std::string err;
    const std::vector<uint8_t> inhalt = {'g', 'e', 'p', 'a', 'c', 'k', 't'};
    {
        auto pl = WegaPlatte::open(p, err, /*read_only=*/false);
        ASSERT_TRUE(pl) << err;
        ASSERT_TRUE(pl->fs(0).write("gz", inhalt, {})) << pl->fs(0).lastError();
        ASSERT_TRUE(pl->flush()) << pl->lastError();
    }
    EXPECT_EQ(k1520::gzip::artVon(p), k1520::gzip::Art::Gzip);
    EXPECT_EQ(k1520::gzip::artVon(p + "~"), k1520::gzip::Art::Gzip);   // Sicherung = altes Original
    EXPECT_LT(fs::file_size(p), b.size() / 10);
    auto pl = WegaPlatte::open(p, err);
    ASSERT_TRUE(pl) << err;
    std::vector<uint8_t> z;
    ASSERT_TRUE(pl->fs(0).read("gz", z));
    EXPECT_EQ(z, inhalt);
    pl.reset();
    fs::remove(p);
    fs::remove(p + "~");
}

TEST(WegaPlatte, InstallierteWegaPlatteOhneBefund) {
    std::string pfad;
    if (const char* e = std::getenv("K1520_WEGA_PLATTE")) pfad = e;
    else if (const char* h = std::getenv("HOME"))
        pfad = std::string(h) + "/.cache/k1520emu/p8000_wega/p15_7_sync.platte.img";
    std::error_code ec;
    if (pfad.empty() || !fs::exists(pfad, ec)) GTEST_SKIP() << "installierte Platte fehlt: " << pfad;
    std::string err;
    auto pl = WegaPlatte::open(pfad, err);                  // nur lesen, nie flush
    ASSERT_TRUE(pl) << err;
    EXPECT_EQ(pl->laufwerk(), "ROB K5504.50");
    ASSERT_EQ(pl->volumeCount(), 4);                        // md0, md2, md3, md4 (md1 = swap)
    EXPECT_EQ(pl->volumeDir(1), "md2");
    EXPECT_EQ(pl->fs(1).superblock().fsize, 7000u);
    EXPECT_NE(pl->fs(1).lookup("etc/passwd"), 0u);
    EXPECT_NE(pl->fs(0).lookup("bin"), 0u);
    for (int v = 0; v < pl->volumeCount(); ++v) {
        const FsCheckReport r = pl->fs(v).check(FsCheckLevel::Voll, true);
        EXPECT_TRUE(r.ohneBefund()) << pl->volumeDir(v) << "\n" << r.alsText();
    }
    EXPECT_FALSE(pl->dirty());
}
