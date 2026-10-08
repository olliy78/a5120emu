/**
 * @file dbg_z8.h
 * @brief Z8-Kontext für k1520dbg (`cpu z8`, AP P19c): Register, Registerdatei, Zähler, UART,
 *        Interrupts, Ports, letzter Buszyklus; Schritt/Lauf/Haltepunkte, Disassembler und
 *        Assembler (tools/z8/).
 *
 * Maschinenfrei: ein `Kontext` arbeitet auf einem `Z8&` und einem `Speicher` (wie der Debugger
 * Programm- und Datenspeicher ohne Buswirkung sieht).  Heute benutzt von `k1520dbg --z8 <bild>`
 * (eigenständiger Prüfstand: Abzug am Programmbus, 64 KB externer Speicher je Raum); P20 hängt
 * den Terminal-Z8 über denselben Kontext ein.  Unit-getestet in tests/debugtools/test_dbg_z8.cpp.
 *
 * Kommandos (help z8):
 *   r                      Register, Flags, Arbeitsregister
 *   s [n] | n | g [adr] | fin    Schritt, Schritt über CALL, Lauf (bis Haltepunkt/adr), bis RET
 *   b adr | bl | bd n|all  Haltepunkte
 *   u [adr [n]]            disassemblieren        a adr befehl   assemblieren (Programmspeicher)
 *   d [adr [n]] | de [adr [n]] | dr [von [bis]]   Programm-, Daten-, Registerdatei-Auszug
 *   e adr b… | ee adr b… | er reg v | set pc|sp|rp|flags|imr|irq|ipr v   ändern
 *   t | uart | irq | ports | bus | sicht           Peripherie
 *   pin p bit 0|1 | port p v   Eingangspegel      irqlog [n]   angenommene Interrupts
 *   reset | takte | q
 *
 * @license MIT
 */
#pragma once
#include "core/primitives/z8.h"
#include "core/primitives/z8/z8_table.h"
#include "tools/z8/z8_asm.h"
#include "tools/z8/z8_disasm.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core/util/os_compat.h"   // isatty (auch unter Windows)

namespace dbgz8 {

struct Speicher {
    std::function<uint8_t(uint16_t)> prog;                 ///< Programmspeicher (Sicht)
    std::function<void(uint16_t, uint8_t)> progSchreiben;  ///< z. B. RAM im Prüfstand
    std::function<uint8_t(uint16_t)> daten;                ///< externer Datenspeicher
    std::function<void(uint16_t, uint8_t)> datenSchreiben;
};

inline std::string fmt(const char* f, ...) {
    char b[512];
    va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a);
    return b;
}

/// Zahl: 12H, 0x12, %12, $12 hex; sonst dezimal.  Registernamen (P0…SPL) erlaubt.
inline bool zahl(const std::string& t0, long& v) {
    if (t0.empty()) return false;
    std::string t = t0;
    if (t[0] == '$') t = "%" + t.substr(1);
    bool unb = false;
    static const std::map<std::string, long> leer;
    z8asm::detail::Kontext k{&leer, 0, true};
    return z8asm::detail::wert(t, k, v, unb);
}

inline std::string flagsText(uint8_t f) {
    std::string s;
    const char* n = "CZSVDH";
    for (int i = 0; i < 6; ++i) s += (f & (0x80 >> i)) ? n[i] : '.';
    s += fmt(" F2=%d F1=%d", (f >> 1) & 1, f & 1);
    return s;
}

inline std::string regText(const Z8& c) {
    std::string o = fmt("PC=%04X  SP=%04X%s  RP=%02X  FLAGS=%02X [%s]\n", c.pc, c.sp,
                        c.stapelIntern() ? " (intern: SPL)" : " (extern)", c.rp, c.flags, flagsText(c.flags).c_str());
    o += fmt("IMR=%02X  IRQ=%02X%s  IPR=%02X  P01M=%02X  P2M=%02X  P3M=%02X  TMR=%02X\n",
             c.imr, c.regSicht(0xFA), c.irqFreigegeben() ? "" : " (gesperrt bis EI)", c.regSicht(0xF9),
             c.p01m(), c.p2m(), c.p3m(), c.tmr());
    for (int n = 0; n < 16; ++n) {
        o += fmt("r%-2d=%02X%s", n, c.regSicht(c.arbeitsreg(unsigned(n))), n == 7 ? "\n" : n == 15 ? "\n" : "  ");
    }
    return o;
}

inline std::string zaehlerText(const Z8& c) {
    std::string o;
    const char* tin[4] = {"extern (TIN-Takt)", "Tor (TIN=H)", "Trigger", "Retrigger"};
    const char* tout[4] = {"aus", "T0", "T1", "interner Takt"};
    for (int n = 0; n < 2; ++n) {
        const Z8Zaehler& z = c.zaehler(n);
        const unsigned p = (z.pre >> 2) ? (z.pre >> 2) : 64;
        const unsigned t = z.anfang ? z.anfang : 256;
        const bool en = c.tmr() & (n ? 0x08 : 0x02);
        std::string quelle = "intern (Takt/4)";
        if (n == 1 && !(z.pre & 0x02)) quelle = tin[(c.tmr() >> 4) & 3];
        o += fmt("T%d: %s  %s  PRE%d=%02X (÷%u)  T%d=%02X (%u)  Stand=%02X  Vorteiler=%u  Endwerte=%llu\n"
                 "    Takt: %s%s  Periode = 4·%u·%u = %u interne Takte\n",
                 n, en ? "zählt" : "steht", (z.pre & 1) ? "Dauer" : "Einzel", n, z.pre, p, n, z.anfang, t,
                 unsigned(z.zaehler & 0xFF), z.vorteiler, (unsigned long long)z.abgelaufen,
                 quelle.c_str(), (n == 1 && z.getriggert) ? " (getriggert)" : "", p, t, 4 * p * t);
    }
    o += fmt("TOUT (P36): %s, Pegel %d\n", tout[(c.tmr() >> 6) & 3], c.tout() ? 1 : 0);
    return o;
}

inline std::string uartText(const Z8& c) {
    const Z8Sicht s = c.sicht();
    const bool an = s.p3m & 0x40;
    const unsigned p = (s.t[0].pre >> 2) ? (s.t[0].pre >> 2) : 64, t = s.t[0].anfang ? s.t[0].anfang : 256;
    std::string o = fmt("UART: %s, Parität %s, Bitzeit = 4·%u·%u·16 = %u interne Takte\n",
                        an ? "ein (P30=SIN, P37=SOUT)" : "aus", (s.p3m & 0x80) ? "ungerade" : "aus", p, t, 64 * p * t);
    o += fmt("  Empfang: Puffer=%02X  %s (Schieber %03X, Teiler %u)\n", s.rxPuffer,
             s.rxLaeuft ? "Zeichen läuft ein" : "wartet auf Startbit", s.rxSchieber, s.rxTeiler);
    o += fmt("  Senden:  %s (Schieber %03X, Teiler %u), P37=%d\n", s.txSchieber ? "läuft" : "ruht",
             s.txSchieber, s.txTeiler, (s.portPins[3] >> 7) & 1);
    return o;
}

inline std::vector<int> rangfolge(uint8_t ipr) {
    int A[2] = {5, 3}, B[2] = {2, 0}, C[2] = {1, 4};
    if (ipr & 0x20) std::swap(A[0], A[1]);
    if (ipr & 0x04) std::swap(B[0], B[1]);
    if (ipr & 0x02) std::swap(C[0], C[1]);
    const int* g[3];
    switch (((ipr >> 2) & 0x06) | (ipr & 0x01)) {
    case 2: g[0] = A; g[1] = B; g[2] = C; break;
    case 3: g[0] = A; g[1] = C; g[2] = B; break;
    case 4: g[0] = B; g[1] = C; g[2] = A; break;
    case 5: g[0] = C; g[1] = B; g[2] = A; break;
    case 6: case 7: g[0] = B; g[1] = A; g[2] = C; break;
    default: g[0] = C; g[1] = A; g[2] = B; break;
    }
    std::vector<int> r;
    for (auto* x : g) { r.push_back(x[0]); r.push_back(x[1]); }
    return r;
}

inline std::string irqText(const Z8& c) {
    const Z8Sicht s = c.sicht();
    static const char* quelle[6] = {"P32", "P33", "P31/TIN", "P30/SIN", "T0/SOUT", "T1"};
    std::string o = fmt("Interrupts %s, IRQ-Register %s\n", (s.imr & 0x80) ? "frei (IMR.7)" : "gesperrt (IMR.7=0)",
                        s.irqFreigegeben ? "aktiv" : "gesperrt bis zum ersten EI");
    o += "  Rangfolge (IPR=" + fmt("%02X", c.regSicht(0xF9)) + "):";
    for (int n : rangfolge(c.regSicht(0xF9))) o += fmt(" IRQ%d", n);
    o += "\n";
    for (int n = 0; n < 6; ++n)
        o += fmt("  IRQ%d %-8s angefordert=%d maske=%d  angenommen=%llu\n", n, quelle[n], (s.irq >> n) & 1,
                 (s.imr >> n) & 1, (unsigned long long)s.interrupts[n]);
    return o;
}

inline std::string portsText(const Z8& c) {
    std::string o;
    static const char* p0[4] = {"Ausgang", "Eingang", "A8–A11", "A8–A11"};
    static const char* p0h[4] = {"Ausgang", "Eingang", "A12–A15", "A12–A15"};
    static const char* p1[4] = {"Ausgang", "Eingang", "AD0–AD7", "hochohmig"};
    const uint8_t m = c.p01m();
    o += fmt("P0: Pins=%02X aus=%02X treibt=%02X  P00–03 %s, P04–07 %s\n", c.portPegel(0), c.sicht().portAus[0],
             c.portTreibt(0), p0[m & 3], p0h[m >> 6]);
    o += fmt("P1: Pins=%02X aus=%02X treibt=%02X  %s;  Stapel %s, Timing %s\n", c.portPegel(1), c.sicht().portAus[1],
             c.portTreibt(1), p1[(m >> 3) & 3], (m & 4) ? "intern" : "extern", (m & 0x20) ? "erweitert" : "normal");
    o += fmt("P2: Pins=%02X aus=%02X treibt=%02X  P2M=%02X (1=Eingang), %s\n", c.portPegel(2), c.sicht().portAus[2],
             c.portTreibt(2), c.p2m(), (c.p3m() & 1) ? "Gegentakt" : "offener Drain");
    static const char* m34[4] = {"P34 Ausgang", "P34 = /DM", "P34 = /DM", "P33/P34 = Handshake Port 1"};
    o += fmt("P3: Pins=%02X (Eingänge P30–P33=%X)  P3M=%02X: %s%s%s%s\n", c.portPegel(3), c.portPegel(3) & 15, c.p3m(),
             m34[(c.p3m() >> 3) & 3], (c.p3m() & 0x04) ? ", Handshake Port 0" : "",
             (c.p3m() & 0x20) ? ", Handshake Port 2" : "", (c.p3m() & 0x40) ? ", UART" : "");
    return o;
}

inline std::string busText(const Z8& c) {
    const Z8BusZyklus& z = c.letzterZyklus();
    return fmt("letzter Buszyklus: %s %s %04X = %02X  /DM=%d%s%s  (Takt %llu)\n", z8ZugriffName(z.art),
               z.lesen ? "lesen" : "schreiben", z.adresse, c.letztesDatum(), z.dm ? 0 : 1,
               z.hochohmig ? "  [hochohmig: kein Zyklus]" : "", z.erweitert ? "  [erweitert]" : "",
               (unsigned long long)z.takt);
}

class Kontext {
public:
    Kontext(Z8& cpu, Speicher sp) : c_(cpu), m_(std::move(sp)) {
        auto alt = c_.onInterrupt;
        c_.onInterrupt = [this, alt](int n, uint16_t a, uint16_t z) {
            if (alt) alt(n, a, z);
            irqLog_.push_back({n, a, z, c_.takte});
            if (irqLog_.size() > 256) irqLog_.pop_front();
        };
    }

    std::set<uint16_t> haltepunkte;
    uint64_t maxSchritte = 20000000;

    /// Eine Kommandozeile.  Rückgabe false = `q`.
    bool befehl(const std::string& zeile, std::string& out) {
        std::vector<std::string> t;
        { std::istringstream is(zeile); std::string w; while (is >> w) t.push_back(w); }
        if (t.empty()) return true;
        const std::string k = t[0];
        auto arg = [&](size_t i, long def) { long v; return (i < t.size() && zahl(t[i], v)) ? v : def; };
        if (k == "q" || k == "quit") return false;
        if (k == "r" || k == "regs") { out += regText(c_); out += zeileBei(c_.pc) + "\n"; return true; }
        if (k == "s" || k == "step") {
            const long n = arg(1, 1);
            for (long i = 0; i < n; ++i) { out += zeileBei(c_.pc) + "\n"; c_.step(); }
            out += "-> " + zeileBei(c_.pc) + "\n";
            return true;
        }
        if (k == "n" || k == "next") {
            const z8dis::Ergebnis e = z8dis::disasm(m_.prog, c_.pc);
            if (e.ruft) laufBis(uint16_t(c_.pc + e.len), out);
            else { out += zeileBei(c_.pc) + "\n"; c_.step(); }
            out += "-> " + zeileBei(c_.pc) + "\n";
            return true;
        }
        if (k == "fin") {   // bis der Stapel über den jetzigen Stand steigt (RET/IRET)
            const uint16_t sp0 = c_.sp;
            uint64_t i = 0;
            bool weg = false;
            while (i++ < maxSchritte) {
                const uint8_t op = m_.prog(c_.pc);
                c_.step();
                if ((op == 0xAF || op == 0xBF) && spGroesser(c_.sp, sp0)) { weg = true; break; }
            }
            out += weg ? "-> " + zeileBei(c_.pc) + "\n" : "  (kein RET innerhalb der Schrittgrenze)\n";
            return true;
        }
        if (k == "g" || k == "c" || k == "go") {
            if (t.size() > 1) laufBis(uint16_t(arg(1, 0)), out); else laufBis(-1, out);
            out += "-> " + zeileBei(c_.pc) + "\n";
            return true;
        }
        if (k == "b") {
            if (t.size() < 2) { out += "  b adr\n"; return true; }
            haltepunkte.insert(uint16_t(arg(1, 0)));
            out += fmt("  Haltepunkt %04X\n", unsigned(arg(1, 0)) & 0xFFFF);
            return true;
        }
        if (k == "bl") { for (uint16_t a : haltepunkte) out += fmt("  %04X\n", a); if (haltepunkte.empty()) out += "  (keine)\n"; return true; }
        if (k == "bd") {
            if (t.size() > 1 && t[1] == "all") haltepunkte.clear(); else haltepunkte.erase(uint16_t(arg(1, 0)));
            return true;
        }
        if (k == "u") {
            uint16_t a = uint16_t(arg(1, c_.pc));
            const long n = arg(2, 12);
            for (long i = 0; i < n; ++i) { int len; out += z8dis::zeile(m_.prog, a, &len) + "\n"; a = uint16_t(a + len); }
            return true;
        }
        if (k == "a") {
            if (t.size() < 3) { out += "  a adr befehl\n"; return true; }
            const size_t p = zeile.find(t[1]) + t[1].size();
            std::vector<uint8_t> b; std::string err;
            const uint16_t adr = uint16_t(arg(1, 0));
            if (!z8asm::befehl(zeile.substr(p), adr, b, err)) { out += "  Fehler: " + err + "\n"; return true; }
            if (!m_.progSchreiben) { out += "  Programmspeicher nicht schreibbar\n"; return true; }
            for (size_t i = 0; i < b.size(); ++i) m_.progSchreiben(uint16_t(adr + i), b[i]);
            out += z8dis::zeile(m_.prog, adr) + "\n";
            return true;
        }
        if (k == "d" || k == "de") {
            auto& f = k == "d" ? m_.prog : m_.daten;
            if (!f) { out += "  kein Speicher\n"; return true; }
            uint16_t a = uint16_t(arg(1, k == "d" ? c_.pc : 0));
            const long n = arg(2, 64);
            for (long i = 0; i < n; i += 16) {
                out += fmt("%04X ", unsigned(a));
                std::string asc;
                for (int j = 0; j < 16 && i + j < n; ++j) {
                    const uint8_t v = f(uint16_t(a + j));
                    out += fmt(" %02X", v);
                    asc += (v >= 32 && v < 127) ? char(v) : '.';
                }
                out += "  " + asc + "\n";
                a = uint16_t(a + 16);
            }
            return true;
        }
        if (k == "dr") {
            const long von = arg(1, 0), bis = arg(2, 0xFF);
            for (long a = von & 0xF0; a <= bis; a += 16) {
                out += fmt("%02X:", unsigned(a));
                for (int j = 0; j < 16; ++j) out += fmt(" %02X", c_.regSicht(uint8_t(a + j)));
                out += "\n";
            }
            return true;
        }
        if (k == "e" || k == "ee") {
            auto& f = k == "e" ? m_.progSchreiben : m_.datenSchreiben;
            if (!f) { out += "  nicht schreibbar\n"; return true; }
            const uint16_t a = uint16_t(arg(1, 0));
            for (size_t i = 2; i < t.size(); ++i) f(uint16_t(a + i - 2), uint8_t(arg(i, 0)));
            return true;
        }
        if (k == "er") { c_.regSchreiben(uint8_t(arg(1, 0)), uint8_t(arg(2, 0))); return true; }
        if (k == "set") {
            if (t.size() < 3) { out += "  set pc|sp|rp|flags|imr|irq|ipr wert\n"; return true; }
            const std::string w = t[1];
            const long v = arg(2, 0);
            if (w == "pc") c_.pc = uint16_t(v);
            else if (w == "sp") c_.sp = uint16_t(v);
            else if (w == "rp") c_.rp = uint8_t(v);
            else if (w == "flags") c_.flags = uint8_t(v);
            else if (w == "imr") c_.imr = uint8_t(v);
            else if (w == "irq") c_.irqReg = uint8_t(v & 0x3F);
            else if (w == "ipr") c_.ipr = uint8_t(v);
            else out += "  unbekannt: " + w + "\n";
            return true;
        }
        if (k == "t" || k == "timer") { out += zaehlerText(c_); return true; }
        if (k == "uart" || k == "sio") { out += uartText(c_); return true; }
        if (k == "irq") { out += irqText(c_); return true; }
        if (k == "ports") { out += portsText(c_); return true; }
        if (k == "bus") { out += busText(c_); return true; }
        if (k == "sicht" || k == "all") {
            out += regText(c_) + zaehlerText(c_) + uartText(c_) + irqText(c_) + portsText(c_) + busText(c_);
            return true;
        }
        if (k == "pin") { c_.setPin(int(arg(1, 3)), int(arg(2, 0)), arg(3, 1) != 0); return true; }
        if (k == "port") { c_.setPort(int(arg(1, 0)), uint8_t(arg(2, 0xFF))); return true; }
        if (k == "irqlog") {
            const size_t n = size_t(arg(1, 16));
            const size_t ab = irqLog_.size() > n ? irqLog_.size() - n : 0;
            for (size_t i = ab; i < irqLog_.size(); ++i)
                out += fmt("  Takt %llu: IRQ%d bei %04X -> %04X\n", (unsigned long long)irqLog_[i].takt,
                           irqLog_[i].n, irqLog_[i].alt, irqLog_[i].neu);
            if (irqLog_.empty()) out += "  (noch keiner)\n";
            return true;
        }
        if (k == "reset") { c_.reset(); c_.step(); out += "-> " + zeileBei(c_.pc) + "\n"; return true; }
        if (k == "takte") { out += fmt("  %llu interne Takte\n", (unsigned long long)c_.takte); return true; }
        if (k == "help" || k == "h" || k == "?") { out += hilfe(); return true; }
        out += "  unbekanntes Kommando '" + k + "' (help)\n";
        return true;
    }

    static std::string hilfe() {
        return "Z8 (cpu z8):\n"
               "  r                       Register, Flags, Arbeitsregister\n"
               "  s [n] | n | fin         Schritt | über CALL | bis RET/IRET\n"
               "  g [adr]                 laufen bis Haltepunkt (bzw. adr)\n"
               "  b adr | bl | bd n|all   Haltepunkte\n"
               "  u [adr [n]]             disassemblieren\n"
               "  a adr befehl            assemblieren in den Programmspeicher\n"
               "  d [adr [n]] | de …      Programm- | Datenspeicher\n"
               "  dr [von [bis]]          Registerdatei (Sicht ohne Wirkung)\n"
               "  e adr b… | ee adr b…    Programm-/Datenspeicher ändern\n"
               "  er reg wert | set pc|sp|rp|flags|imr|irq|ipr wert\n"
               "  t | uart | irq | ports | bus | sicht   Peripherie\n"
               "  pin p bit 0|1 | port p wert            Eingangspegel setzen\n"
               "  irqlog [n] | reset | takte | q\n";
    }

private:
    struct IrqEintrag { int n; uint16_t alt, neu; uint64_t takt; };
    Z8& c_;
    Speicher m_;
    std::deque<IrqEintrag> irqLog_;

    std::string zeileBei(uint16_t a) const { return z8dis::zeile(m_.prog, a); }
    bool spGroesser(uint16_t a, uint16_t b) const {
        return c_.stapelIntern() ? uint8_t(a) > uint8_t(b) : a > b;
    }
    void laufBis(long ziel, std::string& out) {
        uint64_t i = 0;
        bool erster = true;
        while (i++ < maxSchritte) {
            if (!erster && (haltepunkte.count(c_.pc) || long(c_.pc) == ziel)) {
                if (haltepunkte.count(c_.pc)) out += fmt("  Haltepunkt %04X\n", c_.pc);
                return;
            }
            erster = false;
            c_.step();
        }
        out += fmt("  Schrittgrenze (%llu) erreicht\n", (unsigned long long)maxSchritte);
    }
};

/// Eigenständiger Prüfstand für `k1520dbg --z8 <bild>[@org]`: Abzug am Programmbus (Rest FFH),
/// externer Programm- und Datenspeicher je 64 KB RAM.  Kommandos aus @p script, dann stdin.
inline int pruefstand(const std::string& bildArg, const std::string& fassung, const char* script) {
    std::string pfad = bildArg;
    long org = 0;
    const size_t at = bildArg.rfind('@');
    if (at != std::string::npos && at > 0) { pfad = bildArg.substr(0, at); if (!zahl(bildArg.substr(at + 1), org)) org = 0; }
    Z8Config cfg = Z8Config::ub8840();
    if (fassung == "ub8820") cfg = Z8Config::ub8820();
    else if (fassung == "z8681") cfg = Z8Config::z8681();
    else if (!fassung.empty() && fassung != "ub8840") { std::fprintf(stderr, "--z8-fassung ub8840|ub8820|z8681\n"); return 2; }
    std::vector<uint8_t> prog(65536, 0xFF), extProg(65536, 0xFF), daten(65536, 0);
    FILE* f = std::fopen(pfad.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "Z8-Abzug '%s' nicht lesbar\n", pfad.c_str()); return 2; }
    size_t n = 0;
    int ch;
    while ((ch = std::fgetc(f)) != EOF && org + long(n) < 65536) {
        const size_t a = size_t(org) + n++;
        if (a < cfg.programmbus) prog[a] = uint8_t(ch); else extProg[a] = uint8_t(ch);
    }
    std::fclose(f);
    Z8 cpu(cfg);
    cpu.programmLesen = [&](uint16_t a) { return prog[a]; };
    cpu.busLesen = [&](const Z8BusZyklus& c) { return c.datenspeicher ? daten[c.adresse] : extProg[c.adresse]; };
    cpu.busSchreiben = [&](const Z8BusZyklus& c, uint8_t v) { (c.datenspeicher ? daten : extProg)[c.adresse] = v; };
    Speicher sp;
    sp.prog = [&](uint16_t a) { return a < cfg.programmbus ? prog[a] : extProg[a]; };
    sp.progSchreiben = [&](uint16_t a, uint8_t v) { (a < cfg.programmbus ? prog : extProg)[a] = v; };
    sp.daten = [&](uint16_t a) { return daten[a]; };
    sp.datenSchreiben = [&](uint16_t a, uint8_t v) { daten[a] = v; };
    Kontext k(cpu, sp);
    cpu.reset();
    cpu.step();
    std::printf("Z8-Prüfstand (%s): %s, %zu Byte ab %04lX — help für Kommandos\n",
                fassung.empty() ? "ub8840" : fassung.c_str(), pfad.c_str(), n, org);
    auto quelle = [&](FILE* in, bool prompt) {
        char zeile[1024];
        for (;;) {
            if (prompt) { std::printf("z8> "); std::fflush(stdout); }
            if (!std::fgets(zeile, sizeof zeile, in)) return true;
            std::string z = zeile;
            while (!z.empty() && (z.back() == '\n' || z.back() == '\r')) z.pop_back();
            const size_t h = z.find('#');
            if (h == 0) continue;
            if (prompt == false && !z.empty()) std::printf("z8> %s\n", z.c_str());
            std::string out;
            const bool weiter = k.befehl(z == "cpu z8" ? "r" : z, out);
            std::fputs(out.c_str(), stdout);
            if (!weiter) return false;
        }
    };
    if (script) {
        FILE* s = std::fopen(script, "r");
        if (!s) { std::fprintf(stderr, "Skript '%s' nicht lesbar\n", script); return 2; }
        const bool weiter = quelle(s, false);
        std::fclose(s);
        if (!weiter) return 0;
    }
    quelle(stdin, k1520::os::isTerminal(0));
    return 0;
}

}  // namespace dbgz8
