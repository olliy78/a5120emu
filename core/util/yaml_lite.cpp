/**
 * @file yaml_lite.cpp
 * @brief Implementierung des minimalen YAML-Subset-Parsers.
 *
 * Zweistufig: die Datei wird zuerst in eine Liste bedeutungstragender Zeilen
 * (Einrückung + Text + Zeilennummer) zerlegt, danach rekursiv über die Einrückung
 * in Maps/Listen zerlegt.  Flow-Ausdrücke (`[…]`, `{…}`) werden von einem eigenen
 * zeichenweisen Parser innerhalb einer Zeile verarbeitet.
 *
 * @see core/util/yaml_lite.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/util/yaml_lite.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace yaml {

// ─── Node ────────────────────────────────────────────────────────────────────

const Node* Node::find(const std::string& key) const {
    if (type != NodeType::Map) return nullptr;
    for (const auto& e : entries)
        if (e.first == key) return &e.second;
    return nullptr;
}

std::string Error::format(const std::string& path) const {
    std::ostringstream os;
    os << path;
    if (line > 0) os << ':' << line;
    os << ": " << message;
    return os.str();
}

// ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// Entfernt einen Kommentar (`#`) außerhalb von Quotes bis Zeilenende.
std::string stripComment(const std::string& s) {
    bool in_sq = false, in_dq = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in_dq) {
            if (c == '\\' && i + 1 < s.size()) { ++i; continue; }
            if (c == '"') in_dq = false;
        } else if (in_sq) {
            if (c == '\'') in_sq = false;
        } else if (c == '"') {
            in_dq = true;
        } else if (c == '\'') {
            in_sq = true;
        } else if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            return s.substr(0, i);
        }
    }
    return s;
}

/// Hebt Quotes auf und löst `\"`/`\\` in doppelt gequoteten Skalaren auf.
std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
        return s.substr(1, s.size() - 2);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) { out += s[++i]; continue; }
            out += s[i];
        }
        return out;
    }
    return s;
}

/**
 * Position des Key-Doppelpunkts: erstes ':' außerhalb von Quotes und außerhalb
 * von Flow-Klammern, dem ein Leerzeichen oder das Zeilenende folgt.
 */
size_t findKeyColon(const std::string& s) {
    bool in_sq = false, in_dq = false;
    int  depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in_dq) {
            if (c == '\\' && i + 1 < s.size()) { ++i; continue; }
            if (c == '"') in_dq = false;
            continue;
        }
        if (in_sq) {
            if (c == '\'') in_sq = false;
            continue;
        }
        switch (c) {
            case '"':  in_dq = true;  break;
            case '\'': in_sq = true;  break;
            case '[': case '{': ++depth; break;
            case ']': case '}': --depth; break;
            case ':':
                if (depth == 0 && (i + 1 == s.size() || s[i + 1] == ' '))
                    return i;
                break;
            default: break;
        }
    }
    return std::string::npos;
}

/// true, wenn die Zeile ein Listeneintrag ist (`-` allein oder `- …`).
bool isListItem(const std::string& s) {
    return !s.empty() && s[0] == '-' && (s.size() == 1 || s[1] == ' ');
}

// ── Flow-Parser (innerhalb einer Zeile) ──────────────────────────────────────

struct FlowParser {
    const std::string& s;
    size_t             i = 0;
    int                line;
    Error*             err;

    FlowParser(const std::string& str, int ln, Error* e) : s(str), line(ln), err(e) {}

    void skipWs() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }

    bool fail(const std::string& msg) {
        err->line = line;
        err->message = msg;
        return false;
    }

    /// Skalar bis zum nächsten Trennzeichen der aktuellen Flow-Ebene.
    std::string readPlainScalar() {
        const size_t start = i;
        bool in_sq = false, in_dq = false;
        while (i < s.size()) {
            const char c = s[i];
            if (in_dq) {
                if (c == '\\' && i + 1 < s.size()) { i += 2; continue; }
                if (c == '"') in_dq = false;
            } else if (in_sq) {
                if (c == '\'') in_sq = false;
            } else if (c == '"') {
                in_dq = true;
            } else if (c == '\'') {
                in_sq = true;
            } else if (c == ',' || c == ']' || c == '}' || c == ':') {
                break;
            }
            ++i;
        }
        return unquote(trim(s.substr(start, i - start)));
    }

    bool parseValue(Node& out) {
        skipWs();
        if (i >= s.size()) return fail("unerwartetes Zeilenende in Flow-Ausdruck");

        if (s[i] == '[') {
            ++i;
            out.type = NodeType::List;
            out.line = line;
            skipWs();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Node item;
                if (!parseValue(item)) return false;
                out.items.push_back(std::move(item));
                skipWs();
                if (i >= s.size()) return fail("']' fehlt");
                if (s[i] == ']') { ++i; return true; }
                if (s[i] != ',') return fail("',' oder ']' erwartet");
                ++i;
            }
        }

        if (s[i] == '{') {
            ++i;
            out.type = NodeType::Map;
            out.line = line;
            skipWs();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                skipWs();
                const std::string key = readPlainScalar();
                if (key.empty()) return fail("leerer Schlüssel in Flow-Map");
                skipWs();
                if (i >= s.size() || s[i] != ':') return fail("':' nach '" + key + "' erwartet");
                ++i;
                Node val;
                if (!parseValue(val)) return false;
                for (const auto& e : out.entries)
                    if (e.first == key) return fail("doppelter Schlüssel '" + key + "'");
                out.entries.emplace_back(key, std::move(val));
                skipWs();
                if (i >= s.size()) return fail("'}' fehlt");
                if (s[i] == '}') { ++i; return true; }
                if (s[i] != ',') return fail("',' oder '}' erwartet");
                ++i;
            }
        }

        out.type   = NodeType::Scalar;
        out.line   = line;
        out.scalar = readPlainScalar();
        return true;
    }
};

/// Ein Zeilenwert: Flow-Ausdruck oder einfacher Skalar.
bool parseInlineValue(const std::string& value, int line, Node& out, Error& err) {
    if (value.empty()) { out.type = NodeType::Null; out.line = line; return true; }

    if (value[0] == '&' || value[0] == '*')
        { err.line = line; err.message = "Anchors/Aliases werden nicht unterstützt"; return false; }
    if (value[0] == '|' || value[0] == '>')
        { err.line = line; err.message = "mehrzeilige Skalare werden nicht unterstützt"; return false; }
    if (value[0] == '!')
        { err.line = line; err.message = "Tags werden nicht unterstützt"; return false; }

    if (value[0] == '[' || value[0] == '{') {
        FlowParser fp(value, line, &err);
        if (!fp.parseValue(out)) return false;
        fp.skipWs();
        if (fp.i != value.size()) {
            err.line = line;
            err.message = "unerwarteter Text nach Flow-Ausdruck: '" + value.substr(fp.i) + "'";
            return false;
        }
        return true;
    }

    out.type   = NodeType::Scalar;
    out.line   = line;
    out.scalar = unquote(value);
    return true;
}

// ── Block-Parser (über die Einrückung) ───────────────────────────────────────

struct Line {
    int         indent = 0;
    std::string text;
    int         no     = 0;
};

struct BlockParser {
    std::vector<Line> lines;
    size_t            pos = 0;
    Error*            err;

    bool fail(int line, const std::string& msg) {
        err->line = line;
        err->message = msg;
        return false;
    }

    bool parseBlock(int indent, Node& out);
    bool parseList(int indent, Node& out);
    bool parseMap(int indent, Node& out);
};

bool BlockParser::parseBlock(int indent, Node& out) {
    if (pos >= lines.size()) { out.type = NodeType::Null; return true; }

    if (isListItem(lines[pos].text)) return parseList(indent, out);

    // Einzelner Skalar ohne Doppelpunkt (z. B. Listeneintrag "- hfe").
    if (findKeyColon(lines[pos].text) == std::string::npos) {
        const Line& l = lines[pos];
        if (!parseInlineValue(l.text, l.no, out, *err)) return false;
        ++pos;
        return true;
    }

    return parseMap(indent, out);
}

bool BlockParser::parseList(int indent, Node& out) {
    out.type = NodeType::List;
    out.line = lines[pos].no;

    while (pos < lines.size() && lines[pos].indent == indent && isListItem(lines[pos].text)) {
        Line& l = lines[pos];

        // Text hinter dem '-' als virtuelle, tiefer eingerückte Zeile behandeln —
        // so trägt ein "- key: wert" seine Folgezeilen (gleiche Spalte) mit.
        size_t c = 1;
        while (c < l.text.size() && l.text[c] == ' ') ++c;
        const std::string content       = l.text.substr(c);
        const int         content_indent = indent + static_cast<int>(c);
        const int         item_line      = l.no;

        Node item;
        if (content.empty()) {
            ++pos;
            if (pos < lines.size() && lines[pos].indent > indent) {
                if (!parseBlock(lines[pos].indent, item)) return false;
            } else {
                item.type = NodeType::Null;
                item.line = item_line;
            }
        } else {
            l.text   = content;
            l.indent = content_indent;
            if (!parseBlock(content_indent, item)) return false;
        }
        out.items.push_back(std::move(item));
    }

    if (pos < lines.size() && lines[pos].indent > indent)
        return fail(lines[pos].no, "unerwartete Einrückung");

    return true;
}

bool BlockParser::parseMap(int indent, Node& out) {
    out.type = NodeType::Map;
    out.line = lines[pos].no;

    while (pos < lines.size() && lines[pos].indent == indent) {
        const Line& l = lines[pos];
        if (isListItem(l.text))
            return fail(l.no, "Listeneintrag '-' innerhalb einer Map erwartet einen Schlüssel");

        const size_t colon = findKeyColon(l.text);
        if (colon == std::string::npos)
            return fail(l.no, "'schluessel: wert' erwartet, gefunden: '" + l.text + "'");

        const std::string key   = unquote(trim(l.text.substr(0, colon)));
        const std::string value = trim(l.text.substr(colon + 1));
        const int         line  = l.no;
        if (key.empty()) return fail(line, "leerer Schlüssel");
        for (const auto& e : out.entries)
            if (e.first == key) return fail(line, "doppelter Schlüssel '" + key + "'");

        ++pos;

        Node val;
        if (value.empty()) {
            // Verschachtelter Block (tiefer eingerückt) oder leerer Wert.
            if (pos < lines.size() && lines[pos].indent > indent) {
                if (!parseBlock(lines[pos].indent, val)) return false;
            } else {
                val.type = NodeType::Null;
                val.line = line;
            }
        } else {
            if (!parseInlineValue(value, line, val, *err)) return false;
        }
        out.entries.emplace_back(key, std::move(val));
    }

    if (pos < lines.size() && lines[pos].indent > indent)
        return fail(lines[pos].no, "unerwartete Einrückung");

    return true;
}

}  // namespace

// ─── Öffentliche Schnittstelle ───────────────────────────────────────────────

bool parse(const std::string& text, Node& out, Error& err) {
    BlockParser bp;
    bp.err = &err;

    std::istringstream is(text);
    std::string        raw;
    int                no = 0;
    while (std::getline(is, raw)) {
        ++no;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        // Einrückung bestimmen (Tabs sind nicht erlaubt).
        size_t ind = 0;
        while (ind < raw.size() && (raw[ind] == ' ' || raw[ind] == '\t')) {
            if (raw[ind] == '\t') {
                err.line = no;
                err.message = "Tabulator in der Einrückung — bitte Leerzeichen verwenden";
                return false;
            }
            ++ind;
        }

        const std::string body = trim(stripComment(raw));
        if (body.empty()) continue;                      // Leer-/Kommentarzeile
        if (body == "---" || body == "...") {
            err.line = no;
            err.message = "Dokumenttrenner ('---') werden nicht unterstützt";
            return false;
        }
        bp.lines.push_back({static_cast<int>(ind), body, no});
    }

    if (bp.lines.empty()) { out = Node{}; return true; }  // leere Datei = Null

    if (!bp.parseBlock(bp.lines[0].indent, out)) return false;

    if (bp.pos != bp.lines.size()) {
        err.line = bp.lines[bp.pos].no;
        err.message = "unerwartete Einrückung";
        return false;
    }
    return true;
}

bool parseFile(const std::string& path, Node& out, Error& err) {
    std::ifstream f(path);
    if (!f) {
        err.line = 0;
        err.message = "Datei nicht lesbar";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), out, err);
}

bool toInt(const std::string& s, long& out) {
    if (s.empty()) return false;
    const char* start = s.c_str();
    char*       end   = nullptr;
    const long  v     = std::strtol(start, &end, 0);   // 0 → dez / 0x / 0
    if (end == start || *end != '\0') return false;
    out = v;
    return true;
}

bool toBool(const std::string& s, bool& out) {
    if (s == "true"  || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "false" || s == "no"  || s == "off") { out = false; return true; }
    return false;
}

}  // namespace yaml
