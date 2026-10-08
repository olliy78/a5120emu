"""Flachtastatur K7673.09 des P8000-Terminals Typ 2: Matrix, Tastenbild, Abbildung der Wirtstasten.

Reine Daten und Rechnung — kein Qt (prüfbar ohne Fenster).  Quellen: `doc/p8000/tastatur_k7673.md`
§4 (Tastenmatrix, 105 belegte Positionen von 128) und `NORMAL_Tab`/`SHIFT_Tab` der Terminal-Firmware
5.0 (`p8t.main.s`, im Abzug stichprobenartig bestätigt).

* **Position** = ``(zeile, spalte)`` mit Zeile 0–7 und Spalte 0–15 (P0.0–7 = Spalte 0–7,
  P1.0–7 = Spalte 8–15); der Kern kodiert sie als ``0x04000000 | zeile << 8 | spalte``.
* **Die Beschriftung der Tastenkappen kennt keine Quelle** (Befund P19a): Bild und Wirtsabbildung
  folgen dem, was das Terminal aus dem Scancode macht, nicht einem Foto der Tastatur.  Die
  Anordnung (QWERTZ-Block, Cursor-/Bearbeitungsblock, Ziffernblock) ist ein Vorschlag; die
  Matrixpositionen und ihre Scancodes sind dagegen gelesen (Test gegen
  ``term_matrix_scancode`` des Kerns).
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

Position = Tuple[int, int]

#: Kodierung einer Matrixtaste für ``key_press``/``key_release`` (``k1520_term_matrix_key`` gleichwertig).
MATRIX_KODE = 0x04000000


def matrix_kode(zeile: int, spalte: int) -> int:
    return MATRIX_KODE | (zeile << 8) | spalte


def kode_matrix(kode: int) -> Optional[Position]:
    """Umkehrung von :func:`matrix_kode`; ``None``, wenn *kode* keine Matrixtaste ist."""
    if kode & 0xFF000000 != MATRIX_KODE:
        return None
    return (kode >> 8) & 0xFF, kode & 0xFF


# Scancode als Zahl: E0-Taste = 0xE000 | Code, E1-Folgen (PAUSE, „00") als Folge in einer Zahl,
# wie sie ``k1520_term_matrix_scancode`` liefert — der Test vergleicht beides.
_E0 = 0xE000

#: Position → (Scancode, Beschriftung normal, Beschriftung mit SHIFT, Tooltip-Name).
#: Beschriftungen sind die Zeichen, die das Terminal 5.0 daraus macht (Zeichensatz 1).
TASTEN: Dict[Position, Tuple[int, str, str, str]] = {
    # Zeile 0
    (0, 0): (0x02, "1", "!", ""), (0, 1): (0x04, "3", "@", ""), (0, 2): (0x06, "5", "%", ""),
    (0, 3): (0x08, "7", "/", ""), (0, 4): (0x0A, "9", ")", ""), (0, 5): (0x0C, "~", "?", "ß in Zeichensatz 2"),
    (0, 6): (0x1D, "TAB", "", "Tabulator (HT)"), (0, 7): (0x4A, "-", "", "Ziffernblock"),
    (0, 8): (_E0 | 0x4D, "▶", "", "Cursor rechts"), (0, 9): (_E0 | 0x52, "CHAR\nDEL", "", "CHAR DELETE"),
    (0, 10): (_E0 | 0x49, "CHAR\nINS", "", "CHAR INSERT"), (0, 11): (_E0 | 0x35, "/", "", "Ziffernblock"),
    (0, 14): (0x45E19DC5, "PAUSE", "", "PAUSE (ohne Wirkung im Terminal 5.0)"),
    # Zeile 1
    (1, 0): (0x10, "q", "Q", ""), (1, 1): (0x12, "e", "E", ""), (1, 2): (0x14, "t", "T", ""),
    (1, 3): (0x16, "u", "U", ""), (1, 4): (0x18, "o", "O", ""), (1, 5): (0x1A, "]", "}", "ü in Zeichensatz 2"),
    (1, 6): (0x2A, "SHIFT", "", "SHIFT (links)"),
    (1, 8): (_E0 | 0x50, "▼", "", "Cursor runter"), (1, 9): (_E0 | 0x53, "LINE\nDEL", "", "LINE DELETE"),
    (1, 10): (_E0 | 0x51, "LINE\nINS", "", "LINE INSERT"), (1, 11): (0x48, "8", "", "Ziffernblock"),
    (1, 14): (0x57, "", "", ""), (1, 15): (0x58, "", "", ""),
    # Zeile 2
    (2, 0): (0x1E, "a", "A", ""), (2, 1): (0x20, "d", "D", ""), (2, 2): (0x22, "g", "G", ""),
    (2, 3): (0x24, "j", "J", ""), (2, 4): (0x26, "l", "L", ""), (2, 5): (0x28, "[", "{", "ä in Zeichensatz 2"),
    (2, 6): (0x3A, "CAPS\nLOCK", "", "CAPS LOCK (rastet, LED)"), (2, 7): (0x4E, "=", "", "Ziffernblock"),
    (2, 8): (_E0 | 0x4B, "◀", "", "Cursor links"), (2, 11): (0x4C, "5", "", "Ziffernblock"),
    (2, 14): (0x42, "", "", ""),
    # Zeile 3
    (3, 0): (0x2C, "y", "Y", ""), (3, 1): (0x2E, "c", "C", ""), (3, 2): (0x30, "b", "B", ""),
    (3, 3): (0x32, "m", "M", ""), (3, 4): (0x34, ".", ":", ""),
    (3, 6): (0x2A, "SHIFT", "", "SHIFT (rechts)"), (3, 7): (_E0 | 0x1C, "ENTER", "", "ENTER (sendet CR)"),
    (3, 8): (0x1C, "RETURN", "", "RETURN (CR)"), (3, 11): (0x50, "2", "", "Ziffernblock"),
    (3, 14): (0x3E, "BREAK", "", "BREAK"), (3, 15): (0x3F, "", "", ""),
    # Zeile 4
    (4, 0): (0x03, "2", '"', ""), (4, 1): (0x05, "4", "$", ""), (4, 2): (0x07, "6", "&", ""),
    (4, 3): (0x09, "8", "(", ""), (4, 4): (0x0B, "0", "=", ""), (4, 5): (0x0D, "'", "`", ""),
    (4, 6): (0x56, "<", ">", ""), (4, 7): (0x0E, "DEL", "", "DEL (7FH)"),
    (4, 8): (0x52, "0", "", "Ziffernblock"), (4, 9): (_E0 | 0x47, "PAGE\nERASE", "", "PAGE ERASE"),
    (4, 10): (0x45, "BS", "", "BS (zweite Taste)"), (4, 11): (0x37, "*", "", "Ziffernblock"),
    (4, 12): (0x49, "9", "", "zweite 9"), (4, 14): (_E0 | 0x37, "", "", ""), (4, 15): (0x46, "", "", ""),
    # Zeile 5
    (5, 0): (0x11, "w", "W", ""), (5, 1): (0x13, "r", "R", ""), (5, 2): (0x15, "z", "Z", ""),
    (5, 3): (0x17, "i", "I", ""), (5, 4): (0x19, "p", "P", ""), (5, 5): (0x1B, "+", "*", ""),
    (5, 6): (0x01, "ESC", "", "ESC"), (5, 7): (0x0F, "BACK\nTAB", "", "BACKTAB"),
    (5, 8): (0xE152E152, "00", "", "Ziffernblock „00“"), (5, 9): (_E0 | 0x4F, "HOME", "", "HOME"),
    (5, 10): (0x47, "7", "", "Ziffernblock"), (5, 11): (0x49, "9", "", "Ziffernblock"),
    (5, 12): (0x3B, "SI/SO", "", "Zeichensatz 1 ⇄ 2"), (5, 14): (0x43, "", "", ""), (5, 15): (0x44, "", "", ""),
    # Zeile 6
    (6, 0): (0x1F, "s", "S", ""), (6, 1): (0x21, "f", "F", ""), (6, 2): (0x23, "h", "H", ""),
    (6, 3): (0x25, "k", "K", ""), (6, 4): (0x27, "\\", "|", "ö in Zeichensatz 2"),
    (6, 5): (0x2B, "#", "^", ""), (6, 6): (0x2A, "SHIFT", "", "SHIFT (Mitte)"),
    (6, 7): (0x29, "BS", "", "BS (Backspace)"),
    (6, 8): (0x53, ",", "", "Ziffernblock"), (6, 10): (0x4B, "4", "", "Ziffernblock"),
    (6, 11): (0x4D, "6", "", "Ziffernblock"), (6, 13): (0x38, "CTRL", "", "CTRL"),
    (6, 14): (0x40, "", "", ""), (6, 15): (0x41, "", "", ""),
    # Zeile 7
    (7, 0): (0x2D, "x", "X", ""), (7, 1): (0x2F, "v", "V", ""), (7, 2): (0x31, "n", "N", ""),
    (7, 3): (0x33, ",", ";", ""), (7, 4): (0x35, "-", "_", ""), (7, 7): (0x39, "", "", "Leertaste"),
    (7, 9): (_E0 | 0x48, "▲", "", "Cursor hoch"), (7, 10): (0x4F, "1", "", "Ziffernblock"),
    (7, 11): (0x51, "3", "", "Ziffernblock"), (7, 12): (0x54, "ON/\nOFF", "", "ON/OFF (rastet, LED)"),
    (7, 13): (_E0 | 0x38, "", "", ""), (7, 14): (0x3C, "MODE", "", "MODE (rastet, LED)"),
    (7, 15): (0x3D, "VIDEO", "", "VIDEO"),
}

#: Tasten, die im Terminal 5.0 keine Wirkung haben (kein Zeichen, keine Funktion).
OHNE_WIRKUNG: Tuple[Position, ...] = (
    (1, 14), (1, 15), (2, 14), (3, 15), (4, 14), (4, 15), (5, 14), (5, 15), (6, 14), (6, 15),
    (7, 13), (0, 14))

#: Zuordnung der LED-Maske (``term_leds``) zu den Tasten, die sie anzeigen.
LED_TASTEN: Dict[int, Position] = {0x01: (7, 12), 0x02: (2, 6), 0x04: (7, 14)}

#: Rasttasten (die Tastatur rastet nicht mechanisch; die LED zeigt den Zustand des Terminals).
RASTTASTEN = frozenset(LED_TASTEN.values())

#: Tasten, die als Umschalter wirken und von der Bildschirmtastatur „gehalten“ werden können.
UMSCHALTER: Tuple[Position, ...] = ((1, 6), (3, 6), (6, 6), (6, 13))


def positionen() -> List[Position]:
    """Alle belegten Positionen (105), Reihenfolge Zeile, Spalte."""
    return sorted(TASTEN)


def scancode(pos: Position) -> int:
    return TASTEN[pos][0]


def _erste_position() -> Dict[int, Position]:
    aus: Dict[int, Position] = {}
    for pos in positionen():
        aus.setdefault(TASTEN[pos][0], pos)
    return aus


#: Scancode → erste Position, die ihn sendet (SHIFT sitzt dreimal, die „9“ zweimal).
SCANCODE_POS: Dict[int, Position] = _erste_position()

# ── Tastenbild: Position, x, y, Breite in Tasteneinheiten ───────────────────────────────────────
#: Bildhöhe/-breite in Tasteneinheiten (Höhe einer Taste = 1).
BILD_BREITE = 23.4
BILD_HOEHE = 7.6


def _reihe(y: float, x: float, eintraege) -> List[Tuple[Position, float, float, float]]:
    aus = []
    for pos, breite in eintraege:
        aus.append((pos, x, y, breite))
        x += breite
    return aus


def _gleich(positionen_, breite=1.0):
    return [(p, breite) for p in positionen_]


def _bildaufbau() -> List[Tuple[Position, float, float, float]]:
    t: List[Tuple[Position, float, float, float]] = []
    # Funktionsreihe
    t += _reihe(0.0, 0.0, _gleich([(5, 12), (7, 14), (7, 15), (3, 14), (7, 12), (0, 14)], 1.4))
    # Hauptblock
    t += _reihe(1.15, 0.0, [((5, 6), 1.0)] + _gleich(
        [(0, 0), (4, 0), (0, 1), (4, 1), (0, 2), (4, 2), (0, 3), (4, 3), (0, 4), (4, 4),
         (0, 5), (4, 5)]) + [((4, 7), 2.0)])
    t += _reihe(2.15, 0.0, [((0, 6), 1.5)] + _gleich(
        [(1, 0), (5, 0), (1, 1), (5, 1), (1, 2), (5, 2), (1, 3), (5, 3), (1, 4), (5, 4),
         (1, 5), (5, 5)]) + [((5, 7), 1.5)])
    t += _reihe(3.15, 0.0, [((2, 6), 1.75)] + _gleich(
        [(2, 0), (6, 0), (2, 1), (6, 1), (2, 2), (6, 2), (2, 3), (6, 3), (2, 4), (6, 4),
         (2, 5), (6, 5)]) + [((3, 7), 1.25)])
    t += _reihe(4.15, 0.0, [((1, 6), 1.25), ((4, 6), 1.0)] + _gleich(
        [(3, 0), (7, 0), (3, 1), (7, 1), (3, 2), (7, 2), (3, 3), (7, 3), (3, 4), (7, 4)])
        + [((3, 6), 2.75)])
    t += _reihe(5.15, 0.0, [((6, 13), 1.5), ((6, 6), 1.5), ((7, 7), 8.0), ((3, 8), 2.0),
                            ((6, 7), 1.0), ((4, 10), 1.0)])
    # Bearbeitungsblock
    t += _reihe(1.15, 15.6, _gleich([(0, 9), (0, 10), (4, 9)]))
    t += _reihe(2.15, 15.6, _gleich([(1, 9), (1, 10), (5, 9)]))
    t += _reihe(4.15, 16.6, _gleich([(7, 9)]))
    t += _reihe(5.15, 15.6, _gleich([(2, 8), (1, 8), (0, 8)]))
    # Ziffernblock
    t += _reihe(1.15, 19.6, _gleich([(5, 8), (0, 11), (4, 11), (0, 7)]))
    t += _reihe(2.15, 19.6, _gleich([(5, 10), (1, 11), (5, 11), (2, 7)]))
    t += _reihe(3.15, 19.6, _gleich([(6, 10), (2, 11), (6, 11), (6, 8)]))
    t += _reihe(4.15, 19.6, _gleich([(7, 10), (3, 11), (7, 11), (4, 12)]))
    t += _reihe(5.15, 19.6, [((4, 8), 2.0)])
    # Tasten ohne Wirkung: schmale Leiste unten
    t += _reihe(6.6, 0.0, _gleich([p for p in OHNE_WIRKUNG if p != (0, 14)], 1.0))
    return t


#: ``(Position, x, y, breite)`` — jede der 105 Positionen genau einmal.
BILD: List[Tuple[Position, float, float, float]] = _bildaufbau()

# ── Zeichen → Taste (NORMAL_Tab / SHIFT_Tab der Firmware 5.0) ──────────────────────────────────
_NORMAL = bytes.fromhex(
    "2B 1B 31 32 33 34 35 36 37 38 39 30 7E 27 7F 81"
    "71 77 65 72 74 7A 75 69 6F 70 5D 2B 0D 09 61 73"
    "64 66 67 68 6A 6B 6C 5C 5B 08 80 23 79 78 63 76"
    "62 6E 6D 2C 2E 2D 80 2A 80 20 80 F2 F3 F4 F1 80"
    "80 80 80 80 80 08 80 37 38 39 2D 34 35 36 3D 31"
    "32 33 30 2C F0 80 3C 80 80".replace(" ", ""))
_SHIFT = bytes.fromhex(
    "2B 1B 21 22 40 24 25 26 2F 28 29 3D 3F 60 7F 81"
    "51 57 45 52 54 5A 55 49 4F 50 7D 2A 0D 09 41 53"
    "44 46 47 48 4A 4B 4C 7C 7B 08 00 5E 59 58 43 56"
    "42 4E 4D 3B 3A 5F".replace(" ", ""))


def _zeichentabelle() -> Dict[str, Tuple[Position, bool]]:
    """Zeichen → (Position, braucht SHIFT).  Ohne SHIFT geht vor; unter den Tasten gleichen
    Zeichens gewinnt die mit dem kleinsten Scancode (Hauptblock vor Ziffernblock)."""
    aus: Dict[str, Tuple[Position, bool]] = {}
    for mit_shift, tab in ((False, _NORMAL), (True, _SHIFT)):
        for sc, c in enumerate(tab):
            if not 0x20 <= c < 0x7F:
                continue
            ch = chr(c)
            pos = SCANCODE_POS.get(sc)
            if pos is None or ch in aus:
                continue
            aus[ch] = (pos, mit_shift)
    # Mit SHIFT erreichbar, ohne SHIFT ein anderes Zeichen derselben Taste: die Tabellen nennen es.
    return aus


#: Zeichen (ASCII 20H–7EH) → ``((zeile, spalte), braucht_shift)``.
ZEICHEN: Dict[str, Tuple[Position, bool]] = _zeichentabelle()

#: Umlaute der Wirtstastatur → das ASCII-Zeichen der Taste, die im Zeichensatz 2 (SI/SO, deutsch)
#: das Umlautzeichen liefert (Firmware `TGET8`: `[ \ ]` +20H = `{ | }` = ä ö ü; `{ | }` −20H = Ä Ö Ü).
UMLAUTE = {"ä": "[", "ö": "\\", "ü": "]", "Ä": "{", "Ö": "|", "Ü": "}", "ß": "~", "§": "@"}


def zeichen_taste(ch: str) -> Optional[Tuple[Position, bool]]:
    """Die Taste (und ob SHIFT dazu gehört), die das Zeichen *ch* erzeugt; ``None`` = keine."""
    ch = UMLAUTE.get(ch, ch)
    return ZEICHEN.get(ch)


#: Wirtstaste (Qt-Kode) → Scancode der K7673-Taste.  Gesondert, weil sie keinen Text liefern.
#: Eingetragen als Zahlen statt ``Qt.Key_*``, damit dieses Modul ohne Qt auskommt.
_QT = dict(Escape=0x01000000, Tab=0x01000001, Backtab=0x01000002, Backspace=0x01000003,
           Return=0x01000004, Enter=0x01000005, Insert=0x01000006, Delete=0x01000007,
           Pause=0x01000008, Home=0x01000010, End=0x01000011, Left=0x01000012, Up=0x01000013,
           Right=0x01000014, Down=0x01000015,
           Shift=0x01000020, Control=0x01000021, CapsLock=0x01000024,
           F1=0x01000030, F2=0x01000031, F3=0x01000032, F4=0x01000033, F5=0x01000034,
           F6=0x01000035, F7=0x01000036, F8=0x01000037, F9=0x01000038, F10=0x01000039,
           F12=0x0100003B)

SONDERTASTEN: Dict[int, int] = {
    _QT["Escape"]: 0x01, _QT["Tab"]: 0x1D, _QT["Backtab"]: 0x0F, _QT["Backspace"]: 0x29,
    _QT["Return"]: 0x1C, _QT["Enter"]: _E0 | 0x1C, _QT["Delete"]: 0x0E,
    _QT["Left"]: _E0 | 0x4B, _QT["Right"]: _E0 | 0x4D, _QT["Up"]: _E0 | 0x48, _QT["Down"]: _E0 | 0x50,
    _QT["Home"]: _E0 | 0x4F, _QT["Insert"]: _E0 | 0x49, _QT["End"]: _E0 | 0x47,
    _QT["F2"]: _E0 | 0x47, _QT["F3"]: _E0 | 0x51, _QT["F4"]: _E0 | 0x49, _QT["F5"]: _E0 | 0x53,
    _QT["F6"]: _E0 | 0x52, _QT["F7"]: 0x3E, _QT["Pause"]: 0x3E, _QT["F8"]: 0x3B,
    _QT["F9"]: 0x3C, _QT["F10"]: 0x3D, _QT["F12"]: 0x54,
    _QT["Shift"]: 0x2A, _QT["Control"]: 0x38, _QT["CapsLock"]: 0x3A,
}


def sondertaste(qtkey: int) -> Optional[Position]:
    """Position der Taste, die der Wirtstaste *qtkey* ohne Textwirkung entspricht."""
    sc = SONDERTASTEN.get(qtkey)
    return SCANCODE_POS.get(sc) if sc is not None else None
