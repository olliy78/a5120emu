"""
K1520 Emulator - Drive/Disk Management Widget
==============================================

Dialog and controls for mounting/unmounting disk images.
"""

import os

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QComboBox,
    QLabel, QLineEdit, QCheckBox, QFileDialog, QFrame, QMessageBox
)
from PySide6.QtCore import Qt, Signal, QTimer

from app import drive_types as dt


class DriveWidget(QWidget):
    """Widget for managing disk drives.

    The number and labelling of drive panels follows the machine's drive-bay
    configuration (:data:`drive_types`): only *present* K5122 slots get a panel;
    a slot set to "kein Laufwerk" is omitted.
    """

    disk_mounted = Signal(int, str)  # (drive, path)
    disk_unmounted = Signal(int)      # (drive)

    def __init__(self, emulator, drive_types=None, parent=None):
        """Initialize drive widget."""
        super().__init__(parent)
        self.emulator = emulator
        # Per-slot core DriveProfile names (length dt.NUM_SLOTS).
        self.drive_types = dt.normalize_list(drive_types or dt.DEFAULT_DRIVE_TYPES)
        self._drive_leds = {}
        # Laufwerks-Panels je Laufwerk (für programmatisches Restore aus Config).
        self._panels = {}
        # Aktuell gemountete Images je Laufwerk: drive -> (path, fmt, wp).
        # Wird beim Kaltstart (Power ON) neu gemountet, weil das den K5122-
        # Laufwerkszustand zurücksetzt (nötig, damit das Boot-ROM neu bootet).
        self._mounts = {}
        self.setup_ui()

        self._led_timer = QTimer(self)
        self._led_timer.timeout.connect(self._refresh_leds)
        self._led_timer.start(120)

    # Dateifilter für Öffnen/Erstellen: sowohl .img als auch .hfe.
    DISK_FILTER = "Disk Images (*.img *.hfe);;All Files (*)"

    def present_drives(self) -> list:
        """Indices of the K5122 slots that carry a drive (in slot order)."""
        return [i for i, t in enumerate(self.drive_types) if dt.is_present(t)]

    def _populate_format_combo(self, combo, drive: int):
        """Fill *combo* with the formats that fit the drive in *drive* (from core).

        Entry 0 is the drive-type default (labelled "Standard (<name>)"); the
        remaining geometry-compatible formats follow.  Each entry's userData is the
        concrete core format name passed to mount/create.
        """
        combo.clear()
        try:
            formats = self.emulator.drive_formats(drive)
            default = self.emulator.drive_default_format(drive)
        except Exception:
            formats, default = [], ""

        def add(name: str):
            """Eintrag mit dem reinen Formatnamen; die Katalogbeschreibung wird
            nur als Tooltip angeboten, damit das Auswahlfeld schmal bleibt."""
            combo.addItem(name, name)
            try:
                desc = self.emulator.format_description(name)
            except Exception:
                desc = ""
            if desc:
                combo.setItemData(combo.count() - 1, desc, Qt.ToolTipRole)

        if default:
            add(default)
        for name in formats:
            if name != default:
                add(name)
        if combo.count() == 0:
            # Fallback (should not happen for a present drive): plain default name.
            combo.addItem(default or "cpa800", default or "cpa800")
        combo.setCurrentIndex(0)

    @staticmethod
    def _selected_format(combo, drive: int) -> str:
        """Concrete core format name currently selected in *combo*."""
        data = combo.currentData()
        return data if data else (combo.currentText() or "")

    def _fail_msg(self, drive: int, action: str) -> str:
        """Fehlermeldung mit dem konkreten Grund aus dem Core (falls vorhanden)."""
        verb = "mount" if action == "mount" else "create"
        base = f"Failed to {verb} disk on drive {drive}"
        try:
            reason = self.emulator.last_error()
        except Exception:
            reason = ""
        return f"{base}:\n{reason}" if reason else base

    def _default_disk_dir(self) -> str:
        """Startverzeichnis für Datei-Dialoge: Unterordner 'disks' im Projekt-
        Hauptverzeichnis, sonst das Projekt-Hauptverzeichnis selbst."""
        # app/ui/drive_widget.py → Projekt-Hauptverzeichnis
        root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        disks = os.path.join(root, "disks")
        return disks if os.path.isdir(disks) else root

    def setup_ui(self):
        """Setup UI layout (panels are (re)built from the drive-bay config)."""
        self._main_layout = QVBoxLayout(self)
        self._main_layout.setContentsMargins(4, 4, 4, 4)
        self._main_layout.setSpacing(6)
        self._rebuild_panels()

    def _rebuild_panels(self):
        """Clear and recreate the drive panels from :attr:`drive_types`."""
        # Remove every existing item (panels + stretch) from the layout.
        while self._main_layout.count():
            item = self._main_layout.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()
        self._panels.clear()
        self._drive_leds.clear()

        present = self.present_drives()
        for drive in present:
            self._main_layout.addWidget(self._create_drive_panel(drive))

        if not present:
            self._main_layout.addWidget(
                QLabel("Kein Laufwerk bestückt.\nSiehe Einstellungen → Laufwerke."))

        self._main_layout.addStretch()

    def set_drive_types(self, drive_types, emulator=None):
        """Adopt a new drive-bay configuration (and optionally a new emulator).

        Rebuilds the panels for the now-present slots and forgets all mounts (the
        caller supplies a freshly created machine, so the K5122 state is empty).
        Restore any surviving disks afterwards via :meth:`load_mounts`.
        """
        if emulator is not None:
            self.emulator = emulator
        self.drive_types = dt.normalize_list(drive_types)
        self._mounts.clear()
        self._rebuild_panels()

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
        type_label = dt.short_label(self.drive_types[drive])
        head_layout.addWidget(QLabel(f"<b>Drive {drive}</b> — {type_label}"))
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

        # Format selection — populated from the core with the formats that fit
        # this slot's drive type (default first), so a non-K5601 drive offers its
        # own geometries instead of the fixed K5601 CP/A set.
        format_layout = QHBoxLayout()
        format_label = QLabel("Format:")
        format_combo = QComboBox()
        self._populate_format_combo(format_combo, drive)
        format_layout.addWidget(format_label)
        format_layout.addWidget(format_combo)
        layout.addLayout(format_layout)

        # Buttons: eine Toggle-Taste (Mount ⇄ Unmount) + "Leere Diskette erstellen".
        buttons_layout = QHBoxLayout()
        toggle_btn = QPushButton("Mount")
        create_btn = QPushButton("Leere Diskette erstellen")

        def set_mounted(path: str, fmt: str, wp: bool):
            """UI-Zustand nach erfolgreichem Mount setzen."""
            path_display.setText(path)
            toggle_btn.setText("Unmount")
            self._mounts[drive] = (path, fmt, wp)
            self.disk_mounted.emit(drive, path)

        def set_unmounted():
            """UI-Zustand nach Unmount setzen."""
            path_display.setText("")
            toggle_btn.setText("Mount")
            self._mounts.pop(drive, None)
            self.disk_unmounted.emit(drive)

        def on_toggle():
            # Ist ein Image gemountet → Unmount, sonst Mount-Dialog.
            if drive in self._mounts:
                if self.emulator.unmount_disk(drive):
                    set_unmounted()
                else:
                    QMessageBox.critical(self, "Error", f"Failed to unmount drive {drive}")
                return

            path, _ = QFileDialog.getOpenFileName(
                self, f"Mount Disk Image - Drive {drive}",
                self._default_disk_dir(), self.DISK_FILTER
            )
            if not path:
                return
            fmt = self._selected_format(format_combo, drive)
            wp = wp_check.isChecked()
            try:
                if self.emulator.mount_disk(drive, path, fmt, wp):
                    set_mounted(path, fmt, wp)
                else:
                    QMessageBox.critical(self, "Error", self._fail_msg(drive, "mount"))
            except Exception as e:
                QMessageBox.critical(self, "Error", str(e))

        def on_create():
            # Neue Datei anlegen; .img/.hfe erlaubt. Bei .img wird das Format aus
            # dem Auswahlfeld verwendet; .hfe kann ein beliebiges Format aufnehmen.
            path, _ = QFileDialog.getSaveFileName(
                self, f"Leere Diskette erstellen - Drive {drive}",
                self._default_disk_dir(), self.DISK_FILTER
            )
            if not path:
                return
            # Ohne Endung → .img annehmen.
            if not os.path.splitext(path)[1]:
                path += ".img"
            fmt = self._selected_format(format_combo, drive)
            wp = wp_check.isChecked()
            try:
                if self.emulator.create_disk(drive, path, fmt, wp):
                    set_mounted(path, fmt, wp)
                else:
                    QMessageBox.critical(self, "Error", self._fail_msg(drive, "create"))
            except Exception as e:
                QMessageBox.critical(self, "Error", str(e))

        toggle_btn.clicked.connect(on_toggle)
        create_btn.clicked.connect(on_create)

        buttons_layout.addWidget(toggle_btn)
        buttons_layout.addWidget(create_btn)
        layout.addLayout(buttons_layout)

        # Store references for updates
        group._path_display = path_display
        group._toggle_btn = toggle_btn
        group._create_btn = create_btn
        group._wp_check = wp_check
        group._format_combo = format_combo
        group._led = led
        self._panels[drive] = group

        return group

    # ── Config-Persistenz (gemountete Disketten) ─────────────────────────────

    def get_mounts(self) -> list:
        """Aktuell gemountete Images als serialisierbare Liste (für die Config)."""
        out = []
        for drive in sorted(self._mounts):
            path, fmt, wp = self._mounts[drive]
            out.append({
                "drive": drive,
                "path": path,
                "format": fmt,
                "write_protect": bool(wp),
            })
        return out

    def load_mounts(self, mounts: list):
        """Alle Laufwerke gemäß Config-Liste neu belegen (erst leeren, dann mounten).

        Fehlende Dateien werden übersprungen.  Aktualisiert die UI (Pfad, Toggle-
        Beschriftung, Format, Write-Protect) ohne die mount/unmount-Signale
        auszulösen — das Restore ist keine Nutzeraktion."""
        # 1) Alles aushängen, damit ein Config-Wechsel nicht alte Disks stehen lässt.
        for drive in list(self._mounts):
            try:
                self.emulator.unmount_disk(drive)
            except Exception:
                pass
            panel = self._panels.get(drive)
            if panel is not None:
                panel._path_display.setText("")
                panel._toggle_btn.setText("Mount")
        self._mounts.clear()

        # 2) Aus der Config mounten.
        for m in mounts or []:
            try:
                drive = int(m.get("drive"))
            except (TypeError, ValueError):
                continue
            path = m.get("path")
            fmt = m.get("format") or "cpa800"
            wp = bool(m.get("write_protect", False))
            panel = self._panels.get(drive)
            if panel is None or not path or not os.path.exists(path):
                continue
            try:
                if self.emulator.mount_disk(drive, path, fmt, wp):
                    idx = panel._format_combo.findData(fmt)
                    panel._format_combo.setCurrentIndex(idx if idx >= 0 else 0)
                    panel._wp_check.setChecked(wp)
                    panel._path_display.setText(path)
                    panel._toggle_btn.setText("Unmount")
                    self._mounts[drive] = (path, fmt, wp)
            except Exception:
                pass

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
