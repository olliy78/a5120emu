#!/usr/bin/env python3
"""Alle Diskettenformate aller drei Systeme als `.hfe` erzeugen — und vermessen.

Zweck: den Formatkatalog `data/formats.yaml` **empirisch** vervollständigen.  Jedes
Format wird vom Originalprogramm des jeweiligen Betriebssystems im Emulator
geschrieben, danach mit `k1520disktool measure` vermessen und gegen den Katalog
gehalten.  Was nicht passt, ist ein fehlender Eintrag — und die Messung ist zugleich
seine Vorlage.

    CP/A   FORMAT.COM (V19.05.89)   — interaktives Menü; das Laufwerk bestimmt die
                                      Formatliste.  Fremde Laufwerkstypen kommen über
                                      Combo-Bootdisketten ins Spiel (B:/C: sind dort
                                      K5600.10/.20 bzw. MF3200/MF6400).
    SCPX   INIT.COM V1.5             — 5 Formate (0-4); MODF stellt sie je Laufwerk ein.
    UDOS   FORMAT + SET DISKCON      — der Laufwerkstyp bestimmt das Format;
                                      Sektorlänge ≠ 128 ist bei UDOS ein *Build*,
                                      kein Laufzeitschalter (udos_diskettenformat §12.3).

Aufrufe::

    make_all_formats.py --list                 # Matrix zeigen, nichts tun
    make_all_formats.py --system cpa           # nur CP/A formatieren
    make_all_formats.py --all -j 8             # alles, parallel
    make_all_formats.py --measure-only         # vorhandene Abbilder nur vermessen

Ergebnis: `out/formats/<system>/<tag>.hfe` plus ein Bericht, welche Geometrien der
Katalog noch nicht kennt.
"""

import argparse
import concurrent.futures as futures
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, HERE)

import format_all as fa                                        # noqa: E402

DRIVER   = os.environ.get('FORMAT_DRIVER', os.path.join(ROOT, 'build', 'format_driver'))
DISKTOOL = os.environ.get('DISKTOOL', os.path.join(ROOT, 'build', 'k1520disktool'))
DISKS    = os.path.join(ROOT, 'tests', 'fixtures', 'disks')
OUTDIR   = os.path.join(ROOT, 'out', 'formats')

# ─── Laufwerksprofile des Emulators ──────────────────────────────────────────
# FD_PROFILES setzt die Bestückung der vier K5122-Steckplätze.  Bei CP/A macht das
# die Combo-Bootdiskette selbst (ihr BIOS kennt die Typen); SCPX und UDOS brauchen
# die Bestückung von außen.
PROFILE = {
    'K5601':    'K5601',      # 5,25" 80 Spuren DS
    'K5600.10': 'K5600.10',   # 5,25" 40 Spuren SS
    'K5600.20': 'K5600.20',   # 5,25" 80 Spuren SS
    'MF3200':   'MF3200',     # 8" 77 Spuren FM
    'MF6400':   'MF6400',     # 8" 77 Spuren MFM
}


# ═════════════════════════════════════════════════════════════════════════════
# CP/A — die Matrix steht bereits in format_all.py (Live-Mitschnitte der Menüs)
# ═════════════════════════════════════════════════════════════════════════════

def cpa_matrix():
    """(tag, argv-Liste für format_all.py) je Kombination."""
    aufgaben = []
    for boot, drive, geo, key in fa.format_matrix():
        tag = f"cpa_{boot}_{drive}_{geo or 'DS'}_{key}"
        argv = [key, '--boot', boot, '--drive', drive, '--type', 'hfe', '--full']
        if geo:
            argv += ['--geo', geo]
        aufgaben.append((tag, argv))
    return aufgaben


def run_cpa(tag, argv, outdir):
    """Ein CP/A-Format über format_all.py erzeugen."""
    ziel = os.path.join(outdir, 'cpa')
    os.makedirs(ziel, exist_ok=True)
    r = subprocess.run([sys.executable, os.path.join(HERE, 'format_all.py')]
                       + argv + ['--outdir', ziel],
                       capture_output=True, text=True, timeout=900)
    # format_all.py benennt selbst: fmt_<boot>_<drive>_<geo|DS>_<key>.hfe
    teile = tag.split('_')
    datei = os.path.join(ziel, f"fmt_{'_'.join(teile[1:])}.hfe")
    ok = r.returncode == 0 and os.path.exists(datei)
    return {'tag': tag, 'ok': ok, 'file': datei if os.path.exists(datei) else '',
            'log': r.stdout + r.stderr}


# ═════════════════════════════════════════════════════════════════════════════
# SCPX — INIT.COM V1.5, 5 Formate; das physische Laufwerk kommt aus FD_PROFILES
# ═════════════════════════════════════════════════════════════════════════════
#
# Die Formatliste ist ein Live-Mitschnitt (INIT B: auf scpx17_cpa780_k5601.hfe):
#     0 = DD-DS  16* 256  (DEFAULT)      3 = DD-DS   5*1024
#     1 = DD-SS  16* 256                 4 = DD-SS   5*1024
#     2 = DD-SS  26* 128
SCPX_BOOT = os.path.join(DISKS, 'scpx17_cpa780_k5601.hfe')
SCPX_FORMATS = {
    '0': 'DD-DS 16x256',
    '1': 'DD-SS 16x256',
    '2': 'DD-SS 26x128',
    '3': 'DD-DS 5x1024',
    '4': 'DD-SS 5x1024',
}
# Auf welchen Laufwerken wird geprüft?  B: ist das Ziel; sein Typ kommt aus dem Profil.
SCPX_DRIVES = ['K5601', 'K5600.20', 'K5600.10', 'MF6400', 'MF3200']


def scpx_script(fmt_key):
    """INIT B: mit dem gewählten Format fahren.

    Die Promptfolge ist die des echten INIT 1520 V1.5 (nachgestellt aus
    tests/system/test_scpx_init.cpp und am Bildschirm nachgeprüft)::

        PLEASE ENTER DRIVE NAME:        → B
        … HIT <ENTER> FOR DEFAULT:      → Formatziffer
        PLACE DISK … PRESS <ENTER>      → nur ENTER
        ALL FILES … SCRATCHED (Y/N):    → Y
        FORMATTING COMPLETE / ONCE MORE → N
    """
    return "\n".join([
        "boot 90",
        "type INIT", "enter", "run 20",
        "type B", "enter", "run 60",
        f"type {fmt_key}", "enter", "run 30",
        "enter", "run 30",                 # PLACE DISK … PRESS <ENTER>
        "type Y", "enter",                 # Scratch bestätigen
        "run 900",                         # 160 Spuren formatieren + verify
        "dump init-fertig",
        "type N", "enter", "run 20",       # ONCE MORE? → N
    ]) + "\n"


# ═════════════════════════════════════════════════════════════════════════════
# UDOS — FORMAT; das Format kommt aus SET DISKCON (udos_diskettenformat §12.3)
# ═════════════════════════════════════════════════════════════════════════════
#
# Nur das TYP-Nibble wirkt: FORMAT.COM formatiert immer 26x128 und wählt daraus nur
# die Spurzahl (Typ 3 → 40 Spuren, sonst 77).  Sektorlänge ≠ 128 ist bei UDOS ein
# Build, kein Laufzeitschalter — die zugehörigen Typen sind hier bewusst NICHT
# aufgeführt, sie erzeugen dokumentiert defekte Spuren.
UDOS_BOOT = os.path.join(DISKS, 'udos_boot_scp.hfe')
UDOS_TYPES = {
    '41': ('K5600.20', '5,25" 80 Spuren SS, 77 Spuren 26x128'),
    '31': ('K5600.10', '5,25" 40 Spuren,    40 Spuren 26x128'),
    '51': ('K5601',    '5,25" 80 Spuren DS'),
    '41m': ('MF6400',  '8"-Laufwerk mit 5,25"-Typ — laut §12.3 der Weg, der geht'),
}


def udos_script(diskcon):
    """SET DISKCON setzen und B: formatieren.  UDOS-Konsole KLEIN tippen (§14.2)."""
    typ = diskcon.rstrip('m')
    return "\n".join([
        "run 120",                              # Datumsabfrage abwarten
        "type 150388",                          # formatiertes Feld, kein ENTER
        "run 60",
        f"type set diskcon=41 {typ}", "enter", "run 40",
        "type format", "enter", "run 40",
        "type n", "enter", "run 20",            # SYSTEMDISK? → n
        "type 1", "enter", "run 20",            # DRIVE? → 1 (= B:)
        "type testdisk", "enter", "run 20",     # ID?
        "type y", "enter",                      # READY? → y
        "run 900",
        "dump format-fertig",
    ]) + "\n"


def run_driver(tag, boot_disk, script_text, outdir, unterordner, profiles=None,
               timeout=1200):
    """format_driver mit eigenem Skript fahren; B: entsteht als Leerdiskette.

    **Die Bootdiskette wird kopiert.**  `format_driver` mountet Laufwerk A:
    SCHREIBEND — das Gastsystem darf darauf schreiben, und beim Beenden wird
    zurueckgeschrieben.  Wer hier die committete Fixture direkt uebergibt,
    veraendert sie (einmal passiert: SCPX INIT blaehte
    tests/fixtures/disks/scpx17_cpa780_k5601.hfe von 2036 auf 2092 KB auf und der
    Boot-Test fiel um).  Deshalb je Lauf eine eigene Kopie.
    """
    ziel = os.path.join(outdir, unterordner)
    os.makedirs(ziel, exist_ok=True)
    b_datei = os.path.join(ziel, f"{tag}.hfe")
    skript = os.path.join(ziel, f"{tag}.fd")

    boot_kopie = os.path.join(ziel, f"{tag}_boot.hfe")
    shutil.copy(boot_disk, boot_kopie)
    boot_disk = boot_kopie
    with open(skript, 'w') as f:
        f.write(script_text)

    if os.path.exists(b_datei):
        os.remove(b_datei)

    env = dict(os.environ)
    if profiles:
        env['FD_PROFILES'] = profiles
    try:
        r = subprocess.run([DRIVER, boot_disk, b_datei, skript, ''],
                           capture_output=True, text=True, timeout=timeout, env=env)
        log = r.stdout + r.stderr
        ok = r.returncode == 0 and os.path.exists(b_datei)
    except subprocess.TimeoutExpired:
        log, ok = 'TIMEOUT', False
    finally:
        if os.path.exists(boot_kopie):
            os.remove(boot_kopie)
    return {'tag': tag, 'ok': ok, 'file': b_datei if os.path.exists(b_datei) else '',
            'log': log}


# ═════════════════════════════════════════════════════════════════════════════
# Vermessen
# ═════════════════════════════════════════════════════════════════════════════

def measure(path):
    """`k1520disktool measure --json` — Geometrie und passende Katalogeinträge."""
    r = subprocess.run([DISKTOOL, 'measure', path, '--json'],
                       capture_output=True, text=True)
    if not r.stdout.strip():
        return None
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        return None


def spurbereiche(messung):
    """Gemessene Spuren zu Bereichen zusammenfassen (wie ein `tracks:`-Eintrag).

    **Luecken brechen den Bereich.**  Ohne das saehe eine Doppelschritt-Diskette
    (nur jeder zweite Zylinder beschrieben, §3.4-Geometrien T/U) wie ein
    zusammenhaengender Bereich aus — und der daraus abgeleitete Katalogeintrag waere
    schlicht falsch.  `messung['tracks']` enthaelt nur FORMATIERTE Spuren; eine
    fehlende (cyl, head) ist also eine echte Luecke.
    """
    nheads = max((t['head'] for t in messung['tracks']), default=0) + 1

    def folgt(a, b):
        """Ist b der unmittelbare Nachfolger von a in Layout-Reihenfolge?"""
        if a[1] + 1 < nheads:
            return b == (a[0], a[1] + 1)
        return b == (a[0] + 1, 0)

    bereiche = []
    for t in messung['tracks']:
        schluessel = (t['sectors'], t['size'], t['first_id'], t['encoding'])
        hier = (t['cyl'], t['head'])
        if bereiche and bereiche[-1][0] == schluessel and folgt(bereiche[-1][2], hier):
            bereiche[-1][2] = hier
        else:
            bereiche.append([schluessel, hier, hier])
    return bereiche


def bereich_text(b):
    (sec, size, first, enc), (c0, h0), (c1, h1) = b
    cyls = f"{c0}" if c0 == c1 else f"{c0}-{c1}"
    heads = f"{h0}" if h0 == h1 else f"{h0}-{h1}"
    extra = f", first_sector: {first}" if first != 1 else ""
    return (f"      - {{ cyls: {cyls}, heads: {heads}, sectors: {sec}, "
            f"size: {size}{extra} }}   # {enc}")


def bericht(ergebnisse, outdir):
    """Alle erzeugten Abbilder vermessen und die unbekannten Geometrien auflisten."""
    bekannt, unbekannt, kaputt = [], [], []
    for e in ergebnisse:
        if not e['ok'] or not e['file']:
            kaputt.append(e)
            continue
        m = measure(e['file'])
        if not m or not m['tracks']:
            kaputt.append(e)
            continue
        e['messung'] = m
        (bekannt if m['matches'] else unbekannt).append(e)

    print("\n" + "=" * 74)
    print(f"{len(bekannt)} erkannt · {len(unbekannt)} OHNE Katalogeintrag · "
          f"{len(kaputt)} nicht auswertbar")
    print("=" * 74)

    if unbekannt:
        print("\n── Geometrien, die data/formats.yaml noch nicht kennt ──\n")
        # Gleiche Geometrien zusammenfassen: viele Formatvarianten teilen sie.
        nach_geo = {}
        for e in unbekannt:
            schluessel = tuple(tuple(b[0]) + b[1] + b[2] for b in spurbereiche(e['messung']))
            nach_geo.setdefault(schluessel, []).append(e)
        for i, (_, gruppe) in enumerate(sorted(nach_geo.items(), key=lambda x: -len(x[1])), 1):
            beispiel = gruppe[0]
            print(f"  [{i}] {len(gruppe)}× — z. B. {beispiel['tag']}")
            for b in spurbereiche(beispiel['messung']):
                print(bereich_text(b))
            if len(gruppe) > 1:
                print(f"      auch: {', '.join(g['tag'] for g in gruppe[1:6])}"
                      + (" …" if len(gruppe) > 6 else ""))
            print()

    if kaputt:
        print("\n── nicht auswertbar ──")
        for e in kaputt:
            print(f"  {e['tag']}")

    with open(os.path.join(outdir, 'bericht.json'), 'w') as f:
        json.dump([{k: v for k, v in e.items() if k != 'log'} for e in ergebnisse],
                  f, indent=1)
    return len(unbekannt)


# ═════════════════════════════════════════════════════════════════════════════

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--system', choices=['cpa', 'scpx', 'udos'], action='append',
                   help="nur dieses System (mehrfach möglich); ohne Angabe: alle")
    p.add_argument('--all', action='store_true', help="alle Systeme (Vorgabe)")
    p.add_argument('-j', '--jobs', type=int, default=4, help="parallele Läufe")
    p.add_argument('--outdir', default=OUTDIR)
    p.add_argument('--list', action='store_true', help="Matrix zeigen, nichts tun")
    p.add_argument('--measure-only', action='store_true',
                   help="nichts formatieren, nur vorhandene Abbilder vermessen")
    args = p.parse_args()

    systeme = args.system or ['cpa', 'scpx', 'udos']

    aufgaben = []
    if 'cpa' in systeme:
        for tag, argv in cpa_matrix():
            aufgaben.append(('cpa', tag, argv))
    if 'scpx' in systeme:
        for drv in SCPX_DRIVES:
            for key, desc in SCPX_FORMATS.items():
                aufgaben.append(('scpx', f"scpx_{drv}_{key}", (drv, key, desc)))
    if 'udos' in systeme:
        for typ, (drv, desc) in UDOS_TYPES.items():
            aufgaben.append(('udos', f"udos_{drv}_{typ}", (drv, typ, desc)))

    if args.list:
        for sys_, tag, _ in aufgaben:
            print(f"{sys_:5} {tag}")
        print(f"\n{len(aufgaben)} Kombinationen")
        return 0

    os.makedirs(args.outdir, exist_ok=True)

    if args.measure_only:
        ergebnisse = []
        for unter in ('cpa', 'scpx', 'udos'):
            d = os.path.join(args.outdir, unter)
            if not os.path.isdir(d):
                continue
            for f in sorted(os.listdir(d)):
                if f.endswith('.hfe'):
                    ergebnisse.append({'tag': f[:-4], 'ok': True,
                                       'file': os.path.join(d, f), 'log': ''})
        return 0 if bericht(ergebnisse, args.outdir) == 0 else 0

    print(f"{len(aufgaben)} Kombinationen, {args.jobs} parallel → {args.outdir}\n")
    ergebnisse = []

    def starte(eintrag):
        sys_, tag, daten = eintrag
        if sys_ == 'cpa':
            return run_cpa(tag, daten, args.outdir)
        if sys_ == 'scpx':
            drv, key, _ = daten
            return run_driver(tag, SCPX_BOOT, scpx_script(key), args.outdir, 'scpx',
                              profiles=f"K5601,{PROFILE[drv]},K5601,none")
        drv, typ, _ = daten
        return run_driver(tag, UDOS_BOOT, udos_script(typ), args.outdir, 'udos',
                          profiles=f"K5601,{PROFILE[drv]},K5601,none")

    with futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        laufend = {pool.submit(starte, a): a for a in aufgaben}
        fertig = 0
        for fut in futures.as_completed(laufend):
            r = fut.result()
            ergebnisse.append(r)
            fertig += 1
            print(f"[{fertig:3}/{len(aufgaben)}] {'OK  ' if r['ok'] else 'FEHL'} {r['tag']}",
                  flush=True)

    return 0 if bericht(ergebnisse, args.outdir) >= 0 else 1


if __name__ == '__main__':
    sys.exit(main())
