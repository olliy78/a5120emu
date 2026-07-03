#!/usr/bin/env python3
"""
capture_format_menus.py – Greift die FORMAT.COM-Formatmenüs für die verschiedenen
Laufwerkstypen live aus dem A5120-Emulator ab.

Welche Formate FORMAT.COM anbietet, hängt vom Laufwerkstyp ab, den das BIOS für den
gewählten Laufwerksbuchstaben meldet (siehe docs/format.md §2/§10).  Die neuen
Combo-Boot-Disketten konfigurieren B:/C: als andere Laufwerkstypen, sodass sich deren
Menüs erstmals im Emulator abgreifen lassen:

  cpadisk_autofs_noclock_5inchCombo : A:K5601  B:K5600.10 (5" 40 DD SS)  C:K5600.20 (5" 80 DD SS)
  cpadisk_autofs_noclock_8inchCombo : A:K5601  B:MF3200   (8" 77 SD SS)  C:K5602.10/MF6400 (8" 77 DD SS)

Der Runner bootet die passende Combo-Disk (A:), startet FORMAT.COM, wählt das Laufwerk,
blättert mit X/Y/Z durch alle Menüseiten und dumpt jeden Bildschirm.  Es wird NICHT
formatiert (kein Format-Key gesendet) — reines Menü-Capturing.

Verwendung:
  tools/dev.sh tool format_driver              # Treiber bauen (einmalig)
  python3 tools/capture_format_menus.py --list
  python3 tools/capture_format_menus.py K5600.10 K5600.20
  python3 tools/capture_format_menus.py --all --outdir out/menus

Autor: 2026 / MIT
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE     = os.path.dirname(os.path.abspath(__file__))
ROOT     = os.path.dirname(HERE)
DRIVER   = os.path.join(ROOT, 'build', 'format_driver')
DISKS    = os.path.join(ROOT, 'disks')

COMBO5   = os.path.join(DISKS, 'cpadisk_autofs_noclock_5inchCombo.img')
COMBO8   = os.path.join(DISKS, 'cpadisk_autofs_noclock_8inchCombo.img')
# noclk-Disk: bootet ohne Uhr-Abfrage direkt nach A> (alle 3 LW = K5601).
NOCLK    = os.path.join(DISKS, 'cpadisk_autofs_noclk_noautoexec.img')
# Gültiges Template für die B:/C:-Slots (Menü-Capture liest das Medium nicht, aber
# der Slot muss belegt sein, damit FORMAT den Laufwerksbuchstaben akzeptiert).
TEMPLATE = os.path.join(DISKS, 'cpadisk_autofs_clock_noautoexec.img')

# ─── Laufwerks-Matrix ────────────────────────────────────────────────────────
# name: (boot_disk, drive_letter, dpb_code, beschreibung)
#   drive_letter A/B/C — A: ist immer die Combo-Boot-Disk (K5601); B:/C: die
#   in der Combo-BIOS konfigurierten Fremdtypen.
DRIVES = {
    'K5601':    (NOCLK,  'B', '11580', '5 1/4", 80 Sp., DD, DS  (MFS 1.6)'),
    'K5600.10': (COMBO5, 'B', '10540', '5 1/4", 40 Sp., DD, SS  (MFS 1.2)'),
    'K5600.20': (COMBO5, 'C', '10580', '5 1/4", 80 Sp., DD, SS  (MFS 1.4)'),
    'MF3200':   (COMBO8, 'B', '00877', '8", 77 Sp., SD, SS'),
    'MF6400':   (COMBO8, 'C', '10877', '8", 77 Sp., DD, SS  (K5602.10 / FS 6400)'),
}


def make_script(drive_letter):
    """Script: boot → FORMAT starten → Laufwerk wählen → alle Menüseiten dumpen."""
    lines = [
        'boot 120',                 # Kaltstart bis A> (noclock: keine Uhr-Abfrage)
        'type FORMAT', 'enter',
        'boot 30', 'enter',         # Funktion 0 = Formatieren
        'boot 6', f'type {drive_letter}', 'enter',   # Laufwerk + Einlege-Quittung
        'boot 10', 'enter',         # Vergleichs-Lesen = j
        'boot 8', 'dump menu_1',    # Menüseite 1
        'type X', 'boot 4', 'dump menu_2',   # ggf. Menüseite 2
        'type Y', 'boot 4', 'dump menu_3',   # ggf. Menüseite 3
        'type Z', 'boot 4', 'dump menu_back', # zurück zu #1 (Konsistenz)
    ]
    return '\n'.join(lines) + '\n'


SCREEN_RE = re.compile(r'^=== SCREEN:\s*(\S+)\s*===$')


def parse_screens(stdout):
    """Zerlegt format_driver-stdout in {label: [zeilen]}."""
    screens, cur, lbl = {}, None, None
    for line in stdout.splitlines():
        m = SCREEN_RE.match(line.strip())
        if m:
            lbl = m.group(1)
            cur = []
            screens[lbl] = cur
        elif cur is not None and line.startswith('  |'):
            cur.append(line[3:].rstrip())
    return screens


def is_menu(lines):
    return any('Bitte Format auswaehlen' in l for l in lines)


def trim(lines):
    """Menü-Zeilen bis inkl. der 'Bitte Format...'-Prompt-Zeile."""
    out = []
    for l in lines:
        if not l.strip() and (not out or not out[-1].strip()):
            continue
        out.append(l)
        if 'Bitte Format auswaehlen' in l:
            break
    return out


def capture(name, outdir):
    boot_disk, letter, code, desc = DRIVES[name]
    with tempfile.TemporaryDirectory() as td:
        diskA = os.path.join(td, 'A.img'); shutil.copyfile(boot_disk, diskA)
        diskB = os.path.join(td, 'B.img'); shutil.copyfile(TEMPLATE, diskB)
        script_path = os.path.join(td, 's.txt')
        with open(script_path, 'w') as f:
            f.write(make_script(letter))
        env = dict(os.environ)
        cmd = [DRIVER, diskA, diskB, script_path]
        if letter == 'C':
            diskC = os.path.join(td, 'C.img'); shutil.copyfile(TEMPLATE, diskC)
            env['FD_DISKC'] = diskC
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)

    screens = parse_screens(proc.stdout)
    # Eindeutige Menüseiten (Duplikate weglassen — nicht jede Combo hat 3 Seiten).
    pages, seen = [], set()
    for lbl in ('menu_1', 'menu_2', 'menu_3'):
        if lbl in screens and is_menu(screens[lbl]):
            body = '\n'.join(trim(screens[lbl]))
            if body not in seen:
                seen.add(body)
                pages.append(body)

    header = ''
    if pages:
        header = pages[0].splitlines()[0]

    if outdir:
        os.makedirs(outdir, exist_ok=True)
        with open(os.path.join(outdir, f'{name}.txt'), 'w') as f:
            f.write(f'# {name}  ({desc})  DPB={code}  Laufwerk {letter}:\n\n')
            f.write('\n\n'.join(pages) + '\n')

    return {'name': name, 'desc': desc, 'code': code, 'letter': letter,
            'header': header, 'pages': pages, 'ok': bool(pages)}


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('drives', nargs='*', help='Laufwerksnamen (siehe --list); leer = --all')
    p.add_argument('--all', action='store_true', help='alle Laufwerkstypen')
    p.add_argument('--outdir', default='', help='Menüs je Laufwerk als Textdatei ablegen')
    p.add_argument('--list', action='store_true', help='Laufwerks-Matrix zeigen')
    args = p.parse_args()

    if args.list:
        print('Laufwerk    DPB    Boot-Disk / LW  Beschreibung')
        for n, (d, l, c, desc) in DRIVES.items():
            print(f'  {n:<10} {c}  {os.path.basename(d)} {l}:  {desc}')
        return 0

    if not os.path.exists(DRIVER):
        print(f'FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver',
              file=sys.stderr)
        return 2

    names = list(DRIVES) if (args.all or not args.drives) else args.drives
    bad = [n for n in names if n not in DRIVES]
    if bad:
        print(f'FEHLER: unbekannte Laufwerke {bad}', file=sys.stderr)
        return 2

    all_ok = True
    for n in names:
        r = capture(n, args.outdir)
        print(f'\n{"="*72}\n{n}  ({r["desc"]})  DPB={r["code"]}  [Laufwerk {r["letter"]}:]\n{"="*72}')
        if not r['ok']:
            print('  !! kein Formatmenü erfasst (Boot/Navigation fehlgeschlagen)')
            all_ok = False
            continue
        for i, page in enumerate(r['pages'], 1):
            print(f'\n--- Menüseite {i} ---')
            print(page)
    return 0 if all_ok else 1


if __name__ == '__main__':
    sys.exit(main())
