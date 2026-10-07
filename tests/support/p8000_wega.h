/**
 * @file p8000_wega.h
 * @brief WEGA-Installation am P8000 (AP P15, doc/design/25_p8000.md): Zwischenstände der langen
 *        Installation als Dateien AUSSERHALB des Repos, damit Folgearbeiten (Kaltstart von der
 *        Platte, Mehrbenutzer) nicht jedes Mal neu formatieren und installieren.
 *
 * Ein Zwischenstand `<name>` liegt im Ablageordner als drei Dateien:
 *   - `<name>.platte.img`  Winchester-Abbild (K5504.50, roh/LBA) nach `hdFlush()`
 *   - `<name>.start.hfe`   die (veränderte) Kopie der UDOS-Startdiskette aus Laufwerk 0
 *   - `<name>.p8ks`        Save-State P8KS v3 (ohne Medieninhalt, Merkposten p8000 Nr. 6/20)
 * Laufwerk 1 (Quelldiskette) ist an jedem Zwischenstand LEER — gespeichert wird nur am Prompt.
 * Geladen wird in Temp-Kopien (`TempDisk`-Muster): ein Zwischenstand wird nie direkt gemountet.
 *
 * Ablageordner: `K1520_P8000_CACHE`, sonst `$XDG_CACHE_HOME/k1520emu/p8000_wega`, sonst
 * `~/.cache/k1520emu/p8000_wega`.  WEGA-3.0-Disketten (`w30*.img`, nicht im Repo,
 * doc/p8000/wega_datentraeger.md): `K1520_WEGA_DISKS`, sonst
 * `~/Documents/K1520emu/Disketten/P8000/WEGA3.0`.
 *
 * Header-only (wie `p8000_input.h`).
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/machines/p8000/p8000.h"
#include "tests/support/fixtures.h"
#include "tests/support/p8000_input.h"
#include "tests/support/temp_path.h"

namespace k1520test::p8000 {

namespace wega_fs = std::filesystem;

inline std::string heimOrdner(const char* unter) {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + unter;
}

/// Ablageordner der Zwischenstände (wird angelegt).
inline std::string wegaAblage() {
    std::string d;
    if (const char* e = std::getenv("K1520_P8000_CACHE"); e && *e) d = e;
    else if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) d = std::string(x) + "/k1520emu/p8000_wega";
    else d = heimOrdner("/.cache/k1520emu/p8000_wega");
    std::error_code ec;
    wega_fs::create_directories(d, ec);
    return d;
}

/// Ordner der WEGA-3.0-Disketten (`w30root1.img` …).
inline std::string wegaDisketten() {
    if (const char* e = std::getenv("K1520_WEGA_DISKS"); e && *e) return e;
    return heimOrdner("/Documents/K1520emu/Disketten/P8000/WEGA3.0");
}

/// Pfad einer WEGA-Diskette (`"root1"` ⇒ `<ordner>/w30root1.img`).
inline std::string wegaDiskette(const std::string& kurz) { return wegaDisketten() + "/w30" + kurz + ".img"; }

/// Katalogformat der WEGA-Dateisystemdisketten: 80 Zylinder, 2 Köpfe, 9 × 512 B, MFM.
inline constexpr const char* kWegaFormat = "k5601_9x512";

inline bool dateiDa(const std::string& p) { std::error_code ec; return wega_fs::exists(p, ec); }

inline bool kopiere(const std::string& von, const std::string& nach) {
    std::error_code ec;
    wega_fs::copy_file(von, nach, wega_fs::copy_options::overwrite_existing, ec);
    return !ec;
}

/// Konfiguration der Installation: 16-Bit-Karte, WDC 4.2, Platte an Laufwerk 0.
inline P8000Machine::Config wegaConfig(const std::string& platte, bool par_ergaenzen) {
    P8000Machine::Config c;
    c.platte_par_ergaenzen = par_ergaenzen;
    c.karte16 = true;
    c.wdc = P8000Machine::Config::Wdc::V4_2;
    c.platte = platte;
    if (const char* e = std::getenv("K1520_P8000_NBR_STACK"); e && *e) c.nbr_gleichheit_stack = (*e != '0');
    return c;
}

/// Eine laufende Installation: Maschine + Temp-Kopien von Platte, Startdiskette und Quelldiskette.
struct WegaLauf {
    std::string platte = tempPath("k1520_p15_platte.img");
    std::string start  = tempPath("k1520_p15_start.hfe");
    std::string quelle = tempPath("k1520_p15_quelle.img");
    std::unique_ptr<P8000Machine> m;

    WegaLauf() = default;
    WegaLauf(const WegaLauf&) = delete;
    WegaLauf& operator=(const WegaLauf&) = delete;
    ~WegaLauf() {
        m.reset();
        std::error_code ec;
        for (const auto& p : {platte, start, quelle}) wega_fs::remove(p, ec);
    }

    /// Quelldiskette wechseln (wie am Gerät: ausgeworfen, neue eingelegt) — immer eine Temp-Kopie.
    bool diskettenwechsel(const std::string& kurz) {
        m->unmountDisk(1);
        if (!kopiere(wegaDiskette(kurz), quelle)) return false;
        return m->mountDisk(1, quelle, kWegaFormat, false);
    }
    void diskAuswerfen() { m->unmountDisk(1); }
};

inline std::string stufenPfad(const std::string& name, const char* art) {
    return wegaAblage() + "/" + name + "." + art;
}

inline bool stufeDa(const std::string& name) {
    return dateiDa(stufenPfad(name, "p8ks")) && dateiDa(stufenPfad(name, "platte.img")) &&
           dateiDa(stufenPfad(name, "start.hfe"));
}

/// Zwischenstand sichern (nur am Prompt, Quelllaufwerk leer).
inline bool stufeSichern(WegaLauf& l, const std::string& name, std::string* fehler = nullptr) {
    l.m->hdFlush();
    l.m->flushDisks();
    const auto st = l.m->stateBytes();
    {
        std::ofstream f(stufenPfad(name, "p8ks"), std::ios::binary);
        f.write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
        if (!f) { if (fehler) *fehler = "p8ks nicht schreibbar"; return false; }
    }
    if (!kopiere(l.platte, stufenPfad(name, "platte.img")) || !kopiere(l.start, stufenPfad(name, "start.hfe"))) {
        if (fehler) *fehler = "Abbild nicht kopierbar";
        return false;
    }
    std::fprintf(stderr, "  [Zwischenstand '%s' gesichert: %s]\n", name.c_str(), wegaAblage().c_str());
    return true;
}

/// Zwischenstand laden: Temp-Kopien anlegen, Maschine bauen, Startdiskette mounten, Stand laden.
inline bool stufeLaden(WegaLauf& l, const std::string& name, std::string* fehler = nullptr) {
    if (!kopiere(stufenPfad(name, "platte.img"), l.platte) || !kopiere(stufenPfad(name, "start.hfe"), l.start)) {
        if (fehler) *fehler = "Zwischenstand '" + name + "' fehlt";
        return false;
    }
    l.m = std::make_unique<P8000Machine>(wegaConfig(l.platte, false));
    if (!l.m->mountDisk(0, l.start, l.m->defaultFormatName(0), false)) {
        if (fehler) *fehler = "Startdiskette: " + l.m->lastError();
        return false;
    }
    std::ifstream f(stufenPfad(name, "p8ks"), std::ios::binary);
    std::vector<uint8_t> st{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    if (!l.m->restoreStateBytes(st)) {
        if (fehler) *fehler = "Save-State: " + l.m->stateError();
        return false;
    }
    std::fprintf(stderr, "  [Zwischenstand '%s' geladen]\n", name.c_str());
    return true;
}

/// Cursorzeile (rechts ohne Leerzeichen).
inline std::string cursorZeile(const P8000Machine& m) { return zeile(m, m.terminal().zeile()); }

inline bool endetMit(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

/// Läuft, bis die Cursorzeile auf eine der @p fragen endet (rechts getrimmt verglichen); liefert
/// deren Index oder −1 nach @p grenze Takten.  Fortschrittszeile je 400 Mio. Takte.
inline int warteAufFrage(P8000Machine& m, const std::vector<std::string>& fragen, long long grenze) {
    auto passt = [&]() -> int {
        const std::string z = cursorZeile(m);
        for (size_t i = 0; i < fragen.size(); ++i) {
            std::string f = fragen[i];
            while (!f.empty() && f.back() == ' ') f.pop_back();
            if (endetMit(z, f)) return int(i);
        }
        return -1;
    };
    long long t = 0, seit = 0, melde = 0;
    while (t < grenze) {
        m.run(int(kBatch));
        t += kBatch; seit += kBatch; melde += kBatch;
        if (seit >= 100'000) {
            seit = 0;
            if (int i = passt(); i >= 0) return i;
        }
        if (melde >= 400'000'000) {
            melde = 0;
            std::string z = cursorZeile(m);
            while (!z.empty() && z.front() == ' ') z.erase(z.begin());
            std::fprintf(stderr, "  [%6.0f Mio. Takte] %s\n", double(m.totalCycles()) / 1e6, z.c_str());
        }
    }
    return passt();
}

}  // namespace k1520test::p8000
