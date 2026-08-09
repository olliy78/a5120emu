#!/usr/bin/env python3
"""
make_bootdisk.py – Aus einer LEEREN (unformatierten) Diskette eine BOOTfähige
CP/A-Systemdiskette machen und den Boot direkt im A5120-Emulator verifizieren:
die volle Anwenderkette FORMAT.COM → CPABCGEN.COM → Kaltstart.

Presets (--preset) — je Preset ein Laufwerkstyp/Format:

  cpa780        5¼″-MFM, cpa780 (26×128 Sys + 5×1024 Daten, doppelseitig). Uhr-Boot-Disk.
  mf3200_fmt7   8″-SD/FM  MF3200 F7: 26×128 FM Sp.0-2 + 16×256 FM Sp.3-76   (296k)
  mf3200_fmt1   8″-SD/FM  MF3200 F1: 26×128 FM Sp.0-2 +  4×1024 FM Sp.3-76  (296k)
  mf6400_fmt1   8″-DD/MFM MF6400 F1: 26×128 Sp.0-1 + 8×1024 Sp.2-76         (600k, C:)
  k5600_10_fmt1 5¼″-40-SS K5600.10 F1: 26×128 Sp.0-1 + 5×1024 Sp.2-39       (190k)
  k5600_20_fmt1 5¼″-80-SS K5600.20 F1: 26×128 Sp.0-1 + 5×1024 Sp.2-79       (390k, C:)

Ablauf (alles über tools/format_driver, den Zwei-Disk-Tastatur-Treiber):

  1. format+gen : boote die passende Combo-/Uhr-Boot-Disk als A: (mit FORMAT.COM,
     CPABCGEN.COM + @OS.COM) und lege eine **echte Leerdiskette** als B:/C: ein.
     Dann in EINEM Lauf: `FORMAT` (Format des Presets, alle Spuren, MIT Vergleichs-
     Lesen) → zurück ins CCP → `CPABCGEN <LW>:` → `DIR <LW>:`.
     Der Slot bekommt per FD_PROFILES das zum Combo-BIOS passende physische Laufwerk.
  2. verify     : mounte die erzeugte Disk als A: (Ziel-Profil) und boote kalt daraus;
     prüfe, dass CP/A hochkommt (Config-Screen + A>/@OS.COM).

  Der Ausgangszustand ist IMMER eine unformatierte Leerdiskette (format_driver-
  Parameter `createB` = leerer Formatname → `A5120Machine::createDisk` legt ein
  unformatiertes Medium in der Geometrie des LAUFWERKS an, CLAUDE.md §8.7).
  Frühere Fassungen mussten die Vorlage stattdessen mit `mk_disk_template` bzw. aus
  `disks/empty_cpa780.hfe` vorformatiert liefern, weil FORMAT.COM auf einer gap-leeren
  Diskette hing (doc/format.md §8.2) und später `Fehler 'U' SPUR DEFEKT` meldete
  (doc/analyse_format_leerspur.md).  Beides ist behoben.

  ⚠️  format_driver mountet alle Disks schreibend → A: wird IMMER als Temp-Kopie
      gemountet; die Ziel-Disks sind ohnehin Temp-Dateien.

Verwendung:
  tools/dev.sh tool format_driver
  python3 tests/system/drivers/make_bootdisk.py --preset mf6400_fmt1 --quiet

Env:
  FORMAT_DRIVER      Pfad zu format_driver (Default build/format_driver)

Exit-Code: 0 = Boot verifiziert; 1 = Fehler; 2 = Voraussetzung fehlt.

Autor: 2026 / MIT
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE   = os.path.dirname(os.path.abspath(__file__))          # tests/system/drivers
ROOT   = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
# Testdisketten liegen als unveraenderliche Fixtures unter tests/fixtures/disks/;
# disks/ ist das Arbeitsverzeichnis fuer manuelle Laeufe.
DISKS  = os.path.join(ROOT, 'tests', 'fixtures', 'disks')
DRIVER = os.environ.get('FORMAT_DRIVER', os.path.join(ROOT, 'build', 'format_driver'))

BOOT_CLOCK      = os.path.join(DISKS, 'cpa_cpa780_k5601_clock.img')
BOOT_8INCHCOMBO = os.path.join(DISKS, 'cpa_cpa780_combo8zoll_noclock.img')
BOOT_5INCHCOMBO = os.path.join(DISKS, 'cpa_cpa780_combo5zoll_noclock.img')

BLANK = ''   # createB/FD_DISKC_FMT-Formatname für „unformatierte Leerdiskette"

# Preset-Felder:
#   boot        A:-Boot-Disk (FORMAT.COM + CPABCGEN.COM + @OS.COM)
#   drive       Ziel-Laufwerksbuchstabe (B/C)
#   prof        DriveProfile des Ziel-Laufwerks (für Boot-Verify: Ziel liegt auf A:)
#   b_prof      DriveProfile des B:-Slots während Schritt 1 (Combo-B:-Typ)
#   c_prof      DriveProfile des C:-Slots während Schritt 1 (Combo-C:-Typ)
#   needs_clock Kaltstart fragt Uhrzeit ab
#   fmt_key     Auswahltaste in FORMAT.COMs Formatliste (alle Presets: Menüseite 1)
#   fmt_nav     Tastenfolge zur richtigen Menüseite (leer = Seite 1)
#   budget      Takt-Budget (Mio.) fuer den Formatierlauf; Richtwert ~12 je Spur,
#               gemessen werden ~3–5,5 Mio. Takte je LEERER Spur.
PRESETS = {
    'cpa780': dict(
        boot=BOOT_CLOCK, drive='B', prof='K5601', b_prof='K5601', c_prof='K5601',
        needs_clock=True, fmt_key='1', fmt_nav='', budget=2000,   # 160 Spuren
    ),
    'mf3200_fmt7': dict(
        boot=BOOT_8INCHCOMBO, drive='B', prof='MF3200',
        b_prof='MF3200', c_prof='MF6400', needs_clock=False,
        fmt_key='7', fmt_nav='', budget=1200,                     # 77 Spuren
    ),
    'mf3200_fmt1': dict(
        boot=BOOT_8INCHCOMBO, drive='B', prof='MF3200',
        b_prof='MF3200', c_prof='MF6400', needs_clock=False,
        fmt_key='1', fmt_nav='', budget=1200,
    ),
    'mf6400_fmt1': dict(
        boot=BOOT_8INCHCOMBO, drive='C', prof='MF6400',
        b_prof='MF3200', c_prof='MF6400', needs_clock=False,
        fmt_key='1', fmt_nav='', budget=1200,
    ),
    'k5600_10_fmt1': dict(
        boot=BOOT_5INCHCOMBO, drive='B', prof='K5600.10',
        b_prof='K5600.10', c_prof='K5600.20', needs_clock=False,
        fmt_key='1', fmt_nav='', budget=800,                      # 40 Spuren
    ),
    'k5600_20_fmt1': dict(
        boot=BOOT_5INCHCOMBO, drive='C', prof='K5600.20',
        b_prof='K5600.10', c_prof='K5600.20', needs_clock=False,
        fmt_key='1', fmt_nav='', budget=1200,                     # 80 Spuren
    ),
}


def _profiles(a, b, c, d='K5601'):
    return f'{a},{b},{c},{d}'


def _script_format_and_gen(cfg):
    """FORMAT.COM (ganze Diskette, mit Verify) → CCP → CPABCGEN <LW>: → DIR <LW>:."""
    drive = cfg['drive']
    lines = ['boot 90']
    if cfg['needs_clock']:
        lines += ['type 12:00:00', 'enter']
    lines += [
        'boot 5', 'type FORMAT', 'enter',           # FORMAT.COM starten
        'boot 30', 'enter',                         # Funktion 0 = Formatieren
        'boot 6', f'type {drive}', 'enter',         # Laufwerk + Einlege-Quittung
        'boot 10', 'enter',                         # Vergleichs-Lesen = j
        'boot 8',
    ]
    for k in cfg['fmt_nav']:                        # zur richtigen Menüseite blättern
        lines += [f'type {k}', 'boot 3']
    lines += [f"type {cfg['fmt_key']}", 'boot 6']   # Format auswählen
    lines += ['enter', 'boot 5']                    # von Spur 0
    lines += ['enter']                              # bis Spur = letzte
    lines += ['boot 6', 'type j']                   # Warnung bestätigen
    lines += [f"boot {cfg['budget']}", 'dump format']
    # FORMAT.COM verlassen: "Wiederholung? (j/n)" -> n, "Rueckkehr? (j, sonst Ende)" -> n
    lines += ['type n', 'boot 20', 'type n', 'boot 30']
    lines += [f'type CPABCGEN {drive}:', 'enter', 'boot 80', 'dump cpabcgen',
              f'type DIR {drive}:', 'enter', 'boot 120', 'dump dir_target']
    return '\n'.join(lines) + '\n'


def _script_verify(needs_clock):
    lines = ['boot 100', 'dump cold_boot']
    if needs_clock:
        lines += ['type 12:00:00', 'enter', 'boot 30']
    lines += ['type DIR', 'enter', 'boot 120', 'dump dir_a']
    return '\n'.join(lines) + '\n'


def run_driver(script, a_path, b_path, c_path=None, profiles=None):
    """
    format_driver laufen lassen.

      a_path : Boot-Disk — wird IMMER als Temp-Kopie gemountet (das committete Image
               darf nie beschrieben werden).
      b_path : Datei des B:-Slots.  Existiert sie nicht, legt format_driver sie als
               **Leerdiskette** an (createB = leerer Formatname); existiert sie, wird
               sie unverändert (schreibend!) gemountet.
      c_path : dito für den C:-Slot (FD_DISKC / FD_DISKC_FMT).

    Liefert (stdout, stderr).  b_path/c_path bleiben liegen — der Aufrufer räumt auf.
    """
    fd, tmpA = tempfile.mkstemp(suffix=os.path.splitext(a_path)[1]); os.close(fd)
    shutil.copyfile(a_path, tmpA)
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as ts:
        ts.write(script); tmpS = ts.name

    env = dict(os.environ)
    if profiles:
        env['FD_PROFILES'] = profiles
    if c_path:
        env['FD_DISKC'] = c_path
        if not os.path.exists(c_path):
            env['FD_DISKC_FMT'] = BLANK

    cmd = [DRIVER, tmpA, b_path, tmpS]
    if not os.path.exists(b_path):
        cmd.append(BLANK)          # B: als Leerdiskette anlegen
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    finally:
        os.unlink(tmpA)
        os.unlink(tmpS)
    return proc.stdout, proc.stderr


def _blank_path(suffix='.hfe'):
    """Temp-Pfad für eine Leerdiskette — die DATEI wird bewusst nicht angelegt."""
    fd, p = tempfile.mkstemp(suffix=suffix); os.close(fd); os.unlink(p)
    return p


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--preset', default='cpa780', choices=list(PRESETS))
    p.add_argument('--out', help='Ausgabepfad der Boot-Disk (Default: Temp, wird verworfen)')
    p.add_argument('--no-verify', action='store_true', help='Boot-Verify überspringen')
    p.add_argument('--quiet', action='store_true', help='nur Ergebnis-Zeilen ausgeben')
    args = p.parse_args()

    cfg   = PRESETS[args.preset]
    drive = cfg['drive']

    def log(*a):
        if not args.quiet:
            print(*a)

    if not os.path.exists(DRIVER):
        print(f"FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver",
              file=sys.stderr); return 2
    if not os.path.exists(cfg['boot']):
        print(f"FEHLER: Boot-Disk fehlt: {cfg['boot']}", file=sys.stderr); return 2

    temps = []   # aufzuräumende Zwischendateien

    def cleanup():
        for t in temps:
            if t and os.path.exists(t):
                os.unlink(t)

    # ── Schritt 1: FORMAT <LW>: (Leerdiskette) → CPABCGEN <LW>: ──────────────
    log(f"[1/2] ({args.preset}) Leerdiskette → FORMAT {cfg['fmt_key']} → CPABCGEN {drive}:")
    cprof  = _profiles('K5601', cfg['b_prof'], cfg['c_prof'])
    target = _blank_path(); temps.append(target)
    if drive == 'B':
        o1, _e1 = run_driver(_script_format_and_gen(cfg), cfg['boot'], target,
                             profiles=cprof)
    else:  # Ziel in C: → B:-Slot mit einer (ebenfalls leeren) Diskette belegen
        bdummy = _blank_path(); temps.append(bdummy)
        o1, _e1 = run_driver(_script_format_and_gen(cfg), cfg['boot'], bdummy,
                             c_path=target, profiles=cprof)

    formatted = 'FORMATIEREN beendet' in o1 and 'SPUR DEFEKT' not in o1
    if not formatted:
        print("  FEHLER: FORMAT.COM meldete kein sauberes 'FORMATIEREN beendet'.  Screen:")
        print(o1); cleanup(); return 1
    ok = ('Anlegen einer neuen Systemdiskette' in o1 and 'OK' in o1
          and 'Abbruch' not in o1 and 'Schreibfehler' not in o1)
    if not ok:
        print("  FEHLER: CPABCGEN meldete kein OK.  Screen:")
        print(o1); cleanup(); return 1

    out = args.out
    if out:
        shutil.copyfile(target, out)
    else:
        out = target   # bleibt Temp
    log(f"  OK — formatiert + CPABCGEN gemeldet; Ziel @OS.COM: {'@OS' in o1}")

    if args.no_verify:
        log(f"\nFertig: {out}  (Boot-Verify übersprungen)")
        print(f"BOOTDISK OK (no-verify): {out}")
        if out != target:
            cleanup()
        return 0

    # ── Schritt 2: Boot-Verify (Ziel-Disk als A:, Ziel-Profil) ───────────────
    log(f"[2/2] Boot-Verify: kalt aus der erzeugten Disk booten (Profil {cfg['prof']})")
    bprof = _profiles(cfg['prof'], cfg['prof'], 'K5601')
    bdisk = _blank_path(); temps.append(bdisk)   # B:-Slot-Dummy: Leerdiskette
    o2, _e2 = run_driver(_script_verify(cfg['needs_clock']), out, bdisk, profiles=bprof)
    booted  = 'CP/A, Version' in o2 and 'TPA ist OK' in o2
    reached = 'A>DIR' in o2 or ': @OS' in o2
    if not args.quiet:
        tail = o2.rsplit('=== SCREEN:', 1)
        if len(tail) == 2:
            print('=== SCREEN:' + tail[1])
    log(f"\n  Kaltstart CP/A: {booted}   A>-Prompt/@OS: {reached}")
    rc = 0 if (booted and reached) else 1
    print(f"BOOTDISK {'OK' if rc == 0 else 'FEHLER'}: preset={args.preset}")
    cleanup()
    return rc


if __name__ == '__main__':
    sys.exit(main())
