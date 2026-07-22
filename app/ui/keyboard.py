"""
K1520 Emulator - Tastatur (K7637)
=================================

Zwei Dinge leben hier:

1. :func:`qt_event_to_core_key` — übersetzt ein Qt-``QKeyEvent`` in das Tripel
   ``(keycode, shift, ctrl)``, das der Core (``k1520_key_press``) erwartet.  Der
   Vertrag mit ``core/peripherals/k7637`` (siehe ``translateKey``):

   * **Druckbares ASCII (0x20..0x7E)** wird als der *erzeugte* Zeichencode
     übergeben (Shift/Layout stecken bereits im Wert: ``'A'``=0x41, ``'a'``=0x61,
     ``'!'``=0x21).  Der Core reicht ihn unverändert durch.
   * **Sondertasten** (Return/Enter/Tab/Backspace/Esc/Delete/Cursor/F1..F8)
     werden als ``Qt::Key_*``-Konstante übergeben — die ``QK_*``-Werte im Core
     sind mit ``Qt::Key_*`` identisch.
   * **Ctrl+Buchstabe** → Basis-ASCII des Buchstabens + ``ctrl=True``; der Core
     bildet daraus den Steuercode (``code & 0x1F``).

2. :class:`KeyboardWidget` — eine anklickbare Bildschirmtastatur im K7637-Stil.
   Sie sendet dieselben ``(keycode, shift, ctrl)``-Ereignisse über die Signale
   :attr:`keyPressed` / :attr:`keyReleased`.  Das Aussehen ist bewusst schlicht
   gehalten und wird später an die echte Tastatur angeglichen.
"""

from typing import Optional, Tuple

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QGridLayout, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,
    QSizePolicy,
)


# ── Qt-Sondertasten, die der Core direkt als Qt::Key_* versteht ───────────────
_SPECIAL_KEYS = {
    int(Qt.Key_Return),
    int(Qt.Key_Enter),
    int(Qt.Key_Backspace),
    int(Qt.Key_Tab),
    int(Qt.Key_Escape),
    int(Qt.Key_Delete),
    int(Qt.Key_Up),
    int(Qt.Key_Down),
    int(Qt.Key_Left),
    int(Qt.Key_Right),
}


def qt_event_to_core_key(event) -> Optional[Tuple[int, bool, bool]]:
    """Ein ``QKeyEvent`` → ``(keycode, shift, ctrl)`` für ``key_press``.

    Liefert ``None`` für Tasten, die nicht an den Core weitergereicht werden
    sollen (z.B. reine Modifikatoren oder unbekannte Tasten).
    """
    key = int(event.key())
    mods = event.modifiers()
    shift = bool(mods & Qt.ShiftModifier)
    ctrl = bool(mods & Qt.ControlModifier)

    # Reine Modifikatoren erzeugen keinen Tastencode.
    if key in (int(Qt.Key_Shift), int(Qt.Key_Control), int(Qt.Key_Alt),
               int(Qt.Key_Meta), int(Qt.Key_CapsLock), int(Qt.Key_AltGr)):
        return None

    # Sondertasten: als Qt::Key_* durchreichen (Core kennt die Werte 1:1).
    if key in _SPECIAL_KEYS or (int(Qt.Key_F1) <= key <= int(Qt.Key_F8)):
        return (key, shift, ctrl)

    # Ctrl+Buchstabe: Basis-ASCII + ctrl-Flag (Core rechnet & 0x1F).
    if ctrl and int(Qt.Key_A) <= key <= int(Qt.Key_Z):
        return (key + 0x20, shift, True)   # 0x41.. → 0x61.. ('a'..'z')

    # Druckbares Zeichen: den tatsächlich erzeugten Text nehmen (respektiert
    # Shift und das Host-Tastaturlayout).
    text = event.text()
    if text and len(text) == 1:
        cp = ord(text)
        if 0x20 <= cp <= 0x7E:
            return (cp, shift, ctrl)

    # Leertaste liefert je nach Plattform keinen Text.
    if key == int(Qt.Key_Space):
        return (0x20, shift, ctrl)

    return None


# ── Bildschirmtastatur ────────────────────────────────────────────────────────

class _Key:
    """Definition einer einzelnen Taste im Bildschirm-Layout."""

    __slots__ = ("label", "shift_label", "code", "shift_code", "span", "kind")

    def __init__(self, label, code, *, shift_label=None, shift_code=None,
                 span=1, kind="normal"):
        self.label = label            # Beschriftung (unshifted)
        self.shift_label = shift_label  # Beschriftung mit Shift (optional)
        self.code = code              # Core-Keycode ohne Shift
        self.shift_code = shift_code if shift_code is not None else code
        self.span = span              # Spalten im Grid
        self.kind = kind              # "normal" | "shift" | "ctrl" | "lock"


def _c(ch: str) -> int:
    return ord(ch)


class KeyboardWidget(QWidget):
    """Anklickbare Darstellung der K7637-Tastatur.

    Emittiert für jede betätigte Taste :attr:`keyPressed` (mit Shift-/Ctrl-Zustand)
    gefolgt von :attr:`keyReleased`.  Shift/Ctrl/Lock sind rastende Modifikatoren:
    Shift/Ctrl wirken auf genau die nächste Taste, Lock bleibt gesetzt.
    """

    keyPressed = Signal(int, bool, bool)   # keycode, shift, ctrl
    keyReleased = Signal(int)              # keycode

    def __init__(self, parent=None):
        super().__init__(parent)
        self._shift = False
        self._ctrl = False
        self._lock = False
        self._mod_buttons = {}   # kind -> QPushButton (zum Rückspiegeln)
        self._build_ui()
        # Damit die Tastatur auch echte Host-Tasten weiterreicht, wenn sie den
        # Fokus hat (die Bildschirm-Widget-Weiterleitung ist der Hauptpfad).
        self.setFocusPolicy(Qt.StrongFocus)

    # ── Layout ---------------------------------------------------------------

    def _build_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(6, 6, 6, 6)
        outer.setSpacing(8)

        # Funktionstastenreihe.
        frow = [
            _Key("ESC", int(Qt.Key_Escape)),
            _Key("F1", int(Qt.Key_F1)), _Key("F2", int(Qt.Key_F2)),
            _Key("F3", int(Qt.Key_F3)), _Key("F4", int(Qt.Key_F4)),
            _Key("F5", int(Qt.Key_F5)), _Key("F6", int(Qt.Key_F6)),
            _Key("F7", int(Qt.Key_F7)), _Key("F8", int(Qt.Key_F8)),
        ]

        # Haupt-QWERTZ-Block (deutsche Belegung, vereinfacht).
        main_rows = [
            [
                _Key("1", _c("1"), shift_label="!", shift_code=_c("!")),
                _Key("2", _c("2"), shift_label='"', shift_code=_c('"')),
                _Key("3", _c("3"), shift_label="§", shift_code=_c("3")),
                _Key("4", _c("4"), shift_label="$", shift_code=_c("$")),
                _Key("5", _c("5"), shift_label="%", shift_code=_c("%")),
                _Key("6", _c("6"), shift_label="&", shift_code=_c("&")),
                _Key("7", _c("7"), shift_label="/", shift_code=_c("/")),
                _Key("8", _c("8"), shift_label="(", shift_code=_c("(")),
                _Key("9", _c("9"), shift_label=")", shift_code=_c(")")),
                _Key("0", _c("0"), shift_label="=", shift_code=_c("=")),
                _Key("ß", _c("-"), shift_label="?", shift_code=_c("?")),
                _Key("'", _c("'"), shift_label="`", shift_code=_c("`")),
                _Key("<--", int(Qt.Key_Backspace), span=2),
            ],
            [
                _Key("Tab", int(Qt.Key_Tab), span=2),
                _Key("Q", _c("q"), shift_label="Q", shift_code=_c("Q")),
                _Key("W", _c("w"), shift_label="W", shift_code=_c("W")),
                _Key("E", _c("e"), shift_label="E", shift_code=_c("E")),
                _Key("R", _c("r"), shift_label="R", shift_code=_c("R")),
                _Key("T", _c("t"), shift_label="T", shift_code=_c("T")),
                _Key("Z", _c("z"), shift_label="Z", shift_code=_c("Z")),
                _Key("U", _c("u"), shift_label="U", shift_code=_c("U")),
                _Key("I", _c("i"), shift_label="I", shift_code=_c("I")),
                _Key("O", _c("o"), shift_label="O", shift_code=_c("O")),
                _Key("P", _c("p"), shift_label="P", shift_code=_c("P")),
                _Key("Ü", _c("["), shift_label="Ü", shift_code=_c("{")),
                _Key("+", _c("+"), shift_label="*", shift_code=_c("*")),
            ],
            [
                _Key("LOCK", 0, span=2, kind="lock"),
                _Key("A", _c("a"), shift_label="A", shift_code=_c("A")),
                _Key("S", _c("s"), shift_label="S", shift_code=_c("S")),
                _Key("D", _c("d"), shift_label="D", shift_code=_c("D")),
                _Key("F", _c("f"), shift_label="F", shift_code=_c("F")),
                _Key("G", _c("g"), shift_label="G", shift_code=_c("G")),
                _Key("H", _c("h"), shift_label="H", shift_code=_c("H")),
                _Key("J", _c("j"), shift_label="J", shift_code=_c("J")),
                _Key("K", _c("k"), shift_label="K", shift_code=_c("K")),
                _Key("L", _c("l"), shift_label="L", shift_code=_c("L")),
                _Key("Ö", _c(";"), shift_label="Ö", shift_code=_c(":")),
                _Key("Ä", _c("'"), shift_label="Ä", shift_code=_c('"')),
                _Key("ET1", int(Qt.Key_Return), span=2),
            ],
            [
                _Key("SHIFT", 0, span=2, kind="shift"),
                _Key("Y", _c("y"), shift_label="Y", shift_code=_c("Y")),
                _Key("X", _c("x"), shift_label="X", shift_code=_c("X")),
                _Key("C", _c("c"), shift_label="C", shift_code=_c("C")),
                _Key("V", _c("v"), shift_label="V", shift_code=_c("V")),
                _Key("B", _c("b"), shift_label="B", shift_code=_c("B")),
                _Key("N", _c("n"), shift_label="N", shift_code=_c("N")),
                _Key("M", _c("m"), shift_label="M", shift_code=_c("M")),
                _Key(",", _c(","), shift_label=";", shift_code=_c(";")),
                _Key(".", _c("."), shift_label=":", shift_code=_c(":")),
                _Key("-", _c("-"), shift_label="_", shift_code=_c("_")),
                _Key("SHIFT", 0, span=2, kind="shift"),
            ],
            [
                _Key("CTRL", 0, span=2, kind="ctrl"),
                _Key("Leertaste", _c(" "), span=8),
                _Key("DELCH", int(Qt.Key_Delete), span=2),
            ],
        ]

        # Ziffernblock rechts.
        num_rows = [
            [_Key("7", _c("7")), _Key("8", _c("8")), _Key("9", _c("9")),
             _Key("/", _c("/"))],
            [_Key("4", _c("4")), _Key("5", _c("5")), _Key("6", _c("6")),
             _Key("*", _c("*"))],
            [_Key("1", _c("1")), _Key("2", _c("2")), _Key("3", _c("3")),
             _Key("-", _c("-"))],
            [_Key("0", _c("0"), span=2), _Key(".", _c(".")),
             _Key("ENTER", int(Qt.Key_Enter))],
        ]

        # Cursor-Cluster.
        cursor_rows = [
            [None, _Key("↑", int(Qt.Key_Up)), None],
            [_Key("←", int(Qt.Key_Left)), _Key("↓", int(Qt.Key_Down)),
             _Key("→", int(Qt.Key_Right))],
        ]

        # Funktionstastenreihe rendern.
        outer.addWidget(self._make_row(frow))

        # Mittlerer Bereich: Hauptblock | Cursor | Ziffernblock.
        mid = QHBoxLayout()
        mid.setSpacing(12)

        main_box = QVBoxLayout()
        main_box.setSpacing(4)
        for row in main_rows:
            main_box.addWidget(self._make_row(row))
        main_w = QWidget()
        main_w.setLayout(main_box)
        mid.addWidget(main_w, 5)

        cursor_grid = self._make_grid(cursor_rows)
        mid.addWidget(cursor_grid, 0, Qt.AlignBottom)

        num_grid = self._make_grid(num_rows)
        mid.addWidget(num_grid, 0, Qt.AlignTop)

        mid_w = QWidget()
        mid_w.setLayout(mid)
        outer.addWidget(mid_w)

        # Übriger vertikaler Platz geht in einen Abstandhalter unten, damit die
        # Tastenreihen kompakt oben bleiben (statt sich in die Höhe zu ziehen).
        outer.addStretch(1)

    def _make_row(self, keys) -> QWidget:
        """Eine horizontale Tastenreihe (span steuert die relative Breite)."""
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        for k in keys:
            if k is None:
                lay.addStretch(1)
                continue
            lay.addWidget(self._make_button(k), k.span)
        return w

    def _make_grid(self, rows) -> QWidget:
        """Ein Raster fester Zellen (Ziffernblock / Cursor)."""
        w = QWidget()
        grid = QGridLayout(w)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(4)
        for r, row in enumerate(rows):
            col = 0
            for k in row:
                if k is None:
                    col += 1
                    continue
                btn = self._make_button(k)
                grid.addWidget(btn, r, col, 1, k.span)
                col += k.span
        return w

    def _make_button(self, k: _Key) -> QPushButton:
        btn = QPushButton(k.label)
        btn.setFocusPolicy(Qt.NoFocus)   # Fokus soll beim Bildschirm bleiben
        btn.setFixedHeight(32)   # feste Höhe → Reihen wachsen nicht mit dem Dock
        btn.setMinimumWidth(26)
        # Kompakte Tasten: Padding klein halten, damit die Bildschirmtastatur
        # das Fenster nicht unnötig breit macht (Look wird später angepasst).
        btn.setStyleSheet("QPushButton { padding: 1px 2px; }")
        btn.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        if k.kind in ("shift", "ctrl", "lock"):
            btn.setCheckable(True)
            btn.clicked.connect(lambda checked, kind=k.kind: self._on_mod(kind, checked))
            self._mod_buttons[k.kind] = btn
        else:
            btn.clicked.connect(lambda _=False, key=k: self._on_key(key))
        return btn

    # ── Ereignisverarbeitung -------------------------------------------------

    def _on_mod(self, kind: str, checked: bool):
        if kind == "shift":
            self._shift = checked
        elif kind == "ctrl":
            self._ctrl = checked
        elif kind == "lock":
            self._lock = checked

    def _on_key(self, k: _Key):
        shift_active = self._shift or self._lock
        code = k.shift_code if shift_active else k.code
        if code == 0:
            return
        self.keyPressed.emit(int(code), shift_active, self._ctrl)
        self.keyReleased.emit(int(code))
        # Shift/Ctrl sind Einmal-Modifikatoren (Lock bleibt bestehen).
        if self._shift:
            self._shift = False
            self._reflect_mod("shift", False)
        if self._ctrl:
            self._ctrl = False
            self._reflect_mod("ctrl", False)

    def _reflect_mod(self, kind: str, state: bool):
        btn = self._mod_buttons.get(kind)
        if btn is not None:
            btn.setChecked(state)

    # ── Host-Tasten auch verarbeiten, wenn die Tastatur den Fokus hat --------

    def keyPressEvent(self, event):
        mapped = qt_event_to_core_key(event)
        if mapped is not None:
            self.keyPressed.emit(*mapped)
            event.accept()
            return
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        mapped = qt_event_to_core_key(event)
        if mapped is not None:
            self.keyReleased.emit(mapped[0])
            event.accept()
            return
        super().keyReleaseEvent(event)
