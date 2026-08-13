/**
 * @file k1520disktool.cpp
 * @brief `k1520disktool` — Dateien zwischen Linux und K1520-Disketten austauschen.
 *
 * Kommandozeilenseite des k1520DiskTool; dieselbe Bibliothek, die auch die
 * PySide6-Oberflaeche benutzt (@ref DiskVolume).  Ersetzt die `cpmls`/`cpmcp`-Aufrufe
 * der alten Werkzeuge und kann zusaetzlich UDOS.
 *
 * ```
 * k1520disktool ls     <abbild> [--fs NAME] [-l]      # ohne -l nur die Namen
 * k1520disktool get    <abbild> <muster…> --to <verzeichnis> [--text|--binary]
 * k1520disktool put    <abbild> <datei|ordner…> [--as NAME] [--text] [--force]
 * k1520disktool rm     <abbild> <muster…>
 * k1520disktool create <abbild> --fs NAME [--label NAME] [--boot abbild.bin]
 * k1520disktool boot-get <abbild> <datei.bin>        # Systemspuren herausschreiben
 * k1520disktool boot-put <abbild> <datei.bin>        # Bootabbild einspielen
 * k1520disktool info   <abbild> [--fs NAME]
 * k1520disktool check  <abbild>
 * k1520disktool formats
 * ```
 *
 * Exit-Codes: 0 ok · 1 Fehler · 2 Format/Dateisystem nicht erkannt · 3 passt nicht
 * (kein Platz) · 4 Ordnerstruktur falsch (fehlendes `SideN/`).
 *
 * @see doc/design/13_k1520disktool.md §11.1
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/cpm/cpa_dpb.h"
#include "core/filesystem/disk_volume.h"
#include "core/filesystem/geometry_probe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <ostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ─── Exit-Codes (Teil der Schnittstelle, s. Dateikopf) ───────────────────────
constexpr int kOk = 0, kFehler = 1, kNichtErkannt = 2, kPasstNicht = 3, kStruktur = 4;

struct Optionen {
    std::string fs;          ///< --fs
    std::string ziel;        ///< --to
    std::string als;         ///< --as
    std::string label;       ///< --label
    std::string boot;        ///< --boot (Bootabbild fuer die Systemspuren)
    std::string udos_typ;    ///< --type  (UDOS-Dateityp A/P/P1/B)
    std::string udos_eig;    ///< --props (UDOS-Eigenschaften, z. B. WS)
    int         udos_entry = 0;  ///< --entry (UDOS-Startadresse bei P/P1)
    int         udos_satz  = 0;  ///< --record-len (UDOS-Satzlaenge, Vielfaches von 128)
    int         udos_segment = 0; ///< --segment ANFANG:LAENGE — Anfang (hex)
    int         udos_ilen  = 0;   ///< --segment ANFANG:LAENGE — Laenge
    std::string udos_mem;         ///< --mem LOW:HIGH[:STACK] (hex, wie `EXTRACT`)
    int         udos_block = 0;   ///< --block-len (Kopfsektor Offset 17)
    bool        udos_block_gesetzt = false;
    std::string udos_zusatz;      ///< --extra (Kopfsektor 44-47, hex)
    std::string udos_erst;        ///< --created (Erstellungsvermerk, 6 Zeichen)
    std::string udos_geaend;      ///< --date (Aenderungsdatum JJMMTT)
    // CP/M kennt nur drei Attributbits und den Nutzerbereich; jedes hat sein eigenes
    // „gesetzt"-Kennzeichen, damit `--no-ro` von „gar nicht genannt" unterscheidbar ist.
    bool cpm_ro_gesetzt  = false, cpm_ro  = false;   ///< --ro / --no-ro
    bool cpm_sys_gesetzt = false, cpm_sys = false;   ///< --sys / --no-sys
    bool cpm_arc_gesetzt = false, cpm_arc = false;   ///< --arc / --no-arc
    bool cpm_user_gesetzt = false; int cpm_user = 0; ///< --user N
    int         volume  = 0; ///< --volume
    bool        text    = false;
    bool        binaer  = false;
    bool        force   = false;
    bool        lang    = false;   ///< -l
    bool        json    = false;
    bool        dry_run = false;
    bool        nobackup= false;
    std::vector<std::string> rest;  ///< uebrige Argumente (Abbild, Muster, Dateien)
};

void gebrauch() {
    std::cout <<
        "k1520disktool — Dateien zwischen Linux und K1520-Disketten austauschen\n\n"
        "  ls     <abbild> [-l]                     Verzeichnis (nur Namen; -l ausfuehrlich)\n"
        "  get    <abbild> <muster…> --to <ordner>  Dateien herausholen\n"
        "  put    <abbild> <datei|ordner…>          Dateien einfuegen\n"
        "         [--type P1 --props WS --entry 0]  … UDOS-Kopfsektor (Typ/Eigensch./Start)\n"
        "         [--record-len 1024]               … UDOS-Satzlaenge (Vielfaches von 128)\n"
        "         [--segment 2600:5521]             … UDOS-Segment ANFANG(hex):LAENGE\n"
        "         [--mem E000:E3FF:0080]            … LOW:HIGH:STACK (hex)\n"
        "         [--block-len 0] [--extra 0]       … Kopfsektor 17 bzw. 44-47\n"
        "  rm     <abbild> <muster…>                Dateien loeschen\n"
        "  create <abbild> --fs NAME [--label N]    leere Diskette anlegen\n"
        "         [--boot abbild.bin]               … mit Bootabbild in den Systemspuren\n"
        "  attr   <abbild> <datei> [--type …]       Dateiangaben zeigen/aendern\n"
        "         [--ro|--no-ro --sys|--no-sys]     … CP/M-Attribute\n"
        "         [--arc|--no-arc] [--user 3]       … und Nutzerbereich\n"
        "  boot-get <abbild> <datei.bin>            Systemspuren herausschreiben\n"
        "  boot-put <abbild> <datei.bin>            Bootabbild in die Systemspuren\n"
        "  save-as <abbild> <ziel>                  Kopie, ggf. anderes Format\n"
        "  info   <abbild>                          Belegung und Erkennung\n"
        "  check  <abbild>                          Pruefbericht\n"
        "  measure <abbild>                         Geometrie messen (auch ohne Dateisystem)\n"
        "  formats                                  bekannte Dateisysteme auflisten\n\n"
        "Gemeinsam: --fs NAME (Erkennung uebersteuern), --volume N (Seite),\n"
        "           --text|--binary, --force, --dry-run, --no-backup\n"
        "  --json   maschinenlesbar — bei ls, info, check und formats\n\n"
        "Bei beidseitigen UDOS-Disketten sind die Seiten `Side0`/`Side1`:\n"
        "  get  legt sie als Unterverzeichnisse an,\n"
        "  put  verlangt einen Ordner, der genau diese Unterverzeichnisse hat,\n"
        "  Namen duerfen das Praefix tragen:  Side1/HELP.DAT.00\n\n"
        "Exit: 0 ok · 1 Fehler · 2 nicht erkannt · 3 passt nicht · 4 Ordnerstruktur\n";
}

/// @brief Argumente zerlegen; unbekannte Schalter sind ein Fehler.
bool zerlege(int argc, char** argv, int ab, Optionen& o, std::string& err) {
    for (int i = ab; i < argc; ++i) {
        const std::string a = argv[i];
        auto wert = [&](const char* name) -> bool {
            if (i + 1 >= argc) { err = std::string(name) + " braucht einen Wert"; return false; }
            return true;
        };
        if      (a == "--fs")      { if (!wert("--fs"))     return false; o.fs     = argv[++i]; }
        else if (a == "--to")      { if (!wert("--to"))     return false; o.ziel   = argv[++i]; }
        else if (a == "--as")      { if (!wert("--as"))     return false; o.als    = argv[++i]; }
        else if (a == "--label")   { if (!wert("--label"))  return false; o.label  = argv[++i]; }
        else if (a == "--boot")    { if (!wert("--boot"))   return false; o.boot   = argv[++i]; }
        else if (a == "--type")    { if (!wert("--type"))   return false; o.udos_typ = argv[++i]; }
        else if (a == "--props")   { if (!wert("--props"))  return false; o.udos_eig = argv[++i]; }
        else if (a == "--entry")   { if (!wert("--entry"))  return false;
                                     o.udos_entry = static_cast<int>(
                                         std::strtol(argv[++i], nullptr, 0)); }
        else if (a == "--record-len") { if (!wert("--record-len")) return false;
                                     o.udos_satz = std::atoi(argv[++i]); }
        else if (a == "--load")    { if (!wert("--load"))   return false;
                                     o.udos_segment = static_cast<int>(
                                         std::strtol(argv[++i], nullptr, 0)); }
        else if (a == "--image-len") { if (!wert("--image-len")) return false;
                                     o.udos_ilen = static_cast<int>(
                                         std::strtol(argv[++i], nullptr, 0)); }
        else if (a == "--mem")     { if (!wert("--mem"))    return false; o.udos_mem = argv[++i]; }
        else if (a == "--volume")  { if (!wert("--volume")) return false;
                                     o.volume = std::atoi(argv[++i]); }
        else if (a == "--ro")      { o.cpm_ro_gesetzt  = true; o.cpm_ro  = true; }
        else if (a == "--no-ro")   { o.cpm_ro_gesetzt  = true; o.cpm_ro  = false; }
        else if (a == "--sys")     { o.cpm_sys_gesetzt = true; o.cpm_sys = true; }
        else if (a == "--no-sys")  { o.cpm_sys_gesetzt = true; o.cpm_sys = false; }
        else if (a == "--arc")     { o.cpm_arc_gesetzt = true; o.cpm_arc = true; }
        else if (a == "--no-arc")  { o.cpm_arc_gesetzt = true; o.cpm_arc = false; }
        else if (a == "--user")    { if (!wert("--user"))   return false;
                                     o.cpm_user_gesetzt = true;
                                     o.cpm_user = std::atoi(argv[++i]); }
        else if (a == "--text")    o.text     = true;
        else if (a == "--binary")  o.binaer   = true;
        else if (a == "--force")   o.force    = true;
        else if (a == "-l")        o.lang     = true;
        else if (a == "--json")    o.json     = true;
        else if (a == "--dry-run") o.dry_run  = true;
        else if (a == "--no-backup") o.nobackup = true;
        else if (!a.empty() && a[0] == '-') { err = "unbekannter Schalter: " + a; return false; }
        else o.rest.push_back(a);
    }
    return true;
}

// ─── Kataloge ────────────────────────────────────────────────────────────────

const FormatCatalog& formate() {
    static FormatCatalog c = [] {
        std::string fatal;
        FormatCatalog k = FormatCatalog::loadDefault(&fatal);
        if (!fatal.empty()) std::cerr << "Fehler: " << fatal << "\n";
        return k;
    }();
    return c;
}

const FsCatalog& dateisysteme() {
    static FsCatalog c = [] {
        std::string fatal;
        FsCatalog k = FsCatalog::loadDefault(formate(), &fatal);
        if (!fatal.empty()) std::cerr << "Fehler: " << fatal << "\n";
        for (const auto& i : k.issues()) std::cerr << "Hinweis: " << i << "\n";
        return k;
    }();
    return c;
}

/// @brief Shell-artiges Muster (`*`, `?`) auf einen Namen anwenden.
bool passt(const std::string& muster, const std::string& name) {
    size_t m = 0, n = 0, stern = std::string::npos, merk = 0;
    auto gleich = [](char a, char b) {
        return std::toupper(static_cast<unsigned char>(a))
            == std::toupper(static_cast<unsigned char>(b));
    };
    while (n < name.size()) {
        if (m < muster.size() && (muster[m] == '?' || gleich(muster[m], name[n]))) { ++m; ++n; }
        else if (m < muster.size() && muster[m] == '*') { stern = m++; merk = n; }
        else if (stern != std::string::npos) { m = stern + 1; n = ++merk; }
        else return false;
    }
    while (m < muster.size() && muster[m] == '*') ++m;
    return m == muster.size();
}

/**
 * @brief Abbild oeffnen.
 *
 * @p schreibend entscheidet ueber den Schreibschutz: `ls`/`get`/`info`/`check`
 * oeffnen schreibgeschuetzt, sodass beim blossen Lesen nichts kaputtgehen kann —
 * auch nicht durch einen Abbruch.  Nur `put`/`rm` heben ihn auf; dort IST der
 * Aufruf schon der bewusste Schritt.
 */
std::unique_ptr<DiskVolume> oeffne(const Optionen& o, const std::string& pfad, int& rc,
                                   bool schreibend = false) {
    std::string err;
    auto v = DiskVolume::open(pfad, o.fs, formate(), dateisysteme(), err,
                              /*read_only=*/!schreibend);
    if (!v) {
        std::cerr << "Fehler: " << err << "\n";
        rc = kNichtErkannt;
        return nullptr;
    }
    if (o.nobackup) v->setBackup(false);
    return v;
}

/// @brief Byte-Zahl in Klartext ("15 KB").
std::string menschlich(uint64_t bytes);

/**
 * @brief Zustand der Systemspuren in einem Satz — fuer `info`.
 *
 * „beschrieben" heisst: dort steht etwas anderes als das Fuellmuster einer
 * Leerdiskette (0xE5) bzw. 0x00.  Ob es auch BOOTET, sagt nur der Emulator; hier geht
 * es um die Frage „ist da ueberhaupt ein Bootabbild drauf?".
 */
std::string bootZustand(const DiskVolume& v, int volume) {
    const uint64_t platz = v.bootAreaSize(volume);
    if (platz == 0) return "keine (Dateisystem beginnt auf Zylinder 0)";

    std::vector<uint8_t> boot;
    if (!v.readBootImage(boot, volume))
        return menschlich(platz) + ", nicht lesbar";
    // Leer heisst: Fuellbyte der Leerdiskette (0xE5) bzw. — bei UDOS, wo je Sektor
    // der Kontrollblock mitkommt — dessen unbeschriebene Gap-Fuellung (0x4E).
    const bool leer = std::all_of(boot.begin(), boot.end(),
                                  [](uint8_t b) { return b == 0xE5 || b == 0x00 || b == 0x4E; });
    return menschlich(platz) + (leer ? ", leer" : ", beschrieben");
}

std::string menschlich(uint64_t bytes) {
    char p[32];
    if (bytes >= 1024) std::snprintf(p, sizeof(p), "%llu KB",
                                     static_cast<unsigned long long>(bytes / 1024));
    else               std::snprintf(p, sizeof(p), "%llu B",
                                     static_cast<unsigned long long>(bytes));
    return p;
}

/// @brief Liste von Zeichenketten als JSON-Feld.
std::string jsonListe(const std::vector<std::string>& v);

std::string jsonText(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

std::string jsonListe(const std::vector<std::string>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + jsonText(v[i]);
    return o + "]";
}

// ─── Kommandos ───────────────────────────────────────────────────────────────

int cmd_ls(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);
    if (!v) return rc;

    const std::vector<FileEntry> liste = v->list();

    if (o.json) {
        std::cout << "{\"image\":" << jsonText(v->path())
                  << ",\"format\":" << jsonText(v->detection().format)
                  << ",\"filesystem\":" << jsonText(v->detection().filesystem)
                  << ",\"volumes\":" << v->volumeCount()
                  << ",\"files\":[";
        for (size_t i = 0; i < liste.size(); ++i) {
            const FileEntry& e = liste[i];
            std::cout << (i ? "," : "") << "{\"volume\":" << e.volume
                      << ",\"name\":" << jsonText(e.name)
                      << ",\"size\":" << e.size
                      << ",\"type\":" << jsonText(e.type)
                      << ",\"attrs\":" << jsonText(e.attributes)
                      << ",\"entry\":" << e.entry_addr
                      << ",\"record_len\":" << e.record_len
                      << ",\"segment\":" << e.segment_start
                      << ",\"segment_len\":" << e.segment_len
                      << ",\"low_addr\":" << e.low_addr
                      << ",\"high_addr\":" << e.high_addr
                      << ",\"stack_size\":" << e.stack_size
                      << ",\"block_len\":" << e.block_len
                      << ",\"extra\":" << e.extra
                      << ",\"created\":" << jsonText(e.created)
                      << ",\"date\":" << jsonText(e.date)
                      << ",\"hidden\":" << (e.hidden ? "true" : "false") << "}";
        }
        std::cout << "]}\n";
        return kOk;
    }

    // In der KURZFORM gehoert auf die Standardausgabe nur, was weiterverarbeitet
    // wird — die Namen.  Kopf- und Fusszeile sind Beiwerk und gehen nach stderr,
    // damit `ls … | xargs` und `| grep` sauber bleiben.  Mit -l ist die Ausgabe
    // fuer Menschen gedacht und darf zusammenbleiben.
    std::ostream& info_aus = o.lang ? std::cout : std::cerr;

    info_aus << "Dateisystem: " << v->detection().filesystem
             << (o.fs.empty() ? " (erkannt)" : " (gewaehlt)");
    if (!v->detection().unambiguous) info_aus << " — nicht eindeutig";
    info_aus << " · " << v->volumeCount() << (v->volumeCount() == 1 ? " Seite" : " Seiten");
    for (int i = 0; i < v->volumeCount(); ++i) {
        const std::string l = v->volumeInfo(i).label;
        if (!l.empty()) info_aus << (i == 0 ? " · " : " / ") << l;
    }
    info_aus << "\n";
    if (!v->detection().remarks.empty())
        info_aus << "Medium: " << v->detection().remarks << "\n";

    const bool seiten = v->volumeCount() > 1;

    if (!o.lang) {
        // Kurzform wie `cpmls`: ein Name je Zeile, nichts sonst — so laesst sich die
        // Ausgabe weiterverarbeiten (`| grep`, `| xargs`).  Bei mehreren Seiten traegt
        // jeder Name sein Praefix und ist damit auch als Argument wieder brauchbar.
        for (const FileEntry& e : liste)
            std::cout << (seiten ? v->volumeDir(e.volume) + "/" : "")
                      << e.qualifiedName() << "\n";
    } else {
        if (seiten) std::cout << "Seite ";
        std::cout << "Name                 Typ   Groesse  Eigensch. Start Datum\n";
        for (const FileEntry& e : liste) {
            // Die Startadresse steht nur bei UDOS-Programmen (Typ P/P1) und gehoert
            // dorthin: ohne sie laesst sich eine Systemdatei nicht zurueckschreiben.
            char start[8] = "     ";
            if (e.entry_addr != 0) std::snprintf(start, sizeof start, "%04X ", e.entry_addr);
            if (seiten) std::printf("%-5d ", e.volume);
            std::printf("%-20s %-3s %8llu  %-9s %s%s%s\n",
                        e.qualifiedName().c_str(), e.type.c_str(),
                        static_cast<unsigned long long>(e.size),
                        e.attributes.c_str(), start, e.date.c_str(),
                        e.damaged ? "  [DEFEKT]" : "");
        }
    }
    info_aus << liste.size() << " Dateien";
    for (int i = 0; i < v->volumeCount(); ++i) {
        const FsInfo info = v->volumeInfo(i);
        info_aus << " · " << (seiten ? v->volumeDir(i) + ": " : "")
                 << menschlich(info.free_bytes) << " frei";
    }
    info_aus << "\n";
    return kOk;
}

int cmd_info(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);
    if (!v) return rc;

    if (o.json) {
        std::cout << "{\"image\":" << jsonText(v->path())
                  << ",\"format\":" << jsonText(v->detection().format)
                  << ",\"filesystem\":" << jsonText(v->detection().filesystem)
                  << ",\"unambiguous\":" << (v->detection().unambiguous ? "true" : "false")
                  << ",\"alternatives\":" << jsonListe(v->detection().alternatives)
                  << ",\"remarks\":" << jsonText(v->detection().remarks)
                  << ",\"read_only\":" << (v->readOnly() ? "true" : "false")
                  << ",\"volumes\":[";
        for (int i = 0; i < v->volumeCount(); ++i) {
            const FsInfo info = v->volumeInfo(i);
            std::cout << (i ? "," : "")
                      << "{\"index\":" << i
                      << ",\"dir\":" << jsonText(v->volumeDir(i))
                      << ",\"label\":" << jsonText(info.label)
                      << ",\"total\":" << info.total_bytes
                      << ",\"used\":"  << info.used_bytes
                      << ",\"free\":"  << info.free_bytes
                      << ",\"files\":" << info.files
                      << ",\"boot_area\":" << v->bootAreaSize(i)
                      << ",\"warnings\":" << jsonListe(info.warnings) << "}";
        }
        std::cout << "]}\n";
        return kOk;
    }

    std::cout << "Abbild:      " << v->path() << "\n"
              << "Format:      " << v->detection().format << "\n"
              << "Dateisystem: " << v->detection().filesystem;
    if (!v->detection().unambiguous) {
        std::cout << " (nicht eindeutig";
        if (!v->detection().alternatives.empty()) {
            std::cout << ", auch moeglich: ";
            for (size_t i = 0; i < v->detection().alternatives.size(); ++i)
                std::cout << (i ? ", " : "") << v->detection().alternatives[i];
        }
        std::cout << ")";
    }
    std::cout << "\n";
    if (!v->detection().remarks.empty())
        std::cout << "Medium:      " << v->detection().remarks << "\n";

    for (int i = 0; i < v->volumeCount(); ++i) {
        const FsInfo info = v->volumeInfo(i);
        const std::string wo = v->volumeDir(i);
        std::cout << (wo.empty() ? "Datentraeger" : wo);
        if (!info.label.empty()) std::cout << " '" << info.label << "'";
        std::cout << ": " << info.files << " Dateien, " << menschlich(info.used_bytes)
                  << " belegt, " << menschlich(info.free_bytes) << " frei\n";
        std::cout << "  Systemspuren: " << bootZustand(*v, i) << "\n";
        for (const std::string& w : info.warnings) std::cout << "  ! " << w << "\n";
    }
    return kOk;
}

int cmd_check(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);
    if (!v) return rc;

    if (o.json) {
        std::vector<std::string> unlesbar, warnungen;
        for (const FileEntry& e : v->list())
            if (e.damaged)
                unlesbar.push_back((v->volumeCount() > 1
                                        ? v->volumeDir(e.volume) + "/" : "")
                                   + e.qualifiedName());
        for (int i = 0; i < v->volumeCount(); ++i)
            for (const std::string& w : v->volumeInfo(i).warnings) warnungen.push_back(w);

        const bool ok = unlesbar.empty() && warnungen.empty();
        std::cout << "{\"image\":" << jsonText(v->path())
                  << ",\"ok\":" << (ok ? "true" : "false")
                  << ",\"damaged\":" << jsonListe(unlesbar)
                  << ",\"warnings\":" << jsonListe(warnungen)
                  << ",\"remarks\":" << jsonText(v->detection().remarks) << "}\n";
        return ok ? kOk : kFehler;
    }

    int defekt = 0;
    for (const FileEntry& e : v->list()) {
        if (!e.damaged) continue;
        ++defekt;
        std::cout << "  ! nicht lesbar: " << v->volumeDir(e.volume)
                  << (v->volumeCount() > 1 ? "/" : "") << e.name << "\n";
    }
    int warnungen = 0;
    for (int i = 0; i < v->volumeCount(); ++i)
        for (const std::string& w : v->volumeInfo(i).warnings) {
            ++warnungen;
            std::cout << "  ! " << w << "\n";
        }
    if (!v->detection().remarks.empty())
        std::cout << "  Hinweis: " << v->detection().remarks << "\n";

    if (defekt == 0 && warnungen == 0) std::cout << "ohne Befund\n";
    return (defekt || warnungen) ? kFehler : kOk;
}

int cmd_get(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }
    if (o.ziel.empty())    { std::cerr << "Fehler: --to <verzeichnis> fehlt\n"; return kFehler; }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);
    if (!v) return rc;

    TransferOptions t;
    t.text      = o.text;
    t.overwrite = o.force;
    t.dry_run   = o.dry_run;

    // Ohne Muster: alles (mit SideN-Ordnern, wenn die Diskette mehrere Seiten hat).
    if (o.rest.size() == 2) {
        if (!v->extractAll(o.ziel, t)) { std::cerr << "Fehler: " << v->lastError() << "\n";
                                         return kFehler; }
        std::cout << v->list().size() << " Dateien nach " << o.ziel << "\n";
        return kOk;
    }

    std::error_code ec;
    fs::create_directories(o.ziel, ec);

    int n = 0;
    for (size_t i = 2; i < o.rest.size(); ++i) {
        const FileRef muster = FileRef::parse(o.rest[i], o.volume);
        const bool mit_seite = o.rest[i].find('/') != std::string::npos;

        for (const FileEntry& e : v->list()) {
            if (mit_seite && e.volume != muster.volume) continue;
            if (!passt(muster.name, e.qualifiedName())) continue;

            std::string datei = e.qualifiedName();
            std::replace(datei.begin(), datei.end(), ':', '_');
            fs::path ziel = fs::path(o.ziel);
            if (v->volumeCount() > 1) {
                ziel /= v->volumeDir(e.volume);
                fs::create_directories(ziel, ec);
            }
            ziel /= datei;

            if (!v->extract(FileRef{e.volume, e.qualifiedName()}, ziel.string(), t)) {
                std::cerr << "Fehler: " << v->lastError() << "\n";
                return kFehler;
            }
            std::cout << e.qualifiedName() << " → " << ziel.string() << "\n";
            ++n;
        }
    }
    if (n == 0) { std::cerr << "Fehler: kein Eintrag passt auf das Muster\n"; return kFehler; }
    std::cout << n << " Dateien\n";
    return kOk;
}

int cmd_put(const Optionen& o) {
    if (o.rest.size() < 3) {
        std::cerr << "Fehler: Abbild und mindestens eine Datei oder ein Ordner\n";
        return kFehler;
    }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc, /*schreibend=*/true);
    if (!v) return rc;

    TransferOptions t;
    t.text      = o.text;
    t.overwrite = o.force;
    t.dry_run   = o.dry_run;
    t.udos_type       = o.udos_typ;
    t.udos_properties = o.udos_eig;
    t.udos_entry      = static_cast<uint16_t>(o.udos_entry);
    t.udos_record_len = static_cast<uint16_t>(o.udos_satz);
    t.udos_segment       = static_cast<uint16_t>(o.udos_segment);
    t.udos_segment_len  = static_cast<uint16_t>(o.udos_ilen);
    t.udos_block_len = static_cast<uint16_t>(o.udos_block);
    t.udos_block_len_gesetzt = o.udos_block_gesetzt;
    if (!o.udos_zusatz.empty())
        t.udos_extra = static_cast<uint32_t>(std::strtoul(o.udos_zusatz.c_str(), nullptr, 16));
    t.udos_created  = o.udos_erst;
    t.udos_modified = o.udos_geaend;
    if (!o.udos_mem.empty()) {
        // LOW:HIGH[:STACK], alles hexadezimal
        unsigned von = 0, bis = 0, kz = 0x0080;
        std::string x = o.udos_mem;
        std::replace(x.begin(), x.end(), ':', ' ');
        std::istringstream z(x);
        std::string a1, a2, a3;
        z >> a1 >> a2;
        von = std::strtoul(a1.c_str(), nullptr, 16);
        bis = std::strtoul(a2.c_str(), nullptr, 16);
        if (z >> a3) kz = std::strtoul(a3.c_str(), nullptr, 16);
        t.udos_low_addr = static_cast<uint16_t>(von);
        t.udos_high_addr   = static_cast<uint16_t>(bis);
        t.udos_stack_size = static_cast<uint16_t>(kz);
    }

    // Genau EIN Ordner → Stapeloperation mit Struktur- und Platzpruefung.
    std::error_code ec;
    if (o.rest.size() == 3 && fs::is_directory(o.rest[2], ec)) {
        std::string bericht;
        if (!v->checkFit(o.rest[2], bericht)) {
            std::cerr << "Fehler: " << bericht;
            if (bericht.empty() || bericht.back() != '\n') std::cerr << "\n";
            const bool struktur = bericht.find("Unterverzeichnisse") != std::string::npos
                               || bericht.find("SideN") != std::string::npos
                               || bericht.find("Side0") != std::string::npos;
            return struktur ? kStruktur : kPasstNicht;
        }
        if (o.dry_run) { std::cout << "passt\n"; return kOk; }
        if (!v->insertAll(o.rest[2], t)) {
            std::cerr << "Fehler: " << v->lastError() << "\n";
            const std::string e = v->lastError();
            if (e.find("Unterverzeichnisse") != std::string::npos) return kStruktur;
            return kPasstNicht;
        }
        if (!v->flush()) { std::cerr << "Fehler: " << v->lastError() << "\n"; return kFehler; }
        std::cout << "eingefuegt aus " << o.rest[2] << "\n";
        return kOk;
    }

    for (size_t i = 2; i < o.rest.size(); ++i) {
        if (fs::is_directory(o.rest[i], ec)) {
            std::cerr << "Fehler: " << o.rest[i] << " ist ein Ordner — einen Ordner bitte "
                         "allein angeben\n";
            return kFehler;
        }
        std::string name = o.als.empty() ? fs::path(o.rest[i]).filename().string() : o.als;
        FileRef ref = FileRef::parse(name, o.volume);
        if (!v->insert(o.rest[i], ref, t)) {
            std::cerr << "Fehler: " << v->lastError() << "\n";
            return v->lastError().find("voll") != std::string::npos ? kPasstNicht : kFehler;
        }
        std::cout << o.rest[i] << " → " << name << "\n";
    }
    if (o.dry_run) return kOk;
    if (!v->flush()) { std::cerr << "Fehler: " << v->lastError() << "\n"; return kFehler; }
    return kOk;
}

int cmd_rm(const Optionen& o) {
    if (o.rest.size() < 3) { std::cerr << "Fehler: Abbild und Muster angeben\n"; return kFehler; }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc, /*schreibend=*/true);
    if (!v) return rc;

    int n = 0;
    for (size_t i = 2; i < o.rest.size(); ++i) {
        const FileRef muster = FileRef::parse(o.rest[i], o.volume);
        const bool mit_seite = o.rest[i].find('/') != std::string::npos;

        // Erst sammeln, dann loeschen — list() aendert sich waehrenddessen.
        std::vector<FileRef> treffer;
        for (const FileEntry& e : v->list()) {
            if (mit_seite && e.volume != muster.volume) continue;
            if (passt(muster.name, e.qualifiedName()))
                treffer.push_back(FileRef{e.volume, e.qualifiedName()});
        }
        for (const FileRef& r : treffer) {
            if (o.dry_run) { std::cout << "wuerde loeschen: " << r.name << "\n"; ++n; continue; }
            if (!v->erase(r)) { std::cerr << "Fehler: " << v->lastError() << "\n";
                                return kFehler; }
            std::cout << "geloescht: " << r.name << "\n";
            ++n;
        }
    }
    if (n == 0) { std::cerr << "Fehler: kein Eintrag passt auf das Muster\n"; return kFehler; }
    if (o.dry_run) return kOk;
    if (!v->flush()) { std::cerr << "Fehler: " << v->lastError() << "\n"; return kFehler; }
    return kOk;
}

int cmd_create(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }
    if (o.fs.empty()) {
        std::cerr << "Fehler: --fs NAME fehlt — beim Anlegen gibt es nichts zu erkennen "
                     "(`k1520disktool formats` zeigt die bekannten)\n";
        return kFehler;
    }
    std::string err;
    auto v = DiskVolume::create(o.rest[1], o.fs, o.label, formate(), dateisysteme(), err, o.boot);
    if (!v) { std::cerr << "Fehler: " << err << "\n"; return kFehler; }

    std::cout << "angelegt: " << o.rest[1] << " (" << o.fs << ", "
              << v->volumeCount() << (v->volumeCount() == 1 ? " Seite" : " Seiten") << ")\n";
    if (!o.boot.empty())
        std::cout << "Bootabbild: " << o.boot << " → Systemspuren ("
                  << menschlich(v->bootAreaSize()) << " Platz)\n";
    return kOk;
}

/**
 * @brief `boot-get` — die Systemspuren einer Diskette herausschreiben.
 *
 * So kommt man an das Bootabbild eines Fremdsystems, von dem es keine `.bin` gibt:
 * einmal aus einer laufenden Diskette holen, danach beliebig oft mit
 * `create --boot` einspielen.
 */
int cmd_boot_get(const Optionen& o) {
    if (o.rest.size() < 3) {
        std::cerr << "Fehler: Abbild und Zieldatei angeben\n";
        return kFehler;
    }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);           // lesend genuegt
    if (!v) return rc;

    if (!v->readBootImageToFile(o.rest[2], o.volume)) {
        std::cerr << "Fehler: " << v->lastError() << "\n";
        return kFehler;
    }
    std::cout << o.rest[1] << " → " << o.rest[2] << " ("
              << v->bootAreaSize(o.volume) << " Byte Systemspuren)\n";
    return kOk;
}

/// @brief `boot-put` — ein Bootabbild in die Systemspuren einer vorhandenen Diskette.
int cmd_boot_put(const Optionen& o) {
    if (o.rest.size() < 3) {
        std::cerr << "Fehler: Abbild und Bootabbild angeben\n";
        return kFehler;
    }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc, /*schreibend=*/true);
    if (!v) return rc;

    if (!v->writeBootImageFile(o.rest[2], o.volume) || !v->flush()) {
        std::cerr << "Fehler: " << v->lastError() << "\n";
        return kFehler;
    }
    std::cout << o.rest[2] << " → " << o.rest[1] << " (Systemspuren, "
              << menschlich(v->bootAreaSize(o.volume)) << " Platz)\n";
    return kOk;
}

/**
 * @brief `attr` — die UDOS-Kopfsektorangaben einer Datei zeigen und aendern.
 *
 * Ohne Schalter nur anzeigen; jeder gesetzte Schalter aendert genau sein Feld und
 * laesst die uebrigen stehen.  Der Dateiinhalt wird dabei nicht angefasst.
 */
int cmd_attr(const Optionen& o) {
    if (o.rest.size() < 3) {
        std::cerr << "Fehler: Abbild und Dateiname angeben\n";
        return kFehler;
    }
    const bool aendern_udos = !o.udos_typ.empty() || !o.udos_eig.empty() || o.udos_entry
                      || o.udos_block_gesetzt || o.udos_segment || o.udos_ilen
                      || !o.udos_mem.empty() || !o.udos_zusatz.empty()
                      || !o.udos_erst.empty() || !o.udos_geaend.empty();
    const bool aendern_cpm = o.cpm_ro_gesetzt || o.cpm_sys_gesetzt
                          || o.cpm_arc_gesetzt || o.cpm_user_gesetzt;
    const bool aendern = aendern_udos || aendern_cpm;

    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc, /*schreibend=*/aendern);
    if (!v) return rc;

    const FileRef ref = FileRef::parse(o.rest[2], o.volume);
    // Bei CP/M darf der Name den Nutzerbereich tragen ("3:NAME.TYP") — nach einem
    // `--user`-Wechsel steht die Datei sogar nur noch unter dem NEUEN Praefix.
    auto suche = [&](int user_bereich) -> const FileEntry* {
        static FileEntry treffer;
        for (const FileEntry& e : v->list()) {
            if (e.volume != ref.volume) continue;
            if (e.name != ref.name && e.qualifiedName() != ref.name) continue;
            if (user_bereich >= 0 && e.user != user_bereich) continue;
            treffer = e;
            return &treffer;
        }
        return nullptr;
    };
    if (!suche(-1)) {
        std::cerr << "Fehler: '" << o.rest[2] << "' steht nicht im Verzeichnis\n";
        return kFehler;
    }

    if (aendern_cpm) {
        CpmAttrs c;
        c.set_read_only = o.cpm_ro_gesetzt;  c.read_only = o.cpm_ro;
        c.set_system    = o.cpm_sys_gesetzt; c.system    = o.cpm_sys;
        c.set_archived  = o.cpm_arc_gesetzt; c.archived  = o.cpm_arc;
        c.set_user      = o.cpm_user_gesetzt; c.user     = o.cpm_user;
        if (!v->setAttributes(ref, c) || !v->flush()) {
            std::cerr << "Fehler: " << v->lastError() << "\n";
            return kFehler;
        }
    }

    if (aendern_udos) {
        UdosAttrs a;
        a.type       = o.udos_typ;
        a.properties = o.udos_eig;
        a.created    = o.udos_erst;
        a.modified   = o.udos_geaend;
        if (o.udos_entry)        { a.set_entry = true; a.entry = static_cast<uint16_t>(o.udos_entry); }
        if (o.udos_block_gesetzt){ a.set_block_len = true;
                                   a.block_len = static_cast<uint16_t>(o.udos_block); }
        if (o.udos_segment || o.udos_ilen) {
            a.set_segment = true;
            a.segment     = static_cast<uint16_t>(o.udos_segment);
            a.segment_len = static_cast<uint16_t>(o.udos_ilen);
        }
        if (!o.udos_mem.empty()) {
            std::string x = o.udos_mem;
            std::replace(x.begin(), x.end(), ':', ' ');
            std::istringstream z(x);
            std::string p1, p2, p3;
            z >> p1 >> p2;
            a.set_memory = true;
            a.low  = static_cast<uint16_t>(std::strtoul(p1.c_str(), nullptr, 16));
            a.high = static_cast<uint16_t>(std::strtoul(p2.c_str(), nullptr, 16));
            a.stack = (z >> p3) ? static_cast<uint16_t>(std::strtoul(p3.c_str(), nullptr, 16))
                                : 0;
        }
        if (!o.udos_zusatz.empty()) {
            a.set_extra = true;
            a.extra = static_cast<uint32_t>(std::strtoul(o.udos_zusatz.c_str(), nullptr, 16));
        }
        if (!v->setAttributes(ref, a) || !v->flush()) {
            std::cerr << "Fehler: " << v->lastError() << "\n";
            return kFehler;
        }
    }

    const FileEntry* e = suche(o.cpm_user_gesetzt ? o.cpm_user : -1);
    if (!e) { std::cerr << "Fehler: Eintrag nach dem Aendern nicht mehr lesbar\n"; return kFehler; }
    if (o.json) {
        std::cout << "{\"name\":" << jsonText(e->name)
                  << ",\"type\":" << jsonText(e->type)
                  << ",\"attrs\":" << jsonText(e->attributes)
                  << ",\"user\":" << e->user
                  << ",\"size\":" << e->size
                  << ",\"bytes_in_last\":" << e->bytes_in_last
                  << ",\"entry\":" << e->entry_addr
                  << ",\"record_len\":" << e->record_len
                  << ",\"block_len\":" << e->block_len
                  << ",\"segment\":" << e->segment_start
                  << ",\"segment_len\":" << e->segment_len
                  << ",\"low_addr\":" << e->low_addr
                  << ",\"high_addr\":" << e->high_addr
                  << ",\"stack_size\":" << e->stack_size
                  << ",\"extra\":" << e->extra
                  << ",\"created\":" << jsonText(e->created)
                  << ",\"date\":" << jsonText(e->date) << "}\n";
        return kOk;
    }
    // CP/M fuehrt keine Kopfsektorangaben — dort waeren zehn Nullzeilen keine
    // Auskunft, sondern Rauschen.
    if (e->type.empty()) {
        std::printf("%-20s Nutzerbereich %d  Attribute %-8s  %llu Byte\n",
                    e->name.c_str(), e->user,
                    e->attributes.empty() ? "-" : e->attributes.c_str(),
                    static_cast<unsigned long long>(e->size));
        std::printf("  RO %s   SYS %s   ARC %s\n",
                    e->attributes.find("RO")  != std::string::npos ? "ja" : "nein",
                    e->attributes.find("SYS") != std::string::npos ? "ja" : "nein",
                    e->attributes.find("ARC") != std::string::npos ? "ja" : "nein");
        return kOk;
    }

    std::printf("%-20s Typ %-2s  Eigenschaften %-6s  %llu Byte\n",
                e->name.c_str(), e->type.c_str(),
                e->attributes.empty() ? "-" : e->attributes.c_str(),
                static_cast<unsigned long long>(e->size));
    std::printf("  ENTRY %04X   Satzlaenge %u   zweite Laenge %u   letzter Satz %u Byte\n",
                e->entry_addr, e->record_len, e->block_len, e->bytes_in_last);
    std::printf("  SEGMENT %04X + %u Byte\n", e->segment_start, e->segment_len);
    std::printf("  LOW %04X  HIGH %04X  STACK %04X   Zusatz %08X\n",
                e->low_addr, e->high_addr, e->stack_size, e->extra);
    std::printf("  erstellt '%s'   geaendert '%s'\n",
                e->created.c_str(), e->date.c_str());
    return kOk;
}

/**
 * @brief `measure` — die Geometrie eines Abbilds MESSEN, ohne ein Dateisystem zu
 *        verlangen.
 *
 * Fuer unbekannte oder frisch formatierte Disketten: `ls` bricht dort ab, weil kein
 * Dateisystem passt.  Diese Ausgabe ist zugleich die Vorlage fuer einen neuen
 * `formats:`-Eintrag in data/formats.yaml.
 */
int cmd_measure(const Optionen& o) {
    if (o.rest.size() < 2) { std::cerr << "Fehler: kein Abbild angegeben\n"; return kFehler; }

    auto disk = DiskImage::open(o.rest[1], std::nullopt, /*write_protect=*/true);
    if (!disk) {
        std::cerr << "Fehler: Abbild nicht ladbar (selbstbeschreibend muss es sein — "
                     ".hfe oder .dmk): " << o.rest[1] << "\n";
        return kFehler;
    }
    const std::vector<MeasuredTrack> gemessen = GeometryProbe::measure(disk->medium());
    const std::vector<GeometryMatch> treffer =
        GeometryProbe::matchAll(gemessen, formate().formats());

    if (o.json) {
        std::cout << "{\"image\":" << jsonText(o.rest[1]) << ",\"tracks\":[";
        bool erstes = true;
        for (const MeasuredTrack& t : gemessen) {
            if (!t.formatted) continue;
            std::cout << (erstes ? "" : ",")
                      << "{\"cyl\":" << int(t.cyl) << ",\"head\":" << int(t.head)
                      << ",\"sectors\":" << int(t.sectors)
                      << ",\"size\":" << t.sector_size
                      << ",\"first_id\":" << int(t.first_id)
                      << ",\"encoding\":" << jsonText(t.encoding == Encoding::FM ? "fm" : "mfm")
                      << ",\"crc_errors\":" << t.crc_errors << "}";
            erstes = false;
        }
        std::cout << "],\"matches\":[";
        for (size_t i = 0; i < treffer.size(); ++i)
            std::cout << (i ? "," : "") << "{\"format\":" << jsonText(treffer[i].format->name)
                      << ",\"remarks\":" << jsonText(treffer[i].remarks()) << "}";
        std::cout << "]}\n";
        return treffer.empty() ? kNichtErkannt : kOk;
    }

    std::cout << "Gemessen:\n" << GeometryProbe::describe(gemessen);
    if (treffer.empty()) {
        std::cout << "\nKein Format in data/formats.yaml passt.\n";
        return kNichtErkannt;
    }
    std::cout << "\nPasst zu:\n";
    for (const GeometryMatch& m : treffer)
        std::cout << "  " << m.format->name
                  << (m.remarks().empty() ? "" : "   (" + m.remarks() + ")") << "\n";
    return kOk;
}

int cmd_save_as(const Optionen& o) {
    if (o.rest.size() < 3) {
        std::cerr << "Fehler: Quellabbild und Zielabbild angeben\n";
        return kFehler;
    }
    int rc = kOk;
    auto v = oeffne(o, o.rest[1], rc);          // lesend genuegt — die Quelle bleibt heil
    if (!v) return rc;

    if (!v->exportImage(o.rest[2])) {
        std::cerr << "Fehler: " << v->lastError() << "\n";
        return kFehler;
    }
    std::cout << o.rest[1] << " → " << o.rest[2] << "\n";
    return kOk;
}

int cmd_formats(const Optionen& o) {
    if (o.json) {
        std::cout << "[";
        bool erstes = true;
        for (const FsProfile& p : dateisysteme().profiles()) {
            std::cout << (erstes ? "" : ",")
                      << "{\"name\":" << jsonText(p.name)
                      << ",\"type\":" << jsonText(fsTypeName(p.type))
                      << ",\"format\":" << jsonText(p.format)
                      << ",\"description\":" << jsonText(p.description) << "}";
            erstes = false;
        }
        std::cout << "]\n";
        return kOk;
    }
    std::printf("%-14s %-6s %-18s %s\n", "Name", "Typ", "Geometrie", "Beschreibung");
    for (const FsProfile& p : dateisysteme().profiles())
        std::printf("%-14s %-6s %-18s %s\n", p.name.c_str(), fsTypeName(p.type),
                    p.format.c_str(), p.description.c_str());
    // Steht in keinem Katalog, laesst sich aber ueberall angeben — sonst waere die
    // wichtigste Antwort auf „welche Dateisysteme gibt es?" gerade die unsichtbare.
    std::printf("%-14s %-6s %-18s %s\n", CpaDpbRule::kName, "cpm", "(jede)",
                "aus der Geometrie nach der CP/A-Regel abgeleitet — Rueckfall beim "
                "Oeffnen, mit --fs erzwingbar");
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { gebrauch(); return kFehler; }

    const std::string befehl = argv[1];
    if (befehl == "-h" || befehl == "--help" || befehl == "help") { gebrauch(); return kOk; }

    Optionen o;
    std::string err;
    o.rest.push_back(befehl);
    if (!zerlege(argc, argv, 2, o, err)) {
        std::cerr << "Fehler: " << err << "\n";
        return kFehler;
    }
    if (o.text && o.binaer) {
        std::cerr << "Fehler: --text und --binary schliessen sich aus\n";
        return kFehler;
    }

    if (befehl == "ls")      return cmd_ls(o);
    if (befehl == "get")     return cmd_get(o);
    if (befehl == "put")     return cmd_put(o);
    if (befehl == "rm")      return cmd_rm(o);
    if (befehl == "create")  return cmd_create(o);
    if (befehl == "attr")    return cmd_attr(o);
    if (befehl == "boot-get") return cmd_boot_get(o);
    if (befehl == "boot-put") return cmd_boot_put(o);
    if (befehl == "info")    return cmd_info(o);
    if (befehl == "save-as") return cmd_save_as(o);
    if (befehl == "measure") return cmd_measure(o);
    if (befehl == "check")   return cmd_check(o);
    if (befehl == "formats") return cmd_formats(o);

    std::cerr << "Fehler: unbekanntes Kommando '" << befehl << "'\n\n";
    gebrauch();
    return kFehler;
}
