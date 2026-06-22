/**
 * boot_trace.cpp – Standalone boot diagnostic tool for A5120 emulator.
 *
 * Compiles against k1520core and logs the full boot sequence.
 *
 * Usage:
 *   ./boot_trace [options] <disk_image>
 *   Options:
 *     -c <cycles>    Max cycles to simulate (default: 5000000)
 *     -s             Single-step trace: print every ZVE1 ROM instruction (PC<0x0400)
 *                    and every ZVE2 DMA instruction
 *     -n <count>     Number of ZVE1 instructions to single-step trace (default: 200)
 *     -v             Verbose: log every ZVE2 DMA instruction (not just the first 600)
 *     -L <file>      Route the emulator LOG_* stream to <file> (keeps the trace
 *                    summary readable; the libs are built with LOG_LEVEL=5)
 *     -p <cycles>    Keep tracing for <cycles> after the boot ROM hands off to the
 *                    loaded code (0x0437). Reports I/O-port activity, VRAM writes,
 *                    a loaded-code PC histogram, and the screen as text.
 *
 * Traces BOTH CPUs: ZVE1 (main, sampled at batch boundaries) and ZVE2 (DMA-CPU,
 * every instruction via a trace callback). ZVE2 runs only while /BUSRQ is held,
 * so without the callback the boot-sector DMA is invisible. The tool reports
 * where ZVE2 freezes and whether it reached completion ([03F8]=3).
 */

#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/logger.h"
#include "tools/prn_listing.h"
#include "tools/z80dis_min.h"
#include "tools/coverage_diff.h"
#include "tools/until_cond.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// ─── Milestone detection ──────────────────────────────────────────────────────

struct Milestone {
    uint16_t    pc;
    const char* label;
    bool        once;       // only report on first hit
    bool        hit = false;
};

// ZVE1 (main CPU) milestones — matched against machine.cpuPC().
static std::vector<Milestone> milestones = {
    { 0x0008, "LDI loop – zeroing RAM 0000-07FF",          true  },
    { 0x000E, "XOR A after zeroing – PIO init start",       true  },
    { 0x001C, "OUT BS-PIO A ctrl (Mode 1)",                 true  },
    { 0x00BC, "ROM-to-RAM copy section (JR Z target)",      true  },
    { 0x00C4, "LDIR: ROM[BA..3FF] → RAM[BA..3FF]",         true  },
    { 0x00D4, "OUT BS-PIO B – ROM DISABLE",                 true  },
    { 0x010F, "IN fcpiobd – start of disk seek loop",       false },
    { 0x0135, "ZVE1 WAIT_INDEX_LOOP (await 4 index pulses)", false },
    { 0x0194, "ZVE1 CALL drive-init – releases & starts ZVE2", false },
    { 0x0168, "ZVE1 STROBE_INIT wait loop (poll [03F8])",   false },
    { 0x0174, "ZVE1 CALL 0437h – EXECUTE LOADED BOOT CODE",  true  },
    { 0x027E, "ZVE1 alternate boot path (floppy retries done)", true },
};

// ZVE2 (DMA CPU) milestones — matched against the ZVE2 trace callback PC.
static std::vector<Milestone> zve2_milestones = {
    { 0x0000, "ZVE2 reset entry (PC=0000 → JP 01DD)",       true  },
    { 0x01DD, "ZVE2 STROBE_LOOP entry (boot sector load)",  true  },
    { 0x01FA, "ZVE2 /STR assert → doReadSector",            false },
    { 0x020B, "ZVE2 IDAM compare loop",                     false },
    { 0x0224, "ZVE2 IDAM matched → 128-byte INIR transfer", false },
    { 0x0267, "ZVE2 DONE: OUT(11h,03)+[03F8]=3 (completion)", true  },
};

// ─── ROM disassembly reference table (PC → mnemonic) ─────────────────────────
// Hand-disassembled from ZRE_BOOT_ROM bytes. Used for single-step output.
static const char* romLabel(uint16_t pc) {
    switch (pc) {
        case 0x0000: return "NOP (reset entry)";
        case 0x0001: return "LD BC,0x0800 (zero loop count)";
        case 0x0004: return "LD D,C / LD E,C / LD H,C / LD L,C";
        case 0x0008: return "LDI – zero RAM[DE++] from ROM[0]";
        case 0x000A: return "DEC HL – keep HL=0";
        case 0x000B: return "JP PE,0x0008 – loop while BC≠0";
        case 0x000E: return "XOR A – A=0 after zero loop";
        case 0x000F: return "OUT (0x02),A – reset Q240 (/RES-SPA)";
        case 0x0011: return "LD SP,0x07E0";
        case 0x0014: return "IM 2";
        case 0x0016: return "LD A,0 / LD I,A – I=0";
        case 0x001A: return "LD A,0x7F";
        case 0x001C: return "OUT (0x09),A – BS-PIO A ctrl: Mode 1";
        case 0x001E: return "OUT (0x0B),A – BS-PIO B ctrl: Mode 1";
        case 0x0020: return "LD A,0xFF";
        case 0x0022: return "OUT (0x08),A – BS-PIO A data: 0xFF";
        case 0x0024: return "OUT (0x0A),A – BS-PIO B data: 0xFF (preload latch)";
        case 0x0026: return "LD A,0xB8";
        case 0x0028: return "OUT (0x09),A – BS-PIO A: interrupt vector 0xB8";
        case 0x002A: return "LD A,0xFF";
        case 0x002C: return "OUT (0x09),A – BS-PIO A ctrl: Mode 3";
        case 0x002E: return "LD A,0x7F";
        case 0x0030: return "OUT (0x09),A – BS-PIO A dir mask: 0x7F";
        case 0x0032: return "LD IX,0x0800";
        case 0x0036: return "LD (0x0462),IX";
        case 0x003A: return "LD HL,0x044E";
        case 0x003D: return "LD DE,0x0400";
        case 0x0040: return "LD B,0x3E (loop counter=62)";
        case 0x0042: return "LD C,(IX+0) – read page marker";
        case 0x0045: return "LD A,0xD7";
        case 0x0047: return "OUT (0x09),A – BS-PIO A ctrl (int mask)";
        case 0x0049: return "LD A,0x9F";
        case 0x004B: return "OUT (0x09),A – BS-PIO A: int enable";
        case 0x004D: return "EI";
        case 0x004E: return "LD (IX+0),0xFF – write page marker";
        case 0x0052: return "NOP";
        case 0x0053: return "DI";
        case 0x0054: return "LD A,0x47";
        case 0x0056: return "OUT (0x09),A – BS-PIO A: int disable";
        case 0x0058: return "ADD IX,DE – IX += 0x400";
        case 0x005A: return "DJNZ 0x0042 – page loop";
        case 0x005C: return "LD HL,0x046F";
        case 0x005F: return "LD DE,0x006F";
        case 0x0062: return "LD BC,0x0024";
        case 0x0065: return "LDDR – copy config from 0x046F downward";
        case 0x0067: return "LD HL,0x044E";
        case 0x006A: return "BIT 0,(HL) – check boot flag";
        case 0x006C: return "JR Z,0x00BC – jump if normal boot";
        case 0x006E: return "LD HL,0x0800";
        case 0x0071: return "LD (0x004C),HL";
        case 0x0074: return "LD HL,(0x0462)";
        case 0x0077: return "LD L,0x9D";
        case 0x0079: return "JP (HL) – alternate boot path";
        case 0x007B: return "LD A,0x47 (int disable ctrl)";
        case 0x007D: return "OUT (0x09),A";
        case 0x007F: return "LD A,0xFF";
        case 0x00BC: return "LD HL,0x00BA – ROM copy src";
        case 0x00BF: return "LD D,H / LD E,L – DE=0x00BA";
        case 0x00C1: return "LD BC,0x0346";
        case 0x00C4: return "LDIR – copy ROM[BA..3FF] to RAM";
        case 0x00C6: return "LD A,0xFF";
        case 0x00C8: return "OUT (0x0B),A – BS-PIO B ctrl: Mode 3";
        case 0x00CA: return "OUT (0x13),A – fcpiobc: Mode 3";
        case 0x00CC: return "LD A,0xE2";
        case 0x00CE: return "OUT (0x0B),A – BS-PIO B dir mask: 0xE2";
        case 0x00D0: return "IN A,(0x0A) – read BS-PIO B";
        case 0x00D2: return "AND 0xF6 – clear B0(/LD-ROM) and B3(/WAIT-ZVE2)";
        case 0x00D4: return "OUT (0x0A),A – ROM DISABLE + ZVE2 stall";
        case 0x00D6: return "EI – interrupts enabled after ROM disable";
        case 0x00D7: return "LD A,0xF3";
        case 0x00D9: return "OUT (0x13),A – fcpiobc: floppy PIO B setup";
        // ── ZVE1 disk-seek / index-wait / handshake ──────────────────────────
        case 0x0110: return "IN (0x12) – ctrl PIO B drive status";
        case 0x0128: return "WAIT_INDEX_SETUP: [03F7]=4, enable ctrl PIO A int";
        case 0x0135: return "WAIT_INDEX_LOOP: A=[03F7]; JR Z if 0";
        case 0x0153: return "STORE_03FD: [03FD]=0x87 (NZ-path, no step)";
        case 0x015C: return "SEEK_SETUP: cyl/head/sec/size = 0/0/1/0";
        case 0x0165: return "CALL 0194h – drive init + start ZVE2";
        case 0x0168: return "STROBE_INIT wait: A=[03F8]; JR Z,0168 (poll)";
        case 0x016C: return "DEC A; JR Z,014Bh ([03F8]==1 → timeout)";
        case 0x016F: return "CALL 01B6h – verify boot-sector signature";
        case 0x0174: return "CALL 0437h – jump into loaded boot code";
        case 0x0194: return "drive-init: [03F7]=16, OUT(04h) start ZVE2, /STR";
        case 0x01B6: return "verify RAM[0400]='YS', RAM[0402]='L'";
        // ── Index-pulse ISR (vector 0xBA → 0x01C7) ───────────────────────────
        case 0x01C7: return "ISR: EI;PUSH;DEC [03F7];JR Z assert /STR";
        case 0x01D4: return "ISR_ASSERT_STR: OUT(10h,BD); [03F8]=1 (timeout)";
        // ── ZVE2 STROBE_LOOP (DMA program) ───────────────────────────────────
        case 0x01DD: return "ZVE2 STROBE_LOOP: HL=[03F0] load addr";
        case 0x01E9: return "ZVE2 LOAD_IDAM_REGS: EXX; B'=FE; DE'/HL'=cyl/sec";
        case 0x01F4: return "ZVE2 OUT(10h,BD) /STR high";
        case 0x01FA: return "ZVE2 OUT(10h,B5) /STR low → doReadSector";
        case 0x020B: return "ZVE2 READ_IDAM_NZ: IN(16h); CP B' (0xFE marker)";
        case 0x0224: return "ZVE2 IDAM matched → data transfer setup";
        case 0x023C: return "ZVE2 INI/INIR: 128 data bytes → load addr";
        case 0x0253: return "ZVE2 loop ctrl: CP L' vs [07F2]=4 (sector count)";
        case 0x025A: return "ZVE2 STROBE_LOOP_BODY: [03FD] path byte";
        case 0x0267: return "ZVE2 DONE: OUT(11h,03); [03F8]=3";
        default:     return nullptr;
    }
}

// --until <cond>: Bedingung (Parser + Auswertung) in tools/until_cond.h (unit-getestet).
using untilcond::UntilCond;

// ─── --diff a.csv b.csv: compare two --coverage CSVs (no emulation) ────────────
static int runCoverageDiff(const std::string& pa, const std::string& pb) {
    covdiff::CovMap a, b;
    if (!covdiff::loadCsv(pa, a)) { fprintf(stderr, "cannot open %s\n", pa.c_str()); return 1; }
    if (!covdiff::loadCsv(pb, b)) { fprintf(stderr, "cannot open %s\n", pb.c_str()); return 1; }
    auto d = covdiff::diff(a, b);
    fprintf(stderr, "=== Coverage run-diff:  A=%s   B=%s ===\n", pa.c_str(), pb.c_str());
    for (auto& kv : d) {
        const auto& z = kv.second;
        fprintf(stderr, "  %s: A=%zu addrs, B=%zu addrs  |  only-A=%zu  only-B=%zu  common=%zu  hit-diff=%zu\n",
                kv.first.c_str(), z.a_count, z.b_count, z.only_a.size(), z.only_b.size(), z.common, z.changed.size());
        auto list = [&](const char* tag, const std::vector<uint16_t>& v) {
            if (v.empty()) return;
            fprintf(stderr, "     %s:", tag);
            for (size_t i = 0; i < v.size() && i < 24; ++i) fprintf(stderr, " %04X", v[i]);
            if (v.size() > 24) fprintf(stderr, " … (%zu total)", v.size());
            fprintf(stderr, "\n");
        };
        list("only-A", z.only_a); list("only-B", z.only_b); list("hit-diff", z.changed);
    }
    return 0;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // Standalone run-diff mode: `boot_trace --diff a.csv b.csv` (no machine, no disk).
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--diff") && i + 2 < argc)
            return runCoverageDiff(argv[i + 1], argv[i + 2]);

    const char* disk_path   = nullptr;
    const char* log_path    = nullptr;   // -L: route emulator LOG_* output to a file
    int   total_limit       = 5000000;
    int   mount_drive       = 0;         // --drive N: mount on drive N (lower drives empty)
    bool  single_step       = false;
    int   single_step_count = 200;
    bool  verbose           = false;
    int   post_cycles       = 0;         // -p: cycles to keep tracing after boot reached
    int   dump_start        = -1;        // -d start:end: dump live RAM to file at end
    int   dump_end          = -1;
    const char* dump_path   = "/tmp/ram_dump.bin";
    int   win_start         = -1;        // -w start:end: live ZVE1 step trace in a PC window
    int   win_end           = -1;
    int   win_cap           = 3000;      // -W <n>: max window-trace lines (shared -w/-z)
    int   win_count         = 0;
    int   win2_start        = -1;        // -z start:end: live ZVE2 step trace in a PC window
    int   win2_end          = -1;
    int   win2_count        = 0;
    int      watch_n        = 0;         // --watch a,b,..: log memory writes to these addrs
    uint16_t watch_addr[16] = {0};
    int      watchio_n      = 0;         // --watchio p,..: log OUT to these I/O ports
    uint16_t watchio_port[16] = {0};
    int   trace_seq         = 0;         // global event sequence # (chronological order)
    std::vector<std::string> prn_specs;  // -l <file.prn>[@offset]: annotate traces/histograms
    UntilCond until;                     // --until <cond>: stop the run when it holds
    bool      until_hit = false;         // set by the ZVE1 callback when `until` fires
    int       until_cycle = 0; uint16_t until_pc = 0;
    bool      coverage_on   = false;     // --coverage [file]: executed-code report + CSV export
    const char* coverage_path = nullptr;
    const char* csv_path    = nullptr;   // --csv <file>: per-instruction trace (machine-readable)
    FILE*     csv_fp        = nullptr;
    long      csv_rows = 0, csv_cap = 5000000; bool csv_capped = false;
    const char* save_state_path = nullptr;   // --save-state <file>: persist state at run end
    const char* load_state_path = nullptr;   // --load-state <file>: resume from saved state
    bool      json_summary  = false;         // --json: one machine-readable summary line at the end

    // Runtime log control (new gated logging). Default base = ERROR so a plain
    // run is quiet and fast; raise globally with --log-level or, far better,
    // boost only a PC range / cycle window with --log-pc / --log-cycle.
    using k1520::logging::Level;
    using k1520::logging::Logger;
    Level log_base = Level::ERROR;
    bool  log_base_set = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && i+1 < argc) { total_limit = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--drive") && i+1 < argc) { mount_drive = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--log-level") && i+1 < argc) {
            log_base = Logger::levelFromString(argv[++i], Level::ERROR);
            log_base_set = true;
        }
        else if (!strcmp(argv[i], "--log-pc") && i+1 < argc) {  // --log-pc 0x1F7D:0x2060[:trace]
            unsigned lo = 0, hi = 0; char lvl[16] = "trace";
            sscanf(argv[++i], "%x:%x:%15s", &lo, &hi, lvl);
            Logger::instance().addPCGate(lo, hi, Logger::levelFromString(lvl, Level::TRACE));
        }
        else if (!strcmp(argv[i], "--log-cycle") && i+1 < argc) {  // --log-cycle 40000000:41000000[:debug]
            unsigned long long from = 0, to = 0; char lvl[16] = "trace";
            sscanf(argv[++i], "%llu:%llu:%15s", &from, &to, lvl);
            Logger::instance().addCycleGate(from, to, Logger::levelFromString(lvl, Level::TRACE));
        }
        else if (!strcmp(argv[i], "-s"))            { single_step = true; }
        else if (!strcmp(argv[i], "-n") && i+1 < argc) { single_step_count = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-v"))            { verbose = true; }
        else if (!strcmp(argv[i], "-L") && i+1 < argc) { log_path = argv[++i]; }
        else if (!strcmp(argv[i], "-p") && i+1 < argc) { post_cycles = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-d") && i+1 < argc) {  // -d 0x0400:0x0600 [file]
            sscanf(argv[++i], "%i:%i", &dump_start, &dump_end);
            if (i+1 < argc && argv[i+1][0] != '-') dump_path = argv[++i];
        }
        else if (!strcmp(argv[i], "-w") && i+1 < argc) {  // -w 0x1D00:0x1F80 (ZVE1 window)
            sscanf(argv[++i], "%i:%i", &win_start, &win_end);
        }
        else if (!strcmp(argv[i], "-z") && i+1 < argc) {  // -z 0x1F00:0x1FFF (ZVE2 window)
            sscanf(argv[++i], "%i:%i", &win2_start, &win2_end);
        }
        else if (!strcmp(argv[i], "-W") && i+1 < argc) { win_cap = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-l") && i+1 < argc) { prn_specs.push_back(argv[++i]); }
        else if (!strcmp(argv[i], "--until") && i+1 < argc) {   // --until [03F8]==3 | PC==0x1800
            if (!untilcond::parse(argv[++i], until))
                fprintf(stderr, "WARN: bad --until '%s' (use PC<op>A | [A]<op>V | [A]w<op>V)\n", argv[i]);
        }
        else if (!strcmp(argv[i], "--coverage")) {   // --coverage [file.csv]
            coverage_on = true;
            if (i+1 < argc && argv[i+1][0] != '-') coverage_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--csv") && i+1 < argc) { csv_path = argv[++i]; }
        else if (!strcmp(argv[i], "--save-state") && i+1 < argc) { save_state_path = argv[++i]; }
        else if (!strcmp(argv[i], "--load-state") && i+1 < argc) { load_state_path = argv[++i]; }
        else if (!strcmp(argv[i], "--json")) { json_summary = true; }
        else if (!strcmp(argv[i], "--watch") && i+1 < argc) {   // --watch 0x0000,0x03F8,...
            char* tok = strtok(argv[++i], ",");
            while (tok && watch_n < 16) { watch_addr[watch_n++] = (uint16_t)strtol(tok, nullptr, 0); tok = strtok(nullptr, ","); }
        }
        else if (!strcmp(argv[i], "--watchio") && i+1 < argc) { // --watchio 0x04,0x10
            char* tok = strtok(argv[++i], ",");
            while (tok && watchio_n < 16) { watchio_port[watchio_n++] = (uint16_t)strtol(tok, nullptr, 0); tok = strtok(nullptr, ","); }
        }
        else                                        { disk_path = argv[i]; }
    }
    if (!disk_path) disk_path = "disk_b.img";

    // Route the emulator's LOG_* stream to a file so it doesn't drown the
    // trace tool's own stdout/stderr summary (the libs are built LOG_LEVEL=5).
    if (log_path) {
        if (k1520::logging::Logger::instance().setOutputFile(log_path))
            fprintf(stderr, "Emulator log → %s\n", log_path);
        else
            fprintf(stderr, "WARN: could not open log file '%s'\n", log_path);
    }

    // Apply the runtime base level (default ERROR → quiet). Gates registered
    // above raise the effective level only inside their PC range / cycle window.
    Logger::instance().setBaseLevel(log_base);
    fprintf(stderr, "Log: base=%s%s%s\n",
            Logger::levelName(log_base),
            log_base_set ? "" : " (default)",
            Logger::instance().hasGates() ? "  +gates (--log-pc/--log-cycle)" : "");

    // ── .prn listings: annotate per-instruction traces & PC histograms with the
    //    original commented source. Spec = "PFAD[@OFFSET]" (OFFSET signed, hex
    //    0x../..h or decimal) added to each listing address for relocated code.
    prnlst::Listing prn;
    for (auto& spec : prn_specs) {
        std::string path; long off = 0;
        if (!prnlst::splitSpec(spec, path, off)) { fprintf(stderr, "WARN: bad @offset in '%s'\n", spec.c_str()); continue; }
        int n = prn.load(path, off);
        if (n < 0) fprintf(stderr, "WARN: could not open listing '%s'\n", path.c_str());
        else if (off) fprintf(stderr, "Listing:    %s — %d line(s), offset %+ld (0x%04X)\n", path.c_str(), n, off, (uint16_t)off);
        else fprintf(stderr, "Listing:    %s — %d line(s)\n", path.c_str(), n);
    }
    // Fertiger Annotations-Anhang "  ; <quelle>" für eine (Laufzeit-)Adresse, oder
    // "" wenn keine .prn-Quelle vorliegt. Direkt als printf-"%s" am Zeilenende.
    auto prnTail = [&](uint16_t a) -> std::string {
        const std::string* s = prn.find(a); return s ? ("  ; " + *s) : std::string();
    };

    fprintf(stderr, "=== A5120 Boot Trace ===\n");
    fprintf(stderr, "Disk:       %s\n", disk_path);
    fprintf(stderr, "Max cycles: %d\n", total_limit);
    if (until.kind != UntilCond::NONE)
        fprintf(stderr, "Until:      %s  (runs past boot until this or the cycle limit)\n", until.text.c_str());
    fprintf(stderr, "Step trace: %s", single_step ? "ON" : "OFF");
    if (single_step) fprintf(stderr, " (first %d ROM instructions)", single_step_count);
    fprintf(stderr, "\n\n");

    A5120Machine machine;
    machine.powerOn();

    // Live disassembly of the instruction at `a` (decoded from current memory — exact
    // even for self-modifying loader stages). Used by the -w/-z window traces.
    auto disz = [&](uint16_t a) -> std::string {
        z80dis::Insn d = z80dis::decode([&](uint16_t x){ return machine.memReadDebug(x); }, a);
        return std::string(d.text);
    };

    // --csv: one machine-readable row per executed instruction. Bounded by the -w/-z
    // PC window when set (else all, up to csv_cap); pair with --until to trace-to-here.
    if (csv_path) {
        csv_fp = fopen(csv_path, "w");
        if (!csv_fp) fprintf(stderr, "WARN: cannot write --csv '%s'\n", csv_path);
        else fprintf(csv_fp, "seq,cyc,cpu,pc,bytes,disasm,af,bc,de,hl,ix,iy,sp\n");
    }
    auto csvRow = [&](int cpu, const Z80& z) {
        if (!csv_fp) return;
        if (cpu == 1 && win_start  >= 0 && (z.PC < win_start  || z.PC > win_end))  return;
        if (cpu == 2 && win2_start >= 0 && (z.PC < win2_start || z.PC > win2_end)) return;
        if (csv_rows >= csv_cap) {
            if (!csv_capped) { fprintf(stderr, "[csv] cap %ld rows reached — stopped\n", csv_cap); csv_capped = true; }
            return;
        }
        z80dis::Insn d = z80dis::decode([&](uint16_t x){ return machine.memReadDebug(x); }, z.PC);
        char bytes[16] = {0};
        for (int i = 0; i < d.len && i < 4; ++i) { char b[3]; snprintf(b, 3, "%02X", machine.memReadDebug((uint16_t)(z.PC + i))); strcat(bytes, b); }
        // disasm is quoted (it can contain commas, e.g. "JP NZ,0x1234").
        fprintf(csv_fp, "%d,%llu,ZVE%d,0x%04X,%s,\"%s\",%04X,%04X,%04X,%04X,%04X,%04X,%04X\n",
                trace_seq++, (unsigned long long)z.cycles, cpu, z.PC, bytes, d.text,
                z.AF, z.BC, z.DE, z.HL, z.IX, z.IY, z.SP);
        ++csv_rows;
    };

    // ── ZVE1 (main CPU) per-instruction trace ─────────────────────────────────
    // Both CPUs are followed per instruction so milestone counts and PC
    // histograms are exact. (ZVE1 was previously sampled once per batch, which
    // missed short-lived PCs such as the 0x0194 handshake / 0x0168 wait entry.)
    std::unordered_map<uint16_t, uint32_t> pc_hist;       // ZVE1 PC histogram
    std::unordered_map<uint16_t, uint32_t> zve2_pc_hist;  // ZVE2 PC histogram
    uint64_t sample_count = 0;                            // total ZVE1 instructions
    int      cycles_done  = 0;                            // callbacks read this (approx)
    int      step_count   = 0;
    bool     boot_reached = false;                        // ZVE1 entered loaded code
    int      boot_cycle   = -1;                            // cycle at which boot was reached
    std::unordered_map<uint16_t, uint32_t> milestone_counts;

    // ── Post-boot (loaded-code) tracing ───────────────────────────────────────
    // After the boot ROM hands off to the loaded boot record (0x0437), follow the
    // loaded code: a separate PC histogram, an I/O-port R/W histogram, and VRAM
    // (screen) write tracking — so we can see further sector loads, screen init,
    // and BDOS/CP/M activity that loads @os.com.
    std::unordered_map<uint16_t, uint32_t> post_pc_hist;
    uint64_t io_rd[256] = {0}, io_wr[256] = {0};
    uint64_t vram_writes = 0;
    uint16_t vram_first = 0xFFFF, vram_last = 0x0000;

    machine.setCpuTraceCallback([&](const Z80& z) {
        ++sample_count;
        csvRow(1, z);   // --csv machine-readable per-instruction trace (ZVE1)
        pc_hist[z.PC]++;
        if (boot_reached) post_pc_hist[z.PC]++;

        // --until: stop the run when the condition holds (checked per ZVE1 instr).
        if (until.kind != UntilCond::NONE && !until_hit) {
            long cur = (until.kind == UntilCond::PC) ? z.PC
                     : (until.word ? (machine.memReadDebug(until.addr) |
                                      (machine.memReadDebug((uint16_t)(until.addr+1)) << 8))
                                   :  machine.memReadDebug(until.addr));
            if (until.compare(cur)) {
                until_hit = true; until_cycle = cycles_done; until_pc = z.PC;
                fprintf(stderr, "\n*** --until met at cycle %d (ZVE1 PC=0x%04X): %s ***\n",
                        cycles_done, z.PC, until.text.c_str());
                machine.stop();
            }
        }

        // Boot success: ZVE1 executing loaded code (ROM <0x0400, VRAM >=0xF800).
        if (!boot_reached && z.PC >= 0x0400 && z.PC < 0xF800 && !machine.isRomEnabled()) {
            boot_reached = true;
            boot_cycle   = cycles_done;
            fprintf(stderr, "\n*** BOOT SUCCESS: ZVE1 executing loaded code at PC=0x%04X (~cycle %d) ***\n",
                    z.PC, cycles_done);
        }

        for (auto& m : milestones) {
            if (z.PC != m.pc) continue;
            uint32_t n = ++milestone_counts[m.pc];
            if (!m.hit) {
                m.hit = true;
                fprintf(stderr, "\n[MILESTONE] ~cycle=%d PC=0x%04X  %s\n",
                        cycles_done, z.PC, m.label);
                fprintf(stderr, "            SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
                        z.SP, z.AF, z.BC, z.DE, z.HL);
            } else if (!m.once && (n == 10 || n == 100 || (n % 500) == 0)) {
                fprintf(stderr, "  [milestone] 0x%04X hit %u times (~cycle %d)\n",
                        m.pc, n, cycles_done);
            }
        }

        if (single_step && z.PC < 0x0400 && step_count < single_step_count) {
            const char* label = romLabel(z.PC);
            fprintf(stderr, "  [step %3d] PC=%04X %-16s SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X  %s%s\n",
                    step_count, z.PC, disz(z.PC).c_str(), z.SP, z.AF, z.BC, z.DE, z.HL, label ? label : "",
                    prnTail(z.PC).c_str());
            ++step_count;
        }

        // ── Window step trace (-w): live ZVE1 instructions in a PC range ──────────
        // Logs every executed instruction in [win_start,win_end] (after boot),
        // DISASSEMBLED from live memory (exact even for self-modifying loader stages)
        // + registers + .prn annotation, so the live control flow reads directly.
        if (win_start >= 0 && boot_reached && z.PC >= win_start && z.PC <= win_end
            && win_count < win_cap) {
            fprintf(stderr, "  [#%d w%4d] Z1 PC=%04X %-16s AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X%s\n",
                    trace_seq++, win_count, z.PC, disz(z.PC).c_str(), z.AF, z.BC, z.DE, z.HL, z.SP,
                    prnTail(z.PC).c_str());
            ++win_count;
        }
    });

    // ── ZVE2 (DMA-CPU) per-instruction trace ──────────────────────────────────
    // ZVE2 only runs while /BUSRQ is asserted. This callback follows every ZVE2
    // instruction — the only way to see the DMA program and where it freezes.
    uint64_t zve2_instr     = 0;          // total ZVE2 instructions executed
    int      zve2_log_count = 0;
    int      zve2_log_cap   = (single_step || verbose) ? 100000 : 600;  // -s/-v = log all ZVE2 instr
    bool     zve2_done_seen = false;
    std::unordered_map<uint16_t, uint32_t> zve2_ms_counts;

    machine.setZVE2TraceCallback([&](const Z80& z) {
        ++zve2_instr;
        zve2_pc_hist[z.PC]++;
        csvRow(2, z);   // --csv machine-readable per-instruction trace (ZVE2)

        for (auto& m : zve2_milestones) {
            if (z.PC == m.pc) {
                zve2_ms_counts[m.pc]++;
                if (!m.hit) {
                    m.hit = true;
                    fprintf(stderr, "\n[ZVE2 MILESTONE] ~cycle=%d PC=0x%04X  %s\n",
                            cycles_done, z.PC, m.label);
                    fprintf(stderr, "                 SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
                            z.SP, z.AF, z.BC, z.DE, z.HL);
                }
                if (m.pc == 0x0267) zve2_done_seen = true;
            }
        }

        if (zve2_log_count < zve2_log_cap) {
            const char* label = romLabel(z.PC);
            fprintf(stderr, "    [ZVE2 %5llu] PC=%04X %-16s SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X  %s%s\n",
                    (unsigned long long)zve2_instr, z.PC, disz(z.PC).c_str(), z.SP, z.AF, z.BC, z.DE, z.HL,
                    label ? label : "", prnTail(z.PC).c_str());
            ++zve2_log_count;
        }

        // ── ZVE2 window step trace (-z): live ZVE2 instructions in a PC range ─────
        // The companion to -w; shows whether/when ZVE2 actually runs a given DMA
        // routine (e.g. the 3rd stage's 0x1F7D vs the WBOOT/halt) after a restart.
        if (win2_start >= 0 && boot_reached && z.PC >= win2_start && z.PC <= win2_end
            && win2_count < win_cap) {
            fprintf(stderr, "  [#%d z%4d] Z2 PC=%04X %-16s AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X%s\n",
                    trace_seq++, win2_count, z.PC, disz(z.PC).c_str(), z.AF, z.BC, z.DE, z.HL, z.SP,
                    prnTail(z.PC).c_str());
            ++win2_count;
        }
    });

    // ── Bus trace: I/O-port R/W histogram + VRAM tracking + --watch/--watchio ──
    // Watch lines are emitted on the actual write, in execution order, sharing the
    // global trace_seq with -w/-z so the dual-CPU handshake reads as one timeline.
    machine.setBusTrace([&](bool isIO, bool isRead, uint16_t addr, uint8_t data) {
        if (isIO) {
            if (isRead) io_rd[addr & 0xFF]++;
            else        io_wr[addr & 0xFF]++;
            if (!isRead && boot_reached) {
                for (int i = 0; i < watchio_n; ++i)
                    if ((addr & 0xFF) == (watchio_port[i] & 0xFF))
                        fprintf(stderr, "  [#%d cyc%d] OUT(%02X)=%02X  (Z1.PC=%04X Z2.PC=%04X)\n",
                                trace_seq++, cycles_done, addr & 0xFF, data,
                                machine.cpuPC(), machine.zve2PC());
            }
        } else if (!isRead) {
            if (addr >= 0xF800) {                   // VRAM write (K7024 screen)
                ++vram_writes;
                if (addr < vram_first) vram_first = addr;
                if (addr > vram_last)  vram_last  = addr;
            }
            if (boot_reached) {
                for (int i = 0; i < watch_n; ++i)
                    if (addr == watch_addr[i])
                        fprintf(stderr, "  [#%d cyc%d] WR [%04X]=%02X  (Z1.PC=%04X Z2.PC=%04X)\n",
                                trace_seq++, cycles_done, addr, data,
                                machine.cpuPC(), machine.zve2PC());
            }
        }
    });

    // ── Mount boot disk ───────────────────────────────────────────────────────
    // Try cpa780 first: boot ROM expects 128B sectors (size code 0x00 at [0x03F6])
    // --drive N mounts on drive N; lower drives stay empty (drive search starts at A:).
    bool mounted = machine.mountDisk(mount_drive, disk_path, "cpa780", false);
    if (!mounted) {
        mounted = machine.mountDisk(mount_drive, disk_path, "cpa800", false);
    }
    if (!mounted) {
        fprintf(stderr, "ERROR: Could not mount disk '%s'\n", disk_path);
        fprintf(stderr, "Last error: %s\n", machine.lastError().c_str());
        fprintf(stderr, "Continuing without disk...\n\n");
    } else {
        fprintf(stderr, "Disk mounted OK (drive %d)\n\n", mount_drive);
    }

    // --load-state: resume from a previously saved machine state (RAM+CPU) instead of
    // re-running the boot. The trace then starts from that checkpoint.
    if (load_state_path) {
        if (machine.loadState(load_state_path))
            fprintf(stderr, "Loaded state ← %s (resuming at PC=0x%04X)\n",
                    load_state_path, machine.cpuPC());
        else
            fprintf(stderr, "WARN: could not load state '%s' (missing/invalid) — booting normally\n",
                    load_state_path);
    }

    // ── Run loop ──────────────────────────────────────────────────────────────
    // Per-instruction work (histogram, milestones, boot detection) happens in
    // the trace callbacks above; the loop itself only handles batch-level events.
    int     batch          = 500;
    int     next_progress  = 0;
    bool    rom_was_active = true;
    uint8_t last_done = machine.memReadDebug(0x03F8);  // track [03F8] DMA done-flag

    while (cycles_done < total_limit) {
        int done = machine.run(batch);
        cycles_done += done;

        if (until_hit) break;   // --until condition met (callback already reported + stopped)

        uint16_t pc = machine.cpuPC();

        // ── ROM enable/disable transition ─────────────────────────────────────
        bool rom_now = machine.isRomEnabled();
        if (rom_was_active && !rom_now) {
            fprintf(stderr, "\n*** ROM DISABLED at cycle %d (PC=0x%04X) ***\n",
                    cycles_done, pc);
        }
        rom_was_active = rom_now;

        // ── [0x03F8] DMA done-flag tracking ───────────────────────────────────
        // 0 = ZVE2 running / ZVE1 waiting; 1 = index-pulse timeout (ISR);
        // 3 = ZVE2 finished all sectors → ZVE1 leaves the wait loop at 0x0168.
        uint8_t cur_done = machine.memReadDebug(0x03F8);
        if (cur_done != last_done) {
            fprintf(stderr, "  [cycle %7d] [03F8] done-flag: 0x%02X → 0x%02X  (ZVE1 PC=0x%04X, ZVE2 PC=0x%04X)\n",
                    cycles_done, last_done, cur_done, pc, machine.zve2PC());
            last_done = cur_done;
        }

        // ── Progress report ───────────────────────────────────────────────────
        if (cycles_done >= next_progress) {
            fprintf(stderr, "\n[PROGRESS] cycles=%7d  ZVE1 PC=0x%04X SP=0x%04X  ROM=%s  "
                    "[03F8]=0x%02X  ZVE2 PC=0x%04X instr=%llu%s\n",
                    cycles_done, pc, machine.cpuSP(),
                    machine.isRomEnabled() ? "ON" : "OFF",
                    machine.memReadDebug(0x03F8), machine.zve2PC(),
                    (unsigned long long)zve2_instr,
                    machine.isZVE2InReset() ? " (ZVE2 reset)" :
                    machine.isZVE2Waiting() ? " (ZVE2 wait)" : "");
            fflush(stderr);
            next_progress = cycles_done + 200000;
        }

        if (done == 0) break;
        // Stop at boot unless -p asks to keep tracing the loaded code for N cycles.
        // With --until active, keep running past boot until the condition (or -c limit).
        if (boot_reached && until.kind == UntilCond::NONE) {
            if (post_cycles == 0) break;
            if (cycles_done >= boot_cycle + post_cycles) break;
        }
    }

    if (csv_fp) { fclose(csv_fp); fprintf(stderr, "--csv: %ld row(s) → %s\n", csv_rows, csv_path); }

    // --save-state: persist the machine state at the end of the run (e.g. paired with
    // --until to checkpoint exactly at a condition) for cheap resume via --load-state.
    if (save_state_path) {
        if (machine.saveState(save_state_path))
            fprintf(stderr, "Saved state → %s (PC=0x%04X, cycle %d)\n",
                    save_state_path, machine.cpuPC(), cycles_done);
        else
            fprintf(stderr, "WARN: could not write state '%s'\n", save_state_path);
    }

    // ── Final summary ─────────────────────────────────────────────────────────
    fprintf(stderr, "\n=== Boot Trace Complete: %d cycles ===\n", cycles_done);
    if (until.kind != UntilCond::NONE)
        fprintf(stderr, "--until (%s): %s\n", until.text.c_str(),
                until_hit ? ("MET at cycle " + std::to_string(until_cycle) +
                             " (PC=0x" + [&]{ char b[8]; snprintf(b,8,"%04X",until_pc); return std::string(b); }() + ")").c_str()
                          : "not met (cycle/-c limit reached first)");
    fprintf(stderr, "Boot reached: %s\n", boot_reached ? "YES!" : "NO");
    fprintf(stderr, "ROM enabled:  %s\n", machine.isRomEnabled() ? "YES" : "NO (disabled by BIOS)");

    // ZVE2 / DMA summary — the heart of the boot-sector load.
    fprintf(stderr, "\n--- ZVE2 (DMA-CPU) ---\n");
    fprintf(stderr, "  instructions executed: %llu\n", (unsigned long long)zve2_instr);
    fprintf(stderr, "  ZVE2 final PC=0x%04X  reset=%d wait=%d  BUSRQ=%d\n",
            machine.zve2PC(), machine.isZVE2InReset(),
            machine.isZVE2Waiting(), machine.isBUSRQ());
    fprintf(stderr, "  [03F8] done-flag = 0x%02X  (3 = ZVE2 signalled completion)\n",
            machine.memReadDebug(0x03F8));
    fprintf(stderr, "  ZVE2 reached 0x0267 (DONE): %s\n", zve2_done_seen ? "YES" : "NO");
    if (zve2_instr > 0 && !zve2_done_seen) {
        fprintf(stderr, "  >> DIAGNOSIS: ZVE2 ran but never reached completion. It froze at\n");
        fprintf(stderr, "     PC=0x%04X — likely BUSRQ released mid-program (see k5122 ioRead 0x16).\n",
                machine.zve2PC());
    }

    // Milestone summary
    fprintf(stderr, "\nZVE1 milestone hit counts:\n");
    for (auto& m : milestones) {
        uint32_t n = milestone_counts[m.pc];
        fprintf(stderr, "  0x%04X %-42s  %u\n", m.pc, m.label, n);
    }
    fprintf(stderr, "\nZVE2 milestone hit counts:\n");
    for (auto& m : zve2_milestones) {
        uint32_t n = zve2_ms_counts[m.pc];
        fprintf(stderr, "  0x%04X %-42s  %u\n", m.pc, m.label, n);
    }

    // ZVE2 PC histogram (top 15)
    if (!zve2_pc_hist.empty()) {
        std::vector<std::pair<uint32_t, uint16_t>> zh;
        zh.reserve(zve2_pc_hist.size());
        for (auto& kv : zve2_pc_hist) zh.push_back({kv.second, kv.first});
        std::sort(zh.rbegin(), zh.rend());
        fprintf(stderr, "\nZVE2 PC histogram (top 15):\n");
        int zs = 0;
        for (auto& kv : zh) {
            const char* lbl = romLabel(kv.second);
            fprintf(stderr, "  0x%04X : %6u   %s%s\n", kv.second, kv.first, lbl ? lbl : "",
                    prnTail(kv.second).c_str());
            if (++zs >= 15) break;
        }
    }

    // [0xFFFC..0xFFFF] dump
    fprintf(stderr, "\nStack area [0xFFFC..0xFFFF]: ");
    for (int i = 0; i < 4; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xFFFC + i));
    fprintf(stderr, "\n");

    fprintf(stderr, "Stack area [0xFFF0..0xFFFF]: ");
    for (int i = 0; i < 16; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xFFF0 + i));
    fprintf(stderr, "\n");

    // VRAM first 40 bytes (screen start)
    fprintf(stderr, "\nVRAM [0xF800..0xF827]: ");
    for (int i = 0; i < 40; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xF800 + i));
    fprintf(stderr, "\n");

    // Final register state
    fprintf(stderr, "\nFinal CPU state:\n");
    fprintf(stderr, "  PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
            machine.cpuPC(), machine.cpuSP(), machine.cpuAF(),
            machine.cpuBC(), machine.cpuDE(), machine.cpuHL());

    // PC histogram (top 30)
    std::vector<std::pair<uint32_t, uint16_t>> hist_sorted;
    hist_sorted.reserve(pc_hist.size());
    for (auto& kv : pc_hist)
        hist_sorted.push_back({kv.second, kv.first});
    std::sort(hist_sorted.rbegin(), hist_sorted.rend());
    fprintf(stderr, "\nZVE1 PC histogram (top 30, %llu instructions total):\n",
            (unsigned long long)sample_count);
    int shown = 0;
    for (auto& kv : hist_sorted) {
        const char* lbl = romLabel(kv.second);
        fprintf(stderr, "  0x%04X : %6u  (%5.2f%%)  %s%s\n",
                kv.second, kv.first,
                100.0 * kv.first / sample_count,
                lbl ? lbl : "", prnTail(kv.second).c_str());
        if (++shown >= 30) break;
    }

    // ── Code coverage (--coverage): which ZVE1 code actually executed ──────────
    // Derived from pc_hist: decode each DISTINCT executed instruction once to learn
    // its length, mark its bytes covered, then collapse covered bytes into ranges.
    // (Self-modifying code: length is taken from the FINAL memory image — minor.)
    if (coverage_on) {
        std::vector<bool> cov(0x10000, false);
        auto rd = [&](uint16_t x){ return machine.memReadDebug(x); };
        for (auto& kv : pc_hist) {
            int len = z80dis::decode(rd, kv.first).len;
            for (int b = 0; b < len; ++b) cov[(uint16_t)(kv.first + b)] = true;
        }
        size_t bytes = 0; for (bool c : cov) if (c) ++bytes;
        // collapse into contiguous covered ranges (tools/coverage_diff.h, unit-getestet)
        std::vector<std::pair<int,int>> ranges = covdiff::collapseRanges(cov);
        fprintf(stderr, "\n=== Code coverage (ZVE1) ===\n");
        fprintf(stderr, "  %zu distinct instr addresses, %zu bytes covered in %zu range(s):\n",
                pc_hist.size(), bytes, ranges.size());
        int rshown = 0;
        for (auto& r : ranges) {
            fprintf(stderr, "    0x%04X-0x%04X  (%d byte%s)\n", r.first, r.second,
                    r.second - r.first + 1, (r.second - r.first) ? "s" : "");
            if (++rshown >= 60) { fprintf(stderr, "    … (%zu ranges total)\n", ranges.size()); break; }
        }
        fprintf(stderr, "  ZVE2: %zu distinct instr addresses executed\n", zve2_pc_hist.size());

        // CSV export: cpu,pc,hits (sorted) — machine-readable; two runs' CSVs diff
        // (comm/diff) to see which addresses one run hit and the other did not.
        if (coverage_path) {
            FILE* cf = fopen(coverage_path, "w");
            if (!cf) fprintf(stderr, "  WARN: cannot write coverage CSV '%s'\n", coverage_path);
            else {
                fprintf(cf, "cpu,pc,hits\n");
                auto dump = [&](const std::unordered_map<uint16_t,uint32_t>& h, const char* cpu){
                    std::vector<std::pair<uint16_t,uint32_t>> v(h.begin(), h.end());
                    std::sort(v.begin(), v.end());
                    for (auto& kv : v) fprintf(cf, "%s,0x%04X,%u\n", cpu, kv.first, kv.second);
                };
                dump(pc_hist, "ZVE1");
                dump(zve2_pc_hist, "ZVE2");
                fclose(cf);
                fprintf(stderr, "  CSV written → %s (cpu,pc,hits)\n", coverage_path);
            }
        }
    }

    // Framebuffer activity check
    const uint8_t* fb = machine.framebuffer();
    if (fb) {
        int nonzero = 0;
        for (int i = 0; i < 80 * 24; i++)
            if (fb[i] != 0 && fb[i] != 0xFF) nonzero++;
        fprintf(stderr, "\nFramebuffer non-default bytes: %d / %d\n", nonzero, 80*24);
    }

    // ── Post-boot (loaded-code) analysis (only meaningful with -p) ─────────────
    if (boot_reached && post_cycles > 0) {
        fprintf(stderr, "\n=== Post-boot (loaded code, %d cycles after 0x%04X handoff) ===\n",
                cycles_done - boot_cycle, 0x0437);

        // I/O port activity (which cards the loaded code talks to)
        fprintf(stderr, "\nI/O port activity (rd/wr counts):\n");
        auto portName = [](int p) -> const char* {
            if (p <= 0x07) return "ZRE U-Bus";
            if (p <= 0x0B) return "ZRE BS-PIO";
            if (p <= 0x0F) return "ZRE CTC";
            if (p >= 0x10 && p <= 0x18) return "K5122 floppy";
            if (p >= 0x50 && p <= 0x5F) return "K8025 serial";
            return "";
        };
        for (int p = 0; p < 256; ++p) {
            if (io_rd[p] || io_wr[p])
                fprintf(stderr, "  port 0x%02X : rd=%-7llu wr=%-7llu  %s\n", p,
                        (unsigned long long)io_rd[p], (unsigned long long)io_wr[p],
                        portName(p));
        }

        // VRAM (screen) writes
        fprintf(stderr, "\nVRAM (0xF800-0xFFFF) writes: %llu",
                (unsigned long long)vram_writes);
        if (vram_writes)
            fprintf(stderr, "  (range 0x%04X-0x%04X)", vram_first, vram_last);
        fprintf(stderr, "\n");

        // Post-boot PC histogram (loaded-code hotspots)
        std::vector<std::pair<uint32_t, uint16_t>> ph;
        ph.reserve(post_pc_hist.size());
        for (auto& kv : post_pc_hist) ph.push_back({kv.second, kv.first});
        std::sort(ph.rbegin(), ph.rend());
        fprintf(stderr, "\nLoaded-code PC histogram (top 20):\n");
        int ps = 0;
        for (auto& kv : ph) {
            const char* lbl = romLabel(kv.second);
            fprintf(stderr, "  0x%04X : %6u   %s%s\n", kv.second, kv.first, lbl ? lbl : "",
                    prnTail(kv.second).c_str());
            if (++ps >= 20) break;
        }

        // Screen content as text (VRAM bytes → ISO-8859 chars, 80 columns)
        fprintf(stderr, "\nScreen (VRAM as text, 80 cols, '.' = 0x00/0xFF):\n");
        for (int row = 0; row < 24; ++row) {
            char line[81];
            for (int col = 0; col < 80; ++col) {
                uint8_t c = machine.memReadDebug(static_cast<uint16_t>(0xF800 + row * 80 + col));
                line[col] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
            }
            line[80] = '\0';
            fprintf(stderr, "  |%s|\n", line);
        }
    }

    // Dump a live-RAM range to a binary file for offline disassembly. The loaded
    // code is relocated/chained (and may be self-modified), so the on-disk image
    // is not a faithful source — dump what is actually in memory.
    if (dump_start >= 0 && dump_end > dump_start) {
        FILE* df = fopen(dump_path, "wb");
        if (df) {
            for (int a = dump_start; a < dump_end; ++a)
                fputc(machine.memReadDebug(static_cast<uint16_t>(a)), df);
            fclose(df);
            fprintf(stderr, "\nRAM dump 0x%04X-0x%04X (%d bytes) -> %s\n",
                    dump_start, dump_end, dump_end - dump_start, dump_path);
        }
    }

    // --json: one machine-readable summary line (for scripted/agent consumption).
    if (json_summary) {
        fprintf(stderr,
            "{\"boot_reached\":%s,\"cycles\":%d,\"rom_enabled\":%s,\"final_pc\":\"0x%04X\","
            "\"zve1_addrs\":%zu,\"zve2_addrs\":%zu,\"zve2_instr\":%llu,",
            boot_reached?"true":"false", cycles_done, machine.isRomEnabled()?"true":"false",
            machine.cpuPC(), pc_hist.size(), zve2_pc_hist.size(),
            (unsigned long long)zve2_instr);
        if (until.kind != UntilCond::NONE)
            fprintf(stderr, "\"until\":{\"set\":true,\"met\":%s,\"cycle\":%d,\"pc\":\"0x%04X\"}}\n",
                    until_hit?"true":"false", until_cycle, until_pc);
        else
            fprintf(stderr, "\"until\":{\"set\":false}}\n");
    }

    // Exit code (for scripted/agent use): with --until → 0 if met, 2 if not met;
    // otherwise 0 if the run reached the loaded code (boot handoff), else 1.
    if (until.kind != UntilCond::NONE) return until_hit ? 0 : 2;
    return boot_reached ? 0 : 1;
}
