/**
 * @file disk_volume.cpp
 * @brief Umsetzung von @ref DiskVolume.
 *
 * @see doc/design/13_k1520disktool.md §9, §12
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/disk_volume.h"

#include "core/filesystem/cpm/cpa_dpb.h"
#include "core/filesystem/cpm/cpm_fs.h"
#include "core/filesystem/geometry_probe.h"
#include "core/filesystem/udos/udos_fs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string kleinbuchstaben(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string endung(const std::string& pfad) {
    const size_t p = pfad.find_last_of('.');
    if (p == std::string::npos) return {};
    return kleinbuchstaben(pfad.substr(p + 1));
}

/**
 * @brief Positivprobe eines CP/M-Profils: laesst sich das Verzeichnis plausibel lesen?
 *
 * Ein falsches Profil trifft fast immer Datenbytes statt Verzeichniseintraege — und die
 * fallen hier durch (Nutzerbereich > 15, Kleinbuchstaben oder Steuerzeichen im Namen,
 * Blocknummer ausserhalb der Diskette).  Ein einziger schlechter Eintrag reicht zum
 * Verwerfen; eine fabrikfrische Diskette (alles 0xE5) besteht die Probe.
 *
 * @param warum                gesetzt, wenn false zurueckkommt — gehoert in die Meldung
 * @param uneingerichtet_zaehlt „formatiert, aber nie eingerichtet" durchgehen lassen
 *                             (nur beim abgeleiteten Profil, s. u.)
 * @param hinweis              dann der Klartext dazu
 */
bool cpmVerzeichnisPlausibel(DiskMedium& medium, const DiskFormat& f, const FsProfile& p,
                             std::string* warum = nullptr,
                             bool uneingerichtet_zaehlt = false,
                             std::string* hinweis = nullptr) {
    auto nein = [&](const std::string& t) { if (warum) *warum = t; return false; };

    SectorSpace raum(medium, f);
    std::string grund;
    auto cpm = CpmFileSystem::mount(raum, p, grund);
    if (!cpm) return nein(grund);

    // „Formatiert, aber nie eingerichtet": der ganze Verzeichnisbereich traegt EIN
    // Fuellbyte.  Nur bei einem abgeleiteten Profil zulassen — dort steht durch die
    // CP/A-Regel fest, WO das Verzeichnis liegt, und ein durchgehend gleiches Muster
    // laesst keine zweite Deutung zu.  `list()` ueberspringt solche Plaetze ohnehin
    // (Nutzerbereich > 15), die Diskette erscheint also leer, was sie auch ist.
    if (uneingerichtet_zaehlt) {
        const int fuell = cpm->directoryFill();
        if (fuell >= 0 && fuell != 0xE5) {
            char t[64];
            std::snprintf(t, sizeof t,
                          "Verzeichnis nicht angelegt (Fuellbyte 0x%02X)", fuell);
            if (hinweis) *hinweis = t;
            return true;
        }
    }

    int gut = 0;
    for (const CpmDirEntry& d : cpm->directory()) {
        if (d.free()) { ++gut; continue; }
        const std::string wo = "Verzeichnisplatz " + std::to_string(d.index);
        if (d.user > 15) {
            char t[80];
            std::snprintf(t, sizeof t, "%s traegt Nutzerbereich 0x%02X — das "
                          "Verzeichnis ist nicht angelegt", wo.c_str(), d.user);
            return nein(t);
        }
        bool ok = !d.name.empty();
        for (char c : d.name)
            if (c < 0x20 || c > 0x7E || (c >= 'a' && c <= 'z')) ok = false;
        for (uint16_t b : d.blocks)
            if (b >= cpm->totalBlocks()) ok = false;
        if (d.records > 0x80) ok = false;
        if (!ok) return nein(wo + " ist kein gueltiger Eintrag");
        ++gut;
    }
    if (gut == 0) return nein("Verzeichnis ist leer gelesen worden");
    return true;
}

/**
 * @brief Traegt Sektor 1 der Spur 0 einen MS-DOS-Urlader (FAT)?
 *
 * FORMAT.COM legt auf Wunsch **DOS-Disketten** an — die Menuepunkte mit dem Vermerk
 * `{MSDOS}` (doc/format.md §3.3 `E`/`J`/`K`, §3.4).  Darauf liegt kein CP/M-Verzeichnis,
 * und ohne diese Probe endete der Versuch in der wenig hilfreichen Meldung „kein
 * bekanntes Dateisystem".  Erkannt wird der uebliche BPB: Sprungbefehl, plausible
 * Sektorgroesse, Medienkennung ab 0xF0.
 *
 * @return Klartext („MS-DOS-Dateisystem (FAT), Kennung 'CP/A1188'"), sonst leer.
 */
std::string dosKennung(DiskMedium& medium, const DiskFormat& f) {
    SectorSpace raum(medium, f);
    if (raum.trackCount() == 0) return {};
    const SectorSpace::TrackRef t = raum.trackAt(0);
    SectorData s;
    if (!raum.readSector(t.cyl, t.head, t.first_id, s) || s.data.size() < 32) return {};

    const uint8_t* b = s.data.data();
    const bool sprung = (b[0] == 0xEB && b[2] == 0x90) || b[0] == 0xE9;
    const uint16_t bps = static_cast<uint16_t>(b[11] | (b[12] << 8));
    if (!sprung || (bps != 128 && bps != 256 && bps != 512 && bps != 1024)) return {};
    if (b[21] < 0xF0 || b[13] == 0) return {};

    std::string oem;
    for (int i = 3; i < 11; ++i)
        if (b[i] >= 0x20 && b[i] < 0x7F) oem += static_cast<char>(b[i]);
    while (!oem.empty() && oem.back() == ' ') oem.pop_back();

    std::string t2 = "die Diskette traegt ein MS-DOS-Dateisystem (FAT)";
    if (!oem.empty()) t2 += ", Kennung '" + oem + "'";
    return t2;
}

bool leseDatei(const std::string& pfad, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(pfad, std::ios::binary);
    if (!f) { err = "Datei nicht lesbar: " + pfad; return false; }
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool schreibeDatei(const std::string& pfad, const std::vector<uint8_t>& d, std::string& err) {
    std::ofstream f(pfad, std::ios::binary);
    if (!f) { err = "Datei nicht schreibbar: " + pfad; return false; }
    if (!d.empty()) f.write(reinterpret_cast<const char*>(d.data()),
                            static_cast<std::streamsize>(d.size()));
    if (!f) { err = "Schreibfehler: " + pfad; return false; }
    return true;
}

/// @brief Diskette → Linux: CR LF und einzelnes CR werden LF, ab 0x1A ist Schluss.
std::vector<uint8_t> nachLinuxText(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t c = in[i];
        if (c == 0x1A) break;                       // CP/M-Dateiende
        if (c == '\r') {
            if (i + 1 < in.size() && in[i + 1] == '\n') continue;   // CR LF → LF
            out.push_back('\n');                                     // einzelnes CR → LF
            continue;
        }
        out.push_back(c);
    }
    return out;
}

/// @brief Linux → Diskette: LF wird CR LF (CR LF bleibt CR LF).
std::vector<uint8_t> nachDiskettenText(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 16);
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t c = in[i];
        if (c == '\n' && (i == 0 || in[i - 1] != '\r')) out.push_back('\r');
        out.push_back(c);
    }
    return out;
}

/**
 * @brief Nachspannbytes je Sektor im Bootabbild — nur UDOS hat welche.
 *
 * Bei UDOS steht die Verkettung **hinter** der Daten-CRC (4 Byte Kontrollblock,
 * `doc/udos_diskettenformat.md` §1.1).  Ein Bootabbild, das nur die Datenfelder
 * traegt, verliert sie — genau daran scheiterte der erste Versuch: die Diskette
 * lief bis in den UDOS-Debugger (`BREAK F10E`) statt bis zur Datumsabfrage.  Aus
 * demselben Grund kann eine UDOS-Diskette kein `.img` sein.
 */
uint8_t nachspannBytes(const FsProfile& p) {
    return p.type == FsType::Udos ? 4 : 0;
}

/**
 * @brief Aus welchen Spuren besteht der Systembereich? (siehe disk_volume.h)
 *
 *   * **CP/M** — alle Spuren vor dem Beginn des Dateisystems (`data_cyl`/`data_head`).
 *   * **UDOS** — die Spuren 0–2 (Urlader + Nukleus) **und** die Bootspur 21.  Die
 *     Bootspur gehoert dazu, weil der Urlader sie beim Start liest: ohne sie kommt
 *     der Kaltstart bis „ERROR: 45" und nicht weiter.  Sie liegt hinter den
 *     Dateispuren, ist also kein durchgehendes Band — im Bootabbild folgt sie
 *     einfach hinten an.
 *
 * Die Reihenfolge ist die Reihenfolge im Bootabbild und darf sich nicht aendern —
 * sonst wird jede vorhandene `.bin` unbrauchbar.
 */
std::vector<SectorSpace::TrackRef> systemspuren(const SectorSpace& raum, const FsProfile& p) {
    std::vector<SectorSpace::TrackRef> out;
    auto spur = [&](uint8_t cyl) {
        const uint8_t kopf = raum.headFilter() == SectorSpace::kAllHeads
                                 ? 0 : raum.headFilter();
        const int i = raum.trackIndexOf(cyl, kopf);
        if (i >= 0) out.push_back(raum.trackAt(static_cast<size_t>(i)));
    };

    if (p.type == FsType::Udos) {
        for (uint8_t c = 0; c < 3; ++c) spur(c);   // Urlader + Nukleus
        spur(p.boot_track);                        // Bootabbild
        return out;
    }

    // CP/M: alles bis zum Beginn des Dateisystems — das ist immer eine Spurgrenze.
    const int ende = raum.trackIndexOf(p.data_cyl, p.data_head);
    for (int i = 0; i < ende; ++i) out.push_back(raum.trackAt(static_cast<size_t>(i)));
    return out;
}

/// @brief Fassungsvermoegen der Systemspuren; 0 = es gibt keine.
uint64_t systemspurBytes(const SectorSpace& raum, const FsProfile& p) {
    uint64_t n = 0;
    for (const SectorSpace::TrackRef& t : systemspuren(raum, p))
        n += static_cast<uint64_t>(t.sectors) * (t.sector_size + nachspannBytes(p));
    return n;
}


// ─── UDOS-Kopfsektorangaben (Beiblatt zum Ordner) ────────────────────────────
//
// Eine Linux-Datei traegt nur Bytes.  Ein UDOS-Kopfsektor traegt darueber hinaus
// TYP, EIGENSCHAFTEN, STARTADRESSE, SATZLAENGE und — bei Programmen — LADEADRESSE
// und ABBILDLAENGE.  Ohne sie wird beim Zurueckschreiben aus `ZDOS` (P1, Satzlaenge
// 1024, laedt 5521 Byte nach 2600H) eine gewoehnliche Binaerdatei mit 128er Saetzen,
// und die Diskette bootet nicht mehr.  `extractAll` legt deshalb ein Beiblatt an,
// `insertAll` liest es wieder — beide Richtungen ohne Zutun des Anwenders.

constexpr const char* kUdosBeiblatt = "udos-dateiangaben.txt";

struct UdosAngabe {
    std::string typ, eigenschaften, erstellt, geaendert;
    uint16_t    start = 0, satzlaenge = 0, ladeadresse = 0, abbildlaenge = 0;
    uint16_t    speicher_von = 0, speicher_bis = 0, speicher_kz = 0;
    uint16_t    blocklaenge = 0, rest = 0;
    uint32_t    zusatz = 0;
    bool        blocklaenge_gesetzt = false;
};

std::string hex16(uint16_t v) {
    char t[8];
    std::snprintf(t, sizeof t, "%04X", v);
    return t;
}

/// @brief Leerzeichen im Erstellungsvermerk ("V 4.3 ") sind im Beiblatt `_`.
std::string ohneLeer(std::string t) {
    for (char& c : t) if (c == ' ') c = '_';
    return t;
}
std::string mitLeer(std::string t) {
    for (char& c : t) if (c == '_') c = ' ';
    return t;
}

/**
 * @brief Beiblatt schreiben — je Datei eine Zeile aus `schluessel=wert`.
 *
 * Schluessel=Wert statt fester Spalten, weil das Feld fuer Feld erweiterbar ist:
 * ein aelteres Beiblatt bleibt lesbar, ein neueres schadet einem aelteren Werkzeug
 * nicht.  Adressen hexadezimal, Laengen dezimal.
 */
bool schreibeBeiblatt(const fs::path& datei,
                      const std::vector<std::pair<std::string, UdosAngabe>>& eintraege) {
    std::ofstream f(datei, std::ios::binary);
    if (!f) return false;
    f << "# k1520DiskTool — UDOS-Kopfsektorangaben zu den Dateien in diesem Ordner.\n"
         "# Sie stehen NICHT in den Dateien selbst; beim Einfuegen (`put <ordner>`)\n"
         "# werden sie wieder uebernommen.  Ohne sie wird aus einer Systemdatei eine\n"
         "# gewoehnliche Binaerdatei und die Diskette bootet nicht.\n"
         "#\n"
         "#   typ        A | P | P1 | B          eig    Eigenschaften W E L S R F\n"
         "#   start      Startadresse (hex)      satz   Satzlaenge in Byte\n"
         "#   block      zweite Laengenangabe    rest   Bytes im letzten Satz\n"
         "#   segment    ANFANG(hex):LAENGE      mem    LOW:HIGH:STACK (hex, wie EXTRACT)\n"
         "#   zusatz     Kopfsektor 44-47 (hex)  erst/geaend  Vermerke (`_` = Leerzeichen)\n";
    for (const auto& [name, a] : eintraege) {
        f << name
          << " typ=" << (a.typ.empty() ? "-" : a.typ)
          << " eig=" << (a.eigenschaften.empty() ? "-" : a.eigenschaften)
          << " start=" << hex16(a.start)
          << " satz=" << a.satzlaenge
          << " block=" << a.blocklaenge
          << " rest=" << a.rest
          << " segment=" << hex16(a.ladeadresse) << ':' << a.abbildlaenge
          << " mem=" << hex16(a.speicher_von) << ':' << hex16(a.speicher_bis)
          << ':' << hex16(a.speicher_kz)
          << " zusatz=" << std::hex << std::uppercase << a.zusatz << std::dec << std::nouppercase
          << " erst=" << (a.erstellt.empty() ? "-" : ohneLeer(a.erstellt))
          << " geaend=" << (a.geaendert.empty() ? "-" : ohneLeer(a.geaendert))
          << '\n';
    }
    return static_cast<bool>(f);
}

/// @brief Beiblatt lesen; fehlt es, kommt eine leere Tabelle zurueck (kein Fehler).
std::map<std::string, UdosAngabe> leseBeiblatt(const fs::path& datei) {
    std::map<std::string, UdosAngabe> out;
    std::ifstream f(datei);
    if (!f) return out;
    std::string zeile;
    while (std::getline(f, zeile)) {
        if (zeile.empty() || zeile[0] == '#') continue;
        std::istringstream z(zeile);
        std::string name;
        if (!(z >> name)) continue;
        UdosAngabe a;
        std::string feld;
        auto zahl = [](const std::string& t, int basis) -> unsigned long {
            return std::strtoul(t.c_str(), nullptr, basis);
        };
        while (z >> feld) {
            const size_t g = feld.find('=');
            if (g == std::string::npos) continue;
            const std::string k = feld.substr(0, g), v = feld.substr(g + 1);
            if      (k == "typ")    a.typ = (v == "-") ? "" : v;
            else if (k == "eig")    a.eigenschaften = (v == "-") ? "" : v;
            else if (k == "start")  a.start = static_cast<uint16_t>(zahl(v, 16));
            else if (k == "satz")   a.satzlaenge = static_cast<uint16_t>(zahl(v, 10));
            else if (k == "block") { a.blocklaenge = static_cast<uint16_t>(zahl(v, 10));
                                     a.blocklaenge_gesetzt = true; }
            else if (k == "rest")   a.rest = static_cast<uint16_t>(zahl(v, 10));
            else if (k == "segment") {
                const size_t d = v.find(':');
                a.ladeadresse  = static_cast<uint16_t>(zahl(v.substr(0, d), 16));
                if (d != std::string::npos)
                    a.abbildlaenge = static_cast<uint16_t>(zahl(v.substr(d + 1), 10));
            }
            else if (k == "zusatz") a.zusatz = static_cast<uint32_t>(zahl(v, 16));
            else if (k == "erst")   a.erstellt = (v == "-") ? "" : mitLeer(v);
            else if (k == "geaend") a.geaendert = (v == "-") ? "" : mitLeer(v);
            else if (k == "mem") {
                std::string x = v;
                std::replace(x.begin(), x.end(), ':', ' ');
                std::istringstream m(x);
                std::string p1, p2, p3;
                m >> p1 >> p2 >> p3;
                a.speicher_von = static_cast<uint16_t>(zahl(p1, 16));
                a.speicher_bis = static_cast<uint16_t>(zahl(p2, 16));
                a.speicher_kz  = static_cast<uint16_t>(zahl(p3, 16));
            }
        }
        out[name] = a;
    }
    return out;
}

// ─── CP/M-Angaben (Beiblatt zum Ordner) ──────────────────────────────────────
//
// CP/M fuehrt viel weniger als UDOS, aber auch dieses Wenige geht beim Extrahieren
// verloren: der **Nutzerbereich** (aus "3:NAME.TYP" wird die Linux-Datei
// "3_NAME.TYP") und die drei Attributbits R/O, SYS und ARCHIV.  Ohne sie liegt eine
// zurueckgeschriebene Systemdatei im Nutzerbereich 0 und ist im `DIR` sichtbar —
// die Diskette sieht anders aus als vorher.  Gleiches Verfahren wie bei UDOS:
// `extractAll` schreibt, `insertAll` und der Einzel-`insert` lesen.

constexpr const char* kCpmBeiblatt = "cpm-dateiangaben.txt";

struct CpmAngabe {
    std::string name;                ///< der ECHTE CP/M-Name, ggf. "3:NAME.TYP"
    bool read_only = false, system = false, archived = false;
};

std::string cpmAttrText(const CpmAngabe& a) {
    std::string t;
    if (a.read_only) t += 'R';
    if (a.system)    t += 'S';
    if (a.archived)  t += 'A';
    return t.empty() ? "-" : t;
}

/// @brief Beiblatt schreiben — je Datei `<linux-name> name=<cpm-name> attr=RSA`.
bool schreibeCpmBeiblatt(const fs::path& datei,
                         const std::vector<std::pair<std::string, CpmAngabe>>& eintraege) {
    std::ofstream f(datei, std::ios::binary);
    if (!f) return false;
    f << "# k1520DiskTool — CP/M-Angaben zu den Dateien in diesem Ordner.\n"
         "# Sie stehen NICHT in den Dateien selbst; beim Einfuegen (`put <ordner>`)\n"
         "# werden sie wieder uebernommen.\n"
         "#\n"
         "#   name   der echte CP/M-Name mit Nutzerbereich (\"3:NAME.TYP\");\n"
         "#          im Linux-Dateinamen steht dafuer ein Unterstrich\n"
         "#   attr   R = nur lesen   S = Systemdatei (im DIR unsichtbar)\n"
         "#          A = archiviert  -  = keines davon\n";
    for (const auto& [datei_name, a] : eintraege)
        f << datei_name << " name=" << a.name << " attr=" << cpmAttrText(a) << '\n';
    return static_cast<bool>(f);
}

/// @brief Beiblatt lesen; fehlt es, kommt eine leere Tabelle zurueck (kein Fehler).
std::map<std::string, CpmAngabe> leseCpmBeiblatt(const fs::path& datei) {
    std::map<std::string, CpmAngabe> out;
    std::ifstream f(datei);
    if (!f) return out;
    std::string zeile;
    while (std::getline(f, zeile)) {
        if (zeile.empty() || zeile[0] == '#') continue;
        std::istringstream z(zeile);
        std::string datei_name;
        if (!(z >> datei_name)) continue;
        CpmAngabe a;
        std::string feld;
        while (z >> feld) {
            const size_t g = feld.find('=');
            if (g == std::string::npos) continue;
            const std::string k = feld.substr(0, g), v = feld.substr(g + 1);
            if (k == "name") a.name = v;
            else if (k == "attr" && v != "-") {
                a.read_only = v.find('R') != std::string::npos;
                a.system    = v.find('S') != std::string::npos;
                a.archived  = v.find('A') != std::string::npos;
            }
        }
        out[datei_name] = a;
    }
    return out;
}

/// @brief Ist das eines der beiden Beiblaetter?  Sie sind Zubehoer, keine Nutzdatei.
bool istBeiblatt(const fs::path& name) {
    return name == kUdosBeiblatt || name == kCpmBeiblatt;
}

/**
 * @brief Linux-Dateiname → Name auf der Diskette.
 *
 * Bei UDOS unveraendert.  Bei CP/M nennt das Beiblatt den echten Namen samt
 * Nutzerbereich ("3:NAME.TYP") — ohne es bliebe der beim Extrahieren eingesetzte
 * Unterstrich stehen und die Datei landete im Bereich 0.
 */
std::string zielName(const std::string& quelle, FsType typ,
                     const std::map<std::string, CpmAngabe>& beiblatt) {
    const std::string datei = fs::path(quelle).filename().string();
    if (typ == FsType::Udos) return datei;
    const auto it = beiblatt.find(datei);
    if (it != beiblatt.end() && !it->second.name.empty()) return it->second.name;
    return CpmFileSystem::toCpmName(quelle);
}

/// @brief Meldung fuer ein Dateisystem ohne Systemspuren — mit dem Grund.
std::string keineSystemspuren(const FsProfile& p) {
    return "Das Dateisystem '" + p.name + "' hat keine Systemspuren — es beginnt auf "
           "Zylinder 0, dort ist fuer ein Bootabbild kein Platz.";
}

/**
 * @brief Die Auffaelligkeiten eines Treffers als Satz — mit Angabe, WORUEBER er gilt.
 *
 * @param examined  Zahl der angesehenen Spuren; 0 = die ganze Diskette.
 *
 * Bei einer Stichprobe sind die Zaehlungen Aussagen ueber die angesehenen Spuren,
 * nicht ueber die Diskette: „2 Spuren hinter dem Format" heisst dann „2 der
 * 8 angesehenen", und es koennen mehr sein.  Der Satz muss das sagen — sonst liest
 * sich eine Teilmessung wie ein Befund, und ein leeres Ergebnis wie ein Freispruch.
 */
std::string befund(const GeometryMatch& m, int examined) {
    const std::string was = m.remarks();
    if (examined <= 0) return was;               // Vollmessung: der Befund gilt
    const std::string wieviel = std::to_string(examined);
    if (was.empty())
        return "erst " + wieviel + " Spuren angesehen (Stichprobe der Formaterkennung), "
               "die uebrigen sind ungeprueft";
    return "in einer Stichprobe von " + wieviel + " Spuren: " + was
         + " — die uebrigen Spuren sind noch ungeprueft";
}

/// @brief Zwei Befundteile mit Semikolon verbinden; leere Teile fallen weg.
std::string zusammen(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + "; " + b;
}

/// @brief Eine Meldung, die sagt, was zu tun ist — nicht bloss, dass es nicht geht.
constexpr const char* kSchreibschutz =
    "Die Diskette ist schreibgeschuetzt geoeffnet. Zum Aendern den Schreibschutz "
    "aufheben (in der Oberflaeche den Haken \"Nur lesen\" entfernen).";

}  // namespace

// ─── FileRef ─────────────────────────────────────────────────────────────────

FileRef FileRef::parse(const std::string& text, int default_volume) {
    FileRef r;
    r.volume = default_volume;
    r.name   = text;

    const size_t schraeg = text.find('/');
    if (schraeg == std::string::npos) return r;

    const std::string kopf = kleinbuchstaben(text.substr(0, schraeg));
    if (kopf.size() >= 5 && kopf.compare(0, 4, "side") == 0) {
        bool ziffern = true;
        for (size_t i = 4; i < kopf.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(kopf[i]))) ziffern = false;
        if (ziffern) {
            r.volume = std::stoi(kopf.substr(4));
            r.name   = text.substr(schraeg + 1);
        }
    }
    return r;
}

// ─── Aufbau / Abbau ──────────────────────────────────────────────────────────

DiskVolume::~DiskVolume() = default;

std::string DiskVolume::volumeDir(int v) const {
    if (volumes_.size() <= 1) return {};
    return "Side" + std::to_string(v);
}

int DiskVolume::volumeFromDir(const std::string& dir_name) const {
    for (size_t v = 0; v < volumes_.size(); ++v)
        if (kleinbuchstaben(volumeDir(static_cast<int>(v))) == kleinbuchstaben(dir_name))
            return static_cast<int>(v);
    return -1;
}

// Gemeinsamer Kern von @ref DiskVolume::open und @ref DiskVolume::openPhysical.
//
// @p vorhanden ist entweder leer (dann wird @p path geoeffnet) oder ein bereits
// aufgebautes Abbild — bei einer PHYSISCHEN Diskette gibt es keine Datei, die man
// oeffnen koennte.  Alles danach ist identisch: die Erkennung urteilt ueber das
// MEDIUM, nicht ueber die Datei (doc/design/14_physische_diskette.md §11.2).
std::unique_ptr<DiskVolume> DiskVolume::oeffnenMit(std::unique_ptr<DiskImage> vorhanden,
                                                   const std::string& path,
                                                   const std::string& fs_name,
                                                   const FormatCatalog& formats,
                                                   const FsCatalog& fs_cat,
                                                   std::string& err,
                                                   bool read_only,
                                                   bool roh_erlaubt) {
    std::unique_ptr<DiskVolume> dv(new DiskVolume);
    dv->path_      = path;
    dv->read_only_ = read_only;

    // Scheitert die ERKENNUNG, ist die Diskette darum nicht wertlos: man will sie im
    // Sektoreditor ansehen, ihr Abbild sichern oder sie zurechtschneiden.  Mit
    // `roh_erlaubt` kommt sie deshalb OHNE Dateisystem heraus statt gar nicht (§12.6).
    // Der Grund wandert in den Befund — er ist die Auskunft, nicht mehr der Abbruch.
    auto roh = [&](const std::string& grund) -> std::unique_ptr<DiskVolume> {
        if (!roh_erlaubt || !dv->disk_) return nullptr;
        dv->profile_ = nullptr;
        dv->volumes_.clear();
        dv->detection_.filesystem = "";
        dv->detection_.remarks    = zusammen(dv->detection_.remarks, grund);
        return std::move(dv);
    };

    // Eine physische Diskette verhaelt sich wie ein `.hfe`: sie traegt den vollen
    // Spurstrom, also auch alles hinter der Daten-CRC (UDOS-Sektorkontrollblock).
    const std::string ext   = vorhanden ? std::string("hfe") : endung(path);
    const bool        istImg = (ext == "img");

    // ── Fall A': die CP/A-Regel ausdruecklich anfordern ──────────────────────
    // `--fs cpa_auto` heisst „nicht im Katalog nachsehen, rechnen" — die Geometrie
    // wird trotzdem erkannt, denn ohne sie gibt es nichts zu rechnen.
    if (fs_name == CpaDpbRule::kName) {
        std::optional<DiskFormat> of;
        if (istImg) {
            std::error_code ec;
            const uint64_t groesse = fs::file_size(path, ec);
            if (ec) { err = "Datei nicht lesbar: " + path; return nullptr; }
            for (const DiskFormat& f : formats.formats())
                if (f.totalBytes() == groesse) { of = f; break; }
            if (!of) {
                err = "Kein Format in data/formats.yaml hat die Groesse dieses Abbilds ("
                    + std::to_string(groesse) + " Byte).";
                return nullptr;
            }
        }
        dv->disk_ = vorhanden ? std::move(vorhanden) : DiskImage::open(path, of, read_only);
        if (!dv->disk_) { err = "Abbild nicht ladbar: " + path; return nullptr; }
        const std::vector<MeasuredTrack> gemessen =
            GeometryProbe::measure(dv->disk_->medium());   // .img: immer vollstaendig
        const std::vector<GeometryMatch> treffer =
            GeometryProbe::matchAll(gemessen, formats.formats());
        if (treffer.empty()) {
            err = "Das Abbild passt zu keinem Format in data/formats.yaml.\nGemessen:\n"
                + GeometryProbe::describe(gemessen);
            return nullptr;
        }
        FsProfile abgeleitet;
        std::string warum;
        if (!CpaDpbRule::profile(*treffer.front().format,
                                 SectorSpace(dv->disk_->medium(), *treffer.front().format),
                                 abgeleitet, &warum)) {
            err = "Die CP/A-Regel greift hier nicht: " + warum;
            return nullptr;
        }
        dv->abgeleitet_           = abgeleitet;
        dv->profile_              = &*dv->abgeleitet_;
        dv->format_               = treffer.front().format;
        dv->detection_.format     = dv->format_->name;
        dv->detection_.filesystem = dv->profile_->name;
        dv->befund_zusatz_        = abgeleitet.description;
        dv->detection_.remarks    = zusammen(treffer.front().remarks(),
                                             dv->befund_zusatz_);
    } else if (!fs_name.empty()) {
        // ── Fall A: Dateisystem vorgegeben ───────────────────────────────────
        dv->profile_ = fs_cat.find(fs_name);
        if (!dv->profile_) {
            err = "Dateisystem '" + fs_name + "' steht nicht im Katalog";
            return nullptr;
        }
        dv->format_ = formats.find(dv->profile_->format);
        if (!dv->format_) {
            err = "Format '" + dv->profile_->format + "' steht nicht im Katalog";
            return nullptr;
        }
        if (!dv->profile_->allowsContainer(ext)) {
            err = "Dateisystem '" + fs_name + "' kann nicht als ." + ext + " vorliegen"
                + (dv->profile_->type == FsType::Udos && ext == "img"
                       ? " (der UDOS-Sektorkontrollblock steht hinter der Daten-CRC)" : "");
            return nullptr;
        }
        std::optional<DiskFormat> of;
        if (istImg) of = *dv->format_;
        dv->disk_ = vorhanden ? std::move(vorhanden) : DiskImage::open(path, of, read_only);
        if (!dv->disk_) { err = "Abbild nicht ladbar: " + path; return nullptr; }
        dv->detection_.format     = dv->format_->name;
        dv->detection_.filesystem = dv->profile_->name;
    } else {
        // ── Fall B: erkennen ─────────────────────────────────────────────────
        // .hfe/.dmk sind selbstbeschreibend; .img braucht fuer JEDEN Versuch die
        // Geometrie — dort kommen nur Formate passender Dateigroesse in Frage.
        std::vector<const DiskFormat*> kandidaten;
        std::vector<MeasuredTrack>     gemessen;   // leer bei .img (nicht messbar)

        if (istImg) {
            std::error_code ec;
            const uint64_t groesse = fs::file_size(path, ec);
            if (ec) { err = "Datei nicht lesbar: " + path; return nullptr; }
            for (const DiskFormat& f : formats.formats())
                if (f.totalBytes() == groesse) kandidaten.push_back(&f);
            if (kandidaten.empty()) {
                err = "Kein Format in data/formats.yaml hat die Groesse dieses Abbilds ("
                    + std::to_string(groesse) + " Byte).";
                return nullptr;
            }
            // Mit dem ersten Kandidaten oeffnen; die Stufe-2-Probe entscheidet gleich.
            std::optional<DiskFormat> of = *kandidaten.front();
            dv->disk_ = DiskImage::open(path, of, read_only);
        } else {
            dv->disk_ = vorhanden ? std::move(vorhanden)
                                  : DiskImage::open(path, std::nullopt, read_only);
        }
        if (!dv->disk_) { err = "Abbild nicht ladbar: " + path; return nullptr; }

        if (!istImg) {
            // Stufe 1: Geometrie messen und gegen den Katalog abgleichen.
            //
            // Muss jede Spur einzeln vom Original geholt werden (echtes Laufwerk am
            // Greaseweazle: 0,5–0,8 s je Spur), kostet die Vollmessung anderthalb
            // Minuten — fuer eine Frage, die acht Spuren beantworten.  Deshalb dort
            // erst die Stichprobe; erkennt sie nichts, folgt die Vollmessung, denn
            // nur sie traegt `synthesize()` (das braucht das lueckenlose Bild).
            // Bei einer Datei liegt ohnehin alles im Speicher — da wird gemessen.
            const bool teuer = dv->disk_->medium().loader() != nullptr;
            bool stichprobe = false;
            if (teuer) {
                gemessen = GeometryProbe::measureSample(dv->disk_->medium());
                stichprobe = true;
            } else {
                gemessen = GeometryProbe::measure(dv->disk_->medium());
            }
            std::vector<GeometryMatch> treffer =
                GeometryProbe::matchAll(gemessen, formats.formats(), stichprobe);
            if (treffer.empty() && stichprobe) {
                // Die Stichprobe reichte nicht.  Die Vollmessung ist der naechste
                // Schritt — aber nur, wenn sie NICHTS KOSTET: an einem echten
                // Laufwerk zoege sie die ganze Diskette ein (anderthalb Minuten), und
                // am Ende steht womoeglich doch keine Erkennung.  Dann ist es besser,
                // gleich roh zu oeffnen und im Hintergrund weiterzulesen; wer es
                // spaeter noch einmal versucht (@ref redetect), misst dann umsonst
                // voll, weil die Diskette inzwischen im Speicher liegt.
                const DiskMedium& med = dv->disk_->medium();
                const bool teuer_nachzuladen = med.loader() != nullptr && !med.complete();
                if (!teuer_nachzuladen) {
                    // Jetzt gelten wieder ALLE Regeln: `stichprobe` muss false sein,
                    // sonst urteilt die Vollmessung mit den gelockerten Schranken.
                    stichprobe = false;
                    gemessen   = GeometryProbe::measure(med);
                    treffer    = GeometryProbe::matchAll(gemessen, formats.formats());
                }
            }
            if (treffer.empty()) {
                // Letzter Rueckfall: beschreiben, was tatsaechlich draufsteht, und es
                // damit LESBAR machen.  Die Geometrie ist dann geraten — deshalb bleibt
                // die Diskette hart schreibgeschuetzt (siehe unten).
                std::string warum;
                std::optional<DiskFormat> gebaut =
                    GeometryProbe::synthesize(gemessen, &warum);
                if (!gebaut) {
                    err = "Das Abbild passt zu keinem Format in data/formats.yaml ("
                        + warum + ").\nGemessen:\n" + GeometryProbe::describe(gemessen);
                    return roh("kein Format erkannt (" + warum + ")");
                }
                dv->gemessenes_format_    = std::move(*gebaut);
                dv->nur_lesen_erzwungen_  = true;
                kandidaten.push_back(&*dv->gemessenes_format_);
                dv->detection_.remarks =
                    "kein Katalogeintrag passt — als gemessene Geometrie gelesen, "
                    "deshalb schreibgeschuetzt";
            } else {
                for (const GeometryMatch& m : treffer) kandidaten.push_back(m.format);
                dv->detection_.examined_tracks =
                    stichprobe ? static_cast<int>(gemessen.size()) : 0;
                dv->detection_.remarks =
                    befund(treffer.front(), dv->detection_.examined_tracks);
            }
        }

        // Stufe 2: Dateisysteme der Kandidaten positiv nachweisen.
        std::vector<const FsProfile*> passend;
        for (const DiskFormat* f : kandidaten) {
            for (const FsProfile* p : fs_cat.forFormat(f->name)) {
                if (!p->allowsContainer(ext)) continue;

                if (p->type == FsType::Udos) {
                    SectorSpace probe(dv->disk_->medium(), *f);
                    std::string warum;
                    if (!UdosFileSystem::looksLikeUdos(probe, *p, 0, &warum)) continue;
                } else {
                    if (!cpmVerzeichnisPlausibel(dv->disk_->medium(), *f, *p)) continue;
                }
                passend.push_back(p);
                if (!dv->format_) dv->format_ = f;
            }
            if (!passend.empty()) break;      // bestplatzierte Geometrie gewinnt
        }

        // ── Rueckfall: die CP/A-Regel rechnen lassen ─────────────────────────
        // Der Katalog nennt nur die Disketten, die man staendig in der Hand hat.
        // Alles andere leitet dieselbe Regel ab, die das CP/A-BIOS beim LOGIN
        // anwendet (@ref CpaDpbRule) — damit ist jede CP/A-formatierte Diskette
        // lesbar, ohne dass ihr Format im Katalog stehen muss.
        std::string abgelehnt;
        if (passend.empty()) {
            for (const DiskFormat* f : kandidaten) {
                FsProfile abgeleitet;
                SectorSpace raum(dv->disk_->medium(), *f);
                std::string warum, hinweis;
                if (!CpaDpbRule::profile(*f, raum, abgeleitet, &warum)
                    || !abgeleitet.allowsContainer(ext)
                    || !cpmVerzeichnisPlausibel(dv->disk_->medium(), *f, abgeleitet, &warum,
                                                /*uneingerichtet_zaehlt=*/true, &hinweis)) {
                    if (abgelehnt.empty()) abgelehnt = warum;
                    continue;
                }
                dv->abgeleitet_ = abgeleitet;
                dv->format_     = f;
                dv->befund_zusatz_ = abgeleitet.description;
                if (!hinweis.empty()) dv->befund_zusatz_ += "; " + hinweis;
                dv->detection_.remarks =
                    zusammen(dv->detection_.remarks, dv->befund_zusatz_);
                break;
            }
        }

        if (passend.empty() && !dv->abgeleitet_) {
            const std::string dos = dosKennung(dv->disk_->medium(), *kandidaten.front());
            if (dv->gemessenes_format_) {
                // Die Geometrie stand in keinem Katalog UND traegt nichts Lesbares —
                // dann ist die Messung das einzig Brauchbare, was wir sagen koennen.
                err = "Das Abbild passt zu keinem Format in data/formats.yaml, und auf "
                      "der gemessenen Geometrie liegt kein lesbares Dateisystem"
                    + (dos.empty() ? (abgelehnt.empty() ? "" : " (" + abgelehnt + ")")
                                   : " — " + dos)
                    + ".\nGemessen:\n" + GeometryProbe::describe(gemessen);
                return roh("kein Format und kein Dateisystem erkannt");
            }
            err = "Die Geometrie ist erkannt (" + std::string(kandidaten.front()->name)
                + "), aber ";
            if (!dos.empty())
                err += dos + " — dieses Werkzeug liest CP/M und UDOS.";
            else
                err += std::string("kein bekanntes Dateisystem liegt darauf")
                     + (abgelehnt.empty() ? "" : " (" + abgelehnt + ")")
                     + ". Mit --fs laesst sich eines erzwingen.";
            return roh("Geometrie " + std::string(kandidaten.front()->name)
                       + " erkannt, aber kein Dateisystem darauf");
        }

        if (passend.empty()) passend.push_back(&*dv->abgeleitet_);
        dv->profile_ = passend.front();
        dv->detection_.format      = dv->format_->name;
        dv->detection_.filesystem  = dv->profile_->name;
        dv->detection_.unambiguous = (passend.size() == 1);
        for (size_t i = 1; i < passend.size(); ++i)
            dv->detection_.alternatives.push_back(passend[i]->name);
    }

    // ── Volumes aufsetzen ────────────────────────────────────────────────────
    const uint8_t koepfe = dv->disk_->medium().numHeads();

    if (dv->profile_->type == FsType::Udos && dv->profile_->sides_separate) {
        // Jede Seite ist ein eigener Datentraeger (§2).  Eine Seite, die keine
        // gueltige Karte traegt, ist schlicht nicht beschrieben — dann bleibt es
        // bei einem Volume und der Ordner ist flach.
        for (uint8_t h = 0; h < koepfe; ++h) {
            Vol v;
            v.head  = h;
            v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *dv->format_, h);
            std::string warum;
            auto u = UdosFileSystem::mount(*v.space, *dv->profile_, h, warum);
            if (!u) {
                if (h == 0) { err = "Seite 0: " + warum; return roh(warum); }
                break;                       // Seite 1 unbeschrieben → einseitig
            }
            v.fs = std::move(u);
            dv->volumes_.push_back(std::move(v));
        }
    } else {
        Vol v;
        v.head  = SectorSpace::kAllHeads;
        v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *dv->format_);
        std::string warum;
        if (dv->profile_->type == FsType::Udos) {
            auto u = UdosFileSystem::mount(*v.space, *dv->profile_, 0, warum);
            if (!u) { err = warum; return roh(warum); }
            v.fs = std::move(u);
        } else {
            auto c = CpmFileSystem::mount(*v.space, *dv->profile_, warum);
            if (!c) { err = warum; return roh(warum); }
            v.fs = std::move(c);
        }
        dv->volumes_.push_back(std::move(v));
    }

    return dv;
}

std::unique_ptr<DiskVolume> DiskVolume::open(const std::string& path,
                                             const std::string& fs_name,
                                             const FormatCatalog& formats,
                                             const FsCatalog& fs_cat,
                                             std::string& err,
                                             bool read_only) {
    return oeffnenMit(nullptr, path, fs_name, formats, fs_cat, err, read_only);
}

std::unique_ptr<DiskVolume> DiskVolume::openPhysical(std::unique_ptr<DiskImage> disk,
                                                     const std::string& fs_name,
                                                     const FormatCatalog& formats,
                                                     const FsCatalog& fs_cat,
                                                     std::string& err,
                                                     bool read_only,
                                                     bool roh_erlaubt) {
    if (!disk) { err = "kein Abbild uebergeben"; return nullptr; }
    return oeffnenMit(std::move(disk), "", fs_name, formats, fs_cat, err, read_only,
                      roh_erlaubt);
}

// ─── Auskunft ────────────────────────────────────────────────────────────────

FsInfo DiskVolume::volumeInfo(int v) const {
    if (!valid(v)) return {};
    return volumes_[static_cast<size_t>(v)].fs->info();
}

std::vector<FileEntry> DiskVolume::list() const {
    // §9.3: jedes Mal frisch aus dem Medium — kein zwischengespeicherter Stand.
    std::vector<FileEntry> out;
    for (size_t v = 0; v < volumes_.size(); ++v)
        for (FileEntry e : volumes_[v].fs->list()) {
            e.volume = static_cast<int>(v);
            out.push_back(std::move(e));
        }
    return out;
}

bool DiskVolume::refreshDetection() {
    // Nur nach einer Stichprobe, und erst wenn nichts mehr unbekannt ist.  Solange
    // Spuren fehlen, waere die Vollmessung selbst eine Nachladeorgie — genau das,
    // was die Stichprobe vermeiden soll.
    if (detection_.examined_tracks <= 0) return false;
    if (!disk_ || !disk_->medium().complete()) return false;

    if (!format_) return false;
    // Gegen DAS gemountete Format messen, nicht neu suchen: welches es ist, steht
    // fest (das Dateisystem haengt daran).  `match` statt `matchAll` liefert die
    // Zaehlungen auch dann, wenn die volle Diskette knapp nicht mehr passt — sonst
    // verschwaende ein Befund, statt genannt zu werden.
    const std::vector<MeasuredTrack> alle = GeometryProbe::measure(disk_->medium());
    const GeometryMatch voll = GeometryProbe::match(alle, *format_);

    const std::string vorher = detection_.remarks;
    detection_.examined_tracks = 0;
    // Der Zusatz (z. B. „nach der CP/A-Regel abgeleitet …") stammt NICHT aus der
    // Messung; er gilt unabhaengig davon, wie viele Spuren gelesen sind, und bleibt
    // deshalb stehen.  Ohne diese Trennung verloere das Auffrischen ihn.
    detection_.remarks = zusammen(
        voll.ok ? befund(voll, 0)
                : "beim vollstaendigen Lesen zeigte sich mehr, als zum Format passt: "
                  + voll.reason,
        befund_zusatz_);
    return detection_.remarks != vorher;
}

std::vector<FileEntry> DiskVolume::listNames() const {
    std::vector<FileEntry> out;
    for (size_t v = 0; v < volumes_.size(); ++v)
        for (FileEntry e : volumes_[v].fs->listNames()) {
            e.volume = static_cast<int>(v);
            out.push_back(std::move(e));
        }
    return out;
}

bool DiskVolume::detailsReady(const FileEntry& e) const {
    if (!valid(e.volume)) return true;
    return volumes_[static_cast<size_t>(e.volume)].fs->detailsReady(e);
}

bool DiskVolume::loadDetails(FileEntry& e) const {
    if (!valid(e.volume)) return false;
    return volumes_[static_cast<size_t>(e.volume)].fs->loadDetails(e);
}

// ─── Einzeloperationen ───────────────────────────────────────────────────────

bool DiskVolume::extract(const FileRef& ref, const std::string& dest_path,
                         const TransferOptions& opt) {
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    std::vector<uint8_t> d;
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->read(ref.name, d))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());

    if (opt.text) d = nachLinuxText(d);
    if (opt.dry_run) return true;

    std::error_code ec;
    if (!opt.overwrite && fs::exists(dest_path, ec))
        return fail("Zieldatei existiert bereits: " + dest_path);

    std::string err;
    if (!schreibeDatei(dest_path, d, err)) return fail(err);
    return true;
}

bool DiskVolume::insert(const std::string& src_path, const FileRef& ref,
                        const TransferOptions& opt) {
    if (read_only_) return fail(kSchreibschutz);
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    std::vector<uint8_t> d;
    std::string err;
    if (!leseDatei(src_path, d, err)) return fail(err);
    if (opt.text) d = nachDiskettenText(d);
    if (opt.dry_run) return true;

    WriteOptions wo;
    wo.overwrite       = opt.overwrite;
    wo.text            = opt.text;
    // Liegt neben der Quelldatei ein Beiblatt (aus `extractAll`), kommen die
    // Kopfsektorangaben von dort — so behaelt auch eine EINZELN aus der Oberflaeche
    // herübergezogene Systemdatei ihren Typ und ihr Speicherabbild.  Ausdrueckliche
    // Angaben des Aufrufers gehen immer vor.
    TransferOptions mit = opt;
    if (profile_ && profile_->type == FsType::Udos && opt.udos_type.empty()) {
        const fs::path quelle(src_path);
        for (const fs::path& ordner : {quelle.parent_path(),
                                       quelle.parent_path().parent_path()}) {
            if (ordner.empty()) continue;
            const auto tabelle = leseBeiblatt(ordner / kUdosBeiblatt);
            if (tabelle.empty()) continue;
            const std::string kurz = ref.name;
            const std::string lang = volumeDir(ref.volume).empty()
                                   ? kurz : volumeDir(ref.volume) + "/" + kurz;
            auto it = tabelle.find(lang);
            if (it == tabelle.end()) it = tabelle.find(kurz);
            if (it == tabelle.end()) continue;
            mit.udos_type       = it->second.typ;
            mit.udos_properties = it->second.eigenschaften;
            mit.udos_entry      = it->second.start;
            mit.udos_record_len = it->second.satzlaenge;
            mit.udos_segment       = it->second.ladeadresse;
            mit.udos_segment_len  = it->second.abbildlaenge;
            mit.udos_low_addr  = it->second.speicher_von;
            mit.udos_high_addr    = it->second.speicher_bis;
            mit.udos_stack_size  = it->second.speicher_kz;
            mit.udos_block_len  = it->second.blocklaenge;
            mit.udos_bytes_in_last = it->second.rest;
            mit.udos_block_len_gesetzt = it->second.blocklaenge_gesetzt;
            mit.udos_extra      = it->second.zusatz;
            mit.udos_created    = it->second.erstellt;
            mit.udos_modified   = it->second.geaendert;
            break;
        }
    }
    wo.udos_type       = mit.udos_type;
    wo.udos_properties = mit.udos_properties;
    wo.udos_entry      = mit.udos_entry;
    wo.udos_record_len = mit.udos_record_len;
    wo.udos_segment       = mit.udos_segment;
    wo.udos_segment_len  = mit.udos_segment_len;
    wo.udos_low_addr  = mit.udos_low_addr;
    wo.udos_high_addr    = mit.udos_high_addr;
    wo.udos_stack_size  = mit.udos_stack_size;
    wo.udos_block_len  = mit.udos_block_len;
    wo.udos_bytes_in_last = mit.udos_bytes_in_last;
    wo.udos_block_len_gesetzt = mit.udos_block_len_gesetzt;
    wo.udos_extra      = mit.udos_extra;
    wo.udos_created    = mit.udos_created;
    if (!mit.udos_modified.empty()) wo.date = mit.udos_modified;

    // Dasselbe fuer CP/M: die drei Attributbits aus dem Beiblatt neben der Quelle,
    // sofern der Aufrufer keine vorgibt.
    wo.cpm_read_only = opt.cpm_read_only;
    wo.cpm_system    = opt.cpm_system;
    wo.cpm_archived  = opt.cpm_archived;
    if (profile_ && profile_->type == FsType::Cpm
        && !opt.cpm_read_only && !opt.cpm_system && !opt.cpm_archived) {
        const fs::path quelle(src_path);
        for (const fs::path& ordner : {quelle.parent_path(),
                                       quelle.parent_path().parent_path()}) {
            if (ordner.empty()) continue;
            const auto tabelle = leseCpmBeiblatt(ordner / kCpmBeiblatt);
            const auto it = tabelle.find(quelle.filename().string());
            if (it == tabelle.end()) continue;
            wo.cpm_read_only = it->second.read_only;
            wo.cpm_system    = it->second.system;
            wo.cpm_archived  = it->second.archived;
            break;
        }
    }
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->write(ref.name, d, wo))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

bool DiskVolume::setAttributes(const FileRef& ref, const UdosAttrs& attrs) {
    if (read_only_) return fail(kSchreibschutz);
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->setAttributes(ref.name, attrs))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

bool DiskVolume::setAttributes(const FileRef& ref, const CpmAttrs& attrs) {
    if (read_only_) return fail(kSchreibschutz);
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->setAttributes(ref.name, attrs))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

bool DiskVolume::erase(const FileRef& ref) {
    if (read_only_) return fail(kSchreibschutz);
    if (!valid(ref.volume)) return fail("Seite " + std::to_string(ref.volume)
                                        + " gibt es auf dieser Diskette nicht");
    if (!volumes_[static_cast<size_t>(ref.volume)].fs->erase(ref.name))
        return fail(volumes_[static_cast<size_t>(ref.volume)].fs->lastError());
    return true;
}

// ─── Sektoransicht (Diskeditor, §19) ─────────────────────────────────────────

uint8_t DiskVolume::mediumCylinders() const {
    return disk_ ? disk_->medium().numCylinders() : 0;
}

uint8_t DiskVolume::mediumHeads() const {
    return disk_ ? disk_->medium().numHeads() : 0;
}

std::unique_ptr<DiskImage> DiskVolume::releaseImage() {
    // Erst die Dateisysteme fallenlassen: sie zeigen auf Sektorraeume ueber DIESEM
    // Medium.  Ein Volume ohne Abbild ist danach nur noch zu zerstoeren.
    volumes_.clear();
    profile_ = nullptr;
    return std::move(disk_);
}

int DiskVolume::schneide(bool nur_gerade_spuren, bool nur_seite_null) {
    if (!disk_) { fail("keine Diskette geoeffnet"); return -1; }
    DiskMedium& m = disk_->medium();
    const uint8_t alt_c = m.numCylinders(), alt_h = m.numHeads();

    if (nur_gerade_spuren && alt_c < 2) { fail("zu wenige Spuren"); return -1; }
    if (nur_seite_null && alt_h < 2)    { fail("die Diskette hat nur eine Seite"); return -1; }

    // **Nur an einem vollstaendigen Abbild.**  Der Schnitt loest vom Laufwerk (s. u.);
    // was jetzt noch ungelesen ist, bleibt es fuer immer.  Und die verbliebenen
    // Luecken machen die Erkennung unmoeglich — eine unbekannte Spur sieht aus wie
    // eine unformatierte, mitten im beschriebenen Bereich.
    if (!m.complete()) {
        fail("Es sind noch " + std::to_string(m.unknownCount())
             + " Spuren ungelesen.  Der Schnitt loest das Abbild vom Laufwerk — was "
               "jetzt fehlt, fehlt dann endgueltig.  Erst vollstaendig einlesen.");
        return -1;
    }

    // **Erst vom Laufwerk loesen.**  Nach dem Schnitt stimmt die Spurnummer nicht
    // mehr mit der Kopfposition ueberein (Spur n liegt physisch auf 2n); ein
    // Rueckschreiben ginge auf die falschen Zylinder und zerstoerte die Diskette.
    // Das Abbild bleibt vollstaendig im Speicher — nur die Bindung endet.
    m.setLoader(nullptr);

    const uint8_t neu_c = nur_gerade_spuren ? static_cast<uint8_t>((alt_c + 1) / 2) : alt_c;
    const uint8_t neu_h = nur_seite_null ? 1 : alt_h;

    if (nur_gerade_spuren) {
        // Von vorn nach hinten schieben: Ziel n liegt immer VOR der Quelle 2n, es
        // wird also nichts ueberschrieben, was noch gebraucht wird.
        for (uint8_t c = 0; c < neu_c; ++c) {
            const uint8_t quelle = static_cast<uint8_t>(c * 2);
            if (quelle >= alt_c) break;
            for (uint8_t h = 0; h < alt_h; ++h)
                if (quelle != c) m.setTrack(c, h, m.peek(quelle, h));
        }
    }
    m.resize(neu_c, neu_h);
    return static_cast<int>(neu_c) * neu_h;
}

int DiskVolume::deleteCylinder(uint8_t cyl) {
    if (!disk_) { fail("keine Diskette geoeffnet"); return -1; }
    DiskMedium& m = disk_->medium();
    if (cyl >= m.numCylinders()) { fail("diesen Zylinder gibt es nicht"); return -1; }
    if (m.numCylinders() < 2)    { fail("die letzte Spur bleibt"); return -1; }
    if (!m.complete()) {
        fail("Es sind noch Spuren ungelesen — erst vollstaendig einlesen.");
        return -1;
    }
    m.setLoader(nullptr);                    // Lage aendert sich, s. @ref schneide

    // Nach vorn schieben: Ziel liegt immer VOR der Quelle, es geht nichts verloren.
    for (uint8_t c = cyl; c + 1 < m.numCylinders(); ++c)
        for (uint8_t h = 0; h < m.numHeads(); ++h)
            m.setTrack(c, h, m.peek(static_cast<uint8_t>(c + 1), h));
    m.resize(static_cast<uint8_t>(m.numCylinders() - 1), m.numHeads());
    return m.numCylinders();
}

int DiskVolume::insertCylinderAfter(uint8_t cyl) {
    if (!disk_) { fail("keine Diskette geoeffnet"); return -1; }
    DiskMedium& m = disk_->medium();
    if (cyl >= m.numCylinders()) { fail("diesen Zylinder gibt es nicht"); return -1; }
    if (m.numCylinders() >= 255) { fail("mehr Zylinder gehen nicht"); return -1; }
    if (!m.complete()) {
        fail("Es sind noch Spuren ungelesen — erst vollstaendig einlesen.");
        return -1;
    }
    m.setLoader(nullptr);

    const uint8_t alt = m.numCylinders();
    m.resize(static_cast<uint8_t>(alt + 1), m.numHeads());
    // Von HINTEN nach vorn schieben — sonst ueberschreibt man, was noch kommt.
    for (int c = alt; c > cyl + 1; --c)
        for (uint8_t h = 0; h < m.numHeads(); ++h)
            m.setTrack(static_cast<uint8_t>(c), h,
                       m.peek(static_cast<uint8_t>(c - 1), h));
    // Der neue Zylinder ist UNFORMATIERT — aber nicht LEER.  Der Unterschied ist
    // entscheidend: eine Spur ohne Bytes gibt es in dieser Geometrie gar nicht
    // (`TrackView::exists == false`), und in eine solche laesst sich kein Sektor
    // legen.  Eine geloeschte echte Spur traegt Fluss, nur ohne Marken — genau das
    // wird hier gebaut: Gap-Fuellbytes in Laenge und Verfahren des NACHBARN, damit
    // die neue Spur in dieselbe Umdrehung passt.  Sektoren lassen sich darin dann
    // einzeln anlegen: von Hand formatieren (§19.6).
    for (uint8_t h = 0; h < m.numHeads(); ++h) {
        const TrackImage& vorbild = m.peek(cyl, h);
        TrackImage neu;
        neu.encoding = vorbild.encoding;
        neu.bitcells = vorbild.bitcells;
        const size_t laenge = vorbild.bytes.empty() ? 6250 : vorbild.bytes.size();
        neu.bytes.assign(laenge, vorbild.encoding == Encoding::FM ? 0xFF : 0x4E);
        neu.marks.assign(laenge, MarkType::None);
        m.setTrack(static_cast<uint8_t>(cyl + 1), h, std::move(neu));
    }
    return m.numCylinders();
}

int DiskVolume::keepEvenTracks()  { return schneide(true, false); }
int DiskVolume::dropSecondSide()  { return schneide(false, true); }

int DiskVolume::copyTo(DiskMedium& ziel) const {
    if (!disk_) { fail("keine Diskette geoeffnet"); return -1; }
    const DiskMedium& quelle = disk_->medium();

    // Zu wenige SPUREN sind ein Abbruch: die Diskette passt schlicht nicht hin.
    if (quelle.numCylinders() > ziel.numCylinders()) {
        fail("Die Diskette hat " + std::to_string(quelle.numCylinders())
             + " Spuren, das Ziel nur " + std::to_string(ziel.numCylinders()) + ".");
        return -1;
    }
    // Zu wenige SEITEN dagegen nicht: „nur Seite 0" ist eine gewollte Einstellung
    // (§12.5) — etwa wenn auf Seite 1 nur Altbestand steht.  Kopiert wird dann, was
    // hineinpasst.  Dass dabei etwas wegbleibt, sagt die Oberflaeche VORHER; hier
    // waere die Rueckfrage zu spaet und am falschen Ort.
    const uint8_t koepfe = std::min(quelle.numHeads(), ziel.numHeads());

    int n = 0;
    for (uint8_t c = 0; c < quelle.numCylinders(); ++c)
        for (uint8_t h = 0; h < koepfe; ++h) {
            // Eine nie gelesene Spur der QUELLE traegt bedeutungslose Bytes — das gibt
            // es nur, wenn die Quelle selbst ein Laufwerk ist.  Sie zu kopieren
            // schriebe Muell auf die Zieldiskette.  `peek` laedt bewusst nicht nach.
            if (quelle.state(c, h) == TrackState::Unknown) continue;
            ziel.setTrack(c, h, quelle.peek(c, h));
            ++n;
        }
    return n;
}

TrackView DiskVolume::trackView(uint8_t cyl, uint8_t head) const {
    if (!disk_) return TrackView{};
    return scanTrack(disk_->medium().track(cyl, head));
}

int DiskVolume::trackState(uint8_t cyl, uint8_t head) const {
    if (!disk_) return 0;
    switch (disk_->medium().state(cyl, head)) {
        case TrackState::Clean: return 1;
        case TrackState::Dirty: return 2;
        default:                return 0;
    }
}

bool DiskVolume::readSectorAt(uint8_t cyl, uint8_t head, int index,
                              std::vector<uint8_t>& out, uint16_t& crc) const {
    if (!disk_) return fail("keine Diskette geoeffnet");
    const std::vector<LogicalSector> s =
        TrackCodec::parseTrack(disk_->medium().track(cyl, head));
    if (index < 0 || static_cast<size_t>(index) >= s.size())
        return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");
    out = s[static_cast<size_t>(index)].data;
    crc = s[static_cast<size_t>(index)].data_crc;
    return true;
}

bool DiskVolume::sectorCrcFor(uint8_t cyl, uint8_t head, int index,
                              const std::vector<uint8_t>& data, uint16_t& out) const {
    if (!disk_) return fail("keine Diskette geoeffnet");
    if (index < 0) return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");
    if (!TrackCodec::sectorDataCrc(disk_->medium().track(cyl, head),
                                   static_cast<size_t>(index), data, out))
        return fail("Die Laenge passt nicht zur Sektorgroesse");
    return true;
}

bool DiskVolume::writeSectorAt(uint8_t cyl, uint8_t head, int index,
                               const std::vector<uint8_t>& data,
                               const uint16_t* crc_woertlich) {
    if (read_only_) return fail(kSchreibschutz);
    if (!disk_)     return fail("keine Diskette geoeffnet");
    if (index < 0)  return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");

    // Auf einer Kopie arbeiten: `writeSectorAt` prueft zwar zweiphasig, aber so bleibt
    // die Spur auch dann unangetastet, wenn spaeter noch etwas dazukommt.
    TrackImage spur = disk_->medium().track(cyl, head);
    if (!TrackCodec::writeSectorAt(spur, static_cast<size_t>(index), data, {},
                                   crc_woertlich))
        return fail("Der Sektor liess sich nicht schreiben — Laenge oder Lage passt "
                    "nicht (erwartet wird genau die Sektorgroesse).");

    disk_->medium().setTrack(cyl, head, std::move(spur));
    return true;
}

bool DiskVolume::readSectorTail(uint8_t cyl, uint8_t head, int index,
                                std::vector<uint8_t>& out) const {
    if (!disk_) return fail("keine Diskette geoeffnet");
    const std::vector<LogicalSector> s =
        TrackCodec::parseTrack(disk_->medium().track(cyl, head));
    if (index < 0 || static_cast<size_t>(index) >= s.size())
        return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");
    out = s[static_cast<size_t>(index)].tail;
    return true;
}

bool DiskVolume::writeSectorTail(uint8_t cyl, uint8_t head, int index,
                                 const std::vector<uint8_t>& tail) {
    if (read_only_) return fail(kSchreibschutz);
    if (!disk_)     return fail("keine Diskette geoeffnet");
    if (index < 0)  return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");
    if (tail.size() > kSectorTailBytes)
        return fail("Der Nachspann fasst hoechstens "
                    + std::to_string(kSectorTailBytes) + " Byte");

    TrackImage spur = disk_->medium().track(cyl, head);
    const std::vector<LogicalSector> s = TrackCodec::parseTrack(spur);
    if (static_cast<size_t>(index) >= s.size())
        return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");

    // Daten und CRC unveraendert zurueckschreiben: `writeSectorAt` fasst beides an,
    // und eine neu gerechnete CRC wuerde einen absichtlich defekten Sektor heilen.
    const uint16_t crc = s[static_cast<size_t>(index)].data_crc;
    if (!TrackCodec::writeSectorAt(spur, static_cast<size_t>(index),
                                   s[static_cast<size_t>(index)].data, tail, &crc))
        return fail("Der Nachspann liess sich nicht schreiben — er wuerde in die "
                    "naechste Adressmarke laufen.");
    disk_->medium().setTrack(cyl, head, std::move(spur));
    return true;
}

bool DiskVolume::eraseSectorAt(uint8_t cyl, uint8_t head, int index,
                               uint16_t tail_bytes) {
    if (read_only_) return fail(kSchreibschutz);
    if (!disk_)     return fail("keine Diskette geoeffnet");
    if (index < 0)  return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");

    TrackImage spur = disk_->medium().track(cyl, head);
    if (!TrackCodec::eraseSectorAt(spur, static_cast<size_t>(index), tail_bytes))
        return fail("Diesen Sektor gibt es auf der Spur nicht (mehr)");
    disk_->medium().setTrack(cyl, head, std::move(spur));
    return true;
}

bool DiskVolume::createSector(uint8_t cyl, uint8_t head, const TrackCodec::NewSectorSpec& spec,
                              bool mfm) {
    if (read_only_) return fail(kSchreibschutz);
    if (!disk_)     return fail("keine Diskette geoeffnet");

    TrackImage spur = disk_->medium().track(cyl, head);
    std::string warum;
    if (!TrackCodec::createSector(spur, spec, mfm ? Encoding::MFM : Encoding::FM,
                                  &warum))
        return fail(warum);
    disk_->medium().setTrack(cyl, head, std::move(spur));
    return true;
}

bool DiskVolume::planSector(uint8_t cyl, uint8_t head, const TrackCodec::NewSectorSpec& spec,
                            bool mfm, uint32_t& von, uint32_t& laenge) const {
    if (!disk_) return fail("keine Diskette geoeffnet");
    const TrackImage& spur = disk_->medium().track(cyl, head);
    if (spur.bytes.empty())
        return fail("Diese Spur gibt es auf dem Datentraeger nicht.");
    von = static_cast<uint32_t>(
        TrackCodec::newSectorPosition(spur, spec.id, spec.gap_before));
    laenge = static_cast<uint32_t>(TrackCodec::newSectorLength(
        mfm ? Encoding::MFM : Encoding::FM, spec.size, spec.tail_bytes));
    return true;
}

// ─── Stapeloperationen ───────────────────────────────────────────────────────

bool DiskVolume::extractAll(const std::string& dest_dir, const TransferOptions& opt) {
    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) return fail("Zielordner nicht anlegbar: " + dest_dir);

    for (int v = 0; v < volumeCount(); ++v) {
        const std::string unter = volumeDir(v);
        const fs::path    ziel  = unter.empty() ? fs::path(dest_dir)
                                                : fs::path(dest_dir) / unter;
        // Auch eine LEERE Seite bekommt ihren Ordner — das ist die ehrliche Auskunft.
        fs::create_directories(ziel, ec);
        if (ec) return fail("Ordner nicht anlegbar: " + ziel.string());

        for (const FileEntry& e : volumes_[static_cast<size_t>(v)].fs->list()) {
            // Die UDOS-Verzeichnisdatei (Typ D) ist Dateisystemstruktur, keine
            // Nutzdatei — sie wird mit ausgegeben (wie `CAT P=&`), aber nicht
            // extrahiert: beim Zurueckschreiben waere sie ein Fremdkoerper.
            if (e.type == "D") continue;
            FileRef r{v, e.qualifiedName()};
            // Nutzerbereich-Praefix ("3:NAME") ist im Dateinamen unbrauchbar.
            std::string datei = e.qualifiedName();
            std::replace(datei.begin(), datei.end(), ':', '_');
            if (!extract(r, (ziel / datei).string(), opt)) return false;
        }
    }

    // Beiblatt mit den Kopfsektorangaben — nur UDOS fuehrt welche.
    if (profile_ && profile_->type == FsType::Udos) {
        std::vector<std::pair<std::string, UdosAngabe>> eintraege;
        for (int v = 0; v < volumeCount(); ++v) {
            const std::string unter = volumeDir(v);
            for (const FileEntry& e : volumes_[static_cast<size_t>(v)].fs->list()) {
                if (e.type == "D") continue;
                UdosAngabe a;
                a.typ           = e.type;
                a.eigenschaften = e.attributes;
                a.start         = e.entry_addr;
                a.satzlaenge    = e.record_len;
                a.ladeadresse   = e.segment_start;
                a.abbildlaenge  = e.segment_len;
                a.speicher_von  = e.low_addr;
                a.speicher_bis  = e.high_addr;
                a.speicher_kz   = e.stack_size;
                a.blocklaenge   = e.block_len;
                a.rest          = e.bytes_in_last;
                a.blocklaenge_gesetzt = true;
                a.zusatz        = e.extra;
                a.erstellt      = e.created;
                a.geaendert     = e.date;
                eintraege.emplace_back(unter.empty() ? e.name : unter + "/" + e.name, a);
            }
        }
        if (!eintraege.empty()
            && !schreibeBeiblatt(fs::path(dest_dir) / kUdosBeiblatt, eintraege))
            return fail("Beiblatt nicht schreibbar: "
                        + (fs::path(dest_dir) / kUdosBeiblatt).string());
    }

    // Dasselbe fuer CP/M — dort sind es Nutzerbereich und die drei Attributbits.
    if (profile_ && profile_->type == FsType::Cpm) {
        std::vector<std::pair<std::string, CpmAngabe>> eintraege;
        for (const FileEntry& e : volumes_[0].fs->list()) {
            CpmAngabe a;
            a.name      = e.qualifiedName();
            a.read_only = e.attributes.find("RO")  != std::string::npos;
            a.system    = e.attributes.find("SYS") != std::string::npos;
            a.archived  = e.attributes.find("ARC") != std::string::npos;
            // Nur was von der Vorgabe abweicht, ist ueberhaupt eine Angabe wert;
            // eine gewoehnliche Datei im Bereich 0 braucht keine Zeile.
            if (a.name == e.name && !a.read_only && !a.system && !a.archived) continue;
            std::string datei = e.qualifiedName();
            std::replace(datei.begin(), datei.end(), ':', '_');
            eintraege.emplace_back(datei, a);
        }
        if (!eintraege.empty()
            && !schreibeCpmBeiblatt(fs::path(dest_dir) / kCpmBeiblatt, eintraege))
            return fail("Beiblatt nicht schreibbar: "
                        + (fs::path(dest_dir) / kCpmBeiblatt).string());
    }
    return true;
}

bool DiskVolume::sammleQuelldateien(const std::string& src_dir,
                                    std::vector<std::vector<std::string>>& je_volume) const {
    je_volume.assign(volumes_.size(), {});

    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) return fail("Kein Ordner: " + src_dir);

    if (volumes_.size() == 1) {
        for (const auto& e : fs::directory_iterator(src_dir, ec)) {
            if (e.is_directory()) continue;         // Unterordner kennt CP/M nicht
            if (istBeiblatt(e.path().filename())) continue;       // Beiblatt, keine Datei
            je_volume[0].push_back(e.path().string());
        }
        return true;
    }

    // Mehrere Seiten: der Ordner MUSS genau die SideN/ tragen (§9.1).
    std::vector<std::string> fehlend, lose;
    for (int v = 0; v < volumeCount(); ++v)
        if (!fs::is_directory(fs::path(src_dir) / volumeDir(v), ec))
            fehlend.push_back(volumeDir(v));

    for (const auto& e : fs::directory_iterator(src_dir, ec)) {
        if (!e.is_directory()) {
            // Das Beiblatt gehoert dorthin und ist keine „lose Datei".
            if (!istBeiblatt(e.path().filename()))
                lose.push_back(e.path().filename().string());
            continue;
        }
        if (volumeFromDir(e.path().filename().string()) < 0)
            lose.push_back(e.path().filename().string() + "/");
    }

    if (!fehlend.empty()) {
        std::string liste;
        for (int v = 0; v < volumeCount(); ++v) {
            if (!liste.empty()) liste += " und ";
            liste += volumeDir(v) + "/";
        }
        std::string fehlt;
        for (const auto& f : fehlend) { if (!fehlt.empty()) fehlt += ", "; fehlt += f + "/"; }
        return fail("Der Ordner " + src_dir + " muss die Unterverzeichnisse " + liste
                    + " enthalten (die Diskette hat " + std::to_string(volumeCount())
                    + " Seiten). Es fehlt: " + fehlt);
    }
    if (!lose.empty()) {
        std::string namen;
        for (size_t i = 0; i < lose.size() && i < 5; ++i) {
            if (!namen.empty()) namen += ", ";
            namen += lose[i];
        }
        if (lose.size() > 5) namen += ", …";
        return fail("Neben den SideN-Ordnern liegen weitere Eintraege in " + src_dir
                    + " (" + namen + "). Auf welche Seite sie gehoeren, ist nicht "
                      "erkennbar — bitte einsortieren.");
    }

    for (int v = 0; v < volumeCount(); ++v) {
        const fs::path unter = fs::path(src_dir) / volumeDir(v);
        for (const auto& e : fs::directory_iterator(unter, ec)) {
            if (e.is_directory())
                return fail("Unterverzeichnisse kennt das Dateisystem nicht: "
                            + e.path().string());
            je_volume[static_cast<size_t>(v)].push_back(e.path().string());
        }
    }
    return true;
}

bool DiskVolume::checkFit(const std::string& src_dir, std::string& bericht) const {
    bericht.clear();
    std::vector<std::vector<std::string>> je_volume;
    if (!sammleQuelldateien(src_dir, je_volume)) { bericht = last_error_; return false; }

    const std::map<std::string, CpmAngabe> cpm_beiblatt =
        leseCpmBeiblatt(fs::path(src_dir) / kCpmBeiblatt);

    bool passt = true;
    for (size_t v = 0; v < volumes_.size(); ++v) {
        std::vector<PlannedFile> plan;
        for (const std::string& p : je_volume[v]) {
            std::error_code ec;
            PlannedFile f;
            f.name   = zielName(p, profile_->type, cpm_beiblatt);
            f.size   = fs::file_size(p, ec);
            f.volume = static_cast<int>(v);
            plan.push_back(f);
        }
        FitReport r;
        if (!volumes_[v].fs->wouldFit(plan, r)) { bericht = volumes_[v].fs->lastError();
                                                  return false; }
        if (!r.fits) {
            passt = false;
            const std::string wo = volumeDir(static_cast<int>(v));
            bericht += (wo.empty() ? std::string("Diskette") : "Seite " + std::to_string(v))
                     + ": " + r.detail + "\n";
        }
    }
    if (passt) bericht = "passt";
    return passt;
}

bool DiskVolume::insertAll(const std::string& src_dir, const TransferOptions& opt) {
    if (read_only_) return fail(kSchreibschutz);
    std::vector<std::vector<std::string>> je_volume;
    if (!sammleQuelldateien(src_dir, je_volume)) return false;

    // Schritt 2: urteilen — VOR der ersten Aenderung.
    std::string bericht;
    if (!checkFit(src_dir, bericht))
        return fail(bericht + "Es wurde nichts geschrieben.");
    if (opt.dry_run) return true;

    // Schritt 3: ausfuehren, mit Ruecknahme.  Die Momentaufnahme des Mediums kostet
    // ~1 MB je Diskette — billig gegenueber einer halb beschriebenen Diskette.
    const DiskMedium sicherung = disk_->medium();
    const std::map<std::string, UdosAngabe> beiblatt =
        leseBeiblatt(fs::path(src_dir) / kUdosBeiblatt);
    const std::map<std::string, CpmAngabe> cpm_beiblatt =
        leseCpmBeiblatt(fs::path(src_dir) / kCpmBeiblatt);

    for (size_t v = 0; v < volumes_.size(); ++v) {
        for (const std::string& quelle : je_volume[v]) {
            const std::string name = zielName(quelle, profile_->type, cpm_beiblatt);
            TransferOptions o = opt;
            o.overwrite = true;          // im Stapel ersetzt der Ordner den Bestand
            // Kopfsektorangaben aus dem Beiblatt, sofern der Aufrufer keine vorgibt.
            const std::string schluessel = volumes_.size() == 1
                                         ? name : volumeDir(static_cast<int>(v)) + "/" + name;
            const auto it = beiblatt.find(schluessel);
            if (it != beiblatt.end() && o.udos_type.empty()) {
                o.udos_type       = it->second.typ;
                o.udos_properties = it->second.eigenschaften;
                o.udos_entry      = it->second.start;
                o.udos_record_len = it->second.satzlaenge;
                o.udos_segment       = it->second.ladeadresse;
                o.udos_segment_len  = it->second.abbildlaenge;
                o.udos_low_addr  = it->second.speicher_von;
                o.udos_high_addr    = it->second.speicher_bis;
                o.udos_stack_size  = it->second.speicher_kz;
                o.udos_block_len  = it->second.blocklaenge;
                o.udos_bytes_in_last = it->second.rest;
                o.udos_block_len_gesetzt = it->second.blocklaenge_gesetzt;
                o.udos_extra      = it->second.zusatz;
                o.udos_created    = it->second.erstellt;
                o.udos_modified   = it->second.geaendert;
            }
            if (!insert(quelle, FileRef{static_cast<int>(v), name}, o)) {
                const std::string grund = last_error_;
                disk_->medium() = sicherung;         // Ruecknahme
                return fail(grund + " — die Diskette wurde nicht veraendert.");
            }
        }
    }
    return true;
}

// ─── Dateibindung ────────────────────────────────────────────────────────────

bool DiskVolume::dirty() const { return disk_ && disk_->medium().dirty(); }

void DiskVolume::setReadOnly(bool ro) {
    // Eine nur GEMESSENE Geometrie laesst sich nicht freigeben: wir haben sie geraten,
    // nicht belegt.  Ein Schreibvorgang wuerde beim geringsten Irrtum an der falschen
    // Stelle landen — und die Diskette ist in aller Regel ein Einzelstueck.
    if (!ro && nur_lesen_erzwungen_) {
        last_error_ = "Die Geometrie dieser Diskette steht in keinem Katalog und wurde "
                      "nur gemessen — Schreiben ist gesperrt. Erst einen passenden "
                      "Eintrag in data/formats.yaml anlegen (die Meldung von `measure` "
                      "taugt als Vorlage).";
        return;
    }
    read_only_ = ro;
    // Zweite Sperre eine Ebene tiefer: ein schreibgeschuetztes DiskImage schreibt
    // seine Datei auch dann nicht, wenn hier etwas durchrutscht — einschliesslich
    // des flush() aus seinem Destruktor.
    if (disk_) disk_->setWriteProtect(ro);
}

bool DiskVolume::exportImage(const std::string& path) const {
    if (!disk_) return fail("keine Diskette geoeffnet");
    std::optional<DiskFormat> of;
    if (endung(path) == "img") {
        if (profile_ && !profile_->allow_img)
            return fail("Dieses Dateisystem kann nicht als .img abgelegt werden "
                        "(der UDOS-Sektorkontrollblock steht hinter der Daten-CRC).");
        if (format_) of = *format_;
    }
    if (!disk_->exportTo(path, of)) return fail(disk_->lastError());
    return true;
}

bool DiskVolume::flush() {
    if (!disk_) return fail("keine Diskette geoeffnet");
    if (read_only_) {
        if (!dirty()) return true;          // nichts zu tun — kein Grund zu klagen
        return fail(kSchreibschutz);
    }

    // §14.2: fremde Diskettenabbilder sind oft Einzelstuecke — beim ERSTEN
    // Zurueckschreiben eine Sicherungskopie anlegen.
    if (backup_ && !backup_getan_ && disk_->medium().dirty()) {
        std::error_code ec;
        if (fs::exists(path_, ec)) {
            fs::copy_file(path_, path_ + "~", fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("Sicherungskopie " + path_ + "~ nicht anlegbar: "
                                + ec.message());
        }
        backup_getan_ = true;
    }

    if (!disk_->flush()) return fail(disk_->lastError());
    return true;
}

std::unique_ptr<DiskVolume> DiskVolume::create(const std::string& path,
                                               const std::string& fs_name,
                                               const std::string& label,
                                               const FormatCatalog& formats,
                                               const FsCatalog& fs_cat,
                                               std::string& err,
                                               const std::string& boot_image) {
    const FsProfile* profil = fs_cat.find(fs_name);
    if (!profil) { err = "Dateisystem '" + fs_name + "' steht nicht im Katalog"; return nullptr; }
    const DiskFormat* fmt = formats.find(profil->format);
    if (!fmt) { err = "Format '" + profil->format + "' steht nicht im Katalog"; return nullptr; }

    const std::string ext = endung(path);
    if (!profil->allowsContainer(ext)) {
        err = "Dateisystem '" + fs_name + "' kann nicht als ." + ext + " angelegt werden"
            + (profil->type == FsType::Udos && ext == "img"
                   ? " (der UDOS-Sektorkontrollblock steht hinter der Daten-CRC)" : "");
        return nullptr;
    }

    // Das Bootabbild wird gelesen und BEURTEILT, bevor irgendetwas angelegt wird —
    // ein zu grosses Abbild soll keine halbe Diskette hinterlassen.
    std::vector<uint8_t> boot;
    if (!boot_image.empty()) {
        if (!leseDatei(boot_image, boot, err)) return nullptr;
        if (boot.empty()) { err = "Bootabbild ist leer: " + boot_image; return nullptr; }
        const uint64_t platz = bootAreaCapacity(*profil, *fmt);
        if (platz == 0) { err = keineSystemspuren(*profil); return nullptr; }
        if (boot.size() > platz) {
            err = "Das Bootabbild ist " + std::to_string(boot.size())
                + " Byte gross, die Systemspuren von '" + profil->name + "' fassen aber nur "
                + std::to_string(platz) + " Byte.";
            return nullptr;
        }
    }

    // Vollstaendig formatierte Leerdiskette (echte Adressmarken und CRCs) …
    {
        auto leer = DiskImage::create(path, *fmt, /*write_protect=*/false,
                                      fmt->predominantEncoding());
        if (!leer) { err = "Abbild nicht anlegbar: " + path; return nullptr; }
        if (!leer->flush()) { err = leer->lastError(); return nullptr; }
    }

    std::unique_ptr<DiskVolume> dv(new DiskVolume);
    dv->path_    = path;
    dv->format_  = fmt;
    dv->profile_ = profil;
    // Eine gerade selbst angelegte Diskette ist beschreibbar — der Schreibschutz
    // schuetzt FREMDE Abbilder beim Lesen, nicht das eigene frische Werkstueck.
    dv->read_only_ = false;
    dv->detection_.format     = fmt->name;
    dv->detection_.filesystem = profil->name;

    std::optional<DiskFormat> of;
    if (ext == "img") of = *fmt;
    dv->disk_ = DiskImage::open(path, of, false);
    if (!dv->disk_) { err = "frisch angelegtes Abbild nicht ladbar: " + path; return nullptr; }

    // … und darauf das Dateisystem initialisieren.
    const uint8_t koepfe = dv->disk_->medium().numHeads();
    if (profil->type == FsType::Udos && profil->sides_separate) {
        // Beide Seiten sind eigene Datentraeger und bekommen je ein Dateisystem.
        for (uint8_t h = 0; h < koepfe; ++h) {
            Vol v;
            v.head  = h;
            v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *fmt, h);
            auto u = UdosFileSystem::format(*v.space, *profil, h, label, err);
            if (!u) return nullptr;
            v.fs = std::move(u);
            dv->volumes_.push_back(std::move(v));
        }
    } else {
        Vol v;
        v.head  = SectorSpace::kAllHeads;
        v.space = std::make_unique<SectorSpace>(dv->disk_->medium(), *fmt);
        if (profil->type == FsType::Udos) {
            auto u = UdosFileSystem::format(*v.space, *profil, 0, label, err);
            if (!u) return nullptr;
            v.fs = std::move(u);
        } else {
            auto c = CpmFileSystem::mount(*v.space, *profil, err);
            if (!c) return nullptr;
            if (!c->mkfs()) { err = c->lastError(); return nullptr; }
            v.fs = std::move(c);
        }
        dv->volumes_.push_back(std::move(v));
    }

    // … zuletzt das Bootabbild in die Systemspuren (geprueft ist es oben schon).
    if (!boot.empty() && !dv->writeBootImage(boot)) { err = dv->lastError(); return nullptr; }

    // Eine frisch angelegte Diskette braucht keine Sicherungskopie ihrer selbst.
    dv->backup_getan_ = true;
    if (!dv->flush()) { err = dv->lastError(); return nullptr; }
    return dv;
}

// ─── Bootabbild (Systemspuren) ───────────────────────────────────────────────

uint64_t DiskVolume::bootAreaCapacity(const FsProfile& prof, const DiskFormat& fmt) {
    // Der lineare Aufbau haengt allein am Format — ein leeres Medium in dessen
    // Geometrie genuegt, um die Spurlaengen zu addieren.
    DiskMedium leer(fmt.numCylinders(), fmt.numHeads(), fmt.predominantEncoding());
    const uint8_t filter = (prof.type == FsType::Udos && prof.sides_separate)
                               ? 0 : SectorSpace::kAllHeads;
    const SectorSpace raum(leer, fmt, filter);
    return systemspurBytes(raum, prof);
}

uint64_t DiskVolume::bootAreaSize(int volume) const {
    if (!valid(volume) || !profile_) return 0;
    return systemspurBytes(*volumes_[static_cast<size_t>(volume)].space, *profile_);
}

bool DiskVolume::readBootImage(std::vector<uint8_t>& out, int volume) const {
    if (!valid(volume) || !profile_)
        return fail("Seite " + std::to_string(volume) + " gibt es nicht");
    if (bootAreaSize(volume) == 0) return fail(keineSystemspuren(*profile_));

    out.clear();
    SectorSpace& raum = *volumes_[static_cast<size_t>(volume)].space;
    const uint8_t nach = nachspannBytes(*profile_);

    for (const SectorSpace::TrackRef& t : systemspuren(raum, *profile_)) {
        for (uint8_t k = 0; k < t.sectors; ++k) {
            const uint8_t id = static_cast<uint8_t>(t.first_id + k);
            SectorData s;
            if (!raum.readSector(t.cyl, t.head, id, s) || s.data.size() != t.sector_size) {
                out.clear();
                return fail("Systemspur " + std::to_string(t.cyl) + " Sektor "
                            + std::to_string(id) + " nicht lesbar (unformatiert?)");
            }
            out.insert(out.end(), s.data.begin(), s.data.end());
            // Nachspann auf feste Laenge bringen: fehlt er (frisch formatierte Spur),
            // stehen dort Nullen — sonst haetten die Saetze im Abbild keine feste Groesse.
            for (uint8_t i = 0; i < nach; ++i)
                out.push_back(i < s.tail.size() ? s.tail[i] : 0x00);
        }
    }
    return true;
}

bool DiskVolume::readBootImageToFile(const std::string& path, int volume) const {
    std::vector<uint8_t> boot;
    if (!readBootImage(boot, volume)) return false;
    std::string err;
    if (!schreibeDatei(path, boot, err)) return fail(err);
    return true;
}

bool DiskVolume::writeBootImage(const std::vector<uint8_t>& img, int volume) {
    if (!valid(volume) || !profile_)
        return fail("Seite " + std::to_string(volume) + " gibt es nicht");
    if (read_only_)    return fail(kSchreibschutz);
    if (img.empty())   return fail("Das Bootabbild ist leer.");

    const uint64_t n = bootAreaSize(volume);
    if (n == 0) return fail(keineSystemspuren(*profile_));
    if (img.size() > n)
        return fail("Das Bootabbild ist " + std::to_string(img.size())
                    + " Byte gross, die Systemspuren von '" + profile_->name
                    + "' fassen aber nur " + std::to_string(n) + " Byte.");

    SectorSpace& raum = *volumes_[static_cast<size_t>(volume)].space;
    const uint8_t nach = nachspannBytes(*profile_);
    size_t her = 0;

    for (const SectorSpace::TrackRef& t : systemspuren(raum, *profile_)) {
        const size_t satz = t.sector_size + nach;
        for (uint8_t k = 0; k < t.sectors; ++k) {
            if (her >= img.size()) return true;   // kuerzeres Abbild: Rest bleibt Leerspur
            // Ein angebrochener Satz am Ende wird mit dem Fuellbyte der Leerdiskette
            // aufgefuellt — ein halber Sektor laesst sich nicht schreiben.
            std::vector<uint8_t> satzbytes(satz, 0xE5);
            std::copy(img.begin() + static_cast<long>(her),
                      img.begin() + static_cast<long>(std::min(img.size(), her + satz)),
                      satzbytes.begin());
            her += satz;

            const std::vector<uint8_t> daten(satzbytes.begin(),
                                             satzbytes.begin() + t.sector_size);
            const std::vector<uint8_t> nachspann(satzbytes.begin() + t.sector_size,
                                                 satzbytes.end());
            const uint8_t id = static_cast<uint8_t>(t.first_id + k);
            if (!raum.writeSector(t.cyl, t.head, id, daten, nachspann))
                return fail("Systemspuren nicht beschreibbar: " + raum.lastError());
        }
    }
    return true;
}

bool DiskVolume::writeBootImageFile(const std::string& path, int volume) {
    std::vector<uint8_t> img;
    std::string err;
    if (!leseDatei(path, img, err)) return fail(err);
    return writeBootImage(img, volume);
}

bool DiskVolume::saveAs(const std::string& path) {
    if (!disk_) return fail("keine Diskette geoeffnet");
    std::optional<DiskFormat> of;
    if (endung(path) == "img") {
        if (profile_ && !profile_->allow_img)
            return fail("Dieses Dateisystem kann nicht als .img gespeichert werden "
                        "(der UDOS-Sektorkontrollblock steht hinter der Daten-CRC).");
        if (format_) of = *format_;
    }
    if (!disk_->saveAs(path, of)) return fail(disk_->lastError());
    path_ = path;
    return true;
}
