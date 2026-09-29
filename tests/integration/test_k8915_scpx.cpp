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
#include <fstream>
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

/// Freie TPA-Adresse für die kleine Druckroutine unten (weit weg von CCP/BDOS/BIOS,
/// weit weg vom eigenen Stapel).
constexpr uint16_t kDruckCode  = 0x2000;
constexpr uint16_t kDruckStack = 0x2F00;

/**
 * @brief @p bytes über den stabilen BIOS-Sprungtabelleneintrag `JLIST` (D60FH) ausgeben
 *        lassen — dieselbe Adresse in BEIDEN BIOS-Fassungen (901: `JP DE99H`, 900:
 *        `JP DEABH`; §4.4/AP-B2), nur das Sprungziel dahinter unterscheidet sich.
 *
 * Pokt eine kleine Maschinencode-Schleife nach @ref kDruckCode (Bytezeichen bis 00H),
 * die je Zeichen `LD C,<Byte>` + `CALL 0D60FH` macht und danach in sich selbst springt;
 * setzt PC/SP der CPU direkt darauf. Kapert damit die laufende Maschine zwischen zwei
 * Tastatureingaben am Prompt — unabhängig vom CCP-Zustand, denn `LIST` selbst prüft nur
 * die SIO, nicht den Aufrufer. `HL` wird um den Aufruf herum gerettet (`PUSH`/`POP`) —
 * die Fassung „55 K“ benutzt `HL` selbst (F1B8H-Zähler, IOBYTE-Weiche) und liefert es
 * anders als die Fassung „V24 XON/XOFF“ nicht unangetastet zurück.
 */
void druckeUeberBios(K8915Machine& m, const std::vector<uint8_t>& bytes) {
    const uint16_t msg = kDruckCode + 0x13;
    const uint8_t code[] = {
        0x21, static_cast<uint8_t>(msg & 0xFF), static_cast<uint8_t>(msg >> 8),  // LD HL,msg
        0x7E,                                                                    // L: LD A,(HL)
        0xB7,                                                                    // OR A
        0x28, 0x09,                                                              // JR Z,E
        0x4F,                                                                    // LD C,A
        0xE5,                                                                    // PUSH HL
        0xCD, 0x0F, 0xD6,                                                        // CALL D60FH
        0xE1,                                                                    // POP HL
        0x23,                                                                    // INC HL
        0x18, 0xF3,                                                              // JR L
        0xC3, 0x10, 0x20,                                                        // E: JP E
    };
    static_assert(sizeof(code) == 0x13, "Codegroesse <-> Nachrichtadresse (msg)");
    uint16_t a = kDruckCode;
    for (uint8_t b : code) m.memWriteDebug(a++, b);
    for (uint8_t b : bytes) m.memWriteDebug(a++, b);
    m.memWriteDebug(a, 0x00);   // Endemarke der Zeichenkette

    m.zre().cpu().PC = kDruckCode;
    m.zre().cpu().SP = kDruckStack;
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

/**
 * @test K8915Scpx.BootetUndSchreibtVonEinemImgAbbild
 * @brief AP-E4f: seit das `/WAIT`-Lesen die Spur „so, wie sie liegt“ liefert, bekommt eine
 *        Diskette OHNE eigene Aufzeichnung (`.img`: Spuren aus logischen Sektoren) die
 *        Normlücken von FORMAT.COM.  Mit den knappen Lücken von `gapsFor()` (Lücke 2 =
 *        11 × 4E) fand das BIOS kein Datenfeld mehr („SCPX ERR ON A: BAD SECTOR“).
 *        Das Abbild: die Systemspuren der Diskette 900, sonst E5H — Kaltstart bis zum
 *        Prompt, `save`/`dir` auf A:.
 */
TEST(K8915Scpx, BootetUndSchreibtVonEinemImgAbbild)
{
    std::vector<uint8_t> system;
    {
        TempDisk q("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
        std::string f, err;
        const FormatCatalog formate = FormatCatalog::loadDefault(&f);
        const FsCatalog     fs      = FsCatalog::loadDefault(formate, &f);
        auto vol = DiskVolume::open(q.path(), "", formate, fs, err);
        ASSERT_TRUE(vol) << err;
        ASSERT_TRUE(vol->readBootImage(system)) << vol->lastError();
        ASSERT_EQ(system.size(), 20480u) << "Zylinder 0–1, 5 × 1024 beidseitig";
    }
    TempDisk img = TempDisk::empty("k8915_system900.img");
    {
        std::vector<uint8_t> abbild = system;
        abbild.resize(80u * 2u * 5u * 1024u, 0xE5);
        std::ofstream o(img.path(), std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(abbild.data()),
                static_cast<std::streamsize>(abbild.size()));
    }
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, img.path(), "cpa800", false)) << m.lastError();
    ohneSelbsttestZurColdstartMeldung(m);
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "RADE?", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 20'000'000 && letzteZeile(m) != "A>"; t += m.run(kSchritt)) {}
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "55 K   SCPX 8915   BIOS-Version 5.3")) << vramLines(m);

    ASSERT_TRUE(befehl(m, "save 2 x.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "A: X        COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/**
 * @test K8915Scpx.ListGibtUeberV24AusUndHaeltBeiXoff
 * @brief AP-E4c (Fassung V24 XON/XOFF, Diskette 901): die BIOS-`LIST`-Routine (D60FH →
 *        DE99H) gibt Zeichen über SIO1-B (V.24) aus; `LISTST` (DEA2H) liest dafür das
 *        zuletzt empfangene Byte aus 42H OHNE RR0-Prüfung (Befund AP-E2). Ein vom Host
 *        gesendetes XOFF (13H) hält den Druck deshalb an — auch nachdem der
 *        Empfangs-FIFO wieder leer ist, was ohne die `Z80SIO`-Korrektur dieses AP
 *        (leerer Empfänger liefert FFH statt des zuletzt empfangenen Bytes) das XOFF
 *        nach dem ersten Lesen verloren hätte. XON (11H) gibt frei. 8 Bit, keine
 *        Parität (WR5 68H) — der Abnehmer bekommt das volle Byte.
 */
TEST(K8915Scpx, ListGibtUeberV24AusUndHaeltBeiXoff)
{
    Aufbau x;
    K8915Machine& m = x.m;
    std::vector<uint8_t> empfangen;
    m.setPrinterCallback([&](uint8_t b) { empfangen.push_back(b); });
    m.powerOn();
    m.zre().bankPoke(0, 0x0000, 0xC3);
    m.zre().bankPoke(0, 0x0005, 0xC3);
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 5'000'000)) << vramLines(m);
    ladenBisPrompt(m);

    // Leitung anhalten, BEVOR gedruckt wird: LISTST liest 13H zurück, LIST bleibt in
    // seinem eigenen Wartezweig (JR Z,DE99H) hängen -- kein Byte geht hinaus, obwohl
    // die CPU weiterläuft.
    m.printerSend(0x13);   // XOFF
    const std::vector<uint8_t> text = {'T', 'E', 'S', 'T', '-', '4', 'C', 0xC1};
    druckeUeberBios(m, text);
    for (long long t = 0; t < 2'000'000; t += m.run(kSchritt)) {}
    EXPECT_TRUE(empfangen.empty())
        << "haelt bei XOFF an, " << empfangen.size() << " Byte durchgekommen";

    m.printerSend(0x11);   // XON
    for (long long t = 0; t < 500'000 && empfangen.size() < text.size(); t += m.run(kSchritt)) {}
    ASSERT_EQ(empfangen.size(), text.size()) << "nach XON muss der Rest ankommen";
    EXPECT_EQ(empfangen, text) << "8 Bit, keine Parität: das volle Byte kommt an";
}

/**
 * @test K8915Scpx.ListGibtUeberV24AusUndHaeltBeiXoffFassung900
 * @brief AP-E4c (Fassung „55 K“, Diskette 900): eigener Druckertreiber — 7 Bit,
 *        ungerade Parität (WR5 28H), `DEL` (7FH) unaufgefordert beim Kaltstart
 *        (`DE82H`, unabhängig von XON/XOFF — vor jeder eigenen Ausgabe), `LISTST`
 *        (DEE4H) mit umgekehrter Polarität, ebenfalls ohne RR0-Prüfung.
 *
 *        Solange der Drucker seit dem Kaltstart nie ein XON gesendet hat (Reset-
 *        Ruhewert von `Z80SIO::Channel::last_rx`, AP-E4c: 00H — eine Annahme, das
 *        Datenblatt macht dazu keine Aussage), schiebt `LIST` (DEABH) vor dem ERSTEN
 *        eigenen Zeichen ein LF+CR ein (F1B8H = 0, „zuletzt kein XON"; §4.4 [?], Zweck
 *        laut Listing unklar) — hier beobachtet und festgehalten, nicht weggetestet.
 *        Danach hält ein XOFF unabhängig davon an, ein XON gibt frei; der Abnehmer
 *        bekommt nur die unteren 7 Bit.
 */
TEST(K8915Scpx, ListGibtUeberV24AusUndHaeltBeiXoffFassung900)
{
    TempDisk     a("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, a.path(), "cpa800", false)) << m.lastError();
    std::vector<uint8_t> empfangen;
    m.setPrinterCallback([&](uint8_t b) { empfangen.push_back(b); });
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);

    ASSERT_FALSE(empfangen.empty()) << "DEL beim Kaltstart fehlt";
    EXPECT_EQ(empfangen.front(), 0x7F) << "DE82H sendet DEL direkt an den Drucker";
    const size_t nachDel = empfangen.size();

    const std::vector<uint8_t> text = {'K', 'K', static_cast<uint8_t>('K' | 0x80)};
    druckeUeberBios(m, text);
    for (long long t = 0; t < 1'000'000 && empfangen.size() < nachDel + 2 + text.size();
         t += m.run(kSchritt)) {}
    ASSERT_GE(empfangen.size(), nachDel + 2) << vramLines(m);
    EXPECT_EQ(empfangen[nachDel], 0x0A) << "eingeschobenes LF (direkt an 42H)";
    EXPECT_EQ(empfangen[nachDel + 1], 0x0D) << "eingeschobenes CR (über LIST)";
    ASSERT_EQ(empfangen.size(), nachDel + 2 + text.size()) << vramLines(m);
    for (size_t i = 0; i < text.size(); ++i)
        EXPECT_EQ(empfangen[nachDel + 2 + i], text[i] & 0x7F) << "7 Bit, i=" << i;

    // Ab hier steht 42H auf einem "richtigen" Wert (kein XOFF) -- der LF/CR-Sonderfall
    // ist unabhängig davon pruefbar: XOFF haelt an, XON gibt den Rest frei.
    const size_t vorXoff = empfangen.size();
    m.printerSend(0x13);   // XOFF
    druckeUeberBios(m, text);
    for (long long t = 0; t < 2'000'000; t += m.run(kSchritt)) {}
    EXPECT_EQ(empfangen.size(), vorXoff) << "haelt bei XOFF an";

    m.printerSend(0x11);   // XON
    for (long long t = 0; t < 500'000 && empfangen.size() < vorXoff + text.size();
         t += m.run(kSchritt)) {}
    ASSERT_EQ(empfangen.size(), vorXoff + text.size());
    for (size_t i = 0; i < text.size(); ++i)
        EXPECT_EQ(empfangen[vorXoff + i], text[i] & 0x7F);
}

// ─────────────────────────────────────────────────────────────────────────────
// AP-E5a: DiskTool ↔ K8915 auf einer Diskette OHNE Systemspuren (doc/design/16_k8915.md
// §8a).  Das BIOS hat einen festen DPB (OFF 2, DRM 127) — das DiskTool muss dieselben
// Plätze benutzen, sonst sieht die eine Seite die Dateien der anderen nicht oder
// überschreibt sie.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct DtKataloge {
    FormatCatalog formate;
    FsCatalog     fs;
    DtKataloge() {
        std::string f;
        formate = FormatCatalog::loadDefault(&f);
        fs      = FsCatalog::loadDefault(formate, &f);
    }
};

/// Strg+C am Prompt: Warmstart, das BDOS meldet die Laufwerke neu an (nach Diskettenwechsel).
void warmstart(K8915Machine& m) {
    m.keyboard().sendeZeichen(0x03);
    for (long long t = 0; t < 30'000'000; t += m.run(kSchritt))
        if (enthaelt(m, "A>^C") && letzteZeile(m) == "A>" && m.memReadDebug(0xF150) == 0) break;
}

/// Datei @p name mit @p inhalt per DiskTool auf @p pfad legen (@p fs: "" = erkennen).
void dtPut(const std::string& pfad, const std::string& fs, const std::string& name,
           const std::string& inhalt) {
    DtKataloge k;
    TempDisk q = TempDisk::empty("k8915_dt_quelle.txt");
    { std::ofstream(q.path(), std::ios::binary) << inhalt; }
    std::string err;
    auto vol = DiskVolume::open(pfad, fs, k.formate, k.fs, err, /*read_only=*/false);
    ASSERT_TRUE(vol) << err;
    TransferOptions o;
    o.text = true;
    ASSERT_TRUE(vol->insert(q.path(), FileRef::parse(name), o)) << vol->lastError();
    ASSERT_TRUE(vol->flush()) << vol->lastError();
}

/**
 * Die Rundreise auf einer leeren Diskette in B: — erst schreibt der K8915, dann das
 * DiskTool (ohne `--fs`), dann liest der K8915 beides.
 */
void rundreise(const char* bootdiskette, const char* format, const char* temp) {
    Aufbau x(bootdiskette);
    K8915Machine& m = x.m;
    TempDisk b = TempDisk::empty(temp);
    ASSERT_TRUE(m.createDisk(1, b.path(), format, false)) << m.lastError();
    ohneSelbsttestZurColdstartMeldung(m);
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "A>", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 60'000'000 && !(letzteZeile(m) == "A>" && m.memReadDebug(0xF150) == 0);
         t += m.run(kSchritt)) {}

    ASSERT_TRUE(befehl(m, "save 2 b:vomk8915.com")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
    ASSERT_TRUE(m.flushDisks());
    ASSERT_TRUE(m.unmountDisk(1));

    {
        DtKataloge k;
        std::string err;
        auto vol = DiskVolume::open(b.path(), "", k.formate, k.fs, err);
        ASSERT_TRUE(vol) << err;
        EXPECT_EQ(vol->profile().data_cyl, 2) << vol->detection().filesystem;
        EXPECT_EQ(vol->profile().dir_entries, 128) << vol->detection().filesystem;
        ASSERT_EQ(vol->list().size(), 1u) << vol->detection().remarks;
        EXPECT_EQ(vol->list().front().name, "VOMK8915.COM");
        EXPECT_EQ(vol->list().front().size, 512u);
        EXPECT_TRUE(vol->check(FsCheckLevel::Voll, true).ohneBefund());
    }
    ASSERT_NO_FATAL_FAILURE(dtPut(b.path(), "", "VOMDT.TXT", "VOM DISKTOOL GESCHRIEBEN\n"));

    ASSERT_TRUE(m.mountDisk(1, b.path(), format, false)) << m.lastError();
    warmstart(m);
    ASSERT_TRUE(befehl(m, "dir b:")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOMK8915 COM")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOMDT    TXT")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "type b:vomdt.txt")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOM DISKTOOL GESCHRIEBEN")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

}  // namespace

/**
 * @test K8915Scpx.DiskToolRundreiseFuenfMal1024OhneSystemspuren
 * @brief Diskette 900 (B: = 5 × 1024 wie A:), leere Diskette in B:.  Vor AP-E5a hielt
 *        das DiskTool sie nach dem `save` des K8915 für eine LEERE CP/A-Datendiskette
 *        (`cpa800`, Verzeichnis ab c0h0) — jetzt gilt die CP/A-Regel (OFF 2, 128 Plätze),
 *        und die Datei des DiskTools erscheint am K8915.
 */
TEST(K8915Scpx, DiskToolRundreiseFuenfMal1024OhneSystemspuren)
{
    rundreise("k8915scpx_cpa800_k5601_bios55k-disk900.hfe", "cpa800", "k8915_dt_b1024.hfe");
}

/**
 * @test K8915Scpx.DiskToolRundreiseSechzehnMal256
 * @brief Diskette 901: B: ist per DISGEN 16 × 256 (SPT 64, DSM 311, DRM 127, OFF 2) —
 *        die Regel hat dort ein festes Offset, die Rundreise geht ohne Katalogeintrag.
 */
TEST(K8915Scpx, DiskToolRundreiseSechzehnMal256)
{
    rundreise("k8915scpx_boot1.hfe", "k5601_16x256", "k8915_dt_b256.hfe");
}

/**
 * @test K8915Scpx.DiskToolFuelltLeereDisketteNurMitFsScpx8915
 * @brief Die eine Mehrdeutigkeit, die das Medium nicht auflöst: eine LEERE 5 × 1024-
 *        Diskette.  Ohne `--fs` wird sie, was CP/A daraus macht (`cpa800`, Verzeichnis ab
 *        c0h0) — der K8915 sieht die Datei dann NICHT (sein Verzeichnis liegt ab c2h0).
 *        Mit `--fs scpx8915` liegt sie dort, wo das BIOS sucht.  Festgehalten, weil genau
 *        dieser Unterschied der Grund für den Katalogeintrag ist.
 */
TEST(K8915Scpx, DiskToolFuelltLeereDisketteNurMitFsScpx8915)
{
    TempDisk cpa = TempDisk::empty("k8915_dt_leer_cpa.hfe");
    TempDisk k89 = TempDisk::empty("k8915_dt_leer_k89.hfe");
    {
        DtKataloge k;
        std::string err;
        for (const TempDisk* d : {&cpa, &k89})
            ASSERT_TRUE(DiskVolume::create(d->path(), "cpa800", "", k.formate, k.fs, err)) << err;
    }
    ASSERT_NO_FATAL_FAILURE(dtPut(cpa.path(), "", "CPA.TXT", "NUR FUER CP/A\n"));
    ASSERT_NO_FATAL_FAILURE(dtPut(k89.path(), "scpx8915", "K89.TXT", "FUER DEN K8915\n"));

    Aufbau x("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    K8915Machine& m = x.m;
    ASSERT_TRUE(m.mountDisk(1, cpa.path(), "cpa800", false)) << m.lastError();
    ohneSelbsttestZurColdstartMeldung(m);
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "A>", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 60'000'000 && !(letzteZeile(m) == "A>" && m.memReadDebug(0xF150) == 0);
         t += m.run(kSchritt)) {}
    ASSERT_TRUE(befehl(m, "dir b:")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "NO FILE")) << "CP/A-Verzeichnis ab c0h0 sieht der K8915 nicht\n"
                                        << vramLines(m);

    ASSERT_TRUE(m.unmountDisk(1));
    ASSERT_TRUE(m.mountDisk(1, k89.path(), "cpa800", false)) << m.lastError();
    warmstart(m);
    ASSERT_TRUE(befehl(m, "type b:k89.txt")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "FUER DEN K8915")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}
