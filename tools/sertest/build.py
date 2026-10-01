#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Build-Skript fuer sertest.com (Serial Test, A5120/K8915)
=====================================================================

Assembliert tools/sertest/src/sertest.mac mit dem CP/M-Assembler M80 und
linkt es mit LINKMT (Ladeadresse 0100H) zu sertest.com. Beide CP/M-Tools
laufen ueber den Emulator `cparun` aus dem CPA_Workbench-Projekt.

Voraussetzungen (aus CPA_Workbench/tools/):
  - cparun (bzw. cparun.exe)
  - m80.com, linkmt.com

Aufruf:
  python3 tools/sertest/build.py            # baut build/sertest.com und
                                           # kopiert es nach tools/sertest/
  python3 tools/sertest/build.py clean      # leert build/
  python3 tools/sertest/build.py --out <datei>   # in ein temporaeres Verzeichnis
                                           # bauen, Ergebnis nach <datei>
  python3 tools/sertest/build.py --check    # neu bauen (temporaer) und mit der
                                           # eingecheckten .com bytegleich vergleichen;
                                           # Exit 0 gleich, 1 verschieden/Fehler,
                                           # 77 Werkzeugkette fehlt (= uebersprungen)

Ergebnis:
  tools/sertest/sertest.com (eingecheckt - die CI hat die CPA_Workbench nicht)
"""

import glob
import filecmp
import tempfile
import os
import platform
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))           # tools/sertest/
SRC_DIR    = os.path.join(SCRIPT_DIR, 'src')
BUILD_DIR  = os.path.join(SCRIPT_DIR, 'build')

# CP/M-Toolchain liegt im Schwesterprojekt CPA_Workbench/tools/.
# Ueber Umgebungsvariable CPA_TOOLS ueberschreibbar.
DEFAULT_CPA_TOOLS = os.path.expanduser('~/projects/CPA_Workbench/tools')
CPA_TOOLS = os.environ.get('CPA_TOOLS', DEFAULT_CPA_TOOLS)

SOURCE   = 'sertest'          # 8.3-Basisname
LOADADDR = '100'             # CP/M .COM-Ladeadresse


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
        raise RuntimeError(f"Befehl fehlgeschlagen (exit {r.returncode}): {' '.join(map(str, cmd))}")
    return r


def find_cparun():
    name = 'cparun.exe' if platform.system() == 'Windows' else 'cparun'
    path = os.path.join(CPA_TOOLS, name)
    if not os.path.isfile(path):
        raise RuntimeError(
            f"cparun nicht gefunden: {path}\n"
            f"CPA_Workbench-Tools per CPA_TOOLS=<pfad> setzen (aktuell: {CPA_TOOLS}).")
    return path


def fix_case(BUILD_DIR, stem, ext):
    """cparun erzeugt unter Linux klein geschriebene Ausgaben; vereinheitlichen."""
    up = os.path.join(BUILD_DIR, f'{stem.upper()}.{ext.upper()}')
    lo = os.path.join(BUILD_DIR, f'{stem.lower()}.{ext.lower()}')
    if os.path.isfile(up) and not os.path.isfile(lo):
        os.rename(up, lo)
    return lo


def clean():
    if os.path.isdir(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
        log(f"    Geloescht: {BUILD_DIR}")
    else:
        log("    Bereits leer.")


def main(BUILD_DIR=BUILD_DIR, ziel=None):
    src_up = SOURCE.upper()
    src_mac = os.path.join(SRC_DIR, f'{SOURCE}.mac')
    if not os.path.isfile(src_mac):
        raise RuntimeError(f"Quelldatei nicht gefunden: {src_mac}")

    log("=" * 52)
    log("sertest Build  -  Serial Test (A5120/K8915)")
    log("=" * 52)

    # 1) build/-Verzeichnis
    os.makedirs(BUILD_DIR, exist_ok=True)

    # 2) Quelle mit CRLF nach build/ kopieren (CP/M-Tools erwarten CRLF)
    with open(src_mac, 'r', encoding='utf-8', newline='') as f:
        content = f.read()
    content = content.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n')
    dst_mac = os.path.join(BUILD_DIR, f'{src_up}.MAC')
    with open(dst_mac, 'wb') as f:
        f.write(content.encode('ascii', errors='replace'))
    log(f"\n[1] Quelle kopiert: {src_up}.MAC ({os.path.getsize(dst_mac)} Bytes)")

    # 3) CP/M-Tools nach build/
    log("\n[2] Tools kopieren")
    for tool in ('m80.com', 'linkmt.com'):
        s = os.path.join(CPA_TOOLS, tool)
        if not os.path.isfile(s):
            raise RuntimeError(f"Tool nicht gefunden: {s}")
        shutil.copy2(s, BUILD_DIR)
        log(f"    {tool}")

    cparun = find_cparun()

    # 4) Assemblieren: M80  NAME.ERL = NAME
    log(f"\n[3] M80: {src_up}.MAC -> {src_up}.ERL")
    r = run([cparun, 'm80', f'{src_up}.ERL={src_up}'], cwd=BUILD_DIR)
    # M80 endet auch bei Fehlern mit 0 und schreibt trotzdem eine .ERL -
    # massgeblich ist die Schlusszeile "No Fatal error(s)".
    if 'No Fatal error' not in (r.stdout or ''):
        raise RuntimeError("M80 meldet Fehler (Zeilen mit Kennbuchstaben in der "
                           "Ausgabe oben; Listing: cparun m80 SERTEST,SERTEST=SERTEST).")
    erl = fix_case(BUILD_DIR, SOURCE, 'erl')
    if not os.path.isfile(erl):
        raise RuntimeError("M80 hat keine .ERL erzeugt (Assembler-Ausgabe pruefen).")

    # 5) Linken: LINKMT  NAME = NAME / p:100
    log(f"\n[4] LINKMT: {src_up}.ERL -> {src_up}.COM (Ladeadresse 0x{LOADADDR})")
    run([cparun, 'linkmt', f'{src_up}={src_up}/p:{LOADADDR}'], cwd=BUILD_DIR)
    com = fix_case(BUILD_DIR, SOURCE, 'com')
    if not os.path.isfile(com):
        raise RuntimeError("LINKMT hat keine .COM erzeugt (Ausgabe pruefen).")

    # 6) Aufraeumen (alles ausser der .com)
    log("\n[5] Aufraeumen")
    keep = os.path.basename(com).lower()
    for pat in ('*.MAC', '*.mac', '*.ERL', '*.erl', '*.PRN', '*.prn',
                '*.REL', '*.rel', '*.SYM', '*.sym', '*.SYP', '*.syp',
                'm80.com', 'linkmt.com'):
        for f in glob.glob(os.path.join(BUILD_DIR, pat)):
            if os.path.basename(f).lower() != keep:
                os.remove(f)

    # 7) Eingecheckte Fassung neben build/ aktualisieren (bzw. --out)
    if ziel is None:
        ziel = os.path.join(SCRIPT_DIR, f'{SOURCE}.com')
    shutil.copyfile(com, ziel)

    size = os.path.getsize(com)
    log("\n" + "=" * 52)
    log(f"FERTIG: {os.path.basename(com)} ({size} Bytes)")
    log(f"Pfad:   {ziel}")
    log("=" * 52)
    log("\nAuf eine CP/A- bzw. SCPX-Diskette kopieren und 'SERTEST' starten.")


EINGECHECKT = os.path.join(SCRIPT_DIR, f'{SOURCE}.com')
SKIP = 77          # ctest SKIP_RETURN_CODE


def werkzeugkette_da():
    name = 'cparun.exe' if platform.system() == 'Windows' else 'cparun'
    return all(os.path.isfile(os.path.join(CPA_TOOLS, t))
               for t in (name, 'm80.com', 'linkmt.com'))


def check():
    """Waechter: passt die eingecheckte .com zur Quelle?  Baut in ein
    temporaeres Verzeichnis, die eingecheckte Datei bleibt unberuehrt."""
    if not werkzeugkette_da():
        print(f"UEBERSPRUNGEN: CP/M-Werkzeugkette nicht gefunden ({CPA_TOOLS}; "
              f"CPA_TOOLS=<pfad> setzen) - eingecheckte sertest.com ungeprueft.")
        return SKIP
    with tempfile.TemporaryDirectory(prefix='sertest_check_') as tmp:
        neu = os.path.join(tmp, 'sertest.com')
        main(os.path.join(tmp, 'build'), neu)
        if filecmp.cmp(neu, EINGECHECKT, shallow=False):
            print(f"OK: {EINGECHECKT} passt zur Quelle ({os.path.getsize(neu)} Bytes)")
            return 0
        print(f"FEHLER: {EINGECHECKT} passt NICHT zur Quelle src/sertest.mac "
              f"(neu {os.path.getsize(neu)} B, eingecheckt "
              f"{os.path.getsize(EINGECHECKT)} B) - 'python3 tools/sertest/build.py' "
              f"ausfuehren und die .com mit einchecken.", file=sys.stderr)
        return 1


if __name__ == '__main__':
    try:
        args = sys.argv[1:]
        if args[:1] == ['clean']:
            clean()
        elif args[:1] == ['--check']:
            sys.exit(check())
        elif args[:1] == ['--out'] and len(args) == 2:
            with tempfile.TemporaryDirectory(prefix='sertest_build_') as tmp:
                main(os.path.join(tmp, 'build'), os.path.abspath(args[1]))
        elif args:
            print(__doc__)
            sys.exit(2)
        else:
            main()
    except RuntimeError as e:
        print(f"\nFEHLER: {e}", file=sys.stderr)
        sys.exit(1)
