#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die CP/A-Systemdiskette des PC 1715 (mit mehreren BIOS-Fassungen) bauen
====================================================================================

  disks/pc1715_cpa1715_system.hfe

DIE CP/A-Diskette der Auslieferung fuer den PC 1715 (Gegenstueck zu tools/cpa_a5120/).  CP/A 1715
hat keine Systemspuren: der Bootkopf (128 B, `bootabbild.bin`; zaehlen nur Platz 0 und der F0-Platz ab 0x60, Platz 1/2 sind mit E5 geleert) steht im Verzeichnis, und der
Lader liest `@OS.COM` als ERSTE Datei.  Daneben liegen weitere BIOS-Fassungen unter eigenem Namen,
die man vom A>-Prompt startet (Versuch 2026-10-09: voller BIOS-Neustart, auch aus der 4-LW-Fassung):

  @OS.COM    24.05.88, 4 LW, mit BIOS-Monitor         (die bisherige Bootdiskette, getestet)
  OS2LWUHR   24.05.88, 2 LW, ohne Monitor, mit Uhr    (Workbench-Fassung, tests/fixtures/…_workbench)
  OS0189     03.01.89, 3 LW, mit BIOS-Monitor         (neueste Fassung, Gotek-Abzug CPA_PC1715)

Inhalt: varianten/ (BIOS), inhalt/ (Programme; Herkunft: README.md), WM.HLP deutsch aus
../cpa_a5120/quellen/WM_HLP_de.txt (EINE Quelle fuer beide Disketten).  Die BIOS-Quelltexte der
frueheren Bootdiskette stehen nicht mehr darauf (Fixture tests/fixtures/disks/pc1715_cpa1715_boot_4lw.hfe).

Aufruf:
  python3 tools/cpa_pc1715/build.py --tool build/k1520disktool            # bauen
  python3 tools/cpa_pc1715/build.py --tool build/k1520disktool --check    # nur vergleichen

``--check`` ist der Waechter ``cli_cpa_pc1715``.
"""

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))

# Hilfsfunktionen (CP/M-Text, WM.HLP) und der Quellpfad der Hilfe kommen aus dem A5120-Bau.
_spec = importlib.util.spec_from_file_location('cpa_a5120_build', os.path.join(REPO, 'tools', 'cpa_a5120', 'build.py'))
a5120 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(a5120)

NAME = 'pc1715_cpa1715_system.hfe'
BOOT = os.path.join(HIER, 'bootabbild.bin')
LIESMICH = 'LIESMICH.TXT'


def _lauf(tool, *args):
    r = subprocess.run([tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _lies(*teile):
    with open(os.path.join(*teile), 'rb') as f:
        return f.read()


def soll():
    """{Diskettenname: Bytes}."""
    inhalt = {}
    for ordner in ('varianten', 'inhalt'):
        for n in sorted(os.listdir(os.path.join(HIER, ordner))):
            inhalt[n] = _lies(HIER, ordner, n)
    inhalt[LIESMICH] = a5120._cpm_text(inhalt[LIESMICH])
    inhalt['WM.HLP'] = a5120._wm_hlp()
    return inhalt


def bauen(tool, ausgabe):
    ziel = os.path.join(ausgabe, NAME)
    if os.path.exists(ziel):
        os.remove(ziel)
    with tempfile.TemporaryDirectory() as tmp:
        quelle = os.path.join(tmp, 'q')
        os.makedirs(quelle)
        for n, d in soll().items():
            with open(os.path.join(quelle, n), 'wb') as f:
                f.write(d)
        os_com = os.path.join(quelle, '@OS.COM')
        _lauf(tool, 'create', ziel, '--fs', 'cpa1715', '--boot', BOOT)
        _lauf(tool, 'put', ziel, os_com, '--no-backup')          # ERSTE Datei
        os.remove(os_com)
        _lauf(tool, 'put', ziel, quelle, '--no-backup')
    pruefung = _lauf(tool, 'check', ziel, '--full')
    if 'ohne Befund' not in pruefung:
        sys.exit(f'{NAME}: Dateisystempruefung hat Befunde:\n{pruefung}')
    print(f'{NAME}: gebaut, check --full ohne Befund')


def pruefen(tool, ausgabe):
    diskette = os.path.join(ausgabe, NAME)
    if not os.path.isfile(diskette):
        print(f'{NAME}: fehlt')
        return False
    erwartet = soll()
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        ist_ordner = os.path.join(tmp, 'ist')
        os.makedirs(ist_ordner)
        _lauf(tool, 'get', diskette, '*', '--to', ist_ordner)
        ist = {n: _lies(ist_ordner, n) for n in os.listdir(ist_ordner)
               if n != 'cpm-dateiangaben.txt'}
        boot = os.path.join(tmp, 'boot.bin')
        _lauf(tool, 'boot-get', diskette, boot)
        # Nur Platz 0 (Kopf) und der F0-Platz (Parametersaetze) zaehlen; Platz 1/2 sind Dateieintraege
        ist_boot, soll_boot = _lies(boot), _lies(BOOT)
        boot_ok = ist_boot[:0x20] == soll_boot[:0x20] and ist_boot[0x60:0x80] == soll_boot[0x60:0x80]
    for n in sorted(set(erwartet) | set(ist)):
        if n not in ist:
            print(f'{NAME}: {n} fehlt auf der Diskette'); ok = False
        elif n not in erwartet:
            print(f'{NAME}: {n} steht auf der Diskette, aber nicht im Ordner'); ok = False
        elif erwartet[n] != ist[n]:
            print(f'{NAME}: {n} weicht ab'); ok = False
    if not boot_ok:
        print(f'{NAME}: Bootkopf weicht ab'); ok = False
    if ok:
        print(f'{NAME}: ok ({len(erwartet)} Dateien)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = os.path.abspath(o.tool)
    if o.check:
        sys.exit(0 if pruefen(tool, o.out) else 1)
    bauen(tool, o.out)


if __name__ == '__main__':
    main()
