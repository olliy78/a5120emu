/**
 * @file k52_codec.cpp
 * @brief `.k52` lesen/schreiben, siehe k52_codec.h.
 */

#include "core/peripherals/kassette/k52_codec.h"

#include <algorithm>
#include <string>

namespace kassette::k52 {
namespace {

constexpr char kKennung[4] = {'K', '5', '2', 'T'};

void put16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(static_cast<uint8_t>(v));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

/// Lesezeiger mit Grenzprüfung.
struct Leser {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    bool fehl = false;
    bool hat(size_t n) {
        if (fehl || d.size() - p < n) fehl = true;
        return !fehl;
    }
    uint32_t u8() { return hat(1) ? d[p++] : 0; }
    uint32_t u16() {
        if (!hat(2)) return 0;
        const uint32_t v = d[p] | (uint32_t(d[p + 1]) << 8);
        p += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | (u16() << 16);
    }
};

}  // namespace

bool istK52(const std::vector<uint8_t>& inhalt) {
    return inhalt.size() >= 4 && std::equal(kKennung, kKennung + 4, inhalt.begin());
}

std::vector<uint8_t> kodieren(const KassettenMedium& m) {
    std::vector<uint8_t> o(kKennung, kKennung + 4);
    o.push_back(kVersion);
    o.push_back(2);
    put16(o, m.aufnahmeMmS());
    put32(o, m.dichteMilli());
    const Geometrie& g = m.geometrie();
    for (uint32_t v : {g.laenge_um, g.vorspann_um, g.marken_abstand_um, g.marke_um, g.initialluecke_um}) put32(o, v);
    for (Seite s : {Seite::A, Seite::B}) {
        o.push_back(m.schreibgeschuetzt(s) ? 1 : 0);
        o.push_back(0);
        put32(o, static_cast<uint32_t>(m.anzahlBloecke(s)));
        for (size_t i = 0; i < m.anzahlBloecke(s); ++i) {
            const Block& b = m.block(s, i);
            put32(o, b.luecke_vor_um);
            put16(o, static_cast<uint32_t>(b.bytes.size()));
            o.insert(o.end(), b.bytes.begin(), b.bytes.end());
        }
    }
    return o;
}

std::optional<KassettenMedium> dekodieren(const std::vector<uint8_t>& inhalt, std::string& fehler) {
    if (!istK52(inhalt)) {
        fehler = "keine .k52-Datei (Kennung K52T fehlt)";
        return std::nullopt;
    }
    Leser r{inhalt, 4};
    const uint32_t version = r.u8();
    const uint32_t seiten = r.u8();
    if (r.fehl) {
        fehler = ".k52 ist abgeschnitten (Kopf)";
        return std::nullopt;
    }
    if (version != kVersion) {
        fehler = ".k52 hat die Version " + std::to_string(version) + ", bekannt ist nur " + std::to_string(kVersion);
        return std::nullopt;
    }
    if (seiten != 2) {
        fehler = ".k52 mit " + std::to_string(seiten) + " Seiten wird nicht unterstützt (erwartet 2)";
        return std::nullopt;
    }
    const uint32_t aufnahme = r.u16();
    const uint32_t dichte = r.u32();
    Geometrie g;
    g.laenge_um = r.u32();
    g.vorspann_um = r.u32();
    g.marken_abstand_um = r.u32();
    g.marke_um = r.u32();
    g.initialluecke_um = r.u32();
    if (r.fehl) {
        fehler = ".k52 ist abgeschnitten (Kopf)";
        return std::nullopt;
    }
    // Maße müssen zusammenpassen: BOT- und EOT-Marke im Magnetband, in der richtigen Reihenfolge.
    const uint64_t bedarf = 2ull * g.vorspann_um + 2ull * g.marken_abstand_um + 2ull * g.marke_um + g.initialluecke_um;
    if (dichte == 0 || g.laenge_um > 0x7FFFFFFFu || bedarf >= g.laenge_um) {
        fehler = ".k52 hat unbrauchbare Bandmaße (Dichte/Länge)";
        return std::nullopt;
    }

    KassettenMedium m(g, dichte, aufnahme);
    for (Seite s : {Seite::A, Seite::B}) {
        const bool schutz = r.u8() != 0;
        r.u8();  // reserviert
        const uint32_t n = r.u32();
        if (r.fehl) {
            fehler = ".k52 ist abgeschnitten (Seitenkopf)";
            return std::nullopt;
        }
        // Mindestens 7 Byte je Block: eine absurde Blockzahl scheitert hier, bevor etwas reserviert wird.
        if (uint64_t(n) * 7 > inhalt.size() - r.p) {
            fehler = ".k52 ist abgeschnitten (Blockzahl größer als der Rest der Datei)";
            return std::nullopt;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t luecke = r.u32();
            const uint32_t len = r.u16();
            if (!r.hat(len)) {
                fehler = ".k52 ist abgeschnitten (Block " + std::to_string(i) + ")";
                return std::nullopt;
            }
            std::vector<uint8_t> bytes(inhalt.begin() + r.p, inhalt.begin() + r.p + len);
            r.p += len;
            if (!m.haengeAn(s, luecke, bytes)) {
                fehler = ".k52: Block " + std::to_string(i) + " hat keine zulässige Länge oder liegt nicht mehr auf dem Band";
                return std::nullopt;
            }
        }
        m.setSchreibschutz(s, schutz);
    }
    if (r.p != inhalt.size()) {
        fehler = ".k52 hat " + std::to_string(inhalt.size() - r.p) + " Byte Reste hinter der zweiten Seite";
        return std::nullopt;
    }
    return m;
}

}  // namespace kassette::k52
