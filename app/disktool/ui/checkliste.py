"""Die Checkliste eines Laufs — was wurde getan, und was kam dabei heraus.

Beide Dialoge (Prüfen und Suchen) zeigen sie oben, gleich gebaut und gleich
gelesen: ein Zeichen, der Schritt im Klartext, das Ergebnis
(doc/design/15_dateisystempruefung.md §5a).

Der Grund für dieses Widget ist eine Beobachtung am fertigen Werkzeug: eine
Prüfung, die „ohne Befund" meldet, sagt dem Bediener **nicht, worauf sie gesehen
hat** — und weil sie an einer Datei nur rund 70 ms dauert, sieht sie aus, als
hätte sie gar nicht stattgefunden.  Die Liste beantwortet beides in einem Bild.

Die Zeilen kommen aus dem Prüfer selbst (`Schritt`, C-ABI `k1520d_*_step_*`),
nicht aus dieser Datei: eine Checkliste, die einen Schritt behauptet, den es
nicht gab, wäre schlimmer als gar keine.  Auch ein **übersprungener** Schritt
steht drin, mit seinem Grund — „nicht geprüft" ist eine Auskunft, „fehlt in der
Liste" ist keine.
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QBrush, QPalette
from PySide6.QtWidgets import QAbstractItemView, QHeaderView, QTreeWidget, QTreeWidgetItem


class Checkliste(QTreeWidget):
    """Schrittliste mit Haken.  ``setze()`` füllt sie aus einem Bericht."""

    def __init__(self, parent=None, *, was: str = "Befund"):
        super().__init__(parent)
        #: Wie das Ergebnis eines Schritts heißt — „Befund" (Prüfung) oder „Fund".
        self._was = was
        self.setColumnCount(3)
        self.setHeaderLabels(["", "Schritt", "Ergebnis"])
        self.setRootIsDecorated(False)
        self.setUniformRowHeights(True)
        self.setAlternatingRowColors(True)
        self.setSelectionMode(QAbstractItemView.NoSelection)
        self.setFocusPolicy(Qt.NoFocus)
        kopf = self.header()
        kopf.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        kopf.setSectionResizeMode(1, QHeaderView.Stretch)
        kopf.setSectionResizeMode(2, QHeaderView.ResizeToContents)

    def setze(self, schritte) -> None:
        """Die Liste aus den Schritten eines Berichts neu aufbauen."""
        self.clear()
        for s in schritte:
            zeile = QTreeWidgetItem(self)
            zeile.setText(0, s.zeichen)
            zeile.setText(1, s.titel)
            zeile.setText(2, self._ergebnis(s))
            zeile.setToolTip(1, s.id)
            if not s.ausgefuehrt:
                # Übersprungen: sichtbar, aber gedämpft — er ist kein Versäumnis
                # des Bedieners, sondern eine Eigenschaft des Laufs.
                grau = QBrush(self.palette().color(QPalette.Disabled,
                                                   QPalette.Text))
                for spalte in range(3):
                    zeile.setForeground(spalte, grau)
        # Höhe FEST auf den Inhalt: eine Höchsthöhe allein genügt nicht — die
        # Mindesthöhe eines QTreeWidget ist grösser, und der Teiler nimmt dann
        # die grössere.  Das Ergebnis wäre ein leerer Streifen unter der letzten
        # Zeile, der aussieht, als fehlte dort etwas.
        hoehe = self._hoehe()
        self.setMinimumHeight(hoehe)
        self.setMaximumHeight(hoehe)

    def _ergebnis(self, s) -> str:
        if not s.ausgefuehrt:
            return f"übersprungen — {s.grund}" if s.grund else "übersprungen"
        if not s.treffer:
            return "ohne Befund" if self._was == "Befund" else "nichts gefunden"
        return f"{s.treffer} {self._was}" + ("e" if s.treffer != 1 else "")

    def _hoehe(self) -> int:
        """So hoch, wie die Liste WIRKLICH ist — höchstens acht Zeilen.

        Sie ist die Nebensache des Fensters: die Befunde darunter brauchen den
        Platz, und ein leerer Streifen unter der letzten Zeile sähe aus, als
        fehlte dort etwas.  Bei mehr als acht Schritten rollt sie.
        """
        anzahl = self.topLevelItemCount()
        if not anzahl:
            return self.header().height() + 8
        hoehen = [self.sizeHintForRow(i) for i in range(min(anzahl, 8))]
        rahmen = 2 * self.frameWidth()
        return sum(hoehen) + self.header().height() + rahmen + 2
