"""
K1520 Emulator - Main Window
=============================

Main application window with display, controls, and status.
"""

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QLabel, QMessageBox, QDockWidget, QMenu, QApplication, QFileDialog,
    QScrollArea
)
from PySide6.QtCore import Qt, QTimer, QSize
from PySide6.QtGui import QIcon, QKeySequence, QAction

from app.ui.screen_widget import ScreenWidget, StatusWidget
from app.ui.settings_widget import SettingsWidget
from app.ui.drive_widget import DriveWidget
from app.ui.keyboard import KeyboardWidget
from app.core_binding.k1520 import K1520Emulator
from app import config_io


class MainWindow(QMainWindow):
    """Main emulator window."""
    
    def __init__(self):
        """Initialize main window."""
        super().__init__()
        self.setWindowTitle("K1520 A5120 Emulator")
        self.setWindowIcon(QIcon.fromTheme("computer"))
        
        # Create emulator
        try:
            self.emulator = K1520Emulator()
            self.emulator.power_on()
        except Exception as e:
            QMessageBox.critical(self, "Initialization Error", str(e))
            raise
        
        # Setup UI
        self.setup_ui()
        self.setup_menus()
        
        # Status updates
        self.cycles = 0
        self.frame_count = 0
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self._update_status)
        self.status_timer.start(1000)  # Update every second
        
        # Emulator loop.
        # The A5120 U880 runs at ~2.45 MHz (matches the core's K5122 cpu_hz default).
        # To emulate at real time we must execute cpu_hz * frame_interval cycles per
        # frame — with a 20 ms tick that is 49000 cycles, NOT the old 10000 (which ran
        # the machine at only 0.2x speed, so a boot that takes ~13.8M cycles dragged on
        # for ~28 s instead of ~5.6 s, and the CP/A clock ran 5x too slow).
        self.CPU_HZ = 2_450_000
        self.frame_interval_ms = 20  # 50 Hz
        # speed_factor > 1.0 fast-forwards (e.g. to shorten the boot); 1.0 = real time.
        self.speed_factor = 1.0
        self.run_timer = QTimer()
        self.run_timer.timeout.connect(self._run_emulator)
        self.run_timer.setInterval(self.frame_interval_ms)

        # Default power state is ON.
        self.run_timer.start()
        self.screen_widget.start_display()
        
        self.resize(800, 600)
    
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
        self.drives_widget = DriveWidget(self.emulator)
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
        self.settings_dock.hide()

        # Startaufteilung: rechte Spalte (Laufwerke) so schmal wie möglich, ohne
        # horizontales Scrollen; Bildschirm breit.  Tastatur auf Minimalhöhe.
        self._drives_width = self.drives_widget.sizeHint().width() + 28  # + Scrollbar
        self.resizeDocks([self.screen_dock, self.drives_dock],
                         [900, self._drives_width], Qt.Horizontal)
        self._shrink_keyboard()

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
        """
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
        
        emu_menu.addSeparator()
        
        speed_menu = emu_menu.addMenu("&Speed")
        
        for name, speed in [("1x (Original)", 1.0), ("2x", 2.0), ("10x", 10.0), ("Unlimited", 0.0)]:
            action = QAction(name, self)
            action.setCheckable(True)
            action.triggered.connect(lambda checked, s=speed: self._set_speed(s))
            speed_menu.addAction(action)
        
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

    def _on_save_config(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Configuration", "",
            "YAML-Konfiguration (*.yaml *.yml)")
        if not path:
            return
        if "." not in path.rsplit("/", 1)[-1]:
            path += ".yaml"
        try:
            config_io.save_config(path, self.screen_widget.params)
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
            self.screen_widget.params.update_from_dict(data.get("crt", {}))
            self.settings_widget.refresh_all()
            self.screen_widget.update()
        except Exception as e:
            QMessageBox.critical(self, "Load Configuration", str(e))

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
        # This is handled by DriveWidget
        self.drives_widget.setFocus()
    
    def _cycles_per_frame(self) -> int:
        """CPU cycles to run per frame for the current speed (real time * speed_factor)."""
        return int(self.CPU_HZ * self.frame_interval_ms / 1000 * self.speed_factor)

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
    
    def _set_speed(self, speed: float):
        """Set emulation speed. 1.0 = real time, >1.0 = fast-forward, 0.0 = unlimited."""
        if speed == 0.0:
            self.run_timer.stop()  # Unlimited
            self._run_unlimited()
        else:
            self.speed_factor = speed
            self.run_timer.start()
    
    def _run_unlimited(self):
        """Run emulator as fast as the host allows (fast-forward)."""
        # One real-time frame worth of cycles per iteration keeps per-call overhead
        # low while still pumping the Qt event loop often enough to stay responsive.
        chunk = int(self.CPU_HZ * self.frame_interval_ms / 1000)
        while self.power_btn.isChecked():
            try:
                self.cycles += self.emulator.run(chunk)
                self.frame_count += 1
            except Exception as e:
                self._on_error(f"Emulator error: {e}")
                break
            QApplication.processEvents()
    
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
        self.run_timer.stop()
        self.status_timer.stop()
        self.screen_widget.stop_display()
        self.emulator.stop()
        event.accept()
