"""
K8915 (Gen 1) — Bildschirmtastatur K7634
========================================

Nachbildung der **parallelen Tastatur K7634** am K8915 der ersten Bauart
(Foto des Anwenders ``Tastatur_K89xx.jpg``, 2026-10-05).  Gezeichnet wie die
K7637 (`app/ui/keyboard.py`) und mit derselben Schnittstelle (:attr:`keyPressed`
/:attr:`keyReleased`).  **Noch nicht in ein Programmprofil eingehängt** — Gen 1
ist nicht auswählbar, und der Kern hat noch kein Modell der K7634
(`doc/k8915g2/k7634.md` §7, Auftrag V9).

**Was eine Taste sendet.**  Die K7634 ist eine intelligente Tastatur mit eigenem
U880 (k7634.md §0): sie tastet 16 Spalten × 8 Zeilen ab und legt aus ihrem ROM
den Code der Taste auf den Bus.  Die Nachbildung schickt deshalb — wie K7672 und
PC 1715 — die **physische Position**: ``TASTE_BASE | Rechenadresse`` (k7634.md
§3.4: Bit 6…4 Spalte, Bit 3 Matrixnummer, Bit 2…0 Zeile).  Den Code a/b
(Grundstellung/Umschaltstellung) kennt jede Taste zusätzlich (:attr:`_Taste.a`,
:attr:`_Taste.b`) — nur für Kurzhinweis und Tests; ein künftiges Kernmodell
liest dieselbe Tabelle.  ``TASTE_BASE`` ist [?] noch ohne Gegenstück im Kern.

**Die Tabelle** (:data:`TABELLE`) ist die der Variante **K7634.04** (k7634.md §5,
Reihenfolge der ROM-Gruppen ``A0, A8, A1, A9 …``); ein Test gleicht sie mit der
Markdown-Tabelle ab.  Abweichung: ``E02`` hat in der Vorlage ``32/33`` (Druckfehler
[?]) — hier wie in der Doku vorläufig ``22H`` (:data:`KORREKTUREN`).

**Das Foto zeigt eine ANDERE Fassung als die Tabelle.**  Die Beschriftung stammt
vom Foto, der Code von der **Position** der Kappe — beides stimmt nicht überall
überein, das ist offen **[?]** (k7634.md §5.1, §8 Nr. 2; Frage F7):

* Tasten, die in der Tabelle nicht vorkommen (``STRG CHOI PICK LOC``, ``UPDATE ⇑ ⇓``)
  sitzen auf Positionen, die die K7634.04 mit ``00H`` führt (unbestückt):
  ``G17 G51 G52 G53`` bzw. ``E51 E52 E53`` — in dieser Reihenfolge angesetzt [?].
  Sie senden die Position, ihr Tabellencode ist 00H.
* Tasten, deren Beschriftung von der Tabelle abweicht, tragen den Tabellencode ihrer
  Position: ``TAB←`` = G01 (Tabelle: ERASE INP 09), ``TAB→`` = E00 (ERASE EOF 08),
  ``CLEAR TAB`` = G03 (Tabelle: CNCL FE), ``HOLD`` = E16 (INS MODE A8), ``NEXT PAGE`` =
  E17 (PA3 DEL 1B/F8), ``CR`` = D16 (PA1 DUP 1C/FA), ``LF`` = D17 (PA2 FM 1E/F9),
  ``ESC`` = C16 (INS LINE 03), ``DEL`` = C17 (DEL LINE 13).  Die Zeilen/Spalten
  der Positionen passen exakt auf die Lage der Kappen im Foto — das stützt die Zuordnung.
* **CTRL** (links unten) kennt die K7634.04 nicht (k7634.md §3.4: nur K7636/.10, ``A99``);
  die Kappe ist gedruckt, aber ohne Position: sie rastet wie eine Umschalttaste und
  sendet nur den Zustand [?].  Die zwei Umschalttasten (``B11``/``B99``, 7FH) und
  LOCK (``C00``, 77H) sind Modifikatoren der Tastatur-Firmware.

**Drei Kappen fehlen am Gerät** (blanke Schalterstifte im Foto) — hier als
gestrichelt umrandete, beschriftete Tasten **ergänzt [?]**:

* ``PF1`` (G05, ``C1H``) links von ``EDI PF2``;
* ``REC`` (G04, ``FDH``) daneben, links von PF1 — **nicht** CNCL.  Die Folge der
  Positionen G01…G06 (TAB←, CLEAR, CLEAR TAB, ·, ·, PF2) geht nur mit G04/G05 auf;
  CNCL (``FEH``) ist G03 und sitzt unter der Kappe ``CLEAR TAB``;
* die Kappe neben ``ENTER`` im Cursorblock (unterer Schalterstift links von ←): Position
  A15, ``↵`` (``0AH``, Zeilenvorschub/Neue Zeile).  Die ←-Taste (A16, ``06H``) ist im Foto
  **vorhanden** (zwischen dem leeren Stift und →); die Annahme „die ←-Kappe fehlt" trifft
  nach Foto und Matrix die Nachbarposition.  Der Cursorblock ist in der Tabelle ein
  sauberes 2×3-Feld B15 B16 B17 / A15 A16 A17 = ⇤ ↓ ↑ / ↵ ← → — die Lücke ist A15 [?].

**Was ``CLEAR TAB`` und ``PF1`` bedeuten.**  Die Doku sagt dazu nichts Ausdrückliches:
``PF1`` ist die programmierbare Funktionstaste 1 (Code ``C1H``, wie PF2…PF12 = ``C2…CCH``;
das BIOS Krzikalla erwartet in einer anderen Fassung ``91H``); sie löst nichts selbst aus,
die Bedeutung legt die Anwendung fest.  ``CLEAR TAB`` hat keinen Tabellen-Eintrag unter
diesem Namen; seine Position G03 liefert ``FEH`` (Tabelle: CNCL = Cancel; Krzikalla-Fassung
``8FH``).  Die Kappe ist also eine Umbeschriftung der CNCL-Taste; was die Anwendung daraus
macht, legt sie fest.  Sprachlich ist es die übliche Terminal-Funktion „Tabulatorstopp(s)
löschen" (Gegenstück zum Setzen) — **Vermutung [?]**, vom Code nicht belegt.

**Kein Kernanschluss:** :meth:`map_host_key` ist die der K7637 (ASCII über
``qt_event_to_core_key``); erst ein Kernmodell (V9) legt fest, ob die PC-Tastatur
Positionen oder Zeichen schickt.  Die Bildschirmtastatur selbst schickt Positionen.

**Umschaltung** wie die Doku: Großschreibung ist die Grundstellung, SHIFT/LOCK geben die
Kleinbuchstaben (k7634.md §3.4) — das entscheidet die Tastatur-Firmware, nicht die Oberfläche.
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple

from PySide6.QtCore import Qt, QPointF, QRectF
from PySide6.QtGui import QColor, QPainter, QPen

from app.ui.keyboard import KeyboardWidget, _Key, qt_event_to_core_key

#: Wie bei K7672/PC 1715; ein Gegenstück im Kern gibt es noch nicht [?].
TASTE_BASE = 0x03000000


def taste(adresse: int) -> int:
    """Rechenadresse (00H–7FH) in einen Kern-Keycode packen."""
    return TASTE_BASE | (adresse & 0x7F)


#: K7634.04 (k7634.md §5): je ROM-Gruppe (Leitung A0, A8, A1, A9 … A7, A15) die acht Zeilen
#: Z0…Z7 als (Positionsname, Code a, Code b).  Leerer Name = Matrixpunkt unbestückt.
TABELLE = (
    (("G51", 0x00, 0x00), ("", 0x00, 0x00), ("D52", 0x38, 0x38), ("B52", 0x32, 0x32), ("C52", 0x35, 0x35), ("A52", 0x2D, 0x2D), ("E52", 0x00, 0x00), ("", 0x00, 0x00)),
    (("G04", 0xFD, 0xFD), ("B03", 0x43, 0x63), ("E02", 0x32, 0x33), ("C03", 0x44, 0x64), ("D03", 0x45, 0x65), ("A05", 0x20, 0x20), ("E03", 0x33, 0x23), ("B04", 0x56, 0x76)),
    (("G17", 0x00, 0x00), ("", 0x00, 0x00), ("D51", 0x37, 0x37), ("B51", 0x31, 0x31), ("C51", 0x34, 0x34), ("A51", 0x30, 0x30), ("E51", 0x00, 0x00), ("", 0x00, 0x00)),
    (("G01", 0x09, 0x09), ("", 0x00, 0x00), ("D00", 0x10, 0x10), ("B00", 0x01, 0x01), ("C01", 0x41, 0x61), ("A00", 0xAF, 0xAF), ("E00", 0x08, 0x08), ("B01", 0x5A, 0x7A)),
    (("G16", 0xCC, 0xCC), ("", 0x00, 0x00), ("D17", 0x1E, 0xF9), ("B17", 0x04, 0x04), ("C17", 0x13, 0x13), ("A17", 0x07, 0x07), ("E17", 0x1B, 0xF8), ("", 0x00, 0x00)),
    (("G05", 0xC1, 0xC1), ("B05", 0x42, 0x62), ("D05", 0x54, 0x74), ("C04", 0x46, 0x66), ("D04", 0x52, 0x72), ("", 0x00, 0x00), ("E04", 0x34, 0x24), ("C05", 0x47, 0x67)),
    (("G13", 0xC9, 0xC9), ("B15", 0x0B, 0x0B), ("E12", 0x5E, 0x7E), ("D15", 0x5C, 0x7C), ("E13", 0x0F, 0x0F), ("A15", 0x0A, 0x0A), ("G14", 0xCA, 0xCA), ("C15", 0x5F, 0x5F)),
    (("G10", 0xC6, 0xC6), ("B09", 0x2E, 0x3E), ("E08", 0x38, 0x28), ("C08", 0x4B, 0x6B), ("D08", 0x49, 0x69), ("", 0x00, 0x00), ("G09", 0xC5, 0xC5), ("C09", 0x4C, 0x6C)),
    (("G15", 0xCB, 0xCB), ("", 0x00, 0x00), ("D16", 0x1C, 0xFA), ("B16", 0x05, 0x05), ("C16", 0x03, 0x03), ("A16", 0x06, 0x06), ("E16", 0xA8, 0xA8), ("", 0x00, 0x00)),
    (("G06", 0xC2, 0xC2), ("B06", 0x4E, 0x6E), ("E05", 0x35, 0x25), ("C06", 0x48, 0x68), ("D06", 0x59, 0x79), ("", 0x00, 0x00), ("G07", 0xC3, 0xC3), ("", 0x00, 0x00)),
    (("G12", 0xC8, 0xC8), ("C11", 0x3A, 0x2A), ("E11", 0x2D, 0x3D), ("D12", 0x5B, 0x7B), ("D11", 0x40, 0x60), ("", 0x00, 0x00), ("E10", 0x30, 0x20), ("C12", 0x5D, 0x7D)),
    (("G53", 0x00, 0x00), ("", 0x00, 0x00), ("D53", 0x39, 0x39), ("B53", 0x33, 0x33), ("C53", 0x36, 0x36), ("A53", 0x2C, 0x2C), ("E53", 0x00, 0x00), ("G52", 0x00, 0x00)),
    (("G08", 0xC4, 0xC4), ("B07", 0x4D, 0x6D), ("E06", 0x36, 0x26), ("C07", 0x4A, 0x6A), ("D07", 0x55, 0x75), ("", 0x00, 0x00), ("E07", 0x37, 0x27), ("B08", 0x2C, 0x3C)),
    (("G02", 0xFC, 0xFC), ("B02", 0x58, 0x78), ("E01", 0x31, 0x21), ("D01", 0x51, 0x71), ("D02", 0x57, 0x77), ("", 0x00, 0x00), ("G03", 0xFE, 0xFE), ("C02", 0x53, 0x73)),
    (("G11", 0xC7, 0xC7), ("B10", 0x2F, 0x3F), ("D09", 0x4F, 0x6F), ("C10", 0x3B, 0x2B), ("D10", 0x50, 0x70), ("A10", 0xFF, 0xFF), ("E09", 0x39, 0x29), ("C00", 0x00, 0x00)),
    (("SL1", 0xA9, 0xA9), ("SL2", 0xAA, 0xAA), ("SL3", 0xAB, 0xAB), ("SL4", 0xAC, 0xAC), ("SL5", 0xAD, 0xAD), ("SL6", 0xAE, 0xAE), ("", 0x00, 0x00), ("B11/B99", 0x00, 0x00)),
)

#: Berichtigungen gegenüber der gedruckten Vorlage: Position → (a, b).
KORREKTUREN = {"E02": (0x32, 0x22)}   # Druckfehler 33H in der Vorlage [?]

#: Taste mit Position: Name → (Rechenadresse, a, b).  ``B99`` ist die zweite
#: Umschalttaste (links), sie teilt sich Adresse 7FH mit ``B11``.
POSITIONEN = {}
for _i, _gruppe in enumerate(TABELLE):
    for _z, (_pos, _a, _b) in enumerate(_gruppe):
        if not _pos or _pos.startswith("SL"):
            continue
        _a, _b = KORREKTUREN.get(_pos, (_a, _b))
        _adr = ((_i // 2) << 4) | ((_i % 2) << 3) | _z
        for _p in _pos.split("/"):
            POSITIONEN[_p] = (_adr, _a, _b)
del _i, _gruppe, _z, _pos, _a, _b, _adr, _p

# ── Farben (dem Foto abgenommen) ─────────────────────────────────────────────
_C_WANNE = QColor(0x9c, 0x9b, 0x93)      # graues Gehäuse
_C_CREME = QColor(0xe6, 0xd8, 0xb6)      # EDI/TKB/FOR/FLX/VFY
_C_GRAU = QColor(0x8f, 0x92, 0x88)       # TAB←, CLEAR, CLEAR TAB, TAB→, CTRL
_C_TXT = QColor(0x3a, 0x39, 0x35)
_C_ERGAENZT = QColor(0xff, 0xa0, 0x20)   # Rahmen der ergänzten Kappen


@dataclass
class _Taste(_Key):
    """Eine Taste der K7634 — dazu Position, Rechenadresse und Tabellencodes."""

    pos: str = ""
    matrix: int = -1            # Rechenadresse; -1 = keine Position (CTRL)
    a: int = -1                 # Code Grundstellung (Tabelle K7634.04)
    b: int = -1                 # Code Umschaltstellung
    ergaenzt: bool = False      # Kappe fehlt am Gerät, hier nach Doku ergänzt [?]
    zeichen: str = ""           # Host-Zeichen, bei denen diese Taste aufleuchtet


def _t(pos, x, y, low, up="", w=1.0, h=1.0, style="dark", shape="round", kind="normal",
       name="", ergaenzt=False, zeichen="") -> _Taste:
    adr, a, b = POSITIONEN.get(pos, (-1, -1, -1))
    code = taste(adr) if (kind == "normal" and adr >= 0) else None
    nm = name or (f"{up} / {low}" if up else low).replace("\n", " ")
    if ergaenzt:
        nm += " [?] ergänzt (Kappe fehlt am Gerät)"
    return _Taste(x=x, y=y, low=low, up=up, code=code, w=w, h=h, style=style, shape=shape,
                  kind=kind, name=nm, pos=pos, matrix=adr, a=a, b=b, ergaenzt=ergaenzt,
                  # Nur EINZELzeichen-Beschriftungen: sonst träfe „o" die Kappe OFF, „h" HOLD, „p" PF7.
                  zeichen=zeichen or "".join(c for c in (low, up) if len(c) == 1))


def _build_layout_k7634() -> List[_Taste]:
    r"""Das Tastenfeld nach dem Foto (Raster 1.0 = eine Taste, Maße am Foto gemessen).

    ```
    y 0   TAB← CLEAR CLEARTAB [REC] [PF1] PF2…PF6 PF7…PF12 STRG CHOI PICK LOC
    y 2   TAB→ 1…0 = ^ |←        HOLD NEXTPAGE      UPDATE ⇑ ⇓
    y 3   OFF Q…P @ [ \          CR LF              7 8 9
    y 4   LOCK A…L ; * ] _       ESC DEL            4 5 6
    y 5   ⇧ →| Z…M , . / ⇧ ⇤ ↓ ↑                    1 2 3
    y 6   CTRL RESET Leertaste ENTER [↵] ← →        0 - ,
    ```
    """
    k: List[_Taste] = []
    # ── Funktionsreihe ───────────────────────────────────────────────────────
    k.append(_t("G01", 0.0, 0, "TAB←", style="grau", shape="rect", name="TAB← (Position G01)"))
    k.append(_t("G02", 1.0, 0, "CLEAR", style="grau", shape="rect"))
    k.append(_t("G03", 2.0, 0, "TAB", "CLEAR", style="grau", shape="rect",
                name="CLEAR TAB (Position G03, Tabelle: CNCL) [?]"))
    k.append(_t("G04", 3.0, 0, "REC", style="light", shape="rect", ergaenzt=True))
    k.append(_t("G05", 4.0, 0, "PF1", style="creme", shape="rect", ergaenzt=True))
    for i, (pos, up, low) in enumerate((("G06", "EDI", "PF2"), ("G07", "TKB", "PF3"),
                                        ("G08", "FOR", "PF4"), ("G09", "FLX", "PF5"),
                                        ("G10", "VFY", "PF6"))):
        k.append(_t(pos, 5.0 + i, 0, low, up, style="creme", shape="rect"))
    for i, pos in enumerate(("G11", "G12", "G13", "G14", "G15", "G16")):
        k.append(_t(pos, 10.0 + i, 0, f"PF{7 + i}", style="light", shape="rect"))
    for i, (pos, nm) in enumerate((("G17", "STRG"), ("G51", "CHOI"),
                                   ("G52", "PICK"), ("G53", "LOC"))):
        k.append(_t(pos, 16.0 + i, 0, nm, style="light", shape="rect",
                    name=f"{nm} (Position {pos}, Tabelle: unbestückt) [?]"))

    # ── Ziffernreihe ─────────────────────────────────────────────────────────
    Y1 = 2.0
    k.append(_t("E00", 0.0, Y1, "TAB→", style="grau", shape="rect",
                name="TAB→ (Position E00, Tabelle: ERASE EOF)"))
    ziffern = (("E01", "1", "!"), ("E02", "2", '"'), ("E03", "3", "#"), ("E04", "4", "¤"),
               ("E05", "5", "%"), ("E06", "6", "&"), ("E07", "7", "'"), ("E08", "8", "("),
               ("E09", "9", ")"), ("E10", "0", ""), ("E11", "-", "="), ("E12", "^", "‾"))
    for i, (pos, low, up) in enumerate(ziffern):
        k.append(_t(pos, 1.0 + i, Y1, low, up))
    k.append(_t("E13", 13.0, Y1, "|←", name="|← (Position E13, Tabelle: ⇥)"))
    k.append(_t("E16", 14.5, Y1, "HOLD", style="light", shape="rect",
                name="HOLD (Position E16, Tabelle: INS MODE)"))
    k.append(_t("E17", 15.5, Y1, "PAGE", "NEXT", style="light", shape="rect",
                name="NEXT PAGE (Position E17, Tabelle: PA3 DEL)"))
    for i, (pos, low, up) in enumerate((("E51", "DATE", "UP"), ("E52", "⇑", ""),
                                        ("E53", "⇓", ""))):
        k.append(_t(pos, 17.0 + i, Y1, low, up, style="light", shape="rect",
                    name={"E51": "UPDATE", "E52": "⇑", "E53": "⇓"}[pos]
                    + f" (Position {pos}, Tabelle: unbestückt) [?]"))

    # ── Reihe D ──────────────────────────────────────────────────────────────
    Y2 = 3.0
    k.append(_t("D00", 0.35, Y2, "OFF"))
    for i, (pos, ch) in enumerate(zip(("D01", "D02", "D03", "D04", "D05", "D06", "D07",
                                       "D08", "D09", "D10"), "QWERTYUIOP")):
        k.append(_t(pos, 1.4 + i, Y2, ch))
    k.append(_t("D11", 11.4, Y2, "@", "`"))
    k.append(_t("D12", 12.4, Y2, "[", "{"))
    k.append(_t("D15", 13.4, Y2, "\\", "|"))
    k.append(_t("D16", 14.5, Y2, "CR", style="light", shape="rect",
                name="CR (Position D16, Tabelle: PA1 DUP)"))
    k.append(_t("D17", 15.5, Y2, "LF", style="light", shape="rect",
                name="LF (Position D17, Tabelle: PA2 FM)"))
    # ── Reihe C ──────────────────────────────────────────────────────────────
    Y3 = 4.0
    k.append(_t("C00", 0.65, Y3, "↕", kind="lock", name="LOCK (Feststeller, 77H)"))
    for i, (pos, ch) in enumerate(zip(("C01", "C02", "C03", "C04", "C05", "C06", "C07",
                                       "C08", "C09"), "ASDFGHJKL")):
        k.append(_t(pos, 1.65 + i, Y3, ch))
    k.append(_t("C10", 10.65, Y3, ";", "+"))
    k.append(_t("C11", 11.65, Y3, ":", "*"))
    k.append(_t("C12", 12.65, Y3, "]", "}"))
    k.append(_t("C15", 13.65, Y3, "_"))
    k.append(_t("C16", 14.7, Y3, "ESC", style="light", shape="rect",
                name="ESC (Position C16, Tabelle: INS LINE)"))
    k.append(_t("C17", 15.7, Y3, "DEL", style="light", shape="rect",
                name="DEL (Position C17, Tabelle: DEL LINE)"))
    # ── Reihe B ──────────────────────────────────────────────────────────────
    Y4 = 5.0
    k.append(_t("B99", 0.0, Y4, "↕", w=1.15, shape="oval", kind="shift",
                name="Umschalttaste links (B99, 7FH)"))
    k.append(_t("B00", 1.15, Y4, "→|", name="→| (B00, Tabulator)"))
    for i, (pos, ch) in enumerate(zip(("B01", "B02", "B03", "B04", "B05", "B06", "B07"),
                                      "ZXCVBNM")):
        k.append(_t(pos, 2.2 + i, Y4, ch))
    k.append(_t("B08", 9.2, Y4, ",", "<"))
    k.append(_t("B09", 10.2, Y4, ".", ">"))
    k.append(_t("B10", 11.2, Y4, "/", "?"))
    k.append(_t("B11", 12.3, Y4, "↕", w=1.4, shape="oval", kind="shift",
                name="Umschalttaste rechts (B11, 7FH)"))
    k.append(_t("B15", 13.7, Y4, "←|", name="←| (B15, Tabelle: ⇤)"))
    k.append(_t("B16", 14.7, Y4, "↓", name="Kursor abwärts"))
    k.append(_t("B17", 15.7, Y4, "↑", name="Kursor aufwärts"))
    # ── Reihe A ──────────────────────────────────────────────────────────────
    Y5 = 6.0
    k.append(_t("", 0.1, Y5, "CTRL", style="grau", shape="rect", kind="ctrl",
                name="CTRL (nicht in K7634.04, rastet; sendet nur den Zustand) [?]"))
    k.append(_t("A00", 1.25, Y5, "RESET", w=1.4, shape="oval"))
    k.append(_t("A05", 3.3, Y5, "", w=7.9, shape="oval", name="Leertaste", zeichen=" "))
    k.append(_t("A10", 11.75, Y5, "ENTER", w=1.4, shape="oval"))
    k.append(_t("A15", 13.6, Y5, "↵", ergaenzt=True, name="↵"))
    k.append(_t("A16", 14.7, Y5, "←", name="Kursor links"))
    k.append(_t("A17", 15.7, Y5, "→", name="Kursor rechts"))

    # ── Ziffernblock ─────────────────────────────────────────────────────────
    zb = (("D51", "7"), ("D52", "8"), ("D53", "9"),
          ("C51", "4"), ("C52", "5"), ("C53", "6"),
          ("B51", "1"), ("B52", "2"), ("B53", "3"),
          ("A51", "0"), ("A52", "-"), ("A53", ","))
    for i, (pos, ch) in enumerate(zb):
        k.append(_t(pos, 17.0 + i % 3, 3.0 + i // 3, ch,
                    name=f"Ziffernblock {ch}"))
    return k


class KeyboardK7634Widget(KeyboardWidget):
    """Anklickbare Nachbildung der K7634 — Schnittstelle wie die K7637."""

    WANNE = _C_WANNE
    PAD = (0.35, 0.45, 0.35, 0.35)

    def _layout(self) -> List[_Key]:
        return _build_layout_k7634()

    def _anzeigen_verankern(self):
        self._by_pos = {k.pos: k for k in self._keys if isinstance(k, _Taste) and k.pos}
        # Anzeigen: Netz-LED etc. sind hier ohne Befund — nur die drei am Foto sichtbaren.
        self._led_tasten = [self._by_pos[p] for p in ("D00", "C00")]

    def _led_spots(self, unit: float, ox: float, oy: float):
        """Die kleinen LED-Fassungen links von OFF und LOCK und am Stift E15 (Foto)."""
        spots = []
        # Links von OFF: Bedeutung ohne Befund [?]; links von LOCK: C99 (Umschaltfeststeller),
        # die die Tastatur selbst schaltet (k7634.md §3.3) — hier = Feststeller der Nachbildung.
        for key, name, an in zip(self._led_tasten,
                                 ("Anzeige links von OFF [?]", "Umschaltfeststeller (C99)"),
                                 (False, self.lock_active())):
            r = self._rect_of(key, unit, ox, oy)
            spots.append((r.x() - 0.1 * unit, r.center().y(), an, name))
        e13 = self._by_pos["E13"]
        r = self._rect_of(e13, unit, ox, oy)
        spots.append((r.right() + 0.2 * unit, r.center().y(), bool(self._leds & 0x10),
                      "INS-MODE (E15)"))
        return spots

    def _cap_colors(self, key: _Key):
        if key.style == "creme":
            return None, _C_CREME, _C_TXT
        if key.style == "grau":
            return None, _C_GRAU, _C_TXT
        return super()._cap_colors(key)

    def _draw_key(self, p: QPainter, key: _Key, unit: float, ox: float, oy: float):
        super()._draw_key(p, key, unit, ox, oy)
        if getattr(key, "ergaenzt", False):
            # Die Kappe gibt es am Gerät nicht: gestrichelter Rahmen = ergänzt [?].
            z = self._rect_of(key, unit, ox, oy).adjusted(0.1 * unit, 0.1 * unit,
                                                          -0.1 * unit, -0.1 * unit)
            pen = QPen(_C_ERGAENZT, max(1.5, 0.05 * unit), Qt.DashLine)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawRoundedRect(z, 0.15 * unit, 0.15 * unit)

    # ── Echte Tastatur ──────────────────────────────────────────────────────

    _HOST_POS = {
        int(Qt.Key_Return): "A10", int(Qt.Key_Enter): "A10", int(Qt.Key_Tab): "B00",
        int(Qt.Key_Backtab): "B15", int(Qt.Key_Escape): "C16", int(Qt.Key_Backspace): "E13",
        int(Qt.Key_Delete): "C17", int(Qt.Key_Up): "B17", int(Qt.Key_Down): "B16",
        int(Qt.Key_Left): "A16", int(Qt.Key_Right): "A17", int(Qt.Key_Space): "A05",
    }
    _F_POS = ("G05", "G06", "G07", "G08", "G09", "G10", "G11", "G12", "G13", "G14", "G15", "G16")

    def _keys_for_host_event(self, event) -> List[_Key]:
        key = int(event.key())
        if key == int(Qt.Key_Shift):
            return [k for k in self._keys if k.kind == "shift"]
        if key in (int(Qt.Key_Control), int(Qt.Key_Meta)):
            return [k for k in self._keys if k.kind == "ctrl"]
        if key == int(Qt.Key_CapsLock):
            return [k for k in self._keys if k.kind == "lock"]
        pos = self._HOST_POS.get(key)
        if pos is None and int(Qt.Key_F1) <= key <= int(Qt.Key_F12):
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
        if key.kind in ("shift", "ctrl", "lock") or not isinstance(key, _Taste):
            return name
        if key.matrix < 0:
            return f"{name} — sendet nichts"
        t = f"{name}: Position {key.pos}, Adresse {key.matrix:02X}H"
        if key.a:
            t += f", Code {key.a:02X}H" + (f"/{key.b:02X}H" if key.b != key.a else "")
        else:
            t += ", Tabellencode 00H (unbestückt)"
        return t
