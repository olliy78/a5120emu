/**
 * @file boot_trace_pc1715.cpp
 * @brief `boot_trace --machine pc1715`: Netz-Ein → Urlader S502 → Spur 0 Sektor 1 →
 *        Betriebssystem, mit Ereignisprotokoll, PC-Histogramm und Bild als Text
 *        (doc/design/21_pc1715.md AP-2, Muster tools/boot_trace_prg710.cpp).
 *
 * Protokolliert werden die Floppy-Ansteuerung (00H–07H, SE/MO-Register 20H/21H), ROM
 * ein/aus (24H–2BH), BWS-Register (34H), 8275 (18H/19H) und Interrupts.  Gleiche
 * aufeinanderfolgende Ereignisse werden gefaltet (×N); beim Datenport (00H/02H) zählt
 * weder Wert noch PC.
 *
 * Abbruch: `--until`, Stillstand (`--stall` Takte ohne Bildänderung und ohne
 * Schreibzugriff auf einen beobachteten Port) oder `-c`.  Steht beim Stillstand eine
 * Bildzeile `A>` (bzw. `%`), ist das das Ziel von Etappe 2 → Exit 0.
 *
 * @license MIT
 */
#include "tools/boot_trace_pc1715.h"
#include "tools/coverage_diff.h"
#include "tools/event_bp.h"
#include "tools/z80dis_min.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/primitives/upd765.h"
#include "core/primitives/z80_dma.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

const char* portName(uint8_t p) {
    switch (p) {
        case 0x00: return "FD Daten-PIO A Daten (Schreiben)";
        case 0x01: return "FD Daten-PIO A Steuer";
        case 0x02: return "FD Daten-PIO B Daten (Lesen)";
        case 0x03: return "FD Daten-PIO B Steuer";
        case 0x04: return "FD Steuer-PIO A Daten";
        case 0x05: return "FD Steuer-PIO A Steuer";
        case 0x06: return "FD Steuer-PIO B Daten";
        case 0x07: return "FD Steuer-PIO B Steuer";
        case 0x08: case 0x09: case 0x0A: case 0x0B: return "CTC0";
        case 0x0C: return "SIO A Daten (Tastatur/Drucker)";
        case 0x0D: return "SIO B Daten (V.24)";
        case 0x0E: return "SIO A Steuer";
        case 0x0F: return "SIO B Steuer";
        case 0x18: case 0x1A: return "8275 Parameter";
        case 0x19: case 0x1B: return "8275 Befehl/Status";
        case 0x20: case 0x22: return "FD SE-Register (/SE, /LCK)";
        case 0x21: case 0x23: return "FD MO-Register (DB4-7 /MO)";
        case 0x24: case 0x25: case 0x26: case 0x27: return "ROM ein";
        case 0x28: case 0x29: case 0x2A: case 0x2B: return "ROM aus";
        case 0x34: case 0x35: case 0x36: case 0x37: return "BWS-Register";
        default:   return "";
    }
}
/// PC 1715W (doc/pc1715/pc1715w_hardware.md §1): andere Belegung derselben Adressen.
const char* portNameW(uint8_t p) {
    if (p <= 0x03) return "UA858 DMA";
    if (p <= 0x07) return "CTC2";
    if (p <= 0x0B) return "CTC0";
    if (p == 0x0C || p == 0x0D) return p == 0x0C ? "SIO A Daten" : "SIO B Daten";
    if (p == 0x0E || p == 0x0F) return p == 0x0E ? "SIO A Steuer" : "SIO B Steuer";
    switch (p) {
        case 0x18: return "8275 Parameter";
        case 0x19: return "8275 Befehl/Status";
        case 0x1A: return "ZG-Wahl (Bit 4)";
        case 0x1B: return "/ZRES";
        case 0x1C: case 0x1E: return "U8272 MSR";
        case 0x1D: case 0x1F: return "U8272 Daten";
        default: break;
    }
    if (p >= 0x20 && p <= 0x23) return "KRFD (Bit 6 FDC-Reset, Bit 7 TC)";
    if (p >= 0x24 && p <= 0x27) return "BR (Lese-/Schreibbank)";
    if (p >= 0x28 && p <= 0x2B) return "MOS (Motor Bit 4+n)";
    if (p >= 0x34 && p <= 0x37) return "KON (DIP S8)";
    return "";
}
/// Ereignis-Ports.  Am 1715W schaltet das BIOS das Bankregister 24H bei JEDEM Aufruf (auch im
/// Leerlauf der Konsolenabfrage) — es zählt deshalb nicht als Steuerzugriff.
bool beobachtetW(uint8_t p) {
    return p <= 0x07 || (p >= 0x18 && p <= 0x23) || (p >= 0x28 && p <= 0x2B) || (p >= 0x34 && p <= 0x37);
}
bool beobachtet(uint8_t p) {
    return p <= 0x07 || (p >= 0x18 && p <= 0x2B) || (p >= 0x34 && p <= 0x37);
}
bool datenport(uint8_t p) { return p == 0x00 || p == 0x02; }

std::string bildText(Pc1715Machine& m, int cols, int rows) {
    std::string s; s.reserve(static_cast<size_t>(cols * rows));
    for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
        uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    return s;
}

bool promptZeile(const std::string& bild, int cols, int rows) {
    for (int r = 0; r < rows; ++r) {
        std::string z = bild.substr(static_cast<size_t>(r * cols), static_cast<size_t>(cols));
        while (!z.empty() && z.back() == ' ') z.pop_back();
        if (z.size() == 2 && z[1] == '>' && z[0] >= 'A' && z[0] <= 'P') return true;
        if (z == "%") return true;
    }
    return false;
}

}  // namespace

int bootTracePc1715(const K8915TraceOpts& o, const prnlst::Listing& prn, bool w)
{
    Pc1715Machine::Config cfg;
    if (w) cfg.variante = Pc1715Machine::Config::Variante::Pc1715W;
    Pc1715Machine m(cfg);
    const int cols = m.zre().textCols(), rows = m.zre().maxZeilen();   // + CP/A-Statuszeile
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
        fprintf(stderr, w ? "=== PC 1715W Boot Trace ===\n" : "=== PC 1715 Boot Trace ===\n");
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
        bool gewuenscht = w ? beobachtetW(p) : beobachtet(p);
        for (uint16_t w : o.watchio) if ((w & 0xFF) == p) gewuenscht = true;
        if (!gewuenscht) return;
        if (!isRead) last_activity = m.totalCycles();
        char t[200], k[24];
        if (datenport(p)) {
            snprintf(t, sizeof t, "%s (%02XH)  %s", isRead ? "IN " : "OUT", p, (w ? portNameW(p) : portName(p)));
            snprintf(k, sizeof k, "D%c%02X", isRead ? 'r' : 'w', p);
        } else {
            std::string zusatz;
            if (!isRead && p >= 0x34 && p <= 0x37) {   // BWS-Register: Basis = Wert << 10 (DB0 maskiert), DB6 = ZG2
                char z[48]; snprintf(z, sizeof z, "  Basis %04X ZG%d", (data << 10) & 0xFC00, (data & 0x40) ? 2 : 1);
                zusatz = z;
            } else if (!isRead && p >= 0x24 && p <= 0x2B) zusatz = p < 0x28 ? "  (Overlay an)" : "  (Overlay aus)";
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s%s", isRead ? "IN " : "OUT", p, data, (w ? portNameW(p) : portName(p)), zusatz.c_str());
            snprintf(k, sizeof k, "P%c%02X%02X%04X", isRead ? 'r' : 'w', p, data, pc);
        }
        event(k, t, pc);
    });

    // ── Tasten (--keys) ──────────────────────────────────────────────────────
    // `<ET>` = Return (ET-Taste).  Getippt wird blockweise (ein Block endet mit `<ET>`) und
    // erst, wenn die Maschine steht (Stillstand) — also am Prompt, nicht mitten im Laden.
    // Die Tastatur-CPU braucht Entprellung: 5 000-Takt-Pakete, 150 000 halten, 100 000 Pause.
    struct Taste { uint32_t code; bool et; };
    std::vector<Taste> tasten;
    for (size_t i = 0; i < o.keys.size(); ++i) {
        if (o.keys.compare(i, 4, "<ET>") == 0 || o.keys.compare(i, 4, "<et>") == 0) {
            tasten.push_back({0x01000004u, true});
            i += 3;
        } else tasten.push_back({static_cast<uint8_t>(o.keys[i]), false});
    }
    size_t tasten_pos = 0;
    auto laufe = [&](long long t) { for (long long k = 0; k < t && !until_hit; k += 5000) m.run(5000); };
    auto tippeBlock = [&]() {
        while (tasten_pos < tasten.size()) {
            const Taste t = tasten[tasten_pos++];
            m.keyPress(t.code, false, false);  laufe(150000);
            m.keyRelease(t.code);              laufe(100000);
            if (until_hit) return;
            if (t.et) break;
        }
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
        fprintf(stderr, "\n=== PC 1715 Boot Trace Complete: %llu cycles ===\n", (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Bildaenderung und ohne Steuerzugriff (PC=%04X)\n",
                    o.stall, m.cpuPC());
        fprintf(stderr, "Prompt:      %s\n", prompt ? "JA" : "nein");
        if (!o.keys.empty())
            fprintf(stderr, "Tasten:      %zu von %zu getippt\n", tasten_pos, tasten.size());
        if (w)
            fprintf(stderr, "1715W:       BR=%02X  KRFD=%02X  MOS=%02X  U8272-MSR=%02X  DMA %s\n",
                    m.speicherW()->bankRegister(), m.krfdW(), m.mosW(), m.fdcW()->readMsr(),
                    m.dmaW()->enabled() ? "frei" : "gesperrt");
        else
            fprintf(stderr, "ROM:         %s   BWS=%02X (Basis %04X)\n", m.zre().romEin() ? "ein" : "aus",
                    m.zre().bwsRegister(), m.zre().bildBasis());
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = m.zre().cpu();
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IM=%d IFF1=%d%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IM, z.IFF1, prnTail(z.PC).c_str());

        fprintf(stderr, "\nI/O-Ports (rd/wr):\n");
        for (int p = 0; p < 256; ++p)
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-9llu wr=%-9llu %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p], (w ? portNameW(static_cast<uint8_t>(p)) : portName(static_cast<uint8_t>(p))));

        std::vector<std::pair<uint32_t, uint16_t>> hs;
        for (auto& kv : hist) hs.push_back({kv.second, kv.first});
        std::sort(hs.rbegin(), hs.rend());
        fprintf(stderr, "\nPC-Histogramm (top 30, %llu Befehle):\n", (unsigned long long)instr);
        for (size_t i = 0; i < hs.size() && i < 30; ++i)
            fprintf(stderr, "  0x%04X : %9u  (%5.2f%%)%s\n", hs[i].second, hs[i].first,
                    100.0 * hs[i].first / static_cast<double>(instr ? instr : 1), hist_note[hs[i].second].c_str());

        fprintf(stderr, "\nBild (8275):\n");
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
            "\"rom\":%s,\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            w ? "pc1715w" : "pc1715", stillstand ? "true" : "false", prompt ? "true" : "false",
            (unsigned long long)cycles, m.cpuPC(), m.zre().romEin() ? "true" : "false",
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
