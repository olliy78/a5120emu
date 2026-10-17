"""„Archivieren…" — Beschriftung plus (einklappbar) Herkunft und Beschreibung.

Die Beschriftung ist der Text auf dem Aufkleber (benennt das Archiv).  Darunter ein
einklappbarer Bereich fuer Angaben, die der Anwender zur Diskette mitgeben will:

* feste Felder (Quelle/URL, Satz/Titel, Version, Etikett, Bemerkung),
* eine Tabelle „weitere Angaben" (freie Schluessel),
* Beschreibungsdateien, die als Beiblaetter an die Wurzel der `.zip` kommen
  (nicht unter `dateien/`, also nie auf die Diskette).

Leere Felder erzeugen keinen Schluessel; bleibt alles leer, ist das Archiv bitgleich
zu dem ohne diesen Bereich.  Die Pruefung steht in ``app/disktool/archiv_meta.py`` und
wird hier nur gerufen; Fehler erscheinen als Zeile im Dialog, nie als Meldungsfenster.
Zuletzt benutzte Werte (ohne die Beilagen) merkt sich die Oberflaeche in den
Einstellungen unter ``archiv/herkunft``.
"""

from __future__ import annotations

import json
from typing import Callable, Dict, List, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QListWidget, QListWidgetItem, QPlainTextEdit, QPushButton,
    QTableWidget, QTableWidgetItem, QToolButton, QVBoxLayout, QWidget,
)

from app.disktool import archiv_meta
from app.disktool.archiv_meta import ArchivMetaFehler

EINSTELLUNG = "archiv/herkunft"
VORSCHAU_ZEILEN = 12


class ArchivDialog(QDialog):
    def __init__(self, vorschlag: str = "", parent=None, einstellungen=None,
                 stamm_fn: Optional[Callable[[str], str]] = None,
                 start_ordner: str = ""):
        super().__init__(parent)
        self.setWindowTitle("Archivieren")
        self.setMinimumWidth(520)
        self._einst = einstellungen
        self._stamm_fn = stamm_fn or (lambda b: "")
        self._start = start_ordner
        # [(Name im Archiv, Text, Kodierung)]
        self._beilagen: List[tuple] = []

        lay = QVBoxLayout(self)
        lay.addWidget(QLabel(
            "Beschriftung der Diskette (z. B. der Text auf dem Aufkleber).\n"
            "Daraus entstehen der Dateiname des Archivs und der Eintrag im\n"
            "Inhaltsverzeichnis:"))
        self.f_bezeichnung = QLineEdit(vorschlag)
        lay.addWidget(self.f_bezeichnung)

        self.b_auf = QToolButton()
        self.b_auf.setText("Herkunft und Beschreibung")
        self.b_auf.setCheckable(True)
        self.b_auf.setArrowType(Qt.RightArrow)
        self.b_auf.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
        self.b_auf.setAutoRaise(True)
        lay.addWidget(self.b_auf)

        self.bereich = QWidget()
        bl = QVBoxLayout(self.bereich)
        bl.setContentsMargins(12, 0, 0, 0)
        form = QFormLayout()
        self.felder: Dict[str, QLineEdit] = {}
        for schluessel, name in archiv_meta.FESTE_FELDER:
            if schluessel == "note":
                self.f_note = QPlainTextEdit()
                self.f_note.setFixedHeight(60)
                form.addRow(name + ":", self.f_note)
            else:
                e = QLineEdit()
                self.felder[schluessel] = e
                form.addRow(name + ":", e)
        bl.addLayout(form)

        bl.addWidget(QLabel("Weitere Angaben (Schlüssel: a–z, 0–9, _):"))
        self.tabelle = QTableWidget(0, 2)
        self.tabelle.setHorizontalHeaderLabels(["Schlüssel", "Wert"])
        self.tabelle.horizontalHeader().setStretchLastSection(True)
        self.tabelle.setFixedHeight(100)
        bl.addWidget(self.tabelle)
        z = QHBoxLayout()
        self.b_zeile_neu = QPushButton("Zeile hinzufügen")
        self.b_zeile_weg = QPushButton("Zeile entfernen")
        self.b_zeile_neu.clicked.connect(lambda: self._zeile())
        self.b_zeile_weg.clicked.connect(self._zeile_entfernen)
        z.addWidget(self.b_zeile_neu)
        z.addWidget(self.b_zeile_weg)
        z.addStretch(1)
        bl.addLayout(z)

        bl.addWidget(QLabel("Beschreibungsdateien (kommen ins Archiv, nicht auf die "
                            "Diskette):"))
        self.liste = QListWidget()
        self.liste.setFixedHeight(70)
        bl.addWidget(self.liste)
        self.vorschau = QPlainTextEdit()
        self.vorschau.setReadOnly(True)
        self.vorschau.setFixedHeight(80)
        bl.addWidget(self.vorschau)
        k = QHBoxLayout()
        self.b_beilage_neu = QPushButton("Hinzufügen…")
        self.b_beilage_weg = QPushButton("Entfernen")
        self.b_beilage_neu.clicked.connect(self._beilage_waehlen)
        self.b_beilage_weg.clicked.connect(self._beilage_entfernen)
        k.addWidget(self.b_beilage_neu)
        k.addWidget(self.b_beilage_weg)
        k.addStretch(1)
        self.b_leer = QPushButton("Zurück auf leer")
        self.b_leer.clicked.connect(self.auf_leer)
        k.addWidget(self.b_leer)
        bl.addLayout(k)
        lay.addWidget(self.bereich)

        self.fehler = QLabel("")
        self.fehler.setStyleSheet("color: #c00;")
        self.fehler.setWordWrap(True)
        lay.addWidget(self.fehler)

        tasten = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        tasten.accepted.connect(self.accept)
        tasten.rejected.connect(self.reject)
        lay.addWidget(tasten)

        self.b_auf.toggled.connect(self._einklappen)
        self.liste.currentRowChanged.connect(self._vorschau)
        self._zuletzt_laden()
        self._einklappen(self.b_auf.isChecked())

    # ── Bedienung ───────────────────────────────────────────────────────────

    def _einklappen(self, offen: bool) -> None:
        self.b_auf.setArrowType(Qt.DownArrow if offen else Qt.RightArrow)
        self.bereich.setVisible(offen)
        self.adjustSize()

    def _zeile(self, schluessel: str = "", wert: str = "") -> None:
        r = self.tabelle.rowCount()
        self.tabelle.insertRow(r)
        self.tabelle.setItem(r, 0, QTableWidgetItem(schluessel))
        self.tabelle.setItem(r, 1, QTableWidgetItem(wert))

    def _zeile_entfernen(self) -> None:
        r = self.tabelle.currentRow()
        if r >= 0:
            self.tabelle.removeRow(r)

    def beilage_hinzufuegen(self, pfad, name: str = "", kodierung: str = "auto") -> bool:
        """Eine Beschreibungsdatei aufnehmen; Fehler stehen im Dialog (``self.fehler``)."""
        try:
            n, text, kod = archiv_meta.beilage_lesen(
                pfad, name, kodierung, stamm=self._stamm_fn(self.bezeichnung()))
            if n.lower() in {b[0].lower() for b in self._beilagen}:
                raise ArchivMetaFehler(f"Beilage '{n}' ist schon in der Liste")
        except ArchivMetaFehler as e:
            self.fehler.setText(str(e))
            return False
        self.fehler.setText("")
        self._beilagen.append((n, text, kod))
        self.liste.addItem(QListWidgetItem(f"{n}  ({kod})"))
        self.liste.setCurrentRow(self.liste.count() - 1)
        return True

    def _beilage_waehlen(self) -> None:
        pfade, _ = QFileDialog.getOpenFileNames(
            self, "Beschreibungsdateien", self._start, "Textdateien (*.txt *.md *.nfo);;"
            "Alle Dateien (*)")
        for p in pfade:
            self.beilage_hinzufuegen(p)

    def _beilage_entfernen(self) -> None:
        r = self.liste.currentRow()
        if r >= 0:
            del self._beilagen[r]
            self.liste.takeItem(r)
            self.vorschau.clear()

    def _vorschau(self, r: int) -> None:
        if not 0 <= r < len(self._beilagen):
            self.vorschau.clear()
            return
        zeilen = self._beilagen[r][1].splitlines()
        text = "\n".join(zeilen[:VORSCHAU_ZEILEN])
        if len(zeilen) > VORSCHAU_ZEILEN:
            text += f"\n… ({len(zeilen) - VORSCHAU_ZEILEN} weitere Zeilen)"
        self.vorschau.setPlainText(text)

    def auf_leer(self) -> None:
        """Alle Angaben leeren und die gemerkten Werte vergessen."""
        for e in self.felder.values():
            e.clear()
        self.f_note.clear()
        self.tabelle.setRowCount(0)
        self._beilagen.clear()
        self.liste.clear()
        self.vorschau.clear()
        self.fehler.setText("")
        if self._einst is not None:
            self._einst.remove(EINSTELLUNG)

    # ── Werte ───────────────────────────────────────────────────────────────

    def bezeichnung(self) -> str:
        return self.f_bezeichnung.text().strip()

    def _roh(self) -> dict:
        d = {k: e.text() for k, e in self.felder.items()}
        d["note"] = self.f_note.toPlainText()
        return d

    def _zeilen(self) -> list:
        return [(self.tabelle.item(r, 0).text().strip(), self.tabelle.item(r, 1).text())
                for r in range(self.tabelle.rowCount())]

    def provenance(self) -> dict:
        """Die Herkunftsangaben; leere Felder fehlen.  Wirft :class:`ArchivMetaFehler`."""
        roh = self._roh()
        for k, v in self._zeilen():
            if not k and not v.strip():
                continue
            if not k:
                raise ArchivMetaFehler("Eine weitere Angabe hat einen Wert, aber keinen "
                                       "Schlüssel")
            archiv_meta.pruefe_schluessel(k)
            if k in roh:
                raise ArchivMetaFehler(f"'{k}' ist ein festes Feld - bitte dort eintragen")
            roh[k] = v
        return archiv_meta.bereinige_provenance(roh)

    def beilagen(self) -> Dict[str, str]:
        return {n: t for n, t, _ in self._beilagen}

    # ── Merken ──────────────────────────────────────────────────────────────

    def _zuletzt_laden(self) -> None:
        if self._einst is None:
            return
        try:
            d = json.loads(self._einst.value(EINSTELLUNG, "") or "{}")
        except (ValueError, TypeError):
            return
        if not isinstance(d, dict):
            return
        for k, e in self.felder.items():
            e.setText(str((d.get("felder") or {}).get(k, "")))
        self.f_note.setPlainText(str((d.get("felder") or {}).get("note", "")))
        for paar in d.get("weitere") or []:
            if isinstance(paar, list) and len(paar) == 2:
                self._zeile(str(paar[0]), str(paar[1]))
        if d.get("felder") or d.get("weitere"):
            self.b_auf.setChecked(True)

    def _zuletzt_sichern(self, prov: dict) -> None:
        if self._einst is None:
            return
        felder = {k: v for k, v in self._roh().items() if v.strip()}
        weitere = [[k, v] for k, v in self._zeilen() if k]
        if felder or weitere:
            self._einst.setValue(EINSTELLUNG, json.dumps(
                {"felder": felder, "weitere": weitere}, ensure_ascii=False))
        else:
            self._einst.remove(EINSTELLUNG)

    def accept(self) -> None:
        try:
            prov = self.provenance()
            archiv_meta.pruefe_beilagen(self.beilagen(),
                                        stamm=self._stamm_fn(self.bezeichnung()))
        except ArchivMetaFehler as e:
            self.fehler.setText(str(e))
            return
        self._zuletzt_sichern(prov)
        super().accept()
