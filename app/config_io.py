"""
K1520 Emulator - Configuration load/save (YAML)
===============================================

The configuration file is a top-level YAML mapping so further sections can be
added over time.  Today it carries the ``crt`` section (picture-tube look);
later sections (machine, keyboard, ...) slot in alongside it without breaking
older files.

Example::

    version: 1
    crt:
      phosphor_on: '#78D41D'
      phosphor_off: '#050F05'
      brightness: 1.4
      ...
"""

import yaml

from app.ui.screen_widget import CRTParams

CONFIG_VERSION = 1


def save_config(path: str, crt: CRTParams):
    """Write the current configuration to *path* as YAML."""
    data = {
        "version": CONFIG_VERSION,
        "crt": crt.to_dict(),
    }
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)


def load_config(path: str) -> dict:
    """Read a YAML configuration file and return it as a dict (``{}`` if empty)."""
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    return data or {}
