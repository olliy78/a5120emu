"""Bildschirmtastatur der Flachtastatur K7673.09 (P8000-Terminal Typ 2).

doc/design/26_p8000emu_oberflaeche.md §8.  Die K7673.09 steckt im **selben Gehäuse wie die K7672**
(K8915); deshalb erbt dieses Widget Aussehen und Zeichnung von :class:`KeyboardK7672Widget`
(Wanne, Ausschnitte, Kappen mit Flanke, Lampenfeld) und tauscht nur Belegung und Beschriftung.

Nachgebildet ist die **Tastenmatrix** (8 Zeilen × 16 Spalten, 105 belegte Positionen —
`app/ui/k7673_layout.py`): ein Klick drückt die Matrixtaste, das Loslassen der Maus lässt sie los;
die Wiederholung gehaltener Tasten macht die K7673 im Kern (in Echtzeit, nicht in Maschinentakten —
`k1520_set_key_repeat_realtime`).  Das Hauptfenster setzt ``keyPressed(kode, …)`` mit
``kode = 0x04000000 | Zeile << 8 | Spalte`` an den Kern ab — der Kern kennt diese Kodierung
(`k1520_key_press`).

* **SHIFT (zweimal) und CTRL rasten beim Klicken ein** („halten“) und lösen sich mit dem zweiten
  Klick — mit der Maus gibt es keine zwei Hände.  Mit der rechten Maustaste lässt sich jede
  andere Taste ebenso festhalten.
* **LEDs**: OFF, CAPS und MOD im Lampenfeld oben rechts zeigen den Zustand, den die Tastatur im
  Kern führt (``term_leds``).
* Die Wirtstastatur läuft nicht über dieses Widget, sondern über das Terminalbild
  (`p8000_original.py`); die Tasten hier reagieren nur auf die Maus.

Dieselbe Schnittstelle wie die anderen Bildschirmtastaturen (``keyPressed``/``keyReleased``/
``set_powered``/``set_leds``/``clear_host_keys``), damit das Hauptfenster sie einhängt.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional, Set

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtWidgets import QSizePolicy

from app.ui import k7673_layout as L
from app.ui.keyboard import _Key
from app.ui.keyboard_k7672 import KeyboardK7672Widget, _Taste


@dataclass
class _Taste73(_Taste):
    """Eine Taste der K7673 — dazu ihre Matrixposition."""

    pos: L.Position = (-1, -1)


#: Umlauttasten wie auf dem Foto: links oben/unten die ASCII-Zeichen derselben Codes
#: (DIN 66003 ↔ ASCII), rechts groß der Umlaut — und ß mit „?“ oben, „~“ unten, ß unten rechts.
_UMLAUTE = {
    (1, 5): ("}", "]", "Ü", False),
    (2, 5): ("{", "[", "Ä", False),
    (6, 4): ("|", "\\", "Ö", False),
    (0, 5): ("?", "~", "ß", True),
}

#: Sinnbilder auf den Kappen, die mittig stehen (wie die Pfeile der K7672).
_MITTIG = ("↑", "↓", "←", "→", "↕", "⇕", "⇥", "⇤", "⤒", "⤓", "↖", "|←|", "|→|", "")


def _taste_bauen(pos: L.Position, x: float, y: float, b: float) -> _Taste73:
    sc, unten, oben, tipp = L.TASTEN[pos]
    h = L.taste_hoehe(pos)
    kind = "normal"
    if pos in L.UMSCHALTER:
        kind = "shift" if pos[1] == 6 else "ctrl"
    rechts, rechts_unten = "", False
    if pos in _UMLAUTE:
        oben, unten, rechts, rechts_unten = _UMLAUTE[pos]
    name = tipp or unten.replace("\n", " ") or "Taste"
    return _Taste73(x=x, y=y, low=unten, up=oben, code=None, w=b, h=h, style="light",
                    shape="rect", kind=kind, name=name,
                    vertical=(pos == (3, 7)), rechts=rechts, rechts_unten=rechts_unten,
                    gruppe=_gruppe(x, y), matrix=-1, scan=sc, pos=pos)


def _gruppe(x: float, y: float) -> str:
    """Tastenblock (ein Ausschnitt im Gehäuse) — dieselbe Teilung wie bei der K7672."""
    if y == 0.0:
        return "f1" if x < 6.0 else "f2" if x < 10.5 else "f3" if x < 15.0 else "f4"
    if x >= 19.0:
        return "ziffern"
    if x >= 15.5:
        return "kursor" if y >= 4.5 else "mitte"
    return "haupt"


class KeyboardK7673Widget(KeyboardK7672Widget):
    """Anklickbare Nachbildung der K7673.09 (P8000-Terminal) — Schnittstelle wie die K7672."""

    _MITTIG = _MITTIG

    def _layout(self) -> List[_Key]:
        return [_taste_bauen(pos, x, y, b) for pos, x, y, b in L.BILD]

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys}

    def __init__(self, parent=None):
        super().__init__(parent)
        self._gedrueckt: Set[L.Position] = set()   # Maus drückt gerade
        self._gehalten: Set[L.Position] = set()    # eingerastet (SHIFT/CTRL, rechte Maustaste)
        self._host: Set[L.Position] = set()        # von der Wirtstastatur gedrückt (nur Anzeige)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setFocusPolicy(Qt.NoFocus)            # der Fokus gehört dem Terminalbild

    # ── Geometrie (für Tests und Treffer) ────────────────────────────────────

    def taste_rechteck(self, pos: L.Position) -> QRectF:
        """Rechteck der Kappe im Widget."""
        unit, ox, oy = self._geometry()
        return self.kappe_von(self._by_pos[pos], unit, ox, oy)

    def taste_bei(self, punkt: QPointF) -> Optional[L.Position]:
        key = self._key_at(punkt)
        return key.pos if key is not None else None

    def _key_at(self, pos) -> Optional[_Key]:
        unit, ox, oy = self._geometry()
        punkt = QPointF(pos)
        for key in self._keys:
            if self._rect_of(key, unit, ox, oy).contains(punkt):
                return key
        return None

    # ── Zustand ──────────────────────────────────────────────────────────────

    def set_powered(self, an: bool):
        self._powered = bool(an)
        if not an:
            self.clear_host_keys()
        self.setEnabled(bool(an))
        self.update()

    def set_leds(self, maske: int):
        maske = max(0, int(maske)) & 0xFF
        if maske != self._leds:
            self._leds = maske
            self.update()

    def leds(self) -> int:
        return self._leds

    def zeige_flags(self, flags: int):
        """Schnittstelle der Funktionstastenleiste — die LEDs kommen hier aus ``term_leds``."""

    def _lampen_zustand(self):
        return tuple((name, bool(self._leds & bit) and self._powered, "gelb")
                     for bit, name in L.LED_FELD)

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Nur die drei Lampen — die Punkte über ALT1/^S/MOD2 der K7672 gibt es hier nicht."""
        feld = self._lampenfeld(unit, ox, oy)
        return [(feld.x() + feld.width() * (0.17 + 0.33 * i), feld.y() + feld.height() * 0.66,
                 an, name) for i, (name, an, _f) in enumerate(self._lampen_zustand())]

    def _is_down(self, key: _Key) -> bool:
        return key.pos in self._gedrueckt or key.pos in self._gehalten or key.pos in self._host

    def _is_active(self, key: _Key) -> bool:
        return key.pos in self._gehalten

    def host_matrix(self, zeile: int, spalte: int, gedrueckt: bool):
        """Die Wirtstastatur drückt/löst eine Matrixtaste: hervorheben (sendet nichts)."""
        pos = (zeile, spalte)
        if gedrueckt:
            self._host.add(pos)
        else:
            self._host.discard(pos)
        self.update()

    def clear_host_keys(self):
        """Alles loslassen, was die Bildschirmtastatur hält (Fokusverlust, Ausschalten)."""
        self._host.clear()
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
        if pos not in self._by_pos:
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

    def leaveEvent(self, event):
        # Maus verlässt das Widget mit gedrückter Taste: nicht hängen lassen (Wiederholung!).
        for pos in list(self._gedrueckt):
            self.loslassen(pos)

    # Die Wirtstastatur gehört dem Terminalbild; die Bildschirmtastatur nimmt keine Tasten an.
    def host_key_press(self, event):
        return None

    def host_key_release(self, event):
        return None

    def keyPressEvent(self, event):
        event.ignore()

    def keyReleaseEvent(self, event):
        event.ignore()

    # ── Kurzhinweis ──────────────────────────────────────────────────────────

    @staticmethod
    def _tip(key: _Key) -> str:
        pos = key.pos
        sc = L.scancode(pos)
        return (f"{key.name} — Zeile {pos[0]}, Spalte {pos[1]}, Scancode {sc:X}H"
                + (" (ohne Wirkung)" if pos in L.OHNE_WIRKUNG else ""))
