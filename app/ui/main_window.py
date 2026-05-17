"""
K1520 Emulator - Main Window
=============================

Main application window with display, controls, and status.
"""

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QLabel, QMessageBox, QDockWidget, QMenu, QApplication
)
from PySide6.QtCore import Qt, QTimer, QSize
from PySide6.QtGui import QIcon, QKeySequence, QAction

from app.ui.screen_widget import ScreenWidget, StatusWidget
from app.ui.drive_widget import DriveWidget
from app.core_binding.k1520 import K1520Emulator


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
        
        # Emulator loop
        self.run_timer = QTimer()
        self.run_timer.timeout.connect(self._run_emulator)
        self.run_timer.setInterval(20)  # 50 Hz

        # Default power state is ON.
        self.run_timer.start()
        self.screen_widget.start_display()
        
        self.resize(800, 600)
    
    def setup_ui(self):
        """Setup main UI layout."""
        central = QWidget(self)
        self.setCentralWidget(central)
        
        layout = QHBoxLayout(central)
        
        # Display area
        display_layout = QVBoxLayout()
        
        # Display widget
        self.screen_widget = ScreenWidget()
        self.screen_widget.set_emulator(self.emulator)
        display_layout.addWidget(self.screen_widget)
        
        # Control buttons
        controls_layout = QHBoxLayout()
        
        self.power_btn = QPushButton("Power ON")
        self.power_btn.setCheckable(True)
        self.power_btn.setChecked(True)
        self.power_btn.clicked.connect(self._on_power_toggle)
        controls_layout.addWidget(self.power_btn)
        
        self.reset_btn = QPushButton("Reset")
        self.reset_btn.clicked.connect(self._on_reset)
        controls_layout.addWidget(self.reset_btn)
        
        controls_layout.addStretch()
        
        display_layout.addLayout(controls_layout)
        
        # Status bar
        self.status_widget = StatusWidget()
        display_layout.addWidget(self.status_widget)
        
        # Add display to main layout
        layout.addLayout(display_layout, 1)
        
        # Drives dock
        drives_dock = QDockWidget("Disk Drives")
        self.drives_widget = DriveWidget(self.emulator)
        drives_dock.setWidget(self.drives_widget)
        self.addDockWidget(Qt.RightDockWidgetArea, drives_dock)
    
    def setup_menus(self):
        """Setup menu bar."""
        menu_bar = self.menuBar()
        
        # File menu
        file_menu = menu_bar.addMenu("&File")
        
        mount_action = QAction("&Mount Disk...", self)
        mount_action.triggered.connect(self._on_mount_disk)
        file_menu.addAction(mount_action)
        
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
        
        # Help menu
        help_menu = menu_bar.addMenu("&Help")
        
        about_action = QAction("&About", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)
    
    def _on_power_toggle(self, checked):
        """Toggle emulator power."""
        if checked:
            self.power_btn.setText("Power ON")
            self.run_timer.start()
            self.screen_widget.start_display()
        else:
            self.power_btn.setText("Power OFF")
            self.run_timer.stop()
            self.screen_widget.stop_display()
            self.emulator.stop()
    
    def _on_reset(self):
        """Reset emulator."""
        self.emulator.reset()
        self.cycles = 0
    
    def _on_mount_disk(self):
        """Show mount dialog."""
        # This is handled by DriveWidget
        self.drives_widget.setFocus()
    
    def _run_emulator(self):
        """Run emulator for one frame."""
        try:
            cycles = self.emulator.run(10000)
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
        """Set emulation speed."""
        if speed == 0.0:
            self.run_timer.stop()  # Unlimited
            self._run_unlimited()
        else:
            # For now, just use fixed frame rate
            self.run_timer.start()
    
    def _run_unlimited(self):
        """Run emulator as fast as possible."""
        while self.power_btn.isChecked():
            self._run_emulator()
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
