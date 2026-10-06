/**
 * @file test_p8000_boot.cpp
 * @brief P8000 Meilenstein M1 (doc/design/25_p8000.md §10.11 AP P7d): UDOS von der
 *        WEGA-Startdiskette auf der 8-Bit-Seite allein.
 *
 * Ablauf am Gerät: MON8 3.1 (Hardwaretest, „Press RETURN") → RETURN ⇒ `>` → RETURN ⇒ BOOT:
 * Urlader Z0/K0/S1 nach 0C00H („P8000SYS"), OSLOAD (Z22/S7 nach 7000H) lädt OS und NDOS,
 * OS führt OS.INIT aus (`0/DAY,` `0/GETDAY` `F 0/KINIT` `F 0/WEGA`).  Die Koppelsoftware WEGA
 * findet ohne 16-Bit-Karte keine Gegenstelle und meldet „Hardware Error in Connection"; danach
 * steht UDOS am Prompt `%`.
 *
 * Disketten nur über `TempDisk` (der Emulator öffnet sie schreibend).  Getippt wird über das
 * Kern-Terminal (Batch 5 000 Takte, `p8000_input.h`).
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

struct Gebootet {
    k1520test::TempDisk disk{FIXTURE};
    P8000Machine m;
    Gebootet() {
        stumm();
        EXPECT_TRUE(m.mountDisk(0, disk.path(), m.defaultFormatName(0), false)) << m.lastError();
        m.powerOn();
    }
};

/// Monitor → `>` → BOOT → UDOS-Prompt `%`.
void bootBisPrompt(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippe(m, "\r");                                   // RETURN am `>` ⇒ BOOT (MON8 RASC)
    ASSERT_TRUE(laufeBisPrompt(m, "%", 60'000'000)) << bild(m);
}
}  // namespace

/// Banner, Monitor, BOOT, UDOS bis `%`; `CAT` zeigt die Dateien der WEGA-Startdiskette.
TEST(P8000Boot, UdosVonDerWegaStartdisketteBisZumPrompt) {
    Gebootet g;
    P8000Machine& m = g.m;
    ASSERT_NO_FATAL_FAILURE(bootBisPrompt(m));
    EXPECT_NE(bild(m).find("P8000 Hardwaretest U880 - Version 3.1"), std::string::npos) << bild(m);
    EXPECT_EQ(bild(m).find("ERROR"), std::string::npos) << bild(m);
    // KINIT gibt PIO0-B frei, ohne dass ein Interrupt kommt (Modus 3 flankengetriggert); die
    // Koppelsoftware findet ohne 16-Bit-Karte keine Gegenstelle.
    EXPECT_NE(bild(m).find("Hardware Error in Connection"), std::string::npos) << bild(m);

    tippeZeile(m, "CAT");
    ASSERT_TRUE(laufeBisText(m, "UDOSCNVT", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "%", 10'000'000)) << bild(m);
    const std::string b = bild(m);
    for (const char* datei : {" boot ", " wega ", " sa.format ", " WEGA ", " KINIT "})
        EXPECT_NE(b.find(datei), std::string::npos) << datei << "\n" << b;
}

/// Ein unbekanntes Kommando beantwortet UDOS selbst — der Weg Terminal → SIO → UDOS → Terminal.
TEST(P8000Boot, UnbekanntesKommandoMeldetUdos) {
    Gebootet g;
    P8000Machine& m = g.m;
    ASSERT_NO_FATAL_FAILURE(bootBisPrompt(m));
    tippeZeile(m, "dir");
    ASSERT_TRUE(laufeBisText(m, "NONEXISTENT COMMAND", 20'000'000)) << bild(m);
}
