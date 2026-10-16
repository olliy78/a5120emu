/**
 * @file test_disktool_bcu.cpp
 * @brief DiskTool fuer BCU 880 am fruehen A5120 (doc/a5120_alt/bcu.md §11).
 *
 * Massstab ist wie bei `DisktoolPc1715.*`, ob die Maschine von der GEBAUTEN Diskette startet:
 *  - **Startdiskette „SYL auf Spur 1“** (`bcu880_syl`): wird am SYL-Lader erkannt, ist eine reine
 *    Startdiskette (`nur_boot`: kein `put`/`rm`), `boot-get` -> `create --boot` baut sie
 *    SEKTORGLEICH nach, ein fremdes Bootabbild wird VOR dem Anlegen abgewiesen.
 *  - **Arbeitsdiskette „ohne SYL“** (`bcu880`): System auf Zylinder 0-1 (Warmstart), Dateien ab
 *    Zylinder 2 — mit dem Werkzeug gebaut, mit Dateien gefuellt, von BCU gelistet.
 *  - **SYSGEN.COM** der Diskette schreibt genau diese Systemspuren (Experiment im Emulator).
 *
 * Disketten nur ueber Temp-Pfade (der Emulator und das DiskTool oeffnen schreibend).
 */
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/temp_path.h"

#ifndef A5120_LIEFERDISKETTEN
#error "A5120_LIEFERDISKETTEN fehlt - k1520_add_test(... DEFS A5120_LIEFERDISKETTEN=\"<repo>/disks\")"
#endif

namespace fs = std::filesystem;
using A = A5120Machine::Config::Ausfuehrung;
using L = K2526::Laderom;
using k1520test::kSmallBatch;
using k1520test::QK_RETURN;
using k1520test::TempDisk;

namespace {

constexpr const char* kFormat = "k5601_ss40_26x128";
constexpr const char* kSatz2 = "bcu12_ss40-26x128_k5600-10_syl-spur1-satz2.img";   // K7023, 64x16
constexpr const char* kSatz1 = "bcu12_ss40-26x128_k5600-10_syl-spur1-satz1.img";   // K7024, 80x24
constexpr const char* kArbeit = "bcu12_ss40-26x128_k5600-10_ohnesyl-sys1.img";

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

/// Temporaerer Pfad samt Sicherungskopie des DiskTools, raeumt sich weg.
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

/// Ein ausgeliefertes Bootabbild aus disks/bootsektoren/.
std::string bootbin(const char* name) {
    return (fs::path(A5120_LIEFERDISKETTEN) / "bootsektoren" / name).string();
}

std::unique_ptr<DiskVolume> oeffne(const std::string& pfad, const char* fs_name = "",
                                   bool nur_lesen = true) {
    std::string err;
    auto dv = DiskVolume::open(pfad, fs_name, formate(), dateisysteme(), err, nur_lesen);
    EXPECT_NE(dv, nullptr) << pfad << ": " << err;
    return dv;
}

std::unique_ptr<DiskVolume> baue(const std::string& pfad, const char* fs_name,
                                 const std::string& boot, std::string* fehler = nullptr) {
    std::string err;
    auto dv = DiskVolume::create(pfad, fs_name, "", formate(), dateisysteme(), err, boot);
    if (fehler) *fehler = err;
    return dv;
}

// ─── Maschine ────────────────────────────────────────────────────────────────

std::string bild(A5120Machine& m) {
    const int sp = m.k7023() ? 64 : 80, ze = m.k7023() ? 16 : 24;
    std::string s;
    for (int r = 0; r < ze; ++r) {
        for (int c = 0; c < sp; ++c) {
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

/// Bis `A>`; die erste Zeile verbraucht das Typzeichen 80H der K7636 (doc/a5120_alt/bcu.md §4).
void bisPrompt(A5120Machine& m, uint32_t ende) {
    ASSERT_TRUE(laufeBis(m, "\nA>", 40'000'000)) << "kein A>:\n" << bild(m);
    EXPECT_NE(bild(m).find("BCU 880 VERS 1.2"), std::string::npos) << bild(m);
    k1520test::typeKey(m, ende);
    auto prompts = [&] {
        int n = 0;
        const std::string s = bild(m);
        for (size_t p = s.find("\nA>"); p != std::string::npos; p = s.find("\nA>", p + 1)) ++n;
        return n;
    };
    for (long long n = 0; n < 20'000'000; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        if ((n % 100'000) == 0 && prompts() >= 2) break;
    }
    ASSERT_GE(prompts(), 2) << "zweiter Prompt fehlt:\n" << bild(m);
}

/// Befehl tippen und abschliessen.
void befehl(A5120Machine& m, const std::string& zeile, uint32_t ende) {
    k1520test::typeString(m, zeile);
    ASSERT_TRUE(laufeBis(m, zeile, 20'000'000)) << "Eingabe kommt nicht an:\n" << bild(m);
    k1520test::typeKey(m, ende);
}

}  // namespace

// ─── Erkennung und Schreibsperre ─────────────────────────────────────────────

TEST(DisktoolBcu, ErkenntDieStartdisketten) {
    for (const char* name : {kSatz2, kSatz1, "bcu21_ss40-26x128_k5600-10_eigenbau-syl-v21.img"}) {
        TempDisk d(name);
        auto dv = oeffne(d.path());
        ASSERT_NE(dv, nullptr);
        EXPECT_EQ(dv->detection().filesystem, "bcu880_syl") << name;
        EXPECT_TRUE(dv->list().empty()) << name;
        EXPECT_EQ(dv->volumeInfo(0).free_bytes, 0u) << name;   // kein Dateibereich fuer das Geraet
        EXPECT_EQ(dv->bootAreaSize(0), 5u * 26 * 128) << name;   // Zylinder 0-4
    }
}

TEST(DisktoolBcu, DieArbeitsdisketteBleibtLesbar) {
    TempDisk d(kArbeit);
    auto dv = oeffne(d.path());
    ASSERT_NE(dv, nullptr);
    EXPECT_NE(dv->detection().filesystem, "bcu880_syl");
    EXPECT_EQ(dv->list().size(), 19u);
}

TEST(DisktoolBcu, EineDatendisketteIstKeineStartdiskette) {
    // Gleiche Geometrie, aber kein SYL-Lader auf Zylinder 1: der Lader ist der Nachweis.
    TempPfad p("k1520_bcu_daten.img");
    auto dv = baue(p.get(), "bcu880", "");
    ASSERT_NE(dv, nullptr);
    ASSERT_TRUE(dv->flush());
    auto offen = oeffne(p.get());
    ASSERT_NE(offen, nullptr);
    EXPECT_NE(offen->detection().filesystem, "bcu880_syl");
}

TEST(DisktoolBcu, EineStartdisketteNimmtKeineDateienAn) {
    TempDisk d(kSatz2);
    auto dv = oeffne(d.path(), "", /*nur_lesen=*/false);
    ASSERT_NE(dv, nullptr);
    TempPfad q("k1520_bcu_quelle.txt");
    schreibe(q.get(), {'h', 'i'});
    FileRef ref;
    ref.name = "HI.TXT";
    EXPECT_FALSE(dv->insert(q.get(), ref, TransferOptions{}));
    EXPECT_NE(dv->lastError().find("Startdiskette"), std::string::npos) << dv->lastError();
    EXPECT_FALSE(dv->erase(ref));
    EXPECT_FALSE(dv->makeDirectory(ref));
    // Das Bootabbild bleibt les- und schreibbar.
    std::vector<uint8_t> boot;
    ASSERT_TRUE(dv->readBootImage(boot)) << dv->lastError();
    EXPECT_TRUE(dv->writeBootImage(boot)) << dv->lastError();
}

// ─── Bauen ───────────────────────────────────────────────────────────────────

TEST(DisktoolBcu, BautDieStartdisketteSektorgleichNach) {
    struct Fall { const char* fixture; const char* bin; };
    for (const Fall f : {Fall{kSatz2, "boot_bcu12_syl_k7023.bin"},
                         Fall{kSatz1, "boot_bcu12_syl_k7024.bin"}}) {
        TempPfad p("k1520_bcu_syl_neu.img");
        std::string fehler;
        auto dv = baue(p.get(), "bcu880_syl", bootbin(f.bin), &fehler);
        ASSERT_NE(dv, nullptr) << f.bin << ": " << fehler;
        ASSERT_TRUE(dv->flush());
        // Der Beleg ist ein rohes .img: beide muessen Byte fuer Byte gleich sein.
        const fs::path beleg = fs::path(A5120_LIEFERDISKETTEN).parent_path()
                             / "tests" / "fixtures" / "disks" / f.fixture;
        ASSERT_TRUE(fs::is_regular_file(beleg)) << beleg;
        EXPECT_EQ(datei(p.get()), datei(beleg.string())) << f.bin;
    }
}

TEST(DisktoolBcu, WeistFremdeBootabbilderVorDemAnlegenAb) {
    const std::vector<uint8_t> syl = datei(bootbin("boot_bcu12_syl_k7023.bin"));
    const std::vector<uint8_t> ohne = datei(bootbin("boot_bcu12_ohnesyl.bin"));
    ASSERT_EQ(syl.size(), 16640u);

    struct Fall { const char* fs; std::vector<uint8_t> bild; const char* erwarte; };
    std::vector<uint8_t> kaputt = syl;
    kaputt[26 * 128 + 0x100] ^= 0x01;                        // ein Bit im Lader: Selbstpruefung bricht
    std::vector<uint8_t> abgeschnitten(syl.begin(), syl.begin() + 2 * 26 * 128 + 10 * 128);   // 10 statt 67 Sektoren System
    std::vector<uint8_t> cpa(syl.size(), 0xE5);              // sieht nach nichts aus
    const Fall faelle[] = {
        {"bcu880_syl", ohne, "SYL"},                         // „ohne SYL“ auf einer Startdiskette
        {"bcu880_syl", datei(bootbin("boot_cpa780.bin")), "SYL"},
        {"bcu880_syl", kaputt, "Selbstpruefung"},
        {"bcu880_syl", abgeschnitten, "bliebe mitten im System"},
        {"bcu880_syl", cpa, "SYL"},
        {"bcu880", datei(bootbin("boot_cpa780.bin")), "BCU-880-Kennung"},
        {"bcu880", cpa, "BCU-880-Kennung"},
    };
    for (const Fall& f : faelle) {
        TempPfad zu("k1520_bcu_fremd.img"), bin("k1520_bcu_fremd.bin");
        schreibe(bin.get(), f.bild);
        std::string fehler;
        auto dv = baue(zu.get(), f.fs, bin.get(), &fehler);
        EXPECT_EQ(dv, nullptr) << f.fs << " nahm ein falsches Bootabbild";
        EXPECT_NE(fehler.find(f.erwarte), std::string::npos) << f.fs << ": " << fehler;
        EXPECT_FALSE(fs::exists(zu.get())) << "es blieb eine halbe Diskette liegen";
    }
}

TEST(DisktoolBcu, BootPutPruefstAuchAufEinerVorhandenenDiskette) {
    TempDisk d(kSatz2);
    auto dv = oeffne(d.path(), "", false);
    ASSERT_NE(dv, nullptr);
    EXPECT_FALSE(dv->writeBootImageFile(bootbin("boot_bcu12_ohnesyl.bin")));
    EXPECT_NE(dv->lastError().find("SYL"), std::string::npos) << dv->lastError();
    EXPECT_TRUE(dv->writeBootImageFile(bootbin("boot_bcu12_syl_k7023.bin"))) << dv->lastError();
}

// ─── Die gebaute Diskette im Geraet ──────────────────────────────────────────

namespace {

/// Arbeitsdiskette mit System und den 19 Dateien der Beleg-Diskette, gebaut vom Werkzeug.
void baueArbeitsdiskette(const std::string& ziel) {
    TempDisk beleg(kArbeit);
    auto quelle = oeffne(beleg.path());
    ASSERT_NE(quelle, nullptr);
    TempPfad ordner("k1520_bcu_dateien");
    fs::create_directories(ordner.get());
    ASSERT_TRUE(quelle->extractAll(ordner.get(), TransferOptions{})) << quelle->lastError();

    std::string fehler;
    auto dv = baue(ziel, "bcu880", bootbin("boot_bcu12_ohnesyl.bin"), &fehler);
    ASSERT_NE(dv, nullptr) << fehler;
    ASSERT_TRUE(dv->insertAll(ordner.get(), TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(dv->flush()) << dv->lastError();
    EXPECT_EQ(dv->list().size(), 19u);
}

}  // namespace

/**
 * @brief Beide Disketten vom Werkzeug gebaut: Start von der Startdiskette bis `A>`, `dir b:` listet
 *        die geschriebenen Dateien, und mit der Arbeitsdiskette in A: laedt der Warmstart (^C) das
 *        System von Zylinder 0-1 wieder.
 */
TEST(DisktoolBcu, GebauteDisketteBootetUndListetDieDateien) {
    TempPfad start("k1520_bcu_gebaut_a.img"), arbeit("k1520_bcu_gebaut_b.img"),
             arbeit2("k1520_bcu_gebaut_a2.img");
    {
        std::string fehler;
        auto dv = baue(start.get(), "bcu880_syl", bootbin("boot_bcu12_syl_k7023.bin"), &fehler);
        ASSERT_NE(dv, nullptr) << fehler;
        ASSERT_TRUE(dv->flush());
    }
    baueArbeitsdiskette(arbeit.get());
    baueArbeitsdiskette(arbeit2.get());

    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    cfg.laderom = L::V06;
    A5120Machine m(cfg);
    m.powerOn();
    ASSERT_TRUE(m.mountDisk(0, start.get(), kFormat, false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, arbeit.get(), kFormat, false)) << m.lastError();
    const uint32_t ende = K7636::QK_TASTE_BASE | 0x37;   // Satz 2: Zeilenende = ↵ (A15)
    bisPrompt(m, ende);
    befehl(m, "dir b:", ende);
    k1520test::runCycles(m, 30'000'000);                     // Liste vollstaendig ausgeben lassen
    ASSERT_NE(bild(m).find("XSUB"), std::string::npos) << "dir b: ohne Verzeichnis:\n" << bild(m);
    EXPECT_NE(bild(m).find("KMBG     REL"), std::string::npos) << bild(m);
    EXPECT_NE(bild(m).find("SYSGEN   COM"), std::string::npos) << bild(m);

    // Warmstart von der gebauten Arbeitsdiskette in A:.
    ASSERT_TRUE(m.mountDisk(0, arbeit2.get(), kFormat, false)) << m.lastError();
    k1520test::runCycles(m, 2'000'000);
    k1520test::typeKey(m, K7636::QK_TASTE_BASE | 0x0E);   // INS LINE = ^C
    k1520test::runCycles(m, 10'000'000);
    befehl(m, "dir a:", ende);
    EXPECT_TRUE(laufeBis(m, "\nA: ", 30'000'000)) << "dir a: nach dem Warmstart:\n" << bild(m);
}

/**
 * @brief SYSGEN.COM der Diskette (Digital Research SYSGEN VER 2.0, ueber das BIOS: Spur 0-1 mit
 *        26 Sektoren) schreibt genau die Systemspuren, die `bcu880` als Bootabbild fuehrt.
 *
 * Quelle B: (Arbeitsdiskette), Ziel C: (leere, vom Werkzeug angelegte Diskette).  SYSGEN
 * erwartet die Zeilenenden als 0DH; die BCU-Fassung von „Kaltstart“ (Satz 1, K7024) uebersetzt
 * ET1 -> 0DH im CONIN — Satz 2 reicht 0AH durch und kaeme nicht weiter (doc/a5120_alt/bcu.md §4).
 */
TEST(DisktoolBcu, SysgenSchreibtDieSystemspurenDerArbeitsdiskette) {
    TempPfad start("k1520_bcu_sg_a.img"), leer("k1520_bcu_sg_c.img");
    {
        std::string fehler;
        auto dv = baue(start.get(), "bcu880_syl", bootbin("boot_bcu12_syl_k7024.bin"), &fehler);
        ASSERT_NE(dv, nullptr) << fehler;
        ASSERT_TRUE(dv->flush());
        auto c = baue(leer.get(), "bcu880", "", &fehler);     // formatiert, Systemspuren leer
        ASSERT_NE(c, nullptr) << fehler;
        ASSERT_TRUE(c->flush());
    }
    TempDisk quelle(kArbeit);

    A5120Machine::Config cfg;
    cfg.laderom  = L::V06;
    cfg.tastatur = A5120Machine::Config::Tastatur::K7636;
    cfg.drive_profiles = {"K5600.10", "K5600.10", "K5600.10", "none"};
    A5120Machine m(cfg);
    m.powerOn();
    ASSERT_TRUE(m.mountDisk(0, start.get(), kFormat, false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, quelle.path(), kFormat, false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(2, leer.get(), kFormat, false)) << m.lastError();
    bisPrompt(m, QK_RETURN);
    befehl(m, "b:sysgen", QK_RETURN);
    ASSERT_TRUE(laufeBis(m, "SOURCE DRIVE NAME", 30'000'000)) << bild(m);
    k1520test::typeString(m, "B");
    ASSERT_TRUE(laufeBis(m, "THEN TYPE RETURN", 10'000'000)) << bild(m);
    k1520test::typeKey(m, QK_RETURN);
    ASSERT_TRUE(laufeBis(m, "FUNCTION COMPLETE", 30'000'000)) << "Lesen:\n" << bild(m);
    ASSERT_TRUE(laufeBis(m, "DESTINATION DRIVE NAME", 10'000'000)) << bild(m);
    k1520test::typeString(m, "C");
    ASSERT_TRUE(laufeBis(m, "DESTINATION ON C", 10'000'000)) << bild(m);
    k1520test::typeKey(m, QK_RETURN);
    // Zweites „FUNCTION COMPLETE“: das erste steht noch auf dem Bild — erst nach dem Schreiben
    // folgt wieder die Frage nach dem Ziel.
    const std::string vorher = bild(m);
    for (long long n = 0; n < 60'000'000; n += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        const std::string b = bild(m);
        if (b != vorher && b.find("FUNCTION COMPLETE") != std::string::npos
            && b.find("DESTINATION DRIVE NAME", b.find("DESTINATION ON C")) != std::string::npos)
            break;
    }
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    auto dv = oeffne(leer.get(), "bcu880");
    ASSERT_NE(dv, nullptr);
    std::vector<uint8_t> system;
    ASSERT_TRUE(dv->readBootImage(system)) << dv->lastError();
    EXPECT_EQ(system, datei(bootbin("boot_bcu12_ohnesyl.bin")))
        << "SYSGEN hat nicht die Systemspuren der Quelle geschrieben:\n" << bild(m);
}

// ─── Kombidiskette (Eigenbau, doc/a5120_alt/bcu.md §12) ──────────────────────

TEST(DisktoolBcu, KombidisketteWirdErkanntUndNimmtDateienAn) {
    TempPfad p("k1520_bcu_kombi.img");
    std::string fehler;
    auto dv = baue(p.get(), "bcu880_kombi", bootbin("boot_bcu12_kombi_k7023.bin"), &fehler);
    ASSERT_NE(dv, nullptr) << fehler;
    TempPfad q("k1520_bcu_kombi_quelle.txt");
    schreibe(q.get(), {'h', 'a', 'l', 'l', 'o', '\r', '\n', 0x1A});
    FileRef ref;
    ref.name = "HALLO.TXT";
    ASSERT_TRUE(dv->insert(q.get(), ref, TransferOptions{})) << dv->lastError();
    ASSERT_TRUE(dv->flush());
    auto offen = oeffne(p.get());
    ASSERT_NE(offen, nullptr);
    EXPECT_EQ(offen->detection().filesystem, "bcu880_kombi");
    EXPECT_EQ(offen->list().size(), 1u);
    EXPECT_GT(offen->volumeInfo(0).free_bytes, 100u * 1024);
}

TEST(DisktoolBcu, StartUndKombiAbbildSindNichtAustauschbar) {
    TempPfad zu("k1520_bcu_kombi_falsch.img");
    std::string fehler;
    EXPECT_EQ(baue(zu.get(), "bcu880_syl", bootbin("boot_bcu12_kombi_k7023.bin"), &fehler), nullptr);
    EXPECT_NE(fehler.find("bcu880_kombi"), std::string::npos) << fehler;
    EXPECT_EQ(baue(zu.get(), "bcu880_kombi", bootbin("boot_bcu12_syl_k7023.bin"), &fehler), nullptr);
    EXPECT_NE(fehler.find("Kombi-Aenderungen"), std::string::npos) << fehler;
    EXPECT_FALSE(fs::exists(zu.get()));
}

/**
 * @brief Die Kombidiskette in A: bootet, BCU sieht die vom DiskTool geschriebene Datei (Verzeichnis
 *        ab Zylinder 5), die Arbeitsdiskette in B: bleibt mit OFF 2 lesbar, PIP schreibt eine Datei
 *        nach A: (und der Warmstart danach findet sein CCP auf Zylinder 0), und das DiskTool liest sie
 *        bytegleich zurueck.
 */
TEST(DisktoolBcu, KombidisketteBootetUndArbeitet) {
    TempPfad kombi("k1520_bcu_kombi_a.img");
    {
        std::string fehler;
        auto dv = baue(kombi.get(), "bcu880_kombi", bootbin("boot_bcu12_kombi_k7023.bin"), &fehler);
        ASSERT_NE(dv, nullptr) << fehler;
        TempPfad q("k1520_bcu_kombi_hallo.txt");
        schreibe(q.get(), {'h', 'a', 'l', 'l', 'o', '\r', '\n', 0x1A});
        FileRef ref;
        ref.name = "HALLO.TXT";
        ASSERT_TRUE(dv->insert(q.get(), ref, TransferOptions{})) << dv->lastError();
        ASSERT_TRUE(dv->flush());
    }
    TempDisk arbeit(kArbeit);

    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    cfg.laderom = L::V06;
    A5120Machine m(cfg);
    m.powerOn();
    ASSERT_TRUE(m.mountDisk(0, kombi.get(), kFormat, false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, arbeit.path(), kFormat, false)) << m.lastError();
    const uint32_t ende = K7636::QK_TASTE_BASE | 0x37;
    bisPrompt(m, ende);

    befehl(m, "dir a:", ende);
    k1520test::runCycles(m, 20'000'000);
    EXPECT_NE(bild(m).find("HALLO    TXT"), std::string::npos) << "A: ab Zylinder 5:\n" << bild(m);
    befehl(m, "dir b:", ende);
    k1520test::runCycles(m, 30'000'000);
    EXPECT_NE(bild(m).find("SYSGEN   COM"), std::string::npos) << "B: (OFF 2):\n" << bild(m);

    // PIP endet mit Warmstart: das CCP kommt von A: Zylinder 0 (17 Sektoren).
    befehl(m, "b:pip a:kopie.com=b:stat.com", ende);
    k1520test::runCycles(m, 60'000'000);
    befehl(m, "dir a:", ende);
    k1520test::runCycles(m, 30'000'000);
    EXPECT_NE(bild(m).find("KOPIE    COM"), std::string::npos) << "nach PIP und Warmstart:\n" << bild(m);
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    auto a = oeffne(kombi.get());
    auto b = oeffne(arbeit.path());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->detection().filesystem, "bcu880_kombi");
    TempPfad x("k1520_bcu_kombi_kopie.bin"), y("k1520_bcu_kombi_stat.bin");
    FileRef ka, kb;
    ka.name = "KOPIE.COM";
    kb.name = "STAT.COM";
    ASSERT_TRUE(a->extract(ka, x.get(), TransferOptions{})) << a->lastError();
    ASSERT_TRUE(b->extract(kb, y.get(), TransferOptions{})) << b->lastError();
    EXPECT_EQ(datei(x.get()), datei(y.get()));
}

/**
 * @brief 80 Spuren (K5600.20), Eigenbau: Kombidiskette in A:, 80-Spur-Arbeitsdiskette in B:.
 *        Eine 100-KB-Datei, die das DiskTool schreibt, wird von BCU nach A: kopiert — die Kopie liegt
 *        ueber Zylinder 40 hinaus (nur ein 96-tpi-Laufwerk kommt dorthin) und das DiskTool liest beide
 *        bytegleich.  B: ist OFF 2 (Verzeichnis ab Zylinder 2), A: OFF 5.
 */
TEST(DisktoolBcu, Kombidiskette80ArbeitetUeberZylinder40Hinaus) {
    TempPfad kombi("k1520_bcu_kombi80_a.img"), arbeit("k1520_bcu_kombi80_b.img");
    std::vector<uint8_t> gross(100 * 1024);
    for (size_t i = 0; i < gross.size(); ++i) gross[i] = static_cast<uint8_t>((i * 7 + (i >> 8)) & 0xFF);
    {
        std::string fehler;
        auto dv = baue(kombi.get(), "bcu880_kombi80", bootbin("boot_bcu12_kombi80_k7023.bin"), &fehler);
        ASSERT_NE(dv, nullptr) << fehler;
        TempPfad q("k1520_bcu_gross.com");
        schreibe(q.get(), gross);
        FileRef ref;
        ref.name = "GROSS.COM";
        ASSERT_TRUE(dv->insert(q.get(), ref, TransferOptions{})) << dv->lastError();
        ASSERT_TRUE(dv->flush());

        // Arbeitsdiskette: mit BCU-System in Zylinder 0-1 angelegt (daran wird sie erkannt), PIP.COM und STAT.COM von der Beleg-Diskette.
        auto b = baue(arbeit.get(), "bcu880_80", bootbin("boot_bcu12_ohnesyl.bin"), &fehler);
        ASSERT_NE(b, nullptr) << fehler;
        TempDisk beleg(kArbeit);
        auto quelle = oeffne(beleg.path());
        ASSERT_NE(quelle, nullptr);
        for (const char* n : {"PIP.COM", "STAT.COM"}) {
            TempPfad z("k1520_bcu_pip.com");
            FileRef r;
            r.name = n;
            ASSERT_TRUE(quelle->extract(r, z.get(), TransferOptions{})) << quelle->lastError();
            ASSERT_TRUE(b->insert(z.get(), r, TransferOptions{})) << b->lastError();
        }
        ASSERT_TRUE(b->flush());
    }

    auto cfg = A5120Machine::Config::fuerAusfuehrung(A::Alt);
    cfg.laderom = L::V06;
    cfg.drive_profiles = {"K5600.20", "K5600.20", "K5600.20", "none"};
    A5120Machine m(cfg);
    m.powerOn();
    ASSERT_TRUE(m.mountDisk(0, kombi.get(), "k5601_ss80_26x128", false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, arbeit.get(), "k5601_ss80_26x128", false)) << m.lastError();
    const uint32_t ende = K7636::QK_TASTE_BASE | 0x37;
    bisPrompt(m, ende);
    befehl(m, "dir a:", ende);
    k1520test::runCycles(m, 20'000'000);
    EXPECT_NE(bild(m).find("GROSS    COM"), std::string::npos) << "A: ab Zylinder 5:\n" << bild(m);
    befehl(m, "dir b:", ende);
    k1520test::runCycles(m, 20'000'000);
    EXPECT_NE(bild(m).find("PIP      COM"), std::string::npos) << "B: ab Zylinder 2:\n" << bild(m);

    befehl(m, "b:pip a:kopie.com=a:gross.com", ende);
    k1520test::runCycles(m, 400'000'000);
    befehl(m, "dir a:", ende);
    k1520test::runCycles(m, 30'000'000);
    EXPECT_NE(bild(m).find("KOPIE    COM"), std::string::npos) << "nach PIP:\n" << bild(m);
    ASSERT_TRUE(m.flushDisks()) << m.lastError();

    auto a = oeffne(kombi.get());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->detection().filesystem, "bcu880_kombi80");
    auto b = oeffne(arbeit.get());
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->detection().filesystem, "bcu880_80");   // nicht als CP/A-400K gelesen (2-KB-Bloecke!)
    for (const char* n : {"GROSS.COM", "KOPIE.COM"}) {
        TempPfad x("k1520_bcu_gross_aus.bin");
        FileRef r;
        r.name = n;
        ASSERT_TRUE(a->extract(r, x.get(), TransferOptions{})) << n << ": " << a->lastError();
        EXPECT_EQ(datei(x.get()), gross) << n;
    }
}

TEST(DisktoolBcu, Kombi40Und80AbbilderSindNichtAustauschbar) {
    TempPfad zu("k1520_bcu_kombi_spuren.img");
    std::string fehler;
    EXPECT_EQ(baue(zu.get(), "bcu880_kombi", bootbin("boot_bcu12_kombi80_k7023.bin"), &fehler), nullptr);
    EXPECT_NE(fehler.find("Spurzahl"), std::string::npos) << fehler;
    EXPECT_EQ(baue(zu.get(), "bcu880_kombi80", bootbin("boot_bcu12_kombi_k7023.bin"), &fehler), nullptr);
    EXPECT_NE(fehler.find("Spurzahl"), std::string::npos) << fehler;
}

TEST(DisktoolBcu, Arbeitsdiskette80VerlangtEinBootabbild) {
    TempPfad p("k1520_bcu_80_ohne.img");
    std::string fehler;
    EXPECT_EQ(baue(p.get(), "bcu880_80", "", &fehler), nullptr);
    EXPECT_NE(fehler.find("--boot"), std::string::npos) << fehler;
    EXPECT_FALSE(fs::exists(p.get()));
}
