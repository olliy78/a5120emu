"""Bildschirmtastatur der Flachtastatur K7673.09 (P8000-Terminal Typ 2).

doc/design/26_p8000emu_oberflaeche.md §8.  Nachgebildet ist die **Tastenmatrix** (8 Zeilen ×
16 Spalten, 105 belegte Positionen — `app/ui/k7673_layout.py`): ein Klick drückt die Matrixtaste,
das Loslassen der Maus lässt sie los; die Wiederholung gehaltener Tasten macht die K7673 im Kern.
Das Hauptfenster setzt ``keyPressed(kode, …)`` mit ``kode = 0x04000000 | Zeile << 8 | Spalte`` an
den Kern ab — der Kern kennt diese Kodierung (`k1520_key_press`).

* **SHIFT (dreimal) und CTRL rasten beim Klicken ein** („halten“) und lösen sich mit dem zweiten
  Klick — mit der Maus gibt es keine zwei Hände.  Mit der rechten Maustaste lässt sich jede
  andere Taste ebenso festhalten.
* **LEDs**: ON/OFF, CAPS LOCK und MODE zeigen den Zustand, den die Tastatur im Kern führt
  (``term_leds``).
* Anordnung und Beschriftung folgen dem Foto der Tastatur des Anwenders (`k7673_layout`); die LEDs
  sitzen wie dort im Anzeigefeld oben rechts (OFF/CAPS/MOD), nicht auf den Tasten.

Dieselbe Schnittstelle wie die anderen Bildschirmtastaturen (``keyPressed``/``keyReleased``/
``set_powered``/``set_leds``/``clear_host_keys``), damit das Hauptfenster sie einhängt.
"""

from __future__ import annotations

from typing import Optional, Set

from PySide6.QtCore import QPointF, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QSizePolicy, QToolTip, QWidget

from app.ui import k7673_layout as L

_FUGE = 0.08                       # Fuge zwischen zwei Tasten (Tasteneinheiten)


class KeyboardK7673Widget(QWidget):
    keyPressed = Signal(int, bool, bool)
    keyReleased = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._gedrueckt: Set[L.Position] = set()   # Maus drückt gerade
        self._gehalten: Set[L.Position] = set()    # eingerastet (SHIFT/CTRL, rechte Maustaste)
        self._leds = 0
        self._powered = True
        self._unter_maus: Optional[L.Position] = None
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setMinimumSize(360, 120)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.NoFocus)           # der Fokus gehört dem Terminalbild

    # ── Geometrie ────────────────────────────────────────────────────────────

    def _massstab(self) -> float:
        return min(self.width() / L.BILD_BREITE, self.height() / L.BILD_HOEHE)

    def taste_rechteck(self, pos: L.Position) -> QRectF:
        """Rechteck der Taste im Widget (für Zeichnen, Treffer und Tests)."""
        s = self._massstab()
        for p, x, y, b in L.BILD:
            if p == pos:
                return QRectF((x + _FUGE) * s, (y + _FUGE) * s, (b - 2 * _FUGE) * s, (L.taste_hoehe(pos) - 2 * _FUGE) * s)
        raise KeyError(pos)

    def taste_bei(self, punkt: QPointF) -> Optional[L.Position]:
        s = self._massstab()
        for p, x, y, b in L.BILD:
            if QRectF(x * s, y * s, b * s, L.taste_hoehe(p) * s).contains(punkt):
                return p
        return None

    def sizeHint(self) -> QSize:
        return QSize(int(L.BILD_BREITE * 26), int(L.BILD_HOEHE * 26))

    def heightForWidth(self, breite: int) -> int:
        return int(breite * L.BILD_HOEHE / L.BILD_BREITE)

    def hasHeightForWidth(self) -> bool:
        return True

    # ── Zustand ──────────────────────────────────────────────────────────────

    def set_powered(self, an: bool):
        self._powered = bool(an)
        if not an:
            self.clear_host_keys()
        self.setEnabled(bool(an))
        self.update()

    def set_leds(self, maske: int):
        maske = max(0, int(maske))
        if maske != self._leds:
            self._leds = maske
            self.update()

    def leds(self) -> int:
        return self._leds

    def zeige_flags(self, flags: int):
        """Schnittstelle der Funktionstastenleiste — die LEDs kommen hier aus ``term_leds``."""

    def clear_host_keys(self):
        """Alles loslassen, was die Bildschirmtastatur hält (Fokusverlust, Ausschalten)."""
        for pos in sorted(self._gedrueckt | self._gehalten):
            self.keyReleased.emit(L.matrix_kode(*pos))
        self._gedrueckt.clear()
        self._gehalten.clear()
        self.update()

    def gehalten(self) -> Set[L.Position]:
        return set(self._gehalten)

    # ── Bedienung ────────────────────────────────────────────────────────────

    def druecken(self, pos: L.Position, halten: Optional[bool] = None):
        """Taste *pos* bedienen, wie ein Mausklick es täte.

        ``halten=None``: Umschalter rasten, andere Tasten gelten, solange :meth:`loslassen` noch
        nicht kam.  ``halten=True``: einrasten (zweiter Aufruf löst).
        """
        if pos not in L.TASTEN:
            return
        rasten = halten if halten is not None else pos in L.UMSCHALTER
        if rasten:
            if pos in self._gehalten:
                self._gehalten.discard(pos)
                self.keyReleased.emit(L.matrix_kode(*pos))
            else:
                self._gehalten.add(pos)
                self.keyPressed.emit(L.matrix_kode(*pos), False, False)
        else:
            self._gedrueckt.add(pos)
            self.keyPressed.emit(L.matrix_kode(*pos), False, False)
        self.update()

    def loslassen(self, pos: L.Position):
        if pos in self._gedrueckt:
            self._gedrueckt.discard(pos)
            self.keyReleased.emit(L.matrix_kode(*pos))
            self.update()

    def mousePressEvent(self, event):
        pos = self.taste_bei(event.position())
        if pos is None or not self._powered:
            return
        if event.button() == Qt.LeftButton:
            self.druecken(pos)
        elif event.button() == Qt.RightButton:
            self.druecken(pos, halten=True)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            for pos in list(self._gedrueckt):
                self.loslassen(pos)

    def mouseMoveEvent(self, event):
        pos = self.taste_bei(event.position())
        if pos != self._unter_maus:
            self._unter_maus = pos
            if pos is not None:
                sc = L.scancode(pos)
                name = L.TASTEN[pos][3] or L.TASTEN[pos][1].replace("\n", " ")
                QToolTip.showText(event.globalPosition().toPoint(),
                                  f"{name} — Zeile {pos[0]}, Spalte {pos[1]}, Scancode {sc:X}H"
                                  + (" (ohne Wirkung)" if pos in L.OHNE_WIRKUNG else ""), self)

    def leaveEvent(self, event):
        self._unter_maus = None

    # ── Zeichnen ─────────────────────────────────────────────────────────────

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        pal = self.palette()
        p.fillRect(self.rect(), pal.window())
        s = self._massstab()
        schrift = QFont(self.font())
        dunkel = pal.window().color().lightness() < 128
        kappe = QColor(70, 72, 78) if dunkel else QColor(232, 232, 228)
        kappe_tot = QColor(52, 54, 58) if dunkel else QColor(205, 205, 200)
        gedrueckt = QColor(70, 140, 220) if dunkel else QColor(120, 175, 240)
        rand = QColor(25, 25, 28) if dunkel else QColor(120, 120, 120)
        text = QColor(235, 235, 235) if dunkel else QColor(20, 20, 20)
        led_an = QColor(255, 80, 60)
        led_aus = QColor(90, 40, 40)
        for pos, x, y, b in L.BILD:
            r = self.taste_rechteck(pos)
            wirkt = pos not in L.OHNE_WIRKUNG
            aktiv = pos in self._gedrueckt or pos in self._gehalten
            p.setPen(QPen(rand, 1))
            p.setBrush(gedrueckt if aktiv else (kappe if wirkt else kappe_tot))
            p.drawRoundedRect(r, 0.12 * s, 0.12 * s)
            _, normal, mit_shift, _tipp = L.TASTEN[pos]
            if not wirkt:
                sc = L.scancode(pos)
                if not normal:
                    normal = f"E0 {sc & 0xFF:X}" if sc & 0xFF00 == 0xE000 else f"{sc:X}"
                mit_shift = ""
            p.setPen(text if wirkt else QColor(150, 150, 150))
            zeilen = normal.split("\n")
            if mit_shift:
                # Zeichentaste: Umschaltzeichen oben links klein, Grundzeichen unten groß.
                schrift.setPixelSize(max(6, int(s * 0.42)))
                p.setFont(schrift)
                p.drawText(r.adjusted(0, 0.3 * s, 0, 0), Qt.AlignCenter, normal)
                schrift.setPixelSize(max(5, int(s * 0.3)))
                p.setFont(schrift)
                p.drawText(r.adjusted(0.1 * s, 0.04 * s, 0, 0), Qt.AlignLeft | Qt.AlignTop, mit_shift)
            else:
                breit = max(len(z) for z in zeilen)
                schrift.setPixelSize(max(5, int(s * (0.42 if breit <= 2 and len(zeilen) == 1
                                                      else 0.30 if breit <= 4 else 0.25))))
                p.setFont(schrift)
                p.drawText(r, Qt.AlignCenter, normal)
        # Anzeigefeld oben rechts (wie am Foto): Beschriftung und LED nebeneinander
        rx, ry, rb, rh = L.LED_RAHMEN
        p.setPen(QPen(rand, 1))
        p.setBrush(Qt.NoBrush)
        p.drawRoundedRect(QRectF(rx * s, ry * s, rb * s, rh * s), 0.2 * s, 0.2 * s)
        schrift.setPixelSize(max(5, int(s * 0.3)))
        p.setFont(schrift)
        for bit, name, x, y in L.LED_FELD:
            p.setPen(text)
            p.drawText(QRectF(x * s, (y + 0.08) * s, 1.3 * s, 0.4 * s), Qt.AlignLeft | Qt.AlignVCenter, name)
            an = bool(self._leds & bit) and self._powered
            p.setPen(Qt.NoPen)
            p.setBrush(led_an if an else led_aus)
            p.drawRect(QRectF((x + 0.05) * s, (y + 0.6) * s, 0.4 * s, 0.2 * s))
        # Hinweis am unteren Rand (nur bei Platz).
        if self.height() > L.BILD_HOEHE * s + 0.9 * s:
            schrift.setPixelSize(max(6, int(s * 0.3)))
            p.setFont(schrift)
            p.setPen(QColor(140, 140, 140))
            p.drawText(QRectF(0, L.BILD_HOEHE * s, self.width(), s * 0.9), Qt.AlignCenter,
                       "K7673 nach Foto — SHIFT/CTRL rasten, rechte Maustaste hält jede Taste")
