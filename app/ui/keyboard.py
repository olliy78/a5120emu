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

2. :class:`KeyboardWidget` — eine **Nachbildung der echten K7637** (Layout,
   Doppelbeschriftung, Farben Schwarz/Weiß/Rot), gezeichnet statt aus Knöpfen
   zusammengesetzt: die Tastenformen (runde Kappen im Schacht, Ovale, der
   dreireihige ENTER-Balken) sind mit Widgets nicht sinnvoll nachzubauen.

   Sie sendet dieselben ``(keycode, shift, ctrl)``-Ereignisse über die Signale
   :attr:`keyPressed` / :attr:`keyReleased`.

**Woher die Tastencodes kommen.** Die K7637 sendet den *physischen* Code aus
ihrer ROM-Codetabelle; das CP/A-BIOS rekodiert die hohen Codes über die Tabelle
``cp37`` (im Listing ``disks/cpa_cpa780_*.prn``).  Daraus stammen alle
Sondercodes unten — SEL0..3 (die Tasten ``0 1 2 3`` links oben) 0xA0..0xA3,
PF1..PF12 0xC1..0xCC, die Umschaltebene PA1..PA3/CLEAR/REC/FM/DUP/EREOF/ERINP,
CE 0xB9, ENTER 0xC0, ET1 0xFF, MON (``M``) 0xB0, RESET 0xAF, ``00`` 0xB1 sowie
die acht Kursorcodes.  Gesendet werden sie über den **Rohcode-Fluchtweg**
``K7637::QK_RAW_BASE`` (= :data:`RAW_BASE`): ``RAW_BASE | 0xC1`` ist „PF1", ohne
den Umweg über eine PC-Taste, die es für diese Tasten gar nicht gibt.

Nicht belegt sind **PRINT** und **HLT** (stehen in keiner vorliegenden
Codetabelle, das Tastatur-EPROM fehlt): sie werden gezeichnet, senden aber
nichts.  Die rote ``−`` des Ziffernblocks sendet ASCII ``-``; ihr echter Code
ist ebenfalls unbekannt (``cp37`` führt INS MD ausdrücklich als „Ersatz num.
Minus").
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple

from PySide6.QtCore import Qt, Signal, QEvent, QPointF, QRectF, QSize, QTimer
from PySide6.QtGui import QColor, QFont, QFontMetricsF, QPainter
from PySide6.QtWidgets import QSizePolicy, QToolTip, QWidget


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


# ── Rohcode-Fluchtweg in den Kern ────────────────────────────────────────────
#: Muss ``K7637::QK_RAW_BASE`` entsprechen (``core/peripherals/k7637/k7637.h``).
RAW_BASE = 0x02000000


def raw(code: int) -> int:
    """Physischen K7637-Tastencode (ein Byte) in einen Kern-Keycode packen."""
    return RAW_BASE | (code & 0xFF)


# ── Anzeigen: Bitmaske aus `k1520_keyboard_leds` ────────────────────────────
#: Die fünf Funktionsanzeigen über den Selektortasten (Handbuch: G00…G04).
LED_G00, LED_G01, LED_G02, LED_G03, LED_G04 = 0x01, 0x02, 0x04, 0x08, 0x10
#: Fehleranzeige G53 — sie **blinkt**, solange das Bit gesetzt ist.
LED_ERROR = 0x20
#: Akustisches Signal (hier nicht dargestellt, nur der Vollständigkeit halber).
LED_BEEP = 0x80

#: Blinktakt der Fehleranzeige in Millisekunden.
_BLINK_MS = 500


# ── Farben (dem Foto der echten Tastatur abgenommen) ─────────────────────────
_C_BACK      = QColor(0x1e, 0x1e, 0x1e)   # Fläche neben der Tastatur
_C_BEZEL     = QColor(0x6e, 0x6a, 0x55)   # Tastaturwanne, oliv
_C_HOLDER    = QColor(0x54, 0x52, 0x49)   # Schacht um die runden Kappen
_C_DARK      = QColor(0x2c, 0x2c, 0x2e)   # schwarze Kappe
_C_DARK_TXT  = QColor(0xef, 0xea, 0xd8)
_C_LIGHT     = QColor(0xd6, 0xd3, 0xc6)   # helle (durchscheinende) Kappe
_C_LIGHT_TXT = QColor(0x4a, 0x49, 0x45)
_C_RED       = QColor(0xd2, 0x3b, 0x30)
_C_RED_TXT   = QColor(0xff, 0xff, 0xff)
_C_FILLER    = QColor(0x3f, 0x3f, 0x3b)   # Blindtasten ohne Beschriftung
_C_LED_OFF   = QColor(0x7a, 0x4a, 0x42)   # rote LED, unbeleuchtet (milchig-dunkel)
_C_LED_ON    = QColor(0xff, 0x3b, 0x2a)
_C_ACTIVE    = QColor(0xff, 0xd0, 0x40)   # rastender Modifikator an


@dataclass
class _Key:
    """Eine Taste der Nachbildung.

    Geometrie in **Tastenrastern** (1.0 = ein Rastermaß); der Zeichencode ist
    entweder ASCII (0x20..0x7E) oder ein Rohcode (:func:`raw`).
    """

    x: float
    y: float
    low: str = ""              # Beschriftung unten = Grundebene
    up: str = ""               # Beschriftung oben  = Umschaltebene
    code: Optional[int] = None
    shift_code: Optional[int] = None
    w: float = 0.95
    h: float = 0.92
    style: str = "dark"        # dark | light | red | filler
    shape: str = "round"       # round | rect | oval
    kind: str = "normal"       # normal | shift | lock | ctrl | dead
    name: str = ""             # Klartext für den Kurzhinweis
    vertical: bool = False     # Beschriftung um 90° gedreht (ENTER)
    dual: bool = False         # zweizeilig beschriften, auch ohne Umschaltebene

    def code_for(self, shift: bool) -> Optional[int]:
        if shift and self.shift_code is not None:
            return self.shift_code
        return self.code


def _a(ch: str) -> int:
    """ASCII-Code eines Zeichens."""
    return ord(ch)


def _build_layout() -> List[_Key]:
    """Das Tastenfeld der K7637.50 (Standard-Latein), Reihe für Reihe.

    Die x-Werte sind am Foto abgemessen (Rastermaß ≈ 157 px); die Reihen sind
    gegeneinander versetzt wie im Original.  Der Codierstecker am rechten Rand
    ist weggelassen — er wirkt nur unter SIOS.
    """
    k: List[_Key] = []

    # ── Reihe 0: Funktionstasten, helle Quadrate ─────────────────────────────
    # Obere Beschriftung = Umschaltebene.  Codes: SEL0..3 und PF1..PF12 aus
    # cp37; die Umschaltcodes (PA1…ERINP) stehen dort als eigene Einträge.
    fn = [
        ("",       "0",     raw(0xA0), None,       "SEL 0"),
        ("",       "1",     raw(0xA1), None,       "SEL 1"),
        ("",       "2",     raw(0xA2), None,       "SEL 2"),
        ("",       "3",     raw(0xA3), None,       "SEL 3"),
        ("INS L",  "INS MD", raw(0xA8), raw(0x93), "INS MD / INS L"),
        ("DEL L",  "DEL CH", raw(0xBB), raw(0xB3), "DEL CH / DEL L"),
        ("PA 1",   "PF 1",  raw(0xC1), raw(0xFA),  "PF 1 / PA 1"),
        ("PA 2",   "PF 2",  raw(0xC2), raw(0xF9),  "PF 2 / PA 2"),
        ("PA 3",   "PF 3",  raw(0xC3), raw(0xF8),  "PF 3 / PA 3"),
        ("",       "PF 4",  raw(0xC4), None,       "PF 4"),
        ("CLEAR",  "PF 5",  raw(0xC5), raw(0xFC),  "PF 5 / CLEAR"),
        ("REC",    "PF 6",  raw(0xC6), raw(0xFD),  "PF 6 / REC"),
        ("FM",     "PF 7",  raw(0xC7), raw(0xBE),  "PF 7 / FM"),
        ("DUP",    "PF 8",  raw(0xC8), raw(0xBC),  "PF 8 / DUP"),
        ("POWER",  "PF 9",  raw(0xC9), None,       "PF 9 (POWER: am Auftischgerät ohne Funktion)"),
        ("EREOF",  "PF 10", raw(0xCA), raw(0x98),  "PF 10 / EREOF"),
        ("ERINP",  "PF 11", raw(0xCB), raw(0x99),  "PF 11 / ERINP"),
        ("",       "PF 12", raw(0xCC), None,       "PF 12"),
    ]
    for i, (up, low, code, sc, name) in enumerate(fn):
        k.append(_Key(x=float(i), y=0.0, low=low, up=up, code=code,
                      shift_code=sc, h=0.85, style="light", shape="rect",
                      dual=True, name=name))
    k.append(_Key(x=18.3, y=0.0, low="RESET", code=raw(0xAF), h=0.85,
                  style="light", shape="rect", name="RESET (BIOS: PF 15)"))
    k.append(_Key(x=19.3, y=0.0, low="M", code=raw(0xB0), h=0.85,
                  style="light", shape="rect", name="M / MON (BIOS: PF 14)"))

    # ── Reihe 1: Ziffernreihe ────────────────────────────────────────────────
    k.append(_Key(x=0.0, y=1.0, low="CTRL", w=1.25, style="light", shape="rect",
                  kind="ctrl", name="CTRL (Steuerebene)"))
    digits = [
        ("!", "1"), ('"', "2"), ("#", "3"), ("¤", "4"), ("%", "5"),
        ("&", "6"), ("'", "7"), ("(", "8"), (")", "9"), ("_", "0"),
        ("=", "-"), ("‾", "^"),
    ]
    for i, (up, low) in enumerate(digits):
        # Die Umschaltebene ist bitgepaart (ASCII): '1'→'!', '-'→'=', '^'→'~'.
        # '¤' ist die Darstellung von 0x24 im A5120-Zeichensatz, '‾' die von
        # 0x7E (Tilde).
        shift_ch = {"¤": "$", "‾": "~"}.get(up, up)
        k.append(_Key(x=1.4 + i, y=1.0, low=low, up=up,
                      code=_a(low), shift_code=_a(shift_ch),
                      name=f"{low} / {shift_ch}"))
    k.append(_Key(x=13.4, y=1.0, low="|←|", code=raw(0x9F),
                  name="Tabulator (BIOS: TAB)"))
    k.append(_Key(x=14.5, y=1.0, low="↰", code=raw(0x9C), style="light",
                  shape="rect", name="Kursor Seite zurück"))
    k.append(_Key(x=15.5, y=1.0, low="PRINT", style="light", shape="rect",
                  kind="dead", name="PRINT — Tastencode unbekannt"))
    k.append(_Key(x=16.5, y=1.0, low="HLT", style="light", shape="rect",
                  kind="dead", name="HLT — Tastencode unbekannt"))
    k.append(_Key(x=17.5, y=1.0, low="ESC", code=0x1B, style="light",
                  shape="rect", name="ESC (0x1B)"))
    k.append(_Key(x=18.5, y=1.0, w=1.2, style="filler", shape="rect",
                  kind="dead", name="Blindtaste"))

    # ── Reihe 2: QWERTY ──────────────────────────────────────────────────────
    k.append(_Key(x=0.3, y=2.0, low="→|", w=1.3, code=raw(0x91), style="light",
                  shape="rect", name="Kursor Wort vorwärts"))
    for i, ch in enumerate("QWERTYUIOP"):
        k.append(_Key(x=1.8 + i, y=2.0, low=ch, code=_a(ch.lower()),
                      shift_code=_a(ch), name=ch))
    k.append(_Key(x=11.8, y=2.0, low="@", up="`", code=_a("@"),
                  shift_code=_a("`"), name="@ / `"))
    k.append(_Key(x=12.8, y=2.0, low="[", up="{", code=_a("["),
                  shift_code=_a("{"), name="[ / {"))
    k.append(_Key(x=13.9, y=2.0, low="|←", code=raw(0x9B), style="light",
                  shape="rect", name="Kursor Wort zurück"))
    k.append(_Key(x=14.9, y=2.0, low="CE", code=raw(0xB9), style="red",
                  name="CE (Eingabe löschen)"))
    for i, ch in enumerate("789"):
        k.append(_Key(x=16.05 + i, y=2.0, low=ch, code=_a(ch), name=f"Ziffernblock {ch}"))
    k.append(_Key(x=19.05, y=2.0, low=".", code=_a("."), name="Ziffernblock ."))

    # ── Reihe 3: ASDF ────────────────────────────────────────────────────────
    k.append(_Key(x=0.55, y=3.0, low="↕", kind="lock",
                  name="LOCK (Umschaltfeststeller)"))
    for i, ch in enumerate("ASDFGHJKL"):
        k.append(_Key(x=1.55 + i, y=3.0, low=ch, code=_a(ch.lower()),
                      shift_code=_a(ch), name=ch))
    k.append(_Key(x=10.55, y=3.0, low=";", up="+", code=_a(";"),
                  shift_code=_a("+"), name="; / +"))
    k.append(_Key(x=11.55, y=3.0, low=":", up="*", code=_a(":"),
                  shift_code=_a("*"), name=": / *"))
    k.append(_Key(x=12.55, y=3.0, low="]", up="}", code=_a("]"),
                  shift_code=_a("}"), name="] / }"))
    k.append(_Key(x=13.9, y=3.0, low="↵", code=raw(0x9A), style="light",
                  shape="rect", name="Kursor Seite vorwärts"))
    k.append(_Key(x=14.9, y=3.0, low="−", code=_a("-"), style="red",
                  name="Ziffernblock − (sendet ASCII '-')"))
    for i, ch in enumerate("456"):
        k.append(_Key(x=16.05 + i, y=3.0, low=ch, code=_a(ch), name=f"Ziffernblock {ch}"))
    k.append(_Key(x=19.05, y=3.0, low="ENTER", code=raw(0xC0), h=2.92,
                  shape="oval", vertical=True, name="ENTER (Ziffernblock, ≠ ET1)"))

    # ── Reihe 4: ZXCV ────────────────────────────────────────────────────────
    k.append(_Key(x=0.0, y=4.0, low="↕", w=1.05, shape="oval", kind="shift",
                  name="Umschalttaste (SHIFT)"))
    k.append(_Key(x=1.1, y=4.0, low="\\", up="|", code=_a("\\"),
                  shift_code=_a("|"), name="\\ / |"))
    for i, ch in enumerate("ZXCVBNM"):
        k.append(_Key(x=2.15 + i, y=4.0, low=ch, code=_a(ch.lower()),
                      shift_code=_a(ch), name=ch))
    k.append(_Key(x=9.15, y=4.0, low=",", up="<", code=_a(","),
                  shift_code=_a("<"), name=", / <"))
    k.append(_Key(x=10.15, y=4.0, low=".", up=">", code=_a("."),
                  shift_code=_a(">"), name=". / >"))
    k.append(_Key(x=11.15, y=4.0, low="/", up="?", code=_a("/"),
                  shift_code=_a("?"), name="/ / ?"))
    k.append(_Key(x=12.15, y=4.0, low="↕", w=1.55, shape="oval", kind="shift",
                  name="Umschalttaste (SHIFT)"))
    k.append(_Key(x=13.9, y=4.0, low="↓", code=raw(0x95), style="light",
                  shape="rect", name="Kursor abwärts"))
    k.append(_Key(x=14.9, y=4.0, low="↑", code=raw(0x94), style="light",
                  shape="rect", name="Kursor aufwärts"))
    for i, ch in enumerate("123"):
        k.append(_Key(x=16.05 + i, y=4.0, low=ch, code=_a(ch), name=f"Ziffernblock {ch}"))

    # ── Reihe 5: Leertastenreihe ─────────────────────────────────────────────
    k.append(_Key(x=0.1, y=5.0, w=0.8, style="filler", shape="rect",
                  kind="dead", name="Blindtaste"))
    k.append(_Key(x=1.0, y=5.0, low="ET2", w=1.5, shape="oval", kind="ctrl",
                  name="ET2 (wirkt als Steuertaste)"))
    k.append(_Key(x=2.75, y=5.0, w=8.2, shape="oval", code=0x20,
                  name="Leertaste"))
    k.append(_Key(x=11.65, y=5.0, low="ET1", w=1.5, shape="oval",
                  code=raw(0xFF), name="ET1 (BIOS: CR)"))
    k.append(_Key(x=13.9, y=5.0, low="←", code=raw(0x96), style="light",
                  shape="rect", name="Kursor links"))
    k.append(_Key(x=14.9, y=5.0, low="→", code=raw(0x97), style="light",
                  shape="rect", name="Kursor rechts"))
    k.append(_Key(x=16.05, y=5.0, low="0", code=_a("0"), name="Ziffernblock 0"))
    k.append(_Key(x=17.05, y=5.0, low="00", code=raw(0xB1), name="Ziffernblock 00"))
    k.append(_Key(x=18.05, y=5.0, low=",", code=_a(","), name="Ziffernblock ,"))

    return k


class KeyboardWidget(QWidget):
    """Anklickbare Nachbildung der K7637-Tastatur.

    Emittiert für jede betätigte Taste :attr:`keyPressed` (mit Shift-/Ctrl-Zustand)
    und beim Loslassen :attr:`keyReleased`.  (Eine Tastenwiederholung entsteht
    daraus nicht: die Wiederholung des K7637-Modells hängt an ``tick()``, das der
    laufende Rechner nicht aufruft.)

    SHIFT und CTRL/ET2 wirken auf genau die nächste Taste, LOCK bleibt gesetzt
    (wie auf der echten Tastatur, die mit SHIFT zurückgeschaltet wird).
    """

    keyPressed = Signal(int, bool, bool)   # keycode, shift, ctrl
    keyReleased = Signal(int)              # keycode

    #: Kleinstes Rastermaß in Pixeln — darunter ist nichts mehr lesbar.
    MIN_UNIT = 21.0
    #: Rastermaß, das die Tastatur von sich aus vorschlägt.
    PREF_UNIT = 34.0

    def __init__(self, parent=None):
        super().__init__(parent)
        self._shift = False
        self._ctrl = False
        self._lock = False
        self._keys = _build_layout()
        self._pressed: Optional[_Key] = None     # aktuell gedrückte Taste
        self._pressed_code: Optional[int] = None
        self._leds = 0                           # Bitmaske aus dem Kern
        self._blink_on = True                    # Phase der Fehleranzeige
        self._powered = True                     # Betriebsanzeige E54

        # Die Fehleranzeige blinkt — die echte tut es auch.  Der Zeitgeber läuft
        # nur, solange sie eingeschaltet ist.
        self._blink_timer = QTimer(self)
        self._blink_timer.setInterval(_BLINK_MS)
        self._blink_timer.timeout.connect(self._toggle_blink)

        span_x = max(k.x + k.w for k in self._keys)
        span_y = max(k.y + k.h for k in self._keys)
        # Ränder: oben mehr, dort sitzt die LED-Leiste der echten Tastatur.
        self._pad = (0.35, 0.75, 0.35, 0.35)     # links, oben, rechts, unten
        self._units_w = span_x + self._pad[0] + self._pad[2]
        self._units_h = span_y + self._pad[1] + self._pad[3]

        self.setFocusPolicy(Qt.StrongFocus)
        self.setMouseTracking(False)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setAutoFillBackground(False)

    # ── Anzeigen ────────────────────────────────────────────────────────────

    def set_leds(self, mask: int):
        """Zustand der Tastaturanzeigen übernehmen (`k1520_keyboard_leds`).

        Wird je Bild gerufen; neu gezeichnet wird nur bei echter Änderung.
        """
        mask = int(mask) & 0xFF
        if mask == self._leds:
            return
        self._leds = mask
        if mask & LED_ERROR:
            if not self._blink_timer.isActive():
                self._blink_on = True
                self._blink_timer.start()
        else:
            self._blink_timer.stop()
        self.update()

    def set_powered(self, on: bool):
        """Betriebsanzeige E54 — sie hängt an der Spannung, nicht am Rechner."""
        if bool(on) != self._powered:
            self._powered = bool(on)
            self.update()

    def _toggle_blink(self):
        self._blink_on = not self._blink_on
        self.update()

    # ── Größen ──────────────────────────────────────────────────────────────

    def _size_for_unit(self, unit: float) -> QSize:
        return QSize(int(round(self._units_w * unit)),
                     int(round(self._units_h * unit)))

    def sizeHint(self) -> QSize:
        return self._size_for_unit(self.PREF_UNIT)

    def minimumSizeHint(self) -> QSize:
        return self._size_for_unit(self.MIN_UNIT)

    def hasHeightForWidth(self) -> bool:
        return True

    def heightForWidth(self, width: int) -> int:
        """Höhe, bei der die Tastatur die Breite @p width genau ausfüllt."""
        unit = max(self.MIN_UNIT, width / self._units_w)
        return int(round(self._units_h * unit))

    def _geometry(self) -> Tuple[float, float, float]:
        """(Rastermaß, x-Versatz, y-Versatz) für die aktuelle Widgetgröße."""
        unit = min(self.width() / self._units_w, self.height() / self._units_h)
        unit = max(unit, 1.0)
        ox = (self.width() - self._units_w * unit) / 2.0
        oy = (self.height() - self._units_h * unit) / 2.0
        return unit, ox, oy

    def _rect_of(self, key: _Key, unit: float, ox: float, oy: float) -> QRectF:
        return QRectF(ox + (self._pad[0] + key.x) * unit,
                      oy + (self._pad[1] + key.y) * unit,
                      key.w * unit, key.h * unit)

    def _key_at(self, pos) -> Optional[_Key]:
        unit, ox, oy = self._geometry()
        for key in self._keys:
            if self._rect_of(key, unit, ox, oy).contains(QPointF(pos)):
                return key
        return None

    # ── Zeichnen ────────────────────────────────────────────────────────────

    def paintEvent(self, event):
        unit, ox, oy = self._geometry()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        p.fillRect(self.rect(), _C_BACK)

        # Tastaturwanne.
        p.setPen(Qt.NoPen)
        p.setBrush(_C_BEZEL)
        p.drawRoundedRect(QRectF(ox, oy, self._units_w * unit,
                                 self._units_h * unit),
                          0.25 * unit, 0.25 * unit)

        # Die acht Anzeigen (Handbuch §2.1/§2.2.3/§2.3):
        #   G00…G04  die fünf über den Selektortasten — der Rechner schaltet sie
        #   G53      Fehleranzeige, blinkend, rechts in derselben Leiste
        #   E54      Betriebsanzeige neben dem Blindplatz der Einschalttaste
        #   C99      LOCK, folgt dem Feststeller der Tastatur selbst
        led_r = 0.08 * unit
        for cx, cy, lit, _ in self._led_spots(unit, ox, oy):
            self._draw_led(p, cx, cy, led_r, lit)

        for key in self._keys:
            self._draw_key(p, key, unit, ox, oy)

        p.end()

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Die acht Leuchtpunkte als ``(x, y, leuchtet, Bezeichnung)``.

        Eine Stelle für Zeichnen und Kurzhinweis — sonst wandern die Punkte
        beim nächsten Feilen am Layout auseinander.
        """
        led_y = oy + 0.38 * unit
        spots = []
        namen = ("Selektor 0 (G00)", "Selektor 1 (G01)", "Selektor 2 (G02)",
                 "Selektor 3 (G03)", "INS-Modus (G04)")
        for i, bit in enumerate((LED_G00, LED_G01, LED_G02, LED_G03, LED_G04)):
            spots.append((ox + (0.6 + i * 0.9) * unit, led_y,
                          bool(self._leds & bit), namen[i]))
        spots.append((ox + (self._units_w - 0.7) * unit, led_y,
                      bool(self._leds & LED_ERROR) and self._blink_on,
                      "Fehleranzeige (G53) — blinkt"))
        spots.append((ox + (self._pad[0] + 19.85) * unit,
                      oy + (self._pad[1] + 1.45) * unit,
                      self._powered, "Betriebsanzeige (E54)"))
        spots.append((ox + (self._pad[0] + 0.2) * unit,
                      oy + (self._pad[1] + 3.45) * unit,
                      self._lock, "Umschaltfeststeller (C99)"))
        return spots

    def _draw_led(self, p: QPainter, cx: float, cy: float, r: float,
                  lit: bool = False):
        """Eine rote Anzeigediode.

        Rot wie am Original; unbeleuchtet bleibt sie milchig-dunkel.
        """
        p.setPen(Qt.NoPen)
        p.setBrush(_C_LED_ON if lit else _C_LED_OFF)
        p.drawEllipse(QPointF(cx, cy), r, r)

    def _cap_colors(self, key: _Key) -> Tuple[QColor, QColor]:
        if key.style == "light":
            return _C_LIGHT, _C_LIGHT_TXT
        if key.style == "red":
            return _C_RED, _C_RED_TXT
        if key.style == "filler":
            return _C_FILLER, _C_FILLER
        return _C_DARK, _C_DARK_TXT

    def _is_active(self, key: _Key) -> bool:
        return ((key.kind == "shift" and self._shift)
                or (key.kind == "ctrl" and self._ctrl)
                or (key.kind == "lock" and self._lock))

    def _draw_key(self, p: QPainter, key: _Key, unit: float, ox: float, oy: float):
        rect = self._rect_of(key, unit, ox, oy)
        cap, txt = self._cap_colors(key)
        if key is self._pressed:
            cap = cap.darker(135) if key.style != "dark" else cap.lighter(160)

        p.setPen(Qt.NoPen)
        if key.shape == "round":
            # Runde Kappe im quadratischen Schacht — die Bauform der K7637.
            p.setBrush(_C_HOLDER)
            p.drawRoundedRect(rect, 0.12 * unit, 0.12 * unit)
            inset = 0.06 * unit
            cap_rect = rect.adjusted(inset, inset, -inset, -inset)
            p.setBrush(cap)
            p.drawEllipse(cap_rect)
        elif key.shape == "oval":
            p.setBrush(_C_HOLDER)
            p.drawRoundedRect(rect, 0.12 * unit, 0.12 * unit)
            inset = 0.05 * unit
            cap_rect = rect.adjusted(inset, inset, -inset, -inset)
            r = min(cap_rect.width(), cap_rect.height()) / 2.0
            p.setBrush(cap)
            p.drawRoundedRect(cap_rect, r, r)
        else:
            p.setBrush(cap)
            p.drawRoundedRect(rect, 0.15 * unit, 0.15 * unit)

        if self._is_active(key):
            pen = p.pen()
            p.setBrush(Qt.NoBrush)
            p.setPen(_C_ACTIVE)
            p.drawRoundedRect(rect.adjusted(1, 1, -1, -1),
                              0.15 * unit, 0.15 * unit)
            p.setPen(pen)

        self._draw_legend(p, key, rect, txt, unit)

    def _draw_legend(self, p: QPainter, key: _Key, rect: QRectF,
                     color: QColor, unit: float):
        if not key.low and not key.up:
            return
        p.setPen(color)

        if key.vertical:
            p.save()
            p.translate(rect.center())
            p.rotate(90)
            box = QRectF(-rect.height() / 2, -rect.width() / 2,
                         rect.height(), rect.width())
            self._draw_text(p, key.low, box, unit * 0.30)
            p.restore()
            return

        if key.up or key.dual:
            # Doppelbeschriftung: oben die Umschaltebene, unten die Grundebene.
            top = QRectF(rect.x(), rect.y() + rect.height() * 0.12,
                         rect.width(), rect.height() * 0.40)
            bot = QRectF(rect.x(), rect.y() + rect.height() * 0.50,
                         rect.width(), rect.height() * 0.40)
            self._draw_text(p, key.up, top, unit * 0.26)
            self._draw_text(p, key.low, bot, unit * 0.26)
        else:
            self._draw_text(p, key.low, rect, unit * 0.34)

    def _draw_text(self, p: QPainter, text: str, box: QRectF, size: float):
        """Text mittig in @p box — die Schrift schrumpft, bis sie hineinpasst."""
        if not text:
            return
        font = QFont(p.font())
        size = max(5.0, size)
        avail = box.width() * 0.88
        while size > 5.0:
            font.setPixelSize(max(5, int(round(size))))
            if QFontMetricsF(font).horizontalAdvance(text) <= avail:
                break
            size -= 0.75
        font.setPixelSize(max(5, int(round(size))))
        p.setFont(font)
        p.drawText(box, Qt.AlignCenter, text)

    # ── Maus ────────────────────────────────────────────────────────────────

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            super().mousePressEvent(event)
            return
        key = self._key_at(event.position() if hasattr(event, "position")
                           else event.pos())
        if key is None:
            return
        if key.kind in ("shift", "ctrl", "lock"):
            self._toggle_mod(key.kind)
            self.update()   # LOCK-Anzeige C99 hängt an diesem Zustand
            return
        if key.kind == "dead" or key.code is None:
            return

        shift_active = self._shift or self._lock
        code = key.code_for(shift_active)
        if code is None:
            return
        self._pressed = key
        self._pressed_code = int(code)
        self.update()
        self.keyPressed.emit(int(code), shift_active, self._ctrl)

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.LeftButton:
            super().mouseReleaseEvent(event)
            return
        self._release_pressed()

    def leaveEvent(self, event):
        # Maus verlässt das Widget mit gedrückter Taste: nicht hängen lassen,
        # sonst läuft die Tastenwiederholung des K7637 endlos weiter.
        self._release_pressed()
        super().leaveEvent(event)

    def _release_pressed(self):
        if self._pressed_code is None:
            return
        self.keyReleased.emit(int(self._pressed_code))
        self._pressed = None
        self._pressed_code = None
        # SHIFT/CTRL wirken nur auf die eine Taste (LOCK bleibt bestehen).
        if self._shift or self._ctrl:
            self._shift = False
            self._ctrl = False
        self.update()

    def _toggle_mod(self, kind: str):
        if kind == "shift":
            # Wie am Original: SHIFT hebt auch den Feststeller wieder auf.
            if self._lock:
                self._lock = False
                self._shift = False
            else:
                self._shift = not self._shift
        elif kind == "ctrl":
            self._ctrl = not self._ctrl
        elif kind == "lock":
            self._lock = not self._lock
            self._shift = False

    # ── Kurzhinweis: welche Taste ist das, und welchen Code sendet sie? ─────

    def event(self, event):
        if event.type() == QEvent.ToolTip:
            key = self._key_at(event.pos())
            if key is not None:
                QToolTip.showText(event.globalPos(), self._tip(key), self)
                return True
            led = self._led_at(event.pos())
            if led is not None:
                QToolTip.showText(event.globalPos(), led, self)
            else:
                QToolTip.hideText()
            return True
        return super().event(event)

    def _led_at(self, pos) -> Optional[str]:
        """Bezeichnung der Anzeige unter @p pos (oder ``None``)."""
        unit, ox, oy = self._geometry()
        r = 0.22 * unit                   # großzügiger als der Punkt selbst
        for cx, cy, lit, name in self._led_spots(unit, ox, oy):
            if (pos.x() - cx) ** 2 + (pos.y() - cy) ** 2 <= r * r:
                return f"{name}: {'an' if lit else 'aus'}"
        return None

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "ctrl", "lock"):
            return name
        if key.kind == "dead" or key.code is None:
            return f"{name} — sendet nichts"
        parts = [f"{name}: Code 0x{key.code & 0xFF:02X}"]
        if key.shift_code is not None and key.shift_code != key.code:
            parts.append(f"mit SHIFT 0x{key.shift_code & 0xFF:02X}")
        return " · ".join(parts)

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
