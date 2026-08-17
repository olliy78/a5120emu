"""Hauptfenster des k1520DiskTool.

Zwei-Fenster-Ansicht: links die Diskette, rechts ein Linux-Ordner, dazwischen die
Übertragung in beide Richtungen (doc/design/13_k1520disktool.md §11.2).

Seit dem Umbau von 2026-08-14 (§20) ist es ein **gewöhnliches Anwendungsfenster**:
Menüleiste, ausblendbare Symbolleiste, Kopfbereich, Meldungsstreifen, Statuszeile,
Protokoll als Dock.  Die früheren zwei Knopfleisten auf halber Höhe sind
verschwunden — jede Aktion gibt es als :class:`QAction` genau einmal
(``app/disktool/ui/actions.py``) und damit überall gleich.

Die vier zugesagten Verhaltensregeln (§1) sind unverändert hier verdrahtet:

* **Eine beidseitige UDOS-Diskette ist EIN Datenträger** — beide Seiten in einer
  Liste, `Side0/`/`Side1/` beim Extrahieren und Einfügen.
* **Die Ansicht ist immer frisch** — jede schreibende Aktion endet mit
  :meth:`MainWindow._reload`, das das Verzeichnis neu aus dem Medium liest.
* **Passt es nicht, wird gar nicht erst geschrieben** — vor jedem Ordner-Einfügen
  läuft ``check_fit``; die Bibliothek nimmt eine gescheiterte Stapeloperation
  ohnehin zurück.
* **Ohne Erkennung kein Schreiben** — schlägt das Öffnen fehl, bleibt die Liste
  leer, die Meldung steht im Streifen und die Aktionen sind gesperrt.

Alle Aktionen sind als Methoden ohne Dialog erreichbar (``open_image``,
``create_disk``, ``extract_all``, …); die Dialoge sitzen nur in den
Klick-Behandlern.  Damit ist das Fenster headless prüfbar.

**Meldungen (§20.4)** haben je Sorte genau einen Ort: Titel = Identität +
Änderungsmarke (``setWindowModified``), Kopfbereich = dauerhafte Eigenschaften,
Streifen = dauerhafte Einschränkungen, Statuszeile links = letzte Aktion,
Statuszeile rechts = Zustand als Widget, Protokoll = alles, Meldungsfenster nur
bei Abbruch oder Rückfrage.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import QCoreApplication, QSettings, Qt, QTimer
from PySide6.QtGui import QAction, QActionGroup, QKeySequence
from PySide6.QtWidgets import (
    QFileDialog, QFrame, QInputDialog, QLabel, QMainWindow, QMessageBox,
    QSplitter, QToolBar, QToolButton, QVBoxLayout, QWidget,
)

from app import paths
from app.disktool.archive import create_archive

from app.core_binding.k1520disk import DiskTool, K1520DiskError, filesystems
from app.disktool.ui.actions import erzeuge_aktionen
from app.disktool.ui.disk_editor import DiskEditorWindow
from app.disktool.ui.disk_header import DiskHeader, auswahlliste
from app.disktool.ui.disk_info_dialog import DiskInfoDialog
from app.disktool.ui.disk_view import DiskView
from app.disktool.ui.folder_view import FolderView
from app.disktool.ui.help_window import HelpWindow
from app.disktool.ui.icons import icon
from app.disktool.ui.info_bar import InfoBar
from app.disktool.ui.log_dock import LogDock
from app.disktool.ui.properties_dialog import PropertiesDialog
from app.ui.physical_disk import mit_fortschritt

#: Endungen, die im Textmodus vorgeschlagen werden.
TEXT_ENDUNGEN = {".txt", ".text", ".asm", ".mac", ".doc", ".md", ".log", ".dat"}

#: So lange steht eine Rückmeldung in der Statuszeile (ms) — sie ist flüchtig.
STATUS_DAUER = 8000

#: Höchstzahl der Einträge unter „Zuletzt geöffnet".
ZULETZT_MAX = 8

#: Anstrich der Statuszeile, solange auf eine echte Diskette geschrieben wird.
#: Rot heisst hier **nicht** „Fehler", sondern „Finger weg": die Diskette darf
#: nicht aus dem Laufwerk, bis es fertig ist.  Es ist derselbe Rotton wie im
#: Meldungsstreifen — zwei verschiedene Rots wären schlechter als eine Doppelrolle.
SCHREIBT_STIL = ("background: #c0504d; color: white; font-weight: bold;"
                 " padding: 1px 6px; border-radius: 3px;")



class MainWindow(QMainWindow):
    """Fenster des Diskettenwerkzeugs."""

    #: Laufende Sitzung an einem echten Laufwerk (None = Abbilddatei/leer).
    _physisch = None
    #: Welches Laufwerk diese Sitzung bedient (für Kopfzeile und Fenstertitel).
    _physisch_laufwerk = "a"
    #: Womit das echte Laufwerk zuletzt geöffnet wurde (Laufwerk, Geometrie, …).
    #: Gemerkt, weil ein Sync-Handle nach dem Öffnen VERBRAUCHT ist: wer das
    #: Dateisystem übersteuern will, braucht eine neue Sitzung mit denselben Angaben.
    _physisch_wahl = None
    #: Laufender Schreibvorgang auf eine echte Diskette (None = keiner).  Er ist
    #: UNABHAENGIG von der geöffneten Diskette: die ist nur die Quelle.
    _schreib_sitzung = None
    _schreib_gesamt = 0

    def __init__(self, image: Optional[str] = None, folder: Optional[str] = None):
        super().__init__()
        # Das `[*]` ist Qts Platzhalter für die Änderungsmarke; er wird durch
        # `setWindowModified()` gefüllt und trägt auf jeder Plattform das dort
        # übliche Zeichen (§20.4).
        self.setWindowTitle("k1520DiskTool[*]")
        self.setWindowIcon(icon("disk-editor"))
        self.resize(1150, 700)

        self.tool: Optional[DiskTool] = None
        self._diskeditor = None
        self._hilfe = None
        self._zuletzt: List[str] = []

        erzeuge_aktionen(self)
        # Läuft nur, solange ein echtes Laufwerk offen ist (siehe _physisch_tick).
        self._physisch_uhr = QTimer(self)
        self._physisch_uhr.setInterval(500)
        self._physisch_uhr.timeout.connect(self._physisch_tick)
        self._baue_mitte()
        self._baue_menue()
        self._baue_leiste()
        self._baue_status()
        self._baue_protokoll()
        self._verdrahten()
        self._zustand_laden()

        self._aktionen_pruefen()
        if folder:
            self.folder_view.set_folder(folder)
        if image:
            self.open_image(image)

    # ════════════════════════════════════════════════════════════════════════
    # Aufbau
    # ════════════════════════════════════════════════════════════════════════

    def _baue_mitte(self) -> None:
        """Kopfbereich, Meldungsstreifen und der Teiler mit beiden Hälften."""
        self.kopf = DiskHeader()
        self.info_bar = InfoBar()

        self.disk_view = DiskView()
        self.folder_view = FolderView()

        # Die Mittelspalte bleibt: räumlich ist „von hier nach dort" eindeutiger
        # als jeder Menüpunkt.  Sie zeigt dieselben Aktionen wie Menü und Leiste.
        # Aussen die Stapel (alles), innen die Auswahl — die Pfeillänge sagt, wie
        # viel wandert:  →→| / →|  ·  |← / |←←
        self.btn_alles_holen = QToolButton()
        self.btn_alles_holen.setDefaultAction(self.act_alles_raus)
        self.btn_holen = QToolButton()
        self.btn_holen.setDefaultAction(self.act_holen)
        self.btn_schreiben = QToolButton()
        self.btn_schreiben.setDefaultAction(self.act_schreiben)
        self.btn_alles_schreiben = QToolButton()
        self.btn_alles_schreiben.setDefaultAction(self.act_alles_rein)
        knoepfe = (self.btn_alles_holen, self.btn_holen,
                   self.btn_schreiben, self.btn_alles_schreiben)
        for b in knoepfe:
            b.setToolButtonStyle(Qt.ToolButtonIconOnly)
            b.setAutoRaise(True)
            b.setIconSize(self.btn_holen.iconSize() * 1.2)

        mitte = QVBoxLayout()
        mitte.setContentsMargins(2, 0, 2, 0)
        mitte.setSpacing(2)
        mitte.addStretch(1)
        for b in knoepfe:
            mitte.addWidget(b)
        mitte.addStretch(1)
        mitte_w = QWidget()
        mitte_w.setLayout(mitte)

        self.teiler = QSplitter(Qt.Horizontal)
        self.teiler.addWidget(self.disk_view)
        self.teiler.addWidget(mitte_w)
        self.teiler.addWidget(self.folder_view)
        # Beide Hälften gleich gross: keine der beiden Listen ist die wichtigere,
        # und ungleiche Spalten lesen sich wie eine Aussage darüber.
        self.teiler.setStretchFactor(0, 1)
        self.teiler.setStretchFactor(1, 0)
        self.teiler.setStretchFactor(2, 1)
        self.teiler.setChildrenCollapsible(False)
        self.teiler.setSizes([500, 44, 500])

        trenner = QFrame()
        trenner.setFrameShape(QFrame.HLine)
        trenner.setFrameShadow(QFrame.Sunken)

        lay = QVBoxLayout()
        lay.setContentsMargins(6, 4, 6, 4)
        lay.setSpacing(4)
        lay.addWidget(self.kopf)
        lay.addWidget(trenner)
        lay.addWidget(self.info_bar)
        lay.addWidget(self.teiler, 1)

        zentral = QWidget()
        zentral.setLayout(lay)
        self.setCentralWidget(zentral)

    def _baue_menue(self) -> None:
        leiste = self.menuBar()

        m = leiste.addMenu("&Datei")
        m.addAction(self.act_oeffnen)
        self.menue_zuletzt = m.addMenu("&Zuletzt geöffnet")
        m.addAction(self.act_neu)
        m.addSeparator()
        m.addAction(self.act_speichern)
        m.addAction(self.act_speichern_unter)
        m.addAction(self.act_archivieren)
        m.addSeparator()
        m.addAction(self.act_schliessen)
        m.addAction(self.act_beenden)

        m = leiste.addMenu("&Bearbeiten")
        m.addAction(self.act_alles_waehlen)
        m.addSeparator()
        m.addAction(self.act_holen)
        m.addAction(self.act_schreiben)
        m.addAction(self.act_loeschen)
        m.addSeparator()
        m.addAction(self.act_eigenschaften)

        m = leiste.addMenu("Dis&kette")
        # Beide Richtungen der echten Diskette zuerst und beieinander: laden und
        # überschreiben sind derselbe Weg, einmal herein und einmal hinaus.
        m.addAction(self.act_physisch)
        m.addAction(self.act_physisch_schreiben)
        m.addSeparator()
        m.addAction(self.act_schreibschutz)
        self.menue_fs = m.addMenu("&Dateisystem übersteuern")
        self._baue_fs_menue()
        m.addSeparator()
        m.addAction(self.act_alles_raus)
        m.addAction(self.act_alles_rein)
        m.addSeparator()
        # Eigenes Untermenü: das sind Eingriffe ins ABBILD, nicht in die Diskette —
        # der Unterschied gehört sichtbar gemacht.
        self.menue_schnitt = m.addMenu("Speicherabbild &ändern")
        self.menue_schnitt.addAction(self.act_gerade_spuren)
        self.menue_schnitt.addAction(self.act_seite1_weg)
        m.addSeparator()
        m.addAction(self.act_bootabbild)
        m.addAction(self.act_diskeditor)
        m.addSeparator()
        m.addAction(self.act_neu_beschreiben)
        m.addAction(self.act_angaben)

        m = leiste.addMenu("&Übertragung")
        m.addAction(self.act_binaer)
        m.addAction(self.act_text)
        m.addSeparator()
        m.addAction(self.act_ordner)

        self.menue_ansicht = leiste.addMenu("&Ansicht")   # gefüllt in _baue_leiste

        m = leiste.addMenu("&Hilfe")
        m.addAction(self.act_hilfe)
        m.addAction(self.act_ueber)

    def _baue_fs_menue(self) -> None:
        """Dieselbe Auswahl wie das Feld im Kopf, als Radiogruppe.

        Beide zeigen denselben Zustand; gesetzt wird er an EINER Stelle
        (:meth:`_fs_setzen`), damit sie nicht auseinanderlaufen können.
        """
        self.gruppe_fs = QActionGroup(self)
        self.gruppe_fs.setExclusive(True)
        self._fs_aktionen = {}
        for name, text in auswahlliste():
            a = QAction(text, self)
            a.setCheckable(True)
            a.setData(name)
            a.triggered.connect(lambda *_, n=name: self._fs_setzen(n, neu_oeffnen=True))
            self.gruppe_fs.addAction(a)
            self.menue_fs.addAction(a)
            self._fs_aktionen[name] = a
        self._fs_aktionen[""].setChecked(True)

    def _baue_leiste(self) -> None:
        """Symbolleiste — ausblendbar über ihre eigene ``toggleViewAction()``."""
        self.leiste = QToolBar("Symbolleiste", self)
        self.leiste.setObjectName("haupt")           # für saveState()
        self.leiste.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
        self.leiste.setMovable(True)
        self.addToolBar(Qt.TopToolBarArea, self.leiste)

        for gruppe in (
            (self.act_oeffnen, self.act_neu),
            (self.act_physisch, self.act_physisch_schreiben),
            (self.act_speichern, self.act_speichern_unter, self.act_archivieren),
            (self.act_diskeditor, self.act_bootabbild),
            (self.act_holen, self.act_schreiben, self.act_loeschen),
            (self.act_schreibschutz,),
        ):
            for a in gruppe:
                self.leiste.addAction(a)
            self.leiste.addSeparator()

        # ── Ansicht-Menü: die Umschalter kommen von Qt selbst ────────────────
        self.act_leiste_zeigen = self.leiste.toggleViewAction()
        self.act_leiste_zeigen.setText("&Symbolleiste")
        self.menue_ansicht.addAction(self.act_leiste_zeigen)
        # Das Protokoll-Dock hängt sich in _baue_protokoll() an derselben Stelle ein;
        # den Platz dafür merken wir uns über die Reihenfolge der Aufrufe.

    def _baue_status(self) -> None:
        """Links die letzte Aktion (flüchtig), rechts der Zustand (dauerhaft)."""
        self.st_inhalt = QLabel("")
        # Füllstand des echten Laufwerks — eigenes Feld, weil es sich LAUFEND ändert,
        # während `st_inhalt` nur nach einer Aktion neu gesetzt wird.
        self.st_physisch = QLabel("")
        self.st_physisch.hide()
        self.st_modus = QLabel("binär")
        self.st_modus.setToolTip("Übertragungsart — Menü „Übertragung\"")
        # Schloss als Bild, nicht als Emoji: 🔒 und 🔓 sehen in vielen Schriften
        # gleich aus, und dann sagt die Anzeige nichts.
        self.st_schloss = QLabel()
        self.st_schloss.setFixedWidth(18)
        self.st_schutz = QLabel("")

        leiste = self.statusBar()
        leiste.setSizeGripEnabled(True)
        for i, w in enumerate((self.st_inhalt, self.st_physisch,
                               self.st_modus, self.st_schloss)):
            if i:                              # Trennstrich zwischen den Feldern
                strich = QFrame()
                strich.setFrameShape(QFrame.VLine)
                strich.setFrameShadow(QFrame.Sunken)
                leiste.addPermanentWidget(strich)
            w.setFrameShape(QFrame.NoFrame)
            w.setMargin(2)
            leiste.addPermanentWidget(w)
        # Schloss und Wort gehören zusammen — zwischen sie kommt kein Trennstrich.
        self.st_schutz.setMargin(2)
        leiste.addPermanentWidget(self.st_schutz)
        leiste.showMessage("Bereit")

    def _baue_protokoll(self) -> None:
        self.log_dock = LogDock(self)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.log_dock)
        self.log_dock.hide()                         # Historie, kein Dauerplatz
        # Ohne Vorgabe nimmt sich das Dock beim Aufklappen die halbe Fensterhöhe.
        self.resizeDocks([self.log_dock], [170], Qt.Vertical)

        self.act_protokoll_zeigen = self.log_dock.toggleViewAction()
        self.act_protokoll_zeigen.setText("&Protokoll")
        self.act_protokoll_zeigen.setShortcut(QKeySequence("F8"))
        self.menue_ansicht.addAction(self.act_protokoll_zeigen)

        # Die Statuszeile ist NICHT abschaltbar: sie trägt den Zustand der
        # geöffneten Diskette (Schreibschutz, freier Platz, Übertragungsart).
        # Wer sie ausblenden könnte, arbeitete blind (§20.4).
        self.menue_ansicht.addSeparator()
        stil_menue = self.menue_ansicht.addMenu("Symbolleisten&stil")
        self.gruppe_stil = QActionGroup(self)
        self.gruppe_stil.setExclusive(True)
        for text, stil in (("Nur Symbole", Qt.ToolButtonIconOnly),
                           ("Symbole und Text", Qt.ToolButtonTextUnderIcon),
                           ("Text neben dem Symbol", Qt.ToolButtonTextBesideIcon),
                           ("Nur Text", Qt.ToolButtonTextOnly)):
            a = QAction(text, self)
            a.setCheckable(True)
            a.setData(int(stil.value))
            a.triggered.connect(lambda *_, s=stil: self._leistenstil(s))
            self.gruppe_stil.addAction(a)
            stil_menue.addAction(a)
            if stil == Qt.ToolButtonTextUnderIcon:
                a.setChecked(True)

        self.menue_ansicht.addSeparator()
        self.menue_ansicht.addAction(self.act_aktualisieren)

    def _verdrahten(self) -> None:
        self.kopf.filesystem_gewaehlt.connect(
            lambda name: self._fs_setzen(name, neu_oeffnen=True))
        self.folder_view.choose_requested.connect(self._ordner_dialog)
        self.folder_view.disk_files_dropped.connect(self._extrahieren_refs)
        self.disk_view.files_dropped.connect(self._einfuegen_pfade)
        self.disk_view.properties_requested.connect(self._eigenschaften_ref)
        self.disk_view.extract_requested.connect(self._extrahieren_refs)
        self.disk_view.erase_requested.connect(self._loeschen_refs)
        self.disk_view.tree.itemSelectionChanged.connect(self._aktionen_pruefen)
        self.folder_view.tree.itemSelectionChanged.connect(self._aktionen_pruefen)

        # Beide Listen zeigen dieselben Aktionen im Kontextmenü.
        self.disk_view.setze_aktionen(self.act_eigenschaften, self.act_holen,
                                      self.act_loeschen)
        self.folder_view.setze_aktionen(self.act_schreiben, self.act_ordner)

        self.gruppe_modus.triggered.connect(lambda *_: self._modus_geaendert())

    # ── Fensterzustand (nur für den echten Programmlauf) ─────────────────────

    def _einstellungen(self) -> Optional[QSettings]:
        """``QSettings`` — nur, wenn die Anwendung sich benannt hat.

        ``app/disktool/main.py`` setzt Organisation und Name; die Testläufe legen
        eine namenlose ``QApplication`` an.  So schreibt kein Test in die
        Einstellungen des Anwenders und kein Test erbt dessen ausgeblendete
        Symbolleiste.
        """
        if not QCoreApplication.organizationName():
            return None
        return QSettings()

    @staticmethod
    def _als_liste(wert) -> List[str]:
        """Eine Zeichenkettenliste aus ``QSettings`` — auch wenn sie eine wurde.

        Qt speichert eine Liste als ``QStringList``, gibt sie beim Lesen aber als
        **einzelne Zeichenkette** zurück, sobald sie nur ein Element hat.  Ohne
        diese Behandlung zerfiel der eine zuletzt geöffnete Pfad in seine
        Buchstaben und das Menü hatte 80 Einträge.
        """
        if wert is None:
            return []
        if isinstance(wert, str):
            return [wert] if wert else []
        return [str(x) for x in wert if x]

    def _zustand_laden(self) -> None:
        s = self._einstellungen()
        if s is None:
            return
        geo = s.value("fenster/geometrie")
        if geo:
            self.restoreGeometry(geo)
        zustand = s.value("fenster/zustand")
        if zustand:
            self.restoreState(zustand)
        stil = s.value("fenster/leistenstil")
        if stil is not None:
            self._leistenstil(Qt.ToolButtonStyle(int(stil)), merken=False)
            for a in self.gruppe_stil.actions():
                a.setChecked(a.data() == int(stil))
        if s.value("uebertragung/text", "0") == "1":
            self.act_text.setChecked(True)
            self._modus_geaendert()
        self._zuletzt = self._als_liste(s.value("zuletzt", []))
        self._menue_zuletzt_bauen()
        # `restoreState()` kann die Statuszeile verborgen haben (ältere Fassung
        # konnte das) — sie gehört unbedingt sichtbar.
        self.statusBar().show()

    def _zustand_sichern(self) -> None:
        s = self._einstellungen()
        if s is None:
            return
        s.setValue("fenster/geometrie", self.saveGeometry())
        s.setValue("fenster/zustand", self.saveState())
        s.setValue("fenster/leistenstil", int(self.leiste.toolButtonStyle().value))
        s.setValue("uebertragung/text", "1" if self.text_mode else "0")
        s.setValue("zuletzt", self._zuletzt)

    def _leistenstil(self, stil, merken: bool = True) -> None:
        self.leiste.setToolButtonStyle(stil)

    # ── Zuletzt geöffnet ────────────────────────────────────────────────────

    def _zuletzt_merken(self, pfad: str) -> None:
        pfad = str(Path(pfad).resolve())
        if pfad in self._zuletzt:
            self._zuletzt.remove(pfad)
        self._zuletzt.insert(0, pfad)
        del self._zuletzt[ZULETZT_MAX:]
        self._menue_zuletzt_bauen()

    def _menue_zuletzt_bauen(self) -> None:
        self.menue_zuletzt.clear()
        for pfad in self._zuletzt:
            a = QAction(Path(pfad).name, self)
            a.setStatusTip(pfad)
            a.triggered.connect(lambda *_, p=pfad: self.open_image(p))
            self.menue_zuletzt.addAction(a)
        self.menue_zuletzt.setEnabled(bool(self._zuletzt))

    # ════════════════════════════════════════════════════════════════════════
    # Zustand
    # ════════════════════════════════════════════════════════════════════════

    @property
    def text_mode(self) -> bool:
        return self.act_text.isChecked()

    @property
    def protokoll(self):
        """Das Textfeld des Protokoll-Docks (die Historie)."""
        return self.log_dock.text

    def log(self, text: str, *, stufe: str = "") -> None:
        """Rückmeldung ausgeben: Statuszeile (flüchtig) + Protokoll (Historie).

        ``stufe`` (``hinweis``/``warnung``/``fehler``) hebt sie zusätzlich in den
        Meldungsstreifen — für alles, was nicht in acht Sekunden erledigt ist.
        """
        self.log_dock.append(text)
        erste = text.splitlines()[0] if text else ""
        self.statusBar().showMessage(erste, STATUS_DAUER)
        if stufe:
            self.info_bar.zeige(text, stufe)

    def _fehler(self, titel: str, text: str) -> None:
        """Abbruch: Meldungsfenster **und** Streifen **und** Protokoll."""
        self.log_dock.append(f"{titel}: {text}")
        self.statusBar().showMessage(f"{titel}: {text.splitlines()[0]}", STATUS_DAUER)
        self.info_bar.zeige(f"{titel}: {text}", "fehler")
        QMessageBox.critical(self, titel, text)

    def _aktionen_pruefen(self) -> None:
        """Der einzige Ort, an dem Aktionen gesperrt und freigegeben werden.

        Drei Stufen: **offen** (etwas Erkanntes liegt vor) gibt das Lesen frei;
        **schreibbar** zusätzlich, wenn der Schreibschutz fort ist; und die
        auswahlabhängigen Aktionen zusätzlich, wenn in der zuständigen Liste
        wirklich etwas ausgewählt ist — dann gibt es den früheren Hinweis
        „Keine Datei ausgewählt" gar nicht mehr (§20.3).
        """
        offen = self.tool is not None
        # Roh geöffnet: es GIBT eine Diskette (Medium, Spuren, Sektoren), nur kein
        # Dateisystem.  Alles, was am Abbild arbeitet, bleibt darum bedienbar —
        # gesperrt wird nur, was Dateien braucht.
        mit_fs = offen and self.tool.has_filesystem
        schreibbar = offen and not self.tool.read_only
        disk_auswahl = bool(self.disk_view.selected_refs())
        ordner_auswahl = bool(self.folder_view.selected_paths())

        for a in (self.act_speichern_unter, self.act_diskeditor, self.act_angaben,
                  self.act_schliessen, self.act_aktualisieren):
            a.setEnabled(offen)
        for a in (self.act_archivieren, self.act_alles_raus):
            a.setEnabled(mit_fs)            # braucht Dateien
        self.act_speichern.setEnabled(schreibbar)
        self.act_alles_rein.setEnabled(schreibbar and mit_fs)

        self.act_holen.setEnabled(offen and disk_auswahl)
        self.act_eigenschaften.setEnabled(offen and len(
            self.disk_view.selected_refs()) == 1)
        self.act_loeschen.setEnabled(schreibbar and disk_auswahl)
        self.act_schreiben.setEnabled(schreibbar and ordner_auswahl)

        # Systemspuren gibt es nicht überall — eine Datendiskette (cpa800) beginnt
        # auf Zylinder 0 und hat nichts zu sichern.
        self.act_bootabbild.setEnabled(mit_fs and self.tool.boot_area_size(0) > 0)
        self.act_schreibschutz.setEnabled(offen)
        # Auch ohne offene Diskette wählbar, wenn ein physisches Laufwerk im Spiel
        # ist: bei einer nicht erkannten Diskette ist das Übersteuern der einzige Weg
        # hinein, und ein gesperrtes Menü verstellte genau ihn.
        self.menue_fs.setEnabled(offen or bool(self._physisch_wahl))

        # Der Ausweg (§7.2) ist an einer Datei sinnlos — dort gibt es keine
        # Schadstelle, gegen die ein erneutes Wegschreiben helfen würde.  Deshalb
        # ist er nicht bloss gesperrt, sondern gar nicht da.
        self.act_neu_beschreiben.setVisible(self._physisch is not None)
        self.act_neu_beschreiben.setEnabled(schreibbar and self._physisch is not None)

        # Überschreiben braucht eine QUELLE (die offene Diskette), aber keinen
        # aufgehobenen Schreibschutz: der schützt die geöffnete Diskette, und die
        # wird hier nur gelesen.  Was Schutz braucht, ist die Diskette im Laufwerk —
        # dafür steht die Rückfrage.
        # Solange geschrieben wird, ist das Laufwerk belegt — beide Wege dorthin
        # bleiben zu, bis es fertig ist.
        laeuft = self._schreib_sitzung is not None
        self.act_physisch_schreiben.setEnabled(offen and not laeuft)
        # Schneiden geht an JEDEM Abbild — auch an einem roh geöffneten; gerade dort
        # ist es der Weg zur Erkennung.  Nur etwas da sein muss.
        self.act_gerade_spuren.setEnabled(offen and self.tool.medium_cylinders > 1)
        self.act_seite1_weg.setEnabled(offen and self.tool.medium_heads > 1)
        self.menue_schnitt.setEnabled(offen)
        # Laden bleibt sonst IMMER möglich — es ist der Weg, überhaupt eine Diskette
        # zu bekommen; nur während des Schreibens ist das Laufwerk belegt.
        self.act_physisch.setEnabled(not laeuft)

        self._schutz_anzeigen()

    def _schutz_anzeigen(self) -> None:
        """Der Schutzknopf ZEIGT den Zustand, statt ihn nur zu schalten.

        Ein rastender Knopf ist von aussen schwer zu lesen („ist er gedrückt?") —
        deshalb wechseln Symbol **und** Beschriftung mit: geschlossenes Schloss +
        ``R/O`` gegen offenes Schloss + ``R/W``.  Dasselbe Bild trägt der
        Menüpunkt, dieselbe Aussage steht rechts in der Statuszeile.
        """
        schreibbar = self.tool is not None and not self.tool.read_only
        bild = "lock-open" if schreibbar else "lock"
        self.act_schreibschutz.setIcon(icon(bild))
        self.act_schreibschutz.setIconText("R/W" if schreibbar else "R/O")

        if self.tool is None:
            self.st_schloss.clear()
            self.st_schutz.setText("")
        else:
            self.st_schloss.setPixmap(icon(bild).pixmap(16, 16))
            self.st_schutz.setText("R/W  schreibbar" if schreibbar
                                   else "R/O  nur lesen")

    #: Alter Name — das Freigeben hängt nicht mehr an einem Argument.
    def _enable_write(self, offen: bool = True) -> None:
        self._aktionen_pruefen()

    def _reload(self) -> None:
        """Ansicht aus dem Medium neu aufbauen (§9.3 — kein Zwischenspeicher)."""
        if self.tool is None:
            self.disk_view.clear()
            self.kopf.leeren()
            self.st_inhalt.setText("")
            self.setWindowTitle("k1520DiskTool[*]")
            self.setWindowModified(False)
            self._aktionen_pruefen()
            return

        # An einer physischen Diskette zweistufig: erst die Namen (bei UDOS drei
        # Spuren), die Angaben aus den Kopfsektoren trägt `_details_nachtragen`
        # nach, sobald ihre Spuren da sind (§11.2b).  Bei einer Datei ist
        # `list_names()` ohnehin dasselbe wie `list()` — nur bei CP/M immer, bei
        # UDOS deshalb, weil dort jeder Zugriff sofort geht.
        # Ohne Dateisystem gibt es keine Dateiliste — die Diskette ist trotzdem da.
        if not self.tool.has_filesystem:
            eintraege = []
        elif self._physisch is not None:
            eintraege = self.tool.list_names()
        else:
            eintraege = self.tool.list()
        self._eintraege = eintraege
        self.disk_view.set_disk(self.tool, eintraege)
        self.folder_view.refresh()
        self.kopf.setze(self.tool, self._bezeichnung())

        self._inhalt_anzeigen()

        self.setWindowTitle(f"{self._kurzname()}[*] — k1520DiskTool")
        self.setWindowModified(self.tool.dirty)
        self._aktionen_pruefen()

    def _physisch_tick(self) -> None:
        """Füllstand des echten Laufwerks nachführen, solange eine Sitzung läuft.

        Ohne diesen Zeitgeber stand in der Statuszeile der Stand vom letzten
        :meth:`_reload` — beim Vorauslesen wächst er aber weiter, und die Anzeige
        blieb auf einer Spurzahl stehen, die längst überholt war.  Der Emulator
        führt seine Füllstandszeile aus demselben Grund nach (dort am LED-Zeitgeber).

        500 ms genügen: eine Spur dauert 0,5–0,8 s, häufiger gäbe es nichts Neues.
        """
        # Zwei Sitzungsarten, ein Zeitgeber: die geöffnete physische Diskette und ein
        # laufender Schreibvorgang.  Beide brauchen dieselbe Nachführung, und zwei
        # Zeitgeber nebeneinander wären zwei Stellen, die aus dem Tritt geraten können.
        if self._schreib_tick():
            return
        sitzung = self._physisch
        if sitzung is None:
            self._physisch_uhr.stop()
            self.st_physisch.hide()
            return
        self.st_physisch.setStyleSheet("")
        self.st_physisch.setText(sitzung.status_text())
        self.st_physisch.show()
        self._details_nachtragen()
        self._befund_auffrischen()
        self._pruefe_defekte()

    def _schreib_tick(self) -> bool:
        """Einen laufenden Schreibvorgang nachführen.

        Returns:
            True, solange geschrieben wird — dann gehört die Statuszeile ihm.
        """
        sitzung = self._schreib_sitzung
        if sitzung is None:
            return False
        st = sitzung.stats()
        if st is None:                      # Sitzung fort — nichts mehr zu melden
            self._schreib_fertig(None)
            return True

        gesamt = self._schreib_gesamt
        self.st_physisch.setText(
            f"Diskette wird beschrieben: {min(st.verifies_done, gesamt)} von "
            f"{gesamt} Spuren")
        self.st_physisch.setStyleSheet(SCHREIBT_STIL)
        self.st_physisch.show()

        # Fertig ist es, wenn keine Spur mehr aussteht UND gerade nichts läuft.
        # Beides ist nötig: `tracks_dirty` fällt schon vor dem Prüf-Lesen.
        if st.tracks_dirty == 0 and not st.busy:
            self._schreib_fertig(st)
        return True

    def _schreib_fertig(self, st) -> None:
        """Schreibvorgang abschliessen: Sitzung beenden und Bescheid geben."""
        sitzung = self._schreib_sitzung
        self._schreib_sitzung = None
        n = self._schreib_gesamt
        self._schreib_gesamt = 0
        self.st_physisch.clear()
        self.st_physisch.setStyleSheet("")
        self.st_physisch.hide()

        schaden = ""
        if sitzung is not None:
            schaden = sitzung.defect_tracks
            try:
                sitzung.close()
            except Exception:                        # noqa: BLE001
                pass
        if not self._physisch:
            self._physisch_uhr.stop()
        self._aktionen_pruefen()

        if schaden:
            # Kein Meldungsfenster im Rücken des Bedieners — aber auch nichts, was
            # sich wegscrollt: eine Schadstelle ist eine dauerhafte Auskunft.
            self.log(f"SCHREIBFEHLER auf Spur {schaden}", stufe="fehler")
            self.info_bar.zeige(
                f"Die Diskette liess sich auf Spur {schaden} nicht beschreiben — "
                "das Speicherabbild ist unversehrt.  Mit einer anderen Diskette "
                "noch einmal versuchen.", "fehler")
            return
        self.log(f"{n} Spuren geschrieben und fehlerfrei zurückgelesen")
        self.info_bar.zeige(
            f"Die Diskette ist beschrieben: {n} Spuren, fehlerfrei zurückgelesen.",
            "hinweis")

    def _befund_auffrischen(self) -> None:
        """Den Stichproben-Vorbehalt aufheben, sobald die ganze Diskette gelesen ist.

        Die Erkennung urteilt anfangs über acht Spuren (§11.2a); „2 Spuren hinter dem
        Format" heisst dann „2 der 8 angesehenen".  Bliebe dieser Satz stehen, stünde
        an einer längst vollständig gelesenen Diskette dauerhaft ein Vorbehalt, der
        nicht mehr gilt — und die Zahlen wären womöglich zu klein.
        """
        if self.tool is None or self.tool.examined_tracks == 0:
            return          # war keine Stichprobe (oder schon aufgefrischt)
        if self.tool.refresh_detection():
            self._medium_meldungen()
            self.kopf.setze(self.tool, self._bezeichnung())

    def _details_nachtragen(self) -> None:
        """Angaben nachtragen, deren Kopfsektor inzwischen gelesen ist.

        **Nur was ohne Warten zu haben ist** (`entry_details_ready`): der Zeitgeber
        läuft im Oberflächenfaden, ein blockierender Zugriff hielte hier das ganze
        Fenster an — und er würde das Laufwerk antreiben, statt ihm zu folgen.
        Dieselbe Regel wie beim Diskeditor.
        """
        eintraege = getattr(self, "_eintraege", None)
        if not eintraege or self.tool is None:
            return
        offen = False
        for nr, e in enumerate(eintraege):
            if e.details_loaded:
                continue
            if not self.tool.entry_details_ready(nr):
                offen = True
                continue
            eintraege[nr] = self.tool.load_entry_details(nr)
            self.disk_view.eintrag_auffrischen(nr, eintraege[nr])
        if not offen:
            # Alles beisammen — die Statuszeile darf jetzt die echten Zahlen nennen.
            self._inhalt_anzeigen()

    def _inhalt_anzeigen(self) -> None:
        """Dateizahl und freier Platz in die Statuszeile."""
        if self.tool is None:
            return
        if not self.tool.has_filesystem:
            self.st_inhalt.setText("kein Dateisystem — Diskette roh geöffnet")
            return
        teile = [f"{len(getattr(self, '_eintraege', []))} Dateien"]
        mehrseitig = self.tool.volume_count > 1
        for v in self.tool.volumes():
            wo = f"{v.dir}: " if mehrseitig else ""
            teile.append(f"{wo}{v.free // 1024} KB frei")
        self.st_inhalt.setText(" · ".join(teile))

    def _bezeichnung(self) -> str:
        """Woher die offene Diskette kommt — Pfad oder echtes Laufwerk.

        Eine physische Diskette hat keinen Pfad; ihre Herkunft ist trotzdem das
        Erste, was im Kopf stehen muss (sonst bliebe die Zeile leer).
        """
        if self._physisch is None:
            return ""
        art = "schreibend" if self.tool is not None and not self.tool.read_only \
            else "nur lesen"
        return (f"Echtes Laufwerk {self._physisch_laufwerk.upper()} am Greaseweazle"
                f"  ({art})")

    def _kurzname(self) -> str:
        """Name für die Fensterleiste."""
        if self._physisch is not None:
            return f"Laufwerk {self._physisch_laufwerk.upper()}"
        return Path(self.tool.path).name if self.tool is not None else ""

    def _medium_meldungen(self) -> None:
        """Was an dieser Diskette dauerhaft zu beachten ist → Streifen."""
        if self.tool is None:
            return
        teile = []
        if not self.tool.unambiguous and self.tool.alternatives:
            teile.append("Das Dateisystem ist nicht eindeutig erkannt — auch "
                         f"möglich: {', '.join(self.tool.alternatives)}.")
        if self.tool.remarks:
            teile.append("Medium: " + self._kurzgefasst(self.tool.remarks))
        if teile:
            self.info_bar.zeige(
                " ".join(teile), "warnung",
                knopf="Dateisystem wählen…" if not self.tool.unambiguous else None,
                bei_klick=self.menue_fs.exec if not self.tool.unambiguous else None)
        else:
            self.info_bar.verbergen()

    # ════════════════════════════════════════════════════════════════════════
    # Diskette öffnen / anlegen / schließen
    # ════════════════════════════════════════════════════════════════════════

    def open_image(self, path, filesystem: str = "") -> bool:
        """Abbild öffnen.  Scheitert die Erkennung, bleibt alles gesperrt."""
        self._close_tool()
        try:
            self.tool = DiskTool.open(path, filesystem or None)
        except (K1520DiskError, FileNotFoundError) as e:
            self.tool = None
            self.disk_view.clear()
            self.kopf.leeren(str(path))
            self.st_inhalt.setText("")
            self.setWindowTitle("k1520DiskTool[*]")
            self.setWindowModified(False)
            self._aktionen_pruefen()
            self.log_dock.append(f"Nicht geöffnet: {e}")
            self.statusBar().showMessage(f"Nicht geöffnet: {path}", STATUS_DAUER)
            self.info_bar.zeige(f"{path} wurde nicht geöffnet: {e}", "fehler")
            return False

        self._fs_setzen(filesystem, neu_oeffnen=False)
        self.act_schreibschutz.blockSignals(True)
        self.act_schreibschutz.setChecked(self.tool.read_only)
        self.act_schreibschutz.blockSignals(False)
        self._reload()
        self._medium_meldungen()
        self._zuletzt_merken(str(path))
        self.log_dock.append(f"Geöffnet: {path} — {self.tool.filesystem}")
        self.statusBar().showMessage(
            f"Geöffnet: {Path(path).name} — {self.tool.filesystem}", STATUS_DAUER)
        return True

    def open_physical(self, *, filesystem: str = "", **optionen) -> bool:
        """**Echte Diskette** in einem echten Laufwerk am Greaseweazle öffnen.

        Anders als bei einem Abbild gibt es hier keine Datei: die Diskette wird
        **spurweise** gelesen, sobald etwas gebraucht wird.  Das Öffnen selbst ist
        trotzdem teuer — die Formaterkennung sieht sich jede Spur an —, läuft
        deshalb in einem Arbeitsfaden mit Fortschrittsanzeige
        (doc/design/14_physische_diskette.md §11.2).

        Args:
            filesystem: Dateisystem übersteuern (leer = erkennen).  Das einzige
                Argument, das **hierher** gehört.
            optionen: alles Übrige geht **unverändert** an
                :meth:`PhysicalSession.start` — ``drive``, ``num_cyls``,
                ``writable``, ``verify_writes``, …  Bewusst durchgereicht statt
                einzeln aufgezählt: die Auswahl des Dialogs ist genau diese Menge,
                und eine hier nachgepflegte Kopie der Signatur läuft ihr davon
                (``verify_writes`` kam hinzu und fehlte prompt).  Der Emulator
                macht es an derselben Stelle ebenso.

        Returns:
            True, wenn die Diskette offen ist.  Abbruch durch den Bediener gilt
            als „nicht geöffnet", ohne Fehlermeldung.
        """
        from app.ui.physical_disk import PhysicalSession, verfuegbarkeit

        ok, grund = verfuegbarkeit()
        if not ok:
            self._fehler("Physisches Laufwerk", grund)
            return False

        drive = optionen.get("drive", "a")
        writable = bool(optionen.get("writable", False))
        self._close_tool()
        try:
            sitzung = PhysicalSession.start(**optionen)
        except TypeError as e:                       # Signaturdrift, kein Gerätefehler
            self._fehler("Physisches Laufwerk",
                         f"Die Angaben passen nicht zum Laufwerkszugriff:\n{e}")
            return False
        except Exception as e:                       # noqa: BLE001
            self._fehler("Physisches Laufwerk",
                         f"Das Laufwerk liess sich nicht oeffnen:\n{e}")
            return False
        self._physisch = sitzung
        self._physisch_laufwerk = drive
        self._physisch_wahl = dict(optionen)
        return self._physisch_weiter(sitzung, filesystem, writable)

    def _physisch_weiter(self, sitzung, filesystem: str, writable: bool) -> bool:
        """Erkennen, Verzeichnis holen, anzeigen — der Teil NACH der Sitzung.

        Eigene Methode, weil es zwei Wege hierher gibt: das erste Öffnen und das
        Übersteuern des Dateisystems (:meth:`_physisch_erneut`).  Zwei Kopien
        liefen unweigerlich auseinander.
        """
        drive = self._physisch_laufwerk

        def oeffnen_und_verzeichnis():
            """Öffnen UND das Verzeichnis holen — beides im Arbeitsfaden.

            Auch das Verzeichnis gehört hierher, nicht in den Oberflächenfaden: liefe
            es dort, verarbeitete Qt so lange keine Ereignisse — die Dateiliste bliebe
            leer, der Fortschritt stünde, und ein Klick würde erst danach abgearbeitet
            (das Programm sähe eingefroren aus).

            Geholt werden nur die **Namen** (`list_names`).  Die übrigen Angaben
            stehen bei UDOS im Kopfsektor jeder Datei, verstreut über die Diskette;
            sie einzeln nachzutragen ist Sache von `_details_nachtragen`, sobald ihre
            Spuren ohnehin gelesen sind.  Bei CP/M ist das ein und dasselbe.
            """
            # ROH öffnen: wird nichts erkannt, kommt die Diskette trotzdem heraus —
            # ohne Dateisystem, aber mit Medium.  Sie im Sektoreditor ansehen, ihr
            # Abbild sichern oder sie zurechtschneiden geht dann weiter (§12.6).
            werkzeug = DiskTool.open_physical_raw(sitzung, filesystem or None,
                                                  read_only=not writable)
            if werkzeug.has_filesystem:
                werkzeug.list_names()
            return werkzeug

        werkzeug, fehler = mit_fortschritt(
            self, sitzung, oeffnen_und_verzeichnis,
            text="Format wird erkannt…",
            # Ziel sind die Sondenspuren, nicht die ganze Diskette (§11.2a).
            ziel=DiskTool.probe_track_count(sitzung.num_cyls, sitzung.num_heads),
            was="Spuren für die Formaterkennung")

        if werkzeug is None:
            self._close_physisch()
            self.disk_view.clear()
            self.kopf.leeren(f"Echtes Laufwerk {drive.upper()} am Greaseweazle")
            if fehler is not None:
                # Eine Diskette, die keiner Erkennung folgt, ist nicht verloren —
                # sie ist nur nicht ERKANNT.  Deshalb hier nicht bloss der Grund,
                # sondern der Ausweg: das Dateisystem von Hand wählen.  Genau das
                # rettet gemischte Formate (26×128 auf Kopf 0, 5×1024 auf Kopf 1),
                # die es in keinem Katalog gibt.
                self.info_bar.zeige(
                    f"Nicht erkannt: {fehler}", "fehler",
                    knopf="Dateisystem wählen…",
                    bei_klick=self.menue_fs.exec)
                self.log_dock.append(f"Nicht geoeffnet: {fehler}")
                self.statusBar().showMessage(
                    f"Nicht erkannt — Dateisystem im Kopfbereich wählen "
                    f"(Laufwerk {drive.upper()})", STATUS_DAUER)
            else:
                self.log("Physisches Laufwerk: abgebrochen")
            self._aktionen_pruefen()
            return False

        self.tool = werkzeug
        self._fs_setzen(filesystem, neu_oeffnen=False)
        self.act_schreibschutz.blockSignals(True)
        self.act_schreibschutz.setChecked(self.tool.read_only)
        self.act_schreibschutz.blockSignals(False)
        self._reload()
        self._medium_meldungen()
        # ZULETZT: ohne Dateisystem ist das die wichtigere Auskunft, und sie darf
        # nicht vom Medium-Befund überschrieben werden.
        if not werkzeug.has_filesystem:
            self._roh_melden()
        self.log_dock.append(f"Physisches Laufwerk {drive.upper()}: "
                             f"{self.tool.filesystem}, {sitzung.status_text()}")
        self.statusBar().showMessage(
            f"Geöffnet: Laufwerk {drive.upper()} — {self.tool.filesystem}",
            STATUS_DAUER)
        self._physisch_uhr.start()
        self._physisch_tick()      # sofort, nicht erst nach dem ersten Intervall
        return True

    def _vollstaendig_einlesen(self, titel: str) -> bool:
        """Alle noch ungelesenen Spuren holen — Bedingung für jeden Schnitt.

        Bei einer Datei ist ohnehin alles da; nur am echten Laufwerk fehlt etwas.
        Gelesen wird im Arbeitsfaden mit Fortschritt: es können 150 Spuren sein.
        """
        if self.tool is None or self._physisch is None:
            return True
        fehlend = [(c, h)
                   for c in range(self.tool.medium_cylinders)
                   for h in range(self.tool.medium_heads)
                   if self.tool.track_state(c, h) == DiskTool.SPUR_UNBEKANNT]
        if not fehlend:
            return True

        if QMessageBox.question(
                self, titel,
                f"Von der Diskette sind noch {len(fehlend)} Spuren ungelesen.\n\n"
                "Sie müssen vorher geholt werden — der Schnitt löst das Abbild vom "
                "Laufwerk, danach ist nichts mehr nachzuholen.\n\nJetzt einlesen?",
                QMessageBox.Cancel | QMessageBox.Ok,
                QMessageBox.Cancel) != QMessageBox.Ok:
            return False

        werkzeug = self.tool
        _, fehler = mit_fortschritt(
            self, self._physisch,
            lambda: [werkzeug.track(c, h) for c, h in fehlend],
            titel=titel, text="Die restlichen Spuren werden gelesen…",
            ziel=len(fehlend), was="Spuren gelesen")
        if fehler is not None:
            self._fehler(titel, str(fehler))
            return False
        # Abgebrochen oder eine Spur unlesbar — dann NICHT schneiden.
        offen = [1 for c, h in fehlend
                 if werkzeug.track_state(c, h) == DiskTool.SPUR_UNBEKANNT]
        if offen:
            self._fehler(titel,
                         f"{len(offen)} Spuren liessen sich nicht lesen.  Ohne sie "
                         "wäre das Abbild nach dem Schnitt lückenhaft — es bleibt, "
                         "wie es ist.")
            return False
        self._reload()
        return True

    def _gerade_spuren(self) -> None:
        """Jede zweite Spur wegwerfen — Doppelschritt-Diskette geradeziehen (§12.6)."""
        self._schneiden(
            "Ungerade Spuren entfernen",
            "Jede zweite Spur wird aus dem Speicherabbild geworfen; aus "
            f"{self.tool.medium_cylinders} Spuren werden "
            f"{(self.tool.medium_cylinders + 1) // 2}.",
            lambda: self.tool.keep_even_tracks())

    def _seite1_weg(self) -> None:
        """Die Rückseite aus dem Speicherabbild werfen (§12.6)."""
        self._schneiden(
            "Seite 1 entfernen",
            "Seite 1 wird aus dem Speicherabbild geworfen; übrig bleibt eine "
            "einseitige Diskette.",
            lambda: self.tool.drop_second_side())

    def _schneiden(self, titel: str, was: str, tat) -> None:
        """Gemeinsamer Weg beider Abbild-Eingriffe: fragen, schneiden, neu erkennen.

        Beide **lösen das Abbild vom Laufwerk** — danach stimmt die Spurnummer nicht
        mehr mit der Kopfposition überein, ein Rückschreiben ginge auf die falschen
        Zylinder.  Das muss dastehen, bevor es passiert.

        Danach wird die Erkennung am **Speicherabbild** wiederholt: genau dafür ist
        der Schnitt ja da — eine 40-Spur-Diskette, die als 80 Spuren mit Altbestand
        hereinkam, ist danach als das erkennbar, was sie ist.
        """
        if self.tool is None:
            return
        # Vorher VOLLSTÄNDIG einlesen.  Der Schnitt löst vom Laufwerk; was jetzt noch
        # fehlt, fehlt dann für immer — und Lücken im Abbild machen jede spätere
        # Erkennung unmöglich (eine ungelesene Spur sieht aus wie eine unformatierte).
        if not self._vollstaendig_einlesen(titel):
            return
        hinweis = was
        if self._physisch is not None:
            hinweis += ("\n\nDamit endet die Verbindung zum Laufwerk: das Abbild "
                        "bleibt im Speicher, die Diskette wird nicht weiter gelesen "
                        "und auch nicht mehr beschrieben.")
        hinweis += "\n\nFortfahren?"
        if QMessageBox.question(self, titel, hinweis,
                                QMessageBox.Cancel | QMessageBox.Ok,
                                QMessageBox.Cancel) != QMessageBox.Ok:
            return

        try:
            spuren = tat()
        except K1520DiskError as e:
            self._fehler(titel, str(e))
            return

        # Die Sitzung ist jetzt nur noch Ballast — das Abbild hängt nicht mehr daran.
        if self._physisch is not None:
            self._close_physisch()
            self._physisch_uhr.stop()
            self.st_physisch.hide()
        self.log(f"{titel}: {spuren} Spuren im Abbild")

        try:
            erkannt = self.tool.redetect()
        except K1520DiskError as e:
            self._fehler(titel, f"Danach war nichts mehr zu lesen:\n{e}")
            return
        self._reload()
        self._medium_meldungen()
        if erkannt:
            self.info_bar.zeige(
                f"{titel}: {spuren} Spuren — erkannt als {self.tool.filesystem}.",
                "hinweis")
            self._fs_setzen(self.tool.filesystem, neu_oeffnen=False)
        else:
            self._roh_melden()

    def _roh_melden(self) -> None:
        """Sagen, dass die Diskette OHNE Dateisystem offen ist — und wie es weitergeht.

        Kein Meldungsfenster und kein Abbruch: die Diskette liegt im Speicher, der
        Diskeditor geht, das Abbild lässt sich sichern.  Was fehlt, ist die Deutung
        als Dateisystem — und die kann später noch kommen, wenn mehr gelesen ist
        oder das Abbild zurechtgeschnitten wurde (§12.6).

        Der Grund wird **gekürzt**: die Messung listet sonst jede einzelne Spur, und
        160 Zeilen passen auf keinen Bildschirm.  Vollständig steht sie im Protokoll.
        """
        grund = (self.tool.remarks or "").strip()
        self.log_dock.append("Kein Dateisystem erkannt. " + grund)
        self.info_bar.zeige(
            "Aus den zuerst gelesenen Spuren liess sich kein Dateisystem ermitteln — "
            + self._kurzgefasst(grund)
            + "  Die Diskette wird weiter eingelesen; Diskeditor und "
              "Abbild-Sichern gehen bereits.  Danach im Kopfbereich ein Dateisystem "
              "wählen oder das Abbild unter „Speicherabbild ändern\u201c "
              "zurechtschneiden.",
            "warnung", knopf="Dateisystem wählen…", bei_klick=self.menue_fs.exec)
        self.statusBar().showMessage("Kein Dateisystem erkannt — Diskette ist roh "
                                     "geöffnet", STATUS_DAUER)

    @staticmethod
    def _kurzgefasst(text: str, zeilen: int = 2, zeichen: int = 200) -> str:
        """Eine lange Diagnose auf Bildschirmmass bringen.

        Die Geometriemessung nennt JEDE Spur; ein Streifen mit 160 Zeilen ist keine
        Meldung mehr, sondern eine Wand.  Das Ganze steht im Protokoll (F8).
        """
        stuecke = [z.strip() for z in text.splitlines() if z.strip()]
        kurz = " ".join(stuecke[:zeilen])
        if len(kurz) > zeichen:
            kurz = kurz[:zeichen].rstrip() + " …"
        elif len(stuecke) > zeilen:
            kurz += " …"
        return kurz + ("  (alles im Protokoll, F8)" if len(stuecke) > zeilen else "")

    def _pruefe_defekte(self) -> bool:
        """Schadstellen melden — **einmal je Spur**, mit Ausweg.

        Zu rufen nach jedem Vorgang, der geschrieben haben könnte.  Das Abbild im
        Speicher ist dann noch heil; die Meldung sagt, wie man es rettet.
        """
        sitzung = self._physisch
        if sitzung is None:
            return False
        neu = sitzung.neue_defekte()
        if not neu:
            return False
        self.log(f"SCHREIBFEHLER auf Spur {neu}")
        self.info_bar.zeige(
            f"Schreibfehler auf Spur {neu} — das Speicherabbild ist noch heil.",
            "fehler", knopf="Diskette neu beschreiben…",
            bei_klick=self._neu_beschreiben)
        QMessageBox.warning(self, "Schreibfehler", sitzung.defekt_meldung(neu))
        return True

    def _neu_beschreiben(self) -> None:
        """Das Abbild noch einmal vollständig auf die (neue) Diskette schreiben."""
        sitzung = self._physisch
        if sitzung is None:
            return
        st = sitzung.stats()
        unbekannt = (st.tracks_total - st.tracks_known) if st else 0
        frage = ("Das Speicherabbild wird noch einmal vollständig auf die eingelegte "
                 "Diskette geschrieben.\n\nLegen Sie jetzt eine fehlerfreie Diskette ein.")
        if unbekannt:
            frage += (f"\n\n⚠ {unbekannt} von {st.tracks_total} Spuren wurden nie gelesen "
                      "und können deshalb nicht geschrieben werden.")
        if QMessageBox.question(self, "Diskette neu beschreiben", frage,
                                QMessageBox.Ok | QMessageBox.Cancel) != QMessageBox.Ok:
            return

        n = sitzung.rewrite_all()
        self.log(f"{n} Spuren zum Neubeschreiben eingestellt")
        _, fehler = mit_fortschritt(
            self, sitzung, lambda: sitzung.sync.flush(600_000),
            titel="Diskette neu beschreiben",
            text="Das Abbild wird geschrieben und zurückgelesen…",
            ziel=n, was="Spuren geschrieben",
            # Geschrieben wird aus dem Speicher — `tracks_known` ruehrt sich dabei
            # nicht, und der Balken stuende still.  Gezaehlt wird das Prueflesen:
            # erst danach gilt eine Spur als geschrieben (§7.1).
            zaehler=lambda st: st.verifies_done)
        if fehler is not None:
            self._fehler("Diskette neu beschreiben", str(fehler))
            return
        if not self._pruefe_defekte():
            self.info_bar.verbergen()
            QMessageBox.information(
                self, "Diskette neu beschreiben",
                f"{n} Spuren wurden geschrieben und fehlerfrei zurückgelesen.")

    def _am_abbild_erkennen(self, filesystem: str) -> bool:
        """Dateisystem am **Speicherabbild** deuten — ohne die Diskette neu zu lesen.

        Das ist der richtige Weg, sobald etwas im Speicher liegt: die Diskette noch
        einmal zu holen dauert und läse womöglich anderes (eine halb gelesene
        Diskette ist danach anders halb gelesen).
        """
        if self.tool is None:
            return False
        try:
            erkannt = self.tool.redetect(filesystem or None)
        except K1520DiskError as e:
            self._fehler("Dateisystem", str(e))
            return False
        self._reload()
        self._medium_meldungen()
        if not erkannt:
            self._roh_melden()
        else:
            self.log(f"Dateisystem: {self.tool.filesystem}")
        return erkannt

    def _physisch_erneut(self, filesystem: str) -> bool:
        """Die physische Diskette mit anderem Dateisystem noch einmal öffnen.

        Ein Sync-Handle ist nach dem Öffnen **verbraucht** — die Wahl lässt sich
        nicht an der laufenden Sitzung ändern.  Also wird mit denselben Angaben eine
        neue aufgebaut; das kostet den Erkennungslauf noch einmal (~10 s), ist aber
        der einzige ehrliche Weg.  Ohne ihn lief das Übersteuern bei einer physischen
        Diskette in `open_image("")` — leerer Pfad, alles weg.
        """
        from app.ui.physical_disk import PhysicalSession

        wahl = dict(self._physisch_wahl or {})
        if not wahl:
            return False
        self._close_tool()
        try:
            sitzung = PhysicalSession.start(**wahl)
        except Exception as e:                       # noqa: BLE001
            self._fehler("Physisches Laufwerk",
                         f"Das Laufwerk liess sich nicht erneut oeffnen:\n{e}")
            return False
        self._physisch = sitzung
        self._physisch_laufwerk = wahl.get("drive", "a")
        return self._physisch_weiter(sitzung, filesystem,
                                     bool(wahl.get("writable", False)))

    def _physisch_dialog(self) -> None:
        """Abfrage vor dem Einlegen und dann oeffnen.

        **Erst prüfen, dann fragen.**  Geht es gar nicht (Hosttools fehlen, die
        Anbindung lässt sich nicht laden), erscheint der Auswahldialog erst gar
        nicht — sonst füllt der Bediener ihn aus, klickt „Einlegen" und läuft
        danach in eine Meldung, die er schon vor dem Ausfüllen hätte haben können.
        """
        from app.ui.physical_disk import PhysicalDiskDialog, verfuegbarkeit

        ok, grund = verfuegbarkeit()
        if not ok:
            self._fehler("Physisches Laufwerk", grund)
            return
        if not self._darf_verwerfen():
            return
        wahl = PhysicalDiskDialog.frage(self, num_cyls=80, num_heads=2)
        if wahl is None:
            return
        self.open_physical(**wahl)

    def _physisch_schreiben_dialog(self) -> None:
        """Das geöffnete Abbild auf eine **echte** Diskette schreiben.

        Der Gegenweg zu „Physische Diskette laden": die Quelle ist das, was gerade
        offen ist — auch eine `.hfe`-Datei —, das Ziel ein echtes Laufwerk.  Jede
        bekannte Spur wird ins Medium des Laufwerks gelegt und gilt dort als
        geändert; den Rest erledigt der Arbeitsfaden im Hintergrund, samt
        Prüf-Lesen (§7.1).

        **Erst fragen, dann alles andere.**  Hier geht kein Abbild verloren, sondern
        eine Diskette — und zwar unwiederbringlich, bevor irgendetwas zu sehen ist.
        """
        from app.ui.physical_disk import (PhysicalDiskDialog, PhysicalSession,
                                          verfuegbarkeit)

        if self.tool is None:
            return
        ok, grund = verfuegbarkeit()
        if not ok:
            self._fehler("Physische Diskette überschreiben", grund)
            return

        if QMessageBox.question(
                self, "Physische Diskette überschreiben",
                "Die Funktion schreibt das aktuelle Speicherabbild auf eine physische "
                "Diskette und überschreibt dabei alle vorhandenen Daten.\n\n"
                "Sind Sie sicher, dass Sie das wollen?",
                QMessageBox.Cancel | QMessageBox.Ok,
                QMessageBox.Cancel) != QMessageBox.Ok:
            return

        # Die Geometrie der offenen Diskette vorschlagen — auf eine kleinere passt
        # sie ohnehin nicht, und der Bediener soll nicht raten müssen.
        wahl = PhysicalDiskDialog.frage(
            self, writable=True,
            titel="Diskette zum Überschreiben einlegen",
            abbild=f"{self.tool.medium_cylinders} Spuren × "
                   f"{self.tool.medium_heads} Seite(n)")
        if wahl is None:
            return
        wahl["writable"] = True          # ohne Schreibrecht ist der Punkt sinnlos
        wahl["read_ahead"] = False       # gelesen wird hier nichts, nur geschrieben

        # „Nur Seite 0" bei einem ZWEISEITIGEN Abbild ist erlaubt — es kann genau
        # gewollt sein (Seite 1 trägt Altlasten, die niemand haben will).  Aber es
        # geht dabei etwas verloren, und das muss dastehen, bevor es passiert.
        if wahl.get("num_heads", 2) < self.tool.medium_heads:
            if QMessageBox.question(
                    self, "Nur Seite 0 schreiben",
                    f"Das Abbild hat {self.tool.medium_heads} Seiten, geschrieben "
                    "wird nur Seite 0.\n\n**Der Inhalt von Seite 1 landet nicht auf "
                    "der Diskette.**  Das kann gewollt sein — etwa wenn dort nur "
                    "Altbestand steht.\n\nTrotzdem schreiben?".replace("**", ""),
                    QMessageBox.Cancel | QMessageBox.Ok,
                    QMessageBox.Cancel) != QMessageBox.Ok:
                return

        try:
            sitzung = PhysicalSession.start(**wahl)
        except Exception as e:                       # noqa: BLE001
            self._fehler("Physische Diskette überschreiben",
                         f"Das Laufwerk liess sich nicht oeffnen:\n{e}")
            return

        try:
            n = self.tool.write_to_physical(sitzung)
        except K1520DiskError as e:
            self._fehler("Physische Diskette überschreiben", str(e))
            sitzung.close()
            return

        # **Und das war's schon.**  Die Spuren stehen jetzt als „geändert" im Medium
        # des Laufwerks; der Arbeitsfaden schreibt sie von selbst hinaus (§7) — es
        # gibt hier nichts zu warten und darum auch kein Meldungsfenster.  Wie weit
        # er ist, sagt die Statuszeile; fertig meldet `_schreib_tick`.
        self._schreib_sitzung = sitzung
        self._schreib_gesamt  = n
        self._physisch_uhr.start()
        self._schreib_tick()          # sofort, nicht erst nach dem ersten Intervall
        self._aktionen_pruefen()
        self.log(f"{n} Spuren werden auf die eingelegte Diskette geschrieben")
        self.info_bar.zeige(
            f"Die Diskette im Laufwerk {wahl.get('drive', 'a').upper()} wird "
            f"beschrieben ({n} Spuren) — sie darf bis zum Ende nicht entnommen werden.",
            "hinweis")

    def _close_physisch(self) -> None:
        """Laufende Sitzung am echten Laufwerk beenden (schreibt Ausstehendes zurück)."""
        sitzung = self._physisch
        if sitzung is None:
            return
        self._physisch = None
        self._physisch_uhr.stop()
        self.st_physisch.clear()
        self.st_physisch.hide()
        from PySide6.QtGui import QCursor
        from PySide6.QtWidgets import QApplication
        QApplication.setOverrideCursor(QCursor(Qt.WaitCursor))
        try:
            sitzung.close()
        except Exception:
            pass
        finally:
            QApplication.restoreOverrideCursor()

    def create_disk(self, path, filesystem: str, label: str = "",
                    boot_image=None) -> bool:
        """Neue Diskette anlegen — mit ``boot_image`` als **Bootdiskette**.

        Das Bootabbild geht in die Systemspuren vor dem Dateisystem; passt es nicht,
        wird gar nichts angelegt und die Meldung nennt beide Grössen.
        """
        self._close_tool()
        try:
            self.tool = DiskTool.create(path, filesystem, label, boot_image)
        except K1520DiskError as e:
            self.tool = None
            self._aktionen_pruefen()
            self._fehler("Diskette anlegen", str(e))
            return False
        self._fs_setzen(filesystem, neu_oeffnen=False)
        self.act_schreibschutz.blockSignals(True)
        self.act_schreibschutz.setChecked(False)   # frisch angelegt = zum Beschreiben da
        self.act_schreibschutz.blockSignals(False)
        self._reload()
        self._medium_meldungen()
        self._zuletzt_merken(str(path))
        boot = f", Bootabbild {Path(boot_image).name}" if boot_image else ""
        self.log(f"Angelegt: {path} ({filesystem}, {self.tool.volume_count} Seiten{boot})")
        return True

    def close_disk(self) -> bool:
        """Die Diskette schließen — das Fenster bleibt stehen."""
        if self.tool is None:
            return False
        if not self._darf_verwerfen():
            return False
        self._close_tool()
        self.info_bar.verbergen()
        self._reload()
        self.log("Diskette geschlossen")
        return True

    def _close_tool(self) -> None:
        # Der Diskeditor hält dieselbe Diskette; er MUSS vorher zu sein, sonst
        # arbeitete er auf einem geschlossenen Griff weiter.
        editor = getattr(self, "_diskeditor", None)
        if editor is not None:
            editor.close()
            self._diskeditor = None
        if self.tool is not None:
            self.tool.close()
            self.tool = None
        # Erst das Werkzeug, DANN die Sitzung: ~DiskImage schreibt Ausstehendes noch
        # über den Arbeitsfaden zurück und löst sich erst danach vom Medium.
        self._close_physisch()

    def _darf_verwerfen(self) -> bool:
        """Vor dem Schließen/Verwerfen fragen, wenn etwas ungespeichert ist."""
        if self.tool is None or not self.tool.dirty:
            return True
        antwort = QMessageBox.question(
            self, "Ungespeicherte Änderungen",
            "Die Diskette hat ungespeicherte Änderungen. Jetzt speichern?",
            QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
        if antwort == QMessageBox.Cancel:
            return False
        if antwort == QMessageBox.Yes:
            return self.save()
        return True

    # ════════════════════════════════════════════════════════════════════════
    # Übertragung — ohne Dialog aufrufbar (Tests)
    # ════════════════════════════════════════════════════════════════════════

    def extract_all(self, dest_dir) -> bool:
        """Alles extrahieren; bei mehreren Seiten entstehen `Side0/`, `Side1/`."""
        if self.tool is None:
            return False
        try:
            self.tool.extract_all(dest_dir, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Extrahieren", str(e))
            return False
        self.folder_view.set_folder(dest_dir)
        self._reload()
        self.log(f"Extrahiert nach {dest_dir}")
        return True

    def insert_all(self, src_dir) -> bool:
        """Ordner einfügen — Struktur und Platz werden VORHER geprüft (§9.2)."""
        if self.tool is None:
            return False
        bericht = self.tool.check_fit(src_dir)
        if bericht != "passt":
            self._fehler("Einfügen", bericht + "\nEs wurde nichts geschrieben.")
            return False
        try:
            self.tool.insert_all(src_dir, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Einfügen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"Eingefügt aus {src_dir}")
        return True

    def extract_refs(self, refs: List[str], dest_dir) -> bool:
        """Ausgewählte Dateien holen; mehrseitige Disketten in ihre `SideN/`."""
        if self.tool is None or not refs:
            return False
        ziel = Path(dest_dir)
        ziel.mkdir(parents=True, exist_ok=True)
        try:
            for ref in refs:
                unter = ziel
                if "/" in ref and self.tool.volume_count > 1:
                    unter = ziel / ref.split("/", 1)[0]
                    unter.mkdir(parents=True, exist_ok=True)
                name = ref.split("/")[-1].replace(":", "_")
                self.tool.extract(ref, unter / name, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Extrahieren", str(e))
            return False
        self.folder_view.refresh()
        self.log(f"{len(refs)} Dateien nach {dest_dir}")
        return True

    def insert_paths(self, paths: List[str], volume: int = 0) -> bool:
        """Einzelne Dateien auf eine Seite schreiben."""
        if self.tool is None or not paths:
            return False
        praefix = ""
        if self.tool.volume_count > 1:
            praefix = f"{self.tool.volume_dir(volume)}/"
        try:
            for p in paths:
                if Path(p).is_dir():
                    raise K1520DiskError(
                        f"{p} ist ein Ordner — Ordner bitte über „Alles einfügen“")
                name = praefix + Path(p).name
                text = self.text_mode or Path(p).suffix.lower() in TEXT_ENDUNGEN
                self.tool.insert(p, name, text=text, overwrite=True)
        except K1520DiskError as e:
            self._fehler("Einfügen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"{len(paths)} Dateien auf die Diskette")
        return True

    def erase_refs(self, refs: List[str]) -> bool:
        if self.tool is None or not refs:
            return False
        try:
            for ref in refs:
                self.tool.erase(ref)
        except K1520DiskError as e:
            self._fehler("Löschen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"{len(refs)} Dateien gelöscht")
        return True

    def save_as(self, path) -> bool:
        """Unter neuem Namen/Container speichern und **dort weiterarbeiten**.

        Auch bei Schreibschutz erlaubt — die Quelle bleibt unberuehrt.  Genau der
        Weg, um vor Aenderungen eine Arbeitskopie anzulegen.
        """
        if self.tool is None:
            return False
        try:
            self.tool.save_as(path)
        except K1520DiskError as e:
            self._fehler("Speichern unter", str(e))
            return False
        self._reload()
        self._zuletzt_merken(str(path))
        self.log(f"Gespeichert unter {path}")
        return True

    def archive(self, zip_path) -> bool:
        """Abbild (.hfe), alle Dateien und ein Inhaltsverzeichnis in eine `.zip`.

        Reine Leseoperation — auch mit gesetztem Schreibschutz benutzbar.
        """
        if self.tool is None:
            return False
        try:
            ziel = create_archive(self.tool, zip_path, text_mode=self.text_mode)
        except (K1520DiskError, OSError) as e:
            self._fehler("Archivieren", str(e))
            return False
        self.log(f"Archiviert: {ziel}")
        return True

    def save_boot_image(self, path, volume: int = 0) -> bool:
        """Die Systemspuren als `.bin` sichern — der Weg zum eigenen Bootabbild.

        Reine Leseoperation; damit wird aus einer vorhandenen Bootdiskette die Datei,
        die „Neue Diskette" später bootfähig macht.
        """
        if self.tool is None:
            return False
        try:
            self.tool.read_boot_image(path, volume)
        except (K1520DiskError, OSError) as e:
            self._fehler("Bootabbild sichern", str(e))
            return False
        self.log(f"Bootabbild gesichert: {path} ({self.tool.boot_area_size(volume)} Byte)")
        return True

    def set_read_only(self, ro: bool) -> None:
        """Schreibschutz setzen — ohne Rueckfrage (die sitzt im Klick-Behandler)."""
        if self.tool is None:
            return
        self.tool.set_read_only(ro)
        self.act_schreibschutz.blockSignals(True)
        self.act_schreibschutz.setChecked(ro)
        self.act_schreibschutz.blockSignals(False)
        self._aktionen_pruefen()
        self.log("Schreibschutz " + ("gesetzt" if ro else "aufgehoben"))

    def save(self) -> bool:
        """Änderungen festschreiben.

        Bei einer **physischen** Diskette heisst das: warten, bis jede geänderte Spur
        geschrieben **und zurückgelesen** ist (§7.1).  Das kann eine Weile dauern und
        kann an einer Schadstelle scheitern — dann nennt die Meldung die Spur.
        """
        if self.tool is None:
            return False
        sitzung = self._physisch
        if sitzung is not None:
            _, fehler = mit_fortschritt(
                self, sitzung, self.tool.flush,
                titel="Auf die Diskette schreiben",
                text="Geänderte Spuren werden geschrieben und zurückgelesen…")
            if fehler is not None:
                if not self._pruefe_defekte():
                    self._fehler("Speichern", str(fehler))
                return False
            self._pruefe_defekte()
            self._reload()
            self.log("Auf die physische Diskette geschrieben")
            return True

        try:
            self.tool.flush()
        except K1520DiskError as e:
            self._fehler("Speichern", str(e))
            return False
        self._reload()
        self.log(f"Gespeichert: {self.tool.path}")
        return True

    # ════════════════════════════════════════════════════════════════════════
    # Dialoge (Klick-Behandler)
    # ════════════════════════════════════════════════════════════════════════

    def _oeffnen_dialog(self) -> None:
        if not self._darf_verwerfen():
            return
        pfad, _ = QFileDialog.getOpenFileName(
            self, "Diskettenabbild öffnen", self._disketten_ordner(),
            "Diskettenabbilder (*.hfe *.dmk *.img);;Alle Dateien (*)")
        if pfad:
            self.open_image(pfad)

    def _neu_dialog(self) -> None:
        if not self._darf_verwerfen():
            return
        namen = [f.name for f in filesystems()]
        fs, ok = QInputDialog.getItem(self, "Neue Diskette", "Dateisystem:", namen, 0, False)
        if not ok:
            return
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Neue Diskette anlegen", self._disketten_ordner(),
            "HFE-Abbild (*.hfe);;DMK-Abbild (*.dmk);;Sektorabbild (*.img)")
        if not pfad:
            return

        # Bootfähig? — nur, wo es Systemspuren gibt (eine Datendiskette wie cpa800
        # beginnt auf Zylinder 0 und kann gar nicht booten).
        boot = self._bootabbild_waehlen(fs)
        if boot is False:          # Auswahl abgebrochen → gar nichts anlegen
            self.log("Neue Diskette: abgebrochen (kein Bootabbild ausgewählt)")
            return

        label, _ = QInputDialog.getText(self, "Neue Diskette", "Datenträgername:")
        self.create_disk(pfad, fs, label or "", boot or None)

    def _bootabbild_waehlen(self, filesystem: str):
        """Bootabbild für eine neue Diskette erfragen.

        Returns:
            Pfad (str) · ``None`` = gewöhnliche Diskette · ``False`` = abgebrochen,
            es soll gar nichts angelegt werden.
        """
        platz = next((f.boot_capacity for f in filesystems() if f.name == filesystem), 0)
        if platz <= 0:
            return None

        frage = QMessageBox.question(
            self, "Neue Diskette",
            f"Soll die Diskette bootfähig sein?\n\n"
            f"Die Systemspuren von „{filesystem}\" fassen {platz} Byte. "
            f"Ein Bootabbild (.bin) holt man mit „Bootabbild sichern\" aus einer "
            f"vorhandenen Bootdiskette.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if frage != QMessageBox.Yes:
            return None

        pfad, _ = QFileDialog.getOpenFileName(
            self, "Bootabbild auswählen", self._disketten_ordner(),
            "Bootabbild (*.bin);;Alle Dateien (*)")
        return pfad or False

    def _speichern_unter_dialog(self) -> None:
        if self.tool is None:
            return
        # UDOS kann kein .img tragen — den Filter dann gar nicht erst anbieten.
        filter_ = "HFE-Abbild (*.hfe);;DMK-Abbild (*.dmk)"
        if not any(f.name == self.tool.filesystem and f.type == "udos"
                   for f in filesystems()):
            filter_ += ";;Sektorabbild (*.img)"
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Speichern unter", self._neben_der_diskette(), filter_)
        if pfad:
            self.save_as(pfad)

    def _archivieren_dialog(self) -> None:
        if self.tool is None:
            return
        vorschlag = str(Path(self.tool.path).with_suffix(".zip"))
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Archivieren", vorschlag, "ZIP-Archiv (*.zip)")
        if pfad:
            self.archive(pfad)

    def _bootabbild_sichern_dialog(self) -> None:
        if self.tool is None or self.tool.boot_area_size(0) == 0:
            return
        # Bei UDOS ist jede Seite ein eigener Datentraeger — gebootet wird von Seite 0.
        vorschlag = str(Path(self.tool.path).with_suffix(".bin"))
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Bootabbild sichern", vorschlag, "Bootabbild (*.bin)")
        if pfad:
            self.save_boot_image(pfad)

    def _angaben_dialog(self) -> None:
        if self.tool is None:
            return
        DiskInfoDialog(self.tool, self).exec()

    def open_help(self):
        """Das Handbuch öffnen (nicht modal — man liest nach und arbeitet weiter).

        Wie beim Diskeditor gibt es genau EINS je Hauptfenster; ein zweites F1 holt
        das vorhandene nach vorn.
        """
        vorhanden = getattr(self, "_hilfe", None)
        if vorhanden is not None and vorhanden.isVisible():
            vorhanden.raise_()
            vorhanden.activateWindow()
            return vorhanden
        self._hilfe = HelpWindow(self)
        self._hilfe.show()
        return self._hilfe

    def _ueber_dialog(self) -> None:
        from app.core_binding.k1520disk import version
        QMessageBox.about(
            self, "Über k1520DiskTool",
            f"<h3>k1520DiskTool</h3>"
            f"<p>Dateiaustausch mit K1520-Disketten (CP/A, SCPX, UDOS).</p>"
            f"<p>Bibliothek: {version()}</p>")

    def _schreibschutz_umgeschaltet(self, an: bool) -> None:
        """Schreibschutz — der bewusste Schritt zum Schreiben.

        Beim Zurueckschalten auf Nur-Lesen mit ungespeicherten Aenderungen wird
        gefragt: speichern, verwerfen (Datei neu einlesen) oder abbrechen.
        """
        if self.tool is None:
            return
        if an and self.tool.dirty:
            antwort = QMessageBox.question(
                self, "Ungespeicherte Änderungen",
                "Die Diskette hat ungespeicherte Änderungen.\n"
                "Vor dem Schreibschutz speichern?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
            if antwort == QMessageBox.Cancel:
                self.act_schreibschutz.blockSignals(True)
                self.act_schreibschutz.setChecked(False)
                self.act_schreibschutz.blockSignals(False)
                return
            if antwort == QMessageBox.Yes:
                if not self.save():
                    self.act_schreibschutz.blockSignals(True)
                    self.act_schreibschutz.setChecked(False)
                    self.act_schreibschutz.blockSignals(False)
                    return
            else:
                # Verwerfen heisst: die Datei noch einmal einlesen.
                pfad = self.tool.path
                self.open_image(pfad, self.kopf.filesystem())
                return
        self.set_read_only(an)

    # ── Wo die Dialoge aufgehen (§20.8) ─────────────────────────────────────
    #
    # Nie im Installationsordner: dort liegt das Programm, nicht die Arbeit des
    # Anwenders.  Aufgelöst wird ausschließlich über `app.paths` — dieselbe
    # Stelle, die auch der Emulator benutzt, damit beide Programme auf dieselben
    # Ordner zeigen.

    def _disketten_ordner(self) -> str:
        """Startpunkt für Abbild-Dialoge — wie das Laufwerksfeld des Emulators."""
        return str(paths.default_disk_dir())

    def _neben_der_diskette(self) -> str:
        """Startpunkt für „Speichern unter…": neben der geöffneten Diskette.

        Eine Arbeitskopie gehört dorthin, wo das Original liegt; nur ohne
        geöffnete Diskette fällt es auf den Diskettenordner zurück.
        """
        if self.tool is not None:
            return str(Path(self.tool.path).parent)
        return self._disketten_ordner()

    def _ordner_startpunkt(self) -> str:
        """Startpunkt für die Ordnerwahl.

        Der bereits gewählte Ordner, sonst der Dateiordner des Anwenders
        (:func:`app.paths.default_folder_dir`).
        """
        if self.folder_view.folder:
            return str(self.folder_view.folder)
        return str(paths.default_folder_dir())

    def _ordner_dialog(self) -> None:
        pfad = QFileDialog.getExistingDirectory(
            self, "Ordner wählen", self._ordner_startpunkt())
        if pfad:
            self.folder_view.set_folder(pfad)

    def _fs_setzen(self, name: str, *, neu_oeffnen: bool) -> None:
        """Dateisystemwahl in Kopf UND Menü setzen — und ggf. neu öffnen.

        Der einzige Ort, an dem die beiden Darstellungen derselben Wahl gesetzt
        werden; sonst liefen Feld und Menü auseinander.
        """
        name = name or ""
        self.kopf.setze_filesystem(name)
        a = self._fs_aktionen.get(name)
        if a is not None:
            a.setChecked(True)
        if not neu_oeffnen:
            return
        # Eine physische Diskette hat KEINEN Pfad — `open_image("")` warf hier alles
        # weg, und beim Übersteuern blieb die Anzeige leer.  Sie braucht eine neue
        # Sitzung mit denselben Angaben; die stehen in `_physisch_wahl`.  Das gilt
        # auch, wenn die Erkennung gerade gescheitert ist (dann gibt es kein `tool`,
        # aber sehr wohl etwas zu übersteuern) — es ist sogar der Hauptfall.
        if self.tool is not None and not self.tool.path:
            # Physische Diskette (oder ein davon abgelöstes Abbild): sie liegt bereits
            # im Speicher — noch einmal einzulesen wäre Zeitverschwendung und läse
            # womöglich anderes.  Die Erkennung läuft deshalb auf dem Abbild.
            self._am_abbild_erkennen(name)
        elif self.tool is None and self._physisch_wahl:
            self._physisch_erneut(name)      # gescheitertes Öffnen: neue Sitzung
        elif self.tool is not None:
            self.open_image(self.tool.path, name)

    def _modus_geaendert(self) -> None:
        self.st_modus.setText("Text" if self.text_mode else "binär")

    def _aktualisieren(self) -> None:
        self._reload()
        self.log("Aktualisiert")

    def _leere_auswahl(self, was: str) -> None:
        """Der Fall soll gar nicht mehr vorkommen — die Aktion ist dann gesperrt.

        Über Kontextmenü/Ziehen kann er trotzdem eintreten; dann sagt es die
        Statuszeile, nicht ein Meldungsfenster (§20.4).
        """
        self.statusBar().showMessage(f"{was}: keine Datei ausgewählt.", STATUS_DAUER)

    def _ziel_ordner(self) -> Optional[str]:
        if self.folder_view.folder:
            return str(self.folder_view.folder)
        pfad = QFileDialog.getExistingDirectory(
            self, "Zielordner wählen", self._ordner_startpunkt())
        if pfad:
            self.folder_view.set_folder(pfad)
            return pfad
        return None

    def _alles_waehlen(self) -> None:
        """Alles in der Liste auswählen, die gerade den Eingabefokus hat."""
        ziel = self.disk_view.tree
        if self.folder_view.tree.hasFocus():
            ziel = self.folder_view.tree
        ziel.selectAll()

    def _extrahieren_auswahl(self) -> None:
        refs = self.disk_view.selected_refs()
        if not refs:
            self._leere_auswahl("Holen")
            return
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_refs(refs, ziel)

    def _extrahieren_refs(self, refs: List[str]) -> None:
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_refs(refs, ziel)

    def _einfuegen_auswahl(self) -> None:
        pfade = self.folder_view.selected_paths()
        if not pfade:
            self._leere_auswahl("Schreiben")
            return
        seite = self.folder_view.selected_side()
        if seite is None:
            seite = self.disk_view.current_volume()
        self.insert_paths(pfade, seite)

    def _einfuegen_pfade(self, pfade: List[str], volume: int) -> None:
        self.insert_paths(pfade, volume)

    def _alles_extrahieren(self) -> None:
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_all(ziel)

    def _alles_einfuegen(self) -> None:
        quelle = self._ziel_ordner()
        if quelle:
            self.insert_all(quelle)

    def _loeschen_auswahl(self) -> None:
        self._loeschen_refs(self.disk_view.selected_refs())

    def _loeschen_refs(self, refs: List[str]) -> None:
        if not refs:
            self._leere_auswahl("Löschen")
            return
        antwort = QMessageBox.question(
            self, "Löschen",
            f"{len(refs)} Datei(en) von der Diskette löschen?\n" + "\n".join(refs[:10]))
        if antwort == QMessageBox.Yes:
            self.erase_refs(refs)

    # ── Eigenschaften ───────────────────────────────────────────────────────

    def show_properties(self, ref: str) -> bool:
        """Eigenschaften-Dialog zu einer Datei öffnen.

        Der Dialog bekommt den **frisch gelesenen** Verzeichniseintrag (§9.3) und
        schreibt selbst; danach wird die Ansicht neu aufgebaut, weil ein geänderter
        Nutzerbereich den Namen verändert.

        Returns:
            False, wenn zu ``ref`` kein Eintrag (mehr) im Verzeichnis steht.
        """
        if self.tool is None:
            return False
        eintrag = next((e for e in self.tool.list() if e.ref == ref), None)
        if eintrag is None:
            self._fehler("Eigenschaften", f"'{ref}' steht nicht im Verzeichnis.")
            return False

        dialog = PropertiesDialog(self.tool, eintrag, self)
        dialog.exec()
        self._reload()
        return True

    def _eigenschaften_ref(self, ref: str) -> None:
        self.show_properties(ref)

    def _eigenschaften_auswahl(self) -> None:
        refs = self.disk_view.selected_refs()
        if len(refs) != 1:
            self._leere_auswahl("Eigenschaften")
            return
        self.show_properties(refs[0])

    # ── Diskeditor ──────────────────────────────────────────────────────────

    def open_disk_editor(self):
        """Das Sektorfenster öffnen (nicht modal — man arbeitet nebenher weiter).

        Es gibt genau EINS je Hauptfenster; ein zweiter Klick holt das vorhandene
        nach vorn, statt eine zweite Sicht auf dieselbe Diskette aufzumachen.
        """
        if self.tool is None:
            return None
        vorhanden = getattr(self, "_diskeditor", None)
        if vorhanden is not None and vorhanden.isVisible() and vorhanden.tool is self.tool:
            vorhanden.raise_()
            vorhanden.activateWindow()
            return vorhanden

        self._diskeditor = DiskEditorWindow(self.tool, self)
        # Ein geschriebener Sektor ist eine Änderung wie jede andere: Titelmarke,
        # und die Dateiliste kann sich mitgeändert haben.
        self._diskeditor.disk_changed.connect(self._reload)
        self._diskeditor.show()
        return self._diskeditor

    # ── Schließen ───────────────────────────────────────────────────────────

    def closeEvent(self, event):  # noqa: N802
        # Ein laufender Schreibvorgang ist der einzige Grund, das Schliessen
        # aufzuhalten: eine halb beschriebene Diskette ist unbrauchbar, und der
        # Bediener sieht dem Fenster nicht an, dass hinten noch etwas laeuft.
        if self._schreib_sitzung is not None:
            st = self._schreib_sitzung.stats()
            offen_spuren = st.tracks_dirty if st else 0
            if QMessageBox.question(
                    self, "Es wird noch geschrieben",
                    f"Auf die Diskette im Laufwerk sind noch {offen_spuren} Spuren "
                    "zu schreiben.  Wird jetzt beendet, bleibt sie unvollständig.\n\n"
                    "Trotzdem beenden?",
                    QMessageBox.Cancel | QMessageBox.Ok,
                    QMessageBox.Cancel) != QMessageBox.Ok:
                event.ignore()
                return
        if not self._darf_verwerfen():
            event.ignore()
            return
        self._zustand_sichern()
        if self._schreib_sitzung is not None:
            self._schreib_fertig(None)
        self._close_tool()
        event.accept()
