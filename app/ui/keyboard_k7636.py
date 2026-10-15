"""
Frühe A5120 — Bildschirmtastatur K7636.01
=========================================

Nachbildung der **parallelen Tastatur K7636.01** (lateinisch) am U-Bus der K2526
(Belegungsplan ``K7634-36-Serviceschaltplaene.pdf`` S. 8, Abschrift in
``doc/a5120_alt/k7636.md`` / ``k7636_01_codes.csv``).  Gezeichnet wie die K7637 und die
K7634 (:class:`app.ui.keyboard.KeyboardWidget`) und mit derselben Schnittstelle
(:attr:`keyPressed`/:attr:`keyReleased`).  **Noch nicht in ein Programmprofil eingehängt**
(AP A5); der Kern-Gegenpart ist ``core/peripherals/k7636/`` (``K7636::keyPress``).

**Was eine Taste sendet.**  Die Tastatur hat einen eigenen U880 und macht aus der
Matrixposition den Code (Tabelle a = Grundstellung, b = Umschaltstellung).  Die Nachbildung
schickt deshalb — wie K7634, K7672 und PC 1715 — die **physische Position**:
``TASTE_BASE | Rechenadresse`` (Bit 6…4 Spalte, Bit 3 Matrixnummer, Bit 2…0 Zeile).
``TASTE_BASE`` = ``K7636::QK_TASTE_BASE`` (0x03000000).  Die Umschaltung (SHIFT-Tasten,
LOCK) hält die Bildschirmtastatur selbst und schickt sie als ``shift``-Flag mit; der
Kern setzt daraus die Stufe b.  Die Tabelle ``POSITIONEN`` (Position → Adresse, a, b) ist
**erzeugt** (``tools/k7636_tabelle.py`` aus der CSV), ein Test gleicht sie ab.

**Tastenfeld nach dem Belegungsplan (Reihe A…F, Spalte 00…59).**  Hauptblock Spalten 00…17,
rechts davon der Ziffernblock (Spalte 50…53), die S-Tasten (54…57), die vier Wahltasten
0…3 (Spalte 59, LEDs in Spalte 58) und oben rechts die Funktionsreihe F (CI, M, Lampen,
Betriebsbereitschaft).  Die **Kappenfarben** stehen nicht im Scan (nur „rt“/„gn“ am
Rand einiger Zellen) und sind hier Annahmen **[?]**.  Positionen ohne Taste
(``D15``, ``A17``, ``E00`` …) bleiben leer; ``F57`` ist ein Abdeckbaustein.

**Anzeigen** (:meth:`set_leds`, Maske für die künftige C-ABI [?]): Bit 0…3 = Wahllampen
0…3 (D58, C58, B58, A58), Bit 4 = INS MODE (E15), Bit 5 = Fehlerlampe (F53).  Die
Umschaltlampe C99 folgt dem LOCK der Bildschirmtastatur.
"""

from dataclasses import dataclass
from typing import List

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor

from app.ui.keyboard import KeyboardWidget, _Key
from app.ui.k7636_tabelle import POSITIONEN

#: Entspricht ``K7636::QK_TASTE_BASE`` (core/peripherals/k7636/k7636.h).
TASTE_BASE = 0x03000000

#: Bit der Anzeigemaske.
LED_WAHL0, LED_WAHL1, LED_WAHL2, LED_WAHL3 = 0x01, 0x02, 0x04, 0x08
LED_INS_MODE, LED_FEHLER = 0x10, 0x20


def taste(adresse: int) -> int:
    """Rechenadresse (00H–7FH) in einen Kern-Keycode packen."""
    return TASTE_BASE | (adresse & 0x7F)


_C_WANNE = QColor(0xa8, 0xa6, 0x9c)
_C_ROT = QColor(0xb8, 0x3a, 0x2e)      # CI [?]
_C_GRUEN = QColor(0x4a, 0x8a, 0x4c)    # Betriebsbereitschaft [?]
_C_TXT = QColor(0x3a, 0x39, 0x35)


@dataclass
class _Taste(_Key):
    """Eine Taste der K7636 — dazu Position, Rechenadresse und Tabellencodes."""

    pos: str = ""
    matrix: int = -1          # Rechenadresse; -1 = keine Position
    a: int = -1               # Code Grundstellung (CTAB 1)
    b: int = -1               # Code Umschaltstellung
    zeichen: str = ""         # Host-Zeichen, bei denen diese Taste aufleuchtet


def _t(pos, x, y, low, up="", w=1.0, style="dark", shape="round", kind="normal",
       name="", zeichen="", sendet=True) -> _Taste:
    adr, a, b = POSITIONEN.get(pos, (-1, -1, -1))
    code = taste(adr) if (kind == "normal" and adr >= 0 and sendet) else None
    nm = name or (f"{up} / {low}" if up else low).replace("\n", " ")
    return _Taste(x=x, y=y, low=low, up=up, code=code, w=w, style=style, shape=shape,
                  kind=kind, name=nm, pos=pos, matrix=adr, a=a, b=b,
                  zeichen=zeichen or "".join(c for c in (low, up) if len(c) == 1))


def _build_layout_k7636() -> List[_Taste]:
    r"""Das Tastenfeld; Raster 1.0 = eine Taste.

    ```
    y 0                                                   [LED LED LED] CI [LED] M  .  [LED] ⏻
    y 1   1 2 … 0 - ^ |←|          INS-MODE DEL                             (E-Reihe)
    y 2   Q W … P @ [               INS-LINE DEL-LINE      CE 7 8 9  S4 . S5 S6 (LED) W0
    y 3   LOCK A S … L ; : ]  |←  ↑  →|                    -  4 5 6  S1 . S3 S7 (LED) W1
    y 4   ⇧ \ Z X … M , . / ⇧  ←  ⌂  →                         1 2 3  .  . .  S8 (LED) W2
    y 5   ET2 Leertaste ET1    ↵  ↓                        0 00 000  S  . S2 S9 (LED) W3
    ```
    """
    k: List[_Taste] = []
    # ── Reihe E: Ziffern ─────────────────────────────────────────────────────
    ziffern = (("E01", "1", "!"), ("E02", "2", '"'), ("E03", "3", "#"), ("E04", "4", "¤"),
               ("E05", "5", "%"), ("E06", "6", "&"), ("E07", "7", "'"), ("E08", "8", "("),
               ("E09", "9", ")"), ("E10", "0", "_"), ("E11", "-", "="), ("E12", "^", "~"))
    for i, (pos, low, up) in enumerate(ziffern):
        k.append(_t(pos, 1.0 + i, 1.0, low, up))
    k.append(_t("E13", 13.0, 1.0, "⇥", name="Tabulator-Symbol (E13, 0FH)"))
    k.append(_t("E16", 15.2, 1.0, "INS", "MODE", style="light", shape="rect",
                name="INS MODE (E16, A8H) — LED E15"))
    k.append(_t("E17", 16.2, 1.0, "DEL", style="light", shape="rect"))
    # ── Reihe D ──────────────────────────────────────────────────────────────
    for i, (pos, ch) in enumerate(zip(("D01", "D02", "D03", "D04", "D05", "D06", "D07",
                                       "D08", "D09", "D10"), "QWERTYUIOP")):
        k.append(_t(pos, 1.4 + i, 2.0, ch))
    k.append(_t("D11", 11.4, 2.0, "@", "`"))
    k.append(_t("D12", 12.4, 2.0, "[", "{"))
    k.append(_t("D16", 15.2, 2.0, "INS", "LINE", style="light", shape="rect",
                name="INS LINE (D16, 03H)"))
    k.append(_t("D17", 16.2, 2.0, "DEL", "LINE", style="light", shape="rect",
                name="DEL LINE (D17, 13H)"))
    # ── Reihe C ──────────────────────────────────────────────────────────────
    k.append(_t("C00", 0.65, 3.0, "⇪", kind="lock", name="LOCK (Umschaltfeststeller, 77H)"))
    for i, (pos, ch) in enumerate(zip(("C01", "C02", "C03", "C04", "C05", "C06", "C07",
                                       "C08", "C09"), "ASDFGHJKL")):
        k.append(_t(pos, 1.65 + i, 3.0, ch))
    k.append(_t("C10", 10.65, 3.0, ";", "+"))
    k.append(_t("C11", 11.65, 3.0, ":", "*"))
    k.append(_t("C12", 12.65, 3.0, "]", "}"))
    k.append(_t("C15", 14.2, 3.0, "|←", name="Tab links (C15, 0BH)"))
    k.append(_t("C16", 15.2, 3.0, "↑", name="Cursor hoch (C16, 04H)"))
    k.append(_t("C17", 16.2, 3.0, "→|", name="Tab rechts (C17, 01H)"))
    # ── Reihe B ──────────────────────────────────────────────────────────────
    k.append(_t("B99", 0.0, 4.0, "⇧", w=1.15, shape="oval", kind="shift",
                name="Umschalttaste links (B99, 7FH)"))
    k.append(_t("B00", 1.15, 4.0, "\\", "|"))
    for i, (pos, ch) in enumerate(zip(("B01", "B02", "B03", "B04", "B05", "B06", "B07"),
                                      "ZXCVBNM")):
        k.append(_t(pos, 2.2 + i, 4.0, ch))
    k.append(_t("B08", 9.2, 4.0, ",", "<"))
    k.append(_t("B09", 10.2, 4.0, ".", ">"))
    k.append(_t("B10", 11.2, 4.0, "/", "?"))
    k.append(_t("B11", 12.3, 4.0, "⇧", w=1.4, shape="oval", kind="shift",
                name="Umschalttaste rechts (B11, 7FH)"))
    k.append(_t("B15", 14.2, 4.0, "←", name="Cursor links (B15, 06H)"))
    k.append(_t("B16", 15.2, 4.0, "⌂", name="Cursor Home (B16, 0CH)"))
    k.append(_t("B17", 16.2, 4.0, "→", name="Cursor rechts (B17, 07H)"))
    # ── Reihe A ──────────────────────────────────────────────────────────────
    k.append(_t("A00", 1.25, 5.0, "ET2", w=1.4, shape="oval",
                name="ET2 (Eingabetaste 2, FEH) — unter CP/A der Control-Ersatz"))
    k.append(_t("A05", 3.3, 5.0, "", w=7.9, shape="oval", name="Leertaste (A02–A08)", zeichen=" "))
    k.append(_t("A10", 11.75, 5.0, "ET1", w=1.4, shape="oval", name="ET1 (Eingabetaste 1, FFH)"))
    k.append(_t("A15", 14.2, 5.0, "↵", name="Return-Pfeil (A15, 0AH)"))
    k.append(_t("A16", 15.2, 5.0, "↓", name="Cursor runter (A16, 05H)"))

    # ── Ziffernblock + S-Tasten + Wahltasten (Spalten 50…59) ─────────────────
    X = 17.7
    k.append(_t("D50", X, 2.0, "CE", style="light", shape="rect"))
    k.append(_t("C50", X, 3.0, "−", name="Ziffernblock − (C50)"))
    block = (("D51", "7"), ("D52", "8"), ("D53", "9"),
             ("C51", "4"), ("C52", "5"), ("C53", "6"),
             ("B51", "1"), ("B52", "2"), ("B53", "3"))
    for i, (pos, ch) in enumerate(block):
        k.append(_t(pos, X + 1 + i % 3, 2.0 + i // 3, ch, name=f"Ziffernblock {ch}"))
    k.append(_t("A51", X + 1, 5.0, "0", name="Ziffernblock 0"))
    k.append(_t("A52", X + 2, 5.0, "00", name="Ziffernblock 00 (11H)"))
    k.append(_t("A53", X + 3, 5.0, "000", name="Ziffernblock 000 (12H)"))
    # S-Tasten: S, S1, S3 sind 1½-/3fach-Tasten (hier einfach gezeichnet)
    for pos, nm, x, y in (("D54", "S4", 4, 2), ("D56", "S5", 6, 2), ("D57", "S6", 7, 2),
                          ("C54", "S1", 4, 3), ("C56", "S3", 6, 3), ("C57", "S7", 7, 3),
                          ("B57", "S8", 7, 4), ("A54", "S", 4, 5), ("A56", "S2", 6, 5),
                          ("A57", "S9", 7, 5)):
        k.append(_t(pos, X + x, y, nm, style="light", shape="rect"))
    for pos, n, y in (("D59", 0, 2), ("C59", 1, 3), ("B59", 2, 4), ("A59", 3, 5)):
        k.append(_t(pos, X + 9, y, f"W{n}", style="light", shape="rect",
                    name=f"Wahltaste {n} ({pos}, A{n}H) — LED {pos[0]}58"))
    # ── Funktionsreihe F ─────────────────────────────────────────────────────
    k.append(_t("F54", X + 4, 0.0, "CI", style="red", shape="rect", name="CI (F54, AFH)"))
    k.append(_t("F56", X + 6, 0.0, "M", style="light", shape="rect", name="M (F56, 10H)"))
    return k


class KeyboardK7636Widget(KeyboardWidget):
    """Anklickbare Nachbildung der K7636.01 — Schnittstelle wie die K7637."""

    WANNE = _C_WANNE
    PAD = (0.35, 0.75, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_k7636()

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys if isinstance(k, _Taste) and k.pos}

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Anzeigen: C99 (Umschaltlampe), E15, die vier Wahllampen, Fehlerlampe F53."""
        spots = []

        def neben(pos, dx, dy, an, name):
            r = self._rect_of(self._by_pos[pos], unit, ox, oy)
            spots.append((r.center().x() + dx * unit, r.center().y() + dy * unit, an, name))

        neben("C00", -0.65, 0.0, self.lock_active(), "Umschaltlampe (C99)")
        neben("E16", -0.75, 0.0, bool(self._leds & LED_INS_MODE), "INS-MODE (E15)")
        for pos, bit, n in (("D59", LED_WAHL0, "D58"), ("C59", LED_WAHL1, "C58"),
                            ("B59", LED_WAHL2, "B58"), ("A59", LED_WAHL3, "A58")):
            neben(pos, -1.0, 0.0, bool(self._leds & bit), f"Wahllampe {n}")
        neben("F54", -1.0, 0.0, bool(self._leds & LED_FEHLER) and self._blink_on,
              "Fehlerlampe (F53)")
        return spots

    def _cap_colors(self, key: _Key):
        if key.style == "red":
            return None, _C_ROT, QColor(0xff, 0xff, 0xff)
        return super()._cap_colors(key)

    # ── Echte Tastatur: Hervorhebung ────────────────────────────────────────

    _HOST_POS = {
        int(Qt.Key_Return): "A10", int(Qt.Key_Enter): "A10", int(Qt.Key_Tab): "E13",
        int(Qt.Key_Backtab): "C15", int(Qt.Key_Escape): "D17", int(Qt.Key_Backspace): "E17",
        int(Qt.Key_Delete): "E17", int(Qt.Key_Up): "C16", int(Qt.Key_Down): "A16",
        int(Qt.Key_Left): "B15", int(Qt.Key_Right): "B17", int(Qt.Key_Home): "B16",
        int(Qt.Key_Space): "A05",
    }
    #: F1…F9 = S1…S9, F10 = S (wie ``K7636::aufloesen``) [?]
    _F_POS = ("C54", "A56", "C56", "D54", "D56", "D57", "C57", "B57", "A57", "A54")

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key == int(Qt.Key_CapsLock):
            return [k for k in self._keys if k.kind == "lock"]
        pos = self._HOST_POS.get(key)
        if pos is None and int(Qt.Key_F1) <= key <= int(Qt.Key_F10):
            pos = self._F_POS[key - int(Qt.Key_F1)]
        if pos:
            return [self._by_pos[pos]]
        text = event.text()
        if len(text) == 1:
            treffer = [k for k in self._keys if isinstance(k, _Taste) and k.zeichen
                       and text.upper() in k.zeichen.upper() and k.kind == "normal"]
            if event.modifiers() & Qt.KeypadModifier:
                treffer = [k for k in treffer if k.name.startswith("Ziffernblock")] or treffer
            else:
                treffer = [k for k in treffer if not k.name.startswith("Ziffernblock")] or treffer
            return treffer[:1]
        return []

    @staticmethod
    def _tip(key: _Key) -> str:
        name = key.name or key.low or "Taste"
        if key.kind in ("shift", "lock") or not isinstance(key, _Taste) or key.matrix < 0:
            return name
        t = f"{name}: Position {key.pos}, Adresse {key.matrix:02X}H"
        if key.a:
            t += f", Code {key.a:02X}H" + (f"/{key.b:02X}H" if key.b != key.a else "")
        return t
