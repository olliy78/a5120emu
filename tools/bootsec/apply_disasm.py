#!/usr/bin/env python3
"""Ersetzt die DB-Rohbytes ab 0E72H in bootsec.mac durch disassemblierten Code.

Liest bootsec.mac, findet die Grenze zwischen konvertiertem Code und DB-Daten,
ersetzt die DB-Abschnitte ab 0E72H durch disassemblierten Code und Daten.
Nutzt z80disasm.py intern fuer die Disassemblierung.
"""

import os
import re
import struct
import sys

BINPATH = 'prebuilt/bc_a5120/bootsec.bin'
MACPATH = 'bootsec/src/bootsec.mac'

# ======================================================================
# Z80 Disassembler (kompakte Version)
# ======================================================================

R8 = ['B','C','D','E','H','L','(HL)','A']
R16 = ['BC','DE','HL','SP']
R16P = ['BC','DE','HL','AF']
CC = ['NZ','Z','NC','C','PO','PE','P','M']
ALU = ['ADD\tA,','ADC\tA,','SUB\t','SBC\tA,','AND\t','XOR\t','OR\t','CP\t']

def build_main_table():
    t = [None]*256
    t[0x00] = ('NOP', 0)
    for i,r in enumerate(R16):
        t[0x01+i*16] = (f'LD\t{r},{{w}}', 2)
    t[0x02] = ('LD\t(BC),A', 0); t[0x12] = ('LD\t(DE),A', 0)
    t[0x0A] = ('LD\tA,(BC)', 0); t[0x1A] = ('LD\tA,(DE)', 0)
    t[0x22] = ('LD\t({w}),HL', 2); t[0x2A] = ('LD\tHL,({w})', 2)
    t[0x32] = ('LD\t({w}),A', 2); t[0x3A] = ('LD\tA,({w})', 2)
    for i,r in enumerate(R16):
        t[0x03+i*16] = (f'INC\t{r}', 0); t[0x0B+i*16] = (f'DEC\t{r}', 0)
    for i,r in enumerate(R8):
        t[0x04+i*8] = (f'INC\t{r}', 0); t[0x05+i*8] = (f'DEC\t{r}', 0)
    for i,r in enumerate(R8):
        t[0x06+i*8] = (f'LD\t{r},{{b}}', 1)
    for op, mn in zip([7,0xF,0x17,0x1F,0x27,0x2F,0x37,0x3F],
                      ['RLCA','RRCA','RLA','RRA','DAA','CPL','SCF','CCF']):
        t[op] = (mn, 0)
    t[0x08] = ("EX\tAF,AF'", 0)
    for i,r in enumerate(R16): t[0x09+i*16] = (f'ADD\tHL,{r}', 0)
    t[0x10] = ('DJNZ\t{r}', -1); t[0x18] = ('JR\t{r}', -1)
    for i,c in enumerate(['NZ','Z','NC','C']): t[0x20+i*8] = (f'JR\t{c},{{r}}', -1)
    t[0x76] = ('HALT', 0)
    for i,rd in enumerate(R8):
        for j,rs in enumerate(R8):
            op = 0x40+i*8+j
            if op != 0x76: t[op] = (f'LD\t{rd},{rs}', 0)
    for i,a in enumerate(ALU):
        for j,r in enumerate(R8): t[0x80+i*8+j] = (f'{a}{r}', 0)
    alu_n = ['ADD\tA,{b}','ADC\tA,{b}','SUB\t{b}','SBC\tA,{b}',
             'AND\t{b}','XOR\t{b}','OR\t{b}','CP\t{b}']
    for i,a in enumerate(alu_n): t[0xC6+i*8] = (a, 1)
    for i,c in enumerate(CC): t[0xC0+i*8] = (f'RET\t{c}', 0)
    for i,r in enumerate(R16P): t[0xC1+i*16] = (f'POP\t{r}', 0)
    t[0xC9] = ('RET', 0); t[0xD9] = ('EXX', 0)
    t[0xE9] = ('JP\t(HL)', 0); t[0xF9] = ('LD\tSP,HL', 0)
    for i,c in enumerate(CC): t[0xC2+i*8] = (f'JP\t{c},{{w}}', 2)
    t[0xC3] = ('JP\t{w}', 2)
    t[0xD3] = ('OUT\t({b}),A', 1); t[0xDB] = ('IN\tA,({b})', 1)
    t[0xE3] = ('EX\t(SP),HL', 0); t[0xEB] = ('EX\tDE,HL', 0)
    t[0xF3] = ('DI', 0); t[0xFB] = ('EI', 0)
    for i,c in enumerate(CC): t[0xC4+i*8] = (f'CALL\t{c},{{w}}', 2)
    for i,r in enumerate(R16P): t[0xC5+i*16] = (f'PUSH\t{r}', 0)
    t[0xCD] = ('CALL\t{w}', 2)
    for i in range(8): t[0xC7+i*8] = (f'RST\t{i*8:02X}H', 0)
    return t

def build_cb_table():
    t = [None]*256
    for i,op in enumerate(['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']):
        for j,r in enumerate(R8): t[i*8+j] = (f'{op}\t{r}', 0)
    for bit in range(8):
        for j,r in enumerate(R8):
            t[0x40+bit*8+j] = (f'BIT\t{bit},{r}', 0)
            t[0x80+bit*8+j] = (f'RES\t{bit},{r}', 0)
            t[0xC0+bit*8+j] = (f'SET\t{bit},{r}', 0)
    return t

def build_ed_table():
    t = [None]*256
    for i,r in enumerate(R8):
        if r != '(HL)':
            t[0x40+i*8] = (f'IN\t{r},(C)', 0)
            t[0x41+i*8] = (f'OUT\t(C),{r}', 0)
    for i,r in enumerate(R16):
        t[0x42+i*16] = (f'SBC\tHL,{r}', 0); t[0x4A+i*16] = (f'ADC\tHL,{r}', 0)
        t[0x43+i*16] = (f'LD\t({{w}}),{r}', 2); t[0x4B+i*16] = (f'LD\t{r},({{w}})', 2)
    t[0x44]=('NEG',0); t[0x45]=('RETN',0); t[0x4D]=('RETI',0)
    t[0x46]=('IM\t0',0); t[0x56]=('IM\t1',0); t[0x5E]=('IM\t2',0)
    t[0x47]=('LD\tI,A',0); t[0x4F]=('LD\tR,A',0)
    t[0x57]=('LD\tA,I',0); t[0x5F]=('LD\tA,R',0)
    t[0x67]=('RRD',0); t[0x6F]=('RLD',0)
    for op,mn in [(0xA0,'LDI'),(0xA1,'CPI'),(0xA2,'INI'),(0xA3,'OUTI'),
                  (0xA8,'LDD'),(0xA9,'CPD'),(0xAA,'IND'),(0xAB,'OUTD'),
                  (0xB0,'LDIR'),(0xB1,'CPIR'),(0xB2,'INIR'),(0xB3,'OTIR'),
                  (0xB8,'LDDR'),(0xB9,'CPDR'),(0xBA,'INDR'),(0xBB,'OTDR')]:
        t[op] = (mn, 0)
    return t

MAIN = build_main_table()
CB_TAB = build_cb_table()
ED_TAB = build_ed_table()

def fmt_b(v):
    s = f'{v:02X}H'
    return ('0'+s) if s[0].isalpha() else s

def fmt_w(v):
    s = f'{v:04X}H'
    return ('0'+s) if s[0].isalpha() else s

def disasm_one(data, offset, base_addr):
    if offset >= len(data): return None, 0
    pc = base_addr + offset
    b0 = data[offset]
    if b0 in (0xDD, 0xFD):
        reg = 'IX' if b0 == 0xDD else 'IY'
        if offset+1 >= len(data): return None, 1
        b1 = data[offset+1]
        if b1 == 0xCB:
            if offset+3 >= len(data): return None, 1
            d = struct.unpack('b', bytes([data[offset+2]]))[0]
            sign = '+' if d >= 0 else ''
            entry = CB_TAB[data[offset+3]]
            if entry:
                return entry[0].replace('(HL)', f'({reg}{sign}{d})'), 4
            return None, 1
        entry = MAIN[b1]
        if entry is None: return None, 1
        mnem_t, extra = entry
        if '(HL)' in mnem_t:
            if offset+2 >= len(data): return None, 1
            d = struct.unpack('b', bytes([data[offset+2]]))[0]
            sign = '+' if d >= 0 else ''
            nm = mnem_t.replace('(HL)', f'({reg}{sign}{d})')
            total = 3
            if extra == 1:
                if offset+3 >= len(data): return None, 1
                nm = nm.replace('{b}', fmt_b(data[offset+3])); total = 4
            elif extra == 2:
                if offset+4 >= len(data): return None, 1
                w = data[offset+3]|(data[offset+4]<<8); nm = nm.replace('{w}', fmt_w(w)); total = 5
            return nm, total
        elif 'HL' in mnem_t:
            nm = mnem_t.replace('HL', reg)
            total = 2
            if extra == 1:
                if offset+2 >= len(data): return None, 1
                nm = nm.replace('{b}', fmt_b(data[offset+2])); total = 3
            elif extra == 2:
                if offset+3 >= len(data): return None, 1
                w = data[offset+2]|(data[offset+3]<<8); nm = nm.replace('{w}', fmt_w(w)); total = 4
            elif extra == -1:
                if offset+2 >= len(data): return None, 1
                rel = struct.unpack('b', bytes([data[offset+2]]))[0]
                nm = nm.replace('{r}', fmt_w(pc+3+rel)); total = 3
            return nm, total
        return None, 1
    if b0 == 0xCB:
        if offset+1 >= len(data): return None, 1
        entry = CB_TAB[data[offset+1]]
        return (entry[0], 2) if entry else (None, 1)
    if b0 == 0xED:
        if offset+1 >= len(data): return None, 1
        entry = ED_TAB[data[offset+1]]
        if entry:
            nm, ex = entry; total = 2
            if ex == 2:
                if offset+3 >= len(data): return None, 1
                w = data[offset+2]|(data[offset+3]<<8); nm = nm.replace('{w}', fmt_w(w)); total = 4
            return nm, total
        return None, 1
    entry = MAIN[b0]
    if entry is None: return None, 1
    nm, ex = entry; total = 1
    if ex == 1:
        if offset+1 >= len(data): return None, 1
        nm = nm.replace('{b}', fmt_b(data[offset+1])); total = 2
    elif ex == 2:
        if offset+2 >= len(data): return None, 1
        w = data[offset+1]|(data[offset+2]<<8); nm = nm.replace('{w}', fmt_w(w)); total = 3
    elif ex == -1:
        if offset+1 >= len(data): return None, 1
        rel = struct.unpack('b', bytes([data[offset+1]]))[0]
        nm = nm.replace('{r}', fmt_w(pc+2+rel)); total = 2
    return nm, total


# ======================================================================
# Binary-Zugriff
# ======================================================================

def read_bin_t2(addr, length):
    """Liest Bytes aus Track 2 (RAM 0D00-19FF -> Datei-Offset 1A00+)."""
    with open(BINPATH, 'rb') as f:
        f.seek(0x1A00 + (addr - 0x0D00))
        return f.read(length)


# ======================================================================
# Code-Generierung
# ======================================================================

def disasm_section(start, end):
    """Disassembliert einen Code-Bereich und gibt M80-Zeilen zurueck."""
    data = read_bin_t2(start, end - start)
    lines = []
    off = 0
    while off < len(data):
        pc = start + off
        mnem, nb = disasm_one(data, off, start)
        if mnem and nb > 0:
            lines.append(f'\t{mnem}\t\t; {pc:04X}')
            off += nb
        else:
            lines.append(f'\tDB\t{fmt_b(data[off])}\t\t; {pc:04X}: (unbekannter Opcode)')
            off += 1
    return lines

def data_section(start, end, raw=None):
    """Gibt einen Daten-Bereich als DB/DS-Zeilen zurueck."""
    if raw is None:
        raw = read_bin_t2(start, end - start)
    lines = []
    i = 0
    while i < len(raw):
        addr = start + i
        # Null-Bloecke komprimieren
        if i + 8 <= len(raw) and all(b == 0 for b in raw[i:i+8]):
            j = i
            while j < len(raw) and raw[j] == 0: j += 1
            lines.append(f'\tDS\t{j-i},00H\t\t; {addr:04X}: Fuellbytes')
            i = j; continue
        # FF-Bloecke komprimieren
        if i + 8 <= len(raw) and all(b == 0xFF for b in raw[i:i+8]):
            j = i
            while j < len(raw) and raw[j] == 0xFF: j += 1
            lines.append(f'\tDS\t{j-i},0FFH\t\t; {addr:04X}')
            i = j; continue
        # Normale DB-Zeile (max 8 Bytes)
        chunk_end = min(i + 8, len(raw))
        chunk = raw[i:chunk_end]
        hexvals = ','.join(fmt_b(b) for b in chunk)
        ascii_repr = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f'\tDB\t{hexvals}\t; {addr:04X}: {ascii_repr}')
        i = chunk_end
    return lines

def string_data(start, text_bytes, suffix_bytes, prefix_bytes=None):
    """Gibt einen String mit optionalem Prefix und Suffix als DB-Zeile zurueck."""
    lines = []
    addr = start
    if prefix_bytes:
        hexvals = ','.join(fmt_b(b) for b in prefix_bytes)
        lines.append(f'\tDB\t{hexvals}\t\t; {addr:04X}')
        addr += len(prefix_bytes)
    s = bytes(text_bytes).decode('ascii')
    suffix_hex = ','.join(fmt_b(b) for b in suffix_bytes) if suffix_bytes else ''
    if suffix_hex:
        lines.append(f"\tDB\t'{s}',{suffix_hex}\t; {addr:04X}")
    else:
        lines.append(f"\tDB\t'{s}'\t; {addr:04X}")
    return lines


# ======================================================================
# Hauptprogramm
# ======================================================================

def generate_replacement():
    """Generiert den gesamten Ersatztext fuer 0E72-19FF + END."""
    out = []

    # --- 0E72-0FC0: Boot-BIOS Fortsetzung (Code) ---
    out.append('; ======================================================================')
    out.append('; Boot-BIOS Fortsetzung: OS-Lade-Logik (0E72H-0FC0H)')
    out.append('; ======================================================================')
    out.append('; Fortsetzung des @OS.COM-Ladeprozesses:')
    out.append('; - FDC-Sektor-Lesen mit Wiederholungen')
    out.append('; - Interrupt-Service-Routinen fuer FDC')
    out.append('; - Block-Transfer der gelesenen Daten')
    out.append('; (Disassembliert aus Binaerdaten, keine Referenz-Kommentare)')
    out.extend(disasm_section(0x0E72, 0x0FC1))
    out.append('')

    # --- 0FC1-0FFF: Bootloader-Versionsstring (Daten) ---
    out.append('; --- Bootloader-Versionsstring (0FC1H-0FFFH) ---')
    raw = read_bin_t2(0x0FC1, 0x1000 - 0x0FC1)
    # 0FC1: 06H (Header-Byte)
    # 0FC2-0FDD: "Bootloader, Version 24.02.87"
    # 0FDE: 00H (Terminator)
    # 0FDF-0FFD: 00H (Fuellbytes)
    # 0FFE-0FFF: 02H 11H (Daten)
    out.append(f'\tDB\t06H\t\t; 0FC1: Header-Byte')
    out.append(f"\tDB\t'Bootloader, Version 24.02.87',00H\t; 0FC2")
    out.append(f'\tDS\t31,00H\t\t; 0FDF: Fuellbytes')
    out.append(f'\tDB\t02H,11H\t\t; 0FFE')
    out.append('')

    # --- 1000-1032: BIOS-Sprungtabelle ---
    out.append('; ======================================================================')
    out.append('; BIOS-Sprungtabelle (CP/M 2.2 Standard, 17 Eintraege)')
    out.append('; ======================================================================')
    bios_names = [
        ('20BCH', 'BOOT',   'Kaltstart (Jump ins geladene OS)'),
        ('08AFH', 'WBOOT',  'Warmstart'),
        ('08AFH', 'CONST',  'Konsol-Status (Stub)'),
        ('08AFH', 'CONIN',  'Konsol-Eingabe (Stub)'),
        ('18B2H', 'CONOUT', 'Konsol-Ausgabe'),
        ('08AFH', 'LIST',   'Drucker-Ausgabe (Stub)'),
    ]
    addr = 0x1000
    for target, name, desc in bios_names:
        out.append(f'\tJP\t{target}\t\t; {addr:04X}: {name} - {desc}')
        addr += 3
    # PUNCH (inline stub)
    out.append(f'\tLD\tA,C\t\t; {addr:04X}: PUNCH - Lochstreifen (Stub)')
    out.append(f'\tRET\t\t\t; {addr+1:04X}')
    out.append(f'\tNOP\t\t\t; {addr+2:04X}: (Fuellbyte)')
    addr += 3
    # READER (inline stub)
    out.append(f'\tLD\tA,1AH\t\t; {addr:04X}: READER - Leser (Stub, gibt EOF)')
    out.append(f'\tRET\t\t\t; {addr+1:04X}')
    addr += 2
    disk_names = [
        ('1964H', 'HOME',   'Kopf auf Spur 0'),
        ('1949H', 'SELDSK', 'Laufwerk waehlen'),
        ('1972H', 'SETTRK', 'Spur setzen'),
        ('1977H', 'SETSEC', 'Sektor setzen'),
        ('197CH', 'SETDMA', 'DMA-Adresse setzen'),
        ('1988H', 'READ',   'Sektor lesen'),
        ('08AFH', 'WRITE',  'Sektor schreiben (Stub)'),
        ('08AFH', 'LISTST', 'Drucker-Status (Stub)'),
        ('1981H', 'SECTRN', 'Sektor-Uebersetzung'),
    ]
    for target, name, desc in disk_names:
        out.append(f'\tJP\t{target}\t\t; {addr:04X}: {name} - {desc}')
        addr += 3
    out.append(f'\tDS\t5,00H\t\t; 1033: Fuellbytes')
    out.append('')

    # --- 1038-108F: DPH/DPB-Tabellen ---
    out.append('; --- DPH/DPB Disk-Parameter-Tabellen (1038H-108FH) ---')
    out.append('; (Datenbereich: Disk Parameter Header + Block)')
    out.extend(data_section(0x1038, 0x1090))
    out.append('')

    # --- 1090-10B1: GOCPM ---
    out.append('; ======================================================================')
    out.append('; GOCPM: CP/M-Kaltstart-Initialisierung (1090H-10B1H)')
    out.append('; ======================================================================')
    out.append('; Patcht Warmstart-JP bei 0000H, BDOS-JP bei 0005H,')
    out.append('; setzt DMA auf 0080H und springt zum CCP.')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x1090, 0x10B2))
    out.append('')

    # --- 10B2-1134: BIOS-Hilfsfunktionen ---
    out.append('; --- BIOS-Hilfsfunktionen (10B2H-1134H) ---')
    out.append('; CONOUT-Verarbeitung, Zeichenpuffer-Verwaltung')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x10B2, 0x1135))
    out.append('')

    # --- 1135-1147: Sprungtabellen ---
    out.append('; --- Sprungtabellen-Daten (1135H-1147H) ---')
    out.extend(data_section(0x1135, 0x1148))
    out.append('')

    # --- 1148-13B7: FDC-Treiber ---
    out.append('; ======================================================================')
    out.append('; FDC-Treiber: Disk-Read/Write/Seek (1148H-13B7H)')
    out.append('; ======================================================================')
    out.append('; Floppy-Disk-Controller-Routinen fuer Sektor-Lesen/Schreiben/Positionierung')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x1148, 0x13B8))
    out.append('')

    # --- 13B8-13CF: FDC-Fehlermeldung ---
    out.append('; --- FDC-Fehlermeldungs-Vorlage (13B8H-13CFH) ---')
    raw = read_bin_t2(0x13B8, 0x13D0 - 0x13B8)
    # 13B8: 0D 0A  = CR LF
    # 13BA-13C7: "XX;T,Si,Se=XXXXXX" + 07 00
    out.extend(data_section(0x13B8, 0x13D0, raw))
    out.append('')

    # --- 13D0-13EF: FDC-Konfigurationsdaten ---
    out.append('; --- FDC-Konfigurationsdaten (13D0H-13EFH) ---')
    out.extend(data_section(0x13D0, 0x13F0))
    out.append('')

    # --- 13F0-16AF: FDC-Treiber Fortsetzung ---
    out.append('; ======================================================================')
    out.append('; FDC-Treiber Fortsetzung (13F0H-16AFH)')
    out.append('; ======================================================================')
    out.append('; Spur-Seek, Motor-Steuerung, Sektor-Formatierung')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x13F0, 0x16B0))
    out.append('')

    # --- 16B0-16DF: FDC-Konfig-Tabelle ---
    out.append('; --- FDC-Konfigurations-Tabelle (16B0H-16DFH) ---')
    out.extend(data_section(0x16B0, 0x16E0))
    out.append('')

    # --- 16E0-1737: Interrupt-Vektoren ---
    out.append('; --- Interrupt-Vektoren und Parameter (16E0H-1737H) ---')
    out.extend(data_section(0x16E0, 0x1738))
    out.append('')

    # --- 1738-17FF: FDC Low-Level ---
    out.append('; --- FDC Low-Level-Routinen (1738H-17FFH) ---')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x1738, 0x1800))
    out.append('')

    # --- 1800-19BB: WARM_BIOS ---
    out.append('; ======================================================================')
    out.append('; WARM_BIOS: Warmstart-Einstieg (1800H-19BBH)')
    out.append('; ======================================================================')
    out.append('; Wird von WBOOT_JP (0080H: JP 1800H) angesprungen.')
    out.append('; Hardware-Initialisierung, FDC-Setup, CONOUT-Routinen,')
    out.append('; Disk-BIOS-Funktionen (HOME, SELDSK, SETTRK, SETSEC, SETDMA, READ, SECTRN).')
    out.append('; (Disassembliert aus Binaerdaten)')
    out.extend(disasm_section(0x1800, 0x19BC))
    out.append('')

    # --- 19BC-19FF: Boot-Meldung ---
    out.append('; --- Boot-Meldung (19BCH-19FFH) ---')
    raw = read_bin_t2(0x19BC, 0x1A00 - 0x19BC)
    # 19BC: 04 0A = Steuerzeichen (Clear + LF)
    # 19BE-19F0: "CP/A-Bootsystem, Version 05.04.88 laedt @OS.COM ..."
    # 19F1-19F4: 0D 0A 0A 00 = CR LF LF NUL
    # 19F5-19FF: 00 (Fuellbytes)
    out.append(f"\tDB\t04H,0AH\t\t; 19BC: Steuerzeichen")
    out.append(f"\tDB\t'CP/A-Bootsystem, Version 05.04.88 laedt @OS.COM ...',0DH,0AH,0AH,00H\t; 19BE")
    out.append(f'\tDS\t11,00H\t\t; 19F5: Fuellbytes')
    out.append('')

    out.append('\tEND')
    return out


def main():
    # Lese aktuelle bootsec.mac
    with open(MACPATH, 'r') as f:
        mac_lines = f.readlines()

    # Finde die Zeile mit "Boot-BIOS Fortsetzung"
    cut_line = None
    for i, line in enumerate(mac_lines):
        if 'Boot-BIOS Fortsetzung' in line:
            cut_line = i
            break

    if cut_line is None:
        print("FEHLER: 'Boot-BIOS Fortsetzung' nicht in bootsec.mac gefunden!", file=sys.stderr)
        sys.exit(1)

    # Behalte alles VOR der Fortsetzung (inkl. der vorangehenden Leerzeile)
    header = mac_lines[:cut_line]

    # Generiere Ersatz
    replacement = generate_replacement()

    # Schreibe neue Datei
    with open(MACPATH, 'w') as f:
        for line in header:
            f.write(line)
        for line in replacement:
            f.write(line + '\n')

    print(f"bootsec.mac aktualisiert: {cut_line} Header-Zeilen + {len(replacement)} neue Zeilen")


if __name__ == '__main__':
    main()
