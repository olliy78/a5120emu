"""Bildschirm und Tastatur des ORIGINALTERMINALS (Typ 2 + Flachtastatur K7673.09) im P8000 Emulator.

doc/design/26_p8000emu_oberflaeche.md §8, Kern: `doc/design/28_p8000_originalterminal.md` §9.
Anders als das Kern-Terminal (`app/ui/p8000_terminal.py`: Zellen, die die Oberfläche selbst
zeichnet) liefert das Originalterminal ein fertiges **Pixelbild** — Firmware, 8275 und
Zeichengenerator haben es gezeichnet, Cursor, Blinken, Hell und Invers stecken schon darin.
Dieses Modul zeigt es nur an:

* **Kein Dauer-Repaint.**  ``term_frame_count`` zählt die gezeigten Bilder (62,8 Hz); ändert er sich
  nicht, ist das Bild unverändert und es wird weder geholt noch gezeichnet.
* **Skalierung wählbar**: *ganzzahlig* (Pixel bleiben scharf, Rand bleibt dunkel) oder *glatt*
  (füllt das Widget unter Wahrung des Seitenverhältnisses).
* **Die Wirtstastatur geht über die Tastenmatrix** (`k7673_layout`): Zeichen werden über die
  Firmware-Tabellen `NORMAL_Tab`/`SHIFT_Tab` auf die Taste (und ggf. SHIFT) zurückgeführt, die es
  erzeugt; Sondertasten, Shift, Ctrl und Caps lock gehen als die entsprechenden Matrixtasten.
  Die Wiederholung gehaltener Tasten macht die K7673 selbst (Verzögerung/Abstand im Kern), die
  automatische Wiederholung des Wirtsrechners wird deshalb verworfen.
* **Kürzel-Regel unverändert**: Fensterkürzel nur mit Strg+Umschalt (Ausnahme F11).
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from PySide6.QtCore import QRect, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QAction, QColor, QGuiApplication, QImage, QPainter, QActionGroup
from PySide6.QtWidgets import QMenu, QWidget

from app.ui import k7673_layout as L
from app.ui.screen_widget import CRTParams

BILD_W, BILD_H = 640, 312

#: Verzögerung zwischen einem selbst gedrückten SHIFT und dem Zeichen (Wirtszeit, ms): die K7673
#: sendet gleichzeitig erkannte Tasten in Zeilenreihenfolge — SHIFT muss vorher schon da sein.
SHIFT_VORLAUF_MS = 150
#: Haltezeit eines Zeichens, das vor Ablauf des Vorlaufs schon losgelassen wurde.
MINDEST_HALTEN_MS = 100

SKALIERUNGEN = (("ganzzahlig", "Ganzzahlig (scharfe Pixel)"), ("glatt", "Glatt (füllt das Fenster)"))

#: Vorgaben der Zeichenfarbe: (Schlüssel, Anzeige, Phosphor an, Phosphor aus)
FARBEN = (("gruen", "Grün", (0.47, 0.83, 0.11), (0.02, 0.06, 0.02)),
          ("weiss", "Weiß", (0.88, 0.90, 0.88), (0.04, 0.04, 0.05)),
          ("bernstein", "Bernstein", (1.0, 0.69, 0.0), (0.07, 0.04, 0.0)))


class OriginalTerminalWidget(QWidget):
    """Der Bildschirm des Originalterminals: zeigt ``term_framebuffer`` und nimmt die Tasten an."""

    toggleFullscreenRequested = Signal()
    exitFullscreenRequested = Signal()
    #: Terminalzustand (``term_flags``) hat sich geändert (Bit 3 = Zeichensatz 2, Bit 4 = CAPS).
    flagsChanged = Signal(int)

    def __init__(self, params: CRTParams, index: int = 0, parent=None):
        super().__init__(parent)
        self.params = params
        self.index = index
        self.emulator = None
        self._powered = True
        self._frame = -1                       # zuletzt gezeigter Bildzähler
        self._flags = -1
        self._bild: Optional[QImage] = None    # 8 Bit indiziert, 640 × 312
        self._daten = b""
        self.skalierung = "glatt"
        #: gehaltene Wirtstasten: Qt-Kode → [(Position, gedrückt von uns)]
        self._gehalten: Dict[int, List[Tuple[int, int]]] = {}
        self._shift_physisch = False
        self._ausstehend: Dict[int, QTimer] = {}
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumSize(320, 156)
        self.setAttribute(Qt.WA_OpaquePaintEvent, True)
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self._kontextmenue)

    # ── Verbindung zur Maschine ──────────────────────────────────────────────

    def set_emulator(self, emulator):
        self._alles_loslassen()
        self.emulator = emulator
        self._frame, self._flags = -1, -1
        self._bild, self._daten = None, b""
        self.aktualisieren()

    def set_powered(self, an: bool):
        self._powered = bool(an)
        if not an:
            self._alles_loslassen()
        self.update()

    def sizeHint(self) -> QSize:
        return QSize(BILD_W + 16, BILD_H + 16)

    def heightForWidth(self, breite: int) -> int:
        return breite * BILD_H // BILD_W

    # ── Farben und Skalierung ────────────────────────────────────────────────

    def _farbtabelle(self) -> List[int]:
        """Dunkel / normal / hell als RGB aus den Phosphorfarben der Einstellungen (CRT)."""
        p = self.params

        def farbe(rgb, f):
            return QColor(*(max(0, min(255, int(c * 255 * f))) for c in rgb)).rgb()

        hell = max(0.3, min(1.0, p.brightness / 2.5)) * min(1.2, p.contrast)
        return [farbe(p.phosphor_off, 1.0), farbe(p.phosphor_on, 0.72 * hell),
                farbe(p.phosphor_on, 1.0 * hell)]

    def farben_geaendert(self):
        if self._bild is not None:
            self._bild.setColorTable(self._farbtabelle())
        self.update()

    def set_skalierung(self, art: str):
        if art in dict(SKALIERUNGEN) and art != self.skalierung:
            self.skalierung = art
            self.update()

    def set_farbe(self, schluessel: str):
        for k, _anzeige, an, aus in FARBEN:
            if k == schluessel:
                self.params.phosphor_on, self.params.phosphor_off = an, aus
                self.farben_geaendert()
                return

    def _kontextmenue(self, pos):
        menue = QMenu(self)
        gruppe = QActionGroup(menue)
        for k, anzeige in SKALIERUNGEN:
            a = menue.addAction(anzeige)
            a.setCheckable(True)
            a.setChecked(k == self.skalierung)
            gruppe.addAction(a)
            a.triggered.connect(lambda _c=False, k=k: self.set_skalierung(k))
        menue.addSeparator()
        for k, anzeige, _an, _aus in FARBEN:
            a = menue.addAction(f"Farbe: {anzeige}")
            a.triggered.connect(lambda _c=False, k=k: self.set_farbe(k))
        menue.exec(self.mapToGlobal(pos))

    # ── Bild holen und zeichnen ──────────────────────────────────────────────

    def aktualisieren(self):
        """Neues Bild holen — aber nur, wenn der Bildzähler des Kerns sich bewegt hat."""
        emu = self.emulator
        if emu is None:
            return
        flags = emu.term_flags(self.index)
        if flags != self._flags:
            self._flags = flags
            self.flagsChanged.emit(max(0, flags))
        zaehler = emu.term_frame_count(self.index)
        if zaehler == self._frame and self._bild is not None:
            return
        self._frame = zaehler
        bild = emu.term_framebuffer(self.index)
        if bild is None:
            return
        b, h, daten = bild
        self._daten = daten                    # das QImage hält nur einen Zeiger darauf
        img = QImage(self._daten, b, h, b, QImage.Format_Indexed8)
        img.setColorTable(self._farbtabelle())
        self._bild = img
        self.update()

    def bild_rechteck(self) -> QRect:
        """Das Rechteck im Widget, in das das Bild gezeichnet wird (für Tests und Maus)."""
        w, h = self.width(), self.height()
        if self.skalierung == "ganzzahlig":
            f = max(1, min(w // BILD_W, h // BILD_H))
            zw, zh = BILD_W * f, BILD_H * f
        else:
            f = min(w / BILD_W, h / BILD_H)
            zw, zh = int(BILD_W * f), int(BILD_H * f)
        return QRect((w - zw) // 2, (h - zh) // 2, zw, zh)

    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(*(int(c * 255) for c in self.params.phosphor_off)))
        if self._bild is not None and self._powered:
            ziel = self.bild_rechteck()
            p.setRenderHint(QPainter.SmoothPixmapTransform, self.skalierung == "glatt")
            p.drawImage(ziel, self._bild)

    def pixel(self, x: int, y: int) -> int:
        """Stufe (0 dunkel, 1 normal, 2 hell) des Punktes (x, y) im letzten geholten Bild — für Tests."""
        if not self._daten:
            return 0
        return self._daten[y * BILD_W + x]

    # ── Tastatur: Wirtstaste → Matrixtasten ──────────────────────────────────

    def focusNextPrevChild(self, weiter: bool) -> bool:
        return False                           # Tab gehört dem Gast

    def focusOutEvent(self, event):
        self._alles_loslassen()                # sonst bliebe eine Taste im Gast hängen
        super().focusOutEvent(event)

    def _matrix(self, pos, gedrueckt: bool):
        if self.emulator is not None:
            self.emulator.term_matrix_key(self.index, pos[0], pos[1], gedrueckt)

    def _alles_loslassen(self):
        for t in self._ausstehend.values():
            t.stop()
        self._ausstehend.clear()
        for liste in self._gehalten.values():
            for pos, _ in liste:
                self._matrix(pos, False)
        self._gehalten.clear()
        self._shift_physisch = False

    @staticmethod
    def _taste_fuer(event, ctrl: bool):
        """(Position, braucht_shift) der Wirtstaste — nach Zeichen, bei Strg nach der Taste."""
        key = event.key()
        sonder = L.sondertaste(key)
        if sonder is not None:
            return sonder, None                # Sondertaste: Shift-Zustand bleibt, wie er ist
        text = event.text()
        if ctrl and Qt.Key_A <= key <= Qt.Key_Z:
            text = chr(key).lower()            # Strg+Buchstabe: der Buchstabe, nicht das Steuerzeichen
        if not text or len(text) != 1:
            return None
        z = L.zeichen_taste(text)
        return (z[0], z[1]) if z else None

    def keyPressEvent(self, event):
        key = event.key()
        if key == Qt.Key_F11:
            self.toggleFullscreenRequested.emit()
            event.accept()
            return
        if key == Qt.Key_Escape and event.modifiers() == Qt.NoModifier:
            self.exitFullscreenRequested.emit()          # und das Terminal bekommt ESC trotzdem
        if self.emulator is None:
            return super().keyPressEvent(event)
        if event.isAutoRepeat():
            event.accept()                               # die K7673 wiederholt selbst
            return
        if key == Qt.Key_Insert and event.modifiers() & Qt.ShiftModifier:
            self.einfuegen(QGuiApplication.clipboard().text())
            event.accept()
            return
        ctrl = bool(event.modifiers() & Qt.ControlModifier) or (
            L.SCANCODE_POS[0x38] in [p for ps in self._gehalten.values() for p, _ in ps])
        taste = self._taste_fuer(event, ctrl)
        if taste is None:
            return super().keyPressEvent(event)
        pos, braucht_shift = taste
        if key == Qt.Key_Shift:
            self._shift_physisch = True
        # Shift-Zustand dem Zeichen anpassen: fehlt er, wird er für die Dauer des Tastendrucks
        # gedrückt; steht er fälschlich (Wirts-Shift, Zeichen ohne), für die Dauer gelöst.
        umschalten = None
        if braucht_shift is not None:
            if braucht_shift and not self._shift_physisch:
                umschalten = True
            elif not braucht_shift and self._shift_physisch:
                umschalten = False
        shift_pos = L.SCANCODE_POS[0x2A]
        if umschalten is None:
            self._matrix(pos, True)
            self._gehalten[key] = [(pos, 1)]
        else:
            self._matrix(shift_pos, umschalten)
            self._gehalten[key] = [(shift_pos, 0 if umschalten else 2)]    # 0: von uns gedrückt, 2: von uns gelöst
            t = QTimer(self)
            t.setSingleShot(True)
            t.timeout.connect(lambda k=key, p=pos: self._verzoegert(k, p))
            self._ausstehend[key] = t
            t.start(SHIFT_VORLAUF_MS)
        event.accept()

    def _verzoegert(self, key: int, pos):
        self._ausstehend.pop(key, None)
        if key in self._gehalten:
            self._matrix(pos, True)
            self._gehalten[key].insert(0, (pos, 1))

    def keyReleaseEvent(self, event):
        key = event.key()
        if key == Qt.Key_F11:
            event.accept()
            return
        if event.isAutoRepeat() or key not in self._gehalten:
            return super().keyReleaseEvent(event)
        if key == Qt.Key_Shift:
            self._shift_physisch = False
        taste = self._ausstehend.pop(key, None)
        if taste is not None:                  # losgelassen, bevor das Zeichen gedrückt war
            taste.stop()
            liste = self._gehalten[key]
            # Das Zeichen trotzdem tippen: kurz gedrückt, dann beides loslassen.
            pos = self._pos_zu_taste(event)
            if pos is not None:
                self._matrix(pos, True)
                liste.insert(0, (pos, 1))
            QTimer.singleShot(MINDEST_HALTEN_MS, lambda k=key: self._loslassen(k))
            event.accept()
            return
        self._loslassen(key)
        event.accept()

    def _pos_zu_taste(self, event):
        taste = self._taste_fuer(event, bool(event.modifiers() & Qt.ControlModifier))
        return taste[0] if taste else None

    def _loslassen(self, key: int):
        for pos, art in self._gehalten.pop(key, []):
            if art == 1:
                self._matrix(pos, False)
            elif art == 0:                     # unser SHIFT, wieder lösen
                if not self._shift_physisch:
                    self._matrix(pos, False)
            elif art == 2:                     # Wirts-Shift, den wir gelöst hatten: wieder drücken
                if self._shift_physisch:
                    self._matrix(pos, True)

    def mouseReleaseEvent(self, event):
        # Mittlere Taste fügt die Auswahl ein (wie im Terminalfenster unter X11).
        if event.button() == Qt.MiddleButton and self.emulator is not None:
            cb = QGuiApplication.clipboard()
            from PySide6.QtGui import QClipboard
            text = cb.text(QClipboard.Selection) if cb.supportsSelection() else cb.text()
            self.einfuegen(text)
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def einfuegen(self, text: str):
        """Text wie getippt zum Gast schicken (über die K7673: mit ihrem Tempo, ≈ 6 Zeichen/s)."""
        if self.emulator is None or not text:
            return
        ascii_text = "".join(c for c in text.replace("\r\n", "\n") if c == "\n" or 0x20 <= ord(c) < 0x7F)
        if ascii_text:
            self.emulator.term_send(self.index, ascii_text)

    def text(self) -> str:
        """Der Text des Bildes laut Kern (``term_text``) — am Originalterminal aus dem BWS."""
        return self.emulator.term_text(self.index) if self.emulator is not None else ""
