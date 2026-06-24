#!/usr/bin/env python3
"""
gen_zre_prn.py — erzeugt doc/EPROMS/zre.prn (kommentiertes MACRO-80-Listing)
fuer k1520dbg / boot_trace (`-l`).

Quellen:
  doc/EPROMS/zre.rom      autoritative Objektbytes (1 KB Boot-EPROM, Lade-ROM A26)
  doc/EPROMS/zre_rom.asm  von Hand kuratierte Labels + deutsche Kommentare

Ausgabe = von tools/prn_listing.h parsbares Format:
  ADDR  BYTES   [LABEL:]<TAB>MNEMONIC<TAB><TAB>;Kommentar
Die linke Marge (Adresse + Bytes) enthaelt nie Tab/':'; das Quellfeld beginnt
am Label (':' vor erstem Tab) bzw. am ersten Tab.

Die Objektbytes werden aus zre.rom gelesen (Laenge = naechste Adresse - diese),
Mnemonic/Label/Kommentar aus zre_rom.asm uebernommen.  Damit ist das Listing
byte-genau und traegt die schon vorhandene Doku.

Aufruf:  python3 tools/gen_zre_prn.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM  = os.path.join(ROOT, "doc/EPROMS/zre.rom")
ASM  = os.path.join(ROOT, "doc/EPROMS/zre_rom.asm")
OUT  = os.path.join(ROOT, "doc/EPROMS/zre.prn")

# Code-Zeile in zre_rom.asm:  [LABEL:]  AAAA  <mnemonic>
#   Label am Zeilenanfang (nach Leerzeichen), endet mit ':'; dann 4-Hex-Adresse,
#   zwei Leerzeichen, dann der Mnemonic.
CODE = re.compile(r'^\s*(?:([A-Za-z_][\w]*):\s+)?([0-9A-Fa-f]{4})  (.+?)\s*$')


def clean_comment(c):
    """'===== X =====' -> '[X]', sonst unveraendert (getrimmt)."""
    m = re.match(r'^=+\s*(.*?)\s*=+$', c)
    return "[" + m.group(1) + "]" if m else c


def main():
    data = open(ROM, "rb").read()
    if len(data) != 1024:
        raise SystemExit(f"unerwartete ROM-Groesse {len(data)} (erwartet 1024)")

    entries = []   # (addr, label, mnemonic, comment)
    pending = []   # gesammelte Kommentarzeilen vor der naechsten Code-Zeile
    for raw in open(ASM, encoding="utf-8"):
        line = raw.rstrip("\n")
        s = line.strip()
        if not s:
            continue
        if s.startswith(";"):
            c = s[1:].strip()
            if re.fullmatch(r'=+', c.replace(" ", "")):
                pending = []          # nackter Trennstrich (Dateikopf) -> Puffer leeren
                continue
            cc = clean_comment(c)
            if cc and cc != "[]":
                pending.append(cc)
            continue
        m = CODE.match(line)
        if not m:
            continue
        label, addr_s, mnem = m.group(1), m.group(2), m.group(3).strip()
        entries.append([int(addr_s, 16), label, mnem, " | ".join(pending)])
        pending = []

    entries.sort(key=lambda e: e[0])
    addrs = [e[0] for e in entries]
    if len(set(addrs)) != len(addrs):
        raise SystemExit("doppelte Adressen im asm")
    if addrs[0] != 0x0000 or addrs[-1] > 0x03FF:
        raise SystemExit(f"unerwarteter Adressbereich {addrs[0]:#06x}..{addrs[-1]:#06x}")

    hdr = [
        "; ============================================================================",
        "; zre.prn  -  ZRE/K2526 Boot-EPROM (Lade-ROM A26), 1 KB, 0000H-03FFH",
        ";",
        "; Kommentiertes MACRO-80-kompatibles Listing fuer k1520dbg / boot_trace (-l).",
        "; Erzeugt von tools/gen_zre_prn.py aus zre.rom (Bytes) + zre_rom.asm (Kommentare).",
        "; Bytes byte-bestaetigt gegen den realen Chip (Hauptteil 0100H-03FFH ==",
        "; A5120_ZRE_rom.bin[0000:0300], dem realen EPROM-Dump).",
        ";",
        "; Speicher:  0000-00FF Reset/Init-Prolog (loescht 2 KB RAM, IM2, PIO, ISR-Vekt.)",
        ";            0100-03FF Hauptteil (Drive-Probe, ZVE2-DMA-Bootloader, ISRs)",
        "; IM2-ISRs:  Vektor 0xB8 -> 007A (BS-PIO),  Vektor 0xBA -> 01C7 (Index-Puls)",
        "; RAM-Vars:  03F0 Ladeadr | 03F3 Zyl | 03F5 Kopf/Sektor | 03F7 Index-Zaehler",
        ";            03F8 Done-Flag(0/1/3) | 03FA Retry | 03FC Port10-Ctrl | 03FD IDAM-Pfad",
        "; ============================================================================",
        "",
        "\tORG\t0000H",
        "",
    ]
    lines = list(hdr)
    for i, (addr, label, mnem, comment) in enumerate(entries):
        nxt = entries[i + 1][0] if i + 1 < len(entries) else 0x0400
        ln = max(1, nxt - addr)
        hexb = " ".join(f"{b:02X}" for b in data[addr:addr + ln])
        body = (f"{label}:\t{mnem}" if label else f"\t{mnem}")
        if comment:
            body += f"\t\t;{comment}"
        lines.append(f"{addr:04X}  {hexb:<14}" + body)
    lines += ["", "\tEND"]
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"geschrieben: {os.path.relpath(OUT, ROOT)}  ({len(entries)} Code-Zeilen)")


if __name__ == "__main__":
    main()
