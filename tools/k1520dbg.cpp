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
struct Bp { bool enabled=true; bool temp=false; std::string cond; long hits=0; };

int main(int argc, char** argv){
    const char* disk = nullptr;
    const char* script = nullptr;
    std::vector<std::string> symfiles;
    for (int i=1;i<argc;++i){
        if (!strcmp(argv[i],"-x") && i+1<argc) script=argv[++i];
        else if (!strcmp(argv[i],"-s") && i+1<argc) symfiles.push_back(argv[++i]);
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

    // watch/break sets
    std::set<uint16_t> wp_w, wp_r, wb_w;       // mem: print-on-write / print-on-read / break-on-write
    std::set<uint8_t>  io_w, io_b;             // io : print-on-access / break-on-access

    // symbols
    std::map<std::string,uint16_t> sym_by_name;
    std::map<uint16_t,std::string> sym_by_addr;

    // display list (shown at every stop): each entry is a raw token
    std::vector<std::string> displays;

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
        char l[120]; disasmAt(a,l,sizeof l); fprintf(stderr,"  %s %s\n",tag,l); };

    auto flagsStr = [&](uint16_t af, char* o){
        uint8_t f=af&0xFF; snprintf(o,12,"%c%c%c%c%c%c",
            f&Z80::FLAG_S?'S':'-', f&Z80::FLAG_Z?'Z':'-', f&Z80::FLAG_H?'H':'-',
            f&Z80::FLAG_PV?'P':'-', f&Z80::FLAG_N?'N':'-', f&Z80::FLAG_C?'C':'-'); };

    auto traceLine = [&](int cpu, const Z80& z){
        char dis[120]; disasmAt(z.PC,dis,sizeof dis);
        char fl[12]; flagsStr(z.AF,fl);
        fprintf(stderr,"T%d %c%-9lld %-46s AF=%04X[%s] BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X\n",
                cpu,rcpfx(),rc(z.cycles),dis,z.AF,fl,z.BC,z.DE,z.HL,z.IX,z.IY,z.SP);
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
            it->second.hits++; std::string why="bp ZVE1";
            if(!it->second.cond.empty()) why+=" ["+it->second.cond+"]";
            if(it->second.temp) bp1.erase(it);
            stopAt(1,z,why);
        }
    });
    m.setZVE2TraceCallback([&](const Z80& z){
        const uint16_t pc=z.PC;
        if (tw2lo>=0 && pc>=tw2lo && pc<=tw2hi && tw_n<tw_cap){ traceLine(2,z); ++tw_n; }
        if (step2_rem>0){ traceLine(2,z); if(--step2_rem==0){stopAt(2,z,"step ZVE2");} return; }
        auto it=bp2.find(pc);
        if (it!=bp2.end() && it->second.enabled && evalCond(grab(z),it->second.cond)){
            it->second.hits++; std::string why="bp ZVE2";
            if(!it->second.cond.empty()) why+=" ["+it->second.cond+"]";
            if(it->second.temp) bp2.erase(it);
            stopAt(2,z,why);
        }
    });
    m.setBusTrace([&](bool isIO,bool isRead,uint16_t addr,uint8_t data){
        if (isIO){ uint8_t port=(uint8_t)addr;
            if (io_w.count(port))
                fprintf(stderr,"[io] %c%-9lld %s (%02XH)=%02X  ZVE1.PC=%04X\n",
                        rcpfx(),rc(m.cpuCycles()),isRead?"IN ":"OUT",port,data,m.cpuPC());
            if (io_b.count(port)){ char w[40]; snprintf(w,sizeof w,"io %s (%02XH)=%02X",isRead?"IN":"OUT",port,data);
                stopFromBus(w); }
            return;
        }
        if (isRead){ if (wp_r.count(addr))
            fprintf(stderr,"[wr] %c%-9lld RD [%04X]=%02X  ZVE1.PC=%04X\n",
                    rcpfx(),rc(m.cpuCycles()),addr,data,m.cpuPC());
            return; }
        if (wp_w.count(addr))
            fprintf(stderr,"[wp] %c%-9lld WR [%04X]=%02X  ZVE1.PC=%04X\n",
                    rcpfx(),rc(m.cpuCycles()),addr,data,m.cpuPC());
        if (wb_w.count(addr)){ char w[40]; snprintf(w,sizeof w,"watch [%04X]=%02X",addr,data); stopFromBus(w); }
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
    auto backtrace = [&](int depth){
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

    auto parseNum=[&](const std::string& s)->long{ return resolveAddr(s); };

    // ─── command stream: optional script first, then stdin ─────────────────────
    std::deque<std::string> pending;
    if (script){ std::ifstream f(script); std::string l; while(std::getline(f,l)) pending.push_back(l); }
    for (auto& sf : symfiles) loadSyms(sf);

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
              "  BREAK   b <A> [if <cond>] | b2 <A> ...   bp on ZVE1 / ZVE2\n"
              "          tb <A>    temporary (one-shot) bp ; bd/bd2 <A> delete ; bl list\n"
              "          cond: REG/[addr]/[addr]w/(rr)  OP  value   OP: == != < > <= >=\n"
              "  WATCH   wp/wpr/wb <A>   mem watch: print-write / print-read / break-write\n"
              "          wd <A> wl       delete / list mem watches\n"
              "          iow/iob <P>     io port: print / break ; iod <P> wl-io: iol\n"
              "  INSPECT r [2]     registers (ZVE1, +ZVE2) ;  bt [N] backtrace\n"
              "          d <A> [N] hexdump ; u [A] [N] disasm ; e <A> <b..> poke\n"
              "          set [2] <reg> <v>   edit register ; vars   named RAM vars\n"
              "          disp <expr> | undisp <n> | disp   show expr at every stop\n"
              "  MEM     load <f> <A>   read binary into RAM ; save <f> <A> <N> dump RAM\n"
              "  MISC    mark [A]  zero relative cycle counter (now / armed at A)\n"
              "          sym <f> | sym add <name> <A> | sym list\n"
              "          keys <text> (\\r \\t \\e) ; screen ; reset ; q\n");
        }
        else if (cmd=="g"||cmd=="c") go(t.size()>1? (uint64_t)parseNum(t[1]) : 0);
        else if (cmd=="gu" && t.size()>1){ gu_pc=(int)(uint16_t)parseNum(t[1]); go(0); }
        else if (cmd=="s"){ step_rem=t.size()>1?parseNum(t[1]):1; m.clearStop();
            while(step_rem>0){ int n=m.run(20000); if(n==0||hit) break; }
            if(hit){ hit=false; onStop(); } else stateLine(); }
        else if (cmd=="s2"){ step2_rem=t.size()>1?parseNum(t[1]):1; m.clearStop();
            while(step2_rem>0){ int n=m.run(20000); if(n==0||hit) break; }
            if(hit){ hit=false; onStop(); } else { fprintf(stderr,"  (ZVE2 did not run — /BUSRQ not asserted?)\n"); stateLine(); } }
        else if (cmd=="n"){ long k=t.size()>1?parseNum(t[1]):1;
            for(long i=0;i<k;++i){ stepOver(); if(hit) break; }
            if(hit){ hit=false; onStop(); } else { snap1=grab(m.cpuDebug()); showInsn("=>",m.cpuPC()); stateLine(); } }
        else if (cmd=="fin"){ fin_sp=m.cpuSP(); fin_active=true; go(0); }
        else if ((cmd=="b"||cmd=="b2") && t.size()>1){
            auto& tbl = (cmd=="b2")? bp2 : bp1; uint16_t a=(uint16_t)parseNum(t[1]);
            Bp bp; if (t.size()>3 && t[2]=="if"){ for(size_t i=3;i<t.size();++i){ if(i>3)bp.cond+=" "; bp.cond+=t[i]; } }
            tbl[a]=bp; fprintf(stderr,"  bp %s @%04X%s\n",cmd=="b2"?"ZVE2":"ZVE1",a, bp.cond.empty()?"":(" if "+bp.cond).c_str()); }
        else if (cmd=="tb" && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]); Bp bp; bp.temp=true; bp1[a]=bp;
            fprintf(stderr,"  temp bp ZVE1 @%04X\n",a); }
        else if (cmd=="bd" && t.size()>1) bp1.erase((uint16_t)parseNum(t[1]));
        else if (cmd=="bd2"&& t.size()>1) bp2.erase((uint16_t)parseNum(t[1]));
        else if (cmd=="bl"){
            fprintf(stderr,"  ZVE1 breakpoints:\n");
            for(auto&kv:bp1){ std::string s=symFor(kv.first);
                fprintf(stderr,"    %04X%s%s%s  hits=%ld%s\n",kv.first, s.empty()?"":" <",s.c_str(),s.empty()?"":">",
                        kv.second.hits, kv.second.cond.empty()?"":(" if "+kv.second.cond).c_str()); }
            fprintf(stderr,"  ZVE2 breakpoints:\n");
            for(auto&kv:bp2){ fprintf(stderr,"    %04X  hits=%ld%s\n",kv.first,kv.second.hits,
                        kv.second.cond.empty()?"":(" if "+kv.second.cond).c_str()); } }
        else if (cmd=="mark"){ if(t.size()>1){ rel_arm_pc=(int)(uint16_t)parseNum(t[1]); fprintf(stderr,"  mark armed at PC=%04X\n",rel_arm_pc);}
            else { rel_origin=m.cpuCycles(); rel_armed=true; fprintf(stderr,"  mark: origin=%llu (now)\n",(unsigned long long)rel_origin);} }
        else if (cmd=="r"){ snap1=grab(m.cpuDebug()); printSnap(1);
            if(t.size()>1){ snap2=grab(m.zve2Debug()); printSnap(2); } showInsn("=>",m.cpuPC()); stateLine(); }
        else if (cmd=="bt") backtrace(t.size()>1?(int)parseNum(t[1]):8);
        else if (cmd=="d" && t.size()>1) dump((uint16_t)parseNum(t[1]), t.size()>2?(int)parseNum(t[2]):64);
        else if (cmd=="e" && t.size()>2){ uint16_t a=(uint16_t)parseNum(t[1]);
            // poke values default to HEX (matches d/u output); strtol base 16 also accepts 0x..
            for(size_t i=2;i<t.size();++i)
                m.memWriteDebug(a++,(uint8_t)strtol(t[i].c_str(),nullptr,16));
            fprintf(stderr,"  poked %zu byte(s)\n",t.size()-2); }
        else if (cmd=="u"){
            uint16_t a = t.size()>1? (uint16_t)parseNum(t[1]) : (last_u_set? last_u : m.cpuPC());
            int cnt = t.size()>2? (int)parseNum(t[2]) : 12;
            for(int i=0;i<cnt;++i){ char l[120]; int len=disasmAt(a,l,sizeof l); fprintf(stderr,"  %s\n",l); a=(uint16_t)(a+len); }
            last_u=a; last_u_set=true; }
        else if (cmd=="set" && t.size()>=3){
            int cpu=1; size_t idx=1; if(t[1]=="2"){cpu=2; idx=2;}
            if (idx+1<t.size() && setReg(cpu,t[idx],parseNum(t[idx+1])))
                fprintf(stderr,"  ZVE%d %s := 0x%lX\n",cpu,t[idx].c_str(),parseNum(t[idx+1])&0xFFFF);
            else fprintf(stderr,"  ? bad register\n"); }
        else if (cmd=="disp"){ if(t.size()>1){ displays.push_back(t[1]); fprintf(stderr,"  disp[%zu] = %s\n",displays.size()-1,t[1].c_str()); }
            else { for(size_t i=0;i<displays.size();++i) fprintf(stderr,"  disp[%zu] %s\n",i,displays[i].c_str()); } }
        else if (cmd=="undisp" && t.size()>1){ size_t i=(size_t)parseNum(t[1]); if(i<displays.size()) displays.erase(displays.begin()+i); }
        else if ((cmd=="wp"||cmd=="wpr"||cmd=="wb") && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]);
            (cmd=="wp"?wp_w:cmd=="wpr"?wp_r:wb_w).insert(a);
            fprintf(stderr,"  %s [%04X]\n",cmd=="wp"?"watch-write":cmd=="wpr"?"watch-read":"break-write",a); }
        else if (cmd=="wd" && t.size()>1){ uint16_t a=(uint16_t)parseNum(t[1]); wp_w.erase(a);wp_r.erase(a);wb_w.erase(a); }
        else if (cmd=="wl"){ fprintf(stderr,"  watch-write:"); for(auto a:wp_w)fprintf(stderr," %04X",a);
            fprintf(stderr,"\n  watch-read :"); for(auto a:wp_r)fprintf(stderr," %04X",a);
            fprintf(stderr,"\n  break-write:"); for(auto a:wb_w)fprintf(stderr," %04X",a); fprintf(stderr,"\n"); }
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
        else if (cmd=="load" && t.size()>2){ std::ifstream f(t[1],std::ios::binary);
            if(!f){ fprintf(stderr,"  cannot open %s\n",t[1].c_str()); }
            else { uint16_t a=(uint16_t)parseNum(t[2]); int n=0; char b;
                while(f.get(b)){ m.memWriteDebug(a++,(uint8_t)b); ++n; } fprintf(stderr,"  loaded %d byte(s) @%04lX\n",n,parseNum(t[2])&0xFFFF); } }
        else if (cmd=="save" && t.size()>3){ std::ofstream f(t[1],std::ios::binary);
            uint16_t a=(uint16_t)parseNum(t[2]); int n=(int)parseNum(t[3]);
            for(int i=0;i<n;++i) f.put((char)m.memReadDebug(a+i)); fprintf(stderr,"  saved %d byte(s) from %04X to %s\n",n,a,t[1].c_str()); }
        else if (cmd=="keys" && t.size()>1){ std::string s=line.substr(line.find("keys")+5); keys(s); }
        else if (cmd=="screen") screen();
        else if (cmd=="vars"){ auto wd=[&](uint16_t a){return (uint16_t)(m.memReadDebug(a)|(m.memReadDebug(a+1)<<8));};
            fprintf(stderr,"  [03F8]done=%02X  DPB: [D1B2]=%04X [D1B4]=%04X [D1B8]=%04X [D1BE]=%04X [D1CD]=%04X\n",
                    m.memReadDebug(0x03F8),wd(0xD1B2),wd(0xD1B4),wd(0xD1B8),wd(0xD1BE),wd(0xD1CD)); }
        else if (cmd=="reset"){ m.reset(); fprintf(stderr,"  reset\n"); }
        else fprintf(stderr,"  ? unknown command '%s' (try help)\n",cmd.c_str());
    }
    return 0;
}
