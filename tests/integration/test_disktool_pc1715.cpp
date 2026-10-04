/**
 * @file test_disktool_pc1715.cpp
 * @brief DiskTool für PC 1715 / 1715W (doc/design/21_pc1715.md AP-D).
 *
 * Maßstab ist wie bei `DiskToolBootdiskette.*` und `DisktoolPrg710.*`, ob die Maschine von der
 * gebauten Diskette startet:
 *  - **CP/A 1715 ohne Systemspuren:** der Bootkopf (`F003H`) steht in Verzeichnisplatz 0, die
 *    Laufwerks-Parametersätze in einem Platz mit Nutzerbyte `F0` — beides gehört dem Urlader S502,
 *    nicht dem Dateisystem.  Erkennung (`cpa1715`), `ls`/`get`, Schreiben lässt beide unangetastet,
 *    `fsck` meldet sie nicht.
 *  - **SCP 1715 / SCP 3.0 mit Systemspuren:** `boot-get` → `create --fs scp1715 --boot` + Dateien;
 *    der Ladekopf wird VOR dem Schreiben geprüft (A5120-SYL-Kopf abgelehnt).
 *  - **Alle PC-1715-Abzüge** (`tests/fixtures/disks/pc1715*.hfe`) gegen die Tabelle in
 *    `doc/pc1715/disketten.md`: Format, Dateisystem, Dateizahl, keine Falschmeldung.
 *
 * Disketten nur über `TempDisk`/Temp-Pfade (das DiskTool öffnet schreibend).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/disk_volume.h"
#include "core/logger.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/pc1715_input.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;
using namespace k1520test::pc1715;

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

/// Temporärer Pfad samt Sicherungskopie des DiskTools, räumt sich weg.
class TempPfad {
public:
    explicit TempPfad(const char* name) : pfad_(k1520test::tempPath(name)) { weg(); }
    ~TempPfad() { weg(); }
    const std::string& get() const { return pfad_; }
private:
    void weg() {
        std::error_code ec;
        fs::remove_all(pfad_, ec);
        fs::remove(pfad_ + "~", ec);
    }
    std::string pfad_;
};

std::vector<uint8_t> datei(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

void schreibe(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

std::unique_ptr<DiskVolume> oeffne(const std::string& pfad, bool schreibend = false,
                                   const char* fs_name = "") {
    std::string err;
    auto dv = DiskVolume::open(pfad, fs_name, formate(), dateisysteme(), err, !schreibend);
    EXPECT_NE(dv, nullptr) << pfad << ": " << err;
    return dv;
}

/// Eine Datei der Diskette in ein Byte-Feld holen.
std::vector<uint8_t> holeDatei(DiskVolume& dv, const std::string& name) {
    TempPfad ziel("k1520_pc1715_datei.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv.extract(ref, ziel.get(), TransferOptions{})) << name << ": " << dv.lastError();
    return datei(ziel.get());
}

/// Bytes unter dem Namen @p name auf die Diskette schreiben.
bool lege(DiskVolume& dv, const std::string& name, const std::vector<uint8_t>& daten) {
    TempPfad quelle("k1520_pc1715_quelle.bin");
    schreibe(quelle.get(), daten);
    FileRef ref;
    ref.name = name;
    return dv.insert(quelle.get(), ref, TransferOptions{});
}

/// Alle Dateien (Benutzerbereich 0) von @p von nach @p nach, @p zuerst vorweg.
void kopiereAlle(DiskVolume& von, DiskVolume& nach, const std::string& zuerst = "") {
    std::vector<std::string> namen;
    for (const FileEntry& e : von.list()) {
        ASSERT_EQ(e.user, 0) << e.name;
        namen.push_back(e.name);
    }
    if (!zuerst.empty()) {
        auto it = std::find(namen.begin(), namen.end(), zuerst);
        ASSERT_NE(it, namen.end()) << zuerst << " fehlt";
        std::rotate(namen.begin(), it, it + 1);
    }
    for (const std::string& n : namen) {
        ASSERT_TRUE(lege(nach, n, holeDatei(von, n))) << n << ": " << nach.lastError();
    }
}

std::string kennungen(const FsCheckReport& r) {
    std::string s;
    for (const FsFinding& f : r.findings) s += f.id + " ";
    return s;
}

std::string fixtureDir() {
    return fs::path(k1520test::diskPath("x.hfe")).parent_path().string();
}

void stumm() { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR); }

}  // namespace

/// Format, Dateisystem und Dateizahl je Abzug — die Tabelle aus `doc/pc1715/disketten.md`
/// (Spalte „Dateien", Stand der Fixtures seit AP-0c).
struct Abzug {
    const char* datei;
    const char* format;
    const char* dateisystem;
    int dateien;
};

static const Abzug kAbzuege[] = {
    // SCP 1715 V0006 (5×1024): Systemspuren, CP/A-Regel → `cpa_auto`
    {"pc1715_scp1715_v0006_boot.hfe", "cpa800", "cpa_auto", 9},
    // dieselbe + PCTEST.COM (AP-4f, Werkstest unter SCP)
    {"pc1715_scp1715_v0006_pctest.hfe", "cpa800", "cpa_auto", 10},
    // SCP 1715 V0007 (16×256): zweites Format, Katalogprofil
    {"pc1715_scp1715_v0007_cpa640_boot.hfe", "cpa640", "scpx640", 4},
    // CP/A 1715: KEINE Systemspuren, Bootkopf im Verzeichnis — vorher „nicht erkannt"
    {"pc1715_cpa1715_boot_4lw.hfe", "cpa800", "cpa1715", 49},
    // CP/A 1715 aus der CPA-Workbench gebaut (AP-3b, Bootkopf F003H, PCTEST.COM dabei)
    {"pc1715_cpa1715_workbench.hfe", "cpa800", "cpa1715", 22},
    // UDOS 1715
    {"pc1715_udos1715_system.hfe", "k5601_16x256", "udos1715", 67},
    // CP/Z 2.2
    {"pc1715_cpz22_boot.hfe", "cpa640", "scpx640", 35},
    // SCP 3.0 (1715W)
    {"pc1715w_scp30_system.hfe", "cpa800", "cpa_auto", 42},
};

/**
 * @test Jeder PC-1715-Abzug wird erkannt, hat die erwartete Dateizahl und prüft (schnell UND voll)
 *       ohne Befund ab Schwere `Warnung`.  Der zweite Teil macht die Tabelle vollständig: eine neue
 *       `pc1715*.hfe` ohne Zeile hier schlägt an.
 */
TEST(DisktoolPc1715, ErkenntJedenAbzugMitDateizahlUndOhneFalschmeldung) {
    for (const Abzug& a : kAbzuege) {
        auto dv = oeffne(k1520test::diskPath(a.datei));
        ASSERT_NE(dv, nullptr) << a.datei;
        EXPECT_EQ(dv->detection().format, a.format) << a.datei;
        EXPECT_EQ(dv->detection().filesystem, a.dateisystem) << a.datei;
        EXPECT_TRUE(dv->detection().unambiguous) << a.datei;
        EXPECT_EQ(static_cast<int>(dv->list().size()), a.dateien) << a.datei;
        for (FsCheckLevel stufe : {FsCheckLevel::Schnell, FsCheckLevel::Voll}) {
            const FsCheckReport& r = dv->check(stufe, true);
            EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << a.datei << ": " << kennungen(r);
        }
    }

    int gefunden = 0;
    for (const auto& e : fs::directory_iterator(fixtureDir())) {
        const std::string n = e.path().filename().string();
        if (n.rfind("pc1715", 0) != 0 || e.path().extension() != ".hfe") continue;
        ++gefunden;
        const bool da = std::any_of(std::begin(kAbzuege), std::end(kAbzuege),
                                    [&](const Abzug& a) { return n == a.datei; });
        EXPECT_TRUE(da) << n << " steht nicht in der Abzug-Tabelle (doc/pc1715/disketten.md)";
    }
    EXPECT_EQ(gefunden, static_cast<int>(std::size(kAbzuege)));
}

/// @test Nur der Bootkopf macht aus einer Datendiskette ohne Systemspuren eine `cpa1715`: dieselbe
///       Geometrie ohne `F003H` bleibt `cpa800` (sonst wäre jede 800K-Diskette „nicht eindeutig").
TEST(DisktoolPc1715, DatendisketteOhneBootkopfBleibtCpa800) {
    TempPfad pfad("k1520_pc1715_daten.hfe");
    std::string err;
    auto neu = DiskVolume::create(pfad.get(), "cpa800", "", formate(), dateisysteme(), err);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_TRUE(lege(*neu, "TEST.TXT", std::vector<uint8_t>(300, 'x'))) << neu->lastError();
    ASSERT_TRUE(neu->flush());
    neu.reset();

    auto dv = oeffne(pfad.get());
    ASSERT_NE(dv, nullptr);
    EXPECT_EQ(dv->detection().filesystem, "cpa800");
    EXPECT_TRUE(dv->detection().unambiguous);
}

/**
 * @test Verzeichnisplatz 0 (Bootkopf) und der `F0`-Platz (Parametersätze) bleiben beim Schreiben
 *       bitgleich: `put`, `rm`, `attr` (alle Wege über das Verzeichnis) rühren sie nicht an, und
 *       sie erscheinen weder als Dateien noch als Befund.
 */
TEST(DisktoolPc1715, Cpa1715BootbereichBleibtBeimSchreibenUnangetastet) {
    k1520test::TempDisk d("pc1715_cpa1715_boot_4lw.hfe", "k1520_pc1715_cpa_schreiben.hfe");
    std::vector<uint8_t> vorher;
    {
        auto dv = oeffne(d.path(), /*schreibend=*/true);
        ASSERT_NE(dv, nullptr);
        dv->setBackup(false);
        ASSERT_EQ(dv->profile().name, "cpa1715");
        ASSERT_TRUE(dv->readBootImage(vorher));
        ASSERT_EQ(vorher.size(), 128u);
        ASSERT_EQ(vorher[0], 0x03);
        ASSERT_EQ(vorher[1], 0xF0);
        ASSERT_EQ(vorher[0x60], 0xF0);

        // Keine Datei trägt einen Namen aus Bootkopf-Bytes, keine hat den Nutzerbereich 3.
        for (const FileEntry& e : dv->list()) {
            std::string why;
            EXPECT_TRUE(CpmFileSystem::validName(e.name, &why)) << e.name << ": " << why;
            EXPECT_EQ(e.user, 0) << e.name;
        }

        ASSERT_TRUE(lege(*dv, "NEU.TXT", std::vector<uint8_t>(5000, 'n'))) << dv->lastError();
        FileRef weg;
        weg.name = "PIP.COM";
        ASSERT_TRUE(dv->erase(weg)) << dv->lastError();
        ASSERT_TRUE(lege(*dv, "NOCH.TXT", std::vector<uint8_t>(40000, 'm'))) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }

    auto dv = oeffne(d.path());
    ASSERT_NE(dv, nullptr);
    EXPECT_EQ(dv->profile().name, "cpa1715");
    std::vector<uint8_t> nachher;
    ASSERT_TRUE(dv->readBootImage(nachher));
    // Platz 0 (Bootkopf) und Platz 3 (Parametersätze) bitgleich; Platz 1/2 sind Dateieinträge
    // (@OS.COM, das gelöschte PIP.COM) und dürfen sich ändern.
    for (int slot : {0, 3})
        EXPECT_TRUE(std::equal(vorher.begin() + slot * 32, vorher.begin() + slot * 32 + 32,
                               nachher.begin() + slot * 32))
            << "Bootbereich (Platz " << slot << ") wurde beim Schreiben verändert";
    // CP/M füllt auf ganze 128-B-Sätze auf: Inhalt gleich, Länge aufgerundet.
    auto gleichBisSatz = [](std::vector<uint8_t> ist, size_t n, uint8_t b) {
        return ist.size() == (n + 127) / 128 * 128
            && std::all_of(ist.begin(), ist.begin() + static_cast<long>(n),
                           [&](uint8_t x) { return x == b; });
    };
    EXPECT_TRUE(gleichBisSatz(holeDatei(*dv, "NEU.TXT"), 5000, 'n'));
    EXPECT_TRUE(gleichBisSatz(holeDatei(*dv, "NOCH.TXT"), 40000, 'm'));
    for (FsCheckLevel stufe : {FsCheckLevel::Schnell, FsCheckLevel::Voll}) {
        const FsCheckReport& r = dv->check(stufe, true);
        EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
        for (const FsFinding& f : r.findings)
            EXPECT_NE(f.object, "Platz 0") << f.id;      // nicht einmal als Hinweis
    }
}

/**
 * @test CP/A 1715 → `boot-get` → `create --fs cpa1715 --boot` → `@OS.COM` zuerst → bootet in
 *       `Pc1715Machine` bis `A>`.  `@OS.COM` muss auf Block 3 landen, weil der Bootkopf es dort
 *       sucht (Ladeliste ab Byte 0CH) — die erste Datei einer frischen Diskette tut es.
 */
TEST(DisktoolPc1715, GebauteCpa1715BootdisketteBootetBisPrompt) {
    stumm();
    k1520test::TempDisk quelle("pc1715_cpa1715_boot_4lw.hfe", "k1520_pc1715_cpa_quelle.hfe");
    auto q = oeffne(quelle.path());
    ASSERT_NE(q, nullptr);
    std::vector<uint8_t> kopf;
    ASSERT_TRUE(q->readBootImage(kopf));
    TempPfad kopfDatei("k1520_pc1715_kopf.bin");
    schreibe(kopfDatei.get(), kopf);

    TempPfad pfad("k1520_pc1715_cpa_neu.hfe");
    std::string err;
    auto neu = DiskVolume::create(pfad.get(), "cpa1715", "", formate(), dateisysteme(), err,
                                  kopfDatei.get());
    ASSERT_NE(neu, nullptr) << err;
    EXPECT_EQ(neu->bootAreaSize(), 128u);
    ASSERT_TRUE(lege(*neu, "@OS.COM", holeDatei(*q, "@OS.COM"))) << neu->lastError();
    ASSERT_TRUE(lege(*neu, "PIP.COM", holeDatei(*q, "PIP.COM"))) << neu->lastError();
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();

    // Wiedererkennung ohne --fs, Dateien, Bootbereich.
    {
        auto dv = oeffne(pfad.get());
        ASSERT_NE(dv, nullptr);
        EXPECT_EQ(dv->detection().filesystem, "cpa1715");
        EXPECT_EQ(dv->list().size(), 2u);
        std::vector<uint8_t> k2;
        ASSERT_TRUE(dv->readBootImage(k2));
        EXPECT_EQ(k2[0], 0x03);
        EXPECT_EQ(k2[1], 0xF0);
        EXPECT_EQ(k2[0x60], 0xF0);
        EXPECT_EQ(0, dv->check(FsCheckLevel::Voll, true).zaehlerAb(FsSeverity::Warnung));
    }

    Pc1715Machine m;
    ASSERT_TRUE(m.mountDisk(0, pfad.get(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    EXPECT_EQ(zeile(m, 0), " CP/A, Version 24.05.88, TPA 100H - 0C205H");
}

/// @test Ein A5120-Bootabbild (SYL-Lader) wird für eine 1715-Diskette abgewiesen — VOR dem
///       Anlegen (keine halbe Diskette) und beim Einspielen (Diskette unverändert).
TEST(DisktoolPc1715, FremdesBootabbildWirdAbgewiesen) {
    // Ein A5120-Ladesektor: "SYL" am Anfang, nicht F003H.
    std::vector<uint8_t> syl(1024, 0xE5);
    syl[0] = 0x18; syl[1] = 0x03; syl[2] = 'S'; syl[3] = 'Y'; syl[4] = 'L';
    TempPfad sylDatei("k1520_pc1715_syl.bin");
    schreibe(sylDatei.get(), syl);

    for (const char* profil : {"cpa1715", "scp1715"}) {
        TempPfad pfad("k1520_pc1715_syl.hfe");
        std::string err;
        auto neu = DiskVolume::create(pfad.get(), profil, "", formate(), dateisysteme(), err,
                                      sylDatei.get());
        EXPECT_EQ(neu, nullptr) << profil;
        EXPECT_NE(err.find("PC-1715-Bootkopf"), std::string::npos) << profil << ": " << err;
        EXPECT_FALSE(fs::exists(pfad.get())) << profil << ": halbe Diskette liegt herum";
    }

    // boot-put auf eine vorhandene SCP-Diskette: das Profil ist `cpa_auto` (kein boot_header),
    // aber Sektor 1 trägt schon F003H — das genügt.
    k1520test::TempDisk d("pc1715_scp1715_v0006_boot.hfe", "k1520_pc1715_syl_put.hfe");
    auto dv = oeffne(d.path(), true);
    ASSERT_NE(dv, nullptr);
    dv->setBackup(false);
    std::vector<uint8_t> alt;
    ASSERT_TRUE(dv->readBootImage(alt));
    EXPECT_FALSE(dv->writeBootImage(syl));
    EXPECT_NE(dv->lastError().find("PC-1715-Bootkopf"), std::string::npos) << dv->lastError();
    std::vector<uint8_t> danach;
    ASSERT_TRUE(dv->readBootImage(danach));
    EXPECT_EQ(alt, danach);
}

/**
 * @test SCP 1715: Systemspuren (20 KB) per `boot-get` herausholen, `create --fs scp1715 --boot`,
 *       alle 9 Dateien zurück — bootet bis `A>` mit dem Banner des Originals.
 */
TEST(DisktoolPc1715, GebauteScp1715BootdisketteBootetBisPrompt) {
    stumm();
    k1520test::TempDisk quelle("pc1715_scp1715_v0006_boot.hfe", "k1520_pc1715_scp_quelle.hfe");
    auto q = oeffne(quelle.path());
    ASSERT_NE(q, nullptr);
    std::vector<uint8_t> band;
    ASSERT_TRUE(q->readBootImage(band));
    ASSERT_EQ(band.size(), 20480u);
    EXPECT_EQ(band[0], 0x03);
    EXPECT_EQ(band[1], 0xF0);
    TempPfad bandDatei("k1520_pc1715_band.bin");
    schreibe(bandDatei.get(), band);

    TempPfad pfad("k1520_pc1715_scp_neu.hfe");
    std::string err;
    auto neu = DiskVolume::create(pfad.get(), "scp1715", "", formate(), dateisysteme(), err,
                                  bandDatei.get());
    ASSERT_NE(neu, nullptr) << err;
    kopiereAlle(*q, *neu);
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();

    {
        auto dv = oeffne(pfad.get());
        ASSERT_NE(dv, nullptr);
        EXPECT_EQ(dv->list().size(), 9u);
        EXPECT_EQ(0, dv->check(FsCheckLevel::Voll, true).zaehlerAb(FsSeverity::Warnung));
    }

    Pc1715Machine m;
    ASSERT_TRUE(m.mountDisk(0, pfad.get(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 40'000'000)) << bild(m);
    EXPECT_EQ(zeile(m, 2), "SCP   VERS. 0006   -   03/08/87   -   48 KB");
}

/**
 * @test SCP 3.0 (1715W): derselbe Mechanismus — Systemspuren übernehmen, 42 Dateien zurück,
 *       bootet in der 1715W-Variante bis `A>` (nach PROFILE.SUB/MODCS).  CP/M-3-Verzeichnis:
 *       Zeitstempel-/Kennwort-/Label-Sätze erscheinen weder als Dateien noch als Befund und
 *       werden von `put` nicht überschrieben (synthetische Sätze in einer Kopie, siehe unten).
 */
TEST(DisktoolPc1715, GebauteScp30BootdisketteBootetBisPrompt) {
    stumm();
    k1520test::TempDisk quelle("pc1715w_scp30_system.hfe", "k1520_pc1715_scp30_quelle.hfe");
    auto q = oeffne(quelle.path());
    ASSERT_NE(q, nullptr);
    std::vector<uint8_t> band;
    ASSERT_TRUE(q->readBootImage(band));
    ASSERT_EQ(band.size(), 20480u);
    TempPfad bandDatei("k1520_pc1715_band30.bin");
    schreibe(bandDatei.get(), band);

    TempPfad pfad("k1520_pc1715_scp30_neu.hfe");
    std::string err;
    auto neu = DiskVolume::create(pfad.get(), "scp1715", "", formate(), dateisysteme(), err,
                                  bandDatei.get());
    ASSERT_NE(neu, nullptr) << err;
    kopiereAlle(*q, *neu);
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();

    Pc1715Machine::Config c;
    c.variante = Pc1715Machine::Config::Variante::Pc1715W;
    Pc1715Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, pfad.get(), m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    ASSERT_TRUE(laufeBisText(m, " SCP 3.0  (R-BWS)  V0003  -  28/03/89", 200'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, "A>", 100'000'000)) << bild(m);
}

/**
 * @test CP/M-3-Sonderplätze (Zeitstempel 21H, Label 20H, Kennwort 16…31) in SCP-3.0-Verzeichnis:
 *       nicht als Dateien, `get`/`put`/`rm` funktionieren, die Sonderplätze bleiben bitgleich,
 *       `fsck` ohne Befund ab `Warnung`.  Die echte Fixture trägt keine (nicht mit INITDIR
 *       initialisiert) — deshalb werden vier Sätze in eine Kopie geschrieben (Platz 3 jeder
 *       4er-Gruppe, wie INITDIR es tut).
 */
TEST(DisktoolPc1715, Scp30SonderplaetzeSindWederDateienNochBefund) {
    k1520test::TempDisk d("pc1715w_scp30_system.hfe", "k1520_pc1715_scp30_sonder.hfe");
    std::vector<uint8_t> verzeichnis0;
    int dateien = 0;
    {
        auto dv = oeffne(d.path(), true);
        ASSERT_NE(dv, nullptr);
        dv->setBackup(false);
        dateien = static_cast<int>(dv->list().size());
        // Sonderplätze in vier FREIE Plätze am Ende des 128er-Verzeichnisses (c2h0, 4 Sektoren
        // à 32 Plätze): 123 und 127 Zeitstempel (vierter Platz einer Gruppe, wie INITDIR),
        // 124 Etikett, 125 Kennwortsatz (16 + Nutzerbereich 0) zu PIP.COM; 126 bleibt frei.
        std::vector<uint8_t> s;
        uint16_t crc = 0;
        ASSERT_TRUE(dv->readSectorAt(2, 0, 3, s, crc)) << dv->lastError();
        ASSERT_EQ(s.size(), 1024u);
        auto setze = [&](int slot, uint8_t user) {
            uint8_t* p = s.data() + static_cast<size_t>(slot) * 32;
            ASSERT_EQ(p[0], 0xE5) << "Platz " << (96 + slot) << " ist nicht frei";
            std::fill(p, p + 32, uint8_t{0});
            p[0] = user;
            if (user == 0x20) std::copy_n("SCP30LBL   ", 11, p + 1);
            if (user == 0x10) std::copy_n("PIP     COM", 11, p + 1);   // Kennwortsatz zu PIP.COM
        };
        setze(27, 0x21);
        setze(28, 0x20);
        setze(29, 0x10);
        setze(31, 0x21);
        ASSERT_TRUE(dv->writeSectorAt(2, 0, 3, s, nullptr)) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }

    // Das rohe Verzeichnis: die vier Sektoren von c2h0.
    auto roh = [&](DiskVolume& dv) {
        std::vector<uint8_t> v;
        for (int i = 0; i < 4; ++i) {
            std::vector<uint8_t> s;
            uint16_t crc = 0;
            EXPECT_TRUE(dv.readSectorAt(2, 0, i, s, crc)) << dv.lastError();
            v.insert(v.end(), s.begin(), s.end());
        }
        return v;
    };
    {
        auto dv = oeffne(d.path(), true);
        ASSERT_NE(dv, nullptr);
        dv->setBackup(false);
        EXPECT_EQ(static_cast<int>(dv->list().size()), dateien) << "Sonderplätze erscheinen als Dateien";
        verzeichnis0 = roh(*dv);
        ASSERT_TRUE(lege(*dv, "NEU.TXT", std::vector<uint8_t>(3000, 'z'))) << dv->lastError();
        FileRef weg;
        weg.name = "TYPE.COM";
        ASSERT_TRUE(dv->erase(weg)) << dv->lastError();
        ASSERT_TRUE(dv->flush()) << dv->lastError();
    }
    auto dv = oeffne(d.path());
    ASSERT_NE(dv, nullptr);
    EXPECT_EQ(static_cast<int>(dv->list().size()), dateien);   // +1 NEU, −1 TYPE
    {
        const std::vector<uint8_t> neu = holeDatei(*dv, "NEU.TXT");
        ASSERT_EQ(neu.size(), 3072u);
        EXPECT_TRUE(std::all_of(neu.begin(), neu.begin() + 3000, [](uint8_t x) { return x == 'z'; }));
    }
    const std::vector<uint8_t> nachher = roh(*dv);
    for (int slot : {123, 124, 125, 127})
        EXPECT_TRUE(std::equal(verzeichnis0.begin() + slot * 32, verzeichnis0.begin() + slot * 32 + 32,
                               nachher.begin() + slot * 32))
            << "Sonderplatz " << slot << " wurde verändert";
    for (FsCheckLevel stufe : {FsCheckLevel::Schnell, FsCheckLevel::Voll}) {
        const FsCheckReport& r = dv->check(stufe, true);
        EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
    }
}
