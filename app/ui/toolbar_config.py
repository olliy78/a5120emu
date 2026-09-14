"""Die Symbolleiste einrichten — welche Schaltflächen, in welcher Reihenfolge.

Eine Liste mit Häkchen: angehakt heisst „steht in der Leiste", und die
Reihenfolge der Liste ist die der Leiste (Einträge lassen sich mit der Maus
verschieben).  Der Trennstrich ist ein Eintrag wie jeder andere — sonst könnte
man Gruppen zwar bilden, aber nicht trennen.

Das Ergebnis ist eine Liste von Aktionsnamen (``None`` = Trennstrich), die das
Fenster an :meth:`app.ui.main_window.MainWindow._leiste_fuellen` gibt und in der
Konfiguration ablegt (``window.toolbar``).  Mehr Zustand gibt es nicht: die
Leiste wird aus dieser Liste jedesmal neu gebaut.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QAbstractItemView, QDialog, QDialogButtonBox, QHBoxLayout, QLabel,
    QListWidget, QListWidgetItem, QPushButton, QVBoxLayout,
)

#: Was ein Trennstrich in der Liste anzeigt (in der Leiste ist er ``None``).
TRENNER = "── Trennstrich ──"


class ToolbarDialog(QDialog):
    """Auswahl und Reihenfolge der Schaltflächen.

    :param verfuegbar: alle möglichen Einträge in ihrer Anbietreihenfolge
        (Aktionsnamen, ``None`` für einen Trennstrich)
    :param aktuell:    was die Leiste jetzt zeigt
    :param namen:      Aktionsname → Beschriftung für die Liste
    :param standard:   die Belegung des ersten Starts (Knopf „Standard")
    """

    def __init__(self, verfuegbar: List[Optional[str]],
                 aktuell: List[Optional[str]],
                 namen: Dict[str, str],
                 standard: Optional[List[Optional[str]]] = None, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Symbolleiste einrichten")
        self.resize(420, 460)
        self._namen = namen
        self._standard = list(verfuegbar)

        self.liste = QListWidget()
        self.liste.setDragDropMode(QAbstractItemView.InternalMove)
        self.liste.setSelectionMode(QAbstractItemView.SingleSelection)
        self._fuellen(aktuell)

        hinweis = QLabel(
            "Angehakt steht in der Leiste.  Die Reihenfolge lässt sich mit der "
            "Maus ändern; im Menü bleibt ohnehin alles erreichbar.")
        hinweis.setWordWrap(True)

        btn_trenner = QPushButton("Trennstrich einfügen")
        btn_trenner.clicked.connect(self._trenner_einfuegen)
        btn_standard = QPushButton("Standard")
        btn_standard.setToolTip("Die Leiste des ersten Starts wiederherstellen")
        btn_standard.clicked.connect(
            lambda: self.standard_setzen(list(standard or verfuegbar)))

        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        knoepfe.accepted.connect(self.accept)
        knoepfe.rejected.connect(self.reject)

        oben = QHBoxLayout()
        oben.addWidget(btn_trenner)
        oben.addWidget(btn_standard)
        oben.addStretch(1)

        lay = QVBoxLayout(self)
        lay.addWidget(hinweis)
        lay.addLayout(oben)
        lay.addWidget(self.liste, 1)
        lay.addWidget(knoepfe)

    # ── Inhalt ───────────────────────────────────────────────────────────────

    def _eintrag(self, name: Optional[str], an: bool) -> QListWidgetItem:
        eintrag = QListWidgetItem(TRENNER if name is None else self._namen.get(name, name))
        eintrag.setData(Qt.UserRole, name)
        eintrag.setFlags(eintrag.flags() | Qt.ItemIsUserCheckable)
        eintrag.setCheckState(Qt.Checked if an else Qt.Unchecked)
        return eintrag

    def _fuellen(self, aktuell: List[Optional[str]]) -> None:
        """Erst das Gewählte in seiner Reihenfolge, dann der ungenutzte Rest."""
        self.liste.clear()
        for name in aktuell:
            self.liste.addItem(self._eintrag(name, True))
        gewaehlt = {n for n in aktuell if n is not None}
        for name in self._standard:
            if name is not None and name not in gewaehlt:
                self.liste.addItem(self._eintrag(name, False))

    def standard_setzen(self, aktuell: List[Optional[str]]) -> None:
        """Die Liste auf eine vorgegebene Belegung zurücksetzen."""
        self._fuellen(aktuell)

    def _trenner_einfuegen(self) -> None:
        zeile = self.liste.currentRow()
        self.liste.insertItem(zeile + 1 if zeile >= 0 else self.liste.count(),
                              self._eintrag(None, True))

    # ── Ergebnis ─────────────────────────────────────────────────────────────

    def auswahl(self) -> List[Optional[str]]:
        """Die angehakten Einträge in Listenreihenfolge.

        Trennstriche am Rand und doppelte hintereinander fallen weg — sie wären
        in der Leiste nicht zu sehen, blieben aber in der Konfiguration stehen.
        """
        roh = [self.liste.item(i).data(Qt.UserRole)
               for i in range(self.liste.count())
               if self.liste.item(i).checkState() == Qt.Checked]
        raus: List[Optional[str]] = []
        for name in roh:
            if name is None and (not raus or raus[-1] is None):
                continue
            raus.append(name)
        while raus and raus[-1] is None:
            raus.pop()
        return raus
