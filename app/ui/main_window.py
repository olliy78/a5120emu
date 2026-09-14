"""
K1520 Emulator — Hauptfenster
=============================

Bildschirm, Tastatur, Laufwerke und Einstellungen als Kästen um die Bildröhre,
darüber Menü und einrichtbare Symbolleiste, darunter die Statuszeile.

Drei Dinge, die man beim Ändern wissen muss:

* **Jede Bedienung ist eine ``QAction``** und steht in `app/ui/actions.py` —
  Menü und Symbolleiste zeigen dieselbe.  Neue Bedienwege kommen dort hinzu,
  nicht hier.
* **Die Symbolleiste wird aus einer Liste von Aktionsnamen gebaut**
  (:meth:`MainWindow._leiste_fuellen`).  Diese Liste steht in der Konfiguration
  (``window.toolbar``), der Anwender stellt sie über *Ansicht ▸ Symbolleiste
  einrichten* zusammen.
* **Die Statuszeile zeigt Zustand, keine Zähler** (`app/ui/status_bar.py`):
  gemessenes Tempo, eingelegte Abbilder, Schreibschutz.
"""

import os
import sys
import time

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QMessageBox, QDockWidget, QMenu, QFileDialog,
    QScrollArea, QToolBar, QToolButton
)
from PySide6.QtCore import Qt, QTimer, QSize, QByteArray, QEvent
from PySide6.QtGui import QAction, QActionGroup, QGuiApplication, QIcon

from app.ui.screen_widget import ScreenWidget
from app.ui.settings_widget import SettingsWidget
from app.ui.drive_widget import DriveWidget
from app.ui.keyboard import KeyboardWidget
from app.ui.focus import release_focus, ScreenFocusGuard
from app.ui.help_window import HelpWindow
from app.ui import status_bar
from app.ui.status_bar import MachineStatus
from app.ui.toolbar_config import ToolbarDialog
from app.ui import actions as aktionen
from app.ui_icons import icon
from app.core_binding.k1520 import K1520Emulator
from app import config_io
from app import drive_types as dt
from app import paths
from app import programme
from app import takt


class MainWindow(QMainWindow):
    """Main emulator window."""
    
    def __init__(self, disks=None):
        """Initialize main window.

        :param disks: Diskettenabbilder von der Kommandozeile, in Laufwerks-
            reihenfolge (A:, B:, C:, D:).  Sie werden NACH der gespeicherten
            Konfiguration eingelegt und überschreiben deren Belegung nur für die
            angegebenen Laufwerke — was der Anwender beim Aufruf nennt, gewinnt.
        """
        super().__init__()
        self.setWindowTitle("K1520 A5120 Emulator")
        self.setWindowIcon(QIcon.fromTheme("computer"))
        
        # Current drive-bay configuration (one core DriveProfile name per K5122
        # slot).  Starts from the A5120 standard (3× K5601, 4th slot empty) and is
        # overridden by the restored config / the Einstellungen → Laufwerke tab.
        self._drive_types = list(dt.DEFAULT_DRIVE_TYPES)

        # Create emulator (powered on only AFTER the config restored the disks,
        # so a cold start boots from the last-mounted images).
        try:
            self.emulator = K1520Emulator(self._drive_types)
        except Exception as e:
            QMessageBox.critical(self, "Initialization Error", str(e))
            raise

        # Emulator loop parameters.
        # The A5120 U880 runs at ~2.45 MHz (matches the core's K5122 cpu_hz default).
        # To emulate at real time we must execute cpu_hz * frame_interval cycles per
        # frame — with a 20 ms tick that is 49000 cycles, NOT the old 10000 (which ran
        # the machine at only 0.2x speed, so a boot that takes ~13.8M cycles dragged on
        # for ~28 s instead of ~5.6 s, and the CP/A clock ran 5x too slow).
        self.CPU_HZ = takt.NENNTAKT_HZ
        self.frame_interval_ms = 20  # 50 Hz
        # speed_factor > 1.0 fast-forwards (e.g. to shorten the boot); 1.0 = real time;
        # 0.0 = unlimited (run_timer interval 0 → as fast as the host allows).
        self.speed_factor = 1.0
        self._emu_started = False       # gate: don't run the loop before power-on
        self._loading_config = False    # gate: suppress autosave while applying config

        # Default window size on a fresh start (no size stored in the config yet,
        # or the stored size no longer fits on the current screen).
        self.DEFAULT_WIDTH = 1024
        self.DEFAULT_HEIGHT = 680
        # True once a saved dock layout was restored: the auto-shrink layout
        # (see _shrink_keyboard) then steps aside so the user's stored widget
        # sizes are honoured instead of being forced back to minimum.
        self._has_saved_layout = False
        # True, sobald der Anwender selbst eine Trennlinie gezogen hat: die
        # Startaufteilung (:meth:`_shrink_keyboard`) hält sich dann heraus.
        self._nutzer_layout = False
        # True, solange WIR die Kästen umbauen — dann ist eine Größenänderung
        # eines Kastens nicht der Zug des Anwenders.
        self._layout_laeuft = False
        # Last "normal" (non-fullscreen/-maximized) window size, persisted so the
        # size is restored on the next start.
        self._normal_size = QSize(self.DEFAULT_WIDTH, self.DEFAULT_HEIGHT)
        # „maximiert" aus der Konfiguration, nachzuholen beim ersten Anzeigen
        # (`showEvent`) — vorher verfällt es (siehe `_maximiert_herstellen`).
        self._maximiert_nachholen = None

        self.run_timer = QTimer()
        self.run_timer.timeout.connect(self._run_emulator)
        self.run_timer.setInterval(self.frame_interval_ms)

        # Alle Bedienwege einmal anlegen (Menü und Leiste zeigen dieselben).
        aktionen.erzeuge_aktionen(self)
        # Inhalt und Stil der Symbolleiste — aus der Konfiguration überschrieben.
        self._leisten_inhalt = list(aktionen.STANDARD)
        self._hilfe = None

        # Setup UI
        self.setup_ui()
        self.setup_menus()
        # Erst jetzt: die Leiste kann Kastenschalter zeigen, und die gibt es
        # erst, seit die Kästen stehen.
        self._leiste_fuellen(self._leisten_inhalt)

        # Status updates
        self.cycles = 0
        self.frame_count = 0
        # Bezugspunkt der Tempomessung: Zykluszahl und Uhrzeit des letzten Blicks.
        self._tempo_zeit = time.monotonic()
        self._tempo_cycles = 0
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self._update_status)
        self.status_timer.start(1000)  # Update every second

        # Die Laufwerksleuchten brauchen einen VIEL schnelleren Takt: ein
        # Sektorzugriff dauert wenige Zehntelsekunden, im Sekundentakt abgetastet
        # blitzt die Leuchte praktisch nie auf.  Derselbe Takt wie die Leuchten
        # im Laufwerkskasten (DriveWidget._led_timer), und genauso billig —
        # je Laufwerk eine Abfrage, neu gezeichnet wird nur bei echter Änderung.
        self._lamp_timer = QTimer(self)
        self._lamp_timer.timeout.connect(self._update_drive_lamps)
        self._lamp_timer.start(120)

        # Auto-save: coalesce rapid changes (slider drags, mounts) into one write.
        self._autosave_timer = QTimer(self)
        self._autosave_timer.setSingleShot(True)
        self._autosave_timer.setInterval(400)
        self._autosave_timer.timeout.connect(self._autosave_now)

        # Persist on any settings/disk change.
        self.settings_widget.crtChanged.connect(self._schedule_autosave)
        self.settings_widget.speedChanged.connect(self._on_speed_selected)
        self.settings_widget.driveTypesChanged.connect(self._on_drive_types_selected)
        self.drives_widget.disk_mounted.connect(lambda *_: self._schedule_autosave())
        self.drives_widget.disk_unmounted.connect(lambda *_: self._schedule_autosave())
        # Die Statuszeile nennt die eingelegten Abbilder — sie darf nicht bis zum
        # nächsten Sekundentakt hinterherhinken.
        self.drives_widget.disk_mounted.connect(lambda *_: self._update_drive_status())
        self.drives_widget.disk_unmounted.connect(lambda *_: self._update_drive_status())

        # Persist which panels are active (dock shown/hidden) whenever that changes.
        #
        # Und ihre GRÖSSE: ein Zug an der Trennlinie zwischen zwei Kästen ändert
        # nur die Kästen, nicht das Fenster — es gab also weder ein
        # `resizeEvent` des Fensters noch ein Signal, und die neue Aufteilung
        # wurde nie gespeichert (sie überlebte nur, wenn zufällig etwas anderes
        # ein Speichern auslöste).  Ein Ereignisfilter fängt das ab; das
        # Sammeln im Autosave-Timer sorgt dafür, dass ein Ziehen EINE Schreibung
        # ergibt und nicht fünfzig.
        for dock in (self.screen_dock, self.keyboard_dock,
                     self.drives_dock, self.settings_dock):
            dock.visibilityChanged.connect(lambda *_: self._kasten_sichtbarkeit())
            dock.dockLocationChanged.connect(lambda *_: self._schedule_autosave())
            dock.installEventFilter(self)
        # Beim Umdocken der Tastatur ändert sich ihre Breite — und mit ihr die
        # Höhe, die sie braucht (siehe :meth:`_tastatur_einpassen`).
        self.keyboard_dock.dockLocationChanged.connect(
            lambda *_: QTimer.singleShot(0, self._tastatur_einpassen))

        # Fall back to the default size before applying the config, so the window
        # has a defined size even if the config stores no window section.
        self.resize(self.DEFAULT_WIDTH, self.DEFAULT_HEIGHT)

        # Restore the last configuration (or create the default on first run) —
        # this applies CRT/speed, mounts the stored disks and restores the
        # window size + dock layout.
        self._load_or_create_default_config()

        # Disketten von der Kommandozeile — NACH der Konfiguration, damit sie
        # deren Belegung schlagen, und VOR power_on(), damit der Kaltstart schon
        # von der genannten Diskette bootet (genau dafuer gibt man sie an).
        # Nicht gespeichert: ein `a5120emu fremde.hfe` soll die gemerkte
        # Belegung des Anwenders nicht dauerhaft ersetzen.
        self._mount_cli_disks(disks)

        # Die Statuszeile einmal von Hand nachziehen: das Wiederherstellen legt
        # die Disketten bewusst OHNE `disk_mounted` ein (es ist keine
        # Nutzeraktion), sonst stünde dort bis zum ersten Sekundentakt „leer".
        self._update_drive_status()

        # Cold start with the restored disks present, then begin running.
        self.emulator.power_on()
        self._emu_started = True
        self.run_timer.start()
        self.screen_widget.start_display()
        # Der Takt steht sofort in der Zeile — nicht erst, wenn der Sekundentakt
        # das erste Mal schlägt.
        self.status_widget.set_takt(self.speed_factor)
    
    def setup_ui(self):
        """Setup main UI layout.

        Anordnung: **linke Spalte** = Bildschirm oben, Tastatur darunter (ein
        vertikaler Split innerhalb desselben Dock-Bereichs — dessen Trennlinie
        ist im Gegensatz zu einer Bottom-Area-Grenze frei verschiebbar);
        **rechte Spalte** = Laufwerke/Einstellungen über die volle Höhe, schmal.
        Power/Reset liegen in einer Toolbar, die Statuszeile in der
        QMainWindow-Statusleiste.  Die Tastatur wird beim Start und nach jedem
        Fenster-Resize auf ihre Minimalhöhe gebracht, damit der Bildschirm
        maximal groß ist (siehe :meth:`_shrink_keyboard`).
        """
        # Linke bzw. rechte Spalte über die volle Höhe spannen (keine
        # Bottom-Area — die Tastatur sitzt als Split in der linken Spalte).
        self.setCorner(Qt.TopLeftCorner, Qt.LeftDockWidgetArea)
        self.setCorner(Qt.BottomLeftCorner, Qt.LeftDockWidgetArea)
        self.setCorner(Qt.TopRightCorner, Qt.RightDockWidgetArea)
        self.setCorner(Qt.BottomRightCorner, Qt.RightDockWidgetArea)

        # ── Kein zentrales Widget: die Docks füllen das ganze Fenster ────────
        # Wichtig: das zentrale Widget wird *versteckt*, NICHT auf 0×0 geklemmt.
        # Ein harter 0×0-Maximumsatz nimmt der Dock-Anordnung ihre elastische
        # Mitte → die Trennlinien lassen sich dann nicht mehr verschieben.  Ein
        # verstecktes zentrales Widget belegt keinen Platz, die Docks füllen das
        # Fenster und die Separatoren bleiben ziehbar.
        central = QWidget(self)
        self.setCentralWidget(central)
        central.hide()

        # ── Symbolleiste ─────────────────────────────────────────────────────
        # Beweglich, ausblendbar (Ansicht ▸ Symbolleiste) und einrichtbar
        # (Ansicht ▸ Symbolleiste einrichten…).  Der Name bleibt
        # "controls_toolbar", damit ein schon gespeichertes Fensterlayout sie
        # weiterhin wiederfindet.
        self.controls_bar = QToolBar("Symbolleiste", self)
        self.controls_bar.setObjectName("controls_toolbar")
        self.controls_bar.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
        self.controls_bar.setMovable(True)
        self.addToolBar(Qt.TopToolBarArea, self.controls_bar)

        # Der Netzschalter steht beim Start auf EIN (power_on() folgt unten) —
        # ohne Signal, sonst liefe der Kaltstart, bevor es Laufwerke gibt.
        self.act_power.blockSignals(True)
        self.act_power.setChecked(True)
        self.act_power.blockSignals(False)
        self.act_power.setText(aktionen.POWER_TEXT[True])

        # ── Statusleiste ─────────────────────────────────────────────────────
        # Rechts der Zustand (Tempo, Laufwerke), links bleibt Platz für die
        # flüchtigen Meldungen des Fensters.
        self.status_widget = MachineStatus()
        self.statusBar().addPermanentWidget(self.status_widget)
        self.statusBar().showMessage("Bereit")

        # ── Bildschirm-Dock (links) ──────────────────────────────────────────
        self.screen_widget = ScreenWidget()
        self.screen_widget.set_emulator(self.emulator)
        # Fullscreen is owned by the window (see enter/exit_fullscreen); the
        # screen only requests it via signals.  Dock-/Undock-Reparenting des
        # QOpenGLWidget überlebt der Kontext dank aboutToBeDestroyed-Cleanup.
        self.screen_widget.toggleFullscreenRequested.connect(self.toggle_fullscreen)
        self.screen_widget.exitFullscreenRequested.connect(self.exit_fullscreen)

        self.screen_dock = QDockWidget("Bildschirm", self)
        self.screen_dock.setObjectName("screen_dock")
        self.screen_dock.setWidget(self.screen_widget)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.screen_dock)

        # ── Tastatur-Dock (in der linken Spalte UNTER den Bildschirm) ────────
        self.keyboard_widget = KeyboardWidget()
        self.keyboard_widget.keyPressed.connect(self._on_kbd_press)
        self.keyboard_widget.keyReleased.connect(self._on_kbd_release)
        # Die echte Tastatur geht durch die Nachbildung: sie zeigt mit, welche
        # Taste angesprochen wird, und bringt ihren Feststeller zur Geltung.
        self.screen_widget.key_sink = self.keyboard_widget

        self.keyboard_dock = QDockWidget("Tastatur", self)
        self.keyboard_dock.setObjectName("keyboard_dock")
        self.keyboard_dock.setWidget(self.keyboard_widget)
        # Split UNTER den Bildschirm (gleicher Dock-Bereich) → verschiebbare
        # Trennlinie zwischen Bildschirm und Tastatur.
        self.addDockWidget(Qt.LeftDockWidgetArea, self.keyboard_dock)
        self.splitDockWidget(self.screen_dock, self.keyboard_dock, Qt.Vertical)

        # ── Laufwerke-Dock (rechts, scrollbar) ───────────────────────────────
        self.drives_dock = QDockWidget("Laufwerke", self)
        self.drives_dock.setObjectName("drives_dock")
        self.drives_widget = DriveWidget(self.emulator, self._drive_types)
        drives_scroll = QScrollArea()
        drives_scroll.setWidgetResizable(True)
        drives_scroll.setWidget(self.drives_widget)
        self.drives_dock.setWidget(drives_scroll)
        self.addDockWidget(Qt.RightDockWidgetArea, self.drives_dock)

        # ── Einstellungen-Dock (rechts, getabbt; anfangs versteckt) ──────────
        self.settings_dock = QDockWidget("Einstellungen", self)
        self.settings_dock.setObjectName("settings_dock")
        self.settings_widget = SettingsWidget(self.screen_widget)
        self.settings_dock.setWidget(self.settings_widget)
        self.addDockWidget(Qt.RightDockWidgetArea, self.settings_dock)
        self.tabifyDockWidget(self.drives_dock, self.settings_dock)

        # ── Kastenschalter ───────────────────────────────────────────────────
        # Sie kommen von Qt (``toggleViewAction``) und sind damit immer richtig
        # herum angehakt.  Beschriftung, Symbol und Kurzwort bekommen sie hier —
        # so lassen sie sich wie jede andere Aktion in die Leiste stellen.
        for name, dock, text, bild, kurz in (
                ("screen", self.screen_dock, "&Bildschirm", "screen", "Bildschirm"),
                ("keyboard", self.keyboard_dock, "&Tastatur", "keyboard", "Tastatur"),
                ("drives", self.drives_dock, "&Laufwerke", "drives", "Laufwerke"),
                ("settings", self.settings_dock, "&Einstellungen", "settings",
                 "Einstellungen")):
            a = dock.toggleViewAction()
            a.setText(text)
            a.setIcon(icon(bild))
            a.setIconText(kurz)
            a.setStatusTip(f'Den Kasten „{kurz}“ ein- oder ausblenden')
            a.setToolTip(a.statusTip())
            setattr(self, f"act_dock_{name}", a)

        # Default panel state on a fresh start: keyboard OFF, settings panel ON
        # (raised to the front of the tab group).  A saved config overrides this
        # via the restored dock state.
        self.keyboard_dock.hide()
        self.settings_dock.show()
        self.settings_dock.raise_()

        # Startaufteilung: rechte Spalte (Laufwerke) so schmal wie möglich, ohne
        # horizontales Scrollen; Bildschirm breit.  Tastatur auf Minimalhöhe.
        self._drives_width = self.drives_widget.sizeHint().width() + 28  # + Scrollbar
        self.resizeDocks([self.screen_dock, self.drives_dock],
                         [900, self._drives_width], Qt.Horizontal)
        self._shrink_keyboard()

        # ── Tastaturfokus gehört dem emulierten Rechner ──────────────────────
        # Alle Bedienelemente auf NoFocus (ein Klick auf Power/Mount/… bedient
        # sie, nimmt dem Bildschirm aber nicht die Tastatur weg) und ein
        # Wächter, der den Fokus nach Klicks/Fensterwechsel zurückholt — sonst
        # müsste man vor jeder Eingabe erst wieder in die Röhre klicken.
        release_focus(self)
        self._focus_guard = ScreenFocusGuard(self, self.screen_widget)

        # The screen must hold focus to receive F11 / Esc and host keystrokes.
        self.screen_widget.setFocus()

    # ── On-screen keyboard → emulator ────────────────────────────────────────

    def _on_kbd_press(self, keycode: int, shift: bool, ctrl: bool):
        self.emulator.key_press(keycode, shift, ctrl)

    def _on_kbd_release(self, keycode: int):
        self.emulator.key_release(keycode)

    # ── Seiten-Docks minimal halten (Bildschirm bekommt den Rest) ────────────

    def _kasten_sichtbarkeit(self):
        """Ein Kasten wurde ein- oder ausgeblendet.

        Das ordnet die Nachbarn um, ist aber kein Zug an einer Trennlinie —
        deshalb die Marke, die :meth:`eventFilter` davon abhält, es als
        Anordnung des Anwenders zu werten.  Und wenn die Startaufteilung noch
        gilt (kein gespeichertes Layout, nichts von Hand gezogen), bekommt die
        frisch eingeblendete Tastatur gleich ihre Inhaltshöhe.
        """
        self._layout_laeuft = True
        QTimer.singleShot(0, self._shrink_keyboard)
        QTimer.singleShot(0, lambda: setattr(self, "_layout_laeuft", False))
        self._schedule_autosave()

    def _tastatur_einpassen(self):
        """Die Tastatur auf die Höhe bringen, die ihre jetzige Breite verlangt.

        Die Nachbildung hält ihr Seitenverhältnis: ist ihr Kasten höher als
        nötig, bleiben oben und unten schwarze Balken stehen, ist er niedriger,
        wird das Tastenfeld kleiner gezeichnet, als der Platz hergäbe.  Beim
        **Umdocken** ändert sich die Breite sprunghaft (linke Spalte ↔ schmale
        rechte Spalte), die alte Höhe passt dann nicht mehr — deshalb hier neu
        eingepasst, auch wenn der Anwender die Aufteilung sonst schon selbst in
        der Hand hat (:attr:`_nutzer_layout`).  Wer es anders will, zieht die
        Trennlinie; das geht jetzt wieder.
        """
        dock = getattr(self, "keyboard_dock", None)
        if dock is None or not dock.isVisible() or dock.isFloating():
            return
        kw = self.keyboard_widget
        if kw.width() <= 0:
            return
        hoehe = kw.heightForWidth(kw.width()) + (dock.height() - kw.height())
        self._layout_laeuft = True
        self.resizeDocks([dock], [max(1, hoehe)], Qt.Vertical)
        QTimer.singleShot(0, lambda: setattr(self, "_layout_laeuft", False))

    def _shrink_keyboard(self):
        """Tastatur auf Minimalhöhe, Laufwerke/Einstellungen auf Minimalbreite —
        der Bildschirm füllt den ganzen Rest.

        Das ist die **Startaufteilung** für den Fall, dass es noch keine
        gespeicherte gibt.  Sie tritt in zwei Fällen zurück:

        * ein gespeichertes Layout wurde wiederhergestellt
          (:attr:`_has_saved_layout`) — dann gelten die Größen aus der
          Konfiguration;
        * **der Anwender hat selbst eine Trennlinie gezogen**
          (:attr:`_nutzer_layout`, gesetzt in :meth:`eventFilter`).  Ohne das
          holte der nächste Fenster-Resize die Tastatur wieder auf ihre
          Minimalhöhe zurück — die waagerechte Trennlinie liess sich dann
          scheinbar gar nicht verschieben.  Der Zug wird jetzt ohnehin
          gespeichert, es gibt also nichts mehr zurechtzurücken.
        """
        if self._has_saved_layout or self._nutzer_layout:
            return
        # Während dieser Umbau läuft, sind die Größenänderungen der Kästen von
        # UNS — sie dürfen nicht als Zug des Anwenders gelten.  Zurückgesetzt
        # wird die Marke erst eine Runde der Ereignisschleife später: Qt stellt
        # das Layout verzögert zu, die Größenänderungen kämen sonst erst nach
        # dem Zurücksetzen an und gälten als Zug des Anwenders.
        self._layout_laeuft = True
        self._startaufteilung()
        QTimer.singleShot(0, lambda: setattr(self, "_layout_laeuft", False))

    def _startaufteilung(self):
        """Der eigentliche Umbau (siehe :meth:`_shrink_keyboard`).

        Gesetzt wird jeweils nur die Größe des SEITENKASTENS; den Rest verteilt
        Qt auf seine Nachbarn.  Früher stand hier das Paar (Bildschirm, Kasten) —
        das setzt voraus, dass beide nebeneinander liegen, und ging schief,
        sobald der Anwender die Tastatur woanders andockte (etwa unter die
        Laufwerke): dann wurde der Bildschirm gestreckt und der wirkliche
        Nachbar zusammengedrückt.
        """
        # Tastatur auf Inhaltshöhe: sie ist eine maßstäbliche Nachbildung, die
        # nötige Höhe hängt an der Breite, die ihr Kasten ihr lässt.
        if (getattr(self, "keyboard_dock", None) is not None
                and self.keyboard_dock.isVisible()
                and not self.keyboard_dock.isFloating()):
            kw = self.keyboard_widget
            h = (kw.heightForWidth(kw.width()) if kw.width() > 0
                 else kw.minimumSizeHint().height())
            self.resizeDocks([self.keyboard_dock], [h], Qt.Vertical)

        # Laufwerke/Einstellungen auf schmalste Breite ohne horizontales Rollen.
        w = getattr(self, "_drives_width", 0)
        if w:
            schmal = [d for d in (self.drives_dock, self.settings_dock)
                      if d is not None and d.isVisible() and not d.isFloating()]
            if schmal:
                self.resizeDocks(schmal, [w] * len(schmal), Qt.Horizontal)

    def changeEvent(self, event):
        """Den Wechsel maximiert ⇄ normal mitschreiben.

        Der Zustandswechsel kommt NACH dem `resizeEvent` — beim Maximieren hätte
        dieses die „normale" Größe sonst mit der maximierten überschrieben.  Hier
        liegt die alte Größe noch vor, also wird sie beim Übergang ins
        Maximierte aus ``oldState`` heraus festgehalten.
        """
        if event.type() == QEvent.WindowStateChange:
            vorher = event.oldState()
            jetzt = self.windowState()
            wurde_gross = (jetzt & (Qt.WindowMaximized | Qt.WindowFullScreen)
                           and not vorher & (Qt.WindowMaximized | Qt.WindowFullScreen))
            if wurde_gross:
                # `normalGeometry()` ist genau die Größe vor dem Maximieren.
                normal = self.normalGeometry().size()
                if normal.isValid() and not normal.isEmpty():
                    self._normal_size = normal
            self._schedule_autosave()
        super().changeEvent(event)

    def eventFilter(self, obj, event):
        """Grössenänderungen der Kästen mitschreiben.

        Das Ziehen einer Trennlinie erzeugt kein Fenster-`resizeEvent` und kein
        Signal — ohne diesen Filter wäre die Aufteilung beim nächsten Start
        wieder die alte.
        """
        if (event.type() == QEvent.Resize
                and obj in (getattr(self, "screen_dock", None),
                            getattr(self, "keyboard_dock", None),
                            getattr(self, "drives_dock", None),
                            getattr(self, "settings_dock", None))):
            if not self._layout_laeuft:
                # Nicht von uns, also vom Anwender: ab jetzt rückt die
                # Startaufteilung nichts mehr zurecht.
                self._nutzer_layout = True
            self._schedule_autosave()
        return super().eventFilter(obj, event)

    def resizeEvent(self, event):
        # Die Marke VOR dem Layout-Durchlauf: die Kästen wachsen mit dem Fenster,
        # und das ist keine Anordnung des Anwenders (siehe :meth:`eventFilter`).
        self._layout_laeuft = True
        super().resizeEvent(event)
        # Nach dem Layout-Durchlauf ausführen, sonst überschreibt QMainWindow es.
        QTimer.singleShot(0, self._shrink_keyboard)
        QTimer.singleShot(0, lambda: setattr(self, "_layout_laeuft", False))
        # Merke die "normale" Fenstergröße (kein Vollbild/Maximiert) und persistiere
        # sie, damit sie beim nächsten Start wiederhergestellt wird.
        if (not getattr(self, "_fullscreen", False)
                and not self.isMaximized() and not self.isMinimized()):
            self._normal_size = self.size()
        self._schedule_autosave()
    
    def setup_menus(self):
        """Menüleiste — sie zeigt dieselben Aktionen wie die Symbolleiste.

        **Im Menü steht alles.** Die Leiste ist die Abkürzung für die häufigen
        Wege und darf deshalb ausgeblendet oder leergeräumt werden, ohne dass
        etwas unerreichbar wird.
        """
        menu_bar = self.menuBar()

        # ── Datei ────────────────────────────────────────────────────────────
        file_menu = menu_bar.addMenu("&Datei")
        # Einlegen und Auswerfen tragen je ein Untermenü mit den bestückten
        # Laufwerken; gefüllt wird es beim Aufklappen (_disk_menue_fuellen),
        # denn welche Laufwerke es gibt, ändert sich mit der Bestückung.
        for aktion in (self.act_einlegen, self.act_auswerfen):
            menue = QMenu(self)
            aktion.setMenu(menue)
            file_menu.addAction(aktion)
        self.act_einlegen.menu().aboutToShow.connect(
            lambda: self._disk_menue_fuellen(self.act_einlegen.menu(), einlegen=True))
        self.act_auswerfen.menu().aboutToShow.connect(
            lambda: self._disk_menue_fuellen(self.act_auswerfen.menu(), einlegen=False))

        file_menu.addSeparator()
        file_menu.addAction(self.act_konfig_laden)
        file_menu.addAction(self.act_konfig_speichern)
        file_menu.addSeparator()
        file_menu.addAction(self.act_beenden)

        # ── Maschine ─────────────────────────────────────────────────────────
        emu_menu = menu_bar.addMenu("&Maschine")
        emu_menu.addAction(self.act_power)
        emu_menu.addAction(self.act_reset)

        # (Die Geschwindigkeit wird im Einstellungen-Kasten, Reiter „Allgemein",
        #  über ein Dropdown eingestellt; gemessen steht sie in der Statuszeile.)

        # ── Ansicht ──────────────────────────────────────────────────────────
        view_menu = menu_bar.addMenu("&Ansicht")
        view_menu.addAction(self.act_vollbild)
        view_menu.addSeparator()
        for name in ("screen", "keyboard", "drives", "settings"):
            view_menu.addAction(getattr(self, f"act_dock_{name}"))
        view_menu.addSeparator()

        # Die Symbolleiste ein- und ausblenden — der Schalter kommt von ihr selbst.
        self.act_leiste_zeigen = self.controls_bar.toggleViewAction()
        self.act_leiste_zeigen.setText("&Symbolleiste")
        self.act_leiste_zeigen.setStatusTip(
            "Die Symbolleiste ein- oder ausblenden; im Menü bleibt alles erreichbar")
        view_menu.addAction(self.act_leiste_zeigen)
        self.act_leiste_zeigen.toggled.connect(lambda *_: self._schedule_autosave())

        stil_menue = view_menu.addMenu("Symbolleisten&stil")
        self.gruppe_stil = QActionGroup(self)
        self.gruppe_stil.setExclusive(True)
        for text, stil in (("Nur Symbole", Qt.ToolButtonIconOnly),
                           ("Symbole und Text", Qt.ToolButtonTextUnderIcon),
                           ("Text neben dem Symbol", Qt.ToolButtonTextBesideIcon),
                           ("Nur Text", Qt.ToolButtonTextOnly)):
            a = QAction(text, self)
            a.setCheckable(True)
            a.setData(int(stil.value))
            a.triggered.connect(lambda *_, st=stil: self._leistenstil(st))
            self.gruppe_stil.addAction(a)
            stil_menue.addAction(a)
            if stil == self.controls_bar.toolButtonStyle():
                a.setChecked(True)
        view_menu.addAction(self.act_leiste_einrichten)
        view_menu.addSeparator()
        view_menu.addAction(self.act_standard)

        # ── Werkzeuge ────────────────────────────────────────────────────────
        # Die Nachbarprogramme derselben Installation.  Eigenes Menü, weil es
        # weder Dateien noch die Maschine betrifft: hier wird ein ZWEITES
        # Programm gestartet, das neben diesem weiterläuft.
        tools_menu = menu_bar.addMenu("&Werkzeuge")
        tools_menu.addAction(self.act_disktool)
        tools_menu.addAction(self.act_konsole)

        # ── Hilfe ────────────────────────────────────────────────────────────
        help_menu = menu_bar.addMenu("&Hilfe")
        help_menu.addAction(self.act_hilfe)
        help_menu.addAction(self.act_ueber)

    # ── Diskettenmenüs (je bestücktem Laufwerk ein Eintrag) ──────────────────

    def _disk_menue_fuellen(self, menue, einlegen: bool):
        """„Diskette einlegen/auswerfen" mit den vorhandenen Laufwerken füllen.

        Beim Aufklappen, nicht beim Bauen: die Bestückung ändert sich über
        *Einstellungen ▸ Laufwerke*, und ein Menü, das ein abgemeldetes Laufwerk
        anbietet, führt in eine Fehlermeldung statt in eine Diskette.

        Gesperrt ist, was nicht geht: einlegen bei belegtem Laufwerk (erst
        auswerfen) und auswerfen bei leerem.
        """
        menue.clear()
        for drive in self.drives_widget.present_drives():
            belegt = self.drives_widget.is_mounted(drive)
            name = os.path.basename(self.drives_widget.mounted_path(drive))
            if einlegen:
                text = f"Laufwerk &{chr(ord('A') + drive)}:"
                tipp = (f"{name} liegt schon darin — erst auswerfen" if belegt
                        else "Ein Abbild (.hfe, .dmk, .img) in dieses Laufwerk legen")
            else:
                text = (f"Laufwerk &{chr(ord('A') + drive)}:  ({name})" if name
                        else f"Laufwerk &{chr(ord('A') + drive)}:")
                tipp = ("Die Diskette aus diesem Laufwerk nehmen" if belegt
                        else "Das Laufwerk ist leer")
            a = menue.addAction(text)
            a.setStatusTip(tipp)
            a.setToolTip(tipp)
            a.setEnabled(belegt != einlegen)
            a.triggered.connect(lambda *_, d=drive: self.drives_widget.toggle_mount(d))
        if menue.isEmpty():
            leer = menue.addAction("kein Laufwerk bestückt")
            leer.setEnabled(False)

    # ── Symbolleiste ─────────────────────────────────────────────────────────

    def _aktion(self, name: str):
        """Die Aktion zu einem Namen aus `app/ui/actions.py` (oder ``None``).

        Die Kastenschalter werden dabei **frisch beim Kasten geholt**, nicht aus
        einem gemerkten Verweis: PySide gibt die Hülle eines
        ``toggleViewAction()`` beim Leeren der Symbolleiste frei (das C++-Objekt
        selbst überlebt).  Ein gemerkter Verweis wäre danach tot, und das
        Neuaufbauen der Leiste scheiterte mit „Internal C++ object already
        deleted" — beim zweiten Aufbau, also erst beim Anwenden einer
        gespeicherten Konfiguration.
        """
        if name.startswith("dock_"):
            dock = getattr(self, f"{name[5:]}_dock", None)
            return dock.toggleViewAction() if dock is not None else None
        return getattr(self, f"act_{name}", None)

    def _leiste_fuellen(self, namen):
        """Die Symbolleiste aus einer Liste von Aktionsnamen neu aufbauen.

        ``None`` ist ein Trennstrich.  Eine Aktion mit Untermenü (Einlegen,
        Auswerfen) bekommt einen Knopf, der es sofort aufklappt — ein Klick, der
        nichts täte, wäre die schlechtere Antwort auf „welches Laufwerk?".
        """
        # `clear()` gäbe die Hüllen der Kastenschalter frei (s. :meth:`_aktion`);
        # einzeln entfernen lässt sie am Leben.
        for vorhanden in list(self.controls_bar.actions()):
            self.controls_bar.removeAction(vorhanden)
        for name in namen:
            if name is None:
                self.controls_bar.addSeparator()
                continue
            a = self._aktion(name)
            if a is None:
                continue          # Name aus einer älteren Konfiguration
            self.controls_bar.addAction(a)
            if a.menu() is not None:
                knopf = self.controls_bar.widgetForAction(a)
                if isinstance(knopf, QToolButton):
                    knopf.setPopupMode(QToolButton.InstantPopup)
        self._leisten_inhalt = list(namen)
        # Die Knöpfe dürfen den Tastaturfokus nicht an sich ziehen — der gehört
        # der emulierten Maschine (app/ui/focus.py).
        release_focus(self.controls_bar)

    def _leistenstil(self, stil):
        self.controls_bar.setToolButtonStyle(stil)
        self._schedule_autosave()

    def _leiste_einrichten(self):
        """Dialog: welche Schaltflächen die Leiste zeigt und in welcher Folge."""
        namen = {}
        for name in aktionen.REIHENFOLGE:
            a = name and self._aktion(name)
            if a is not None:
                namen[name] = aktionen.DIALOG_NAME.get(
                    name, a.text().replace("&", "").rstrip("…"))
        dlg = ToolbarDialog(aktionen.REIHENFOLGE, self._leisten_inhalt, namen,
                            aktionen.STANDARD, self)
        if dlg.exec():
            self._leiste_fuellen(dlg.auswahl())
            self._schedule_autosave()

    # ── Handbuch ─────────────────────────────────────────────────────────────

    def open_help(self):
        """Das Handbuch öffnen (nicht modal — man liest nach und arbeitet weiter).

        Es gibt genau eins je Fenster; ein zweites F1 holt das vorhandene nach vorn.
        """
        vorhanden = getattr(self, "_hilfe", None)
        if vorhanden is not None and vorhanden.isVisible():
            vorhanden.raise_()
            vorhanden.activateWindow()
            return vorhanden
        self._hilfe = HelpWindow(self)
        self._hilfe.show()
        return self._hilfe

    # ── Fullscreen (window-level, keeps the GL context intact) ───────────────

    def toggle_fullscreen(self, *args):
        if getattr(self, "_fullscreen", False):
            self.exit_fullscreen()
        else:
            self.enter_fullscreen()

    def _vollbild_haken(self, an: bool):
        """Den Haken an „Vollbild" nachziehen, ohne ihn erneut auszulösen.

        Die Aktion ist rastend und ruft beim Umschalten :meth:`toggle_fullscreen`;
        würde sie hier ungebremst gesetzt, schaltete sie sofort wieder zurück.
        """
        self.act_vollbild.blockSignals(True)
        self.act_vollbild.setChecked(an)
        self.act_vollbild.blockSignals(False)

    def enter_fullscreen(self):
        if getattr(self, "_fullscreen", False):
            return
        self._fullscreen = True
        # Hide all chrome and let the CRT fill the screen (the central area is
        # already 0-sized, so the visible screen dock fills the window).
        self._chrome_hidden = []
        for w in (self.menuBar(), self.controls_bar, self.statusBar(),
                  self.keyboard_dock, self.drives_dock, self.settings_dock):
            if w is not None and w.isVisible():
                self._chrome_hidden.append(w)
                w.hide()
        # Falls das Bildschirm-Dock ausgeblendet war, für Vollbild einblenden.
        self._screen_was_hidden = not self.screen_dock.isVisible()
        self.screen_dock.show()
        self.screen_dock.setFloating(False)
        self.showFullScreen()
        self._vollbild_haken(True)
        self.screen_widget.setFocus()

    def exit_fullscreen(self):
        if not getattr(self, "_fullscreen", False):
            return
        self._fullscreen = False
        self.showNormal()
        for w in getattr(self, "_chrome_hidden", []):
            w.show()
        self._chrome_hidden = []
        if getattr(self, "_screen_was_hidden", False):
            self.screen_dock.hide()
        self._vollbild_haken(False)
        self.screen_widget.setFocus()

    # ── Configuration (YAML) ─────────────────────────────────────────────────

    def _gather_config(self) -> dict:
        """Build the full configuration dict from the live application state."""
        general = {"speed": float(self.speed_factor)}
        return config_io.build_config(
            self.screen_widget.params, general, self.drives_widget.get_mounts(),
            self._gather_window_state(), drive_types=self._drive_types)

    def _gather_window_state(self) -> dict:
        """Fenstergeometrie + Kastenaufteilung (Sichtbarkeit, Lage, Größen).

        Getragen wird die Geometrie von **Qt selbst** (``geometry`` =
        base64-kodiertes :meth:`QWidget.saveGeometry`): darin stecken Größe,
        Bildschirmposition, der Zustand *maximiert* und — wichtig — die Größe des
        ZURÜCKGESETZTEN Fensters, getrennt von der maximierten.  Genau daran
        scheiterte der Versuch, das von Hand zu führen: beim Maximieren trifft
        das ``resizeEvent`` mit der neuen Größe ein, BEVOR ``isMaximized()``
        wahr wird (der Fensterverwalter meldet den Zustand erst danach), sodass
        die „normale" Größe die maximierte wurde — und die fiel beim nächsten
        Start durch die Bildschirmprüfung auf die Vorgabegröße zurück.

        ``width``/``height``/``maximized`` bleiben daneben stehen: lesbar in der
        YAML-Datei und Rückfall für eine Geometrie, die sich nicht anwenden
        lässt (anderer Bildschirm, andere Qt-Fassung).  ``dock_state`` ist
        :meth:`QMainWindow.saveState` — welche Kästen sichtbar sind, wo sie
        liegen und wie breit sie sind.
        """
        size = self._normal_size
        return {
            "geometry": bytes(self.saveGeometry().toBase64()).decode("ascii"),
            "width": int(size.width()),
            "height": int(size.height()),
            "maximized": bool(self.isMaximized()),
            "dock_state": bytes(self.saveState().toBase64()).decode("ascii"),
            # Der Inhalt der Symbolleiste steht NICHT im dock_state — Qt merkt
            # sich dort nur, wo sie liegt und ob sie sichtbar ist.  Welche
            # Schaltflächen darin stehen, ist unsere Sache.
            "toolbar": ["" if n is None else str(n) for n in self._leisten_inhalt],
            "toolbar_style": int(self.controls_bar.toolButtonStyle().value),
        }

    def _apply_window_state(self, win: dict):
        """Fenstergeometrie, Symbolleiste und Kastenaufteilung wiederherstellen.

        Reihenfolge: erst die Geometrie (:meth:`_geometrie_herstellen`), dann der
        Inhalt der Symbolleiste, zuletzt ``restoreState()`` — dieses stellt die
        Sichtbarkeit der Leiste wieder her und soll sich auf die Leiste beziehen,
        die der Anwender zusammengestellt hat.
        """
        if not isinstance(win, dict):
            return

        self._geometrie_herstellen(win)

        # Erst der Leisteninhalt, dann das Layout: `restoreState()` stellt die
        # Sichtbarkeit der Leiste wieder her, und die soll sich auf die Leiste
        # beziehen, die der Anwender zusammengestellt hat.
        leiste = win.get("toolbar")
        if isinstance(leiste, list):
            self._leiste_fuellen([n if n else None for n in leiste])
        stil = win.get("toolbar_style")
        if stil is not None:
            try:
                self.controls_bar.setToolButtonStyle(Qt.ToolButtonStyle(int(stil)))
                for a in self.gruppe_stil.actions():
                    a.setChecked(a.data() == int(stil))
            except (TypeError, ValueError):
                pass

        state = win.get("dock_state")
        if state:
            try:
                if self.restoreState(QByteArray.fromBase64(state.encode("ascii"))):
                    self._has_saved_layout = True
            except Exception:
                pass

    def _geometrie_herstellen(self, win: dict):
        """Größe, Lage und „maximiert" aus dem ``window``-Abschnitt herstellen.

        **Erste Wahl ist Qts eigene Geometrie** (``geometry`` =
        :meth:`QWidget.saveGeometry`): darin stecken Größe, Bildschirmposition,
        der Zustand *maximiert* und die Größe des zurückgesetzten Fensters — und
        sie wirkt auf dem noch unsichtbaren Fenster, sodass es gleich richtig
        aufgeht.  Qt prüft dabei selbst, ob die Lage auf einen heute vorhandenen
        Bildschirm fällt.

        Der Rückfall (ältere Konfiguration, oder die Geometrie ließ sich nicht
        anwenden) setzt die Größe von Hand und maximiert über den
        **Fensterzustand** — nicht über ``showMaximized()``, das ein noch
        unsichtbares Fenster anzeigen würde.
        """
        geo = win.get("geometry")
        if geo:
            try:
                if self.restoreGeometry(QByteArray.fromBase64(geo.encode("ascii"))):
                    normal = self.normalGeometry().size()
                    self._normal_size = (normal if normal.isValid()
                                         and not normal.isEmpty() else self.size())
                    self._maximiert_herstellen(win)
                    return
            except Exception:
                pass

        w = int(win.get("width", self.DEFAULT_WIDTH))
        h = int(win.get("height", self.DEFAULT_HEIGHT))

        screen = self.screen() or QGuiApplication.primaryScreen()
        if screen is not None:
            avail = screen.availableGeometry()
            if w > avail.width() or h > avail.height():
                w, h = self.DEFAULT_WIDTH, self.DEFAULT_HEIGHT
        self.resize(w, h)
        self._normal_size = QSize(w, h)
        self._maximiert_herstellen(win)

    def _maximiert_herstellen(self, win: dict):
        """„Maximiert" wiederherstellen — aus UNSEREM Feld und NACH dem Anzeigen.

        Zwei Dinge stehen dem im Weg, und beide zusammen liessen ein maximiert
        beendetes Fenster im Fenstermodus wieder aufgehen:

        1. **Qts Geometrieblock trägt den Zustand nicht zurück.**
           ``saveGeometry`` schreibt ihn zwar mit, ``restoreGeometry`` meldet
           Erfolg und stellt die Größe her — ``isMaximized()`` bleibt danach
           trotzdem falsch (nachgemessen mit Qt 6.11 unter X11).  Also
           entscheidet hier **immer** unser eigenes ``maximized``-Feld, und der
           Block liefert nur noch Größe und Lage.
        2. **Vor dem Anzeigen verfällt der Zustand.**  Ein
           ``setWindowState(WindowMaximized)`` auf dem noch unsichtbaren Fenster
           — und ebenso eines aus ``showEvent`` heraus — wird vom
           Fensterverwalter verworfen (GNOME Shell/X11: das Fenster geht in
           seiner normalen Größe auf, und der Zustand fällt binnen 200 ms auf
           „normal" zurück).  Erst ein Zug **nach** dem Anzeigen, aus der
           Ereignisschleife heraus, hält.  Der Wunsch wird deshalb gemerkt und
           von :meth:`showEvent` nachgeholt.

        Dass es lange unbemerkt blieb, liegt an der Prüfung: unter
        ``QT_QPA_PLATFORM=offscreen`` scheitert ``restoreGeometry`` (der
        gespeicherte Bildschirm passt nicht), und der Rückfall setzte den
        Zustand von Hand auf einem Fenster, das der Test schon angezeigt hatte —
        der Wächter lief also durch beide Zweige NICHT, die der Anwender geht.
        """
        maximiert = bool(win.get("maximized", False))
        if not self.isVisible():
            # Den Zustand am unsichtbaren Fenster AUSDRÜCKLICH löschen und den
            # Wunsch merken.  Ohne das Löschen bliebe die Marke stehen, die
            # ``restoreGeometry`` hinterlässt — und die ist eine Lüge: das
            # Fenster geht trotzdem normal auf, aber der nachgeholte Zug hielte
            # sie für schon erfüllt und täte nichts.  Erst ein WECHSEL am
            # sichtbaren Fenster erreicht den Fensterverwalter.
            self.setWindowState(self.windowState() & ~Qt.WindowMaximized)
            self._maximiert_nachholen = maximiert
            return
        self._maximiert_setzen(maximiert)

    def _maximiert_setzen(self, an: bool):
        """Den Fensterzustand *maximiert* setzen oder lösen (ohne Umweg über show)."""
        if an == self.isMaximized():
            return
        if an:
            self.setWindowState(self.windowState() | Qt.WindowMaximized)
        else:
            self.setWindowState(self.windowState() & ~Qt.WindowMaximized)

    def showEvent(self, event):
        """Beim ERSTEN Anzeigen das gemerkte „maximiert" nachholen.

        Nicht hier direkt, sondern eine Runde der Ereignisschleife später: zum
        Zeitpunkt des ``showEvent`` ist das Fenster noch nicht auf dem Schirm,
        und ein Zustandswechsel geht dann verloren (siehe
        :meth:`_maximiert_herstellen`).
        """
        super().showEvent(event)
        wunsch = getattr(self, "_maximiert_nachholen", None)
        if wunsch is not None:
            self._maximiert_nachholen = None
            QTimer.singleShot(0, lambda: self._maximiert_setzen(wunsch))

    def _mount_cli_disks(self, disks):
        """Diskettenargumente der Kommandozeile einlegen (A:, B:, C:, D:)."""
        if not disks:
            return
        for drive, path in enumerate(disks[:4]):
            if not path:
                continue
            if not self.drives_widget.mount_path(drive, str(path)):
                # Kein Abbruch: die Oberflaeche laeuft, das Laufwerk bleibt leer.
                print(f"a5120emu: '{path}' konnte nicht in Laufwerk "
                      f"{chr(ord('A') + drive)}: eingelegt werden", file=sys.stderr)

    def _apply_config(self, data: dict):
        """Apply a loaded configuration to the running application.

        Applies the CRT look and speed, and (re)mounts the stored disks.  Guards
        against triggering an autosave loop while values are being restored.

        **Ein FEHLENDER Abschnitt heisst „nicht anfassen", ein leerer „leeren".**
        Das betrifft die Disketten: ``disks: []`` wirft alles aus, ein gar nicht
        vorhandenes ``disks`` lässt die Laufwerke, wie sie sind.  Genau davon
        lebt die Auslieferungskonfiguration (`app/config_io.py`), die keine
        Diskettenpfade trägt — *Ansicht ▸ Standard zurücksetzen* stellt damit
        die Ansicht her, ohne die Maschine leerzuräumen.
        """
        self._loading_config = True
        try:
            self.screen_widget.params.update_from_dict(data.get("crt", {}))
            self.settings_widget.refresh_all()
            self.screen_widget.update()

            general = data.get("general") or {}
            speed = float(general.get("speed", 1.0))
            self.settings_widget.set_speed_value(speed)
            self._apply_speed(speed)

            # Drive-bay configuration must be applied BEFORE the disks, so the
            # panels for the present slots exist and the machine matches.  During
            # a config restore we never cold-restart here (power-on happens later).
            self._apply_drive_types(
                data.get("drive_types") or dt.DEFAULT_DRIVE_TYPES, cold_restart=False)

            if "disks" in data:
                self.drives_widget.load_mounts(data.get("disks") or [])

            if "window" in data:
                self._apply_window_state(data.get("window") or {})
        finally:
            self._loading_config = False

    def _load_or_create_default_config(self):
        """Die Konfiguration des Anwenders laden — oder die Auslieferung nehmen.

        Beim ersten Start (nach der Erstinstallation) gibt es noch keine
        ``config.yaml``.  Dann kommt die **mitgelieferte**
        Auslieferungskonfiguration zum Zug (``data/default_config.yaml``, siehe
        :func:`app.config_io.standard_konfiguration`) und wird gleich als die
        neue ``config.yaml`` des Anwenders geschrieben — von da an gehört sie
        ihm und wird fortgeschrieben.

        Findet sich auch die nicht (unvollständige Installation), bleibt es bei
        den im Programm eingebauten Vorgaben; geschrieben wird die Datei
        trotzdem, damit es ab jetzt eine gibt.
        """
        path = config_io.default_config_path()
        if os.path.exists(path):
            try:
                self._apply_config(config_io.load_config(path))
                return
            except Exception as e:
                QMessageBox.warning(
                    self, "Konfiguration",
                    f"Konnte {path} nicht laden:\n{e}\n\nStandardwerte werden verwendet.")
        self._apply_config(config_io.standard_konfiguration())
        try:
            config_io.save_config(path, self._gather_config())
        except Exception as e:
            QMessageBox.warning(self, "Konfiguration",
                                f"Konnte Standard-Konfiguration nicht anlegen:\n{e}")

    def _standard_zuruecksetzen(self):
        """*Ansicht ▸ Standard zurücksetzen* — zurück zur Auslieferung.

        Stellt Bildröhre, Tempo, Laufwerksbestückung, Fenstergröße,
        Kastenaufteilung und Symbolleiste so her, wie der Emulator nach der
        Erstinstallation aussieht, und **überschreibt damit die gespeicherte
        Konfiguration** — deshalb die Rückfrage.

        Die eingelegten Disketten bleiben liegen: die Auslieferungskonfiguration
        trägt keinen ``disks``-Abschnitt (siehe :meth:`_apply_config`), und ein
        Zurücksetzen der Ansicht soll die Maschine nicht leerräumen.
        """
        vorgabe = config_io.standard_konfiguration()
        if not vorgabe:
            QMessageBox.warning(
                self, "Standard zurücksetzen",
                "Die mitgelieferte Standard-Konfiguration wurde nicht gefunden.\n\n"
                "Gesucht wurde:\n" + "\n".join(
                    str(p) for p in paths.default_config_candidates()))
            return
        if QMessageBox.question(
                self, "Standard zurücksetzen",
                "Bildröhre, Tempo, Laufwerksbestückung, Fenstergröße, Kästen und "
                "Symbolleiste auf die Auslieferung zurücksetzen?\n\n"
                "Die gespeicherte Konfiguration wird dabei überschrieben; die "
                "eingelegten Disketten bleiben liegen.  Ändert sich dabei die "
                "Laufwerksbestückung, startet die Maschine kalt.",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No) != QMessageBox.Yes:
            return
        vorher = list(self._drive_types)
        self._apply_config(vorgabe)
        # Ein geänderter Laufwerksschacht bedeutet eine NEUE Maschine
        # (:meth:`_apply_drive_types`), und die ist noch nicht eingeschaltet:
        # beim Wiederherstellen kommt das Einschalten sonst später von selbst,
        # hier läuft die alte schon.  Ohne Bestückungswechsel bleibt die Maschine
        # in Ruhe — ein Zurücksetzen der Ansicht soll kein CP/A abwürgen.
        if self._drive_types != vorher:
            self._cold_restart()
        # Sofort schreiben, nicht über den sammelnden Autosave: der Anwender hat
        # das Überschreiben eben bestätigt, es darf nicht an einem Absturz in den
        # nächsten 400 ms hängen.
        self._autosave_now()
        self.statusBar().showMessage("Standard-Konfiguration wiederhergestellt.", 4000)

    def _schedule_autosave(self):
        """Queue a debounced write of the current config to the default path."""
        if self._loading_config or getattr(self, "_autosave_timer", None) is None:
            return
        self._autosave_timer.start()  # restarts the single-shot timer

    def _autosave_now(self):
        """Write the current configuration to the default config path."""
        try:
            config_io.save_config(config_io.default_config_path(), self._gather_config())
        except Exception as e:
            # Never let a persistence hiccup take down the UI.
            print(f"[config] Auto-Speichern fehlgeschlagen: {e}")

    def _on_save_config(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Configuration", "",
            "YAML-Konfiguration (*.yaml *.yml)")
        if not path:
            return
        if "." not in path.rsplit("/", 1)[-1]:
            path += ".yaml"
        try:
            config_io.save_config(path, self._gather_config())
        except Exception as e:
            QMessageBox.critical(self, "Save Configuration", str(e))

    def _on_load_config(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Load Configuration", "",
            "YAML-Konfiguration (*.yaml *.yml)")
        if not path:
            return
        try:
            data = config_io.load_config(path)
            self._apply_config(data)
            # Cold-start so the newly mounted disks actually boot.
            self._cold_restart()
            # The loaded config is immediately adopted as the default config.
            self._autosave_now()
        except Exception as e:
            QMessageBox.critical(self, "Load Configuration", str(e))

    def _cold_restart(self):
        """Restart the machine from the boot ROM with the current disks (if on)."""
        if not self._emu_started or not self.act_power.isChecked():
            return
        self.drives_widget.remount_all()
        self.emulator.power_on()
        self.cycles = 0
        self.frame_count = 0

    def _on_power_toggle(self, checked):
        """Netzschalter.

        EIN = Kaltstart (Boot-ROM neu aktiviert, ZVE1-Reset), Zykluszähler
        zurückgesetzt, Bildschirm an.  AUS = Röhre dunkel, Emulatorlauf
        angehalten.  Die Beschriftung im Menü sagt, was ein Klick TUT; das
        Kurzwort in der Leiste bleibt „Power", der Knopf zeigt den Zustand
        durch seine Rastung.
        """
        self.act_power.setText(aktionen.POWER_TEXT[bool(checked)])
        if checked:
            # Kaltstart: Images neu mounten (setzt den K5122-Laufwerkszustand
            # zurück — sonst bootet das Boot-ROM nicht neu), dann power_on().
            self.drives_widget.remount_all()
            self.emulator.power_on()
            self.cycles = 0
            self.frame_count = 0
            self.run_timer.start()
            self.screen_widget.set_powered(True)
            self.keyboard_widget.set_powered(True)
            self.statusBar().showMessage("Kaltstart — die Maschine läuft an.", 4000)
        else:
            self.run_timer.stop()
            self.screen_widget.set_powered(False)
            # Betriebsanzeige der Tastatur aus, Funktionsanzeigen mit ihr.
            self.keyboard_widget.set_powered(False)
            self.keyboard_widget.set_leds(0)
            self.statusBar().showMessage("Ausgeschaltet.", 4000)
        # Statusanzeige sofort aktualisieren (Tempo eingefroren bzw. auf „—").
        self._update_status()

    def _on_reset(self):
        """Reset emulator (Neustart vom Boot-ROM)."""
        # Wie beim Kaltstart: Images neu mounten setzt den K5122-Laufwerks-
        # zustand zurück, sonst läuft der Boot-ROM-Neustart nicht an.
        self.drives_widget.remount_all()
        self.emulator.reset()
        self.cycles = 0
        self.statusBar().showMessage("Rückgestellt — Neustart vom Boot-ROM.", 4000)

    
    def _cycles_per_frame(self) -> int:
        """CPU cycles to run per timer tick.

        For a normal speed factor this is real-time * factor.  For unlimited
        (factor 0.0) the run_timer fires with interval 0, so one real-time
        frame worth of cycles per tick already fast-forwards as hard as the host
        allows while keeping the Qt event loop responsive."""
        factor = self.speed_factor if self.speed_factor > 0.0 else 1.0
        return int(self.CPU_HZ * self.frame_interval_ms / 1000 * factor)

    def _run_emulator(self):
        """Run emulator for one frame."""
        try:
            cycles = self.emulator.run(self._cycles_per_frame())
            self.cycles += cycles
            self.frame_count += 1
            # Die Tastaturanzeigen hängen am Kommandostrom zur Tastatur, ändern
            # sich also mitten im Lauf; einmal je Bild abholen (die
            # Bildschirmtastatur zeichnet nur bei echter Änderung neu).
            if self.keyboard_dock.isVisible():
                self.keyboard_widget.set_leds(self.emulator.keyboard_leds())
        except Exception as e:
            self._on_error(f"Emulator error: {e}")
    
    def _update_status(self):
        """Statuszeile nachführen: eingestellter Takt und Laufwerke.

        Angezeigt wird der EINGESTELLTE Takt — wortgleich mit dem Auswahlfeld.
        Der tatsächlich erreichte wird zwar weiter gemessen (gelaufene Zyklen je
        verstrichener Sekunde Wanduhr, bezogen auf den Nenntakt), steht aber nur
        im Tooltip: als Anzeige schwankte er von Sekunde zu Sekunde und las sich
        wie ein Fehler, wo keiner war.
        """
        jetzt = time.monotonic()
        vergangen = jetzt - self._tempo_zeit
        gelaufen = self.cycles - self._tempo_cycles
        self._tempo_zeit, self._tempo_cycles = jetzt, self.cycles
        self.frame_count = 0

        laeuft = self._emu_started and self.act_power.isChecked()
        gemessen = (gelaufen / vergangen / self.CPU_HZ
                    if laeuft and vergangen > 0 else None)
        self.status_widget.set_takt(self.speed_factor if laeuft else None, gemessen)
        self._update_drive_status()

    def _update_drive_status(self):
        """Die Laufwerksfelder der Statuszeile aus der Maschine nachziehen.

        Gefragt wird der KERN (Pfad, Schreibschutz, Zugriff), nicht die
        Oberfläche: er ist die Stelle, an der die Diskette wirklich liegt.  Nur
        wofür er keine Auskunft hat — dass ein Laufwerk ein echtes am
        Greaseweazle ist —, kommt aus dem Laufwerkskasten.
        """
        for feld in self.status_widget.felder():
            drive = feld.drive
            try:
                pfad = self.emulator.disk_path(drive)
                wp = self.emulator.is_disk_write_protected(drive)
            except Exception:
                pfad, wp = "", False
            physisch = self.drives_widget.is_physical(drive)
            if not pfad and not physisch:
                # Der Kern meldet bei manchen Behältern keinen Pfad zurück; was
                # eingelegt wurde, weiss der Laufwerkskasten dann immer noch.
                pfad = self.drives_widget.mounted_path(drive)
            feld.zeige(pfad, wp, physisch=physisch)
        self._update_drive_lamps()

    def _update_drive_lamps(self):
        """Nur die Leuchten — im 120-ms-Takt, damit ein Zugriff sichtbar wird.

        Dieselbe Quelle wie die Leuchte im Laufwerkskasten (`is_disk_led_on`) —
        und dieselbe Aussage: **der Zugriff zaehlt, nicht der Inhalt**.  Ein
        angesprochenes LEERES Laufwerk leuchtet deshalb genauso rot wie ein
        belegtes.  Das ist kein Schoenheitsfehler, sondern die Auskunft, auf die
        es dann ankommt: wer sucht, warum das Gastsystem haengt, will sehen, dass
        es auf B: wartet — und dass dort nichts liegt.  Vorher blieb der Ring in
        genau diesem Fall leer und die Anzeige schwieg.

        Im Sekundentakt abgetastet blitzte hier praktisch nie etwas auf: ein
        Sektorzugriff ist in wenigen Zehntelsekunden vorbei.
        """
        for feld in self.status_widget.felder():
            drive = feld.drive
            lampe = self.status_widget.lampe(drive)
            if lampe is None:
                continue
            try:
                aktiv = self.emulator.is_disk_led_on(drive)
            except Exception:
                aktiv = False
            belegt = (self.drives_widget.is_mounted(drive)
                      or bool(self._disk_path(drive)))
            lampe.set_zustand(status_bar.ZUGRIFF if aktiv
                              else status_bar.BELEGT if belegt
                              else status_bar.LEER)

    def _disk_path(self, drive: int) -> str:
        """Pfad der eingelegten Diskette laut Kern (leer = nichts eingelegt)."""
        try:
            return self.emulator.disk_path(drive)
        except Exception:
            return ""

    
    def _on_speed_selected(self, speed: float):
        """Speed dropdown changed → apply live and persist."""
        self._apply_speed(speed)
        self._schedule_autosave()

    def _apply_speed(self, speed: float):
        """Set emulation speed. 1.0 = real time, >1.0 = fast-forward, 0.0 = unlimited.

        Unlimited runs the timer with a 0 ms interval (as fast as the host allows)
        instead of a blocking loop, so the UI stays responsive."""
        self.speed_factor = float(speed)
        self.run_timer.setInterval(0 if self.speed_factor == 0.0
                                   else self.frame_interval_ms)
        # Die Statuszeile nennt den eingestellten Takt — sie darf nicht bis zum
        # nächsten Sekundentakt hinterherhinken.
        if getattr(self, "status_widget", None) is not None:
            self.status_widget.set_takt(
                self.speed_factor
                if self._emu_started and self.act_power.isChecked() else None)
        if self._emu_started and self.act_power.isChecked():
            self.run_timer.start()

    def _on_drive_types_selected(self, types: list):
        """A drive-type dropdown changed → rebuild the machine and persist."""
        self._apply_drive_types(types, cold_restart=True)
        self._schedule_autosave()

    def _apply_drive_types(self, types: list, cold_restart: bool):
        """Adopt a new drive-bay configuration.

        The core sets the per-slot ``DriveProfile`` at construction time, so a
        changed bay means a **fresh machine**: the emulator is recreated with the
        new profiles, every reference is rewired, the drive panels are rebuilt and
        the disks that still have a drive are remounted.  With *cold_restart* the
        new machine is powered on immediately (boot ROM restarts); during a config
        restore it is ``False`` (power-on happens once, later).
        """
        types = dt.normalize_list(types)

        # Disks whose slot still carries a drive survive the reconfiguration.
        surviving = [m for m in self.drives_widget.get_mounts()
                     if 0 <= int(m.get("drive", -1)) < dt.NUM_SLOTS
                     and dt.is_present(types[int(m["drive"])])]

        # Recreate the machine with the new drive bay.
        try:
            new_emu = K1520Emulator(types)
        except Exception as e:
            QMessageBox.critical(self, "Laufwerke",
                                 f"Konnte Maschine nicht neu erzeugen:\n{e}")
            # Keep the settings dropdowns consistent with the machine still in use.
            self.settings_widget.set_drive_types(self._drive_types)
            return

        try:
            self.emulator.stop()
        except Exception:
            pass

        self._drive_types = types
        self.emulator = new_emu
        self.screen_widget.set_emulator(new_emu)
        self.drives_widget.set_drive_types(types, new_emu)  # rebuild panels, clear mounts
        self.drives_widget.load_mounts(surviving)           # remount into new machine
        self.settings_widget.set_drive_types(types)         # keep dropdowns in sync (no re-emit)
        # Die Statuszeile führt je bestücktem Steckplatz ein Feld — ein
        # abgemeldetes Laufwerk muss auch dort verschwinden.
        self.status_widget.set_drive_types(types)
        self._update_drive_status()

        if cold_restart and self._emu_started and self.act_power.isChecked():
            self.emulator.power_on()
            self.cycles = 0
            self.frame_count = 0
            self.run_timer.start()
            self.screen_widget.set_powered(True)

    def _on_error(self, message: str):
        """Handle error."""
        QMessageBox.critical(self, "Error", message)
        self.emulator.stop()
        self.run_timer.stop()
        self.act_power.setChecked(False)
    
    # ── Nachbarprogramme ────────────────────────────────────────────────────

    def _disktool_starten(self):
        """Das k1520DiskTool als eigenständiges Programm daneben öffnen.

        Bewusst ein zweiter Prozess und kein zweites Fenster in diesem hier: die
        beiden benutzen verschiedene Bibliotheken (``libk1520core`` gegen
        ``libk1520disk``), und ein hängendes Diskettenwerkzeug soll die laufende
        Maschine nicht mitreissen.
        """
        try:
            programme.programm_starten(programme.DISKTOOL)
        except RuntimeError as e:
            QMessageBox.warning(self, "k1520DiskTool", str(e))

    def _konsole_starten(self):
        """Ein Konsolenfenster mit den K1520-Kommandozeilenwerkzeugen öffnen.

        Der Debugger mountet seine Diskette standardmässig als Kopie
        (Copy-on-Write, siehe ``tools/k1520dbg.md`` §1), eine hier eingelegte
        Diskette nimmt also keinen Schaden, wenn sie dort zugleich offen ist.
        """
        try:
            programme.konsole_starten()
        except RuntimeError as e:
            QMessageBox.warning(self, "Werkzeugkonsole", str(e))

    def _on_about(self):
        """Fassung und Herkunft — die Fassung kommt aus der Bibliothek selbst."""
        from app.core_binding.k1520 import K1520Emulator as _E
        try:
            fassung = _E.version()
        except Exception:
            fassung = "unbekannt"
        QMessageBox.about(
            self, "Über a5120emu",
            f"<h3>a5120emu</h3>"
            f"<p>Emulator des Bürocomputers <b>robotron A5120</b> am K1520-Bus — "
            f"Karten und Bus werden nachgebildet, der Z80-Code von Boot-ROM, "
            f"BIOS und Betriebssystem läuft unverändert.</p>"
            f"<p>Bibliothek: {fassung}</p>"
            f"<p>Das Handbuch steht unter <i>Hilfe ▸ Handbuch</i> (F1).</p>")
    
    def closeEvent(self, event):
        """Cleanup on close."""
        # Den Stand beim Beenden IMMER wegschreiben, nicht nur eine anstehende
        # Änderung: die letzte Aufteilung der Kästen kann von einem Ereignis
        # stammen, das kein Speichern ausgelöst hat.
        self._autosave_timer.stop()
        self._autosave_now()
        self.run_timer.stop()
        self.status_timer.stop()
        self._lamp_timer.stop()
        self.screen_widget.stop_display()
        self.emulator.stop()
        # Echte Laufwerke abmelden: ausstehende Spuren zurückschreiben und den
        # Arbeitsfaden anhalten.  Ohne das bliebe eine Änderung im Abbild liegen,
        # die auf der eingelegten Diskette nie ankäme
        # (doc/design/14_physische_diskette.md §7).
        try:
            self.drives_widget.close_physical_sessions()
        except Exception:
            pass
        event.accept()
