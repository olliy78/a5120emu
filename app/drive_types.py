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
DRIVE_TYPES = [
    ("K5601",    "K5601",         '5,25" DS, 800K'),
    ("K5600.10", "ss_525_40",     '5,25" SS40, 200K'),
    ("K5600.20", "ss_525_80",     '5,25" SS80, 400K'),
    ("MF3200",   "mf3200_8_ss77", '8" FM, 308K'),
    ("MF6400",   "mf6400_8_ds77", '8" MFM, 616K'),
]

NO_DRIVE = DRIVE_NONE          # core name for an empty slot
NO_DRIVE_LABEL = "kein Laufwerk"

# Standard A5120 office configuration: three 5,25" K5601 drives, 4th slot empty.
DEFAULT_DRIVE_TYPES = ["K5601", "K5601", "K5601", NO_DRIVE]

NUM_SLOTS = 4

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
    """Map any stored/None value to a known core name (unknown → K5601)."""
    if not core_name:
        return "K5601"
    if core_name == NO_DRIVE:
        return NO_DRIVE
    return core_name if core_name in _BY_CORE else "K5601"


def normalize_list(types) -> list:
    """Coerce a stored list to exactly :data:`NUM_SLOTS` valid core names."""
    types = list(types or [])
    out = [normalize(types[i]) if i < len(types) else NO_DRIVE
           for i in range(NUM_SLOTS)]
    return out
