"""
K1520 Emulator - Settings Widget
================================

Dockable settings panel with tabbed categories:

* **Allgemein** — general emulator settings (model, emulation speed dropdown,
  RAM-Disk RAF + Stand-by, doc/design/22_raf512.md §7.1; Lochstreifen K6022,
  doc/design/23_lochstreifen.md §7).
* **Schnittstellen** — die seriellen Schnittstellen nach außen (AP-S10; das
  Widget kommt vom Hauptfenster, damit sein Takt auch ohne sichtbaren Reiter läuft).
* **CRT** — every :class:`~app.ui.screen_widget.CRTParams` field as a live
  control (slider + spin box, or colour picker), so the picture-tube look can be
  dialled in interactively.

Every change is applied to the live emulator/``ScreenWidget.params`` and emits a
signal (:attr:`crtChanged` / :attr:`speedChanged`) so the main window can persist
the configuration automatically.
"""

from typing import Callable, List

from PySide6.QtWidgets import (
    QWidget, QTabWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QScrollArea,
    QSlider, QDoubleSpinBox, QComboBox, QCheckBox, QPushButton, QLabel, QColorDialog, QFrame,
)
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor

from app.ui.screen_widget import CRTParams
from app import drive_types as dt
from app import profil as profile
from app import takt
from app import raf


class SettingsWidget(QWidget):
    """Tabbed settings panel: general (speed) + CRT (picture-tube) parameters."""

    # Emitted after any CRT parameter change (colour/slider/reset).
    crtChanged = Signal()
    # Emitted when the speed dropdown selection changes; carries the factor
    # (1.0 = real time, >1.0 = fast-forward, 0.0 = unlimited).
    speedChanged = Signal(float)
    # Emitted when a drive-type dropdown changes; carries the list of 4 core
    # DriveProfile names (one per K5122 slot, "none" = empty slot).
    driveTypesChanged = Signal(list)
    # Emitted when the model dropdown changes; carries the model key
    # (app.modell.A5120 / app.modell.A5120_16).
    modelChanged = Signal(str)
    # Hardwarevariante (Profil.hardware) geändert: Schlüssel, Wert.
    hardwareChanged = Signal(str, str)
    # RAM-Disk-Auswahl geändert (``"none"``/``"raf128"``/``"raf512"``/``"raf2m"``,
    # `app/raf.py`) — wie der Modellwechsel ein Neuaufbau der Maschine.
    rafChanged = Signal(str)
    # Stand-by-Kästchen der RAF umgeschaltet (kein Neuaufbau).
    rafStandbyChanged = Signal(bool)
    # Kästchen „Lochstreifen (SIF1000, K6022)" umgeschaltet — Neuaufbau der Maschine
    # wie bei der RAF (doc/design/23_lochstreifen.md §7).
    ptapeChanged = Signal(bool)

    #: (Beschriftung, Faktor) — Faktor 0.0 heisst „unbegrenzt".  Die Stufen
    #: stehen in :mod:`app.takt`, damit Auswahlfeld und Statuszeile dasselbe
    #: sagen: der Nenntakt des A5120 (2,45 MHz) und seine Vielfachen.  Das
    #: Profil des K8915 bekommt seine eigene Liste (2,4576 MHz), siehe __init__.
    SPEED_OPTIONS = takt.auswahl()

    def __init__(self, screen_widget, parent=None, profil=None, schnittstellen=None):
        super().__init__(parent)
        self.screen = screen_widget
        self.profil = profil or profile.VORGABE
        self.SPEED_OPTIONS = takt.auswahl(self.profil.nenntakt_text)

        # Refreshers re-read control values from params (used by "Reset").
        self._refreshers: List[Callable[[], None]] = []
        # Guards the speed combo against emitting while set programmatically.
        self._speed_guard = False
        # Guards the drive-type combos against emitting while set programmatically.
        self._drive_guard = False
        self._drive_combos: List[QComboBox] = []
        # Guards the model combo against emitting while set programmatically.
        self._model_guard = False
        # Dito für RAM-Disk-Auswahl und Stand-by-Kästchen.
        self._raf_guard = False
        self._ptape_guard = False

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        self.tabs = QTabWidget()
        self.tabs.addTab(self._build_general_tab(), "Allgemein")
        self.tabs.addTab(self._build_drives_tab(), "Laufwerke")
        self.schnittstellen = schnittstellen
        if schnittstellen is not None:
            rolle = QScrollArea()
            rolle.setWidgetResizable(True)
            rolle.setFrameShape(QFrame.NoFrame)
            rolle.setWidget(schnittstellen)
            self.tabs.addTab(rolle, "Schnittstellen")
        self.tabs.addTab(self._build_crt_tab(), "CRT")
        layout.addWidget(self.tabs)

    def zeige_schnittstellen(self):
        """Den Reiter „Schnittstellen“ nach vorn holen (für Tests und Aufrufer)."""
        if self.schnittstellen is not None:
            self.tabs.setCurrentIndex(self.tabs.indexOf(self.schnittstellen.parentWidget()
                                                        .parentWidget()))

    # ── helpers ──────────────────────────────────────────────────────────────

    def _apply(self):
        """Push params to the screen, force a repaint, and notify listeners."""
        self.screen.update()
        self.crtChanged.emit()

    def _add_float(self, form: QFormLayout, label: str, minv: float, maxv: float,
                   step: float, getter: Callable[[], float],
                   setter: Callable[[float], None]):
        """A labelled slider + spin box bound to one float parameter."""
        decimals = max(0, len(str(step).split(".")[-1])) if "." in str(step) else 0

        slider = QSlider(Qt.Horizontal)
        slider.setRange(0, 1000)
        spin = QDoubleSpinBox()
        spin.setRange(minv, maxv)
        spin.setSingleStep(step)
        spin.setDecimals(decimals if decimals else 2)

        guard = {"busy": False}

        def to_slider(v):
            if maxv == minv:
                return 0
            return int(round((v - minv) / (maxv - minv) * 1000))

        def from_slider(i):
            return minv + (i / 1000.0) * (maxv - minv)

        def on_spin(v):
            if guard["busy"]:
                return
            guard["busy"] = True
            slider.setValue(to_slider(v))
            guard["busy"] = False
            setter(float(v))
            self._apply()

        def on_slider(i):
            if guard["busy"]:
                return
            guard["busy"] = True
            spin.setValue(from_slider(i))
            guard["busy"] = False
            setter(float(spin.value()))
            self._apply()

        def refresh():
            guard["busy"] = True
            v = getter()
            spin.setValue(v)
            slider.setValue(to_slider(v))
            guard["busy"] = False

        spin.valueChanged.connect(on_spin)
        slider.valueChanged.connect(on_slider)
        refresh()
        self._refreshers.append(refresh)

        row = QWidget()
        rl = QHBoxLayout(row)
        rl.setContentsMargins(0, 0, 0, 0)
        rl.addWidget(slider, 1)
        rl.addWidget(spin)
        form.addRow(label, row)

    def _add_stufe(self, form: QFormLayout, label: str, attr: str):
        """Checkbox, die eine Filterstufe (``CRTParams.<attr>``) ein-/ausschaltet."""
        box = QCheckBox(label)

        def refresh():
            box.blockSignals(True)
            box.setChecked(bool(getattr(self.screen.params, attr)))
            box.blockSignals(False)

        def on_toggled(on):
            setattr(self.screen.params, attr, bool(on))
            self._apply()

        box.toggled.connect(on_toggled)
        refresh()
        self._refreshers.append(refresh)
        form.addRow(box)

    def _add_color(self, form: QFormLayout, label: str,
                   getter: Callable[[], tuple],
                   setter: Callable[[tuple], None]):
        """A colour-swatch button + hex value bound to one RGB (0..1) parameter."""
        btn = QPushButton()
        btn.setFixedWidth(80)
        hexlbl = QLabel()

        def swatch():
            r, g, b = getter()
            ri, gi, bi = int(r * 255), int(g * 255), int(b * 255)
            btn.setStyleSheet(f"background-color: rgb({ri},{gi},{bi});")
            hexlbl.setText(f"#{ri:02X}{gi:02X}{bi:02X}")

        def pick():
            r, g, b = getter()
            initial = QColor(int(r * 255), int(g * 255), int(b * 255))
            c = QColorDialog.getColor(initial, self, label)
            if c.isValid():
                setter((c.redF(), c.greenF(), c.blueF()))
                swatch()
                self._apply()

        btn.clicked.connect(pick)
        swatch()
        self._refreshers.append(swatch)

        row = QWidget()
        rl = QHBoxLayout(row)
        rl.setContentsMargins(0, 0, 0, 0)
        rl.addWidget(btn)
        rl.addWidget(hexlbl)
        rl.addStretch(1)
        form.addRow(label, row)

    # ── Allgemein tab ────────────────────────────────────────────────────────

    def _build_general_tab(self) -> QWidget:
        inner = QWidget()
        form = QFormLayout(inner)

        self.model_combo = QComboBox(inner)
        for schluessel, _maschine, _em, beschriftung, _tastatur in self.profil.modelle:
            self.model_combo.addItem(beschriftung, schluessel)
        # Modelle, die der Kern noch nicht fährt: sichtbar, aber nicht wählbar.
        for schluessel, beschriftung, grund in self.profil.gesperrte_modelle:
            self.model_combo.addItem(beschriftung, schluessel)
            i = self.model_combo.count() - 1
            self.model_combo.model().item(i).setEnabled(False)
            self.model_combo.setItemData(i, grund, Qt.ToolTipRole)
        self.model_combo.currentIndexChanged.connect(self._on_model_combo)
        self.model_combo.setToolTip(self.profil.modell_tipp)
        # Nur im Programm mit Modellwahl; model_value() liefert sonst
        # die Vorgabe.
        if self.profil.modellwahl:
            form.addRow("Modell:", self.model_combo)
        else:
            self.model_combo.setVisible(False)

        # Hardwarevarianten des Programms (Profil.hardware, z. B. PC 1715: Zeichensatz, Tastatur)
        self.hardware_combos = {}
        for schluessel, beschriftung, tipp, werte, _modelle in self.profil.hardware:
            box = QComboBox(inner)
            for wert, anzeige in werte:
                box.addItem(anzeige, wert)
            box.setToolTip(tipp)
            box.currentIndexChanged.connect(
                lambda _i, s=schluessel: self._on_hardware_combo(s))
            self.hardware_combos[schluessel] = box
            form.addRow(beschriftung, box)
        self._hardware_guard = False
        self._hardware_gesperrt_nach_modell()

        self.speed_combo = QComboBox()
        for label, factor in self.SPEED_OPTIONS:
            self.speed_combo.addItem(label, float(factor))
        self.speed_combo.currentIndexChanged.connect(self._on_speed_combo)
        self.set_nenntakt(self.profil.nenntakt_text)
        form.addRow("Takt:", self.speed_combo)

        # RAM-Floppy RAF (doc/design/22_raf512.md §7.1) — in jedem Programm, dessen
        # Profil sie anbietet.  Ein Wechsel erzeugt die Maschine neu (rafChanged).
        self.raf_combo = QComboBox(inner)
        for schluessel, beschriftung in raf.TYPEN:
            self.raf_combo.addItem(beschriftung, schluessel)
        self.raf_combo.setToolTip(raf.TIPP)
        self.raf_combo.currentIndexChanged.connect(self._on_raf_combo)
        self.raf_standby_box = QCheckBox("Inhalt beim Beenden behalten (Stand-by 5PG)", inner)
        self.raf_standby_box.setToolTip(raf.STANDBY_TIPP)
        self.raf_standby_box.toggled.connect(self._on_raf_standby_box)
        self.raf_standby_box.setEnabled(False)          # erst mit gewählter RAF
        if self.profil.raf_wahl:
            form.addRow("RAM-Disk:", self.raf_combo)
            form.addRow("", self.raf_standby_box)
        else:
            self.raf_combo.setVisible(False)
            self.raf_standby_box.setVisible(False)

        # Lochstreifen: ADA K6022 mit Leser daro 1210 und Stanzer daro 1215 (SIF1000) —
        # in allen Programmen eine Option, Vorgabe aus (Anwenderentscheid E3).
        self.ptape_box = QCheckBox("Lochstreifen (SIF1000, K6022)", inner)
        self.ptape_box.setToolTip(
            "Steckt die ADA K6022 mit Lochstreifenleser und -stanzer (E/A E0H–E7H).  "
            "Bedient werden beide im Kasten „Lochstreifen“.  Ein Wechsel schaltet die "
            "Maschine aus und neu ein.")
        self.ptape_box.toggled.connect(self._on_ptape_box)
        if self.profil.ptape_wahl:
            form.addRow("Peripherie:", self.ptape_box)
        else:
            self.ptape_box.setVisible(False)       # P8000: gibt es die Karte nicht

        return inner

    def _on_hardware_combo(self, schluessel: str):
        if self._hardware_guard:
            return
        self.hardwareChanged.emit(schluessel, self.hardware_combos[schluessel].currentData())

    def hardware_value(self) -> dict:
        """Gewählte Hardwarevarianten ``{Schlüssel: Wert}`` (leer ohne Profil.hardware)."""
        return {k: b.currentData() for k, b in self.hardware_combos.items()}

    def set_hardware_value(self, daten: dict):
        """Hardwarevarianten setzen (ohne Signal)."""
        self._hardware_guard = True
        try:
            for k, box in self.hardware_combos.items():
                i = box.findData((daten or {}).get(k))
                box.setCurrentIndex(i if i >= 0 else 0)
        finally:
            self._hardware_guard = False

    def _hardware_gesperrt_nach_modell(self):
        """Felder ausgrauen, die am gewählten Modell nichts bewirken (PC 1715W: Zeichensatz)."""
        modell = self.model_value()
        for k, box in self.hardware_combos.items():
            box.setEnabled(self.profil.hardware_wirkt(k, modell))

    # ── Lochstreifen (K6022) ─────────────────────────────────────────────────

    def _on_ptape_box(self, an: bool):
        if self._ptape_guard:
            return
        self.ptapeChanged.emit(bool(an))

    def ptape_value(self) -> bool:
        return self.ptape_box.isChecked()

    def set_ptape_value(self, an: bool):
        """Kästchen setzen, ohne ``ptapeChanged`` auszulösen."""
        self._ptape_guard = True
        self.ptape_box.setChecked(bool(an))
        self._ptape_guard = False

    # ── RAM-Disk (RAF) ───────────────────────────────────────────────────────

    def _on_raf_combo(self, _idx: int):
        self.raf_standby_box.setEnabled(self.raf_value() != raf.KEINE)
        if self._raf_guard:
            return
        self.rafChanged.emit(self.raf_value())

    def _on_raf_standby_box(self, checked: bool):
        if self._raf_guard:
            return
        self.rafStandbyChanged.emit(bool(checked))

    def raf_value(self) -> str:
        """Der gewählte RAF-Schlüssel (``"none"``/``"raf128"``/``"raf512"``/``"raf2m"``)."""
        return raf.normalize(self.raf_combo.currentData())

    def set_raf_value(self, typ: str):
        """Den Eintrag für *typ* wählen (ohne Signal)."""
        self._raf_guard = True
        idx = self.raf_combo.findData(raf.normalize(typ))
        self.raf_combo.setCurrentIndex(idx if idx >= 0 else 0)
        self._raf_guard = False

    def raf_standby_value(self) -> bool:
        return self.raf_standby_box.isChecked()

    def set_raf_standby_value(self, an: bool):
        """Das Stand-by-Kästchen setzen (ohne Signal)."""
        self._raf_guard = True
        self.raf_standby_box.setChecked(bool(an))
        self._raf_guard = False

    def _on_model_combo(self, _idx: int):
        self._hardware_gesperrt_nach_modell()
        if self._model_guard:
            return
        self.modelChanged.emit(self.model_value())

    def model_value(self) -> str:
        """Der aktuell gewählte Modellschlüssel (``app.modell.A5120``/``A5120_16``)."""
        data = self.model_combo.currentData()
        return self.profil.modell_normalisieren(data)

    def set_model_value(self, model: str):
        """Den Eintrag für *model* wählen (ohne Signal)."""
        self._model_guard = True
        idx = self.model_combo.findData(self.profil.modell_normalisieren(model))
        self.model_combo.setCurrentIndex(idx if idx >= 0 else 0)
        self._model_guard = False
        self._hardware_gesperrt_nach_modell()

    def set_nenntakt(self, text: str) -> None:
        """Beschriftung der Taktstufen nach dem Nenntakt des Modells (ohne Signal)."""
        self.SPEED_OPTIONS = takt.auswahl(text)
        self._speed_guard = True
        try:
            for i, (label, _f) in enumerate(self.SPEED_OPTIONS):
                self.speed_combo.setItemText(i, label)
        finally:
            self._speed_guard = False
        self.speed_combo.setToolTip(
            f"Der {self.profil.rechner} läuft mit {text}.  "
            "Ein Vielfaches davon "
            "kürzt einen Kaltstart ab — die Uhr des Gastsystems zählt aber "
            "Taktzyklen und geht dann entsprechend falsch.")

    def _on_speed_combo(self, _idx: int):
        if self._speed_guard:
            return
        self.speedChanged.emit(self.speed_value())

    def speed_value(self) -> float:
        """Current speed factor from the dropdown (0.0 = unlimited)."""
        data = self.speed_combo.currentData()
        return float(data) if data is not None else 1.0

    def set_speed_value(self, factor: float):
        """Select the dropdown entry matching *factor* (no signal emitted)."""
        self._speed_guard = True
        idx = 0
        for i in range(self.speed_combo.count()):
            if abs(float(self.speed_combo.itemData(i)) - float(factor)) < 1e-9:
                idx = i
                break
        self.speed_combo.setCurrentIndex(idx)
        self._speed_guard = False

    # ── Laufwerke tab ────────────────────────────────────────────────────────

    def _build_drives_tab(self) -> QWidget:
        """One dropdown per K5122 slot to pick the drive type (or 'kein Laufwerk')."""
        inner = QWidget()
        form = QFormLayout(inner)

        for slot in range(dt.NUM_SLOTS):
            combo = QComboBox()
            for _short, core, _desc in self.profil.waehlbare_laufwerke():
                combo.addItem(dt.combo_label(core), core)
            combo.addItem(dt.NO_DRIVE_LABEL, dt.NO_DRIVE)
            combo.currentIndexChanged.connect(self._on_drive_combo)
            self._drive_combos.append(combo)
            form.addRow(f"Laufwerk {slot}:", combo)

        # Start from the standard configuration until a config restore sets it.
        self.set_drive_types(self.profil.standard_laufwerke())
        return inner

    def _on_drive_combo(self, _idx: int):
        if self._drive_guard:
            return
        self.driveTypesChanged.emit(self.drive_types())

    def drive_types(self) -> list:
        """Current per-slot core DriveProfile names (length :data:`dt.NUM_SLOTS`)."""
        return [c.currentData() for c in self._drive_combos]

    def set_drive_types(self, types: list):
        """Select the dropdown entries for *types* (no signal emitted)."""
        types = dt.normalize_list(types)
        self._drive_guard = True
        for slot, combo in enumerate(self._drive_combos):
            core = types[slot]
            idx = combo.findData(core)
            combo.setCurrentIndex(idx if idx >= 0 else 0)
        self._drive_guard = False

    # ── CRT tab ──────────────────────────────────────────────────────────────

    def _build_crt_tab(self) -> QWidget:
        p = self.screen.params

        inner = QWidget()
        form = QFormLayout(inner)

        # Filterstufen (einzeln abschaltbar)
        for text, attr in (("Vignette", "vignette_on"),):
            self._add_stufe(form, text, attr)

        # Colours
        self._add_color(form, "Farbe aktiv",
                        lambda: self.screen.params.phosphor_on,
                        lambda v: setattr(self.screen.params, "phosphor_on", v))
        self._add_color(form, "Farbe inaktiv",
                        lambda: self.screen.params.phosphor_off,
                        lambda v: setattr(self.screen.params, "phosphor_off", v))

        # Image
        self._add_float(form, "Helligkeit", 0.0, 4.0, 0.01,
                        lambda: self.screen.params.brightness,
                        lambda v: setattr(self.screen.params, "brightness", v))
        self._add_float(form, "Kontrast", 0.0, 2.0, 0.01,
                        lambda: self.screen.params.contrast,
                        lambda v: setattr(self.screen.params, "contrast", v))

        # Geometry
        self._add_float(form, "Krümmung X", 0.0, 0.3, 0.005,
                        lambda: self.screen.params.curvature[0],
                        lambda v: setattr(self.screen.params, "curvature",
                                          (v, self.screen.params.curvature[1])))
        self._add_float(form, "Krümmung Y", 0.0, 0.3, 0.005,
                        lambda: self.screen.params.curvature[1],
                        lambda v: setattr(self.screen.params, "curvature",
                                          (self.screen.params.curvature[0], v)))
        self._add_float(form, "Ecken-Rundung", 0.0, 0.25, 0.005,
                        lambda: self.screen.params.corner_radius,
                        lambda v: setattr(self.screen.params, "corner_radius", v))

        # Raster geometry (H/V size + position)
        self._add_float(form, "Bildbreite", 0.3, 1.5, 0.005,
                        lambda: self.screen.params.scale_x,
                        lambda v: setattr(self.screen.params, "scale_x", v))
        self._add_float(form, "Bildhöhe", 0.3, 1.5, 0.005,
                        lambda: self.screen.params.scale_y,
                        lambda v: setattr(self.screen.params, "scale_y", v))
        self._add_float(form, "Verschiebung X", -0.5, 0.5, 0.005,
                        lambda: self.screen.params.offset_x,
                        lambda v: setattr(self.screen.params, "offset_x", v))
        self._add_float(form, "Verschiebung Y", -0.5, 0.5, 0.005,
                        lambda: self.screen.params.offset_y,
                        lambda v: setattr(self.screen.params, "offset_y", v))

        # Reset
        reset_btn = QPushButton("Auf Standard zurücksetzen")
        reset_btn.clicked.connect(self._reset_crt)
        form.addRow(reset_btn)

        # Wrap in a scroll area so the panel stays usable when narrow.
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(inner)
        return scroll

    def refresh_all(self):
        """Re-read every control from the live params (e.g. after a config load)."""
        for refresh in self._refreshers:
            refresh()

    def _reset_crt(self):
        from dataclasses import fields
        defaults = CRTParams()
        for f in fields(CRTParams):
            setattr(self.screen.params, f.name, getattr(defaults, f.name))
        self.refresh_all()
        self._apply()
