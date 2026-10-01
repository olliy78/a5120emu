#!/usr/bin/env python3
"""
Stellt nach, was der Diskettenlader des K8915-Boot-ROMs (0406H..08D7H, s.
doc/EPROMS/K8915/k8915_zre.prn) aus den Systemspuren einer SCPX-8915-Diskette
in den Speicher laedt -- ohne Emulator, direkt aus dem Abbild (.hfe/.dmk/.img).

Aufbau, wie ihn das ROM liest (doc/design/16_k8915.md §4.3a):
  Sektor 1 von c0h0 beginnt mit einem 16-Byte-Kopf, den der Lader nach F700H holt
  und per CRC-CCITT (Start FFFFH, ueber alle 16 Byte inkl. der CRC) auf 0 prueft:
    +0  Ladeadresse           +2  Einsprung (JP (HL) aus F702H)
    +4  je Zylinder 0..2 die Zahl der zu ladenden Sektoren, Diskette 0
    +7  dasselbe fuer eine Folgediskette (F710H = 1; das ROM ruft nur mit 0)
    +10 ungleich 0 => "Loading complete, replace system disk"
    +11 je Zylinder 0..2 die Sektoren auf Kopf 0 (danach geht es auf Kopf 1 weiter)
    +14 CRC
  Geladen wird Sektor 1 ohne den Kopf, danach jeder weitere Sektor ganz.

Aufruf:  python3 tools/k8915_sysload.py <abbild> [--out speicher.bin]
  --out schreibt ein 64-KB-Speicherbild (nur der geladene Bereich belegt) -- die
  Vorlage fuer den Vergleich nach dem Laden im Emulator (Etappe 3).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app"))
from core_binding.k1520disk import DiskTool  # noqa: E402


def crc_ccitt(data, crc=0xFFFF):
    # dieselbe Rechnung wie ROM 08E0H (D = hoeherwertiges Byte)
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def sektor(disk, cyl, head, sec):
    for sp in disk.track(cyl, head).spans:
        if sp.id == sec and sp.size:
            if not sp.data_crc_ok:
                sys.exit(f"c{cyl}h{head} Sektor {sec}: Daten-CRC falsch (ROM: 'Disk-error')")
            if sp.cyl != cyl or sp.head != head:
                sys.exit(f"c{cyl}h{head} Sektor {sec}: ID traegt c{sp.cyl}h{sp.head} (ROM vergleicht beide)")
            return disk.sector_data(cyl, head, sp.index)
    sys.exit(f"c{cyl}h{head}: Sektor {sec} fehlt")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("abbild")
    ap.add_argument("--out")
    a = ap.parse_args()

    disk = DiskTool.open_raw(a.abbild)
    kopf = sektor(disk, 0, 0, 1)[:16]
    if crc_ccitt(kopf) != 0:
        sys.exit(f"Kopf {kopf.hex(' ')}: CRC falsch (ROM: 'No system disk')")
    lade = kopf[0] | kopf[1] << 8
    start = kopf[2] | kopf[3] << 8
    anzahl, seite0 = kopf[4:7], kopf[11:14]
    print(f"Kopf       {kopf.hex(' ')}")
    print(f"Laden ab   {lade:04X}H, Einsprung {start:04X}H")
    print(f"Sektoren   je Zylinder {list(anzahl)}, davon auf Kopf 0 {list(seite0)}, "
          f"Folgediskette {list(kopf[7:10])}, Wechselmeldung {kopf[10]}")

    speicher = bytearray()
    for cyl in range(3):
        for i in range(anzahl[cyl]):
            head, sec = (0, i + 1) if not seite0[cyl] or i < seite0[cyl] else (1, i - seite0[cyl] + 1)
            daten = sektor(disk, cyl, head, sec)
            speicher += daten[16:] if cyl == 0 and i == 0 else daten
    ende = lade + len(speicher) - 1
    print(f"Geladen    {len(speicher)} Byte nach {lade:04X}H..{ende:04X}H")
    if ende > 0xF6FF:
        print("WARNUNG: ueberschreibt die Arbeitszellen des Laders (F700H..)")

    if a.out:
        bild = bytearray(0x10000)
        bild[lade:lade + len(speicher)] = speicher
        open(a.out, "wb").write(bild)


if __name__ == "__main__":
    main()
