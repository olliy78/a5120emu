"""Die Rückfrage „welches Bandformat?" nach jeder Dateiwahl im Kasten „Lochstreifen".

doc/design/23_lochstreifen.md §4/§7.  Ein Lochstreifen ist im Emulator eine Datei in
einem von drei Formaten (Kennungen ``PTAPE_FMT_*`` = ``K1520_PTAPE_FMT_*``):

* **Roh** — ein Byte je Sprosse, wie gestanzt (``.ptp``, ``.bin``),
* **Intel HEX** — Adresse = Bandposition (``.hex``, ``.ihx``),
* **ASCII-Art** — das Lochbild zum Ansehen und Bearbeiten (``.txt``, ``.tape``).

Die Vorauswahl kommt beim Leser (und beim Stanzer, wenn die Datei schon etwas trägt)
aus dem **Inhalt** (``k1520_ptape_detect_format``), sonst aus der **Endung**
(``k1520_ptape_format_from_ext``).  Gefragt wird trotzdem immer: eine rohe Datei, die
zufällig nur aus ``:``-Zeilen besteht, sähe wie Intel HEX aus.

Benutzt wird :func:`frage`; Tests ersetzen genau diese Funktion (ein ``exec()`` stünde
headless bis zum Zeitüberlauf).
"""

from __future__ import annotations

import os
from typing import Optional

from PySide6.QtWidgets import (
    QButtonGroup, QDialog, QDialogButtonBox, QLabel, QRadioButton, QVBoxLayout,
)

from app.core_binding.k1520 import (
    K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX, PTAPE_FMT_RAW,
)

#: (Kennung, Name, Endungen, Erklärung) in der Reihenfolge des Dialogs.
FORMATE = [
    (PTAPE_FMT_RAW, "Roh", "*.ptp *.bin",
     "Ein Byte je Sprosse, genau wie gestanzt — ohne Kopf und ohne Umsetzung.  "
     "Eine Textdatei des PCs lässt sich so direkt einlesen."),
    (PTAPE_FMT_IHEX, "Intel HEX", "*.hex *.ihx",
     "Sätze „:LLAAAATT…PP“; die Adresse ist die Stelle auf dem Band, Lücken "
     "werden mit 00H gefüllt.  Prüfsummen werden geprüft."),
    (PTAPE_FMT_ASCII, "ASCII-Art", "*.txt *.tape",
     "Das Lochbild als Text: eine Zeile je Sprosse, O = Loch, . = kein Loch, "
     "o = Transportloch, rechts Wert und Zeichen.  Zum Ansehen und Bearbeiten."),
]

#: Dateifilter der Auswahl — alle Endungen zuerst, dann je Format.
FILTER = ";;".join(
    ["Lochstreifen (" + " ".join(e for _, _, e, _ in FORMATE) + ")"]
    + [f"{name} ({endungen})" for _, name, endungen, _ in FORMATE]
    + ["Alle Dateien (*)"])


def vorauswahl(pfad: str) -> int:
    """Format, das der Dialog vorschlägt: aus dem Inhalt einer vorhandenen, nicht
    leeren Datei, sonst aus der Endung."""
    try:
        traegt_etwas = os.path.getsize(pfad) > 0
    except OSError:
        traegt_etwas = False
    if traegt_etwas:
        erkannt = K1520Emulator.ptape_detect_format(pfad)
        if erkannt is not None:
            return erkannt
    return K1520Emulator.ptape_format_from_ext(pfad)


class LochstreifenFormatDialog(QDialog):
    """Drei Knöpfe mit je einer Zeile Erklärung."""

    def __init__(self, parent=None, *, pfad: str, vorschlag: int, stanzer: bool):
        super().__init__(parent)
        self.setWindowTitle("Format des Lochstreifens")
        self.setModal(True)
        self.setMinimumWidth(420)
        lay = QVBoxLayout(self)
        wer = "Der Stanzer schreibt" if stanzer else "Der Leser liest"
        kopf = QLabel(f"<b>{os.path.basename(pfad)}</b><br>{wer} die Datei im Format:")
        kopf.setWordWrap(True)
        lay.addWidget(kopf)
        self.gruppe = QButtonGroup(self)
        for kennung, name, endungen, erklaerung in FORMATE:
            knopf = QRadioButton(f"{name}  ({endungen.replace('*', '')})")
            self.gruppe.addButton(knopf, kennung)
            lay.addWidget(knopf)
            text = QLabel(erklaerung)
            text.setWordWrap(True)
            text.setContentsMargins(24, 0, 0, 6)
            lay.addWidget(text)
            if kennung == vorschlag:
                knopf.setChecked(True)
        if self.gruppe.checkedButton() is None:
            self.gruppe.button(PTAPE_FMT_RAW).setChecked(True)
        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        knoepfe.accepted.connect(self.accept)
        knoepfe.rejected.connect(self.reject)
        lay.addWidget(knoepfe)

    def format(self) -> int:
        return self.gruppe.checkedId()


def frage(parent, pfad: str, stanzer: bool) -> Optional[int]:
    """Format für *pfad* erfragen; ``None`` = abgebrochen."""
    dlg = LochstreifenFormatDialog(parent, pfad=pfad, vorschlag=vorauswahl(pfad),
                                   stanzer=stanzer)
    return dlg.format() if dlg.exec() == QDialog.Accepted else None
