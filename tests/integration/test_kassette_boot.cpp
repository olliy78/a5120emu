/**
 * @file test_kassette_boot.cpp
 * @brief A5120 bootet vom Band (doc/design/29_kassette.md, AP K7).
 *
 * Ohne Diskette probiert das Lade-ROM alle Floppy-Steuerworte durch und faellt dann auf den
 * Kassettenzweig (zre.prn 027E–03AF): zuerst Geraet 1 (E = 09H), nach fuenf Fehlversuchen Geraet 2
 * (E = 06H).  Es laedt 4 × 128 B nach 0400H, prueft „SYL“ (01B6H) und ruft 0434H.  Das Band ist
 * die eingecheckte Fixture `tests/fixtures/kassette/bandtest_boot.k52` = `k1520tape bootband
 * tools/kassette/bandtest/bandtest.bin` (Waechter `cli_bandtest_bootband`); das Programm schreibt
 * „KASSETTE OK“ in den Bildspeicher und bleibt stehen.
 *
 * Die Kassette liegt so, wie man sie einlegt: Kopf am Anfang des Vorspanns (Position 0).
 */
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/kassette/k52_codec.h"
#include "core/peripherals/kassette/kassetten_medium.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

namespace {

using kassette::KassettenMedium;

std::shared_ptr<KassettenMedium> bootband() {
    const std::string pfad = std::string(FIXTURE_DIR) + "/../kassette/bandtest_boot.k52";
    std::ifstream f(pfad, std::ios::binary);
    std::vector<uint8_t> inhalt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string fehler;
    auto m = kassette::k52::dekodieren(inhalt, fehler);
    if (!m) {
        ADD_FAILURE() << pfad << ": " << fehler;
        return nullptr;
    }
    return std::make_shared<KassettenMedium>(*m);
}

/// Maschine ohne Diskette, K5020 gesteckt, Bootband in Geraet @p geraet (0 = LW1, 1 = LW2).
void bootetVomBand(int geraet) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::WARN);
    A5120Machine m;
    ASSERT_TRUE(m.installK5020()) << m.k5020Fehler();
    auto band = bootband();
    ASSERT_TRUE(band);
    ASSERT_TRUE(m.k5020()->laufwerk(geraet).kassetteEinlegen(band));
    m.powerOn();
    // Floppy-Durchlauf (alle Steuerworte), dann Kassette; bei Geraet 2 erst fuenf Fehlversuche an\n    // Geraet 1.  Obergrenze ≈ 400 s Maschinenzeit — sie wirkt nur im Fehlerfall.
    ASSERT_TRUE(k1520test::runUntilVramContains(m, "KASSETTE OK", 1'000'000'000))
        << "PC=" << std::hex << m.cpuPC() << " [0400]=" << int(m.memReadDebug(0x400))
        << " ROM=" << m.isRomEnabled() << " Bild: " << k1520test::vramText(m).substr(0, 160);
    EXPECT_EQ(k1520test::vramText(m).substr(0, 11), "KASSETTE OK");
    EXPECT_EQ(m.k5020()->verloreneBytes(), 0u);
}

TEST(KassetteBoot, LadeRomLaedtVonBand) {
    SCOPED_TRACE("Bootband in Geraet 1");
    bootetVomBand(0);
}

TEST(KassetteBoot, LadeRomLaedtVonBand_Geraet1LeerDannGeraet2) {
    SCOPED_TRACE("Geraet 1 leer, Bootband in Geraet 2");
    bootetVomBand(1);
}

}  // namespace
