#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
disketten.py  -  SERTEST.COM auf die Bootdisketten in disks/ bringen
====================================================================

Die Arbeitsdisketten in ``disks/`` (und damit die Beispieldisketten des Pakets,
``packaging/build_payload.sh``) tragen SERTEST.COM, damit das Testprogramm am
Emulator wie am Geraet ohne eigenen Kopierschritt bereitsteht.  Nach jedem Neubau
von ``tools/sertest/sertest.com`` muessen diese Kopien nachgezogen werden — sonst
liefert das Paket eine alte Fassung aus.

Aufruf:
  python3 tools/sertest/disketten.py --tool <k1520disktool>            # aufspielen
  python3 tools/sertest/disketten.py --tool <k1520disktool> --check    # nur vergleichen

``--check`` ist der Waechter ``cli_sertest_auf_den_disketten``: Exit 1, wenn eine
Diskette SERTEST.COM nicht oder in anderer Fassung traegt.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPT_DIR))
COM = os.path.join(SCRIPT_DIR, 'sertest.com')

# Alle Bootdisketten, unter deren System SERTEST laeuft (CP/A am A5120,
# SCPX 8915 am K8915).  SCPX 1526 am A5120 ist nicht geprueft und fehlt deshalb.
DISKETTEN = [
    'cpa_cpa780_k5601_noclock.hfe',
    'cpa_cpa780_k5601_noclock.img',
    'cpa_cpa780_k5601_clock.hfe',
    'cpa_cpa780_k5601_clock.img',
    'cpa_cpa780_combo5zoll_noclock.hfe',
    'cpa_cpa780_combo5zoll_noclock.img',
    'cpa_cpa780_combo8zoll_noclock.hfe',
    'cpa_cpa780_combo8zoll_noclock.img',
    'k8915scpx_boot1.hfe',
]


def tool(disktool, *args):
    # disktool = Befehlszeile als Liste (Emulator wie `wine` + Programm)
    return subprocess.run([*disktool, *args], capture_output=True, text=True,
                          errors='replace')


def lies(disktool, abbild, tmp):
    """SERTEST.COM von der Diskette holen; None, wenn sie keine traegt."""
    ziel = os.path.join(tmp, os.path.basename(abbild))
    shutil.rmtree(ziel, ignore_errors=True)   # sonst faende man die alte Fassung
    tool(disktool, 'get', abbild, 'SERTEST.COM', '--to', ziel)
    for wurzel, _, dateien in os.walk(ziel):
        for d in dateien:
            if d.upper() == 'SERTEST.COM':
                with open(os.path.join(wurzel, d), 'rb') as f:
                    return f.read()
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument('--tool', required=True, help='Pfad zu k1520disktool')
    ap.add_argument('--emulator', action='append', default=[],
                    help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen')
    ap.add_argument('--ordner', default=os.path.join(REPO, 'disks'),
                    help='Diskettenordner (Vorgabe: disks/)')
    ap.add_argument('--nur', nargs='+', metavar='NAME', default=DISKETTEN,
                    help='nur diese Disketten (Vorgabe: alle aus DISKETTEN)')
    a = ap.parse_args()
    a.tool = [*a.emulator, a.tool]

    with open(COM, 'rb') as f:
        soll = f.read()
    fehler = 0
    with tempfile.TemporaryDirectory() as tmp:
        # Unter dem 8.3-Namen aufspielen, den CP/A erwartet.
        quelle = os.path.join(tmp, 'SERTEST.COM')
        shutil.copyfile(COM, quelle)
        for name in a.nur:
            abbild = os.path.join(a.ordner, name)
            if not os.path.isfile(abbild):
                print(f'FEHLT   {name}')
                fehler += 1
                continue
            ist = lies(a.tool, abbild, tmp)
            if ist == soll:
                print(f'aktuell {name}')
                continue
            if a.check:
                print(f'ALT     {name}' if ist else f'OHNE    {name}')
                fehler += 1
                continue
            # put ueberschreibt nicht; eine alte Fassung erst loeschen.
            if ist is not None:
                tool(a.tool, 'rm', '--no-backup', abbild, 'SERTEST.COM')
            r = tool(a.tool, 'put', '--no-backup', '--binary', abbild, quelle)
            if r.returncode != 0 or lies(a.tool, abbild, tmp) != soll:
                print(f'FEHLER  {name}: {r.stdout.strip()} {r.stderr.strip()}')
                fehler += 1
            else:
                print(f'NEU     {name}')
    if fehler and a.check:
        print('Nachziehen mit: python3 tools/sertest/disketten.py --tool <k1520disktool>')
    return 1 if fehler else 0


if __name__ == '__main__':
    sys.exit(main())
