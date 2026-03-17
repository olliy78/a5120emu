#!/usr/bin/env python3
"""
Comprehensive Z80 Disassembler for Robotron A5120 format.com
Produces a fully annotated .mac assembly source file.
"""

import sys
import struct
import re
from collections import defaultdict

ORG = 0x0100

def read_binary(path):
    with open(path, 'rb') as f:
        return f.read()

def signed8(b):
    return b - 256 if b >= 128 else b

class Z80Decoder:
    """Complete Z80 instruction decoder"""
    
    def __init__(self, data, org):
        self.data = data
        self.org = org
    
    def b(self, addr):
        o = addr - self.org
        if 0 <= o < len(self.data):
            return self.data[o]
        return 0
    
    def w(self, addr):
        return self.b(addr) | (self.b(addr+1) << 8)
    
    def decode(self, addr):
        """Returns (mnemonic, length, branch_targets, is_unconditional_end, is_call)"""
        o = addr - self.org
        if o < 0 or o >= len(self.data):
            return None, 0, [], False, False
        
        d = self.data
        opcode = d[o]
        
        def bb(n): return d[o+n] if o+n < len(d) else 0
        def ww(n): return (d[o+n] | (d[o+n+1] << 8)) if o+n+1 < len(d) else 0
        def rel(n):
            v = bb(n)
            return addr + n + 1 + (v - 256 if v >= 128 else v)
        
        r_names = ['B','C','D','E','H','L','(HL)','A']
        rp_names = ['BC','DE','HL','SP']
        rp2_names = ['BC','DE','HL','AF']
        cc_names = ['NZ','Z','NC','C','PO','PE','P','M']
        alu_ops = ['ADD A,','ADC A,','SUB ','SBC A,','AND ','XOR ','OR ','CP ']
        
        # === Prefix CB ===
        if opcode == 0xCB:
            op2 = bb(1)
            r = r_names[op2 & 7]
            bit = (op2 >> 3) & 7
            group = (op2 >> 6) & 3
            if group == 0:
                ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
                return f"{ops[bit]} {r}", 2, [], False, False
            elif group == 1:
                return f"BIT {bit},{r}", 2, [], False, False
            elif group == 2:
                return f"RES {bit},{r}", 2, [], False, False
            else:
                return f"SET {bit},{r}", 2, [], False, False
        
        # === Prefix ED ===
        if opcode == 0xED:
            op2 = bb(1)
            ed_simple = {
                0x40: "IN B,(C)", 0x41: "OUT (C),B",
                0x42: "SBC HL,BC", 0x44: "NEG", 0x46: "IM 0", 0x47: "LD I,A",
                0x48: "IN C,(C)", 0x49: "OUT (C),C",
                0x4A: "ADC HL,BC", 0x4C: "NEG", 0x4E: "IM 0", 0x4F: "LD R,A",
                0x50: "IN D,(C)", 0x51: "OUT (C),D",
                0x52: "SBC HL,DE", 0x54: "NEG", 0x56: "IM 1", 0x57: "LD A,I",
                0x58: "IN E,(C)", 0x59: "OUT (C),E",
                0x5A: "ADC HL,DE", 0x5C: "NEG", 0x5E: "IM 2", 0x5F: "LD A,R",
                0x60: "IN H,(C)", 0x61: "OUT (C),H",
                0x62: "SBC HL,HL", 0x64: "NEG", 0x66: "IM 0", 0x67: "RRD",
                0x68: "IN L,(C)", 0x69: "OUT (C),L",
                0x6A: "ADC HL,HL", 0x6C: "NEG", 0x6E: "IM 0", 0x6F: "RLD",
                0x70: "IN F,(C)", 0x71: "OUT (C),0",
                0x72: "SBC HL,SP", 0x74: "NEG", 0x76: "IM 1",
                0x78: "IN A,(C)", 0x79: "OUT (C),A",
                0x7A: "ADC HL,SP", 0x7C: "NEG", 0x7E: "IM 2",
                0xA0: "LDI", 0xA1: "CPI", 0xA2: "INI", 0xA3: "OUTI",
                0xA8: "LDD", 0xA9: "CPD", 0xAA: "IND", 0xAB: "OUTD",
                0xB0: "LDIR", 0xB1: "CPIR", 0xB2: "INIR", 0xB3: "OTIR",
                0xB8: "LDDR", 0xB9: "CPDR", 0xBA: "INDR", 0xBB: "OTDR",
            }
            if op2 in ed_simple:
                return ed_simple[op2], 2, [], False, False
            
            # ED with word operand
            ed_word = {
                0x43: f"LD (${ww(2):04X}),BC", 0x4B: f"LD BC,(${ww(2):04X})",
                0x53: f"LD (${ww(2):04X}),DE", 0x5B: f"LD DE,(${ww(2):04X})",
                0x63: f"LD (${ww(2):04X}),HL", 0x6B: f"LD HL,(${ww(2):04X})",
                0x73: f"LD (${ww(2):04X}),SP", 0x7B: f"LD SP,(${ww(2):04X})",
            }
            if op2 in ed_word:
                return ed_word[op2], 4, [], False, False
            
            # RETN/RETI
            if op2 in (0x45, 0x55, 0x65, 0x75, 0x7D):
                return "RETN", 2, [], True, False
            if op2 == 0x4D:
                return "RETI", 2, [], True, False
            
            return f"DB 0EDH,{op2:02X}H", 2, [], False, False
        
        # === Prefix DD/FD (IX/IY) ===
        if opcode in (0xDD, 0xFD):
            ir = "IX" if opcode == 0xDD else "IY"
            op2 = bb(1)
            
            # DD CB dd xx
            if op2 == 0xCB:
                disp = signed8(bb(2))
                op3 = bb(3)
                ds = f"+{disp}" if disp >= 0 else str(disp)
                bit = (op3 >> 3) & 7
                group = (op3 >> 6) & 3
                suffix = ""
                if (op3 & 7) != 6:
                    suffix = "," + r_names[op3 & 7]
                if group == 0:
                    ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
                    return f"{ops[bit]} ({ir}{ds}){suffix}", 4, [], False, False
                elif group == 1:
                    return f"BIT {bit},({ir}{ds})", 4, [], False, False
                elif group == 2:
                    return f"RES {bit},({ir}{ds}){suffix}", 4, [], False, False
                else:
                    return f"SET {bit},({ir}{ds}){suffix}", 4, [], False, False
            
            disp = signed8(bb(2))
            ds = f"+{disp}" if disp >= 0 else str(disp)
            
            # IX/IY instructions with displacement (3 bytes)
            dd3 = {
                0x34: f"INC ({ir}{ds})", 0x35: f"DEC ({ir}{ds})",
                0x46: f"LD B,({ir}{ds})", 0x4E: f"LD C,({ir}{ds})",
                0x56: f"LD D,({ir}{ds})", 0x5E: f"LD E,({ir}{ds})",
                0x66: f"LD H,({ir}{ds})", 0x6E: f"LD L,({ir}{ds})",
                0x70: f"LD ({ir}{ds}),B", 0x71: f"LD ({ir}{ds}),C",
                0x72: f"LD ({ir}{ds}),D", 0x73: f"LD ({ir}{ds}),E",
                0x74: f"LD ({ir}{ds}),H", 0x75: f"LD ({ir}{ds}),L",
                0x77: f"LD ({ir}{ds}),A", 0x7E: f"LD A,({ir}{ds})",
                0x86: f"ADD A,({ir}{ds})", 0x8E: f"ADC A,({ir}{ds})",
                0x96: f"SUB ({ir}{ds})", 0x9E: f"SBC A,({ir}{ds})",
                0xA6: f"AND ({ir}{ds})", 0xAE: f"XOR ({ir}{ds})",
                0xB6: f"OR ({ir}{ds})", 0xBE: f"CP ({ir}{ds})",
            }
            if op2 in dd3:
                return dd3[op2], 3, [], False, False
            
            # 4-byte: LD (IX+d),n
            if op2 == 0x36:
                return f"LD ({ir}{ds}),{bb(3):02X}H", 4, [], False, False
            
            # 2-byte IX/IY instructions
            dd2 = {
                0x09: f"ADD {ir},BC", 0x19: f"ADD {ir},DE",
                0x23: f"INC {ir}", 0x24: f"INC {ir}H", 0x25: f"DEC {ir}H",
                0x29: f"ADD {ir},{ir}", 0x2B: f"DEC {ir}",
                0x2C: f"INC {ir}L", 0x2D: f"DEC {ir}L",
                0x39: f"ADD {ir},SP",
                0x44: f"LD B,{ir}H", 0x45: f"LD B,{ir}L",
                0x4C: f"LD C,{ir}H", 0x4D: f"LD C,{ir}L",
                0x54: f"LD D,{ir}H", 0x55: f"LD D,{ir}L",
                0x5C: f"LD E,{ir}H", 0x5D: f"LD E,{ir}L",
                0x60: f"LD {ir}H,B", 0x61: f"LD {ir}H,C",
                0x62: f"LD {ir}H,D", 0x63: f"LD {ir}H,E",
                0x64: f"LD {ir}H,{ir}H", 0x65: f"LD {ir}H,{ir}L",
                0x67: f"LD {ir}H,A",
                0x68: f"LD {ir}L,B", 0x69: f"LD {ir}L,C",
                0x6A: f"LD {ir}L,D", 0x6B: f"LD {ir}L,E",
                0x6C: f"LD {ir}L,{ir}H", 0x6D: f"LD {ir}L,{ir}L",
                0x6F: f"LD {ir}L,A",
                0x7C: f"LD A,{ir}H", 0x7D: f"LD A,{ir}L",
                0x84: f"ADD A,{ir}H", 0x85: f"ADD A,{ir}L",
                0x8C: f"ADC A,{ir}H", 0x8D: f"ADC A,{ir}L",
                0x94: f"SUB {ir}H", 0x95: f"SUB {ir}L",
                0x9C: f"SBC A,{ir}H", 0x9D: f"SBC A,{ir}L",
                0xA4: f"AND {ir}H", 0xA5: f"AND {ir}L",
                0xAC: f"XOR {ir}H", 0xAD: f"XOR {ir}L",
                0xB4: f"OR {ir}H", 0xB5: f"OR {ir}L",
                0xBC: f"CP {ir}H", 0xBD: f"CP {ir}L",
                0xE1: f"POP {ir}", 0xE3: f"EX (SP),{ir}",
                0xE5: f"PUSH {ir}", 0xF9: f"LD SP,{ir}",
            }
            if op2 in dd2:
                return dd2[op2], 2, [], False, False
            
            # JP (IX/IY)
            if op2 == 0xE9:
                return f"JP ({ir})", 2, [], True, False
            
            # 4-byte IX/IY with word
            dd4 = {
                0x21: f"LD {ir},${ww(2):04X}",
                0x22: f"LD (${ww(2):04X}),{ir}",
                0x2A: f"LD {ir},(${ww(2):04X})",
            }
            if op2 in dd4:
                return dd4[op2], 4, [], False, False
            
            # 3-byte IX/IY with byte
            if op2 == 0x26:
                return f"LD {ir}H,{bb(2):02X}H", 3, [], False, False
            if op2 == 0x2E:
                return f"LD {ir}L,{bb(2):02X}H", 3, [], False, False
            
            # Unrecognized - treat as single byte prefix (NOP-like)
            return f"DB {opcode:02X}H", 1, [], False, False
        
        # === Main unprefixed opcodes ===
        
        # NOP and misc
        simple = {
            0x00: "NOP", 0x02: "LD (BC),A", 0x07: "RLCA",
            0x08: "EX AF,AF'", 0x0A: "LD A,(BC)", 0x0F: "RRCA",
            0x12: "LD (DE),A", 0x17: "RLA", 0x1A: "LD A,(DE)",
            0x1F: "RRA", 0x27: "DAA", 0x2F: "CPL",
            0x37: "SCF", 0x3F: "CCF", 0x76: "HALT",
            0xD9: "EXX", 0xE3: "EX (SP),HL", 0xEB: "EX DE,HL",
            0xF3: "DI", 0xFB: "EI",
        }
        if opcode in simple:
            end = (opcode == 0x76)  # HALT
            return simple[opcode], 1, [], end, False
        
        # RET
        if opcode == 0xC9:
            return "RET", 1, [], True, False
        
        # JP (HL)
        if opcode == 0xE9:
            return "JP (HL)", 1, [], True, False
        
        # LD r,r'
        if (opcode & 0xC0) == 0x40:
            dst = r_names[(opcode >> 3) & 7]
            src = r_names[opcode & 7]
            return f"LD {dst},{src}", 1, [], False, False
        
        # ALU A,r
        if (opcode & 0xC0) == 0x80:
            op_name = alu_ops[(opcode >> 3) & 7]
            src = r_names[opcode & 7]
            return f"{op_name}{src}", 1, [], False, False
        
        # INC/DEC r
        if (opcode & 0xC7) == 0x04:
            return f"INC {r_names[(opcode >> 3) & 7]}", 1, [], False, False
        if (opcode & 0xC7) == 0x05:
            return f"DEC {r_names[(opcode >> 3) & 7]}", 1, [], False, False
        
        # LD r,n
        if (opcode & 0xC7) == 0x06:
            return f"LD {r_names[(opcode >> 3) & 7]},{bb(1):02X}H", 2, [], False, False
        
        # 16-bit LD rp,nn
        if (opcode & 0xCF) == 0x01:
            rp = rp_names[(opcode >> 4) & 3]
            return f"LD {rp},${ww(1):04X}", 3, [], False, False
        
        # ADD HL,rp
        if (opcode & 0xCF) == 0x09:
            return f"ADD HL,{rp_names[(opcode >> 4) & 3]}", 1, [], False, False
        
        # INC/DEC rp
        if (opcode & 0xCF) == 0x03:
            return f"INC {rp_names[(opcode >> 4) & 3]}", 1, [], False, False
        if (opcode & 0xCF) == 0x0B:
            return f"DEC {rp_names[(opcode >> 4) & 3]}", 1, [], False, False
        
        # PUSH/POP
        if (opcode & 0xCF) == 0xC5:
            return f"PUSH {rp2_names[(opcode >> 4) & 3]}", 1, [], False, False
        if (opcode & 0xCF) == 0xC1:
            return f"POP {rp2_names[(opcode >> 4) & 3]}", 1, [], False, False
        
        # LD (nn),HL / LD HL,(nn)
        if opcode == 0x22:
            return f"LD (${ww(1):04X}),HL", 3, [], False, False
        if opcode == 0x2A:
            return f"LD HL,(${ww(1):04X})", 3, [], False, False
        
        # LD (nn),A / LD A,(nn)
        if opcode == 0x32:
            return f"LD (${ww(1):04X}),A", 3, [], False, False
        if opcode == 0x3A:
            return f"LD A,(${ww(1):04X})", 3, [], False, False
        
        # JP nn
        if opcode == 0xC3:
            t = ww(1)
            return f"JP ${t:04X}", 3, [t], True, False
        
        # JP cc,nn
        if (opcode & 0xC7) == 0xC2:
            cc = cc_names[(opcode >> 3) & 7]
            t = ww(1)
            return f"JP {cc},${t:04X}", 3, [t], False, False
        
        # JR e
        if opcode == 0x18:
            t = rel(1)
            return f"JR ${t:04X}", 2, [t], True, False
        
        # JR cc,e
        if opcode in (0x20, 0x28, 0x30, 0x38):
            cc = ['NZ','Z','NC','C'][(opcode - 0x20) >> 3]
            t = rel(1)
            return f"JR {cc},${t:04X}", 2, [t], False, False
        
        # DJNZ e
        if opcode == 0x10:
            t = rel(1)
            return f"DJNZ ${t:04X}", 2, [t], False, False
        
        # CALL nn
        if opcode == 0xCD:
            t = ww(1)
            return f"CALL ${t:04X}", 3, [t], False, True
        
        # CALL cc,nn
        if (opcode & 0xC7) == 0xC4:
            cc = cc_names[(opcode >> 3) & 7]
            t = ww(1)
            return f"CALL {cc},${t:04X}", 3, [t], False, True
        
        # RET cc
        if (opcode & 0xC7) == 0xC0:
            cc = cc_names[(opcode >> 3) & 7]
            return f"RET {cc}", 1, [], False, False
        
        # RST
        if (opcode & 0xC7) == 0xC7:
            t = opcode & 0x38
            return f"RST {t:02X}H", 1, [t], False, True
        
        # ALU A,n
        if (opcode & 0xC7) == 0xC6:
            op_name = alu_ops[(opcode >> 3) & 7]
            return f"{op_name}{bb(1):02X}H", 2, [], False, False
        
        # IN A,(n) / OUT (n),A
        if opcode == 0xDB:
            return f"IN A,({bb(1):02X}H)", 2, [], False, False
        if opcode == 0xD3:
            return f"OUT ({bb(1):02X}H),A", 2, [], False, False
        
        return f"DB {opcode:02X}H", 1, [], False, False


class FormatDisassembler:
    """Main disassembler for format.com"""
    
    def __init__(self, data):
        self.data = data
        self.org = ORG
        self.decoder = Z80Decoder(data, ORG)
        self.instructions = {}  # addr -> (mnemonic, length, raw, targets, is_end, is_call)
        self.labels = {}  # addr -> name
        self.comments = {}  # addr -> comment
        self.pre_comments = defaultdict(list)  # addr -> [lines before]
        self.code_bytes = set()  # all bytes that are part of code
        self.call_targets = set()
        self.jump_targets = set()
        self.data_refs = set()  # addresses referenced as data
        
        # Define known regions
        self.code_regions = [
            (0x0100, 0x0FBC),  # Main program code
            (0x1C8E, 0x1EDF),  # CPU2 format/ISR code
        ]
        self.string_regions_manual = []  # Will be populated
        self.data_regions_manual = []    # Will be populated
        
    def decode_all_code(self):
        """Multi-pass code decoding"""
        # Pass 1: Recursive descent from entry point
        self._recursive_decode(0x0100, set())
        
        # Pass 2: Follow all discovered targets
        changed = True
        while changed:
            changed = False
            for t in sorted(self.call_targets | self.jump_targets):
                if t not in self.instructions and self._in_code_region(t):
                    self._recursive_decode(t, set())
                    changed = True
        
        # Pass 3: Linear decode remaining code regions
        for start, end in self.code_regions:
            self._linear_decode(start, end)
    
    def _in_code_region(self, addr):
        for start, end in self.code_regions:
            if start <= addr < end:
                return True
        return False
    
    def _recursive_decode(self, start, visited):
        addr = start
        while True:
            if addr in visited or addr in self.instructions:
                break
            o = addr - self.org
            if o < 0 or o >= len(self.data):
                break
            
            visited.add(addr)
            mn, length, targets, is_end, is_call = self.decoder.decode(addr)
            if mn is None or length == 0:
                break
            
            raw = self.data[o:o+length]
            self.instructions[addr] = (mn, length, raw, targets, is_end, is_call)
            for i in range(length):
                self.code_bytes.add(addr + i)
            
            for t in targets:
                if self.org <= t < self.org + len(self.data):
                    if is_call:
                        self.call_targets.add(t)
                    else:
                        self.jump_targets.add(t)
            
            if is_end:
                for t in targets:
                    if self._in_code_region(t):
                        self._recursive_decode(t, visited)
                break
            else:
                for t in targets:
                    if self._in_code_region(t):
                        self._recursive_decode(t, visited)
                addr += length
    
    def _linear_decode(self, start, end):
        addr = start
        while addr < end:
            if addr in self.instructions:
                _, length, _, _, _, _ = self.instructions[addr]
                addr += length
                continue
            
            o = addr - self.org
            if o >= len(self.data):
                break
            
            mn, length, targets, is_end, is_call = self.decoder.decode(addr)
            if length == 0:
                addr += 1
                continue
            
            raw = self.data[o:o+length]
            self.instructions[addr] = (mn, length, raw, targets, is_end, is_call)
            for i in range(length):
                self.code_bytes.add(addr + i)
            
            for t in targets:
                if self.org <= t < self.org + len(self.data):
                    if is_call:
                        self.call_targets.add(t)
                    else:
                        self.jump_targets.add(t)
            
            addr += length
    
    def _find_strings(self):
        """Find all string regions outside of code"""
        strings = []
        i = 0
        while i < len(self.data):
            addr = i + self.org
            if addr in self.code_bytes:
                i += 1
                continue
            
            # Check for printable string
            j = i
            printable = 0
            while j < len(self.data):
                b = self.data[j]
                if 0x20 <= b <= 0x7E or b in (0x0D, 0x0A, 0x07):
                    printable += 1
                    j += 1
                elif b == 0x24 and printable >= 3:  # '$' terminator
                    j += 1
                    break
                elif b == 0x00 and printable >= 3:
                    break
                else:
                    break
            
            if printable >= 5:
                strings.append((addr, j - i))
                i = j
            else:
                i += 1
        
        return strings
    
    def assign_labels(self):
        """Assign meaningful names to labels based on context analysis"""
        # Known address labels from analysis
        known_labels = {
            # === Main program flow ===
            0x0100: "start",
            0x0122: "check_CPA",
            0x0131: "check_TPA",
            0x0142: "check_cmdline",
            0x0158: "main_init",
            0x0165: "show_menu",
            0x0183: "menu_input",
            0x01AF: "process_choice",
            0x01FA: "setup_format_params",
            0x0258: "setup_drive",
            0x02D6: "begin_operation",
            0x0392: "apply_params",
            0x03D4: "format_setup",
            0x03DE: "set_tracks",
            0x0467: "set_length_code",
            0x048D: "set_format_5_14",
            0x049B: "set_format_5_14b",
            0x04A7: "set_format_8",
            0x05CC: "calc_capacity",
            0x0633: "set_side_offset",
            0x064B: "prompt_start_track",
            0x065E: "prompt_end_track",
            0x06B0: "prompt_source_track",
            0x06E1: "setup_operation",
            0x06F8: "setup_format_ops",
            0x0733: "do_format_main",
            0x07CB: "setup_variables",
            0x0898: "prepare_format",
            0x08F1: "format_loop",
            0x0960: "format_track_data",
            0x09AB: "verify_loop",
            0x09F1: "format_next_track",
            0x0A33: "format_done_prompt",
            0x0A6A: "check_last_track",
            0x0A76: "handle_error",
            0x0A96: "display_status",
            
            # === Disk I/O subroutines ===
            0x0B02: "do_seek",
            0x0B5D: "select_drive_side",
            0x0B65: "set_side_flag",
            0x0B76: "check_valid_drive",
            0x0B7B: "restore_drive",
            0x0B96: "save_drive_state",
            0x0B9E: "deselect_drive",
            0x0BAA: "display_track_info",
            0x0BB4: "calc_track_capacity",
            0x0BED: "prompt_format_count",
            0x0BEE: "input_format_count",
            0x0C00: "seek_only",
            0x0C03: "do_multisector_io",
            0x0C10: "calc_sectors",
            0x0C5A: "check_capacity",
            0x0C61: "do_cdb_io",
            0x0C6A: "do_cdb_io_write",
            0x0C78: "save_isr_vectors",
            0x0C8E: "next_drive",
            0x0CA1: "next_side",
            0x0CA2: "display_format_info",
            0x0D10: "do_disk_io",
            0x0D5D: "do_format_write",
            0x0D6C: "fill_format_buffer",
            0x0D97: "write_sector_data",
            0x0DAD: "read_sector_data",
            0x0DB2: "do_verify_read",
            0x0DC3: "save_de_to_iy",
            0x0DC7: "advance_dma_ptr",
            
            # === CPA extension calls ===
            0x0DEE: "call_cpmx18",         # delsps
            0x0DF9: "resolve_cpmext",
            0x0E00: "call_cpmx21",         # diskio
            0x0E0E: "call_cpmext",
            
            # === Console I/O subroutines ===
            0x0E18: "print_string",
            0x0E1B: "print_string_inline",
            0x0E20: "print_char",
            0x0E27: "print_char_bdos",
            0x0E34: "read_char_upper",
            0x0E37: "read_char",
            0x0E3D: "read_char_wait",
            0x0E63: "input_number",
            0x0E70: "print_dec_HL",
            0x0EA5: "dec_div_digit",
            0x0EAD: "dec_check_zero",
            0x0EB0: "dec_out_digit",
            
            # === Output formatting / input parsing ===
            0x0ECC: "print_hex_A",
            0x0ED5: "print_hex_nib",
            0x0EE2: "read_user_input",
            0x0EED: "read_chars_loop",
            0x0F0F: "read_line_bdos",
            0x0F32: "parse_hex_input",
            0x0F63: "parse_dec_input",
            0x0F70: "print_decimal_HL",
            0x0F90: "restore_isr_exit",
            0x0F99: "exit_program",
            0x0F9E: "exit_warm_boot",
            0x0FA8: "restore_isr",
            0x0FAB: "call_diskio_cdb",
            0x0FAD: "delay_loop",
            
            # === String data ===
            0x0FBD: "msg_version_error",
            0x1012: "msg_tpa_error",
            0x105B: "msg_banner",
            0x10BE: "msg_menu",
            0x10E3: "msg_cpa_mode",
            0x111C: "msg_format_info",
            0x134F: "msg_formatting",
            0x135D: "msg_copying",
            0x1368: "msg_checking_done",
            0x137C: "msg_repeat",
            0x13AB: "msg_track_error",
            0x13CA: "msg_return_menu",
            0x141C: "msg_no_drive",
            0x143A: "msg_write_prot",
            0x145A: "msg_from_track",
            0x1464: "msg_to_track",
            0x146D: "msg_length_bytes",
            0x147C: "msg_auto_format",
            0x14BA: "msg_verify_yes",
            0x14EC: "msg_verify_no",
            0x151E: "msg_source_drive",
            0x153F: "msg_select_fmt",
            0x157C: "msg_formatting_label",
            0x15E1: "msg_read_error",
            0x15FB: "msg_sector_ident",
            0x172D: "msg_offset_prompt",
            0x175A: "msg_format_uses",
            
            # === Data areas ===
            0x1A30: "format_data_area",
            0x1A3C: "default_params",
            0x1A4E: "sector_table_1",
            0x1A83: "format_params_2",
            0x1A92: "sector_table_2",
            0x1AD8: "format_params_3",
            0x1AF4: "sector_table_3",
            0x1B30: "work_cdb",
            0x1B50: "format_buffer",
            0x1BD0: "active_cdb",
            
            # === CPU2/ISR code ===
            0x1C1E: "drive_a_params",
            0x1C2C: "drive_b_params",
            0x1C8E: "cpu2_init",
            0x1CD0: "cpu2_copy_data",
            0x1CE1: "cpu2_setup_loop",
            0x1CF5: "cpu2_jr_loop",
            0x1D22: "cpu2_format_end",
            0x1D30: "cpu2_check_ready",
            0x1D3A: "cpu2_format_5_sd",
            0x1DF7: "cpu2_format_5_dd_wr",
            0x1E88: "isr_index_handler",
            0x1E8E: "setup_isr",
            0x1EB6: "restore_isr_vecs",
            0x1ED0: "cpu2_copy_routine",
            0x1EDF: "cpu2_format_table",
            0x1F04: "gap_fill_table",
            0x1F2B: "format_ptr_table",
            0x1F3C: "dpb_table",
            0x1FCB: "cpa_version_str",
            0x1FD3: "dpb_default",
            
            # === Boot sector template ===
            0x2000: "boot_sector",
            0x2147: "boot_msg_no_sys",
            0x2197: "boot_msg_load_err",
            0x21AE: "boot_filenames",
            0x21C5: "boot_sector_end",
            
            # === Format definition tables ===
            0x21C9: "fmt_5_25_40_ss",
            0x295A: "fmt_5_25_80_ss",
            0x2DAF: "fmt_5_25_40_ds",
            0x3312: "fmt_5_25_80_ds",
            0x3B8E: "fmt_8_ss_sd",
            0x3FCB: "fmt_8_ss_dd",
        }
        
        self.labels = dict(known_labels)
        
        # Add labels for all branch/call targets not yet named
        all_targets = self.call_targets | self.jump_targets
        for t in sorted(all_targets):
            if t not in self.labels:
                if t in self.call_targets:
                    self.labels[t] = f"sub_{t:04X}"
                else:
                    self.labels[t] = f"L{t:04X}"
        
        # Add labels for data references
        # Scan instructions for LD DE,addr patterns that point to strings
        for addr in sorted(self.instructions):
            mn, _, _, _, _, _ = self.instructions[addr]
            # Look for LD DE,$NNNN patterns
            m = re.search(r'\$([0-9A-F]{4})', mn)
            if m:
                ref_addr = int(m.group(1), 16)
                if ref_addr not in self.labels and self.org <= ref_addr < self.org + len(self.data):
                    if ref_addr not in self.code_bytes:
                        self.data_refs.add(ref_addr)
    
    def assign_comments(self):
        """Add comments to specific addresses"""
        c = self.comments
        pc = self.pre_comments
        
        pc[0x0100].append("; ============================================================================")
        pc[0x0100].append("; Entry Point: Check BIOS for CPA version string")
        pc[0x0100].append("; Validates that system is CPA (not standard CP/M) with version >= CPAB5")
        pc[0x0100].append("; ============================================================================")
        c[0x0100] = "HL = warm boot vector (BIOS base)"
        c[0x0103] = "offset +30H into BIOS"
        c[0x0106] = "HL -> BIOS version string"
        c[0x0107] = "check for 'CPA' string"
        c[0x010A] = "not CPA -> error"
        c[0x010C] = "'B' - check for CPAB variant"
        c[0x0112] = "check version digit >= '9'"
        c[0x0117] = "check version digit >= '5'"
        c[0x011C] = "-> msg_version_error"
        
        pc[0x0122].append("; -----------------------------------------------")
        pc[0x0122].append("; Check for 'CPA' string at (HL)")
        pc[0x0122].append("; Uses CPI to compare and advance HL")
        pc[0x0122].append("; Returns: Z=match, NZ=no match")
        pc[0x0122].append("; -----------------------------------------------")
        c[0x0122] = "'C'"
        c[0x0127] = "'P'"
        c[0x012C] = "'A'"
        
        pc[0x0131].append("; Check TPA size >= 9660H (required memory)")
        c[0x0131] = "HL = BDOS entry addr = top of TPA"
        c[0x0134] = "minimum required = 38496 bytes"
        c[0x0137] = "clear carry for SBC"
        c[0x013C] = "-> msg_tpa_error"
        
        pc[0x0142].append("; Check command line for 'CPA' parameter")
        pc[0x0142].append("; If present, enables CPA-specific mode (format types)")
        c[0x0142] = "FCB1+1 = first cmdline arg"
        c[0x014C] = "call delsps (delete spaces) 3x"
        c[0x0155] = "store CPA mode flag"
        
        pc[0x0158].append("")
        pc[0x0158].append("; ============================================================================")
        pc[0x0158].append("; Main Program Loop - Display menu and handle selections")
        pc[0x0158].append("; ============================================================================")
        c[0x0158] = "set stack from BDOS addr"
        c[0x015C] = "save ISR vectors, init hardware"
        c[0x015F] = "-> msg_banner"
        
        pc[0x0165].append("; --- Show format type menu ---")
        c[0x0165] = "-> msg_menu"
        c[0x016B] = "CPA mode flag (self-modifying)"
        c[0x0176] = "-> msg_format_info"
        
        pc[0x0183].append("; --- Read user menu selection ---")
        c[0x0183] = "check for 'Q' = quit"
        c[0x0188] = "check for 'X' = quit"
        c[0x018D] = "convert ASCII to number"
        c[0x018F] = "< '0' -> retry"
        c[0x0191] = "> 3 -> retry"
        c[0x0195] = "choice 1?"
        c[0x0199] = "check CPA mode required"
        
        pc[0x01AF].append("; --- Process format function selection ---")
        pc[0x01AF].append("; A = selected function (0=format, 1=copy, 2=verify, 3=autodetect)")
        c[0x01A1] = "store function number"
        c[0x01A3] = "function 1 -> copy prompt"
        
        pc[0x02D6].append("; --- Calculate sector size from SLC ---")
        c[0x02D6] = "SRL A - divide by 2 per SLC step"
        
        pc[0x03D4].append("; --- Set up format parameters ---")
        
        pc[0x0E18].append("; -----------------------------------------------")
        pc[0x0E18].append("; Print $-terminated string")
        pc[0x0E18].append("; DE = pointer to string")
        pc[0x0E18].append("; -----------------------------------------------")
        c[0x0E18] = "BDOS function 9"
        
        pc[0x0E1B].append("; Print $-terminated string + CRLF")
        c[0x0E1B] = "BDOS function 9"
        
        pc[0x0E20].append("; Print character in A")
        pc[0x0E27].append("; Print character via BDOS (E=char)")
        
        pc[0x0E34].append("; Read char and convert to uppercase")
        
        pc[0x0E37].append("; -----------------------------------------------")
        pc[0x0E37].append("; Read character from console")
        pc[0x0E37].append("; Returns: A = character, Z if no char")
        pc[0x0E37].append("; -----------------------------------------------")
        c[0x0E37] = "BDOS direct console I/O"
        
        pc[0x0E63].append("; Read decimal number from console")
        
        pc[0x0E70].append("; -----------------------------------------------")
        pc[0x0E70].append("; Print HL as decimal number (signed)")
        pc[0x0E70].append("; Uses successive subtraction by -10000,-1000,-100,-10")
        pc[0x0E70].append("; -----------------------------------------------")
        
        pc[0x0ECC].append("; -----------------------------------------------")
        pc[0x0ECC].append("; Print A register as 2-digit hex")
        pc[0x0ECC].append("; Rotates high nibble down, prints both nibbles")
        pc[0x0ECC].append("; -----------------------------------------------")
        
        pc[0x0EE2].append("; -----------------------------------------------")
        pc[0x0EE2].append("; Read user input (from command tail or console)")
        pc[0x0EE2].append("; If (0080H)=0: uses BDOS read buffer function")
        pc[0x0EE2].append("; Otherwise: reads chars via cpmx18, echoing each")
        pc[0x0EE2].append("; Converts result to uppercase")
        pc[0x0EE2].append("; -----------------------------------------------")
        
        pc[0x0F0F].append("; Read console line via BDOS function 0AH")
        
        pc[0x0F32].append("; -----------------------------------------------")
        pc[0x0F32].append("; Parse hex number from user input buffer")
        pc[0x0F32].append("; Returns: E=value, Z if empty input")
        pc[0x0F32].append("; -----------------------------------------------")
        
        pc[0x0C78].append("; -----------------------------------------------")
        pc[0x0C78].append("; Save ISR vectors and setup for disk operations")
        pc[0x0C78].append("; -----------------------------------------------")
        
        pc[0x0D10].append("; -----------------------------------------------")
        pc[0x0D10].append("; Disk I/O via cpmx21 (diskio)")
        pc[0x0D10].append("; Sets up CDB structure and calls BIOS extension")
        pc[0x0D10].append("; -----------------------------------------------")
        
        pc[0x0DEE].append("; Call CPA extension cpmx18 (delsps - delete spaces)")
        pc[0x0E00].append("; Call CPA extension cpmx21 (diskio - disk transfer)")
        pc[0x0E0E].append("; Call CPA extension by number")
        
        pc[0x0FAB].append("; Call diskio with CDB at HL")
        
        pc[0x0FAD].append("; -----------------------------------------------")
        pc[0x0FAD].append("; Delay loop")
        pc[0x0FAD].append("; -----------------------------------------------")
        
        pc[0x0F90].append("; -----------------------------------------------")
        pc[0x0F90].append("; Restore ISR vectors and return to CP/M")
        pc[0x0F90].append("; -----------------------------------------------")
        
        # Data area comments
        pc[0x0FBD].append("")
        pc[0x0FBD].append("; ============================================================================")
        pc[0x0FBD].append("; String Data Area")
        pc[0x0FBD].append("; ============================================================================")
        
        pc[0x1A30].append("")
        pc[0x1A30].append("; ============================================================================")
        pc[0x1A30].append("; Format Parameter Data Area")
        pc[0x1A30].append("; ============================================================================")
        
        pc[0x1B30].append("")
        pc[0x1B30].append("; ============================================================================")
        pc[0x1B30].append("; Working Data Area (CDB, buffers, variables)")
        pc[0x1B30].append("; ============================================================================")
        
        pc[0x1C8E].append("")
        pc[0x1C8E].append("; ============================================================================")
        pc[0x1C8E].append("; CPU2 (K2526) Format Code")
        pc[0x1C8E].append("; This is the timing-critical code for physical disk formatting.")
        pc[0x1C8E].append("; The K2526 second CPU executes this while main CPU waits in JR $ loop.")
        pc[0x1C8E].append("; Index pulse interrupt triggers ISR that modifies the JR displacement")
        pc[0x1C8E].append("; to break the loop when formatting is complete.")
        pc[0x1C8E].append("; ============================================================================")
        
        pc[0x1E88].append("; --- Index pulse ISR ---")
        pc[0x1E88].append("; Modifies JR displacement in CPU2 loop to break out")
        
        pc[0x1E8E].append("; --- Setup ISR vectors for format ---")
        pc[0x1E8E].append("; Saves old vectors at F7E8/F7EA, installs format handlers")
        
        pc[0x1EB6].append("; --- Restore original ISR vectors ---")
        
        pc[0x1EDF].append("")
        pc[0x1EDF].append("; ============================================================================")
        pc[0x1EDF].append("; CPU2 Format Routine Pointer Table")
        pc[0x1EDF].append("; ============================================================================")
        
        pc[0x1F2B].append("")
        pc[0x1F2B].append("; ============================================================================")
        pc[0x1F2B].append("; Format Parameter/Pointer Tables")
        pc[0x1F2B].append("; ============================================================================")
        
        pc[0x1F3C].append("; DPB-like parameter blocks for different formats")
        
        pc[0x1FCB].append("; CPA version identification string")
        
        pc[0x2000].append("")
        pc[0x2000].append("; ============================================================================")
        pc[0x2000].append("; Boot Sector Template (512 bytes)")
        pc[0x2000].append("; Written to track 0 of formatted disks as system bootstrap")
        pc[0x2000].append("; Contains x86-compatible boot code for MSDOS/CPA dual boot")
        pc[0x2000].append("; ============================================================================")
        
        pc[0x21C9].append("")
        pc[0x21C9].append("; ============================================================================")
        pc[0x21C9].append("; Format Definition Tables")
        pc[0x21C9].append("; Each table contains: menu text + DPB parameters + sector interleave tables")
        pc[0x21C9].append("; for a specific disk format (5.25\"/8\", SS/DS, SD/DD, etc.)")
        pc[0x21C9].append("; ============================================================================")
        
        pc[0x295A].append("; --- 5 1/4\", 80 tracks, single-sided formats ---")
        pc[0x2DAF].append("; --- 5 1/4\", 40 tracks, double-sided formats ---")
        pc[0x3312].append("; --- 5 1/4\", 80 tracks, double-sided formats ---")
        pc[0x3B8E].append("; --- 8\", single-sided, single density formats ---")
        pc[0x3FCB].append("; --- 8\", single-sided, double density formats ---")
    
    def format_operand(self, mn):
        """Replace $NNNN references with label names"""
        def repl(m):
            val = int(m.group(1), 16)
            if val in self.labels:
                return self.labels[val]
            return f"{val:04X}H"
        return re.sub(r'\$([0-9A-F]{4})', repl, mn)
    
    def output_data_bytes(self, start_addr, end_addr):
        """Output raw data bytes in DB format, 16 per line"""
        lines = []
        addr = start_addr
        while addr < end_addr:
            o = addr - self.org
            if o >= len(self.data):
                break
            count = min(16, end_addr - addr, len(self.data) - o)
            raw = self.data[o:o+count]
            hex_parts = [f"{b:02X}H" for b in raw]
            # Try to show ASCII in comment
            ascii_str = ''.join(chr(b) if 0x20 <= b <= 0x7E else '.' for b in raw)
            
            lines.append(f"\tDB\t{','.join(hex_parts):50s}; {addr:04X} | {ascii_str}")
            addr += count
        return lines
    
    def output_string(self, addr, length):
        """Output a string as DB with proper formatting"""
        lines = []
        o = addr - self.org
        raw = self.data[o:o+length]
        
        # Split string into manageable parts at CR/LF boundaries
        i = 0
        while i < len(raw):
            parts = []
            line_len = 0
            while i < len(raw) and line_len < 60:
                b = raw[i]
                if b == 0x0D:
                    parts.append("0DH")
                    i += 1
                    line_len += 4
                elif b == 0x0A:
                    parts.append("0AH")
                    i += 1
                    line_len += 4
                    # Break after LF for readability
                    break
                elif b == 0x07:
                    parts.append("07H")
                    i += 1
                    line_len += 4
                elif b == 0x24:
                    parts.append("'$'")
                    i += 1
                    line_len += 3
                    break  # '$' is usually string terminator
                elif 0x20 <= b <= 0x7E:
                    # Collect text run
                    text = ""
                    while i < len(raw) and 0x20 <= raw[i] <= 0x7E and raw[i] != 0x24:
                        text += chr(raw[i])
                        i += 1
                        line_len += 1
                        if line_len >= 60:
                            break
                    # Escape single quotes in text
                    text = text.replace("'", "''")
                    parts.append(f"'{text}'")
                else:
                    parts.append(f"{b:02X}H")
                    i += 1
                    line_len += 4
            
            if parts:
                lines.append(f"\tDB\t{','.join(parts)}")
        
        return lines
    
    def generate_output(self):
        """Generate the complete .mac file"""
        lines = []
        
        # Header
        lines.append("; ============================================================================")
        lines.append("; FORMAT.COM - Disketten-FORMAT fuer Buerocomputer")
        lines.append("; Robotron A5120 - CPA/CP/M 2.2 System")
        lines.append("; Version 19.05.89")
        lines.append(f"; Size: {len(self.data)} bytes ({len(self.data):04X}H)")
        lines.append(f"; Address range: {self.org:04X}H - {self.org+len(self.data)-1:04X}H")
        lines.append(";")
        lines.append("; Disassembled from binary - all bytes accounted for")
        lines.append("; ============================================================================")
        lines.append("")
        
        # Constants
        lines.append("; --- System addresses ---")
        lines.append("WBOOT\t\tEQU\t0000H\t; CP/M warm boot")
        lines.append("WBOOTV\t\tEQU\t0001H\t; Warm boot vector")
        lines.append("BDOS\t\tEQU\t0005H\t; BDOS entry")
        lines.append("BDOSADDR\tEQU\t0006H\t; BDOS address (high byte)")
        lines.append("CPMEXT\t\tEQU\t004EH\t; CPA extensions table pointer")
        lines.append("FCB1\t\tEQU\t005CH\t; Default FCB")
        lines.append("DMAADDR\t\tEQU\t0080H\t; Default DMA address")
        lines.append("")
        lines.append("; --- BDOS function codes ---")
        lines.append("F_RAWIO\t\tEQU\t06H\t; Direct console I/O")
        lines.append("F_PRINT\t\tEQU\t09H\t; Print $-terminated string")
        lines.append("F_RDBUF\t\tEQU\t0AH\t; Read console buffer")
        lines.append("F_GETVER\tEQU\t0CH\t; Get CP/M version")
        lines.append("")
        lines.append("; --- FDC/PIO port addresses ---")
        lines.append("PIO_CTRL_A\tEQU\t10H\t; PIO A data (FDC control)")
        lines.append("PIO_CTRL_C\tEQU\t11H\t; PIO A control register")
        lines.append("PIO_STAT_B\tEQU\t12H\t; PIO B data (FDC status)")
        lines.append("PIO_DATA_A\tEQU\t14H\t; Data PIO A")
        lines.append("PIO_DATA_B\tEQU\t04H\t; Data PIO B")
        lines.append("PIO_DATA_C\tEQU\t16H\t; Data PIO control")
        lines.append("PIO_DATA_D\tEQU\t17H\t; Data PIO control 2")
        lines.append("DRV_SEL\t\tEQU\t18H\t; Drive select latch")
        lines.append("")
        lines.append("; --- PIO A control bits ---")
        lines.append("; Bit 0: /WE (write enable, active low)")
        lines.append("; Bit 2: /SIDE (side select: 0=side 1, 1=side 0)")
        lines.append("; Bit 5: SD (step direction: 0=outward, 1=inward)")
        lines.append("; Bit 7: /ST (step pulse, rising edge)")
        lines.append("")
        lines.append("; --- PIO B status bits ---")
        lines.append("; Bit 0: /RDY (0=drive ready)")
        lines.append("; Bit 5: /WP (0=write protected)")  
        lines.append("; Bit 7: /T0 (0=at track 0)")
        lines.append("")
        lines.append("; --- Interrupt vectors ---")
        lines.append("IV_INDEX\tEQU\t0F7E8H\t; Index pulse ISR vector")
        lines.append("IV_MARK\t\tEQU\t0F7EAH\t; Data mark ISR vector")
        lines.append("")
        lines.append("; ============================================================================")
        lines.append("")
        lines.append("\tORG\t0100H")
        lines.append("")
        
        # Find all string regions
        string_data = self._find_strings()
        string_map = {}
        for (sa, sl) in string_data:
            string_map[sa] = sl
        
        # Generate output, walking through all addresses
        addr = self.org
        end_addr = self.org + len(self.data)
        
        while addr < end_addr:
            o = addr - self.org
            
            # Add pre-comments
            if addr in self.pre_comments:
                for line in self.pre_comments[addr]:
                    lines.append(line)
            
            # Helper to emit label for this address
            def emit_label(a):
                if a in self.labels:
                    lines.append(f"{self.labels[a]}:")
            
            # Is this a decoded instruction?
            if addr in self.instructions:
                emit_label(addr)
                mn, length, raw, targets, is_end, is_call = self.instructions[addr]
                hex_str = ' '.join(f'{b:02X}' for b in raw)
                
                display = self.format_operand(mn)
                
                comment = ""
                if addr in self.comments:
                    comment = f" ; {self.comments[addr]}"
                
                lines.append(f"\t{display:40s}; {addr:04X}: {hex_str}{comment}")
                addr += length
                
                # Add blank line after unconditional jumps/returns for readability
                if is_end:
                    lines.append("")
                continue
            
            # Is this a string region?
            if addr in string_map:
                emit_label(addr)
                sl = string_map[addr]
                str_lines = self.output_string(addr, sl)
                lines.extend(str_lines)
                addr += sl
                continue
            
            # Raw data byte
            # Try to group consecutive data bytes
            group_start = addr
            emit_label(addr)
            while addr < end_addr and addr not in self.instructions and addr not in string_map:
                # Check if next address has a label - if so, break here
                if addr != group_start and addr in self.labels:
                    break
                addr += 1
            
            group_len = addr - group_start
            if group_len > 0:
                data_lines = self.output_data_bytes(group_start, addr)
                lines.extend(data_lines)
        
        lines.append("")
        lines.append("\tEND")
        
        return '\n'.join(lines)
    
    def run(self):
        """Execute the full disassembly pipeline"""
        sys.stderr.write("Pass 1: Decoding instructions...\n")
        self.decode_all_code()
        sys.stderr.write(f"  Found {len(self.instructions)} instructions\n")
        
        sys.stderr.write("Pass 2: Assigning labels...\n")
        self.assign_labels()
        sys.stderr.write(f"  {len(self.labels)} labels\n")
        
        sys.stderr.write("Pass 3: Adding comments...\n")
        self.assign_comments()
        
        sys.stderr.write("Pass 4: Generating output...\n")
        return self.generate_output()


def main():
    path = '/home/olliy/projects/a5120emu/boot_disk/format.com'
    data = read_binary(path)
    
    dis = FormatDisassembler(data)
    output = dis.run()
    print(output)


if __name__ == '__main__':
    main()
