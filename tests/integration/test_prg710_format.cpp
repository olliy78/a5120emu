/**
 * @file test_prg710_format.cpp
 * @brief PRG 710 / 710-1 (doc/design/20_prg710.md AP-P5f): `FORMAT` unter UDOS und
 *        SCPX auf einer **Leerdiskette** (`createDisk` mit leerem Formatnamen) in
 *        Laufwerk 1 — die schnellen Wächter.  Die ganze Kette bis zur bootfähigen
 *        UDOS-Diskette steht in `tests/system/test_prg710_format_boot.cpp`
 *        (`format_integration`).
 *
 * **Befunde aus AP-P5f — keine Kernänderung nötig:**
 * - UDOS-`FORMAT` (V 4.3, „FORMAT 870507“, auf allen PRG-Disketten bitgleich) schreibt
 *   die Spur **selbst** im `/WAIT`-Betrieb (Segment 5897H–5A5BH: Indexinterrupt über die
 *   Steuer-PIO A, Vektor E8H → 5A52H; Strom `B4H`/`B6H` an 10H, Bytes an 14H, 26 × 128 B,
 *   Sektorkontrollblock `FF FF FF FF`, Ende `ADH`) — anders als am A5120 (ZVE2-Koroutine).
 *   Es formatiert **eine Seite**: Laufwerk 0–3 = Seite 0, 4–7 = Seite 1; geprüft wird
 *   anschließend über den Resident (`0BFDH`, Lesen jeder Spur).
 * - Der Befund (b) aus AP-P3 („FORMAT kehrt sofort ohne Meldung zurück“) war die
 *   **Bedienung im Test**: Zeichen vor der Frage gehen verloren, und `READY? Y` innerhalb
 *   des Motornachlaufs vom Laden des Programms gibt „ERROR C2“ (Kopf von
 *   `tests/system/prg710_bedienung.h`) — Gastverhalten, am Gerät ebenso.
 * - SCPX-`FORMAT.COM` („FORMAT 1520(SCPX) V 1.5“) läuft an beiden Varianten auf Anhieb,
 *   auch mit Systemkopie; die Kopie bootet an derselben Variante.
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
#include "tests/system/prg710_bedienung.h"

namespace fs = std::filesystem;
using namespace prg710test;

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

/// Temporäre Datei (samt Sicherungskopie und Beiblatt des DiskTools), räumt sich weg.
class TempPfad {
public:
    explicit TempPfad(const std::string& name) : pfad_(k1520test::tempPath(name)) { weg(); }
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

std::unique_ptr<DiskVolume> oeffne(const std::string& abbild, const std::string& fs) {
    std::string err;
    auto dv = DiskVolume::open(abbild, fs, formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    return dv;
}

std::vector<uint8_t> holeDatei(const std::string& abbild, const std::string& fs,
                               const std::string& name) {
    auto dv = oeffne(abbild, fs);
    if (!dv) return {};
    TempPfad ziel("prg710_format_datei.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv->extract(ref, ziel.get(), TransferOptions{})) << dv->lastError();
    return datei(ziel.get());
}

std::vector<uint8_t> systemspuren(const std::string& abbild, const std::string& fs) {
    auto dv = oeffne(abbild, fs);
    std::vector<uint8_t> b;
    if (dv) EXPECT_TRUE(dv->readBootImage(b)) << dv->lastError();
    return b;
}

std::unique_ptr<Prg710Machine> maschine(V v) {
    Prg710Machine::Config c;
    c.variante = v;
    return std::make_unique<Prg710Machine>(c);
}

}  // namespace

class Prg710Format : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
};

/**
 * @test Prg710Format.UdosFormatiertLeerdiskette
 * @brief UDOS bootet von der Gerätediskette, danach kommt eine Leerdiskette in Laufwerk 1
 *        (erst nach dem Start: vorher läsen ROM bzw. Resident die leere Diskette, bis das
 *        Rauschen einen Fehler liefert — beim 710 ≈ 80 s Maschinenzeit, AP-P3b).  `FORMAT`
 *        SYSTEMDISK? N auf Laufwerk 1 (Seite 0, ID TEST) und 5 (Seite 1, ID TEST.B)
 *        meldet keine defekte Spur, `STATUS 1` sieht den neuen Datenträger mit
 *        77 × 26 − 14 = 1988 freien Sektoren, `COPY` schreibt darauf, `CAT` sieht die
 *        Kopie, und das DiskTool liest sie bytegleich.  Beide Seiten werden formatiert
 *        (Laufwerkstyp 5 ist zweiseitig): UDOS liest beim ersten Zugriff auf die Diskette
 *        auch die andere Seite — eine leere Rückseite hängt seit AP-P3b nicht mehr, kostet
 *        aber je Zugriff die Fehlerwiederholungen im Rauschen
 *        (`Prg710Udos.StatusMitEinseitigerDisketteInLaufwerk1`).
 */
TEST_P(Prg710Format, UdosFormatiertLeerdiskette) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    TempPfad leer(std::string("prg710_format_udos_") + name(GetParam()) + ".hfe");
    auto m = maschine(GetParam());
    ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
    ASSERT_TRUE(udosBisDatum(*m)) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
    tippe(*m, "021086");
    ASSERT_TRUE(bisUdosPrompt(*m, kBefehl)) << bild(*m);

    ASSERT_TRUE(m->createDisk(1, leer.get(), "", false)) << m->lastError();
    for (const auto& [lw, id] : {std::pair{"1", "TEST"}, std::pair{"5", "TEST.B"}}) {
        ASSERT_TRUE(udosFormat(*m, "N", lw, id)) << bild(*m);
        EXPECT_EQ(bild(*m).find("DEFEKTIVE TRACK"), std::string::npos) << bild(*m);
        EXPECT_EQ(bild(*m).find("ERROR"), std::string::npos) << bild(*m);
    }
    ASSERT_TRUE(udos(*m, "STATUS 1")) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
    EXPECT_NE(bild(*m).find("DRIVE 1   TEST"), std::string::npos) << bild(*m);
    EXPECT_NE(bild(*m).find("1988 SECTORS AVAILABLE"), std::string::npos) << bild(*m);

    ASSERT_TRUE(udos(*m, "COPY OS.INIT 1/KOPIE")) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
    EXPECT_EQ(vorletzteZeile(*m), "%COPY OS.INIT 1/KOPIE") << "keine Fehlermeldung\n" << bild(*m);
    ASSERT_TRUE(udos(*m, "CAT D=1 P=&")) << "PC=" << std::hex << m->cpuPC() << "\n" << bild(*m);
    EXPECT_NE(bild(*m).find("KOPIE                1"), std::string::npos) << bild(*m);

    ASSERT_TRUE(m->flushDisks()) << m->lastError();
    const auto original = holeDatei(disk, "udos_ds77", "OS.INIT");
    ASSERT_FALSE(original.empty());
    EXPECT_EQ(holeDatei(leer.get(), "udos_ds77", "KOPIE"), original);
}

/**
 * @test Prg710Format.ScpxFormatMitSystemkopieBootet
 * @brief SCPX von der Gerätediskette (710-1: `PRG710-1_SCPX_Boot`, 710: die
 *        `SYSPRG`-Diskette aus AP-P5e), Leerdiskette in B:, `FORMAT` (Vorgabe 0 = DD-DS
 *        16 × 256) mit Systemkopie.  Das DiskTool erkennt `scpx640` mit leerem Verzeichnis
 *        und denselben Systemspuren wie die Quelle; die neue Diskette bootet an derselben
 *        Variante bis `A>`, `DIR` meldet ein leeres Verzeichnis.  Einziger Unterschied
 *        der Systemspuren: FORMAT.COM legt seinen Urheberstempel in die Kopie (s. u.).
 */
TEST_P(Prg710Format, ScpxFormatMitSystemkopieBootet) {
    k1520test::TempDisk disk(scpxDiskette(GetParam()));
    TempPfad leer(std::string("prg710_format_scpx_") + name(GetParam()) + ".hfe");
    {
        auto m = maschine(GetParam());
        ASSERT_TRUE(m->mountDisk(0, disk, m->defaultFormatName(0), false)) << m->lastError();
        m->powerOn();
        lauf(*m, 3'000'000);
        taste(*m, QK_RETURN);
        ASSERT_TRUE(bisScpxPrompt(*m, kBoot)) << bild(*m);
        ASSERT_NE(bild(*m).find(scpxGruss(GetParam())), std::string::npos) << bild(*m);

        ASSERT_TRUE(m->createDisk(1, leer.get(), "", false)) << m->lastError();
        ASSERT_TRUE(scpxFormatB(*m, '\r', true)) << bild(*m);
        EXPECT_NE(bild(*m).find("FORMATTING COMPLETE"), std::string::npos) << bild(*m);
        EXPECT_EQ(bild(*m).find("DEFEKTIV"), std::string::npos) << bild(*m);
        EXPECT_EQ(bild(*m).find("ERROR"), std::string::npos) << bild(*m);
        ASSERT_TRUE(m->flushDisks()) << m->lastError();
    }
    {
        auto dv = oeffne(leer.get(), "scpx640");
        ASSERT_NE(dv, nullptr);
        EXPECT_TRUE(dv->list().empty()) << "frisch formatiert: leeres Verzeichnis";
    }
    const auto quelle = systemspuren(disk, "scpx640");
    ASSERT_EQ(quelle.size(), 16384u);
    // FORMAT.COM legt in die Kopie seinen Urheberstempel (32 Byte ab Systemspur-Byte 2FE0H,
    // in FORMAT.COM ab Dateiversatz 065FH mit +C0H verschlüsselt): Gastverhalten.
    auto kopie = systemspuren(leer.get(), "scpx640");
    ASSERT_EQ(kopie.size(), quelle.size());
    const std::string stempel(kopie.begin() + 0x2FE0, kopie.begin() + 0x2FE0 + 14);
    EXPECT_EQ(stempel, "IMPLEMENTET BY");
    std::fill(kopie.begin() + 0x2FE0, kopie.begin() + 0x3000, 0xAA);
    EXPECT_EQ(kopie, quelle) << "Systemkopie = Systemspuren von A: (bis auf den Stempel)";

    auto neu = maschine(GetParam());
    ASSERT_TRUE(neu->mountDisk(0, leer.get(), neu->defaultFormatName(0), false)) << neu->lastError();
    neu->powerOn();
    lauf(*neu, 3'000'000);
    taste(*neu, QK_RETURN);
    ASSERT_TRUE(bisScpxPrompt(*neu, kBoot)) << "PC=" << std::hex << neu->cpuPC() << "\n" << bild(*neu);
    EXPECT_NE(bild(*neu).find(scpxGruss(GetParam())), std::string::npos) << bild(*neu);
    ASSERT_TRUE(scpx(*neu, "DIR")) << bild(*neu);
    EXPECT_NE(bild(*neu).find("NO FILE"), std::string::npos) << bild(*neu);
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Format, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return std::string(name(i.param)); });
