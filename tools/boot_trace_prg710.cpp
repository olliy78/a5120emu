/**
 * @file boot_trace_prg710.cpp
 * @brief `boot_trace --machine prg710|prg710-1`: Netz-Ein → „NKM-LOADER“ → Laufwerke →
 *        Tastaturabfrage, mit Ereignisprotokoll, PC-Histogramm und Bild als Text
 *        (doc/design/20_prg710.md AP-P1d, Muster tools/boot_trace_k8915.cpp).
 *
 * Protokolliert werden die K5122 (10H–18H), die ZRE-CTC (80H–83H), die
 * Speicherverwaltung (E8H–EBH, mit Seite = A12–A15 der E/A-Adresse und dem
 * entstehenden Speicherbild), die Tastatur (710: 8279 C8H/C9H; 710-1: K8025 A32-B
 * 5EH/5FH und Baudtakt 58H) und Interrupts.  Gleiche aufeinanderfolgende Ereignisse
 * werden gefaltet (×N); bei 14H/16H zählt weder Wert noch PC.
 *
 * Abbruch: `--until`, Stillstand (`--stall` Takte ohne Bildänderung und ohne
 * Schreibzugriff auf einen beobachteten Port) oder `-c`.  Steht die CPU beim
 * Stillstand in der Tastaturabfrage des ROMs (aus der RAM-Kopie, §4a/§4a.1), ist das
 * das Ziel von Etappe 1 → Exit 0.
 *
 * @license MIT
 */
#include "tools/dbg_machine.h"
#include "tools/boot_trace_prg710.h"
#include "tools/coverage_diff.h"
#include "tools/event_bp.h"
#include "tools/z80dis_min.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/k7609/k7609.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

using Quelle = Prg710Speicher::Quelle;

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
        case 0x58: return "K8025 CTC Kanal 0 (Baudtakt A32)";
        case 0x5E: return "K8025 SIO A32-B Daten";
        case 0x5F: return "K8025 SIO A32-B Steuer";
        case 0x80: case 0x81: case 0x82: case 0x83: return "ZRE-CTC";
        case 0xC8: return "8279 Daten";
        case 0xC9: return "8279 Befehl/Status";
        case 0xE8: return "Speicherverwaltung Herkunft";
        case 0xE9: return "Speicherverwaltung E9H [?]";
        case 0xEA: return "Speicherverwaltung physische Seite";
        case 0xEB: return "Speicherverwaltung Freigabe";
        default:   return "";
    }
}
bool beobachtet(uint8_t p, bool v1) {
    if (p >= 0x10 && p <= 0x18) return true;
    if (p >= 0x80 && p <= 0x83) return true;
    if (p >= 0xE8 && p <= 0xEB) return true;
    return v1 ? (p == 0x58 || p == 0x5E || p == 0x5F) : (p == 0xC8 || p == 0xC9);
}
bool datenport(uint8_t p) { return p == 0x14 || p == 0x16; }

/// Speicherbild je 4-KB-Seite: Z = ZRE, V = Seite F mit VRAM, 0–F = physische OPS-Seite, - = leer.
std::string speicherbild(Prg710Machine& m) {
    std::string s;
    for (int n = 0; n < 16; ++n) {
        const uint16_t a = static_cast<uint16_t>((n << 12) | (n == 15 ? 0x800 : 0));
        const auto o = m.speicher().ortVon(a);
        switch (o.quelle) {
            case Quelle::Zre:  s += 'Z'; break;
            case Quelle::Vram: s += 'V'; break;
            case Quelle::Ops:  s += "0123456789ABCDEF"[o.offset >> 12]; break;
            case Quelle::Leer: s += '-'; break;
        }
    }
    return s;
}

std::string bildText(Prg710Machine& m) {
    std::string s; s.reserve(80 * 24);
    for (int r = 0; r < 24; ++r) for (int c = 0; c < 80; ++c) {
        uint8_t ch = static_cast<uint8_t>(m.screenChar(c, r) & 0x7F);
        s += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }
    return s;
}

/// Tastaturabfrage des ROMs in der RAM-Kopie (Seite 0 = OPS): Warteschleife + Abfrageroutine.
/// 710: 007DH–0084H / 0160H–0167H (8279); 710-1: 0070H–0077H / 0163H–016AH (SIO A32-B).
bool inTastaturabfrage(Prg710Machine& m, bool v1, uint16_t pc) {
    if (m.speicher().ortVon(0).quelle != Quelle::Ops) return false;
    if (v1) return (pc >= 0x0070 && pc <= 0x0077) || (pc >= 0x0163 && pc <= 0x016A);
    return (pc >= 0x007D && pc <= 0x0084) || (pc >= 0x0160 && pc <= 0x0167);
}

/// Letzte nicht leere Bildzeile (ohne Randleerzeichen).
std::string letzteZeile(const std::string& bild) {
    for (int r = 23; r >= 0; --r) {
        std::string z = bild.substr(static_cast<size_t>(r) * 80, 80);
        while (!z.empty() && z.back() == ' ') z.pop_back();
        size_t a = z.find_first_not_of(' ');
        if (a != std::string::npos) return z.substr(a);
    }
    return {};
}

}  // namespace

int bootTracePrg710(const K8915TraceOpts& o, bool v1, const prnlst::Listing& prn)
{
    Prg710Machine::Config cfg;
    cfg.variante = v1 ? Prg710Machine::Config::Variante::Prg710_1
                      : Prg710Machine::Config::Variante::Prg710;
    Prg710Machine m(cfg);
    { std::string err;
      if (!dbgm::steckeRaf(m, o.raf, err)) { fprintf(stderr, "--raf: %s\n", err.c_str()); return 2; } }
    m.powerOn();
    const char* name_m = v1 ? "PRG 710-1" : "PRG 710";
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
        mounted = m.mountDisk(o.drive, o.mount_path, m.defaultFormatName(o.drive), o.write_protect)
               || m.mountDisk(o.drive, o.mount_path, "udos_ds77", o.write_protect);
        if (!mounted)
            fprintf(stderr, "ERROR: Could not mount disk '%s': %s\n", o.disk.c_str(), m.lastError().c_str());
    }
    if (!o.quiet) {
        fprintf(stderr, "=== %s Boot Trace ===\n", name_m);
        fprintf(stderr, "Disk:       %s%s\n", o.disk.empty() ? "(keine)" : o.disk.c_str(),
                mounted ? "" : (o.disk.empty() ? "" : "  [NICHT gemountet]"));
        fprintf(stderr, "Max cycles: %lld   Stillstand nach %lld\n", o.limit, o.stall);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "Until:      %s\n", o.until.text.c_str());
        fprintf(stderr, "\n");
    }

    // ── Ereignisprotokoll mit Faltung (wie K8915) ────────────────────────────
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
    // Schlüssel = PC | 10000H, wenn der Befehl aus der ZRE (ROM/ZRE-RAM) kam: 0160H im ROM
    // und 0160H in der RAM-Kopie sind zwar dieselben Bytes, aber verschiedene Phasen.
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
    uint16_t insn_pc = 0;

    m.setCpuTraceCallback([&](const Z80& z) {
        ++instr; insn_pc = z.PC;
        {
            const bool zre = m.speicher().ortVon(z.PC).quelle == Quelle::Zre;
            const uint32_t key = z.PC | (zre ? 0x10000u : 0u);
            if (hist[key]++ == 0) hist_note[key] = name(z.PC) + prnTail(z.PC);
        }
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
                   : o.until.word ? (rd(o.until.addr) | (rd(static_cast<uint16_t>(o.until.addr + 1)) << 8))
                                  : rd(o.until.addr);
            if (!until_hit && o.until.compare(v)) {
                until_hit = true; until_cyc = m.totalCycles(); until_pc = z.PC; m.stop();
            }
        }
    });
    // Speicherzugriffe der K2521 gehen nicht über den Systembus (Speicherweg =
    // Speicherverwaltung) — Prg710Machine::setBusTrace meldet sie zusätzlich (--watch).
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
        bool gewuenscht = beobachtet(p, v1);
        for (uint16_t w : o.watchio) if ((w & 0xFF) == p) gewuenscht = true;
        if (!gewuenscht) return;
        if (!isRead) last_activity = m.totalCycles();   // Schreiben = kein Stillstand
        char t[200], k[24];
        if (datenport(p)) {
            snprintf(t, sizeof t, "%s (%02XH)  %s", isRead ? "IN " : "OUT", p, portName(p));
            snprintf(k, sizeof k, "D%c%02X", isRead ? 'r' : 'w', p);
        } else {
            std::string zusatz;
            if ((p & 0xFC) == 0xE8) {
                char s[64];
                // Der Busbeobachter bekommt nur A0–A7; die Seite steht in A12–A15 (ioAddress).
                if (p == 0xE8 || p == 0xEA) snprintf(s, sizeof s, " [Seite %X]", m.bus().ioAddress() >> 12);
                else s[0] = 0;
                zusatz = s;
                if (!isRead) zusatz += "  map=" + speicherbild(m);
            }
            snprintf(t, sizeof t, "%s (%02XH)=%02X  %s%s", isRead ? "IN " : "OUT", p, data, portName(p), zusatz.c_str());
            snprintf(k, sizeof k, "P%c%04X%02X%04X", isRead ? 'r' : 'w', m.bus().ioAddress(), data, pc);
        }
        event(k, t, pc);
    });

    // ── Tasten (--keys) ──────────────────────────────────────────────────────
    // `<ET>` = ET1 (710: QK_TASTE_BASE|37H an der K7609) bzw. Return (710-1: K7672 → 0DH).
    // Getippt wird blockweise (ein Block endet mit `<ET>`) und immer erst, wenn die Maschine
    // steht (Stillstand: --stall Takte ohne Bildänderung/Steuerzugriff) — also am Start-
    // taster-Warten des ROMs, an der Datumsabfrage von UDOS usw.
    struct Taste { uint32_t code; bool et; };
    std::vector<Taste> tasten;
    for (size_t i = 0; i < o.keys.size(); ++i) {
        if (o.keys.compare(i, 4, "<ET>") == 0 || o.keys.compare(i, 4, "<et>") == 0) {
            tasten.push_back({v1 ? 0x01000004u : (K7609::QK_TASTE_BASE | 0x37u), true});
            i += 3;
        } else tasten.push_back({static_cast<uint8_t>(o.keys[i]), false});
    }
    size_t tasten_pos = 0;
    // Nächster Block; false = Maschine während des Tippens angehalten (--until).
    auto tippeBlock = [&]() {
        while (tasten_pos < tasten.size()) {
            const Taste t = tasten[tasten_pos++];
            m.keyPress(t.code, false, false);   m.run(100000);
            m.keyRelease(t.code);               m.run(100000);
            if (until_hit) return;
            if (t.et) break;
        }
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
            fprintf(stderr, "[PROGRESS] cycles=%llu PC=%04X map=%s instr=%llu\n",
                    (unsigned long long)now, m.cpuPC(), speicherbild(m).c_str(), (unsigned long long)instr);
            next_progress = now + 10'000'000;
        }
        if (n == 0) break;
    }
    flush();
    if (ev != stderr) fclose(ev);
    if (csv) { fclose(csv); fprintf(stderr, "--csv: %ld row(s) → %s\n", csv_rows, o.csv_path.c_str()); }
    if (itr) { fclose(itr); fprintf(stderr, "--itrace: %ld INT/NMI → %s\n", itr_n, o.itrace_path.c_str()); }

    const uint64_t cycles = m.totalCycles();
    const std::string prompt_zeile = letzteZeile(bild);
    const bool prompt = stillstand && prompt_zeile == "%";   // UDOS-Prompt, Bild steht
    const bool taste = stillstand && inTastaturabfrage(m, v1, m.cpuPC());
    if (!o.quiet) {
        fprintf(stderr, "\n=== %s Boot Trace Complete: %llu cycles ===\n", name_m, (unsigned long long)cycles);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "--until (%s): %s", o.until.text.c_str(), until_hit ? "MET" : "not met\n");
        if (until_hit) fprintf(stderr, " at cycle %llu (PC=0x%04X)\n", (unsigned long long)until_cyc, until_pc);
        if (stillstand)
            fprintf(stderr, "Stillstand:  JA — %lld Takte ohne Bildaenderung und ohne Steuerzugriff (PC=%04X)\n",
                    o.stall, m.cpuPC());
        fprintf(stderr, "Prompt:      %s\n", prompt ? "JA (\"%\")" : "nein");
        if (!o.keys.empty())
            fprintf(stderr, "Tasten:      %zu von %zu getippt\n", tasten_pos, tasten.size());
        fprintf(stderr, "Tastatur:    %s\n", taste ? (v1 ? "ROM wartet in der SIO-Abfrage (0163H, 5FH)"
                                                         : "ROM wartet in der 8279-Abfrage (0160H, C9H)")
                                                    : "nein");
        fprintf(stderr, "Speicher:    EBH=%02X map=%s  E8H[0]=%02X E8H[F]=%02X\n", m.speicher().freigabe(),
                speicherbild(m).c_str(), m.speicher().attr(0), m.speicher().attr(15));
        fprintf(stderr, "Ereignisse:  %ld (%ld Zeilen), Interrupts: %ld, Befehle: %llu\n",
                ev_total, ev_lines, ints, (unsigned long long)instr);
        const Z80& z = m.zre().cpu();
        fprintf(stderr, "Final CPU:   PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IY=%04X%s\n",
                z.PC, z.SP, z.AF, z.BC, z.DE, z.HL, z.IY, prnTail(z.PC).c_str());

        fprintf(stderr, "\nI/O-Ports (rd/wr):\n");
        for (int p = 0; p < 256; ++p)
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-9llu wr=%-9llu %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p], portName(static_cast<uint8_t>(p)));

        std::vector<std::pair<uint32_t, uint32_t>> hs;
        for (auto& kv : hist) hs.push_back({kv.second, kv.first});
        std::sort(hs.rbegin(), hs.rend());
        fprintf(stderr, "\nPC-Histogramm (top 30, %llu Befehle; ZRE = aus ROM/ZRE-RAM, sonst OPS):\n",
                (unsigned long long)instr);
        for (size_t i = 0; i < hs.size() && i < 30; ++i)
            fprintf(stderr, "  0x%04X%s : %9u  (%5.2f%%)%s\n", hs[i].second & 0xFFFF,
                    (hs[i].second & 0x10000) ? " ZRE" : "    ", hs[i].first,
                    100.0 * hs[i].first / static_cast<double>(instr ? instr : 1), hist_note[hs[i].second].c_str());

        fprintf(stderr, "\nBild (K7024):\n");
        for (int r = 0; r < 24; ++r) fprintf(stderr, "  |%s|\n", bild.substr(r * 80, 80).c_str());
    }
    if (o.coverage) {
        std::vector<bool> cov(0x10000, false);
        for (auto& kv : hist) {
            const uint16_t pc = static_cast<uint16_t>(kv.first);
            int len = z80dis::decode(rd, pc).len;
            for (int b = 0; b < len; ++b) cov[static_cast<uint16_t>(pc + b)] = true;
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
                std::vector<std::pair<uint32_t, uint32_t>> v(hist.begin(), hist.end());
                std::sort(v.begin(), v.end());
                for (auto& kv : v) fprintf(cf, "%s,0x%04X,%u\n", (kv.first & 0x10000) ? "ZRE" : "CPU",
                                           kv.first & 0xFFFF, kv.second);
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
            "{\"machine\":\"%s\",\"keywait\":%s,\"stall\":%s,\"prompt\":%s,\"cycles\":%llu,\"final_pc\":\"0x%04X\","
            "\"map\":\"%s\",\"instr\":%llu,\"events\":%ld,\"ints\":%ld,",
            v1 ? "prg710-1" : "prg710", taste ? "true" : "false", stillstand ? "true" : "false", prompt ? "true" : "false",
            (unsigned long long)cycles, m.cpuPC(), speicherbild(m).c_str(), (unsigned long long)instr,
            ev_total, ints);
        if (o.until.kind != untilcond::UntilCond::NONE)
            fprintf(stderr, "\"until\":{\"set\":true,\"met\":%s,\"cycle\":%llu,\"pc\":\"0x%04X\"}}\n",
                    until_hit ? "true" : "false", (unsigned long long)until_cyc, until_pc);
        else
            fprintf(stderr, "\"until\":{\"set\":false}}\n");
    }
    if (o.until.kind != untilcond::UntilCond::NONE) return until_hit ? 0 : 2;
    return (taste || prompt) ? 0 : 1;
}
