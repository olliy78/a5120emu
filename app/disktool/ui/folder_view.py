"""Rechte Hälfte: der Linux-Ordner.

Zeigt den gewählten Ordner flach, `Side0/`/`Side1/` als Gruppen — dieselbe
Darstellung wie die Diskettenseite, damit sichtbar ist, was zueinandergehört
(doc/design/13_k1520disktool.md §9.1).

Der frühere Knopf „Ordner…" ist mit dem Umbau §20 verschwunden: die Aktion steht
im Menü *Übertragung*, und der Pfad in der Überschrift ist anklickbar.  Ebenso die
Fusszeile mit der Dateizahl — beide Hälften sind gleich gebaut (Überschrift +
Liste, sonst nichts), und Zahlen stehen in der Statuszeile.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractItemView, QLabel, QMenu, QTreeWidget, QTreeWidgetItem, QVBoxLayout,
    QWidget,
)

from app.disktool.ui.disk_view import DISK_MIME

PATH_ROLE = Qt.UserRole + 1


class _Tree(QTreeWidget):
    """Baum, der von der Diskette gezogene Dateien annimmt."""

    disk_files_dropped = Signal(list)   # Referenzen ("Side1/NAME")

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptDrops(True)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setUniformRowHeights(True)
        self.setAlternatingRowColors(True)

    def dragEnterEvent(self, event):  # noqa: N802
        if event.mimeData().hasFormat(DISK_MIME):
            event.acceptProposedAction()

    def dragMoveEvent(self, event):  # noqa: N802
        if event.mimeData().hasFormat(DISK_MIME):
            event.acceptProposedAction()

    def dropEvent(self, event):  # noqa: N802
        if not event.mimeData().hasFormat(DISK_MIME):
            return
        roh = bytes(event.mimeData().data(DISK_MIME)).decode("utf-8")
        refs = [r for r in roh.split("\n") if r]
        if refs:
            self.disk_files_dropped.emit(refs)
        event.acceptProposedAction()


class _PfadLabel(QLabel):
    """Überschrift, die zugleich der Weg zur Ordnerwahl ist."""

    clicked = Signal()

    def mouseReleaseEvent(self, event):  # noqa: N802
        if event.button() == Qt.LeftButton:
            self.clicked.emit()
        super().mouseReleaseEvent(event)


class FolderView(QWidget):
    """Ordnerseite des Fensters."""

    folder_changed = Signal(str)
    disk_files_dropped = Signal(list)
    choose_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._folder: Optional[Path] = None
        self._aktionen = []

        self.pfad = _PfadLabel("Ordner — kein Ordner gewählt (klicken)")
        self.pfad.setWordWrap(True)
        self.pfad.setCursor(Qt.PointingHandCursor)
        self.pfad.setToolTip("Anklicken, um den Ordner zu wechseln")
        schrift = self.pfad.font()
        schrift.setBold(True)
        self.pfad.setFont(schrift)
        self.pfad.clicked.connect(self.choose_requested)

        self.tree = _Tree()
        self.tree.setHeaderLabels(["Name", "Größe"])
        self.tree.setColumnWidth(0, 260)
        self.tree.disk_files_dropped.connect(self.disk_files_dropped)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._kontextmenue)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        lay.addWidget(self.pfad)
        lay.addWidget(self.tree, 1)

    # ── Ordner ──────────────────────────────────────────────────────────────

    def setze_aktionen(self, *aktionen) -> None:
        """Aktionen des Kontextmenüs (``None`` = Trenner)."""
        self._aktionen = list(aktionen)

    @property
    def folder(self) -> Optional[Path]:
        return self._folder

    def set_folder(self, path) -> None:
        self._folder = Path(path) if path else None
        self.pfad.setText(f"Ordner — {self._folder}" if self._folder
                          else "Ordner — kein Ordner gewählt (klicken)")
        self.refresh()
        if self._folder:
            self.folder_changed.emit(str(self._folder))

    @staticmethod
    def _groesse(pfad: Path) -> str:
        """Dateigröße als Text; ``?`` für einen toten Verweis.

        Ein Ordner kann einen Symlink ins Nichts enthalten (``/tmp`` ist voll
        davon) — daran darf die Ansicht nicht scheitern.
        """
        try:
            return str(pfad.stat().st_size)
        except OSError:
            return "?"

    def refresh(self) -> None:
        """Ordnerinhalt neu einlesen — nach jedem Extrahieren aufzurufen."""
        self.tree.clear()
        if not self._folder or not self._folder.is_dir():
            return

        for eintrag in sorted(self._folder.iterdir(), key=lambda p: (not p.is_dir(), p.name)):
            if eintrag.is_dir():
                gruppe = QTreeWidgetItem(self.tree, [eintrag.name + "/", ""])
                gruppe.setExpanded(True)
                for kind in sorted(eintrag.iterdir()):
                    if kind.is_dir():
                        continue
                    item = QTreeWidgetItem(gruppe, [kind.name, self._groesse(kind)])
                    item.setData(0, PATH_ROLE, str(kind))
            else:
                item = QTreeWidgetItem(self.tree, [eintrag.name,
                                                   self._groesse(eintrag)])
                item.setData(0, PATH_ROLE, str(eintrag))

    # ── Kontextmenü ─────────────────────────────────────────────────────────

    def _kontextmenue(self, punkt) -> None:
        if not self._aktionen:
            return
        item = self.tree.itemAt(punkt)
        if item is not None and item.data(0, PATH_ROLE) \
                and item.data(0, PATH_ROLE) not in self.selected_paths():
            self.tree.clearSelection()
            item.setSelected(True)
            self.tree.setCurrentItem(item)

        menue = QMenu(self)
        for a in self._aktionen:
            menue.addSeparator() if a is None else menue.addAction(a)
        menue.exec(self.tree.viewport().mapToGlobal(punkt))

    # ── Auswahl ─────────────────────────────────────────────────────────────

    def selected_paths(self) -> List[str]:
        return [p for p in (i.data(0, PATH_ROLE) for i in self.tree.selectedItems()) if p]

    def selected_side(self) -> Optional[int]:
        """Aus einem ausgewählten `SideN/`-Eintrag die Seitennummer ableiten."""
        for item in self.tree.selectedItems():
            knoten = item
            while knoten is not None:
                name = knoten.text(0).rstrip("/").lower()
                if name.startswith("side") and name[4:].isdigit():
                    return int(name[4:])
                knoten = knoten.parent()
        return None
