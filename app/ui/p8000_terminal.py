"""Terminal-Bildschirm und Funktionstastenleiste des P8000 Emulators.

doc/design/26_p8000emu_oberflaeche.md §3.  Der Kern führt je Kern-Terminal eine Textschicht
(80 × 24 Zellen mit Attributen, ADM31/VT100-Parser, `core/peripherals/p8000_terminal/`) — die
Oberfläche zeichnet sie nur: ``k1520_term_snapshot`` liefert das ganze Bild in EINEM Aufruf,
gezeichnet wird mit dem Zeichengenerator des echten Terminals.

Drei Festlegungen, die man nicht aufweichen darf:

* **Der Zeichensatz kommt aus den EPROM-Abzügen, nicht aus einer Schrift.**  `P8TEZS` (ZG1,
  ASCII) und `P8TDZS` (ZG2, deutsch: § Ä Ö Ü ä ö ü ß auf @ [ \\ ] { | } ~) liegen als
  Bitmuster 8 × 12 in `app/ui/p8000_zeichensatz.py` (erzeugt von
  `tools/p8000/zeichensatz_zu_py.py`).  Eine Monospace-Schrift müsste die Umlaute auf die
  ASCII-Stellen legen UND die Programm-Mode-Zeichen 00H–1FH erfinden; die Abzüge haben beides
  und sind pixelgleich zum Gerät.  Jede Zelle trägt ihr eigenes Flag „mit ZG2 geschrieben"
  (SI/SO schaltet nur, was danach geschrieben wird).
* **Gezeichnet wird in einen 640 × 288-Puffer, nur geänderte Zellen**, der Puffer skaliert aufs
  Widget.  Neuzeichnen aller 1920 Zellen je Bild wäre in Python spürbar; ein Bild ändert fast
  nie mehr als eine Zeile.
* **Tasten gehen als Qt-Kodes bzw. 0x02000000 + TerminalTaste an den Kern** (siehe
  `P8000Machine::keyPress`) — dort gibt es keinen anderen Weg an die Funktionstasten.  Kürzel
  des Fensters gibt es nur mit Strg+Umschalt (`app/ui/actions.py`); alles andere gehört dem
  Gast, auch Strg+C und die F-Tasten (F11 = Vollbild ist die einzige Ausnahme).
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from PySide6.QtCore import QPointF, QRect, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QGuiApplication, QImage, QPainter, QPen, QClipboard
from PySide6.QtWidgets import (QGridLayout, QHBoxLayout, QLabel, QPushButton, QTabWidget,
                               QVBoxLayout, QWidget)

from app.ui.p8000_zeichensatz import ZG1, ZG2
from app.ui.screen_widget import CRTParams

COLS, ROWS = 80, 24
CELL_W, CELL_H = 8, 12
BILD_W, BILD_H = COLS * CELL_W, ROWS * CELL_H          # 640 × 288
_ZELLE = 3                                             # Byte je Zelle im Schnappschuss

# Attributbits (term_attr, core/peripherals/p8000_terminal/terminal.h)
ATTR_BLINK, ATTR_INVERS, ATTR_LEER, ATTR_HELL, ATTR_UNTERSTRICH = 1, 2, 4, 8, 16
# Flags des Schnappschusses
FLAG_ZG2, FLAG_FELD = 1, 2
# term_flags
TF_ONLINE, TF_VIDEO, TF_PROGRAMM, TF_ZG2, TF_CAPS = 1, 2, 4, 8, 16

# Tastenkodes zum Kern (P8000Machine::keyPress)
QT_RETURN, QT_ESCAPE, QT_BACKSPACE, QT_TAB, QT_BACKTAB, QT_DELETE = (
    0x01000004, 0x01000000, 0x01000003, 0x01000001, 0x01000002, 0x01000007)
PRIVAT = 0x02000000
CAPS_AN, CAPS_AUS = 0x02000100, 0x02000101

#: TerminalTaste in der Reihenfolge des Kerns (`enum class TerminalTaste`) — Index = Kodeoffset.
TASTEN = ("VT", "LF", "FF", "BS", "HOME", "HT", "NL", "CR", "ESC", "DEL",
          "LINE_ERASE", "PAGE_ERASE", "LINE_INSERT", "CHAR_INSERT", "LINE_DELETE", "CHAR_DELETE",
          "TAB", "BACKTAB", "BREAK", "MODE", "VIDEO", "ON_OFF", "SI_SO")


def taste_kode(name: str) -> int:
    """Kern-Kode einer Terminaltaste ohne Qt-Gegenstück (``0x02000000 + TerminalTaste``)."""
    return PRIVAT + TASTEN.index(name)


#: Host-Taste → Kern-Kode für alle Tasten, die keinen Text liefern.
_SONDERTASTEN: Dict[int, int] = {
    Qt.Key_Return: QT_RETURN, Qt.Key_Enter: QT_RETURN,
    Qt.Key_Escape: QT_ESCAPE, Qt.Key_Backspace: QT_BACKSPACE, Qt.Key_Tab: QT_TAB,
    Qt.Key_Backtab: QT_BACKTAB, Qt.Key_Delete: QT_DELETE,
    Qt.Key_Left: 0x01000012, Qt.Key_Up: 0x01000013,       # <BS> <VT>
    Qt.Key_Right: 0x01000014, Qt.Key_Down: 0x01000015,    # <FF> <LF>
    Qt.Key_Home: 0x01000010,
    Qt.Key_F1: taste_kode("LINE_ERASE"), Qt.Key_F2: taste_kode("PAGE_ERASE"),
    Qt.Key_F3: taste_kode("LINE_INSERT"), Qt.Key_F4: taste_kode("CHAR_INSERT"),
    Qt.Key_F5: taste_kode("LINE_DELETE"), Qt.Key_F6: taste_kode("CHAR_DELETE"),
    Qt.Key_F7: taste_kode("BREAK"), Qt.Key_F8: taste_kode("SI_SO"),
    Qt.Key_Pause: taste_kode("BREAK"), Qt.Key_Insert: taste_kode("CHAR_INSERT"),
}

#: Deutsche Tasten → Zeichen des deutschen Satzes ZG2 (ein deutsches Terminal sendet für „ä" das
#: Byte 7BH; angezeigt wird es mit ZG1 als „{" — wie am Gerät).
_UMLAUTE = {"ä": 0x7B, "ö": 0x7C, "ü": 0x7D, "ß": 0x7E, "Ä": 0x5B, "Ö": 0x5C, "Ü": 0x5D}


def host_taste(key: int, text: str, modifiers) -> Optional[Tuple[int, bool, bool]]:
    """Host-Taste → ``(Kern-Kode, shift, ctrl)`` oder ``None`` (Taste gehört dem Terminal nicht).

    Eigene Funktion statt Methode, damit die Abbildung ohne Widget prüfbar ist.  Umschalt/Strg/
    Alt/Feststeller allein ergeben nichts; Feststeller behandelt das Widget selbst (Rasttaste
    mit Zustand im Terminal).
    """
    ctrl = bool(modifiers & Qt.ControlModifier)
    shift = bool(modifiers & Qt.ShiftModifier)
    if key in _SONDERTASTEN:
        # Shift+Tab liefert Qt schon als Key_Backtab; Shift+Pfeil ist wie der Pfeil.
        return _SONDERTASTEN[key], shift, ctrl
    # Strg + Buchstabe: der Kern bildet das Steuerzeichen selbst (Tab. 4.3-5).
    if ctrl and Qt.Key_A <= key <= Qt.Key_Z:
        return key - Qt.Key_A + ord("A"), shift, True
    if not text:
        return None
    c = text[0]
    if c in _UMLAUTE:
        return _UMLAUTE[c], shift, False
    o = ord(c)
    if 0x20 <= o < 0x7F:
        return o, shift, ctrl
    if 0 < o < 0x20 and ctrl:               # Strg+[ \ ] ^ _ → ESC FS GS RS US
        return o, shift, False
    return None


class TerminalWidget(QWidget):
    """Eine Bildröhre des Terminals: zeichnet ``k1520_term_snapshot`` des Kern-Terminals ``index``."""

    toggleFullscreenRequested = Signal()
    exitFullscreenRequested = Signal()
    #: Terminalzustand (``term_flags``) hat sich geändert.
    flagsChanged = Signal(int)

    def __init__(self, params: CRTParams, index: int = 0, parent=None):
        super().__init__(parent)
        self.params = params
        self.index = index
        self.emulator = None
        self._powered = True
        self._snap = b""
        self._cursor: Tuple[int, int] = (0, 0)
        self._flags = -1
        self._phase = True                      # Blinkphase: an
        self._blinker: set = set()              # Zellen (z, s) mit Blinken
        self._puffer = QImage(BILD_W, BILD_H, QImage.Format_RGB32)
        self._glyphen: Dict[tuple, QImage] = {}
        self._alles_neu = True
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumSize(320, 144)
        self.setAttribute(Qt.WA_OpaquePaintEvent, True)

    # ── Verbindung zur Maschine ──────────────────────────────────────────────

    def set_emulator(self, emulator):
        self.emulator = emulator
        self._snap, self._flags = b"", -1
        self._alles_neu = True
        self.aktualisieren()

    def set_powered(self, an: bool):
        self._powered = bool(an)
        self._alles_neu = True
        self.update()

    def sizeHint(self) -> QSize:
        return QSize(BILD_W + 16, BILD_H + 16)

    def heightForWidth(self, breite: int) -> int:
        return breite * BILD_H // BILD_W

    # ── Farben ───────────────────────────────────────────────────────────────

    def _farben(self) -> Tuple[QColor, QColor, QColor]:
        """(Hintergrund, normal, hell) aus den Phosphorfarben und der Helligkeit.

        Normal ist gedämpft, hell (ATTR_HELL = Halbhell-Gegenstück am Gerät) voll — der Abstand
        ist, was man am Schirm sieht; Helligkeit und Kontrast der Bildröhre wirken als Faktor.
        """
        p = self.params

        def farbe(rgb, f):
            return QColor(*(max(0, min(255, int(c * 255 * f))) for c in rgb))

        hell = max(0.3, min(1.0, p.brightness / 2.5)) * min(1.2, p.contrast)
        return farbe(p.phosphor_off, 1.0), farbe(p.phosphor_on, 0.72 * hell), \
            farbe(p.phosphor_on, 1.0 * hell)

    def farben_geaendert(self):
        """Nach einer Änderung der Röhrenwerte: alles mit den neuen Farben zeichnen."""
        self._glyphen.clear()
        self._alles_neu = True
        self.update()

    # ── Zeichnen in den Puffer ───────────────────────────────────────────────

    def _glyphe(self, zg2: bool, zeichen: int, fg: QColor, bg: QColor) -> QImage:
        """Glyphenbild 8 × 12 (zwischengespeichert je Satz/Zeichen/Farbpaar)."""
        schluessel = (zg2, zeichen, fg.rgb(), bg.rgb())
        bild = self._glyphen.get(schluessel)
        if bild is None:
            bild = QImage(CELL_W, CELL_H, QImage.Format_RGB32)
            bild.fill(bg)
            satz = ZG2 if zg2 else ZG1
            muster = satz[(zeichen & 0x7F) * 16:(zeichen & 0x7F) * 16 + CELL_H]
            rgb = fg.rgb()
            for zeile, bits in enumerate(muster):
                for spalte in range(CELL_W):
                    if bits & (0x80 >> spalte):
                        bild.setPixel(spalte, zeile, rgb)
            self._glyphen[schluessel] = bild
        return bild

    def _zeichne_zelle(self, z: int, s: int, cursor: bool):
        """Zelle (z, s) aus dem Schnappschuss in den Puffer."""
        o = (z * COLS + s) * _ZELLE
        zeichen, attr, flags = self._snap[o], self._snap[o + 1], self._snap[o + 2]
        bg, normal, hell = self._farben()
        fg = hell if attr & ATTR_HELL else normal
        if attr & ATTR_INVERS:
            fg, bg = bg, fg
        if attr & ATTR_LEER:
            fg = bg
        if attr & ATTR_BLINK:
            self._blinker.add((z, s))
            if not self._phase:
                fg = bg                          # Blinken: in der Aus-Phase unsichtbar
        else:
            self._blinker.discard((z, s))
        if cursor:
            fg, bg = bg, fg                      # Blockcursor = Zelle invertiert
        p = QPainter(self._puffer)
        p.drawImage(s * CELL_W, z * CELL_H,
                    self._glyphe(bool(flags & FLAG_ZG2), zeichen, fg, bg))
        if attr & ATTR_UNTERSTRICH:
            p.setPen(QPen(fg, 1))
            p.drawLine(s * CELL_W, z * CELL_H + CELL_H - 1,
                       s * CELL_W + CELL_W - 1, z * CELL_H + CELL_H - 1)
        p.end()

    def _cursor_sichtbar(self) -> bool:
        return self._powered and self._phase and 0 <= self._cursor[0] < COLS \
            and 0 <= self._cursor[1] < ROWS

    def aktualisieren(self):
        """Ein Bild nachführen: Schnappschuss holen, geänderte Zellen zeichnen (50-Hz-Takt)."""
        emu = self.emulator
        if emu is None or not self._powered:
            return
        snap = emu.term_snapshot(self.index)
        if not snap:
            return
        cur = emu.term_cursor(self.index) or (0, 0)
        flags = emu.term_flags(self.index)
        if flags != self._flags:
            self._flags = flags
            self.flagsChanged.emit(flags)
        if not self._alles_neu and snap == self._snap and cur == self._cursor:
            return
        alt, alt_cursor = self._snap, self._cursor
        self._snap, self._cursor = snap, cur
        zeilen = range(ROWS)
        zeile_bytes = COLS * _ZELLE
        for z in zeilen:
            a = z * zeile_bytes
            if (not self._alles_neu and len(alt) == len(snap)
                    and snap[a:a + zeile_bytes] == alt[a:a + zeile_bytes]):
                continue
            for s in range(COLS):
                self._zeichne_zelle(z, s, self._cursor_sichtbar() and (z, s) == (cur[1], cur[0]))
        # Cursor bewegt: alte Stelle ohne, neue mit Block (nur wo die Zeile nicht ohnehin neu war)
        if not self._alles_neu:
            for (c, r), mit in ((alt_cursor, False), (cur, self._cursor_sichtbar())):
                if 0 <= c < COLS and 0 <= r < ROWS:
                    self._zeichne_zelle(r, c, mit and (r, c) == (cur[1], cur[0]))
        self._alles_neu = False
        self.update()

    def blinken(self):
        """Blinkphase umschalten (Takt des Aufrufers, ≈ 2 Hz): Blinkzellen und Cursor neu zeichnen."""
        self._phase = not self._phase
        if not self._snap or not self._powered:
            return
        c, r = self._cursor
        for z, s in list(self._blinker):
            self._zeichne_zelle(z, s, (z, s) == (r, c) and self._cursor_sichtbar())
        if 0 <= c < COLS and 0 <= r < ROWS:
            self._zeichne_zelle(r, c, self._cursor_sichtbar())
        self.update()

    # ── Anzeige ──────────────────────────────────────────────────────────────

    def bild_rechteck(self) -> QRect:
        """Das Rechteck im Widget, in das der Puffer unter Wahrung des Seitenverhältnisses passt."""
        w, h = self.width(), self.height()
        if w * BILD_H > h * BILD_W:
            bw, bh = h * BILD_W // BILD_H, h
        else:
            bw, bh = w, w * BILD_H // BILD_W
        return QRect((w - bw) // 2, (h - bh) // 2, bw, bh)

    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), self._farben()[0])        # der Rand ist Teil der Röhre
        if self._powered:
            p.setRenderHint(QPainter.SmoothPixmapTransform, True)
            p.drawImage(self.bild_rechteck(), self._puffer)
        p.end()

    def text(self) -> str:
        """Der Bildschirminhalt als Text (24 Zeilen) — für Tests und Diagnose."""
        if self.emulator is None:
            return ""
        return self.emulator.term_text(self.index)

    # ── Tastatur ─────────────────────────────────────────────────────────────

    def focusNextPrevChild(self, weiter: bool) -> bool:
        return False          # Tab gehört dem Gast (Tabulator, Shift+Tab = BACKTAB)

    def focusInEvent(self, event):
        super().focusInEvent(event)

    def keyPressEvent(self, event):
        key = event.key()
        if key == Qt.Key_F11:
            self.toggleFullscreenRequested.emit()
            event.accept()
            return
        if key == Qt.Key_Escape and event.modifiers() == Qt.NoModifier:
            self.exitFullscreenRequested.emit()          # und der Gast bekommt ESC trotzdem
        if self.emulator is None:
            return super().keyPressEvent(event)
        if key == Qt.Key_CapsLock:
            an = not (self.emulator.term_flags(self.index) & TF_CAPS)
            self.emulator.term_key(self.index, CAPS_AN if an else CAPS_AUS, False, False)
            event.accept()
            return
        if key == Qt.Key_Insert and event.modifiers() & Qt.ShiftModifier:
            self.einfuegen(QGuiApplication.clipboard().text())
            event.accept()
            return
        abbildung = host_taste(key, event.text(), event.modifiers())
        if abbildung is None:
            return super().keyPressEvent(event)
        self.emulator.term_key(self.index, *abbildung)
        event.accept()

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key_F11:
            event.accept()
            return
        super().keyReleaseEvent(event)

    def mouseReleaseEvent(self, event):
        # Mittlere Taste fügt die Auswahl ein (wie im Terminalfenster unter X11).
        if event.button() == Qt.MiddleButton and self.emulator is not None:
            cb = QGuiApplication.clipboard()
            text = cb.text(QClipboard.Selection) if cb.supportsSelection() else cb.text()
            self.einfuegen(text)
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def einfuegen(self, text: str):
        """Text wie getippt zum Gast schicken (Zeilenenden = Return, Nicht-ASCII entfällt)."""
        if self.emulator is None or not text:
            return
        ascii_text = "".join(c for c in text.replace("\r\n", "\n") if c == "\n" or 0x20 <= ord(c) < 0x7F)
        if ascii_text:
            self.emulator.term_send(self.index, ascii_text)


class TerminalTabs(QWidget):
    """Der „Bildschirm" des P8000 Emulators: je Kern-Terminal ein Reiter.

    Spielt die Rolle des ``ScreenWidget`` im Hauptfenster (gleiche Schnittstelle:
    ``set_emulator``/``start_display``/``stop_display``/``set_powered``/``params``/
    ``key_sink``/Vollbild-Signale) und zeigt so viele Reiter, wie der Kern Terminals führt
    (``term_count()``) — heute eines (tty1 = Konsole), ab dem Kernausbau tty4–tty7 weitere.
    Darunter steht die Zeile mit Betriebsart und Zustand des Terminals.
    """

    toggleFullscreenRequested = Signal()
    exitFullscreenRequested = Signal()
    #: ``term_flags`` des aktuellen Terminals (für die Funktionstastenleiste).
    flagsChanged = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.emulator = None
        self.params = CRTParams()
        self.key_sink = None
        self._powered = True
        #: Skalierung des Originalterminals („ganzzahlig“/„glatt“), an neue Reiter weitergegeben.
        self.skalierung = "glatt"
        self._terminals: List[TerminalWidget] = []

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self.tabs = QTabWidget(self)
        self.tabs.setDocumentMode(True)
        self.tabs.setFocusPolicy(Qt.NoFocus)
        self.tabs.currentChanged.connect(self._reiter_gewechselt)
        lay.addWidget(self.tabs, 1)
        self.info = QLabel(self)
        self.info.setMargin(3)
        self.info.setFocusPolicy(Qt.NoFocus)
        lay.addWidget(self.info)

        self.timer = QTimer(self)
        self.timer.setInterval(40)               # 25 Hz — das Terminal ändert sich seltener
        self.timer.timeout.connect(self._takt)
        self._blink = QTimer(self)
        self._blink.setInterval(500)
        self._blink.timeout.connect(self._blinken)
        self.setMinimumSize(320, 200)

    # ── Schnittstelle des ScreenWidget ───────────────────────────────────────

    def set_emulator(self, emulator):
        """An die (neue) Maschine hängen; die Reiter folgen ``term_count()``."""
        self.emulator = emulator
        n = emulator.term_count() if emulator is not None else 0
        aktuell = self.tabs.currentIndex()
        while self.tabs.count():
            w = self.tabs.widget(0)
            self.tabs.removeTab(0)
            w.deleteLater()
        self._terminals = []
        for i in range(n):
            original = emulator.term_kind(i) == 1
            if original:
                from app.ui.p8000_original import OriginalTerminalWidget
                t = OriginalTerminalWidget(self.params, i)
                t.set_skalierung(self.skalierung)
            else:
                t = TerminalWidget(self.params, i)
            t.toggleFullscreenRequested.connect(self.toggleFullscreenRequested)
            t.exitFullscreenRequested.connect(self.exitFullscreenRequested)
            t.flagsChanged.connect(lambda f, ti=i: self._flags(ti, f))
            self._terminals.append(t)            # vor set_emulator: das meldet schon Flags
            t.set_emulator(emulator)
            t.set_powered(self._powered)
            tty = emulator.term_tty(i)
            if tty < 0:                          # eigenständiges Terminal: keine Rechner-Schnittstelle
                name = "P8000 Terminal"
            else:
                name = f"tty{tty} (Konsole)" if tty == 1 else f"tty{tty}"
            self.tabs.addTab(t, name)
        # Ein einziger Reiter braucht keine Leiste.
        self.tabs.tabBar().setVisible(n > 1)
        if 0 <= aktuell < n:
            self.tabs.setCurrentIndex(aktuell)
        self._reiter_gewechselt(self.tabs.currentIndex())
        self._info_zeigen()

    def start_display(self):
        self.timer.start()
        self._blink.start()

    def stop_display(self):
        self.timer.stop()
        self._blink.stop()

    def set_powered(self, an: bool):
        self._powered = bool(an)
        for t in self._terminals:
            t.set_powered(an)
        if an:
            self.start_display()
        else:
            self.stop_display()
        self._info_zeigen()

    def update(self):
        """Nach einer Änderung der Röhrenwerte (Einstellungen ▸ CRT): neu zeichnen."""
        for t in self._terminals:
            t.farben_geaendert()
        super().update()

    # ── Terminals ────────────────────────────────────────────────────────────

    def terminals(self) -> List[TerminalWidget]:
        return list(self._terminals)

    def aktuell(self) -> Optional[TerminalWidget]:
        return self._terminals[self.tabs.currentIndex()] if self._terminals else None

    def _reiter_gewechselt(self, i: int):
        t = self.aktuell()
        self.setFocusProxy(t)
        if t is not None and self.isVisible():
            t.setFocus(Qt.OtherFocusReason)
        self._info_zeigen()
        if t is not None:
            self.flagsChanged.emit(max(0, t._flags))

    def _takt(self):
        t = self.aktuell()
        if t is not None:                        # nur der sichtbare Reiter kostet Zeit
            t.aktualisieren()

    def _blinken(self):
        t = self.aktuell()
        if t is not None and hasattr(t, "blinken"):      # das Originalbild blinkt selbst (Firmware)
            t.blinken()

    def set_skalierung(self, art: str):
        """Skalierung des Originalterminal-Bildes („ganzzahlig“/„glatt“) für alle Reiter."""
        self.skalierung = art
        for t in self._terminals:
            if hasattr(t, "set_skalierung"):
                t.set_skalierung(art)

    def _flags(self, index: int, flags: int):
        if self.aktuell() is self._terminals[index]:
            self._info_zeigen()
            self.flagsChanged.emit(flags)

    def sende_taste(self, kode: int, shift: bool = False, ctrl: bool = False):
        """Eine Taste der Funktionstastenleiste an das Terminal des aktuellen Reiters."""
        t = self.aktuell()
        if self.emulator is not None and t is not None:
            self.emulator.term_key(t.index, kode, shift, ctrl)

    def modus_text(self) -> str:
        """Betriebsart und Zustand des aktuellen Terminals, wie die Zeile unter dem Bild."""
        t = self.aktuell()
        if self.emulator is None or t is None:
            return "kein Terminal"
        if not self._powered:
            return "ausgeschaltet"
        f = max(0, self.emulator.term_flags(t.index))
        if self.emulator.term_kind(t.index) == 1:
            # Originalterminal: die Betriebsart (ADM31/VT100) steckt in der Firmware, nicht im Kern.
            teile = ["Originalterminal Typ 2",
                     "Zeichensatz 2 (deutsch)" if f & TF_ZG2 else "Zeichensatz 1 (ASCII)"]
            if f & TF_CAPS:
                teile.append("Caps lock")
            return " · ".join(teile)
        modus = "VT100" if self.emulator.term_mode(t.index) == 1 else "ADM31"
        teile = [modus, "Zeichensatz 2 (deutsch)" if f & TF_ZG2 else "Zeichensatz 1 (ASCII)",
                 "On-Line" if f & TF_ONLINE else "Off-Line"]
        if not f & TF_VIDEO:
            teile.append("Video-Attribute aus")
        if f & TF_PROGRAMM:
            teile.append("Programm-Mode")
        if f & TF_CAPS:
            teile.append("Caps lock")
        return " · ".join(teile)

    def _info_zeigen(self):
        text = self.modus_text()
        if text != self.info.text():
            self.info.setText(text)
            self.info.setToolTip("Betriebsart des Terminals: ADM31 oder VT100 stellt der Gast mit "
                                 "seinen Steuerfolgen ein (WEGA: ttytype); Zeichensatz und Caps lock "
                                 "über die Tasten SI/SO und Caps lock.")


class KeyboardP8000Widget(QWidget):
    """Funktionstastenleiste des P8000-Terminals — die Tasten, die es auf dem PC nicht gibt.

    Die Zeichen- und Cursortasten kommen von der Tastatur des Wirtsrechners (Fokus im
    Terminal); hier liegen <LINE ERASE> … <CHAR DELETE>, <TAB>/<BACKTAB>, <BREAK>, die
    Cursortasten und die Terminalschalter <SI/SO>, <Caps lock>, <MODE>, <VIDEO>, <ON/OFF>
    (Tab. 4.3-6/-7).  Dieselbe Schnittstelle wie die Bildschirmtastaturen der anderen Maschinen
    (``keyPressed``/``keyReleased``/``set_powered``/``set_leds``), damit das Hauptfenster sie
    wie jede andere einhängt.  Die Knöpfe nehmen keinen Fokus: der gehört dem Terminal.
    """

    keyPressed = Signal(int, bool, bool)
    keyReleased = Signal(int)

    #: (Beschriftung, TerminalTaste, Tooltip) je Zeile.
    ZEILEN = (
        (("LINE ERASE", "LINE_ERASE", "Zeile ab Cursor löschen (F1)"),
         ("PAGE ERASE", "PAGE_ERASE", "Schirm ab Cursor löschen (F2)"),
         ("LINE INSERT", "LINE_INSERT", "Zeile einfügen (F3)"),
         ("CHAR INSERT", "CHAR_INSERT", "Zeichen einfügen (F4, Einfg)"),
         ("LINE DELETE", "LINE_DELETE", "Zeile löschen (F5)"),
         ("CHAR DELETE", "CHAR_DELETE", "Zeichen löschen (F6)")),
        (("◀", "BS", "Cursor links (<BS>)"), ("▲", "VT", "Cursor hoch (<VT>)"),
         ("▼", "LF", "Cursor runter (<LF>)"), ("▶", "FF", "Cursor rechts (<FF>)"),
         ("HOME", "HOME", "Cursor auf den Bildanfang (Pos1)"),
         ("TAB", "TAB", "Tabulator"), ("BACKTAB", "BACKTAB", "Rückwärts-Tabulator (Umschalt+Tab)")),
        (("BREAK", "BREAK", "BREAK-Signal (F7, Pause)"),
         ("MODE", "MODE", "Terminal neu initialisieren (löscht das Bild!)"),
         ("VIDEO", "VIDEO", "Video-Attribute ein/aus (initialisiert neu)"),
         ("ON/OFF", "ON_OFF", "On-Line/Off-Line: Off-Line zeigt Tasten nur lokal an")),
    )

    def __init__(self, parent=None):
        super().__init__(parent)
        self._tasten: Dict[str, QPushButton] = {}
        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(3)
        for zeile in self.ZEILEN:
            h = QHBoxLayout()
            h.setSpacing(3)
            for text, name, tipp in zeile:
                knopf = self._knopf(text, tipp)
                knopf.clicked.connect(lambda _c=False, n=name: self._druck(taste_kode(n)))
                self._tasten[name] = knopf
                h.addWidget(knopf)
            lay.addLayout(h)
        # Rasttasten: der Zustand liegt im Terminal und kommt über zeige_flags zurück.
        h = QHBoxLayout()
        h.setSpacing(3)
        self.si_so = self._knopf("SI/SO", "Zeichensatz 1 ⇄ 2 (deutsch) — wirkt auf das, was danach "
                                          "geschrieben wird (F8)", rast=True)
        self.si_so.clicked.connect(lambda _c=False: self._druck(taste_kode("SI_SO")))
        self.caps = self._knopf("CAPS LOCK", "Feststeller: Großbuchstaben (nur Buchstaben)",
                                rast=True)
        self.caps.clicked.connect(
            lambda _c=False: self._druck(CAPS_AUS if not self.caps.isChecked() else CAPS_AN))
        h.addWidget(self.si_so)
        h.addWidget(self.caps)
        h.addStretch(1)
        lay.addLayout(h)
        self._rasten_stumm = False

    @staticmethod
    def _knopf(text: str, tipp: str, rast: bool = False) -> QPushButton:
        k = QPushButton(text)
        k.setToolTip(tipp)
        k.setCheckable(rast)
        k.setFocusPolicy(Qt.NoFocus)
        return k

    def _druck(self, kode: int):
        self.keyPressed.emit(kode, False, False)

    def knopf(self, name: str) -> QPushButton:
        """Der Knopf einer Terminaltaste (``TASTEN``-Name) — für Tests."""
        return self._tasten[name]

    def zeige_flags(self, flags: int):
        """Rasttasten nach dem Terminalzustand (``term_flags``) setzen, ohne Taste auszulösen."""
        self.si_so.blockSignals(True)
        self.caps.blockSignals(True)
        self.si_so.setChecked(bool(flags & TF_ZG2))
        self.caps.setChecked(bool(flags & TF_CAPS))
        self.si_so.blockSignals(False)
        self.caps.blockSignals(False)

    def set_powered(self, an: bool):
        self.setEnabled(bool(an))

    def set_leds(self, maske: int):
        """Schnittstelle der anderen Tastaturen — die Anzeigen kommen hier aus ``term_flags``."""

    def clear_host_keys(self):
        """Schnittstelle der anderen Tastaturen (keine Nachbildung der Host-Tasten)."""

    def sizeHint(self) -> QSize:
        return QSize(430, 4 + 3 * 28 + 28 + 12)

    def heightForWidth(self, breite: int) -> int:
        return self.sizeHint().height()
