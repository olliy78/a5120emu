#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  BCU-880-Disketten und Beispielbaender des fruehen A5120 in disks/ legen
====================================================================================

  disks/a5120_bcu/a5120_bcu12_k5600-10_start-syl.hfe     Startdiskette „SYL auf Spur 1“ (Satz 2, V1.2)
  disks/a5120_bcu/a5120_bcu12_k5600-10_arbeit-sys1.hfe   Arbeitsdiskette „ohne SYL“ (SYS1, V1.2, KMBG.REL)
  disks/a5120_bcu/bcu_kassette_formatiert.k52            Kassette, von BCU-INIT formatiert (s. u.)
  disks/a5120_bandtest/bandtest_boot.k52                 Demo-Bootband (Eigenbau, tools/kassette/bandtest/)

Die beiden Disketten gehen auf die Belege doc/a5120_alt/bcu/satz2_SYL_auf_Spur1/disk.img und
satz2_OhneSYL.1/disk.img zurueck (NIE aendern):

  * Die **Arbeitsdiskette** ist der Beleg, mit dem DiskTool unveraendert nach .hfe gewandelt
    (`save-as`).  Ihre Systemspuren sind das Bootabbild disks/bootsektoren/boot_bcu12_ohnesyl.bin.
  * Die **Startdiskette** wird GEBAUT: `create --fs bcu880_syl --boot boot_bcu12_syl_k7023.bin`
    (das Bootabbild = Zylinder 0-4 des Belegs; das DiskTool prueft Lader und Selbstpruefung vor dem
    Anlegen).  Gegenprobe: zurueck nach .img muss der Beleg sein, Sektor fuer Sektor — damit ist
    der Bau aus dem Bootabbild bewiesen, und jeder kann sich mit demselben Aufruf eine neue
    Startdiskette (auch mit dem K7024-Treiber: boot_bcu12_syl_k7024.bin) bauen.

Die Bootabbilder sind `boot-get` der Belege; ``--check`` vergleicht auch das.

Die formatierte Kassette entsteht im Lauf von BCU 880 selbst (Test BcuKassette.FormatiertSchreibtLiest,
`B:INIT` an Geraet 2 = F:) und wird dort mit dem Inhalt in disks/ verglichen (Bloecke, nicht die
Luecken).  Neu erzeugen:

  K1520_BEISPIELBAND=$PWD/disks/a5120_bcu/bcu_kassette_formatiert.k52 \\
      tools/dev.sh test-format -R BcuKassette.FormatiertSchreibtLiest

Aufruf:
  python3 tools/bcu_a5120/build.py --tool build/k1520disktool            # bauen
  python3 tools/bcu_a5120/build.py --tool build/k1520disktool --check    # Waechter cli_bcu_a5120

``--check``: Exit 1, wenn eine Diskette nicht die Wandlung ihres Belegs ist, das Bootband nicht die
Fixture tests/fixtures/kassette/bandtest_boot.k52 oder die Kassette nicht genau eine Bandmarke traegt.
"""

import argparse
import filecmp
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import disketten_beigaben as _db  # noqa: E402  (Ordner je Diskette)

BELEGE = os.path.join(REPO, 'doc', 'a5120_alt', 'bcu')

BOOTSEKTOREN = os.path.join(REPO, 'disks', 'bootsektoren')

#: Diskette in disks/ -> (Beleg, Zusatzargumente der Wandlung)  — ueber `save-as` gewandelt
DISKETTEN = {
    'a5120_bcu12_k5600-10_arbeit-sys1.hfe':
        (os.path.join(BELEGE, 'satz2_OhneSYL.1', 'disk.img'), []),
    # EIGENBAU (bcu.md §9, tools/bcu_v21_eigenbau.py --treiber): V2.1-System im SYL-Aufbau; die Fixture
    # ist der Beleg des Baus, nicht einer Originaldiskette
    'a5120_bcu21_k5600-10_eigenbau-syl-v21.hfe':
        (os.path.join(REPO, 'tests', 'fixtures', 'disks',
                      'bcu21_ss40-26x128_k5600-10_eigenbau-syl-v21.img'), ['--fs', 'bcu880_syl']),
}
#: Diskette in disks/ -> (Beleg, Dateisystem, Bootabbild) — mit `create --boot` GEBAUT
GEBAUT = {
    'a5120_bcu12_k5600-10_start-syl.hfe':
        (os.path.join(BELEGE, 'satz2_SYL_auf_Spur1', 'disk.img'), 'bcu880_syl',
         os.path.join(BOOTSEKTOREN, 'boot_bcu12_syl_k7023.bin')),
    # EIGENBAU (kein Beleg, doc/a5120_alt/bcu.md §12): Startdiskette MIT Dateisystem (nur als A:)
    'a5120_bcu12_k5600-10_kombi40-eigenbau.hfe':
        (None, 'bcu880_kombi', os.path.join(BOOTSEKTOREN, 'boot_bcu12_kombi_k7023.bin')),
    # dasselbe fuer 80 Spuren (K5600.20, 96 tpi)
    'a5120_bcu12_k5600-20_kombi80-eigenbau.hfe':
        (None, 'bcu880_kombi80', os.path.join(BOOTSEKTOREN, 'boot_bcu12_kombi80_k7023.bin')),
}
#: Arbeitsdiskette 80 Spuren (EIGENBAU): `create --fs bcu880_80 --boot` + die Dateien der Beleg-Arbeitsdiskette
ARBEIT80 = 'a5120_bcu12_k5600-20_arbeit80-eigenbau.hfe'
#: Arbeitsdiskette 40 Spuren (EIGENBAU, K5600.10): `create --fs bcu880 --boot` + dieselben Dateien
ARBEIT40 = 'a5120_bcu12_k5600-10_arbeit40-eigenbau.hfe'
#: Kombi-Bootabbild -> (Startabbild, Spurzahl) fuer tools/bcu_kombi_bau.py
KOMBI_ABBILDER = {
    'boot_bcu12_kombi_k7023.bin': ('boot_bcu12_syl_k7023.bin', 40),
    'boot_bcu12_kombi_k7024.bin': ('boot_bcu12_syl_k7024.bin', 40),
    'boot_bcu12_kombi80_k7023.bin': ('boot_bcu12_syl_k7023.bin', 80),
    'boot_bcu12_kombi80_k7024.bin': ('boot_bcu12_syl_k7024.bin', 80),
}
#: Bootabbild in disks/bootsektoren/ -> Beleg, dem es mit `boot-get` entnommen ist
BOOTABBILDER = {
    'boot_bcu12_syl_k7023.bin': os.path.join(BELEGE, 'satz2_SYL_auf_Spur1', 'disk.img'),
    'boot_bcu12_syl_k7024.bin': os.path.join(BELEGE, 'satz1_SYL_auf_Spur1', 'disk.img'),
    'boot_bcu12_ohnesyl.bin':   os.path.join(BELEGE, 'satz2_OhneSYL.1', 'disk.img'),
}
BOOTBAND = 'bandtest_boot.k52'
BOOTBAND_QUELLE = os.path.join(REPO, 'tests', 'fixtures', 'kassette', BOOTBAND)
KASSETTE = 'bcu_kassette_formatiert.k52'


def _lauf(tool, *args):
    r = subprocess.run([*tool, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"Fehler: {' '.join([os.path.basename(tool[-1]), *args])}\n{r.stdout}{r.stderr}")
    return r.stdout


def _wandeln(tool, beleg, ziel, extra, tmp):
    """Beleg (.img) ueber eine Kopie nach @p ziel (.hfe) wandeln — der Beleg wird nur gelesen."""
    kopie = os.path.join(tmp, 'beleg.img')
    shutil.copyfile(beleg, kopie)
    if os.path.exists(ziel):
        os.remove(ziel)
    _lauf(tool, 'save-as', kopie, ziel, *extra)
    # Gegenprobe: zurueck nach .img muss der Beleg sein.
    rueck = os.path.join(tmp, 'rueck.img')
    if os.path.exists(rueck):
        os.remove(rueck)
    _lauf(tool, 'save-as', ziel, rueck, *extra)
    if not filecmp.cmp(rueck, beleg, shallow=False):
        sys.exit(f'{os.path.basename(ziel)}: Rueckwandlung weicht vom Beleg ab')


def _bauen(tool, beleg, fs, boot, ziel, tmp):
    """Diskette mit `create --boot` bauen; Gegenprobe: zurueck nach .img ist der Beleg."""
    if os.path.exists(ziel):
        os.remove(ziel)
    _lauf(tool, 'create', ziel, '--fs', fs, '--boot', boot)
    if beleg is None:
        return                      # Eigenbau: kein Beleg, der Bau selbst ist der Nachweis (Test DisktoolBcu.Kombi*)
    rueck = os.path.join(tmp, 'rueck_gebaut.img')
    if os.path.exists(rueck):
        os.remove(rueck)
    _lauf(tool, 'save-as', ziel, rueck, '--fs', fs)
    if not filecmp.cmp(rueck, beleg, shallow=False):
        sys.exit(f'{os.path.basename(ziel)}: die gebaute Diskette weicht vom Beleg '
                 f'{os.path.relpath(beleg, REPO)} ab')


def _bootabbilder_ok(tool, tmp):
    """Die Bootabbilder in disks/bootsektoren/ sind `boot-get` ihrer Belege (Kombi: gebaut aus dem Startabbild)."""
    ok = True
    sys.path.insert(0, os.path.join(REPO, 'tools'))
    import bcu_kombi_bau
    for name, (quelle, spuren) in KOMBI_ABBILDER.items():
        with open(os.path.join(BOOTSEKTOREN, quelle), 'rb') as f:
            soll = bcu_kombi_bau.bauen(f.read(), spuren)
        ist = os.path.join(BOOTSEKTOREN, name)
        if os.path.isfile(ist) and open(ist, 'rb').read() == soll:
            print(f'{name}: ok')
        else:
            print(f'{name}: fehlt oder ist nicht tools/bcu_kombi_bau.py auf {quelle}')
            ok = False
    for name, beleg in BOOTABBILDER.items():
        kopie = os.path.join(tmp, 'beleg_' + name + '.img')
        shutil.copyfile(beleg, kopie)
        aus = os.path.join(tmp, name)
        _lauf(tool, 'boot-get', kopie, aus)
        ist = os.path.join(BOOTSEKTOREN, name)
        if os.path.isfile(ist) and filecmp.cmp(ist, aus, shallow=False):
            print(f'{name}: ok')
        else:
            print(f'{name}: fehlt oder ist nicht boot-get von {os.path.relpath(beleg, REPO)}')
            ok = False
    return ok


def _arbeit_neu(tool, ziel, tmp, fs):
    """Arbeitsdiskette (EIGENBAU; 40 oder 80 Spuren je nach @p fs) mit den Dateien der Beleg-Arbeitsdiskette."""
    beleg = os.path.join(tmp, 'arbeit_beleg.img')
    shutil.copyfile(DISKETTEN['a5120_bcu12_k5600-10_arbeit-sys1.hfe'][0], beleg)
    dateien = os.path.join(tmp, 'arbeit_dateien')
    shutil.rmtree(dateien, ignore_errors=True)
    os.makedirs(dateien)
    _lauf(tool, 'get', beleg, '*', '--to', dateien)
    if os.path.exists(ziel):
        os.remove(ziel)
    _lauf(tool, 'create', ziel, '--fs', fs, '--boot', os.path.join(BOOTSEKTOREN, 'boot_bcu12_ohnesyl.bin'))
    _lauf(tool, 'put', ziel, dateien)


def k52_bloecke(pfad):
    """Bloecke je Seite einer .k52 (core/peripherals/kassette/k52_codec.h, Version 1)."""
    with open(pfad, 'rb') as f:
        d = f.read()
    if d[:4] != b'K52T' or d[4] != 1 or d[5] != 2:
        raise ValueError('keine .k52 der Version 1')
    pos = 4 + 1 + 1 + 2 + 4 + 5 * 4
    seiten = []
    for _ in range(2):
        _ws, _null, anzahl = struct.unpack_from('<BBI', d, pos)
        pos += 6
        bloecke = []
        for _ in range(anzahl):
            _luecke, laenge = struct.unpack_from('<IH', d, pos)
            pos += 6
            bloecke.append(d[pos:pos + laenge])
            pos += laenge
        seiten.append(bloecke)
    if pos != len(d):
        raise ValueError('Reste hinter dem letzten Block')
    return seiten


def kassette_ok(pfad):
    """Formatiert = Seite A genau EIN Block, die Bandmarke AA 00 00 AA; Seite B leer."""
    try:
        a, b = k52_bloecke(pfad)
    except (OSError, ValueError, struct.error) as e:
        print(f'{KASSETTE}: {e}')
        return False
    if a != [bytes([0xAA, 0x00, 0x00, 0xAA])] or b:
        print(f'{KASSETTE}: nicht „eine Bandmarke auf Seite A“ ({len(a)}/{len(b)} Bloecke)')
        return False
    return True


def bauen(tool, wurzel):
    with tempfile.TemporaryDirectory() as tmp:
        for name, (beleg, extra) in DISKETTEN.items():
            ziel = _db.diskpfad(wurzel, name)
            os.makedirs(os.path.dirname(ziel), exist_ok=True)
            _wandeln(tool, beleg, ziel, extra, tmp)
            print(f'{name}: gewandelt')
        if not _bootabbilder_ok(tool, tmp):
            return 1
        for name, (beleg, fs, boot) in GEBAUT.items():
            ziel = _db.diskpfad(wurzel, name)
            os.makedirs(os.path.dirname(ziel), exist_ok=True)
            _bauen(tool, beleg, fs, boot, ziel, tmp)
            print(f'{name}: gebaut aus {os.path.relpath(boot, REPO)}')
        ziel = _db.diskpfad(wurzel, ARBEIT80)
        _arbeit_neu(tool, ziel, tmp, 'bcu880_80')
        print(f'{ARBEIT80}: gebaut (System + Dateien der Arbeitsdiskette)')
        ziel = _db.diskpfad(wurzel, ARBEIT40)
        _arbeit_neu(tool, ziel, tmp, 'bcu880')
        print(f'{ARBEIT40}: gebaut (System + Dateien der Arbeitsdiskette)')
    ziel = _db.diskpfad(wurzel, BOOTBAND)
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    shutil.copyfile(BOOTBAND_QUELLE, ziel)
    print(f'{BOOTBAND}: kopiert')
    k = _db.diskpfad(wurzel, KASSETTE)
    if not os.path.isfile(k):
        print(f'{KASSETTE}: fehlt — erzeugen mit K1520_BEISPIELBAND=… (Kopf dieses Skripts)')
        return 1
    return 0 if kassette_ok(k) else 1


def pruefen(tool, wurzel):
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for name, (beleg, extra) in DISKETTEN.items():
            ist = _db.diskpfad(wurzel, name)
            if not os.path.isfile(ist):
                print(f'{name}: fehlt'); ok = False
                continue
            soll = os.path.join(tmp, name)
            _wandeln(tool, beleg, soll, extra, tmp)
            if filecmp.cmp(ist, soll, shallow=False):
                print(f'{name}: ok')
            else:
                print(f'{name}: ist nicht die Wandlung von {os.path.relpath(beleg, REPO)}'); ok = False
        ok = _bootabbilder_ok(tool, tmp) and ok
        for name, (beleg, fs, boot) in GEBAUT.items():
            ist = _db.diskpfad(wurzel, name)
            if not os.path.isfile(ist):
                print(f'{name}: fehlt'); ok = False
                continue
            soll = os.path.join(tmp, name)
            _bauen(tool, beleg, fs, boot, soll, tmp)
            if filecmp.cmp(ist, soll, shallow=False):
                print(f'{name}: ok')
            else:
                print(f'{name}: ist nicht der Bau aus {os.path.relpath(boot, REPO)}'); ok = False
        for name, fs in ((ARBEIT80, 'bcu880_80'), (ARBEIT40, 'bcu880')):
            ist = _db.diskpfad(wurzel, name)
            soll = os.path.join(tmp, name)
            _arbeit_neu(tool, soll, tmp, fs)
            if os.path.isfile(ist) and filecmp.cmp(ist, soll, shallow=False):
                print(f'{name}: ok')
            else:
                print(f'{name}: fehlt oder weicht vom Bau ab'); ok = False
    band = _db.diskpfad(wurzel, BOOTBAND)
    if os.path.isfile(band) and filecmp.cmp(band, BOOTBAND_QUELLE, shallow=False):
        print(f'{BOOTBAND}: ok')
    else:
        print(f'{BOOTBAND}: fehlt oder weicht von der Fixture ab'); ok = False
    k = _db.diskpfad(wurzel, KASSETTE)
    if not os.path.isfile(k):
        print(f'{KASSETTE}: fehlt'); ok = False
    elif kassette_ok(k):
        print(f'{KASSETTE}: ok')
    else:
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--emulator', action='append', default=[],
                    help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = [*o.emulator, os.path.abspath(o.tool)]
    if o.check:
        return 0 if pruefen(tool, o.out) else 1
    return bauen(tool, o.out)


if __name__ == '__main__':
    sys.exit(main())
