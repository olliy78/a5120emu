/**
 * @file test_k8915_format.cpp
 * @brief K8915, AP-E4f (doc/design/16_k8915.md §8a): Leerdiskette → FORMAT.COM → DISGEN.COM
 *        → Kaltstart von der neuen Diskette — die ganze Kette mit den Originalprogrammen.
 *
 * FORMAT.COM des K8915 programmiert die K5122 selbst: im Index-ISR (Vektor 24H) schreibt
 * es mit `/WE` = 0 den vorgebauten Rohstrom einer Spur Byte für Byte an 14H (jedes
 * `OUT` wartet per `/WAIT` auf sein Fenster), im nächsten Index-ISR (22H) liest es die
 * Spur zurück und vergleicht sie Byte für Byte (Indexmarke mit MK = 1, jedes Feld mit
 * MK = 0).  DISGEN.COM liest die Systemspuren von A: und schreibt sie über das BIOS auf
 * B: — dafür muss das BIOS B: als 5 × 1024 kennen, deshalb läuft die Kette auf der
 * Diskette 900 (Fassung „55 K“; auf 901 ist B: per DISGEN 16 × 256, §4.4).
 *
 * Langsam (≈ 500 Mio. Takte für 160 Spuren): Label `format_integration`
 * (`tools/dev.sh test-format`).
 */

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"
#include "tests/system/k8915_bedienung.h"

using k1520test::TempDisk;
using k1520test::vramLines;
using namespace k8915test;

namespace {

struct Kataloge {
    FormatCatalog formate;
    FsCatalog     fs;
    Kataloge() {
        std::string f;
        formate = FormatCatalog::loadDefault(&f);
        fs      = FsCatalog::loadDefault(formate, &f);
    }
};

}  // namespace

/**
 * @test K8915Format.LeerdisketteFormatDisgenKaltstart
 * @brief Das Fertig-Kriterium von AP-E4f.
 *  1. Eine echte Leerdiskette (unformatiert, `createDisk` mit leerem Formatnamen) in B:.
 *  2. FORMAT.COM, Verfahren 24 (5,25″ DS, 5 × 1024, mit Indexmarke), Spur 00–79:
 *     „FUNCTION COMPLETE“ ohne „ERROR“ — das eigene Prüf-Lesen ist also durch.
 *     `dir b:` meldet „NO FILE“; das k1520DiskTool erkennt `cpa800`, Prüfung ohne Befund.
 *  3. DISGEN.COM: „Read system tracks“ von A:, „Write system tracks“ auf B:, „Exit“.
 *     Die Systemspuren der neuen Diskette gleichen danach denen der Quelle.
 *  4. Neue Maschine, die neue Diskette in A:, Kaltstart: das BIOS „55 K“ meldet sich,
 *     der Autostart `rade` findet RADE.COM nicht (die Diskette ist leer) — Prompt.
 */
TEST(K8915Format, LeerdisketteFormatDisgenKaltstart)
{
    TempDisk a("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    TempDisk b = TempDisk::empty("k8915_format_b.hfe");
    {
        K8915Machine m;
        ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
        ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
        ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);

        FormatLauf f;
        f.verfahren = "24";
        const std::string bild = formatiere(m, f, 900'000'000);
        ASSERT_NE(bild.find("FUNCTION COMPLETE"), std::string::npos) << bild;
        EXPECT_EQ(bild.find("ERROR"), std::string::npos) << bild;
        EXPECT_NE(bild.find("76  77  78  79"), std::string::npos) << "alle 80 Zylinder\n" << bild;
        ASSERT_EQ(letzteZeile(m), "A>") << "EXIT ⇒ Warmstart\n" << vramLines(m);
        EXPECT_EQ(m.afs().debugState().waitSpuren, 160u) << "80 Zylinder × 2 Seiten";

        ASSERT_TRUE(befehl(m, "dir b:")) << vramLines(m);
        EXPECT_TRUE(enthaelt(m, "NO FILE")) << vramLines(m);
        EXPECT_FALSE(enthaelt(m, "ERR ON B")) << vramLines(m);

        // Gegenprobe ausserhalb des Emulators: formatiert, erkannt, ohne Befund.
        ASSERT_TRUE(m.flushDisks());
        {
            Kataloge k;
            std::string err;
            auto vol = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
            ASSERT_TRUE(vol) << err;
            EXPECT_EQ(vol->detection().format, "cpa800");
            EXPECT_TRUE(vol->hasFileSystem());
            EXPECT_TRUE(vol->list().empty());
            EXPECT_TRUE(vol->check(FsCheckLevel::Voll, true).ohneBefund());
        }

        // DISGEN: Systemspuren von A: lesen, auf B: schreiben.
        for (char c : std::string("disgen\r")) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
        ASSERT_TRUE(feld(m, "command: Read system tracks", 60'000'000)) << vramLines(m);
        tippe(m, "\r");
        ASSERT_TRUE(feld(m, "read system from device: A", 20'000'000)) << vramLines(m);
        tippe(m, "\r");
        ASSERT_TRUE(bisWeg(m, "read system from device", 60'000'000)) << vramLines(m);
        EXPECT_FALSE(enthaelt(m, "error")) << vramLines(m);
        ASSERT_TRUE(feld(m, "command: Read system tracks")) << vramLines(m);
        tippe(m, "W");
        ASSERT_TRUE(feld(m, "command: Write system tracks", 20'000'000)) << vramLines(m);
        tippe(m, "\r");
        ASSERT_TRUE(feld(m, "write system to device:", 20'000'000)) << vramLines(m);
        tippe(m, "B\r");
        ASSERT_TRUE(bisWeg(m, "write system to device", 60'000'000)) << vramLines(m);
        EXPECT_FALSE(enthaelt(m, "error")) << vramLines(m);
        EXPECT_FALSE(enthaelt(m, "not ready")) << vramLines(m);
        ASSERT_TRUE(feld(m, "command: Write system tracks")) << vramLines(m);
        tippe(m, "E");
        ASSERT_TRUE(feld(m, "command: Exit", 20'000'000)) << vramLines(m);
        tippe(m, "\r");
        ASSERT_TRUE(bisPrompt(m, 60'000'000)) << vramLines(m);
        ASSERT_TRUE(m.flushDisks());
    }

    // Die Systemspuren der neuen Diskette = die der Quelle (DISGEN ohne Änderung).
    {
        Kataloge k;
        std::string err;
        auto quelle = DiskVolume::open(a.path(), "", k.formate, k.fs, err);
        ASSERT_TRUE(quelle) << err;
        auto neu = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
        ASSERT_TRUE(neu) << err;
        EXPECT_EQ(neu->bootAreaSize(), quelle->bootAreaSize());
        std::vector<uint8_t> q, n;
        ASSERT_TRUE(quelle->readBootImage(q)) << quelle->lastError();
        ASSERT_TRUE(neu->readBootImage(n)) << neu->lastError();
        EXPECT_EQ(n, q) << "Systemspuren weichen ab";
        EXPECT_TRUE(neu->check(FsCheckLevel::Voll, true).ohneBefund());
    }

    // Kaltstart von der neuen Diskette.
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, b.path(), "cpa800", false)) << m.lastError();
    ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "55 K   SCPX 8915   BIOS-Version 5.3")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "RADE?")) << "Autostart ohne RADE.COM\n" << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "NO FILE")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/**
 * @test K8915Format.FormatDisketteGetBootPutBootetOhneDisgen
 * @brief AP-E5c: dieselbe Leerdiskette, aber statt DISGEN das DiskTool — FORMAT.COM
 *        (Verfahren 24) formatiert B:, `boot-get` holt die Systemspuren von A: (900),
 *        `boot-put` mit `--fs scpx8915` (ohne das Profil hat die frisch formatierte
 *        Diskette für das DiskTool keine Systemspuren) bringt sie auf B:.  Danach sind sie
 *        byteweise gleich der Quelle, und ein neuer Kaltstart von B: läuft bis `A>`.
 */
TEST(K8915Format, FormatDisketteGetBootPutBootetOhneDisgen)
{
    TempDisk a("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    TempDisk b = TempDisk::empty("k8915_format_bootput.hfe");
    {
        K8915Machine m;
        ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
        ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
        ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);
        FormatLauf f;
        f.verfahren = "24";
        const std::string bild = formatiere(m, f, 900'000'000);
        ASSERT_NE(bild.find("FUNCTION COMPLETE"), std::string::npos) << bild;
        ASSERT_EQ(bild.find("ERROR"), std::string::npos) << bild;
        ASSERT_TRUE(m.flushDisks());
    }

    Kataloge k;
    std::string err;
    std::vector<uint8_t> quelle;
    {
        auto v = DiskVolume::open(a.path(), "", k.formate, k.fs, err);
        ASSERT_TRUE(v) << err;
        ASSERT_TRUE(v->readBootImage(quelle)) << v->lastError();
    }
    {
        auto v = DiskVolume::open(b.path(), "scpx8915", k.formate, k.fs, err, /*read_only=*/false);
        ASSERT_TRUE(v) << err;
        ASSERT_TRUE(v->writeBootImage(quelle)) << v->lastError();
        ASSERT_TRUE(v->flush()) << v->lastError();
    }
    {
        auto v = DiskVolume::open(b.path(), "scpx8915", k.formate, k.fs, err);
        ASSERT_TRUE(v) << err;
        std::vector<uint8_t> n;
        ASSERT_TRUE(v->readBootImage(n)) << v->lastError();
        EXPECT_EQ(n, quelle) << "Systemspuren weichen von der Quelle ab";
        EXPECT_TRUE(v->check(FsCheckLevel::Voll, true).ohneBefund());
    }

    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, b.path(), "cpa800", false)) << m.lastError();
    ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "55 K   SCPX 8915   BIOS-Version 5.3")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "NO FILE")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

// ─────────────────────────────────────────────────────────────────────────────
// AP-E5a (doc/design/16_k8915.md §8a): frisch mit FORMAT.COM formatierte Disketten im
// k1520DiskTool — ohne Systemspuren, leeres Verzeichnis, dann Rundreise mit dem K8915.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct DtFall {
    const char* verfahren;
    const char* bootdiskette;   ///< deren DISGEN-Einstellung bestimmt B:
    int         sektoren;
    bool        fuenf_mal_1024;
};

void PrintTo(const DtFall& f, std::ostream* os) { *os << f.verfahren; }

class K8915FormatDiskTool : public ::testing::TestWithParam<DtFall> {};

std::unique_ptr<DiskVolume> dtOeffne(const std::string& p, const std::string& fs,
                                     bool schreiben = false) {
    static Kataloge k;
    std::string err;
    auto v = DiskVolume::open(p, fs, k.formate, k.fs, err, !schreiben);
    EXPECT_TRUE(v) << err;
    return v;
}

}  // namespace

/**
 * @test K8915FormatDiskTool.FrischFormatiertUndRundreise
 * @brief Verfahren 24/31 (5 × 1024, mit/ohne Indexmarke) auf Diskette 900 (B: = A:),
 *        22/37 (16 × 256) auf Diskette 901 (B: per DISGEN 16 × 256), Zylinder 00–79.
 *  1. Frisch formatiert, leer: 16 × 256 ⇒ OFF 2 / 128 Plätze (festes Offset der Regel);
 *     5 × 1024 ⇒ `cpa800` (192 ab c0h0 — vom Medium nicht von einer leeren CP/A-
 *     Datendiskette zu unterscheiden), mit dem Hinweis auf `--fs scpx8915`.
 *  2. Der K8915 schreibt (`save`): das DiskTool findet die Datei, OFF 2 / 128 Plätze,
 *     Vollprüfung ohne Befund.
 *  3. Das DiskTool schreibt (ohne `--fs`): der K8915 listet beide Dateien und `type`
 *     gibt die neue aus.
 */
TEST_P(K8915FormatDiskTool, FrischFormatiertUndRundreise)
{
    const DtFall c = GetParam();
    TempDisk a(c.bootdiskette);
    TempDisk b = TempDisk::empty(std::string("k8915_dt_fmt_") + c.verfahren + ".hfe");
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
    ASSERT_TRUE(kaltstartBisPrompt(m)) << vramLines(m);

    FormatLauf f;
    f.verfahren = c.verfahren;
    const std::string bild = formatiere(m, f, 900'000'000);
    ASSERT_NE(bild.find("FUNCTION COMPLETE"), std::string::npos) << bild;
    ASSERT_EQ(bild.find("ERROR"), std::string::npos) << bild;
    ASSERT_TRUE(m.flushDisks());
    {
        auto v = dtOeffne(b.path(), "");
        ASSERT_TRUE(v);
        EXPECT_TRUE(v->list().empty());
        EXPECT_TRUE(v->detection().unambiguous);
        EXPECT_TRUE(v->check(FsCheckLevel::Voll, true).ohneBefund());
        if (c.fuenf_mal_1024) {
            EXPECT_EQ(v->detection().filesystem, "cpa800");
            EXPECT_NE(v->detection().remarks.find("--fs scpx8915"), std::string::npos)
                << v->detection().remarks;
        } else {
            EXPECT_EQ(v->profile().data_cyl, 2);
            EXPECT_EQ(v->profile().dir_entries, 128);
        }
    }

    ASSERT_TRUE(befehl(m, "save 3 b:vomk8915.com")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
    ASSERT_TRUE(m.flushDisks());
    ASSERT_TRUE(m.unmountDisk(1));
    {
        auto v = dtOeffne(b.path(), "");
        ASSERT_TRUE(v);
        EXPECT_EQ(v->profile().data_cyl, 2) << v->detection().filesystem;
        EXPECT_EQ(v->profile().dir_entries, 128) << v->detection().filesystem;
        ASSERT_EQ(v->list().size(), 1u) << v->detection().remarks;
        EXPECT_EQ(v->list().front().name, "VOMK8915.COM");
        EXPECT_TRUE(v->check(FsCheckLevel::Voll, true).ohneBefund());
    }
    {
        auto v = dtOeffne(b.path(), "", /*schreiben=*/true);
        ASSERT_TRUE(v);
        TempDisk q = TempDisk::empty("k8915_dt_fmt_quelle.txt");
        { std::ofstream(q.path(), std::ios::binary) << "VOM DISKTOOL\n"; }
        TransferOptions o;
        o.text = true;
        ASSERT_TRUE(v->insert(q.path(), FileRef::parse("VOMDT.TXT"), o)) << v->lastError();
        ASSERT_TRUE(v->flush()) << v->lastError();
    }
    ASSERT_TRUE(m.mountDisk(1, b.path(), "cpa800", false)) << m.lastError();
    m.keyboard().sendeZeichen(0x03);   // Warmstart: Laufwerke neu anmelden
    ASSERT_TRUE(bisPrompt(m, 30'000'000)) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir b:")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOMK8915 COM")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOMDT    TXT")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "type b:vomdt.txt")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOM DISKTOOL")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

INSTANTIATE_TEST_SUITE_P(
    Verfahren, K8915FormatDiskTool,
    ::testing::Values(
        DtFall{"24", "k8915scpx_cpa800_k5601_bios55k-disk900.hfe", 5, true},
        DtFall{"31", "k8915scpx_cpa800_k5601_bios55k-disk900.hfe", 5, true},
        DtFall{"22", "k8915scpx_boot1.hfe", 16, false},
        DtFall{"37", "k8915scpx_boot1.hfe", 16, false}),
    [](const ::testing::TestParamInfo<DtFall>& i) { return std::string("V") + i.param.verfahren; });
