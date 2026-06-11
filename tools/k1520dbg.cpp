/**
 * @file k1520dbg.cpp
 * @brief Interactive command-line debugger for the A5120 / K1520 core.
 *
 * A gdb-style front end around A5120Machine.  Unlike boot_trace (which filters by
 * ABSOLUTE cycles from power-on — handy for the boot ROM, awkward once the loaded
 * OS or a transient program is running), this tool lets you:
 *
 *   - run to BREAKPOINTS on either CPU (ZVE1 main / ZVE2 DMA),
 *   - set a MARKER that zeroes a RELATIVE cycle counter (e.g. at the OS entry or a
 *     program's load-and-go point), so all later trace/timing is measured from there,
 *   - single-STEP, dump/disassemble memory at any point, watch RAM writes,
 *   - inject keystrokes and view the screen — all from one interactive session.
 *
 * Commands come from stdin (interactive or piped) and/or a -x script file.
 * Type `help` for the command list.  See tools/README.md §k1520dbg.
 *
 * @license MIT
 */
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>

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

int main(int argc, char** argv){
    const char* disk = nullptr;
    const char* script = nullptr;
    for (int i=1;i<argc;++i){
        if (!strcmp(argv[i],"-x") && i+1<argc) script=argv[++i];
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
    std::vector<bool> bp1(0x10000,false), bp2(0x10000,false);
    int  tw1lo=-1,tw1hi=-1, tw2lo=-1,tw2hi=-1; long tw_cap=4000, tw_n=0;
    std::set<uint16_t> wps;
    uint64_t rel_origin=0; bool rel_armed=false; int rel_arm_pc=-1;
    Snap snap1, snap2;
    bool hit=false; int hit_cpu=0; uint16_t hit_pc=0;
    int  gu_pc=-1; long step_rem=0;

    auto rc = [&](uint64_t cyc)->long long {            // relative-or-absolute cycle
        return rel_armed ? (long long)(cyc-rel_origin) : (long long)cyc;
    };
    auto rcpfx = [&]{ return rel_armed ? '+' : 'c'; };

    auto traceLine = [&](int cpu, const Z80& z){
        uint8_t b0=m.memReadDebug(z.PC),b1=m.memReadDebug(z.PC+1),
                b2=m.memReadDebug(z.PC+2),b3=m.memReadDebug(z.PC+3);
        fprintf(stderr,"T%d %c%-9lld PC=%04X  %02X %02X %02X %02X  "
                "AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X\n",
                cpu,rcpfx(),rc(z.cycles),z.PC,b0,b1,b2,b3,
                z.AF,z.BC,z.DE,z.HL,z.IX,z.IY,z.SP);
    };

    // ─── per-instruction callbacks ─────────────────────────────────────────────
    // Hot path: grab a full snapshot ONLY when we actually stop (bp/step/gu),
    // never per-instruction — keeps long `g` runs fast.
    m.setCpuTraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        if (rel_arm_pc>=0 && pc==(uint16_t)rel_arm_pc){
            rel_origin=z.cycles; rel_armed=true; rel_arm_pc=-1;
            fprintf(stderr,"[mark] relative origin set at PC=%04X (abs cyc=%llu)\n",
                    pc,(unsigned long long)z.cycles);
        }
        if (tw1lo>=0 && pc>=tw1lo && pc<=tw1hi && tw_n<tw_cap){ traceLine(1,z); ++tw_n; }
        if (step_rem>0){ traceLine(1,z); if(--step_rem==0){snap1=grab(z);hit=true;hit_cpu=1;hit_pc=pc;m.stop();} return; }
        if (gu_pc>=0 && pc==(uint16_t)gu_pc){snap1=grab(z);hit=true;hit_cpu=1;hit_pc=pc;gu_pc=-1;m.stop();return;}
        if (bp1[pc]){snap1=grab(z);hit=true;hit_cpu=1;hit_pc=pc;m.stop();}
    });
    m.setZVE2TraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        if (tw2lo>=0 && pc>=tw2lo && pc<=tw2hi && tw_n<tw_cap){ traceLine(2,z); ++tw_n; }
        if (bp2[pc]){snap2=grab(z);hit=true;hit_cpu=2;hit_pc=pc;m.stop();}
    });
    m.setBusTrace([&](bool isIO,bool isRead,uint16_t addr,uint8_t data){
        if (isIO||isRead) return;
        if (wps.count(addr))
            fprintf(stderr,"[wp] %c%-9lld WR [%04X]=%02X  ZVE1.PC=%04X\n",
                    rcpfx(),rc(m.cpuCycles()),addr,data,m.cpuPC());
    });

    // ─── helpers ───────────────────────────────────────────────────────────────
    auto printSnap = [&](int cpu){
        const Snap& s = (cpu==2)?snap2:snap1;
        if (!s.valid){ fprintf(stderr,"  (CPU%d: no state captured yet — run first)\n",cpu); return; }
        uint16_t ret=(uint16_t)(m.memReadDebug(s.SP)|(m.memReadDebug(s.SP+1)<<8));
        fprintf(stderr,
            "  ZVE%d PC=%04X SP=%04X (->%04X)  AF=%04X BC=%04X DE=%04X HL=%04X  "
            "IX=%04X IY=%04X  AF'=%04X BC'=%04X DE'=%04X HL'=%04X  I=%02X R=%02X%s  cyc=%llu\n",
            cpu,s.PC,s.SP,ret,s.AF,s.BC,s.DE,s.HL,s.IX,s.IY,s.AF_,s.BC_,s.DE_,s.HL_,
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
    auto screen = [&]{
        for (int row=0;row<24;++row){ char ln[81];
            for (int c=0;c<80;++c){ uint8_t ch=m.memReadDebug((uint16_t)(0xF800+row*80+c));
                ln[c]=(ch>=0x20&&ch<0x7F)?(char)ch:'.'; }
            ln[80]=0; fprintf(stderr,"  |%s|\n",ln); }
    };
    auto go = [&](uint64_t budget){
        hit=false; m.clearStop(); tw_n=0;
        uint64_t start=m.cpuCycles();
        uint64_t cap = budget? budget : 400000000ULL;     // safety cap for bare `g`
        while (!hit){
            if (m.cpuCycles()-start>=cap) break;
            int n=m.run(50000); if(n==0) break;
        }
        uint64_t ran=m.cpuCycles()-start;
        if (hit){ fprintf(stderr,"** break: ZVE%d PC=%04X  (ran %llu cyc)\n",
                          hit_cpu,hit_pc,(unsigned long long)ran); printSnap(hit_cpu); }
        else fprintf(stderr,"   ran %llu cyc, no breakpoint (PC=%04X)\n",
                     (unsigned long long)ran,m.cpuPC());
        stateLine();
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

    auto parseNum=[&](const std::string& s)->long{ return strtol(s.c_str(),nullptr,0); };

    // ─── command stream: optional script first, then stdin ─────────────────────
    std::deque<std::string> pending;
    if (script){ std::ifstream f(script); std::string l; while(std::getline(f,l)) pending.push_back(l); }

    fprintf(stderr,"k1520dbg — type 'help'.  CPU clock = ZVE1 cycles.\n");
    std::string line;
    for (;;){
        if (!pending.empty()){ line=pending.front(); pending.pop_front(); fprintf(stderr,"(dbg) %s\n",line.c_str()); }
        else { fprintf(stderr,"(dbg) "); if(!std::getline(std::cin,line)) break; }
        // tokenize
        std::istringstream is(line); std::vector<std::string> t; std::string w;
        while (is>>w) t.push_back(w);
        if (t.empty()||t[0][0]=='#') continue;
        const std::string& cmd=t[0];

        if (cmd=="q"||cmd=="quit") break;
        else if (cmd=="help"||cmd=="h"||cmd=="?"){
            fprintf(stderr,
              "  g [N]            run to next breakpoint (or N ZVE1-cycles)\n"
              "  gu <PC>          run until ZVE1 reaches PC (temp breakpoint)\n"
              "  s [N]            single-step N ZVE1 instructions (default 1)\n"
              "  b <PC> | b2 <PC> set breakpoint on ZVE1 / ZVE2 ;  bd/bd2 <PC> delete ; bl list\n"
              "  mark [PC]        zero the relative cycle counter now, or arm it at PC\n"
              "  r                registers (ZVE1+ZVE2) + state\n"
              "  d <A> [N]        hex+ascii dump (default 64) ; e <A> <b..> poke bytes\n"
              "  u <A> [N]        disassemble N bytes (via tools/z80_disasm2.py)\n"
              "  t <lo> <hi> [N]  trace ZVE1 instrs in PC window (cap N) ; t2 .. ; t off\n"
              "  wp <A> | wd <A> | wl    watch RAM writes\n"
              "  keys <text>      type text (\\r=Enter \\t \\e) — advances the machine\n"
              "  screen           dump 80x24 text VRAM ; vars  named RAM vars\n"
              "  reset            power-cycle ; q quit\n");
        }
        else if (cmd=="g")      go(t.size()>1? (uint64_t)parseNum(t[1]) : 0);
        else if (cmd=="gu" && t.size()>1){ gu_pc=(int)parseNum(t[1]); go(0); }
        else if (cmd=="s"){ step_rem=t.size()>1?parseNum(t[1]):1; m.clearStop();
            while(step_rem>0){ int n=m.run(20000); if(n==0||hit) break; } hit=false; stateLine(); }
        else if (cmd=="b"  && t.size()>1){ bp1[(uint16_t)parseNum(t[1])]=true; fprintf(stderr,"  bp ZVE1 @%04lX\n",parseNum(t[1])&0xFFFF); }
        else if (cmd=="b2" && t.size()>1){ bp2[(uint16_t)parseNum(t[1])]=true; fprintf(stderr,"  bp ZVE2 @%04lX\n",parseNum(t[1])&0xFFFF); }
        else if (cmd=="bd" && t.size()>1) bp1[(uint16_t)parseNum(t[1])]=false;
        else if (cmd=="bd2"&& t.size()>1) bp2[(uint16_t)parseNum(t[1])]=false;
        else if (cmd=="bl"){ fprintf(stderr,"  ZVE1:"); for(int i=0;i<0x10000;++i) if(bp1[i]) fprintf(stderr," %04X",i);
            fprintf(stderr,"\n  ZVE2:"); for(int i=0;i<0x10000;++i) if(bp2[i]) fprintf(stderr," %04X",i); fprintf(stderr,"\n"); }
        else if (cmd=="mark"){ if(t.size()>1){ rel_arm_pc=(int)parseNum(t[1]); fprintf(stderr,"  mark armed at PC=%04X\n",rel_arm_pc);}
            else { rel_origin=m.cpuCycles(); rel_armed=true; fprintf(stderr,"  mark: origin=%llu (now)\n",(unsigned long long)rel_origin);} }
        else if (cmd=="r"){ printSnap(1); printSnap(2); stateLine(); }
        else if (cmd=="d" && t.size()>1) dump((uint16_t)parseNum(t[1]), t.size()>2?(int)parseNum(t[2]):64);
        else if (cmd=="e" && t.size()>2){ uint16_t a=(uint16_t)parseNum(t[1]);
            for(size_t i=2;i<t.size();++i) m.memWriteDebug(a++,(uint8_t)parseNum(t[i])); fprintf(stderr,"  poked %zu byte(s)\n",t.size()-2); }
        else if (cmd=="u" && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]); int n=t.size()>2?(int)parseNum(t[2]):32;
            { std::ofstream f("/tmp/k1520dbg_u.bin",std::ios::binary); for(int i=0;i<n;++i){uint8_t b=m.memReadDebug(a+i); f.put((char)b);} }
            char buf[256]; snprintf(buf,sizeof(buf),"python3 tools/z80_disasm2.py --org 0x%04X /tmp/k1520dbg_u.bin 2>/dev/null | tail -n +8",a);
            if (system(buf)!=0) fprintf(stderr,"  (disasm helper failed — run k1520dbg from the repo root)\n"); }
        else if (cmd=="t"){ if(t.size()>1&&t[1]=="off"){tw1lo=tw1hi=-1;fprintf(stderr,"  ZVE1 trace off\n");}
            else if(t.size()>=3){ tw1lo=(int)parseNum(t[1]); tw1hi=(int)parseNum(t[2]); if(t.size()>3)tw_cap=parseNum(t[3]);
                fprintf(stderr,"  ZVE1 trace %04X..%04X cap=%ld\n",tw1lo,tw1hi,tw_cap);} }
        else if (cmd=="t2"){ if(t.size()>1&&t[1]=="off"){tw2lo=tw2hi=-1;fprintf(stderr,"  ZVE2 trace off\n");}
            else if(t.size()>=3){ tw2lo=(int)parseNum(t[1]); tw2hi=(int)parseNum(t[2]); if(t.size()>3)tw_cap=parseNum(t[3]);
                fprintf(stderr,"  ZVE2 trace %04X..%04X cap=%ld\n",tw2lo,tw2hi,tw_cap);} }
        else if (cmd=="wp" && t.size()>1){ wps.insert((uint16_t)parseNum(t[1])); fprintf(stderr,"  watch [%04lX]\n",parseNum(t[1])&0xFFFF); }
        else if (cmd=="wd" && t.size()>1) wps.erase((uint16_t)parseNum(t[1]));
        else if (cmd=="wl"){ fprintf(stderr,"  watch:"); for(auto a:wps) fprintf(stderr," %04X",a); fprintf(stderr,"\n"); }
        else if (cmd=="keys" && t.size()>1){ std::string s=line.substr(line.find("keys")+5); keys(s); }
        else if (cmd=="screen") screen();
        else if (cmd=="vars"){ auto w=[&](uint16_t a){return (uint16_t)(m.memReadDebug(a)|(m.memReadDebug(a+1)<<8));};
            fprintf(stderr,"  [03F8]done=%02X  DPB: [D1B2]=%04X [D1B4]=%04X [D1B8]=%04X [D1BE]=%04X [D1CD]=%04X\n",
                    m.memReadDebug(0x03F8),w(0xD1B2),w(0xD1B4),w(0xD1B8),w(0xD1BE),w(0xD1CD)); }
        else if (cmd=="reset"){ m.reset(); fprintf(stderr,"  reset\n"); }
        else fprintf(stderr,"  ? unknown command '%s' (try help)\n",cmd.c_str());
    }
    return 0;
}
