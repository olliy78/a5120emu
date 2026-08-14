/**
 * @file test_disktool_bootdiskette.cpp
 * @brief Kreuzprobe: das DiskTool baut eine **bootfähige** Diskette, der Emulator bootet sie.
 *
 * Eine leere Diskette anzulegen konnte das Werkzeug schon; bootfähig wird sie erst durch
 * die **Systemspuren** vor dem Dateisystem — das Lade-ROM liest Spur 0 blind ein, lange
 * bevor es ein Dateisystem gibt.  Dass ein solches Byteband richtig herausgeholt und
 * wieder eingespielt wird, kann kein Unit-Test beweisen: Maßstab ist allein, ob die
 * Maschine davon startet.
 *
 * Der Weg hier ist genau der des Anwenders (`tools/k1520disktool.md` §Bootdiskette):
 *
 *   1. Bootabbild aus einer echten CP/A-Diskette holen (@ref DiskVolume::readBootImage),
 *   2. neue Diskette **mit** diesem Abbild anlegen (`create --boot`),
 *   3. die Systemdateien hineinkopieren (`@OS.COM` und die übrigen),
 *   4. kalt davon starten — bis zur Uhrzeitabfrage und weiter zum `A>`-Prompt.
 *
 * Schritt 4 durchläuft die ganze Kette Lade-ROM → SYL-Lader → Sekundärlader →
 * CP/A-Bootsystem → `@OS.COM` (doc/K1520_architecture.md §14.5).
 *
 * @see doc/design/13_k1520disktool.md §15
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/machines/a5120/a5120.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/screen.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

using k1520test::diskPath;
using k1520test::QK_RETURN;
using k1520test::runCycles;
using k1520test::runSmallUntil;
using k1520test::typeKey;
using k1520test::typeString;
using k1520test::vramText;

namespace {

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        FormatCatalog k = FormatCatalog::loadDefault(&f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        FsCatalog k = FsCatalog::loadDefault(formate(), &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

/// @brief Temporärer Ordner, räumt sich weg.
class TempOrdner {
public:
    explicit TempOrdner(const char* name) : pfad_(k1520test::tempPath(name)) {
        std::error_code ec;
        fs::remove_all(pfad_, ec);
        fs::create_directories(pfad_, ec);
    }
    ~TempOrdner() { std::error_code ec; fs::remove_all(pfad_, ec); }
    const std::string& path() const { return pfad_; }
private:
    std::string pfad_;
};

/// @brief Temporäre Diskette samt der Sicherungskopie, die das Werkzeug anlegt.
class TempPfad {
public:
    explicit TempPfad(const char* name) : pfad_(k1520test::tempPath(name)) {
        std::error_code ec;
        fs::remove(pfad_, ec);
    }
    ~TempPfad() {
        std::error_code ec;
        fs::remove(pfad_, ec);
        fs::remove(pfad_ + "~", ec);
    }
    const std::string& get() const { return pfad_; }
private:
    std::string pfad_;
};

/**
 * @brief Eine Bootdiskette bauen — genau die drei Schritte des Anwenders.
 *
 * Bootabbild und Dateien kommen aus @p referenz, hinein gehen sie in eine frisch
 * angelegte Diskette des Profils @p fs_name.  Schlägt etwas fehl, bricht der
 * aufrufende Test ab (die ASSERTs stehen deshalb hier drin).
 */
void baueBootdiskette(const std::string& referenz, const std::string& fs_name,
                      const std::string& ziel, uint64_t bootgroesse) {
    std::string err;
    auto quelle = DiskVolume::open(referenz, fs_name, formate(), dateisysteme(), err);
    ASSERT_NE(quelle, nullptr) << err;

    const std::string bootbin = k1520test::tempPath("k1520_bootdisk_boot.bin");
    ASSERT_TRUE(quelle->readBootImageToFile(bootbin)) << quelle->lastError();
    ASSERT_EQ(fs::file_size(bootbin), bootgroesse) << "Systemspuren von " << fs_name;

    TempOrdner dateien("k1520_bootdisk_dateien");
    ASSERT_TRUE(quelle->extractAll(dateien.path(), TransferOptions{})) << quelle->lastError();
    const size_t anzahl = quelle->list().size();
    ASSERT_GT(anzahl, 10u) << "die Referenzdiskette ist unerwartet leer";
    quelle.reset();

    auto neu = DiskVolume::create(ziel, fs_name, "", formate(), dateisysteme(), err, bootbin);
    std::error_code ec;
    fs::remove(bootbin, ec);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_EQ(neu->bootAreaSize(), bootgroesse);
    ASSERT_TRUE(neu->insertAll(dateien.path(), TransferOptions{})) << neu->lastError();
    ASSERT_EQ(neu->list().size(), anzahl);
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    // ~DiskImage schreibt die Datei — erst danach darf der Emulator sie anfassen.
}

}  // namespace

/**
 * @test DiskToolBootdiskette/GebauteCpaDisketteBootetKalt
 * @brief `create --boot` + Systemdateien ergibt eine Diskette, von der CP/A startet.
 * @par Kriterium  Kaltstart bis zur Uhrzeitabfrage und nach der Eingabe bis zum
 *                 interaktiven `A>`-Prompt — dieselben Marken wie beim Original.
 */
TEST(DiskToolBootdiskette, GebauteCpaDisketteBootetKalt) {
    TempPfad ziel("k1520_bootdisk_cpa.hfe");
    ASSERT_NO_FATAL_FAILURE(baueBootdiskette(diskPath("cpa_cpa780_k5601_clock.img"),
                                             "cpa780", ziel.get(), 15104));

    A5120Machine machine;
    machine.powerOn();
    ASSERT_TRUE(machine.mountDisk(0, ziel.get(), "cpa780", /*wp=*/false))
        << machine.lastError();

    ASSERT_TRUE(runSmallUntil(machine, "Bitte Uhrzeit eingeben!", 60'000'000))
        << "die selbst gebaute Diskette bootete nicht bis zur Uhrzeitabfrage:\n"
        << vramText(machine);

    runCycles(machine, 2'000'000);
    typeString(machine, "120000");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "A>", 60'000'000))
        << "CP/A erreichte den A>-Prompt nicht:\n" << vramText(machine);
    runCycles(machine, 12'000'000);   // Selbststart nach dem Login abwarten

    // Und die Diskette ist BENUTZBAR: eingebauter Befehl und ein Programm von
    // Diskette.  Erst das beweist, dass auch der Laufzeit-Lesepfad stimmt.
    typeString(machine, "DIR");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "PIP", 30'000'000))
        << "DIR listete das Verzeichnis nicht:\n" << vramText(machine);

    // PIP.COM wird von der Diskette geladen und meldet sich mit seinem `*`-Prompt —
    // das prueft den Laufzeit-Lesepfad, nicht nur den Boot.
    typeString(machine, "PIP");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "*", 40'000'000))
        << "PIP.COM lud/lief nicht:\n" << vramText(machine);
}

/**
 * @test DiskToolBootdiskette/GebauteScpxDisketteBootetKalt
 * @brief Dasselbe für SCPX 1526 — anderes Betriebssystem, andere Geometrie.
 * @par Kriterium  Banner und `A>`-Prompt wie beim Original (SCPX fragt keine Uhrzeit).
 *                 SCPX' Systemspuren sind 16384 Byte gross (4 × 16×256) — dass die
 *                 Grenze pro Dateisystem stimmt, prüft dieser Fall mit.
 */
TEST(DiskToolBootdiskette, GebauteScpxDisketteBootetKalt) {
    TempPfad ziel("k1520_bootdisk_scpx.hfe");
    ASSERT_NO_FATAL_FAILURE(baueBootdiskette(diskPath("scpx17_cpa780_k5601.hfe"),
                                             "scpx640", ziel.get(), 16384));

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, ziel.get(), "cpa640", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 60'000'000))
        << "die selbst gebaute SCPX-Diskette zeigte den Banner nicht:\n"
        << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "A>", 10'000'000))
        << "A>-Prompt nach dem Banner nie erreicht:\n" << vramText(machine);
    runCycles(machine, 2'000'000);

    // Benutzbar: DIR (eingebaut) und STAT.COM (von Diskette geladen).
    typeString(machine, "DIR");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "BIOSG617", 30'000'000))
        << "DIR listete das Verzeichnis nicht:\n" << vramText(machine);

    typeString(machine, "STAT");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "Space:", 30'000'000))
        << "STAT.COM lud/lief nicht:\n" << vramText(machine);
}

/**
 * @test DiskToolBootdiskette/GebauteScpx798DisketteBootetKalt
 * @brief SCPX auf GEMISCHTER Geometrie — 2 × 16×256 Systemspuren, dann 5×1024.
 * @par Kriterium  Derselbe Banner.  Dieser Fall prüft, dass die Systemspuren auch
 *                 dort richtig abgegrenzt werden, wo sich die Sektorgrösse mitten
 *                 im Bereich ändert (18432 = 2×4096 + 2×5120).
 */
TEST(DiskToolBootdiskette, GebauteScpx798DisketteBootetKalt) {
    TempPfad ziel("k1520_bootdisk_scpx798.hfe");
    ASSERT_NO_FATAL_FAILURE(baueBootdiskette(diskPath("scpx17_5x1024_k5601_hardy.hfe"),
                                             "scpx798", ziel.get(), 18432));

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, ziel.get(), "scpx798", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();

    ASSERT_TRUE(runSmallUntil(machine, "SCPX 1526 - V 1.7", 60'000'000))
        << "die selbst gebaute SCPX-Diskette (5×1024) zeigte den Banner nicht:\n"
        << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "A>", 10'000'000))
        << "A>-Prompt nach dem Banner nie erreicht:\n" << vramText(machine);
}

/**
 * @test DiskToolBootdiskette/GebauteUdosDisketteBootetUndFuehrtBefehleAus
 * @brief UDOS: selbst gebaute Diskette, vollständiger Start und laufende Befehle.
 *
 * Der Weg dorthin war die eigentliche Arbeit (doc/udos_diskettenformat.md §14).  Drei
 * Dinge einer UDOS-Datei stehen **nicht** in ihren Bytes und mussten mitkopiert werden:
 *
 *  1. die **Systemspuren** samt der Bootspur 21 — und zwar mit dem Sektorkontrollblock
 *     hinter der Daten-CRC (ohne ihn: `ERROR: 45`);
 *  2. der **Kopfsektor** mit LOW/HIGH ADDRESS und STACK SIZE (Offset 122…127), aus
 *     denen der Lader die Speicherzuteilung nimmt (ohne sie:
 *     `MEMORY PROTECT VIOLATION`), sowie die zweite Längenangabe (Offset 17);
 *  3. das **ganze Speicherabbild**: bei `OS` reicht es 128 Byte über das logische
 *     Dateiende hinaus, und dort steht Programmcode, in den der Nukleus selbst springt
 *     (ohne ihn: `BREAK 4150` beim ersten Befehl).
 *
 * @par Kriterium  Banner der Startdatei `OS.INIT`, Datumsabfrage, `%`-Prompt — und
 *                 dann ein echter Befehl (`CAT`), der das Verzeichnis listet.
 */
TEST(DiskToolBootdiskette, GebauteUdosDisketteBootetUndFuehrtBefehleAus) {
    std::string err;
    auto quelle = DiskVolume::open(diskPath("udos_boot_scp.hfe"), "udos_ds77",
                                   formate(), dateisysteme(), err);
    ASSERT_NE(quelle, nullptr) << err;

    const std::string bootbin = k1520test::tempPath("k1520_udos_boot.bin");
    ASSERT_TRUE(quelle->readBootImageToFile(bootbin)) << quelle->lastError();

    TempOrdner dateien("k1520_udos_dateien");
    ASSERT_TRUE(quelle->extractAll(dateien.path(), TransferOptions{})) << quelle->lastError();
    const size_t anzahl = quelle->list().size();
    quelle.reset();

    // Das Beiblatt trägt die Kopfsektorangaben — ohne es wäre die Diskette tot.
    ASSERT_TRUE(fs::exists(fs::path(dateien.path()) / "udos-dateiangaben.txt"));

    TempPfad ziel("k1520_bootdisk_udos.hfe");
    auto neu = DiskVolume::create(ziel.get(), "udos_ds77", "UDOS.SYS.4.3",
                                  formate(), dateisysteme(), err, bootbin);
    std::error_code ec;
    fs::remove(bootbin, ec);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_TRUE(neu->insertAll(dateien.path(), TransferOptions{})) << neu->lastError();
    ASSERT_EQ(neu->list().size(), anzahl);

    // Der Nukleus muss byte-genau wieder ankommen — 5504 Byte logisch, 5632 im Abbild.
    for (const FileEntry& e : neu->list()) {
        if (e.name != "OS" || e.volume != 0) continue;
        EXPECT_EQ(e.size, 5504u);
        EXPECT_EQ(e.segment_len, 5632);
        EXPECT_EQ(e.block_len, 0) << "Offset 17 ist bei Satzlänge 512 NULL, nicht deren Kopie";
        EXPECT_EQ(e.low_addr, 0x1000);
        EXPECT_EQ(e.high_addr, 0x25FF);
    }
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();

    A5120Machine machine;
    ASSERT_TRUE(machine.mountDisk(0, ziel.get(), "udos_ds77", /*wp=*/false))
        << machine.lastError();
    machine.powerOn();

    // 1. Die Startdatei OS.INIT läuft: Banner, dann fragt DATE nach dem Datum.
    ASSERT_TRUE(runSmallUntil(machine, "UDOS 4.3", 150'000'000))
        << "die selbst gebaute UDOS-Diskette startete OS.INIT nicht:\n" << vramText(machine);
    ASSERT_TRUE(runSmallUntil(machine, "Neues Datum", 40'000'000))
        << "keine Datumsabfrage:\n" << vramText(machine);

    // 2. Datum eingeben → Systemmeldung und Prompt.
    runCycles(machine, 2'000'000);
    typeString(machine, "010187");
    typeKey(machine, QK_RETURN);
    ASSERT_TRUE(runSmallUntil(machine, "UDOS BC.5120", 60'000'000))
        << "nach der Datumseingabe kam das System nicht hoch:\n" << vramText(machine);

    // 3. Und ein Befehl läuft wirklich: CAT wird von Diskette geladen und listet.
    runCycles(machine, 4'000'000);
    typeString(machine, "cat");
    typeKey(machine, QK_RETURN);
    // `ZDOS` & Co. sind SECRET und erscheinen ohne `P=&` nicht — auf eine gewöhnliche
    // Datei prüfen.
    ASSERT_TRUE(runSmallUntil(machine, "HELP.DAT.00", 60'000'000))
        << "CAT lud/lief nicht:\n" << vramText(machine);
}

/**
 * @test DiskToolBootdiskette/UdosSystemspurenGehenMitUndZwarBeideSeiten
 * @brief UDOS: die Systemspuren wandern vollständig mit — die Grenze steht dabei.
 *
 * Bei UDOS sind die Systemspuren anders geschnitten als bei CP/M: Spuren 0–2
 * (Urlader + Nukleus) **und** die Bootspur 21, je Seite ein eigener Datenträger.
 * Dass die Bootspur dazugehört, ist am laufenden System belegt — ohne sie bricht der
 * Kaltstart mit `ERROR: 45` ab (Stand 2026-08-12).
 *
 * @par Kriterium  13312 Byte je Seite, unverändert wieder auslesbar, und Seite 1
 *                 bleibt unberührt, solange man ihr nichts gibt.
 * @par Grenze  Eine so gebaute UDOS-Diskette **bootet trotzdem nicht** von allein:
 *              nach dem Urlader landet sie im UDOS-Debugger (`BREAK F10E`).  UDOS
 *              legt sein Systemabbild nicht allein in den Systemspuren ab — dafür
 *              gibt es unter dem laufenden System eigene Werkzeuge.  Der Test hält
 *              deshalb fest, was das Werkzeug leistet, und behauptet nicht mehr.
 */
TEST(DiskToolBootdiskette, UdosSystemspurenGehenMitUndZwarBeideSeiten) {
    TempPfad ziel("k1520_bootdisk_udos.hfe");
    ASSERT_NO_FATAL_FAILURE(baueBootdiskette(diskPath("udos_boot_scp.hfe"),
                                             "udos_ds77", ziel.get(), 13728));

    std::string err;
    auto quelle = DiskVolume::open(diskPath("udos_boot_scp.hfe"), "udos_ds77",
                                   formate(), dateisysteme(), err);
    ASSERT_NE(quelle, nullptr) << err;
    auto neu = DiskVolume::open(ziel.get(), "udos_ds77", formate(), dateisysteme(), err);
    ASSERT_NE(neu, nullptr) << err;

    std::vector<uint8_t> q0, n0, n1;
    ASSERT_TRUE(quelle->readBootImage(q0, 0)) << quelle->lastError();
    ASSERT_TRUE(neu->readBootImage(n0, 0))   << neu->lastError();
    ASSERT_TRUE(neu->readBootImage(n1, 1))   << neu->lastError();
    EXPECT_EQ(q0, n0) << "die Systemspuren der Seite 0 kamen nicht unverändert an";
    EXPECT_EQ(n1.size(), 13728u);
    // Seite 1 blieb Leerdiskette: je Satz 128 Datenbytes 0xE5, dahinter der noch
    // unbeschriebene Kontrollblock (Gap-Fuellbytes 0x4E) — das Abbild ging nur auf
    // Seite 0, weil auch nur dafuer eines angegeben war.
    for (size_t i = 0; i + 132 <= n1.size(); i += 132)
        ASSERT_TRUE(std::all_of(n1.begin() + static_cast<long>(i),
                                n1.begin() + static_cast<long>(i + 128),
                                [](uint8_t b) { return b == 0xE5; }))
            << "Seite 1 hat ein Bootabbild bekommen, obwohl keines angegeben war "
               "(Satz bei Byte " << i << ")";
}
