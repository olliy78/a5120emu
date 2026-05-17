with open('/home/olliy/projects/a5120emu/doc/EPROMS/Prozessoreinheit_ZRE_K2526_A26.bin', 'rb') as f:
    data = bytearray(f.read())

print(f"File size: {len(data)} bytes")
print()
print("First 64 bytes as hex:")
for i in range(0, min(64, len(data)), 16):
    hex_bytes = ' '.join(f'{b:02X}' for b in data[i:i+16])
    print(f"{i:04X}: {hex_bytes}")

print()
# Check doubled byte pattern
print("Checking byte pairs (first 32 pairs):")
for i in range(0, min(64, len(data)), 2):
    same = "==" if data[i] == data[i+1] else "!="
    print(f"pair {i//2:02d} ({i:04X}): {data[i]:02X} {same} {data[i+1]:02X}")

print()
# Disassemble using even bytes only
print("Even-indexed bytes (every other byte starting from 0):")
even_bytes = data[0::2]
for i in range(0, min(64, len(even_bytes)), 16):
    hex_bytes = ' '.join(f'{b:02X}' for b in even_bytes[i:i+16])
    print(f"{i:04X}: {hex_bytes}")

print()    
# Simple Z80 disassembly of even bytes
import struct

def disasm_simple(rom, start=0, count=30):
    pc = start
    results = []
    i = 0
    while i < count and pc < len(rom):
        b = rom[pc]
        if b == 0xEE:  # XOR A, n
            n = rom[pc+1] if pc+1 < len(rom) else 0
            results.append((pc, f"XOR A, 0x{n:02X}"))
            pc += 2
        elif b == 0x77:  # LD (HL), A
            results.append((pc, "LD (HL), A"))
            pc += 1
        elif b == 0xF3:  # DI
            results.append((pc, "DI"))
            pc += 1
        elif b == 0x19:  # ADD HL, DE
            results.append((pc, "ADD HL, DE"))
            pc += 1
        elif b == 0x27:  # DAA
            results.append((pc, "DAA"))
            pc += 1
        elif b == 0x50:  # LD D, B
            results.append((pc, "LD D, B"))
            pc += 1
        elif b == 0x04:  # INC B
            results.append((pc, "INC B"))
            pc += 1
        elif b == 0x45:  # LD B, L
            results.append((pc, "LD B, L"))
            pc += 1
        elif b == 0xF0:  # RET P
            results.append((pc, "RET P"))
            pc += 1
        elif b == 0xC3:  # JP nn
            lo = rom[pc+1] if pc+1 < len(rom) else 0
            hi = rom[pc+2] if pc+2 < len(rom) else 0
            results.append((pc, f"JP 0x{hi:02X}{lo:02X}"))
            pc += 3
        elif b == 0x31:  # LD SP, nn
            lo = rom[pc+1] if pc+1 < len(rom) else 0
            hi = rom[pc+2] if pc+2 < len(rom) else 0
            results.append((pc, f"LD SP, 0x{hi:02X}{lo:02X}"))
            pc += 3
        elif b == 0xD3:  # OUT (n), A
            n = rom[pc+1] if pc+1 < len(rom) else 0
            results.append((pc, f"OUT (0x{n:02X}), A"))
            pc += 2
        elif b == 0x3E:  # LD A, n
            n = rom[pc+1] if pc+1 < len(rom) else 0
            results.append((pc, f"LD A, 0x{n:02X}"))
            pc += 2
        else:
            results.append((pc, f"DB 0x{b:02X}"))
            pc += 1
        i += 1
    return results

print("Disassembly of FULL ROM (as Z80 reads sequentially):")
for addr, instr in disasm_simple(data, 0, 30):
    print(f"  {addr:04X}: {instr}")

print()
print("Disassembly of EVEN-BYTES-ONLY interpretation:")
for addr, instr in disasm_simple(even_bytes, 0, 30):
    print(f"  {addr:04X}: {instr}")
