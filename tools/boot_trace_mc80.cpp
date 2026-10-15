/**
 * @file boot_trace_mc80.cpp
 * @brief `boot_trace --machine mc80[-30|-31]`: Netz-Ein -> EGOS-RESET-Kette -> Kommandoschleife,
 *        mit Ereignisprotokoll, PC-Histogramm und Bild als Text (doc/design/32_... AP M8,
 *        Muster tools/boot_trace_pc1715.cpp).
 *
 * Eine CPU (ZVE), Bild = VIS 2a, als Text per Mustervergleich (25 Zeilen).  Protokolliert
 * werden Segmentregister (85H), RAM-Steuerung (8CH/8DH), SPE-2-Sperrtore (B0H-B3H), VIS (C0H-CFH),
 * Kassette (30H-37H) und Interrupts.  Abbruch: `--until`, Stillstand (`--stall` Takte ohne
 * Bildaenderung und ohne Schreibzugriff auf einen beobachteten Port) oder `-c`.  Steht beim
 * Stillstand die Eingabezeile 24 als `>` (EGOS-Prompt) im Bild, ist das das Ziel -> Exit 0.
 *
 * @license MIT
 */
#include "tools/boot_trace_mc80.h"
#include "tools/coverage_diff.h"
#include "tools/event_bp.h"
#include "tools/dbg_machine.h"
#include "tools/z80dis_min.h"
#include "core/machines/mc80/mc80.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

const char* portName(uint8_t p) {
    if (p >= 0x30 && p <= 0x37) return "K5020 Kassette";
    if (p >= 0x80 && p <= 0x83) return "ZVE-CTC";
    if (p == 0x84) return "ZVE-PIO A Daten";
    if (p == 0x85) return "ZVE-PIO B Daten = Segmentregister";
    if (p == 0x86) return "ZVE-PIO A Steuer";
    if (p == 0x87) return "ZVE-PIO B Steuer";
    if (p == 0x8C || p == 0x8D) return "RAM-Steuerung (Schutz)";
    if (p >= 0xB0 && p <= 0xB3) return "SPE 2 Sperre/Freigabe";
    if (p >= 0xC0 && p <= 0xCF) return "VIS 2a";
    if (p >= 0xD0 && p <= 0xD3) return "ASP-SIO";
    if (p >= 0xD4 && p <= 0xD7) return "ASP-CTC";
    if (p >= 0xD8 && p <= 0xDB) return "ASP-PIO";
    return "";
}
/// Ereignis-Ports: Aenderungen am Speicherbild und am Bild; CTC/SIO/PIO laufen im Leerlauf mit.
bool beobachtet(uint8_t p) {
    return (p >= 0x30 && p <= 0x37) || p == 0x85 || p == 0x8C || p == 0x8D
        || (p >= 0xB0 && p <= 0xB3) || (p >= 0xC0 && p <= 0xCF);
}

/// VIS-Ports: Bildkommandos in grosser Zahl -> nach Port gefaltet, weder Wert noch PC zaehlen.
bool datenport(uint8_t p) { return (p >= 0xC0 && p <= 0xCF) || p == 0x8C || p == 0x8D; }   // 8CH/8DH: der RESET-Test zählt sie durch

std::string bildText(Mc80Machine& m, int cols, int rows) {
    std::string s; s.reserve(static_cast<size_t>(cols * rows));
    for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
        uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    return s;
}

/// EGOS wartet auf Eingabe, wenn Zeile 24 nur den Prompt `>` zeigt.
bool promptZeile(const std::string& bild, int cols, int rows) {
    std::string z = bild.substr(static_cast<size_t>((rows - 1) * cols), static_cast<size_t>(cols));
    while (!z.empty() && z.back() == ' ') z.pop_back();
    return z == ">";
}

}  // namespace

int bootTraceMc80(const K8915TraceOpts& o, const prnlst::Listing& prn, bool v31)
{
    Mc80Machine::Config cfg;
    cfg.variante = v31 ? Mc80Machine::Config::Variante::Mc8031 : Mc80Machine::Config::Variante::Mc8030;
    cfg.spe_karten = o.spe;
    Mc80Machine m(cfg);
    const int cols = Mc80Machine::TEXT_SPALTEN, rows = 25;
    { std::string err;   // vor dem ersten Lauf stecken
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
        fprintf(stderr, "=== %s Boot Trace (SPE 2 x%d) ===\n", m.variantenName(cfg.variante), o.spe);
        fprintf(stderr, "Max cycles: %lld   Stillstand nach %lld\n", o.limit, o.stall);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "Until:      %s\n", o.until.text.c_str());
        fprintf(stderr, "\n");
    }

    // ── Ereignisprotokoll mit Faltung (wie PRG 710) ──────────────────────────
    FILE* ev = stderr;
    if (!o.events_path.empty()) {
        ev = fopen(o.events_path.c_str(), "w");
        if (!ev) { fprintf(stderr, "WARN: cannot write --events '%s'\n", o.events_path.c_str()); ev = stderr; }
    }
    const bool ev_on = !o.quiet || ev != stderr;
    long ev_lines = 0, ev_total = 0; bool ev_capped = false;
    struct Ev { std::string key, text; uint64_t cyc = 0; uint16_t pc_lo = 0, pc_hi = 0; long n = 0;
                std::string tail; };
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
        bool gewuenscht = beobachtet(p);
        for (uint16_t w : o.watchio) if ((w & 0xFF) == p) gewuenscht = true;
        if (!gewuenscht) return;
        if (!isRead) last_activity = m.totalCycles();
        char t[200], k[24];
        if (datenport(p)) {
            snprintf(t, sizeof t, "%s (%02XH)  %s", isRead ? "IN " : "OUT", p, portName(p));
            snprintf(k, sizeof k, "D%c%02X", isRead ? 'r' : 'w', p);
        } else {
            std::string zusatz;
            if (!isRead && p == 0x85) {   // Segmentregister: welche Segmente frei werden
                char z[64]; snprintf(z, sizeof z, "  Segmente %02XH", data);
                zusatz = z;
            }
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s%s", isRead ? "IN " : "OUT", p, data, portName(p), zusatz.c_str());
            snprintf(k, sizeof k, "P%c%02X%02X%04X", isRead ? 'r' : 'w', p, data, pc);
        }
        event(k, t, pc);
    });

    // ── Tasten (--keys) ──────────────────────────────────────────────────────
    // `<ET>` = Return (ET-Taste).  Getippt wird blockweise (ein Block endet mit `<ET>`) und
    // erst, wenn die Maschine steht (Stillstand) — also am Prompt, nicht mitten im Laden.
    // Die Tastatur-CPU braucht Entprellung: 5 000-Takt-Pakete, 150 000 halten, 100 000 Pause.
    struct Taste { uint32_t code; bool shift, strg, et; };
    std::vector<Taste> tasten;
    for (size_t i = 0; i < o.keys.size(); ++i) {
        if (o.keys.compare(i, 4, "<ET>") == 0 || o.keys.compare(i, 4, "<et>") == 0) {
            tasten.push_back({0x01000004u, false, false, true});
            i += 3;
        } else {
            const uint8_t c = static_cast<uint8_t>(o.keys[i]);
            tasten.push_back({c, c >= 'A' && c <= 'Z', false, false});
        }
    }
    size_t tasten_pos = 0;
    auto laufe = [&](long long t) { for (long long k = 0; k < t && !until_hit; k += 5000) m.run(5000); };
    auto tippeBlock = [&]() {
        while (tasten_pos < tasten.size()) {
            const Taste t = tasten[tasten_pos++];
            m.keyPress(t.code, t.shift, t.strg);
            laufe(60000);   // die Tastatur liefert im Zeichentakt (9600 Bd, 20 ms je Taste)
            if (until_hit) return;
            if (t.et) break;
        }
        laufe(100000);
    };

    // ── Lauf ─────────────────────────────────────────────────────────────────
    const int batch = 20000;
    std::string bild = bildText(m, cols, rows), bild_alt = bild;
    bool stillstand = false;
    uint64_t next_progress = 10'000'000;
    while (static_cast<long long>(m.totalCycles()) < o.limit) {
        const int n = m.run(batch);
        if (until_hit) break;
        bild = bildText(m, cols, rows);
        const uint64_t now = m.totalCycles();
        if (bild != bild_alt) { bild_alt = bild; last_activity = now; }
        if (o.until.kind == untilcond::UntilCond::SCREEN && o.until.screenMatch(bild)) {
            until_hit = true; until_cyc = now; until_pc = m.cpuPC(); break;
        }
        if (now - last_activity >= static_cast<uint64_t>(o.stall)) {
            if (tasten_pos < tasten.size()) {   // Maschine steht: nächster Tastenblock
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
    const bool prompt = stillstand && promptZeile(bild, cols, rows);
    if (!o.quiet) {
        fprintf(stderr, "\n=== MC80 Boot Trace Complete: %llu cycles ===\n", (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Bildaenderung und ohne Steuerzugriff (PC=%04X)\n",
                    o.stall, m.cpuPC());
        fprintf(stderr, "Prompt:      %s\n", prompt ? "JA" : "nein");
        if (!o.keys.empty())
            fprintf(stderr, "Tasten:      %zu von %zu getippt\n", tasten_pos, tasten.size());
        fprintf(stderr, "Segment:     85H=%02X   RAM-Schutz %s\n", m.zve().segmentRegister(),
                m.zve().schreibschutz() ? "ein" : "aus");
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = m.zve().cpu();
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IM=%d IFF1=%d%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IM, z.IFF1, prnTail(z.PC).c_str());

        fprintf(stderr, "\nI/O-Ports (rd/wr):\n");
        for (int p = 0; p < 256; ++p)
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-9llu wr=%-9llu %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p], portName(static_cast<uint8_t>(p)));

        std::vector<std::pair<uint32_t, uint16_t>> hs;
        for (auto& kv : hist) hs.push_back({kv.second, kv.first});
        std::sort(hs.rbegin(), hs.rend());
        fprintf(stderr, "\nPC-Histogramm (top 30, %llu Befehle):\n", (unsigned long long)instr);
        for (size_t i = 0; i < hs.size() && i < 30; ++i)
            fprintf(stderr, "  0x%04X : %9u  (%5.2f%%)%s\n", hs[i].second, hs[i].first,
                    100.0 * hs[i].first / static_cast<double>(instr ? instr : 1), hist_note[hs[i].second].c_str());

        fprintf(stderr, "\nBild (VIS 2a, Text):\n");
        for (int r = 0; r < rows; ++r)
            fprintf(stderr, "  |%s|\n", bild.substr(static_cast<size_t>(r * cols), static_cast<size_t>(cols)).c_str());
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
            "{\"machine\":\"%s\",\"stall\":%s,\"prompt\":%s,\"cycles\":%llu,\"final_pc\":\"0x%04X\","
            "\"seg\":\"0x%02X\",\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            v31 ? "mc80-31" : "mc80-30", stillstand ? "true" : "false", prompt ? "true" : "false",
            (unsigned long long)cycles, m.cpuPC(), m.zve().segmentRegister(),
            (unsigned long long)instr, ev_total, ints);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "\"until\":{\"set\":true,\"met\":%s,\"cycle\":%llu,\"pc\":\"0x%04X\"}}\n",
                    until_hit ? "true" : "false", (unsigned long long)until_cyc, until_pc);
        else
            fprintf(stderr, "\"until\":{\"set\":false}}\n");
    }
    if (o.until.kind != untilcond::UntilCond::NONE) return until_hit ? 0 : 2;
    return prompt ? 0 : 1;
}
