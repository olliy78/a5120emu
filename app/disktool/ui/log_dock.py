"""Das Protokoll — die **Historie**, nicht die Anzeige.

Jede Meldung landet hier, mit Uhrzeit, ausnahmslos (doc/design/13_k1520disktool.md
§20.4).  Damit es dafür keinen Dauerplatz im Fenster braucht, sitzt es in einem
``QDockWidget`` unten, das beim Start **zu** ist; ``Ansicht ▸ Protokoll`` (F8)
holt es hervor — der Menüpunkt kommt von Qt selbst
(:meth:`QDockWidget.toggleViewAction`).

Ein verborgenes Dock nimmt trotzdem Text an; die Historie ist also auch dann
vollständig, wenn sie nie aufgeklappt wurde.
"""

from __future__ import annotations

import time

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QDockWidget, QPlainTextEdit, QWidget


class LogDock(QDockWidget):
    """Andockbares Protokollfenster."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__("Protokoll", parent)
        self.setObjectName("protokoll")          # für saveState()
        self.setAllowedAreas(Qt.BottomDockWidgetArea | Qt.TopDockWidgetArea)

        self.text = QPlainTextEdit()
        self.text.setReadOnly(True)
        self.text.setMaximumBlockCount(5000)
        schrift = QFont("monospace")
        schrift.setStyleHint(QFont.TypeWriter)
        self.text.setFont(schrift)
        self.setWidget(self.text)

    def append(self, zeile: str) -> None:
        """Zeile mit Uhrzeit anhängen (mehrzeiliges bleibt zusammen)."""
        marke = time.strftime("%H:%M:%S")
        erste, *weitere = zeile.split("\n")
        self.text.appendPlainText(f"{marke}  {erste}")
        for w in weitere:
            self.text.appendPlainText(f"          {w}")

    def clear(self) -> None:
        self.text.clear()
