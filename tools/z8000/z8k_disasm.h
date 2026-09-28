/**
 * @file z8k_disasm.h
 * @brief U8001/U8002-Disassembler (Zilog-Syntax), header-only, aus z8k_table.h.
 *
 * Schreibweise (vom Assembler z8k_asm.h wieder lesbar):
 *   Register      R0..R15, RH0..RH7, RL0..RL7, RR0..RR14, RQ0..RQ12
 *   Direktwert    #%ED4D (Byte #%4D, Langwort #%0012ED4D); Anzahlen/Bits dezimal (#3)
 *   Adresse       %1234 (nichtsegmentiert), <<3>>%1234 (segmentiert, lang),
 *                 |<<3>>%12| (segmentiert, kurzer Offset)
 *   indiziert     %1234(R3), <<3>>%1234(R3)
 *   indirekt      @R5 bzw. @RR4;  Basis R5(#%0018) bzw. RR4(#%0018);  R5(R3)
 *   relativ       Zieladresse absolut (%0208 bzw. <<13>>%0208)
 *   JP/JR/RET     Bedingung T wird weggelassen.
 *   Zweitkodierung (Z8K_ALT, LDB Rbd,#data im Langformat) → Mnemonik mit ".L".
 * Unbekannte Kodierung → ".WORD %xxxx" (Länge 2).
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000/z8k_codec.h"

#include <cstdio>
#include <functional>
#include <string>

namespace z8k {

namespace detail {
inline std::string hexN(uint32_t v, int digits) {
    char b[16];
    std::snprintf(b, sizeof b, "%%%0*X", digits, v);
    return b;
}
inline std::string regName(Kind k, unsigned r) {
    char b[8];
    switch (k) {
    case Kind::RB: std::snprintf(b, sizeof b, r < 8 ? "RH%u" : "RL%u", r & 7); break;
    case Kind::RL: std::snprintf(b, sizeof b, "RR%u", r); break;
    case Kind::RQ: std::snprintf(b, sizeof b, "RQ%u", r); break;
    default:       std::snprintf(b, sizeof b, "R%u", r); break;
    }
    return b;
}
/// Adressregister: Rn (nonseg) bzw. RRn (seg).
inline std::string ptrReg(unsigned r, bool seg) { return regName(seg ? Kind::RL : Kind::RW, r); }

inline std::string segText(unsigned seg, unsigned off, bool shortForm) {
    std::string s = "<<" + std::to_string(seg) + ">>" + hexN(off, shortForm ? 2 : 4);
    return shortForm ? "|" + s + "|" : s;
}
} // namespace detail

/// Text eines Operanden.  pcSeg/pcNext: Segment und Offset des Folgebefehls (für RA).
inline std::string formatOperand(const Operand& o, bool seg, unsigned pcSeg, uint16_t pcNext) {
    using namespace detail;
    auto addr = [&]() {
        return seg ? segText(o.seg, o.value, o.shortSeg) : hexN(o.value, 4);
    };
    switch (o.kind) {
    case Kind::RB: case Kind::RW: case Kind::RL: case Kind::RQ: return regName(o.kind, o.reg);
    case Kind::RP:   return ptrReg(o.reg, seg);
    case Kind::IR:   return "@" + ptrReg(o.reg, seg);
    case Kind::IO:   return "@" + regName(Kind::RW, o.reg);
    case Kind::DA:   return addr();
    case Kind::X:    return addr() + "(" + regName(Kind::RW, o.reg) + ")";
    case Kind::BA:   return ptrReg(o.reg, seg) + "(#" + hexN(o.value, 4) + ")";
    case Kind::BX:   return ptrReg(o.reg, seg) + "(" + regName(Kind::RW, o.reg2) + ")";
    case Kind::RA16: case Kind::RA8: case Kind::RA7: case Kind::RA12: {
        uint16_t t = uint16_t(pcNext + o.disp);
        return seg ? segText(pcSeg, t, false) : hexN(t, 4);
    }
    case Kind::IMB:  return "#" + hexN(o.value, 2);
    case Kind::IMW:  return "#" + hexN(o.value, 4);
    case Kind::IML:  return "#" + hexN(o.value, 8);
    case Kind::IM8:  return "#" + hexN(o.value, 2);
    case Kind::RAW:  return "#" + hexN(o.value, o.value > 0xFF ? 4 : 2);
    case Kind::IM4: case Kind::N16: case Kind::BIT: case Kind::LDMN: case Kind::LIT:
    case Kind::SHL: case Kind::SHR:
        return "#" + std::to_string(o.value);
    case Kind::CC:   return ccName(o.value);
    case Kind::FL: {
        std::string s;
        static const char* const n[4] = {"C", "Z", "S", "V"};   // Bit 3..0
        for (int b = 3; b >= 0; --b)
            if (o.value & (1u << b)) { if (!s.empty()) s += ","; s += n[3 - b]; }
        return s;
    }
    case Kind::INT: {
        std::string s;
        if (!(o.value & 2)) s = "VI";
        if (!(o.value & 1)) s += s.empty() ? "NVI" : ",NVI";
        return s;
    }
    case Kind::CTL: { const char* c = ctlName(o.value, seg); return c ? c : "?"; }
    case Kind::FLAGS: return "FLAGS";
    case Kind::PORT: return hexN(o.value, 4);
    case Kind::None: break;
    }
    return "?";
}

/// Mnemonik und Operanden eines dekodierten Befehls als Text ("LD R0,#%ED4D").
inline std::string formatDecoded(const Decoded& d, unsigned pcSeg, uint16_t pcNext) {
    if (!d.insn) return ".WORD " + detail::hexN(d.w[0], 4);
    const Insn& in = *d.insn;
    std::string s = in.mn;
    if (in.has(Z8K_ALT)) s += ".L";
    bool omitT = (std::strcmp(in.mn, "JP") == 0 || std::strcmp(in.mn, "JR") == 0 ||
                  std::strcmp(in.mn, "RET") == 0);
    std::string ops;
    for (int i = 0; i < d.nops; ++i) {
        const Operand& o = d.op[i];
        if (omitT && o.kind == Kind::CC && o.value == 8) continue;
        std::string t = formatOperand(o, d.seg, pcSeg, pcNext);
        if (t.empty()) continue;                       // leere Flag-/Interruptliste
        if (!ops.empty()) ops += ",";
        ops += t;
    }
    if (!ops.empty()) s += " " + ops;
    return s;
}

/// Ergebnis einer Disassemblierzeile.
struct Line {
    int         bytes = 2;         ///< Befehlslänge in Bytes (unbekannt: 2)
    std::string text;              ///< "LD R0,#%ED4D"
    Decoded     dec;               ///< Dekodierergebnis (insn == nullptr: unbekannt)
    bool        hasTarget = false; ///< Sprung-/Aufrufziel bekannt (RA, DA ohne Index)
    uint8_t     targetSeg = 0;
    uint16_t    target = 0;
};

/**
 * Disassembliert einen Befehl an seg:off.  `readWord(off)` liefert das Wort am
 * (geraden) Offset im selben Segment — der Befehlsstrom bleibt im Segment, der
 * Offset läuft über 16 Bit um.
 */
inline Line disasm(const std::function<uint16_t(uint16_t)>& readWord, uint16_t pc, bool seg,
                   uint8_t pcSeg = 0) {
    Line l;
    bool ok = decode([&](int i) { return readWord(uint16_t(pc + 2 * i)); }, seg, l.dec);
    if (!ok) {
        l.dec.insn = nullptr;
        l.bytes = 2;
        l.text = ".WORD " + detail::hexN(l.dec.w[0], 4);
        return l;
    }
    l.bytes = l.dec.bytes();
    uint16_t next = uint16_t(pc + l.bytes);
    l.text = formatDecoded(l.dec, pcSeg, next);
    const Insn& in = *l.dec.insn;
    if (in.has(Z8K_JUMP | Z8K_CALL)) {
        for (int i = 0; i < l.dec.nops; ++i) {
            const Operand& o = l.dec.op[i];
            if (o.kind == Kind::RA8 || o.kind == Kind::RA7 || o.kind == Kind::RA12) {
                l.hasTarget = true; l.targetSeg = pcSeg; l.target = uint16_t(next + o.disp);
            } else if (o.kind == Kind::DA) {
                l.hasTarget = true; l.targetSeg = o.seg; l.target = uint16_t(o.value);
            }
        }
    }
    return l;
}

/// Bequemlichkeit für Tests und Werkzeuge: aus einem Bytepuffer (big-endian) ab base.
inline Line disasmBytes(const uint8_t* buf, size_t len, uint16_t base, uint16_t pc, bool seg,
                        uint8_t pcSeg = 0) {
    auto rd = [&](uint16_t a) -> uint16_t {
        size_t i = size_t(uint16_t(a - base));
        if (i + 1 >= len) return 0;
        return uint16_t((buf[i] << 8) | buf[i + 1]);
    };
    return disasm(rd, pc, seg, pcSeg);
}

} // namespace z8k
