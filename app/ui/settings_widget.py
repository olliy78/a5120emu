"""
K1520 Emulator - Settings Widget
================================

Dockable settings panel with tabbed categories.  The first tab, "CRT",
exposes every :class:`~app.ui.screen_widget.CRTParams` field as a live control
(slider + spin box, or colour picker), so the picture-tube look can be dialled
in interactively.  Every change is applied to the live ``ScreenWidget.params``
and triggers an immediate repaint.
"""

from typing import Callable, List

from PySide6.QtWidgets import (
    QWidget, QTabWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QScrollArea,
    QSlider, QDoubleSpinBox, QPushButton, QLabel, QColorDialog,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor

from app.ui.screen_widget import CRTParams


class SettingsWidget(QWidget):
    """Tabbed settings panel; currently the CRT (picture-tube) parameters."""

    def __init__(self, screen_widget, parent=None):
        super().__init__(parent)
        self.screen = screen_widget

        # Refreshers re-read control values from params (used by "Reset").
        self._refreshers: List[Callable[[], None]] = []

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        self.tabs = QTabWidget()
        self.tabs.addTab(self._build_crt_tab(), "CRT")
        layout.addWidget(self.tabs)

    # ── helpers ──────────────────────────────────────────────────────────────

    def _apply(self):
        """Push params to the screen and force an immediate repaint."""
        self.screen.update()

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

    # ── CRT tab ──────────────────────────────────────────────────────────────

    def _build_crt_tab(self) -> QWidget:
        p = self.screen.params

        inner = QWidget()
        form = QFormLayout(inner)

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
