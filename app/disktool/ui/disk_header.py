"""Kopfbereich — die **dauerhaften** Eigenschaften der geöffneten Diskette.

Zwei Zeilen (Pfad, dann Format · Dateisystem · Seiten) und rechts das Auswahlfeld,
mit dem sich die Erkennung übersteuern lässt.  Es steht bewusst hier und nicht in
einer Knopfleiste: es bezieht sich auf die Angabe, die daneben steht
(doc/design/13_k1520disktool.md §20.2).

Was hier steht, steht dauerhaft — flüchtige Rückmeldungen gehören in die
Statuszeile, Einschränkungen in den :class:`~app.disktool.ui.info_bar.InfoBar`.
"""

from __future__ import annotations

from typing import List, Tuple

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox, QFrame, QGridLayout, QLabel, QSizePolicy,
)

from app.core_binding.k1520disk import filesystems
from app.disktool.ui.icons import icon


def auswahlliste() -> List[Tuple[str, str]]:
    """(Name, Beschriftung) für Kopffeld **und** Menü — eine einzige Quelle.

    Neben den benannten Katalogprofilen steht hier ``cpa_auto``: die abgeleitete
    Erkennung (`CpaDpbRule`) hat keinen Katalogeintrag, ist aber ein zulässiger
    Zwang — ohne sie liesse sich eine Diskette ohne Profil nicht öffnen
    (doc/design/13_k1520disktool.md §6.4).
    """
    eintraege = [("", "automatisch erkennen")]
    eintraege += [(f.name, f"{f.name} — {f.description}") for f in filesystems()]
    eintraege.append(("cpa_auto", "cpa_auto — aus der Geometrie abgeleitet "
                                  "(Regel des CP/A-BIOS)"))
    return eintraege


class DiskHeader(QFrame):
    """Kopfzeile über beiden Hälften."""

    #: Der Anwender hat ein anderes Dateisystem gewählt ('' = automatisch).
    filesystem_gewaehlt = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.NoFrame)

        self.symbol = QLabel()
        self.symbol.setPixmap(icon("disk-editor").pixmap(32, 32))
        self.symbol.setFixedWidth(38)

        self.pfad = QLabel("Keine Diskette geöffnet")
        self.pfad.setTextInteractionFlags(Qt.TextSelectableByMouse)
        schrift = self.pfad.font()
        schrift.setBold(True)
        self.pfad.setFont(schrift)
        self.pfad.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)

        self.angaben = QLabel("Öffnen Sie ein Abbild oder legen Sie eine neue "
                              "Diskette an.")
        self.angaben.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)

        self.fs_wahl = QComboBox()
        self.fs_wahl.setToolTip("Die Erkennung übersteuern und neu öffnen")
        for name, text in auswahlliste():
            self.fs_wahl.addItem(text, name)
        self.fs_wahl.currentIndexChanged.connect(
            lambda *_: self.filesystem_gewaehlt.emit(self.fs_wahl.currentData() or ""))

        # Das Auswahlfeld bekommt eine feste Breite: es ist eine Ausnahme-Bedienung
        # und darf dem Pfad daneben nicht den Platz nehmen.
        self.fs_wahl.setMinimumWidth(240)
        self.fs_wahl.setMaximumWidth(320)

        raster = QGridLayout(self)
        raster.setContentsMargins(4, 4, 4, 2)
        raster.setHorizontalSpacing(8)
        raster.setVerticalSpacing(0)
        raster.addWidget(self.symbol, 0, 0, 2, 1)
        raster.addWidget(self.pfad, 0, 1)
        raster.addWidget(self.angaben, 1, 1)
        raster.addWidget(QLabel("Dateisystem:"), 0, 2, Qt.AlignRight | Qt.AlignBottom)
        raster.addWidget(self.fs_wahl, 1, 2)
        raster.setColumnStretch(1, 1)

    # ── Inhalt ──────────────────────────────────────────────────────────────

    def leeren(self, pfad: str = "") -> None:
        self.pfad.setText(pfad or "Keine Diskette geöffnet")
        self.angaben.setText("Öffnen Sie ein Abbild oder legen Sie eine neue "
                             "Diskette an.")

    def setze(self, tool, name: str = "") -> None:
        """Kopf aus einer geöffneten Diskette füllen.

        ``name`` übersteuert die Pfadzeile — eine **physische** Diskette hat keinen
        Pfad (``tool.path`` ist leer), aber sehr wohl eine Herkunft, die dort stehen
        muss.
        """
        self.pfad.setText(name or tool.path)
        erkannt = "erkannt" if tool.unambiguous else "erkannt, nicht eindeutig"
        teile = [f"Format: {tool.format}",
                 f"Dateisystem: {tool.filesystem} ({erkannt})"]
        if tool.volume_count > 1:
            teile.append(f"{tool.volume_count} Seiten")
        self.angaben.setText("   ·   ".join(teile))

    def setze_filesystem(self, name: str) -> None:
        """Das Auswahlfeld ohne Rückmeldung auf ``name`` stellen."""
        index = self.fs_wahl.findData(name or "")
        if index < 0:
            index = 0
        self.fs_wahl.blockSignals(True)
        self.fs_wahl.setCurrentIndex(index)
        self.fs_wahl.blockSignals(False)

    def filesystem(self) -> str:
        return self.fs_wahl.currentData() or ""
