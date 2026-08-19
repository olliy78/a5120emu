/**
 * @file fs_check.cpp
 * @brief Umsetzung des Pruefmodells: ordnen, zaehlen, ausgeben.
 *
 * @see doc/design/15_dateisystempruefung.md §5
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_check.h"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

const char* fsSeverityName(FsSeverity s) {
    switch (s) {
        case FsSeverity::Info:    return "Hinweis";
        case FsSeverity::Warnung: return "Warnung";
        case FsSeverity::Fehler:  return "Fehler";
        case FsSeverity::Gefahr:  return "Gefahr";
    }
    return "?";
}

const char* fsLayerName(FsLayer l) {
    switch (l) {
        case FsLayer::Medium:     return "Medium";
        case FsLayer::Verwaltung: return "Verwaltung";
        case FsLayer::Dateien:    return "Dateien";
        case FsLayer::Erkennung:  return "Erkennung";
    }
    return "?";
}

FsSeverity FsCheckReport::hoechste() const {
    FsSeverity h = FsSeverity::Info;
    for (const FsFinding& f : findings)
        if (f.severity > h) h = f.severity;
    return h;
}

int FsCheckReport::zaehler(FsSeverity s) const {
    int n = 0;
    for (const FsFinding& f : findings) if (f.severity == s) ++n;
    return n;
}

int FsCheckReport::zaehlerAb(FsSeverity s) const {
    int n = 0;
    for (const FsFinding& f : findings) if (f.severity >= s) ++n;
    return n;
}

void FsCheckReport::sortieren() {
    // Reproduzierbar bis in die letzte Stelle: zwei Laeufe ueber dieselbe Diskette
    // muessen dieselbe Reihenfolge liefern, sonst taugt weder ein Textvergleich im
    // Test noch die Auswahl im Reparaturdialog (die ueber den Index geht).
    std::stable_sort(findings.begin(), findings.end(),
                     [](const FsFinding& a, const FsFinding& b) {
        if (a.severity != b.severity) return a.severity > b.severity;
        if (a.volume   != b.volume)   return a.volume   < b.volume;
        if (a.layer    != b.layer)    return a.layer    < b.layer;
        if (a.cyl      != b.cyl)      return a.cyl      < b.cyl;
        if (a.head     != b.head)     return a.head     < b.head;
        if (a.sector_index != b.sector_index) return a.sector_index < b.sector_index;
        if (a.id       != b.id)       return a.id       < b.id;
        return a.object < b.object;
    });
}

void FsCheckReport::uebernimm(const FsCheckReport& anderer) {
    findings.insert(findings.end(), anderer.findings.begin(), anderer.findings.end());
    if (!anderer.vollstaendig) vollstaendig = false;
    spuren_gelesen += anderer.spuren_gelesen;
    spuren_gesamt  += anderer.spuren_gesamt;
    if (anderer.level > level) level = anderer.level;
}

void FsCheckReport::begrenzen(size_t je_kennung) {
    if (je_kennung == 0) return;
    std::map<std::pair<std::string, int>, size_t> zahl;
    std::vector<FsFinding> behalten;
    std::vector<FsFinding> summen;
    behalten.reserve(findings.size());

    for (const FsFinding& f : findings) {
        size_t& n = zahl[{f.id, f.volume}];
        if (++n <= je_kennung) { behalten.push_back(f); continue; }
        if (n == je_kennung + 1) {
            // Die Sammelmeldung entsteht EINMAL und wird danach nur hochgezaehlt;
            // ihre Schwere ist die des ersten ueberzaehligen Befundes.
            FsFinding s;
            s.id       = f.id;
            s.severity = f.severity;
            s.layer    = f.layer;
            s.volume   = f.volume;
            s.object   = "";
            summen.push_back(std::move(s));
        }
    }
    for (FsFinding& s : summen) {
        const size_t gesamt = zahl[{s.id, s.volume}];
        s.text = "und " + std::to_string(gesamt - je_kennung) + " weitere Befunde"
                 " dieser Art (insgesamt " + std::to_string(gesamt) + ")";
    }
    behalten.insert(behalten.end(), summen.begin(), summen.end());
    findings = std::move(behalten);
}

std::string FsCheckReport::alsText(bool mit_volume) const {
    auto breit = [](std::string s, size_t n) {
        while (s.size() < n) s += ' ';
        return s;
    };

    std::string out;
    for (const FsFinding& f : findings) {
        std::string ort;
        if (f.cyl >= 0) {
            ort = "c" + std::to_string(f.cyl) + "h" + std::to_string(f.head);
            if (f.sector_index >= 0) ort += " #" + std::to_string(f.sector_index);
        }
        out += breit(fsSeverityName(f.severity), 8);
        out += breit(fsLayerName(f.layer), 11);
        if (mit_volume) out += breit("Vol" + std::to_string(f.volume), 6);
        out += breit(ort, 12);
        out += breit(f.object, 16);
        out += breit(f.id, 30);
        out += f.text;
        out += "\n";
    }
    return out;
}

std::string FsCheckReport::kurzfassung() const {
    static const std::array<FsSeverity, 4> reihe = {
        FsSeverity::Gefahr, FsSeverity::Fehler, FsSeverity::Warnung, FsSeverity::Info};
    std::string out;
    for (FsSeverity s : reihe) {
        const int n = zaehler(s);
        if (n == 0) continue;
        if (!out.empty()) out += ", ";
        out += std::to_string(n) + " " + fsSeverityName(s);
    }
    return out;
}

// ─── Bausatz ─────────────────────────────────────────────────────────────────

FsFinding& FsFindings::add(std::string id, FsSeverity sev, FsLayer layer,
                           std::string object, std::string text) {
    FsFinding f;
    f.id       = std::move(id);
    f.severity = sev;
    f.layer    = layer;
    f.object   = std::move(object);
    f.text     = std::move(text);
    an_.findings.push_back(std::move(f));
    return an_.findings.back();
}

FsFinding& FsFindings::addAt(std::string id, FsSeverity sev, FsLayer layer,
                             std::string object, std::string text,
                             int cyl, int head, int sector_index) {
    FsFinding& f = add(std::move(id), sev, layer, std::move(object), std::move(text));
    f.cyl          = cyl;
    f.head         = head;
    f.sector_index = sector_index;
    return f;
}
