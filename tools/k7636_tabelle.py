#!/usr/bin/env python3
"""Erzeugt core/peripherals/k7636/k7636_tabelle.h aus doc/a5120_alt/k7636_01_codes.csv.

Aufruf (aus der Projektwurzel):  python3 tools/k7636_tabelle.py
Die CSV ist die Quelle (Abschrift von Codetabelle K7636.01, SP S. 7, AP A3a); hier
werden nur die in doc/a5120_alt/k7636.md §6/§7 festgelegten Berichtigungen
angewendet, sonst ist die Tabelle wie gedruckt.
"""
import csv
import pathlib

wurzel = pathlib.Path(__file__).resolve().parent.parent
zeilen = list(csv.DictReader(open(wurzel / "doc/a5120_alt/k7636_01_codes.csv", encoding="utf-8")))
assert len(zeilen) == 128

# Abweichungen von der gedruckten Tabelle (k7636.md §6, §7):
#   A15 (Return-Pfeil): a gedruckt DA, 0A erwartet (.03 + CP/A kennen nur 0AH)
#   C02 (S): b gedruckt 73, 53 erwartet (Grossbuchstabe) [?] - Druckfehler, auch auf .03 so
KORREKTUR = {"A15": (0x0A, 0x0A), "C02": (0x73, 0x53)}

out = []
py = []
for z in zeilen:
    adr = int(z["rechenadresse_hex"], 16)
    assert adr == len(out)
    pos = z["pos"] if z["pos"] != "-" else ""
    pos = pos.split("/")[0]            # B11/B99 -> B11
    a, b = int(z["code_a_hex"], 16), int(z["code_b_hex"], 16)
    a, b = KORREKTUR.get(pos, (a, b))
    if pos:
        for p in (["B11", "B99"] if pos == "B11" else [pos]):
            py.append(f'    "{p}": (0x{adr:02X}, 0x{a:02X}, 0x{b:02X}),')
    out.append(f'    {{"{pos}", 0x{a:02X}, 0x{b:02X}}},  // {adr:02X}H {z["leitung"]}/{z["zeile"]}')

(wurzel / "core/peripherals/k7636/k7636_tabelle.h").write_text(
    "// ERZEUGT von tools/k7636_tabelle.py aus doc/a5120_alt/k7636_01_codes.csv - nicht von Hand aendern.\n"
    "#pragma once\n#include <cstdint>\n\n"
    "struct K7636TabellenEintrag { const char* pos; uint8_t a; uint8_t b; };\n\n"
    "/// Codetabelle K7636.01 (CTAB 1), Index = Rechenadresse 00H-7FH; a = Grund-, b = Umschaltstellung.\n"
    "inline constexpr K7636TabellenEintrag K7636_TABELLE[128] = {\n" + "\n".join(out) + "\n};\n",
    encoding="utf-8")

(wurzel / "app/ui/k7636_tabelle.py").write_text(
    "\"\"\"ERZEUGT von tools/k7636_tabelle.py aus doc/a5120_alt/k7636_01_codes.csv - nicht von Hand aendern.\n\n"
    "Codetabelle K7636.01 (CTAB 1): Position -> (Rechenadresse, Code a, Code b).\"\"\"\n\n"
    "POSITIONEN = {\n" + "\n".join(py) + "\n}\n", encoding="utf-8")
