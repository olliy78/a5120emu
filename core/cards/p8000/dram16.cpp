/**
 * @file dram16.cpp
 * @brief P8000 16-Bit-Teil: DRAM-Karten am Speicherbus.  Quellen und Annahmen [D1]–[D5]: dram16.h.
 * @license MIT
 */
#include "dram16.h"

#include "core/util/zustand.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {
constexpr uint8_t SAVE_VERSION = 1;
}

std::string P8000Dram16::Karte::text() const {
    char t[64];
    if (ramKarte())
        std::snprintf(t, sizeof t, "RAM-Karte %u MB", unsigned(groesse() >> 20));
    else
        std::snprintf(t, sizeof t, "%s @ Modul %u (%06XH)", typ == Typ::M1 ? "1 MB" : "256 KB",
                      unsigned(modul), unsigned(basis()));
    return t;
}

std::string P8000Dram16::pruefe(const Config& cfg) {
    if (cfg.karten.size() > size_t(MAX_KARTEN))
        return "höchstens " + std::to_string(MAX_KARTEN) + " DRAM-Karten (Speicherbus X10–X13)";
    for (size_t i = 0; i < cfg.karten.size(); ++i) {
        const Karte& k = cfg.karten[i];
        if (uint8_t(k.typ) > uint8_t(Karte::Typ::R16)) return "unbekannter DRAM-Kartentyp";
        const int max = k.ramKarte() ? 0 : k.typ == Karte::Typ::M1 ? 15 : 63;
        if (k.modul > max)
            return k.ramKarte() ? std::string("die RAM-Karte 16 MB hat keine Moduladresse (liegt immer ab 0)")
                                : "Moduladresse " + std::to_string(k.modul) + " außerhalb 0…" + std::to_string(max);
        for (size_t j = 0; j < i; ++j) {
            const Karte& o = cfg.karten[j];
            const uint64_t a0 = k.basis(), a1 = a0 + k.groesse(), b0 = o.basis(), b1 = b0 + o.groesse();
            if (a0 < b1 && b0 < a1) {
                char t[160];
                std::snprintf(t, sizeof t, "DRAM-Karten %zu und %zu überlappen (%s / %s)", j + 1, i + 1,
                              o.text().c_str(), k.text().c_str());
                return t;
            }
        }
    }
    return {};
}

namespace {
std::string gross(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool zahl(const std::string& s, int max, int& n) {
    if (s.empty() || s.size() > 2 || s.find_first_not_of("0123456789") != std::string::npos) return false;
    n = std::stoi(s);
    return n <= max;
}
using KTyp = P8000Dram16::Karte::Typ;
const struct { const char* name; KTyp typ; } TYPEN[] = {
    {"256K", KTyp::K256}, {"1M", KTyp::M1}, {"2M", KTyp::R2}, {"4M", KTyp::R4}, {"8M", KTyp::R8}, {"16M", KTyp::R16}};
bool typAus(const std::string& t, KTyp& typ) {
    for (const auto& e : TYPEN)
        if (t == e.name) { typ = e.typ; return true; }
    return false;
}
const char* typName(KTyp t) {
    for (const auto& e : TYPEN)
        if (t == e.typ) return e.name;
    return "?";
}
}  // namespace

std::string P8000Dram16::parse(const std::string& text, std::vector<Karte>& karten) {
    std::string v = gross(text);
    for (size_t p; (p = v.find("\xC3\x97")) != std::string::npos;) v.replace(p, 2, "X");   // „×"
    static const std::string hilfe =
        " (Langform 1M@n | 256K@n | 16M@0 …, mit '+' verbunden; Kurzform 4x256K | 4x1M | 2M | 4M | 8M | 16M)";
    if (v.empty()) return "dram leer" + hilfe;
    std::vector<Karte> neu;
    const size_t x = v.find('X');
    if (x != std::string::npos) {                                   // Kurzform NxT ab Modul 0
        int n = 0;
        Karte k;
        if (!zahl(v.substr(0, x), MAX_KARTEN, n) || n < 1 || !typAus(v.substr(x + 1), k.typ) || k.ramKarte())
            return "dram='" + text + "' unbekannt" + hilfe;
        for (int i = 0; i < n; ++i) { k.modul = uint8_t(i); neu.push_back(k); }
    } else if (v.find('@') == std::string::npos && v.find('+') == std::string::npos) {   // „16M", „1M"
        Karte k;
        if (!typAus(v, k.typ)) return "dram='" + text + "' unbekannt" + hilfe;
        k.modul = 0;
        neu.push_back(k);
    } else {                                                        // Langform
        size_t q = 0;
        while (q <= v.size()) {
            size_t e = v.find('+', q);
            if (e == std::string::npos) e = v.size();
            const std::string s = v.substr(q, e - q);
            q = e + 1;
            const size_t at = s.find('@');
            Karte k;
            if (at == std::string::npos || !typAus(s.substr(0, at), k.typ))
                return "dram-Karte '" + s + "' unbekannt" + hilfe;
            int m = 0;
            const int max = k.ramKarte() ? 0 : k.typ == Karte::Typ::M1 ? 15 : 63;
            if (!zahl(s.substr(at + 1), max, m))
                return "dram-Moduladresse in '" + s + "' ungültig" +
                       (k.ramKarte() ? std::string(" (RAM-Karte 16 MB nur @0 — sie hat keine Moduladresse)")
                                     : " (0–" + std::to_string(max) + ")");
            k.modul = uint8_t(m);
            neu.push_back(k);
        }
    }
    Config c;
    c.karten = neu;
    const std::string f = pruefe(c);
    if (!f.empty()) return f;
    karten = neu;
    return {};
}

std::string P8000Dram16::format(const std::vector<Karte>& karten) {
    std::string s;
    for (const Karte& k : karten) {
        if (!s.empty()) s += '+';
        s += std::string(typName(k.typ)) + "@" + std::to_string(k.modul);
    }
    return s;
}

int P8000Dram16::maxSegment(const std::vector<Karte>& karten) {
    auto da = [&](uint32_t adr) {
        for (const Karte& k : karten)
            if (k.waehlt(adr)) return true;
        return false;
    };
    int seg = -1;
    for (uint32_t s = 0; s < 0x80; ++s) {          // MMU aus: A16–A22 = SN, A23 = 0
        if (!da((s << 16) | 0x8000) && !da((s << 16) | 0xFFFE)) break;
        seg = int(s);
    }
    return seg;
}

P8000Dram16::P8000Dram16() : P8000Dram16(Config{}) {}

P8000Dram16::P8000Dram16(const Config& cfg) : cfg_(cfg) {
    fehler_ = pruefe(cfg);
    if (fehler_.empty()) karten_ = cfg.karten;
    for (const Karte& k : karten_) inhalt_.emplace_back(k.groesse(), cfg_.fuellwert);
}

uint32_t P8000Dram16::gesamt() const {
    uint32_t s = 0;
    for (const Karte& k : karten_) s += k.groesse();
    return s;
}

void P8000Dram16::powerOn() {
    for (auto& v : inhalt_) std::fill(v.begin(), v.end(), cfg_.fuellwert);
    paritaetFalsch_.clear();
    pe_ = led_ = false;
}

void P8000Dram16::reset() { led_ = false; }

uint8_t* P8000Dram16::zelle(uint32_t adr) {
    adr &= 0xFFFFFF;
    for (size_t i = 0; i < karten_.size(); ++i)      // Moduladressdekoder je Karte (MEMSEL)
        if (karten_[i].waehlt(adr)) return &inhalt_[i][adr - karten_[i].basis()];
    return nullptr;
}

bool P8000Dram16::lesen(uint32_t adr, uint16_t& wort) {
    const uint32_t a = adr & 0xFFFFFE;
    uint8_t* z = zelle(a);
    pe_ = false;
    if (!z) return false;
    wort = uint16_t((z[0] << 8) | z[1]);
    if (!paritaetFalsch_.empty() && (paritaetFalsch_.count(a) || paritaetFalsch_.count(a | 1))) {
        pe_ = true;    // [D3]
        led_ = true;
    }
    return true;
}

bool P8000Dram16::schreiben(uint32_t adr, bool wort, uint16_t daten) {
    adr &= 0xFFFFFF;
    const uint32_t a = adr & 0xFFFFFE;
    uint8_t* z = zelle(a);
    if (!z) return false;
    if (wort) {
        z[0] = uint8_t(daten >> 8);
        z[1] = uint8_t(daten);
        paritaetFalsch_.erase(a);
        paritaetFalsch_.erase(a | 1);
    } else if (adr & 1) {
        z[1] = uint8_t(daten);          // A0 = 1: Low-Bank D0–7
        paritaetFalsch_.erase(a | 1);
    } else {
        z[0] = uint8_t(daten >> 8);     // A0 = 0: High-Bank D8–15
        paritaetFalsch_.erase(a);
    }
    return true;
}

bool P8000Dram16::paritaetsfehlerSetzen(uint32_t adr) {
    adr &= 0xFFFFFF;
    if (!zelle(adr)) return false;
    for (const Karte& k : karten_)                  // RAM-Karte 16 MB: keine Paritätsbits
        if (k.waehlt(adr) && !k.paritaet()) return false;
    paritaetFalsch_.insert(adr);
    return true;
}

uint8_t P8000Dram16::peek(uint32_t adr) const {
    const uint8_t* z = const_cast<P8000Dram16*>(this)->zelle(adr);
    return z ? *z : 0xFF;
}

bool P8000Dram16::poke(uint32_t adr, uint8_t wert) {
    uint8_t* z = zelle(adr);
    if (!z) return false;
    *z = wert;
    return true;
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void P8000Dram16::serialize(std::vector<uint8_t>& out) const {
    out.push_back(SAVE_VERSION);
    auto a = k1520::ZAr::schreiber(out);
    uint8_t n = uint8_t(karten_.size());
    a.num(n);
    for (size_t i = 0; i < karten_.size(); ++i) {
        uint8_t typ = uint8_t(karten_[i].typ), modul = karten_[i].modul;
        a.num(typ); a.num(modul);
        a.raw(const_cast<uint8_t*>(inhalt_[i].data()), inhalt_[i].size());
    }
    uint32_t nf = uint32_t(paritaetFalsch_.size());
    a.num(nf);
    for (uint32_t x : paritaetFalsch_) a.num(x);
    bool led = led_;
    a.flag(led);
}

bool P8000Dram16::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    const uint8_t* q = p + 1;
    auto a = k1520::ZAr::leser(q, end);
    uint8_t n = 0;
    a.num(n);
    if (!a.ok || n != karten_.size()) return false;   // Bestückung gehört zur Konfiguration
    std::vector<std::vector<uint8_t>> neu(karten_.size());
    for (size_t i = 0; i < karten_.size(); ++i) {
        uint8_t typ = 0, modul = 0;
        a.num(typ); a.num(modul);
        if (!a.ok || typ != uint8_t(karten_[i].typ) || modul != karten_[i].modul) return false;
        neu[i].resize(karten_[i].groesse());
        a.raw(neu[i].data(), neu[i].size());
    }
    uint32_t nf = 0;
    a.num(nf);
    std::set<uint32_t> falsch;
    for (uint32_t i = 0; i < nf && a.ok; ++i) { uint32_t x = 0; a.num(x); falsch.insert(x); }
    bool led = false;
    a.flag(led);
    if (!a.ok) return false;
    inhalt_ = std::move(neu);
    paritaetFalsch_ = std::move(falsch);
    led_ = led;
    pe_ = false;
    p = a.p;
    return true;
}
