/**
 * @file format_catalog.cpp
 * @brief Implementierung des YAML-Formatkatalogs (Laden, Validieren, Auswählen).
 *
 * Ablauf je Datei: YAML parsen (@ref yaml::parseFile) → `formats:`-Liste durchgehen →
 * jede Definition einzeln nach @ref DiskFormat übersetzen und validieren.  Eine
 * fehlerhafte Definition wird mit Datei/Zeile/Name in @ref FormatCatalog::issues
 * vermerkt und **übersprungen**; korrekte Definitionen der gleichen Datei bleiben gültig.
 *
 * @see core/peripherals/floppy_drive/format_catalog.h
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/util/yaml_lite.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

// Vom Build gesetzt (CMake): Katalog im Quell- bzw. Installbaum.
#ifndef K1520_FORMATS_DEFAULT
#define K1520_FORMATS_DEFAULT ""
#endif

namespace {

constexpr const char* kCatalogFileName = "formats.yaml";

// ─── Laufwerks-Kompatibilität ────────────────────────────────────────────────

std::string encName(Encoding e) { return e == Encoding::FM ? "FM" : "MFM"; }

// ─── Kleine Helfer ───────────────────────────────────────────────────────────

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

/// „5" → [5,5]; „2-79" → [2,79].  false bei Syntax-/Wertefehler.
bool parseRange(const std::string& in, int& lo, int& hi) {
    const std::string s = trim(in);
    if (s.empty()) return false;
    const size_t dash = s.find('-', 1);          // ab 1: kein Vorzeichen erlaubt
    long a = 0, b = 0;
    if (dash == std::string::npos) {
        if (!yaml::toInt(s, a)) return false;
        b = a;
    } else {
        if (!yaml::toInt(trim(s.substr(0, dash)), a)) return false;
        if (!yaml::toInt(trim(s.substr(dash + 1)), b)) return false;
    }
    if (a < 0 || b < 0 || a > 255 || b > 255 || a > b) return false;
    lo = static_cast<int>(a);
    hi = static_cast<int>(b);
    return true;
}

bool parseEncoding(const std::string& s, Encoding& out) {
    std::string v;
    for (char c : s) v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "fm")  { out = Encoding::FM;  return true; }
    if (v == "mfm") { out = Encoding::MFM; return true; }
    return false;
}

/// Skalar oder Liste von Skalaren → Stringliste (für `drives:` / `containers:`).
bool stringList(const yaml::Node& n, std::vector<std::string>& out) {
    if (n.isScalar()) { out.push_back(n.scalar); return true; }
    if (!n.isList()) return false;
    for (const auto& item : n.items) {
        if (!item.isScalar()) return false;
        out.push_back(item.scalar);
    }
    return true;
}

/// Wirft ein `nicht unterstütztes Feld` als Hinweis aus (V1b, nicht fatal).
void collectUnknownKeys(const yaml::Node& map,
                        const std::vector<std::string>& known,
                        const std::string& where,
                        std::vector<std::string>& issues) {
    for (const auto& e : map.entries)
        if (std::find(known.begin(), known.end(), e.first) == known.end())
            issues.push_back(where + ": unbekanntes Feld '" + e.first + "' — ignoriert");
}

// ─── Eine Formatdefinition übersetzen ────────────────────────────────────────

/**
 * Übersetzt einen `formats:`-Listeneintrag nach DiskFormat.
 * @return false → Definition ist fehlerhaft; @p why enthält den Grund.
 */
bool buildFormat(const yaml::Node& node, DiskFormat& out, std::string& why,
                 std::vector<std::string>& issues, const std::string& file) {
    if (!node.isMap()) { why = "Formatdefinition ist keine Map"; return false; }

    const std::string where = file + ":" + std::to_string(node.line);
    collectUnknownKeys(node,
                       {"name", "description", "drives", "default_for",
                        "encoding", "containers", "tracks"},
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

    // ── drives (Pflicht) ──
    const yaml::Node* n_drives = node.find("drives");
    if (!n_drives) { why = "Pflichtfeld 'drives' fehlt"; return false; }
    if (!stringList(*n_drives, out.drives) || out.drives.empty())
        { why = "'drives' muss eine nicht-leere Liste von Laufwerksprofilen sein"; return false; }

    if (const yaml::Node* n = node.find("default_for")) {
        if (!stringList(*n, out.default_for))
            { why = "'default_for' muss eine Liste von Laufwerksprofilen sein"; return false; }
    }

    // ── Verfahrens-Vorgabe des Formats (optional) ──
    bool     have_fmt_enc = false;
    Encoding fmt_enc      = Encoding::MFM;
    if (const yaml::Node* n = node.find("encoding")) {
        if (!n->isScalar() || !parseEncoding(n->scalar, fmt_enc))
            { why = "'encoding' muss 'fm' oder 'mfm' sein"; return false; }
        have_fmt_enc = true;
    }

    // ── containers (optional) ──
    if (const yaml::Node* n = node.find("containers")) {
        std::vector<std::string> c;
        if (!stringList(*n, c) || c.empty())
            { why = "'containers' muss eine nicht-leere Liste sein ('img', 'hfe')"; return false; }
        out.allow_img = out.allow_hfe = false;
        for (const auto& v : c) {
            if      (v == "img") out.allow_img = true;
            else if (v == "hfe") out.allow_hfe = true;
            else { why = "'containers': unbekannter Typ '" + v + "' (erlaubt: img, hfe)"; return false; }
        }
    }

    // ── tracks (Pflicht) ──
    const yaml::Node* n_tracks = node.find("tracks");
    if (!n_tracks || !n_tracks->isList() || n_tracks->items.empty())
        { why = "Pflichtfeld 'tracks' fehlt oder ist leer"; return false; }

    for (const auto& tn : n_tracks->items) {
        if (!tn.isMap()) { why = "Spurbereich ist keine Map"; return false; }
        collectUnknownKeys(tn,
                           {"cyls", "heads", "sectors", "size", "encoding", "first_sector"},
                           file + ":" + std::to_string(tn.line), issues);

        TrackFormat tf;

        const yaml::Node* n_cyls  = tn.find("cyls");
        const yaml::Node* n_heads = tn.find("heads");
        const yaml::Node* n_secs  = tn.find("sectors");
        const yaml::Node* n_size  = tn.find("size");
        if (!n_cyls || !n_heads || !n_secs || !n_size)
            { why = "Spurbereich braucht 'cyls', 'heads', 'sectors' und 'size'"; return false; }

        int lo = 0, hi = 0;
        if (!n_cyls->isScalar() || !parseRange(n_cyls->scalar, lo, hi))
            { why = "'cyls': ungültiger Bereich '" + n_cyls->scalar + "'"; return false; }
        tf.cyl_first = static_cast<uint8_t>(lo);
        tf.cyl_last  = static_cast<uint8_t>(hi);

        if (!n_heads->isScalar() || !parseRange(n_heads->scalar, lo, hi) || hi > 1)
            { why = "'heads': ungültiger Bereich '" + n_heads->scalar + "' (erlaubt 0, 1, 0-1)"; return false; }
        tf.head_first = static_cast<uint8_t>(lo);
        tf.head_last  = static_cast<uint8_t>(hi);

        long v = 0;
        if (!n_secs->isScalar() || !yaml::toInt(n_secs->scalar, v) || v < 1 || v > 255)
            { why = "'sectors': ungültige Sektorzahl '" + n_secs->scalar + "'"; return false; }
        tf.secs_per_track = static_cast<uint8_t>(v);

        if (!n_size->isScalar() || !yaml::toInt(n_size->scalar, v))
            { why = "'size': ungültige Sektorgröße '" + n_size->scalar + "'"; return false; }
        if (v != 128 && v != 256 && v != 512 && v != 1024)
            { why = "'size': " + std::to_string(v) + " — erlaubt sind 128, 256, 512, 1024"; return false; }
        tf.bytes_per_sec = static_cast<uint16_t>(v);

        // Verfahren: Spur → Format → (später) Laufwerks-Default.
        if (const yaml::Node* n = tn.find("encoding")) {
            if (!n->isScalar() || !parseEncoding(n->scalar, tf.encoding))
                { why = "Spur-'encoding' muss 'fm' oder 'mfm' sein"; return false; }
        } else if (have_fmt_enc) {
            tf.encoding = fmt_enc;
        } else {
            tf.encoding = Encoding::MFM;   // Katalog-Default, s. §8.6.1
        }

        if (const yaml::Node* n = tn.find("first_sector")) {
            if (!n->isScalar() || !yaml::toInt(n->scalar, v) || v < 0 || v > 255)
                { why = "'first_sector': ungültig"; return false; }
            tf.first_sector_id = static_cast<uint8_t>(v);
        }

        // V2 — Überlappungsfreiheit
        for (const auto& prev : out.tracks) {
            if (prev.overlaps(tf)) {
                std::ostringstream os;
                os << "Spurbereiche überlappen (Zyl " << int(tf.cyl_first) << '-' << int(tf.cyl_last)
                   << ", Kopf " << int(tf.head_first) << '-' << int(tf.head_last) << ')';
                why = os.str();
                return false;
            }
        }
        out.tracks.push_back(tf);
    }

    return true;
}

// ─── Validierung gegen die Laufwerksprofile (V3/V4) ──────────────────────────

bool validateDrives(const DiskFormat& fmt, std::string& why) {
    const auto known = knownDriveProfileNames();

    auto check_known = [&](const std::vector<std::string>& list, const char* field) {
        for (const auto& d : list) {
            if (std::find(known.begin(), known.end(), d) == known.end()) {
                why = std::string(field) + ": unbekanntes Laufwerksprofil '" + d + "'";
                return false;
            }
        }
        return true;
    };

    // V3 — Namen müssen existieren.  WICHTIG: builtinDriveProfile() liefert für
    // unbekannte Namen stillschweigend das Default-Profil, deshalb wird hier gegen
    // die explizite Namensliste geprüft und nicht gegen den Rückgabewert.
    if (!check_known(fmt.drives, "drives"))          return false;
    if (!check_known(fmt.default_for, "default_for")) return false;

    // default_for ⊆ drives
    for (const auto& d : fmt.default_for) {
        if (!fmt.supportsDrive(d)) {
            why = "default_for: '" + d + "' ist nicht in 'drives' gelistet";
            return false;
        }
    }

    // V4 — Geometrie/Verfahren müssen zu JEDEM gelisteten Laufwerk passen.
    for (const auto& d : fmt.drives) {
        std::string reason;
        if (!formatFitsDrive(fmt, builtinDriveProfile(d), &reason)) {
            why = "passt nicht zu Laufwerk '" + d + "': " + reason;
            return false;
        }
    }
    return true;
}

// ─── Pfadsuche ───────────────────────────────────────────────────────────────

/// Anker im eigenen Modul — nur seine ADRESSE wird gebraucht (siehe @ref moduleDir).
const char kModuleAnchor = 0;

/**
 * @brief Verzeichnis des Moduls, in dem DIESER Code liegt.
 *
 * Bewusst **nicht** das Verzeichnis der Programmdatei: steckt der Kern in
 * `libk1520core.so` und lädt die GUI ihn per `ctypes`, dann ist die Programmdatei
 * der Python-Interpreter im venv — die mitgelieferte `formats.yaml` läge daneben
 * nirgends.  Über die Moduladresse gefragt, antwortet jeder Fall richtig: bei den
 * statisch gelinkten Werkzeugen ist das Modul die Programmdatei selbst, bei der
 * gemeinsam genutzten Bibliothek diese Bibliothek.  Damit ist eine Installation
 * frei verschiebbar (`doc/design/13_distribution.md` §6.2).
 */
std::string moduleDir() {
#if defined(_WIN32)
    HMODULE mod = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&kModuleAnchor), &mod))
        return {};
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(mod, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(buf).parent_path().string();
#else
    ::Dl_info info{};
    if (::dladdr(&kModuleAnchor, &info) && info.dli_fname && *info.dli_fname)
        return std::filesystem::path(info.dli_fname).parent_path().string();
    // Kein dynamischer Linker (vollstatisches Binary) → Programmdatei.
    char    buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}

/// Basisverzeichnis der Benutzerkonfiguration (ohne den Programmnamen).
std::string homeConfigDir() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return x;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"); a && *a) return a;
    if (const char* p = std::getenv("USERPROFILE"); p && *p)
        return std::string(p) + "/.config";
    return {};
#else
    if (const char* h = std::getenv("HOME"); h && *h)
        return std::string(h) + "/.config";
    return {};
#endif
}

/// Trennzeichen der Pfadliste in `K1520_FORMATS` (unter Windows ist ':' Teil von `C:\…`).
constexpr char kPathListSeparator =
#if defined(_WIN32)
    ';';
#else
    ':';
#endif

}  // namespace

// ─── formatFitsDrive ─────────────────────────────────────────────────────────

bool formatFitsDrive(const DiskFormat& fmt, const DriveProfile& prof, std::string* why) {
    auto say = [&](const std::string& m) { if (why) *why = m; return false; };

    if (!prof.present)
        return say("Slot ist unbestückt");

    if (fmt.numHeads() > prof.num_heads)
        return say("Format braucht " + std::to_string(fmt.numHeads()) + " Köpfe, Laufwerk hat "
                   + std::to_string(prof.num_heads));

    if (fmt.numCylinders() > prof.num_cyls)
        return say("Format braucht " + std::to_string(fmt.numCylinders()) + " Spuren, Laufwerk hat "
                   + std::to_string(prof.num_cyls));

    for (const auto& t : fmt.tracks) {
        if (!prof.supports(t.encoding))
            return say("Laufwerk beherrscht kein " + encName(t.encoding));
    }
    return true;
}

// ─── FormatCatalog ───────────────────────────────────────────────────────────

std::vector<std::string> FormatCatalog::searchPaths() {
    std::vector<std::string> p;

    // 1) Compile-Define (Quell-/Installbaum) — sorgt dafür, dass Tests und Tools
    //    den Katalog ohne jede Umgebungsvariable finden.
    if (std::string d = K1520_FORMATS_DEFAULT; !d.empty()) p.push_back(d);

    // 2) neben dem eigenen Modul installiert (Bibliothek bzw. Programmdatei)
    if (std::string e = moduleDir(); !e.empty()) {
        p.push_back(e + "/../share/a5120emu/" + kCatalogFileName);
        p.push_back(e + "/data/" + kCatalogFileName);
    }

    // 3) aktuelles Verzeichnis
    p.push_back(std::string("data/") + kCatalogFileName);

    // 4) Benutzerkonfiguration
    if (std::string c = homeConfigDir(); !c.empty())
        p.push_back(c + "/a5120emu/" + kCatalogFileName);

    // 5) explizite Vorgabe (höchste Priorität), Dateien/Verzeichnisse getrennt
    //    durch ':' (unixoid) bzw. ';' (Windows)
    if (const char* env = std::getenv("K1520_FORMATS"); env && *env) {
        std::stringstream ss(env);
        std::string       item;
        while (std::getline(ss, item, kPathListSeparator)) {
            if (item.empty()) continue;
            std::error_code ec;
            if (std::filesystem::is_directory(item, ec))
                p.push_back(item + "/" + kCatalogFileName);
            else
                p.push_back(item);
        }
    }
    return p;
}

FormatCatalog FormatCatalog::loadDefault(std::string* fatal_error) {
    return load(searchPaths(), fatal_error);
}

FormatCatalog FormatCatalog::load(const std::vector<std::string>& paths,
                                  std::string* fatal_error) {
    FormatCatalog cat;
    if (fatal_error) fatal_error->clear();

    // Derselbe Katalog kann über mehrere Kandidaten erreichbar sein (z. B. absolut per
    // Compile-Define UND relativ zum Arbeitsverzeichnis).  Ohne Deduplizierung würde er
    // doppelt geladen und jede Beanstandung doppelt gemeldet.
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
            // Syntaxfehler in einer vorhandenen Datei ist FATAL: sonst liefe der
            // Emulator stillschweigend mit einem halben Katalog weiter.
            if (fatal_error)
                *fatal_error = "Formatkatalog fehlerhaft — " + err.format(path);
            return FormatCatalog{};
        }

        cat.sources_.push_back(path);

        if (!root.isMap()) {
            if (fatal_error)
                *fatal_error = "Formatkatalog fehlerhaft — " + path + ": erwartet eine Map "
                               "mit dem Schlüssel 'formats'";
            return FormatCatalog{};
        }

        if (const yaml::Node* v = root.find("version")) {
            long ver = 0;
            if (!v->isScalar() || !yaml::toInt(v->scalar, ver) || ver != 1) {
                if (fatal_error)
                    *fatal_error = "Formatkatalog fehlerhaft — " + path + ":"
                                   + std::to_string(v->line)
                                   + ": nicht unterstützte Schema-Version '" + v->scalar
                                   + "' (unterstützt: 1)";
                return FormatCatalog{};
            }
        }

        const yaml::Node* list = root.find("formats");
        if (!list || !list->isList()) {
            if (fatal_error)
                *fatal_error = "Formatkatalog fehlerhaft — " + path
                               + ": Schlüssel 'formats' fehlt oder ist keine Liste";
            return FormatCatalog{};
        }

        for (const auto& node : list->items) {
            DiskFormat  fmt;
            std::string why;
            const std::string where = path + ":" + std::to_string(node.line);

            if (!buildFormat(node, fmt, why, cat.issues_, path)) {
                // buildFormat setzt den Namen als Erstes — er steht daher auch bei
                // späteren Fehlern zur Verfügung und gehört in die Meldung.
                const std::string what = fmt.name.empty()
                                             ? std::string("Format")
                                             : ("Format '" + fmt.name + "'");
                cat.issues_.push_back(where + ": " + what + " übersprungen — " + why);
                continue;
            }
            if (!validateDrives(fmt, why)) {
                cat.issues_.push_back(where + ": Format '" + fmt.name
                                      + "' übersprungen — " + why);
                continue;
            }

            // Gleicher Name aus einer Datei niedrigerer Priorität → ersetzen.
            auto it = std::find_if(cat.formats_.begin(), cat.formats_.end(),
                                   [&](const DiskFormat& f) { return f.name == fmt.name; });
            if (it != cat.formats_.end()) *it = std::move(fmt);
            else                          cat.formats_.push_back(std::move(fmt));
        }
    }

    if (cat.sources_.empty()) {
        if (fatal_error) {
            std::ostringstream os;
            os << "Keine Formatkatalog-Datei (" << kCatalogFileName << ") gefunden.\n"
               << "Gesucht wurde in:";
            for (const auto& p : paths) os << "\n  - " << p;
            os << "\nAbhilfe: data/formats.yaml bereitstellen oder K1520_FORMATS=<datei> setzen.";
            *fatal_error = os.str();
        }
        return FormatCatalog{};
    }

    if (cat.formats_.empty()) {
        if (fatal_error) {
            std::ostringstream os;
            os << "Formatkatalog enthält kein einziges gültiges Diskettenformat ("
               << cat.sources_.front() << ").";
            for (const auto& i : cat.issues_) os << "\n  - " << i;
            *fatal_error = os.str();
        }
        return FormatCatalog{};
    }

    return cat;
}

const DiskFormat* FormatCatalog::find(const std::string& name) const {
    for (const auto& f : formats_)
        if (f.name == name) return &f;
    return nullptr;
}

std::vector<const DiskFormat*> FormatCatalog::forDrive(const DriveProfile& prof) const {
    std::vector<const DiskFormat*> out;
    if (!prof.present) return out;

    // Standardformat des Laufwerkstyps zuerst (bevorzugte Auswahl in der GUI).
    const DiskFormat* def = defaultFor(prof);
    if (def) out.push_back(def);

    for (const auto& f : formats_) {
        if (&f == def) continue;
        if (f.supportsDrive(prof.name)) out.push_back(&f);
    }
    return out;
}

const DiskFormat* FormatCatalog::defaultFor(const DriveProfile& prof) const {
    if (!prof.present) return nullptr;
    for (const auto& f : formats_)
        if (std::find(f.default_for.begin(), f.default_for.end(), prof.name)
            != f.default_for.end())
            return &f;
    return nullptr;
}
