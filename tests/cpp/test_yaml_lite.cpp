/**
 * @file test_yaml_lite.cpp
 * @brief Unit-Tests für den minimalen YAML-Subset-Parser (`core/util/yaml_lite.h`).
 *
 * @details
 * Getestete Komponente: **yaml::parse** — der handgeschriebene Parser, der die
 * Konfigurationsdateien des Kerns liest (`data/formats.yaml`, siehe
 * doc/K1520_architecture.md §8.6.2).
 *
 * | Gruppe            | Inhalt                                                     |
 * |-------------------|------------------------------------------------------------|
 * | Skalare/Maps      | Schlüssel-Wert, Quotes, Kommentare, Zahlen, Bools          |
 * | Listen            | Block-Listen, Listen von Maps, verschachtelte Blöcke       |
 * | Flow-Ausdrücke    | `{a: 1}`, `[a, b]`, verschachtelt, in Listeneinträgen      |
 * | Fehlerfälle       | Tabs, doppelte Keys, Anchors, kaputte Flows — mit Zeile    |
 *
 * @see core/util/yaml_lite.h
 */

#include <gtest/gtest.h>
#include "core/util/yaml_lite.h"

namespace {

yaml::Node parseOk(const std::string& text) {
    yaml::Node  n;
    yaml::Error e;
    EXPECT_TRUE(yaml::parse(text, n, e)) << "Zeile " << e.line << ": " << e.message;
    return n;
}

yaml::Error parseFail(const std::string& text) {
    yaml::Node  n;
    yaml::Error e;
    EXPECT_FALSE(yaml::parse(text, n, e)) << "Parser hätte scheitern müssen";
    return e;
}

}  // namespace

// ─── Maps und Skalare ────────────────────────────────────────────────────────

/**
 * @test YamlLite/Map_SchluesselWertUndKommentare
 * @brief Einfache Map; Kommentare und Leerzeilen werden ignoriert.
 * @par Kriterium  Beide Schlüssel vorhanden, Kommentar nicht Teil des Werts.
 */
TEST(YamlLite, Map_SchluesselWertUndKommentare) {
    auto n = parseOk("# Kopfkommentar\n"
                     "version: 1\n"
                     "\n"
                     "name: cpa800   # Zeilenkommentar\n");
    ASSERT_TRUE(n.isMap());
    ASSERT_NE(n.find("version"), nullptr);
    EXPECT_EQ(n.find("version")->scalar, "1");
    EXPECT_EQ(n.find("name")->scalar, "cpa800");
}

/**
 * @test YamlLite/Skalare_QuotesUndSonderzeichen
 * @brief Quotes werden entfernt; '#' und ':' innerhalb von Quotes bleiben erhalten.
 * @par Kriterium  Wert enthält Doppelpunkt und Rautenzeichen unverändert.
 */
TEST(YamlLite, Skalare_QuotesUndSonderzeichen) {
    auto n = parseOk("a: \"CP/A 780K: Boot # nicht kommentiert\"\n"
                     "b: 'einfach gequotet'\n"
                     "c: bare wert\n");
    EXPECT_EQ(n.find("a")->scalar, "CP/A 780K: Boot # nicht kommentiert");
    EXPECT_EQ(n.find("b")->scalar, "einfach gequotet");
    EXPECT_EQ(n.find("c")->scalar, "bare wert");
}

/**
 * @test YamlLite/Konvertierung_IntUndBool
 * @brief toInt akzeptiert dezimal und 0x…; toBool die üblichen Schreibweisen.
 * @par Kriterium  Gültige Werte konvertieren, ungültige werden abgelehnt.
 */
TEST(YamlLite, Konvertierung_IntUndBool) {
    long v = 0;
    EXPECT_TRUE(yaml::toInt("42", v));    EXPECT_EQ(v, 42);
    EXPECT_TRUE(yaml::toInt("0x1F", v));  EXPECT_EQ(v, 31);
    EXPECT_FALSE(yaml::toInt("12abc", v));
    EXPECT_FALSE(yaml::toInt("", v));

    bool b = false;
    EXPECT_TRUE(yaml::toBool("true", b));  EXPECT_TRUE(b);
    EXPECT_TRUE(yaml::toBool("no", b));    EXPECT_FALSE(b);
    EXPECT_FALSE(yaml::toBool("vielleicht", b));
}

/**
 * @test YamlLite/Map_VerschachtelterBlock
 * @brief Ein Schlüssel ohne Wert öffnet einen eingerückten Unterblock.
 * @par Kriterium  Verschachtelte Map ist über zwei Ebenen erreichbar.
 */
TEST(YamlLite, Map_VerschachtelterBlock) {
    auto n = parseOk("aussen:\n"
                     "  innen:\n"
                     "    tief: 7\n");
    ASSERT_NE(n.find("aussen"), nullptr);
    ASSERT_TRUE(n.find("aussen")->isMap());
    const yaml::Node* innen = n.find("aussen")->find("innen");
    ASSERT_NE(innen, nullptr);
    EXPECT_EQ(innen->find("tief")->scalar, "7");
}

// ─── Listen ──────────────────────────────────────────────────────────────────

/**
 * @test YamlLite/Liste_EinfacheSkalare
 * @brief Block-Liste aus Skalaren.
 * @par Kriterium  Drei Einträge in Dateireihenfolge.
 */
TEST(YamlLite, Liste_EinfacheSkalare) {
    auto n = parseOk("drives:\n"
                     "  - K5601\n"
                     "  - ss_525_80\n"
                     "  - mf3200_8_ss77\n");
    const yaml::Node* d = n.find("drives");
    ASSERT_NE(d, nullptr);
    ASSERT_TRUE(d->isList());
    ASSERT_EQ(d->items.size(), 3u);
    EXPECT_EQ(d->items[0].scalar, "K5601");
    EXPECT_EQ(d->items[2].scalar, "mf3200_8_ss77");
}

/**
 * @test YamlLite/Liste_VonMaps_MehrzeiligeEintraege
 * @brief `- key: wert` startet eine Map, deren Folgezeilen zum selben Eintrag gehören.
 * @par Kriterium  Zwei Einträge mit je zwei Feldern; Felder korrekt zugeordnet.
 */
TEST(YamlLite, Liste_VonMaps_MehrzeiligeEintraege) {
    auto n = parseOk("formats:\n"
                     "  - name: cpa800\n"
                     "    description: gross\n"
                     "  - name: cpa640\n"
                     "    description: klein\n");
    const yaml::Node* f = n.find("formats");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(f->items.size(), 2u);
    EXPECT_EQ(f->items[0].find("name")->scalar, "cpa800");
    EXPECT_EQ(f->items[0].find("description")->scalar, "gross");
    EXPECT_EQ(f->items[1].find("name")->scalar, "cpa640");
    EXPECT_EQ(f->items[1].find("description")->scalar, "klein");
}

/**
 * @test YamlLite/Liste_MapMitVerschachtelterListe
 * @brief Ein Listeneintrag trägt selbst eine Liste (formats[].tracks[]).
 * @par Kriterium  Der innere Spurbereich ist über beide Ebenen erreichbar.
 */
TEST(YamlLite, Liste_MapMitVerschachtelterListe) {
    auto n = parseOk("formats:\n"
                     "  - name: cpa780\n"
                     "    tracks:\n"
                     "      - cyls: 0\n"
                     "        sectors: 26\n"
                     "      - cyls: 2-79\n"
                     "        sectors: 5\n");
    const yaml::Node* tr = n.find("formats")->items[0].find("tracks");
    ASSERT_NE(tr, nullptr);
    ASSERT_EQ(tr->items.size(), 2u);
    EXPECT_EQ(tr->items[0].find("cyls")->scalar, "0");
    EXPECT_EQ(tr->items[1].find("cyls")->scalar, "2-79");
    EXPECT_EQ(tr->items[1].find("sectors")->scalar, "5");
}

// ─── Flow-Ausdrücke ──────────────────────────────────────────────────────────

/**
 * @test YamlLite/Flow_ListeUndMap
 * @brief Einzeilige Flow-Liste und Flow-Map werden geparst.
 * @par Kriterium  Werte identisch zur Blockschreibweise.
 */
TEST(YamlLite, Flow_ListeUndMap) {
    auto n = parseOk("drives: [K5601, mfs_525_ds80]\n"
                     "track: { cyls: 2-79, heads: 0-1, sectors: 5, size: 1024 }\n");
    const yaml::Node* d = n.find("drives");
    ASSERT_TRUE(d->isList());
    ASSERT_EQ(d->items.size(), 2u);
    EXPECT_EQ(d->items[1].scalar, "mfs_525_ds80");

    const yaml::Node* t = n.find("track");
    ASSERT_TRUE(t->isMap());
    EXPECT_EQ(t->find("cyls")->scalar, "2-79");
    EXPECT_EQ(t->find("size")->scalar, "1024");
}

/**
 * @test YamlLite/Flow_AlsListeneintrag
 * @brief `- { … }` — die im Katalog benutzte kompakte Spurbereichs-Schreibweise.
 * @par Kriterium  Beide Spurbereiche als Maps mit korrekten Feldern.
 */
TEST(YamlLite, Flow_AlsListeneintrag) {
    auto n = parseOk("tracks:\n"
                     "  - { cyls: 0, heads: 0-1, sectors: 26, size: 128 }\n"
                     "  - { cyls: 1, heads: 1,   sectors: 5,  size: 1024 }\n");
    const yaml::Node* t = n.find("tracks");
    ASSERT_EQ(t->items.size(), 2u);
    EXPECT_EQ(t->items[0].find("sectors")->scalar, "26");
    EXPECT_EQ(t->items[1].find("heads")->scalar, "1");
    EXPECT_EQ(t->items[1].find("size")->scalar, "1024");
}

/**
 * @test YamlLite/Flow_LeerUndVerschachtelt
 * @brief Leere Flow-Container und Verschachtelung von Liste in Map.
 * @par Kriterium  Leere Container haben 0 Elemente; verschachtelte Liste lesbar.
 */
TEST(YamlLite, Flow_LeerUndVerschachtelt) {
    auto n = parseOk("leer_liste: []\n"
                     "leer_map: {}\n"
                     "tief: { a: [1, 2], b: { c: 3 } }\n");
    EXPECT_TRUE(n.find("leer_liste")->isList());
    EXPECT_EQ(n.find("leer_liste")->items.size(), 0u);
    EXPECT_TRUE(n.find("leer_map")->isMap());
    EXPECT_EQ(n.find("tief")->find("a")->items.size(), 2u);
    EXPECT_EQ(n.find("tief")->find("b")->find("c")->scalar, "3");
}

// ─── Fehlerfälle ─────────────────────────────────────────────────────────────

/**
 * @test YamlLite/Fehler_TabInEinrueckung
 * @brief Tabulatoren in der Einrückung sind ein Fehler mit Zeilenangabe.
 * @par Kriterium  parse() == false, Zeile 2, Meldung nennt „Tabulator".
 */
TEST(YamlLite, Fehler_TabInEinrueckung) {
    auto e = parseFail("a:\n\tb: 1\n");
    EXPECT_EQ(e.line, 2);
    EXPECT_NE(e.message.find("Tabulator"), std::string::npos);
}

/**
 * @test YamlLite/Fehler_DoppelterSchluessel
 * @brief Ein doppelter Schlüssel in derselben Map wird abgelehnt.
 * @par Kriterium  parse() == false; Meldung nennt den Schlüsselnamen.
 */
TEST(YamlLite, Fehler_DoppelterSchluessel) {
    auto e = parseFail("name: a\nname: b\n");
    EXPECT_EQ(e.line, 2);
    EXPECT_NE(e.message.find("name"), std::string::npos);
}

/**
 * @test YamlLite/Fehler_NichtUnterstuetzteKonstrukte
 * @brief Anchors, mehrzeilige Skalare und Dokumenttrenner werden klar abgelehnt.
 * @par Kriterium  Jeweils parse() == false mit erklärender Meldung.
 */
TEST(YamlLite, Fehler_NichtUnterstuetzteKonstrukte) {
    EXPECT_NE(parseFail("basis: &anker\n").message.find("Anchors"), std::string::npos);
    EXPECT_NE(parseFail("text: |\n").message.find("mehrzeilige"), std::string::npos);
    EXPECT_NE(parseFail("---\na: 1\n").message.find("Dokumenttrenner"), std::string::npos);
}

/**
 * @test YamlLite/Fehler_KaputterFlowAusdruck
 * @brief Eine nicht geschlossene Flow-Map wird mit Zeilenangabe gemeldet.
 * @par Kriterium  parse() == false, Zeile 1.
 */
TEST(YamlLite, Fehler_KaputterFlowAusdruck) {
    auto e = parseFail("track: { cyls: 0, heads: 0-1\n");
    EXPECT_EQ(e.line, 1);
}

/**
 * @test YamlLite/Fehler_ZeileOhneDoppelpunkt
 * @brief Eine Map-Zeile ohne 'schluessel: wert' ist ein Fehler.
 * @par Kriterium  parse() == false, Zeile 2.
 */
TEST(YamlLite, Fehler_ZeileOhneDoppelpunkt) {
    auto e = parseFail("a: 1\nnur_text\n");
    EXPECT_EQ(e.line, 2);
}

/**
 * @test YamlLite/LeereDatei_LiefertNullKnoten
 * @brief Eine leere oder nur aus Kommentaren bestehende Datei ist kein Fehler.
 * @par Kriterium  parse() == true, Ergebnis ist ein Null-Knoten.
 */
TEST(YamlLite, LeereDatei_LiefertNullKnoten) {
    auto n = parseOk("# nur ein Kommentar\n\n");
    EXPECT_TRUE(n.isNull());
}
