/**
 * @file test_p8000_wega_install.cpp
 * @brief P8000 Meilenstein M3 (doc/design/25_p8000.md AP P15): WEGA 3.0 im Emulator auf die
 *        Winchester installieren und von der Platte starten.
 *
 * Soll = `~/projects/robotron/P8000/doc/install_WEGA_3.1.log` (Ablauf), Datenträger = die 17
 * WEGA-3.0-Disketten (doc/p8000/wega_datentraeger.md; NICHT im Repo — fehlen sie, wird der Test
 * übersprungen).  Stufen, jede mit Zwischenstand in der Ablage (`tests/support/p8000_wega.h`):
 *   1 `mkfs`  : Hardwaretest, `O U`, `boot`, `sa.format` (4.1) und `sa.mkfs` /usr 13000 + / 7000
 *   2 `root`  : `sa.install` nach md(0,16000) mit root1 … root5
 *   3 `usr`   : `sa.install` nach md(0,0) mit usr1 … usr9
 *   …
 * Eine vorhandene Stufe wird geladen statt gerechnet (`K1520_P8000_NEU=1` rechnet alles neu,
 * `K1520_P8000_BIS=<stufe>` hört nach dieser Stufe auf).  Harte Taktgrenzen, Fortschrittszeile je
 * 400 Mio. Takte — ein Hänger endet mit Bild, nicht im Zeitüberlauf.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "core/filesystem/disk_volume.h"
#include "core/logger.h"
#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"
#include "tests/support/p8000_wega.h"

using namespace k1520test::p8000;

namespace {
void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }
constexpr const char* FIXTURE = "udosP8000_640k_wega.hfe";

bool neuRechnen() { const char* e = std::getenv("K1520_P8000_NEU"); return e && *e && *e != '0'; }
bool letzteStufe(const char* name) {
    const char* e = std::getenv("K1520_P8000_BIS");
    return e && std::string(e) == name;
}

/// `sa.format`/`sa.verify` 4.1 (WEGA 3.1) anstelle der V1.4 der WEGA-3.0-Startdiskette (wie
/// test_p8000_sa_format.cpp); dafür weichen die Kernvarianten `wega.n26`/`wega.n52`.
::testing::AssertionResult saFassung41(const std::string& diskette) {
    std::string f, err;
    static const FormatCatalog fk = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs = FsCatalog::loadDefault(fk, &f);
    auto vol = DiskVolume::open(diskette, "", fk, fs, err, /*read_only=*/false);
    if (!vol) return ::testing::AssertionFailure() << err;
    vol->setBackup(false);
    for (const char* n : {"wega.n26", "wega.n52"}) {
        FileRef ref;
        ref.name = n;
        if (!vol->erase(ref)) return ::testing::AssertionFailure() << n << ": " << vol->lastError();
    }
    for (const char* n : {"sa.format", "sa.verify"}) {
        FileRef ref;
        ref.name = n;
        if (!vol->erase(ref)) return ::testing::AssertionFailure() << n << ": " << vol->lastError();
        if (!vol->insert(std::string(P8000_FIXTURE_DIR) + "/" + n, ref, TransferOptions{}))
            return ::testing::AssertionFailure() << n << ": " << vol->lastError();
    }
    if (!vol->flush()) return ::testing::AssertionFailure() << vol->lastError();
    return ::testing::AssertionSuccess();
}

/// Auf Text im Bild warten, Fortschrittszeile je 400 Mio. Takte.
bool langBisText(P8000Machine& m, const std::string& nadel, long long grenze) {
    for (long long t = 0; t < grenze; t += 400'000'000) {
        if (laufeBisText(m, nadel, std::min<long long>(400'000'000, grenze - t), 1'000'000)) return true;
        std::string z = cursorZeile(m);
        while (!z.empty() && z.front() == ' ') z.erase(z.begin());
        std::fprintf(stderr, "  [%6.0f Mio. Takte] %s\n", double(m.totalCycles()) / 1e6, z.c_str());
    }
    return false;
}

/// Frage abwarten (Cursorzeile endet darauf), dann @p antwort tippen.
::testing::AssertionResult frage(P8000Machine& m, const std::string& text, const std::string& antwort,
                                 long long grenze = 400'000'000) {
    if (warteAufFrage(m, {text}, grenze) != 0)
        return ::testing::AssertionFailure() << "fehlt: '" << text << "'\n" << bild(m);
    laufe(m, 200'000);
    tippeZeile(m, antwort);
    return ::testing::AssertionSuccess();
}

void bisBootPrompt(P8000Machine& m) {
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << bild(m);
    laufe(m, 2'000'000);
    m.nmi();
    ASSERT_TRUE(laufeBisText(m, "MAXSEG=<0F>", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "*", 80'000'000)) << bild(m);
    tippeZeile(m, "O U");
    ASSERT_TRUE(laufeBisText(m, "BOOTING FROM UDOS FLOPPY", 40'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ">", 40'000'000)) << bild(m);
    tippeZeile(m, "boot");
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
}

/// Stufe 1: frische Platte ohne PAR (wie im Protokoll), formatieren, Dateisysteme anlegen.
void stufeMkfs(WegaLauf& l) {
    {
        k1520test::TempDisk fix{FIXTURE};
        ASSERT_TRUE(kopiere(fix.path(), l.start));
    }
    ASSERT_TRUE(saFassung41(l.start));
    {   // Platte K5504.50, Z0/K0/S1 = E5 (kein PAR; die Spursynthese ergänzt ihn nicht)
        std::string fehler;
        ASSERT_TRUE(k1520::winchester::Platte::neu(l.platte, *k1520::winchester::typNachName("K5504.50"), &fehler))
            << fehler;
        std::fstream f(l.platte, std::ios::in | std::ios::out | std::ios::binary);
        const std::string e5(512, char(0xE5));
        f.write(e5.data(), std::streamsize(e5.size()));
    }
    l.m = std::make_unique<P8000Machine>(wegaConfig(l.platte, false));
    P8000Machine& m = *l.m;
    ASSERT_TRUE(m.mountDisk(0, l.start, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_NO_FATAL_FAILURE(bisBootPrompt(m));

    tippeZeile(m, "ud(0,0)sa.format");
    ASSERT_TRUE(frage(m, "Which Typ ? (No./n/q)", "4"));
    ASSERT_TRUE(frage(m, "Parameter for Drive ok ? (y/l/p/q)", "y"));
    ASSERT_TRUE(frage(m, "Manual Input of bad Track of Drive 0 (y/n/q) ?", "n"));
    ASSERT_TRUE(frage(m, "Format Begin: Cylinder (a/Start-Cylinder)", "a"));
    ASSERT_TRUE(frage(m, "to Cyl 1023 Hd 4 ? (y/n/q)", "y"));
    ASSERT_TRUE(frage(m, "Rewrite PAR&BTT from WDC-RAM to HD-Drive 0 ? (y/n)", "y", 8'000'000'000LL));
    ASSERT_TRUE(frage(m, "End of 'sa.format' (y/n) ?", "y"));
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
    // sa.verify ist in test_p8000_sa_format.cpp abgedeckt und wird hier übersprungen.
    struct Fs { const char* groesse; const char* name; };
    for (const Fs& fs : {Fs{"13000", "md(0,0)"}, Fs{"7000", "md(0,16000)"}}) {
        tippeZeile(m, "ud(0,0)sa.mkfs");
        ASSERT_TRUE(frage(m, "file system size:", fs.groesse));
        ASSERT_TRUE(frage(m, "file system:", fs.name));
        ASSERT_TRUE(laufeBisText(m, "m/n = 1 72", 4'000'000'000LL)) << bild(m);
        ASSERT_TRUE(laufeBisPrompt(m, ":", 4'000'000'000LL)) << bild(m);
    }
}

/// `sa.install` mit den Disketten @p disks nach @p ziel (Protokoll Z. 106–439 bzw. 444–1555).
void saInstall(WegaLauf& l, const std::string& ziel, const std::vector<std::string>& disks) {
    P8000Machine& m = *l.m;
    tippeZeile(m, "ud(0,0)sa.install");
    ASSERT_TRUE(frage(m, "Enter Date (MM/DD/YY) :", "05/30/89"));
    // Quelldiskette erst NACH dem Laden von sa.install einlegen: liegt die 9×512-Diskette schon
    // vorher in Laufwerk 1, scheitert das Laden von ud(0,0) mit UDOS-Fehler C4 (SECTOR ADDRESS
    // ERROR) — Befund P15, s. doc/design/25_p8000.md §9 P15.
    ASSERT_TRUE(l.diskettenwechsel(disks.front()));
    ASSERT_TRUE(frage(m, "input file system :", "fd(1,0)"));
    ASSERT_TRUE(frage(m, "output file system :", ziel));
    size_t naechste = 1;
    for (;;) {
        const int i = warteAufFrage(m, {"(n/y/a/A/q/Q) ? :", "next input disk ? (y/n) :",
                                        "overwrite (y/n/q) ? :", "repeat (y/n/q) ? :"},
                                    4'000'000'000LL);
        ASSERT_GE(i, 0) << "sa.install haengt\n" << bild(m);
        ASSERT_NE(i, 3) << "Lesefehler\n" << bild(m);
        laufe(m, 200'000);
        if (i == 0) { tippeZeile(m, "A"); continue; }
        if (i == 2) { tippeZeile(m, "y"); continue; }
        // next input disk
        if (naechste < disks.size()) {
            std::fprintf(stderr, "  [Diskette %s]\n", disks[naechste].c_str());
            ASSERT_TRUE(l.diskettenwechsel(disks[naechste++]));
            tippeZeile(m, "y");
        } else {
            tippeZeile(m, "n");
            break;
        }
    }
    EXPECT_EQ(naechste, disks.size());
    ASSERT_TRUE(laufeBisText(m, "Exit called", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
    l.diskAuswerfen();
}

/// Stufe 4: Kern von der Platte starten (`md(0,16000)wega`, Protokoll Z. 1559–1584), Einbenutzer-
/// betrieb, `/etc/new.install` (Z. 1584–1666), `init 2` bis `login:` (Z. 1666–1724).
/// WEGA 3.0 meldet — anders als das 3.1-Protokoll — kein „Single-User Mode", nur den Prompt `#1`.
void stufeKern(WegaLauf& l) {
    P8000Machine& m = *l.m;
    tippeZeile(m, "md(0,16000)wega");
    ASSERT_TRUE(langBisText(m, "WEGA Kernel -- Release 3.2", 2'000'000'000LL)) << bild(m);
    ASSERT_TRUE(langBisText(m, "file system /z          = offset 27000, 60732 blocks", 400'000'000)) << bild(m);
    ASSERT_GE(warteAufFrage(m, {"#1"}, 2'000'000'000LL), 0) << bild(m);
    laufe(m, 2'000'000);
}

void stufeNewInstall(WegaLauf& l) {
    P8000Machine& m = *l.m;
    tippeZeile(m, "/etc/new.install");
    ASSERT_TRUE(frage(m, "neu angelegt werden ? (j/n) :", "j", 2'000'000'000LL));
    ASSERT_TRUE(frage(m, "/dev/tmp (Standard 4000) :", "4000"));
    ASSERT_TRUE(frage(m, "/dev/z (Standard 60732) :", "60732"));   // WEGA 3.0: ein Leerzeichen
    ASSERT_TRUE(langBisText(m, "Damit ist das System vollstaendig eingerichtet.", 40'000'000'000LL)) << bild(m);
    ASSERT_GE(warteAufFrage(m, {"#2"}, 2'000'000'000LL), 0) << bild(m);
    laufe(m, 2'000'000);
}

/// Stufe 6: `init 2` — Prüfläufe, Datum/Uhrzeit, Mehrbenutzerbetrieb bis `login:` (Z. 1666–1724).
void stufeMehrbenutzer(WegaLauf& l) {
    P8000Machine& m = *l.m;
    tippeZeile(m, "init 2");
    ASSERT_TRUE(frage(m, "Enter Date (MM/DD/YY or <cr>):", "05/30/89", 40'000'000'000LL));
    ASSERT_TRUE(frage(m, "Enter Time (HH:MM):", "20:55"));
    ASSERT_TRUE(langBisText(m, "Going multi-user", 4'000'000'000LL)) << bild(m);
    ASSERT_GE(warteAufFrage(m, {"login:"}, 4'000'000'000LL), 0) << bild(m);
}

/// Eine Stufe: aus der Ablage laden oder rechnen und ablegen.  Liefert false, wenn danach Schluss ist.
template <typename F>
bool stufe(WegaLauf& l, const char* name, F&& rechnen) {
    std::string fehler;
    if (!neuRechnen() && stufeDa(name)) {
        EXPECT_TRUE(stufeLaden(l, name, &fehler)) << fehler;
    } else {
        std::fprintf(stderr, "  [Stufe %s rechnen]\n", name);
        rechnen();
        if (::testing::Test::HasFatalFailure()) return false;
        EXPECT_TRUE(stufeSichern(l, name, &fehler)) << fehler;
    }
    return !::testing::Test::HasFailure() && !letzteStufe(name);
}
}  // namespace

TEST(P8000WegaInstall, InstalliertWegaAufDiePlatte) {
    stumm();
    if (!dateiDa(wegaDiskette("root1")))
        GTEST_SKIP() << "WEGA-3.0-Disketten fehlen (" << wegaDisketten() << ", K1520_WEGA_DISKS)";
    WegaLauf l;
    if (!stufe(l, "p15_1_mkfs", [&] { stufeMkfs(l); })) return;
    if (!stufe(l, "p15_2_root", [&] { saInstall(l, "md(0,16000)", {"root1", "root2", "root3", "root4", "root5"}); }))
        return;
    if (!stufe(l, "p15_3_usr", [&] {
            saInstall(l, "md(0,0)", {"usr1", "usr2", "usr3", "usr4", "usr5", "usr6", "usr7", "usr8", "usr9"});
        }))
        return;
    if (!stufe(l, "p15_4_kern", [&] { stufeKern(l); })) return;
    if (!stufe(l, "p15_5_newinst", [&] { stufeNewInstall(l); })) return;
    if (!stufe(l, "p15_6_login", [&] { stufeMehrbenutzer(l); })) return;
    // Abnahme M3: Anmeldung als Superuser (Systemhandbuch: Name „wega", Passwort bei Auslieferung
    // „root"), einfache Kommandos am Terminal.
    P8000Machine& m = *l.m;
    tippeZeile(m, "wega");
    ASSERT_TRUE(frage(m, "Password:", "root", 400'000'000));
    ASSERT_GE(warteAufFrage(m, {"#1"}, 2'000'000'000LL), 0) << bild(m);
    for (const char* k : {"date", "who", "ls /"}) {
        tippeZeile(m, k);
        laufe(m, 200'000'000);
    }
    const std::string b = bild(m);
    EXPECT_NE(b.find("MES 1989"), std::string::npos) << b;          // date
    EXPECT_NE(b.find("wega     console"), std::string::npos) << b;  // who
    EXPECT_NE(b.find("pb.image"), std::string::npos) << b;          // ls /
    std::fprintf(stderr, "%s", b.c_str());
    // Für den Kaltstart: Puffer zurückschreiben (wie `sync;sync` im Protokoll Z. 1851) und die Platte
    // als Zwischenstand ablegen — ohne sync findet fsck beim Start „BAD FREE LIST" und verlangt
    // „BOOT WEGA (NO SYNC!)".
    tippeZeile(m, "sync;sync");
    ASSERT_GE(warteAufFrage(m, {"#5"}, 400'000'000), 0) << bild(m);
    laufe(m, 400'000'000);
    std::string fehler;
    EXPECT_TRUE(stufeSichern(l, "p15_7_sync", &fehler)) << fehler;
}

/// Kaltstart von der installierten Platte (Protokoll Z. 1852–1941): Netz ein, MON8 → RETURN → UDOS
/// mit der Koppelsoftware → MON16 → NMI → Hardwaretest → AUTOBOOT lädt Block 0 (`pb.image` =
/// boot0.md, von `/etc/new.install` geschrieben) → `> boot` → `:` → `md(0,16000)wega` →
/// Mehrbenutzerbetrieb → `login:`.  Kein Save-State: nur Platte + Startdiskette aus p15_7_sync.
TEST(P8000WegaInstall, KaltstartVonDerPlatteBisZurAnmeldung) {
    stumm();
    if (!stufeDa("p15_7_sync")) GTEST_SKIP() << "kein Zwischenstand p15_7_sync";
    WegaLauf l;
    ASSERT_TRUE(kopiere(stufenPfad("p15_7_sync", "platte.img"), l.platte));
    ASSERT_TRUE(kopiere(stufenPfad("p15_7_sync", "start.hfe"), l.start));
    l.m = std::make_unique<P8000Machine>(wegaConfig(l.platte, false));
    P8000Machine& m = *l.m;
    ASSERT_TRUE(m.mountDisk(0, l.start, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisText(m, "U880-Softwaremonitor Version 3.1 - Press RETURN", 200'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisPrompt(m, ">", 4'000'000)) << bild(m);
    tippe(m, "\r");
    ASSERT_TRUE(laufeBisText(m, "U8000-Softwaremonitor Version 3.1 - Press NMI", 80'000'000)) << bild(m);
    laufe(m, 2'000'000);
    m.nmi();
    ASSERT_TRUE(laufeBisText(m, "MAXSEG=<0F>", 400'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, ">", 400'000'000)) << bild(m);   // boot0.md aus Block 0
    tippeZeile(m, "boot");
    ASSERT_TRUE(laufeBisPrompt(m, ":", 400'000'000)) << bild(m);
    tippeZeile(m, "md(0,16000)wega");
    ASSERT_TRUE(langBisText(m, "WEGA Kernel -- Release 3.2", 2'000'000'000LL)) << bild(m);
    for (;;) {
        const int i = warteAufFrage(m, {"Enter Date (MM/DD/YY or <cr>):", "Enter Time (HH:MM):", "login:", "#1"},
                                    8'000'000'000LL);
        ASSERT_GE(i, 0) << bild(m);
        if (i == 2) break;
        laufe(m, 200'000);
        if (i == 3) { tippeZeile(m, "init 2"); continue; }   // falls der Kern im Einbenutzerbetrieb steht
        tippeZeile(m, i == 0 ? "" : "21:10");
    }
    tippeZeile(m, "wega");
    ASSERT_TRUE(frage(m, "Password:", "root", 400'000'000));
    ASSERT_GE(warteAufFrage(m, {"#1"}, 2'000'000'000LL), 0) << bild(m);
    tippeZeile(m, "who");
    EXPECT_TRUE(laufeBisText(m, "wega     console", 400'000'000)) << bild(m);
    std::fprintf(stderr, "%s", bild(m).c_str());
}

/// Befund P15 (Gastverhalten, kein Emulatorfehler): UDOS sucht eine Datei ohne Laufwerksangabe
/// auch auf Laufwerk 1 — SENSE DRIVE STATUS meldet dort READY, also liest es Spur 22 mit 256-B-
/// Sektoren; eine WEGA-Diskette (9 × 512) hat keine ⇒ ST1 = 04 (No Data) ⇒ „File … Error C4"
/// (SECTOR ADDRESS ERROR).  Mit leerem Laufwerk 1 (ST3 = 19H, nicht bereit) lädt dasselbe.
/// Prüft nebenbei, dass ein Zwischenstand nach dem Laden wieder von der Startdiskette liest.
TEST(P8000WegaInstall, UdosSuchtAufLaufwerkEinsMitWegaDisketteC4) {
    stumm();
    if (!stufeDa("p15_1_mkfs") || !dateiDa(wegaDiskette("root1"))) GTEST_SKIP() << "kein Zwischenstand";
    for (bool lw1 : {false, true}) {
        WegaLauf l;
        std::string fehler;
        ASSERT_TRUE(stufeLaden(l, "p15_1_mkfs", &fehler)) << fehler;
        if (lw1) ASSERT_TRUE(l.diskettenwechsel("root1"));
        tippeZeile(*l.m, "ud(0,0)sa.install");
        if (lw1)
            EXPECT_TRUE(laufeBisText(*l.m, "File sa.install Error C4", 400'000'000)) << bild(*l.m);
        else
            EXPECT_GE(warteAufFrage(*l.m, {"Enter Date (MM/DD/YY) :"}, 400'000'000), 0) << bild(*l.m);
    }
}

/// Wächter P15: Eingabe an den laufenden WEGA-Kern (Zeichen über SIO0-B des 8-Bit-Teils →
/// Koppelsoftware → 16-PIO1 → `_kint` → `_rint`).  Rot war: `srlb rl4,#3` als B2C1 FFFD kannte
/// der U8001-Kern nicht ⇒ `_kint` sprang nach `_xint`, kein Echo, keine Kommandos.
TEST(P8000WegaInstall, KernNimmtKommandosAmTerminalAn) {
    stumm();
    if (!stufeDa("p15_4_kern")) GTEST_SKIP() << "kein Zwischenstand p15_4_kern";
    WegaLauf l;
    std::string fehler;
    ASSERT_TRUE(stufeLaden(l, "p15_4_kern", &fehler)) << fehler;
    tippeZeile(*l.m, "ls");
    EXPECT_TRUE(laufeBisText(*l.m, "sa.install", 80'000'000)) << bild(*l.m);
    EXPECT_GE(warteAufFrage(*l.m, {"#2"}, 80'000'000), 0) << bild(*l.m);
}

/// Werkzeug (kein Wächter): Kommandos am `#`-Prompt des Zwischenstands p15_4_kern ausführen und
/// das Bild drucken — `K1520_P8000_KOMMANDOS='date;ls /bin' … --gtest_also_run_disabled_tests
/// --gtest_filter='*Kommandos*'`.
TEST(P8000WegaInstall, DISABLED_Kommandos) {
    stumm();
    WegaLauf l;
    std::string fehler;
    ASSERT_TRUE(stufeLaden(l, "p15_4_kern", &fehler)) << fehler;
    const char* k = std::getenv("K1520_P8000_KOMMANDOS");
    std::string alle = k ? k : "date;sh -c ls";
    size_t pos = 0;
    int n = 2;
    while (pos <= alle.size()) {
        size_t e = alle.find(';', pos);
        std::string cmd = alle.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? alle.size() + 1 : e + 1;
        tippeZeile(*l.m, cmd);
        warteAufFrage(*l.m, {"#" + std::to_string(n++)}, 400'000'000);
    }
    std::fprintf(stderr, "%s", bild(*l.m).c_str());
}
