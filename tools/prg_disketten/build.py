#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die beiden UDOS-Lieferdisketten der PRG 710 / 710-1 aus EINEM Ordner bauen
======================================================================================

  disks/prg710_udos43_k5601_system.hfe        PRG 710
  disks/prg710-1_udos43_k5601_v43_189.hfe     PRG 710-1

Beide tragen dasselbe UDOS 4.3 und dieselben Programme; verschieden sind nur

  * die Systemspuren (Tastatur-/MKE-Treiber des Geraets)  -> variante/<geraet>/bootabbild.bin
  * die Datei OS (nur die Kennung `UDOS PRG710` / `UDOS PG710-1`) -> variante/<geraet>/Side0/OS

Alles andere liegt EINMAL in `inhalt/Side0` und `inhalt/Side1` (mit `.fileinfo`, das die
Angaben traegt, die eine Linux-Datei nicht kennt).  Dadurch koennen die Disketten inhaltlich
nicht mehr auseinanderlaufen.  Der Inhalt und die Auswahl: doc/udos_programme.md.

Aufruf:
  python3 tools/prg_disketten/build.py --tool build/k1520disktool            # bauen
  python3 tools/prg_disketten/build.py --tool build/k1520disktool --check    # nur vergleichen

``--check`` ist der Waechter ``cli_prg_disketten``: Exit 1, wenn eine Diskette in disks/ nicht
genau den Inhalt hat, den dieser Ordner beschreibt (Dateiliste, Seite, Inhalt, Systemspuren).
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))

# Geraet -> (Diskette in disks/, Datentraegername)
DISKETTEN = {
    '710':   ('prg710_udos43_k5601_system.hfe',    'UDOS.PRG710'),
    '710-1': ('prg710-1_udos43_k5601_v43_189.hfe', 'UDOS.PRG710-1'),
}


def _lauf(tool, *args):
    r = subprocess.run([*tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool[-1]), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _sammeln(geraet, ziel):
    """Gemeinsamen Inhalt + OS dieses Geraets in einen Ordner mit Side0/Side1 legen."""
    for quelle in (os.path.join(HIER, 'inhalt'), os.path.join(HIER, 'variante', geraet)):
        for seite in ('Side0', 'Side1'):
            d = os.path.join(quelle, seite)
            if not os.path.isdir(d):
                continue
            os.makedirs(os.path.join(ziel, seite), exist_ok=True)
            for n in os.listdir(d):
                shutil.copy2(os.path.join(d, n), os.path.join(ziel, seite, n))


def _inhalt_von_ordner(ordner):
    """{Seite/Name: md5} ohne die .fileinfo."""
    erg = {}
    for seite in ('Side0', 'Side1'):
        d = os.path.join(ordner, seite)
        if not os.path.isdir(d):
            continue
        for n in os.listdir(d):
            if n.endswith('.fileinfo') or n == 'DIRECTORY':
                continue
            erg[f'{seite}/{n}'] = hashlib.md5(open(os.path.join(d, n), 'rb').read()).hexdigest()
    return erg


def bauen(tool, geraet, ausgabe):
    name, label = DISKETTEN[geraet]
    ziel = os.path.join(ausgabe, name)
    if os.path.exists(ziel):
        os.remove(ziel)
    with tempfile.TemporaryDirectory() as tmp:
        quelle = os.path.join(tmp, 'q')
        _sammeln(geraet, quelle)
        _lauf(tool, 'create', ziel, '--fs', 'udos_ds77', '--label', label,
              '--boot', os.path.join(HIER, 'variante', geraet, 'bootabbild.bin'),
              '--prg', geraet)
        _lauf(tool, 'put', ziel, quelle, '--no-backup')
    pruefung = _lauf(tool, 'check', ziel, '--full')
    if 'ohne Befund' not in pruefung:
        sys.exit(f'{name}: Dateisystempruefung hat Befunde:\n{pruefung}')
    print(f'{name}: gebaut, check --full ohne Befund')


def pruefen(tool, geraet, ausgabe):
    name, _ = DISKETTEN[geraet]
    diskette = os.path.join(ausgabe, name)
    if not os.path.isfile(diskette):
        print(f'{name}: fehlt')
        return False
    with tempfile.TemporaryDirectory() as tmp:
        soll_ordner = os.path.join(tmp, 'soll')
        ist_ordner = os.path.join(tmp, 'ist')
        _sammeln(geraet, soll_ordner)
        os.makedirs(ist_ordner)
        _lauf(tool, 'get', diskette, '*', '--to', ist_ordner)
        soll, ist = _inhalt_von_ordner(soll_ordner), _inhalt_von_ordner(ist_ordner)
        # Systemspuren
        boot = os.path.join(tmp, 'boot.bin')
        _lauf(tool, 'boot-get', diskette, boot)
        boot_ok = (open(boot, 'rb').read()
                   == open(os.path.join(HIER, 'variante', geraet, 'bootabbild.bin'), 'rb').read())
    ok = True
    for n in sorted(set(soll) | set(ist)):
        if n not in ist:
            print(f'{name}: {n} fehlt auf der Diskette'); ok = False
        elif n not in soll:
            print(f'{name}: {n} steht auf der Diskette, aber nicht im Ordner'); ok = False
        elif soll[n] != ist[n]:
            print(f'{name}: {n} weicht ab'); ok = False
    if not boot_ok:
        print(f'{name}: Systemspuren weichen ab'); ok = False
    if ok:
        print(f'{name}: ok ({len(soll)} Dateien)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--emulator', action='append', default=[],
                    help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = [*o.emulator, os.path.abspath(o.tool)]
    if o.check:
        ok = all([pruefen(tool, g, o.out) for g in DISKETTEN])
        sys.exit(0 if ok else 1)
    for g in DISKETTEN:
        bauen(tool, g, o.out)


if __name__ == '__main__':
    main()
