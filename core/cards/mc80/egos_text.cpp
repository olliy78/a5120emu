/**
 * @file egos_text.cpp
 * @brief Textrueckgewinnung aus dem VIS-2a-Bild (Mustervergleich gegen den EGOS-ZG).
 * @see egos_text.h
 */

#include "core/cards/mc80/egos_text.h"
#include <array>
#include <vector>

namespace mc80 {

namespace {
struct Glyph {
    char c;
    std::vector<uint8_t> spalten;
};
}  // namespace

namespace {

std::vector<Glyph> leseZg(const std::function<uint8_t(uint16_t)>& mem, int& leer_breite)
{
    // Zeichengenerator aus der CPU-Sicht (FB40H = Zeigertabelle).
    const uint16_t tab = static_cast<uint16_t>(mem(0xFB40) | (mem(0xFB41) << 8));
    auto zeiger = [&](int c) {
        const uint16_t a = static_cast<uint16_t>(tab + 2 * (c - 0x20));
        return static_cast<uint16_t>(mem(a) | (mem(static_cast<uint16_t>(a + 1)) << 8));
    };
    std::vector<Glyph> glyphen;
    leer_breite = 0;
    for (int c = 0x20; c <= 0x7E; ++c) {
        const uint16_t p = zeiger(c);
        const int w = static_cast<uint8_t>((zeiger(c + 1) & 0xFF) - (p & 0xFF));
        if (w <= 0 || w > 16) continue;
        if (c == 0x20) { leer_breite = w; continue; }
        Glyph g{static_cast<char>(c), {}};
        for (int i = 0; i < w; ++i) g.spalten.push_back(mem(static_cast<uint16_t>(p + i)));
        glyphen.push_back(std::move(g));
    }
    return glyphen;
}

/// Spaltenmuster ab Bildzeile @p yb, @p hoehe = 8 (normal) oder 16 (doppelt hoch).
std::array<uint16_t, Mc80Vis2a::BREITE> spalten(const Mc80Vis2a& vis, int yb, int hoehe)
{
    std::array<uint16_t, Mc80Vis2a::BREITE> sp{};
    for (int x = 0; x < Mc80Vis2a::BREITE; ++x) {
        uint16_t m = 0;
        for (int b = 0; b < hoehe; ++b)
            if (vis.punkt(x, (yb + b) & 0xFF)) m |= static_cast<uint16_t>(1u << b);
        sp[static_cast<size_t>(x)] = m;
    }
    return sp;
}

/// Musterbyte fuer doppelte Hoehe: jedes Bit zweifach (egos.prn 0170H ZEICHEN_DOPPELT).
uint16_t doppelt(uint8_t g)
{
    uint16_t r = 0;
    for (int b = 0; b < 8; ++b)
        if (g & (1u << b)) r |= static_cast<uint16_t>(3u << (2 * b));
    return r;
}

std::string erkenne(const std::array<uint16_t, Mc80Vis2a::BREITE>& sp, const std::vector<Glyph>& glyphen,
                    int leer_breite, int f /* 1 = normal, 2 = doppelt */)
{
    auto muster = [&](const Glyph& g, int i) -> uint16_t {
        const uint8_t b = g.spalten[static_cast<size_t>(i / f)];
        return f == 1 ? b : doppelt(b);
    };
    std::string text;
    int x = 0;
    const int lb = leer_breite * f;
    while (x < Mc80Vis2a::BREITE) {
        const Glyph* best = nullptr;
        for (const auto& g : glyphen) {
            const int w = static_cast<int>(g.spalten.size()) * f;
            if (x + w > Mc80Vis2a::BREITE) continue;
            if (best && w <= static_cast<int>(best->spalten.size()) * f) continue;
            bool ok = true;
            for (int i = 0; i < w && ok; ++i) ok = sp[static_cast<size_t>(x + i)] == muster(g, i);
            if (ok) best = &g;
        }
        if (best) {
            text += best->c;
            x += static_cast<int>(best->spalten.size()) * f;
            continue;
        }
        if (sp[static_cast<size_t>(x)] == 0) {
            bool leer = lb > 0 && x + lb <= Mc80Vis2a::BREITE;
            for (int i = 0; i < lb && leer; ++i) leer = sp[static_cast<size_t>(x + i)] == 0;
            if (leer) { text += ' '; x += lb; }
            else ++x;
            continue;
        }
        text += '?';   // belegt, aber kein Zeichen: bis zur naechsten Leerspalte ueberspringen
        while (x < Mc80Vis2a::BREITE && sp[static_cast<size_t>(x)] != 0) ++x;
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

int erkannt(const std::string& t)
{
    int n = 0;
    for (char c : t) n += (c != ' ' && c != '?');
    return n;
}

int basis(const Mc80Vis2a& vis, int zeile) { return (0xF8 - 10 * zeile + vis.rollzaehler()) & 0xFE; }

}  // namespace

std::string egosTextzeile(const Mc80Vis2a& vis, const std::function<uint8_t(uint16_t)>& mem,
                          int zeile)
{
    if (zeile < 0 || zeile >= EGOS_TEXTZEILEN) return {};
    int lb = 0;
    const auto glyphen = leseZg(mem, lb);
    return erkenne(spalten(vis, basis(vis, zeile), 8), glyphen, lb, 1);
}

std::vector<std::string> egosText(const Mc80Vis2a& vis, const std::function<uint8_t(uint16_t)>& mem)
{
    int lb = 0;
    const auto glyphen = leseZg(mem, lb);
    std::vector<std::string> z(EGOS_TEXTZEILEN);
    for (int i = 0; i < EGOS_TEXTZEILEN; ++i)
        z[static_cast<size_t>(i)] = erkenne(spalten(vis, basis(vis, i), 8), glyphen, lb, 1);
    // Doppelt hohe Zeichen stehen auf der Zeilenbasis und reichen in die Zeile darueber.
    for (int i = 1; i < EGOS_TEXTZEILEN; ++i) {
        const std::string d = erkenne(spalten(vis, basis(vis, i), 16), glyphen, lb, 2);
        if (erkannt(d) > erkannt(z[static_cast<size_t>(i)]) + erkannt(z[static_cast<size_t>(i - 1)])) {
            z[static_cast<size_t>(i)] = d;
            z[static_cast<size_t>(i - 1)].clear();
        }
    }
    return z;
}

}  // namespace mc80
