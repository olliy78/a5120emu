/**
 * @file test_raf_k8915.cpp
 * @brief Gastnachweis RAF am K8915 (doc/design/22_raf512.md §8 AP-R5): der Original-
 *        Treiber `RAF512.COM` (DKt 26.07.08, Laufwerk P:) unter SCPX 8915 V5.3 gegen die
 *        Nachbildung der Karte; dazu `RAFCPM.COM` (M:) zur Frage aus §3.3, ob M: am K8915
 *        schon belegt ist.
 *
 * Bootdiskette `k8915scpx_cpa800_k5601_bios55k-disk900.hfe` (Diskette 900, Fassung „55 K",
 * die einzige der drei mit `PIP.COM`) als
 * TempDisk, die Treiber legt `DiskVolume::insert` darauf.  Bedienung wie in
 * test_k8915_scpx.cpp: Zeichen über die K7672 (`sendeZeichen`), Bild direkt von der K7024.
 */
#include <gtest/gtest.h>

#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"

using k1520test::TempDisk;
using k1520test::vramLines;
using k1520test::vramText;

namespace {

constexpr int       kSchritt = 100'000;
constexpr long long kBefehl  = 120'000'000;

// Texte wörtlich aus RAF512.COM / RAFCPM.COM.
const char* kKapazitaet = "RAF-Gesamtkapazitaet 512K Bytes (32 Spuren zu 128 Sektoren)";
const char* kUndef      = "RAF ist undefiniert, es folgt Loeschen Directory";
const char* kGeladen    = "RAF ist noch wie bei letzter Benutzung geladen!";
const char* kKeine      = "Keine RAF-Karte vorhanden!";

::testing::AssertionResult treiberAufDiskette(const std::string& pfad, const std::string& datei) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(pfad, "", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    FileRef ref;
    ref.name = datei;
    if (!vol->insert(std::string(RAF_FIXTURE_DIR) + "/" + datei, ref, TransferOptions{}))
        return ::testing::AssertionFailure() << vol->lastError();
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

bool enthaelt(K8915Machine& m, const std::string& s) {
    return vramText(m).find(s) != std::string::npos;
}

bool bis(K8915Machine& m, const std::string& text, long long frist) {
    for (long long t = 0; t < frist; t += m.run(kSchritt))
        if (enthaelt(m, text)) return true;
    return enthaelt(m, text);
}

std::string letzteZeile(K8915Machine& m) {
    const std::string t = vramText(m);
    std::string letzte;
    for (size_t z = 0; z + 80 <= t.size(); z += 80) {
        std::string zeile = t.substr(z, 80);
        while (!zeile.empty() && (zeile.back() == ' ' || zeile.back() == '\0')) zeile.pop_back();
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

/// Kommando tippen und bis zum nächsten Prompt (Muster test_k8915_scpx.cpp: K7672 leer,
/// BIOS-Tastaturpuffer F150H leer, `A>` nach dem Echo).
bool befehl(K8915Machine& m, const std::string& cmd) {
    for (char c : cmd) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    m.keyboard().sendeZeichen(0x0D);
    bool echo = false;
    for (long long t = 0; t < kBefehl; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    }
    return false;
}

/// Bildtext hinter dem letzten `A><cmd>` — die Ausgabe genau dieses Kommandos, auch
/// wenn ältere Ausgaben noch im Bild stehen (die Reset-Taste löscht es nicht).
std::string ausgabe(K8915Machine& m, const std::string& cmd) {
    const std::string t = vramText(m);
    const size_t p = t.rfind("A>" + cmd);
    return p == std::string::npos ? std::string() : t.substr(p);
}
bool hat(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

/// Nach der Coldstart-Meldung `CR` und bis zum Prompt hinter `rade`.
void ladenBisPrompt(K8915Machine& m) {
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "size: 1 kByte groups 0 ... 03FH", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 20'000'000 && letzteZeile(m) != "A>"; t += m.run(kSchritt)) {}
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);
}

/// Netz-Ein, ohne Selbsttest (JP bei 0000H/0005H, test_k8915_scpx.cpp) bis zum Prompt.
void kaltstart(K8915Machine& m) {
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
    ladenBisPrompt(m);
}

struct Aufbau {
    TempDisk     a;
    K8915Machine m;
    Aufbau(const char* tmp, bool raf) : a("k8915scpx_cpa800_k5601_bios55k-disk900.hfe", tmp) {
        EXPECT_TRUE(treiberAufDiskette(a.path(), "RAF512.COM"));
        EXPECT_TRUE(treiberAufDiskette(a.path(), "RAFCPM.COM"));
        if (raf) EXPECT_TRUE(m.installRaf(RAF::Typ::RAF512)) << m.rafFehler();
        EXPECT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    }
};

}  // namespace

class RafK8915 : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test RafK8915.Raf512LegtPAnUndUeberstehtReset
 * @brief `RAF512` unter SCPX 8915: 512K / 32 Spuren, „undefiniert … Loeschen Directory",
 *        P: installiert; `PIP P:=A:R*.COM`, `DIR P:`.  Reset-Taste → `RAF512` erneut →
 *        „noch wie bei letzter Benutzung geladen", Dateien da.  Netz-Ein → „undefiniert".
 */
TEST_F(RafK8915, Raf512LegtPAnUndUeberstehtReset) {
    Aufbau x("k1520_raf_k8915_p.hfe", true);
    K8915Machine& m = x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));

    ASSERT_TRUE(befehl(m, "raf512")) << vramLines(m);
    std::string a = ausgabe(m, "raf512");
    EXPECT_TRUE(hat(a, "Nachladbare RAF-Installation, (DKt) v.26.07.08 ohne Parity")) << vramLines(m);
    EXPECT_TRUE(hat(a, kKapazitaet)) << vramLines(m);
    EXPECT_TRUE(hat(a, kUndef)) << vramLines(m);
    EXPECT_TRUE(hat(a, "RAF als Laufwerk P: installiert")) << vramLines(m);

    ASSERT_TRUE(befehl(m, "pip p:=a:ra*.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir p:")) << vramLines(m);
    a = ausgabe(m, "dir p:");
    EXPECT_TRUE(hat(a, "P: RADE     COM")) << vramLines(m);
    EXPECT_TRUE(hat(a, "RAF512   COM")) << vramLines(m);
    EXPECT_TRUE(hat(a, "RAFCPM   COM")) << vramLines(m);

    m.reset();   // systemweiter /RESET; JP bei 0000H steht noch → ohne Selbsttest
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 20'000'000)) << vramLines(m);
    ASSERT_NO_FATAL_FAILURE(ladenBisPrompt(m));
    ASSERT_TRUE(befehl(m, "raf512")) << vramLines(m);
    a = ausgabe(m, "raf512");
    EXPECT_TRUE(hat(a, kGeladen)) << vramLines(m);
    EXPECT_FALSE(hat(a, kUndef)) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir p:")) << vramLines(m);
    a = ausgabe(m, "dir p:");
    EXPECT_TRUE(hat(a, "P: RADE     COM")) << "nach RESET\n" << vramLines(m);
    EXPECT_TRUE(hat(a, "RAF512   COM")) << vramLines(m);

    ASSERT_NO_FATAL_FAILURE(kaltstart(m));   // Netz-Ein ohne Pufferung
    ASSERT_TRUE(befehl(m, "raf512")) << vramLines(m);
    EXPECT_TRUE(hat(ausgabe(m, "raf512"), kUndef)) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir p:")) << vramLines(m);
    EXPECT_FALSE(hat(ausgabe(m, "dir p:"), "RADE     COM")) << "nach Netz-Ein\n" << vramLines(m);
}

/**
 * @test RafK8915.RafcpmFindetMFrei
 * @brief §3.3: ist M: am K8915 schon belegt?  Unter SCPX 8915 V5.3 (Disketten-K8915)
 *        **nein** — `RAFCPM` meldet nicht „Laufwerk M: ist schon installiert!", sondern
 *        legt M: an, und M: ist benutzbar.  Belegt ist M: nur am K8915 mit Festplatte
 *        („K8915 (neu)", Kopf von `RAF2X24O.MAC`), für den die P:/O:-Fassungen entstanden.
 */
TEST_F(RafK8915, RafcpmFindetMFrei) {
    Aufbau x("k1520_raf_k8915_m.hfe", true);
    K8915Machine& m = x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    ASSERT_TRUE(befehl(m, "rafcpm")) << vramLines(m);
    const std::string a = ausgabe(m, "rafcpm");
    EXPECT_FALSE(hat(a, "Laufwerk M: ist schon installiert!")) << vramLines(m);
    EXPECT_TRUE(hat(a, kUndef)) << vramLines(m);
    EXPECT_TRUE(hat(a, "RAF als Laufwerk M: installiert")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "pip m:=a:stat.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir m:")) << vramLines(m);
    EXPECT_TRUE(hat(ausgabe(m, "dir m:"), "M: STAT     COM")) << vramLines(m);
}

/**
 * @test RafK8915.Raf512OhneKarte
 * @brief Ohne gesteckte RAF: „Keine RAF-Karte vorhanden!", zurück am Prompt.
 */
TEST_F(RafK8915, Raf512OhneKarte) {
    Aufbau x("k1520_raf_k8915_ohne.hfe", false);
    K8915Machine& m = x.m;
    ASSERT_NO_FATAL_FAILURE(kaltstart(m));
    ASSERT_TRUE(befehl(m, "raf512")) << vramLines(m);
    const std::string a = ausgabe(m, "raf512");
    EXPECT_TRUE(hat(a, kKeine)) << vramLines(m);
    EXPECT_FALSE(hat(a, "installiert")) << vramLines(m);
}
