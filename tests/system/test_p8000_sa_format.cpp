/**
 * @file test_p8000_sa_format.cpp
 * @brief P8000 Meilenstein M3, Anfang (doc/design/25_p8000.md §10.11 AP P13d): WDC mit echter
 *        Firmware 4.2 an der PIO2 der 16-Bit-Karte, `sa.format`/`sa.verify` von der
 *        WEGA-Startdiskette formatieren und prüfen eine `TempPlatte`.
 *
 * Soll = `~/projects/robotron/P8000/doc/install_WEGA_3.1.log` Z. 1–80:
 *   `O U` ⇒ „BOOTING FROM UDOS FLOPPY" ⇒ `> boot` ⇒ `:` ⇒ `ud(0,0)sa.format` ⇒
 *   „>>>  Format Hard-Disk 4.1  <<<", „Firmwareversion 'WDC_4.2'", Laufwerksliste, „Which Typ ?" 4 …
 * Die Platte ist wie im Protokoll formatiert, aber ohne PAR/BTT-Sektor („Error in PAR&BTT"): die
 * `TempPlatte` bekommt Z0/K0/S1 = E5, die Spursynthese ergänzt ihn nicht (`platte_par_ergaenzen`).
 * Deshalb meldet auch der Hardwaretest U8001 `*** ERROR 52   39` (Init-Fehler 38 + LW 0) — das
 * Protokoll zeigt den Hardwaretest nicht.  `sa.format`/`sa.verify` 4.1 stehen nicht auf der
 * WEGA-3.0-Startdiskette (dort V1.4 für Firmware 3.x) und kommen aus tests/fixtures/p8000/.
 * Abweichung (zugelassen): die drei BTT-Einträge des Protokolls stammen vom echten Datenträger.
 *
 * Disketten nur über `TempDisk`, Platten nur über `TempPlatte`; getippt über das Kern-Terminal.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"
#include "tests/support/temp_platte.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";
/// Harte Grenzen in Maschinentakten (4 MHz): wer sie reißt, scheitert mit Bild statt zu hängen.
constexpr long long kFormatGrenze = 8'000'000'000LL;
constexpr long long kVerifyGrenze = 8'000'000'000LL;

/// `sa.format`/`sa.verify` 4.1 (WEGA 3.1, tests/fixtures/p8000/) anstelle der V1.4 der
/// WEGA-3.0-Startdiskette auf die Temp-Kopie legen — V1.4 erwartet den Parameterblock der Firmware 3.x.
/// Dafür weichen die Kernvarianten `wega.n26`/`wega.n52` (die Diskette ist voll).
::testing::AssertionResult saFassung41(const std::string& diskette) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(diskette, "", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    vol->setBackup(false);
    // Platz schaffen: die Diskette ist voll; die Kernvarianten braucht dieser Lauf nicht.
    for (const char* n : {"wega.n26", "wega.n52"}) {
        FileRef ref;
        ref.name = n;
        if (!vol->erase(ref)) return ::testing::AssertionFailure() << n << ": " << vol->lastError();
    }
    for (const char* n : {"sa.format", "sa.verify"}) {
        FileRef ref;
        ref.name = n;
        if (!vol->erase(ref)) return ::testing::AssertionFailure() << n << ": " << vol->lastError();
        if (!vol->insert(std::string(P8000_FIXTURE_DIR) + "/" + n, ref, TransferOptions{}))
            return ::testing::AssertionFailure() << n << ": " << vol->lastError();
    }
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

/// Platte ohne PAR/BTT-Sektor wie im Protokoll („Error in PAR&BTT"): formatiert, Z0/K0/S1 = E5.
void parLoeschen(const std::string& pfad) {
    std::fstream f(pfad, std::ios::in | std::ios::out | std::ios::binary);
    const std::string e5(512, char(0xE5));
    f.write(e5.data(), std::streamsize(e5.size()));
}

P8000Machine::Config mitWdc(const std::string& platte) {
    P8000Machine::Config c;
    c.platte_par_ergaenzen = false;
    c.karte16 = true;
    c.wdc = P8000Machine::Config::Wdc::V4_2;
    c.platte = platte;
    return c;
}

/// Lange Wartezeit (Formatieren/Prüfen der ganzen Platte) mit Fortschrittszeile je 400 Mio. Takte.
bool langBisText(P8000Machine& m, const std::string& nadel, long long grenze) {
    for (long long t = 0; t < grenze; t += 400'000'000) {
        if (laufeBisText(m, nadel, 400'000'000, 1'000'000)) return true;
        std::string z = zeile(m, m.terminal().zeile());
        while (!z.empty() && z.front() == ' ') z.erase(z.begin());
        fprintf(stderr, "  [%5.0f Mio. Takte] %s\n", double(m.totalCycles()) / 1e6, z.c_str());
    }
    return false;
}

/// Wartet auf @p frage und tippt dann @p antwort (die Eingabe kommt wie am Gerät erst, wenn die
/// Frage ganz da ist).
::testing::AssertionResult frage(P8000Machine& m, const std::string& text, const std::string& antwort,
                                 long long grenze = 400'000'000) {
    if (!(grenze > 400'000'000 ? langBisText(m, text, grenze) : laufeBisText(m, text, grenze)))
        return ::testing::AssertionFailure() << "fehlt: '" << text << "'\n" << bild(m);
    laufe(m, 200'000);
    tippeZeile(m, antwort);
    return ::testing::AssertionSuccess();
}

/// U880-Monitor ⇒ BOOT ⇒ UDOS ⇒ Koppelsoftware ⇒ U8000-Monitor ⇒ Hardwaretest ⇒ `O U` ⇒ `boot` ⇒ `:`.
void bisBootPrompt(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << bild(m);
    laufe(m, 2'000'000);
    m.nmi();
    ASSERT_TRUE(laufeBisText(m, "MAXSEG=<0F>", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "*", 80'000'000)) << bild(m);
    tippeZeile(m, "O U");
    ASSERT_TRUE(laufeBisText(m, "BOOTING FROM UDOS FLOPPY", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ">", 40'000'000)) << bild(m);
    tippeZeile(m, "boot");
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
}
}  // namespace

TEST(P8000SaFormat, FormatiertUndPrueftEineTempPlatte) {
    stumm();
    k1520test::TempDisk disk{FIXTURE};
    ASSERT_TRUE(saFassung41(disk.path()));
    k1520test::TempPlatte platte;
    parLoeschen(platte.path());
    P8000Machine m(mitWdc(platte.path()));
    ASSERT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisBootPrompt(m));

    tippeZeile(m, "ud(0,0)sa.format");
    ASSERT_TRUE(laufeBisText(m, ">>>  Format Hard-Disk 4.1  <<<", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Firmwareversion 'WDC_4.2'", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Number of Drives: 1  (Drive 0)", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Error in PAR&BTT on Drive 0 (PAR not ok) (BTT not ok)", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "| 4 |ROB K5504.50|  1024; 5; 18; 1024; 1;", 40'000'000)) << bild(m);
    ASSERT_TRUE(frage(m, "Which Typ ? (No./n/q)", "4"));
    ASSERT_TRUE(laufeBisText(m, "PAR --- Cylinders: 1024  Heads: 5  Sectors: 18  Praecomp: 1024  Ramp: 1",
                             40'000'000)) << bild(m);
    ASSERT_TRUE(frage(m, "Parameter for Drive ok ? (y/l/p/q)", "y"));
    ASSERT_TRUE(laufeBisText(m, "No Entries in Bad Track Table (BTT) of Drive 0", 400'000'000)) << bild(m);
    ASSERT_TRUE(frage(m, "Manual Input of bad Track of Drive 0 (y/n/q) ?", "n"));
    ASSERT_TRUE(frage(m, "Format Begin: Cylinder (a/Start-Cylinder)", "a"));
    ASSERT_TRUE(frage(m, "to Cyl 1023 Hd 4 ? (y/n/q)", "y"));
    // Formatieren: 5120 Spuren mit Rücklesen ≈ 5 Mrd. Takte (≈ 21 min Maschinenzeit, wie am Gerät).
    ASSERT_TRUE(frage(m, "Rewrite PAR&BTT from WDC-RAM to HD-Drive 0 ? (y/n)", "y", kFormatGrenze));
    EXPECT_EQ(bild(m).find("New Entry in BTT"), std::string::npos) << bild(m);
    EXPECT_EQ(bild(m).find("Error"), std::string::npos) << bild(m);
    ASSERT_TRUE(frage(m, "End of 'sa.format' (y/n) ?", "y"));
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);

    tippeZeile(m, "ud(0,0)sa.verify");
    ASSERT_TRUE(laufeBisText(m, ">>>  Verify Hard-Disk 4.1  <<<", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Blocks/Drive: 92070", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "useful Blocks of Drive 0: 92070", 400'000'000)) << bild(m);
    // sa.verify FRAGT Anfangs- und Endzylinder (im Protokoll stehen die Antworten 0 und 1023).
    ASSERT_TRUE(frage(m, "Verify Begin: Cylinder", "0"));
    ASSERT_TRUE(frage(m, "Verify   End: Cylinder", "1023"));
    ASSERT_TRUE(frage(m, "End of 'sa.verify' ? (y/n)", "y", kVerifyGrenze));
    EXPECT_NE(bild(m).find("Verify complete"), std::string::npos) << bild(m);
    EXPECT_EQ(bild(m).find("Error"), std::string::npos) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);

    // Folgeschritt des Protokolls (Z. 86–103): Dateisysteme /usr (md(0,0), 13000 Blöcke) und
    // / (md(0,16000), 7000 Blöcke) anlegen.  Der volle WEGA-Install ist P15.
    struct Fs { const char* groesse; const char* name; const char* isize; };
    for (const Fs& fs : {Fs{"13000", "md(0,0)", "isize = 4176"}, Fs{"7000", "md(0,16000)", "isize = 2256"}}) {
        tippeZeile(m, "ud(0,0)sa.mkfs");
        ASSERT_TRUE(frage(m, "file system size:", fs.groesse));
        ASSERT_TRUE(frage(m, "file system:", fs.name));
        ASSERT_TRUE(laufeBisText(m, fs.isize, 400'000'000)) << bild(m);   // Protokoll Z. 90 bzw. 101
        ASSERT_TRUE(laufeBisPrompt(m, ":", 4'000'000'000LL)) << bild(m);   // „Exit called", Boot
        EXPECT_NE(bild(m).find("m/n = 1 72"), std::string::npos) << bild(m);
    }
}

/// Der Anwenderweg des Plattenkastens „Neue Platte…" (Standard „unformatiert"): `hdCreate` mit dem
/// Suffix ":unformatiert" an einer Maschine MIT Vorgabe `platte_par_ergaenzen` (wie im Programm)
/// führt bis zum ersten Bild von sa.format — ohne Parametersatz meldet der WDC „Error in PAR&BTT"
/// und der Monitor bleibt bedienbar.  (Mit Parametersatz + E5-Inhalt startete AUTOBOOT die E5-Bytes.)
TEST(P8000SaFormat, NeuePlatteUnformatiertKommtBisZumFormatDialog) {
    stumm();
    k1520test::TempDisk disk{FIXTURE};
    ASSERT_TRUE(saFassung41(disk.path()));
    k1520test::TempPlatte pfad = k1520test::TempPlatte::leer("p8000_neu_unformatiert.img");
    P8000Machine::Config c = mitWdc("");
    c.platte_par_ergaenzen = true;                       // Programmvorgabe
    P8000Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.hdCreate(0, pfad.path(), "K5504.50:unformatiert")) << m.hdError();
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisBootPrompt(m));
    tippeZeile(m, "ud(0,0)sa.format");
    ASSERT_TRUE(laufeBisText(m, ">>>  Format Hard-Disk 4.1  <<<", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Firmwareversion 'WDC_4.2'", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Error in PAR&BTT on Drive 0 (PAR not ok) (BTT not ok)", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "Which Typ ? (No./n/q)", 40'000'000)) << bild(m);
}
