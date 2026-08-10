"""Rechte Hälfte: der Linux-Ordner.

Zeigt den gewählten Ordner flach, `Side0/`/`Side1/` als Gruppen — dieselbe
Darstellung wie die Diskettenseite, damit sichtbar ist, was zueinandergehört
(doc/design/13_k1520disktool.md §9.1).
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractItemView, QHBoxLayout, QLabel, QPushButton, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
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


class FolderView(QWidget):
    """Ordnerseite des Fensters."""

    folder_changed = Signal(str)
    disk_files_dropped = Signal(list)
    choose_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._folder: Optional[Path] = None

        self.pfad = QLabel("Kein Ordner gewählt")
        self.pfad.setWordWrap(True)
        knopf = QPushButton("Ordner…")
        knopf.clicked.connect(self.choose_requested)

        kopf = QHBoxLayout()
        kopf.addWidget(self.pfad, 1)
        kopf.addWidget(knopf)

        self.tree = _Tree()
        self.tree.setHeaderLabels(["Name", "Größe"])
        self.tree.setColumnWidth(0, 260)
        self.tree.disk_files_dropped.connect(self.disk_files_dropped)

        self.fuss = QLabel("")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addLayout(kopf)
        lay.addWidget(self.tree, 1)
        lay.addWidget(self.fuss)

    # ── Ordner ──────────────────────────────────────────────────────────────

    @property
    def folder(self) -> Optional[Path]:
        return self._folder

    def set_folder(self, path) -> None:
        self._folder = Path(path) if path else None
        self.pfad.setText(str(self._folder) if self._folder else "Kein Ordner gewählt")
        self.refresh()
        if self._folder:
            self.folder_changed.emit(str(self._folder))

    def refresh(self) -> None:
        """Ordnerinhalt neu einlesen — nach jedem Extrahieren aufzurufen."""
        self.tree.clear()
        if not self._folder or not self._folder.is_dir():
            self.fuss.setText("")
            return

        anzahl = 0
        for eintrag in sorted(self._folder.iterdir(), key=lambda p: (not p.is_dir(), p.name)):
            if eintrag.is_dir():
                gruppe = QTreeWidgetItem(self.tree, [eintrag.name + "/", ""])
                gruppe.setExpanded(True)
                for kind in sorted(eintrag.iterdir()):
                    if kind.is_dir():
                        continue
                    item = QTreeWidgetItem(gruppe, [kind.name, str(kind.stat().st_size)])
                    item.setData(0, PATH_ROLE, str(kind))
                    anzahl += 1
            else:
                item = QTreeWidgetItem(self.tree, [eintrag.name, str(eintrag.stat().st_size)])
                item.setData(0, PATH_ROLE, str(eintrag))
                anzahl += 1
        self.fuss.setText(f"{anzahl} Dateien")

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
