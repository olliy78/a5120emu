/**
 * @file test_p8000_monitor16.cpp
 * @brief P8000 Meilenstein M2 (doc/design/25_p8000.md §10.11 AP P11): 16-Bit-Teil über die Kopplung.
 *
 * Soll = Geräteprotokoll des Anwenders (`~/Documents/obsidian/notes/P8000.md`, MON 3.0) und
 * `install_WEGA_3.1.log` (MON 3.1):
 *   `>x` ⇒ „U8000-Softwaremonitor Version 3.1 - Press NMI" auf tty1 (Konsole über die Kopplung);
 *   NMI ⇒ „P8000 Hardwaretest U8001 - Version 3.1" … `MAXSEG=<0F>` … `*`;
 *   von UDOS aus (WEGA-Startdiskette, OS.INIT startet die Koppelsoftware `WEGA`) dasselbe, dazu
 *   `O U` ⇒ „BOOTING FROM UDOS FLOPPY", `> boot` lädt den Zwischenlader, Prompt `:`.
 *
 * **ERROR 52/53/54 im U8001-Hardwaretest sind ohne WDC erwartet** (Rückgabe C1 = WDC antwortet
 * nicht, P13).  Das Geräteprotokoll des Anwenders zeigt dieselben drei Fehler — vermutlich, weil
 * dort kein Winchester-Beisteller angeschlossen war; ein Befund, kein Defekt.
 *
 * Disketten nur über `TempDisk`; getippt wird über das Kern-Terminal (Batch 5 000 Takte).
 */

#include <gtest/gtest.h>

#include <string>

#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";

P8000Machine::Config mit16() {
    P8000Machine::Config c;
    c.karte16 = true;
    return c;
}

/// Hardwaretest U880 bis „Press RETURN", RETURN ⇒ `>`.
void bisMonitor8(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
}

/// NMI ⇒ Hardwaretest U8001 bis zum Prompt `*`.  Gedrückt wird erst, wenn die Meldung ganz
/// über die Kopplung ist (wie am Gerät: lesen, dann drücken).
void hardwaretest16(P8000Machine& m) {
    laufe(m, 2'000'000);
    m.nmi();
    ASSERT_TRUE(laufeBisText(m, "P8000 Hardwaretest U8001 - Version 3.1", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisText(m, "MAXSEG=<0F>", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "*", 40'000'000)) << bild(m);
    const std::string b = bild(m);
    // Ohne WDC: genau ERROR 52/53/54 mit Rückgabe C1 (s. Dateikopf), sonst keiner.
    const std::string kopf = "*** ERROR ";
    for (size_t p = b.find(kopf); p != std::string::npos; p = b.find(kopf, p + 1)) {
        const std::string nr = b.substr(p + kopf.size(), 2);
        EXPECT_TRUE(nr == "52" || nr == "53" || nr == "54") << b;
    }
}
}  // namespace

/// Ohne 16-Bit-Karte ist der U880-Monitor unverändert; `x` findet keine Gegenstelle (die Pull-ups
/// melden „Port frei", die Quittung bleibt aus) — dieselbe Meldung wie die Koppelsoftware unter UDOS.
TEST(P8000Monitor16, OhneKarte16ArbeitetDerU8000Nicht) {
    stumm();
    P8000Machine m;
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    tippeZeile(m, "x");
    ASSERT_TRUE(laufeBisText(m, "Hardware Error in Connection", 40'000'000)) << bild(m);
    EXPECT_EQ(m.panelLamps(), 0);
}

/// `x` im U880-Monitor ⇒ U8000-Softwaremonitor über die Kopplung an tty1; NMI ⇒ Hardwaretest.
TEST(P8000Monitor16, XStartetDenU8000MonitorUndNmiDenHardwaretest) {
    stumm();
    P8000Machine m(mit16());
    m.powerOn();
    ASSERT_NE(m.karte16(), nullptr);
    EXPECT_TRUE(m.karte16()->inReset());               // K11: B7 offen ⇒ Pull-up ⇒ Reset
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    EXPECT_NE(bild(m).find("P8000 Hardwaretest U880 - Version 3.1"), std::string::npos) << bild(m);
    EXPECT_EQ(bild(m).find("ERROR"), std::string::npos) << bild(m);
    EXPECT_TRUE(m.karte16()->inReset());

    tippeZeile(m, "x");
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 40'000'000)) << bild(m);
    EXPECT_FALSE(m.karte16()->inReset());
    EXPECT_EQ(m.panelLamps() & 2, 2);
    ASSERT_NO_FATAL_FAILURE(hardwaretest16(m));
}

/// Von UDOS aus (OS.INIT ⇒ Koppelsoftware `WEGA`): kein „Hardware Error in Connection" mehr,
/// U8000-Monitor, Hardwaretest, `O U` ⇒ „BOOTING FROM UDOS FLOPPY", `boot` ⇒ Prompt `:`.
TEST(P8000Monitor16, VonUdosBootetDerU8000VonDerWegaStartdiskette) {
    stumm();
    k1520test::TempDisk disk{FIXTURE};
    P8000Machine m(mit16());
    ASSERT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisMonitor8(m));
    tippe(m, "\r");                                   // RETURN am `>` ⇒ BOOT
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << bild(m);
    EXPECT_EQ(bild(m).find("Hardware Error in Connection"), std::string::npos) << bild(m);
    ASSERT_NO_FATAL_FAILURE(hardwaretest16(m));

    tippeZeile(m, "O U");
    ASSERT_TRUE(laufeBisText(m, "BOOTING FROM UDOS FLOPPY", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ">", 40'000'000)) << bild(m);
    tippeZeile(m, "boot");
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
    EXPECT_NE(bild(m).find("Boot"), std::string::npos) << bild(m);
}

