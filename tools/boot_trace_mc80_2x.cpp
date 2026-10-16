/**
 * @file boot_trace_mc80_2x.cpp
 * @brief `boot_trace --machine mc80-21|mc80-22`: Netz-Ein -> BSYS-Menue, mit Ereignisprotokoll,
 *        PC-Histogramm und DPL-Bild als Text (doc/design/32_... AP M12; Muster boot_trace_mc80.cpp).
 *
 * Eine CPU (K2521), Bild = DPL-Oszilloskop 8 Zeilen x 32 Zeichen (@ref Mc80_2xMachine::screenChar).
 * Die DPL zeichnet alle ~20 ms neu und die Menuemarke in Spalte 5 (`>` / `_`) blinkt mit ~320 ms;
 * deshalb zaehlt die Marke NICHT als Bildaenderung, sonst gaebe es nie einen Stillstand.  Ziel ist das
 * Menue: steht bei Stillstand irgendwo im Bild `TASTENTEST` (Kopf der BSYS-Tabelle 1), ist das Exit 0.
 *
 * @license MIT
 */
#include "tools/boot_trace_mc80.h"
#include "tools/coverage_diff.h"
#include "tools/event_bp.h"
#include "tools/dbg_machine.h"
#include "tools/z80dis_min.h"
#include "core/machines/mc80/mc80_2x.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <unordered_map>

namespace {

const char* portName2x(uint8_t p) {
    if (p <= 0x0F) return "Systemtore 00H-0FH (RSP)";
    if (p >= 0x80 && p <= 0x83) return "ZRE-CTC";
    if (p >= 0x84 && p <= 0x87) return "ZRE-PIO";
    if (p >= 0xE0 && p <= 0xE8) return "PPE (Programmiereinheit)";
    if (p == 0xEF) return "MBI Laufwerksteuerung (EFH)";
    if (p >= 0xF0 && p <= 0xF3) return "DPL-CTC";
    if (p >= 0xF4 && p <= 0xF7) return "DPL-PIO (A Zeichen/Zeile, B Position/Matrix)";
    if (p >= 0xF8 && p <= 0xFB) return "MBI-CTC";
    if (p >= 0xFC && p <= 0xFF) return "MBI-PIO (FCH Datenbyte)";
    return "";
}
/// Ereignis-Ports: Laufwerk, PPE und MBI-PIO; die DPL laeuft im Leerlauf (Bild alle ~20 ms).
bool beobachtet2x(uint8_t p) { return p == 0xEF || (p >= 0xE0 && p <= 0xE8) || (p >= 0xFC && p <= 0xFF); }
/// Datenbytes der Bandschnittstelle: nach Port gefaltet, Wert und PC zaehlen nicht.
bool datenport2x(uint8_t p) { return p == 0xFC || p == 0xFD; }

/// Bild als Text (8 Zeilen x 32).  @p ohneMarke: die blinkende Marke in Spalte 5 wird zu ' '.
std::string bildText2x(Mc80_2xMachine& m, bool ohneMarke) {
    std::string s; s.reserve(8 * 32);
    for (int r = 0; r < 8; ++r) for (int c = 0; c < 32; ++c) {
        uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        char z = (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
        if (ohneMarke && c == 5 && (z == '>' || z == '_')) z = ' ';
        s += z;
    }
    return s;
}

bool menueSteht(const std::string& bild) { return bild.find("TASTENTEST") != std::string::npos; }

}  // namespace

int bootTraceMc80_2x(const K8915TraceOpts& o, const prnlst::Listing& prn, bool v22)
{
    Mc80_2xMachine::Config cfg;
    cfg.variante = v22 ? Mc80_2xMachine::Config::Variante::Mc8022 : Mc80_2xMachine::Config::Variante::Mc8021;
    Mc80_2xMachine m(cfg);
    { std::string err;   // vor dem ersten Lauf stecken (K6022 scheitert: E0H gehoert der PPE)
      if (!dbgm::steckeK6022(m, o.ptape, err)) { fprintf(stderr, "--ptape: %s\n", err.c_str()); return 2; }
      if (!dbgm::steckeK5020(m, o.tape, o.tape1, o.tape2, err)) { fprintf(stderr, "--tape: %s\n", err.c_str()); return 2; } }
    auto rd = [&](uint16_t a) { return m.memReadDebug(a); };
    auto prnTail = [&](uint16_t a) -> std::string {
        const std::string* s = prn.find(a);
        if (!s || !prn.matches(a, rd)) return std::string();
        return "  ; " + *s;
    };
    auto name = [&](uint16_t a) -> std::string {
        std::string l = prn.labelNear(a);
        return l.empty() ? l : " <" + l + ">";
    };

    m.powerOn();
    if (!o.quiet) {
        fprintf(stderr, "=== %s Boot Trace ===\n", Mc80_2xMachine::variantenName(cfg.variante));
        fprintf(stderr, "Max cycles: %lld   Stillstand nach %lld\n", o.limit, o.stall);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "Until:      %s\n", o.until.text.c_str());
        fprintf(stderr, "\n");
    }

    // ── Ereignisprotokoll mit Faltung ────────────────────────────────────────
    FILE* ev = stderr;
    if (!o.events_path.empty()) {
        ev = fopen(o.events_path.c_str(), "w");
        if (!ev) { fprintf(stderr, "WARN: cannot write --events '%s'\n", o.events_path.c_str()); ev = stderr; }
    }
    const bool ev_on = !o.quiet || ev != stderr;
    long ev_lines = 0, ev_total = 0; bool ev_capped = false;
    struct Ev { std::string key, text; uint64_t cyc = 0; uint16_t pc_lo = 0, pc_hi = 0; long n = 0; std::string tail; };
    Ev cur; bool have = false;
    auto emit = [&](const Ev& e) {
        if (!ev_on) return;
        if (ev_lines >= o.events_cap) {
            if (!ev_capped) { fprintf(ev, "  [ev] Grenze %ld Zeilen erreicht — Protokoll endet (--events-cap)\n", o.events_cap); ev_capped = true; }
            return;
        }
        char pcs[24];
        if (e.pc_lo == e.pc_hi) snprintf(pcs, sizeof pcs, "PC=%04X", e.pc_lo);
        else snprintf(pcs, sizeof pcs, "PC=%04X..%04X", e.pc_lo, e.pc_hi);
        fprintf(ev, "  [ev c%-10llu %s] %s", (unsigned long long)e.cyc, pcs, e.text.c_str());
        if (e.n > 1) fprintf(ev, "  ×%ld", e.n);
        fprintf(ev, "%s\n", e.tail.c_str());
        ++ev_lines;
    };
    auto flush = [&] { if (have) emit(cur); have = false; };
    auto event = [&](const std::string& key, const std::string& text, uint16_t pc) {
        ++ev_total;
        if (have && cur.key == key) {
            ++cur.n; cur.pc_lo = std::min(cur.pc_lo, pc); cur.pc_hi = std::max(cur.pc_hi, pc);
            return;
        }
        flush();
        cur = Ev{key, text, m.totalCycles(), pc, pc, 1, prnTail(pc)}; have = true;
    };

    // ── Beobachter ───────────────────────────────────────────────────────────
    std::unordered_map<uint16_t, uint32_t> hist;
    std::unordered_map<uint16_t, std::string> hist_note;
    uint64_t instr = 0;
    uint64_t io_rd[256] = {0}, io_wr[256] = {0};
    uint64_t last_activity = 0;
    bool until_hit = false; uint64_t until_cyc = 0; uint16_t until_pc = 0;
    eventbp::Prev prev;
    long ints = 0;
    FILE* csv = nullptr; long csv_rows = 0;
    if (!o.csv_path.empty()) {
        csv = fopen(o.csv_path.c_str(), "w");
        if (csv) fprintf(csv, "seq,cyc,cpu,pc,bytes,disasm,af,bc,de,hl,ix,iy,sp\n");
        else fprintf(stderr, "WARN: cannot write --csv '%s'\n", o.csv_path.c_str());
    }
    FILE* itr = nullptr; long itr_n = 0;
    if (!o.itrace_path.empty()) {
        itr = fopen(o.itrace_path.c_str(), "w");
        if (itr) fprintf(itr, "seq,cyc,kind,int_pc,isr_pc,sp,vector,device\n");
        else fprintf(stderr, "WARN: cannot write --itrace '%s'\n", o.itrace_path.c_str());
    }
    int win_n = 0;
    uint16_t insn_pc = 0;

    m.setCpuTraceCallback([&](const Z80& z) {
        ++instr; insn_pc = z.PC;
        if (hist[z.PC]++ == 0) hist_note[z.PC] = name(z.PC) + prnTail(z.PC);
        if (csv && (o.win_lo < 0 || (z.PC >= o.win_lo && z.PC <= o.win_hi)) && csv_rows < 5'000'000) {
            z80dis::Insn d = z80dis::decode(rd, z.PC);
            char bytes[16] = {0};
            for (int i = 0; i < d.len && i < 4; ++i) { char b[3]; snprintf(b, 3, "%02X", rd(static_cast<uint16_t>(z.PC + i))); strcat(bytes, b); }
            fprintf(csv, "%ld,%llu,CPU,0x%04X,%s,\"%s\",%04X,%04X,%04X,%04X,%04X,%04X,%04X\n",
                    csv_rows, (unsigned long long)z.cycles, z.PC, bytes, d.text,
                    z.AF, z.BC, z.DE, z.HL, z.IX, z.IY, z.SP);
            ++csv_rows;
        }
        const eventbp::Event e = eventbp::classify(z.PC, z.SP, z.IFF1, prev, true, true, false, 0, 0);
        if (e == eventbp::Event::Interrupt || e == eventbp::Event::NMI) {
            ++ints;
            const uint16_t ret = static_cast<uint16_t>(rd(z.SP) | (rd(static_cast<uint16_t>(z.SP + 1)) << 8));
            const auto& ia = m.bus().lastIntAck();
            const char* dev = e == eventbp::Event::NMI ? "-" : ia.spurious ? "SPURIOUS" : (ia.device ? ia.device : "?");
            char t[160];
            if (e == eventbp::Event::NMI) snprintf(t, sizeof t, "NMI → ISR 0066 (unterbrochen bei %04X)", ret);
            else snprintf(t, sizeof t, "INT (IM %d) Vektor=%02X von %s → ISR %04X%s", z.IM, ia.vector, dev,
                          z.PC, name(z.PC).c_str());
            char key[48]; snprintf(key, sizeof key, "I%02X%04X", e == eventbp::Event::NMI ? 0x100 : ia.vector, z.PC);
            event(key, t, ret);
            if (itr) {
                char vec[8] = "-";
                if (e == eventbp::Event::Interrupt) snprintf(vec, sizeof vec, "0x%02X", ia.vector);
                fprintf(itr, "%ld,%llu,%s,0x%04X,0x%04X,0x%04X,%s,%s\n", itr_n, (unsigned long long)m.totalCycles(),
                        e == eventbp::Event::NMI ? "NMI" : "INT", ret, z.PC, z.SP, vec, dev);
                ++itr_n;
            }
        }
        prev.have = true; prev.sp = z.SP; prev.iff1 = z.IFF1;

        if (o.win_lo >= 0 && z.PC >= o.win_lo && z.PC <= o.win_hi && win_n < o.win_cap && !o.quiet) {
            z80dis::Insn d = z80dis::decode(rd, z.PC);
            fprintf(stderr, "  [w%4d] PC=%04X %-16s AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X%s\n", win_n++,
                    z.PC, d.text, z.AF, z.BC, z.DE, z.HL, z.SP, prnTail(z.PC).c_str());
        }
        if (o.until.kind == untilcond::UntilCond::PC || o.until.kind == untilcond::UntilCond::MEM) {
            long v = o.until.kind == untilcond::UntilCond::PC ? z.PC
                   : o.until.word ? (rd(o.until.addr) | (rd(static_cast<uint16_t>(o.until.addr + 1)) << 8))
                                  : rd(o.until.addr);
            if (!until_hit && o.until.compare(v)) {
                until_hit = true; until_cyc = m.totalCycles(); until_pc = z.PC; m.stop();
            }
        }
    });
    m.setBusTrace([&](bool isIO, bool isRead, uint16_t addr, uint8_t data) {
        const uint16_t pc = insn_pc;
        if (!isIO) {
            if (!isRead)
                for (uint16_t w : o.watch)
                    if (w == addr) {
                        char t[64]; snprintf(t, sizeof t, "WR [%04X]=%02X", addr, data);
                        char k[24]; snprintf(k, sizeof k, "W%04X%02X", addr, data);
                        event(k, t, pc);
                    }
            return;
        }
        const uint8_t p = static_cast<uint8_t>(addr);
        (isRead ? io_rd : io_wr)[p]++;
        bool gewuenscht = beobachtet2x(p);
        for (uint16_t w : o.watchio) if ((w & 0xFF) == p) gewuenscht = true;
        if (!gewuenscht) return;
        if (!isRead) last_activity = m.totalCycles();
        char t[200], k[24];
        if (datenport2x(p)) {
            snprintf(t, sizeof t, "%s (%02XH)  %s", isRead ? "IN " : "OUT", p, portName2x(p));
            snprintf(k, sizeof k, "D%c%02X", isRead ? 'r' : 'w', p);
        } else {
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s", isRead ? "IN " : "OUT", p, data, portName2x(p));
            snprintf(k, sizeof k, "P%c%02X%02X%04X", isRead ? 'r' : 'w', p, data, pc);
        }
        event(k, t, pc);
    });

    // ── Tasten (--keys) ──────────────────────────────────────────────────────
    // Marken <ET> ENTER, <OFF>, <UP> <DN> <LT> <RT>, <CL>, <BK>, <TAB>; sonst das Zeichen selbst.  Ein Block
    // endet mit <ET> und wird erst bei Stillstand getippt.  Die Maschine haelt jede Taste 60 ms und pausiert
    // 60 ms (Maschinenzeit, Mc80_2xMachine::TASTE_*): 300 000 Takte je Taste.
    struct Taste { uint32_t code; bool et; };
    struct Marke { const char* name; uint32_t code; };
    static const Marke marken[] = {{"<ET>", 0x01000004u}, {"<OFF>", 0x01000000u}, {"<UP>", 0x01000013u},
                                   {"<DN>", 0x01000015u}, {"<LT>", 0x01000012u}, {"<RT>", 0x01000014u},
                                   {"<CL>", 0x01000003u}, {"<BK>", 0x01000008u}, {"<TAB>", 0x01000001u}};
    auto gleich = [](const std::string& s, size_t ab, const char* m, size_t n) {   // ohne strncasecmp (MSVC)
        for (size_t k = 0; k < n; ++k)
            if (std::toupper(static_cast<unsigned char>(s[ab + k])) != std::toupper(static_cast<unsigned char>(m[k]))) return false;
        return true;
    };
    std::vector<Taste> tasten;
    for (size_t i = 0; i < o.keys.size(); ++i) {
        bool gefunden = false;
        if (o.keys[i] == '<') {
            for (const Marke& mk : marken) {
                const size_t n = strlen(mk.name);
                if (o.keys.size() - i >= n && gleich(o.keys, i, mk.name, n)) {
                    tasten.push_back({mk.code, mk.code == 0x01000004u});
                    i += n - 1; gefunden = true; break;
                }
            }
        }
        if (!gefunden) tasten.push_back({static_cast<uint8_t>(o.keys[i]), false});
    }
    size_t tasten_pos = 0;
    auto laufe = [&](long long t) { for (long long k = 0; k < t && !until_hit; k += 5000) m.run(5000); };
    auto tippeBlock = [&]() {
        while (tasten_pos < tasten.size()) {
            const Taste t = tasten[tasten_pos++];
            m.keyPress(t.code, false, false);
            laufe(300000);
            if (until_hit) return;
            if (t.et) break;
        }
        laufe(200000);
    };

    // ── Lauf ─────────────────────────────────────────────────────────────────
    const int batch = 20000;
    std::string bild = bildText2x(m, true), bild_alt = bild;
    bool stillstand = false;
    uint64_t next_progress = 10'000'000;
    while (static_cast<long long>(m.totalCycles()) < o.limit) {
        const int n = m.run(batch);
        if (until_hit) break;
        bild = bildText2x(m, true);
        const uint64_t now = m.totalCycles();
        if (bild != bild_alt) { bild_alt = bild; last_activity = now; }
        if (o.until.kind == untilcond::UntilCond::SCREEN && o.until.screenMatch(bildText2x(m, false))) {
            until_hit = true; until_cyc = now; until_pc = m.cpuPC(); break;
        }
        if (now - last_activity >= static_cast<uint64_t>(o.stall)) {
            if (tasten_pos < tasten.size()) {   // Maschine steht: naechster Tastenblock
                tippeBlock();
                if (until_hit) break;
                last_activity = m.totalCycles();
                continue;
            }
            stillstand = true; break;
        }
        if (!o.quiet && now >= next_progress) {
            fprintf(stderr, "[PROGRESS] cycles=%llu PC=%04X instr=%llu\n",
                    (unsigned long long)now, m.cpuPC(), (unsigned long long)instr);
            next_progress = now + 10'000'000;
        }
        if (n == 0) break;
    }
    flush();
    if (ev != stderr) fclose(ev);
    if (csv) { fclose(csv); fprintf(stderr, "--csv: %ld row(s) → %s\n", csv_rows, o.csv_path.c_str()); }
    if (itr) { fclose(itr); fprintf(stderr, "--itrace: %ld INT/NMI → %s\n", itr_n, o.itrace_path.c_str()); }

    const uint64_t cycles = m.totalCycles();
    const bool menue = stillstand && menueSteht(bild);
    if (!o.quiet) {
        fprintf(stderr, "\n=== MC80.2x Boot Trace Complete: %llu cycles ===\n", (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Bildaenderung (Marke in Spalte 5 ausgenommen) und ohne Steuerzugriff (PC=%04X)\n",
                    o.stall, m.cpuPC());
        fprintf(stderr, "Menue:       %s\n", menue ? "JA (BSYS-Menue, Zeile mit TASTENTEST)" : "nein");
        if (!o.keys.empty())
            fprintf(stderr, "Tasten:      %zu von %zu getippt\n", tasten_pos, tasten.size());
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = m.zre().cpu();
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IM=%d IFF1=%d%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IM, z.IFF1, prnTail(z.PC).c_str());

        fprintf(stderr, "\nI/O-Ports (rd/wr):\n");
        for (int p = 0; p < 256; ++p)
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-9llu wr=%-9llu %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p], portName2x(static_cast<uint8_t>(p)));

        std::vector<std::pair<uint32_t, uint16_t>> hs;
        for (auto& kv : hist) hs.push_back({kv.second, kv.first});
        std::sort(hs.rbegin(), hs.rend());
        fprintf(stderr, "\nPC-Histogramm (top 30, %llu Befehle):\n", (unsigned long long)instr);
        for (size_t i = 0; i < hs.size() && i < 30; ++i)
            fprintf(stderr, "  0x%04X : %9u  (%5.2f%%)%s\n", hs[i].second, hs[i].first,
                    100.0 * hs[i].first / static_cast<double>(instr ? instr : 1), hist_note[hs[i].second].c_str());

        const std::string voll = bildText2x(m, false);
        fprintf(stderr, "\nBild (DPL, Text):\n");
        for (int r = 0; r < 8; ++r)
            fprintf(stderr, "  |%s|\n", voll.substr(static_cast<size_t>(r * 32), 32).c_str());
    }
    if (o.coverage) {
        std::vector<bool> cov(0x10000, false);
        for (auto& kv : hist) {
            int len = z80dis::decode(rd, kv.first).len;
            for (int b = 0; b < len; ++b) cov[static_cast<uint16_t>(kv.first + b)] = true;
        }
        auto ranges = covdiff::collapseRanges(cov);
        fprintf(stderr, "\n=== Code coverage (CPU) ===\n  %zu distinct instr addresses, %zu range(s)\n",
                hist.size(), ranges.size());
        for (size_t i = 0; i < ranges.size() && i < 60; ++i)
            fprintf(stderr, "    0x%04X-0x%04X%s\n", ranges[i].first, ranges[i].second,
                    name(static_cast<uint16_t>(ranges[i].first)).c_str());
        if (!o.coverage_path.empty()) {
            if (FILE* cf = fopen(o.coverage_path.c_str(), "w")) {
                fprintf(cf, "cpu,pc,hits\n");
                std::vector<std::pair<uint16_t, uint32_t>> v(hist.begin(), hist.end());
                std::sort(v.begin(), v.end());
                for (auto& kv : v) fprintf(cf, "CPU,0x%04X,%u\n", kv.first, kv.second);
                fclose(cf);
                fprintf(stderr, "  CSV written → %s (cpu,pc,hits)\n", o.coverage_path.c_str());
            }
        }
    }
    if (o.dump_lo >= 0 && o.dump_hi > o.dump_lo) {
        if (FILE* df = fopen(o.dump_path.c_str(), "wb")) {
            for (int a = o.dump_lo; a < o.dump_hi; ++a) fputc(rd(static_cast<uint16_t>(a)), df);
            fclose(df);
            fprintf(stderr, "\nRAM dump (CPU-Sicht) 0x%04X-0x%04X -> %s\n", o.dump_lo, o.dump_hi, o.dump_path.c_str());
        }
    }
    if (o.json) {
        fprintf(stderr,
            "{\"machine\":\"%s\",\"stall\":%s,\"menue\":%s,\"cycles\":%llu,\"final_pc\":\"0x%04X\","
            "\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            v22 ? "mc80-22" : "mc80-21", stillstand ? "true" : "false", menue ? "true" : "false",
            (unsigned long long)cycles, m.cpuPC(), (unsigned long long)instr, ev_total, ints);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "\"until\":{\"set\":true,\"met\":%s,\"cycle\":%llu,\"pc\":\"0x%04X\"}}\n",
                    until_hit ? "true" : "false", (unsigned long long)until_cyc, until_pc);
        else
            fprintf(stderr, "\"until\":{\"set\":false}}\n");
    }
    if (o.until.kind != untilcond::UntilCond::NONE) return until_hit ? 0 : 2;
    return menue ? 0 : 1;
}
