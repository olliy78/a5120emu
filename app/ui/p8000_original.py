"""Bildschirm und Tastatur des ORIGINALTERMINALS (Typ 2 + Flachtastatur K7673.09) im P8000 Emulator.

doc/design/26_p8000emu_oberflaeche.md §8, Kern: `doc/design/28_p8000_originalterminal.md` §9.
Das Originalterminal liefert ein fertiges **Pixelbild** (640 × 312, drei Stufen: dunkel/normal/hell) —
Firmware, 8275 und Zeichengenerator haben es gezeichnet, Cursor, Blinken, Hell und Invers stecken
schon darin.  Dieses Modul speist es in dasselbe **CRT-Widget** ein, das die anderen Maschinen
benutzen (:class:`~app.ui.screen_widget.ScreenWidget`, GLSL-Shader, :class:`CRTParams`): Kontrast,
Helligkeit, Scanlines, Krümmung, Phosphorfarbe & Co. stellt der CRT-Reiter der Einstellungen ein,
Farbwahl und Zoom des Widgets selbst gibt es nicht mehr (P23a).

* **Kein Dauer-Upload.**  ``term_frame_count`` zählt die gezeigten Bilder (62,8 Hz); ändert er sich
  nicht, ist das Bild unverändert und es wird weder geholt noch hochgeladen.
* **Stufen → Textur**: dunkel/normal/hell werden auf 0, 0,72 und 1,0 der Textur abgebildet (das
  Verhältnis, mit dem das Gerät „normal“ gegen „hell“ zeichnet); den Rest macht der Shader.
* **Die Wirtstastatur geht über die Tastenmatrix** (`k7673_layout`): Zeichen werden über die
  Firmware-Tabellen `NORMAL_Tab`/`SHIFT_Tab` auf die Taste (und ggf. SHIFT) zurückgeführt, die es
  erzeugt; Sondertasten, Shift, Ctrl und Caps lock gehen als die entsprechenden Matrixtasten.
  Die Wiederholung gehaltener Tasten macht die K7673 selbst (Verzögerung/Abstand im Kern), die
  automatische Wiederholung des Wirtsrechners wird deshalb verworfen.
* **Rechtsklick ▸ „Bildschirminhalt als Text kopieren“**: der Text des Bildes laut Kern
  (``term_text``, 80 × 24, Zeilen ohne Schlussleerzeichen, ``\n``) in die Zwischenablage — nicht
  gerendert.  Kein Tastenkürzel: ^C gehört dem Gast.
* **Kürzel-Regel unverändert**: Fensterkürzel nur mit Strg+Umschalt (Ausnahme F11).
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from PySide6.QtCore import QSize, Qt, QTimer, Signal
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QMenu

from app.ui import k7673_layout as L
from app.ui.screen_widget import ScreenWidget

BILD_W, BILD_H = 640, 312

KOPIEREN_TEXT = "Bildschirminhalt als Text kopieren"

#: Terminalstufe (0 dunkel, 1 normal, 2 hell) → Textur-Byte des CRT-Shaders.
_STUFEN = bytes([0, 184, 255] + [255] * 253)

#: Verzögerung zwischen einem selbst gedrückten SHIFT und dem Zeichen (Wirtszeit, ms): die K7673
#: sendet gleichzeitig erkannte Tasten in Zeilenreihenfolge — SHIFT muss vorher schon da sein.
SHIFT_VORLAUF_MS = 150
#: Haltezeit eines Zeichens, das vor Ablauf des Vorlaufs schon losgelassen wurde.
MINDEST_HALTEN_MS = 100

class OriginalTerminalWidget(ScreenWidget):
    """Der Bildschirm des Originalterminals: CRT-Widget, das ``term_framebuffer`` zeigt und die
    Tasten annimmt."""

    #: Terminalzustand (``term_flags``) hat sich geändert (Bit 3 = Zeichensatz 2, Bit 4 = CAPS).
    flagsChanged = Signal(int)
    #: Zeile, Spalte, gedrückt? — jede Matrixtaste, die die Wirtstastatur an den Kern gibt
    #: (die Bildschirmtastatur zeigt sie mit).
    matrixGeaendert = Signal(int, int, bool)

    def __init__(self, parent=None, index: int = 0):
        super().__init__(parent)
        self.index = index
        self._frame = -1                       # zuletzt gezeigter Bildzähler
        self._flags = -1
        self._daten = b""                      # letztes Bild, Stufen 0…2 (für Tests)
        #: gehaltene Wirtstasten: Qt-Kode → [(Position, gedrückt von uns)]
        self._gehalten: Dict[int, List[Tuple[int, int]]] = {}
        self._shift_physisch = False
        self._ausstehend: Dict[int, QTimer] = {}
        self._fb_w, self._fb_h = BILD_W, BILD_H
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self._kontextmenue)

    # ── Verbindung zur Maschine ──────────────────────────────────────────────

    def set_emulator(self, emulator):
        self._alles_loslassen()
        super().set_emulator(emulator)
        self._frame, self._flags = -1, -1
        self._fb_bytes, self._daten = None, b""
        self._on_update()

    def set_powered(self, an: bool):
        if not an:
            self._alles_loslassen()
        super().set_powered(an)

    def sizeHint(self) -> QSize:
        return QSize(BILD_W + 16, BILD_H + 16)

    # ── Kontextmenü ──────────────────────────────────────────────────────────

    def kontextmenue_bauen(self) -> QMenu:
        """Das Menü der rechten Maustaste: nur noch das Kopieren (Farbe: Einstellungen ▸ CRT)."""
        menue = QMenu(self)
        a = menue.addAction(KOPIEREN_TEXT)
        a.setEnabled(self.emulator is not None)
        a.triggered.connect(lambda _c=False: self.text_kopieren())
        return menue

    def _kontextmenue(self, pos):
        self.kontextmenue_bauen().exec(self.mapToGlobal(pos))

    def text_kopieren(self) -> str:
        """Den Bildschirminhalt als reinen Text in die Zwischenablage (und zurück)."""
        text = self.bildschirmtext()
        QGuiApplication.clipboard().setText(text)
        return text

    def bildschirmtext(self) -> str:
        """Der Text des Bildes: 24 Zeilen, rechts ohne Leerzeichen, durch ``\n`` getrennt."""
        return "\n".join(z.rstrip() for z in self.text().split("\n")) if self.emulator else ""

    # ── Bild holen (der Takt des ScreenWidget ruft es) ───────────────────────

    def aktualisieren(self):
        self._on_update()

    def _on_update(self):
        """Neues Bild holen — aber nur, wenn der Bildzähler des Kerns sich bewegt hat."""
        emu = self.emulator
        if emu is None:
            return
        flags = emu.term_flags(self.index)
        if flags != self._flags:
            self._flags = flags
            self.flagsChanged.emit(max(0, flags))
        if not self._powered:
            return
        zaehler = emu.term_frame_count(self.index)
        if zaehler == self._frame and self._fb_bytes is not None:
            return
        bild = emu.term_framebuffer(self.index)
        if bild is None:
            return
        self._frame = zaehler
        b, h, daten = bild
        self._daten = daten
        if (b, h) != (self._fb_w, self._fb_h):
            self._fb_w, self._fb_h = b, h
            self._texture_stale = True
        self._fb_bytes = daten.translate(_STUFEN)
        self._pending_upload = True
        self.update()

    def pixel(self, x: int, y: int) -> int:
        """Stufe (0 dunkel, 1 normal, 2 hell) des Punktes (x, y) im letzten geholten Bild — für Tests."""
        if not self._daten:
            return 0
        return self._daten[y * self._fb_w + x]

    # ── Tastatur: Wirtstaste → Matrixtasten ──────────────────────────────────

    def focusNextPrevChild(self, weiter: bool) -> bool:
        return False                           # Tab gehört dem Gast

    def focusOutEvent(self, event):
        self._alles_loslassen()                # sonst bliebe eine Taste im Gast hängen
        super().focusOutEvent(event)

    def _matrix(self, pos, gedrueckt: bool):
        self.matrixGeaendert.emit(pos[0], pos[1], gedrueckt)
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
