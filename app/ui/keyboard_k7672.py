"""
K8915 Emulator — Bildschirmtastatur K7672
=========================================

Eine **Nachbildung der Folientastatur K7672.03** des K8915 (Fotos des Anwenders,
2026-09-29), gezeichnet wie die K7637 des A5120 (`app/ui/keyboard.py`) und mit
derselben Schnittstelle: :attr:`keyPressed`/:attr:`keyReleased`, :meth:`set_leds`,
:meth:`host_key_press`/:meth:`host_key_release`.  Die Oberfläche tauscht nur das
Widget (Programmprofil, ``tastatur = "k7672"``).

**Was eine Taste sendet.**  Jede Taste kennt ihre **Matrixposition** in der
Tastatur (Firmware-Register 22H) und schickt ``TASTE_BASE | Matrix`` an den Kern
(``K7672::QK_TASTE_BASE``).  Was daraus wird, entscheidet der Kern nach den
Tabellen der Firmware (EPROM D3, doc/EPROMS/K7672/README.md):

* **DCP-Modus** (SCPX schaltet ihn beim Kaltstart ein): der Scancode Satz 1 aus
  D3 0080H — Drücken = Code, Loslassen = Code | 80H.  Tasten mit Bit 7 im
  Tabelleneintrag bekommen einen **Vorsatz** (Umschalt 2AH oder Strg 1DH, je
  Tastenart), **kein** ``E0``: ↑ = ``2A 48``, ^S = ``1D 45`` (Strg+Pause — das
  BIOS schaltet damit XOFF samt Lampe).
* **SCP-Modus** (Boot-ROM, Selbsttest, Lader): das Zeichen aus D3 0400H/0480H.
  Funktionstasten senden dort ESC-Folgen aus dem fehlenden Firmwareteil — sie
  **federn zurück und senden nichts**, ebenso ``CL``, das in der Firmware nur den
  Tastenklick umschaltet.

Der Scancode steht hier noch einmal je Taste (:attr:`_Taste.scan`) — nur für den
Kurzhinweis; ``tests/python/test_keyboard_k7672.py`` gleicht ihn mit dem EPROM ab.

**Zuordnung Taste → Matrixposition** aus den Tabellen selbst hergeleitet: die
Zeichentabelle nennt das Zeichen jeder Position, die Scancodetabelle den Code, und
die Zweitbeschriftungen auf dem Foto (``Pg Up``, ``Prt Sc``, ``Num``, ``Pause`` …)
sind genau die PC-Bedeutung dieser Codes.  Offen **[?]**: welche der zwei
Positionen mit Scancode 36H die rechte Umschalttaste ist (16H oder 66H, hier 66H).

**Umschalt und CTRL** rasten wie bei der K7637 für genau eine Taste; der Kern
setzt sie als gehaltene Taste um (2AH/1DH davor, AAH/9DH danach).  **CAPS LOCK**
ist dagegen eine echte Taste (3AH): sie schaltet das BIOS und — in der Firmware —
die Lampe CAPS.

**Die drei Lampen** GRAPH / CAPS / READY folgen ``k1520_keyboard_leds`` (Register
21H der Firmware): READY = Bit 3 (Senden frei, ``DC1``/``DC3``), CAPS = Bit 7
(Feststelltaste, ``ESC [?11h/l``), GRAPH = Bit 6 (``ESC [?18h/l`` — vermutet
**[?]**).  Die drei unbeschrifteten Punkte über ALT1/^S/MOD2: der über ^S zeigt
Bit 0 (``ESC [?13h/l``, vom BIOS bei Strg+Pause = XOFF geschaltet) **[?]**, die
beiden anderen bleiben dunkel (Bedeutung unbekannt).
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple

from PySide6.QtCore import Qt, QPointF, QRectF
from PySide6.QtGui import QColor, QFont, QFontMetricsF, QPainter, QPen

from app.ui.keyboard import KeyboardWidget, _Key, qt_event_to_core_key

#: Muss ``K7672::QK_TASTE_BASE`` entsprechen (``core/peripherals/k7672/k7672.h``).
TASTE_BASE = 0x03000000


def taste(matrix: int) -> int:
    """Matrixposition (00H–7FH) in einen Kern-Keycode packen."""
    return TASTE_BASE | (matrix & 0x7F)


# ── Anzeigen: Bits aus `k1520_keyboard_leds` (Firmware-Register 21H) ────────
LED_XOFF = 0x01    #: ESC [?13h/l — vom BIOS bei Strg+Pause (^S-Taste) geschaltet
LED_READY = 0x08   #: Senden frei (DC1) — grün
LED_GRAPH = 0x40   #: ESC [?18h/l [?]
LED_CAPS = 0x80    #: Feststelltaste / ESC [?11h/l

# ── Farben (dem Foto abgenommen, AP-UI2) ─────────────────────────────────────
# Die K7672 hat KEINEN schwarzen Schacht wie die K7637: der graue Kunststoff
# geht unter den Tasten weiter, der Ausschnitt ist nur im Schatten dunkler, und
# an seinem Rand sitzt eine schmale, noch dunklere Kante.  Gemessen am Foto
# (Tageslicht, daher dunkler als hier): Gehäuse 190/184/164, Kappenoberseite
# ~200/194/175, Kappenflanke ~138/132/116, Kante/Fuge ~40/35/27 (1–3 px breit).
_C_BACK = QColor(0x24, 0x23, 0x20)       # Fläche neben der Tastatur
_C_GEHAEUSE = QColor(0xd2, 0xcc, 0xb8)   # graue Wanne
_C_KANTE = QColor(0xb4, 0xad, 0x98)      # Außenkante der Wanne
_C_GRUND = QColor(0x92, 0x8b, 0x78)      # Grund des Ausschnitts (Schatten)
_C_SCHATTEN = QColor(0x5e, 0x59, 0x4d)   # Kante des Ausschnitts
_C_FLANKE = QColor(0xc4, 0xbe, 0xaa)     # Flanke der Kappe
_C_KAPPE = QColor(0xe2, 0xde, 0xcd)      # Oberseite der Kappe
_C_BLIND = QColor(0xae, 0xa8, 0x93)      # Blindstück links neben CAPS LOCK
_C_TEXT = QColor(0x2c, 0x2b, 0x27)
_C_ROT = QColor(0xc8, 0x2a, 0x2a)        # rote Zweitbeschriftung (PC-Bedeutung)
_C_GRUEN = QColor(0x2f, 0x86, 0x4a)      # grüne Zweitbeschriftung (3270-Funktion)
_C_GRAU = QColor(0x5a, 0x58, 0x52)       # Zweitbeschriftung am Ziffernblock
_C_FELD = QColor(0xdc, 0xd6, 0xc3)       # Lampenfeld
_C_ACTIVE = QColor(0xd0, 0x80, 0x10)     # rastender Modifikator an
_FARBEN = {"rot": _C_ROT, "gruen": _C_GRUEN, "grau": _C_GRAU}

# ── Maße in Tastenrastern (dem Foto abgenommen, AP-UI2) ──────────────────────
# Raster 55,9 px auf dem Foto; Kappe 51,8 px breit ⇒ Fuge 0,075; Mittelblock
# beginnt 0,50 hinter DEL, Ziffernblock 0,50 hinter PF12 — das Raster selbst
# stimmte schon.  Zu weit WIRKTEN die Abstände durch schmale Kappen (0,88) in
# einer schwarzen Einfassung von 0,07.
FUGE = 0.075        #: Spalt zwischen zwei Kappen (je Seite die Hälfte)
GRUND_RAND = 0.03   #: so weit reicht der Ausschnitt über die Kappe hinaus
KANTE_RAND = 0.025  #: dunkle Kante des Ausschnitts, außen um den Grund
ECKE = 0.10         #: Radius der Kappenecke (der Ausschnitt folgt ihr)

#: (an, aus) je Lampenfarbe.
_LAMPE = {
    "gelb": (QColor(0xff, 0xd0, 0x30), QColor(0x8a, 0x80, 0x55)),
    "gruen": (QColor(0x3c, 0xe0, 0x6a), QColor(0x3d, 0x6b, 0x5a)),
    "orange": (QColor(0xff, 0x8a, 0x20), QColor(0xb8, 0x8a, 0x4a)),
}


@dataclass
class _Taste(_Key):
    """Eine Taste der K7672 — dazu Matrixposition, Scancode und Frontbeschriftung."""

    matrix: int = -1
    scan: int = -1               # aus der Firmware (D3 0080H), nur für den Hinweis
    front: str = ""              # Zweitbeschriftung vorn an der Kappe
    front_farbe: str = "rot"     # rot | gruen | grau
    gruppe: str = ""             # Tastenblock (ein Ausschnitt im Gehäuse)
    zeichen: str = ""            # Host-Zeichen, bei denen diese Taste aufleuchtet
    rechts: str = ""             # Umlauttasten: großes Zeichen rechts (Ü, Ö, Ä, ß)
    rechts_unten: bool = False   # …in der unteren statt der oberen Zeile (ß)


def _t(x, y, low, m, scan, up="", w=1.0, h=1.0, front="", ff="rot", gruppe="haupt",
       name="", kind="normal", zeichen="", vertical=False, rechts="",
       rechts_unten=False) -> _Taste:
    return _Taste(x=x, y=y, low=low, up=up, code=None if kind != "normal" else taste(m),
                  w=w, h=h, style="light", shape="rect", kind=kind,
                  name=name or (f"{up} / {low}" if up else low), vertical=vertical,
                  matrix=m, scan=scan, front=front, front_farbe=ff, gruppe=gruppe,
                  zeichen=zeichen, rechts=rechts, rechts_unten=rechts_unten)


#: Feste Blindstücke im Tastenfeld: (x, y, w, h, Gruppe) — keine Tasten, aber
#: Teil des Ausschnitts.  Auf dem Foto links neben CAPS LOCK.
BLINDSTUECKE = ((0.0, 3.5, 0.5, 1.0, "haupt"),)


def _build_layout_k7672() -> List[_Taste]:
    r"""Das Tastenfeld der K7672.03 nach dem Foto (Tastenraster, 1.0 = eine Taste).

    ```
    y 0    CTRL · ALT1 ^S MOD2 PF1 · PF2–PF5 · PF6–PF9 · CLEAR RESET BREAK · Lampen
    y 1.5  ESC 1 … 0 ß ´ |←| DEL          · PF10 PF11 PF12 · CE / * −
    y 2.5  →| Q … Ü *+ |←                  · PA2/1 PA3 GRAPH · 7 8 9 +
    y 3.5  ▯ CAPS A … Ö Ä ^# RETURN        ·                 · 4 5 6 =
    y 4.5  ↕ <> Y … M ;, :. _- ↕ (RETURN)  ·       ↑         · 1 2 3 ENTER
    y 5.5    ALT Leertaste CL              ·     ← ↓ →       · 0 , .  (ENTER)
    ```

    ▯ = festes Blindstück (BLINDSTUECKE); RETURN ist EINE Taste über zwei Reihen.
    """
    k: List[_Taste] = []
    MB, NB = 15.5, 19.0            # linke Kante Mittelblock / Ziffernblock
    Y1, Y2, Y3, Y4, Y5 = 1.5, 2.5, 3.5, 4.5, 5.5

    # ── Funktionsreihe ───────────────────────────────────────────────────────
    k.append(_t(0.0, 0.0, "CTRL", 0x7C, 0x1D, gruppe="ctrl", kind="ctrl",
                name="CTRL (Steuertaste, rastet für eine Taste)"))
    for i, (low, m, sc, front, ff, name) in enumerate((
            ("ALT1", 0x5C, 0xB8, "", "rot", "ALT1 (Strg + 38H)"),
            ("^S", 0x7E, 0xC5, "Pause", "rot", "^S (Strg + Pause: XOFF)"),
            ("MOD2", 0x7F, 0xD2, "Ins", "rot", "MOD2 (Umschalt + Ins)"),
            ("PF1", 0x3E, 0x3B, "Ins M", "gruen", "PF1"))):
        k.append(_t(2.0 + i, 0.0, low, m, sc, front=front, ff=ff, gruppe="f1", name=name))
    for i, (low, m, sc, front) in enumerate((
            ("PF2", 0x3F, 0x3C, "Ins L"), ("PF3", 0x6E, 0x3D, "Del L"),
            ("PF4", 0x6F, 0x3E, "E Inp"), ("PF5", 0x2E, 0x3F, "E EOF"))):
        k.append(_t(6.5 + i, 0.0, low, m, sc, front=front, ff="gruen", gruppe="f2"))
    for i, (low, m, sc, front) in enumerate((
            ("PF6", 0x5E, 0x40, "Dup"), ("PF7", 0x5F, 0x41, "FM"),
            ("PF8", 0x1E, 0x42, ""), ("PF9", 0x1F, 0x43, ""))):
        k.append(_t(11.0 + i, 0.0, low, m, sc, front=front, ff="gruen", gruppe="f3"))
    for i, (low, m, sc, front, name) in enumerate((
            ("CLEAR", 0x4E, 0xB7, "Prt Sc", "CLEAR (Umschalt + Prt Sc)"),
            ("RESET", 0x4F, 0x45, "Num", "RESET der Tastatur (Num)"),
            ("BREAK", 0x0E, 0x46, "SRoll", "BREAK (Rollen)"))):
        k.append(_t(MB + i, 0.0, low, m, sc, front=front, gruppe="f4", name=name))

    # ── Reihe 1: Ziffern ─────────────────────────────────────────────────────
    k.append(_t(0.0, Y1, "ESC", 0x56, 0x01, zeichen="\x1b"))
    ziffern = (("1", "!", 0x00, 0x02), ("2", '"', 0x40, 0x03), ("3", "@ §", 0x01, 0x04),
               ("4", "$", 0x41, 0x05), ("5", "%", 0x02, 0x06), ("6", "&", 0x42, 0x07),
               ("7", "/", 0x03, 0x08), ("8", "(", 0x43, 0x09), ("9", ")", 0x04, 0x0A),
               ("0", "=", 0x44, 0x0B), ("ß", "?", 0x05, 0x0C), ("´", "`", 0x45, 0x0D))
    for i, (low, up, m, sc) in enumerate(ziffern):
        k.append(_t(1.0 + i, Y1, low, m, sc, up=up,
                    zeichen=low + up.replace(" ", "") + {"ß": "~", "´": "'"}.get(low, "")))
    # ß: links oben ?, links unten ¯ (~ = 7EH, die ASCII-Lage von ß), rechts unten ß.
    sz = next(t for t in k if t.matrix == 0x05)
    sz.low, sz.rechts, sz.rechts_unten, sz.name = "¯", "ß", True, "ß"
    k.append(_t(13.0, Y1, "|←|", 0x67, 0x0E, name="Rücktaste |←|", zeichen="\x08"))
    k.append(_t(14.0, Y1, "DEL", 0x47, 0xD3, name="DEL (Umschalt + Entf)", zeichen="\x7f"))

    # ── Reihe 2: QWERTZ ──────────────────────────────────────────────────────
    k.append(_t(0.0, Y2, "→|", 0x06, 0x0F, w=1.5, name="Tabulator →|", zeichen="\t"))
    for i, (ch, m) in enumerate(zip("QWERTZUIOP",
                                    (0x10, 0x50, 0x11, 0x51, 0x12, 0x52, 0x13, 0x53,
                                     0x14, 0x54))):
        sc = (0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19)[i]
        k.append(_t(1.5 + i, Y2, ch, m, sc, zeichen=ch + ch.lower()))
    # Umlauttasten wie auf dem Foto: links oben/unten die ASCII-Zeichen derselben
    # Codes (DIN 66003 ↔ ASCII), rechts groß der Umlaut.
    k.append(_t(11.5, Y2, "]", 0x15, 0x1A, up="}", rechts="Ü", name="Ü (} ])",
                zeichen="]}"))
    k.append(_t(12.5, Y2, "+", 0x55, 0x1B, up="*", zeichen="+*"))
    k.append(_t(13.5, Y2, "|←", 0x57, 0x8F, w=1.5, name="Tabulator zurück |← (Umschalt + Tab)"))

    # ── Reihe 3: ASDF ────────────────────────────────────────────────────────
    # Links neben CAPS LOCK sitzt ein festes Blindstück (BLINDSTUECKE).
    k.append(_t(0.5, Y3, "CAPS\nLOCK", 0x26, 0x3A, w=1.25,
                name="CAPS LOCK (Feststelltaste, Lampe CAPS)"))
    for i, (ch, m, sc) in enumerate(zip("ASDFGHJKL",
                                        (0x20, 0x60, 0x21, 0x61, 0x22, 0x62, 0x23, 0x63, 0x24),
                                        range(0x1E, 0x27))):
        k.append(_t(1.75 + i, Y3, ch, m, sc, zeichen=ch + ch.lower()))
    k.append(_t(10.75, Y3, "\\", 0x64, 0x27, up="|", rechts="Ö", name="Ö (| \\)",
                zeichen="\\|"))
    k.append(_t(11.75, Y3, "[", 0x25, 0x28, up="{", rechts="Ä", name="Ä ({ [)",
                zeichen="[{"))
    k.append(_t(12.75, Y3, "#", 0x65, 0x29, up="^", zeichen="#^"))
    k.append(_t(13.75, Y3, "↵\nRETURN", 0x38, 0x1C, w=1.25, h=2.0, name="RETURN",
                zeichen="\r"))

    # ── Reihe 4: YXCV ────────────────────────────────────────────────────────
    k.append(_t(0.0, Y4, "↕", 0x36, 0x2A, w=1.25, kind="shift",
                name="Umschalttaste links (rastet für eine Taste)"))
    k.append(_t(1.25, Y4, "<", 0x46, 0x2B, up=">", zeichen="<>"))
    for i, (ch, m, sc) in enumerate(zip("YXCVBNM",
                                        (0x30, 0x70, 0x31, 0x71, 0x32, 0x72, 0x33),
                                        (0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32))):
        k.append(_t(2.25 + i, Y4, ch, m, sc, zeichen=ch + ch.lower()))
    k.append(_t(9.25, Y4, ",", 0x73, 0x33, up=";", zeichen=",;"))
    k.append(_t(10.25, Y4, ".", 0x34, 0x34, up=":", zeichen=".:"))
    k.append(_t(11.25, Y4, "-", 0x74, 0x35, up="_", zeichen="-_"))
    # Welche der beiden Matrixpositionen mit 36H die rechte ist, sagt keine Quelle [?].
    k.append(_t(12.25, Y4, "↕", 0x66, 0x36, w=1.5, kind="shift",
                name="Umschalttaste rechts (rastet für eine Taste)"))

    # ── Reihe 5: Leertaste ───────────────────────────────────────────────────
    k.append(_t(2.5, Y5, "ALT", 0x6D, 0x38, name="ALT (38H)"))
    k.append(_t(3.5, Y5, "", 0x77, 0x39, w=8.0, name="Leertaste", zeichen=" "))
    k.append(_t(11.5, Y5, "CL", 0x7D, 0x00,
                name="CL — schaltet in der Firmware nur den Tastenklick um"))

    # ── Mittelblock ──────────────────────────────────────────────────────────
    for i, (low, m, sc, front) in enumerate((("PF10", 0x09, 0x44, ""),
                                             ("PF11", 0x49, 0xC9, "Pg Up"),
                                             ("PF12", 0x0A, 0xD1, "Pg Dn"))):
        k.append(_t(MB + i, Y1, low, m, sc, front=front, gruppe="mitte"))
    k.append(_t(MB, Y2, "PA 1", 0x19, 0xCF, up="PA 2", front="End", gruppe="mitte",
                name="PA2 / PA1 (Umschalt + End)"))
    k.append(_t(MB + 1, Y2, "↖", 0x59, 0xC7, up="PA 3", gruppe="mitte",
                name="PA3 / ↖ (Umschalt + Pos1)"))
    k.append(_t(MB + 2, Y2, "GRAPH", 0x1A, 0x7E, gruppe="mitte",
                name="GRAPH (7EH; das BIOS sendet darauf ESC [?18l)"))
    k.append(_t(MB + 1, Y4, "↑", 0x79, 0xC8, gruppe="kursor", name="Kursor aufwärts"))
    k.append(_t(MB, Y5, "←", 0x28, 0xCB, gruppe="kursor", name="Kursor links"))
    k.append(_t(MB + 1, Y5, "↓", 0x18, 0xD0, gruppe="kursor", name="Kursor abwärts"))
    k.append(_t(MB + 2, Y5, "→", 0x08, 0xCD, gruppe="kursor", name="Kursor rechts"))

    # ── Ziffernblock ─────────────────────────────────────────────────────────
    nb = (
        (0, Y1, "CE", 0x4A, 0x79, ""), (1, Y1, "/", 0x0B, 0x7A, "Win"),
        (2, Y1, "*", 0x4B, 0x37, "Ref"), (3, Y1, "−", 0x07, 0x4A, "Bk"),
        (0, Y2, "7", 0x5A, 0x47, "Choi"), (1, Y2, "8", 0x1B, 0x48, "Pick"),
        (2, Y2, "9", 0x5B, 0x49, "Loc"), (3, Y2, "+", 0x17, 0x4E, "Stro"),
        (0, Y3, "4", 0x6A, 0x4B, "Copy"), (1, Y3, "5", 0x2B, 0x4C, "Split"),
        (2, Y3, "6", 0x6B, 0x4D, "Std"), (3, Y3, "=", 0x27, 0x7B, "Pos"),
        (0, Y4, "1", 0x7A, 0x4F, ""), (1, Y4, "2", 0x3B, 0x50, ""),
        (2, Y4, "3", 0x7B, 0x51, ""),
        (0, Y5, "0", 0x48, 0x52, ""), (1, Y5, ",", 0x58, 0x7D, ""),
        (2, Y5, ".", 0x68, 0x53, ""),
    )
    for dx, y, low, m, sc, front in nb:
        k.append(_t(NB + dx, y, low, m, sc, front=front, ff="grau", gruppe="ziffern",
                    name=f"Ziffernblock {low}" + (f" ({front})" if front else "")))
    k.append(_t(NB + 3, Y4, "ENTER", 0x37, 0x7C, h=2.0, gruppe="ziffern",
                vertical=True, name="ENTER (Ziffernblock, 7CH)"))
    return k


class KeyboardK7672Widget(KeyboardWidget):
    """Anklickbare Nachbildung der K7672 (K8915) — Schnittstelle wie die K7637."""

    #: Oben Platz für die drei Punkte über ALT1/^S/MOD2.
    PAD = (0.35, 0.65, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_k7672()

    def _anzeigen_verankern(self):
        self._by_matrix = {k.matrix: k for k in self._keys if isinstance(k, _Taste)}
        self._punkt_tasten = [self._by_matrix[m] for m in (0x5C, 0x7E, 0x7F)]

    # ── Anzeigen ─────────────────────────────────────────────────────────────

    def set_leds(self, mask: int):
        """Register 21H der Firmware (`k1520_keyboard_leds`) — kein Blinken hier."""
        mask = int(mask) & 0xFF
        if mask != self._leds:
            self._leds = mask
            self.update()

    def set_powered(self, on: bool):
        """Die K7672 hat keine eigene Betriebsanzeige — READY erlischt mit dem Rechner."""
        self._powered = bool(on)

    def lampen(self) -> dict:
        """{Name: leuchtet?} — GRAPH, CAPS, READY (für Tests und den Kurzhinweis)."""
        return {name: an for name, an, *_ in self._lampen_zustand()}

    def _lampen_zustand(self):
        return (("GRAPH", bool(self._leds & LED_GRAPH), "gelb"),
                ("CAPS", bool(self._leds & LED_CAPS), "gelb"),
                ("READY", bool(self._leds & LED_READY), "gruen"))

    def _lampenfeld(self, unit: float, ox: float, oy: float) -> QRectF:
        x0 = ox + (self._pad[0] + 19.0) * unit
        y0 = oy + (self._pad[1] + 0.0) * unit
        return QRectF(x0, y0, 4.0 * unit, 1.0 * unit)

    def _led_spots(self, unit: float, ox: float, oy: float):
        """(x, y, leuchtet, Bezeichnung) — die drei Lampen und die drei Punkte."""
        feld = self._lampenfeld(unit, ox, oy)
        spots = []
        for i, (name, an, _farbe) in enumerate(self._lampen_zustand()):
            cx = feld.x() + feld.width() * (0.17 + 0.33 * i)
            spots.append((cx, feld.y() + feld.height() * 0.66, an, name))
        punkt_y = oy + (self._pad[1] - 0.33) * unit
        for taste_, (an, name) in zip(self._punkt_tasten, (
                (False, "unbeschrifteter Punkt über ALT1 — Bedeutung unbekannt [?]"),
                (bool(self._leds & LED_XOFF),
                 "unbeschrifteter Punkt über ^S — vermutlich XOFF (ESC [?13h/l) [?]"),
                (False, "unbeschrifteter Punkt über MOD2 — Bedeutung unbekannt [?]"))):
            spots.append((self._rect_of(taste_, unit, ox, oy).center().x(), punkt_y,
                          an, name))
        return spots

    # ── Zeichnen ─────────────────────────────────────────────────────────────
    #
    # Die Ausschnitte im Gehäuse werden NICHT als Pfad vereinigt.  Bis AP-UI1
    # entstand die Einfassung je Block aus ``QPainterPath.addRect`` aller
    # (vergrößerten, sich überlappenden) Zellen + ``simplified()`` — und Qts
    # Pfad-Vereinigung verliert bei bestimmten Fließkomma-Lagen fast
    # zusammenfallender Kanten ganze Teilflächen: bei rund einem Drittel aller
    # Fensterbreiten fehlte die Einfassung von RETURN und der rechten
    # Umschalttaste, und die helle Wanne lag zwischen den Tasten (AP-UI2,
    # Befund „rot").  Jetzt wird jede Fläche einzeln DECKEND gemalt — je Zelle
    # ein abgerundetes Rechteck, dazu eckige Brücken zwischen Nachbarzellen,
    # die Fugen und Innenecken schließen.  Überdeckung statt Mengenlehre: das
    # kann nichts verlieren, bei keiner Größe.

    def _zellen(self):
        """(x, y, w, h, Gruppe) aller Zellen im Tastenfeld — Tasten und Blindstücke."""
        zellen = [(k.x, k.y, k.w, k.h, getattr(k, "gruppe", "")) for k in self._keys]
        zellen.extend(BLINDSTUECKE)
        return zellen

    def _zelle_px(self, z, unit: float, ox: float, oy: float) -> QRectF:
        x, y, w, h, _g = z
        return QRectF(ox + (self._pad[0] + x) * unit, oy + (self._pad[1] + y) * unit,
                      w * unit, h * unit)

    def ausschnitt_flaechen(self, unit: float, ox: float, oy: float, rand: float):
        """Die Flächen, die zusammen den Ausschnitt eines Blocks bilden.

        Liefert ``[(QRectF, Eckradius)]``: je Zelle ihr Umriss (um *rand* Raster
        über die Kappe hinaus, Ecken = Kappenecke + *rand* — der Ausschnitt folgt
        der Rundung der Tasten), dazu je Paar aneinanderstoßender Zellen
        desselben Blocks eine eckige Brücke von Mitte zu Mitte über die
        gemeinsame Kante.  Die Brücken decken die Fuge und die Innenecken, wo
        vier Kappen zusammenstoßen; außen bleiben die Rundungen stehen.
        """
        eps = 1e-6
        halb = FUGE / 2.0
        flaechen = []
        zellen = self._zellen()
        for z in zellen:
            r = self._zelle_px(z, unit, ox, oy)
            d = (rand - halb) * unit
            flaechen.append((r.adjusted(-d, -d, d, d), (ECKE + rand) * unit))
        e = (rand - halb) * unit
        for i, a in enumerate(zellen):
            for b in zellen[i + 1:]:
                if a[4] != b[4]:
                    continue
                ax0, ay0, ax1, ay1 = a[0], a[1], a[0] + a[2], a[1] + a[3]
                bx0, by0, bx1, by1 = b[0], b[1], b[0] + b[2], b[1] + b[3]
                oy0, oy1 = max(ay0, by0), min(ay1, by1)
                ox0, ox1 = max(ax0, bx0), min(ax1, bx1)
                if (abs(ax1 - bx0) < eps or abs(bx1 - ax0) < eps) and oy1 - oy0 > eps:
                    # nebeneinander: Brücke über die ganze gemeinsame Höhe
                    links, rechts = (a, b) if abs(ax1 - bx0) < eps else (b, a)
                    x0 = links[0] + links[2] / 2.0
                    x1 = rechts[0] + rechts[2] / 2.0
                    y0, y1 = oy0, oy1
                elif (abs(ay1 - by0) < eps or abs(by1 - ay0) < eps) and ox1 - ox0 > eps:
                    oben, unten = (a, b) if abs(ay1 - by0) < eps else (b, a)
                    y0 = oben[1] + oben[3] / 2.0
                    y1 = unten[1] + unten[3] / 2.0
                    x0, x1 = ox0, ox1
                else:
                    continue
                r = self._zelle_px((x0, y0, x1 - x0, y1 - y0, ""), unit, ox, oy)
                flaechen.append((r.adjusted(-e, -e, e, e), 0.0))
        return flaechen

    def _male_flaechen(self, p: QPainter, flaechen, farbe: QColor):
        p.setPen(Qt.NoPen)
        p.setBrush(farbe)
        for rect, ecke in flaechen:
            if ecke > 0.0:
                p.drawRoundedRect(rect, ecke, ecke)
            else:
                p.drawRect(rect)

    def paintEvent(self, event):
        unit, ox, oy = self._geometry()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        p.fillRect(self.rect(), _C_BACK)

        wanne = QRectF(ox, oy, self._units_w * unit, self._units_h * unit)
        p.setPen(QPen(_C_KANTE, max(1.0, 0.05 * unit)))
        p.setBrush(_C_GEHAEUSE)
        p.drawRoundedRect(wanne, 0.2 * unit, 0.2 * unit)

        # Ausschnitt: erst die dunkle Kante (etwas größer), darüber der Grund.
        self._male_flaechen(p, self.ausschnitt_flaechen(unit, ox, oy,
                                                        GRUND_RAND + KANTE_RAND),
                            _C_SCHATTEN)
        self._male_flaechen(p, self.ausschnitt_flaechen(unit, ox, oy, GRUND_RAND),
                            _C_GRUND)

        for z in BLINDSTUECKE:
            r = self._zelle_px(z, unit, ox, oy)
            d = FUGE / 2.0 * unit
            p.setPen(Qt.NoPen)
            p.setBrush(_C_BLIND)
            p.drawRoundedRect(r.adjusted(d, d, -d, -d), ECKE * unit, ECKE * unit)

        for key in self._keys:
            self._draw_key(p, key, unit, ox, oy)

        self._draw_lampenfeld(p, unit, ox, oy)
        p.end()

    def _draw_lampenfeld(self, p: QPainter, unit: float, ox: float, oy: float):
        feld = self._lampenfeld(unit, ox, oy)
        p.setPen(QPen(_C_KANTE, 1.0))
        p.setBrush(_C_FELD)
        p.drawRoundedRect(feld, 0.12 * unit, 0.12 * unit)
        zustand = self._lampen_zustand()
        spots = self._led_spots(unit, ox, oy)
        for (name, an, farbe), (cx, cy, _lit, _n) in zip(zustand, spots[:3]):
            p.setPen(_C_TEXT)
            self._draw_text(p, name, QRectF(cx - 0.6 * unit, feld.y() + 0.05 * unit,
                                            1.2 * unit, 0.4 * unit), unit * 0.24)
            ein, aus = _LAMPE[farbe]
            p.setPen(QPen(_C_SCHATTEN, 1.0))
            p.setBrush(ein if an else aus)
            p.drawRect(QRectF(cx - 0.17 * unit, cy - 0.07 * unit, 0.34 * unit, 0.14 * unit))
        for cx, cy, an, _name in spots[3:]:
            ein, aus = _LAMPE["orange"]
            p.setPen(QPen(_C_KANTE, 1.0))
            p.setBrush(ein if an else aus)
            p.drawEllipse(QPointF(cx, cy), 0.07 * unit, 0.07 * unit)

    def kappe_von(self, key: _Key, unit: float, ox: float, oy: float) -> QRectF:
        """Umriss der Kappe (Zelle abzüglich der halben Fuge) — auch für Tests."""
        d = FUGE / 2.0 * unit
        return self._rect_of(key, unit, ox, oy).adjusted(d, d, -d, -d)

    @staticmethod
    def _oberseite(kappe: QRectF, unit: float) -> QRectF:
        """Die Oberseite der Kappe — ringsum die Flanke, vorn am breitesten."""
        return kappe.adjusted(0.045 * unit, 0.025 * unit, -0.045 * unit, -0.12 * unit)

    def _draw_key(self, p: QPainter, key: _Key, unit: float, ox: float, oy: float):
        kappe = self.kappe_von(key, unit, ox, oy)
        ecke = ECKE * unit
        gedrueckt = self._is_down(key)
        flanke = _C_FLANKE.darker(112) if gedrueckt else _C_FLANKE
        oben_farbe = _C_KAPPE.darker(114) if gedrueckt else _C_KAPPE

        p.setPen(Qt.NoPen)
        p.setBrush(flanke)
        p.drawRoundedRect(kappe, ecke, ecke)
        oben = self._oberseite(kappe, unit)
        p.setBrush(oben_farbe)
        p.drawRoundedRect(oben, 0.8 * ecke, 0.8 * ecke)

        if self._is_active(key):
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(_C_ACTIVE, max(1.5, 0.05 * unit)))
            p.drawRoundedRect(kappe.adjusted(1, 1, -1, -1), ecke, ecke)

        self._draw_legend_k7672(p, key, oben, kappe, unit)

    def _text_an(self, p: QPainter, text: str, box: QRectF, size: float,
                 ausrichtung=Qt.AlignLeft | Qt.AlignVCenter):
        """Wie ``_draw_text``, aber mit wählbarer Ausrichtung (die K7672 ist
        links oben beschriftet, nicht mittig)."""
        if not text:
            return
        font = QFont(p.font())
        font.setWeight(QFont.DemiBold)      # die Kappen sind kräftig bedruckt
        size = max(5.0, size)
        while size > 5.0:
            font.setPixelSize(max(5, int(round(size))))
            if QFontMetricsF(font).horizontalAdvance(text) <= box.width():
                break
            size -= 0.75
        font.setPixelSize(max(5, int(round(size))))
        p.setFont(font)
        p.drawText(box, ausrichtung, text)

    #: Tasten, deren Zeichen mittig steht (Kursorkreuz, Pfeile auf dem Foto).
    _MITTIG = ("↑", "↓", "←", "→", "↕", "")

    def legenden_boxen(self, key: _Key, oben: QRectF, unit: float):
        """``[(Text, QRectF, Pixelgröße, Ausrichtung)]`` — wo welche Beschriftung
        steht (getrennt vom Malen, damit die Tests die Lage prüfen können).

        Nach dem Foto: links oben beschriftet; zwei Ebenen links übereinander;
        Umlauttasten links die beiden ASCII-Zeichen, rechts groß der Umlaut;
        RETURN oben ↵, unten „RETURN"; ENTER senkrecht.
        """
        mx, my = 0.07 * unit, 0.03 * unit
        innen = oben.adjusted(mx, my, -mx, -my)
        zeile = min(0.34 * unit, innen.height() / 2.0)
        L = Qt.AlignLeft | Qt.AlignVCenter
        C = Qt.AlignCenter
        oz = QRectF(innen.x(), innen.y(), innen.width(), zeile)
        uz = QRectF(innen.x(), innen.bottom() - zeile, innen.width(), zeile)
        klein, gross = 0.22 * unit, 0.30 * unit
        boxen = []
        if key.vertical:
            zeichen = [c for c in key.low if not c.isspace()]
            hoehe = innen.height() * 0.85 / max(1, len(zeichen))
            y0 = innen.y() + innen.height() * 0.05
            spalte = QRectF(innen.x(), 0, innen.width() * 0.4, 0)
            for i, c in enumerate(zeichen):
                boxen.append((c, QRectF(spalte.x(), y0 + i * hoehe, spalte.width(), hoehe),
                              min(0.24 * unit, hoehe * 0.95), C))
        elif getattr(key, "rechts", ""):
            links = innen.width() * 0.42
            boxen.append((key.up, QRectF(oz.x(), oz.y(), links, oz.height()), klein, L))
            boxen.append((key.low, QRectF(uz.x(), uz.y(), links, uz.height()), klein, L))
            ziel = uz if key.rechts_unten else oz
            boxen.append((key.rechts, QRectF(ziel.x() + links, ziel.y(),
                                             ziel.width() - links, ziel.height()),
                          gross, C))
        elif "\n" in key.low:
            zeilen = key.low.split("\n")
            if key.h > 1.0:
                # RETURN: oben groß ↵, unten klein der Name — eine hohe Taste.
                boxen.append((zeilen[0], oz, 0.40 * unit, L))
                boxen.append((" ".join(zeilen[1:]), uz, 0.20 * unit, L))
            else:
                h = min(zeile, innen.height() / len(zeilen))
                for i, z in enumerate(zeilen):
                    boxen.append((z, QRectF(innen.x(), innen.y() + i * h, innen.width(), h),
                                  0.19 * unit, L))
        elif key.up:
            boxen.append((key.up, oz, klein, L))
            boxen.append((key.low, uz, klein if len(key.low) > 1 else gross * 0.9, L))
        elif key.low in self._MITTIG:
            boxen.append((key.low, innen, 0.34 * unit, C))
        else:
            groesse = gross if len(key.low) <= 2 else 0.21 * unit
            boxen.append((key.low, oz, groesse, L))
        return [b for b in boxen if b[0]]

    def _draw_legend_k7672(self, p: QPainter, key: _Key, oben: QRectF,
                           kappe: QRectF, unit: float):
        p.setPen(_C_TEXT)
        for text, box, groesse, ausrichtung in self.legenden_boxen(key, oben, unit):
            self._text_an(p, text, box, groesse, ausrichtung)
        front = getattr(key, "front", "")
        if front:
            # Vorn auf der Flanke, wie auf dem Foto (Pause, Pg Up, Choi …).
            p.setPen(_FARBEN.get(getattr(key, "front_farbe", "rot"), _C_ROT))
            unten = QRectF(kappe.x(), oben.bottom(), kappe.width(),
                           kappe.bottom() - oben.bottom())
            font = QFont(p.font())
            font.setItalic(True)
            p.setFont(font)
            self._draw_text(p, front, unten, unit * 0.14)
            font.setItalic(False)
            p.setFont(font)

    # ── Die ECHTE Tastatur ──────────────────────────────────────────────────

    #: Qt-Sondertaste → Matrixposition (nur für die Hervorhebung; gesendet wird
    #: der Qt-Code, den der Kern selbst übersetzt — `K7672::tasteDcp`).
    _HOST_MATRIX = {
        int(Qt.Key_Return): 0x38, int(Qt.Key_Enter): 0x37, int(Qt.Key_Tab): 0x06,
        int(Qt.Key_Backtab): 0x57, int(Qt.Key_Escape): 0x56,
        int(Qt.Key_Backspace): 0x28, int(Qt.Key_Delete): 0x47,   # AP-E4m: ← statt |←|
        int(Qt.Key_Up): 0x79, int(Qt.Key_Down): 0x18, int(Qt.Key_Left): 0x28,
        int(Qt.Key_Right): 0x08, int(Qt.Key_CapsLock): 0x26,
        int(Qt.Key_Alt): 0x6D, int(Qt.Key_Space): 0x77,
        int(Qt.Key_PageUp): 0x49, int(Qt.Key_PageDown): 0x0A,
    }
    _F_MATRIX = (0x3E, 0x3F, 0x6E, 0x6F, 0x2E, 0x5E, 0x5F, 0x1E, 0x1F, 0x09, 0x49, 0x0A)

    def map_host_key(self, event) -> Optional[Tuple[int, bool, bool]]:
        """Host-Taste → ``(code, shift, ctrl)``: der Qt-/ASCII-Code, wie ihn der Kern
        für die K7672 übersetzt (`K7672::tasteDcp`/`zeichenFuer`), dazu die rastende
        CTRL der Nachbildung und — für Buchstaben — ihre rastende Umschalttaste.

        Anders als bei der K7637 gibt es hier keinen Umweg über die Taste der
        Nachbildung: die Übersetzung Host-Zeichen → Taste steht im Kern (nach der
        Tastentabelle des BIOS).  Die Feststelltaste des PCs geht als eigene Taste
        durch (3AH) — das BIOS führt die Feststellung, nicht die Oberfläche.
        """
        if int(event.key()) == int(Qt.Key_CapsLock):
            return (int(Qt.Key_CapsLock), False, False)
        mapped = qt_event_to_core_key(event)
        if mapped is None:
            return None
        code, shift, ctrl = mapped
        ctrl = ctrl or self._ctrl
        if self._shift and 0x61 <= code <= 0x7A and not ctrl:
            code -= 0x20
            shift = True
        return (code, shift, ctrl)

    # ── Umschalttasten des PCs und Tastenwiederholung (AP-E4g) ──────────────

    #: Reine Modifikatoren des PCs, die der K8915 als EIGENE Tasten braucht: im
    #: DCP-Modus meldet die K7672 Umschalt (2AH) und Strg (1DH) einzeln, damit das
    #: BIOS z. B. Umschalt+PF1 unterscheiden kann.  Der Kern setzt sie zusammen mit
    #: der je Zeichen gewählten Umschaltung um (kein Doppel: `K7672::dcpDruecken`
    #: vergleicht mit dem Zustand, den der Rechner kennt).  **ALT fehlt mit Absicht**:
    #: Qt gibt das Drücken nicht zuverlässig an das Widget (die Menüleiste wertet es
    #: zuerst aus und holt sich beim Loslassen den Fokus); der ALT des K8915 ist an
    #: der Bildschirmtastatur (Matrix 6DH) zu haben.
    _MODIFIKATOREN = (int(Qt.Key_Shift), int(Qt.Key_Control))

    def __init__(self, *args, **kwargs):
        # Je Host-Taste der Kern-Code, mit dem sie GEDRÜCKT wurde.  Das Loslassen
        # muss denselben Code tragen: ändert sich die Umschaltung dazwischen
        # (Umschalt zuerst losgelassen: `A` → `a`), träfe es sonst eine andere
        # Taste, und die Tastenwiederholung der K7672 liefe weiter.
        self._core_down = {}
        super().__init__(*args, **kwargs)

    def host_key_press(self, event):
        key = int(event.key())
        if key in self._MODIFIKATOREN:
            self._mark_down(event)
            self._core_down[key] = key
            return (key, False, False)
        mapped = super().host_key_press(event)
        if mapped is not None:
            self._core_down[key] = mapped[0]
        return mapped

    def host_key_release(self, event):
        key = int(event.key())
        if key in self._MODIFIKATOREN:
            # Die rastenden Umschalttasten der Nachbildung bleiben unberührt.
            self._mark_up(event)
            self._core_down.pop(key, None)
            return (key, False, False)
        mapped = super().host_key_release(event)
        code = self._core_down.pop(key, None)
        if mapped is not None and code is not None:
            mapped = (code,) + tuple(mapped[1:])
        return mapped

    def clear_host_keys(self):
        """Fokusverlust: alle gehaltenen Tasten loslassen — auch im Kern.

        Sonst bliebe Umschalt/Strg dort hängen und die zuletzt gedrückte Taste
        wiederholte sich endlos (die K7672 wiederholt selbst, bis das Loslassen kommt).
        """
        codes = list(self._core_down.values())
        self._core_down.clear()
        super().clear_host_keys()
        for code in codes:
            self.keyReleased.emit(int(code))

    def lock_active(self) -> bool:
        """Keine eigene Feststellung — die führt das BIOS (Lampe CAPS)."""
        return False

    def _note_host_caps(self, event):
        """Die Feststellung der PC-Tastatur zählt hier nicht (s. :meth:`lock_active`)."""
        return

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [k for k in self._keys if k.kind == "ctrl"]
        if key in self._HOST_MATRIX:
            t = self._by_matrix.get(self._HOST_MATRIX[key])
            return [t] if t else []
        if int(Qt.Key_F1) <= key <= int(Qt.Key_F12):
            t = self._by_matrix.get(self._F_MATRIX[key - int(Qt.Key_F1)])
            return [t] if t else []
        text = event.text()
        if len(text) == 1:
            ziffernblock = bool(event.modifiers() & Qt.KeypadModifier)
            treffer = [k for k in self._keys if text in getattr(k, "zeichen", "")]
            if not treffer:
                treffer = [k for k in self._keys if isinstance(k, _Taste)
                           and k.low == text and k.gruppe == "ziffern"]
            if ziffernblock:
                treffer = ([k for k in self._keys if isinstance(k, _Taste)
                            and k.gruppe == "ziffern" and k.low == text] or treffer)
            return treffer[:1]
        return []

    # ── Kurzhinweis ──────────────────────────────────────────────────────────

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "ctrl"):
            return name
        scan = getattr(key, "scan", -1)
        m = getattr(key, "matrix", -1)
        if scan < 0 or scan == 0xFF or m == 0x7D:
            return f"{name} — sendet nichts"
        if scan & 0x80:
            teile = f"Scancode {scan & 0x7F:02X}H mit Vorsatz"
        else:
            teile = f"Scancode {scan:02X}H"
        return f"{name}: {teile} (Taste {m:02X}H)"
