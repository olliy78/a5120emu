/**
 * @file test_fs_recover.cpp
 * @brief GoogleTests der Wiederherstellung — CP/M (Etappe 5) und UDOS/NDOS (Etappe 6).
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

/// @brief Das UDOS-Opfer: `NOTE.TO.SD` liegt auf Seite 1 von `udos_boot_scp.hfe` —
///        16 Saetze zu 128 B, und ihr Kopfsektor liegt auf **Spur 21**, der
///        „Bootspur".  Genau deshalb ist sie das richtige Opfer (s. u.).
constexpr const char* kUdosOpfer = "Side1/NOTE.TO.SD";
/// @brief Das NDOS-Opfer: `ZLINK` der PC-1715-Systemdiskette — eine Programmdatei
///        mit SECHS Speichersegmenten, an der sich das Beiblatt beweisen laesst.
constexpr const char* kNdosOpfer = "ZLINK";

/// @brief Datei einlesen (fuer den byteweisen Vergleich).
std::vector<uint8_t> bytes(const std::string& pfad) {
    std::ifstream f(pfad, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

/// @brief Eine Zeile des Beiblatts `udos-dateiangaben.txt`; "" wenn es sie nicht gibt.
std::string beiblattZeile(const std::string& ordner, const std::string& name) {
    std::ifstream f(ordner + "/udos-dateiangaben.txt");
    std::string zeile;
    while (std::getline(f, zeile))
        if (zeile.rfind(name + " ", 0) == 0) return zeile;
    return "";
}

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

/// @test Auch der Suchlauf fuehrt seine Checkliste (§5a) — und ein Fund landet in
///       dem Schritt, der ihn gefunden hat.
TEST(FsRecoverCheckliste, JederLaufMeldetSeineSchritte) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_liste.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0)));

    const FsRecoverReport& flach = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_FALSE(flach.schritte.empty());
    const auto oberflaeche = std::find_if(
        flach.schritte.begin(), flach.schritte.end(),
        [](const FsSchritt& s) { return s.id == "cpm.suche.oberflaeche"; });
    ASSERT_NE(flach.schritte.end(), oberflaeche);
    EXPECT_FALSE(oberflaeche->ausgefuehrt);
    EXPECT_FALSE(oberflaeche->grund.empty());

    const FsRecoverReport& tief = v->recoverScan(FsRecoverLevel::Oberflaeche, true);
    int summe = 0;
    for (const FsSchritt& s : tief.schritte) summe += s.treffer;
    EXPECT_EQ(static_cast<int>(tief.funde.size()), summe)
        << "jeder Fund gehoert in genau einen Schritt";
    const auto verzeichnis = std::find_if(
        tief.schritte.begin(), tief.schritte.end(),
        [](const FsSchritt& s) { return s.id == "cpm.suche.verzeichnis"; });
    ASSERT_NE(tief.schritte.end(), verzeichnis);
    EXPECT_GT(verzeichnis->treffer, 0) << "die geloeschte Datei kam aus dem Verzeichnis";
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

/// @test Zwei nacheinander geloeschte Dateien GLEICHEN NAMENS sind zwei Funde.
///
/// Der Name ist der einzige Schlüssel, der das Löschen überlebt — der Nutzerbereich
/// stand in ebendem Byte, das `0xE5` wurde.  Beide Dateien landen deshalb beim
/// Gruppieren im selben Topf, und bis 2026-08-21 wurden sie zu **einem** Fund
/// zusammengeworfen: 7424 Byte, Güte „sicher", und die ersten 2048 Byte davon
/// stammten aus der ANDEREN Datei.  Eine Mischdatei, die sich sicher nennt — genau
/// das, was E10 verbietet.
///
/// Auseinanderzuhalten sind sie **beweisbar**: die Extent-Nummer kommt innerhalb
/// einer Datei genau einmal vor.  Hier tragen beide Extent 0.
///
/// Der Aufbau stellt nach, wie es im Betrieb dazu kommt — und er ist nicht
/// selbstverständlich: normalerweise belegt die neue Datei den Verzeichnisplatz der
/// gelöschten gleich wieder, und dann gibt es gar keine zwei Einträge.  Es braucht
/// einen FREIEN Platz WEITER VORN, den die neue Datei stattdessen nimmt.
TEST(FsRecoverCpm, ZweiGleichnamigeGeloeschteDateienSindZweiFunde) {
    const std::string pfad   = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_zwei.img");
    const std::string ordner = (fs::temp_directory_path() / "fsrec_zwei_out").string();
    fs::remove_all(ordner);
    fs::create_directories(ordner);

    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);

    // Das Original sichern — der Vergleich am Ende braucht es.
    ASSERT_TRUE(v->extract(FileRef::parse(kOpfer, 0), ordner + "/original.bin",
                           TransferOptions{})) << v->lastError();
    const std::vector<uint8_t> original = bytes(ordner + "/original.bin");
    ASSERT_FALSE(original.empty());

    // Einen Verzeichnisplatz WEITER VORN frei machen, damit die neue Datei nicht den
    // Platz der gelöschten wiederbekommt.
    const std::string frueh = v->list().front().name;
    ASSERT_NE(frueh, kOpfer);
    ASSERT_TRUE(v->erase(FileRef::parse(frueh, 0))) << v->lastError();
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    // Eine ANDERE Datei unter demselben Namen — und auch die wieder löschen.
    {
        std::ofstream f(ordner + "/neu.bin", std::ios::binary);
        const std::vector<char> d(3000, 'A');
        f.write(d.data(), static_cast<std::streamsize>(d.size()));
    }
    ASSERT_TRUE(v->insert(ordner + "/neu.bin", FileRef::parse(kOpfer, 0),
                          TransferOptions{})) << v->lastError();
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    // ── Zwei Funde, nicht einer ──────────────────────────────────────────────
    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    std::vector<const FsRecoverFind*> gleich;
    for (const FsRecoverFind& f : r.funde)
        if (f.name == kOpfer) gleich.push_back(&f);
    ASSERT_EQ(2u, gleich.size())
        << "zusammengeworfen ergäbe das eine Mischdatei aus zwei Quellen: " << namen(r);

    // Beide tragen denselben Namen — aber NICHT denselben Vorschlag, sonst
    // überschriebe „alles retten" den einen mit dem anderen.
    EXPECT_NE(gleich[0]->vorschlag, gleich[1]->vorschlag);
    for (const FsRecoverFind* f : gleich) {
        EXPECT_NE(std::string::npos, f->detail.find("2 geloeschte Eintraege"))
            << "der Anwender muss wissen, dass es den Namen zweimal gibt: " << f->detail;
        // Die Zuordnung ist hier eindeutig — jede Datei hat genau einen Platz.
        EXPECT_EQ(FsRecoverQuality::Sicher, f->quality) << f->detail;
    }

    // ── Und der Inhalt stimmt: EINER der beiden ist byteweise das Original ───
    int treffer = 0;
    for (size_t i = 0; i < r.funde.size(); ++i) {
        if (r.funde[i].name != kOpfer) continue;
        const std::string ziel = ordner + "/" + std::to_string(i) + ".bin";
        ASSERT_TRUE(v->recoverExtract(static_cast<int>(i), ziel)) << v->lastError();
        if (bytes(ziel) == original) ++treffer;
    }
    EXPECT_EQ(1, treffer)
        << "genau einer der beiden Funde muss byteweise die geloeschte Datei sein";

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

// ═══ 5. UDOS / ZDOS — Etappe 6 ═══════════════════════════════════════════════
//
// Der Schnitt ist hier ein anderer als bei CP/M, und das praegt jeden Fall:
// UDOS loescht den VERZEICHNISEINTRAG, nicht ein Byte.  Erhalten bleibt der
// Kopfsektor mit allem, was er traegt — verloren ist allein der Name.  Deshalb gibt
// es kein Zurueckschreiben auf die Diskette; der Weg zurueck fuehrt ueber Retten,
// Benennen und `put`, und das Beiblatt traegt die Angaben hinueber.

/// @test Eine frisch angelegte UDOS-/NDOS-Diskette hat nichts zu retten.
///
/// Das Gegenstueck zum CP/M-Fall weiter oben, und es ist die schaerfere Probe: die
/// Suche laeuft ueber die Signatur eines Kopfsektors, und die muss auf einer leeren
/// Diskette **nirgends** zufaellig zutreffen.
TEST(FsRecoverKeineFalschmeldungen, EineFrischAngelegteUdosDisketteHatNichtsZuRetten) {
    int geprueft = 0;
    for (const FsProfile& p : dateisysteme().profiles()) {
        if (p.type != FsType::Udos && p.type != FsType::Udos1715) continue;
        const std::string ext  = p.allow_hfe ? ".hfe" : ".img";
        const std::string ziel = (fs::temp_directory_path()
                                  / ("fsrec_udos_neu_" + p.name + ext)).string();
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
    EXPECT_GT(geprueft, 1) << "Es wurde kaum ein UDOS-Profil geprueft — Katalog leer?";
}

/// @test Eine geloeschte UDOS-Datei wird gefunden und liest sich Byte fuer Byte wie vorher.
TEST(FsRecoverUdos, EineGeloeschteDateiKommtByteFuerByteZurueck) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos.hfe");
    const std::string ordner = (fs::temp_directory_path() / "fsrec_udos_out").string();
    fs::remove_all(ordner);
    fs::create_directories(ordner);

    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    const FileRef opfer = FileRef::parse(kUdosOpfer);
    ASSERT_TRUE(v->extract(opfer, ordner + "/original.bin", TransferOptions{}))
        << v->lastError();
    ASSERT_TRUE(v->erase(opfer)) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_EQ(1u, r.funde.size()) << namen(r);
    const FsRecoverFind& f = r.funde.front();

    // Der Name ist FORT — er stand nur im Verzeichnis.  Was der Dialog zeigt, ist ein
    // Vorschlag, und `name` bleibt leer; genau daran haengt, dass niemand ihn fuer
    // eine gesicherte Angabe haelt.
    EXPECT_TRUE(f.name.empty()) << f.name;
    EXPECT_EQ("GERETTET.001", f.vorschlag);
    EXPECT_EQ(FsRecoverQuality::Sicher, f.quality) << f.detail;
    EXPECT_EQ("A", f.type);
    EXPECT_EQ(1, f.volume) << "Die Datei lag auf Seite 1";
    EXPECT_EQ(fs::file_size(ordner + "/original.bin"), f.size);

    const std::string gerettet = ordner + "/gerettet.bin";
    ASSERT_TRUE(v->recoverExtract(0, gerettet)) << v->lastError();
    EXPECT_EQ(bytes(ordner + "/original.bin"), bytes(gerettet));

    v.reset();
    fs::remove_all(ordner);
    fs::remove(pfad);
}

/// @test Ein Kopfsektor auf einer SYSTEMSPUR wird trotzdem gefunden.
///
/// Der Wächter gegen den naheliegendsten Fehlgriff: „auf Spur 0–2, 21, 22, 23 legt
/// UDOS keine Datei an, also gar nicht erst hinsehen".  An der echten
/// Referenzdiskette ist das **falsch** — `NOTE.TO.SD` hat ihren Kopfsektor auf Spur
/// 21, weil Seite 1 eine reine Datenseite ohne Urlader ist.  Mit dem Spurfilter fand
/// der Suchlauf diese Datei nicht, und zwar stillschweigend.  Es ist dieselbe Lehre
/// wie in `udos_check.cpp`: **die Systemspuren sind Sitte, nicht Struktur.**
TEST(FsRecoverUdos, EinKopfsektorAufDerBootspurWirdGefunden) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_systemspur.hfe");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kUdosOpfer))) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_EQ(1u, r.funde.size()) << namen(r);
    EXPECT_EQ(21, r.funde.front().cyl)
        << "Der Fund liegt auf der Bootspur — wer sie ueberspringt, findet ihn nie";
    EXPECT_EQ(1, r.funde.front().head);
    EXPECT_GT(r.funde.front().sector_index, 0)
        << "Der Ort traegt die Sektor-KENNUNG (1-basiert), nicht ihren Versatz";

    v.reset();
    fs::remove(pfad);
}

/// @test Bei UDOS wird nicht auf der Diskette wiederhergestellt — und es steht dabei,
///       wie es stattdessen geht.
///
/// Kein Mangel, sondern die Festlegung (§13.3): der Name ist ohnehin frei zu waehlen,
/// und drei Schreibzugriffe auf einen Datentraeger, den man fuer eine Rettung gerade
/// nicht anfassen will, waeren der falsche Preis dafuer.
TEST(FsRecoverUdos, KeinZurueckschreibenAufDieDiskette) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_kein_restore.hfe");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kUdosOpfer))) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_EQ(1u, r.funde.size()) << namen(r);
    EXPECT_FALSE(r.funde.front().wiederherstellbar);
    EXPECT_NE(std::string::npos, r.funde.front().warum_nicht.find("put"))
        << r.funde.front().warum_nicht;
    EXPECT_FALSE(v->recoverRestore(0, "NEU"));

    v.reset();
    fs::remove(pfad);
}

/// @test Der ganze Weg zurueck: retten, benennen, `put` — und die Datei ist wieder da.
///
/// Das ist die eigentliche Zusage von Etappe 6, und sie haengt am **Beiblatt**: die
/// Kopfsektorangaben ueberleben das Loeschen vollstaendig (nur der Name nicht), und
/// nur weil `recoverExtract` sie in `udos-dateiangaben.txt` schreibt, nimmt `put` sie
/// wieder an.  Ohne das kaeme die Datei mit falschem Typ und falscher Satzlaenge
/// zurueck.
TEST(FsRecoverUdos, RettenBenennenUndWiederEinspielen) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_rundlauf.hfe");
    const std::string ordner = (fs::temp_directory_path() / "fsrec_udos_rund").string();
    fs::remove_all(ordner);
    fs::create_directories(ordner);

    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    const FileRef opfer = FileRef::parse(kUdosOpfer);
    ASSERT_TRUE(v->extract(opfer, ordner + "/original.bin", TransferOptions{}))
        << v->lastError();
    ASSERT_TRUE(v->erase(opfer)) << v->lastError();
    ASSERT_EQ(1u, v->recoverScan(FsRecoverLevel::Verzeichnis, true).funde.size());
    ASSERT_TRUE(v->recoverExtract(0, ordner + "/NOTE.TO.SD")) << v->lastError();

    // Das Beiblatt traegt die Angaben aus dem ueberlebenden Kopfsektor.
    const std::string zeile = beiblattZeile(ordner, "NOTE.TO.SD");
    ASSERT_FALSE(zeile.empty()) << "Ohne Beiblatt ist der Weg zurueck nicht vollstaendig";
    EXPECT_NE(std::string::npos, zeile.find("typ=A"))    << zeile;
    EXPECT_NE(std::string::npos, zeile.find("satz=128")) << zeile;

    ASSERT_TRUE(v->insert(ordner + "/NOTE.TO.SD", FileRef::parse(kUdosOpfer),
                          TransferOptions{})) << v->lastError();
    ASSERT_TRUE(v->extract(opfer, ordner + "/zurueck.bin", TransferOptions{}))
        << v->lastError();
    EXPECT_EQ(bytes(ordner + "/original.bin"), bytes(ordner + "/zurueck.bin"));

    v.reset();
    fs::remove_all(ordner);
    fs::remove(pfad);
}

/// @test Rohbereiche findet erst die Oberflaechensuche — und die Verzeichnissuche
///       bleibt davon frei.
TEST(FsRecoverUdos, RohbereicheErstBeiDerOberflaechensuche) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_roh.hfe");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);

    // Ohne Loeschung ist die Diskette in Ordnung: nichts zu retten.
    EXPECT_TRUE(v->recoverScan(FsRecoverLevel::Verzeichnis, true).leer())
        << namen(v->recoverReport());

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Oberflaeche, true);
    ASSERT_FALSE(r.leer()) << "Auf einer 35 Jahre alten Diskette steht Altbestand herum";
    int rohbereiche = 0;
    for (const FsRecoverFind& f : r.funde) {
        if (f.vorschlag.rfind("fragment_", 0) != 0) continue;
        ++rohbereiche;
        EXPECT_EQ(FsRecoverQuality::Bruchstueck, f.quality);
        EXPECT_TRUE(f.name.empty());
        EXPECT_FALSE(f.wiederherstellbar);
        EXPECT_GT(f.size, 0u);
        EXPECT_FALSE(f.type.empty()) << "Ein Rohbereich braucht eine Einordnung";
        EXPECT_GE(f.cyl, 0) << "Ein Fund ohne Ort laesst sich nicht ansehen";
    }
    EXPECT_GT(rohbereiche, 0) << namen(r);

    v.reset();
    fs::remove(pfad);
}

// ═══ 6. UDOS1715 / NDOS ══════════════════════════════════════════════════════

/// @test Eine geloeschte NDOS-Datei kommt Byte fuer Byte zurueck — samt Segmentliste.
///
/// `ZLINK` ist mit Absicht gewaehlt: eine Programmdatei mit SECHS Speichersegmenten.
/// Sie beweist beides auf einmal — dass die Zeigersektorkette wieder gelesen wird und
/// dass das Beiblatt die Angaben traegt, ohne die das Programm nicht mehr startet.
TEST(FsRecoverNdos, EineGeloeschteDateiKommtByteFuerByteZurueck) {
    const std::string pfad = kopie("udos1715_640k_pc1715_system.img", "fsrec_ndos.img");
    const std::string ordner = (fs::temp_directory_path() / "fsrec_ndos_out").string();
    fs::remove_all(ordner);
    fs::create_directories(ordner);

    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    const FileRef opfer = FileRef::parse(kNdosOpfer);
    ASSERT_TRUE(v->extract(opfer, ordner + "/original.bin", TransferOptions{}))
        << v->lastError();
    ASSERT_TRUE(v->erase(opfer)) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_EQ(1u, r.funde.size()) << namen(r);
    const FsRecoverFind& f = r.funde.front();
    EXPECT_TRUE(f.name.empty());
    EXPECT_EQ(FsRecoverQuality::Sicher, f.quality) << f.detail;
    EXPECT_EQ("P", f.type);
    EXPECT_FALSE(f.wiederherstellbar);
    EXPECT_EQ(fs::file_size(ordner + "/original.bin"), f.size);

    ASSERT_TRUE(v->recoverExtract(0, ordner + "/ZLINK")) << v->lastError();
    EXPECT_EQ(bytes(ordner + "/original.bin"), bytes(ordner + "/ZLINK"));

    const std::string zeile = beiblattZeile(ordner, "ZLINK");
    ASSERT_FALSE(zeile.empty());
    EXPECT_NE(std::string::npos, zeile.find("typ=P"))   << zeile;
    EXPECT_NE(std::string::npos, zeile.find("satz=512")) << zeile;
    EXPECT_NE(std::string::npos, zeile.find("segs="))
        << "Die sechs Segmente von ZLINK fehlen — so startet die Datei nicht mehr: "
        << zeile;

    v.reset();
    fs::remove_all(ordner);
    fs::remove(pfad);
}

/// @test Der Suchlauf schreibt auch bei UDOS nie (E1/E7).
///
/// Die Zusage, auf der das ganze Verfahren steht: eine physische Diskette wird fuer
/// eine Rettung schreibgeschuetzt geoeffnet, und dabei muss der Suchlauf vollstaendig
/// bedienbar bleiben.
TEST(FsRecoverUdos, DerSuchlaufSchreibtNie) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_lesend.hfe");
    std::string err;
    auto v = DiskVolume::open(pfad, "", formate(), dateisysteme(), err, /*read_only=*/true);
    ASSERT_TRUE(v) << err;
    const auto vorher = fs::last_write_time(pfad);
    v->recoverScan(FsRecoverLevel::Oberflaeche, true);
    v.reset();
    EXPECT_EQ(vorher, fs::last_write_time(pfad));
    fs::remove(pfad);
}

// ═══ 7. Die Sektorliste eines Fundes ═════════════════════════════════════════
//
// Sie ist die Antwort auf die Frage, die vor jeder Rettung steht: *ist das
// überhaupt, was ich suche?*  Ohne sie bliebe nur der Anfang — und bei UDOS liegen
// die Sätze einer Datei nicht hintereinander, sondern verkettet über die Diskette
// verstreut.

/// @test Ein UDOS-Fund führt Kopfsektor UND alle Sätze auf, in Lesereihenfolge.
TEST(FsRecoverUdos, DerFundFuehrtSeineSektorenAuf) {
    const std::string pfad = kopie("udos_boot_scp.hfe", "fsrec_udos_orte.hfe");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kUdosOpfer))) << v->lastError();

    const FsRecoverReport& r = v->recoverScan(FsRecoverLevel::Verzeichnis, true);
    ASSERT_EQ(1u, r.funde.size()) << namen(r);
    const FsRecoverFind& f = r.funde.front();

    // 16 Sätze zu 128 B — und davor der Kopfsektor.
    ASSERT_EQ(17u, f.orte.size()) << "Kopfsektor + 16 Sätze";
    // Der erste Eintrag IST der Ort des Fundes — sonst zeigte der Sprung woandershin
    // als die Liste.
    EXPECT_EQ(f.cyl,          f.orte.front().cyl);
    EXPECT_EQ(f.head,         f.orte.front().head);
    EXPECT_EQ(f.sector_index, f.orte.front().sector);

    // Und sie liegen NICHT hintereinander: genau deshalb braucht der Bediener die
    // Liste — von Hand fände er den zweiten Satz nicht.
    bool verstreut = false;
    for (size_t i = 1; i < f.orte.size(); ++i)
        if (f.orte[i].sector != f.orte[i - 1].sector + 1) verstreut = true;
    EXPECT_TRUE(verstreut) << "Die Sätze dieser Datei sind verkettet, nicht fortlaufend";

    for (const FsRecoverOrt& o : f.orte) {
        EXPECT_GE(o.cyl, 0);
        EXPECT_GE(o.head, 0);
        EXPECT_GT(o.sector, 0) << "Die Liste führt Sektor-KENNUNGEN, keine Versätze";
    }

    v.reset();
    fs::remove(pfad);
}

/// @test Auch ein CP/M-Fund führt seine Sektoren auf — und der erste ist sein Ort.
TEST(FsRecoverCpm, DerFundFuehrtSeineSektorenAuf) {
    const std::string pfad = kopie("cpa_cpa780_k5601_noclock.img", "fsrec_cpm_orte.img");
    auto v = oeffneSchreibend(pfad);
    ASSERT_TRUE(v);
    ASSERT_TRUE(v->erase(FileRef::parse(kOpfer, 0))) << v->lastError();

    const int i = suche(v->recoverScan(FsRecoverLevel::Verzeichnis, true), kOpfer);
    ASSERT_GE(i, 0) << namen(v->recoverReport());
    const FsRecoverFind& f = v->recoverReport().funde[static_cast<size_t>(i)];

    ASSERT_FALSE(f.orte.empty());
    EXPECT_EQ(f.cyl,          f.orte.front().cyl);
    EXPECT_EQ(f.head,         f.orte.front().head);
    EXPECT_EQ(f.sector_index, f.orte.front().sector);
    // Die Liste deckt die Datei ab.  Die Sektorgröße kennt der Test nicht (cpa780
    // hat 1024er-Sektoren, andere Profile 128er) — gerechnet wird deshalb mit der
    // GRÖSSTEN im Katalog, dann gilt die Aussage für jedes Profil.
    EXPECT_GE(f.orte.size() * 1024u, f.size);
    EXPECT_GT(f.orte.size(), 1u) << "Eine 7 KB grosse Datei belegt mehr als einen Sektor";
    for (const FsRecoverOrt& o : f.orte) EXPECT_GT(o.sector, 0);

    v.reset();
    fs::remove(pfad);
}
