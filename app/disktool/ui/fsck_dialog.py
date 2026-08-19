"""„Dateisystem prüfen und reparieren…" — Befund ansehen, auswählen, eingreifen.

Der Meldungsstreifen trägt eine Zeile, die Diskettenangaben tragen den ganzen
Bericht — aber beide sind nur zum Lesen.  Hier steht derselbe Befund mit dem, was
sich daran tun lässt (doc/design/15_dateisystempruefung.md §16.3).

Drei Festlegungen tragen diesen Dialog; sie sind der Grund für den meisten Code
darin:

* **Nichts ist vorausgewählt, was Daten verwirft.**  Angekreuzt kommt nur, was
  ``empfohlen`` und nicht ``datenverlust`` ist (:pyattr:`Repair.vorauswaehlen`).
* **Ein Befund ohne Reparatur steht ohne Ankreuzfeld da.**  Er ist eine Auskunft,
  kein Versäumnis — ein leeres Kästchen läse sich wie „hier wurde etwas versäumt".
* **Ein gesperrter Vorschlag bleibt sichtbar** und nennt seinen Grund (E8).  Ihn
  wegzulassen hiesse, dem Bediener zu verschweigen, dass es einen Weg gäbe — und
  warum er heute nicht gangbar ist.

Ausgeführt wird in EINER Transaktion über :pymeth:`K1520Disk.apply_repairs`; die
Rangfolge (Verzeichnis → Ketten → Plan → Zähler) macht die Bibliothek, nicht diese
Oberfläche.  Danach ist jeder Index hinfällig, deshalb wird die Liste vollständig
neu aufgebaut.
"""

from __future__ import annotations

from typing import List, Tuple

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView, QComboBox, QDialog, QDialogButtonBox, QLabel, QMessageBox,
    QSplitter, QTextBrowser, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

from app.core_binding.k1520disk import FEHLER, GEFAHR, WARNUNG

#: Zeichen je Schwere — dieselben wie in der Statuszeile des Hauptfensters.
ZEICHEN = {GEFAHR: "⛔", FEHLER: "✖", WARNUNG: "⚠"}

#: Rolle, unter der die Befundnummer am Baumeintrag hängt (Index in `bericht.findings`).
ROLLE_BEFUND = int(Qt.UserRole) + 1


def kopfzeile(tool, bericht) -> str:
    """Eine Zeile Datenträger + eine Zeile Zählwerk."""
    tiefe = "Vollprüfung" if bericht.complete else "Schnellprüfung"
    wo = (f" · {bericht.tracks_read} von {bericht.tracks_total} Spuren angesehen"
          if bericht.tracks_total else "")
    zahlen = []
    for schwere, wort in ((GEFAHR, "Gefahr"), (FEHLER, "Fehler"),
                          (WARNUNG, "Warnungen"), (0, "Hinweise")):
        n = sum(1 for f in bericht.findings if f.severity == schwere)
        if n:
            zahlen.append(f"{n} {wort}")
    return (f"{tool.path or 'physische Diskette'} · {tool.filesystem} · {tiefe}{wo}\n"
            + ("   ".join(zahlen) if zahlen else "ohne Befund"))


def einzelheiten(befund) -> str:
    """Der ausgewählte Befund in voller Länge — Ort, Kennung, jeder Vorschlag."""
    zeilen = [f"{befund.schwere} · {befund.ebene} · {befund.object}", "", befund.text]
    if befund.ortbar:
        ort = f"Zylinder {befund.cyl}, Kopf {befund.head}"
        if befund.sector >= 0:
            ort += f", Sektor {befund.sector}"
        zeilen += ["", ort]
    for r in befund.repairs:
        marken = [w for w, ja in (("empfohlen", r.recommended),
                                  ("Datenverlust", r.destructive),
                                  ("geraten", r.guessed)) if ja]
        zeilen.append("")
        zeilen.append("↳ " + r.text + (f"   [{', '.join(marken)}]" if marken else ""))
        if r.blocked:
            zeilen.append("   GESPERRT: " + r.blocked_why)
    zeilen += ["", f"Kennung: {befund.id}"]
    return "\n".join(zeilen)


class FsckDialog(QDialog):
    """Befundliste mit Auswahl, Einzelheiten und Ausführung.

    :param tool:      die offene Diskette (:class:`K1520Disk`)
    :param zeige_ort: Rückruf ``(cyl, head, sector) -> None`` für den Sprung in den
                      Diskeditor; ``None`` blendet den Knopf aus.
    :param log:       Rückruf ``(text) -> None`` für das Protokoll des Fensters.
    """

    def __init__(self, tool, parent=None, zeige_ort=None, log=None):
        super().__init__(parent)
        self.setWindowTitle("Dateisystem prüfen und reparieren")
        self.resize(860, 620)
        self.tool = tool
        self._zeige_ort = zeige_ort
        self._log = log or (lambda text: None)
        #: Wurde geprüft oder repariert?  Dann muss das Fenster dahinter auffrischen.
        self.geprueft = False
        #: Zahl der ausgeführten Reparaturen über die ganze Sitzung dieses Dialogs.
        self.repariert = 0
        #: Je Baumeintrag der Wähler, wenn es mehr als einen gangbaren Vorschlag gibt.
        self._waehler = {}

        self.kopf = QLabel("")
        self.kopf.setTextInteractionFlags(Qt.TextSelectableByMouse)

        self.baum = QTreeWidget()
        self.baum.setColumnCount(3)
        self.baum.setHeaderLabels(["Befund", "Ort", "Reparatur"])
        self.baum.setRootIsDecorated(False)
        self.baum.setSelectionMode(QAbstractItemView.SingleSelection)
        self.baum.setAlternatingRowColors(True)
        self.baum.currentItemChanged.connect(lambda *_: self._einzelheiten_zeigen())
        self.baum.itemChanged.connect(lambda *_: self._knoepfe_nachziehen())
        # Doppelklick auf einen ortbaren Befund = Diskeditor (E9).
        self.baum.itemDoubleClicked.connect(lambda *_: self._springen())

        self.details = QTextBrowser()
        self.details.setLineWrapMode(QTextBrowser.NoWrap)

        teiler = QSplitter(Qt.Vertical)
        teiler.addWidget(self.baum)
        unten = QWidget()
        unten_lay = QVBoxLayout(unten)
        unten_lay.setContentsMargins(0, 0, 0, 0)
        unten_lay.addWidget(self.details, 1)
        self.knopf_editor = QDialogButtonBox()
        self._b_editor = self.knopf_editor.addButton("Im &Diskeditor zeigen",
                                                     QDialogButtonBox.ActionRole)
        self._b_editor.clicked.connect(self._springen)
        if zeige_ort is None:
            self._b_editor.hide()
        unten_lay.addWidget(self.knopf_editor)
        teiler.addWidget(unten)
        teiler.setStretchFactor(0, 3)
        teiler.setStretchFactor(1, 2)

        #: Warum gerade nicht repariert werden kann — steht ÜBER den Knöpfen, nicht
        #: in einem Tooltip: ein gesperrter Knopf ohne Begründung ist eine Sackgasse.
        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Close)
        self.b_voll = self.knoepfe.addButton("&Vollprüfung", QDialogButtonBox.ActionRole)
        self.b_voll.setToolTip(
            "Jede Spur ansehen: Ketten, Kreuzbelegungen, Belegungsplan und Prüfsummen.")
        self.b_voll.clicked.connect(self._vollpruefung)
        # An einer PHYSISCHEN Diskette zöge die Vollprüfung die ganze Scheibe ein
        # (0,5–0,8 s je Spur) und liesse das Fenster ein bis zwei Minuten stehen.
        # Das gehört in einen Arbeitsfaden mit Fortschritt; bis es den gibt, ist der
        # Knopf gesperrt und sagt warum — dieselbe Regel wie in den Diskettenangaben.
        if not tool.path:
            self.b_voll.setEnabled(False)
            self.b_voll.setToolTip(
                "An einer echten Diskette müsste dafür jede Spur einzeln gelesen "
                "werden (ein bis zwei Minuten) — das kommt mit einem Arbeitsfaden "
                "mit Fortschrittsanzeige.")
        self.b_reparieren = self.knoepfe.addButton("&Ausgewählte reparieren",
                                                   QDialogButtonBox.ApplyRole)
        self.b_reparieren.clicked.connect(self._reparieren)
        self.knoepfe.rejected.connect(self.reject)

        lay = QVBoxLayout(self)
        lay.addWidget(self.kopf)
        lay.addWidget(teiler, 1)
        lay.addWidget(self.hinweis)
        lay.addWidget(self.knoepfe)

        self._fuellen()

    # ── Aufbau ──────────────────────────────────────────────────────────────

    def _fuellen(self) -> None:
        """Die Liste aus dem zuletzt erhobenen Befund neu aufbauen."""
        self.baum.clear()
        self._waehler.clear()
        self.bericht = self.tool.findings()
        self.kopf.setText(kopfzeile(self.tool, self.bericht))

        for i, f in enumerate(self.bericht.findings):
            eintrag = QTreeWidgetItem(self.baum)
            eintrag.setData(0, ROLLE_BEFUND, i)
            eintrag.setText(0, f"{ZEICHEN.get(f.severity, 'ℹ')} {f.object}  {f.text}")
            eintrag.setText(1, f"c{f.cyl}h{f.head}" if f.ortbar else "")
            eintrag.setToolTip(0, f"{f.schwere} · {f.ebene} · {f.id}")

            gangbar = [(j, r) for j, r in enumerate(f.repairs) if not r.blocked]
            gesperrt = [r for r in f.repairs if r.blocked]
            if not gangbar:
                # Kein Ankreuzfeld: eine Auskunft ist kein Versäumnis.  Der Flag
                # muss AUSDRÜCKLICH weg — ``QTreeWidgetItem`` bringt ihn mit.
                eintrag.setFlags(eintrag.flags() & ~Qt.ItemIsUserCheckable)
                eintrag.setText(2, ("GESPERRT: " + gesperrt[0].blocked_why)
                                if gesperrt else "")
                if gesperrt:
                    eintrag.setToolTip(2, gesperrt[0].text)
                continue

            eintrag.setFlags(eintrag.flags() | Qt.ItemIsUserCheckable)
            vorwahl = any(r.vorauswaehlen for _, r in gangbar)
            eintrag.setCheckState(0, Qt.Checked if vorwahl else Qt.Unchecked)
            if len(gangbar) == 1:
                j, r = gangbar[0]
                eintrag.setText(2, self._beschriftung(r))
                eintrag.setToolTip(2, r.text)
                eintrag.setData(2, ROLLE_BEFUND, j)
            else:
                # Mehrere Wege: der Bediener wählt EINEN, vorgewählt ist der
                # empfohlene ohne Datenverlust.
                wahl = QComboBox()
                for j, r in gangbar:
                    wahl.addItem(self._beschriftung(r), j)
                vorne = next((k for k, (_, r) in enumerate(gangbar)
                              if r.vorauswaehlen), 0)
                wahl.setCurrentIndex(vorne)
                self.baum.setItemWidget(eintrag, 2, wahl)
                self._waehler[id(eintrag)] = wahl

        for spalte in range(3):
            self.baum.resizeColumnToContents(spalte)
        if self.bericht.findings:
            self.baum.setCurrentItem(self.baum.topLevelItem(0))
        self._einzelheiten_zeigen()
        self._knoepfe_nachziehen()

    @staticmethod
    def _beschriftung(r) -> str:
        marken = [w for w, ja in (("empfohlen", r.recommended),
                                  ("Datenverlust", r.destructive),
                                  ("geraten", r.guessed)) if ja]
        return r.text + (f"   [{', '.join(marken)}]" if marken else "")

    # ── Zustand ─────────────────────────────────────────────────────────────

    def _aktueller_befund(self):
        eintrag = self.baum.currentItem()
        if eintrag is None:
            return None
        return self.bericht.findings[eintrag.data(0, ROLLE_BEFUND)]

    def auswahl(self) -> List[Tuple[int, int]]:
        """Die angekreuzten Paare ``(Befundnummer, Vorschlagsnummer)``."""
        paare = []
        for k in range(self.baum.topLevelItemCount()):
            eintrag = self.baum.topLevelItem(k)
            if eintrag.checkState(0) != Qt.Checked:
                continue
            i = eintrag.data(0, ROLLE_BEFUND)
            wahl = self._waehler.get(id(eintrag))
            j = wahl.currentData() if wahl is not None else eintrag.data(2, ROLLE_BEFUND)
            if j is not None:
                paare.append((i, j))
        return paare

    def _einzelheiten_zeigen(self) -> None:
        befund = self._aktueller_befund()
        self.details.setPlainText(einzelheiten(befund) if befund else "")
        self._b_editor.setEnabled(bool(befund and befund.ortbar))

    def _knoepfe_nachziehen(self) -> None:
        gewaehlt = self.auswahl()
        schreibgeschuetzt = bool(self.tool.read_only)
        self.b_reparieren.setEnabled(bool(gewaehlt) and not schreibgeschuetzt)
        if schreibgeschuetzt:
            self.hinweis.setText(
                "🔒 Die Diskette ist schreibgeschützt — Reparieren ist nicht möglich. "
                "Der Schreibschutz lässt sich im Hauptfenster aufheben.")
        elif not gewaehlt:
            self.hinweis.setText("")
        else:
            mit_verlust = sum(1 for i, j in gewaehlt
                              if self.bericht.findings[i].repairs[j].destructive)
            self.hinweis.setText(
                f"{len(gewaehlt)} Reparatur(en) ausgewählt"
                + (f", davon {mit_verlust} mit Datenverlust." if mit_verlust else "."))

    # ── Handlungen ──────────────────────────────────────────────────────────

    def _springen(self) -> None:
        befund = self._aktueller_befund()
        if befund is None or not befund.ortbar or self._zeige_ort is None:
            return
        self._zeige_ort(befund.cyl, befund.head, befund.sector)

    def _vollpruefung(self) -> None:
        QGuiApplication.setOverrideCursor(Qt.WaitCursor)
        try:
            self.tool.check(voll=True)
        finally:
            QGuiApplication.restoreOverrideCursor()
        self.geprueft = True
        self._fuellen()

    def _reparieren(self) -> None:
        gewaehlt = self.auswahl()
        if not gewaehlt:
            return
        mit_verlust = sum(1 for i, j in gewaehlt
                          if self.bericht.findings[i].repairs[j].destructive)

        frage = f"{len(gewaehlt)} Reparatur(en) ausführen"
        frage += (f", davon {mit_verlust} mit Datenverlust?" if mit_verlust else "?")
        if self.tool.path:
            frage += f"\n\nSicherung: „{self.tool.path}~\" wird beim Schreiben angelegt."
        else:
            # Eine echte Diskette hat kein `~` — hier gibt es keinen zweiten Versuch.
            frage += ("\n\nDies ist eine ECHTE Diskette: es gibt keine "
                      "Sicherungsdatei.  Vorher „Speichern unter…\" wäre die "
                      "einzige Umkehr.")
        if QMessageBox.question(self, "Reparieren", frage,
                                QMessageBox.Yes | QMessageBox.No,
                                QMessageBox.No) != QMessageBox.Yes:
            return

        vorher = self.bericht
        try:
            getan = self.tool.apply_repairs(gewaehlt)
        except Exception as fehler:                      # K1520DiskError
            # Die Bibliothek hat zurückgerollt — die Diskette ist unverändert.
            QMessageBox.critical(self, "Reparatur fehlgeschlagen",
                                 f"{fehler}\n\nDie Diskette ist unverändert.")
            self._log(f"Reparatur fehlgeschlagen: {fehler}")
            return

        self.geprueft = True
        self.repariert += getan
        self._fuellen()                    # apply_repairs hat schon neu geprüft
        nachher = self.bericht

        def zaehle(bericht, schwere):
            return sum(1 for f in bericht.findings if f.severity >= schwere)

        bilanz = (f"{getan} Reparatur(en) ausgeführt — vorher "
                  f"{len(vorher.findings)} Befunde ({zaehle(vorher, GEFAHR)} Gefahr), "
                  f"nachher {len(nachher.findings)} Befunde "
                  f"({zaehle(nachher, GEFAHR)} Gefahr)")
        self._log(bilanz)
        for i, j in gewaehlt:
            self._log("  " + vorher.findings[i].repairs[j].id + ": "
                      + vorher.findings[i].repairs[j].text)
        QMessageBox.information(self, "Reparatur", bilanz + ".")
