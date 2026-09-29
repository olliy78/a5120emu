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
