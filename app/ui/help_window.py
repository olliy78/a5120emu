"""Das Handbuch des Emulators — Fenster und Textsatz stehen in :mod:`app.ui_help`.

Es ist dasselbe Fenster wie beim DiskTool (Inhaltsverzeichnis links, Suche oben,
Markdown ohne Bauschritt und ohne zusätzliche Abhängigkeit); hier steht nur,
welche Datei darin liegt.

Die Datei liegt unter ``app/help/`` und nicht in ``doc/`` — ``doc/`` ist im
Anwenderpaket nicht dabei, ``app/`` wird als Ganzes mitgeliefert
(`packaging/build_payload.sh`).
"""

from __future__ import annotations

from pathlib import Path

from app.ui_help import HelpWindow as _HelpWindow
from app.ui_help import abschnitte
from app.ui_help import lade_handbuch as _lade_handbuch

#: Das Handbuch des Emulators.
HANDBUCH = Path(__file__).resolve().parents[1] / "help" / "handbuch.md"


def lade_handbuch(pfad: Path = HANDBUCH) -> str:
    """Den Handbuchtext des EMULATORS lesen.

    Eigene Hülle mit eigener Vorgabe: ``app.ui_help.lade_handbuch`` zeigt ohne
    Pfad auf das Handbuch des DiskTool — wer sich darauf verliesse, prüfte still
    das falsche Buch.
    """
    return _lade_handbuch(pfad)

__all__ = ["HANDBUCH", "HelpWindow", "abschnitte", "lade_handbuch"]


class HelpWindow(_HelpWindow):
    """Das Handbuchfenster des Emulators."""

    def __init__(self, parent=None, titel: str = "a5120emu — Handbuch",
                 pfad: Path = HANDBUCH):
        super().__init__(parent, titel, pfad)
