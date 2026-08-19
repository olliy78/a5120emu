"""„Gelöschte Dateien suchen…" — was ein Löschen übriggelassen hat.

Gegenstück zum Reparaturdialog, und mit einer anderen Grundhaltung: dort geht es
um einen Schaden, hier um einen Fund.  Keines der Dateisysteme überschreibt beim
Löschen Nutzdaten — bei CP/M bleiben Name, Satzzahl und alle Blockzeiger im
Verzeichnisplatz stehen, nur das Nutzerbyte wird 0xE5
(doc/design/15_dateisystempruefung.md §13).

Vier Festlegungen tragen diesen Dialog:

* **Retten geht vor Wiederherstellen** (E7).  „In den Ordner retten…" ist der
  Vorgabeknopf und bleibt an einer schreibgeschützten Diskette voll bedienbar —
  der häufigste Fall ist „einmal alles retten, was noch da ist, dann die Diskette
  in Ruhe lassen".  Auf die Diskette zurückzuschreiben ist der Ausnahmefall.
* **Die Güte ist kein Gefühl, sondern eine Aussage mit Belegen.**  Zu jedem Fund
  steht im Klartext, woran es hängt („Block 44 gehört jetzt zu HELP.DAT") — eine
  Sterneskala ohne Begründung wäre an einer dreißig Jahre alten Diskette wertlos.
* **Ein Fund ohne Namen ist trotzdem ein Fund.**  Namenlose bekommen einen
  Vorschlag, unter dem sie sich ohne Rückfrage speichern lassen; ändern lässt er
  sich in der Liste.
* **Die Suchtiefe ist eine Entscheidung des Bedieners.**  Die Verzeichnissuche
  sieht nur ins Verzeichnis, die Oberflächensuche in jeden freien Bereich — an
  einer echten Diskette ist das der Unterschied zwischen einem Wimpernschlag und
  ein bis zwei Minuten, und deshalb ist sie dort (noch) gesperrt.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QHBoxLayout,
    QInputDialog, QLabel, QMessageBox, QSplitter, QTextBrowser, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

from app.core_binding.k1520disk import BRUCHSTUECK, SICHER

#: Zeichen je Güte — dieselbe Sprache wie die Schwerezeichen des Prüfdialogs.
ZEICHEN = {SICHER: "✔", 1: "≈", BRUCHSTUECK: "✂"}

#: Rolle, unter der die Fundnummer am Baumeintrag hängt (Index in ``bericht.finds``).
ROLLE_FUND = int(Qt.UserRole) + 1


def hexdump(daten: bytes, breite: int = 16) -> str:
    """Hexdump mit ASCII-Spalte — dieselbe Darstellung wie im Diskeditor."""
    zeilen = []
    for off in range(0, len(daten), breite):
        stueck = daten[off:off + breite]
        hexteil = " ".join(f"{b:02X}" for b in stueck).ljust(breite * 3 - 1)
        text = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in stueck)
        zeilen.append(f"{off:04X}  {hexteil}  {text}")
    return "\n".join(zeilen)


def alsText(daten: bytes) -> str:
    """Der Inhalt als Text — bis zum CP/M-Dateiende 0x1A."""
    ende = daten.find(b"\x1a")
    roh = daten[:ende] if ende >= 0 else daten
    return roh.decode("cp437", "replace").replace("\r\n", "\n").replace("\r", "\n")


class RecoverDialog(QDialog):
    """Fundliste, Vorschau und die drei Wege, mit einem Fund umzugehen.

    :param tool:        die offene Diskette (:class:`K1520Disk`)
    :param zielordner:  Startverzeichnis für „retten" (die Ordnerseite)
    :param log:         Rückruf ``(text) -> None`` für das Protokoll des Fensters
    """

    def __init__(self, tool, parent=None, zielordner=None, log=None):
        super().__init__(parent)
        self.setWindowTitle("Gelöschte Dateien suchen")
        self.resize(900, 620)
        self.tool = tool
        self._zielordner = str(zielordner or Path.home())
        self._log = log or (lambda text: None)
        #: Wurde etwas auf der Diskette eingetragen?  Dann muss das Fenster dahinter
        #: die Dateiliste neu lesen.
        self.wiederhergestellt = 0
        #: Von der Liste vergebene Namen (Fundnummer → Name), für namenlose Funde.
        self._namen = {}

        self.kopf = QLabel("")
        self.kopf.setTextInteractionFlags(Qt.TextSelectableByMouse)

        self.tiefe = QComboBox()
        self.tiefe.addItem("Verzeichnisreste (schnell)", 0)
        self.tiefe.addItem("Ganze Oberfläche (jeder freie Bereich)", 1)
        self.tiefe.currentIndexChanged.connect(lambda *_: self._suchen())
        # An einer PHYSISCHEN Diskette zöge die Oberflächensuche die ganze Scheibe
        # ein (0,5–0,8 s je Spur).  Dasselbe Zugeständnis wie bei der Vollprüfung:
        # bis es einen Arbeitsfaden mit Fortschritt gibt, bleibt sie dort gesperrt —
        # ein Fenster, das zwei Minuten steht, sieht aus wie ein Absturz.
        if not tool.path:
            self.tiefe.model().item(1).setEnabled(False)
            self.tiefe.setToolTip(
                "An einer echten Diskette müsste für die Oberflächensuche jede Spur "
                "einzeln gelesen werden (ein bis zwei Minuten) — das kommt mit einem "
                "Arbeitsfaden mit Fortschrittsanzeige.")

        kopfzeile = QHBoxLayout()
        kopfzeile.addWidget(QLabel("Suchtiefe:"))
        kopfzeile.addWidget(self.tiefe, 1)

        self.baum = QTreeWidget()
        self.baum.setColumnCount(5)
        self.baum.setHeaderLabels(["Name", "Typ", "Größe", "Güte", "Herkunft"])
        self.baum.setRootIsDecorated(False)
        self.baum.setSelectionMode(QAbstractItemView.SingleSelection)
        self.baum.setAlternatingRowColors(True)
        self.baum.currentItemChanged.connect(lambda *_: self._vorschau_zeigen())
        self.baum.itemChanged.connect(self._name_geaendert)

        self.vorschau = QTextBrowser()
        self.vorschau.setLineWrapMode(QTextBrowser.NoWrap)
        self.darstellung = QComboBox()
        self.darstellung.addItems(["Hexdump", "Text"])
        self.darstellung.currentIndexChanged.connect(lambda *_: self._vorschau_zeigen())

        rechts = QWidget()
        rechts_lay = QVBoxLayout(rechts)
        rechts_lay.setContentsMargins(0, 0, 0, 0)
        zeile = QHBoxLayout()
        zeile.addWidget(QLabel("Vorschau:"))
        zeile.addWidget(self.darstellung)
        zeile.addStretch(1)
        rechts_lay.addLayout(zeile)
        rechts_lay.addWidget(self.vorschau, 1)

        teiler = QSplitter(Qt.Horizontal)
        teiler.addWidget(self.baum)
        teiler.addWidget(rechts)
        teiler.setStretchFactor(0, 3)
        teiler.setStretchFactor(1, 2)

        #: Warum gerade nicht wiederhergestellt werden kann — über den Knöpfen,
        #: nicht im Tooltip: ein gesperrter Knopf ohne Begründung ist eine Sackgasse.
        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Close)
        self.b_retten = self.knoepfe.addButton("In den &Ordner retten…",
                                               QDialogButtonBox.AcceptRole)
        self.b_retten.clicked.connect(self._retten)
        self.b_alles = self.knoepfe.addButton("&Alles Sichere retten…",
                                              QDialogButtonBox.ActionRole)
        self.b_alles.clicked.connect(self._alles_sichere_retten)
        self.b_zurueck = self.knoepfe.addButton("Auf der Diskette &wiederherstellen",
                                                QDialogButtonBox.ApplyRole)
        self.b_zurueck.clicked.connect(self._wiederherstellen)
        self.knoepfe.rejected.connect(self.reject)

        lay = QVBoxLayout(self)
        lay.addWidget(self.kopf)
        lay.addLayout(kopfzeile)
        lay.addWidget(teiler, 1)
        lay.addWidget(self.hinweis)
        lay.addWidget(self.knoepfe)

        self._suchen()

    # ── Suchen und anzeigen ─────────────────────────────────────────────────

    def _suchen(self) -> None:
        """Suchlauf in der gewählten Tiefe und die Liste neu aufbauen."""
        voll = self.tiefe.currentData() == 1
        QGuiApplication.setOverrideCursor(Qt.WaitCursor)
        try:
            self.bericht = self.tool.recover_scan(voll=voll)
        finally:
            QGuiApplication.restoreOverrideCursor()
        self._namen.clear()
        self._fuellen()

    def _fuellen(self) -> None:
        self.baum.blockSignals(True)
        self.baum.clear()
        for i, f in enumerate(self.bericht.finds):
            eintrag = QTreeWidgetItem(self.baum)
            eintrag.setData(0, ROLLE_FUND, i)
            eintrag.setText(0, f.name or f.suggestion)
            eintrag.setText(1, f.type)
            eintrag.setText(2, str(f.size))
            eintrag.setText(3, f"{ZEICHEN.get(f.quality, '?')} {f.guete}")
            eintrag.setText(4, f.origin)
            if f.detail:
                eintrag.setToolTip(3, f.detail)
                eintrag.setToolTip(4, f.detail)
            # Namenlose Funde bekommen ein Namensfeld direkt in der Liste — sie
            # heissen sonst alle „fragment_…" und sind nach dem Retten nicht mehr
            # auseinanderzuhalten.
            if not f.name:
                eintrag.setFlags(eintrag.flags() | Qt.ItemIsEditable)
        self.baum.blockSignals(False)
        for spalte in range(5):
            self.baum.resizeColumnToContents(spalte)

        gefunden = len(self.bericht.finds)
        was = "kein Fund" if not gefunden else f"{gefunden} Fund(e)"
        unvollstaendig = "" if self.bericht.complete else \
            " — UNVOLLSTÄNDIG: es wurden nicht alle Spuren angesehen"
        self.kopf.setText(
            f"{self.tool.path or 'physische Diskette'} · {self.tool.filesystem}\n"
            f"{was}{unvollstaendig}")

        if self.bericht.finds:
            self.baum.setCurrentItem(self.baum.topLevelItem(0))
        self._vorschau_zeigen()
        self._knoepfe_nachziehen()

    def _name_geaendert(self, eintrag, spalte) -> None:
        if spalte != 0:
            return
        i = eintrag.data(0, ROLLE_FUND)
        if i is not None:
            self._namen[i] = eintrag.text(0)

    # ── Zustand ─────────────────────────────────────────────────────────────

    def _aktueller_index(self):
        eintrag = self.baum.currentItem()
        return None if eintrag is None else eintrag.data(0, ROLLE_FUND)

    def _aktueller_fund(self):
        i = self._aktueller_index()
        return None if i is None else self.bericht.finds[i]

    def zielname(self, i: int) -> str:
        """Unter welchem Namen der Fund gespeichert wird (Liste schlägt Vorschlag)."""
        f = self.bericht.finds[i]
        return self._namen.get(i) or f.name or f.suggestion

    def _vorschau_zeigen(self) -> None:
        i = self._aktueller_index()
        if i is None:
            self.vorschau.setPlainText("")
            self._knoepfe_nachziehen()
            return
        f = self.bericht.finds[i]
        try:
            daten = self.tool.recover_preview(i, 512)
        except Exception as fehler:                      # K1520DiskError
            self.vorschau.setPlainText(f"Nicht lesbar: {fehler}")
            self._knoepfe_nachziehen()
            return
        kopf = [f"{f.guete} · {f.origin} · {f.size} Byte"]
        if f.detail:
            kopf.append(f.detail)
        inhalt = (alsText(daten) if self.darstellung.currentText() == "Text"
                  else hexdump(daten))
        self.vorschau.setPlainText("\n".join(kopf) + "\n\n" + inhalt)
        self._knoepfe_nachziehen()

    def _knoepfe_nachziehen(self) -> None:
        f = self._aktueller_fund()
        etwas = f is not None
        # Retten geht IMMER — auch schreibgeschützt, auch bei Bruchstücken (E7).
        self.b_retten.setEnabled(etwas)
        self.b_alles.setEnabled(bool(self.bericht.ab(SICHER)))
        schreibgeschuetzt = bool(self.tool.read_only)
        self.b_zurueck.setEnabled(bool(etwas and f.restorable and not schreibgeschuetzt))

        if not etwas:
            self.hinweis.setText("")
        elif schreibgeschuetzt:
            self.hinweis.setText(
                "🔒 Die Diskette ist schreibgeschützt — auf ihr eintragen geht nicht. "
                "Retten geht: das ist der übliche Weg.")
        elif not f.restorable:
            self.hinweis.setText("Auf der Diskette eintragen geht hier nicht: "
                                 + (f.blocked_why or "kein Grund genannt"))
        else:
            self.hinweis.setText("")

    # ── Handlungen ──────────────────────────────────────────────────────────

    def _retten(self) -> None:
        i = self._aktueller_index()
        if i is None:
            return
        vorschlag = str(Path(self._zielordner) / self.zielname(i))
        pfad, _ = QFileDialog.getSaveFileName(self, "Fund retten", vorschlag)
        if not pfad:
            return
        try:
            self.tool.recover_extract(i, pfad)
        except Exception as fehler:                      # K1520DiskError
            QMessageBox.critical(self, "Retten fehlgeschlagen", str(fehler))
            return
        f = self.bericht.finds[i]
        self._log(f"gerettet: {self.zielname(i)} → {pfad} ({f.guete})")
        if f.quality != SICHER:
            QMessageBox.information(
                self, "Gerettet",
                f"„{Path(pfad).name}\" ist gerettet — mit Vorbehalt ({f.guete}).\n\n"
                f"{f.detail}\n\nDaneben liegt ein Beiblatt „{Path(pfad).name}"
                ".rettung.txt\", das das festhält.")

    def _alles_sichere_retten(self) -> None:
        """Alle Funde der Güte *sicher* in EINEN Ordner — der Massenweg."""
        sichere = [i for i, f in enumerate(self.bericht.finds) if f.quality == SICHER]
        if not sichere:
            return
        ordner = QFileDialog.getExistingDirectory(self, "Alles Sichere retten nach…",
                                                  self._zielordner)
        if not ordner:
            return
        getan, fehler = 0, []
        for i in sichere:
            try:
                self.tool.recover_extract(i, str(Path(ordner) / self.zielname(i)))
                getan += 1
            except Exception as f:                       # K1520DiskError
                fehler.append(f"{self.zielname(i)}: {f}")
        self._log(f"{getan} von {len(sichere)} sicheren Funden nach {ordner} gerettet")
        text = f"{getan} von {len(sichere)} Funden gerettet."
        if fehler:
            text += "\n\nNicht gerettet:\n" + "\n".join(fehler)
        QMessageBox.information(self, "Alles Sichere retten", text)

    def _wiederherstellen(self) -> None:
        i = self._aktueller_index()
        if i is None:
            return
        f = self.bericht.finds[i]
        name, ok = QInputDialog.getText(self, "Auf der Diskette wiederherstellen",
                                        "Unter welchem Namen?", text=self.zielname(i))
        if not ok or not name:
            return
        if f.quality == BRUCHSTUECK and QMessageBox.question(
                self, "Bruchstück wiederherstellen",
                f"„{name}\" ist ein Bruchstück:\n\n{f.detail}\n\n"
                "Trotzdem auf der Diskette eintragen?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        try:
            self.tool.recover_restore(i, name)
        except Exception as fehler:                      # K1520DiskError
            QMessageBox.critical(self, "Wiederherstellen fehlgeschlagen",
                                 f"{fehler}\n\nDie Diskette ist unverändert.")
            self._log(f"Wiederherstellen fehlgeschlagen: {fehler}")
            return
        self.wiederhergestellt += 1
        self._log(f"wiederhergestellt: {name} ({f.origin})")
        # recover_restore hat schon neu gesucht — die Liste neu lesen, jeder
        # bisherige Index ist hinfällig.
        self.bericht = self.tool.recover_finds()
        self._namen.clear()
        self._fuellen()
        QMessageBox.information(self, "Wiederhergestellt",
                                f"„{name}\" steht wieder im Verzeichnis.")
