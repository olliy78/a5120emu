"""
K1520 Emulator - Main Window
=============================

Main application window with display, controls, and status.
"""

import sys
import os

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QLabel, QMessageBox, QDockWidget, QMenu, QFileDialog,
    QScrollArea
)
from PySide6.QtCore import Qt, QTimer, QSize, QByteArray
from PySide6.QtGui import QIcon, QKeySequence, QAction, QGuiApplication

from app.ui.screen_widget import ScreenWidget, StatusWidget
from app.ui.settings_widget import SettingsWidget
from app.ui.drive_widget import DriveWidget
from app.ui.keyboard import KeyboardWidget
from app.ui.focus import release_focus, ScreenFocusGuard
from app.core_binding.k1520 import K1520Emulator
from app import config_io
from app import drive_types as dt


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
        self.CPU_HZ = 2_450_000
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
        # Last "normal" (non-fullscreen/-maximized) window size, persisted so the
        # size is restored on the next start.
        self._normal_size = QSize(self.DEFAULT_WIDTH, self.DEFAULT_HEIGHT)

        self.run_timer = QTimer()
        self.run_timer.timeout.connect(self._run_emulator)
        self.run_timer.setInterval(self.frame_interval_ms)

        # Setup UI
        self.setup_ui()
        self.setup_menus()

        # Status updates
        self.cycles = 0
        self.frame_count = 0
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self._update_status)
        self.status_timer.start(1000)  # Update every second

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

        # Persist which panels are active (dock shown/hidden) whenever that changes.
        for dock in (self.screen_dock, self.keyboard_dock,
                     self.drives_dock, self.settings_dock):
            dock.visibilityChanged.connect(lambda *_: self._schedule_autosave())
            dock.dockLocationChanged.connect(lambda *_: self._schedule_autosave())

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

        # Cold start with the restored disks present, then begin running.
        self.emulator.power_on()
        self._emu_started = True
        self.run_timer.start()
        self.screen_widget.start_display()
    
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

        # ── Toolbar: Power / Reset ───────────────────────────────────────────
        self.controls_bar = self.addToolBar("Steuerung")
        self.controls_bar.setObjectName("controls_toolbar")
        self.controls_bar.setMovable(False)

        self.power_btn = QPushButton("Power ON")
        self.power_btn.setCheckable(True)
        self.power_btn.setChecked(True)
        self.power_btn.clicked.connect(self._on_power_toggle)
        self.controls_bar.addWidget(self.power_btn)

        self.reset_btn = QPushButton("Reset")
        self.reset_btn.clicked.connect(self._on_reset)
        self.controls_bar.addWidget(self.reset_btn)

        # ── Statusleiste ─────────────────────────────────────────────────────
        self.status_widget = StatusWidget()
        self.statusBar().addPermanentWidget(self.status_widget, 1)

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

        self.keyboard_dock = QDockWidget("Tastatur", self)
        self.keyboard_dock.setObjectName("keyboard_dock")
        self.keyboard_dock.setWidget(self.keyboard_widget)
        # Split UNTER den Bildschirm (gleicher Dock-Bereich) → verschiebbare
        # Trennlinie zwischen Bildschirm und Tastatur.
        self.addDockWidget(Qt.LeftDockWidgetArea, self.keyboard_dock)
        self.splitDockWidget(self.screen_dock, self.keyboard_dock, Qt.Vertical)

        # ── Laufwerke-Dock (rechts, scrollbar) ───────────────────────────────
        self.drives_dock = QDockWidget("Disk Drives", self)
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

    def _shrink_keyboard(self):
        """Tastatur auf Minimalhöhe, Laufwerke/Einstellungen auf Minimalbreite —
        der Bildschirm füllt den ganzen Rest.

        Wird beim Start und nach jedem Fenster-Resize (auch Maximieren)
        aufgerufen.  Zwischendurch lassen sich die Trennlinien weiterhin von Hand
        verschieben; der nächste Resize setzt wieder auf minimal.

        Sobald ein gespeichertes Layout wiederhergestellt wurde
        (:attr:`_has_saved_layout`), tritt diese Auto-Anpassung zurück, damit die
        in der Config abgelegten Widget-Größen erhalten bleiben.
        """
        if self._has_saved_layout:
            return
        # Tastatur unten in der linken Spalte auf Inhaltshöhe.
        if (getattr(self, "keyboard_dock", None) is not None
                and self.keyboard_dock.isVisible()
                and not self.keyboard_dock.isFloating()):
            h = self.keyboard_widget.minimumSizeHint().height()
            self.resizeDocks([self.screen_dock, self.keyboard_dock],
                             [max(1, self.height() - h), h], Qt.Vertical)

        # Rechte Spalte (Laufwerke/Einstellungen) auf schmalste Breite ohne
        # horizontales Scrollen.
        w = getattr(self, "_drives_width", 0)
        if w:
            for dock in (self.drives_dock, self.settings_dock):
                if dock is not None and dock.isVisible() and not dock.isFloating():
                    self.resizeDocks([self.screen_dock, dock],
                                     [max(1, self.width() - w), w], Qt.Horizontal)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        # Nach dem Layout-Durchlauf ausführen, sonst überschreibt QMainWindow es.
        QTimer.singleShot(0, self._shrink_keyboard)
        # Merke die "normale" Fenstergröße (kein Vollbild/Maximiert) und persistiere
        # sie, damit sie beim nächsten Start wiederhergestellt wird.
        if (not getattr(self, "_fullscreen", False)
                and not self.isMaximized() and not self.isMinimized()):
            self._normal_size = self.size()
        self._schedule_autosave()
    
    def setup_menus(self):
        """Setup menu bar."""
        menu_bar = self.menuBar()
        
        # File menu
        file_menu = menu_bar.addMenu("&File")
        
        mount_action = QAction("&Mount Disk...", self)
        mount_action.triggered.connect(self._on_mount_disk)
        file_menu.addAction(mount_action)

        file_menu.addSeparator()

        save_cfg_action = QAction("Save Configuration...", self)
        save_cfg_action.triggered.connect(self._on_save_config)
        file_menu.addAction(save_cfg_action)

        load_cfg_action = QAction("Load Configuration...", self)
        load_cfg_action.triggered.connect(self._on_load_config)
        file_menu.addAction(load_cfg_action)

        file_menu.addSeparator()

        exit_action = QAction("E&xit", self)
        exit_action.setShortcut(QKeySequence.Quit)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # Emulator menu
        emu_menu = menu_bar.addMenu("&Emulator")
        
        reset_action = QAction("&Reset", self)
        reset_action.setShortcut(QKeySequence(Qt.CTRL | Qt.Key_F5))
        reset_action.triggered.connect(self._on_reset)
        emu_menu.addAction(reset_action)

        # (Die Geschwindigkeit wird jetzt im Einstellungen-Dock, Reiter
        #  "Allgemein", über ein Dropdown eingestellt — kein Speed-Menü mehr.)

        # View menu
        view_menu = menu_bar.addMenu("&Ansicht")

        self.fullscreen_action = QAction("&Vollbild", self)
        self.fullscreen_action.setCheckable(True)
        self.fullscreen_action.setShortcut(QKeySequence(Qt.Key_F11))
        self.fullscreen_action.triggered.connect(self.toggle_fullscreen)
        view_menu.addAction(self.fullscreen_action)

        view_menu.addSeparator()

        # Dock toggles — re-open a closed dock via these checkboxes.
        screen_toggle = self.screen_dock.toggleViewAction()
        screen_toggle.setText("&Bildschirm")
        view_menu.addAction(screen_toggle)

        keyboard_toggle = self.keyboard_dock.toggleViewAction()
        keyboard_toggle.setText("&Tastatur")
        view_menu.addAction(keyboard_toggle)

        drives_toggle = self.drives_dock.toggleViewAction()
        drives_toggle.setText("Disk Drives")
        view_menu.addAction(drives_toggle)

        settings_toggle = self.settings_dock.toggleViewAction()
        settings_toggle.setText("Einstellungen")
        view_menu.addAction(settings_toggle)

        # Help menu
        help_menu = menu_bar.addMenu("&Help")
        
        about_action = QAction("&About", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)
    
    # ── Fullscreen (window-level, keeps the GL context intact) ───────────────

    def toggle_fullscreen(self, *args):
        if getattr(self, "_fullscreen", False):
            self.exit_fullscreen()
        else:
            self.enter_fullscreen()

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
        self.fullscreen_action.setChecked(True)
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
        self.fullscreen_action.setChecked(False)
        self.screen_widget.setFocus()

    # ── Configuration (YAML) ─────────────────────────────────────────────────

    def _gather_config(self) -> dict:
        """Build the full configuration dict from the live application state."""
        general = {"speed": float(self.speed_factor)}
        return config_io.build_config(
            self.screen_widget.params, general, self.drives_widget.get_mounts(),
            self._gather_window_state(), drive_types=self._drive_types)

    def _gather_window_state(self) -> dict:
        """Window size + dock layout (panel visibility/arrangement/sizes).

        The size stored is the last *normal* (non-fullscreen/-maximized) size, so
        a config saved while in fullscreen still restores the usable window size.
        ``dock_state`` is the base64-encoded :meth:`QMainWindow.saveState` blob,
        which captures which docks are shown, their arrangement and their sizes.
        """
        size = self._normal_size
        state = bytes(self.saveState().toBase64()).decode("ascii")
        return {
            "width": int(size.width()),
            "height": int(size.height()),
            "dock_state": state,
        }

    def _apply_window_state(self, win: dict):
        """Restore window size + dock layout from a config ``window`` section.

        The stored size is only honoured if it still fits on the current screen;
        otherwise the default size is used.  The dock layout (panel visibility,
        arrangement and sizes) is restored via :meth:`QMainWindow.restoreState`,
        which overrides the fresh-start defaults set up in :meth:`setup_ui`.
        """
        if not isinstance(win, dict):
            return
        w = int(win.get("width", self.DEFAULT_WIDTH))
        h = int(win.get("height", self.DEFAULT_HEIGHT))

        screen = self.screen() or QGuiApplication.primaryScreen()
        if screen is not None:
            avail = screen.availableGeometry()
            if w > avail.width() or h > avail.height():
                w, h = self.DEFAULT_WIDTH, self.DEFAULT_HEIGHT
        self.resize(w, h)
        self._normal_size = QSize(w, h)

        state = win.get("dock_state")
        if state:
            try:
                if self.restoreState(QByteArray.fromBase64(state.encode("ascii"))):
                    self._has_saved_layout = True
            except Exception:
                pass

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
        against triggering an autosave loop while values are being restored."""
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

            self.drives_widget.load_mounts(data.get("disks") or [])

            if "window" in data:
                self._apply_window_state(data.get("window") or {})
        finally:
            self._loading_config = False

    def _load_or_create_default_config(self):
        """Load ``~/.config/k1520emu/config.yaml`` or create it from defaults."""
        path = config_io.default_config_path()
        if os.path.exists(path):
            try:
                self._apply_config(config_io.load_config(path))
                return
            except Exception as e:
                QMessageBox.warning(
                    self, "Konfiguration",
                    f"Konnte {path} nicht laden:\n{e}\n\nStandardwerte werden verwendet.")
        # First run (or unreadable): write the current defaults as the new config.
        try:
            config_io.save_config(path, self._gather_config())
        except Exception as e:
            QMessageBox.warning(self, "Konfiguration",
                                f"Konnte Standard-Konfiguration nicht anlegen:\n{e}")

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
        if not self._emu_started or not self.power_btn.isChecked():
            return
        self.drives_widget.remount_all()
        self.emulator.power_on()
        self.cycles = 0
        self.frame_count = 0

    def _on_power_toggle(self, checked):
        """Toggle emulator power.

        Power ON = Kaltstart (Boot-ROM neu aktiviert, ZVE1-Reset), Zykluszähler
        zurückgesetzt, Bildschirm an.  Power OFF = Röhre dunkel, Emulator- und
        Zykluszähler-Lauf angehalten.
        """
        if checked:
            self.power_btn.setText("Power ON")
            # Kaltstart: Images neu mounten (setzt den K5122-Laufwerkszustand
            # zurück — sonst bootet das Boot-ROM nicht neu), dann power_on().
            self.drives_widget.remount_all()
            self.emulator.power_on()
            self.cycles = 0
            self.frame_count = 0
            self.run_timer.start()
            self.screen_widget.set_powered(True)
        else:
            self.power_btn.setText("Power OFF")
            self.run_timer.stop()
            self.screen_widget.set_powered(False)
        # Statusanzeige sofort aktualisieren (Zähler eingefroren bzw. auf 0).
        self._update_status()
    
    def _on_reset(self):
        """Reset emulator (Neustart vom Boot-ROM)."""
        # Wie beim Kaltstart: Images neu mounten setzt den K5122-Laufwerks-
        # zustand zurück, sonst läuft der Boot-ROM-Neustart nicht an.
        self.drives_widget.remount_all()
        self.emulator.reset()
        self.cycles = 0
    
    def _on_mount_disk(self):
        """Show mount dialog."""
        # Das Mounten selbst erledigt das DriveWidget — hier nur dessen Dock
        # nach vorn holen (der Tastaturfokus bleibt beim Bildschirm).
        self.drives_dock.show()
        self.drives_dock.raise_()
    
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
        except Exception as e:
            self._on_error(f"Emulator error: {e}")
    
    def _update_status(self):
        """Update status display."""
        fps = self.frame_count
        self.frame_count = 0
        
        # Disk status
        disk_status = ""
        for drive in range(4):
            if self.emulator.is_disk_active(drive):
                disk_status += "D"
            else:
                disk_status += "-"
        
        self.status_widget.update_status(self.cycles, fps, disk_status)
    
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
        if self._emu_started and self.power_btn.isChecked():
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

        if cold_restart and self._emu_started and self.power_btn.isChecked():
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
        self.power_btn.setChecked(False)
    
    def _on_about(self):
        """Show about dialog."""
        QMessageBox.information(
            self, "About K1520 Emulator",
            "K1520 A5120 Emulator v1.0\n\n"
            "A faithful emulation of the Robotron A5120 computer "
            "from the 1980s (German DDR).\n\n"
            "Features:\n"
            "  • Z80 CPU emulation\n"
            "  • 64 KB RAM (OPS card)\n"
            "  • Graphics display (ABS card)\n"
            "  • Floppy disk controller (AFS card)\n"
            "  • Serial interface (ASS card)\n"
            "  • Keyboard support\n"
        )
    
    def closeEvent(self, event):
        """Cleanup on close."""
        # Flush any pending configuration change so nothing is lost on exit.
        if self._autosave_timer.isActive():
            self._autosave_timer.stop()
            self._autosave_now()
        self.run_timer.stop()
        self.status_timer.stop()
        self.screen_widget.stop_display()
        self.emulator.stop()
        event.accept()
