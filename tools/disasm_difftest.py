#!/usr/bin/env python3
"""
Differential test for the a5120emu disassembler.

Cross-checks tools/z80_disasm2.py's hand-rolled decoder against the independent
third-party `z80dis` package at every byte offset of a binary. Instruction LENGTH
is the objective invariant; numeric operands (addresses/immediates) are compared
after normalisation. This catches off-by-one operand bugs, wrong instruction
lengths, and decoding gaps before they corrupt an analysis.

Requires: pip install z80dis  (already in the project venv)

Usage:
    venv/bin/python tools/disasm_difftest.py doc/EPROMS/zre.rom
    venv/bin/python tools/disasm_difftest.py boot_disk/format.com --org 0x100
    venv/bin/python tools/disasm_difftest.py <file> --max-report 40
"""
import argparse
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_our_engine():
    """Import Z80Disassembler from z80_disasm2.py without running its main()."""
    spec = importlib.util.spec_from_file_location(
        "z80_disasm2", os.path.join(HERE, "z80_disasm2.py"))
    mod = importlib.util.module_from_spec(spec)
    saved_argv = sys.argv
    sys.argv = ["z80_disasm2"]          # keep argparse in main() from firing on import
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved_argv
    return mod


def nums(text):
    """Multiset of numeric operand values, base- and notation-agnostic.

    Handles our engine's hex (1234H / 0FFH) and z80dis's 0x1234 plus bare
    decimal immediates (LD A,5, RST 0, BIT 1,A). Matched spans are consumed
    so the same digits aren't counted twice."""
    vals = []
    # (IX+0)/(IY+0) and z80dis's (IX)/(IY) are equivalent; drop the zero displacement
    work = text.replace("+0)", ")")
    # 0x-prefixed hex (z80dis addresses/immediates)
    def take_hex0x(m): vals.append(int(m.group(1), 16)); return " " * len(m.group(0))
    work = re.sub(r'0x([0-9a-fA-F]+)', take_hex0x, work)
    # H-suffixed hex (our engine)
    def take_hexH(m): vals.append(int(m.group(1), 16)); return " " * len(m.group(0))
    work = re.sub(r'\b([0-9A-Fa-f]+)H\b', take_hexH, work)
    # remaining bare decimals (small immediates, bit numbers, displacements)
    for m in re.finditer(r'\d+', work):
        vals.append(int(m.group(0)))
    return sorted(vals)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--org", default="0", help="origin address (hex/dec), default 0")
    ap.add_argument("--max-report", type=int, default=25,
                    help="max mismatches to print per category")
    args = ap.parse_args()

    try:
        from z80dis.z80 import decode, disasm
    except ImportError:
        sys.exit("z80dis not installed — run: venv/bin/pip install z80dis")

    org = int(args.org, 0)
    data = open(args.file, "rb").read()
    eng = load_our_engine()
    dis = eng.Z80Disassembler(data, org=org)

    len_mismatch = []
    operand_mismatch = []
    our_db = 0          # positions our engine could not decode (DB fallback)
    checked = 0

    for off in range(len(data)):
        addr = org + off
        window = data[off:off + 4]
        try:
            ref = decode(window, addr)
            ref_len = ref.len
            ref_txt = disasm(window, addr)
        except Exception:
            continue
        if ref_len <= 0 or off + ref_len > len(data):
            continue

        our_txt, our_len, _, _ = dis.decode_one(addr)
        checked += 1

        if our_txt.startswith("DB "):
            our_db += 1
            continue

        if our_len != ref_len:
            len_mismatch.append((addr, window[:max(our_len, ref_len)].hex(),
                                 our_txt, our_len, ref_txt, ref_len))
            continue

        if nums(our_txt) != nums(ref_txt):
            operand_mismatch.append((addr, window[:our_len].hex(),
                                     our_txt, ref_txt))

    print(f"=== disasm differential test: {args.file} (ORG {org:04X}H) ===")
    print(f"offsets checked: {checked}   our DB-fallbacks: {our_db}")
    print(f"length mismatches:  {len(len_mismatch)}")
    print(f"operand mismatches: {len(operand_mismatch)}")

    if len_mismatch:
        print("\n-- LENGTH MISMATCHES --")
        for a, hx, ot, ol, rt, rl in len_mismatch[:args.max_report]:
            print(f"  {a:04X}: {hx:<8}  ours='{ot}'(len {ol})  ref='{rt}'(len {rl})")
    if operand_mismatch:
        print("\n-- OPERAND MISMATCHES --")
        for a, hx, ot, rt in operand_mismatch[:args.max_report]:
            print(f"  {a:04X}: {hx:<8}  ours='{ot}'  ref='{rt}'")

    sys.exit(1 if (len_mismatch or operand_mismatch) else 0)


if __name__ == "__main__":
    main()
