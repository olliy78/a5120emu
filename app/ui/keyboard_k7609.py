"""
PRG710 Emulator — Bildschirmtastatur K7609 (PRG 710)
====================================================

Die Tastatur des PRG 710 hängt an einem 8279 (ATP 590068); sie liefert je Taste
einen Code 00H–3FH, Bit 6 ist die Umschaltung (`doc/prg710/k7609_codes.csv`,
`doc/design/20_prg710.md` §3.10, AP-P2a).  Jede Taste schickt
``QK_TASTE_BASE | Code`` an den Kern (``K7609::QK_TASTE_BASE``, derselbe Wert wie
bei der K7672); die Umschaltung der Nachbildung wird als ``shift`` mitgegeben, die
rastende Strg-Taste als ``ctrl`` (der Kern stellt dann die Strg-Einmaltaste 28H vor).

**Das Tastenbild folgt dem Foto des Geräts** (2026-10-05): Lage, Größe und Form der
Kappen, Blindmodule und Beschriftung sind dort abgemessen (Einzelheiten und die
Annahmen **[?]** zur Belegung der unbeschrifteten Kappen: :func:`_build_layout_k7609`).
Die früher vermuteten S1–S9 hat diese Tastatur nicht; ihre Funktionstasten
``+1 -1 FC BA FW CL`` haben in UDOS/SCPX keinen Code und **senden nichts**.
**Leertaste = 16H** bleibt vorläufige Wahl (alle 20H-Codes wirken gleich); die
Polarität der Umschaltung (Bit 6) ist Abmachung („ohne Shift Bit 6 = 0“).

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
       dead=False, hinweis="", shape="round") -> _Taste:
    code = None if (dead or kind != "normal") else taste(pos)
    t = _Taste(x=x, y=y, low=low, up=up, code=code, shift_code=code, w=w,
               style=style, shape=shape, kind="dead" if dead else kind,
               name=name or (f"{up} / {low}" if up else low), pos=pos,
               hinweis=hinweis)
    return t


def _blind(x, y, w=0.5) -> _Taste:
    """Blindmodul ohne Kappe — reagiert nicht."""
    return _Taste(x=x, y=y, w=w, style="filler", kind="dead", name="Blindmodul")


#: Die ungeklärte Belegung beschrifteter Funktionstasten (Foto 2026-10-05).
_NICHT = ("sendet nichts — Funktionstaste des PRG-Betriebs (BS610), in UDOS/SCPX "
          "ohne Code; welche Matrixposition sie hat, ist offen [?]")


def _build_layout_k7609() -> List[_Taste]:
    r"""Tastenfeld nach dem Foto des Geräts (2026-10-05) — Lage aus den Pixelkoordinaten
    abgemessen (Raster 1.0 ≈ 57 px im 1600-px-Foto).

    ```
    y 0  ▯ 1 2 3 4 5 6 7 8 9 0 -  ■   +1 FC
    y 1  ■  Q W E R T Z U I O P ,  ▯   -1 BA        7 8 9 [
    y 2  CL A S D F G H J K L #  ■ ▯   ↑  FW        4 5 6 ]
    y 3  ▯■  Y X C V B N M ↵ TB  ■ ▯  ←  →  ▯       1 2 3 >
    y 4  ⬭  ▯ ════ Leertaste ════ ▯ ⬭ ▯ ↓  ST       ══0══ @
    ```
    ``■`` = unbeschriftete Kappe, ``▯`` = Blindmodul, ``⬭`` = unbeschriftete ovale Kappe.

    **Gesichert** ist, was beschriftet ist und einen Code der Tabelle trägt: Zeichen,
    Pfeile, ``↵`` = ET1 (37H), ``ST`` = ET2/ST (38H), ``TB`` = Tabulator (3CH).  Der
    Ziffernblock ist der Matrix parallel geschaltet (eine Ziffer, ein Code); ``[ ] > @``
    gibt es nur dort.  **Angenommen [?]**: die beiden Ovale unten sind die Umschaltung
    (Bit 6), die unbeschrifteten Kappen tragen die Codes ohne Aufschrift — STRG 28H
    links von Q (wie beim PC 1715), BS 08H neben ``-``, DRUCK 18H neben ``#``, MRK 10H
    links von Y; die neben TB sendet nichts.  ``+1 -1 FC BA FW CL`` sind Funktionstasten
    des PRG-Betriebs ohne Code in UDOS/SCPX (§8.7: die früher vermuteten S1–S9 hat
    diese Tastatur nicht) und senden nichts.  ``$`` (09H) und ``%`` (11H) haben auf dem
    Foto keine Kappe; die Host-Tastatur erreicht sie weiter.
    """
    k: List[_Taste] = []

    def zt(x, y, c, nur_grund=False):
        lo, up = _ZEICHEN[c]
        if lo.isalpha() or nur_grund:
            k.append(_k(x, y, c, lo, name=f"{lo} / {up}"))
        else:
            k.append(_k(x, y, c, lo, up))

    def tot(x, y, lo):
        k.append(_k(x, y, -1, lo, dead=True, name=f"{lo} [?] — {_NICHT}", hinweis=_NICHT))

    # Reihe 0.
    k.append(_blind(0.0, 0.0, 0.4))
    for i, c in enumerate((0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0B, 0x0A)):
        zt(0.4 + i, 0.0, c)
    k.append(_k(11.4, 0.0, BACKSPACE, "", w=0.9, shape="rect",
                name="unbeschriftet — BS |←| (08H) [?]"))
    tot(12.3, 0.0, "+1")
    tot(13.3, 0.0, "FC")

    # Reihe 1.
    k.append(_k(0.0, 1.0, 0x28, "", w=0.85, kind="ctrl", shape="rect",
                name="unbeschriftet — STRG (Strg-Einmaltaste 28H, rastet für eine Taste) [?]"))
    for i, c in enumerate((0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x14, 0x13, 0x12)):
        zt(0.85 + i, 1.0, c)
    k.append(_blind(11.85, 1.0, 0.45))
    tot(12.3, 1.0, "-1")
    tot(13.3, 1.0, "BA")

    # Reihe 2.
    tot(0.1, 2.0, "CL")
    for i, c in enumerate((0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x1C, 0x1B, 0x1A)):
        zt(1.1 + i, 2.0, c)
    k.append(_k(11.1, 2.0, 0x18, "", shape="rect",
                name="unbeschriftet — Drucker-Umschalter (18H, UDOS) [?]"))
    k.append(_blind(12.1, 2.0, 0.4))
    k.append(_k(12.5, 2.0, 0x19, "↑", name="Cursor hoch (19H)"))
    tot(13.5, 2.0, "FW")

    # Reihe 3.
    k.append(_blind(0.0, 3.0))
    k.append(_k(0.5, 3.0, 0x10, "", shape="rect",
                name="unbeschriftet — Sondertaste Merker (10H) [?]"))
    for i, c in enumerate((0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36)):
        zt(1.5 + i, 3.0, c)
    k.append(_k(8.5, 3.0, ET1, "↵", name="↵ = ET1 (CR, 37H) — Starttaste des Boot-ROMs"))
    k.append(_k(9.5, 3.0, 0x3C, "TB", name="TB = Tabulator →| (3CH)"))
    k.append(_k(10.5, 3.0, -1, "", shape="rect", dead=True,
                name="unbeschriftet [?] — sendet nichts", hinweis="Belegung offen [?]"))
    k.append(_blind(11.5, 3.0))
    k.append(_k(12.0, 3.0, 0x3A, "←", name="Cursor links (3AH)"))
    k.append(_k(13.0, 3.0, 0x39, "→", name="Cursor rechts (39H)"))
    k.append(_blind(14.0, 3.0))

    # Reihe 4: Umschaltung links und rechts der Leertaste.
    k.append(_k(0.0, 4.0, -1, "", w=1.5, kind="shift", shape="oval",
                name="unbeschriftet — Umschaltung (Bit 6; rastet für eine Taste) [?]"))
    k.append(_blind(1.5, 4.0))
    k.append(_k(2.0, 4.0, LEERTASTE, "", w=8.0, shape="oval",
                name="Leertaste (16H, vorläufig [?])"))
    k.append(_blind(10.0, 4.0))
    k.append(_k(10.5, 4.0, -1, "", w=1.5, kind="shift", shape="oval",
                name="unbeschriftet — Umschaltung (Bit 6; rastet für eine Taste) [?]"))
    k.append(_blind(12.0, 4.0))
    k.append(_k(12.5, 4.0, 0x3B, "↓", name="Cursor runter (3BH)"))
    k.append(_k(13.5, 4.0, ET2, "ST", name="ST = ET2/ST (ESC, 38H)"))

    # Ziffernblock (der Matrix parallel: dieselben Codes wie die Ziffernreihe).
    ZB = 15.8
    for dy, reihe in ((1, (0x06, 0x07, 0x0C, 0x0F)), (2, (0x03, 0x04, 0x05, 0x0E)),
                      (3, (0x00, 0x01, 0x02, 0x0D))):
        for dx, c in enumerate(reihe):
            zt(ZB + dx, float(dy), c, nur_grund=dx < 3)   # Ziffern einfach beschriftet
    k.append(_k(ZB, 4.0, 0x0B, "0", w=3.0, shape="oval", name="0 (Ziffernblock, 0BH)"))
    zt(ZB + 3, 4.0, 0x15)
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
        # Erste Kappe je Code: die Ziffern des Ziffernblocks sind der Ziffernreihe
        # parallel geschaltet, hervorgehoben wird die Ziffernreihe.
        self._by_pos = {}
        for k in self._keys:
            if isinstance(k, _Taste) and k.pos >= 0:
                self._by_pos.setdefault(k.pos, k)

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
