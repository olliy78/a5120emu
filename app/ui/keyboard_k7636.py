"""
Fruehe A5120 - Bildschirmtastatur K7636.01
==========================================

Nachbildung der **parallelen Tastatur K7636.01** (lateinisch) am U-Bus der K2526
(Belegungsplan ``K7634-36-Serviceschaltplaene.pdf`` S. 8, Abschrift in
``doc/a5120_alt/k7636.md`` / ``k7636_01_codes.csv``).  Gezeichnet wie die K7637 und die
K7634 (:class:`app.ui.keyboard.KeyboardWidget`, gleiche Farben) und mit derselben
Schnittstelle (:attr:`keyPressed`/:attr:`keyReleased`).  Eingehaengt im ``a5120emu`` bei
den Modellen "A5120 (64x16, K7636)" und "A5120 Kassette"; der Kern-Gegenpart ist
``core/peripherals/k7636/`` (``K7636::keyPress``).

**Die Tastenlage ist DATEN** (:data:`LAGE`, eine Zeile je Kappe) und stammt aus dem Foto
einer K7636.01 ohne Gehaeuse (``doc/a5120_alt/k7636.md`` "Bildschirmtastatur nach Foto"):
Hauptblock mit weissen Kappen ESC/CTRL/ALT links und INS/HOME/DEL/END/PgUp/PgDn/Pfeilen
rechts, Ziffernblock mit roten CE/-, S-Tasten (S1 rot, S1/S3/S/S2 hoch), Spalte der
Rechenzeichen (Wahltasten 0-3), Funktionsreihe F mit drei roten Lampenlinsen, CI, M und
der gruenen Betriebstaste.  Leere Fassungen (Loch mit Silberring) sind ``kind="dead"``.

**Was eine Taste sendet.**  Die Tastatur hat einen eigenen U880 und macht aus der
Matrixposition den Code (Tabelle a = Grundstellung, b = Umschaltstellung).  Die
Nachbildung schickt deshalb - wie K7634, K7672 und PC 1715 - die **physische Position**:
``TASTE_BASE | Rechenadresse``.  Die Beschriftung der Kappe folgt dem Foto, der gesendete
Code der Tabelle; wo beides nicht zusammenpasst (Pfeil-Kappen), steht es im Kurzhinweis.
Die Umschaltung (SHIFT, LOCK) haelt die Bildschirmtastatur selbst.

**Anzeigen** (:meth:`set_leds`, Maske von ``k1520_keyboard_leds``): Bit 0...3 = Wahllampen
0...3 (D58, C58, B58, A58), Bit 4 = INS MODE (E15), Bit 5 = Fehlerlampe (Linse F53).  Die
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
    """Rechenadresse (00H-7FH) in einen Kern-Keycode packen."""
    return TASTE_BASE | (adresse & 0x7F)


_C_GRUEN = QColor(0x1f, 0x6b, 0x52)      # Betriebsbereitschaft F59 (Foto)
_C_LAMPE = QColor(0xd2, 0x3b, 0x30)      # rote Linsen F51-F53 (Foto)
_C_LAMPE_AN = QColor(0xff, 0x6a, 0x55)
_C_LOCH = QColor(0x8a, 0x88, 0x7f)       # leere Fassung: graue Tasche (Foto)
_C_RING = QColor(0xc9, 0xc6, 0xba)       # Silberring darin
_C_LOCH_INNEN = QColor(0x1a, 0x1a, 0x18)


@dataclass
class _Taste(_Key):
    """Eine Taste der K7636 - dazu Position, Rechenadresse und Tabellencodes."""

    pos: str = ""
    matrix: int = -1          # Rechenadresse; -1 = keine Position
    a: int = -1               # Code Grundstellung (CTAB 1)
    b: int = -1               # Code Umschaltstellung
    zeichen: str = ""         # Host-Zeichen, bei denen diese Taste aufleuchtet


def _t(pos, x, y, low, up="", w=1.0, h=1.0, style="dark", shape="round", kind="normal",
       name="", zeichen="") -> _Taste:
    adr, a, b = POSITIONEN.get(pos, (-1, -1, -1))
    code = taste(adr) if (kind == "normal" and adr >= 0) else None
    nm = name or (f"{up} / {low}" if up else low).replace("\n", " ")
    return _Taste(x=x, y=y, low=low, up=up, code=code, w=w, h=h, style=style, shape=shape,
                  kind=kind, name=nm, pos=pos, matrix=adr, a=a, b=b,
                  zeichen=zeichen or "".join(c for c in (low, up) if len(c) == 1))


# --- Die Tastenlage als DATEN -------------------------------------------------------------
# Eine Zeile je Kappe: (Position, x, y, Grundebene, Umschaltebene, Optionen).  Raster 1.0 =
# eine Taste; x/y am Foto gemessen.  Zeilen y = 0 (F), 1 (E), 2 (D), 3 (C), 4 (B), 5 (A).
# Optionen = Argumente von _t (w, h, style, shape, kind, name, zeichen).  style: dark |
# light | red | gruen | lampe | filler; shape: round | rect | oval | loch.  Position ""
# = Teil ohne Tabellenzelle (Lampenfassung).

def _hell(**o):
    return dict(style="light", shape="rect", **o)


def _tot(**o):
    """Teil ohne Funktion (Fassung, Lampe, leere Tasche)."""
    return dict(style="filler", shape=o.pop("shape", "rect"), kind="dead", **o)


def _lage_haupt():
    z = []
    # Reihe E: ESC (weiss), Ziffern, Kappe "Pfeil links" (E13), INS-MODE-Lampe, INS, HOME
    z.append(("E00", 0.4, 1.0, "ESC", "", _hell(w=1.15, name="ESC (E00, Code A4H) [?]")))
    ziffern = (("E01", "1", "!"), ("E02", "2", '"'), ("E03", "3", "#"), ("E04", "4", "¤"),
               ("E05", "5", "%"), ("E06", "6", "&"), ("E07", "7", "'"), ("E08", "8", "("),
               ("E09", "9", ")"), ("E10", "0", "_"), ("E11", "-", "="), ("E12", "^", "~"))
    for i, (pos, low, up) in enumerate(ziffern):
        z.append((pos, 1.5 + i, 1.0, low, up, {}))
    z.append(("E13", 13.5, 1.0, "←", "",
              dict(name="E13: Kappe zeigt Pfeil links, Tabellencode 0FH (Tabulator)")))
    z.append(("", 14.5, 1.0, "", "", _tot(w=0.5, name="INS-MODE-Lampe (E15)")))
    z.append(("E16", 15.0, 1.0, "INS", "", _hell(name="INS (E16, Code A8H = INS MODE)")))
    z.append(("E17", 16.0, 1.0, "HOME", "", _hell(name="HOME (E17, Code 1BH)")))
    # Reihe D
    z.append(("D00", 0.45, 2.0, "CTRL", "", _hell(w=1.45, name="CTRL (D00, Code 00H) [?]")))
    for i, (pos, ch) in enumerate(zip(("D01", "D02", "D03", "D04", "D05", "D06", "D07",
                                       "D08", "D09", "D10"), "QWERTYUIOP")):
        z.append((pos, 1.95 + i, 2.0, ch, "", {}))
    z.append(("D11", 11.95, 2.0, "@", "`", {}))
    z.append(("D12", 12.95, 2.0, "[", "{", {}))
    z.append(("D15", 13.95, 2.0, "", "", _tot(name="Blindkappe (D15, nicht bestueckt)")))
    z.append(("D16", 15.0, 2.0, "DEL", "", _hell(name="DEL (D16, Code 03H = INS LINE)")))
    z.append(("D17", 16.0, 2.0, "END", "", _hell(name="END (D17, Code 13H = DEL LINE)")))
    # Reihe C
    z.append(("", 0.0, 3.0, "", "", _tot(w=0.7, name="Umschaltlampe (C99)")))
    z.append(("C00", 1.1, 3.0, "⇕", "",
              dict(kind="lock", name="LOCK (Umschaltfeststeller, 77H)")))
    for i, (pos, ch) in enumerate(zip(("C01", "C02", "C03", "C04", "C05", "C06", "C07",
                                       "C08", "C09"), "ASDFGHJKL")):
        z.append((pos, 2.1 + i, 3.0, ch, "", {}))
    z.append(("C10", 11.1, 3.0, ";", "+", {}))
    z.append(("C11", 12.1, 3.0, ":", "*", {}))
    z.append(("C12", 13.1, 3.0, "]", "}", {}))
    z.append(("C15", 14.15, 3.0, "", "",
              _tot(shape="loch", name="leere Fassung (C15, Tabellencode 0BH)")))
    z.append(("C16", 15.2, 3.0, "PgUp", "", _hell(name="PgUp (C16, Code 04H)")))
    z.append(("C17", 16.2, 3.0, "PgDn", "", _hell(name="PgDn (C17, Code 01H)")))
    # Reihe B
    z.append(("B99", 0.23, 4.0, "⇕", "",
              dict(w=1.25, shape="oval", kind="shift", name="Umschalttaste links (B99, 7FH)")))
    z.append(("B00", 1.45, 4.0, "\\", "|", {}))
    for i, (pos, ch) in enumerate(zip(("B01", "B02", "B03", "B04", "B05", "B06", "B07"),
                                      "ZXCVBNM")):
        z.append((pos, 2.5 + i, 4.0, ch, "", {}))
    z.append(("B08", 9.5, 4.0, ",", "<", {}))
    z.append(("B09", 10.5, 4.0, ".", ">", {}))
    z.append(("B10", 11.5, 4.0, "/", "?", {}))
    z.append(("B11", 12.65, 4.0, "⇕", "",
              dict(w=1.45, shape="oval", kind="shift", name="Umschalttaste rechts (B11, 7FH)")))
    z.append(("B15", 14.15, 4.0, "", "",
              _tot(shape="loch", name="leere Fassung (B15, Tabellencode 06H)")))
    z.append(("B16", 15.2, 4.0, "↓", "",
              _hell(name="Pfeil (B16, Code 0CH); Kappe am Foto ↓ [?]")))
    z.append(("B17", 16.2, 4.0, "", "",
              _tot(shape="loch", name="leere Fassung (B17, Tabellencode 07H)")))
    # Reihe A
    z.append(("A99", 0.1, 5.0, "ALT", "", _hell(w=1.4, name="ALT (A99, Code 00H) [?]")))
    z.append(("A00", 1.55, 5.0, "ET2", "",
              dict(w=1.25, shape="oval",
                   name="ET2 (Eingabetaste 2, FEH) - unter CP/A der Control-Ersatz")))
    z.append(("A05", 3.6, 5.0, "", "",
              dict(w=8.0, shape="oval", name="Leertaste (A02-A08)", zeichen=" ")))
    z.append(("A10", 12.2, 5.0, "ENTER", "",
              dict(w=1.4, shape="oval", name="ENTER (A10, Code FFH = ET1)")))
    z.append(("A15", 14.15, 5.0, "←", "", _hell(name="Pfeil links (A15, Code 0AH = Return)")))
    z.append(("A16", 15.2, 5.0, "↓", "", _hell(name="Pfeil runter (A16, Code 05H)")))
    z.append(("A17", 16.2, 5.0, "→", "",
              _hell(name="Pfeil rechts (A17, Tabellencode 00H) [?]")))
    return z


def _lage_block():
    """Ziffernblock, S-Tasten, Rechenzeichen-Spalte, Funktionsreihe F (Spalten 50...59)."""
    z = []
    z.append(("D50", 17.9, 2.0, "CE", "", dict(style="red", shape="rect")))
    z.append(("C50", 17.9, 3.0, "−", "", dict(style="red", name="Minus rot (C50, Code 2DH)")))
    for pos, ch, x, y in (("D51", "7", 18.95, 2), ("D52", "8", 19.95, 2), ("D53", "9", 20.95, 2),
                          ("C51", "4", 18.95, 3), ("C52", "5", 19.95, 3), ("C53", "6", 20.95, 3),
                          ("B51", "1", 18.95, 4), ("B52", "2", 19.95, 4), ("B53", "3", 20.95, 4),
                          ("A51", "0", 18.95, 5), ("A52", "00", 19.95, 5)):
        z.append((pos, x, float(y), ch, "", dict(name=f"Ziffernblock {ch}")))
    z.append(("A53", 20.95, 5.0, "", "",
              _tot(shape="loch", name="leere Fassung (A53, Tabellencode 12H = 000)")))
    # S-Tasten: S1/S3 und S/S2 sind hohe Kappen (Foto)
    for pos, nm, x, y, h in (("D54", "S4", 22.5, 2.0, 1.0), ("C54", "S1", 22.5, 3.0, 1.55),
                             ("A54", "S", 22.5, 4.8, 1.2),
                             ("D56", "S5", 24.1, 2.0, 1.0), ("C56", "S3", 24.1, 3.0, 1.55),
                             ("A56", "S2", 24.1, 4.8, 1.2),
                             ("D57", "S6", 25.15, 2.0, 1.0), ("C57", "S7", 25.15, 3.0, 1.0),
                             ("B57", "S8", 25.15, 4.0, 1.0), ("A57", "S9", 25.15, 5.0, 1.0)):
        z.append((pos, x, y, nm, "", dict(h=h, shape="oval" if h > 1 else "round",
                                          style="red" if nm == "S1" else "dark")))
    # Wahltasten 0-3 (Spalte 59): am Foto mit Rechenzeichen beschriftet; Lampen in Spalte 58
    for pos, n, y, zeichen in (("D59", 0, 2.0, "−"), ("C59", 1, 3.0, "+"),
                               ("B59", 2, 4.0, "÷"), ("A59", 3, 5.0, "*")):
        z.append((pos, 26.7, y, zeichen, "",
                  _hell(w=1.1, zeichen="",
                        name=f"Wahltaste {n} ({pos}, Code A{n}H), Kappe {zeichen}, "
                             f"Lampe {pos[0]}58")))
        z.append(("", 26.1, y, "", "", _tot(w=0.6, name=f"Wahllampe {pos[0]}58")))
    # Funktionsreihe F
    for pos, x in (("F51", 18.95), ("F52", 19.95), ("F53", 20.95)):
        z.append((pos, x, 0.0, "", "",
                  dict(style="lampe", shape="rect", kind="dead",
                       name=f"rote Lampenlinse ({pos}); F53 = Fehlerlampe")))
    z.append(("F54", 22.4, 0.0, "CI", "",
              dict(style="red", shape="rect", w=1.2, name="CI (F54, Code AFH)")))
    z.append(("", 23.6, 0.0, "", "", _tot(w=0.5, name="Lampe F55")))
    z.append(("F56", 24.1, 0.0, "M", "", _hell(name="M (F56, Code 10H)")))
    z.append(("F57", 25.15, 0.0, "", "", _tot(name="Abdeckbaustein (F57, nicht bestueckt)")))
    z.append(("", 26.1, 0.0, "", "", _tot(w=0.6, name="Lampe F58")))
    z.append(("F59", 26.7, 0.0, "⏻", "",
              dict(style="gruen", shape="rect", w=1.1, kind="dead",
                   name="Betriebsbereitschaft (F59) - wirkt ueber /SA, kein Code")))
    return z


#: Die ganze Lage; die Testschicht prueft sie gegen die Tabelle.
LAGE = _lage_haupt() + _lage_block()


def _build_layout_k7636() -> List[_Taste]:
    """Tasten aus :data:`LAGE` bauen."""
    return [_t(pos, x, y, low, up, **opt) for pos, x, y, low, up, opt in LAGE]


class KeyboardK7636Widget(KeyboardWidget):
    """Anklickbare Nachbildung der K7636.01 - Schnittstelle wie die K7637."""

    # Wanne wie K7637 (Original olivfarben, Vorgabe der Basisklasse)
    PAD = (0.35, 0.45, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_k7636()

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys if isinstance(k, _Taste) and k.pos}
        self._lampen = {k.name: k for k in self._keys
                        if isinstance(k, _Taste) and k.kind == "dead" and k.style == "filler"
                        and not k.pos}

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Anzeigen in den Lampenfassungen: C99, E15, Wahllampen 58, F55/F58 (ohne Befund)."""
        an = {"Umschaltlampe (C99)": self.lock_active(),
              "INS-MODE-Lampe (E15)": bool(self._leds & LED_INS_MODE),
              "Wahllampe D58": bool(self._leds & LED_WAHL0),
              "Wahllampe C58": bool(self._leds & LED_WAHL1),
              "Wahllampe B58": bool(self._leds & LED_WAHL2),
              "Wahllampe A58": bool(self._leds & LED_WAHL3)}
        spots = []
        for name, key in self._lampen.items():
            c = self._rect_of(key, unit, ox, oy).center()
            spots.append((c.x(), c.y(), an.get(name, False), name))
        return spots

    def _cap_colors(self, key: _Key):
        if key.style == "gruen":
            return None, _C_GRUEN, QColor(0xe8, 0xf4, 0xee)
        if key.style == "lampe":
            hell = (key.pos == "F53" and bool(self._leds & LED_FEHLER) and self._blink_on)
            return None, (_C_LAMPE_AN if hell else _C_LAMPE), QColor(0xff, 0xff, 0xff)
        return super()._cap_colors(key)

    def _draw_key(self, p, key: _Key, unit: float, ox: float, oy: float):
        if key.shape != "loch":
            super()._draw_key(p, key, unit, ox, oy)
            return
        # Leere Fassung: graue Tasche mit Silberring und dunkler Bohrung (Foto).
        zelle = self._rect_of(key, unit, ox, oy)
        sp = self.SPALT * unit
        tasche = zelle.adjusted(sp, sp, -sp, -sp)
        p.setPen(Qt.NoPen)
        p.setBrush(_C_LOCH)
        p.drawRoundedRect(tasche, 0.1 * unit, 0.1 * unit)
        m = tasche.center()
        r = 0.27 * unit
        p.setBrush(_C_RING)
        p.drawEllipse(m, r, r)
        p.setBrush(_C_LOCH_INNEN)
        p.drawEllipse(m, 0.19 * unit, 0.19 * unit)

    # -- Echte Tastatur: Hervorhebung ----------------------------------------------------

    _HOST_POS = {
        int(Qt.Key_Return): "A10", int(Qt.Key_Enter): "A10", int(Qt.Key_Tab): "E13",
        int(Qt.Key_Backtab): "C15", int(Qt.Key_Escape): "D17", int(Qt.Key_Backspace): "E17",
        int(Qt.Key_Delete): "E17", int(Qt.Key_Up): "C16", int(Qt.Key_Down): "A16",
        int(Qt.Key_Left): "B15", int(Qt.Key_Right): "B17", int(Qt.Key_Home): "B16",
        int(Qt.Key_Space): "A05",
    }
    #: F1...F9 = S1...S9, F10 = S (wie ``K7636::aufloesen``) [?]
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
        if key.kind == "dead":
            return name
        t = f"{name}: Position {key.pos}, Adresse {key.matrix:02X}H"
        if key.a:
            t += f", Code {key.a:02X}H" + (f"/{key.b:02X}H" if key.b != key.a else "")
        return t
