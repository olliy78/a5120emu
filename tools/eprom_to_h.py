#!/usr/bin/env python3
"""Convert EPROM binary file to C++ constexpr uint8_t array header."""

import sys
import argparse
import pathlib


def convert(bin_path: str, symbol: str, out_path: str,
            weitere: list[str] | None = None, anhaengen: bool = False) -> None:
    # Mehrere Bausteine hintereinander (z. B. ROM 175 + 176 + 177 der K8915-V2-ZRE):
    # bin_path zuerst, dann jede --weitere in der angegebenen Reihenfolge.
    quellen = [bin_path] + list(weitere or [])
    data = b"".join(pathlib.Path(q).read_bytes() for q in quellen)
    lines = [
        f"// Generated from: {' + '.join(quellen)}",
        f"// Size: {len(data)} bytes",
    ]
    if not anhaengen:
        lines += ["#pragma once", "#include <cstdint>"]
    lines += [
        f"static constexpr uint8_t {symbol}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    out = pathlib.Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    if anhaengen:
        # weiteres Symbol in dieselbe Datei (mehrere Felder je Header, z. B. rom_mon8.h)
        with out.open("a") as f:
            f.write(text)
    else:
        out.write_text(text)
    print(f"Written {len(data)} bytes → {out_path} (symbol: {symbol})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert EPROM .bin to C++ constexpr header"
    )
    parser.add_argument("bin_path", help="Input .bin file")
    parser.add_argument("symbol", help="C++ symbol name (e.g. ZRE_BOOT_ROM)")
    parser.add_argument("out_path", help="Output .h file path")
    parser.add_argument("--weitere", action="append", default=[], metavar="BIN",
                        help="weitere .bin, hinter bin_path angehängt (mehrfach, in Reihenfolge)")
    parser.add_argument("--anhaengen", action="store_true",
                        help="an eine bestehende Ausgabedatei anhängen (ohne #pragma once/#include)")
    args = parser.parse_args()
    convert(args.bin_path, args.symbol, args.out_path, args.weitere, args.anhaengen)


if __name__ == "__main__":
    main()
