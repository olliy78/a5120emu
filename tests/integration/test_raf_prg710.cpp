/**
 * @file test_raf_prg710.cpp
 * @brief Gastnachweis RAF am PRG 710-1 (doc/design/22_raf512.md §8 AP-R5): der Original-
 *        Treiber `RAF512.COM` (DKt 26.07.08, Laufwerk P:) unter SCPX 1526 V1.7 gegen die
 *        Nachbildung der Karte — der Weg, den der Anwender am Gerät benutzt (§1).
 *
 * Bootdiskette `prg710-1_scpx17_cpa640_boot.hfe` als TempDisk, der Treiber kommt mit
 * `DiskVolume::insert` darauf.  Bedienung wie in test_prg710_scpx.cpp: Tasten über
 * `keyPress` **und** `keyRelease` (am 710-1 Pflicht, Merkposten prg710.md), Bild über
 * `screenChar` (nie über den Speicher).
 */
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"

namespace {
using V = Prg710Machine::Config::Variante;

constexpr int       kSchritt  = 20'000;
constexpr long long kBoot     = 60'000'000;
constexpr long long kBefehl   = 60'000'000;
constexpr uint32_t  QK_RETURN = 0x01000004;
const char*         kDisk     = "prg710-1_scpx17_cpa640_boot.hfe";
const char*         kGruss    = "SCPX 1526 - V 1.7 (52K)";

// Texte wörtlich aus RAF512.COM.
const char* kKapazitaet = "RAF-Gesamtkapazitaet 512K Bytes (32 Spuren zu 128 Sektoren)";
const char* kUndef      = "RAF ist undefiniert, es folgt Loeschen Directory";
const char* kGeladen    = "RAF ist noch wie bei letzter Benutzung geladen!";
const char* kKeine      = "Keine RAF-Karte vorhanden!";

::testing::AssertionResult treiberAufDiskette(const std::string& pfad, const std::string& datei) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(pfad, "scpx640", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    FileRef ref;
    ref.name = datei;
    if (!vol->insert(std::string(RAF_FIXTURE_DIR) + "/" + datei, ref, TransferOptions{}))
        return ::testing::AssertionFailure() << vol->lastError();
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

std::string zeile(Prg710Machine& m, int r) {
    std::string s;
    for (int c = 0; c < 80; ++c) {
        const uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
std::string bild(Prg710Machine& m) {
    std::string s;
    for (int r = 0; r < 24; ++r) s += zeile(m, r) + "\n";
    return s;
}
std::string letzteZeile(Prg710Machine& m) {
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (!z.empty()) return z;
    }
    return {};
}

void lauf(Prg710Machine& m, long long n) {
    for (long long d = 0; d < n;) d += m.run(kSchritt);
}
/// Bis die letzte Zeile `A>` ist und 2 Mio. Takte so bleibt (Ausgabe fertig).
bool bisPrompt(Prg710Machine& m, long long frist) {
    long long ruhig = 0;
    for (long long d = 0; d < frist;) {
        const int n = m.run(kSchritt);
        d += n;
        ruhig = (letzteZeile(m) == "A>") ? ruhig + n : 0;
        if (ruhig >= 2'000'000) return true;
    }
    return false;
}
/// Erst bis die letzte Zeile NICHT `A>` ist (das alte Bild überlebt die Reset-Taste),
/// dann bis zum neuen Prompt.
bool bisNeuemPrompt(Prg710Machine& m, long long frist) {
    long long d = 0;
    while (d < frist && letzteZeile(m) == "A>") d += m.run(kSchritt);
    return d < frist && bisPrompt(m, frist - d);
}

void taste(Prg710Machine& m, uint32_t k) {
    m.keyPress(k, false, false);
    lauf(m, 100'000);
    m.keyRelease(k);
    lauf(m, 100'000);
}
bool kommando(Prg710Machine& m, const std::string& k) {
    for (char c : k) taste(m, static_cast<uint8_t>(c));
    taste(m, QK_RETURN);
    return bisPrompt(m, kBefehl);
}

/// Bildtext hinter dem letzten `A><cmd>` — die Ausgabe genau dieses Kommandos.
std::string ausgabe(Prg710Machine& m, const std::string& cmd) {
    const std::string t = bild(m);
    const size_t p = t.rfind("A>" + cmd);
    return p == std::string::npos ? std::string() : t.substr(p);
}
bool hat(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

/// Netz-Ein → Starttaste → `A>`.
void kaltstart(Prg710Machine& m) {
    m.powerOn();
    lauf(m, 3'000'000);                              // ROM bis zur Tastaturabfrage
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisNeuemPrompt(m, kBoot)) << bild(m);
    EXPECT_TRUE(hat(bild(m), kGruss)) << bild(m);
}

struct Aufbau {
    k1520test::TempDisk            disk;
    std::unique_ptr<Prg710Machine> m;
    Aufbau(const char* tmp, bool raf) : disk(kDisk, tmp) {
        EXPECT_TRUE(treiberAufDiskette(disk.path(), "RAF512.COM"));
        Prg710Machine::Config c;
        c.variante = V::Prg710_1;
        m = std::make_unique<Prg710Machine>(c);
        if (raf) EXPECT_TRUE(m->installRaf(RAF::Typ::RAF512)) << m->rafFehler();
        EXPECT_TRUE(m->mountDisk(0, disk.path(), m->defaultFormatName(0), false)) << m->lastError();
    }
};

}  // namespace

class RafPrg710 : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test RafPrg710.Raf512LegtPAnUndUeberstehtReset
 * @brief `RAF512` unter SCPX 1526 V1.7 am 710-1: 512K / 32 Spuren, „undefiniert …
 *        Loeschen Directory", P: installiert; `PIP P:=A:S*.COM`, `DIR P:`.  Reset-Taste →
 *        Starttaste → `RAF512` erneut → „noch wie bei letzter Benutzung geladen", Dateien
 *        da.  Netz-Ein → „undefiniert", P: leer.
 */
TEST_F(RafPrg710, Raf512LegtPAnUndUeberstehtReset) {
    Aufbau x("k1520_raf_prg710_p.hfe", true);
    Prg710Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));

    ASSERT_TRUE(kommando(m, "RAF512")) << bild(m);
    std::string a = ausgabe(m, "RAF512");
    EXPECT_TRUE(hat(a, "Nachladbare RAF-Installation, (DKt) v.26.07.08 ohne Parity")) << bild(m);
    EXPECT_TRUE(hat(a, kKapazitaet)) << bild(m);
    EXPECT_TRUE(hat(a, kUndef)) << bild(m);
    EXPECT_TRUE(hat(a, "RAF als Laufwerk P: installiert")) << bild(m);

    ASSERT_TRUE(kommando(m, "PIP P:=A:ST*.COM")) << bild(m);
    ASSERT_TRUE(kommando(m, "DIR P:")) << bild(m);
    a = ausgabe(m, "DIR P:");
    EXPECT_TRUE(hat(a, "P: STAT     COM")) << bild(m);

    m.reset();                                       // systemweiter /RESET
    lauf(m, 3'000'000);
    taste(m, QK_RETURN);                             // Starttaste
    ASSERT_TRUE(bisNeuemPrompt(m, kBoot)) << bild(m);
    ASSERT_TRUE(kommando(m, "RAF512")) << bild(m);
    a = ausgabe(m, "RAF512");
    EXPECT_TRUE(hat(a, kGeladen)) << bild(m);
    EXPECT_FALSE(hat(a, kUndef)) << bild(m);
    ASSERT_TRUE(kommando(m, "DIR P:")) << bild(m);
    EXPECT_TRUE(hat(ausgabe(m, "DIR P:"), "P: STAT     COM")) << "nach RESET\n" << bild(m);

    ASSERT_NO_FATAL_FAILURE(kaltstart(m));           // Netz-Ein ohne Pufferung
    ASSERT_TRUE(kommando(m, "RAF512")) << bild(m);
    EXPECT_TRUE(hat(ausgabe(m, "RAF512"), kUndef)) << bild(m);
    ASSERT_TRUE(kommando(m, "DIR P:")) << bild(m);
    EXPECT_FALSE(hat(ausgabe(m, "DIR P:"), "STAT     COM")) << "nach Netz-Ein\n" << bild(m);
}

/**
 * @test RafPrg710.Raf512OhneKarte
 * @brief Ohne gesteckte RAF: „Keine RAF-Karte vorhanden!", zurück am Prompt.
 */
TEST_F(RafPrg710, Raf512OhneKarte) {
    Aufbau x("k1520_raf_prg710_ohne.hfe", false);
    Prg710Machine& m = *x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    ASSERT_TRUE(kommando(m, "RAF512")) << bild(m);
    const std::string a = ausgabe(m, "RAF512");
    EXPECT_TRUE(hat(a, kKeine)) << bild(m);
    EXPECT_FALSE(hat(a, "installiert")) << bild(m);
}
