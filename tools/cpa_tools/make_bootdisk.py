#!/usr/bin/env python3
"""
make_bootdisk.py – Aus einer FORMATIERTEN CP/A-Diskette mit CPABCGEN.COM eine
BOOTfähige Systemdiskette machen (schreibt Boot-Lader + @OS.COM auf die Systemspuren)
und den Boot direkt im A5120-Emulator verifizieren.

Ablauf (alles über tools/format_driver, den Zwei-Disk-Tastatur-Treiber):

  1. cpabcgen  : boote eine CP/A-Boot-Disk als A: (die CPABCGEN.COM + @OS.COM
                 enthält), lege die zu bootbarmachende, bereits als cpa780
                 formatierte Ziel-Disk als B: ein und fahre  `CPABCGEN B:` .
                 CPABCGEN ist NICHT dialogbasiert — es nimmt das Ziel-Laufwerk
                 als Kommandozeilen-Argument, schreibt Lader+System und meldet `OK`.
  2. verify    : mounte die eben geschriebene Ziel-Disk als A: und boote den Rechner
                 kalt daraus; prüfe, dass CP/A hochkommt (Config-Screen + A>).

Die Ziel-Disk muss zuvor als **cpa780** (26x128 Systemspuren + 5x1024 Datenbereich)
formatiert sein — CPABCGEN verlangt 128er/256er Systemspuren.  Eine leere solche Disk
liegt als `disks/empty_cpa780.hfe` bereit (erzeugt mit
`python3 tools/format_all.py 1 --full --type hfe`).

  ⚠️  format_driver mountet BEIDE Disks schreibend → es werden IMMER Temp-Kopien
      benutzt; die committeten Images in disks/ bleiben unangetastet.

Verwendung:
  tools/dev.sh tool format_driver                       # Treiber einmalig bauen
  python3 tools/cpa_tools/make_bootdisk.py \
        --formatted disks/empty_cpa780.hfe \
        --out       disks/bootdisk_cpabcgen.hfe
  python3 tools/cpa_tools/make_bootdisk.py --formatted X.hfe --out Y.hfe --no-verify

Autor: 2026 / MIT
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE   = os.path.dirname(os.path.abspath(__file__))
ROOT   = os.path.dirname(os.path.dirname(HERE))
DRIVER = os.path.join(ROOT, 'build', 'format_driver')
DISKS  = os.path.join(ROOT, 'disks')
# Boot-Disk (A:) mit CPABCGEN.COM + @OS.COM.  Die clock-Variante fragt beim
# Kaltstart die Uhrzeit ab.
BOOT_CLOCK = os.path.join(DISKS, 'cpadisk_autofs_clock_noautoexec.img')

# CPABCGEN-Schritt: Uhrzeit setzen, `CPABCGEN B:` fahren, Ergebnis zeigen.
SCRIPT_CPABCGEN = """\
boot 80
type 12:00:00
enter
boot 8
type CPABCGEN {drive}:
enter
boot 60
dump cpabcgen
type DIR {drive}:
enter
boot 120
dump dir_target
"""

# Boot-Verify: Ziel-Disk als A:, kalt booten, DIR fahren.
SCRIPT_VERIFY = """\
boot 90
dump cold_boot
type 12:00:00
enter
boot 30
type DIR
enter
boot 120
dump dir_a
"""


def run_driver(diskA, diskB, script):
    """format_driver mit Temp-Kopien beider Disks laufen lassen; stdout zurückgeben."""
    with tempfile.NamedTemporaryFile(suffix=os.path.splitext(diskA)[1], delete=False) as ta:
        tA = ta.name
    with tempfile.NamedTemporaryFile(suffix=os.path.splitext(diskB)[1], delete=False) as tb:
        tB = tb.name
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as ts:
        ts.write(script)
        tS = ts.name
    shutil.copyfile(diskA, tA)
    shutil.copyfile(diskB, tB)
    try:
        proc = subprocess.run([DRIVER, tA, tB, tS], capture_output=True, text=True)
        # Der B:-Slot ist das (potenziell) geschriebene Ziel — zurückschreiben.
        return proc.stdout, proc.stderr, tB
    finally:
        os.unlink(tA)
        os.unlink(tS)
        # tB wird vom Aufrufer weiterverwendet und dort gelöscht.


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--formatted', default=os.path.join(DISKS, 'empty_cpa780.hfe'),
                   help='als cpa780 formatierte Ziel-Disk (Vorlage; wird kopiert)')
    p.add_argument('--out', default=os.path.join(DISKS, 'bootdisk_cpabcgen.hfe'),
                   help='Pfad der erzeugten Boot-Disk')
    p.add_argument('--boot', default=BOOT_CLOCK,
                   help='Boot-Disk (A:) mit CPABCGEN.COM + @OS.COM')
    p.add_argument('--drive', default='B', choices=['B', 'C'],
                   help='Ziel-Laufwerksbuchstabe für CPABCGEN')
    p.add_argument('--no-verify', action='store_true', help='Boot-Verify überspringen')
    args = p.parse_args()

    if not os.path.exists(DRIVER):
        print(f"FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver",
              file=sys.stderr)
        return 2
    for f in (args.formatted, args.boot):
        if not os.path.exists(f):
            print(f"FEHLER: Datei fehlt: {f}", file=sys.stderr)
            return 2

    # ── Schritt 1: CPABCGEN B: ────────────────────────────────────────────────
    print(f"[1/2] CPABCGEN {args.drive}:  ({os.path.basename(args.formatted)} → "
          f"{os.path.basename(args.out)})")
    out, err, written = run_driver(args.boot, args.formatted,
                                   SCRIPT_CPABCGEN.format(drive=args.drive))
    ok = 'Anlegen einer neuen Systemdiskette' in out and 'OK' in out
    has_os = '@OS' in out and 'COM' in out
    if not ok:
        print("  FEHLER: CPABCGEN meldete kein OK.  Screen:")
        print(out)
        os.unlink(written)
        return 1
    # geschriebenes Ziel sichern
    shutil.copyfile(written, args.out)
    os.unlink(written)
    print(f"  OK — CPABCGEN gemeldet; DIR zeigt @OS.COM: {has_os}")

    if args.no_verify:
        print(f"\nFertig: {args.out}  (Boot-Verify übersprungen)")
        return 0

    # ── Schritt 2: Boot-Verify ────────────────────────────────────────────────
    print(f"[2/2] Boot-Verify: kalt aus {os.path.basename(args.out)} booten")
    vout, verr, vtmp = run_driver(args.out, args.formatted, SCRIPT_VERIFY)
    os.unlink(vtmp)
    booted = 'CP/A, Version' in vout and 'TPA ist OK' in vout
    reached_prompt = 'A>DIR' in vout or ': @OS' in vout
    # letzten Screen zeigen
    tail = vout.rsplit('=== SCREEN:', 1)
    if len(tail) == 2:
        print('=== SCREEN:' + tail[1])
    print(f"\n  Kaltstart CP/A: {booted}   A>-Prompt erreicht: {reached_prompt}")
    if booted and reached_prompt:
        print(f"\n✅ BOOTfähig: {args.out}")
        return 0
    print(f"\n❌ Boot-Verify fehlgeschlagen — siehe Screens oben.")
    return 1


if __name__ == '__main__':
    sys.exit(main())
