/**
 * @file test_z8_mame.cpp
 * @brief Differenzprüfung des Z8-Kerns (core/primitives/z8.h, AP P19c) gegen MAMEs z8ops.hxx.
 *
 * MAMEs Befehlssemantik (z8ops.hxx + Opcodetabelle aus z8.cpp, fester Stand, Prüfsumme —
 * tests/oracle/CMakeLists.txt) läuft in einer Hülle (mame_shim_z8/).  Je Opcode viele
 * Zufallszustände (Allzweckregister, Flags, RP, SP, IMR, Speicher als Hash der Adresse),
 * Stapel intern und extern; je Seite EIN Befehl; verglichen werden PC, FLAGS, RP, SP, IMR,
 * IRQ-Freigabe, Register 04–7F, alle Speicherschreibzugriffe (Programm- und Datenraum) und
 * die Takte.
 *
 * Ausgeblendet (gezählt, nicht verglichen):
 *  - Fälle, die Port- oder Steuerregister (00–03, F0–FF) als Operand berühren — dort hat der
 *    Kern Peripheriewirkung, die Hülle nicht; abgedeckt in tests/unit/primitives/test_z8*.
 *  - unbelegte Opcodes (MAME: 0 Takte, Kern: NOP 6 Takte [Z8-B1]).
 *  - LD r,X(r)/LD X(r),r mit Basis E0–EF: MAME bildet die Basis als Arbeitsregister ab,
 *    der Kern nimmt sie als Registeradresse [Z8-A3].
 * Bekannte Taktabweichungen (MAME gegen Opcode-Karte UM0016 Figure 134):
 *  - 02/03 ADD r,r / ADD r,Ir: MAME 10, Datenblatt 6.
 *  - PUSH mit externem Stapel: MAME 10/12, Datenblatt 12/14.
 *
 * Label `mame_oracle`; tools/dev.sh test-oracle (braucht Netz beim Konfigurieren).
 */
#include "z8_shim.h"

#include "core/primitives/z8.h"
#include "core/primitives/z8/z8_table.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <random>
#include <set>

namespace {

struct HashMem {
    uint32_t seed;
    std::map<uint16_t, uint8_t> over;
    static uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
    uint8_t b(uint16_t a) const { auto it = over.find(a); return it == over.end() ? uint8_t(mix(a * 2654435761u ^ seed)) : it->second; }
};

struct Fall {
    uint8_t regs[256];
    uint16_t pc, sp;
    uint8_t flags, rp, imr;
    bool irqEin, intern;
    uint8_t bytes[3];
    uint32_t seedP, seedD;
};

struct Ergebnis {
    uint16_t pc, sp;
    uint8_t flags, rp, imr;
    bool irqEin;
    uint8_t regs[0x80];
    std::map<uint16_t, uint8_t> prog, daten;
    int takte;
    bool beruehrt = false;   ///< Port/Steuerregister angefasst (nur MAME-Seite ermittelt)
};

Ergebnis mame(const Fall& f) {
    z8_device d;
    uint8_t regs[256];
    std::copy(f.regs, f.regs + 256, regs);
    HashMem p{f.seedP, {}}, dm{f.seedD, {}};
    for (int i = 0; i < 3; ++i) p.over[uint16_t(f.pc + i)] = f.bytes[i];
    Ergebnis e{};
    d.umg.regLesen = [&](uint8_t a) -> uint8_t {
        if (a <= 3 || a >= 0xF0) e.beruehrt = true;
        if (a >= 0x80 && a < 0xF0) return 0xFF;
        return regs[a];
    };
    d.umg.regSchreiben = [&](uint8_t a, uint8_t v) {
        if (a <= 3 || a >= 0xF0) e.beruehrt = true;
        if (a >= 0x80 && a < 0xF0) return;
        regs[a] = v;
    };
    auto pl = [&](uint16_t a) { return p.b(a); };
    auto ps = [&](uint16_t a, uint8_t v) { p.over[a] = v; e.prog[a] = v; };
    d.m_cache.rd = pl; d.m_program.rd = pl; d.m_program.wr = ps;
    d.m_data.rd = [&](uint16_t a) { return dm.b(a); };
    d.m_data.wr = [&](uint16_t a, uint8_t v) { dm.over[a] = v; e.daten[a] = v; };
    d.m_pc = f.pc; d.m_sp.w = f.sp; d.m_flags = f.flags; d.m_rp = f.rp; d.m_imr = f.imr;
    d.m_irq_initialized = f.irqEin; d.intern = f.intern;
    e.takte = d.schritt();
    e.pc = d.m_pc; e.sp = d.m_sp.w; e.flags = d.m_flags; e.rp = d.m_rp; e.imr = d.m_imr; e.irqEin = d.m_irq_initialized;
    std::copy(regs, regs + 0x80, e.regs);
    return e;
}

Ergebnis kern(const Fall& f) {
    Z8 c(Z8Config::z8681());
    HashMem p{f.seedP, {}}, dm{f.seedD, {}};
    for (int i = 0; i < 3; ++i) p.over[uint16_t(f.pc + i)] = f.bytes[i];
    Ergebnis e{};
    c.busLesen = [&](const Z8BusZyklus& z) { return z.datenspeicher ? dm.b(z.adresse) : p.b(z.adresse); };
    c.busSchreiben = [&](const Z8BusZyklus& z, uint8_t v) {
        if (z.datenspeicher) { dm.over[z.adresse] = v; e.daten[z.adresse] = v; }
        else { p.over[z.adresse] = v; e.prog[z.adresse] = v; }
    };
    c.reset(); c.step();
    c.regSchreiben(0xF8, f.intern ? 0x96 : 0x92);     // A8–A15, AD0–7, Stapel intern/extern
    std::copy(f.regs + 4, f.regs + 0x80, c.reg + 4);
    c.pc = f.pc; c.sp = f.sp; c.flags = f.flags; c.rp = f.rp; c.imr = f.imr; c.setIrqFreigabe(f.irqEin);
    c.irqReg = 0;
    e.takte = c.step();
    e.pc = c.pc; e.sp = c.sp; e.flags = c.flags; e.rp = c.rp; e.imr = c.imr; e.irqEin = c.irqFreigegeben();
    std::copy(c.reg, c.reg + 0x80, e.regs);
    return e;
}

}  // namespace

TEST(Z8Mame, AlleBelegtenOpcodesGegenMame) {
    std::mt19937 rng(0x2826);
    auto r8 = [&] { return uint8_t(rng()); };
    auto feld = [&] {      // Operandenbyte: meist Allzweckregister oder Arbeitsregister
        const unsigned w = rng() % 8;
        if (w < 4) return uint8_t(0x04 + rng() % 0x7C);
        if (w < 7) return uint8_t(0xE0 | (rng() & 15));
        return r8();
    };
    int verglichen = 0, beruehrt = 0, xBasis = 0;
    std::map<std::string, int> taktAbw;
    std::map<int, int> fehlerJeOp;
    for (int op = 0; op < 256; ++op) {
        const z8::Insn& in = z8::tabelle()[uint8_t(op)];
        if (in.mn == z8::Mn::Ungueltig || in.mn == z8::Mn::HALT || in.mn == z8::Mn::STOP) continue;
        for (int i = 0; i < 3000; ++i) {
            Fall f{};
            for (int a = 0; a < 256; ++a) f.regs[a] = (rng() % 5 < 3) ? uint8_t(0x04 + rng() % 0x7C) : r8();
            f.intern = (i & 1) == 0;
            f.rp = uint8_t((1 + rng() % 7) << 4);
            f.flags = r8();
            f.imr = r8();
            f.irqEin = rng() & 1;
            f.sp = f.intern ? uint16_t(r8() << 8 | (0x20 + rng() % 0x50)) : uint16_t(rng());
            f.pc = uint16_t(0x1000 + rng() % 0xE000);
            f.bytes[0] = uint8_t(op); f.bytes[1] = feld(); f.bytes[2] = in.len == 3 ? feld() : r8();
            if (in.fmt == z8::Fmt::r1_IM || in.fmt == z8::Fmt::R1_IM || in.fmt == z8::Fmt::IR1_IM ||
                in.fmt == z8::Fmt::IM || in.fmt == z8::Fmt::cc_RA || in.fmt == z8::Fmt::r1_RA || in.fmt == z8::Fmt::DA ||
                in.fmt == z8::Fmt::cc_DA)
                f.bytes[in.len - 1] = r8();
            if (in.fmt == z8::Fmt::r1_r2 || in.fmt == z8::Fmt::r1_Ir2 || in.fmt == z8::Fmt::r1_Irr2 || in.fmt == z8::Fmt::Irr2_r1 ||
                in.fmt == z8::Fmt::Ir1_Irr2 || in.fmt == z8::Fmt::Irr2_Ir1 || in.fmt == z8::Fmt::Ir1_r2 ||
                in.fmt == z8::Fmt::r1_X || in.fmt == z8::Fmt::X_r1)
                f.bytes[1] = r8();
            if ((in.fmt == z8::Fmt::r1_X || in.fmt == z8::Fmt::X_r1) && (f.bytes[2] & 0xF0) == 0xE0) { ++xBasis; continue; }
            f.seedP = rng(); f.seedD = rng();
            const Ergebnis m = mame(f);
            if (m.beruehrt) { ++beruehrt; continue; }
            const Ergebnis k = kern(f);
            ++verglichen;
            bool gleich = m.pc == k.pc && m.flags == k.flags && m.rp == k.rp && m.imr == k.imr && m.irqEin == k.irqEin &&
                          (f.intern ? (m.sp & 0xFF) == (k.sp & 0xFF) : m.sp == k.sp) &&
                          std::equal(m.regs + 4, m.regs + 0x80, k.regs + 4) && m.prog == k.prog && m.daten == k.daten;
            if (!gleich && fehlerJeOp[op]++ < 3) {
                ADD_FAILURE() << std::hex << "op " << op << " " << int(f.bytes[1]) << " " << int(f.bytes[2])
                              << (f.intern ? " intern" : " extern") << ": PC " << m.pc << "/" << k.pc
                              << " FLAGS " << int(m.flags) << "/" << int(k.flags) << " RP " << int(m.rp) << "/" << int(k.rp)
                              << " SP " << m.sp << "/" << k.sp << " IMR " << int(m.imr) << "/" << int(k.imr);
            }
            if (m.takte != k.takte) {
                char b[64];
                std::snprintf(b, sizeof b, "%02X MAME %d Kern %d%s", op, m.takte, k.takte, f.intern ? "" : " (ext. Stapel)");
                ++taktAbw[b];
            }
        }
    }
    std::set<std::string> erlaubt = {"02 MAME 10 Kern 6", "03 MAME 10 Kern 6",
                                     "70 MAME 10 Kern 12 (ext. Stapel)", "71 MAME 12 Kern 14 (ext. Stapel)",
                                     "02 MAME 10 Kern 6 (ext. Stapel)", "03 MAME 10 Kern 6 (ext. Stapel)"};
    for (auto& kv : taktAbw) {
        std::printf("  Takte: %s  (%d Fälle)%s\n", kv.first.c_str(), kv.second, erlaubt.count(kv.first) ? "  [bekannt]" : "");
        EXPECT_TRUE(erlaubt.count(kv.first)) << "unerwartete Taktabweichung " << kv.first;
    }
    std::printf("  verglichen %d, ausgeblendet: Port/Steuerregister %d, X-Basis E0–EF %d\n", verglichen, beruehrt, xBasis);
    EXPECT_GT(verglichen, 300000);
}
