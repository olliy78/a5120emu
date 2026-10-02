/**
 * @file test_prg710_scpx.cpp
 * @brief PRG 710 / 710-1 Etappe 5 (doc/design/20_prg710.md AP-P5e): SCPX 1526 am 710-1
 *        bis `A>`, `DIR`/`STAT`/`PIP` nach B:, und mit `SYSPRG` eine SCPX-Bootdiskette
 *        für den PRG 710.
 *
 * Ladekette (§4a): Starttaste → ROM liest den Bootsektor `SYL17` nach 0400H → `SYL17`
 * lädt die Systemspuren über die ROM-Schnittstelle `03FDH` ab C600H (Spur 0 Seite 0 ab
 * Sektor 1, also `SYL17` selbst nach C600H, CCP+BDOS `CCPBD17` ab C800H, BIOS ab DE00H),
 * schaltet `E8H[F]` und springt nach DE00H.
 *
 * **Befunde aus AP-P5e:**
 * - Am 710-1 läuft `PRG710-1_SCPX_Boot` ohne jede Kernänderung bis `A>` („SCPX 1526 -
 *   V 1.7 (52K)“), ≈ 12 Mio. Takte nach ENTER; `DIR`, `STAT`, `PIP` nach B: laufen.
 * - Die Systemspuren (`scpx640`, 16384 B) sind genau `SYL17.SYS` (Bootsektor, 256 B,
 *   Rest des ersten 512-B-Blocks AAH) + `CCPBD17.SYS` (ab Byte 512) + BIOS (ab Byte 6144,
 *   hier `B17272V2.SYS`), danach AAH.  `SYSPRG` (Bildschirm 2, Tastatur 2, Drucker 3,
 *   Format 0, Laufwerk B) schreibt im Emulator **bytegleich dieselben** Systemspuren.
 * - `SYSPRG` wählt das BIOS nur über den Dateinamen `B17<Bild><Tastatur><Drucker>.SYS`
 *   (Tastatur 09 = K7609/PRG 710, 72 = K7672/PRG 710-1).  Die PRG-710-Module
 *   `B17109xx`/`B17209xx` liegen auf **keiner** vorhandenen Diskette — mit Tastatur 1
 *   meldet `SYSPRG` „DATEI B17209V2.SYS NICHT AUF DISKETTE“.  Für den 710 gibt es nur
 *   die älteren BIOS-Fassungen `B151V24`, `B152V24`, `B152IFSS` („SCPX V 1.5
 *   B. Daehmlow“, gleiche Basis DE00H, gleiche Länge).  Die 710-Diskette
 *   entsteht deshalb mit `SYSPRG` im Emulator, nachdem `B152V24.SYS` unter dem erwarteten
 *   Namen `B17209V2.SYS` bereitliegt — `SYSPRG` setzt nur zusammen, das Ergebnis ist
 *   `SYL17` + `CCPBD17` (V1.7) + `B152V24` (V1.5) und bootet am 710 bis `A>`.
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
#include "core/logger.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {
using V = Prg710Machine::Config::Variante;

constexpr int       kSchritt  = 20'000;
constexpr long long kBoot     = 60'000'000;   // ENTER → `A>`: ≈ 12 Mio. Takte
constexpr long long kBefehl   = 60'000'000;   // je Kommando
constexpr uint32_t  QK_RETURN = 0x01000004;   // Qt::Key_Return (710: ET1 = 37H, 710-1: 0DH)

const char* kDiskette710_1 = "prg710-1_scpx17_cpa640_boot.hfe";
const char* kDiskette710   = "prg710_scpx15_cpa640_sysprg.hfe";
const char* kGruss17       = "SCPX 1526 - V 1.7 (52K)";
const char* kGruss15       = "SCPX  V 1.5  B. Daehmlow";

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
bool bisText(Prg710Machine& m, const std::string& t, long long frist) {
    for (long long d = 0; d < frist;) {
        d += m.run(kSchritt);
        if (bild(m).find(t) != std::string::npos) return true;
    }
    return false;
}
/// Bis die letzte Zeile genau `A>` ist und 2 Mio. Takte so bleibt (Ausgabe fertig).
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

/// Eine Taste drücken und loslassen (die K7672 wiederholt eine gehaltene Taste).
void taste(Prg710Machine& m, uint32_t k) {
    m.keyPress(k, false, false);
    lauf(m, 100'000);
    m.keyRelease(k);
    lauf(m, 100'000);
}
void tippe(Prg710Machine& m, const std::string& s) {
    for (char c : s) taste(m, static_cast<uint8_t>(c));
}
/// Kommando + ET bzw. ENTER, dann bis zum nächsten `A>`.
bool kommando(Prg710Machine& m, const std::string& k) {
    tippe(m, k);
    taste(m, QK_RETURN);
    return bisPrompt(m, kBefehl);
}
/// Eine Antwort an `SYSPRG`, dann bis `frage` im Bild steht.  Die Typnummern verlangen
/// ENTER (auch die Laufwerksfrage).
bool antwort(Prg710Machine& m, char c, const std::string& frage) {
    taste(m, static_cast<uint8_t>(c));
    taste(m, QK_RETURN);
    return bisText(m, frage, kBefehl);
}

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

/// Temporäre Datei (samt Sicherungskopie und Beiblatt des DiskTools), räumt sich weg.
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
        fs::remove(pfad_ + ".fileinfo", ec);
    }
    std::string pfad_;
};

std::vector<uint8_t> datei(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

/// Leere `scpx640`-Diskette (16 × 256, Verzeichnis leer, keine Systemspuren) vom DiskTool.
void leereDiskette(const std::string& pfad) {
    std::string err;
    auto neu = DiskVolume::create(pfad, "scpx640", "", formate(), dateisysteme(), err);
    ASSERT_NE(neu, nullptr) << err;
    ASSERT_TRUE(neu->flush()) << neu->lastError();
}

/// Inhalt einer Datei einer `scpx640`-Diskette (über das DiskTool).
std::vector<uint8_t> scpxDatei(const std::string& abbild, const std::string& name) {
    std::string err;
    auto dv = DiskVolume::open(abbild, "scpx640", formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    if (!dv) return {};
    TempPfad ziel("prg710_scpx_datei.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv->extract(ref, ziel.get(), TransferOptions{})) << dv->lastError();
    return datei(ziel.get());
}

/// Systemspuren einer `scpx640`-Diskette.
std::vector<uint8_t> systemspuren(const std::string& abbild) {
    std::string err;
    auto dv = DiskVolume::open(abbild, "scpx640", formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    std::vector<uint8_t> b;
    if (dv) EXPECT_TRUE(dv->readBootImage(b)) << dv->lastError();
    return b;
}

/// Netz-Ein → Starttaste → Begrüssung → `A>`.
void booteBisPrompt(Prg710Machine& m, const char* gruss) {
    m.powerOn();
    lauf(m, 3'000'000);                              // ROM bis zur Tastaturabfrage
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisPrompt(m, kBoot)) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    EXPECT_NE(bild(m).find(gruss), std::string::npos) << bild(m);
}

std::unique_ptr<Prg710Machine> maschine(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return std::make_unique<Prg710Machine>(c);
}

/// `SYSPRG` mit 2K-Bild, Drucker V.24, 16 × 256 nach B: — Tastatur `tastatur`
/// ('1' = K7609, '2' = K7672) — bis „DISKETTE FERTIG GENERIERT !“, dann Q bis `A>`.
void sysprgNachB(Prg710Machine& m, char tastatur) {
    tippe(m, "SYSPRG");
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "2 = 2K BILDSCHIRM", kBefehl)) << bild(m);
    ASSERT_TRUE(antwort(m, '2', "TASTATUR K7672")) << bild(m);
    ASSERT_TRUE(antwort(m, tastatur, "DRUCKER.V24")) << bild(m);
    ASSERT_TRUE(antwort(m, '3', "5 x 1024 BYTE")) << bild(m);
    ASSERT_TRUE(antwort(m, '0', "LAUFWERK A ODER B")) << bild(m);
    ASSERT_TRUE(antwort(m, 'B', "DISKETTE FERTIG GENERIERT !")) << bild(m);
    taste(m, 'Q');
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << bild(m);
}

}  // namespace

class Prg710Scpx : public ::testing::Test {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test Prg710Scpx.Prg710_1BootetBisZumPrompt
 * @brief `PRG710-1_SCPX_Boot` am 710-1: ENTER → `SYL17` → CCP/BDOS + BIOS `B17272V2`
 *        → „SCPX 1526 - V 1.7 (52K)“ → `A>`.  Seiten 0–E bleiben OPS-RAM (§4b).
 */
TEST_F(Prg710Scpx, Prg710_1BootetBisZumPrompt) {
    k1520test::TempDisk disk(kDiskette710_1);
    auto m = maschine(V::Prg710_1);
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    booteBisPrompt(*m, kGruss17);
    EXPECT_EQ(letzteZeile(*m), "A>");
    EXPECT_EQ(m->speicher().freigabe(), 0x0F);
    for (int n = 0; n < 15; ++n) EXPECT_EQ(m->speicher().attr(n) & 0x0F, 0) << "Seite " << n;
}

/**
 * @test Prg710Scpx.Prg710_1DirStatUndKopieNachB
 * @brief `DIR` listet die Diskette (u. a. `SYSPRG.COM`, die BIOS-Module), `STAT` meldet
 *        146k frei; `PIP B:=STAT.COM` schreibt im `/WAIT`-Betrieb auf eine leere
 *        `scpx640`-Diskette in Laufwerk 1, `DIR B:` und `STAT B:*.*` sehen sie, und das
 *        DiskTool findet die Kopie bytegleich zum Original.
 */
TEST_F(Prg710Scpx, Prg710_1DirStatUndKopieNachB) {
    k1520test::TempDisk disk(kDiskette710_1);
    TempPfad zweit("prg710_scpx_lw1.hfe");
    leereDiskette(zweit.get());
    auto m = maschine(V::Prg710_1);
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(m->mountDisk(1, zweit.get(), m->defaultFormatName(1), false)) << m->lastError();
    booteBisPrompt(*m, kGruss17);

    ASSERT_TRUE(kommando(*m, "DIR")) << bild(*m);
    EXPECT_NE(bild(*m).find("A: SYSPRG   COM"), std::string::npos) << bild(*m);
    EXPECT_NE(bild(*m).find("B152V24  SYS"), std::string::npos) << bild(*m);
    ASSERT_TRUE(kommando(*m, "STAT")) << bild(*m);
    EXPECT_NE(bild(*m).find("A: R/W, Space: 146k"), std::string::npos) << bild(*m);

    ASSERT_TRUE(kommando(*m, "PIP B:=STAT.COM")) << bild(*m);
    ASSERT_TRUE(kommando(*m, "DIR B:")) << bild(*m);
    EXPECT_NE(bild(*m).find("B: STAT     COM"), std::string::npos) << bild(*m);
    ASSERT_TRUE(kommando(*m, "STAT B:*.*")) << bild(*m);
    EXPECT_NE(bild(*m).find("   42     6k    1 R/W B:STAT.COM"), std::string::npos) << bild(*m);

    ASSERT_TRUE(m->flushDisks()) << m->lastError();
    const auto original = scpxDatei(disk, "STAT.COM");
    ASSERT_EQ(original.size(), 5376u);
    EXPECT_EQ(scpxDatei(zweit.get(), "STAT.COM"), original);
}

/**
 * @test Prg710Scpx.SysprgSchreibtDieSystemspurenDes710_1
 * @brief `SYSPRG` (2K-Bild, K7672, Drucker V.24, 16 × 256, Laufwerk B) im Emulator:
 *        „DISKETTE FERTIG GENERIERT !“, und die Systemspuren der leeren Diskette in B:
 *        sind danach **bytegleich** denen der Gerätediskette — `SYSPRG` setzt nur
 *        `SYL17` + `CCPBD17` + `B17272V2` zusammen.
 */
TEST_F(Prg710Scpx, SysprgSchreibtDieSystemspurenDes710_1) {
    k1520test::TempDisk disk(kDiskette710_1);
    TempPfad zweit("prg710_scpx_sysprg.hfe");
    leereDiskette(zweit.get());
    auto m = maschine(V::Prg710_1);
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(m->mountDisk(1, zweit.get(), m->defaultFormatName(1), false)) << m->lastError();
    booteBisPrompt(*m, kGruss17);
    sysprgNachB(*m, '2');
    ASSERT_TRUE(m->flushDisks()) << m->lastError();
    const auto soll = systemspuren(disk);
    ASSERT_EQ(soll.size(), 16384u);
    EXPECT_EQ(systemspuren(zweit.get()), soll);
}

/**
 * @test Prg710Scpx.SysprgOhnePrg710ModulMeldetFehlendeDatei
 * @brief Mit Tastatur 1 (K7609, PRG 710) sucht `SYSPRG` das BIOS `B17209V2.SYS` —
 *        das liegt auf keiner vorhandenen Diskette (Dateikopf).  `SYSPRG` meldet es
 *        und kehrt zum `A>` zurück; deshalb geht die 710-Diskette den Umweg über
 *        `B152V24.SYS` (`SysprgBautBootdisketteFuerDen710`).
 */
TEST_F(Prg710Scpx, SysprgOhnePrg710ModulMeldetFehlendeDatei) {
    k1520test::TempDisk disk(kDiskette710_1);
    auto m = maschine(V::Prg710_1);
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    booteBisPrompt(*m, kGruss17);
    tippe(*m, "SYSPRG");
    taste(*m, QK_RETURN);
    ASSERT_TRUE(bisText(*m, "2 = 2K BILDSCHIRM", kBefehl)) << bild(*m);
    ASSERT_TRUE(antwort(*m, '2', "TASTATUR K7609")) << bild(*m);
    ASSERT_TRUE(antwort(*m, '1', "DRUCKER.V24")) << bild(*m);
    ASSERT_TRUE(antwort(*m, '3', "NICHT AUF DISKETTE")) << bild(*m);
    EXPECT_NE(bild(*m).find("DATEI B17209V2.SYS NICHT AUF DISKETTE"), std::string::npos)
        << bild(*m);
    EXPECT_TRUE(bisPrompt(*m, kBefehl)) << bild(*m);
}

/**
 * @test Prg710Scpx.SysprgBautBootdisketteFuerDen710
 * @brief Am 710-1: `PIP A:B17209V2.SYS=A:B152V24.SYS` (das fehlende PRG-710-Modul unter
 *        dem Namen, den `SYSPRG` sucht), `SYSPRG` mit Tastatur 1 (K7609) nach B:, dann
 *        `PIP` und `STAT` auf die neue Diskette.  Deren Systemspuren sind `SYL17` +
 *        `CCPBD17` + `B152V24` (an Byte 0 / 512 / 6144).  Am **PRG 710** bootet sie bis
 *        `A>` („SCPX  V 1.5  B. Daehmlow“), `DIR` sieht die Dateien, und `PIP B:=STAT.COM`
 *        kopiert auf eine leere Diskette in Laufwerk 1 — bytegleich.
 */
TEST_F(Prg710Scpx, SysprgBautBootdisketteFuerDen710) {
    k1520test::TempDisk disk(kDiskette710_1);
    TempPfad neu("prg710_scpx_neu710.hfe");
    leereDiskette(neu.get());
    {
        auto m = maschine(V::Prg710_1);
        ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
        ASSERT_TRUE(m->mountDisk(1, neu.get(), m->defaultFormatName(1), false)) << m->lastError();
        booteBisPrompt(*m, kGruss17);
        ASSERT_TRUE(kommando(*m, "PIP A:B17209V2.SYS=A:B152V24.SYS")) << bild(*m);
        sysprgNachB(*m, '1');
        ASSERT_TRUE(kommando(*m, "PIP B:=PIP.COM")) << bild(*m);
        ASSERT_TRUE(kommando(*m, "PIP B:=STAT.COM")) << bild(*m);
        ASSERT_TRUE(m->flushDisks()) << m->lastError();
    }

    // Systemspuren = Module der Diskette, so wie SYSPRG sie zusammensetzt.
    const auto band = systemspuren(neu.get());
    ASSERT_EQ(band.size(), 16384u);
    const auto syl  = scpxDatei(disk, "SYL17.SYS");
    const auto ccp  = scpxDatei(disk, "CCPBD17.SYS");
    const auto bios = scpxDatei(disk, "B152V24.SYS");
    ASSERT_EQ(syl.size(), 256u);
    ASSERT_EQ(ccp.size(), 5632u);
    ASSERT_EQ(bios.size(), 3328u);
    EXPECT_TRUE(std::equal(syl.begin(), syl.end(), band.begin()));
    EXPECT_TRUE(std::equal(ccp.begin(), ccp.end(), band.begin() + 512));
    EXPECT_TRUE(std::equal(bios.begin(), bios.end(), band.begin() + 6144));

    // Am PRG 710 (8279 + K7609, Marken-FF low-aktiv).  Die K7609 liefert unter SCPX
    // Kleinbuchstaben (AP-P2a); der CCP setzt um.
    TempPfad zweit("prg710_scpx_lw1_710.hfe");
    leereDiskette(zweit.get());
    auto m = maschine(V::Prg710);
    ASSERT_TRUE(m->mountDisk(0, neu.get(), m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(m->mountDisk(1, zweit.get(), m->defaultFormatName(1), false)) << m->lastError();
    booteBisPrompt(*m, kGruss15);
    ASSERT_TRUE(kommando(*m, "dir")) << bild(*m);
    EXPECT_NE(bild(*m).find("A: PIP      COM : STAT     COM"), std::string::npos) << bild(*m);
    ASSERT_TRUE(kommando(*m, "pip b:=stat.com")) << bild(*m);
    ASSERT_TRUE(kommando(*m, "dir b:")) << bild(*m);
    EXPECT_NE(bild(*m).find("B: STAT     COM"), std::string::npos) << bild(*m);
    ASSERT_TRUE(m->flushDisks()) << m->lastError();
    EXPECT_EQ(scpxDatei(zweit.get(), "STAT.COM"), scpxDatei(disk, "STAT.COM"));
}

/**
 * @test Prg710Scpx.Prg710BootetVonDerSysprgDiskette
 * @brief Die eingecheckte 710-Diskette (Systemspuren wie in
 *        `SysprgBautBootdisketteFuerDen710`, dazu die SCPX-Werkzeuge und BIOS-Module der
 *        710-1-Diskette) bootet am PRG 710 bis `A>`; `STAT` rechnet.
 */
TEST_F(Prg710Scpx, Prg710BootetVonDerSysprgDiskette) {
    k1520test::TempDisk disk(kDiskette710);
    auto m = maschine(V::Prg710);
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    booteBisPrompt(*m, kGruss15);
    ASSERT_TRUE(kommando(*m, "stat")) << bild(*m);
    EXPECT_NE(bild(*m).find("A: R/W, Space:"), std::string::npos) << bild(*m);
}
