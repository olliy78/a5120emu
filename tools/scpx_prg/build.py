#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die SCPX-Systemdisketten des PRG 710 und PRG 710-1 bauen
    disks/prg710_scpx15_system.hfe      PRG 710:    SCPX V 1.5 „B. Daehmlow“ (BIOS B151V24/B152V24/B152IFSS)
    disks/prg710-1_scpx17_system.hfe    PRG 710-1:  SCPX 1526 V 1.7 (52K)    (BIOS B17172xx/B17272xx, xx = V2/ZI/FS/SD)

Format 16 x 256 (`scpx640`, wie die bisherigen PRG-Disketten).  Gemeinsam: `inhalt/` (Lader, CCP/BDOS, Dienst- und
Entwicklungsprogramme, PROM-Programmierer PROG, CONV1 UDOS->SCP), `tools/scp_gemeinsam/` und die eigenen
Prüfprogramme; je Gerät die BIOS-Module (`inhalt710/`, `inhalt710-1/`) und die Systemspuren (`bootabbild_*.bin`).  Die
Anwenderdaten der früheren 710-1-Diskette (KLINGEL.DAT, RITE.DAT, ROM*.DAT, LC80PRG.DAT, DIT.*, ALCP.*, …) sind
bewusst nicht mehr dabei.  Alle Programme wurden am PRG-Emulator unter SCPX gestartet (Nachweis: README.md).

  python3 tools/scpx_prg/build.py --tool build/k1520disktool            # bauen
  python3 tools/scpx_prg/build.py --tool build/k1520disktool --check    # Wächter cli_scpx_prg
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import diskbau as db  # noqa: E402

D710 = db.Diskette(__file__, 'prg710_scpx15_system.hfe', fs='scpx640', boot='bootabbild_710.bin',
                   create_args=['--prg', '710'])
D7101 = db.Diskette(__file__, 'prg710-1_scpx17_system.hfe', fs='scpx640', boot='bootabbild_710-1.bin',
                    create_args=['--prg', '710-1'])
HIER = D710.hier

GERAETE = {
    'prg710': dict(
        geraet='PRG 710', modul_ordner='inhalt710',
        system='SCPX V 1.5 "B. Daehmlow" (BIOS BW 15, Tastatur 8279/K7609)',
        module='''  BIOS-Module   B151V24 und B152V24 (V.24, 1K- bzw. 2K-Bildschirm), B152IFSS
                (IFSS); das mitgelieferte System ist ein V.24-System.
                Fuer den PRG 710-1 gibt es die Module B17172xx/B17272xx.''',
        sertest='V.24, IFSS-Hauptdrucker, ZIFSS'),
    'prg710-1': dict(
        geraet='PRG 710-1', modul_ordner='inhalt710-1',
        system='SCPX 1526 - V 1.7 (52K) (BIOS BW 17, Tastatur K7672)',
        module='''  BIOS-Module   B17172xx / B17272xx (1K- bzw. 2K-Bildschirm); xx = V2
                (V.24 50H), ZI (ZIFSS 5CH), FS (Fernschreiber C5H), SD
                (Drucker SD1156 E0H). Fuer den PRG 710 gibt es die Module
                B151V24, B152V24 und B152IFSS.''',
        sertest='V.24, ZIFSS'),
}


def soll_fuer(schluessel):
    g = GERAETE[schluessel]

    def soll():
        d = db.ordner(os.path.join(HIER, 'inhalt'), ausser=('LIESMICH.vorlage',))
        d.update(db.ordner(os.path.join(HIER, g['modul_ordner'])))
        text = db.lies(HIER, 'inhalt', 'LIESMICH.vorlage').decode('ascii')
        for k, v in (('@GERAET@', g['geraet']), ('@SYSTEM@', g['system']), ('@MODULE@', g['module']),
                     ('@SERTEST@', g['sertest'])):
            text = text.replace(k, v)
        d['LIESMICH.TXT'] = db.liesmich(text.encode('ascii'))
        d['WM.HLP'] = db.wm_hlp()
        d.update(db.gemeinsam(
            'TP.COM', 'TPHT.OVR', 'TPOVLY1.OVR', 'TPDRUCK.OVR', 'TPINSCPA.COM', 'WM.COM', 'DIENST.COM', 'BASIC.COM',
            'PASCAL.COM', 'PASCAL.TXT', 'PASCAL.RES', 'M80.COM', 'LINKMT.COM',
            'MLOAD.COM', 'Z1.COM', 'ZSID.COM', 'RAMTEST.COM', 'DIMA.COM', 'UNERA.COM', 'DISKCOPY.COM'))
        d.update(db.beigaben('SERTEST.COM', 'RAFCPM.COM', 'RAF512.COM', 'LBREAD.COM', 'LBPUNCH.COM'))
        d['RAFTEST.COM'] = db.raftest()
        d.update(db.aus_fixture(os.path.join('tests', 'fixtures', 'raf'), 'RAFQUICK.COM'))
        return d
    return soll


if __name__ == '__main__':
    db.main_mehrere([(D710, soll_fuer('prg710')), (D7101, soll_fuer('prg710-1'))], __doc__)
