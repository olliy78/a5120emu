"""
MC80.21/.22 — Bildschirmtastatur der Matrixtastatur
===================================================

Die Tastatur des MC80.2x besteht aus 79 kontaktlosen Tasten in einer 8 × 7-Matrix an der
DPL (``doc/mc80/hardware_2x.md`` §6); die Tastennummer (8 · Zeile + Bit) und die zweite
Ebene (UC), die dritte (CTR) und BK sind festgelegt, die Zeichen jeder Taste stehen in der
Tabelle TTA des BSYS.

**Lage der Tasten ist [?].**  Das Tastaturfoto (Bild 2.3 des Handbuchs) ist im Scan zu klein
für die Lage; belegt sind nur „Schreibmaschinenanordnung QWERTZ, CL rechts oben, BK links,
OFF und ENTER neben der Leertaste".  Die Nachbildung ordnet die Tasten deshalb **logisch nach
der Codetabelle** an und behauptet nichts über das Gerät: Ziffernreihe, QWERTZ-Reihen, die
Zeichen jeder Taste in beiden Ebenen laut TTA.  Die Taste „Leer" (Nr. 10H) ist die Leertaste;
Nr. 2EH ist unbelegt.

**Was eine Taste sendet.**  Dieselben Kerncodes wie die Wirtstastatur (``Mc80_2xMachine::
keyPress``): ASCII für Zeichentasten (umgeschaltete Zeichen tragen UC, Control-Buchstaben CTR),
Qt-Codes für ENTER/OFF/Tab/CL/Kursor und ``Qt::Key_Pause`` für BK.  Der Kern hält jede Taste
60 ms und reiht schnelle Anschläge ein [?].
"""

from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt

from app.ui.keyboard import KeyboardWidget, _Key
from app.ui.keyboard_mc80 import KeyboardMc80Widget, _s, _z

KEY_BK = int(Qt.Key_Pause)
KEY_CL = int(Qt.Key_Backspace)
KEY_HOCH, KEY_RUNTER = int(Qt.Key_Up), int(Qt.Key_Down)
KEY_LINKS, KEY_RECHTS = int(Qt.Key_Left), int(Qt.Key_Right)
KEY_RETURN, KEY_ESCAPE, KEY_TAB = int(Qt.Key_Return), int(Qt.Key_Escape), int(Qt.Key_Tab)


def _layout_mc80_2x() -> List[_Key]:
    r"""Logische Anordnung nach der Codetabelle (Raster 1.0 = eine Taste)::

        y 0  BK  1 2 3 4 5 6 7 8 9 0 - + \     CL
        y 1  ⇥   Q W E R T Z U I O P [         ↑
        y 2  CTR A S D F G H J K L ; = ENTER   ↓
        y 3  ⇧   Y X C V B N M , . ⇧           ← →
        y 4         Leertaste                  OFF
    """
    k: List[_Key] = []
    k.append(_s(0.0, 0.0, "BK", KEY_BK, "BK (Unterbrechung über die DPL) [?]"))
    reihe0 = (("1", "!"), ("2", "@"), ("3", "#"), ("4", "$"), ("5", "%"), ("6", "&"),
              ("7", "'"), ("8", "("), ("9", ")"), ("0", '"'), ("-", "/"), ("+", "*"),
              ("\\", "^"))
    for i, (low, up) in enumerate(reihe0):
        k.append(_z(1.0 + i, 0.0, low, up))
    k.append(_s(14.5, 0.0, "CL", KEY_CL, "CL (Löschen, Nr. 2FH)"))
    k.append(_s(0.0, 1.0, "⇥", KEY_TAB, "Tabulator (Nr. 2DH)", w=1.5))
    for i, ch in enumerate("QWERTZUIOP"):
        k.append(_z(1.5 + i, 1.0, ch))
    k.append(_z(11.5, 1.0, "[", "]"))
    k.append(_s(14.5, 1.0, "↑", KEY_HOCH, "Kursor hoch (Nr. 31H)"))
    k.append(_Key(x=0.0, y=2.0, low="CTR", w=1.8, shape="oval", style="light", kind="ctrl",
                  name="CTR — dritte Ebene (Control)"))
    for i, ch in enumerate("ASDFGHJKL"):
        k.append(_z(1.8 + i, 2.0, ch))
    k.append(_z(10.8, 2.0, ";", ":"))
    k.append(_z(11.8, 2.0, "=", "?"))
    k.append(_s(12.8, 2.0, "ENTER", KEY_RETURN, "ENTER (Nr. 30H)", w=1.6, shape="oval"))
    k.append(_s(14.5, 2.0, "↓", KEY_RUNTER, "Kursor runter (Nr. 33H)"))
    k.append(_Key(x=0.0, y=3.0, low="⇧", w=2.0, shape="oval", style="light", kind="shift",
                  name="UC — zweite Ebene (Umschalttaste)"))
    for i, ch in enumerate("YXCVBNM"):
        k.append(_z(2.0 + i, 3.0, ch))
    k.append(_z(9.0, 3.0, ",", "<"))
    k.append(_z(10.0, 3.0, ".", ">"))
    k.append(_Key(x=11.0, y=3.0, low="⇧", w=2.0, shape="oval", style="light", kind="shift",
                  name="UC — zweite Ebene (Umschalttaste)"))
    k.append(_s(13.5, 3.0, "←", KEY_LINKS, "Kursor links (Nr. 35H)"))
    k.append(_s(15.5, 3.0, "→", KEY_RECHTS, "Kursor rechts (Nr. 34H)"))
    k.append(_s(14.5, 4.0, "OFF", KEY_ESCAPE, "OFF / Abbruch (Nr. 32H)", w=1.4, shape="oval"))
    k.append(_Key(x=3.0, y=4.0, low="", code=0x20, w=8.0, shape="oval",
                  name="Leertaste (Nr. 10H)"))
    return k


class KeyboardMc80_2xWidget(KeyboardMc80Widget):
    """Anklickbare Nachbildung der MC80.2x-Matrixtastatur — Schnittstelle wie die K7637."""

    def _layout(self) -> List[_Key]:
        return _layout_mc80_2x()

    def _anzeigen_verankern(self):
        self._ctrl_taste = next(k for k in self._keys if k.kind == "ctrl")
        self._shift_taste = next(k for k in self._keys if k.kind == "shift")

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Die Ebenenanzeige (Leuchtdiode am Ebenen-Flipflop, [HB (33)]) folgt UC."""
        r = self._rect_of(self._shift_taste, unit, ox, oy)
        return [(r.left() - 0.45 * unit, r.center().y(), self.lock_active(),
                 "Ebenenanzeige (UC)")]

    _HOST_NAME = {int(Qt.Key_Return): "ENTER", int(Qt.Key_Enter): "ENTER",
                  int(Qt.Key_Escape): "OFF", int(Qt.Key_Tab): "⇥",
                  int(Qt.Key_Up): "↑", int(Qt.Key_Down): "↓", int(Qt.Key_Left): "←",
                  int(Qt.Key_Right): "→", int(Qt.Key_Backspace): "CL",
                  int(Qt.Key_Delete): "CL", int(Qt.Key_Pause): "BK"}

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [self._ctrl_taste]
        if key == int(Qt.Key_Space):
            return self._by_code.get(0x20, [])[:1]
        if key in self._HOST_NAME:
            return [k for k in self._keys if k.low == self._HOST_NAME[key]][:1]
        return KeyboardWidget._keys_for_host_event(self, event)

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "ctrl", "lock", "dead") or key.code is None:
            return name
        if key.code >= 0x01000000:
            return name
        t = f"{name}: Zeichen {key.code:02X}H"
        if key.shift_code is not None and key.shift_code != key.code:
            t += f", mit UC {key.shift_code:02X}H"
        return t
