#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
cpm_bau.py  -  gemeinsamer Bau der eigenen CP/M-Programme (.COM)
================================================================

SERTEST, ROMREAD und die A5120.16-Pruefprogramme (EM256ADR, EM16ABL, EM256FUL)
werden gleich gebaut: Quelle mit CRLF nach build/, M80, LINKMT (Ladeadresse
0100H), die fertige .COM neben build/ ablegen — sie ist EINGECHECKT, denn die CI
hat die CP/M-Werkzeugkette nicht.  Die Werkzeugkette (cparun, m80.com,
linkmt.com) kommt aus dem Schwesterprojekt CPA_Workbench (``CPA_TOOLS=<pfad>``).

Jedes Programm hat ein kleines ``build.py``, das :func:`hauptprogramm` mit seiner
Beschreibung ruft.  Gemeinsame Kommandozeile:

  build.py [NAME …]          bauen, Ergebnis nach <ordner>/<name>.com
  build.py clean             build/ leeren
  build.py --out DATEI NAME  in ein temporaeres Verzeichnis bauen, nach DATEI
  build.py --check           alle neu bauen (temporaer) und bytegleich mit den
                             eingecheckten .com vergleichen; Exit 0 gleich,
                             1 verschieden/Fehler, 77 Werkzeugkette fehlt
                             (ctest: uebersprungen)
"""

import filecmp
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

CPA_TOOLS = os.environ.get('CPA_TOOLS',
                           os.path.expanduser('~/projects/CPA_Workbench/tools'))
LOADADDR = '100'
SKIP = 77          # ctest SKIP_RETURN_CODE


class Programm:
    """Ein .COM-Programm: Quelle ``<src>/<name>.mac``, Ergebnis ``<ordner>/<name>.com``.

    ``vorstufe(build_dir)`` (optional) erzeugt vor dem Assemblieren Dateien in
    build/ — etwa eingebettete U8001-Firmware als ``.inc``; Zeilen
    ``;%INCLUDE datei.inc`` der Quelle werden dann durch deren Inhalt ersetzt.
    """

    def __init__(self, name, ordner, vorstufe=None):
        if len(name) > 8:
            raise ValueError(f"'{name}' ist kein CP/M-8.3-Name")
        self.name = name.lower()
        self.ordner = ordner
        self.vorstufe = vorstufe

    @property
    def quelle(self):
        return os.path.join(self.ordner, 'src', f'{self.name}.mac')

    @property
    def eingecheckt(self):
        return os.path.join(self.ordner, f'{self.name}.com')


def log(msg):
    print(msg)


def run(cmd, cwd, timeout=60):
    log(f"  > {' '.join(str(c) for c in cmd)}")
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                           timeout=timeout, errors='replace')
    except FileNotFoundError:
        raise RuntimeError(f"Programm nicht gefunden: {cmd[0]}")
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"Timeout nach {timeout}s: {' '.join(map(str, cmd))}")
    for stream in (r.stdout, r.stderr):
        if stream and stream.strip():
            for line in stream.strip().splitlines():
                log(f"    {line}")
    if r.returncode != 0:
        raise RuntimeError(f"Befehl fehlgeschlagen (exit {r.returncode}): "
                           f"{' '.join(map(str, cmd))}")
    return r


def _cparun_name():
    return 'cparun.exe' if platform.system() == 'Windows' else 'cparun'


def werkzeugkette_da():
    return all(os.path.isfile(os.path.join(CPA_TOOLS, t))
               for t in (_cparun_name(), 'm80.com', 'linkmt.com'))


def _cparun():
    path = os.path.join(CPA_TOOLS, _cparun_name())
    if not os.path.isfile(path):
        raise RuntimeError(f"cparun nicht gefunden: {path}\n"
                           f"CPA_Workbench-Tools per CPA_TOOLS=<pfad> setzen "
                           f"(aktuell: {CPA_TOOLS}).")
    return path


def _klein(build_dir, stem, ext):
    """cparun erzeugt unter Linux klein geschriebene Ausgaben; vereinheitlichen."""
    up = os.path.join(build_dir, f'{stem.upper()}.{ext.upper()}')
    lo = os.path.join(build_dir, f'{stem.lower()}.{ext.lower()}')
    if os.path.isfile(up) and not os.path.isfile(lo):
        os.rename(up, lo)
    return lo


def _includes(text, build_dir):
    def ersetze(m):
        pfad = os.path.join(build_dir, m.group(1))
        if not os.path.isfile(pfad):
            raise RuntimeError(f"Include-Datei nicht gefunden: {pfad}")
        with open(pfad, 'r', encoding='utf-8') as f:
            inhalt = f.read().rstrip('\n')
        log(f"    eingefuegt: {m.group(1)}")
        return inhalt
    return re.sub(r'^;\s*%INCLUDE\s+(\S+)', ersetze, text,
                  flags=re.IGNORECASE | re.MULTILINE)


def baue(p, build_dir, ziel):
    """Programm @p bauen; die .COM landet in @p ziel."""
    log("=" * 52)
    log(f"{p.name}.com  -  M80 + LINKMT")
    log("=" * 52)
    if not os.path.isfile(p.quelle):
        raise RuntimeError(f"Quelldatei nicht gefunden: {p.quelle}")
    os.makedirs(build_dir, exist_ok=True)
    if p.vorstufe:
        log("\n[0] Vorstufe")
        p.vorstufe(build_dir)

    up = p.name.upper()
    with open(p.quelle, 'r', encoding='utf-8', newline='') as f:
        text = f.read()
    text = _includes(text, build_dir)
    # CP/M-Werkzeuge erwarten CRLF
    text = text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n')
    with open(os.path.join(build_dir, f'{up}.MAC'), 'wb') as f:
        f.write(text.encode('ascii', errors='replace'))

    for t in ('m80.com', 'linkmt.com'):
        s = os.path.join(CPA_TOOLS, t)
        if not os.path.isfile(s):
            raise RuntimeError(f"Tool nicht gefunden: {s}")
        shutil.copy2(s, build_dir)
    cparun = _cparun()

    log(f"\n[1] M80: {up}.MAC -> {up}.ERL")
    r = run([cparun, 'm80', f'{up}.ERL={up}'], cwd=build_dir)
    # M80 endet auch bei Fehlern mit 0 und schreibt trotzdem eine .ERL -
    # massgeblich ist die Schlusszeile "No Fatal error(s)".
    if 'No Fatal error' not in (r.stdout or ''):
        raise RuntimeError(f"M80 meldet Fehler (Listing: cparun m80 {up},{up}={up}).")
    if not os.path.isfile(_klein(build_dir, p.name, 'erl')):
        raise RuntimeError("M80 hat keine .ERL erzeugt.")

    log(f"\n[2] LINKMT: {up}.ERL -> {up}.COM (Ladeadresse {LOADADDR}H)")
    run([cparun, 'linkmt', f'{up}={up}/p:{LOADADDR}'], cwd=build_dir)
    com = _klein(build_dir, p.name, 'com')
    if not os.path.isfile(com):
        raise RuntimeError("LINKMT hat keine .COM erzeugt.")

    for pat in ('*.MAC', '*.mac', '*.ERL', '*.erl', '*.PRN', '*.prn', '*.REL',
                '*.rel', '*.SYM', '*.sym', '*.SYP', '*.syp', '*.bin', '*.inc',
                'm80.com', 'linkmt.com'):
        for f in glob.glob(os.path.join(build_dir, pat)):
            if os.path.basename(f).lower() != os.path.basename(com):
                os.remove(f)
    shutil.copyfile(com, ziel)
    log(f"\nFERTIG: {ziel} ({os.path.getsize(ziel)} Bytes)")


def pruefe(programme):
    """Waechter: passen die eingecheckten .com zur Quelle?"""
    if not werkzeugkette_da():
        print(f"UEBERSPRUNGEN: CP/M-Werkzeugkette nicht gefunden ({CPA_TOOLS}; "
              f"CPA_TOOLS=<pfad> setzen) - eingecheckte .com ungeprueft.")
        return SKIP
    fehler = 0
    with tempfile.TemporaryDirectory(prefix='cpm_check_') as tmp:
        for p in programme:
            neu = os.path.join(tmp, f'{p.name}.com')
            baue(p, os.path.join(tmp, 'build', p.name), neu)
            if filecmp.cmp(neu, p.eingecheckt, shallow=False):
                print(f"OK: {p.eingecheckt} passt zur Quelle ({os.path.getsize(neu)} Bytes)")
            else:
                print(f"FEHLER: {p.eingecheckt} passt NICHT zur Quelle "
                      f"(neu {os.path.getsize(neu)} B, eingecheckt "
                      f"{os.path.getsize(p.eingecheckt)} B) - build.py ausfuehren "
                      f"und die .com mit einchecken.", file=sys.stderr)
                fehler += 1
    return 1 if fehler else 0


def hauptprogramm(programme, doc):
    """Kommandozeile eines build.py (siehe Kopf dieser Datei)."""
    namen = {p.name: p for p in programme}
    ordner = programme[0].ordner

    def waehle(liste):
        unbekannt = [n for n in liste if n.lower() not in namen]
        if unbekannt:
            raise RuntimeError(f"unbekannt: {', '.join(unbekannt)} "
                               f"(bekannt: {', '.join(namen)})")
        return [namen[n.lower()] for n in liste] or programme

    try:
        args = sys.argv[1:]
        if args[:1] == ['clean']:
            shutil.rmtree(os.path.join(ordner, 'build'), ignore_errors=True)
        elif args[:1] == ['--check']:
            return pruefe(waehle(args[1:]))
        elif args[:1] == ['--out'] and len(args) in (2, 3):
            p = waehle(args[2:])[0]
            with tempfile.TemporaryDirectory(prefix='cpm_build_') as tmp:
                baue(p, os.path.join(tmp, 'build'), os.path.abspath(args[1]))
        elif any(a.startswith('-') for a in args):
            print(doc)
            return 2
        else:
            for p in waehle(args):
                baue(p, os.path.join(ordner, 'build'), p.eingecheckt)
        return 0
    except RuntimeError as e:
        print(f"\nFEHLER: {e}", file=sys.stderr)
        return 1
