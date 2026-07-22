"""
K1520 Emulator - Drive/Disk Management Widget
==============================================

Dialog and controls for mounting/unmounting disk images.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QComboBox,
    QLabel, QLineEdit, QCheckBox, QFileDialog, QFrame, QMessageBox
)
from PySide6.QtCore import Qt, Signal, QTimer


class DriveWidget(QWidget):
    """Widget for managing disk drives."""
    
    # Disk format list (from C-API)
    FORMATS = ["cpa800", "cpa780", "cpa640", "cpa624"]
    
    disk_mounted = Signal(int, str)  # (drive, path)
    disk_unmounted = Signal(int)      # (drive)
    
    def __init__(self, emulator, parent=None):
        """Initialize drive widget."""
        super().__init__(parent)
        self.emulator = emulator
        self._drive_leds = {}
        # Aktuell gemountete Images je Laufwerk: drive -> (path, fmt, wp).
        # Wird beim Kaltstart (Power ON) neu gemountet, weil das den K5122-
        # Laufwerkszustand zurücksetzt (nötig, damit das Boot-ROM neu bootet).
        self._mounts = {}
        self.setup_ui()

        self._led_timer = QTimer(self)
        self._led_timer.timeout.connect(self._refresh_leds)
        self._led_timer.start(120)
    
    # Nur drei Laufwerke (Drive 0..2).
    NUM_DRIVES = 3

    def setup_ui(self):
        """Setup UI layout."""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(6)

        for drive in range(self.NUM_DRIVES):
            layout.addWidget(self._create_drive_panel(drive))

        layout.addStretch()

    def _create_drive_panel(self, drive: int) -> QFrame:
        """Create panel for one drive (no title; LED+Name+Write-Protect in einer
        Zeile, darunter Image/Format/Buttons)."""
        group = QFrame(self)
        group.setFrameShape(QFrame.StyledPanel)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(4)

        # Kopfzeile: LED + "Drive N" + Write-Protect (alles in einer Zeile).
        head_layout = QHBoxLayout()
        led = QLabel(" ")
        led.setFixedSize(14, 14)
        led.setStyleSheet("border-radius: 7px; background-color: #2b2b2b; border: 1px solid #666;")
        head_layout.addWidget(led)
        head_layout.addWidget(QLabel(f"<b>Drive {drive}</b>"))
        head_layout.addStretch()
        wp_check = QCheckBox("Write-Protect")
        head_layout.addWidget(wp_check)
        layout.addLayout(head_layout)
        self._drive_leds[drive] = led

        # Path display
        path_layout = QHBoxLayout()
        path_label = QLabel("Image:")
        path_display = QLineEdit()
        path_display.setReadOnly(True)
        path_display.setPlaceholderText("[not mounted]")
        path_layout.addWidget(path_label)
        path_layout.addWidget(path_display)
        layout.addLayout(path_layout)

        # Format selection
        format_layout = QHBoxLayout()
        format_label = QLabel("Format:")
        format_combo = QComboBox()
        format_combo.addItems(self.FORMATS)
        format_combo.setCurrentText("cpa800")
        format_layout.addWidget(format_label)
        format_layout.addWidget(format_combo)
        layout.addLayout(format_layout)

        # Mount/Unmount buttons
        buttons_layout = QHBoxLayout()
        mount_btn = QPushButton("Mount")
        unmount_btn = QPushButton("Unmount")
        unmount_btn.setEnabled(False)
        
        def on_mount():
            path, _ = QFileDialog.getOpenFileName(
                self, f"Mount Disk Image - Drive {drive}",
                "", "Disk Images (*.img);;All Files (*)"
            )
            if path:
                fmt = format_combo.currentText()
                wp = wp_check.isChecked()
                
                try:
                    if self.emulator.mount_disk(drive, path, fmt, wp):
                        path_display.setText(path)
                        mount_btn.setEnabled(False)
                        unmount_btn.setEnabled(True)
                        self._mounts[drive] = (path, fmt, wp)
                        self.disk_mounted.emit(drive, path)
                    else:
                        QMessageBox.critical(self, "Error", f"Failed to mount disk on drive {drive}")
                except Exception as e:
                    QMessageBox.critical(self, "Error", str(e))
        
        def on_unmount():
            if self.emulator.unmount_disk(drive):
                path_display.setText("")
                mount_btn.setEnabled(True)
                unmount_btn.setEnabled(False)
                self._mounts.pop(drive, None)
                self.disk_unmounted.emit(drive)
            else:
                QMessageBox.critical(self, "Error", f"Failed to unmount drive {drive}")
        
        mount_btn.clicked.connect(on_mount)
        unmount_btn.clicked.connect(on_unmount)
        
        buttons_layout.addWidget(mount_btn)
        buttons_layout.addWidget(unmount_btn)
        layout.addLayout(buttons_layout)
        
        # Store references for updates
        group._path_display = path_display
        group._mount_btn = mount_btn
        group._unmount_btn = unmount_btn
        group._wp_check = wp_check
        group._format_combo = format_combo
        group._led = led
        
        return group

    def remount_all(self):
        """Alle aktuell gemounteten Images neu mounten (setzt den K5122-
        Laufwerkszustand zurück — für den Kaltstart bei Power ON)."""
        for drive, (path, fmt, wp) in list(self._mounts.items()):
            try:
                self.emulator.mount_disk(drive, path, fmt, wp)
            except Exception:
                pass

    def _refresh_leds(self):
        """Update all drive LED indicators from emulator state."""
        for drive, led in self._drive_leds.items():
            is_on = False
            try:
                is_on = self.emulator.is_disk_led_on(drive)
            except Exception:
                is_on = False

            if is_on:
                led.setStyleSheet(
                    "border-radius: 7px; background-color: #ff3b30; border: 1px solid #ff8a80;"
                )
            else:
                led.setStyleSheet(
                    "border-radius: 7px; background-color: #2b2b2b; border: 1px solid #666;"
                )
