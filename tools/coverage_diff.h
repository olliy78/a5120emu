/**
 * @file coverage_diff.h
 * @brief Lädt boot_trace `--coverage`-CSVs (cpu,pc,hits) und difft zwei Läufe.
 *
 * Der CSV-Export von `boot_trace --coverage <file>` hat das Format
 *   cpu,pc,hits         (Kopfzeile)  z.B.  ZVE1,0x0135,42
 * Dieser Header parst solche CSVs in eine `cpu → (pc → hits)`-Map und vergleicht
 * zwei davon: welche Adressen lief nur Lauf A, nur Lauf B, und wo unterscheiden sich
 * die Trefferzahlen. Das ist der eingebaute Run-Diff (`boot_trace --diff a b`).
 *
 * Header-only & über `std::istream` parsebar → ohne Maschine unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <istream>
#include <fstream>

namespace covdiff {

/// cpu-Name → (PC → Trefferzahl)
using CovMap = std::map<std::string, std::map<uint16_t, long>>;

/// Eine CSV-Zeile "cpu,pc,hits" parsen (pc base-0). false = unbrauchbar.
inline bool parseLine(const std::string& line, std::string& cpu, uint16_t& pc, long& hits) {
    if (line.empty()) return false;
    size_t c1 = line.find(',');             if (c1 == std::string::npos) return false;
    size_t c2 = line.find(',', c1 + 1);     if (c2 == std::string::npos) return false;
    cpu = line.substr(0, c1);
    if (cpu == "cpu") return false;         // Kopfzeile
    char* e1 = nullptr; long p = strtol(line.substr(c1 + 1, c2 - c1 - 1).c_str(), &e1, 0);
    char* e2 = nullptr; long h = strtol(line.substr(c2 + 1).c_str(), &e2, 0);
    if (e1 && *e1 && *e1 != ' ' && *e1 != '\r') return false;
    pc = (uint16_t)p; hits = h; (void)e2;
    return true;
}

/// CSV aus einem Stream in @p out einlesen. @return Zahl gelesener Zeilen.
inline int parseCsv(std::istream& in, CovMap& out) {
    std::string l; int n = 0; std::string cpu; uint16_t pc; long hits;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (parseLine(l, cpu, pc, hits)) { out[cpu][pc] = hits; ++n; }
    }
    return n;
}

/// CSV aus einer Datei laden. @return false, wenn die Datei fehlt.
inline bool loadCsv(const std::string& path, CovMap& out) {
    std::ifstream f(path);
    if (!f) return false;
    parseCsv(f, out);
    return true;
}

/// Diff-Ergebnis für eine CPU.
struct CpuDiff {
    std::vector<uint16_t> only_a;   ///< PCs nur in Lauf A ausgeführt
    std::vector<uint16_t> only_b;   ///< PCs nur in Lauf B ausgeführt
    std::vector<uint16_t> changed;  ///< PCs in beiden, aber mit anderer Trefferzahl
    size_t a_count = 0, b_count = 0, common = 0;
};

/// Zwei CovMaps vergleichen → je CPU (Vereinigung der CPU-Namen) ein CpuDiff.
inline std::map<std::string, CpuDiff> diff(const CovMap& a, const CovMap& b) {
    std::map<std::string, CpuDiff> res;
    std::vector<std::string> cpus;
    for (auto& kv : a) cpus.push_back(kv.first);
    for (auto& kv : b) if (a.find(kv.first) == a.end()) cpus.push_back(kv.first);
    for (auto& cpu : cpus) {
        CpuDiff d;
        auto ia = a.find(cpu); auto ib = b.find(cpu);
        static const std::map<uint16_t, long> empty;
        const auto& ma = (ia != a.end()) ? ia->second : empty;
        const auto& mb = (ib != b.end()) ? ib->second : empty;
        d.a_count = ma.size(); d.b_count = mb.size();
        for (auto& kv : ma) {
            auto it = mb.find(kv.first);
            if (it == mb.end()) d.only_a.push_back(kv.first);
            else { ++d.common; if (it->second != kv.second) d.changed.push_back(kv.first); }
        }
        for (auto& kv : mb) if (ma.find(kv.first) == ma.end()) d.only_b.push_back(kv.first);
        res[cpu] = std::move(d);
    }
    return res;
}

} // namespace covdiff
