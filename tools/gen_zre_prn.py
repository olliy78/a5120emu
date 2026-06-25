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


# CPU-Zuordnung der ROM-Adressen (empirisch via boot_trace --coverage cpu,pc + Kontrollfluss):
#   ZVE2 (DMA-CPU) = die EINE STROBE_LOOP-Sektorlade-Routine 0x01DD..0x027D
#                    (IDAM-Suche + INI/INIR-Transfer + DONE + Mehrsektor-Reentry JP 01F8h),
#                    betreten nur ueber RAM[0]=JP 01DD.
#   ZVE1 (Haupt-CPU) = alles andere (Reset/Init, Seek/Retry, Drive-Init, IM2-Index-ISR
#                    0x01C7, Signatur-Check, Alt-Boot/Bildschirm/Fehler, RAM-Vars).
ZVE2_LO, ZVE2_HI = 0x01DD, 0x027D


def cpu_of(addr):
    return "ZVE2" if ZVE2_LO <= addr <= ZVE2_HI else "ZVE1"


# Session-verifizierte Anreicherungen (2026-06-24, gegen echtes zre.rom + boot_trace
# --coverage + analyse_zre_rom_boot.md geprueft). Werden an den Harvest-Kommentar
# angehaengt. Schwerpunkt: FM/MFM-Umschaltung ([0x03FD] bit1) und CRC.
EXTRA = {
    0x0137: "FM/MFM: A=0x87 = Default-FM-Pfadbyte (Trial-and-Error-Startwert). [0x03FD] bit1 "
            "geht @01AD nach Port 0x10 = Signal MK des K5122 (Tor A bit1) und waehlt FM(1)/MFM(0). "
            "bit7=1 unterdrueckt Step-Puls. Abgelegt auf der Index-OK-Erfolgsstrecke "
            "(JR Z @0139 -> STORE_03FD @0153).",
    0x0151: "FM<->MFM-UMSCHALTUNG: XOR 02h kippt bit1 (0x87 FM <-> 0x85 MFM). "
            "WAIT_INDEX_OK-/Retry-Pfad probiert die jeweils andere Aufzeichnungsart.",
    0x0153: "Speichert FM/MFM-Pfadbyte [0x03FD]: 0x87=FM (Default), 0x85=MFM (nach Toggle). "
            "Wird @01AD nach Port 0x10 geladen und von ZVE2 @022B/@025A (BIT 1) ausgewertet.",
    0x01AD: "Laedt FM/MFM-Pfadbyte [0x03FD] in Port 0x10 = setzt FM/MFM-Modus des "
            "K5122-Datenseparators fuer den ZVE2-Sektor-Read.",
    0x0228: "FM/MFM-Verzweigung: liest [0x03FD] fuer den anschliessenden BIT-1-Test.",
    0x022B: "BIT 1 von [0x03FD] = Port-0x10-Signal MK: bit1=1 FM (MK->/MK=0: K5122 erkennt "
            "FM-Marke, IDAM-FE direkt -> NZ-Pfad, wenige Bytes bis FE); bit1=0 MFM (/MK=1: "
            "K5122 erkennt MFM-A1-Sync, A1(A1A1) vor FE -> Z-Pfad, mehr Bytes bis FE). Die "
            "eigentliche FM/MFM-Demodulation macht der K5122 (PLL + Marken-ROM), nicht ZVE2.",
    0x0245: "Die 2 nachlaufenden INI lesen die DATEN-CRC-Bytes in den Scratch 0x0700. "
            "WICHTIG: Das Boot-ROM LIEST die CRC nur ein, PRUEFT sie NICHT — Validierung "
            "allein per IDAM-Feldvergleich (cyl/head/sec/size) + Boot-Signatur @01B6. "
            "Echte CRC-Verifikation (sub_0407, Dual-Konvention 0xBF84 FM / 0xE295 MFM, "
            "gewaehlt ueber [0x03FD] bit1) erfolgt erst im GELADENEN Lader (RAM 0x04xx).",
    0x025A: "FM/MFM-Pfadwahl pro Schleifendurchlauf: liest [0x03FD]; bit1=1 FM(NZ)/bit1=0 "
            "MFM(Z). ACHTUNG: bit7=0 erzeugt am OUT(10h) zusaetzlich einen Step-Puls.",
    0x00DD: "DATENRATE: Tor B (Port 0x12) = 0x7F -> bit2=/HF=1 = NIEDRIGE Frequenz. Laut "
            "K5122-Doku deckt /HF=1 sowohl FM-8\"-Laufwerke (MF3200/6400) ALS AUCH MFM-5,25\" "
            "(MFS) ab -> derselbe Takt fuer beide; nur die Markenart (MK, FM/MFM) wird per "
            "Trial-and-Error variiert. /HF wird hier FEST gesetzt, NICHT mitgetoggelt "
            "(MFM-8\", das /HF=0/hohe Frequenz braeuchte, liefe damit NICHT).",
    0x015C: "ERWARTETES IDAM 1. Sektor: [03F3]=Zyl=0, [03F4]=Kopf=0, [03F5]=Sektor-ID=1 "
            "(1-BASIERT, nicht 0!), [03F6]=Size-Code=0 (=128B). ZVE2 vergleicht alle vier "
            "Felder gegen das gelesene IDAM (FE cyl head sec size); Folgesektoren via INC L "
            "@0259 bis Sektor-ID==[07F2]=4 (Sektoren 1..4 auf Zyl0/Kopf0).",
    0x016D: "FM/MFM-UMSCHALTUNG, WANN: [0x03F8]=1 = Read-Timeout (ZVE2 fand in 4 Indexpulsen "
            "kein gueltiges IDAM in der aktuellen FM-oder-MFM-Einstellung) -> 014B DEC E (init 6); "
            "E!=0 -> 014E XOR 02h (FM<->MFM kippen) + Re-Read; E=0 -> BOOT_FAIL (naechstes "
            "Laufwerk via [03FC]-Rotation). => Die Aufzeichnungsart wird NICHT am A1-Header "
            "erkannt, sondern per Trial-and-Error durchprobiert.",
}


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
        ";",
        "; CPU-Zuordnung (jede Code-Zeile traegt [ZVE1]/[ZVE2] im Kommentar;",
        "; empirisch via boot_trace --coverage cpu,pc + Kontrollfluss bestaetigt):",
        ";   ZVE2 (DMA-CPU):  0x01DD-0x027D  = die EINE STROBE_LOOP-Sektorlade-Routine",
        ";        (IDAM-Suche, INI/INIR-Transfer, DONE [03F8]=3, Mehrsektor-Reentry),",
        ";        betreten nur ueber RAM[0000]=C3 DD 01 = JP 01DD.",
        ";   ZVE1 (Haupt-CPU): 0x0000-0x01DC + 0x027E-0x03FF = alles andere",
        ";        (Reset/Init, Seek/Retry, Drive-Init das ZVE2 startet @0194,",
        ";        IM2-Index-ISR @01C7, Signatur-Check @01B6, Alt-Boot @027E, RAM-Vars).",
        "; IM2-ISRs (auf ZVE1): Vektor 0xB8 -> 007A (BS-PIO),  0xBA -> 01C7 (Index-Puls)",
        ";",
        "; FM/MFM:  [0x03FD] bit1 = FM(1)/MFM(0)-Modus des K5122-Datenseparators.",
        ";          Default 0x87=FM @0137; Retry toggelt via XOR 02h @0151 zu 0x85=MFM;",
        ";          @01AD -> Port 0x10; ZVE2 testet BIT 1 @022B u. @025A. bit7=Step-Unterdr.",
        "; CRC:     Das Boot-ROM PRUEFT KEINE CRC. ZVE2 validiert nur das IDAM-Feld",
        ";          (cyl/head/sec/size) + Boot-Signatur @01B6; CRC-Bytes @0245/0247 werden",
        ";          nur gelesen. Echte CRC-Pruefung (sub_0407, Dual-Seed 0xBF84 FM/0xE295",
        ";          MFM via [0x03FD] bit1) liegt im GELADENEN Lader (RAM 0x04xx), nicht hier.",
        "; RAM-Vars:  03F0 Ladeadr | 03F3 Zyl | 03F5 Kopf/Sektor | 03F7 Index-Zaehler",
        ";            03F8 Done-Flag(0/1/3) | 03FA Retry | 03FC Port10-Ctrl | 03FD FM/MFM-Pfad",
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
        # jede Code-Zeile mit ausfuehrender CPU taggen + ggf. Anreicherung anhaengen
        tag = f"[{cpu_of(addr)}]"
        full = comment
        if addr in EXTRA:
            full = (full + " || " if full else "") + EXTRA[addr]
        body += f"\t\t;{tag}" + (f" {full}" if full else "")
        lines.append(f"{addr:04X}  {hexb:<14}" + body)
    lines += ["", "\tEND"]
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"geschrieben: {os.path.relpath(OUT, ROOT)}  ({len(entries)} Code-Zeilen)")


if __name__ == "__main__":
    main()
