/**
 * @file z80dis_min.h
 * @brief Kompakter Ein-Instruktions-Z80-Disassembler (header-only) für k1520dbg.
 *
 * Dekodiert *eine* Z80-Instruktion ab einer Adresse und liefert Länge, Mnemonik
 * und eine Klassifikation (CALL/RET/JP/Block-Repeat) plus — falls vorhanden — die
 * absolute Zieladresse eines Sprung-/Call-Operanden. Damit baut der Debugger
 * `step-over` (n), `step-out` (fin), Inline-Disassembly an jedem Stopp und die
 * `u`-Auflistung *ohne* externen Python-Aufruf.
 *
 * Bewusst kompakt gehalten (Octal-Dekodierung x/y/z/p/q nach z80.info). Der
 * undokumentierte IXH/IXL-Sonderfall in kombinierten (IX+d)-Befehlen wird
 * kosmetisch vereinfacht; die *Länge* ist in allen Fällen korrekt (entscheidend
 * für step-over). Für vollständige Listings bleibt tools/z80_disasm2.py kanonisch.
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

namespace z80dis {

struct Insn {
    int       len = 1;          ///< Instruktionslänge in Bytes
    char      text[48] = {0};   ///< Mnemonik (Operanden als Hex)
    bool      is_call   = false;///< CALL / CALL cc / RST  → step-over setzt BP bei PC+len
    bool      is_ret    = false;///< RET / RET cc / RETI / RETN
    bool      is_jump   = false;///< JP / JR / DJNZ (jede Form)
    bool      is_repeat = false;///< auto-repetierende ED-Blockbefehle (LDIR/CPIR/INIR/OTIR …)
    bool      has_target = false;///< 'target' enthält eine absolute Zieladresse
    uint16_t  target = 0;       ///< absolute Zieladresse (CALL/JP/JR/RST), falls has_target
};

/** Dekodiert eine Instruktion. rd(addr) liest ein Byte (über den Bus). */
inline Insn decode(const std::function<uint8_t(uint16_t)>& rd, uint16_t pc0) {
    Insn ins;
    uint16_t pc = pc0;
    auto fetch  = [&]() -> uint8_t { return rd(pc++); };
    auto imm16  = [&]() -> uint16_t { uint8_t lo = fetch(), hi = fetch(); return (uint16_t)(lo | (hi << 8)); };

    static const char* R[8]   = {"B","C","D","E","H","L","(HL)","A"};
    static const char* RP[4]  = {"BC","DE","HL","SP"};
    static const char* RP2[4] = {"BC","DE","HL","AF"};
    static const char* CC[8]  = {"NZ","Z","NC","C","PO","PE","P","M"};
    static const char* ALU[8] = {"ADD A,","ADC A,","SUB ","SBC A,","AND ","XOR ","OR ","CP "};
    static const char* ROT[8] = {"RLC ","RRC ","RL ","RR ","SLA ","SRA ","SLL ","SRL "};

    const char* idx = nullptr;            // "IX"/"IY" bei aktivem DD/FD-Präfix
    uint8_t op = fetch();
    while (op == 0xDD || op == 0xFD) { idx = (op == 0xDD) ? "IX" : "IY"; op = fetch(); }

    char buf[48];

    // Liefert den 8-Bit-Registernamen für Index i; liest bei (HL)+idx den Displacement.
    auto regname = [&](int i, char* out) {
        if (idx && i == 6) { int8_t d = (int8_t)fetch();
            snprintf(out, 16, "(%s%c%02XH)", idx, d < 0 ? '-' : '+', d < 0 ? -d : d); }
        else if (idx && i == 4) snprintf(out, 16, "%sH", idx);
        else if (idx && i == 5) snprintf(out, 16, "%sL", idx);
        else strncpy(out, R[i], 16);
    };
    auto rpname = [&](const char* const* tbl, int i) -> const char* {
        if (idx && tbl[i][0] == 'H' && tbl[i][1] == 'L') return idx; return tbl[i]; };

    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;

    if (op == 0xCB) {
        // ── CB-Präfix (bzw. DDCB/FDCB) ───────────────────────────────────────
        char rn[16];
        if (idx) { int8_t d = (int8_t)fetch(); uint8_t cb = fetch();
            x = cb >> 6; y = (cb >> 3) & 7; z = cb & 7;
            snprintf(rn, sizeof rn, "(%s%c%02XH)", idx, d < 0 ? '-' : '+', d < 0 ? -d : d);
        } else { uint8_t cb = fetch(); x = cb >> 6; y = (cb >> 3) & 7; z = cb & 7;
            strncpy(rn, R[z], sizeof rn); }
        switch (x) {
            case 0: snprintf(buf, sizeof buf, "%s%s", ROT[y], rn); break;
            case 1: snprintf(buf, sizeof buf, "BIT %d,%s", y, rn); break;
            case 2: snprintf(buf, sizeof buf, "RES %d,%s", y, rn); break;
            default:snprintf(buf, sizeof buf, "SET %d,%s", y, rn); break;
        }
    } else if (op == 0xED) {
        // ── ED-Präfix ────────────────────────────────────────────────────────
        uint8_t e = fetch(); x = e >> 6; y = (e >> 3) & 7; z = e & 7; p = y >> 1; q = y & 1;
        idx = nullptr; // ED hebt Index-Wirkung auf
        if (x == 1) {
            switch (z) {
                case 0: if (y == 6) strcpy(buf, "IN (C)"); else snprintf(buf, sizeof buf, "IN %s,(C)", R[y]); break;
                case 1: if (y == 6) strcpy(buf, "OUT (C),0"); else snprintf(buf, sizeof buf, "OUT (C),%s", R[y]); break;
                case 2: snprintf(buf, sizeof buf, "%s HL,%s", q ? "ADC" : "SBC", RP[p]); break;
                case 3: { uint16_t nn = imm16();
                    if (q) snprintf(buf, sizeof buf, "LD %s,(%04XH)", RP[p], nn);
                    else   snprintf(buf, sizeof buf, "LD (%04XH),%s", nn, RP[p]); break; }
                case 4: strcpy(buf, "NEG"); break;
                case 5: strcpy(buf, y == 1 ? "RETI" : "RETN"); ins.is_ret = true; break;
                case 6: { static const int im[8] = {0,0,1,2,0,0,1,2}; snprintf(buf, sizeof buf, "IM %d", im[y]); break; }
                default: { static const char* m[8] = {"LD I,A","LD R,A","LD A,I","LD A,R","RRD","RLD","NOP","NOP"};
                          strcpy(buf, m[y]); break; }
            }
        } else if (x == 2 && z <= 3 && y >= 4) {
            static const char* bli[4][4] = {
                {"LDI","CPI","INI","OUTI"}, {"LDD","CPD","IND","OUTD"},
                {"LDIR","CPIR","INIR","OTIR"}, {"LDDR","CPDR","INDR","OTDR"} };
            strcpy(buf, bli[y-4][z]);
            if (y >= 6) ins.is_repeat = true;
        } else strcpy(buf, "NOP*"); // undefinierte ED-Opcodes wirken als NOP
    } else if (x == 0) {
        switch (z) {
            case 0:
                if (y == 0) strcpy(buf, "NOP");
                else if (y == 1) strcpy(buf, "EX AF,AF'");
                else if (y == 2) { int8_t d = (int8_t)fetch(); ins.is_jump = true; ins.has_target = true;
                    ins.target = (uint16_t)(pc + d); snprintf(buf, sizeof buf, "DJNZ %04XH", ins.target); }
                else if (y == 3) { int8_t d = (int8_t)fetch(); ins.is_jump = true; ins.has_target = true;
                    ins.target = (uint16_t)(pc + d); snprintf(buf, sizeof buf, "JR %04XH", ins.target); }
                else { int8_t d = (int8_t)fetch(); ins.is_jump = true; ins.has_target = true;
                    ins.target = (uint16_t)(pc + d); snprintf(buf, sizeof buf, "JR %s,%04XH", CC[y-4], ins.target); }
                break;
            case 1:
                if (q == 0) { uint16_t nn = imm16(); snprintf(buf, sizeof buf, "LD %s,%04XH", rpname(RP,p), nn); }
                else snprintf(buf, sizeof buf, "ADD %s,%s", idx ? idx : "HL", rpname(RP,p));
                break;
            case 2:
                if (q == 0) { if (p==0) strcpy(buf,"LD (BC),A"); else if(p==1) strcpy(buf,"LD (DE),A");
                    else if(p==2){ uint16_t nn=imm16(); snprintf(buf,sizeof buf,"LD (%04XH),%s",nn,idx?idx:"HL"); }
                    else { uint16_t nn=imm16(); snprintf(buf,sizeof buf,"LD (%04XH),A",nn); } }
                else { if (p==0) strcpy(buf,"LD A,(BC)"); else if(p==1) strcpy(buf,"LD A,(DE)");
                    else if(p==2){ uint16_t nn=imm16(); snprintf(buf,sizeof buf,"LD %s,(%04XH)",idx?idx:"HL",nn); }
                    else { uint16_t nn=imm16(); snprintf(buf,sizeof buf,"LD A,(%04XH)",nn); } }
                break;
            case 3: snprintf(buf, sizeof buf, "%s %s", q ? "DEC" : "INC", rpname(RP,p)); break;
            case 4: { char rn[16]; regname(y, rn); snprintf(buf, sizeof buf, "INC %s", rn); break; }
            case 5: { char rn[16]; regname(y, rn); snprintf(buf, sizeof buf, "DEC %s", rn); break; }
            case 6: { char rn[16]; regname(y, rn); uint8_t n = fetch(); snprintf(buf, sizeof buf, "LD %s,%02XH", rn, n); break; }
            default:{ static const char* m[8]={"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"}; strcpy(buf,m[y]); }
        }
    } else if (x == 1) {
        if (z == 6 && y == 6) strcpy(buf, "HALT");
        else { char dn[16], sn[16]; regname(y, dn); regname(z, sn); snprintf(buf, sizeof buf, "LD %s,%s", dn, sn); }
    } else if (x == 2) {
        char rn[16]; regname(z, rn); snprintf(buf, sizeof buf, "%s%s", ALU[y], rn);
    } else { // x == 3
        switch (z) {
            case 0: snprintf(buf, sizeof buf, "RET %s", CC[y]); ins.is_ret = true; break;
            case 1:
                if (q == 0) snprintf(buf, sizeof buf, "POP %s", rpname(RP2,p));
                else { if(p==0){strcpy(buf,"RET");ins.is_ret=true;} else if(p==1)strcpy(buf,"EXX");
                    else if(p==2){snprintf(buf,sizeof buf,"JP (%s)",idx?idx:"HL");ins.is_jump=true;}
                    else snprintf(buf,sizeof buf,"LD SP,%s",idx?idx:"HL"); }
                break;
            case 2: { uint16_t nn=imm16(); ins.is_jump=true; ins.has_target=true; ins.target=nn;
                snprintf(buf,sizeof buf,"JP %s,%04XH",CC[y],nn); break; }
            case 3:
                if (y==0){ uint16_t nn=imm16(); ins.is_jump=true; ins.has_target=true; ins.target=nn;
                    snprintf(buf,sizeof buf,"JP %04XH",nn); }
                else if (y==2){ uint8_t n=fetch(); snprintf(buf,sizeof buf,"OUT (%02XH),A",n); }
                else if (y==3){ uint8_t n=fetch(); snprintf(buf,sizeof buf,"IN A,(%02XH)",n); }
                else if (y==4) snprintf(buf,sizeof buf,"EX (SP),%s",idx?idx:"HL");
                else if (y==5) strcpy(buf,"EX DE,HL");
                else if (y==6) strcpy(buf,"DI");
                else strcpy(buf,"EI");
                break;
            case 4: { uint16_t nn=imm16(); ins.is_call=true; ins.has_target=true; ins.target=nn;
                snprintf(buf,sizeof buf,"CALL %s,%04XH",CC[y],nn); break; }
            case 5:
                if (q==0) snprintf(buf,sizeof buf,"PUSH %s",rpname(RP2,p));
                else { uint16_t nn=imm16(); ins.is_call=true; ins.has_target=true; ins.target=nn;
                    snprintf(buf,sizeof buf,"CALL %04XH",nn); }
                break;
            case 6: { uint8_t n=fetch(); snprintf(buf,sizeof buf,"%s%02XH",ALU[y],n); break; }
            default:{ ins.is_call=true; ins.has_target=true; ins.target=(uint16_t)(y*8);
                snprintf(buf,sizeof buf,"RST %02XH",y*8); }
        }
    }

    strncpy(ins.text, buf, sizeof(ins.text) - 1);
    ins.len = (int)(uint16_t)(pc - pc0);
    if (ins.len == 0) ins.len = 1;
    return ins;
}

} // namespace z80dis
