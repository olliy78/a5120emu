# Feinentwurf: Python Qt6-Anwendung

**Modul:** `app/`  
**Framework:** PySide6 (Qt6, offizielle Python-Bindung)

---

## 1. Aufgabe

Die Python Qt6-Anwendung ist die grafische Benutzeroberfläche des Emulators. Sie:
- zeigt den Bildschirm des emulierten Computers (Framebuffer → Phosphor-Darstellung)
- verwaltet Diskettenlaufwerke (Einlegen, Auswerfen, Typ wählen)
- nimmt Tastatureingaben entgegen und leitet sie an den C++-Kern weiter
- zeigt eine maschinenspezifische Optik (A5120-Frontpanel-Look)
- startet den Emulator-Thread und synchronisiert GUI-Updates

---

## 2. Datei-Struktur

```
app/
├── main.py                      # Einstiegspunkt, Argument-Parsing
├── emulator_thread.py           # QThread: C++-Kern-Schleife
├── core_binding/
│   ├── __init__.py
│   └── k1520.py                 # ctypes-Wrapper (→ doc/design/10_c_api.md)
├── ui/
│   ├── main_window.py           # QMainWindow: Hauptfenster
│   ├── machine_view.py          # Maschinenansicht (Screen + Peripherie)
│   ├── screen_widget.py         # Framebuffer-Widget
│   ├── drive_widget.py          # Einzelnes Diskettenlaufwerk
│   └── config_dialog.py        # Konfigurationsdialog
├── themes/
│   ├── base_theme.py            # Abstrakte Theme-Basisklasse
│   └── a5120_theme.py           # A5120 spezifische Darstellung
└── config/
    └── machines/
        ├── a5120.json           # A5120 Layout-Konfiguration
        └── prg710.json
```

---

## 3. Haupt-Einstiegspunkt

```python
# main.py
import sys
import argparse
from PySide6.QtWidgets import QApplication
from ui.main_window import MainWindow
from core_binding.k1520 import K1520Machine

def parse_args():
    p = argparse.ArgumentParser(description="K1520 Emulator")
    p.add_argument("--machine", default="a5120",
                   choices=["a5120", "prg710", "k8915"])
    p.add_argument("--disk-a", metavar="PATH[:FORMAT]")
    p.add_argument("--disk-b", metavar="PATH[:FORMAT]")
    p.add_argument("--disk-c", metavar="PATH[:FORMAT]")
    p.add_argument("--console", action="store_true",
                   help="CLI-Modus (kein Qt)")
    p.add_argument("--write-protect", action="store_true")
    return p.parse_args()

def main():
    args = parse_args()

    if args.console:
        # CLI-Modus: kein Qt
        from cli_runner import run_cli
        sys.exit(run_cli(args))

    app = QApplication(sys.argv)
    app.setApplicationName("K1520 Emulator")
    app.setOrganizationName("k1520emu")

    machine_type = {"a5120": 0, "prg710": 1, "k8915": 2}[args.machine]
    machine = K1520Machine(machine_type)

    # Disks aus Argumenten mounten
    for drive_idx, disk_arg in enumerate([args.disk_a, args.disk_b, args.disk_c]):
        if disk_arg:
            parts = disk_arg.split(":", 1)
            path = parts[0]
            fmt  = parts[1] if len(parts) > 1 else "cpa780"
            machine.mount_disk(drive_idx, path, fmt, args.write_protect)

    win = MainWindow(machine, args.machine)
    win.show()
    machine.power_on()

    sys.exit(app.exec())

if __name__ == "__main__":
    main()
```

---

## 4. Emulator-Thread

```python
# emulator_thread.py
from PySide6.QtCore import QThread, Signal
from core_binding.k1520 import K1520Machine

class EmulatorThread(QThread):
    frameReady   = Signal()              # Framebuffer hat sich geändert
    diskActivity = Signal(int, bool)     # (drive, active)

    # A5120: 2.4576 MHz CPU, 60 FPS
    CPU_FREQ_HZ      = 2_457_600
    FRAMES_PER_SEC   = 60
    CYCLES_PER_FRAME = CPU_FREQ_HZ // FRAMES_PER_SEC  # = 40960

    def __init__(self, machine: K1520Machine):
        super().__init__()
        self._machine = machine
        self._stop    = False

    def stop(self):
        self._stop = True
        self.wait()

    def run(self):
        import time
        frame_time = 1.0 / self.FRAMES_PER_SEC
        next_frame = time.monotonic()

        while not self._stop:
            self._machine.run(self.CYCLES_PER_FRAME)

            # Framebuffer-Update signalisieren
            if self._machine.fb_dirty():
                self._machine.fb_clear_dirty()
                self.frameReady.emit()

            # Laufwerk-Aktivität prüfen
            for drv in range(4):
                active = self._machine.disk_active(drv)
                self.diskActivity.emit(drv, active)

            # Timing: exakt 60 Hz halten
            next_frame += frame_time
            now = time.monotonic()
            sleep_time = next_frame - now
            if sleep_time > 0:
                time.sleep(sleep_time)
            elif sleep_time < -0.5:
                next_frame = now  # Catch-up verhindern
```

---

## 5. Hauptfenster

```python
# ui/main_window.py
from PySide6.QtWidgets import QMainWindow, QVBoxLayout, QWidget, QMenuBar
from PySide6.QtCore import Qt

class MainWindow(QMainWindow):
    def __init__(self, machine, machine_name: str):
        super().__init__()
        self._machine = machine
        self._machine_name = machine_name

        self.setWindowTitle(f"K1520 Emulator – {machine_name.upper()}")
        self.setFocusPolicy(Qt.StrongFocus)  # Tastatureingaben empfangen

        # Theme laden
        from themes.a5120_theme import A5120Theme
        self._theme = A5120Theme()

        # Maschinen-Ansicht
        from ui.machine_view import MachineView
        self._view = MachineView(machine, self._theme, machine_name)
        self.setCentralWidget(self._view)

        # Emulator-Thread
        from emulator_thread import EmulatorThread
        self._thread = EmulatorThread(machine)
        self._thread.frameReady.connect(self._view.screen.update)
        self._thread.diskActivity.connect(self._view.onDiskActivity)
        self._thread.start()

        self._buildMenu()
        self.resize(self._theme.window_width, self._theme.window_height)

    def closeEvent(self, event):
        self._thread.stop()
        super().closeEvent(event)

    def keyPressEvent(self, event):
        self._machine.key_press(event.key(),
                                 bool(event.modifiers() & Qt.ShiftModifier),
                                 bool(event.modifiers() & Qt.ControlModifier))

    def keyReleaseEvent(self, event):
        self._machine.key_release(event.key())

    def _buildMenu(self):
        menu = self.menuBar()
        file_menu = menu.addMenu("Datei")
        file_menu.addAction("Reset", self._machine.reset)
        file_menu.addSeparator()
        file_menu.addAction("Beenden", self.close)
```

---

## 6. Screen-Widget

```python
# ui/screen_widget.py
from PySide6.QtWidgets import QWidget
from PySide6.QtGui import QImage, QPainter, QColor
from PySide6.QtCore import Qt

class ScreenWidget(QWidget):
    """Zeigt den K7024 Framebuffer als Phosphor-Bildschirm."""

    PHOSPHOR_COLOR = QColor(0, 255, 64)   # P31 grün-phosphor (#00FF40)

    def __init__(self, machine, parent=None):
        super().__init__(parent)
        self._machine = machine
        self._scale   = 2   # 2× Skalierung: 640→1280, 288→576
        self.setFixedSize(640 * self._scale, 288 * self._scale)
        self.setFocusPolicy(Qt.NoFocus)
        self.setAttribute(Qt.WA_OpaquePaintEvent)

    def paintEvent(self, _event):
        fb = self._machine.framebuffer_bytes()
        if not fb:
            return

        w = self._machine.fb_width()
        h = self._machine.fb_height()

        # Graustufenbild aus Framebuffer
        img = QImage(fb, w, h, w, QImage.Format_Grayscale8)

        # Phosphor-Einfärbung
        colored = self._colorize(img)

        # Skalierung (Integer-Faktor, kein Anti-Aliasing)
        scaled = colored.scaled(
            w * self._scale, h * self._scale,
            Qt.KeepAspectRatio, Qt.FastTransformation)

        painter = QPainter(self)
        painter.setBackground(QColor(0, 0, 0))
        painter.eraseRect(self.rect())
        # Zentriert
        x = (self.width()  - scaled.width())  // 2
        y = (self.height() - scaled.height()) // 2
        painter.drawImage(x, y, scaled)

    def _colorize(self, gray: QImage) -> QImage:
        """Wandelt Graustufen in Phosphor-Farbe um."""
        rgb = gray.convertToFormat(QImage.Format_RGB32)
        bits = rgb.bits()
        # Schnelle Vektorisierung via numpy falls verfügbar
        try:
            import numpy as np
            arr = np.frombuffer(bits, dtype=np.uint8).reshape(
                rgb.height(), rgb.width(), 4)
            g = arr[:, :, 1].astype(np.float32) / 255.0
            arr[:, :, 0] = (g * 0).astype(np.uint8)          # R
            arr[:, :, 1] = (g * 255).astype(np.uint8)         # G
            arr[:, :, 2] = (g * 64).astype(np.uint8)          # B
        except ImportError:
            pass  # Fallback: kein numpy, keine Farbe
        return rgb
```

---

## 7. Diskettenlaufwerk-Widget

```python
# ui/drive_widget.py
from PySide6.QtWidgets import (QWidget, QHBoxLayout, QLabel,
                                 QComboBox, QPushButton, QFileDialog)
from PySide6.QtCore import Qt, Signal

class DriveWidget(QWidget):
    """Einzelnes Diskettenlaufwerk mit Typ-Auswahl und Datei-Öffner."""

    diskMounted   = Signal(int, str, str)  # (drive, path, format)
    diskUnmounted = Signal(int)

    FORMATS = [
        ("CPA 780 (Boot)", "cpa780"),
        ("CPA 800", "cpa800"),
        ("CPA 640", "cpa640"),
        ("CPA 624", "cpa624"),
        ("CPA 200", "cpa200"),
        ("CPA 200 (Boot)", "cpa200_boot"),
        ("SCPX 780", "scpx780"),
        ("SCPX 780b", "scpx780_b"),
    ]

    def __init__(self, drive_idx: int, label: str, parent=None):
        super().__init__(parent)
        self._drive_idx   = drive_idx
        self._mounted_path = None
        self._active = False

        layout = QHBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # LED
        self._led = QLabel("○")
        self._led.setFixedWidth(16)
        layout.addWidget(self._led)

        # Label (Laufwerk A:, B:, ...)
        lbl = QLabel(label)
        lbl.setFixedWidth(80)
        layout.addWidget(lbl)

        # Typ-Auswahl
        self._fmt_combo = QComboBox()
        for display, _ in self.FORMATS:
            self._fmt_combo.addItem(display)
        self._fmt_combo.setFixedWidth(140)
        layout.addWidget(self._fmt_combo)

        # Datei-Name
        self._path_label = QLabel("(leer)")
        self._path_label.setFixedWidth(180)
        self._path_label.setStyleSheet("color: gray;")
        layout.addWidget(self._path_label)

        # Öffnen-Knopf
        open_btn = QPushButton("Einlegen...")
        open_btn.clicked.connect(self._openDialog)
        layout.addWidget(open_btn)

        # Auswerfen-Knopf
        eject_btn = QPushButton("⏏")
        eject_btn.setFixedWidth(28)
        eject_btn.clicked.connect(self._eject)
        layout.addWidget(eject_btn)

    def setActive(self, active: bool):
        if active != self._active:
            self._active = active
            self._led.setText("●" if active else "○")
            self._led.setStyleSheet(
                "color: #00FF40;" if active else "color: #404040;")

    def _openDialog(self):
        path, _ = QFileDialog.getOpenFileName(
            self, f"Diskette für Laufwerk {self._drive_idx}",
            "", "Disk Images (*.img);;Alle Dateien (*)")
        if path:
            fmt_name = self.FORMATS[self._fmt_combo.currentIndex()][1]
            self._mounted_path = path
            self._path_label.setText(path.split("/")[-1])
            self._path_label.setStyleSheet("color: white;")
            self.diskMounted.emit(self._drive_idx, path, fmt_name)

    def _eject(self):
        self._mounted_path = None
        self._path_label.setText("(leer)")
        self._path_label.setStyleSheet("color: gray;")
        self.diskUnmounted.emit(self._drive_idx)
```

---

## 8. Maschinen-Theme

```python
# themes/a5120_theme.py
from PySide6.QtGui import QColor, QFont

class A5120Theme:
    # Fenstermaße
    window_width  = 1100
    window_height = 680

    # Farben (A5120: hellgrau, Plastik-Gehäuse)
    background_color  = QColor(180, 180, 175)
    panel_color       = QColor(160, 160, 155)
    text_color        = QColor(30, 30, 30)
    phosphor_color    = QColor(0, 255, 64)    # P31 grün
    led_active_color  = QColor(0, 255, 64)
    led_inactive_color = QColor(40, 80, 40)

    # Schrift
    label_font = QFont("Monospace", 9)
    title_font = QFont("Monospace", 11, QFont.Bold)

    # Bildschirm-Bezel
    bezel_color   = QColor(20, 20, 20)
    bezel_width   = 12    # px

    # Bildschirm-Skalierung
    screen_scale  = 2     # Integer-Faktor (640→1280, 288→576)

    # Laufwerks-Layout
    drives_position = "right"  # oder "bottom"
    num_drives      = 3

    # Titel
    machine_label = "ROBOTRON A5120"
```

---

## 9. Abhängigkeiten

```
# requirements.txt
PySide6>=6.5.0
numpy>=1.24.0   # Optional (Phosphor-Farbe beschleunigen)
```

Installation:
```bash
pip install -r app/requirements.txt
python app/main.py --machine a5120 --disk-a disks/cpadisk.img:cpa780
```
