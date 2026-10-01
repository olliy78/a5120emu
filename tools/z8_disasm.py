#!/usr/bin/env python3
"""Z8-Disassembler (UB8820M / U881 / Z8601) mit rekursivem Abstieg.

Geschrieben für die Firmware der Tastatur K7672 (UB8820M, Programm in zwei
externen 2716, doc/design/16_k8915.md §3.5), taugt aber für jedes Z8-Abbild.

    tools/z8_disasm.py 7672.03-D2.bin                  # Vektoren + Reset 000CH
    tools/z8_disasm.py fw.bin --entry 0x123,0x456 --org 0x800
    tools/z8_disasm.py fw.bin --linear 0x40 0x80       # Bereich stur linear

Ausgabe: Adresse, Bytes, Befehl; Bytes, die der Abstieg nicht erreicht, als
`DB` mit ASCII-Spalte.  Sprungziele werden als `L_xxxx` benannt.

Befehlsmatrix nach Zilog „Z8 Microcomputer Technical Manual", Tabelle der
Opcodes (Spalte = Adressierungsart, Zeile = Operation).  Unsicher ist nur die
Operandenreihenfolge der seltenen Befehle LDE/LDC in Speicherrichtung (92/D2):
ausgegeben als `LDC @rrS,rD` mit rD = oberes, rrS = unteres Halbbyte.
"""
import argparse
import sys

# ─── Register ────────────────────────────────────────────────────────────────
SFR = {0xF0: "SIO", 0xF1: "TMR", 0xF2: "T1", 0xF3: "PRE1", 0xF4: "T0",
       0xF5: "PRE0", 0xF6: "P2M", 0xF7: "P3M", 0xF8: "P01M", 0xF9: "IPR",
       0xFA: "IRQ", 0xFB: "IMR", 0xFC: "FLAGS", 0xFD: "RP", 0xFE: "SPH",
       0xFF: "SPL", 0x00: "P0", 0x01: "P1", 0x02: "P2", 0x03: "P3"}

CC = ["F", "LT", "LE", "ULE", "OV", "MI", "Z", "C",
      "", "GE", "GT", "UGT", "NOV", "PL", "NZ", "NC"]

ONE = {0x0: "DEC", 0x1: "RLC", 0x2: "INC", 0x4: "DA", 0x5: "POP", 0x6: "COM",
       0x7: "PUSH", 0x8: "DECW", 0x9: "RL", 0xA: "INCW", 0xB: "CLR", 0xC: "RRC",
       0xD: "SRA", 0xE: "RR", 0xF: "SWAP"}
TWO = {0x0: "ADD", 0x1: "ADC", 0x2: "SUB", 0x3: "SBC", 0x4: "OR", 0x5: "AND",
       0x6: "TCM", 0x7: "TM", 0xA: "CP", 0xB: "XOR"}
MISC = {0x8F: "DI", 0x9F: "EI", 0xAF: "RET", 0xBF: "IRET", 0xCF: "RCF",
        0xDF: "SCF", 0xEF: "CCF", 0xFF: "NOP"}


def reg(b):
    """8-Bit-Registeradresse; Ex = Arbeitsregister rx."""
    if (b & 0xF0) == 0xE0:
        return f"r{b & 15}"
    return SFR.get(b, f"{b:02X}H")


def rreg(b):
    if (b & 0xF0) == 0xE0:
        return f"rr{b & 15}"
    return SFR.get(b, f"{b:02X}H")


def decode(mem, pc, org):
    """→ (Länge, Text, [Folgeadressen], [Sprungziele], endet_fluss)."""
    def byte(i):
        a = pc - org + i
        return mem[a] if 0 <= a < len(mem) else 0

    op = byte(0)
    hi, lo = op >> 4, op & 15
    b1, b2 = byte(1), byte(2)

    if op in MISC:
        return 1, MISC[op], [], [], op in (0xAF, 0xBF)
    if lo == 0x8:
        return 2, f"LD r{hi},{reg(b1)}", [], [], False
    if lo == 0x9:
        return 2, f"LD {reg(b1)},r{hi}", [], [], False
    if lo == 0xA:
        t = (pc + 2 + (b1 - 256 if b1 > 127 else b1)) & 0xFFFF
        return 2, f"DJNZ r{hi},L_{t:04X}", [], [t], False
    if lo == 0xB:
        t = (pc + 2 + (b1 - 256 if b1 > 127 else b1)) & 0xFFFF
        c = CC[hi]
        return 2, f"JR {c + ',' if c else ''}L_{t:04X}", [], [t], hi == 8
    if lo == 0xC:
        return 2, f"LD r{hi},#{b1:02X}H", [], [], False
    if lo == 0xD:
        t = (b1 << 8) | b2
        c = CC[hi]
        return 3, f"JP {c + ',' if c else ''}L_{t:04X}", [], [t], hi == 8
    if lo == 0xE:
        return 1, f"INC r{hi}", [], [], False

    # Sonderfälle der Spalten 0–7
    if op == 0x30:
        return 2, f"JP @{rreg(b1)}", [], [], True
    if op == 0x31:
        return 2, f"SRP #{b1:02X}H", [], [], False
    if op == 0xD4:
        return 2, f"CALL @{rreg(b1)}", [], [], False
    if op == 0xD6:
        t = (b1 << 8) | b2
        return 3, f"CALL L_{t:04X}", [t], [], False
    if op == 0x82:
        return 2, f"LDE r{b1 >> 4},@rr{b1 & 15}", [], [], False
    if op == 0x83:
        return 2, f"LDEI @r{b1 >> 4},@rr{b1 & 15}", [], [], False
    if op == 0x92:
        return 2, f"LDE @rr{b1 & 15},r{b1 >> 4}", [], [], False
    if op == 0x93:
        return 2, f"LDEI @rr{b1 & 15},@r{b1 >> 4}", [], [], False
    if op == 0xC2:
        return 2, f"LDC r{b1 >> 4},@rr{b1 & 15}", [], [], False
    if op == 0xC3:
        return 2, f"LDCI @r{b1 >> 4},@rr{b1 & 15}", [], [], False
    if op == 0xD2:
        return 2, f"LDC @rr{b1 & 15},r{b1 >> 4}", [], [], False
    if op == 0xD3:
        return 2, f"LDCI @rr{b1 & 15},@r{b1 >> 4}", [], [], False
    if op == 0xC7:
        return 3, f"LD r{b1 >> 4},{reg(b2)}(r{b1 & 15})", [], [], False
    if op == 0xD7:
        return 3, f"LD {reg(b2)}(r{b1 & 15}),r{b1 >> 4}", [], [], False
    if op == 0xE3:
        return 2, f"LD r{b1 >> 4},@r{b1 & 15}", [], [], False
    if op == 0xF3:
        return 2, f"LD @r{b1 >> 4},r{b1 & 15}", [], [], False
    if op == 0xE4:
        return 3, f"LD {reg(b2)},{reg(b1)}", [], [], False
    if op == 0xE5:
        return 3, f"LD {reg(b2)},@{reg(b1)}", [], [], False
    if op == 0xF5:
        return 3, f"LD @{reg(b2)},{reg(b1)}", [], [], False
    if op == 0xE6:
        return 3, f"LD {reg(b1)},#{b2:02X}H", [], [], False
    if op == 0xE7:
        return 3, f"LD @{reg(b1)},#{b2:02X}H", [], [], False

    if lo in (0, 1) and hi in ONE:
        name = ONE[hi]
        r = rreg(b1) if hi in (0x8, 0xA) else reg(b1)
        return 2, f"{name} {'@' if lo == 1 else ''}{r}", [], [], False
    if 2 <= lo <= 7 and hi in TWO:
        n = TWO[hi]
        if lo == 2:
            return 2, f"{n} r{b1 >> 4},r{b1 & 15}", [], [], False
        if lo == 3:
            return 2, f"{n} r{b1 >> 4},@r{b1 & 15}", [], [], False
        if lo == 4:
            return 3, f"{n} {reg(b2)},{reg(b1)}", [], [], False
        if lo == 5:
            return 3, f"{n} {reg(b2)},@{reg(b1)}", [], [], False
        if lo == 6:
            return 3, f"{n} {reg(b1)},#{b2:02X}H", [], [], False
        if lo == 7:
            return 3, f"{n} @{reg(b1)},#{b2:02X}H", [], [], False
    return 1, f"DB {op:02X}H        ; ??? unbelegter Opcode", [], [], True


def trace(mem, org, entries):
    code = {}           # pc → (len, text)
    labels = set()
    todo = list(entries)
    end = org + len(mem)
    while todo:
        pc = todo.pop()
        while org <= pc < end and pc not in code:
            n, text, calls, jumps, stop = decode(mem, pc, org)
            code[pc] = (n, text)
            for t in calls + jumps:
                labels.add(t)
                todo.append(t)
            if stop:
                break
            pc += n
    return code, labels


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--org", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--entry", action="append", default=[],
                    help="zusätzlicher Einsprung (mehrfach oder kommagetrennt)")
    ap.add_argument("--no-vectors", action="store_true",
                    help="IRQ0–5 (0000H–000BH) nicht als Einsprünge werten")
    ap.add_argument("--linear", nargs=2, type=lambda s: int(s, 0), metavar=("VON", "BIS"))
    a = ap.parse_args()

    mem = open(a.image, "rb").read()
    org = a.org
    if a.linear:
        pc = a.linear[0]
        while pc < a.linear[1]:
            n, text, *_ = decode(mem, pc, org)
            bs = " ".join(f"{mem[pc - org + i]:02X}" for i in range(n))
            print(f"{pc:04X}  {bs:<9} {text}")
            pc += n
        return

    entries = [int(x, 0) for e in a.entry for x in e.split(",") if x]
    vec = {}
    if not a.no_vectors and org == 0:
        for i in range(6):
            v = (mem[2 * i] << 8) | mem[2 * i + 1]
            if v:
                vec[v] = f"IRQ{i}"
                entries.append(v)
        entries.append(0x000C)
        vec.setdefault(0x000C, "RESET")
    code, labels = trace(mem, org, entries)

    pc = org
    end = org + len(mem)
    while pc < end:
        if pc in code:
            n, text = code[pc]
            if pc in vec:
                print(f"\n; ─── {vec[pc]} ───")
            if pc in labels or pc in vec:
                print(f"L_{pc:04X}:")
            bs = " ".join(f"{mem[pc - org + i]:02X}" for i in range(n))
            print(f"{pc:04X}  {bs:<9} {text}")
            pc += n
        else:
            # Datenblock bis zum nächsten Code, höchstens 16 Byte je Zeile
            s = pc
            while pc < end and pc not in code and pc - s < 16:
                pc += 1
            chunk = mem[s - org:pc - org]
            asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
            print(f"{s:04X}  DB " + ",".join(f"{c:02X}" for c in chunk) + f"   ; {asc}")


if __name__ == "__main__":
    main()
