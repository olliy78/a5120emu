/**
 * @file test_k8915_scpx.cpp
 * @brief K8915 Etappe 3 (doc/design/16_k8915.md §8a AP-E3): SCPX 8915 V5.3 startet von
 *        der eigenen Bootdiskette bis zum Prompt und arbeitet mit ihr.
 *
 * Die ganze Kette auf der K8915Machine: Boot-ROM (Selbsttest, Coldstart-Meldung, `CR`)
 * → Lader (liest die Systemspuren über die K5122 im `/WAIT`-Betrieb nach C000–EFEFH,
 * springt nach D600H) → BIOS-Kaltstart (Tastatur in den DCP-Modus, Marken-ISR für die
 * Diskette) → CCP mit dem vorbelegten Tastaturpuffer `rade` → RAM-Disk E: in Bank 2 →
 * Prompt.  Danach Kommandos über die Tastatur (K7672, Scancodes Satz 1).
 *
 * Die Bootdiskette ist `tests/fixtures/disks/k8915scpx_boot1.hfe` (Diskette 901 des
 * Anwenders, BIOS-Fassung „V24 (XON/XOFF)“, `cpa800`); AP-B2 fügt die Disketten 900
 * (Fassung „55 K“) und 904 (langer Autostart) hinzu — immer als TempDisk, der Emulator
 * schreibt zurück.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"

using k1520test::TempDisk;
using k1520test::vramLines;
using k1520test::vramText;

namespace {

constexpr int kSchritt = 100'000;

bool enthaelt(K8915Machine& m, const std::string& s) {
    return vramText(m).find(s) != std::string::npos;
}

/// Läuft, bis @p text im Bild steht; false nach @p frist Takten.
bool bis(K8915Machine& m, const std::string& text, long long frist) {
    for (long long t = 0; t < frist; t += m.run(kSchritt))
        if (enthaelt(m, text)) return true;
    return enthaelt(m, text);
}

/// Letzte nicht leere Bildzeile, rechts ohne Leerzeichen.
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

/// Kommando über die Tastatur tippen und warten, bis es abgearbeitet ist: alles
/// getippt (K7672 leer), der Tastaturpuffer des BIOS (F150H) leer und wieder „A>“ als
/// letzte Zeile — nachdem sie zwischendurch eine andere war (das Echo).
bool befehl(K8915Machine& m, const std::string& cmd, long long frist = 60'000'000) {
    for (char c : cmd) m.keyboard().sendeZeichen(static_cast<uint8_t>(c));
    m.keyboard().sendeZeichen(0x0D);
    bool echo = false;
    for (long long t = 0; t < frist; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    }
    return false;
}

/// Maschine mit einer Bootdiskette in A: (Vorgabe: Diskette 901, Fassung V24 XON/XOFF).
struct Aufbau {
    TempDisk     a;
    K8915Machine m;
    explicit Aufbau(const char* fixture = "k8915scpx_boot1.hfe") : a(fixture) {
        EXPECT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    }
};

/// Nach dem Coldstart-Bild `CR` und bis zum Prompt hinter `rade`.
void ladenBisPrompt(K8915Machine& m) {
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "size: 1 kByte groups 0 ... 03FH", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 20'000'000 && letzteZeile(m) != "A>"; t += m.run(kSchritt)) {}
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);
}

}  // namespace

/**
 * @test K8915Scpx.KaltstartVomNetzEinBisZumPrompt
 * @brief Das Fertig-Kriterium von Etappe 3: vom Netz-Ein über Selbsttest und
 *        Coldstart-Meldung lädt der ROM-Lader SCPX 8915 V5.3 von der Diskette, das BIOS
 *        meldet sich, der Autostart `rade` richtet die RAM-Disk E: (64 × 1 KB in Bank 2)
 *        ein, und der CCP steht am Prompt.  Die Tastatur läuft im DCP-Modus, die ZRE im
 *        Betrieb (A8H = 87H), die Anzeige meldet „bereit“ (61H = B0H).
 */
TEST(K8915Scpx, KaltstartVomNetzEinBisZumPrompt)
{
    Aufbau x;
    K8915Machine& m = x.m;
    m.powerOn();
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 60'000'000)) << vramLines(m);
    EXPECT_EQ(m.keyboard().modus(), K7672::Modus::Scp);

    ladenBisPrompt(m);
    EXPECT_TRUE(enthaelt(m, "Konfigurierbare Datenstation   K 8915")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "SCPX 8915  V 5.3  Anpassung:  V24  (XON/XOFF)"));
    EXPECT_TRUE(enthaelt(m, "A>rade")) << "Autostart aus dem Tastaturpuffer";
    EXPECT_TRUE(enthaelt(m, "* RAM-device for SCPX 8915, version 1.5 *"));
    EXPECT_TRUE(enthaelt(m, "E: containing no files"));
    EXPECT_TRUE(enthaelt(m, "running RAM-test: ****")) << "vier Viertel von Bank 2";
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
    EXPECT_EQ(m.keyboard().modus(), K7672::Modus::Dcp) << "BIOS schaltet ESC [?22h";
    EXPECT_EQ(m.zre().reg(), 0x87);
    EXPECT_EQ(m.ats().anzeige(), 0xB0);
}

/**
 * @test K8915Scpx.DirSaveEraWarmstartUndRamDisk
 * @brief Am Prompt: `dir` listet die Bootdiskette; `save 2 test.com` schreibt über den
 *        Schreibpfad des BIOS (EA94H, `/WE` + `OUT (14H)` mit `/WAIT`) auf A: — das
 *        k1520DiskTool findet die Datei danach in der Abbilddatei; `era` löscht sie
 *        wieder; Strg+C = Warmstart (das BIOS ruft den ROM-Lader bei 0406H, der CCP+BDOS
 *        nachlädt); `save 1 e:x.com` schreibt auf die RAM-Disk in Bank 2.
 *
 * Zeitsparend über den Weg nach einem Reset: steht bei 0000H/0005H ein `JP`, geht das
 * ROM ohne Selbsttest zur Coldstart-Meldung (vom Netz-Ein: Fall oben).
 */
TEST(K8915Scpx, DirSaveEraWarmstartUndRamDisk)
{
    Aufbau x;
    K8915Machine& m = x.m;
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
    ladenBisPrompt(m);

    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    for (const char* n : {"A: RADE     COM", "DISGEN   COM", "FORMAT   COM", "TPINSCPA COM",
                          "***901   VOL"})
        EXPECT_TRUE(enthaelt(m, n)) << n << "\n" << vramLines(m);

    ASSERT_TRUE(befehl(m, "save 2 test.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "TEST     COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);

    // Gegenprobe außerhalb des Emulators: die Datei steht in der Abbilddatei.
    ASSERT_TRUE(m.flushDisks());
    {
        std::string f, err;
        const FormatCatalog formate = FormatCatalog::loadDefault(&f);
        const FsCatalog     fs      = FsCatalog::loadDefault(formate, &f);
        auto vol = DiskVolume::open(x.a.path(), "", formate, fs, err);
        ASSERT_TRUE(vol) << err;
        bool da = false;
        for (const auto& e : vol->list())
            if (e.name == "TEST.COM") { da = true; EXPECT_EQ(e.size, 512u); }
        EXPECT_TRUE(da) << "TEST.COM fehlt in der Abbilddatei";
    }

    ASSERT_TRUE(befehl(m, "era test.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    const std::string nach_era = vramText(m).substr(vramText(m).rfind("A>dir"));
    EXPECT_EQ(nach_era.find("TEST     COM"), std::string::npos) << vramLines(m);

    // Strg+C: Warmstart über den ROM-Lader (A8H = 06H, CALL 0406H), zurück am Prompt.
    m.keyboard().sendeZeichen(0x03);
    for (long long t = 0; t < 30'000'000; t += m.run(kSchritt)) {
        if (enthaelt(m, "A>^C") && letzteZeile(m) == "A>") break;
    }
    EXPECT_EQ(letzteZeile(m), "A>") << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << "nach dem Warmstart\n" << vramLines(m);

    ASSERT_TRUE(befehl(m, "save 1 e:x.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir e:")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "E: X        COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/**
 * @test K8915Scpx.TastenAusZweitemFadenKommenVollstaendigUndInFolgeAn
 * @brief AP-E4b: `keyPress`/`keyRelease` der Maschine sind der Weg der Oberfläche und
 *        werden aus DEREN Faden gerufen, während der Lauffaden `run()` fährt.  Sie
 *        landen nur unter Sperre in einer Warteschlange; die K7672 bekommt sie erst im
 *        Lauffaden (vorher ein Wettlauf mit `kbd_.service()`).  Geprüft am Echo des
 *        CCP: jede Taste genau einmal, in der getippten Reihenfolge — über viele
 *        run()-Abschnitte verteilt (der Tippfaden schläft zwischen den Tasten).
 *        Ohne Datenwettlauf auch unter TSan (einziger geteilter Zustand: die Schlange).
 */
TEST(K8915Scpx, TastenAusZweitemFadenKommenVollstaendigUndInFolgeAn)
{
    Aufbau x;
    K8915Machine& m = x.m;
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
    ladenBisPrompt(m);

    const std::string text = "dir qwertz12.abc";
    constexpr uint32_t QK_RETURN = 0x01000004;
    std::atomic<bool> fertig{false};
    std::thread tipper([&] {
        for (char c : text) {
            m.keyPress(static_cast<uint8_t>(c), false, false);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            m.keyRelease(static_cast<uint8_t>(c));
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        m.keyPress(QK_RETURN, false, false);
        m.keyRelease(QK_RETURN);
        fertig.store(true);
    });
    // Lauffaden: in kleinen Abschnitten, damit sich Tippen und run() verschränken.
    for (long long t = 0; t < 400'000'000 && !fertig.load(); t += m.run(20'000)) {}
    tipper.join();
    ASSERT_TRUE(fertig.load());

    bool echo = false;
    for (long long t = 0; t < 60'000'000; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0) break;
    }
    EXPECT_TRUE(enthaelt(m, "A>" + text)) << vramLines(m);
    EXPECT_EQ(letzteZeile(m), "A>") << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/// Kurzer Weg zur Coldstart-Meldung (s. DirSaveEraWarmstartUndRamDisk): `JP` bei
/// 0000H/0005H im RAM ⇒ das ROM überspringt den Selbsttest wie nach einem Reset.
void ohneSelbsttestZurColdstartMeldung(K8915Machine& m) {
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
}

/**
 * @test K8915Scpx.Fassung55KVonDiskette900BisZumPrompt
 * @brief AP-B2: die Diskette 900 des Anwenders trägt die ANDERE BIOS-Fassung
 *        „55 K SCPX 8915 BIOS-Version 5.3“ (IOBYTE-Weiche in CONST/CONIN/CONOUT/LIST/
 *        PUNCH/READER, Drucker 7 Bit ungerade Parität; §4.4 in 16_k8915.md).  Sie startet
 *        auf demselben Kern unverändert bis zum Prompt, `rade` richtet E: ein, `dir` listet
 *        die Diskette.  Die IOBYTE-Weiche ist dabei im Spiel: jedes Zeichen am Bildschirm
 *        geht über CONOUT DCF2H mit IOBYTE = 95H (CON: = CRT).
 */
TEST(K8915Scpx, Fassung55KVonDiskette900BisZumPrompt)
{
    Aufbau x("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    K8915Machine& m = x.m;
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);
    EXPECT_TRUE(enthaelt(m, "Konfigurierbare Datenstation   K 8915")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "55 K   SCPX 8915   BIOS-Version 5.3")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "Anpassung"));
    EXPECT_TRUE(enthaelt(m, "* RAM-device for SCPX 8915, version 1.5 *"));
    EXPECT_EQ(m.memReadDebug(0x0003), 0x95) << "IOBYTE: CON: = CRT, LST: = LPT";
    EXPECT_EQ(m.ats().anzeige(), 0xB0);

    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    for (const char* n : {"SOFTKEY  COM", "PIP      COM", "XSUB     COM", "***900   VOL"})
        EXPECT_TRUE(enthaelt(m, n)) << n << "\n" << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/**
 * @test K8915Scpx.Grundsoftware904AutostartLaeuftInsLeere
 * @brief AP-B2: die Diskette 904 („Grundsoftware“) trägt dieselbe BIOS-Fassung wie 901,
 *        aber per DISGEN einen längeren Autostart: 33 Zeichen
 *        `rade CR ␠dbase CR ␠CR ␠use lohn CR ␠do lohn CR` im Tastaturpuffer (D641H).
 *        Auf der Diskette fehlen **RADE.COM und DBASE.COM** (dBASE-Befehle `use`/`do`
 *        wären erst in dBASE sinnvoll) — der CCP antwortet der Reihe nach mit `RADE?`,
 *        `DBASE?`, einer leeren Zeile, `USE?`, `DO?` und steht dann am Prompt, ohne
 *        RAM-Disk.  Beobachtetes Verhalten, festgehalten, nicht angepasst: die Diskette
 *        ist offenbar eine Kopie eines anders bestückten Arbeitssystems (REDABAS statt
 *        dBASE).
 */
TEST(K8915Scpx, Grundsoftware904AutostartLaeuftInsLeere)
{
    Aufbau x("k8915scpx_cpa800_k5601_v24xonxoff-autodbase-disk904.hfe");
    K8915Machine& m = x.m;
    ohneSelbsttestZurColdstartMeldung(m);
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "A> do lohn", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 20'000'000 && !(letzteZeile(m) == "A>" && m.memReadDebug(0xF150) == 0);
         t += m.run(kSchritt)) {}
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);
    EXPECT_EQ(m.memReadDebug(0xF150), 0) << "Tastaturpuffer leer";

    EXPECT_TRUE(enthaelt(m, "SCPX 8915  V 5.3  Anpassung:  V24  (XON/XOFF)")) << vramLines(m);
    const std::string t = vramText(m);
    const char* folge[] = {"A>rade", "RADE?", "A> dbase", "DBASE?", "A> use lohn", "USE?",
                           "A> do lohn", "DO?"};
    size_t pos = 0;
    for (const char* f : folge) {
        const size_t p = t.find(f, pos);
        ASSERT_NE(p, std::string::npos) << f << "\n" << vramLines(m);
        pos = p + 1;
    }
    EXPECT_FALSE(enthaelt(m, "RAM-device")) << "RADE.COM fehlt auf 904";
    EXPECT_EQ(m.ats().anzeige(), 0xB0);
}
