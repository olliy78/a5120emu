#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die UDOS-Entwicklerdiskette aus EINEM Ordner bauen
================================================================

  disks/udos_entwickler_k5601.hfe

Eine NICHT startfaehige Datendiskette (UDOS 4.3, `udos_ds77`, beidseitig) fuer das zweite
Laufwerk jeder Maschine mit ZDOS und K5601-Diskette (A5120, PRG 710/710-1, USAR).  UDOS findet
Programme auf allen aktiven Laufwerken; die Werkzeuge liegen dort also bereit, ohne die
Systemdisketten zu fuellen.

  Seite 0   Werkzeuge: Assembler (ASM, ASM2, ASM3), Binder (LINK), Editoren (EDIT, EDI mit Bildschirmtreiber ADM_O2, SEDIT),
            Debugger (SYD), BASIC, PL/Z-Compiler (PLZSYS, PLZCG, PLINK), TRANSFER, REORG, LW
  Seite 1   Systemgenerierung (SG, Quellen und Objekte, fertige POS_*-Kerne) und Dokumente

Alles liegt in `inhalt/Side0` und `inhalt/Side1` (mit `.fileinfo`, das die Angaben traegt, die
eine Linux-Datei nicht kennt).  Auswahl und Herkunft: doc/udos_programme.md §6.

Aufruf:
  python3 tools/prg_disketten/build.py --tool build/k1520disktool            # bauen
  python3 tools/prg_disketten/build.py --tool build/k1520disktool --check    # nur vergleichen

``--check`` ist der Waechter ``cli_udos_entwickler``: Exit 1, wenn eine Diskette in disks/ nicht
genau den Inhalt hat, den dieser Ordner beschreibt (Dateiliste, Seite, Inhalt).
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

# Geraet -> (Diskette in disks/, Datentraegername); hier nur EINE, ohne Geraetevariante
DISKETTEN = {
    '': ('udos_entwickler_k5601.hfe', 'UDOS.ENTWICKLER'),
}


def _lauf(tool, *args):
    r = subprocess.run([tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _sammeln(geraet, ziel):
    """Gemeinsamen Inhalt + OS dieses Geraets in einen Ordner mit Side0/Side1 legen."""
    for quelle in (os.path.join(HIER, 'inhalt'),):
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
        _lauf(tool, 'create', ziel, '--fs', 'udos_ds77', '--label', label)
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
    ok = True
    for n in sorted(set(soll) | set(ist)):
        if n not in ist:
            print(f'{name}: {n} fehlt auf der Diskette'); ok = False
        elif n not in soll:
            print(f'{name}: {n} steht auf der Diskette, aber nicht im Ordner'); ok = False
        elif soll[n] != ist[n]:
            print(f'{name}: {n} weicht ab'); ok = False
    if ok:
        print(f'{name}: ok ({len(soll)} Dateien)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = os.path.abspath(o.tool)
    if o.check:
        ok = all([pruefen(tool, g, o.out) for g in DISKETTEN])
        sys.exit(0 if ok else 1)
    for g in DISKETTEN:
        bauen(tool, g, o.out)


if __name__ == '__main__':
    main()
