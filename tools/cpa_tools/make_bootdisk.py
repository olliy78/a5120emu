#!/usr/bin/env python3
"""
make_bootdisk.py – Aus einer FORMATIERTEN CP/A-Leerdiskette mit CPABCGEN.COM eine
BOOTfähige Systemdiskette machen (schreibt Boot-Lader + @OS.COM auf die Systemspuren)
und den Boot direkt im A5120-Emulator verifizieren.

Zwei Presets (--preset):

  cpa780   5¼″-MFM, cpa780 (26×128 Systemspuren + 5×1024 Daten).  Standard-K5601-
           Laufwerk, Uhr-Boot-Disk als A:.
  mf3200   8″-SD/FM, MF3200 Format 7 (26×128 FM Sp.0-2 + 16×256 FM Sp.3-76, 296k).
           Der B:/A:-Slot wird auf das einseitige FM-Laufwerk mf3200_8_ss77 gesetzt
           (FD_PROFILES), da das 8″-Medium physisch einseitig/FM ist.

Ablauf (alles über tools/format_driver, den Zwei-Disk-Tastatur-Treiber):

  1. cpabcgen : boote eine CP/A-Boot-Disk als A: (die CPABCGEN.COM + @OS.COM
                enthält), lege die als Leer-System formatierte Ziel-Disk als B:/C:
                ein und fahre  `CPABCGEN <LW>:` .  CPABCGEN ist NICHT dialogbasiert —
                es nimmt das Ziel-Laufwerk als Kommandozeilen-Argument, schreibt
                Lader+System und meldet `OK`.
  2. verify    : mounte die eben geschriebene Ziel-Disk als A: und boote den Rechner
                kalt daraus; prüfe, dass CP/A hochkommt (Config-Screen + A>).

Die Ziel-Disk muss zuvor als das jeweilige Format formatiert sein — für die 8″-FM-
Variante liegt sie als `disks/empty_mf3200_296k.hfe` bereit (erzeugt mit
`tools/dev.sh tool mk_fm8_template disks/empty_mf3200_296k.hfe`; FORMAT.COM kann eine
gap-leere .hfe nicht direkt formatieren, s. docs/format.md §8.2).  Die 5¼″-Variante
nutzt `disks/empty_cpa780.hfe` (erzeugt mit `python3 tools/format_all.py 1 --full`).

  ⚠️  format_driver mountet BEIDE Disks schreibend → es werden IMMER Temp-Kopien
      benutzt; die committeten Images in disks/ bleiben unangetastet.

Verwendung:
  tools/dev.sh tool format_driver                       # Treiber einmalig bauen
  python3 tools/cpa_tools/make_bootdisk.py --preset cpa780
  python3 tools/cpa_tools/make_bootdisk.py --preset mf3200
  python3 tools/cpa_tools/make_bootdisk.py --preset mf3200 --out /tmp/x.hfe --quiet

Exit-Code: 0 = Boot verifiziert; 1 = Fehler; 2 = Voraussetzung fehlt.

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
# format_driver-Pfad: per Env FORMAT_DRIVER überschreibbar (z. B. aus ctest mit dem
# konkreten Build-Verzeichnis), sonst build/format_driver im Projektroot.
DRIVER = os.environ.get('FORMAT_DRIVER', os.path.join(ROOT, 'build', 'format_driver'))
DISKS  = os.path.join(ROOT, 'disks')

# ─── Presets ─────────────────────────────────────────────────────────────────
# boot_disk     : A:-Image mit CPABCGEN.COM + @OS.COM (liefert Lader & System)
# formatted     : als Leer-System formatierte Ziel-Vorlage (wird kopiert)
# out           : Standard-Ausgabepfad der Boot-Disk
# drive         : Ziel-Laufwerksbuchstabe für CPABCGEN
# cpabcgen_prof : FD_PROFILES für den CPABCGEN-Schritt (A: … / B: … / C: … / D: …)
# boot_prof     : FD_PROFILES für den Boot-Verify-Schritt (Boot-Disk als A:)
# needs_clock   : True → Kaltstart fragt die Uhrzeit ab (Uhr-@OS.COM)
PRESETS = {
    'cpa780': {
        'boot_disk':     os.path.join(DISKS, 'cpadisk_autofs_clock_noautoexec.img'),
        'formatted':     os.path.join(DISKS, 'empty_cpa780.hfe'),
        'out':           os.path.join(DISKS, 'bootdisk_cpabcgen.hfe'),
        'drive':         'B',
        'cpabcgen_prof': None,                       # 4× K5601 (Default)
        'boot_prof':     None,
        'needs_clock':   True,
    },
    'mf3200': {
        'boot_disk':     os.path.join(DISKS, 'cpadisk_autofs_noclock_8inchCombo.img'),
        'formatted':     os.path.join(DISKS, 'empty_mf3200_296k.hfe'),
        'out':           os.path.join(DISKS, 'bootdisk_mf3200.hfe'),
        'drive':         'B',
        # A: = 8inchCombo (cpa780-Format → K5601), B: = 8″-FM-Ziel (mf3200 einseitig).
        'cpabcgen_prof': 'K5601,mf3200_8_ss77,K5601,K5601',
        # Boot: die erzeugte 8″-FM-Disk liegt auf A: → mf3200-Profil.
        'boot_prof':     'mf3200_8_ss77,mf3200_8_ss77,K5601,K5601',
        'needs_clock':   False,
    },
}


def _script_cpabcgen(drive, needs_clock):
    lines = ['boot 90']
    if needs_clock:
        lines += ['type 12:00:00', 'enter']
    lines += ['boot 8', f'type CPABCGEN {drive}:', 'enter',
              'boot 70', 'dump cpabcgen',
              f'type DIR {drive}:', 'enter', 'boot 120', 'dump dir_target']
    return '\n'.join(lines) + '\n'


def _script_verify(needs_clock):
    lines = ['boot 100', 'dump cold_boot']
    if needs_clock:
        lines += ['type 12:00:00', 'enter', 'boot 30']
    lines += ['type DIR', 'enter', 'boot 120', 'dump dir_a']
    return '\n'.join(lines) + '\n'


def run_driver(diskA, diskB, script, profiles=None):
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
    env = dict(os.environ)
    if profiles:
        env['FD_PROFILES'] = profiles
    try:
        proc = subprocess.run([DRIVER, tA, tB, tS], capture_output=True, text=True, env=env)
        return proc.stdout, proc.stderr, tB
    finally:
        os.unlink(tA)
        os.unlink(tS)
        # tB wird vom Aufrufer weiterverwendet und dort gelöscht.


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--preset', default='cpa780', choices=list(PRESETS),
                   help='Laufwerks-/Format-Preset (cpa780 = 5¼″ MFM, mf3200 = 8″ FM)')
    p.add_argument('--formatted', help='Ziel-Vorlage überschreiben (sonst Preset)')
    p.add_argument('--out', help='Ausgabepfad überschreiben (sonst Preset)')
    p.add_argument('--no-verify', action='store_true', help='Boot-Verify überspringen')
    p.add_argument('--quiet', action='store_true', help='nur Ergebnis-Zeilen ausgeben')
    args = p.parse_args()

    cfg       = PRESETS[args.preset]
    formatted = args.formatted or cfg['formatted']
    out       = args.out       or cfg['out']
    boot_disk = cfg['boot_disk']
    drive     = cfg['drive']

    def log(*a):
        if not args.quiet:
            print(*a)

    if not os.path.exists(DRIVER):
        print(f"FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver",
              file=sys.stderr)
        return 2
    for f in (formatted, boot_disk):
        if not os.path.exists(f):
            print(f"FEHLER: Datei fehlt: {f}", file=sys.stderr)
            return 2

    # ── Schritt 1: CPABCGEN <LW>: ─────────────────────────────────────────────
    log(f"[1/2] ({args.preset}) CPABCGEN {drive}:  "
        f"({os.path.basename(formatted)} → {os.path.basename(out)})")
    o1, _e1, written = run_driver(boot_disk, formatted,
                                  _script_cpabcgen(drive, cfg['needs_clock']),
                                  profiles=cfg['cpabcgen_prof'])
    ok = 'Anlegen einer neuen Systemdiskette' in o1 and 'OK' in o1 \
         and 'Abbruch' not in o1 and 'Schreibfehler' not in o1
    if not ok:
        print("  FEHLER: CPABCGEN meldete kein OK.  Screen:")
        print(o1)
        os.unlink(written)
        return 1
    shutil.copyfile(written, out)
    os.unlink(written)
    log(f"  OK — CPABCGEN gemeldet; Ziel @OS.COM: {'@OS' in o1}")

    if args.no_verify:
        log(f"\nFertig: {out}  (Boot-Verify übersprungen)")
        print(f"BOOTDISK OK (no-verify): {out}")
        return 0

    # ── Schritt 2: Boot-Verify ────────────────────────────────────────────────
    log(f"[2/2] Boot-Verify: kalt aus {os.path.basename(out)} booten")
    o2, _e2, vtmp = run_driver(out, formatted, _script_verify(cfg['needs_clock']),
                               profiles=cfg['boot_prof'])
    os.unlink(vtmp)
    booted = 'CP/A, Version' in o2 and 'TPA ist OK' in o2
    reached = 'A>DIR' in o2 or ': @OS' in o2
    if not args.quiet:
        tail = o2.rsplit('=== SCREEN:', 1)
        if len(tail) == 2:
            print('=== SCREEN:' + tail[1])
    log(f"\n  Kaltstart CP/A: {booted}   A>-Prompt/@OS erreicht: {reached}")
    if booted and reached:
        print(f"BOOTDISK OK: {out}")
        return 0
    print(f"BOOTDISK FEHLER: Boot-Verify fehlgeschlagen ({out})")
    return 1


if __name__ == '__main__':
    sys.exit(main())
