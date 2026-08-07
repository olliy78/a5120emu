#!/usr/bin/env python3
"""gen_scpx_readpath_prn.py — kommentiertes .prn des SCPX-1526-Lesepfads erzeugen.

Erzeugt ein via `k1520dbg -l` / `boot_trace -l` ladbares MACRO-80-kompatibles Listing
(Format wie doc/EPROMS/zre.prn) der SCPX-Laufzeit-Lese-Koroutine + IDAM-Matcher, damit
Disasm/Trace die Original-Semantik inline (`; …`) zeigen. Analog gen_zre_prn.py.

Quelle der Bytes: ein SCPX-RAM-Dump. So gewinnen (BIOS liegt erst nach dem Boot im RAM):

    D=$(mktemp --suffix=.hfe); cp disks/scpx17_cpa780_k5601.hfe "$D"
    printf 'b 0xE079\\ng\\nbd 0xE079\\nsavestate /tmp/scpx.state\\nq\\n' | ./build/k1520dbg --rw "$D"
    dd if=/tmp/scpx.state bs=1 skip=12 count=65536 of=/tmp/scpx_ram64.bin ; rm -f "$D"
    python3 tools/gen_scpx_readpath_prn.py /tmp/scpx_ram64.bin doc/EPROMS/scpx_readpath.prn

Adressen/Semantik: doc/analyse_scpx_com_load.md §3/§5/§7, tools/scpx1526.sym.
Der Disassembler (tools/z80_disasm2.py) wird als Subprozess je Region aufgerufen.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Regionen des Lesepfads: (Startadresse für den Disassembler, lo, hi einschließlich).
REGIONS = [
    (0xE9C8, 0xE9C8, 0xEA2A),   # Lese-Koroutine-Setup + IDAM-Matcher (Herz)
    (0xEA9A, 0xEA9A, 0xEAB0),   # DATA-Feld-Lesung
    (0xE6E2, 0xE6E2, 0xE742),   # Seek / Positionierung
    (0xE88C, 0xE88C, 0xE8C0),   # Read-Setup ([0001]-Poise/Restore, Poll-Wait E8B5)
    (0xD098, 0xD098, 0xD0B0),   # BAD-SECTOR-Handler
]

# Kommentare je Adresse (deutsch, wie im Repo üblich). Fehlt eine Adresse, bleibt die
# Zeile unkommentiert (nur Disasm). Labels kommen aus tools/scpx1526.sym (via -s).
COMMENTS = {
    0xE9C8: "[ZVE1/ZVE2 Lese-Koroutine, an [0000]=JP E9C8 gepoist] Selbstmod-Setup: HL=20B9",
    0xE9CB: "self-modifying: schreibt nach E9FD (Matcher-Rumpf wird gepatcht)",
    0xE9CE: "HL = Laufwerks-Deskriptor (aus [EBFE]) — liefert u.a. Spurregister [EBFA]",
    0xE9D1: "LD SP,EC0D → Mini-Stack auf den Template-Block (SCHICHT-2-KORRUPTIONSZIEL: "
            "ein CTC-INT pusht hierhin die Matcher-PC, s. analyse §5)",
    0xE9D4: "POP DE: DE = [EC0D/EC0E] = IDAM-CRC-Sollwert (16-Bit)",
    0xE9D5: "EXX → CRC-Soll ins Schattenregister D'/E' (Vergleich später @EA12/EA17)",
    0xE9D6: "HL = [EC03/EC04] = Soll Sektor-ID / Größe-Code",
    0xE9D9: "BC = [EC01/EC02] = Soll Zylinder / Kopf",
    0xE9DD: "DE = FE / A1: FE = IDAM-Adressmarke, A1 = MFM-Sync (Sollbytes)",
    0xE9E0: "K5122-Steuerwort B9 → Port 10 (Lesekopf/Verfahren scharf)",
    0xE9E6: "MATCHER-Schleifenkopf: LD A,B5 (0x85 = MFM-Lesemarke) → Port 10",
    0xE9EA: "CRC-Steuerwort [EC05] → Port 10",
    0xE9EF: "IN (16H): nächstes Stream-Byte vom K5122-Datenseparator lesen",
    0xE9F1: "OUT (14H): Byte quittieren/durchreichen",
    0xE9F3: "IN (16H) / CP E: auf A1-Sync (E=A1) prüfen …",
    0xE9F6: "… JR Z: A1-Sync-Bytes überspringen (Schleife bis != A1)",
    0xE9F8: "CP D: auf FE (IDAM-Adressmarke, D=FE) prüfen",
    0xE9FB: "JR NZ,E9E6: keine Marke → Matcher weiter (Byte-für-Byte-Suche)",
    0xE9FD: "CP C: ZYLINDER-Vergleich (Soll C=[EC01]) — SCHICHT-1-Mismatch schlägt HIER zu "
            "(Kopf divergiert → cyl passt nicht → 43× Retry → BAD SECTOR)",
    0xE9FE: "JR NZ,E9E6: Zylinder falsch → nächste Marke suchen",
    0xEA02: "CP B: KOPF-Vergleich (Soll B=[EC02])",
    0xEA07: "CP L: SEKTOR-Vergleich (Soll L=[EC03])",
    0xEA0C: "CP H: GRÖSSE-Vergleich (Soll H=[EC04])",
    0xEA11: "EXX: auf CRC-Schattenregister (D'/E' = IDAM-CRC-Soll) umschalten",
    0xEA12: "CP D': IDAM-CRC-Hi gegen [EC0D] — SCHICHT-2-Korruption trifft HIER "
            "(Stream-CRC korrekt, D' vom INT überschrieben → Fail)",
    0xEA15: "JR NZ,E9E5: CRC-Hi falsch → Matcher-Neustart",
    0xEA17: "CP E': IDAM-CRC-Lo gegen [EC0E]",
    0xEA1A: "JR NZ,E9E5: CRC-Lo falsch → Matcher-Neustart",
    0xEA9A: "DATA-Feld-Lesung (nach erfolgreichem IDAM-Match)",
    0xE6E2: "SEEK: Positionierung; schreibt Spurregister [EBFA] @E706, /TO-Reset @E701",
    0xE701: "/TO-Reset (Timeout-Flip-Flop)",
    0xE706: "Spurregister [EBFA] = Zielzylinder (per Laufwerk aus IX+2/+3)",
    0xE88C: "READ-SETUP: poist die Lese-Koroutine nach [0001] (@E858) / restauriert (@E8A0)",
    0xE8B5: "POLL-WAIT: ZVE1 wartet auf [EC0B] (ZVE2-Signal) — GENERISCH, nicht read-eindeutig",
    0xD098: "BAD-SECTOR-Handler (LD IX,D0CA; CALL D0E5; …): Meldung 'BAD SECTOR'",
}

LINE_RE = re.compile(r"^([0-9A-Fa-f]{4})\s+((?:[0-9A-Fa-f]{2} )+)\s*\t?(.*)$")


def disasm_region(rambin: str, entry: int):
    """z80_disasm2 ab `entry` laufen lassen → dict addr -> (bytes_str, mnemonic)."""
    out = subprocess.run(
        [sys.executable, str(HERE / "z80_disasm2.py"), "--org", "0",
         "--entry", hex(entry), rambin],
        capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        by = m.group(2).strip()
        mnem = m.group(3).strip()
        res.setdefault(addr, (by, mnem))
    return res


def main():
    rambin = sys.argv[1] if len(sys.argv) > 1 else "/tmp/scpx_ram64.bin"
    outpath = sys.argv[2] if len(sys.argv) > 2 else str(
        HERE.parent / "doc" / "EPROMS" / "scpx_readpath.prn")

    lines = [
        "; ============================================================================",
        "; scpx_readpath.prn  -  SCPX 1526 V1.7 Laufzeit-Lesepfad (BIOS im RAM)",
        ";",
        "; Kommentiertes Listing fuer k1520dbg / boot_trace (-l). Erzeugt von",
        "; tools/gen_scpx_readpath_prn.py aus einem SCPX-RAM-Dump (Boot bis A>, savestate).",
        "; Semantik: doc/analyse_scpx_com_load.md 3/5/7 + tools/scpx1526.sym.",
        ";",
        "; Herz: Lese-Koroutine E9C8 (an [0000] gepoist) + IDAM-Matcher E9E6-EA1A.",
        ";   Matcher-Felder: cyl E9FD / head EA02 / sec EA07 / size EA0C / CRC EA12+EA17.",
        ";   Fail-Ruecksprung EA15/EA1A. Der Read laeuft auf ZVE2 (b2, nicht b!).",
        "; ============================================================================",
        "",
    ]

    total = 0
    commented = 0
    for entry, lo, hi in REGIONS:
        # Jede Region mit ihrem EIGENEN Entry disassemblieren → korrekte Ausrichtung
        # (ein globaler Merge würde den Rumpf durch fehlausgerichtete Nachbar-Läufe zerstören).
        decoded = disasm_region(rambin, entry)
        lines.append(f";  ---- {lo:04X}..{hi:04X} ----")
        addr = lo
        while addr <= hi:
            ent = decoded.get(addr)
            if ent is None:              # nicht dekodiert (Datenbyte/Mitte einer Instr)
                addr += 1
                continue
            by, mnem = ent
            comment = COMMENTS.get(addr, "")
            src = mnem
            if comment:
                src = f"{mnem}\t\t;{comment}"
                commented += 1
            lines.append(f"{addr:04X}  {by:<12}\t{src}")
            total += 1
            addr += 1 + by.count(" ")     # Anzahl Bytes = Anzahl Hex-Paare
        lines.append("")

    Path(outpath).write_text("\n".join(lines) + "\n")
    print(f"geschrieben: {outpath}  ({total} Code-Zeilen, {commented} kommentiert)")


if __name__ == "__main__":
    main()
