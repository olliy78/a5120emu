/**
 * @file fs_recover.cpp
 * @brief Umsetzung des Wiederherstellungsmodells: ordnen, zaehlen, ausgeben.
 *
 * @see doc/design/15_dateisystempruefung.md §13
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/check/fs_recover.h"

#include <algorithm>
#include <array>

const char* fsRecoverQualityName(FsRecoverQuality q) {
    switch (q) {
        case FsRecoverQuality::Sicher:         return "sicher";
        case FsRecoverQuality::Wahrscheinlich: return "wahrscheinlich";
        case FsRecoverQuality::Bruchstueck:    return "Bruchstueck";
    }
    return "?";
}

std::string fsRecoverEinordnung(const std::vector<uint8_t>& d) {
    if (d.empty()) return "unklar";
    size_t druckbar = 0;
    for (uint8_t b : d)
        if ((b >= 0x20 && b < 0x7F) || b == 0x09 || b == 0x0A || b == 0x0D || b == 0x1A)
            ++druckbar;
    if (druckbar * 10 >= d.size() * 9) return "Text";
    // Z80-Einsprungmuster am Anfang: JP nn (C3), LD SP,nn (31), DI (F3).
    if (d[0] == 0xC3 || d[0] == 0x31 || d[0] == 0xF3) return "Programm";
    return "unklar";
}

bool fsRecoverFuellmuster(const std::vector<uint8_t>& d) {
    if (d.empty()) return true;
    for (uint8_t b : d) if (b != d[0]) return false;
    return true;
}

void fsRecoverBelege(std::string& detail, const std::vector<std::string>& belege,
                     const std::string& was) {
    if (belege.empty()) return;
    if (!detail.empty()) detail += "; ";
    for (size_t i = 0; i < belege.size() && i < 3; ++i)
        detail += (i ? ", " : "") + belege[i];
    if (belege.size() > 3)
        detail += " und " + std::to_string(belege.size() - 3) + " weitere " + was;
}

int FsRecoverReport::zaehler(FsRecoverQuality q) const {
    int n = 0;
    for (const FsRecoverFind& f : funde) if (f.quality == q) ++n;
    return n;
}

void FsRecoverReport::sortieren() {
    // Das Beste zuerst — das ist die Reihenfolge, in der man die Liste durchgeht.
    // Darunter reproduzierbar bis in die letzte Stelle: die Auswahl im Dialog und
    // `--restore N` auf der Kommandozeile gehen ueber den INDEX, und der darf sich
    // zwischen zwei Laeufen ueber dieselbe Diskette nicht verschieben.
    std::stable_sort(funde.begin(), funde.end(),
                     [](const FsRecoverFind& a, const FsRecoverFind& b) {
        if (a.quality != b.quality) return a.quality > b.quality;
        if (a.volume  != b.volume)  return a.volume  < b.volume;
        if (a.cyl     != b.cyl)     return a.cyl     < b.cyl;
        if (a.head    != b.head)    return a.head    < b.head;
        if (a.sector_index != b.sector_index) return a.sector_index < b.sector_index;
        return a.vorschlag < b.vorschlag;
    });
}

FsSchritt& FsRecoverReport::schritt(std::string id, std::string titel) {
    for (size_t k = 0; k < schritte.size(); ++k)
        if (schritte[k].id == id) {
            aktueller_schritt = static_cast<int>(k);
            schritte[k].ausgefuehrt = true;
            schritte[k].grund.clear();
            return schritte[k];
        }
    FsSchritt s;
    s.id    = std::move(id);
    s.titel = std::move(titel);
    schritte.push_back(std::move(s));
    aktueller_schritt = static_cast<int>(schritte.size()) - 1;
    return schritte.back();
}

void FsRecoverReport::schrittEntfaellt(std::string id, std::string titel,
                                       std::string grund) {
    for (const FsSchritt& vorhanden : schritte)
        if (vorhanden.id == id) return;
    FsSchritt s;
    s.id          = std::move(id);
    s.titel       = std::move(titel);
    s.ausgefuehrt = false;
    s.grund       = std::move(grund);
    schritte.push_back(std::move(s));
    aktueller_schritt = -1;
}

void FsRecoverReport::hinzu(FsRecoverFind f) {
    funde.push_back(std::move(f));
    if (aktueller_schritt >= 0 && static_cast<size_t>(aktueller_schritt) < schritte.size())
        ++schritte[static_cast<size_t>(aktueller_schritt)].treffer;
}

void FsRecoverReport::uebernimm(const FsRecoverReport& anderer) {
    funde.insert(funde.end(), anderer.funde.begin(), anderer.funde.end());
    if (!anderer.vollstaendig) vollstaendig = false;
    spuren_gelesen += anderer.spuren_gelesen;
    spuren_gesamt  += anderer.spuren_gesamt;
    if (anderer.level > level) level = anderer.level;
    // Wie bei der Pruefung: dieselbe Kennung ist derselbe Schritt (beide Seiten
    // einer UDOS-Diskette ergeben sonst zweimal dieselbe Zeile).
    for (const FsSchritt& s : anderer.schritte) {
        auto gefunden = std::find_if(schritte.begin(), schritte.end(),
                                     [&](const FsSchritt& x) { return x.id == s.id; });
        if (gefunden == schritte.end()) { schritte.push_back(s); continue; }
        gefunden->treffer += s.treffer;
        if (s.ausgefuehrt && !gefunden->ausgefuehrt) {
            gefunden->ausgefuehrt = true;
            gefunden->grund.clear();
        }
    }
    aktueller_schritt = -1;
}

std::string FsRecoverReport::alsText(bool mit_volume) const {
    auto breit = [](std::string s, size_t n) {
        while (s.size() < n) s += ' ';
        return s;
    };
    auto rechts = [](std::string s, size_t n) {
        while (s.size() < n) s.insert(s.begin(), ' ');
        return s;
    };

    std::string out;
    for (size_t i = 0; i < funde.size(); ++i) {
        const FsRecoverFind& f = funde[i];
        out += rechts(std::to_string(i), 3) + "  ";
        out += breit(fsRecoverQualityName(f.quality), 15);
        if (mit_volume) out += breit("Vol" + std::to_string(f.volume), 6);
        out += breit(f.name.empty() ? f.vorschlag : f.name, 22);
        out += rechts(std::to_string(f.size), 8) + "  ";
        out += f.origin;
        if (!f.detail.empty()) out += "  — " + f.detail;
        out += "\n";
    }
    return out;
}

std::string FsRecoverReport::kurzfassung() const {
    static const std::array<FsRecoverQuality, 3> reihe = {
        FsRecoverQuality::Sicher, FsRecoverQuality::Wahrscheinlich,
        FsRecoverQuality::Bruchstueck};
    std::string out;
    for (FsRecoverQuality q : reihe) {
        const int n = zaehler(q);
        if (n == 0) continue;
        if (!out.empty()) out += ", ";
        out += std::to_string(n) + " " + fsRecoverQualityName(q);
    }
    return out;
}
