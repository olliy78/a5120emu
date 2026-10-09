#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die SCP-3.0-Systemdiskette des PC 1715W bauen   (disks/pc1715w_scp30_system.hfe)

SCP 3.0 (R-BWS) V0003 (28.03.89), Lader PC 1715W V0001, Format 5 x 1024 (`scp1715`, vier Systemspuren
`bootabbild.bin`).  Basis ist die bisherige Systemdiskette; dazu kommen das auf SCP 3 zugeschnittene Textprogramm V1/3B samt
Installer TPINSTD (aus der SCP-3.0-Programmdiskette), gemeinsame Programme (tools/scp_gemeinsam/: WM, BASIC, Pascal, M80 …)
und SERTEST.  Alle Programme wurden am PC-1715W-Emulator unter SCP 3.0 gestartet (Nachweis: README.md).

  python3 tools/scp_pc1715w/build.py --tool build/k1520disktool            # bauen
  python3 tools/scp_pc1715w/build.py --tool build/k1520disktool --check    # Wächter cli_scp_pc1715w
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import diskbau as db  # noqa: E402

DISK = db.Diskette(__file__, 'pc1715w_scp30_system.hfe', fs='scp1715', boot='bootabbild.bin')
HIER = DISK.hier


def soll():
    d = db.ordner(os.path.join(HIER, 'inhalt'))
    d['LIESMICH.TXT'] = db.liesmich(d['LIESMICH.TXT'])
    d['WM.HLP'] = db.wm_hlp()
    d.update(db.gemeinsam('WM.COM', 'BASIC.COM', 'PASCAL.COM', 'PASCAL.TXT', 'PASCAL.RES'))
    d.update(db.beigaben('SERTEST.COM'))
    return d


if __name__ == '__main__':
    DISK.main(soll, __doc__)
