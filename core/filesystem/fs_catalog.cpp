/**
 * @file fs_catalog.cpp
 * @brief Implementierung des Dateisystemkatalogs (Laden, Validieren, Auswaehlen).
 *
 * Ablauf je Datei: YAML parsen → `filesystems:`-Liste durchgehen → jede Definition
 * einzeln nach @ref FsProfile uebersetzen und validieren.  Eine fehlerhafte Definition
 * wird mit Datei/Zeile/Name in @ref FsCatalog::issues vermerkt und **uebersprungen**.
 *
 * @see core/filesystem/fs_catalog.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/fs_catalog.h"
#include "core/util/yaml_lite.h"

#include <algorithm>
#include <filesystem>

const char* fsTypeName(FsType t) {
    switch (t) {
        case FsType::Cpm:  return "cpm";
        case FsType::Udos: return "udos";
    }
    return "?";
}

namespace {

/// Skalar oder Liste von Skalaren → Stringliste (`containers:`).
bool stringList(const yaml::Node& n, std::vector<std::string>& out) {
    if (n.isScalar()) { out.push_back(n.scalar); return true; }
    if (!n.isList()) return false;
    for (const auto& item : n.items) {
        if (!item.isScalar()) return false;
        out.push_back(item.scalar);
    }
    return true;
}

void collectUnknownKeys(const yaml::Node& map, const std::vector<std::string>& known,
                        const std::string& where, std::vector<std::string>& issues) {
    for (const auto& e : map.entries)
        if (std::find(known.begin(), known.end(), e.first) == known.end())
            issues.push_back(where + ": unbekanntes Feld '" + e.first + "' — ignoriert");
}

/// Pflicht-Skalar als Ganzzahl mit Bereichspruefung.
bool intField(const yaml::Node& map, const char* key, long lo, long hi,
              long& out, std::string& why) {
    const yaml::Node* n = map.find(key);
    if (!n) return true;                       // optional — Aufrufer hat einen Default
    long v = 0;
    if (!n->isScalar() || !yaml::toInt(n->scalar, v)) {
        why = std::string("'") + key + "' muss eine Zahl sein";
        return false;
    }
    if (v < lo || v > hi) {
        why = std::string("'") + key + "': " + std::to_string(v) + " liegt ausserhalb "
            + std::to_string(lo) + ".." + std::to_string(hi);
        return false;
    }
    out = v;
    return true;
}

/**
 * Uebersetzt einen `filesystems:`-Listeneintrag nach FsProfile.
 * @return false → Definition fehlerhaft; @p why enthaelt den Grund.
 */
bool buildProfile(const yaml::Node& node, const FormatCatalog& formats, FsProfile& out,
                  std::string& why, std::vector<std::string>& issues,
                  const std::string& file) {
    if (!node.isMap()) { why = "Dateisystemdefinition ist keine Map"; return false; }

    const std::string where = file + ":" + std::to_string(node.line);
    collectUnknownKeys(node,
                       {"name", "description", "format", "type", "data_start",
                        "containers", "detect_rank",
                        "block_size", "dir_entries", "skew", "os",
                        "sides_separate", "boot_track", "directory_track",
                        "bitmap_track", "usable_tracks"},
                       where, issues);

    // ── name (Pflicht) ──
    const yaml::Node* n_name = node.find("name");
    if (!n_name || !n_name->isScalar() || n_name->scalar.empty())
        { why = "Pflichtfeld 'name' fehlt oder ist leer"; return false; }
    out.name = n_name->scalar;

    if (const yaml::Node* n = node.find("description")) {
        if (!n->isScalar()) { why = "'description' muss ein Text sein"; return false; }
        out.description = n->scalar;
    }

    // ── format (Pflicht, muss im Formatkatalog existieren) ──
    const yaml::Node* n_fmt = node.find("format");
    if (!n_fmt || !n_fmt->isScalar() || n_fmt->scalar.empty())
        { why = "Pflichtfeld 'format' fehlt oder ist leer"; return false; }
    out.format = n_fmt->scalar;

    const DiskFormat* fmt = formats.find(out.format);
    if (!fmt) {
        why = "'format': '" + out.format + "' steht nicht im Formatkatalog";
        return false;
    }

    // ── type (Pflicht) ──
    const yaml::Node* n_type = node.find("type");
    if (!n_type || !n_type->isScalar())
        { why = "Pflichtfeld 'type' fehlt (cpm | udos)"; return false; }
    if      (n_type->scalar == "cpm")  out.type = FsType::Cpm;
    else if (n_type->scalar == "udos") out.type = FsType::Udos;
    else { why = "'type': '" + n_type->scalar + "' — erlaubt sind cpm | udos"; return false; }

    // ── data_start ──
    if (const yaml::Node* n = node.find("data_start")) {
        if (!n->isMap()) { why = "'data_start' muss { cyl: c, head: h } sein"; return false; }
        long c = 0, h = 0;
        if (!intField(*n, "cyl",  0, 255, c, why)) return false;
        if (!intField(*n, "head", 0, 1,   h, why)) return false;
        out.data_cyl  = static_cast<uint8_t>(c);
        out.data_head = static_cast<uint8_t>(h);
    }
    if (!fmt->findTrack(out.data_cyl, out.data_head)) {
        why = "'data_start': Spur c" + std::to_string(out.data_cyl) + "h"
            + std::to_string(out.data_head) + " gibt es im Format '" + out.format + "' nicht";
        return false;
    }

    // ── containers ──
    bool img_ausdruecklich = false;
    if (const yaml::Node* n = node.find("containers")) {
        std::vector<std::string> list;
        if (!stringList(*n, list) || list.empty())
            { why = "'containers' muss eine Liste aus img | hfe | dmk sein"; return false; }
        out.allow_img = out.allow_hfe = out.allow_dmk = false;
        for (const auto& c : list) {
            if      (c == "img") { out.allow_img = true; img_ausdruecklich = true; }
            else if (c == "hfe") out.allow_hfe = true;
            else if (c == "dmk") out.allow_dmk = true;
            else { why = "'containers': unbekannter Typ '" + c + "'"; return false; }
        }
    }

    long v = 0;
    if (!intField(node, "detect_rank", -999, 999, v, why)) return false;
    out.detect_rank = static_cast<int>(v);

    // ── typabhaengige Felder ─────────────────────────────────────────────────
    if (out.type == FsType::Cpm) {
        v = out.block_size;
        if (!intField(node, "block_size", 1024, 16384, v, why)) return false;
        out.block_size = static_cast<uint32_t>(v);
        if (out.block_size & (out.block_size - 1)) {
            why = "'block_size' muss eine Zweierpotenz sein (1024…16384)";
            return false;
        }

        v = out.dir_entries;
        if (!intField(node, "dir_entries", 1, 4096, v, why)) return false;
        out.dir_entries = static_cast<uint16_t>(v);

        v = out.skew;
        if (!intField(node, "skew", 0, 255, v, why)) return false;
        out.skew = static_cast<uint8_t>(v);

        if (const yaml::Node* n = node.find("os")) {
            if (!n->isScalar()) { why = "'os' muss ein Text sein"; return false; }
            if (n->scalar != "cpm2.2" && n->scalar != "p2dos" && n->scalar != "cpm3") {
                why = "'os': '" + n->scalar + "' — erlaubt sind cpm2.2 | p2dos | cpm3";
                return false;
            }
            out.os = n->scalar;
        }

        // Ein UDOS-Dateisystem kann kein .img sein (Kontrollblock hinter der CRC);
        // fuer CP/M gilt umgekehrt nichts Besonderes.
        for (const char* feld : {"sides_separate", "boot_track", "directory_track",
                                 "bitmap_track", "usable_tracks"})
            if (node.has(feld))
                issues.push_back(where + ": '" + feld + "' gilt nur fuer type: udos — ignoriert");
    } else {
        // UDOS haengt je Sektor 4 Bytes HINTER die Daten-CRC — ein rohes Sektorabbild
        // verliert damit die gesamte Dateiverkettung.  Das ist keine Einstellung.
        if (img_ausdruecklich)
            issues.push_back(where + ": 'containers: img' ist fuer UDOS unmoeglich "
                                     "(Sektorkontrollblock hinter der Daten-CRC) — entfernt");
        out.allow_img = false;

        if (const yaml::Node* n = node.find("sides_separate")) {
            bool b = true;
            if (!n->isScalar() || !yaml::toBool(n->scalar, b))
                { why = "'sides_separate' muss true/false sein"; return false; }
            out.sides_separate = b;
        }

        const uint8_t ncyls = fmt->numCylinders();
        v = out.boot_track;
        if (!intField(node, "boot_track", 0, ncyls - 1, v, why)) return false;
        out.boot_track = static_cast<uint8_t>(v);
        v = out.directory_track;
        if (!intField(node, "directory_track", 0, ncyls - 1, v, why)) return false;
        out.directory_track = static_cast<uint8_t>(v);
        v = out.bitmap_track;
        if (!intField(node, "bitmap_track", 0, ncyls - 1, v, why)) return false;
        out.bitmap_track = static_cast<uint8_t>(v);
        v = 0;
        if (!intField(node, "usable_tracks", 0, ncyls, v, why)) return false;
        out.usable_tracks = static_cast<uint8_t>(v ? v : ncyls);

        for (const char* feld : {"block_size", "dir_entries", "skew", "os"})
            if (node.has(feld))
                issues.push_back(where + ": '" + feld + "' gilt nur fuer type: cpm — ignoriert");
    }

    return true;
}

}  // namespace

// ─── FsCatalog ───────────────────────────────────────────────────────────────

FsCatalog FsCatalog::loadDefault(const FormatCatalog& formats, std::string* fatal_error) {
    return load(FormatCatalog::searchPaths(), formats, fatal_error);
}

FsCatalog FsCatalog::load(const std::vector<std::string>& paths,
                          const FormatCatalog& formats,
                          std::string* fatal_error) {
    FsCatalog cat;
    if (fatal_error) fatal_error->clear();

    std::vector<std::string> seen;

    for (const auto& path : paths) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) continue;

        const std::string canon = std::filesystem::weakly_canonical(path, ec).string();
        const std::string key   = ec ? path : canon;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);

        yaml::Node  root;
        yaml::Error err;
        if (!yaml::parseFile(path, root, err)) {
            if (fatal_error)
                *fatal_error = "Katalog fehlerhaft — " + err.format(path);
            return FsCatalog{};
        }
        if (!root.isMap()) continue;

        const yaml::Node* list = root.find("filesystems");
        if (!list) continue;                    // §6.5: Sektion ist optional
        if (!list->isList()) {
            if (fatal_error)
                *fatal_error = "Katalog fehlerhaft — " + path + ":"
                             + std::to_string(list->line)
                             + ": 'filesystems' ist keine Liste";
            return FsCatalog{};
        }

        cat.sources_.push_back(path);

        for (const auto& node : list->items) {
            FsProfile   p;
            std::string why;
            const std::string where = path + ":" + std::to_string(node.line);

            if (!buildProfile(node, formats, p, why, cat.issues_, path)) {
                const std::string what = p.name.empty() ? std::string("Dateisystem")
                                                        : "Dateisystem '" + p.name + "'";
                cat.issues_.push_back(where + ": " + what + " uebersprungen — " + why);
                continue;
            }

            // Spaeterer Eintrag gleichen Namens ersetzt den frueheren (User-Override).
            auto it = std::find_if(cat.profiles_.begin(), cat.profiles_.end(),
                                   [&](const FsProfile& e) { return e.name == p.name; });
            if (it != cat.profiles_.end()) *it = p;
            else                            cat.profiles_.push_back(p);
        }
    }

    return cat;
}

const FsProfile* FsCatalog::find(const std::string& name) const {
    for (const auto& p : profiles_) if (p.name == name) return &p;
    return nullptr;
}

std::vector<const FsProfile*> FsCatalog::forFormat(const std::string& format_name) const {
    std::vector<const FsProfile*> out;
    for (const auto& p : profiles_) if (p.format == format_name) out.push_back(&p);
    std::stable_sort(out.begin(), out.end(),
                     [](const FsProfile* a, const FsProfile* b) {
                         return a->detect_rank < b->detect_rank;
                     });
    return out;
}
