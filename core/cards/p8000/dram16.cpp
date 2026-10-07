/**
 * @file dram16.cpp
 * @brief P8000 16-Bit-Teil: DRAM-Karten am Speicherbus.  Quellen und Annahmen [D1]–[D5]: dram16.h.
 * @license MIT
 */
#include "dram16.h"

#include "core/util/zustand.h"

#include <algorithm>
#include <cstdio>

namespace {
constexpr uint8_t SAVE_VERSION = 1;
}

std::string P8000Dram16::pruefe(const Config& cfg) {
    if (cfg.karten.size() > size_t(MAX_KARTEN))
        return "höchstens " + std::to_string(MAX_KARTEN) + " DRAM-Karten";
    for (size_t i = 0; i < cfg.karten.size(); ++i) {
        const Karte& k = cfg.karten[i];
        const int max = k.typ == Karte::Typ::M1 ? 15 : 63;
        if (k.modul > max)
            return "Moduladresse " + std::to_string(k.modul) + " außerhalb 0…" + std::to_string(max);
        for (size_t j = 0; j < i; ++j) {
            const Karte& o = cfg.karten[j];
            const uint64_t a0 = k.basis(), a1 = a0 + k.groesse(), b0 = o.basis(), b1 = b0 + o.groesse();
            if (a0 < b1 && b0 < a1) {
                char t[96];
                std::snprintf(t, sizeof t, "DRAM-Karten %zu und %zu überlappen (%06X/%06X)", j + 1, i + 1,
                              unsigned(b0), unsigned(a0));
                return t;
            }
        }
    }
    return {};
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
    for (size_t i = 0; i < karten_.size(); ++i) {   // Moduladressdekoder je Karte (MEMSEL)
        const uint32_t b = karten_[i].basis();
        if (adr - b < karten_[i].groesse()) return &inhalt_[i][adr - b];
    }
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
