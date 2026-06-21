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
#include "tools/z80dis_min.h"
#include "tools/prn_listing.h"
#include "tools/callstack_tracker.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>

using k1520::logging::Logger;
using k1520::logging::Level;

// ─── Register snapshot captured at the start of an instruction ────────────────
struct Snap {
    uint16_t PC=0,SP=0,AF=0,BC=0,DE=0,HL=0,IX=0,IY=0,AF_=0,BC_=0,DE_=0,HL_=0;
    uint8_t  I=0,R=0; uint64_t cyc=0; bool halted=false; bool valid=false;
};
static inline Snap grab(const Z80& z){
    Snap s; s.PC=z.PC; s.SP=z.SP; s.AF=z.AF; s.BC=z.BC; s.DE=z.DE; s.HL=z.HL;
    s.IX=z.IX; s.IY=z.IY; s.AF_=z.AF_; s.BC_=z.BC_; s.DE_=z.DE_; s.HL_=z.HL_;
    s.I=z.I; s.R=z.R; s.cyc=z.cycles; s.halted=z.halted; s.valid=true; return s;
}

// ─── breakpoint record ────────────────────────────────────────────────────────
struct Bp { bool enabled=true; bool temp=false; std::string cond; long hits=0; long ignore=0; };

int main(int argc, char** argv){
    const char* disk = nullptr;
    const char* script = nullptr;
    std::vector<std::string> symfiles;
    std::vector<std::string> prnfiles;
    for (int i=1;i<argc;++i){
        if (!strcmp(argv[i],"-x") && i+1<argc) script=argv[++i];
        else if (!strcmp(argv[i],"-s") && i+1<argc) symfiles.push_back(argv[++i]);
        else if (!strcmp(argv[i],"-l") && i+1<argc) prnfiles.push_back(argv[++i]);
        else disk=argv[i];
    }
    Logger::instance().setBaseLevel(Level::ERROR);

    A5120Machine m;
    m.powerOn();
    if (disk){
        if (!(m.mountDisk(0,disk,"cpa780",false) || m.mountDisk(0,disk,"cpa800",false)))
            fprintf(stderr,"WARN: mount '%s' failed: %s\n",disk,m.lastError().c_str());
        else fprintf(stderr,"Mounted %s on A:\n",disk);
    }

    // ─── debugger state ────────────────────────────────────────────────────────
    std::map<uint16_t,Bp> bp1, bp2;            // breakpoints per CPU
    int  tw1lo=-1,tw1hi=-1, tw2lo=-1,tw2hi=-1; long tw_cap=4000, tw_n=0;
    uint64_t rel_origin=0; bool rel_armed=false; int rel_arm_pc=-1;
    Snap snap1, snap2;
    bool hit=false; int hit_cpu=0; uint16_t hit_pc=0; std::string stop_reason;
    int  gu_pc=-1; long step_rem=0; long step2_rem=0;
    bool fin_active=false; uint16_t fin_sp=0;
    uint16_t last_u=0; bool last_u_set=false;

    // ─── reverse-debugging + history backtrace state ──────────────────────────
    cstrack::CallStackTracker callstack;                     // exact CALL/RST/RET stack
    bool bt_use_history = true;                              // `bt scan` forces old heuristic
    std::deque<A5120Machine::MachineSnapshot> rev_ring;      // auto snapshot before each fwd cmd
    const size_t rev_cap = 200;                              // ring depth (≈13 MB)
    std::map<std::string,A5120Machine::MachineSnapshot> named_snaps;   // snap <name>

    // memory watchpoints: address RANGE + optional VALUE-condition; print or break.
    struct MemWatch {
        uint16_t lo=0, hi=0;                   // address range (single byte → lo==hi)
        bool wr=false, rd=false, brk=false;    // on write / on read ; brk=stop else print
        enum Cond { ANY, EQ, NE, CHG } cond=ANY;
        uint8_t val=0;                         // for EQ/NE
        std::map<uint16_t,uint8_t> last;       // for CHG: last seen value per address
        long hits=0;
    };
    std::vector<MemWatch> mwatch;
    std::set<uint8_t>  io_w, io_b;             // io : print-on-access / break-on-access

    // symbols
    std::map<std::string,uint16_t> sym_by_name;
    std::map<uint16_t,std::string> sym_by_addr;

    // display list (shown at every stop): each entry is a raw token
    std::vector<std::string> displays;

    // ─── trace-to-file + logpoints ("run and log", no stopping) ────────────────
    FILE* trace_fp   = nullptr;                 // `trace <file>`: continuous instr trace
    int   trace_lo   = -1, trace_hi = -1;       // optional PC window for the file trace
    long  trace_lines= 0;                        // lines written this session
    long  trace_cap  = 2000000;                  // safety cap (~prevents runaway files)
    bool  trace_capped = false;
    // logpoints: at PC, print (PC + optional exprs) and CONTINUE — gdb dprintf.
    std::map<uint16_t,std::vector<std::string>> logpoints;

    auto rc = [&](uint64_t cyc)->long long {            // relative-or-absolute cycle
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

    // ─── .prn listings (Adresse → kommentierte Original-Quellzeile) ─────────────
    prnlst::Listing prn;
    // spec = "PFAD[@OFFSET]"; OFFSET (signiert, hex 0x../..h oder dez) wird zu jeder
    // Listing-Adresse addiert — für Code, der nicht an der .prn-Adresse läuft.
    auto loadPrnSpec = [&](const std::string& spec)->int{
        std::string path; long off=0;
        if (!prnlst::splitSpec(spec,path,off)){ fprintf(stderr,"  bad @offset in '%s'\n",spec.c_str()); return -1; }
        int n = prn.load(path,off);
        if (n < 0) fprintf(stderr,"  cannot open %s\n",path.c_str());
        else if (off) fprintf(stderr,"  loaded %d listing line(s) from %s (offset %+ld / %04X)\n",n,path.c_str(),off,(uint16_t)off);
        else fprintf(stderr,"  loaded %d listing line(s) from %s\n",n,path.c_str());
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

    // readOperand: evaluate a register / [mem] / (rr) / number against a snapshot.
    auto readOperand = [&](const Snap& s, std::string t, bool& ok)->long{
        ok=true;
        std::string u=t; for(auto&c:u) c=(char)toupper(c);
        auto hi=[&](uint16_t v){return (v>>8)&0xFF;}; auto lo=[&](uint16_t v){return v&0xFF;};
        if (u=="A") return hi(s.AF); if (u=="F") return lo(s.AF);
        if (u=="B") return hi(s.BC); if (u=="C") return lo(s.BC);
        if (u=="D") return hi(s.DE); if (u=="E") return lo(s.DE);
        if (u=="H") return hi(s.HL); if (u=="L") return lo(s.HL);
        if (u=="AF") return s.AF; if (u=="BC") return s.BC; if (u=="DE") return s.DE;
        if (u=="HL") return s.HL; if (u=="IX") return s.IX; if (u=="IY") return s.IY;
        if (u=="SP") return s.SP; if (u=="PC") return s.PC;
        if (u=="I")  return s.I;  if (u=="R")  return s.R;
        if (u=="(HL)") return m.memReadDebug(s.HL); if (u=="(DE)") return m.memReadDebug(s.DE);
        if (u=="(BC)") return m.memReadDebug(s.BC); if (u=="(SP)") return m.memReadDebug(s.SP);
        if (!t.empty() && t[0]=='['){
            size_t r=t.find(']'); std::string inner=t.substr(1, r==std::string::npos? std::string::npos : r-1);
            bool word = (r!=std::string::npos && r+1<t.size() && (t[r+1]=='w'||t[r+1]=='W'));
            uint16_t a=(uint16_t)resolveAddr(inner);
            return word ? (m.memReadDebug(a)|(m.memReadDebug(a+1)<<8)) : m.memReadDebug(a); }
        // plain number / symbol
        return resolveAddr(t);
    };

    // evalCond: "LHS OP RHS" with OP in == != <= >= < > ; empty → true.
    auto evalCond = [&](const Snap& s, const std::string& cond)->bool{
        if (cond.empty()) return true;
        static const char* ops[]={"==","!=","<=",">=","<",">"};
        for (const char* op: ops){
            size_t pos=cond.find(op);
            if (pos==std::string::npos) continue;
            std::string ls=cond.substr(0,pos), rs=cond.substr(pos+strlen(op));
            auto trim=[](std::string& x){ while(!x.empty()&&x.front()==' ')x.erase(x.begin());
                                          while(!x.empty()&&x.back()==' ')x.pop_back(); };
            trim(ls); trim(rs); bool ok1,ok2;
            long a=readOperand(s,ls,ok1), b=readOperand(s,rs,ok2);
            if(!strcmp(op,"==")) return a==b; if(!strcmp(op,"!=")) return a!=b;
            if(!strcmp(op,"<=")) return a<=b; if(!strcmp(op,">=")) return a>=b;
            if(!strcmp(op,"<"))  return a<b;  return a>b;
        }
        return true; // no operator → always
    };

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

    // ─── per-instruction callbacks ─────────────────────────────────────────────
    m.setCpuTraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        // Maintain the exact CALL/RST/RET call stack (for the history backtrace).
        // Cheap: 1 mem read/instr in the common (non-call/ret) case.
        callstack.onInstruction(pc,[&](uint16_t a){ return m.memReadDebug(a); });
        // "run and log" (no stopping): continuous file trace + logpoints fire first.
        traceToFile(1,z);
        { auto lit=logpoints.find(pc); if(lit!=logpoints.end()) logHit(z,lit->second); }
        if (rel_arm_pc>=0 && pc==(uint16_t)rel_arm_pc){
            rel_origin=z.cycles; rel_armed=true; rel_arm_pc=-1;
            fprintf(stderr,"[mark] relative origin set at PC=%04X (abs cyc=%llu)\n",
                    pc,(unsigned long long)z.cycles);
        }
        if (tw1lo>=0 && pc>=tw1lo && pc<=tw1hi && tw_n<tw_cap){ traceLine(1,z); ++tw_n; }
        if (step_rem>0){ traceLine(1,z); if(--step_rem==0){stopAt(1,z,"step");} return; }
        if (fin_active && z.SP > fin_sp){ fin_active=false; stopAt(1,z,"step-out"); return; }
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
        traceToFile(2,z);   // gap-free trace across DMA phases (ZVE2 also logged)
        if (tw2lo>=0 && pc>=tw2lo && pc<=tw2hi && tw_n<tw_cap){ traceLine(2,z); ++tw_n; }
        if (step2_rem>0){ traceLine(2,z); if(--step2_rem==0){stopAt(2,z,"step ZVE2");} return; }
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
            if (addr < w.lo || addr > w.hi) continue;
            if (isRead ? !w.rd : !w.wr) continue;
            bool fire;
            switch (w.cond){
                case MemWatch::EQ: fire = (data==w.val); break;
                case MemWatch::NE: fire = (data!=w.val); break;
                case MemWatch::CHG:{ auto it=w.last.find(addr);
                    fire = (it==w.last.end()) || (it->second!=data);
                    w.last[addr]=data; break; }
                default: fire = true;
            }
            if (!fire) continue;
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
    auto showDisplays = [&]{
        if (displays.empty()) return;
        Snap& s = (hit_cpu==2)?snap2:snap1;
        for (size_t i=0;i<displays.size();++i){
            bool ok; long v=readOperand(s,displays[i],ok);
            fprintf(stderr,"  disp[%zu] %-10s = %ld (0x%lX)\n",i,displays[i].c_str(),v,(unsigned long)(v&0xFFFF));
        }
    };
    auto onStop = [&]{
        fprintf(stderr,"** %s : ZVE%d PC=%04X\n",stop_reason.c_str(),hit_cpu,hit_pc);
        printSnap(hit_cpu);
        showInsn("=>", hit_pc);
        showDisplays();
        stateLine();
    };
    auto screen = [&]{
        for (int row=0;row<24;++row){ char ln[81];
            for (int c=0;c<80;++c){ uint8_t ch=m.memReadDebug((uint16_t)(0xF800+row*80+c));
                ln[c]=(ch>=0x20&&ch<0x7F)?(char)ch:'.'; }
            ln[80]=0; fprintf(stderr,"  |%s|\n",ln); }
    };
    // silent run kernel: runs until a stop is signalled or budget/cap reached.
    auto goSilent = [&](uint64_t budget)->uint64_t{
        hit=false; m.clearStop(); tw_n=0;
        uint64_t start=m.cpuCycles();
        uint64_t cap = budget? budget : 400000000ULL;     // safety cap for bare `g`
        while (!hit){
            if (m.cpuCycles()-start>=cap) break;
            int n=m.run(50000); if(n==0) break;
        }
        return m.cpuCycles()-start;
    };
    auto go = [&](uint64_t budget){
        uint64_t ran=goSilent(budget);
        if (hit){ fprintf(stderr,"   (ran %llu cyc)\n",(unsigned long long)ran); onStop(); }
        else { fprintf(stderr,"   ran %llu cyc, no breakpoint (PC=%04X)\n",
                     (unsigned long long)ran,m.cpuPC()); stateLine(); }
    };
    // single step-over of one ZVE1 instruction (silent — caller prints)
    auto stepOver = [&]{
        z80dis::Insn d = z80dis::decode(rd1, m.cpuPC());
        if (d.is_call || d.is_repeat){ gu_pc=(int)(uint16_t)(m.cpuPC()+d.len); goSilent(0); }
        else { step_rem=1; m.clearStop(); while(step_rem>0){int n=m.run(20000); if(n==0||hit)break;} }
    };
    auto keys = [&](const std::string& t){
        for (size_t i=0;i<t.size();++i){
            uint32_t code; char c=t[i];
            if (c=='\\' && i+1<t.size()){ char e=t[++i];
                code = (e=='r'||e=='n')?0x01000004 : e=='t'?0x09 : e=='e'?0x1B : (uint8_t)e; }
            else code=(uint8_t)c;
            m.keyPress(code,false,false); go(600000); if(hit) return;
            m.keyRelease(code);          go(150000); if(hit) return;
        }
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
        for (auto it=f.rbegin(); it!=f.rend() && frame<=depth; ++it,++frame){
            // The caller's PC is the CALL site; show it (and its return address).
            fprintf(stderr,"  #%d %04X (call → %04X, ret %04X)%s\n",
                    frame, it->site, it->target, it->ret, annot(it->site).c_str());
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

    // ─── command stream: optional script first, then stdin ─────────────────────
    std::deque<std::string> pending;
    if (script){ std::ifstream f(script); std::string l; while(std::getline(f,l)) pending.push_back(l); }
    for (auto& sf : symfiles) loadSyms(sf);
    for (auto& pf : prnfiles) loadPrnSpec(pf);

    fprintf(stderr,"k1520dbg — type 'help'.  Clock = ZVE1 cycles.  Disassembler: built-in.\n");
    std::string line;
    for (;;){
        if (!pending.empty()){ line=pending.front(); pending.pop_front(); fprintf(stderr,"(dbg) %s\n",line.c_str()); }
        else { fprintf(stderr,"(dbg) "); if(!std::getline(std::cin,line)) break; }
        std::istringstream is(line); std::vector<std::string> t; std::string w;
        while (is>>w) t.push_back(w);
        if (t.empty()||t[0][0]=='#') continue;
        const std::string& cmd=t[0];

        if (cmd=="q"||cmd=="quit") break;
        else if (cmd=="help"||cmd=="h"||cmd=="?"){
            fprintf(stderr,
              "  RUN     g/c [N]   run to breakpoint (or N ZVE1-cycles)\n"
              "          gu <A>    run until ZVE1 reaches A (temp bp)\n"
              "          s [N]     step INTO N ZVE1 instrs ;  s2 [N] step ZVE2\n"
              "          n [N]     step OVER N ZVE1 instrs (skip CALL/blockrepeat)\n"
              "          fin       step OUT (run until SP rises above current frame)\n"
              "  REVERSE rs [N]    reverse-step: undo last N forward commands (snapshot ring)\n"
              "          snap <name> | snap list ; restore <name>   named full snapshots\n"
              "  BREAK   b <A> [if <cond>] | b2 <A> ...   bp on ZVE1 / ZVE2\n"
              "          tb <A>    temporary (one-shot) bp ; bd/bd2 <A> delete ; bl list\n"
              "          be/bdis <A> (be2/bdis2) enable/disable ; bi/bi2 <A> <N> ignore N hits\n"
              "          cond: REG/[addr]/[addr]w/(rr)  OP  value   OP: == != < > <= >=\n"
              "  WATCH   wp/wpr/wb <A|A..B> [==v|!=v|changed]   mem watch (range+cond):\n"
              "                          print-write / print-read / break-write\n"
              "          wd <A>|all  wl  delete (covering A) / list mem watches\n"
              "          iow/iob <P>     io port: print / break ; iod <P> wl-io: iol\n"
              "  LOG     logpoint <A> [expr..]  print + CONTINUE (dprintf) ; lpd <A> ; lpl\n"
              "          trace <file> [lo hi]   log every executed instr to file ; trace off\n"
              "  INSPECT r [2]     registers (ZVE1, +ZVE2) ;  bt [N] backtrace (exact; bt scan = heuristic)\n"
              "          d <A> [N] hexdump ; u [A] [N] disasm ; e <A> <b..> poke\n"
              "          set [2] <reg> <v>   edit register ; vars   named RAM vars\n"
              "          dev       K5122 controller state (drive/cyl/head/transfer)\n"
              "          disp <expr> | undisp <n> | disp   show expr at every stop\n"
              "  MEM     load <f> <A>   read binary into RAM ; save <f> <A> <N> dump RAM\n"
              "  MISC    mark [A]  zero relative cycle counter (now / armed at A)\n"
              "          sym <f> | sym add <name> <A> | sym list\n"
              "          lst <f.prn>[@off] | lst <f.prn> <off> | lst list   MACRO-80 listing → annotate\n"
              "          keys <text> (\\r \\t \\e) ; screen ; reset ; q\n");
        }
        else if (cmd=="g"||cmd=="c"){ pushHistory(); go(t.size()>1? (uint64_t)parseNum(t[1]) : 0); }
        else if (cmd=="gu" && t.size()>1){ pushHistory(); gu_pc=(int)(uint16_t)parseNum(t[1]); go(0); }
        else if (cmd=="s"){ pushHistory(); step_rem=t.size()>1?parseNum(t[1]):1; m.clearStop();
            while(step_rem>0){ int n=m.run(20000); if(n==0||hit) break; }
            if(hit){ hit=false; onStop(); } else stateLine(); }
        else if (cmd=="s2"){ pushHistory(); step2_rem=t.size()>1?parseNum(t[1]):1; m.clearStop();
            while(step2_rem>0){ int n=m.run(20000); if(n==0||hit) break; }
            if(hit){ hit=false; onStop(); } else { fprintf(stderr,"  (ZVE2 did not run — /BUSRQ not asserted?)\n"); stateLine(); } }
        else if (cmd=="n"){ pushHistory(); long k=t.size()>1?parseNum(t[1]):1;
            for(long i=0;i<k;++i){ stepOver(); if(hit) break; }
            if(hit){ hit=false; onStop(); } else { snap1=grab(m.cpuDebug()); showInsn("=>",m.cpuPC()); stateLine(); } }
        else if (cmd=="fin"){ pushHistory(); fin_sp=m.cpuSP(); fin_active=true; go(0); }
        else if (cmd=="rs"||cmd=="bs") reverseStep(t.size()>1?parseNum(t[1]):1);
        else if (cmd=="snap"){
            if (t.size()>=2 && t[1]=="list"){
                if(named_snaps.empty()) fprintf(stderr,"  (no named snapshots)\n");
                for(auto&kv:named_snaps) fprintf(stderr,"  %-16s PC=%04X cyc=%llu\n",
                        kv.first.c_str(),kv.second.zve1.PC,(unsigned long long)kv.second.zve1.cycles); }
            else if (t.size()>=2){ m.captureState(named_snaps[t[1]]);
                fprintf(stderr,"  snapshot '%s' saved (PC=%04X)\n",t[1].c_str(),m.cpuPC()); }
            else fprintf(stderr,"  snap <name> | snap list   (restore with: restore <name>)\n"); }
        else if (cmd=="restore" && t.size()>=2){
            auto it=named_snaps.find(t[1]);
            if(it==named_snaps.end()) fprintf(stderr,"  no snapshot '%s' (snap list)\n",t[1].c_str());
            else { pushHistory(); applySnapshot(it->second,("snapshot '"+t[1]+"'").c_str()); } }
        else if ((cmd=="b"||cmd=="b2") && t.size()>1){
            auto& tbl = (cmd=="b2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
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
            show(bp1,"ZVE1"); show(bp2,"ZVE2"); }
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
        else if (cmd=="mark"){ if(t.size()>1){ rel_arm_pc=(int)(uint16_t)parseNum(t[1]); fprintf(stderr,"  mark armed at PC=%04X\n",rel_arm_pc);}
            else { rel_origin=m.cpuCycles(); rel_armed=true; fprintf(stderr,"  mark: origin=%llu (now)\n",(unsigned long long)rel_origin);} }
        else if (cmd=="r"){ snap1=grab(m.cpuDebug()); printSnap(1);
            if(t.size()>1){ snap2=grab(m.zve2Debug()); printSnap(2); } showInsn("=>",m.cpuPC()); stateLine(); }
        else if (cmd=="bt"){
            if (t.size()>1 && t[1]=="scan"){ bool prev=bt_use_history; bt_use_history=false;
                backtrace(t.size()>2?(int)parseNum(t[2]):8); bt_use_history=prev; }
            else backtrace(t.size()>1?(int)parseNum(t[1]):8); }
        else if (cmd=="d" && t.size()>1) dump((uint16_t)parseNum(t[1]), t.size()>2?(int)parseNum(t[2]):64);
        else if (cmd=="e" && t.size()>2){ uint16_t a=(uint16_t)parseNum(t[1]);
            // poke values default to HEX (matches d/u output); strtol base 16 also accepts 0x..
            for(size_t i=2;i<t.size();++i)
                m.memWriteDebug(a++,(uint8_t)strtol(t[i].c_str(),nullptr,16));
            fprintf(stderr,"  poked %zu byte(s)\n",t.size()-2); }
        else if (cmd=="u"){
            uint16_t a = t.size()>1? (uint16_t)parseNum(t[1]) : (last_u_set? last_u : m.cpuPC());
            int cnt = t.size()>2? (int)parseNum(t[2]) : 12;
            for(int i=0;i<cnt;++i){ char l[120]; int len=disasmAt(a,l,sizeof l);
                std::string p=prnFor(a);
                fprintf(stderr,"  %s%s%s\n",l, p.empty()?"":"  ; ", p.c_str()); a=(uint16_t)(a+len); }
            last_u=a; last_u_set=true; }
        else if (cmd=="set" && t.size()>=3){
            int cpu=1; size_t idx=1; if(t[1]=="2"){cpu=2; idx=2;}
            if (idx+1<t.size() && setReg(cpu,t[idx],parseNum(t[idx+1])))
                fprintf(stderr,"  ZVE%d %s := 0x%lX\n",cpu,t[idx].c_str(),parseNum(t[idx+1])&0xFFFF);
            else fprintf(stderr,"  ? bad register\n"); }
        else if (cmd=="disp"){ if(t.size()>1){ displays.push_back(t[1]); fprintf(stderr,"  disp[%zu] = %s\n",displays.size()-1,t[1].c_str()); }
            else { for(size_t i=0;i<displays.size();++i) fprintf(stderr,"  disp[%zu] %s\n",i,displays[i].c_str()); } }
        else if (cmd=="undisp" && t.size()>1){ size_t i=(size_t)parseNum(t[1]); if(i<displays.size()) displays.erase(displays.begin()+i); }
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
        else if (cmd=="load" && t.size()>2){ std::ifstream f(t[1],std::ios::binary);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); }
            else { uint16_t a=(uint16_t)parseNum(t[2]); int n=0; char b;
                while(f.get(b)){ m.memWriteDebug(a++,(uint8_t)b); ++n; } fprintf(stderr,"  loaded %d byte(s) @%04lX\n",n,parseNum(t[2])&0xFFFF); } }
        else if (cmd=="save" && t.size()>3){ std::ofstream f(t[1],std::ios::binary);
            uint16_t a=(uint16_t)parseNum(t[2]); int n=(int)parseNum(t[3]);
            for(int i=0;i<n;++i) f.put((char)m.memReadDebug(a+i)); fprintf(stderr,"  saved %d byte(s) from %04X to %s\n",n,a,t[1].c_str()); }
        else if (cmd=="keys" && t.size()>1){ pushHistory(); std::string s=line.substr(line.find("keys")+5); keys(s); }
        else if (cmd=="screen") screen();
        else if (cmd=="vars"){ auto wd=[&](uint16_t a){return (uint16_t)(m.memReadDebug(a)|(m.memReadDebug(a+1)<<8));};
            fprintf(stderr,"  [03F8]done=%02X  DPB: [D1B2]=%04X [D1B4]=%04X [D1B8]=%04X [D1BE]=%04X [D1CD]=%04X\n",
                    m.memReadDebug(0x03F8),wd(0xD1B2),wd(0xD1B4),wd(0xD1B8),wd(0xD1BE),wd(0xD1CD)); }
        else if (cmd=="dev"){ auto k=m.k5122State();
            fprintf(stderr,"  K5122: D%d %s  cyl=%u head=%u  %s%s  headPos=%zu/%zu secSize=%u  /BUSRQ-pend=%s\n",
                    k.drive, k.mounted?"mounted":"EMPTY", k.cylinder, k.head,
                    k.transferring?"READING":"idle", k.writeMode?"+WRITE":"",
                    k.headPos, k.trackLen, k.sectorSize, k.busrq?"yes":"no"); }
        else if (cmd=="reset"){ m.reset(); fprintf(stderr,"  reset\n"); }
        else fprintf(stderr,"  ? unknown command '%s' (try help)\n",cmd.c_str());
    }
    if (trace_fp){ fclose(trace_fp); fprintf(stderr,"trace closed (%ld line(s))\n",trace_lines); }
    return 0;
}
