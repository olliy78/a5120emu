/**
 * @file test_disktool_prg710.cpp
 * @brief DiskTool: bootfähige Disketten für PRG 710 / 710-1 (doc/design/20_prg710.md AP-P6).
 *
 * Maßstab ist wie bei `DiskToolBootdiskette.*` allein, ob die Maschine von der gebauten
 * Diskette startet — hier der PRG, beide Varianten:
 *  - UDOS: Bootabbild + Dateien aus einer Gerätediskette (`get`/`boot-get`), `create --boot`
 *    + `insertAll`; Kaltstart bis zum `%`.
 *  - SCPX: Systemspuren aus den **Modulen** (`SYL17` + `CCPBD17` + BIOS, `prg_boot::scpxBand`
 *    = `k1520disktool boot-scpx`); Kaltstart bis `A>`.
 * Dazu: die Fassung eines Bootabbilds (710 ↔ 710-1) wird erkannt und eine falsche beim
 * Einspielen abgewiesen; A5120-Abbilder bleiben unberührt.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/prg_boot.h"
#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/temp_path.h"
#include "tests/system/prg710_bedienung.h"

namespace fs = std::filesystem;
using namespace prg710test;
using prg_boot::System;
using PV = prg_boot::Variante;

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

/// Systemspuren (Byteband) einer Diskette.
std::vector<uint8_t> band(const std::string& abbild, const char* fs_name) {
    std::string err;
    auto dv = DiskVolume::open(abbild, fs_name, formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    std::vector<uint8_t> b;
    if (dv) EXPECT_TRUE(dv->readBootImage(b)) << dv->lastError();
    return b;
}

/// Eine Datei der Diskette in ein Byte-Feld holen.
std::vector<uint8_t> holeDatei(DiskVolume& dv, const std::string& name) {
    TempPfad ziel("k1520_prgboot_datei.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv.extract(ref, ziel.get(), TransferOptions{})) << dv.lastError();
    return datei(ziel.get());
}

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

/// Die Dateien @p namen (Seite @p seite, bei UDOS mit .fileinfo) von @p quelle in @p ziel kopieren.
void kopiere(DiskVolume& quelle, DiskVolume& ziel, int seite, const std::vector<std::string>& namen,
             const char* tmp) {
    TempOrdner o(tmp);
    for (const std::string& n : namen) {
        FileRef ref;
        ref.volume = seite;
        ref.name = n;
        const std::string pfad = (fs::path(o.path()) / n).string();
        ASSERT_TRUE(quelle.extract(ref, pfad, TransferOptions{})) << n << ": " << quelle.lastError();
        ASSERT_TRUE(ziel.insert(pfad, ref, TransferOptions{})) << n << ": " << ziel.lastError();
    }
}

PV prgVariante(V v) { return v == V::Prg710_1 ? PV::Prg710_1 : PV::Prg710; }

std::unique_ptr<Prg710Machine> maschine(V v) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    Prg710Machine::Config c;
    c.variante = v;
    return std::make_unique<Prg710Machine>(c);
}

}  // namespace

class DisktoolPrg710 : public ::testing::TestWithParam<V> {};

/**
 * @test DisktoolPrg710.ErkenntSystemUndFassungAnDenGeraetedisketten
 * @brief Alle sechs Systemdisketten des PRG (UDOS und SCPX, je beide Geräte) werden an
 *        ihrem Bootabbild richtig eingeordnet; A5120-Abbilder sind kein PRG.
 */
TEST(DisktoolPrg710, ErkenntSystemUndFassungAnDenGeraetedisketten) {
    struct Fall { const char* disk; const char* fs; System sys; PV soll; };
    const Fall faelle[] = {
        {"prg710_udos43_k5601_mrs_boot.hfe",    "udos_ds77", System::Udos, PV::Prg710},
        {"prg710_udos43_k5601_boot01.hfe",      "udos_ds77", System::Udos, PV::Prg710},
        {"prg710-1_udos43_k5601_v43_189.hfe",   "udos_ds77", System::Udos, PV::Prg710_1},
        {"prg710-1_udos_k5601_boot.hfe",        "udos_ds77", System::Udos, PV::Prg710_1},
        {"prg710_scpx15_cpa640_sysprg.hfe",     "scpx640",   System::Scpx, PV::Prg710},
        {"prg710-1_scpx17_cpa640_boot.hfe",     "scpx640",   System::Scpx, PV::Prg710_1},
    };
    for (const Fall& f : faelle) {
        k1520test::TempDisk d(f.disk);
        const prg_boot::Kennung k = prg_boot::erkenne(band(d, f.fs), f.sys);
        EXPECT_TRUE(k.ist_prg) << f.disk;
        EXPECT_EQ(k.variante, f.soll) << f.disk << ": " << prg_boot::name(k.variante);
    }
    // A5120: UDOS-Lader und SCPX-Lader tragen den PRG-Ladesektor nicht.
    {
        k1520test::TempDisk d("udos_boot_scp.hfe");
        EXPECT_FALSE(prg_boot::erkenne(band(d, "udos_ds77"), System::Udos).ist_prg);
    }
    {
        k1520test::TempDisk d("scpx17_cpa780_k5601.hfe");
        EXPECT_FALSE(prg_boot::erkenne(band(d, "scpx640"), System::Scpx).ist_prg);
    }
}

/**
 * @test DisktoolPrg710.ScpxBandAusDenModulenGleichtDemGeraetband
 * @brief `SYL17` + `CCPBD17` + BIOS ergeben bytegleich die Systemspuren der Gerätedisketten
 *        (bis zum Ende des BIOS; dahinter bleibt Leerspur).  Das Gerät wird am BIOS
 *        abgelesen: V1.5 (`B15x`) und V1.7 mit K7672 (`B17x72`) in allen Ausstattungen;
 *        ein BIOS der anderen Fassung als verlangt wird abgewiesen.
 */
TEST(DisktoolPrg710, ScpxBandAusDenModulenGleichtDemGeraetband) {
    k1520test::TempDisk quelle("prg710-1_scpx17_cpa640_boot.hfe");
    std::string err;
    auto dv = DiskVolume::open(quelle, "scpx640", formate(), dateisysteme(), err);
    ASSERT_NE(dv, nullptr) << err;
    const auto syl = holeDatei(*dv, "SYL17.SYS");
    const auto ccp = holeDatei(*dv, "CCPBD17.SYS");
    ASSERT_EQ(syl.size(), 256u);
    ASSERT_EQ(ccp.size(), 5632u);

    const std::vector<uint8_t> soll710_1 = band(quelle, "scpx640");
    k1520test::TempDisk d710("prg710_scpx15_cpa640_sysprg.hfe");
    const std::vector<uint8_t> soll710 = band(d710, "scpx640");

    struct Bios { const char* name; PV geraet; };
    const Bios bios[] = {
        {"B151V24.SYS", PV::Prg710},   {"B152V24.SYS", PV::Prg710},   {"B152IFSS.SYS", PV::Prg710},
        {"B17172V2.SYS", PV::Prg710_1}, {"B17172ZI.SYS", PV::Prg710_1},
        {"B17172FS.SYS", PV::Prg710_1}, {"B17172SD.SYS", PV::Prg710_1},
        {"B17272V2.SYS", PV::Prg710_1}, {"B17272ZI.SYS", PV::Prg710_1},
        {"B17272FS.SYS", PV::Prg710_1}, {"B17272SD.SYS", PV::Prg710_1},
    };
    for (const Bios& b : bios) {
        const auto inhalt = holeDatei(*dv, b.name);
        std::vector<uint8_t> aus;
        PV erkannt = PV::Unbekannt;
        ASSERT_TRUE(prg_boot::scpxBand(syl, ccp, inhalt, PV::Unbekannt, aus, erkannt, err))
            << b.name << ": " << err;
        EXPECT_EQ(erkannt, b.geraet) << b.name;
        EXPECT_EQ(aus.size() % 256, 0u);
        // Die Gerätedisketten tragen B152V24 bzw. B17272V2.
        const std::string n = b.name;
        if (n == "B152V24.SYS" || n == "B17272V2.SYS") {
            const auto& soll = (n == "B152V24.SYS") ? soll710 : soll710_1;
            ASSERT_LE(aus.size(), soll.size());
            EXPECT_TRUE(std::equal(aus.begin(), aus.end(), soll.begin())) << n;
        }
        // Falsche Fassung verlangt → abgewiesen, ohne etwas zu bauen.
        const PV falsch = (b.geraet == PV::Prg710) ? PV::Prg710_1 : PV::Prg710;
        std::vector<uint8_t> nichts;
        EXPECT_FALSE(prg_boot::scpxBand(syl, ccp, inhalt, falsch, nichts, erkannt, err)) << n;
        EXPECT_NE(err.find("gebaut"), std::string::npos) << err;
        EXPECT_TRUE(nichts.empty());
    }
    // Falsche Module werden vor dem Zusammensetzen abgewiesen.
    std::vector<uint8_t> aus;
    PV erkannt = PV::Unbekannt;
    EXPECT_FALSE(prg_boot::scpxBand(ccp, ccp, holeDatei(*dv, "B152V24.SYS"), PV::Unbekannt,
                                    aus, erkannt, err));              // Lader ist CCPBD17
    EXPECT_FALSE(prg_boot::scpxBand(syl, syl, holeDatei(*dv, "B152V24.SYS"), PV::Unbekannt,
                                    aus, erkannt, err));              // CCP zu kurz
    EXPECT_FALSE(prg_boot::scpxBand(syl, ccp, syl, PV::Unbekannt, aus, erkannt, err));  // BIOS
}

/**
 * @test DisktoolPrg710.UdosBootdisketteBootetBisZumPrompt
 * @brief Boot­abbild + Dateien einer Gerätediskette → `create --boot` → `insert` (OS, ZDOS, OS.INIT, DO, DATE, CAT) → die
 *        neue Diskette startet am selben Gerät bis Datumsabfrage und `%` (Systemzeile der
 *        Variante), `CAT` listet.  Beide Seiten sind formatiert (AP-P5f, Befund 4).
 */
TEST_P(DisktoolPrg710, UdosBootdisketteBootetBisZumPrompt) {
    const V v = GetParam();
    k1520test::TempDisk quelle(udosDiskette(v));
    std::string err;
    auto q = DiskVolume::open(quelle, "udos_ds77", formate(), dateisysteme(), err);
    ASSERT_NE(q, nullptr) << err;
    const std::string bootbin = k1520test::tempPath("k1520_prgboot_udos.bin");
    ASSERT_TRUE(q->readBootImageToFile(bootbin)) << q->lastError();
    TempPfad ziel("k1520_prgboot_udos.hfe");
    auto neu = DiskVolume::create(ziel.get(), "udos_ds77", "UDOS.PRG", formate(),
                                  dateisysteme(), err, bootbin);
    std::error_code ec;
    fs::remove(bootbin, ec);
    ASSERT_NE(neu, nullptr) << err;
    // Mindestbestand (AP-P5f): OS, ZDOS, OS.INIT, DO, DATE auf Seite 0 — dazu CAT.
    ASSERT_NO_FATAL_FAILURE(kopiere(*q, *neu, 0, {"OS", "ZDOS", "OS.INIT", "DO", "DATE", "CAT"},
                                    "k1520_prgboot_udos_dateien"));
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();
    q.reset();

    auto m = maschine(v);
    ASSERT_TRUE(m->mountDisk(0, ziel.get(), m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(udosBisDatum(*m)) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
    tippe(*m, "021086");
    ASSERT_TRUE(bisUdosPrompt(*m, kBefehl)) << bild(*m);
    EXPECT_TRUE(enthaelt(*m, udosSystemzeile(v))) << bild(*m);
    ASSERT_TRUE(udos(*m, "CAT D=0 P=& *DOS")) << bild(*m);
    EXPECT_TRUE(enthaelt(*m, "ZDOS                 0")) << bild(*m);
}

/**
 * @test DisktoolPrg710.ScpxBootdisketteAusModulenBootetBisA
 * @brief Systemspuren aus den Modulen (`boot-scpx`), `create --fs scpx640 --boot`, die
 *        Programme der Gerätediskette dazu → Kaltstart bis `A>` mit dem Gruß des BIOS
 *        (710: V1.5, 710-1: V1.7), `DIR` listet, `STAT` wird von der Diskette geladen.
 */
TEST_P(DisktoolPrg710, ScpxBootdisketteAusModulenBootetBisA) {
    const V v = GetParam();
    k1520test::TempDisk quelle("prg710-1_scpx17_cpa640_boot.hfe");   // trägt alle Module
    std::string err;
    auto q = DiskVolume::open(quelle, "scpx640", formate(), dateisysteme(), err);
    ASSERT_NE(q, nullptr) << err;
    const auto syl = holeDatei(*q, "SYL17.SYS");
    const auto ccp = holeDatei(*q, "CCPBD17.SYS");
    const auto bios = holeDatei(*q, v == V::Prg710 ? "B152V24.SYS" : "B17272V2.SYS");

    std::vector<uint8_t> systemspuren;
    PV erkannt = PV::Unbekannt;
    ASSERT_TRUE(prg_boot::scpxBand(syl, ccp, bios, prgVariante(v), systemspuren, erkannt, err))
        << err;
    const std::string bootbin = k1520test::tempPath("k1520_prgboot_scpx.bin");
    {
        std::ofstream f(bootbin, std::ios::binary);
        f.write(reinterpret_cast<const char*>(systemspuren.data()),
                static_cast<std::streamsize>(systemspuren.size()));
    }
    TempPfad ziel("k1520_prgboot_scpx.hfe");
    auto neu = DiskVolume::create(ziel.get(), "scpx640", "", formate(), dateisysteme(), err,
                                  bootbin);
    std::error_code ec;
    fs::remove(bootbin, ec);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_NO_FATAL_FAILURE(kopiere(*q, *neu, 0, {"STAT.COM", "PIP.COM", "FORMAT.COM"},
                                    "k1520_prgboot_scpx_dateien"));
    ASSERT_TRUE(neu->flush()) << neu->lastError();
    neu.reset();
    q.reset();

    auto m = maschine(v);
    ASSERT_TRUE(m->mountDisk(0, ziel.get(), m->defaultFormatName(0), false)) << m->lastError();
    m->powerOn();
    lauf(*m, 3'000'000);
    taste(*m, QK_RETURN);
    ASSERT_TRUE(bisText(*m, scpxGruss(v), kBoot)) << bild(*m);
    ASSERT_TRUE(bisScpxPrompt(*m, kBefehl)) << bild(*m);
    ASSERT_TRUE(scpx(*m, "DIR")) << bild(*m);
    EXPECT_TRUE(enthaelt(*m, "STAT")) << bild(*m);
    tippe(*m, "STAT");
    taste(*m, QK_RETURN);
    EXPECT_TRUE(bisText(*m, "Space:", kBefehl)) << "STAT.COM lud nicht:\n" << bild(*m);
}

/**
 * @test DisktoolPrg710.LieferdisketteBootet
 * @brief AP-P5h: die vier PRG-Systemdisketten, die ins Paket kommen (`disks/`, Liste
 *        `DISKS_DEFAULT` in `packaging/build_payload.sh`), starten je am passenden Gerät bis
 *        `%` bzw. `A>`.  Gemountet wird eine TEMP-KOPIE (der Emulator öffnet r/w).
 */
TEST(DisktoolPrg710, LieferdisketteBootet) {
    struct Fall { V v; bool udos; const char* disk; };
    const Fall faelle[] = {
        {V::Prg710,   true,  "prg710_udos43_k5601_system.hfe"},
        {V::Prg710_1, true,  "prg710-1_udos43_k5601_v43_189.hfe"},
        {V::Prg710,   false, "prg710_scpx15_cpa640_sysprg.hfe"},
        {V::Prg710_1, false, "prg710-1_scpx17_cpa640_boot.hfe"},
    };
    for (const Fall& f : faelle) {
        SCOPED_TRACE(f.disk);
        const fs::path quelle = fs::path(PRG710_LIEFERDISKETTEN) / f.disk;
        ASSERT_TRUE(fs::is_regular_file(quelle)) << quelle;
        TempPfad kopie("k1520_prg_lieferdiskette.hfe");
        fs::copy_file(quelle, kopie.get(), fs::copy_options::overwrite_existing);
        auto m = maschine(f.v);
        ASSERT_TRUE(m->mountDisk(0, kopie.get(), m->defaultFormatName(0), false)) << m->lastError();
        if (f.udos) {
            ASSERT_TRUE(udosBisDatum(*m)) << bild(*m);
            tippe(*m, "021086");
            ASSERT_TRUE(bisUdosPrompt(*m, kBefehl)) << bild(*m);
            EXPECT_TRUE(enthaelt(*m, udosSystemzeile(f.v))) << bild(*m);
        } else {
            m->powerOn();
            lauf(*m, 3'000'000);
            taste(*m, QK_RETURN);
            ASSERT_TRUE(bisText(*m, scpxGruss(f.v), kBoot)) << bild(*m);
            ASSERT_TRUE(bisScpxPrompt(*m, kBefehl)) << bild(*m);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Varianten, DisktoolPrg710, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return name(i.param); });

/**
 * @test DisktoolPrg710.FremdeFassungWirdBeimEinspielenAbgewiesen
 * @brief Das Bootabbild des 710 auf eine Diskette, die schon ein 710-1-System trägt (und
 *        umgekehrt), wird vor dem Schreiben abgewiesen — die Diskette bleibt unverändert.
 *        Dasselbe für UDOS.  Das Abbild derselben Fassung geht durch.
 */
TEST(DisktoolPrg710, FremdeFassungWirdBeimEinspielenAbgewiesen) {
    struct Paar { const char* ziel; const char* fremd; const char* fs; const char* gleich; };
    const Paar paare[] = {
        {"prg710-1_scpx17_cpa640_boot.hfe", "prg710_scpx15_cpa640_sysprg.hfe", "scpx640",
         "prg710-1_scpx17_cpa640_boot.hfe"},
        {"prg710-1_udos43_k5601_v43_189.hfe", "prg710_udos43_k5601_mrs_boot.hfe", "udos_ds77",
         "prg710-1_udos_k5601_boot.hfe"},
        {"prg710_udos43_k5601_mrs_boot.hfe", "prg710-1_udos43_k5601_v43_189.hfe", "udos_ds77",
         "prg710_udos43_k5601_boot01.hfe"},
    };
    for (const Paar& p : paare) {
        k1520test::TempDisk ziel(p.ziel), fremd(p.fremd), gleich(p.gleich);
        const auto vorher = band(ziel, p.fs);
        const auto fremdband = band(fremd, p.fs);
        const auto gleichband = band(gleich, p.fs);

        std::string err;
        auto dv = DiskVolume::open(ziel, p.fs, formate(), dateisysteme(), err,
                                   /*read_only=*/false);
        ASSERT_NE(dv, nullptr) << err;
        EXPECT_FALSE(dv->writeBootImage(fremdband)) << p.ziel << " <- " << p.fremd;
        EXPECT_NE(dv->lastError().find("PRG 710"), std::string::npos) << dv->lastError();
        std::vector<uint8_t> danach;
        ASSERT_TRUE(dv->readBootImage(danach));
        EXPECT_EQ(danach, vorher) << "die Systemspuren blieben nicht unberührt";
        EXPECT_TRUE(dv->writeBootImage(gleichband)) << dv->lastError();
    }
}

/**
 * @test DisktoolPrg710.UnvollstaendigesPrgAbbildLegtKeineDisketteAn
 * @brief Ein Abbild mit PRG-Lader, aber ohne BIOS (SCPX) bzw. ohne Zweitlader (UDOS) wird
 *        beim Anlegen abgewiesen, bevor eine Datei entsteht.  Ein A5120-Abbild
 *        (`disks/boot_scpx640.bin`) geht durch — dort ändert sich nichts.
 */
TEST(DisktoolPrg710, UnvollstaendigesPrgAbbildLegtKeineDisketteAn) {
    struct Fall { const char* disk; const char* fs; size_t laenge; };
    const Fall faelle[] = {
        {"prg710_scpx15_cpa640_sysprg.hfe", "scpx640", 5000},
        {"prg710_udos43_k5601_mrs_boot.hfe", "udos_ds77", 5000},
    };
    for (const Fall& f : faelle) {
        k1520test::TempDisk d(f.disk);
        auto b = band(d, f.fs);
        b.resize(f.laenge);
        const std::string bootbin = k1520test::tempPath("k1520_prgboot_kurz.bin");
        {
            std::ofstream o(bootbin, std::ios::binary);
            o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        TempPfad ziel("k1520_prgboot_kurz.hfe");
        std::string err;
        auto neu = DiskVolume::create(ziel.get(), f.fs, "", formate(), dateisysteme(), err, bootbin);
        std::error_code ec;
        fs::remove(bootbin, ec);
        EXPECT_EQ(neu, nullptr) << f.disk;
        EXPECT_NE(err.find("endet vor"), std::string::npos) << err;
        EXPECT_FALSE(fs::exists(ziel.get())) << "es blieb eine halbe Diskette liegen";
    }
    {
        // A5120-Abbild (SCPX-Systemspuren der A5120-Fixture): keine PRG-Prüfung.
        k1520test::TempDisk d("scpx17_cpa780_k5601.hfe");
        const auto b = band(d, "scpx640");
        const std::string bootbin = k1520test::tempPath("k1520_prgboot_a5120.bin");
        {
            std::ofstream o(bootbin, std::ios::binary);
            o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        TempPfad ziel("k1520_prgboot_a5120.hfe");
        std::string err;
        auto neu = DiskVolume::create(ziel.get(), "scpx640", "", formate(), dateisysteme(),
                                      err, bootbin);
        std::error_code ec;
        fs::remove(bootbin, ec);
        EXPECT_NE(neu, nullptr) << err;
    }
}
