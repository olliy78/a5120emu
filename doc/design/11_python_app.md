# Feinentwurf: Python Qt6-Anwendung

**Modul:** `app/`  
**Framework:** PySide6 (Qt6, offizielle Python-Bindung)

> **Lesehinweis.** Die Abschnitte 2–9 sind der URSPRÜNGLICHE Entwurf und an
> mehreren Stellen von der Umsetzung überholt (es gibt keinen `emulator_thread`,
> keine `themes/`, keine `machine_view`; der Lauf hängt an einem `QTimer` im
> Hauptfenster).  Wie die Oberfläche **heute** geschnitten ist, steht in
> **§10**; der Rest ist als Begründung der Grundentscheidungen weiter nützlich.

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

---

## 10. Oberfläche des Emulators — heutiger Stand (2026-09-13)

Das Fenster ist nach demselben Muster geschnitten wie das des k1520DiskTool
(`doc/design/13_k1520disktool.md` §20), damit beide Programme sich gleich
bedienen lassen.  Was sie sich dafür teilen, liegt **neben** beiden
Oberflächen, nicht in einer von ihnen:

| Modul | Was | Benutzt von |
|-------|-----|-------------|
| `app/ui_icons.py` | Symbole aus `app/icons/*.svg`, in der Palettenfarbe eingefärbt | beide |
| `app/ui_help.py`  | Handbuchfenster (Markdown, Inhaltsverzeichnis, Suche) | beide |
| `app/ui/help_window.py` · `app/disktool/ui/help_window.py` | nur noch Titel und Pfad der jeweiligen `.md` | je eins |

### 10.1 Jede Bedienung ist eine `QAction` (`app/ui/actions.py`)

Menü und Symbolleiste zeigen **dieselbe** Aktion; es gibt sie genau einmal,
mitsamt Kürzel, Symbol, Kurzwort und Statustext.  Neue Bedienwege kommen dort
hinzu, nicht im Fenster.  `erzeuge_aktionen(fenster)` legt sie als
`fenster.act_<name>` ab und ruft `fenster.addAction()` — ohne das gälte ein
Kürzel nur, solange die Aktion in einem **sichtbaren** Menü hängt, im Vollbild
also nicht mehr.

**Kein Kürzel ohne `Strg+Umschalt`** (Ausnahme `F11`).  Die Tastatur gehört dem
emulierten Rechner: `screen_widget` reicht jeden Tastendruck an den K7637 weiter,
auch `^S`, `^P`, `^C` und die Funktionstasten — und genau die braucht CP/M.  Qt
wertet ein Kürzel aber **vor** dem Widget aus; was das Fenster beansprucht, kommt
beim Gast nie an.  Das frühere `Strg+Q` (Beenden) und `Strg+F5` (Reset) sind
deshalb umgezogen.

### 10.2 Die Symbolleiste ist einrichtbar

Sie wird aus einer **Liste von Aktionsnamen** gebaut (`_leiste_fuellen`),
`None` ist ein Trennstrich.  Die Liste steht in der Konfiguration
(`window.toolbar`, `window.toolbar_style`), zusammengestellt wird sie über
*Ansicht ▸ Symbolleiste einrichten…* (`app/ui/toolbar_config.py`).  Ausblenden
geht über Qts eigenen `toggleViewAction()`.  **Im Menü steht immer alles** —
eine leergeräumte Leiste macht nichts unerreichbar.

Zwei Fallen, beide mit Wächter in `tests/python/test_gui_smoke.py`:

* **`QToolBar.clear()` gibt die Python-Hülle eines `toggleViewAction()` frei**
  (das C++-Objekt überlebt).  Ein gemerkter Verweis darauf ist danach tot, und
  der zweite Aufbau der Leiste — genau den löst eine gespeicherte Konfiguration
  aus — scheiterte mit „Internal C++ object already deleted".  Deshalb: einzeln
  `removeAction()` statt `clear()`, und die Kastenschalter werden in `_aktion()`
  **frisch beim Kasten geholt**.
* **Ein unbekannter Name wird übergangen**, nicht als Fehler behandelt — sonst
  stürbe die Oberfläche an einer Konfiguration aus einer älteren Fassung.

### 10.3 Die Statuszeile zeigt Zustand, keine Zähler (`app/ui/status_bar.py`)

Rechts steht, was dauerhaft gilt: der **eingestellte Takt** und **je bestücktem
Steckplatz eine Leuchte samt Feld** (Dateiname der eingelegten Diskette,
`R/O`/`R/W`).  Gefragt wird der Kern (`disk_path`, `is_disk_write_protected`,
`is_disk_led_on`); nur „das ist ein echtes Laufwerk am Greaseweazle" weiss allein
der Laufwerkskasten.

* **Der Takt ist der EINGESTELLTE**, wortgleich mit dem Auswahlfeld — beide
  Beschriftungen kommen aus `app/takt.py` (`2,45 MHz`, `10 × 2,45 MHz`,
  `unbegrenzt`).  Hier stand zuerst der *gemessene*; der schwankt im
  Sekundentakt (`10,0×`, `9,8×`, `10×`) und las sich wie ein Fehler, wo keiner
  war.  Gemessen wird weiter — der Wert steht im **Tooltip**, mitsamt dem Satz
  „Der Wirtsrechner kommt nicht mit", sobald er unter 90 % der Einstellung
  fällt.  Damit bleibt die Diagnose erreichbar, ohne dass die Zeile zappelt.
* **Die Leuchte ist gezeichnet, nicht getippt** (`DriveLamp.paintEvent`): leerer
  Ring = keine Diskette, gefüllt = eingelegt, rot = Zugriff.  Ein Emoji-Kreis
  (`○`/`●`) sieht in vielen Schriften fast gleich aus — derselbe Grund, aus dem
  das DiskTool sein Schloss zeichnet (§20.4 dort).
* **Sie hat einen EIGENEN, schnellen Takt** (`_lamp_timer`, 120 ms — derselbe wie
  `DriveWidget._led_timer`).  Am Sekundentakt des übrigen Statuszeilen-Aufbaus
  blitzte sie praktisch nie auf: ein Sektorzugriff ist in wenigen
  Zehntelsekunden vorbei.  Die Abfrage ist billig (eine je Laufwerk), und
  `DriveLamp.set_zustand` zeichnet nur bei echter Änderung neu.

Zykluszähler und Bildrate standen hier früher und sind ersatzlos weg — beide
sagen über die Maschine nichts, was man beim Arbeiten wissen will.

Damit das Feld stimmt, wirkt der Haken *Write-Protect* seit dieser Fassung
**sofort** (`DriveWidget._wp_umgeschaltet` → `set_disk_write_protect`) und nicht
erst beim nächsten Einlegen.

### 10.4 Das Handbuch (`app/help/handbuch.md`)

Eine `.md`-Datei, die Qt selbst setzt — kein Bauschritt, keine zusätzliche
Abhängigkeit.  Sie liegt unter `app/`, weil `doc/` im Anwenderpaket nicht dabei
ist (`packaging/build_payload.sh` kopiert `app/` als Ganzes).  Die Tabelle
*Tastenkürzel* ist ein **Vertrag**: zwei Tests prüfen beide Richtungen — jedes
genannte Kürzel hängt an einer Aktion, und kein verdrahtetes Kürzel fehlt im
Handbuch.

### 10.5 Fensterzustand — was gespeichert wird und was daran hakte

`window` trägt `geometry` (Qts `saveGeometry()`), daneben lesbar
`width`/`height`/`maximized`, den `dock_state` (Qts `saveState()`: welche Kästen
sichtbar sind, wo sie liegen, **wie breit** sie sind) und die Symbolleiste.  Drei
Dinge waren kaputt und sind behoben:

* **Ein Zug an der Trennlinie zwischen zwei Kästen wurde nie gespeichert.** Er
  ändert nur die Kästen, nicht das Fenster — es gab weder ein `resizeEvent` des
  Fensters (daran hing das Speichern) noch ein Signal.  Die Aufteilung überlebte
  nur, wenn zufällig etwas anderes ein Speichern auslöste.  Jetzt hängt ein
  Ereignisfilter an den vier Kästen (`MainWindow.eventFilter`, `QEvent.Resize`)
  und stösst den Autosave an, der ein Ziehen wie gehabt zu EINER Schreibung
  sammelt.  Dazu schreibt `closeEvent` jetzt **immer**, nicht nur eine anstehende
  Änderung.
* **Der Kasten der Tastatur war in der Höhe festgenagelt.**  `KeyboardWidget`
  trug senkrecht `QSizePolicy.Fixed`; damit nimmt Qt die Wunschhöhe zugleich als
  Mindest- **und** Höchsthöhe (`min == max == 257`), und die Trennlinie darüber
  lässt sich überhaupt nicht ziehen.  Solange die Tastatur unter dem Bildschirm
  sass, fiel es nicht auf — `_shrink_keyboard` rückte sie nach jedem Resize auf
  die zur Breite passende Höhe.  Sobald sie woanders andockte (etwa unter die
  Laufwerke), stand sie auf ihrer Wunschhöhe: schwarze Balken über und unter dem
  Tastenfeld (die Zeichnung hält ihr Seitenverhältnis) und der Nachbarkasten im
  Rollbalken.  Jetzt `Preferred` (min 165, max unbegrenzt), dazu passt
  `_tastatur_einpassen` die Höhe beim **Umdocken** an die neue Breite an —
  dort ändert sie sich sprunghaft.  Wächter:
  `test_the_keyboard_dock_can_be_resized_at_all`.
* **Und die Startaufteilung zog ihn wieder zurück.** `_shrink_keyboard` (Tastatur
  auf Inhaltshöhe, Seitenkästen schmal, Bildschirm bekommt den Rest) lief nach
  JEDEM Fenster-Resize, solange kein Layout gespeichert war — die waagerechte
  Trennlinie unter dem Bildschirm liess sich damit scheinbar nicht verschieben.
  Sie ist jetzt das, was ihr Name sagt: die Aufteilung für den Start.  Sobald
  eine Kastengrösse sich ändert, ohne dass wir selbst gerade umbauen
  (`_layout_laeuft`), gilt das als Anordnung des Anwenders (`_nutzer_layout`) und
  die Startaufteilung hält sich heraus.  Die Marke `_layout_laeuft` deckt drei
  eigene Umbauten ab — Fenster-Resize, Ein-/Ausblenden eines Kastens
  (`_kasten_sichtbarkeit`) und die Startaufteilung selbst — und wird jeweils erst
  eine Runde der Ereignisschleife später zurückgenommen, weil Qt das Layout
  verzögert zustellt.  Wächter: `test_a_dragged_separator_is_not_pushed_back`
  und die beiden Gegenproben daneben.  `_startaufteilung` setzt dabei nur noch
  die Größe des SEITENKASTENS (`resizeDocks([kasten], …)`); das frühere Paar
  (Bildschirm, Kasten) setzte voraus, dass beide nebeneinander liegen, und
  streckte den Bildschirm, sobald der Anwender einen Kasten woanders andockte.
* **Maximieren fiel beim nächsten Start auf die Vorgabegrösse zurück.** Die
  Ursache lag nicht am Speichern des Schalters, sondern an der gemerkten Grösse:
  beim Maximieren trifft das `resizeEvent` mit der neuen Grösse ein, **bevor**
  `isMaximized()` wahr wird (der Fensterverwalter meldet den Zustand erst
  danach).  Damit wurde die „normale" Grösse die maximierte (z. B. 1920×1080) —
  und die fiel beim nächsten Start durch die Prüfung „passt das noch auf den
  Bildschirm?" (die nutzbare Fläche ist wegen der Leisten kleiner) auf
  1024×680 zurück.  Der Wechsel wird jetzt im `changeEvent`
  (`QEvent.WindowStateChange`) mitgeschrieben, wo `normalGeometry()` noch die
  Grösse VOR dem Maximieren führt.
* **Getragen wird die Geometrie seitdem von Qt selbst.**  `saveGeometry()`
  bündelt Grösse, Bildschirmposition, den maximierten Zustand und die normale
  Grösse in einem Blob; `restoreGeometry()` wirkt auf dem noch unsichtbaren
  Fenster (kein `showMaximized()` im `showEvent` — das zeigte das Fenster mit
  halb aufgebauten Kästen) und prüft selbst, ob die Lage auf einen heute
  vorhandenen Bildschirm fällt.  `width`/`height`/`maximized` bleiben als
  lesbarer Rückfall daneben stehen; **`_geometrie_herstellen` ist deshalb ein
  eigener Schritt** — als früher `return` in `_apply_window_state` verschluckte
  der geglückte Geometrieweg die Wiederherstellung von Symbolleiste und
  Kästen.
* **…aber den maximierten Zustand trägt der Blob NICHT zurück** (2026-09-14,
  nachgemessen mit Qt 6.11 unter X11): `restoreGeometry()` meldet `True`, stellt
  die Grösse her — und `isMaximized()` bleibt falsch.  Und selbst von Hand
  gesetzt verfällt der Zustand am **unsichtbaren** Fenster: der Fensterverwalter
  (GNOME Shell/X11) zeigt es normal und lässt die Marke binnen 200 ms
  zurückfallen; ein `setWindowState` aus dem `showEvent` heraus genügt ebenfalls
  nicht, erst ein Zug eine Runde der Ereignisschleife SPÄTER hält.  Deshalb:
  `_maximiert_herstellen` löscht am unsichtbaren Fenster die (gelogene) Marke,
  merkt den Wunsch in `_maximiert_nachholen`, und `showEvent` holt ihn per
  `QTimer.singleShot(0, …)` nach.  Auf einem schon sichtbaren Fenster — der Weg
  von *Konfiguration laden* und *Standard zurücksetzen* — wirkt er sofort.
  Entschieden wird dabei **immer** nach unserem eigenen `maximized`-Feld; der
  Qt-Blob liefert nur noch Grösse und Lage.

  > **Warum es zweimal durch die Prüfung ging.** Unter
  > `QT_QPA_PLATFORM=offscreen` **scheitert** `restoreGeometry()` (der
  > gespeicherte Bildschirm passt nicht), also lief der Wächter durch den
  > Rückfallzweig — und auf einem Fenster, das er selbst schon angezeigt hatte.
  > Geprüft wurde damit genau der Weg, den der Anwender nie geht.  Die beiden
  > neuen Wächter stellen das ohne Fensterverwalter nach:
  > `test_maximized_comes_back_even_when_qt_drops_it_from_its_geometry` lässt
  > `restoreGeometry` gelingen, ohne etwas zu tun, und
  > `test_maximized_is_applied_after_the_window_is_shown` prüft, dass am
  > unsichtbaren Fenster nur gemerkt und beim Anzeigen gesetzt wird.  Beide
  > fallen ohne die Behebung um.

### 10.6 Diskette einlegen — im Menü, mit Untermenü je Laufwerk

*Datei ▸ Diskette einlegen ▸ Laufwerk …* (früher ein Menüpunkt ohne Wirkung)
ruft `DriveWidget.toggle_mount(drive)` — denselben Weg wie der Knopf im
Laufwerkskasten; ein zweiter Dateidialog daneben liefe beim nächsten Umbau
auseinander.  Gefüllt wird das Untermenü beim **Aufklappen**, weil sich die
Bestückung über *Einstellungen ▸ Laufwerke* ändert; gesperrt ist, was nicht
geht (einlegen bei belegtem, auswerfen bei leerem Laufwerk).

### 10.7 Auslieferungskonfiguration und „Standard zurücksetzen" (2026-09-14)

Der Zustand, in dem der Emulator **nach der Erstinstallation** aufgeht, ist
keine Sammlung von Vorgabewerten im Programmtext mehr, sondern **eine Datei
desselben Aufbaus wie die Konfiguration des Anwenders**:
`data/default_config.yaml` im Quellbaum, `share/k1520emu/default_config.yaml` in
einer Installation (`packaging/build_payload.sh` legt sie dorthin, Wächter
`py_packaging`).  Aufgelöst wird sie in `app/paths.py`
(`default_config_file()`), gelesen in `app/config_io.py`
(`standard_konfiguration()`), gebraucht an genau zwei Stellen:

* `MainWindow._load_or_create_default_config` — beim ersten Start, solange es
  noch keine `config.yaml` gibt; sie wird anschliessend als die neue
  `config.yaml` des Anwenders geschrieben und gehört von da an ihm.
* `MainWindow._standard_zuruecksetzen` — *Ansicht ▸ Standard zurücksetzen*
  (`act_standard`, kein Tastenkürzel, Symbol `reset-view`).  Nach Rückfrage
  wird die Vorgabe angewandt und **sofort** geschrieben (`_autosave_now()`, nicht
  über den sammelnden Timer): das Überschreiben ist bestätigt und darf nicht an
  einem Absturz in den nächsten 400 ms hängen.

Vier Festlegungen, die das tragen:

* **Ein FEHLENDER Abschnitt heisst „nicht anfassen", ein leerer „leeren".**
  `_apply_config` mountet nur noch, wenn `disks` überhaupt vorkommt.  Die
  Vorgabe trägt den Abschnitt bewusst nicht — Diskettenpfade sind absolut und
  rechnerspezifisch, und das Zurücksetzen der *Ansicht* darf die Maschine nicht
  leerräumen.  Wächter `test_reset_to_default_leaves_the_mounted_disks_alone`.
* **`window.geometry` gehört nicht in die Auslieferung** (darin steckt die
  Bildschirmposition des Baurechners).  `width`/`height`/`dock_state` reichen;
  wo das Fenster aufgeht, entscheidet der Fensterverwalter.  Wächter
  `test_the_shipped_default_config_is_found_and_complete`.
* **Der Benutzerordner ist KEIN Fundort.**  Anders als beim Formatkatalog
  endet die Kandidatenliste vor `config_dir()` — sonst setzte *Standard
  zurücksetzen* auf den Zustand zurück, den es gerade überschreiben will.
  Umbiegen lässt sich der Fundort über `K1520_DEFAULT_CONFIG` (Datei oder
  Verzeichnis).  Wächter `test_vorgabe_konfiguration_ignoriert_den_benutzerordner`.
* **Ohne die Datei läuft alles weiter.**  `standard_konfiguration()` gibt `{}`
  zurück; der erste Start bleibt dann bei den im Programm eingebauten Vorgaben
  (`CRTParams()`, Tempo 1,0, `dt.DEFAULT_DRIVE_TYPES`, `actions.STANDARD`) und
  schreibt trotzdem eine `config.yaml`, *Standard zurücksetzen* meldet, wo es
  gesucht hat.  Sie ist eine Beigabe, keine Voraussetzung.

Eine andere Auslieferung herzustellen ist damit ein Kopiervorgang: Fenster
einrichten, den Inhalt der eigenen `config.yaml` nach `data/default_config.yaml`
übernehmen, `disks:` und `window.geometry` streichen.

### 10.8 Die Nachbarprogramme starten — Werkzeugmenü und Werkzeugkonsole (2026-09-14)

Zur Installation gehören vier Programme, die dieselben Disketten anfassen: die
beiden Oberflächen (`a5120emu`, `k1520DiskTool`) und die beiden Konsolenwerkzeuge
(`k1520dbg`, `k1520disktool-cli`).  Wer eines davon offen hat, braucht
regelmässig ein zweites.  Beide Oberflächen haben deshalb ein Menü
**„Werkzeuge"** vor „Hilfe":

| Programm | Eintrag | Aktion |
|----------|---------|--------|
| Emulator | *k1520DiskTool starten* | `act_disktool` → `MainWindow._disktool_starten` |
| Emulator | *Werkzeugkonsole öffnen* | `act_konsole` → `MainWindow._konsole_starten` |
| DiskTool | *A5120-Emulator starten* | `act_emulator` → `MainWindow._emulator_starten` |

Das Wie steht an **einer** Stelle: `app/programme.py`
(`programm_starten()`, `konsole_starten()`).  Die Pfade der Konsolenwerkzeuge
und ihrer Handbücher kommen wie alles andere aus `app/paths.py`
(`tools_dir()`, `debugger()`, `disktool_cli()`, `doc_dir()`/`doc_file()`;
`--paths` zeigt sie seitdem mit).  Wächter: `py_programme`.

Vier Festlegungen, die man nicht aufweichen darf:

* **Gestartet wird der eigene Interpreter mit dem Skript des anderen Programms**
  (`sys.executable` + `<root>/app/…/main.py`), nicht der Starter aus `bin/`:
  den gibt es nur in einer Installation, der Quellbaum hat `run_a5120emu.sh` /
  `run_disktool.sh`.  Über den Interpreter ist der Weg in beiden Layouts
  derselbe, und die Umgebung (`K1520_HOME`, `K1520_DATA`, `LD_LIBRARY_PATH`)
  erbt das Kind ohnehin.  Unter Windows wird `pythonw.exe` genommen — sonst
  stünde hinter dem Fenster eine leere Eingabeaufforderung.
* **Das Kind wird abgekoppelt** (`start_new_session` bzw.
  `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`) und läuft im
  **Diskettenordner** des Anwenders — dieselbe Wahl wie in
  `packaging/launcher.sh`, damit der Kern sein `logs/` nicht irgendwo anlegt.
  Ohne eigene Sitzung nähme das Beenden des Elternteils es mit.
* **Die Konsole ist eine erzeugte Startdatei, kein Kommandoband**:
  `<config_dir>/werkzeugkonsole.sh` bzw. `.cmd`, bei jedem Öffnen neu
  geschrieben.  Sie setzt `K1520_HOME`, `K1520_FORMATS` und den `PATH` auf
  `tools_dir()`, wechselt in den Diskettenordner, druckt einen **abtippbaren
  Beispielaufruf mit einer wirklich vorhandenen Diskette** samt Verweis auf
  `handbuch_k1520dbg.md` und endet auf einer interaktiven Shell
  (`exec "$SHELL" -i` bzw. `cmd /k`) — ohne die schlösse sich das Fenster
  sofort wieder.  Wer sie anpassen will, hat eine Vorlage vor sich; dasselbe
  Muster wie `packaging/k1520dbg.cmd.in`, das weiterhin der Startmenü-Eintrag
  des Windows-Installers ist.
* **Die Windows-Startdatei bleibt ASCII** (`_nur_ascii`): `cmd.exe` liest eine
  Batchdatei in der eingestellten Kodepage, nicht in UTF-8.  Wächter
  `test_die_windows_startdatei_bleibt_ascii` — ein `?` darin heisst, dass ein
  Zeichen in der Umschrifttabelle fehlt.

**Kein Tastenkürzel** für die drei Einträge: jedes weitere `Strg+Umschalt+…`
müsste in die Kürzeltabelle des Handbuchs, und die ist ein Vertrag (§10.1).
Das Terminalprogramm wird unter Linux der Reihe nach gesucht
(`$TERMINAL`, dann `x-terminal-emulator`, `konsole`, `gnome-terminal`, …);
findet sich keines, nennt die Meldung den Pfad der vorbereiteten Startdatei,
statt kommentarlos nichts zu tun.
