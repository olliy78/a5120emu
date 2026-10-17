"""„tar-Satz öffnen…" — mehrere Abbilder als EIN tar-Strom.

Ein tar-Archiv, das auf mehrere Disketten verteilt wurde, ist nur in der richtigen
Reihenfolge lesbar: die letzte Datei einer Diskette setzt sich auf der nächsten fort.
Der Dialog zeigt die gewählten Abbilder als Liste, die der Anwender mit Auf/Ab
ordnet; der Vorschlag sortiert nach den Zahlen in den Dateinamen (``ariane2`` vor
``ariane10``).
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import List

from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QHBoxLayout, QLabel, QListWidget, QListWidgetItem,
    QPushButton, QVBoxLayout,
)
from PySide6.QtCore import Qt

PFAD_ROLE = Qt.UserRole + 1


def reihenfolge_vorschlag(pfade) -> List[str]:
    """Pfade nach den Zahlen im Dateinamen ordnen (natürliche Sortierung)."""
    def schluessel(p):
        name = Path(p).name.lower()
        return [(0, int(t), "") if t.isdigit() else (1, 0, t)
                for t in re.split(r"(\d+)", name) if t]
    return sorted((str(p) for p in pfade), key=schluessel)


class TarSatzDialog(QDialog):
    """Reihenfolge der Abbilder eines tar-Satzes festlegen."""

    def __init__(self, pfade, parent=None):
        super().__init__(parent)
        self.setWindowTitle("tar-Satz öffnen")
        self.setMinimumWidth(480)

        lay = QVBoxLayout(self)
        lay.addWidget(QLabel(
            "Die Abbilder werden in dieser Reihenfolge zu <b>einem</b> tar-Strom "
            "verkettet. Der Vorschlag folgt den Zahlen in den Dateinamen."))
        self.liste = QListWidget()
        for p in reihenfolge_vorschlag(pfade):
            e = QListWidgetItem(Path(p).name)
            e.setToolTip(p)
            e.setData(PFAD_ROLE, p)
            self.liste.addItem(e)
        self.liste.setCurrentRow(0)

        seite = QHBoxLayout()
        seite.addWidget(self.liste, 1)
        knoepfe_lay = QVBoxLayout()
        self.b_auf = QPushButton("Auf")
        self.b_ab = QPushButton("Ab")
        self.b_auf.clicked.connect(lambda: self._schieben(-1))
        self.b_ab.clicked.connect(lambda: self._schieben(1))
        knoepfe_lay.addWidget(self.b_auf)
        knoepfe_lay.addWidget(self.b_ab)
        knoepfe_lay.addStretch()
        seite.addLayout(knoepfe_lay)
        lay.addLayout(seite)

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        self.knoepfe.button(QDialogButtonBox.Ok).setText("Öffnen")
        self.knoepfe.button(QDialogButtonBox.Cancel).setText("Abbrechen")
        self.knoepfe.accepted.connect(self.accept)
        self.knoepfe.rejected.connect(self.reject)
        lay.addWidget(self.knoepfe)

    def _schieben(self, richtung: int) -> None:
        i = self.liste.currentRow()
        j = i + richtung
        if i < 0 or not 0 <= j < self.liste.count():
            return
        e = self.liste.takeItem(i)
        self.liste.insertItem(j, e)
        self.liste.setCurrentRow(j)

    def pfade(self) -> List[str]:
        """Die Abbilder in der gewählten Reihenfolge."""
        return [self.liste.item(i).data(PFAD_ROLE) for i in range(self.liste.count())]
