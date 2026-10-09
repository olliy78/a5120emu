#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  die CP/A-Systemdiskette des A5120 (mit allen BIOS-Varianten) bauen
===============================================================================

  disks/a5120_cpa_k5601_system.hfe

DIE CP/A-Diskette der Auslieferung (statt frueher vier: clock, noclock, combo5zoll,
combo8zoll).  Sie bootet mit `@OS.COM` = BIOS mit Uhr, 3 x K5601; die uebrigen BIOS-Fassungen
liegen unter eigenem Namen daneben und werden vom A>-Prompt aus gestartet (vollstaendiger
Neustart des BIOS, ein Warmstart behaelt die Wahl; Versuch 2026-10-08, doc/disketten_bestand.md §4):

  @OS.COM    K5601 / K5601 / K5601, mit Uhr        OSCOMB5   K5601 / K5600.10 / K5600.20
  OSNOCLK    K5601 / K5601 / K5601, ohne Uhr       OSCOMB8   K5601 / MF3200 / MF6400
  OSRAF      wie OSNOCLK, RAF-Treiber (M:)         OSEM256   wie OSNOCLK, A5120.16 (M: im EM256)

Inhalt (Herkunft je Gruppe):
  varianten/   die sechs `@OS.COM`-Fassungen unter ihrem Diskettennamen (Abzuege der Fixtures
               tests/fixtures/disks/cpa_cpa780_*.img; Bau der -raf/-em256: tests/fixtures/README.md)
  inhalt/      Systemprogramme (FORMAT, CPABCGEN, PIP, POWER ...), STAT.COM (aus SCPX 1.7, laeuft
               unter CP/A), TP, BASIC, Pascal, LIESMICH.TXT (hier LF; auf der Diskette CRLF + ^Z),
               WM.HLP (deutsch; ERZEUGT aus quellen/WM_HLP_de.txt bei jedem Bau, mit eingecheckt)
  Beigaben     eigene Programme aus ihren Quellordnern, wie tools/disketten_beigaben.py:
               SERTEST, ROMREAD, EM256ADR, EM16ABL, EM256FUL, RAFCPM, RAF512, LBREAD, LBPUNCH
  RAFTEST      ZWG-Pruefprogramm aus tests/fixtures/raf/, Kartenadresse auf 89H/88H gepatcht
               (Patchbereich Dateiversatz 22H/23H, Vorgabe 8FH/8EH; wie test_raf_zwg.cpp)
  RAFQUICK     ZWG-Schnelltest aus tests/fixtures/raf/, unveraendert

Systemspuren: disks/boot_cpa780.bin.  `@OS.COM` wird als ERSTE Datei eingespielt (dort, in
Block 3, sucht der Lader).

Aufruf:
  python3 tools/cpa_a5120/build.py --tool build/k1520disktool            # bauen
  python3 tools/cpa_a5120/build.py --tool build/k1520disktool --check    # nur vergleichen

``--check`` ist der Waechter ``cli_cpa_a5120``: Exit 1, wenn die Diskette in disks/ nicht genau
den Inhalt hat, den dieser Ordner beschreibt (Dateiliste, Inhalt, Systemspuren).
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import disketten_beigaben as beigaben  # noqa: E402  (Programmliste + Quellpfade, EINE Wahrheit)

NAME = 'a5120_cpa_k5601_system.hfe'
BOOT = os.path.join(REPO, 'disks', 'boot_cpa780.bin')
RAF_FIXTURES = os.path.join(REPO, 'tests', 'fixtures', 'raf')
# Die Beigaben des A5120, die auf CP/A-Disketten gehoeren (ohne PCTEST: PC 1715)
BEIGABEN = ['SERTEST.COM', 'ROMREAD.COM', 'EM256ADR.COM', 'EM16ABL.COM', 'EM256FUL.COM',
            'RAFCPM.COM', 'RAF512.COM', 'LBREAD.COM', 'LBPUNCH.COM']
LIESMICH = 'LIESMICH.TXT'


def _lauf(tool, *args):
    r = subprocess.run([tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _cpm_text(daten):
    """LF -> CRLF, mit ^Z abschliessen und auf volle 128-B-Saetze auffuellen (so liefert get)."""
    t = daten.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n') + b'\x1a'
    return t + b'\x1a' * (-len(t) % 128)


def _wm_hlp():
    """WM.HLP deutsch aus quellen/WM_HLP_de.txt.  Aufbau wie das russische Original
    (quellen/WM.HLP.russisch): Zeilen mit CRLF, jede Seite endet mit DC1 TAB CRLF, der Text
    mit ' ' und ^Z bis zum vollen 128-B-Satz."""
    text = open(os.path.join(HIER, 'quellen', 'WM_HLP_de.txt'), encoding='ascii').read()
    seiten = text.split('\n=====\n')
    if len(seiten) != 4:
        sys.exit('WM_HLP_de.txt: vier Seiten erwartet')
    out = b''
    for i, seite in enumerate(seiten):
        zeilen = seite.rstrip('\n').split('\n')
        out += b''.join(z.encode('ascii') + b'\r\n' for z in zeilen)
        out += b'\x11\t\r\n' if i < len(seiten) - 1 else b' '
    return out + b'\x1a' * (-len(out) % 128 or 128)


def _raftest():
    daten = bytearray(open(os.path.join(RAF_FIXTURES, 'RAFTEST.COM'), 'rb').read())
    if daten[0x22] != 0x8F or daten[0x23] != 0x8E:
        sys.exit('RAFTEST.COM: Patchbereich nicht wie erwartet')
    daten[0x22], daten[0x23] = 0x89, 0x88   # RFCtl, RFDat
    return bytes(daten)


def _lies(*teile):
    with open(os.path.join(*teile), 'rb') as f:
        return f.read()


def soll():
    """{Diskettenname: Bytes} - der Inhalt, den die Diskette tragen muss."""
    inhalt = {}
    for ordner in ('varianten', 'inhalt'):
        for n in sorted(os.listdir(os.path.join(HIER, ordner))):
            inhalt[n] = _lies(HIER, ordner, n)
    inhalt[LIESMICH] = _cpm_text(inhalt[LIESMICH])
    for n in BEIGABEN:
        inhalt[n] = _lies(beigaben.PROGRAMME[n])
    inhalt['RAFTEST.COM'] = _raftest()
    inhalt['RAFQUICK.COM'] = _lies(RAF_FIXTURES, 'RAFQUICK.COM')
    return inhalt


def wm_hlp_schreiben():
    """inhalt/WM.HLP aus quellen/WM_HLP_de.txt erzeugen (damit inhalt/ vollstaendig ist)."""
    with open(os.path.join(HIER, 'inhalt', 'WM.HLP'), 'wb') as f:
        f.write(_wm_hlp())


def bauen(tool, ausgabe):
    wm_hlp_schreiben()
    ziel = os.path.join(ausgabe, NAME)
    if os.path.exists(ziel):
        os.remove(ziel)
    with tempfile.TemporaryDirectory() as tmp:
        quelle = os.path.join(tmp, 'q')
        os.makedirs(quelle)
        for n, d in soll().items():
            with open(os.path.join(quelle, n), 'wb') as f:
                f.write(d)
        os_com = os.path.join(quelle, '@OS.COM')
        _lauf(tool, 'create', ziel, '--fs', 'cpa780', '--boot', BOOT)
        _lauf(tool, 'put', ziel, os_com, '--no-backup')          # ERSTE Datei
        os.remove(os_com)
        _lauf(tool, 'put', ziel, quelle, '--no-backup')
    pruefung = _lauf(tool, 'check', ziel, '--full')
    if 'ohne Befund' not in pruefung:
        sys.exit(f'{NAME}: Dateisystempruefung hat Befunde:\n{pruefung}')
    print(f'{NAME}: gebaut, check --full ohne Befund')


def pruefen(tool, ausgabe):
    diskette = os.path.join(ausgabe, NAME)
    if not os.path.isfile(diskette):
        print(f'{NAME}: fehlt')
        return False
    erwartet = soll()
    ok = True
    if erwartet['WM.HLP'] != _wm_hlp():
        print('inhalt/WM.HLP passt nicht zu quellen/WM_HLP_de.txt (build.py ohne --check ausfuehren)')
        ok = False
    with tempfile.TemporaryDirectory() as tmp:
        ist_ordner = os.path.join(tmp, 'ist')
        os.makedirs(ist_ordner)
        _lauf(tool, 'get', diskette, '*', '--to', ist_ordner)
        ist = {n: _lies(ist_ordner, n) for n in os.listdir(ist_ordner)
               if n != 'cpm-dateiangaben.txt'}
        boot = os.path.join(tmp, 'boot.bin')
        _lauf(tool, 'boot-get', diskette, boot)
        boot_ok = _lies(boot) == _lies(BOOT)
        # @OS.COM muss die erste Datei im Verzeichnis sein (Lader sucht in Block 3)
        erste = [z.strip() for z in _lauf(tool, 'ls', diskette).splitlines()
                 if z.strip() and ':' not in z and 'Datei' not in z]
    for n in sorted(set(erwartet) | set(ist)):
        if n not in ist:
            print(f'{NAME}: {n} fehlt auf der Diskette'); ok = False
        elif n not in erwartet:
            print(f'{NAME}: {n} steht auf der Diskette, aber nicht im Ordner'); ok = False
        elif hashlib.md5(erwartet[n]).digest() != hashlib.md5(ist[n]).digest():
            print(f'{NAME}: {n} weicht ab'); ok = False
    if not boot_ok:
        print(f'{NAME}: Systemspuren weichen ab'); ok = False
    if not erste or erste[0] != '@OS.COM':
        print(f'{NAME}: @OS.COM ist nicht die erste Datei ({erste[:1]})'); ok = False
    if ok:
        print(f'{NAME}: ok ({len(erwartet)} Dateien)')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = os.path.abspath(o.tool)
    if o.check:
        sys.exit(0 if pruefen(tool, o.out) else 1)
    bauen(tool, o.out)


if __name__ == '__main__':
    main()
