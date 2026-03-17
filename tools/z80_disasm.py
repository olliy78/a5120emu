#!/usr/bin/env python3
"""
Z80 Disassembler for CP/M .COM files
Specifically for Robotron A5120 format.com
"""

import sys
import struct
from z80dis.z80 import decode, disasm, INSTRTYPE

ORG = 0x0100

def read_binary(path):
    with open(path, 'rb') as f:
        return f.read()

def decode_instruction(data, offset):
    """Decode one instruction at offset in data. Returns (mnemonic, length, is_flow_change, targets)"""
    if offset >= len(data):
        return None, 0, False, []
    
    try:
        d = decode(data[offset:], 0)
        length = d.len
        mnemonic = disasm(data[offset:], 0)
        
        # Fix address display for jumps/calls - add ORG offset
        addr = offset + ORG
        
        # Determine if it's a flow control instruction and get targets
        is_flow = False
        targets = []
        
        opcode = data[offset]
        
        # Check for prefix bytes
        actual_opcode = opcode
        prefix_len = 0
        if opcode in (0xCB, 0xED):
            if offset + 1 < len(data):
                actual_opcode = data[offset + 1]
                prefix_len = 1
        elif opcode in (0xDD, 0xFD):
            if offset + 1 < len(data):
                if data[offset + 1] == 0xCB:
                    prefix_len = 2
                else:
                    actual_opcode = data[offset + 1]
                    prefix_len = 1
        
        if prefix_len == 0:
            # JP nn
            if opcode == 0xC3:
                target = struct.unpack('<H', data[offset+1:offset+3])[0]
                targets.append(target)
                is_flow = True
            # JP cc,nn
            elif opcode in (0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA):
                target = struct.unpack('<H', data[offset+1:offset+3])[0]
                targets.append(target)
                is_flow = True
            # JR e
            elif opcode == 0x18:
                rel = data[offset+1]
                if rel >= 128:
                    rel -= 256
                target = addr + 2 + rel
                targets.append(target)
                is_flow = True
            # JR cc,e
            elif opcode in (0x20, 0x28, 0x30, 0x38):
                rel = data[offset+1]
                if rel >= 128:
                    rel -= 256
                target = addr + 2 + rel
                targets.append(target)
                is_flow = True
            # CALL nn
            elif opcode == 0xCD:
                target = struct.unpack('<H', data[offset+1:offset+3])[0]
                targets.append(target)
                is_flow = True
            # CALL cc,nn
            elif opcode in (0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC):
                target = struct.unpack('<H', data[offset+1:offset+3])[0]
                targets.append(target)
                is_flow = True
            # DJNZ e
            elif opcode == 0x10:
                rel = data[offset+1]
                if rel >= 128:
                    rel -= 256
                target = addr + 2 + rel
                targets.append(target)
                is_flow = True
            # RST
            elif opcode in (0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF):
                is_flow = True
                targets.append(opcode & 0x38)
        
        return mnemonic, length, is_flow, targets
    except Exception as e:
        return None, 1, False, []


def find_strings(data, min_len=4):
    """Find potential ASCII string regions in the data"""
    strings = {}
    i = 0
    while i < len(data):
        if 0x20 <= data[i] <= 0x7E or data[i] in (0x0D, 0x0A, 0x09):
            start = i
            while i < len(data) and (0x20 <= data[i] <= 0x7E or data[i] in (0x0D, 0x0A, 0x09, 0x00, 0x24)):
                if data[i] == 0x00 or data[i] == 0x24:
                    i += 1
                    break
                i += 1
            length = i - start
            if length >= min_len:
                strings[start] = length
        else:
            i += 1
    return strings

def first_pass(data):
    """First pass: decode all instructions, find all branch targets"""
    labels = set()
    instructions = {}  # offset -> (mnemonic, length, targets)
    
    offset = 0
    while offset < len(data):
        mnemonic, length, is_flow, targets = decode_instruction(data, offset)
        if mnemonic is None:
            offset += 1
            continue
        
        addr = offset + ORG
        instructions[offset] = (mnemonic, length, targets)
        
        for t in targets:
            if ORG <= t < ORG + len(data):
                labels.add(t)
        
        offset += length
    
    return instructions, labels

def is_printable_run(data, offset, min_len=5):
    """Check if there's a run of printable ASCII at offset"""
    count = 0
    i = offset
    while i < len(data):
        b = data[i]
        if 0x20 <= b <= 0x7E or b in (0x0D, 0x0A):
            count += 1
            i += 1
        elif b == 0x24 and count >= min_len:  # '$' terminator
            return count + 1
        elif b == 0x00 and count >= min_len:
            return count + 1
        else:
            break
    if count >= min_len:
        return count
    return 0

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/home/olliy/projects/a5120emu/boot_disk/format.com'
    data = read_binary(path)
    
    print(f"; File: {path}")
    print(f"; Size: {len(data)} bytes (0x{len(data):04X})")
    print(f"; Load address: 0x{ORG:04X}")
    print(f"; End address: 0x{ORG + len(data) - 1:04X}")
    print()
    
    # First pass
    instructions, labels = first_pass(data)
    
    # Output labels sorted
    print("; === Branch/Call targets ===")
    for l in sorted(labels):
        print(f"; L{l:04X} at file offset 0x{l-ORG:04X}")
    print()
    
    # Second pass: output disassembly
    print(f"\tORG\t{ORG:04X}H")
    print()
    
    offset = 0
    while offset < len(data):
        addr = offset + ORG
        
        # Print label if this is a target
        if addr in labels:
            print(f"L{addr:04X}:")
        
        # Get hex bytes for this instruction
        if offset in instructions:
            mnemonic, length, targets = instructions[offset]
            hex_bytes = ' '.join(f'{data[offset+i]:02X}' for i in range(length))
            
            # Fix mnemonic to use labels
            display = mnemonic
            for t in targets:
                if ORG <= t < ORG + len(data):
                    display = display.replace(f'0x{t:04X}', f'L{t:04X}')
                    display = display.replace(f'0x{t:04x}', f'L{t:04X}')
            
            # Also fix addresses that don't have labels but are absolute
            # Replace 0xNNNN with NNNN format
            import re
            def fix_hex(m):
                val = int(m.group(1), 16)
                return f'{val:04X}H'
            display = re.sub(r'0x([0-9a-fA-F]{4})', fix_hex, display)
            display = re.sub(r'0x([0-9a-fA-F]{2})', lambda m: f'{int(m.group(1),16):02X}H', display)
            
            print(f"\t{display:30s} ; {addr:04X}: {hex_bytes}")
            offset += length
        else:
            # Data byte
            print(f"\tDB\t{data[offset]:02X}H\t\t\t ; {addr:04X}: {data[offset]:02X}")
            offset += 1

if __name__ == '__main__':
    main()
