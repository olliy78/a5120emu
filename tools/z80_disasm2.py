#!/usr/bin/env python3
"""
Comprehensive Z80 Disassembler for Robotron A5120 format.com
Multi-pass with string detection, data area identification, proper labeling.
"""

import sys
import struct
import re

ORG = 0x0100

def read_binary(path):
    with open(path, 'rb') as f:
        return f.read()

# =================== Z80 INSTRUCTION TABLE ===================
# Each entry: (mnemonic_template, length, operand_types)
# Operand types: 'n'=byte, 'nn'=word, 'e'=relative, 'd'=displacement

def signed8(b):
    return b - 256 if b >= 128 else b

class Z80Disassembler:
    def __init__(self, data, org=0x0100):
        self.data = data
        self.org = org
        self.labels = {}  # addr -> label_name
        self.code_addrs = set()  # addresses that are code
        self.data_ranges = []  # (start, end) ranges that are data
        self.string_ranges = []  # (start, end) ranges that are strings
        self.comments = {}  # addr -> comment
        self.instructions = {}  # addr -> (mnemonic, length, raw_bytes)
        self.call_targets = set()
        self.jump_targets = set()
        
    def byte_at(self, addr):
        offset = addr - self.org
        if 0 <= offset < len(self.data):
            return self.data[offset]
        return None
    
    def word_at(self, addr):
        offset = addr - self.org
        if 0 <= offset + 1 < len(self.data):
            return self.data[offset] | (self.data[offset+1] << 8)
        return None
    
    def decode_one(self, addr):
        """Decode one Z80 instruction at addr. Returns (mnemonic, length, targets, is_end)"""
        offset = addr - self.org
        if offset < 0 or offset >= len(self.data):
            return None, 0, [], False
        
        d = self.data
        o = offset
        
        def b(n):
            if o+n < len(d): return d[o+n]
            return 0
        
        def w(n):
            if o+n+1 < len(d): return d[o+n] | (d[o+n+1] << 8)
            return 0
        
        def rel(n):
            v = b(n)
            return addr + n + 1 + (v - 256 if v >= 128 else v)
        
        opcode = b(0)
        targets = []
        is_end = False  # True if this instruction ends a basic block (unconditional jump/ret)
        
        # === Prefix CB ===
        if opcode == 0xCB:
            op2 = b(1)
            r_names = ['B','C','D','E','H','L','(HL)','A']
            r = r_names[op2 & 7]
            bit = (op2 >> 3) & 7
            group = (op2 >> 6) & 3
            if group == 0:
                ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
                return f"{ops[bit]} {r}", 2, [], False
            elif group == 1:
                return f"BIT {bit},{r}", 2, [], False
            elif group == 2:
                return f"RES {bit},{r}", 2, [], False
            else:
                return f"SET {bit},{r}", 2, [], False
        
        # === Prefix ED ===
        if opcode == 0xED:
            op2 = b(1)
            rp_names = ['BC','DE','HL','SP']
            rp = rp_names[(op2 >> 4) & 3]
            
            ed_map = {
                0x40: ("IN B,(C)", 2), 0x41: ("OUT (C),B", 2),
                0x42: (f"SBC HL,BC", 2), 0x43: (f"LD ({w(2):04X}H),BC", 4),
                0x44: ("NEG", 2), 0x45: ("RETN", 2),
                0x46: ("IM 0", 2), 0x47: ("LD I,A", 2),
                0x48: ("IN C,(C)", 2), 0x49: ("OUT (C),C", 2),
                0x4A: (f"ADC HL,BC", 2), 0x4B: (f"LD BC,({w(2):04X}H)", 4),
                0x4C: ("NEG", 2), 0x4D: ("RETI", 2),
                0x4E: ("IM 0", 2), 0x4F: ("LD R,A", 2),
                0x50: ("IN D,(C)", 2), 0x51: ("OUT (C),D", 2),
                0x52: (f"SBC HL,DE", 2), 0x53: (f"LD ({w(2):04X}H),DE", 4),
                0x54: ("NEG", 2), 0x55: ("RETN", 2),
                0x56: ("IM 1", 2), 0x57: ("LD A,I", 2),
                0x58: ("IN E,(C)", 2), 0x59: ("OUT (C),E", 2),
                0x5A: (f"ADC HL,DE", 2), 0x5B: (f"LD DE,({w(2):04X}H)", 4),
                0x5C: ("NEG", 2), 0x5D: ("RETN", 2),
                0x5E: ("IM 2", 2), 0x5F: ("LD A,R", 2),
                0x60: ("IN H,(C)", 2), 0x61: ("OUT (C),H", 2),
                0x62: (f"SBC HL,HL", 2), 0x63: (f"LD ({w(2):04X}H),HL", 4),
                0x64: ("NEG", 2), 0x65: ("RETN", 2),
                0x66: ("IM 0", 2), 0x67: ("RRD", 2),
                0x68: ("IN L,(C)", 2), 0x69: ("OUT (C),L", 2),
                0x6A: (f"ADC HL,HL", 2), 0x6B: (f"LD HL,({w(2):04X}H)", 4),
                0x6C: ("NEG", 2), 0x6D: ("RETN", 2),
                0x6E: ("IM 0", 2), 0x6F: ("RLD", 2),
                0x70: ("IN (C)", 2), 0x71: ("OUT (C),0", 2),
                0x72: (f"SBC HL,SP", 2), 0x73: (f"LD ({w(2):04X}H),SP", 4),
                0x74: ("NEG", 2), 0x75: ("RETN", 2),
                0x76: ("IM 1", 2), 0x77: ("NOP", 2),
                0x78: ("IN A,(C)", 2), 0x79: ("OUT (C),A", 2),
                0x7A: (f"ADC HL,SP", 2), 0x7B: (f"LD SP,({w(2):04X}H)", 4),
                0x7C: ("NEG", 2), 0x7D: ("RETN", 2),
                0x7E: ("IM 2", 2), 0x7F: ("NOP", 2),
                0xA0: ("LDI", 2), 0xA1: ("CPI", 2),
                0xA2: ("INI", 2), 0xA3: ("OUTI", 2),
                0xA8: ("LDD", 2), 0xA9: ("CPD", 2),
                0xAA: ("IND", 2), 0xAB: ("OUTD", 2),
                0xB0: ("LDIR", 2), 0xB1: ("CPIR", 2),
                0xB2: ("INIR", 2), 0xB3: ("OTIR", 2),
                0xB8: ("LDDR", 2), 0xB9: ("CPDR", 2),
                0xBA: ("INDR", 2), 0xBB: ("OTDR", 2),
            }
            if op2 in ed_map:
                mn, ln = ed_map[op2]
                end = op2 in (0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D)
                return mn, ln, [], end
            return f"DB 0EDH,{op2:02X}H", 2, [], False
        
        # === Prefix DD/FD (IX/IY) ===
        if opcode in (0xDD, 0xFD):
            ir = "IX" if opcode == 0xDD else "IY"
            op2 = b(1)
            
            # DD CB dd xx - bit operations on (IX+d)
            if op2 == 0xCB:
                disp = signed8(b(2))
                op3 = b(3)
                ds = f"+{disp}" if disp >= 0 else f"{disp}"
                bit = (op3 >> 3) & 7
                group = (op3 >> 6) & 3
                if group == 0:
                    ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
                    if (op3 & 7) == 6:
                        return f"{ops[bit]} ({ir}{ds})", 4, [], False
                    else:
                        r_names = ['B','C','D','E','H','L','(HL)','A']
                        return f"{ops[bit]} ({ir}{ds}),{r_names[op3&7]}", 4, [], False
                elif group == 1:
                    return f"BIT {bit},({ir}{ds})", 4, [], False
                elif group == 2:
                    if (op3 & 7) == 6:
                        return f"RES {bit},({ir}{ds})", 4, [], False
                    else:
                        r_names = ['B','C','D','E','H','L','(HL)','A']
                        return f"RES {bit},({ir}{ds}),{r_names[op3&7]}", 4, [], False
                else:
                    if (op3 & 7) == 6:
                        return f"SET {bit},({ir}{ds})", 4, [], False
                    else:
                        r_names = ['B','C','D','E','H','L','(HL)','A']
                        return f"SET {bit},({ir}{ds}),{r_names[op3&7]}", 4, [], False
            
            disp = signed8(b(2))
            ds = f"+{disp}" if disp >= 0 else f"{disp}"
            
            dd_map = {
                0x09: (f"ADD {ir},BC", 2), 0x19: (f"ADD {ir},DE", 2),
                0x21: (f"LD {ir},{w(2):04X}H", 4),
                0x22: (f"LD ({w(2):04X}H),{ir}", 4),
                0x23: (f"INC {ir}", 2),
                0x24: (f"INC {ir}H", 2), 0x25: (f"DEC {ir}H", 2),
                0x26: (f"LD {ir}H,{b(2):02X}H", 3),
                0x29: (f"ADD {ir},{ir}", 2),
                0x2A: (f"LD {ir},({w(2):04X}H)", 4),
                0x2B: (f"DEC {ir}", 2),
                0x2C: (f"INC {ir}L", 2), 0x2D: (f"DEC {ir}L", 2),
                0x2E: (f"LD {ir}L,{b(2):02X}H", 3),
                0x34: (f"INC ({ir}{ds})", 3),
                0x35: (f"DEC ({ir}{ds})", 3),
                0x36: (f"LD ({ir}{ds}),{b(3):02X}H", 4),
                0x39: (f"ADD {ir},SP", 2),
                0x44: (f"LD B,{ir}H", 2), 0x45: (f"LD B,{ir}L", 2),
                0x46: (f"LD B,({ir}{ds})", 3),
                0x4C: (f"LD C,{ir}H", 2), 0x4D: (f"LD C,{ir}L", 2),
                0x4E: (f"LD C,({ir}{ds})", 3),
                0x54: (f"LD D,{ir}H", 2), 0x55: (f"LD D,{ir}L", 2),
                0x56: (f"LD D,({ir}{ds})", 3),
                0x5C: (f"LD E,{ir}H", 2), 0x5D: (f"LD E,{ir}L", 2),
                0x5E: (f"LD E,({ir}{ds})", 3),
                0x60: (f"LD {ir}H,B", 2), 0x61: (f"LD {ir}H,C", 2),
                0x62: (f"LD {ir}H,D", 2), 0x63: (f"LD {ir}H,E", 2),
                0x64: (f"LD {ir}H,{ir}H", 2), 0x65: (f"LD {ir}H,{ir}L", 2),
                0x66: (f"LD H,({ir}{ds})", 3),
                0x67: (f"LD {ir}H,A", 2),
                0x68: (f"LD {ir}L,B", 2), 0x69: (f"LD {ir}L,C", 2),
                0x6A: (f"LD {ir}L,D", 2), 0x6B: (f"LD {ir}L,E", 2),
                0x6C: (f"LD {ir}L,{ir}H", 2), 0x6D: (f"LD {ir}L,{ir}L", 2),
                0x6E: (f"LD L,({ir}{ds})", 3),
                0x6F: (f"LD {ir}L,A", 2),
                0x70: (f"LD ({ir}{ds}),B", 3), 0x71: (f"LD ({ir}{ds}),C", 3),
                0x72: (f"LD ({ir}{ds}),D", 3), 0x73: (f"LD ({ir}{ds}),E", 3),
                0x74: (f"LD ({ir}{ds}),H", 3), 0x75: (f"LD ({ir}{ds}),L", 3),
                0x77: (f"LD ({ir}{ds}),A", 3),
                0x7C: (f"LD A,{ir}H", 2), 0x7D: (f"LD A,{ir}L", 2),
                0x7E: (f"LD A,({ir}{ds})", 3),
                0x84: (f"ADD A,{ir}H", 2), 0x85: (f"ADD A,{ir}L", 2),
                0x86: (f"ADD A,({ir}{ds})", 3),
                0x8C: (f"ADC A,{ir}H", 2), 0x8D: (f"ADC A,{ir}L", 2),
                0x8E: (f"ADC A,({ir}{ds})", 3),
                0x94: (f"SUB {ir}H", 2), 0x95: (f"SUB {ir}L", 2),
                0x96: (f"SUB ({ir}{ds})", 3),
                0x9C: (f"SBC A,{ir}H", 2), 0x9D: (f"SBC A,{ir}L", 2),
                0x9E: (f"SBC A,({ir}{ds})", 3),
                0xA4: (f"AND {ir}H", 2), 0xA5: (f"AND {ir}L", 2),
                0xA6: (f"AND ({ir}{ds})", 3),
                0xAC: (f"XOR {ir}H", 2), 0xAD: (f"XOR {ir}L", 2),
                0xAE: (f"XOR ({ir}{ds})", 3),
                0xB4: (f"OR {ir}H", 2), 0xB5: (f"OR {ir}L", 2),
                0xB6: (f"OR ({ir}{ds})", 3),
                0xBC: (f"CP {ir}H", 2), 0xBD: (f"CP {ir}L", 2),
                0xBE: (f"CP ({ir}{ds})", 3),
                0xE1: (f"POP {ir}", 2), 0xE3: (f"EX (SP),{ir}", 2),
                0xE5: (f"PUSH {ir}", 2), 0xE9: (f"JP ({ir})", 2),
                0xF9: (f"LD SP,{ir}", 2),
            }
            if op2 in dd_map:
                mn, ln = dd_map[op2]
                end = (op2 == 0xE9)  # JP (IX/IY) is end of block
                return mn, ln, [], end
            # Fall through - treat as NOP + next instruction
            return f"DB {opcode:02X}H", 1, [], False
        
        # === Main unprefixed opcodes ===
        r_names = ['B','C','D','E','H','L','(HL)','A']
        rp_names = ['BC','DE','HL','SP']
        rp2_names = ['BC','DE','HL','AF']
        cc_names = ['NZ','Z','NC','C','PO','PE','P','M']
        
        # 8-bit load group: LD r,r'
        if (opcode & 0xC0) == 0x40 and opcode != 0x76:  # HALT is 76
            dst = r_names[(opcode >> 3) & 7]
            src = r_names[opcode & 7]
            return f"LD {dst},{src}", 1, [], False
        
        # ALU group: op A,r
        if (opcode & 0xC0) == 0x80:
            alu_ops = ['ADD A,','ADC A,','SUB ','SBC A,','AND ','XOR ','OR ','CP ']
            op_name = alu_ops[(opcode >> 3) & 7]
            src = r_names[opcode & 7]
            return f"{op_name}{src}", 1, [], False
        
        single = {
            0x00: ("NOP", 1, False),
            0x02: ("LD (BC),A", 1, False),
            0x07: ("RLCA", 1, False),
            0x08: ("EX AF,AF'", 1, False),
            0x0A: ("LD A,(BC)", 1, False),
            0x0F: ("RRCA", 1, False),
            0x12: ("LD (DE),A", 1, False),
            0x17: ("RLA", 1, False),
            0x1A: ("LD A,(DE)", 1, False),
            0x1F: ("RRA", 1, False),
            0x22: (f"LD ({w(2):04X}H),HL", 3, False),
            0x27: ("DAA", 1, False),
            0x2A: (f"LD HL,({w(2):04X}H)", 3, False),
            0x2F: ("CPL", 1, False),
            0x32: (f"LD ({w(2):04X}H),A", 3, False),
            0x37: ("SCF", 1, False),
            0x3A: (f"LD A,({w(2):04X}H)", 3, False),
            0x3F: ("CCF", 1, False),
            0x76: ("HALT", 1, True),
            0xC9: ("RET", 1, True),
            0xD9: ("EXX", 1, False),
            0xE3: ("EX (SP),HL", 1, False),
            0xE9: ("JP (HL)", 1, True),
            0xEB: ("EX DE,HL", 1, False),
            0xF3: ("DI", 1, False),
            0xFB: ("EI", 1, False),
        }
        
        if opcode in single:
            mn, ln, end = single[opcode]
            return mn, ln, [], end
        
        # LD r,n
        if (opcode & 0xC7) == 0x06:
            dst = r_names[(opcode >> 3) & 7]
            return f"LD {dst},{b(1):02X}H", 2, [], False
        
        # INC r / DEC r
        if (opcode & 0xC7) == 0x04:
            r = r_names[(opcode >> 3) & 7]
            return f"INC {r}", 1, [], False
        if (opcode & 0xC7) == 0x05:
            r = r_names[(opcode >> 3) & 7]
            return f"DEC {r}", 1, [], False
        
        # 16-bit LD
        if (opcode & 0xCF) == 0x01:
            rp = rp_names[(opcode >> 4) & 3]
            return f"LD {rp},{w(1):04X}H", 3, [], False
        
        # ADD HL,rp
        if (opcode & 0xCF) == 0x09:
            rp = rp_names[(opcode >> 4) & 3]
            return f"ADD HL,{rp}", 1, [], False
        
        # INC rp / DEC rp
        if (opcode & 0xCF) == 0x03:
            rp = rp_names[(opcode >> 4) & 3]
            return f"INC {rp}", 1, [], False
        if (opcode & 0xCF) == 0x0B:
            rp = rp_names[(opcode >> 4) & 3]
            return f"DEC {rp}", 1, [], False
        
        # PUSH/POP
        if (opcode & 0xCF) == 0xC5:
            rp = rp2_names[(opcode >> 4) & 3]
            return f"PUSH {rp}", 1, [], False
        if (opcode & 0xCF) == 0xC1:
            rp = rp2_names[(opcode >> 4) & 3]
            return f"POP {rp}", 1, [], False
        
        # JP nn
        if opcode == 0xC3:
            target = w(1)
            return f"JP {target:04X}H", 3, [target], True
        
        # JP cc,nn
        if (opcode & 0xC7) == 0xC2:
            cc = cc_names[(opcode >> 3) & 7]
            target = w(1)
            return f"JP {cc},{target:04X}H", 3, [target], False
        
        # JR e
        if opcode == 0x18:
            target = rel(1)
            return f"JR {target:04X}H", 2, [target], True
        
        # JR cc,e  (NZ,Z,NC,C)
        if opcode in (0x20, 0x28, 0x30, 0x38):
            cc = ['NZ','Z','NC','C'][(opcode - 0x20) >> 3]
            target = rel(1)
            return f"JR {cc},{target:04X}H", 2, [target], False
        
        # DJNZ e
        if opcode == 0x10:
            target = rel(1)
            return f"DJNZ {target:04X}H", 2, [target], False
        
        # CALL nn
        if opcode == 0xCD:
            target = w(1)
            return f"CALL {target:04X}H", 3, [target], False
        
        # CALL cc,nn
        if (opcode & 0xC7) == 0xC4:
            cc = cc_names[(opcode >> 3) & 7]
            target = w(1)
            return f"CALL {cc},{target:04X}H", 3, [target], False
        
        # RET cc
        if (opcode & 0xC7) == 0xC0:
            cc = cc_names[(opcode >> 3) & 7]
            return f"RET {cc}", 1, [], False
        
        # RST
        if (opcode & 0xC7) == 0xC7:
            target = opcode & 0x38
            return f"RST {target:02X}H", 1, [target], False
        
        # ALU A,n
        if (opcode & 0xC7) == 0xC6:
            alu_ops = ['ADD A,','ADC A,','SUB ','SBC A,','AND ','XOR ','OR ','CP ']
            op_name = alu_ops[(opcode >> 3) & 7]
            return f"{op_name}{b(1):02X}H", 2, [], False
        
        # IN A,(n) / OUT (n),A
        if opcode == 0xDB:
            return f"IN A,({b(1):02X}H)", 2, [], False
        if opcode == 0xD3:
            return f"OUT ({b(1):02X}H),A", 2, [], False
        
        # Fallback
        return f"DB {opcode:02X}H", 1, [], False
    
    def recursive_decode(self, start_addr, visited=None):
        """Recursively decode instructions following control flow"""
        if visited is None:
            visited = set()
        
        addr = start_addr
        while True:
            if addr in visited:
                break
            offset = addr - self.org
            if offset < 0 or offset >= len(self.data):
                break
            
            visited.add(addr)
            self.code_addrs.add(addr)
            
            mnemonic, length, targets, is_end = self.decode_one(addr)
            if length == 0:
                break
            
            raw = self.data[offset:offset+length]
            self.instructions[addr] = (mnemonic, length, raw)
            
            # Mark all bytes of this instruction
            for i in range(1, length):
                self.code_addrs.add(addr + i)
            
            for t in targets:
                if self.org <= t < self.org + len(self.data):
                    self.jump_targets.add(t)
            
            # Check for CALL targets specifically
            if self.data[offset] == 0xCD or (self.data[offset] & 0xC7) == 0xC4:
                for t in targets:
                    if self.org <= t < self.org + len(self.data):
                        self.call_targets.add(t)
            
            if is_end:
                # Follow targets for jumps (but not for RET)
                for t in targets:
                    if self.org <= t < self.org + len(self.data):
                        self.recursive_decode(t, visited)
                break
            else:
                # Follow fall-through and branch targets
                for t in targets:
                    if self.org <= t < self.org + len(self.data):
                        self.recursive_decode(t, visited)
                addr += length
    
    def linear_decode(self, start, end):
        """Linearly decode a range, for code areas missed by recursive descent"""
        addr = start
        while addr < end:
            if addr in self.instructions:
                _, length, _ = self.instructions[addr]
                addr += length
                continue
            
            offset = addr - self.org
            if offset >= len(self.data):
                break
            
            mnemonic, length, targets, is_end = self.decode_one(addr)
            if length == 0:
                addr += 1
                continue
            
            raw = self.data[offset:offset+length]
            self.instructions[addr] = (mnemonic, length, raw)
            self.code_addrs.add(addr)
            for i in range(1, length):
                self.code_addrs.add(addr + i)
            
            for t in targets:
                if self.org <= t < self.org + len(self.data):
                    self.jump_targets.add(t)
                    if self.data[offset] == 0xCD or (self.data[offset] & 0xC7) == 0xC4:
                        self.call_targets.add(t)
            
            addr += length
    
    def is_printable(self, b):
        return 0x20 <= b <= 0x7E or b in (0x0D, 0x0A)
    
    def find_string_at(self, offset):
        """Find string length starting at offset, returns 0 if not a string"""
        i = offset
        printable_count = 0
        while i < len(self.data):
            b = self.data[i]
            if self.is_printable(b):
                printable_count += 1
                i += 1
            elif b == 0x24 and printable_count >= 3:  # '$' terminator (BDOS string)
                return i - offset + 1
            elif b == 0x00 and printable_count >= 3:
                return i - offset
            else:
                break
        if printable_count >= 5:
            return printable_count
        return 0


def format_label(addr, call_targets, jump_targets):
    """Generate a label name for an address"""
    if addr in call_targets:
        return f"sub_{addr:04X}"
    else:
        return f"L{addr:04X}"


def main():
    path = '/home/olliy/projects/a5120emu/boot_disk/format.com'
    data = read_binary(path)
    
    dis = Z80Disassembler(data)
    
    # First: find the entry point and do recursive descent
    print(f"; Recursive decode from entry point...", file=sys.stderr)
    dis.recursive_decode(0x0100)
    
    # Also decode from all discovered call targets
    all_targets = set(dis.jump_targets) | set(dis.call_targets)
    for t in sorted(all_targets):
        if t not in dis.instructions and dis.org <= t < dis.org + len(data):
            dis.recursive_decode(t)
    
    # Linear decode code areas that might have been missed
    # The main code area seems to be roughly 0x0100-0x0FBC based on string analysis
    dis.linear_decode(0x0100, 0x0FC0)
    
    # Also linearly decode 0x1E00-0x2200 area which may have code (boot loader code)
    dis.linear_decode(0x1E00, 0x2200)
    
    # Identify string/data areas
    # Let's find strings in non-code areas
    string_regions = []
    i = 0
    while i < len(data):
        addr = i + dis.org
        if addr not in dis.code_addrs:
            slen = dis.find_string_at(i)
            if slen >= 5:
                string_regions.append((addr, slen))
                i += slen
                continue
        i += 1
    
    # Build label map
    all_label_addrs = dis.jump_targets | dis.call_targets
    # Also add addresses that are referenced in LD instructions
    label_map = {}
    for addr in sorted(all_label_addrs):
        label_map[addr] = format_label(addr, dis.call_targets, dis.jump_targets)
    
    # Now produce the output
    output_lines = []
    
    # Header
    output_lines.append("; ============================================================================")
    output_lines.append("; FORMAT.COM - Floppy Disk Formatter for Robotron A5120")
    output_lines.append("; CPA/CP/M 2.2 System")
    output_lines.append(f"; Size: {len(data)} bytes (0x{len(data):04X})")
    output_lines.append(f"; Load address: {dis.org:04X}H - {dis.org+len(data)-1:04X}H")
    output_lines.append("; ============================================================================")
    output_lines.append("")
    output_lines.append("; --- System Constants ---")
    output_lines.append("BDOS\tEQU\t0005H\t\t; BDOS entry point")
    output_lines.append("WBOOT\tEQU\t0000H\t\t; Warm boot entry")
    output_lines.append("WBOOTV\tEQU\t0001H\t\t; Warm boot vector address")
    output_lines.append("CPMEXT\tEQU\t004EH\t\t; CPA extensions pointer")
    output_lines.append("FCB1\tEQU\t005CH\t\t; Default FCB 1")
    output_lines.append("DMA\tEQU\t0080H\t\t; Default DMA buffer")
    output_lines.append("")
    output_lines.append("; --- BDOS Functions ---")
    output_lines.append("C_WRITE\tEQU\t02H\t\t; Console output")
    output_lines.append("C_RAWIO\tEQU\t06H\t\t; Direct console I/O")
    output_lines.append("P_STR\tEQU\t09H\t\t; Print string ($-terminated)")
    output_lines.append("C_READSTR\tEQU\t0AH\t; Read console buffer")
    output_lines.append("C_STAT\tEQU\t0BH\t\t; Console status")
    output_lines.append("S_BDOSVER\tEQU\t0CH\t; Return version number")
    output_lines.append("DRV_SET\tEQU\t0EH\t\t; Select disk")
    output_lines.append("F_SFIRST\tEQU\t11H\t; Search for first")
    output_lines.append("")
    output_lines.append("; --- FDC PIO Ports (K5122) ---")
    output_lines.append("PIO_A_DATA\tEQU\t10H\t; Control PIO A data")
    output_lines.append("PIO_A_CTRL\tEQU\t11H\t; Control PIO A control")
    output_lines.append("PIO_B_DATA\tEQU\t12H\t; Control PIO B data (status)")
    output_lines.append("PIO_DATA\tEQU\t14H\t; Data PIO (raw disk data)")
    output_lines.append("DRV_SELECT\tEQU\t18H\t; Drive select latch")
    output_lines.append("")
    output_lines.append("; --- PIO A Bit Definitions ---")
    output_lines.append("; Bit 0: /WE (write enable, active low)")
    output_lines.append("; Bit 2: /SIDE (0=side 1, 1=side 0)")
    output_lines.append("; Bit 5: SD (step direction: 0=out, 1=in)")
    output_lines.append("; Bit 7: /ST (step pulse, active on rising edge)")
    output_lines.append("")
    output_lines.append("; --- PIO B Bit Definitions ---")
    output_lines.append("; Bit 0: /RDY (0=drive ready)")
    output_lines.append("; Bit 5: /WP (1=not write protected)")
    output_lines.append("; Bit 7: /T0 (0=at track 0)")
    output_lines.append("")
    output_lines.append("; --- CDB (Command Descriptor Block) Offsets ---")
    output_lines.append("CDBFL\tEQU\t0\t\t; Flags (bit2=write, bit5=head-up)")
    output_lines.append("CDBDEV\tEQU\t1\t\t; Logical device 0-3")
    output_lines.append("CDBTRK\tEQU\t2\t\t; Track number")
    output_lines.append("CDBSID\tEQU\t3\t\t; Side number")
    output_lines.append("CDBSEC\tEQU\t4\t\t; Sector number")
    output_lines.append("CDBSLC\tEQU\t5\t\t; Sector length code")
    output_lines.append("CDBSNB\tEQU\t6\t\t; Sector count (0=seek only)")
    output_lines.append("CDBDMA\tEQU\t7\t\t; DMA address (2 bytes)")
    output_lines.append("")
    output_lines.append("; --- IM2 Interrupt Vectors ---")
    output_lines.append("IV_INDEX\tEQU\t0F7E8H\t; Index pulse ISR vector")
    output_lines.append("IV_MARK\tEQU\t0F7EAH\t; Mark ISR vector")
    output_lines.append("")
    output_lines.append("\tORG\t0100H")
    output_lines.append("")
    
    # Now generate the disassembly
    # Create a set of string start addresses for quick lookup
    string_starts = {}
    for (sa, sl) in string_regions:
        string_starts[sa] = sl
    
    addr = dis.org
    end_addr = dis.org + len(data)
    
    while addr < end_addr:
        offset = addr - dis.org
        
        # Print label if this is a target
        if addr in label_map:
            output_lines.append(f"{label_map[addr]}:")
        
        # Check if this is a string region
        if addr in string_starts and addr not in dis.instructions:
            slen = string_starts[addr]
            # Output string as DB with text
            raw = data[offset:offset+slen]
            # Split into lines at CR/LF boundaries
            line_start = 0
            while line_start < len(raw):
                # Find the end of this text segment
                line_end = line_start
                while line_end < len(raw):
                    b = raw[line_end]
                    if b == 0x24:  # '$' terminator
                        line_end += 1
                        break
                    elif b == 0x00:
                        break
                    line_end += 1
                
                segment = raw[line_start:line_end]
                if len(segment) > 0:
                    parts = []
                    text_buf = ""
                    for b in segment:
                        if 0x20 <= b <= 0x7E:
                            text_buf += chr(b)
                        else:
                            if text_buf:
                                parts.append(f"'{text_buf}'")
                                text_buf = ""
                            parts.append(f"{b:02X}H")
                    if text_buf:
                        parts.append(f"'{text_buf}'")
                    
                    db_str = ",".join(parts)
                    a = addr + line_start
                    output_lines.append(f"\tDB\t{db_str}")
                
                line_start = line_end
                # Skip null terminators
                while line_start < len(raw) and raw[line_start] == 0x00:
                    output_lines.append(f"\tDB\t00H")
                    line_start += 1
            
            addr += slen
            continue
        
        # Check if this is a decoded instruction
        if addr in dis.instructions:
            mnemonic, length, raw_bytes = dis.instructions[addr]
            hex_str = ' '.join(f'{b:02X}' for b in raw_bytes)
            
            # Replace address references with labels
            display = mnemonic
            # Find 4-digit hex addresses in the mnemonic and replace with labels
            def replace_addr(m):
                val_str = m.group(1)
                val = int(val_str, 16)
                if val in label_map:
                    return label_map[val]
                return f"{val:04X}H"
            
            display = re.sub(r'([0-9A-F]{4})H', replace_addr, display)
            
            output_lines.append(f"\t{display:40s}; {addr:04X}: {hex_str}")
            addr += length
        else:
            # Data byte
            b_val = data[offset]
            output_lines.append(f"\tDB\t{b_val:02X}H\t\t\t\t\t; {addr:04X}")
            addr += 1
    
    output_lines.append("")
    output_lines.append("\tEND")
    
    # Write output
    result = '\n'.join(output_lines)
    print(result)

if __name__ == '__main__':
    main()
