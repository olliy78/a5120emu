"""Flachtastatur K7673.09 des P8000-Terminals Typ 2: Matrix, Tastenbild, Abbildung der Wirtstasten.

Reine Daten und Rechnung — kein Qt (prüfbar ohne Fenster).  Quellen: `doc/p8000/tastatur_k7673.md`
§4 (Tastenmatrix, 105 belegte Positionen von 128) und `NORMAL_Tab`/`SHIFT_Tab` der Terminal-Firmware
5.0 (`p8t.main.s`, im Abzug stichprobenartig bestätigt).

* **Position** = ``(zeile, spalte)`` mit Zeile 0–7 und Spalte 0–15 (P0.0–7 = Spalte 0–7,
  P1.0–7 = Spalte 8–15); der Kern kodiert sie als ``0x04000000 | zeile << 8 | spalte``.
* **Anordnung und Beschriftung der Tastenkappen folgen dem Foto des Anwenders** (2026-10-08,
  ``doc/p8000/bilder/tastatur_k7673_foto.jpg``, ``doc/p8000/tastatur_k7673.md`` §8).  Die Zuordnung
  Kappe ↔ Matrixposition ist dort, wo das Foto keine Quelle hat (F1–F11, CE, „+“, rechtes CTRL,
  die vier Zeilen-/Zeichentasten), eine ANNAHME aus der Scancode-Wirkung.  Die Matrixpositionen
  und ihre Scancodes sind dagegen gelesen (Test gegen ``term_matrix_scancode`` des Kerns).
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

_SCANCODE: Dict[Position, int] = {
    # Zeile 0
    (0, 0): 0x02, (0, 1): 0x04, (0, 2): 0x06, (0, 3): 0x08, (0, 4): 0x0A, (0, 5): 0x0C, (0, 6): 0x1D, (0, 7): 0x4A, (0, 8): _E0 | 0x4D, (0, 9): _E0 | 0x52, (0, 10): _E0 | 0x49, (0, 11): _E0 | 0x35, (0, 14): 0x45e19dc5,
    # Zeile 1
    (1, 0): 0x10, (1, 1): 0x12, (1, 2): 0x14, (1, 3): 0x16, (1, 4): 0x18, (1, 5): 0x1A, (1, 6): 0x2A, (1, 8): _E0 | 0x50, (1, 9): _E0 | 0x53, (1, 10): _E0 | 0x51, (1, 11): 0x48, (1, 14): 0x57, (1, 15): 0x58,
    # Zeile 2
    (2, 0): 0x1E, (2, 1): 0x20, (2, 2): 0x22, (2, 3): 0x24, (2, 4): 0x26, (2, 5): 0x28, (2, 6): 0x3A, (2, 7): 0x4E, (2, 8): _E0 | 0x4B, (2, 11): 0x4C, (2, 14): 0x42,
    # Zeile 3
    (3, 0): 0x2C, (3, 1): 0x2E, (3, 2): 0x30, (3, 3): 0x32, (3, 4): 0x34, (3, 6): 0x2A, (3, 7): _E0 | 0x1C, (3, 8): 0x1C, (3, 11): 0x50, (3, 14): 0x3E, (3, 15): 0x3F,
    # Zeile 4
    (4, 0): 0x03, (4, 1): 0x05, (4, 2): 0x07, (4, 3): 0x09, (4, 4): 0x0B, (4, 5): 0x0D, (4, 6): 0x56, (4, 7): 0x0E, (4, 8): 0x52, (4, 9): _E0 | 0x47, (4, 10): 0x45, (4, 11): 0x37, (4, 12): 0x49, (4, 14): _E0 | 0x37, (4, 15): 0x46,
    # Zeile 5
    (5, 0): 0x11, (5, 1): 0x13, (5, 2): 0x15, (5, 3): 0x17, (5, 4): 0x19, (5, 5): 0x1B, (5, 6): 0x01, (5, 7): 0x0F, (5, 8): 0xe152e152, (5, 9): _E0 | 0x4F, (5, 10): 0x47, (5, 11): 0x49, (5, 12): 0x3B, (5, 14): 0x43, (5, 15): 0x44,
    # Zeile 6
    (6, 0): 0x1F, (6, 1): 0x21, (6, 2): 0x23, (6, 3): 0x25, (6, 4): 0x27, (6, 5): 0x2B, (6, 6): 0x2A, (6, 7): 0x29, (6, 8): 0x53, (6, 10): 0x4B, (6, 11): 0x4D, (6, 13): 0x38, (6, 14): 0x40, (6, 15): 0x41,
    # Zeile 7
    (7, 0): 0x2D, (7, 1): 0x2F, (7, 2): 0x31, (7, 3): 0x33, (7, 4): 0x35, (7, 7): 0x39, (7, 9): _E0 | 0x48, (7, 10): 0x4F, (7, 11): 0x51, (7, 12): 0x54, (7, 13): _E0 | 0x38, (7, 14): 0x3C, (7, 15): 0x3D,
}

# Beschriftung der Tastenkappen nach dem Foto des Anwenders (2026-10-08, `doc/p8000/bilder/
# tastatur_k7673_foto.jpg`; Zuordnung und Annahmen: `doc/p8000/tastatur_k7673.md` §8).
#: Position → (Kappe unten/Haupt, Kappe oben, Tooltip).  „Oben“ ist die Umschaltbelegung der Kappe
#: (bei Zeichentasten das Zeichen mit SHIFT); die Zeichensatz-2-Belegung (DIN 66003: §, ß, Ä Ö Ü …)
#: steht im Tooltip.  Tasten, die das Foto nicht zeigt, tragen ihren Scancode.
BESCHRIFTUNG: Dict[Position, Tuple[str, str, str]] = {
    # Zeile 0
    (0, 0): ("1", "!", ""), (0, 1): ("3", "@", "Zeichensatz 2 (SI/SO): §"), (0, 2): ("5", "%", ""),
    (0, 3): ("7", "/", ""), (0, 4): ("9", ")", ""), (0, 5): ("ß", "?", "Kappe „? ~ ß“; ß im Zeichensatz 2, sonst ~"),
    (0, 6): ("⇥", "", "TAB (HT)"), (0, 7): ("−", "", "Ziffernblock"),
    (0, 8): ("→", "", "Cursor rechts"),
    (0, 9): ("|←|", "", "CHAR DELETE (Annahme: Kappensymbol |←|)"),
    (0, 10): ("|→|", "", "CHAR INSERT (Annahme: Kappensymbol |→|)"),
    (0, 11): ("÷", "", "Ziffernblock (sendet „/“)"),
    (0, 14): ("F11", "", "Annahme: F11 (Matrixposition mit PAUSE-Folge, im Terminal 5.0 ohne Wirkung)"),
    # Zeile 1
    (1, 0): ("Q", "", ""), (1, 1): ("E", "", ""), (1, 2): ("T", "", ""), (1, 3): ("U", "", ""),
    (1, 4): ("O", "", ""), (1, 5): ("Ü", "}", "Kappe „} Ü ]“; Ü im Zeichensatz 2"),
    (1, 6): ("+", "", "Ziffernblock „+“ (Annahme). K7673.09 sendet an dieser Position SHIFT (2AH), "
                      "K7673.01 den Code 00H = „+“"),
    (1, 8): ("↓", "", "Cursor runter"), (1, 9): ("⤒", "", "LINE DELETE (Annahme: Kappensymbol ⤒)"),
    (1, 10): ("⤓", "", "LINE INSERT (Annahme: Kappensymbol ⤓)"), (1, 11): ("8", "", "Ziffernblock"),
    (1, 14): ("F8", "", "Annahme: F8 (Scancode 57H, ohne Wirkung)"),
    (1, 15): ("F9", "", "Annahme: F9 (Scancode 58H, ohne Wirkung)"),
    # Zeile 2
    (2, 0): ("A", "", ""), (2, 1): ("D", "", ""), (2, 2): ("G", "", ""), (2, 3): ("J", "", ""),
    (2, 4): ("L", "", ""), (2, 5): ("Ä", "{", "Kappe „{ Ä [“; Ä im Zeichensatz 2"),
    (2, 6): ("CAPS\nLOCK", "", "CAPS LOCK (rastet im Terminal, LED)"), (2, 7): ("=", "", "Ziffernblock"),
    (2, 8): ("←", "", "Cursor links"), (2, 11): ("5", "", "Ziffernblock"),
    (2, 14): ("F4", "", "Annahme: F4 (Scancode 42H, ohne Wirkung)"),
    # Zeile 3
    (3, 0): ("Y", "", ""), (3, 1): ("C", "", ""), (3, 2): ("B", "", ""), (3, 3): ("M", "", ""),
    (3, 4): (".", ":", ""), (3, 6): ("⇕", "", "SHIFT (links und rechts am Foto: ⇕)"),
    (3, 7): ("ENTER", "", "Ziffernblock ENTER (sendet CR)"), (3, 8): ("↵\nRETURN", "", "RETURN (CR)"),
    (3, 11): ("2", "", "Ziffernblock"), (3, 14): ("BREAK", "", "BREAK"),
    (3, 15): ("F1", "", "Annahme: F1 (Scancode 3FH, ohne Wirkung)"),
    # Zeile 4
    (4, 0): ("2", '"', ""), (4, 1): ("4", "$", ""), (4, 2): ("6", "&", ""),
    (4, 3): ("8", "(", ""), (4, 4): ("0", "=", ""), (4, 5): ("´", "`", "Kappe „` ´“ (ASCII ' und `)"),
    (4, 6): ("<", ">", ""), (4, 7): ("DEL", "", "DEL (7FH)"),
    (4, 8): ("0", "", "Ziffernblock"), (4, 9): ("CLEAR", "", "PAGE ERASE (Kappe CLEAR)"),
    (4, 10): ("CE", "", "Ziffernblock CE (Annahme: Matrixposition 45H, das Terminal macht BS daraus)"),
    (4, 11): ("*", "", "Ziffernblock"),
    (4, 12): ("9*", "", "zweite „9“ (45H/49H-Position) — am Foto keine Taste"),
    (4, 14): ("F10", "", "Annahme: F10 (E0 37, ohne Wirkung)"),
    (4, 15): ("F7", "", "Annahme: F7 (Scancode 46H, ohne Wirkung)"),
    # Zeile 5
    (5, 0): ("W", "", ""), (5, 1): ("R", "", ""), (5, 2): ("Z", "", ""), (5, 3): ("I", "", ""),
    (5, 4): ("P", "", ""), (5, 5): ("+", "*", ""), (5, 6): ("ESC", "", "ESC"),
    (5, 7): ("⇤", "", "BACKTAB"),
    (5, 8): ("00", "", "Ziffernblock „00“"), (5, 9): ("↖", "", "HOME"),
    (5, 10): ("7", "", "Ziffernblock"), (5, 11): ("9", "", "Ziffernblock"),
    (5, 12): ("SI\nSO", "", "Zeichensatz 1 ⇄ 2"),
    (5, 14): ("F5", "", "Annahme: F5 (Scancode 43H, ohne Wirkung)"),
    (5, 15): ("F6", "", "Annahme: F6 (Scancode 44H, ohne Wirkung)"),
    # Zeile 6
    (6, 0): ("S", "", ""), (6, 1): ("F", "", ""), (6, 2): ("H", "", ""), (6, 3): ("K", "", ""),
    (6, 4): ("Ö", "|", "Kappe „| Ö \\“; Ö im Zeichensatz 2"),
    (6, 5): ("#", "^", ""), (6, 6): ("⇕", "", "SHIFT (rechts)"), (6, 7): ("BS", "", "BS (Backspace)"),
    (6, 8): (",", "", "Ziffernblock"), (6, 10): ("4", "", "Ziffernblock"),
    (6, 11): ("6", "", "Ziffernblock"), (6, 13): ("CTRL", "", "CTRL (links)"),
    (6, 14): ("F2", "", "Annahme: F2 (Scancode 40H, ohne Wirkung)"),
    (6, 15): ("F3", "", "Annahme: F3 (Scancode 41H, ohne Wirkung)"),
    # Zeile 7
    (7, 0): ("X", "", ""), (7, 1): ("V", "", ""), (7, 2): ("N", "", ""),
    (7, 3): (",", ";", ""), (7, 4): ("-", "_", ""), (7, 7): ("", "", "Leertaste"),
    (7, 9): ("↑", "", "Cursor hoch"), (7, 10): ("1", "", "Ziffernblock"),
    (7, 11): ("3", "", "Ziffernblock"), (7, 12): ("OFF", "", "ON/OFF (rastet im Terminal, LED)"),
    (7, 13): ("CTRL", "", "CTRL (rechts; Annahme: Matrixposition E0 38 — im Terminal 5.0 ohne Wirkung)"),
    (7, 14): ("MOD", "", "MODE (rastet, LED)"), (7, 15): ("VIDEO", "", "VIDEO"),
}

#: Position → (Scancode, Kappe unten, Kappe oben, Tooltip) — die Scancodes sind massgeblich (EPROM-Abzug).
TASTEN: Dict[Position, Tuple[int, str, str, str]] = {
    p: (_SCANCODE[p],) + BESCHRIFTUNG[p] for p in _SCANCODE}

#: Tasten, die im Terminal 5.0 keine Wirkung haben (kein Zeichen, keine Funktion).
OHNE_WIRKUNG: Tuple[Position, ...] = (
    (1, 14), (1, 15), (2, 14), (3, 15), (4, 14), (4, 15), (5, 14), (5, 15), (6, 14), (6, 15),
    (7, 13), (0, 14))

#: Zuordnung der LED-Maske (``term_leds``) zu den Tasten, die sie anzeigen.
LED_TASTEN: Dict[int, Position] = {0x01: (7, 12), 0x02: (2, 6), 0x04: (7, 14)}

#: Rasttasten (die Tastatur rastet nicht mechanisch; die LED zeigt den Zustand des Terminals).
RASTTASTEN = frozenset(LED_TASTEN.values())

#: Tasten, die als Umschalter wirken und von der Bildschirmtastatur „gehalten“ werden können.
UMSCHALTER: Tuple[Position, ...] = ((3, 6), (6, 6), (6, 13))


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
# Der Wirts-SHIFT geht über die linke Shift-Kappe des Fotos; (1, 6) ist dort das „+“ des Ziffernblocks.
SCANCODE_POS[0x2A] = (3, 6)

# ── Tastenbild: Position, x, y, Breite in Tasteneinheiten ───────────────────────────────────────
# Anordnung nach dem Foto des Anwenders: Funktionsreihe oben (OFF | SI/SO MOD VIDEO BREAK | F1–F4 |
# F5–F8 | F9–F11 | Anzeigefeld), darunter Haupt-, Bearbeitungs- und Ziffernblock, Cursorkreuz.
#: Bildhöhe/-breite in Tasteneinheiten (Höhe einer Taste = 1).
BILD_BREITE = 23.4
BILD_HOEHE = 7.6

#: Anzeigefeld oben rechts am Foto: (LED-Bit, Beschriftung, x, y) — die LEDs sitzen NICHT auf den Tasten.
LED_FELD: Tuple[Tuple[int, str, float, float], ...] = (
    (0x01, "OFF", 19.2, 0.0), (0x02, "CAPS", 20.9, 0.0), (0x04, "MOD", 22.4, 0.0))
#: Rahmen des Anzeigefelds (x, y, Breite, Höhe).
LED_RAHMEN = (19.0, 0.0, 4.3, 1.05)


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
    # Funktionsreihe (y = 0): OFF, SI/SO MOD VIDEO BREAK, F1–F4, F5–F8, F9–F11.
    # F1–F11 sind ANNAHME: die elf Positionen ohne Wirkung in aufsteigender Scancode-Folge.
    t += [((7, 12), 0.0, 0.0, 1.0)]
    t += _reihe(0.0, 2.0, _gleich([(5, 12), (7, 14), (7, 15), (3, 14)]))
    fkeys = [(3, 15), (6, 14), (6, 15), (2, 14), (5, 14), (5, 15), (4, 15), (1, 14), (1, 15), (4, 14), (0, 14)]
    for p, x in zip(fkeys, (6.5, 7.5, 8.5, 9.5, 11.0, 12.0, 13.0, 14.0, 15.5, 16.5, 17.5)):
        t.append((p, x, 0.0, 1.0))
    # Zifferntasten-Reihe: ESC 1 2 … 0 ß ´ BS DEL
    t += _reihe(1.5, 0.0, [((5, 6), 1.0)] + _gleich(
        [(0, 0), (4, 0), (0, 1), (4, 1), (0, 2), (4, 2), (0, 3), (4, 3), (0, 4), (4, 4),
         (0, 5), (4, 5)]) + [((6, 7), 1.0), ((4, 7), 1.0)])
    # TAB Q W E R T Z U I O P Ü + BACKTAB
    t += _reihe(2.5, 0.0, [((0, 6), 1.5)] + _gleich(
        [(1, 0), (5, 0), (1, 1), (5, 1), (1, 2), (5, 2), (1, 3), (5, 3), (1, 4), (5, 4),
         (1, 5), (5, 5)]) + [((5, 7), 1.5)])
    # CAPS LOCK A S D F G H J K L Ö Ä # RETURN (RETURN zweizeilig hoch)
    t += _reihe(3.5, 0.5, [((2, 6), 1.4)] + _gleich(
        [(2, 0), (6, 0), (2, 1), (6, 1), (2, 2), (6, 2), (2, 3), (6, 3), (2, 4), (6, 4),
         (2, 5), (6, 5)]))
    # SHIFT < Y X C V B N M , . - SHIFT
    t += _reihe(4.5, 0.0, [((3, 6), 1.25), ((4, 6), 0.8)] + _gleich(
        [(3, 0), (7, 0), (3, 1), (7, 1), (3, 2), (7, 2), (3, 3), (7, 3), (3, 4), (7, 4)])
        + [((6, 6), 1.85)])
    t += [((3, 8), 13.9, 3.5, 1.1)]          # RETURN: Höhe 2 (siehe HOEHE2)
    # CTRL, Leertaste, CTRL
    t += [((6, 13), 2.6, 5.5, 1.0), ((7, 7), 3.6, 5.5, 7.9), ((7, 13), 11.5, 5.5, 1.0)]
    # Bearbeitungsblock: Zeile 1: |←| CLEAR |→| ; Zeile 2: ⤒ HOME ⤓
    t += _reihe(1.5, 15.5, _gleich([(0, 9), (4, 9), (0, 10)]))
    t += _reihe(2.5, 15.5, _gleich([(1, 9), (5, 9), (1, 10)]))
    # Cursorkreuz
    t += [((7, 9), 16.5, 4.5, 1.0)]
    t += _reihe(5.5, 15.5, _gleich([(2, 8), (1, 8), (0, 8)]))
    # Ziffernblock (Spalten bei 19.1, Teilung 1.06); ENTER zweizeilig hoch
    for y, zeile in ((1.5, [(4, 10), (0, 11), (4, 11), (0, 7)]),
                     (2.5, [(5, 10), (1, 11), (5, 11), (1, 6)]),
                     (3.5, [(6, 10), (2, 11), (6, 11), (2, 7)]),
                     (4.5, [(7, 10), (3, 11), (7, 11)]),
                     (5.5, [(4, 8), (5, 8), (6, 8)])):
        for k, p in enumerate(zeile):
            t.append((p, 19.1 + 1.06 * k, y, 1.0))
    t.append(((3, 7), 19.1 + 1.06 * 3, 4.5, 1.0))   # ENTER: Höhe 2
    # Die einzige belegte Position ohne Taste am Foto: schmale Leiste unten
    t.append(((4, 12), 19.1, 6.55, 1.0))
    return t


#: ``(Position, x, y, breite)`` — jede der 105 Positionen genau einmal.
BILD: List[Tuple[Position, float, float, float]] = _bildaufbau()

#: Tasten, die zwei Zeilen hoch sind (RETURN, ENTER des Ziffernblocks).
HOEHE2: Tuple[Position, ...] = ((3, 8), (3, 7))


def taste_hoehe(pos: Position) -> float:
    return 2.0 if pos in HOEHE2 else 1.0

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
    # SHIFT + Taste 56H liefert die Firmware (TGETCHAR) als „>“, ohne SHIFT_Tab-Eintrag.
    aus.setdefault(">", ((4, 6), True))
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
