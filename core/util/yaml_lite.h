/**
 * @file yaml_lite.h
 * @brief Minimaler YAML-Subset-Parser — bewusst ohne externe Abhängigkeit.
 *
 * Deckt genau den Umfang ab, den die Konfigurationsdateien des Kerns brauchen
 * (`data/formats.yaml`, siehe @ref FormatCatalog).  Ergebnis ist ein generischer
 * @ref yaml::Node -Baum (Map/List/Skalar); die fachliche Abbildung auf Structs
 * macht der jeweilige Konsument.
 *
 * ### Unterstützt
 * - Kommentare `#` bis Zeilenende (auch am Zeilenende, außerhalb von Quotes)
 * - Einrückungs-Verschachtelung — **nur Leerzeichen**, Tabs sind ein Fehler
 * - Block-Maps (`key: wert`) und Block-Listen (`- eintrag`)
 * - Flow-Maps `{ a: 1, b: 2 }` und Flow-Listen `[a, b, c]` (einzeilig, verschachtelbar)
 * - Skalare: bare, `'einfach'`, `"doppelt"` (mit `\"`/`\\`-Escapes)
 *
 * ### Nicht unterstützt (jeweils Ladefehler mit Zeilennummer)
 * - Anchors/Aliases (`&a` / `*a`), Merge-Keys (`<<:`)
 * - mehrzeilige Skalare (`|`, `>`), mehrere Dokumente (`---`), Tags (`!!str`)
 * - komplexe Keys (`? …`), Tabs als Einrückung, doppelte Keys in derselben Map
 *
 * @see doc/K1520_architecture.md §8.6.2
 * @see core/peripherals/floppy_drive/format_catalog.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <string>
#include <utility>
#include <vector>

namespace yaml {

/// @brief Knotenart im geparsten Baum.
enum class NodeType { Null, Scalar, Map, List };

/**
 * @class Node
 * @brief Ein Knoten des YAML-Baums (Map/List/Skalar/Null).
 *
 * Maps behalten die Reihenfolge der Datei (wichtig für reproduzierbare
 * Fehlermeldungen und stabile Katalogreihenfolge).
 */
class Node {
public:
    NodeType    type = NodeType::Null;
    std::string scalar;                                   ///< nur bei Scalar
    std::vector<std::pair<std::string, Node>> entries;    ///< nur bei Map (geordnet)
    std::vector<Node> items;                              ///< nur bei List
    int         line = 0;                                 ///< 1-basierte Quellzeile

    bool isNull()   const { return type == NodeType::Null; }
    bool isScalar() const { return type == NodeType::Scalar; }
    bool isMap()    const { return type == NodeType::Map; }
    bool isList()   const { return type == NodeType::List; }

    /// @brief Map-Eintrag suchen; nullptr, wenn nicht vorhanden oder kein Map-Knoten.
    const Node* find(const std::string& key) const;
    /// @brief true, wenn der Map-Eintrag existiert.
    bool has(const std::string& key) const { return find(key) != nullptr; }
};

/// @brief Parse-Fehler mit Zeilenbezug.
struct Error {
    int         line = 0;
    std::string message;
    /// @brief Als `pfad:zeile: meldung` formatieren (für Logausgaben).
    std::string format(const std::string& path) const;
};

/**
 * @brief Parst YAML-Text in einen Knotenbaum.
 * @param text Vollständiger Dateiinhalt
 * @param out  Ergebnisbaum (bei Fehler undefiniert)
 * @param err  Fehlerbeschreibung, wenn false zurückgegeben wird
 * @return true bei Erfolg
 */
bool parse(const std::string& text, Node& out, Error& err);

/**
 * @brief Wie @ref parse, liest den Text aus einer Datei.
 * @return false auch, wenn die Datei nicht lesbar ist (err.line == 0).
 */
bool parseFile(const std::string& path, Node& out, Error& err);

/// @brief Skalar → Ganzzahl (dezimal oder `0x…`); false, wenn nicht konvertierbar.
bool toInt(const std::string& s, long& out);
/// @brief Skalar → bool (`true`/`false`/`yes`/`no`/`on`/`off`); false, wenn nicht konvertierbar.
bool toBool(const std::string& s, bool& out);

}  // namespace yaml
