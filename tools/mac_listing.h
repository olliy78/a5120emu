/**
 * @file mac_listing.h
 * @brief Fremdquellen (`.MAC`/`.ASM`) als Annotationsquelle — Adresse → Quellzeile.
 *
 * Gegenstück zu tools/prn_listing.h: Jenes braucht ein **Listing** (Adressspalte +
 * emittierte Objektbytes), wie es nur der eigene Assemblerlauf liefert. Fremd-
 * Betriebssysteme (UDOS, SCPX …) liegen dagegen als **reiner Quelltext** vor —
 * `ORG 0`, keine Adressspalte. Bisher hieß das: Opcode-Längen von Hand durchzählen,
 * um zu wissen, welche Zeile bei welcher Adresse steht.
 *
 * Dieser Header assembliert den Quelltext so weit, dass jede Zeile eine Adresse
 * (und — für den Versatz-Abgleich, s. `Image`) ihre Objektbytes bekommt:
 *
 *   - **Encoder aus dem Disassembler abgeleitet.** Statt einer handgepflegten
 *     Opcode-Tabelle wird tools/z80dis_min.h einmalig *rückwärts* aufgezogen:
 *     jede Opcode-Kombination wird mit Platzhalter-Operanden dekodiert, die
 *     Operandenfelder werden durch Byte-Variation ermittelt und der so
 *     normalisierte Mnemonik-Text wird zum Schlüssel („LD BC,$" → DD/Opcode +
 *     Feldlage). Damit ist der Assembler per Konstruktion mit dem Disassembler
 *     konsistent und deckt denselben Befehlsvorrat ab.
 *   - **Mxxxx-Labelanker.** Disassemblat-Quellen tragen die Originaladresse im
 *     Labelnamen (`M03F8:`). Weicht der laufende Adresszähler davon ab, wird er
 *     nachgeführt und die Abweichung gemeldet — ein einzelner falsch gezählter
 *     Befehl verschiebt also nicht mehr alles Folgende.
 *   - **Wildcards.** Adressabhängige Operandenbytes (Symbol-Adressen, relative
 *     Sprünge) werden als „variabel" markiert. Der Versatz-Abgleich (`@auto`)
 *     vergleicht nur die relokationsunabhängigen Bytes.
 *
 * Header-only, hängt nur an prn_listing.h/z80dis_min.h + STL → unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include "tools/prn_listing.h"
#include "tools/z80dis_min.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace maclst {

// ─── kleine Textwerkzeuge ────────────────────────────────────────────────────
inline std::string upper(std::string s){
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
inline std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
inline std::string noSpace(const std::string& s){
    std::string o; o.reserve(s.size());
    for (char c : s) if (c != ' ' && c != '\t') o.push_back(c);
    return o;
}
inline bool isIdentStart(char c){
    return isalpha((unsigned char)c) || c=='?' || c=='@' || c=='.' || c=='_';
}
inline bool isIdentChar(char c){
    return isalnum((unsigned char)c) || c=='?' || c=='@' || c=='.' || c=='_' || c=='\'';
}

/**
 * Zahl in MACRO-80-Schreibweise parsen: `0FFH`/`12H` (hex), `1011B` (binär),
 * `17O`/`17Q` (oktal), `123`/`123D` (dezimal), `0x1F`, `'A'` (Zeichen).
 * @return false, wenn @p t keine reine Zahl ist.
 */
inline bool parseNum(const std::string& t, long& v){
    if (t.empty()) return false;
    if (t.size() >= 3 && t.front()=='\'' && t.back()=='\'')
        { v = (unsigned char)t[1]; return true; }
    std::string s = upper(t);
    if (s.size() > 2 && s[0]=='0' && s[1]=='X'){
        char* e=nullptr; v = strtol(s.c_str()+2, &e, 16); return e && *e=='\0'; }
    char suf = s.back();
    int base = 10; std::string body = s;
    if (suf=='H'){ base=16; body.pop_back(); }
    else if (suf=='B'){ base=2;  body.pop_back(); }
    else if (suf=='O'||suf=='Q'){ base=8; body.pop_back(); }
    else if (suf=='D'){ base=10; body.pop_back(); }
    if (body.empty()) return false;
    // Hex-Literale müssen mit einer Ziffer beginnen (MACRO-80-Konvention 0FFH).
    if (!isdigit((unsigned char)body[0])) return false;
    char* e=nullptr; long x = strtol(body.c_str(), &e, base);
    if (!e || *e != '\0') return false;
    v = x; return true;
}

// ─── Ausdrucksauswertung ─────────────────────────────────────────────────────
/// Symboltabelle des Assemblerlaufs (Labels + EQU-Werte), Namen in Großschreibung.
using SymTab = std::map<std::string,long>;

/**
 * Wertet einen MACRO-80-Ausdruck aus: Zahlen, Symbole, `$` (aktueller Adress-
 * zähler), unäres `+`/`-`/`NOT`, `* / MOD SHL SHR`, `+ -`, `AND OR XOR`.
 * @param unresolved [out] wird gesetzt, wenn ein unbekanntes Symbol vorkam
 *                   (der Ausdruck gilt dann als adressabhängig/unsicher).
 */
inline long evalExpr(const std::string& expr, const SymTab& syms, long loc,
                     bool* unresolved = nullptr, bool* has_symbol = nullptr){
    struct P {
        const std::string& s; size_t i=0; const SymTab& sy; long loc;
        bool unres=false, sym=false;
        P(const std::string& str, const SymTab& t, long l):s(str),sy(t),loc(l){}
        void sp(){ while(i<s.size() && (s[i]==' '||s[i]=='\t')) ++i; }
        bool word(const char* w){                       // Operator-Wort (AND/OR/…)
            sp(); size_t n=strlen(w);
            if (i+n>s.size()) return false;
            for(size_t k=0;k<n;++k) if (toupper((unsigned char)s[i+k])!=w[k]) return false;
            if (i+n<s.size() && isIdentChar(s[i+n])) return false;
            i+=n; return true;
        }
        long primary(){
            sp();
            if (i<s.size() && s[i]=='('){ ++i; long v=orx(); sp(); if(i<s.size()&&s[i]==')') ++i; return v; }
            if (i<s.size() && s[i]=='-'){ ++i; return -primary(); }
            if (i<s.size() && s[i]=='+'){ ++i; return  primary(); }
            if (word("NOT")) return ~primary();
            if (i<s.size() && s[i]=='$' && (i+1>=s.size() || !isIdentChar(s[i+1]))){ ++i; return loc; }
            if (i<s.size() && s[i]=='\''){                 // 'A' bzw. 'AB'
                size_t j=i+1; long v=0; int n=0;
                while (j<s.size() && s[j]!='\''){ v=(v<<8)|(unsigned char)s[j]; ++j; ++n; }
                i = (j<s.size())? j+1 : j;
                return n? v : 0;
            }
            size_t a=i;
            if (i<s.size() && (isalnum((unsigned char)s[i]) || isIdentStart(s[i]))){
                while (i<s.size() && isIdentChar(s[i])) ++i;
                std::string tok = s.substr(a, i-a);
                long v;
                if (parseNum(tok, v)) return v;
                sym = true;
                auto it = sy.find(upper(tok));
                if (it != sy.end()) return it->second;
                unres = true; return 0;
            }
            return 0;
        }
        long mul(){ long v=primary();
            for(;;){ sp();
                if (i<s.size() && s[i]=='*'){ ++i; v*=primary(); }
                else if (i<s.size() && s[i]=='/'){ ++i; long d=primary(); v = d? v/d : 0; }
                else if (word("MOD")){ long d=primary(); v = d? v%d : 0; }
                else if (word("SHL")){ v = v << (primary() & 31); }
                else if (word("SHR")){ v = (long)((unsigned long)v >> (primary() & 31)); }
                else return v; } }
        long add(){ long v=mul();
            for(;;){ sp();
                if (i<s.size() && s[i]=='+'){ ++i; v+=mul(); }
                else if (i<s.size() && s[i]=='-'){ ++i; v-=mul(); }
                else return v; } }
        long orx(){ long v=add();
            for(;;){
                if (word("AND")) v &= add();
                else if (word("OR")) v |= add();
                else if (word("XOR")) v ^= add();
                else return v; } }
    } p(expr, syms, loc);
    long v = p.orx();
    if (unresolved) *unresolved = p.unres;
    if (has_symbol) *has_symbol = p.sym;
    return v;
}

// ─── Encoder-Tabelle (aus dem Disassembler abgeleitet) ───────────────────────
/**
 * Ein Befehlsmuster: Byte-Schablone + Lage der variablen Felder.
 * `p_*` = Byte-Position des Feldes in @ref bytes (−1 = nicht vorhanden).
 */
struct Enc {
    uint8_t bytes[4] = {0,0,0,0};
    int  len    = 1;
    int  p_imm8 = -1;   ///< 8-Bit-Konstante
    int  p_imm16= -1;   ///< 16-Bit-Konstante/Adresse (lo, hi)
    int  p_rel  = -1;   ///< relativer Sprung (JR/DJNZ)
    int  p_disp = -1;   ///< Index-Displacement (IX+d)
};

namespace detail {

/// Text-Span (Anfang/Ende) auf Hexziffern + abschließendes 'H' ausdehnen.
inline void widenSpan(const std::string& t, size_t& a, size_t& b){
    auto hex=[&](char c){ return isxdigit((unsigned char)c)!=0; };
    while (a > 0 && hex(t[a-1])) --a;
    while (b < t.size() && hex(t[b])) ++b;
    if (b < t.size() && (t[b]=='H'||t[b]=='h')) ++b;
}

/// Erste/letzte abweichende Position zweier gleich langer Texte.
inline bool diffSpan(const std::string& x, const std::string& y, size_t& a, size_t& b){
    if (x.size()!=y.size()) return false;
    size_t i=0; while (i<x.size() && x[i]==y[i]) ++i;
    if (i==x.size()) return false;
    size_t j=x.size(); while (j>i && x[j-1]==y[j-1]) --j;
    a=i; b=j; return true;
}

/// Mnemonik + Operanden aus einem Disassembler-/Quelltext trennen.
inline void splitMnem(const std::string& text, std::string& mnem, std::string& ops){
    std::string t = trim(text);
    size_t sp = t.find_first_of(" \t");
    if (sp == std::string::npos){ mnem = upper(t); ops.clear(); return; }
    mnem = upper(t.substr(0,sp));
    ops  = noSpace(upper(t.substr(sp+1)));
}

/// Reine Zahlen-Operanden auf ihren Dezimalwert normieren (RST 38H → RST 56),
/// damit opcode-codierte Zahlen auf beiden Seiten gleich aussehen.
inline std::string canonNumericOps(const std::string& ops){
    std::string out; std::string cur;
    auto flush=[&]{
        long v;
        if (!cur.empty() && parseNum(cur, v)) out += std::to_string(v);
        else out += cur;
        cur.clear(); };
    for (char c : ops){ if (c==','){ flush(); out.push_back(','); } else cur.push_back(c); }
    flush();
    return out;
}

inline std::string makeKey(const std::string& mnem, const std::string& ops){
    return ops.empty() ? mnem : (mnem + " " + canonNumericOps(ops));
}

/// Eine Byte-Schablone dekodieren (Bytes jenseits der Schablone = 0).
inline z80dis::Insn decodeTemplate(const uint8_t* tpl, int n){
    return z80dis::decode([&](uint16_t a)->uint8_t{ return a < (uint16_t)n ? tpl[a] : 0; }, 0);
}

/**
 * Eine Opcode-Schablone in einen @ref Enc + Schlüssel übersetzen.
 * @param tpl   Schablone (Präfixe + Opcode + Füllbytes).
 * @param n     Schablonenlänge.
 * @param free  Positionen, die Operandenbytes sein KÖNNEN (nie Opcode-Bytes).
 */
inline bool buildEntry(const uint8_t* tpl, int n, const std::vector<int>& free,
                       std::string& key, Enc& e){
    uint8_t base[8]; memcpy(base, tpl, (size_t)n);
    z80dis::Insn ins = decodeTemplate(base, n);
    if (ins.len > n) return false;                       // Schablone zu kurz
    std::string text = ins.text;
    if (text.rfind("NOP*",0)==0) return false;           // undefinierter ED-Opcode
    // Welche freien Positionen liegen im Befehl — und welchen Textabschnitt
    // steuern sie? Byte variieren und die Textänderung beobachten.
    struct Field { int pos; size_t a, b; };
    std::vector<Field> fields;
    for (int p : free){
        if (p >= ins.len) continue;
        uint8_t alt[8]; memcpy(alt, base, (size_t)n);
        alt[p] = (uint8_t)(base[p] + 0x10);              // gleiche Stellenzahl, gleiches Vorzeichen
        z80dis::Insn i2 = decodeTemplate(alt, n);
        if (i2.len != ins.len) continue;
        size_t a,b;
        if (!diffSpan(text, i2.text, a, b)) continue;
        widenSpan(text, a, b);
        fields.push_back({p,a,b});
    }
    // Felder zusammenfassen: zwei benachbarte Bytes mit überlappendem Span = 16 Bit.
    e = Enc{}; e.len = ins.len;
    memcpy(e.bytes, base, (size_t)ins.len);
    std::vector<std::pair<size_t,size_t>> spans;
    for (size_t i=0;i<fields.size();){
        if (i+1 < fields.size() && fields[i+1].pos == fields[i].pos+1
            && !(fields[i].b <= fields[i+1].a || fields[i+1].b <= fields[i].a)){
            e.p_imm16 = fields[i].pos;
            spans.push_back({std::min(fields[i].a,fields[i+1].a),
                             std::max(fields[i].b,fields[i+1].b)});
            i += 2; continue;
        }
        size_t a=fields[i].a, b=fields[i].b;
        // Displacement erkennt man am Kontext "(IX+"/"(IY-" unmittelbar links.
        bool disp = a>=4 && (text.compare(a-4,3,"(IX")==0 || text.compare(a-4,3,"(IY")==0);
        if (disp)                       e.p_disp = fields[i].pos;
        else if (ins.has_target && ins.len==2 && ins.is_jump) e.p_rel = fields[i].pos;
        else                            e.p_imm8 = fields[i].pos;
        spans.push_back({a,b});
        ++i;
    }
    // Spans (von hinten) durch "$" ersetzen → Schlüsseltext.
    std::sort(spans.begin(), spans.end());
    for (auto it = spans.rbegin(); it != spans.rend(); ++it)
        text = text.substr(0,it->first) + "$" + text.substr(it->second);
    std::string mnem, ops; splitMnem(text, mnem, ops);
    key = makeKey(mnem, ops);
    return true;
}

/// Die (einmalig gebaute) Schlüssel→Kodierung-Tabelle des Z80-Befehlssatzes.
inline const std::map<std::string,Enc>& table(){
    static const std::map<std::string,Enc> t = []{
        std::map<std::string,Enc> m;
        auto add=[&](const uint8_t* tpl, int n, const std::vector<int>& free){
            std::string k; Enc e;
            if (!buildEntry(tpl,n,free,k,e)) return;
            if (m.find(k)==m.end()) m.emplace(k,e);
            // Der Disassembler vereinfacht den kombinierten Fall DD/FD + (IX+d)
            // kosmetisch zu IXH/IXL (s. z80dis_min.h). Gemeint — und in jeder Quelle
            // so geschrieben — ist dort das normale H/L. Zweitschlüssel eintragen.
            if (e.p_disp >= 0){
                std::string alias = k; bool changed=false;
                for (const char* pat : {"IXH","IYH"}){
                    size_t p; while ((p=alias.find(pat))!=std::string::npos){ alias.replace(p,3,"H"); changed=true; } }
                for (const char* pat : {"IXL","IYL"}){
                    size_t p; while ((p=alias.find(pat))!=std::string::npos){ alias.replace(p,3,"L"); changed=true; } }
                if (changed && m.find(alias)==m.end()) m.emplace(alias,e);
            } };
        // Reihenfolge = Priorität: kürzeste/kanonische Kodierung gewinnt.
        for (int op=0; op<256; ++op){                    // ohne Präfix
            if (op==0xCB||op==0xED||op==0xDD||op==0xFD) continue;
            uint8_t tpl[4]={(uint8_t)op,0x05,0x06,0x07}; add(tpl,4,{1,2});
        }
        for (int op=0; op<256; ++op){ uint8_t tpl[2]={0xCB,(uint8_t)op}; add(tpl,2,{}); }
        for (int op=0; op<256; ++op){ uint8_t tpl[4]={0xED,(uint8_t)op,0x05,0x06}; add(tpl,4,{2,3}); }
        for (uint8_t pfx : {0xDDu, 0xFDu}){
            for (int op=0; op<256; ++op){
                if (op==0xCB) continue;
                uint8_t tpl[4]={pfx,(uint8_t)op,0x05,0x06}; add(tpl,4,{2,3});
            }
            for (int op=0; op<256; ++op){                 // DD CB d op
                // Nur die dokumentierte Form (z==6): der Disassembler zeigt für alle
                // acht z-Varianten denselben Text "(IX+d)" — ohne diese Auswahl würde
                // der Schlüssel auf die undokumentierte Kopie z==0 zeigen.
                if ((op & 7) != 6) continue;
                uint8_t tpl[4]={pfx,0xCB,0x05,(uint8_t)op}; add(tpl,4,{2});
            }
        }
        return m;
    }();
    return t;
}

} // namespace detail

// ─── Quellzeile ──────────────────────────────────────────────────────────────
struct SrcLine {
    std::string label;   ///< Label ohne ':' (leer, wenn keins)
    std::string mnem;    ///< Mnemonik/Direktive in Großschreibung
    std::string ops;     ///< Operandenfeld (roh, ohne Kommentar)
};

/// Eine Quellzeile in Label / Mnemonik / Operanden zerlegen (Kommentar entfernt).
inline bool splitSource(const std::string& raw, SrcLine& out){
    out = SrcLine{};
    // Kommentar abschneiden — ';' zählt nicht innerhalb von Zeichenketten.
    std::string s; char q=0;
    for (size_t i=0;i<raw.size();++i){
        char c = raw[i];
        if (q){ if (c==q) q=0; }
        else if (c=='\''||c=='"') q=c;
        else if (c==';') break;
        s.push_back(c);
    }
    s = trim(s);
    if (s.empty()) return false;
    size_t i = 0;
    bool at_col0 = !raw.empty() && raw[0]!=' ' && raw[0]!='\t';
    if (at_col0 && isIdentStart(s[0])){
        size_t a=i; while (i<s.size() && isIdentChar(s[i]) && s[i]!='\'') ++i;
        std::string tok = upper(s.substr(a,i-a));
        bool colon = (i<s.size() && s[i]==':');
        // Ohne ':' kann in Spalte 1 auch eine Direktive stehen (TITLE, SUBTTL,
        // .PHASE …). Sie als Label zu lesen, verschöbe den ganzen Rest.
        static const std::set<std::string> col0_directives = {
            "TITLE","SUBTTL","NAME","PAGE","ORG","END",".PHASE",".DEPHASE",".Z80",".8080",
            "ASEG","CSEG","DSEG","PUBLIC","EXTRN","GLOBAL","ENTRY",".RADIX",".LIST",".XLIST",
            ".COMMENT",".PRINTX","INCLUDE","IF","IFT","IFF","IFE","IF1","IF2","IFDEF",
            "IFNDEF","ELSE","ENDIF","ENDC" };
        if (!colon && col0_directives.count(tok)) i = a;      // doch Mnemonik
        else { out.label = tok; if (colon) ++i; }
    }
    while (i<s.size() && (s[i]==' '||s[i]=='\t')) ++i;
    size_t a=i; while (i<s.size() && !isspace((unsigned char)s[i])) ++i;
    out.mnem = upper(s.substr(a,i-a));
    while (i<s.size() && (s[i]==' '||s[i]=='\t')) ++i;
    out.ops = trim(s.substr(i));
    return !(out.label.empty() && out.mnem.empty());
}

// ─── Operanden-Normalisierung (Quellseite) ───────────────────────────────────
namespace detail {

inline bool isRegKeyword(const std::string& t){
    static const std::set<std::string> k = {
        "A","B","C","D","E","H","L","I","R","AF","BC","DE","HL","SP","IX","IY","AF'",
        "IXH","IXL","IYH","IYL","(HL)","(BC)","(DE)","(SP)","(C)","(IX)","(IY)",
        "NZ","Z","NC","PO","PE","P","M" };
    return k.count(t) != 0;
}

/// Ein Operand in der Schlüssel-Schreibweise + der zugehörige Ausdruck.
struct OpndForm {
    std::string key;      ///< "B" / "(HL)" / "$" / "($)" / "(IX+$)"
    std::string expr;     ///< auszuwertender Ausdruck (leer bei Registern)
    bool        is_disp = false;
    bool        constant_ok = false;   ///< Ausdruck ist eine reine Zahl (Alternativschlüssel)
    long        constant = 0;
};

inline OpndForm classifyOperand(const std::string& raw){
    OpndForm o;
    std::string t = noSpace(upper(raw));
    if (t.empty()) return o;
    if (isRegKeyword(t)){ o.key = t; return o; }
    if (t.front()=='(' && t.back()==')'){
        std::string in = t.substr(1, t.size()-2);
        if ((in.rfind("IX",0)==0 || in.rfind("IY",0)==0) && in.size()>2
            && (in[2]=='+' || in[2]=='-')){
            o.key = "(" + in.substr(0,2) + "+$)";
            o.expr = (in[2]=='-') ? in.substr(2) : in.substr(3);
            o.is_disp = true;
        } else {
            o.key = "($)"; o.expr = in;
        }
    } else { o.key = "$"; o.expr = t; }
    long v; o.constant_ok = parseNum(o.expr, v); if (o.constant_ok) o.constant = v;
    return o;
}

/// Operandenfeld an Kommas der obersten Ebene zerlegen.
inline std::vector<std::string> splitOperands(const std::string& ops){
    std::vector<std::string> out; int depth=0; char q=0; std::string cur;
    for (char c : ops){
        if (q){ cur.push_back(c); if (c==q) q=0; continue; }
        if (c=='\''||c=='"'){ q=c; cur.push_back(c); continue; }
        if (c=='(') ++depth;
        if (c==')') --depth;
        if (c==',' && depth==0){ out.push_back(cur); cur.clear(); continue; }
        cur.push_back(c);
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(cur);
    return out;
}

} // namespace detail

// ─── Assemblat ───────────────────────────────────────────────────────────────
/**
 * Die assemblierten Bytes (für den Versatz-Abgleich `@auto` / `verify`).
 * `wild` markiert adressabhängige Bytes (Symboladressen, relative Sprünge) —
 * beim Musterabgleich mit dem RAM werden sie übersprungen.
 */
struct Image {
    std::map<uint16_t,uint8_t> byte;
    std::set<uint16_t>         wild;
};

/// Ergebnisbericht eines Assemblerlaufs.
struct Result {
    int lines = 0;        ///< gelesene Zeilen
    int code  = 0;        ///< Zeilen mit Adresse (Code/Daten)
    int unknown = 0;      ///< nicht erkannte Befehle (Adresszähler läuft weiter)
    int anchors = 0;      ///< erkannte Mxxxx-Adressanker
    int resyncs = 0;      ///< davon: Adresszähler musste nachgeführt werden
    uint16_t first = 0xFFFF, last = 0;
    std::vector<std::string> problems;   ///< erste unbekannte Zeilen (Diagnose)
    std::string error;                   ///< gesetzt, wenn die Datei fehlt
};

namespace detail {

/// `M03F8`/`L1084`-Label → implizite Adresse (Disassemblat-Konvention).
inline bool anchorAddr(const std::string& label, uint16_t& a){
    if (label.size() != 5) return false;
    if (label[0] != 'M' && label[0] != 'L') return false;
    for (int i=1;i<5;++i) if (!isxdigit((unsigned char)label[i])) return false;
    a = (uint16_t)strtol(label.substr(1).c_str(), nullptr, 16);
    return true;
}

/// Zeichenkette/Bytefolge eines DB-Operanden anhängen.
inline void emitDbItem(const std::string& item, const SymTab& syms, long loc,
                       std::vector<uint8_t>& out, std::vector<bool>& wild){
    std::string t = trim(item);
    if (t.size() >= 2 && (t.front()=='\'' || t.front()=='"') && t.back()==t.front()){
        char q = t.front();
        for (size_t i=1;i+1<t.size();++i){
            if (t[i]==q && i+2<t.size() && t[i+1]==q) ++i;   // verdoppeltes Quote
            out.push_back((uint8_t)t[i]); wild.push_back(false);
        }
        return;
    }
    bool unres=false, sym=false;
    long v = evalExpr(t, syms, loc, &unres, &sym);
    out.push_back((uint8_t)(v & 0xFF)); wild.push_back(sym);
}

} // namespace detail

/**
 * Quelltext assemblieren und als Adresse→Quellzeile-Tabelle ablegen.
 *
 * @param path       `.MAC`/`.ASM`-Datei.
 * @param addr_offset zu jeder Quelladresse addiert (Ladeadresse bei `ORG 0`-Quellen).
 * @param out        Zieltabelle (kompatibel zu den `.prn`-Konsumenten).
 * @param res        [out] Bericht (Zeilen, Anker, unbekannte Befehle).
 * @param img        [out] optional: assemblierte Bytes + Wildcard-Maske.
 * @param use_anchors `Mxxxx`-Labels als Adressanker verwenden (Selbstkorrektur).
 * @return false, wenn die Datei nicht lesbar ist.
 */
inline bool assemble(const std::string& path, long addr_offset,
                     prnlst::Listing& out, Result& res,
                     Image* img = nullptr, bool use_anchors = true){
    std::ifstream f(path);
    if (!f){ res.error = "cannot open " + path; return false; }
    std::vector<std::string> raw;
    { std::string l; while (std::getline(f,l)) raw.push_back(l); }
    res.lines = (int)raw.size();

    SymTab syms;
    // ── Zwei Durchläufe: erst Adressen/Labels, dann Bytes (Vorwärtsbezüge). ──
    for (int pass = 0; pass < 2; ++pass){
        long loc = 0;
        bool ended = false;
        if (pass == 1){ res.code = res.unknown = res.anchors = res.resyncs = 0; res.problems.clear(); }
        for (const std::string& line : raw){
            if (ended) break;
            SrcLine sl;
            if (!splitSource(line, sl)) continue;

            // Label: EQU/SET definieren einen Wert, sonst die aktuelle Adresse.
            // Wertzuweisende Pseudobefehle. `SET` ist doppelt belegt: als MACRO-80-
            // Pseudobefehl (`NAME SET wert`) UND als Z80-Befehl `SET b,r` — letzterer
            // steht ohne Label da. Ohne diese Unterscheidung verschwände jedes
            // `SET 0,(IY+1)` spurlos aus dem Assemblat.
            bool is_equ = (sl.mnem=="EQU")
                       || (!sl.label.empty() &&
                           (sl.mnem=="SET" || sl.mnem=="DEFL" || sl.mnem=="ASET"));
            if (!sl.label.empty() && !is_equ){
                uint16_t anch;
                if (use_anchors && detail::anchorAddr(sl.label, anch)){
                    ++res.anchors;
                    if ((uint16_t)loc != anch){ ++res.resyncs; loc = anch; }
                }
                syms[sl.label] = loc;
            }
            if (sl.mnem.empty()) continue;

            // ── Direktiven ──────────────────────────────────────────────────
            if (is_equ){
                if (!sl.label.empty()) syms[sl.label] = evalExpr(sl.ops, syms, loc);
                continue;
            }
            if (sl.mnem=="ORG"){ loc = evalExpr(sl.ops, syms, loc) & 0xFFFF; continue; }
            if (sl.mnem==".PHASE"){ loc = evalExpr(sl.ops, syms, loc) & 0xFFFF; continue; }
            if (sl.mnem=="END"){ ended = true; continue; }
            if (sl.mnem=="DS" || sl.mnem=="DEFS"){ loc += evalExpr(sl.ops, syms, loc); continue; }
            // Bedingte Assemblierung: wir kennen die Bedingungssymbole des fremden
            // Builds nicht. Die Direktiven selbst erzeugen keine Bytes; die Zweige
            // werden ALLE mitassembliert — die Mxxxx-Anker fangen den Versatz ab.
            static const std::set<std::string> conds = {
                "IF","IFT","IFF","IFE","IF1","IF2","IFDEF","IFNDEF","IFB","IFNB",
                "IFIDN","IFDIF","ELSE","ENDIF","ENDC" };
            if (conds.count(sl.mnem)) continue;
            if (sl.mnem=="DB" || sl.mnem=="DEFB" || sl.mnem=="DEFM" || sl.mnem=="DC"
                || sl.mnem=="DW" || sl.mnem=="DEFW"){
                bool is_w = (sl.mnem=="DW" || sl.mnem=="DEFW");
                std::vector<uint8_t> bytes; std::vector<bool> wild;
                for (const std::string& it : detail::splitOperands(sl.ops)){
                    if (is_w){
                        bool unres=false, sym=false;
                        long v = evalExpr(it, syms, loc, &unres, &sym);
                        bytes.push_back((uint8_t)(v & 0xFF));   wild.push_back(sym);
                        bytes.push_back((uint8_t)((v>>8)&0xFF)); wild.push_back(sym);
                    } else detail::emitDbItem(it, syms, loc, bytes, wild);
                }
                // DC = wie DB, aber im letzten Byte ist Bit 7 gesetzt (MACRO-80).
                if (sl.mnem=="DC" && !bytes.empty()) bytes.back() |= 0x80;
                if (pass==1){
                    uint16_t a = (uint16_t)(loc + addr_offset);
                    out.by_addr.emplace(a, trim(line));
                    ++res.code;
                    res.first = std::min(res.first, a); res.last = std::max(res.last, a);
                    if (img) for (size_t i=0;i<bytes.size();++i){
                        uint16_t ad = (uint16_t)(a + i);
                        img->byte[ad] = bytes[i];
                        if (wild[i]) img->wild.insert(ad);
                    }
                }
                loc += (long)bytes.size();
                continue;
            }
            // Listing-/Segment-Pseudobefehle ohne Wirkung auf den Adresszähler.
            static const std::set<std::string> ignore = {
                ".Z80",".8080",".DEPHASE","ASEG","CSEG","DSEG","TITLE","SUBTTL","NAME",
                "PUBLIC","EXTRN","EXT","GLOBAL","ENTRY",".LIST",".XLIST",".RADIX",
                ".PRINTX",".COMMENT",".SALL",".LALL",".XALL",".REQUEST","PAGE",".PAGE" };
            if (ignore.count(sl.mnem) || sl.mnem[0]=='.') continue;

            // ── Befehl ──────────────────────────────────────────────────────
            std::vector<detail::OpndForm> forms;
            for (const std::string& o : detail::splitOperands(sl.ops)){
                if (trim(o).empty()) continue;
                forms.push_back(detail::classifyOperand(o));
            }
            // Schlüsselkandidaten: erst mit "$"-Platzhaltern, dann mit den
            // Konstanten im Klartext (RST 38H / BIT 3,B — Zahl steckt im Opcode).
            std::vector<std::string> keys;
            {
                std::vector<std::string> parts;
                for (auto& fo : forms) parts.push_back(fo.key);
                auto join=[&](const std::vector<std::string>& p){
                    std::string s; for(size_t i=0;i<p.size();++i){ if(i) s+=","; s+=p[i]; }
                    return detail::makeKey(sl.mnem, s); };
                keys.push_back(join(parts));
                for (size_t i=0;i<forms.size();++i){
                    if (!forms[i].constant_ok || forms[i].is_disp) continue;
                    auto p2 = parts; p2[i] = std::to_string(forms[i].constant);
                    keys.push_back(join(p2));
                }
                // ALU-Kurzform: Quelle schreibt evtl. "CP A,x" statt "CP x".
                if (parts.size()==2 && parts[0]=="A" &&
                    (sl.mnem=="SUB"||sl.mnem=="AND"||sl.mnem=="OR"||sl.mnem=="XOR"||sl.mnem=="CP"))
                    keys.push_back(detail::makeKey(sl.mnem, parts[1]));
                // "(IX)" ohne Displacement als "(IX+0)" nachreichen.
                for (size_t i=0;i<parts.size();++i)
                    if (parts[i]=="(IX)"||parts[i]=="(IY)"){
                        auto p2 = parts; p2[i] = "(" + parts[i].substr(1,2) + "+$)";
                        keys.push_back(join(p2));
                    }
            }
            const Enc* enc = nullptr; std::string used_key;
            for (const std::string& k : keys){
                auto it = detail::table().find(k);
                if (it != detail::table().end()){ enc = &it->second; used_key = k; break; }
            }
            if (!enc){
                if (pass==1){ ++res.unknown;
                    if ((int)res.problems.size() < 20) res.problems.push_back(trim(line)); }
                continue;                       // Adresszähler steht — Anker fangen das ab
            }
            if (pass==1){
                uint16_t a = (uint16_t)(loc + addr_offset);
                out.by_addr.emplace(a, trim(line));
                ++res.code;
                res.first = std::min(res.first, a); res.last = std::max(res.last, a);
                if (img){
                    uint8_t b[4]; memcpy(b, enc->bytes, 4);
                    std::vector<bool> wild(4,false);
                    long disp = 0, imm = 0; bool imm_sym=false, have_imm=false;
                    for (auto& fo : forms){
                        if (fo.expr.empty()) continue;
                        bool unres=false, sym=false;
                        long v = evalExpr(fo.expr, syms, loc, &unres, &sym);
                        if (fo.is_disp) disp = v;
                        else { imm = v; imm_sym = sym; have_imm = true; }
                    }
                    if (enc->p_disp >= 0) b[enc->p_disp] = (uint8_t)(disp & 0xFF);
                    if (enc->p_imm8 >= 0 && have_imm){
                        b[enc->p_imm8] = (uint8_t)(imm & 0xFF);
                        wild[enc->p_imm8] = imm_sym; }
                    if (enc->p_imm16 >= 0 && have_imm){
                        b[enc->p_imm16]   = (uint8_t)(imm & 0xFF);
                        b[enc->p_imm16+1] = (uint8_t)((imm>>8)&0xFF);
                        wild[enc->p_imm16] = wild[enc->p_imm16+1] = true; }  // Adresse → reloziert
                    if (enc->p_rel >= 0){
                        b[enc->p_rel] = (uint8_t)((imm - (loc + enc->len)) & 0xFF);
                        wild[enc->p_rel] = false; }   // relativ = relokationsinvariant
                    for (int i=0;i<enc->len;++i){
                        uint16_t ad = (uint16_t)(a + i);
                        img->byte[ad] = b[i];
                        if (wild[i]) img->wild.insert(ad);
                    }
                }
            }
            loc += enc->len;
        }
    }
    if (res.first == 0xFFFF) res.first = 0;
    return true;
}

// ─── Versatz-Abgleich (`@auto`) ──────────────────────────────────────────────
/// Zahl der (nicht-wildcard) Bytes im Assemblat — Bezugsgröße für `matched`.
inline int fixedByteCount(const Image& img){
    int n=0; for (auto& kv : img.byte) if (!img.wild.count(kv.first)) ++n;
    return n;
}

/// Ergebnis eines Versatz-Abgleichs Assemblat ↔ Speicher.
struct MatchResult {
    bool  found      = false;
    long  offset     = 0;     ///< zu addierender Versatz (Laufzeit = Quelle + offset)
    int   anchor_len = 0;     ///< Länge der verglichenen Ankersequenz
    int   matched    = 0;     ///< übereinstimmende Ankerbytes
    int   candidates = 0;     ///< Zahl geprüfter Versatz-Kandidaten
    uint16_t anchor_src = 0;  ///< Quelladresse der Ankersequenz
    int   fixed      = 0;     ///< Zahl der verglichenen (nicht-wildcard) Bytes
    double ratio     = 0.0;   ///< matched/fixed — 1.0 = identischer Build

    /// Klartext-Einschätzung des Treffers (für die Werkzeugausgabe).
    const char* verdict() const {
        if (!found)        return "kein Treffer";
        if (ratio >= 0.98) return "identischer Build";
        if (ratio >= 0.80) return "gleicher Build, wenige Abweichungen";
        if (ratio >= 0.25) return "gleiches Programm, ANDERER Build";
        return "schwacher Treffer — passt die Quelle zu diesem Image?";
    }
};

/**
 * Den Ladeversatz eines Assemblats im Speicher bestimmen.
 *
 * Sucht die längste zusammenhängende Folge relokationsunabhängiger Bytes
 * (@ref Image::wild ausgenommen) und scannt damit den 64-KB-Adressraum;
 * anschließend wird die Trefferquote über das ganze Assemblat gemessen.
 *
 * @param rd  Byte-Leser des Zielspeichers.
 * @param min_anchor  Mindestlänge der Ankersequenz (Voreinstellung 12 Bytes).
 */
template <typename Reader>
inline MatchResult findOffset(const Image& img, Reader rd, int min_anchor = 12){
    MatchResult r;
    if (img.byte.empty()) return r;
    // Alle zusammenhängenden wildcard-freien Folgen sammeln (längste zuerst).
    std::vector<std::pair<uint16_t,int>> runs;   // (Startadresse, Länge)
    { int cur_len=0; uint16_t cur_start=0, prev=0; bool have_prev=false;
      for (auto& kv : img.byte){
        bool usable = !img.wild.count(kv.first);
        bool cont = have_prev && kv.first == (uint16_t)(prev+1) && usable && cur_len>0;
        if (cont) ++cur_len;
        else { if (cur_len) runs.push_back({cur_start,cur_len});
               cur_start = kv.first; cur_len = usable? 1 : 0; }
        prev = kv.first; have_prev = true; }
      if (cur_len) runs.push_back({cur_start,cur_len}); }
    std::sort(runs.begin(), runs.end(),
              [](const std::pair<uint16_t,int>& a, const std::pair<uint16_t,int>& b){
                  return a.second > b.second; });

    // MEHRERE Ankerfenster über das ganze Assemblat abstimmen lassen. Ein
    // einzelnes Fenster genügt nicht: Fremd-Builds sind oft nur STELLENWEISE
    // verschoben (eingefügte/entfernte Befehle) — ein Fenster aus einem
    // verschobenen Bereich stimmt dann für einen falschen Versatz. Erst die
    // Gesamtbewertung aller Kandidaten trennt Zufallstreffer vom Ladeversatz.
    std::set<long> cands;
    const int kMaxWindows = 24;
    int windows = 0;
    for (auto& run : runs){
        if (run.second < min_anchor || windows >= kMaxWindows) break;
        // Fenster kleiner als die Folge wählen: so liegt bei einem einzelnen
        // abweichenden Byte (anderer Build) noch ein Fenster daneben.
        int win = std::max(min_anchor, std::min(24, run.second/2));
        if (win > run.second) win = run.second;
        int step = std::max(win, run.second / 4);
        for (int off = 0; off + win <= run.second && windows < kMaxWindows; off += step){
            uint16_t start = (uint16_t)(run.first + off);
            std::vector<uint8_t> pat;
            for (int i=0;i<win;++i) pat.push_back(img.byte.at((uint16_t)(start+i)));
            ++windows;
            int found_here = 0;
            for (long a=0; a + win <= 0x10000 && found_here < 64; ++a){
                bool ok = true;
                for (int i=0;i<win && ok;++i) ok = (rd((uint16_t)(a+i)) == pat[i]);
                if (ok){ cands.insert(a - (long)start); ++found_here; }
            }
            if (found_here && !r.anchor_len){ r.anchor_len = win; r.anchor_src = start; }
        }
    }
    r.candidates = (int)cands.size();
    if (cands.empty()) return r;
    // Kandidaten über das GANZE Assemblat bewerten; der beste gewinnt.
    long best_off = *cands.begin(); int best_hit = -1;
    for (long off : cands){
        int hit = 0;
        for (auto& kv : img.byte){
            if (img.wild.count(kv.first)) continue;
            if (rd((uint16_t)(kv.first + off)) == kv.second) ++hit;
        }
        if (hit > best_hit || (hit == best_hit && off == 0)){ best_hit = hit; best_off = off; }
    }
    r.found = true; r.offset = best_off; r.matched = best_hit;
    r.fixed = fixedByteCount(img);
    r.ratio = r.fixed? (double)r.matched / (double)r.fixed : 0.0;
    return r;
}

// ─── `-l`-Spezifikation ──────────────────────────────────────────────────────
/// Ist @p path eine Quelltextdatei (im Gegensatz zu einem `.prn`-Listing)?
inline bool isSourceFile(const std::string& path){
    auto ends=[&](const char* e){
        size_t n=strlen(e);
        return path.size()>=n && upper(path.substr(path.size()-n))==upper(std::string(e)); };
    return ends(".mac") || ends(".asm") || ends(".z80") || ends(".src") || ends(".s");
}

} // namespace maclst
