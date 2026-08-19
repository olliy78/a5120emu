/**
 * @file test_fs_check.cpp
 * @brief GoogleTests der Dateisystempruefung — Etappe 1 (CP/M).
 *
 * Die Reihenfolge in dieser Datei ist Absicht und entspricht E10 des Entwurfs:
 *
 *   1. **Zuerst der Waechter gegen Falschmeldungen.**  Jede unversehrte Diskette im
 *      Baum und jedes anlegbare Katalogformat muss ohne Befund durchlaufen.  Ein
 *      `fsck`, das bei gesunden Disketten meckert, wird weggeklickt und ist damit
 *      wertlos — auch dann, wenn es spaeter einmal recht hat.
 *   2. **Dann die Schadensinjektion.**  Je Schaden ein Fall nach demselben
 *      Dreischritt: genau der erwartete Befund, kein zusaetzlicher, und die
 *      unbeteiligten Dateien lesen sich Byte fuer Byte wie vorher.
 *
 * Beschaedigt wird ueber die **Datei**, nicht ueber den Pruefcode: bei einem `.img`
 * ist der lineare Sektorraum bitgleich der Datei (`SectorSpace`-Zusage), ein
 * Verzeichnisplatz laesst sich also mit `pwrite` treffen.  So haengt der Test nicht
 * an dem, was er prueft.
 *
 * @see core/filesystem/check/fs_check.h · doc/design/15_dateisystempruefung.md §18
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/check/fs_check.h"
#include "core/filesystem/disk_volume.h"
#include "tests/support/temp_path.h"

namespace fs = std::filesystem;

namespace {

/// @brief Byte-Offset des CP/M-Verzeichnisses auf einer `cpa780`-Diskette.
///        Nachgemessen (doc/design/13_k1520disktool.md §6.2): Anfang c2h0.
constexpr uint64_t kCpa780Dir = 15104;
constexpr size_t   kPlatz     = 32;

std::string fixture(const char* name) {
    return (fs::path(FIXTURE_DIR) / name).string();
}

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string f;
        FormatCatalog k = FormatCatalog::load({K1520_FORMATS_DEFAULT}, &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string f;
        FsCatalog k = FsCatalog::load({K1520_FORMATS_DEFAULT}, formate(), &f);
        EXPECT_TRUE(f.empty()) << f;
        return k;
    }();
    return c;
}

std::unique_ptr<DiskVolume> oeffne(const std::string& pfad) {
    std::string err;
    auto v = DiskVolume::open(pfad, "", formate(), dateisysteme(), err);
    EXPECT_TRUE(v) << pfad << ": " << err;
    return v;
}

/**
 * @brief Eine BESCHAEDIGTE Diskette oeffnen — mit erzwungenem Profil.
 *
 * Notwendig, und das ist selbst eine Aussage: die Erkennung prueft den ersten
 * Verzeichnisplatz auf Plausibilitaet und weist eine Diskette mit zerstoertem
 * Verzeichnisanfang **ab** — die Pruefung kommt dann gar nicht erst zum Zug
 * (§20 „Eine Diskette, die nicht mountet, wird nicht geprueft").  Der Weg hinein
 * ist derselbe, den auch ein Anwender nimmt: `--fs cpa780`.
 */
std::unique_ptr<DiskVolume> oeffneErzwungen(const std::string& pfad) {
    std::string err;
    auto v = DiskVolume::open(pfad, "cpa780", formate(), dateisysteme(), err);
    EXPECT_TRUE(v) << pfad << ": " << err;
    return v;
}

/// @brief Eine Fixture nach /tmp kopieren, damit der Test sie beschaedigen darf.
std::string kopie(const char* fixture_name, const std::string& als) {
    const std::string ziel = (fs::temp_directory_path() / als).string();
    fs::remove(ziel);
    fs::copy_file(fixture(fixture_name), ziel);
    return ziel;
}

std::vector<uint8_t> lies(const std::string& pfad, uint64_t off, size_t n) {
    std::ifstream f(pfad, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(off));
    std::vector<uint8_t> v(n);
    f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n));
    return v;
}

void schreib(const std::string& pfad, uint64_t off, const std::vector<uint8_t>& v) {
    std::fstream f(pfad, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(static_cast<std::streamoff>(off));
    f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}

/// @brief Nummer des ersten belegten Verzeichnisplatzes einer cpa780-Diskette.
int ersterPlatz(const std::string& pfad) {
    const std::vector<uint8_t> dir = lies(pfad, kCpa780Dir, 128 * kPlatz);
    for (int i = 0; i < 128; ++i)
        if (dir[static_cast<size_t>(i) * kPlatz] <= 15) return i;
    return -1;
}

uint64_t platzOffset(int i) { return kCpa780Dir + static_cast<uint64_t>(i) * kPlatz; }

/// @brief Alle Befunde mit dieser Kennung.
std::vector<FsFinding> mitId(const FsCheckReport& r, const std::string& id) {
    std::vector<FsFinding> out;
    for (const FsFinding& f : r.findings) if (f.id == id) out.push_back(f);
    return out;
}

/// @brief Alle Kennungen als eine Zeichenkette — fuer aussagekraeftige Fehlermeldungen.
std::string kennungen(const FsCheckReport& r) {
    std::string s;
    for (const FsFinding& f : r.findings)
        s += (s.empty() ? "" : ", ") + f.id + " (" + fsSeverityName(f.severity) + ": "
           + f.text + ")";
    return s;
}

/// @brief Inhaltsverzeichnis + alle Dateiinhalte — die Gegenprobe „nichts kaputtgemacht".
std::vector<std::pair<std::string, uint64_t>> bestand(DiskVolume& v) {
    std::vector<std::pair<std::string, uint64_t>> out;
    for (const FileEntry& e : v.list()) out.emplace_back(e.qualifiedName(), e.size);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

// ═══ 1. Keine Falschmeldungen (E10) ═══════════════════════════════════════════

/**
 * @test Jede unversehrte Diskette im Baum prueft ohne Befund.
 *
 * Das ist der wichtigste Test des ganzen Vorhabens.  Ausgenommen ist genau eine
 * Fixture, und zwar mit Beleg: der A7100-Referenzdatentraeger ist WIRKLICH
 * beschaedigt (`doc/design/13_k1520disktool.md` §22: „sein Bootspur-Sektor 10 ist
 * beschaedigt") — dort ist ein Befund die richtige Antwort, und der naechste Test
 * nagelt ihn fest.
 */
TEST(FsCheckKeineFalschmeldungen, JedeUnversehrteFixturePrueftOhneBefund) {
    static const char* disketten[] = {
        "cpa_cpa780_k5601_clock.hfe",   "cpa_cpa780_k5601_noclock.hfe",
        "cpa_cpa780_k5601_clock.img",   "cpa_cpa780_k5601_noclock.img",
        "cpa_cpa780_combo5zoll_noclock.img", "cpa_cpa780_combo8zoll_noclock.img",
        "scpx17_cpa780_k5601.hfe",      "scpx17_5x1024_k5601_hardy.hfe",
        // Die UDOS-Sitten haben (Etappe 1) noch keinen Pruefer — sie muessen
        // schweigen, und genau das wird hier mitgeprueft.
        "udos_boot_scp.hfe",            "udos_ds77_k5601_fremdsync.hfe",
        "udos1715_640k_pc1715_system.img", "udosP8000_640k_wega.hfe",
    };

    for (const char* name : disketten) {
        auto v = oeffne(fixture(name));
        ASSERT_TRUE(v) << name;

        for (FsCheckLevel stufe : {FsCheckLevel::Schnell, FsCheckLevel::Voll}) {
            const FsCheckReport& r = v->check(stufe, true);
            EXPECT_TRUE(r.vollstaendig) << name;
            EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung))
                << name << " (" << (stufe == FsCheckLevel::Voll ? "voll" : "schnell")
                << "): " << kennungen(r);
        }
    }
}

/// @test Der bekannte Schaden des A7100-Datentraegers wird gefunden — und benannt.
///
/// Die Erkennung meldet heute „1 Sektor mit CRC-Fehler", ohne zu sagen, welcher.
/// Genau diese Luecke schliesst die Pruefung.
TEST(FsCheckKeineFalschmeldungen, DerBekannteSchadenDesA7100TraegersWirdGenannt) {
    auto v = oeffne(fixture("scp1700_640k_a7100_system.hfe"));
    ASSERT_TRUE(v);

    // Die Schnellpruefung fasst die Systemspuren nicht an — sie gehoeren nicht zum
    // Dateisystem, und ihr Zustand kostet einen eigenen Spurdurchlauf.
    EXPECT_EQ(0, v->check(FsCheckLevel::Schnell, true).zaehlerAb(FsSeverity::Warnung));

    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> sys = mitId(r, "cpm.medium.systemspur");
    ASSERT_EQ(2u, sys.size()) << kennungen(r);
    for (const FsFinding& f : sys) {
        EXPECT_EQ(FsSeverity::Warnung, f.severity);
        EXPECT_EQ(FsLayer::Medium, f.layer);
        EXPECT_EQ(0, f.cyl);          // die FM-Bootspur c0h0
        EXPECT_EQ(0, f.head);
    }
    EXPECT_NE(std::string::npos, sys[0].text.find("Sektor 5"))  << sys[0].text;
    EXPECT_NE(std::string::npos, sys[1].text.find("Sektor 10")) << sys[1].text;
    // Und sonst nichts: das Dateisystem selbst ist heil.
    EXPECT_EQ(2, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
}

/// @test Jedes anlegbare CP/M-Katalogprofil prueft frisch angelegt ohne Befund.
///
/// Gegenstueck zu `DiskVolume.JedesKatalogformatLaesstSichAnlegenUndWiederOeffnen`:
/// dort geht es um die Erkennung, hier um die Pruefung.  Eine soeben angelegte
/// Diskette ist der einzige Zustand, von dem man mit Sicherheit weiss, dass er
/// stimmt — meldet die Pruefung dort etwas, ist die Pruefung falsch.
TEST(FsCheckKeineFalschmeldungen, JedesAngelegteCpmProfilPrueftOhneBefund) {
    int geprueft = 0;
    for (const FsProfile& p : dateisysteme().profiles()) {
        if (p.type != FsType::Cpm) continue;
        const std::string ext  = p.allow_hfe ? ".hfe" : ".img";
        const std::string ziel = (fs::temp_directory_path()
                                  / ("fscheck_neu_" + p.name + ext)).string();
        fs::remove(ziel);

        std::string err;
        auto v = DiskVolume::create(ziel, p.name, "", formate(), dateisysteme(), err);
        ASSERT_TRUE(v) << p.name << ": " << err;

        const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
        EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << p.name << ": " << kennungen(r);
        ++geprueft;
        v.reset();
        fs::remove(ziel);
    }
    EXPECT_GT(geprueft, 3) << "Es wurde kaum ein Profil geprueft — Katalog leer?";
}

// ═══ 2. Der Bericht selbst ════════════════════════════════════════════════════

/// @test Die Schnellpruefung laeuft beim OEFFNEN, ohne dass jemand danach fragt.
TEST(FsCheckAutomatik, DasOeffnenPruefRSchonSelbst) {
    auto v = oeffne(fixture("cpa_cpa780_k5601_noclock.img"));
    ASSERT_TRUE(v);
    EXPECT_EQ(FsCheckLevel::Schnell, v->checkReport().level);
    EXPECT_TRUE(v->checkReport().vollstaendig);
    EXPECT_TRUE(v->checkReport().ohneBefund()) << kennungen(v->checkReport());
}

/// @test Die Zahlenwerte der Schweregrade sind ein Vertrag (E5) — sie gehen als
///       `int` ueber die C-ABI und stehen in `--json`.
TEST(FsCheckVertrag, SchweregradeUndEbenenSindStabil) {
    EXPECT_EQ(0, static_cast<int>(FsSeverity::Info));
    EXPECT_EQ(1, static_cast<int>(FsSeverity::Warnung));
    EXPECT_EQ(2, static_cast<int>(FsSeverity::Fehler));
    EXPECT_EQ(3, static_cast<int>(FsSeverity::Gefahr));
    EXPECT_EQ(0, static_cast<int>(FsLayer::Medium));
    EXPECT_EQ(1, static_cast<int>(FsLayer::Verwaltung));
    EXPECT_EQ(2, static_cast<int>(FsLayer::Dateien));
    EXPECT_STREQ("Gefahr", fsSeverityName(FsSeverity::Gefahr));
    EXPECT_STREQ("Verwaltung", fsLayerName(FsLayer::Verwaltung));
}

/// @test Sortierung, Zaehlung und Kurzfassung.
TEST(FsCheckVertrag, BerichtOrdnetNachSchwere) {
    FsCheckReport r;
    FsFindings b(r);
    b.add("a.info", FsSeverity::Info, FsLayer::Verwaltung, "", "");
    b.add("b.gefahr", FsSeverity::Gefahr, FsLayer::Dateien, "", "");
    b.add("c.warnung", FsSeverity::Warnung, FsLayer::Medium, "", "");
    r.sortieren();

    ASSERT_EQ(3u, r.findings.size());
    EXPECT_EQ("b.gefahr", r.findings[0].id);
    EXPECT_EQ("c.warnung", r.findings[1].id);
    EXPECT_EQ("a.info", r.findings[2].id);
    EXPECT_EQ(FsSeverity::Gefahr, r.hoechste());
    EXPECT_EQ(2, r.zaehlerAb(FsSeverity::Warnung));
    EXPECT_EQ("1 Gefahr, 1 Warnung, 1 Hinweis", r.kurzfassung());
    EXPECT_TRUE(FsCheckReport{}.kurzfassung().empty());
}

// ═══ 3. Schadensinjektion ═════════════════════════════════════════════════════

/// @test Ein Blockzeiger hinter den Datenbereich — der klassische Hinweis auf das
///       falsche Dateisystemprofil.
TEST(FsCheckCpmSchaden, WilderBlockzeiger) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_wild.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);

    // Vergleichsstand VOR dem Schaden.
    std::vector<std::pair<std::string, uint64_t>> vorher;
    { auto v = oeffneErzwungen(d); ASSERT_TRUE(v); vorher = bestand(*v); }

    // cpa780 hat 390 Bloecke → 16-Bit-Zeiger; der erste steht bei Offset 16.
    schreib(d, platzOffset(slot) + 16, {0x0F, 0x27});          // 9999

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.block.ausserhalb");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    EXPECT_EQ(FsLayer::Verwaltung, f[0].layer);
    EXPECT_NE(std::string::npos, f[0].text.find("9999")) << f[0].text;

    // Die uebrigen Dateien stehen unveraendert da.
    EXPECT_EQ(vorher.size(), bestand(*v).size());
    fs::remove(d);
}

/// @test Derselbe Block in zwei Eintraegen — der einzige `Gefahr`-Befund, den CP/M
///       ueberhaupt hervorbringen kann (es gibt keinen gespeicherten Belegungsplan).
TEST(FsCheckCpmSchaden, KreuzbelegungIstGefahr) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_kreuz.img");
    const int a = ersterPlatz(d);
    ASSERT_GE(a, 0);
    int bslot = -1;
    for (int i = a + 1; i < 128; ++i) {
        if (lies(d, platzOffset(i), 1)[0] <= 15) { bslot = i; break; }
    }
    ASSERT_GE(bslot, 0) << "Fixture hat nur eine Datei";

    // Den ersten Blockzeiger von A auch in B eintragen.
    schreib(d, platzOffset(bslot) + 16, lies(d, platzOffset(a) + 16, 2));

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.block.doppelt");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    EXPECT_EQ(FsSeverity::Gefahr, r.hoechste());
    fs::remove(d);
}

/// @test Ein Blockzeiger in den Verzeichnisbereich ist ebenfalls `Gefahr`: ein
///       Schreibvorgang auf diese Datei wuerde das Verzeichnis ueberschreiben.
TEST(FsCheckCpmSchaden, BlockzeigerInsVerzeichnis) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_dirblock.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot) + 16, {0x01, 0x00});          // Block 1 = Verzeichnis

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.block.verzeichnis");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    fs::remove(d);
}

/// @test Ein Platz voller Muell — Steuerzeichen im Namen.
TEST(FsCheckCpmSchaden, SteuerzeichenImNamen) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_name.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot) + 1, {0x01, 0x02, 0x03});

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.dir.name");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    fs::remove(d);
}

/// @test Kleinbuchstaben sind nur eine Warnung — die Datei ist lesbar, aber am CCP
///       nicht einzugeben.
TEST(FsCheckCpmSchaden, KleinbuchstabenSindNurEineWarnung) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_klein.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot) + 1, {'t', 'e', 's', 't'});

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.dir.name");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, f[0].severity);
    fs::remove(d);
}

/// @test Ein Nutzerbyte, das weder Nutzerbereich noch Sondersatz ist.
TEST(FsCheckCpmSchaden, UnbekanntesNutzerbyte) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_user.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot), {0x40});

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    ASSERT_EQ(1u, mitId(r, "cpm.dir.user").size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, mitId(r, "cpm.dir.user")[0].severity);
    fs::remove(d);
}

/// @test Ein CP/M-3-Zeitstempelsatz ist KEIN Fehler, sondern ein Hinweis — sonst
///       meldete die Pruefung auf jeder Diskette mit Zeitstempeln 32 Fehler (E10).
TEST(FsCheckCpmSchaden, SondersaetzeSindNurEinHinweis) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_sonder.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot), {0x21});          // Zeitstempelsatz

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
    ASSERT_EQ(1u, mitId(r, "cpm.dir.sonderplatz").size()) << kennungen(r);
    fs::remove(d);
}

/// @test Satzzahl groesser als ein Extent fassen kann.
TEST(FsCheckCpmSchaden, SatzzahlZuGross) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_rc.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot) + 15, {200});

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    ASSERT_EQ(1u, mitId(r, "cpm.dir.rc").size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, mitId(r, "cpm.dir.rc")[0].severity);
    fs::remove(d);
}

/// @test Der Anfang einer Datei fehlt (sie beginnt bei Extent 5).
TEST(FsCheckCpmSchaden, FehlenderDateianfang) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_extent.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot) + 12, {5});        // EX = 5

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    ASSERT_EQ(1u, mitId(r, "cpm.dir.extentluecke").size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, mitId(r, "cpm.dir.extentluecke")[0].severity);
    fs::remove(d);
}

/// @test Ein geloeschter Platz wird als **Hinweis** gemeldet — er ist der Vorrat der
///       Wiederherstellung (§13), kein Schaden.  Und ein NIE benutzter Platz (alle
///       32 Byte 0xE5) darf dabei nicht mitgezaehlt werden.
TEST(FsCheckCpmSchaden, GeloeschterPlatzIstEinHinweis) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_del.img");
    const int slot = ersterPlatz(d);
    ASSERT_GE(slot, 0);
    schreib(d, platzOffset(slot), {0xE5});          // so loescht CP/M

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
    const std::vector<FsFinding> f = mitId(r, "cpm.dir.geloescht");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_NE(std::string::npos, f[0].text.find("1 freie")) << f[0].text;
    fs::remove(d);
}

/// @test Ein Verzeichnisbereich aus lauter Fuellbytes: formatiert, nie eingerichtet.
///       Danach hat es keinen Sinn, ueber einzelne Plaetze zu reden — der Bericht
///       sagt EINEN Satz und hoert auf.
TEST(FsCheckCpmSchaden, VerzeichnisVollerFuellbytes) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_fuell.img");
    schreib(d, kCpa780Dir, std::vector<uint8_t>(128 * kPlatz, 0xF6));

    auto v = oeffneErzwungen(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    ASSERT_EQ(1u, r.findings.size()) << kennungen(r);
    EXPECT_EQ("cpm.dir.fuellbyte", r.findings[0].id);
    EXPECT_NE(std::string::npos, r.findings[0].text.find("0xF6")) << r.findings[0].text;
    fs::remove(d);
}

/// @test Ein Sektor mit falscher Daten-CRC wird bei der Vollpruefung auf die Datei
///       zurueckgerechnet, der er gehoert — das ist der Unterschied zum roten
///       Kaestchen im Diskeditor, das nur „Sektor kaputt" sagt.
TEST(FsCheckCpmSchaden, EinKaputterSektorNenntSeineDatei) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.hfe", "fscheck_crc.hfe");
    {
        std::string err;
        auto v = DiskVolume::open(d, "", formate(), dateisysteme(), err, /*read_only=*/false);
        ASSERT_TRUE(v) << err;

        // Erster Datenblock hinter dem Verzeichnis: Block 2 → Byte 4096 des
        // Datenbereichs → c2h0, logischer Sektor 4 → ID 5 (skew 0).
        const TrackView sicht = v->trackView(2, 0);
        int index = -1;
        for (const TrackSpan& s : sicht.spans)
            if (s.kind == TrackSpan::Kind::Sector && s.id == 5) index = s.index;
        ASSERT_GE(index, 0);

        std::vector<uint8_t> daten;
        uint16_t crc = 0;
        ASSERT_TRUE(v->readSectorAt(2, 0, index, daten, crc));
        const uint16_t falsch = static_cast<uint16_t>(crc ^ 0xFFFF);
        ASSERT_TRUE(v->writeSectorAt(2, 0, index, daten, &falsch)) << v->lastError();
        ASSERT_TRUE(v->flush()) << v->lastError();
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    // Die Schnellpruefung sieht ihn NICHT — er liegt hinter dem Verzeichnis.
    EXPECT_EQ(0, v->check(FsCheckLevel::Schnell, true).zaehlerAb(FsSeverity::Warnung));

    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "cpm.medium.crc");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    EXPECT_EQ(FsLayer::Medium, f[0].layer);
    EXPECT_EQ(2, f[0].cyl);
    EXPECT_EQ(0, f[0].head);
    EXPECT_FALSE(f[0].object.empty()) << "Der Befund muss die Datei nennen";
    EXPECT_NE(std::string::npos, f[0].text.find(f[0].object)) << f[0].text;
    fs::remove(d);
    fs::remove(d + "~");
}

/// @test Ein zerstoerter ERSTER Verzeichnisplatz macht die Diskette unerkennbar —
///       die Pruefung kommt dann gar nicht zum Zug (§20, Grenze der Etappe 1).
///
/// Der Test haelt die Grenze fest, damit sie nicht unbemerkt zur Ueberraschung
/// wird: der Ausweg ist ein erzwungenes Profil, und die Meldung sagt das auch.
TEST(FsCheckCpmSchaden, EinZerstoerterErsterPlatzVerhindertDasErkennen) {
    const std::string d = kopie("cpa_cpa780_k5601_noclock.img", "fscheck_unerkannt.img");
    schreib(d, platzOffset(0), {0x40});

    std::string err;
    auto v = DiskVolume::open(d, "", formate(), dateisysteme(), err);
    EXPECT_FALSE(v);
    EXPECT_NE(std::string::npos, err.find("--fs")) << err;

    // Mit erzwungenem Profil geht es hinein, und die Pruefung nennt den Grund.
    auto e = oeffneErzwungen(d);
    ASSERT_TRUE(e);
    EXPECT_EQ(1u, mitId(e->check(FsCheckLevel::Schnell, true), "cpm.dir.user").size());
    fs::remove(d);
}


// ═══ 4. UDOS/ZDOS ═════════════════════════════════════════════════════════════
//
// Hier ist mehr moeglich als bei CP/M, weil UDOS zwei unabhaengige Darstellungen
// derselben Wahrheit fuehrt: den gespeicherten Belegungsplan und die
// selbsttragende Verkettung hinter der Daten-CRC.  Beschaedigt wird deshalb
// jeweils GENAU EINE davon — und die Pruefung muss den Widerspruch finden.

#include "core/filesystem/udos/udos_fs.h"
#include "core/peripherals/floppy_drive/disk_image.h"

namespace {

/// @brief Eine schreibbare Kopie einer UDOS-Seite, samt allem, was daran haengt.
struct UdosSeite {
    std::string                     pfad;
    std::unique_ptr<DiskImage>      disk;
    std::unique_ptr<SectorSpace>    space;
    std::unique_ptr<UdosFileSystem> fs;
    std::string                     err;
    explicit operator bool() const { return fs != nullptr; }
};

UdosSeite udosOeffne(const std::string& pfad, uint8_t head, bool schreibbar,
                     const char* fsname = "udos_ds77") {
    UdosSeite s;
    s.pfad = pfad;
    const FsProfile* p = dateisysteme().find(fsname);
    const DiskFormat* f = p ? formate().find(p->format) : nullptr;
    if (!f) { s.err = "Katalog unvollstaendig"; return s; }
    s.disk = DiskImage::open(pfad, std::nullopt, !schreibbar);
    if (!s.disk) { s.err = "Abbild nicht ladbar"; return s; }
    s.space = std::make_unique<SectorSpace>(s.disk->medium(), *f, head);
    s.fs    = UdosFileSystem::mount(*s.space, *p, head, s.err);
    return s;
}

/// @brief Ein Bit der Belegungskarte setzen oder loeschen — **an der Karte selbst**,
///        nicht ueber @ref UdosBitmap.  Der Test soll nicht dieselbe Rechnung
///        benutzen wie der Prueflig.
bool karteBit(SectorSpace& space, uint8_t head, uint8_t bm_track,
              uint8_t track, uint8_t sector_id, bool belegt) {
    const size_t off = 24 + static_cast<size_t>(track) * 4
                     + static_cast<size_t>(sector_id - 1) / 8;
    const uint8_t maske = static_cast<uint8_t>(0x80 >> ((sector_id - 1) % 8));
    const uint8_t id    = static_cast<uint8_t>(off / 128 + 1);

    SectorData sec;
    if (!space.readSector(bm_track, head, id, sec)) return false;
    std::vector<uint8_t> d(sec.data.begin(), sec.data.begin() + 128);
    if (belegt) d[off % 128] |= maske;
    else        d[off % 128] &= static_cast<uint8_t>(~maske);
    return space.writeSector(bm_track, head, id, d);
}

/// @brief Den Sektorkontrollblock (Rueckwaerts-/Vorwaertszeiger) ersetzen.
bool setzeZeiger(SectorSpace& space, uint8_t head, UdosPointer p,
                 UdosPointer back, UdosPointer fwd) {
    SectorData sec;
    if (!space.readSector(p.track, head, p.sectorId(), sec)) return false;
    const std::vector<uint8_t> daten(sec.data.begin(), sec.data.begin() + 128);
    const std::vector<uint8_t> tail = {back.sector_index, back.track,
                                       fwd.sector_index,  fwd.track};
    return space.writeSector(p.track, head, p.sectorId(), daten, tail);
}

/// @brief Eine Datei der Referenzseite mit mindestens @p saetze Saetzen.
const UdosDirEntry* findeDatei(const UdosFileSystem& fs, size_t saetze,
                               UdosFileHeader& hdr,
                               std::vector<UdosPointer>& kette,
                               std::vector<UdosDirEntry>& verz) {
    verz = fs.directory();
    for (const UdosDirEntry& e : verz) {
        if (e.name == "DIRECTORY") continue;
        if (!fs.readHeader(e.header, hdr)) continue;
        if (!fs.recordChain(hdr, kette)) continue;
        if (kette.size() >= saetze) return &e;
    }
    return nullptr;
}

}  // namespace

/// @test Ein geloeschtes Kartenbit ist **Gefahr** — der wichtigste Befund ueberhaupt.
///
/// Die Datei ist vollstaendig heil; nur der Belegungsplan sagt, ihr Sektor sei frei.
/// UDOS vergibt ihn beim naechsten Schreiben weiter und zerstoert sie damit.  Kein
/// Betriebssystem der Familie merkt das von allein — der Plan IST dort die Wahrheit
/// ueber den freien Platz (doc/udos_diskettenformat.md §4.2).
TEST(FsCheckUdosSchaden, EinGeloeschtesKartenbitIstGefahr) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_karte.hfe");

    std::string name;
    UdosPointer ziel{0, 0};
    {
        UdosSeite s = udosOeffne(d, 0, /*schreibbar=*/true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = findeDatei(*s.fs, 2, hdr, kette, verz);
        ASSERT_NE(e, nullptr) << "keine Datei mit zwei Saetzen gefunden";
        name = e->name;
        ziel = kette[1];
        ASSERT_TRUE(karteBit(*s.space, 0, 23, ziel.track, ziel.sectorId(), false));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush()) << "Kopie nicht schreibbar";
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);

    // Die SCHNELLpruefung kann den Gefahrbefund nicht stellen: dafuer muesste sie
    // jeden Kopfsektor und jede Kette lesen — an einer physischen Diskette Dutzende
    // Spuren (E2).  **Sie merkt aber, dass etwas nicht stimmt**: der gespeicherte
    // Freizaehler passt nicht mehr zu den Bits.  Das ist der billige Schatten des
    // teuren Befundes, und darum ist der Zaehlerabgleich seinen Platz in der
    // Schnellpruefung wert.
    {
        const FsCheckReport& schnell = v->check(FsCheckLevel::Schnell, true);
        EXPECT_TRUE(mitId(schnell, "udos.karte.frei_aber_belegt").empty())
            << kennungen(schnell);
        EXPECT_EQ(1u, mitId(schnell, "udos.karte.zaehler").size()) << kennungen(schnell);
        EXPECT_EQ(FsSeverity::Warnung, schnell.hoechste())
            << "die Schnellpruefung darf hier noch keine Gefahr behaupten";
    }

    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.karte.frei_aber_belegt");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    EXPECT_EQ(FsSeverity::Gefahr, r.hoechste());
    EXPECT_EQ(name, f[0].object) << "der Befund muss die betroffene Datei nennen";
    EXPECT_EQ(ziel.track, f[0].cyl);
    // Der Zaehler faellt dabei zwangslaeufig mit auf — das ist richtig so.
    EXPECT_EQ(1u, mitId(r, "udos.karte.zaehler").size()) << kennungen(r);
    fs::remove(d);
}

/// @test Ein belegtes Bit ohne Datei dahinter ist nur **verlorener Platz**.
///
/// Die Gegenrichtung desselben Abgleichs, und bewusst NICHT `Gefahr`: es geht
/// nichts verloren, es liegt nur Platz brach — und dort koennten geloeschte
/// Dateien stehen (§13).
TEST(FsCheckUdosSchaden, EinBelegtesBitOhneDateiIstVerlorenerPlatz) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_verloren.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        // Eine Spur, die weder System noch Verzeichnis ist, und ein Sektor, den die
        // Karte als frei fuehrt.
        bool gesetzt = false;
        for (uint8_t t = 40; t < 60 && !gesetzt; ++t)
            for (uint8_t sid = 1; sid <= 26; ++sid)
                if (!s.fs->bitmap().used(t, sid)) {
                    ASSERT_TRUE(karteBit(*s.space, 0, 23, t, sid, true));
                    gesetzt = true;
                    break;
                }
        ASSERT_TRUE(gesetzt) << "kein freier Sektor zum Belegen gefunden";
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.karte.belegt_aber_frei");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, f[0].severity);
    EXPECT_LT(f[0].severity, FsSeverity::Gefahr) << "verlorener Platz ist keine Gefahr";
    fs::remove(d);
}

/// @test Ein abgeschnittener Vorwaertszeiger: die Kette endet vor der Satzzahl.
TEST(FsCheckUdosSchaden, EinKettenbruchWirdGefunden) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_bruch.hfe");
    std::string name;
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = findeDatei(*s.fs, 3, hdr, kette, verz);
        ASSERT_NE(e, nullptr);
        name = e->name;
        // Satz 1 endet die Kette — Saetze 2…n sind damit unerreichbar.
        ASSERT_TRUE(setzeZeiger(*s.space, 0, kette[0], e->header,
                                UdosPointer{0xFF, 0xFF}));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.kette.bruch");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    EXPECT_EQ(name, f[0].object);
    // Und die abgehaengten Saetze fallen als verlorener Platz auf.
    EXPECT_EQ(1u, mitId(r, "udos.karte.belegt_aber_frei").size()) << kennungen(r);
    fs::remove(d);
}

/// @test Ein verdrehter Rueckwaertszeiger ist nur eine Warnung — die Datei bleibt
///       lesbar, denn gelesen wird vorwaerts.  Reparierbar ist er trotzdem, und
///       genau deshalb wird er gemeldet.
TEST(FsCheckUdosSchaden, EinVerdrehterRueckwaertszeiger) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_rueck.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = findeDatei(*s.fs, 2, hdr, kette, verz);
        ASSERT_NE(e, nullptr);

        // Rueckwaertszeiger von Satz 2 auf Unsinn, Vorwaertszeiger unangetastet.
        SectorData sec;
        ASSERT_TRUE(s.space->readSector(kette[1].track, 0, kette[1].sectorId(), sec));
        const UdosPointer fwd = UdosPointer::fromBytes(sec.tail.data() + 2);
        ASSERT_TRUE(setzeZeiger(*s.space, 0, kette[1], UdosPointer{5, 60}, fwd));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.kette.rueckwaerts");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, f[0].severity);
    fs::remove(d);
}

/// @test Eine Kette, die im Kreis laeuft, wird erkannt — und die Pruefung nennt den
///       Sektor, an dem sich die Schleife schliesst.
TEST(FsCheckUdosSchaden, EinZyklusWirdGefundenUndVerortet) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_zyklus.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = findeDatei(*s.fs, 3, hdr, kette, verz);
        ASSERT_NE(e, nullptr);

        // Satz 2 zeigt zurueck auf Satz 1 — die Kette beisst sich in den Schwanz.
        SectorData sec;
        ASSERT_TRUE(s.space->readSector(kette[1].track, 0, kette[1].sectorId(), sec));
        const UdosPointer back = UdosPointer::fromBytes(sec.tail.data());
        ASSERT_TRUE(setzeZeiger(*s.space, 0, kette[1], back, kette[0]));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.kette.zyklus");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    EXPECT_GE(f[0].cyl, 0) << "der Ort der Schleife gehoert in den Befund";
    fs::remove(d);
}

/// @test Zwei Dateien auf demselben Sektor — bei UDOS ebenfalls `Gefahr`.
TEST(FsCheckUdosSchaden, EinSektorInZweiDateienIstGefahr) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_kreuz.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        std::vector<UdosDirEntry> verz = s.fs->directory();

        // Zwei verschiedene Dateien mit je mindestens einem Satz suchen und die
        // erste auf den Satz der zweiten zeigen lassen.
        std::vector<std::pair<const UdosDirEntry*, std::vector<UdosPointer>>> mit;
        for (const UdosDirEntry& e : verz) {
            if (e.name == "DIRECTORY") continue;
            UdosFileHeader h;
            std::vector<UdosPointer> k;
            if (s.fs->readHeader(e.header, h) && s.fs->recordChain(h, k) && !k.empty())
                mit.push_back({&e, k});
            if (mit.size() == 2) break;
        }
        ASSERT_EQ(2u, mit.size());
        // Kopfsektor von Datei A auf den ersten Satz von Datei B umbiegen.
        SectorData sec;
        ASSERT_TRUE(s.space->readSector(mit[0].first->header.track, 0,
                                        mit[0].first->header.sectorId(), sec));
        const UdosPointer back = UdosPointer::fromBytes(sec.tail.data());
        ASSERT_TRUE(setzeZeiger(*s.space, 0, mit[0].first->header, back, mit[1].second[0]));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.kette.doppelt");
    ASSERT_FALSE(f.empty()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    fs::remove(d);
}

/// @test Was das Dateisystem SELBST belegt, muss in der Karte stehen.
///
/// Das ist der einzige Teil der Systembereiche, der sich ABLEITEN laesst — die
/// Spuren 0–2 und die Bootspur sind Sitte und auf echten Disketten uneinheitlich
/// belegt (Seite 1 von `udos_ds77_k5601_fremdsync.hfe` hat sie voellig frei).
TEST(FsCheckUdosSchaden, DieKarteMussIhreEigenenSektorenFuehren) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_system.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        ASSERT_TRUE(karteBit(*s.space, 0, 23, 23, 2, false));   // die Karte selbst
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    // Schon die SCHNELLpruefung findet ihn — es geht nur um Karte und Verzeichnis.
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "udos.karte.system");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    EXPECT_EQ(FsLayer::Verwaltung, f[0].layer);
    fs::remove(d);
}

/// @test Der gespeicherte Freizaehler ist eine Gegenprobe, nicht die Wahrheit —
///       eine Abweichung ist eine Warnung, kein Fehler (§4.2).
TEST(FsCheckUdosSchaden, EinFalscherFreizaehlerIstNurEineWarnung) {
    const std::string d = kopie("udos_boot_scp.hfe", "fscheck_udos_zaehler.hfe");
    {
        UdosSeite s = udosOeffne(d, 0, true);
        ASSERT_TRUE(s) << s.err;
        // Freizaehler steht bei Offset 380/381 — im dritten Kartensektor.
        SectorData sec;
        ASSERT_TRUE(s.space->readSector(23, 0, 3, sec));
        std::vector<uint8_t> dat(sec.data.begin(), sec.data.begin() + 128);
        dat[380 - 256] = 0x00;
        dat[381 - 256] = 0x00;
        ASSERT_TRUE(s.space->writeSector(23, 0, 3, dat));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Schnell, true);
    const std::vector<FsFinding> f = mitId(r, "udos.karte.zaehler");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, f[0].severity);
    fs::remove(d);
}


// ═══ 5. UDOS1715 / NDOS ═══════════════════════════════════════════════════════
//
// Dieselbe Familie, andere Verkettung: der µPD765 erreicht die Bytes hinter der
// Daten-CRC nicht, deshalb stehen die Adressen in eigenen **Zeigersektoren**.  Die
// Pruefung muss die also mitzaehlen — sie belegen selbst Platz — und ihre doppelte
// Verkettung gegenpruefen.

#include "core/filesystem/udos/udos1715_fs.h"

namespace {

struct NdosDiskette {
    std::unique_ptr<DiskImage>          disk;
    std::unique_ptr<SectorSpace>        space;
    std::unique_ptr<Udos1715FileSystem> fs;
    std::string                         err;
    explicit operator bool() const { return fs != nullptr; }
};

NdosDiskette ndosOeffne(const std::string& pfad, bool schreibbar) {
    NdosDiskette s;
    const FsProfile* p = dateisysteme().find("udos1715");
    const DiskFormat* f = p ? formate().find(p->format) : nullptr;
    if (!f) { s.err = "Katalog unvollstaendig"; return s; }
    // Ein `.img` traegt KEINE Geometrie — sie muss mitgegeben werden.  (Bei ZDOS
    // stellt sich die Frage nicht: dort ist `.img` unmoeglich, weil der
    // Sektorkontrollblock hinter der Daten-CRC verlorenginge.)
    s.disk = DiskImage::open(pfad, *f, !schreibbar);
    if (!s.disk) { s.err = "Abbild nicht ladbar"; return s; }
    s.space = std::make_unique<SectorSpace>(s.disk->medium(), *f);
    s.fs    = Udos1715FileSystem::mount(*s.space, *p, s.err);
    return s;
}

/// @brief Einen NDOS-Sektor lesen/schreiben.  Die „Spur" ist der ganze Zylinder:
///        `UDOS-Sektor = (ID−1) + Kopf·16` (doc/udos1715_diskettenformat.md §1.1).
std::vector<uint8_t> ndosLies(SectorSpace& space, UdosPointer p) {
    SectorData sec;
    EXPECT_TRUE(space.readSector(p.track, p.sector_index >= 16 ? 1 : 0,
                                 static_cast<uint8_t>(p.sector_index % 16 + 1), sec));
    return {sec.data.begin(), sec.data.begin() + 256};
}

bool ndosSchreib(SectorSpace& space, UdosPointer p, const std::vector<uint8_t>& d) {
    return space.writeSector(p.track, p.sector_index >= 16 ? 1 : 0,
                             static_cast<uint8_t>(p.sector_index % 16 + 1), d);
}

/// @brief Ein Bit des Belegungsplans (NDOS: 2 Sektoren à 256 B auf Spur 17H).
bool ndosPlanBit(SectorSpace& space, uint8_t bm_track, uint8_t track,
                 uint8_t sector_id, bool belegt) {
    const size_t off = 24 + static_cast<size_t>(track) * 4
                     + static_cast<size_t>(sector_id - 1) / 8;
    const uint8_t maske = static_cast<uint8_t>(0x80 >> ((sector_id - 1) % 8));
    const UdosPointer p{static_cast<uint8_t>(off / 256), bm_track};
    std::vector<uint8_t> d = ndosLies(space, p);
    if (d.size() < 256) return false;
    if (belegt) d[off % 256] |= maske;
    else        d[off % 256] &= static_cast<uint8_t>(~maske);
    return ndosSchreib(space, p, d);
}

/// @brief Eine Datei mit mindestens @p saetze Saetzen (ohne DIRECTORY).
const UdosDirEntry* ndosFindeDatei(const Udos1715FileSystem& fs, size_t saetze,
                                   UdosFileHeader& hdr,
                                   std::vector<UdosPointer>& kette,
                                   std::vector<UdosDirEntry>& verz) {
    verz = fs.directory();
    for (const UdosDirEntry& e : verz) {
        if (e.name == "DIRECTORY") continue;
        if (!fs.readDescriptor(e.header, hdr)) continue;
        if (!fs.recordChain(hdr, kette)) continue;
        if (kette.size() >= saetze) return &e;
    }
    return nullptr;
}

}  // namespace

/// @test Auch bei NDOS ist ein geloeschtes Planbit **Gefahr** — derselbe Abgleich,
///       andere Verkettung.
TEST(FsCheckNdosSchaden, EinGeloeschtesPlanbitIstGefahr) {
    const std::string d = kopie("udos1715_640k_pc1715_system.img", "fscheck_ndos_plan.img");
    std::string name;
    {
        NdosDiskette s = ndosOeffne(d, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = ndosFindeDatei(*s.fs, 2, hdr, kette, verz);
        ASSERT_NE(e, nullptr);
        name = e->name;
        ASSERT_TRUE(ndosPlanBit(*s.space, 23, kette[1].track, kette[1].sectorId(), false));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "udos.karte.frei_aber_belegt");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Gefahr, f[0].severity);
    EXPECT_EQ(name, f[0].object);
    fs::remove(d);
}

/// @test Ein Descriptor ohne Zeigersektor (`FIRSTBL` = FFFF) ist unbrauchbar —
///       bei NDOS haengt daran die GESAMTE Verkettung der Datei.
TEST(FsCheckNdosSchaden, FirstblInsLeere) {
    const std::string d = kopie("udos1715_640k_pc1715_system.img", "fscheck_ndos_firstbl.img");
    std::string name;
    {
        NdosDiskette s = ndosOeffne(d, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = ndosFindeDatei(*s.fs, 1, hdr, kette, verz);
        ASSERT_NE(e, nullptr);
        name = e->name;
        std::vector<uint8_t> desc = ndosLies(*s.space, e->header);
        desc[0x80] = 0xFF;
        desc[0x81] = 0xFF;
        ASSERT_TRUE(ndosSchreib(*s.space, e->header, desc));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "ndos.firstbl");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    EXPECT_EQ(name, f[0].object);
    fs::remove(d);
}

/// @test Satzzahl im Descriptor ≠ Zahl der Adressen in den Zeigersektoren.
TEST(FsCheckNdosSchaden, SatzzahlPasstNichtZuDenAdressen) {
    const std::string d = kopie("udos1715_640k_pc1715_system.img", "fscheck_ndos_anzahl.img");
    {
        NdosDiskette s = ndosOeffne(d, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = ndosFindeDatei(*s.fs, 2, hdr, kette, verz);
        ASSERT_NE(e, nullptr);
        std::vector<uint8_t> desc = ndosLies(*s.space, e->header);
        const uint16_t mehr = static_cast<uint16_t>(hdr.record_count + 5);
        desc[0x0D] = static_cast<uint8_t>(mehr & 0xFF);
        desc[0x0E] = static_cast<uint8_t>(mehr >> 8);
        ASSERT_TRUE(ndosSchreib(*s.space, e->header, desc));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "ndos.zeiger.anzahl");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Fehler, f[0].severity);
    fs::remove(d);
}

/// @test Der Rueckwaertszeiger eines Zeigersektors ist redundant — und damit eine
///       echte Gegenprobe (spaeter auch reparierbar).
TEST(FsCheckNdosSchaden, EinVerdrehterZeigersektorRueckwaerts) {
    const std::string d = kopie("udos1715_640k_pc1715_system.img", "fscheck_ndos_bck.img");
    {
        NdosDiskette s = ndosOeffne(d, true);
        ASSERT_TRUE(s) << s.err;
        UdosFileHeader hdr;
        std::vector<UdosPointer> kette;
        std::vector<UdosDirEntry> verz;
        const UdosDirEntry* e = ndosFindeDatei(*s.fs, 1, hdr, kette, verz);
        ASSERT_NE(e, nullptr);
        std::vector<uint8_t> blk = ndosLies(*s.space, hdr.firstbl);
        blk[0xFC] = 7;      // BCKZGR auf Unsinn
        blk[0xFD] = 60;
        ASSERT_TRUE(ndosSchreib(*s.space, hdr.firstbl, blk));
        s.fs.reset(); s.space.reset();
        ASSERT_TRUE(s.disk->flush());
    }

    auto v = oeffne(d);
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    const std::vector<FsFinding> f = mitId(r, "ndos.zeiger.kette");
    ASSERT_EQ(1u, f.size()) << kennungen(r);
    EXPECT_EQ(FsSeverity::Warnung, f[0].severity);
    fs::remove(d);
}

/// @test Die WEGA-Startdiskette des P8000 faehrt dasselbe NDOS und muss ohne
///       Befund durchlaufen — trotz ihres groesseren Systembereichs (Kopf 0 der
///       Spuren 0, 21, 22, 23) und ihrer 13 Sektoren mit Schreibnaht.
///
/// Sie ist der schaerfste Waechter gegen zu enge Annahmen ueber „wie eine
/// NDOS-Diskette auszusehen hat" (doc/udos1715_diskettenformat.md §3.0a).
TEST(FsCheckNdosSchaden, DieP8000DisketteBleibtOhneBefund) {
    auto v = oeffne(fixture("udosP8000_640k_wega.hfe"));
    ASSERT_TRUE(v);
    const FsCheckReport& r = v->check(FsCheckLevel::Voll, true);
    EXPECT_EQ(0, r.zaehlerAb(FsSeverity::Warnung)) << kennungen(r);
    EXPECT_TRUE(r.vollstaendig);
}


/// @test Die Spurzaehler bedeuten „gewollt" und „davon verfuegbar" — nicht „von
///       allen Spuren der Diskette".
///
/// Sonst meldete eine tadellose Schnellpruefung „1 von 156 Spuren angesehen" und
/// saehe unvollstaendig aus, obwohl sie alles hat, was sie braucht.  Die Anzeige
/// haengt daran (sie schreibt die Zahlen hin), und `vollstaendig` muss dazu passen.
TEST(FsCheckVertrag, SpurzaehlerZeigenBeiVollstaendigerPruefungNvonN) {
    struct Fall { const char* datei; const char* fs; };
    static const Fall faelle[] = {
        {"cpa_cpa780_k5601_noclock.img", ""},
        {"udos_boot_scp.hfe", ""},
        {"udos1715_640k_pc1715_system.img", ""},
    };
    for (const Fall& f : faelle) {
        auto v = oeffne(fixture(f.datei));
        ASSERT_TRUE(v) << f.datei;
        for (FsCheckLevel stufe : {FsCheckLevel::Schnell, FsCheckLevel::Voll}) {
            const FsCheckReport& r = v->check(stufe, true);
            EXPECT_TRUE(r.vollstaendig) << f.datei;
            EXPECT_GT(r.spuren_gesamt, 0) << f.datei;
            EXPECT_EQ(r.spuren_gesamt, r.spuren_gelesen)
                << f.datei << ": eine vollstaendige Pruefung muss N von N zeigen";
        }
        // Und die Schnellpruefung fasst wirklich weniger an als die volle — genau
        // das ist ihr Zweck (E2).
        const int schnell = v->check(FsCheckLevel::Schnell, true).spuren_gesamt;
        const int voll    = v->check(FsCheckLevel::Voll, true).spuren_gesamt;
        EXPECT_LT(schnell, voll) << f.datei;
    }
}
