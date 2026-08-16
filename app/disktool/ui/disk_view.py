"""Linke Hälfte: der Inhalt der Diskette.

Beide Seiten einer UDOS-Diskette stehen in **einer** Liste, nach Seite gruppiert —
es wird nicht umgeschaltet (doc/design/13_k1520disktool.md §9.1).  Bei einer
CP/M-Diskette entfällt die Gruppierung ersatzlos.

Die Ansicht hält **keinen** eigenen Verzeichnisstand: `set_disk()` bekommt bei
jedem Aufruf die frisch gelesene Liste (§9.3).

Sie trägt seit dem Umbau §20 auch **keine Meldungen** mehr: Pfad und Format stehen
im Kopfbereich des Fensters, Auffälligkeiten im Meldungsstreifen, die Belegung
rechts in der Statuszeile.  Hier ist nur noch die Liste.
"""

from __future__ import annotations

from typing import List, Optional

from PySide6.QtCore import Qt, QMimeData, Signal
from PySide6.QtGui import QDrag
from PySide6.QtWidgets import (
    QAbstractItemView, QHeaderView, QLabel, QMenu, QTreeWidget, QTreeWidgetItem,
    QVBoxLayout, QWidget,
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
        self.setAlternatingRowColors(True)
        self.setSortingEnabled(False)

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
    """Diskettenseite des Fensters: Überschrift und Liste."""

    files_dropped = Signal(list, int)
    #: Rechtsklick → „Eigenschaften…" bzw. Doppelklick auf eine Datei.
    properties_requested = Signal(str)     # Referenz („Side1/NAME")
    extract_requested = Signal(list)       # Referenzen
    erase_requested = Signal(list)         # Referenzen

    def __init__(self, parent=None):
        super().__init__(parent)
        self._aktionen = []

        self.titel = QLabel("Diskette")
        schrift = self.titel.font()
        schrift.setBold(True)
        self.titel.setFont(schrift)

        self.tree = _Tree()
        self.tree.setHeaderLabels(["Name", "Typ", "Größe", "Eigensch.", "Datum"])
        # Der Name nimmt, was übrig ist; die vier schmalen Spalten richten sich
        # nach ihrem Inhalt — sonst wird das Datum abgeschnitten und die Liste
        # bekommt einen waagerechten Rollbalken, obwohl Platz da wäre.
        kopfzeile = self.tree.header()
        kopfzeile.setSectionResizeMode(0, QHeaderView.Stretch)
        for spalte in (1, 2, 3, 4):
            kopfzeile.setSectionResizeMode(spalte, QHeaderView.ResizeToContents)
        self.tree.setTextElideMode(Qt.ElideMiddle)
        self.tree.files_dropped.connect(self.files_dropped)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._kontextmenue)
        self.tree.itemDoubleClicked.connect(self._doppelklick)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        lay.addWidget(self.titel)
        lay.addWidget(self.tree, 1)

    # ── Inhalt setzen ───────────────────────────────────────────────────────

    def setze_aktionen(self, *aktionen) -> None:
        """Die Aktionen des Kontextmenüs — dieselben wie in Menü und Leiste.

        ``None`` steht für einen Trenner.
        """
        self._aktionen = list(aktionen)

    def clear(self) -> None:
        self.tree.clear()
        self.titel.setText("Diskette")

    def set_disk(self, tool, entries) -> None:
        """Baum aus einer geöffneten Diskette füllen.

        Args:
            tool: ``DiskTool`` (oder None)
            entries: die **frisch gelesene** Liste von ``Entry``
        """
        self.tree.clear()
        if tool is None:
            self.clear()
            return

        self.titel.setText(f"Diskette — {tool.filesystem}")

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

        self._zeilen = {}
        for nr, e in enumerate(entries):
            eltern = gruppen.get(e.volume, self.tree) if mehrseitig else self.tree
            item = QTreeWidgetItem(eltern, self._spalten(e))
            item.setData(0, REF_ROLE, e.ref)
            item.setData(0, VOLUME_ROLE, e.volume)
            if e.damaged:
                item.setText(1, "DEFEKT")
            # Zeile über die LAUFENDE NUMMER merken: genau darüber spricht die
            # Bibliothek beim Nachtragen (`load_entry_details(i)`), und Namen sind
            # bei UDOS über beide Seiten hinweg nicht eindeutig.
            self._zeilen[nr] = item

    @staticmethod
    def _spalten(e) -> List[str]:
        """Die fünf Spalten eines Eintrags.

        Solange die Angaben aus dem Kopfsektor fehlen (UDOS an einer physischen
        Diskette), steht dort ein Gedankenstrich — **nicht** „0 Byte": eine Null
        wäre eine Behauptung, der Strich sagt „noch nicht gelesen".
        """
        if not getattr(e, "details_loaded", True):
            return [e.name, "…", "…", "", ""]
        return [e.name, e.type, f"{e.size}", e.attrs, e.date]

    def eintrag_auffrischen(self, nr: int, e) -> None:
        """Eine Zeile mit nachgetragenen Angaben neu beschriften."""
        item = getattr(self, "_zeilen", {}).get(nr)
        if item is None:
            return
        for spalte, text in enumerate(self._spalten(e)):
            item.setText(spalte, text)
        if e.damaged:
            item.setText(1, "DEFEKT")

    # ── Kontextmenü ─────────────────────────────────────────────────────────

    def _kontextmenue(self, punkt) -> None:
        """Rechtsklick auf eine Datei — dieselben Aktionen wie im Menü.

        Auf einer Gruppenzeile (der Seite) oder im Leeren gibt es nichts zu tun —
        dann bleibt das Menü weg statt mit toten Einträgen zu erscheinen.
        """
        item = self.tree.itemAt(punkt)
        if item is not None and item.data(0, REF_ROLE):
            # Der angeklickte Eintrag zählt, auch wenn die Auswahl woanders steht.
            if item.data(0, REF_ROLE) not in self.selected_refs():
                self.tree.clearSelection()
                item.setSelected(True)
                self.tree.setCurrentItem(item)
        if not self.selected_refs() or not self._aktionen:
            return

        menue = QMenu(self)
        for a in self._aktionen:
            menue.addSeparator() if a is None else menue.addAction(a)
        menue.exec(self.tree.viewport().mapToGlobal(punkt))

    def _doppelklick(self, item, spalte) -> None:
        ref = item.data(0, REF_ROLE)
        if ref:
            self.properties_requested.emit(ref)

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
