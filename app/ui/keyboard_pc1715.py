"""
PC1715 Emulator — Bildschirmtastatur des PC 1715 / PC 1715W
===========================================================

Die Tastatur des PC 1715 ist eine Matrix aus 13 Spalten und 8 Zeilen, die ein eigener
U880 mit dem EPROM S600 abfragt (`doc/pc1715/tastatur.md` §6).  Jede Kappe schickt
``QK_TASTE_BASE | (Spalte * 8 + Zeile)`` an den Kern (``Pc1715Machine::QK_TASTE_BASE``) —
die **physische Taste**; welches Zeichen daraus wird, entscheidet das ROM nach dem Stand
von Shift, LOCK und SI/SO, nicht die Oberfläche.

Drei Arten von Umschalttasten, so wie sie am Gerät wirken:

* **SHIFT, CTRL, REP** wirken nur, solange sie GEHALTEN werden.  Die Nachbildung merkt
  sie sich beim Anklicken (Rahmen leuchtet) und drückt sie beim nächsten Tastenklick
  **vor** der Zeichentaste (das ROM sendet nichts, wenn zwei Tasten im selben
  Abfragedurchlauf neu erscheinen — die Warteschlange des Kerns hält den Abstand ein);
  nach dem Loslassen der Zeichentaste werden sie wieder gelöst.
* **LOCK und SI/SO** rasten im ROM selbst: ein Klick ist ein Tastendruck, der Zustand
  wird nur mitgezeichnet (er beginnt „aus“; ein Reset der Maschine setzt das ROM der
  Tastatur nicht zurück — wer ihn kennt, sieht ihn an der Statuszeile von CP/A).
* Alle übrigen sind gewöhnliche Tasten.

**Das Tastenbild ist NICHT vermessen**: Matrixpositionen und Codes stimmen (Tabelle
§6 des Befunds), die Lage der Kappen ist eine plausible Anordnung nach der Beschriftung
der Codes.  Ziffernblock-Tasten ``S`` (zweimal in der Matrix) und ``CE``/``00`` tragen
ihre Beschriftung nach dem CP/A-BIOS.

Die Host-Tastatur geht den Weg des Kerns: druckbares ASCII, Return/Escape/Tab/Rücktaste
als Zeichen (das ROM-Verhalten ergibt sich daraus), Pfeile, Entf, Einfg und F1…F12 als
physische Tasten — der Kern kennt für sie keine Zeichen.
"""

from typing import Dict, List, Optional, Tuple

from PySide6.QtCore import Qt

from app.ui.keyboard import KeyboardWidget, _Key, qt_event_to_core_key

#: Muss ``Pc1715Machine::QK_TASTE_BASE`` entsprechen (core/machines/pc1715/pc1715.h).
TASTE_BASE = 0x03000000


def pos(spalte: int, zeile: int) -> int:
    """Matrixposition → Tastenkennung ohne Basis (``Spalte * 8 + Zeile``)."""
    return spalte * 8 + zeile


def taste(position: int) -> int:
    """Matrixposition → Kern-Keycode."""
    return TASTE_BASE | (position & 0x7F)


#: Umschalttasten (Spalte 8): CTRL (8,0), Shift links (8,1) / rechts (8,4), LOCK (8,5),
#: REP (8,6), SI/SO (8,7).
CTRL, SHIFT_L, SHIFT_R = pos(8, 0), pos(8, 1), pos(8, 4)
LOCK, REP, SISO = pos(8, 5), pos(8, 6), pos(8, 7)
#: Die Taste, die das ROM für Return sendet (ET, 9EH — `Pc1715Tastatur.SondertastenReturnEscapeTab`).
ET = pos(3, 4)

#: Zeichentasten: (Spalte, Zeile, Grundzeichen, Umschaltzeichen) — Tabelle S600.
ZEICHEN = (
    (0, 0, "r", "R"), (0, 2, "4", "$"), (0, 4, "v", "V"), (0, 5, "f", "F"),
    (1, 0, "e", "E"), (1, 2, "3", "#"), (1, 4, "c", "C"), (1, 5, "d", "D"),
    (2, 0, "w", "W"), (2, 2, "2", '"'), (2, 4, "x", "X"), (2, 5, "s", "S"),
    (3, 0, "@", "`"), (3, 2, "-", "="), (3, 5, ":", "*"),
    (4, 0, "p", "P"), (4, 2, "0", "_"), (4, 4, "/", "?"), (4, 5, ";", "+"),
    (5, 0, "[", "{"), (5, 2, "^", "~"), (5, 4, "z", "Z"), (5, 5, "]", "}"),
    (6, 0, "u", "U"), (6, 2, "7", "'"), (6, 4, "m", "M"), (6, 5, "j", "J"),
    (7, 0, "q", "Q"), (7, 2, "1", "!"), (7, 4, "\\", "|"), (7, 5, "a", "A"),
    (9, 0, "i", "I"), (9, 2, "8", "("), (9, 4, ",", "<"), (9, 5, "k", "K"),
    (10, 0, "o", "O"), (10, 2, "9", ")"), (10, 4, ".", ">"), (10, 5, "l", "L"),
    (11, 0, "t", "T"), (11, 2, "5", "%"), (11, 4, "b", "B"), (11, 5, "g", "G"),
    (12, 0, "y", "Y"), (12, 2, "6", "&"), (12, 4, "n", "N"), (12, 5, "h", "H"),
)
_ZEICHEN: Dict[int, Tuple[str, str]] = {pos(c, z): (lo, up) for c, z, lo, up in ZEICHEN}

#: TAST_618 (QWERTZ, AP-6): die Positionen, an denen sich das ROM vom S600 unterscheidet —
#: Position → (unverschoben, Shift).  Gegen das ROM geprüft von
#: ``Tastatur1715.Tast618ZeichentabelleStimmtMitDemRomUeberein`` (C++, dieselbe Liste) und
#: `doc/pc1715/tastatur.md` §6.  Alle übrigen Tasten sind wie beim S600.
ZEICHEN_618_ABWEICHUNG: Dict[int, Tuple[str, str]] = {
    pos(1, 2): ("3", "@"), pos(3, 0): ("}", "]"), pos(3, 2): ("~", "+"),
    pos(3, 5): ("{", "["), pos(4, 2): ("0", "="), pos(4, 4): ("-", "_"),
    pos(4, 5): ("|", "\\"), pos(5, 0): ("?", "^"), pos(5, 2): ("*", "`"),
    pos(5, 4): ("y", "Y"), pos(5, 5): ("#", "'"), pos(6, 2): ("7", "/"),
    pos(7, 4): ("<", ">"), pos(9, 4): (",", ";"), pos(10, 4): (".", ":"),
    pos(12, 0): ("z", "Z"),
}
_ZEICHEN_618: Dict[int, Tuple[str, str]] = {**_ZEICHEN, **ZEICHEN_618_ABWEICHUNG}

#: Sondertasten ohne Zeichen: Position → (Beschriftung, Name für den Hinweis).
#: Namen der Codes ≥ 80H nach `kbdcpt` im CP/A-BIOS (Befund §6).
SONDER = {
    pos(8, 2): ("ESC", "ESC (1BH)"),
    pos(3, 4): ("ET", "ET = Return (9EH)"),
    pos(12, 1): ("", "Leertaste (20H)"),
    pos(6, 6): ("DEL", "DEL (7FH)"),
    pos(5, 6): ("INS", "INS (82H)"),
    pos(12, 6): ("→|", "Taste ->(Tab) (8DH; CP/A macht Tab 09H daraus)"),
    pos(12, 7): ("|←", "Taste |<- (87H)"),
    pos(6, 7): ("→|", "Taste ->| (89H)"),
    pos(5, 7): ("↑", "Cursor hoch (8BH)"),
    pos(3, 7): ("↓", "Cursor runter (8AH)"),
    pos(12, 3): ("←", "Cursor links (88H)"),
    pos(6, 3): ("→", "Cursor rechts (86H)"),
    pos(3, 3): ("↵", "Taste <-' (9DH)"),
    pos(5, 3): ("'\\", "Taste '\\ (8CH)"),
    pos(6, 1): ("F15", "F15 (8EH)"),
    pos(0, 1): ("S", "Ziffernblock S (D0H)"),
    pos(0, 3): ("S", "Ziffernblock S (D0H)"),
    pos(0, 6): ("CE", "Ziffernblock CE (CEH)"),
    pos(2, 1): ("00", "Ziffernblock 00 (BBH)"),
}
#: F-Tasten: Nummer → Position (Tabelle §6).
FTASTEN = {
    1: pos(4, 7), 2: pos(4, 6), 3: pos(10, 1), 4: pos(10, 3), 5: pos(10, 6),
    6: pos(11, 6), 7: pos(11, 7), 8: pos(11, 3), 9: pos(11, 1), 10: pos(10, 7),
    11: pos(9, 6), 12: pos(9, 7), 13: pos(9, 3), 14: pos(9, 1), 15: pos(6, 1),
}
#: Ziffernblock: Position → Beschriftung.
ZIFFERNBLOCK = {
    pos(7, 1): "0", pos(7, 3): "1", pos(2, 3): "2", pos(1, 3): "3",
    pos(7, 7): "4", pos(2, 7): "5", pos(1, 7): "6",
    pos(7, 6): "7", pos(2, 6): "8", pos(1, 6): "9",
    pos(1, 1): ",", pos(0, 7): "-",
}


class _Taste(_Key):
    """Taste der Nachbildung — dazu ihre Matrixposition (-1 = keine)."""

    def __init__(self, *a, pos: int = -1, **kw):
        super().__init__(*a, **kw)
        self.pos = pos


def _k(x, y, p, low, up="", w=1.0, name="", style="dark", kind="normal",
       shape="rect") -> _Taste:
    return _Taste(x=x, y=y, low=low, up=up, code=taste(p), shift_code=taste(p), w=w,
                  style=style, shape=shape, kind=kind, name=name or low, pos=p)


def _build_layout_pc1715(zeichen: Optional[Dict[int, Tuple[str, str]]] = None) -> List[_Taste]:
    r"""Tastenfeld (Raster, 1.0 = eine Taste) — Lage geschätzt, Positionen belegt.

    ```
    y 0   F1 … F15
    y 1   ESC 1 2 3 4 5 6 7 8 9 0 - ^ DEL INS        cursor block      S S CE -
    y 2   →| q w e r t y u i o p @ [                  |<- ^ ->|        7 8 9 ,
    y 3   CTRL a s d f g h j k l ; : ] ET             <- ↵ ->          4 5 6 00
    y 4   SHIFT \ z x c v b n m , . / SHIFT           '\ v             1 2 3
    y 5   LOCK SI/SO REP        Leertaste                              0
    ```
    """
    _Z = zeichen or _ZEICHEN
    k: List[_Taste] = []
    for n in range(1, 16):
        k.append(_k(n - 1, 0.0, FTASTEN[n], f"F{n}", style="light", name=f"F{n}"))

    reihe1 = (pos(7, 2), pos(2, 2), pos(1, 2), pos(0, 2), pos(11, 2), pos(12, 2),
              pos(6, 2), pos(9, 2), pos(10, 2), pos(4, 2), pos(3, 2), pos(5, 2))
    k.append(_k(0.0, 1.5, pos(8, 2), "ESC", name=SONDER[pos(8, 2)][1]))
    for i, p in enumerate(reihe1):
        lo, up = _Z[p]
        k.append(_k(1.0 + i, 1.5, p, lo, up))
    k.append(_k(13.0, 1.5, pos(6, 6), "DEL", name=SONDER[pos(6, 6)][1]))
    k.append(_k(14.0, 1.5, pos(5, 6), "INS", name=SONDER[pos(5, 6)][1]))

    k.append(_k(0.0, 2.5, pos(12, 6), "→|", w=1.5, name=SONDER[pos(12, 6)][1]))
    reihe2 = (pos(7, 0), pos(2, 0), pos(1, 0), pos(0, 0), pos(11, 0), pos(12, 0),
              pos(6, 0), pos(9, 0), pos(10, 0), pos(4, 0), pos(3, 0), pos(5, 0))
    for i, p in enumerate(reihe2):
        lo, up = _Z[p]
        k.append(_k(1.5 + i, 2.5, p, lo.upper() if lo.isalpha() else lo,
                    "" if lo.isalpha() else up))

    k.append(_k(0.0, 3.5, CTRL, "CTRL", w=1.5, kind="ctrl", style="light",
                name="CTRL (gehalten; gemerkt bis zur nächsten Taste)"))
    reihe3 = (pos(7, 5), pos(2, 5), pos(1, 5), pos(0, 5), pos(11, 5), pos(12, 5),
              pos(6, 5), pos(9, 5), pos(10, 5), pos(4, 5), pos(3, 5), pos(5, 5))
    for i, p in enumerate(reihe3):
        lo, up = _Z[p]
        k.append(_k(1.5 + i, 3.5, p, lo.upper() if lo.isalpha() else lo,
                    "" if lo.isalpha() else up))
    k.append(_k(13.5, 3.5, ET, "ET", w=1.5, style="red", name=SONDER[ET][1]))

    k.append(_k(0.0, 4.5, SHIFT_L, "SHIFT", w=1.5, kind="shift", style="light",
                name="SHIFT links (gehalten; gemerkt bis zur nächsten Taste)"))
    reihe4 = (pos(7, 4), pos(5, 4), pos(2, 4), pos(1, 4), pos(0, 4), pos(11, 4),
              pos(12, 4), pos(6, 4), pos(9, 4), pos(10, 4), pos(4, 4))
    for i, p in enumerate(reihe4):
        lo, up = _Z[p]
        k.append(_k(1.5 + i, 4.5, p, lo.upper() if lo.isalpha() else lo,
                    "" if lo.isalpha() else up))
    k.append(_k(12.5, 4.5, SHIFT_R, "SHIFT", w=1.5, kind="shift", style="light",
                name="SHIFT rechts (gehalten; gemerkt bis zur nächsten Taste)"))

    k.append(_k(0.0, 5.5, LOCK, "LOCK", w=1.5, kind="toggle", style="light",
                name="LOCK (rastend im ROM: Buchstaben groß)"))
    k.append(_k(1.5, 5.5, SISO, "SI/SO", w=1.5, kind="toggle", style="light",
                name="SI/SO (rastend im ROM: zweiter Zeichensatz)"))
    k.append(_k(3.0, 5.5, REP, "REP", kind="rep", style="light",
                name="REP (gehalten; gemerkt — erlaubt Wiederholung einer gehaltenen Taste)"))
    k.append(_k(4.5, 5.5, pos(12, 1), "", w=7.0, name=SONDER[pos(12, 1)][1]))

    # Cursorblock.
    CB = 15.75
    for dx, dy, p in ((0, 2.5, pos(12, 7)), (1, 2.5, pos(5, 7)), (2, 2.5, pos(6, 7)),
                      (0, 3.5, pos(12, 3)), (1, 3.5, pos(3, 3)), (2, 3.5, pos(6, 3)),
                      (0, 4.5, pos(5, 3)), (1, 4.5, pos(3, 7))):
        k.append(_k(CB + dx, dy, p, SONDER[p][0], name=SONDER[p][1]))

    # Ziffernblock.
    ZB = 19.5
    for dx, dy, p in ((0, 1.5, pos(0, 1)), (1, 1.5, pos(0, 3)), (2, 1.5, pos(0, 6))):
        k.append(_k(ZB + dx, dy, p, SONDER[p][0], name=SONDER[p][1]))
    k.append(_k(ZB + 3, 1.5, pos(0, 7), "-", name="Ziffernblock - (BDH)"))
    for dy, reihe in ((2.5, (pos(7, 6), pos(2, 6), pos(1, 6), pos(1, 1))),
                      (3.5, (pos(7, 7), pos(2, 7), pos(1, 7), pos(2, 1))),
                      (4.5, (pos(7, 3), pos(2, 3), pos(1, 3)))):
        for dx, p in enumerate(reihe):
            name = SONDER[p][1] if p in SONDER else f"Ziffernblock {ZIFFERNBLOCK[p]}"
            lo = SONDER[p][0] if p in SONDER else ZIFFERNBLOCK[p]
            k.append(_k(ZB + dx, dy, p, lo, name=name))
    k.append(_k(ZB, 5.5, pos(7, 1), "0", w=2.0, name="Ziffernblock 0 (B0H)"))
    return k


#: Hosttasten ohne Zeichen, die der Kern nicht übersetzt → physische Taste.
_HOST_POS = {
    int(Qt.Key_Up): pos(5, 7), int(Qt.Key_Down): pos(3, 7),
    int(Qt.Key_Left): pos(12, 3), int(Qt.Key_Right): pos(6, 3),
    int(Qt.Key_Delete): pos(6, 6), int(Qt.Key_Insert): pos(5, 6),
}
_HOST_POS.update({int(Qt.Key_F1) + n - 1: FTASTEN[n] for n in range(1, 13)})
#: Hosttasten, die der Kern als Zeichen übersetzt — für die Hervorhebung.
_HOST_HERVOR = {
    int(Qt.Key_Return): ET, int(Qt.Key_Enter): ET, int(Qt.Key_Escape): pos(8, 2),
    int(Qt.Key_Tab): pos(12, 6), int(Qt.Key_Backtab): pos(12, 6),
    int(Qt.Key_Backspace): pos(6, 6), int(Qt.Key_Space): pos(12, 1),
}


class KeyboardPc1715Widget(KeyboardWidget):
    """Anklickbare Nachbildung der Tastatur des PC 1715 — Schnittstelle wie die K7637."""

    PAD = (0.35, 0.35, 0.35, 0.35)

    def __init__(self, parent=None, qwertz: bool = False):
        # Vor dem Oberklassen-Konstruktor: der ruft _layout().
        self._qwertz = bool(qwertz)
        self._zeichen = _ZEICHEN_618 if qwertz else _ZEICHEN
        super().__init__(parent)

    def _layout(self) -> List[_Key]:
        return _build_layout_pc1715(self._zeichen)

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys if isinstance(k, _Taste)}
        # Zustand der Nachbildung; vor dem ersten Zeichnen angelegt (der Konstruktor der
        # Oberklasse ruft diese Methode, bevor er fertig ist).
        self._rep = False
        self._rastend: Dict[int, bool] = {LOCK: False, SISO: False}   # LOCK, SI/SO
        self._gehalten: List[int] = []     # beim Druck mitgedrückte Umschalter (Positionen)
        self._zeichen_pos: Optional[int] = None

    # ── Anzeigen: das Gerät hat keine Leuchten an der Tastatur (sie stehen in der
    # Statuszeile von CP/A) ──────────────────────────────────────────────────

    def _led_spots(self, unit, ox, oy):
        return []

    def set_leds(self, mask: int):
        return

    def lock_active(self) -> bool:
        return self._rastend.get(LOCK, False)

    def _note_host_caps(self, event):
        return

    def _is_active(self, key: _Key) -> bool:
        if key.kind == "rep":
            return self._rep
        if key.kind == "toggle":
            return self._rastend.get(key.pos, False)
        return super()._is_active(key)

    # ── Maus ────────────────────────────────────────────────────────────────

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            super().mousePressEvent(event)
            return
        key = self._key_at(event.position() if hasattr(event, "position") else event.pos())
        if not isinstance(key, _Taste):
            return
        if key.kind in ("shift", "ctrl", "rep"):
            # Umschalter: nur merken; gedrückt wird beim nächsten Tastenklick.
            if key.kind == "rep":
                self._rep = not self._rep
            elif key.kind == "ctrl":
                self._ctrl = not self._ctrl
            else:
                self._shift = not self._shift
            self.update()
            return
        if key.kind == "toggle":
            self._rastend[key.pos] = not self._rastend[key.pos]
        # Gemerkte Umschalter VOR der Zeichentaste einzeln drücken.
        self._gehalten = ([CTRL] if self._ctrl else []) + ([SHIFT_L] if self._shift else []) \
            + ([REP] if self._rep else [])
        for p in self._gehalten:
            self.keyPressed.emit(taste(p), False, False)
        self._pressed = key
        self._pressed_code = taste(key.pos)
        self._zeichen_pos = key.pos
        self.update()
        self.keyPressed.emit(taste(key.pos), False, False)

    def _release_pressed(self):
        if self._pressed is None and self._pressed_code is None:
            return
        if self._pressed_code is not None:
            self.keyReleased.emit(int(self._pressed_code))
        # Umschalter in umgekehrter Reihenfolge lösen; sie galten nur für diese Taste.
        for p in reversed(self._gehalten):
            self.keyReleased.emit(taste(p))
        self._gehalten = []
        self._pressed = None
        self._pressed_code = None
        self._zeichen_pos = None
        self._shift = self._ctrl = self._rep = False
        self.update()

    # ── Host-Tastatur ───────────────────────────────────────────────────────

    def map_host_key(self, event) -> Optional[Tuple[int, bool, bool]]:
        """Host-Taste → ``(code, shift, ctrl)`` für den Kern: Zeichen als ASCII (der Kern
        sucht Taste und Umschaltung), Pfeile/Entf/Einfg/F-Tasten als physische Taste."""
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return None
        code, shift, ctrl = mapped
        p = _HOST_POS.get(int(event.key()))
        if p is not None:
            return (taste(p), False, False)
        ctrl = ctrl or self._ctrl
        if self._shift and 0x61 <= code <= 0x7A and not ctrl:
            code -= 0x20          # angeklicktes SHIFT der Nachbildung
            shift = True
        return (code, shift, ctrl)

    def host_key_release(self, event):
        mapped = self.map_host_key(event)
        self._mark_up(event)
        if mapped is not None and (self._shift or self._ctrl):
            self._shift = self._ctrl = False
            self.update()
        return mapped

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [self._by_pos[SHIFT_L], self._by_pos[SHIFT_R]]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [self._by_pos[CTRL]]
        if key == int(Qt.Key_CapsLock):
            return [self._by_pos[LOCK]]
        p = _HOST_POS.get(key, _HOST_HERVOR.get(key))
        if p is not None:
            t = self._by_pos.get(p)
            return [t] if t else []
        text = event.text()
        if len(text) == 1:
            for position, (lo, up) in self._zeichen.items():
                if text in (lo, up):
                    return [self._by_pos[position]]
        return []

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        p = getattr(key, "pos", -1)
        if p < 0:
            return name
        return f"{name} — Matrix Spalte {p // 8}, Zeile {p % 8}"
