#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Build-Skript fuer die A5120.16-Pruefprogramme (EM064/EM256, U8001)
==============================================================================

  em256adr  G1: Portbasis X10/X11 und Attributspeicher (reiner U880-Code)
  em16abl   S4/G2-G5: Ablaeufe des 16-Bit-Modes (U880 + U8001-Firmware fw16abl.s)
  em256ful  Volltest v2.0, Gruppen A-E (U880 + neun U8001-Bloecke fw_*.s)

Die U8001-Firmware wird mit z8kasm aus diesem Projekt assembliert (segmentiert,
`-s`) und als DB-Zeilen an die Stelle ``;%INCLUDE fw_….inc`` der Quelle gesetzt;
danach M80 + LINKMT wie bei allen eigenen CP/M-Programmen (tools/cpm_bau.py,
CPA_TOOLS=<pfad>).  z8kasm: ``build/z8kasm`` (tools/dev.sh build) oder
``Z8KASM=<pfad>``.

  python3 tools/em256/build.py [NAME …]     # bauen (Vorgabe: alle drei)
  python3 tools/em256/build.py clean
  python3 tools/em256/build.py --out <datei> NAME
  python3 tools/em256/build.py --check      # bytegleich mit den eingecheckten .com?
                                            # (0 gleich, 1 verschieden, 77 ohne Werkzeugkette)

Herkunft: CPA_Workbench/tools/16bitTest (em256ful v2.0, em16abl v1.2,
em256adr), uebernommen 2026-10-02.  Die .com sind eingecheckt.  Danach die
Disketten und Prueflinge nachziehen:
python3 tools/disketten_beigaben.py --tool <k1520disktool>
"""
import os
import sys

ORDNER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(ORDNER))
sys.path.insert(0, os.path.dirname(ORDNER))
import cpm_bau  # noqa: E402


def z8kasm():
    for p in (os.environ.get('Z8KASM'),
              os.path.join(REPO, 'build', 'z8kasm'),
              os.path.join(REPO, 'build', 'z8kasm.exe')):
        if p and os.path.isfile(p):
            return p
    raise RuntimeError("z8kasm nicht gefunden - tools/dev.sh build (oder Z8KASM=<pfad>)")


def firmware(quellen):
    """Vorstufe: U8001-Firmware -> <stem>.inc (DB-Zeilen) in build/."""
    def vorstufe(build_dir):
        asm = z8kasm()
        for src in quellen:
            stem = os.path.splitext(src)[0]
            binaer = os.path.join(build_dir, stem + '.bin')
            cpm_bau.run([asm, '-s', '-o', binaer, os.path.join(ORDNER, 'src', src)],
                        cwd=build_dir)
            with open(binaer, 'rb') as f:
                daten = f.read()
            zeilen = [f'; {src} - {len(daten)} Bytes, z8kasm -s']
            for i in range(0, len(daten), 12):
                zeilen.append('\tDB\t' + ','.join(f'0{b:02X}H' for b in daten[i:i + 12]))
            with open(os.path.join(build_dir, stem + '.inc'), 'w', encoding='ascii') as f:
                f.write('\n'.join(zeilen) + '\n')
    return vorstufe


PROGRAMME = [
    cpm_bau.Programm('em256adr', ORDNER),
    cpm_bau.Programm('em16abl', ORDNER, firmware(['fw16abl.s'])),
    cpm_bau.Programm('em256ful', ORDNER, firmware([
        'fw_add.s', 'fw_sub.s', 'fw_logic.s', 'fw_memrw.s', 'fw_loop.s',
        'fw_stack.s', 'fw_add32.s', 'fw_byte.s', 'fw_march.s'])),
]

if __name__ == '__main__':
    sys.exit(cpm_bau.hauptprogramm(PROGRAMME, __doc__))
