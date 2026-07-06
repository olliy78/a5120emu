#!/usr/bin/env python3
"""
make_bootdisk.py – Aus einer FORMATIERTEN CP/A-Leerdiskette mit CPABCGEN.COM eine
BOOTfähige Systemdiskette machen (schreibt Boot-Lader + @OS.COM auf die Systemspuren)
und den Boot direkt im A5120-Emulator verifizieren.

Presets (--preset) — je Preset ein Laufwerkstyp/Format:

  cpa780        5¼″-MFM, cpa780 (26×128 Sys + 5×1024 Daten, doppelseitig). Uhr-Boot-Disk.
  mf3200_fmt7   8″-SD/FM  MF3200 F7: 26×128 FM Sp.0-2 + 16×256 FM Sp.3-76   (296k)
  mf3200_fmt1   8″-SD/FM  MF3200 F1: 26×128 FM Sp.0-2 +  4×1024 FM Sp.3-76  (296k)
  mf6400_fmt1   8″-DD/MFM MF6400 F1: 26×128 Sp.0-1 + 8×1024 Sp.2-76         (600k, C:)
  k5600_10_fmt1 5¼″-40-SS K5600.10 F1: 26×128 Sp.0-1 + 5×1024 Sp.2-39       (190k)
  k5600_20_fmt1 5¼″-80-SS K5600.20 F1: 26×128 Sp.0-1 + 5×1024 Sp.2-79       (390k, C:)

Ablauf (alles über tools/format_driver, den Zwei-Disk-Tastatur-Treiber):

  0. (einseitige Formate) Leere Ziel-Vorlage per `mk_disk_template` ERZEUGEN — FORMAT.COM
     kann eine gap-leere .hfe nicht direkt formatieren (Gap-Blank-Hänger, docs/format.md
     §8.2/§8.6); das Tool schreibt eine gültig vorformatierte einseitige Diskette.
     (cpa780 nutzt die committete, doppelseitige `disks/empty_cpa780.hfe`.)
  1. cpabcgen : boote die passende Combo-/Uhr-Boot-Disk als A: (mit CPABCGEN.COM +
     @OS.COM), lege die Ziel-Vorlage als B:/C: ein und fahre `CPABCGEN <LW>:`.
     Der Slot bekommt per FD_PROFILES das zum Combo-BIOS passende physische Laufwerk.
  2. verify   : mounte die erzeugte Disk als A: (Ziel-Profil) und boote kalt daraus;
     prüfe, dass CP/A hochkommt (Config-Screen + A>/@OS.COM).

  ⚠️  format_driver mountet alle Disks schreibend → es werden IMMER Temp-Kopien benutzt.

Verwendung:
  tools/dev.sh tool format_driver
  tools/dev.sh tool mk_disk_template
  python3 tools/cpa_tools/make_bootdisk.py --preset mf6400_fmt1 --quiet

Env:
  FORMAT_DRIVER      Pfad zu format_driver (Default build/format_driver)
  MK_DISK_TEMPLATE   Pfad zu mk_disk_template (Default build/mk_disk_template)

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
DISKS  = os.path.join(ROOT, 'disks')
DRIVER = os.environ.get('FORMAT_DRIVER',   os.path.join(ROOT, 'build', 'format_driver'))
MKTMPL = os.environ.get('MK_DISK_TEMPLATE', os.path.join(ROOT, 'build', 'mk_disk_template'))

BOOT_CLOCK      = os.path.join(DISKS, 'cpadisk_autofs_clock_noautoexec.img')
BOOT_8INCHCOMBO = os.path.join(DISKS, 'cpadisk_autofs_noclock_8inchCombo.img')
BOOT_5INCHCOMBO = os.path.join(DISKS, 'cpadisk_autofs_noclock_5inchCombo.img')

# Template-Spezifikation für mk_disk_template: (fm|mfm, cyls, sys_cyls, sys_nsec,
# sys_size, data_nsec, data_size).  None = committete Vorlage (cpa780, doppelseitig).
#
# Preset-Felder:
#   boot        A:-Boot-Disk (CPABCGEN.COM + @OS.COM)
#   drive       Ziel-Laufwerksbuchstabe (B/C)
#   prof        DriveProfile des Ziel-Laufwerks (für Boot-Verify: Ziel liegt auf A:)
#   b_prof      DriveProfile des B:-Slots während des CPABCGEN-Schritts (Combo-B:-Typ)
#   c_prof      DriveProfile des C:-Slots während des CPABCGEN-Schritts (Combo-C:-Typ)
#   needs_clock Kaltstart fragt Uhrzeit ab
#   template    Ziel-Template-Spec (mk_disk_template) oder None (→ committed)
#   b_dummy     (nur drive=C) Template-Spec für den B:-Slot-Dummy (Combo-B:-Typ)
#   committed   (nur template=None) Pfad der committeten Vorlage
PRESETS = {
    'cpa780': dict(
        boot=BOOT_CLOCK, drive='B', prof='K5601', b_prof='K5601', c_prof='K5601',
        needs_clock=True, template=None, b_dummy=None,
        committed=os.path.join(DISKS, 'empty_cpa780.hfe'),
    ),
    'mf3200_fmt7': dict(
        boot=BOOT_8INCHCOMBO, drive='B', prof='mf3200_8_ss77',
        b_prof='mf3200_8_ss77', c_prof='mf6400_8_ss77', needs_clock=False,
        template=('fm', 77, 3, 26, 128, 16, 256), b_dummy=None,
    ),
    'mf3200_fmt1': dict(
        boot=BOOT_8INCHCOMBO, drive='B', prof='mf3200_8_ss77',
        b_prof='mf3200_8_ss77', c_prof='mf6400_8_ss77', needs_clock=False,
        template=('fm', 77, 3, 26, 128, 4, 1024), b_dummy=None,
    ),
    'mf6400_fmt1': dict(
        boot=BOOT_8INCHCOMBO, drive='C', prof='mf6400_8_ss77',
        b_prof='mf3200_8_ss77', c_prof='mf6400_8_ss77', needs_clock=False,
        # 8″-DD ist MISCHDICHTE (System-34): FM-Systemspuren + MFM-Datenspuren.
        template=('fm/mfm', 77, 2, 26, 128, 8, 1024),
        b_dummy=('fm', 77, 3, 26, 128, 16, 256),   # gültige MF3200-FM-Disk für B:
    ),
    'k5600_10_fmt1': dict(
        boot=BOOT_5INCHCOMBO, drive='B', prof='ss_525_40',
        b_prof='ss_525_40', c_prof='ss_525_80', needs_clock=False,
        template=('mfm', 40, 2, 26, 128, 5, 1024), b_dummy=None,
    ),
    'k5600_20_fmt1': dict(
        boot=BOOT_5INCHCOMBO, drive='C', prof='ss_525_80',
        b_prof='ss_525_40', c_prof='ss_525_80', needs_clock=False,
        template=('mfm', 80, 2, 26, 128, 5, 1024),
        b_dummy=('mfm', 40, 2, 26, 128, 5, 1024),  # gültige K5600.10-Disk für B:
    ),
}


def gen_template(spec):
    """Erzeugt via mk_disk_template eine Temp-HFE und liefert deren Pfad."""
    fd, path = tempfile.mkstemp(suffix='.hfe'); os.close(fd)
    enc, cyls, sysc, snsec, ssz, dnsec, dsz = spec
    subprocess.run([MKTMPL, path, enc, str(cyls), str(sysc),
                    str(snsec), str(ssz), str(dnsec), str(dsz)],
                   check=True, stdout=subprocess.DEVNULL)
    return path


def _profiles(a, b, c, d='K5601'):
    return f'{a},{b},{c},{d}'


def _script_cpabcgen(drive, needs_clock):
    lines = ['boot 90']
    if needs_clock:
        lines += ['type 12:00:00', 'enter']
    lines += ['boot 8', f'type CPABCGEN {drive}:', 'enter',
              'boot 80', 'dump cpabcgen',
              f'type DIR {drive}:', 'enter', 'boot 120', 'dump dir_target']
    return '\n'.join(lines) + '\n'


def _script_verify(needs_clock):
    lines = ['boot 100', 'dump cold_boot']
    if needs_clock:
        lines += ['type 12:00:00', 'enter', 'boot 30']
    lines += ['type DIR', 'enter', 'boot 120', 'dump dir_a']
    return '\n'.join(lines) + '\n'


def run_driver(script, disks, profiles=None, target_slot='B'):
    """
    format_driver mit Temp-Kopien laufen lassen.
      disks: dict {0:pathA, 1:pathB, 2:pathC(optional)}
      target_slot: 'B' oder 'C' — dessen Temp-Pfad (mit den Schreibergebnissen)
                   wird zurückgegeben.
    Liefert (stdout, stderr, result_path).  result_path muss vom Aufrufer gelöscht werden.
    """
    tmp = {}
    for slot, path in disks.items():
        fd, tp = tempfile.mkstemp(suffix=os.path.splitext(path)[1]); os.close(fd)
        shutil.copyfile(path, tp)
        tmp[slot] = tp
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as ts:
        ts.write(script); tS = ts.name

    env = dict(os.environ)
    if profiles:
        env['FD_PROFILES'] = profiles
    if 2 in tmp:
        env['FD_DISKC'] = tmp[2]

    cmd = [DRIVER, tmp[0], tmp[1], tS]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    finally:
        os.unlink(tS)
        os.unlink(tmp[0])
        # tmp[1]/tmp[2] je nach target_slot behalten/löschen:
        keep = 1 if target_slot == 'B' else 2
        for slot, tp in tmp.items():
            if slot != 0 and slot != keep:
                os.unlink(tp)
    return proc.stdout, proc.stderr, tmp[1 if target_slot == 'B' else 2]


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

    for tool in (DRIVER, MKTMPL):
        if not os.path.exists(tool) and cfg['template'] is not None and tool == MKTMPL:
            print(f"FEHLER: {tool} fehlt — zuerst: tools/dev.sh tool mk_disk_template",
                  file=sys.stderr); return 2
    if not os.path.exists(DRIVER):
        print(f"FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver",
              file=sys.stderr); return 2
    if not os.path.exists(cfg['boot']):
        print(f"FEHLER: Boot-Disk fehlt: {cfg['boot']}", file=sys.stderr); return 2

    temps = []   # aufzuräumende Zwischen-Templates

    def cleanup():
        for t in temps:
            if t and os.path.exists(t):
                os.unlink(t)

    # ── Ziel-Vorlage (leer, formatiert) bereitstellen ────────────────────────
    if cfg['template'] is None:
        formatted = cfg['committed']
        if not os.path.exists(formatted):
            print(f"FEHLER: committete Vorlage fehlt: {formatted}", file=sys.stderr)
            return 2
    else:
        formatted = gen_template(cfg['template']); temps.append(formatted)

    # ── Schritt 1: CPABCGEN <LW>: ────────────────────────────────────────────
    log(f"[1/2] ({args.preset}) CPABCGEN {drive}:  → {os.path.basename(args.out or '(temp)')}")
    cprof = _profiles('K5601', cfg['b_prof'], cfg['c_prof'])
    if drive == 'B':
        disks = {0: cfg['boot'], 1: formatted}
    else:  # C:  → B:-Slot mit gültigem Combo-B:-Dummy belegen, Ziel via FD_DISKC
        bdummy = gen_template(cfg['b_dummy']); temps.append(bdummy)
        disks = {0: cfg['boot'], 1: bdummy, 2: formatted}
    o1, _e1, written = run_driver(_script_cpabcgen(drive, cfg['needs_clock']),
                                  disks, profiles=cprof, target_slot=drive)
    temps.append(written)
    ok = ('Anlegen einer neuen Systemdiskette' in o1 and 'OK' in o1
          and 'Abbruch' not in o1 and 'Schreibfehler' not in o1)
    if not ok:
        print("  FEHLER: CPABCGEN meldete kein OK.  Screen:")
        print(o1); cleanup(); return 1

    out = args.out
    if out:
        shutil.copyfile(written, out)
    else:
        out = written   # bleibt Temp
    log(f"  OK — CPABCGEN gemeldet; Ziel @OS.COM: {'@OS' in o1}")

    if args.no_verify:
        log(f"\nFertig: {out}  (Boot-Verify übersprungen)")
        print(f"BOOTDISK OK (no-verify): {out}")
        cleanup(); return 0

    # ── Schritt 2: Boot-Verify (Ziel-Disk als A:, Ziel-Profil) ───────────────
    log(f"[2/2] Boot-Verify: kalt aus der erzeugten Disk booten (Profil {cfg['prof']})")
    bprof = _profiles(cfg['prof'], cfg['prof'], 'K5601')
    # B:-Slot-Dummy für den Boot: eine gültige Disk des Ziel-Profils (frische Vorlage).
    if cfg['template'] is None:
        bdisk = cfg['committed']
    else:
        bdisk = gen_template(cfg['template']); temps.append(bdisk)
    o2, _e2, vtmp = run_driver(_script_verify(cfg['needs_clock']),
                               {0: out, 1: bdisk}, profiles=bprof, target_slot='B')
    if vtmp != out:
        temps.append(vtmp)
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
