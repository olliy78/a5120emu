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
#include <vector>
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

/**
 * Wie @ref parseLine, liefert zusätzlich die emittierten Objektbytes der Zeile.
 * @param bytes [out] die Bytes der linken Marge (für den Versatz-Abgleich `@auto`).
 */
inline bool parseLine(const std::string& line, uint16_t& addr, std::string& src,
                      std::vector<uint8_t>* bytes);

inline bool parseLine(const std::string& line, uint16_t& addr, std::string& src){
    return parseLine(line, addr, src, nullptr);
}

inline bool parseLine(const std::string& line, uint16_t& addr, std::string& src,
                      std::vector<uint8_t>* bytes){
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

    // --- Objektbytes einsammeln (optional; für den Versatz-Abgleich `@auto`) --
    // Die linke Marge enthält NIE Tab/':' — die Byte-Tokens (exakt 2 Hexziffern)
    // enden also spätestens am Quellfeld.
    // MACRO-80 druckt 16-Bit-Operanden als EIN Wort ("C3 E860" für JP 0E860H) —
    // ein 4-stelliges Token liefert also zwei Bytes (lo, hi).
    if (bytes){
        bytes->clear();
        bytes->push_back((uint8_t)strtol(line.substr(b0,2).c_str(), nullptr, 16));
        size_t k = i;
        for (;;){
            while (k<n && line[k]==' ') ++k;
            size_t s0=k; while (k<n && isHexDigit(line[k])) ++k;
            size_t tl = k - s0;
            if (tl != 2 && tl != 4) break;
            if (k<n && line[k]=='\'') ++k;                 // relozierbar-Marker
            if (k<n && line[k]!=' ' && line[k]!='\t') break;
            long v = strtol(line.substr(s0,tl).c_str(), nullptr, 16);
            if (tl == 2) bytes->push_back((uint8_t)v);
            else { bytes->push_back((uint8_t)(v & 0xFF)); bytes->push_back((uint8_t)((v>>8)&0xFF)); }
            if (k>=n || line[k]=='\t') break;
        }
    }

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

/**
 * Eine `-l`-Spezifikation mit optionalem Quellbereich aufteilen:
 * "PFAD[@OFFSET][:VON-BIS]" — genauer: hinter dem '@' steht `OFFSET`, `OFFSET:VON-BIS`
 * oder `:VON-BIS` (Versatz 0).  VON/BIS sind LISTING-Adressen (vor dem Versatz), hex
 * mit oder ohne `0x`/`H`.  Damit lässt sich ein Teil eines Listings an eine andere
 * Laufzeitadresse legen, ohne den Rest mitzuverschieben — beim K8915-Boot-ROM läuft
 * 00D0H–03FFH als Kopie bei F0D0H, 0000H–00CFH und 0400H–09FFH aber an Ort und Stelle
 * (`k8915_zre.prn@0xF000:00D0-03FF`).  §8a AP-E4d.
 * @param lo,hi [out] Quellbereich, -1/-1 = ganzes Listing.
 * @return false bei ungültigem Versatz oder Bereich.
 */
inline bool splitSpecRange(const std::string& spec, std::string& path, long& offset,
                           int& lo, int& hi){
    lo = hi = -1; offset = 0;
    size_t at = spec.rfind('@');
    if (at == std::string::npos){ path = spec; return true; }
    path = spec.substr(0, at);
    std::string rest = spec.substr(at+1);
    size_t colon = rest.find(':');
    std::string offs = colon == std::string::npos ? rest : rest.substr(0, colon);
    if (!offs.empty()){
        bool ok; offset = parseOffset(offs, ok);
        if (!ok) return false;
    } else if (colon == std::string::npos) return false;   // "PFAD@" allein
    if (colon == std::string::npos) return true;
    std::string rng = rest.substr(colon+1);
    size_t dash = rng.find('-');
    if (dash == std::string::npos || dash == 0 || dash+1 >= rng.size()) return false;
    auto hex = [](std::string t, bool& ok)->long{
        ok = false;
        if (t.size()>2 && t[0]=='0' && (t[1]=='x'||t[1]=='X')) t = t.substr(2);
        if (!t.empty() && (t.back()=='h'||t.back()=='H')) t.pop_back();
        if (t.empty() || t.size()>4) return 0;
        for (char c : t) if (!isHexDigit(c)) return 0;
        ok = true; return strtol(t.c_str(), nullptr, 16);
    };
    bool ok1, ok2;
    long a = hex(rng.substr(0, dash), ok1), b = hex(rng.substr(dash+1), ok2);
    if (!ok1 || !ok2 || b < a) return false;
    lo = (int)a; hi = (int)b;
    return true;
}

/// Eine geladene .prn-Tabelle: Adresse → kommentierte Quellzeile.
struct Listing {
    std::map<uint16_t, std::string> by_addr;
    /// Objektbytes je (Laufzeit-)Adresse — nur gefüllt, wenn load(…, want_bytes=true).
    std::map<uint16_t, uint8_t>     bytes_by_addr;
    /// Labels, die im Listing auf einer EIGENEN Zeile stehen ("L055E:" ohne Code, wie in
    /// den selbst erzeugten K8915-Listings) — gelten für die nächste Codezeile.  Nur für
    /// @ref labelNear; by_addr/Symbolimport bleiben davon unberührt.
    std::map<uint16_t, std::string> labels_by_addr;

    /**
     * Lädt eine .prn-Datei.
     * @param path         Listing-Datei.
     * @param addr_offset  zu jeder Listing-Adresse addiert (für reloziert geladenen Code);
     *                     die Tabelle wird also unter der LAUFZEIT-Adresse abgelegt.
     * @param want_bytes   zusätzlich die Objektbytes in @ref bytes_by_addr ablegen
     *                     (Versatz-Abgleich `@auto`).
     * @return Zahl aufgenommener Code-Zeilen (-1 = Datei fehlt).
     */
    int load(const std::string& path, long addr_offset = 0, bool want_bytes = false,
             int src_lo = -1, int src_hi = -1){
        std::ifstream f(path);
        if (!f) return -1;
        std::string l; int n = 0;
        uint16_t a; std::string src;
        std::vector<uint8_t> bs;
        std::string pending_label;             // allein stehendes "NAME:" vor der Codezeile
        while (std::getline(f, l)){
            if (!parseLine(l, a, src, want_bytes? &bs : nullptr)){
                std::string t = l;
                while (!t.empty() && (t.back()=='\r' || t.back()==' ' || t.back()=='\t')) t.pop_back();
                std::string lab = labelOf(t);
                if (!lab.empty() && lab.size()+1 == t.size()) pending_label = lab;
                continue;
            }
            {
                // Quellbereich (splitSpecRange): nur Zeilen, deren LISTING-Adresse passt.
                if (src_lo >= 0 && (a < src_lo || a > src_hi)){ pending_label.clear(); continue; }
                if (want_bytes)
                    for (size_t k=0;k<bs.size();++k)
                        bytes_by_addr.emplace((uint16_t)((long)a + addr_offset + (long)k), bs[k]);
                uint16_t key = (uint16_t)((long)a + addr_offset);
                if (!pending_label.empty()){ labels_by_addr.emplace(key, pending_label); pending_label.clear(); }
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

    /**
     * Stehen an @p a gerade die Objektbytes der Listingzeile im Speicher?
     *
     * Für Maschinen, deren Speicherbild umschaltet (K8915: 0000H–0FFFH ist je nach A8H
     * das Boot-ROM ODER RAM mit dem TPA) — eine ROM-Zeile darf dann nur annotieren,
     * solange das ROM auch eingeblendet ist.  Verglichen werden die Bytes der Zeile
     * (höchstens 4, bis zur nächsten Listingzeile).  Ohne Objektbytes (geladen ohne
     * `want_bytes`) gibt es nichts zu prüfen → true.
     */
    template <class ReadByte>
    bool matches(uint16_t a, ReadByte rd) const {
        if (bytes_by_addr.empty()) return true;
        for (int k = 0; k < 4; ++k){
            const uint16_t x = (uint16_t)(a + k);
            if (k > 0 && by_addr.count(x)) break;          // nächste Zeile beginnt
            auto it = bytes_by_addr.find(x);
            if (it == bytes_by_addr.end()) break;
            if (rd(x) != it->second) return false;
        }
        return true;
    }

    /**
     * Nächstes Label an oder vor @p a (höchstens @p max_back Byte zurück) als
     * "NAME" bzw. "NAME+n" — für PC-Histogramme, deren Adressen meist mitten in einer
     * Routine liegen.  "" wenn keins in Reichweite.
     */
    std::string labelNear(uint16_t a, int max_back = 256) const {
        auto it = by_addr.upper_bound(a);
        while (it != by_addr.begin()){
            --it;
            if ((int)a - (int)it->first > max_back) break;
            std::string lab = labelOf(it->second);
            if (lab.empty()){
                auto lt = labels_by_addr.find(it->first);
                if (lt != labels_by_addr.end()) lab = lt->second;
            }
            if (!lab.empty()){
                if (it->first == a) return lab;
                return lab + "+" + std::to_string(a - it->first);
            }
        }
        return "";
    }
};

} // namespace prnlst
