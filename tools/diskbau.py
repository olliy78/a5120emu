#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
diskbau.py  -  gemeinsamer Baustein der Diskettenbauskripte (SCP/SCPX/CP/Z/SCP 3.0)
===================================================================================

Jede ausgelieferte Systemdiskette hat ein eigenes `tools/<ordner>/build.py` (Vorbild: tools/cpa_a5120/), das
nur BESCHREIBT, was auf die Diskette gehoert (`soll()`), und alles Uebrige hier erledigt:

  * `main(...)`    Kommandozeile (`--tool`, `--out`, `--check`)
  * bauen          create (mit Systemspuren) -> erste Dateien einzeln -> Rest -> `check --full` ohne Befund
  * pruefen        Wächter: Dateiliste, Inhalt, Systemspuren der Diskette in disks/ == Beschreibung
  * Quellen        eigener Ordner `inhalt/` , gemeinsame Programme `tools/scp_gemeinsam/`, eigene Beigaben aus ihren
                   Quellordnern (tools/disketten_beigaben.PROGRAMME: SERTEST, EM256*, RAF*, LBREAD/LBPUNCH …)
  * Texte          LIESMICH.TXT (LF in der Quelle; auf der Diskette CRLF + ^Z, auf volle Saetze aufgefuellt) und die
                   deutsche WM.HLP (aus tools/cpa_a5120/quellen/WM_HLP_de.txt)

Beschreibung einer Diskette:

    import diskbau as db
    DISK = db.Diskette(__file__, 'a5120_scpx17_k5601_system.hfe', fs='scpx798',
                       boot='bootabbild.bin',            # Systemspuren (None = keine)
                       create_args=[],                   # z. B. ['--prg', '710']
                       erste=[])                         # Dateien, die zuerst eingespielt werden muessen
    def soll(): ...                                      # {Diskettenname: Bytes}
    DISK.main(soll)
"""

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
GEMEINSAM = os.path.join(TOOLS, 'scp_gemeinsam')

sys.path.insert(0, TOOLS)
import disketten_beigaben as _beigaben  # noqa: E402

# Hilfsfunktionen (CP/M-Text, deutsche WM.HLP) liegen im A5120-CP/A-Bau
_spec = importlib.util.spec_from_file_location('cpa_a5120_build', os.path.join(TOOLS, 'cpa_a5120', 'build.py'))
_cpa = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cpa)


def lies(*teile):
    with open(os.path.join(*teile), 'rb') as f:
        return f.read()


def ordner(pfad, ausser=()):
    """{Name: Bytes} aller Dateien eines Ordners (ohne die in `ausser`)."""
    return {n: lies(pfad, n) for n in sorted(os.listdir(pfad)) if n not in ausser}


def gemeinsam(*namen):
    """Gemeinsame Programme aus tools/scp_gemeinsam/."""
    return {n: lies(GEMEINSAM, n) for n in namen}


def beigaben(*namen):
    """Eigene Programme aus ihren Quellordnern (disketten_beigaben.PROGRAMME)."""
    return {n: lies(_beigaben.PROGRAMME[n]) for n in namen}


def aus_fixture(pfad, *namen):
    """Dateien aus einer eingecheckten Datei (z. B. tests/fixtures/raf/RAFTEST.COM) unter dem Diskettennamen."""
    return {n: lies(REPO, pfad, n) for n in namen}


def raftest():
    """RAFTEST.COM mit der Kartenadresse 88H/89H des A5120 (Patchbereich 22H/23H, wie test_raf_zwg.cpp)."""
    return _cpa._raftest()


def liesmich(daten):
    return _cpa._cpm_text(daten)


def wm_hlp():
    return _cpa._wm_hlp()


class Diskette:
    def __init__(self, skript, name, fs, boot=None, create_args=(), erste=()):
        self.hier = os.path.dirname(os.path.abspath(skript))
        self.name, self.fs, self.create_args, self.erste = name, fs, list(create_args), list(erste)
        self.boot = os.path.join(self.hier, boot) if boot else None

    # -- Werkzeug ---------------------------------------------------------------------------------------------
    @staticmethod
    def _lauf(tool, *args):
        r = subprocess.run([*tool, *args], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"Fehler: {' '.join([os.path.basename(tool[-1]), *args])}\n{r.stdout}{r.stderr}")
        return r.stdout

    # -- bauen ------------------------------------------------------------------------------------------------
    def bauen(self, tool, ausgabe, soll):
        ziel = _beigaben.diskpfad(ausgabe, self.name)
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
        if os.path.exists(ziel):
            os.remove(ziel)
        with tempfile.TemporaryDirectory() as tmp:
            quelle = os.path.join(tmp, 'q')
            os.makedirs(quelle)
            inhalt = soll()
            for n, d in inhalt.items():
                with open(os.path.join(quelle, n), 'wb') as f:
                    f.write(d)
            args = ['create', ziel, '--fs', self.fs, *self.create_args]
            if self.boot:
                args += ['--boot', self.boot]
            self._lauf(tool, *args)
            for n in self.erste:
                self._lauf(tool, 'put', ziel, os.path.join(quelle, n), '--no-backup')
                os.remove(os.path.join(quelle, n))
            self._lauf(tool, 'put', ziel, quelle, '--no-backup')
        pruefung = self._lauf(tool, 'check', ziel, '--full')
        if 'ohne Befund' not in pruefung:
            sys.exit(f'{self.name}: Dateisystempruefung hat Befunde:\n{pruefung}')
        print(f'{self.name}: gebaut, check --full ohne Befund')

    # -- pruefen (Waechter) -----------------------------------------------------------------------------------
    def pruefen(self, tool, ausgabe, soll):
        diskette = _beigaben.diskpfad(ausgabe, self.name)
        if not os.path.isfile(diskette):
            print(f'{self.name}: fehlt')
            return False
        erwartet = soll()
        ok = True
        with tempfile.TemporaryDirectory() as tmp:
            ist_ordner = os.path.join(tmp, 'ist')
            os.makedirs(ist_ordner)
            self._lauf(tool, 'get', diskette, '*', '--to', ist_ordner)
            ist = {n: lies(ist_ordner, n) for n in os.listdir(ist_ordner) if n != 'cpm-dateiangaben.txt'}
            boot_ok = True
            if self.boot:
                bf = os.path.join(tmp, 'boot.bin')
                self._lauf(tool, 'boot-get', diskette, bf)
                boot_ok = lies(bf) == lies(self.boot)
        for n in sorted(set(erwartet) | set(ist)):
            if n not in ist:
                print(f'{self.name}: {n} fehlt auf der Diskette'); ok = False
            elif n not in erwartet:
                print(f'{self.name}: {n} steht auf der Diskette, aber nicht im Ordner'); ok = False
            elif erwartet[n] != ist[n]:
                print(f'{self.name}: {n} weicht ab'); ok = False
        if not boot_ok:
            print(f'{self.name}: Systemspuren weichen ab'); ok = False
        if ok:
            print(f'{self.name}: ok ({len(erwartet)} Dateien)')
        return ok

    # -- Kommandozeile ----------------------------------------------------------------------------------------
    def main(self, soll, beschreibung=''):
        ap = argparse.ArgumentParser(description=beschreibung or self.name)
        ap.add_argument('--tool', required=True, help='k1520disktool')
        ap.add_argument('--emulator', action='append', default=[],
                        help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
        ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
        ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
        o = ap.parse_args()
        tool = [*o.emulator, os.path.abspath(o.tool)]
        if o.check:
            sys.exit(0 if self.pruefen(tool, o.out, soll) else 1)
        self.bauen(tool, o.out, soll)


def main_mehrere(paare, beschreibung=''):
    """Eine build.py, die mehrere Disketten baut: paare = [(Diskette, soll), ...]."""
    ap = argparse.ArgumentParser(description=beschreibung)
    ap.add_argument('--tool', required=True, help='k1520disktool')
    ap.add_argument('--emulator', action='append', default=[],
                    help='davor zu setzender Starter (Cross-Bau: wine; wiederholbar)')
    ap.add_argument('--out', default=os.path.join(REPO, 'disks'), help='Zielordner (Vorgabe: disks/)')
    ap.add_argument('--check', action='store_true', help='nur vergleichen, nichts schreiben')
    o = ap.parse_args()
    tool = [*o.emulator, os.path.abspath(o.tool)]
    if o.check:
        ok = [d.pruefen(tool, o.out, soll) for d, soll in paare]
        sys.exit(0 if all(ok) else 1)
    for d, soll in paare:
        d.bauen(tool, o.out, soll)
