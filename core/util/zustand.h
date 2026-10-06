/**
 * @file zustand.h
 * @brief Ein Durchlauf für Speichern UND Laden von Gerätezustand (Save-State P8KS, Entwurf 25
 *        §10.2): jedes Feld steht genau einmal in der Feldliste einer `visit(ZAr&)`-Funktion,
 *        beide Richtungen können nicht auseinanderlaufen.  Little-Endian, unabhängig vom Wirt.
 *        (Muster: `Ar` in core/primitives/upd765.cpp, hier als Header für mehrere Bausteine.)
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace k1520 {

struct ZAr {
    bool save;
    std::vector<uint8_t>* out = nullptr;
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool ok = true;

    static ZAr schreiber(std::vector<uint8_t>& o) { ZAr a{true}; a.out = &o; return a; }
    static ZAr leser(const uint8_t*& p, const uint8_t* e) { ZAr a{false}; a.p = p; a.end = e; return a; }

    template <class T> void num(T& v) {
        static_assert(std::is_integral<T>::value, "integral");
        if (save) {
            for (size_t i = 0; i < sizeof(T); ++i)
                out->push_back(static_cast<uint8_t>((static_cast<uint64_t>(v) >> (8 * i)) & 0xFF));
        } else {
            if (!ok || static_cast<size_t>(end - p) < sizeof(T)) { ok = false; return; }
            uint64_t x = 0;
            for (size_t i = 0; i < sizeof(T); ++i) x |= static_cast<uint64_t>(*p++) << (8 * i);
            v = static_cast<T>(x);
        }
    }
    template <class E> void en(E& v) { uint8_t t = static_cast<uint8_t>(v); num(t); v = static_cast<E>(t); }
    void flag(bool& v) { uint8_t t = v ? 1 : 0; num(t); v = t != 0; }
    /// @p n Rohbytes (Speicherfelder).
    void raw(uint8_t* d, size_t n) {
        if (save) { out->insert(out->end(), d, d + n); return; }
        if (!ok || static_cast<size_t>(end - p) < n) { ok = false; return; }
        for (size_t i = 0; i < n; ++i) d[i] = p[i];
        p += n;
    }
};

}  // namespace k1520
