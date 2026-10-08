/**
 * @file z8_asm.h
 * @brief Kleiner Z8-Assembler (header-only) für Testprogramme und den Debugger (`a`).
 *
 * Liest die Schreibweise von tools/z8/z8_disasm.h und die Zilog-Schreibweise:
 *   marke:  LD r3,#12H        ; Kommentar
 *           ADD 45H,@r6       JP NZ,marke    DJNZ r2,marke    LDC r1,@rr4
 *           LD r10,24H(r0)    CALL @rr2      SRP #70H
 *   ORG 1000H   DB 1,2,'Text'   DW marke   name EQU 7FH
 * Zahlen: 12H, 0x12, %12, 0b101 / 101B, dezimal, 'c'.  Ausdrücke: Summen/Differenzen
 * aus Zahlen, Marken und Registernamen (P0 … SPL).
 *
 * Die Kodierung wird nicht von Hand geschrieben: für jeden Befehl werden alle 256 Zeilen der
 * Befehlstabelle mit gleichem Namen durchprobiert, die kürzeste passende gewinnt (so passen
 * Assembler, Disassembler und Kern zwangsläufig zusammen).
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8/z8_table.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace z8asm {

struct Fehler { int zeile; std::string text; };

struct Ergebnis {
    std::map<uint16_t, uint8_t> bild;          ///< Adresse → Byte
    std::vector<Fehler> fehler;
    std::map<std::string, long> marken;
    bool ok() const { return fehler.empty(); }
    /// Bytes ab @p von bis zur höchsten belegten Adresse (Lücken = FFH).
    std::vector<uint8_t> flach(uint16_t von = 0) const {
        std::vector<uint8_t> v;
        if (bild.empty()) return v;
        const unsigned bis = bild.rbegin()->first;
        for (unsigned a = von; a <= bis; ++a) {
            auto it = bild.find(uint16_t(a));
            v.push_back(it == bild.end() ? 0xFF : it->second);
        }
        return v;
    }
};

namespace detail {

inline std::string gross(std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; }
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

enum class K : uint8_t { r, rr, Ir, Irr, R, IR, IM, X, CC, Wert };

struct Op {
    K kind = K::Wert;
    long v = 0;          ///< Wert/Register/Adresse
    unsigned n = 0;      ///< Arbeitsregister
    bool unbekannt = false;   ///< Marke im ersten Durchlauf noch unbekannt
};

inline int sfr(const std::string& u) {
    for (int a = 0; a < 256; ++a) {
        const char* n = z8::sfrName(uint8_t(a));
        if (n && u == n) return a;
    }
    return -1;
}

inline int ccCode(const std::string& u) {
    static const char* const k[] = {"F", "LT", "LE", "ULE", "OV", "MI", "Z", "C",
                                    "T", "GE", "GT", "UGT", "NOV", "PL", "NZ", "NC"};
    for (int i = 0; i < 16; ++i) if (u == k[i]) return i;
    if (u == "EQ") return 6;
    if (u == "NE") return 14;
    if (u == "ULT") return 7;
    if (u == "UGE") return 15;
    return -1;
}

/// "r12" → 12, "rr4" → 4 (mit pair = true); -1 sonst.
inline int arbeitsreg(const std::string& u, bool pair) {
    const size_t p = pair ? 2 : 1;
    if (u.size() <= p || u.size() > p + 2) return -1;
    if (u[0] != 'R' || (pair && u[1] != 'R')) return -1;
    for (size_t i = p; i < u.size(); ++i) if (!std::isdigit((unsigned char)u[i])) return -1;
    const int n = std::atoi(u.c_str() + p);
    return n <= 15 ? n : -1;
}

struct Kontext {
    const std::map<std::string, long>* marken;
    long pc;
    bool letzterDurchlauf;
};

/// Zahl, Marke oder Summe/Differenz.  false bei Unsinn.
inline bool wert(const std::string& s0, const Kontext& k, long& v, bool& unbekannt) {
    std::string s = trim(s0);
    v = 0;
    if (s.empty()) return false;
    long sum = 0;
    int vz = 1;
    size_t i = 0;
    bool erwartet = true;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t') { ++i; continue; }
        if (!erwartet && (c == '+' || c == '-')) { vz = c == '-' ? -1 : 1; erwartet = true; ++i; continue; }
        if (erwartet && c == '-') { vz = -vz; ++i; continue; }
        if (!erwartet) return false;
        // Term
        size_t j = i;
        long t = 0;
        if (c == '\'') {
            if (i + 2 >= s.size() || s[i + 2] != '\'') return false;
            t = (unsigned char)s[i + 1];
            j = i + 3;
        } else if (c == '$') {
            t = k.pc; j = i + 1;
        } else {
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '%' || s[j] == '.')) ++j;
            const std::string tok = s.substr(i, j - i);
            const std::string u = gross(tok);
            char* e = nullptr;
            if (tok[0] == '%') { t = std::strtol(tok.c_str() + 1, &e, 16); if (*e || tok.size() < 2) return false; }
            else if (std::isdigit((unsigned char)tok[0]) && u.back() == 'H') {   // vor 0x/0b: 0B3H ist hex
                t = std::strtol(u.substr(0, u.size() - 1).c_str(), &e, 16); if (*e) return false;
            }
            else if (u.size() > 2 && u[0] == '0' && u[1] == 'X') { t = std::strtol(tok.c_str() + 2, &e, 16); if (*e) return false; }
            else if (u.size() > 2 && u[0] == '0' && u[1] == 'B' && u.find_first_not_of("01", 2) == std::string::npos) {
                t = std::strtol(tok.c_str() + 2, &e, 2); if (*e) return false;
            }
            else if (std::isdigit((unsigned char)tok[0])) {
                if (u.back() == 'B' && u.find_first_not_of("01B") == std::string::npos) {
                    t = std::strtol(u.substr(0, u.size() - 1).c_str(), &e, 2); if (*e) return false;
                } else { t = std::strtol(tok.c_str(), &e, 10); if (*e) return false; }
            } else {
                const int sf = sfr(u);
                if (sf >= 0) t = sf;
                else {
                    auto it = k.marken->find(u);
                    if (it != k.marken->end()) t = it->second;
                    else { if (k.letzterDurchlauf) return false; unbekannt = true; t = 0; }
                }
            }
        }
        sum += vz * t;
        vz = 1;
        erwartet = false;
        i = j;
    }
    if (erwartet) return false;
    v = sum;
    return true;
}

inline bool operand(const std::string& s0, const Kontext& k, Op& o, std::string& err) {
    const std::string s = trim(s0);
    const std::string u = gross(s);
    if (s.empty()) { err = "leerer Operand"; return false; }
    if (s[0] == '#') {
        o.kind = K::IM;
        if (!wert(s.substr(1), k, o.v, o.unbekannt)) { err = "Direktwert '" + s + "'"; return false; }
        return true;
    }
    if (s[0] == '@') {
        const std::string r = gross(trim(s.substr(1)));
        int n;
        if ((n = arbeitsreg(r, true)) >= 0) { o.kind = K::Irr; o.n = unsigned(n); return true; }
        if ((n = arbeitsreg(r, false)) >= 0) { o.kind = K::Ir; o.n = unsigned(n); return true; }
        o.kind = K::IR;
        if (!wert(s.substr(1), k, o.v, o.unbekannt)) { err = "indirekt '" + s + "'"; return false; }
        return true;
    }
    int n;
    if ((n = arbeitsreg(u, true)) >= 0) { o.kind = K::rr; o.n = unsigned(n); return true; }
    if ((n = arbeitsreg(u, false)) >= 0) { o.kind = K::r; o.n = unsigned(n); return true; }
    const size_t lp = s.find('(');
    if (lp != std::string::npos && s.back() == ')') {
        const std::string inner = gross(trim(s.substr(lp + 1, s.size() - lp - 2)));
        const int r = arbeitsreg(inner, false);
        if (r < 0) { err = "Index muss ein Arbeitsregister sein: '" + s + "'"; return false; }
        o.kind = K::X; o.n = unsigned(r);
        if (!wert(s.substr(0, lp), k, o.v, o.unbekannt)) { err = "Indexbasis '" + s + "'"; return false; }
        return true;
    }
    o.kind = K::Wert;
    if (!wert(s, k, o.v, o.unbekannt)) { err = "Operand '" + s + "'"; return false; }
    return true;
}

/// Registerfeld (R): Arbeitsregister → Ex, Wert → 8 Bit.
inline bool feldR(const Op& o, uint8_t& b) {
    if (o.kind == K::r) { b = uint8_t(0xE0 | o.n); return true; }
    if (o.kind == K::Wert && o.v >= 0 && o.v <= 255) { b = uint8_t(o.v); return true; }
    if (o.kind == K::Wert && o.unbekannt) { b = 0; return true; }
    return false;
}
inline bool feldRR(const Op& o, uint8_t& b) {
    if (o.kind == K::rr) { b = uint8_t(0xE0 | o.n); return true; }
    if (o.kind == K::r) return false;
    return feldR(o, b);
}
inline bool feldIR(const Op& o, uint8_t& b) {
    if (o.kind == K::Ir) { b = uint8_t(0xE0 | o.n); return true; }
    if (o.kind == K::IR && ((o.v >= 0 && o.v <= 255) || o.unbekannt)) { b = uint8_t(o.v); return true; }
    return false;
}
inline bool feldIRR(const Op& o, uint8_t& b) {
    if (o.kind == K::Irr) { b = uint8_t(0xE0 | o.n); return true; }
    if (o.kind == K::IR && ((o.v >= 0 && o.v <= 255) || o.unbekannt)) { b = uint8_t(o.v); return true; }
    return false;
}

/// Versucht Opcode @p op mit den Operanden; Bytes nach @p out.
inline bool passt(uint8_t op, const std::vector<Op>& ops, long pc, bool letzter, std::vector<uint8_t>& out, std::string& err) {
    using z8::Fmt;
    const z8::Insn& in = z8::tabelle()[op];
    const unsigned hi = op >> 4;
    out.clear();
    out.push_back(op);
    auto n = ops.size();
    uint8_t a, b;
    auto rel = [&](const Op& o, uint8_t& r) {
        if (o.kind != K::Wert) return false;
        const long d = o.v - (pc + 2);
        if (!o.unbekannt && (d < -128 || d > 127)) {
            if (letzter) err = "Sprungziel ausser Reichweite";
            return false;
        }
        r = uint8_t(d);
        return true;
    };
    auto ccOf = [&](size_t i, unsigned& c) -> bool {
        if (ops[i].kind != K::CC) return false;
        c = unsigned(ops[i].v); return true;
    };
    switch (in.fmt) {
    case Fmt::Keins: return n == 0;
    case Fmt::R1: if (n != 1 || !feldR(ops[0], a)) return false; out.push_back(a); return true;
    case Fmt::IR1: if (n != 1 || !feldIR(ops[0], a)) return false; out.push_back(a); return true;
    case Fmt::RR1: if (n != 1 || !feldRR(ops[0], a)) return false; out.push_back(a); return true;
    case Fmt::IRR1: if (n != 1 || !feldIRR(ops[0], a)) return false; out.push_back(a); return true;
    case Fmt::r1_r2: if (n != 2 || ops[0].kind != K::r || ops[1].kind != K::r) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); return true;
    case Fmt::r1_Ir2: if (n != 2 || ops[0].kind != K::r || ops[1].kind != K::Ir) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); return true;
    case Fmt::R2_R1: if (n != 2 || !feldR(ops[0], a) || !feldR(ops[1], b)) return false;
        out.push_back(b); out.push_back(a); return true;
    case Fmt::IR2_R1: if (n != 2 || !feldR(ops[0], a) || !feldIR(ops[1], b)) return false;
        out.push_back(b); out.push_back(a); return true;
    case Fmt::R1_IM: if (n != 2 || !feldR(ops[0], a) || ops[1].kind != K::IM) return false;
        out.push_back(a); out.push_back(uint8_t(ops[1].v)); return true;
    case Fmt::IR1_IM: if (n != 2 || !feldIR(ops[0], a) || ops[1].kind != K::IM) return false;
        out.push_back(a); out.push_back(uint8_t(ops[1].v)); return true;
    case Fmt::r1_R2: if (n != 2 || ops[0].kind != K::r || ops[0].n != hi || !feldR(ops[1], a)) return false;
        out.push_back(a); return true;
    case Fmt::r2_R1: if (n != 2 || ops[1].kind != K::r || ops[1].n != hi || !feldR(ops[0], a)) return false;
        out.push_back(a); return true;
    case Fmt::r1_RA: if (n != 2 || ops[0].kind != K::r || ops[0].n != hi || !rel(ops[1], a)) return false;
        out.push_back(a); return true;
    case Fmt::cc_RA: {
        unsigned c = 8; size_t z = 0;
        if (n == 2) { if (!ccOf(0, c)) return false; z = 1; } else if (n != 1) return false;
        if (c != hi || !rel(ops[z], a)) return false;
        out.push_back(a); return true;
    }
    case Fmt::r1_IM: if (n != 2 || ops[0].kind != K::r || ops[0].n != hi || ops[1].kind != K::IM) return false;
        out.push_back(uint8_t(ops[1].v)); return true;
    case Fmt::cc_DA: {
        unsigned c = 8; size_t z = 0;
        if (n == 2) { if (!ccOf(0, c)) return false; z = 1; } else if (n != 1) return false;
        if (c != hi || ops[z].kind != K::Wert) return false;
        out.push_back(uint8_t(ops[z].v >> 8)); out.push_back(uint8_t(ops[z].v)); return true;
    }
    case Fmt::r1: return n == 1 && ops[0].kind == K::r && ops[0].n == hi;
    case Fmt::IM: if (n != 1 || ops[0].kind != K::IM) return false; out.push_back(uint8_t(ops[0].v)); return true;
    case Fmt::DA: if (n != 1 || ops[0].kind != K::Wert) return false;
        out.push_back(uint8_t(ops[0].v >> 8)); out.push_back(uint8_t(ops[0].v)); return true;
    case Fmt::r1_Irr2: if (n != 2 || ops[0].kind != K::r || ops[1].kind != K::Irr) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); return true;
    case Fmt::Irr2_r1: if (n != 2 || ops[0].kind != K::Irr || ops[1].kind != K::r) return false;
        out.push_back(uint8_t(ops[1].n << 4 | ops[0].n)); return true;
    case Fmt::Ir1_Irr2: if (n != 2 || ops[0].kind != K::Ir || ops[1].kind != K::Irr) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); return true;
    case Fmt::Irr2_Ir1: if (n != 2 || ops[0].kind != K::Irr || ops[1].kind != K::Ir) return false;
        out.push_back(uint8_t(ops[1].n << 4 | ops[0].n)); return true;
    case Fmt::r1_X: if (n != 2 || ops[0].kind != K::r || ops[1].kind != K::X) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); out.push_back(uint8_t(ops[1].v)); return true;
    case Fmt::X_r1: if (n != 2 || ops[0].kind != K::X || ops[1].kind != K::r) return false;
        out.push_back(uint8_t(ops[1].n << 4 | ops[0].n)); out.push_back(uint8_t(ops[0].v)); return true;
    case Fmt::Ir1_r2: if (n != 2 || ops[0].kind != K::Ir || ops[1].kind != K::r) return false;
        out.push_back(uint8_t(ops[0].n << 4 | ops[1].n)); return true;
    case Fmt::IR1_R2: if (n != 2 || !feldIR(ops[0], a) || !feldR(ops[1], b)) return false;
        out.push_back(b); out.push_back(a); return true;
    }
    return false;
}

inline std::vector<std::string> teile(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    bool q = false;
    for (char c : s) {
        if (c == '\'') q = !q;
        if (c == ',' && !q) { v.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty() || !v.empty()) v.push_back(trim(cur));
    return v;
}

}  // namespace detail

/// Eine Zeile (ohne Marke/Direktive) zu Bytes; für den Debugger (`a`).
inline bool befehl(const std::string& text, uint16_t pc, std::vector<uint8_t>& out, std::string& err,
                   const std::map<std::string, long>* marken = nullptr, bool letzter = true) {
    using namespace detail;
    static const std::map<std::string, long> leer;
    Kontext k{marken ? marken : &leer, pc, letzter};
    const std::string t = trim(text);
    const size_t sp = t.find_first_of(" \t");
    const std::string mn = gross(sp == std::string::npos ? t : t.substr(0, sp));
    const std::string rest = sp == std::string::npos ? "" : trim(t.substr(sp));
    std::vector<Op> ops;
    std::vector<std::string> roh = rest.empty() ? std::vector<std::string>{} : teile(rest);
    const bool sprung = mn == "JP" || mn == "JR";
    for (size_t i = 0; i < roh.size(); ++i) {
        Op o;
        if (sprung && i == 0 && roh.size() == 2) {
            const int c = ccCode(gross(roh[0]));
            if (c >= 0) { o.kind = K::CC; o.v = c; ops.push_back(o); continue; }
        }
        if (!operand(roh[i], k, o, err)) return false;
        ops.push_back(o);
    }
    bool gefunden = false;
    std::vector<uint8_t> best, cand;
    std::string e2;
    for (int op = 0; op < 256; ++op) {
        const z8::Insn& in = z8::tabelle()[uint8_t(op)];
        if (in.mn == z8::Mn::Ungueltig || mn != z8::mnName(in.mn)) continue;
        gefunden = true;
        if (passt(uint8_t(op), ops, pc, letzter, cand, e2) && (best.empty() || cand.size() < best.size()))
            best = cand;
    }
    if (!gefunden) { err = "unbekannter Befehl '" + mn + "'"; return false; }
    if (best.empty()) { err = e2.empty() ? "Operanden passen nicht zu " + mn + ": '" + rest + "'" : e2; return false; }
    out = best;
    return true;
}

/// Ganzer Quelltext (zwei Durchläufe).
inline Ergebnis assemble(const std::string& src) {
    using namespace detail;
    Ergebnis r;
    std::vector<std::string> zeilen;
    {
        std::string cur;
        for (char c : src) { if (c == '\n') { zeilen.push_back(cur); cur.clear(); } else cur += c; }
        zeilen.push_back(cur);
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool letzter = pass == 1;
        long pc = 0;
        r.bild.clear();
        if (letzter) r.fehler.clear();
        for (size_t zi = 0; zi < zeilen.size(); ++zi) {
            std::string z = zeilen[zi];
            // Kommentar (ausserhalb von '…')
            bool q = false;
            for (size_t i = 0; i < z.size(); ++i) {
                if (z[i] == '\'') q = !q;
                if (z[i] == ';' && !q) { z = z.substr(0, i); break; }
            }
            z = trim(z);
            if (z.empty()) continue;
            auto fehler = [&](const std::string& t) { if (letzter) r.fehler.push_back({int(zi + 1), t}); };
            // Marke
            const size_t col = z.find(':');
            if (col != std::string::npos && z.find('\'') > col) {
                const std::string m = gross(trim(z.substr(0, col)));
                bool gut = !m.empty() && (std::isalpha((unsigned char)m[0]) || m[0] == '_');
                for (char c : m) gut = gut && (std::isalnum((unsigned char)c) || c == '_' || c == '.');
                if (gut) { r.marken[m] = pc; z = trim(z.substr(col + 1)); if (z.empty()) continue; }
            }
            Kontext k{&r.marken, pc, letzter};
            std::string w1 = z, rest;
            const size_t sp = z.find_first_of(" \t");
            if (sp != std::string::npos) { w1 = z.substr(0, sp); rest = trim(z.substr(sp)); }
            const std::string u1 = gross(w1);
            // name EQU wert
            {
                const size_t sp2 = rest.find_first_of(" \t");
                const std::string w2 = gross(sp2 == std::string::npos ? rest : rest.substr(0, sp2));
                if (w2 == "EQU") {
                    long v; bool unb = false;
                    if (!wert(sp2 == std::string::npos ? "" : rest.substr(sp2), k, v, unb)) fehler("EQU ohne Wert");
                    else r.marken[u1] = v;
                    continue;
                }
            }
            if (u1 == "ORG") {
                long v; bool unb = false;
                if (!wert(rest, k, v, unb)) fehler("ORG ohne Adresse"); else pc = v;
                continue;
            }
            if (u1 == "DB" || u1 == "DEFB" || u1 == "DW" || u1 == "DEFW") {
                const bool wort = u1 == "DW" || u1 == "DEFW";
                for (const std::string& t : teile(rest)) {
                    if (!wort && t.size() >= 2 && t.front() == '\'' && t.back() == '\'' && t.size() != 3) {
                        for (size_t i = 1; i + 1 < t.size(); ++i) r.bild[uint16_t(pc++)] = uint8_t(t[i]);
                        continue;
                    }
                    long v; bool unb = false;
                    if (!wert(t, k, v, unb)) { fehler("Datum '" + t + "'"); continue; }
                    if (wort) { r.bild[uint16_t(pc++)] = uint8_t(v >> 8); r.bild[uint16_t(pc++)] = uint8_t(v); }
                    else r.bild[uint16_t(pc++)] = uint8_t(v);
                }
                continue;
            }
            std::vector<uint8_t> bytes;
            std::string err;
            if (!befehl(z, uint16_t(pc), bytes, err, &r.marken, letzter)) {
                fehler(err);
                // im ersten Durchlauf dennoch Platz lassen (3 Byte schätzen)
                if (!letzter) pc += 3;
                continue;
            }
            for (uint8_t b : bytes) r.bild[uint16_t(pc++)] = b;
        }
    }
    return r;
}

}  // namespace z8asm
