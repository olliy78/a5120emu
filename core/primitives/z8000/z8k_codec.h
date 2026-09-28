/**
 * @file z8k_codec.h
 * @brief Dekodieren und Kodieren einzelner U8001/U8002-Befehle anhand von z8k_table.h.
 *
 * decode() holt die Befehlsworte über einen Rückruf `fetch(i)` (i = 0, 1, 2 …,
 * immer in dieser Reihenfolge und nur so viele wie nötig) — so kann der
 * CPU-Kern (S3) jeden Abruf als eigenen Buszyklus fahren.  Das Ergebnis enthält
 * die Tabellenzeile, alle gelesenen Worte und die Operandenwerte.
 * encode() ist die Umkehrung und wird vom Assembler und von den Rundlauf-Tests
 * benutzt.
 *
 * Adressen im segmentierten Modus: segment (7 Bit) + offset (16 Bit).  Die
 * Zilog-Form eines segmentierten Zeigers im Registerpaar bzw. als 32-Bit-Wert
 * ist  0sss ssss 0000 0000 | offset  →  segAddr(seg, off) = seg << 24 | off.
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8000/z8k_table.h"

#include <cstdint>
#include <string>
#include <vector>

namespace z8k {

/// 32-Bit-Darstellung einer segmentierten Adresse (wie im Registerpaar).
inline uint32_t segAddr(unsigned seg, unsigned off) { return (uint32_t(seg & 0x7F) << 24) | (off & 0xFFFF); }

/// Ein dekodierter bzw. zu kodierender Operand.  Welche Felder gelten, hängt an `kind`.
struct Operand {
    Kind     kind = Kind::None;
    uint8_t  reg = 0;         ///< Register (RB..RQ, RP, IR, IO), Index (X), Basis (BA, BX)
    uint8_t  reg2 = 0;        ///< BX: Indexregister
    uint8_t  seg = 0;         ///< DA/X segmentiert: Segmentnummer
    bool     shortSeg = false;///< DA/X segmentiert: kurze Form (1 Wort, Offset 0..255)
    uint32_t value = 0;       ///< DA/X: Offset; IM*: Wert; PORT; BA: disp; CC/FL/INT/CTL/BIT/N16/LDMN/RAW/LIT: Wert
    int32_t  disp = 0;        ///< RA*: Byteabstand zum Folgebefehl (Ziel = PC_danach + disp);
                              ///< SHL/SHR: Schiebeweite mit Vorzeichen (rechts negativ)
};

/// Ergebnis von decode().
struct Decoded {
    const Insn* insn = nullptr;   ///< nullptr = keine Tabellenzeile passt
    bool     seg = false;         ///< dekodiert für den segmentierten Modus
    int      nwords = 0;          ///< gelesene Befehlsworte (= Länge / 2)
    uint16_t w[6] = {};
    int      nops = 0;
    Operand  op[4];

    int bytes() const { return nwords * 2; }

    /// Wird eine DA/X-Adresse in kurzer Form benutzt?
    bool shortSegAddr() const {
        for (int i = 0; i < nops; ++i)
            if ((op[i].kind == Kind::DA || op[i].kind == Kind::X) && op[i].shortSeg) return true;
        return false;
    }

    /**
     * Grundtakte laut Tabelle.  `condTrue=false` wählt die Takte „nicht
     * genommen" (JP/RET …).  Zusatztakte `perN·n` kommen vom Aufrufer, der n kennt.
     */
    int cycles(bool condTrue = true) const {
        if (!insn) return 0;
        int col = !seg ? 0 : (shortSegAddr() ? 1 : 2);
        if (!condTrue && insn->alt[col]) return insn->alt[col];
        return insn->cyc[col];
    }
};

namespace detail {
inline int32_t sext(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return int32_t((v ^ m) - m);
}
} // namespace detail

/**
 * Dekodiert einen Befehl.  `fetch(i)` liefert das i-te Befehlswort (ab der
 * Befehlsadresse).  Bei unbekannter Kodierung: false, insn = nullptr, nwords =
 * Zahl der bereits gelesenen Worte (1, bzw. 2 wenn das zweite Wort mitentschied).
 */
template <class Fetch>
bool decode(Fetch&& fetch, bool seg, Decoded& d) {
    d = Decoded{};
    d.seg = seg;
    const Table& t = Table::get();
    uint16_t w0 = uint16_t(fetch(0));
    d.w[0] = w0; d.nwords = 1;
    int n = 0;
    const uint16_t* cand = t.candidates(w0, n);
    if (n == 0) return false;

    const Insn* in = nullptr;
    bool needW1 = false;
    for (int i = 0; i < n; ++i) needW1 |= t.at(cand[i]).hasW1;
    uint16_t w1 = 0;
    if (needW1) { w1 = uint16_t(fetch(1)); d.w[1] = w1; d.nwords = 2; }
    for (int i = 0; i < n && !in; ++i) {
        const Insn& c = t.at(cand[i]);
        if (c.hasW1) {
            if ((w1 & c.mask1) == c.match1 && fieldsOk(c, w0, w1, true)) in = &c;
        } else if (!needW1) {
            in = &c;
        } else {
            // Zeile ohne festes zweites Wort neben einer mit: das gelesene w1 ist
            // dann schon das erste Erweiterungswort.  Kommt in der Tabelle nicht
            // vor (Test TableIsUnambiguous), bleibt aber korrekt behandelt.
            in = &c;
        }
    }
    if (!in) return false;
    d.insn = in;
    int next = in->hasW1 ? 2 : 1;          // nächstes zu lesendes Wort
    int prefetched = d.nwords;             // schon gelesen (w1 evtl. ohne hasW1)
    auto word = [&]() -> uint16_t {
        uint16_t v;
        if (next < prefetched) v = d.w[next];
        else { v = uint16_t(fetch(next)); d.w[next] = v; }
        ++next;
        if (next > d.nwords) d.nwords = next;
        return v;
    };

    d.nops = in->nops;
    for (int i = 0; i < in->nops; ++i) {
        const OpSpec& s = in->op[i];
        Operand& o = d.op[i];
        o.kind = s.kind;
        uint16_t fv = s.f.valid() ? s.f.get(s.f.word == 0 ? w0 : w1) : 0;
        switch (s.kind) {
        case Kind::RB: case Kind::RW: case Kind::RL: case Kind::RQ: case Kind::RP:
        case Kind::IR: case Kind::IO:
            o.reg = uint8_t(fv); break;
        case Kind::DA: case Kind::X: {
            if (s.kind == Kind::X) o.reg = uint8_t(fv);
            uint16_t a = word();
            if (!seg) { o.value = a; }
            else if (a & 0x8000) { o.seg = uint8_t((a >> 8) & 0x7F); o.value = word(); }
            else { o.seg = uint8_t((a >> 8) & 0x7F); o.value = a & 0xFF; o.shortSeg = true; }
            break;
        }
        case Kind::BA: o.reg = uint8_t(fv); o.value = word(); break;
        case Kind::BX: o.reg = uint8_t(fv); o.reg2 = uint8_t(s.g.get(s.g.word == 0 ? w0 : w1)); break;
        case Kind::RA16: o.disp = int16_t(word()); break;
        case Kind::RA8:  o.disp = detail::sext(fv, 8) * 2; break;
        case Kind::RA7:  o.disp = -int32_t(fv) * 2; break;
        case Kind::RA12: o.disp = -detail::sext(fv, 12) * 2; break;
        case Kind::IMB:  o.value = word() & 0xFF; break;
        case Kind::IMW:  o.value = word(); break;
        case Kind::IML:  { uint32_t hi = word(); o.value = (hi << 16) | word(); break; }
        case Kind::IM8: case Kind::IM4: case Kind::BIT: case Kind::CC: case Kind::FL:
        case Kind::INT: case Kind::CTL: case Kind::RAW:
            o.value = fv; break;
        case Kind::N16: case Kind::LDMN: o.value = uint32_t(fv) + 1; break;
        case Kind::SHL: o.disp = fv; o.value = fv; break;
        case Kind::SHR: o.disp = int32_t(fv) - int32_t(1u << s.f.width); o.value = uint32_t(-o.disp); break;
        case Kind::PORT: o.value = word(); break;
        case Kind::LIT:  o.value = s.lit; break;
        case Kind::FLAGS: case Kind::None: break;
        }
    }
    return true;
}

/// Bequemlichkeit: dekodieren aus einem Wortfeld.
inline bool decodeWords(const uint16_t* words, int count, bool seg, Decoded& d) {
    return decode([&](int i) -> uint16_t { return i < count ? words[i] : uint16_t(0); }, seg, d);
}

/**
 * Kodiert eine Tabellenzeile mit Operandenwerten (in Syntax-Reihenfolge).
 * Prüft nur, was die Kodierung verlangt (Feldbreiten, ≠0-Felder, Reichweiten);
 * die Syntaxprüfung (gerade Registerpaare usw.) macht der Assembler.
 */
inline bool encode(const Insn& in, const Operand* ops, int nops, bool seg,
                   std::vector<uint16_t>& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (nops != in.nops) return fail("falsche Operandenzahl");
    uint16_t w[2] = {in.match0, in.match1};
    std::vector<uint16_t> ext;
    auto put = [&](const Field& f, int64_t v, const char* what) -> bool {
        if (v < 0 || v >= (int64_t(1) << f.width))
            return fail(std::string(what) + " ausserhalb des Feldes");
        w[f.word] = uint16_t(w[f.word] | (uint16_t(v) << f.lsb));
        return true;
    };
    auto putAddr = [&](const Operand& o) -> bool {
        if (!seg) { if (o.value > 0xFFFF) return fail("Adresse > 16 Bit"); ext.push_back(uint16_t(o.value)); return true; }
        if (o.seg > 0x7F) return fail("Segment > 127");
        if (o.shortSeg) {
            if (o.value > 0xFF) return fail("kurze Segmentadresse: Offset > %FF");
            ext.push_back(uint16_t((o.seg << 8) | o.value));
        } else {
            if (o.value > 0xFFFF) return fail("Offset > 16 Bit");
            ext.push_back(uint16_t(0x8000 | (o.seg << 8)));
            ext.push_back(uint16_t(o.value));
        }
        return true;
    };
    for (int i = 0; i < nops; ++i) {
        const OpSpec& s = in.op[i];
        const Operand& o = ops[i];
        switch (s.kind) {
        case Kind::RB: case Kind::RW: case Kind::RL: case Kind::RQ: case Kind::RP: case Kind::IO:
            if (!put(s.f, o.reg, "Register")) return false; break;
        case Kind::IR:
            if (o.reg == 0) return fail("R0 als Zeiger nicht kodierbar");
            if (!put(s.f, o.reg, "Register")) return false; break;
        case Kind::DA:
            if (!putAddr(o)) return false; break;
        case Kind::X:
            if (o.reg == 0) return fail("R0 als Index nicht kodierbar");
            if (!put(s.f, o.reg, "Index")) return false;
            if (!putAddr(o)) return false; break;
        case Kind::BA:
            if (o.reg == 0) return fail("R0 als Basis nicht kodierbar");
            if (!put(s.f, o.reg, "Basis")) return false;
            if (o.value > 0xFFFF) return fail("Verschiebung > 16 Bit");
            ext.push_back(uint16_t(o.value)); break;
        case Kind::BX:
            if (o.reg == 0) return fail("R0 als Basis nicht kodierbar");
            if (!put(s.f, o.reg, "Basis") || !put(s.g, o.reg2, "Index")) return false; break;
        case Kind::RA16:
            if (o.disp < -32768 || o.disp > 32767) return fail("relatives Ziel ausser Reichweite (±32 KB)");
            ext.push_back(uint16_t(o.disp)); break;
        case Kind::RA8:
            if ((o.disp & 1) || o.disp < -256 || o.disp > 254) return fail("JR-Ziel ausser Reichweite (-256..+254)");
            if (!put(s.f, (o.disp / 2) & 0xFF, "Abstand")) return false; break;
        case Kind::RA7:
            if ((o.disp & 1) || o.disp > 0 || o.disp < -254) return fail("DJNZ-Ziel ausser Reichweite (-254..0)");
            if (!put(s.f, -o.disp / 2, "Abstand")) return false; break;
        case Kind::RA12:
            if ((o.disp & 1) || o.disp < -4094 || o.disp > 4096) return fail("CALR-Ziel ausser Reichweite (-4094..+4096)");
            if (!put(s.f, (-o.disp / 2) & 0xFFF, "Abstand")) return false; break;
        case Kind::IMB:
            if (o.value > 0xFF) return fail("Byte-Direktwert > %FF");
            ext.push_back(uint16_t((o.value << 8) | o.value)); break;
        case Kind::IMW:
            if (o.value > 0xFFFF) return fail("Wort-Direktwert > %FFFF");
            ext.push_back(uint16_t(o.value)); break;
        case Kind::IML:
            ext.push_back(uint16_t(o.value >> 16)); ext.push_back(uint16_t(o.value)); break;
        case Kind::IM8: case Kind::IM4: case Kind::BIT: case Kind::CC: case Kind::FL:
        case Kind::INT: case Kind::RAW:
            if (!put(s.f, o.value, "Wert")) return false; break;
        case Kind::CTL:
            if (o.value < 2) return fail("Steuerregister unbekannt");
            if (!put(s.f, o.value, "Steuerregister")) return false; break;
        case Kind::N16: case Kind::LDMN:
            if (o.value < 1 || o.value > 16) return fail("Anzahl ausserhalb 1..16");
            if (!put(s.f, o.value - 1, "Anzahl")) return false; break;
        case Kind::SHL:
            if (o.disp < 0) return fail("negative Schiebeweite");
            if (!put(s.f, o.disp, "Schiebeweite")) return false; break;
        case Kind::SHR: {
            if (o.disp >= 0) return fail("Rechtsschieben um 0 ist nicht kodierbar (= Linksschieben um 0)");
            int64_t f = (int64_t(1) << s.f.width) + o.disp;
            if (f < 0) return fail("Schiebeweite zu gross");
            if (!put(s.f, f, "Schiebeweite")) return false; break;
        }
        case Kind::PORT:
            if (o.value > 0xFFFF) return fail("Portadresse > %FFFF");
            ext.push_back(uint16_t(o.value)); break;
        case Kind::LIT:
            if (o.value != s.lit) return fail("feste Anzahl passt nicht");
            break;
        case Kind::FLAGS: case Kind::None: break;
        }
    }
    out.clear();
    out.push_back(w[0]);
    if (in.hasW1) out.push_back(w[1]);
    out.insert(out.end(), ext.begin(), ext.end());
    return true;
}

} // namespace z8k
