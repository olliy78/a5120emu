"""Rechte Hälfte: der Ordner des Wirtsystems.

Sie ist ein kleiner **Dateibrowser**: oben eine editierbare Adresszeile mit dem
Ordnerknopf daneben, darunter die Liste.  Darin führt `..` eine Ebene hinauf, und
ein Verzeichnis öffnet sich beim Aktivieren (Doppelklick, `Enter` — und einfacher
Klick, wenn das Thema so eingestellt ist).  Damit gibt es den früheren Zustand
„kein Ordner gewählt" nicht mehr: beim Start steht der Standardordner darin
(`paths.default_folder_dir()`), von dem aus man sich zum Ziel klickt.

`Side0/`/`Side1/` bleiben als **aufgeklappte Gruppen** stehen — sie sind die
Darstellung des zusammengesetzten Datenträgers (§9.1) und zugleich das Ziel beim
Schreiben (`selected_side`).  Jedes ANDERE Verzeichnis ist nur ein Wegpunkt und
wird deshalb nicht aufgeklappt: in einem Heimatverzeichnis wäre der Inhalt aller
Unterordner keine Auskunft, sondern eine Wand.

Angelegt und umbenannt wird **an Ort und Stelle**: „Neuer Ordner" legt `neu`
(bzw. `neu2`, `neu3`, …) an und öffnet sofort das Eingabefeld in der Zeile;
„Umbenennen" (F2) tut dasselbe an einem vorhandenen Eintrag.  Übernommen wird mit
der Eingabetaste oder einem Klick daneben — das erledigt Qts Editor von selbst,
`Esc` verwirft.  Der Baum hat dafür **`NoEditTriggers`**: sonst öffnete der
Doppelklick, der hier navigiert, stattdessen das Eingabefeld.

Die Fusszeile mit der Dateizahl gibt es weiterhin nicht — Zahlen stehen in der
Statuszeile, Meldungen (unlesbarer Ordner, unsinnige Adresse) gehen über
`hinweis` an das Protokoll (§20.4).
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractItemView, QHBoxLayout, QLineEdit, QMenu, QToolButton, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

from app.disktool.ui.disk_view import DISK_MIME
from app.disktool.ui.icons import icon

PATH_ROLE = Qt.UserRole + 1
#: Ziel eines Verzeichniseintrags (`..` eingeschlossen) — nur diese Zeilen
#: navigieren; Dateien tragen die Rolle nicht.
NAV_ROLE = Qt.UserRole + 2


def _ist_seite(name: str) -> bool:
    """`Side0`, `side1`, … — die Gruppen des zusammengesetzten Datenträgers."""
    n = name.rstrip("/").lower()
    return n.startswith("side") and n[4:].isdigit()


class _Tree(QTreeWidget):
    """Baum, der von der Diskette gezogene Dateien annimmt."""

    disk_files_dropped = Signal(list)   # Referenzen ("Side1/NAME")
    nach_oben = Signal()                # Rücktaste

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptDrops(True)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setUniformRowHeights(True)
        self.setAlternatingRowColors(True)
        # Umbenannt wird nur auf Ansage (`editItem`).  Mit den Vorgabe-Auslösern
        # öffnete der Doppelklick das Eingabefeld statt den Ordner.
        self.setEditTriggers(QAbstractItemView.NoEditTriggers)

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

    def keyPressEvent(self, event):  # noqa: N802
        # Rücktaste = eine Ebene hinauf, wie in jedem Dateimanager.  In dieser
        # Liste hat sie sonst keine Bedeutung (nichts wird hier getippt).
        if event.key() == Qt.Key_Backspace:
            self.nach_oben.emit()
            return
        super().keyPressEvent(event)


class FolderView(QWidget):
    """Ordnerseite des Fensters."""

    folder_changed = Signal(str)
    disk_files_dropped = Signal(list)
    choose_requested = Signal()
    #: Einzeiler fürs Protokoll/die Statuszeile (unlesbarer Ordner, Fehleingabe).
    hinweis = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._folder: Optional[Path] = None
        self._aktionen = []
        #: Zeile, die gerade im Eingabefeld steht (sonst ``None``) — nur für sie
        #: gilt ein `itemChanged` als Umbenennung.
        self._umbenennen_item = None
        self._umbenennen_alt = ""

        self.adresse = QLineEdit()
        self.adresse.setClearButtonEnabled(False)
        self.adresse.setPlaceholderText("Pfad eingeben und mit Eingabe bestätigen")
        self.adresse.setToolTip("Pfad des Ordners — änderbar, Eingabe wechselt dorthin")
        self.adresse.returnPressed.connect(self._adresse_uebernehmen)

        self.knopf_ordner = QToolButton()
        self.knopf_ordner.setIcon(icon("folder"))
        self.knopf_ordner.setAutoRaise(True)
        self.knopf_ordner.setToolTip("Ordner im Dialog wählen")
        self.knopf_ordner.clicked.connect(self.choose_requested)

        # Die Kopfzeile ist ein eigenes Widget, damit das Fenster sie in der Höhe
        # an die Überschrift der Diskettenseite binden kann (§20.10) — sonst
        # begännen die beiden Listen ein paar Pixel versetzt.
        kopf = QHBoxLayout()
        kopf.setContentsMargins(0, 0, 0, 0)
        kopf.setSpacing(2)
        kopf.addWidget(self.adresse, 1)
        kopf.addWidget(self.knopf_ordner)
        self.kopfzeile = QWidget()
        self.kopfzeile.setLayout(kopf)

        self.tree = _Tree()
        self.tree.setHeaderLabels(["Name", "Größe"])
        self.tree.setColumnWidth(0, 260)
        self.tree.disk_files_dropped.connect(self.disk_files_dropped)
        self.tree.nach_oben.connect(self.hinauf)
        self.tree.itemActivated.connect(self._aktiviert)
        self.tree.itemChanged.connect(self._eintrag_geaendert)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._kontextmenue)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        lay.addWidget(self.kopfzeile)
        lay.addWidget(self.tree, 1)

    # ── Ordner ──────────────────────────────────────────────────────────────

    def setze_aktionen(self, *aktionen) -> None:
        """Aktionen des Kontextmenüs (``None`` = Trenner)."""
        self._aktionen = list(aktionen)

    @property
    def folder(self) -> Optional[Path]:
        return self._folder

    def set_folder(self, path) -> None:
        self._folder = Path(path).expanduser() if path else None
        self.adresse.setText(str(self._folder) if self._folder else "")
        self.refresh()
        if self._folder:
            self.folder_changed.emit(str(self._folder))

    # ── Navigation ──────────────────────────────────────────────────────────

    def hinauf(self) -> None:
        """Eine Ebene höher — an der Wurzel geschieht nichts."""
        if self._folder is None:
            return
        eltern = self._folder.parent
        if eltern != self._folder:
            self.set_folder(eltern)

    def _aktiviert(self, item, spalte: int) -> None:
        ziel = item.data(0, NAV_ROLE)
        if ziel:
            self.set_folder(ziel)

    def _adresse_uebernehmen(self) -> None:
        """Eingegebene Adresse übernehmen — oder sie zurücksetzen und melden.

        Auf eine **Datei** wird nicht abgewiesen: gemeint ist dann ihr Ordner,
        und sie steht danach ausgewählt in der Liste (wie beim Einfügen eines
        Pfades aus der Zwischenablage).
        """
        text = self.adresse.text().strip()
        ziel = Path(text).expanduser() if text else None
        if ziel is not None and ziel.is_dir():
            self.set_folder(ziel)
            return
        if ziel is not None and ziel.is_file():
            self.set_folder(ziel.parent)
            self._auswaehlen(str(ziel))
            return
        if text:
            self.hinweis.emit(f"Kein Ordner: {text}")
        self.adresse.setText(str(self._folder) if self._folder else "")

    def _auswaehlen(self, pfad: str) -> None:
        item = self._finde(pfad)
        if item is None:
            return
        self.tree.clearSelection()
        item.setSelected(True)
        self.tree.setCurrentItem(item)

    # ── Anlegen und Umbenennen (im Wirtsystem, nicht auf der Diskette) ──────

    def neuer_ordner(self) -> None:
        """`neu` (bzw. `neu2`, `neu3`, …) anlegen und sofort umbenennen lassen."""
        if self._folder is None:
            return
        ziel = self._freier_name()
        try:
            ziel.mkdir()
        except OSError as fehler:
            self.hinweis.emit(f"Ordner nicht angelegt: {ziel.name} ({fehler.strerror})")
            return
        self.refresh()
        self.hinweis.emit(f"Ordner angelegt: {ziel.name}")
        item = self._finde(str(ziel))
        if item is not None:
            self.umbenennen_starten(item)

    def _freier_name(self) -> Path:
        """Der erste freie Name der Reihe `neu`, `neu2`, `neu3`, …"""
        ziel = self._folder / "neu"
        nummer = 2
        while ziel.exists():
            ziel = self._folder / f"neu{nummer}"
            nummer += 1
        return ziel

    def umbenennbar(self) -> Optional[QTreeWidgetItem]:
        """Die Zeile, die umbenannt werden könnte — `..` und Leerauswahl: ``None``."""
        item = self.tree.currentItem()
        if item is not None and item.isSelected() and self._pfad_von(item):
            return item
        for anderes in self.tree.selectedItems():
            if self._pfad_von(anderes):
                return anderes
        return None

    def umbenennen_starten(self, item: Optional[QTreeWidgetItem] = None) -> None:
        """Das Eingabefeld in der Zeile öffnen (Windows-Sitte)."""
        if item is None:
            item = self.umbenennbar()
        if item is None or not self._pfad_von(item):
            return
        name = item.text(0)
        if name.endswith("/"):
            # Der Schrägstrich ist Darstellung, kein Namensteil — er gehört nicht
            # ins Eingabefeld.  `refresh()` baut ihn danach wieder auf.
            item.setText(0, name[:-1])
        item.setFlags(item.flags() | Qt.ItemIsEditable)
        self._umbenennen_item = item
        self._umbenennen_alt = self._pfad_von(item)
        self.tree.setCurrentItem(item)
        self.tree.editItem(item, 0)

    def _eintrag_geaendert(self, item: QTreeWidgetItem, spalte: int) -> None:
        """Übernahme aus dem Eingabefeld — mit Eingabetaste ODER Klick daneben.

        `itemChanged` kommt auch beim Aufbau der Liste; gehandelt wird deshalb
        nur für die eine Zeile, die wirklich im Eingabefeld steht.
        """
        if spalte != 0 or item is not self._umbenennen_item:
            return
        alt = Path(self._umbenennen_alt)
        self._umbenennen_item = None
        neu = item.text(0).strip().rstrip("/")

        if not neu or neu == alt.name:
            self.refresh()                      # Abbruch: Darstellung wiederherstellen
            return
        if neu in (".", "..") or "/" in neu or "\\" in neu:
            self.hinweis.emit(f"Kein gültiger Name: {neu}")
            self.refresh()
            return
        ziel = alt.parent / neu
        if ziel.exists():
            self.hinweis.emit(f"Gibt es schon: {neu}")
            self.refresh()
            return
        try:
            alt.rename(ziel)
        except OSError as fehler:
            self.hinweis.emit(f"Nicht umbenannt: {alt.name} ({fehler.strerror})")
            self.refresh()
            return
        self.hinweis.emit(f"Umbenannt: {alt.name} → {neu}")
        self.refresh()
        self._auswaehlen(str(ziel))

    @staticmethod
    def _pfad_von(item: QTreeWidgetItem) -> str:
        """Pfad hinter einer Zeile — Datei ODER Ordner; `..` hat keinen."""
        if item.text(0) == "..":
            return ""
        return item.data(0, PATH_ROLE) or item.data(0, NAV_ROLE) or ""

    def _finde(self, pfad: str) -> Optional[QTreeWidgetItem]:
        """Die Zeile zu einem Pfad, über beide Ebenen."""
        for i in range(self.tree.topLevelItemCount()):
            oben = self.tree.topLevelItem(i)
            if self._pfad_von(oben) == pfad:
                return oben
            for k in range(oben.childCount()):
                if self._pfad_von(oben.child(k)) == pfad:
                    return oben.child(k)
        return None

    # ── Inhalt ──────────────────────────────────────────────────────────────

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
        self._umbenennen_item = None       # die Zeilen von eben gibt es gleich nicht mehr
        self.tree.clear()
        if not self._folder or not self._folder.is_dir():
            return

        # `..` zuerst und IMMER, wenn es eine Ebene darüber gibt — auch dann,
        # wenn der Ordner sich gleich als unlesbar erweist: sonst sässe der
        # Anwender dort fest.
        eltern = self._folder.parent
        if eltern != self._folder:
            auf = QTreeWidgetItem(self.tree, ["..", ""])
            auf.setData(0, NAV_ROLE, str(eltern))
            auf.setIcon(0, icon("folder"))
            auf.setToolTip(0, f"Hinauf nach {eltern}")

        try:
            eintraege = sorted(self._folder.iterdir(),
                               key=lambda p: (not p.is_dir(), p.name.lower()))
        except OSError as fehler:
            self.hinweis.emit(f"Ordner nicht lesbar: {self._folder} ({fehler.strerror})")
            return

        for eintrag in eintraege:
            if eintrag.is_dir():
                gruppe = QTreeWidgetItem(self.tree, [eintrag.name + "/", ""])
                gruppe.setData(0, NAV_ROLE, str(eintrag))
                gruppe.setIcon(0, icon("folder"))
                if _ist_seite(eintrag.name):
                    self._seite_fuellen(gruppe, eintrag)
            else:
                item = QTreeWidgetItem(self.tree, [eintrag.name,
                                                   self._groesse(eintrag)])
                item.setData(0, PATH_ROLE, str(eintrag))

    def _seite_fuellen(self, gruppe: QTreeWidgetItem, ordner: Path) -> None:
        """Die Dateien einer `SideN/`-Gruppe darunterhängen und aufklappen."""
        try:
            kinder = sorted(ordner.iterdir(), key=lambda p: p.name.lower())
        except OSError:
            return
        for kind in kinder:
            if kind.is_dir():
                continue
            item = QTreeWidgetItem(gruppe, [kind.name, self._groesse(kind)])
            item.setData(0, PATH_ROLE, str(kind))
        gruppe.setExpanded(True)

    # ── Kontextmenü ─────────────────────────────────────────────────────────

    def _kontextmenue(self, punkt) -> None:
        if not self._aktionen:
            return
        item = self.tree.itemAt(punkt)
        # Der angeklickte Eintrag zählt — Datei ODER Ordner (`Umbenennen` gilt für
        # beide), sofern er nicht ohnehin schon ausgewählt ist.
        if item is not None and self._pfad_von(item) and not item.isSelected():
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
