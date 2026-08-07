#!/usr/bin/env python3
"""
format_all.py – Scriptgesteuertes Formatieren + Verifizieren aller nativen
K5601-Diskettenformate (5¼″, 80 Spuren, doppelseitig) im A5120-Emulator.

Der Emulator bootet CP/A von Laufwerk A: und formatiert mit FORMAT.COM (V19.05.89)
in Laufwerk B: (bzw. C:) der Reihe nach die gewünschten Formate.  **Das Ziel ist immer
eine frisch angelegte, ECHTE LEERDISKETTE** (unformatiert, in der Geometrie des
Laufwerks) — der Anwenderfall.  Ausnahme: `--type img`; ein rohes Sektorimage kennt
keinen Zustand „unformatiert" und wird deshalb vorformatiert (0xE5) angelegt.
Nach dem Formatieren prüft der Runner den End-Screen auf `FORMATIEREN beendet`
OHNE `SPUR DEFEKT` (mit Verify = Vergleichs-Lesen).

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
DISKS    = os.path.join(ROOT, 'disks')
BOOT_IMG = os.path.join(DISKS, 'cpadisk_autofs_clock_noautoexec.img')

# ─── Boot-Disketten ──────────────────────────────────────────────────────────
# name: (img-Basename, needs_clock)  needs_clock=True → Uhr-Abfrage beim Kaltstart.
# Die Combo-Disks konfigurieren B:/C: als Fremd-Laufwerkstypen (docs/format.md §11).
BOOT_DISKS = {
    'clock':      ('cpadisk_autofs_clock_noautoexec.img', True),
    'noclk':      ('cpadisk_autofs_noclk_noautoexec.img',  False),
    '5inchCombo': ('cpadisk_autofs_noclock_5inchCombo.img', False),
    '8inchCombo': ('cpadisk_autofs_noclock_8inchCombo.img', False),
}

# ─── Ausgangszustand des Ziels: ECHTE LEERDISKETTE ───────────────────────────
# Alle Läufe starten auf einer unformatierten Leerdiskette — genau das, was ein
# Anwender in das Laufwerk legt.  format_driver bekommt dafür den LEEREN Formatnamen
# ('' → A5120Machine::createDisk legt ein unformatiertes Medium in der Geometrie des
# LAUFWERKS an, CLAUDE.md §8.7).
#
# Historie: früher wurde eine gültige, bereits formatierte Disk als B:-Template kopiert,
# weil eine gap-leere Diskette den Emulator hängen ließ bzw. `Fehler 'U' SPUR DEFEKT`
# provozierte (docs/format.md §8.2).  Beides ist behoben — der markenlose Gap-Fluss
# terminiert über den Index-Timeout, und der FORMAT-Schreibstrom wird vor dem
# Vergleichs-Lesen committet (doc/analyse_format_leerspur.md).  Ein Template würde die
# Tests nur noch schwächen: die Vorlesungen fänden fremde Sektoren statt gar keiner.
BLANK = ''      # createB/FD_DISKC_FMT-Formatname für „unformatierte Leerdiskette"

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


# ─── Native Formattabellen der Fremd-Laufwerkstypen (docs/format.md §3.5/§5) ──
#
# Diese Menüs sind Live-Emulator-Mitschnitte (tools/capture_format_menus.py).  Anders
# als die §3.4-Geometrie-Umschalter (die ein K5601 auf andere Geometrien zwingen)
# präsentiert das echte Laufwerk seine Formatliste NATIV — ohne Umschalt-Buchstaben.
#   img_format=None ⇒ (noch) keine passende RawSectorImage-Geometrie in FormatParser
#   definiert → nur .hfe (formatagnostisch).  Neue Geometrien in builtinFormats() ergänzen.
def _tbl(entries):
    """entries: Liste (key, menu_nav, beschreibung[, img_format])."""
    return {e[0]: (list(e[1]), e[0], e[2], e[3] if len(e) > 3 else None) for e in entries}

# K5600.10 — 5" 40 Sp. einseitig (DPB 10540); Seite 2 (E-L) via 'X'.
K5600_10 = _tbl([
    ('0', [],    '5x1024 Sp.0-39            200k CP/A'),
    ('1', [],    '26x128+5x1024 Sp.2-39     190k CP/A S'),
    ('2', [],    '26x128 Sf.1,7 Sp.0-39     123k'),
    ('3', [],    '26x128 Sf.1,7 Sp.0-39     130k'),
    ('4', [],    '16x256 ohne Sv. Sp.0-39   148k SCP'),
    ('5', [],    '16x256 mit  Sv. Sp.0-39   148k'),
    ('6', [],    '15x256 Sf.1,4,7 Sp.0-39   138k BAP2001'),
    ('7', [],    '5x1024 ohne Sv. Sp.0-39   185k Osborne'),
    ('E', ['X'], '9x512 Sf.1,3,5 Sp.0-39    170k DEC VT'),
    ('F', ['X'], '9x512 1k-BDOS Sp.0-39     171k VPPC'),
    ('G', ['X'], '9x512 Sf.41,42 Sp.0-39    171k Schn. S'),
    ('H', ['X'], '9x512 Sf.c1,c2 Sp.0-39    180k Schn. D'),
    ('I', ['X'], '8x512 2k-BDOS Sp.0-39     156k CP/M 86'),
    ('J', ['X'], '9x512 Sp.0-39             180k {MSDOS}'),
    ('K', ['X'], '8x512 1k-BDOS Sp.0-39     156k IBM CPC'),
    ('L', ['X'], '10x512 Sf.0,1 Sp.0-39     195k KAYPRO'),
])

# K5600.20 — 5" 80 Sp. einseitig (DPB 10580); eine Menüseite, U/W-Umschalter.
K5600_20 = _tbl([
    ('0', [], '5x1024 Sp.0-79            400k CP/A'),
    ('1', [], '26x128+5x1024 Sp.2-79     390k CP/A S'),
    ('2', [], '26x128 Sf.1,7 Sp.0-79     253k'),
    ('3', [], '26x128 Sf.1,7 Sp.0-79     260k'),
    ('4', [], '16x256 ohne Sv. Sp.0-79   308k SCP'),
    ('5', [], '16x256 mit  Sv. Sp.0-79   308k'),
    ('6', [], '16x256 Sf.1,3,5 Sp.0-79   308k'),
    ('7', [], '9x512 Sp.0-79             360k'),
])

# MF3200 — 8" 77 Sp. einfache Dichte (SD/FM, DPB 00877); eine Menüseite.
MF3200 = _tbl([
    ('0', [], '4x1024 Sp.0-76            308k CP/A'),
    ('1', [], '26x128+4x1024 Sp.3-76     296k CP/A BC'),
    ('2', [], '26x128 Sf.1,7 Sp.0-76     243k'),
    ('3', [], '26x128 Sf.1,7 Sp.0-76     250k'),
    ('4', [], '26x128+4x1024 (SCP)       296k SCP'),
    ('5', [], '9x512 Sp.0-76             346k'),
    ('6', [], '26x128+9x512 Sp.2-76      336k'),
    ('7', [], '26x128+16x256 Sp.3-76     296k'),
    ('8', [], '9x512 Sp.0-79 IH Mittweida    -'),
])

# MF6400 / K5602.10 — 8" 77 Sp. doppelte Dichte (DD/MFM, DPB 10877); V=SD-Umschalter.
MF6400 = _tbl([
    ('0', [], '8x1024 Sp.0-76            616k CP/A'),
    ('1', [], '26x128+8x1024 Sp.2-76     600k'),
    ('2', [], '26x128+40x128 Sp.2-76     374k'),
    ('3', [], '40x128 Sf.1,2,3 Sp.0-76   384k'),
    ('4', [], '26x128+8x1024 (SCP)       600k SCP'),
    ('5', [], '16x512 Sp.0-76            616k'),
    ('6', [], '26x128+16x512 Sp.2-76     600k'),
    ('7', [], '9x1024 Sp.0-76            692k'),
])

# (boot_disk_name, drive_letter) → (drivetype_label, format_table)
DRIVE_TABLES = {
    ('5inchCombo', 'B'): ('K5600.10 (5\" 40 SS)',  K5600_10),
    ('5inchCombo', 'C'): ('K5600.20 (5\" 80 SS)',  K5600_20),
    ('8inchCombo', 'B'): ('MF3200 (8\" 77 SD)',    MF3200),
    ('8inchCombo', 'C'): ('MF6400 (8\" 77 DD)',    MF6400),
}


def resolve_table(geo):
    """Liefert (switch_key, formats_dict) für eine Geometrie ('' = Default 80 DS)."""
    if not geo:
        return (None, FORMATS)
    if geo not in GEO_FORMATS:
        raise ValueError(f"unbekannte Geometrie '{geo}' (S/T/U/V/W)")
    return GEO_FORMATS[geo]


def resolve_drive(boot, drive, geo):
    """
    Liefert (boot_img, needs_clock, drive_letter, switch_key, table).
    Für die Combo-Disks + B:/C: wird die native Fremd-Laufwerks-Tabelle gewählt;
    sonst die K5601-Default/Geometrie-Tabelle (§3/§3.4) auf dem gewählten Laufwerk.
    """
    if boot not in BOOT_DISKS:
        raise ValueError(f"unbekannte Boot-Disk '{boot}' ({list(BOOT_DISKS)})")
    img_base, needs_clock = BOOT_DISKS[boot]
    boot_img = os.path.join(DISKS, img_base)
    key = (boot, drive)
    if key in DRIVE_TABLES:
        if geo:
            raise ValueError("--geo gilt nur für das K5601-Default-Laufwerk, "
                             "nicht für native Fremdtypen (deren Menü ist bereits nativ)")
        _label, table = DRIVE_TABLES[key]
        return (boot_img, needs_clock, drive, None, table)
    switch_key, table = resolve_table(geo)
    return (boot_img, needs_clock, drive, switch_key, table)


def make_script(entry, switch_key, full, upto_track, dir_verify,
                drive_letter='B', needs_clock=True):
    """Erzeugt das format_driver-Tastatur-Script für ein Format."""
    menu_nav, sel, _desc, _img = entry
    lines = ['boot 80']
    if needs_clock:
        lines += ['type 12:00:00', 'enter']     # Uhrzeit-Prompt beim Kaltstart
    lines += [
        'boot 5', 'type FORMAT', 'enter',       # FORMAT.COM starten
        'boot 30', 'enter',                     # Funktion 0 = Formatieren
        'boot 6', f'type {drive_letter}', 'enter',  # Laufwerk + Einlege-Quittung
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
                  f'type DIR {drive_letter}:', 'enter', 'boot 100', 'dump dir_b']
    return '\n'.join(lines) + '\n'


def prepare_target(path, filetype, img_format):
    """
    Bereitet das Zielimage vor und liefert den `createB`-Formatnamen für format_driver.

    - **.hfe → IMMER `BLANK`**: eine echte, unformatierte Leerdiskette in der Geometrie
      des Laufwerks.  Das ist der Anwenderfall und die schärfere Prüfung — FORMAT.COM
      muss die Spurlänge auf markenlosem Gap-Fluss messen und darf sich beim
      Vergleichs-Lesen nicht auf Restdaten stützen.  Eine `img_format`-Angabe wird für
      .hfe nicht gebraucht (der Container ist formatagnostisch).
    - **.img → `img_format`** (vorformatiert, Nutzdaten 0xE5): ein rohes Sektorimage hat
      keinen Zustand „unformatiert" — `createDisk` lehnt den leeren Formatnamen für .img
      ab.  Der .img-Pfad prüft deshalb weiterhin das Umformatieren einer gültigen Disk.
    """
    if os.path.exists(path):
        os.remove(path)              # Datei wird von format_driver (create) neu angelegt
    if filetype == 'hfe':
        return BLANK
    if not img_format:
        raise ValueError("kein .img-DiskFormat für dieses Format definiert")
    return img_format


def run_format(fmt_key, boot, drive, geo, filetype, full, upto_track, outdir,
               dir_verify, keep_bad):
    boot_img, needs_clock, drive_letter, switch_key, table = resolve_drive(boot, drive, geo)
    entry = table[fmt_key]
    menu_nav, sel, desc, img_format = entry

    if filetype == 'img' and img_format is None:
        # Kein sauberes logisches .img (Doppelschritt-Geo T/U oder Fremdtyp ohne
        # definierte RawSectorImage-Geometrie) → überspringen, .hfe verwenden.
        return {'key': fmt_key, 'desc': desc, 'status': 'SKIP(.img)',
                'beendet': False, 'defekt': False, 'dir_ok': None,
                'target': '-', 'stdout': '', 'stderr': ''}

    script = make_script(entry, switch_key, full, upto_track, dir_verify,
                         drive_letter=drive_letter, needs_clock=needs_clock)

    with tempfile.NamedTemporaryFile(suffix='.img', delete=False) as ta:
        diskA = ta.name
    shutil.copyfile(boot_img, diskA)
    tag = f'{boot}_{drive_letter}_' + (geo or 'DS') + '_' + fmt_key
    target = os.path.join(outdir, f'fmt_{tag}.{filetype}')
    create_fmt = prepare_target(target, filetype, img_format)

    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as ts:
        ts.write(script)
        script_path = ts.name

    env = dict(os.environ, FD_LOGLEVEL=os.environ.get('FD_LOGLEVEL', 'warn'))
    if drive_letter == 'C':
        # Ziel liegt in Laufwerk C: (FD_DISKC).  Der B:-Slot muss nur BELEGT sein,
        # damit FORMATs Laufwerkswahl sauber durchläuft — er bekommt ebenfalls eine
        # Leerdiskette (in der Geometrie des B:-Laufwerks, also passend zum Combo-BIOS).
        with tempfile.NamedTemporaryFile(suffix='.hfe', delete=False) as tb:
            diskB = tb.name
        os.remove(diskB)               # format_driver legt sie via createB neu an
        env['FD_DISKC']     = target
        env['FD_DISKC_FMT'] = create_fmt
        cmd = [DRIVER, diskA, diskB, script_path, BLANK]
    else:
        diskB = target
        cmd = [DRIVER, diskA, diskB, script_path, create_fmt]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    finally:
        os.unlink(diskA)
        os.unlink(script_path)
        if drive_letter == 'C':
            os.unlink(diskB)
    diskB = target   # für Ergebnis-Reporting

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
    p.add_argument('--boot', default='clock', choices=list(BOOT_DISKS),
                   help="Boot-Disk (clock/noclk/5inchCombo/8inchCombo; §11)")
    p.add_argument('--drive', default='B', choices=['B', 'C'],
                   help="Ziel-Laufwerk; bei Combo-Disks bestimmt es den Laufwerkstyp")
    p.add_argument('--geo', default='', choices=['', 'S', 'T', 'U', 'V', 'W'],
                   help="§3.4-Geometrie-Umschalter (nur K5601-Default-Laufwerk)")
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

    _bi, _nc, drive_letter, switch_key, table = resolve_drive(args.boot, args.drive, args.geo)
    dtype = DRIVE_TABLES.get((args.boot, args.drive), (None, None))[0]

    if args.list:
        label = dtype or ('Geometrie ' + (args.geo or '(Default 80-Spur-DS)'))
        print(f"Boot={args.boot}  Laufwerk {drive_letter}:  →  {label}"
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
        print(f"FEHLER: unbekannte Format-Tasten für {dtype or ('Geometrie ' + (args.geo or 'DS'))}: "
              f"{bad_keys}", file=sys.stderr)
        return 2

    os.makedirs(args.outdir, exist_ok=True)
    print(f"Boot: {args.boot}  |  Laufwerk {drive_letter}: ({dtype or ('K5601 ' + (args.geo or '80-DS'))})"
          f"  |  Ziel-Typ: .{args.type}  |  "
          f"{'VOLL' if args.full else f'SMOKE (0-{args.upto})'}"
          f"  |  Verify: ein  |  outdir: {args.outdir}\n")

    results = []
    for k in keys:
        print(f"[{args.boot}/{drive_letter}:{k}] {table[k][2]} … ", end='', flush=True)
        r = run_format(k, args.boot, args.drive, args.geo, args.type, args.full,
                       args.upto, args.outdir, args.dir_verify and args.full, keep_bad=True)
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
