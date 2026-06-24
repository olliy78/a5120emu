#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
build.py  -  Build-Skript fuer romread.com (A5120 Boot-EPROM Dumper)
====================================================================

Assembliert tools/romread/src/romread.mac mit dem CP/M-Assembler M80 und
linkt es mit LINKMT (Ladeadresse 0100H) zu romread.com. Beide CP/M-Tools
laufen ueber den Emulator `cparun` aus dem CPA_Workbench-Projekt.

Voraussetzungen (aus CPA_Workbench/tools/):
  - cparun (bzw. cparun.exe)
  - m80.com, linkmt.com

Aufruf:
  python3 tools/romread/build.py            # baut build/romread.com
  python3 tools/romread/build.py clean      # leert build/

Ergebnis:
  tools/romread/build/romread.com
"""

import glob
import os
import platform
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))           # tools/romread/
SRC_DIR    = os.path.join(SCRIPT_DIR, 'src')
BUILD_DIR  = os.path.join(SCRIPT_DIR, 'build')

# CP/M-Toolchain liegt im Schwesterprojekt CPA_Workbench/tools/.
# Ueber Umgebungsvariable CPA_TOOLS ueberschreibbar.
DEFAULT_CPA_TOOLS = os.path.expanduser('~/projects/CPA_Workbench/tools')
CPA_TOOLS = os.environ.get('CPA_TOOLS', DEFAULT_CPA_TOOLS)

SOURCE   = 'romread'          # 8.3-Basisname
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


def fix_case(stem, ext):
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


def main():
    src_up = SOURCE.upper()
    src_mac = os.path.join(SRC_DIR, f'{SOURCE}.mac')
    if not os.path.isfile(src_mac):
        raise RuntimeError(f"Quelldatei nicht gefunden: {src_mac}")

    log("=" * 52)
    log("romread Build  -  A5120 Boot-EPROM Dumper")
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
    run([cparun, 'm80', f'{src_up}.ERL={src_up}'], cwd=BUILD_DIR)
    erl = fix_case(SOURCE, 'erl')
    if not os.path.isfile(erl):
        raise RuntimeError("M80 hat keine .ERL erzeugt (Assembler-Ausgabe pruefen).")

    # 5) Linken: LINKMT  NAME = NAME / p:100
    log(f"\n[4] LINKMT: {src_up}.ERL -> {src_up}.COM (Ladeadresse 0x{LOADADDR})")
    run([cparun, 'linkmt', f'{src_up}={src_up}/p:{LOADADDR}'], cwd=BUILD_DIR)
    com = fix_case(SOURCE, 'com')
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

    size = os.path.getsize(com)
    log("\n" + "=" * 52)
    log(f"FERTIG: {os.path.basename(com)} ({size} Bytes)")
    log(f"Pfad:   {com}")
    log("=" * 52)
    log("\nAuf eine CP/A-Diskette kopieren und auf dem A5120 'romread' starten.")


if __name__ == '__main__':
    try:
        if len(sys.argv) > 1 and sys.argv[1] == 'clean':
            clean()
        else:
            main()
    except RuntimeError as e:
        print(f"\nFEHLER: {e}", file=sys.stderr)
        sys.exit(1)
