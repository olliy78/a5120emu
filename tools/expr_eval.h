/**
 * @file expr_eval.h
 * @brief Ausdrucks-Evaluator für k1520dbg (rekursiv-absteigend), maschinenfrei.
 *
 * Wertet Ausdrücke wie `(HL & 0xFF)==0xF7`, `[D1BE]w < 0x100`, `B*2+1`, `[HL+5]`
 * gegen einen Register-Snapshot + Speicher + Symboltabelle aus. Genutzt von
 * `b … if`, `disp`, `logpoint` und der `x`-Adresse.
 *
 * Grammatik (niedrige → hohe Präzedenz):
 *   cmp(== != < > <= >=) · `|` · `^` · `&` · shift(<< >>) · add(+ -) ·
 *   mul(* / %) · unär(- ~) · primary
 * primary: Zahl (0x../..h/dez) · Symbol · Register · (HL)/(DE)/(BC)/(SP) ·
 *          [expr] / [expr]w (Speicher, Index ist selbst ein Ausdruck) · ( expr )
 *
 * Header-only & über Callbacks parametrisiert (Byte-Leser, Symbol-Resolver) →
 * ohne Maschine unit-testbar. Bei Parse-/Bereichsfehlern wird `ok=false` gesetzt.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <functional>

namespace expreval {

/// Register-Sicht (Teilmenge eines Z80-Snapshots), die der Parser liest.
struct RegView {
    uint16_t AF=0, BC=0, DE=0, HL=0, IX=0, IY=0, SP=0, PC=0;
    uint8_t  I=0, R=0;
};

/// Byte-Leser für [expr]/(rr): Adresse → Speicherbyte.
using ReadByte = std::function<uint8_t(uint16_t)>;
/// Symbol-Resolver: Name → Wert; @return true, wenn das Symbol existiert.
using FindSym  = std::function<bool(const std::string&, uint16_t&)>;

namespace detail {
struct Parser {
    const RegView&   r;
    const ReadByte&  rb;
    const FindSym&   fs;
    const std::string& e;
    size_t i = 0;
    bool   ok = true;

    void ws(){ while(i<e.size() && e[i]==' ') ++i; }
    bool eat(const char* op){ ws(); size_t n=strlen(op);
        if(i+n<=e.size() && e.compare(i,n,op)==0){ i+=n; return true; } return false; }
    bool ciEat(const char* op){ ws(); size_t n=strlen(op); if(i+n>e.size()) return false;
        for(size_t k=0;k<n;++k) if((char)toupper(e[i+k])!=(char)toupper(op[k])) return false;
        i+=n; return true; }
    bool nextIs(char c){ ws(); return i<e.size() && e[i]==c; }

    long parseAll(){ long v=cmp(); ws(); if(i!=e.size()) ok=false; return v; }

    long cmp(){ long v=bor();
        for(;;){ if(eat("==")) v=(v==bor()); else if(eat("!=")) v=(v!=bor());
            else if(eat("<=")) v=(v<=bor()); else if(eat(">=")) v=(v>=bor());
            else { ws(); if(i<e.size()&&e[i]=='<'&&!(i+1<e.size()&&e[i+1]=='<')){ ++i; v=(v<bor()); }
                   else if(i<e.size()&&e[i]=='>'&&!(i+1<e.size()&&e[i+1]=='>')){ ++i; v=(v>bor()); }
                   else break; } }
        return v; }
    long bor(){  long v=bxor(); while(true){ ws();
        if(i<e.size()&&e[i]=='|'){ ++i; v|=bxor(); } else break; } return v; }
    long bxor(){ long v=band(); while(eat("^")) v^=band(); return v; }
    long band(){ long v=shift(); while(true){ ws();
        if(i<e.size()&&e[i]=='&'){ ++i; v&=shift(); } else break; } return v; }
    long shift(){ long v=add(); for(;;){ if(eat("<<")) v<<=add(); else if(eat(">>")) v>>=add(); else break; } return v; }
    long add(){ long v=mul(); for(;;){ if(eat("+")) v+=mul(); else if(eat("-")) v-=mul(); else break; } return v; }
    long mul(){ long v=unary(); for(;;){
        if(eat("*")) v*=unary();
        else if(eat("/")){ long d=unary(); if(d==0){ok=false; } else v/=d; }
        else if(eat("%")){ long d=unary(); if(d==0){ok=false; } else v%=d; }
        else break; } return v; }
    long unary(){ if(eat("-")) return -unary(); if(eat("~")) return ~unary(); return primary(); }

    long regOr(const std::string& tok){
        std::string U; for(char c:tok) U+=(char)toupper(c);
        if(U=="A")return (r.AF>>8)&0xFF; if(U=="F")return r.AF&0xFF;
        if(U=="B")return (r.BC>>8)&0xFF; if(U=="C")return r.BC&0xFF;
        if(U=="D")return (r.DE>>8)&0xFF; if(U=="E")return r.DE&0xFF;
        if(U=="H")return (r.HL>>8)&0xFF; if(U=="L")return r.HL&0xFF;
        if(U=="AF")return r.AF; if(U=="BC")return r.BC; if(U=="DE")return r.DE;
        if(U=="HL")return r.HL; if(U=="IX")return r.IX; if(U=="IY")return r.IY;
        if(U=="SP")return r.SP; if(U=="PC")return r.PC; if(U=="I")return r.I; if(U=="R")return r.R;
        uint16_t sv; if(fs && fs(tok, sv)) return sv;          // Symbol
        // hex (..H) oder base-0-Zahl
        if(!tok.empty() && (tok.back()=='H'||tok.back()=='h')){
            char* end=nullptr; long v=strtol(tok.substr(0,tok.size()-1).c_str(),&end,16);
            if(end && *end) ok=false; return v; }
        char* end=nullptr; long v=strtol(tok.c_str(),&end,0);
        if(end==tok.c_str() || (end&&*end)) ok=false; return v; }

    long primary(){ ws();
        if(i>=e.size()){ ok=false; return 0; }
        if(nextIs('(')){
            if(ciEat("(HL)")) return rb(r.HL);
            if(ciEat("(DE)")) return rb(r.DE);
            if(ciEat("(BC)")) return rb(r.BC);
            if(ciEat("(SP)")) return rb(r.SP);
            ++i; long v=cmp(); if(!eat(")")) ok=false; return v; }
        if(nextIs('[')){ ++i; long a=cmp(); if(!eat("]")) ok=false;
            bool w=false; if(i<e.size()&&(e[i]=='w'||e[i]=='W')){ w=true; ++i; }
            uint16_t ad=(uint16_t)a;
            return w ? (rb(ad)|(rb((uint16_t)(ad+1))<<8)) : rb(ad); }
        // Token: Register / Symbol / Zahl
        size_t st=i;
        while(i<e.size()){ char c=e[i];
            if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='@'||c=='?'||c=='$'||c=='.') ++i; else break; }
        if(i==st){ ok=false; return 0; }
        return regOr(e.substr(st,i-st)); }
};
} // namespace detail

/**
 * Einen vollständigen Ausdruck auswerten.
 * @param expr      der Ausdruckstext.
 * @param r         Register-Snapshot.
 * @param readByte  Speicher-Byte-Leser (für [expr], (rr)).
 * @param findSym   Symbol-Resolver (oder leer/`nullptr` → keine Symbole).
 * @param ok        [out] false bei Parse-/Bereichsfehler (Wert dann undefiniert/0).
 * @return          der ausgewertete (vorzeichenbehaftete) Wert.
 */
inline long eval(const std::string& expr, const RegView& r,
                 const ReadByte& readByte, const FindSym& findSym, bool& ok) {
    detail::Parser p{r, readByte, findSym, expr};
    long v = p.parseAll();
    ok = p.ok;
    return v;
}

} // namespace expreval
