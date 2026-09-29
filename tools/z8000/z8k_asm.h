/**
 * @file z8k_asm.h
 * @brief z8kasm — Zwei-Pass-Assembler für U8001/U8002 (Zilog-Syntax), header-only.
 *
 * Befehle kommen ausschließlich aus z8k_table.h: für eine Mnemonik werden die
 * Tabellenzeilen der Reihe nach probiert, die erste passende kodiert.  Die
 * Länge eines Befehls hängt nur von Zeile und Schreibweise ab (nie von einem
 * Symbolwert), deshalb genügen zwei Durchläufe.
 *
 * Quelltext:
 *   [marke:] [befehl|direktive operanden] [; kommentar | !kommentar!]
 *   name EQU ausdruck   (auch  name: EQU …  und  name := …)
 * Direktiven: ORG a · DB/BVAL … · DW/WVAL … · DL/LVAL … · DS/BLKB n ·
 *   EVEN · ALIGN n · SEG/SEGMENTED · NONSEG/NONSEGMENTED · END
 * Zahlen: %1234 (hex), %(2)1010 (Basis 2..16), 0x1234, 1234H, 0b1010, 1010B,
 *   dezimal, 'A'/'AB', $ = aktuelle Adresse.  Operatoren: + - * / MOD & | ^
 *   AND OR XOR ~ NOT, Klammern, SEG(x), OFF(x).  <<s>>o = segmentierte Adresse
 *   (Wert s<<24 | o, wie im Registerpaar).  Groß-/Kleinschreibung egal.
 * Operanden: siehe z8k_disasm.h (dieselbe Schreibweise).  Kurzer segmentierter
 *   Offset nur ausdrücklich: |<<3>>%12|.  JP/JR/RET ohne Bedingung = T.
 *   INC/DEC, Rotieren, Schieben ohne Anzahl = 1.  LDB.L = Langform LDB Rbd,#data.
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000/z8k_codec.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace z8k {

struct AsmOptions {
    bool seg = false;              ///< Startmodus (Direktiven SEG/NONSEG schalten um)
    /// Kompatibilität zu z8001asm.py: @Rn und @RRn (ebenso Basis Rn/RRn) gelten in
    /// beiden Modi, kodiert wird die Registernummer, wie sie dasteht — auch ungerade.
    bool laxPointers = false;
};

struct AsmMessage { int line = 0; std::string text; };

struct ListLine {
    int      line = 0;             ///< 1-basiert
    bool     hasAddr = false;
    uint8_t  seg = 0;
    uint16_t off = 0;
    bool     segMode = false;
    std::vector<uint8_t> bytes;
    std::string src;
};

struct AsmResult {
    std::vector<AsmMessage> errors;
    std::map<uint32_t, uint8_t> image;       ///< lineare Adresse (seg<<16 | off) → Byte
    std::vector<ListLine> listing;
    std::map<std::string, int64_t> symbols;  ///< Namen in Großbuchstaben
    /// Marken (Adresse eines Befehls/Datums) mit Segment — ohne EQU-Konstanten.
    /// Nichtsegmentiert steht hier das Segment 0 (`formatSymbols` setzt es ein).
    std::map<std::string, std::pair<bool, uint32_t>> labels;   ///< Name → (segmentiert, seg<<16|off)
    bool ok() const { return errors.empty(); }

    /// Zusammenhängendes Abbild von der kleinsten bis zur größten belegten Adresse
    /// (Lücken = 0).  base = lineare Startadresse.
    std::vector<uint8_t> flat(uint32_t* base = nullptr) const {
        std::vector<uint8_t> out;
        if (image.empty()) { if (base) *base = 0; return out; }
        uint32_t lo = image.begin()->first, hi = image.rbegin()->first;
        if (base) *base = lo;
        out.assign(size_t(hi - lo + 1), 0);
        for (auto& kv : image) out[kv.first - lo] = kv.second;
        return out;
    }
};

namespace asmdetail {

inline std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}
inline bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?'; }
inline bool identChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?'; }

/// Registername → Klasse ('B','W','L','Q') und Kodierung; false = kein Register.
inline bool parseReg(const std::string& up, char& cls, int& num, std::string* err = nullptr) {
    auto digits = [&](size_t from, int& v) {
        if (from >= up.size()) return false;
        v = 0;
        for (size_t i = from; i < up.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(up[i]))) return false;
            v = v * 10 + (up[i] - '0');
            if (v > 99) return false;
        }
        return true;
    };
    int v = 0;
    if (up.size() >= 3 && up[0] == 'R' && up[1] == 'Q' && digits(2, v)) {
        if (v > 12 || (v & 3)) { if (err) *err = "ungueltiges Quadregister " + up; return false; }
        cls = 'Q'; num = v; return true;
    }
    if (up.size() >= 3 && up[0] == 'R' && up[1] == 'R' && digits(2, v)) {
        if (v > 14 || (v & 1)) { if (err) *err = "ungueltiges Registerpaar " + up + " (nur gerade RR0..RR14)"; return false; }
        cls = 'L'; num = v; return true;
    }
    if (up.size() >= 3 && up[0] == 'R' && (up[1] == 'H' || up[1] == 'L') && digits(2, v)) {
        if (v > 7) { if (err) *err = "ungueltiges Byteregister " + up; return false; }
        cls = 'B'; num = (up[1] == 'H') ? v : v + 8; return true;
    }
    if (up.size() >= 2 && up[0] == 'R' && digits(1, v)) {
        if (v > 15) { if (err) *err = "ungueltiges Register " + up; return false; }
        cls = 'W'; num = v; return true;
    }
    return false;
}

/// Symbolzugriff des Ausdrucksauswerters: Name (groß) → Wert; false = unbekannt.
using SymFn = std::function<bool(const std::string&, int64_t&)>;

/// Rekursiv-absteigender Ausdrucksauswerter.
class Expr {
public:
    Expr(const std::string& s, const SymFn& sym, int64_t dollar)
        : s_(s), sym_(sym), dollar_(dollar) {}

    /// Wertet den ganzen Text aus.  undefined = ein Symbol fehlte (Wert dann 0).
    bool eval(int64_t& v, bool& undefined, std::string& err) {
        undef_ = false; err_.clear(); p_ = 0;
        v = orExpr();
        skip();
        if (err_.empty() && p_ < s_.size()) err_ = "unerwartetes Zeichen '" + std::string(1, s_[p_]) + "'";
        undefined = undef_;
        err = err_;
        return err_.empty();
    }

private:
    void skip() { while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) ++p_; }
    bool kw(const char* w) {   // Schlüsselwort-Operator (AND, MOD …)
        skip();
        size_t n = std::strlen(w);
        if (p_ + n > s_.size()) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::toupper(static_cast<unsigned char>(s_[p_ + i])) != w[i]) return false;
        if (p_ + n < s_.size() && identChar(s_[p_ + n])) return false;
        p_ += n; return true;
    }
    bool ch(char c) { skip(); if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; } return false; }
    void fail(const std::string& m) { if (err_.empty()) err_ = m; }

    int64_t orExpr() {
        int64_t v = xorExpr();
        for (;;) {
            if (kw("OR") || (peekOp('|'))) v |= xorExpr();
            else return v;
        }
    }
    bool peekOp(char c) { skip(); if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; } return false; }
    int64_t xorExpr() {
        int64_t v = andExpr();
        for (;;) {
            if (kw("XOR") || peekOp('^')) v ^= andExpr();
            else return v;
        }
    }
    int64_t andExpr() {
        int64_t v = addExpr();
        for (;;) {
            if (kw("AND") || peekOp('&')) v &= addExpr();
            else return v;
        }
    }
    int64_t addExpr() {
        int64_t v = mulExpr();
        for (;;) {
            if (ch('+')) v += mulExpr();
            else if (ch('-')) v -= mulExpr();
            else return v;
        }
    }
    int64_t mulExpr() {
        int64_t v = unary();
        for (;;) {
            if (ch('*')) v *= unary();
            else if (ch('/')) { int64_t r = unary(); if (r == 0) { fail("Division durch 0"); r = 1; } v /= r; }
            else if (kw("MOD")) { int64_t r = unary(); if (r == 0) { fail("Division durch 0"); r = 1; } v %= r; }
            else return v;
        }
    }
    int64_t unary() {
        if (ch('-')) return -unary();
        if (ch('+')) return unary();
        if (ch('~') || kw("NOT")) return ~unary();
        return primary();
    }
    int64_t number() {
        // Aufrufer steht auf einer Ziffer oder '%'.
        size_t b = p_;
        if (s_[p_] == '%') {
            ++p_;
            int base = 16;
            if (p_ < s_.size() && s_[p_] == '(') {
                ++p_; int bb = 0;
                while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) bb = bb * 10 + (s_[p_++] - '0');
                if (!ch(')') || bb < 2 || bb > 16) { fail("ungueltige Basisangabe"); return 0; }
                base = bb;
            }
            return digitsIn(base, "%");
        }
        std::string tok;
        while (p_ < s_.size() && std::isalnum(static_cast<unsigned char>(s_[p_]))) tok += char(std::toupper(static_cast<unsigned char>(s_[p_++])));
        int base = 10; std::string d = tok;
        if (tok.size() > 2 && tok[0] == '0' && tok[1] == 'X') { base = 16; d = tok.substr(2); }
        else if (!tok.empty() && tok.back() == 'H') { base = 16; d = tok.substr(0, tok.size() - 1); }
        else if (tok.size() > 2 && tok[0] == '0' && tok[1] == 'B' && tok.find_first_not_of("01", 2) == std::string::npos) { base = 2; d = tok.substr(2); }
        else if (tok.size() > 1 && tok.back() == 'B' && tok.find_first_not_of("01") == tok.size() - 1) { base = 2; d = tok.substr(0, tok.size() - 1); }
        int64_t v = 0;
        if (d.empty()) { fail("leere Zahl"); return 0; }
        for (char c : d) {
            int dv = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
            if (dv >= base) { p_ = b; fail("ungueltige Zahl '" + tok + "'"); return 0; }
            v = v * base + dv;
        }
        return v;
    }
    int64_t digitsIn(int base, const char* what) {
        int64_t v = 0; size_t n = 0;
        while (p_ < s_.size()) {
            char c = char(std::toupper(static_cast<unsigned char>(s_[p_])));
            int dv = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
            if (dv >= base) break;
            v = v * base + dv; ++p_; ++n;
        }
        if (n == 0) fail(std::string("Ziffern fehlen nach ") + what);
        return v;
    }
    int64_t primary() {
        skip();
        if (p_ >= s_.size()) { fail("Ausdruck fehlt"); return 0; }
        char c = s_[p_];
        if (c == '(') { ++p_; int64_t v = orExpr(); if (!ch(')')) fail("')' fehlt"); return v; }
        if (c == '<' && p_ + 1 < s_.size() && s_[p_ + 1] == '<') {
            p_ += 2;
            int64_t sg = orExpr();
            skip();
            if (!(p_ + 1 < s_.size() && s_[p_] == '>' && s_[p_ + 1] == '>')) { fail("'>>' fehlt"); return 0; }
            p_ += 2;
            int64_t off = unary();
            if (sg < 0 || sg > 127) fail("Segmentnummer ausserhalb 0..127");
            if (off < 0 || off > 0xFFFF) fail("Offset ausserhalb 0..%FFFF");
            return (int64_t(sg & 0x7F) << 24) | (off & 0xFFFF);
        }
        if (c == '$' ) { ++p_; return dollar_; }
        if (c == '%' || std::isdigit(static_cast<unsigned char>(c))) return number();
        if (c == '\'' || c == '"') {
            char q = c; ++p_; int64_t v = 0; int n = 0;
            while (p_ < s_.size() && s_[p_] != q) { v = (v << 8) | uint8_t(s_[p_++]); ++n; }
            if (p_ >= s_.size()) { fail("Zeichenkonstante nicht abgeschlossen"); return 0; }
            ++p_;
            if (n == 0 || n > 4) fail("Zeichenkonstante mit 1..4 Zeichen erwartet");
            return v;
        }
        if (identStart(c)) {
            std::string id;
            while (p_ < s_.size() && identChar(s_[p_])) id += char(std::toupper(static_cast<unsigned char>(s_[p_++])));
            if ((id == "SEG" || id == "OFF") && ch('(')) {
                int64_t v = orExpr();
                if (!ch(')')) fail("')' fehlt");
                return id == "SEG" ? ((v >> 24) & 0x7F) : (v & 0xFFFF);
            }
            int64_t v = 0;
            if (sym_ && sym_(id, v)) return v;
            undef_ = true;
            if (!sym_) fail("unbekanntes Symbol " + id);
            return 0;
        }
        fail("unerwartetes Zeichen '" + std::string(1, c) + "'");
        ++p_;
        return 0;
    }

    const std::string& s_;
    const SymFn& sym_;
    int64_t dollar_;
    size_t p_ = 0;
    bool undef_ = false;
    std::string err_;
};

/// Ein syntaktisch zerlegter Operand.
struct PArg {
    enum T { Reg, Ind, Imm, Addr, Idx, Base, BaseIdx } t = Addr;
    char cls = 0;          ///< Registerklasse (Reg/Ind/Base/BaseIdx: Basis)
    int  reg = 0;
    int  reg2 = 0;         ///< BaseIdx: Index (Wortregister); Idx: Index
    bool shortSeg = false; ///< |…|
    std::string expr;      ///< Imm/Addr/Idx: Adresse; Base: Verschiebung
    std::string text;      ///< Rohtext in Großbuchstaben (Schlüsselwörter)
};

/// Operandentext zerlegen.  false + err bei Syntaxfehler.
inline bool parseArg(const std::string& raw, PArg& a, std::string& err) {
    std::string s = trim(raw);
    a = PArg{};
    a.text = upper(s);
    if (s.empty()) { err = "leerer Operand"; return false; }
    char cls; int num;
    std::string rerr;
    if (s[0] == '#') { a.t = PArg::Imm; a.expr = s.substr(1); return true; }
    if (s[0] == '@') {
        std::string r = upper(trim(s.substr(1)));
        if (!parseReg(r, cls, num, &rerr)) { err = rerr.empty() ? "Register nach @ erwartet" : rerr; return false; }
        if (cls != 'W' && cls != 'L') { err = "@ braucht Rn oder RRn"; return false; }
        a.t = PArg::Ind; a.cls = cls; a.reg = num; return true;
    }
    if (parseReg(a.text, cls, num, &rerr)) { a.t = PArg::Reg; a.cls = cls; a.reg = num; return true; }
    if (!rerr.empty()) { err = rerr; return false; }
    // Rn(…) / RRn(…) — Basis
    size_t lp = s.find('(');
    if (lp != std::string::npos && s.back() == ')') {
        std::string head = upper(trim(s.substr(0, lp)));
        std::string inner = trim(s.substr(lp + 1, s.size() - lp - 2));
        std::string herr;
        if (parseReg(head, cls, num, &herr)) {
            if (cls != 'W' && cls != 'L') { err = "Basis muss Rn oder RRn sein"; return false; }
            a.cls = cls; a.reg = num;
            if (!inner.empty() && inner[0] == '#') { a.t = PArg::Base; a.expr = inner.substr(1); return true; }
            char c2; int n2;
            if (parseReg(upper(inner), c2, n2, &herr) && c2 == 'W') { a.t = PArg::BaseIdx; a.reg2 = n2; return true; }
            err = "Basis: Rn(#disp) oder Rn(Rm) erwartet"; return false;
        }
        // Adresse(Rn) — indiziert, die Klammer muss das letzte Glied sein
        char c2; int n2; std::string ierr;
        if (parseReg(upper(inner), c2, n2, &ierr)) {
            if (c2 != 'W') { err = "Index muss ein Wortregister sein"; return false; }
            std::string ad = trim(s.substr(0, lp));
            if (ad.empty()) { err = "Adresse vor (Rn) fehlt"; return false; }
            a.t = PArg::Idx; a.reg2 = n2;
            if (ad.size() >= 2 && ad.front() == '|' && ad.back() == '|') { a.shortSeg = true; ad = ad.substr(1, ad.size() - 2); }
            a.expr = ad;
            return true;
        }
    }
    a.t = PArg::Addr;
    if (s.size() >= 2 && s.front() == '|' && s.back() == '|') { a.shortSeg = true; s = s.substr(1, s.size() - 2); }
    a.expr = s;
    return true;
}

/// Operanden an Kommas teilen (nicht in Klammern oder Anführungszeichen).
inline std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur; int depth = 0; char q = 0;
    for (char c : s) {
        if (q) { cur += c; if (c == q) q = 0; continue; }
        if (c == '\'' || c == '"') { q = c; cur += c; continue; }
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

/// Mnemonik → Tabellenzeilen.
inline const std::unordered_map<std::string, std::vector<int>>& mnemonicIndex() {
    static const std::unordered_map<std::string, std::vector<int>> idx = [] {
        std::unordered_map<std::string, std::vector<int>> m;
        for (auto& in : Table::get().insns()) m[in.mn].push_back(in.index);
        return m;
    }();
    return idx;
}

/// Kontext eines Befehls beim Binden der Operanden.
struct BindCtx {
    bool seg = false;
    uint8_t pcSeg = 0;
    uint16_t pc = 0;          ///< Adresse des Befehls
    const SymFn* sym = nullptr;
    bool final = true;        ///< Pass 2: Werte prüfen; Pass 1: Undefiniertes tolerieren
    bool laxPtr = false;      ///< AsmOptions::laxPointers
};

inline int instrWords(const Insn& in, const Operand* ops, bool seg) {
    int n = in.hasW1 ? 2 : 1;
    for (int i = 0; i < in.nops; ++i) {
        switch (in.op[i].kind) {
        case Kind::DA: case Kind::X: n += (!seg || ops[i].shortSeg) ? 1 : 2; break;
        case Kind::BA: case Kind::RA16: case Kind::IMB: case Kind::IMW: case Kind::PORT: n += 1; break;
        case Kind::IML: n += 2; break;
        default: break;
        }
    }
    return n;
}

/**
 * Operanden an eine Tabellenzeile binden.  Liefert false + Fehlertext, wenn
 * die Zeile nicht passt (`mismatch=true`: Form passt nicht — nächste Zeile
 * probieren) oder ein Wert falsch ist (`mismatch=false`: Fehler melden).
 */
inline bool bindRow(const Insn& in, std::vector<PArg> args, const BindCtx& cx,
                    Operand* ops, std::string& err, bool& mismatch) {
    mismatch = true;
    // Flag- und Interruptlisten: alle Argumente sind Schlüsselwörter
    if (in.nops == 1 && (in.op[0].kind == Kind::FL || in.op[0].kind == Kind::INT)) {
        uint32_t v = in.op[0].kind == Kind::FL ? 0 : 3;
        for (auto& a : args) {
            const std::string& k = a.text;
            if (in.op[0].kind == Kind::FL) {
                if (k == "C") v |= 8; else if (k == "Z") v |= 4; else if (k == "S") v |= 2;
                else if (k == "V" || k == "P" || k == "P/V") v |= 1;
                else { err = "Flag C, Z, S, P/V erwartet: " + k; return false; }
            } else {
                if (k == "VI") v &= ~2u; else if (k == "NVI") v &= ~1u;
                else { err = "VI oder NVI erwartet: " + k; return false; }
            }
        }
        ops[0].kind = in.op[0].kind; ops[0].value = v;
        mismatch = false;
        return true;
    }
    // weggelassene Bedingung (JP/JR/RET) und weggelassene Anzahl (= 1)
    if (int(args.size()) == in.nops - 1) {
        if (in.nops >= 1 && in.op[0].kind == Kind::CC &&
            (std::strcmp(in.mn, "JP") == 0 || std::strcmp(in.mn, "JR") == 0 || std::strcmp(in.mn, "RET") == 0)) {
            PArg t; t.t = PArg::Addr; t.text = "T"; t.expr = "T";
            args.insert(args.begin(), t);
        } else {
            Kind lk = in.op[in.nops - 1].kind;
            if (lk == Kind::N16 || lk == Kind::SHL || lk == Kind::SHR || lk == Kind::LIT) {
                PArg one; one.t = PArg::Imm; one.expr = "1"; one.text = "#1";
                args.push_back(one);
            }
        }
    }
    if (int(args.size()) != in.nops) { err = "falsche Operandenzahl"; return false; }

    auto evalExpr = [&](const std::string& e, int64_t& v) -> bool {
        bool undef = false; std::string eerr;
        int64_t dollar = cx.seg ? int64_t(segAddr(cx.pcSeg, cx.pc)) : cx.pc;
        SymFn none;
        Expr x(e, cx.sym ? *cx.sym : none, dollar);
        if (!x.eval(v, undef, eerr)) { err = eerr; mismatch = false; return false; }
        if (undef) {
            if (cx.final) { err = "unbekanntes Symbol in '" + trim(e) + "'"; mismatch = false; return false; }
            v = 0;
        }
        return true;
    };
    auto valueErr = [&](const std::string& m) { err = m; mismatch = false; return false; };
    const bool B = in.has(Z8K_B);
    const bool Lw = in.has(Z8K_L);

    for (int i = 0; i < in.nops; ++i) {
        const OpSpec& s = in.op[i];
        const PArg& a = args[size_t(i)];
        Operand& o = ops[i];
        o = Operand{};
        o.kind = s.kind;
        auto wantReg = [&](char cls) {
            if (a.t != PArg::Reg || a.cls != cls) return false;
            o.reg = uint8_t(a.reg); return true;
        };
        switch (s.kind) {
        case Kind::RB: if (!wantReg('B')) { err = "Byteregister erwartet"; return false; } break;
        case Kind::RW: if (!wantReg('W')) { err = "Wortregister erwartet"; return false; } break;
        case Kind::RL: if (!wantReg('L')) { err = "Registerpaar RRn erwartet"; return false; } break;
        case Kind::RQ: if (!wantReg('Q')) { err = "Quadregister RQn erwartet"; return false; } break;
        case Kind::RP: if (!wantReg(cx.seg ? 'L' : 'W')) { err = cx.seg ? "RRn erwartet" : "Rn erwartet"; return false; } break;
        case Kind::IR:
            if (a.t != PArg::Ind || (a.cls != (cx.seg ? 'L' : 'W') && !cx.laxPtr)) {
                err = cx.seg ? "@RRn erwartet (segmentiert)" : "@Rn erwartet (nichtsegmentiert)"; return false;
            }
            if (a.reg == 0) return valueErr("R0/RR0 ist als Zeiger nicht erlaubt");
            o.reg = uint8_t(a.reg); break;
        case Kind::IO:
            if (a.t != PArg::Ind || a.cls != 'W') { err = "@Rn als Port erwartet"; return false; }
            o.reg = uint8_t(a.reg); break;
        case Kind::DA: case Kind::X: {
            if (s.kind == Kind::DA && a.t != PArg::Addr) { err = "Adresse erwartet"; return false; }
            if (s.kind == Kind::X && a.t != PArg::Idx) { err = "Adresse(Rn) erwartet"; return false; }
            if (s.kind == Kind::X) {
                if (a.reg2 == 0) return valueErr("R0 ist als Index nicht erlaubt");
                o.reg = uint8_t(a.reg2);
            }
            if (!cx.seg && a.shortSeg) return valueErr("|…| (kurzer Offset) nur segmentiert");
            int64_t v;
            if (!evalExpr(a.expr, v)) return false;
            o.shortSeg = a.shortSeg;
            if (!cx.seg) {
                if (v < -32768 || v > 0xFFFF) { if (cx.final) return valueErr("Adresse ausserhalb 16 Bit"); v = 0; }
                o.value = uint32_t(v) & 0xFFFF;
            } else {
                if (v < 0 || (v & ~int64_t(0x7F00FFFF))) { if (cx.final) return valueErr("keine segmentierte Adresse (<<s>>offset)"); v = 0; }
                o.seg = uint8_t((v >> 24) & 0x7F);
                o.value = uint32_t(v & 0xFFFF);
                if (a.shortSeg && o.value > 0xFF) { if (cx.final) return valueErr("kurzer Offset > %FF"); o.value = 0; }
            }
            break;
        }
        case Kind::BA: case Kind::BX: {
            PArg::T want = s.kind == Kind::BA ? PArg::Base : PArg::BaseIdx;
            if (a.t != want) { err = s.kind == Kind::BA ? "Rn(#disp) erwartet" : "Rn(Rm) erwartet"; return false; }
            if (a.cls != (cx.seg ? 'L' : 'W') && !cx.laxPtr) { err = cx.seg ? "Basis RRn erwartet" : "Basis Rn erwartet"; return false; }
            if (a.reg == 0) return valueErr("R0/RR0 ist als Basis nicht erlaubt");
            o.reg = uint8_t(a.reg);
            if (s.kind == Kind::BX) o.reg2 = uint8_t(a.reg2);
            else {
                int64_t v;
                if (!evalExpr(a.expr, v)) return false;
                if (v < -32768 || v > 0xFFFF) { if (cx.final) return valueErr("Verschiebung ausserhalb 16 Bit"); v = 0; }
                o.value = uint32_t(v) & 0xFFFF;
            }
            break;
        }
        case Kind::RA16: case Kind::RA8: case Kind::RA7: case Kind::RA12: {
            if (a.t != PArg::Addr || a.shortSeg) { err = "Zieladresse erwartet"; return false; }
            int64_t v;
            if (!evalExpr(a.expr, v)) return false;
            // Länge ist ohne Werte bekannt → Folgeadresse
            Operand tmp[4]; int words = instrWords(in, tmp, cx.seg);
            int64_t next = int64_t(cx.pc) + 2 * words;
            int64_t off = v & 0xFFFF;
            if (cx.seg) {
                int64_t sg = (v >> 24) & 0x7F;
                if (cx.final && (v & ~int64_t(0x7F00FFFF))) return valueErr("Zieladresse ungueltig");
                if (cx.final && sg != 0 && sg != cx.pcSeg) return valueErr("relatives Ziel in anderem Segment");
            } else if (cx.final && (v < 0 || v > 0xFFFF)) {
                return valueErr("Zieladresse ausserhalb 16 Bit");
            }
            int64_t d = off - (next & 0xFFFF);
            if (d > 32767) d -= 65536;
            if (d < -32768) d += 65536;
            o.disp = cx.final ? int32_t(d) : 0;
            break;
        }
        case Kind::IMB: case Kind::IMW: case Kind::IML: case Kind::IM8: case Kind::IM4:
        case Kind::N16: case Kind::LDMN: case Kind::BIT: case Kind::SHL: case Kind::SHR:
        case Kind::LIT: case Kind::RAW: {
            if (a.t != PArg::Imm) { err = "#Wert erwartet"; return false; }
            int64_t v;
            if (s.kind == Kind::LIT) {
                std::string t = trim(a.expr);
                if (t != std::to_string(int(s.lit))) { err = "andere Anzahl"; return false; }
                o.value = s.lit;
                break;
            }
            if (!evalExpr(a.expr, v)) return false;
            bool fin = cx.final;
            auto range = [&](int64_t lo, int64_t hi, const char* what) {
                if (fin && (v < lo || v > hi)) { err = std::string(what) + " ausserhalb " + std::to_string(lo) + ".." + std::to_string(hi); mismatch = false; return false; }
                if (!fin && (v < lo || v > hi)) v = lo < 0 ? 0 : lo;
                return true;
            };
            switch (s.kind) {
            case Kind::IMB: case Kind::IM8: if (!range(-128, 255, "Byte-Direktwert")) return false; o.value = uint32_t(v) & 0xFF; break;
            case Kind::IMW: if (!range(-32768, 65535, "Wort-Direktwert")) return false; o.value = uint32_t(v) & 0xFFFF; break;
            case Kind::IML: if (!range(-2147483648LL, 4294967295LL, "Langwort-Direktwert")) return false; o.value = uint32_t(v); break;
            case Kind::IM4: if (!range(0, 15, "Konstante")) return false; o.value = uint32_t(v); break;
            case Kind::N16: case Kind::LDMN: if (!range(1, 16, "Anzahl")) return false; o.value = uint32_t(v); break;
            case Kind::BIT: if (!range(0, B ? 7 : 15, "Bitnummer")) return false; o.value = uint32_t(v); break;
            case Kind::SHL: case Kind::SHR: {
                int64_t mx = B ? 8 : Lw ? 32 : 16;
                if (!range(s.kind == Kind::SHR ? 1 : 0, mx, "Schiebeweite")) return false;
                o.value = uint32_t(v);
                o.disp = s.kind == Kind::SHR ? -int32_t(v) : int32_t(v);
                break;
            }
            case Kind::RAW: if (!range(0, (int64_t(1) << s.f.width) - 1, "EPU-Feld")) return false; o.value = uint32_t(v); break;
            default: break;
            }
            break;
        }
        case Kind::CC: {
            int c = ccFromName(a.text);
            if (c < 0) { err = "Bedingung erwartet"; return false; }
            o.value = uint32_t(c); break;
        }
        case Kind::CTL: {
            int c = ctlFromName(a.text);
            if (c < 0) { err = "Steuerregister erwartet"; return false; }
            o.value = uint32_t(c); break;
        }
        case Kind::FLAGS:
            if (a.text != "FLAGS") { err = "FLAGS erwartet"; return false; }
            break;
        case Kind::PORT: {
            if (a.t != PArg::Addr || a.shortSeg) { err = "Portadresse erwartet"; return false; }
            int64_t v;
            if (!evalExpr(a.expr, v)) return false;
            if (cx.final && (v < 0 || v > 0xFFFF)) return valueErr("Portadresse ausserhalb 0..%FFFF");
            o.value = uint32_t(v) & 0xFFFF; break;
        }
        case Kind::FL: case Kind::INT: case Kind::None:
            err = "interne Tabellenform"; return false;
        }
    }
    mismatch = false;
    return true;
}

/// Mnemonik (evtl. mit ".L") + Argumenttexte → Zeile und Operanden.
inline bool matchInstr(const std::string& mnemonic, const std::vector<std::string>& argTexts,
                       const BindCtx& cx, const Insn*& chosen, Operand* ops, std::string& err) {
    std::string mn = asmdetail::upper(mnemonic);
    bool alt = false;
    if (mn.size() > 2 && mn.compare(mn.size() - 2, 2, ".L") == 0) { alt = true; mn.resize(mn.size() - 2); }
    auto& idx = mnemonicIndex();
    auto it = idx.find(mn);
    if (it == idx.end()) { err = "unbekannter Befehl " + mn; return false; }
    std::vector<PArg> args;
    for (auto& t : argTexts) {
        PArg a; std::string perr;
        if (!parseArg(t, a, perr)) { err = perr; return false; }
        args.push_back(a);
    }
    std::string firstErr;
    const Table& tab = Table::get();
    for (int i : it->second) {
        const Insn& in = tab.at(i);
        if (in.has(Z8K_ALT) != alt) continue;
        std::string e; bool mismatch = true;
        if (bindRow(in, args, cx, ops, e, mismatch)) { chosen = &in; return true; }
        if (!mismatch) { err = e; return false; }
        if (firstErr.empty()) firstErr = e;
    }
    err = "Operanden passen zu keiner Form von " + mn + (alt ? ".L" : "") +
          (firstErr.empty() ? "" : " (" + firstErr + ")");
    return false;
}

/// Kommentar entfernen (';' bis Zeilenende, '!…!' nach Zilog), Anführungszeichen beachten.
inline std::string stripComment(const std::string& line) {
    std::string out; char q = 0; bool bang = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (bang) { if (c == '!') bang = false; continue; }
        if (q) { out += c; if (c == q) q = 0; continue; }
        if (c == ';') break;
        if (c == '!') { bang = true; continue; }
        if (c == '\'' || c == '"') {
            // 'x' als Zeichen nur, wenn wirklich geschlossen (sonst Kommentar-Apostroph)
            if (line.find(c, i + 1) != std::string::npos) q = c;
        }
        out += c;
    }
    return out;
}

} // namespace asmdetail

/**
 * Einen einzelnen Befehl assemblieren (Tests, Inline-Assembler des Debuggers).
 * `pc`/`pcSeg`: Adresse des Befehls (für relative Ziele und $).
 */
inline bool assembleLine(const std::string& text, bool seg, uint8_t pcSeg, uint16_t pc,
                         std::vector<uint16_t>& words, std::string& err,
                         const asmdetail::SymFn* sym = nullptr, const Insn** chosen = nullptr) {
    using namespace asmdetail;
    std::string s = trim(stripComment(text));
    size_t sp = 0;
    while (sp < s.size() && !std::isspace(static_cast<unsigned char>(s[sp]))) ++sp;
    std::string mn = s.substr(0, sp);
    std::string rest = trim(s.substr(sp));
    std::vector<std::string> args = rest.empty() ? std::vector<std::string>{} : splitArgs(rest);
    BindCtx cx; cx.seg = seg; cx.pcSeg = pcSeg; cx.pc = pc; cx.sym = sym; cx.final = true;
    const Insn* in = nullptr;
    Operand ops[4];
    if (!matchInstr(mn, args, cx, in, ops, err)) return false;
    if (chosen) *chosen = in;
    return encode(*in, ops, in->nops, seg, words, &err);
}

/// Ganzen Quelltext assemblieren.
inline AsmResult assemble(const std::string& source, const AsmOptions& opt = {}) {
    using namespace asmdetail;
    AsmResult res;

    struct Stmt {
        int line = 0;
        std::string src, label, op, rest;
        std::vector<std::string> args;
    };
    std::vector<Stmt> stmts;
    {
        size_t pos = 0; int ln = 0;
        while (pos <= source.size()) {
            size_t e = source.find('\n', pos);
            std::string raw = source.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            ++ln;
            Stmt st; st.line = ln; st.src = raw;
            std::string s = trim(stripComment(raw));
            // Marke:  NAME:  bzw.  NAME EQU … / NAME := …
            size_t i = 0;
            if (!s.empty() && identStart(s[0])) {
                while (i < s.size() && identChar(s[i])) ++i;
                std::string name = s.substr(0, i);
                size_t j = i;
                while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                if (j < s.size() && s[j] == ':' && !(j + 1 < s.size() && s[j + 1] == '=')) {
                    st.label = upper(name); s = trim(s.substr(j + 1));
                } else if (j + 1 < s.size() && s[j] == ':' && s[j + 1] == '=') {
                    st.label = upper(name); st.op = "EQU"; st.rest = trim(s.substr(j + 2)); s.clear();
                } else {
                    std::string after = s.substr(j);
                    std::string w; size_t k = 0;
                    while (k < after.size() && identChar(after[k])) w += after[k++];
                    if (upper(w) == "EQU") { st.label = upper(name); st.op = "EQU"; st.rest = trim(after.substr(k)); s.clear(); }
                }
            }
            if (!s.empty()) {
                size_t k = 0;
                while (k < s.size() && !std::isspace(static_cast<unsigned char>(s[k]))) ++k;
                st.op = upper(s.substr(0, k));
                st.rest = trim(s.substr(k));
            }
            if (!st.rest.empty()) st.args = splitArgs(st.rest);
            stmts.push_back(st);
            if (e == std::string::npos) break;
            pos = e + 1;
        }
    }

    std::map<std::string, int64_t> syms;
    std::map<std::string, int> defLine;
    auto addErr = [&](int line, const std::string& m) { res.errors.push_back({line, m}); };

    for (int pass = 1; pass <= 2; ++pass) {
        bool seg = opt.seg;
        uint8_t curSeg = 0;
        uint32_t off = 0;             // darf zum Prüfen über 0xFFFF laufen
        bool ended = false;
        SymFn symFn = [&](const std::string& n, int64_t& v) {
            auto it = syms.find(n);
            if (it == syms.end()) return false;
            v = it->second; return true;
        };
        auto here = [&]() -> int64_t { return seg ? int64_t(segAddr(curSeg, off)) : int64_t(off); };
        auto evalE = [&](const std::string& e, int line, int64_t& v, bool mustDefine) -> bool {
            bool undef = false; std::string err;
            Expr x(e, symFn, here());
            if (!x.eval(v, undef, err)) { if (pass == 2) addErr(line, err); v = 0; return false; }
            if (undef) {
                if (pass == 2 || mustDefine) addErr(line, "unbekanntes Symbol in '" + e + "'");
                v = 0; return false;
            }
            return true;
        };
        auto emit = [&](ListLine& ll, uint8_t b) {
            if (off > 0xFFFF) { return; }
            if (pass == 2) {
                uint32_t lin = (uint32_t(seg ? curSeg : 0) << 16) | off;
                if (res.image.count(lin)) addErr(ll.line, "Adresse doppelt belegt");
                res.image[lin] = b;
                ll.bytes.push_back(b);
            }
            ++off;
        };

        for (auto& st : stmts) {
            ListLine ll; ll.line = st.line; ll.src = st.src; ll.segMode = seg;
            if (ended) { if (pass == 2) res.listing.push_back(ll); continue; }
            bool isEqu = st.op == "EQU";
            if (!st.label.empty() && !isEqu) {
                if (pass == 1) {
                    if (syms.count(st.label)) addErr(st.line, "Marke doppelt: " + st.label);
                    else { syms[st.label] = here(); defLine[st.label] = st.line;
                           res.labels[st.label] = {seg, (uint32_t(seg ? curSeg : 0) << 16) | uint16_t(off)}; }
                } else if (syms[st.label] != here() && defLine[st.label] == st.line) {
                    addErr(st.line, "Marke " + st.label + " hat sich zwischen den Durchlaeufen verschoben");
                }
            }
            ll.hasAddr = true; ll.seg = curSeg; ll.off = uint16_t(off);
            const std::string& op = st.op;
            if (op.empty()) { ll.hasAddr = !st.label.empty(); if (pass == 2) res.listing.push_back(ll); continue; }

            if (isEqu) {
                ll.hasAddr = false;
                if (st.label.empty()) { if (pass == 2) addErr(st.line, "EQU ohne Namen"); }
                else {
                    int64_t v;
                    bool ok = evalE(st.rest, st.line, v, false);
                    if (pass == 1) {
                        if (syms.count(st.label) && defLine[st.label] != st.line) addErr(st.line, "Symbol doppelt: " + st.label);
                        if (ok) { syms[st.label] = v; defLine[st.label] = st.line; }
                    } else {
                        if (ok) syms[st.label] = v;
                    }
                }
                if (pass == 2) res.listing.push_back(ll);
                continue;
            }
            if (op == "ORG") {
                int64_t v;
                if (evalE(st.rest, st.line, v, true)) {
                    if (seg) {
                        if (v < 0 || (v & ~int64_t(0x7F00FFFF))) { if (pass == 2) addErr(st.line, "ORG: keine segmentierte Adresse"); }
                        else { curSeg = uint8_t((v >> 24) & 0x7F); off = uint32_t(v & 0xFFFF); }
                    } else {
                        if (v < 0 || v > 0xFFFF) { if (pass == 2) addErr(st.line, "ORG ausserhalb 0..%FFFF"); }
                        else off = uint32_t(v);
                    }
                }
                ll.seg = curSeg; ll.off = uint16_t(off);
                if (pass == 2) res.listing.push_back(ll);
                continue;
            }
            if (op == "SEG" || op == "SEGMENTED" || op == "NONSEG" || op == "NONSEGMENTED") {
                seg = (op == "SEG" || op == "SEGMENTED");
                ll.hasAddr = false;
                if (pass == 2) res.listing.push_back(ll);
                continue;
            }
            if (op == "END") { ended = true; ll.hasAddr = false; if (pass == 2) res.listing.push_back(ll); continue; }
            if (op == "EVEN" || op == "ALIGN") {
                int64_t a = 2;
                if (op == "ALIGN" && !evalE(st.rest, st.line, a, true)) a = 2;
                if (a < 1) a = 1;
                while (off % uint32_t(a)) emit(ll, 0);
                if (pass == 2) res.listing.push_back(ll);
                continue;
            }
            if (op == "DS" || op == "BLKB") {
                int64_t n;
                if (evalE(st.rest, st.line, n, true)) {
                    if (n < 0 || n > 0x10000) { if (pass == 2) addErr(st.line, "DS: Anzahl ungueltig"); }
                    else for (int64_t k = 0; k < n; ++k) emit(ll, 0);
                }
                if (pass == 2) { ll.bytes.clear(); res.listing.push_back(ll); }
                continue;
            }
            if (op == "DB" || op == "BVAL" || op == "DW" || op == "WVAL" || op == "DL" || op == "LVAL") {
                int size = (op == "DB" || op == "BVAL") ? 1 : (op == "DW" || op == "WVAL") ? 2 : 4;
                if (size > 1 && (off & 1) && pass == 2) addErr(st.line, op + " auf ungerader Adresse");
                for (auto& a : st.args) {
                    std::string t = trim(a);
                    if (size == 1 && t.size() >= 2 && (t[0] == '\'' || t[0] == '"') && t.back() == t[0] && t.size() != 3) {
                        for (size_t k = 1; k + 1 < t.size(); ++k) emit(ll, uint8_t(t[k]));
                        continue;
                    }
                    int64_t v = 0;
                    evalE(t, st.line, v, false);
                    if (pass == 2) {
                        bool okRange = size == 1 ? (v >= -128 && v <= 255)
                                     : size == 2 ? ((v >= -32768 && v <= 0xFFFF) || (v > 0 && (v & ~int64_t(0x7F00FFFF)) == 0))
                                     : (v >= -2147483648LL && v <= 4294967295LL);
                        if (!okRange) addErr(st.line, op + ": Wert passt nicht");
                    }
                    uint32_t u = uint32_t(v);
                    for (int k = size - 1; k >= 0; --k) emit(ll, uint8_t(u >> (8 * k)));
                }
                if (pass == 2) res.listing.push_back(ll);
                continue;
            }
            // ── Befehl
            if ((off & 1) && pass == 2) addErr(st.line, "Befehl auf ungerader Adresse");
            BindCtx cx; cx.seg = seg; cx.pcSeg = curSeg; cx.pc = uint16_t(off); cx.sym = &symFn;
            cx.final = (pass == 2); cx.laxPtr = opt.laxPointers;
            const Insn* in = nullptr;
            Operand ops[4];
            std::string err;
            if (!matchInstr(op, st.args, cx, in, ops, err)) {
                if (pass == 2) {
                    addErr(st.line, err);
                    res.listing.push_back(ll);
                    // Länge wie in Pass 1 weiterzählen, sonst verschieben sich die Marken
                    BindCtx c1 = cx; c1.final = false;
                    std::string e1;
                    if (matchInstr(op, st.args, c1, in, ops, e1)) off += uint32_t(2 * instrWords(*in, ops, seg));
                }
                continue;
            }
            int nw = instrWords(*in, ops, seg);
            if (pass == 1) { off += uint32_t(2 * nw); continue; }
            std::vector<uint16_t> words;
            if (!encode(*in, ops, in->nops, seg, words, &err)) {
                addErr(st.line, err);
                off += uint32_t(2 * nw);
                res.listing.push_back(ll);
                continue;
            }
            for (uint16_t w : words) { emit(ll, uint8_t(w >> 8)); emit(ll, uint8_t(w)); }
            if (off > 0x10000) addErr(st.line, "Offset laeuft ueber %FFFF");
            res.listing.push_back(ll);
        }
        if (pass == 1 && !res.errors.empty()) {
            // Pass-1-Fehler (doppelte Marken, ORG) melden; Pass 2 läuft trotzdem,
            // damit alle übrigen Fehler auch erscheinen.
        }
    }
    std::stable_sort(res.errors.begin(), res.errors.end(),
                     [](const AsmMessage& a, const AsmMessage& b) { return a.line < b.line; });
    res.symbols = syms;
    return res;
}

/**
 * @brief Symboldatei für k1520dbg (`sym`/`-s`): eine Marke je Zeile, `<<SEG>>%OFFS NAME`.
 *
 * Nur Marken (EQU-Konstanten nicht — sie sind keine Adressen und würden sonst jede
 * gleichlautende Zahl im Disassembler beschriften).  Nichtsegmentiert assemblierte
 * Marken haben kein Segment; sie bekommen @p nonsegSeg (dort, wohin das Programm
 * geladen wird).  Sortiert nach Adresse.
 */
inline std::string formatSymbols(const AsmResult& r, uint8_t nonsegSeg = 0,
                                 const std::string& source = "") {
    std::vector<std::pair<uint32_t, std::string>> v;
    for (auto& kv : r.labels) {
        uint32_t k = kv.second.second;
        if (!kv.second.first) k = (uint32_t(nonsegSeg & 0x7F) << 16) | (k & 0xFFFF);
        v.push_back({k, kv.first});
    }
    std::sort(v.begin(), v.end());
    std::string out = "# z8kasm-Symbole (U8001) fuer k1520dbg: <<SEG>>%OFFS NAME";
    if (!source.empty()) out += " — " + source;
    out += "\n";
    char buf[32];
    for (auto& e : v) {
        std::snprintf(buf, sizeof buf, "<<%u>>%%%04X ", unsigned(e.first >> 16), unsigned(e.first & 0xFFFF));
        out += buf + e.second + "\n";
    }
    return out;
}

/// Listing als Text: Adresse, Bytes (höchstens 8 je Zeile, Rest in Folgezeilen), Quelle.
inline std::string formatListing(const AsmResult& r) {
    std::string out;
    char buf[64];
    for (auto& l : r.listing) {
        std::string addr;
        if (l.hasAddr) {
            if (l.segMode) std::snprintf(buf, sizeof buf, "<<%u>>%04X ", unsigned(l.seg), unsigned(l.off));
            else           std::snprintf(buf, sizeof buf, "%04X ", unsigned(l.off));
            addr = buf;
        }
        size_t i = 0;
        do {
            std::string hex;
            for (size_t k = 0; k < 8 && i < l.bytes.size(); ++k, ++i) {
                std::snprintf(buf, sizeof buf, "%02X", l.bytes[i]);
                hex += buf;
                if (k & 1) hex += ' ';
            }
            std::snprintf(buf, sizeof buf, "%5d  ", l.line);
            std::string col = addr;
            col.resize(12, ' ');
            hex.resize(21, ' ');
            out += std::string(buf) + col + hex + (i <= 8 ? l.src : "") + "\n";
            addr.clear();
        } while (i < l.bytes.size());
    }
    return out;
}

} // namespace z8k
