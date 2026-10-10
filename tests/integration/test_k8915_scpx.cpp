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
#include <cstdio>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/k8915/k8915.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tests/support/fixtures.h"
#include "tests/support/screen.h"
#include "tests/support/temp_path.h"

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

void ohneSelbsttestZurColdstartMeldung(K8915Machine& m);

namespace {

/// Tasten über den Weg der Oberfläche (`keyPress`/`keyRelease` mit Qt-Code bzw.
/// Matrixposition) tippen, danach bis zum Prompt hinter der Ausgabe laufen.
bool tippeUeberOberflaeche(K8915Machine& m, const std::vector<uint32_t>& codes) {
    for (uint32_t k : codes) {
        m.keyPress(k, false, false);
        m.keyRelease(k);
    }
    bool echo = false;
    for (long long t = 0; t < 60'000'000; t += m.run(kSchritt)) {
        const bool prompt = letzteZeile(m) == "A>";
        echo = echo || !prompt;
        if (echo && prompt && !m.keyboard().sendetNoch() && m.memReadDebug(0xF150) == 0)
            return true;
    }
    return false;
}

constexpr uint32_t QK_BACKSPACE = 0x01000003, QK_RETURN_ = 0x01000004;

}  // namespace

/**
 * @test K8915Scpx.RuecktasteKorrigiertDieEingabezeile
 * @brief AP-E4m (Anwenderbefund: „Rücktaste geht nicht“): `dirx`, PC-Rücktaste, RETURN
 *        über den Tastenweg der Oberfläche.  Die Rücktaste trifft Kursor ← (2AH 4BH),
 *        das BIOS liefert 08H (DCC2H), das BDOS (Funktion 10, CA03H) löscht das `x`
 *        aus Puffer UND Bild — die Zeile steht als `A>dir` da, `dir` läuft.
 *
 *        Gegenprobe mit der Taste |←| der Nachbildung (Matrix 67H, 0EH → 7FH, DC1CH):
 *        das BDOS nimmt das `x` aus dem Puffer, wiederholt es aber am Schirm (Rubout,
 *        CA14H) — `A>dirxx`, und trotzdem läuft `dir`.  Genau das sah der Anwender, als
 *        die PC-Rücktaste noch auf |←| lag.
 */
TEST(K8915Scpx, RuecktasteKorrigiertDieEingabezeile)
{
    Aufbau x;
    K8915Machine& m = x.m;
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);

    ASSERT_TRUE(tippeUeberOberflaeche(m, {'d', 'i', 'r', 'x', QK_BACKSPACE, QK_RETURN_}))
        << vramLines(m);
    const std::string t = vramText(m);
    size_t zeile = t.rfind("A>dir");
    ASSERT_NE(zeile, std::string::npos) << vramLines(m);
    zeile -= zeile % 80;
    std::string eingabe = t.substr(zeile, 80);
    while (!eingabe.empty() && (eingabe.back() == ' ' || eingabe.back() == '\0')) eingabe.pop_back();
    EXPECT_EQ(eingabe, "A>dir") << "das x ist vom Schirm gelöscht\n" << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "A: RADE     COM")) << "dir ausgeführt\n" << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "DIRX?")) << vramLines(m);

    // Gegenprobe: |←| der Nachbildung = Rubout mit Echo, Puffer trotzdem richtig.
    const size_t vorher = vramText(m).find("A>dirxx");
    ASSERT_EQ(vorher, std::string::npos) << vramLines(m);
    ASSERT_TRUE(tippeUeberOberflaeche(m, {'d', 'i', 'r', 'x', K7672::QK_TASTE_BASE | 0x67,
                                          QK_RETURN_}))
        << vramLines(m);
    const std::string u = vramText(m);
    const size_t p = u.find("A>dirxx");
    ASSERT_NE(p, std::string::npos) << "Rubout wiederholt das x\n" << vramLines(m);
    EXPECT_NE(u.find("A: RADE     COM", p), std::string::npos) << "dir läuft trotzdem\n"
                                                                << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "DIRX?")) << vramLines(m);
}

/**
 * @test K8915Scpx.GehalteneTasteWiederholtAmPrompt
 * @brief AP-E4g: SCPX läuft bis zum Prompt, `x` wird GEHALTEN — die K7672 wiederholt
 *        den Drücken-Code selbst (DCP-Modus, Firmware 0190H → 0320H) nach ≈ 0,9 s
 *        und dann ≈ 10 Hz [?]; das BIOS echot jedes `x`.  Loslassen beendet es, ein
 *        gehaltenes Umschalt/Strg dagegen erzeugt nie etwas.  Läuft in 5 000-Takt-
 *        Schritten durch die Maschine (K7672 = 9600 Baud + Zeitgeber-ISR) — die
 *        Wiederholung zählt also Maschinenzeit im Service-Pfad, nicht in einem Aufruf,
 *        den nur der Test macht (AP-S9 am A5120: dort wiederholte gar nichts).
 *        Ohne die Wiederholung steht hier genau ein `x` (Gegenprobe).
 */
TEST(K8915Scpx, GehalteneTasteWiederholtAmPrompt)
{
    Aufbau x;
    K8915Machine& m = x.m;
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);

    auto zaehleX = [&] {
        const std::string z = letzteZeile(m);
        return static_cast<int>(std::count(z.begin(), z.end(), 'x'));
    };
    auto fahre = [&](long long takte) {
        for (long long t = 0; t < takte; t += m.run(5'000)) {}
    };
    constexpr long long VERZ = K7672::WDH_VERZOEGERUNG_DURCHLAEUFE * K7672::ABTASTDURCHLAUF_TAKTE;
    constexpr long long FOLGE = K7672::WDH_FOLGE_DURCHLAEUFE * K7672::ABTASTDURCHLAUF_TAKTE;

    // Modifikatoren allein erzeugen nichts, so lange man sie auch hält.
    m.keyPress(0x01000020, false, false);              // Qt::Key_Shift
    m.keyPress(0x01000021, false, true);               // Qt::Key_Control
    fahre(VERZ + 4 * FOLGE);
    m.keyRelease(0x01000021);
    m.keyRelease(0x01000020);
    fahre(1'000'000);
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);

    m.keyPress('x', false, false);
    fahre(VERZ / 2);
    EXPECT_EQ(zaehleX(), 1) << "vor Ablauf der Verzögerung nur der Druck\n" << vramLines(m);
    fahre(VERZ / 2 + 4 * FOLGE);
    EXPECT_GE(zaehleX(), 4) << "gehalten: wiederholt\n" << vramLines(m);
    m.keyRelease('x');
    fahre(1'000'000);
    const int nachLoslassen = zaehleX();
    fahre(VERZ + 3 * FOLGE);
    EXPECT_EQ(zaehleX(), nachLoslassen) << "nach dem Loslassen wiederholt nichts mehr";
    EXPECT_LE(nachLoslassen, 8) << "etwa 1 + 4 Wiederholungen, nicht ein Strom\n" << vramLines(m);
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
 *        DE99H) gibt Zeichen über SIO1-B (Drucker/IFSS1, V.24-Pegel) aus; `LISTST` (DEA2H) liest dafür das
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
// K8915Seriell: die Schnittstellen der ATS über den SerialHub (Entwurf 19 §3.2, AP-S5)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string dateiLesen(const std::string& pfad) {
    std::ifstream f(std::filesystem::u8path(pfad), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void loopAlle(K8915Machine& m, bool an) {
    auto* hub = m.serialHub();
    for (int i = 0; i < hub->anzahl(); ++i) {
        auto k = hub->konfig(i);
        k.loop = an;
        ASSERT_TRUE(hub->konfigurieren(i, k));
    }
}

}  // namespace

/// Hub-Index der Druckerschnittstelle (X3 „Drucker/IFSS1" = SIO1-B, AP-S12).
constexpr int kDrucker = 0;

/**
 * @test K8915Seriell.HubUndVorgaben
 * @brief Drei einstellbare Schnittstellen in Steckerreihenfolge (Gerätebeschriftung,
 *        AP-S12) — Drucker/IFSS1 (SIO1-B, X3), V.24 (SIO1-A, X4), DFÜ/IFSS2 (SIO2-A,
 *        X5) — plus die feste Tastatur; die Maschinenvorgabe `pruefstecker` ist der
 *        Rx/Tx-Loop aller drei.
 */
TEST(K8915Seriell, HubUndVorgaben)
{
    K8915Machine m;
    auto* hub = m.serialHub();
    ASSERT_NE(hub, nullptr);
    ASSERT_EQ(hub->anzahl(), 3);
    EXPECT_EQ(hub->info(0).name, "Drucker/IFSS1");
    EXPECT_EQ(hub->info(0).stecker, "X3");
    EXPECT_FALSE(hub->info(0).v24);
    EXPECT_EQ(hub->info(1).name, "V.24");
    EXPECT_EQ(hub->info(1).stecker, "X4");
    EXPECT_TRUE(hub->info(1).v24);
    EXPECT_EQ(hub->info(2).name, "DFÜ/IFSS2");
    EXPECT_EQ(hub->info(2).stecker, "X5");
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(hub->konfig(i).loop) << i;
        EXPECT_TRUE(hub->info(i).taktquellen.empty()) << i;
    }
    ASSERT_EQ(m.serielleAnschluesse().size(), 3u);
    EXPECT_STREQ(m.serielleAnschluesse()[0]->name(), "Drucker/IFSS1");
    EXPECT_EQ(m.festeSchnittstellen(), std::vector<std::string>{"Tastatur K7672"});

    K8915Machine::Config c;
    c.pruefstecker = false;
    K8915Machine ohne(c);
    for (int i = 0; i < 3; ++i) EXPECT_FALSE(ohne.serialHub()->konfig(i).loop) << i;

    // Alter Unterbau: ein Druckerrückruf zieht den Prüfstecker von Drucker/IFSS1
    // (SIO1-B) ab, ein DFÜ-Rückruf den von DFÜ/IFSS2 (SIO2-A); ein leerer steckt ihn
    // wieder.
    m.setPrinterCallback([](uint8_t) {});
    EXPECT_FALSE(hub->konfig(kDrucker).loop);
    EXPECT_TRUE(hub->konfig(1).loop);
    EXPECT_TRUE(hub->konfig(2).loop);
    m.setPrinterCallback({});
    EXPECT_TRUE(hub->konfig(kDrucker).loop);
    m.setDFUECallback([](uint8_t) {});
    EXPECT_FALSE(hub->konfig(2).loop);
    EXPECT_TRUE(hub->konfig(1).loop);
    m.setDFUECallback({});
    EXPECT_TRUE(hub->konfig(2).loop);
}

/**
 * @test K8915Seriell.BiosLaeuftOhneLoopWeiter
 * @brief Offener Punkt 3 aus Entwurf 19: nur der ROM-Selbsttest verlangt das Echo.
 *        Nach der Coldstart-Meldung wird der Loop an allen drei Schnittstellen gezogen
 *        — Lader, BIOS-Kaltstart, `rade` und Kommandos laufen trotzdem.
 */
TEST(K8915Seriell, BiosLaeuftOhneLoopWeiter)
{
    Aufbau x;
    K8915Machine& m = x.m;
    m.powerOn();
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 60'000'000)) << vramLines(m);
    loopAlle(m, false);
    ladenBisPrompt(m);
    ASSERT_TRUE(befehl(m, "save 1 y.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, ": Y        COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

/**
 * @test K8915Seriell.ListUeberIfs1InEineDatei
 * @brief Maschinenprobe: `LIST` des BIOS (SIO1-B = Drucker/IFSS1, Index 0) mit Betriebsart Datei — die
 *        Zeichen kommen über `hub.takt` im Lauf von `run()` in der Datei an; der alte
 *        Druckerrückruf schweigt, solange der Transport anliegt.
 */
TEST(K8915Seriell, ListUeberIfs1InEineDatei)
{
    Aufbau x;
    K8915Machine& m = x.m;
    std::vector<uint8_t> alt;
    m.powerOn();
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 60'000'000)) << vramLines(m);
    ladenBisPrompt(m);

    auto* hub = m.serialHub();
    const std::string pfad = k1520test::tempPath("k1520_test_k8915_list.txt");
    std::filesystem::remove(std::filesystem::u8path(pfad));
    auto k = hub->konfig(kDrucker);
    k.loop = false;
    k.betriebsart = k1520::serial::Betriebsart::Datei;
    k.datei = pfad;
    ASSERT_TRUE(hub->konfigurieren(kDrucker, k));
    ASSERT_TRUE(hub->start(kDrucker));
    m.setPrinterCallback([&](uint8_t b) { alt.push_back(b); });   // Loop bleibt aus

    const std::string text = "K8915 LIST";
    druckeUeberBios(m, {text.begin(), text.end()});
    for (int n = 0; n < 200; ++n) {
        m.run(kSchritt);
        const auto st = hub->status(kDrucker);
        if (st.bytes_gesendet >= text.size() && st.puffer_senden == 0) break;
    }
    const auto st = hub->status(kDrucker);
    EXPECT_EQ(st.baud_nenn, 9600u) << "CTC1-K2 05H/01H, WR4 44H";
    EXPECT_TRUE(st.format_gueltig);
    hub->stop(kDrucker);
    EXPECT_EQ(dateiLesen(pfad), text);
    EXPECT_TRUE(alt.empty()) << "alter Rückruf ins Leere, solange der Transport anliegt";
    std::filesystem::remove(std::filesystem::u8path(pfad));
}

/**
 * @test K8915Seriell.ListUeberTelnetMitXonXoff
 * @brief Dieselbe Strecke über Telnet (Server auf Loopback, Port vom System): ein
 *        Client empfängt die Druckausgabe; sein XOFF kommt über den Wandler im
 *        Empfänger von SIO1-B an und hält `LIST` an, XON gibt den Rest frei.
 */
TEST(K8915Seriell, ListUeberTelnetMitXonXoff)
{
    namespace net = k1520::serial::net;
    Aufbau x;
    K8915Machine& m = x.m;
    m.powerOn();
    ASSERT_TRUE(bis(m, "* Coldstart *  Disk on A: ready", 60'000'000)) << vramLines(m);
    ladenBisPrompt(m);

    auto* hub = m.serialHub();
    auto k = hub->konfig(kDrucker);
    k.loop = false;
    k.betriebsart = k1520::serial::Betriebsart::Telnet;
    k.rolle = k1520::serial::Rolle::Server;
    k.port = 0;
    ASSERT_TRUE(hub->konfigurieren(kDrucker, k));
    ASSERT_TRUE(hub->start(kDrucker));
    const uint16_t port = hub->status(kDrucker).port_aktiv;
    ASSERT_NE(port, 0);
    net::Socket s = net::verbindenAlle(net::aufloesen("127.0.0.1", port), 2000);
    ASSERT_TRUE(s.gueltig());
    for (int n = 0; n < 100 && hub->status(kDrucker).zustand != k1520::serial::Zustand::Verbunden; ++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(hub->status(kDrucker).zustand, k1520::serial::Zustand::Verbunden);

    std::string empf;
    auto abholen = [&] {
        uint8_t buf[256];
        for (;;) {
            const auto r = net::empfangen(s.fd(), buf, sizeof buf);
            if (r.status != net::IoStatus::Ok) break;
            for (size_t i = 0; i < r.n; ++i)
                if (buf[i] >= 0x20 && buf[i] < 0x7F) empf += static_cast<char>(buf[i]);   // ohne IAC-Verhandlung
        }
    };
    auto laufen = [&](int schritte) {
        for (int n = 0; n < schritte; ++n) {
            m.run(kSchritt);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            abholen();
        }
    };

    const uint8_t xoff = 0x13, xon = 0x11;
    ASSERT_EQ(net::senden(s.fd(), &xoff, 1).status, net::IoStatus::Ok);
    laufen(20);   // XOFF liegt im Empfänger von SIO1-B
    const std::string text = "TELNET";
    druckeUeberBios(m, {text.begin(), text.end()});
    laufen(30);
    EXPECT_EQ(empf.find(text), std::string::npos) << "hält bei XOFF an: " << empf;
    ASSERT_EQ(net::senden(s.fd(), &xon, 1).status, net::IoStatus::Ok);
    for (int n = 0; n < 300 && empf.find(text) == std::string::npos; ++n) laufen(1);
    EXPECT_NE(empf.find(text), std::string::npos) << empf;
    hub->stop(kDrucker);
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

// ─── AP-E5b: Schreibnachlauf hinter der Daten-CRC vs. `.img`-Export ─────────

namespace {

/// Sektoren eines Abbilds, deren Bytes hinter der Daten-CRC NICHT reine Lückenfüller
/// (4E/FF/00) sind — je Sektor „c/h/id: hex“.
std::vector<std::string> sektorenMitNachlauf(const DiskMedium& med) {
    std::vector<std::string> out;
    for (uint8_t c = 0; c < med.numCylinders(); ++c)
        for (uint8_t h = 0; h < med.numHeads(); ++h)
            for (const LogicalSector& s : TrackCodec::parseTrack(med.track(c, h))) {
                bool gap = true;
                for (uint8_t b : s.tail) gap = gap && (b == 0x4E || b == 0xFF || b == 0x00);
                if (gap) continue;
                char buf[64];
                std::string hex;
                for (uint8_t b : s.tail) { std::snprintf(buf, sizeof buf, "%02X", b); hex += buf; }
                std::snprintf(buf, sizeof buf, "%u/%u/%u: ", c, h, s.id);
                out.push_back(buf + hex);
            }
    return out;
}

/// Spuren, auf denen JEDE CRC stimmt und die trotzdem nicht `.img`-fähig sind — dann
/// sperrt allein der Inhalt hinter der Daten-CRC.
int spurenGesperrtNurWegenNachspann(const DiskMedium& med) {
    int n = 0;
    for (uint8_t c = 0; c < med.numCylinders(); ++c)
        for (uint8_t h = 0; h < med.numHeads(); ++h) {
            const auto secs = TrackCodec::parseTrack(med.track(c, h));
            if (secs.empty()) continue;
            bool crc = true;
            for (const auto& s : secs) crc = crc && s.id_crc_ok && s.data_crc_ok;
            if (crc && !med.trackRawCompatible(c, h)) ++n;
        }
    return n;
}

}  // namespace

/**
 * @test K8915Scpx.ImgExportSchreibnachlaufJaUdosNein
 * @brief AP-E5b: die am Gerät vom SCPX-8915-BIOS und von DISGEN beschriebenen Sektoren
 *        tragen hinter der Daten-CRC ein 4E und danach die Schreibnaht (alte Lücke mit
 *        verrutschtem Byterahmen) — das sperrt den `.img`-Export nicht mehr.  Die
 *        Disketten 900 und 904 sind damit exportierbar; 901 bleibt es begründet NICHT
 *        (echter Daten-CRC-Fehler auf c48h0, §4.4).  Gegenwächter: jede UDOS-Fixture
 *        bleibt gesperrt, und zwar auf Spuren mit lauter gültigen CRCs — also allein
 *        wegen des Kontrollblocks.
 */
TEST(K8915Scpx, ImgExportSchreibnachlaufJaUdosNein)
{
    for (const char* f : {"k8915scpx_cpa800_k5601_bios55k-disk900.hfe",
                          "k8915scpx_cpa800_k5601_v24xonxoff-autodbase-disk904.hfe"}) {
        TempDisk d(f);
        auto img = DiskImage::open(d.path(), std::nullopt, true);
        ASSERT_TRUE(img) << f;
        EXPECT_GT(sektorenMitNachlauf(img->medium()).size(), 20u)
            << f << ": ohne Schreibnachlauf prüfte der Fall nichts";
        EXPECT_TRUE(img->rawCompatible()) << f << ": " << img->medium().rawIncompatibleReason();
    }
    {
        TempDisk d("k8915scpx_boot1.hfe");
        auto img = DiskImage::open(d.path(), std::nullopt, true);
        ASSERT_TRUE(img);
        EXPECT_FALSE(img->rawCompatible());
        EXPECT_EQ(img->medium().rawIncompatibleReason(), "Spur 48/0")
            << "901: gesperrt NUR wegen des CRC-Fehlers c48h0 #1";
        EXPECT_EQ(spurenGesperrtNurWegenNachspann(img->medium()), 0);
    }
    for (const char* f : {"udos_boot_scp.hfe", "udos_ds77_k5601_fremdsync.hfe",
                          "mixed_udos_ss40_over_cpa800.hfe"}) {
        TempDisk d(f);
        auto img = DiskImage::open(d.path(), std::nullopt, true);
        ASSERT_TRUE(img) << f;
        EXPECT_FALSE(img->rawCompatible()) << f;
        EXPECT_GT(spurenGesperrtNurWegenNachspann(img->medium()), 0)
            << f << ": der UDOS-Kontrollblock allein muss sperren";
    }
}

/**
 * @test K8915Scpx.VomEmulatorGeschriebenerSektorIstImgFaehig
 * @brief AP-E5b: was das BIOS im Emulator schreibt (`save`), trägt hinter der CRC
 *        `4E 4E 4E` und danach den alten Nachspann des Sektors — dieselbe Art Naht wie
 *        am Gerät (dort überlebt nur das erste 4E, der Rest fällt in die Abschaltung
 *        von /WE).  Das Abbild bleibt `.img`-fähig.
 */
TEST(K8915Scpx, VomEmulatorGeschriebenerSektorIstImgFaehig)
{
    Aufbau x("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
    K8915Machine& m = x.m;
    std::vector<std::string> vorher;
    {
        auto img = DiskImage::open(x.a.path(), std::nullopt, true);
        ASSERT_TRUE(img);
        vorher = sektorenMitNachlauf(img->medium());
    }
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);
    ASSERT_TRUE(befehl(m, "save 40 gross.com")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
    ASSERT_TRUE(m.flushDisks());

    auto img = DiskImage::open(x.a.path(), std::nullopt, true);
    ASSERT_TRUE(img);
    // Das BIOS schreibt hinter die CRC drei Lückenbytes (EAF7H–EB03H: 3 × OUT (14H),A;
    // das `LD A,00H` des Listings ist zur Laufzeit auf 4EH gepatcht) und schaltet dann
    // /WE ab.  Dahinter bleibt stehen, was vorher dort lag.
    EXPECT_EQ(m.memReadDebug(0xEAF8), 0x4E);
    int neu = 0;
    for (const auto& s : sektorenMitNachlauf(img->medium())) {
        if (std::find(vorher.begin(), vorher.end(), s) != vorher.end()) continue;
        ++neu;
        EXPECT_EQ(s.substr(s.find(": ") + 2, 6), "4E4E4E") << s;
    }
    EXPECT_GT(neu, 0) << "save hat keinen Sektor mit altem Nachlauf überschrieben";
    EXPECT_TRUE(img->rawCompatible()) << img->medium().rawIncompatibleReason();
}

/**
 * @test K8915Scpx.ImgRundreiseDerSystemdisketteBootet
 * @brief AP-E5b: die Diskette 900 (Systemspuren von DISGEN, Dateien vom BIOS — alles mit
 *        Schreibnachlauf) wird im Emulator als `.img` gespeichert; von diesem Abbild
 *        startet der K8915 bis `A>`, `dir` listet die Dateien, `save` schreibt.
 */
TEST(K8915Scpx, ImgRundreiseDerSystemdisketteBootet)
{
    TempDisk img = TempDisk::empty("k8915_rundreise900.img");
    {
        Aufbau x("k8915scpx_cpa800_k5601_bios55k-disk900.hfe");
        ASSERT_TRUE(x.m.saveDiskAs(0, img.path(), "cpa800")) << x.m.lastError();
    }
    K8915Machine m;
    ASSERT_TRUE(m.mountDisk(0, img.path(), "cpa800", false)) << m.lastError();
    ohneSelbsttestZurColdstartMeldung(m);
    ladenBisPrompt(m);
    EXPECT_TRUE(enthaelt(m, "55 K   SCPX 8915   BIOS-Version 5.3")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    for (const char* n : {"DISGEN   COM", "FORMAT   COM", "***900   VOL"})
        EXPECT_TRUE(enthaelt(m, n)) << n << "\n" << vramLines(m);
    ASSERT_TRUE(befehl(m, "save 2 x.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir x.com")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "A: X        COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

// ─── AP-E5c: bootfähige K8915-Disketten mit dem DiskTool ─────────────────────

namespace {

const char* const kSys900 = "k8915scpx_cpa800_k5601_bios55k-disk900.hfe";   // Fassung „55 K“
const char* const kSys901 = "k8915scpx_boot1.hfe";                          // Fassung „V24“
const char* const kBanner900 = "55 K   SCPX 8915   BIOS-Version 5.3";
const char* const kBanner901 = "SCPX 8915  V 5.3  Anpassung:  V24  (XON/XOFF)";

/// Systemspuren (Bootabbild) einer Diskette aus den Fixtures — über eine TempDisk.
std::vector<uint8_t> systemabbild(const char* fixture, const char* fs = "") {
    TempDisk q(fixture);
    DtKataloge k;
    std::string err;
    auto vol = DiskVolume::open(q.path(), fs, k.formate, k.fs, err);
    std::vector<uint8_t> out;
    EXPECT_TRUE(vol) << err;
    if (vol) EXPECT_TRUE(vol->readBootImage(out)) << vol->lastError();
    return out;
}

void schreibeBin(const std::string& pfad, const std::vector<uint8_t>& b) {
    std::ofstream o(pfad, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

/// Kaltstart von @p pfad in A: bis zum Prompt; der Autostart `rade` findet nichts.
void bootetBisPromptOhneRade(K8915Machine& m, const std::string& pfad) {
    ASSERT_TRUE(m.mountDisk(0, pfad, "cpa800", false)) << m.lastError();
    ohneSelbsttestZurColdstartMeldung(m);
    m.keyboard().sendeZeichen(0x0D);
    ASSERT_TRUE(bis(m, "RADE?", 150'000'000)) << vramLines(m);
    for (long long t = 0; t < 20'000'000 && letzteZeile(m) != "A>"; t += m.run(kSchritt)) {}
    ASSERT_EQ(letzteZeile(m), "A>") << vramLines(m);
}

/**
 * Die ganze Kette: `boot-get` von @p quelle → `create --fs scpx8915 --boot` (Endung
 * @p ext) → Systemspuren byteweise gleich der Quelle → eine Datei per DiskTool → der
 * K8915 bootet davon bis `A>`, listet die Datei, gibt sie aus und schreibt selbst.
 */
void bautBootdiskette(const char* quelle, const char* ext, const char* banner) {
    const std::vector<uint8_t> sys = systemabbild(quelle);
    ASSERT_EQ(sys.size(), 20480u);
    TempDisk bin = TempDisk::empty("k8915_boot.bin");
    schreibeBin(bin.path(), sys);

    TempDisk neu = TempDisk::empty(std::string("k8915_bootdisk.") + ext);
    DtKataloge k;
    std::string err;
    {
        auto v = DiskVolume::create(neu.path(), "scpx8915", "", k.formate, k.fs, err, bin.path());
        ASSERT_TRUE(v) << err;
        EXPECT_EQ(v->bootAreaSize(), 20480u);
    }
    {
        // Maßstab: Systemspuren byteweise gleich der Quelle; das Dateisystem ist leer und heil.
        auto v = DiskVolume::open(neu.path(), "scpx8915", k.formate, k.fs, err);
        ASSERT_TRUE(v) << err;
        std::vector<uint8_t> n;
        ASSERT_TRUE(v->readBootImage(n)) << v->lastError();
        EXPECT_EQ(n, sys) << "Systemspuren weichen von der Quelle ab";
        EXPECT_TRUE(v->list().empty());
        EXPECT_TRUE(v->check(FsCheckLevel::Voll, true).ohneBefund());
    }
    ASSERT_NO_FATAL_FAILURE(dtPut(neu.path(), "scpx8915", "VOMDT.TXT", "AUF BOOTDISKETTE\n"));
    {
        // Mit Dateien darauf erkennt das DiskTool die Diskette ohne `--fs` als K8915.
        auto v = DiskVolume::open(neu.path(), "", k.formate, k.fs, err);
        ASSERT_TRUE(v) << err;
        EXPECT_EQ(v->profile().data_cyl, 2) << v->detection().filesystem;
    }

    K8915Machine m;
    ASSERT_NO_FATAL_FAILURE(bootetBisPromptOhneRade(m, neu.path()));
    EXPECT_TRUE(enthaelt(m, banner)) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "VOMDT    TXT")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "type vomdt.txt")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "AUF BOOTDISKETTE")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "save 2 x.com")) << vramLines(m);
    ASSERT_TRUE(befehl(m, "dir x.com")) << vramLines(m);
    EXPECT_TRUE(enthaelt(m, "A: X        COM")) << vramLines(m);
    EXPECT_FALSE(enthaelt(m, "ERR")) << vramLines(m);
}

}  // namespace

/// @test AP-E5c: `create --fs scpx8915 --boot` aus den Systemspuren der Diskette 900 (.hfe).
TEST(K8915Scpx, DiskToolBautBootdisketteAus900Hfe) { bautBootdiskette(kSys900, "hfe", kBanner900); }
/// @test Wie oben aus der Diskette 901 (BIOS-Fassung V24 XON/XOFF).
TEST(K8915Scpx, DiskToolBautBootdisketteAus901Hfe) { bautBootdiskette(kSys901, "hfe", kBanner901); }
/// @test Als `.img` (seit AP-E5b zulässig) — die aus Sektoren gebaute Spur bootet ebenso.
TEST(K8915Scpx, DiskToolBautBootdisketteAus900Img) { bautBootdiskette(kSys900, "img", kBanner900); }
TEST(K8915Scpx, DiskToolBautBootdisketteAus901Img) { bautBootdiskette(kSys901, "img", kBanner901); }

/**
 * @test K8915Scpx.BootPutMachtEineLeereDisketteBootfaehig
 * @brief `boot-put` auf eine leere, formatierte Diskette (ohne Systemspuren, wie nach
 *        FORMAT.COM): ohne `--fs scpx8915` sind das für das DiskTool eine CP/A-Datendiskette
 *        ohne Systemspuren (Meldung „keine Systemspuren“), mit dem Profil geht es.
 */
TEST(K8915Scpx, BootPutMachtEineLeereDisketteBootfaehig)
{
    const std::vector<uint8_t> sys = systemabbild(kSys900);
    TempDisk leer = TempDisk::empty("k8915_bootput.hfe");
    DtKataloge k;
    std::string err;
    ASSERT_TRUE(DiskVolume::create(leer.path(), "cpa800", "", k.formate, k.fs, err)) << err;
    {
        auto v = DiskVolume::open(leer.path(), "", k.formate, k.fs, err, /*read_only=*/false);
        ASSERT_TRUE(v) << err;
        EXPECT_FALSE(v->writeBootImage(sys)) << "cpa800 ab c0h0 hat keine Systemspuren";
        EXPECT_NE(v->lastError().find("keine Systemspuren"), std::string::npos);
    }
    {
        auto v = DiskVolume::open(leer.path(), "scpx8915", k.formate, k.fs, err, false);
        ASSERT_TRUE(v) << err;
        ASSERT_TRUE(v->writeBootImage(sys)) << v->lastError();
        ASSERT_TRUE(v->flush()) << v->lastError();
    }
    {
        auto v = DiskVolume::open(leer.path(), "scpx8915", k.formate, k.fs, err);
        ASSERT_TRUE(v) << err;
        std::vector<uint8_t> n;
        ASSERT_TRUE(v->readBootImage(n)) << v->lastError();
        EXPECT_EQ(n, sys);
    }
    K8915Machine m;
    ASSERT_NO_FATAL_FAILURE(bootetBisPromptOhneRade(m, leer.path()));
    EXPECT_TRUE(enthaelt(m, kBanner900)) << vramLines(m);
}

/**
 * @test K8915Scpx.FremdeSystemspurWirdFuerDenK8915Abgelehnt
 * @brief Ein A5120-CP/A-Bootabbild (kein Ladekopf), ein Abbild mit zerstörter Ladekopf-CRC
 *        und eines, das kürzer ist als der Ladekopf verlangt, werden abgewiesen — beim
 *        Anlegen (dann bleibt keine Datei liegen) wie bei `boot-put` (Systemspuren
 *        unverändert), auch wenn die Diskette nur an ihrem Ladekopf als K8915 kenntlich
 *        ist (Profil `cpa800`, ohne `--fs`).  Ein gültiges Abbild geht danach durch.
 */
TEST(K8915Scpx, FremdeSystemspurWirdFuerDenK8915Abgelehnt)
{
    const std::vector<uint8_t> a5120 = systemabbild("cpa_cpa780_k5601_noclock.hfe");
    ASSERT_GT(a5120.size(), 0u);
    std::vector<uint8_t> kaputt = systemabbild(kSys900);
    kaputt[3] ^= 0x01;                                  // Einsprung verändert, CRC bleibt
    std::vector<uint8_t> kurz = systemabbild(kSys900);
    kurz.resize(5 * 1024);                              // Ladekopf verlangt 12 Sektoren

    DtKataloge k;
    for (const std::vector<uint8_t>* schlecht : {static_cast<const std::vector<uint8_t>*>(&a5120), static_cast<const std::vector<uint8_t>*>(&kaputt), static_cast<const std::vector<uint8_t>*>(&kurz)}) {
        TempDisk bin = TempDisk::empty("k8915_fremd.bin");
        schreibeBin(bin.path(), *schlecht);
        TempDisk neu = TempDisk::empty("k8915_fremd_neu.hfe");
        std::string err;
        EXPECT_FALSE(DiskVolume::create(neu.path(), "scpx8915", "", k.formate, k.fs, err, bin.path()));
        EXPECT_FALSE(err.empty());
        EXPECT_FALSE(std::ifstream(neu.path()).good() && std::ifstream(neu.path()).peek() != EOF)
            << "es darf keine halbe Diskette liegen bleiben";

        // boot-put auf einer vorhandenen K8915-Systemdiskette (erkannt als cpa800).
        TempDisk sys(kSys900);
        auto v = DiskVolume::open(sys.path(), "", k.formate, k.fs, err, /*read_only=*/false);
        ASSERT_TRUE(v) << err;
        std::vector<uint8_t> vorher;
        ASSERT_TRUE(v->readBootImage(vorher));
        EXPECT_FALSE(v->writeBootImage(*schlecht)) << "Abbild " << schlecht->size() << " B";
        EXPECT_NE(v->lastError().find("Ladekopf"), std::string::npos) << v->lastError();
        std::vector<uint8_t> nachher;
        ASSERT_TRUE(v->readBootImage(nachher));
        EXPECT_EQ(nachher, vorher) << "abgelehnt heisst: nichts geschrieben";
    }

    // Gegenprobe: ein gültiges Abbild (auch das der anderen Fassung) geht auf 900.
    TempDisk sys(kSys900);
    std::string err;
    auto v = DiskVolume::open(sys.path(), "", k.formate, k.fs, err, false);
    ASSERT_TRUE(v) << err;
    const std::vector<uint8_t> s901 = systemabbild(kSys901);
    ASSERT_TRUE(v->writeBootImage(s901)) << v->lastError();
    std::vector<uint8_t> n;
    ASSERT_TRUE(v->readBootImage(n));
    EXPECT_EQ(n, s901);
}

/**
 * @test K8915Scpx.AusgelieferteBootabbilderSindDieSystemspurenDerDisketten
 * @brief `disks/bootsektoren/boot_scpx8915_{55k,v24}.bin` (README) = `boot-get` der Disketten 900/901 —
 *        sonst läge dort ein Abbild, dessen Herkunft niemand mehr belegen kann.
 */
TEST(K8915Scpx, AusgelieferteBootabbilderSindDieSystemspurenDerDisketten)
{
    using k1520test::diskPath;
    using k1520test::readFileBytes;
    for (const auto& p : {std::pair<const char*, const char*>{"boot_scpx8915_55k.bin", kSys900},
                          {"boot_scpx8915_v24.bin", kSys901}}) {
        const std::string datei = readFileBytes(diskPath(std::string("../../../disks/bootsektoren/") + p.first));
        const std::vector<uint8_t> sys = systemabbild(p.second);
        ASSERT_EQ(datei.size(), 20480u) << p.first;
        EXPECT_EQ(std::vector<uint8_t>(datei.begin(), datei.end()), sys) << p.first;
    }
}
