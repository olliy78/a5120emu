"""Symbole der Oberfläche — **mitgeliefert**, nicht vom Systemthema geborgt.

Der Grund ist die Verteilung (`doc/design/13_distribution.md`): ``QIcon.fromTheme()``
liefert unter Windows **nichts**, und ein Paket, dessen Symbolleiste auf der einen
Plattform leer bleibt, ist keins.  Die Zeichnungen liegen deshalb als einfarbige
SVG unter ``app/icons/`` und werden hier eingefärbt: das Wort ``currentColor``
wird durch die Textfarbe der laufenden Palette ersetzt, bevor Qt sie rastert —
so passen sie sich hellem wie dunklem Thema an, ohne dass es zwei Sätze braucht.

``QIcon.fromTheme(name)`` bleibt der Rückfall für den Fall, dass eine Datei fehlt.
"""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from PySide6.QtGui import QGuiApplication, QIcon, QPalette, QPixmap

#: ``app/icons`` — von ``app/disktool/ui/icons.py`` aus zwei Ebenen hinauf.
ICON_DIR = Path(__file__).resolve().parents[2] / "icons"

#: Kantenlängen, in denen jedes Symbol gerastert wird (Leiste, Menü, HiDPI).
GROESSEN = (16, 22, 32, 48)


def _vordergrund() -> str:
    """Textfarbe der laufenden Palette als ``#rrggbb``."""
    app = QGuiApplication.instance()
    if app is None:                       # ohne QApplication (reine Importprüfung)
        return "#303030"
    return app.palette().color(QPalette.ButtonText).name()


@lru_cache(maxsize=None)
def _gebaut(name: str, farbe: str) -> QIcon:
    datei = ICON_DIR / f"{name}.svg"
    if not datei.is_file():
        return QIcon.fromTheme(name)
    quelle = datei.read_text(encoding="utf-8").replace("currentColor", farbe)
    symbol = QIcon()
    for kante in GROESSEN:
        # Je Größe neu rastern statt eine Pixmap zu skalieren — sonst franst der
        # 1,6-px-Strich in der Menüzeile aus.
        skaliert = quelle.replace('width="24" height="24"',
                                  f'width="{kante}" height="{kante}"')
        bild = QPixmap()
        if bild.loadFromData(skaliert.encode("utf-8"), "SVG"):
            symbol.addPixmap(bild)
    return symbol


def icon(name: str) -> QIcon:
    """Symbol ``name`` in der aktuellen Vordergrundfarbe.

    Ein unbekannter Name ergibt ein leeres ``QIcon`` (bzw. das Themensymbol) —
    eine Aktion ohne Bild ist ein Schönheitsfehler, kein Absturz.
    """
    if not name:
        return QIcon()
    return _gebaut(name, _vordergrund())
