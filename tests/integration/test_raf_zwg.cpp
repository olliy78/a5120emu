/**
 * @file test_raf_zwg.cpp
 * @brief ZWG-Prüfprogramme `RAFTEST.COM` (M80, 13.2.87, Quelle doc/raf512/RAFTEST.MAC) und
 *        `RAFQUICK.COM` (Turbo Pascal 3, 03/86) gegen die Nachbildung der RAF512 am A5120
 *        unter CP/A (doc/design/22_raf512.md §8 AP-R6).
 *
 * `RAFTEST` hat keinen Dialog für die Kartenadresse: sie steht im „PATCH AREA" am
 * Programmanfang (`RFCtl`/`RFDat`, Dateiversatz 22H/23H, Vorgabe 8FH/8EH) und wurde am Gerät
 * mit DDT umgesetzt.  Der Test tut dasselbe auf der Kopie (89H/88H) und prüft, dass das
 * Programm die Adresse so meldet.  Der Logikanalysator-Ausgang (`LogAnlzTrig equ 1`, PIO
 * 84H/86H) geht am A5120 ins Leere — dort liegt keine PIO.
 *
 * Geprüft wird nur, was `RAFTEST` selbst auf den Schirm schreibt, plus der Karteninhalt über
 * `RAF::peek`:
 *  - Typ „5", Bereich 0…FFFH (alle 4096 Sektoren = 512 KByte), „Q" (Zufallsmuster schreiben,
 *    wiederholt lesen und vergleichen): ein voller Durchgang (`*`) ohne Fehlerzeile `Raf=`.
 *  - Sperrmuster 90H und 10H (A15 bzw. nur A12, §3.2): Schreiben verpufft, Lesen liefert FFH
 *    (`Raf=FF`), der Inhalt der Karte bleibt unverändert; Muster 00H öffnet wieder, „O" findet
 *    den alten Inhalt und schreibt ihn byteweise neu (Steuerbyte 21H, „Rewrite 1 BYTE").
 */
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::runSmallUntilVisible;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramLines;
using k1520test::visibleText;

namespace {

constexpr long long kBootBudget = 90'000'000;
constexpr long long kDialog     = 60'000'000;     // bis zur nächsten Eingabeaufforderung
constexpr long long kDurchgang  = 600'000'000;    // ein voller Prüfdurchgang über 512 KByte
const char*         kCpa        = "cpa_cpa780_k5601_noclock.img";
const char*         kMenue      = "O  :";          // „( ? W D L V S P )   C  R  Q  F  T  O  : "
const char*         kLauf       = "Test until Keyboard input.";

/// Hostdatei @p host als @p name auf die CP/A-Diskette @p pfad.
::testing::AssertionResult aufDiskette(const std::string& pfad, const std::string& host,
                                       const std::string& name) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(pfad, "cpa780", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    FileRef ref;
    ref.name = name;
    if (!vol->insert(host, ref, TransferOptions{}))
        return ::testing::AssertionFailure() << vol->lastError();
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

/// `RAFTEST.COM` mit Kartenadresse 88H/89H im Patchbereich auf die Diskette @p pfad.
::testing::AssertionResult raftestAufDiskette(const std::string& pfad, const std::string& tmp) {
    std::string com = k1520test::readFileBytes(std::string(RAF_FIXTURE_DIR) + "/RAFTEST.COM");
    if (com.size() < 0x24 || uint8_t(com[0x22]) != 0x8F || uint8_t(com[0x23]) != 0x8E)
        return ::testing::AssertionFailure() << "RAFTEST.COM: Patchbereich nicht wie erwartet";
    com[0x22] = char(0x89);   // RFCtl
    com[0x23] = char(0x88);   // RFDat
    { std::ofstream(tmp, std::ios::binary).write(com.data(), std::streamsize(com.size())); }
    return aufDiskette(pfad, tmp, "RAFTEST.COM");
}

std::string letzteZeile(A5120Machine& m) {
    const std::string t = visibleText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        zeile.erase(zeile.find_last_not_of(' ') + 1);
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Bis die letzte sichtbare Zeile auf @p endung endet (Eingabeaufforderung steht).
bool bisAufforderung(A5120Machine& m, const std::string& endung, long long frist = kDialog) {
    for (long long d = 0; d < frist; d += 100'000) {
        k1520test::runCycles(m, 100'000);
        const std::string z = letzteZeile(m);
        if (z.size() >= endung.size() && z.compare(z.size() - endung.size(), endung.size(),
                                                   endung) == 0)
            return true;
    }
    return false;
}

/// Eingabe tippen, RETURN, bis zur nächsten Aufforderung.
bool eingabe(A5120Machine& m, const std::string& s, const std::string& endung) {
    typeString(m, s);
    typeKey(m, k1520test::QK_RETURN);
    return bisAufforderung(m, endung);
}

/// Bildschirmtext hinter dem LETZTEN Vorkommen von @p marke ("" = nicht da).
std::string nach(A5120Machine& m, const std::string& marke) {
    const std::string t = visibleText(m);
    const size_t p = t.rfind(marke);
    return p == std::string::npos ? std::string() : t.substr(p + marke.size());
}

/// Laufenden Prüfdurchgang bis zum ersten `*` (Durchgang vollständig) fahren, dann mit
/// einer beliebigen Taste ins Menü zurück.  Liefert den Text des Durchgangs.
std::string durchgang(A5120Machine& m) {
    std::string t;
    for (long long d = 0; d < kDurchgang; d += 1'000'000) {
        k1520test::runCycles(m, 1'000'000);
        t = nach(m, kLauf);
        if (t.find('*') != std::string::npos) break;
    }
    typeString(m, "x");
    EXPECT_TRUE(bisAufforderung(m, kMenue)) << vramLines(m);
    return t;
}

/// Bereich (hex) über „C" setzen, Steuerbyte unverändert (21H).
void bereich(A5120Machine& m, const std::string& von, const std::string& bis) {
    typeString(m, "c");
    ASSERT_TRUE(bisAufforderung(m, "Other Range (y) :")) << vramLines(m);
    typeString(m, "y");
    ASSERT_TRUE(bisAufforderung(m, "Lower Sector : $")) << vramLines(m);
    ASSERT_TRUE(eingabe(m, von, "Upper : $")) << vramLines(m);
    ASSERT_TRUE(eingabe(m, bis, "Other (y) :")) << vramLines(m);
    EXPECT_NE(visibleText(m).find("Function Ctrl Byte = $21"), std::string::npos)
        << vramLines(m);
    typeString(m, "n");
    ASSERT_TRUE(bisAufforderung(m, kMenue)) << vramLines(m);
}

/// Sperrmuster über „P" setzen; liefert die Antwort von `PROTYPE`.
std::string muster(A5120Machine& m, const std::string& hex) {
    typeString(m, "p");
    EXPECT_TRUE(bisAufforderung(m, "NEW(hex) =")) << vramLines(m);
    EXPECT_TRUE(eingabe(m, hex, kMenue)) << vramLines(m);
    return nach(m, "NEW(hex) = ");
}

std::vector<uint8_t> inhalt(const RAF& raf, uint32_t n) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = raf.peek(i);
    return v;
}

}  // namespace

class RafZwg : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test RafZwg.RaftestTyp5VollerDurchgangUndSperrmuster
 * @brief `RAFTEST` Typ „5" an 88H/89H: Vollauf 0…FFFH fehlerfrei; Sperrmuster 90H/10H sperren
 *        (FFH, Schreiben verpufft), 00H öffnet.
 */
TEST_F(RafZwg, RaftestTyp5VollerDurchgangUndSperrmuster) {
    TempDisk disk(kCpa, "k1520_raf_zwg.img");
    TempDisk com = TempDisk::empty("k1520_raf_zwg_RAFTEST.COM");
    ASSERT_TRUE(raftestAufDiskette(disk.path(), com.path()));
    auto mp = std::make_unique<A5120Machine>();
    A5120Machine& m = *mp;
    ASSERT_TRUE(m.installRaf(RAF::Typ::RAF512)) << m.rafFehler();
    ASSERT_TRUE(m.mountDisk(0, disk.path(), "cpa780", false)) << m.lastError();

    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(bisAufforderung(m, "A>")) << vramLines(m);

    // Start: Kartentyp erfragen, dann Kenndaten + Sonderbefehle, „press any key".
    ASSERT_TRUE(eingabe(m, "RAFTEST", "<RET> = \"512\" :")) << vramLines(m);
    typeString(m, "5");
    ASSERT_TRUE(bisAufforderung(m, "press any key :")) << vramLines(m);
    EXPECT_NE(visibleText(m).find("I/O-Addr  =  $89 (ctrlIO),     $88 (datIO)"),
              std::string::npos) << vramLines(m);
    typeString(m, " ");
    ASSERT_TRUE(bisAufforderung(m, kMenue)) << vramLines(m);

    // Vollauf: alle 4096 Sektoren.
    ASSERT_NO_FATAL_FAILURE(bereich(m, "0", "fff"));
    typeString(m, "q");
    std::string t = durchgang(m);
    EXPECT_NE(t.find('*'), std::string::npos) << "kein vollständiger Durchgang\n" << vramLines(m);
    EXPECT_EQ(t.find("Raf="), std::string::npos) << vramLines(m);
    EXPECT_FALSE(m.raf()->gesperrt());   // RAFTEST sperrt nicht von selbst

    // Sperrmuster nur auf Sektor 0…3; Puffer vorher auf ein festes Byte, damit ein
    // durchgekommener Schreibzugriff am Inhalt sichtbar würde.
    ASSERT_NO_FATAL_FAILURE(bereich(m, "0", "3"));
    const std::vector<uint8_t> vorher = inhalt(*m.raf(), 512);
    for (const char* p : {"90", "10"}) {
        SCOPED_TRACE(std::string("Sperrmuster ") + p);
        t = muster(m, p);
        EXPECT_NE(t.find(" Protected"), std::string::npos) << vramLines(m);
        EXPECT_EQ(t.find("NOT"), std::string::npos) << vramLines(m);
        EXPECT_EQ(t.find("??"), std::string::npos) << vramLines(m);

        typeString(m, "f");
        ASSERT_TRUE(bisAufforderung(m, "(i/c) :")) << vramLines(m);
        typeString(m, "c");
        ASSERT_TRUE(bisAufforderung(m, "Buffer = $")) << vramLines(m);
        ASSERT_TRUE(eingabe(m, "55", kMenue)) << vramLines(m);

        typeString(m, "t");
        t = durchgang(m);
        EXPECT_NE(t.find("Raf=FF"), std::string::npos) << "gesperrt liest FFH\n" << vramLines(m);
        EXPECT_EQ(inhalt(*m.raf(), 512), vorher) << "Schreiben bei Sperre muss verpuffen";
    }

    // 00H öffnet: „O" liest den alten Inhalt (≠ 55H) und schreibt jedes falsche Byte neu.
    t = muster(m, "0");
    EXPECT_NE(t.find(" NOT Protected"), std::string::npos) << vramLines(m);
    typeString(m, "o");
    t = durchgang(m);
    EXPECT_NE(t.find("Raf="), std::string::npos) << vramLines(m);
    EXPECT_EQ(t.find("Raf=FF"), std::string::npos) << "offen darf nichts FFH liefern\n"
                                                    << vramLines(m);
    EXPECT_EQ(inhalt(*m.raf(), 512), std::vector<uint8_t>(512, 0x55));

    k1520test::typeCtrl(m, 'C');   // ^C im Menü → Warmstart
    EXPECT_TRUE(bisAufforderung(m, "A>")) << vramLines(m);
}

namespace {

/// Zähler unter der Beschriftung @p titel in der Mittelspalte des RAFQUICK-Schirms
/// (Spalten 36–41; Zeile darunter = Laufzahl, zwei darunter = Fehlerzahl).  -1 = nicht lesbar.
int quickZaehler(A5120Machine& m, const std::string& titel, int abstand) {
    const std::string t = visibleText(m);
    const size_t p = t.find(titel);
    if (p == std::string::npos) return -1;
    const size_t zeile = p / 80 + size_t(abstand);
    if ((zeile + 1) * 80 > t.size()) return -1;
    const std::string feld = t.substr(zeile * 80 + 36, 6);
    try {
        return std::stoi(feld);
    } catch (...) {
        return -1;
    }
}

}  // namespace

/**
 * @test RafZwg.RafquickZerstoerungsfreiOhneFehler
 * @brief `RAFQUICK.COM` (Turbo Pascal 3, ZWG 03/86): 8 Bänke, Datenport 88H aus der
 *        Auswahl.  Die Bildschirmsteuerung des Programms ist auf die SCP-Positionierung
 *        installiert (`ESC`, Zeile + 80H, Spalte + 80H) — dieselbe, die das CP/A-BIOS des
 *        A5120 versteht, das Formular steht also richtig.  Mindestens zwei Datenläufe und
 *        ein Adresslauf ohne Fehler; in der Pause ist der Karteninhalt unverändert, nach der
 *        Reset-Taste ebenso und CP/A steht wieder am Prompt.
 */
TEST_F(RafZwg, RafquickZerstoerungsfreiOhneFehler) {
    TempDisk disk(kCpa, "k1520_raf_zwg_quick.img");
    ASSERT_TRUE(aufDiskette(disk.path(), std::string(RAF_FIXTURE_DIR) + "/RAFQUICK.COM",
                            "RAFQUICK.COM"));
    auto mp = std::make_unique<A5120Machine>();
    A5120Machine& m = *mp;
    ASSERT_TRUE(m.installRaf(RAF::Typ::RAF512)) << m.rafFehler();
    ASSERT_TRUE(m.mountDisk(0, disk.path(), "cpa780", false)) << m.lastError();

    m.powerOn();
    ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
    ASSERT_TRUE(bisAufforderung(m, "A>")) << vramLines(m);

    // Altinhalt, den ein zerstörungsfreier Test stehen lassen muss.
    RAF& raf = *m.raf();
    for (uint32_t i = 0; i < raf.kapazitaet(); ++i) raf.poke(i, uint8_t(i * 7 ^ (i >> 7)));
    const std::vector<uint8_t> vorher = inhalt(raf, raf.kapazitaet());

    ASSERT_TRUE(eingabe(m, "RAFQUICK", "[<cr> = 8]")) << vramLines(m);
    ASSERT_TRUE(eingabe(m, "", "select:")) << vramLines(m);
    EXPECT_NE(visibleText(m).find("1: 0088h"), std::string::npos) << vramLines(m);
    typeString(m, "1");
    typeKey(m, k1520test::QK_RETURN);
    ASSERT_TRUE(runSmallUntilVisible(m, "zerstoerungsfreier RAF  512 Test", kDialog))
        << vramLines(m);

    for (long long d = 0; d < kDurchgang; d += 10'000'000) {
        k1520test::runCycles(m, 10'000'000);
        if (quickZaehler(m, "D-Lauf", 1) >= 2 && quickZaehler(m, "A-Lauf", 1) >= 1) break;
    }
    EXPECT_GE(quickZaehler(m, "D-Lauf", 1), 2) << vramLines(m);
    EXPECT_GE(quickZaehler(m, "A-Lauf", 1), 1) << vramLines(m);
    EXPECT_EQ(quickZaehler(m, "D-Lauf", 3), 0) << "Datenfehler\n" << vramLines(m);
    EXPECT_EQ(quickZaehler(m, "A-Lauf", 3), 0) << "Adressfehler\n" << vramLines(m);
    EXPECT_NE(visibleText(m).find("88,89h"), std::string::npos) << vramLines(m);

    // Leertaste = Pause: `exmonitor` hält am Ende der Bank bzw. des Adresslaufs — dort ist
    // jeder Sektor zurückgeschrieben; erkannt an stehenden Zählern und stehendem Inhalt.
    // NICHT mit `x` beenden: das Programm ist mit Endadresse CCC0H übersetzt, seine globalen Variablen
    // liegen auf C955H…CCBFH und damit im BDOS dieses CP/A (C400H) — das Programm selbst
    // läuft (es spricht die Konsole über die BIOS-Vektoren an), der Warmstart danach
    // nicht mehr (Befund AP-R6).  Zurück also über die Reset-Taste, wie am Gerät.
    typeString(m, " ");
    k1520test::runCycles(m, 60'000'000);
    const int d0 = quickZaehler(m, "D-Lauf", 1), a0 = quickZaehler(m, "A-Lauf", 1);
    const std::vector<uint8_t> pause = inhalt(raf, raf.kapazitaet());
    k1520test::runCycles(m, 10'000'000);
    EXPECT_TRUE(inhalt(raf, raf.kapazitaet()) == pause) << "Programm steht nicht in der Pause";
    EXPECT_EQ(quickZaehler(m, "D-Lauf", 1), d0) << vramLines(m);
    EXPECT_EQ(quickZaehler(m, "A-Lauf", 1), a0) << vramLines(m);
    EXPECT_TRUE(pause == vorher) << "RAFQUICK muss zerstörungsfrei sein";

    k1520test::wipeVram(m);
    m.reset();
    ASSERT_TRUE(runSmallUntilVisible(m, "A>", kBootBudget)) << vramLines(m);
    EXPECT_TRUE(bisAufforderung(m, "A>")) << vramLines(m);
    EXPECT_TRUE(inhalt(raf, raf.kapazitaet()) == vorher) << "RESET erhält den Inhalt";
}
