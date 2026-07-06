#!/usr/bin/env python3
# Copyright (c) 2026 Olaf Krieger
# SPDX-License-Identifier: MIT
"""
build_bootsec.py  --  Build-Skript fuer den CPA780 SYL-Bootlader (3-Datei-Version)
===================================================================================

Baut den Bootlader aus drei separaten Assembler-Quellen mithilfe des
CP/M-Assemblers M80 (ueber cparun) und erzeugt die fertige bootsec.bin
im SYL-Mixed-Geometry-Format (15104 Bytes).

Quelldateien:
  src/bootsec_syl.mac     0000H-00FFH  SYL-Header + Bootloader
  src/mini_bdos_asm.mac   0100H-0CFFH  Mini-BDOS (voll kommentiert)
  src/boot_bios.mac       0D00H-19FFH  Boot-BIOS / Stage-2

Arbeitsweise
------------
1. Ergebnisverzeichnis bootsec/build/ anlegen (falls noetig)
2. Quelldateien nach build/ kopieren (CRLF-Konvertierung, CP/M 8.3-Namen)
3. M80-Tool (m80.com) aus tools/ nach build/ kopieren
4. Alle drei Dateien mit M80 assemblieren (PRN-Listings erzeugen)
5. PRN-Listings parsen: Binaerdaten extrahieren und zusammenfuehren
6. bootsec.bin konstruieren (4-Track-Format, 15104 Bytes)
7. Nach prebuilt/bc_a5120/bootsec.bin kopieren (uerschreiben)
8. Temporaere Dateien aufraeumen

Voraussetzungen
---------------
- tools/cparun (Linux) oder tools/cparun.exe (Windows)
- tools/m80.com
- Python 3.6+

Aufruf
------
  Standalone:
    cd /pfad/zu/CPA_Workbench
        python3 tools/bootsec/build_bootsec.py

  Via cpa_builder.py (benutzerdefinierte Pfade):
        python3 tools/bootsec/build_bootsec.py --build-dir build/

Das fertige bootsec.bin liegt danach im jeweiligen Build-Verzeichnis.
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys

# ---------------------------------------------------------------------------
# Pfade (relativ zum CPA_Workbench-Root-Verzeichnis)
# ---------------------------------------------------------------------------
SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))  # CPA_Workbench/
SRC_DIR     = os.path.join(SCRIPT_DIR, 'src')       # bootsec/src/
BUILD_DIR   = os.path.join(SCRIPT_DIR, 'build')     # bootsec/build/
TOOLS_DIR   = os.path.join(PROJECT_DIR, 'tools')    # tools/
PREBUILT    = os.path.join(PROJECT_DIR, 'prebuilt', 'bc_a5120', 'bootsec.bin')

# Quelldateien: (lokaler Name, CP/M 8.3 Name)
SOURCES = [
    ('bootsec_syl.mac',   'BOOTSYL'),
    ('mini_bdos_asm.mac', 'MINIBDOS'),
    ('boot_bios.mac',     'BOOTBIOS'),
]

# Track-Konstanten
TRACK_SYS_SIZE   = 3328         # Systemspur: 26 Sektoren x 128 Bytes
TRACK_DATA_SIZE  = 5120         # Datenspur:   5 Sektoren x 1024 Bytes
FILL_BYTE        = 0x53         # 'S' -- SYL-Fuellbyte
TOTAL_SIZE       = 15104        # 3 x 3328 + 1 x 5120

# RAM-Bereiche fuer Code
TRACK0_RAM_START = 0x0000
TRACK0_RAM_END   = 0x0D00       # exklusiv
TRACK2_RAM_START = 0x0D00
TRACK2_RAM_END   = 0x1A00       # exklusiv


# ---------------------------------------------------------------------------
def log(msg):
    """Nachricht auf stdout ausgeben."""
    print(msg)


def run(cmd, cwd=None, check=True, timeout=60):
    """Externen Befehl ausfuehren und Ausgabe anzeigen."""
    cwd = cwd or BUILD_DIR
    log(f"  > {' '.join(str(c) for c in cmd)}")
    try:
        result = subprocess.run(
            cmd, cwd=cwd,
            capture_output=True, text=True,
            timeout=timeout, errors='replace'
        )
        if result.stdout.strip():
            for line in result.stdout.strip().splitlines():
                log(f"    {line}")
        if result.stderr.strip():
            for line in result.stderr.strip().splitlines():
                log(f"    {line}")
        if check and result.returncode != 0:
            raise RuntimeError(
                f"Befehl fehlgeschlagen (exit {result.returncode}): {' '.join(cmd)}"
            )
        return result
    except FileNotFoundError:
        raise RuntimeError(f"Programm nicht gefunden: {cmd[0]}")
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"Timeout nach {timeout}s: {' '.join(cmd)}")


def convert_to_crlf(src_path, dst_path):
    """Quelldatei kopieren und Zeilenenden zu CRLF konvertieren (M80 erwartet CRLF)."""
    with open(src_path, 'r', encoding='utf-8', newline='') as f:
        content = f.read()
    content = content.replace('\r\n', '\n').replace('\r', '\n')
    content = content.replace('\n', '\r\n')
    with open(dst_path, 'wb') as f:
        f.write(content.encode('ascii', errors='replace'))
    log(f"    Kopiert (CRLF): {os.path.basename(src_path)} -> {os.path.basename(dst_path)}")


def find_cparun():
    """cparun-Pfad plattformabhaengig ermitteln."""
    name = 'cparun.exe' if platform.system() == 'Windows' else 'cparun'
    path = os.path.join(TOOLS_DIR, name)
    if not os.path.isfile(path):
        raise RuntimeError(
            f"cparun nicht gefunden: {path}\n"
            "Bitte sicherstellen, dass das tools/-Verzeichnis vollstaendig ist."
        )
    return path


def parse_prn_listing(prn_path):
    """M80-Listing (.PRN) parsen und Binaerdaten extrahieren.

    Das PRN-Format von M80 (MACRO-80 V3.50) hat pro Zeile:
      __AAAA'  BB BB BB BB  <tab><quellentext>
    wobei AAAA die Adresse (hex), ' der Segment-Marker (ASEG) und
    BB die assemblierten Bytes sind.

    Gibt ein dict {adresse: byte_value} zurueck.
    """
    memory = {}
    addr_re = re.compile(r'^\s{2}([0-9A-Fa-f]{4})[\'` ]?')

    with open(prn_path, 'r', errors='replace') as f:
        for line in f:
            m = addr_re.match(line)
            if m:
                addr = int(m.group(1), 16)
                # Hex-Daten aus fixem Spaltenbereich (Cols 8-23)
                hex_area = line[8:24] if len(line) > 8 else ''
                byte_vals = re.findall(r'[0-9A-Fa-f]{4}|[0-9A-Fa-f]{2}', hex_area)
                for bv in byte_vals:
                    v = int(bv, 16)
                    if len(bv) <= 2:
                        memory[addr] = v
                        addr += 1
                    else:
                        # 16-Bit-Wert: Little-Endian speichern
                        memory[addr] = v & 0xFF
                        memory[addr + 1] = (v >> 8) & 0xFF
                        addr += 2

    return memory


def memory_to_binary(memory, start, end):
    """Speicherabbild in Bytearray umwandeln.

    Fehlende Adressen werden mit 0x00 gefuellt.
    """
    result = bytearray(end - start)
    for addr in range(start, end):
        if addr in memory:
            result[addr - start] = memory[addr]
    return bytes(result)


def build_bootsec_bin(track0_data, track2_data):
    """Zusammensetzen der 4-Track bootsec.bin."""
    assert len(track0_data) == TRACK_SYS_SIZE, \
        f"Track 0 Groesse falsch: {len(track0_data)} != {TRACK_SYS_SIZE}"
    assert len(track2_data) == TRACK_SYS_SIZE, \
        f"Track 2 Groesse falsch: {len(track2_data)} != {TRACK_SYS_SIZE}"

    track1_fill = bytes([FILL_BYTE] * TRACK_SYS_SIZE)
    track3_fill = bytes([FILL_BYTE] * TRACK_DATA_SIZE)

    result = track0_data + track1_fill + track2_data + track3_fill
    assert len(result) == TOTAL_SIZE, \
        f"Gesamtgroesse falsch: {len(result)} != {TOTAL_SIZE}"
    return result


def find_prn_file(cpm_name):
    """PRN-Datei finden (cparun erzeugt lowercase auf Linux)."""
    for name in [cpm_name + '.PRN', cpm_name.lower() + '.prn',
                 cpm_name + '.prn', cpm_name.lower() + '.PRN']:
        p = os.path.join(BUILD_DIR, name)
        if os.path.isfile(p):
            return p
    return None


def assemble_file(cparun, cpm_name):
    """Eine Quelldatei mit M80 assemblieren und PRN-Pfad zurueckgeben."""
    log(f"\n    Assembliere {cpm_name}.MAC ...")

    # M80 Aufruf: objfile,lstfile=source
    result = run(
        [cparun, 'm80',
         f'{cpm_name},{cpm_name}={cpm_name}'],
        cwd=BUILD_DIR, check=False
    )

    prn_path = find_prn_file(cpm_name)

    if not prn_path:
        # Alternativer Aufruf
        log(f"    PRN nicht gefunden, versuche alternativen Aufruf...")
        result = run(
            [cparun, 'm80',
             f'={cpm_name}/L'],
            cwd=BUILD_DIR, check=False
        )
        prn_path = find_prn_file(cpm_name)

    if not prn_path:
        raise RuntimeError(
            f"M80 hat kein PRN-Listing fuer {cpm_name} erzeugt.\n"
            "Vorhandene Dateien: " +
            ", ".join(os.listdir(BUILD_DIR))
        )

    log(f"    Listing: {os.path.basename(prn_path)}")
    return prn_path


def main():
    log("=" * 60)
    log("CPA780 SYL-Bootlader  --  Build-Skript (3-Datei-Version)")
    log("=" * 60)

    # --- Schritt 1: Build-Verzeichnis anlegen ---
    log("\n[STEP 1] Build-Verzeichnis anlegen")
    os.makedirs(BUILD_DIR, exist_ok=True)
    log(f"    {BUILD_DIR}")

    # --- Schritt 2: Quelldateien kopieren ---
    log("\n[STEP 2] Quelldateien nach build/ kopieren (CRLF, CP/M 8.3-Namen)")
    for src_name, cpm_name in SOURCES:
        src_path = os.path.join(SRC_DIR, src_name)
        dst_path = os.path.join(BUILD_DIR, cpm_name + '.MAC')
        if not os.path.isfile(src_path):
            raise RuntimeError(f"Quelldatei nicht gefunden: {src_path}")
        convert_to_crlf(src_path, dst_path)

    # --- Schritt 3: M80 kopieren ---
    log("\n[STEP 3] M80 nach build/ kopieren")
    m80_src = os.path.join(TOOLS_DIR, 'm80.com')
    if not os.path.isfile(m80_src):
        raise RuntimeError(f"M80 nicht gefunden: {m80_src}")
    shutil.copy2(m80_src, BUILD_DIR)
    log(f"    Kopiert: m80.com")

    # --- Schritt 4: Alle drei Dateien assemblieren ---
    cparun = find_cparun()
    log(f"\n[STEP 4] Assemblieren mit M80 (3 Quelldateien)")

    memory = {}
    for src_name, cpm_name in SOURCES:
        prn_path = assemble_file(cparun, cpm_name)

        # PRN parsen und in gemeinsames Memory-Image einfuegen
        file_memory = parse_prn_listing(prn_path)

        if not file_memory:
            raise RuntimeError(
                f"Keine Binaerdaten im PRN-Listing fuer {cpm_name}!"
            )

        min_addr = min(file_memory.keys())
        max_addr = max(file_memory.keys())
        log(f"    {cpm_name}: {min_addr:04X}H-{max_addr:04X}H "
            f"({len(file_memory)} Bytes)")

        # Pruefung auf Ueberlappungen
        overlap = set(memory.keys()) & set(file_memory.keys())
        if overlap:
            log(f"    WARNUNG: {len(overlap)} Adress-Ueberlappungen!")
            for addr in sorted(overlap)[:5]:
                log(f"      {addr:04X}H: alt={memory[addr]:02X}H "
                    f"neu={file_memory[addr]:02X}H")

        memory.update(file_memory)

    # --- Schritt 5: Gesamtstatistik ---
    log(f"\n[STEP 5] Gesamtes Speicherabbild")
    min_addr = min(memory.keys())
    max_addr = max(memory.keys())
    log(f"    Adressbereich: {min_addr:04X}H - {max_addr:04X}H")
    log(f"    Bytes gesamt: {len(memory)}")

    expected_bytes = (TRACK0_RAM_END - TRACK0_RAM_START) + \
                     (TRACK2_RAM_END - TRACK2_RAM_START)
    if len(memory) < expected_bytes * 0.95:
        log(f"    WARNUNG: Nur {len(memory)} von {expected_bytes} erwarteten Bytes!")

    # Extrahiere Track-Daten
    track0_data = memory_to_binary(memory, TRACK0_RAM_START, TRACK0_RAM_END)
    track2_data = memory_to_binary(memory, TRACK2_RAM_START, TRACK2_RAM_END)

    # --- Schritt 6: bootsec.bin zusammensetzen ---
    log(f"\n[STEP 6] bootsec.bin zusammensetzen (4-Track-Format)")
    log(f"    Track 0: {len(track0_data)} Bytes (Code 0000H-0CFFH)")
    log(f"    Track 1: {TRACK_SYS_SIZE} Bytes (Fuellung 53H)")
    log(f"    Track 2: {len(track2_data)} Bytes (Code 0D00H-19FFH)")
    log(f"    Track 3: {TRACK_DATA_SIZE} Bytes (Fuellung 53H)")

    bootsec = build_bootsec_bin(track0_data, track2_data)
    output_path = os.path.join(BUILD_DIR, 'bootsec.bin')
    with open(output_path, 'wb') as f:
        f.write(bootsec)
    log(f"    Geschrieben: {output_path} ({len(bootsec)} Bytes)")

    # --- Schritt 7: Nach prebuilt kopieren ---
    log("\n[STEP 7] Kopiere nach prebuilt/bc_a5120/bootsec.bin")
    os.makedirs(os.path.dirname(PREBUILT), exist_ok=True)
    shutil.copy2(output_path, PREBUILT)
    log(f"    Ueberschrieben: {PREBUILT}")

    # --- Schritt 8: Aufraeumen ---
    log("\n[STEP 8] Temporaere Dateien loeschen")
    # Nur die eigenen Dateien loeschen (nicht fremde im gemeinsamen build/)
    cleanup_names = []
    for _, cpm_name in SOURCES:
        for ext in ['.MAC', '.mac', '.REL', '.rel', '.PRN', '.prn', '.SYM', '.sym']:
            cleanup_names.append(cpm_name + ext)
            cleanup_names.append(cpm_name.lower() + ext)
    cleanup_names.extend(['m80.com', 'M80.COM'])
    for fname in sorted(set(cleanup_names)):
        p = os.path.join(BUILD_DIR, fname)
        if os.path.isfile(p):
            os.remove(p)
            log(f"    Geloescht: {fname}")

    # --- Fertig ---
    log("\n" + "=" * 60)
    log("FERTIG: bootsec.bin erzeugt und prebuilt/bc_a5120 aktualisiert")
    log(f"Pfad:   {output_path}")
    log(f"Groesse: {len(bootsec)} Bytes")
    log("=" * 60)

    return 0


def parse_args():
    """Kommandozeilenargumente parsen.

    Ohne Argumente: Standalone-Modus (Vorgabe-Pfade).
    Mit Argumenten: von cpa_builder.py gesteuerte Pfade.
    """
    parser = argparse.ArgumentParser(
        description='CPA780 SYL-Bootlader Build-Skript (3-Datei-Version)')
    parser.add_argument('--build-dir',
        help='Build-Verzeichnis fuer Zwischendateien und Ausgabe')
    parser.add_argument('--project-dir',
        help='Projektverzeichnis (CPA_Workbench)')
    return parser.parse_args()


if __name__ == '__main__':
    args = parse_args()
    if args.project_dir:
        PROJECT_DIR = os.path.abspath(args.project_dir)
        TOOLS_DIR = os.path.join(PROJECT_DIR, 'tools')
        PREBUILT = os.path.join(PROJECT_DIR, 'prebuilt', 'bc_a5120', 'bootsec.bin')
    if args.build_dir:
        BUILD_DIR = os.path.abspath(args.build_dir)
    try:
        sys.exit(main())
    except RuntimeError as e:
        print(f"\nFEHLER: {e}", file=sys.stderr)
        sys.exit(2)
