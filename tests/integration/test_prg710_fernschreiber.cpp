/**
 * @file test_prg710_fernschreiber.cpp
 * @brief PRG 710-1 (doc/design/20_prg710.md AP-P8c): SCPX 1526 V1.7 mit dem Fernschreiber-
 *        BIOS `B17272FS` druckt über die ASS 590069 (SIO-Kanal B, C5H/C7H) — der Anschluss
 *        „Fernschreiber“ des SerialHub fängt den Text in einer Datei.
 *
 * Die Systemdiskette entsteht im Test wie mit `k1520disktool boot-scpx` (AP-P6): `SYL17` +
 * `CCPBD17` + `B17272FS` von der Gerätediskette `prg710-1_scpx17_cpa640_boot.hfe`.  Gedruckt
 * wird mit ^P (Druckerecho des CCP) und `DIR`.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/prg_boot.h"
#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/serial/hub.h"
#include "tests/support/fixtures.h"
#include "tests/support/temp_path.h"
#include "tests/system/prg710_bedienung.h"

namespace fs = std::filesystem;
using namespace prg710test;
using namespace k1520::serial;

namespace {

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        return FormatCatalog::loadDefault(&f);
    }();
    return c;
}
const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        return FsCatalog::loadDefault(formate(), &f);
    }();
    return c;
}

std::vector<uint8_t> datei(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

std::vector<uint8_t> holeDatei(DiskVolume& dv, const std::string& name) {
    const std::string ziel = k1520test::tempPath("k1520_fs_modul.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv.extract(ref, ziel, TransferOptions{})) << dv.lastError();
    auto d = datei(ziel);
    std::error_code ec;
    fs::remove(ziel, ec);
    return d;
}

/// SCPX-Systemdiskette des 710-1 mit dem BIOS @p bios (Systemspuren aus den Modulen).
bool baueScpxDiskette(const std::string& ziel, const char* bios, std::string& err) {
    k1520test::TempDisk quelle("prg710-1_scpx17_cpa640_boot.hfe");
    auto q = DiskVolume::open(quelle, "scpx640", formate(), dateisysteme(), err);
    if (!q) return false;
    std::vector<uint8_t> band;
    prg_boot::Variante erkannt = prg_boot::Variante::Unbekannt;
    if (!prg_boot::scpxBand(holeDatei(*q, "SYL17.SYS"), holeDatei(*q, "CCPBD17.SYS"),
                            holeDatei(*q, bios), prg_boot::Variante::Prg710_1, band, erkannt, err))
        return false;
    const std::string bootbin = k1520test::tempPath("k1520_fs_band.bin");
    {
        std::ofstream f(bootbin, std::ios::binary);
        f.write(reinterpret_cast<const char*>(band.data()), static_cast<std::streamsize>(band.size()));
    }
    auto neu = DiskVolume::create(ziel, "scpx640", "", formate(), dateisysteme(), err, bootbin);
    std::error_code ec;
    fs::remove(bootbin, ec);
    if (!neu) return false;
    return neu->flush();
}

}  // namespace

/**
 * @test Prg710Fernschreiber.ScpxDrucktUeberDieAss590069
 * @brief Kaltstart von der FS-Systemdiskette bis `A>`, ^P, `DIR`, ^P: der Anschluss
 *        „Fernschreiber“ (Wandler „Datei“) bekommt das Kommando und das Verzeichnis als
 *        Text; die Leitung ist so programmiert wie das BIOS es tut (100 Bd, 5 Bit, 1½ Stopp).
 */
TEST(Prg710Fernschreiber, ScpxDrucktUeberDieAss590069) {
    const std::string disk = k1520test::tempPath("k1520_prg_fs.hfe");
    const std::string pfad = k1520test::tempPath("k1520_prg_fernschreiber.txt");
    std::error_code ec;
    fs::remove(disk, ec);
    fs::remove(fs::u8path(pfad), ec);
    std::string err;
    ASSERT_TRUE(baueScpxDiskette(disk, "B17272FS.SYS", err)) << err;

    Prg710Machine::Config cfg;
    cfg.variante = V::Prg710_1;
    Prg710Machine m(cfg);
    // Der Fernschreiber hängt hinter den K8025-Anschlüssen (710-1: Index 2).
    const auto an = m.serielleAnschluesse();
    ASSERT_EQ(an.size(), 3u);
    const int fsIdx = 2;
    EXPECT_STREQ(an[fsIdx]->name(), "Fernschreiber");
    SerialKonfig k = m.serialHub()->konfig(fsIdx);
    k.betriebsart = Betriebsart::Datei;
    k.datei = pfad;
    k.loop = false;
    ASSERT_TRUE(m.serialHub()->konfigurieren(fsIdx, k));
    ASSERT_TRUE(m.serialHub()->start(fsIdx));

    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    lauf(m, 3'000'000);
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, scpxGruss(V::Prg710_1), kBoot)) << bild(m);
    ASSERT_TRUE(bisScpxPrompt(m, kBefehl)) << bild(m);

    m.keyPress('p', false, true);   // ^P: Druckerecho an
    lauf(m, 100'000);
    m.keyRelease('p');
    lauf(m, 100'000);
    ASSERT_TRUE(scpx(m, "DIR")) << bild(m);
    lauf(m, 60'000'000);             // 100 Bd ≈ 75 ms je Zeichen: Rest der Leitung abwarten
    m.serialHub()->stop(fsIdx);

    const auto roh = datei(pfad);
    const std::string aus(roh.begin(), roh.end());
    EXPECT_NE(aus.find("DIR"), std::string::npos) << "[" << aus << "]";
    // Die Diskette trägt nur die Systemspuren; '>' gibt es in ITA2 nicht — das BIOS
    // schickt dafür einen Zwischenraum (Tabellensuche erfolglos → 04H, E332H).
    EXPECT_NE(aus.find("NO FILE"), std::string::npos) << "[" << aus << "]";
    EXPECT_NE(aus.find("\r\nA "), std::string::npos) << "[" << aus << "]";
    EXPECT_NE(aus.find("\r\n"), std::string::npos) << "Wagenrücklauf + Zeilenvorschub";

    const auto f = an[fsIdx]->format();
    EXPECT_TRUE(f.gueltig);
    EXPECT_EQ(f.baud_nenn, 100u);
    fs::remove(disk, ec);
    fs::remove(fs::u8path(pfad), ec);
}
