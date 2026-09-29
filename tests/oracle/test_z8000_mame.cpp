/**
 * @file test_z8000_mame.cpp
 * @brief Differenzprüfung der U8001/U8002-Primitive gegen MAMEs z8000 (BSD-3) als ORAKEL.
 *
 * MAMEs Quellen werden beim Konfigurieren heruntergeladen (fester Stand, Prüfsumme —
 * tests/oracle/CMakeLists.txt) und gegen einen Framework-Ersatz (mame_shim/) übersetzt.
 * Nichts davon fliesst in core/ ein; verglichen wird nur das Ergebnis EINES Befehls:
 *
 *   gleicher Zufallszustand (Register beider Bänke, Flags, PC, Speicherinhalt als
 *   Hashfunktion der Adresse) → je CPU ein Schritt → Register, FCW, PC, alle
 *   geschriebenen Speicherbytes, E/A-Schreibzugriffe.
 *
 * Je Tabellenzeile (core/primitives/z8000/z8k_table.h) viele Zufallsfälle in den Modi
 * Z8001 segmentiert System/Normal und Z8002 System/Normal.  Vom Handbuch als
 * „undefiniert" bezeichnete Flags/Registerteile werden ausgeblendet.  Wo MAME
 * nachweislich vom Handbuch abweicht, steht die Zeile mit Begründung in
 * kBekannteAbweichungen — sie wird gezählt und gemeldet, lässt den Test aber nicht
 * scheitern.  Takte vergleicht die Prüfung nicht (MAMEs Takte sind grob).
 *
 * Label `mame_oracle`, nur mit -DK1520_Z8K_MAME_ORACLE=ON (braucht Netz beim Konfigurieren).
 */
#include "emu.h"
#include "z8000.h"

#include "core/primitives/z8000.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ── gemeinsamer Speicher: Grundinhalt = Hash(Adresse), Schreibzugriffe als Überlagerung
struct HashMem {
    uint32_t seed = 1;
    std::map<uint32_t, uint8_t> over;          // lineare Adresse → Byte
    std::vector<std::pair<uint32_t, uint16_t>> ioW;   // (Raum<<16 | Port&~1, Wert)

    static uint32_t mix(uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x;
    }
    uint8_t base(uint32_t a) const { return uint8_t(mix(a * 2654435761u ^ seed)); }
    uint8_t b(uint32_t a) const { auto it = over.find(a); return it == over.end() ? base(a) : it->second; }
    uint16_t w(uint32_t a) const { a &= ~1u; return uint16_t(b(a) << 8 | b(a + 1)); }
    uint16_t io(int sp, uint16_t port) const {   // Hälften VERSCHIEDEN: prüft die Bytelage
        port &= 0xFFFE;                           // MAME liest Wortweise an der geraden Adresse
        return uint16_t(mix(uint32_t(port) * 40503u ^ seed ^ uint32_t(sp) << 20));
    }
};

constexpr uint32_t lin(uint8_t seg, uint16_t off) { return (uint32_t(seg & 0x7F) << 16) | off; }

// ── MAME-Seite ───────────────────────────────────────────────────────────────
struct State {
    uint16_t R[14]; uint16_t R14[2]; uint16_t R15[2];
    uint16_t fcw; uint8_t pcSeg; uint16_t pc; uint16_t psapSeg, psapOff;
};

template <class Base>
class MameCpu : public Base {
public:
    mame_oracle_bus bus;
    HashMem* mem = nullptr;
    bool z8001;

    MameCpu(bool is8001) : Base(machine_config(), "orakel", nullptr, 4000000), z8001(is8001) {
        bus.readWord = [this](int sp, offs_t a) -> u16 {
            if (sp == AS_IO) return mem->io(0, u16(a));
            if (sp == 5) return mem->io(1, u16(a));
            return mem->w(a & 0x7FFFFF);
        };
        bus.writeWord = [this](int sp, offs_t a, u16 v, u16 m) {
            if (sp == AS_IO || sp == 5) {
                mem->ioW.push_back({uint32_t(sp == 5) << 16 | (a & 0xFFFE), m == 0xFFFF ? v : u16(v & 0xFF)});
                return;
            }
            a &= 0x7FFFFE;
            if (m & 0xFF00) mem->over[a] = u8(v >> 8);
            if (m & 0x00FF) mem->over[a + 1] = u8(v);
        };
        this->oracleBus = &bus;
        this->device_start();
        this->m_irq_req = 0;
    }

    uint16_t& W(int n) { return this->m_regs.W[n ^ 3]; }   // RW() aus z8000cpu.h (Wirt LE)

    void set(const State& s) {
        this->m_fcw = s.fcw;
        for (int i = 0; i < 14; ++i) W(i) = s.R[i];
        const bool sys = s.fcw & 0x4000, seg = z8001 && (s.fcw & 0x8000);
        if (sys && seg)      { W(14) = s.R14[1]; W(15) = s.R15[1]; this->m_nspseg = s.R14[0]; this->m_nspoff = s.R15[0]; }
        else if (sys)        { W(14) = s.R14[0]; W(15) = s.R15[1]; this->m_nspseg = s.R14[1]; this->m_nspoff = s.R15[0]; }
        else                 { W(14) = s.R14[0]; W(15) = s.R15[0]; this->m_nspseg = s.R14[1]; this->m_nspoff = s.R15[1]; }
        this->m_pc = z8001 ? (uint32_t(s.pcSeg) << 16 | s.pc) : s.pc;
        this->m_psapseg = s.psapSeg; this->m_psapoff = s.psapOff;
        this->m_refresh = 0; this->m_halt = false; this->m_irq_req = 0; this->m_op_valid = 0;
    }
    State get() {
        State s{};
        s.fcw = this->m_fcw;
        for (int i = 0; i < 14; ++i) s.R[i] = W(i);
        const bool sys = s.fcw & 0x4000, seg = z8001 && (s.fcw & 0x8000);
        if (sys && seg)      { s.R14[1] = W(14); s.R15[1] = W(15); s.R14[0] = this->m_nspseg; s.R15[0] = this->m_nspoff; }
        else if (sys)        { s.R14[0] = W(14); s.R15[1] = W(15); s.R14[1] = this->m_nspseg; s.R15[0] = this->m_nspoff; }
        else                 { s.R14[0] = W(14); s.R15[0] = W(15); s.R14[1] = this->m_nspseg; s.R15[1] = this->m_nspoff; }
        if (!z8001) s.R14[1] = 0;
        s.pc = uint16_t(this->m_pc); s.pcSeg = z8001 ? uint8_t((this->m_pc >> 16) & 0x7F) : 0;
        s.psapSeg = this->m_psapseg; s.psapOff = this->m_psapoff;
        return s;
    }
    void step() {
        this->m_icount = 1;
        this->execute_run();
        // Interne Traps (SC, privilegiert) setzt MAME als Anforderung; der Eintritt
        // gehört noch zu diesem Befehl.
        if (this->m_irq_req & (0x80 | 0x40 | 0x02)) this->Interrupt();
    }
};

// ── eigene Seite ─────────────────────────────────────────────────────────────
struct Ours {
    Z8000 cpu;
    HashMem* mem = nullptr;
    explicit Ours(bool is8001) : cpu(cfg(is8001)) {
        cpu.read = [this](const Z8kBusCycle& c) -> uint16_t {
            if (c.st == Z8kStatus::Io || c.st == Z8kStatus::SpecialIo) {
                uint16_t v = mem->io(c.st == Z8kStatus::SpecialIo, c.addr);
                // MAMEs Wort an ungerader Adresse kommt vertauscht (s. Schreiben)
                if (c.word && (c.addr & 1)) v = uint16_t(v << 8 | v >> 8);
                return v;
            }
            if (c.isMemory()) return mem->w(lin(c.seg, c.addr));
            return 0xFFFF;
        };
        cpu.write = [this](const Z8kBusCycle& c, uint16_t v) {
            if (c.st == Z8kStatus::Io || c.st == Z8kStatus::SpecialIo) {
                bool sp = c.st == Z8kStatus::SpecialIo;
                uint16_t val = c.word ? v : uint16_t(sp ? v >> 8 : v & 0xFF);
                if (c.word && (c.addr & 1)) val = uint16_t(val << 8 | val >> 8);   // MAMEs Wort an ungerader Adresse
                mem->ioW.push_back({uint32_t(sp) << 16 | (c.addr & 0xFFFE), val});
                return;
            }
            if (!c.isMemory()) return;
            uint32_t a = lin(c.seg, c.addr);
            if (c.word) { a &= ~1u; mem->over[a] = uint8_t(v >> 8); mem->over[a + 1] = uint8_t(v); }
            else mem->over[a] = (c.addr & 1) ? uint8_t(v) : uint8_t(v >> 8);
        };
    }
    static Z8000::Config cfg(bool is8001) {
        Z8000::Config c;
        c.model = is8001 ? Z8000::Model::Z8001 : Z8000::Model::Z8002;
        c.emitRefreshCycles = false;
        return c;
    }
    void set(const State& s) {
        if (cpu.inReset()) cpu.step();                // Resetsequenz einmal durchlaufen
        for (int i = 0; i < 14; ++i) cpu.Rg[i] = s.R[i];
        cpu.R14[0] = s.R14[0]; cpu.R14[1] = s.R14[1]; cpu.R15[0] = s.R15[0]; cpu.R15[1] = s.R15[1];
        cpu.fcw = s.fcw; cpu.pc = s.pc; cpu.pcSeg = s.pcSeg;
        cpu.psapSeg = s.psapSeg; cpu.psapOff = s.psapOff; cpu.refresh = 0;
    }
    State get() const {
        State s{};
        for (int i = 0; i < 14; ++i) s.R[i] = cpu.Rg[i];
        s.R14[0] = cpu.R14[0]; s.R14[1] = cpu.isZ8001() ? cpu.R14[1] : 0;
        s.R15[0] = cpu.R15[0]; s.R15[1] = cpu.R15[1];
        s.fcw = cpu.fcw; s.pc = cpu.pc; s.pcSeg = cpu.pcSeg;
        s.psapSeg = cpu.psapSeg; s.psapOff = cpu.psapOff;
        return s;
    }
};

// ── Ausblendungen „undefiniert" (Handbuch Kap. 6) ─────────────────────────────
struct Masks { uint16_t flags = 0x00FC; uint16_t reg[16]; Masks() { for (auto& r : reg) r = 0xFFFF; } };

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool isOneOf(const std::string& s, std::initializer_list<const char*> l) { for (auto* p : l) if (s == p) return true; return false; }

Masks masksFor(const z8k::Decoded& d, const Z8000& after, const State& before, bool seg) {
    Masks m;
    const std::string mn = d.insn->mn;
    auto noFlag = [&](uint16_t f) { m.flags = uint16_t(m.flags & ~f); };
    std::string base = mn;
    if (d.insn->has(z8k::Z8K_B) && base.size() > 3 && base.back() == 'B' && !starts(base, "TR")) base.pop_back();
    if (isOneOf(base, {"CPI", "CPIR", "CPD", "CPDR"})) noFlag(Z8000::F_C | Z8000::F_S);
    if (isOneOf(base, {"LDI", "LDIR", "LDD", "LDDR", "INI", "INIR", "IND", "INDR", "SINI", "SINIR", "SIND",
                       "SINDR", "OUTI", "OTIR", "OUTD", "OTDR", "SOUTI", "SOTIR", "SOUTD", "SOTDR",
                       "TRIB", "TRIRB", "TRDB", "TRDRB"}))
        noFlag(Z8000::F_Z);
    if (isOneOf(mn, {"TRIB", "TRIRB", "TRDB", "TRDRB"})) m.reg[1] = 0x00FF;          // RH1 undefiniert
    if (mn == "TESTL") noFlag(Z8000::F_PV);
    if (isOneOf(mn, {"RLDB", "RRDB"})) noFlag(Z8000::F_S);
    if (mn == "COMFLG") noFlag(Z8000::F_H);
    const bool logShift = starts(mn, "SLL") || starts(mn, "SRL") || starts(mn, "SDL");
    if (logShift) noFlag(Z8000::F_PV);
    if (starts(mn, "SLL") || starts(mn, "SRL") || starts(mn, "SLA") || starts(mn, "SRA")) {
        if (d.op[1].disp == 0) noFlag(Z8000::F_C);
    }
    if (starts(mn, "SD")) {
        uint16_t cnt = d.op[1].reg < 14 ? before.R[d.op[1].reg] : 1;
        if (cnt == 0) noFlag(Z8000::F_C);
    }
    if ((mn == "DIV" || mn == "DIVL") && (after.fcw & Z8000::F_PV) && !(after.fcw & Z8000::F_C)) {
        noFlag(Z8000::F_S);                                 // Fall 3: S und Ziel undefiniert
        unsigned n = d.op[0].reg & (mn == "DIV" ? 14 : 12), k = mn == "DIV" ? 2 : 4;
        for (unsigned i = 0; i < k; ++i) m.reg[(n + i) & 15] = 0;
    }
    if (mn == "LDA" && seg) m.reg[d.op[0].reg & 14] = 0x7F00;   // reservierte Bits undefiniert
    // LDAR: Handbuch „reserved bits cleared"; MAME setzt Bit 31 (make_segmented_addr) → nur Bit 31 aus
    if (mn == "LDAR" && seg) m.reg[d.op[0].reg & 14] = 0x7FFF;
    if (mn == "LDCTL" && d.op[0].kind == z8k::Kind::RW) {
        switch (d.op[1].value) {
        case 2: m.reg[d.op[0].reg] = 0xF8FC; break;
        case 4: m.reg[d.op[0].reg] = 0x7F00; break;
        case 5: m.reg[d.op[0].reg] = 0xFF00; break;
        default: break;
        }
    }
    return m;
}

/// Zeilen, die das Orakel nicht vergleicht (Grund in der Zeile).
bool skipRow(const z8k::Insn& in) {
    const std::string mn = in.mn;
    if (in.has(z8k::Z8K_EPA)) return true;                              // ohne EPU: eigener Test
    if (isOneOf(mn, {"MBIT", "MREQ", "MSET", "MRES"})) return true;     // µI/µ0: Pins, eigener Test
    return false;
}

/// Bekannte, begründete Abweichungen MAME ↔ Handbuch.  Schlüssel: Mnemonik.
/// Wert: Begründung (für den Bericht).  Wird gefüllt, sobald ein Befund belegt ist.
const std::map<std::string, const char*>& kBekannteAbweichungen() {
    static const std::map<std::string, const char*> k = {
        {"DAB", "MAME prüft die Zehnerstelle nach der Einerkorrektur ohne Übertrag: 96+64 "
                "(=%FA) ergibt dort %00/C=0, das Handbuch (Tabelle DAB, Zeile C=0 9-F/A-F) "
                "und wir %60/C=1."},
    };
    return k;
}

/// Nur Kodierungen, die das Handbuch als gültig beschreibt (sonst gibt es nichts zu vergleichen).
bool validEncoding(const z8k::Decoded& d, const z8k::Insn& in) {
    const std::string mn = in.mn;
    const int bits = in.has(z8k::Z8K_B) ? 8 : in.has(z8k::Z8K_L) ? 32 : 16;
    for (int i = 0; i < d.nops; ++i) {
        const z8k::Operand& o = d.op[i];
        if ((o.kind == z8k::Kind::SHL || o.kind == z8k::Kind::SHR) && (o.disp > bits || o.disp < -bits)) return false;
        if (o.kind == z8k::Kind::BIT && in.has(z8k::Z8K_B) && o.value > 7) return false;   // Bytebit 0..7
    }
    // CALL @SP: Ziel = Stapelzeiger selbst — MAME liest ihn NACH dem Push, wir davor.
    if (mn == "CALL" && d.op[0].kind == z8k::Kind::IR && (d.seg ? (d.op[0].reg & 14) == 14 : d.op[0].reg == 15)) return false;
    if (mn == "CALL" && d.op[0].kind == z8k::Kind::X && (d.seg ? (d.op[0].reg & 14) == 14 : d.op[0].reg == 15)) return false;
    // Ungerades Registerpaar als Ziel von LDA/LDAR (RP): MAME schreibt dann R(n) doppelt
    // (addr_to_reg: n und n|1), liest Zeiger aber als RR(n&~1) — in sich uneinheitlich.
    if ((mn == "LDA" || mn == "LDAR") && d.seg && (d.op[0].reg & 1)) return false;
    if ((mn == "RLDB" || mn == "RRDB") && (d.op[0].reg & 7) == (d.op[1].reg & 7)) return false;
    // PUSH/POP: „the same register cannot be used in both source and destination"
    if (mn.rfind("PUSH", 0) == 0 || mn.rfind("POP", 0) == 0) {
        const z8k::Operand& p = mn.rfind("PUSH", 0) == 0 ? d.op[0] : d.op[1];
        const z8k::Operand& o = mn.rfind("PUSH", 0) == 0 ? d.op[1] : d.op[0];
        unsigned pm = d.seg ? 3u << (p.reg & 14) : 1u << p.reg;
        unsigned om = 0;
        if (o.kind == z8k::Kind::RW || o.kind == z8k::Kind::X) om = 1u << o.reg;
        if (o.kind == z8k::Kind::RL) om = 3u << (o.reg & 14);
        if (o.kind == z8k::Kind::IR) om = d.seg ? 3u << (o.reg & 14) : 1u << o.reg;
        if (pm & om) return false;
    }
    // Übersetzen: RH1 wird zerstört — kein Zeiger/Zähler darf R1 (bzw. RR0) enthalten.
    if (mn.rfind("TR", 0) == 0)
        for (int i = 0; i < 3; ++i) if ((d.op[i].reg & 14) == 0) return false;
    // Blockbefehle: Quelle, Ziel, Zähler getrennt und nicht überlappend (Handbuch).
    if (in.hasW1 && in.nops >= 3 && in.op[2].kind == z8k::Kind::RW &&
        (in.op[0].kind == z8k::Kind::IR || in.op[1].kind == z8k::Kind::IR)) {
        unsigned used = 0;
        for (int i = 0; i < 3; ++i) {
            const z8k::Operand& o = d.op[i];
            unsigned m;
            if (o.kind == z8k::Kind::IR && d.seg) m = 3u << (o.reg & 14);
            else if (o.kind == z8k::Kind::RB) m = 1u << (o.reg & 7);
            else m = 1u << (o.reg & 15);
            if (used & m) return false;
            used |= m;
        }
    }
    return true;
}

int g_segBit15 = 0;   ///< angeglichene PC-Segmentworte (Bit 15)

struct RowStat { int cases = 0, diffs = 0; std::string first; };

std::string hex(uint32_t v, int n = 4) { char b[16]; std::snprintf(b, sizeof b, "%0*X", n, v); return b; }

/// Ein Modus: Z8001 seg System/Normal oder Z8002 System/Normal.
void runMode(bool is8001, uint16_t modeFcw, int perRow, std::map<std::string, RowStat>& stats) {
    std::mt19937 rng(0xC0FFEE ^ modeFcw ^ (is8001 ? 1u : 2u));
    const auto& t = z8k::Table::get();
    const bool seg = is8001 && (modeFcw & 0x8000);
    for (const z8k::Insn& in : t.insns()) {
        if (skipRow(in)) continue;
        for (int k = 0; k < perRow; ++k) {
            // Befehlsworte erzeugen, die GENAU diese Zeile treffen
            uint16_t words[6];
            z8k::Decoded d;
            bool ok = false;
            for (int tries = 0; tries < 64 && !ok; ++tries) {
                for (auto& w : words) w = uint16_t(rng());
                words[0] = uint16_t((words[0] & ~in.mask0) | in.match0);
                if (in.hasW1) words[1] = uint16_t((words[1] & ~in.mask1) | in.match1);
                ok = z8k::decodeWords(words, 6, seg, d) && d.insn == &in;
            }
            if (!ok) continue;
            if (!validEncoding(d, in)) continue;
            // LDCTL REFRESH: unser Zeilenzähler läuft mit der Zeit, MAMEs nicht
            if ((d.op[0].kind == z8k::Kind::CTL && d.op[0].value == 3) ||
                (d.nops > 1 && d.op[1].kind == z8k::Kind::CTL && d.op[1].value == 3)) continue;

            HashMem memA, memB;
            memA.seed = memB.seed = rng();
            State s{};
            for (auto& r : s.R) r = uint16_t(rng());
            for (int i = 0; i < 2; ++i) { s.R14[i] = uint16_t(rng()); s.R15[i] = uint16_t(rng()); }
            if (!is8001) s.R14[1] = 0;
            // Dynamisches Schieben: Weite im gültigen Bereich (Handbuch: −n..+n)
            if (std::string(in.mn).rfind("SD", 0) == 0 && d.op[1].reg < 14) {
                int bits = in.has(z8k::Z8K_B) ? 8 : in.has(z8k::Z8K_L) ? 32 : 16;
                s.R[d.op[1].reg] = uint16_t(int(rng() % uint32_t(2 * bits + 1)) - bits);
                if ((d.op[0].reg & (in.has(z8k::Z8K_B) ? 7 : 15)) == d.op[1].reg) continue;
            }
            if (std::string(in.mn).rfind("SD", 0) == 0 && d.op[1].reg >= 14) continue;
            // Zählregister der Blockbefehle klein halten wäre egal: ein Schritt = ein Durchlauf.
            s.fcw = uint16_t(modeFcw | (rng() & 0x00FC));
            s.pcSeg = is8001 ? uint8_t(rng() & 0x7F) : 0;
            s.pc = uint16_t(rng() & 0xFEFE);   // kein Offsetüberlauf im Befehl: MAME trägt ins Segment (README)
            s.psapSeg = is8001 ? uint16_t(rng() & 0x7F00) : 0;
            s.psapOff = uint16_t(rng() & 0xFF00);
            for (int i = 0; i < 6; ++i) {
                uint32_t a = lin(s.pcSeg, uint16_t(s.pc + 2 * i));
                for (HashMem* m : {&memA, &memB}) { m->over[a] = uint8_t(words[i] >> 8); m->over[a + 1] = uint8_t(words[i]); }
            }
            // Befehlsworte gehören zum Grundinhalt: Überlagerung erst ab hier vergleichen.
            const auto pre = memA.over;

            State ma, oa;
            Ours ours(is8001);
            ours.mem = &memB;
            ours.set(s);
            memB.over = pre;   // Resetlesen schreibt nichts, aber sicher ist sicher
            if (is8001) {
                MameCpu<z8001_device> mame(true);
                mame.mem = &memA; mame.set(s); mame.step(); ma = mame.get();
            } else {
                MameCpu<z8002_device> mame(false);
                mame.mem = &memA; mame.set(s); mame.step(); ma = mame.get();
            }
            ours.cpu.step();
            oa = ours.get();

            // Bekannte Abweichung MAME: im gesicherten PC-Segmentwort (CALL/CALR, Trap-
            // und SC-Rahmen) setzt MAME Bit 15 (make_segmented_addr).  Handbuch: Format
            // 0sss ssss 0000 0000 (Bild 7-1 nennt nur „PC SEGMENT").  Angeglichen und gezählt.
            if (is8001) {
                for (int bank = 0; bank < 2; ++bank)          // Trap-Rahmen: System; CALL: aktueller Modus
                for (uint16_t k : {uint16_t(0), uint16_t(4)}) {
                    uint16_t so = oa.R15[bank];
                    uint8_t ss = uint8_t(oa.R14[bank] >> 8 & 0x7F);
                    uint32_t a = lin(ss, uint16_t(so + k));
                    auto ia = memA.over.find(a), ib = memB.over.find(a);
                    if (ia != memA.over.end() && ib != memB.over.end() && ia->second == uint8_t(ib->second | 0x80) &&
                        ia->second != ib->second) { ia->second = ib->second; ++g_segBit15; }
                }
            }
            Masks m = masksFor(d, ours.cpu, s, seg);
            std::string why;
            auto chk = [&](const char* name, uint32_t a, uint32_t b, uint32_t mask) {
                if (((a ^ b) & mask) && why.empty())
                    why = std::string(name) + " MAME=" + hex(a & mask) + " wir=" + hex(b & mask);
            };
            chk("FCW", ma.fcw, oa.fcw, uint32_t(0xF800 | m.flags));   // Bit 0,1,8..10 reserviert
            chk("PC", ma.pc, oa.pc, 0xFFFF);
            chk("PCSEG", ma.pcSeg, oa.pcSeg, 0x7F);
            for (int i = 0; i < 14; ++i) chk(("R" + std::to_string(i)).c_str(), ma.R[i], oa.R[i], m.reg[i]);
            for (int b = 0; b < 2; ++b) {
                chk(b ? "R14sys" : "R14nrm", ma.R14[b], oa.R14[b], m.reg[14]);
                chk(b ? "R15sys" : "R15nrm", ma.R15[b], oa.R15[b], m.reg[15]);
            }
            chk("PSAPOFF", ma.psapOff, oa.psapOff, 0xFF00);
            if (is8001) chk("PSAPSEG", ma.psapSeg, oa.psapSeg, 0x7F00);
            if (why.empty() && memA.over != memB.over) {
                int shown = 0;
                for (auto& kv : memA.over) {
                    auto it = memB.over.find(kv.first);
                    if (it == memB.over.end() || it->second != kv.second) {
                        why += (why.empty() ? "MEM " : " ") + hex(kv.first, 6) + " MAME=" + hex(kv.second, 2) +
                               " wir=" + (it == memB.over.end() ? std::string("--") : hex(it->second, 2));
                        if (++shown == 3) break;
                    }
                }
                if (!why.empty()) why += " SPsys=" + hex(oa.R14[1]) + ":" + hex(oa.R15[1]);
                if (why.empty()) for (auto& kv : memB.over)
                    if (!memA.over.count(kv.first)) { why = "MEM " + hex(kv.first, 6) + " nur wir=" + hex(kv.second, 2); break; }
            }
            if (why.empty() && memA.ioW != memB.ioW) why = "E/A-Schreibzugriffe verschieden";

            std::string key = std::string(in.mn) + (is8001 ? (seg ? "/seg" : "/1ns") : "/z8002") +
                              ((modeFcw & 0x4000) ? "/sys" : "/nrm");
            RowStat& st = stats[key];
            ++st.cases;
            if (!why.empty()) {
                ++st.diffs;
                if (st.first.empty()) {
                    std::string w;
                    for (int i = 0; i < d.nwords; ++i) w += hex(words[i]) + " ";
                    st.first = "[" + w + "] fcw=" + hex(s.fcw) + ": " + why;
                }
            }
        }
    }
}

} // namespace

TEST(Z8000MameOrakel, JederBefehlWieMame) {
    std::map<std::string, RowStat> stats;
    const int perRow = 200;
    runMode(true, 0xC000, perRow, stats);
    runMode(true, 0x8000, perRow, stats);
    runMode(false, 0x4000, perRow, stats);
    runMode(false, 0x0000, perRow, stats);

    int cases = 0, diffs = 0, known = 0;
    std::map<std::string, std::vector<std::string>> byMn;
    for (auto& kv : stats) {
        cases += kv.second.cases;
        if (!kv.second.diffs) continue;
        std::string mn = kv.first.substr(0, kv.first.find('/'));
        bool isKnown = kBekannteAbweichungen().count(mn) != 0;
        (isKnown ? known : diffs) += kv.second.diffs;
        std::printf("%s %-22s %4d/%-4d  %s\n", isKnown ? "bekannt" : "ABWEICH", kv.first.c_str(),
                    kv.second.diffs, kv.second.cases, kv.second.first.c_str());
    }
    std::printf("Orakel: %d Faelle, %d Abweichungen, %d bekannte (begruendet), %d PC-Segmentworte mit MAME-Bit-15 angeglichen\n",
                cases, diffs, known, g_segBit15);
    for (auto& kv : kBekannteAbweichungen()) std::printf("  bekannt %s: %s\n", kv.first.c_str(), kv.second);
    EXPECT_EQ(diffs, 0);
}
