/**
 * @file prn_listing.h
 * @brief Parser für MACRO-80 ".prn"-Listings (Adresse → kommentierte Quellzeile).
 *
 * Die CP/A-Quellen (BIOS, BDOS, …) liegen als gut kommentierte MACRO-80-Listings
 * vor (z.B. CPA_Workbench/build/bios.prn).  Jede emittierte Zeile trägt links die
 * absolute Lade-Adresse + die erzeugten Objektbytes und rechts die Original-
 * Quellzeile mit Label, Mnemonic und ;Kommentar:
 *
 *     D227    C3 DDF3               BIOS27: JP<TAB>read<TAB><TAB>;oA Resultat; A=0 ok
 *     D271    04             C      <TAB>DB<TAB>4<TAB>;;(2)<TAB>;BLOCK SHIFT
 *
 * Dieser Header baut daraus eine `Adresse → Quelltext`-Tabelle, mit der Debugger/
 * Trace-Werkzeuge ihre Disassembler-Ausgabe annotieren können — kein Disassemblieren
 * nötig, Mnemonic *und* Kommentar stehen schon im Listing.
 *
 * Parse-Regel (robust gegen die "db == zwei Hexziffern"-Falle):
 *   - Erstes Token = 4-stellige Hex-Adresse (optional ' für relozierbar) → Loc-Counter.
 *   - Es muss mindestens EIN emittiertes Objektbyte folgen (Token = exakt 2 Hexziffern);
 *     dadurch fallen equ/aset/set/Makro-Definitions-/Leerzeilen automatisch heraus
 *     (deren führende 4-Hex ist ein *Wert*, kein Loc-Counter, und es folgen keine Bytes).
 *   - Die linke Marge (Adresse + Objektbytes + Listing-Flag) enthält NIE Tab und NIE ':'.
 *     Das Quellfeld beginnt deshalb entweder beim Label (Bezeichner, der vor dem ersten
 *     Tab mit ':' endet) oder — wenn label-los — beim ersten Tab.
 *
 * Header-only, keine Abhängigkeiten außer der STL → direkt unit-testbar.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <map>
#include <fstream>

namespace prnlst {

inline bool isHexDigit(char c){
    return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');
}
// Zeichen, die in einem MACRO-80-Bezeichner (Label) vorkommen dürfen.
inline bool isIdentChar(char c){
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
        || c=='@'||c=='?'||c=='$'||c=='.'||c=='_';
}

/**
 * Eine einzelne Listing-Zeile parsen.
 * @param line  Rohzeile (mit evtl. \t und \r).
 * @param addr  [out] Lade-Adresse, falls die Zeile Objektcode emittiert.
 * @param src   [out] aufbereiteter Quelltext (Tabs→Space, Runs kollabiert, getrimmt).
 * @return true, wenn die Zeile eine emittierte Code-/Daten-Zeile ist.
 */
/**
 * Das führende Label einer aufbereiteten Quellzeile extrahieren (oder "").
 * Eine Zeile "BIOS27: JP read ;…" liefert "BIOS27". Labels beginnen mit einem
 * Bezeichner-Zeichen (kein Ziffernanfang) und enden mit ':'.
 */
inline std::string labelOf(const std::string& src){
    if (src.empty()) return "";
    if (!isIdentChar(src[0]) || (src[0]>='0' && src[0]<='9')) return "";
    size_t i = 0;
    while (i < src.size() && isIdentChar(src[i])) ++i;
    if (i < src.size() && src[i] == ':') return src.substr(0, i);
    return "";
}

inline bool parseLine(const std::string& line, uint16_t& addr, std::string& src){
    size_t i = 0, n = line.size();
    auto skipSp = [&]{ while(i<n && (line[i]==' '||line[i]=='\t')) ++i; };

    skipSp();
    // --- Token 1: Hex-Adresse (4 Stellen, optional trailing ') ---------------
    size_t a0 = i;
    while (i<n && isHexDigit(line[i])) ++i;
    size_t alen = i - a0;
    if (alen < 1 || alen > 4) return false;          // keine reine Hex-Adresse
    if (i<n && line[i]=='\'') ++i;                    // relozierbar-Marker
    if (i<n && line[i]!=' ' && line[i]!='\t') return false;  // Token muss enden
    uint16_t a = (uint16_t)strtol(line.substr(a0, alen).c_str(), nullptr, 16);

    // --- Es muss ein emittiertes Objektbyte folgen (Token = exakt 2 Hexziffern) ---
    skipSp();
    size_t b0 = i;
    while (i<n && isHexDigit(line[i])) ++i;
    size_t blen = i - b0;
    bool byteFollows = (blen==2) && (i>=n || line[i]==' ' || line[i]=='\t');
    if (!byteFollows) return false;                  // equ/aset/Makro-Def/leer → raus

    // --- Quellfeld-Anfang finden: erstes ':' (Label) ODER erster Tab ----------
    size_t tabPos = line.find('\t');
    size_t srcStart = std::string::npos;
    // Doppelpunkt vor dem ersten Tab ⇒ es gibt ein Label auf dieser Zeile.
    size_t colon = line.find(':');
    if (colon != std::string::npos &&
        (tabPos == std::string::npos || colon < tabPos)) {
        // Label-Anfang = zurücklaufen über den Bezeichner vor dem ':'.
        size_t s = colon;
        while (s > 0 && isIdentChar(line[s-1])) --s;
        srcStart = s;
    } else if (tabPos != std::string::npos) {
        srcStart = tabPos;                           // label-los: Mnemonic nach Tab
    } else {
        return false;                                // kein erkennbares Quellfeld
    }

    // --- Quelltext aufbereiten: Tabs/CR→Space, Runs kollabieren, trimmen ------
    std::string out;
    out.reserve(line.size() - srcStart);
    bool prevSpace = false;
    for (size_t k = srcStart; k < n; ++k){
        char c = line[k];
        if (c=='\t' || c=='\r' || c==' '){
            if (!prevSpace && !out.empty()) out.push_back(' ');
            prevSpace = true;
        } else {
            out.push_back(c);
            prevSpace = false;
        }
    }
    while (!out.empty() && out.back()==' ') out.pop_back();
    if (out.empty()) return false;

    addr = a;
    src  = out;
    return true;
}

/**
 * Offset-Argument parsen ("@OFFSET"-Spezifikation eines `-l`-Listings).
 * Akzeptiert vorzeichenbehaftet: `0x1F00`, `-0x100`, `1800h`, `512`, `-256`.
 * @param str  Offset-Text (ohne führendes '@').
 * @param ok   [out] true bei gültiger Zahl.
 * @return     der Offset (0 bei Fehler).
 */
inline long parseOffset(const std::string& str, bool& ok){
    ok = false;
    if (str.empty()) return 0;
    std::string s = str;
    int base = 0;                                    // 0 → 0x = hex, sonst dezimal
    if (!s.empty() && (s.back()=='h' || s.back()=='H')){ s.pop_back(); base = 16; }
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, base);
    if (end == s.c_str() || (end && *end != '\0')) return 0;
    ok = true;
    return v;
}

/**
 * Eine `-l`-Spezifikation "PFAD[@OFFSET]" in Pfad + Offset aufteilen.
 * @return false, wenn ein @OFFSET angegeben, aber ungültig ist.
 */
inline bool splitSpec(const std::string& spec, std::string& path, long& offset){
    offset = 0;
    size_t at = spec.rfind('@');
    if (at == std::string::npos){ path = spec; return true; }
    path = spec.substr(0, at);
    bool ok; offset = parseOffset(spec.substr(at+1), ok);
    return ok;
}

/// Eine geladene .prn-Tabelle: Adresse → kommentierte Quellzeile.
struct Listing {
    std::map<uint16_t, std::string> by_addr;

    /**
     * Lädt eine .prn-Datei.
     * @param path         Listing-Datei.
     * @param addr_offset  zu jeder Listing-Adresse addiert (für reloziert geladenen Code);
     *                     die Tabelle wird also unter der LAUFZEIT-Adresse abgelegt.
     * @return Zahl aufgenommener Code-Zeilen (-1 = Datei fehlt).
     */
    int load(const std::string& path, long addr_offset = 0){
        std::ifstream f(path);
        if (!f) return -1;
        std::string l; int n = 0;
        uint16_t a; std::string src;
        while (std::getline(f, l)){
            if (parseLine(l, a, src)){
                uint16_t key = (uint16_t)((long)a + addr_offset);
                // Erste Quelle pro Adresse gewinnt (Conditionals/Makro-Reexpansion).
                if (by_addr.find(key) == by_addr.end()){ by_addr[key] = src; ++n; }
            }
        }
        return n;
    }

    /// Quelltext zu einer (Laufzeit-)Adresse oder nullptr.
    const std::string* find(uint16_t a) const {
        auto it = by_addr.find(a);
        return it == by_addr.end() ? nullptr : &it->second;
    }
};

} // namespace prnlst
