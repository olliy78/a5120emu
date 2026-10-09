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
 *     dump/poke/load/save, keystroke injection and a screen view,
 *   - A5120.16 (`--em em256`): the U8001 of the expansion module as a THIRD CPU
 *     context (`cpu u8000`) with its own disassembler/assembler, breakpoints, steps,
 *     Segmentweiche-aware memory commands, `dev em`, `fcw`/`psa` and event breaks
 *     on mode change / VI / INT-16 (`bmode`, `bvi`, `bint16`).
 *
 * Commands come from stdin (interactive or piped) and/or a -x script file.
 * Type `help` for the command list.  See tools/k1520dbg.md for the full manual.
 *
 * `--machine k8915` fährt statt des A5120 einen K8915 (eine CPU, A8H-Speicherbild,
 * K5122 im /WAIT-Betrieb) — tools/dbg_machine.h, `help k8915`, §8a AP-E4d.
 *
 * `--raf raf128|raf512|raf2m` steckt eine RAM-Floppy (alle Maschinen); Befehl `raf`.
 * `--wdc 4.2|4.0.05|3.4.05` und `--hd <abbild>` (nur `--machine p8000-16`, P13d): WDC an der
 * 16-Bit-PIO2 bzw. Winchester an Laufwerk 0 (Kopie wie die Disketten, `--rw` = Original);
 * Befehle `wdc [r|u|d|log]`, `cpu wdc` (nur Ansicht).
 * `--ptape` steckt die Lochstreifen-Karte K6022 (SIF1000, E0H–E7H; alle Maschinen).
 * `--terminal`: Originalterminal P8000 Typ 2 (P8T 5.0 + K7673.09) als Einheit, Kontext `cpu z8`
 * auf dessen Z8, Zusatzkommandos term/crt/host/key/taste/lauf (tools/dbg_p8000_terminal.h, P20a).
 * `--z8 <abzug>[@org]` [--z8-fassung ub8840|ub8820|z8681]: Z8-Prüfstand ohne Maschine
 * (`cpu z8`-Kontext aus tools/dbg_z8.h; AP P19c) — z. B. Terminal-/Tastaturfirmware des P8000.
 *
 * @license MIT
 */
#include "core/machines/a5120/a5120.h"
#include "tools/dbg_machine.h"   // A5120 ODER K8915 hinter demselben `m` (§8a AP-E4d)
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
#include "tools/dbg_u8000.h"      // U8001-Kontext: Adressen, FCW, Aufrufstapel
#include "tools/dbg_p8000.h"      // P8000: ADP, DMA, Kopplung, MMU (AP P12)
#include "tools/dbg_z8.h"         // Z8/UB8840 (`--z8`, `cpu z8`, AP P19c)
#include "tools/dbg_p8000_terminal.h"   // Originalterminal (`--terminal`, AP P20a)
#include "tools/em_trace.h"       // EM-Ereignisse als Text (emlog, boot_trace --em)
#include "tools/z8000/z8k_disasm.h"
#include "tools/z8000/z8k_asm.h"
#ifdef HAVE_ISOCLINE
#include <isocline.h>          // third_party/isocline (MIT) — Zeileneditor, s. dortige README
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
#include "tools/term_console.h"   // Konsolenmodus: Rohmodus, Tasten, Bildschirm
#include <thread>

using k1520::logging::Logger;
using k1520::logging::Level;

// ─── Register snapshot captured at the start of an instruction ────────────────
// Lightweight copy of the Z80 register file taken at a stop (or by `r`). Used so
// printing/condition-evaluation works against a frozen view and to show shadow
// registers. `valid` distinguishes "never captured yet". (Distinct from the
// machine-wide A5120Machine::MachineSnapshot used by snap/restore/reverse-step.)
struct Snap {
    uint16_t PC=0,SP=0,AF=0,BC=0,DE=0,HL=0,IX=0,IY=0,AF_=0,BC_=0,DE_=0,HL_=0;
    uint8_t  I=0,R=0,IM=0; bool IFF1=false; uint64_t cyc=0; bool halted=false; bool valid=false;
};
static inline Snap grab(const Z80& z){
    Snap s; s.PC=z.PC; s.SP=z.SP; s.AF=z.AF; s.BC=z.BC; s.DE=z.DE; s.HL=z.HL;
    s.IX=z.IX; s.IY=z.IY; s.AF_=z.AF_; s.BC_=z.BC_; s.DE_=z.DE_; s.HL_=z.HL_;
    s.I=z.I; s.R=z.R; s.IM=z.IM; s.IFF1=z.IFF1; s.cyc=z.cycles; s.halted=z.halted; s.valid=true; return s;
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

#ifdef HAVE_ISOCLINE
// ─── Tab-Vervollständigung: das ERSTE Wort gegen die Kommandonamen ────────────
// Kommandoliste und Präfix-Matcher liegen in tools/dbg_commands.h, damit sie ohne
// Maschine unit-testbar sind (tests/debugtools/test_dbg_commands.cpp).
//
// Nur das erste Wort wird vervollständigt: ab dem zweiten sind die Argumente
// Adressen, Ausdrücke und Dateinamen — dort wäre eine Kommandoliste falsch, und
// eine Dateinamensvervollständigung hülfe nur bei einem Bruchteil der Kommandos.
// Erkennungsmerkmal ist, dass vor dem Präfix nichts als Leerraum steht.
// Sammelt die Treffer. Läuft INNERHALB von ic_complete_word() (s.u.), bekommt also
// nur das aktuelle Wort und darf es unmittelbar als Ersatz anbieten.
static void dbgCmdMatches(ic_completion_env_t* cenv, const char* wort){
    for (const auto& m : dbgcmd::match(wort ? wort : ""))
        if (!ic_add_completion(cenv, m.c_str())) return;   // false = genug gesammelt
}

// `prefix` ist die Eingabe BIS ZUM CURSOR.
//
// Zwei Feinheiten, beide am lebenden Terminal gefunden:
//  * **ic_complete_word() ist Pflicht.** ic_add_completion() allein FÜGT EIN, statt
//    zu ersetzen — aus „whe"+TAB wurde „whewhere". Erst ic_complete_word() rechnet
//    aus, welcher Teil zu ersetzen ist, und richtet die Vorschläge daran aus.
//  * **Nur das erste Wort.** Ab dem zweiten sind die Argumente Adressen, Ausdrücke
//    und Dateinamen; eine Kommandoliste wäre dort schlicht falsch. Erkennbar daran,
//    dass im Präfix noch kein Leerraum steht.
static void dbgCompleter(ic_completion_env_t* cenv, const char* prefix){
    if (prefix == nullptr || strpbrk(prefix, " \t") != nullptr) return;
    ic_complete_word(cenv, prefix, dbgCmdMatches, nullptr);
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
    // Disketten je Laufwerk: [0]=A: (erstes Argument ohne Schalter), [1..3]=B:/C:/D:
    // ueber -b/-c/-d.  Die Hardware kann vier (K5122::drives_[4]); bis 2026-08-19 bot
    // die Kommandozeile nur zwei an, was Kombi-Disketten (Fremdsystem auf C:) nur
    // ueber den Umweg eines Skripts erreichbar machte.
    const char* disks[4] = { nullptr, nullptr, nullptr, nullptr };
    const char*& disk  = disks[0];
    const char* script = nullptr;
    std::vector<std::string> symfiles;     // -s: symbol tables (repeatable)
    std::vector<std::string> prnfiles;     // -l: MACRO-80 .prn listings (repeatable)
    // Disk-mount mode (§6): COW is the DEFAULT — the disk is copied to a temp file and
    // that copy is mounted read/write, so a committed fixture can never be corrupted
    // (no more manual `mktemp; cp DISK $T; … $T; rm $T` ritual). `--rw` mounts the
    // original writable (writes persist); `--read-only`/`--ro` mounts write-protected.
    enum { MOUNT_COW=0, MOUNT_RW=1, MOUNT_RO=2 } mount_mode = MOUNT_COW;
    bool start_console = false;   // --console: sofort in den Konsolenmodus (§9)
    dbgm::Art art = dbgm::Art::A5120;   // --machine a5120|k8915|k8915-g2|prg710|prg710-1|pc1715|pc1715w|p8000|p8000-16 (Vorgabe a5120)
    bool skip_selftest = false;   // --skip-selftest: K8915 ohne ROM-Selbsttest (wie ein Warmstart)
    const char* em_opt = nullptr; // --em none|em064|em256: A5120.16 mit Erweiterungsmodul
    const char* raf_opt = nullptr; // --raf none|raf128|raf512|raf2m: RAM-Floppy auf 88H/89H
    const char* wdc_opt = nullptr; // --wdc 4.2|4.0.05|3.4.05: WDC an der 16-Bit-PIO2 (P8000, P13d)
    const char* konsole_opt = nullptr; // --konsole kern|original: Terminal an tty1 (P8000, P20c)
    const char* hd_opt  = nullptr; // --hd <abbild>: Winchester an WDC-Laufwerk 0 (setzt --wdc 4.2)
    std::string ptape_in, ptape_out;   // --ptape-in/-out: Band einlegen / Stanzer an Datei binden
    bool ptape_opt = false;        // --ptape: Lochstreifen-Karte K6022 auf E0H–E7H (Entwurf 23)
    const char* z8_bild = nullptr; // --z8 <abzug>[@org]: Z8-Prüfstand (P19c), ohne Maschine
    std::string z8_fassung;        // --z8-fassung ub8840|ub8820|z8681
    bool terminal_hw = false;      // --terminal: Originalterminal P8000 Typ 2 (P20a)
    for (int i=1;i<argc;++i){
        if (!strcmp(argv[i],"--machine") && i+1<argc){
            if (!dbgm::parseMachine(argv[++i], art)){
                fprintf(stderr,"unbekannte Maschine '%s' (a5120 | k8915 | k8915-g2 | prg710 | prg710-1 | pc1715 | pc1715w | p8000 | p8000-16)\n",argv[i]); return 2; } }
        else if (!strcmp(argv[i],"--skip-selftest")) skip_selftest=true;
        else if (!strcmp(argv[i],"-x") && i+1<argc) script=argv[++i];
        else if (!strcmp(argv[i],"-s") && i+1<argc) symfiles.push_back(argv[++i]);
        else if (!strcmp(argv[i],"-l") && i+1<argc) prnfiles.push_back(argv[++i]);
        else if (!strcmp(argv[i],"-b") && i+1<argc) disks[1]=argv[++i];
        else if (!strcmp(argv[i],"-c") && i+1<argc) disks[2]=argv[++i];
        else if (!strcmp(argv[i],"-d") && i+1<argc) disks[3]=argv[++i];
        else if (!strcmp(argv[i],"--console")) start_console=true;
        else if (!strcmp(argv[i],"--em") && i+1<argc) em_opt=argv[++i];
        else if (!strcmp(argv[i],"--raf") && i+1<argc) raf_opt=argv[++i];
        else if (!strcmp(argv[i],"--wdc") && i+1<argc) wdc_opt=argv[++i];
        else if (!strcmp(argv[i],"--konsole") && i+1<argc) konsole_opt=argv[++i];
        else if (!strcmp(argv[i],"--hd") && i+1<argc) hd_opt=argv[++i];
        else if (!strcmp(argv[i],"--ptape")) ptape_opt=true;
        else if (!strcmp(argv[i],"--z8") && i+1<argc) z8_bild=argv[++i];
        else if (!strcmp(argv[i],"--z8-fassung") && i+1<argc) z8_fassung=argv[++i];
        else if (!strcmp(argv[i],"--terminal")) terminal_hw=true;
        else if (!strcmp(argv[i],"--ptape-in") && i+1<argc) { ptape_opt=true; ptape_in=argv[++i]; }
        else if (!strcmp(argv[i],"--ptape-out") && i+1<argc) { ptape_opt=true; ptape_out=argv[++i]; }
        else if (!strcmp(argv[i],"--rw")) mount_mode=MOUNT_RW;
        else if (!strcmp(argv[i],"--cow")) mount_mode=MOUNT_COW;
        else if (!strcmp(argv[i],"--read-only")||!strcmp(argv[i],"--ro")) mount_mode=MOUNT_RO;
        else disk=argv[i];
    }
    // Z8-Prüfstand (P19c): eigener Kontext ohne Maschine (tools/dbg_z8.h, `help` dort).
    if (z8_bild) return dbgz8::pruefstand(z8_bild, z8_fassung, script);
    // Originalterminal (P20a): Einheit ohne Maschine, Kontext `cpu z8` auf dem Terminal-Z8.
    if (terminal_hw) return dbgp8kt::pruefstand(script);
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

    A5120Machine::Config mcfg;
    if (em_opt){
        if (art!=dbgm::Art::A5120){ fprintf(stderr,"--em gibt es nur am A5120 (A5120.16)\n"); return 2; }
        const std::string e = em_opt;
        if      (e=="em256") mcfg.em = A5120Machine::Config::Em::em256;
        else if (e=="em064") mcfg.em = A5120Machine::Config::Em::em064;
        else if (e!="none"){ fprintf(stderr,"--em: unbekanntes Modul '%s' (none|em064|em256)\n",em_opt); return 2; }
    }
    // P8000-WDC (P13d): --wdc/--hd nur mit p8000-16.  Die Platte wird wie die Disketten im
    // Vorgabemodus als Kopie gemountet (COW: Temp-Datei, am Ende gelöscht), mit --rw direkt.
    P8000Machine::Config p8cfg;
    if (konsole_opt){
        if (art!=dbgm::Art::P8000 && art!=dbgm::Art::P8000_16){ fprintf(stderr,"--konsole gibt es nur am P8000\n"); return 2; }
        const std::string k = konsole_opt;
        if (k=="original") p8cfg.terminal = P8000Machine::Config::TerminalArt::Original;
        else if (k!="kern"){ fprintf(stderr,"--konsole: '%s' unbekannt (kern|original)\n",konsole_opt); return 2; }
    }
    static std::string hd_kopie;
    struct HdKopieWeg { ~HdKopieWeg(){ if(!hd_kopie.empty()) std::remove(hd_kopie.c_str()); } };
    static HdKopieWeg hd_kopie_weg;
    if (wdc_opt || hd_opt){
        if (art!=dbgm::Art::P8000_16){ fprintf(stderr,"--wdc/--hd gibt es nur am P8000 mit 16-Bit-Karte (--machine p8000-16)\n"); return 2; }
        const std::string w = wdc_opt? wdc_opt : "4.2";
        using W = P8000Machine::Config::Wdc;
        if      (w=="4.2")    p8cfg.wdc = W::V4_2;
        else if (w=="4.0.05") p8cfg.wdc = W::V4_0_05;
        else if (w=="3.4.05") p8cfg.wdc = W::V3_4_05;
        else if (w!="aus"){ fprintf(stderr,"--wdc: unbekannte Firmware '%s' (4.2|4.0.05|3.4.05|aus)\n",wdc_opt); return 2; }
        if (hd_opt){
            if (p8cfg.wdc==W::Aus){ fprintf(stderr,"--hd braucht einen WDC\n"); return 2; }
            std::string pfad = hd_opt;
            if (mount_mode!=MOUNT_RW){
                hd_kopie = (std::filesystem::temp_directory_path() / ("k1520dbg_hd_" + std::to_string(k1520::os::processId()) + ".img")).string();
                std::error_code ec;
                std::filesystem::copy_file(pfad, hd_kopie, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec){ fprintf(stderr,"--hd: Kopie von %s scheitert: %s\n",hd_opt,ec.message().c_str()); hd_kopie.clear(); return 2; }
                pfad = hd_kopie;
                fprintf(stderr,"Platte (Kopie, --rw schreibt ins Original): %s\n",hd_opt);
            }
            p8cfg.platte = pfad;
        }
    }
    std::unique_ptr<dbgm::DbgMachine> m_halter;
    try { m_halter = std::make_unique<dbgm::DbgMachine>(art, mcfg, &p8cfg); }
    catch (const std::exception& e){ fprintf(stderr,"%s\n",e.what()); return 2; }
    dbgm::DbgMachine& m = *m_halter;
    if (raf_opt){   // vor dem ersten Lauf stecken (installRaf verlangt das)
        RAF::Typ rt = RAF::Typ::RAF512; bool keine = false;
        if (!dbgm::parseRaf(raf_opt, rt, keine)){
            fprintf(stderr,"--raf: unbekannte Karte '%s' (none|raf128|raf512|raf2m)\n",raf_opt); return 2; }
        if (!keine && !m.base().installRaf(rt)){
            fprintf(stderr,"--raf: %s\n",m.base().rafFehler().c_str()); return 2; }
    }
    { std::string err;   // ebenfalls vor dem ersten Lauf
      if (!dbgm::steckeK6022(m.base(), ptape_opt, err)){ fprintf(stderr,"--ptape: %s\n",err.c_str()); return 2; }
      if (m.base().k6022()) {
          if (!ptape_in.empty() && !m.base().k6022()->bandEinlegenDatei(ptape_in, err)){ fprintf(stderr,"--ptape-in: %s\n",err.c_str()); return 2; }
          if (!ptape_out.empty() && !m.base().k6022()->stanzerBinden(ptape_out, K6022::Format::Roh, err)){ fprintf(stderr,"--ptape-out: %s\n",err.c_str()); return 2; } } }
    m.powerOn();
    const bool K8 = m.einCpu();      // eine CPU: K8915 ODER PRG (keine ZVE2/Snapshots)
    const bool K89 = m.isK8915();
    const bool KP = m.isPrg();       // PRG 710 / 710-1
    const bool KC = m.isPc1715();    // PC 1715: eine CPU, 8275-Bild aus dem Haupt-RAM, ROM-Overlay 24H/28H
    const bool KP1 = m.isPrg1();     // 710-1: Tastatur K7672 an A32-B, Return = 0DH
    EM* em = m.em();          // nullptr ohne --em (und immer am K8915)
    const bool KQ = m.isP8000();   // P8000 (AP P12): U880 + MON8, mit p8000-16 auch U8001/MMU/Kopplung
    // 16-Bit-Karte des P8000 (U8001-Kontext wie beim EM, aber MMU statt Segmentweiche); nullptr ohne `p8000-16`.
    P8000Karte16* p16 = KQ ? m.p8000()->karte16() : nullptr;
    const bool has16 = em || p16;   // es gibt einen U8001 (`cpu u8000`)
    // WDC des P8000 (P13d): Kommando-/Statusprotokoll über `P8000Wdc::statusBeobachter`.
    P8000Wdc* wdcK = KQ ? m.p8000()->wdc() : nullptr;
    struct WdcEreignis { uint64_t t8; uint8_t alt, neu; std::array<uint8_t,9> kmd; uint8_t fehler; };
    std::deque<WdcEreignis> wdc_prot; uint64_t wdc_prot_n = 0;
    if (wdcK) wdcK->statusBeobachter = [&](uint8_t alt, uint8_t neu){
        WdcEreignis e{m.p8000()->totalCycles(), alt, neu, {}, 0};
        for (int i=0;i<9;++i) e.kmd[size_t(i)] = wdcK->lesen(uint16_t(0x30B7+i));
        e.fehler = wdcK->lesen(0x30C7);
        wdc_prot.push_back(e); ++wdc_prot_n;
        if (wdc_prot.size()>4096) wdc_prot.pop_front(); };
    const char* C1 = m.cpuName();            // "ZVE1" (A5120) bzw. "CPU" (K8915) in Meldungen
    if (skip_selftest){
        if (!K89) fprintf(stderr,"WARN: --skip-selftest gibt es nur am K8915 — ignoriert\n");
        else if (dbgm::gen2(*m.k8915()))   // Gen 2: der Stub bei 0400H ist ein anderer; Weg ungeprüft
            fprintf(stderr,"WARN: --skip-selftest gibt es an der Gen 2 nicht — ignoriert\n");
        else {
            // Genau der Weg eines echten Warmstarts (doc/merkposten/k8915.md): steht bei
            // 0000H/0005H im RAM ein JP, springt der Stub bei FFE0H ohne Selbsttest zur
            // Coldstart-Meldung.  Bank 1 direkt — über die CPU-Sicht läge dort das ROM.
            m.k8915()->zre().bankPoke(0, 0x0000, 0xC3);
            m.k8915()->zre().bankPoke(0, 0x0005, 0xC3);
            fprintf(stderr,"K8915: Selbsttest uebersprungen (JP bei 0000H/0005H in Bank 1)\n");
        }
    }
    // Formatname ist bei .hfe/.dmk nur Platzhalter; bei .img entscheidet er.  Der K8915
    // bootet von cpa800, der A5120 zuerst cpa780 (unverändert).
    const std::string pfmt = (KP||KC||KQ) ? m.base().defaultFormatName(0) : std::string();
    const char* fmt1 = (KP||KC||KQ) ? pfmt.c_str() : K8 ? "cpa800" : "cpa780";
    const char* fmt2 = KP ? "udos_ds77" : KC ? "cpa640" : KQ ? pfmt.c_str() : K8 ? "cpa780" : "cpa800";
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
        if (!(m.mountDisk(0,mp,fmt1,wp) || m.mountDisk(0,mp,fmt2,wp))){
            fprintf(stderr,"WARN: mount '%s' failed: %s\n",disk,m.lastError().c_str());
            mount_failed = true;   // session still runs; reflected in the exit code
        }
        else {
            fprintf(stderr,"Mounted %s on A:%s\n",disk,wp?" (read-only)":"");
            // Anpassung ans Laufwerk (Spurdichte/Seitenzahl) gleich mitmelden.
            const std::string hinweis = m.diskNotice(0);
            if (!hinweis.empty()) fprintf(stderr,"  ! %s\n",hinweis.c_str());
        }
    }
    // B:/C:/D: — identisch behandelt, nur der Laufwerksbuchstabe wechselt.
    for (int drv=1; drv<4; ++drv){
        if (!disks[drv]) continue;
        bool wp; std::string mp=prepareDisk(disks[drv],wp);
        const char letter = (char)('A'+drv);
        if (!(m.mountDisk(drv,mp,fmt1,wp) || m.mountDisk(drv,mp,fmt2,wp))){
            fprintf(stderr,"WARN: mount %c '%s' failed: %s\n",letter,disks[drv],
                    m.lastError().c_str());
            mount_failed = true;
        }
        else {
            fprintf(stderr,"Mounted %s on %c:%s\n",disks[drv],letter,wp?" (read-only)":"");
            const std::string hinweis = m.diskNotice(drv);
            if (!hinweis.empty()) fprintf(stderr,"  ! %s\n",hinweis.c_str());
        }
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
    std::vector<char> displays16;               // S5b: im U8001-Kontext angelegt ⇒ U8001-Sicht

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

    // ─── A5120.16: U8001-Kontext (S5) ─────────────────────────────────────────
    int  cpu_ctx = 1;                           // 1 = ZVE1, 2 = ZVE2, 3 = U8001 (`cpu …`)
    std::map<uint32_t,Bp> bp16;                 // U8001-Haltepunkte, Schlüssel seg<<16|off
    long step16_rem=0; bool step16_active=false;
    long resume_skip16=-1, gu16=-1;             // wie resume_skip1 / gu_pc
    bool fin16_active=false; uint16_t fin16_sp=0;
    uint32_t hit_key16=0;                       // Halteadresse, wenn hit_cpu==3
    std::string ev16_why;                       // Halt vor dem nächsten U8001-Befehl (VI)
    int  brk_mode=0; bool brk_vi=false, brk_int16=false;   // bmode (1=→16, 2=→8), bvi, bint16
    FILE* emlog_fp=nullptr; bool emlog_file=false;          // emlog: EM-Transaktionen protokollieren
    dbg16::CallStack16 cs16;
    std::map<uint32_t,uint32_t> hist16;
    long hist16_lo=-1, hist16_hi=-1;            // `hist … lo hi` im U8001-Kontext (Schlüssel)
    long last_u16=-1, last_a16=-1, last_x16=-1;
    // ─── S5b: U8001 wie ZVE1 ────────────────────────────────────────────────────
    dbg16::SymTab16 sym16;                      // Symbole mit Segment (`<<s>>%off NAME`)
    std::map<uint32_t,std::vector<std::string>> logpoints16;   // lp im U8001-Kontext
    struct Watch16 { memwatch::MemWatch32 w; bool raw=false; };  // logisch <<s>>off | roh em:
    std::vector<Watch16> mwatch16;
    std::set<uint16_t> io16_w, io16_b;          // U8001-E/A-Ports: drucken / anhalten
    bool brk16_int=false, brk16_nmi=false, brk16_iret=false;    // bint/bnmi/breti im U8001-Kontext
    bool brk16_trap=false;                      // btrap: interner Trap / Segmenttrap angenommen (P8)
    dbg16::ExcLog16 exc16;                      // `trap`: Ausnahmeprotokoll des U8001 (P8)
    long mark16=-1;                             // `mark <A>` im U8001-Kontext (Schlüssel)
    // P8000 (AP P12): Protokolle der Karten — UA858-Status (`dma log`), Kopplung (`kopp log`), SEGT (`segt`)
    dbgp8::DmaProtokoll dmaProt;
    dbgp8::Protokoll koppProt(512), segtProt(256);
    bool brk16_segt=false;                      // bsegt: Halt vor dem ersten Befehl der SEGT-Behandlung
    std::map<std::string,dbg16::EmSnap> named_em;               // EM-Teil benannter Snapshots
    uint16_t last_pc1=0;                        // ZVE1: Beginn der laufenden Instruktion
    std::string ev_origin;                      // Ereignishalt: wer die Flanke ausgelöst hat
    bool regrab_on_stop=false;                  // Halt mitten in einer Z80-Instruktion: Sicht neu holen

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
        std::string l; int n=0, n16=0, bad16=0;
        while (std::getline(f,l)){
            // S5b: `<<SEG>>%OFFS NAME` (z8kasm --sym) ist ein U8001-Symbol mit Segment.
            { int r16=sym16.parseLine(l); if(r16>0){ ++n16; continue; } if(r16<0){ ++bad16; continue; } }
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
        if (n16||bad16) fprintf(stderr,"  loaded %d symbol(s) + %d U8001-Symbol(e) mit Segment from %s%s\n",
                                n,n16,path.c_str(),bad16?" (unlesbare <<…>>-Zeilen uebergangen)":"");
        else fprintf(stderr,"  loaded %d symbol(s) from %s\n",n,path.c_str());
        return n+n16; };

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
        std::string path; long off=0; std::string mode; int src_lo=-1, src_hi=-1;
        {   size_t at = spec.rfind('@');
            if (at != std::string::npos){
                std::string s = spec.substr(at+1);
                std::string sl = s; for(auto&c:sl) c=(char)tolower(c);
                if (sl=="auto"||sl=="labels"||sl=="noanchor"){ mode=sl; path=spec.substr(0,at); }
            }
        }
        // PFAD[@OFFSET][:VON-BIS] — VON-BIS begrenzt auf einen Teil des Listings
        // (K8915-ROM: 00D0H–03FFH läuft als Kopie bei F0D0H, §8a AP-E4d).
        if (mode.empty() && !prnlst::splitSpecRange(spec,path,off,src_lo,src_hi)){
            fprintf(stderr,"  bad @offset[:von-bis] in '%s'\n",spec.c_str()); return -1; }

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
        // Am K8915 immer MIT Objektbytes: annotiert wird nur, solange die Bytes der
        // Zeile im Speicher stehen (prnFor) — 0000H–0FFFH ist je nach A8H das ROM
        // oder RAM mit dem TPA, und eine ROM-Zeile über einem .COM wäre Irreführung.
        if (mode=="auto"){
            // Versatz an einem eigenen Listing bestimmen — prn kann schon andere
            // Listings samt Objektbytes tragen, die den Abgleich verfälschten.
            prnlst::Listing probe;
            if (probe.load(path, 0, /*want_bytes=*/true) < 0){
                fprintf(stderr,"  cannot open %s\n",path.c_str()); return -1; }
            maclst::Image img; img.byte = probe.bytes_by_addr;
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
                else off = match.offset;
            }
        }
        int n = prn.load(path, off, /*want_bytes=*/K8, src_lo, src_hi);
        if (n < 0){ fprintf(stderr,"  cannot open %s\n",path.c_str()); return n; }
        // Labels (name:) aus dem Listing als Symbole importieren (b/u/list per Name),
        // ohne bestehende (z.B. -s-/user-) Symbole zu überschreiben.
        int li=0;
        for (auto& kv : prn.by_addr){
            std::string lab = prnlst::labelOf(kv.second);
            if (!lab.empty() && sym_by_name.find(lab)==sym_by_name.end()){ symAdd(lab,kv.first); ++li; }
        }
        char off_s[64]={0}; if(off) snprintf(off_s,sizeof off_s," (offset %+ld / %04X)",off,(uint16_t)off);
        if (src_lo>=0){ size_t l=strlen(off_s);
            snprintf(off_s+l,sizeof off_s-l," [Quelle %04X..%04X]",src_lo,src_hi); }
        fprintf(stderr,"  loaded %d listing line(s) from %s%s, %d label(s) → symbols\n",n,path.c_str(),off_s,li);
        return n; };
    // Annotation für eine Adresse (leer, wenn keine .prn-Quelle vorliegt).
    auto prnFor = [&](uint16_t a)->std::string{
        const std::string* s = prn.find(a);
        if (!s) return std::string();
        // K8915: nur, wenn die Bytes der Zeile gerade dort stehen (ROM ein-/ausgeblendet).
        if (K8 && !prn.matches(a,[&](uint16_t x){ return m.memReadDebug(x); })) return std::string();
        return *s; };

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
        last_pc1 = pc;                                   // Auslöser von EM-Ereignissen (S5b)
        if (KQ && dmaProt.an) dmaProt.beobachte(m.machineCycles(), pc, m.p8000()->floppy8().dma().sicht());
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
                std::string why=std::string("bp ")+C1;
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
        snprintf(s,sizeof s,"%s.PC=%04X", m.busMasterIsZVE2()?"ZVE2":C1, m.busMasterPC());
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
            if (KQ){   // P8000: UA858 (3CH–3FH) und Kopplungstore (PIO0 0CH–0FH, Latches 10H–17H)
                if (port>=0x3C && port<=0x3F) dmaProt.port(m.machineCycles(), m.cpuPC(), isRead, data);
                if (dbgp8::koppPort8(port)) koppProt.add(m.machineCycles(), dbgp8::koppText8(port,isRead,data));
            }
            if (io_w.count(port))
                fprintf(stderr,"[io] %c%-9lld %s (%02XH)=%02X  %s\n",
                        rcpfx(),rc(m.cpuCycles()),isRead?"IN ":"OUT",port,data,busWho());
            if (io_b.count(port)){ char w[40]; snprintf(w,sizeof w,"io %s (%02XH)=%02X",isRead?"IN":"OUT",port,data);
                stopFromBus(w); }
            return;
        }
        hitMem(isRead, addr, data);
    });

    // ═══ A5120.16: der U8001 als dritte CPU (S5, Plan 17 §3) ═══════════════════
    // Mit EM (`--em em256|em064`) gibt es neben ZVE1/ZVE2 den U8001 auf der Steuerkarte.
    // `cpu u8000` schaltet r/s/n/u/a/b/where/hist/bt/snap/rs/x/d/e/set auf ihn um.
    // Speicher sieht er NUR über die Segmentweiche A42 (wie ein Befehl im jetzigen
    // Zustand: A33, N/S, Status), roh über `em:ZELLE`.  Haltepunkte greifen VOR dem
    // Befehl: EM::setStepHook hält den U8001 an, bevor er ihn ausführt; die Zeit
    // bleibt als Guthaben stehen und wird beim Fortsetzen nachgeholt.
    auto z16 = [&]()->Z8000& { return em ? em->u8001() : p16->cpu(); };
    auto key16 = [](uint8_t seg, uint16_t off)->uint32_t{ return (uint32_t(seg&0x7F)<<16)|off; };
    auto pcKey16 = [&](const Z8000& z)->uint32_t{ return key16(z.pcSeg, z.pc); };
    // SP-Offset, wie ihn der laufende Modus benutzt (segmentiert: R15 von RR14).
    auto sp16 = [&](const Z8000& z)->uint16_t{ return z.r(15); };
    auto cycle16 = [&](uint8_t seg, uint16_t off, bool instr)->Z8kBusCycle{
        Z8kBusCycle c; c.st = instr? Z8kStatus::MemInstr : Z8kStatus::MemData;
        c.system = z16().systemMode(); c.word = false; c.read = true;
        c.seg = seg; c.addr = off; return c; };
    // Byte über die Segmentweiche (instr = Programmspeicher, für Mode 1 relevant).
    // Ein Byte hinter dem Buszyklus @p c, ohne Nebenwirkung: am EM über die Segmentweiche A42, am P8000
    // über die Auswahllogik und die drei UB8010 (`P8000MmuLogik16::probe`); offener Bus liest FFH.
    auto rdbCyc16 = [&](const Z8kBusCycle& c)->uint8_t{
        if (em) return em->peek(em->cellFor16(c));
        uint8_t v; dbgp8::physByte(*p16, p16->mmu().probe(c).zugriff, 0, v); return v; };
    auto wrbCyc16 = [&](const Z8kBusCycle& c, uint8_t v){
        if (em){ em->poke(em->cellFor16(c), v); return; }
        const auto z = p16->mmu().probe(c).zugriff;
        using Zl = P8000MmuLogik16::Ziel;
        if (z.unterdrueckt) return;
        if (z.ziel==Zl::OnBoardSram) p16->sram(uint16_t(z.adresse)) = v;
        else if (z.ziel==Zl::Hauptspeicher) p16->dram().poke(z.adresse, v);
        /* EPROM/leer: nicht beschreibbar */ };
    auto rdb16 = [&](uint8_t seg, uint16_t off, bool instr)->uint8_t{
        return rdbCyc16(cycle16(seg,off,instr)); };
    auto wrb16 = [&](uint8_t seg, uint16_t off, bool instr, uint8_t v){
        wrbCyc16(cycle16(seg,off,instr), v); };
    // Rohzugriff (`em:ZELLE`): am EM die DRAM-Zelle, am P8000 die physische 24-Bit-Hauptspeicheradresse.
    auto rdRaw16 = [&](uint32_t cell)->uint8_t{
        if (em) return em->peek(cell);
        uint8_t v; dbgp8::physHaupt(*p16, cell & 0xFFFFFF, v); return v; };
    auto wrRaw16 = [&](uint32_t cell, uint8_t v){
        if (em) em->poke(cell, v); else p16->dram().poke(cell & 0xFFFFFF, v); };
    auto rdw16 = [&](uint8_t seg, uint16_t off, bool instr)->uint16_t{
        off = uint16_t(off & ~1u);
        return uint16_t((rdb16(seg,off,instr)<<8) | rdb16(seg,uint16_t(off+1),instr)); };
    // Addr16 (Segmentweiche oder roh) + Versatz → Byte / Schreiben.
    auto rdA16 = [&](const dbg16::Addr16& a, uint32_t i)->uint8_t{
        if (a.kind==dbg16::Addr16::Raw) return rdRaw16(a.cell+i);
        return rdb16(a.seg, uint16_t(a.off+i), false); };
    auto wrA16 = [&](const dbg16::Addr16& a, uint32_t i, uint8_t v){
        if (a.kind==dbg16::Addr16::Raw) wrRaw16(a.cell+i, v);
        else wrb16(a.seg, uint16_t(a.off+i), false, v); };
    auto a16Text = [&](const dbg16::Addr16& a, uint32_t i)->std::string{
        char b[32];
        if (a.kind==dbg16::Addr16::Raw){
            if (em) snprintf(b,sizeof b,"em:%05X",(unsigned)((a.cell+i)%em->size()));
            else    snprintf(b,sizeof b,"em:%06X",(unsigned)((a.cell+i)&0xFFFFFF));   // P8000: physisch
            return b; }
        return dbg16::addrText(a.seg, uint16_t(a.off+i), true); };
    // Adresse der 16-Bit-Seite aus einem Wort; Vorgabesegment = PC-Segment.
    // Symbolauflösung der 16-Bit-Seite: erst die U8001-Tabelle (mit Segment), dann die
    // Z80-Tabelle (16 Bit ⇒ PC-Segment); NAME+OFF/NAME-OFF in beiden.
    auto symVal16 = [&](const std::string& n, long& v)->bool{
        uint32_t k; if (sym16.find(n,k)){ v=(long)k; return true; }
        size_t p=n.find_first_of("+-",1); std::string base=p==std::string::npos? n : n.substr(0,p);
        auto it=sym_by_name.find(base); if(it==sym_by_name.end()) return false;
        long d=0; if(p!=std::string::npos){ long x; if(!dbg16::parseNumber(n.substr(p+1),x)) return false; d = n[p]=='-'? -x : x; }
        v=(long)(uint16_t)(it->second+d); return true; };
    auto parse16 = [&](const std::string& tok, dbg16::Addr16& a)->bool{
        if (!dbg16::parseAddr(tok, has16? z16().pcSeg : 0, a, symVal16)){
            fprintf(stderr,"  ? Adresse '%s' (<<seg>>off | em:ZELLE | Zahl | Symbol)\n",tok.c_str()); return false; }
        return true; };
    // Symbol für eine Adresse der 16-Bit-Seite: genau / nächstes darunter (bt, hist).
    auto symAt16   = [&](uint32_t key)->std::string{ return sym16.at(key); };
    auto symNear16 = [&](uint32_t key)->std::string{ return sym16.nearest(key); };
    auto disasm16 = [&](uint8_t seg, uint16_t off, char* out, size_t n)->int{
        const Z8000& z = z16();
        z8k::Line l = z8k::disasm([&](uint16_t o){ return rdw16(seg,o,true); }, off, z.segMode(), seg);
        char hex[32]={0};
        for (int i=0;i<l.bytes && i<10;i+=2){ char w[8]; snprintf(w,sizeof w,"%04X ",rdw16(seg,uint16_t(off+i),true)); strcat(hex,w); }
        // Symbole wie bei ZVE1: <NAME> hinter der Adresse, Sprungziel-Name hinter dem Befehl.
        std::string lab = symAt16(key16(seg,off)), tgt;
        if (l.hasTarget){ std::string ts = symAt16(key16(z.segMode()? l.targetSeg : seg, l.target));
            if(!ts.empty()) tgt=" <"+ts+">"; }
        snprintf(out,n,"%s%s: %-25s %s%s", dbg16::addrText(seg,off,z.segMode()).c_str(),
                 lab.empty()?"":(" <"+lab+">").c_str(), hex, l.text.c_str(), tgt.c_str());
        return l.bytes; };
    auto showInsn16 = [&](const char* tag, uint32_t key){
        char l[160]; disasm16(uint8_t(key>>16), uint16_t(key), l, sizeof l);
        fprintf(stderr,"  %s %s\n",tag,l); };
    auto state16 = [&](const Z8000& z)->const char*{
        if (z.inReset()) return (em? em->reset16() : p16->resetEingang())? "RESET16" : "Reset-Ablauf";
        if (z.stopped()) return "STOP";
        if (z.busAck())  return "BUSAK";
        if (z.halted())  return "HALT";
        return "run"; };
    // S5b: EM-Zustand für `snap`/`snap diff` (Register, Karte, A22, PIO A32, DRAM).
    auto captureEm = [&](dbg16::EmSnap& s){
        const Z8000& z = z16();
        s = dbg16::EmSnap{}; s.valid=true;
        for (int i=0;i<16;++i) s.r[i]=z.r(unsigned(i));
        const int other = z.systemMode()? 0 : 1; s.r14o=z.R14[other]; s.r15o=z.R15[other];
        s.fcw=z.fcw; s.pc=z.pc; s.pcSeg=z.pcSeg; s.psapSeg=z.psapSeg; s.psapOff=z.psapOff; s.refresh=z.refresh;
        s.cyc=z.cycles; s.state=state16(z);
        for (int p=0;p<16;++p) s.attr[p]=em->attribute(p);
        s.a33=em->steuer16(); s.a34=em->vector8(); s.a35=em->status16(); s.a36=em->status8(); s.a53=em->a53();
        s.seg=em->segment(); s.vi=em->viPending(); s.mode8=em->mode8(); s.reset16=em->reset16(); s.ramen=em->ramEnabled();
        s.stop=em->stop16(); s.trq8=em->trq8(); s.tren=em->tren(); s.busrq=em->busRq16(); s.busak=em->busAck16();
        s.pr=em->prLine(); s.pe=em->parityError(); s.a54=em->a54Freigabe(); s.nvi=em->nviLine();
        auto p=em->pio().debugState();
        for (int i=0;i<2;++i){ auto& pt=p.port[i];
            s.pio[i][0]=pt.mode; s.pio[i][1]=pt.out; s.pio[i][2]=pt.in; s.pio[i][3]=pt.dir; s.pio[i][4]=pt.vector;
            s.pio[i][5]=uint8_t((pt.ie?1:0)|(pt.pending?2:0)|(pt.ius?4:0)); }
        s.dram.resize(em->size());
        for (uint32_t c=0;c<em->size();++c) s.dram[c]=em->peek(c); };
    auto printRegs16 = [&]{
        const Z8000& z = z16();
        std::string sy = sym16.nearest(pcKey16(z));
        fprintf(stderr,"  U8001 PC=%s%s%s%s FCW=%%%04X %s  PSAP=%s REFRESH=%%%04X cyc=%llu [%s]\n",
                dbg16::addrText(z.pcSeg,z.pc,true).c_str(), sy.empty()?"":" <",sy.c_str(),sy.empty()?"":">",
                z.fcw, dbg16::fcwText(z.fcw).c_str(),
                dbg16::addrText(uint8_t((z.psapSeg>>8)&0x7F), z.psapOff, true).c_str(),
                z.refresh, (unsigned long long)z.cycles, state16(z));
        fprintf(stderr,"   ");
        for (int i=0;i<16;++i){ fprintf(stderr," R%-2d=%04X",i,z.r(i)); if(i==7) fprintf(stderr,"\n   "); }
        // die andere Bank von R14/R15 (System ↔ Normal)
        const int other = z.systemMode()? 0 : 1;
        fprintf(stderr,"\n    %s: R14'=%04X R15'=%04X\n", other? "System-SP":"Normal-SP",
                z.R14[other], z.R15[other]);
        // P8: Pins/Merker, letzter Buszyklus, letzte Ausnahme
        fprintf(stderr,"    Pins: %s\n", dbg16::pinsText(z).c_str());
        fprintf(stderr,"    Bus:  %s\n", dbg16::cycleText(z.lastCycle(), z.lastCycleData()).c_str());
        if (exc16.total())
            fprintf(stderr,"    Ausnahme: %s\n", dbg16::exceptionText(z.lastException(), z.isZ8001()).c_str());
    };
    auto emLine = [&]{
        if (!em){   // P8000: Karte16 statt EM-Steuerkarte
            const auto sc = p16->mmu().sicht();
            fprintf(stderr,"  Karte16: SCR=%02X (MMU %s, On-Board %s, %s) NBR=%02X TRPL=%02X IF1L=%02X  /SEGT=%s  "
                           "RESET16=%d  NMI-Merker=%d\n",
                    sc.scr, (sc.scr&P8000MmuLogik16::SCR_MMU_ON)?"EIN":"aus",
                    (sc.scr&P8000MmuLogik16::SCR_BDMEM_AUS)?"aus":"ein",
                    (sc.scr&P8000MmuLogik16::SCR_SEG_USR)?"SEG USR":"nichtsegm.", sc.nbr, sc.trpl, sc.if1l,
                    sc.segt?"AKTIV":"-", p16->inReset(), z16().nmiPending());
            return; }
        fprintf(stderr,"  EM: %s-Bit-Mode  A33=%02X (SegMode %u) A35=%02X Status8=%02X Vektor8=%02X%s  "
                       "TREN=%d TRQ8=%d BUSRQ16=%d BUSAK16=%d RESET16=%d STOP=%d RAMEN=%d\n",
                em->mode8()?"8":"16", em->steuer16(), em->segMode(), em->status16(), em->status8(),
                em->vector8(), em->viPending()?" VI!":"", em->tren(), em->trq8(), em->busRq16(),
                em->busAck16(), em->reset16(), em->stop16(), em->ramEnabled()); };
    auto stopAt16 = [&](const std::string& why){
        hit=true; hit_cpu=3; hit_key16=pcKey16(z16()); stop_reason=why; m.stop(); };
    // Trace-Zeile des U8001 (Konsole bzw. Datei).
    auto traceLine16To = [&](FILE* fp, const Z8000& z){
        char l[200]; disasm16(z.pcSeg, z.pc, l, sizeof l);
        fprintf(fp,"T3 %c%-9lld %-58s FCW=%04X R0=%04X R1=%04X R2=%04X R3=%04X SP=%04X\n",
                rcpfx(), rc(m.cpuCycles()), l, z.fcw, z.r(0), z.r(1), z.r(2), z.r(3), sp16(z)); };

    // ─── S5b: Ausdrücke in der U8001-Sicht (b … if, lp, disp, x-Adresse) ──────────
    // Register wie der laufende Modus sie sieht, Flags, PC/PCSEG, Karte (A33…, A53, PE);
    // Speicher [x]/[x]w/[x]l durch die Segmentweiche (Datenzugriff, big-endian),
    // [em:x] roh ins DRAM.  Grammatik und Operatoren wie bei ZVE1 (tools/expr_eval.h).
    auto eval16 = [&](const std::string& ex, bool& ok)->long long{
        const Z8000& z = z16();
        dbg16::RegView16 rv;
        for (int i=0;i<16;++i) rv.r[i]=z.r(unsigned(i));
        rv.fcw=z.fcw; rv.pc=z.pc; rv.pcSeg=z.pcSeg; rv.psapSeg=z.psapSeg; rv.psapOff=z.psapOff; rv.refresh=z.refresh;
        if (em){
        rv.haveEm=true; rv.a33=em->steuer16(); rv.a34=em->vector8(); rv.a35=em->status16(); rv.a36=em->status8();
        rv.a53=em->a53(); rv.pe=em->parityError(); rv.a54=em->a54Freigabe(); rv.mode8=em->mode8(); }
        expreval::Env env;
        env.reg = [&](const std::string& U, long long& v){ return dbg16::reg16(rv,U,v); };
        env.mem = [&](long long a, int size, bool raw, long long& v)->bool{
            v=0;
            if (raw){ for (int k=0;k<size;++k) v=(v<<8)|rdRaw16(uint32_t(a)+uint32_t(k)); return true; }
            uint8_t sg; uint16_t off; dbg16::decodeAddr(a, z.pcSeg, sg, off);
            for (int k=0;k<size;++k) v=(v<<8)|rdb16(sg,uint16_t(off+k),false);
            return true; };
        env.sym = [&](const std::string& n, long long& v)->bool{ long x; if(!symVal16(n,x)) return false; v=x; return true; };
        return expreval::evalEnv(ex, env, ok); };
    auto evalCond16 = [&](const std::string& c)->bool{
        if (c.empty()) return true;
        bool ok; long long v=eval16(c,ok);
        if (!ok){ fprintf(stderr,"  [cond] Ausdruck '%s' nicht auswertbar — Halt\n",c.c_str()); return true; }
        return v!=0; };
    auto exprText16 = [&](const std::string& e)->std::string{
        bool ok; long long v=eval16(e,ok); char b[96];
        if (!ok) snprintf(b,sizeof b,"  %s=?",e.c_str());
        else snprintf(b,sizeof b,"  %s=%lld(%%%llX)",e.c_str(),v,(unsigned long long)(v & 0xFFFFFFFFLL));
        return b; };
    // Logpoint des U8001: drucken und weiterlaufen (wie ZVE1 `[lp]`).
    auto logHit16 = [&](const Z8000& z, const std::vector<std::string>& exprs){
        char l[200]; disasm16(z.pcSeg, z.pc, l, sizeof l);
        fprintf(stderr,"[lp] %c%-9lld %s",rcpfx(),rc(m.cpuCycles()),l);
        for (auto& e: exprs) fprintf(stderr,"%s",exprText16(e).c_str());
        fprintf(stderr,"\n"); };
    // Der Befehl, der gerade lief (bzw. zuletzt lief): Beginn aus Z8000::lastPc().
    auto lastInsn16 = [&]()->std::string{
        const Z8000& z = z16(); char l[200]; disasm16(z.lastPcSeg(), z.lastPc(), l, sizeof l); return l; };
    // Wer hat einen Speicher-/E/A-Zugriff ausgelöst?  U8001 mit Busstatus, sonst U880.
    auto accWho = [&](const EM::Zugriff& a)->std::string{
        char b[160];
        if (a.by16){ const Z8000& z=z16();
            std::string s=symNear16(key16(z.lastPcSeg(),z.lastPc()));
            snprintf(b,sizeof b,"U8001 PC=%s%s%s%s (%s, %s)",
                     dbg16::addrText(z.lastPcSeg(),z.lastPc(),true).c_str(), s.empty()?"":" <",s.c_str(),s.empty()?"":">",
                     z8kStatusName(a.cycle.st), a.cycle.system?"System":"Normal"); }
        else snprintf(b,sizeof b,"U880 [%04X] %s.PC=%04X", a.addr8, m.busMasterIsZVE2()?"ZVE2":"ZVE1", m.busMasterPC());
        return b; };

    // Rückruf vor jedem Befehl des U8001 (EM: `EM::setStepHook`, P8000: `P8000Karte16::schrittHaken`).
    auto stepHook16 = [&](const Z8000& z)->bool{
            // Ein anderer Halt (Haltepunkt, Überwachung, Ereignis) ist schon beschlossen:
            // der U8001 läuft dann nicht mehr weiter (sonst holte er den Rest des Guthabens nach).
            if (hit && !hist_on) return true;
            // Keine Befehlsgrenze: Resetablauf (RESET16 gerade gelöst), HALT, Stop-Refresh,
            // BUSAK, laufender Wiederholungsbefehl.
            if (z.inReset() || z.halted() || z.stopped() || z.busAck() || z.inRepeat()) return false;
            const uint32_t key = pcKey16(z);
            if (hist_on){ if (hist16_lo<0 || (key>=(uint32_t)hist16_lo && key<=(uint32_t)hist16_hi)) hist16[key]++; return false; }
            bool skip = (resume_skip16 >= 0 && key == (uint32_t)resume_skip16);
            resume_skip16 = -1;
            // Aufrufstapel: vor dem Befehl einordnen, CALL vormerken (Dekodieren kostet —
            // aber nur, solange der U8001 läuft).
            const uint16_t sp = sp16(z);
            cs16.onInstruction(key, sp);
            z8k::Line l = z8k::disasm([&](uint16_t o){ return rdw16(z.pcSeg,o,true); }, z.pc, z.segMode(), z.pcSeg);
            if (l.dec.insn && l.dec.insn->has(z8k::Z8K_CALL))
                cs16.noteCall(key, key16(z.pcSeg, uint16_t(z.pc + l.bytes)), sp);
            // „laufen und mitschreiben" zuerst (kein Halt): Datei-Trace, Logpoints, mark.
            if (trace_fp && trace_lines < trace_cap){ traceLine16To(trace_fp, z); ++trace_lines; }
            if (!logpoints16.empty()){ auto lit=logpoints16.find(key); if (lit!=logpoints16.end()) logHit16(z,lit->second); }
            if (mark16>=0 && key==(uint32_t)mark16){ rel_origin=m.cpuCycles(); rel_armed=true; mark16=-1;
                fprintf(stderr,"[mark] relative origin set at U8001 PC=%s (abs cyc=%llu)\n",
                        dbg16::addrText(z.pcSeg,z.pc,true).c_str(),(unsigned long long)rel_origin); }
            if (!ev16_why.empty()){ std::string w=ev16_why; ev16_why.clear(); stopAt16(w); return true; }
            if (step16_active){
                if (step16_rem<=0){ step16_active=false; stopAt16("step U8001"); return true; }
                traceLine16To(stderr, z); --step16_rem; return false;
            }
            if (fin16_active && sp > fin16_sp){ fin16_active=false; stopAt16("step-out U8001"); return true; }
            if (skip) return false;
            if (gu16 >= 0 && key == (uint32_t)gu16){ gu16=-1; stopAt16("run-until U8001"); return true; }
            // breti im U8001-Kontext: vor dem IRET (wie ZVE1 vor RETI/RETN).
            if (brk16_iret && l.dec.insn && !strcmp(l.dec.insn->mn,"IRET")){ stopAt16("IRET U8001"); return true; }
            auto it=bp16.find(key);
            if (it!=bp16.end() && it->second.enabled && evalCond16(it->second.cond)){
                it->second.hits++;
                if (it->second.ignore>0){ it->second.ignore--; return false; }
                std::string why="bp U8001";
                if (!it->second.cond.empty()) why+=" ["+it->second.cond+"]";
                if (it->second.temp) bp16.erase(it);
                stopAt16(why); return true;
            }
            return false;
    };
    // Ausnahmen des U8001 ins Protokoll (`trap`); `btrap` hält vor dem ersten Befehl der Behandlung eines
    // internen Traps (EPA/PRIV/SC) bzw. SEGT.  Auch die angenommenen Interrupts gehen in `itrace`.
    auto exHook16 = [&](const Z8kExceptionInfo& x){
        exc16.add(x);
        if (itrace_fp && (x.kind==Z8kException::Vi || x.kind==Z8kException::Nvi || x.kind==Z8kException::Nmi)){
            const Z8000& zz = z16();
            fprintf(itrace_fp,"IT16 %c%-9lld %-3s U8001 %s\n", rcpfx(), rc(m.cpuCycles()),
                    z8kExceptionName(x.kind), dbg16::exceptionText(x).c_str()); (void)zz;
            ++itrace_n; }
        if (hist_on || hit || !brk16_trap) return;
        if (x.kind==Z8kException::ExtendedInstruction || x.kind==Z8kException::Privileged ||
            x.kind==Z8kException::SystemCall || x.kind==Z8kException::SegmentTrap)
            ev16_why = std::string("Trap angenommen: ") + dbg16::exceptionText(x);
    };
    if (em){
        em->setStepHook(stepHook16);
        // S5b: Watchpoints der 16-Bit-Seite und E/A-Ports des U8001.  Ein Wortzugriff
        // wird in seine zwei Bytes zerlegt (gerade Adresse = oberes Byte), damit die
        // Wertbedingungen (== != changed) wie bei ZVE1 byteweise gelten.
        em->setAccessHook([&](const EM::Zugriff& a){
            if (hist_on) return;
            if (a.io){
                const uint16_t port=a.cycle.addr;
                if (io16_w.count(port))
                    fprintf(stderr,"[io16] %c%-9lld %s %s %%%04X=%0*X  %s\n", rcpfx(), rc(m.cpuCycles()),
                            a.read?"IN ":"OUT", a.cycle.st==Z8kStatus::SpecialIo?"SPEZ":"STD", port,
                            a.word?4:2, a.value, accWho(a).c_str());
                if (io16_b.count(port) && !hit){ char w[96];
                    snprintf(w,sizeof w,"io16 %s %%%04X=%0*X",a.read?"IN":"OUT",port,a.word?4:2,a.value);
                    ev_origin = accWho(a); stopAt16(w); }
                return;
            }
            if (mwatch16.empty()) return;
            const int nb = a.word? 2 : 1;
            for (int k=0;k<nb;++k){
                const uint8_t  byte = a.word? uint8_t(k? a.value : a.value>>8) : uint8_t(a.value);
                const uint32_t cell = a.cell + uint32_t(k);
                const uint16_t off  = a.word? uint16_t((a.cycle.addr & ~1u) + k) : a.cycle.addr;
                const uint32_t lkey = key16(a.cycle.seg, off);
                for (auto& wt : mwatch16){
                    if (!wt.raw && !a.by16) continue;             // logisch = nur der U8001
                    if (!wt.w.matches(a.read, wt.raw? cell : lkey, byte)) continue;
                    ++wt.w.hits;
                    char where_[48];
                    if (a.by16) snprintf(where_,sizeof where_,"%s (em:%05X)",
                                         dbg16::addrText(a.cycle.seg,off,true).c_str(),(unsigned)cell);
                    else        snprintf(where_,sizeof where_,"em:%05X",(unsigned)cell);
                    const char* we = (!a.read && !a.wirksam)? " (WE=0, nicht geschrieben)" : "";
                    if (wt.w.brk){
                        if (hit) continue;
                        char w[160]; snprintf(w,sizeof w,"watch %s %s=%02X%s by %s",a.read?"RD":"WR",where_,byte,we,accWho(a).c_str());
                        if (a.by16) stopAt16(w);
                        else stopFromBus(w);
                    } else
                        fprintf(stderr,"[%s16] %c%-9lld %s %s=%02X%s  %s\n", a.read?"wr":"wp", rcpfx(), rc(m.cpuCycles()),
                                a.read?"RD":"WR", where_, byte, we, accWho(a).c_str());
                }
            }
        });
        em->setEventHook([&](const EM::EreignisInfo& e){
            if (emlog_fp){
                const Z8000& z = z16();
                fprintf(emlog_fp,"EM %c%-9lld %-52s ZVE1.PC=%04X U8001.PC=%s\n", rcpfx(), rc(m.cpuCycles()),
                        emtrace::text(e).c_str(), m.cpuPC(), dbg16::addrText(z.pcSeg,z.pc,true).c_str());
            }
            using E = EM::Ereignis;
            // itrace: angenommene Interrupts des U8001 mitschreiben (wie INT/NMI der ZVE1).
            if (itrace_fp && (e.kind==E::ViQuittung || e.kind==E::NviQuittung || e.kind==E::NmiQuittung)){
                const Z8000& z = z16();
                fprintf(itrace_fp,"IT16 %c%-9lld %-3s U8001 bei %s  Kennung=%04X  SP=%04X\n", rcpfx(), rc(m.cpuCycles()),
                        e.kind==E::ViQuittung?"VI":e.kind==E::NviQuittung?"NVI":"NMI",
                        dbg16::addrText(z.pcSeg,z.pc,true).c_str(), e.value, sp16(z));
                ++itrace_n;
            }
            if (hist_on || hit) return;
            // Halt GENAU an der Flanke (S5b).  Das Ereignis kommt synchron aus dem Zyklus,
            // der die Flanke erzeugt; gehalten wird
            //  - by16: vor dem NÄCHSTEN Befehl des U8001 (sein Befehl mit dem auslösenden
            //    Buszyklus ist fertig, ein Z8000-Schritt ist unteilbar); ZVE1 steht an der
            //    Befehlsgrenze, bis zu der die Maschinenzeit gerade nachgezogen wurde;
            //  - m1: VOR dem U880-Befehl, dessen M1 das FF A29 kippte (abortBeforeExecute);
            //  - sonst (E/A des U880): nach dem U880-Befehl mit dem E/A-Zyklus, vor dem
            //    nächsten; der U8001 holt die Zeit dieses Befehls nicht mehr nach.
            // Gezeigt wird die CPU des Kontexts; die Zeile „ausgelöst" nennt den Verursacher.
            auto origin = [&]()->std::string{
                char b[240];
                if (e.by16){ const Z8000& z=z16();
                    snprintf(b,sizeof b,"U8001, Befehl %s%s — Halt vor dem naechsten U8001-Befehl",
                             lastInsn16().c_str(), z.busAck()?" (danach BUSAK)":""); }
                else if (e.m1){ char l[120]; disasmAt(m.cpuPC(),l,sizeof l);
                    snprintf(b,sizeof b,"M1 des U880 (FF A29) vor %s — der Befehl ist noch nicht gelaufen",l); }
                else { char l[120]; disasmAt(last_pc1,l,sizeof l);
                    snprintf(b,sizeof b,"U880, E/A-Zyklus in %s — Halt nach diesem Befehl",l); }
                return b; };
            auto stopEv = [&](const std::string& why){
                ev_origin = origin();
                if (cpu_ctx==3){ hit=true; hit_cpu=3; hit_key16=pcKey16(z16()); stop_reason=why; m.stop(); }
                else { stopFromBus(why); regrab_on_stop=true; }
            };
            if (brk_mode && e.kind==E::Modus && ((e.value==0 && (brk_mode&1)) || (e.value==1 && (brk_mode&2))))
                stopEv(e.value? "Moduswechsel → 8-Bit-Mode" : "Moduswechsel → 16-Bit-Mode");
            else if (brk_int16 && e.kind==E::Int16 && e.value==1)
                stopEv("INT-16 (A33 Bit 4 → PIO A4)");
            else if ((brk_vi || brk16_int) && e.kind==E::ViQuittung){
                // Mitten im Interrupteintritt: gehalten wird VOR dem ersten Befehl der ISR.
                char w[64]; snprintf(w,sizeof w,"VI angenommen, Kennung=%04X",e.value); ev16_why=w; }
            else if (brk16_int && e.kind==E::NviQuittung){
                char w[64]; snprintf(w,sizeof w,"NVI angenommen (A53=%u)",e.value); ev16_why=w; }
            else if (brk16_nmi && e.kind==E::NmiQuittung)
                ev16_why="NMI angenommen (U8001)";
            else if (brk_vi && e.kind==E::Vektor8)
                fprintf(stderr,"  [vi] U880 OUT ADH=%02X → VI am U8001 (Halt bei der Quittung)\n",e.value&0xFF);
        });
        // P8: jede angenommene Ausnahme des U8001 ins Protokoll (`trap`); `btrap` hält vor
        // dem ersten Befehl der Behandlung eines internen Traps (EPA/PRIV/SC) bzw. SEGT.
        z16().onException = exHook16;
    }
    // P8000: der U8001 der 16-Bit-Karte — dieselben Rückrufe, dazu SEGT-/Kopplungsprotokoll (AP P12a/b).
    if (p16){
        p16->schrittHaken = stepHook16;
        z16().onException = exHook16;
        p16->mmu().onSegtEreignis = [&](const P8000MmuLogik16::SegtEreignis& e){
            segtProt.add(m.machineCycles(), dbgp8::segtText(e));
            if (hist_on || hit || !brk16_segt) return;
            ev16_why = "SEGT durch die MMU: " + dbgp8::segtText(e);
        };
        p16->zyklusHaken = [&](const Z8kBusCycle& c, uint16_t d, bool rd){
            if (hist_on) return;
            if (c.st==Z8kStatus::Io){
                const std::string kt = dbgp8::koppText16(c.addr, rd, d);
                if (!kt.empty()) koppProt.add(m.machineCycles(), kt);
            }
            if (c.st==Z8kStatus::Io || c.st==Z8kStatus::SpecialIo){
                if (io16_w.count(c.addr))
                    fprintf(stderr,"[io16] %c%-9lld %s %s %%%04X=%04X  U8001 PC=%s\n", rcpfx(), rc(m.cpuCycles()),
                            rd?"IN ":"OUT", c.st==Z8kStatus::SpecialIo?"SPEZ":"STD", c.addr, d,
                            dbg16::addrText(z16().lastPcSeg(),z16().lastPc(),true).c_str());
                if (io16_b.count(c.addr) && !hit){ char w[96];
                    snprintf(w,sizeof w,"io16 %s %%%04X=%04X",rd?"IN":"OUT",c.addr,d);
                    stopAt16(w); }
            }
        };
    }

    // ─── helpers ───────────────────────────────────────────────────────────────
    auto printSnap = [&](int cpu){
        const Snap& s = (cpu==2)?snap2:snap1;
        const std::string who = (cpu==2)? std::string("ZVE2") : std::string(C1);
        if (!s.valid){ fprintf(stderr,"  (%s: no state captured yet — run first)\n",who.c_str()); return; }
        uint16_t ret=(uint16_t)(m.memReadDebug(s.SP)|(m.memReadDebug(s.SP+1)<<8));
        char fl[12]; flagsStr(s.AF,fl);
        fprintf(stderr,
            "  %s PC=%04X SP=%04X(->%04X) AF=%04X[%s] BC=%04X DE=%04X HL=%04X "
            "IX=%04X IY=%04X  AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X IM=%u%s R=%02X%s cyc=%llu\n",
            who.c_str(),s.PC,s.SP,ret,s.AF,fl,s.BC,s.DE,s.HL,s.IX,s.IY,s.AF_,s.BC_,s.DE_,s.HL_,
            s.I,(unsigned)s.IM,s.IFF1?" EI":" DI",s.R, s.halted?" HALT":"", (unsigned long long)s.cyc);
    };
    auto stateLine = [&]{
        if (KP){   // PRG: Speicherverwaltung (EBH Freigabe, E8H[0]/[F]) statt A8H
            Prg710Machine& k=*m.prg();
            fprintf(stderr,"  state: EBH=%02X map=%s E8H[0]=%02X E8H[F]=%02X  %c-cyc=%lld%s\n",
                k.speicher().freigabe(), dbgm::speicherbild(k).c_str(), k.speicher().attr(0),
                k.speicher().attr(15), rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
            return;
        }
        if (KC){   // PC 1715: ROM-Overlay (24H/28H) und BWS-Register 34H
            Pc1715Zre& z=m.pc1715()->zre();
            fprintf(stderr,"  state: ROM=%s BWS=%02X (Basis %04X, ZG%d)  %c-cyc=%lld%s\n",
                z.romEin()?"ein":"aus", z.bwsRegister(), z.bildBasis(), z.bwsZg2()?2:1,
                rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
            return;
        }
        if (KQ){   // P8000: ADP-Speicherbild, RFF und die U8001-Seite
            P8000Karte8& k=m.p8000()->karte8();
            fprintf(stderr,"  state: RFF=%d map=%s  %c-cyc=%lld%s",k.speicher().rffGesetzt()?1:0,
                dbgp8::speicherbild(k.speicher()).c_str(), rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
            if (p16) fprintf(stderr,"  U8001=%s SCR=%02X",state16(z16()),p16->mmu().scr());
            fprintf(stderr,"\n");
            return;
        }
        if (K8){   // K8915: eine CPU; was zählt, ist das Speicherbild (A8H) und /MEMDI
            K8915Machine& k=*m.k8915();
            fprintf(stderr,"  state: A8H=%02X map=%s /MEMDI=%s 61H=%02X  %c-cyc=%lld%s\n",
                dbgm::k8A8(k), dbgm::speicherbild(k).c_str(), dbgm::k8Memdi(k)?"1":"0",
                k.ats().anzeige(), rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
            return;
        }
        fprintf(stderr,"  state: ROM=%s BUSRQ=%s ZVE2=%s  %c-cyc=%lld%s",
            m.isRomEnabled()?"on":"off", m.isBUSRQ()?"yes":"no",
            m.isZVE2InReset()?"reset":(m.isZVE2Waiting()?"wait":"run"),
            rcpfx(), rc(m.cpuCycles()), rel_armed?" (rel)":"");
        if (em) fprintf(stderr,"  EM=%s-Bit U8001=%s",em->mode8()?"8":"16",state16(z16()));
        fprintf(stderr,"\n");
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
            // Im U8001-Kontext angelegt ⇒ in der U8001-Sicht, gleich welche CPU hielt.
            if (i<displays16.size() && displays16[i]){
                if (has16) fprintf(stderr,"  disp[%zu] U8001%s\n",i,exprText16(displays[i]).c_str());
                continue; }
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
        std::string origin; origin.swap(ev_origin);
        if (hit_cpu==3 && has16){
            // Halt aus einem Buszyklus (Watchpoint, Ereignis): der U8001 steht erst jetzt an
            // der Befehlsgrenze — die Haltezeile zeigt diese, nicht die Mitte des Befehls.
            hit_key16 = pcKey16(z16());
            std::string sy = symNear16(hit_key16);
            fprintf(stderr,"** %s : U8001 PC=%s%s%s%s\n",stop_reason.c_str(),
                    dbg16::addrText(uint8_t(hit_key16>>16),uint16_t(hit_key16),true).c_str(),
                    sy.empty()?"":" <",sy.c_str(),sy.empty()?"":">");
            if (!origin.empty()) fprintf(stderr,"   ausgeloest: %s\n",origin.c_str());
            printRegs16(); showInsn16("=>",pcKey16(z16())); showDisplays(); emLine(); stateLine();
            if (stop_reason.rfind("bp",0)==0){
                bphit_ring.emplace_back(); m.captureState(bphit_ring.back(), false);
                while (bphit_ring.size()>bphit_cap) bphit_ring.pop_front(); }
            return;
        }
        if (hit_cpu==3) hit_cpu=1;
        if (regrab_on_stop){ regrab_on_stop=false; snap1=grab(m.cpuDebug()); hit_pc=m.cpuPC(); }
        fprintf(stderr,"** %s : %s PC=%04X\n",stop_reason.c_str(),hit_cpu==2?"ZVE2":C1,hit_pc);
        if (!origin.empty()) fprintf(stderr,"   ausgeloest: %s\n",origin.c_str());
        printSnap(hit_cpu);
        showInsn("=>", hit_pc);
        showDisplays();
        stateLine();
        rst38Hint();
        // §17: remember the full state at each PC-breakpoint stop so `rc` can jump back
        // to the previous hit (the snapshot ring only holds coarse pre-command states).
        if (!K8 && stop_reason.rfind("bp",0)==0){   // K8915: keine Snapshots
            bphit_ring.emplace_back(); m.captureState(bphit_ring.back(), false);
            while (bphit_ring.size()>bphit_cap) bphit_ring.pop_front();
        }
    };
    auto screen = [&]{
        for (int row=0;row<24;++row){ char ln[81];
            for (int c=0;c<80;++c){ uint8_t ch=m.screenByte(row,c);
                ln[c]=(ch>=0x20&&ch<0x7F)?(char)ch:'.'; }
            ln[80]=0; fprintf(stderr,"  |%s|\n",ln); }
    };
    // ── screen text as a condition (doc/feature_requests/interaktive_programme.md #1/#5) ──
    // Render one 80-char row of the text VRAM (0xF800) into `out` (no trailing NUL
    // handling needed by callers — 80 chars). Non-printable → space (so matches
    // survive control bytes / the cursor-flag high bit).
    auto vramRow = [&](int row, char* out){
        for (int c=0;c<80;++c){ uint8_t ch=m.screenByte(row,c);
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
        if (KP){   // PRG: eine CPU, K5122 im /WAIT-Betrieb, Speicherverwaltung E8H-EBH
            Prg710Machine& p=*m.prg();
            uint16_t pc=m.cpuPC();
            if (json){
                fprintf(stderr,
                  "\n{\"machine\":\"%s\",\"pc\":\"0x%04X\",\"ebh\":\"0x%02X\",\"map\":\"%s\","
                  "\"k5122\":{\"drive\":%u,\"mounted\":%s,\"cyl\":%u,\"head\":%u,"
                  "\"transferring\":%s,\"write\":%s,\"headPos\":%zu,\"trackLen\":%zu}}\n",
                  KP1?"prg710-1":"prg710",pc,p.speicher().freigabe(),dbgm::speicherbild(p).c_str(),
                  (unsigned)k.drive,k.mounted?"true":"false",(unsigned)k.cylinder,
                  (unsigned)k.head,k.transferring?"true":"false",k.writeMode?"true":"false",k.headPos,k.trackLen);
                return;
            }
            char l1[120]; disasmAt(pc,l1,sizeof l1); std::string p1=prnFor(pc);
            fprintf(stderr,"  CPU  %s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str());
            fprintf(stderr,"  EBH=%02X  map=%s  (Z=ZRE V=VRAM 0-F=OPS-Seite -=leer; je 4 KB ab 0000H)\n",
                    p.speicher().freigabe(),dbgm::speicherbild(p).c_str());
            fprintf(stderr,"  K5122 (/WAIT): D%d %s cyl=%u head=%u %s%s headPos=%zu/%zu MKE=%d"
                    " geschrieben=%zu B, ganze Spuren=%llu\n",
                    k.drive,k.mounted?"mounted":"EMPTY",(unsigned)k.cylinder,(unsigned)k.head,
                    k.transferring?"READING":"idle",k.writeMode?"+WRITE":"",k.headPos,k.trackLen,
                    k.waitMke?1:0,k.waitSchreibBytes,(unsigned long long)k.waitSpuren);
            fprintf(stderr,"  Takte=%llu\n",(unsigned long long)m.machineCycles());
            rst38Hint();
            return;
        }
        if (KC){   // PC 1715: eine CPU, K5122 im /WAIT-Betrieb, ROM-Overlay, BWS 34H
            Pc1715Zre& z=m.pc1715()->zre();
            uint16_t pc=m.cpuPC();
            if (json){
                fprintf(stderr,
                  "\n{\"machine\":\"pc1715\",\"pc\":\"0x%04X\",\"rom\":%s,\"bws\":\"0x%02X\",\"basis\":\"0x%04X\",\"zg\":%d,"
                  "\"k5122\":{\"drive\":%u,\"mounted\":%s,\"cyl\":%u,\"head\":%u,"
                  "\"transferring\":%s,\"write\":%s,\"headPos\":%zu,\"trackLen\":%zu}}\n",
                  pc,z.romEin()?"true":"false",z.bwsRegister(),z.bildBasis(),z.bwsZg2()?2:1,
                  (unsigned)k.drive,k.mounted?"true":"false",(unsigned)k.cylinder,
                  (unsigned)k.head,k.transferring?"true":"false",k.writeMode?"true":"false",k.headPos,k.trackLen);
                return;
            }
            char l1[120]; disasmAt(pc,l1,sizeof l1); std::string p1=prnFor(pc);
            fprintf(stderr,"  CPU  %s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str());
            fprintf(stderr,"  ROM-Overlay 0000-07FF: %s   BWS=%02X  Bildbasis %04X  ZG%d\n",
                    z.romEin()?"ein (Lesen = S502, Schreiben = RAM)":"aus",z.bwsRegister(),z.bildBasis(),z.bwsZg2()?2:1);
            fprintf(stderr,"  K5122 (/WAIT): D%d %s cyl=%u head=%u %s%s headPos=%zu/%zu MKE=%d"
                    " geschrieben=%zu B, ganze Spuren=%llu\n",
                    k.drive,k.mounted?"mounted":"EMPTY",(unsigned)k.cylinder,(unsigned)k.head,
                    k.transferring?"READING":"idle",k.writeMode?"+WRITE":"",k.headPos,k.trackLen,
                    k.waitMke?1:0,k.waitSchreibBytes,(unsigned long long)k.waitSpuren);
            fprintf(stderr,"  Takte=%llu\n",(unsigned long long)m.machineCycles());
            rst38Hint();
            return;
        }
        if (KQ){   // P8000: U880, UA858, ADP-Speicherbild, mit 16-Bit-Karte der U8001
            P8000Machine& pm=*m.p8000(); uint16_t pc=m.cpuPC();
            const auto dm=pm.floppy8().dma().sicht();
            if (json){
                fprintf(stderr,"\n{\"machine\":\"p8000\",\"pc\":\"0x%04X\",\"rff\":%s,\"map\":\"%s\",\"dma_rr0\":\"0x%02X\"",
                        pc,pm.karte8().speicher().rffGesetzt()?"true":"false",
                        dbgp8::speicherbild(pm.karte8().speicher()).c_str(),dm.status);
                if (p16) fprintf(stderr,",\"u8001_pc\":\"%s\",\"u8001\":\"%s\",\"scr\":\"0x%02X\"",
                        dbg16::addrText(z16().pcSeg,z16().pc,true).c_str(),state16(z16()),p16->mmu().scr());
                fprintf(stderr,",\"cpu\":\"%s\"}\n", cpu_ctx==3?"u8000":"u880");
                return; }
            char l1[120]; disasmAt(pc,l1,sizeof l1); std::string p1=prnFor(pc);
            fprintf(stderr,"  U880 %s%s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str(),cpu_ctx!=3?"  <- Kontext":"");
            fprintf(stderr,"  RFF=%d  map=%s  (E=EPROM S=SRAM D=DRAM M=mehrere -=leer; je 4 KB ab 0000H)\n",
                    pm.karte8().speicher().rffGesetzt()?1:0,dbgp8::speicherbild(pm.karte8().speicher()).c_str());
            fprintf(stderr,"  UA858 RR0=%02X [%s]  %s%s\n",dm.status,dbgp8::dmaRr0Text(dm.status).c_str(),
                    dm.freigegeben?"frei ":"gesperrt ",dm.busAnforderung?"BUSRQ":"");
            if (p16){ char l3[160]; const Z8000& z3=z16(); disasm16(z3.pcSeg,z3.pc,l3,sizeof l3);
                fprintf(stderr,"  U8001 %s   [%s] SCR=%02X%s\n",l3,state16(z3),p16->mmu().scr(),cpu_ctx==3?"  <- Kontext":""); }
            fprintf(stderr,"  Takte=%llu\n",(unsigned long long)m.machineCycles());
            rst38Hint();
            return;
        }
        if (K8){   // K8915: eine CPU, K5122 im /WAIT-Betrieb, Speicherbild A8H
            K8915Machine& k8=*m.k8915();
            uint16_t pc=m.cpuPC();
            if (json){
                fprintf(stderr,
                  "\n{\"machine\":\"%s\",\"pc\":\"0x%04X\",\"a8\":\"0x%02X\",\"map\":\"%s\",\"memdi\":%s,"
                  "\"lamps\":\"0x%02X\",\"k5122\":{\"drive\":%u,\"mounted\":%s,\"cyl\":%u,\"head\":%u,"
                  "\"transferring\":%s,\"write\":%s,\"headPos\":%zu,\"trackLen\":%zu}}\n",
                  dbgm::gen2(k8)?"k8915-g2":"k8915",pc,dbgm::k8A8(k8),dbgm::speicherbild(k8).c_str(),dbgm::k8Memdi(k8)?"true":"false",
                  k8.ats().anzeige(),(unsigned)k.drive,k.mounted?"true":"false",(unsigned)k.cylinder,
                  (unsigned)k.head,k.transferring?"true":"false",k.writeMode?"true":"false",k.headPos,k.trackLen);
                return;
            }
            char l1[120]; disasmAt(pc,l1,sizeof l1); std::string p1=prnFor(pc);
            fprintf(stderr,"  CPU  %s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str());
            fprintf(stderr,"  A8H=%02X  map=%s  (R=ROM 1=Bank1 2=Bank2 .=Bus; je 4 KB ab 0000H)  /MEMDI=%s\n",
                    dbgm::k8A8(k8),dbgm::speicherbild(k8).c_str(),dbgm::k8Memdi(k8)?"1":"0");
            fprintf(stderr,"  K5122 (/WAIT): D%d %s cyl=%u head=%u %s%s headPos=%zu/%zu MKE=%d"
                    " geschrieben=%zu B, ganze Spuren=%llu\n",
                    k.drive,k.mounted?"mounted":"EMPTY",(unsigned)k.cylinder,(unsigned)k.head,
                    k.transferring?"READING":"idle",k.writeMode?"+WRITE":"",k.headPos,k.trackLen,
                    k.waitMke?1:0,k.waitSchreibBytes,(unsigned long long)k.waitSpuren);
            fprintf(stderr,"  Anzeigefeld 61H=%02X (%s)   Takte=%llu\n",k8.ats().anzeige(),
                    dbgm::lampen61(k8.ats().anzeige()).c_str(),(unsigned long long)m.machineCycles());
            rst38Hint();
            return;
        }
        uint16_t pc1=m.cpuPC(); const Z80& z2=m.zve2Debug(); uint16_t pc2=z2.PC;
        const char* z2s = m.isZVE2InReset()?"reset":(m.isZVE2Waiting()?"wait":"run");
        if (json){
            fprintf(stderr,
              "\n{\"zve1_pc\":\"0x%04X\",\"zve2_pc\":\"0x%04X\",\"zve2\":\"%s\",\"busrq\":%s,"
              "\"busmaster\":\"%s\",\"k5122\":{\"drive\":%u,\"mounted\":%s,\"cyl\":%u,\"head\":%u,"
              "\"transferring\":%s,\"write\":%s,\"headPos\":%zu,\"trackLen\":%zu}",
              pc1,pc2,z2s, m.isBUSRQ()?"true":"false", zve2Active()?"ZVE2":"ZVE1",
              (unsigned)k.drive,k.mounted?"true":"false",(unsigned)k.cylinder,(unsigned)k.head,
              k.transferring?"true":"false",k.writeMode?"true":"false",k.headPos,k.trackLen);
            if (em){ const Z8000& z3=z16();
                fprintf(stderr,",\"u8001_pc\":\"%s\",\"u8001\":\"%s\",\"em_mode\":%d",
                        dbg16::addrText(z3.pcSeg,z3.pc,true).c_str(),state16(z3),em->mode8()?8:16); }
            fprintf(stderr,",\"cpu\":\"%s\"}\n", cpu_ctx==3?"u8000":cpu_ctx==2?"zve2":"zve1");
            return;
        }
        char l1[120],l2[120]; disasmAt(pc1,l1,sizeof l1); disasmAt(pc2,l2,sizeof l2);
        std::string p1=prnFor(pc1), p2=prnFor(pc2);
        fprintf(stderr,"  ZVE1 %s%s%s\n",l1,p1.empty()?"":"  ; ",p1.c_str());
        fprintf(stderr,"  ZVE2 %s%s%s   [%s]\n",l2,p2.empty()?"":"  ; ",p2.c_str(),z2s);
        if (em){ char l3[160]; const Z8000& z3=z16(); disasm16(z3.pcSeg,z3.pc,l3,sizeof l3);
            fprintf(stderr,"  U8001 %s   [%s] %s-Bit-Mode%s\n",l3,state16(z3),em->mode8()?"8":"16",
                    cpu_ctx==3?"  <- Kontext":""); }
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
        if (KP){ fprintf(stderr,"  … %llu cyc  PC=%04X  EBH=%02X  [Ctrl-C bricht ab]\n",
                         (unsigned long long)(runClock()-start), m.cpuPC(), m.prg()->speicher().freigabe());
                 return; }
        if (KC){ fprintf(stderr,"  … %llu cyc  PC=%04X  BWS=%02X  [Ctrl-C bricht ab]\n",
                         (unsigned long long)(runClock()-start), m.cpuPC(), m.pc1715()->zre().bwsRegister());
                 return; }
        if (KQ){ fprintf(stderr,"  … %llu cyc  PC=%04X  RFF=%d  [Ctrl-C bricht ab]\n",
                         (unsigned long long)(runClock()-start), m.cpuPC(), m.p8000()->karte8().speicher().rffGesetzt()?1:0);
                 return; }
        if (K8){ fprintf(stderr,"  … %llu cyc  PC=%04X  A8H=%02X  [Ctrl-C bricht ab]\n",
                         (unsigned long long)(runClock()-start), m.cpuPC(), dbgm::k8A8(*m.k8915()));
                 return; }
        fprintf(stderr,"  … %llu cyc (%s)  ZVE1 PC=%04X  ZVE2 PC=%04X  busrq=%s  [Ctrl-C bricht ab]\n",
                (unsigned long long)(runClock()-start), clock_machine?"Maschine":"ZVE1",
                m.cpuPC(), m.zve2PC(), m.isBUSRQ()?"yes":"no");
    };
    // §9 `hist <cycles> [lo hi]`: run N cycles profiling BOTH CPUs' PCs, print the
    // hotspots (with symbol/.prn annotation). One glance instead of reading a trace file.
    auto runHist = [&](uint64_t cycles, int lo, int hi){
        hist1.clear(); hist2.clear(); hist16.clear(); hist_lo=lo; hist_hi=hi; hist_on=true;
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
        top(hist1,C1); if(!K8) top(hist2,"ZVE2");
        if (has16){
            if (hist16.empty()){ fprintf(stderr,"  U8001: (no samples)\n"); return; }
            std::vector<std::pair<uint32_t,uint32_t>> v; uint64_t tot=0;
            for (auto& kv:hist16){ v.push_back({kv.second,kv.first}); tot+=kv.second; }
            std::sort(v.rbegin(),v.rend());
            fprintf(stderr,"  U8001 top (%llu instrs, %zu distinct PCs):\n",(unsigned long long)tot,hist16.size());
            for (size_t i=0;i<v.size() && i<15;++i){ char l[200];
                disasm16(uint8_t(v[i].second>>16),uint16_t(v[i].second),l,sizeof l);
                std::string sy=symNear16(v[i].second);
                // Name der umgebenden Routine, wenn die Zeile selbst keinen trägt.
                bool own = !symAt16(v[i].second).empty();
                fprintf(stderr,"    %6.2f%%  %6u  %s%s%s\n",100.0*v[i].first/(double)tot,v[i].first,l,
                        (sy.empty()||own)?"":"   ; in ",(sy.empty()||own)?"":sy.c_str()); }
        }
    };
    // silent run kernel: runs until a stop is signalled or budget/cap reached.
    // Beim Fortsetzen wird die aktuelle Adresse beider CPUs als „einmal nicht halten"
    // vorgemerkt — sonst hielte ein Breakpoint, auf dem wir gerade STEHEN, sofort wieder
    // (break-before-execute, s. tools/k1520dbg.md §2).
    auto armResume = [&]{ resume_skip1 = (int)m.cpuPC(); resume_skip2 = (int)m.zve2PC();
        resume_skip16 = has16? (long)pcKey16(z16()) : -1; };
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
        if (KP){
            // PRG: Taste drücken + loslassen (je 100 000 Takte, wie die Tests).  Return =
            // ET1 (QK_TASTE_BASE|37H) am 710 (K7609), Qt-Return am 710-1 (K7672 → 0DH).
            uint64_t ran=0; int n=0;
            for (size_t i=0;i<t.size();++i){
                uint32_t code=decodeKey(t,i); ++n;
                if (code==0x01000004u && !KP1) code = K7609::QK_TASTE_BASE | 0x37u;
                else if (code<0x20 && code!=0x0D) code = 0x0D;   // am K7609/K7672 ohne Rohcode
                m.keyPress(code,false,false); ran+=goSilent(100000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
                m.keyRelease(code);           ran+=goSilent(100000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
            }
            fprintf(stderr,"   keys: %d Zeichen getippt, %llu cyc (PC=%04X)\n",n,
                    (unsigned long long)ran,m.cpuPC());
            return;
        }
        if (KC){
            // PC 1715: Tastatur1715 (eigener U880) braucht Entprellung — Taste 150 000 Takte
            // gedrückt, 100 000 Pause (wie tests/support/pc1715_input.h).  Return = Qt-Return.
            uint64_t ran=0; int n=0;
            for (size_t i=0;i<t.size();++i){
                uint32_t code=decodeKey(t,i); ++n;
                bool strg=false;
                if (code>=0x01 && code<=0x1A && code!=0x0D) { code += 0x60; strg=true; }   // ^A..^Z = Strg + Buchstabe (\x0b = ^K)
                else if (code<0x20 && code!=0x01000004u) code = 0x01000004u;   // CR/LF/übrige Steuerzeichen → Return
                m.keyPress(code,false,strg); ran+=goSilent(150000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
                m.keyRelease(code);           ran+=goSilent(100000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
            }
            fprintf(stderr,"   keys: %d Zeichen getippt, %llu cyc (PC=%04X)\n",n,
                    (unsigned long long)ran,m.cpuPC());
            return;
        }
        if (KQ){
            // P8000: Terminal tty1 — das Terminal reiht die Zeichen im Zeichentakt aus (9600 Bd 8N2 ≈ 4 600 Takte
            // je Zeichen); je Taste 10 000 Takte Luft.  Return = Qt-Return; `\r`/`\n` im Text ebenso.
            uint64_t ran=0; int n=0;
            for (size_t i=0;i<t.size();++i){
                uint32_t code=decodeKey(t,i); ++n;
                if (code<0x20 && code!=0x01000004u && code!=0x1B && code!=0x09 && code!=0x08 && code!=0x03) code = 0x01000004u;
                m.keyPress(code,false,false); ran+=goSilent(10000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
            }
            fprintf(stderr,"   keys: %d Zeichen getippt, %llu cyc (PC=%04X)\n",n,(unsigned long long)ran,m.cpuPC());
            return;
        }
        if (K8){
            // K8915: jedes Zeichen als fertiges Zeichen der K7672 (sendeZeichen) — im
            // SCP-Modus das Zeichen selbst, im DCP-Modus (SCPX) die Taste samt Umschalt/
            // Strg als Scancodes Satz 1.  So kommt auch ^C (\x03) an.  Eine Taste je
            // 750 000 Takte (≈ 0,3 s), wie beim A5120 Drücken + Loslassen zusammen.
            uint64_t ran=0; int n=0;
            for (size_t i=0;i<t.size();++i){
                uint32_t code=decodeKey(t,i); ++n;
                uint8_t ch = (code==0x01000004u)? 0x0D : (uint8_t)code;
                m.k8915()->keyboard().sendeZeichen(ch);
                ran+=goSilent(750000);
                if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); return; }
            }
            fprintf(stderr,"   keys: %d Zeichen getippt, %llu cyc (PC=%04X)\n",n,
                    (unsigned long long)ran,m.cpuPC());
            return;
        }
        for (size_t i=0;i<t.size();++i){
            uint32_t code=decodeKey(t,i);
            // Steuerzeichen (\x03 = ^C, \e = ESC, \t …) kennt K7637::translateKey nicht
            // als Qt-Code (→ 0x00, Taste verpufft) — als Rohbyte senden (QK_RAW_BASE);
            // das CP/A-BIOS reicht Codes < 20H unverändert durch.
            if (code < 0x20) code |= 0x02000000u;
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
    // ── Konsolenmodus: die Maschine LIVE bedienen ────────────────────────────
    // Der eigentliche Gewinn ist nicht das Emulieren im Terminal (dafuer gibt es
    // die Oberflaeche), sondern dass die HALTEPUNKTE SCHARF BLEIBEN: von Hand bis
    // zum Fehler bedienen, dann steht die Maschine im Debugger. Das kann sonst
    // nichts — die Oberflaeche hat keine Haltepunkte, der Debugger hatte keine
    // lebende Eingabe.
    //
    // Warum das verlustfrei geht: der K7024 ist ein ZEICHENbildschirm, Bit [6:0]
    // Code, Bit 7 Cursor (k7024.cpp). Ein Terminal gibt ihn Zelle fuer Zelle wieder.
    auto consoleMode = [&](double speed){
        if (!k1520::os::isTerminal(0)){
            fprintf(stderr,"  console braucht ein Terminal — im Pipe-/Skriptbetrieb "
                           "stattdessen keys/gscreen benutzen\n");
            return;
        }
        k1520term::RawMode raw;
        if (!raw.ok()){ fprintf(stderr,"  console: Terminal laesst sich nicht in den "
                                       "Rohmodus schalten\n"); return; }
        if (speed <= 0.0) speed = 1.0;

        k1520term::Keyboard kb;
        k1520term::ScreenDiff scr;
        k1520term::ScreenDiff::clear(stderr);
        scr.invalidate();

        // Anschlagstakt: der K7637 sendet den Code SOFORT beim Druecken und wiederholt
        // erst nach 500 ms (REPEAT_DELAY_MS). Also kurz halten (bleibt unter der
        // Wiederholschwelle) und danach eine Luecke lassen, damit zwei gleiche Tasten
        // hintereinander zwei Bytes ergeben (die Strecke laeuft mit 9600 Baud).
        constexpr uint64_t kHold = 60000;   // ~24 ms Maschinenzeit
        constexpr uint64_t kGap  = 20000;   // ~8 ms
        std::deque<int> pending; int held=-1; uint64_t hold_until=0, gap_until=0;

        const uint64_t per_frame = (uint64_t)(2450000.0 * 0.020 * speed);
        auto next = std::chrono::steady_clock::now();
        bool leave=false;

        std::string status = "  Ctrl-]  zurueck in den Debugger";
        if (disks[0]) status += "   |  A: " + std::string(disks[0]);
        for (int d=1;d<4;++d) if (disks[d])
            status += std::string("  ") + (char)('A'+d) + ": " + disks[d];

        while (!leave){
            for (;;){                                   // alle anliegenden Tasten holen
                int k = kb.read();
                if (k == k1520term::KEY_NONE) break;
                if (k == k1520term::KEY_LEAVE){ leave=true; break; }
                pending.push_back(k);
            }
            if (leave) break;

            const uint64_t now = runClock();
            if (held>=0 && now>=hold_until){ m.keyRelease(held); held=-1; gap_until=now+kGap; }
            if (held<0 && !pending.empty() && now>=gap_until){
                held = pending.front(); pending.pop_front();
                m.keyPress((uint32_t)held,false,false);
                hold_until = now + kHold;
            }

            goSilent(per_frame);
            scr.render([&](int r,int c){ return m.screenByte(r,c); }, stderr, status);
            if (hit) break;                             // Haltepunkt → zurueck in den Debugger

            next += std::chrono::milliseconds(20);
            std::this_thread::sleep_until(next);
        }

        if (held>=0) m.keyRelease(held);
        raw.disable();
        k1520term::ScreenDiff::clear(stderr);
        if (hit){ fprintf(stderr,"  (console verlassen: Haltepunkt)\n"); onStop(); }
        else      fprintf(stderr,"  (console verlassen)\n");
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
        if (K8) return;                    // K8915: keine Snapshots (Savestates nur A5120)
        rev_ring.emplace_back();
        // RAF-Inhalt (bis 2 MB) NICHT je Schritt — 200 Stände wären bis 400 MB.  `rs`/`rc`
        // stellen daher nur das RAF-Latch zurück; benannte Snapshots nehmen den Inhalt mit.
        m.captureState(rev_ring.back(), false);
        while (rev_ring.size() > rev_cap) rev_ring.pop_front();
    };
    // Restore a snapshot and re-sync the debugger's view (call-stack history is reset).
    auto applySnapshot = [&](const A5120Machine::MachineSnapshot& s,const char* what){
        bool ok = m.restoreState(s);
        if (!ok && !m.stateError().empty()){   // unpassende RAF: nichts übernommen
            fprintf(stderr,"  restore abgelehnt: %s\n",m.stateError().c_str()); return; }
        callstack.clear();                 // call history can't be reconstructed
        cs16.clear();
        snap1=grab(m.cpuDebug()); snap2=Snap{};
        if(!ok) fprintf(stderr,"  note: ROM-mapping differs from snapshot — RAM+regs restored,"
                               " but this snapshot predates/postdates the boot-ROM unmap.\n");
        fprintf(stderr,"  restored %s\n",what);
        if (cpu_ctx==3 && has16) showInsn16("=>",pcKey16(z16()));
        else showInsn("=>",m.cpuPC());
        stateLine();
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

    // ─── U8001-Kontext: Kommandos, die `cpu u8000` umschaltet (S5) ─────────────
    // Liefert true, wenn das Kommando hier erledigt wurde.  Speicherkommandos mit
    // einer `em:`-Adresse landen in JEDEM Kontext hier (roh ins DRAM des EM).
    auto isRawTok = [](const std::string& s){ return s.size()>3 && (s.compare(0,3,"em:")==0||s.compare(0,3,"EM:")==0); };
    // Geparkt (RESET16, Bus abgegeben, STOP): dann gibt es keinen Befehl zum Schritt.
    auto parked16 = [&]()->bool{
        const Z8000& z = z16();
        if (!em){   // P8000: Reset-Eingang X3:B1 (K11) bzw. Reset-Ablauf
            if (z.inReset()){
                fprintf(stderr,"  (U8001 laeuft nicht: %s — K11/RESET16 loesen: der U880-Monitor tut es mit `x`/Boot)\n",state16(z)); return true; }
            return false; }
        if (em->reset16() || (z.busAck() && em->busRq16()) || (z.stopped() && em->stop16())){
            fprintf(stderr,"  (U8001 laeuft nicht: %s — erst RESET16/BUSRQ/STOP loesen)\n",state16(z)); return true; }
        return false; };
    // Einzelschritte laufen, bis der Schritt-Rückruf hält; höchstens 50 Mio. Takte
    // (parkt der U8001 unterwegs, z. B. per TRQ8/BUSAK, kommt er nicht mehr voran).
    auto runStep16 = [&](long k){
        step16_rem=k; step16_active=true; m.clearStop(); hit=false; armResume();
        uint64_t start=runClock();
        while(step16_active){ int n=m.run(20000); if(n==0||hit) break;
            if (runClock()-start > 50000000ULL){ fprintf(stderr,"  (U8001 kam in 50 Mio. Takten nicht voran — geparkt?)\n"); break; } }
        step16_active=false; };
    auto step16 = [&](long k){
        if (parked16()) return;
        runStep16(k);
        if(hit){ hit=false; onStop(); } else stateLine(); };
    // Aufrufstapel des U8001 mit Symbolen; gleiche Folgerahmen gefaltet (wie ZVE1 `bt`).
    auto bt16 = [&](int depth){
        const Z8000& z = z16();
        auto an=[&](uint32_t k){ std::string s=symNear16(k); return s.empty()? s : " <"+s+">"; };
        auto at=[&](uint32_t k){ return dbg16::addrText(uint8_t(k>>16),uint16_t(k),true); };
        fprintf(stderr,"  #0 %s%s\n",at(pcKey16(z)).c_str(),an(pcKey16(z)).c_str());
        const auto& f = cs16.frames();
        int frame=1;
        for (auto it=f.rbegin(); it!=f.rend() && frame<=depth; ){
            int reps=0; auto j=it;
            while (j!=f.rend() && j->site==it->site && j->target==it->target && j->ret==it->ret){ ++reps; ++j; }
            fprintf(stderr,"  #%d %s (call → %s%s, ret %s)%s", frame, at(it->site).c_str(),
                    at(it->target).c_str(), an(it->target).c_str(), at(it->ret).c_str(), an(it->site).c_str());
            if (reps>1) fprintf(stderr,"   ↻ ×%d",reps);
            fprintf(stderr,"\n");
            it=j; ++frame;
        }
        if (f.empty()) fprintf(stderr,"  (Aufrufstapel leer — erst laufen/schrittweise fahren; 'bt scan' sucht im Stapel)\n");
    };
    // `bt scan`: Stapel nach Rücksprungadressen absuchen (ohne Befehlsgeschichte, z. B.
    // gleich nach restore).  Segmentiert: Paare (Segmentwort 0sss ssss 0000 0000, Offset),
    // sonst einzelne Worte; plausibel ist eine Adresse, vor der ein CALL/CALR endet.
    auto bt16Scan = [&](int depth){
        const Z8000& z = z16();
        const bool sg = z.segMode();
        const uint8_t spSeg = sg? uint8_t((z.r(14)>>8)&0x7F) : z.pcSeg;
        uint16_t sp = z.r(15);
        fprintf(stderr,"  #0 %s\n",dbg16::addrText(z.pcSeg,z.pc,true).c_str());
        int frame=1;
        auto stackW=[&](uint16_t o){ Z8kBusCycle c; c.st=Z8kStatus::MemStack; c.system=z.systemMode(); c.seg=spSeg; c.addr=uint16_t(o&~1u);
            Z8kBusCycle c1=c, c2=c; c2.addr=uint16_t(c.addr+1);
            return uint16_t((rdbCyc16(c1)<<8)|rdbCyc16(c2)); };
        for (int o=0; o<depth*32 && frame<=depth; o+=2){
            uint8_t rs = z.pcSeg; uint16_t ro;
            if (sg){ uint16_t w0=stackW(uint16_t(sp+o)); if (w0 & 0x80FF) continue;
                     rs=uint8_t((w0>>8)&0x7F); ro=stackW(uint16_t(sp+o+2)); }
            else ro=stackW(uint16_t(sp+o));
            for (int len=2; len<=6; len+=2){
                z8k::Line l = z8k::disasm([&](uint16_t a){ return rdw16(rs,a,true); }, uint16_t(ro-len), sg, rs);
                if (l.bytes==len && l.dec.insn && l.dec.insn->has(z8k::Z8K_CALL)){
                    uint32_t k=key16(rs,ro); std::string s=symNear16(key16(rs,uint16_t(ro-len)));
                    fprintf(stderr,"  #%d %s (ret, via SP+%d)  %s%s%s\n",frame,dbg16::addrText(rs,ro,true).c_str(),o,
                            l.text.c_str(), s.empty()?"":"  <", s.empty()?"":(s+">").c_str());
                    (void)k; ++frame; break; }
            }
        }
    };
    // Adresse der 16-Bit-Seite oder ein Ausdruck (Register, [RR14]l …) — wie ZVE1 `x HL`.
    auto addrOrExpr16 = [&](const std::string& tok, dbg16::Addr16& a)->bool{
        if (dbg16::parseAddr(tok, z16().pcSeg, a, symVal16)) return true;
        bool ok; long long v=eval16(tok,ok);
        if (!ok){ fprintf(stderr,"  ? Adresse/Ausdruck '%s'\n",tok.c_str()); return false; }
        a=dbg16::Addr16{}; dbg16::decodeAddr(v, z16().pcSeg, a.seg, a.off); return true; };
    auto handle16 = [&](const std::vector<std::string>& t)->bool{
        const std::string& cmd=t[0];
        const bool ctx = (cpu_ctx==3);
        // Eine `em:`-Adresse schickt ein Speicherkommando in JEDEM Kontext hierher.
        auto rawAt = [&](size_t i){ return t.size()>i && isRawTok(t[i]); };
        const bool rawCmd =
            (rawAt(1) && (cmd=="d"||cmd=="dump"||cmd=="e"||cmd=="x"||cmd.rfind("x/",0)==0||cmd=="u"||
                          cmd=="wp"||cmd=="wpr"||cmd=="wb"||cmd=="wpa"||cmd=="wbr"||cmd=="wba"||cmd=="wd")) ||
            (rawAt(2) && (cmd=="load"||cmd=="save")) ||
            (cmd=="verify" && t.size()>2 && isRawTok(t[2][0]=='@'? t[2].substr(1) : t[2]));
        if (!ctx && !rawCmd) return false;
        if (!has16){ fprintf(stderr,"  (kein U8001 in dieser Maschine — A5120: --em em256, P8000: --machine p8000-16)\n"); return true; }
        Z8000& z = z16();
        // Beobachtungspunkte auf Speicher/Ports der 16-Bit-Seite gibt es am P8000 nur für E/A (iow/iob);
        // Speicher: `ml`/`mp` + Haltepunkt/`bsegt` — der Zugriffshaken der Karte meldet je Zyklus, nicht je Byte.
        if (p16 && (cmd=="wp"||cmd=="wpr"||cmd=="wb"||cmd=="wpa"||cmd=="wbr"||cmd=="wba"||cmd=="wd")){
            fprintf(stderr,"  (Speicher-Watchpoints der 16-Bit-Seite gibt es am P8000 nicht — Haltepunkt `b`, `bsegt`, E/A: iow/iob)\n");
            return true; }
        if (cmd=="r"){ printRegs16(); showInsn16("=>",pcKey16(z)); emLine(); stateLine(); return true; }
        if (cmd=="rj"){
            fprintf(stderr,"\n{\"cpu\":\"u8001\",\"pc\":\"%s\",\"pcseg\":%u,\"pcoff\":\"0x%04X\",\"fcw\":\"0x%04X\",\"r\":[",
                    dbg16::addrText(z.pcSeg,z.pc,true).c_str(), z.pcSeg, z.pc, z.fcw);
            for (int i=0;i<16;++i) fprintf(stderr,"%s\"0x%04X\"", i?",":"", z.r(i));
            if (em)
            fprintf(stderr,"],\"state\":\"%s\",\"mode\":%d,\"a33\":\"0x%02X\",\"a53\":%u,\"a54\":%s,\"pe\":%s,\"sym\":\"%s\",\"cyc\":%llu,",
                    state16(z), em->mode8()?8:16, em->steuer16(), em->a53(), em->a54Freigabe()?"true":"false",
                    em->parityError()?"true":"false", symNear16(pcKey16(z)).c_str(), (unsigned long long)z.cycles);
            else {   // P8000: Karte16 (SCR, NBR, TRPL, IF1L) statt der EM-Steuerkarte
                const auto sc = p16->mmu().sicht();
                fprintf(stderr,"],\"state\":\"%s\",\"scr\":\"0x%02X\",\"nbr\":\"0x%02X\",\"trpl\":\"0x%02X\",\"if1l\":\"0x%02X\","
                               "\"sym\":\"%s\",\"cyc\":%llu,",
                        state16(z), sc.scr, sc.nbr, sc.trpl, sc.if1l, symNear16(pcKey16(z)).c_str(), (unsigned long long)z.cycles); }
            // P8: Pins und letzter Buszyklus (Status-Code, N/S, SN)
            const Z8kBusCycle& lc = z.lastCycle();
            fprintf(stderr,"\"nmi_merker\":%s,\"segt\":%s,\"vi\":%s,\"nvi\":%s,\"st\":%u,\"st_name\":\"%s\",\"ns\":\"%s\",\"sn\":%u,\"ad\":\"0x%04X\",\"ausnahmen\":%llu,\"letzte_ausnahme\":\"%s\"}\n",
                    z.nmiPending()?"true":"false", z.segtLine()?"true":"false", z.viLine()?"true":"false",
                    z.nviLine()?"true":"false", unsigned(lc.st), z8kStatusName(lc.st), lc.system?"S":"N", lc.seg, lc.addr,
                    (unsigned long long)exc16.total(), exc16.total()? z8kExceptionName(z.lastException().kind) : "");
            return true; }
        // P8: Ausnahmeprotokoll, Halt bei Traps, Buszyklen je Statuscode
        if (cmd=="trap"){
            if (t.size()>1 && t[1]=="clear"){ exc16.clear(); fprintf(stderr,"  Ausnahmeprotokoll geleert\n"); return true; }
            long n = t.size()>1? parseNum(t[1]) : 16;
            const auto& e = exc16.entries();
            fprintf(stderr,"  U8001-Ausnahmen: %s (gesamt %llu)\n", exc16.countsText().c_str(), (unsigned long long)exc16.total());
            size_t from = e.size() > size_t(n)? e.size()-size_t(n) : 0;
            for (size_t i=from;i<e.size();++i)
                fprintf(stderr,"  #%-3zu cyc=%-10llu %s\n", i, (unsigned long long)e[i].cycle,
                        dbg16::exceptionText(e[i], z.isZ8001()).c_str());
            return true; }
        if (cmd=="btrap"){ brk16_trap = (t.size()>1)? (t[1]!="off") : !brk16_trap;
            fprintf(stderr,"  break-on-Trap (EPA/PRIV/SC/SEGT) U8001 %s\n", brk16_trap?"ON":"off"); return true; }
        if (cmd=="status"){
            if (t.size()>1 && t[1]=="clear"){ z.clearStatusCounts(); fprintf(stderr,"  Statuszaehler geleert\n"); return true; }
            for (auto& l : dbg16::statusCountLines(z, t.size()>1 && t[1]=="all")) fprintf(stderr,"  %s\n", l.c_str());
            fprintf(stderr,"  letzter Zyklus: %s\n", dbg16::cycleText(z.lastCycle(), z.lastCycleData()).c_str());
            return true; }
        if (cmd=="s"){ pushHistory(); step16(t.size()>1?parseNum(t[1]):1); return true; }
        if (cmd=="n"){ if (parked16()) return true;
            pushHistory(); long k=t.size()>1?parseNum(t[1]):1;
            for (long i=0;i<k;++i){
                z8k::Line l = z8k::disasm([&](uint16_t o){ return rdw16(z.pcSeg,o,true); }, z.pc, z.segMode(), z.pcSeg);
                if (l.dec.insn && l.dec.insn->has(z8k::Z8K_CALL|z8k::Z8K_REPEAT)){
                    gu16=(long)key16(z.pcSeg,uint16_t(z.pc+l.bytes)); goSilent(0);
                    if (hit && stop_reason=="run-until U8001"){ hit=false; } else if (hit) break;
                } else { runStep16(1);
                    if (hit && stop_reason=="step U8001") hit=false; else break; }
            }
            if(hit){ hit=false; onStop(); } else { showInsn16("=>",pcKey16(z)); stateLine(); }
            return true; }
        if (cmd=="fin"){ pushHistory(); fin16_sp=sp16(z); fin16_active=true; go(0); fin16_active=false; return true; }
        if (cmd=="gu" && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            pushHistory(); gu16=(long)a.key(); go(0); gu16=-1; return true; }
        if ((cmd=="b"||cmd=="tb") && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            if (a.kind!=dbg16::Addr16::Seg){ fprintf(stderr,"  (Haltepunkte brauchen <<seg>>off, keine em:-Zelle)\n"); return true; }
            Bp bp; bp.temp=(cmd=="tb");
            // "b A if <expr…>" — wie ZVE1, Ausdruck in der U8001-Sicht (Register, Flags, [x]w …)
            if (t.size()>3 && t[2]=="if"){ for(size_t i=3;i<t.size();++i){ if(i>3)bp.cond+=" "; bp.cond+=t[i]; }
                bool ok; eval16(bp.cond,ok);
                if (!ok) fprintf(stderr,"  (Hinweis: '%s' ist jetzt nicht auswertbar — haelt dann immer)\n",bp.cond.c_str()); }
            else if (t.size()>2) fprintf(stderr,"  ? b <A> [if <Ausdruck>]\n");
            bp16[a.key()]=bp;
            std::string sy=symAt16(a.key());
            fprintf(stderr,"  %sbp U8001 @%s%s%s%s%s\n",bp.temp?"temp ":"",dbg16::addrText(a.seg,a.off,true).c_str(),
                    sy.empty()?"":" <",sy.c_str(),sy.empty()?"":">", bp.cond.empty()?"":(" if "+bp.cond).c_str()); return true; }
        if (cmd=="bd" && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true; bp16.erase(a.key()); return true; }
        if ((cmd=="be"||cmd=="bdis"||cmd=="bi") && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            auto it=bp16.find(a.key());
            if (it==bp16.end()){ fprintf(stderr,"  no bp @%s\n",dbg16::addrText(a.seg,a.off,true).c_str()); return true; }
            if (cmd=="bi"){ it->second.ignore = t.size()>2? parseNum(t[2]) : 1;
                fprintf(stderr,"  bp U8001 @%s: ignore next %ld hit(s)\n",dbg16::addrText(a.seg,a.off,true).c_str(),it->second.ignore); }
            else { it->second.enabled=(cmd=="be");
                fprintf(stderr,"  bp U8001 @%s %s\n",dbg16::addrText(a.seg,a.off,true).c_str(),it->second.enabled?"enabled":"disabled"); }
            return true; }
        // Ereignis-Halte wie ZVE1 bint/bnmi/breti — im U8001-Kontext auf den U8001:
        // bint = VI oder NVI angenommen (vor dem ersten ISR-Befehl), bnmi = NMI, breti = vor IRET.
        if (cmd=="bint"||cmd=="bnmi"||cmd=="breti"){
            bool& f = cmd=="bint"? brk16_int : cmd=="bnmi"? brk16_nmi : brk16_iret;
            f = (t.size()>1)? (t[1]!="off") : !f;
            fprintf(stderr,"  break-on-%s U8001 %s\n", cmd=="bint"?"VI/NVI":cmd=="bnmi"?"NMI":"IRET", f?"ON":"off");
            return true; }
        if ((cmd=="logpoint"||cmd=="lp") && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            std::vector<std::string> ex(t.begin()+2,t.end());
            logpoints16[a.key()]=ex;
            std::string sx; for(auto& e: ex) sx+=" "+e;
            fprintf(stderr,"  logpoint U8001 @%s%s%s\n",dbg16::addrText(a.seg,a.off,true).c_str(),ex.empty()?"":" exprs:",sx.c_str());
            return true; }
        if (cmd=="lpd" && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            logpoints16.erase(a.key()); fprintf(stderr,"  logpoint U8001 deleted\n"); return true; }
        if (cmd=="mark" && t.size()>1){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            mark16=(long)a.key(); fprintf(stderr,"  mark armed at U8001 PC=%s\n",dbg16::addrText(a.seg,a.off,true).c_str()); return true; }
        if (cmd=="hist" && t.size()>1){
            uint64_t cyc=(uint64_t)parseNum(t[1]);
            hist16_lo=hist16_hi=-1;
            if (t.size()>3){ dbg16::Addr16 lo,hi; if(!parse16(t[2],lo)||!parse16(t[3],hi)) return true;
                hist16_lo=(long)lo.key(); hist16_hi=(long)key16(lo.seg,hi.off); }
            pushHistory(); runHist(cyc,-1,-1); hist16_lo=hist16_hi=-1; return true; }
        // Watchpoints der 16-Bit-Seite: <<seg>>off (logisch, nur Zugriffe des U8001 — so,
        // wie er die Adresse ausgibt: SN + Offset) oder em:ZELLE (roh: jeder Zugriff auf die
        // DRAM-Zelle, auch der des U880 über das Fenster).  Formen wie ZVE1.
        if ((cmd=="wp"||cmd=="wpr"||cmd=="wb"||cmd=="wpa"||cmd=="wbr"||cmd=="wba") && t.size()>1){
            dbg16::Addr16 lo,hi;
            if (!dbg16::parseRange16(t[1], z.pcSeg, lo, hi, symVal16)){
                fprintf(stderr,"  ? Bereich '%s' (<<seg>>A..B im selben Segment | em:X..Y)\n",t[1].c_str()); return true; }
            Watch16 w; w.raw = lo.kind==dbg16::Addr16::Raw;
            w.w.lo = w.raw? lo.cell : lo.key(); w.w.hi = w.raw? hi.cell : hi.key();
            w.w.rd = (cmd=="wpr"||cmd=="wbr"||cmd=="wpa"||cmd=="wba");
            w.w.wr = (cmd=="wp"||cmd=="wb"||cmd=="wpa"||cmd=="wba");
            w.w.brk = (cmd=="wb"||cmd=="wbr"||cmd=="wba");
            char cond[24]={0};
            if (t.size()>=4 && (t[2]=="=="||t[2]=="!=")){
                w.w.cond = t[2]=="=="? memwatch::MemWatch32::EQ : memwatch::MemWatch32::NE;
                w.w.val=(uint8_t)strtol(t[3].c_str(),nullptr,16);
                snprintf(cond,sizeof cond," %s %02X",t[2].c_str(),w.w.val); }
            else if (t.size()>=3 && t[2]=="changed"){ w.w.cond=memwatch::MemWatch32::CHG;
                for (uint32_t a=w.w.lo; a<=w.w.hi; ++a)
                    w.w.last[a] = w.raw? rdRaw16(a) : rdb16(uint8_t(a>>16),uint16_t(a),false);
                snprintf(cond,sizeof cond," changed"); }
            mwatch16.push_back(std::move(w));
            const Watch16& b = mwatch16.back();
            auto wtxt=[&](uint32_t k){ char x[24];
                if (b.raw){ snprintf(x,sizeof x,"em:%05X",(unsigned)k); return std::string(x); }
                return dbg16::addrText(uint8_t(k>>16),uint16_t(k),true); };
            fprintf(stderr,"  [16:%zu] %s-%s [%s..%s]%s%s\n", mwatch16.size()-1, b.w.brk?"break":"watch",
                    (b.w.rd&&b.w.wr)?"rw":b.w.rd?"read":"write", wtxt(b.w.lo).c_str(), wtxt(b.w.hi).c_str(),
                    cond, b.raw? "  (roh: U8001 und U880)" : "  (logisch: U8001)");
            return true; }
        if (cmd=="wd" && t.size()>1 && t[1]!="all"){
            dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            const bool raw = a.kind==dbg16::Addr16::Raw; const uint32_t k = raw? a.cell : a.key();
            size_t before=mwatch16.size();
            mwatch16.erase(std::remove_if(mwatch16.begin(),mwatch16.end(),[&](const Watch16& w){
                return w.raw==raw && k>=w.w.lo && k<=w.w.hi; }), mwatch16.end());
            fprintf(stderr,"  removed %zu U8001-watch(es) covering %s\n",before-mwatch16.size(),a16Text(a,0).c_str());
            return true; }
        if ((cmd=="iow"||cmd=="iob"||cmd=="iod") && t.size()>1){
            long p; if(!dbg16::parseNumber(t[1],p)){ p=parseNum(t[1]); }
            const uint16_t port=(uint16_t)p;
            if (cmd=="iod"){ io16_w.erase(port); io16_b.erase(port); return true; }
            (cmd=="iow"?io16_w:io16_b).insert(port);
            fprintf(stderr,"  %s U8001-io %%%04X\n",cmd=="iow"?"watch":"break",port); return true; }
        if (cmd=="bt"){
            if (t.size()>1 && t[1]=="scan"){ bt16Scan(t.size()>2?(int)parseNum(t[2]):8); return true; }
            bt16(t.size()>1? (int)parseNum(t[1]) : 8); return true; }
        if (cmd=="u"){
            dbg16::Addr16 a; a.seg=z.pcSeg; a.off=z.pc;
            if (t.size()>1){ if(!addrOrExpr16(t[1],a)) return true; }
            else if (last_u16>=0){ a.seg=uint8_t(last_u16>>16); a.off=uint16_t(last_u16); }
            if (a.kind==dbg16::Addr16::Raw){
                // roh: Zelle → <<SG>>off (Zelle = SG·64K + Offset)
                a.kind=dbg16::Addr16::Seg; a.seg=uint8_t((a.cell>>16)&3); a.off=uint16_t(a.cell);
                fprintf(stderr,"  (em:-Adresse als Segment %u gelesen)\n",a.seg); }
            int cnt = t.size()>2? (int)parseNum(t[2]) : 12;
            uint16_t o=a.off; int blank=0;
            for (int i=0;i<cnt;++i){ char l[200];
                // Unbeschriebener Speicher (lauter FFFF/0000) ist kein Code — wie ZVE1 abbrechen.
                uint16_t w0=rdw16(a.seg,o,true);
                if (w0==0xFFFF || w0==0x0000){ if(++blank>4){
                    fprintf(stderr,"  … ab %s unbeschriebener Speicher (%%%04X) — Ausgabe abgebrochen\n",
                            dbg16::addrText(a.seg,uint16_t(o-8),true).c_str(),w0); break; } }
                else blank=0;
                int len=disasm16(a.seg,o,l,sizeof l);
                fprintf(stderr,"  %s\n",l); o=uint16_t(o+len); }
            last_u16=(long)key16(a.seg,o); return true; }
        if (cmd=="a" && t.size()>2){
            // a <adr>|. <Befehl …>  — ein Befehl, z8kasm-Syntax, über die Segmentweiche
            // in den PROGRAMMspeicher (Status 1100; in Mode 1 zählt das).
            dbg16::Addr16 a;
            if (t[1]=="."){ if(last_a16<0){ fprintf(stderr,"  (noch keine a-Adresse)\n"); return true; }
                a.seg=uint8_t(last_a16>>16); a.off=uint16_t(last_a16); }
            else if(!parse16(t[1],a)) return true;
            if (a.kind!=dbg16::Addr16::Seg){ fprintf(stderr,"  (a braucht eine Segmentadresse, kein em:)\n"); return true; }
            std::string text; for (size_t i=2;i<t.size();++i){ if(i>2) text+=" "; text+=t[i]; }
            std::vector<uint16_t> words; std::string err;
            if (!z8k::assembleLine(text, z.segMode(), a.seg, a.off, words, err)){
                fprintf(stderr,"  ? %s\n",err.c_str()); return true; }
            for (size_t i=0;i<words.size();++i){
                wrb16(a.seg,uint16_t(a.off+2*i),true,uint8_t(words[i]>>8));
                wrb16(a.seg,uint16_t(a.off+2*i+1),true,uint8_t(words[i])); }
            char l[200]; disasm16(a.seg,a.off,l,sizeof l); fprintf(stderr,"  %s\n",l);
            last_a16=(long)key16(a.seg,uint16_t(a.off+2*words.size())); return true; }
        if ((cmd=="d"||cmd=="dump") && t.size()>1){
            dbg16::Addr16 a; if(!addrOrExpr16(t[1],a)) return true;
            if (t.size()>3){                          // dump <A> <N> <datei>
                int n=(int)parseNum(t[2]); std::ofstream f(t[3],std::ios::binary);
                if(!f){ fprintf(stderr,"  cannot write %s\n",t[3].c_str()); return true; }
                for(int i=0;i<n;++i) f.put((char)rdA16(a,(uint32_t)i));
                fprintf(stderr,"  dumped %d byte(s) from %s → %s\n",n,a16Text(a,0).c_str(),t[3].c_str()); return true; }
            int len = t.size()>2? (int)parseNum(t[2]) : 64;
            for (int o=0;o<len;o+=16){ char asc[17]={0};
                fprintf(stderr,"  %-14s ",a16Text(a,(uint32_t)o).c_str());
                for (int i=0;i<16;++i){ if(o+i<len){ uint8_t b=rdA16(a,(uint32_t)(o+i));
                    fprintf(stderr,"%02X ",b); asc[i]=(b>=0x20&&b<0x7F)?(char)b:'.'; }
                    else { fprintf(stderr,"   "); asc[i]=' '; } }
                fprintf(stderr," |%s|\n",asc); }
            return true; }
        if (cmd=="e" && t.size()>2){ dbg16::Addr16 a; if(!parse16(t[1],a)) return true;
            for (size_t i=2;i<t.size();++i) wrA16(a,(uint32_t)(i-2),(uint8_t)strtol(t[i].c_str(),nullptr,16));
            fprintf(stderr,"  poked %zu byte(s) @%s\n",t.size()-2,a16Text(a,0).c_str()); return true; }
        // load/save/verify wie ZVE1, Adresse der 16-Bit-Seite (Segmentweiche) oder em:.
        if (cmd=="load" && t.size()>2){ dbg16::Addr16 a; if(!parse16(t[2],a)) return true;
            std::ifstream f(t[1],std::ios::binary);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); return true; }
            uint32_t n=0; char b; while(f.get(b)) wrA16(a,n++,(uint8_t)b);
            fprintf(stderr,"  loaded %u byte(s) @%s\n",n,a16Text(a,0).c_str()); return true; }
        if (cmd=="save" && t.size()>3){ dbg16::Addr16 a; if(!parse16(t[2],a)) return true;
            std::ofstream f(t[1],std::ios::binary); int n=(int)parseNum(t[3]);
            if(!f){ fprintf(stderr,"  cannot write %s\n",t[1].c_str()); return true; }
            for(int i=0;i<n;++i) f.put((char)rdA16(a,(uint32_t)i));
            fprintf(stderr,"  saved %d byte(s) from %s to %s\n",n,a16Text(a,0).c_str(),t[1].c_str()); return true; }
        if (cmd=="verify"){
            if (t.size()<3){ fprintf(stderr,"  verify <datei> @<A> [laenge]\n"); return true; }
            std::string as=t[2]; if(!as.empty()&&as[0]=='@') as=as.substr(1);
            dbg16::Addr16 a; if(!parse16(as,a)) return true;
            std::ifstream f(t[1],std::ios::binary);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); return true; }
            std::vector<uint8_t> fb((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t n=fb.size(); if (t.size()>3){ size_t lim=(size_t)parseNum(t[3]); if(lim<n) n=lim; }
            size_t same=0, shown=0; std::vector<size_t> diffs;
            for(size_t i=0;i<n;++i){ if (rdA16(a,(uint32_t)i)==fb[i]) ++same; else diffs.push_back(i); }
            fprintf(stderr,"  %zu Bytes @%s, %zu identisch (%.1f %%), %zu Abweichung(en)%s\n",
                    n,a16Text(a,0).c_str(),same, n? 100.0*(double)same/(double)n : 0.0, diffs.size(), diffs.empty()?"":":");
            for(size_t d : diffs){
                if (++shown>32){ fprintf(stderr,"    … (%zu weitere)\n",diffs.size()-32); break; }
                fprintf(stderr,"    %-14s Datei %02X   Speicher %02X\n",a16Text(a,(uint32_t)d).c_str(),fb[d],rdA16(a,(uint32_t)d)); }
            return true; }
        if (cmd=="x" || cmd.rfind("x/",0)==0){
            // x/<N><fmt><size> <A>  — fmt x d u c t o a i s, size b w l (Worte big-endian:
            // gerade Adresse = oberes Byte); ohne <A> weiter hinter der letzten Ausgabe.
            std::string spec = cmd.find('/')!=std::string::npos? cmd.substr(cmd.find('/')+1) : (t.size()>2? t[2] : "");
            int cnt=0; char fmt='x'; int size=2;
            for (char c: spec){ if(c>='0'&&c<='9') cnt=cnt*10+(c-'0'); else if(c=='b') size=1; else if(c=='w'||c=='h') size=2;
                                else if(c=='l') size=4; else fmt=c; }
            if (cnt<1) cnt = 8;
            if (fmt=='c') size=1;
            if (fmt=='a') size = z.segMode()? 4 : 2;
            dbg16::Addr16 a; a.seg=z.pcSeg; a.off=z.pc;
            if (t.size()>1){ if (!addrOrExpr16(t[1],a)) return true; }
            else if (last_x16>=0){ a.seg=uint8_t(last_x16>>16); a.off=uint16_t(last_x16); }
            if (fmt=='i'){ uint16_t o=a.off; for(int k=0;k<cnt;++k){ char l[200]; o=uint16_t(o+disasm16(a.seg,o,l,sizeof l)); fprintf(stderr,"  %s\n",l);}
                if (a.kind==dbg16::Addr16::Seg) last_x16=(long)key16(a.seg,o); return true; }
            uint32_t o=0;
            if (fmt=='s'){ for(int k=0;k<cnt;++k){ fprintf(stderr,"  %s: \"",a16Text(a,o).c_str()); int n=0; uint8_t b;
                    while((b=rdA16(a,o))!=0 && n<255){ fputc((b>=0x20&&b<0x7F)?(char)b:'.',stderr); ++o; ++n; }
                    ++o; fprintf(stderr,"\"\n"); }
                if (a.kind==dbg16::Addr16::Seg) last_x16=(long)key16(a.seg,uint16_t(a.off+o)); return true; }
            auto pv=[&](unsigned long long u)->std::string{ char b[64];
                switch(fmt){
                    case 'd':{ long long sv = size==1? (int8_t)u : size==2? (int16_t)u : (int32_t)u; snprintf(b,sizeof b,"%lld",sv); break; }
                    case 'u': snprintf(b,sizeof b,"%llu",u); break;
                    case 'c': snprintf(b,sizeof b,"%02llX %s",u,(u>=0x20&&u<0x7F)?(std::string("'")+(char)u+"'").c_str():"  "); break;
                    case 't':{ std::string s; for(int i=size*8-1;i>=0;--i) s+=((u>>i)&1)?'1':'0'; snprintf(b,sizeof b,"%s",s.c_str()); break; }
                    case 'o': snprintf(b,sizeof b,"0%llo",u); break;
                    case 'a':{ uint8_t sg; uint16_t of; dbg16::decodeAddr((long long)u, z.pcSeg, sg, of);
                        std::string s=symNear16(key16(sg,of));
                        snprintf(b,sizeof b,"%s%s%s%s",dbg16::addrText(sg,of,z.segMode()).c_str(),s.empty()?"":" <",s.c_str(),s.empty()?"":">"); break; }
                    default: snprintf(b,sizeof b,"%0*llX",size*2,u); break;
                } return std::string(b); };
            const int per = fmt=='a'? 4 : 8;
            for (int k=0;k<cnt;){ fprintf(stderr,"  %-14s:",a16Text(a,o).c_str());
                for (int col=0; col<per && k<cnt; ++col,++k){
                    unsigned long long v=0; for (int i=0;i<size;++i) v=(v<<8)|rdA16(a,o+uint32_t(i));
                    o+=uint32_t(size); fprintf(stderr," %s",pv(v).c_str()); }
                fprintf(stderr,"\n"); }
            if (a.kind==dbg16::Addr16::Seg) last_x16=(long)key16(a.seg,uint16_t(a.off+o));
            return true; }
        if (cmd=="set" && t.size()>=3){
            std::string rn=t[1]; for(auto&c:rn) c=(char)toupper((unsigned char)c);
            long v=parseNum(t[2]); { long pv; if (dbg16::parseNumber(t[2],pv)) v=pv; }
            unsigned n=0; bool ok=true;
            if (rn=="PC") z.pc=(uint16_t)v;
            else if (rn=="PCSEG") z.pcSeg=(uint8_t)(v&0x7F);
            else if (rn=="FCW") z.fcw=(uint16_t)v;
            else if (rn.rfind("RR",0)==0 && sscanf(rn.c_str()+2,"%u",&n)==1 && n<16) z.setRR(n,(uint32_t)v);
            else if (rn.rfind("RH",0)==0 && sscanf(rn.c_str()+2,"%u",&n)==1 && n<8) z.setRB(n,(uint8_t)v);
            else if (rn.rfind("RL",0)==0 && sscanf(rn.c_str()+2,"%u",&n)==1 && n<8) z.setRB(8+n,(uint8_t)v);
            else if (rn[0]=='R' && sscanf(rn.c_str()+1,"%u",&n)==1 && n<16) z.setR(n,(uint16_t)v);
            else ok=false;
            if (ok) fprintf(stderr,"  U8001 %s := %%%lX\n",rn.c_str(),v); else fprintf(stderr,"  ? bad register (R0..R15 RR RH RL PC PCSEG FCW)\n");
            return true; }
        return false;   // alles Übrige (g, where, hist, snap, rs, …) wie gewohnt
    };

    // ─── P8000 (AP P12a/P12b): Kommandos zu ADP, UA858, Kopplung, MMU, Terminal ───────────────
    // adp · dma · kopp · mmu · xlat · ml · mp · segt · bsegt · term · wdc.  Am P8000 ohne 16-Bit-Karte
    // gibt es nur adp/dma/kopp/term; an anderen Maschinen melden sie „nur am P8000“.
    auto p8Kommando = [&](const std::vector<std::string>& t)->bool{
        const std::string& cmd=t[0];
        static const std::set<std::string> namen={"adp","dma","kopp","mmu","xlat","ml","mp","segt","bsegt","term","wdc"};
        if (!namen.count(cmd)) return false;
        if (!KQ){ fprintf(stderr,"  '%s' gibt es nur am P8000 (--machine p8000|p8000-16)\n",cmd.c_str()); return true; }
        P8000Machine& pm=*m.p8000();
        auto need16=[&]()->bool{
            if (!p16){ fprintf(stderr,"  '%s' braucht die 16-Bit-Karte (--machine p8000-16)\n",cmd.c_str()); return false; }
            return true; };
        auto zeilen=[&](const std::vector<std::string>& v){ for (auto& l: v) fprintf(stderr,"  %s\n",l.c_str()); };
        auto protZeilen=[&](dbgp8::Protokoll& pr, const std::vector<std::string>& a, size_t ab, const char* leer){
            if (a.size()>ab && a[ab]=="clear"){ pr.clear(); fprintf(stderr,"  Protokoll geleert\n"); return; }
            bool nurW=false; if (a.size()>ab && a[ab]=="w"){ nurW=true; ++ab; }   // `kopp log w [n]`: ohne Lesezugriffe
            const size_t n = a.size()>ab? (size_t)parseNum(a[ab]) : 32;
            if (pr.eintraege().empty()){ fprintf(stderr,"  %s\n",leer); return; }
            const auto z = pr.zeilen(n,nurW);
            fprintf(stderr,"  %zu Zeilen (%llu Ereignisse insgesamt%s):\n",z.size(),(unsigned long long)pr.gesamt(),nurW?", nur Schreiben":"");
            zeilen(z); };
        if (cmd=="adp"){ zeilen(dbgp8::adpZeilen(pm.karte8().speicher())); return true; }
        if (cmd=="wdc"){
            if (!wdcK){ fprintf(stderr,"  kein WDC (--machine p8000-16 --wdc 4.2 bzw. --hd <abbild>)\n"); return true; }
            P8000Wdc& w=*wdcK; Z80& z=w.cpu();
            const std::string sub = t.size()>1? t[1] : "";
            auto rdw=[&](uint16_t a)->uint8_t{ return w.lesen(a); };
            auto regs=[&](){
                char fl[12]; flagsStr(z.AF,fl);
                fprintf(stderr,"  WDC-Z80 PC=%04X SP=%04X AF=%04X[%s] BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X I=%02X IM%d IFF=%d%s%s  Takte=%llu\n",
                        z.PC,z.SP,z.AF,fl,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,(int)z.IM,(int)z.IFF1,z.halted?" HALT":"",
                        w.imReset()?" RESET":"",(unsigned long long)w.takte()); };
            auto dis=[&](uint16_t a, int n){
                for (int i=0;i<n;++i){ z80dis::Insn d=z80dis::decode(rdw,a);
                    char hex[16]={0}; for(int k=0;k<d.len && k<5;++k){ char b[4]; snprintf(b,4,"%02X ",rdw(uint16_t(a+k))); strcat(hex,b);}
                    fprintf(stderr,"  %s%04X: %-14s %s\n", a==z.PC?"=>":"  ", a, hex, d.text); a=uint16_t(a+d.len); } };
            if (sub=="r"){ regs(); dis(z.PC,1); return true; }
            if (sub=="u"){ dis(t.size()>2? (uint16_t)parseNum(t[2]) : z.PC, t.size()>3? (int)parseNum(t[3]) : 12); return true; }
            if (sub=="d"){
                uint16_t a = t.size()>2? (uint16_t)parseNum(t[2]) : 0x30B7; int n = t.size()>3? (int)parseNum(t[3]) : 64;
                for (int o=0;o<n;o+=16){ fprintf(stderr,"  %04X:",uint16_t(a+o));
                    for (int k=0;k<16 && o+k<n;++k) fprintf(stderr," %02X",rdw(uint16_t(a+o+k))); fprintf(stderr,"\n"); }
                return true; }
            static const char* stName[8]={"besetzt","bereit fuer Kommando","bereit fuer Daten (Host->WDC)","sendet Daten (WDC->Host)","?4","?5","EPROM-Pruefsummenfehler","Fehler (1 Byte folgt)"};
            if (sub=="log"){
                if (t.size()>2 && t[2]=="clear"){ wdc_prot.clear(); fprintf(stderr,"  Protokoll geleert\n"); return true; }
                const size_t n = t.size()>2? (size_t)parseNum(t[2]) : 32;
                if (wdc_prot.empty()){ fprintf(stderr,"  (noch kein Statuswechsel des WDC)\n"); return true; }
                fprintf(stderr,"  %llu Statuswechsel insgesamt, die letzten %zu:\n",(unsigned long long)wdc_prot_n, std::min(n,wdc_prot.size()));
                for (size_t i = wdc_prot.size()>n? wdc_prot.size()-n : 0; i<wdc_prot.size(); ++i){
                    const auto& e=wdc_prot[i];
                    fprintf(stderr,"  t8=%-12llu ST %d->%d %s%s",(unsigned long long)e.t8,e.alt&7,e.neu&7,stName[e.neu&7],(e.neu&0x20)?" TR":"");
                    if ((e.alt&7)==1 && (e.neu&7)!=1){   // Kommandoblock übernommen (FW 30B7–30BF)
                        fprintf(stderr,"  Kmd %02X LW %02X:",e.kmd[0],e.kmd[1]);
                        for (int k=2;k<9;++k) fprintf(stderr," %02X",e.kmd[size_t(k)]); }
                    if ((e.neu&7)==7) fprintf(stderr,"  Fehler %02X",e.fehler);
                    fprintf(stderr,"\n"); }
                return true; }
            if (!sub.empty()){ fprintf(stderr,"  wdc [r | u [adr] [n] | d [adr] [n] | log [n|clear]]\n"); return true; }
            const uint8_t c=w.cntst(), d1=w.dskc1(), d2=w.dskc2();
            regs(); dis(z.PC,1);
            fprintf(stderr,"  Host:  ST=%d (%s)  HEN=%d HA12=%d HR/W=%d(TR)  Uebertragung %s  Host-Zaehler %03X\n",
                    c&7, stName[c&7], (c>>3)&1, (c>>4)&1, (c>>5)&1, w.uebertragungAktiv()?"aktiv":"aus", w.hostZaehler());
            fprintf(stderr,"  Disk:  DSKEA=%02X  DSKC1=%02X (Kopf %d, Richtung %s)  DSKC2=%02X (DEN=%d /MEN=%d CRCEN=%d WG=%d LW=%d FR=%d)  DA12=%d DR/W=%d  Disk-Zaehler %03X\n",
                    w.dskea(), d1, d1>>4, (d1&8)?"innen":"aussen", d2, d2&1,(d2>>1)&1,(d2>>2)&1,(d2>>3)&1, w.gewaehltesLaufwerk(), (d2>>7)&1,
                    (c>>6)&1, (c>>7)&1, w.diskZaehler());
            fprintf(stderr,"  Platte: Byte %d von %d seit Index, Zugriff %s\n", w.plattenPosition(),
                    k1520::winchester::Platte::BYTES_JE_SPUR, w.zugriffAktiv()?"aktiv":"ruht");
            for (int u=0;u<P8000Wdc::LAUFWERKE;++u){ auto* pl=w.platte(u);
                if (!pl){ fprintf(stderr,"  LW %d: -\n",u); continue; }
                const auto& g=pl->geometrie();
                fprintf(stderr,"  LW %d: %s  %u/%u/%u  Zylinder %d%s%s\n",u,pl->pfad().c_str(),unsigned(g.zylinder),unsigned(g.koepfe),
                        unsigned(g.sektoren),pl->zylinder(),pl->schmutzig()?"  (ungeschrieben)":"",pl->parErgaenzt()?"  (PAR ergaenzt)":""); }
            fprintf(stderr,"  Letzter Kommandoblock (30B7):");
            for (int k=0;k<9;++k) fprintf(stderr," %02X",rdw(uint16_t(0x30B7+k)));
            fprintf(stderr,"   Fehlerbyte (30C7) %02X   -> 'wdc log' fuer das Protokoll\n",rdw(0x30C7));
            return true; }
        if (cmd=="term"){
            auto& tm=pm.konsole();
            fprintf(stderr,"  Terminal tty1 (%s): Cursor Zeile %d Spalte %d\n",
                    pm.hatOriginalTerminal()?"Originalterminal Typ 2, P8T 5.0":"Kern-Terminal",tm.zeile(),tm.spalte());
            int last=-1; for (int r=0;r<k1520::p8000::Terminal::ZEILEN;++r){
                std::string z=pm.terminalZeile(r); while(!z.empty() && (z.back()==' '||z.back()==0)) z.pop_back();
                if (!z.empty()) last=r; }
            for (int r=0;r<=last;++r){ std::string z=pm.terminalZeile(r);
                for (char& c: z) if (c<0x20||c>=0x7F) c=' ';
                while(!z.empty() && z.back()==' ') z.pop_back();
                fprintf(stderr,"  %2d |%s\n",r,z.c_str()); }
            if (last<0) fprintf(stderr,"  (leer)\n");
            return true; }
        if (cmd=="dma"){
            Z80Dma& d=pm.floppy8().dma();
            if (t.size()>1 && t[1]=="log"){
                if (t.size()>2 && t[2]=="on"){ dmaProt.an=true; fprintf(stderr,"  dma log ON (Status-Aenderungen je U880-Befehl + Portzugriffe 3CH-3FH)\n"); }
                else if (t.size()>2 && t[2]=="off"){ dmaProt.an=false; fprintf(stderr,"  dma log off\n"); }
                else protZeilen(dmaProt.prot,t,2,"(dma log leer — erst `dma log on`, dann laufen)");
                return true; }
            if (t.size()>1 && t[1]=="rr"){
                const auto sc=d.sicht();
                fprintf(stderr,"  RR0=%02X [%s]  Zaehler(RR1/2)=%04X  A=%04X  B=%04X  Lesemaske=%02X naechstes=%d\n",
                        sc.status,dbgp8::dmaRr0Text(sc.status).c_str(),sc.zaehlerGelesen,sc.adresseA,sc.adresseB,sc.lesemaske,sc.lesePos);
                return true; }
            zeilen(dbgp8::dmaZeilen(d.sicht()));
            fprintf(stderr,"  (dma log on|off|clear|<n> | dma rr)\n");
            return true; }
        if (cmd=="kopp"){
            if (t.size()>1 && (t[1]=="log"||t[1]=="clear")){
                protZeilen(koppProt,t,t[1]=="log"?2:1,"(kopp log leer — noch kein Zugriff auf Latches/PIO0/PIO1)"); return true; }
            P8000Karte8& k8=pm.karte8();
            if (!p16 || !pm.kopplung()){
                fprintf(stderr,"  ohne 16-Bit-Karte: Kopplungseingaenge offen (Pull-up).  DS8282 L1=%02X L2=%02X  8-PIO0:\n",k8.latch1(),k8.latch2());
                const auto p0=k8.pio0().debugState();
                fprintf(stderr,"    A mode=%u out=%02X in=%02X   B mode=%u out=%02X in=%02X\n",p0.port[0].mode,p0.port[0].out,p0.port[0].in,
                        p0.port[1].mode,p0.port[1].out,p0.port[1].in);
                return true; }
            zeilen(dbgp8::koppZeilen(*pm.kopplung(),k8.latch1(),k8.latch2(),k8.pio0().debugState(),
                                     p16->pio0().debugState(),p16->pio1().debugState(),p16->runLed()));
            fprintf(stderr,"  (kopp log [w] [n] | kopp clear)\n");
            return true; }
        if (!need16()) return true;
        auto& lg=p16->mmu();
        if (cmd=="segt"){ protZeilen(segtProt,t,1,"(keine SEGT-Flanke bisher)"); return true; }
        if (cmd=="bsegt"){ brk16_segt = (t.size()>1)? (t[1]!="off") : !brk16_segt;
            fprintf(stderr,"  break-on-SEGT (vor dem ersten Befehl der Behandlung) %s\n",brk16_segt?"ON":"off"); return true; }
        if (cmd=="mmu"){
            using M=P8000MmuLogik16::Mmu;
            int which=-2; size_t ai=1;
            if (t.size()>1){ const std::string& c=t[1];
                if (c=="code"||c=="c") { which=0; ai=2; } else if (c=="data"||c=="d") { which=1; ai=2; }
                else if (c=="stack"||c=="s") { which=2; ai=2; } else if (c=="all"||c=="a") { which=-2; ai=2; }
                else { fprintf(stderr,"  mmu [all | code|data|stack [von [bis]]]\n"); return true; } }
            if (which==-2){
                zeilen(dbgp8::mmuLogikZeilen(lg));
                for (int i=0;i<3;++i) zeilen(dbgp8::mmuRegisterZeilen(lg.mmu(M(i)),dbgp8::mmuName(M(i))));
                fprintf(stderr,"  (mmu code|data|stack [von [bis]] listet Deskriptoren; xlat/ml/mp: Uebersetzung)\n");
                return true; }
            const Z8010& z=lg.mmu(M(which));
            zeilen(dbgp8::mmuRegisterZeilen(z,dbgp8::mmuName(M(which))));
            int von=0, bis=63; const bool explizit = t.size()>ai;
            if (t.size()>ai) von=bis=(int)parseNum(t[ai]);
            if (t.size()>ai+1) bis=(int)parseNum(t[ai+1]);
            if (von<0||bis>63||von>bis){ fprintf(stderr,"  ? Deskriptoren 0..63\n"); return true; }
            zeilen(dbgp8::mmuDeskriptorZeilen(z,von,bis,explizit));
            return true; }
        // xlat/ml: <Adresse> [code|data|stack] [sys|normal] [w]
        auto zugriffsArgs=[&](size_t ab, dbgp8::Art& art, bool& system, bool& lesen){
            art=dbgp8::Art::Data; system=z16().systemMode(); lesen=true;
            for (size_t i=ab;i<t.size();++i){
                dbgp8::Art a;
                if (dbgp8::parseArt(t[i],a)) art=a;
                else if (t[i]=="sys"||t[i]=="system") system=true;
                else if (t[i]=="normal"||t[i]=="norm"||t[i]=="user") system=false;
                else if (t[i]=="w"||t[i]=="write") lesen=false; } };
        if (cmd=="xlat"){
            if (t.size()<2){ fprintf(stderr,"  xlat <<seg>>off|Adresse [code|data|stack] [sys|normal] [w]\n"); return true; }
            dbg16::Addr16 a; if (!parse16(t[1],a)) return true;
            dbgp8::Art art; bool sys,rd; zugriffsArgs(2,art,sys,rd);
            zeilen(dbgp8::xlat(lg,dbgp8::zyklusFuer(a.seg,a.off,art,sys,rd)).zeilen);
            return true; }
        if (cmd=="ml"){
            if (t.size()<2){ fprintf(stderr,"  ml <<seg>>off|Adresse [Laenge] [code|data|stack] [sys|normal]\n"); return true; }
            dbg16::Addr16 a; if (!parse16(t[1],a)) return true;
            int len=64; size_t ab=2;
            if (t.size()>2){ dbgp8::Art tmp; if (!dbgp8::parseArt(t[2],tmp) && t[2]!="sys" && t[2]!="system" && t[2]!="normal" && t[2]!="user"){
                len=(int)parseNum(t[2]); ab=3; } }
            dbgp8::Art art; bool sys,rd; zugriffsArgs(ab,art,sys,rd);
            const auto first=dbgp8::xlat(lg,dbgp8::zyklusFuer(a.seg,a.off,art,sys));
            fprintf(stderr,"  logisch <<%u>>%%%04X (%s, %s): MMU %s -> %s%s\n",a.seg,a.off,
                    art==dbgp8::Art::Code?"Befehl":art==dbgp8::Art::Data?"Daten":"Stapel", sys?"System":"Normal",
                    P8000MmuLogik16::wahlName(first.probe.zugriff.wahl),P8000MmuLogik16::zielName(first.probe.zugriff.ziel),
                    first.hauptspeicher? dbgp8::fmt(" phys=%06X",first.phys).c_str() : "");
            for (int o=0;o<len;o+=16){ char asc[17]={0};
                fprintf(stderr,"  %s ",dbg16::addrText(a.seg,uint16_t(a.off+o),true).c_str());
                for (int i=0;i<16;++i){
                    if (o+i<len){ uint8_t v; const bool ok=dbgp8::logByte(*p16,a.seg,uint16_t(a.off+o+i),art,sys,v);
                        if (ok){ fprintf(stderr,"%02X ",v); asc[i]=(v>=0x20&&v<0x7F)?(char)v:'.'; } else { fprintf(stderr,"-- "); asc[i]='?'; } }
                    else { fprintf(stderr,"   "); asc[i]=' '; } }
                fprintf(stderr," |%s|\n",asc); }
            fprintf(stderr,"  (-- = kein Baustein antwortet / unterdrueckt; `xlat` zeigt den Weg)\n");
            return true; }
        if (cmd=="mp"){
            if (t.size()<2){ fprintf(stderr,"  mp <Hauptspeicheradresse A0-A23> [Laenge]\n"); return true; }
            const uint32_t a=(uint32_t)parseNum(t[1])&0xFFFFFF; const int len=t.size()>2?(int)parseNum(t[2]):64;
            for (int o=0;o<len;o+=16){ char asc[17]={0};
                fprintf(stderr,"  P:%06X ",(a+o)&0xFFFFFF);
                for (int i=0;i<16;++i){
                    if (o+i<len){ uint8_t v; const bool ok=dbgp8::physHaupt(*p16,(a+o+i)&0xFFFFFF,v);
                        if (ok){ fprintf(stderr,"%02X ",v); asc[i]=(v>=0x20&&v<0x7F)?(char)v:'.'; } else { fprintf(stderr,"-- "); asc[i]='?'; } }
                    else { fprintf(stderr,"   "); asc[i]=' '; } }
                fprintf(stderr," |%s|\n",asc); }
            fprintf(stderr,"  (physischer Hauptspeicher der DRAM-Karten; -- = keine Karte gewaehlt; On-Board: `ml` Segment 0 < 8000)\n");
            return true; }
        return false;
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
    if (KQ) fprintf(stderr,"k1520dbg — Maschine P8000 (U880%s; 'help p8000').  Disassembler: built-in.\n",p16?" + U8001/MMU/Kopplung":"");
    else if (KC) fprintf(stderr,"k1520dbg — Maschine PC 1715 (eine CPU; 'help pc1715').  Disassembler: built-in.\n");
    else if (KP) fprintf(stderr,"k1520dbg — Maschine %s (eine CPU; 'help prg').  Disassembler: built-in.\n",m.name());
    else if (K8) fprintf(stderr,"k1520dbg — Maschine %s (eine CPU; 'help k8915').  Disassembler: built-in.\n",m.name());
    else
    fprintf(stderr,"k1520dbg — type 'help'.  Lauf-Uhr = %s (clock zve1|machine).  Disassembler: built-in.\n",
            clock_machine? "Maschine (beide CPUs)" : "ZVE1");
#ifdef HAVE_ISOCLINE
    // ic_init(true) = Ausgabe des Editors auf STDERR. Das ist keine Kosmetik: der
    // ganze Debugger schreibt auf stderr (damit `… 2>&1 | tee sitzung.log` alles
    // erwischt), und ein Prompt auf stdout würde aus der Reihe tanzen.
    ic_init(true);
    ic_set_prompt_marker("(dbg) ", nullptr);   // Prompt exakt wie im Skriptbetrieb
    ic_enable_multiline(false);                // eine Zeile = ein Kommando
    ic_enable_hint(false);                     // keine Vorschau-Einblendung
    ic_set_history(nullptr, -1);               // History nur im Speicher, 200 Einträge
    ic_set_default_completer(dbgCompleter, nullptr);
#endif
    if (start_console) consoleMode(1.0);   // --console
    std::string line;
    for (;;){
        // read one command (echo script lines so piped sessions are readable)
        if (!pending.empty()){ line=pending.front(); pending.pop_front(); fprintf(stderr,"(dbg) %s\n",line.c_str()); }
        else {
            // Terminal → isocline (Zeilenbearbeitung, History, Tab-Vervollständigung);
            // Pipe/Skript → schlichtes getline mit eigenem Prompt (unverändert).
            // isocline erkennt eine Pipe zwar selbst, aber der Skriptpfad muss
            // buchstabengetreu bleiben: ~30 cli_dbg_-Tests pinnen den Wortlaut.
#ifdef HAVE_ISOCLINE
            if (k1520::os::isTerminal(0)) {
                char* rl = ic_readline("");     // Prompt kommt aus dem prompt_marker
                if (!rl) break;                 // EOF / Ctrl-D / Ctrl-C
                line = rl; ic_free(rl);         // History führt isocline selbst
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
        // Kontext ZVE2 (`cpu zve2`): die ZVE1-Kommandos auf ihre 2-Varianten umbiegen.
        if (cpu_ctx==2){
            static const std::map<std::string,std::string> z2={{"b","b2"},{"s","s2"},{"bd","bd2"},
                {"be","be2"},{"bdis","bdis2"},{"bi","bi2"},{"rj","rj2"}};
            auto it=z2.find(t[0]);
            if (it!=z2.end()) t[0]=it->second;
            else if (t[0]=="r" && t.size()==1) t.push_back("2");
            else if (t[0]=="set" && t.size()>=3 && t[1]!="2") t.insert(t.begin()+1,"2");
        }
        // Kontext U8001 (`cpu u8000`) bzw. eine `em:`-Adresse: eigener Zweig.
        if (handle16(t)) continue;
        const std::string& cmd=t[0];

        // K8915: was es nur am A5120 gibt (ZVE2, /BUSRQ-/DMA-Ereignisse, Snapshots,
        // Savestates), wird gemeldet statt ausgeführt — nie ein Absturz (§8a AP-E4d).
        if (K8 && dbgm::nurA5120Kommando(cmd) && !(KQ && (cmd=="savestate"||cmd=="loadstate"))){
            fprintf(stderr,"  '%s' gibt es am %s nicht (nur A5120: ZVE2, /BUSRQ/DMA, "
                           "Snapshots/Savestates) — nicht vorhanden\n",cmd.c_str(),m.name());
            continue;
        }
        auto nichtAmK8915 = [&](const char* was){
            fprintf(stderr,"  %s gibt es am %s nicht — nicht vorhanden\n",was,m.name()); };

        // §0 discoverability: when ZVE2 is the current bus master, a ZVE1-only command is
        // almost always a mistake (the DMA/read runs on ZVE2). Nudge toward the 2-variants.
        if (zve2Active() && cpu_ctx==1){
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
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 && (t[1]=="prg"||t[1]=="prg710"||t[1]=="PRG")){
            fprintf(stderr,
              "  PRG 710 / 710-1 (--machine prg710|prg710-1)  eine CPU (K2521), Speicherverwaltung E8H-EBH\n"
              "    map               EBH + die 16 Seitenregister (Herkunft E8H, Seite EAH, Quelle ZRE/VRAM/OPS)\n"
              "    d/u/x/e/wp        Speicher in CPU-SICHT (VRAM nur bei E8H[F]=FFH)\n"
              "    screen/gscreen    Bild direkt von der K7024, nie ueber die CPU-Sicht\n"
              "    keys <text>       Tasten: 710 K7609 (Return = ET1, QK_TASTE_BASE|37H), 710-1 K7672 (Return = 0DH)\n"
              "    vars ; where ; dev [ctc|pio [zre]|sio|sio2] ; ivt   Speicherbild, K5122, ZRE, K8025, Interruptkette\n"
              "    dev sio           K8025 A32 (710: Tastatur-Port, 710-1: B = Tastatur K7672); sio2 = A33 (DFUE)\n"
              "    Listings: -l doc/EPROMS/PRG710/prg710_zre.prn@0x0000  (ROM, @-Versatz fuer die RAM-Kopie)\n"
              "              -l doc/prg710/resident_710.lst  (710-1: prg710-1_zre.prn, resident_710-1.lst)\n"
              "    Nicht vorhanden: s2/b2/rj2/r 2 (ZVE2), bbusrq, snap/restore/rs/rc, savestate/loadstate, bank\n");
        }
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 && (t[1]=="p8000"||t[1]=="P8000"||t[1]=="p8000-16")){
            fprintf(stderr,
              "  P8000 (--machine p8000 | p8000-16)  U880 + MON8 + Kern-Terminal tty1; p8000-16 mit U8001, MMU, Kopplung\n"
              "    cpu u880|u8000      Kontext: U880 (r s n b g …) oder U8001 (help u8000: s n b x d u e a bt …)\n"
              "    map | adp           ADP-Zellen je 4-KB-Seite (E/S/D), RFF; d/u/x/e/wp in U880-SICHT (mit ADP)\n"
              "    dma [log on|off|clear|<n>] | dma rr   UA858: WR0-WR5/RR0-RR6, Zaehler, Adressen, Modus, Statusprotokoll\n"
              "    kopp [log [w] [n] | clear]   Kopplungsleitungen K1-K15, Latches 10H/14H, PIO0/PIO1 beider Karten, Zugriffsprotokoll (w = ohne Lesen)\n"
              "    dev [ctc|sio|sio2|pio|dma|fdc|ctc16|sio16|pio16]   Bausteine beider Karten\n"
              "    term                Terminal tty1 als Text (Cursor); screen/bscreen/screen find gehen ebenfalls\n"
              "    keys <text>         ueber die Terminaltastatur; \\r = Return (Batch 10 000 Takte je Zeichen)\n"
              "    --- mit 16-Bit-Karte ---\n"
              "    mmu [all|code|data|stack [von [bis]]]   SCR/NBR/TRPL/IF1L, MR/VTR/VSN/VOFF/BCSR/ISN/IOFF, Deskriptoren (SDR 0-63)\n"
              "    xlat <<s>>off [code|data|stack] [sys|normal] [w]   probeweise Uebersetzung ohne Nebenwirkung: MMU, phys., Verletzungen\n"
              "    ml <<s>>off [n] [art] [mode]   Speicher LOGISCH (ueber Auswahllogik + UB8010) | mp <A0-A23> [n]   PHYSISCH\n"
              "    segt [n|clear] | bsegt [on|off]   SEGT-Protokoll (Zyklus, MMU, TRPL/IF1L) / Halt vor der Behandlung\n"
              "    trap | btrap | status | fcw | psa   Ausnahmen des U8001 (im Kontext u8000)\n"
              "    savestate <f> | loadstate <f>   P8KS (nicht: Medieninhalt) ; snap/rs/bbusrq: nicht vorhanden\n"
              "    wdc [r|u|d|log]     WDC (--wdc 4.2 / --hd abbild): Zustand, Register, Disassembler, Speicher, Kommando-Protokoll\n"
              "    Adressen der 16-Bit-Seite: <<seg>>off; `em:ADR` = physischer Hauptspeicher A0-A23 (roh)\n");
        }
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 && (t[1]=="pc1715"||t[1]=="PC1715"||t[1]=="1715")){
            fprintf(stderr,
              "  PC 1715 (--machine pc1715)  eine CPU (U880), ROM-Overlay S502 (24H ein / 28H aus), K5122 /WAIT\n"
              "    map               Overlay ein/aus, BWS-Register 34H (Bildbasis, ZG-Wahl), Bildformat\n"
              "    d/u/x/e/wp        Speicher in CPU-SICHT (mit Overlay: Lesen = ROM, Schreiben = RAM darunter)\n"
              "    screen/gscreen    Bild direkt vom 8275-Raster (screenChar), nie ueber die CPU-Sicht\n"
              "    keys <text>       Tasten ueber Tastatur1715 (U880 + S600), je Taste 250 000 Takte; \\r = Return, \\x01..\\x1A = Strg+Buchstabe\n"
              "    vars ; where ; dev [ctc|pio|sio|crt] ; ivt   Overlay/BWS, K5122, CTC0, SIO0, 8275, Interruptkette\n"
              "    Listings: -l doc/EPROMS/PC1715/s502.prn  (ROM-Overlay; nur solange die Bytes passen)\n"
              "    Nicht vorhanden: s2/b2/rj2/r 2 (ZVE2), bbusrq, snap/restore/rs/rc, savestate/loadstate, bank\n");
        }
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 && (t[1]=="k8915"||t[1]=="K8915")){
            fprintf(stderr,
              "  K8915 (--machine k8915 | k8915-g2)  eine CPU, Speicherbild ueber A8H, K5122 im /WAIT-Betrieb\n"
              "    map               A8H, /MEMDI, Quelle je 4-KB-Seite (ROM/Bank 1/Bank 2/Bus; Gen 2: ZRE/K3528/Bus)\n"
              "    bank <1|2> <A> [N]  Hexdump direkt aus einer DRAM-Bank (Bank 2: Viertel q = q*4000H)\n"
              "    d/u/x/e/wp        Speicher in CPU-SICHT (mit A8H-Abbildung)\n"
              "    screen/gscreen    Bild direkt von der K7024 (1000H), nie ueber die CPU-Sicht\n"
              "    keys <text>       Zeichen ueber die K7672 (SCP: Zeichen, DCP: Scancodes) — \\x03 = ^C\n"
              "    vars ; where ; dev [ctc|pio|sio|sio2] ; ivt   A8H/61H, K5122, ATS, Interruptkette\n"
              "    --skip-selftest   Start ohne ROM-Selbsttest (JP bei 0000H/0005H wie ein Warmstart; nicht Gen 2)\n"
              "    Gen 2: bank nicht vorhanden; dev ctc = ZRE K2521; Listing -l k8915g2_zre.prn@0xFC00:0021-03FF\n"
              "           -l k8915g2_zre.prn@0xFC00:0400-0BFF (Kopierer; Stub 0400H unverschoben)\n"
              "    Listings: -l k8915_zre.prn -l k8915_zre.prn@0xF000:00D0-03FF\n"
              "              -l k8915_zre.prn@0xF000:0C00-0FFF -l scpx8915_v53_bios.prn\n"
              "              (annotiert wird nur, solange die Bytes der Zeile im Speicher stehen)\n"
              "    Nicht vorhanden: s2/b2/rj2/r 2 (ZVE2), bbusrq, snap/restore/rs/rc, savestate/loadstate\n"
              "    bxfer [read|write]: Datenfluss /STR=0 bzw. /WE=0 (K5122 im /WAIT-Betrieb)\n");
        }
        else if ((cmd=="help"||cmd=="h"||cmd=="?") && t.size()>1 &&
                 (t[1]=="u8000"||t[1]=="u8001"||t[1]=="em"||t[1]=="16")){
            fprintf(stderr,
              "  A5120.16 / U8001 (Start mit --em em256|em064)\n"
              "    cpu [zve1|zve2|u8000]   CPU-Kontext; u8000 schaltet um:\n"
              "      r / rj   Register (R0..R15, FCW, PSAP, Zustand) ;  set R3|RR2|RH0|PC|PCSEG|FCW <v>\n"
              "      s [N] / n [N] / fin / gu <A>   Schritt, ueber CALL/Blockbefehl, heraus, bis A\n"
              "      u [A] [N]   Disassembler ;  a <A>|. <Befehl>   Inline-Assembler (z8kasm-Syntax)\n"
              "      b <A> [if <Ausdruck>] / tb/bd/be/bdis/bi <A>   Haltepunkte (vor dem Befehl)\n"
              "      lp <A> [expr..] / lpd <A>   Logpoint: drucken und weiterlaufen\n"
              "      bt [N] | bt scan   Aufrufstapel (mit Symbolen) ;  mark <A> ; hist <cyc> [lo hi]\n"
              "      bint | bnmi | breti [on|off]   Halt bei VI/NVI-, NMI-Annahme bzw. vor IRET\n"
              "      btrap [on|off]   Halt bei EPA-/PRIV-/SC-Trap oder Segmenttrap (vor dem 1. Befehl)\n"
              "      trap [N|clear]   Ausnahmeprotokoll (Art, Kennung, gekellert, FCW alt->neu, Ziel)\n"
              "      status [all|clear]   Buszyklen je Statuscode ST3..0 + letzter Zyklus (N/S, SN, AD)\n"
              "      iow/iob/iod <port>   E/A-Port des U8001 beobachten / anhalten / loeschen\n"
              "      disp <expr>   Ausdruck in der U8001-Sicht an jedem Halt\n"
              "      d/e/x <A>   Speicher DURCH DIE SEGMENTWEICHE (wie ein Datenzugriff jetzt)\n"
              "      x/<N><x|d|u|c|t|o|a|i|s><b|w|l> <A|Ausdruck>   ohne <A> weiter ; load/save/verify <f> <A>\n"
              "    Ausdruecke: R0..R15 RH/RL RR RQ PC PCSEG SP FCW, Flags C Z S V D H, SEG SYS VIE NVIE,\n"
              "      A33 A34 A35 A36 A53 A54 PE MODE ; [x] [x]w [x]l (Segmentweiche) [em:x] (roh) ; %%hex <<s>>off\n"
              "    Watch: wp/wpr/wpa (drucken) wb/wbr/wba (halten) <<s>>A[..B] (logisch, U8001)\n"
              "           | em:X[..Y] (roh, auch U880) [==v|!=v|changed] ; wd <A> ; wl\n"
              "    Adressen: <<seg>>off (<<0>>%%0100), Zahl = PC-Segment, em:ZELLE = roh ins DRAM,\n"
              "              Symbol[+off] (sym <datei> mit <<s>>%%off NAME, z8kasm --sym)\n"
              "              (d/e/x/u/wp../load/save/verify mit em: gehen in jedem Kontext)\n"
              "    dev em      Steuerkarte: A22, A33..A36, PIO A32, A29 8/16, TREN, BUSRQ/BUSAK,\n"
              "                A46 (Paritaet, Merker)/PR, A53/A54, Guthaben\n"
              "    fcw [v]     FCW anzeigen/setzen ;  psa [n]  Program Status Area (+ n VI-Eintraege)\n"
              "    bmode [16|8|both|off]   Halt GENAU an der Flanke des FF A29 (Moduswechsel)\n"
              "    bvi [on|off]            Halt, wenn der U8001 ein VI annimmt (vor dem 1. ISR-Befehl)\n"
              "    bint16 [on|off]         Halt GENAU an der Flanke INT-16 (A33 Bit 4 → PIO A4 → U880)\n"
              "                            (Zeile 'ausgeloest': welcher Befehl welcher CPU sie erzeugte)\n"
              "    emlog [on|off|<datei>]  jede EM-Transaktion eine Zeile (wie boot_trace --em)\n"
              "    where / hist / snap / snap diff / rs   zeigen, vergleichen bzw. sichern den U8001 mit\n");
        }
        else if (cmd=="help"||cmd=="h"||cmd=="?"){
            fprintf(stderr,
              "  RUN     g/c [N]   run to breakpoint (or N MASCHINEN-Takte; Ctrl-C bricht ab)\n"
              "          clock [zve1|machine]   welche Uhr die Lauf-Budgets zaehlt\n"
              "  RAF     raf       (mit --raf) Typ, Kapazitaet, Latch, gesperrt, Sektor aus dem Latch\n"
              "          raf <S>   Hexdump des 128-B-Sektors S in TREIBER-Lesart: Byte 0 zuerst, also\n"
              "                    rueckwaerts aus der Karte gelesen (INIR/OTIR ohne Autoinkrement)\n"
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
              "  WATCH   wp/wpr/wpa | wb/wbr/wba <A|A..B> [==v|!=v|changed]   mem watch (range+cond):\n"
              "                          print write/read/both | break write/read/both\n"
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
              "          console [tempo]  Maschine LIVE bedienen (Ctrl-] zurueck; Haltepunkte bleiben scharf)\n"
              "          gscreen \"txt\"|/re/ [maxcyc]   run until screen shows txt (deterministic menus)\n"
              "          bscreen \"txt\"|/re/ | off      arm: any g/gu/n stops on screen match\n"
              "          keyuntil \"<key>\" \"txt\" [maxcyc]  press key until screen shows txt (poll-robust)\n"
              "          dialog <file>   drive a menu: per line  \"screen-txt\" \"keys\" [maxcyc]\n"
              "          alias <name> <expansion..> | unalias <name> | alias ; source <file>\n"
              "  K8915   map | bank <1|2> <A> [N] | help k8915   (--machine k8915 | k8915-g2)\n"
              "  PRG     map | help prg   (--machine prg710|prg710-1)\n"
              "  PC1715  map | help pc1715   (--machine pc1715)\n"
              "  P8000   map|adp | dma | kopp | mmu | xlat | ml | mp | segt | bsegt | term | help p8000   (--machine p8000|p8000-16)\n"

              "  A5120.16 cpu [zve1|zve2|u8000] ; dev em ; fcw ; psa ; bmode ; bvi ; bint16 ; emlog\n"
              "          (help u8000 — der U8001 des Erweiterungsmoduls, Start mit --em em256)\n");
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
                for(auto&kv:named_snaps){ fprintf(stderr,"  %-16s PC=%04X cyc=%llu",
                        kv.first.c_str(),kv.second.zve1.PC,(unsigned long long)kv.second.zve1.cycles);
                    auto ie=named_em.find(kv.first);
                    if (ie!=named_em.end() && ie->second.valid)
                        fprintf(stderr,"  U8001 PC=%s [%s] %s-Bit",dbg16::addrText(ie->second.pcSeg,ie->second.pc,true).c_str(),
                                ie->second.state.c_str(), ie->second.mode8?"8":"16");
                    fprintf(stderr,"\n"); } }
            else if (t.size()>=4 && t[1]=="diff"){   // §14 snap diff <a> <b>
                auto ia=named_snaps.find(t[2]), ib=named_snaps.find(t[3]);
                if(ia==named_snaps.end()||ib==named_snaps.end())
                    fprintf(stderr,"  snapshot '%s' oder '%s' fehlt (snap list)\n",t[2].c_str(),t[3].c_str());
                else { snapDiff(t[2],t[3],ia->second,ib->second);
                    // S5b: U8001-Register, Steuerkarte, A22, PIO A32 und EM-DRAM mit vergleichen.
                    auto ea=named_em.find(t[2]), eb=named_em.find(t[3]);
                    if (em && ea!=named_em.end() && eb!=named_em.end()){
                        auto d=dbg16::diffEm(ea->second,eb->second);
                        for (auto& l: d) fprintf(stderr,"    %s\n",l.c_str());
                        if (d.empty()) fprintf(stderr,"  → EM (U8001, Karte, DRAM) unverändert\n"); } } }
            else if (t.size()>=2){ m.captureState(named_snaps[t[1]]);
                if (em) captureEm(named_em[t[1]]);
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
            tbl[a]=bp; fprintf(stderr,"  bp %s @%04X%s\n",cmd=="b2"?"ZVE2":C1,a, bp.cond.empty()?"":(" if "+bp.cond).c_str()); }
        else if (cmd=="tb" && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]); Bp bp; bp.temp=true; bp1[a]=bp;
            fprintf(stderr,"  temp bp %s @%04X\n",C1,a); }
        else if (cmd=="bd" && t.size()>1) bp1.erase((uint16_t)parseNum(t[1]));
        else if (cmd=="bd2"&& t.size()>1) bp2.erase((uint16_t)parseNum(t[1]));
        // enable / disable (keep but inactive) — be/bdis (ZVE1), be2/bdis2 (ZVE2)
        else if ((cmd=="be"||cmd=="bdis"||cmd=="be2"||cmd=="bdis2") && t.size()>1){
            auto& tbl = (cmd=="be2"||cmd=="bdis2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            auto it=tbl.find(a);
            if(it==tbl.end()) fprintf(stderr,"  no bp @%04X\n",a);
            else { bool en=(cmd=="be"||cmd=="be2"); it->second.enabled=en;
                fprintf(stderr,"  bp %s @%04X %s\n",(cmd=="be2"||cmd=="bdis2")?"ZVE2":C1,a,en?"enabled":"disabled"); } }
        // ignore next N hits before stopping — bi/bi2 (gdb 'ignore')
        else if ((cmd=="bi"||cmd=="bi2") && t.size()>2){
            auto& tbl = (cmd=="bi2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            auto it=tbl.find(a);
            if(it==tbl.end()) fprintf(stderr,"  no bp @%04X\n",a);
            else { it->second.ignore=parseNum(t[2]);
                fprintf(stderr,"  bp %s @%04X: ignore next %ld hit(s)\n",cmd=="bi2"?"ZVE2":C1,a,it->second.ignore); } }
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
            show(bp1,C1); if(!K8) show(bp2,"ZVE2");
            if (has16){ fprintf(stderr,"  U8001 breakpoints:\n");
                if (bp16.empty()) fprintf(stderr,"    (none)\n");
                for (auto& kv:bp16){ std::string sy=symAt16(kv.first);
                    fprintf(stderr,"    %s%s%s%s  hits=%ld%s%s%s%s\n",
                        dbg16::addrText(uint8_t(kv.first>>16),uint16_t(kv.first),true).c_str(),
                        sy.empty()?"":" <",sy.c_str(),sy.empty()?"":">", kv.second.hits,
                        kv.second.enabled?"":" [disabled]", kv.second.temp?" [temp]":"",
                        kv.second.ignore>0?(" ignore="+std::to_string(kv.second.ignore)).c_str():"",
                        kv.second.cond.empty()?"":(" if "+kv.second.cond).c_str()); } }
            if(brk_int||brk_nmi||brk_reti||brk_busrq||brk_xfer||brk_wxfer||brk_mode||brk_vi||brk_int16||brk16_int||brk16_nmi||brk16_iret)
                fprintf(stderr,"  events:%s%s%s%s%s%s%s%s%s%s%s%s\n",
                brk_int?" interrupt":"",brk_nmi?" nmi":"",brk_reti?" reti":"",
                brk_busrq?" busrq":"",brk_xfer?" read-xfer":"",brk_wxfer?" write-xfer":"",
                brk_mode==1?" mode→16":brk_mode==2?" mode→8":brk_mode==3?" mode":"",
                brk_vi?" vi":"", brk_int16?" int16":"",
                brk16_int?" u8001-vi/nvi":"", brk16_nmi?" u8001-nmi":"", brk16_iret?" u8001-iret":""); }
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
            if(logpoints.empty() && logpoints16.empty()) fprintf(stderr,"  (no logpoints)\n");
            for(auto&kv:logpoints16){ std::string sy=symAt16(kv.first);
                fprintf(stderr,"    U8001 %s%s%s%s ",dbg16::addrText(uint8_t(kv.first>>16),uint16_t(kv.first),true).c_str(),
                        sy.empty()?"":" <",sy.c_str(),sy.empty()?"":">");
                for(auto&e:kv.second) fprintf(stderr,"%s ",e.c_str()); fprintf(stderr,"\n"); }
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
            if(t.size()>1){ if(K8) nichtAmK8915("ZVE2 ('r 2')"); else { snap2=grab(m.zve2Debug()); printSnap(2); } }
            showInsn("=>",m.cpuPC()); stateLine(); }
        // machine-readable registers (one JSON line) — for scripted/agent consumption
        else if (cmd=="rj" && KQ){ const Z80& z=m.cpuDebug(); P8000Karte8& k=m.p8000()->karte8();
            fprintf(stderr,"\n{\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\",\"bc\":\"0x%04X\","
                "\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"cyc\":%llu,\"rom\":%s,"
                "\"machine\":\"p8000\",\"rff\":%s,\"map\":\"%s\"}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,z.R, z.IFF1?"true":"false",
                (unsigned long long)m.cpuCycles(), m.isRomEnabled()?"true":"false",
                k.speicher().rffGesetzt()?"true":"false", dbgp8::speicherbild(k.speicher()).c_str()); }
        else if (cmd=="rj" && KP){ const Z80& z=m.cpuDebug(); Prg710Machine& p=*m.prg();
            fprintf(stderr,"\n{\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\",\"bc\":\"0x%04X\","
                "\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"cyc\":%llu,\"rom\":%s,"
                "\"machine\":\"%s\",\"ebh\":\"0x%02X\",\"map\":\"%s\"}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,z.R, z.IFF1?"true":"false",
                (unsigned long long)m.cpuCycles(), m.isRomEnabled()?"true":"false",
                KP1?"prg710-1":"prg710", p.speicher().freigabe(), dbgm::speicherbild(p).c_str()); }
        else if (cmd=="rj" && KC){ const Z80& z=m.cpuDebug(); Pc1715Zre& pz=m.pc1715()->zre();
            fprintf(stderr,"\n{\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\",\"bc\":\"0x%04X\","
                "\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"cyc\":%llu,\"rom\":%s,"
                "\"machine\":\"pc1715\",\"bws\":\"0x%02X\",\"basis\":\"0x%04X\"}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,z.R, z.IFF1?"true":"false",
                (unsigned long long)m.cpuCycles(), m.isRomEnabled()?"true":"false",
                pz.bwsRegister(), pz.bildBasis()); }
        else if (cmd=="rj" && K89){ const Z80& z=m.cpuDebug(); K8915Machine& k8=*m.k8915();
            fprintf(stderr,"\n{\"pc\":\"0x%04X\",\"sp\":\"0x%04X\",\"af\":\"0x%04X\",\"bc\":\"0x%04X\","
                "\"de\":\"0x%04X\",\"hl\":\"0x%04X\",\"ix\":\"0x%04X\",\"iy\":\"0x%04X\","
                "\"i\":\"0x%02X\",\"r\":\"0x%02X\",\"iff1\":%s,\"cyc\":%llu,\"rom\":%s,"
                "\"machine\":\"%s\",\"a8\":\"0x%02X\",\"lamps\":\"0x%02X\"}\n",
                z.PC,z.SP,z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.I,z.R, z.IFF1?"true":"false",
                (unsigned long long)m.cpuCycles(), m.isRomEnabled()?"true":"false",
                dbgm::gen2(k8)?"k8915-g2":"k8915", dbgm::k8A8(k8), k8.ats().anzeige()); }
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
            if (cpu==2 && K8) nichtAmK8915("ZVE2 ('set 2')");
            else if (idx+1<t.size() && setReg(cpu,t[idx],parseNum(t[idx+1])))
                fprintf(stderr,"  %s %s := 0x%lX\n",cpu==2?"ZVE2":C1,t[idx].c_str(),parseNum(t[idx+1])&0xFFFF);
            else fprintf(stderr,"  ? bad register\n"); }
        else if (cmd=="disp"){ if(t.size()>1){
                std::string ex; for(size_t i=1;i<t.size();++i){ if(i>1)ex+=" "; ex+=t[i]; } // join → spaces in exprs ok
                displays.push_back(ex); displays16.push_back(cpu_ctx==3 && has16);
                fprintf(stderr,"  disp[%zu] = %s%s\n",displays.size()-1,ex.c_str(),displays16.back()?"  (U8001)":""); }
            else { for(size_t i=0;i<displays.size();++i) fprintf(stderr,"  disp[%zu] %s%s\n",i,displays[i].c_str(),
                                                                  displays16[i]?"  (U8001)":""); } }
        else if (cmd=="undisp" && t.size()>1){ size_t i=(size_t)parseNum(t[1]);
            if(i<displays.size()){ displays.erase(displays.begin()+i); displays16.erase(displays16.begin()+i); } }
        // ══ WATCH: memory watchpoints (range + value-cond) and I/O-port watches ══
        else if ((cmd=="wp"||cmd=="wpr"||cmd=="wb"||cmd=="wpa"||cmd=="wbr"||cmd=="wba") && t.size()>1){
            MemWatch w; parseRange(t[1], w.lo, w.hi);
            // wp/wpr/wpa = drucken bei Schreiben/Lesen/beidem, wb/wbr/wba = anhalten.
            w.rd = (cmd=="wpr"||cmd=="wbr"||cmd=="wpa"||cmd=="wba");
            w.wr = (cmd=="wp"||cmd=="wb"||cmd=="wpa"||cmd=="wba");
            w.brk = (cmd=="wb"||cmd=="wbr"||cmd=="wba");
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
            fprintf(stderr,"  [%zu] %s-%s [%04X..%04X]%s\n", mwatch.size()-1,
                    mwatch.back().brk?"break":"watch",
                    (mwatch.back().rd&&mwatch.back().wr)?"rw":mwatch.back().rd?"read":"write",
                    mwatch.back().lo,mwatch.back().hi,cond); }
        else if (cmd=="wd" && t.size()>1){
            if (t[1]=="all"){ mwatch.clear(); mwatch16.clear(); fprintf(stderr,"  all watchpoints cleared\n"); }
            else { uint16_t a=(uint16_t)parseNum(t[1]); size_t before=mwatch.size();
                mwatch.erase(std::remove_if(mwatch.begin(),mwatch.end(),
                    [&](const MemWatch& w){ return a>=w.lo && a<=w.hi; }), mwatch.end());
                fprintf(stderr,"  removed %zu watch(es) covering %04X\n",before-mwatch.size(),a); } }
        else if (cmd=="wl"){
            if(mwatch.empty() && mwatch16.empty()) fprintf(stderr,"  (no memory watchpoints)\n");
            for(size_t i=0;i<mwatch.size();++i){ const MemWatch& w=mwatch[i];
                const char* k = w.brk?"break":"print";
                const char* dir = (w.rd&&w.wr)?"rw":w.rd?"rd":"wr";
                char cond[24]={0}; if(w.cond==MemWatch::EQ) snprintf(cond,sizeof cond," == %02X",w.val);
                else if(w.cond==MemWatch::NE) snprintf(cond,sizeof cond," != %02X",w.val);
                else if(w.cond==MemWatch::CHG) snprintf(cond,sizeof cond," changed");
                fprintf(stderr,"  [%zu] %s-%s [%04X..%04X]%s  hits=%ld\n",i,k,dir,w.lo,w.hi,cond,w.hits); }
            for(size_t i=0;i<mwatch16.size();++i){ const auto& ww=mwatch16[i]; const auto& w=ww.w;
                auto tx=[&](uint32_t k){ char x[24]; if(ww.raw){ snprintf(x,sizeof x,"em:%05X",(unsigned)k); return std::string(x); }
                    return dbg16::addrText(uint8_t(k>>16),uint16_t(k),true); };
                char cond[24]={0}; if(w.cond==memwatch::MemWatch32::EQ) snprintf(cond,sizeof cond," == %02X",w.val);
                else if(w.cond==memwatch::MemWatch32::NE) snprintf(cond,sizeof cond," != %02X",w.val);
                else if(w.cond==memwatch::MemWatch32::CHG) snprintf(cond,sizeof cond," changed");
                fprintf(stderr,"  [16:%zu] %s-%s [%s..%s]%s  hits=%ld  (%s)\n",i,w.brk?"break":"print",
                        (w.rd&&w.wr)?"rw":w.rd?"rd":"wr",tx(w.lo).c_str(),tx(w.hi).c_str(),cond,w.hits,
                        ww.raw?"roh":"U8001 logisch"); } }
        else if ((cmd=="iow"||cmd=="iob") && t.size()>1){ uint8_t p=(uint8_t)parseNum(t[1]);
            (cmd=="iow"?io_w:io_b).insert(p); fprintf(stderr,"  %s io (%02XH)\n",cmd=="iow"?"watch":"break",p); }
        else if (cmd=="iod" && t.size()>1){ uint8_t p=(uint8_t)parseNum(t[1]); io_w.erase(p); io_b.erase(p); }
        else if (cmd=="iol"){ fprintf(stderr,"  io-watch:"); for(auto p:io_w)fprintf(stderr," %02X",p);
            fprintf(stderr,"\n  io-break:"); for(auto p:io_b)fprintf(stderr," %02X",p); fprintf(stderr,"\n");
            if (em){ fprintf(stderr,"  U8001 io-watch:"); for(auto p:io16_w) fprintf(stderr," %%%04X",p);
                fprintf(stderr,"\n  U8001 io-break:"); for(auto p:io16_b) fprintf(stderr," %%%04X",p); fprintf(stderr,"\n"); } }
        // ══ SYMBOLS & LISTINGS: -s symbol tables and -l .prn listings ══
        else if (cmd=="sym"){
            if (t.size()>=4 && t[1]=="add" && t[3].compare(0,2,"<<")==0){      // U8001: sym add NAME <<s>>off
                dbg16::Addr16 a; if (dbg16::parseAddr(t[3],0,a) && a.kind==dbg16::Addr16::Seg){ sym16.add(t[2],a.key());
                    fprintf(stderr,"  sym %s=%s (U8001)\n",t[2].c_str(),dbg16::addrText(a.seg,a.off,true).c_str()); }
                else fprintf(stderr,"  ? Adresse '%s'\n",t[3].c_str()); }
            else if (t.size()>=4 && t[1]=="add" && isRawTok(t[3]))
                fprintf(stderr,"  ? Symbole zeigen auf <<seg>>off (U8001) oder eine Z80-Adresse, nicht auf em:\n");
            else if (t.size()>=4 && t[1]=="add"){ symAdd(t[2],(uint16_t)parseNum(t[3])); fprintf(stderr,"  sym %s=%04X\n",t[2].c_str(),(uint16_t)parseNum(t[3])); }
            else if (t.size()>=2 && t[1]=="list"){ for(auto&kv:sym_by_addr) fprintf(stderr,"  %04X %s\n",kv.first,kv.second.c_str());
                for(auto&kv:sym16.byKey()) fprintf(stderr,"  %s %s\n",dbg16::addrText(uint8_t(kv.first>>16),uint16_t(kv.first),true).c_str(),kv.second.c_str()); }
            else if (t.size()>=2) loadSyms(t[1]);
            else fprintf(stderr,"  sym <file> | sym add <name> <addr>|<<seg>>off | sym list\n"); }
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
            if(m.loadState(t[1])){ snap1=grab(m.cpuDebug()); callstack.clear(); cs16.clear(); rev_ring.clear();
                fprintf(stderr,"  state loaded ← %s\n",t[1].c_str()); showInsn("=>",m.cpuPC()); stateLine(); }
            else if (!m.stateError().empty()) fprintf(stderr,"  cannot load state %s: %s\n",t[1].c_str(),m.stateError().c_str());
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
            else if (KQ){   // P8000: ADP statt Handschlag-RAM/CP/A-DPB
                P8000Karte8& k=m.p8000()->karte8();
                fprintf(stderr,"  RFF=%d  map=%s  UA858 RR0=%02X\n  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n",
                        k.speicher().rffGesetzt()?1:0,dbgp8::speicherbild(k.speicher()).c_str(),
                        m.p8000()->floppy8().dma().status()); }
            else if (KP){   // PRG: Speicherverwaltung statt Handschlag-RAM/CP/A-DPB
                Prg710Machine& p=*m.prg();
                fprintf(stderr,"  EBH=%02X  map=%s\n  E8H (Herkunft) [0..F]:",p.speicher().freigabe(),dbgm::speicherbild(p).c_str());
                for (int n=0;n<16;++n) fprintf(stderr," %02X",p.speicher().attr(n));
                fprintf(stderr,"\n  EAH (Seite)    [0..F]:");
                for (int n=0;n<16;++n) fprintf(stderr," %X",p.speicher().seite(n));
                fprintf(stderr,"\n  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n"); }
            else if (KC){   // PC 1715: Overlay und BWS statt Handschlag-RAM/CP/A-DPB
                Pc1715Zre& z=m.pc1715()->zre();
                fprintf(stderr,"  ROM-Overlay=%s  BWS(34H)=%02X  Bildbasis=%04X  ZG%d  Format %dx%d (+Statuszeile)\n"
                               "  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n",
                        z.romEin()?"ein":"aus", z.bwsRegister(), z.bildBasis(), z.bwsZg2()?2:1,
                        z.textCols(), z.textRows()); }
            else if (K89){   // K8915: Speicherbild und Anzeigefeld statt Handschlag-RAM/CP/A-DPB
                K8915Machine& k8=*m.k8915(); uint8_t a8=dbgm::k8A8(k8), l=k8.ats().anzeige();
                char b2[48]=""; if (!dbgm::gen2(k8)) snprintf(b2,sizeof b2,"  Bank-2-Viertel=%d%s",(a8>>4)&3,(a8&0x40)?" (eingeblendet)":"");
                fprintf(stderr,"  A8H=%02X  map=%s  /MEMDI=%s /MEMDI1=%s%s\n"
                               "  61H=%02X  Lampen an: %s\n"
                               "  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n",
                        a8, dbgm::speicherbild(k8).c_str(), dbgm::k8Memdi(k8)?"aktiv":"-",
                        dbgm::k8Memdi1(k8)?"aktiv":"-", b2,
                        l, dbgm::lampen61(l).c_str()); }
            else fprintf(stderr,"  [03F8]done=%02X  DPB: [D1B2]=%04X [D1B4]=%04X [D1B8]=%04X [D1BE]=%04X [D1CD]=%04X\n"
                                "  (vars -f <datei> | vars add <name> <addr> [w] | vars clear)\n",
                    m.memReadDebug(0x03F8),wd(0xD1B2),wd(0xD1B4),wd(0xD1B8),wd(0xD1BE),wd(0xD1CD)); }
        else if (cmd=="dev"){
            std::string w = t.size()>1? t[1] : "k5122";
            auto Y=[&](bool b){ return b?"1":"0"; };
            auto showCtc=[&](const char* name, const Z80CTC::DebugState& c){
                fprintf(stderr,"  CTC (%s)  vecBase=%02X  IEI=%s IEO=%s\n",name,c.vecBase,Y(c.iei),Y(c.ieo));
                for(int i=0;i<4;++i){ auto& ch=c.ch[i];
                    fprintf(stderr,"    ch%d ctl=%02X TC=%02X cnt=%-3d run=%s  INT(en=%s pend=%s ius=%s iei=%s)\n",
                        i,ch.control,ch.timeConst,ch.counter,Y(ch.running),Y(ch.intEn),Y(ch.intPending),Y(ch.ius),Y(ch.iei)); } };
            auto showSio=[&](const char* titel, const Z80SIO::DebugState& s){
                fprintf(stderr,"  %s  IEI=%s IEO=%s\n",titel,Y(s.iei),Y(s.ieo));
                for(int i=0;i<2;++i){ auto& ch=s.ch[i];
                    fprintf(stderr,"    %c rr0=%02X rr1=%02X wr1=%02X vec=%02X  irq(rx=%s tx=%s ext=%s) ius=%s iei=%s  rxQ=%zu txBusy=%s\n",
                        i?'B':'A',ch.rr0,ch.rr1,ch.wr1,ch.wr2,Y(ch.irqRx),Y(ch.irqTx),Y(ch.irqExt),Y(ch.ius),Y(ch.iei),ch.rxQueued,Y(ch.txBusy)); } };
            if (KQ){   // P8000: die Bausteine der 8-Bit-Karte, der Floppy-Seite und (p8000-16) der 16-Bit-Karte
                P8000Machine& pm=*m.p8000(); P8000Karte8& k=pm.karte8();
                auto pioZ=[&](const char* name, const Z80PIO::DebugState& pd){
                    fprintf(stderr,"  %s  IEI=%s IEO=%s\n",name,Y(pd.iei),Y(pd.ieo));
                    for(int i=0;i<2;++i){ auto& pt=pd.port[i];
                        fprintf(stderr,"    %c mode=%u out=%02X in=%02X dir=%02X vec=%02X  INT(en=%s pend=%s ius=%s iei=%s)\n",
                            i?'B':'A',pt.mode,pt.out,pt.in,pt.dir,pt.vector,Y(pt.ie),Y(pt.pending),Y(pt.ius),Y(pt.iei)); } };
                auto n16=[&]()->bool{ if(!p16){ fprintf(stderr,"  (dev %s braucht die 16-Bit-Karte — --machine p8000-16)\n",w.c_str()); return false; } return true; };
                if (w=="ctc"){ showCtc("CTC0, 08H-0BH",k.ctc0().debugState()); showCtc("CTC1, 2CH-2FH",k.ctc1().debugState()); }
                else if (w=="sio"){ showSio("SIO0, 24H-27H (A tty0, B tty1 = Konsole)",k.sio0().debugState()); }
                else if (w=="sio2"){ showSio("SIO1, 28H-2BH (A tty2, B tty3)",k.sio1().debugState()); }
                else if (w=="pio"){ pioZ("PIO0 (Kopplung, 0CH-0FH)",k.pio0().debugState());
                    pioZ("PIO1 (EPROMmer, 18H-1BH)",k.pio1().debugState()); pioZ("PIO2 (Floppy-Port, 1CH-1FH)",k.pio2().debugState()); }
                else if (w=="dma"){ for (auto& l: dbgp8::dmaZeilen(pm.floppy8().dma().sicht())) fprintf(stderr,"  %s\n",l.c_str()); }
                else if (w=="fdc"||w=="k5122"){
                    P8000Floppy8& f=pm.floppy8();
                    fprintf(stderr,"  U8272 (20H/21H): INT=%d  TC=%d  gewaehltes Laufwerk=%d  Motor:",f.fdcIntPegel()?1:0,f.tcPegel()?1:0,f.gewaehlt());
                    for (int d=0;d<4;++d) fprintf(stderr," %d=%s",d,f.motorAn(d)?"an":"aus");
                    fprintf(stderr,"\n"); }
                else if (w=="ctc16"){ if (n16()){ showCtc("16-Bit CTC0, FFA9-FFAF",p16->ctc0().debugState()); showCtc("16-Bit CTC1, FFB1-FFB7",p16->ctc1().debugState()); } }
                else if (w=="sio16"){ if (n16()){ showSio("16-Bit SIO0, FF81-FF87 (tty4/5)",p16->sio0().debugState()); showSio("16-Bit SIO1, FF89-FF8F (tty6/7)",p16->sio1().debugState()); } }
                else if (w=="pio16"){ if (n16()){ pioZ("16-Bit PIO0 (Kopplung 16->8, FF91-FF97)",p16->pio0().debugState());
                    pioZ("16-Bit PIO1 (Kopplung 8->16, FF99-FF9F)",p16->pio1().debugState()); pioZ("16-Bit PIO2 (WDC, FFA1-FFA7)",p16->pio2().debugState()); } }
                else fprintf(stderr,"  dev [ctc|sio|sio2|pio|dma|fdc | ctc16|sio16|pio16]   (UA858 ausfuehrlich: `dma`, Kopplung: `kopp`)\n"); }
            else if (KP && (w=="ctc"||w=="sio"||w=="sio2")){ Prg710Machine& p=*m.prg();
                if (w=="ctc") showCtc("ZRE K2521, 80H-83H",p.zre().ctc().debugState());
                else if (w=="sio") showSio(KP1?"K8025 SIO A32 (B = Tastatur K7672)":"K8025 SIO A32 (A = Tastatur-Port, B = Drucker)",
                                           p.ass().sioA32().debugState());
                else showSio("K8025 SIO A33 (DFUE)",p.ass().sioA33().debugState()); }
            else if (KC && (w=="ctc"||w=="sio"||w=="sio2"||w=="crt")){ Pc1715Zre& z=m.pc1715()->zre();
                if (w=="ctc") showCtc("ZRE CTC0, 08H-0BH",z.ctc().debugState());
                else if (w=="crt")
                    fprintf(stderr,"  8275 (18H-1BH)  Bildformat %dx%d, Zeichenlinien %d, Zeilen laut 8275 %d, BWS=%02X Basis %04X ZG%d\n",
                            z.textCols(),z.textRows(),z.zeichenLinien(),z.bildZeilen(),z.bwsRegister(),z.bildBasis(),z.bwsZg2()?2:1);
                else showSio("ZRE SIO0 (A = Tastatur 1715, B = V.24)",z.sio().debugState()); }
            else if (w=="ctc" && K89){ K8915Machine& k8=*m.k8915();
                showCtc(dbgm::gen2(k8)?"ZRE K2521, 80H-83H":"ZRE 045-8762, 80H-83H",dbgm::k8ZreCtc(k8).debugState());
                showCtc("ATS CTC1",k8.ats().ctc1().debugState());
                showCtc("ATS CTC2",k8.ats().ctc2().debugState()); }
            else if ((w=="sio"||w=="sio2") && K89){ K8915Machine& k8=*m.k8915();
                if (w=="sio") showSio("SIO1 (ATS; B = Drucker V.24)",k8.ats().sio1().debugState());
                else          showSio("SIO2 (ATS; B = Tastatur K7672)",k8.ats().sio2().debugState()); }
            else if (w=="ctc"){ showCtc("K2526",m.a5120()->ctcState()); }
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
                    showPio(KC?"FD Steuer-PIO (Ports 04-07)":"K5122 ctrl-PIO (Ports 10-13)", m.k5122CtrlPioState()); any=true; }
                if (all || which=="k5122data" || which=="data"){
                    showPio(KC?"FD Daten-PIO (Ports 00-03)":"K5122 data-PIO (Ports 14-17)", m.k5122DataPioState()); any=true; }
                if (KP && (all || which=="zre")){
                    showPio("ZRE K2521-PIO (Ports 84H-87H)", m.prg()->zre().pio().debugState()); any=true; }
                if (KC && which=="zre"){ nichtAmK8915("Eine ZRE-PIO"); any=true; }
                if (which=="bs" && K8){ nichtAmK8915("Die BS-PIO (K2526)"); any=true; }
                else if (all || which=="bs"){
                    if (!K8) showPio("BS-PIO (K2526, Ports 08-0B)", m.a5120()->bsPioState());
                    any=true; }
                if (!any) fprintf(stderr,"  dev pio [all|bs|zre|k5122ctrl|k5122data]\n"); }
            else if (w=="sio" || w=="sio2"){
                auto s = (w=="sio2")? m.a5120()->dfueSioState() : m.a5120()->kbdSioState();
                char titel[48]; snprintf(titel,sizeof titel,"SIO %s (K8025 %s)",
                    w=="sio2"?"DFUE":"kbd/prn", w=="sio2"?"A33":"A32");
                showSio(titel,s); }
            else if (w=="em"){
                if (!em) fprintf(stderr,"  (kein EM — Start mit --em em256|em064)\n");
                else {
                    const Z8000& z=z16();
                    fprintf(stderr,"  EM %s an MODADR %02XH  %s-Bit-Mode (A29)  LED V1(RAMEN)=%d V2(8-Bit)=%d\n",
                            em->config().variante==EM::Variante::EM256?"EM256 (U8001, 256 KB)":"EM064 (U8002, 64 KB)",
                            em->config().modadr, em->mode8()?"8":"16", em->ledV1(), em->ledV2());
                    fprintf(stderr,"  A22 Seiten (PEN WE A15 A14):");
                    for (int pg=0;pg<16;++pg){ uint8_t a=em->attribute(pg);
                        fprintf(stderr,"%s%X:%c%c%d%d", pg%8?" ":"\n    ", pg, (a&1)?'-':'P', (a&2)?'-':'W', !(a&8), !(a&4)); }
                    fprintf(stderr,"\n  A33=%02X (SegMode %u, SG=%u, INT16=%d, A53-Freigabe=%d)  A35=%02X  A36/Status8=%02X  A34/Vektor8=%02X%s\n",
                            em->steuer16(), em->segMode(), (em->steuer16()>>5)&3, (em->steuer16()>>4)&1, (em->steuer16()>>3)&1,
                            em->status16(), em->status8(), em->vector8(), em->viPending()?" (VI anstehend)":"");
                    fprintf(stderr,"  PIO B: SG=%u RAMEN=%d STOP=%d RESET16=%d TRQ8=%d PR=%d /PE=%d\n",
                            em->segment(), em->ramEnabled(), em->stop16(), em->reset16(), em->trq8(),
                            em->prLine(), !em->parityError());
                    // S5b: Paritäts-FF A46 ist ein MERKER (Plan §7.3, G5): gesetzt bleibt er, bis PR
                    // (PIO B6) oder eine NMI-Quittung des U8001 ihn löscht.
                    fprintf(stderr,"  A46 (Paritaets-FF, Merker)=%d%s  — loeschen: PR=1 oder NMI-Quittung\n",
                            em->parityError(), em->parityError()?" FEHLER GEMERKT":"");
                    {   // A53 zählt Stapelzugriffe rückwärts, solange A54 freigibt; NVI bei QD = 0 (Stand 7).
                        const unsigned a53=em->a53(), vl=em->a53Ladewert();
                        char rest[64]="";
                        if (em->a54Freigabe() && a53>=8) snprintf(rest,sizeof rest,", NVI nach %u weiteren Stapelzugriff(en)",a53-7);
                        fprintf(stderr,"  A53 (Einzelbefehlszaehler)=%u (Vorlast %u) NVI=%d  A54 (Freigabe)=%d %s%s\n",
                                a53, vl, em->nviLine(), em->a54Freigabe(),
                                em->a54Freigabe()? "zaehlt" : "gehalten (geladen bis A33 Bit 3)", rest); }
                    fprintf(stderr,"  TREN=%d BUSRQ16=%d BUSAK16=%d  U8001=%s  Guthaben=%.1f Takte  cyc16=%llu\n",
                            em->tren(), em->busRq16(), em->busAck16(), state16(z),
                            em->guthabenTakte16(), (unsigned long long)z.cycles);
                    auto p=em->pio().debugState();
                    for(int i=0;i<2;++i){ auto& pt=p.port[i];
                        fprintf(stderr,"  PIO-A32 %c mode=%u out=%02X in=%02X dir=%02X vec=%02X  INT(en=%d pend=%d ius=%d)\n",
                                i?'B':'A',pt.mode,pt.out,pt.in,pt.dir,pt.vector,pt.ie,pt.pending,pt.ius); } } }
            else { auto k=m.k5122State();
                if (K8) fprintf(stderr,"  (K8915: K5122 im /WAIT-Betrieb — keine ZVE2, kein /BUSRQ;"
                                        " MKE=%d, geschrieben=%zu B, ganze Spuren=%llu)\n",
                                k.waitMke?1:0, k.waitSchreibBytes,(unsigned long long)k.waitSpuren);
                fprintf(stderr,"  K5122: D%d %s  cyl=%u head=%u  %s%s  headPos=%zu/%zu secSize=%u  /BUSRQ-pend=%s\n",
                        k.drive, k.mounted?"mounted":"EMPTY", k.cylinder, k.head,
                        k.transferring?"READING":"idle", k.writeMode?"+WRITE":"",
                        k.headPos, k.trackLen, k.sectorSize, k.busrq?"yes":"no");
                fprintf(stderr,"  (dev ctc | dev pio [all|bs|k5122ctrl|k5122data] | dev sio | dev sio2 | dev em)\n"); } }
        else if (cmd=="ivt"){    // §6: IM-2-Vektortabelle auf einen Blick
            // Für jede Interruptquelle der Daisy-Chain: programmierter Vektor →
            // Tabellenadresse (I<<8 | vec&0xFE) → dort eingetragene ISR-Adresse.
            // Ein Gerät mit IE=1, dessen Eintrag ins Leere zeigt, ist der klassische
            // Fremd-OS-Fehler (Interruptsturm / Sprung nach 0xFFFF).
            bool useZ2 = t.size()>1 && (t[1]=="2"||t[1]=="zve2");
            if (useZ2 && K8){ nichtAmK8915("ZVE2 ('ivt 2')"); continue; }
            const Z80& z = useZ2? m.zve2Debug() : m.cpuDebug();
            fprintf(stderr,"  %s: I=%02X  IM %u  IFF1=%d\n",
                    useZ2?"ZVE2":C1, z.I, z.IM, (int)z.IFF1);
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
                // Laufwerk als Buchstabe (A..D) oder Zahl (0..3); ohne Angabe A:.
                int drv = 0;
                if (t.size()>=3 && !t[2].empty()){
                    char a = (char)toupper((unsigned char)t[2][0]);
                    if (a>='A'&&a<='D') drv = a-'A';
                    else if (a>='0'&&a<='3') drv = a-'0';
                }
                const char letter[2] = { (char)('A'+drv), 0 };
                diskVerify(disks[drv], (std::string(letter)+":").c_str());
            } else fprintf(stderr,"  disk verify [A|B|C|D]   Sektor-/CRC-Health aller Spuren des Originals\n"); }
        // ══ K8915: Speicherbild (A8H) und DRAM-Bänke direkt ══
        else if (cmd=="map"){
            if (KQ){   // P8000: ADP-Zellen je 4-KB-Seite + RFF (00H-07H)
                for (auto& l: dbgp8::adpZeilen(m.p8000()->karte8().speicher())) fprintf(stderr,"  %s\n",l.c_str());
                if (p16) fprintf(stderr,"  16-Bit-Seite: `mmu`, `xlat <<seg>>off`, `ml`, `mp` (logisch/physisch ueber die UB8010)\n"); }
            else if (KP){   // PRG: die 16 Seitenregister der Speicherverwaltung E8H-EBH
                Prg710Machine& p=*m.prg(); Prg710Speicher& sp=p.speicher();
                fprintf(stderr,"  EBH=%02X (Freigabe: %s)  map=%s\n",sp.freigabe(),
                        sp.freigabe()?"Register wirken":"Abbildung aus",dbgm::speicherbild(p).c_str());
                fprintf(stderr,"    Seite  Adressen     E8H  Herkunft  EAH  Quelle\n");
                for (int n=0;n<16;++n){
                    const uint8_t a=sp.attr(n);
                    const char* her = (a&0x0F)==0 ? "OPS" : n==0 ? "ZRE" : n==15 ? "VRAM/Sys" : "Sys";
                    auto o=sp.ortVon((uint16_t)((n<<12)|(n==15?0x800:0)));
                    char q[32];
                    switch (o.quelle){
                        case Prg710Speicher::Quelle::Zre:  snprintf(q,sizeof q,"ZRE +%04X",o.offset); break;
                        case Prg710Speicher::Quelle::Vram: snprintf(q,sizeof q,"VRAM +%04X",o.offset); break;
                        case Prg710Speicher::Quelle::Ops:  snprintf(q,sizeof q,"OPS +%04X",o.offset); break;
                        default: snprintf(q,sizeof q,"leer"); }
                    fprintf(stderr,"    %X      %04X-%04X    %02X   %-8s  %X    %s\n",n,(n<<12),(n<<12)|0x0FFF,a,her,sp.seite(n),q);
                }
            }
            else if (KC){   // PC 1715: ROM-Overlay + BWS-Register
                Pc1715Zre& z=m.pc1715()->zre();
                fprintf(stderr,"  Overlay 0000-07FF: %s  (Lesen: %s, Schreiben: RAM; 24H-27H ein, 28H-2BH aus, /RESET ein)\n",
                        z.romEin()?"EIN":"aus", z.romEin()?"S502":"RAM");
                fprintf(stderr,"  RAM 0000-FFFF (64 KB flach)\n");
                fprintf(stderr,"  BWS 34H=%02X  Bildbasis %04X (Wert<<10, DB0 maskiert)  ZG%d (DB6 XOR GPA0)\n",
                        z.bwsRegister(), z.bildBasis(), z.bwsZg2()?2:1);
                fprintf(stderr,"  Bild: %dx%d Zeichen (+ Statuszeile bei 25/17 Zeilen), 8275-Zeilen aktuell %d\n",
                        z.textCols(), z.textRows(), z.bildZeilen());
            }
            else if (!K89) fprintf(stderr,"  map gibt es nur am K8915 (A8H-Speicherbild), am PRG (E8H-EBH) und am PC 1715 (Overlay/BWS) — am A5120 nicht vorhanden\n");
            else if (dbgm::gen2(*m.k8915())){   // Gen 2: K3528 (A8H–ABH) statt Bank 1/2
                K8915Machine& k8=*m.k8915(); K3528& o=k8.ops();
                fprintf(stderr,"  A8H=%02X  map=%s  /MEMDI=%s /MEMDI1=%s  (Z=ZRE K2521 M=RAM K3528 .=Bus; je 4 KB ab 0000H)\n",
                        o.reg(),dbgm::speicherbild(o).c_str(),o.memdi()?"aktiv":"-",o.memdi1()?"aktiv":"-");
                static const char* qn[3]={"K3528","ZRE","Bus"};
                for (int p=0;p<16;++p){ auto q=o.ortVon((uint16_t)(p<<12));
                    fprintf(stderr,"    %04X-%04X  %-6s",(p<<12),(p<<12)|0x0FFF,qn[(int)q]);
                    if (q==K3528::Quelle::Bus) fprintf(stderr,"%s\n", (p==1)?"  (K7024-Bildspeicher)":"");
                    else fprintf(stderr,"  +%04X\n",(unsigned)(p<<12)); } }
            else { K8915Machine& k8=*m.k8915(); K8915Zre& z=k8.zre();
                fprintf(stderr,"  A8H=%02X  /MEMDI=%s /MEMDI1=%s  Bank-2-Viertel=%d\n",z.reg(),
                        z.memdi()?"aktiv":"-", z.memdi1()?"aktiv":"-", (z.reg()>>4)&3);
                static const char* qn[4]={"ROM","Bank 1","Bank 2","Bus"};
                for (int p=0;p<16;++p){ auto o=z.ortVon((uint16_t)(p<<12));
                    fprintf(stderr,"    %04X-%04X  %-6s",(p<<12),(p<<12)|0x0FFF,qn[(int)o.quelle]);
                    if (o.quelle==K8915Zre::Quelle::Bus) fprintf(stderr,"%s\n", (p==1)?"  (K7024-Bildspeicher)":"");
                    else fprintf(stderr,"  +%05X\n",(unsigned)o.offset); } } }
        else if (cmd=="bank"){
            if (!K89) fprintf(stderr,"  bank gibt es nur am K8915 (zwei DRAM-Baenke) — am %s nicht vorhanden\n",m.name());
            else if (dbgm::gen2(*m.k8915())) fprintf(stderr,"  bank: nicht vorhanden (Gen 2 ohne Bank 2; Bank 1 = RAM der K3528, 'd' liest es in CPU-Sicht)\n");
            else if (t.size()<3 || (t[1]!="1" && t[1]!="2"))
                fprintf(stderr,"  bank <1|2> <A> [N]   Hexdump direkt aus Bank 1/2 (Bank 2: Viertel q ab q*4000H)\n");
            else { int b=(t[1]=="2")?1:0; uint16_t a=(uint16_t)parseNum(t[2]);
                int len=t.size()>3?(int)parseNum(t[3]):64;
                for (int o=0;o<len;o+=16){ char asc[17]={0};
                    fprintf(stderr,"  B%d:%04X: ",b+1,(uint16_t)(a+o));
                    for (int i=0;i<16;++i){ if(o+i<len){ uint8_t v=m.k8915()->zre().bankPeek(b,(uint16_t)(a+o+i));
                        fprintf(stderr,"%02X ",v); asc[i]=(v>=0x20&&v<0x7F)?(char)v:'.'; }
                        else { fprintf(stderr,"   "); asc[i]=' '; } }
                    fprintf(stderr," |%s|\n",asc); } } }
        else if (cmd=="raf"){   // RAM-Floppy: Zustand bzw. ein 128-B-Sektor in Treiber-Lesart
            const RAF* r = m.base().raf();
            if (!r){ fprintf(stderr,"  keine RAF gesteckt (--raf raf128|raf512|raf2m)\n"); }
            else if (t.size()<2){
                const uint16_t l=r->latch();
                fprintf(stderr,"  %s  %u KByte (%u Sektoren)  Port 88H/89H\n",dbgm::rafName(r->config().typ),
                        (unsigned)(r->kapazitaet()/1024),(unsigned)r->sektoren());
                fprintf(stderr,"  Latch=%04X  Sperrmaske=%04X  gesperrt: %s\n",l,r->sperrmaske(),r->gesperrt()?"ja":"nein");
                if (r->gesperrt()) fprintf(stderr,"  Sektor aus dem Latch: - (gesperrt)\n");
                else fprintf(stderr,"  Sektor aus dem Latch: %u (%04XH)\n",
                             (unsigned)(l&r->sektormaske()),(unsigned)(l&r->sektormaske())); }
            else {
                const long sek=parseNum(t[1]);
                if (sek<0 || (uint32_t)sek>=r->sektoren())
                    fprintf(stderr,"  ? Sektor 0..%u (RAF hat %u Sektoren)\n",(unsigned)(r->sektoren()-1),(unsigned)r->sektoren());
                else {
                    // Treiber-Lesart: INIR/OTIR laufen rueckwaerts, Byte i des Sektors liegt in der Karte
                    // bei Sektor*128 + 127 - i (siehe `help raf`).
                    fprintf(stderr,"  RAF Sektor %ld (%lXH), Byte 0 zuerst (Treiber-Lesart):\n",sek,sek);
                    for (int o=0;o<128;o+=16){
                        char asc[17]; fprintf(stderr,"  %02X: ",o);
                        for (int i=0;i<16;++i){ uint8_t v=r->peek((uint32_t)sek*128u+127u-(uint32_t)(o+i));
                            fprintf(stderr,"%02X ",v); asc[i]=(v>=0x20&&v<0x7F)?(char)v:'.'; }
                        asc[16]=0; fprintf(stderr," |%s|\n",asc); } } } }
        else if (cmd=="clock"){   // §7: Uhrenwahl für Lauf-Budgets (g/gu/gscreen/hist)
            if (t.size()>1){
                if (t[1]=="zve1") clock_machine=false;
                else if (t[1]=="machine"||t[1]=="maschine") clock_machine=true;
                else { fprintf(stderr,"  clock [zve1|machine]\n"); } }
            fprintf(stderr,"  Lauf-Uhr = %s   ZVE1=%llu  Maschine=%llu\n",
                    clock_machine?"Maschine (beide CPUs)":"ZVE1",
                    (unsigned long long)m.cpuCycles(),(unsigned long long)m.machineCycles()); }
        else if (cmd=="console"){   // §9: Maschine live bedienen, Haltepunkte bleiben scharf
            double sp = (t.size()>1)? atof(t[1].c_str()) : 1.0;
            consoleMode(sp); }
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
        // ══ P8000: ADP, UA858, Kopplung, MMU, Terminal (AP P12) ══
        else if (p8Kommando(t)){ /* erledigt */ }
        // ══ A5120.16: CPU-Kontext, U8001-Zustand, Ereignis-Halte (S5) ══
        else if (cmd=="cpu"){
            if (t.size()>1){
                std::string c=t[1]; for(auto&ch:c) ch=(char)tolower((unsigned char)ch);
                if (c=="zve1"||c=="1"||(KQ && (c=="u880"||c=="z80"))) cpu_ctx=1;
                else if ((c=="zve2"||c=="2") && !KQ) cpu_ctx=2;
                else if (KQ && c=="wdc"){
                    // Nur Ansicht: Haltepunkte/Schritte bleiben beim U880 bzw. U8001; der WDC läuft mit.
                    if (!wdcK) fprintf(stderr,"  kein WDC (--machine p8000-16 --wdc 4.2 bzw. --hd <abbild>)\n");
                    else { std::vector<std::string> tw={"wdc","r"}; p8Kommando(tw);
                           fprintf(stderr,"  (WDC-Z80 nur zur Ansicht: wdc r | wdc u | wdc d | wdc log — Haltepunkte/Schritte bleiben beim U880/U8001)\n"); } }
                else if (c=="u8000"||c=="u8001"||c=="u8002"||c=="16"||c=="3"){
                    if (!has16) fprintf(stderr,"  (kein U8001 in dieser Maschine — A5120: --em em256, P8000: --machine p8000-16)\n");
                    else cpu_ctx=3; }
                else fprintf(stderr,KQ? "  cpu [u880|u8000|wdc]\n" : "  cpu [zve1|zve2|u8000]\n"); }
            fprintf(stderr,"  CPU-Kontext: %s\n", cpu_ctx==3?"U8001 (r s n fin gu u a b lp wp.. bt x d e set … — help u8000)":
                                                cpu_ctx==2?"ZVE2":KQ?"U880":"ZVE1");
            if (cpu_ctx==3) showInsn16("=>",pcKey16(z16()));
            else showInsn("=>", cpu_ctx==2? m.zve2PC() : m.cpuPC()); }
        else if (cmd=="fcw"){
            if (!has16) fprintf(stderr,"  (kein U8001)\n");
            else { Z8000& z=z16();
                if (t.size()>1){ long v; if(dbg16::parseNumber(t[1],v)) z.fcw=(uint16_t)v; else fprintf(stderr,"  ? Wert\n"); }
                fprintf(stderr,"  FCW=%%%04X  %s\n",z.fcw,dbg16::fcwText(z.fcw).c_str()); } }
        else if (cmd=="psa"){
            // Program Status Area: je Eintrag neues FCW + neuer PC; VI mit Sprungtabelle.
            if (!has16) fprintf(stderr,"  (kein U8001)\n");
            else { const Z8000& z=z16();
                const bool z1 = z.isZ8001(); const int mult = z1? 2 : 1;
                const uint8_t sg = z1? uint8_t((z.psapSeg>>8)&0x7F) : 0;
                const uint16_t base = uint16_t(z.psapOff & 0xFF00);
                fprintf(stderr,"  PSAP=%s (%s; gelesen über die Segmentweiche als Programmspeicher, System)\n",
                        dbg16::addrText(sg,base,z1).c_str(), z1?"U8001":"U8002");
                struct E{ const char* n; uint16_t off; } es[]={{"EPA",Z8000::PSA_EPA},{"PRIV",Z8000::PSA_PRIV},
                    {"SC",Z8000::PSA_SC},{"SEGT",Z8000::PSA_SEGT},{"NMI",Z8000::PSA_NMI},{"NVI",Z8000::PSA_NVI},{"VI",Z8000::PSA_VI}};
                // PSA-Lesen hat Status 1100 und läuft im Systemmodus.
                auto rw=[&](uint16_t o)->uint16_t{ Z8kBusCycle c; c.st=Z8kStatus::MemInstr; c.system=true; c.seg=sg; c.addr=uint16_t(o&~1u);
                    Z8kBusCycle c2=c; c2.addr=uint16_t(c.addr+1);
                    return uint16_t((rdbCyc16(c)<<8)|rdbCyc16(c2)); };
                int nvec = t.size()>1? (int)parseNum(t[1]) : 4;
                for (auto& e: es){
                    uint16_t f = uint16_t(base + e.off*mult + (z1?2:0));
                    uint16_t fcw=rw(f);
                    if (z1) fprintf(stderr,"  %-4s @%s  FCW=%04X  PC=%s\n",e.n,dbg16::addrText(sg,f,true).c_str(),fcw,
                                    dbg16::addrText(uint8_t((rw(uint16_t(f+2))>>8)&0x7F),rw(uint16_t(f+4)),true).c_str());
                    else    fprintf(stderr,"  %-4s @%%%04X  FCW=%04X  PC=%%%04X\n",e.n,f,fcw,rw(uint16_t(f+2)));
                }
                uint16_t fv = uint16_t(base + Z8000::PSA_VI*mult + (z1?2:0));
                for (int i=0;i<nvec;++i){ const int id = z1? 2*i : i;   // Z8001: 4 Byte je Eintrag ⇒ gerade Kennungen
                    uint16_t p=uint16_t(fv+2+2*id);
                    if (z1) fprintf(stderr,"    VI-Kennung %02X → %s\n",id,
                                    dbg16::addrText(uint8_t((rw(p)>>8)&0x7F),rw(uint16_t(p+2)),true).c_str());
                    else    fprintf(stderr,"    VI-Kennung %02X → %%%04X\n",id,rw(p)); }
                fprintf(stderr,"  (psa <n> = n VI-Einträge; Eintrag = PSAP+VI+2+2·(Kennung & FF))\n"); } }
        else if (cmd=="bmode"||cmd=="bvi"||cmd=="bint16"){
            if (!em){ fprintf(stderr,"  (kein EM)\n"); }
            else if (cmd=="bmode"){
                int mode = 3;
                if (t.size()>1){ if(t[1]=="16") mode=1; else if(t[1]=="8") mode=2; else if(t[1]=="off") mode=0; else if(t[1]=="both"||t[1]=="on") mode=3; }
                else if (brk_mode) mode=0;          // blosses Kommando schaltet um
                brk_mode=mode;
                fprintf(stderr,"  break-on-Moduswechsel %s\n", mode==0?"off":mode==1?"→16":mode==2?"→8":"both"); }
            else { bool& f = cmd=="bvi"? brk_vi : brk_int16;
                f = (t.size()>1)? (t[1]!="off") : !f;
                fprintf(stderr,"  break-on-%s %s\n", cmd=="bvi"?"VI (Quittung am U8001)":"INT-16 (A33 Bit 4)", f?"ON":"off"); } }
        else if (cmd=="emlog"){
            if (!em){ fprintf(stderr,"  (kein EM)\n"); }
            else {
                if (emlog_fp && emlog_file) fclose(emlog_fp);
                emlog_fp=nullptr; emlog_file=false;
                if (t.size()<2 || t[1]=="on"){ emlog_fp=stderr; fprintf(stderr,"  emlog → Konsole (jede EM-Transaktion eine Zeile; emlog off)\n"); }
                else if (t[1]=="off") fprintf(stderr,"  emlog off\n");
                else { emlog_fp=fopen(t[1].c_str(),"w"); emlog_file=emlog_fp!=nullptr;
                    if(!emlog_fp) fprintf(stderr,"  cannot open %s\n",t[1].c_str());
                    else fprintf(stderr,"  emlog → %s\n",t[1].c_str()); } } }
        else fprintf(stderr,"  ? unknown command '%s' (try help)\n",cmd.c_str());
    }
    if (trace_fp){ fclose(trace_fp); fprintf(stderr,"trace closed (%ld line(s))\n",trace_lines); }
    if (emlog_fp && emlog_file) fclose(emlog_fp);
    if (itrace_fp){ fclose(itrace_fp); fprintf(stderr,"itrace closed (%ld INT/NMI)\n",itrace_n); }
    for (auto& t : cow_temps){ std::error_code ec; std::filesystem::remove(t,ec); }   // drop COW temps
    return mount_failed ? 1 : 0;   // non-zero exit if a requested disk failed to mount
}
