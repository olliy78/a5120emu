/**
 * @file k1520tape.cpp
 * @brief `k1520tape` — Kassettenbaender (`.k52`) anlegen, ansehen, mit Dateien fuellen, Bootband bauen.
 *
 * ```
 * k1520tape create   <band.k52>
 * k1520tape ls       <band.k52> [--side A|B]
 * k1520tape put      <band.k52> <datei> [--side A|B] [--block N] [--no-mark]
 * k1520tape get      <band.k52> <datei> [--side A|B] [--file N]
 * k1520tape bootband <binaer> <band.k52> [A|B]
 * ```
 * Nur Rohbloecke: eine Datei ist eine Folge von Bloecken zu je N (Vorgabe 128, hoechstens 256)
 * Datenbytes, der letzte mit 0xE5 aufgefuellt, danach eine Bandmarke.  FEX-Formate sind nicht
 * belegt und werden nicht erzeugt (doc/kassette/treiber.md §6.2).  Ein „File" beim Auslesen ist
 * die Blockfolge bis zur naechsten Bandmarke.
 *
 * Exit-Codes: 0 ok · 1 Fehler (Aufruf, Datei, Band voll).
 * @see doc/kassette/treiber.md §6.1, tools/k1520tape.md
 */

#include "core/peripherals/kassette/k52_codec.h"
#include "core/peripherals/kassette/kassetten_medium.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace kassette;

namespace {

constexpr uint32_t kBlockLuecke_um = 10'000;  // Luecke zwischen Bloecken [?] (nicht belegt)
constexpr uint8_t kFuell = 0xE5;

int fehler(const std::string& s) {
    std::fprintf(stderr, "k1520tape: %s\n", s.c_str());
    return 1;
}

bool leseDatei(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool schreibeDatei(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return static_cast<bool>(f);
}

std::optional<KassettenMedium> ladeBand(const std::string& p) {
    std::vector<uint8_t> d;
    if (!leseDatei(p, d)) { fehler("kann '" + p + "' nicht lesen"); return std::nullopt; }
    std::string f;
    auto m = k52::dekodieren(d, f);
    if (!m) fehler(p + ": " + f);
    return m;
}
bool speichereBand(const std::string& p, const KassettenMedium& m) {
    if (!schreibeDatei(p, k52::kodieren(m))) { fehler("kann '" + p + "' nicht schreiben"); return false; }
    return true;
}

bool seiteParsen(const std::string& s, Seite& out) {
    if (s == "A" || s == "a") { out = Seite::A; return true; }
    if (s == "B" || s == "b") { out = Seite::B; return true; }
    return false;
}

/// Haengt hinter den letzten Block an; die erste Luecke ist die Initialluecke.
bool anhaengen(KassettenMedium& m, Seite s, const std::vector<uint8_t>& bytes) {
    uint32_t l = m.anzahlBloecke(s) == 0 ? m.geometrie().initialluecke_um : kBlockLuecke_um;
    return m.haengeAn(s, l, bytes);
}

int cmdCreate(const std::vector<std::string>& a) {
    if (a.size() != 1) return fehler("create <band.k52>");
    KassettenMedium m;
    return speichereBand(a[0], m) ? 0 : 1;
}

int cmdLs(const std::vector<std::string>& a) {
    Seite nur = Seite::A;
    bool alle = true;
    std::vector<std::string> pos;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--side" && i + 1 < a.size()) {
            if (!seiteParsen(a[++i], nur)) return fehler("--side erwartet A oder B");
            alle = false;
        } else pos.push_back(a[i]);
    }
    if (pos.size() != 1) return fehler("ls <band.k52> [--side A|B]");
    auto m = ladeBand(pos[0]);
    if (!m) return 1;
    for (Seite s : {Seite::A, Seite::B}) {
        if (!alle && s != nur) continue;
        std::printf("Seite %c: %zu Bloecke%s\n", s == Seite::A ? 'A' : 'B', m->anzahlBloecke(s),
                    m->schreibgeschuetzt(s) ? " (schreibgeschuetzt)" : "");
        for (size_t i = 0; i < m->anzahlBloecke(s); ++i) {
            const Block& b = m->block(s, i);
            if (b.bandmarke()) std::printf("  %3zu  Bandmarke\n", i);
            else std::printf("  %3zu  %4zu Bytes, %3zu Daten, CRC %s\n", i, b.bytes.size(),
                             b.bytes.size() >= 4 ? b.bytes.size() - 4 : 0, crcStimmt(b.bytes) ? "ok" : "FALSCH");
        }
    }
    return 0;
}

int cmdPut(const std::vector<std::string>& a) {
    Seite s = Seite::A;
    size_t bl = 128;
    bool marke = true;
    std::vector<std::string> pos;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--side" && i + 1 < a.size()) {
            if (!seiteParsen(a[++i], s)) return fehler("--side erwartet A oder B");
        } else if (a[i] == "--block" && i + 1 < a.size()) {
            bl = std::strtoul(a[++i].c_str(), nullptr, 0);
            if (bl < 1 || bl > 256) return fehler("--block muss 1 bis 256 sein");
        } else if (a[i] == "--no-mark") marke = false;
        else pos.push_back(a[i]);
    }
    if (pos.size() != 2) return fehler("put <band.k52> <datei> [--side A|B] [--block N] [--no-mark]");
    auto m = ladeBand(pos[0]);
    if (!m) return 1;
    std::vector<uint8_t> d;
    if (!leseDatei(pos[1], d)) return fehler("kann '" + pos[1] + "' nicht lesen");
    if (d.empty()) return fehler("Datei ist leer");
    KassettenMedium neu = *m;  // Transaktion: bei vollem Band bleibt die Datei unveraendert
    for (size_t o = 0; o < d.size(); o += bl) {
        std::vector<uint8_t> teil(d.begin() + static_cast<long>(o),
                                  d.begin() + static_cast<long>(std::min(d.size(), o + bl)));
        teil.resize(bl, kFuell);
        if (!anhaengen(neu, s, blockBauen(teil))) return fehler("Band voll");
    }
    if (marke && !anhaengen(neu, s, bandmarkeBytes())) return fehler("Band voll");
    return speichereBand(pos[0], neu) ? 0 : 1;
}

int cmdGet(const std::vector<std::string>& a) {
    Seite s = Seite::A;
    size_t nr = 0;
    std::vector<std::string> pos;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--side" && i + 1 < a.size()) {
            if (!seiteParsen(a[++i], s)) return fehler("--side erwartet A oder B");
        } else if (a[i] == "--file" && i + 1 < a.size()) nr = std::strtoul(a[++i].c_str(), nullptr, 0);
        else pos.push_back(a[i]);
    }
    if (pos.size() != 2) return fehler("get <band.k52> <datei> [--side A|B] [--file N]");
    auto m = ladeBand(pos[0]);
    if (!m) return 1;
    // Datei N = Blockfolge zwischen den Bandmarken (Marken selbst gehoeren nicht dazu).
    size_t n = m->anzahlBloecke(s), i = 0, dateien = 0;
    std::vector<uint8_t> out;
    bool gefunden = false;
    while (i < n) {
        size_t j = i;
        std::vector<uint8_t> teil;
        bool hat = false;
        while (j < n && !m->block(s, j).bandmarke()) {
            const Block& b = m->block(s, j);
            if (!crcStimmt(b.bytes)) return fehler("Block " + std::to_string(j) + ": CRC falsch");
            teil.insert(teil.end(), b.bytes.begin() + 1, b.bytes.end() - 3);
            hat = true;
            ++j;
        }
        if (hat) {
            if (dateien == nr) { out = teil; gefunden = true; break; }
            ++dateien;
        }
        i = j + 1;
    }
    if (!gefunden) return fehler("Datei " + std::to_string(nr) + " nicht auf dem Band");
    return schreibeDatei(pos[1], out) ? 0 : fehler("kann '" + pos[1] + "' nicht schreiben");
}

int cmdBootband(const std::vector<std::string>& a) {
    if (a.size() < 2 || a.size() > 3) return fehler("bootband <binaer> <band.k52> [A|B]");
    Seite s = Seite::A;
    if (a.size() == 3 && !seiteParsen(a[2], s)) return fehler("Seite erwartet A oder B");
    std::vector<uint8_t> d;
    if (!leseDatei(a[0], d)) return fehler("kann '" + a[0] + "' nicht lesen");
    if (d.empty() || d.size() > 512) return fehler("Bootprogramm muss 1 bis 512 Bytes lang sein");
    d.resize(512, 0x00);
    // Lade-ROM prueft (0400H..0402H) = 'SYL' (treiber.md §6.1); Einsprung bei Versatz 34H.
    if (d[0] != 'S' || d[1] != 'Y' || d[2] != 'L')
        return fehler("Bootprogramm muss mit 'SYL' beginnen (Pruefung des Lade-ROMs); Einsprung bei Versatz 34H");
    KassettenMedium m;
    for (size_t b = 0; b < 4; ++b) {
        std::vector<uint8_t> teil(d.begin() + static_cast<long>(b * 128),
                                  d.begin() + static_cast<long>(b * 128 + 128));
        if (!anhaengen(m, s, blockBauen(teil))) return fehler("Band voll");
    }
    return speichereBand(a[1], m) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Aufruf: k1520tape create|ls|put|get|bootband ... (siehe tools/k1520tape.md)\n");
        return 1;
    }
    std::string c = argv[1];
    std::vector<std::string> a(argv + 2, argv + argc);
    if (c == "create") return cmdCreate(a);
    if (c == "ls") return cmdLs(a);
    if (c == "put") return cmdPut(a);
    if (c == "get") return cmdGet(a);
    if (c == "bootband") return cmdBootband(a);
    return fehler("unbekanntes Kommando '" + c + "'");
}
