#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
disketten_beigaben.py  -  eigene CP/M-Programme auf die Bootdisketten in disks/ bringen
=====================================================================================

Die Arbeitsdisketten in ``disks/`` (und damit die Beispieldisketten des Pakets,
``packaging/build_payload.sh``) tragen die eigenen Programme, damit sie am
Emulator wie am Geraet ohne eigenen Kopierschritt bereitstehen:

  SERTEST.COM   tools/sertest/   A5120 (CP/A) und K8915 (SCPX 8915)
  ROMREAD.COM   tools/romread/   A5120 — liest das Boot-EPROM der ZRE (BS-PIO 0AH)
  EM256ADR.COM  tools/em256/     A5120.16 — Pruefprogramme der EM064/EM256
  EM16ABL.COM   tools/em256/
  EM256FUL.COM  tools/em256/

Nach jedem Neubau einer .com muessen diese Kopien nachgezogen werden — sonst
liefert das Paket eine alte Fassung aus.  Ebenso die Prueflinge der Tests unter
``tests/fixtures/cpm/`` (EM*.COM), die bytegleich bleiben muessen.

Aufruf:
  python3 tools/disketten_beigaben.py --tool <k1520disktool>            # aufspielen
  python3 tools/disketten_beigaben.py --tool <k1520disktool> --check    # nur vergleichen

``--check`` ist der Waechter ``cli_beigaben_auf_den_disketten``: Exit 1, wenn eine
Diskette ein Programm nicht oder in anderer Fassung traegt oder ein Pruefling abweicht.
"""

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
FIXTURES = os.path.join(REPO, 'tests', 'fixtures', 'cpm')

# Name auf der Diskette (8.3, CP/A erwartet Grossbuchstaben) -> eingecheckte .com
PROGRAMME = {
    'SERTEST.COM':  os.path.join(TOOLS, 'sertest', 'sertest.com'),
    'ROMREAD.COM':  os.path.join(TOOLS, 'romread', 'romread.com'),
    'EM256ADR.COM': os.path.join(TOOLS, 'em256', 'em256adr.com'),
    'EM16ABL.COM':  os.path.join(TOOLS, 'em256', 'em16abl.com'),
    'EM256FUL.COM': os.path.join(TOOLS, 'em256', 'em256ful.com'),
}
A5120 = list(PROGRAMME)
K8915 = ['SERTEST.COM']      # ROMREAD/EM* sprechen A5120-Hardware an

# Bootdisketten und was sie tragen.  SCPX 1526 am A5120 ist nicht geprueft und
# fehlt deshalb.
DISKETTEN = {
    'cpa_cpa780_k5601_noclock.hfe': A5120,
    'cpa_cpa780_k5601_noclock.img': A5120,
    'cpa_cpa780_k5601_clock.hfe': A5120,
    'cpa_cpa780_k5601_clock.img': A5120,
    'cpa_cpa780_combo5zoll_noclock.hfe': A5120,
    'cpa_cpa780_combo5zoll_noclock.img': A5120,
    'cpa_cpa780_combo8zoll_noclock.hfe': A5120,
    'cpa_cpa780_combo8zoll_noclock.img': A5120,
    'k8915scpx_boot1.hfe': K8915,
}

# Prueflinge der Tests, die dieselbe Fassung tragen muessen.
KOPIEN = {
    os.path.join(FIXTURES, 'em256adr.com'): PROGRAMME['EM256ADR.COM'],
    os.path.join(FIXTURES, 'em16abl.com'): PROGRAMME['EM16ABL.COM'],
    os.path.join(FIXTURES, 'em256ful.com'): PROGRAMME['EM256FUL.COM'],
}


def tool(disktool, *args):
    # disktool = Befehlszeile als Liste (Emulator wie `wine` + Programm)
    return subprocess.run([*disktool, *args], capture_output=True, text=True,
                          errors='replace')


def lies(disktool, abbild, name, tmp):
    """Datei @p name von der Diskette holen; None, wenn sie keine traegt."""
    ziel = os.path.join(tmp, 'get', os.path.basename(abbild))
    shutil.rmtree(ziel, ignore_errors=True)   # sonst faende man die alte Fassung
    tool(disktool, 'get', abbild, name, '--to', ziel)
    for wurzel, _, dateien in os.walk(ziel):
        for d in dateien:
            if d.upper() == name:
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
    ap.add_argument('--nur', nargs='+', metavar='NAME', default=list(DISKETTEN),
                    help='nur diese Disketten (Vorgabe: alle aus DISKETTEN)')
    a = ap.parse_args()
    a.tool = [*a.emulator, a.tool]

    fehler = 0
    for kopie, quelle in KOPIEN.items():
        name = os.path.relpath(kopie, REPO)
        if os.path.isfile(kopie) and filecmp.cmp(kopie, quelle, shallow=False):
            continue
        if a.check:
            print(f'ALT     {name}')
            fehler += 1
        else:
            shutil.copyfile(quelle, kopie)
            print(f'NEU     {name}')

    with tempfile.TemporaryDirectory() as tmp:
        for diskette in a.nur:
            abbild = os.path.join(a.ordner, diskette)
            if not os.path.isfile(abbild):
                print(f'FEHLT   {diskette}')
                fehler += 1
                continue
            for name in DISKETTEN.get(diskette, A5120):
                with open(PROGRAMME[name], 'rb') as f:
                    soll = f.read()
                ist = lies(a.tool, abbild, name, tmp)
                if ist == soll:
                    print(f'aktuell {diskette}: {name}')
                    continue
                if a.check:
                    print(f'{"ALT    " if ist else "OHNE   "} {diskette}: {name}')
                    fehler += 1
                    continue
                # put ueberschreibt nicht; eine alte Fassung erst loeschen.
                if ist is not None:
                    tool(a.tool, 'rm', '--no-backup', abbild, name)
                # Unter dem 8.3-Namen aufspielen, den CP/A erwartet.
                quelle = os.path.join(tmp, name)
                shutil.copyfile(PROGRAMME[name], quelle)
                r = tool(a.tool, 'put', '--no-backup', '--binary', abbild, quelle)
                if r.returncode != 0 or lies(a.tool, abbild, name, tmp) != soll:
                    print(f'FEHLER  {diskette}: {name}: {r.stdout.strip()} {r.stderr.strip()}')
                    fehler += 1
                else:
                    print(f'NEU     {diskette}: {name}')
    if fehler and a.check:
        print('Nachziehen mit: python3 tools/disketten_beigaben.py --tool <k1520disktool>')
    return 1 if fehler else 0


if __name__ == '__main__':
    sys.exit(main())
