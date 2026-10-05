"""
K1520 Emulator - Drive-type catalogue
=====================================

Single source of truth for the selectable floppy-drive types, shared by the
settings dropdowns (:class:`~app.ui.settings_widget.SettingsWidget`) and the
drive panels (:class:`~app.ui.drive_widget.DriveWidget`).

Each entry maps a user-facing product name to the core ``DriveProfile`` name that
:func:`k1520_create_configured` expects (see ``core/peripherals/floppy_drive/
drive_profile.cpp``).  ``"none"`` is the special empty-slot value ("kein
Laufwerk"); a slot set to it has no drive wired and takes no disk.
"""

from app.core_binding.k1520 import DRIVE_NONE

# (short product name, core DriveProfile name, one-line description)
# Die Kernprofile heißen seit 2026-08-03 wie die realen Laufwerke — Produktname und
# Kernname sind daher identisch (siehe drive_profile.cpp).
DRIVE_TYPES = [
    ("K5601",    "K5601",    '5,25" DS 80 Spuren, MFM, 800K'),
    ("K5600.10", "K5600.10", '5,25" SS 40 Spuren, MFM, 200K'),
    ("K5600.20", "K5600.20", '5,25" SS 80 Spuren, MFM, 400K'),
    ("MF3200",   "MF3200",   '8" SS 77 Spuren, nur FM, 300K'),
    ("MF6400",   "MF6400",   '8" SS 77 Spuren, FM+MFM, 600K'),
]

# Früher benutzte technische Profilnamen -> heutige Namen.  Gespeicherte
# Konfigurationen (app/config_io.py) tragen noch die alten Werte; ohne diese
# Zuordnung würde normalize() sie still auf K5601 zurücksetzen und damit die
# Laufwerksbestückung des Nutzers verändern.
LEGACY_PROFILE_NAMES = {
    "ss_525_40":     "K5600.10",
    "ss_525_80":     "K5600.20",
    "mf3200_8_ss77": "MF3200",
    "mf6400_8_ss77": "MF6400",
    "mf6400_8_ds77": "MF6400",   # zweiseitiges 8"-Profil; gibt es als HW nicht
    "mfs_525_ds80":  "K5601",    # war ein K5601-Duplikat ohne FM-Lesepfad
}

#: Physische Reichweite je Laufwerkstyp: (Zylinder, Köpfe, Zellrate kbit/s, U/min).
#:
#: Spiegelt die `DriveProfile`-Tabelle des Kerns (core/peripherals/floppy_drive/
#: drive_profile.cpp).  Gebraucht wird sie nur für das **echte** Laufwerk am
#: Greaseweazle (doc/design/14_physische_diskette.md): dort muss die Anwendung dem
#: Kern sagen, wie weit der Kopf fahren kann — bei einer Abbilddatei sagt das die Datei.
GEOMETRIE = {
    "K5601":    (80, 2, 250, 300),
    "K5600.10": (40, 1, 250, 300),
    "K5600.20": (80, 1, 250, 300),
    "MF3200":   (77, 1, 250, 360),   # 8" FM
    "MF6400":   (77, 1, 500, 360),   # 8" MFM
}


def geometrie(core_name: str):
    """(Zylinder, Köpfe, Zellrate, U/min) eines Laufwerkstyps; Vorgabe K5601."""
    return GEOMETRIE.get(normalize(core_name), GEOMETRIE["K5601"])


NO_DRIVE = DRIVE_NONE          # core name for an empty slot
NO_DRIVE_LABEL = "kein Laufwerk"

# Standard A5120 office configuration: three 5,25" K5601 drives, 4th slot empty.
DEFAULT_DRIVE_TYPES = ["K5601", "K5601", "K5601", NO_DRIVE]

NUM_SLOTS = 4

# ── Je Maschine (Programmprofil, app/profil.py) ─────────────────────────────
#: Bestückung ab Werk.  K8915: wie das Gerät des Anwenders — zwei K5601
#: (doc/design/16_k8915.md §6.4), Steckplatz 2/3 leer.
_STANDARD_JE_MASCHINE = {
    "a5120": DEFAULT_DRIVE_TYPES,
    "k8915": ["K5601", "K5601", NO_DRIVE, NO_DRIVE],
    # PRG 710/710-1: zwei K5601 an der AFS K5122 (doc/design/20_prg710.md §3.6).
    "prg710": ["K5601", "K5601", NO_DRIVE, NO_DRIVE],
    # PC 1715: zwei K5601 an der K5122 (doc/design/21_pc1715.md §8.3).
    "pc1715": ["K5601", "K5601", NO_DRIVE, NO_DRIVE],
}
#: Wählbare Typen.  Alle Maschinen bekommen alle Typen: die Laufwerke sind reine
#: `DriveProfile`-Daten im Kern (`builtinDriveProfile`), den Rest (Drehzahl,
#: Zellrate) richtet `laufwerke.cpp` danach aus — es gibt keine maschinenspezifische
#: Sperre.  Ein 8″-Beistellgerät ist an K8915 (K5122 im /WAIT-Zweig), PRG 710 und
#: PC 1715 (DIP S8 „8″ erlaubt") anschliessbar.  Die Tabelle bleibt, damit eine
#: Maschine bei Bedarf wieder eingeschränkt werden kann.
_ALLE_TYPEN = [core for _s, core, _d in DRIVE_TYPES]
_TYPEN_JE_MASCHINE = {
    "a5120": _ALLE_TYPEN,
    "k8915": _ALLE_TYPEN,
    "prg710": _ALLE_TYPEN,
    "pc1715": _ALLE_TYPEN,
}


def default_drive_types(maschine: str = "a5120") -> list:
    """Bestückung ab Werk der Maschine *maschine* (Kopie, je Steckplatz ein Name)."""
    return list(_STANDARD_JE_MASCHINE.get(maschine, DEFAULT_DRIVE_TYPES))


def drive_types_for(maschine: str = "a5120") -> list:
    """Die Einträge aus :data:`DRIVE_TYPES`, die an *maschine* wählbar sind."""
    erlaubt = _TYPEN_JE_MASCHINE.get(maschine, _TYPEN_JE_MASCHINE["a5120"])
    return [e for e in DRIVE_TYPES if e[1] in erlaubt]

_BY_CORE = {core: (short, desc) for short, core, desc in DRIVE_TYPES}


def is_present(core_name: str) -> bool:
    """True if *core_name* denotes a wired drive (not the empty-slot value)."""
    return bool(core_name) and core_name != NO_DRIVE


def combo_label(core_name: str) -> str:
    """Full dropdown label for a core drive name (with description)."""
    if not is_present(core_name):
        return NO_DRIVE_LABEL
    short, desc = _BY_CORE.get(core_name, (core_name, ""))
    return f"{short} ({desc})" if desc else short


def short_label(core_name: str) -> str:
    """Short product name for a core drive name (panel headers)."""
    if not is_present(core_name):
        return NO_DRIVE_LABEL
    return _BY_CORE.get(core_name, (core_name, ""))[0]


def normalize(core_name) -> str:
    """Map any stored/None value to a known core name (unknown → K5601).

    Alte Profilnamen aus gespeicherten Konfigurationen werden dabei auf ihr
    heutiges Gegenstück abgebildet (:data:`LEGACY_PROFILE_NAMES`).
    """
    if not core_name:
        return "K5601"
    if core_name == NO_DRIVE:
        return NO_DRIVE
    if core_name in _BY_CORE:
        return core_name
    return LEGACY_PROFILE_NAMES.get(core_name, "K5601")


def normalize_list(types) -> list:
    """Coerce a stored list to exactly :data:`NUM_SLOTS` valid core names."""
    types = list(types or [])
    out = [normalize(types[i]) if i < len(types) else NO_DRIVE
           for i in range(NUM_SLOTS)]
    return out
