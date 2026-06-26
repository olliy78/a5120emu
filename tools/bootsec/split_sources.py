#!/usr/bin/env python3
"""
split_sources.py -- Teilt die monolithische bootsec.mac in drei
separate Quelldateien auf:
  1. bootsec_syl.mac   (0000H-00FFH: SYL-Header + Bootloader)
  2. mini_bdos.mac      (0100H-0CFFH: Mini-BDOS, voll kommentiert)
  3. boot_bios.mac      (0D00H-19FFH: Boot-BIOS / Stage-2)

Voraussetzungen:
  - src/bootsec.mac      (monolithische Version, IDENTISCH-verifiziert)
  - src/mini_bdos.mac    (Dokumentations-Disassembly, Basis fuer BDOS-Code)

Ergebnis:
  - src/bootsec_syl.mac
  - src/mini_bdos_asm.mac (assemblierbare Version)
  - src/boot_bios.mac
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(SCRIPT_DIR, 'src')

BOOTSEC_MAC = os.path.join(SRC_DIR, 'bootsec.mac')
MINI_BDOS_DOC = os.path.join(SRC_DIR, 'mini_bdos.mac')

OUT_SYL   = os.path.join(SRC_DIR, 'bootsec_syl.mac')
OUT_BDOS  = os.path.join(SRC_DIR, 'mini_bdos_asm.mac')
OUT_BIOS  = os.path.join(SRC_DIR, 'boot_bios.mac')


def read_lines(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.readlines()


def write_file(path, lines):
    with open(path, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    print(f"  Geschrieben: {os.path.basename(path)} ({len(lines)} Zeilen)")


def create_bootsec_syl(bootsec_lines):
    """Extrahiert 0000H-00FFH aus bootsec.mac."""
    header = """\
; ======================================================================
; BOOTSEC_SYL -- SYL-Header + Bootloader (0000H-00FFH)
; ======================================================================
; Generiert aus bootsec.mac (monolithische Version)
;
; Adressbereich: 0000H-00FFH (256 Bytes, Track 0 Sektor 0-1)
;
; Inhalt:
;   0000H-007FH  SYL-Header: Kennung, FDC_INIT, JP COLDBOOT
;   0080H-00CBH  WBOOT-Vektor + @OS.COM-Ladesequenz + READ_FILE
;   00CCH-00F0H  FCB fuer @OS.COM ("@OS     COM")
;   00F1H-00FFH  Padding (Nullbytes, Teil 1)
;
; Dieses Modul wird zusammen mit mini_bdos_asm.mac und boot_bios.mac
; assembliert, um die vollstaendige bootsec.bin zu erzeugen.
; ======================================================================

\t.Z80
\tASEG

"""
    out = [header]

    # Finde ABSCHNITT 1 (ORG 0000H) -- Zeile mit "ABSCHNITT 1"
    start_idx = None
    for i, line in enumerate(bootsec_lines):
        if 'ABSCHNITT 1: SYL-HEADER' in line:
            start_idx = i
            break

    if start_idx is None:
        raise RuntimeError("ABSCHNITT 1 nicht gefunden in bootsec.mac")

    # Finde die Zeile "DS 21 ; 00F1-0105" und ersetze sie
    end_idx = None
    for i in range(start_idx, len(bootsec_lines)):
        line = bootsec_lines[i]
        if '00F1-0105' in line and 'DS' in line:
            end_idx = i
            break

    if end_idx is None:
        raise RuntimeError("Padding DS 21 (00F1-0105) nicht gefunden")

    # Alles von ABSCHNITT 1 bis VOR die DS 21 Zeile
    for i in range(start_idx, end_idx):
        out.append(bootsec_lines[i])

    # Ersetze DS 21 durch DS 15 (00F1-00FF statt 00F1-0105)
    out.append('; --- Padding (Nullbytes) (00F1H-00FFH) ---\n')
    out.append('\tDS\t15\t\t; 00F1-00FF: Padding\n')
    out.append('\n')
    out.append('\tEND\n')

    write_file(OUT_SYL, out)


def create_mini_bdos_asm(bdos_doc_lines):
    """Erstellt assemblierbare mini_bdos_asm.mac aus der Doku-Version."""
    header = """\
; ======================================================================
; MINI_BDOS_ASM -- Mini-BDOS Modul (0100H-0CFFH)
; ======================================================================
; Assemblierbare Version, basierend auf der kommentierten Disassemblierung
; (mini_bdos.mac, erzeugt mit bdos_analyze.py).
;
; Adressbereich: 0100H-0CFFH (3072 Bytes, Track 0 Sektoren 2-25)
;
; Architektur:
;   Das BDOS-Modul wurde original mit ORG 0900H assembliert.
;   Alle internen Referenzen verwenden Laufzeit-Adressen (+0800H).
;   @OS.COM verschiebt den Code zur Laufzeit von 0100H nach 0900H.
;
;   Phys. 0100H = Laufzeit 0900H   (BDOS-Modul-Anfang)
;   Phys. 0106H = Laufzeit 0906H   (BDOS-Einstieg: JP bdose)
;   Phys. 0111H = Laufzeit 0911H   (bdose Einstiegspunkt)
;   Phys. 0CFFH = Laufzeit 14FFH   (BDOS-Modul-Ende)
;
; Dies ist ein READ-ONLY Mini-BDOS fuer den Boot-Vorgang:
;   - Schreib-Funktionen (func10/21/33/34/40) zeigen auf Stub (08AFH)
;   - Console-I/O ist vollstaendig (func1-9, func11)
;   - Disk-Lesen ist vollstaendig (func15/17/18/20)
;
; CP/M-Referenz: doc/CPM_2_2/OS3BDOS.ASM + OS3BDOS1.ASM
;
; Dieses Modul wird zusammen mit bootsec_syl.mac und boot_bios.mac
; assembliert, um die vollstaendige bootsec.bin zu erzeugen.
; ======================================================================

\t.Z80
\tASEG
\tORG\t0100H

; --- Padding (0100H-0105H) ---
\tDS\t6\t\t; 0100-0105: Padding (Nullbytes)

"""
    out = [header]

    # Finde BDOS_ENTRY (0106H) in der Doku-Datei
    start_idx = None
    for i, line in enumerate(bdos_doc_lines):
        if line.startswith('BDOS_ENTRY:'):
            start_idx = i
            break

    if start_idx is None:
        raise RuntimeError("BDOS_ENTRY nicht gefunden in mini_bdos.mac")

    # Finde das Ende (letzte DS 7 + Padding, vor dem Statistik-Block)
    end_idx = None
    for i in range(len(bdos_doc_lines) - 1, -1, -1):
        line = bdos_doc_lines[i]
        if '\tEND' in line:
            end_idx = i
            break

    if end_idx is None:
        raise RuntimeError("END nicht gefunden in mini_bdos.mac")

    # Alles von BDOS_ENTRY bis VOR END
    for i in range(start_idx, end_idx):
        out.append(bdos_doc_lines[i])

    out.append('\n')
    out.append('\tEND\n')

    write_file(OUT_BDOS, out)


def create_boot_bios(bootsec_lines):
    """Extrahiert 0D00H-19FFH aus bootsec.mac."""
    header = """\
; ======================================================================
; BOOT_BIOS -- Boot-BIOS / Stage-2 (0D00H-19FFH)
; ======================================================================
; Generiert aus bootsec.mac (monolithische Version)
;
; Adressbereich: 0D00H-19FFH (3328 Bytes, Track 2 Sektoren 0-25)
;
; Inhalt:
;   0D00H-0D06H  Zweiter SYL-Kopf + Padding
;   0D07H-0D2DH  FDC_ADRDEC: CRC/Adressen-Decoder
;   0D2EH-0E6FH  BOOT_MAIN: FDC-Init, @OS.COM suchen/laden
;   0E72H-0FC0H  OS-Lade-Fortsetzung, ISRs, Block-Transfer
;   0FC1H-0FFFH  Versionsstring + Fuellbytes
;   1000H-1037H  CP/M 2.2 BIOS-Sprungtabelle (17 Eintraege)
;   1038H-108FH  DPH/DPB Disk-Parameter-Tabellen
;   1090H-10B1H  GOCPM: Kaltstart-Patches (JP 0000/0005)
;   10B2H-1134H  CONOUT: Bildschirmtreiber, Scroll, Cursor
;   1148H-13B7H  FDC-Treiber: Disk Read/Write/Seek
;   13F0H-16AEH  FDC: Seek, Motor, Sektor-IO, CRC, Format
;   1738H-17FFH  FDC Low-Level: PIO-Setup, DMA, Sync
;   1800H-19BBH  WARM_BIOS: Hardware-Init, Laufwerk-Scan
;   19BCH-19FFH  Boot-Meldung + Fuellbytes
;
; Dieses Modul wird zusammen mit bootsec_syl.mac und mini_bdos_asm.mac
; assembliert, um die vollstaendige bootsec.bin zu erzeugen.
; ======================================================================

\t.Z80
\tASEG

"""
    out = [header]

    # Finde ABSCHNITT 3 (ORG 0D00H)
    start_idx = None
    for i, line in enumerate(bootsec_lines):
        if 'ABSCHNITT 3' in line and 'STAGE-2' in line:
            start_idx = i
            break

    if start_idx is None:
        raise RuntimeError("ABSCHNITT 3 nicht gefunden in bootsec.mac")

    # Finde END
    end_idx = None
    for i in range(len(bootsec_lines) - 1, -1, -1):
        if bootsec_lines[i].strip() == 'END':
            end_idx = i
            break

    if end_idx is None:
        raise RuntimeError("END nicht gefunden in bootsec.mac")

    # Alles von ABSCHNITT 3 bis einschliesslich END
    for i in range(start_idx, end_idx + 1):
        out.append(bootsec_lines[i])

    write_file(OUT_BIOS, out)


def main():
    print("=" * 60)
    print("Split bootsec.mac -> 3 separate Quelldateien")
    print("=" * 60)

    bootsec_lines = read_lines(BOOTSEC_MAC)
    print(f"  Gelesen: bootsec.mac ({len(bootsec_lines)} Zeilen)")

    bdos_doc_lines = read_lines(MINI_BDOS_DOC)
    print(f"  Gelesen: mini_bdos.mac ({len(bdos_doc_lines)} Zeilen)")

    print()
    create_bootsec_syl(bootsec_lines)
    create_mini_bdos_asm(bdos_doc_lines)
    create_boot_bios(bootsec_lines)

    print()
    print("Fertig. Dateien in src/ erstellt.")
    print("Naechster Schritt: Build-Script anpassen und IDENTISCH-Vergleich.")


if __name__ == '__main__':
    main()
