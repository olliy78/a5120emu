#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die Systemdisketten des PC 1715 bauen (drei Betriebssysteme, drei Disketten)
    disks/pc1715_scp1715_v0006_system.hfe   SCP 1715 V0006 (03/08/87, 48 KB), 5 x 1024
    disks/pc1715_scp1715_v0007_system.hfe   SCP 1715 V0007 (01/11/88, 50 KB, nachladbarer CCP), 16 x 256
    disks/pc1715_cpz22_system.hfe           CP/Z 2.2 („52K CP/Z 2.2 ZOAZ MRL“ 06.12.88), 16 x 256

Basis sind die bisherigen Bootdisketten (Systemspuren `bootabbild_*.bin`, Dateien in `inhalt_v0006/`, `inhalt_v0007/`,
`inhalt_cpz22/`); dazu kommen gemeinsame Programme (tools/scp_gemeinsam/), SCP-Werkzeuge und Sprachen (`inhalt_scp/`) und
SERTEST.  Alle Programme wurden am PC-1715-Emulator unter dem jeweiligen System gestartet (Nachweis: README.md).

  python3 tools/scp_pc1715/build.py --tool build/k1520disktool            # bauen
  python3 tools/scp_pc1715/build.py --tool build/k1520disktool --check    # Wächter cli_scp_pc1715
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import diskbau as db  # noqa: E402

V6 = db.Diskette(__file__, 'pc1715_scp1715_v0006_system.hfe', fs='scp1715', boot='bootabbild_v0006.bin')
V7 = db.Diskette(__file__, 'pc1715_scp1715_v0007_system.hfe', fs='scpx640', boot='bootabbild_v0007.bin')
CPZ = db.Diskette(__file__, 'pc1715_cpz22_system.hfe', fs='scpx640', boot='bootabbild_cpz22.bin')
HIER = V6.hier

GEMEINSAM_SCP = ('TP.COM', 'TPHT.OVR', 'TPOVLY1.OVR', 'TPDRUCK.OVR', 'TPINSCPA.COM', 'WM.COM', 'DIENST.COM', 'BASIC.COM',
                 'PASCAL.COM', 'PASCAL.TXT', 'PASCAL.RES', 'PASSAVE.COM', 'PASINST.COM', 'M80.COM', 'LINKMT.COM',
                 'MLOAD.COM', 'Z1.COM', 'ZSID.COM', 'RAMTEST.COM', 'TLC.COM', 'TLC.PAR', 'TLCX.PMA', 'DIMA.COM', 'DISKCOPY.COM',
                 'UNERA.COM')

SCP = {
    'v0006': dict(
        titel='SCP 1715 (V0006) fuer den PC 1715 - Systemdiskette des k1520emu', basis='inhalt_v0006',
        system='SCP "ROBOTRON 1715", VERS. 0006 - 03/08/87 - 48 KB (CCP/BDOS V0/4)',
        format='5 x 1024 Byte, 80 Spuren, beidseitig (K5601); vier Systemspuren.',
        hinweis='',
        pctest='''  PCTEST            Werkstest des PC 1715 (Robotron). Pruefstecker 320-032
                    (V.24), 330-032 (IFSS), 330-042 (Drucker); die Disketten
                    muessen initialisiert sein.\n''',
        systemprogramme='''  INIT      Diskette formatieren (INIT - SCP V 0.5, Geraet ROBOTRON 1715).
  SGEN      Systemgenerierung (V 0.5): System aus einer COM-Datei, aus dem
            Speicher oder von der Bootspur laden und auf Diskette schreiben.
  INSTSCP   Installation: System laden/sichern, Systemparameter anzeigen,
            Floppy-, E/A-, Treiber- und Kaltstart-Installation.'''),
    'v0007': dict(
        titel='SCP 1715 (V0007) fuer den PC 1715 - Systemdiskette des k1520emu', basis='inhalt_v0007',
        system='SCP "ROBOTRON 1715", VERS. 0007 - 01/11/88 - 50 KB; Betriebssystem-\n'
               'version mit nachladbarem CCP (CCP.SPR, TPA 50 KByte)',
        format='16 x 256 Byte, 80 Spuren, beidseitig; vier Systemspuren (2. Fassung des\n'
               'SCP 1715, anderes Format als V0006).',
        hinweis='',
        pctest='',
        systemprogramme='''  INIT      Diskette formatieren.
  SGEN      Systemgenerierung: System in den Systemspuren erzeugen.
  INSTSCP   Installation: System laden/sichern, Systemparameter anzeigen,
            Floppy-, E/A-, Treiber- und Kaltstart-Installation.
  CCP.SPR   der nachladbare Kommandoprozessor (muss auf der Diskette bleiben).'''),
}


def _scp_soll(schluessel):
    g = SCP[schluessel]

    def soll():
        d = db.ordner(os.path.join(HIER, g['basis']))
        d.update(db.ordner(os.path.join(HIER, 'inhalt_scp'), ausser=('LIESMICH.vorlage',)))
        text = db.lies(HIER, 'inhalt_scp', 'LIESMICH.vorlage').decode('ascii')
        for k, v in (('@TITEL@', g['titel']), ('@UNTERSTRICH@', '=' * len(g['titel'])), ('@SYSTEM@', g['system']),
                     ('@FORMAT@', g['format']), ('@HINWEIS@', g['hinweis']),
                     ('@SYSTEMPROGRAMME@', g['systemprogramme']), ('@PCTEST@\n', g['pctest'])):
            text = text.replace(k, v)
        d['LIESMICH.TXT'] = db.liesmich(text.encode('ascii'))
        d['WM.HLP'] = db.wm_hlp()
        d.update(db.gemeinsam(*GEMEINSAM_SCP))
        d.update(db.beigaben('SERTEST.COM'))
        return d
    return soll


def _cpz_soll():
    d = db.ordner(os.path.join(HIER, 'inhalt_cpz22'), ausser=('LIESMICH.vorlage',))
    d['LIESMICH.TXT'] = db.liesmich(db.lies(HIER, 'inhalt_cpz22', 'LIESMICH.vorlage'))
    d['WM.HLP'] = db.wm_hlp()
    d.update(db.gemeinsam('WM.COM', 'DIENST.COM', 'BASIC.COM', 'TLC.COM', 'TLC.PAR', 'TLCX.PMA'))
    d.update(db.beigaben('SERTEST.COM'))
    return d


if __name__ == '__main__':
    db.main_mehrere([(V6, _scp_soll('v0006')), (V7, _scp_soll('v0007')), (CPZ, _cpz_soll)], __doc__)
