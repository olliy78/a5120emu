/**
 * @file dbg_u8000.h
 * @brief Maschinenfreie Bausteine des U8001-Kontexts in k1520dbg (S5, Plan 17 §3).
 *
 * - Adressen der 16-Bit-Seite: `<<seg>>off` (Zilog-Schreibweise des Disassemblers),
 *   `em:ZELLE` (roh ins DRAM, an der Segmentweiche vorbei) oder eine schlichte Zahl
 *   (Segment = Vorgabe, meist das PC-Segment).  Zahlen: `%1234`/`0x1234`/`1234h` hex,
 *   sonst dezimal — wie im übrigen Debugger.
 * - FCW als Text.
 * - Aufrufstapel des U8001 aus der Befehlsfolge (CALL/CALR und SP-Anstieg), das
 *   Gegenstück zu tools/callstack_tracker.h für den Z80.
 * - (S5b) Register-Sicht für Ausdrücke (`b … if`, `lp`, `disp`), Adressbereiche für
 *   Watchpoints, Symbole mit Segment (Symboldatei aus `z8kasm --sym`) und der
 *   Vergleich zweier EM-Zustände für `snap diff`.
 *
 * Ohne Emulator unit-getestet (tests/debugtools/test_dbg_u8000.cpp).
 *
 * @license MIT
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dbg16 {

/// Eine Adresse der 16-Bit-Seite.
struct Addr16 {
    enum Kind : uint8_t { Seg, Raw } kind = Seg;
    uint8_t  seg = 0;      ///< Kind::Seg: Segmentnummer (0..127)
    uint16_t off = 0;      ///< Kind::Seg: Offset
    uint32_t cell = 0;     ///< Kind::Raw: DRAM-Zelle
    bool     explicitSeg = false;   ///< `<<n>>` stand ausdrücklich da
    uint32_t key() const { return (uint32_t(seg) << 16) | off; }
};

/// Zahl im Debuggerstil: %hex, 0xhex, …h hex, sonst dezimal.  false bei Unsinn.
inline bool parseNumber(const std::string& t, long& v) {
    if (t.empty()) return false;
    char* e = nullptr;
    if (t[0] == '%') { v = std::strtol(t.c_str() + 1, &e, 16); return e && *e == 0 && t.size() > 1; }
    if (t.size() > 1 && (t.back() == 'h' || t.back() == 'H')) {
        std::string b = t.substr(0, t.size() - 1);
        v = std::strtol(b.c_str(), &e, 16);
        return e && *e == 0;
    }
    v = std::strtol(t.c_str(), &e, 0);
    return e && *e == 0;
}

/**
 * @brief `<<seg>>off` | `em:ZELLE` | Zahl | Symbol (über @p sym) → Addr16.
 * @param defSeg Segment für eine Zahl ohne `<<…>>` (PC-Segment).
 */
inline bool parseAddr(const std::string& tok, uint8_t defSeg, Addr16& a,
                      const std::function<bool(const std::string&, long&)>& sym = nullptr) {
    a = Addr16{};
    if (tok.size() > 3 && (tok.compare(0, 3, "em:") == 0 || tok.compare(0, 3, "EM:") == 0)) {
        long v;
        // Rohzellen sind immer hex (so schreibt sie auch `dev em`).
        std::string s = tok.substr(3);
        if (!s.empty() && s[0] == '%') s = s.substr(1);
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
        char* e = nullptr;
        v = std::strtol(s.c_str(), &e, 16);
        if (s.empty() || !e || *e) return false;
        a.kind = Addr16::Raw;
        a.cell = uint32_t(v);
        return true;
    }
    std::string rest = tok;
    a.seg = defSeg;
    if (tok.size() > 4 && tok.compare(0, 2, "<<") == 0) {
        size_t e = tok.find(">>");
        if (e == std::string::npos) return false;
        long s;
        std::string st = tok.substr(2, e - 2);
        // Segment: dezimal wie beim Disassembler; %/0x/h als hex erlaubt.
        if (!parseNumber(st, s) || s < 0 || s > 127) return false;
        a.seg = uint8_t(s);
        a.explicitSeg = true;
        rest = tok.substr(e + 2);
    }
    long v;
    if (parseNumber(rest, v)) { a.off = uint16_t(v); return true; }
    if (sym) {                              // Symbol (NAME[+OFF] löst der Rufer auf)
        long sv;
        if (sym(rest, sv)) {
            if (!a.explicitSeg && sv > 0xFFFF) a.seg = uint8_t((sv >> 16) & 0x7F);
            a.off = uint16_t(sv);
            return true;
        }
    }
    return false;
}

/// `<<3>>%1234` (segmentiert) bzw. `%1234`.
inline std::string addrText(uint8_t seg, uint16_t off, bool segmented) {
    char b[24];
    if (segmented) std::snprintf(b, sizeof b, "<<%u>>%%%04X", unsigned(seg), off);
    else           std::snprintf(b, sizeof b, "%%%04X", off);
    return b;
}

/// FCW als Text: "SEG S --- VIE NVIE  C Z - V - -".
inline std::string fcwText(uint16_t fcw) {
    std::string s;
    s += (fcw & 0x8000) ? "SEG " : "NONSEG ";
    s += (fcw & 0x4000) ? "SYS " : "NORM ";
    if (fcw & 0x2000) s += "EPA ";
    s += (fcw & 0x1000) ? "VIE " : "vi- ";
    s += (fcw & 0x0800) ? "NVIE " : "nvi- ";
    const char* n = "CZSVDH";
    const uint16_t bit[6] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04};
    s += "[";
    for (int i = 0; i < 6; ++i) s += (fcw & bit[i]) ? n[i] : '-';
    s += "]";
    return s;
}

/// Ein Rahmen des U8001-Aufrufstapels.
struct Frame16 {
    uint32_t site;      ///< seg<<16|off des CALL
    uint32_t target;    ///< Ziel
    uint32_t ret;       ///< Rückkehradresse
    uint16_t sp;        ///< SP-Offset NACH dem Kellern der Rückkehradresse
};

/**
 * @brief Aufrufstapel aus der Befehlsfolge.
 *
 * Vor jedem Befehl: @ref onInstruction mit dem jetzigen SP-Offset.  Ein CALL/CALR
 * wird vorgemerkt und beim nächsten Befehl als Rahmen übernommen (SP ist dann um
 * 2/4 gefallen); jeder Rahmen, über dessen SP der Stapel wieder gestiegen ist,
 * ist zurückgekehrt (RET, IRET, Stapelabbau von Hand).
 */
class CallStack16 {
public:
    void onInstruction(uint32_t pcKey, uint16_t sp) {
        while (!frames_.empty() && sp > frames_.back().sp) frames_.pop_back();
        if (pending_) {
            pending_ = false;
            if (sp < pendSp_) {                       // CALL hat wirklich gekellert
                frames_.push_back(Frame16{pendSite_, pcKey, pendRet_, sp});
                if (frames_.size() > 256) frames_.erase(frames_.begin());
            }
        }
    }
    /// Der gerade anstehende Befehl ist ein Aufruf (vor seiner Ausführung melden).
    void noteCall(uint32_t site, uint32_t ret, uint16_t sp) {
        pending_ = true; pendSite_ = site; pendRet_ = ret; pendSp_ = sp;
    }
    const std::vector<Frame16>& frames() const { return frames_; }
    void clear() { frames_.clear(); pending_ = false; }

private:
    std::vector<Frame16> frames_;
    bool     pending_ = false;
    uint32_t pendSite_ = 0, pendRet_ = 0;
    uint16_t pendSp_ = 0;
};

// ─── S5b: Ausdrücke ──────────────────────────────────────────────────────────

/// Was ein Ausdruck in der U8001-Sicht lesen kann (Register so, wie der laufende
/// Modus sie sieht, dazu optional der Zustand der EM-Steuerkarte).
struct RegView16 {
    uint16_t r[16] = {};
    uint16_t fcw = 0, pc = 0;
    uint8_t  pcSeg = 0;
    uint16_t psapSeg = 0, psapOff = 0, refresh = 0;
    bool     haveEm = false;
    uint8_t  a33 = 0, a34 = 0, a35 = 0, a36 = 0, a53 = 0;
    bool     pe = false, a54 = false, mode8 = true;
};

/**
 * @brief Register-/Flag-/Kartenname (GROSS) → Wert.
 *
 * R0..R15, RH0..RH7, RL0..RL7, RR0..RR14 (Rn = oberes Wort), RQ0..RQ12, PC (Offset),
 * PCSEG, SP (= R15, der Offset), SPSEG (segmentiert: Segment aus R14), FCW, Flags
 * C Z S V P PV D H (0/1), SEG SYS VIE NVIE EPA (FCW-Bits), PSAP PSAPSEG REFRESH;
 * mit Karte A33 A34 A35 A36 A53 A54 PE MODE (8 oder 16).
 */
inline bool reg16(const RegView16& v, const std::string& U, long long& out) {
    auto num = [&](const char* pfx, unsigned& n) -> bool {
        const size_t l = std::strlen(pfx);
        if (U.size() <= l || U.compare(0, l, pfx) != 0) return false;
        for (size_t k = l; k < U.size(); ++k) if (!std::isdigit((unsigned char)U[k])) return false;
        n = unsigned(std::atoi(U.c_str() + l));
        return true;
    };
    unsigned n = 0;
    if (num("RQ", n)) { if (n > 12 || (n & 3)) return false;
        out = (long long)(((uint64_t)v.r[n] << 48) | ((uint64_t)v.r[n+1] << 32) |
                          ((uint64_t)v.r[n+2] << 16) | v.r[n+3]); return true; }
    if (num("RR", n)) { if (n > 14 || (n & 1)) return false;
        out = (long long)(((uint32_t)v.r[n] << 16) | v.r[n+1]); return true; }
    if (num("RH", n)) { if (n > 7) return false; out = v.r[n] >> 8; return true; }
    if (num("RL", n)) { if (n > 7) return false; out = v.r[n] & 0xFF; return true; }
    if (num("R", n))  { if (n > 15) return false; out = v.r[n]; return true; }
    struct F { const char* n; uint16_t bit; };
    static const F flags[] = {{"C",0x80},{"Z",0x40},{"S",0x20},{"V",0x10},{"P",0x10},{"PV",0x10},
                              {"D",0x08},{"H",0x04},{"SEG",0x8000},{"SYS",0x4000},{"EPA",0x2000},
                              {"VIE",0x1000},{"NVIE",0x0800}};
    for (const F& f : flags) if (U == f.n) { out = (v.fcw & f.bit) ? 1 : 0; return true; }
    if (U == "PC")      { out = v.pc; return true; }
    if (U == "PCSEG")   { out = v.pcSeg; return true; }
    if (U == "SP")      { out = v.r[15]; return true; }
    if (U == "SPSEG")   { out = (v.r[14] >> 8) & 0x7F; return true; }
    if (U == "FCW")     { out = v.fcw; return true; }
    if (U == "PSAP")    { out = v.psapOff; return true; }
    if (U == "PSAPSEG") { out = (v.psapSeg >> 8) & 0x7F; return true; }
    if (U == "REFRESH") { out = v.refresh; return true; }
    if (!v.haveEm) return false;
    if (U == "A33") { out = v.a33; return true; }
    if (U == "A34") { out = v.a34; return true; }
    if (U == "A35") { out = v.a35; return true; }
    if (U == "A36") { out = v.a36; return true; }
    if (U == "A53") { out = v.a53; return true; }
    if (U == "A54") { out = v.a54 ? 1 : 0; return true; }
    if (U == "PE")  { out = v.pe ? 1 : 0; return true; }
    if (U == "MODE"){ out = v.mode8 ? 8 : 16; return true; }
    return false;
}

/**
 * @brief Ausdruckswert → Segment + Offset.
 *
 * Drei Formen: Registerpaar-Form (Segment in Bit 24..30, Bit 16..23 = 0 — so steht
 * eine Adresse in RRn), Schlüssel-/Literalform `<<s>>off` (Segment in Bit 16..22)
 * und eine 16-Bit-Zahl (Segment = @p defSeg).
 */
inline void decodeAddr(long long v, uint8_t defSeg, uint8_t& seg, uint16_t& off) {
    const uint32_t u = uint32_t(v);
    off = uint16_t(u);
    if ((u & 0x7F000000u) && !(u & 0x00FF0000u)) seg = uint8_t((u >> 24) & 0x7F);
    else if (u > 0xFFFFu)                          seg = uint8_t((u >> 16) & 0x7F);
    else                                           seg = defSeg;
}

// ─── S5b: Symbole mit Segment ────────────────────────────────────────────────

/**
 * @brief Symboltabelle der 16-Bit-Seite (Schlüssel seg·2¹⁶+off).
 *
 * Zeilenformat (wie `z8kasm --sym` schreibt, `#` = Kommentar):
 *   `<<SEG>>%OFFS NAME`   — auch `NAME <<SEG>>%OFFS` und `NAME = <<SEG>>%OFFS`.
 * Eine Zeile ohne `<<…>>` ist KEIN U8001-Symbol (sie gehört der Z80-Tabelle).
 */
class SymTab16 {
public:
    void add(const std::string& name, uint32_t key) {
        auto it = byName_.find(name);
        if (it != byName_.end()) byKey_.erase(it->second);
        byName_[name] = key;
        byKey_[key] = name;
    }
    bool empty() const { return byName_.empty(); }
    size_t size() const { return byName_.size(); }
    const std::map<uint32_t, std::string>& byKey() const { return byKey_; }
    const std::map<std::string, uint32_t>& byName() const { return byName_; }

    /// Name oder NAME+OFF / NAME-OFF → Schlüssel.
    bool find(const std::string& tok, uint32_t& key) const {
        size_t p = tok.find_first_of("+-", 1);
        std::string base = p == std::string::npos ? tok : tok.substr(0, p);
        auto it = byName_.find(base);
        if (it == byName_.end()) return false;
        long d = 0;
        if (p != std::string::npos) {
            long v; if (!parseNumber(tok.substr(p + 1), v)) return false;
            d = tok[p] == '-' ? -v : v;
        }
        key = (it->second & 0x7F0000u) | uint16_t(uint16_t(it->second) + d);
        return true;
    }
    /// Genau dieser Schlüssel → Name (sonst leer).
    std::string at(uint32_t key) const {
        auto it = byKey_.find(key);
        return it == byKey_.end() ? std::string() : it->second;
    }
    /// Nächstes Symbol darunter im selben Segment, höchstens @p maxDist entfernt:
    /// "NAME" bzw. "NAME+%1A" (für bt/where/hist); sonst leer.
    std::string near(uint32_t key, uint32_t maxDist = 0x400) const {
        auto it = byKey_.upper_bound(key);
        if (it == byKey_.begin()) return {};
        --it;
        if ((it->first >> 16) != (key >> 16) || key - it->first > maxDist) return {};
        if (it->first == key) return it->second;
        char b[16]; std::snprintf(b, sizeof b, "+%%%X", unsigned(key - it->first));
        return it->second + b;
    }
    /**
     * @brief Eine Zeile lesen.
     * @return 1 = U8001-Symbol übernommen, 0 = keine U8001-Zeile (leer, Kommentar,
     *         Z80-Form), -1 = U8001-Form, aber unlesbar.
     */
    int parseLine(const std::string& line) {
        std::vector<std::string> w;
        std::string cur;
        for (char c : line) {
            if (c == '#' && cur.empty() && w.empty()) return 0;
            if (std::isspace((unsigned char)c)) { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) w.push_back(cur);
        if (w.size() == 3 && w[1] == "=") w.erase(w.begin() + 1);
        if (w.size() < 2) return 0;
        int ai = w[0].compare(0, 2, "<<") == 0 ? 0 : w[1].compare(0, 2, "<<") == 0 ? 1 : -1;
        if (ai < 0) return 0;
        Addr16 a;
        if (!parseAddr(w[size_t(ai)], 0, a) || a.kind != Addr16::Seg) return -1;
        add(w[size_t(1 - ai)], a.key());
        return 1;
    }

private:
    std::map<std::string, uint32_t> byName_;
    std::map<uint32_t, std::string> byKey_;
};

// ─── S5b: Adressbereiche für Watchpoints ─────────────────────────────────────

/**
 * @brief `A` oder `A..B` der 16-Bit-Seite.  Logisch: beide Enden im selben Segment
 *        (B ohne `<<…>>` erbt das Segment von A); roh: `em:X..Y` bzw. `em:X..em:Y`.
 */
inline bool parseRange16(const std::string& tok, uint8_t defSeg, Addr16& lo, Addr16& hi,
                         const std::function<bool(const std::string&, long&)>& sym = nullptr) {
    size_t dd = tok.find("..");
    if (dd == std::string::npos) {
        if (!parseAddr(tok, defSeg, lo, sym)) return false;
        hi = lo;
        return true;
    }
    if (!parseAddr(tok.substr(0, dd), defSeg, lo, sym)) return false;
    std::string b = tok.substr(dd + 2);
    if (lo.kind == Addr16::Raw && b.compare(0, 3, "em:") != 0 && b.compare(0, 3, "EM:") != 0)
        b = "em:" + b;
    if (!parseAddr(b, lo.seg, hi, sym)) return false;
    if (hi.kind != lo.kind) return false;
    if (lo.kind == Addr16::Seg) {
        if (hi.explicitSeg && hi.seg != lo.seg) return false;   // ein Bereich = ein Segment
        hi.seg = lo.seg;
        if (hi.off < lo.off) std::swap(hi.off, lo.off);
    } else if (hi.cell < lo.cell) std::swap(hi.cell, lo.cell);
    return true;
}

// ─── S5b: EM-Zustand für `snap diff` ─────────────────────────────────────────

/// Was `snap` vom Erweiterungsmodul festhält (Register, Karte, DRAM).
struct EmSnap {
    bool     valid = false;
    uint16_t r[16] = {};            ///< R0..R15 wie der Modus sie sieht
    uint16_t r14o = 0, r15o = 0;    ///< die andere Bank
    uint16_t fcw = 0, pc = 0;
    uint8_t  pcSeg = 0;
    uint16_t psapSeg = 0, psapOff = 0, refresh = 0;
    uint64_t cyc = 0;
    std::string state;              ///< run/RESET16/HALT/…
    uint8_t  attr[16] = {};         ///< A22
    uint8_t  a33 = 0, a34 = 0, a35 = 0, a36 = 0, a53 = 0, seg = 0;
    bool     vi = false, mode8 = true, reset16 = true, ramen = false, stop = false, trq8 = false;
    bool     tren = false, busrq = false, busak = false, pr = true, pe = false, a54 = false, nvi = false;
    uint8_t  pio[2][6] = {};        ///< je Tor: mode out in dir vec (ie|pend<<1|ius<<2)
    std::vector<uint8_t> dram;
};

/// Unterschiede zweier EM-Zustände als Textzeilen (leer = gleich).  DRAM-Bereiche
/// zusammengefasst, höchstens @p maxRanges gelistet.
inline std::vector<std::string> diffEm(const EmSnap& a, const EmSnap& b, int maxRanges = 40) {
    std::vector<std::string> out;
    if (!a.valid || !b.valid) return out;
    char buf[160];
    std::string regs;
    auto reg = [&](const char* n, unsigned va, unsigned vb, int w) {
        if (va == vb) return;
        std::snprintf(buf, sizeof buf, " %s %0*X→%0*X", n, w, va, w, vb);
        regs += buf;
    };
    for (int i = 0; i < 16; ++i) { char n[8]; std::snprintf(n, sizeof n, "R%d", i); reg(n, a.r[i], b.r[i], 4); }
    reg("R14'", a.r14o, b.r14o, 4); reg("R15'", a.r15o, b.r15o, 4);
    reg("FCW", a.fcw, b.fcw, 4); reg("PCSEG", a.pcSeg, b.pcSeg, 2); reg("PC", a.pc, b.pc, 4);
    reg("PSAPSEG", a.psapSeg, b.psapSeg, 4); reg("PSAP", a.psapOff, b.psapOff, 4);
    if (!regs.empty()) out.push_back("U8001:" + regs);
    if (a.state != b.state) out.push_back("U8001: Zustand " + a.state + "→" + b.state);
    std::string k;
    auto kv = [&](const char* n, unsigned va, unsigned vb) {
        if (va == vb) return;
        std::snprintf(buf, sizeof buf, " %s %X→%X", n, va, vb);
        k += buf;
    };
    kv("A33", a.a33, b.a33); kv("A34", a.a34, b.a34); kv("A35", a.a35, b.a35); kv("A36", a.a36, b.a36);
    kv("VI", a.vi, b.vi); kv("8/16(A29)", a.mode8 ? 8 : 16, b.mode8 ? 8 : 16);
    kv("RESET16", a.reset16, b.reset16); kv("RAMEN", a.ramen, b.ramen); kv("STOP", a.stop, b.stop);
    kv("TRQ8", a.trq8, b.trq8); kv("SG", a.seg, b.seg); kv("TREN", a.tren, b.tren);
    kv("BUSRQ16", a.busrq, b.busrq); kv("BUSAK16", a.busak, b.busak);
    kv("A53", a.a53, b.a53); kv("A54", a.a54, b.a54); kv("NVI", a.nvi, b.nvi);
    kv("PR", a.pr, b.pr); kv("A46/PE", a.pe, b.pe);
    if (!k.empty()) out.push_back("EM:" + k);
    std::string at;
    for (int p = 0; p < 16; ++p)
        if (a.attr[p] != b.attr[p]) { std::snprintf(buf, sizeof buf, " %X:%X→%X", p, a.attr[p], b.attr[p]); at += buf; }
    if (!at.empty()) out.push_back("A22:" + at);
    for (int t = 0; t < 2; ++t) {
        static const char* const nm[6] = {"mode", "out", "in", "dir", "vec", "int"};
        std::string ps;
        for (int f = 0; f < 6; ++f)
            if (a.pio[t][f] != b.pio[t][f]) { std::snprintf(buf, sizeof buf, " %s %02X→%02X", nm[f], a.pio[t][f], b.pio[t][f]); ps += buf; }
        if (!ps.empty()) out.push_back(std::string("PIO-A32 ") + (t ? "B:" : "A:") + ps);
    }
    const size_t n = std::min(a.dram.size(), b.dram.size());
    long changed = 0; int runs = 0; long lo = -1;
    for (size_t i = 0; i <= n; ++i) {
        const bool d = i < n && a.dram[i] != b.dram[i];
        if (d) { ++changed; if (lo < 0) lo = long(i); }
        else if (lo >= 0) {
            if (runs < maxRanges) {
                std::snprintf(buf, sizeof buf, "EM-DRAM em:%05lX..em:%05lX (%ld B)", lo, long(i) - 1, long(i) - lo);
                out.push_back(buf);
            }
            ++runs; lo = -1;
        }
    }
    if (changed) {
        std::snprintf(buf, sizeof buf, "→ %ld EM-DRAM-Byte(s) in %d Bereich(en)%s", changed, runs,
                      runs > maxRanges ? "  (nur die ersten gelistet)" : "");
        out.push_back(buf);
    }
    return out;
}

}  // namespace dbg16
