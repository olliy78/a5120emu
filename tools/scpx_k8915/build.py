#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die SCPX-Systemdiskette des K8915 bauen   (disks/k8915_scpx8915_v24_system.hfe)

SCPX 8915 V 5.3, Anpassung „V24 (XON/XOFF)“ (die frühere Diskette „901“; der Handschrift-Name der Diskette soll nicht
mehr im Dateinamen stehen).  Systemspuren `bootabbild.bin` (= disks/boot_scpx8915_v24.bin), System- und Dienstdateien in
`inhalt/` (DISGEN, FORMAT, POWER, RADE von 901; DUMP, PIP, SOFTKEY, STAT, SUBM von 900), dazu gemeinsame Programme
(tools/scp_gemeinsam/) und die eigenen Prüfprogramme.  Nicht dabei: die Datenträgerkennung `***901.VOL`/`-SICHERH.901`.
Alle Programme wurden am K8915-Emulator gestartet (Nachweis: README.md).

  python3 tools/scpx_k8915/build.py --tool build/k1520disktool            # bauen
  python3 tools/scpx_k8915/build.py --tool build/k1520disktool --check    # Wächter cli_scpx_k8915
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import diskbau as db  # noqa: E402

DISK = db.Diskette(__file__, 'k8915_scpx8915_v24_system.hfe', fs='scpx8915', boot='bootabbild.bin')
HIER = DISK.hier


def soll():
    d = db.ordner(os.path.join(HIER, 'inhalt'))
    d['LIESMICH.TXT'] = db.liesmich(d['LIESMICH.TXT'])
    d['WM.HLP'] = db.wm_hlp()
    d.update(db.gemeinsam(
        'TP.COM', 'TPHT.OVR', 'TPOVLY1.OVR', 'TPDRUCK.OVR', 'TPINSCPA.COM', 'WM.COM', 'DIENST.COM', 'BASIC.COM',
        'PASCAL.COM', 'PASCAL.TXT', 'PASCAL.RES', 'PASSAVE.COM', 'PASINST.COM', 'M80.COM', 'LINKMT.COM', 'MLOAD.COM',
        'Z1.COM', 'ZSID.COM', 'RAMTEST.COM', 'DIMA.COM', 'UNERA.COM', 'DISKCOPY.COM'))
    d.update(db.beigaben('SERTEST.COM', 'RAFCPM.COM', 'RAF512.COM', 'LBREAD.COM', 'LBPUNCH.COM'))
    d['RAFTEST.COM'] = db.raftest()
    d.update(db.aus_fixture(os.path.join('tests', 'fixtures', 'raf'), 'RAFQUICK.COM'))
    return d


if __name__ == '__main__':
    DISK.main(soll, __doc__)
