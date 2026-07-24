"""
K1520 Emulator - Configuration load/save (YAML)
===============================================

The configuration file is a top-level YAML mapping so further sections can be
added over time.  Sections carried today:

* ``crt``     — picture-tube look (see :class:`~app.ui.screen_widget.CRTParams`)
* ``general`` — general emulator settings (currently the emulation ``speed``)
* ``disks``   — the mounted disk images, so they are restored on the next start
* ``window``  — window size + dock layout (which panels are active, their
  arrangement and sizes), so the previous look is restored on the next start

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
        path: /home/user/projects/a5120emu_ui/disks/cpa_leer800k.hfe
        format: cpa800
        write_protect: false
    window:
      width: 1024
      height: 680
      dock_state: <base64 QMainWindow.saveState()>

The auto-persisted configuration lives under ``~/.config/k1520emu/config.yaml``
(honouring ``$XDG_CONFIG_HOME``); every exported/loaded file uses the very same
syntax, so a saved config can later be loaded back verbatim.
"""

import os

import yaml

from app.ui.screen_widget import CRTParams

CONFIG_VERSION = 1


def default_config_dir() -> str:
    """Directory of the auto-persisted configuration (``~/.config/k1520emu``)."""
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "k1520emu")


def default_config_path() -> str:
    """Path of the auto-persisted configuration file."""
    return os.path.join(default_config_dir(), "config.yaml")


def build_config(crt: CRTParams, general: dict, disks: list,
                 window: dict = None) -> dict:
    """Assemble the full configuration dict from the live application state."""
    return {
        "version": CONFIG_VERSION,
        "crt": crt.to_dict(),
        "general": dict(general or {}),
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
