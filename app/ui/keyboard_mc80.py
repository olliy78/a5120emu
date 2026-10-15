"""
MC80.3x — Bildschirmtastatur der abgesetzten Tastatur
=====================================================

Nachbildung der Tastatur des MC80.30/31 nach dem Bild A5 „Tastenübersicht“ der
Betriebsdokumentation (``doc/original_docs/mc80/mc80.3x-bda.pdf`` S. 39; Beschreibung
[BDA S.12], Abschrift in ``doc/mc80/hardware_3x.md`` §6.3).  Gezeichnet wie die übrigen
Tastaturen (:class:`app.ui.keyboard.KeyboardWidget`), mit derselben Schnittstelle
(:attr:`keyPressed`/:attr:`keyReleased`).  Eingehängt im ``mc80emu`` (AP M6).

**Was eine Taste sendet.**  Die echte Tastatur hat einen eigenen U881 und schickt
**Zeichen** (9600 Bd an SIO A der ASP); ihre Firmware liegt nicht vor.  Der Kern
(``core/peripherals/mc80_tastatur/``, ``Mc80Tastatur::qtTaste``) nimmt deshalb dieselben
Codes wie von der Wirtstastatur: ASCII für Zeichentasten (mit ``shift``/``ctrl``-Flag),
``Qt::Key_Return`` = ENTER (11H), ``Qt::Key_Escape`` = OFF (13H), ``Qt::Key_Tab`` = 09H,
``Qt::Key_F1``…``F8`` = Sondertaste 0…7.  Vor jedem Anschlag sendet der Kern das Präfix 1CH
(bzw. 1EH bei Sondertasten) [Annahme, Merkposten M3].

**Tasten ohne bekannten Code** (Kursor ↑ ↓ ← →, CL, CRLF, ÷, 00 und die unbeschriftete
Taste des Ziffernblocks) lassen sich drücken, senden aber nichts — ihre Codes stehen in
keiner vorliegenden Unterlage [?].  Welche der acht Steuertasten (21) welche Sondertaste
0…7 ist, ist ebenfalls nicht belegt; die Nachbildung zählt zeilenweise [?].

**Anzeigen:** 17 Umschaltanzeige (folgt dem Feststeller der Nachbildung), 18 CTRL-Anzeige
(folgt der angeklickten CTRL-Taste), 20 Bereitschaft der Tastatur (Bit 0 von
``k1520_keyboard_leds`` = 14H/12H vom Rechner [?]).
"""

from __future__ import annotations

from typing import List, Optional, Tuple

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor

from app.ui.keyboard import KeyboardWidget, _Key, qt_event_to_core_key

#: Kerncodes der Sondertasten (Qt-Werte, ``Mc80Tastatur::qtTaste``).
KEY_RETURN = int(Qt.Key_Return)       # ENTER → 11H
KEY_ESCAPE = int(Qt.Key_Escape)       # OFF   → 13H
KEY_TAB = int(Qt.Key_Tab)             # 09H
KEY_F1 = int(Qt.Key_F1)               # Sondertaste n = Key_F1 + n

LED_BEREIT = 0x01                     # Bit 0 der Anzeigemaske [?]

_C_WANNE = QColor(0x9c, 0x9a, 0x92)


def sondertaste(n: int) -> int:
    """Kerncode der Sondertaste *n* (0…7)."""
    return KEY_F1 + n


def _z(x, y, low, up="", name="", w=1.0, shape="round", style="dark") -> _Key:
    """Zeichentaste: unten = Grundebene, oben = Umschaltebene (Buchstaben: klein/groß)."""
    if len(low) == 1 and low.isalpha():
        code, shift_code, unten, oben = ord(low.lower()), ord(low.upper()), low.upper(), ""
    else:
        code = ord(low)
        shift_code = ord(up) if up and len(up) == 1 else None
        unten, oben = low, up
    return _Key(x=x, y=y, low=unten, up=oben, code=code, shift_code=shift_code, w=w,
                shape=shape, style=style, name=name or (f"{up} / {low}" if up else low))


def _s(x, y, low, code, name, w=1.0, shape="round", style="light") -> _Key:
    """Sondertaste mit festem Kerncode (kein Umschalten)."""
    return _Key(x=x, y=y, low=low, code=code, w=w, shape=shape, style=style, name=name)


def _tot(x, y, low, name, w=1.0, shape="round", style="light") -> _Key:
    """Taste, deren Code unbekannt ist: drückbar, sendet nichts [?]."""
    return _Key(x=x, y=y, low=low, w=w, shape=shape, style=style, kind="dead", name=name)


def _build_layout_mc80() -> List[_Key]:
    r"""Das Tastenfeld nach Bild A5; Raster 1.0 = eine Taste.

    ```
    y 0     @ 1 2 3 4 5 6 7 8 9 0 - ^ ↑ CL (20)     S0 S1     CL ÷ * =
    y 1   CTRL Q W E R T Y U I O P ] ← → ⇥          S2 S3     7  8 9 -
    y 2 (18) ⇩ A S D F G H J K L \ [ ↓ CRLF         S4 S5     4  5 6 +
    y 3 (17)  ⇧ Z X C V B N M , . / ; : ⇧           S6 S7     1  2 3 □
    y 4         OFF  Leertaste  ENTER                         0 00 , ↵
    ```
    """
    k: List[_Key] = []
    # ── Reihe 0: Ziffern ─────────────────────────────────────────────────────
    reihe0 = (("@", "`"), ("1", "!"), ("2", '"'), ("3", "#"), ("4", "¤"), ("5", "%"),
              ("6", "&"), ("7", "'"), ("8", "("), ("9", ")"), ("0", "_"), ("-", "="),
              ("^", "~"))
    for i, (low, up) in enumerate(reihe0):
        if up == "¤":
            # „¤“ ist kein ASCII: in ISO 646 (IRV 1973) liegt es auf 24H [?]
            t = _z(1.0 + i, 0.0, low, "$", name="¤ / 4 (Code 24H [?])")
            t.up = "¤"
            k.append(t)
        else:
            k.append(_z(1.0 + i, 0.0, low, up))
    k.append(_tot(14.0, 0.0, "↑", "Kursor hoch — Code unbekannt [?]"))
    k.append(_tot(15.0, 0.0, "CL", "CL (Löschen) — Code unbekannt [?]"))
    # ── Reihe 1 ──────────────────────────────────────────────────────────────
    k.append(_Key(x=0.0, y=1.0, low="CTRL", w=1.5, shape="oval", style="light", kind="ctrl",
                  name="Control-Taste (19) — wirkt auf die nächste Taste"))
    for i, ch in enumerate("QWERTYUIOP"):
        k.append(_z(1.5 + i, 1.0, ch))
    k.append(_z(11.5, 1.0, "]", "}"))
    k.append(_tot(12.5, 1.0, "←", "Kursor links — Code unbekannt [?]"))
    k.append(_tot(13.5, 1.0, "→", "Kursor rechts — Code unbekannt [?]"))
    k.append(_s(14.5, 1.0, "⇥", KEY_TAB, "Tabulator (09H)"))
    # ── Reihe 2 ──────────────────────────────────────────────────────────────
    k.append(_Key(x=0.8, y=2.0, low="⇩", style="light", kind="lock",
                  name="Feststeller (22) für die Umschalttasten — Anzeige 17"))
    for i, ch in enumerate("ASDFGHJKL"):
        k.append(_z(1.8 + i, 2.0, ch))
    k.append(_z(10.8, 2.0, "\\", "|"))
    k.append(_z(11.8, 2.0, "[", "{"))
    k.append(_tot(12.8, 2.0, "↓", "Kursor runter — Code unbekannt [?]"))
    k.append(_tot(13.8, 2.0, "CRLF", "CRLF-Taste (23) — Code unbekannt [?]", w=1.7,
                  shape="oval"))
    # ── Reihe 3 ──────────────────────────────────────────────────────────────
    k.append(_Key(x=0.8, y=3.0, low="⇧", w=1.6, shape="oval", style="light", kind="shift",
                  name="Umschalttaste (16) für die zweite Ebene — Anzeige 17"))
    for i, ch in enumerate("ZXCVBNM"):
        k.append(_z(2.6 + i, 3.0, ch))
    for i, (low, up) in enumerate(((",", "<"), (".", ">"), ("/", "?"), (";", "+"),
                                   (":", "*"))):
        k.append(_z(9.6 + i, 3.0, low, up))
    k.append(_Key(x=14.6, y=3.0, low="⇧", style="light", kind="shift",
                  name="Umschalttaste (16) rechts"))
    # ── Reihe 4 ──────────────────────────────────────────────────────────────
    k.append(_s(2.8, 4.0, "OFF", KEY_ESCAPE, "OFF / Abbruch (13H)", w=1.2, shape="oval"))
    k.append(_Key(x=4.2, y=4.0, low="", code=0x20, w=8.0, shape="oval", name="Leertaste"))
    k.append(_s(12.4, 4.0, "ENTER", KEY_RETURN, "ENTER (11H)", w=1.8, shape="oval"))

    # ── Steuertasten (21): Sondertasten 0…7 ──────────────────────────────────
    X = 16.9
    for n in range(8):
        k.append(_s(X + n % 2, float(n // 2), f"S{n}", sondertaste(n),
                    f"Steuertaste (Sondertaste {n}, Aussprung FED0H+3·{n}) — "
                    f"Zählung der Nachbildung [?]; Wirtstastatur F{n + 1}",
                    shape="rect"))
    # ── Ziffernblock (20 Tasten) ─────────────────────────────────────────────
    Z = 19.4
    k.append(_tot(Z, 0.0, "CL", "Ziffernblock CL — Code unbekannt [?]", shape="rect"))
    k.append(_tot(Z + 1, 0.0, "÷", "Ziffernblock ÷ — Code unbekannt [?]", shape="rect"))
    for i, ch in enumerate("*="):
        k.append(_z(Z + 2 + i, 0.0, ch, name=f"Ziffernblock {ch}", shape="rect"))
    block = (("7", "8", "9", "-"), ("4", "5", "6", "+"), ("1", "2", "3", None))
    for r, zeile in enumerate(block):
        for c, ch in enumerate(zeile):
            if ch is None:
                k.append(_tot(Z + c, 1.0 + r, "", "Ziffernblock, unbeschriftet — sendet nichts [?]",
                              shape="rect"))
            else:
                k.append(_z(Z + c, 1.0 + r, ch, name=f"Ziffernblock {ch}", shape="rect"))
    k.append(_z(Z, 4.0, "0", name="Ziffernblock 0", shape="rect"))
    k.append(_tot(Z + 1, 4.0, "00", "Ziffernblock 00 — Code unbekannt [?]", shape="rect"))
    k.append(_z(Z + 2, 4.0, ",", name="Ziffernblock ,", shape="rect"))
    k.append(_s(Z + 3, 4.0, "↵", KEY_RETURN, "Ziffernblock ↵ (als ENTER [?])", shape="rect"))
    return k


class KeyboardMc80Widget(KeyboardWidget):
    """Anklickbare Nachbildung der MC80.3x-Tastatur — Schnittstelle wie die K7637."""

    WANNE = _C_WANNE
    PAD = (0.9, 0.35, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_mc80()

    def _anzeigen_verankern(self):
        self._lock_taste = next(k for k in self._keys if k.kind == "lock")
        self._ctrl_taste = next(k for k in self._keys if k.kind == "ctrl")
        self._shift_taste = next(k for k in self._keys if k.kind == "shift")
        self._cl_taste = next(k for k in self._keys if k.low == "CL" and k.y == 0.0 and k.x < 16)

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Anzeigen 18 (CTRL), 17 (Umschaltung) links am Feld, 20 (Bereitschaft) neben CL."""
        spots = []

        def neben(taste, dx, an, name):
            r = self._rect_of(taste, unit, ox, oy)
            x = (r.left() if dx < 0 else r.right()) + dx * unit
            spots.append((x, r.center().y(), an, name))

        neben(self._lock_taste, -0.45, self._ctrl, "Anzeige CTRL (18)")
        neben(self._shift_taste, -0.45, self.lock_active(), "Umschaltanzeige (17)")
        neben(self._cl_taste, 0.35, bool(self._leds & LED_BEREIT),
              "Bereitschaft der Tastatur (20) [?]")
        return spots

    # ── Wirtstastatur ───────────────────────────────────────────────────────

    def map_host_key(self, event) -> Optional[Tuple[int, bool, bool]]:
        """Host-Taste → Kern-Tripel: Zeichen wie getippt, ENTER/OFF/Tab/F1–F8 als Qt-Codes.

        Anders als bei den Matrix-Tastaturen gibt es hier keine Taste der Nachbildung, deren
        Code an Stelle des Wirtscodes ginge — der Kern löst die Qt-Codes selbst auf.  Der
        Feststeller der Nachbildung macht aus Buchstaben Großbuchstaben, die angeklickte
        CTRL-Taste wirkt wie die des Wirts.
        """
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return None
        code, shift, ctrl = mapped
        ctrl = ctrl or self._ctrl
        if (shift or self.schicht()) and 0x61 <= code <= 0x7A and not ctrl:
            code -= 0x20
            shift = True
        return (code, shift, ctrl)

    _HOST_NAME = {int(Qt.Key_Return): "ENTER", int(Qt.Key_Enter): "ENTER",
                  int(Qt.Key_Escape): "OFF", int(Qt.Key_Tab): "⇥",
                  int(Qt.Key_Up): "↑", int(Qt.Key_Down): "↓", int(Qt.Key_Left): "←",
                  int(Qt.Key_Right): "→", int(Qt.Key_Space): ""}

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [self._ctrl_taste]
        if key == int(Qt.Key_CapsLock):
            return [self._lock_taste]
        if int(Qt.Key_F1) <= key <= int(Qt.Key_F8):
            return self._by_code.get(key, [])[:1]
        if key in self._HOST_NAME:
            name = self._HOST_NAME[key]
            if key == int(Qt.Key_Space):
                return self._by_code.get(0x20, [])[:1]
            return [k for k in self._keys if k.low == name and k.x < 16][:1]
        return super()._keys_for_host_event(event)

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "ctrl", "lock", "dead") or key.code is None:
            return name
        if key.code >= 0x01000000:
            return name
        t = f"{name}: Zeichen {key.code:02X}H"
        if key.shift_code is not None and key.shift_code != key.code:
            t += f", umgeschaltet {key.shift_code:02X}H"
        return t + " (Präfix 1CH)"
