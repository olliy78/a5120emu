"""Das Handbuch des DiskTool — Fenster und Textsatz stehen in :mod:`app.ui_help`.

Emulator und DiskTool zeigen dasselbe Fenster (Inhaltsverzeichnis, Suche,
Markdown ohne Bauschritt); hier steht nur, WELCHE Datei darin liegt und wie das
Fenster heisst.  Die eingeführten Namen bleiben über die Wiederausfuhr gültig.
"""

from __future__ import annotations

from pathlib import Path

from app.ui_help import HANDBUCH, STIL, abschnitte, lade_handbuch
from app.ui_help import HelpWindow as _HelpWindow

__all__ = ["HANDBUCH", "STIL", "HelpWindow", "abschnitte", "lade_handbuch"]


class HelpWindow(_HelpWindow):
    """Das Handbuchfenster des DiskTool."""

    def __init__(self, parent=None, titel: str = "k1520DiskTool — Handbuch",
                 pfad: Path = HANDBUCH):
        super().__init__(parent, titel, pfad)
