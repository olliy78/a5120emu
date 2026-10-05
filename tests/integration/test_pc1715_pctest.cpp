/**
 * @file test_pc1715_pctest.cpp
 * @brief PC 1715 AP-4d (doc/design/21_pc1715.md): Abnahme mit dem Werks-Testprogramm
 *        `PCTEST.COM` („Testprogramm fuer PC 1715", CPA_Workbench/additions/pc_1715/).
 *
 * Gefahren wird unter dem CP/A 1715, den die Workbench gebaut hat (Fixture
 * `pc1715_cpa1715_workbench.hfe`, trägt PCTEST.COM).  Die Kopfdaten-Abfrage läuft vollständig;
 * die eigentlichen Prüfungen (Speicher, V.24, Drucker, Floppy) laufen **nicht an**, und das ist
 * Gastverhalten, kein Emulatorfehler — der Befund steht im zweiten Fall und in
 * `doc/merkposten/pc1715.md` („PCTEST.COM").  Disketten nur über `TempDisk`.
 *
 * Die Workbench trägt PCTEST **V 0.1** (10 496 B) — dieser Abzug ist **beschädigt**: Satz 56
 * (1D00–1D7FH) ist ganz E5H, der Kern des Speichertests fehlt (AP-4f).  Der vollständige
 * Werkstest läuft deshalb mit der heilen Fassung aus `SOFT1715.img` unter SCP:
 * `tests/system/test_pc1715_pctest_scp.cpp`.
 */

#include <gtest/gtest.h>

#include <string>

#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"

using namespace k1520test::pc1715;

namespace {

/// CP/A der Workbench (Vorgabe: fragt nach der Uhrzeit), `PCTEST` gestartet, Kopfdaten bis zur
/// Rückfrage „alle Eingaben korrekt ?" eingegeben.
struct PctestKopf {
    Pc1715Machine m;
    k1520test::TempDisk disk{"pc1715_cpa1715_workbench.hfe"};

    bool bis(const std::string& text, long long grenze = 400'000'000) {
        return laufeBisText(m, text, grenze);
    }
    /// Antwort tippen, sobald @p prompt im Bild steht.  @p mitReturn: Zeilenabfragen (Nummer,
    /// Name, Zeit, Laufwerke) ja, Einzeltasten-Fragen (j/n) nein — ein Return dort wäre schon die
    /// Antwort auf die NÄCHSTE Frage.
    bool antworte(const std::string& prompt, const std::string& text, bool mitReturn = true) {
        if (!bis(prompt)) return false;
        laufe(m, 5'000'000);   // das Programm muss erst seine Eingabe anfordern
        tippe(m, mitReturn ? text + "\r" : text);
        return true;
    }
    /// Alles bis zur Rückfrage; die Rückfrage selbst bleibt unbeantwortet.
    bool bisRueckfrage() {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
        if (!m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) return false;
        m.powerOn();
        if (!antworte("Uhrzeit in der Form HH:MM", "12:00")) return false;
        if (!laufeBisPrompt(m, "A>", 40'000'000)) return false;
        tippeZeile(m, "pctest");
        return antworte("Geraetenr.", "1") && antworte("Bediener", "OL") &&
               antworte("Testzeit(ss.mm):", "00.01") &&
               antworte("Standard-Test(Speicher,V24,Printer,Floppy)", "j", false) &&
               antworte("Eingabe (max.4)", "a") && antworte("Zusatzinterface", "n", false) &&
               bis("alle Eingaben korrekt ? (j/n)");
    }
};

}  // namespace

/// PCTEST startet unter CP/A 1715 aus der Workbench, zeigt Kopf und Bedingungen
/// (Rückkopplungsstecker 320-032 V.24, 330-032 IFSS, 330-042 Drucker) und nimmt Gerätenummer,
/// Bediener, Testzeit, Umfang, Laufwerke und „kein Zusatzinterface" an.
TEST(Pc1715Pctest, CpaStartetUndFragtDieKopfdatenAb) {
    PctestKopf k;
    ASSERT_TRUE(k.bisRueckfrage()) << bild(k.m);
    const std::string b = bild(k.m);
    EXPECT_NE(b.find("Testprogramm fuer  P C  1 7 1 5"), std::string::npos) << b;
    EXPECT_NE(b.find("(320-032=V24),(330-032=IFSS),(330-042=PRINTER)"), std::string::npos) << b;
    EXPECT_NE(b.find("Geraetenr.     :1"), std::string::npos) << b;
    EXPECT_NE(b.find("Bediener       :OL"), std::string::npos) << b;
    EXPECT_NE(b.find("Testzeit(ss.mm):00.01"), std::string::npos) << b;
    EXPECT_NE(b.find("Eingabe (max.4) : a"), std::string::npos) << b;
    EXPECT_NE(b.find("Zusatzinterface ja/nein (j/n):n"), std::string::npos) << b;
}

/// Befund AP-4d: Nach der Bestätigung programmiert PCTEST die System-CTC um — Vektorbasis 08H
/// (`OUT (08H),08H`), Kanal 2 als eigener Zeitgeber (`B7`/`FA`, Vektor 0CH) — und lässt Kanal 3,
/// den 25-ms-Takt des CP/A (Uhr, Laufwerksüberwachung, `bioptimc.mac`), eingeschaltet.  Dessen
/// Interrupt geht jetzt auf Vektor 0EH, dessen Tabelleneintrag leer ist (PCTEST legt nur 0CH an)
/// → Sprung nach 0000H → Warmstart des CP/A, immer wieder; der Warmstart stellt den Kanal 2
/// zurück (`timwarm`: `OUT (0AH),0FH`), PCTESTs Zeitgeber läuft nie, die Prüfungen kommen nicht
/// zum Ablauf.  Dasselbe am Gerät, denn alles ist Gast-Software (PCTEST V 0.1 stammt von einem
/// BIOS, in dem der Takt nicht auf Kanal 3 lag) — kein Emulatorfehler.
TEST(Pc1715Pctest, UnterCpaStoertPctestDieBiosUhrDieCtc) {
    PctestKopf k;
    ASSERT_TRUE(k.bisRueckfrage()) << bild(k.m);
    tippe(k.m, "j");
    laufe(k.m, 30'000'000);
    const auto d = k.m.zre().ctc().debugState();
    EXPECT_EQ(d.vecBase & 0xF8, 0x08) << "PCTEST hat die Vektorbasis auf 08H gesetzt";
    EXPECT_TRUE(d.ch[3].intEn) << "CP/A-Uhr (Kanal 3) läuft weiter";
    // Tabelle am I-Register: Eintrag für Kanal 3 (Vektor 0EH) leer, Kanal 2 (0CH) belegt.
    const unsigned i = k.m.zre().cpu().I;
    auto wort = [&](unsigned a) {
        return unsigned(k.m.zre().ramPeek(uint16_t(a))) | unsigned(k.m.zre().ramPeek(uint16_t(a + 1))) << 8;
    };
    EXPECT_EQ(wort(i << 8 | 0x0E), 0u) << "Kanal 3: kein Interruptziel";
    EXPECT_NE(wort(i << 8 | 0x0C), 0u) << "Kanal 2: PCTESTs eigener Takt";
    // Die Prüfungen laufen nicht an: das Bild zeigt weiter die Kopfdaten.
    EXPECT_NE(bild(k.m).find("alle Eingaben korrekt ? (j/n)j"), std::string::npos) << bild(k.m);
}
