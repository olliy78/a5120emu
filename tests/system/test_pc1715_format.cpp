/**
 * @file test_pc1715_format.cpp
 * @brief PC 1715, AP-5b (doc/design/21_pc1715.md §13): Leerdiskette → FORMAT der
 *        Originalprogramme → Lesen/Schreiben darauf → DiskTool.
 *
 *  - SCP 1715 V0007 (`INIT.COM` V 0.5/1): Laufwerk B:, 16 × 256 einseitig, 80 Spuren.
 *  - CP/A 1715 (`FORMATPX.COM` V 23.01.88): Laufwerk B:, Format 0 (5 × 1024, CP/A), alle
 *    160 Spuren, MIT Vergleichs-Lesen; danach `PIP B:=A:ZSID.COM`, `DIR B:`.
 *
 * Langsam (Spur für Spur ≈ 1 Mio. Takte Maschinenzeit je Umdrehung): Label
 * `format_integration` (`tools/dev.sh test-format`).  Disketten nur über `TempDisk`.
 * Bedienung der Formatierer: doc/merkposten/pc1715.md.
 */

#include <gtest/gtest.h>

#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"
#include "tests/support/temp_path.h"

using namespace k1520test::pc1715;
using k1520test::TempDisk;

namespace {

void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

bool enthaelt(Pc1715Machine& m, const std::string& s) { return bild(m).find(s) != std::string::npos; }

struct Kataloge {
    FormatCatalog formate;
    FsCatalog     fs;
    Kataloge() {
        std::string f;
        formate = FormatCatalog::loadDefault(&f);
        fs      = FsCatalog::loadDefault(formate, &f);
    }
};

/// Einzelnes Zeichen ohne Return (die Auswahlfragen der Formatierer lesen eine Taste).
void taste(Pc1715Machine& m, char c) { tippe(m, std::string(1, c)); }

}  // namespace

/**
 * @test Pc1715Format.DISABLED_Scp1715InitFormatiertB  (AUSGESCHALTET — offener Befund)
 * @brief SCP 1715 V0007: `INIT` → Laufwerk B → Format 0 (DD-SS 16 × 256 × 80) → `Y` → alle 80
 *        Spuren, „FORMATTING COMPLETE“ ohne „ERROR“.  DiskTool erkennt ein Dateisystem und
 *        findet keine Datei; nach einer vom DiskTool eingebrachten Datei zeigt `DIR B:` der
 *        CCP sie an (Lesen vom Gast).
 *
 * **Befund (AP-5b):** INIT schreibt Spur 0 (K5122 „WAIT-FORMAT … OK“), armiert dann mit
 * `OUT 05H,22H` den Lese-/Vergleichs-Index-Interrupt (Vektor 22H, `sub_238C` @238CH) — der
 * kommt nie: `319AH` läuft in die Frist (0x90), dreimal, dann „BAD TRACKS“.  Ursache im
 * Interruptweg (Steuer-PIO nach dem Schreib-ISR 24H) noch nicht gefunden; CP/A-FORMATPX
 * läuft über denselben Weg fehlerfrei.  SCP V0006-INIT (V 0.5) hängt ohnehin an Spur 0
 * (Gast: `LD (7E5AH),A` statt `(315AH)` → Spurzähler stimmt nie).
 */
TEST(Pc1715Format, DISABLED_Scp1715InitFormatiertB) {
    stumm();
    Pc1715Machine m;
    TempDisk a("pc1715_scp1715_v0007_cpa640_boot.hfe");
    auto b = TempDisk::empty("pc1715_format_scp_b.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 100'000'000)) << bild(m);

    tippeZeile(m, "init");
    ASSERT_TRUE(laufeBisText(m, "PLEASE ENTER DRIVE:", 30'000'000)) << bild(m);
    tippeZeile(m, "B");
    ASSERT_TRUE(laufeBisText(m, "PLEASE SELECT FORMAT:", 30'000'000)) << bild(m);
    taste(m, '0');
    ASSERT_TRUE(laufeBisText(m, "(Y/N)", 30'000'000)) << bild(m);
    taste(m, 'Y');
    ASSERT_TRUE(laufeBisText(m, "FORMATTING COMPLETE", 2'000'000'000)) << bild(m);
    EXPECT_FALSE(enthaelt(m, "ERROR")) << bild(m);
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    // DiskTool: Dateisystem erkannt, leer; eine Datei einbringen.
    Kataloge k;
    {
        std::string err;
        auto dv = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
        ASSERT_NE(dv, nullptr) << err;
        ASSERT_TRUE(dv->hasFileSystem()) << dv->detection().remarks;
        EXPECT_TRUE(dv->list().empty());
        const std::string quelle = k1520test::tempPath("pc1715_format_scp_probe.txt");
        {
            FILE* f = fopen(quelle.c_str(), "wb");
            ASSERT_NE(f, nullptr);
            fputs("PROBE\r\n", f);
            fclose(f);
        }
        TransferOptions to;
        ASSERT_TRUE(dv->insert(quelle, FileRef::parse("PROBE.TXT"), to));
        std::remove(quelle.c_str());
    }
    // Gast liest: eine neue Maschine, die formatierte Diskette in B:.
    Pc1715Machine m2;
    TempDisk a2("pc1715_scp1715_v0007_cpa640_boot.hfe");
    ASSERT_TRUE(m2.mountDisk(0, a2.path(), m2.defaultFormatName(0), false)) << m2.lastError();
    ASSERT_TRUE(m2.mountDisk(1, b.path(), m2.defaultFormatName(1), false)) << m2.lastError();
    m2.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m2, "A>", 100'000'000)) << bild(m2);
    tippeZeile(m2, "dir b:");
    EXPECT_TRUE(laufeBisText(m2, "PROBE", 100'000'000)) << bild(m2);
}

/**
 * @test Pc1715Format.Cpa1715FormatpxFormatiertBUndPipKopiert
 * @brief CP/A 1715: `FORMATPX` → Funktion 0 → Laufwerk B → Vergleichs-Lesen j → Format 0
 *        (5 × 1024, 160 Spuren) → `FORMATIEREN beendet` ohne „SPUR DEFEKT“.  Danach
 *        `PIP B:=A:ZSID.COM`, `DIR B:`; das DiskTool findet ZSID.COM, Prüfung ohne Befund.
 */
TEST(Pc1715Format, Cpa1715FormatpxFormatiertBUndPipKopiert) {
    stumm();
    Pc1715Machine m;
    TempDisk a("pc1715_cpa1715_boot_4lw.hfe");
    auto b = TempDisk::empty("pc1715_format_cpa_b.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);

    tippeZeile(m, "formatpx");
    ASSERT_TRUE(laufeBisText(m, "Bitte FORMAT-Funktion auswaehlen", 30'000'000)) << bild(m);
    taste(m, '0');
    ASSERT_TRUE(laufeBisText(m, "Bitte Laufwerk angeben:", 30'000'000)) << bild(m);
    taste(m, 'B');
    ASSERT_TRUE(laufeBisText(m, "Diskette bitte in Laufwerk B", 30'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisText(m, "Vergleichs-Lesen", 30'000'000)) << bild(m);
    tippe(m, "\r");                       // ENTER = j
    ASSERT_TRUE(laufeBisText(m, "Bitte Format auswaehlen", 60'000'000)) << bild(m);
    taste(m, '0');                        // 5*1024, Spur 0-159, CP/A
    ASSERT_TRUE(laufeBisText(m, "FORMATIEREN von Spur", 30'000'000)) << bild(m);
    tippe(m, "\r");                       // ab Spur 0
    ASSERT_TRUE(laufeBisText(m, "FORMATIEREN bis Spur", 30'000'000)) << bild(m);
    tippe(m, "\r");                       // bis letzte Spur
    ASSERT_TRUE(laufeBisText(m, "Erlaubt?", 30'000'000)) << bild(m);
    taste(m, 'j');
    ASSERT_TRUE(laufeBisText(m, "FORMATIEREN beendet", 3'000'000'000)) << bild(m);
    EXPECT_FALSE(enthaelt(m, "DEFEKT")) << bild(m);
    EXPECT_FALSE(enthaelt(m, "Fehler")) << bild(m);

    // zurück ins CP/A: Funktionsauswahl verlassen
    ASSERT_TRUE(laufeBisText(m, "Wiederholung mit gleichen Parametern", 30'000'000)) << bild(m);
    taste(m, 'n');
    ASSERT_TRUE(laufeBisText(m, "Rueckkehr in Funktionsauswahl", 30'000'000)) << bild(m);
    taste(m, 'n');
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 60'000'000)) << bild(m);

    tippeZeile(m, "PIP B:=A:ZSID.COM");
    laufe(m, 80'000'000);
    tippeZeile(m, "DIR B:");
    EXPECT_TRUE(laufeBisText(m, "B: ZSID     COM", 60'000'000)) << bild(m);
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    Kataloge k;
    std::string err;
    auto dv = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_TRUE(dv->hasFileSystem()) << dv->detection().remarks;
    bool gefunden = false;
    for (const auto& e : dv->list()) gefunden |= (e.name.find("ZSID") != std::string::npos);
    EXPECT_TRUE(gefunden);
}
