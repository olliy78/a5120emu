#!/usr/bin/env python3
"""
format_all.py – Scriptgesteuertes Formatieren + Verifizieren aller nativen
K5601-Diskettenformate (5¼″, 80 Spuren, doppelseitig) im A5120-Emulator.

Der Emulator bootet CP/A von Laufwerk A: und formatiert mit FORMAT.COM (V19.05.89)
in Laufwerk B: der Reihe nach die gewünschten Formate.  Als B:-Ziel dient ein
frisch erzeugtes, leeres Template — .hfe (formatagnostisch) oder .img (mit passender
Geometrie/Größe).  Nach dem Formatieren prüft der Runner den End-Screen auf
`FORMATIEREN beendet` OHNE `SPUR DEFEKT` (mit Verify = Vergleichs-Lesen).

Die eigentliche Emulation macht `tools/format_driver` (Zwei-Disk-Treiber mit
Tastatur-Script).  Dieses Skript erzeugt je Format das Treiber-Script, legt die
Temp-/Ziel-Dateien an und wertet die Ausgabe aus.

  ⚠️  format_driver mountet BEIDE Disks (A: und B:) schreibend.  Für A: wird IMMER
      eine Temp-Kopie der Boot-Disk verwendet, damit das committete Boot-Image nie
      korrumpiert wird.

Verwendung:
  tools/dev.sh tool format_driver          # zuerst den Treiber bauen
  python3 tools/format_all.py --list
  python3 tools/format_all.py 0 1 E                 # nur diese Formate (Schnell-Smoke)
  python3 tools/format_all.py --all --full          # alle §3-Formate, volle 160 Spuren
  python3 tools/format_all.py --all --type img      # als .img (Phase B)
  python3 tools/format_all.py 0 --full --outdir out # Ergebnis-Images ablegen

Menü-Navigation (FORMAT.COM):  Menü #1 (0-3) --X--> Menü #2 (4-7) --Y--> Menü #3 (E-K).

Autor: Olaf Krieger / 2026 / MIT
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE     = os.path.dirname(os.path.abspath(__file__))
ROOT     = os.path.dirname(HERE)
DRIVER   = os.path.join(ROOT, 'build', 'format_driver')
IMG2HFE  = os.path.join(HERE, 'img_to_hfe.py')
BOOT_IMG = os.path.join(ROOT, 'disks', 'cpadisk_autofs_clock_noautoexec.img')

# B:-Ausgangs-Template: eine GÜLTIGE, bereits formatierte Disk (nicht leer!).
# FORMAT.COM liest jede Zielspur VOR dem Neuformatieren (Rotations-/Längenmessung).
# Auf einem gültigen Template finden diese Vorlesungen echte Sektoren → schnell, kein
# Index-Timeout — und der ZVE1↔ZVE2-Bus (BUSRQ) bleibt stabil.  Ein GAP-LEERES Template
# zwingt jede Vorlesung in einen Index-Timeout; das verschiebt das Bus-Arbitrierungs-
# Timing und führt formatabhängig zu einem BUSRQ-Hänger (s. docs/format.md §8.2).
# Beim VOLL-Format wird das ganze Template überschrieben → sauberes Ziel-Format;
# beim Smoke behalten die nicht formatierten Spuren die (lesbaren) Template-Daten.
TEMPLATE_HFE = os.path.join(ROOT, 'disks', 'cpadisk_autofs_clock_noautoexec.hfe')

# ─── §3-Formattabelle (FORMAT.COM V19.05.89, 5¼″ 80-Spur-DS) ─────────────────
#
# key: (menu_nav, format_key, beschreibung, .img-DiskFormat-Name-oder-None)
#   menu_nav   : Tastenfolge, um von Menü #1 zur richtigen Menüseite zu blättern
#   format_key : Auswahltaste in der Formatliste
#   img_format : Name des DiskFormat (core FormatParser) für .img-Ziel; None = (noch)
#                keine passende Geometrie definiert → .img für dieses Format übersprungen
#
# Siehe docs/format.md §3 für Layout/Kapazität je Format.
FORMATS = {
    '0': ([],          '0', 'CP/A        5x1024 Sp.0-159   800k', 'cpa800'),
    '1': ([],          '1', 'CP/A BC     26x128+5x1024     780k', 'cpa780'),
    '2': ([],          '2', 'SCP1715     5x1024 Sp.0-159   780k', 'cpa800'),
    '3': ([],          '3', 'HU Krz      5x1024 Sp.0-159   790k', 'cpa800'),
    '4': (['X'],       '4', 'SCP         16x256 Sp.0-159   624k', 'k5601_16x256'),
    '5': (['X'],       '5', '            16x256 Sp.0-159   624k', 'k5601_16x256'),
    '6': (['X'],       '6', '            26x128 Sp.0-159   520k', 'k5601_26x128'),
    '7': (['X'],       '7', 'ZIK-NK      16x256 Sp.0-153   600k', 'k5601_16x256_77'),
    'E': (['X', 'Y'],  'E', 'MSDOS       9x512  Sp.0-159   720k', 'k5601_9x512'),
    'F': (['X', 'Y'],  'F', 'VORTEX      9x512  Sp.0-159   708k', 'k5601_9x512'),
    'G': (['X', 'Y'],  'G', 'NGB         10x512 Sp.0-159   788k', 'k5601_10x512'),
    'H': (['X', 'Y'],  'H', 'FDC3 4M     5x1024 Sp.0-159   800k', 'cpa800'),
    'I': (['X', 'Y'],  'I', '            5x1024 Sp.0-159   800k', 'cpa800'),
    'J': (['X', 'Y'],  'J', 'MSDOS ITT   10x512 (SCOPY)    800k', 'k5601_10x512'),
    'K': (['X', 'Y'],  'K', 'MSDOS P30/P40 5x1024 (SCOPY)  800k', 'cpa800'),
}


# ─── §3.4-Geometrien (S/T/U/V/W) ─────────────────────────────────────────────
#
# Ein Geometrie-Umschalter (Taste S/T/U/V/W) im Format-Menü stellt die logische
# Geometrie um; die Formatliste wechselt (docs/format.md §3.4).  Jede Geometrie:
#   switch_key, header, {format_key: (submenu_nav, format_key, beschreibung, img_format)}
#
# .img-Regel (RawSectorImage nutzt die PHYSISCHE Kopfposition cur_cyl_ als Offset):
#   - einseitig (S, U, W)      → 1-Kopf-Format
#   - 40-Spur EINZELschritt    → physisch = logisch → 40-Zyl-Format
#   - 40-Spur DOPPELschritt    → physisch = 2×logisch (Zyl 0,2,…,78): KEIN sauberes
#     logisches .img → img_format=None ⇒ .img wird übersprungen, nur .hfe (physisch
#     verify-konsistent).  T/V bzw. U/W teilen dieselbe Formatliste (§3.4).
_SS40 = {'0': 'k5601_ss40_5x1024', '2': 'k5601_ss40_26x128', '3': 'k5601_ss40_26x128',
         '4': 'k5601_ss40_16x256', '5': 'k5601_ss40_16x256', '6': 'k5601_ss40_15x256',
         '7': 'k5601_ss40_5x1024'}
_DS40 = {'0': 'k5601_ds40_5x1024', '3': 'k5601_ds40_26x128', '4': 'k5601_ds40_16x256',
         '5': 'k5601_ds40_16x256', '6': 'k5601_ds40_17x256', '7': 'k5601_ds40_16x256'}
_SS80 = {'0': 'cpa200', '2': 'k5601_ss80_26x128', '3': 'k5601_ss80_26x128',
         '4': 'cpa640', '5': 'cpa640', '7': 'k5601_ss80_9x512'}

def _geo_table(header, keys, img_map):
    """Baut die Formattabelle einer Geometrie (Menü #1, keine Sub-Navigation)."""
    return {k: ([], k, f'{header:22} Format {k}', img_map.get(k)) for k in keys}

GEO_FORMATS = {
    # geo: (switch_key, {format_key: entry})   — img_format=None bei Doppelschritt
    'S': ('S', _geo_table('80 Sp. einseitig',  '023457',   _SS80)),
    'W': ('W', _geo_table('40 Sp. einseitig',  '0234567',  _SS40)),
    'U': ('U', _geo_table('40 Sp. eins. Dopp.', '0234567', {k: None for k in '0234567'})),
    'V': ('V', _geo_table('40 Sp. doppels.',   '034567',   _DS40)),
    'T': ('T', _geo_table('40 Sp. dopp. Dopp.', '034567',  {k: None for k in '034567'})),
}


def resolve_table(geo):
    """Liefert (switch_key, formats_dict) für eine Geometrie ('' = Default 80 DS)."""
    if not geo:
        return (None, FORMATS)
    if geo not in GEO_FORMATS:
        raise ValueError(f"unbekannte Geometrie '{geo}' (S/T/U/V/W)")
    return GEO_FORMATS[geo]


def make_script(entry, switch_key, full, upto_track, dir_verify):
    """Erzeugt das format_driver-Tastatur-Script für ein Format."""
    menu_nav, sel, _desc, _img = entry
    lines = [
        'boot 80', 'type 12:00:00', 'enter',   # Uhrzeit-Prompt beim Kaltstart
        'boot 5', 'type FORMAT', 'enter',       # FORMAT.COM starten
        'boot 30', 'enter',                     # Funktion 0 = Formatieren
        'boot 6', 'type B', 'enter',            # Laufwerk B + Einlege-Quittung
        'boot 10', 'enter',                     # Vergleichs-Lesen = j (mit Verify)
        'boot 8',
    ]
    if switch_key:                              # §3.4: Geometrie umschalten
        lines += [f'type {switch_key}', 'boot 4']
    for k in menu_nav:                          # zur richtigen Menüseite blättern
        lines += [f'type {k}', 'boot 3']
    lines += [f'type {sel}', 'boot 6']          # Format auswählen
    lines += ['enter', 'boot 5']                # von Spur 0
    if full:
        lines += ['enter']                      # bis Spur = letzte
    else:
        lines += [f'type {upto_track}', 'enter']
    lines += ['boot 6', 'type j']               # Warnung bestätigen
    # Formatier-Budget (Mio. Takte): eine Blank-Spur kostet ~90M (Vorlese-Index-
    # Timeouts + Write + Verify).  Voll = großzügig; Smoke skaliert mit dem Bereich.
    budget = 3600 if full else max(400, (upto_track + 3) * 110)
    lines += [f'boot {budget}', 'dump result']
    if dir_verify:
        # Zurück ins CCP und DIR B: als unabhängige Gültigkeitsprüfung.
        #   "Wiederholung mit gleichen Parametern? (j/n)"      -> n
        #   "Rueckkehr in Funktionsauswahl? (j, sonst Ende)"   -> n  (= Ende -> CCP)
        lines += ['type n', 'boot 20',
                  'type n', 'boot 30',
                  'type DIR B:', 'enter', 'boot 100', 'dump dir_b']
    return '\n'.join(lines) + '\n'


def prepare_target(path, filetype, img_format):
    """
    Bereitet das B:-Zielimage vor und liefert den `createB`-Formatnamen für
    format_driver zurück (oder None = B: nur öffnen).

    - .hfe: Kopie eines GÜLTIGEN Templates (kein Gap-Blank — s. §8.2/TEMPLATE_HFE).
            format_driver ÖFFNET diese Datei (createB=None).
    - .img: format_driver LEGT die Datei via `create` NEU an (0xE5 in der Geometrie
            des DiskFormat) → createB = img_format.  Kein Python-Vorbau nötig.
    """
    if filetype == 'hfe':
        shutil.copyfile(TEMPLATE_HFE, path)
        return None
    if not img_format:
        raise ValueError(f"kein .img-DiskFormat für dieses Format definiert")
    # Datei wird von format_driver (create) angelegt; alten Rest entfernen.
    if os.path.exists(path):
        os.remove(path)
    return img_format


def run_format(fmt_key, geo, filetype, full, upto_track, outdir, dir_verify, keep_bad):
    switch_key, table = resolve_table(geo)
    entry = table[fmt_key]
    menu_nav, sel, desc, img_format = entry

    if filetype == 'img' and img_format is None:
        # Doppelschritt-Geometrie (T/U): kein sauberes logisches .img → überspringen.
        return {'key': fmt_key, 'desc': desc, 'status': 'SKIP(.img)',
                'beendet': False, 'defekt': False, 'dir_ok': None,
                'target': '-', 'stdout': '', 'stderr': ''}

    script = make_script(entry, switch_key, full, upto_track, dir_verify)

    with tempfile.NamedTemporaryFile(suffix='.img', delete=False) as ta:
        diskA = ta.name
    shutil.copyfile(BOOT_IMG, diskA)
    tag = (geo or 'DS') + '_' + fmt_key
    diskB = os.path.join(outdir, f'k5601_{tag}.{filetype}')
    createB = prepare_target(diskB, filetype, img_format)   # None → B: öffnen

    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as ts:
        ts.write(script)
        script_path = ts.name

    cmd = [DRIVER, diskA, diskB, script_path]
    if createB is not None:            # .img: format_driver legt B: via create an
        cmd.append(createB)
    env = dict(os.environ, FD_LOGLEVEL=os.environ.get('FD_LOGLEVEL', 'warn'))
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    finally:
        os.unlink(diskA)
        os.unlink(script_path)

    out = proc.stdout
    ok  = 'FORMATIEREN beendet' in out
    bad = 'SPUR DEFEKT' in out or 'DEFEKT' in out
    dir_ok = None
    if dir_verify:
        # In einem gültigen leeren CP/A-Verzeichnis erscheint "No File".
        # (Erscheint NUR in der DIR-B:-Ausgabe, daher genügt die Suche im ganzen out.)
        dir_ok = ('No File' in out) or ('NO FILE' in out.upper())

    status = 'OK' if (ok and not bad) else 'FEHLER'
    if not (ok and not bad) and not keep_bad:
        # Fehlgeschlagenes Ziel für die Analyse behalten (nicht löschen).
        pass

    return {
        'key': fmt_key, 'desc': desc, 'status': status,
        'beendet': ok, 'defekt': bad, 'dir_ok': dir_ok,
        'target': diskB, 'stdout': out, 'stderr': proc.stderr,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('formats', nargs='*', help="Format-Tasten (0-7, E-K); leer = --all")
    p.add_argument('--all', action='store_true', help="alle Formate der Geometrie")
    p.add_argument('--geo', default='', choices=['', 'S', 'T', 'U', 'V', 'W'],
                   help="§3.4-Geometrie-Umschalter (leer = 80-Spur-DS Default)")
    p.add_argument('--type', choices=['hfe', 'img'], default='hfe', help="Zieldateityp")
    p.add_argument('--full', action='store_true',
                   help="volle 160 Spuren (Default: Schnell-Smoke bis --upto)")
    p.add_argument('--upto', type=int, default=2, help="Smoke: bis Spur N (Default 2)")
    p.add_argument('--outdir', default=os.path.join(ROOT, 'out', 'formats'),
                   help="Zielverzeichnis für formatierte Images")
    p.add_argument('--dir-verify', action='store_true',
                   help="nach Voll-Format DIR B: fahren und auf 'No File' prüfen")
    p.add_argument('--list', action='store_true', help="Formattabelle zeigen und Ende")
    p.add_argument('--keep-log', action='store_true', help="Treiber-stdout je Format ablegen")
    args = p.parse_args()

    switch_key, table = resolve_table(args.geo)

    if args.list:
        geo_label = args.geo or '(Default 80-Spur-DS)'
        print(f"Geometrie {geo_label}"
              + (f" — Umschalter '{switch_key}'" if switch_key else "") + "\n")
        print("Taste  Menü-Nav  Beschreibung                          .img-Format")
        for k, (nav, sel, desc, img) in table.items():
            print(f"  {k}     {''.join(nav) or '-':<6}  {desc:<38} {img or '-'}")
        return 0

    if not os.path.exists(DRIVER):
        print(f"FEHLER: {DRIVER} fehlt — zuerst: tools/dev.sh tool format_driver",
              file=sys.stderr)
        return 2

    keys = list(table.keys()) if (args.all or not args.formats) else args.formats
    bad_keys = [k for k in keys if k not in table]
    if bad_keys:
        print(f"FEHLER: unbekannte Format-Tasten für Geometrie '{args.geo or 'DS'}': "
              f"{bad_keys}", file=sys.stderr)
        return 2

    os.makedirs(args.outdir, exist_ok=True)
    print(f"Geometrie: {args.geo or '80-Spur-DS'}  |  Ziel-Typ: .{args.type}  |  "
          f"{'VOLL (0-159)' if args.full else f'SMOKE (0-{args.upto})'}"
          f"  |  Verify: ein  |  outdir: {args.outdir}\n")

    results = []
    for k in keys:
        print(f"[{args.geo or 'DS'}:{k}] {table[k][2]} … ", end='', flush=True)
        r = run_format(k, args.geo, args.type, args.full, args.upto, args.outdir,
                       args.dir_verify and args.full, keep_bad=True)
        results.append(r)
        extra = ''
        if r['dir_ok'] is not None:
            extra = f"  DIR={'No File' if r['dir_ok'] else '??'}"
        print(f"{r['status']}{extra}  -> {os.path.basename(r['target'])}")
        if args.keep_log:
            with open(r['target'] + '.log', 'w') as lf:
                lf.write(r['stdout'])

    print("\n=== Zusammenfassung ===")
    n_ok   = sum(1 for r in results if r['status'] == 'OK')
    n_skip = sum(1 for r in results if r['status'].startswith('SKIP'))
    n_eval = len(results) - n_skip
    for r in results:
        print(f"  {r['key']}: {r['status']:<10} beendet={r['beendet']} defekt={r['defekt']}"
              + (f" dir_ok={r['dir_ok']}" if r['dir_ok'] is not None else ''))
    print(f"\n{n_ok}/{n_eval} Formate OK" + (f" ({n_skip} übersprungen)" if n_skip else ""))
    return 0 if n_ok == n_eval else 1


if __name__ == '__main__':
    sys.exit(main())
