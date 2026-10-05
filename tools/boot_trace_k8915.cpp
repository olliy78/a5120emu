/**
 * @file boot_trace_k8915.cpp
 * @brief `boot_trace --machine k8915`: Netz-Ein → Selbsttest → Lader → SCPX bis `A>`,
 *        mit Ereignisprotokoll, PC-Histogramm und Abbruch bei Prompt/Stillstand.
 *
 * Was protokolliert wird (§8a AP-E4d, Schritt 3):
 *  - **E/A**: jeder Zugriff auf die K5122 (10H–18H), das Anzeigefeld 61H und das
 *    Speicherregister A8H.  Gleiche aufeinanderfolgende Ereignisse werden gefaltet
 *    („×N“); bei den Datenports 14H/16H zählt dabei weder Wert noch PC — der Lese-ISR
 *    E965H liest ausgerollt von vielen Adressen, sonst gäbe jedes Byte eine Zeile.
 *  - **Interrupts** mit Vektor, Quellbaustein und ISR-Adresse.
 *  - A8H mit dem entstehenden Speicherbild (`map=R...1111…`), 61H mit den Lampen.
 *
 * Abbruch: stabiler Prompt (letzte Bildzeile `X>` und das Bild 2 Mio. Takte unverändert
 * — der Autostart `rade` tippt sonst schon, während `A>` kurz letzte Zeile ist),
 * `--until`, Stillstand (`--stall` Takte ohne Bildänderung und ohne Schreibzugriff
 * auf 10H–13H/18H/61H/A8H) oder `-c`.
 *
 * @license MIT
 */
#include "tools/boot_trace_k8915.h"
#include "tools/dbg_machine.h"
#include "tools/event_bp.h"
#include "tools/coverage_diff.h"
#include "tools/z80dis_min.h"
#include "core/machines/k8915/k8915.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unordered_map>

namespace {

const char* portName(uint8_t p) {
    switch (p) {
        case 0x10: return "K5122 Steuer-PIO A Daten";
        case 0x11: return "K5122 Steuer-PIO A Steuer";
        case 0x12: return "K5122 Steuer-PIO B Daten";
        case 0x13: return "K5122 Steuer-PIO B Steuer";
        case 0x14: return "K5122 Daten schreiben";
        case 0x15: return "K5122 Daten-PIO A Steuer";
        case 0x16: return "K5122 Daten lesen";
        case 0x17: return "K5122 Daten-PIO B Steuer";
        case 0x18: return "K5122 Laufwerkswahl (8212)";
        case 0x61: return "Anzeigefeld";
        case 0xA8: return "ZRE Speicherregister";
        default:   return "";
    }
}
bool beobachtet(uint8_t p) { return (p >= 0x10 && p <= 0x18) || p == 0x61 || (p >= 0xA8 && p <= 0xAB); }
bool datenport(uint8_t p) { return p == 0x14 || p == 0x16; }

std::string bildText(K8915Machine& m) {
    std::string s; s.reserve(80 * 24);
    for (int r = 0; r < 24; ++r) for (int c = 0; c < 80; ++c) {
        uint8_t ch = (uint8_t)(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ';
    }
    return s;
}
/// Letzte nicht leere Bildzeile, rechts ohne Leerzeichen.
std::string letzteZeile(const std::string& bild) {
    std::string letzte;
    for (size_t z = 0; z + 80 <= bild.size(); z += 80) {
        std::string zeile = bild.substr(z, 80);
        while (!zeile.empty() && zeile.back() == ' ') zeile.pop_back();
        if (!zeile.empty()) letzte = zeile;
    }
    return letzte;
}

}  // namespace

int bootTraceK8915(const K8915TraceOpts& o, const prnlst::Listing& prn)
{
    K8915Machine::Config cfg;
    if (o.gen2) cfg.generation = K8915Machine::Generation::Gen2;
    K8915Machine m(cfg);
    { std::string err;
      if (!dbgm::steckeRaf(m, o.raf, err)) { fprintf(stderr, "--raf: %s\n", err.c_str()); return 2; }
      if (!dbgm::steckeK6022(m, o.ptape, err)) { fprintf(stderr, "--ptape: %s\n", err.c_str()); return 2; } }
    m.powerOn();
    auto rd = [&](uint16_t a) { return m.memReadDebug(a); };
    // Annotation nur, solange die Bytes der Listingzeile gerade dort stehen (ROM vs. RAM).
    auto prnTail = [&](uint16_t a) -> std::string {
        const std::string* s = prn.find(a);
        if (!s || !prn.matches(a, rd)) return std::string();
        return "  ; " + *s;
    };
    auto name = [&](uint16_t a) -> std::string {
        std::string l = prn.labelNear(a);
        return l.empty() ? l : " <" + l + ">";
    };

    if (o.skip_selftest) {
        m.zre().bankPoke(0, 0x0000, 0xC3);
        m.zre().bankPoke(0, 0x0005, 0xC3);
    }
    bool mounted = false;
    if (!o.disk.empty()) {
        mounted = m.mountDisk(o.drive, o.mount_path, "cpa800", o.write_protect)
               || m.mountDisk(o.drive, o.mount_path, "cpa780", o.write_protect);
        if (!mounted)
            fprintf(stderr, "ERROR: Could not mount disk '%s': %s\n", o.disk.c_str(), m.lastError().c_str());
    }
    if (!o.quiet) {
        fprintf(stderr, "=== %s Boot Trace ===\n", o.gen2 ? "K8915 Gen 2" : "K8915");
        fprintf(stderr, "Disk:       %s%s\n", o.disk.empty() ? "(keine)" : o.disk.c_str(),
                mounted ? "" : (o.disk.empty() ? "" : "  [NICHT gemountet]"));
        fprintf(stderr, "Max cycles: %lld   Stillstand nach %lld   Selbsttest: %s   CR nach Coldstart: %s\n",
                o.limit, o.stall, o.skip_selftest ? "uebersprungen" : "ja", o.auto_cr ? "ja" : "nein");
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "Until:      %s\n", o.until.text.c_str());
        if (mounted) { const std::string h = m.diskNotice(o.drive); if (!h.empty()) fprintf(stderr, "  ! %s\n", h.c_str()); }
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
    // Die Annotation wird beim ENTSTEHEN festgehalten: ausgegeben wird erst beim nächsten
    // anderen Ereignis, und bis dahin hat ein OUT (A8H) das Speicherbild womöglich umgelegt.
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
    // Schlüssel = PC | 10000H, wenn der Befehl aus dem Boot-ROM kam: 0100H im ROM und
    // 0100H im TPA sind verschiedener Code.  Die Annotation (Listingzeile + nächstes Label)
    // wird beim ersten Auftreten festgehalten — am Ende steht das ROM oft nicht mehr da.
    std::unordered_map<uint32_t, uint32_t> hist;
    std::unordered_map<uint32_t, std::string> hist_note;
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
    uint16_t insn_pc = 0;   // PC des laufenden Befehls (im Busbeobachter steht der PC schon dahinter)

    m.setCpuTraceCallback([&](const Z80& z) {
        ++instr; insn_pc = z.PC;
        {
            const bool rom = dbgm::k8RomEin(m, z.PC);
            const uint32_t key = z.PC | (rom ? 0x10000u : 0u);
            if (hist[key]++ == 0) hist_note[key] = name(z.PC) + prnTail(z.PC);
        }
        if (csv && (o.win_lo < 0 || (z.PC >= o.win_lo && z.PC <= o.win_hi)) && csv_rows < 5'000'000) {
            z80dis::Insn d = z80dis::decode(rd, z.PC);
            char bytes[16] = {0};
            for (int i = 0; i < d.len && i < 4; ++i) { char b[3]; snprintf(b, 3, "%02X", rd((uint16_t)(z.PC + i))); strcat(bytes, b); }
            fprintf(csv, "%ld,%llu,CPU,0x%04X,%s,\"%s\",%04X,%04X,%04X,%04X,%04X,%04X,%04X\n",
                    csv_rows, (unsigned long long)z.cycles, z.PC, bytes, d.text,
                    z.AF, z.BC, z.DE, z.HL, z.IX, z.IY, z.SP);
            ++csv_rows;
        }
        const eventbp::Event e = eventbp::classify(z.PC, z.SP, z.IFF1, prev, true, true, false, 0, 0);
        if (e == eventbp::Event::Interrupt || e == eventbp::Event::NMI) {
            ++ints;
            const uint16_t ret = (uint16_t)(rd(z.SP) | (rd((uint16_t)(z.SP + 1)) << 8));
            const auto& ia = m.lastIntAck();
            const char* dev = e == eventbp::Event::NMI ? "-" : ia.spurious ? "SPURIOUS" : (ia.device ? ia.device : "?");
            char t[160];
            if (e == eventbp::Event::NMI) snprintf(t, sizeof t, "NMI → ISR 0066 (unterbrochen bei %04X)", ret);
            else snprintf(t, sizeof t, "INT Vektor=%02X von %s → ISR %04X%s", ia.vector, dev, z.PC, name(z.PC).c_str());
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
                   : o.until.word ? (rd(o.until.addr) | (rd((uint16_t)(o.until.addr + 1)) << 8))
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
        const uint8_t p = (uint8_t)addr;
        (isRead ? io_rd : io_wr)[p]++;
        bool gewuenscht = beobachtet(p);
        for (uint16_t w : o.watchio) if ((w & 0xFF) == p) gewuenscht = true;
        if (!gewuenscht) return;
        if (!isRead) last_activity = m.totalCycles();   // Schreiben = kein Stillstand
        char t[160], k[24];
        if (datenport(p)) {
            snprintf(t, sizeof t, "%s (%02XH)  %s", isRead ? "IN " : "OUT", p, portName(p));
            snprintf(k, sizeof k, "D%c%02X", isRead ? 'r' : 'w', p);
        } else {
            std::string zusatz;
            if (!isRead && (p & 0xFC) == 0xA8)
                zusatz = "  map=" + dbgm::speicherbild(m) + (dbgm::k8Memdi(m) ? " /MEMDI" : "");
            if (!isRead && p == 0x61) zusatz = "  Lampen: " + dbgm::lampen61(data);
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s%s", isRead ? "IN " : "OUT", p, data, portName(p), zusatz.c_str());
            snprintf(k, sizeof k, "P%c%02X%02X%04X", isRead ? 'r' : 'w', p, data, pc);
        }
        event(k, t, pc);
    });

    // ── Lauf ─────────────────────────────────────────────────────────────────
    const int batch = 20000;
    std::string bild = bildText(m), bild_alt = bild;
    uint64_t bild_seit = 0;             // Takt der letzten Bildänderung
    bool cr_gesendet = false, fehler_cr = false, prompt = false, stillstand = false;
    std::string prompt_zeile;
    uint64_t next_progress = 10'000'000;
    while ((long long)m.totalCycles() < o.limit) {
        const int n = m.run(batch);
        if (until_hit) break;
        bild = bildText(m);
        const uint64_t now = m.totalCycles();
        if (bild != bild_alt) { bild_alt = bild; bild_seit = now; last_activity = now; }
        if (o.until.kind == untilcond::UntilCond::SCREEN && o.until.screenMatch(bild)) {
            until_hit = true; until_cyc = now; until_pc = m.cpuPC(); break;
        }
        // Gen 2: ein Selbsttestfehler (Buchstabe in 1776H unter dem Testnamen bei 1770H, 61H = ERROR-
        // Lampe) wartet mit 16 × BEL auf `CR` (doc/k8915g2/zre_rom.md §4) — wie ein Bediener tippen.
        if (o.gen2 && o.auto_cr && !fehler_cr && m.bellCount() >= 16 && !(m.panelLamps() & 0x80)) {
            const char f = bild[23 * 80 + 70];
            if (f >= 'A' && f <= 'Z') {
                const std::string tn = bild.substr(23 * 80 + 64, 3);
                m.keyboard().sendeZeichen(0x0D); fehler_cr = true;
                if (!o.quiet) fprintf(stderr, "  [c%llu] Selbsttestfehler %s %c → CR getippt\n",
                                      (unsigned long long)now, tn.c_str(), f);
            }
        }
        if (o.auto_cr && !cr_gesendet && bild.find("* Coldstart *") != std::string::npos) {
            m.keyboard().sendeZeichen(0x0D); cr_gesendet = true;
            if (!o.quiet) fprintf(stderr, "  [c%llu] Coldstart-Meldung → CR getippt\n", (unsigned long long)now);
        }
        const std::string lz = letzteZeile(bild);
        static const std::regex prompt_re("^[A-P]>$");
        if (o.until.kind == untilcond::UntilCond::NONE && std::regex_match(lz, prompt_re)
            && now - bild_seit >= 2'000'000) { prompt = true; prompt_zeile = lz; break; }
        if (now - last_activity >= (uint64_t)o.stall) { stillstand = true; break; }
        if (!o.quiet && now >= next_progress) {
            fprintf(stderr, "[PROGRESS] cycles=%llu PC=%04X A8H=%02X 61H=%02X instr=%llu\n",
                    (unsigned long long)now, m.cpuPC(), dbgm::k8A8(m), m.ats().anzeige(),
                    (unsigned long long)instr);
            next_progress = now + 10'000'000;
        }
        if (n == 0) break;
    }
    flush();
    if (ev != stderr) fclose(ev);
    if (csv) { fclose(csv); fprintf(stderr, "--csv: %ld row(s) → %s\n", csv_rows, o.csv_path.c_str()); }
    if (itr) { fclose(itr); fprintf(stderr, "--itrace: %ld INT/NMI → %s\n", itr_n, o.itrace_path.c_str()); }

    const uint64_t cycles = m.totalCycles();
    if (!o.quiet) {
        fprintf(stderr, "\n=== K8915 Boot Trace Complete: %llu cycles ===\n", (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        fprintf(stderr, "Prompt:      %s\n", prompt ? ("JA (\"" + prompt_zeile + "\")").c_str() : "nein");
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Bildaenderung und ohne Steuerzugriff "
                            "(PC=%04X)\n", o.stall, m.cpuPC());
        fprintf(stderr, "Speicher:    A8H=%02X map=%s /MEMDI=%s   Anzeigefeld 61H=%02X (%s)\n",
                dbgm::k8A8(m), dbgm::speicherbild(m).c_str(), dbgm::k8Memdi(m) ? "1" : "0",
                m.ats().anzeige(), dbgm::lampen61(m.ats().anzeige()).c_str());
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = dbgm::k8Cpu(m);
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, prnTail(z.PC).c_str());

        fprintf(stderr, "\nI/O-Ports (rd/wr):\n");
        for (int p = 0; p < 256; ++p)
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-9llu wr=%-9llu %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p], portName((uint8_t)p));

        std::vector<std::pair<uint32_t, uint32_t>> hs;
        for (auto& kv : hist) hs.push_back({kv.second, kv.first});
        std::sort(hs.rbegin(), hs.rend());
        fprintf(stderr, "\nPC-Histogramm (top 30, %llu Befehle; ROM = aus dem Boot-ROM):\n",
                (unsigned long long)instr);
        for (size_t i = 0; i < hs.size() && i < 30; ++i)
            fprintf(stderr, "  0x%04X%s : %9u  (%5.2f%%)%s\n", hs[i].second & 0xFFFF,
                    (hs[i].second & 0x10000) ? " ROM" : "    ", hs[i].first,
                    100.0 * hs[i].first / (double)(instr ? instr : 1), hist_note[hs[i].second].c_str());

        fprintf(stderr, "\nBild (K7024):\n");
        for (int r = 0; r < 24; ++r) fprintf(stderr, "  |%s|\n", bild.substr(r * 80, 80).c_str());
    }

    if (o.coverage) {
        std::vector<bool> cov(0x10000, false);
        for (auto& kv : hist) {
            const uint16_t pc = (uint16_t)kv.first;
            int len = z80dis::decode(rd, pc).len;
            for (int b = 0; b < len; ++b) cov[(uint16_t)(pc + b)] = true;
        }
        auto ranges = covdiff::collapseRanges(cov);
        fprintf(stderr, "\n=== Code coverage (CPU) ===\n  %zu distinct instr addresses, %zu range(s)\n",
                hist.size(), ranges.size());
        for (size_t i = 0; i < ranges.size() && i < 60; ++i)
            fprintf(stderr, "    0x%04X-0x%04X%s\n", ranges[i].first, ranges[i].second,
                    name((uint16_t)ranges[i].first).c_str());
        if (!o.coverage_path.empty()) {
            if (FILE* cf = fopen(o.coverage_path.c_str(), "w")) {
                fprintf(cf, "cpu,pc,hits\n");
                std::vector<std::pair<uint32_t, uint32_t>> v(hist.begin(), hist.end());
                std::sort(v.begin(), v.end());
                for (auto& kv : v) fprintf(cf, "%s,0x%04X,%u\n", (kv.first & 0x10000) ? "ROM" : "CPU",
                                           kv.first & 0xFFFF, kv.second);
                fclose(cf);
                fprintf(stderr, "  CSV written → %s (cpu,pc,hits)\n", o.coverage_path.c_str());
            }
        }
    }
    if (o.dump_lo >= 0 && o.dump_hi > o.dump_lo) {
        if (FILE* df = fopen(o.dump_path.c_str(), "wb")) {
            for (int a = o.dump_lo; a < o.dump_hi; ++a) fputc(rd((uint16_t)a), df);
            fclose(df);
            fprintf(stderr, "\nRAM dump (CPU-Sicht) 0x%04X-0x%04X -> %s\n", o.dump_lo, o.dump_hi, o.dump_path.c_str());
        }
    }
    if (o.json) {
        fprintf(stderr,
            "{\"machine\":\"%s\",\"prompt\":%s,\"stall\":%s,\"cycles\":%llu,\"final_pc\":\"0x%04X\","
            "\"a8\":\"0x%02X\",\"lamps\":\"0x%02X\",\"cpu_addrs\":%zu,\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            o.gen2 ? "k8915-g2" : "k8915", prompt ? "true" : "false", stillstand ? "true" : "false", (unsigned long long)cycles, m.cpuPC(),
            dbgm::k8A8(m), m.ats().anzeige(), hist.size(), (unsigned long long)instr, ev_total, ints);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "\"until\":{\"set\":true,\"met\":%s,\"cycle\":%llu,\"pc\":\"0x%04X\"}}\n",
                    until_hit ? "true" : "false", (unsigned long long)until_cyc, until_pc);
        else
            fprintf(stderr, "\"until\":{\"set\":false}}\n");
    }
    if (o.until.kind != untilcond::UntilCond::NONE) return until_hit ? 0 : 2;
    return prompt ? 0 : 1;
}
