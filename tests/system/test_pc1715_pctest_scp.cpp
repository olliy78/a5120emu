/**
 * @file test_pc1715_pctest_scp.cpp
 * @brief PC 1715 AP-4f (doc/design/21_pc1715.md): der Werkstest `PCTEST.COM` („Testprogramm
 *        fuer PC 1715", Robotron) als automatischer Abnahmetest — wie HARDY am A5120.
 *
 * Gefahren unter **SCP 1715 V0006** (Fixture `pc1715_scp1715_v0006_pctest.hfe` = Systemdiskette +
 * `PCTEST.COM` aus `tests/fixtures/cpm/`).  Bedienung (doc/merkposten/pc1715.md „PCTEST.COM"):
 * Bediener, Gerätenummer, Testzeit `ss.mm` (Stunden.Minuten), Standardtest `j` (Einzeltaste),
 * Laufwerke, Zusatzinterface `n` (Einzeltaste; 10H–17H ist nicht bestückt), Bestätigung `j`.
 * Danach läuft der Test ohne Bedienung: Speicher (29E8H bis unter das BDOS), Drucker
 * (Prüfstecker 330-042: 103 → 106), V.24 (320-032: 103 → 104, 105 → 106, 108 → 107, 111 → 109),
 * Floppy (Testdatei je Laufwerk), alles im Kreis, bis die Testzeit abgelaufen ist; am Ende
 * „positiv beendet" und ein Beleg (Datei mit der Gerätenummer) auf A:.
 *
 * **Prüfstecker = Loop am Hub** (beide Anschlüsse).  Die Brücken der Robotron-Stecker bildet die
 * Karte sofort nach (`SerialAnschluss::pruefstecker`, `Pc1715Zre`): PCTEST liest eine Leitung
 * ≈ 40 Takte nach dem Setzen, der Wandler blickt nur alle 1/16 Zeichenzeit.
 *
 * Testzeit 00.01 = 1 min Maschinenzeit (Zeitgeber CTC0 K2, 2300 × 25,6 ms) ≈ 150 Mio Takte;
 * Label `format_integration` (`tools/dev.sh test-format`).  Disketten nur über `TempDisk`.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/serial/hub.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"
#include "tests/support/temp_path.h"

using namespace k1520test::pc1715;
using k1520test::TempDisk;

namespace {

constexpr long long kFrist = 600'000'000;   ///< Testzeit 1 min ≈ 150 Mio Takte, reichlich Luft

bool enthaelt(Pc1715Machine& m, const std::string& s) { return bild(m).find(s) != std::string::npos; }

struct Pctest {
    Pc1715Machine m;
    TempDisk a{"pc1715_scp1715_v0006_pctest.hfe"};
    TempDisk b = TempDisk::empty("pc1715_pctest_scp_b.hfe");

    /// Antwort tippen, sobald @p prompt im Bild steht.  (j/n)-Fragen sind Einzeltasten OHNE
    /// Return — ein Return dort beantwortete schon die nächste Frage.
    bool antworte(const std::string& prompt, const std::string& text, bool mitReturn = true) {
        if (!laufeBisText(m, prompt, 100'000'000)) return false;
        laufe(m, 5'000'000);   // das Programm muss erst seine Eingabe anfordern
        tippe(m, mitReturn ? text + "\r" : text);
        return true;
    }

    /// SCP booten, PCTEST starten, Kopfdaten eingeben und bestätigen.  @p laufwerke wie
    /// eingetippt („a", „ab"); für B: liegt eine mit `cpa800` vorformatierte Diskette ein
    /// (SCP führt B: wie A: als 5 × 1024 × 80 DS).
    bool starte(const std::string& laufwerke, bool pruefstecker) {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        if (!m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) return false;
        if (!m.createDisk(1, b.path(), "cpa800", false)) return false;
        for (int i = 0; i < 2; ++i) {   // 0 = Drucker X4 (330-042), 1 = V.24 X5 (320-032)
            auto k = m.serialHub()->konfig(i);
            k.loop = pruefstecker;
            if (!m.serialHub()->konfigurieren(i, k)) return false;
        }
        m.powerOn();
        if (!laufeBisPrompt(m, "A>", 40'000'000)) return false;
        tippeZeile(m, "pctest");
        return antworte("Bediener       :", "OL") && antworte("Geraetenr.     :", "1715") &&
               antworte("Testzeit(ss.mm):", "00.01") &&
               antworte("Standard-Test(Speicher,V24,Printer,Floppy) (j/n):", "j", false) &&
               antworte("Eingabe (max.4) :", laufwerke) &&
               antworte("Zusatzinterface ja/nein (j/n):", "n", false) &&
               antworte("alle Eingaben korrekt ? (j/n)", "j", false);
    }

    /// Beleg (Datei = Gerätenummer) von A: holen.
    std::string beleg() {
        m.flushDisks();
        std::string err;
        FormatCatalog formate = FormatCatalog::loadDefault(&err);
        FsCatalog fs = FsCatalog::loadDefault(formate, &err);
        auto dv = DiskVolume::open(a.path(), "", formate, fs, err);
        if (!dv) return "(A: nicht lesbar: " + err + ")";
        const std::string ziel = k1520test::tempPath("pc1715_pctest_beleg.txt");
        if (!dv->extract(FileRef{0, "1715"}, ziel, TransferOptions{})) return "(kein Beleg 1715)";
        std::ifstream f(ziel, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }
};

}  // namespace

/**
 * @test Pc1715Pctest.ScpStandardtestMitPruefsteckernBestehtPositiv
 * @brief Standardtest (Speicher, Drucker, V.24, Floppy A: und B:) mit beiden Prüfsteckern,
 *        Testzeit 1 min: „positiv beendet", kein „gestoert", Beleg auf A: nennt alle vier
 *        Teiltests, beide Laufwerke und Restzeit 00.00.
 */
TEST(Pc1715Pctest, ScpStandardtestMitPruefsteckernBestehtPositiv) {
    Pctest t;
    ASSERT_TRUE(t.starte("ab", true)) << bild(t.m);
    ASSERT_TRUE(laufeBisText(t.m, "positiv beendet", kFrist)) << bild(t.m);
    const std::string b = bild(t.m);
    EXPECT_EQ(b.find("gestoert"), std::string::npos) << b;
    EXPECT_EQ(b.find("abgebrochen"), std::string::npos) << b;
    EXPECT_NE(b.find("Laufzeit von 00.01 Stunden"), std::string::npos) << b;

    const std::string beleg = t.beleg();
    EXPECT_NE(beleg.find("Bearbeiter: OL"), std::string::npos) << beleg;
    EXPECT_NE(beleg.find("Geraetenummer: 1715"), std::string::npos) << beleg;
    EXPECT_NE(beleg.find("Restzeit: 00.00 Stunden"), std::string::npos) << beleg;
    // Getestete Teile stehen mit „===" davor; Zusatz-V.24/IFSS sind abgewählt.
    EXPECT_NE(beleg.find("===r a m  /===printer/===v 24   /===floppy /"), std::string::npos) << beleg;
    EXPECT_EQ(beleg.find("2*z v24"), std::string::npos) << beleg;
    EXPECT_NE(beleg.find("Getestet wurden die Laufwerke : ab"), std::string::npos) << beleg;
}

/**
 * @test Pc1715Pctest.ScpOhnePruefsteckerMeldetDruckerGestoert
 * @brief Gegenprobe: ohne Prüfstecker bricht PCTEST beim Druckertest ab („PRINTER
 *        Ltg.103---106 gestoert") — die Leitungsprüfung ist also wirksam, nicht blind grün.
 */
TEST(Pc1715Pctest, ScpOhnePruefsteckerMeldetDruckerGestoert) {
    Pctest t;
    ASSERT_TRUE(t.starte("a", false)) << bild(t.m);
    ASSERT_TRUE(laufeBisText(t.m, "PRINTER Ltg.103---106 gestoert", kFrist)) << bild(t.m);
    EXPECT_TRUE(enthaelt(t.m, "abgebrochen")) << bild(t.m);
}
