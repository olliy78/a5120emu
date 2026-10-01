/**
 * @file test_em256_adr.cpp
 * @brief G1-Prüfprogramm `em256adr.com` (doc/design/17_a5120_16.md §3 G1) gegen den
 *        Emulator: Portbasis A8H, Attributspeicher A22 negiert, Lesart der Leseadresse.
 *
 * Das Programm stammt aus der CPA-Workbench (`tools/16bitTest/src/em256adr.mac`) und ist
 * für den echten A5120.16 geschrieben — es kennt den Emulator nicht.  Läuft es hier mit
 * dem erwarteten Ergebnis durch, stimmen Programm und Kartenmodell überein; die Messung
 * am Gerät ist die Gegenprobe beider.  Die `.com` liegt als Fixture unter
 * `tests/fixtures/cpm/` (tests/fixtures/README.md), der Test braucht die Workbench nicht.
 *
 * Diskette: `cpa_cpa780_k5601_noclock.img` (BIOS OHNE `em256`) — das BIOS fasst die
 * Karte also nicht an, das Programm sieht sie im Zustand nach Netz-Ein/RESET.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"

using k1520test::runSmallUntil;
using k1520test::runUntilVramContains;
using k1520test::TempDisk;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramLines;
using k1520test::vramText;

namespace {

constexpr int       kBootBudget  = 90'000'000;
constexpr int       kInputBudget = 40'000'000;
constexpr long long kProgBudget  = 400'000'000;   // ganzer Programmlauf inkl. Seitenpausen
const char*         kFixture     = "cpa_cpa780_k5601_noclock.img";
const char*         kPause       = "-- weiter mit Taste";

/// em256adr.com auf die (Temp-)Diskette schreiben.
::testing::AssertionResult programmAufDiskette(const std::string& pfad) {
    const std::string prog = k1520test::readFileBytes(EM256ADR_COM);
    if (prog.empty())
        return ::testing::AssertionFailure() << "Fixture fehlt: " << EM256ADR_COM;
    std::string f;
    static FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    const FsProfile*  p  = fs.find("cpa780");
    const DiskFormat* df = fk.find(p->format);
    auto disk = DiskImage::open(pfad, std::optional<DiskFormat>(*df), false);
    if (!disk) return ::testing::AssertionFailure() << "Abbild nicht ladbar";
    SectorSpace space(disk->medium(), *df);
    std::string err;
    auto vol = CpmFileSystem::mount(space, *p, err);
    if (!vol) return ::testing::AssertionFailure() << err;
    if (!vol->write("EM256ADR.COM", std::vector<uint8_t>(prog.begin(), prog.end()), {}))
        return ::testing::AssertionFailure() << vol->lastError();
    if (!disk->flush()) return ::testing::AssertionFailure() << disk->lastError();
    return ::testing::AssertionSuccess();
}

/// Letzte nicht leere Bildschirmzeile, rechts gestutzt.
std::string letzteZeile(A5120Machine& m) {
    const std::string s = vramLines(m);
    std::string letzte;
    size_t a = 0;
    while (a < s.size()) {
        size_t e = s.find('\n', a);
        if (e == std::string::npos) e = s.size();
        std::string z = s.substr(a, e - a);
        z.erase(z.find_last_not_of(' ') + 1);
        if (!z.empty()) letzte = z;
        a = e + 1;
    }
    return letzte;
}

/**
 * @brief EM256ADR starten und bis zum Prompt laufen lassen; jede Seitenpause wird mit
 *        einer Leertaste quittiert.  Liefert die Abschrift aller Bildschirme (je Pause
 *        einer, dazu der letzte) — der Bildschirm rollt, einzeln sähe man zu wenig.
 */
std::string lasseLaufen(A5120Machine& m) {
    typeString(m, "EM256ADR");
    typeKey(m, k1520test::QK_RETURN);
    std::string abschrift;
    long long rest = kProgBudget;
    // Erst den Start abwarten: das Banner verdrängt die Kommandozeile „A>EM256ADR".
    if (!runSmallUntil(m, "EM256ADR 1.0", kInputBudget)) return vramLines(m);
    while (rest > 0) {
        k1520test::runCycles(m, 200'000);
        rest -= 200'000;
        if (vramText(m).find(kPause) != std::string::npos) {
            abschrift += vramLines(m) + "----\n";
            typeKey(m, ' ');
            // Warten, bis die Pausenzeile gelöscht ist (M_DELL).
            for (int i = 0; i < 200 && vramText(m).find(kPause) != std::string::npos; ++i) {
                k1520test::runCycles(m, 100'000);
                rest -= 100'000;
            }
            continue;
        }
        if (letzteZeile(m) == "A>") break;
    }
    abschrift += vramLines(m);
    return abschrift;
}

bool enthaelt(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

struct Lauf {
    std::unique_ptr<TempDisk>     disk;
    std::unique_ptr<A5120Machine> m;
    std::string                   abschrift;
};

void starte(Lauf& l, const A5120Machine::Config& cfg, const char* tmp) {
    l.disk = std::make_unique<TempDisk>(kFixture, tmp);
    ASSERT_TRUE(programmAufDiskette(l.disk->path()));
    l.m = std::make_unique<A5120Machine>(cfg);
    ASSERT_TRUE(l.m->mountDisk(0, l.disk->path(), "cpa780", false)) << l.m->lastError();
    l.m->powerOn();
    ASSERT_TRUE(runUntilVramContains(*l.m, "TPA ist OK!", kBootBudget)) << vramLines(*l.m);
    ASSERT_TRUE(runSmallUntil(*l.m, "A>", kInputBudget)) << vramLines(*l.m);
    l.abschrift = lasseLaufen(*l.m);
    ASSERT_EQ(letzteZeile(*l.m), "A>") << "Programm kehrt nicht zurück:\n" << l.abschrift;
}

A5120Machine::Config em256(EM::A22Lesart lesart) {
    A5120Machine::Config c;
    c.em            = A5120Machine::Config::Em::em256;
    c.em_a22_lesart = lesart;
    return c;
}

}  // namespace

/**
 * @test Em256Adr/BelegteLesartFindetAttributspeicherBeiA8
 * @brief Vorgabe-Lesart (AB12–15 des E/A-Zyklus = B): „Attributspeicher bei A8H,
 *        16 Seiten x 4 Bit, negiert", Lesart „B aus IN A,(C)", alle Muster fehlerfrei,
 *        Aufräumen ok — danach stehen alle 16 A22-Einträge auf 07H (PEN = 0, WE = 0).
 */
TEST(Em256Adr, BelegteLesartFindetAttributspeicherBeiA8) {
    Lauf l;
    ASSERT_NO_FATAL_FAILURE(starte(l, em256(EM::A22Lesart::ZyklusAdresse), "k1520_em256adr_b.img"));
    const std::string& t = l.abschrift;
    if (std::getenv("K1520_ABSCHRIFT")) std::printf("%s\n", t.c_str());
    EXPECT_TRUE(enthaelt(t, "ERGEBNIS: Attributspeicher bei A8H, 16 Seiten x 4 Bit, negiert")) << t;
    EXPECT_TRUE(enthaelt(t, "Lesart: Leseadresse = B aus IN A,(C)")) << t;
    // Matrix „gelesene Seite je B" — in jeder Stub-Seite dieselbe Zeile 0..F.
    EXPECT_TRUE(enthaelt(t, "s0:0123456789ABCDEF s1:0123456789ABCDEF")) << t;
    EXPECT_TRUE(enthaelt(t, "Muster F,5         je Seite 0..F: ++++++++++++++++")) << t;
    EXPECT_TRUE(enthaelt(t, "Muster 0,A,Seite   je Seite 0..F: ++++++++++++++++")) << t;
    EXPECT_TRUE(enthaelt(t, "Kontrolle ok")) << t;
    EXPECT_FALSE(enthaelt(t, "erste Abweichung")) << t;
    for (int p = 0; p < 16; ++p)
        EXPECT_EQ(l.m->em()->attribute(p), 0x07) << "Seite " << p;
}

/**
 * @test Em256Adr/HandbuchLesartWirdErkannt
 * @brief Gegenprobe mit der wörtlichen Handbuchlesart (A22 liest die Seite des letzten
 *        Speicherzugriffs): das Programm muss die ANDERE Lesart erkennen — sonst könnte
 *        es am Gerät die beiden nicht unterscheiden.
 */
TEST(Em256Adr, HandbuchLesartWirdErkannt) {
    Lauf l;
    ASSERT_NO_FATAL_FAILURE(
        starte(l, em256(EM::A22Lesart::LetzterSpeicherzugriff), "k1520_em256adr_h.img"));
    const std::string& t = l.abschrift;
    if (std::getenv("K1520_ABSCHRIFT")) std::printf("%s\n", t.c_str());
    // Matrix: die gelesene Seite folgt der Seite des IN-Befehls, nicht B.
    EXPECT_TRUE(enthaelt(t, "s0:0000000000000000 s1:1111111111111111")) << t;
    EXPECT_TRUE(enthaelt(t, "Lesart: Leseadresse = Seite des letzten Befehlsholens")) << t;
    EXPECT_TRUE(enthaelt(t, "Lesen adressiert: Seite des letzten Befehlsholens")) << t;
    // Seiten ohne Lese-Stub (oberhalb der TPA) sind in dieser Lesart nicht prüfbar.
    EXPECT_TRUE(enthaelt(t, "Muster F,5         je Seite 0..F: ++++++++++++")) << t;
    EXPECT_TRUE(enthaelt(t, "Kontrolle ok")) << t;
    for (int p = 0; p < 16; ++p)
        EXPECT_EQ(l.m->em()->attribute(p), 0x07) << "Seite " << p;
}

/**
 * @test Em256Adr/OhneKarteKeineKarte
 * @brief A5120 ohne EM: „ERGEBNIS: keine Karte", zurück am Prompt.
 */
TEST(Em256Adr, OhneKarteKeineKarte) {
    Lauf l;
    ASSERT_NO_FATAL_FAILURE(starte(l, A5120Machine::Config{}, "k1520_em256adr_ohne.img"));
    const std::string& t = l.abschrift;
    if (std::getenv("K1520_ABSCHRIFT")) std::printf("%s\n", t.c_str());
    EXPECT_TRUE(enthaelt(t, "ERGEBNIS: keine Karte")) << t;
    EXPECT_FALSE(enthaelt(t, "Schreibtest bei")) << t;
}
