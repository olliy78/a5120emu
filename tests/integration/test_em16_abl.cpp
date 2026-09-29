/**
 * @file test_em16_abl.cpp
 * @brief Abnahme S4 (doc/design/17_a5120_16.md §3 S4): Prüfprogramm `em16abl.com` unter
 *        CP/A gegen EM256 + U8001 — Ende-zu-Ende.
 *
 * Das Programm stammt aus der CPA-Workbench (`tools/16bitTest/src/em16abl.mac` +
 * `fw16abl.s`, U8001-Teil mit z8kasm) und ist für den echten A5120.16 geschrieben
 * (Vorlage für G2).  Es fährt die in §7 belegten Abläufe: Start aus Reset, Rechnen,
 * A33/A35/Status-8, Rückweg TRQ8 → MSET → TREN → BUSRQ/BUSAK → A29 beim M1, alle drei
 * Segmentmodi, VI vom U880, INT-16 zum U880 (IM 2), A53 → NVI, STOP, RESET16.
 *
 * Diskette: `cpa_cpa780_k5601_noclock.img` (BIOS OHNE `em256`), wie Em256Adr.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>
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

/// em16abl.com auf die (Temp-)Diskette schreiben.
::testing::AssertionResult programmAufDiskette(const std::string& pfad) {
    const std::string prog = k1520test::readFileBytes(EM16ABL_COM);
    if (prog.empty())
        return ::testing::AssertionFailure() << "Fixture fehlt: " << EM16ABL_COM;
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
    if (!vol->write("EM16ABL.COM", std::vector<uint8_t>(prog.begin(), prog.end()), {}))
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
 * @brief EM16ABL starten und bis zum Prompt laufen lassen; jede Seitenpause wird mit
 *        einer Leertaste quittiert.  Liefert die Abschrift aller Bildschirme (je Pause
 *        einer, dazu der letzte) — der Bildschirm rollt, einzeln sähe man zu wenig.
 */
std::string lasseLaufen(A5120Machine& m) {
    typeString(m, "EM16ABL");
    typeKey(m, k1520test::QK_RETURN);
    std::string abschrift;
    long long rest = kProgBudget;
    // Erst den Start abwarten: das Banner verdrängt die Kommandozeile „A>EM256ADR".
    if (!runSmallUntil(m, "EM16ABL 1.0", kInputBudget)) return vramLines(m);
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

A5120Machine::Config em256() {
    A5120Machine::Config c;
    c.em = A5120Machine::Config::Em::em256;
    return c;
}

}  // namespace

/**
 * @test Em16Abl/AlleAblaeufeOk
 * @brief Jede Zeile A–G „OK", Ergebnis „alle Abläufe OK"; dazu die beiden Messwerte
 *   für G2/G3 so, wie der Kern sie annimmt (OUTB auf beiden Hälften ⇒ A35 = 01H;
 *   A53-Vorlast 15 ⇒ NVI nach 8 Stapelzugriffen); danach Ruhezustand der Karte.
 */
TEST(Em16Abl, AlleAblaeufeOk) {
    Lauf l;
    ASSERT_NO_FATAL_FAILURE(starte(l, em256(), "k1520_em16abl.img"));
    const std::string& t = l.abschrift;
    if (std::getenv("K1520_ABSCHRIFT")) std::printf("%s\n", t.c_str());
    for (const char* z : {
             "A Start aus Reset (A29 sofort, Vektor Segment 0) OK",
             "A Rechnen, Ergebnis im EM-Speicher               OK",
             "A Wort-OUT: A33 -> PIO A0-2, A35 -> AEH          OK",
             "A Wort-IN: Status-8 im oberen Byte               OK",
             "A Rueckweg TRQ8/MSET/TREN/BUSAK -> A29 beim M1   OK",
             "B Segmentweiche Mode 0 (A33 Bit 5/6)             OK",
             "B Segmentweiche Mode 1 (INSTR x N/S)             OK",
             "B Segmentweiche Mode 2 (SN0/SN1)                 OK",
             "C VI vom U880 (Kennung = Vektor + Status-8)      OK",
             "D INT-16 -> PIO A4 -> Interrupt U880             OK",
             "E Einzelbefehlszaehler A53 -> NVI                OK",
             "F STOP haelt den U8001 an                        OK",
             "G RESET16: 8-Bit-Mode, A33 geloescht             OK",
             "INB liest untere Haelfte: FFH",
             "G2: OUTB %81,01H -> A35 = 01H, PIO A0-2 = 1",
             "G3: NVI nach 0008H Stapelzugriffen",
             "ERGEBNIS: alle Ablaeufe OK"})
        EXPECT_TRUE(enthaelt(t, z)) << z << "\n" << t;
    EXPECT_FALSE(enthaelt(t, "FEHLER")) << t;
    const EM& em = *l.m->em();
    EXPECT_TRUE(em.reset16());
    EXPECT_TRUE(em.mode8());
    EXPECT_FALSE(em.ramEnabled());
    for (int p = 0; p < 16; ++p) EXPECT_EQ(em.attribute(p), 0x07) << "Seite " << p;
}

/**
 * @test Em16Abl/OhneKarte
 * @brief A5120 ohne EM: „keine Karte an A8H", zurück am Prompt.
 */
TEST(Em16Abl, OhneKarte) {
    Lauf l;
    ASSERT_NO_FATAL_FAILURE(starte(l, A5120Machine::Config{}, "k1520_em16abl_ohne.img"));
    EXPECT_TRUE(enthaelt(l.abschrift, "ERGEBNIS: keine Karte an A8H")) << l.abschrift;
}

/**
 * @test Em16Abl/AltePruefprogrammeAbschrift
 * @brief Nur von Hand (Plan §3 S4: „danach em256tst/em256ful im Emulator laufen lassen"):
 *   mit `K1520_EM256_ALT=<Verzeichnis mit em256tst.com/em256ful.com>` wird jedes der
 *   beiden alten Workbench-Programme gestartet, jede Tastenpause mit Leertaste
 *   quittiert und die Abschrift ausgegeben.  Keine Erwartung: die Programme sind
 *   Hypothesen (liefen nie am Gerät); Abweichungen sind Befunde über SIE.
 */
TEST(Em16Abl, AltePruefprogrammeAbschrift) {
    const char* dir = std::getenv("K1520_EM256_ALT");
    if (!dir) GTEST_SKIP() << "K1520_EM256_ALT nicht gesetzt";
    for (const char* name : {"EM256TST", "EM256FUL"}) {
        std::string lower(name);
        for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        std::ifstream f(std::string(dir) + "/" + lower + ".com", std::ios::binary);
        ASSERT_TRUE(f) << lower;
        std::stringstream ss; ss << f.rdbuf();
        const std::string prog = ss.str();
        TempDisk disk(kFixture, ("k1520_" + lower + ".img").c_str());
        {
            std::string e;
            static FormatCatalog fk = FormatCatalog::loadDefault(&e);
            static FsCatalog     fs = FsCatalog::loadDefault(fk, &e);
            const FsProfile*  p  = fs.find("cpa780");
            const DiskFormat* df = fk.find(p->format);
            auto img = DiskImage::open(disk.path(), std::optional<DiskFormat>(*df), false);
            ASSERT_TRUE(img);
            SectorSpace space(img->medium(), *df);
            auto vol = CpmFileSystem::mount(space, *p, e);
            ASSERT_TRUE(vol) << e;
            vol->erase(std::string(name) + ".COM");   // die Fixture kann eine alte Fassung tragen
            ASSERT_TRUE(vol->write(std::string(name) + ".COM",
                                   std::vector<uint8_t>(prog.begin(), prog.end()), {}))
                << vol->lastError();
            ASSERT_TRUE(img->flush());
        }
        A5120Machine m(em256());
        ASSERT_TRUE(m.mountDisk(0, disk.path(), "cpa780", false));
        m.powerOn();
        ASSERT_TRUE(runUntilVramContains(m, "TPA ist OK!", kBootBudget));
        ASSERT_TRUE(runSmallUntil(m, "A>", kInputBudget));
        typeString(m, name);
        typeKey(m, k1520test::QK_RETURN);
        k1520test::runCycles(m, 2'000'000);
        // Abschrift über das Rollen hinweg: je Blick nur die neu hinzugekommenen Zeilen.
        std::vector<std::string> alt;
        std::string abschrift;
        auto zeilen = [&] {
            std::vector<std::string> z;
            const std::string v = k1520test::visibleText(m);
            for (size_t i = 0; i + 80 <= v.size(); i += 80) {
                std::string l = v.substr(i, 80);
                l.erase(l.find_last_not_of(' ') + 1);
                z.push_back(l);
            }
            if (!z.empty()) z.pop_back();          // letzte Zeile ist evtl. noch im Entstehen
            return z;
        };
        auto nimm = [&] {
            // Nur ein ruhiges Bild auswerten (nicht mitten im Rollen des BIOS).
            auto neu = zeilen();
            k1520test::runCycles(m, 5'000);
            if (zeilen() != neu) return;
            size_t k = 0;   // größte Überdeckung: Anfang von neu == Ende von alt
            for (size_t n = std::min(neu.size(), alt.size()); n > 0; --n)
                if (std::equal(neu.begin(), neu.begin() + long(n), alt.end() - long(n))) { k = n; break; }
            for (size_t i = k; i < neu.size(); ++i) abschrift += neu[i] + "\n";
            alt = neu;
        };
        long long rest = 3'000'000'000LL;
        const char* pausen[] = {"weiter mit Taste", "Taste druecken", "Beliebige Taste"};
        while (rest > 0) {
            k1520test::runCycles(m, 20'000);
            rest -= 25'000;
            nimm();
            const std::string v = vramText(m);
            bool pause = false;
            for (const char* pz : pausen) pause |= v.find(pz) != std::string::npos;
            if (pause) {
                typeKey(m, ' ');
                for (int i = 0; i < 100; ++i) {
                    k1520test::runCycles(m, 100'000);
                    rest -= 100'000;
                    bool noch = false;
                    for (const char* pz : pausen) noch |= vramText(m).find(pz) != std::string::npos;
                    if (!noch) break;
                }
                continue;
            }
            if (letzteZeile(m) == "A>") break;
        }
        abschrift += "[letztes Bild]\n" + vramLines(m);
        std::printf("===== %s (Rest %lld Takte) =====\n%s\n", name, rest, abschrift.c_str());
        const EM& em = *m.em();
        std::printf("EM: reset16=%d mode8=%d tren=%d busak=%d A33=%02X A35=%02X U8001 pc=<<%u>>%04X "
                    "fcw=%04X halted=%d illegal=%llu\n",
                    em.reset16(), em.mode8(), em.tren(), em.busAck16(), em.steuer16(),
                    em.status16(), em.u8001().pcSeg, em.u8001().pc, em.u8001().fcw,
                    em.u8001().halted(), (unsigned long long)em.u8001().illegalCount());
    }
}
