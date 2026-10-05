/**
 * @file test_prg710_lochband.cpp
 * @brief PRG 710 / 710-1 (doc/design/20_prg710.md AP-P8b): ADA K6022 — Lochband stanzen und
 *        lesen unter UDOS 4.3, wie ein Anwender es tut: `DO TWRITE.1215 <datei> F=A` bzw.
 *        `DO TREAD.1210 <datei> F=A` (Prozeduren der Gerätediskette: `ACTIVATE $PTAPE.6022`,
 *        `DEFINE 20 $PTAPE.6022`, `TAPE.WRITE`/`TAPE.READ`, Treiber zurück).
 *
 * Befunde (doc/prg710/sif1000_fernschreiber.md §2):
 * - `PTAPE.6022` überträgt **Text**: Bit 7 = gerade Parität, CR → NL (1EH), LF entfällt;
 *   beim Lesen werden 00H, FFH und LF übergangen, NL → CR, Bit 7 gelöscht.  Nullbytes trägt
 *   der Treiber nicht — deshalb `F=A`; das Vorgabeformat von `TAPE.WRITE` (Kopfsatz mit
 *   Nullbytes) liest er nicht zurück (`ERROR C9`).
 * - `TAPE.WRITE` stanzt je 150 Nullbytes Vor- und Nachlauf selbst; `TAPE.READ` legt
 *   die Datei auf Seite 1 an (`CAT`: „DRIVE 4“) und füllt den letzten Satz mit 00H auf.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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

constexpr long long kBand = 300'000'000;   // ein Band stanzen bzw. lesen (150 Z/s bzw. 1000 Z/s)

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        return FormatCatalog::loadDefault(&f);
    }();
    return c;
}
const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        return FsCatalog::loadDefault(formate(), &f);
    }();
    return c;
}

/// Eine Datei der UDOS-Diskette @p abbild holen (Seite @p seite; UDOS legt die vom Band
/// gelesenen Dateien auf Seite 1 an — `CAT` meldet „DRIVE 4“).
std::vector<uint8_t> holeDatei(const std::string& abbild, const std::string& name, int seite = 0) {
    std::string err;
    auto dv = DiskVolume::open(abbild, "udos_ds77", formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    if (!dv) return {};
    const std::string ziel = k1520test::tempPath("k1520_lochband_datei.bin");
    // extract überschreibt nicht; unter wine ist getpid() je Lauf gleich, ein Rest eines
    // abgebrochenen Laufs bliebe sonst liegen und ließe jeden weiteren scheitern.
    std::error_code ec;
    fs::remove(ziel, ec);
    FileRef ref;
    ref.volume = seite;
    ref.name = name;
    EXPECT_TRUE(dv->extract(ref, ziel, TransferOptions{})) << dv->lastError();
    std::ifstream f(ziel, std::ios::binary);
    std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), {}};
    fs::remove(ziel, ec);
    return d;
}

/// So stanzt `PTAPE.6022` eine Textzeile (F033H–F04EH).
std::vector<uint8_t> gestanzt(const std::vector<uint8_t>& text) {
    std::vector<uint8_t> b;
    for (uint8_t c : text) {
        if (c == 0x0A) continue;
        if (c == 0x0D) c = 0x1E;
        c &= 0x7F;
        int eins = 0;
        for (int i = 0; i < 7; ++i) eins += (c >> i) & 1;
        if (eins & 1) c |= 0x80;   // gerade Parität
        b.push_back(c);
    }
    return b;
}

std::vector<uint8_t> ohneVorUndNachlauf(std::vector<uint8_t> b) {
    while (!b.empty() && b.back() == 0) b.pop_back();
    size_t i = 0;
    while (i < b.size() && b[i] == 0) ++i;
    return {b.begin() + static_cast<long>(i), b.end()};
}

/// `TAPE.READ` legt nur ganze Sätze an: der letzte ist mit 00H aufgefüllt.
std::vector<uint8_t> ohneFuellung(std::vector<uint8_t> b) {
    while (!b.empty() && b.back() == 0) b.pop_back();
    return b;
}

class Prg710Lochband : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override { k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::WARN); }
    /// UDOS bis zum Prompt (Datum 02.10.86).
    void starte(Prg710Machine& m, const std::string& disk) {
        ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
        ASSERT_TRUE(udosBisDatum(m)) << bild(m);
        tippe(m, "021086");
        taste(m, QK_RETURN);
        ASSERT_TRUE(bisUdosPrompt(m, kBefehl)) << bild(m);
    }
    static Prg710Machine::Config cfg(V v) {
        Prg710Machine::Config c;
        c.variante = v;
        return c;
    }
};

}  // namespace

/**
 * @test Prg710Lochband.StanztEineDateiBytegleichUndLiestSieZurueck
 * @brief `TWRITE.1215 TREAD.1210 F=A` stanzt die Datei Byte für Byte so, wie der Treiber es
 *        vorschreibt (Vor-/Nachlauf aus Nullbytes); dasselbe Band in den Leser,
 *        `TREAD.1210 X1 F=A` — die neue Datei ist inhaltsgleich zur Quelle.
 */
TEST_P(Prg710Lochband, StanztEineDateiBytegleichUndLiestSieZurueck) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    const std::vector<uint8_t> quelle = holeDatei(disk, "TREAD.1210");
    ASSERT_EQ(quelle.size(), 125u);

    Prg710Machine m(cfg(GetParam()));
    ASSERT_TRUE(m.installK6022()) << m.k6022Fehler();   // seit AP-L1 eine Option (Entwurf 23)
    ASSERT_NO_FATAL_FAILURE(starte(m, disk));
    ASSERT_TRUE(udos(m, "DO TWRITE.1215 TREAD.1210 F=A", kBand)) << bild(m);
    EXPECT_EQ(bild(m).find("ERROR"), std::string::npos) << bild(m);

    const std::vector<uint8_t> band = m.k6022()->stanzband();
    EXPECT_EQ(ohneVorUndNachlauf(band), gestanzt(quelle));
    ASSERT_EQ(band.size(), 150 + quelle.size() + 150);   // Vor- und Nachlauf je 150 Nullbytes
    for (int i = 0; i < 150; ++i) ASSERT_EQ(band[static_cast<size_t>(i)], 0) << i;   // Vorlauf (150)

    m.k6022()->bandEinlegen(band);
    ASSERT_TRUE(udos(m, "DO TREAD.1210 X1 F=A", kBand)) << bild(m);
    EXPECT_EQ(bild(m).find("ERROR"), std::string::npos) << bild(m);
    EXPECT_GE(m.k6022()->leserStand().gelesen, 160u + quelle.size());
    ASSERT_TRUE(m.flushDisks());
    EXPECT_EQ(ohneFuellung(holeDatei(disk, "X1", 1)), quelle);
}

/**
 * @test Prg710Lochband.LiestEinFremdesTextband
 * @brief Ein Band ohne Vorlauf und ohne Parität (eine Textdatei des Wirtsrechners, CR LF)
 *        — der Leser gibt Vor- und Nachlauf dazu; die Zeilen kommen mit CR an.
 */
TEST_P(Prg710Lochband, LiestEinFremdesTextband) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    Prg710Machine m(cfg(GetParam()));
    ASSERT_TRUE(m.installK6022()) << m.k6022Fehler();   // seit AP-L1 eine Option (Entwurf 23)
    ASSERT_NO_FATAL_FAILURE(starte(m, disk));
    const std::string text = "ZEILE EINS\r\nZEILE ZWEI\r\n";
    m.k6022()->bandEinlegen({text.begin(), text.end()});
    ASSERT_TRUE(udos(m, "DO TREAD.1210 X2 F=A", kBand)) << bild(m);
    EXPECT_EQ(bild(m).find("ERROR"), std::string::npos) << bild(m);
    // Der Treiber hört nach 100 Nullbytes des Nachlaufs auf — ganz durch ist das Band nicht.
    EXPECT_EQ(m.k6022()->leserStand().gelesen, text.size());
    EXPECT_FALSE(m.k6022()->leserStand().bandende);
    ASSERT_TRUE(m.flushDisks());
    const std::vector<uint8_t> x2 = ohneFuellung(holeDatei(disk, "X2", 1));
    EXPECT_EQ(std::string(x2.begin(), x2.end()), "ZEILE EINS\rZEILE ZWEI\r");
}

/**
 * @test Prg710Lochband.OhneBandUndOhneStanzerMeldetDerTreiberC2
 * @brief Leser ohne Band (STA D6 vor Datenbeginn) und ausgeschalteter Stanzer (kein END,
 *        Frist ≈ 0,9 s): `PTAPE.6022` meldet „ERROR C2“; UDOS läuft weiter.
 */
TEST_P(Prg710Lochband, OhneBandUndOhneStanzerMeldetDerTreiberC2) {
    k1520test::TempDisk disk(udosDiskette(GetParam()));
    Prg710Machine m(cfg(GetParam()));
    ASSERT_TRUE(m.installK6022()) << m.k6022Fehler();   // seit AP-L1 eine Option (Entwurf 23)
    ASSERT_NO_FATAL_FAILURE(starte(m, disk));
    ASSERT_TRUE(udos(m, "DO TREAD.1210 X3 F=A", kBand)) << bild(m);
    EXPECT_NE(bild(m).find("ERROR C2"), std::string::npos) << bild(m);

    m.k6022()->setStanzerEin(false);
    ASSERT_TRUE(udos(m, "DO TWRITE.1215 TREAD.1210 F=A", kBand)) << bild(m);
    EXPECT_EQ(vorletzteZeile(m), "ERROR C2") << bild(m);
    EXPECT_EQ(m.k6022()->stanzbandLaenge(), 0u);
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Lochband, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return name(i.param); });
