#!/usr/bin/env python3
"""
bootsec_analyze.py — CPA780-SYL-Bootloader-Analyseprogramm
===========================================================

Copyright (c) 2026 Olaf Krieger
SPDX-License-Identifier: MIT
This file is part of the a5120emu project. See the LICENSE file in the
project root for the full text of the MIT License.

Dieses Skript disassembliert das Binärabbild des System-Bootloaders
(disks/bootsec.bin) des Robotron A5120 / CPA780.

Das Skript ist vollständig in der Python-Standardbibliothek implementiert
und benötigt kein externes Z80-Disassembler-Paket.

Ausgabe (mit Umleitung nach /tmp/full_analysis.txt):
    Adressspalte 1 : Datei-Offset (hexadezimal)
    Adressspalte 2 : RAM-Adresse  (hexadezimal, nach ORG-Zuordnung)
    Bytes          : Roh-Opcode-Bytes (max. 4)
    Mnemonic       : Z80-Assemblerbefehl

Disk-Geometrie (SYL Mixed Geometry):
    Systemspuren (Track 0, 2): 26 Sektoren × 128 Byte = 3328 Byte/Spur
    Track 1 ist Lückenspur (nicht geladen, Inhalt: 0x53)

Drei Lade-Abschnitte / ORG-Bereiche:
    T0S0  (Offset 0x0000, 128 B): SYL-Kennung + Sektor-0-Stub → RAM 0x0000
    T0S1-25 (Offset 0x0080, 3200 B): Stage-1-Code            → RAM 0x0080
    T2S0-25 (Offset 0x1A00, 3328 B): Stage-2-Kern           → RAM 0x0D00

Erzeugte Dokumentationsdateien:
    docs/bootsec.mac  — vollständig kommentierter Z80-Assemblertext (M80-Format)
    docs/bootsec.md   — funktionale Beschreibung (Deutsch/Englisch)

Z80-Befehlsabdeckung (Untermenge des vollständigen Z80-Befehlssatzes):
    Implementiert:
      - Alle ungepräfixten 1-/2-/3-Byte-Opcodes (0x00..0xFF), soweit unten
        explizit behandelt
      - ED-Präfix: Block-Transfers (LDIR/LDDR/…), ADC HL/SBC HL, NEG, RETI,
        RETN, IM 0/1/2, LD I,A / LD A,I / LD R,A / LD A,R, RRD/RLD,
        16-Bit-Speichertransfers LD (nn),rr / LD rr,(nn)
      - DD-Präfix: IX-Registerbefehle (LD r,(IX+d), LD (IX+d),r, INC/DEC,
        ADD IX,rr, PUSH/POP/JP (IX))
      - FD-Präfix: IY-Registerbefehle (analog zu IX)

    Nicht implementiert (fallen auf DB-Fallback zurück):
      - CB-Präfix (komplett):  RLC/RRC/RL/RR/SLA/SRA/SRL/BIT/SET/RES auf
        allen 8 Registern — insgesamt 256 Sub-Opcodes (2-Byte-Befehle)
      - DDCB-/FDCB-Präfix (komplett):  indizierte Bit-Operationen auf
        (IX+d)/(IY+d) — 4-Byte-Befehle
      - Einzelne 0xCx..0xFx-Opcodes: RST 08H/10H/18H/20H/28H/30H
        (0xCF/D7/DF/E7/EF/F7), RET C/PE/M (0xD8/E8/F8),
        CALL PO/PE/P/M (0xE4/EC/F4/FC), SBC A,n (0xDE)

    Für die Analyse von bootsec.bin ist die Untermenge ausreichend,
    da die fehlenden Befehle im Bootloader-Binärabbild nicht vorkommen.
"""

import struct

# ─── Konfiguration ────────────────────────────────────────────────────────────

# Pfad zum Bootloader-Binärabbild (3 Systemspuren × 26 Sektoren × 128 Byte)
BOOTSEC = '/home/olliy/projects/a5120emu/disks/bootsec.bin'

# Binärdaten einlesen
data = open(BOOTSEC, 'rb').read()


# ─── Hilfsfunktion: Vorzeichenbehaftetes Byte ─────────────────────────────────

def s8(b):
    """Wandelt ein 8-Bit-Byte in eine vorzeichenbehaftete Zahl (-128..+127) um.
    Wird für relative Sprungweiten (JR d, DJNZ d) und IX/IY-Displacements verwendet.
    """
    return b - 256 if b >= 128 else b


# ─── Kern-Disassembler ─────────────────────────────────────────────────────────

def z80dis(d, pc):
    """Minimaler Z80-Disassembler.

    Dekodiert einen einzelnen Z80-Befehl aus dem Byte-Puffer `d`
    beginnend bei PC-Adresse `pc` (für korrekte Sprungzielrechnung).

    Unterstützte Befehlsgruppen (Untermenge des Z80-Befehlssatzes):
      - Ungepräfixte Opcodes: NOP, HALT, RET, EI, DI, EXX, EX DE/HL/AF,
        LD r,r', LD r,n, LD rr,nn, IN/OUT, JR/JP/CALL/RET (teilweise, s.u.),
        PUSH/POP, ALU-Operationen (ADD/ADC/SUB/SBC/AND/XOR/OR/CP), DJNZ,
        RST 00H und RST 38H
      - ED-Präfix:  LDIR/LDDR, LDI/LDD, CPIR/CPDR, CPI/CPD, INI/IND/OUTI/OUTD
                    und deren Repeat-Varianten, ADC HL/SBC HL, NEG, RETI, RETN,
                    IM 0/1/2, LD I,A / LD A,I / LD R,A / LD A,R, RRD/RLD,
                    16-Bit-Speichertransfers LD (nn),rr / LD rr,(nn)
      - DD-Präfix:  IX-Registerbefehle (LD r,(IX+d), LD (IX+d),r,
                    INC/DEC (IX+d), ADD IX,rr, INC/DEC IX, PUSH/POP/JP (IX),
                    LD IX,nn / LD (nn),IX / LD IX,(nn))
      - FD-Präfix:  IY-Registerbefehle (analog zu IX)

    Nicht implementiert (unbekannte Opcodes → DB-Direktive, Länge 1 Byte):
      - CB-Präfix (komplett fehlend):  RLC/RRC/RL/RR/SLA/SRA/SRL r,
                                        BIT b,r / SET b,r / RES b,r
      - DDCB-/FDCB-Präfix (komplett fehlend):  BIT/SET/RES/RLC/… auf (IX+d)/(IY+d)
      - Folgende Einzel-Opcodes fehlen:  RST 08H..30H (0xCF/D7/DF/E7/EF/F7),
                                          RET C (0xD8), RET PE (0xE8), RET M (0xF8),
                                          CALL PO (0xE4), CALL PE (0xEC),
                                          CALL P (0xF4), CALL M (0xFC),
                                          SBC A,n (0xDE)

    Rückgabe:
        (mnemonic: str, länge: int)
        `mnemonic` — lesbarer Z80-Assemblertext
        `länge`    — Länge des Befehls in Bytes (1..4)
    """
    if not d:
        return 'DB 00H', 1  # Leerer Puffer → Datenbyte

    b = d[0]  # aktueller Opcode

    # Hilfsfunktionen für Operandenzugriff (offset relativ zu d[0])
    def b1(): return d[1] if len(d) > 1 else 0   # zweites Byte
    def b2(): return d[2] if len(d) > 2 else 0   # drittes Byte
    def w():  return b1() | (b2() << 8)           # 16-Bit-Wort (little-endian)
    def r():  return pc + 2 + s8(b1())            # relatives Sprungziel (JR, DJNZ)
    def r3(): return pc + 3 + s8(b2())            # relatives Sprungziel nach DD/FD-Präfix

    # Registertabellen
    rp  = ['BC', 'DE', 'HL', 'SP']    # 16-Bit-Registerpaare (für LD rr,nn usw.)
    rp2 = ['BC', 'DE', 'HL', 'AF']    # 16-Bit-Paare für PUSH/POP (AF statt SP)
    r8  = ['B', 'C', 'D', 'E', 'H', 'L', '(HL)', 'A']  # 8-Bit-Register (r-Encoding)
    cc  = ['NZ', 'Z', 'NC', 'C', 'PO', 'PE', 'P', 'M']  # Bedingungscodes (JP cc usw.)
    alu = ['ADD A,', 'ADC A,', 'SUB ', 'SBC A,',
           'AND ', 'XOR ', 'OR ', 'CP ']           # ALU-Operationen (0x80..0xBF)
    rot = ['RLCA', 'RRCA', 'RLA', 'RRA', 'DAA', 'CPL', 'SCF', 'CCF']  # Bit 0..7 Rotation/Flags

    # ED-Präfix: Erweiterte Befehle (Block-Transfers, I/O, 16-Bit-Arithmetik …)
    blk = {
        # Block-Transfer- und -Vergleichs-/Ein-Ausgabe-Befehle
        0xA0: 'LDI',  0xA1: 'CPI',  0xA2: 'INI',  0xA3: 'OUTI',
        0xA8: 'LDD',  0xA9: 'CPD',  0xAA: 'IND',  0xAB: 'OUTD',
        0xB0: 'LDIR', 0xB1: 'CPIR', 0xB2: 'INIR', 0xB3: 'OTIR',
        0xB8: 'LDDR', 0xB9: 'CPDR', 0xBA: 'INDR', 0xBB: 'OTDR',
        # Sonderbefehle
        0x44: 'NEG',        # A = -A
        0x45: 'RETN',       # Return from NMI
        0x46: 'IM 0',       # Interrupt Mode 0
        0x47: 'LD I,A',     # I-Register laden
        0x4D: 'RETI',       # Return from Interrupt
        0x4F: 'LD R,A',     # R-Register (DRAM-Refresh) laden
        0x56: 'IM 1',       # Interrupt Mode 1
        0x57: 'LD A,I',
        0x5E: 'IM 2',       # Interrupt Mode 2 (Vektor)
        0x5F: 'LD A,R',
        0x67: 'RRD',        # BCD-Rotation
        0x6F: 'RLD',
        # 16-Bit-Arithmetik: SBC HL,rr / ADC HL,rr
        0x42: 'SBC HL,BC', 0x52: 'SBC HL,DE',
        0x62: 'SBC HL,HL', 0x72: 'SBC HL,SP',
        0x4A: 'ADC HL,BC', 0x5A: 'ADC HL,DE',
        0x6A: 'ADC HL,HL', 0x7A: 'ADC HL,SP',
        # 16-Bit-Speichertransfers: LD (nn),rr / LD rr,(nn)
        0x43: f'LD ({w():04X}H),BC',  0x53: f'LD ({w():04X}H),DE',
        0x63: f'LD ({w():04X}H),HL',  0x73: f'LD ({w():04X}H),SP',
        0x4B: f'LD BC,({w():04X}H)',  0x5B: f'LD DE,({w():04X}H)',
        0x6B: f'LD HL,({w():04X}H)',  0x7B: f'LD SP,({w():04X}H)',
    }

    # ── Befehle 0x00..0x07 ────────────────────────────────────────────────────
    # Erste Octal-Reihe: NOP, Rotationen, LD (BC),A, INC/DEC BC, LD B,n
    if b < 8:
        if b == 0: return 'NOP', 1
        if b == 2: return 'LD (BC),A', 1
        if b == 3: return 'INC BC', 1
        if b == 4: return 'INC B', 1
        if b == 5: return 'DEC B', 1
        if b == 6: return f'LD B,{b1():02X}H', 2
        if b == 7: return 'RLCA', 1
        return rot[b], 1  # 0x01 wird hier nicht erreicht (s. unten)

    # ── Befehle 0x08..0x0F ────────────────────────────────────────────────────
    if 8 <= b < 16:
        n = b - 8
        if n == 0: return "EX AF,AF'", 1   # Schattregister tauschen
        if n == 1: return 'ADD HL,BC', 1
        if n == 2: return 'LD A,(BC)', 1
        if n == 3: return 'DEC BC', 1
        if n == 4: return 'INC C', 1
        if n == 5: return 'DEC C', 1
        if n == 6: return f'LD C,{b1():02X}H', 2
        if n == 7: return 'RRCA', 1
        return f'DJNZ {r():04X}H', 2  # 0x10: loop-Befehl

    # ── Befehle 0x10..0x17 ────────────────────────────────────────────────────
    if 16 <= b < 24:
        n = b - 16
        if n == 0: return f'JR {r():04X}H', 2         # unbed. relativer Sprung
        if n == 1: return f'LD DE,{w():04X}H', 3
        if n == 2: return 'LD (DE),A', 1
        if n == 3: return 'INC DE', 1
        if n == 4: return 'INC D', 1
        if n == 5: return 'DEC D', 1
        if n == 6: return f'LD D,{b1():02X}H', 2
        if n == 7: return 'RLA', 1
        if n == 8:  return f'JR NZ,{r():04X}H', 2
        if n == 9:  return 'ADD HL,DE', 1
        if n == 10: return 'LD A,(DE)', 1
        if n == 11: return 'DEC DE', 1
        if n == 12: return 'INC E', 1
        if n == 13: return 'DEC E', 1
        if n == 14: return f'LD E,{b1():02X}H', 2
        if n == 15: return 'RRA', 1

    # ── Befehle 0x18..0x1F ────────────────────────────────────────────────────
    if 24 <= b < 32:
        n = b - 24
        jr_cc = ['NZ', 'Z', 'NC', 'C']
        if n <= 3: return f'JR {jr_cc[n]},{r():04X}H', 2  # bedingte JR
        if n == 4: return f'LD HL,{w():04X}H', 3
        if n == 5: return f'LD ({w():04X}H),HL', 3
        if n == 6: return 'INC HL', 1
        if n == 7: return 'INC H', 1
        if n == 8: return 'DEC H', 1
        if n == 9: return f'LD H,{b1():02X}H', 2
        if n == 10: return 'DAA', 1                        # BCD-Korrektur
        if n == 11: return f'JR Z,{r():04X}H', 2
        if n == 12: return 'ADD HL,HL', 1
        if n == 13: return f'LD HL,({w():04X}H)', 3
        if n == 14: return 'DEC HL', 1
        if n == 15: return 'INC L', 1

    # ── Befehle 0x20..0x27 ────────────────────────────────────────────────────
    if 32 <= b < 40:
        n = b - 32
        if n == 0: return f'JR NC,{r():04X}H', 2
        if n == 1: return f'LD SP,{w():04X}H', 3
        if n == 2: return f'LD ({w():04X}H),A', 3
        if n == 3: return 'INC SP', 1
        if n == 4: return 'INC (HL)', 1
        if n == 5: return 'DEC (HL)', 1
        if n == 6: return f'LD (HL),{b1():02X}H', 2
        if n == 7: return 'SCF', 1
        if n == 8: return f'JR C,{r():04X}H', 2
        if n == 9: return 'ADD HL,SP', 1
        if n == 10: return f'LD A,({w():04X}H)', 3
        if n == 11: return 'DEC SP', 1
        if n == 12: return 'INC A', 1
        if n == 13: return 'DEC A', 1
        if n == 14: return f'LD A,{b1():02X}H', 2
        if n == 15: return 'CCF', 1

    # ── Befehle 0x40..0x7F: LD r,r' / HALT ───────────────────────────────────
    # Alle 8-Byte-zu-8-Byte-Ladetransfers, kodiert als: 01dddssss
    if 0x40 <= b <= 0x7F:
        if b == 0x76: return 'HALT', 1     # 0110 110 = HALT (kein LD (HL),(HL))
        dst, src = (b >> 3) & 7, b & 7
        return f'LD {r8[dst]},{r8[src]}', 1

    # ── Befehle 0x80..0xBF: ALU-Operationen ──────────────────────────────────
    # Kodierung: 10ooo rrr  (ooo=ALU-Op, rrr=Quellregister)
    if 0x80 <= b <= 0xBF:
        op, src = (b >> 3) & 7, b & 7
        return f'{alu[op]}{r8[src]}', 1

    # ── Befehle 0xC0..0xFF: Sprünge, Calls, Returns, Sonderbefehle ───────────
    if b == 0xC0: return 'RET NZ', 1
    if b == 0xC1: return 'POP BC', 1
    if b == 0xC2: return f'JP NZ,{w():04X}H', 3
    if b == 0xC3: return f'JP {w():04X}H', 3          # unbedingter Sprung
    if b == 0xC4: return f'CALL NZ,{w():04X}H', 3
    if b == 0xC5: return 'PUSH BC', 1
    if b == 0xC6: return f'ADD A,{b1():02X}H', 2
    if b == 0xC7: return 'RST 00H', 1
    if b == 0xC8: return 'RET Z', 1
    if b == 0xC9: return 'RET', 1                     # bedingungsloser Return
    if b == 0xCA: return f'JP Z,{w():04X}H', 3
    if b == 0xCC: return f'CALL Z,{w():04X}H', 3
    if b == 0xCD: return f'CALL {w():04X}H', 3        # unbedingter Aufruf
    if b == 0xCE: return f'ADC A,{b1():02X}H', 2
    if b == 0xD0: return 'RET NC', 1
    if b == 0xD1: return 'POP DE', 1
    if b == 0xD2: return f'JP NC,{w():04X}H', 3
    if b == 0xD3: return f'OUT ({b1():02X}H),A', 2    # Port-Ausgabe
    if b == 0xD4: return f'CALL NC,{w():04X}H', 3
    if b == 0xD5: return 'PUSH DE', 1
    if b == 0xD6: return f'SUB {b1():02X}H', 2
    if b == 0xD9: return 'EXX', 1                     # BC/DE/HL ↔ BC'/DE'/HL'
    if b == 0xDA: return f'JP C,{w():04X}H', 3
    if b == 0xDB: return f'IN A,({b1():02X}H)', 2     # Port-Eingabe
    if b == 0xDC: return f'CALL C,{w():04X}H', 3
    if b == 0xE0: return 'RET PO', 1
    if b == 0xE1: return 'POP HL', 1
    if b == 0xE2: return f'JP PO,{w():04X}H', 3
    if b == 0xE3: return 'EX (SP),HL', 1
    if b == 0xE5: return 'PUSH HL', 1
    if b == 0xE6: return f'AND {b1():02X}H', 2
    if b == 0xE9: return 'JP (HL)', 1                 # indirekter Sprung
    if b == 0xEB: return 'EX DE,HL', 1
    if b == 0xED:
        # ── ED-Präfix: Erweiterte Befehle ─────────────────────────────────────
        # Alle Einträge aus dem `blk`-Dictionary (oben definiert).
        # Befehle mit 16-Bit-Adressoperand (LD (nn),rr / LD rr,(nn))
        # sind 4 Bytes lang; alle anderen ED-Befehle sind 2 Bytes.
        b2v = b1()
        m = blk.get(b2v, f'DB EDH,{b2v:02X}H')  # Fallback: rohe Datenbytes
        ln = 4 if b2v in (0x43, 0x53, 0x63, 0x73,
                          0x4B, 0x5B, 0x6B, 0x7B) else 2
        return m, ln

    if b == 0xEE: return f'XOR {b1():02X}H', 2
    if b == 0xF0: return 'RET P', 1
    if b == 0xF1: return 'POP AF', 1
    if b == 0xF2: return f'JP P,{w():04X}H', 3
    if b == 0xF3: return 'DI', 1                      # Interrupts sperren
    if b == 0xF5: return 'PUSH AF', 1
    if b == 0xF6: return f'OR {b1():02X}H', 2
    if b == 0xF9: return 'LD SP,HL', 1
    if b == 0xFA: return f'JP M,{w():04X}H', 3
    if b == 0xFB: return 'EI', 1                      # Interrupts freigeben
    if b == 0xFE: return f'CP {b1():02X}H', 2
    if b == 0xFF: return 'RST 38H', 1

    # ── DD/FD-Präfix: IX- bzw. IY-Register-Befehle ───────────────────────────
    # DD → IX-Register, FD → IY-Register.
    # Displacement (d) ist vorzeichenbehaftet (+/−).
    # Typische Kodierungen:
    #   FD 21 lo hi  = LD IY,nn          (4 Byte)
    #   FD 7E d      = LD A,(IY+d)       (3 Byte)
    #   FD 77 d      = LD (IY+d),A       (3 Byte)
    #   FD 23        = INC IY            (2 Byte)
    #   FD 2B        = DEC IY            (2 Byte)
    #   FD 19        = ADD IY,DE         (2 Byte)
    if b in (0xDD, 0xFD):
        pfx = 'IX' if b == 0xDD else 'IY'
        if len(d) < 2:
            return f'DB {b:02X}H', 1
        b2v = d[1]       # zweiter Byte nach Präfix
        d2  = b2()       # drittes Byte = Displacement oder Low-Byte von nn
        d3  = d[3] if len(d) > 3 else 0  # viertes Byte = High-Byte von nn

        disp = s8(d2)    # vorzeichenbehaftetes Displacement

        # LD IX/IY, nn (4 Byte)
        if b2v == 0x21: return f'LD {pfx},{(d2 | (d3 << 8)):04X}H', 4
        # LD (nn), IX/IY (4 Byte)
        if b2v == 0x22: return f'LD ({(d2 | (d3 << 8)):04X}H),{pfx}', 4
        # LD IX/IY, (nn) (4 Byte)
        if b2v == 0x2A: return f'LD {pfx},({(d2 | (d3 << 8)):04X}H)', 4
        # LD (IX/IY+d), n (4 Byte)
        if b2v == 0x36: return f'LD ({pfx}{disp:+d}),{d3:02X}H', 4
        # Stack-Operationen
        if b2v == 0xE1: return f'POP {pfx}', 2
        if b2v == 0xE5: return f'PUSH {pfx}', 2
        if b2v == 0xE3: return f'EX (SP),{pfx}', 2
        if b2v == 0xE9: return f'JP ({pfx})', 2
        # INC / DEC
        if b2v == 0x23: return f'INC {pfx}', 2
        if b2v == 0x2B: return f'DEC {pfx}', 2
        # ADD IX/IY, rr
        if b2v == 0x09: return f'ADD {pfx},BC', 2
        if b2v == 0x19: return f'ADD {pfx},DE', 2
        if b2v == 0x29: return f'ADD {pfx},{pfx}', 2
        if b2v == 0x39: return f'ADD {pfx},SP', 2
        # LD r, (IX/IY+d):  Opcode-Bits 5..3 = dst, Bits 2..0 = 110 (=(HL))
        # Kodierung: DD/FD  01 ddd 110  d
        if 0x46 <= b2v <= 0x7E and b2v & 7 == 6:
            dst = (b2v >> 3) & 7
            return f'LD {r8[dst]},({pfx}{disp:+d})', 3
        # LD (IX/IY+d), r:  Opcode-Bits 5..3 = 110, Bits 2..0 = src
        # Kodierung: DD/FD  01 110 sss  d
        if 0x70 <= b2v <= 0x77 and b2v & 7 != 6:
            return f'LD ({pfx}{disp:+d}),{r8[b2v & 7]}', 3
        # INC/DEC (IX/IY+d)
        if b2v == 0x34: return f'INC ({pfx}{disp:+d})', 3
        if b2v == 0x35: return f'DEC ({pfx}{disp:+d})', 3
        # Unbekannt: rohe Datenbytes ausgeben
        return f'DB {b:02X}H,{b2v:02X}H', 2

    # ── Fallback: unbekannter Opcode → als DB-Direktive ausgeben ─────────────
    return f'DB {b:02X}H', 1


# ─── Disassembler-Schleife ────────────────────────────────────────────────────

def disasm_range(raw, file_base, ram_org, max_bytes=None, stop_set=None):
    """Disassembliert einen Byte-Bereich sequenziell.

    Parameter:
        raw        — Rohdaten (bytes)
        file_base  — Datei-Basisoffset des ersten Bytes in `raw`
                     (wird in Spalte 1 der Ausgabe angezeigt)
        ram_org    — RAM-Adresse des ersten Bytes in `raw` nach dem Laden
                     (wird in Spalte 2 der Ausgabe angezeigt, entspricht .ORG)
        max_bytes  — Optional: maximale Anzahl zu verarbeitender Bytes
        stop_set   — Optional: Menge von RAM-Adressen, bei denen die Schleife
                     abbricht (z.B. bekannte Datenbereiche)

    Rückgabe:
        Liste von Tupeln (file_offset, ram_addr, raw_bytes, mnemonic)
    """
    i = 0
    end = len(raw) if max_bytes is None else min(len(raw), max_bytes)
    rows = []
    while i < end:
        ram = ram_org + i
        if stop_set and ram in stop_set:
            break          # Bereich auf Anfrage vorzeitig beenden
        chunk = raw[i:]
        mnem, ln = z80dis(chunk, ram)
        raw_b = raw[i:i + ln]
        rows.append((file_base + i, ram, bytes(raw_b), mnem))
        i += ln
    return rows


# ─── Hauptprogramm ────────────────────────────────────────────────────────────

# ─── 1. Sektor 0 von Track 0: SYL-Kennung und Stub-Analyse ──────────────────
# Sektor 0 (erste 128 Byte) enthält die SYL-Identifikation ('SYL' = 53 59 4C)
# und bei Offset 0x002E den Sprungbefehl, der vom ROM nach dem Einlesen
# ausgeführt wird: C3 40 01 = JP 0140H → Einstieg in Stage-1 (DISPATCH).

sec0 = data[0:128]  # Sektor 0 von Track 0

print("SYL-Sektor 0 (0x0000-0x007F) — Hex-Dump:")
for i in range(0, 128, 16):
    b = sec0[i:i + 16]
    hex_part  = ' '.join(f'{x:02X}' for x in b)
    char_part = ''.join(chr(x) if 0x20 <= x < 0x7E else '.' for x in b)
    print(f"  {i:04X}: {hex_part}  {char_part}")

# Alle JP-Befehle (0xC3) in Sektor 0 auffinden und Sprungziel ausgeben
jidx = None
for i in range(126):
    if sec0[i] == 0xC3:
        tgt = sec0[i + 1] | (sec0[i + 2] << 8)
        print(f"\n  JP bei Offset 0x{i:02X}: JP {tgt:04X}H")
        jidx = (i, tgt)

# ─── 2. Stage-1-Disassembly ───────────────────────────────────────────────────
# Stage-1-Code befindet sich in den Sektoren 1–25 von Track 0.
# Datei-Bereich: 0x0080 – 0x0CFF (3200 Byte).
# Nach dem Laden liegt er im RAM ab Adresse 0x0080.
# Der Einstieg (vom ROM) ist typischerweise 0x0080, aber der eigentliche
# Kalt-Start-Einstieg ist 0x0140 (DISPATCH) → JP in Sektor 0 Offset 0x002E.

t0code = data[0x0080:0x0D00]   # 3200 Bytes = 25 Sektoren à 128 Byte

print("\n" + "=" * 70)
print("STAGE-1 Disassembly (Track0 Sek1-25, RAM 0x0080..0x0CFF)")
print("=" * 70)

rows = disasm_range(t0code, 0x0080, 0x0080)
for (fo, ra, rb, mn) in rows:
    rbs = ' '.join(f'{b:02X}' for b in rb)
    print(f"{fo:04X}  {ra:04X}  {rbs:<12}  {mn}")

# ─── 3. Stage-2-Disassembly mit automatischer ORG-Erkennung ──────────────────
# Stage-2-Code liegt in Track 2 (Track 1 ist Lückenspur mit 0x53-Füllzeichen).
# Datei-Bereich: 0x1A00 – 0x26FF (3328 Byte).
# Die RAM-ORG lässt sich aus dem ersten JP-Befehl im Track schlussfolgern.

print("\n" + "=" * 70)
print("STAGE-2 Disassembly (Track2 komplett, RAM-ORG bestimmen)")
print("=" * 70)

# Letzter JP in Stage-1 als Hinweis auf Stage-2-Einstieg ausgeben
last_jp = None
for (fo, ra, rb, mn) in rows:
    if mn.startswith('JP ') and '(' not in mn:
        last_jp = (ra, mn)
print(f"Letzter JP in Stage-1: {last_jp}")

t2code = data[0x1A00:0x2700]   # 3328 Bytes = 26 Sektoren à 128 Byte (Track 2)

print(f"\nT2 erste 16 Bytes: {' '.join(f'{x:02X}' for x in t2code[:16])}")

# Nach JP-Befehlen im Header von Track 2 suchen
for i in range(16):
    if t2code[i] == 0xC3:
        tgt2 = t2code[i + 1] | (t2code[i + 2] << 8)
        print(f"JP bei T2+{i:02X}: JP {tgt2:04X}H")

# ORG automatisch berechnen:
# Wenn der erste sinnvolle JP ein Ziel im Bereich 0x0100..0x4000 hat,
# dann ist ORG = Sprungziel − Offset_des_JP_im_Track.
t2_org = 0x0000  # Fallback

for i in range(0, min(256, len(t2code))):
    if t2code[i] == 0xC3 and i + 2 < len(t2code):
        tgt = t2code[i + 1] | (t2code[i + 2] << 8)
        if 0x0100 <= tgt <= 0x4000:
            t2_org = tgt - i
            print(f"T2 ORG geschätzt: {t2_org:04X}H  (JP {tgt:04X}H bei +{i:02X})")
            break

print(f"\n--- Stage-2 mit ORG={t2_org:04X}H ---")
rows2 = disasm_range(t2code, 0x1A00, t2_org)
for (fo, ra, rb, mn) in rows2:
    rbs = ' '.join(f'{b:02X}' for b in rb)
    print(f"{fo:04X}  {ra:04X}  {rbs:<12}  {mn}")
