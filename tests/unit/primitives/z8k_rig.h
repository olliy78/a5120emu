// Prüfstand für die U8001/U8002-Primitive: flacher Speicher (Segment·64K + Offset,
// big-endian), E/A-Tabellen, Busprotokoll, Assembler aus S2 (tools/z8000/z8k_asm.h).
#pragma once
#include "core/primitives/z8000.h"
#include "tools/z8000/z8k_asm.h"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace z8ktest {

struct Rig {
    Z8000 cpu;
    std::vector<uint8_t> mem = std::vector<uint8_t>(size_t(1) << 23, 0);
    std::map<uint16_t, uint16_t> io, sio;          ///< was E/A-Lesen liefert (AD0..15)
    std::vector<std::pair<Z8kBusCycle, uint16_t>> log;   ///< alle Zyklen (Lesen: gelieferter Wert)
    std::vector<std::pair<Z8kBusCycle, uint16_t>> ioWrites;
    uint16_t ackValue = 0;                          ///< Kennung für Interrupt-/Segmenttrapquittungen
    bool logging = true;
    /// Wie die MMU: /SEGT fällt mit der Segmenttrap-Quittung (Status 0100).
    bool segtFaelltBeiQuittung = true;
    /// Wird VOR jedem Zyklus gerufen (Lesen und Schreiben) — um Pins mitten im Befehl zu
    /// setzen, wie es eine MMU oder ein Peripheriebaustein täte.
    std::function<void(const Z8kBusCycle&)> vorZyklus;

    explicit Rig(Z8000::Model m = Z8000::Model::Z8001) : cpu(cfg(m)) {
        cpu.read = [this](const Z8kBusCycle& c) -> uint16_t {
            if (vorZyklus) vorZyklus(c);
            uint16_t v = 0xFFFF;
            if (c.isMemory()) {
                uint32_t a = lin(c.seg, uint16_t(c.addr & ~1u));
                v = uint16_t(mem[a] << 8 | mem[a + 1]);
            } else if (c.st == Z8kStatus::Io) {
                auto it = io.find(c.addr); v = it == io.end() ? 0xFFFF : it->second;
            } else if (c.st == Z8kStatus::SpecialIo) {
                auto it = sio.find(c.addr); v = it == sio.end() ? 0xFFFF : it->second;
            } else if (c.st == Z8kStatus::ViAck || c.st == Z8kStatus::NviAck || c.st == Z8kStatus::NmiAck) {
                v = ackValue;
            } else if (c.st == Z8kStatus::SegTrapAck) {
                v = ackValue;
                if (segtFaelltBeiQuittung) cpu.setSEGT(false);
            }
            if (logging) log.push_back({c, v});
            return v;
        };
        cpu.write = [this](const Z8kBusCycle& c, uint16_t v) {
            if (vorZyklus) vorZyklus(c);
            if (logging) log.push_back({c, v});
            if (c.isMemory()) {
                uint32_t a = lin(c.seg, c.addr);
                if (c.word) { mem[a & ~1u] = uint8_t(v >> 8); mem[(a & ~1u) + 1] = uint8_t(v); }
                else mem[a] = (c.addr & 1) ? uint8_t(v) : uint8_t(v >> 8);
            } else {
                ioWrites.push_back({c, v});
            }
        };
    }
    static Z8000::Config cfg(Z8000::Model m) { Z8000::Config c; c.model = m; return c; }
    static uint32_t lin(uint8_t seg, uint16_t off) { return (uint32_t(seg & 0x7F) << 16) | off; }

    uint16_t w(uint8_t seg, uint16_t off) const { uint32_t a = lin(seg, off); return uint16_t(mem[a] << 8 | mem[a + 1]); }
    void setW(uint8_t seg, uint16_t off, uint16_t v) { uint32_t a = lin(seg, off); mem[a] = uint8_t(v >> 8); mem[a + 1] = uint8_t(v); }
    uint8_t b(uint8_t seg, uint16_t off) const { return mem[lin(seg, off)]; }
    void setB(uint8_t seg, uint16_t off, uint8_t v) { mem[lin(seg, off)] = v; }

    /// Assembliert `src` (SEG/NONSEG, ORG …) in den Speicher.
    void load(const std::string& src, bool seg = true) {
        z8k::AsmOptions o; o.seg = seg;
        z8k::AsmResult r = z8k::assemble(src, o);
        for (auto& e : r.errors) ADD_FAILURE() << "asm Zeile " << e.line << ": " << e.text;
        for (auto& kv : r.image) mem[kv.first & 0x7FFFFF] = kv.second;
    }

    /// Resetvektor setzen und Reset ausführen (Z8001: FCW/Seg/Off, Z8002: FCW/PC).
    void boot(uint16_t fcw, uint8_t seg, uint16_t pc) {
        setW(0, 2, fcw);
        if (cpu.isZ8001()) { setW(0, 4, uint16_t(seg << 8)); setW(0, 6, pc); }
        else setW(0, 4, pc);
        cpu.reset();
        cpu.step();
        log.clear();
    }

    /// Schritte bis HALT (oder maxSteps).  Rückgabe: Summe der Takte.
    uint64_t runToHalt(int maxSteps = 100000) {
        uint64_t c = 0;
        for (int i = 0; i < maxSteps && !cpu.halted(); ++i) c += uint64_t(cpu.step());
        EXPECT_TRUE(cpu.halted()) << "kein HALT nach " << maxSteps << " Schritten";
        return c;
    }

    /// Program Status Area anlegen: Z8001-Eintrag (FCW, Segment, Offset) an PSAP+entry*2.
    void psa(uint8_t psaSeg, uint16_t psaOff, uint16_t entry, uint16_t fcw, uint8_t seg, uint16_t off) {
        if (cpu.isZ8001()) {
            uint16_t base = uint16_t(psaOff + entry * 2);
            setW(psaSeg, uint16_t(base + 2), fcw);
            setW(psaSeg, uint16_t(base + 4), uint16_t(seg << 8));
            setW(psaSeg, uint16_t(base + 6), off);
        } else {
            setW(0, uint16_t(psaOff + entry), fcw);
            setW(0, uint16_t(psaOff + entry + 2), off);
        }
    }

    int count(Z8kStatus st) const {
        int n = 0;
        for (auto& e : log) n += e.first.st == st;
        return n;
    }
};

} // namespace z8ktest
