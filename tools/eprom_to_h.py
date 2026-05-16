#!/usr/bin/env python3
"""Convert EPROM binary file to C++ constexpr uint8_t array header."""

import sys
import argparse
import pathlib


def convert(bin_path: str, symbol: str, out_path: str) -> None:
    data = pathlib.Path(bin_path).read_bytes()
    lines = [
        f"// Generated from: {bin_path}",
        f"// Size: {len(data)} bytes",
        "#pragma once",
        "#include <cstdint>",
        f"static constexpr uint8_t {symbol}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    out = pathlib.Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print(f"Written {len(data)} bytes → {out_path} (symbol: {symbol})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert EPROM .bin to C++ constexpr header"
    )
    parser.add_argument("bin_path", help="Input .bin file")
    parser.add_argument("symbol", help="C++ symbol name (e.g. ZRE_BOOT_ROM)")
    parser.add_argument("out_path", help="Output .h file path")
    args = parser.parse_args()
    convert(args.bin_path, args.symbol, args.out_path)


if __name__ == "__main__":
    main()
