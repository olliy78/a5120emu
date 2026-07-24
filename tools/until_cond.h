/**
 * @file until_cond.h
 * @brief Bedingung für `boot_trace --until <cond>` (Parsen + Auswerten).
 *
 * Hält den Trace-Lauf an, sobald die Bedingung gilt. Unterstützte Formen:
 *   PC<op>ADDR, [ADDR]<op>VAL, [ADDR]w<op>VAL   mit op ∈ == != < > <= >=
 *   screen ~ "text"  /  screen ~ /regex/        Bildschirm-Text (80×24 VRAM)
 * ADDR/VAL base-0 (0x.. hex oder dezimal). Pro ZVE1-Instruktion geprüft.
 *
 * Header-only → Parser + Operator-Auswertung ohne Maschine unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>

namespace untilcond {

struct UntilCond {
    enum Kind { NONE, MEM, PC, SCREEN } kind = NONE;
    enum Op   { EQ, NE, LT, GT, LE, GE } op = EQ;
    bool        word = false;   ///< MEM: 16-bit little-endian
    uint16_t    addr = 0;       ///< MEM-Adresse oder PC-Ziel
    long        val  = 0;       ///< Vergleichswert (bei PC: die Zieladresse)
    std::string text;           ///< Original-Spezifikation (für Report)
    std::string pattern;        ///< SCREEN: Substring oder /regex/

    /// SCREEN: den (bereits gerenderten) 80×24-Bildschirmtext gegen das Muster prüfen.
    bool screenMatch(const std::string& scr) const {
        if (pattern.size() >= 2 && pattern.front()=='/' && pattern.back()=='/') {
            try { std::regex re(pattern.substr(1, pattern.size()-2));
                  return std::regex_search(scr, re); }
            catch (...) { return scr.find(pattern) != std::string::npos; }
        }
        return scr.find(pattern) != std::string::npos;
    }

    /// Den aktuellen Wert (Speicher bzw. PC) gegen die Bedingung prüfen.
    bool compare(long cur) const {
        switch (op) {
            case EQ: return cur == val;
            case NE: return cur != val;
            case LT: return cur <  val;
            case GT: return cur >  val;
            case LE: return cur <= val;
            case GE: return cur >= val;
        }
        return false;
    }
};

/**
 * Eine `--until`-Spezifikation parsen.
 * @return false bei unbrauchbarer Eingabe (dann bleibt `u.kind == NONE`).
 */
inline bool parse(const std::string& spec, UntilCond& u) {
    // screen ~ "text" / screen ~ /regex/  — vor der Op-Zerlegung behandeln (kein
    // klassischer Vergleichsoperator; Auswertung braucht den gerenderten Bildschirm).
    {
        std::string s = spec;
        while (!s.empty() && s.front()==' ') s.erase(s.begin());
        if (s.rfind("screen",0)==0 && (s.size()==6 || s[6]==' ' || s[6]=='~')) {
            size_t i = 6;
            while (i<s.size() && s[i]==' ') ++i;
            if (i<s.size() && s[i]=='~') ++i;
            while (i<s.size() && s[i]==' ') ++i;
            std::string pat = s.substr(i);
            if (pat.size()>=2 && (pat.front()=='"'||pat.front()=='\'') && pat.back()==pat.front())
                pat = pat.substr(1, pat.size()-2);
            if (pat.empty()) return false;
            u.kind = UntilCond::SCREEN; u.text = spec; u.pattern = pat; return true;
        }
    }
    // Auf dem Vergleichsoperator splitten (2-Zeichen-Ops zuerst, damit "<" ≠ "<=").
    static const struct { const char* s; UntilCond::Op op; } ops[] = {
        {"==",UntilCond::EQ},{"!=",UntilCond::NE},{"<=",UntilCond::LE},
        {">=",UntilCond::GE},{"<",UntilCond::LT},{">",UntilCond::GT} };
    size_t pos = std::string::npos, oplen = 0; UntilCond::Op op = UntilCond::EQ;
    for (auto& o : ops){ size_t p = spec.find(o.s); if (p != std::string::npos){ pos=p; oplen=strlen(o.s); op=o.op; break; } }
    if (pos == std::string::npos) return false;
    std::string lhs = spec.substr(0, pos), rhs = spec.substr(pos + oplen);
    auto trim = [](std::string& x){ while(!x.empty()&&x.front()==' ')x.erase(x.begin());
                                    while(!x.empty()&&x.back()==' ')x.pop_back(); };
    trim(lhs); trim(rhs);
    u.op = op; u.text = spec; u.val = strtol(rhs.c_str(), nullptr, 0);
    if (!lhs.empty() && lhs[0]=='[') {                       // [ADDR] / [ADDR]w
        size_t r = lhs.find(']'); if (r == std::string::npos) return false;
        u.kind = UntilCond::MEM;
        u.addr = (uint16_t)strtol(lhs.substr(1, r-1).c_str(), nullptr, 0);
        u.word = (r+1 < lhs.size() && (lhs[r+1]=='w' || lhs[r+1]=='W'));
        return true;
    }
    if (lhs=="PC" || lhs=="pc") { u.kind = UntilCond::PC; u.addr = (uint16_t)u.val; return true; }
    return false;
}

} // namespace untilcond
