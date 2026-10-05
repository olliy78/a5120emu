/**
 * @file test_pc1715_format.cpp
 * @brief PC 1715, AP-5b/AP-5d (doc/design/21_pc1715.md §13): Leerdiskette → FORMAT der
 *        Originalprogramme → Lesen/Schreiben darauf → DiskTool.
 *
 *  - SCP 1715 V0007 (`INIT.COM` V 0.5/1): Laufwerk B:, 16 × 256 doppelseitig, 80 Zylinder
 *    (mit Bedienpause vor `Y`, s. dort); danach `SAVE`/`DIR B:`.
 *  - CP/A 1715 (`FORMATPX.COM` V 23.01.88): Laufwerk B:, Format 0 (5 × 1024, CP/A), alle
 *    160 Spuren, MIT Vergleichs-Lesen; danach `PIP B:=A:ZSID.COM`, `DIR B:`.
 *  - UDOS 1715 (`FORMAT`): Laufwerk 1, 80 × 32 × 256, Anwenderdiskette; danach `COPY`/`CAT`.
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
 * @test Pc1715Format.Scp1715InitFormatiertB
 * @brief SCP 1715 V0007: `INIT` → Laufwerk B → Format 3 (DD-DS 16 × 256 × 80) → `Y` → alle
 *        Spuren, „FORMATTING COMPLETE“ ohne „ERROR“.  Danach schreibt der Gast mit dem
 *        CCP-Kommando `SAVE 2 B:SAVED.COM` (V0007 hat kein PIP) und liest mit `DIR B:`;
 *        das DiskTool erkennt `scpx640`, findet SAVED.COM und bringt PROBE.TXT ein, die
 *        eine frisch gebootete Maschine mit `DIR B:` sieht.
 *
 * **Bedienpause vor `Y` (AP-5d, Gastverhalten, kein Emulatorfehler):** das SCP-BIOS zählt
 * im CTC0-K3-Interrupt (26,6 ms) ab dem letzten BIOS-Diskettenzugriff 96 Schläge herunter
 * (`F7BDH`, ≈ 2,55 s) und schaltet dann alle Laufwerke ab (`OUT 20H/21H := FFH`) und setzt
 * die Steuer-PIO zurück (Vektor 10H, Interrupt gesperrt).  INIT wählt Laufwerk und Motor
 * und programmiert die PIO nur EINMAL zu Beginn.  Tippt der Test so schnell wie ein
 * Automat, fällt die Abschaltung in das Vergleichs-Lesen der Spur 0 — der Index-Interrupt
 * (Vektor 22H) kommt nie, „BAD TRACKS“.  Ein Mensch braucht für die drei Fragen länger;
 * danach feuert der Zähler bis zum nächsten BIOS-Zugriff nicht wieder.
 *
 * **Format 3, nicht 0:** das SCP-1715-BIOS führt B: fest doppelseitig wie A: (16 × 256,
 * Verzeichnis c2h0).  Eine mit Format 0 (DD-SS) formatierte Diskette meldet beim ersten
 * Zugriff `BIOS ERR ON: B  T: 02  S: 17` (Kopf 1 fehlt) — ebenfalls Gast, nicht Emulator.
 */
TEST(Pc1715Format, Scp1715InitFormatiertB) {
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
    taste(m, '3');                        // DD - DS 16 * 256 * 80
    ASSERT_TRUE(laufeBisText(m, "(Y/N)", 30'000'000)) << bild(m);
    laufe(m, 8'000'000);                  // Bedienpause: BIOS-Abschaltzeit verstreichen lassen
    taste(m, 'Y');
    ASSERT_TRUE(laufeBisText(m, "FORMATTING COMPLETE", 3'000'000'000)) << bild(m);
    EXPECT_FALSE(enthaelt(m, "ERROR")) << bild(m);
    EXPECT_FALSE(enthaelt(m, "BAD")) << bild(m);

    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    // Gast schreibt und liest B: — INIT fragt nach COMPLETE wieder nach dem Laufwerk (ein
    // Ausgang ist nicht bedienbar), also eine frisch gebootete Maschine.
    Pc1715Machine m1;
    TempDisk a1("pc1715_scp1715_v0007_cpa640_boot.hfe");
    ASSERT_TRUE(m1.mountDisk(0, a1.path(), m1.defaultFormatName(0), false)) << m1.lastError();
    ASSERT_TRUE(m1.mountDisk(1, b.path(), m1.defaultFormatName(1), false)) << m1.lastError();
    m1.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m1, "A>", 100'000'000)) << bild(m1);
    tippeZeile(m1, "save 2 b:saved.com");
    ASSERT_TRUE(laufeBisPrompt(m1, "A>", 60'000'000)) << bild(m1);
    tippeZeile(m1, "dir b:");
    EXPECT_TRUE(laufeBisText(m1, "SAVED", 60'000'000)) << bild(m1);
    EXPECT_FALSE(enthaelt(m1, "BIOS ERR")) << bild(m1);
    ASSERT_TRUE(m1.flushDisks()) << m1.lastError();

    // DiskTool: Dateisystem erkannt, SAVED.COM gefunden; eine Datei einbringen.
    Kataloge k;
    {
        std::string err;
        auto dv = DiskVolume::open(b.path(), "", k.formate, k.fs, err, /*read_only=*/false);
        ASSERT_NE(dv, nullptr) << err;
        ASSERT_TRUE(dv->hasFileSystem()) << dv->detection().remarks;
        EXPECT_EQ(dv->detection().filesystem, "scpx640") << dv->detection().remarks;
        bool gefunden = false;
        for (const auto& e : dv->list()) gefunden |= (e.name.find("SAVED") != std::string::npos);
        EXPECT_TRUE(gefunden);
        const std::string quelle = k1520test::tempPath("pc1715_format_scp_probe.txt");
        {
            FILE* f = fopen(quelle.c_str(), "wb");
            ASSERT_NE(f, nullptr);
            fputs("PROBE\r\n", f);
            fclose(f);
        }
        TransferOptions to;
        const bool ok = dv->insert(quelle, FileRef::parse("PROBE.TXT"), to);
        std::remove(quelle.c_str());
        ASSERT_TRUE(ok) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
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
    EXPECT_TRUE(enthaelt(m2, "SAVED")) << bild(m2);
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

/**
 * @test Pc1715Format.Udos1715FormatiertBUndKopiert
 * @brief UDOS 1715 (AP-5d): `FORMAT` → Drive `1` → 80 Spuren `J` → doppelseitig `J` →
 *        Disk-ID `TEST` → Systemdisk `N` → Bereit `J` (jede Antwort mit Return) auf einer
 *        echten Leerdiskette, alle 80 Zylinder mit Kontrolllesen, zurück am `%`.  Danach
 *        `COPY TERMINE 1/TERMINE` und `CAT D=1`; das DiskTool erkennt `udos1715`, findet
 *        TERMINE, die Vollprüfung ist ohne Befund.
 *
 * Bedienung aus `HELP FORMAT` des Systems.  Die Leerdiskette in B: meldet UDOS beim Start
 * als `DISK INITIALIZATION ERROR C4` (Laufwerk wird angelesen) und kommt trotzdem an `%`.
 */
TEST(Pc1715Format, Udos1715FormatiertBUndKopiert) {
    stumm();
    Pc1715Machine m;
    TempDisk a("pc1715_udos1715_system.hfe");
    auto b = TempDisk::empty("pc1715_format_udos_b.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "%", 120'000'000)) << bild(m);

    tippeZeile(m, "format");
    ASSERT_TRUE(laufeBisText(m, "Drive :", 40'000'000)) << bild(m);
    tippeZeile(m, "1");
    ASSERT_TRUE(laufeBisText(m, "80 Spuren ?", 30'000'000)) << bild(m);
    tippeZeile(m, "J");
    ASSERT_TRUE(laufeBisText(m, "Doppelseitig ?", 30'000'000)) << bild(m);
    tippeZeile(m, "J");
    ASSERT_TRUE(laufeBisText(m, "Disk-ID :", 30'000'000)) << bild(m);
    tippeZeile(m, "TEST");
    ASSERT_TRUE(laufeBisText(m, "Systemdisk ?", 30'000'000)) << bild(m);
    tippeZeile(m, "N");
    ASSERT_TRUE(laufeBisText(m, "Bereit ?", 30'000'000)) << bild(m);
    tippeZeile(m, "J");
    ASSERT_TRUE(laufeBisText(m, "Formatieren  Spur", 30'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "%", 1'500'000'000)) << bild(m);
    EXPECT_FALSE(enthaelt(m, "Formatieren  Spur")) << bild(m);   // Zeile wird am Ende ersetzt

    // CAT ist auf der Systemdiskette geheim ('S') und erschiene im Katalog nicht — TERMINE nicht.
    tippeZeile(m, "copy termine 1/termine");
    ASSERT_TRUE(laufeBisText(m, "%COPY TERMINE 1/TERMINE", 10'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "%", 100'000'000)) << bild(m);
    tippeZeile(m, "cat d=1");
    ASSERT_TRUE(laufeBisText(m, "%CAT D=1", 10'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "%", 100'000'000)) << bild(m);
    const std::string nachher = bild(m).substr(bild(m).find("%COPY TERMINE"));
    EXPECT_NE(nachher.find(" TERMINE            1"), std::string::npos) << bild(m);
    EXPECT_EQ(nachher.find("ERROR"), std::string::npos) << bild(m);
    EXPECT_EQ(nachher.find("NOT FOUND"), std::string::npos) << bild(m);
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    Kataloge k;
    std::string err;
    auto dv = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
    ASSERT_NE(dv, nullptr) << err;
    ASSERT_TRUE(dv->hasFileSystem()) << dv->detection().remarks;
    EXPECT_EQ(dv->detection().filesystem, "udos1715") << dv->detection().remarks;
    bool gefunden = false;
    for (const auto& e : dv->list()) gefunden |= (e.name == "TERMINE");
    EXPECT_TRUE(gefunden);
    EXPECT_TRUE(dv->check(FsCheckLevel::Voll, true).ohneBefund());
}
