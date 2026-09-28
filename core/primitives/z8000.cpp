/**
 * @file z8000.cpp
 * @brief U8001/U8002 (≙ Z8001/Z8002) — Ausführung.  Dekodieren und Takte:
 *        core/primitives/z8000/z8k_table.h.  Quelle der Semantik: Zilog Z8000
 *        CPU User's Reference Manual (Kap. 6 Befehle, Kap. 7 Ausnahmen, Kap. 8
 *        Refresh, Kap. 9 Bus).  Annahmen: core/primitives/z8000/README.md.
 *
 * @license MIT
 */
#include "core/primitives/z8000.h"

#include <vector>

using z8k::Kind;
using z8k::Operand;
using z8k::Decoded;
using z8k::Insn;

const char* z8kStatusName(Z8kStatus st) {
    static const char* const k[16] = {"INT", "REFR", "IO", "SIO", "SEGTA", "NMIA", "NVIA", "VIA",
                                      "DATA", "STACK", "EDATA", "ESTACK", "INSTR", "IF1", "EPU", "RSV"};
    return k[uint8_t(st) & 15];
}

// ─────────────────────────────────────────────────────────────────────────────
// Befehlsgruppen: aus der Mnemonik der Tabellenzeile, einmal je Prozess.
// ─────────────────────────────────────────────────────────────────────────────

enum class Z8000::Op : uint8_t {
    Illegal, Add, Adc, Sub, Sbc, Or, And, Xor, Cp, Com, Neg, Test, Tset, Clr,
    Ld, Ldk, Lda, Push, Pop, Ldm, Ex, Inc, Dec, Res, Set, Bit,
    Rl, Rlc, Rr, Rrc, ShL, ShA, Sdl, Sda, Rldb, Rrdb, Dab, Extsb, Exts, Extsl,
    Mult, Div, Jp, Call, Calr, Jr, Djnz, Ret, Sc, Tcc, Block, In, Out,
    Halt, Iret, Mset, Mres, Mbit, Mreq, Di, Ei, Ldctl, Ldctlb, Setflg, Resflg, Comflg,
    Nop, Ldps, Epa,
};

namespace {

enum class BlkType : uint8_t { Load, Cmp, CmpStr, Tr, TrTest, In, Out };
struct BlkSpec { BlkType t; int dir; bool rep; bool special; };

struct OpName { const char* mn; uint8_t op; };

/// Wiederholungs-/Blockbefehle: Name ohne „B" → Art, Richtung, Wiederholung, Spezial-E/A.
bool blockSpecOf(const std::string& mnIn, bool isByte, BlkSpec& s) {
    std::string mn = mnIn;
    // Byteform endet auf B (LDIRB, CPSDB, INIB …); TRIB & Co. sind nur Byte und
    // tragen das B fest im Namen.
    if (isByte && mn.size() > 3 && mn.back() == 'B' && mn.compare(0, 2, "TR") != 0) mn.pop_back();
    static const struct { const char* n; BlkType t; int dir; bool rep; bool sp; } k[] = {
        {"LDI", BlkType::Load, 1, false, false},   {"LDIR", BlkType::Load, 1, true, false},
        {"LDD", BlkType::Load, -1, false, false},  {"LDDR", BlkType::Load, -1, true, false},
        {"CPI", BlkType::Cmp, 1, false, false},    {"CPIR", BlkType::Cmp, 1, true, false},
        {"CPD", BlkType::Cmp, -1, false, false},   {"CPDR", BlkType::Cmp, -1, true, false},
        {"CPSI", BlkType::CmpStr, 1, false, false},{"CPSIR", BlkType::CmpStr, 1, true, false},
        {"CPSD", BlkType::CmpStr, -1, false, false},{"CPSDR", BlkType::CmpStr, -1, true, false},
        {"TRIB", BlkType::Tr, 1, false, false},    {"TRIRB", BlkType::Tr, 1, true, false},
        {"TRDB", BlkType::Tr, -1, false, false},   {"TRDRB", BlkType::Tr, -1, true, false},
        {"TRTIB", BlkType::TrTest, 1, false, false},{"TRTIRB", BlkType::TrTest, 1, true, false},
        {"TRTDB", BlkType::TrTest, -1, false, false},{"TRTDRB", BlkType::TrTest, -1, true, false},
        {"INI", BlkType::In, 1, false, false},     {"INIR", BlkType::In, 1, true, false},
        {"IND", BlkType::In, -1, false, false},    {"INDR", BlkType::In, -1, true, false},
        {"SINI", BlkType::In, 1, false, true},     {"SINIR", BlkType::In, 1, true, true},
        {"SIND", BlkType::In, -1, false, true},    {"SINDR", BlkType::In, -1, true, true},
        {"OUTI", BlkType::Out, 1, false, false},   {"OTIR", BlkType::Out, 1, true, false},
        {"OUTD", BlkType::Out, -1, false, false},  {"OTDR", BlkType::Out, -1, true, false},
        {"SOUTI", BlkType::Out, 1, false, true},   {"SOTIR", BlkType::Out, 1, true, true},
        {"SOUTD", BlkType::Out, -1, false, true},  {"SOTDR", BlkType::Out, -1, true, true},
    };
    for (auto& e : k)
        if (mn == e.n) { s = {e.t, e.dir, e.rep, e.sp}; return true; }
    return false;
}

struct Dispatch {
    std::vector<BlkSpec> blk;     // je Tabellenzeile (nur für Op::Block gültig)
};

int widthOf(const Insn& in) {
    if (in.has(z8k::Z8K_B)) return 1;
    if (in.has(z8k::Z8K_Q)) return 8;
    if (in.has(z8k::Z8K_L)) return 4;
    return 2;
}

uint32_t maskOf(int w) { return w >= 4 ? 0xFFFFFFFFu : ((1u << (8 * w)) - 1); }
uint32_t signOf(int w) { return 1u << (8 * w - 1); }

bool parityEven(uint8_t v) {
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (v & 1) == 0;
}

int popcount16(uint16_t v) { int n = 0; while (v) { v &= uint16_t(v - 1); ++n; } return n; }

} // namespace

Z8000::Op Z8000::opOf(const Insn& in) {
    static const OpName kMap[] = {
        {"ADD", uint8_t(Op::Add)}, {"ADDB", uint8_t(Op::Add)}, {"ADDL", uint8_t(Op::Add)},
        {"ADC", uint8_t(Op::Adc)}, {"ADCB", uint8_t(Op::Adc)},
        {"SUB", uint8_t(Op::Sub)}, {"SUBB", uint8_t(Op::Sub)}, {"SUBL", uint8_t(Op::Sub)},
        {"SBC", uint8_t(Op::Sbc)}, {"SBCB", uint8_t(Op::Sbc)},
        {"OR", uint8_t(Op::Or)}, {"ORB", uint8_t(Op::Or)},
        {"AND", uint8_t(Op::And)}, {"ANDB", uint8_t(Op::And)},
        {"XOR", uint8_t(Op::Xor)}, {"XORB", uint8_t(Op::Xor)},
        {"CP", uint8_t(Op::Cp)}, {"CPB", uint8_t(Op::Cp)}, {"CPL", uint8_t(Op::Cp)},
        {"COM", uint8_t(Op::Com)}, {"COMB", uint8_t(Op::Com)},
        {"NEG", uint8_t(Op::Neg)}, {"NEGB", uint8_t(Op::Neg)},
        {"TEST", uint8_t(Op::Test)}, {"TESTB", uint8_t(Op::Test)}, {"TESTL", uint8_t(Op::Test)},
        {"TSET", uint8_t(Op::Tset)}, {"TSETB", uint8_t(Op::Tset)},
        {"CLR", uint8_t(Op::Clr)}, {"CLRB", uint8_t(Op::Clr)},
        {"LD", uint8_t(Op::Ld)}, {"LDB", uint8_t(Op::Ld)}, {"LDL", uint8_t(Op::Ld)},
        {"LDR", uint8_t(Op::Ld)}, {"LDRB", uint8_t(Op::Ld)}, {"LDRL", uint8_t(Op::Ld)},
        {"LDK", uint8_t(Op::Ldk)}, {"LDA", uint8_t(Op::Lda)}, {"LDAR", uint8_t(Op::Lda)},
        {"PUSH", uint8_t(Op::Push)}, {"PUSHL", uint8_t(Op::Push)},
        {"POP", uint8_t(Op::Pop)}, {"POPL", uint8_t(Op::Pop)},
        {"LDM", uint8_t(Op::Ldm)}, {"EX", uint8_t(Op::Ex)}, {"EXB", uint8_t(Op::Ex)},
        {"INC", uint8_t(Op::Inc)}, {"INCB", uint8_t(Op::Inc)},
        {"DEC", uint8_t(Op::Dec)}, {"DECB", uint8_t(Op::Dec)},
        {"RES", uint8_t(Op::Res)}, {"RESB", uint8_t(Op::Res)},
        {"SET", uint8_t(Op::Set)}, {"SETB", uint8_t(Op::Set)},
        {"BIT", uint8_t(Op::Bit)}, {"BITB", uint8_t(Op::Bit)},
        {"RL", uint8_t(Op::Rl)}, {"RLB", uint8_t(Op::Rl)},
        {"RLC", uint8_t(Op::Rlc)}, {"RLCB", uint8_t(Op::Rlc)},
        {"RR", uint8_t(Op::Rr)}, {"RRB", uint8_t(Op::Rr)},
        {"RRC", uint8_t(Op::Rrc)}, {"RRCB", uint8_t(Op::Rrc)},
        {"SLL", uint8_t(Op::ShL)}, {"SLLB", uint8_t(Op::ShL)}, {"SLLL", uint8_t(Op::ShL)},
        {"SRL", uint8_t(Op::ShL)}, {"SRLB", uint8_t(Op::ShL)}, {"SRLL", uint8_t(Op::ShL)},
        {"SLA", uint8_t(Op::ShA)}, {"SLAB", uint8_t(Op::ShA)}, {"SLAL", uint8_t(Op::ShA)},
        {"SRA", uint8_t(Op::ShA)}, {"SRAB", uint8_t(Op::ShA)}, {"SRAL", uint8_t(Op::ShA)},
        {"SDL", uint8_t(Op::Sdl)}, {"SDLB", uint8_t(Op::Sdl)}, {"SDLL", uint8_t(Op::Sdl)},
        {"SDA", uint8_t(Op::Sda)}, {"SDAB", uint8_t(Op::Sda)}, {"SDAL", uint8_t(Op::Sda)},
        {"RLDB", uint8_t(Op::Rldb)}, {"RRDB", uint8_t(Op::Rrdb)}, {"DAB", uint8_t(Op::Dab)},
        {"EXTSB", uint8_t(Op::Extsb)}, {"EXTS", uint8_t(Op::Exts)}, {"EXTSL", uint8_t(Op::Extsl)},
        {"MULT", uint8_t(Op::Mult)}, {"MULTL", uint8_t(Op::Mult)},
        {"DIV", uint8_t(Op::Div)}, {"DIVL", uint8_t(Op::Div)},
        {"JP", uint8_t(Op::Jp)}, {"CALL", uint8_t(Op::Call)}, {"CALR", uint8_t(Op::Calr)},
        {"JR", uint8_t(Op::Jr)}, {"DJNZ", uint8_t(Op::Djnz)}, {"DBJNZ", uint8_t(Op::Djnz)},
        {"RET", uint8_t(Op::Ret)}, {"SC", uint8_t(Op::Sc)},
        {"TCC", uint8_t(Op::Tcc)}, {"TCCB", uint8_t(Op::Tcc)},
        {"IN", uint8_t(Op::In)}, {"INB", uint8_t(Op::In)}, {"SIN", uint8_t(Op::In)}, {"SINB", uint8_t(Op::In)},
        {"OUT", uint8_t(Op::Out)}, {"OUTB", uint8_t(Op::Out)}, {"SOUT", uint8_t(Op::Out)}, {"SOUTB", uint8_t(Op::Out)},
        {"HALT", uint8_t(Op::Halt)}, {"IRET", uint8_t(Op::Iret)},
        {"MSET", uint8_t(Op::Mset)}, {"MRES", uint8_t(Op::Mres)}, {"MBIT", uint8_t(Op::Mbit)},
        {"MREQ", uint8_t(Op::Mreq)}, {"DI", uint8_t(Op::Di)}, {"EI", uint8_t(Op::Ei)},
        {"LDCTL", uint8_t(Op::Ldctl)}, {"LDCTLB", uint8_t(Op::Ldctlb)},
        {"SETFLG", uint8_t(Op::Setflg)}, {"RESFLG", uint8_t(Op::Resflg)}, {"COMFLG", uint8_t(Op::Comflg)},
        {"NOP", uint8_t(Op::Nop)}, {"LDPS", uint8_t(Op::Ldps)},
    };
    if (in.has(z8k::Z8K_EPA)) return Op::Epa;
    BlkSpec b;
    if (blockSpecOf(in.mn, in.has(z8k::Z8K_B), b)) return Op::Block;
    for (auto& e : kMap)
        if (std::string(in.mn) == e.mn) return Op(e.op);
    return Op::Illegal;
}

namespace {
const Dispatch& dispatch() {
    static const Dispatch d = [] {
        Dispatch t;
        const auto& ins = z8k::Table::get().insns();
        t.blk.resize(ins.size());
        for (size_t i = 0; i < ins.size(); ++i) {
            BlkSpec b{};
            if (blockSpecOf(ins[i].mn, ins[i].has(z8k::Z8K_B), b)) t.blk[i] = b;
        }
        return t;
    }();
    return d;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Register
// ─────────────────────────────────────────────────────────────────────────────

Z8000::Z8000(const Config& cfg) : cfg_(cfg) {
    (void)dispatch();
}

uint16_t& Z8000::rw(unsigned n) {
    n &= 15;
    if (n < 14) return Rg[n];
    if (n == 15) return R15[systemMode() ? 1 : 0];
    return R14[(z8001() && systemMode() && segMode()) ? 1 : 0];   // Tabelle 4.1
}

const uint16_t& Z8000::rwc(unsigned n) const { return const_cast<Z8000*>(this)->rw(n); }

uint16_t Z8000::r(unsigned n) const { return rwc(n); }
void Z8000::setR(unsigned n, uint16_t v) { rw(n) = v; }

uint8_t Z8000::rb(unsigned n) const {
    uint16_t w = rwc(n & 7);
    return (n & 8) ? uint8_t(w) : uint8_t(w >> 8);
}
void Z8000::setRB(unsigned n, uint8_t v) {
    uint16_t& w = rw(n & 7);
    w = (n & 8) ? uint16_t((w & 0xFF00) | v) : uint16_t((w & 0x00FF) | (v << 8));
}
// Registerpaare/-quadrupel: das niedrigste Feldbit wird ignoriert (RR3 ≡ RR2) —
// so macht es MAME; am Baustein nicht belegt (README §8 „ungerade Registerpaare").
uint32_t Z8000::rr(unsigned n) const { n &= 14; return (uint32_t(rwc(n)) << 16) | rwc(n + 1); }
void Z8000::setRR(unsigned n, uint32_t v) { n &= 14; rw(n) = uint16_t(v >> 16); rw(n + 1) = uint16_t(v); }
uint64_t Z8000::rq(unsigned n) const { n &= 12; return (uint64_t(rr(n)) << 32) | rr(n + 2); }
void Z8000::setRQ(unsigned n, uint64_t v) { n &= 12; setRR(n, uint32_t(v >> 32)); setRR(n + 2, uint32_t(v)); }

void Z8000::setFcw(uint16_t v) {
    v &= 0xF8FC;                         // Bit 0,1,8..10 reserviert
    if (!z8001()) v &= uint16_t(~FCW_SEG);
    fcw = v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pins
// ─────────────────────────────────────────────────────────────────────────────

void Z8000::setResetLine(bool active) {
    if (active) resetPending_ = true;
    resetLine_ = active;
}

void Z8000::reset() { setResetLine(true); setResetLine(false); }

void Z8000::setNMI(bool active) {
    if (active && !nmiLine_) nmiPending_ = true;   // Flanke H→L
    nmiLine_ = active;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bus
// ─────────────────────────────────────────────────────────────────────────────

uint16_t Z8000::bus(Z8kStatus st, uint8_t seg, uint16_t addr, bool word, bool rd, uint16_t data) {
    Z8kBusCycle c;
    c.st = st;
    c.system = systemMode();
    c.word = word;
    c.read = rd;
    c.seg = z8001() ? uint8_t(seg & 0x7F) : 0;
    c.addr = addr;
    if (rd) return read ? read(c) : 0xFFFF;
    if (write) write(c, data);
    return 0;
}

uint8_t Z8000::memB(const Ea& a) {
    uint16_t v = bus(a.st, a.seg, a.off, false, true);
    return (a.off & 1) ? uint8_t(v) : uint8_t(v >> 8);
}
uint16_t Z8000::memW(const Ea& a) { return bus(a.st, a.seg, uint16_t(a.off & ~1u), true, true); }
uint32_t Z8000::memL(const Ea& a) {
    uint16_t o = uint16_t(a.off & ~1u);
    uint32_t hi = bus(a.st, a.seg, o, true, true);
    return (hi << 16) | bus(a.st, a.seg, uint16_t(o + 2), true, true);
}
void Z8000::memWB(const Ea& a, uint8_t v) { bus(a.st, a.seg, a.off, false, false, uint16_t(v << 8 | v)); }
void Z8000::memWW(const Ea& a, uint16_t v) { bus(a.st, a.seg, uint16_t(a.off & ~1u), true, false, v); }
void Z8000::memWL(const Ea& a, uint32_t v) {
    uint16_t o = uint16_t(a.off & ~1u);
    bus(a.st, a.seg, o, true, false, uint16_t(v >> 16));
    bus(a.st, a.seg, uint16_t(o + 2), true, false, uint16_t(v));
}
uint32_t Z8000::memRead(const Ea& a, int w) {
    return w == 1 ? memB(a) : w == 2 ? memW(a) : memL(a);
}
void Z8000::memWrite(const Ea& a, int w, uint32_t v) {
    if (w == 1) memWB(a, uint8_t(v));
    else if (w == 2) memWW(a, uint16_t(v));
    else memWL(a, v);
}

uint16_t Z8000::ioRead(bool special, uint16_t port, bool word) {
    uint16_t v = bus(special ? Z8kStatus::SpecialIo : Z8kStatus::Io, 0, port, word, true);
    if (word) return v;
    return special ? uint8_t(v >> 8) : uint8_t(v);   // §9.4.3: Standard AD0..7, Spezial AD8..15
}

void Z8000::ioWrite(bool special, uint16_t port, bool word, uint16_t v) {
    if (!word) {
        uint8_t b = uint8_t(v);
        if (cfg_.ioByteOnBothHalves) v = uint16_t(b << 8 | b);
        else v = special ? uint16_t(b << 8) : b;
    }
    bus(special ? Z8kStatus::SpecialIo : Z8kStatus::Io, 0, port, word, false, v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adressen
// ─────────────────────────────────────────────────────────────────────────────

Z8000::Ea Z8000::ptr(unsigned reg, int32_t disp) const {
    Ea e;
    if (segMode()) {
        uint32_t v = rr(reg);
        e.seg = uint8_t((v >> 24) & 0x7F);
        e.off = uint16_t(v + uint32_t(disp));
        e.st = ((reg & 14) == 14) ? Z8kStatus::MemStack : Z8kStatus::MemData;
    } else {
        e.seg = dataSeg();
        e.off = uint16_t(r(reg) + uint32_t(disp));
        e.st = ((reg & 15) == 15) ? Z8kStatus::MemStack : Z8kStatus::MemData;
    }
    return e;
}

void Z8000::ptrAdd(unsigned reg, int delta) {
    uint16_t& o = segMode() ? rw((reg & 14) | 1) : rw(reg);
    o = uint16_t(o + delta);
}

Z8000::Ea Z8000::ea(const Operand& o, uint16_t pcNext) const {
    const bool sg = segMode();
    switch (o.kind) {
    case Kind::IR: return ptr(o.reg);
    case Kind::DA:
        return {sg ? o.seg : dataSeg(), uint16_t(o.value), Z8kStatus::MemData};
    case Kind::X:
        return {sg ? o.seg : dataSeg(), uint16_t(o.value + r(o.reg)), Z8kStatus::MemData};
    case Kind::BA: return ptr(o.reg, int32_t(o.value));
    case Kind::BX: return ptr(o.reg, int32_t(r(o.reg2)));
    case Kind::RA16: case Kind::RA8: case Kind::RA7: case Kind::RA12:
        // LDR/LDAR: Status „Program Reference" (1100), Segment des PC.
        return {pcSeg, uint16_t(pcNext + o.disp), Z8kStatus::MemInstr};
    default:
        return {dataSeg(), 0, Z8kStatus::MemData};
    }
}

// Stapelzeiger-Fortschaltung mit Ausrichtung: ein ungerader Zeiger landet nach
// PUSH/POP auf der geraden Nachbaradresse (o += delta − Bit 0).  Das Handbuch
// schweigt; MAME tut es so (Commit 9e78116399, „correct misaligned stack pointers",
// am System 8000 erprobt) — README §„Ungerade Zeiger".
void Z8000::stackAdd(unsigned reg, int delta) {
    uint16_t& o = segMode() ? rw((reg & 14) | 1) : rw(reg);
    o = uint16_t(o + delta - (o & 1));
}
void Z8000::pushW(uint16_t v) { unsigned sp = spReg(); stackAdd(sp, -2); memWW(ptr(sp), v); }
void Z8000::pushL(uint32_t v) { unsigned sp = spReg(); stackAdd(sp, -4); memWL(ptr(sp), v); }
uint16_t Z8000::popW() { unsigned sp = spReg(); uint16_t v = memW(ptr(sp)); stackAdd(sp, 2); return v; }
uint32_t Z8000::popL() { unsigned sp = spReg(); uint32_t v = memL(ptr(sp)); stackAdd(sp, 4); return v; }

void Z8000::pushPc(uint16_t off) {
    if (segMode()) pushL((uint32_t(pcSeg & 0x7F) << 24) | off);
    else pushW(off);
}
void Z8000::popPc() {
    if (segMode()) { uint32_t v = popL(); pcSeg = uint8_t((v >> 24) & 0x7F); pc = uint16_t(v); }
    else pc = popW();
}

// ─────────────────────────────────────────────────────────────────────────────
// Operanden
// ─────────────────────────────────────────────────────────────────────────────

uint64_t Z8000::regRead(const Operand& o) const {
    switch (o.kind) {
    case Kind::RB: return rb(o.reg);
    case Kind::RW: return r(o.reg);
    case Kind::RL: return rr(o.reg);
    case Kind::RQ: return rq(o.reg);
    default: return 0;
    }
}

uint32_t Z8000::rdOp(const Operand& o, int w, uint16_t pcNext) {
    switch (o.kind) {
    case Kind::RB: case Kind::RW: case Kind::RL: return uint32_t(regRead(o));
    case Kind::IR: case Kind::DA: case Kind::X: case Kind::BA: case Kind::BX: case Kind::RA16:
        return memRead(ea(o, pcNext), w);
    default: return o.value;   // Direktwerte
    }
}

void Z8000::wrOp(const Operand& o, int w, uint32_t v, uint16_t pcNext) {
    switch (o.kind) {
    case Kind::RB: setRB(o.reg, uint8_t(v)); break;
    case Kind::RW: setR(o.reg, uint16_t(v)); break;
    case Kind::RL: setRR(o.reg, v); break;
    case Kind::IR: case Kind::DA: case Kind::X: case Kind::BA: case Kind::BX: case Kind::RA16:
        memWrite(ea(o, pcNext), w, v); break;
    default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Flags
// ─────────────────────────────────────────────────────────────────────────────

bool Z8000::cond(unsigned cc, uint16_t f) {
    const bool C = f & F_C, Z = f & F_Z, S = f & F_S, V = f & F_PV;
    bool r = false;
    switch (cc & 7) {
    case 0: r = false; break;          // F / T
    case 1: r = S != V; break;         // LT / GE
    case 2: r = Z || (S != V); break;  // LE / GT
    case 3: r = C || Z; break;         // ULE / UGT
    case 4: r = V; break;              // OV / NOV
    case 5: r = S; break;              // MI / PL
    case 6: r = Z; break;              // Z / NZ
    case 7: r = C; break;              // C / NC
    }
    return (cc & 8) ? !r : r;
}

void Z8000::flagsZS(uint32_t v, int w) {
    v &= maskOf(w);
    setFlag(F_Z, v == 0);
    setFlag(F_S, (v & signOf(w)) != 0);
}

void Z8000::flagsLogic(uint32_t v, int w) {
    flagsZS(v, w);
    if (w == 1) setFlag(F_PV, parityEven(uint8_t(v)));
}

uint32_t Z8000::add(uint32_t a, uint32_t b, bool c, int w, bool bcd) {
    const uint32_t m = maskOf(w), s = signOf(w);
    a &= m; b &= m;
    uint64_t full = uint64_t(a) + b + (c ? 1 : 0);
    uint32_t r = uint32_t(full) & m;
    setFlag(F_C, full > m);
    flagsZS(r, w);
    setFlag(F_PV, ((a ^ r) & (b ^ r) & s) != 0);
    if (bcd && w == 1) {
        setFlag(F_D, false);
        setFlag(F_H, ((a ^ b ^ r) & 0x10) != 0);
    }
    return r;
}

uint32_t Z8000::sub(uint32_t a, uint32_t b, bool c, int w, bool bcd) {
    const uint32_t m = maskOf(w), s = signOf(w);
    a &= m; b &= m;
    uint64_t sb = uint64_t(b) + (c ? 1 : 0);
    uint32_t r = uint32_t(uint64_t(a) - sb) & m;
    setFlag(F_C, uint64_t(a) < sb);
    flagsZS(r, w);
    setFlag(F_PV, ((a ^ b) & (a ^ r) & s) != 0);
    if (bcd && w == 1) {
        setFlag(F_D, true);
        setFlag(F_H, ((a ^ b ^ r) & 0x10) != 0);
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ablauf
// ─────────────────────────────────────────────────────────────────────────────

int Z8000::refreshCycle() {
    if (cfg_.emitRefreshCycles) bus(Z8kStatus::Refresh, 0, uint16_t(refresh & 0x01FE), true, true);
    refresh = uint16_t((refresh & 0xFE00) | ((refresh + 2) & 0x01FE));
    return 3;
}

int Z8000::finish(int c) {
    c += waits_;
    waits_ = 0;
    if (refresh & 0x8000) {                       // periodischer Refresh (Kap. 8.3)
        int rate = (refresh >> 9) & 0x3F;
        int period = rate ? 4 * rate : 256;
        refreshAcc_ += c;
        while (refreshAcc_ >= period) {
            refreshAcc_ -= period;
            c += refreshCycle();
            c += waits_;
            waits_ = 0;
        }
    }
    cycles += uint64_t(c);
    return c;
}

void Z8000::doReset() {
    resetPending_ = false;
    halted_ = stopped_ = haveW0_ = inRepeat_ = false;
    nmiPending_ = false;
    refresh &= 0x7FFF;
    refreshAcc_ = 0;
    if (mo_) { mo_ = false; if (onMO) onMO(false); }
    // §7.4: Lesezyklen im Systemmodus aus dem Programmspeicher, Segment 0.
    fcw = uint16_t(FCW_SN | (z8001() ? FCW_SEG : 0));
    pcSeg = 0;
    uint16_t f = bus(Z8kStatus::MemInstr, 0, 0x0002, true, true);
    if (z8001()) {
        uint16_t s = bus(Z8kStatus::MemInstr, 0, 0x0004, true, true);
        uint16_t o = bus(Z8kStatus::MemInstr, 0, 0x0006, true, true);
        pcSeg = uint8_t((s >> 8) & 0x7F);
        pc = o;
    } else {
        pc = bus(Z8kStatus::MemInstr, 0, 0x0004, true, true);
    }
    setFcw(f);
}

int Z8000::takeException(Z8kStatus ack, uint16_t entry, uint16_t id, uint16_t savedPc, bool external) {
    int c = 0;
    const uint16_t old = fcw;
    if (external && cfg_.spuriousFetchBeforeAck) {
        bus(Z8kStatus::MemInstrFirst, pcSeg, pc, true, true);   // verworfen, PC bleibt
        c += 3;
    }
    // „Before the acknowledge cycle, the CPU enters segmented (Z8001 only) system mode."
    fcw = uint16_t(old | FCW_SN | (z8001() ? FCW_SEG : 0));
    if (external) {
        id = bus(ack, pcSeg, pc, true, true);
        c += 8;   // Quittung: 8 Takte inkl. 5 automatischer WAIT-Takte (§9.4.5)
    }
    // Sichern: PC, FCW, Kennung (Bild 7-1).
    pushPc(savedPc);
    pushW(old);
    pushW(id);
    // Laden aus der Program Status Area (Programmspeicher, System).
    const uint8_t mult = z8001() ? 2 : 1;
    const uint8_t seg = z8001() ? uint8_t((psapSeg >> 8) & 0x7F) : 0;
    const uint16_t base = uint16_t(psapOff & 0xFF00);
    uint16_t fAddr, pAddr;
    if (entry == PSA_VI) {
        fAddr = uint16_t(base + PSA_VI * mult + (z8001() ? 2 : 0));
        pAddr = uint16_t(fAddr + 2 + 2 * (id & 0xFF));
    } else {
        fAddr = uint16_t(base + entry * mult + (z8001() ? 2 : 0));
        pAddr = uint16_t(fAddr + 2);
    }
    uint16_t nf = bus(Z8kStatus::MemInstr, seg, fAddr, true, true);
    if (z8001()) {
        uint16_t s = bus(Z8kStatus::MemInstr, seg, pAddr, true, true);
        uint16_t o = bus(Z8kStatus::MemInstr, seg, uint16_t(pAddr + 2), true, true);
        pcSeg = uint8_t((s >> 8) & 0x7F);
        pc = o;
    } else {
        pc = bus(Z8kStatus::MemInstr, seg, pAddr, true, true);
    }
    setFcw(nf);
    halted_ = false;
    inRepeat_ = false;
    return c;
}

int Z8000::checkInterrupts() {
    Z8kStatus ack;
    uint16_t entry;
    if (nmiPending_) { nmiPending_ = false; ack = Z8kStatus::NmiAck; entry = PSA_NMI; }
    else if (vi_ && (fcw & FCW_VIE)) { ack = Z8kStatus::ViAck; entry = PSA_VI; }
    else if (nvi_ && (fcw & FCW_NVIE)) { ack = Z8kStatus::NviAck; entry = PSA_NVI; }
    else return 0;
    int c = 0;
    if (inRepeat_) { inRepeat_ = false; pc = lastPc_; pcSeg = lastPcSeg_; c += 7; }  // §6.7 LDIR: +7
    // Eintritt wie SC (33/39 Takte, hier ohne das Holen des SC) + Quittung + Scheinholen.
    c += (z8001() ? 39 : 33) - 3;
    c += takeException(ack, entry, 0, pc, true);
    return c;
}

int Z8000::step() {
    waits_ = 0;
    if (resetLine_) return finish(1);
    if (resetPending_) { doReset(); return finish(z8001() ? 12 : 9); }

    if (busReq_) {                                 // Bus-Disconnect
        if (!busAck_) { busAck_ = true; if (onBusAck) onBusAck(true); }
        return finish(1);
    }
    if (busAck_) { busAck_ = false; if (onBusAck) onBusAck(false); }

    int c = 0;
    if (stopped_) {
        if (stopLine_) return finish(refreshCycle());
        stopped_ = false;
        c += refreshCycle();                       // noch ein Refresh, dann weiter (§8.4)
    } else if (!haveW0_) {
        if (int ic = checkInterrupts()) return finish(ic);
        if (halted_) return finish(3);
        if (inRepeat_) return finish(blockStep(rep_, repNext_, false));
        // erstes Befehlswort
        lastPc_ = pc;
        lastPcSeg_ = pcSeg;
        w0_ = bus(Z8kStatus::MemInstrFirst, pcSeg, pc, true, true);
        haveW0_ = true;
        if (stopLine_) { stopped_ = true; return finish(3 + refreshCycle()); }
        c += 0;
    }
    haveW0_ = false;
    const uint16_t start = pc;

    // Privilegiert bzw. EPA-Befehl — aus dem ersten Wort allein entscheidbar.
    {
        int n = 0;
        const uint16_t* cand = z8k::Table::get().candidates(w0_, n);
        if (n > 0) {
            bool allPriv = true, allEpa = true;
            for (int i = 0; i < n; ++i) {
                const Insn& in = z8k::Table::get().at(cand[i]);
                allPriv &= in.has(z8k::Z8K_PRIV);
                allEpa &= in.has(z8k::Z8K_EPA);
            }
            if (allEpa && !(fcw & FCW_EPA)) {       // Extended Instruction Trap
                c += z8001() ? 39 : 33;
                c += takeException(Z8kStatus::Internal, PSA_EPA, w0_, uint16_t(start + 2), false);
                return finish(c);
            }
            if (allPriv && !systemMode()) {          // Privileged Instruction Trap
                c += z8001() ? 39 : 33;
                c += takeException(Z8kStatus::Internal, PSA_PRIV, w0_, uint16_t(start + 2), false);
                return finish(c);
            }
        }
    }

    Decoded d;
    const uint8_t fseg = pcSeg;
    bool ok = z8k::decode([&](int i) -> uint16_t {
        if (i == 0) return w0_;
        return bus(Z8kStatus::MemInstr, fseg, uint16_t(start + 2 * i), true, true);
    }, segMode(), d);
    const uint16_t pcNext = uint16_t(start + 2 * d.nwords);
    if (!ok) {
        ++illegal_;
        if (onIllegal) onIllegal(pcSeg, start, w0_);
        pc = pcNext;
        return finish(c + 7);
    }
    c += execute(d, pcNext);
    return finish(c);
}

// ─────────────────────────────────────────────────────────────────────────────
// Befehle
// ─────────────────────────────────────────────────────────────────────────────

int Z8000::shiftOp(const Operand& dst, int w, int count, bool arith) {
    const int bits = 8 * w;
    const uint64_t m = maskOf(w), s = signOf(w);
    uint64_t v = rdOp(dst, w, 0) & m;
    const bool sign0 = (v & s) != 0;
    bool ov = false;
    int n = count < 0 ? -count : count;
    int steps = n > bits + 1 ? bits + 1 : n;   // danach ändert sich nichts mehr
    bool c = flag(F_C);
    for (int i = 0; i < steps; ++i) {
        if (count > 0) {
            c = (v & s) != 0;
            v = (v << 1) & m;
        } else {
            c = (v & 1) != 0;
            v = arith ? ((v >> 1) | (v & s)) : (v >> 1);
        }
        if (((v & s) != 0) != sign0) ov = true;
    }
    if (n != 0) setFlag(F_C, c);                 // Weite 0: C undefiniert → bleibt
    flagsZS(uint32_t(v), w);
    if (arith) setFlag(F_PV, ov);                // logisch: V undefiniert → bleibt
    wrOp(dst, w, uint32_t(v), 0);
    return n;
}

int Z8000::blockStep(const Decoded& d, uint16_t pcNext, bool first) {
    const Insn& in = *d.insn;
    const BlkSpec& s = dispatch().blk[size_t(in.index)];
    const int w = in.has(z8k::Z8K_B) ? 1 : 2;
    const Operand* o = d.op;
    bool stop = false;
    unsigned cntReg = o[2].reg;
    switch (s.t) {
    case BlkType::Load: {
        uint32_t v = memRead(ptr(o[1].reg), w);
        memWrite(ptr(o[0].reg), w, v);
        ptrAdd(o[0].reg, s.dir * w);
        ptrAdd(o[1].reg, s.dir * w);
        break;
    }
    case BlkType::Cmp: case BlkType::CmpStr: {
        uint32_t a = (s.t == BlkType::Cmp) ? uint32_t(regRead(o[0])) : memRead(ptr(o[0].reg), w);
        uint32_t b = memRead(ptr(o[1].reg), w);
        const uint16_t keepDH = fcw & (F_D | F_H);
        sub(a, b, false, w, false);            // C, S, V (vorläufig) aus dem Vergleich
        bool hit = cond(o[3].value, fcw);
        fcw = uint16_t((fcw & ~(F_D | F_H)) | keepDH);
        setFlag(F_Z, hit);
        if (s.t == BlkType::CmpStr) ptrAdd(o[0].reg, s.dir * w);
        ptrAdd(o[1].reg, s.dir * w);
        stop = hit;
        break;
    }
    case BlkType::Tr: case BlkType::TrTest: {
        Ea sd = ptr(o[0].reg);
        uint8_t t = memB(sd);
        uint8_t x = memB(ptr(o[1].reg, t));
        setRB(1, x);                            // RH1: TRT = Ergebnis, TR = „undefiniert"
        if (s.t == BlkType::Tr) memWB(sd, x);
        else { setFlag(F_Z, x == 0); stop = x != 0; }
        ptrAdd(o[0].reg, s.dir);
        break;
    }
    case BlkType::In: {
        uint16_t v = ioRead(s.special, r(o[1].reg), w == 2);
        memWrite(ptr(o[0].reg), w, v);
        ptrAdd(o[0].reg, s.dir * w);
        break;
    }
    case BlkType::Out: {
        uint32_t v = memRead(ptr(o[1].reg), w);
        ioWrite(s.special, r(o[0].reg), w == 2, uint16_t(v));
        ptrAdd(o[1].reg, s.dir * w);
        break;
    }
    }
    uint16_t cnt = uint16_t(r(cntReg) - 1);
    setR(cntReg, cnt);
    setFlag(F_PV, cnt == 0);
    int cyc = s.rep ? (first ? d.cycles() : 0) + in.perN : d.cycles();
    if (s.rep && !stop && cnt != 0) {
        inRepeat_ = true;                       // unterbrechbar: PC bleibt am Befehl
        rep_ = d;
        repNext_ = pcNext;
        pc = lastPc_;
        pcSeg = lastPcSeg_;
    } else {
        inRepeat_ = false;
        pc = pcNext;
    }
    return cyc;
}

int Z8000::execute(const Decoded& d, uint16_t pcNext) {
    const Insn& in = *d.insn;
    const Operand* o = d.op;
    const int w = widthOf(in);
    const bool isB = in.has(z8k::Z8K_B);
    int extra = 0;
    bool taken = true;
    pc = pcNext;
    static const std::vector<uint8_t> ops = [] {
        const auto& ins = z8k::Table::get().insns();
        std::vector<uint8_t> v(ins.size());
        for (size_t i = 0; i < ins.size(); ++i) v[i] = uint8_t(opOf(ins[i]));
        return v;
    }();

    switch (Op(ops[size_t(in.index)])) {
    case Op::Illegal: case Op::Nop: break;

    case Op::Add: wrOp(o[0], w, add(rdOp(o[0], w, pcNext), rdOp(o[1], w, pcNext), false, w, isB), pcNext); break;
    case Op::Adc: wrOp(o[0], w, add(rdOp(o[0], w, pcNext), rdOp(o[1], w, pcNext), flag(F_C), w, isB), pcNext); break;
    case Op::Sub: wrOp(o[0], w, sub(rdOp(o[0], w, pcNext), rdOp(o[1], w, pcNext), false, w, isB), pcNext); break;
    case Op::Sbc: wrOp(o[0], w, sub(rdOp(o[0], w, pcNext), rdOp(o[1], w, pcNext), flag(F_C), w, isB), pcNext); break;
    case Op::Cp:  sub(rdOp(o[0], w, pcNext), rdOp(o[1], w, pcNext), false, w, false); break;
    case Op::Or:  { uint32_t v = rdOp(o[0], w, pcNext) | rdOp(o[1], w, pcNext); flagsLogic(v, w); wrOp(o[0], w, v, pcNext); break; }
    case Op::And: { uint32_t v = rdOp(o[0], w, pcNext) & rdOp(o[1], w, pcNext); flagsLogic(v, w); wrOp(o[0], w, v, pcNext); break; }
    case Op::Xor: { uint32_t v = rdOp(o[0], w, pcNext) ^ rdOp(o[1], w, pcNext); flagsLogic(v, w); wrOp(o[0], w, v, pcNext); break; }
    case Op::Com: { Ea e{}; bool mem = o[0].kind != Kind::RB && o[0].kind != Kind::RW;
                    if (mem) e = ea(o[0], pcNext);
                    uint32_t v = ~(mem ? memRead(e, w) : rdOp(o[0], w, pcNext)) & maskOf(w);
                    flagsLogic(v, w);
                    if (mem) memWrite(e, w, v); else wrOp(o[0], w, v, pcNext);
                    break; }
    case Op::Neg: { Ea e{}; bool mem = o[0].kind != Kind::RB && o[0].kind != Kind::RW;
                    if (mem) e = ea(o[0], pcNext);
                    uint32_t v = sub(0, mem ? memRead(e, w) : rdOp(o[0], w, pcNext), false, w, false);
                    if (mem) memWrite(e, w, v); else wrOp(o[0], w, v, pcNext);
                    break; }
    case Op::Test: {
        uint32_t v = rdOp(o[0], w, pcNext);
        flagsLogic(v, w);          // Z, S; nur TESTB setzt P (TEST: bleibt, TESTL: undefiniert → bleibt)
        break;
    }
    case Op::Tset: {
        Ea e{}; bool mem = o[0].kind != Kind::RB && o[0].kind != Kind::RW;
        if (mem) e = ea(o[0], pcNext);
        uint32_t v = mem ? memRead(e, w) : rdOp(o[0], w, pcNext);
        setFlag(F_S, (v & signOf(w)) != 0);
        if (mem) memWrite(e, w, maskOf(w)); else wrOp(o[0], w, maskOf(w), pcNext);
        break;
    }
    case Op::Clr: wrOp(o[0], w, 0, pcNext); break;
    case Op::Ld:  wrOp(o[0], w, rdOp(o[1], w, pcNext), pcNext); break;
    case Op::Ldk: setR(o[0].reg, uint16_t(o[1].value)); break;
    case Op::Lda: {
        Ea e = ea(o[1], pcNext);
        if (segMode()) setRR(o[0].reg, (uint32_t(e.seg) << 24) | e.off);
        else setR(o[0].reg, e.off);
        break;
    }
    case Op::Push: {
        uint32_t v = rdOp(o[1], w, pcNext);
        stackAdd(o[0].reg, -w);
        memWrite(ptr(o[0].reg), w, v);
        break;
    }
    case Op::Pop: {
        uint32_t v = memRead(ptr(o[1].reg), w);
        stackAdd(o[1].reg, w);
        wrOp(o[0], w, v, pcNext);
        break;
    }
    case Op::Ldm: {
        const bool toReg = o[0].kind == Kind::RW;
        const Operand& reg = toReg ? o[0] : o[1];
        const Operand& mem = toReg ? o[1] : o[0];
        const unsigned n = o[2].value;
        Ea e = ea(mem, pcNext);
        for (unsigned i = 0; i < n; ++i) {
            Ea ei{e.seg, uint16_t(e.off + 2 * i), e.st};
            if (toReg) setR((reg.reg + i) & 15, memW(ei));
            else memWW(ei, r((reg.reg + i) & 15));
        }
        extra = int(in.perN * n);
        break;
    }
    case Op::Ex: {
        if (o[1].kind == Kind::RB || o[1].kind == Kind::RW) {
            uint32_t a = rdOp(o[0], w, pcNext), b = rdOp(o[1], w, pcNext);
            wrOp(o[0], w, b, pcNext); wrOp(o[1], w, a, pcNext);
        } else {
            Ea e = ea(o[1], pcNext);
            uint32_t b = memRead(e, w);
            memWrite(e, w, rdOp(o[0], w, pcNext));
            wrOp(o[0], w, b, pcNext);
        }
        break;
    }
    case Op::Inc: case Op::Dec: {
        Ea e{}; bool mem = o[0].kind != Kind::RB && o[0].kind != Kind::RW;
        if (mem) e = ea(o[0], pcNext);
        uint32_t a = mem ? memRead(e, w) : rdOp(o[0], w, pcNext);
        const uint16_t keep = fcw & (F_C | F_D | F_H);
        uint32_t v = (Op(ops[size_t(in.index)]) == Op::Inc) ? add(a, o[1].value, false, w, false)
                                                              : sub(a, o[1].value, false, w, false);
        fcw = uint16_t((fcw & ~(F_C | F_D | F_H)) | keep);
        if (mem) memWrite(e, w, v); else wrOp(o[0], w, v, pcNext);
        break;
    }
    case Op::Res: case Op::Set: case Op::Bit: {
        unsigned bit = (o[1].kind == Kind::BIT) ? o[1].value : r(o[1].reg);
        uint32_t m = 1u << (bit & (w == 1 ? 7 : 15));
        Ea e{}; bool mem = o[0].kind != Kind::RB && o[0].kind != Kind::RW;
        if (mem) e = ea(o[0], pcNext);
        uint32_t v = mem ? memRead(e, w) : rdOp(o[0], w, pcNext);
        Op op = Op(ops[size_t(in.index)]);
        if (op == Op::Bit) { setFlag(F_Z, (v & m) == 0); break; }
        v = (op == Op::Set) ? (v | m) : (v & ~m);
        if (mem) memWrite(e, w, v); else wrOp(o[0], w, v, pcNext);
        break;
    }
    case Op::Rl: case Op::Rlc: case Op::Rr: case Op::Rrc: {
        const int bits = 8 * w;
        const uint32_t m = maskOf(w), s = signOf(w);
        uint32_t v = rdOp(o[0], w, pcNext);
        const bool sign0 = (v & s) != 0;
        bool c = flag(F_C), ov = false;
        Op op = Op(ops[size_t(in.index)]);
        for (unsigned i = 0; i < o[1].value; ++i) {
            if (op == Op::Rl || op == Op::Rlc) {
                bool msb = (v & s) != 0;
                v = ((v << 1) | (op == Op::Rl ? msb : c)) & m;
                c = msb;
            } else {
                bool lsb = v & 1;
                v = (v >> 1) | (uint32_t(op == Op::Rr ? lsb : c) << (bits - 1));
                c = lsb;
            }
            if (((v & s) != 0) != sign0) ov = true;
        }
        setFlag(F_C, c);
        flagsZS(v, w);
        setFlag(F_PV, ov);
        wrOp(o[0], w, v, pcNext);
        break;
    }
    case Op::ShL: extra = in.perN * shiftOp(o[0], w, o[1].disp, false); break;
    case Op::ShA: extra = in.perN * shiftOp(o[0], w, o[1].disp, true); break;
    case Op::Sdl: extra = in.perN * shiftOp(o[0], w, int16_t(r(o[1].reg)), false); break;
    case Op::Sda: extra = in.perN * shiftOp(o[0], w, int16_t(r(o[1].reg)), true); break;
    case Op::Rldb: case Op::Rrdb: {
        uint8_t link = rb(o[0].reg), src = rb(o[1].reg), tmp = link & 0x0F;
        if (Op(ops[size_t(in.index)]) == Op::Rldb) {
            link = uint8_t((link & 0xF0) | (src >> 4));
            src = uint8_t((src << 4) | tmp);
        } else {
            link = uint8_t((link & 0xF0) | (src & 0x0F));
            src = uint8_t((src >> 4) | (tmp << 4));
        }
        setRB(o[1].reg, src);
        setRB(o[0].reg, link);
        setFlag(F_Z, link == 0);
        break;
    }
    case Op::Dab: {
        uint8_t a = rb(o[0].reg), adj = 0;
        bool c = flag(F_C);
        if (flag(F_H) || (a & 0x0F) > 9) adj |= 0x06;
        if (c || a > 0x99) { adj |= 0x60; c = true; }
        uint8_t v = flag(F_D) ? uint8_t(a - adj) : uint8_t(a + adj);
        setFlag(F_C, c);
        flagsZS(v, 1);
        setRB(o[0].reg, v);
        break;
    }
    case Op::Extsb: { uint16_t v = r(o[0].reg); setR(o[0].reg, uint16_t(int16_t(int8_t(v & 0xFF)))); break; }
    case Op::Exts:  { unsigned n = o[0].reg & 14; setR(n, (r(n + 1) & 0x8000) ? 0xFFFF : 0); break; }
    case Op::Extsl: { unsigned n = o[0].reg & 12; setRR(n, (rr(n + 2) & 0x80000000u) ? 0xFFFFFFFFu : 0); break; }
    case Op::Mult: {
        setFlag(F_PV, false);
        if (w == 2) {
            int32_t a = int16_t(r((o[0].reg & 14) + 1));
            int32_t b = int16_t(rdOp(o[1], 2, pcNext));
            int32_t p = a * b;
            setRR(o[0].reg, uint32_t(p));
            setFlag(F_C, p < -32768 || p > 32767);
            setFlag(F_Z, p == 0);
            setFlag(F_S, p < 0);
            if (b == 0) extra = -52;                       // Note 2: 18..23 Takte
        } else {
            int64_t a = int32_t(rr((o[0].reg & 12) + 2));
            int64_t b = int32_t(rdOp(o[1], 4, pcNext));
            int64_t p = a * b;
            setRQ(o[0].reg, uint64_t(p));
            setFlag(F_C, p < -2147483648LL || p > 2147483647LL);
            setFlag(F_Z, p == 0);
            setFlag(F_S, p < 0);
            uint32_t ua = uint32_t(a < 0 ? -a : a);
            extra = (b == 0) ? -252 : in.perN * popcount16(uint16_t(ua));   // 282 + 7n
        }
        break;
    }
    case Op::Div: {
        if (w == 2) {
            int32_t a = int32_t(rr(o[0].reg));
            int32_t b = int16_t(rdOp(o[1], 2, pcNext));
            if (b == 0) {
                setFlag(F_PV, true); setFlag(F_Z, true); setFlag(F_C, false); setFlag(F_S, false);
                extra = -94;
                break;
            }
            int64_t q = int64_t(a) / b, rem = int64_t(a) % b;
            if (q >= -32768 && q <= 32767) {
                setRR(o[0].reg, (uint32_t(uint16_t(rem)) << 16) | uint16_t(q));
                setFlag(F_PV, false); setFlag(F_C, false);
                setFlag(F_Z, q == 0); setFlag(F_S, q < 0);
            } else if (q >= -65536 && q <= 65535) {       // Fall 4
                setRR(o[0].reg, (uint32_t(uint16_t(rem)) << 16) | uint16_t(q));
                setFlag(F_PV, true); setFlag(F_C, true);
                setFlag(F_Z, q == 0); setFlag(F_S, q < 0);
                extra = -82;
            } else {                                       // Fall 3: Ziel undefiniert (bleibt)
                setFlag(F_PV, true); setFlag(F_C, false); setFlag(F_Z, false);
                extra = -82;
            }
        } else {
            int64_t a = int64_t(rq(o[0].reg));
            int32_t b = int32_t(rdOp(o[1], 4, pcNext));
            if (b == 0) {
                setFlag(F_PV, true); setFlag(F_Z, true); setFlag(F_C, false); setFlag(F_S, false);
                extra = -714;
                break;
            }
            // Betragsrechnung in 64 Bit (kein int128 — MSVC).
            uint64_t ua = a < 0 ? 0 - uint64_t(a) : uint64_t(a);
            uint64_t ub = b < 0 ? 0 - uint64_t(int64_t(b)) : uint64_t(b);
            uint64_t uq = ua / ub, ur = ua % ub;
            bool neg = (a < 0) != (b < 0);
            uint32_t q32 = uint32_t(neg ? 0 - uq : uq);
            uint32_t r32 = uint32_t(a < 0 ? 0 - ur : ur);
            bool case1 = neg ? uq <= 0x80000000ull : uq <= 0x7FFFFFFFull;
            bool case4 = neg ? uq <= 0x100000000ull : uq <= 0xFFFFFFFFull;
            if (case1 || case4) {
                setRQ(o[0].reg, (uint64_t(r32) << 32) | q32);
                setFlag(F_PV, !case1); setFlag(F_C, !case1);
                setFlag(F_Z, uq == 0); setFlag(F_S, neg && uq != 0);
                if (!case1) extra = -693;
            } else {
                setFlag(F_PV, true); setFlag(F_C, false); setFlag(F_Z, false);
                extra = -693;
            }
        }
        break;
    }
    case Op::Jp:
        taken = cond(o[0].value, fcw);
        if (taken) { Ea e = ea(o[1], pcNext); pc = e.off; if (segMode()) pcSeg = e.seg; }
        break;
    case Op::Call: {
        Ea e = ea(o[0], pcNext);
        pushPc(pcNext);
        pc = e.off;
        if (segMode()) pcSeg = e.seg;
        break;
    }
    case Op::Calr: pushPc(pcNext); pc = uint16_t(pcNext + o[0].disp); break;
    case Op::Jr:
        taken = cond(o[0].value, fcw);
        if (taken) pc = uint16_t(pcNext + o[1].disp);
        break;
    case Op::Djnz: {
        uint16_t v;
        if (isB) { v = uint8_t(rb(o[0].reg) - 1); setRB(o[0].reg, uint8_t(v)); }
        else { v = uint16_t(r(o[0].reg) - 1); setR(o[0].reg, v); }
        taken = v != 0;
        if (taken) pc = uint16_t(pcNext + o[1].disp);
        break;
    }
    case Op::Ret:
        taken = cond(o[0].value, fcw);
        if (taken) popPc();
        break;
    case Op::Sc:
        return d.cycles() + takeException(Z8kStatus::Internal, PSA_SC, d.w[0], pcNext, false);
    case Op::Tcc:
        if (cond(o[0].value, fcw)) {
            if (isB) setRB(o[1].reg, uint8_t(rb(o[1].reg) | 1));
            else setR(o[1].reg, uint16_t(r(o[1].reg) | 1));
        }
        break;
    case Op::Block: return blockStep(d, pcNext, true);
    case Op::In: {
        bool special = in.mn[0] == 'S';
        uint16_t port = (o[1].kind == Kind::IO) ? r(o[1].reg) : uint16_t(o[1].value);
        uint16_t v = ioRead(special, port, w == 2);
        if (isB) setRB(o[0].reg, uint8_t(v)); else setR(o[0].reg, v);
        break;
    }
    case Op::Out: {
        bool special = in.mn[0] == 'S';
        uint16_t port = (o[0].kind == Kind::IO) ? r(o[0].reg) : uint16_t(o[0].value);
        uint16_t v = isB ? rb(o[1].reg) : r(o[1].reg);
        ioWrite(special, port, w == 2, v);
        break;
    }
    case Op::Halt: halted_ = true; break;
    case Op::Iret: {
        (void)popW();                                   // Kennung verwerfen
        uint16_t f = popW();
        if (z8001()) {                                  // Z8001: nur segmentiert definiert
            uint32_t v;
            if (segMode()) v = popL();
            else { uint32_t hi = popW(); v = (hi << 16) | popW(); }
            pcSeg = uint8_t((v >> 24) & 0x7F);
            pc = uint16_t(v);
        } else {
            pc = popW();
        }
        setFcw(f);
        break;
    }
    case Op::Mset: if (!mo_) { mo_ = true; if (onMO) onMO(true); } break;
    case Op::Mres: if (mo_) { mo_ = false; if (onMO) onMO(false); } break;
    case Op::Mbit: setFlag(F_S, !mi_); break;           // S = 1, wenn µI H (inaktiv)
    case Op::Mreq: {
        setFlag(F_Z, false);
        if (mi_) {
            setFlag(F_S, false);
            if (mo_) { mo_ = false; if (onMO) onMO(false); }
        } else {
            if (!mo_) { mo_ = true; if (onMO) onMO(true); }
            uint32_t n = r(o[0].reg);
            if (n == 0) n = 0x10000;
            setR(o[0].reg, 0);
            extra = int(in.perN * n);
            if (mi_) setFlag(F_S, true);
            else { setFlag(F_S, false); mo_ = false; if (onMO) onMO(false); }
            setFlag(F_Z, true);
        }
        break;
    }
    case Op::Di: case Op::Ei: {
        bool on = Op(ops[size_t(in.index)]) == Op::Ei;
        if (!(o[0].value & 2)) setFlag(FCW_VIE, on);
        if (!(o[0].value & 1)) setFlag(FCW_NVIE, on);
        break;
    }
    case Op::Ldctl: {
        const bool toCtl = o[0].kind == Kind::CTL;
        const unsigned ctl = toCtl ? o[0].value : o[1].value;
        const unsigned rg = toCtl ? o[1].reg : o[0].reg;
        if (toCtl) {
            uint16_t v = r(rg);
            switch (ctl) {
            case 2: setFcw(v); break;
            case 3: refresh = uint16_t(v & 0xFFFE); break;
            case 4: if (z8001()) psapSeg = uint16_t(v & 0x7F00); break;
            case 5: psapOff = uint16_t(v & 0xFF00); break;
            case 6: if (z8001()) R14[0] = v; break;          // Z8002: kein NSPSEG
            case 7: R15[0] = v; break;
            }
        } else {
            uint16_t v = 0;
            switch (ctl) {
            case 2: v = uint16_t(fcw & 0xF8FC); break;
            case 3: v = uint16_t(refresh & 0xFFFE); break;
            case 4: v = uint16_t(psapSeg & 0x7F00); break;
            case 5: v = uint16_t(psapOff & 0xFF00); break;
            case 6: v = z8001() ? R14[0] : 0; break;
            case 7: v = R15[0]; break;
            }
            setR(rg, v);
        }
        break;
    }
    case Op::Ldctlb:
        if (o[0].kind == Kind::FLAGS) fcw = uint16_t((fcw & 0xFF03) | (rb(o[1].reg) & 0xFC));
        else setRB(o[0].reg, uint8_t(fcw & 0xFC));
        break;
    case Op::Setflg: fcw = uint16_t(fcw | ((o[0].value & 0xF) << 4)); break;
    case Op::Resflg: fcw = uint16_t(fcw & ~((o[0].value & 0xF) << 4)); break;
    case Op::Comflg: fcw = uint16_t(fcw ^ ((o[0].value & 0xF) << 4)); break;
    case Op::Ldps: {
        Ea e = ea(o[0], pcNext);
        auto at = [&](int k) { return Ea{e.seg, uint16_t(e.off + k), e.st}; };
        if (segMode()) {
            uint16_t f = memW(at(2)), s = memW(at(4)), off = memW(at(6));
            pcSeg = uint8_t((s >> 8) & 0x7F);
            pc = off;
            setFcw(f);
        } else {
            uint16_t f = memW(at(0));
            pc = memW(at(2));                            // PC-Segment bleibt
            setFcw(f);
        }
        break;
    }
    case Op::Epa:
        // EPA = 1, aber kein Z8070 bestückt: niemand antwortet — als NOP der
        // richtigen Länge (Worte sind schon geholt).  README §„EPA".
        extra = 7;
        break;
    }
    return d.cycles(taken) + extra;
}
