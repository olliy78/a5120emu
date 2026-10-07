#!/usr/bin/env python3
"""Erzeugt app/ui/p8000_zeichensatz.py aus den Zeichengenerator-Abzuegen des P8000-Terminals.

Quelle: doc/p8000/eproms/TERMINAL/P8TEZS (Zeichensatz 1, ASCII) und P8TDZS (Zeichensatz 2,
deutsch: § Ä Ö Ü ä ö ü ß an den Stellen @ [ \\ ] { | } ~).  Je Abzug 2 KB = 128 Zeichen x 16 Byte,
je Zeile ein Byte (Bit 7 = linkes Pixel), 12 Zeilen sind sichtbar (Matrix 8 x 12, hw_terminal.md §1).

    python3 tools/p8000/zeichensatz_zu_py.py
"""
from pathlib import Path

WURZEL = Path(__file__).resolve().parents[2]
QUELLE = WURZEL / "doc/p8000/eproms/TERMINAL"
ZIEL = WURZEL / "app/ui/p8000_zeichensatz.py"


def hexblock(daten: bytes) -> str:
    zeilen = [daten[i:i + 32].hex() for i in range(0, len(daten), 32)]
    return "\n".join(f'    "{z}"' for z in zeilen)


def main():
    teile = ['"""Zeichengenerator des P8000-Terminals — ERZEUGT von tools/p8000/zeichensatz_zu_py.py.\n\n'
             'Nicht von Hand aendern.  Je Satz 128 Zeichen x 16 Byte (eine Pixelzeile je Byte, Bit 7 = links);\n'
             'sichtbar sind die ersten 12 Zeilen.  ZG1 = P8TEZS (ASCII), ZG2 = P8TDZS (deutsch).\n"""\n']
    for name, datei in (("ZG1", "P8TEZS"), ("ZG2", "P8TDZS")):
        daten = (QUELLE / datei).read_bytes()
        assert len(daten) == 2048, datei
        teile.append(f"\n{name} = bytes.fromhex(\n{hexblock(daten)}\n)\n")
    ZIEL.write_text("".join(teile), encoding="utf-8")
    print("geschrieben:", ZIEL)


if __name__ == "__main__":
    main()
