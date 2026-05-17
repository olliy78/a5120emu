with open('/home/olliy/projects/a5120emu/doc/EPROMS/Prozessoreinheit_ZRE_K2526_A26.bin', 'rb') as f:
    rom = bytearray(f.read())

print(f"File size: {len(rom)} bytes\n")

# Show first 64 bytes
print("First 64 bytes:")
for i in range(0, 64, 16):
    hex_bytes = ' '.join(f'{b:02X}' for b in rom[i:i+16])
    ascii_bytes = ''.join(chr(b) if 32 <= b < 127 else '.' for b in rom[i:i+16])
    print(f"  {i:04X}: {hex_bytes}  |{ascii_bytes}|")

print()

# Full Z80 disassembler (basic)
def z80_disasm(mem, start, end):
    pc = start
    while pc < end:
        b0 = mem[pc]
        opc_start = pc
        
        def rb(offset=1):
            addr = opc_start + offset
            return mem[addr] if addr < len(mem) else 0
        
        def rw(offset=1):
            lo = rb(offset)
            hi = rb(offset+1)
            return lo | (hi << 8)
        
        r8 = ['B','C','D','E','H','L','(HL)','A']
        r16 = ['BC','DE','HL','SP']
        r16af = ['BC','DE','HL','AF']
        
        if b0 == 0x00: yield opc_start, 1, "NOP"
        elif b0 == 0x01: yield opc_start, 3, f"LD BC, 0x{rw():04X}"; pc+=2
        elif b0 == 0x02: yield opc_start, 1, "LD (BC), A"
        elif b0 == 0x03: yield opc_start, 1, "INC BC"
        elif b0 == 0x04: yield opc_start, 1, "INC B"
        elif b0 == 0x05: yield opc_start, 1, "DEC B"
        elif b0 == 0x06: yield opc_start, 2, f"LD B, 0x{rb():02X}"; pc+=1
        elif b0 == 0x07: yield opc_start, 1, "RLCA"
        elif b0 == 0x08: yield opc_start, 1, "EX AF, AF'"
        elif b0 == 0x09: yield opc_start, 1, "ADD HL, BC"
        elif b0 == 0x0A: yield opc_start, 1, "LD A, (BC)"
        elif b0 == 0x0B: yield opc_start, 1, "DEC BC"
        elif b0 == 0x0C: yield opc_start, 1, "INC C"
        elif b0 == 0x0D: yield opc_start, 1, "DEC C"
        elif b0 == 0x0E: yield opc_start, 2, f"LD C, 0x{rb():02X}"; pc+=1
        elif b0 == 0x0F: yield opc_start, 1, "RRCA"
        elif b0 == 0x10: yield opc_start, 2, f"DJNZ 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x11: yield opc_start, 3, f"LD DE, 0x{rw():04X}"; pc+=2
        elif b0 == 0x12: yield opc_start, 1, "LD (DE), A"
        elif b0 == 0x13: yield opc_start, 1, "INC DE"
        elif b0 == 0x14: yield opc_start, 1, "INC D"
        elif b0 == 0x15: yield opc_start, 1, "DEC D"
        elif b0 == 0x16: yield opc_start, 2, f"LD D, 0x{rb():02X}"; pc+=1
        elif b0 == 0x17: yield opc_start, 1, "RLA"
        elif b0 == 0x18: yield opc_start, 2, f"JR 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x19: yield opc_start, 1, "ADD HL, DE"
        elif b0 == 0x1A: yield opc_start, 1, "LD A, (DE)"
        elif b0 == 0x1B: yield opc_start, 1, "DEC DE"
        elif b0 == 0x1C: yield opc_start, 1, "INC E"
        elif b0 == 0x1D: yield opc_start, 1, "DEC E"
        elif b0 == 0x1E: yield opc_start, 2, f"LD E, 0x{rb():02X}"; pc+=1
        elif b0 == 0x1F: yield opc_start, 1, "RRA"
        elif b0 == 0x20: yield opc_start, 2, f"JR NZ, 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x21: yield opc_start, 3, f"LD HL, 0x{rw():04X}"; pc+=2
        elif b0 == 0x22: yield opc_start, 3, f"LD (0x{rw():04X}), HL"; pc+=2
        elif b0 == 0x23: yield opc_start, 1, "INC HL"
        elif b0 == 0x24: yield opc_start, 1, "INC H"
        elif b0 == 0x25: yield opc_start, 1, "DEC H"
        elif b0 == 0x26: yield opc_start, 2, f"LD H, 0x{rb():02X}"; pc+=1
        elif b0 == 0x27: yield opc_start, 1, "DAA"
        elif b0 == 0x28: yield opc_start, 2, f"JR Z, 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x29: yield opc_start, 1, "ADD HL, HL"
        elif b0 == 0x2A: yield opc_start, 3, f"LD HL, (0x{rw():04X})"; pc+=2
        elif b0 == 0x2B: yield opc_start, 1, "DEC HL"
        elif b0 == 0x2C: yield opc_start, 1, "INC L"
        elif b0 == 0x2D: yield opc_start, 1, "DEC L"
        elif b0 == 0x2E: yield opc_start, 2, f"LD L, 0x{rb():02X}"; pc+=1
        elif b0 == 0x2F: yield opc_start, 1, "CPL"
        elif b0 == 0x30: yield opc_start, 2, f"JR NC, 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x31: yield opc_start, 3, f"LD SP, 0x{rw():04X}"; pc+=2
        elif b0 == 0x32: yield opc_start, 3, f"LD (0x{rw():04X}), A"; pc+=2
        elif b0 == 0x33: yield opc_start, 1, "INC SP"
        elif b0 == 0x34: yield opc_start, 1, "INC (HL)"
        elif b0 == 0x35: yield opc_start, 1, "DEC (HL)"
        elif b0 == 0x36: yield opc_start, 2, f"LD (HL), 0x{rb():02X}"; pc+=1
        elif b0 == 0x37: yield opc_start, 1, "SCF"
        elif b0 == 0x38: yield opc_start, 2, f"JR C, 0x{(opc_start+2+((rb() if rb() < 128 else rb()-256)) & 0xFFFF):04X}"; pc+=1
        elif b0 == 0x39: yield opc_start, 1, "ADD HL, SP"
        elif b0 == 0x3A: yield opc_start, 3, f"LD A, (0x{rw():04X})"; pc+=2
        elif b0 == 0x3B: yield opc_start, 1, "DEC SP"
        elif b0 == 0x3C: yield opc_start, 1, "INC A"
        elif b0 == 0x3D: yield opc_start, 1, "DEC A"
        elif b0 == 0x3E: yield opc_start, 2, f"LD A, 0x{rb():02X}"; pc+=1
        elif b0 == 0x3F: yield opc_start, 1, "CCF"
        elif 0x40 <= b0 <= 0x7F and b0 != 0x76:
            dst = (b0-0x40) >> 3; src = b0 & 7
            yield opc_start, 1, f"LD {r8[dst]}, {r8[src]}"
        elif b0 == 0x76: yield opc_start, 1, "HALT"
        elif 0x80 <= b0 <= 0x87:
            yield opc_start, 1, f"ADD A, {r8[b0 & 7]}"
        elif 0x88 <= b0 <= 0x8F:
            yield opc_start, 1, f"ADC A, {r8[b0 & 7]}"
        elif 0x90 <= b0 <= 0x97:
            yield opc_start, 1, f"SUB {r8[b0 & 7]}"
        elif 0x98 <= b0 <= 0x9F:
            yield opc_start, 1, f"SBC A, {r8[b0 & 7]}"
        elif 0xA0 <= b0 <= 0xA7:
            yield opc_start, 1, f"AND {r8[b0 & 7]}"
        elif 0xA8 <= b0 <= 0xAF:
            yield opc_start, 1, f"XOR {r8[b0 & 7]}"
        elif b0 == 0xB0: yield opc_start, 1, "OR B"
        elif b0 == 0xB1: yield opc_start, 1, "OR C"
        elif b0 == 0xB2: yield opc_start, 1, "OR D"
        elif b0 == 0xB3: yield opc_start, 1, "OR E"
        elif b0 == 0xB4: yield opc_start, 1, "OR H"
        elif b0 == 0xB5: yield opc_start, 1, "OR L"
        elif b0 == 0xB6: yield opc_start, 1, "OR (HL)"
        elif b0 == 0xB7: yield opc_start, 1, "OR A"
        elif 0xB8 <= b0 <= 0xBF:
            yield opc_start, 1, f"CP {r8[b0 & 7]}"
        elif b0 == 0xC0: yield opc_start, 1, "RET NZ"
        elif b0 == 0xC1: yield opc_start, 1, "POP BC"
        elif b0 == 0xC2: yield opc_start, 3, f"JP NZ, 0x{rw():04X}"; pc+=2
        elif b0 == 0xC3: yield opc_start, 3, f"JP 0x{rw():04X}"; pc+=2
        elif b0 == 0xC4: yield opc_start, 3, f"CALL NZ, 0x{rw():04X}"; pc+=2
        elif b0 == 0xC5: yield opc_start, 1, "PUSH BC"
        elif b0 == 0xC6: yield opc_start, 2, f"ADD A, 0x{rb():02X}"; pc+=1
        elif b0 == 0xC7: yield opc_start, 1, "RST 00H"
        elif b0 == 0xC8: yield opc_start, 1, "RET Z"
        elif b0 == 0xC9: yield opc_start, 1, "RET"
        elif b0 == 0xCA: yield opc_start, 3, f"JP Z, 0x{rw():04X}"; pc+=2
        elif b0 == 0xCB:
            b1 = rb(1); pc += 1
            cb_ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
            if b1 < 0x40:
                yield opc_start, 2, f"{cb_ops[(b1>>3)&7]} {r8[b1&7]}"
            elif b1 < 0x80:
                yield opc_start, 2, f"BIT {(b1>>3)&7}, {r8[b1&7]}"
            elif b1 < 0xC0:
                yield opc_start, 2, f"RES {(b1>>3)&7}, {r8[b1&7]}"
            else:
                yield opc_start, 2, f"SET {(b1>>3)&7}, {r8[b1&7]}"
        elif b0 == 0xCC: yield opc_start, 3, f"CALL Z, 0x{rw():04X}"; pc+=2
        elif b0 == 0xCD: yield opc_start, 3, f"CALL 0x{rw():04X}"; pc+=2
        elif b0 == 0xCE: yield opc_start, 2, f"ADC A, 0x{rb():02X}"; pc+=1
        elif b0 == 0xCF: yield opc_start, 1, "RST 08H"
        elif b0 == 0xD0: yield opc_start, 1, "RET NC"
        elif b0 == 0xD1: yield opc_start, 1, "POP DE"
        elif b0 == 0xD2: yield opc_start, 3, f"JP NC, 0x{rw():04X}"; pc+=2
        elif b0 == 0xD3: yield opc_start, 2, f"OUT (0x{rb():02X}), A"; pc+=1
        elif b0 == 0xD4: yield opc_start, 3, f"CALL NC, 0x{rw():04X}"; pc+=2
        elif b0 == 0xD5: yield opc_start, 1, "PUSH DE"
        elif b0 == 0xD6: yield opc_start, 2, f"SUB 0x{rb():02X}"; pc+=1
        elif b0 == 0xD7: yield opc_start, 1, "RST 10H"
        elif b0 == 0xD8: yield opc_start, 1, "RET C"
        elif b0 == 0xD9: yield opc_start, 1, "EXX"
        elif b0 == 0xDA: yield opc_start, 3, f"JP C, 0x{rw():04X}"; pc+=2
        elif b0 == 0xDB: yield opc_start, 2, f"IN A, (0x{rb():02X})"; pc+=1
        elif b0 == 0xDC: yield opc_start, 3, f"CALL C, 0x{rw():04X}"; pc+=2
        elif b0 == 0xDD:
            b1 = rb(1); pc += 1
            if b1 == 0x21: yield opc_start, 4, f"LD IX, 0x{rw(2):04X}"; pc+=2
            elif b1 == 0x22: yield opc_start, 4, f"LD (0x{rw(2):04X}), IX"; pc+=2
            elif b1 == 0x2A: yield opc_start, 4, f"LD IX, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0x36: d=rb(2); yield opc_start, 4, f"LD (IX+{d}), 0x{rb(3):02X}"; pc+=2
            elif b1 == 0xE1: yield opc_start, 2, "POP IX"
            elif b1 == 0xE5: yield opc_start, 2, "PUSH IX"
            elif b1 == 0xE9: yield opc_start, 2, "JP (IX)"
            else: yield opc_start, 2, f"DD {b1:02X}"
        elif b0 == 0xDE: yield opc_start, 2, f"SBC A, 0x{rb():02X}"; pc+=1
        elif b0 == 0xDF: yield opc_start, 1, "RST 18H"
        elif b0 == 0xE0: yield opc_start, 1, "RET PO"
        elif b0 == 0xE1: yield opc_start, 1, "POP HL"
        elif b0 == 0xE2: yield opc_start, 3, f"JP PO, 0x{rw():04X}"; pc+=2
        elif b0 == 0xE3: yield opc_start, 1, "EX (SP), HL"
        elif b0 == 0xE4: yield opc_start, 3, f"CALL PO, 0x{rw():04X}"; pc+=2
        elif b0 == 0xE5: yield opc_start, 1, "PUSH HL"
        elif b0 == 0xE6: yield opc_start, 2, f"AND 0x{rb():02X}"; pc+=1
        elif b0 == 0xE7: yield opc_start, 1, "RST 20H"
        elif b0 == 0xE8: yield opc_start, 1, "RET PE"
        elif b0 == 0xE9: yield opc_start, 1, "JP (HL)"
        elif b0 == 0xEA: yield opc_start, 3, f"JP PE, 0x{rw():04X}"; pc+=2
        elif b0 == 0xEB: yield opc_start, 1, "EX DE, HL"
        elif b0 == 0xEC: yield opc_start, 3, f"CALL PE, 0x{rw():04X}"; pc+=2
        elif b0 == 0xED:
            b1 = rb(1); pc += 1
            if b1 == 0x43: yield opc_start, 4, f"LD (0x{rw(2):04X}), BC"; pc+=2
            elif b1 == 0x4B: yield opc_start, 4, f"LD BC, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0x53: yield opc_start, 4, f"LD (0x{rw(2):04X}), DE"; pc+=2
            elif b1 == 0x5B: yield opc_start, 4, f"LD DE, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0x63: yield opc_start, 4, f"LD (0x{rw(2):04X}), HL"; pc+=2
            elif b1 == 0x6B: yield opc_start, 4, f"LD HL, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0x73: yield opc_start, 4, f"LD (0x{rw(2):04X}), SP"; pc+=2
            elif b1 == 0x7B: yield opc_start, 4, f"LD SP, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0x44: yield opc_start, 2, "NEG"
            elif b1 == 0x45: yield opc_start, 2, "RETN"
            elif b1 == 0x46: yield opc_start, 2, "IM 0"
            elif b1 == 0x4D: yield opc_start, 2, "RETI"
            elif b1 == 0x56: yield opc_start, 2, "IM 1"
            elif b1 == 0x5E: yield opc_start, 2, "IM 2"
            elif b1 == 0x47: yield opc_start, 2, "LD I, A"
            elif b1 == 0x4F: yield opc_start, 2, "LD R, A"
            elif b1 == 0x57: yield opc_start, 2, "LD A, I"
            elif b1 == 0x5F: yield opc_start, 2, "LD A, R"
            elif b1 == 0x67: yield opc_start, 2, "RRD"
            elif b1 == 0x6F: yield opc_start, 2, "RLD"
            elif b1 == 0xA0: yield opc_start, 2, "LDI"
            elif b1 == 0xA1: yield opc_start, 2, "CPI"
            elif b1 == 0xA8: yield opc_start, 2, "LDD"
            elif b1 == 0xA9: yield opc_start, 2, "CPD"
            elif b1 == 0xB0: yield opc_start, 2, "LDIR"
            elif b1 == 0xB1: yield opc_start, 2, "CPIR"
            elif b1 == 0xB8: yield opc_start, 2, "LDDR"
            elif b1 == 0xB9: yield opc_start, 2, "CPDR"
            else: yield opc_start, 2, f"ED {b1:02X}"
        elif b0 == 0xEE: yield opc_start, 2, f"XOR 0x{rb():02X}"; pc+=1
        elif b0 == 0xEF: yield opc_start, 1, "RST 28H"
        elif b0 == 0xF0: yield opc_start, 1, "RET P"
        elif b0 == 0xF1: yield opc_start, 1, "POP AF"
        elif b0 == 0xF2: yield opc_start, 3, f"JP P, 0x{rw():04X}"; pc+=2
        elif b0 == 0xF3: yield opc_start, 1, "DI"
        elif b0 == 0xF4: yield opc_start, 3, f"CALL P, 0x{rw():04X}"; pc+=2
        elif b0 == 0xF5: yield opc_start, 1, "PUSH AF"
        elif b0 == 0xF6: yield opc_start, 2, f"OR 0x{rb():02X}"; pc+=1
        elif b0 == 0xF7: yield opc_start, 1, "RST 30H"
        elif b0 == 0xF8: yield opc_start, 1, "RET M"
        elif b0 == 0xF9: yield opc_start, 1, "LD SP, HL"
        elif b0 == 0xFA: yield opc_start, 3, f"JP M, 0x{rw():04X}"; pc+=2
        elif b0 == 0xFB: yield opc_start, 1, "EI"
        elif b0 == 0xFC: yield opc_start, 3, f"CALL M, 0x{rw():04X}"; pc+=2
        elif b0 == 0xFD:
            b1 = rb(1); pc += 1
            if b1 == 0x21: yield opc_start, 4, f"LD IY, 0x{rw(2):04X}"; pc+=2
            elif b1 == 0x22: yield opc_start, 4, f"LD (0x{rw(2):04X}), IY"; pc+=2
            elif b1 == 0x2A: yield opc_start, 4, f"LD IY, (0x{rw(2):04X})"; pc+=2
            elif b1 == 0xE1: yield opc_start, 2, "POP IY"
            elif b1 == 0xE5: yield opc_start, 2, "PUSH IY"
            elif b1 == 0xE9: yield opc_start, 2, "JP (IY)"
            else: yield opc_start, 2, f"FD {b1:02X}"
        elif b0 == 0xFE: yield opc_start, 2, f"CP 0x{rb():02X}"; pc+=1
        elif b0 == 0xFF: yield opc_start, 1, "RST 38H"
        else: yield opc_start, 1, f"DB 0x{b0:02X}"
        
        pc += 1

print("=== FULL DISASSEMBLY OF K2526 BOOT ROM (first 256 bytes) ===")
for addr, length, mnemonic in z80_disasm(rom, 0, 256):
    raw = ' '.join(f'{rom[addr+i]:02X}' for i in range(min(length, len(rom)-addr)))
    print(f"  {addr:04X}: {raw:<12} {mnemonic}")

print()
print("=== BYTES 0x0C to 0x40 ===")
for addr, length, mnemonic in z80_disasm(rom, 0x0C, 0x40):
    raw = ' '.join(f'{rom[addr+i]:02X}' for i in range(min(length, len(rom)-addr)))
    print(f"  {addr:04X}: {raw:<12} {mnemonic}")
