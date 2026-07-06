#!/usr/bin/env python3
"""
bdos_analyze.py — Mini-BDOS-Disassembler fuer CPA780-SYL-Bootloader
=====================================================================

Disassembliert den Mini-BDOS-Bereich (0148H-0CFFH) des Bootloaders
und erzeugt eine separate, kommentierte M80-Assembler-Datei.

Verwendet den Z80-Disassembler aus bootsec_analyze.py als Basis,
erweitert um CB-Praefix-Unterstuetzung (BIT/SET/RES/RL/RR/...).

Eingabe:  prebuilt/bc_a5120/bootsec.bin (15104 Bytes)
Ausgabe:  bootsec/src/mini_bdos.mac (M80-Assembler, UTF-8)

Copyright (c) 2026 Olaf Krieger
SPDX-License-Identifier: MIT
"""

import os
import sys

# ─── Pfade ────────────────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BOOTSEC_BIN = os.path.join(PROJECT_DIR, 'prebuilt', 'bc_a5120', 'bootsec.bin')
OUTPUT_FILE = os.path.join(SCRIPT_DIR, 'src', 'mini_bdos.mac')

# ─── Binaerdaten einlesen ─────────────────────────────────────────────────────
data = open(BOOTSEC_BIN, 'rb').read()
assert len(data) == 15104, f"Unerwartete Dateigroesse: {len(data)}"


# ─── Vorzeichenbehaftetes Byte ────────────────────────────────────────────────
def s8(b):
    return b - 256 if b >= 128 else b


# ─── Z80-Disassembler (erweitert um CB-Praefix) ──────────────────────────────
def z80dis(d, pc):
    """Erweiterter Z80-Disassembler mit CB-Praefix-Unterstuetzung.

    Gegenueber bootsec_analyze.py zusaetzlich implementiert:
      - CB-Praefix (komplett): RLC/RRC/RL/RR/SLA/SRA/SRL/BIT/SET/RES
      - SBC A,n (0xDE)
      - RET C (0xD8), RET PE (0xE8), RET M (0xF8)
      - CALL PO/PE/P/M (0xE4/EC/F4/FC)
      - RST 08H..30H (0xCF/D7/DF/E7/EF/F7)
      - DDCB/FDCB-Praefix (indizierte Bit-Operationen)
    """
    if not d:
        return 'DB 00H', 1

    b = d[0]

    def b1(): return d[1] if len(d) > 1 else 0
    def b2(): return d[2] if len(d) > 2 else 0
    def w():  return b1() | (b2() << 8)
    def r():  return pc + 2 + s8(b1())

    rp  = ['BC', 'DE', 'HL', 'SP']
    rp2 = ['BC', 'DE', 'HL', 'AF']
    r8  = ['B', 'C', 'D', 'E', 'H', 'L', '(HL)', 'A']
    cc  = ['NZ', 'Z', 'NC', 'C', 'PO', 'PE', 'P', 'M']
    alu = ['ADD A,', 'ADC A,', 'SUB ', 'SBC A,',
           'AND ', 'XOR ', 'OR ', 'CP ']
    rot = ['RLCA', 'RRCA', 'RLA', 'RRA', 'DAA', 'CPL', 'SCF', 'CCF']

    # CB-Praefix-Operationsnamen
    cb_ops = ['RLC', 'RRC', 'RL', 'RR', 'SLA', 'SRA', 'SLL', 'SRL']
    cb_bit = ['BIT', 'RES', 'SET']

    # ED-Praefix
    blk = {
        0xA0: 'LDI',  0xA1: 'CPI',  0xA2: 'INI',  0xA3: 'OUTI',
        0xA8: 'LDD',  0xA9: 'CPD',  0xAA: 'IND',  0xAB: 'OUTD',
        0xB0: 'LDIR', 0xB1: 'CPIR', 0xB2: 'INIR', 0xB3: 'OTIR',
        0xB8: 'LDDR', 0xB9: 'CPDR', 0xBA: 'INDR', 0xBB: 'OTDR',
        0x44: 'NEG',  0x45: 'RETN', 0x46: 'IM 0', 0x47: 'LD I,A',
        0x4D: 'RETI', 0x4F: 'LD R,A', 0x56: 'IM 1', 0x57: 'LD A,I',
        0x5E: 'IM 2', 0x5F: 'LD A,R', 0x67: 'RRD',  0x6F: 'RLD',
        0x42: 'SBC HL,BC', 0x52: 'SBC HL,DE',
        0x62: 'SBC HL,HL', 0x72: 'SBC HL,SP',
        0x4A: 'ADC HL,BC', 0x5A: 'ADC HL,DE',
        0x6A: 'ADC HL,HL', 0x7A: 'ADC HL,SP',
    }

    # ── CB-Praefix ────────────────────────────────────────────────────────────
    if b == 0xCB:
        if len(d) < 2:
            return f'DB {b:02X}H', 1
        b2v = d[1]
        reg = r8[b2v & 7]
        if b2v < 0x40:
            # Rotation/Shift: RLC..SRL
            op = cb_ops[(b2v >> 3) & 7]
            return f'{op} {reg}', 2
        else:
            # BIT/RES/SET
            bit_num = (b2v >> 3) & 7
            if b2v < 0x80:
                return f'BIT {bit_num},{reg}', 2
            elif b2v < 0xC0:
                return f'RES {bit_num},{reg}', 2
            else:
                return f'SET {bit_num},{reg}', 2

    # ── Befehle 0x00..0x3F ────────────────────────────────────────────────────
    if b == 0x00: return 'NOP', 1
    if b == 0x01: return f'LD BC,{w():04X}H', 3
    if b == 0x02: return 'LD (BC),A', 1
    if b == 0x03: return 'INC BC', 1
    if b == 0x04: return 'INC B', 1
    if b == 0x05: return 'DEC B', 1
    if b == 0x06: return f'LD B,{b1():02X}H', 2
    if b == 0x07: return 'RLCA', 1
    if b == 0x08: return "EX AF,AF'", 1
    if b == 0x09: return 'ADD HL,BC', 1
    if b == 0x0A: return 'LD A,(BC)', 1
    if b == 0x0B: return 'DEC BC', 1
    if b == 0x0C: return 'INC C', 1
    if b == 0x0D: return 'DEC C', 1
    if b == 0x0E: return f'LD C,{b1():02X}H', 2
    if b == 0x0F: return 'RRCA', 1
    if b == 0x10: return f'DJNZ {r():04X}H', 2
    if b == 0x11: return f'LD DE,{w():04X}H', 3
    if b == 0x12: return 'LD (DE),A', 1
    if b == 0x13: return 'INC DE', 1
    if b == 0x14: return 'INC D', 1
    if b == 0x15: return 'DEC D', 1
    if b == 0x16: return f'LD D,{b1():02X}H', 2
    if b == 0x17: return 'RLA', 1
    if b == 0x18: return f'JR {r():04X}H', 2
    if b == 0x19: return 'ADD HL,DE', 1
    if b == 0x1A: return 'LD A,(DE)', 1
    if b == 0x1B: return 'DEC DE', 1
    if b == 0x1C: return 'INC E', 1
    if b == 0x1D: return 'DEC E', 1
    if b == 0x1E: return f'LD E,{b1():02X}H', 2
    if b == 0x1F: return 'RRA', 1
    if b == 0x20: return f'JR NZ,{r():04X}H', 2
    if b == 0x21: return f'LD HL,{w():04X}H', 3
    if b == 0x22: return f'LD ({w():04X}H),HL', 3
    if b == 0x23: return 'INC HL', 1
    if b == 0x24: return 'INC H', 1
    if b == 0x25: return 'DEC H', 1
    if b == 0x26: return f'LD H,{b1():02X}H', 2
    if b == 0x27: return 'DAA', 1
    if b == 0x28: return f'JR Z,{r():04X}H', 2
    if b == 0x29: return 'ADD HL,HL', 1
    if b == 0x2A: return f'LD HL,({w():04X}H)', 3
    if b == 0x2B: return 'DEC HL', 1
    if b == 0x2C: return 'INC L', 1
    if b == 0x2D: return 'DEC L', 1
    if b == 0x2E: return f'LD L,{b1():02X}H', 2
    if b == 0x2F: return 'CPL', 1
    if b == 0x30: return f'JR NC,{r():04X}H', 2
    if b == 0x31: return f'LD SP,{w():04X}H', 3
    if b == 0x32: return f'LD ({w():04X}H),A', 3
    if b == 0x33: return 'INC SP', 1
    if b == 0x34: return 'INC (HL)', 1
    if b == 0x35: return 'DEC (HL)', 1
    if b == 0x36: return f'LD (HL),{b1():02X}H', 2
    if b == 0x37: return 'SCF', 1
    if b == 0x38: return f'JR C,{r():04X}H', 2
    if b == 0x39: return 'ADD HL,SP', 1
    if b == 0x3A: return f'LD A,({w():04X}H)', 3
    if b == 0x3B: return 'DEC SP', 1
    if b == 0x3C: return 'INC A', 1
    if b == 0x3D: return 'DEC A', 1
    if b == 0x3E: return f'LD A,{b1():02X}H', 2
    if b == 0x3F: return 'CCF', 1

    # ── LD r,r' / HALT (0x40..0x7F) ──────────────────────────────────────────
    if 0x40 <= b <= 0x7F:
        if b == 0x76: return 'HALT', 1
        dst, src = (b >> 3) & 7, b & 7
        return f'LD {r8[dst]},{r8[src]}', 1

    # ── ALU r (0x80..0xBF) ────────────────────────────────────────────────────
    if 0x80 <= b <= 0xBF:
        op, src = (b >> 3) & 7, b & 7
        return f'{alu[op]}{r8[src]}', 1

    # ── 0xC0..0xFF ────────────────────────────────────────────────────────────
    if b == 0xC0: return 'RET NZ', 1
    if b == 0xC1: return 'POP BC', 1
    if b == 0xC2: return f'JP NZ,{w():04X}H', 3
    if b == 0xC3: return f'JP {w():04X}H', 3
    if b == 0xC4: return f'CALL NZ,{w():04X}H', 3
    if b == 0xC5: return 'PUSH BC', 1
    if b == 0xC6: return f'ADD A,{b1():02X}H', 2
    if b == 0xC7: return 'RST 00H', 1
    if b == 0xC8: return 'RET Z', 1
    if b == 0xC9: return 'RET', 1
    if b == 0xCA: return f'JP Z,{w():04X}H', 3
    if b == 0xCC: return f'CALL Z,{w():04X}H', 3
    if b == 0xCD: return f'CALL {w():04X}H', 3
    if b == 0xCE: return f'ADC A,{b1():02X}H', 2
    if b == 0xCF: return 'RST 08H', 1
    if b == 0xD0: return 'RET NC', 1
    if b == 0xD1: return 'POP DE', 1
    if b == 0xD2: return f'JP NC,{w():04X}H', 3
    if b == 0xD3: return f'OUT ({b1():02X}H),A', 2
    if b == 0xD4: return f'CALL NC,{w():04X}H', 3
    if b == 0xD5: return 'PUSH DE', 1
    if b == 0xD6: return f'SUB {b1():02X}H', 2
    if b == 0xD7: return 'RST 10H', 1
    if b == 0xD8: return 'RET C', 1
    if b == 0xD9: return 'EXX', 1
    if b == 0xDA: return f'JP C,{w():04X}H', 3
    if b == 0xDB: return f'IN A,({b1():02X}H)', 2
    if b == 0xDC: return f'CALL C,{w():04X}H', 3
    if b == 0xDE: return f'SBC A,{b1():02X}H', 2
    if b == 0xDF: return 'RST 18H', 1
    if b == 0xE0: return 'RET PO', 1
    if b == 0xE1: return 'POP HL', 1
    if b == 0xE2: return f'JP PO,{w():04X}H', 3
    if b == 0xE3: return 'EX (SP),HL', 1
    if b == 0xE4: return f'CALL PO,{w():04X}H', 3
    if b == 0xE5: return 'PUSH HL', 1
    if b == 0xE6: return f'AND {b1():02X}H', 2
    if b == 0xE7: return 'RST 20H', 1
    if b == 0xE8: return 'RET PE', 1
    if b == 0xE9: return 'JP (HL)', 1
    if b == 0xEA: return f'JP PE,{w():04X}H', 3
    if b == 0xEB: return 'EX DE,HL', 1
    if b == 0xEC: return f'CALL PE,{w():04X}H', 3
    if b == 0xED:
        b2v = b1()
        # 16-Bit-Speichertransfers (4 Byte)
        ed_mem = {
            0x43: f'LD ({w():04X}H),BC', 0x53: f'LD ({w():04X}H),DE',
            0x63: f'LD ({w():04X}H),HL', 0x73: f'LD ({w():04X}H),SP',
            0x4B: f'LD BC,({w():04X}H)', 0x5B: f'LD DE,({w():04X}H)',
            0x6B: f'LD HL,({w():04X}H)', 0x7B: f'LD SP,({w():04X}H)',
        }
        if b2v in ed_mem:
            return ed_mem[b2v], 4
        m = blk.get(b2v, f'DB 0EDH,{b2v:02X}H')
        return m, 2
    if b == 0xEE: return f'XOR {b1():02X}H', 2
    if b == 0xEF: return 'RST 28H', 1
    if b == 0xF0: return 'RET P', 1
    if b == 0xF1: return 'POP AF', 1
    if b == 0xF2: return f'JP P,{w():04X}H', 3
    if b == 0xF3: return 'DI', 1
    if b == 0xF4: return f'CALL P,{w():04X}H', 3
    if b == 0xF5: return 'PUSH AF', 1
    if b == 0xF6: return f'OR {b1():02X}H', 2
    if b == 0xF7: return 'RST 30H', 1
    if b == 0xF8: return 'RET M', 1
    if b == 0xF9: return 'LD SP,HL', 1
    if b == 0xFA: return f'JP M,{w():04X}H', 3
    if b == 0xFB: return 'EI', 1
    if b == 0xFC: return f'CALL M,{w():04X}H', 3
    if b == 0xFE: return f'CP {b1():02X}H', 2
    if b == 0xFF: return 'RST 38H', 1

    # ── DD/FD-Praefix: IX/IY-Register ────────────────────────────────────────
    if b in (0xDD, 0xFD):
        pfx = 'IX' if b == 0xDD else 'IY'
        if len(d) < 2:
            return f'DB {b:02X}H', 1
        b2v = d[1]
        d2  = b2()
        d3  = d[3] if len(d) > 3 else 0
        disp = s8(d2)
        nn = d2 | (d3 << 8)

        # DDCB/FDCB-Praefix: indizierte Bit-Operationen (4 Byte)
        if b2v == 0xCB:
            if len(d) < 4:
                return f'DB {b:02X}H,{b2v:02X}H', 2
            disp_cb = s8(d2)
            op_cb = d3
            reg_name = f'({pfx}{disp_cb:+d})'
            if op_cb < 0x40:
                op_name = cb_ops[(op_cb >> 3) & 7]
                return f'{op_name} {reg_name}', 4
            elif op_cb < 0x80:
                bit_num = (op_cb >> 3) & 7
                return f'BIT {bit_num},{reg_name}', 4
            elif op_cb < 0xC0:
                bit_num = (op_cb >> 3) & 7
                return f'RES {bit_num},{reg_name}', 4
            else:
                bit_num = (op_cb >> 3) & 7
                return f'SET {bit_num},{reg_name}', 4

        if b2v == 0x21: return f'LD {pfx},{nn:04X}H', 4
        if b2v == 0x22: return f'LD ({nn:04X}H),{pfx}', 4
        if b2v == 0x2A: return f'LD {pfx},({nn:04X}H)', 4
        if b2v == 0x36: return f'LD ({pfx}{disp:+d}),{d3:02X}H', 4
        if b2v == 0xE1: return f'POP {pfx}', 2
        if b2v == 0xE5: return f'PUSH {pfx}', 2
        if b2v == 0xE3: return f'EX (SP),{pfx}', 2
        if b2v == 0xE9: return f'JP ({pfx})', 2
        if b2v == 0x23: return f'INC {pfx}', 2
        if b2v == 0x2B: return f'DEC {pfx}', 2
        if b2v == 0x09: return f'ADD {pfx},BC', 2
        if b2v == 0x19: return f'ADD {pfx},DE', 2
        if b2v == 0x29: return f'ADD {pfx},{pfx}', 2
        if b2v == 0x39: return f'ADD {pfx},SP', 2
        if b2v == 0xF9: return f'LD SP,{pfx}', 2
        # LD r,(IX/IY+d)
        if 0x46 <= b2v <= 0x7E and b2v & 7 == 6:
            dst = (b2v >> 3) & 7
            return f'LD {r8[dst]},({pfx}{disp:+d})', 3
        # LD (IX/IY+d),r
        if 0x70 <= b2v <= 0x77 and b2v & 7 != 6:
            return f'LD ({pfx}{disp:+d}),{r8[b2v & 7]}', 3
        # ADD/ADC/SUB/SBC/AND/XOR/OR/CP (IX/IY+d)
        if 0x86 <= b2v <= 0xBE and b2v & 7 == 6:
            op = (b2v >> 3) & 7
            return f'{alu[op]}({pfx}{disp:+d})', 3
        if b2v == 0x34: return f'INC ({pfx}{disp:+d})', 3
        if b2v == 0x35: return f'DEC ({pfx}{disp:+d})', 3
        return f'DB {b:02X}H,{b2v:02X}H', 2

    return f'DB {b:02X}H', 1


# ─── Disassembler-Schleife ────────────────────────────────────────────────────

def disasm_range(raw, ram_org):
    """Disassembliert Bytepuffer ab ram_org.
    Rueckgabe: Liste von (ram_addr, raw_bytes, mnemonic)
    """
    rows = []
    i = 0
    while i < len(raw):
        ram = ram_org + i
        chunk = raw[i:]
        mnem, ln = z80dis(chunk, ram)
        raw_b = bytes(raw[i:i + ln])
        rows.append((ram, raw_b, mnem))
        i += ln
    return rows


# ─── Bekannte Adressen und Labels ────────────────────────────────────────────

LABELS = {
    # --- Boot-Code (0080H-00FFH, nicht Teil des BDOS-Moduls) ---
    0x0080: 'WBOOT_JP',
    0x0083: 'LOAD_OS',
    0x00A3: 'LOAD_ADDR',
    0x00A6: 'LOAD_LOOP',
    0x00AF: 'LOAD_ERR',
    0x00B5: 'READ_FILE',
    0x00CC: 'FCB_OS',
    # --- BDOS-Modul-Kopf (0100H-0110H, CP/M 2.2 Struktur) ---
    0x0106: 'BDOS_ENTRY',       # JMP bdose
    0x0109: 'PERERR',           # DW persub  (permanent error handler)
    # --- bdose: BDOS-Einstiegspunkt (0111H) ---
    0x0111: 'BDOSE',            # CP/M 2.2: arrive here from user programs
    0x012B: 'BDOSE_RESEL',      # sta resel (alternate entry)
    0x012E: 'BDOSE_DISP',       # Push goback, check func#, dispatch
    0x0140: 'BDOSE_PCHL',       # Load target addr, JP (HL)
    # --- functab: BDOS-Funktionstabelle (0147H, 41 DW-Eintraege) ---
    0x0147: 'FUNCTAB',
    # --- Fehlerbehandlung (0198H-01B9H, CP/M 2.2 error handlers) ---
    0x0198: 'PERSUB',           # CP/M 2.2: report permanent error
    0x01A5: 'SELSUB',           # CP/M 2.2: report select error
    0x01AB: 'RODSUB',           # CP/M 2.2: report R/O disk error
    0x01B1: 'ROFSUB',           # CP/M 2.2: report R/O file error
    0x01B4: 'WAIT_ERR',         # CP/M 2.2: wait for response, reboot
    # --- Fehlermeldungen (01BAH-01E4H) ---
    0x01BA: 'DSKMSG',           # 'Bdos Err On  : $'
    0x01CA: 'PERMSG',           # 'Bad Sector$'
    0x01D5: 'SELMSG',           # 'Select$'
    0x01DC: 'ROFMSG',           # 'File ' (gefolgt von RODMSG)
    0x01E1: 'RODMSG',           # 'R/O$'
    # --- Console-I/O-Routinen (01E5H-0322H, CP/M 2.2) ---
    0x01E5: 'ERRFLG',           # CP/M 2.2: report error to console
    0x01FB: 'CONIN',            # CP/M 2.2: read console character
    0x0206: 'CONECH',           # CP/M 2.2: console input with echo
    0x0214: 'ECHOC',            # CP/M 2.2: echo char if graphic
    0x0223: 'CONBRK',           # CP/M 2.2: check for break
    0x0242: 'CONB0',            # save kbchar
    0x0245: 'CONB1',            # return true
    0x0248: 'CONOUT',           # CP/M 2.2: console output
    0x0262: 'COMPOUT',          # CP/M 2.2: compute column position
    0x027F: 'CTLOUT',           # CP/M 2.2: control char output
    0x0290: 'TABOUT',           # CP/M 2.2: tab expansion
    0x0296: 'TAB0',             # tab fill loop
    0x02A4: 'BACKUP',           # CP/M 2.2: backspace
    0x02AC: 'PCTLH',            # send ctlh to console
    0x02B1: 'CRLFP',            # CP/M 2.2: CR/LF + reposition
    0x02B9: 'CRLFP0',           # column adjustment loop
    0x02C9: 'CRLF',             # CP/M 2.2: CR + LF
    0x02D3: 'PRINT',            # CP/M 2.2: print until '$'
    # --- Funktions-Handler (02E1H-0322H) ---
    0x02E1: 'FUNC1',            # CP/M func  1: console input with echo
    0x02E7: 'FUNC3',            # CP/M func  3: reader input
    0x02ED: 'FUNC6',            # CP/M func  6: direct console I/O
    0x02F9: 'DIRINP',           # CP/M 2.2: direct input
    0x0306: 'FUNC7',            # CP/M func  7: get IOBYTE
    0x030C: 'FUNC8',            # CP/M func  8: set IOBYTE
    0x0311: 'FUNC9',            # CP/M func  9: print string
    0x0317: 'FUNC11',           # CP/M func 11: console status
    0x031A: 'STA_RET',          # CP/M 2.2: sta aret; ret
    0x031D: 'FUNC_RET',         # CP/M 2.2: ret (= goback)
    0x031E: 'SETLRET1',         # CP/M 2.2: set lret=1
    # --- BDOS Datenbereich (0323H-035FH) ---
    0x0323: 'COMPCOL',          # computing column flag
    0x0324: 'STRTCOL',          # start column for ctl-x
    0x0325: 'COLUMN',           # current column position
    0x0326: 'LISTCP',           # list copy flag
    0x0327: 'KBCHAR',           # keyboard char buffer
    0x0328: 'ENTSP',            # entry stack pointer
    0x032A: 'LSTACK_AREA',      # local stack (24 words = 48 bytes)
    0x035A: 'LSTACK',           # top of local stack
    # --- Gemeinsame BDOS-Variablen (035AH-035FH) ---
    0x035A: 'USRCODE',          # current user number
    0x035B: 'CURDSK',           # current disk number
    0x035C: 'INFO',             # information address (DE from caller)
    0x035E: 'ARET',             # return value (low byte = LRET)
    # --- BDOS Disk-Routinen (0360H-08BAH) ---
    0x0360: 'GOERR',            # CP/M 2.2: call error handler indirect
    0x0368: 'MOVE',             # CP/M 2.2: copy memory block
    0x0372: 'SELECTDISK',       # CP/M 2.2: select disk + fill DPB
    0x03BA: 'HOME',             # CP/M 2.2: home disk to track 0
    0x03CB: 'RDBUFF',           # CP/M 2.2: read buffer
    0x03D1: 'WRBUFF',           # CP/M 2.2: write buffer
    0x03DC: 'SEEK',             # CP/M 2.2: seek to track/sector
    0x0457: 'DM_POSITION',      # CP/M 2.2: disk map position
    0x0477: 'GETDM',            # CP/M 2.2: get disk map block
    0x0490: 'INDEX',            # CP/M 2.2: compute block from vrecord
    0x049D: 'ALLOCATED',        # CP/M 2.2: check block allocated
    0x04A3: 'ATRAN',            # CP/M 2.2: translate block to record
    0x04BF: 'GETEXTA',          # CP/M 2.2: get extent addr
    0x04C7: 'GETFCBA',          # CP/M 2.2: get reccnt/nxtrec addr
    0x04D4: 'GETFCB',           # CP/M 2.2: load FCB params
    0x04EB: 'SETFCB',           # CP/M 2.2: store params to FCB
    0x0503: 'HLROTR',           # CP/M 2.2: rotate HL right
    0x0510: 'COMPUTE_CS',       # CP/M 2.2: compute checksum
    0x051D: 'HLROTL',           # CP/M 2.2: rotate HL left
    0x0524: 'SET_CDISK',        # CP/M 2.2: set disk bit in vector
    0x0537: 'GETALLOCBIT',      # CP/M 2.2: get allocation bit
    0x0545: 'SETALLOCBIT',      # CP/M 2.2: set allocation bit
    0x055D: 'CHECK_RODIR',      # CP/M 2.2: check dir entry R/O
    0x056D: 'CHECK_WRITE',      # CP/M 2.2: check write-protected
    0x0577: 'SETDIR',           # CP/M 2.2: set DMA to dir buffer
    0x0582: 'GETMODNUM',        # CP/M 2.2: get module number
    0x058B: 'CLRMODNUM',        # CP/M 2.2: clear module number
    0x0591: 'SETFWF',           # CP/M 2.2: set file write flag
    0x0598: 'COMPCDR',          # CP/M 2.2: compare dir counters
    0x05A5: 'SETCDR',           # CP/M 2.2: set current dir max
    0x05AE: 'SUBDH',            # CP/M 2.2: subtract HL from DE
    0x05B5: 'GET_BLOCK',        # CP/M 2.2: find free alloc block
    0x05DF: 'SCANDM',           # CP/M 2.2: scan disk map
    0x05ED: 'SETDATA',          # CP/M 2.2: set DMA to data buffer
    0x05F9: 'RD_DIR',           # CP/M 2.2: read directory record
    0x0602: 'WRDIR',            # CP/M 2.2: write dir + checksum
    0x0605: 'COPY_FCB',         # CP/M 2.2: copy FCB to directory
    0x060E: 'END_OF_DIR',       # CP/M 2.2: check end of directory
    0x0617: 'SET_END_DIR',      # CP/M 2.2: mark end of directory
    0x061E: 'READ_DIR',         # CP/M 2.2: read next dir entry
    0x0632: 'SEEKDIR',          # CP/M 2.2: seek to dir record
    0x064E: 'COMPEXT',          # CP/M 2.2: compare extent numbers
    0x0675: 'ROTR',             # CP/M 2.2: rotate byte back
    0x0684: 'SCANDM_LOOP',      # scan disk map loop
    0x06BC: 'INITIALIZE',       # CP/M 2.2: initialize disk
    0x0731: 'SEARCH',           # CP/M 2.2: search first
    0x07B5: 'DELETE',           # CP/M 2.2: delete file
    0x07D7: 'GETBLOCK_SUB',     # CP/M 2.2: get block subroutine
    0x0800: 'READ_BUFFER',      # CP/M 2.2: read buffer area
    0x0816: 'OPEN_COPY',        # CP/M 2.2: open$copy
    0x082F: 'COPY_DIR',         # CP/M 2.2: copy FCB range to dir
    0x0854: 'SEEK_COPY',        # CP/M 2.2: seek and write dir
    0x086A: 'OPEN',             # CP/M 2.2: open existing file
    0x08AD: 'MERGEZERO',        # CP/M 2.2: merge zero entries
    0x08AF: 'MERGEZERO2',       # CP/M 2.2: merge zero (alt entry)
    0x08B5: 'OPEN_REEL',        # CP/M 2.2: open next extent
    0x08BB: 'CLOSE',            # CP/M 2.2: close file
    0x08CC: 'FCB_OS_RT',        # Runtime-FCB fuer @OS.COM
    0x08F3: 'DISKREAD_BODY',    # CP/M 2.2: disk read body
    0x0901: 'OPEN_REEL_2',      # open$reel continuation
    0x0911: 'COMPEXT_MAIN',     # extent comparison main
    0x093D: 'MAKE',             # CP/M 2.2: create new file
    0x0973: 'SEQDISKREAD',      # CP/M 2.2: sequential disk read
    0x0999: 'OPEN_NEXT',        # open next extent handler
    0x09C5: 'DISKREAD_END',     # disk read end handler
    0x09CF: 'SEQDISKWRITE',     # CP/M 2.2: sequential disk write
    0x09D5: 'DISKWRITE_BODY',   # disk write body
    0x09E5: 'RSEEK',            # CP/M 2.2: random seek
    0x0A14: 'COMPUTE_RR',       # CP/M 2.2: compute random record
    0x0A44: 'GETFILESIZE',      # CP/M 2.2: compute file size
    0x0A80: 'SETRANDOM',        # CP/M 2.2: set random record field
    0x0A93: 'SELECT',           # CP/M 2.2: select disk
    0x0AB7: 'CURSELECT',        # CP/M 2.2: select from FCB
    0x0AC3: 'RESELECT',         # CP/M 2.2: reselect disk
    0x0AF0: 'FUNC12',           # CP/M func 12: return version 2.2
    0x0AF5: 'FUNC13',           # CP/M func 13: reset disk system
    0x0B12: 'FUNC14',           # CP/M func 14: select disk
    0x0B1B: 'FUNC15',           # CP/M func 15: open file
    0x0B21: 'FUNC16',           # CP/M func 16: close file
    0x0B2A: 'FUNC17',           # CP/M func 17: search first
    0x0B3E: 'FUNC18',           # CP/M func 18: search next
    0x0B4D: 'FUNC19',           # CP/M func 19: delete
    0x0B56: 'FUNC20',           # CP/M func 20: read sequential
    0x0B5C: 'FUNC21',           # CP/M func 21: write sequential
    0x0B65: 'FUNC22',           # CP/M func 22: make file
    0x0B6E: 'FUNC23',           # CP/M func 23: rename (=> login vect)
    0x0B74: 'FUNC24',           # CP/M func 24: return login vector
    0x0B7A: 'FUNC25',           # CP/M func 25: return current disk
    0x0B81: 'FUNC26',           # CP/M func 26: set DMA
    0x0B87: 'FUNC27',           # CP/M func 27: return alloc vector
    0x0B8D: 'FUNC28',           # CP/M func 28: write protect
    0x0B96: 'FUNC29',           # CP/M func 29: return R/O vector
    0x0B9D: 'FUNC30',           # CP/M func 30: set indicators
    0x0BB1: 'FUNC31',           # CP/M func 31: return DPB address
    0x0BB7: 'FUNC32',           # CP/M func 32: set/get user code
    0x0BFF: 'BDOS_WORKSPACE',   # BDOS Arbeitsbereich (Variablen)
    0x0C41: 'DISK_INIT_A',      # Disk-Initialisierung A
    0x0C49: 'DISK_INIT_B',      # Disk-Initialisierung B
    0x0C51: 'CHECK_RESEL',      # Check reselect
    0x0C5D: 'RESELECT_2',      # Reselect continuation        # save kbchar
    0x0245: 'CONB1',            # return true
    0x0248: 'CONOUT',           # CP/M 2.2: console output
    0x0262: 'COMPOUT',          # CP/M 2.2: compute column position
    0x027F: 'CTLOUT',           # CP/M 2.2: control char output
    0x0290: 'TABOUT',           # CP/M 2.2: tab expansion
    0x0296: 'TAB0',             # tab fill loop
    0x02A4: 'BACKUP',           # CP/M 2.2: backspace
    0x02AC: 'PCTLH',            # send ctlh to console
    0x02B1: 'CRLFP',            # CP/M 2.2: CR/LF + reposition
    0x02B9: 'CRLFP0',           # column adjustmen
    # --- Funktions-Handler (02E1H-0322H) ---
    0x02E1: 'FUNC1',            # CP/M func  1: console input with echo
    0x02E7: 'FUNC3',            # CP/M func  3: reader input
    0x02ED: 'FUNC6',            # CP/M func  6: direct console I/O
    0x02F9: 'DIRINP',           # CP/M 2.2: direct input
    0x0306: 'FUNC7',            # CP/M func  7: get IOBYTE
    0x030C: 'FUNC8',            # CP/M func  8: set IOBYTE
    0x0311: 'FUNC9',            # CP/M func  9: print string
    0x0317: 'FUNC11',           # CP/M func 11: console status
    0x031A: 'STA_RET',          # CP/M 2.2: sta aret; ret
    0x031D: 'FUNC_RET',         # CP/M 2.2: ret (= goback)
    0x031E: 'SETLRET1',         # CP/M 2.2: set lret=1
    # --- BDOS Datenbereich (0323H-035FH) ---
    0x0323: 'COMPCOL',          # computing column flag
    0x0324: 'STRTCOL',          # start column for ctl-x
    0x0325: 'COLUMN',           # current column position
    0x0326: 'LISTCP',           # list copy flag
    0x0327: 'KBCHAR',           # keyboard char buffer
    0x0328: 'ENTSP',            # entry stack pointer
    0x032A: 'LSTACK_AREA',      # local stack (24 words = 48 bytes)
    0x035A: 'LSTACK',           # top of local stack
    # --- Gemeinsame BDOS-Variablen (035AH-035FH) ---
    0x035A: 'USRCODE',          # current user number
    0x035B: 'CURDSK',           # current disk number
    0x035C: 'INFO',             # information address (DE from caller)
    0x035E: 'ARET',             # return value (low byte = LRET)
    # --- BDOS Disk-Routinen (0360H-08BAH) ---
    0x0360: 'GOERR',            # CP/M 2.2: call error handler indirect
    0x0368: 'MOVE',             # CP/M 2.2: copy memory block
    0x0372: 'SELECTDISK',       # CP/M 2.2: select disk + fill DPB
    0x03BA: 'HOME',             # CP/M 2.2: home disk to track 0
    0x03CB: 'RDBUFF',           # CP/M 2.2: read buffer
    0x03D1: 'WRBUFF',           # CP/M 2.2: write buffer
    0x03DC: 'SEEK',             # CP/M 2.2: seek to track/sector
    0x0457: 'DM_POSITION',      # CP/M 2.2: disk map position
    0x0477: 'GETDM',            # CP/M 2.2: get disk map block
    0x0490: 'INDEX',            # CP/M 2.2: compute block from vrecord
    0x049D: 'ALLOCATED',        # CP/M 2.2: check block allocated
    0x04A3: 'ATRAN',            # CP/M 2.2: translate block to record
    0x04BF: 'GETEXTA',          # CP/M 2.2: get extent addr
    0x04C7: 'GETFCBA',          # CP/M 2.2: get reccnt/nxtrec addr
    0x04D4: 'GETFCB',           # CP/M 2.2: load FCB params
    0x04EB: 'SETFCB',           # CP/M 2.2: store params to FCB
    0x0503: 'HLROTR',           # CP/M 2.2: rotate HL right
    0x0510: 'COMPUTE_CS',       # CP/M 2.2: compute checksum
    0x051D: 'HLROTL',           # CP/M 2.2: rotate HL left
    0x0524: 'SET_CDISK',        # CP/M 2.2: set disk bit in vector
    0x0537: 'GETALLOCBIT',      # CP/M 2.2: get allocation bit
    0x0545: 'SETALLOCBIT',      # CP/M 2.2: set allocation bit
    0x055D: 'CHECK_RODIR',      # CP/M 2.2: check dir entry R/O
    0x056D: 'CHECK_WRITE',      # CP/M 2.2: check write-protected
    0x0577: 'SETDIR',           # CP/M 2.2: set DMA to dir buffer
    0x0582: 'GETMODNUM',        # CP/M 2.2: get module number
    0x058B: 'CLRMODNUM',        # CP/M 2.2: clear module number
    0x0591: 'SETFWF',           # CP/M 2.2: set file write flag
    0x0598: 'COMPCDR',          # CP/M 2.2: compare dir counters
    0x05A5: 'SETCDR',           # CP/M 2.2: set current dir max
    0x05AE: 'SUBDH',            # CP/M 2.2: subtract HL from DE
    0x05B5: 'GET_BLOCK',        # CP/M 2.2: find free alloc block
    0x05DF: 'SCANDM',           # CP/M 2.2: scan disk map
    0x05ED: 'SETDATA',          # CP/M 2.2: set DMA to data buffer
    0x05F9: 'RD_DIR',           # CP/M 2.2: read directory record
    0x0602: 'WRDIR',            # CP/M 2.2: write dir + checksum
    0x0605: 'COPY_FCB',         # CP/M 2.2: copy FCB to directory
    0x060E: 'END_OF_DIR',       # CP/M 2.2: check end of directory
    0x0617: 'SET_END_DIR',      # CP/M 2.2: mark end of directory
    0x061E: 'READ_DIR',         # CP/M 2.2: read next dir entry
    0x0632: 'SEEKDIR',          # CP/M 2.2: seek to dir record
    0x064E: 'COMPEXT',          # CP/M 2.2: compare extent numbers
    0x0675: 'ROTR',             # CP/M 2.2: rotate byte back
    0x0684: 'SCANDM_LOOP',      # scan disk map loop
    0x06BC: 'INITIALIZE',       # CP/M 2.2: initialize disk
    0x0731: 'SEARCH',           # CP/M 2.2: search first
    0x07B5: 'DELETE',           # CP/M 2.2: delete file
    0x07D7: 'GETBLOCK_SUB',     # CP/M 2.2: get block subroutine
    0x0800: 'READ_BUFFER',      # CP/M 2.2: read buffer area
    0x0816: 'OPEN_COPY',        # CP/M 2.2: open$copy
    0x082F: 'COPY_DIR',         # CP/M 2.2: copy FCB range to dir
    0x0854: 'SEEK_COPY',        # CP/M 2.2: seek and write dir
    0x086A: 'OPEN',             # CP/M 2.2: open existing file
    0x08AD: 'MERGEZERO',        # CP/M 2.2: merge zero entries
    0x08AF: 'MERGEZERO2',       # CP/M 2.2: merge zero (alt entry)
    0x08B5: 'OPEN_REEL',        # CP/M 2.2: open next extent
    0x08BB: 'CLOSE',            # CP/M 2.2: close file
    0x08CC: 'FCB_OS_RT',        # Runtime-FCB fuer @OS.COM
    0x08F3: 'DISKREAD_BODY',    # CP/M 2.2: disk read body
    0x0901: 'OPEN_REEL_2',      # open$reel continuation
    0x0911: 'COMPEXT_MAIN',     # extent comparison main
    0x093D: 'MAKE',             # CP/M 2.2: create new file
    0x0973: 'SEQDISKREAD',      # CP/M 2.2: sequential disk read
    0x0999: 'OPEN_NEXT',        # open next extent handler
    0x09C5: 'DISKREAD_END',     # disk read end handler
    0x09CF: 'SEQDISKWRITE',     # CP/M 2.2: sequential disk write
    0x09D5: 'DISKWRITE_BODY',   # disk write body
    0x09E5: 'RSEEK',            # CP/M 2.2: random seek
    0x0A14: 'COMPUTE_RR',       # CP/M 2.2: compute random record
    0x0A44: 'GETFILESIZE',      # CP/M 2.2: compute file size
    0x0A80: 'SETRANDOM',        # CP/M 2.2: set random record field
    0x0A93: 'SELECT',           # CP/M 2.2: select disk
    0x0AB7: 'CURSELECT',        # CP/M 2.2: select from FCB
    0x0AC3: 'RESELECT',         # CP/M 2.2: reselect disk
    0x0AF0: 'FUNC12',           # CP/M func 12: return version 2.2
    0x0AF5: 'FUNC13',           # CP/M func 13: reset disk system
    0x0B12: 'FUNC14',           # CP/M func 14: select disk
    0x0B1B: 'FUNC15',           # CP/M func 15: open file
    0x0B21: 'FUNC16',           # CP/M func 16: close file
    0x0B2A: 'FUNC17',           # CP/M func 17: search first
    0x0B3E: 'FUNC18',           # CP/M func 18: search next
    0x0B4D: 'FUNC19',           # CP/M func 19: delete
    0x0B56: 'FUNC20',           # CP/M func 20: read sequential
    0x0B5C: 'FUNC21',           # CP/M func 21: write sequential
    0x0B65: 'FUNC22',           # CP/M func 22: make file
    0x0B6E: 'FUNC23',           # CP/M func 23: rename (=> login vect)
    0x0B74: 'FUNC24',           # CP/M func 24: return login vector
    0x0B7A: 'FUNC25',           # CP/M func 25: return current disk
    0x0B81: 'FUNC26',           # CP/M func 26: set DMA
    0x0B87: 'FUNC27',           # CP/M func 27: return alloc vector
    0x0B8D: 'FUNC28',           # CP/M func 28: write protect
    0x0B96: 'FUNC29',           # CP/M func 29: return R/O vector
    0x0B9D: 'FUNC30',           # CP/M func 30: set indicators
    0x0BB1: 'FUNC31',           # CP/M func 31: return DPB address
    0x0BB7: 'FUNC32',           # CP/M func 32: set/get user code
    0x0BFF: 'BDOS_WORKSPACE',   # BDOS Arbeitsbereich (Variablen)
    0x0C41: 'DISK_INIT_A',      # Disk-Initialisierung A
    0x0C49: 'DISK_INIT_B',      # Disk-Initialisierung B
    0x0C51: 'CHECK_RESEL',      # Check reselect
    0x0C5D: 'RESELECT_2',      # Reselect continuation
}

# Bereiche, die als Daten (nicht als Code) behandelt werden
# (start_addr, end_addr_exclusive, typ, description)
#  typ: 'db' = rohe Bytes, 'dw' = Word-Tabelle, 'fcb' = FCB
DATA_RANGES = [
    # FCB fuer @OS.COM (Vorlage)
    (0x00CC, 0x00F1, 'fcb', 'FCB fuer @OS.COM'),
    # Fehler-Handler-Zeiger (pererr/selerr/roderr/roferr)
    (0x0109, 0x0111, 'dw', 'Error-Handler-Zeiger (CP/M 2.2: pererr/selerr/roderr/roferr)'),
    # Funktions-Dispatch-Tabelle (41 Eintraege)
    (0x0147, 0x0199, 'dw', 'functab: BDOS-Dispatch-Tabelle (41 DW-Eintraege)'),
    # FCB fuer @OS.COM (Runtime-Kopie im Arbeitsbereich)
    (0x08CC, 0x08F1, 'fcb', 'FCB fuer @OS.COM (Runtime-Kopie)'),
]

# ASCII-String-Bereiche (inklusive $-Terminator)
STRING_RANGES = [
    (0x01BA, 0x01CA, 'Bdos Err On  : $'),
    (0x01CA, 0x01D5, 'Bad Sector$'),
    (0x01D5, 0x01DC, 'Select$'),
    (0x01DC, 0x01E5, 'File R/O$'),
]

# Null-Padding-Bereiche
PADDING_RANGES = [
    (0x00F1, 0x0106, 'Padding'),
    (0x0CF9, 0x0D00, 'Padding (Ende Track 0)'),
]

# NOP-/Workspace-Bereiche (als DS ausgeben statt einzelner NOPs)
NOP_RANGES = [
    (0x0323, 0x0360, 'BDOS-Variablen + lokaler Stack (compcol..lstack)'),
    (0x0BFF, 0x0C41, 'BDOS-Arbeitsbereich (Variablen/Puffer)'),
    (0x0C6F, 0x0CF9, 'BDOS-Arbeitsbereich (Disk-Parameter / unbenutzt)'),
]

# ─── Inline-Kommentare fuer einzelne Adressen ────────────────────────────────
# Werden an die Disassembly-Zeile angehaengt.
# Format: {addr: "Kommentar-Text"}

COMMENTS = {
    # --- Boot-Code (nicht Teil des BDOS-Moduls) ---
    0x0080: 'Warmstart-Vektor -> BIOS CBOOT',
    0x0083: 'WBOOT: @OS.COM Laderoutine',
    0x0085: 'BDOS Func 32: Set User Code = 0',
    0x0087: '-> BDOS (0005H)',
    0x008A: 'BDOS Func 13: Reset Disk System',
    0x008C: '-> BDOS',
    0x008F: 'Hole aktuelle Disknummer aus 0004H',
    0x0093: 'BDOS Func 14: Select Disk',
    0x0095: '-> BDOS',
    0x0098: 'DE = FCB_OS (00CCH)',
    0x009B: 'BDOS Func 15: Open File',
    0x009D: '-> BDOS',
    0x00A0: 'FFH+1 = 0 -> Fehler?',
    0x00A1: 'ja -> LOAD_ERR',
    0x00A3: 'Ladeadresse = 3780H (@OS.COM Ziel)',
    0x00A6: 'Naechsten Record lesen',
    0x00A9: 'Schleife solange A=0 (kein EOF)',
    0x00AB: 'A=1 -> EOF (erfolgreich)',
    0x00AC: 'Springe zu geladenem @OS.COM',
    0x00AF: 'Fehlercode 88H (LED-Muster)',
    0x00B1: '-> FDC Status-Port',
    0x00B3: 'Interrupts sperren',
    0x00B4: 'System stoppen',
    0x00B5: 'DMA-Adresse retten',
    0x00B7: 'BDOS Func 26: Set DMA Address',
    0x00B9: '-> BDOS',
    0x00BC: 'DE = FCB_OS',
    0x00BF: 'BDOS Func 20: Read Sequential',
    0x00C1: '-> BDOS',
    0x00C4: 'A=0 -> Erfolg',
    0x00C7: 'DMA += 128 Bytes',
    # --- BDOS-Modul (ab 0100H, CP/M 2.2 Struktur) ---
    0x0106: 'JMP bdose (CP/M 2.2 Standard-Einstieg)',
    # --- bdose (0111H) ---
    0x0111: 'xchg (DE<->HL)',
    0x0112: 'shld info ; info = Parameter-Adresse',
    0x0115: 'xchg',
    0x0116: 'mov a,e',
    0x0117: 'sta linfo ; low(info) merken',
    0x011A: 'lxi h,0',
    0x011D: 'shld aret ; Rueckgabewert = 0000H',
    0x0120: 'dad sp ; SP merken',
    0x0121: 'shld entsp',
    0x0124: 'lxi sp,lstack ; lokaler Stack',
    0x0127: 'xra a',
    0x0128: 'sta fcbdsk ; fcbdsk = false',
    0x012B: 'sta resel  ; resel = false',
    0x012E: 'lxi h,goback ; Ruecksprung-Adresse',
    0x0131: 'push h ; -> Stack (funktioniert wie jmp goback bei RET)',
    0x0132: 'mov a,c ; Funktionsnummer',
    0x0133: 'cpi nfuncs (41) ; gueltige Funktion?',
    0x0135: 'ret nc ; nein -> goback',
    0x0136: 'mov c,e ; Ausgabe-Zeichen nach C',
    0x0137: 'lxi h,functab ; Adresse der Dispatch-Tabelle',
    0x013A: 'mov e,a ; DE = Funktionsnummer',
    0x013B: 'mvi d,0',
    0x013D: 'dad d',
    0x013E: 'dad d ; HL = functab + func*2',
    0x013F: 'mov e,m ; Low-Byte der Zieladresse',
    0x0140: 'inx h',
    0x0141: 'mov d,m ; High-Byte der Zieladresse',
    0x0142: 'lhld info ; HL = info (Parameter)',
    0x0145: 'xchg ; HL = Zieladresse, DE = info',
    0x0146: 'pchl ; -> Funktions-Handler',
    # --- persub/selsub/rodsub/rofsub ---
    0x0198: 'lxi h,permsg',
    0x019C: 'call errflg',
    0x019F: 'cpi ctlc (03H)',
    0x01A1: 'jz reboot ; Ctrl-C -> Neustart',
    0x01A4: 'ret ; Fehler ignorieren',
    0x01A5: 'lxi h,selmsg',
    0x01A8: 'jmp wait$err',
    0x01AB: 'lxi h,rodmsg',
    0x01AE: 'jmp wait$err',
    0x01B1: 'lxi h,rofmsg',
    0x01B4: 'call errflg',
    0x01B7: 'jmp reboot ; Neustart nach R/O-Fehler',
    # --- errflg (01E5H) ---
    0x01E5: 'push h ; Fehlermeldung auf Stack',
    0x01E6: 'call crlf ; neue Zeile',
    0x01E9: 'lda curdsk ; aktuelles Laufwerk',
    0x01EC: "adi 'A' ; -> Buchstabe",
    0x01EE: 'sta dskerr ; in Meldung einsetzen',
    0x01F1: 'lxi b,dskmsg',
    0x01F4: 'call print ; "Bdos Err On X : "',
    0x01F7: 'pop b ; Fehlermeldung vom Stack',
    0x01F8: 'call print ; Fehlertext ausgeben',
    # --- conin (01FBH) ---
    0x01FB: 'lxi h,kbchar ; gepuffertes Zeichen?',
    0x01FE: 'mov a,m',
    0x01FF: 'mvi m,0 ; Puffer loeschen',
    0x0201: 'ora a',
    0x0202: 'ret nz ; ja -> Zeichen zurueck',
    0x0203: 'jmp coninf ; BIOS: Konsole lesen',
    # --- conech (0206H) ---
    0x0206: 'call conin ; Zeichen von Konsole',
    0x0209: 'call echoc ; Echo noetig?',
    0x020C: 'ret c ; Steuerzeichen -> kein Echo',
    0x020D: 'push af',
    0x020E: 'mov c,a',
    0x020F: 'call tabout ; Zeichen mit Tab-Expansion',
    0x0212: 'pop af',
    # --- echoc (0214H) ---
    0x0214: 'cpi cr ; Wagenruecklauf?',
    0x0217: 'cpi lf ; Zeilenvorschub?',
    0x021A: 'cpi tab ; Tabulator?',
    0x021D: 'cpi ctlh ; Backspace?',
    0x0220: "cpi ' ' ; Carry gesetzt wenn nicht druckbar",
    # --- conbrk (0223H) ---
    0x0223: 'lda kbchar ; gepuffertes Zeichen?',
    0x0227: 'jnz conb1 ; ja -> return true',
    0x022A: 'call constf ; BIOS: Status pruefen',
    0x022D: 'ani 1',
    0x022F: 'ret z ; kein Zeichen bereit',
    0x0230: 'call coninf ; BIOS: Zeichen lesen',
    0x0233: 'cpi ctls (13H) ; Bildschirm-Stop?',
    0x0235: 'jnz conb0 ; nein -> Zeichen merken',
    0x0238: 'call coninf ; naechstes Zeichen',
    0x023B: 'cpi ctlc (03H) ; Ctrl-C?',
    0x023D: 'jz reboot ; ja -> Neustart',
    0x0240: 'xra a ; nichts passiert',
    0x0242: 'sta kbchar ; Zeichen merken',
    0x0245: 'mvi a,1 ; true zurueck',
    # --- conout (0248H) ---
    0x0248: 'lda compcol ; nur Spalte berechnen?',
    0x024C: 'jnz compout ; ja -> nur Position',
    0x024F: 'push b ; Zeichen retten',
    0x0250: 'call conbrk ; Bildschirm-Stop pruefen',
    0x0253: 'pop b',
    0x0254: 'push b',
    0x0255: 'call conoutf ; BIOS: Zeichen ausgeben',
    0x0258: 'pop b',
    0x0259: 'push b',
    0x025A: 'lda listcp ; Drucker-Kopie aktiv?',
    0x025E: 'cnz listf ; ja -> auch an Drucker',
    0x0261: 'pop b',
    0x0262: 'mov a,c ; Zeichen zurueck',
    0x0263: 'lxi h,column ; Spaltenposition',
    0x0266: 'cpi rubout (7FH)',
    0x0268: 'ret z ; DEL -> keine Spaltenveraenderung',
    0x0269: 'inr m ; column++',
    0x026A: "cpi ' '",
    0x026C: 'ret nc ; druckbar -> fertig',
    0x026D: 'dcr m ; column-- (nicht druckbar)',
    0x026E: 'mov a,m',
    0x0270: 'ret z ; Spalte 0 -> fertig',
    0x0271: 'mov a,c',
    0x0272: 'cpi ctlh (08H) ; Backspace?',
    0x0274: 'jnz notbacksp',
    0x0277: 'dcr m ; column-- (Backspace)',
    0x0279: 'cpi lf ; Zeilenende?',
    0x027C: 'mvi m,0 ; column = 0',
    # --- ctlout (027FH) ---
    0x027F: 'mov a,c',
    0x0280: 'call echoc',
    0x0283: 'jnc tabout ; druckbar -> normal ausgeben',
    0x0286: 'push af',
    0x0287: "mvi c,'^' ; Pfeil-Praefix",
    0x0289: 'call conout',
    0x028C: 'pop af',
    0x028D: 'ori 40H ; Steuerzeichen -> Buchstabe',
    0x028F: 'mov c,a',
    # --- tabout (0290H) ---
    0x0290: 'mov a,c',
    0x0291: 'cpi tab (09H)',
    0x0293: 'jnz conout ; kein Tab -> direkt ausgeben',
    0x0296: "mvi c,' ' ; Leerzeichen",
    0x0298: 'call conout',
    0x029B: 'lda column',
    0x029E: 'ani 7 ; column mod 8',
    0x02A0: 'jnz tab0 ; weiter bis Tabstop',
    # --- backup (02A4H) ---
    0x02A4: 'call pctlh ; Backspace',
    0x02A7: "mvi c,' '",
    0x02A9: 'call conoutf ; Leerzeichen (BIOS direkt)',
    0x02AC: 'mvi c,ctlh (08H)',
    0x02AE: 'jmp conoutf ; nochmal Backspace',
    # --- crlfp (02B1H) ---
    0x02B1: "mvi c,'#'",
    0x02B3: 'call conout',
    0x02B6: 'call crlf',
    0x02B9: 'lda column',
    0x02BC: 'lxi h,strtcol ; Startposition',
    0x02BF: 'cmp m ; Ziel erreicht?',
    0x02C0: 'ret nc',
    0x02C1: "mvi c,' '",
    0x02C3: 'call conout',
    0x02C6: 'jmp crlfp0 ; weiter auffuellen',
    # --- crlf (02C9H) ---
    0x02C9: 'mvi c,cr (0DH)',
    0x02CB: 'call conout',
    0x02CE: 'mvi c,lf (0AH)',
    0x02D0: 'jmp conout',
    # --- print (02D3H) ---
    0x02D3: "ldax b ; Zeichen holen",
    0x02D4: "cpi '$' ; Ende?",
    0x02D6: 'ret z',
    0x02D7: 'inx b',
    0x02D8: 'push b',
    0x02D9: 'mov c,a',
    0x02DA: 'call tabout ; Zeichen ausgeben',
    0x02DD: 'pop b',
    0x02DE: 'jmp print ; naechstes Zeichen',
    # --- func handlers ---
    0x02E1: 'call conech ; Zeichen mit Echo',
    0x02E4: 'jmp sta$ret ; -> Rueckgabe',
    0x02E7: 'call readerf ; BIOS: Reader-Zeichen',
    0x02EA: 'jmp sta$ret',
    0x02ED: 'mov a,c ; Func 6: Direct I/O',
    0x02EE: 'inr a ; FFH -> 00 = Eingabe?',
    0x02EF: 'jz dirinp',
    0x02F2: 'inr a ; FEH -> 00 = Status?',
    0x02F3: 'jz constf ; BIOS: Status',
    0x02F6: 'jmp conoutf ; direkte Ausgabe',
    0x02F9: 'call constf ; Status pruefen',
    0x02FC: 'ora a',
    0x02FD: 'jz func$ret ; kein Zeichen -> 0 zurueck',
    0x0300: 'call coninf ; Zeichen holen',
    0x0303: 'jmp sta$ret',
    0x0306: 'lda ioloc (0003H)',
    0x0309: 'jmp sta$ret',
    0x030C: 'lxi h,ioloc (0003H)',
    0x030F: 'mov m,c ; IOBYTE setzen',
    0x0311: 'xchg ; HL = info',
    0x0312: 'mov c,l',
    0x0313: 'mov b,h ; BC = info',
    0x0314: 'jmp print ; String ausgeben',
    0x0317: 'call conbrk ; Status pruefen',
    0x031A: 'sta aret ; Ergebnis speichern',
    0x031E: 'mvi a,1',
    0x0320: 'jmp sta$ret',
    # --- BDOS disk variables ---
    0x0360: 'goerr: indirekte Fehleraufruf-Routine',
    0x0368: 'move: Speicherblock kopieren (C Bytes, DE->HL)',
    0x0372: 'selectdisk: Laufwerk auswaehlen, DPB laden',
    0x03BA: 'home: Kopf auf Spur 0',
    0x03CB: 'rdbuff: Sektor lesen mit Fehlerpruefung',
    0x03D1: 'wrbuff: Sektor schreiben mit Fehlerpruefung',
    0x03DC: 'seek: Spur/Sektor aus Recordnummer berechnen',
    0x04BF: 'getexta: Extent-Adresse im FCB holen',
    0x04C7: 'getfcba: reccnt/nxtrec-Adressen holen',
    0x04D4: 'getfcb: FCB-Parameter in Variablen laden',
    0x04EB: 'setfcb: Variablen zurueck in FCB schreiben',
    0x05B5: 'get$block: freien Allokations-Block suchen',
    0x060E: 'Verzeichnis-Ende pruefen',
    0x061E: 'Naechsten Verzeichnis-Eintrag lesen',
    0x0632: 'Verzeichnis-Record anfahren',
    0x06BC: 'Disk initialisieren (Verzeichnis scannen)',
    0x0731: 'Datei suchen (erstes Vorkommen)',
    0x07B5: 'Datei loeschen',
    0x0816: 'FCB-Daten in Verzeichnis kopieren',
    0x086A: 'Datei oeffnen (open)',
    0x08AD: 'Null-Eintraege in Disk-Map zusammenfuehren',
    0x08BB: 'Datei schliessen (close)',
    0x093D: 'Neue Datei anlegen (make)',
    0x0973: 'Sequentielles Lesen vorbereiten',
    # --- func12 onwards ---
    0x0AF0: 'Version 2.2 zurueck (22H)',
    0x0AF2: 'jmp sta$ret',
    0x0AF5: 'Reset: Login/R-O-Vektoren loeschen',
    0x0AF8: 'lxi h,0 ; Vektoren = 0',
    0x0AFE: 'lda cdisk ; aktuelles Laufwerk',
    0x0B06: 'DMA = 0080H (Default)',
    0x0B12: 'CALL reselect ; Laufwerk aus FCB',
    0x0B1B: 'CALL reselect ; + open',
    0x0B56: 'CALL reselect ; + seqdiskread',
    0x0B5C: 'CALL reselect ; + seqdiskwrite (STUB)',
    0x0B65: 'CALL reselect ; + make',
    0x0B74: 'Login-Vektor zurueck',
    0x0B7A: 'Aktuelles Laufwerk zurueck',
    0x0B81: 'Allokations-Vektor zurueck',
    0x0B96: 'DPB-Adresse zurueck',
    0x0B9D: 'Func 30: User-Code Set/Get',
    0x0BB7: 'File-Groesse berechnen',
}

# ─── Funktionsnamen fuer DW-Tabellen-Eintraege ──────────────────────────────
FUNCTAB_NAMES = {
    0: 'func00: wbootf (BIOS Warmstart)',
    1: 'func01: conech (Konsoleingabe+Echo)',
    2: 'func02: tabout (Konsolausgabe+Tab)',
    3: 'func03: readerf (Reader-Eingabe)',
    4: 'func04: punchf (Punch-Ausgabe)',
    5: 'func05: listf (List-Ausgabe)',
    6: 'func06: direct I/O',
    7: 'func07: get IOBYTE',
    8: 'func08: set IOBYTE',
    9: 'func09: print string',
    10: 'func10: read line (STUB)',
    11: 'func11: console status',
    12: 'func12: version (2.2)',
    13: 'func13: reset disks',
    14: 'func14: select disk',
    15: 'func15: open file',
    16: 'func16: close file',
    17: 'func17: search first',
    18: 'func18: search next',
    19: 'func19: delete file',
    20: 'func20: read sequential',
    21: 'func21: write seq (STUB)',
    22: 'func22: make file',
    23: 'func23: rename file',
    24: 'func24: login vector',
    25: 'func25: current disk',
    26: 'func26: set DMA',
    27: 'func27: alloc vector',
    28: 'func28: write protect',
    29: 'func29: R/O vector',
    30: 'func30: set indicators',
    31: 'func31: DPB address',
    32: 'func32: user code',
    33: 'func33: random read (STUB)',
    34: 'func34: random write (STUB)',
    35: 'func35: file size',
    36: 'func36: set random',
    37: 'func37: reset drive',
    38: 'func38: (unused)',
    39: 'func39: (unused)',
    40: 'func40: rand write+zero (STUB)',
}

# ─── Abschnitts-Ueberschriften (werden VOR dem Label eingefuegt) ────────────
SECTION_HEADERS = {
    0x0080: (
        '; ======================================================================',
        '; WBOOT-Code: @OS.COM Ladesequenz',
        '; ======================================================================',
        '; Wird beim Warmstart ausgefuehrt. Laedt @OS.COM von Disk ab 3780H.',
    ),
    0x00CC: (
        '; --- FCB fuer @OS.COM (Template im ROM) ---',
    ),
    0x0100: (
        '',
        '; ======================================================================',
        '; CP/M 2.2 BDOS-Modul (Mini-BDOS, Read-Only)',
        '; ======================================================================',
        ';',
        '; Assembliert mit ORG 0900H, aber physisch ab 0100H geladen.',
        '; @OS.COM verschiebt das Modul zur Laufzeit nach 0900H.',
        '; Alle internen Referenzen verwenden Laufzeit-Adressen (+0800H).',
        ';',
        '; HINWEIS: Dies ist ein abgespeckter BDOS fuer den Boot-Vorgang.',
        '; Schreib-Funktionen (func10/21/33/34/40) sind Stubs (08AFH).',
        '; Dient nur zum Laden von @OS.COM und BIOS-Initialisierung.',
        ';',
    ),
    0x0111: (
        '; --- bdose: BDOS-Einstiegspunkt (CP/M 2.2: arrive here from user) ---',
        '; Registerbelegung: C=Funktionsnummer, DE=Parameter',
    ),
    0x0147: (
        '; --- functab: 41 Dispatch-Eintraege (func00..func40) ---',
        '; Eintraege zeigen auf Laufzeit-Adressen (nach Relokation auf 0900H).',
        '; STUB-Eintraege (08AFH) verweisen auf mergezero (= Dummy-Ruecksprung).',
    ),
    0x0198: (
        '; --- Fehlerbehandlung: persub/selsub/rodsub/rofsub (CP/M 2.2) ---',
    ),
    0x01BA: (
        '; --- Fehlermeldungen (ASCII, $-terminiert) ---',
    ),
    0x01E5: (
        '; --- Console-I/O-Routinen (CP/M 2.2: errflg..print) ---',
        '; Identisch mit CP/M 2.2 BDOS, nur in Z80-Mnemonics.',
    ),
    0x02E1: (
        '; --- Funktions-Handler (func1..func11, sta$ret, func$ret) ---',
    ),
    0x0323: (
        '; --- BDOS-Variablenbereich + lokaler Stack ---',
    ),
    0x0360: (
        '; --- Disk-Routinen (CP/M 2.2: goerr..mergezero) ---',
        '; Low-Level Disk-I/O mit DPB-Verwaltung, Verzeichnis-Zugriff,',
        '; Allokations-Verwaltung und FCB-Operationen.',
    ),
    0x08CC: (
        '; --- @OS.COM FCB (Runtime-Kopie) ---',
    ),
    0x0AF0: (
        '; --- High-Level BDOS-Funktionen (func12..func32) ---',
    ),
    0x0BFF: (
        '; --- BDOS-Arbeitsbereich (Variablen, Disk-Parameter) ---',
    ),
}


# ─── Hauptprogramm ───────────────────────────────────────────────────────────

def main():
    # Stage-1 extrahieren (0080H-0CFFH)
    stage1 = data[0x0080:0x0D00]
    ram_base = 0x0080

    # Komplett disassemblieren
    rows = disasm_range(stage1, ram_base)

    # Label-Lookup fuer Sprungziele erstellen
    # Alle Sprungziele sammeln, die im Bereich 0080H-0CFFH liegen
    jump_targets = set()
    for addr, raw_b, mnem in rows:
        # JP/CALL/JR Ziele extrahieren
        for prefix in ('JP ', 'CALL ', 'JR ', 'JP NZ,', 'JP Z,', 'JP NC,',
                        'JP C,', 'JP PO,', 'JP PE,', 'JP P,', 'JP M,',
                        'CALL NZ,', 'CALL Z,', 'CALL NC,', 'CALL C,',
                        'CALL PO,', 'CALL PE,', 'CALL P,', 'CALL M,',
                        'JR NZ,', 'JR Z,', 'JR NC,', 'JR C,',
                        'DJNZ '):
            if mnem.startswith(prefix) and '(' not in prefix:
                try:
                    tgt_str = mnem[len(prefix):].rstrip('H')
                    tgt = int(tgt_str, 16)
                    if 0x0080 <= tgt <= 0x0CFF:
                        jump_targets.add(tgt)
                except ValueError:
                    pass

    # Automatische Labels fuer unbenannte Sprungziele
    auto_labels = {}
    for tgt in sorted(jump_targets):
        if tgt not in LABELS:
            auto_labels[tgt] = f'L{tgt:04X}'

    all_labels = {**LABELS, **auto_labels}

    # Daten- / String- / Padding- / NOP-Bereiche als Sets
    data_addrs = set()
    for start, end, typ, desc in DATA_RANGES:
        for a in range(start, end):
            data_addrs.add(a)

    string_addrs = set()
    for start, end, _ in STRING_RANGES:
        for a in range(start, end):
            string_addrs.add(a)

    padding_addrs = set()
    for start, end, _ in PADDING_RANGES:
        for a in range(start, end):
            padding_addrs.add(a)

    nop_addrs = set()
    for start, end, _ in NOP_RANGES:
        for a in range(start, end):
            nop_addrs.add(a)

    # Ausgabe erzeugen
    lines = []
    lines.append('; ======================================================================')
    lines.append('; MINI-BDOS — Kommentierte Disassemblierung des CPA780-SYL-Bootladers')
    lines.append('; ======================================================================')
    lines.append(';')
    lines.append('; Adressbereich: 0080H - 0CFFH (3200 Bytes, Track 0 Sektoren 1-25)')
    lines.append('; Erzeugt mit:   bdos_analyze.py (Z80-Disassembler + CP/M 2.2 Mapping)')
    lines.append('; Binaerquelle:  prebuilt/bc_a5120/bootsec.bin')
    lines.append('; CP/M-Referenz: doc/CPM_2_2/OS3BDOS.ASM + OS3BDOS1.ASM')
    lines.append(';')
    lines.append('; Dieses File ist eine SEPARATE ANALYSE des Mini-BDOS-Bereichs.')
    lines.append('; Es ist NICHT direkt assemblierbar, sondern dient der Dokumentation.')
    lines.append('; Die assemblierbare Version ist bootsec/src/bootsec.mac.')
    lines.append(';')
    lines.append('; Architektur:')
    lines.append(';   Das BDOS-Modul (0100H-0CFFH) wurde mit ORG 0900H assembliert.')
    lines.append(';   Alle internen Referenzen verwenden Laufzeit-Adressen (+0800H).')
    lines.append(';   @OS.COM verschiebt den Code zur Laufzeit von 0100H nach 0900H.')
    lines.append(';   BIOS-Aufrufe (18xxH) verweisen auf das von @OS.COM eingerichtete BIOS.')
    lines.append(';')
    lines.append(';   Phys. 0100H = Laufzeit 0900H   (BDOS-Modul-Anfang)')
    lines.append(';   Phys. 0111H = Laufzeit 0911H   (bdose Einstiegspunkt)')
    lines.append(';   Phys. 0147H = Laufzeit 0947H   (functab Dispatch-Tabelle)')
    lines.append(';   Phys. 0CFFH = Laufzeit 14FFH   (BDOS-Modul-Ende)')
    lines.append(';')
    lines.append('; Dies ist ein READ-ONLY Mini-BDOS fuer den Boot-Vorgang:')
    lines.append(';   - Schreib-Funktionen (func10/21/33/34/40) zeigen auf Stub (08AFH)')
    lines.append(';   - Console-I/O ist vollstaendig (func1-9, func11)')
    lines.append(';   - Disk-Lesen ist vollstaendig (func15/17/18/20)')
    lines.append(';')
    lines.append('; Inhalt:')
    lines.append(';   0080H-00CBH  WBOOT-Vektor + @OS.COM-Ladesequenz + READ_FILE')
    lines.append(';   00CCH-00F0H  FCB fuer @OS.COM')
    lines.append(';   00F1H-0105H  Padding')
    lines.append(';   0106H-0110H  BDOS-Modul-Kopf (JMP bdose + Fehler-Handler-Zeiger)')
    lines.append(';   0111H-0146H  bdose: BDOS-Einstiegspunkt + Dispatch-Code')
    lines.append(';   0147H-0197H  functab: 41 DW-Eintraege (Funktions-Dispatch)')
    lines.append(';   0198H-01B9H  Fehlerbehandlung (persub/selsub/rodsub/rofsub)')
    lines.append(';   01BAH-01E4H  ASCII-Fehlermeldungen')
    lines.append(';   01E5H-02E0H  Console-I/O (errflg, conin, conout, print, ...)')
    lines.append(';   02E1H-0322H  Funktions-Handler (func1..func11, sta$ret)')
    lines.append(';   0323H-035FH  BDOS-Variablen + lokaler Stack')
    lines.append(';   0360H-08CCH  Disk-Routinen (goerr..open..close..search..read)')
    lines.append(';   08CCH-08F0H  FCB @OS.COM (Runtime-Kopie)')
    lines.append(';   08F1H-0AEFH  Disk-Routinen (Fortsetzung, make..rseek..select)')
    lines.append(';   0AF0H-0BFEH  High-Level BDOS-Funktionen (func12..func32)')
    lines.append(';   0BFFH-0CF8H  BDOS-Arbeitsbereich (Variablen/Puffer)')
    lines.append(';   0CF9H-0CFFH  Padding (Ende Track 0)')
    lines.append(';')
    lines.append('; Label-Konvention:')
    lines.append(';   GROSSBUCHSTABEN = CP/M 2.2 Standard-Bezeichner')
    lines.append(';   Lxxxx          = automatisch generiertes Label (Sprungziel)')
    lines.append('; ======================================================================')
    lines.append('')
    lines.append('\tORG\t0080H')
    lines.append('')

    # Zeilen ausgeben
    i = 0
    while i < len(rows):
        addr, raw_b, mnem = rows[i]

        # Abschnitts-Ueberschriften VOR dem Label
        if addr in SECTION_HEADERS:
            lines.append('')
            for hdr_line in SECTION_HEADERS[addr]:
                lines.append(hdr_line)

        # Label ausgeben
        if addr in all_labels:
            lbl = all_labels[addr]
            lines.append(f'{lbl}:')

        # Sektionsueberschriften fuer DATA_RANGES
        for start, end, typ, desc in DATA_RANGES:
            if addr == start:
                lines.append(f'; --- {desc} ---')

        # Sektionsueberschriften fuer STRING_RANGES
        for start, end, text in STRING_RANGES:
            if addr == start:
                lines.append(f'; --- ASCII: "{text}" ---')

        # Sektionsueberschriften fuer PADDING_RANGES
        for start, end, desc in PADDING_RANGES:
            if addr == start:
                lines.append(f'; --- {desc} ({end - start} Bytes) ---')

        # Sektionsueberschriften fuer NOP_RANGES
        for start, end, desc in NOP_RANGES:
            if addr == start:
                lines.append(f'; --- {desc} ---')

        # Padding-Bereich: als DS ausgeben
        if addr in padding_addrs:
            # Finde das Ende des Padding-Bereichs
            for start, end, desc in PADDING_RANGES:
                if start <= addr < end:
                    pad_len = end - addr
                    lines.append(f'\tDS\t{pad_len}\t\t; {addr:04X}H-{end - 1:04X}H: {desc}')
                    # Ueberspringe die Padding-Zeilen
                    while i < len(rows) and rows[i][0] < end:
                        i += 1
                    break
            continue

        # NOP-Bereich: als DS ausgeben (statt vieler einzelner NOPs)
        if addr in nop_addrs:
            for start, end, desc in NOP_RANGES:
                if addr == start:
                    nop_len = end - start
                    lines.append(f'\tDS\t{nop_len}\t\t; {addr:04X}H-{end - 1:04X}H: {desc}')
                    while i < len(rows) and rows[i][0] < end:
                        i += 1
                    break
            else:
                # Mitten in einem NOP-Bereich (schon uebersprungen)
                i += 1
            continue

        # Datenbereich: je nach Typ ausgeben
        if addr in data_addrs:
            for start, end, typ, desc in DATA_RANGES:
                if addr == start:
                    if typ == 'dw':
                        # Word-Tabelle ausgeben
                        a = start
                        while a < end and a - ram_base < len(stage1) - 1:
                            lo = stage1[a - ram_base]
                            hi = stage1[a - ram_base + 1]
                            word = lo | (hi << 8)
                            idx = (a - start) // 2
                            # Funktionsnamen aus FUNCTAB_NAMES (fuer functab)
                            if start == 0x0147 and idx in FUNCTAB_NAMES:
                                fname = FUNCTAB_NAMES[idx]
                                comment = f'; {a:04X}H: {fname}'
                            else:
                                tgt_lbl = all_labels.get(word, '')
                                comment = f'; {a:04X}H: Eintrag {idx:02d} -> {word:04X}H'
                                if tgt_lbl:
                                    comment += f' ({tgt_lbl})'
                            lines.append(f'\tDW\t{word:04X}H\t\t{comment}')
                            a += 2
                    elif typ == 'fcb':
                        # FCB: erstes Byte = Laufwerk, dann 8+3 Name, Rest Nullen
                        fcb_data = stage1[start - ram_base:end - ram_base]
                        lines.append(f'\tDB\t{fcb_data[0]:02X}H\t\t; {start:04X}H: Laufwerk (0=default)')
                        # Name (Bytes 1-8)
                        name = fcb_data[1:9]
                        name_str = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in name)
                        lines.append(f"\tDB\t'{name_str}'\t; {start + 1:04X}H: Dateiname (8 Zeichen)")
                        # Extension (Bytes 9-11)
                        ext = fcb_data[9:12]
                        ext_str = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in ext)
                        lines.append(f"\tDB\t'{ext_str}'\t\t; {start + 9:04X}H: Extension")
                        # Rest als DS
                        rest_len = end - start - 12
                        if rest_len > 0:
                            lines.append(f'\tDS\t{rest_len}\t\t; {start + 12:04X}H-{end - 1:04X}H: FCB-Rest')
                    else:
                        # Rohe DB-Bytes
                        a = start
                        while a < end:
                            chunk_end = min(a + 8, end)
                            chunk_bytes = stage1[a - ram_base:chunk_end - ram_base]
                            hex_str = ','.join(f'0{b:02X}H' if b > 0x09 else f'{b:02X}H'
                                               for b in chunk_bytes)
                            lines.append(f'\tDB\t{hex_str}\t; {a:04X}H')
                            a = chunk_end
                    # Ueberspringe die disassemblierten Zeilen
                    while i < len(rows) and rows[i][0] < end:
                        i += 1
                    break
            continue

        # String-Bereich: als DB 'text' ausgeben
        if addr in string_addrs:
            for start, end, text in STRING_RANGES:
                if addr == start:
                    str_bytes = stage1[start - ram_base:end - ram_base]
                    # ASCII-Darstellung
                    ascii_str = ''.join(chr(b) if 0x20 <= b < 0x7F else '.'
                                        for b in str_bytes)
                    lines.append(f"\tDB\t'{ascii_str}'\t; {start:04X}H")
                    while i < len(rows) and rows[i][0] < end:
                        i += 1
                    break
            continue

        # Code: als Assembler ausgeben —   continue

        # Code: als Assembler ausgeben — Kommentar zusammenbauen
        comment_parts = [f'{addr:04X}H']

        # Inline-Kommentar aus COMMENTS dict
        if addr in COMMENTS:
            comment_parts.append(COMMENTS[addr])
        else:
            # Sprungziele mit Label-Referenz kommentieren
            for prefix in ('JP ', 'CALL ', 'JR ', 'DJNZ ',
                            'JP NZ,', 'JP Z,', 'JP NC,', 'JP C,',
                            'JP PO,', 'JP PE,', 'JP P,', 'JP M,',
                            'CALL NZ,', 'CALL Z,', 'CALL NC,', 'CALL C,',
                            'JR NZ,', 'JR Z,', 'JR NC,', 'JR C,'):
                if mnem.startswith(prefix) and '(' not in prefix:
                    try:
                        tgt_str = mnem[len(prefix):].rstrip('H')
                        tgt = int(tgt_str, 16)
                        if tgt in all_labels:
                            comment_parts.append(f'-> {all_labels[tgt]}')
                    except ValueError:
                        pass
                    break

        comment = '; ' + '  '.join(comment_parts)        comment = '; ' + '  '.join(comment_parts)    code_count = sum(1 for _, _, m in rows if not m.startswith('DB '))
    db_count = sum(1 for _, _, m in rows if m.startswith('DB '))
    total = len(rows)

    lines.append('')
    lines.append('; ======================================================================')
    lines.append(f'; Statistik: {total} Eintraege, {code_count} Befehle, {db_count} Datenbytes')
    lines.append(f'; Erkannte Labels: {len(LABELS)} manuell + {len(auto_labels)} automatisch')
    lines.append('; ======================================================================')
    lines.append(f'\tEND')

    # Datei schreiben
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    print(f"Mini-BDOS disassembliert: {OUTPUT_FILE}")
    print(f"  {total} Eintraege, {code_count} Befehle, {db_count} Datenbytes")
    print(f"  {len(LABELS)} manuelle + {len(auto_labels)} automatische Labels")
    print(f"  Sprungziele im Bereich: {len(jump_targets)}")


if __name__ == '__main__':
    main()
