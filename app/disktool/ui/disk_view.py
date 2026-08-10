"""Linke Hälfte: der Inhalt der Diskette.

Beide Seiten einer UDOS-Diskette stehen in **einer** Liste, nach Seite gruppiert —
es wird nicht umgeschaltet (doc/design/13_k1520disktool.md §9.1).  Bei einer
CP/M-Diskette entfällt die Gruppierung ersatzlos.

Die Ansicht hält **keinen** eigenen Verzeichnisstand: `set_entries()` bekommt bei
jedem Aufruf die frisch gelesene Liste (§9.3).
"""

from __future__ import annotations

from typing import List, Optional

from PySide6.QtCore import Qt, QMimeData, Signal
from PySide6.QtGui import QDrag
from PySide6.QtWidgets import (
    QAbstractItemView, QLabel, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

#: Eigener MIME-Typ für das Ziehen von der Diskette in einen Ordner.
DISK_MIME = "application/x-k1520-diskfiles"

#: Rolle, unter der die eindeutige Bezeichnung („Side1/NAME") am Eintrag hängt.
REF_ROLE = Qt.UserRole + 1
VOLUME_ROLE = Qt.UserRole + 2


class _Tree(QTreeWidget):
    """Baum mit Ziehen (zum Ordner) und Ablegen (Dateien aus dem Dateimanager)."""

    files_dropped = Signal(list, int)   # (Pfade, Ziel-Seite)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptDrops(True)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDrop)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setRootIsDecorated(True)
        self.setUniformRowHeights(True)

    # ── Ziehen: die ausgewählten Dateien als Referenzliste ──────────────────

    def startDrag(self, actions):  # noqa: N802 (Qt-Namensschema)
        refs = [r for r in (i.data(0, REF_ROLE) for i in self.selectedItems()) if r]
        if not refs:
            return
        mime = QMimeData()
        mime.setData(DISK_MIME, "\n".join(refs).encode("utf-8"))
        drag = QDrag(self)
        drag.setMimeData(mime)
        drag.exec(Qt.CopyAction)

    # ── Ablegen: Dateien aus dem Dateimanager ───────────────────────────────

    def dragEnterEvent(self, event):  # noqa: N802
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dragMoveEvent(self, event):  # noqa: N802
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):  # noqa: N802
        if not event.mimeData().hasUrls():
            return
        pfade = [u.toLocalFile() for u in event.mimeData().urls() if u.isLocalFile()]
        if not pfade:
            return
        # Auf welche Seite wurde abgelegt?  Die Gruppe unter dem Mauszeiger
        # entscheidet; ohne Treffer die erste Seite.
        ziel = 0
        item = self.itemAt(event.position().toPoint())
        while item is not None:
            v = item.data(0, VOLUME_ROLE)
            if v is not None:
                ziel = int(v)
                break
            item = item.parent()
        self.files_dropped.emit(pfade, ziel)
        event.acceptProposedAction()


class DiskView(QWidget):
    """Diskettenseite des Fensters: Kopfzeile, Baum, Fußzeile."""

    files_dropped = Signal(list, int)

    def __init__(self, parent=None):
        super().__init__(parent)

        self.kopf = QLabel("Keine Diskette geöffnet")
        self.kopf.setWordWrap(True)
        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)
        self.hinweis.setStyleSheet("color: #a06000;")
        self.hinweis.hide()

        self.tree = _Tree()
        self.tree.setHeaderLabels(["Name", "Typ", "Größe", "Eigensch.", "Datum"])
        self.tree.setColumnWidth(0, 240)
        self.tree.files_dropped.connect(self.files_dropped)

        self.fuss = QLabel("")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.kopf)
        lay.addWidget(self.hinweis)
        lay.addWidget(self.tree, 1)
        lay.addWidget(self.fuss)

    # ── Inhalt setzen ───────────────────────────────────────────────────────

    def clear(self) -> None:
        self.tree.clear()
        self.kopf.setText("Keine Diskette geöffnet")
        self.hinweis.hide()
        self.fuss.setText("")

    def set_disk(self, tool, entries) -> None:
        """Kopfzeile und Baum aus einer geöffneten Diskette füllen.

        Args:
            tool: ``DiskTool`` (oder None)
            entries: die **frisch gelesene** Liste von ``Entry``
        """
        self.tree.clear()
        if tool is None:
            self.clear()
            return

        erkannt = "erkannt" if tool.unambiguous else "erkannt, nicht eindeutig"
        self.kopf.setText(
            f"<b>{tool.path}</b><br>"
            f"Format: {tool.format} &nbsp;·&nbsp; Dateisystem: {tool.filesystem} "
            f"({erkannt})"
        )
        if tool.remarks:
            self.hinweis.setText(f"Medium: {tool.remarks}")
            self.hinweis.show()
        else:
            self.hinweis.hide()

        mehrseitig = tool.volume_count > 1
        gruppen = {}
        if mehrseitig:
            for v in tool.volumes():
                g = QTreeWidgetItem(self.tree, [f"{v.dir}   {v.label}", "", "", "",
                                                f"{v.free // 1024} KB frei"])
                g.setData(0, VOLUME_ROLE, v.index)
                g.setFirstColumnSpanned(False)
                g.setExpanded(True)
                gruppen[v.index] = g

        for e in entries:
            eltern = gruppen.get(e.volume, self.tree) if mehrseitig else self.tree
            item = QTreeWidgetItem(eltern, [
                e.name, e.type, f"{e.size}", e.attrs, e.date,
            ])
            item.setData(0, REF_ROLE, e.ref)
            item.setData(0, VOLUME_ROLE, e.volume)
            if e.damaged:
                item.setText(1, "DEFEKT")

        teile = [f"{len(entries)} Dateien"]
        for v in tool.volumes():
            wo = f"{v.dir}: " if mehrseitig else ""
            teile.append(f"{wo}{v.free // 1024} KB frei")
        self.fuss.setText(" · ".join(teile))

    # ── Auswahl ─────────────────────────────────────────────────────────────

    def selected_refs(self) -> List[str]:
        """Eindeutige Bezeichnungen der ausgewählten Dateien (`Side1/NAME`)."""
        return [r for r in (i.data(0, REF_ROLE) for i in self.tree.selectedItems()) if r]

    def current_volume(self) -> int:
        """Seite der aktuellen Auswahl (0, wenn nichts ausgewählt ist)."""
        item = self.tree.currentItem()
        while item is not None:
            v = item.data(0, VOLUME_ROLE)
            if v is not None:
                return int(v)
            item = item.parent()
        return 0
