#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Bau des Kassetten-Bootprogramms BANDTEST (AP K7, Entwurf 29)
=========================================================================

Assembliert src/bandtest.mac (M80 + LINKMT ueber cparun aus der CPA_Workbench,
CPA_TOOLS=<pfad>, gemeinsamer Bau tools/cpm_bau.py) zu bandtest.bin — den
Bandinhalt ab 0400H.  Das Bootband entsteht daraus mit dem Kassettenwerkzeug:

  python3 tools/kassette/bandtest/build.py          # baut bandtest.bin
  python3 tools/kassette/bandtest/build.py --check  # bytegleich mit der eingecheckten .bin?
                                                    # (0 gleich, 1 verschieden, 77 ohne Werkzeugkette)
  tools/dev.sh tool k1520tape bootband tools/kassette/bandtest/bandtest.bin \\
      tests/fixtures/kassette/bandtest_boot.k52

bandtest.bin und das Band sind eingecheckt (die CI hat die CPA_Workbench nicht);
der Waechter cli_bandtest_bootband prueft, dass das Band zum Programm passt.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
import cpm_bau  # noqa: E402

PROGRAMME = [cpm_bau.Programm('bandtest', os.path.dirname(os.path.abspath(__file__)), endung='bin')]

if __name__ == '__main__':
    sys.exit(cpm_bau.hauptprogramm(PROGRAMME, __doc__))
