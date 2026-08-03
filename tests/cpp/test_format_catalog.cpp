/**
 * @file test_format_catalog.cpp
 * @brief Unit-Tests für den YAML-Diskettenformat-Katalog (`FormatCatalog`).
 *
 * @details
 * Getestete Komponente: **FormatCatalog** (`core/peripherals/floppy_drive/format_catalog.h`),
 * der die Formatdefinitionen aus `data/formats.yaml` lädt (doc/K1520_architecture.md §8.6)
 * und die früher einkompilierten `FormatParser::builtinFormats()` ablöst.
 *
 * | Gruppe                | Inhalt                                                      |
 * |-----------------------|-------------------------------------------------------------|
 * | Ausgelieferter Katalog| data/formats.yaml lädt; boot-kritische Geometrien unverändert|
 * | Fehler-Isolation      | kaputte Definition wird übersprungen, gute bleiben nutzbar   |
 * | Fatale Fehler         | fehlende Datei / Syntaxfehler / leerer Katalog               |
 * | Kompatibilität        | drives:/default_for: steuern Auswahl und Standardformat      |
 * | Mischdichte           | Verfahren pro Spurbereich (FM-Systemspur + MFM-Daten)        |
 *
 * @see core/peripherals/floppy_drive/format_catalog.h
 */

#include <gtest/gtest.h>
#include "core/peripherals/floppy_drive/format_catalog.h"

#include <filesystem>
#include <fstream>

namespace {

/// Schreibt @p text in eine temporäre .yaml-Datei und gibt den Pfad zurück.
std::string writeTmp(const std::string& stem, const std::string& text) {
    const auto p = std::filesystem::temp_directory_path()
                 / ("k1520_fmt_" + stem + ".yaml");
    std::ofstream f(p);
    f << text;
    f.close();
    return p.string();
}

FormatCatalog loadText(const std::string& stem, const std::string& text,
                       std::string* fatal) {
    const std::string path = writeTmp(stem, text);
    FormatCatalog cat = FormatCatalog::load({path}, fatal);
    std::filesystem::remove(path);
    return cat;
}

/// Pfad der ausgelieferten data/formats.yaml (vom Build als Define gesetzt).
const char* shippedCatalog() { return K1520_FORMATS_DEFAULT; }

}  // namespace

// ─── Ausgelieferter Katalog ──────────────────────────────────────────────────

/**
 * @test FormatCatalog/AusgelieferterKatalog_LaedtOhneBeanstandung
 * @brief data/formats.yaml lädt fehlerfrei und ohne übersprungene Definitionen.
 * @par Kriterium  Kein fataler Fehler, issues() leer, mindestens 25 Formate.
 */
TEST(FormatCatalog, AusgelieferterKatalog_LaedtOhneBeanstandung) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({shippedCatalog()}, &fatal);

    EXPECT_TRUE(fatal.empty()) << fatal;
    for (const auto& i : cat.issues()) ADD_FAILURE() << "Beanstandung: " << i;
    EXPECT_GE(cat.formats().size(), 25u);
}

/**
 * @test FormatCatalog/BootKritischeGeometrien_Unveraendert
 * @brief cpa780/cpa800 haben exakt die Geometrie der früheren builtinFormats().
 *
 * Diese Erwartungswerte sind der Regressionsanker des Umbaus (§8.6.8/E2): der
 * Bootpfad hängt an der asymmetrischen cpa780-Aufteilung (Systembereich 3× 128 B,
 * Datenbereich ab Zyl 1 Kopf 1).
 * @par Kriterium  Spurbereiche, Sektorzahl/-größe und Gesamtgröße wie zuvor.
 */
TEST(FormatCatalog, BootKritischeGeometrien_Unveraendert) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({shippedCatalog()}, &fatal);
    ASSERT_TRUE(fatal.empty()) << fatal;

    const DiskFormat* f780 = cat.find("cpa780");
    ASSERT_NE(f780, nullptr);
    ASSERT_EQ(f780->tracks.size(), 4u);
    EXPECT_EQ(f780->numCylinders(), 80);
    EXPECT_EQ(f780->numHeads(), 2);

    struct Exp { int cf, cl, hf, hl, secs, bps; };
    const Exp exp780[4] = {
        {0, 0,  0, 1, 26, 128},
        {1, 1,  0, 0, 26, 128},
        {1, 1,  1, 1,  5, 1024},
        {2, 79, 0, 1,  5, 1024},
    };
    for (size_t i = 0; i < 4; ++i) {
        const TrackFormat& t = f780->tracks[i];
        EXPECT_EQ(t.cyl_first,      exp780[i].cf)   << "Spurbereich " << i;
        EXPECT_EQ(t.cyl_last,       exp780[i].cl)   << "Spurbereich " << i;
        EXPECT_EQ(t.head_first,     exp780[i].hf)   << "Spurbereich " << i;
        EXPECT_EQ(t.head_last,      exp780[i].hl)   << "Spurbereich " << i;
        EXPECT_EQ(t.secs_per_track, exp780[i].secs) << "Spurbereich " << i;
        EXPECT_EQ(t.bytes_per_sec,  exp780[i].bps)  << "Spurbereich " << i;
        // Boot-Invariante: die A5120-Disketten sind durchgängig MFM (§14.5).
        EXPECT_EQ(t.encoding, Encoding::MFM)        << "Spurbereich " << i;
    }

    const DiskFormat* f800 = cat.find("cpa800");
    ASSERT_NE(f800, nullptr);
    ASSERT_EQ(f800->tracks.size(), 1u);
    EXPECT_EQ(f800->tracks[0].secs_per_track, 5);
    EXPECT_EQ(f800->tracks[0].bytes_per_sec, 1024);
    EXPECT_EQ(f800->totalBytes(), 80u * 2u * 5u * 1024u);
}

/**
 * @test FormatCatalog/AchtZollFormate_VerfahrenWieBisher
 * @brief mf3200 ist FM (8″-SD), mf6400 ist MFM — wie die frühere Ableitung aus dem Laufwerk.
 * @par Kriterium  Verfahren und Geometrie stimmen; Laufwerkszuordnung passt.
 */
TEST(FormatCatalog, AchtZollFormate_VerfahrenWieBisher) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({shippedCatalog()}, &fatal);
    ASSERT_TRUE(fatal.empty()) << fatal;

    const DiskFormat* fm = cat.find("mf3200");
    ASSERT_NE(fm, nullptr);
    EXPECT_EQ(fm->predominantEncoding(), Encoding::FM);
    EXPECT_EQ(fm->numCylinders(), 77);
    EXPECT_EQ(fm->numHeads(), 1);
    EXPECT_TRUE(fm->supportsDrive("mf3200_8_ss77"));

    const DiskFormat* mfm = cat.find("mf6400");
    ASSERT_NE(mfm, nullptr);
    EXPECT_EQ(mfm->predominantEncoding(), Encoding::MFM);
    EXPECT_EQ(mfm->tracks[0].secs_per_track, 8);
}

/**
 * @test FormatCatalog/JedesProfil_HatEinStandardformat
 * @brief Für jedes bestückte Laufwerksprofil benennt der Katalog ein `default_for`.
 * @par Kriterium  defaultFor() liefert für alle Profile außer "none" ein Format.
 */
TEST(FormatCatalog, JedesProfil_HatEinStandardformat) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({shippedCatalog()}, &fatal);
    ASSERT_TRUE(fatal.empty()) << fatal;

    for (const auto& name : knownDriveProfileNames()) {
        const DriveProfile& p = builtinDriveProfile(name);
        if (!p.present) continue;                      // "none" hat keine Diskette
        const DiskFormat* def = cat.defaultFor(p);
        EXPECT_NE(def, nullptr) << "kein default_for für Profil '" << name << "'";
        if (def) EXPECT_TRUE(def->supportsDrive(name));
    }
}

/**
 * @test FormatCatalog/ForDrive_NurExplizitGelisteteFormate
 * @brief forDrive() liefert genau die Formate mit passendem `drives:`-Eintrag, Standard zuerst.
 * @par Kriterium  Ein 8″-FM-Format erscheint nicht in der Liste eines 5,25″-Laufwerks.
 */
TEST(FormatCatalog, ForDrive_NurExplizitGelisteteFormate) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({shippedCatalog()}, &fatal);
    ASSERT_TRUE(fatal.empty()) << fatal;

    const auto list = cat.forDrive(builtinDriveProfile("K5601"));
    ASSERT_FALSE(list.empty());
    EXPECT_EQ(list.front()->name, "cpa800");            // default_for zuerst

    for (const DiskFormat* f : list) {
        EXPECT_TRUE(f->supportsDrive("K5601")) << f->name;
        EXPECT_NE(f->name, "mf3200") << "8″-FM-Format darf nicht am K5601 erscheinen";
    }

    // Einseitiges Laufwerk bekommt kein doppelseitiges Format angeboten.
    for (const DiskFormat* f : cat.forDrive(builtinDriveProfile("ss_525_80")))
        EXPECT_LE(f->numHeads(), 1) << f->name;

    // Unbestückter Slot bietet nichts an.
    EXPECT_TRUE(cat.forDrive(builtinDriveProfile("none")).empty());
}

// ─── Fehler-Isolation je Formatdefinition ────────────────────────────────────

/**
 * @test FormatCatalog/FehlerhafteDefinition_WirdUebersprungen
 * @brief Eine kaputte Definition wird gemeldet und übersprungen; gültige bleiben nutzbar.
 * @par Kriterium  Nur das gute Format ist im Katalog; issues() nennt Name und Grund.
 */
TEST(FormatCatalog, FehlerhafteDefinition_WirdUebersprungen) {
    std::string fatal;
    FormatCatalog cat = loadText("skip",
        "version: 1\n"
        "formats:\n"
        "  - name: gut\n"
        "    drives: [K5601]\n"
        "    encoding: mfm\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n"
        "  - name: kaputt_groesse\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 999 }\n",
        &fatal);

    EXPECT_TRUE(fatal.empty()) << fatal;
    EXPECT_NE(cat.find("gut"), nullptr);
    EXPECT_EQ(cat.find("kaputt_groesse"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("kaputt_groesse"), std::string::npos);
    EXPECT_NE(cat.issues()[0].find("999"), std::string::npos);
}

/**
 * @test FormatCatalog/UnbekanntesLaufwerksprofil_WirdErkannt
 * @brief Ein Tippfehler im Profilnamen wird erkannt (nicht still als Default akzeptiert).
 *
 * builtinDriveProfile() liefert für unbekannte Namen das Default-Profil — die
 * Validierung prüft deshalb gegen knownDriveProfileNames() (§8.6.5 V3).
 * @par Kriterium  Format übersprungen, Meldung nennt den falschen Namen.
 */
TEST(FormatCatalog, UnbekanntesLaufwerksprofil_WirdErkannt) {
    std::string fatal;
    FormatCatalog cat = loadText("badprofile",
        "formats:\n"
        "  - name: tippfehler\n"
        "    drives: [K5061]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n"
        "  - name: ok\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n",
        &fatal);

    EXPECT_TRUE(fatal.empty()) << fatal;
    EXPECT_EQ(cat.find("tippfehler"), nullptr);
    EXPECT_NE(cat.find("ok"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("K5061"), std::string::npos);
}

/**
 * @test FormatCatalog/GeometrieKonflikt_WirdAbgelehnt
 * @brief Ein doppelseitiges Format an einem einseitigen Laufwerk wird abgelehnt (V4).
 * @par Kriterium  Format übersprungen; Meldung nennt Köpfe und Laufwerk.
 */
TEST(FormatCatalog, GeometrieKonflikt_WirdAbgelehnt) {
    std::string fatal;
    FormatCatalog cat = loadText("geo",
        "formats:\n"
        "  - name: zweiseitig_an_einseitig\n"
        "    drives: [ss_525_80]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n"
        "  - name: gut\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n",
        &fatal);

    EXPECT_EQ(cat.find("zweiseitig_an_einseitig"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("ss_525_80"), std::string::npos);
    EXPECT_NE(cat.issues()[0].find("Köpfe"), std::string::npos);
}

/**
 * @test FormatCatalog/VerfahrensKonflikt_WirdAbgelehnt
 * @brief Ein MFM-Format an einem reinen FM-Laufwerk wird abgelehnt (V4).
 * @par Kriterium  Format übersprungen; Meldung nennt das Verfahren.
 */
TEST(FormatCatalog, VerfahrensKonflikt_WirdAbgelehnt) {
    std::string fatal;
    FormatCatalog cat = loadText("enc",
        "formats:\n"
        "  - name: mfm_an_fm_laufwerk\n"
        "    drives: [mf3200_8_ss77]\n"
        "    encoding: mfm\n"
        "    tracks:\n"
        "      - { cyls: 0-76, heads: 0, sectors: 4, size: 1024 }\n"
        "  - name: gut\n"
        "    drives: [mf3200_8_ss77]\n"
        "    encoding: fm\n"
        "    tracks:\n"
        "      - { cyls: 0-76, heads: 0, sectors: 4, size: 1024 }\n",
        &fatal);

    EXPECT_EQ(cat.find("mfm_an_fm_laufwerk"), nullptr);
    EXPECT_NE(cat.find("gut"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("MFM"), std::string::npos);
}

/**
 * @test FormatCatalog/UeberlappendeSpurbereiche_WerdenAbgelehnt
 * @brief Sich überschneidende Spurbereiche sind mehrdeutig und damit ungültig (V2).
 * @par Kriterium  Format übersprungen; Meldung nennt „überlappen".
 */
TEST(FormatCatalog, UeberlappendeSpurbereiche_WerdenAbgelehnt) {
    std::string fatal;
    FormatCatalog cat = loadText("overlap",
        "formats:\n"
        "  - name: ueberlappend\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-40, heads: 0-1, sectors: 5, size: 1024 }\n"
        "      - { cyls: 40-79, heads: 0-1, sectors: 5, size: 1024 }\n"
        "  - name: gut\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n",
        &fatal);

    EXPECT_EQ(cat.find("ueberlappend"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("überlappen"), std::string::npos);
}

/**
 * @test FormatCatalog/UnbekanntesFeld_IstNurHinweis
 * @brief Ein unbekanntes Feld wird gemeldet, verwirft die Definition aber nicht (V1b).
 * @par Kriterium  Format vorhanden; genau eine Beanstandung mit dem Feldnamen.
 */
TEST(FormatCatalog, UnbekanntesFeld_IstNurHinweis) {
    std::string fatal;
    FormatCatalog cat = loadText("unknownfield",
        "formats:\n"
        "  - name: mit_extra\n"
        "    drives: [K5601]\n"
        "    zukunftsfeld: 42\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n",
        &fatal);

    EXPECT_TRUE(fatal.empty()) << fatal;
    EXPECT_NE(cat.find("mit_extra"), nullptr);
    ASSERT_EQ(cat.issues().size(), 1u);
    EXPECT_NE(cat.issues()[0].find("zukunftsfeld"), std::string::npos);
}

// ─── Fatale Fehler ───────────────────────────────────────────────────────────

/**
 * @test FormatCatalog/FehlendeDatei_IstFatalUndNenntPfade
 * @brief Ohne Katalogdatei entsteht ein fataler Fehler, der alle gesuchten Pfade nennt.
 * @par Kriterium  fatal_error nicht leer und enthält den gesuchten Pfad.
 */
TEST(FormatCatalog, FehlendeDatei_IstFatalUndNenntPfade) {
    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({"/nicht/vorhanden/formats.yaml"}, &fatal);

    EXPECT_TRUE(cat.formats().empty());
    ASSERT_FALSE(fatal.empty());
    EXPECT_NE(fatal.find("/nicht/vorhanden/formats.yaml"), std::string::npos);
    EXPECT_NE(fatal.find("Gesucht"), std::string::npos);
}

/**
 * @test FormatCatalog/SyntaxFehler_IstFatal
 * @brief Ein YAML-Syntaxfehler in einer vorhandenen Datei bricht das Laden ab.
 * @par Kriterium  fatal_error nennt Datei und Zeilennummer.
 */
TEST(FormatCatalog, SyntaxFehler_IstFatal) {
    std::string fatal;
    FormatCatalog cat = loadText("syntax",
        "version: 1\n"
        "formats:\n"
        "\t- name: mit_tab\n",
        &fatal);

    EXPECT_TRUE(cat.formats().empty());
    ASSERT_FALSE(fatal.empty());
    EXPECT_NE(fatal.find(":3"), std::string::npos) << fatal;
}

/**
 * @test FormatCatalog/NurFehlerhafteFormate_IstFatal
 * @brief Enthält die Datei kein einziges gültiges Format, ist das ein Startabbruch.
 * @par Kriterium  fatal_error nicht leer und listet die Einzelgründe auf.
 */
TEST(FormatCatalog, NurFehlerhafteFormate_IstFatal) {
    std::string fatal;
    FormatCatalog cat = loadText("allbad",
        "formats:\n"
        "  - name: ohne_spuren\n"
        "    drives: [K5601]\n",
        &fatal);

    EXPECT_TRUE(cat.formats().empty());
    ASSERT_FALSE(fatal.empty());
    EXPECT_NE(fatal.find("ohne_spuren"), std::string::npos);
}

/**
 * @test FormatCatalog/FalscheSchemaVersion_IstFatal
 * @brief Eine unbekannte `version:` wird abgelehnt statt halb interpretiert.
 * @par Kriterium  fatal_error nennt die Version.
 */
TEST(FormatCatalog, FalscheSchemaVersion_IstFatal) {
    std::string fatal;
    FormatCatalog cat = loadText("version",
        "version: 99\n"
        "formats:\n"
        "  - name: egal\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n",
        &fatal);

    EXPECT_TRUE(cat.formats().empty());
    ASSERT_FALSE(fatal.empty());
    EXPECT_NE(fatal.find("99"), std::string::npos);
}

// ─── Mischdichte (das eigentliche Ziel des Umbaus) ───────────────────────────

/**
 * @test FormatCatalog/Mischdichte_VerfahrenProSpurbereich
 * @brief FM-Systemspur + MFM-Datenspuren in EINEM Format (§8.6).
 * @par Kriterium  Spur 0 ist FM, Datenspuren MFM; predominant = MFM; isMixed = true.
 */
TEST(FormatCatalog, Mischdichte_VerfahrenProSpurbereich) {
    std::string fatal;
    FormatCatalog cat = loadText("mixed",
        "formats:\n"
        "  - name: mischdichte\n"
        "    description: \"8″ System-34 — FM-Systemspur, MFM-Daten\"\n"
        "    drives: [mf6400_8_ss77]\n"
        "    tracks:\n"
        "      - { cyls: 0,    heads: 0, sectors: 26, size: 128,  encoding: fm  }\n"
        "      - { cyls: 1-76, heads: 0, sectors: 8,  size: 1024, encoding: mfm }\n",
        &fatal);

    ASSERT_TRUE(fatal.empty()) << fatal;
    const DiskFormat* f = cat.find("mischdichte");
    ASSERT_NE(f, nullptr);

    EXPECT_TRUE(f->isMixedEncoding());
    ASSERT_NE(f->findTrack(0, 0), nullptr);
    EXPECT_EQ(f->findTrack(0, 0)->encoding, Encoding::FM);
    EXPECT_EQ(f->findTrack(0, 0)->bytes_per_sec, 128);
    ASSERT_NE(f->findTrack(40, 0), nullptr);
    EXPECT_EQ(f->findTrack(40, 0)->encoding, Encoding::MFM);
    EXPECT_EQ(f->findTrack(40, 0)->bytes_per_sec, 1024);

    // Der HFE-Header trägt nur EIN Verfahren → das der Mehrheit der Spuren.
    EXPECT_EQ(f->predominantEncoding(), Encoding::MFM);
}

/**
 * @test FormatCatalog/EncodingVorgabe_WirdVonSpurUeberschrieben
 * @brief Die Format-Vorgabe `encoding:` gilt für Spuren ohne eigene Angabe.
 * @par Kriterium  Spur ohne Angabe erbt fm, Spur mit Angabe behält mfm.
 */
TEST(FormatCatalog, EncodingVorgabe_WirdVonSpurUeberschrieben) {
    std::string fatal;
    FormatCatalog cat = loadText("encdefault",
        "formats:\n"
        "  - name: vorgabe\n"
        "    drives: [mf6400_8_ss77]\n"
        "    encoding: fm\n"
        "    tracks:\n"
        "      - { cyls: 0,    heads: 0, sectors: 26, size: 128 }\n"
        "      - { cyls: 1-76, heads: 0, sectors: 8,  size: 1024, encoding: mfm }\n",
        &fatal);

    ASSERT_TRUE(fatal.empty()) << fatal;
    const DiskFormat* f = cat.find("vorgabe");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->tracks[0].encoding, Encoding::FM);    // geerbt
    EXPECT_EQ(f->tracks[1].encoding, Encoding::MFM);   // überschrieben
}

// ─── Mehrere Dateien / Override ──────────────────────────────────────────────

/**
 * @test FormatCatalog/SpaetereDatei_UeberschreibtGleichenNamen
 * @brief Eine Datei höherer Priorität ersetzt ein gleichnamiges Format (User-Override).
 * @par Kriterium  Die Definition der zweiten Datei gewinnt; Anzahl bleibt 1.
 */
TEST(FormatCatalog, SpaetereDatei_UeberschreibtGleichenNamen) {
    const std::string a = writeTmp("ovr_a",
        "formats:\n"
        "  - name: gleich\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 5, size: 1024 }\n");
    const std::string b = writeTmp("ovr_b",
        "formats:\n"
        "  - name: gleich\n"
        "    drives: [K5601]\n"
        "    tracks:\n"
        "      - { cyls: 0-79, heads: 0-1, sectors: 16, size: 256 }\n");

    std::string fatal;
    FormatCatalog cat = FormatCatalog::load({a, b}, &fatal);
    EXPECT_TRUE(fatal.empty()) << fatal;
    EXPECT_EQ(cat.formats().size(), 1u);
    ASSERT_NE(cat.find("gleich"), nullptr);
    EXPECT_EQ(cat.find("gleich")->tracks[0].secs_per_track, 16);   // zweite Datei gewinnt
    EXPECT_EQ(cat.sources().size(), 2u);

    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

/**
 * @test FormatCatalog/SearchPaths_EnthaeltCompileDefault
 * @brief Die Suchpfadliste enthält den Build-Fallback, damit Tools ihn ohne ENV finden.
 * @par Kriterium  searchPaths() ist nicht leer und enthält K1520_FORMATS_DEFAULT.
 */
TEST(FormatCatalog, SearchPaths_EnthaeltCompileDefault) {
    const auto paths = FormatCatalog::searchPaths();
    ASSERT_FALSE(paths.empty());
    EXPECT_EQ(paths.front(), std::string(K1520_FORMATS_DEFAULT));
}
