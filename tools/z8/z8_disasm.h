/**
 * @file z8_disasm.h
 * @brief Z8-Disassembler (header-only) aus der Befehlstabelle core/primitives/z8/z8_table.h.
 *
 * C++-Gegenstück zu tools/z8_disasm.py (das als Werkzeug mit rekursivem Abstieg für ganze
 * Abzüge bleibt).  Benutzt von k1520dbg (`cpu z8`), dem Assembler-Rundlauftest und dem
 * MAME-Orakel.  Schreibweise (vom Assembler tools/z8/z8_asm.h wieder lesbar):
 *   r0..r15, rr0..rr14       Arbeitsregister/-paare
 *   P0..P3, SIO … SPL        Port- und Steuerregister mit Namen, sonst 7FH / 0A4H
 *   @r5, @rr6, @45H          indirekt
 *   #12H                     Direktwert,   24H(r0)  indiziert,   JP NZ,1234H
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8/z8_table.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace z8dis {

struct Ergebnis {
    int         len = 1;
    std::string text;
    bool        hatZiel = false;   ///< Sprung-/Rufziel (DA/RA) bekannt
    uint16_t    ziel = 0;
    bool        ruft = false;      ///< CALL
    bool        endet = false;     ///< unbedingter Sprung, RET, IRET (Ablauf endet hier)
    uint8_t     bytes[3] = {};
};

/// Hexzahl im Zilog-Stil: führende 0, wenn sie mit einem Buchstaben beginnt (0A4H).
inline std::string hex(unsigned v, int stellen) {
    char b[16];
    std::snprintf(b, sizeof b, "%0*XH", stellen, v);
    std::string s = b;
    if (s[0] >= 'A' && s[0] <= 'F') s = "0" + s;
    return s;
}

/// 8-Bit-Registeradresse (Ex = Arbeitsregister).
inline std::string reg(uint8_t b) {
    if ((b & 0xF0) == 0xE0) return "r" + std::to_string(b & 15);
    if (const char* n = z8::sfrName(b)) return n;
    return hex(b, 2);
}
/// Registerpaar (Ex = Arbeitspaar).
inline std::string regPaar(uint8_t b) {
    if ((b & 0xF0) == 0xE0) return "rr" + std::to_string(b & 15);
    if (const char* n = z8::sfrName(b)) return n;
    return hex(b, 2);
}
/// Basis einer indizierten Adresse: volle Registeradresse, KEIN Arbeitsregister (Annahme [Z8-A3]).
inline std::string basis(uint8_t b) {
    if (const char* n = z8::sfrName(b)) return n;
    return hex(b, 2);
}
inline std::string wr(unsigned n) { return "r" + std::to_string(n & 15); }
inline std::string wrr(unsigned n) { return "rr" + std::to_string(n & 15); }

/// Einen Befehl an @p pc dekodieren; @p mem liefert Programmbytes.
inline Ergebnis disasm(const std::function<uint8_t(uint16_t)>& mem, uint16_t pc) {
    using z8::Fmt;
    using z8::Mn;
    Ergebnis e;
    const uint8_t op = mem(pc);
    const z8::Insn& in = z8::tabelle()[op];
    e.bytes[0] = op;
    if (in.mn == Mn::Ungueltig) {
        e.len = 1;
        e.text = "DB " + hex(op, 2);
        e.endet = true;
        return e;
    }
    e.len = in.len;
    for (int i = 1; i < in.len; ++i) e.bytes[i] = mem(uint16_t(pc + i));
    const uint8_t b1 = e.bytes[1], b2 = e.bytes[2];
    const unsigned hi = op >> 4;
    std::string m = z8::mnName(in.mn), o;
    auto rel = [&](uint8_t ra) {
        e.hatZiel = true;
        e.ziel = uint16_t(pc + 2 + int8_t(ra));
        return hex(e.ziel, 4);
    };
    auto cc = [](unsigned c) { std::string s = z8::ccName(c); return s.empty() ? s : s + ","; };
    switch (in.fmt) {
    case Fmt::Keins: break;
    case Fmt::R1: o = reg(b1); break;
    case Fmt::IR1: o = "@" + reg(b1); break;
    case Fmt::RR1: o = regPaar(b1); break;
    case Fmt::IRR1: o = "@" + regPaar(b1); break;
    case Fmt::r1_r2: o = wr(b1 >> 4) + "," + wr(b1); break;
    case Fmt::r1_Ir2: o = wr(b1 >> 4) + ",@" + wr(b1); break;
    case Fmt::R2_R1: o = reg(b2) + "," + reg(b1); break;
    case Fmt::IR2_R1: o = reg(b2) + ",@" + reg(b1); break;
    case Fmt::R1_IM: o = reg(b1) + ",#" + hex(b2, 2); break;
    case Fmt::IR1_IM: o = "@" + reg(b1) + ",#" + hex(b2, 2); break;
    case Fmt::r1_R2: o = wr(hi) + "," + reg(b1); break;
    case Fmt::r2_R1: o = reg(b1) + "," + wr(hi); break;
    case Fmt::r1_RA: o = wr(hi) + "," + rel(b1); break;
    case Fmt::cc_RA: o = cc(hi) + rel(b1); break;
    case Fmt::r1_IM: o = wr(hi) + ",#" + hex(b1, 2); break;
    case Fmt::cc_DA:
        e.hatZiel = true; e.ziel = uint16_t(b1 << 8 | b2);
        o = cc(hi) + hex(e.ziel, 4);
        break;
    case Fmt::r1: o = wr(hi); break;
    case Fmt::IM: o = "#" + hex(b1, 2); break;
    case Fmt::DA:
        e.hatZiel = true; e.ziel = uint16_t(b1 << 8 | b2);
        o = hex(e.ziel, 4);
        break;
    case Fmt::r1_Irr2: o = wr(b1 >> 4) + ",@" + wrr(b1); break;
    case Fmt::Irr2_r1: o = "@" + wrr(b1) + "," + wr(b1 >> 4); break;
    case Fmt::Ir1_Irr2: o = "@" + wr(b1 >> 4) + ",@" + wrr(b1); break;
    case Fmt::Irr2_Ir1: o = "@" + wrr(b1) + ",@" + wr(b1 >> 4); break;
    case Fmt::r1_X: o = wr(b1 >> 4) + "," + basis(b2) + "(" + wr(b1) + ")"; break;
    case Fmt::X_r1: o = basis(b2) + "(" + wr(b1) + ")," + wr(b1 >> 4); break;
    case Fmt::Ir1_r2: o = "@" + wr(b1 >> 4) + "," + wr(b1); break;
    case Fmt::IR1_R2: o = "@" + reg(b2) + "," + reg(b1); break;
    }
    e.ruft = in.mn == Mn::CALL;
    e.endet = in.mn == Mn::RET || in.mn == Mn::IRET ||
              (in.mn == Mn::JP && (in.fmt == Fmt::IRR1 || hi == 8)) || (in.mn == Mn::JR && hi == 8);
    e.text = o.empty() ? m : m + " " + o;
    return e;
}

/// Zeile im Debuggerformat: "0123  E6 F8 96   LD P01M,#96H".
inline std::string zeile(const std::function<uint8_t(uint16_t)>& mem, uint16_t pc, int* len = nullptr) {
    const Ergebnis e = disasm(mem, pc);
    char b[64];
    std::string bs;
    for (int i = 0; i < 3; ++i) {
        if (i < e.len) { std::snprintf(b, sizeof b, "%02X ", e.bytes[i]); bs += b; }
        else bs += "   ";
    }
    std::snprintf(b, sizeof b, "%04X  ", pc);
    if (len) *len = e.len;
    return std::string(b) + bs + "  " + e.text;
}

}  // namespace z8dis
