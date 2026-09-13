"""
K1520 Emulator - Configuration load/save (YAML)
===============================================

The configuration file is a top-level YAML mapping so further sections can be
added over time.  Sections carried today:

* ``crt``     — picture-tube look (see :class:`~app.ui.screen_widget.CRTParams`)
* ``general`` — general emulator settings (currently the emulation ``speed``)
* ``drive_types`` — the drive-bay configuration: one core ``DriveProfile`` name
  per K5122 slot (``"none"`` = empty slot), restored on the next start
* ``disks``   — the mounted disk images, so they are restored on the next start
* ``window``  — window size, dock layout (which panels are active, their
  arrangement and sizes) **and die Symbolleiste** (``toolbar``: die Aktionsnamen
  in ihrer Reihenfolge, ``""`` = Trennstrich; ``toolbar_style``: Qts
  ``ToolButtonStyle``), so the previous look is restored on the next start.
  Der Leisten-INHALT steht bewusst nicht im ``dock_state``: Qt merkt sich dort
  nur Ort und Sichtbarkeit einer Leiste, nicht, was darin liegt.

Example::

    version: 1
    crt:
      phosphor_on: '#78D41D'
      brightness: 2.5
      ...
    general:
      speed: 1.0
    disks:
      - drive: 0
        path: /home/user/projects/a5120emu_ui/disks/cpa_cpa780_k5601_clock.hfe
        format: cpa780
        write_protect: false
    window:
      width: 1024
      height: 680
      dock_state: <base64 QMainWindow.saveState()>
      toolbar: [konfig_laden, konfig_speichern, '', power, reset]
      toolbar_style: 3

The auto-persisted configuration lives under ``~/.config/k1520emu/config.yaml``
(honouring ``$XDG_CONFIG_HOME``); every exported/loaded file uses the very same
syntax, so a saved config can later be loaded back verbatim.

**Die Auslieferungskonfiguration** (:func:`standard_konfiguration`) ist eine
Datei desselben Aufbaus, die mit dem Programm kommt statt vom Anwender:
``data/default_config.yaml`` im Quellbaum, ``share/k1520emu/`` in einer
Installation.  Sie ist der Zustand nach der Erstinstallation und das Ziel von
*Ansicht ▸ Standard zurücksetzen*.  Sie trägt bewusst KEINEN ``disks``-Abschnitt
— Diskettenpfade sind rechnerspezifisch, und ohne den Abschnitt lässt das
Zurücksetzen die eingelegten Disketten in Ruhe.
"""

import os

import yaml

from app import paths
from app.ui.screen_widget import CRTParams

CONFIG_VERSION = 1


def default_config_dir() -> str:
    """Directory of the auto-persisted configuration (``~/.config/k1520emu``).

    Platform-dependent — the resolution (and the fallback to an existing
    ``~/.config/k1520emu`` on Windows/macOS) lives in :func:`app.paths.config_dir`.
    """
    return str(paths.config_dir())


def default_config_path() -> str:
    """Path of the auto-persisted configuration file."""
    return os.path.join(default_config_dir(), "config.yaml")


def build_config(crt: CRTParams, general: dict, disks: list,
                 window: dict = None, drive_types: list = None) -> dict:
    """Assemble the full configuration dict from the live application state.

    ``drive_types`` is the per-slot list of core ``DriveProfile`` names (one per
    K5122 slot, ``"none"`` = empty slot), so the drive-bay configuration is
    restored on the next start.
    """
    return {
        "version": CONFIG_VERSION,
        "crt": crt.to_dict(),
        "general": dict(general or {}),
        "drive_types": list(drive_types) if drive_types else [],
        "disks": list(disks or []),
        "window": dict(window or {}),
    }


def save_config(path: str, data: dict):
    """Write *data* to *path* as YAML, creating parent directories as needed."""
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)


def load_config(path: str) -> dict:
    """Read a YAML configuration file and return it as a dict (``{}`` if empty)."""
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    return data or {}


def standard_konfiguration() -> dict:
    """Die mitgelieferte Auslieferungskonfiguration (``{}``, wenn es keine gibt).

    Sie liegt als ``default_config.yaml`` neben dem Formatkatalog
    (:func:`app.paths.default_config_file`) und wird an zwei Stellen gebraucht:
    beim ERSTEN Start, solange es noch keine ``config.yaml`` gibt, und bei
    *Ansicht ▸ Standard zurücksetzen*.

    **Ein Fehlschlag ist kein Grund, den Start abzubrechen**: fehlt oder bricht
    die Datei, kommt ein leeres Verzeichnis zurück und der Emulator bleibt bei
    den im Programm eingebauten Vorgaben (CRTParams(), Tempo 1,0,
    ``dt.DEFAULT_DRIVE_TYPES``, Standard-Symbolleiste).  Sie ist eine Beigabe,
    keine Voraussetzung.
    """
    pfad = paths.default_config_file()
    if pfad is None:
        return {}
    try:
        return load_config(str(pfad))
    except Exception as e:           # defekte YAML-Datei, Leserechte …
        print(f"[config] Vorgabe-Konfiguration {pfad} nicht lesbar: {e}")
        return {}
