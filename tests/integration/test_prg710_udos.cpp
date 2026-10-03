/**
 * @file test_prg710_udos.cpp
 * @brief PRG 710 / 710-1 Etappe 3 (doc/design/20_prg710.md AP-P3): UDOS 4.3 von der
 *        Gerätediskette bis zur Datumsabfrage und zum `%`-Prompt; `CAT`, `DATE` und
 *        `COPY` auf Laufwerk 1.
 *
 * Ladekette (§5.1): Starttaste → ROM liest den Bootsektor (Spur 0) nach 0400H → der
 * Bootsektor liest mit `03FDH` den Zweitlader (Spur 2, 26 × 128 B) nach 1000H → Resident
 * 0000–0BFFH → BOOT-Modul (Spur 21) → `OS`/`ZDOS` → `OS.INIT` → Datumsabfrage → `%`.
 *
 * **Befunde aus AP-P3:**
 * - „DISKERROR C6“ am Zweitlader des PRG 710 war ein Fehler der K5122-Einstellung
 *   `setMkeJedesSyncByte`: stand der Kopf beim Scharfmachen im ERSTEN A1 einer Gruppe, galt
 *   die Marke sofort, im Daten-PIO lag aber noch das 00H davor — das ROM las es als
 *   Datenmarke, und der erste Sektor kam um die Sync-Bytes verschoben an (CRC falsch,
 *   16 Lesungen gleich).  Jetzt gilt „sofort“ nur, wenn das zuletzt FERTIGE Byte ein
 *   Sync-Byte ist (Wächter `K5122Wait.MkeJedesSyncByte_ErstesInLiefertDasSyncByte`,
 *   hier `Boot01ZweitladerOhneCrcFehler`).
 * - Die Diskette `PRG710_UDOS43_Boot01` ist nur **einseitig** abgezogen (HFE-Kopf: 1 Seite,
 *   dazu Zylinder 28/46/50/51/77 leer).  Der Resident liest beim Start Spur 23 der Seite 1
 *   (Laufwerk 4 = Seite 1 von Laufwerk 0); bis AP-P3b setzte das Marken-FF dort nie und der
 *   Treiber wartete ohne Zeitablauf (0A5CH).  Seit AP-P3b liegt auf der leeren Seite
 *   Rauschen (`K5122::setRauschenAufLeererSpur`), der Lesezugriff kehrt mit Fehler zurück
 *   und die Diskette bootet (`Boot01EinseitigBisZumPrompt`).  Die übrigen UDOS-Läufe am
 *   710 benutzen weiter `PRG710_UDOS43_MRS_Boot` (beidseitig, kein Umweg über das Rauschen).  Am 710-1 trägt die Gerätediskette
 *   `PRG710-1_UDOS_Boot` weder `DATE` noch `COPY` (OS.INIT endet in „NONEXISTENT
 *   COMMAND“); dort `UDOS.PRG710-1_V4.3_1_89`.
 * - Am 710-1 sind alle Dateien „geheim“ (S): `CAT` ohne `P=&` meldet „FILE NOT FOUND“,
 *   und `COPY` überträgt die Eigenschaft auf die Kopie.
 */

#include <gtest/gtest.h>

#include <cstdio>
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

constexpr int       kSchritt = 20'000;
constexpr long long kBoot    = 120'000'000;   // Netz-Ein → Datumsabfrage: ≈ 45 Mio. Takte
constexpr long long kBefehl  =  60'000'000;   // je Kommando (Diskettenzugriffe)
constexpr uint32_t  QK_RETURN = 0x01000004;   // Qt::Key_Return (710: ET1 = 37H, 710-1: 0DH)

const char* diskette(V v) {
    return v == V::Prg710_1 ? "prg710-1_udos43_k5601_v43_189.hfe"
                            : "prg710_udos43_k5601_mrs_boot.hfe";
}
const char* systemzeile(V v) { return v == V::Prg710_1 ? "UDOS PG710-1" : "UDOS PRG710"; }

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
/// Letzte nicht leere Zeile des Bildes.
std::string letzteZeile(Prg710Machine& m) {
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (!z.empty()) return z;
    }
    return {};
}
/// Vorletzte nicht leere Zeile (die letzte Ausgabe vor dem `%`).
std::string vorletzteZeile(Prg710Machine& m) {
    bool erste = true;
    for (int r = 23; r >= 0; --r) {
        std::string z = zeile(m, r);
        if (z.empty()) continue;
        if (!erste) return z;
        erste = false;
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
/// Bis die letzte Zeile genau `%` ist und 2 Mio. Takte so bleibt (Ausgabe fertig).
bool bisPrompt(Prg710Machine& m, long long frist) {
    long long ruhig = 0;
    for (long long d = 0; d < frist;) {
        const int n = m.run(kSchritt);
        d += n;
        ruhig = (letzteZeile(m) == "%") ? ruhig + n : 0;
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
/// Kommando + ET bzw. ENTER, dann bis zum nächsten `%`.
bool kommando(Prg710Machine& m, const std::string& k) {
    tippe(m, k);
    taste(m, QK_RETURN);
    return bisPrompt(m, kBefehl);
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

/// Inhalt einer Datei auf Seite 0 einer UDOS-Diskette (über das DiskTool).
std::vector<uint8_t> udosDatei(const std::string& abbild, const std::string& name) {
    std::string err;
    auto dv = DiskVolume::open(abbild, "udos_ds77", formate(), dateisysteme(), err);
    EXPECT_NE(dv, nullptr) << err;
    if (!dv) return {};
    TempPfad ziel("prg710_udos_datei.bin");
    FileRef ref;
    ref.name = name;
    EXPECT_TRUE(dv->extract(ref, ziel.get(), TransferOptions{})) << dv->lastError();
    return datei(ziel.get());
}

}  // namespace

class Prg710Udos : public ::testing::TestWithParam<V> {
protected:
    void SetUp() override {
        k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    }
    /// Netz-Ein → Starttaste → Datumsabfrage → Datum 02.10.(19)86 → `%`.
    void booteBisPrompt(Prg710Machine& m) {
        m.powerOn();
        lauf(m, 3'000'000);                          // ROM bis zur Tastaturabfrage
        taste(m, QK_RETURN);
        ASSERT_TRUE(bisText(m, "Neues Datum", kBoot))
            << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
        tippe(m, "021086");                          // Maske __.__.19__, kein ET nötig
        ASSERT_TRUE(bisPrompt(m, kBefehl)) << bild(m);
        ASSERT_NE(bild(m).find("Donnerstag, der 2. Oktober 1986"), std::string::npos) << bild(m);
        ASSERT_NE(bild(m).find(systemzeile(GetParam())), std::string::npos) << bild(m);
    }
};

/**
 * @test Prg710Udos.BisZumPrompt
 * @brief Starttaste → UDOS 4.3 lädt (Zweitlader, Resident, BOOT, OS, ZDOS), `OS.INIT`
 *        fragt das Datum ab, danach Wochentag, Systemzeile („UDOS PRG710“ bzw. „UDOS
 *        PG710-1“) und `%`.  Die Speicherverwaltung bleibt im Arbeitsmodell §4b (Seiten
 *        0–E OPS-RAM in Identität; Seite F schaltet der Resident je Bildzugriff um).
 */
TEST_P(Prg710Udos, BisZumPrompt) {
    k1520test::TempDisk disk(diskette(GetParam()));
    Prg710Machine::Config c;
    c.variante = GetParam();
    Prg710Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    booteBisPrompt(m);
    EXPECT_EQ(letzteZeile(m), "%");
    EXPECT_EQ(vorletzteZeile(m), systemzeile(GetParam()));
    EXPECT_EQ(m.speicher().freigabe(), 0x0F);
    for (int n = 0; n < 15; ++n) {
        EXPECT_EQ(m.speicher().attr(n) & 0x0F, 0) << "Seite " << n << " = OPS-RAM";
        EXPECT_EQ(m.speicher().seite(n), n);
    }
}

/**
 * @test Prg710Udos.CatUndDate
 * @brief `CAT D=0 P=& *DOS` (auch die geheimen Dateien) zeigt `ZDOS` auf Laufwerk 0,
 *        `DATE` den gesetzten Tag, `DATE 861003` setzt ihn um.
 */
TEST_P(Prg710Udos, CatUndDate) {
    k1520test::TempDisk disk(diskette(GetParam()));
    Prg710Machine::Config c;
    c.variante = GetParam();
    Prg710Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    booteBisPrompt(m);

    ASSERT_TRUE(kommando(m, "CAT D=0 P=& *DOS")) << bild(m);
    EXPECT_NE(bild(m).find(" FILENAME         DRIVE"), std::string::npos) << bild(m);
    EXPECT_NE(bild(m).find("ZDOS                 0"), std::string::npos) << bild(m);

    ASSERT_TRUE(kommando(m, "DATE")) << bild(m);
    EXPECT_EQ(vorletzteZeile(m), "Donnerstag, der 2. Oktober 1986") << bild(m);
    ASSERT_TRUE(kommando(m, "DATE 861003")) << bild(m);
    EXPECT_EQ(vorletzteZeile(m), "Freitag, der 3. Oktober 1986") << bild(m);
}

/**
 * @test Prg710Udos.KopiertAufLaufwerk1
 * @brief Laufwerk 1 trägt eine vom DiskTool frisch angelegte, leere UDOS-Diskette
 *        (`DiskVolume::create`, `udos_ds77`, Name „LEER“ — UDOS braucht Verzeichnis und
 *        Belegungsplan, die `createDisk` mit Format allein nicht schreibt).  `STATUS 1`
 *        liest sie, `COPY OS.INIT 1/KOPIE` schreibt im `/WAIT`-Betrieb, `CAT D=1 P=&`
 *        zeigt die Kopie, und das DiskTool findet sie danach bytegleich zum Original.
 */
TEST_P(Prg710Udos, KopiertAufLaufwerk1) {
    k1520test::TempDisk disk(diskette(GetParam()));
    TempPfad zweit("prg710_udos_lw1.hfe");
    {
        std::string err;
        auto neu = DiskVolume::create(zweit.get(), "udos_ds77", "LEER", formate(),
                                      dateisysteme(), err);
        ASSERT_NE(neu, nullptr) << err;
        ASSERT_TRUE(neu->flush()) << neu->lastError();
    }
    Prg710Machine::Config c;
    c.variante = GetParam();
    Prg710Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, zweit.get(), m.defaultFormatName(1), false)) << m.lastError();
    booteBisPrompt(m);

    ASSERT_TRUE(kommando(m, "STATUS 1")) << bild(m);
    EXPECT_NE(bild(m).find("DRIVE 1   LEER"), std::string::npos) << bild(m);
    ASSERT_TRUE(kommando(m, "COPY OS.INIT 1/KOPIE")) << bild(m);
    EXPECT_EQ(vorletzteZeile(m), "%COPY OS.INIT 1/KOPIE") << "keine Fehlermeldung\n" << bild(m);
    ASSERT_TRUE(kommando(m, "CAT D=1 P=&")) << bild(m);
    EXPECT_NE(bild(m).find("KOPIE                1"), std::string::npos) << bild(m);

    ASSERT_TRUE(m.flushDisks()) << m.lastError();
    const std::vector<uint8_t> original = udosDatei(disk, "OS.INIT");
    const std::vector<uint8_t> kopie    = udosDatei(zweit.get(), "KOPIE");
    ASSERT_FALSE(original.empty());
    EXPECT_EQ(kopie, original);
}

/**
 * @test Prg710Udos.Boot01ZweitladerOhneCrcFehler
 * @brief Gerätediskette `PRG710_UDOS43_Boot01` am PRG 710: der Bootsektor liest den
 *        Zweitlader (Spur 2, 26 × 128 B, Parameterblock 0422H) mit Status 80H und ohne
 *        eine einzige CRC-Wiederholung — vor AP-P3 „DISKERROR C6“ (Marken-FF, s.
 *        Dateikopf).  Bis zum Prompt: `Boot01EinseitigBisZumPrompt`.
 */
TEST(Prg710Udos, Boot01ZweitladerOhneCrcFehler) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    k1520test::TempDisk disk("prg710_udos43_k5601_boot01.hfe");
    Prg710Machine m;
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    lauf(m, 3'000'000);
    int gut = 0, crc = 0;
    m.setCpuTraceCallback([&](const Z80& z) {
        if (z.IY != 0x0422) return;                  // Parameterblock des Bootsektors
        if (z.PC == 0x037E) { ++gut; m.stop(); }     // alle CRCs gut → Status 80H
        if (z.PC == 0x038B) ++crc;                   // CRC_FALSCH
    });
    taste(m, QK_RETURN);
    for (long long d = 0; gut == 0 && d < kBoot;) d += m.run(kSchritt);
    m.setCpuTraceCallback(nullptr);
    EXPECT_EQ(gut, 1) << bild(m);
    EXPECT_EQ(crc, 0) << "keine einzige Wiederholung wegen der Daten-CRC";
    EXPECT_EQ(m.memReadDebug(0x1000), 0x31) << "Zweitlader (Spur 2 Sektor 1) beginnt mit 31H";
}

/**
 * @test Prg710Udos.UnformatierteDisketteMeldetFehlerUndBootetNachWechsel
 * @brief AP-P3b: eine UNFORMATIERTE Diskette in Laufwerk 0.  Am Gerät kommt nach der
 *        Starttaste eine Meldung, danach bootet eine eingelegte Bootdiskette mit der
 *        Starttaste (Anwender, 2026-10-02).  Das ROM wartet in 02DDH ohne Zeitablauf auf
 *        eine Marke (CTC-Kanal 3 ist beim Lesen gestoppt, 0195H); beendet wird das erst
 *        durch das Rauschen der leeren Spur (`K5122::setRauschenAufLeererSpur`): hinter
 *        einer Scheinmarke steht irgendwann ein FEH, der Spurvergleich scheitert
 *        (26 Köpfe × 3 Neukalibrierungen) → „DISKERROR C5“.  Vor AP-P3b hing es.
 */
TEST_P(Prg710Udos, UnformatierteDisketteMeldetFehlerUndBootetNachWechsel) {
    TempPfad leer("prg710_unformatiert.hfe");
    k1520test::TempDisk boot(diskette(GetParam()));
    Prg710Machine::Config c;
    c.variante = GetParam();
    Prg710Machine m(c);
    ASSERT_TRUE(m.createDisk(0, leer.get(), "", false)) << m.lastError();

    // Tastaturabfrage des ROMs (aus der RAM-Kopie): 710 007DH, 710-1 0070H.
    const uint16_t taste_warten = GetParam() == V::Prg710_1 ? 0x0070 : 0x007D;
    bool wartet = false;
    m.setCpuTraceCallback([&](const Z80& z) { if (z.PC == taste_warten) wartet = true; });
    auto bisTastenabfrage = [&](long long frist) {
        wartet = false;
        for (long long d = 0; !wartet && d < frist;) d += m.run(kSchritt);
        return wartet;
    };
    m.powerOn();
    // Der 710 liest vor der Taste beide Laufwerke (0097H) — auf der leeren Diskette
    // dauert das, bis das Rauschen den Fehler liefert.
    ASSERT_TRUE(bisTastenabfrage(400'000'000)) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "DISKERROR", 400'000'000))
        << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    EXPECT_NE(bild(m).find("DISKERROR C5"), std::string::npos) << bild(m);

    // Diskette wechseln (gleich nach der Meldung — der 710 liest nach „NO SYSTEM“ vor der
    // Taste wieder beide Laufwerke), Starttaste: UDOS bootet.
    ASSERT_TRUE(m.unmountDisk(0));
    ASSERT_TRUE(m.mountDisk(0, boot, m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(bisTastenabfrage(100'000'000)) << bild(m);
    m.setCpuTraceCallback(nullptr);
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "Neues Datum", kBoot)) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
}

/**
 * @test Prg710Udos.StatusMitEinseitigerDisketteInLaufwerk1
 * @brief AP-P3b: in Laufwerk 1 liegt eine nur EINSEITIG formatierte UDOS-Diskette
 *        (`PRG710_UDOS43_Boot01`, Seite 1 unformatiert).  `STATUS 1` liest auch die
 *        Rückseite (Laufwerk 5); auf der Seite ohne Marken wartete der Resident ohne
 *        Zeitablauf (0A5CH/0A85H).  Mit dem Rauschen der leeren Seite kommt er zurück
 *        und zeigt die Diskette.
 */
TEST_P(Prg710Udos, StatusMitEinseitigerDisketteInLaufwerk1) {
    k1520test::TempDisk disk(diskette(GetParam()));
    k1520test::TempDisk einseitig("prg710_udos43_k5601_boot01.hfe");
    Prg710Machine::Config c;
    c.variante = GetParam();
    Prg710Machine m(c);
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_TRUE(m.mountDisk(1, einseitig, m.defaultFormatName(1), false)) << m.lastError();
    booteBisPrompt(m);
    ASSERT_TRUE(kommando(m, "STATUS 1")) << bild(m);
    EXPECT_NE(bild(m).find("DRIVE 1"), std::string::npos) << bild(m);
}

/**
 * @test Prg710Udos.Boot01EinseitigBisZumPrompt
 * @brief AP-P3b: `PRG710_UDOS43_Boot01` ist nur einseitig abgezogen.  Der Resident liest
 *        beim Start Spur 23 der Seite 1 (Laufwerk 4); seit dem Rauschen der leeren Seite
 *        kehrt der Lesezugriff mit Fehlerstatus zurück, und UDOS bootet wie am Gerät
 *        („die Rückseite ist egal“) bis zum `%`.
 */
TEST(Prg710Udos, Boot01EinseitigBisZumPrompt) {
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);
    k1520test::TempDisk disk("prg710_udos43_k5601_boot01.hfe");
    Prg710Machine m;
    ASSERT_TRUE(m.mountDisk(0, disk, m.defaultFormatName(0), false)) << m.lastError();
    m.powerOn();
    lauf(m, 3'000'000);
    taste(m, QK_RETURN);
    ASSERT_TRUE(bisText(m, "Neues Datum", kBoot)) << "PC=" << std::hex << m.cpuPC() << "\n" << bild(m);
    tippe(m, "021086");
    ASSERT_TRUE(bisPrompt(m, kBefehl)) << bild(m);
    EXPECT_NE(bild(m).find("UDOS PRG710"), std::string::npos) << bild(m);
}

INSTANTIATE_TEST_SUITE_P(Varianten, Prg710Udos, ::testing::Values(V::Prg710, V::Prg710_1),
                         [](const auto& i) { return i.param == V::Prg710 ? "Prg710" : "Prg710_1"; });
