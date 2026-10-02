"""
PRG710 Emulator — Bildschirmtastatur K7609 (PRG 710)
====================================================

Die Tastatur des PRG 710 hängt an einem 8279 (ATP 590068); sie liefert je Taste
einen Code 00H–3FH, Bit 6 ist die Umschaltung (`doc/prg710/k7609_codes.csv`,
`doc/design/20_prg710.md` §3.10, AP-P2a).  Jede Taste schickt
``QK_TASTE_BASE | Code`` an den Kern (``K7609::QK_TASTE_BASE``, derselbe Wert wie
bei der K7672); die Umschaltung der Nachbildung wird als ``shift`` mitgegeben, die
rastende Strg-Taste als ``ctrl`` (der Kern stellt dann die Strg-Einmaltaste 28H vor).

**Das Tastenbild ist NICHT vermessen** (§8.7 [Gerät]): die Codes stimmen, die
Lage der Kappen ist eine plausible Anordnung nach der Codetabelle.  Offen **[?]**:

* **S1–S9 und CL** haben keine eigenen Codes (die Codes 17H/1DH–1FH/3DH–3FH liefern
  alle 20H) — die Kappen sind da, **senden aber nichts** und tragen ein „[?]“.
* **Leertaste = 16H** nur als vorläufige Wahl, **BS = 08H** ebenso.
* Polarität der Umschaltung (Bit 6) ist Abmachung („ohne Shift Bit 6 = 0“).

**ET1 (37H) und ET2/ST (38H) sind Tasten wie jede andere** — ohne Kürzel erreichbar,
und die Hosttaste Return liefert ET1 (Starttaste des Boot-ROMs), Esc ET2.

Die Schnittstelle ist die der K7637-Nachbildung (:class:`~app.ui.keyboard.KeyboardWidget`):
``keyPressed``/``keyReleased``, ``set_leds`` (hier ohne Anzeigen), ``host_key_press``.
Am PRG 710 ist ``keyRelease`` wirkungslos (kein Loslass-Code, keine Wiederholung),
wird aber gemeldet — dieselbe Oberfläche fährt auch das 710-1 (K7672 wiederholt sonst).
"""

from typing import List, Optional, Tuple

from PySide6.QtCore import Qt

from app.ui.keyboard import KeyboardWidget, _Key, qt_event_to_core_key

#: Muss ``K7609::QK_TASTE_BASE`` entsprechen (``core/peripherals/k7609/k7609.h``).
TASTE_BASE = 0x03000000
#: ET1 (CR, Starttaste des Boot-ROMs) und ET2/ST.
ET1, ET2 = 0x37, 0x38
#: Vorläufige Wahl (§8.7).
LEERTASTE, BACKSPACE = 0x16, 0x08


def taste(code: int) -> int:
    """Tastenposition (00H–3FH) in einen Kern-Keycode packen."""
    return TASTE_BASE | (code & 0x3F)


#: (Code, Grundzeichen, Umschaltzeichen) der Zeichentasten — aus
#: ``doc/prg710/k7609_codes.csv`` (Wächter ``test_k7609_table_matches_the_csv``).
ZEICHEN = (
    (0x00, "1", ":"), (0x01, "2", "/"), (0x02, "3", "*"), (0x03, "4", "("),
    (0x04, "5", ")"), (0x05, "6", "="), (0x06, "7", "."), (0x07, "8", "+"),
    (0x09, "$", "&"), (0x0A, "-", "!"), (0x0B, "0", "'"), (0x0C, "9", "?"),
    (0x0D, ">", "<"), (0x0E, "]", "}"), (0x0F, "[", "{"), (0x11, "%", "_"),
    (0x12, ",", ";"), (0x13, "P", "p"), (0x14, "O", "o"), (0x15, "@", "`"),
    (0x1A, "#", '"'), (0x1B, "L", "l"), (0x1C, "K", "k"),
    (0x20, "Q", "q"), (0x21, "W", "w"), (0x22, "E", "e"), (0x23, "R", "r"),
    (0x24, "T", "t"), (0x25, "Z", "z"), (0x26, "U", "u"), (0x27, "I", "i"),
    (0x29, "A", "a"), (0x2A, "S", "s"), (0x2B, "D", "d"), (0x2C, "F", "f"),
    (0x2D, "G", "g"), (0x2E, "H", "h"), (0x2F, "J", "j"),
    (0x30, "Y", "y"), (0x31, "X", "x"), (0x32, "C", "c"), (0x33, "V", "v"),
    (0x34, "B", "b"), (0x35, "N", "n"), (0x36, "M", "m"),
)
_ZEICHEN = {c: (lo, up) for c, lo, up in ZEICHEN}


class _Taste(_Key):
    """Taste der K7609 — dazu ihr Code (für Kurzhinweis und Host-Zuordnung)."""

    def __init__(self, *a, pos: int = -1, hinweis: str = "", **kw):
        super().__init__(*a, **kw)
        self.pos = pos
        self.hinweis = hinweis


def _k(x, y, pos, low, up="", w=1.0, name="", style="dark", kind="normal",
       dead=False, hinweis="") -> _Taste:
    code = None if (dead or kind != "normal") else taste(pos)
    t = _Taste(x=x, y=y, low=low, up=up, code=code, shift_code=code, w=w,
               style=style, shape="rect", kind="dead" if dead else kind,
               name=name or (f"{up} / {low}" if up else low), pos=pos,
               hinweis=hinweis)
    return t


def _build_layout_k7609() -> List[_Taste]:
    r"""Tastenfeld nach der Codetabelle (Raster, 1.0 = eine Taste) — Lage geschätzt [?].

    ```
    y 0    S1 … S9 CL                      (sendet nichts, [?])
    y 1.5  1 2 3 4 5 6 7 8 9 0 - $ % ,  BS            MRK DRUCK
    y 2.5  →| Q W E R T Z U I O P @ [ ]               ET2
    y 3.5  STRG A S D F G H J K L # >  ET1              ↑
    y 4.5  UMSCH Y X C V B N M                        ← ↓ →
    y 5.5       Leertaste [?]
    ```
    """
    k: List[_Taste] = []
    nicht = "sendet nichts — Lage und Code nicht bekannt [?]"
    for i in range(9):
        k.append(_k(i, 0.0, -1, f"S{i + 1}", dead=True, style="light",
                    name=f"S{i + 1} [?] — {nicht}", hinweis=nicht))
    k.append(_k(9.5, 0.0, -1, "CL", dead=True, style="light",
                name=f"CL [?] — {nicht}", hinweis=nicht))

    reihe1 = (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0B, 0x0A,
              0x09, 0x11, 0x12)
    for i, c in enumerate(reihe1):
        lo, up = _ZEICHEN[c]
        k.append(_k(float(i), 1.5, c, lo, up))
    k.append(_k(14.0, 1.5, BACKSPACE, "BS", w=1.5, name="BS |←| (08H, vorläufig [?])"))

    k.append(_k(0.0, 2.5, 0x3C, "→|", w=1.5, name="Tabulator →|"))
    for i, c in enumerate((0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x14, 0x13,
                           0x15, 0x0F, 0x0E)):
        lo, up = _ZEICHEN[c]
        k.append(_k(1.5 + i, 2.5, c, lo, up))

    k.append(_k(0.0, 3.5, 0x28, "STRG", w=1.75, kind="ctrl",
                name="STRG (Strg-Einmaltaste 28H, rastet für eine Taste)"))
    for i, c in enumerate((0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x1C, 0x1B,
                           0x1A, 0x0D)):
        lo, up = _ZEICHEN[c]
        k.append(_k(1.75 + i, 3.5, c, lo, up))
    k.append(_k(12.75, 3.5, ET1, "ET1", w=1.75, style="red",
                name="ET1 (CR, 37H) — Starttaste des Boot-ROMs"))

    k.append(_k(0.0, 4.5, -1, "UMSCH", w=2.25, kind="shift",
                name="UMSCH (Umschaltung, Bit 6; rastet für eine Taste)"))
    for i, c in enumerate((0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36)):
        lo, up = _ZEICHEN[c]
        k.append(_k(2.25 + i, 4.5, c, lo, up))

    k.append(_k(3.0, 5.5, LEERTASTE, "Leertaste [?]", w=6.0,
                name="Leertaste (16H, vorläufig [?])"))

    # Rechter Block: Sonder-/Umschaltertasten, ET2, Cursor.
    RB = 16.5
    k.append(_k(RB, 1.5, 0x10, "MRK", name="Sondertaste Merker (10H) [?]"))
    k.append(_k(RB + 1, 1.5, 0x18, "DRUCK", name="Drucker-Umschalter (18H, UDOS) [?]"))
    k.append(_k(RB + 1, 2.5, ET2, "ET2", style="red", name="ET2/ST (ESC, 38H)"))
    k.append(_k(RB + 1, 3.5, 0x19, "↑", name="Cursor hoch (19H)"))
    k.append(_k(RB, 4.5, 0x3A, "←", name="Cursor links (3AH)"))
    k.append(_k(RB + 1, 4.5, 0x3B, "↓", name="Cursor runter (3BH)"))
    k.append(_k(RB + 2, 4.5, 0x39, "→", name="Cursor rechts (39H)"))
    return k


#: Hosttasten, die auf eine bestimmte Taste zeigen (Hervorhebung).
_HOST_POS = {
    int(Qt.Key_Return): ET1, int(Qt.Key_Enter): ET1, int(Qt.Key_Escape): ET2,
    int(Qt.Key_Tab): 0x3C, int(Qt.Key_Backtab): 0x3C,
    int(Qt.Key_Backspace): BACKSPACE, int(Qt.Key_Space): LEERTASTE,
    int(Qt.Key_Up): 0x19, int(Qt.Key_Down): 0x3B,
    int(Qt.Key_Left): 0x3A, int(Qt.Key_Right): 0x39,
}


class KeyboardK7609Widget(KeyboardWidget):
    """Anklickbare Nachbildung der K7609 (PRG 710) — Schnittstelle wie die K7637."""

    PAD = (0.35, 0.35, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_k7609()

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys
                        if isinstance(k, _Taste) and k.pos >= 0}

    def _led_spots(self, unit, ox, oy):
        return []                       # die K7609 hat keine Anzeigen

    def set_leds(self, mask: int):
        """Keine Anzeigen — nichts zu tun (Schnittstelle der anderen Tastaturen)."""
        return

    def lock_active(self) -> bool:
        return False

    def _note_host_caps(self, event):
        return

    def map_host_key(self, event) -> Optional[Tuple[int, bool, bool]]:
        """Host-Taste → ``(code, shift, ctrl)`` wie der Kern sie für die K7609 übersetzt
        (`K7609`: ASCII, Qt-Return = ET1, Esc = ET2, Cursor), dazu die rastenden
        Tasten der Nachbildung."""
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return None
        code, shift, ctrl = mapped
        ctrl = ctrl or self._ctrl
        if self._shift and 0x61 <= code <= 0x7A and not ctrl:
            code -= 0x20
            shift = True
        return (code, shift, ctrl)

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [k for k in self._keys if k.kind == "ctrl"]
        if key in _HOST_POS:
            t = self._by_pos.get(_HOST_POS[key])
            return [t] if t else []
        text = event.text()
        if len(text) == 1:
            for pos, (lo, up) in _ZEICHEN.items():
                if text in (lo, up, lo.lower()):
                    return [self._by_pos[pos]]
        return []

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "ctrl", "dead"):
            return name
        return f"{name}: Code {getattr(key, 'pos', -1):02X}H"
