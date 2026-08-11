/**
 * @file k1520dbg.cpp
 * @brief Interactive command-line debugger for the A5120 / K1520 core.
 *
 * A gdb-style front end around A5120Machine.  Unlike boot_trace (which filters by
 * ABSOLUTE cycles from power-on — handy for the boot ROM, awkward once the loaded
 * OS or a transient program is running), this tool drives an interactive session:
 *
 *   - run to BREAKPOINTS on either CPU (ZVE1 main / ZVE2 DMA), with optional
 *     CONDITIONS (e.g. `b 0xC7A3 if [D1BE]==0`),
 *   - STEP into / over / out (`s`, `n`, `fin`) on either CPU, with each instruction
 *     shown DISASSEMBLED via the built-in single-instruction decoder,
 *   - MEMORY and I/O-PORT watchpoints that print or break,
 *   - SYMBOLS (`sym`) so disassembly and breakpoints can use names,
 *   - a MARKER that zeroes a RELATIVE cycle counter (`mark`) so post-boot / per-
 *     program timing is measured from a chosen origin,
 *   - DISPLAY expressions shown at every stop, register edit, backtrace, memory
 *     dump/poke/load/save, keystroke injection and a screen view.
 *
 * Commands come from stdin (interactive or piped) and/or a -x script file.
 * Type `help` for the command list.  See tools/k1520dbg.md for the full manual.
 *
 * @license MIT
 */
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/track_codec.h"
#include "tools/z80dis_min.h"
#include "tools/prn_listing.h"
#include "tools/mac_listing.h"
#include "tools/callstack_tracker.h"
#include "tools/dbg_commands.h"
#include "tools/expr_eval.h"
#include "tools/event_bp.h"
#include "tools/mem_watch.h"
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <tuple>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <filesystem>
#include <system_error>
#include <optional>
#include <csignal>
#include <chrono>
#include "core/util/os_compat.h"   // processId, isTerminal

using k1520::logging::Logger;
using k1520::logging::Level;

// ─── Register snapshot captured at the start of an instruction ────────────────
// Lightweight copy of the Z80 register file taken at a stop (or by `r`). Used so
// printing/condition-evaluation works against a frozen view and to show shadow
// registers. `valid` distinguishes "never captured yet". (Distinct from the
// machine-wide A5120Machine::MachineSnapshot used by snap/restore/reverse-step.)
struct Snap {
    uint16_t PC=0,SP=0,AF=0,BC=0,DE=0,HL=0,IX=0,IY=0,AF_=0,BC_=0,DE_=0,HL_=0;
    uint8_t  I=0,R=0; uint64_t cyc=0; bool halted=false; bool valid=false;
};
static inline Snap grab(const Z80& z){
    Snap s; s.PC=z.PC; s.SP=z.SP; s.AF=z.AF; s.BC=z.BC; s.DE=z.DE; s.HL=z.HL;
    s.IX=z.IX; s.IY=z.IY; s.AF_=z.AF_; s.BC_=z.BC_; s.DE_=z.DE_; s.HL_=z.HL_;
    s.I=z.I; s.R=z.R; s.cyc=z.cycles; s.halted=z.halted; s.valid=true; return s;
}

// ─── Ctrl-C während eines langen Laufs (§7) ───────────────────────────────────
// Ein `g 20000000` kann Minuten dauern (ZVE2-lastige Phasen). Ohne Ausstieg wirkt
// das wie ein toter Debugger und man killt den Prozess (Sitzung weg). Deshalb:
// SIGINT bricht einen laufenden `g`/`gscreen` ab und kehrt in die REPL zurück;
// außerhalb eines Laufs bleibt Ctrl-C das gewohnte Abbrechen des Prozesses.
static volatile sig_atomic_t g_int_flag = 0;   ///< SIGINT während eines Laufs gesehen
static volatile sig_atomic_t g_in_run   = 0;   ///< läuft gerade ein Lauf-Kernel?
static void dbgSigInt(int){
    if (g_in_run){ g_int_flag = 1; return; }
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

// ─── breakpoint record ────────────────────────────────────────────────────────
// One PC breakpoint (per-CPU maps bp1/bp2 are keyed by address). `cond` is an
// optional expression evaluated at each hit (empty = unconditional); `ignore`
// counts down hits to skip before stopping; `temp` self-deletes on first stop.
struct Bp { bool enabled=true; bool temp=false; std::string cond; long hits=0; long ignore=0; };

#ifdef HAVE_READLINE
// ─── readline tab-completion: complete the FIRST word against command names ───
// (Args fall back to readline's default filename completion.) Command list +
// matcher live in tools/dbg_commands.h so they are unit-testable.
static char* dbgCmdGenerator(const char* text, int state){
    static std::vector<std::string> matches; static size_t idx;
    if (state == 0){ matches = dbgcmd::match(text); idx = 0; }
    return (idx < matches.size()) ? strdup(matches[idx++].c_str()) : nullptr;
}
static char** dbgCompletion(const char* text, int start, int /*end*/){
    if (start == 0){ rl_attempted_completion_over = 1;          // only our matches for the command word
        return rl_completion_matches(text, dbgCmdGenerator); }
    return nullptr;                                              // args → default (filename) completion
}
#endif

// =============================================================================
//  main() — structure
//  -----------------------------------------------------------------------------
//  The whole debugger lives in one main() so that all state and helpers can be
//  shared by reference through lambdas (no globals, no class boilerplate). It is
//  organised in four phases, top to bottom:
//
//    1. CLI parsing + machine bring-up        (disk mount, log level)
//    2. Debugger STATE                        (breakpoints, watchpoints, symbols,
//                                              reverse-ring, trace/logpoint flags …)
//    3. HELPER LAMBDAS                         (capture state by reference):
//         · symbols / .prn listings / address + EXPRESSION evaluation
//         · disassembly + trace formatting
//         · the three machine CALLBACKS (per-ZVE1-instr, per-ZVE2-instr, bus access)
//           — these run during m.run() and decide when to STOP (set `hit`)
//         · inspection/output + run-control helpers (go, step, reverse, backtrace …)
//    4. The REPL                               (read a line, tokenise, dispatch to a
//                                              big if/else-if chain grouped like `help`)
//
//  Stop model: a callback that decides to stop calls stopAt()/stopFromBus(), which
//  sets `hit=true` and m.stop(); the run helper then returns and onStop() prints.
//  "Break-before-execute": callbacks fire BEFORE the instruction, so at a stop the
//  PC sits ON the not-yet-executed instruction.
// =============================================================================
int main(int argc, char** argv){
    // ── Phase 1: CLI parsing ── DISK [-x script] [-s symfile]… [-l listing.prn]…
    const char* disk = nullptr;
    const char* diskB = nullptr;           // -b: second disk, mounted on B: (drive 1)
    const char* script = nullptr;
    std::vector<std::string> symfiles;     // -s: symbol tables (repeatable)
    std::vector<std::string> prnfiles;     // -l: MACRO-80 .prn listings (repeatable)
    // Disk-mount mode (§6): COW is the DEFAULT — the disk is copied to a temp file and
    // that copy is mounted read/write, so a committed fixture can never be corrupted
    // (no more manual `mktemp; cp DISK $T; … $T; rm $T` ritual). `--rw` mounts the
    // original writable (writes persist); `--read-only`/`--ro` mounts write-protected.
    enum { MOUNT_COW=0, MOUNT_RW=1, MOUNT_RO=2 } mount_mode = MOUNT_COW;
    for (int i=1;i<argc;++i){
        if (!strcmp(argv[i],"-x") && i+1<argc) script=argv[++i];
        else if (!strcmp(argv[i],"-s") && i+1<argc) symfiles.push_back(argv[++i]);
        else if (!strcmp(argv[i],"-l") && i+1<argc) prnfiles.push_back(argv[++i]);
        else if (!strcmp(argv[i],"-b") && i+1<argc) diskB=argv[++i];
        else if (!strcmp(argv[i],"--rw")) mount_mode=MOUNT_RW;
        else if (!strcmp(argv[i],"--cow")) mount_mode=MOUNT_COW;
        else if (!strcmp(argv[i],"--read-only")||!strcmp(argv[i],"--ro")) mount_mode=MOUNT_RO;
        else disk=argv[i];
    }
    // Emulator-Log standardmäßig still (das Tool druckt selbst); für Diagnose per
    // K1520DBG_LOGLEVEL=off|error|warn|info|debug|trace anhebbar (z. B. K5122 >>> READ/FORMAT).
    Level baseLvl = Level::ERROR;
    if (const char* lv = getenv("K1520DBG_LOGLEVEL")) {
        if      (!strcmp(lv,"off"))   baseLvl = Level::OFF;
        else if (!strcmp(lv,"error")) baseLvl = Level::ERROR;
        else if (!strcmp(lv,"warn"))  baseLvl = Level::WARN;
        else if (!strcmp(lv,"info"))  baseLvl = Level::INFO;
        else if (!strcmp(lv,"debug")) baseLvl = Level::DEBUG;
        else if (!strcmp(lv,"trace")) baseLvl = Level::TRACE;
    }
    Logger::instance().setBaseLevel(baseLvl);

    A5120Machine m;
    m.powerOn();
    bool mount_failed = false;
    // COW temp copies to unlink at exit (empty unless mount_mode==MOUNT_COW).
    std::vector<std::string> cow_temps;
    // Resolve a requested disk path to the path actually mounted + the write-protect flag.
    // COW: copy to a temp file keeping the extension (so .hfe/.img format detection is
    // unchanged) and mount that; RW: the original; RO: the original, write-protected.
    auto prepareDisk = [&](const std::string& path, bool& wp_out)->std::string{
        if (mount_mode==MOUNT_RW){ wp_out=false; return path; }
        if (mount_mode==MOUNT_RO){ wp_out=true;  return path; }
        wp_out=false;                                   // COW
        std::error_code ec;
        std::filesystem::path src(path);
        std::filesystem::path tmp = std::filesystem::temp_directory_path() /
            ("k1520dbg_cow_"+std::to_string(k1520::os::processId())+"_"+
             std::to_string(cow_temps.size())+src.extension().string());
        std::filesystem::copy_file(src,tmp,std::filesystem::copy_options::overwrite_existing,ec);
        if (ec){ fprintf(stderr,"WARN: COW copy of '%s' failed (%s) — mounting original writable\n",
                         path.c_str(),ec.message().c_str()); return path; }
        cow_temps.push_back(tmp.string());
        fprintf(stderr,"COW: '%s' → %s (writes discarded; use --rw to persist)\n",
                path.c_str(),tmp.string().c_str());
        return tmp.string();
    };
    if (disk){ bool wp; std::string mp=prepareDisk(disk,wp);
        if (!(m.mountDisk(0,mp,"cpa780",wp) || m.mountDisk(0,mp,"cpa800",wp))){
            fprintf(stderr,"WARN: mount '%s' failed: %s\n",disk,m.lastError().c_str());
            mount_failed = true;   // session still runs; reflected in the exit code
        }
        else fprintf(stderr,"Mounted %s on A:%s\n",disk,wp?" (read-only)":"");
    }
    if (diskB){ bool wp; std::string mp=prepareDisk(diskB,wp);
        if (!(m.mountDisk(1,mp,"cpa780",wp) || m.mountDisk(1,mp,"cpa800",wp))){
            fprintf(stderr,"WARN: mount B '%s' failed: %s\n",diskB,m.lastError().c_str());
            mount_failed = true;
        }
        else fprintf(stderr,"Mounted %s on B:%s\n",diskB,wp?" (read-only)":"");
    }

    // ─── Phase 2: debugger state ───────────────────────────────────────────────
    // Most of these are read/written by the run-control commands AND the per-instr
    // callbacks (the callbacks decide when a `run` ends). "Pending" counters use the
    // convention: a callback decrements/clears the field and stops when it reaches 0.
    std::map<uint16_t,Bp> bp1, bp2;            // breakpoints, keyed by PC, per CPU (1=ZVE1, 2=ZVE2)
    int  tw1lo=-1,tw1hi=-1, tw2lo=-1,tw2hi=-1; // -w/-z style live trace windows (PC range; <0 = off)
    long tw_cap=4000, tw_n=0;                  //   cap + counter for window-trace lines
    uint64_t rel_origin=0; bool rel_armed=false; int rel_arm_pc=-1;  // `mark`: relative-cycle origin / arm-at-PC
    Snap snap1, snap2;                          // last captured ZVE1 / ZVE2 register view (for printing)
    bool hit=false; int hit_cpu=0; uint16_t hit_pc=0; std::string stop_reason;  // "we stopped" signal from a callback
    std::string screen_bp;   // #1: if set, any `g`/`gu`/`n` stops once the text VRAM shows this pattern
    int  gu_pc=-1;                              // `gu`/temp-bp target PC (<0 = inactive)
    // Einzelschritt-Kontingent. `step_rem` = wie viele Instruktionen noch AUSGEFÜHRT
    // werden sollen; gehalten wird VOR der darauffolgenden (break-before-execute), das
    // Kommando ist also erst mit dem Halt fertig — dafür `step_active` als Schleifen-
    // bedingung (nicht step_rem, das schon eine Instruktion früher 0 wird).
    long step_rem=0, step2_rem=0;
    bool step_active=false, step2_active=false;
    // Wiederaufnahme genau AUF einem Haltepunkt: der Halt greift jetzt vor der
    // Instruktion, der PC steht also noch darauf. Ohne einmaliges Überspringen hielte
    // `g` sofort wieder am selben Breakpoint (gdb macht es genauso). Gilt nur für die
    // ERSTE Instruktion nach dem Fortsetzen, danach greift der Breakpoint wieder.
    int resume_skip1=-1, resume_skip2=-1;
    bool fin_active=false; uint16_t fin_sp=0;   // `fin`: stop once SP rises above this frame
    bool clock_machine=true;                    // §7: Lauf-Budgets auf der Maschinenuhr (beide CPUs)
    uint16_t last_u=0; bool last_u_set=false;          // `u` continue position
    uint16_t last_list=0; bool last_list_set=false;    // `list` continue position

    // ─── reverse-debugging + history backtrace state ──────────────────────────
    cstrack::CallStackTracker callstack;                     // exact CALL/RST/RET stack
    bool bt_use_history = true;                              // `bt scan` forces old heuristic
    std::deque<A5120Machine::MachineSnapshot> rev_ring;      // auto snapshot before each fwd cmd
    const size_t rev_cap = 200;                              // ring depth (≈13 MB)
    std::map<std::string,A5120Machine::MachineSnapshot> named_snaps;   // snap <name>
    std::deque<A5120Machine::MachineSnapshot> bphit_ring;    // §17: full state at each PC-bp stop (for `rc`)
    const size_t bphit_cap = 60;

    // memory watchpoints: address RANGE + optional VALUE-condition (tools/mem_watch.h,
    // unit-getestet); print or break. Matching-Logik in MemWatch::matches().
    using memwatch::MemWatch;
    std::vector<MemWatch> mwatch;
    std::set<uint8_t>  io_w, io_b;             // io : print-on-access / break-on-access

    // symbols
    std::map<std::string,uint16_t> sym_by_name;
    std::map<uint16_t,std::string> sym_by_addr;

    // command aliases: first token of a line is replaced by its expansion (one level).
    std::map<std::string,std::string> aliases;

    // display list (shown at every stop): each entry is a raw token
    std::vector<std::string> displays;

    // §16 loadable variable dashboard: (name, addr, word?) watch-set for `vars`
    // (vars -f <file> / vars add …). Empty → `vars` shows the built-in CP/A defaults.
    std::vector<std::tuple<std::string,uint16_t,bool>> var_watch;

    // ─── trace-to-file + logpoints ("run and log", no stopping) ────────────────
    FILE* trace_fp   = nullptr;                 // `trace <file>`: continuous instr trace
    int   trace_lo   = -1, trace_hi = -1;       // optional PC window for the file trace
    long  trace_lines= 0;                        // lines written this session
    long  trace_cap  = 2000000;                  // safety cap (~prevents runaway files)
    bool  trace_capped = false;
    // logpoints: at PC, print (PC + optional exprs) and CONTINUE — gdb dprintf.
    std::map<uint16_t,std::vector<std::string>> logpoints;
    // §11 interrupt trace: log every ACCEPTED INT/NMI (no stopping) — the SCPX .COM bug
    // is a CTC-interrupt corrupting the EC0D mini-stack, so a timeline of INTs vs the
    // matcher window is exactly what's needed. Reuses eventbp::classify (like `bint`).
    FILE* itrace_fp=nullptr; long itrace_n=0;

    // ─── break on interrupt / NMI / RETI (event breakpoints, ZVE1) ─────────────
    bool     brk_int=false, brk_nmi=false, brk_reti=false;
    uint16_t bi_prev_sp=0; bool bi_prev_iff1=false, bi_have_prev=false;

    // ─── floppy/bus event breakpoints (§5/§15): /BUSRQ edge + K5122 read/write xfer edge ─
    // Polled once per executed instruction (either CPU) — the K5122/bus state is read
    // through the machine accessors, so no core callback is needed. 0=off / 1=assert /
    // 2=release / 3=both edges (bare command → both). Each carries an OPTIONAL condition
    // (§15: `bxfer if [EBFA]==4`) evaluated at the edge before stopping.
    int  brk_busrq=0, brk_xfer=0, brk_wxfer=0;
    bool fev_prev_busrq=false, fev_prev_xfer=false, fev_prev_wxfer=false, fev_have_prev=false;
    std::string ev_busrq_cond, ev_xfer_cond, ev_wxfer_cond;

    // ─── PC-hotspot profiler (§9 `hist`): count PCs of BOTH CPUs over a cycle window ─
    // While hist_on, the per-instr callbacks ONLY tally (they skip all stop logic), so
    // `hist` profiles straight through breakpoints instead of tripping them.
    bool hist_on=false; int hist_lo=-1, hist_hi=-1;
    std::map<uint16_t,uint32_t> hist1, hist2;

    // ─── Phase 3: helper lambdas (capture all state above by reference) ─────────
    // Small formatting/util helpers first, then symbols, .prn, the expression
    // evaluator, disassembly, the machine callbacks, and finally run-control.
    auto rc = [&](uint64_t cyc)->long long {            // cycle as shown: relative to `mark` origin, else absolute
        return rel_armed ? (long long)(cyc-rel_origin) : (long long)cyc;
    };
    auto rcpfx = [&]{ return rel_armed ? '+' : 'c'; };
    auto rd1   = [&](uint16_t a){ return m.memReadDebug(a); };   // byte reader for the decoder

    // ─── symbols ───────────────────────────────────────────────────────────────
    auto symAdd = [&](const std::string& name, uint16_t a){
        sym_by_name[name]=a; sym_by_addr[a]=name; };
    auto symFor = [&](uint16_t a)->std::string{
        auto it=sym_by_addr.find(a); return it==sym_by_addr.end()? std::string() : it->second; };
    auto loadSyms = [&](const std::string& path)->int{
        std::ifstream f(path); if(!f){ fprintf(stderr,"  cannot open %s\n",path.c_str()); return 0; }
        std::string l; int n=0;
        while (std::getline(f,l)){
            std::istringstream is(l); std::string a,b; if(!(is>>a)) continue; if(a[0]=='#') continue;
            if(!(is>>b)){ continue; }
            // accept "ADDR NAME", "NAME ADDR", "NAME = ADDR"
            if (b=="=") { std::string c; if(!(is>>c)) continue; b=c; }
            char* e1=nullptr; long va=strtol(a.c_str(),&e1,16);
            char* e2=nullptr; long vb=strtol(b.c_str(),&e2,16);
            if (*e1==0 && e1!=a.c_str()) symAdd(b,(uint16_t)va);          // ADDR NAME
            else if (*e2==0 && e2!=b.c_str()) symAdd(a,(uint16_t)vb);     // NAME ADDR
            else continue;
            ++n;
        }
        fprintf(stderr,"  loaded %d symbol(s) from %s\n",n,path.c_str()); return n; };

    // ─── Listings (Adresse → kommentierte Original-Quellzeile) ─────────────────
    prnlst::Listing prn;
    // spec = "PFAD[@SPEC]" mit SPEC =
    //   OFFSET  — signiert (0x../..h/dez), wird zu jeder Listing-Adresse addiert
    //             (Code, der nicht an der Listing-Adresse läuft),
    //   auto    — Versatz selbst bestimmen: Objektbytes im RAM suchen (§2),
    //   labels  — nur `.MAC`: Adressen ausschließlich aus den Mxxxx-Labelankern,
    //   noanchor— nur `.MAC`: Anker ignorieren, rein durchgezählte Längen.
    // `.MAC`/`.ASM`-Dateien (Fremdquellen ohne Adressspalte) werden dazu von
    // tools/mac_listing.h assembliert; `.prn`-Listings tragen ihre Adressen selbst.
    auto loadPrnSpec = [&](const std::string& spec)->int{
        std::string path; long off=0; std::string mode;
        {   size_t at = spec.rfind('@');
            if (at != std::string::npos){
                std::string s = spec.substr(at+1);
                std::string sl = s; for(auto&c:sl) c=(char)tolower(c);
                if (sl=="auto"||sl=="labels"||sl=="noanchor"){ mode=sl; path=spec.substr(0,at); }
            }
        }
        if (mode.empty() && !prnlst::splitSpec(spec,path,off)){
            fprintf(stderr,"  bad @offset in '%s'\n",spec.c_str()); return -1; }

        // ── Fremdquelle (.MAC/.ASM): assemblieren statt Listing parsen ────────
        if (maclst::isSourceFile(path)){
            maclst::Result mr; maclst::Image img;
            bool anchors = (mode != "noanchor");
            if (!maclst::assemble(path, off, prn, mr, &img, anchors)){
                fprintf(stderr,"  %s\n", mr.error.c_str()); return -1; }
            if (mode=="auto"){
                // §2: Objektbytes im Speicher wiederfinden → Ladeversatz ableiten.
                auto match = maclst::findOffset(img,[&](uint16_t a){ return m.memReadDebug(a); });
                if (!match.found){
                    fprintf(stderr,"  @auto: kein Treffer im Speicher (%d feste Ankerbytes) — "
                                   "passt die Quelle zu diesem Image?\n", maclst::fixedByteCount(img));
                } else {
                    fprintf(stderr,"  @auto: Versatz %+ld / %04X — %s\n"
                                   "         %d von %d Bytes gleich (%.1f %%), %d Abweichung(en); "
                                   "Anker %d B @%04X, %d Kandidat(en)\n",
                            match.offset,(uint16_t)match.offset, match.verdict(),
                            match.matched, match.fixed, 100.0*match.ratio,
                            match.fixed-match.matched, match.anchor_len, match.anchor_src,
                            match.candidates);
                    // Nur einen belastbaren Versatz anwenden: eine 30-%-Übereinstimmung
                    // heißt „anderes Build" — die Zeilen lägen dann versetzt auf fremdem
                    // Code und die Annotation führte in die Irre.
                    if (match.ratio < 0.60)
                        fprintf(stderr,"         → Versatz NICHT angewandt (zu unsicher). Quelle und Image "
                                       "sind verschiedene Builds;\n           notfalls '%s@%ld' erzwingen "
                                       "oder mit 'verify' bereichsweise vergleichen.\n",
                                path.c_str(), match.offset);
                    else if (match.offset){            // Tabelle mit dem Versatz neu aufbauen
                        prn.by_addr.clear(); maclst::Result r2;
                        maclst::assemble(path, match.offset, prn, r2, nullptr, anchors);
                    }
                }
            }
            int li=0;
            for (auto& kv : prn.by_addr){
                std::string lab = prnlst::labelOf(kv.second);
                if (!lab.empty() && sym_by_name.find(lab)==sym_by_name.end()){ symAdd(lab,kv.first); ++li; }
            }
            fprintf(stderr,"  assembliert: %d Zeile(n) aus %s (%04X..%04X), %d Anker/%d Nachführung(en), "
                           "%d unbekannt, %d Label → Symbole\n",
                    mr.code, path.c_str(), mr.first, mr.last, mr.anchors, mr.resyncs, mr.unknown, li);
            for (size_t i=0;i<mr.problems.size() && i<5;++i)
                fprintf(stderr,"    ? %s\n", mr.problems[i].c_str());
            return mr.code;
        }

        // ── .prn-Listing ─────────────────────────────────────────────────────
        int n = prn.load(path, mode=="auto"? 0 : off, /*want_bytes=*/mode=="auto");
        if (n < 0){ fprintf(stderr,"  cannot open %s\n",path.c_str()); return n; }
        if (mode=="auto"){
            maclst::Image img; img.byte = prn.bytes_by_addr;
            auto match = maclst::findOffset(img,[&](uint16_t a){ return m.memReadDebug(a); });
            if (!match.found)
                fprintf(stderr,"  @auto: kein Treffer im Speicher (%d Ankerbytes)\n",
                        maclst::fixedByteCount(img));
            else {
                fprintf(stderr,"  @auto: Versatz %+ld / %04X — %s (%d von %d Bytes, %.1f %%)\n",
                        match.offset,(uint16_t)match.offset, match.verdict(),
                        match.matched, match.fixed, 100.0*match.ratio);
                if (match.ratio < 0.60)
                    fprintf(stderr,"         → Versatz NICHT angewandt (zu unsicher)\n");
                else if (match.offset){
                    prn.by_addr.clear(); prn.bytes_by_addr.clear();
                    n = prn.load(path, match.offset); }
            }
        }
        // Labels (name:) aus dem Listing als Symbole importieren (b/u/list per Name),
        // ohne bestehende (z.B. -s-/user-) Symbole zu überschreiben.
        int li=0;
        for (auto& kv : prn.by_addr){
            std::string lab = prnlst::labelOf(kv.second);
            if (!lab.empty() && sym_by_name.find(lab)==sym_by_name.end()){ symAdd(lab,kv.first); ++li; }
        }
        char off_s[32]={0}; if(off) snprintf(off_s,sizeof off_s," (offset %+ld / %04X)",off,(uint16_t)off);
        fprintf(stderr,"  loaded %d listing line(s) from %s%s, %d label(s) → symbols\n",n,path.c_str(),off_s,li);
        return n; };
    // Annotation für eine Adresse (leer, wenn keine .prn-Quelle vorliegt).
    auto prnFor = [&](uint16_t a)->std::string{
        const std::string* s = prn.find(a); return s ? *s : std::string(); };

    // ─── address / value resolution ──────────────────────────────────────────
    // resolveAddr: number (0x.., ..H, dec) OR a symbol name, optionally NAME+OFF.
    auto resolveAddr = [&](const std::string& tok)->long{
        size_t plus=tok.find_first_of("+-",1);
        std::string base = plus==std::string::npos? tok : tok.substr(0,plus);
        long off=0;
        if (plus!=std::string::npos){ off=strtol(tok.c_str()+plus,nullptr,0); }
        auto it=sym_by_name.find(base);
        if (it!=sym_by_name.end()) return (long)(uint16_t)(it->second+off);
        // ..H suffix → hex
        if (!base.empty() && (base.back()=='H'||base.back()=='h')){
            return strtol(base.substr(0,base.size()-1).c_str(),nullptr,16)+off; }
        return strtol(base.c_str(),nullptr,0)+off; };

    // ─── expression evaluator ──────────────────────────────────────────────────
    // The recursive-descent parser lives in tools/expr_eval.h (maschinenfrei, unit-
    // getestet). Hier nur die Brücke: Snap→RegView, Speicher- und Symbol-Callback.
    // Genutzt von `b … if`, `disp`, `logpoint` und der `x`-Adresse.
    expreval::ReadByte exprReadByte = [&](uint16_t a){ return m.memReadDebug(a); };
    expreval::FindSym  exprFindSym  = [&](const std::string& n, uint16_t& v)->bool{
        auto it=sym_by_name.find(n); if(it==sym_by_name.end()) return false; v=it->second; return true; };
    auto readOperand = [&](const Snap& s, const std::string& t, bool& ok)->long{
        expreval::RegView rv{ s.AF,s.BC,s.DE,s.HL,s.IX,s.IY,s.SP,s.PC,s.I,s.R };
        return expreval::eval(t, rv, exprReadByte, exprFindSym, ok); };

    // evalCond: empty → always; else the expression must be non-zero (comparisons → 0/1).
    auto evalCond = [&](const Snap& s, const std::string& cond)->bool{
        if (cond.empty()) return true;
        bool ok; long v=readOperand(s,cond,ok); return v!=0; };

    // ─── disassembly helpers ───────────────────────────────────────────────────
    auto disasmAt = [&](uint16_t a, char* out, size_t n)->int{
        z80dis::Insn d = z80dis::decode(rd1, a);
        std::string sym = symFor(a);
        std::string tgt;
        if (d.has_target){ std::string ts=symFor(d.target); if(!ts.empty()) tgt=" <"+ts+">"; }
        char hex[16]={0}; for(int i=0;i<d.len && i<5;++i){ char b[4]; snprintf(b,4,"%02X ",m.memReadDebug(a+i)); strcat(hex,b);}
        snprintf(out,n,"%04X%s%s: %-14s %s%s", a,
                 sym.empty()?"":" ", sym.empty()?"":("<"+sym+">").c_str(), hex, d.text, tgt.c_str());
        return d.len; };
    auto showInsn = [&](const char* tag, uint16_t a){
        char l[120]; disasmAt(a,l,sizeof l);
        std::string p=prnFor(a);
        fprintf(stderr,"  %s %s%s%s\n",tag,l, p.empty()?"":"  ; ", p.c_str()); };

    auto flagsStr = [&](uint16_t af, char* o){
        uint8_t f=af&0xFF; snprintf(o,12,"%c%c%c%c%c%c",
            f&Z80::FLAG_S?'S':'-', f&Z80::FLAG_Z?'Z':'-', f&Z80::FLAG_H?'H':'-',
            f&Z80::FLAG_PV?'P':'-', f&Z80::FLAG_N?'N':'-', f&Z80::FLAG_C?'C':'-'); };

    auto traceLineTo = [&](FILE* fp, int cpu, const Z80& z){
        char dis[120]; disasmAt(z.PC,dis,sizeof dis);
        char fl[12]; flagsStr(z.AF,fl);
        // .prn-Annotation ans Zeilenende, damit die Register-Spalten ausgerichtet bleiben.
        std::string p=prnFor(z.PC);
        fprintf(fp,"T%d %c%-9lld %-46s AF=%04X[%s] BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X%s%s\n",
                cpu,rcpfx(),rc(z.cycles),dis,z.AF,fl,z.BC,z.DE,z.HL,z.IX,z.IY,z.SP,
                p.empty()?"":"  ; ", p.c_str());
    };
    auto traceLine = [&](int cpu, const Z80& z){ traceLineTo(stderr,cpu,z); };
    // Continuous trace-to-file for one CPU (honours the optional PC window + cap).
    auto traceToFile = [&](int cpu, const Z80& z){
        if (!trace_fp) return;
        if (trace_lo>=0 && (z.PC<trace_lo || z.PC>trace_hi)) return;
        if (trace_lines>=trace_cap){
            if(!trace_capped){ fprintf(stderr,"  [trace] cap %ld lines reached — tracing stopped (trace off / raise cap)\n",trace_cap); trace_capped=true; }
            return;
        }
        traceLineTo(trace_fp,cpu,z); ++trace_lines;
    };
    // Logpoint hit: print PC + disasm + evaluated exprs to the console, then continue.
    auto logHit = [&](const Z80& z, const std::vector<std::string>& exprs){
        Snap s=grab(z);
        char dis[120]; disasmAt(z.PC,dis,sizeof dis);
        fprintf(stderr,"[lp] %c%-9lld %s",rcpfx(),rc(z.cycles),dis);
        for (auto& e: exprs){ bool ok; long v=readOperand(s,e,ok);
            fprintf(stderr,"  %s=%ld(0x%lX)",e.c_str(),v,(unsigned long)(v&0xFFFF)); }
        std::string p=prnFor(z.PC); if(!p.empty()) fprintf(stderr,"  ; %s",p.c_str());
        fprintf(stderr,"\n");
    };

    // central "we decided to stop" used by all callbacks
    auto stopAt = [&](int cpu, const Z80& z, const std::string& why){
        if(cpu==2) snap2=grab(z); else snap1=grab(z);
        hit=true; hit_cpu=cpu; hit_pc=z.PC; stop_reason=why; m.stop();
    };
    auto stopFromBus = [&](const std::string& why){
        snap1=grab(m.cpuDebug()); hit=true; hit_cpu=1; hit_pc=m.cpuPC(); stop_reason=why; m.stop(); };

    // §5 floppy/bus event breakpoints: sample /BUSRQ and the K5122 read-transfer flag
    // each instruction and stop on the requested edge. Called from BOTH per-instr
    // callbacks (so an edge is caught whichever CPU is stepping). Returns true if it stopped.
    auto checkFloppyEv = [&](int cpu, const Z80& z)->bool{
        auto k = m.k5122State();
        bool busrq = m.isBUSRQ();
        bool xfer  = k.transferring;
        bool wxfer = k.writeMode;
        bool stopped=false;
        if (fev_have_prev){
            if (brk_busrq && busrq!=fev_prev_busrq &&
                ((busrq && (brk_busrq&1)) || (!busrq && (brk_busrq&2))) &&
                evalCond(grab(z), ev_busrq_cond)){
                char w[40]; snprintf(w,sizeof w,"/BUSRQ %s", busrq?"asserted":"released");
                stopAt(cpu,z,w); stopped=true;
            }
            if (!stopped && brk_xfer && xfer!=fev_prev_xfer &&
                ((xfer && (brk_xfer&1)) || (!xfer && (brk_xfer&2))) &&
                evalCond(grab(z), ev_xfer_cond)){
                char w[48]; snprintf(w,sizeof w,"K5122 read-xfer %s", xfer?"start":"end");
                stopAt(cpu,z,w); stopped=true;
            }
            if (!stopped && brk_wxfer && wxfer!=fev_prev_wxfer &&
                ((wxfer && (brk_wxfer&1)) || (!wxfer && (brk_wxfer&2))) &&
                evalCond(grab(z), ev_wxfer_cond)){
                char w[48]; snprintf(w,sizeof w,"K5122 write-xfer %s", wxfer?"start":"end");
                stopAt(cpu,z,w); stopped=true;
            }
        }
        fev_prev_busrq=busrq; fev_prev_xfer=xfer; fev_prev_wxfer=wxfer; fev_have_prev=true;
        return stopped;
    };

    // ─── per-instruction & bus callbacks ───────────────────────────────────────
    // These fire from inside m.run(), once per executed instruction (ZVE1 / ZVE2)
    // or per bus access. They are the ONLY place a run is ended: a stop decision
    // calls stopAt()/stopFromBus() (sets `hit`, calls m.stop()). The run helper
    // then returns to the REPL. Fires BEFORE the instruction executes.
    //
    // ZVE1 order matters and is, top to bottom:
    //   1. bookkeeping that must see EVERY instruction (call-stack, file trace,
    //      logpoints, event breakpoints, `mark` arming, window trace) — no early exit;
    //   2. the run-terminating checks, highest priority first: single-step quota →
    //      step-out (`fin`) → run-until (`gu`) → address breakpoint. Each of these
    //      `return`s after acting, so a pending step is not also treated as a bp hit.
    m.setCpuTraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        // §9 hist: profiling mode only tallies PCs and skips ALL stop logic below.
        if (hist_on){ if(hist_lo<0 || (pc>=hist_lo && pc<=hist_hi)) hist1[pc]++; return; }
        // Erste Instruktion nach einem Fortsetzen? Dann darf ein Haltepunkt AUF dieser
        // Adresse nicht erneut greifen (wir stehen ja genau darauf — break-before-execute).
        // Das Kennzeichen gilt für genau einen Callback und wird hier verbraucht.
        bool skip_resume_stop = false;
        if (resume_skip1 >= 0){
            skip_resume_stop = (pc == (uint16_t)resume_skip1);
            resume_skip1 = -1;
        }
        // Maintain the exact CALL/RST/RET call stack (for the history backtrace).
        // Cheap: 1 mem read/instr in the common (non-call/ret) case.
        callstack.onInstruction(pc,[&](uint16_t a){ return m.memReadDebug(a); });
        // "run and log" (no stopping): continuous file trace + logpoints fire first.
        traceToFile(1,z);
        { auto lit=logpoints.find(pc); if(lit!=logpoints.end()) logHit(z,lit->second); }
        // event breakpoints: interrupt / NMI accepted (state signature) or RETI (opcode).
        // Klassifikation in tools/event_bp.h (unit-getestet, inkl. NMI-bei-IFF1=0-Grenzfall).
        // Beim Fortsetzen übersprungen: das RETI/RETN, auf dem wir stehen, würde sonst
        // sofort wieder halten (INT/NMI erkennen ohnehin nur Flanken).
        if ((brk_int||brk_nmi||brk_reti) && !skip_resume_stop){
            uint8_t o0=0,o1=0; if(brk_reti){ o0=m.memReadDebug(pc); o1=m.memReadDebug((uint16_t)(pc+1)); }
            eventbp::Prev pv{bi_have_prev, bi_prev_sp, bi_prev_iff1};
            switch (eventbp::classify(pc, z.SP, z.IFF1, pv, brk_int, brk_nmi, brk_reti, o0, o1)){
                case eventbp::Event::NMI: stopAt(1,z,"NMI accepted (Q240/protection?)"); break;
                // §5: Vektor + Quellgerät + aufgelöste Tabellenadresse gleich mit anzeigen.
                // „Gerät hat Vektor 0xFF" vs. „kein Gerät hat geantwortet" (SPURIOUS) ist der
                // Unterschied, an dem ein Fremd-OS-Interruptsturm hängt.
                case eventbp::Event::Interrupt:{ char w[160];
                    auto& ia=m.lastIntAck();
                    uint16_t tb=(uint16_t)((z.I<<8)|(ia.vector&0xFE));
                    snprintf(w,sizeof w,"interrupt → ISR %04X  Vektor=%02X %s%s%s  Tabelle [%04X]",
                             pc, ia.vector,
                             ia.spurious? "SPURIOUS (kein Geraet!)" : "von ",
                             ia.spurious? "" : (ia.device?ia.device:"?"),
                             z.IM==2? "" : "  (IM!=2)", tb);
                    stopAt(1,z,w); } break;
                case eventbp::Event::RETI: stopAt(1,z,"RETI"); break;
                case eventbp::Event::RETN: stopAt(1,z,"RETN"); break;
                case eventbp::Event::None: break;
            }
        }
        // §11 interrupt trace (non-stopping): classify with INT+NMI always armed and log.
        if (itrace_fp){
            eventbp::Prev pv{bi_have_prev, bi_prev_sp, bi_prev_iff1};
            eventbp::Event e = eventbp::classify(pc, z.SP, z.IFF1, pv, true, true, false, 0, 0);
            if (e==eventbp::Event::Interrupt || e==eventbp::Event::NMI){
                uint16_t ret=(uint16_t)(m.memReadDebug(z.SP)|(m.memReadDebug((uint16_t)(z.SP+1))<<8));
                std::string p=prnFor(pc), isr=symFor(pc);
                // §5: Vektor + Quellgerät mitschreiben (bei NMI gibt es keine Quittung).
                auto& ia=m.lastIntAck();
                char via[64]="";
                if (e==eventbp::Event::Interrupt)
                    snprintf(via,sizeof via," vec=%02X dev=%s", ia.vector,
                             ia.spurious? "SPURIOUS" : (ia.device?ia.device:"?"));
                fprintf(itrace_fp,"IT %c%-9lld %-3s int@%04X → ISR %04X%s%s%s SP=%04X%s%s%s\n",
                        rcpfx(), rc(z.cycles), e==eventbp::Event::NMI?"NMI":"INT", ret, pc,
                        isr.empty()?"":" <",isr.c_str(),isr.empty()?"":">", z.SP, via,
                        p.empty()?"":"  ; ", p.c_str());
                ++itrace_n;
            }
        }
        bi_prev_sp=z.SP; bi_prev_iff1=z.IFF1; bi_have_prev=true;
        if (rel_arm_pc>=0 && pc==(uint16_t)rel_arm_pc){
            rel_origin=z.cycles; rel_armed=true; rel_arm_pc=-1;
            fprintf(stderr,"[mark] relative origin set at PC=%04X (abs cyc=%llu)\n",
                    pc,(unsigned long long)z.cycles);
        }
        if (tw1lo>=0 && pc>=tw1lo && pc<=tw1hi && tw_n<tw_cap){ traceLine(1,z); ++tw_n; }
        if ((brk_busrq||brk_xfer||brk_wxfer) && checkFloppyEv(1,z)) return;   // §5/§15 /BUSRQ / xfer edge
        // Einzelschritt: der Halt bricht die Instruktion ab, also erst durchlassen und
        // beim NÄCHSTEN Callback halten — sonst käme `s` nie von der Stelle. Ergebnis
        // ist zugleich die gdb-Semantik: nach `s` steht der PC auf dem NÄCHSTEN Befehl.
        if (step_active){
            if (step_rem<=0){ step_active=false; stopAt(1,z,"step"); return; }
            traceLine(1,z); --step_rem; return;
        }
        if (fin_active && z.SP > fin_sp){ fin_active=false; stopAt(1,z,"step-out"); return; }
        // Fortsetzen VON einem Haltepunkt: die erste Instruktion nach dem Resume darf
        // nicht sofort wieder halten (sie ist ja genau die, auf der wir stehen).
        if (skip_resume_stop){ return; }
        if (gu_pc>=0 && pc==(uint16_t)gu_pc){ gu_pc=-1; stopAt(1,z,"run-until"); return; }
        auto it=bp1.find(pc);
        if (it!=bp1.end() && it->second.enabled && evalCond(grab(z),it->second.cond)){
            it->second.hits++;
            if (it->second.ignore>0){ it->second.ignore--; }   // skip this hit (gdb ignore)
            else {
                std::string why="bp ZVE1";
                if(!it->second.cond.empty()) why+=" ["+it->second.cond+"]";
                if(it->second.temp) bp1.erase(it);
                stopAt(1,z,why);
            }
        }
    });
    m.setZVE2TraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        if (hist_on){ if(hist_lo<0 || (pc>=hist_lo && pc<=hist_hi)) hist2[pc]++; return; }  // §9
        bool skip_resume_stop = false;                 // s. ZVE1-Callback
        if (resume_skip2 >= 0){
            skip_resume_stop = (pc == (uint16_t)resume_skip2);
            resume_skip2 = -1;
        }
        traceToFile(2,z);   // gap-free trace across DMA phases (ZVE2 also logged)
        if ((brk_busrq||brk_xfer||brk_wxfer) && checkFloppyEv(2,z)) return;   // §5/§15 (edge, either CPU)
        if (tw2lo>=0 && pc>=tw2lo && pc<=tw2hi && tw_n<tw_cap){ traceLine(2,z); ++tw_n; }
        if (step2_active){
            if (step2_rem<=0){ step2_active=false; stopAt(2,z,"step ZVE2"); return; }
            traceLine(2,z); --step2_rem; return;
        }
        if (skip_resume_stop){ return; }
        auto it=bp2.find(pc);
        if (it!=bp2.end() && it->second.enabled && evalCond(grab(z),it->second.cond)){
            it->second.hits++;
            if (it->second.ignore>0){ it->second.ignore--; }   // skip this hit (gdb ignore)
            else {
            std::string why="bp ZVE2";
            if(!it->second.cond.empty()) why+=" ["+it->second.cond+"]";
            if(it->second.temp) bp2.erase(it);
            stopAt(2,z,why); }
        }
    });
    // Attribute a bus access to the CPU that actually issued it. During a DMA the bus
    // master is ZVE2, not ZVE1 — printing ZVE1's PC there is misleading. m.busMasterIsZVE2()
    // is valid inside this callback (set by the run loop around each CPU step).
    auto busWho = [&]{ static char s[20];
        snprintf(s,sizeof s,"%s.PC=%04X", m.busMasterIsZVE2()?"ZVE2":"ZVE1", m.busMasterPC());
        return s; };
    // Evaluate a memory access against every watchpoint (range + value-condition).
    auto hitMem = [&](bool isRead, uint16_t addr, uint8_t data){
        for (auto& w : mwatch){
            if (!w.matches(isRead, addr, data)) continue;   // siehe tools/mem_watch.h
            ++w.hits;
            if (w.brk){ char wmsg[56];
                snprintf(wmsg,sizeof wmsg,"watch %s [%04X]=%02X by %s",isRead?"RD":"WR",addr,data,busWho());
                stopFromBus(wmsg); }
            else
                fprintf(stderr,"[%s] %c%-9lld %s [%04X]=%02X  %s\n", isRead?"wr":"wp",
                        rcpfx(),rc(m.cpuCycles()),isRead?"RD":"WR",addr,data,busWho());
        }
    };
    m.setBusTrace([&](bool isIO,bool isRead,uint16_t addr,uint8_t data){
        if (isIO){ uint8_t port=(uint8_t)addr;
            if (io_w.count(port))
                fprintf(stderr,"[io] %c%-9lld %s (%02XH)=%02X  %s\n",
                        rcpfx(),rc(m.cpuCycles()),isRead?"IN ":"OUT",port,data,busWho());
            if (io_b.count(port)){ char w[40]; snprintf(w,sizeof w,"io %s (%02XH)=%02X",isRead?"IN":"OUT",port,data);
                stopFromBus(w); }
            return;
        }
        hitMem(isRead, addr, data);
    });

    // ─── helpers ───────────────────────────────────────────────────────────────
    auto printSnap = [&](int cpu){
        const Snap& s = (cpu==2)?snap2:snap1;
        if (!s.valid){ fprintf(stderr,"  (ZVE%d: no state captured yet — run first)\n",cpu); return; }
        uint16_t ret=(uint16_t)(m.memReadDebug(s.SP)|(m.memReadDebug(s.SP+1)<<8));
        char fl[12]; flagsStr(s.AF,fl);
        fprintf(stderr,
            "  ZVE%d PC=%04X SP=%04X(->%04X) AF=%04X[%s] BC=%04X DE=%04X HL=%04X "
            "IX=%04X IY=%04X  AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X%s cyc=%llu\n",
            cpu,s.PC,s.SP,ret,s.AF,fl,s.BC,s.DE,s.HL,s.IX,s.IY,s.AF_,s.BC_,s.DE_,s.HL_,
            s.I,s.R, s.halted?" HALT":"", (unsigned long long)s.cyc);
    };
    auto stateLine = [&]{
        fprintf(stderr,"  state: ROM=%s BUSRQ=%s ZVE2=%s  %c-cyc=%lld%s\n",
            m.isRomEnabled()?"on":"off", m.isBUSRQ()?"yes":"no",
            m.isZVE2InReset()?"reset":(m.isZVE2Waiting()?"wait":"run"),
            rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
    };
    auto dump = [&](uint16_t a, int len){
        for (int o=0;o<len;o+=16){
            char asc[17]={0}; fprintf(stderr,"  %04X: ",(uint16_t)(a+o));
            for (int i=0;i<16;++i){ if(o+i<len){uint8_t b=m.memReadDebug(a+o+i);
                fprintf(stderr,"%02X ",b); asc[i]=(b>=0x20&&b<0x7F)?(char)b:'.';}
                else { fprintf(stderr,"   "); asc[i]=' '; } }
            fprintf(stderr," |%s|\n",asc);
        }
    };
    // ─── gdb-style memory examine:  x/<count><fmt><size> <addr> ────────────────
    // fmt: x hex · d signed-dec · u unsigned-dec · c char · t binary · o octal ·
    //      a address(+symbol) · i instruction · s NUL-string.  size: b=1 · w/h=2 (LE).
    struct XFmt { int count=1; char fmt='x'; int size=1; };
    XFmt     xlast;
    uint16_t xaddr=0; bool xaddr_set=false;
    auto examine = [&](const std::string& spec, bool have_addr, uint16_t addr){
        XFmt f = xlast;
        if (!spec.empty()){
            size_t i=0; std::string num;
            while(i<spec.size() && spec[i]>='0' && spec[i]<='9') num+=spec[i++];
            if(!num.empty()) f.count=atoi(num.c_str());
            for(; i<spec.size(); ++i){ char c=spec[i];
                if(c=='b') f.size=1; else if(c=='w'||c=='h') f.size=2;
                else f.fmt=c; }
        }
        if(f.fmt=='a') f.size=2; else if(f.fmt=='c') f.size=1;
        if(f.count<1) f.count=1;
        xlast=f;
        if(have_addr){ xaddr=addr; xaddr_set=true; }
        else if(!xaddr_set){ xaddr=m.cpuPC(); xaddr_set=true; }
        uint16_t a=xaddr;
        if(f.fmt=='i'){
            for(int k=0;k<f.count;++k){ char l[120]; int len=disasmAt(a,l,sizeof l);
                std::string p=prnFor(a); fprintf(stderr,"  %s%s%s\n",l,p.empty()?"":"  ; ",p.c_str());
                a=(uint16_t)(a+len); }
        } else if(f.fmt=='s'){
            for(int k=0;k<f.count;++k){ fprintf(stderr,"  %04X: \"",a); int n=0; uint8_t b;
                while((b=m.memReadDebug(a))!=0 && n<255){ fputc((b>=0x20&&b<0x7F)?(char)b:'.',stderr); ++a; ++n; }
                ++a; fprintf(stderr,"\"\n"); }
        } else {
            auto pv=[&](long u)->std::string{ char b[48];
                switch(f.fmt){
                    case 'd':{ long sv=(f.size==2)?(int16_t)u:(int8_t)u; snprintf(b,sizeof b,"%ld",sv); break; }
                    case 'u': snprintf(b,sizeof b,"%lu",(unsigned long)u); break;
                    case 'c': snprintf(b,sizeof b,"0x%02lX %s",u,(u>=0x20&&u<0x7F)?(std::string("'")+(char)u+"'").c_str():"  "); break;
                    case 't':{ std::string s; for(int i=f.size*8-1;i>=0;--i) s+=((u>>i)&1)?'1':'0'; snprintf(b,sizeof b,"%s",s.c_str()); break; }
                    case 'o': snprintf(b,sizeof b,"0%lo",(unsigned long)u); break;
                    case 'a':{ std::string s=symFor((uint16_t)u); snprintf(b,sizeof b,"0x%04lX%s%s%s",u,s.empty()?"":" <",s.c_str(),s.empty()?"":">"); break; }
                    default: snprintf(b,sizeof b, f.size==2?"%04lX":"%02lX", u); break;
                } return std::string(b); };
            int per = (f.fmt=='x') ? (f.size==2?8:8) : (f.fmt=='a'?4:8);
            for(int k=0;k<f.count;){
                std::string sym=symFor(a);
                fprintf(stderr,"  %04X%s%s%s:",a,sym.empty()?"":" <",sym.c_str(),sym.empty()?"":">");
                for(int col=0; col<per && k<f.count; ++col,++k){
                    long v; if(f.size==2){ v=m.memReadDebug(a)|(m.memReadDebug((uint16_t)(a+1))<<8); a=(uint16_t)(a+2); }
                            else { v=m.memReadDebug(a); a=(uint16_t)(a+1); }
                    fprintf(stderr," %s",pv(v).c_str());
                }
                fprintf(stderr,"\n");
            }
        }
        xaddr=a;
    };

    // ─── source view from the loaded .prn listing (gdb `list`) ─────────────────
    // Show N listing lines around address `a`, marking the line covering `a` with =>.
    auto listSrc = [&](uint16_t a, int n){
        if (prn.by_addr.empty()){ fprintf(stderr,"  (no .prn loaded — use -l/lst)\n"); return; }
        auto cur = prn.by_addr.upper_bound(a);     // first entry > a
        if (cur != prn.by_addr.begin()) --cur;     // largest <= a (the line covering a)
        uint16_t cur_addr = cur->first;
        auto it = cur;
        for (int b=0; b<n/2 && it!=prn.by_addr.begin(); ++b) --it;
        uint16_t end = it->first;
        for (int k=0; k<n && it!=prn.by_addr.end(); ++k,++it){
            fprintf(stderr,"  %s %04X  %s\n", it->first==cur_addr?"=>":"  ", it->first, it->second.c_str());
            end = it->first;
        }
        last_list = (uint16_t)(end+1); last_list_set = true;
    };

    auto showDisplays = [&]{
        if (displays.empty()) return;
        Snap& s = (hit_cpu==2)?snap2:snap1;
        for (size_t i=0;i<displays.size();++i){
            bool ok; long v=readOperand(s,displays[i],ok);
            fprintf(stderr,"  disp[%zu] %-10s = %ld (0x%lX)\n",i,displays[i].c_str(),v,(unsigned long)(v&0xFFFF));
        }
    };
    // #5: a CPU parked at 0x0038 executing 0xFF (RST 38H) is the classic signature
    // of a memory-disable / read-gate problem (the fetch reads 0xFF because RAM is
    // gated off) — flag it automatically so it needn't be deduced by hand.
    auto rst38Hint = [&]{
        if (m.cpuPC()==0x0038 && m.memReadDebug(0x0038)==0xFF)
            fprintf(stderr,"  ⚠ ZVE1 @0038 mit [0038]=FF — RST-38-Schleife: "
                           "Fetch liest 0xFF (Speicher gegated/disabled?), kein echter RST-Handler\n");
    };
    auto onStop = [&]{
        fprintf(stderr,"** %s : ZVE%d PC=%04X\n",stop_reason.c_str(),hit_cpu,hit_pc);
        printSnap(hit_cpu);
        showInsn("=>", hit_pc);
        showDisplays();
        stateLine();
        rst38Hint();
        // §17: remember the full state at each PC-breakpoint stop so `rc` can jump back
        // to the previous hit (the snapshot ring only holds coarse pre-command states).
        if (stop_reason.rfind("bp",0)==0){
            bphit_ring.emplace_back(); m.captureState(bphit_ring.back());
            while (bphit_ring.size()>bphit_cap) bphit_ring.pop_front();
        }
    };
    auto screen = [&]{
        for (int row=0;row<24;++row){ char ln[81];
            for (int c=0;c<80;++c){ uint8_t ch=m.memReadDebug((uint16_t)(0xF800+row*80+c));
                ln[c]=(ch>=0x20&&ch<0x7F)?(char)ch:'.'; }
            ln[80]=0; fprintf(stderr,"  |%s|\n",ln); }
    };
    // ── screen text as a condition (doc/feature_requests/interaktive_programme.md #1/#5) ──
    // Render one 80-char row of the text VRAM (0xF800) into `out` (no trailing NUL
    // handling needed by callers — 80 chars). Non-printable → space (so matches
    // survive control bytes / the cursor-flag high bit).
    auto vramRow = [&](int row, char* out){
        for (int c=0;c<80;++c){ uint8_t ch=m.memReadDebug((uint16_t)(0xF800+row*80+c));
            out[c]=(ch>=0x20&&ch<0x7F)?(char)ch:' '; } out[80]=0;
    };
    // A `pat` of the form /re/ is treated as an ECMAScript regex, otherwise a
    // literal substring. `pat` is matched per-row (so it need not span the 80-col
    // wrap). Returns true and (if row/col given) the first hit position.
    auto screenFind = [&](const std::string& pat, int* hitRow, int* hitCol)->bool{
        bool rx = pat.size()>=2 && pat.front()=='/' && pat.back()=='/';
        std::regex re; if (rx){ try{ re.assign(pat.substr(1,pat.size()-2)); }catch(...){ rx=false; } }
        for (int row=0;row<24;++row){ char ln[81]; vramRow(row,ln); std::string s(ln);
            if (rx){ std::smatch mo; if(std::regex_search(s,mo,re)){
                        if(hitRow)*hitRow=row; if(hitCol)*hitCol=(int)mo.position(0); return true; } }
            else   { size_t p=s.find(pat); if(p!=std::string::npos){
                        if(hitRow)*hitRow=row; if(hitCol)*hitCol=(int)p; return true; } } }
        return false;
    };
    auto screenContains = [&](const std::string& pat)->bool{ return screenFind(pat,nullptr,nullptr); };
    // ZVE2 is the active bus master when /BUSRQ is asserted and it is neither in reset
    // nor waiting. Used by the discoverability hint (§0) and `where` (§4).
    auto zve2Active = [&]{ return m.isBUSRQ() && !m.isZVE2InReset() && !m.isZVE2Waiting(); };
    // §13 disk verify: read the ORIGINAL image file (not the mounted COW copy) track by
    // track and report sector-ID + CRC health — answers "is the medium good?" in one
    // command (replaces the ad-hoc scpx_dump.cpp harness). Only problem tracks are listed.
    auto diskVerify = [&](const char* path, const char* label){
        if (!path){ fprintf(stderr,"  (kein Image auf %s)\n",label); return; }
        auto img = DiskImage::open(path, std::nullopt, /*write_protect=*/true);
        if (!img){ fprintf(stderr,"  '%s' nicht öffenbar (self-describing .hfe ok; rohe .img "
                                  "braucht ein bekanntes Format)\n",path); return; }
        DiskGeometry g = img->geometry();
        fprintf(stderr,"  %s %s — Geometrie %u Zyl × %u Kopf, %s\n",label,path,
                (unsigned)g.num_cyls,(unsigned)g.num_heads, g.encoding==Encoding::FM?"FM":"MFM");
        long total_sec=0, bad_crc=0; int bad_tracks=0, empty_tracks=0;
        for (uint8_t c=0;c<g.num_cyls;++c) for (uint8_t h=0;h<g.num_heads;++h){
            TrackImage t = img->readTrack(c,h);
            if (t.empty()){ ++empty_tracks; continue; }
            auto secs = TrackCodec::parseTrack(t);
            int tbad=0; for (auto& s: secs) if(!s.id_crc_ok || !s.data_crc_ok) ++tbad;
            total_sec += (long)secs.size(); bad_crc += tbad;
            if (secs.empty() || tbad){ ++bad_tracks;
                fprintf(stderr,"    C%2u H%u: %zu Sekt, %d CRC-Fehler%s\n",
                        (unsigned)c,(unsigned)h,secs.size(),tbad,
                        secs.empty()?" (KEINE Marken!)":""); }
        }
        fprintf(stderr,"  → %d Spuren, %ld Sektoren, %ld CRC-Fehler, %d Problem-Spuren, %d leer%s\n",
                g.num_cyls*g.num_heads, total_sec, bad_crc, bad_tracks, empty_tracks,
                (bad_crc==0 && bad_tracks==0)?"   ✓ OK":"");
    };
    // §14 snap diff: which registers (both CPUs) and which RAM ranges changed between two
    // named snapshots — nails "ZVE1 ran ahead and clobbered [0000]" type divergences.
    auto snapDiff = [&](const std::string& na, const std::string& nb,
                        const A5120Machine::MachineSnapshot& a,
                        const A5120Machine::MachineSnapshot& b){
        fprintf(stderr,"  diff %s → %s   (Δcyc=%lld)\n", na.c_str(), nb.c_str(),
                (long long)(b.zve1.cycles - a.zve1.cycles));
        auto dumpRegs=[&](const char* who,
                          const A5120Machine::MachineSnapshot::Z80Regs& ra,
                          const A5120Machine::MachineSnapshot::Z80Regs& rb){
            struct R{ const char* n; uint16_t va,vb; };
            R rs[]={{"PC",ra.PC,rb.PC},{"SP",ra.SP,rb.SP},{"AF",ra.AF,rb.AF},{"BC",ra.BC,rb.BC},
                    {"DE",ra.DE,rb.DE},{"HL",ra.HL,rb.HL},{"IX",ra.IX,rb.IX},{"IY",ra.IY,rb.IY}};
            bool any=false;
            for (auto& r: rs) if (r.va!=r.vb){ if(!any){fprintf(stderr,"    %s:",who);any=true;}
                fprintf(stderr," %s %04X→%04X",r.n,r.va,r.vb); }
            if (any) fprintf(stderr,"\n");
        };
        dumpRegs("ZVE1",a.zve1,b.zve1); dumpRegs("ZVE2",a.zve2,b.zve2);
        long changed=0; int runs=0; int rlo=-1; const int SHOW=40;
        for (int i=0;i<=65536;++i){ bool d = (i<65536) && (a.ram[i]!=b.ram[i]);
            if (d){ ++changed; if(rlo<0) rlo=i; }
            else if (rlo>=0){ if(runs<SHOW) fprintf(stderr,"    RAM %04X..%04X (%d B)\n",rlo,i-1,i-rlo);
                              ++runs; rlo=-1; } }
        fprintf(stderr,"  → %ld RAM-Byte(s) in %d Bereich(en)%s\n",changed,runs,
                runs>SHOW?"  (nur erste 40 gelistet)":"");
    };
    // §4 `where`/`w`: one glance at BOTH CPUs + the floppy — ZVE1/ZVE2 PC+disasm, /BUSRQ,
    // current bus master, and the K5122 head/transfer state. `--json` for agents.
    auto whereShow = [&](bool json){
        auto k=m.k5122State();
        uint16_t pc1=m.cpuPC(); const Z80& z2=m.zve2Debug(); uint16_t pc2=z2.PC;
        const char* z2s = m.isZVE2InReset()?"reset":(m.isZVE2Waiting()?"wait":"run");
        if (json){
            fprintf(stderr,
              "\n{\"zve1_pc\":\"0x%04X\",\"zve2_pc\":\"0x%04X\",\"zve2\":\"%s\",\"busrq\":%s,"
              "\"busmaster\":\"%s\",\"k5122\":{\"drive\":%u,\"mounted\":%s,\"cyl\":%u,\"head\":%u,"
              "\"transferring\":%s,\"write\":%s,\"headPos\":%zu,\"trackLen\":%zu}}\n",
              pc1,pc2,z2s, m.isBUSRQ()?"true":"false", zve2Active()?"ZVE2":"ZVE1",
              (unsigned)k.drive,k.mounted?"true":"false",(unsigned)k.cylinder,(unsigned)k.head,
              k.transferring?"true":"false",k.writeMode?"true":"false",k.headPos,k.trackLen);
            return;
        }
        char l1[120],l2[120]; disasmAt(pc1,l1,sizeof l1); disasmAt(pc2,l2,sizeof l2);
        std::string p1=prnFor(pc1), p2=prnFor(pc2);
        fprintf(stderr,"  ZVE1 %s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str());
        fprintf(stderr,"  ZVE2 %s%s%s   [%s]\n",l2,p2.empty()?"":"  ; ",p2.c_str(),z2s);
        fprintf(stderr,"  BUSRQ=%s  bus-master=%s  K5122: D%d %s cyl=%u head=%u %s%s headPos=%zu/%zu\n",
                m.isBUSRQ()?"yes":"no", zve2Active()?"ZVE2":"ZVE1",
                k.drive,k.mounted?"mounted":"EMPTY",(unsigned)k.cylinder,(unsigned)k.head,
                k.transferring?"READING":"idle",k.writeMode?"+WRITE":"",k.headPos,k.trackLen);
        // §7: welche Uhr Lauf-Budgets zählen — und wie weit sie auseinanderlaufen.
        fprintf(stderr,"  Takte: ZVE1=%llu  Maschine=%llu  (Lauf-Uhr = %s)\n",
                (unsigned long long)m.cpuCycles(), (unsigned long long)m.machineCycles(),
                clock_machine?"Maschine":"ZVE1");
        rst38Hint();
    };
    // §7 Lauf-Uhr: BEIDE CPUs. `m.cpuCycles()` ist die ZVE1-Uhr; hält ZVE2 den Bus
    // (DMA — oder ein abgestürztes ZVE2, das Millionen Instruktionen dreht), steht
    // sie fast still und ein `g 20000000` läuft minutenlang, obwohl die Maschine
    // längst weit gekommen ist. `clock zve1` schaltet auf das alte Verhalten zurück.
    auto runClock = [&]()->uint64_t{ return clock_machine? m.machineCycles() : m.cpuCycles(); };
    // §7 Fortschrittsanzeige: bei langen Läufen alle 2 s eine Zeile auf stderr, damit
    // „ZVE2 dreht durch" sofort sichtbar ist statt als toter Debugger.
    auto runProgress = [&](uint64_t start, std::chrono::steady_clock::time_point& last)->void{
        using namespace std::chrono;
        auto now = steady_clock::now();
        if (now - last < seconds(2)) return;
        last = now;
        fprintf(stderr,"  … %llu cyc (%s)  ZVE1 PC=%04X  ZVE2 PC=%04X  busrq=%s  [Ctrl-C bricht ab]\n",
                (unsigned long long)(runClock()-start), clock_machine?"Maschine":"ZVE1",
                m.cpuPC(), m.zve2PC(), m.isBUSRQ()?"yes":"no");
    };
    // §9 `hist <cycles> [lo hi]`: run N cycles profiling BOTH CPUs' PCs, print the
    // hotspots (with symbol/.prn annotation). One glance instead of reading a trace file.
    auto runHist = [&](uint64_t cycles, int lo, int hi){
        hist1.clear(); hist2.clear(); hist_lo=lo; hist_hi=hi; hist_on=true;
        uint64_t start=runClock(); m.clearStop(); hit=false;
        g_int_flag=0; g_in_run=1;
        auto last_prog = std::chrono::steady_clock::now();
        while (runClock()-start < cycles){ int n=m.run(50000); if(n==0) break;
            if (g_int_flag){ fprintf(stderr,"\n  ^C — Lauf abgebrochen\n"); break; }
            runProgress(start,last_prog); }
        g_in_run=0;
        hist_on=false;
        uint64_t ran=runClock()-start;
        fprintf(stderr,"hist over %llu cyc",(unsigned long long)ran);
        if(lo>=0) fprintf(stderr," in [%04X..%04X]",(uint16_t)lo,(uint16_t)hi);
        fprintf(stderr,":\n");
        auto top=[&](std::map<uint16_t,uint32_t>& h, const char* who){
            if(h.empty()){ fprintf(stderr,"  %s: (no samples)\n",who); return; }
            std::vector<std::pair<uint32_t,uint16_t>> v; uint64_t tot=0;
            for(auto&kv:h){ v.push_back({kv.second,kv.first}); tot+=kv.second; }
            std::sort(v.rbegin(),v.rend());
            fprintf(stderr,"  %s top (%llu instrs, %zu distinct PCs):\n",
                    who,(unsigned long long)tot,h.size());
            for(size_t i=0;i<v.size() && i<15;++i){ uint16_t a=v[i].second;
                std::string s=symFor(a), p=prnFor(a);
                fprintf(stderr,"    %6.2f%%  %6u  %04X%s%s%s%s%s\n",
                    100.0*v[i].first/(double)tot, v[i].first, a,
                    s.empty()?"":" <",s.c_str(),s.empty()?"":">",
                    p.empty()?"":"  ; ",p.c_str()); }
        };
        top(hist1,"ZVE1"); top(hist2,"ZVE2");
    };
    // silent run kernel: runs until a stop is signalled or budget/cap reached.
    // Beim Fortsetzen wird die aktuelle Adresse beider CPUs als „einmal nicht halten"
    // vorgemerkt — sonst hielte ein Breakpoint, auf dem wir gerade STEHEN, sofort wieder
    // (break-before-execute, s. tools/k1520dbg.md §2).
    auto armResume = [&]{ resume_skip1 = (int)m.cpuPC(); resume_skip2 = (int)m.zve2PC(); };
    auto goSilent = [&](uint64_t budget)->uint64_t{
        hit=false; m.clearStop(); tw_n=0; armResume();
        uint64_t start=runClock();
        uint64_t cap = budget? budget : 400000000ULL;     // safety cap for bare `g`
        g_int_flag=0; g_in_run=1;
        auto last_prog = std::chrono::steady_clock::now();
        while (!hit){
            if (runClock()-start>=cap) break;
            int n=m.run(50000); if(n==0) break;
            // #1 bscreen: stop as soon as the screen shows the armed pattern.
            if (!screen_bp.empty() && screenContains(screen_bp))
                stopFromBus("bscreen \""+screen_bp+"\"");
            if (g_int_flag){ fprintf(stderr,"\n  ^C — Lauf abgebrochen\n"); break; }
            runProgress(start,last_prog);
        }
        g_in_run=0;
        return runClock()-start;
    };
    auto go = [&](uint64_t budget){
        uint64_t ran=goSilent(budget);
        if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); }
        else { fprintf(stderr,"   ran %llu cyc, no breakpoint (PC=%04X)\n",
                     (unsigned long long)ran,m.cpuPC()); stateLine(); }
    };
    // #1 screen-conditioned run: run until the text VRAM contains `pat` (literal or
    // /regex/), a breakpoint hits, or the cycle cap is reached. Returns cycles run;
    // sets `matched`/`hit` so the caller can report which happened. Makes menu
    // navigation deterministic instead of guessing `g <cycles>`.
    auto goUntilScreen = [&](const std::string& pat, uint64_t cap, bool& matched)->uint64_t{
        hit=false; matched=false; m.clearStop(); tw_n=0; armResume();
        uint64_t start=runClock();
        if (cap==0) cap=400000000ULL;
        if (screenContains(pat)){ matched=true; return 0; }
        g_int_flag=0; g_in_run=1;
        auto last_prog = std::chrono::steady_clock::now();
        while (!hit){
            if (runClock()-start>=cap) break;
            int n=m.run(50000); if(n==0) break;
            if (screenContains(pat)){ matched=true; break; }
            if (g_int_flag){ fprintf(stderr,"\n  ^C — Lauf abgebrochen\n"); break; }
            runProgress(start,last_prog);
        }
        g_in_run=0;
        return runClock()-start;
    };
    // Step OVER one ZVE1 instruction (silent — caller prints the result).
    // For CALL and repeating block ops (LDIR/INIR…) "over" means: don't descend —
    // set a one-shot run-until at the instruction's fall-through address and run.
    // Everything else is a plain single step.
    auto stepOver = [&]{
        z80dis::Insn d = z80dis::decode(rd1, m.cpuPC());
        if (d.is_call || d.is_repeat){ gu_pc=(int)(uint16_t)(m.cpuPC()+d.len); goSilent(0); }
        else { step_rem=1; step_active=true; m.clearStop(); armResume();
               while(step_active){ int n=m.run(20000); if(n==0||hit)break; } }
    };
    // Decode the key token starting at s[i], advancing i past it (the caller's for-loop
    // does the final ++i). Escapes: `\r`/`\n`→Enter, `\t`→Tab, `\e`→ESC, `\s`→Space
    // (#3: sending a bare space is otherwise awkward), `\xNN`→raw hex code.
    auto decodeKey = [&](const std::string& s, size_t& i)->uint32_t{
        char c=s[i];
        if (c=='\\' && i+1<s.size()){ char e=s[++i];
            if (e=='x' && i+2<s.size()){
                auto hx=[&](char h)->int{ if(h>='0'&&h<='9')return h-'0'; h=(char)tolower(h);
                    return (h>='a'&&h<='f')?10+h-'a':-1; };
                int hi=hx(s[i+1]), lo=hx(s[i+2]);
                if(hi>=0&&lo>=0){ i+=2; return (uint32_t)(hi*16+lo); } }
            switch(e){ case 'r': case 'n': return 0x01000004; case 't': return 0x09;
                       case 'e': return 0x1B; case 's': return 0x20; default: return (uint8_t)e; } }
        return (uint8_t)c;
    };
    // Extract one argument from `s` starting at index `i`: a "quoted"/'quoted'
    // string (kept verbatim, spaces allowed) or a bare whitespace-delimited token.
    // Returns the index just past the argument. Used by gscreen/bscreen/keyuntil,
    // whose screen-text args may contain spaces (the plain tokenizer would split).
    auto extractArg = [](const std::string& s, size_t i, std::string& out)->size_t{
        while(i<s.size() && isspace((unsigned char)s[i])) ++i;
        out.clear();
        if(i<s.size() && (s[i]=='"'||s[i]=='\'')){ char q=s[i++];
            while(i<s.size() && s[i]!=q) out+=s[i++]; if(i<s.size()) ++i; }
        else while(i<s.size() && !isspace((unsigned char)s[i])) out+=s[i++];
        return i;
    };
    // Inject keystrokes while the machine keeps running. Each char is pressed, run a
    // little (so the BIOS keyboard poll picks it up), released, run a little more.
    // Stops early if a breakpoint hits.
    auto keys = [&](const std::string& t){
        for (size_t i=0;i<t.size();++i){
            uint32_t code=decodeKey(t,i);
            m.keyPress(code,false,false); go(600000); if(hit) return;
            m.keyRelease(code);          go(150000); if(hit) return;
        }
    };
    // #3 keyuntil: press ONE key repeatedly until the screen shows `pat` (robust
    // against direct-poll keyboard loss — HARDY polls the SIO directly, so a
    // single fixed-timing key can be missed if the program is not polling right
    // then). Returns true if `pat` appeared, false on cap/breakpoint.
    auto keyUntil = [&](const std::string& keyspec, const std::string& pat, uint64_t cap)->bool{
        size_t i=0; uint32_t code=decodeKey(keyspec,i);
        uint64_t start=m.cpuCycles(); if(cap==0) cap=200000000ULL; bool matched=false;
        while (m.cpuCycles()-start < cap){
            if (screenContains(pat)) return true;
            m.keyPress(code,false,false);
            goUntilScreen(pat,700000,matched); if(hit) return false; if(matched){ m.keyRelease(code); return true; }
            m.keyRelease(code);
            goUntilScreen(pat,200000,matched); if(hit) return false; if(matched) return true;
        }
        return screenContains(pat);
    };
    // set a register (ZVE1 default, cpu=2 → ZVE2)
    auto setReg = [&](int cpu, std::string name, long v)->bool{
        Z80& z = (cpu==2)? m.zve2Debug() : m.cpuDebug();
        for(auto&c:name) c=(char)toupper(c);
        auto seth=[&](uint16_t& rr,long val){ rr=(uint16_t)((rr&0x00FF)|((val&0xFF)<<8)); };
        auto setl=[&](uint16_t& rr,long val){ rr=(uint16_t)((rr&0xFF00)|(val&0xFF)); };
        if(name=="A")seth(z.AF,v); else if(name=="F")setl(z.AF,v);
        else if(name=="B")seth(z.BC,v); else if(name=="C")setl(z.BC,v);
        else if(name=="D")seth(z.DE,v); else if(name=="E")setl(z.DE,v);
        else if(name=="H")seth(z.HL,v); else if(name=="L")setl(z.HL,v);
        else if(name=="AF")z.AF=(uint16_t)v; else if(name=="BC")z.BC=(uint16_t)v;
        else if(name=="DE")z.DE=(uint16_t)v; else if(name=="HL")z.HL=(uint16_t)v;
        else if(name=="IX")z.IX=(uint16_t)v; else if(name=="IY")z.IY=(uint16_t)v;
        else if(name=="SP")z.SP=(uint16_t)v; else if(name=="PC")z.PC=(uint16_t)v;
        else if(name=="I")z.I=(uint8_t)v; else if(name=="R")z.R=(uint8_t)v;
        else return false;
        return true;
    };
    // heuristic backtrace: scan the stack for plausible return addresses
    // Heuristic backtrace: scan the stack for plausible return addresses (fallback /
    // `bt scan`). Used when no call-stack history exists (e.g. right after restore).
    auto backtraceScan = [&](int depth){
        uint16_t sp = snap1.valid? snap1.SP : m.cpuSP();
        fprintf(stderr,"  #0 %04X", snap1.valid? snap1.PC : m.cpuPC());
        { std::string s=symFor(snap1.valid?snap1.PC:m.cpuPC()); if(!s.empty()) fprintf(stderr," <%s>",s.c_str()); }
        fprintf(stderr,"\n"); int frame=1;
        for (int o=0; o<depth*16 && frame<=depth; o+=2){
            uint16_t w = (uint16_t)(m.memReadDebug(sp+o)|(m.memReadDebug(sp+o+1)<<8));
            uint8_t pre3=m.memReadDebug((uint16_t)(w-3));
            bool looksCall = (pre3==0xCD) || ((pre3&0xC7)==0xC4);   // CALL nn / CALL cc nn
            if (looksCall){
                fprintf(stderr,"  #%d %04X (ret, via [%04X])", frame, w, (uint16_t)(sp+o));
                std::string s=symFor((uint16_t)(w-3)); if(s.empty()) s=symFor(w);
                if(!s.empty()) fprintf(stderr," <%s>",s.c_str());
                fprintf(stderr,"\n"); ++frame;
            }
        }
    };
    // Exact backtrace from the live CALL/RST/RET call-stack tracker (default `bt`).
    auto backtraceHistory = [&](int depth){
        uint16_t pc = snap1.valid? snap1.PC : m.cpuPC();
        auto annot=[&](uint16_t a){ std::string s=symFor(a);
            std::string p=prnFor(a);
            std::string out; if(!s.empty()) out+=" <"+s+">"; if(!p.empty()) out+="  ; "+p; return out; };
        fprintf(stderr,"  #0 %04X%s\n", pc, annot(pc).c_str());
        const auto& f = callstack.frames();
        int frame=1;
        // #5 bt-fold: collapse runs of identical consecutive frames (e.g. an
        // endless RST 38H fetch-crash floods `bt` with 0038-frames and buries the
        // real callers). `… ×N` keeps the relevant frame visible.
        for (auto it=f.rbegin(); it!=f.rend() && frame<=depth; ){
            uint16_t site=it->site, tgt=it->target, ret=it->ret; int reps=0;
            auto j=it; while (j!=f.rend() && j->site==site && j->target==tgt && j->ret==ret){ ++reps; ++j; }
            if (reps>1) fprintf(stderr,"  #%d %04X (call → %04X, ret %04X)%s   ↻ ×%d\n",
                    frame, site, tgt, ret, annot(site).c_str(), reps);
            else        fprintf(stderr,"  #%d %04X (call → %04X, ret %04X)%s\n",
                    frame, site, tgt, ret, annot(site).c_str());
            it=j; ++frame;
        }
        if (f.empty())
            fprintf(stderr,"  (call-stack history empty — try 'bt scan', or step/run to build it)\n");
    };
    auto backtrace = [&](int depth){
        if (bt_use_history) backtraceHistory(depth); else backtraceScan(depth);
    };

    auto parseNum=[&](const std::string& s)->long{ return resolveAddr(s); };
    // "A" or "A..B" → address range (symbols/hex allowed on both ends).
    auto parseRange=[&](const std::string& tok, uint16_t& lo, uint16_t& hi){
        size_t dd=tok.find("..");
        if (dd==std::string::npos){ lo=hi=(uint16_t)resolveAddr(tok); }
        else { lo=(uint16_t)resolveAddr(tok.substr(0,dd)); hi=(uint16_t)resolveAddr(tok.substr(dd+2)); }
        if (hi<lo) std::swap(lo,hi);
    };

    // ─── snapshot / reverse-step helpers ──────────────────────────────────────
    // Push the current machine state onto the reverse-ring before a forward command.
    auto pushHistory = [&]{
        rev_ring.emplace_back();
        m.captureState(rev_ring.back());
        while (rev_ring.size() > rev_cap) rev_ring.pop_front();
    };
    // Restore a snapshot and re-sync the debugger's view (call-stack history is reset).
    auto applySnapshot = [&](const A5120Machine::MachineSnapshot& s,const char* what){
        bool ok = m.restoreState(s);
        callstack.clear();                 // call history can't be reconstructed
        snap1=grab(m.cpuDebug()); snap2=Snap{};
        if(!ok) fprintf(stderr,"  note: ROM-mapping differs from snapshot — RAM+regs restored,"
                               " but this snapshot predates/postdates the boot-ROM unmap.\n");
        fprintf(stderr,"  restored %s\n",what);
        showInsn("=>",m.cpuPC()); stateLine();
    };
    // Reverse-step: undo the last N forward commands.
    auto reverseStep = [&](long n){
        if (rev_ring.empty()){ fprintf(stderr,"  no reverse history (run/step something first)\n"); return; }
        A5120Machine::MachineSnapshot s;
        for (long i=0;i<n && !rev_ring.empty();++i){ s=rev_ring.back(); rev_ring.pop_back(); }
        char w[48]; snprintf(w,sizeof w,"%ld step(s) back (%zu left)",n,rev_ring.size());
        applySnapshot(s,w);
    };
    // §17 reverse-continue: jump back to the PREVIOUS breakpoint hit (from the dedicated
    // bp-hit ring filled in onStop). The last entry is the CURRENT hit, so drop it and
    // restore the one before. Reliable — no PC guessing against post-instruction snapshots.
    auto reverseContinue = [&](){
        if (bphit_ring.size() < 2){
            fprintf(stderr,"  no earlier breakpoint hit in history (need ≥2 bp stops)\n"); return; }
        bphit_ring.pop_back();                                 // discard the current hit
        A5120Machine::MachineSnapshot s = bphit_ring.back();   // the previous one
        char w[64]; snprintf(w,sizeof w,"reverse-continue → previous bp hit PC=%04X (%zu left)",
                             s.zve1.PC,bphit_ring.size());
        applySnapshot(s,w);
    };

    // ═══ Phase 4: the REPL ══════════════════════════════════════════════════════
    // Commands come from the -x script first (queued in `pending`), then stdin.
    // Each line is whitespace-tokenised into `t`; t[0] is the command, t[1..] the
    // args. Dispatch is one big if/else-if chain below — grouped, in the same order
    // as `help`: RUN · REVERSE · BREAK · WATCH · LOG · INSPECT · MEM · MISC. The
    // section banners (// ── … ──) are navigation anchors only; add new commands to
    // the matching group and mirror them in the `help` text and tools/k1520dbg.md.
    std::deque<std::string> pending;       // -x script lines, consumed before stdin
    if (script){ std::ifstream f(script); std::string l; while(std::getline(f,l)) pending.push_back(l); }
    for (auto& sf : symfiles) loadSyms(sf);       // apply -s symbol files
    for (auto& pf : prnfiles) loadPrnSpec(pf);    // apply -l .prn listings (also imports labels)

    signal(SIGINT, dbgSigInt);      // §7: Ctrl-C bricht einen laufenden `g` ab, nicht die Sitzung
    fprintf(stderr,"k1520dbg — type 'help'.  Lauf-Uhr = %s (clock zve1|machine).  Disassembler: built-in.\n",
            clock_machine? "Maschine (beide CPUs)" : "ZVE1");
#ifdef HAVE_READLINE
    rl_attempted_completion_function = dbgCompletion;   // Tab → command-name completion
#endif
    std::string line;
    for (;;){
        // read one command (echo script lines so piped sessions are readable)
        if (!pending.empty()){ line=pending.front(); pending.pop_front(); fprintf(stderr,"(dbg) %s\n",line.c_str()); }
        else {
            // interactive tty → readline (line editing, history, Tab-completion);
            // pipes/scripts → plain getline with a manual prompt (unchanged behaviour).
#ifdef HAVE_READLINE
            if (k1520::os::isTerminal(0)) {
                char* rl = readline("(dbg) ");
                if (!rl) break;
                line = rl; if (rl[0]) add_history(rl); free(rl);
            } else
#endif
            { fprintf(stderr,"(dbg) "); if(!std::getline(std::cin,line)) break; }
        }
        std::istringstream is(line); std::vector<std::string> t; std::string w;
        while (is>>w) t.push_back(w);
        if (t.empty()||t[0][0]=='#') continue;     // blank line or # comment
        // alias expansion (one level, so an alias can't loop): replace t[0] by its
        // expansion and keep the user's extra args, then re-tokenise.
        { auto ait=aliases.find(t[0]);
          if(ait!=aliases.end()){ std::string ex=ait->second;
              for(size_t i=1;i<t.size();++i){ ex+=" "; ex+=t[i]; }
              std::istringstream is2(ex); t.clear(); std::string w2;
              while(is2>>w2) t.push_back(w2); if(t.empty()) continue; } }
        const std::string& cmd=t[0];

        // §0 discoverability: when ZVE2 is the current bus master, a ZVE1-only command is
        // almost always a mistake (the DMA/read runs on ZVE2). Nudge toward the 2-variants.
        if (zve2Active()){
            static const std::set<std::string> zve1only={"b","s","n","fin","gu","rj"};
            if (zve1only.count(cmd))
                fprintf(stderr,"  [hint] bus-master is ZVE2 now — '%s' acts on ZVE1; "
                               "you may want b2/s2/rj2/r 2. ('where' shows both.)\n",cmd.c_str());
        }

        // ── session ──
        if (cmd=="q"||cmd=="quit") break;
        // §0.4 topic help: `help floppy` / `help dualcpu` — the two recipes that were
        // the least discoverable (this session cost 8+ runs for want of them).
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 &&
                 (t[1]=="floppy"||t[1]=="disk"||t[1]=="k5122")){
            fprintf(stderr,
              "  FLOPPY / K5122 recipes\n"
              "    where             both CPUs + K5122 head/xfer at a glance (--json for agents)\n"
              "    dev               K5122 state: drive/cyl/head/READING/headPos ; dev ctc|pio|sio\n"
              "    bxfer [start|end] break when a K5122 read-transfer begins / ends\n"
              "    bbusrq [assert|release]  break on a /BUSRQ edge (DMA hand-off)\n"
              "    wp EBFA           watch the SCPX track register ; wp EC00..EC0F changed  (template)\n"
              "    iow 16 ; iow 14   watch the K5122 data / ctrl ports\n"
              "    -s tools/scpx1526.sym   load SCPX BIOS labels (matcher/poll_wait/…)\n"
              "  Read fails (BAD SECTOR)? head+sectors are on the card; the target-compare is CPU-side:\n"
              "    b2 <matcher>  (NOT b — the matcher runs on ZVE2!) ; r 2 ; wp EC0C..EC0E changed\n");
        }
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 &&
                 (t[1]=="dualcpu"||t[1]=="zve2"||t[1]=="dma")){
            fprintf(stderr,
              "  DUAL-CPU (ZVE1 main / ZVE2 DMA) recipes\n"
              "    During a DMA the bus master is ZVE2 — ZVE1-only commands act on the wrong CPU:\n"
              "      b2 <A>          breakpoint on ZVE2 (b = ZVE1 only!)\n"
              "      s2 [N]          step ZVE2 ;  r 2 / rj2   ZVE2 registers (text / JSON)\n"
              "      where           ZVE1 PC + ZVE2 PC + /BUSRQ + bus master in one line\n"
              "      hist <cyc>      PC hotspots of BOTH CPUs (finds the spin loop instantly)\n"
              "    bbusrq / bxfer    stop exactly at the DMA hand-off / read-transfer edge\n"
              "    A ZVE1-only command while ZVE2 is bus master prints a [hint].\n");
        }
        else if (cmd=="help"||cmd=="h"||cmd=="?"){
            fprintf(stderr,
              "  RUN     g/c [N]   run to breakpoint (or N MASCHINEN-Takte; Ctrl-C bricht ab)\n"
              "          clock [zve1|machine]   welche Uhr die Lauf-Budgets zaehlt\n"
              "          gu <A>    run until ZVE1 reaches A (temp bp)\n"
              "          s [N]     step INTO N ZVE1 instrs ;  s2 [N] step ZVE2\n"
              "          n [N]     step OVER N ZVE1 instrs (skip CALL/blockrepeat)\n"
              "          fin       step OUT (run until SP rises above current frame)\n"
              "  REVERSE rs [N]    reverse-step: undo last N forward commands (snapshot ring)\n"
              "          rc        reverse-continue: jump back to the previous breakpoint hit\n"
              "          snap <name> | snap list | snap diff <a> <b> ; restore <name>   full snapshots\n"
              "  BREAK   b <A> [if <cond>] | b2 <A> ...   bp on ZVE1 / ZVE2\n"
              "          tb <A>    temporary (one-shot) bp ; bd/bd2 <A> delete ; bl list\n"
              "          be/bdis <A> (be2/bdis2) enable/disable ; bi/bi2 <A> <N> ignore N hits\n"
              "          bint | bnmi | breti [on|off]   break on interrupt / NMI / RETI (ZVE1)\n"
              "          bbusrq | bxfer [read|write] [assert|release|off] [if <cond>]   /BUSRQ / K5122-xfer edge\n"
              "          cond: REG/[addr]/[addr]w/(rr)  OP  value   OP: == != < > <= >=\n"
              "  WATCH   wp/wpr/wb <A|A..B> [==v|!=v|changed]   mem watch (range+cond):\n"
              "                          print-write / print-read / break-write\n"
              "          wd <A>|all  wl  delete (covering A) / list mem watches\n"
              "          iow/iob <P>     io port: print / break ; iod <P> wl-io: iol\n"
              "  LOG     logpoint <A> [expr..]  print + CONTINUE (dprintf) ; lpd <A> ; lpl\n"
              "          trace <file> [lo hi]   log every executed instr to file ; trace off\n"
              "          itrace <file>          log every accepted INT/NMI (cycle, int@PC, ISR, SP,\n"
              "                                 Vektor + Quellgeraet/SPURIOUS) ; itrace off\n"
              "  INSPECT r [2]     registers (ZVE1, +ZVE2) ; rj / rj2 registers as JSON (ZVE1/ZVE2)\n"
              "          where/w [--json]   BOTH CPUs + /BUSRQ + K5122 head/xfer at a glance\n"
              "          hist <cyc> [lo hi] PC hotspots of both CPUs over a cycle window ; bt [N] backtrace\n"
              "          d/dump <A> [N] hexdump ; dump <A> <N> <datei>  RAM -> Binaerdatei\n"
              "          u [A] [N] disasm (bricht in unbeschriebenem Speicher ab) ; e <A> <b..> poke\n"
              "          x/<N><fmt><sz> <A> | x <A> [N]  examine (fmt x/d/u/c/t/o/a/i/s, sz b/w); x continues\n"
              "          list/l [A] [N]  .prn source lines around A (labels load as symbols)\n"
              "          set [2] <reg> <v>   edit register ; vars [-f <f>|add <n> <A> [w]|clear]  RAM dashboard\n"
              "          dev [ctc|pio [all|bs|k5122ctrl|k5122data]|sio|sio2]   chip state (default K5122)\n"
              "          ivt [all|2]     IM-2-Vektortabelle: Vektor/Tabelle/Eintrag/Geraet + Status\n"
              "          disk verify [B]   Sektor-/CRC-Health aller Spuren des Images\n"
              "          disp <expr> | undisp <n> | disp   show expr at every stop\n"
              "  MEM     load <f> <A>   read binary into RAM ; save <f> <A> <N> dump RAM\n"
              "          verify <datei> @<A> [N]   Datei mit dem RAM vergleichen (Build-Abgleich)\n"
              "          savestate <f> | loadstate <f>   full machine state (boot once, resume)\n"
              "  MISC    mark [A]  zero relative cycle counter (now / armed at A)\n"
              "          sym <f> | sym add <name> <A> | sym list\n"
              "          lst <f>[@off|@auto] | lst <f> <off> | lst list   Listing/Quelle → annotate\n"
              "                    <f> = .prn-Listing ODER .MAC/.ASM-Quelltext (wird assembliert)\n"
              "                    @auto = Ladeversatz aus den Objektbytes im RAM bestimmen\n"
              "                    @labels/@noanchor (nur .MAC): Mxxxx-Adressanker erzwingen/abschalten\n"
              "          keys <text> (\\r \\t \\e \\s \\xNN) ; screen [find \"txt\"] ; reset ; q\n"
              "          gscreen \"txt\"|/re/ [maxcyc]   run until screen shows txt (deterministic menus)\n"
              "          bscreen \"txt\"|/re/ | off      arm: any g/gu/n stops on screen match\n"
              "          keyuntil \"<key>\" \"txt\" [maxcyc]  press key until screen shows txt (poll-robust)\n"
              "          dialog <file>   drive a menu: per line  \"screen-txt\" \"keys\" [maxcyc]\n"
              "          alias <name> <expansion..> | unalias <name> | alias ; source <file>\n");
        }
        // ══ RUN: continue / step (each snapshots first via pushHistory for `rs`) ══
        else if (cmd=="g"||cmd=="c"){ pushHistory(); go(t.size()>1? (uint64_t)parseNum(t[1]) : 0); }
        else if (cmd=="gu" && t.size()>1){ pushHistory(); gu_pc=(int)(uint16_t)parseNum(t[1]); go(0); }
        else if (cmd=="s"){ pushHistory(); step_rem=t.size()>1?parseNum(t[1]):1; step_active=true;
            m.clearStop(); armResume();
            while(step_active){ int n=m.run(20000); if(n==0||hit) break; }
            step_active=false;
            if(hit){ hit=false; onStop(); } else stateLine(); }
        else if (cmd=="s2"){ pushHistory(); step2_rem=t.size()>1?parseNum(t[1]):1; step2_active=true;
            m.clearStop(); armResume();
            while(step2_active){ int n=m.run(20000); if(n==0||hit) break; }
            step2_active=false;
            if(hit){ hit=false; onStop(); } else { fprintf(stderr,"  (ZVE2 did not run — /BUSRQ not asserted?)\n"); stateLine(); } }
        else if (cmd=="n"){ pushHistory(); long k=t.size()>1?parseNum(t[1]):1;
            for(long i=0;i<k;++i){ stepOver(); if(hit) break; }
            if(hit){ hit=false; onStop(); } else { snap1=grab(m.cpuDebug()); showInsn("=>",m.cpuPC()); stateLine(); } }
        else if (cmd=="fin"){ pushHistory(); fin_sp=m.cpuSP(); fin_active=true; go(0); }
        // ══ REVERSE: reverse-step + named snapshots ══
        else if (cmd=="rs"||cmd=="bs") reverseStep(t.size()>1?parseNum(t[1]):1);
        else if (cmd=="rc") reverseContinue();   // §17 reverse-continue to previous bp hit
        else if (cmd=="snap"){
            if (t.size()>=2 && t[1]=="list"){
                if(named_snaps.empty()) fprintf(stderr,"  (no named snapshots)\n");
                for(auto&kv:named_snaps) fprintf(stderr,"  %-16s PC=%04X cyc=%llu\n",
                        kv.first.c_str(),kv.second.zve1.PC,(unsigned long long)kv.second.zve1.cycles); }
            else if (t.size()>=4 && t[1]=="diff"){   // §14 snap diff <a> <b>
                auto ia=named_snaps.find(t[2]), ib=named_snaps.find(t[3]);
                if(ia==named_snaps.end()||ib==named_snaps.end())
                    fprintf(stderr,"  snapshot '%s' oder '%s' fehlt (snap list)\n",t[2].c_str(),t[3].c_str());
                else snapDiff(t[2],t[3],ia->second,ib->second); }
            else if (t.size()>=2){ m.captureState(named_snaps[t[1]]);
                fprintf(stderr,"  snapshot '%s' saved (PC=%04X)\n",t[1].c_str(),m.cpuPC()); }
            else fprintf(stderr,"  snap <name> | snap list   (restore with: restore <name>)\n"); }
        else if (cmd=="restore" && t.size()>=2){
            auto it=named_snaps.find(t[1]);
            if(it==named_snaps.end()) fprintf(stderr,"  no snapshot '%s' (snap list)\n",t[1].c_str());
            else { pushHistory(); applySnapshot(it->second,("snapshot '"+t[1]+"'").c_str()); } }
        // ══ BREAK: PC breakpoints (+cond/temp/enable/ignore) and event breakpoints ══
        else if ((cmd=="b"||cmd=="b2") && t.size()>1){
            auto& tbl = (cmd=="b2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            // "b A if <expr…>" — the condition is all tokens after `if`, space-joined.
            Bp bp; if (t.size()>3 && t[2]=="if"){ for(size_t i=3;i<t.size();++i){ if(i>3)bp.cond+=" "; bp.cond+=t[i]; } }
            tbl[a]=bp; fprintf(stderr,"  bp %s @%04X%s\n",cmd=="b2"?"ZVE2":"ZVE1",a, bp.cond.empty()?"":(" if "+bp.cond).c_str()); }
        else if (cmd=="tb" && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]); Bp bp; bp.temp=true; bp1[a]=bp;
            fprintf(stderr,"  temp bp ZVE1 @%04X\n",a); }
        else if (cmd=="bd" && t.size()>1) bp1.erase((uint16_t)parseNum(t[1]));
        else if (cmd=="bd2"&& t.size()>1) bp2.erase((uint16_t)parseNum(t[1]));
        // enable / disable (keep but inactive) — be/bdis (ZVE1), be2/bdis2 (ZVE2)
        else if ((cmd=="be"||cmd=="bdis"||cmd=="be2"||cmd=="bdis2") && t.size()>1){
            auto& tbl = (cmd=="be2"||cmd=="bdis2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            auto it=tbl.find(a);
            if(it==tbl.end()) fprintf(stderr,"  no bp @%04X\n",a);
            else { bool en=(cmd=="be"||cmd=="be2"); it->second.enabled=en;
                fprintf(stderr,"  bp %s @%04X %s\n",(cmd=="be2"||cmd=="bdis2")?"ZVE2":"ZVE1",a,en?"enabled":"disabled"); } }
        // ignore next N hits before stopping — bi/bi2 (gdb 'ignore')
        else if ((cmd=="bi"||cmd=="bi2") && t.size()>2){
            auto& tbl = (cmd=="bi2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            auto it=tbl.find(a);
            if(it==tbl.end()) fprintf(stderr,"  no bp @%04X\n",a);
            else { it->second.ignore=parseNum(t[2]);
                fprintf(stderr,"  bp %s @%04X: ignore next %ld hit(s)\n",cmd=="bi2"?"ZVE2":"ZVE1",a,it->second.ignore); } }
        else if (cmd=="bl"){
            auto show=[&](const std::map<uint16_t,Bp>& tbl,const char* cpu){
                fprintf(stderr,"  %s breakpoints:\n",cpu);
                if(tbl.empty()){ fprintf(stderr,"    (none)\n"); return; }
                for(auto&kv:tbl){ std::string s=symFor(kv.first);
                    fprintf(stderr,"    %04X%s%s%s  hits=%ld%s%s%s\n",kv.first,
                        s.empty()?"":" <",s.c_str(),s.empty()?"":">", kv.second.hits,
                        kv.second.enabled?"":" [disabled]",
                        kv.second.ignore>0?(" ignore="+std::to_string(kv.second.ignore)).c_str():"",
                        kv.second.cond.empty()?"":(" if "+kv.second.cond).c_str()); } };
            show(bp1,"ZVE1"); show(bp2,"ZVE2");
            if(brk_int||brk_nmi||brk_reti||brk_busrq||brk_xfer||brk_wxfer)
                fprintf(stderr,"  events:%s%s%s%s%s%s\n",
                brk_int?" interrupt":"",brk_nmi?" nmi":"",brk_reti?" reti":"",
                brk_busrq?" busrq":"",brk_xfer?" read-xfer":"",brk_wxfer?" write-xfer":""); }
        // event breakpoints: break on interrupt / NMI / RETI (toggle; "off" disarms)
        else if (cmd=="bint"||cmd=="bnmi"||cmd=="breti"){
            bool on = !(t.size()>1 && t[1]=="off");
            if(t.size()>1 && t[1]=="on") on=true;
            bool& flag = cmd=="bint"?brk_int : cmd=="bnmi"?brk_nmi : brk_reti;
            flag = (t.size()>1)? on : !flag;   // bare command toggles
            fprintf(stderr,"  break-on-%s %s\n", cmd=="bint"?"interrupt":cmd=="bnmi"?"nmi":"reti",
                    flag?"ON":"off"); }
        // §5/§15 floppy/bus event breakpoints: break on /BUSRQ edge (bbusrq) or K5122
        // transfer edge (bxfer). Args (in any order):
        //   [read|write]  (bxfer: read=default)   [assert|start | release|end | both | off]
        //   [if <cond…>]  (only stop when the expression holds at the edge)
        else if (cmd=="bbusrq"||cmd=="bxfer"){
            bool write=false; int mode=3; std::string cond;
            for (size_t i=1;i<t.size();++i){ const std::string& a=t[i];
                if (a=="if"){ for(size_t j=i+1;j<t.size();++j){ if(j>i+1)cond+=" "; cond+=t[j]; } break; }
                else if (a=="write") write=true;
                else if (a=="read")  write=false;
                else if (a=="off") mode=0;
                else if (a=="assert"||a=="start"||a=="on") mode=1;
                else if (a=="release"||a=="end") mode=2;
                else if (a=="both") mode=3; }
            fev_have_prev=false;   // re-baseline on next instruction
            const char* ms = mode==0?"off":mode==1?"assert/start":mode==2?"release/end":"both edges";
            const char* what;
            if (cmd=="bbusrq"){ brk_busrq=mode; ev_busrq_cond=cond; what="/BUSRQ"; }
            else if (write)   { brk_wxfer=mode; ev_wxfer_cond=cond; what="K5122-write-xfer"; }
            else              { brk_xfer =mode; ev_xfer_cond =cond; what="K5122-read-xfer"; }
            fprintf(stderr,"  break-on-%s %s%s%s\n", what, ms, cond.empty()?"":" if ", cond.c_str()); }
        // ══ LOG: run-and-log without stopping (logpoints + trace-to-file) ══
        // logpoints (dprintf-style: print + continue, never stop) on ZVE1
        else if ((cmd=="logpoint"||cmd=="lp") && t.size()>1){
            uint16_t a=(uint16_t)parseNum(t[1]);
            std::vector<std::string> ex(t.begin()+2,t.end());
            logpoints[a]=ex;
            fprintf(stderr,"  logpoint @%04X%s%s\n",a, ex.empty()?"":" exprs:",
                    ex.empty()?"":[&]{ std::string s; for(auto&e:ex){s+=" "+e;} return s; }().c_str()); }
        else if (cmd=="lpd" && t.size()>1){ logpoints.erase((uint16_t)parseNum(t[1])); fprintf(stderr,"  logpoint deleted\n"); }
        else if (cmd=="lpl"){
            if(logpoints.empty()) fprintf(stderr,"  (no logpoints)\n");
            for(auto&kv:logpoints){ std::string s=symFor(kv.first);
                fprintf(stderr,"    %04X%s%s%s ",kv.first,s.empty()?"":" <",s.c_str(),s.empty()?"":">");
                for(auto&e:kv.second) fprintf(stderr,"%s ",e.c_str()); fprintf(stderr,"\n"); } }
        // continuous trace-to-file: `trace <file> [lo hi]` ; `trace off`
        else if (cmd=="trace"){
            if (t.size()>=2 && t[1]=="off"){
                if(trace_fp){ fclose(trace_fp); fprintf(stderr,"  trace off (%ld line(s) written)\n",trace_lines); }
                else fprintf(stderr,"  trace was not on\n");
                trace_fp=nullptr; trace_lo=trace_hi=-1; trace_lines=0; trace_capped=false; }
            else if (t.size()>=2){
                if(trace_fp) fclose(trace_fp);
                trace_fp=fopen(t[1].c_str(),"w"); trace_lines=0; trace_capped=false;
                trace_lo=trace_hi=-1;
                if(!trace_fp){ fprintf(stderr,"  cannot open %s for writing\n",t[1].c_str()); }
                else { if(t.size()>=4){ trace_lo=(int)(uint16_t)parseNum(t[2]); trace_hi=(int)(uint16_t)parseNum(t[3]); }
                    fprintf(stderr,"  trace → %s%s (every executed instr; cap %ld lines, 'trace off' to stop)\n",
                            t[1].c_str(), trace_lo>=0?(" in ["+std::to_string(trace_lo)+","+std::to_string(trace_hi)+"]").c_str():"", trace_cap); } }
            else fprintf(stderr,"  trace <file> [lo hi] | trace off\n"); }
        // §11 interrupt trace to file (non-stopping)
        else if (cmd=="itrace"){
            if (t.size()>=2 && t[1]=="off"){
                if(itrace_fp){ fclose(itrace_fp); fprintf(stderr,"  itrace off (%ld INT/NMI logged)\n",itrace_n); }
                else fprintf(stderr,"  itrace was not on\n");
                itrace_fp=nullptr; itrace_n=0; }
            else if (t.size()>=2){ if(itrace_fp) fclose(itrace_fp);
                itrace_fp=fopen(t[1].c_str(),"w"); itrace_n=0;
                if(!itrace_fp) fprintf(stderr,"  cannot open %s for writing\n",t[1].c_str());
                else fprintf(stderr,"  itrace → %s (each accepted INT/NMI: cycle, int@PC, ISR, SP)\n",t[1].c_str()); }
            else fprintf(stderr,"  itrace <file> | itrace off\n"); }
        // ══ MISC: relative-cycle marker ══
        else if (cmd=="mark"){ if(t.size()>1){ rel_arm_pc=(int)(uint16_t)parseNum(t[1]); fprintf(stderr,"  mark armed at PC=%04X\n",rel_arm_pc);}
            else { rel_origin=m.cpuCycles(); rel_armed=true; fprintf(stderr,"  mark: origin=%llu (now)\n",(unsigned long long)rel_origin);} }
        // ══ INSPECT: registers, backtrace, memory dump/poke, disasm, examine, source, set ══
        else if (cmd=="r"){ snap1=grab(m.cpuDebug()); printSnap(1);
            if(t.size()>1){ snap2=grab(m.zve2Debug()); printSnap(2); } showInsn("=>",m.cpuPC()); stateLine(); }
        // machine-readable registers (one JSON line) — for scripted/agent consumption
        else if (cmd=="rj"){ const Z80& z=m.cpuDebug();
            fprintf(stderr,"\n{\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\",\"bc\":\"0x%04X\","
                "\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"cyc\":%llu,\"rom\":%s,\"busrq\":%s,"
                "\"zve2\":\"%s\"}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,z.R, z.IFF1?"true":"false",
                (unsigned long long)m.cpuCycles(), m.isRomEnabled()?"true":"false",
                m.isBUSRQ()?"true":"false",
                m.isZVE2InReset()?"reset":(m.isZVE2Waiting()?"wait":"run")); }
        // ZVE2 registers as one JSON line (§0.3) — the DMA CPU's view for scripted analysis.
        else if (cmd=="rj2"){ const Z80& z=m.zve2Debug();
            fprintf(stderr,"\n{\"cpu\":\"zve2\",\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\","
                "\"bc\":\"0x%04X\",\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"af_\":\"0x%04X\",\"bc_\":\"0x%04X\",\"de_\":\"0x%04X\",\"hl_\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"active\":%s}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.AF_,z.BC_,z.DE_,z.HL_,z.I,z.R,
                z.IFF1?"true":"false", zve2Active()?"true":"false"); }
        // §4 dual-CPU status line: where do BOTH CPUs stand + the floppy? (`where --json` for agents)
        else if (cmd=="where"||cmd=="w"){ whereShow(t.size()>1 && t[1]=="--json"); }
        // §9 PC-hotspot profiler: hist <cycles> [lo hi]
        else if (cmd=="hist" && t.size()>1){
            uint64_t cyc=(uint64_t)parseNum(t[1]);
            int lo=-1,hi=-1; if(t.size()>3){ lo=(int)(uint16_t)parseNum(t[2]); hi=(int)(uint16_t)parseNum(t[3]); }
            pushHistory(); runHist(cyc,lo,hi); }
        else if (cmd=="bt"){
            if (t.size()>1 && t[1]=="scan"){ bool prev=bt_use_history; bt_use_history=false;
                backtrace(t.size()>2?(int)parseNum(t[2]):8); bt_use_history=prev; }
            else backtrace(t.size()>1?(int)parseNum(t[1]):8); }
        else if ((cmd=="d"||cmd=="dump") && t.size()>1){
            // §3-Gegenstück: `dump <adr> <len> <datei>` schreibt den Bereich als Binärdatei
            // heraus (für externen Disassembler/Diff); ohne Dateinamen bleibt es der Hexdump.
            if (t.size()>3){
                uint16_t a=(uint16_t)parseNum(t[1]); int n=(int)parseNum(t[2]);
                std::ofstream f(t[3],std::ios::binary);
                if(!f) fprintf(stderr,"  cannot write %s\n",t[3].c_str());
                else { for(int i=0;i<n;++i) f.put((char)m.memReadDebug((uint16_t)(a+i)));
                       fprintf(stderr,"  dumped %d byte(s) from %04X → %s\n",n,a,t[3].c_str()); } }
            else dump((uint16_t)parseNum(t[1]), t.size()>2?(int)parseNum(t[2]):64); }
        else if (cmd=="e" && t.size()>2){ uint16_t a=(uint16_t)parseNum(t[1]);
            // poke values default to HEX (matches d/u output); strtol base 16 also accepts 0x..
            for(size_t i=2;i<t.size();++i)
                m.memWriteDebug(a++,(uint8_t)strtol(t[i].c_str(),nullptr,16));
            fprintf(stderr,"  poked %zu byte(s)\n",t.size()-2); }
        else if (cmd=="u"){
            uint16_t a = t.size()>1? (uint16_t)parseNum(t[1]) : (last_u_set? last_u : m.cpuPC());
            int cnt = t.size()>2? (int)parseNum(t[2]) : 12;
            // §8: `u <adr> <hi>` (Endadresse) ist die naheliegende, aber falsche Lesart —
            // das zweite Argument ist die ANZAHL. Sieht es klar nach einer Endadresse aus
            // (> Startadresse, > 256), so behandeln und es sagen, statt 5000 Zeilen zu drucken.
            uint16_t u_end = 0; bool u_range=false;
            if (t.size()>2 && cnt > 256 && cnt > (int)a && cnt <= 0xFFFF){
                u_end=(uint16_t)cnt; u_range=true; cnt=0x7FFF;
                fprintf(stderr,"  (2. Argument als END-Adresse %04X gelesen; fuer eine Anzahl < 257 angeben)\n",u_end); }
            // läuft die Disassembly in unbeschriebenen Speicher (lauter 0xFF → "RST 38H"),
            // ist das keine Codeausgabe, sondern Rauschen. Nach 4 solchen Zeilen abbrechen
            // und sagen, ab wo — spart das Scrollen durch 40 sinnlose Zeilen.
            int ff_run = 0;
            for(int i=0;i<cnt;++i){ if(u_range && a>=u_end) break;
                char l[120]; int len=disasmAt(a,l,sizeof l);
                uint8_t op = m.memReadDebug(a);
                if (op==0xFF || op==0x00){
                    if (++ff_run > 4){
                        fprintf(stderr,"  … ab %04X unbeschriebener Speicher (%s) — Ausgabe abgebrochen\n",
                                (unsigned)(a - (uint16_t)(4*len)), op==0xFF? "0xFF":"0x00");
                        break; }
                } else ff_run = 0;
                std::string p=prnFor(a);
                fprintf(stderr,"  %s%s%s\n",l, p.empty()?"":"  ; ", p.c_str()); a=(uint16_t)(a+len); }
            last_u=a; last_u_set=true; }
        else if (cmd=="x" || cmd.rfind("x/",0)==0){
            std::string spec = (cmd.find('/')!=std::string::npos)? cmd.substr(cmd.find('/')+1) : "";
            bool have=t.size()>1; uint16_t a=0;
            // address may be a register / (rr) / [mem] / symbol / number (readOperand superset)
            if(have){ if(snap1.valid){ bool ok; a=(uint16_t)readOperand(snap1,t[1],ok); }
                      else a=(uint16_t)parseNum(t[1]); }
            // #4: `x ADDR N` (no /count/ spec) — take N (decimal) as the count, so a
            // plain multi-byte dump works without the /N syntax.
            if (spec.empty() && t.size()>2) spec = t[2];
            examine(spec, have, a); }
        else if (cmd=="list" || cmd=="l"){
            // list [A|symbol] [N]  — .prn source around A (default: continue, else PC)
            uint16_t a = t.size()>1? (uint16_t)parseNum(t[1])
                                   : (last_list_set? last_list : m.cpuPC());
            int n = t.size()>2? (int)parseNum(t[2]) : 10;
            listSrc(a,n); }
        else if (cmd=="set" && t.size()>=3){
            int cpu=1; size_t idx=1; if(t[1]=="2"){cpu=2; idx=2;}
            if (idx+1<t.size() && setReg(cpu,t[idx],parseNum(t[idx+1])))
                fprintf(stderr,"  ZVE%d %s := 0x%lX\n",cpu,t[idx].c_str(),parseNum(t[idx+1])&0xFFFF);
            else fprintf(stderr,"  ? bad register\n"); }
        else if (cmd=="disp"){ if(t.size()>1){
                std::string ex; for(size_t i=1;i<t.size();++i){ if(i>1)ex+=" "; ex+=t[i]; } // join → spaces in exprs ok
                displays.push_back(ex); fprintf(stderr,"  disp[%zu] = %s\n",displays.size()-1,ex.c_str()); }
            else { for(size_t i=0;i<displays.size();++i) fprintf(stderr,"  disp[%zu] %s\n",i,displays[i].c_str()); } }
        else if (cmd=="undisp" && t.size()>1){ size_t i=(size_t)parseNum(t[1]); if(i<displays.size()) displays.erase(displays.begin()+i); }
        // ══ WATCH: memory watchpoints (range + value-cond) and I/O-port watches ══
        else if ((cmd=="wp"||cmd=="wpr"||cmd=="wb") && t.size()>1){
            MemWatch w; parseRange(t[1], w.lo, w.hi);
            w.rd = (cmd=="wpr"); w.wr = (cmd!="wpr"); w.brk = (cmd=="wb");
            // optional value condition:  == N  |  != N  |  changed
            // N is a memory BYTE → parsed as HEX (consistent with d/u/e), 0x.. also ok.
            if (t.size()>=4 && t[2]=="=="){ w.cond=MemWatch::EQ; w.val=(uint8_t)strtol(t[3].c_str(),nullptr,16); }
            else if (t.size()>=4 && t[2]=="!="){ w.cond=MemWatch::NE; w.val=(uint8_t)strtol(t[3].c_str(),nullptr,16); }
            else if (t.size()>=3 && t[2]=="changed"){ w.cond=MemWatch::CHG;
                for (uint32_t a=w.lo;a<=w.hi;++a) w.last[(uint16_t)a]=m.memReadDebug((uint16_t)a); }
            const char* cs = w.cond==MemWatch::EQ?"==":w.cond==MemWatch::NE?"!=":w.cond==MemWatch::CHG?"changed":"";
            char cond[24]={0}; if(w.cond==MemWatch::EQ||w.cond==MemWatch::NE) snprintf(cond,sizeof cond," %s %02X",cs,w.val);
            else if(w.cond==MemWatch::CHG) snprintf(cond,sizeof cond," changed");
            mwatch.push_back(std::move(w));
            fprintf(stderr,"  [%zu] %s [%04X..%04X]%s\n", mwatch.size()-1,
                    cmd=="wp"?"watch-write":cmd=="wpr"?"watch-read":"break-write",
                    mwatch.back().lo,mwatch.back().hi,cond); }
        else if (cmd=="wd" && t.size()>1){
            if (t[1]=="all"){ mwatch.clear(); fprintf(stderr,"  all watchpoints cleared\n"); }
            else { uint16_t a=(uint16_t)parseNum(t[1]); size_t before=mwatch.size();
                mwatch.erase(std::remove_if(mwatch.begin(),mwatch.end(),
                    [&](const MemWatch& w){ return a>=w.lo && a<=w.hi; }), mwatch.end());
                fprintf(stderr,"  removed %zu watch(es) covering %04X\n",before-mwatch.size(),a); } }
        else if (cmd=="wl"){
            if(mwatch.empty()) fprintf(stderr,"  (no memory watchpoints)\n");
            for(size_t i=0;i<mwatch.size();++i){ const MemWatch& w=mwatch[i];
                const char* k = w.brk?"break":"print";
                const char* dir = (w.rd&&w.wr)?"rw":w.rd?"rd":"wr";
                char cond[24]={0}; if(w.cond==MemWatch::EQ) snprintf(cond,sizeof cond," == %02X",w.val);
                else if(w.cond==MemWatch::NE) snprintf(cond,sizeof cond," != %02X",w.val);
                else if(w.cond==MemWatch::CHG) snprintf(cond,sizeof cond," changed");
                fprintf(stderr,"  [%zu] %s-%s [%04X..%04X]%s  hits=%ld\n",i,k,dir,w.lo,w.hi,cond,w.hits); } }
        else if ((cmd=="iow"||cmd=="iob") && t.size()>1){ uint8_t p=(uint8_t)parseNum(t[1]);
            (cmd=="iow"?io_w:io_b).insert(p); fprintf(stderr,"  %s io (%02XH)\n",cmd=="iow"?"watch":"break",p); }
        else if (cmd=="iod" && t.size()>1){ uint8_t p=(uint8_t)parseNum(t[1]); io_w.erase(p); io_b.erase(p); }
        else if (cmd=="iol"){ fprintf(stderr,"  io-watch:"); for(auto p:io_w)fprintf(stderr," %02X",p);
            fprintf(stderr,"\n  io-break:"); for(auto p:io_b)fprintf(stderr," %02X",p); fprintf(stderr,"\n"); }
        // ══ SYMBOLS & LISTINGS: -s symbol tables and -l .prn listings ══
        else if (cmd=="sym"){
            if (t.size()>=4 && t[1]=="add"){ symAdd(t[2],(uint16_t)parseNum(t[3])); fprintf(stderr,"  sym %s=%04X\n",t[2].c_str(),(uint16_t)parseNum(t[3])); }
            else if (t.size()>=2 && t[1]=="list"){ for(auto&kv:sym_by_addr) fprintf(stderr,"  %04X %s\n",kv.first,kv.second.c_str()); }
            else if (t.size()>=2) loadSyms(t[1]);
            else fprintf(stderr,"  sym <file> | sym add <name> <addr> | sym list\n"); }
        else if (cmd=="lst"){
            if (t.size()>=2 && t[1]=="list"){
                fprintf(stderr,"  %zu listing line(s) loaded\n",prn.by_addr.size());
                for(auto&kv:prn.by_addr) fprintf(stderr,"  %04X  %s\n",kv.first,kv.second.c_str()); }
            else if (t.size()>=3) loadPrnSpec(t[1]+"@"+t[2]);   // lst <file.prn> <offset>
            else if (t.size()>=2) loadPrnSpec(t[1]);            // lst <file.prn>[@offset]
            else fprintf(stderr,"  lst <file.prn>[@offset] | lst <file.prn> <offset> | lst list\n"); }
        // ══ MEM: load/save raw binary to/from RAM ══
        else if (cmd=="load" && t.size()>2){ std::ifstream f(t[1],std::ios::binary);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); }
            else { uint16_t a=(uint16_t)parseNum(t[2]); int n=0; char b;
                while(f.get(b)){ m.memWriteDebug(a++,(uint8_t)b); ++n; } fprintf(stderr,"  loaded %d byte(s) @%04lX\n",n,parseNum(t[2])&0xFFFF); } }
        else if (cmd=="save" && t.size()>3){ std::ofstream f(t[1],std::ios::binary);
            uint16_t a=(uint16_t)parseNum(t[2]); int n=(int)parseNum(t[3]);
            for(int i=0;i<n;++i) f.put((char)m.memReadDebug(a+i)); fprintf(stderr,"  saved %d byte(s) from %04X to %s\n",n,a,t[1].c_str()); }
        // §3: Binärabgleich Datei ↔ RAM — „ist das die richtige Datei, derselbe Build,
        // und wo genau nicht?" in einem Kommando (statt xxd-Ausgabe per Auge).
        else if (cmd=="verify"){
            if (t.size()<3){ fprintf(stderr,"  verify <datei> @<adr> [laenge]   Datei mit dem RAM vergleichen\n"); }
            else {
                std::string as=t[2]; if(!as.empty()&&as[0]=='@') as=as.substr(1);
                uint16_t base=(uint16_t)resolveAddr(as);
                std::ifstream f(t[1],std::ios::binary);
                if(!f) fprintf(stderr,"  cannot open %s\n",t[1].c_str());
                else {
                    std::vector<uint8_t> fb((std::istreambuf_iterator<char>(f)),
                                             std::istreambuf_iterator<char>());
                    size_t n=fb.size();
                    if (t.size()>3){ size_t lim=(size_t)parseNum(t[3]); if(lim<n) n=lim; }
                    if (base+n > 0x10000) n = 0x10000 - base;
                    size_t same=0, shown=0;
                    std::vector<size_t> diffs;
                    for(size_t i=0;i<n;++i){
                        uint8_t r=m.memReadDebug((uint16_t)(base+i));
                        if (r==fb[i]) ++same; else diffs.push_back(i);
                    }
                    fprintf(stderr,"  %zu Bytes @%04X, %zu identisch (%.1f %%), %zu Abweichung(en)%s\n",
                            n,base,same, n? 100.0*(double)same/(double)n : 0.0, diffs.size(),
                            diffs.empty()?"":":");
                    for(size_t d : diffs){
                        if (++shown>32){ fprintf(stderr,"    … (%zu weitere)\n",diffs.size()-32); break; }
                        fprintf(stderr,"    %04X  Datei %02X   RAM %02X\n",
                                (unsigned)(base+d), fb[d], m.memReadDebug((uint16_t)(base+d)));
                    }
                } } }
        // full machine state (RAM+CPU) to/from a file — boot once, then resume cheaply
        else if (cmd=="savestate" && t.size()>1){
            if(m.saveState(t[1])) fprintf(stderr,"  state saved → %s (PC=%04X)\n",t[1].c_str(),m.cpuPC());
            else fprintf(stderr,"  cannot write state %s\n",t[1].c_str()); }
        else if (cmd=="loadstate" && t.size()>1){
            if(m.loadState(t[1])){ snap1=grab(m.cpuDebug()); callstack.clear(); rev_ring.clear();
                fprintf(stderr,"  state loaded ← %s\n",t[1].c_str()); showInsn("=>",m.cpuPC()); stateLine(); }
            else fprintf(stderr,"  cannot load state %s (missing/invalid)\n",t[1].c_str()); }
        // ══ MISC: machine I/O — keystrokes, screen, named RAM vars, chip state, reset ══
        else if (cmd=="keys" && t.size()>1){ pushHistory(); std::string s=line.substr(line.find("keys")+5); keys(s); }
        else if (cmd=="screen"){
            if (t.size()>=3 && t[1]=="find"){   // #5 screen find "<text>"|/regex/
                std::string pat; extractArg(line, line.find("find")+4, pat);
                int r=-1,c=-1;
                if(screenFind(pat,&r,&c)) fprintf(stderr,"  found at row %d col %d\n",r,c);
                else fprintf(stderr,"  not found\n"); }
            else screen(); }
        // #1 screen-conditioned run / breakpoint (deterministic menu navigation)
        else if (cmd=="gscreen"){
            std::string pat; size_t p=extractArg(line, line.find("gscreen")+7, pat);
            if(pat.empty()){ fprintf(stderr,"  gscreen \"<text>\"|/regex/ [maxcyc]\n"); }
            else { uint64_t cap=0; { std::istringstream r(line.substr(p)); std::string cw; if(r>>cw) cap=(uint64_t)parseNum(cw); }
                pushHistory(); bool matched=false; uint64_t ran=goUntilScreen(pat,cap,matched);
                if(hit){ fprintf(stderr,"   (ran %llu cyc — breakpoint before screen matched)\n",(unsigned long long)ran); onStop(); }
                else if(matched){ fprintf(stderr,"   screen matched after %llu cyc\n",(unsigned long long)ran); screen(); }
                else { fprintf(stderr,"   ran %llu cyc, screen NOT matched (cap)\n",(unsigned long long)ran); screen(); } } }
        else if (cmd=="bscreen"){
            if (t.size()>=2 && t[1]=="off"){ screen_bp.clear(); fprintf(stderr,"  screen breakpoint cleared\n"); }
            else { std::string pat; extractArg(line, line.find("bscreen")+7, pat);
                if(pat.empty()) fprintf(stderr,"  bscreen \"<text>\"|/regex/ | bscreen off   (any g/gu/n then stops on match)\n");
                else { screen_bp=pat; fprintf(stderr,"  screen bp armed: %s\n",pat.c_str()); } } }
        // #3 keyuntil: press a key repeatedly until the screen shows <text> (robust
        // against direct-poll key loss)
        else if (cmd=="keyuntil"){
            std::string keyspec,pat; size_t p=extractArg(line, line.find("keyuntil")+8, keyspec);
            p=extractArg(line,p,pat);
            if(keyspec.empty()||pat.empty()){ fprintf(stderr,"  keyuntil \"<key>\" \"<screen-text>\" [maxcyc]\n"); }
            else { uint64_t cap=0; { std::istringstream r(line.substr(p)); std::string cw; if(r>>cw) cap=(uint64_t)parseNum(cw); }
                pushHistory(); bool ok=keyUntil(keyspec,pat,cap);
                if(hit){ onStop(); }
                else { fprintf(stderr,"   key '%s' → screen %s\n",keyspec.c_str(),ok?"matched":"NOT matched (cap)"); screen(); } } }
        // dialog <file>: drive a whole menu/wizard non-interactively. Each line is
        //   "<screen-pattern>" "<keys>" [maxcyc]
        // → wait until the text VRAM shows <screen-pattern> (empty = don't wait),
        // then inject <keys> (same escapes as `keys`). Exactly the "wait for the
        // menu, then answer" loop that INIT.COM / HARDY navigation needs.
        else if (cmd=="dialog" && t.size()>1){
            std::ifstream f(t[1]);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); }
            else { std::string l; int step=0; pushHistory();
                while(std::getline(f,l)){
                    size_t i=0; while(i<l.size()&&isspace((unsigned char)l[i]))++i;
                    if(i>=l.size()||l[i]=='#') continue;         // blank / comment
                    std::string pat,ks; size_t p=extractArg(l,i,pat); p=extractArg(l,p,ks);
                    uint64_t cap=0; { std::istringstream r(l.substr(p)); std::string cw; if(r>>cw) cap=(uint64_t)parseNum(cw); }
                    if(!pat.empty()){ bool matched=false; goUntilScreen(pat,cap,matched);
                        if(hit){ onStop(); break; }
                        if(!matched){ fprintf(stderr,"  dialog step %d: screen '%s' NOT reached (cap) — abort\n",step,pat.c_str()); break; } }
                    fprintf(stderr,"  dialog step %d: '%s' ✓%s%s\n",step,pat.c_str(),
                            ks.empty()?"":" → keys ",ks.c_str());
                    if(!ks.empty()){ keys(ks); if(hit){ onStop(); break; } }
                    ++step;
                }
                fprintf(stderr,"  dialog: %d step(s) done\n",step); screen(); } }
        else if (cmd=="vars"){ auto wd=[&](uint16_t a){return (uint16_t)(m.memReadDebug(a)|(m.memReadDebug(a+1)<<8));};
            if (t.size()>=3 && (t[1]=="-f"||t[1]=="load")){   // §16 vars -f <file>: name addr [w]
                std::ifstream f(t[2]);
                if(!f){ fprintf(stderr,"  cannot open %s\n",t[2].c_str()); }
                else { std::string l; int n=0;
                    while(std::getline(f,l)){ std::istringstream is(l); std::string nm,ad,wf;
                        if(!(is>>nm)||nm[0]=='#') continue; if(!(is>>ad)) continue;
                        bool word=(bool)(is>>wf) && (wf=="w"||wf=="W");
                        var_watch.emplace_back(nm,(uint16_t)resolveAddr(ad),word); ++n; }
                    fprintf(stderr,"  loaded %d var(s) from %s\n",n,t[2].c_str()); } }
            else if (t.size()>=4 && t[1]=="add"){
                bool word = t.size()>=5 && (t[4]=="w"||t[4]=="W");
                uint16_t a=(uint16_t)resolveAddr(t[3]); var_watch.emplace_back(t[2],a,word);
                fprintf(stderr,"  var %s = [%04X]%s\n",t[2].c_str(),a,word?" (word)":""); }
            else if (t.size()>=2 && t[1]=="clear"){ var_watch.clear(); fprintf(stderr,"  vars cleared\n"); }
            else if (!var_watch.empty()){
                for(auto& v: var_watch){ uint16_t a=std::get<1>(v);
                    if(std::get<2>(v)) fprintf(stderr,"  %-14s [%04X] = %04X\n",std::get<0>(v).c_str(),a,wd(a));
                    else               fprintf(stderr,"  %-14s [%04X] = %02X\n",std::get<0>(v).c_str(),a,m.memReadDebug(a)); } }
            else fprintf(stderr,"  [03F8]done=%02X  DPB: [D1B2]=%04X [D1B4]=%04X [D1B8]=%04X [D1BE]=%04X [D1CD]=%04X\n"
                                "  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n",
                    m.memReadDebug(0x03F8),wd(0xD1B2),wd(0xD1B4),wd(0xD1B8),wd(0xD1BE),wd(0xD1CD)); }
        else if (cmd=="dev"){
            std::string w = t.size()>1? t[1] : "k5122";
            auto Y=[&](bool b){ return b?"1":"0"; };
            if (w=="ctc"){ auto c=m.ctcState();
                fprintf(stderr,"  CTC (K2526)  vecBase=%02X  IEI=%s IEO=%s\n",c.vecBase,Y(c.iei),Y(c.ieo));
                for(int i=0;i<4;++i){ auto& ch=c.ch[i];
                    fprintf(stderr,"    ch%d ctl=%02X TC=%02X cnt=%-3d run=%s  INT(en=%s pend=%s ius=%s iei=%s)\n",
                        i,ch.control,ch.timeConst,ch.counter,Y(ch.running),Y(ch.intEn),Y(ch.intPending),Y(ch.ius),Y(ch.iei)); } }
            else if (w=="pio"){
                // §4: alle drei PIOs erreichbar — die K5122-PIOs (Steuer/Daten) waren
                // bisher nur per C++-Instrumentierung sichtbar, obwohl genau dort der
                // Interrupt-Zustand steht, der einen Fremd-OS-Sturm erklärt.
                std::string which = t.size()>2? t[2] : "all";
                auto showPio=[&](const char* name, const Z80PIO::DebugState& p){
                    fprintf(stderr,"  %s  IEI=%s IEO=%s\n",name,Y(p.iei),Y(p.ieo));
                    for(int i=0;i<2;++i){ auto& pt=p.port[i];
                        fprintf(stderr,"    %c mode=%u out=%02X in=%02X dir=%02X vec=%02X  INT(en=%s pend=%s ius=%s iei=%s)\n",
                            i?'B':'A',pt.mode,pt.out,pt.in,pt.dir,pt.vector,Y(pt.ie),Y(pt.pending),Y(pt.ius),Y(pt.iei)); } };
                bool all = (which=="all");
                bool any = false;
                if (all || which=="k5122ctrl" || which=="ctrl"){
                    showPio("K5122 ctrl-PIO (Ports 10-13)", m.k5122CtrlPioState()); any=true; }
                if (all || which=="k5122data" || which=="data"){
                    showPio("K5122 data-PIO (Ports 14-17)", m.k5122DataPioState()); any=true; }
                if (all || which=="bs"){
                    showPio("BS-PIO (K2526, Ports 08-0B)", m.bsPioState()); any=true; }
                if (!any) fprintf(stderr,"  dev pio [all|bs|k5122ctrl|k5122data]\n"); }
            else if (w=="sio" || w=="sio2"){
                auto s = (w=="sio2")? m.dfueSioState() : m.kbdSioState();
                fprintf(stderr,"  SIO %s (K8025 %s)  IEI=%s IEO=%s\n",
                    w=="sio2"?"DFUE":"kbd/prn", w=="sio2"?"A33":"A32", Y(s.iei),Y(s.ieo));
                for(int i=0;i<2;++i){ auto& ch=s.ch[i];
                    fprintf(stderr,"    %c rr0=%02X rr1=%02X wr1=%02X vec=%02X  irq(rx=%s tx=%s ext=%s) ius=%s iei=%s  rxQ=%zu txBusy=%s\n",
                        i?'B':'A',ch.rr0,ch.rr1,ch.wr1,ch.wr2,Y(ch.irqRx),Y(ch.irqTx),Y(ch.irqExt),Y(ch.ius),Y(ch.iei),ch.rxQueued,Y(ch.txBusy)); } }
            else { auto k=m.k5122State();
                fprintf(stderr,"  K5122: D%d %s  cyl=%u head=%u  %s%s  headPos=%zu/%zu secSize=%u  /BUSRQ-pend=%s\n",
                        k.drive, k.mounted?"mounted":"EMPTY", k.cylinder, k.head,
                        k.transferring?"READING":"idle", k.writeMode?"+WRITE":"",
                        k.headPos, k.trackLen, k.sectorSize, k.busrq?"yes":"no");
                fprintf(stderr,"  (dev ctc | dev pio [all|bs|k5122ctrl|k5122data] | dev sio | dev sio2)\n"); } }
        else if (cmd=="ivt"){    // §6: IM-2-Vektortabelle auf einen Blick
            // Für jede Interruptquelle der Daisy-Chain: programmierter Vektor →
            // Tabellenadresse (I<<8 | vec&0xFE) → dort eingetragene ISR-Adresse.
            // Ein Gerät mit IE=1, dessen Eintrag ins Leere zeigt, ist der klassische
            // Fremd-OS-Fehler (Interruptsturm / Sprung nach 0xFFFF).
            bool useZ2 = t.size()>1 && (t[1]=="2"||t[1]=="zve2");
            const Z80& z = useZ2? m.zve2Debug() : m.cpuDebug();
            fprintf(stderr,"  %s: I=%02X  IM %u  IFF1=%d\n",
                    useZ2?"ZVE2":"ZVE1", z.I, z.IM, (int)z.IFF1);
            if (z.IM != 2)
                fprintf(stderr,"  (Hinweis: IM != 2 — die Tabelle wird gerade nicht benutzt)\n");
            fprintf(stderr,"  Vektor Tabelle Eintrag Geraet                     Status\n");
            auto entryAt=[&](uint8_t vec)->uint16_t{
                uint16_t tb=(uint16_t)((z.I<<8)|(vec&0xFE));
                return (uint16_t)(m.memReadDebug(tb) | (m.memReadDebug((uint16_t)(tb+1))<<8)); };
            int warned=0;
            for (auto& s : m.interruptSources()){
                // Nicht programmierte/uninteressante Quellen ausblenden, außer sie sind scharf.
                bool interesting = s.ie || s.pending || s.ius;
                if (!interesting && !(t.size()>1 && (t[1]=="all"||t.back()=="all"))) continue;
                uint16_t tb=(uint16_t)((z.I<<8)|(s.vector&0xFE));
                uint16_t ent=entryAt(s.vector);
                const char* st = "ok";
                if (ent==0xFFFF || ent==0x0000){ st = s.ie? "ZEIGT INS LEERE  <-- IE=1!" : "zeigt ins Leere"; if(s.ie) ++warned; }
                else if (!s.ie) st = "(IE=0)";
                fprintf(stderr,"  %s0x%02X  0x%04X  %04X    %-26s %s%s%s\n",
                        s.exact?" ":"~", s.vector, tb, ent, s.device.c_str(), st,
                        s.pending?"  pend":"", s.ius?"  ius":"");
            }
            // Fallback-Zeile: der Bus liefert 0xFF, wenn KEIN Gerät antwortet.
            {   uint16_t tb=(uint16_t)((z.I<<8)|0xFE);
                uint16_t ent=entryAt(0xFF);
                fprintf(stderr,"   0xFF  0x%04X  %04X    %-26s %s\n",tb,ent,
                        "(Fallback: kein Geraet)", (ent==0xFFFF||ent==0x0000)?"zeigt ins Leere":"ok"); }
            auto& ia = m.lastIntAck();
            if (ia.count) fprintf(stderr,"  letzte Quittung: %s Vektor=%02X (%llu gesamt)\n",
                    ia.spurious? "SPURIOUS (kein Geraet)" : (ia.device?ia.device:"?"),
                    ia.vector, (unsigned long long)ia.count);
            if (warned) fprintf(stderr,"  ==> %d scharfe Quelle(n) ohne gueltigen Tabelleneintrag\n",warned);
            fprintf(stderr,"  (ivt all = auch gesperrte Quellen; ivt 2 = I-Register der ZVE2; ~ = SIO-Basisvektor)\n"); }
        else if (cmd=="disk"){   // §13 disk verify [B]: Sektor-/CRC-Health aller Spuren
            if (t.size()>=2 && t[1]=="verify"){
                bool wantB = t.size()>=3 && (t[2]=="B"||t[2]=="b"||t[2]=="1");
                diskVerify(wantB?diskB:disk, wantB?"B:":"A:");
            } else fprintf(stderr,"  disk verify [B]   Sektor-/CRC-Health aller Spuren des Originals\n"); }
        else if (cmd=="clock"){   // §7: Uhrenwahl für Lauf-Budgets (g/gu/gscreen/hist)
            if (t.size()>1){
                if (t[1]=="zve1") clock_machine=false;
                else if (t[1]=="machine"||t[1]=="maschine") clock_machine=true;
                else { fprintf(stderr,"  clock [zve1|machine]\n"); } }
            fprintf(stderr,"  Lauf-Uhr = %s   ZVE1=%llu  Maschine=%llu\n",
                    clock_machine?"Maschine (beide CPUs)":"ZVE1",
                    (unsigned long long)m.cpuCycles(),(unsigned long long)m.machineCycles()); }
        else if (cmd=="reset"){ m.reset(); fprintf(stderr,"  reset\n"); }
        // ── MISC: command aliases + sourcing a script mid-session ──
        else if (cmd=="alias"){
            if (t.size()>=3){ std::string ex; for(size_t i=2;i<t.size();++i){ if(i>2)ex+=" "; ex+=t[i]; }
                aliases[t[1]]=ex; fprintf(stderr,"  alias %s = %s\n",t[1].c_str(),ex.c_str()); }
            else if (aliases.empty()) fprintf(stderr,"  (no aliases)\n");
            else for(auto&kv:aliases) fprintf(stderr,"  alias %s = %s\n",kv.first.c_str(),kv.second.c_str()); }
        else if (cmd=="unalias" && t.size()>1){ aliases.erase(t[1]); }
        else if (cmd=="source" && t.size()>1){   // queue a script file's lines to run next
            std::ifstream f(t[1]);
            if(!f) fprintf(stderr,"  cannot open %s\n",t[1].c_str());
            else { std::vector<std::string> ls; std::string l; while(std::getline(f,l)) ls.push_back(l);
                pending.insert(pending.begin(), ls.begin(), ls.end());
                fprintf(stderr,"  sourced %zu line(s) from %s\n",ls.size(),t[1].c_str()); } }
        else fprintf(stderr,"  ? unknown command '%s' (try help)\n",cmd.c_str());
    }
    if (trace_fp){ fclose(trace_fp); fprintf(stderr,"trace closed (%ld line(s))\n",trace_lines); }
    if (itrace_fp){ fclose(itrace_fp); fprintf(stderr,"itrace closed (%ld INT/NMI)\n",itrace_n); }
    for (auto& t : cow_temps){ std::error_code ec; std::filesystem::remove(t,ec); }   // drop COW temps
    return mount_failed ? 1 : 0;   // non-zero exit if a requested disk failed to mount
}
