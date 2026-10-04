/**
 * @file test_pc1715_tastatur.cpp
 * @brief PC 1715 Etappe 3 (doc/design/21_pc1715.md AP-3): die Tastatur (`Tastatur1715`, eigener
 *        U880 + S600) hängt am Kanal A der SIO0; Betriebssysteme nehmen Eingaben an.
 *
 * Disketten nur über `TempDisk`.  Batch 5 000 Takte (tests/support/pc1715_input.h).  Groß- und
 * Kleinschreibung: `Shift` wird vor der Zeichentaste einzeln gedrückt (das ROM sendet nichts,
 * wenn zwei Tasten im selben Abfragedurchlauf neu kommen) — beides wird hier ausgeübt.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"

using namespace k1520test::pc1715;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

bool enthaelt(Pc1715Machine& m, const std::string& s) { return bild(m).find(s) != std::string::npos; }

/// Rohe Bytes im Empfangs-FIFO von SIO0-A (ohne Betriebssystem, das sie abholt).
std::vector<uint8_t> fifo(Pc1715Machine& m) {
    const auto& q = m.zre().sio().channelA().rx_fifo;
    return {q.begin(), q.end()};
}
}  // namespace

// ─── Ohne Betriebssystem: Weg Taste → ROM S600 → Rahmen → SIO-A ──────────────────────────────

/// Eine Zeichentaste liefert Statusbyte E0H + Code; beide kommen im FIFO an (zwei Rahmen im
/// Abstand von ≈ 0,7 ms gehen nicht verloren, FIFO = 3 Bytes).
TEST(Pc1715Tastatur, ZeichentasteLiefertStatusUndCode) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    ASSERT_TRUE(fifo(m).empty());
    m.keyPress('a', false, false);
    laufe(m, 400'000);
    EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE0, 0x61}));
}

/// Ein Anschlag der Oberfläche, der sofort wieder losgelassen wird, wird gestreckt statt verschluckt
/// (die Warteschlange hält die Taste mindestens HALTE_MIN unten).
TEST(Pc1715Tastatur, KurzerAnschlagWirdNichtVerschluckt) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    m.keyPress('x', false, false);
    m.keyRelease('x');
    laufe(m, 600'000);
    EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE0, 0x78}));
}

/// Großbuchstabe: Shift allein vorweg, dann die Taste → Status E2H (Shift) + Code des Großbuchstaben.
TEST(Pc1715Tastatur, GrossbuchstabeDrueckteShiftVorher) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    m.keyPress('A', false, false);
    laufe(m, 600'000);
    EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE2, 0x41}));
}

/// Ctrl: Statusbit 0, der Code bleibt der der Taste (tastatur.md §5: CTRL verändert den Code nicht).
TEST(Pc1715Tastatur, CtrlSetztStatusbit0) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    m.keyPress(0x03 /* Ctrl-C */, false, true);
    laufe(m, 600'000);
    EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE1, 0x63}));
}

/// Physische Taste der Matrix (Bildschirmtastatur): (Spalte 3, Zeile 4) = ET.
TEST(Pc1715Tastatur, PhysischeTasteUeberMatrixposition) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    m.keyPress(Pc1715Machine::QK_TASTE_BASE | (3 * 8 + 4), false, false);
    laufe(m, 400'000);
    EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE0, 0x9E}));
}

/// Return/Escape/Tab aus der Oberfläche (Qt-Codes) gehen auf die richtigen Tasten.
TEST(Pc1715Tastatur, SondertastenReturnEscapeTab) {
    stumm();
    Pc1715Machine m;
    m.powerOn();
    laufe(m, 1'000'000);
    struct F { uint32_t qt; uint8_t code; };
    for (const F& f : {F{0x01000004, 0x9E}, F{0x01000000, 0x1B}, F{0x01000001, 0x8D}}) {
        m.zre().sio().channelA().rx_fifo.clear();
        m.keyPress(f.qt, false, false);
        laufe(m, 400'000);
        m.keyRelease(f.qt);
        laufe(m, 200'000);
        EXPECT_EQ(fifo(m), (std::vector<uint8_t>{0xE0, f.code})) << std::hex << f.qt;
    }
}

// ─── Mit Betriebssystem ──────────────────────────────────────────────────────────────────────

/// SCP 1715 V0006: DIR (klein getippt) listet die Dateien, STAT (groß) meldet das Laufwerk.
/// `PIP.COM` dieser Version bricht mit „REQUIRES SCP3" ab — sie braucht ein CP/M-3-BDOS; die
/// Meldung ist selbst der Beleg, dass der Lader das Programm gestartet hat.
TEST(Pc1715Tastatur, Scp1715DirStatPip) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_scp1715_v0006_boot.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);

    tippeZeile(m, "dir");
    ASSERT_TRUE(laufeBisText(m, "A: DIMA     COM", 30'000'000)) << bild(m);
    EXPECT_TRUE(enthaelt(m, "A: INIT     COM : INSTSCP  COM")) << bild(m);
    EXPECT_TRUE(enthaelt(m, "A: DISKCOPY COM : PIP      COM : STAT     COM")) << bild(m);

    tippeZeile(m, "STAT");
    ASSERT_TRUE(laufeBisText(m, "A: R/W, Space: 692k", 30'000'000)) << bild(m);

    tippeZeile(m, "PIP");
    EXPECT_TRUE(laufeBisText(m, "REQUIRES SCP3", 30'000'000)) << bild(m);
}

/// CP/A 1715: DIR (klein) listet die Bootdiskette.
TEST(Pc1715Tastatur, Cpa1715Dir) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_cpa1715_boot_4lw.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    tippeZeile(m, "dir");
    ASSERT_TRUE(laufeBisText(m, "A: M80      COM", 30'000'000)) << bild(m);
    EXPECT_TRUE(enthaelt(m, "A: @OS      COM : PIP      COM : CPA1715G COM : FORMATPX COM")) << bild(m);
}

/// CP/A 1715: PIP kopiert eine Datei auf eine zweite, vorformatierte Diskette (B:); DIR B: zeigt sie.
TEST(Pc1715Tastatur, Cpa1715PipKopiertNachB) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk a("pc1715_cpa1715_boot_4lw.hfe");
    auto b = k1520test::TempDisk::empty("pc1715_tastatur_b.hfe");
    ASSERT_TRUE(m.mountDisk(0, a.path(), m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.createDisk(1, b.path(), "cpa800", false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    tippeZeile(m, "PIP B:=A:ZSID.COM");
    laufe(m, 60'000'000);
    tippeZeile(m, "DIR B:");
    EXPECT_TRUE(laufeBisText(m, "B: ZSID     COM", 40'000'000)) << bild(m);
}

/// UDOS 1715: `CAT` (klein) zeigt das Inhaltsverzeichnis und endet wieder am Prompt `%`.
TEST(Pc1715Tastatur, Udos1715Cat) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_udos1715_system.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "%", 100'000'000)) << bild(m);
    tippeZeile(m, "cat");
    ASSERT_TRUE(laufeBisText(m, "ZLINK2", 60'000'000)) << bild(m);
    EXPECT_TRUE(enthaelt(m, "IMAGER")) << bild(m);
}

// ─── AP-4e: zwei Befunde ─────────────────────────────────────────────────────────────────────

/// CP/Z 2.2 nimmt die Tastatur an.  Es programmiert SIO-B (WR2 = C0H, WR1 = 04H „Status affects
/// Vector"), setzt Kanal B danach für die V.24-Parameter erneut zurück (Channel Reset) und
/// erwartet Vektor und Vektorbit unverändert — seine Tabelle liegt auf F7CCH (Rx von Kanal A).
/// Ein Kanalreset, der WR2/WR1 D2 löschte, schickte jeden Tasteninterrupt in die Leere.
TEST(Pc1715Tastatur, Cpz22NimmtTastaturAn) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_cpz22_boot.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    tippe(m, "ab");
    EXPECT_TRUE(laufeBisText(m, "A>ab", 5'000'000)) << bild(m);
}

/// SCP 1715: die jeweils erste Taste nach einer langen Pause am Prompt geht nicht verloren.
TEST(Pc1715Tastatur, Scp1715ErsteTasteNachPause) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_scp1715_v0006_boot.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    std::string soll = "A>";
    for (long long pause : {500'000LL, 3'000'000LL, 20'000'000LL, 700'000LL}) {
        laufe(m, pause);
        tippe(m, "x");
        soll += "x";
        laufe(m, 300'000);
        const bool ok = enthaelt(m, "\n" + soll + "\n");
        EXPECT_TRUE(ok) << "Pause " << pause << "\n" << bild(m);
        if (!ok) break;
    }
}

/// Wie oben, aber wie die Oberfläche: Taste sofort wieder loslassen, Pausen mit wechselnder Phase
/// zum Abfragedurchlauf des Tastatur-ROMs.
TEST(Pc1715Tastatur, Scp1715ErsteTasteNachPausePhasen) {
    stumm();
    Pc1715Machine m;
    k1520test::TempDisk d("pc1715_scp1715_v0006_boot.hfe");
    ASSERT_TRUE(m.mountDisk(0, d.path(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    std::string soll = "A>";
    for (int i = 0; i < 16; ++i) {
        laufe(m, 2'000'000 + 7'000LL * i * i);
        m.keyPress('x', false, false);
        m.keyRelease('x');
        laufe(m, 700'000);
        soll += "x";
        const bool ok = enthaelt(m, "\n" + soll + "\n");
        ASSERT_TRUE(ok) << "Durchlauf " << i << "\n" << bild(m);
    }
}
