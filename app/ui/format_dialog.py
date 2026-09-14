"""Die Rückfrage „welches Format?" — nur für rohe Sektorabbilder (``.img``).

Ein ``.hfe`` oder ``.dmk`` beschreibt sich selbst: Spuren, Sektoren, Marken und
das Aufzeichnungsverfahren stehen in der Datei, der Emulator liest sie wie eine
echte Diskette.  Ein ``.img`` ist dagegen nur eine Folge von Sektorinhalten —
wie viele Sektoren auf eine Spur gehören, wie groß sie sind und wo Kopf 1
anfängt, steht **nirgends**.  Diese Angabe ist Vereinbarung, nicht Befund, und
deshalb wird sie erfragt statt geraten.

Vorher stand dafür ein dauerhaftes Auswahlfeld in jedem Laufwerkskasten.  Das
war an der falschen Stelle: es galt für alle Dateiarten, obwohl nur eine davon
es braucht, und es stand *vor* der Dateiauswahl — man musste das Format wählen,
bevor man wusste, welche Datei man einlegt.  Jetzt fragt der Dialog **nach** der
Dateiauswahl und nur dann, wenn die Antwort gebraucht wird; an der Stelle des
Auswahlfeldes steht seitdem das ERKANNTE Format (siehe
:meth:`app.ui.drive_widget.DriveWidget._update_format_label`).

Die Vorauswahl kommt, wo möglich, aus der **Dateigröße**: ein rohes Sektorimage
hat genau so viele Bytes, wie seine Geometrie hergibt — das ist das einzige
Merkmal, das eine ``.img``-Datei über sich selbst preisgibt.
"""

from __future__ import annotations

from typing import Callable, List, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QLabel, QListWidget, QListWidgetItem, QVBoxLayout,
)


class FormatDialog(QDialog):
    """Ein Katalogformat aus einer Liste wählen.

    Benutzt über :meth:`frage`; die Klasse selbst ist nur deshalb öffentlich,
    damit Tests sie ohne Ereignisschleife aufbauen können.
    """

    def __init__(self, parent=None, *, formate: List[str], vorauswahl: str = "",
                 erklaerung: str = "", titel: str = "Diskettenformat",
                 beschreibung: Optional[Callable[[str], str]] = None):
        super().__init__(parent)
        self.setWindowTitle(titel)
        self.setModal(True)

        lay = QVBoxLayout(self)
        if erklaerung:
            hinweis = QLabel(erklaerung)
            hinweis.setWordWrap(True)
            lay.addWidget(hinweis)

        self.liste = QListWidget()
        for name in formate:
            text = name
            if beschreibung:
                try:
                    d = beschreibung(name)
                except Exception:
                    d = ""
                if d:
                    text = f"{name} — {d}"
            eintrag = QListWidgetItem(text)
            # Der Klartext steht in der Zeile, der KATALOGNAME in den Daten: nur
            # der geht an den Kern, und nur er bleibt beim Umbenennen der
            # Beschreibung derselbe.
            eintrag.setData(Qt.UserRole, name)
            self.liste.addItem(eintrag)
        if self.liste.count():
            zeile = 0
            for i in range(self.liste.count()):
                if self.liste.item(i).data(Qt.UserRole) == vorauswahl:
                    zeile = i
                    break
            self.liste.setCurrentRow(zeile)
        lay.addWidget(self.liste)

        # Doppelklick ist hier der schnellste Weg — die Liste ist die einzige
        # Eingabe des Dialogs.
        self.liste.itemDoubleClicked.connect(lambda _i: self.accept())

        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        knoepfe.accepted.connect(self.accept)
        knoepfe.rejected.connect(self.reject)
        lay.addWidget(knoepfe)

    def gewaehlt(self) -> str:
        """Der ausgewählte Katalogname ("" = nichts ausgewählt)."""
        eintrag = self.liste.currentItem()
        return eintrag.data(Qt.UserRole) if eintrag is not None else ""

    @classmethod
    def frage(cls, parent, *, formate, vorauswahl: str = "", erklaerung: str = "",
              titel: str = "Diskettenformat", beschreibung=None) -> Optional[str]:
        """Den Dialog zeigen; ``None`` bei Abbruch, sonst der Katalogname.

        Ohne Auswahlmöglichkeit (leere Liste) kommt sofort ``None`` zurück —
        einen Dialog ohne Inhalt zu zeigen hilft niemandem.
        """
        formate = list(formate or [])
        if not formate:
            return None
        dlg = cls(parent, formate=formate, vorauswahl=vorauswahl,
                  erklaerung=erklaerung, titel=titel, beschreibung=beschreibung)
        if dlg.exec() != QDialog.Accepted:
            return None
        return dlg.gewaehlt() or None
