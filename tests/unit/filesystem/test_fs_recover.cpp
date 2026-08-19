/**
 * @file test_fs_recover.cpp
 * @brief GoogleTests der Wiederherstellung — Etappe 5 (CP/M).
 *
 * Der Aufbau folgt derselben Ordnung wie `test_fs_check.cpp`, aber die Fragen sind
 * andere.  Eine Pruefung darf nicht zu VIEL melden; eine Suche darf nicht zu WENIG
 * finden — und vor allem darf sie nichts finden, was es nicht gibt:
 *
 *   1. **Eine frisch angelegte Diskette hat nichts zu retten.**  Nach `mkfs` ist der
 *      ganze Verzeichnisbereich 0xE5; wer dort etwas findet, findet Fuellbytes.
 *   2. **Was geloescht wurde, kommt Byte fuer Byte zurueck.**  Das ist der Kern:
 *      erst lesen, dann loeschen, dann suchen, dann vergleichen.
 *   3. **Auf die Diskette zurueck geht nur, was keiner lebenden Datei ins Gehege
 *      kommt.**  Sonst entstuende genau die Kreuzbelegung, die die Pruefung als
 *      Gefahr meldet — die Rettung wuerde zum Schaden.
 *
 * @see core/filesystem/check/fs_recover.h · doc/design/15_dateisystempruefung.md §13
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filesystem/check/fs_recover.h"
#include "core/filesystem/disk_volume.h"

namespace fs = std::filesystem;

namespace {

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

/// @brief Eine Fixture nach /tmp kopieren — der Test loescht darauf herum.
std::string kopie(const char* fixture_name, const std::string& als) {
    const std::string ziel = (fs::temp_directory_path() / als).string();
    fs::remove(ziel);
    fs::copy_file(fixture(fixture_name), ziel);
    return ziel;
}

std::unique_ptr<DiskVolume> oeffneSchreibend(const std::string& pfad) {
    std::string err;
    auto v = DiskVolume::open(pfad, "", formate(), dateisysteme(), err,
                              /*read_only=*/false);
    EXPECT_TRUE(v) << pfad << ": " << err;
    if (v) v->setBackup(false);
    return v;
}

/// @brief Namen aller Funde, fuer die Fehlermeldung eines fehlgeschlagenen EXPECT.
std::string namen(const FsRecoverReport& r) {
    std::string s;
    for (const FsRecoverFind& f : r.funde)
        s += (s.empty() ? "" : ", ") + (f.name.empty() ? f.vorschlag : f.name)
           + "(" + fsRecoverQualityName(f.quality) + ")";
    return s.empty() ? "(keine Funde)" : s;
}

/// @brief Index des Fundes mit diesem Namen; -1 = nicht gefunden.
int suche(const FsRecoverReport& r, const std::string& name) {
    for (size_t i = 0; i < r.funde.size(); ++i)
        if (r.funde[i].name == name) return static_cast<int>(i);
    return -1;
}

/// @brief Eine Datei, die auf der Fixture liegt und mehr als einen Block belegt.
constexpr const char* kOpfer = "PIP.COM";

}  // namespace

// ═══ 1. Keine Funde, wo es nichts zu finden gibt ══════════════════════════════

/// @test Eine soeben angelegte Diskette hat nichts zu retten.
///
/// Das Gegenstueck zu `FsCheckKeineFalschmeldungen`: nach `mkfs` besteht der
/// Verzeichnisbereich durchgehend aus 0xE5.  Ein geloeschter Platz ist gerade daran
/// zu erkennen, dass er das NICHT tut — wer hier etwas findet, findet Fuellbytes und
/// bietet dem Anwender Muell als Datei an.
TEST(FsRecoverKeineFalschmeldungen, EineFrischAngelegteDisketteHatNichtsZuRetten) {
    int geprueft = 0;
    for (const FsProfile& p : dateisysteme().profiles()) {
        if (p.type != FsType::Cpm) continue;
        const std::string ext  = p.allow_hfe ? ".hfe" : ".img";
        const std::string ziel = (fs::temp_directory_path()
                                  / ("fsrec_neu_" + p.name + ext)).string();
        fs::remove(ziel);

        std::string err;
        auto v = DiskVolume::create(ziel, p.name, "", formate(), dateisysteme(), err);
        ASSERT_TRUE(v) << p.name << ": " << err;

        const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Oberflaeche, true);
        EXPECT_TRUE(r.leer()) << p.name << ": " << namen(r);
        ++geprueft;
        v.reset();
        fs::remove(ziel);
    }
    EXPECT_GT(geprueft, 3) << "Es wurde kaum ein Profil geprueft — Katalog leer?";
}

/// @test Die Suche laeuft NICHT von selbst beim Oeffnen.
///
/// Anders als die Pruefung: sie kostet, und sie beantwortet eine Frage, die niemand
/// gestellt hat.  (Dass es etwas zu finden GAEBE, sagt die Pruefung mit
/// `cpm.dir.geloescht`.)
TEST(FsRecoverAutomatik, DasOeffnenSuchtNichtVonSelbst) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_auto.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    EXPECT_TRUE(v->recoverReport().leer()) << namen(v->recoverReport());
    fs::remove(pfad);
}

/// @test Ein Suchlauf aendert die Diskette nicht (E1/E7).
TEST(FsRecoverAutomatik, DerSuchlaufSchreibtNie) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_lesend.img");
    const auto vorher = fs::file_size(pfad);
    {
        auto v = oeffneSchreibend(pfad);
        ASSERT_TRUE(v);
        ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0)));
        v->recoverScan(FsRecoverLevel::Oberflaeche, true);
        EXPECT_FALSE(v->recoverReport().leer());
    }
    // Die Datei darf sich durch das Loeschen geaendert haben, nicht aber schrumpfen
    // oder wachsen — und der Suchlauf danach hat gar nichts angefasst.
    EXPECT_EQ(vorher, fs::file_size(pfad));
    fs::remove(pfad);
}

// ═══ 2. Der Kern: geloescht, gefunden, Byte fuer Byte zurueck ═════════════════

/// @test Eine geloeschte Datei wird gefunden und liest sich wie vorher.
TEST(FsRecoverCpm, EineGeloeschteDateiKommtByteFuerByteZurueck) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_kern.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);

    // Erst lesen, dann loeschen — der Vergleich braucht das Original.
    const std::string ordner = (fs::temp_directory_path() / "fsrec_kern_out").string();
    fs::create_directories(ordner);
    const std::string vorher_datei = ordner + "/original.bin";
    ASSERT_TRUE(v->extract(FileRef::parse(kOpfer, 0), vorher_datei, TransferOptions{}))
        << v->lastError();
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    const int i = suche(r, kOpfer);
    ASSERT_GE(i, 0) << namen(r);
    const FsRecoverFind& f = r.funde[static_cast<size_t>(i)];
    EXPECT_EQ(FsRecoverQuality::Sicher, f.quality) << f.detail;
    EXPECT_TRUE(f.wiederherstellbar) << f.warum_nicht;
    EXPECT_TRUE(f.detail.empty()) << f.detail;
    EXPECT_EQ(fs::file_size(vorher_datei), f.size);
    EXPECT_GE(f.cyl, 0) << "Ein Fund ohne Ort laesst sich im Diskeditor nicht ansehen";
    EXPECT_EQ("Verzeichnisplatz", f.origin.substr(0, 16));

    const std::string gerettet = ordner + "/gerettet.bin";
    ASSERT_TRUE(v->recoverExtract(i, gerettet)) << v->lastError();
    // Ein sicherer Fund kommt OHNE Beiblatt — das gibt es nur, wo es etwas
    // einzuschraenken gibt.
    EXPECT_FALSE(fs::exists(gerettet + ".rettung.txt"));

    std::ifstream a(vorher_datei, std::ios::binary), b(gerettet, std::ios::binary);
    const std::vector<uint8_t> da((std::istreambuf_iterator<char>(a)),
                                   std::istreambuf_iterator<char>());
    const std::vector<uint8_t> db((std::istreambuf_iterator<char>(b)),
                                   std::istreambuf_iterator<char>());
    EXPECT_EQ(da, db);

    v.reset();
    fs::remove_all(ordner);
    fs::remove(pfad);
}

/// @test Wiederhergestellt steht die Datei wieder im Verzeichnis — und die Pruefung
///       schweigt danach.
TEST(FsRecoverCpm, WiederhergestelltStehtSieWiederImVerzeichnis) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_zurueck.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    const int i = suche(v->recoverScan(FsRecoverLevel::Verzeichnis, true), kOpfer);
    ASSERT_GE(i, 0) << namen(v->recoverReport());
    ASSERT_TRUE(v->recoverRestore(i, "")) << v->lastError();

    bool da = false;
    for (const FileEntry& e : v->list()) if (e.name == kOpfer) da = true;
    EXPECT_TRUE(da) << "Die wiederhergestellte Datei steht nicht im Verzeichnis";
    // Und die Diskette ist danach in Ordnung: kein Doppelblock, kein Loch.
    EXPECT_EQ(0, v->check(FsCheckLevel::Voll, true).zaehlerAb(FsSeverity::Warnung))
        << v->checkReport().alsText();
    // Der Fund ist verbraucht — der Suchlauf danach kennt ihn nicht mehr.
    EXPECT_LT(suche(v->recoverReport(), kOpfer), 0) << namen(v->recoverReport());

    v.reset();
    fs::remove(pfad);
}

/// @test Unter einem anderen Namen geht es auch — und nur unter dem anderen.
///
/// Der haeufigste Fall nach einem versehentlichen `ERA`: die Datei ist inzwischen
/// neu angelegt worden, der alte Stand soll daneben.
TEST(FsRecoverCpm, UmbenennenBeimWiederherstellen) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_name.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    const int i = suche(v->recoverScan(FsRecoverLevel::Verzeichnis, true), kOpfer);
    ASSERT_GE(i, 0);
    EXPECT_FALSE(v->recoverRestore(i, "klein.txt"));   // kleingeschrieben
    ASSERT_TRUE(v->recoverRestore(i, "ALT.COM")) << v->lastError();

    bool alt = false, neu = false;
    for (const FileEntry& e : v->list()) {
        if (e.name == kOpfer)    alt = true;
        if (e.name == "ALT.COM") neu = true;
    }
    EXPECT_FALSE(alt);
    EXPECT_TRUE(neu);
    v.reset();
    fs::remove(pfad);
}

/// @test Sind die Bloecke inzwischen neu vergeben, ist der Fund ein Bruchstueck —
///       und **nicht** wiederherstellbar.
///
/// Das ist die Stelle, an der aus einer Rettung ein Schaden wuerde: den
/// Verzeichnisplatz zurueckzuholen erzeugte `cpm.block.doppelt`, den einzigen
/// Befund der Schwere Gefahr, den CP/M ueberhaupt kennt.
///
/// Nachgestellt wird das so, wie es im Betrieb geschieht: zwei Dateien geloescht,
/// danach eine grosse neu geschrieben.  CP/M vergibt die niedrigsten freien Bloecke
/// und den niedrigsten freien Verzeichnisplatz — die neue Datei nimmt damit die
/// Plaetze der ERSTEN geloeschten Datei und greift mit ihren Bloecken schon in die
/// zweite hinein, deren Verzeichnisplatz stehenbleibt.
TEST(FsRecoverCpm, NeuVergebeneBloeckeMachenAusDemFundEinBruchstueck) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_streit.img");
    const std::string ordner = (fs::temp_directory_path() / "fsrec_streit_in").string();
    fs::create_directories(ordner);
    {
        std::ofstream f(ordner + "/GROSS.DAT", std::ios::binary);
        const std::vector<char> d(32768, 'X');
        f.write(d.data(), static_cast<std::streamsize>(d.size()));
    }

    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse("FORMAT.COM", 0))) << v->lastError();
    ASSERT_TRUE(v->erase(FileRef::parse("M80.COM", 0)))    << v->lastError();
    ASSERT_TRUE(v->insert(ordner + "/GROSS.DAT", FileRef::parse("GROSS.DAT", 0),
                          TransferOptions{})) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    int i = -1;
    for (size_t k = 0; k < r.funde.size(); ++k)
        if (r.funde[k].detail.find("GROSS.DAT") != std::string::npos) i = static_cast<int>(k);
    ASSERT_GE(i, 0) << "Kein Fund nennt den Streit mit der neuen Datei: " << namen(r);

    const FsRecoverFind& f = r.funde[static_cast<size_t>(i)];
    EXPECT_EQ(FsRecoverQuality::Bruchstueck, f.quality) << f.detail;
    EXPECT_FALSE(f.wiederherstellbar);
    EXPECT_NE(std::string::npos, f.warum_nicht.find("Kreuzbelegung")) << f.warum_nicht;

    // Herausholen geht trotzdem — mit Beiblatt.
    const std::string ziel = ordner + "/bruchstueck.bin";
    ASSERT_TRUE(v->recoverExtract(i, ziel)) << v->lastError();
    EXPECT_TRUE(fs::exists(ziel + ".rettung.txt"));
    // Auf die Diskette zurueck aber nicht — und zwar mit Begruendung.
    EXPECT_FALSE(v->recoverRestore(i, ""));
    EXPECT_NE(std::string::npos, v->lastError().find("Kreuzbelegung")) << v->lastError();

    v.reset();
    fs::remove_all(ordner);
    fs::remove(pfad);
}

// ═══ 3. Rohbereiche — Funde ohne Verzeichnisplatz ═════════════════════════════

/// @test Ohne Verzeichnisplatz findet erst die Oberflaechensuche etwas.
///
/// Nachgestellt wird der haeufigste Fall dieser Art: das Verzeichnis ist neu
/// aufgesetzt (oder ueberschrieben), die Daten stehen unberuehrt auf der Scheibe.
TEST(FsRecoverCpm, OhneVerzeichnisplatzFindetErstDieOberflaechensuche) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_roh.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    // Alle Dateien loeschen und die Verzeichnisplaetze vollstaendig ausloeschen —
    // danach sieht die Diskette aus wie frisch eingerichtet.
    for (const FileEntry& e : v->list())
        ASSERT_TRUE(v->erase(FileRef::parse(e.qualifiedName(), 0))) << v->lastError();
    v.reset();

    // Der Verzeichnisbereich einer cpa780-Diskette beginnt bei Byte 15104 (§6.2);
    // ein `.img` ist bitgleich der lineare Sektorraum.
    {
        std::fstream f(pfad, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(15104);
        const std::vector<char> e5(128 * 32, static_cast<char>(0xE5));
        f.write(e5.data(), static_cast<std::streamsize>(e5.size()));
    }

    v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    EXPECT_TRUE(v->recoverScan(FsRecoverLevel::Verzeichnis, true).leer())
        << namen(v->recoverReport());

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Oberflaeche, true);
    ASSERT_FALSE(r.leer()) << "Die Daten stehen noch da, nur der Verweis fehlt";
    const FsRecoverFind& f = r.funde.front();
    EXPECT_EQ(FsRecoverQuality::Bruchstueck, f.quality);
    EXPECT_TRUE(f.name.empty());
    EXPECT_EQ("fragment_", f.vorschlag.substr(0, 9)) << f.vorschlag;
    EXPECT_FALSE(f.wiederherstellbar);
    EXPECT_GT(f.size, 0u);
    EXPECT_FALSE(f.type.empty()) << "Ein Rohbereich braucht eine Einordnung";

    const std::string ziel = (fs::temp_directory_path() / "fsrec_roh_out.bin").string();
    ASSERT_TRUE(v->recoverExtract(0, ziel)) << v->lastError();
    EXPECT_EQ(f.size, fs::file_size(ziel));

    v.reset();
    fs::remove(ziel);
    fs::remove(ziel + ".rettung.txt");
    fs::remove(pfad);
}

/// @test Die Verzeichnissuche sieht nur in die Verzeichnisspuren.
///
/// Das ist der ganze Sinn der beiden Suchtiefen (E2): an einer physischen Diskette
/// kostet jede Spur 0,5–0,8 s.  Sieht die billige Suche schon ueberall hin, ist sie
/// nicht billig.
TEST(FsRecoverCpm, DieVerzeichnissucheSiehtNurInsVerzeichnis) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_tiefe.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    const int schmal = v->recoverScan(FsRecoverLevel::Verzeichnis, true).spuren_gesamt;
    const int breit  = v->recoverScan(FsRecoverLevel::Oberflaeche, true).spuren_gesamt;
    EXPECT_GT(schmal, 0);
    EXPECT_LT(schmal, breit);
    v.reset();
    fs::remove(pfad);
}

// ═══ 4. Das Modell ════════════════════════════════════════════════════════════

/// @test Die Zahlenwerte der Guete sind ein Vertrag — sie gehen als `int` ueber die
///       C-ABI und stehen in `--json`.
TEST(FsRecoverVertrag, GueteUndSuchtiefeSindStabil) {
    EXPECT_EQ(0, static_cast<int>(FsRecoverQuality::Bruchstueck));
    EXPECT_EQ(1, static_cast<int>(FsRecoverQuality::Wahrscheinlich));
    EXPECT_EQ(2, static_cast<int>(FsRecoverQuality::Sicher));
    EXPECT_EQ(0, static_cast<int>(FsRecoverLevel::Verzeichnis));
    EXPECT_EQ(1, static_cast<int>(FsRecoverLevel::Oberflaeche));
    EXPECT_STREQ("sicher", fsRecoverQualityName(FsRecoverQuality::Sicher));
}

/// @test Der Bericht sortiert das Beste nach vorn — und tut es reproduzierbar.
TEST(FsRecoverVertrag, DasBesteStehtVorn) {
    FsRecoverReport r;
    FsRecoverFind a; a.quality = FsRecoverQuality::Bruchstueck;    a.vorschlag = "a";
    FsRecoverFind b; b.quality = FsRecoverQuality::Sicher;         b.vorschlag = "b";
    FsRecoverFind c; c.quality = FsRecoverQuality::Wahrscheinlich; c.vorschlag = "c";
    r.funde = {a, b, c};
    r.sortieren();
    EXPECT_EQ("b", r.funde[0].vorschlag);
    EXPECT_EQ("c", r.funde[1].vorschlag);
    EXPECT_EQ("a", r.funde[2].vorschlag);
    EXPECT_EQ("1 sicher, 1 wahrscheinlich, 1 Bruchstueck", r.kurzfassung());
}
