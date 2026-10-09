#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die SCPX-Systemdiskette des A5120 bauen   (disks/a5120_scpx17_k5601_system.hfe)

SCPX 1526 V 1.7 (52K), Format 5 x 1024 (`scpx798`, K5601).  Basis: die Hardy-Diskette `scpx17_5x1024_k5601_hardy`
(Systemspuren `bootabbild.bin` = disks/boot_scpx798.bin, System- und Dienstdateien in `inhalt/`).  Dazu kommen
gemeinsame Programme (tools/scp_gemeinsam/), die eigenen Pruefprogramme (EM256*, RAF*, SERTEST, ROMREAD, Lochband)
und eine zu den A5120-Anschluessen passende TLC.PAR.  Die Programme wurden am A5120-Emulator unter SCPX gestartet
(Nachweis: README.md).  Ein 16x256-System erzeugt der Anwender selbst (INIT + SYSG, s. LIESMICH.TXT).

  python3 tools/scpx_a5120/build.py --tool build/k1520disktool            # bauen
  python3 tools/scpx_a5120/build.py --tool build/k1520disktool --check    # Waechter cli_scpx_a5120
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import diskbau as db  # noqa: E402

DISK = db.Diskette(__file__, 'a5120_scpx17_k5601_system.hfe', fs='scpx798', boot='bootabbild.bin')
HIER = DISK.hier


def soll():
    d = db.ordner(os.path.join(HIER, 'inhalt'))
    d['LIESMICH.TXT'] = db.liesmich(d['LIESMICH.TXT'])
    d['WM.HLP'] = db.wm_hlp()
    d.update(db.gemeinsam(
        'TP.COM', 'TPHT.OVR', 'TPOVLY1.OVR', 'TPDRUCK.OVR', 'TPINSCPA.COM', 'WM.COM', 'DIENST.COM', 'BASIC.COM',
        'PASCAL.COM', 'PASCAL.TXT', 'PASCAL.RES', 'PASSAVE.COM', 'PASINST.COM', 'M80.COM', 'LINKMT.COM', 'MLOAD.COM',
        'Z1.COM', 'ZSID.COM', 'RAMTEST.COM', 'DIMA.COM', 'UNERA.COM', 'DISKCOPY.COM', 'TLC.COM', 'TLCX.PMA', 'EM256TST.COM'))
    d['TLC.PAR'] = db.gemeinsam('TLC_A5120.PAR')['TLC_A5120.PAR']
    d.update(db.beigaben('SERTEST.COM', 'ROMREAD.COM', 'EM256ADR.COM', 'EM16ABL.COM', 'EM256FUL.COM',
                         'RAFCPM.COM', 'RAF512.COM', 'LBREAD.COM', 'LBPUNCH.COM'))
    d['RAFTEST.COM'] = db.raftest()
    d.update(db.aus_fixture(os.path.join('tests', 'fixtures', 'raf'), 'RAFQUICK.COM'))
    return d


if __name__ == '__main__':
    DISK.main(soll, __doc__)
