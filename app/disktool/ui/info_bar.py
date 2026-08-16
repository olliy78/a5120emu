"""Meldungsstreifen — dauerhafte Einschränkungen, die man wegklicken kann.

Er trägt genau **eine** Sorte Meldung: was an der geöffneten Diskette dauerhaft
zu beachten ist (nicht eindeutig erkannt, gemessene Geometrie ⇒ unaufhebbar
schreibgeschützt, Altbestand im Medium, „es wurde nichts geschrieben").  Alles
Flüchtige gehört in die Statuszeile, alles Nachschlagbare ins Protokoll
(doc/design/13_k1520disktool.md §20.4).

Er sitzt über **beiden** Hälften des Fensters, denn auch das Einfügen aus dem
Ordner kann etwas zu melden haben — nicht nur die Diskettenseite.
"""

from __future__ import annotations

from typing import Callable, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QPushButton, QToolButton

#: Schweregrad → (Rahmen, Fläche, Schrift, Zeichen)
STUFEN = {
    "hinweis": ("#5b8db8", "rgba(91,141,184,0.13)", "#1d4d73", "ℹ"),
    "warnung": ("#c08a2e", "rgba(192,138,46,0.15)", "#7a5205", "⚠"),
    "fehler":  ("#c0504d", "rgba(192,80,77,0.15)", "#8a2f2c", "✖"),
}


class InfoBar(QFrame):
    """Einzeiliger Streifen mit Schweregrad, Text, Schaltfläche und Schließkreuz."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.NoFrame)

        self._zeichen = QLabel("")
        self._text = QLabel("")
        self._text.setWordWrap(True)
        self._text.setTextInteractionFlags(Qt.TextSelectableByMouse)

        self._knopf = QPushButton("")
        self._knopf.hide()
        self._knopf.setFlat(True)

        self._zu = QToolButton()
        self._zu.setText("✕")
        self._zu.setAutoRaise(True)
        self._zu.setToolTip("Meldung schließen")
        self._zu.clicked.connect(self.verbergen)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(8, 4, 4, 4)
        lay.setSpacing(8)
        lay.addWidget(self._zeichen)
        lay.addWidget(self._text, 1)
        lay.addWidget(self._knopf)
        lay.addWidget(self._zu)

        self._stufe = "hinweis"
        self._verbunden = False
        self.hide()

    # ── Anzeigen ────────────────────────────────────────────────────────────

    def zeige(self, text: str, stufe: str = "hinweis", *,
              knopf: Optional[str] = None,
              bei_klick: Optional[Callable[[], None]] = None) -> None:
        """Meldung anzeigen.  Leerer Text = verbergen (dann fällt der Streifen weg)."""
        if not text:
            self.verbergen()
            return
        rahmen, flaeche, schrift, zeichen = STUFEN.get(stufe, STUFEN["hinweis"])
        self._stufe = stufe
        self.setStyleSheet(
            f"InfoBar {{ background: {flaeche}; border: 1px solid {rahmen};"
            f" border-radius: 4px; }}"
            f" QLabel {{ color: {schrift}; }}")
        self._zeichen.setText(zeichen)
        self._text.setText(text)

        # Qt beschwert sich über ein `disconnect()` ins Leere — deshalb wird nur
        # getrennt, was auch verbunden wurde.
        if self._verbunden:
            self._knopf.clicked.disconnect()
            self._verbunden = False
        if knopf and bei_klick is not None:
            self._knopf.setText(knopf)
            self._knopf.clicked.connect(lambda *_: bei_klick())
            self._verbunden = True
            self._knopf.show()
        else:
            self._knopf.hide()
        self.show()

    def verbergen(self) -> None:
        self._text.setText("")
        self.hide()

    # ── Abfragen (für Tests und Statusmeldungen) ────────────────────────────

    def text(self) -> str:
        return self._text.text()

    @property
    def stufe(self) -> str:
        return self._stufe
