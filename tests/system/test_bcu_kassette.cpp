/**
 * @file test_bcu_kassette.cpp
 * @brief BCU 880 V1.2 auf dem A5120 „KMBG“ arbeitet mit der Kassette als Laufwerk F:
 *        (doc/design/30_a5120_fruehe_varianten.md, doc/design/32_… AP A8, doc/a5120_alt/bcu.md §8).
 *
 * Ausführung `Kmbg` (K7023 64×16, K7636, K5020 fest auf 30H–37H), Lade-ROM V0.6, Start von der
 * BCU-Diskette „SYL auf Spur 1“ Satz 2.  Der Ablauf ist der des Bedieners:
 *
 * 1. Nach dem Kaltstart die Systemdiskette „ohne SYL“ (`satz2_OhneSYL.1`, dieselbe V1.2) nach A:
 *    — der Warmstart (WBOOT ECC7H) lädt das System von A: Zylinder 0–1 nach; auf der
 *    SYL-Diskette liegt dort der Lader, ein Warmstart von ihr stürzt ab.  B: = eine zweite Kopie.
 * 2. `OPEN B:KMBG` — eingebautes CCP-Kommando (Tafel D958H): lädt `KMBG.REL` speicherresident
 *    (Seitenrelokation, Meldung „kmbg opened“) und hängt den Treiber in den BDOS-Einsprung.
 *    `B:KMBG` ohne Typ sucht `KMBG.COM` („B:KMBG?“).
 * 3. `B:INIT` (BDOS 50H) — „KASSETTE IN DRIVE F! (Y)“, `Y`: Bandmarke, 1,27 s löschen,
 *    zurückspulen.  INIT fragt danach „AGAIN?“ und endet nur auf 0DH; die K7636.01 hat keine
 *    Taste mit 0DH, und der CONIN dieser BCU-Fassung übersetzt nicht (bcu.md §4).  Der Ausweg
 *    am Gerät ist die BDOS-Ausgabebremse: DEL LINE (13H = ^S) während des Formatierens
 *    vormerken, dann INS LINE (03H = ^C) → Warmstart.
 * 4. `PIP F:=B:STAT.COM` schreibt die Datei aufs Band (16-B-Kopf, Datenblöcke, 17-B-Abschluss,
 *    Bandmarke), `PIP B:STAT2.COM=F:STAT.COM` liest sie zurück; verglichen wird mit dem DiskTool.
 *
 * **F: ist das GERÄT 2** (X4): der BCU-Treiber führt E: und F: (BDOS 0EH, E = 4/5; Merker
 * 0018H/0019H, Bit 0 = 0 → `/RES2`, `/AWA2`, `/NIB2`).  Die Kassette liegt so, wie man sie
 * einlegt, am Anfang des Vorspanns: der BCU-Treiber wartet beim Überfahren ohne Frist auf das
 * Magnetband (KMBG.REL der BCU, 0B04H relativ) — anders als die SCPX-Bibliothek (Merkposten
 * Kassette 9).
 *
 * Langsam (Kaltstart + ≈ 50 s Bandlauf in Maschinenzeit) → Label `format_integration`.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/logger.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/peripherals/kassette/kassetten_medium.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"

namespace {

using A = A5120Machine::Config::Ausfuehrung;
using k1520test::kSmallBatch;
using k1520test::TempDisk;
using kassette::KassettenMedium;
using kassette::Seite;

constexpr const char* kFormat = "k5601_ss40_26x128";
constexpr const char* kSystem = "bcu12_ss40-26x128_k5600-10_ohnesyl-sys1.img";
constexpr uint32_t kZeilenende = K7636::QK_TASTE_BASE | 0x37;   // ↵ (A15, 0AH) — bcu.md §4
constexpr uint32_t kDelLine    = K7636::QK_TASTE_BASE | 0x0F;   // 13H = ^S
constexpr uint32_t kInsLine    = K7636::QK_TASTE_BASE | 0x0E;   // 03H = ^C

/// K7023-Bild (16×64) als Text; Bit 7 (Kursor) ausgeblendet.
std::string bild(A5120Machine& m) {
    std::string s;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 64; ++c) {
            const uint8_t ch = m.screenChar(c, r) & 0x7F;
            s.push_back((ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ');
        }
        s.push_back('\n');
    }
    return s;
}

bool laufeBis(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long n = 0; n < max_cycles; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) == 0 && bild(m).find(needle) != std::string::npos) return true;
    }
    return bild(m).find(needle) != std::string::npos;
}

/// Letzte nicht leere Bildzeile ohne Leerzeichen am Ende.
std::string letzteZeile(A5120Machine& m) {
    const std::string s = bild(m);
    std::string letzte;
    for (size_t a = 0; a < s.size();) {
        const size_t e = s.find('\n', a);
        std::string z = s.substr(a, e - a);
        while (!z.empty() && z.back() == ' ') z.pop_back();
        if (!z.empty()) letzte = z;
        a = e + 1;
    }
    return letzte;
}

/// Bis die unterste Zeile nur den Prompt trägt (der vorige Befehl ist fertig).
bool laufeBisPrompt(A5120Machine& m, long long max_cycles) {
    for (long long n = 0; n < max_cycles; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) == 0 && letzteZeile(m) == "A>") return true;
    }
    return letzteZeile(m) == "A>";
}

void befehl(A5120Machine& m, const std::string& s) {
    k1520test::typeString(m, s);
    k1520test::typeKey(m, kZeilenende);
}

/// Datei @p name von der Diskette @p pfad holen (DiskTool); leer bei Fehler (+ ADD_FAILURE).
std::vector<uint8_t> holeDatei(const std::string& pfad, const std::string& name) {
    std::string f;
    static const FormatCatalog formate = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs      = FsCatalog::loadDefault(formate, &f);
    std::string err;
    auto v = DiskVolume::open(pfad, "", formate, fs, err, /*read_only=*/true);
    if (!v) { ADD_FAILURE() << "DiskVolume::open: " << err; return {}; }
    const std::string ziel = pfad + "." + name;
    TransferOptions o;
    o.overwrite = true;
    if (!v->extract(FileRef::parse(name), ziel, o)) { ADD_FAILURE() << name << ": " << v->lastError(); return {}; }
    std::ifstream in(ziel, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(ziel.c_str());
    return d;
}

}  // namespace

/**
 * @brief Abnahme AP A8: BCU 880 formatiert die Kassette in F:, schreibt eine Datei und liest
 *        sie zurück (Wächter auch für `Z80::ei_schatten` und `Z80PIO::onRETI`, Befund A8).
 */
TEST(BcuKassette, FormatiertSchreibtLiest) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::WARN);
    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Kmbg);
    cfg.laderom = K2526::Laderom::V06;
    A5120Machine m(cfg);
    ASSERT_TRUE(m.k5020());
    auto band = std::make_shared<KassettenMedium>();       // leer, Kopf am Vorspannanfang
    ASSERT_TRUE(m.k5020()->laufwerk(1).kassetteEinlegen(band));   // F: = Gerät 2
    m.powerOn();
    TempDisk start("bcu12_ss40-26x128_k5600-10_syl-spur1-satz2.img");
    ASSERT_TRUE(m.mountDisk(0, start.path(), kFormat, false)) << m.lastError();
    TempDisk b(kSystem);
    ASSERT_TRUE(m.mountDisk(1, b.path(), kFormat, false)) << m.lastError();

    ASSERT_TRUE(laufeBis(m, "\nA>", 40'000'000)) << bild(m);
    // Typzeichen 80H der K7636 verbrauchen (bcu.md §4): erst eine leere Zeile.
    k1520test::typeKey(m, kZeilenende);
    ASSERT_TRUE(laufeBisPrompt(m, 20'000'000)) << bild(m);

    // Systemdiskette für den Warmstart nach A: (siehe Dateikopf, Schritt 1).
    TempDisk a(kSystem);
    ASSERT_TRUE(m.mountDisk(0, a.path(), kFormat, false)) << m.lastError();

    befehl(m, "open b:kmbg");
    ASSERT_TRUE(laufeBis(m, "kmbg opened", 30'000'000)) << bild(m);
    ASSERT_TRUE(laufeBisPrompt(m, 20'000'000)) << bild(m);

    // ── Formatieren ──
    befehl(m, "b:init");
    ASSERT_TRUE(laufeBis(m, "KASSETTE IN DRIVE F! (Y)", 30'000'000)) << bild(m);
    k1520test::typeString(m, "Y");
    k1520test::runCycles(m, 1'000'000);
    k1520test::typeKey(m, kDelLine);              // ^S vormerken (Schritt 3)
    for (long long n = 0; n < 100'000'000 && band->anzahlBloecke(Seite::A) == 0; n += kSmallBatch)
        m.run(static_cast<int>(kSmallBatch));
    ASSERT_EQ(band->anzahlBloecke(Seite::A), 1u) << bild(m);
    EXPECT_TRUE(band->block(Seite::A, 0).bandmarke());
    k1520test::runCycles(m, 20'000'000);          // löschen, zurückspulen, freigeben
    k1520test::typeKey(m, kInsLine);              // ^C → Warmstart
    ASSERT_TRUE(laufeBisPrompt(m, 60'000'000)) << bild(m);
    EXPECT_LT(band->positionUm(), band->geometrie().vorspann_um) << "nicht zurückgespult";

    // ── Schreiben ──
    befehl(m, "pip f:=b:stat.com");
    ASSERT_TRUE(laufeBisPrompt(m, 1'500'000'000)) << bild(m);
    EXPECT_EQ(bild(m).find("7x"), std::string::npos) << bild(m);   // 7xWRITE/7xREAD

    // Auf dem Band: Bandmarke (INIT) · 16-B-Kopf (FCB 0–15) · 23 × 128 B · 17-B-Abschluss ·
    // Bandmarke (treiber.md §6.3).  Je Block AA | Daten | CRC | AA.
    const std::vector<uint8_t> stat = holeDatei(b.path(), "STAT.COM");
    ASSERT_EQ(stat.size(), 23u * 128u);
    ASSERT_EQ(band->anzahlBloecke(Seite::A), 1u + 1u + 23u + 1u + 1u);
    const auto& kopf = band->block(Seite::A, 1).bytes;
    ASSERT_EQ(kopf.size(), 16u + 4u);
    // Nach PIPs Umbenennen (BDOS 17H) trägt der Kopf den endgültigen Namen und in FCB-Byte 15
    // die Satzzahl; der Abschlussblock behält den Arbeitsnamen „STAT    $$$“.
    EXPECT_EQ(std::string(kopf.begin() + 2, kopf.begin() + 13), "STAT    COM");
    EXPECT_EQ(kopf[16], 23);
    for (size_t i = 0; i < 23; ++i) {
        const auto& d = band->block(Seite::A, 2 + i).bytes;
        ASSERT_EQ(d.size(), 128u + 4u) << "Block " << i;
        EXPECT_TRUE(std::equal(d.begin() + 1, d.begin() + 129, stat.begin() + 128 * i)) << "Block " << i;
    }
    EXPECT_EQ(band->block(Seite::A, 25).bytes.size(), 17u + 4u);
    EXPECT_TRUE(band->block(Seite::A, 26).bandmarke());

    // ── Lesen ──
    befehl(m, "pip b:stat2.com=f:stat.com");
    ASSERT_TRUE(laufeBisPrompt(m, 1'500'000'000)) << bild(m);
    EXPECT_EQ(bild(m).find("7x"), std::string::npos) << bild(m);
    // BCU-READ-SEQ (KMBG.REL der BCU 01D2–01F0) endet erst an der Bandmarke (Länge 0) und
    // liefert den 17-B-Abschlussblock als weiteren Satz — Gastverhalten: PIP schreibt einen Satz
    // mehr, dessen erste 17 Byte der Abschlussblock sind (FCB mit Name „STAT    $$$“).
    const std::vector<uint8_t> zurueck = holeDatei(b.path(), "STAT2.COM");
    ASSERT_EQ(zurueck.size(), stat.size() + 128u);
    EXPECT_TRUE(std::equal(stat.begin(), stat.end(), zurueck.begin())) << "zurückgelesene Daten weichen ab";
    const auto& ab = band->block(Seite::A, 25).bytes;
    EXPECT_TRUE(std::equal(ab.begin() + 1, ab.begin() + 18, zurueck.begin() + stat.size()));
    EXPECT_EQ(band->anzahlBloecke(Seite::A), 27u) << "Lesen hat das Band verändert";
}
