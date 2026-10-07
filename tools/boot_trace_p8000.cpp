/**
 * @file boot_trace_p8000.cpp
 * @brief `boot_trace --machine p8000`: Netz-Ein → MON8-Hardwaretest → „Press RETURN" → `>` →
 *        BOOT (Urlader, OSLOAD, UDOS), mit Ereignisprotokoll, PC-Histogramm und dem Bild des
 *        Kern-Terminals (tty1) als Text (doc/design/25_p8000.md AP P7c, Muster
 *        tools/boot_trace_pc1715.cpp).
 *
 * Protokolliert werden ADP/RFF (00H–07H), Latches (10H–17H), PIO2/Floppy-Port (1CH–1FH),
 * U8272 (20H/21H), DMA (3CH–3FH) und Interrupts.  Gleiche aufeinanderfolgende Ereignisse
 * werden gefaltet (×N); bei den Datentoren (FDC 21H, DMA) zählt weder Wert noch PC.
 *
 * Abbruch: `--until`, Stillstand (`--stall` Takte ohne Terminaländerung und ohne
 * Schreibzugriff auf einen beobachteten Port) oder `-c`.  Steht beim Stillstand die Cursorzeile
 * `>`/`%` oder eine Zeile mit „Press RETURN", ist das das Ziel → Exit 0.
 *
 * @license MIT
 */
#include "tools/boot_trace_p8000.h"
#include "tools/coverage_diff.h"
#include "tools/event_bp.h"
#include "tools/z80dis_min.h"
#include "core/machines/p8000/p8000.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

const char* portName(uint8_t p) {
    if (p <= 0x07) return "ADP/RFF (Speicher8)";
    if (p >= 0x10 && p <= 0x13) return "Latch 1 (DS8282)";
    if (p >= 0x14 && p <= 0x17) return "Latch 2 (DS8282)";
    if (p >= 0x1C && p <= 0x1F) return "PIO2 (Floppy-Port)";
    if (p == 0x20) return "U8272 Status";
    if (p == 0x21) return "U8272 Daten";
    if (p >= 0x3C && p <= 0x3F) return "UA858 DMA";
    return "";
}
bool beobachtet(uint8_t p) {
    return p <= 0x07 || (p >= 0x10 && p <= 0x17) || (p >= 0x1C && p <= 0x21) || (p >= 0x3C && p <= 0x3F);
}
bool datenport(uint8_t p) { return p == 0x21 || (p >= 0x3C && p <= 0x3F); }

std::string bildText(const P8000Machine& m) {
    using T = k1520::p8000::Terminal;
    std::string s; s.reserve(size_t(T::SPALTEN * T::ZEILEN));
    for (int r = 0; r < T::ZEILEN; ++r) s += m.terminalZeile(r);
    for (char& c : s) if (c < 0x20 || c >= 0x7F) c = ' ';
    return s;
}

std::string zeileAus(const std::string& bild, int r) {
    using T = k1520::p8000::Terminal;
    std::string z = bild.substr(size_t(r * T::SPALTEN), size_t(T::SPALTEN));
    while (!z.empty() && z.back() == ' ') z.pop_back();
    return z;
}

/// Ziel erreicht: Cursorzeile `>` oder `%` (Monitor/UDOS) oder „Press RETURN" irgendwo im Bild.
bool promptZeile(const P8000Machine& m, const std::string& bild) {
    using T = k1520::p8000::Terminal;
    const std::string c = zeileAus(bild, std::min(m.terminal().zeile(), T::ZEILEN - 1));
    if (c == ">" || c == "%") return true;
    return bild.find("Press RETURN") != std::string::npos;
}

}  // namespace

int bootTraceP8000(const K8915TraceOpts& o, const prnlst::Listing& prn, bool karte16,
                   const std::string& wdc, const std::string& hd)
{
    using T = k1520::p8000::Terminal;
    P8000Machine::Config cfg;
    cfg.karte16 = karte16;
    if (!wdc.empty() || !hd.empty()) {
        if (!karte16) { fprintf(stderr, "ERROR: --wdc/--hd nur mit --machine p8000-16\n"); return 2; }
        using W = P8000Machine::Config::Wdc;
        const std::string w = wdc.empty() ? "4.2" : wdc;
        if      (w == "4.2")    cfg.wdc = W::V4_2;
        else if (w == "4.0.05") cfg.wdc = W::V4_0_05;
        else if (w == "3.4.05") cfg.wdc = W::V3_4_05;
        else { fprintf(stderr, "ERROR: --wdc %s unbekannt (4.2|4.0.05|3.4.05)\n", w.c_str()); return 2; }
        cfg.platte = hd;
    }
    std::unique_ptr<P8000Machine> mp;
    try { mp = std::make_unique<P8000Machine>(cfg); }
    catch (const std::exception& e) { fprintf(stderr, "ERROR: %s\n", e.what()); return 2; }
    P8000Machine& m = *mp;
    if ((!o.raf.empty() && o.raf != "none") || o.ptape)
        fprintf(stderr, "WARN: --raf/--ptape gibt es am P8000 nicht (kein K1520-Steckplatz) — ignoriert\n");
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

    bool mounted = false;
    if (!o.disk.empty()) {
        mounted = m.mountDisk(o.drive, o.mount_path, m.defaultFormatName(o.drive), o.write_protect);
        if (!mounted)
            fprintf(stderr, "ERROR: Could not mount disk '%s': %s\n", o.disk.c_str(), m.lastError().c_str());
    }
    m.powerOn();
    if (!o.quiet) {
        fprintf(stderr, "=== P8000 Boot Trace (8-Bit-Seite) ===\n");
        fprintf(stderr, "Disk:       %s%s\n", o.disk.empty() ? "(keine)" : o.disk.c_str(),
                mounted ? "" : (o.disk.empty() ? "" : "  [NICHT gemountet]"));
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
    // WDC (AP P13d): je Statuswechsel ein Ereignis; Kommandoblock (FW 30B7–30BF) bei Übernahme,
    // Fehlerbyte (30C7) bei Status 7.
    long wdc_kmd = 0, wdc_fehler = 0;
    if (P8000Wdc* w = m.wdc()) {
        w->statusBeobachter = [&, w](uint8_t alt, uint8_t neu) {
            char b[96];
            if ((alt & 7) == 1 && (neu & 7) != 1) {
                ++wdc_kmd;
                snprintf(b, sizeof b, "WDC Kommando %02X LW %u  %02X %02X %02X %02X  Laenge %02X%02X",
                         w->lesen(0x30B7), w->lesen(0x30B8), w->lesen(0x30B9), w->lesen(0x30BA), w->lesen(0x30BB),
                         w->lesen(0x30BC), w->lesen(0x30BE), w->lesen(0x30BD));
                event(std::string("wdck") + b, b, m.cpuPC());
            } else if ((neu & 7) == 7) {
                ++wdc_fehler;
                snprintf(b, sizeof b, "WDC Fehler %02X", w->lesen(0x30C7));
                event(b, b, m.cpuPC());
            } else {
                snprintf(b, sizeof b, "WDC Status %d -> %d", alt & 7, neu & 7);
                event(b, b, m.cpuPC());
            }
        };
    }
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
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s%s", isRead ? "IN " : "OUT", p, data, portName(p), zusatz.c_str());
            snprintf(k, sizeof k, "P%c%02X%02X%04X", isRead ? 'r' : 'w', p, data, pc);
        }
        event(k, t, pc);
    });

    // ── Tasten (--keys) ──────────────────────────────────────────────────────
    // `<CR>`/`<ET>` = Return.  Getippt wird blockweise (ein Block endet mit Return) und erst,
    // wenn die Maschine steht (Stillstand) — also am Prompt, nicht mitten im Laden.  Der
    // Terminal-Anschluss liefert die Bytes im Zeichentakt (9600 Bd ≈ 4 600 Takte je Zeichen),
    // deshalb darf der Block auf einmal eingereiht werden; danach 5 000-Takt-Pakete laufen.
    struct Taste { uint32_t code; bool cr; };
    std::vector<Taste> tasten;
    for (size_t i = 0; i < o.keys.size(); ++i) {
        const bool cr = o.keys.compare(i, 4, "<CR>") == 0 || o.keys.compare(i, 4, "<cr>") == 0 ||
                        o.keys.compare(i, 4, "<ET>") == 0 || o.keys.compare(i, 4, "<et>") == 0;
        if (cr) { tasten.push_back({0x01000004u, true}); i += 3; }
        else tasten.push_back({static_cast<uint8_t>(o.keys[i]), false});
    }
    size_t tasten_pos = 0;
    auto laufe = [&](long long t) { for (long long k = 0; k < t && !until_hit; k += 5000) m.run(5000); };
    auto tippeBlock = [&]() {
        long long zeichen = 0;
        while (tasten_pos < tasten.size()) {
            const Taste t = tasten[tasten_pos++];
            m.keyPress(t.code, false, false);
            ++zeichen;
            if (t.cr) break;
        }
        laufe(zeichen * 10'000 + 200'000);
    };

    // ── Lauf ─────────────────────────────────────────────────────────────────
    const int batch = 20000;
    std::string bild = bildText(m), bild_alt = bild;
    bool stillstand = false;
    uint64_t next_progress = 10'000'000;
    while (static_cast<long long>(m.totalCycles()) < o.limit) {
        const int n = m.run(batch);
        if (until_hit) break;
        bild = bildText(m);
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
    const bool prompt = stillstand && promptZeile(m, bild);
    if (!o.quiet) {
        fprintf(stderr, "\n=== P8000 Boot Trace Complete: %llu cycles ===\n", (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Terminalaenderung und ohne Steuerzugriff (PC=%04X)\n",
                    o.stall, m.cpuPC());
        fprintf(stderr, "Prompt:      %s\n", prompt ? "JA" : "nein");
        if (!o.keys.empty())
            fprintf(stderr, "Tasten:      %zu von %zu getippt\n", tasten_pos, tasten.size());
        fprintf(stderr, "Terminal:    Cursor %d/%d, Modus %s, BEL %u\n", m.terminal().zeile(), m.terminal().spalte(),
                m.terminal().modus() == k1520::p8000::TerminalModus::VT100 ? "VT100" : "ADM31", m.terminal().klingel());
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = m.karte8().cpu();
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IM=%d IFF1=%d%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IM, z.IFF1, prnTail(z.PC).c_str());
        if (P8000Karte16* k16 = m.karte16())
            fprintf(stderr, "U8001:       %s, PC=%02X:%04X FCW=%04X, Zeit=%llu\n",
                    k16->inReset() ? "im Reset" : "laeuft", k16->cpu().pcSeg, k16->cpu().pc, k16->cpu().fcw,
                    (unsigned long long)k16->zeit());
        if (P8000Wdc* w = m.wdc())
            fprintf(stderr, "WDC:         %s, PC=%04X Status %d, %ld Kommandos, %ld Fehlermeldungen, Platte %s\n",
                    w->imReset() ? "im Reset" : "laeuft", w->cpu().PC, w->status(), wdc_kmd, wdc_fehler,
                    m.hdPath(0).empty() ? "(keine)" : m.hdPath(0).c_str());

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

        fprintf(stderr, "\nTerminal (tty1):\n");
        for (int r = 0; r < T::ZEILEN; ++r)
            fprintf(stderr, "  |%s|\n", bild.substr(static_cast<size_t>(r * T::SPALTEN), static_cast<size_t>(T::SPALTEN)).c_str());
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
            "{\"machine\":\"p8000\",\"stall\":%s,\"prompt\":%s,\"cycles\":%llu,\"final_pc\":\"0x%04X\","
            "\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            stillstand ? "true" : "false", prompt ? "true" : "false",
            (unsigned long long)cycles, m.cpuPC(),
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
