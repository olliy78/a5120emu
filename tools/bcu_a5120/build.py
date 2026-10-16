#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  BCU-880-Disketten und Beispielbaender des fruehen A5120 in disks/ legen
====================================================================================

  disks/a5120_bcu/a5120_bcu12_k5600-10_start-syl.hfe     Startdiskette „SYL auf Spur 1“ (Satz 2, V1.2)
  disks/a5120_bcu/a5120_bcu12_k5600-10_arbeit-sys1.hfe   Arbeitsdiskette „ohne SYL“ (SYS1, V1.2, KMBG.REL)
  disks/a5120_bcu/bcu_kassette_formatiert.k52            Kassette, von BCU-INIT formatiert (s. u.)
  disks/a5120_bandtest/bandtest_boot.k52                 Demo-Bootband (Eigenbau, tools/kassette/bandtest/)

Die beiden Disketten sind die Belege doc/a5120_alt/bcu/satz2_SYL_auf_Spur1/disk.img und
satz2_OhneSYL.1/disk.img (NIE aendern), mit dem DiskTool unveraendert nach .hfe gewandelt
(`save-as`, Format k5601_ss40_26x128 erkannt; die SYL-Diskette traegt kein Dateisystem, daher
`--fs cpa_auto` nur fuer die Wandlung).  Inhalt sektorgleich (Rueckwandlung nach .img = Beleg).

Die formatierte Kassette entsteht im Lauf von BCU 880 selbst (Test BcuKassette.FormatiertSchreibtLiest,
`B:INIT` an Geraet 2 = F:) und wird dort mit dem Inhalt in disks/ verglichen (Bloecke, nicht die
Luecken).  Neu erzeugen:

  K1520_BEISPIELBAND=$PWD/disks/a5120_bcu/bcu_kassette_formatiert.k52 \\
      tools/dev.sh test-format -R BcuKassette.FormatiertSchreibtLiest

Aufruf:
  python3 tools/bcu_a5120/build.py --tool build/k1520disktool            # bauen
  python3 tools/bcu_a5120/build.py --tool build/k1520disktool --check    # Waechter cli_bcu_a5120

``--check``: Exit 1, wenn eine Diskette nicht die Wandlung ihres Belegs ist, das Bootband nicht die
Fixture tests/fixtures/kassette/bandtest_boot.k52 oder die Kassette nicht genau eine Bandmarke traegt.
"""

import argparse
import filecmp
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import disketten_beigaben as _db  # noqa: E402  (Ordner je Diskette)

BELEGE = os.path.join(REPO, 'doc', 'a5120_alt', 'bcu')

#: Diskette in disks/ -> (Beleg, Zusatzargumente der Wandlung)
DISKETTEN = {
    'a5120_bcu12_k5600-10_start-syl.hfe':
        (os.path.join(BELEGE, 'satz2_SYL_auf_Spur1', 'disk.img'), ['--fs', 'cpa_auto']),
    'a5120_bcu12_k5600-10_arbeit-sys1.hfe':
        (os.path.join(BELEGE, 'satz2_OhneSYL.1', 'disk.img'), []),
}
BOOTBAND = 'bandtest_boot.k52'
BOOTBAND_QUELLE = os.path.join(REPO, 'tests', 'fixtures', 'kassette', BOOTBAND)
KASSETTE = 'bcu_kassette_formatiert.k52'


def _lauf(tool, *args):
    r = subprocess.run([*tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool[-1]), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _wandeln(tool, beleg, ziel, extra, tmp):
    """Beleg (.img) ueber eine Kopie nach @p ziel (.hfe) wandeln — der Beleg wird nur gelesen."""
    kopie = os.path.join(tmp, 'beleg.img')
    shutil.copyfile(beleg, kopie)
    if os.path.exists(ziel):
        os.remove(ziel)
    _lauf(tool, 'save-as', kopie, ziel, *extra)
    # Gegenprobe: zurueck nach .img muss der Beleg sein.
    rueck = os.path.join(tmp, 'rueck.img')
    if os.path.exists(rueck):
        os.remove(rueck)
    _lauf(tool, 'save-as', ziel, rueck, *extra)
    if not filecmp.cmp(rueck, beleg, shallow=False):
        sys.exit(f'{os.path.basename(ziel)}: Rueckwandlung weicht vom Beleg ab')


def k52_bloecke(pfad):
    """Bloecke je Seite einer .k52 (core/peripherals/kassette/k52_codec.h, Version 1)."""
    with open(pfad, 'rb') as f:
        d = f.read()
    if d[:4] != b'K52T' or d[4] != 1 or d[5] != 2:
        raise ValueError('keine .k52 der Version 1')
    pos = 4 + 1 + 1 + 2 + 4 + 5 * 4
    seiten = []
    for _ in range(2):
        _ws, _null, anzahl = struct.unpack_from('<BBI', d, pos)
        pos += 6
        bloecke = []
        for _ in range(anzahl):
            _luecke, laenge = struct.unpack_from('<IH', d, pos)
            pos += 6
            bloecke.append(d[pos:pos + laenge])
            pos += laenge
        seiten.append(bloecke)
    if pos != len(d):
        raise ValueError('Reste hinter dem letzten Block')
    return seiten


def kassette_ok(pfad):
    """Formatiert = Seite A genau EIN Block, die Bandmarke AA 00 00 AA; Seite B leer."""
    try:
        a, b = k52_bloecke(pfad)
    except (OSError, ValueError, struct.error) as e:
        print(f'{KASSETTE}: {e}')
        return False
    if a != [bytes([0xAA, 0x00, 0x00, 0xAA])] or b:
        print(f'{KASSETTE}: nicht „eine Bandmarke auf Seite A“ ({len(a)}/{len(b)} Bloecke)')
        return False
    return True


def bauen(tool, wurzel):
    with tempfile.TemporaryDirectory() as tmp:
        for name, (beleg, extra) in DISKETTEN.items():
            ziel = _db.diskpfad(wurzel, name)
            os.makedirs(os.path.dirname(ziel), exist_ok=True)
            _wandeln(tool, beleg, ziel, extra, tmp)
            print(f'{name}: gewandelt')
    ziel = _db.diskpfad(wurzel, BOOTBAND)
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    shutil.copyfile(BOOTBAND_QUELLE, ziel)
    print(f'{BOOTBAND}: kopiert')
    k = _db.diskpfad(wurzel, KASSETTE)
    if not os.path.isfile(k):
        print(f'{KASSETTE}: fehlt — erzeugen mit K1520_BEISPIELBAND=… (Kopf dieses Skripts)')
        return 1
    return 0 if kassette_ok(k) else 1


def pruefen(tool, wurzel):
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for name, (beleg, extra) in DISKETTEN.items():
            ist = _db.diskpfad(wurzel, name)
            if not os.path.isfile(ist):
                print(f'{name}: fehlt'); ok = False
                continue
            soll = os.path.join(tmp, name)
            _wandeln(tool, beleg, soll, extra, tmp)
            if filecmp.cmp(ist, soll, shallow=False):
                print(f'{name}: ok')
            else:
                print(f'{name}: ist nicht die Wandlung von {os.path.relpath(beleg, REPO)}'); ok = False
    band = _db.diskpfad(wurzel, BOOTBAND)
    if os.path.isfile(band) and filecmp.cmp(band, BOOTBAND_QUELLE, shallow=False):
        print(f'{BOOTBAND}: ok')
    else:
        print(f'{BOOTBAND}: fehlt oder weicht von der Fixture ab'); ok = False
    k = _db.diskpfad(wurzel, KASSETTE)
    if not os.path.isfile(k):
        print(f'{KASSETTE}: fehlt'); ok = False
    elif kassette_ok(k):
        print(f'{KASSETTE}: ok')
    else:
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--emulator', action='append', default=[],
                    help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = [*o.emulator, os.path.abspath(o.tool)]
    if o.check:
        return 0 if pruefen(tool, o.out) else 1
    return bauen(tool, o.out)


if __name__ == '__main__':
    sys.exit(main())
