/**
 * @file test_a5120_disk_api.cpp
 * @brief Unit-Tests der Disketten-API auf Maschinenebene (`A5120Machine`).
 *
 * @details
 * Getestete Komponente: die Verdrahtung des YAML-Formatkatalogs in die Maschine
 * (`core/machines/a5120/a5120.cpp`, doc/K1520_architecture.md §8.6).  Diese Ebene
 * wurde bisher nur indirekt über die langsamen Boot-/Format-Integrationstests
 * berührt — ein Bruch zeigte sich dort als „CPABCGEN meldete kein OK" statt als
 * konkrete Meldung.
 *
 * | Gruppe                | Inhalt                                                     |
 * |-----------------------|------------------------------------------------------------|
 * | Standardformat        | defaultFormatName() je Laufwerksprofil (aus `default_for:`) |
 * | Auswahl               | compatibleFormats() = explizite `drives:`-Liste             |
 * | createDisk            | leerer Name = Leerdiskette; vorformatiert mit Formatnamen   |
 * | saveDiskAs            | Containerwechsel + Umbinden; Format nur für `.img`          |
 * | mountDisk             | erzwingt `drives:` BEWUSST NICHT (Combo-Boot, .hfe)         |
 * | Startabbruch          | kaputter Katalog → Konstruktor wirft mit klarer Meldung     |
 *
 * @see core/machines/a5120/a5120.h
 * @see core/peripherals/floppy_drive/format_catalog.h
 */

#include <gtest/gtest.h>
#include "core/machines/a5120/a5120.h"

#include "tests/support/fixtures.h"
#include "core/util/os_compat.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

/// Maschine mit expliziter Laufwerksbestückung je Slot.
A5120Machine::Config withDrives(const char* d0, const char* d1 = "K5601",
                                const char* d2 = "K5601", const char* d3 = "K5601") {
    A5120Machine::Config cfg;
    cfg.drive_profiles = {d0, d1, d2, d3};
    return cfg;
}

/// Freier Temp-Pfad für ein Testziel; die Datei räumt sich am Testende weg.
k1520test::TempDisk tmpPath(const std::string& name) {
    return k1520test::TempDisk::empty("diskapi_" + name);
}

}  // namespace

// ─── Standardformat je Laufwerkstyp ──────────────────────────────────────────

/**
 * @test A5120DiskApi/DefaultFormat_KommtAusDefaultFor
 * @brief Das Standardformat eines Slots stammt aus `default_for:` im Katalog.
 *
 * Ersetzt die früher hartkodierte if-Kette `defaultFormatFor()`; die Zuordnung muss
 * unverändert bleiben, sonst legt „Neue Diskette" plötzlich eine andere Geometrie an.
 * @par Kriterium  K5601→cpa800, K5600.20→cpa200, K5600.10→k5601_ss40_5x1024,
 *                 MF3200→mf3200, MF6400→mf6400.
 */
TEST(A5120DiskApi, DefaultFormat_KommtAusDefaultFor) {
    A5120Machine m(withDrives("K5601", "K5600.20", "K5600.10", "MF3200"));
    EXPECT_EQ(m.defaultFormatName(0), "cpa800");
    EXPECT_EQ(m.defaultFormatName(1), "cpa200");
    EXPECT_EQ(m.defaultFormatName(2), "k5601_ss40_5x1024");
    EXPECT_EQ(m.defaultFormatName(3), "mf3200");

    A5120Machine m2(withDrives("MF6400"));
    EXPECT_EQ(m2.defaultFormatName(0), "mf6400");
}

/**
 * @test A5120DiskApi/LeererSlot_HatWederStandardNochAuswahl
 * @brief Ein Slot ohne Laufwerk ("none") bietet keine Formate an.
 * @par Kriterium  defaultFormatName() leer, compatibleFormats() leer.
 */
TEST(A5120DiskApi, LeererSlot_HatWederStandardNochAuswahl) {
    A5120Machine m(withDrives("none"));
    EXPECT_TRUE(m.defaultFormatName(0).empty());
    EXPECT_TRUE(m.compatibleFormats(0).empty());
}

// ─── Angebotene Auswahl ──────────────────────────────────────────────────────

/**
 * @test A5120DiskApi/CompatibleFormats_StandardZuerstUndNurPassende
 * @brief compatibleFormats() liefert die explizite `drives:`-Liste, Standard an Position 0.
 *
 * Das ist die Liste, aus der die GUI auswählen lässt — hier (und beim Anlegen) wird
 * die Laufwerks-Kompatibilität erzwungen, nicht beim Mounten (s. u.).
 * @par Kriterium  Erstes Element = Standardformat; ein einseitiges Laufwerk bekommt
 *                 kein doppelseitiges Format und kein 8″-Format angeboten.
 */
TEST(A5120DiskApi, CompatibleFormats_StandardZuerstUndNurPassende) {
    A5120Machine m(withDrives("K5600.20"));
    const auto list = m.compatibleFormats(0);

    ASSERT_FALSE(list.empty());
    EXPECT_EQ(list.front(), "cpa200");                 // default_for
    EXPECT_EQ(list.front(), m.defaultFormatName(0));

    for (const auto& name : list) {
        EXPECT_NE(name, "cpa800")  << "doppelseitiges Format an einseitigem Laufwerk";
        EXPECT_NE(name, "cpa780")  << "doppelseitiges Format an einseitigem Laufwerk";
        EXPECT_NE(name, "mf3200")  << "8″-Format an 5,25″-Laufwerk";
    }
    // Keine Dubletten in der Auswahlliste.
    for (size_t i = 0; i < list.size(); ++i)
        for (size_t j = i + 1; j < list.size(); ++j)
            EXPECT_NE(list[i], list[j]) << "Format '" << list[i] << "' doppelt";
}

// ─── createDisk ──────────────────────────────────────────────────────────────

/**
 * @test A5120DiskApi/CreateDisk_LeererNameLegtLeerdisketteAn
 * @brief Ein leerer Formatname legt eine ECHTE Leerdiskette in Laufwerksgeometrie an
 *        (§8.7) — unformatiert, damit das Gastsystem sie selbst formatieren kann.
 * @par Kriterium  createDisk() == true; Datei entsteht; 40×1 Spuren; unformatiert;
 *                 nicht `.img`-fähig.
 */
TEST(A5120DiskApi, CreateDisk_LeererNameLegtLeerdisketteAn) {
    A5120Machine m(withDrives("K5600.10"));
    k1520test::TempDisk path_disk = tmpPath("blank.hfe");
    const std::string& path = path_disk.path();
    std::filesystem::remove(path);

    ASSERT_TRUE(m.createDisk(0, path, "", /*wp=*/false)) << m.lastError();
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(m.isDiskActive(0));

    EXPECT_EQ(m.diskGeometry(0).num_cyls,  40u);   // Geometrie kommt vom LAUFWERK
    EXPECT_EQ(m.diskGeometry(0).num_heads,  1u);
    EXPECT_FALSE(m.isDiskFormatted(0));
    EXPECT_FALSE(m.isDiskRawCompatible(0));

    m.unmountDisk(0);
}

/**
 * @test A5120DiskApi/CreateDisk_LeerdisketteAlsImgAbgelehnt
 * @brief Ein rohes Sektorimage kann „nicht formatiert" nicht ausdrücken.
 */
TEST(A5120DiskApi, CreateDisk_LeerdisketteAlsImgAbgelehnt) {
    A5120Machine m(withDrives("K5600.10"));
    k1520test::TempDisk path_disk = tmpPath("blank_reject.img");
    const std::string& path = path_disk.path();
    std::filesystem::remove(path);

    EXPECT_FALSE(m.createDisk(0, path, "", /*wp=*/false));
    EXPECT_NE(m.lastError().find(".img"), std::string::npos) << m.lastError();
    EXPECT_FALSE(std::filesystem::exists(path));
}

/**
 * @test A5120DiskApi/CreateDisk_MitFormatnamenVorformatiert
 * @brief Mit gesetztem Formatnamen entsteht weiterhin eine vorformatierte Diskette
 *        im Standardformat des Slots (k5601_ss40_5x1024 = 40×1×5×1024 B).
 */
TEST(A5120DiskApi, CreateDisk_MitFormatnamenVorformatiert) {
    A5120Machine m(withDrives("K5600.10"));
    k1520test::TempDisk path_disk = tmpPath("default.img");
    const std::string& path = path_disk.path();

    ASSERT_TRUE(m.createDisk(0, path, m.defaultFormatName(0), /*wp=*/false)) << m.lastError();
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(m.isDiskActive(0));
    EXPECT_TRUE(m.isDiskFormatted(0));
    EXPECT_TRUE(m.isDiskRawCompatible(0));
    EXPECT_EQ(std::filesystem::file_size(path), 40u * 1u * 5u * 1024u);

    m.unmountDisk(0);
}

/**
 * @test A5120DiskApi/SaveDiskAs_WechseltContainerUndBindetUm
 * @brief „Speichern unter" schreibt in einen beliebigen Container und bindet um;
 *        das Diskettenformat wird nur für `.img` verlangt.
 */
TEST(A5120DiskApi, SaveDiskAs_WechseltContainerUndBindetUm) {
    A5120Machine m(withDrives("K5600.10"));
    k1520test::TempDisk img_disk = tmpPath("saveas_src.img");
    const std::string& img = img_disk.path();
    k1520test::TempDisk dmk_disk = tmpPath("saveas_ziel.dmk");
    const std::string& dmk = dmk_disk.path();
    std::filesystem::remove(img);
    std::filesystem::remove(dmk);

    ASSERT_TRUE(m.createDisk(0, img, m.defaultFormatName(0), false)) << m.lastError();
    ASSERT_EQ(m.diskContainer(0), "img");

    ASSERT_TRUE(m.saveDiskAs(0, dmk, /*format_name=*/"")) << m.lastError();
    EXPECT_TRUE(std::filesystem::exists(dmk));
    EXPECT_EQ(m.diskContainer(0), "dmk");
    EXPECT_EQ(m.diskPath(0), dmk);

    m.unmountDisk(0);
    std::filesystem::remove(img);
    std::filesystem::remove(dmk);
}

/**
 * @test A5120DiskApi/SaveDiskAs_ImgOhneFormatnamenAbgelehnt
 */
TEST(A5120DiskApi, SaveDiskAs_ImgOhneFormatnamenAbgelehnt) {
    A5120Machine m(withDrives("K5600.10"));
    k1520test::TempDisk src_disk = tmpPath("saveas2_src.hfe");
    const std::string& src = src_disk.path();
    std::filesystem::remove(src);
    ASSERT_TRUE(m.createDisk(0, src, m.defaultFormatName(0), false)) << m.lastError();

    EXPECT_FALSE(m.saveDiskAs(0, tmpPath("saveas2_ziel.img"), ""));
    EXPECT_NE(m.lastError().find("Diskettenformat"), std::string::npos) << m.lastError();

    m.unmountDisk(0);
    std::filesystem::remove(src);
}

/**
 * @test A5120DiskApi/CreateDisk_LehntInkompatiblesFormatAb
 * @brief Ein Format, das der Slot nicht führt, wird beim ANLEGEN abgelehnt.
 *
 * Genau der vom Nutzer geforderte Schutz: kein doppelseitiges Format an einem
 * einseitigen Laufwerk.  Beim Anlegen bestimmt das Format die Struktur, hier muss
 * die Prüfung greifen.
 * @par Kriterium  createDisk() == false; lastError() nennt Format und Laufwerk; die
 *                 Datei wird nicht angelegt.
 */
TEST(A5120DiskApi, CreateDisk_LehntInkompatiblesFormatAb) {
    A5120Machine m(withDrives("K5600.20"));           // 80 Spuren, EINSEITIG
    k1520test::TempDisk path_disk = tmpPath("incompatible.img");
    const std::string& path = path_disk.path();

    EXPECT_FALSE(m.createDisk(0, path, "cpa800", /*wp=*/false));
    EXPECT_NE(m.lastError().find("cpa800"), std::string::npos)     << m.lastError();
    EXPECT_NE(m.lastError().find("K5600.20"), std::string::npos)  << m.lastError();
    EXPECT_FALSE(std::filesystem::exists(path));
}

/**
 * @test A5120DiskApi/CreateDisk_LehntUnbekanntesFormatAb
 * @brief Ein nicht im Katalog vorhandener Formatname wird mit Nennung abgelehnt.
 * @par Kriterium  createDisk() == false; lastError() nennt den Namen.
 */
TEST(A5120DiskApi, CreateDisk_LehntUnbekanntesFormatAb) {
    A5120Machine m(withDrives("K5601"));
    k1520test::TempDisk path_disk = tmpPath("unknown.img");
    const std::string& path = path_disk.path();

    EXPECT_FALSE(m.createDisk(0, path, "gibt_es_nicht", /*wp=*/false));
    EXPECT_NE(m.lastError().find("gibt_es_nicht"), std::string::npos) << m.lastError();
}

/**
 * @test A5120DiskApi/CreateDisk_AchtZollFmLaufwerkErzeugtFmMedium
 * @brief Das Verfahren kommt aus dem FORMAT, nicht mehr aus dem Laufwerkstyp.
 * @par Kriterium  Die für MF3200 angelegte Diskette meldet Encoding::FM.
 */
TEST(A5120DiskApi, CreateDisk_AchtZollFmLaufwerkErzeugtFmMedium) {
    A5120Machine m(withDrives("MF3200"));
    k1520test::TempDisk path_disk = tmpPath("fm.hfe");
    const std::string& path = path_disk.path();

    ASSERT_TRUE(m.createDisk(0, path, "", /*wp=*/false)) << m.lastError();
    auto img = DiskImage::open(path, std::nullopt, /*wp=*/true);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->geometry().encoding, Encoding::FM);
    EXPECT_EQ(img->geometry().num_cyls, 77);
    EXPECT_EQ(img->geometry().num_heads, 1);

    m.unmountDisk(0);
}

// ─── mountDisk: bewusst KEINE drives:-Prüfung ────────────────────────────────

/**
 * @test A5120DiskApi/MountDisk_ErzwingtLaufwerksKompatibilitaetBewusstNicht
 * @brief Ein vorhandenes .hfe lässt sich mit einem Platzhalter-Formatnamen mounten,
 *        der nicht zum Laufwerk des Slots gehört.
 *
 * **Das ist Absicht, keine vergessene Prüfung** (§8.6.6, Abweichung 2):
 *  - bei self-describing `.hfe` ist der Formatname bedeutungslos, die Geometrie steht
 *    in der Datei — `tools/format_driver` mountet deshalb ALLE Slots nominell als
 *    "cpa780", auch die 8″-Laufwerke der Combo-Boot-Tests;
 *  - der Laufwerkstyp ist auf der A5120 reine BIOS-Software, Combo-Disketten betreiben
 *    an B:/C: bewusst Fremdtypen.
 *
 * Wird hier eine `drives:`-Prüfung eingebaut, brechen die vier langsamen
 * `format_integration`-Bootdisk-Tests mit einer nichtssagenden Meldung.  Dieser Test
 * hält die Absicht fest und schlägt stattdessen sofort und erklärend fehl.
 * @par Kriterium  mountDisk() mit "cpa780" auf einem 8″-FM-Slot == true.
 */
TEST(A5120DiskApi, MountDisk_ErzwingtLaufwerksKompatibilitaetBewusstNicht) {
    A5120Machine m(withDrives("MF3200"));
    k1520test::TempDisk path_disk = tmpPath("combo.hfe");
    const std::string& path = path_disk.path();

    // Gültige 8″-FM-Diskette im Standardformat des Slots anlegen …
    ASSERT_TRUE(m.createDisk(0, path, m.defaultFormatName(0), /*wp=*/false)) << m.lastError();
    ASSERT_TRUE(m.unmountDisk(0));

    // … und wie format_driver nominell als "cpa780" mounten (5,25″-Format, das für
    // dieses Laufwerk NICHT gelistet ist).  Muss gelingen.
    EXPECT_TRUE(m.mountDisk(0, path, "cpa780", /*wp=*/false)) << m.lastError();
    EXPECT_TRUE(m.isDiskActive(0));

    m.unmountDisk(0);
}

/**
 * @test A5120DiskApi/MountDisk_LehntUnbekanntesFormatWeiterhinAb
 * @brief Ein unbekannter Formatname bleibt auch beim Mounten ein Fehler.
 *
 * Abgrenzung zum Test darüber: nicht geprüft wird die Laufwerks-ZUORDNUNG, sehr wohl
 * aber die EXISTENZ des Formats.
 * @par Kriterium  mountDisk() == false; lastError() nennt den Namen.
 */
TEST(A5120DiskApi, MountDisk_LehntUnbekanntesFormatWeiterhinAb) {
    A5120Machine m(withDrives("K5601"));
    k1520test::TempDisk path_disk = tmpPath("known.img");
    const std::string& path = path_disk.path();
    ASSERT_TRUE(m.createDisk(0, path, m.defaultFormatName(0), /*wp=*/false)) << m.lastError();
    ASSERT_TRUE(m.unmountDisk(0));

    EXPECT_FALSE(m.mountDisk(0, path, "gibt_es_nicht", /*wp=*/false));
    EXPECT_NE(m.lastError().find("gibt_es_nicht"), std::string::npos) << m.lastError();
}

// ─── Startabbruch bei kaputtem Katalog ───────────────────────────────────────

/**
 * @test A5120DiskApi/KaputterKatalog_BrichtStartMitKlarerMeldungAb
 * @brief Ein syntaktisch kaputter Katalog lässt die Maschine gar nicht erst entstehen.
 *
 * `K1520_FORMATS` hat die höchste Priorität im Suchpfad (§8.6.4).  Ein Syntaxfehler
 * ist bewusst FATAL — sonst liefe der Emulator still mit einem halben Katalog weiter.
 * Der C-API-Aufrufer bekommt den Grund über `k1520_last_init_error()`, die GUI beendet
 * sich damit.
 * @par Kriterium  Konstruktor wirft std::runtime_error; die Meldung nennt Datei und Zeile.
 */
TEST(A5120DiskApi, KaputterKatalog_BrichtStartMitKlarerMeldungAb) {
    k1520test::TempDisk bad_disk = tmpPath("broken.yaml");
    const std::string& bad = bad_disk.path();
    { std::ofstream f(bad); f << "version: 1\nformats:\n\t- name: mit_tab\n"; }

    const char* prev = std::getenv("K1520_FORMATS");
    const std::string saved = prev ? prev : "";
    k1520::os::setEnv("K1520_FORMATS", bad.c_str());

    std::string message;
    try {
        A5120Machine m;
        ADD_FAILURE() << "Konstruktor hätte werfen müssen";
    } catch (const std::runtime_error& e) {
        message = e.what();
    }

    if (saved.empty()) k1520::os::unsetEnv("K1520_FORMATS");
    else               k1520::os::setEnv("K1520_FORMATS", saved.c_str());
    std::filesystem::remove(bad);

    EXPECT_NE(message.find("broken.yaml"), std::string::npos) << message;
    EXPECT_NE(message.find(":3"), std::string::npos)          << message;
}

/**
 * @test A5120DiskApi/NachStartabbruch_BleibtMaschineWeiterBaubar
 * @brief Der Startabbruch hinterlässt keinen kaputten globalen Zustand.
 *
 * Absicherung gegen versehentlich statisch zwischengespeicherte Kataloge: nach einem
 * fehlgeschlagenen Konstruktor muss die nächste Maschine mit gültigem Katalog wieder
 * normal entstehen (relevant, weil die Tests in EINEM Prozess laufen).
 * @par Kriterium  Konstruktor gelingt; das Standardformat ist wieder da.
 */
TEST(A5120DiskApi, NachStartabbruch_BleibtMaschineWeiterBaubar) {
    A5120Machine m;
    EXPECT_EQ(m.defaultFormatName(0), "cpa800");
    EXPECT_FALSE(m.formatCatalog().sources().empty());
}
