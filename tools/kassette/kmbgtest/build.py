#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Bau des SCPX-Pruefprogramms KMBGTEST (AP K8, Entwurf 29)
=====================================================================

Assembliert src/kmbgtest.mac und bindet es mit der Robotron-Kassettenbibliothek
doc/kassette/fex/KMBG.REL (M80 + LINKMT ueber cparun aus der CPA_Workbench,
CPA_TOOLS=<pfad>, gemeinsamer Bau tools/cpm_bau.py) zu kmbgtest.com.

  python3 tools/kassette/kmbgtest/build.py          # baut kmbgtest.com
  python3 tools/kassette/kmbgtest/build.py --check  # bytegleich mit der eingecheckten .com?
                                                    # (0 gleich, 1 verschieden, 77 ohne Werkzeugkette)

kmbgtest.com ist eingecheckt (die CI hat die CPA_Workbench nicht); Waechter
ScpxKmbg.SchreibtUndLiestEinenBlock (tests/system/test_scpx_kmbg.cpp).
"""
import os
import sys

_HIER = os.path.dirname(os.path.abspath(__file__))
_WURZEL = os.path.dirname(os.path.dirname(os.path.dirname(_HIER)))
sys.path.insert(0, os.path.join(_WURZEL, 'tools'))
import cpm_bau  # noqa: E402

PROGRAMME = [cpm_bau.Programm('kmbgtest', _HIER,
                              module=[os.path.join(_WURZEL, 'doc', 'kassette', 'fex', 'KMBG.REL')])]

if __name__ == '__main__':
    sys.exit(cpm_bau.hauptprogramm(PROGRAMME, __doc__))
