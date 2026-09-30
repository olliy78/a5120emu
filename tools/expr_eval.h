/**
 * @file expr_eval.h
 * @brief Ausdrucks-Evaluator für k1520dbg (rekursiv-absteigend), maschinenfrei.
 *
 * Wertet Ausdrücke wie `(HL & 0xFF)==0xF7`, `[D1BE]w < 0x100`, `B*2+1`, `[HL+5]`
 * gegen einen Register-Snapshot + Speicher + Symboltabelle aus. Genutzt von
 * `b … if`, `disp`, `logpoint` und der `x`-Adresse — für ZVE1/ZVE2 (Z80-Sicht,
 * @ref eval) wie für den U8001 (@ref evalEnv mit eigenem Register-/Speicherleser,
 * tools/dbg_u8000.h).
 *
 * Grammatik (niedrige → hohe Präzedenz):
 *   cmp(== != < > <= >=) · `|` · `^` · `&` · shift(<< >>) · add(+ -) ·
 *   mul(* / %) · unär(- ~) · primary
 * primary: Zahl (0x../..h/dez, `%hex`) · `<<seg>>off` (Wert seg·2¹⁶+off) · Symbol ·
 *          Register · (HL)/(DE)/(BC)/(SP) (nur Z80) · ( expr ) ·
 *          [expr] / [expr]w / [expr]l (Speicher: Byte/Wort/Langwort, Index ist selbst
 *          ein Ausdruck) · [em:expr]… (roh ins DRAM des EM, Zahlen darin hex)
 * `%` und `<<` an der Stelle eines OPERANDEN sind eindeutig (sonst Rest/Schieben).
 *
 * Header-only & über Callbacks parametrisiert (Byte-Leser, Symbol-Resolver) →
 * ohne Maschine unit-testbar. Bei Parse-/Bereichsfehlern wird `ok=false` gesetzt.
 * Gerechnet wird in 64 Bit (RQ-Register des U8001; `long` ist unter Windows 32 Bit).
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

/// Allgemeine Umgebung (U8001 und alles, was keine Z80-Sicht ist).
struct Env {
    /// Register: Name in GROSSschrift → Wert; false = kein Register.
    std::function<bool(const std::string& upper, long long& v)> reg;
    /// Speicher: Adresse (wie berechnet), Breite 1/2/4, roh (`[em:…]`) → Wert; false = Fehler.
    std::function<bool(long long addr, int size, bool raw, long long& v)> mem;
    /// Symbol → Wert (beliebig breit); false = unbekannt.
    std::function<bool(const std::string& name, long long& v)> sym;
    /// Z80-Schreibweisen (HL)/(DE)/(BC)/(SP) erlaubt.
    bool z80Paren = false;
};

namespace detail {
struct Parser {
    const Env& env;
    const std::string& e;
    size_t i = 0;
    bool   ok = true;
    bool   hexDefault = false;   ///< in [em:…]: Zahlen ohne Präfix sind hex

    void ws(){ while(i<e.size() && e[i]==' ') ++i; }
    bool eat(const char* op){ ws(); size_t n=strlen(op);
        if(i+n<=e.size() && e.compare(i,n,op)==0){ i+=n; return true; } return false; }
    bool ciEat(const char* op){ ws(); size_t n=strlen(op); if(i+n>e.size()) return false;
        for(size_t k=0;k<n;++k) if((char)toupper((unsigned char)e[i+k])!=(char)toupper((unsigned char)op[k])) return false;
        i+=n; return true; }
    bool nextIs(char c){ ws(); return i<e.size() && e[i]==c; }

    long long parseAll(){ long long v=cmp(); ws(); if(i!=e.size()) ok=false; return v; }

    long long cmp(){ long long v=bor();
        for(;;){ if(eat("==")) v=(v==bor()); else if(eat("!=")) v=(v!=bor());
            else if(eat("<=")) v=(v<=bor()); else if(eat(">=")) v=(v>=bor());
            else { ws(); if(i<e.size()&&e[i]=='<'&&!(i+1<e.size()&&e[i+1]=='<')){ ++i; v=(v<bor()); }
                   else if(i<e.size()&&e[i]=='>'&&!(i+1<e.size()&&e[i+1]=='>')){ ++i; v=(v>bor()); }
                   else break; } }
        return v; }
    long long bor(){  long long v=bxor(); while(true){ ws();
        if(i<e.size()&&e[i]=='|'){ ++i; v|=bxor(); } else break; } return v; }
    long long bxor(){ long long v=band(); while(eat("^")) v^=band(); return v; }
    long long band(){ long long v=shift(); while(true){ ws();
        if(i<e.size()&&e[i]=='&'){ ++i; v&=shift(); } else break; } return v; }
    long long shift(){ long long v=add(); for(;;){ if(eat("<<")) v<<=add(); else if(eat(">>")) v>>=add(); else break; } return v; }
    long long add(){ long long v=mul(); for(;;){ if(eat("+")) v+=mul(); else if(eat("-")) v-=mul(); else break; } return v; }
    long long mul(){ long long v=unary(); for(;;){
        if(eat("*")) v*=unary();
        else if(eat("/")){ long long d=unary(); if(d==0){ok=false; } else v/=d; }
        else if(eat("%")){ long long d=unary(); if(d==0){ok=false; } else v%=d; }
        else break; } return v; }
    long long unary(){ if(eat("-")) return -unary(); if(eat("~")) return ~unary(); return primary(); }

    static bool isTokChar(char c){
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='@'||c=='?'||c=='$'||c=='.'; }

    long long number(const std::string& tok, int base){
        char* end=nullptr; long long v=strtoll(tok.c_str(),&end,base);
        if(end==tok.c_str() || (end&&*end)) ok=false; return v; }

    long long regOr(const std::string& tok){
        std::string U; for(char c:tok) U+=(char)toupper((unsigned char)c);
        long long v=0;
        if(env.reg && env.reg(U, v)) return v;
        if(env.sym && env.sym(tok, v)) return v;                // Symbol
        // hex (..H) oder base-0-Zahl (in [em:…] hex)
        if(!tok.empty() && (tok.back()=='H'||tok.back()=='h'))
            return number(tok.substr(0,tok.size()-1),16);
        return number(tok, hexDefault? 16 : 0); }

    long long primary(){ ws();
        if(i>=e.size()){ ok=false; return 0; }
        if(env.z80Paren && nextIs('(')){
            static const char* const rp[4]={"(HL)","(DE)","(BC)","(SP)"};
            for(const char* p: rp) if(ciEat(p)){
                long long a=0, v=0; std::string r(p+1,2);
                if(!env.reg || !env.reg(r,a) || !env.mem || !env.mem(a,1,false,v)) ok=false;
                return v; }
        }
        if(nextIs('(')){ ++i; long long v=cmp(); if(!eat(")")) ok=false; return v; }
        if(nextIs('%')){                                        // %hex (Zilog-Schreibweise)
            ++i; size_t st=i; while(i<e.size() && isxdigit((unsigned char)e[i])) ++i;
            if(i==st){ ok=false; return 0; }
            return number(e.substr(st,i-st),16); }
        if(i+1<e.size() && e[i]=='<' && e[i+1]=='<'){           // <<seg>>off
            i+=2; long long s=primary(); if(!eat(">>")) { ok=false; return 0; }
            long long o=primary();
            if(s<0||s>127){ ok=false; return 0; }
            return (s<<16) | (o & 0xFFFF); }
        if(nextIs('[')){ ++i;
            bool raw=false; bool saveHex=hexDefault;
            if(ciEat("em:")){ raw=true; hexDefault=true; }
            long long a=cmp(); hexDefault=saveHex;
            if(!eat("]")) ok=false;
            int size=1;
            if(i<e.size()&&(e[i]=='w'||e[i]=='W')){ size=2; ++i; }
            else if(i<e.size()&&(e[i]=='l'||e[i]=='L')){ size=4; ++i; }
            long long v=0;
            if(!env.mem || !env.mem(a,size,raw,v)) ok=false;
            return v; }
        // Token: Register / Symbol / Zahl
        size_t st=i;
        while(i<e.size() && isTokChar(e[i])) ++i;
        if(i==st){ ok=false; return 0; }
        return regOr(e.substr(st,i-st)); }
};
} // namespace detail

/**
 * Einen Ausdruck in einer allgemeinen Umgebung auswerten (U8001 u. a.).
 * @param ok [out] false bei Parse-/Bereichs-/Lesefehler.
 */
inline long long evalEnv(const std::string& expr, const Env& env, bool& ok) {
    detail::Parser p{env, expr};
    long long v = p.parseAll();
    ok = p.ok;
    return v;
}

/**
 * Einen vollständigen Ausdruck in der Z80-Sicht auswerten.
 * @param expr      der Ausdruckstext.
 * @param r         Register-Snapshot.
 * @param readByte  Speicher-Byte-Leser (für [expr], (rr)); Worte little-endian.
 * @param findSym   Symbol-Resolver (oder leer/`nullptr` → keine Symbole).
 * @param ok        [out] false bei Parse-/Bereichsfehler (Wert dann undefiniert/0).
 * @return          der ausgewertete (vorzeichenbehaftete) Wert.
 */
inline long eval(const std::string& expr, const RegView& r,
                 const ReadByte& readByte, const FindSym& findSym, bool& ok) {
    Env env;
    env.z80Paren = true;
    env.reg = [&r](const std::string& U, long long& v)->bool{
        if(U=="A"){ v=(r.AF>>8)&0xFF; return true; } if(U=="F"){ v=r.AF&0xFF; return true; }
        if(U=="B"){ v=(r.BC>>8)&0xFF; return true; } if(U=="C"){ v=r.BC&0xFF; return true; }
        if(U=="D"){ v=(r.DE>>8)&0xFF; return true; } if(U=="E"){ v=r.DE&0xFF; return true; }
        if(U=="H"){ v=(r.HL>>8)&0xFF; return true; } if(U=="L"){ v=r.HL&0xFF; return true; }
        if(U=="AF"){ v=r.AF; return true; } if(U=="BC"){ v=r.BC; return true; }
        if(U=="DE"){ v=r.DE; return true; } if(U=="HL"){ v=r.HL; return true; }
        if(U=="IX"){ v=r.IX; return true; } if(U=="IY"){ v=r.IY; return true; }
        if(U=="SP"){ v=r.SP; return true; } if(U=="PC"){ v=r.PC; return true; }
        if(U=="I"){ v=r.I; return true; }   if(U=="R"){ v=r.R; return true; }
        return false; };
    env.mem = [&readByte](long long a, int size, bool raw, long long& v)->bool{
        if (raw || !readByte) return false;              // em: gibt es nur in der U8001-Sicht
        uint16_t ad=(uint16_t)a; v=0;
        for (int k=size-1;k>=0;--k) v = (v<<8) | readByte((uint16_t)(ad+k));   // little-endian
        return true; };
    if (findSym)
        env.sym = [&findSym](const std::string& n, long long& v)->bool{
            uint16_t s; if(!findSym(n,s)) return false; v=s; return true; };
    return (long)evalEnv(expr, env, ok);
}

} // namespace expreval
