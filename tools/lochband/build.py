#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Build-Skript fuer lbread.com und lbpunch.com (Lochband K6022/SIF1000)
=====================================================================

Assembliert src/lbread.mac, src/lbpunch.mac mit M80 + LINKMT (ueber cparun aus der
CPA_Workbench, CPA_TOOLS=<pfad>) zu lbread.com / lbpunch.com — gemeinsamer Bau in
tools/cpm_bau.py.

  python3 tools/lochband/build.py            # baut, Ergebnis tools/lochband/lbread.com / lbpunch.com
  python3 tools/lochband/build.py clean      # leert build/
  python3 tools/lochband/build.py --out <datei>   # temporaer bauen, nach <datei>
  python3 tools/lochband/build.py --check    # bytegleich mit der eingecheckten .com?
                                            # (0 gleich, 1 verschieden, 77 ohne Werkzeugkette)

Die .com ist eingecheckt (die CI hat die CPA_Workbench nicht).  Danach die
Disketten nachziehen: python3 tools/disketten_beigaben.py --tool <k1520disktool>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import cpm_bau  # noqa: E402

PROGRAMME = [cpm_bau.Programm(n, os.path.dirname(os.path.abspath(__file__))) for n in ('lbread', 'lbpunch')]

if __name__ == '__main__':
    sys.exit(cpm_bau.hauptprogramm(PROGRAMME, __doc__))
