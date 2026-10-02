/**
 * @file test_prg710_format_boot.cpp
 * @brief PRG 710 / 710-1 (doc/design/20_prg710.md AP-P5f): die TIEFE der FORMAT-Wächter
 *        (`format_integration`, `tools/dev.sh test-format`) — Leerdiskette → UDOS-`FORMAT`
 *        beider Seiten mit `SYSTEMDISK? Y` → Systemdateien kopieren → Kaltstart von der
 *        neuen Diskette bis zum `%`; dazu SCPX-`FORMAT.COM` mit 5 × 1024.  Die schnellen
 *        Fälle stehen in `tests/integration/test_prg710_format.cpp`.
 *
 * **Warum beide Seiten:** am PRG ist Laufwerkstyp 5 (DISKCON 51H) zweiseitig — Laufwerk
 * 1 und 5 sind die zwei Seiten derselben Diskette, und UDOS liest beim ersten Zugriff auf
 * die Diskette (Start, `STATUS`, `COPY`) auch die andere Seite.  Eine Seite ohne Marken
 * hängt seit AP-P3b nicht mehr (Rauschen, `K5122::setRauschenAufLeererSpur`), kostet aber
 * je Zugriff die Fehlerwiederholungen des Residenten; eine Systemdiskette vom Typ 5 hat
 * ohnehin zwei Seiten (wie `FORMAT` am Gerät).  `FORMAT` selbst schreibt immer nur eine Seite (Laufwerk 0–3 = Seite 0,
 * 4–7 = Seite 1).
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/temp_path.h"
#include "tests/system/prg710_bedienung.h"

namespace fs = std::filesystem;
using namespace prg710test;

namespace {

/// Temporäre Datei samt Sicherungskopie, räumt sich weg.
class TempPfad {
public:
    explicit TempPfad(const std::string& name) : pfad_(k1520test::tempPath(name)) { weg(); }
    ~TempPfad() { weg(); }
    const std::string& get() const { return pfad_; }
private:
    void weg() {
        std::error_code ec;
        fs::remove(pfad_, ec);
        fs::remove(pfad_ + "~", ec);
    }
    std::string pfad_;
};

std::unique_ptr<Prg710Machine> maschine(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return std::make_unique<Prg710Machine>(c);
}

}  // namespace

class Prg710FormatBoot : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test Prg710FormatBoot.UdosBautBootfaehigeSystemdiskette
 * @brief Gerätediskette in Laufwerk 0, nach dem Start eine Leerdiskette in Laufwerk 1:
 *        `FORMAT` SYSTEMDISK? Y / DRIVE? 1 / ID? SYSDISK (Seite 0 samt Bootsektor,
 *        Zweitlader und BOOT-Modul von Laufwerk 0), dann N / 5 / SYSDISK.B (Seite 1).
 *        `STATUS` zeigt beide Seiten (Systemseite 47 belegt, Datenseite 14), `COPY`
 *        bringt `OS`, `ZDOS`, `OS.INIT` und die Kommandos, die `OS.INIT` braucht
 *        (`OS.INIT` ist eine Textdatei (Typ A2) und läuft über `DO`; darin `DATE`), dazu
 *        `STATUS`.  Kaltstart einer
 *        neuen Maschine von dieser Diskette → Datumsabfrage → Systemzeile → `%`, und
 *        `STATUS` meldet die eigene Diskette als `SYSDISK`/`SYSDISK.B`.
 */
TEST_P(Prg710FormatBoot, UdosBautBootfaehigeSystemdiskette) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    TempPfad neu(std::string("prg710_format_sysdisk_") + name(GetParam()) + ".hfe");
    {
        auto m = maschine(GetParam());
        ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
        ASSERT_TRUE(udosBisDatum(*m)) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
        tippe(*m, "021086");
        ASSERT_TRUE(bisUdosPrompt(*m, kBefehl)) << bild(*m);

        ASSERT_TRUE(m->createDisk(1, neu.get(), "", false)) << m->lastError();
        ASSERT_TRUE(udosFormat(*m, "Y", "1", "SYSDISK")) << bild(*m);
        ASSERT_TRUE(udosFormat(*m, "N", "5", "SYSDISK.B")) << bild(*m);
        EXPECT_EQ(bild(*m).find("DEFEKTIVE TRACK"), std::string::npos) << bild(*m);
        EXPECT_EQ(bild(*m).find("SYSTEMCOPY NOT POSSIBLE"), std::string::npos) << bild(*m);

        ASSERT_TRUE(udos(*m, "STATUS")) << bild(*m);
        const std::string status = bild(*m);
        EXPECT_NE(status.find("DRIVE 1   SYSDISK\n  47 SECTORS USED"), std::string::npos) << status;
        EXPECT_NE(status.find("DRIVE 5   SYSDISK.B\n  14 SECTORS USED"), std::string::npos) << status;

        for (const char* f : {"OS", "ZDOS", "OS.INIT", "DO", "DATE", "STATUS"}) {
            ASSERT_TRUE(udos(*m, std::string("COPY ") + f + " 1/" + f, kFormat)) << bild(*m);
            EXPECT_EQ(vorletzteZeile(*m), std::string("%COPY ") + f + " 1/" + f) << bild(*m);
        }
        ASSERT_TRUE(m->flushDisks()) << m->lastError();
    }

    auto m = maschine(GetParam());
    ASSERT_TRUE(m->mountDisk(0, neu.get(), m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(udosBisDatum(*m)) << "Die neue Systemdiskette bootet nicht, PC=" << std::hex
                                  << m->cpuPC() << "\n" << bild(*m);
    tippe(*m, "021086");
    ASSERT_TRUE(bisUdosPrompt(*m, kBefehl)) << bild(*m);
    EXPECT_EQ(vorletzteZeile(*m), udosSystemzeile(GetParam())) << bild(*m);
    EXPECT_EQ(bild(*m).find("NONEXISTENT COMMAND"), std::string::npos) << bild(*m);
    ASSERT_TRUE(udos(*m, "STATUS")) << bild(*m);
    EXPECT_NE(bild(*m).find("DRIVE 0   SYSDISK\n"), std::string::npos) << bild(*m);
    EXPECT_NE(bild(*m).find("DRIVE 4   SYSDISK.B\n"), std::string::npos) << bild(*m);
}

/**
 * @test Prg710FormatBoot.ScpxFormatFuenfMal1024
 * @brief SCPX-`FORMAT.COM` mit Format 3 (DD-DS 5 × 1024) auf eine Leerdiskette in B: —
 *        eine Systemkopie bietet FORMAT dabei nicht an.  Das DiskTool erkennt danach
 *        `scpx798` (wie die PRG-Abzüge, §5.4) mit leerem Verzeichnis und ohne
 *        unformatierte Spur.
 */
TEST_P(Prg710FormatBoot, ScpxFormatFuenfMal1024) {
    k1520test::TempDisk disk(scpxDiskette(GetParam()));
    TempPfad neu(std::string("prg710_format_scpx800_") + name(GetParam()) + ".hfe");
    auto m = maschine(GetParam());
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    m->powerOn();
    lauf(*m, 3'000'000);
    taste(*m, QK_RETURN);
    ASSERT_TRUE(bisScpxPrompt(*m, kBoot)) << bild(*m);

    ASSERT_TRUE(m->createDisk(1, neu.get(), "", false)) << m->lastError();
    ASSERT_TRUE(scpxFormatB(*m, '3', false)) << bild(*m);
    EXPECT_NE(bild(*m).find("FORMATTING COMPLETE"), std::string::npos) << bild(*m);
    EXPECT_EQ(bild(*m).find("DEFEKTIV"), std::string::npos) << bild(*m);
    ASSERT_TRUE(m->flushDisks()) << m->lastError();

    std::string f;
    const FormatCatalog formate = FormatCatalog::loadDefault(&f);
    ASSERT_TRUE(f.empty()) << f;
    const FsCatalog dateisysteme = FsCatalog::loadDefault(formate, &f);
    ASSERT_TRUE(f.empty()) << f;
    std::string err;
    auto dv = DiskVolume::open(neu.get(), "", formate, dateisysteme, err);
    ASSERT_NE(dv, nullptr) << err;
    EXPECT_EQ(dv->detection().format, "scpx798") << dv->detection().remarks;
    EXPECT_EQ(dv->detection().remarks.find("unformatiert"), std::string::npos)
        << dv->detection().remarks;
    EXPECT_TRUE(dv->list().empty());
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710FormatBoot, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return std::string(name(i.param)); });
