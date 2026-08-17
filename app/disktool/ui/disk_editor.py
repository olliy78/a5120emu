"""Diskeditor — die Diskette als Scheibe, Sektor für Sektor.

Eine Ebene **unter** dem Dateisystem: hier gibt es keine Dateien, nur Spuren,
Sektoren, Gaps und CRCs.  Genau das braucht man, um eine schadhafte Diskette zu
begutachten oder von Hand zu reparieren — und um überhaupt zu *sehen*, was auf
einem Datenträger liegt, dessen Verzeichnis nichts (mehr) hergibt.

### Die Darstellung
Zwei Scheiben nebeneinander, links Seite 0, rechts Seite 1.  Spur 0 liegt **außen**,
Sektor 0 beginnt bei **12 Uhr**.  Seite 0 zählt im Uhrzeigersinn, Seite 1 dagegen —
so, wie man die Diskette sähe, wenn man sie umdreht.  Farben: Sektor mit gültigen
CRCs grün, mit CRC-Fehler rot, Gap orange, unformatiert grau.

**Die Winkel sind echt.**  Eine Spur im Speicher ist genau eine Umdrehung, also ist
der Winkel eines Bytes `Position ÷ Spurlänge`.  Drehzahl und Bitrate aus dem
HFE-Kopf werden dafür nicht gebraucht — sie skalieren nur die Zeitachse.  Bei `.img`
gibt es keine Winkelinformation im Container; dort liegen die Sektoren gleichmäßig,
und das sieht man dem Bild dann auch an.

### Zwei bewusste Abweichungen von der Kreisdarstellung
* **Trennlinien nur, wenn Platz ist.**  Bei 80 Spuren ist ein Ring wenige Pixel hoch;
  80 schwarze Ringlinien und 26 Speichen je Ring würden die Farbflächen auffressen.
  Sie werden deshalb erst ab einer Ringhöhe von @ref MIN_LINIE Pixeln gezeichnet.
* **Das Bild liegt als Pixmap vor** und wird nur bei Größen- oder Datenänderung neu
  gezeichnet; sonst kostete jede Mausbewegung ein paar tausend Bogenstücke.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import List, Optional, TYPE_CHECKING

from PySide6.QtCore import QPoint, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QFontDatabase, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout,
    QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QPushButton, QSizePolicy,
    QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from app.core_binding.k1520disk import GAP, SECTOR, UNFORMATTED, K1520DiskError

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool, Track

#: Bytes je Zeile im Hexfeld — ein 1024-B-Sektor ergibt damit 32 Zeilen.
HEX_BREITE = 32

#: Ab dieser Ringhöhe (Pixel) werden Trennlinien gezeichnet.
MIN_LINIE = 5.0

FARBE_OK          = QColor("#3fa34d")   # Sektor mit Daten, beide CRCs gültig
# Ein formatierter, aber nie beschriebener Sektor: dieselbe Farbe, nur blass.  Es ist
# kein anderer Zustand, sondern derselbe mit weniger Inhalt — deshalb kein eigener
# Farbton, sondern eine hellere Tönung desselben Grüns.
FARBE_OK_LEER     = QColor("#a8dcae")   # Sektor ohne Daten (Datenfeld einförmig)
FARBE_DEFEKT      = QColor("#cc2b2b")   # Sektor mit CRC-Fehler
FARBE_GAP         = QColor("#e8912a")   # Gap zwischen den Sektorfeldern
FARBE_UNFORMAT    = QColor("#b8b8b8")   # unformatiert / markenloser Gap-Fluss
# Schwarz heisst NICHT „leer", sondern „noch keine Aussage": die Spur wurde vom
# echten Laufwerk noch nicht geholt.  Sie von „unformatiert" (grau) zu trennen ist
# wesentlich — das eine ist eine Feststellung über die Diskette, das andere keine.
FARBE_UNBEKANNT   = QColor("#101010")   # noch nicht gelesen (physisches Laufwerk)
FARBE_LINIE       = QColor("#000000")
FARBE_HINTERGRUND = QColor("#ffffff")
FARBE_AUSWAHL     = QColor("#1060d0")


#: Länge des UDOS-Sektorkontrollblocks hinter der Daten-CRC (§1.1 des Datenformats).
UDOS_TAIL = 4


def udos_zeiger(roh: bytes) -> str:
    """Ein UDOS-Zeiger (2 Byte) im Klartext.

    Byte 0 ist der **Sektorindex, 0-basiert** (0 = Sektor-ID 1), Byte 1 die
    Spurnummer; `FF FF` ist das Kettenende
    (`doc/udos_diskettenformat.md` §1.2).
    """
    if len(roh) < 2:
        return "?"
    if roh[0] == 0xFF and roh[1] == 0xFF:
        return "Ende"
    return f"Spur {roh[1]}/Sektor {roh[0] + 1}"


def _monospace() -> QFont:
    f = QFontDatabase.systemFont(QFontDatabase.FixedFont)
    f.setPointSize(max(9, f.pointSize()))
    return f


def hexdump(data: bytes, breite: int = HEX_BREITE) -> str:
    """``data`` als Hexdump: Offset, Hexbytes, ASCII-Deutung.

    Feste Spalten — :func:`parse_hexdump` liest sie positionsgenau zurück, damit ein
    ``.`` in der ASCII-Spalte nicht als Hexziffer missverstanden werden kann.
    """
    zeilen = []
    for ab in range(0, len(data), breite):
        teil = data[ab:ab + breite]
        hexteil = " ".join(f"{b:02X}" for b in teil)
        hexteil = hexteil.ljust(breite * 3 - 1)
        ascii_teil = "".join(chr(b) if 32 <= b < 127 else "." for b in teil)
        zeilen.append(f"{ab:04X}  {hexteil}  {ascii_teil}")
    return "\n".join(zeilen)


def parse_hexdump(text: str, breite: int = HEX_BREITE) -> bytes:
    """Hexdump zurücklesen.  Wirft ``ValueError`` mit Klartext.

    Gelesen wird **nur die Hexspalte** (feste Position), Offset und ASCII-Deutung
    werden übergangen: sie sind Anzeige, nicht Inhalt.  Wer den Offset verändert,
    ändert damit nichts — wer die Hexspalte verändert, schon.
    """
    ab, breite_spalte = 6, breite * 3 - 1
    out = bytearray()
    for nr, zeile in enumerate(text.split("\n"), 1):
        if not zeile.strip():
            continue
        if len(zeile) < ab:
            raise ValueError(f"Zeile {nr} ist zu kurz für einen Hexdump")
        spalte = zeile[ab:ab + breite_spalte]
        for stueck in spalte.split():
            if len(stueck) != 2:
                raise ValueError(f"Zeile {nr}: '{stueck}' ist kein Bytewert")
            try:
                out.append(int(stueck, 16))
            except ValueError:
                raise ValueError(f"Zeile {nr}: '{stueck}' ist keine Hexzahl") from None
    return bytes(out)


# ════════════════════════════════════════════════════════════════════════════
# Die Scheiben
# ════════════════════════════════════════════════════════════════════════════


class DiskSurface(QWidget):
    """Beide Diskettenseiten als Vektorgrafik, mit Tooltip und Auswahl."""

    #: (Seite, Spur, laufende Nummer des Sektors)
    sector_clicked = Signal(int, int, int)
    #: (Seite, Spur) — eine noch nicht gelesene Spur wurde angeklickt.
    track_requested = Signal(int, int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMouseTracking(True)
        self.setMinimumHeight(260)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setAutoFillBackground(True)

        self.tracks: List[List["Track"]] = [[], []]   # [Kopf][Zylinder]
        self.heads = 0
        self.cylinders = 0
        self.auswahl: Optional[tuple] = None          # (Kopf, Zylinder, Sektornummer)
        self._bild: Optional[QPixmap] = None

    # ── Daten ───────────────────────────────────────────────────────────────

    def load(self, tool: "DiskTool") -> None:
        """Alle **bekannten** Spuren beider Seiten einlesen.

        Eine noch nie gelesene Spur (echtes Laufwerk) wird als ``None`` geführt und
        schwarz gezeichnet.  Sie hier zu holen hiesse, das Öffnen des Editors auf die
        ganze Diskette warten zu lassen — anderthalb Minuten, in denen das Fenster
        steht.  Der Editor zeigt stattdessen, was er weiss, und wächst mit
        (:meth:`aktualisieren`).
        """
        self.cylinders = tool.medium_cylinders
        self.heads = tool.medium_heads
        self.tracks = [[], []]
        for kopf in range(min(2, self.heads)):
            self.tracks[kopf] = [self._bekannte_spur(tool, c, kopf)
                                 for c in range(self.cylinders)]
        self._bild = None
        self.update()

    @staticmethod
    def _bekannte_spur(tool: "DiskTool", cyl: int, head: int):
        """Die Spur — oder ``None``, wenn sie noch nie gelesen wurde.

        Der Zustand wird VOR dem Zugriff gefragt; ``tool.track()`` würde die Spur
        sonst beschaffen und dabei blockieren.
        """
        if tool.track_state(cyl, head) == tool.SPUR_UNBEKANNT:
            return None
        return tool.track(cyl, head)

    def aktualisieren(self, tool: "DiskTool") -> bool:
        """Spuren nachtragen, die inzwischen gelesen wurden.

        Returns:
            True, wenn sich etwas geändert hat (dann wurde neu gezeichnet).
        """
        if self.cylinders <= 0:
            return False
        neu = False
        for kopf in range(min(2, self.heads)):
            for c in range(self.cylinders):
                if self.tracks[kopf][c] is not None:
                    continue
                if tool.track_state(c, kopf) == tool.SPUR_UNBEKANNT:
                    continue
                self.tracks[kopf][c] = tool.track(c, kopf)
                neu = True
        if neu:
            self._bild = None
            self.update()
        return neu

    def reload_track(self, tool: "DiskTool", cyl: int, head: int) -> None:
        """Eine einzelne Spur auffrischen (nach dem Schreiben eines Sektors)."""
        if 0 <= head < len(self.tracks) and 0 <= cyl < len(self.tracks[head]):
            self.tracks[head][cyl] = tool.track(cyl, head)
            self._bild = None
            self.update()

    # ── Geometrie ───────────────────────────────────────────────────────────

    def _mitte(self, kopf: int) -> tuple:
        """Mittelpunkt und Radien der Scheibe von @p kopf.

        Über der Scheibe wird **kein** Platz für die Beschriftung freigehalten: sie
        steht im leeren Eck oben links (ein Kreis füllt sein Quadrat nicht aus).
        Das ist der Unterschied zwischen einer Scheibe, die die Fensterhöhe
        ausnutzt, und einer, die 26 Pixel darüber verschenkt.
        """
        halb = self.width() / 2.0
        cx = halb * (0.5 + kopf)
        cy = self.height() / 2.0
        r_aussen = max(10.0, min(halb, self.height()) / 2.0 - 6.0)
        return cx, cy, r_aussen, r_aussen * 0.22    # Nabe = Loch in der Mitte

    def _ringhoehe(self, r_aussen: float, r_nabe: float) -> float:
        n = max(1, self.cylinders)
        return (r_aussen - r_nabe) / n

    def treffer(self, punkt) -> Optional[tuple]:
        """Was liegt unter @p punkt?  ``(Kopf, Spur, Span)`` oder ``None``.

        Rein rechnerisch über Polarkoordinaten — kein Szenengraph, damit auch bei
        80 Spuren × 26 Sektoren × 2 Seiten jede Mausbewegung billig bleibt.
        """
        if self.cylinders <= 0:
            return None
        kopf = 0 if punkt.x() < self.width() / 2.0 else 1
        if kopf >= len(self.tracks) or not self.tracks[kopf]:
            return None

        cx, cy, r_aussen, r_nabe = self._mitte(kopf)
        dx, dy = punkt.x() - cx, punkt.y() - cy
        r = math.hypot(dx, dy)
        if not (r_nabe <= r <= r_aussen):
            return None

        spur = int((r_aussen - r) / self._ringhoehe(r_aussen, r_nabe))
        spur = min(spur, self.cylinders - 1)
        if spur >= len(self.tracks[kopf]):
            return None

        # Bildschirm-y wächst nach unten; theta ist der übliche mathematische Winkel.
        theta = math.degrees(math.atan2(-dy, dx))
        roh = (90.0 - theta) if kopf == 0 else (theta - 90.0)
        anteil = (roh % 360.0) / 360.0

        gefunden = self.tracks[kopf][spur]
        if gefunden is None:                 # noch nicht gelesen — nichts zu treffen
            return kopf, spur, None
        for s in gefunden.spans:
            if s.start <= anteil < s.end:
                return kopf, spur, s
        return None

    # ── Zeichnen ────────────────────────────────────────────────────────────

    def _farbe(self, span) -> QColor:
        if span.kind == SECTOR:
            if not span.ok:
                return FARBE_DEFEKT
            # Leer heisst „Datenfeld einförmig" — der UDOS-Anhang hinter der
            # Daten-CRC zählt NICHT mit: er trägt die Dateiverkettung und ist auch
            # auf einer frisch formatierten Diskette belegt.
            return FARBE_OK_LEER if getattr(span, "blank", False) else FARBE_OK
        if span.kind == GAP:
            return FARBE_GAP
        return FARBE_UNFORMAT

    def _zeichne_scheibe(self, p: QPainter, kopf: int) -> None:
        cx, cy, r_aussen, r_nabe = self._mitte(kopf)

        p.setPen(QPen(FARBE_LINIE, 1.2))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QRectF(cx - r_aussen, cy - r_aussen, 2 * r_aussen, 2 * r_aussen))
        p.drawEllipse(QRectF(cx - r_nabe, cy - r_nabe, 2 * r_nabe, 2 * r_nabe))

        # Beschriftung auf etwa ein Viertel der Scheibenbreite nach links — dort ist
        # zwischen Kreisbogen und Ecke des Quadrats ohnehin nichts.
        p.drawText(QRectF(cx - r_aussen, cy - r_aussen, r_aussen, 20),
                   Qt.AlignCenter, f"Seite {kopf}")

        spuren = self.tracks[kopf] if kopf < len(self.tracks) else []
        if not spuren:
            # Einseitige Diskette: nur die beiden Kreise, sonst nichts — das ist die
            # ehrliche Auskunft „diese Seite gibt es nicht".
            return

        hoehe = self._ringhoehe(r_aussen, r_nabe)
        linien = hoehe >= MIN_LINIE

        for c, spur in enumerate(spuren):
            r_ring = r_aussen - (c + 0.5) * hoehe          # Mitte des Rings
            rect = QRectF(cx - r_ring, cy - r_ring, 2 * r_ring, 2 * r_ring)
            p.setBrush(Qt.NoBrush)
            if spur is None:
                # Noch nicht gelesen: voller Ring, keine Abschnitte — wir wissen ja
                # nicht einmal, ob die Spur formatiert ist.
                stift = QPen(FARBE_UNBEKANNT, hoehe)
                stift.setCapStyle(Qt.FlatCap)
                p.setPen(stift)
                p.drawArc(rect, 0, 360 * 16)
                continue
            for s in spur.spans:
                weite = (s.end - s.start) * 360.0
                if weite <= 0:
                    continue
                start = (90.0 - s.end * 360.0) if kopf == 0 else (90.0 + s.start * 360.0)
                stift = QPen(self._farbe(s), hoehe)
                stift.setCapStyle(Qt.FlatCap)
                p.setPen(stift)
                p.drawArc(rect, int(round(start * 16)), int(round(weite * 16)))

            if linien:
                p.setPen(QPen(FARBE_LINIE, 0.6))
                r_kante = r_aussen - c * hoehe
                p.drawEllipse(QRectF(cx - r_kante, cy - r_kante,
                                     2 * r_kante, 2 * r_kante))
                for s in spur.spans:
                    winkel = (90.0 - s.start * 360.0) if kopf == 0 \
                        else (90.0 + s.start * 360.0)
                    bog = math.radians(winkel)
                    p.drawLine(QPoint(int(cx + math.cos(bog) * (r_kante - hoehe)),
                                      int(cy - math.sin(bog) * (r_kante - hoehe))),
                               QPoint(int(cx + math.cos(bog) * r_kante),
                                      int(cy - math.sin(bog) * r_kante)))

    def _neu_zeichnen(self) -> QPixmap:
        bild = QPixmap(self.size())
        bild.fill(FARBE_HINTERGRUND)
        p = QPainter(bild)
        p.setRenderHint(QPainter.Antialiasing, True)
        for kopf in (0, 1):
            self._zeichne_scheibe(p, kopf)
        p.end()
        return bild

    def paintEvent(self, event):  # noqa: N802
        if self._bild is None or self._bild.size() != self.size():
            self._bild = self._neu_zeichnen()
        p = QPainter(self)
        p.drawPixmap(0, 0, self._bild)

        # Die Auswahl kommt obenauf, damit sie kein neues Gesamtbild kostet.
        if self.auswahl:
            kopf, spur, nummer = self.auswahl
            p.setRenderHint(QPainter.Antialiasing, True)
            cx, cy, r_aussen, r_nabe = self._mitte(kopf)
            hoehe = self._ringhoehe(r_aussen, r_nabe)
            r_ring = r_aussen - (spur + 0.5) * hoehe
            rect = QRectF(cx - r_ring, cy - r_ring, 2 * r_ring, 2 * r_ring)
            gewaehlt = self.tracks[kopf][spur] if spur < len(self.tracks[kopf]) else None
            for s in (gewaehlt.spans if gewaehlt else ()):
                if s.kind != SECTOR or s.index != nummer:
                    continue
                weite = (s.end - s.start) * 360.0
                start = (90.0 - s.end * 360.0) if kopf == 0 else (90.0 + s.start * 360.0)
                stift = QPen(FARBE_AUSWAHL, max(2.0, hoehe))
                stift.setCapStyle(Qt.FlatCap)
                p.setPen(stift)
                p.drawArc(rect, int(round(start * 16)), int(round(weite * 16)))
        p.end()

    def resizeEvent(self, event):  # noqa: N802
        self._bild = None
        super().resizeEvent(event)

    # ── Maus ────────────────────────────────────────────────────────────────

    def beschreibung(self, treffer) -> str:
        """Kurztext für den Tooltip."""
        kopf, spur, s = treffer
        if s is None:
            return (f"Seite {kopf} · Spur {spur} · noch nicht gelesen"
                    "  (anklicken, um sie jetzt zu holen)")
        if s.kind == UNFORMATTED:
            return f"Seite {kopf} · Spur {spur} · unformatiert"
        if s.kind == GAP:
            return f"Seite {kopf} · Spur {spur} · Gap"
        zustand = "" if s.ok else "  ⚠ CRC-Fehler"
        if s.ok and getattr(s, "blank", False):
            zustand = "  · leer (formatiert, keine Daten)"
        return (f"Seite {kopf} · Spur {spur} · Sektor {s.id}"
                f"  ({s.size} Byte){zustand}")

    def mouseMoveEvent(self, event):  # noqa: N802
        t = self.treffer(event.position())
        self.setToolTip(self.beschreibung(t) if t else "")
        super().mouseMoveEvent(event)

    def mousePressEvent(self, event):  # noqa: N802
        t = self.treffer(event.position())
        if t and t[2] is None:
            # Unbekannte Spur angeklickt: der Bediener will SIE sehen, also darf sie
            # jetzt geholt werden — das ist ein ausdrücklicher Wunsch, kein Beiwerk.
            self.track_requested.emit(t[0], t[1])
            super().mousePressEvent(event)
            return
        if t and t[2].kind == SECTOR:
            kopf, spur, s = t
            self.auswahl = (kopf, spur, s.index)
            self.update()
            self.sector_clicked.emit(kopf, spur, s.index)
        super().mousePressEvent(event)


# ════════════════════════════════════════════════════════════════════════════
# Das Fenster
# ════════════════════════════════════════════════════════════════════════════


class _Waehler(QWidget):
    """``[−] Beschriftung: [Wert] [+]`` — Zahl eingeben ODER durchschalten.

    Auf der Grafik einen bestimmten Sektor zu treffen ist Sucharbeit; wer weiß,
    dass er Spur 25 will, tippt sie.  Und wer *sucht*, schaltet mit den Knöpfen
    durch, ohne die Maus zu bewegen.
    """

    #: Neuer Wert (durch Eingabe oder Knopf).
    changed = Signal(int)
    #: Ein Knopf wurde gedrückt: ±1 (die Schrittweite bestimmt der Empfänger).
    stepped = Signal(int)

    def __init__(self, beschriftung: str, breite: int = 52, parent=None):
        super().__init__(parent)
        self.minus = QPushButton("−")
        self.plus = QPushButton("+")
        for k in (self.minus, self.plus):
            k.setFixedWidth(26)
            k.setAutoRepeat(True)          # gedrückt halten blättert weiter
        self.feld = QLineEdit()
        self.feld.setFixedWidth(breite)
        self.feld.setAlignment(Qt.AlignRight)
        self.feld.setFont(_monospace())

        zeile = QHBoxLayout(self)
        zeile.setContentsMargins(0, 0, 0, 0)
        zeile.setSpacing(2)
        zeile.addWidget(self.minus)
        zeile.addWidget(QLabel(f"{beschriftung}:"))
        zeile.addWidget(self.feld)
        zeile.addWidget(self.plus)

        self.minus.clicked.connect(lambda: self.stepped.emit(-1))
        self.plus.clicked.connect(lambda: self.stepped.emit(+1))
        self.feld.returnPressed.connect(self._eingegeben)
        self.feld.editingFinished.connect(self._eingegeben)

    def _eingegeben(self) -> None:
        text = self.feld.text().strip()
        if not text:
            return
        try:
            self.changed.emit(int(text, 10))
        except ValueError:
            pass                            # stehenlassen; setValue korrigiert später

    def value(self) -> Optional[int]:
        try:
            return int(self.feld.text().strip(), 10)
        except ValueError:
            return None

    def setValue(self, wert: Optional[int]) -> None:   # noqa: N802 (Qt-Namensschema)
        self.feld.blockSignals(True)
        self.feld.setText("" if wert is None else str(wert))
        self.feld.blockSignals(False)

    def setEnabled(self, an: bool) -> None:            # noqa: N802
        for w in (self.minus, self.plus, self.feld):
            w.setEnabled(an)


class NewTrackDialog(QDialog):
    """Wo die neue Spur hinkommt — und in welchem Verfahren.

    Zwei Angaben, und beide muss man treffen können:

    * **Die Spurnummer**, die die neue Spur bekommen soll.  Alles von dort an rückt
      nach hinten (aus 42 wird 43).  Zulässig ist auch **0** — eine Spur vor allen
      bestehenden — und das Ende (anhängen).
    * **Das Verfahren.**  Es folgt bewusst NICHT dem Nachbarn: in der K1520-Welt
      gibt es gemischte Formate, und gerade der Wechsel ist der Zweck — eine
      FM-Systemspur vor MFM-Daten, oder umgekehrt eine MFM-Spur hinter einer
      FM-Spur.
    """

    def __init__(self, tool: "DiskTool", vorschlag: int, parent=None):
        super().__init__(parent)
        self.tool = tool
        self.setWindowTitle("Spur einfügen")
        self.setMinimumWidth(430)

        self.f_pos = QSpinBox()
        self.f_pos.setRange(0, tool.medium_cylinders)      # ein Platz mehr: anhängen
        self.f_pos.setValue(max(0, min(vorschlag, tool.medium_cylinders)))
        self.f_pos.setToolTip(
            "Die Nummer, die die NEUE Spur bekommt.  Alles von dort an rückt nach "
            "hinten.  0 setzt sie vor alle bestehenden.")

        self.f_mfm = QComboBox()
        self.f_mfm.addItem("MFM (doppelte Dichte)", True)
        self.f_mfm.addItem("FM (einfache Dichte)", False)
        self.f_mfm.setToolTip(
            "Verfahren der neuen Spur.  Es folgt NICHT dem Nachbarn — gemischte "
            "Formate sind gerade der Zweck.")
        # Vorbelegung: was der künftige Vorgänger hat.  Das ist der übliche Fall;
        # der Wechsel bleibt ein bewusster Griff.
        self.f_mfm.setCurrentIndex(0 if self._nachbar_mfm(vorschlag) else 1)

        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)

        form = QFormLayout()
        form.addRow("Spurnummer:", self.f_pos)
        form.addRow("Verfahren:", self.f_mfm)
        form.addRow("", self.hinweis)

        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        knoepfe.button(QDialogButtonBox.Ok).setText("Einfügen")
        knoepfe.accepted.connect(self.accept)
        knoepfe.rejected.connect(self.reject)

        lay = QVBoxLayout(self)
        lay.addLayout(form)
        lay.addWidget(knoepfe)

        self.f_pos.valueChanged.connect(self._vorschau)
        self.f_mfm.currentIndexChanged.connect(self._vorschau)
        self._vorschau()

    def _nachbar_mfm(self, pos: int) -> bool:
        """Verfahren des künftigen Vorgängers (Vorgabe); MFM, wenn es keinen gibt."""
        vorher = pos - 1
        if vorher < 0 or vorher >= self.tool.medium_cylinders:
            vorher = min(max(pos, 0), self.tool.medium_cylinders - 1)
        try:
            return self.tool.track(vorher, 0).encoding != "FM"
        except K1520DiskError:
            return True

    def _vorschau(self) -> None:
        """Sagen, was geschieht — vor dem Bestätigen, nicht danach."""
        pos = self.f_pos.value()
        n = self.tool.medium_cylinders
        art = "MFM" if self.f_mfm.currentData() else "FM"
        if pos >= n:
            wohin = f"hinten angehängt (neue Spur {pos})"
        else:
            wohin = (f"vor der jetzigen Spur {pos} eingefügt — aus {pos} wird "
                     f"{pos + 1}, und so weiter bis {n - 1} → {n}")
        self.hinweis.setText(
            f"Die neue {art}-Spur wird {wohin}.  Sie ist **unformatiert**: Sektoren "
            f"legt man danach einzeln an.  Danach hat das Abbild {n + 1} Spuren."
            .replace("**", ""))

    def werte(self) -> tuple:
        return self.f_pos.value(), bool(self.f_mfm.currentData())


class NewSectorDialog(QDialog):
    """Angaben für einen neuen Sektor — und wo er landen wird.

    **Die ID bestimmt die Lage**: der neue Sektor kommt hinter den vorhandenen mit
    der nächstkleineren ID, um den eingestellten Gap versetzt.  Wer Lücken lassen
    will, gibt beim Anlegen des späteren Sektors einen Gap an, in den die fehlenden
    hineinpassen — sonst überschreibt der nächste angelegte den Nachbarn.  Was
    passieren wird, steht deshalb **im Dialog**, bevor man ihn bestätigt.
    """

    def __init__(self, tool: "DiskTool", cyl: int, head: int, spur: "Track",
                 udos: bool, parent=None):
        super().__init__(parent)
        self.tool = tool
        self.cyl, self.head = cyl, head
        self.spur = spur
        self.setWindowTitle(f"Neuer Sektor — Seite {head}, Spur {cyl}")
        self.setMinimumWidth(460)

        vorhanden = [s for s in spur.spans if s.kind == SECTOR]
        naechste = max((s.id for s in vorhanden), default=0) + 1 if vorhanden else 1

        self.f_id = QSpinBox()
        self.f_id.setRange(0, 255)
        self.f_id.setValue(naechste)
        self.f_id.setToolTip("Sektor-ID im ID-Feld — sie bestimmt zugleich die LAGE.")

        self.f_size = QComboBox()
        for n in (128, 256, 512, 1024):
            self.f_size.addItem(f"{n} Byte", n)
        self.f_size.setCurrentIndex(
            [128, 256, 512, 1024].index(vorhanden[0].size) if vorhanden else 0)

        self.f_gap = QSpinBox()
        self.f_gap.setRange(0, 4096)
        self.f_gap.setValue(self._gap_vorschlag())
        self.f_gap.setSuffix(" Byte")
        self.f_gap.setToolTip(
            "Gap zwischen dem Ende des vorherigen Sektors und der Sync-Gruppe des "
            "neuen.\nVorgabe: der Abstand, der auf dieser Spur tatsächlich vorkommt.")

        self.f_mfm = QComboBox()
        self.f_mfm.addItem("MFM (Double Density)", True)
        self.f_mfm.addItem("FM (Single Density)", False)
        self.f_mfm.setCurrentIndex(0 if spur.encoding != "FM" else 1)
        # FM und MFM lassen sich in EINER Spur nicht mischen — das Verfahren hängt
        # am Bit-Codec der ganzen Spur.  Nur eine leere Spur darf es noch festlegen.
        self.f_mfm.setEnabled(not vorhanden)

        self.f_tail = QCheckBox(f"UDOS-Kontrollblock ({UDOS_TAIL} Byte hinter der CRC)")
        self.f_tail.setChecked(udos)
        self.f_tail.setToolTip(
            "Rückwärts- und Vorwärtszeiger der UDOS-Dateiverkettung.\n"
            "Wird als „Kettenende“ (FF FF FF FF) angelegt.")

        self.f_fill = QLineEdit("E5")
        self.f_fill.setMaximumWidth(60)
        self.f_fill.setFont(_monospace())
        self.f_fill.setToolTip("Füllbyte der Nutzdaten (hexadezimal).")

        self.f_idcyl = QSpinBox(); self.f_idcyl.setRange(0, 255); self.f_idcyl.setValue(cyl)
        self.f_idhead = QSpinBox(); self.f_idhead.setRange(0, 255); self.f_idhead.setValue(head)
        kennung = QHBoxLayout()
        kennung.addWidget(QLabel("Zylinder"))
        kennung.addWidget(self.f_idcyl)
        kennung.addWidget(QLabel("Kopf"))
        kennung.addWidget(self.f_idhead)
        kennung.addStretch(1)
        kennung_w = QWidget()
        kennung_w.setLayout(kennung)
        kennung_w.setToolTip(
            "Was im ID-Feld steht.  Darf von der tatsächlichen Lage abweichen — "
            "genau das braucht man, um eine fehlerhafte Diskette nachzubauen.")

        form = QFormLayout()
        form.addRow("Sektor-ID", self.f_id)
        form.addRow("Größe", self.f_size)
        form.addRow("Gap davor", self.f_gap)
        form.addRow("Verfahren", self.f_mfm)
        form.addRow("", self.f_tail)
        form.addRow("Füllbyte", self.f_fill)
        form.addRow("ID-Feld", kennung_w)

        self.vorschau = QLabel("")
        self.vorschau.setWordWrap(True)

        self.knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        self.knoepfe.button(QDialogButtonBox.Ok).setText("Anlegen")
        self.knoepfe.accepted.connect(self.accept)
        self.knoepfe.rejected.connect(self.reject)

        lay = QVBoxLayout(self)
        lay.addLayout(form)
        lay.addWidget(self.vorschau)
        lay.addWidget(self.knoepfe)

        for w in (self.f_id, self.f_gap):
            w.valueChanged.connect(self._vorschau)
        self.f_size.currentIndexChanged.connect(self._vorschau)
        self.f_mfm.currentIndexChanged.connect(self._vorschau)
        self.f_tail.toggled.connect(self._vorschau)
        self._vorschau()

    # ── Vorgaben und Vorschau ───────────────────────────────────────────────

    def _gap_vorschlag(self) -> int:
        """Der Abstand, der auf DIESER Spur zwischen den Sektoren vorkommt.

        Ein nachgelegter Sektor soll aussehen wie seine Nachbarn.  Gibt es keine
        (leere Spur), bleibt der Normwert des Verfahrens.
        """
        laengen = sorted(round((s.end - s.start) * self.spur.bytes)
                         for s in self.spur.spans if s.kind == GAP)
        # Der Index-Gap ist der längste und untypisch — der Median trifft die
        # gewöhnlichen Zwischenräume.
        return laengen[len(laengen) // 2] if laengen else 24

    def werte(self) -> dict:
        try:
            fill = int(self.f_fill.text().strip() or "E5", 16) & 0xFF
        except ValueError:
            fill = 0xE5
        return {
            "id": self.f_id.value(),
            "size": self.f_size.currentData(),
            "gap": self.f_gap.value(),
            "tail_bytes": UDOS_TAIL if self.f_tail.isChecked() else 0,
            "mfm": bool(self.f_mfm.currentData()),
            "id_cyl": self.f_idcyl.value(),
            "id_head": self.f_idhead.value(),
            "fill": fill,
        }

    def betroffen(self) -> tuple:
        """``(von, laenge, [überschriebene Sektor-IDs])`` — was der Sektor treffen wird."""
        w = self.werte()
        von, laenge = self.tool.sector_plan(
            self.cyl, self.head, id=w["id"], size=w["size"], gap=w["gap"],
            tail_bytes=w["tail_bytes"], mfm=w["mfm"])
        bis = von + laenge
        getroffen = []
        for s in self.spur.spans:
            if s.kind != SECTOR:
                continue
            s_von = round(s.start * self.spur.bytes)
            s_bis = round(s.end * self.spur.bytes)
            if s_von < bis and von < s_bis:
                getroffen.append(s.id)
        return von, laenge, sorted(set(getroffen))

    def _vorschau(self) -> None:
        try:
            von, laenge, getroffen = self.betroffen()
        except K1520DiskError as e:
            self.vorschau.setText(str(e))
            return
        text = (f"Landet bei Byte {von} von {self.spur.bytes} "
                f"({von / max(1, self.spur.bytes) * 360:.0f}° nach dem Index) "
                f"und belegt {laenge} Byte.")
        passt = von + laenge <= self.spur.bytes
        if not passt:
            text += "  ⚠ Das passt nicht mehr auf die Spur."
        elif getroffen:
            text += ("  ⚠ Überschreibt Sektor "
                     + ", ".join(str(i) for i in getroffen) + ".")
        self.vorschau.setStyleSheet(
            "color: #a06000;" if (getroffen or not passt) else "color: #505050;")
        self.vorschau.setText(text)
        self.knoepfe.button(QDialogButtonBox.Ok).setEnabled(passt)


class DiskEditorWindow(QDialog):
    """Scheibenansicht oben, Sektorinhalt unten.

    Das Fenster hält **keinen** eigenen Diskettenstand: gelesen wird bei jedem
    Klick frisch aus dem Medium, und ``Save Sektor`` schreibt zurück **und in die
    Datei** — ein Sektoreditor, dessen „Speichern“ nichts speichert, wäre eine Falle.
    """

    #: Signal an das Hauptfenster: die Diskette hat sich geändert.
    disk_changed = Signal()

    def __init__(self, tool: "DiskTool", parent=None):
        super().__init__(parent)
        self.tool = tool
        self.setWindowTitle(f"Diskeditor — {tool.path}")
        # Ein QDialog bekommt vom Fensterverwalter sonst keinen Maximieren-Knopf —
        # ausgerechnet hier, wo mehr Fläche mehr Diskette bedeutet.
        self.setWindowFlags(Qt.Window
                            | Qt.WindowMinMaxButtonsHint
                            | Qt.WindowCloseButtonHint)
        self.resize(1000, 820)
        self.setSizeGripEnabled(True)

        self.aktuell: Optional[tuple] = None      # (Kopf, Spur, Sektornummer)
        self._im_umbau = False                   # gegen Rückkopplung beim Auffrischen
        # Ob hinter der Daten-CRC ein Kontrollblock steht, weiß nicht der Sektor,
        # sondern das DATEISYSTEM — deshalb einmal beim Öffnen bestimmt.  Nur ZDOS
        # (A5120) hat einen; UDOS1715/NDOS hält alles im Sektor.
        self.udos = (tool.filesystem_type == "udos")

        self.surface = DiskSurface()
        self.surface.sector_clicked.connect(self.select_sector)

        # ── Unterer Teil: Wähler, Angaben, CRC, Hexfeld, Knöpfe ─────────────
        self.w_seite = _Waehler("Seite", 34)
        self.w_spur = _Waehler("Spur", 44)
        self.w_sektor = _Waehler("Sektor", 44)
        self.w_seite.setValue(0)
        self.w_spur.setValue(0)
        self.w_seite.changed.connect(lambda v: self._springe(seite=v))
        self.w_spur.changed.connect(lambda v: self._springe(spur=v))
        self.w_sektor.changed.connect(lambda v: self._springe(sektor_id=v))
        self.w_seite.stepped.connect(lambda d: self._schritt_seite(d))
        self.w_spur.stepped.connect(lambda d: self._schritt_spur(d))
        self.w_sektor.stepped.connect(lambda d: self._schritt_sektor(d))

        self.info = QLabel("Kein Sektor gewählt — auf einen Sektor klicken.")
        # KEIN Umbruch: eine zweite Zeile hier kostet Höhe, die der Scheibe fehlt.
        self.info.setWordWrap(False)
        self.info.setTextInteractionFlags(Qt.TextSelectableByMouse)

        wahlzeile = QHBoxLayout()
        wahlzeile.addWidget(self.w_seite)
        wahlzeile.addSpacing(12)
        wahlzeile.addWidget(self.w_spur)
        wahlzeile.addSpacing(12)
        wahlzeile.addWidget(self.w_sektor)
        wahlzeile.addSpacing(16)

        self.crc_feld = QLineEdit()
        self.crc_feld.setMaximumWidth(90)
        self.crc_feld.setFont(_monospace())
        self.crc_feld.setToolTip(
            "Die Daten-CRC, wie sie auf der Diskette steht (hexadezimal).\n"
            "„Save Sektor“ schreibt genau diesen Wert — auch einen falschen, damit "
            "sich eine schadhafte Diskette originalgetreu nachbilden lässt.\n"
            "„Fix CRC“ trägt den zu den Daten passenden Wert ein.")
        self.crc_feld.textChanged.connect(self._crc_bewerten)
        self.crc_urteil = QLabel("")

        # Die CRC steht in derselben Zeile wie die Wähler — sie gehört zur Auswahl,
        # nicht zu den Angaben, und spart so eine Zeile Höhe.
        wahlzeile.addWidget(QLabel("Daten-CRC:"))
        wahlzeile.addWidget(self.crc_feld)
        wahlzeile.addWidget(self.crc_urteil)
        wahlzeile.addStretch(1)

        # ── UDOS-Nachspann: die Dateiverkettung, änderbar ───────────────────
        # Sie zu ändern ist etwas anderes, als die Nutzdaten zu ändern — deshalb ein
        # eigenes Feld.  Links die vier Rohbytes (das ist die Wahrheit auf dem
        # Medium), rechts, was sie bedeuten.
        self.tail_feld = QLineEdit()
        self.tail_feld.setMaximumWidth(130)
        self.tail_feld.setFont(_monospace())
        self.tail_feld.setToolTip(
            "Der UDOS-Sektorkontrollblock: Rückwärtszeiger (2 Byte) + "
            "Vorwärtszeiger (2 Byte), hexadezimal.\n"
            "Je Zeiger: Sektorindex (0-basiert), Spurnummer.  FF FF = Kettenende.\n"
            "„Save Sektor“ schreibt ihn mit.")
        self.tail_feld.textChanged.connect(self._tail_deuten)
        self.tail_deutung = QLabel("")
        self.tail_deutung.setTextInteractionFlags(Qt.TextSelectableByMouse)

        # Alles zum Sektor in EINER Zeile: Format, Größe, die vier Rohbytes, ihre
        # Deutung.  Eine eigene Zeile für den Anhang kostete Höhe, die im Fenster
        # der Scheibe fehlt.
        angaben = QHBoxLayout()
        angaben.setContentsMargins(0, 0, 0, 0)
        angaben.addWidget(self.info)
        angaben.addWidget(self.tail_feld)
        angaben.addWidget(self.tail_deutung)
        angaben.addStretch(1)
        self.angaben_widget = QWidget()
        self.angaben_widget.setLayout(angaben)
        # Zunächst weg — ob es einen Nachspann gibt, entscheidet der ANGEZEIGTE
        # Sektor (`_zeige_sektor`), nicht das erkannte Dateisystem.
        self.tail_feld.setVisible(False)
        self.tail_deutung.setVisible(False)

        self.hex = QPlainTextEdit()
        self.hex.setFont(_monospace())
        self.hex.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.hex.setPlaceholderText(
            "Hier steht der Inhalt des gewählten Sektors.\n"
            "Geändert wird ausschließlich die Hexspalte; Offset und ASCII-Deutung "
            "sind Anzeige.")
        # ÜBERSCHREIBEN statt Einfügen: in einem Hexdump sind die Spalten fest.
        # Beim Einfügen verschöbe jede getippte Ziffer den Rest der Zeile, und die
        # ASCII-Spalte liesse sich nicht mehr verlässlich nachziehen.
        self.hex.setOverwriteMode(True)
        # Auch eine Änderung der DATEN macht die CRC ungültig — das Urteil muss
        # beides im Blick haben, sonst stünde „gültig“ neben veränderten Bytes.
        self.hex.textChanged.connect(self._hex_geaendert)

        self.btn_reload = QPushButton("Reload Sektor")
        self.btn_fixcrc = QPushButton("Fix CRC")
        self.btn_save = QPushButton("Save Sektor")
        self.btn_reload.setToolTip("Änderungen verwerfen und neu aus der Diskette lesen")
        self.btn_fixcrc.setToolTip("Die zu den Daten passende CRC eintragen")
        self.btn_save.setToolTip(
            "Nutzdaten und CRC in die Diskette schreiben — unmittelbar in die "
            "Abbilddatei (beim ersten Mal entsteht eine Sicherungskopie „…~“)")
        self.btn_reload.clicked.connect(self.reload_sector)
        self.btn_fixcrc.clicked.connect(self.fix_crc)
        self.btn_save.clicked.connect(self.save_sector)

        self.btn_neu = QPushButton("Neuer Sektor…")
        self.btn_loeschen = QPushButton("Sektor löschen")
        self.btn_neu.setToolTip(
            "Einen Sektor auf dieser Spur anlegen — die ID bestimmt, wo er landet")
        self.btn_loeschen.setToolTip(
            "Den gewählten Sektor entfernen; sein Bereich wird wieder Gap")
        self.btn_neu.clicked.connect(self.new_sector)
        self.btn_loeschen.clicked.connect(self.delete_sector)

        # Ganze SPUREN — eine Ebene über den Sektoren.  Getrennt beschriftet, weil
        # „Spur löschen" etwas anderes ist als „Sektor löschen": es verschiebt die
        # ganze Diskette dahinter.
        self.btn_spur_neu = QPushButton("Spur einfügen")
        self.btn_spur_weg = QPushButton("Spur löschen")
        self.btn_spur_neu.setToolTip(
            "Eine LEERE Spur einfügen — Stelle und Verfahren (FM/MFM) werden "
            "erfragt.  Alles ab dort rückt nach hinten; Sektoren legt man danach an.")
        self.btn_spur_weg.setToolTip(
            "Die gewählte Spur ganz aus dem Abbild werfen; alles dahinter rückt "
            "auf.  Für Abbilder mit zu vielen Spuren (82 statt 80) oder zum "
            "Zurechtstutzen (77 Spuren für 8″).")
        self.btn_spur_neu.clicked.connect(self.insert_track)
        self.btn_spur_weg.clicked.connect(self.delete_track)

        knopfzeile = QHBoxLayout()
        knopfzeile.addWidget(self.btn_reload)
        knopfzeile.addWidget(self.btn_fixcrc)
        knopfzeile.addWidget(self.btn_save)
        knopfzeile.addSpacing(16)
        knopfzeile.addWidget(self.btn_neu)
        knopfzeile.addWidget(self.btn_loeschen)
        knopfzeile.addSpacing(16)
        knopfzeile.addWidget(self.btn_spur_neu)
        knopfzeile.addWidget(self.btn_spur_weg)
        knopfzeile.addStretch(1)
        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)
        knopfzeile.addWidget(self.hinweis, 2)

        unten = QWidget()
        unten_lay = QVBoxLayout(unten)
        unten_lay.setContentsMargins(6, 6, 6, 6)
        unten_lay.addLayout(wahlzeile)
        unten_lay.addWidget(self.angaben_widget)
        unten_lay.addWidget(self.hex, 1)
        unten_lay.addLayout(knopfzeile)

        teiler = QSplitter(Qt.Vertical)
        teiler.addWidget(self.surface)
        teiler.addWidget(unten)
        teiler.setStretchFactor(0, 3)
        teiler.setStretchFactor(1, 2)

        self.surface.load(tool)

        lay = QVBoxLayout(self)
        lay.addWidget(self._legende())
        lay.addWidget(teiler, 1)
        self._enable(False)

        # Solange die Diskette noch wächst (echtes Laufwerk), die Ansicht nachführen.
        # Der Zeitgeber hält sich selbst an: sind alle Spuren bekannt, hört er auf.
        self.surface.track_requested.connect(self._spur_anfordern)
        self._nachlauf = QTimer(self)
        self._nachlauf.setInterval(1000)
        self._nachlauf.timeout.connect(self._nachtragen)
        if any(t is None for seite in self.surface.tracks for t in seite):
            self._nachlauf.start()

    # ── Aufbau ──────────────────────────────────────────────────────────────

    # ── Nachwachsende Diskette (echtes Laufwerk) ────────────────────────────

    def _nachtragen(self) -> None:
        """Inzwischen gelesene Spuren in die Ansicht übernehmen.

        Fragt nur den **Zustand** ab (`track_state`) und holt ausschliesslich das,
        was schon da ist — der Zeitgeber darf das Laufwerk nicht antreiben, sonst
        führte das blosse Offenhalten des Editors die ganze Diskette ein.
        """
        try:
            self.surface.aktualisieren(self.tool)
        except K1520DiskError:
            self._nachlauf.stop()               # Diskette weg — nichts mehr zu holen
            return
        if not any(t is None for seite in self.surface.tracks for t in seite):
            self._nachlauf.stop()               # vollständig, es gibt nichts mehr

    def _spur_anfordern(self, seite: int, spur: int) -> None:
        """Eine noch ungelesene Spur auf Wunsch jetzt holen.

        Das blockiert (eine halbe bis ganze Sekunde am echten Laufwerk) — deshalb
        der Wartecursor.  Es ist der einzige Weg, auf dem der Editor selbst ein
        Lesen auslöst, und er geht immer vom Bediener aus.
        """
        from PySide6.QtGui import QCursor
        from PySide6.QtWidgets import QApplication

        QApplication.setOverrideCursor(QCursor(Qt.WaitCursor))
        try:
            self.surface.reload_track(self.tool, spur, seite)
        except K1520DiskError as e:
            QMessageBox.warning(self, "Spur lesen", str(e))
        finally:
            QApplication.restoreOverrideCursor()

    def _legende(self) -> QWidget:
        w = QWidget()
        zeile = QHBoxLayout(w)
        zeile.setContentsMargins(6, 2, 6, 2)
        eintraege = [(FARBE_OK, "Sektor mit Daten"), (FARBE_OK_LEER, "leer"),
                     (FARBE_DEFEKT, "CRC-Fehler"),
                     (FARBE_GAP, "Gap"), (FARBE_UNFORMAT, "unformatiert")]
        # „Noch nicht gelesen" gibt es nur an einem nachladenden Medium; bei einer
        # Datei stünde ein Eintrag in der Legende, der nie vorkommt.
        if any(t is None for seite in self.surface.tracks for t in seite):
            eintraege.append((FARBE_UNBEKANNT, "noch nicht gelesen"))
        for farbe, text in eintraege:
            punkt = QLabel("  ")
            punkt.setStyleSheet(
                f"background:{farbe.name()}; border:1px solid black;")
            punkt.setFixedWidth(16)
            zeile.addWidget(punkt)
            zeile.addWidget(QLabel(text))
            zeile.addSpacing(10)
        zeile.addStretch(1)
        zeile.addWidget(QLabel("Spur 0 außen · Sektor 0 bei 12 Uhr · "
                               "Seite 1 gespiegelt (von oben durch die Scheibe)"))
        return w

    def _enable(self, an: bool) -> None:
        schreibbar = an and not self.tool.read_only
        self.hex.setReadOnly(not schreibbar)
        self.crc_feld.setReadOnly(not schreibbar)
        self.tail_feld.setReadOnly(not schreibbar)
        self.btn_reload.setEnabled(an)
        self.btn_fixcrc.setEnabled(schreibbar)
        self.btn_save.setEnabled(schreibbar)
        self.btn_loeschen.setEnabled(schreibbar)
        # Anlegen geht auch auf einer LEEREN Spur — dort gibt es keinen Sektor zu
        # wählen, und genau dann braucht man den Knopf am dringendsten.
        self.btn_neu.setEnabled(not self.tool.read_only
                                and self.surface.cylinders > 0)
        # Ganze Spuren ändern die GEOMETRIE des Abbilds — erst recht nichts für
        # eine schreibgeschützt geöffnete Diskette.  Sie hingen bisher an gar
        # nichts und liessen sich auch im Nur-Lesen-Zustand auslösen.
        aendern = not self.tool.read_only and self.surface.cylinders > 0
        self.btn_spur_neu.setEnabled(aendern)
        self.btn_spur_weg.setEnabled(aendern and self.surface.cylinders > 1)
        if an and self.tool.read_only:
            self.hinweis.setText("Diskette ist schreibgeschützt geöffnet — "
                                 "„Nur lesen“ im Hauptfenster abwählen zum Ändern.")

    # ── Sektor wählen und anzeigen ──────────────────────────────────────────

    def select_sector(self, head: int, cyl: int, index: int) -> bool:
        """Einen Sektor in den Editor holen.  False = gibt es nicht (mehr)."""
        try:
            daten = self.tool.sector_data(cyl, head, index)
            crc = self.tool.sector_crc(cyl, head, index)
        except K1520DiskError as e:
            self.hinweis.setText(str(e))
            return False

        self.aktuell = (head, cyl, index)
        self.surface.auswahl = (head, cyl, index)
        self.surface.update()
        self.w_seite.setValue(head)
        self.w_spur.setValue(cyl)
        self.w_seite.setEnabled(self.surface.heads > 1)

        spur = self.tool.track(cyl, head)
        span = next((s for s in spur.spans
                     if s.kind == SECTOR and s.index == index), None)
        self.w_sektor.setValue(span.id if span else None)
        marke = "  · Datenmarke F8 (gelöscht)" if span and span.deleted else ""
        id_crc = "" if not span or span.id_crc_ok else "  · ID-CRC FEHLERHAFT"

        # Bei UDOS hängt hinter der Daten-CRC ein 4-Byte-Kontrollblock — der Sektor
        # ist damit tatsächlich größer als sein ID-Feld sagt, und die beiden Zeiger
        # darin sind die Dateiverkettung.  CP/M kennt das nicht; dort bleibt die
        # Angabe weg, statt eine leere Spalte zu zeigen.
        verfahren = f"IBM-{spur.encoding}"
        groesse = f"{len(daten)} Byte"
        # Zwei Wege zur selben Frage — und beide werden gebraucht:
        #
        # * **Der Sektor selbst**: steht hinter der Daten-CRC etwas anderes als
        #   Gap-Füllbytes, ist der Anhang belegt.  Das trägt auch eine gemischte
        #   oder gar nicht erkannte Diskette — dort ist es die einzige Auskunft.
        # * **Das Dateisystem**: auf einer FRISCH FORMATIERTEN UDOS-Diskette lautet
        #   der Kontrollblock nie beschriebener Sektoren `4E 4E 4E 4E`
        #   (doc/udos_diskettenformat.md §1.1) — vom Gap nicht zu unterscheiden.
        #   Wo UDOS erkannt ist, wissen wir es trotzdem besser.
        #
        # Nur das erste Kriterium war zu streng (frisch angelegte Disketten zeigten
        # nichts), nur das zweite war es auch (gemischte Disketten zeigten nichts).
        anhang_da = bool(getattr(span, "tail_bytes", 0)) if span else False
        anhang_da = anhang_da or (self.udos and span is not None)
        self.tail_feld.setVisible(anhang_da)
        self.tail_deutung.setVisible(anhang_da)
        if anhang_da:
            anhang = self.tool.sector_tail(cyl, head, index)[:UDOS_TAIL]
            verfahren += " + UDOS-Erweiterung"
            # Nutzdaten + Kontrollblock.  Die Daten-CRC wird hier NICHT mitgezählt —
            # bei CP/M steht sie ebenso wenig in der Größe, und sie hat ihr eigenes
            # Feld.  Sonst zählte dieselbe Angabe je Dateisystem etwas anderes.
            groesse = f"{len(daten)}+{UDOS_TAIL} Byte"
            self.tail_feld.blockSignals(True)
            self.tail_feld.setText(anhang.hex(" ").upper())
            self.tail_feld.blockSignals(False)
            self._tail_deuten()
        self.info.setText(f"Format: {verfahren},  Größe: {groesse}{marke}{id_crc}")

        self.crc_feld.setText(f"{crc:04X}")
        self._im_umbau = True                 # kein Auffrischen beim Befüllen
        self.hex.setPlainText(hexdump(daten))
        self._im_umbau = False
        self._enable(True)
        self._crc_bewerten()
        return True

    # ── Navigation über die Wähler ──────────────────────────────────────────

    def _spur_von(self, seite: int, spur: int):
        """Die Spur aus der eingelesenen Ansicht; ``None``, wenn es sie nicht gibt."""
        if not (0 <= seite < len(self.surface.tracks)):
            return None
        spuren = self.surface.tracks[seite]
        if not (0 <= spur < len(spuren)):
            return None
        return spuren[spur]

    def _sektoren(self, seite: int, spur: int) -> list:
        t = self._spur_von(seite, spur)
        return [s for s in t.spans if s.kind == SECTOR] if t else []

    def _springe(self, seite: Optional[int] = None, spur: Optional[int] = None,
                 sektor_id: Optional[int] = None) -> bool:
        """Auf einen eingetippten Wert springen; die übrigen bleiben stehen.

        Eine unmögliche Eingabe wird **zurückgesetzt statt angenommen** — sonst
        stünde im Feld eine Spur, die gar nicht angezeigt wird.
        """
        s_alt, p_alt, i_alt = self.aktuell or (self.w_seite.value() or 0,
                                               self.w_spur.value() or 0, None)
        ziel_seite = s_alt if seite is None else seite
        ziel_spur = p_alt if spur is None else spur

        sektoren = self._sektoren(ziel_seite, ziel_spur)
        if not sektoren:
            self._zeige_leere_spur(ziel_seite, ziel_spur)
            return False

        if sektor_id is not None:
            treffer = next((x for x in sektoren if x.id == sektor_id), None)
            if treffer is None:
                self.hinweis.setText(
                    f"Auf Seite {ziel_seite}, Spur {ziel_spur} gibt es keinen Sektor "
                    f"{sektor_id} (vorhanden: {sektoren[0].id}…{sektoren[-1].id}).")
                self._werte_zurueck()
                return False
        elif seite is None and spur is None:
            treffer = next((x for x in sektoren if x.index == i_alt), sektoren[0])
        else:
            # Beim Spur-/Seitenwechsel möglichst denselben Sektor weiterzeigen.
            gleiche = next((x for x in sektoren
                            if i_alt is not None and x.index == i_alt), None)
            treffer = gleiche or sektoren[0]

        self.hinweis.setText("")
        return self.select_sector(ziel_seite, ziel_spur, treffer.index)

    def _zeige_leere_spur(self, seite: int, spur: int) -> None:
        """Spur ohne Sektoren: sagen, was da ist, statt den alten Inhalt stehenzulassen."""
        t = self._spur_von(seite, spur)
        if t is None:
            self.hinweis.setText(f"Seite {seite}, Spur {spur} gibt es auf diesem "
                                 "Datenträger nicht.")
            self._werte_zurueck()
            return
        self.aktuell = None
        self.surface.auswahl = None
        self.surface.update()
        self.w_seite.setValue(seite)
        self.w_spur.setValue(spur)
        self.w_sektor.setValue(None)
        self.info.setText(f"Seite {seite}, Spur {spur}: keine Sektoren "
                          f"({'unformatiert' if not t.formatted else 'leer'}).")
        self.hex.blockSignals(True)
        self.hex.setPlainText("")
        self.hex.blockSignals(False)
        self.crc_feld.setText("")
        self.crc_urteil.setText("")
        self._enable(False)

    def _werte_zurueck(self) -> None:
        """Die Felder auf den tatsächlich angezeigten Sektor zurücksetzen."""
        if self.aktuell is None:
            return
        seite, spur, index = self.aktuell
        self.w_seite.setValue(seite)
        self.w_spur.setValue(spur)
        span = next((x for x in self._sektoren(seite, spur) if x.index == index), None)
        self.w_sektor.setValue(span.id if span else None)

    def _schritt_seite(self, richtung: int) -> None:
        seite = (self.w_seite.value() or 0) + richtung
        if 0 <= seite < max(1, self.surface.heads):
            self._springe(seite=seite)

    def _schritt_spur(self, richtung: int) -> None:
        spur = (self.w_spur.value() or 0) + richtung
        if 0 <= spur < self.surface.cylinders:
            self._springe(spur=spur)

    def _schritt_sektor(self, richtung: int) -> None:
        """Zum nächsten Sektor **in Spurreihenfolge** — nicht zur nächsten ID.

        Beim Suchen will man der Scheibe entlanggehen; die IDs müssen dabei weder
        lückenlos noch aufsteigend liegen (Sektorversatz, Fremdformate).
        """
        if self.aktuell is None:
            return
        seite, spur, index = self.aktuell
        sektoren = self._sektoren(seite, spur)
        stelle = next((i for i, x in enumerate(sektoren) if x.index == index), None)
        if stelle is None:
            return
        neu = stelle + richtung
        if 0 <= neu < len(sektoren):
            self.select_sector(seite, spur, sektoren[neu].index)

    def _hex_geaendert(self) -> None:
        """ASCII-Spalte nachziehen und die CRC neu bewerten."""
        self._ascii_auffrischen()
        self._crc_bewerten()

    def _ascii_auffrischen(self) -> None:
        """Die ASCII-Deutung an die geänderten Hexbytes anpassen.

        Der Dump wird neu erzeugt und die Schreibmarke an ihre Stelle zurückgesetzt.
        Solange die Hexspalte unvollständig ist (halb getippter Bytewert), bleibt
        alles stehen — Zwischenstände sind kein Grund, den Text umzubauen.
        """
        if self._im_umbau:
            return
        text = self.hex.toPlainText()
        try:
            daten = parse_hexdump(text)
        except ValueError:
            return
        neu = hexdump(daten)
        if neu == text:
            return

        self._im_umbau = True
        marke = self.hex.textCursor()
        stelle = marke.position()
        self.hex.setPlainText(neu)
        marke = self.hex.textCursor()
        marke.setPosition(min(stelle, len(neu)))
        self.hex.setTextCursor(marke)
        self._im_umbau = False

    def tail_bytes(self) -> Optional[bytes]:
        """Der eingetippte Nachspann; ``None``, wenn er nicht lesbar ist."""
        roh = self.tail_feld.text().replace(" ", "").strip()
        if len(roh) != UDOS_TAIL * 2:
            return None
        try:
            return bytes.fromhex(roh)
        except ValueError:
            return None

    def _tail_deuten(self) -> None:
        """Die vier Bytes in Klartext übersetzen — Spur und Sektor dezimal."""
        roh = self.tail_bytes()
        if roh is None:
            self.tail_deutung.setText(
                f"— {UDOS_TAIL} Bytes hexadezimal erwartet")
            self.tail_deutung.setStyleSheet("color: #cc2b2b;")
            return
        self.tail_deutung.setStyleSheet("color: #505050;")
        self.tail_deutung.setText(
            f"zurück: {udos_zeiger(roh[0:2])}    vor: {udos_zeiger(roh[2:4])}")

    def _crc_bewerten(self) -> None:
        """Sagt, ob die eingetragene CRC zu den angezeigten Daten passt."""
        if self.aktuell is None:
            self.crc_urteil.setText("")
            return
        head, cyl, index = self.aktuell
        try:
            daten = parse_hexdump(self.hex.toPlainText())
            soll = self.tool.sector_crc_for(cyl, head, index, daten)
            ist = int(self.crc_feld.text().strip() or "-1", 16)
        except (ValueError, K1520DiskError):
            self.crc_urteil.setText("(nicht prüfbar)")
            self.crc_urteil.setStyleSheet("color: #808080;")
            return
        if ist == soll:
            self.crc_urteil.setText("gültig")
            self.crc_urteil.setStyleSheet("color: #3fa34d;")
        else:
            self.crc_urteil.setText(f"ungültig — richtig wäre {soll:04X}")
            self.crc_urteil.setStyleSheet("color: #cc2b2b;")

    # ── Knöpfe ──────────────────────────────────────────────────────────────

    def reload_sector(self) -> bool:
        if self.aktuell is None:
            return False
        head, cyl, index = self.aktuell
        self.hinweis.setText("")
        return self.select_sector(head, cyl, index)

    def fix_crc(self) -> bool:
        """Die zu den angezeigten Daten passende CRC eintragen."""
        if self.aktuell is None:
            return False
        head, cyl, index = self.aktuell
        try:
            daten = parse_hexdump(self.hex.toPlainText())
            self.crc_feld.setText(f"{self.tool.sector_crc_for(cyl, head, index, daten):04X}")
        except (ValueError, K1520DiskError) as e:
            QMessageBox.warning(self, "CRC nicht berechenbar", str(e))
            return False
        return True

    # ── Sektoren anlegen und löschen (§19.4) ────────────────────────────────

    def _aktuelle_stelle(self) -> tuple:
        """``(Seite, Spur)`` — die gerade gezeigte Stelle, auch ohne gewählten Sektor."""
        if self.aktuell is not None:
            return self.aktuell[0], self.aktuell[1]
        return (self.w_seite.value() or 0), (self.w_spur.value() or 0)

    def _speichern_oder_melden(self) -> bool:
        """Nach einer Spuränderung in die Datei durchschreiben (wie `Save Sektor`)."""
        try:
            self.tool.flush()
        except K1520DiskError as e:
            QMessageBox.warning(
                self, "Nicht in die Datei geschrieben",
                f"{e}\n\nDie Spur ist im Speicher geändert. Um sie zu behalten: im "
                "Hauptfenster „Speichern unter…“ als .hfe oder .dmk.")
            return False
        return True

    def new_sector(self) -> bool:
        """Dialog öffnen und den Sektor anlegen; danach ist er sofort bearbeitbar."""
        head, cyl = self._aktuelle_stelle()
        spur = self.tool.track(cyl, head)
        if not spur.exists:
            QMessageBox.warning(self, "Keine Spur",
                                f"Seite {head}, Spur {cyl} gibt es auf diesem "
                                "Datenträger nicht.")
            return False

        dialog = NewSectorDialog(self.tool, cyl, head, spur, self.udos, self)
        if dialog.exec() != QDialog.Accepted:
            return False

        w = dialog.werte()
        _, _, getroffen = dialog.betroffen()
        if getroffen:
            # Ihr Modell erlaubt das Überschreiben ausdrücklich — unbeabsichtigt
            # soll es trotzdem nicht passieren.
            antwort = QMessageBox.question(
                self, "Überschreiben?",
                "Der neue Sektor überschreibt Sektor "
                + ", ".join(str(i) for i in getroffen)
                + " ganz oder teilweise.\n\nTrotzdem anlegen?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if antwort != QMessageBox.Yes:
                return False

        try:
            self.tool.sector_create(cyl, head, **w)
        except K1520DiskError as e:
            QMessageBox.critical(self, "Nicht angelegt", str(e))
            return False

        self.surface.reload_track(self.tool, cyl, head)
        self.disk_changed.emit()
        self._speichern_oder_melden()

        # Den neuen Sektor gleich anzeigen — er ist ja der Grund für den Aufwand.
        neu = next((s for s in self.tool.track(cyl, head).spans
                    if s.kind == SECTOR and s.id == w["id"]), None)
        if neu is not None:
            self.select_sector(head, cyl, neu.index)
        self.hinweis.setText(f"Sektor {w['id']} angelegt ({w['size']} Byte).")
        return True

    # ── Ganze Spuren ────────────────────────────────────────────────────────

    def _gewaehlte_spur(self):
        """Seite und Spur der Auswahl; ``None``, wenn nichts gewählt ist."""
        if self.aktuell is not None:
            head, cyl, _ = self.aktuell
            return head, cyl
        return None

    def delete_track(self) -> bool:
        """Die gewählte Spur ganz aus dem Abbild werfen.

        Anders als „Sektor löschen" verschiebt das die Diskette dahinter: aus Spur
        n+1 wird n.  Das ist der Weg, ein Abbild mit zu vielen Spuren (82 statt 80)
        oder auf eine Zielgeometrie (77 für 8″) zurechtzustutzen.
        """
        wahl = self._gewaehlte_spur()
        if wahl is None:
            return False
        _, cyl = wahl
        if QMessageBox.question(
                self, "Spur löschen",
                f"Spur {cyl} wird aus dem Abbild geworfen — mit BEIDEN Seiten.\n"
                f"Alles dahinter rückt auf; aus Spur {cyl + 1} wird Spur {cyl}.\n\n"
                f"Danach hat das Abbild {self.tool.medium_cylinders - 1} Spuren.",
                QMessageBox.Ok | QMessageBox.Cancel,
                QMessageBox.Cancel) != QMessageBox.Ok:
            return False
        try:
            n = self.tool.delete_cylinder(cyl)
        except K1520DiskError as e:
            QMessageBox.warning(self, "Spur löschen", str(e))
            return False
        self._nach_spurwechsel(f"Spur {cyl} gelöscht — {n} Spuren", cyl)
        return True

    def insert_track(self) -> bool:
        """Eine leere Spur einfügen — Stelle und Verfahren werden erfragt.

        Beides muss wählbar sein: die **Stelle**, weil man eine Spur auch VOR alle
        bestehenden setzen können muss, und das **Verfahren**, weil es in der
        K1520-Welt gemischte Formate gibt — eine FM-Systemspur vor MFM-Daten ist
        genau der Fall, für den das gebaut ist.
        """
        wahl = self._gewaehlte_spur()
        vorschlag = (wahl[1] + 1) if wahl is not None else self.tool.medium_cylinders

        dialog = NewTrackDialog(self.tool, vorschlag, self)
        if dialog.exec() != QDialog.Accepted:
            return False
        pos, mfm = dialog.werte()

        try:
            n = self.tool.insert_cylinder_at(pos, mfm)
        except K1520DiskError as e:
            QMessageBox.warning(self, "Spur einfügen", str(e))
            return False
        self._nach_spurwechsel(
            f"{'MFM' if mfm else 'FM'}-Spur {pos} eingefügt — {n} Spuren", pos)
        return True

    def _nach_spurwechsel(self, meldung: str, ziel: int) -> None:
        """Nach dem Löschen/Einfügen: alles neu zeichnen und wieder hinspringen.

        Die Geometrie hat sich geändert — die Ansicht muss komplett neu aufgebaut
        werden, sonst zeigt sie Spuren, die es nicht mehr gibt.
        """
        self.surface.load(self.tool)
        self.aktuell = None
        self.hinweis.setText(meldung)
        self.disk_changed.emit()
        if ziel < self.tool.medium_cylinders:
            self._springe(spur=ziel)
        else:
            self._zeige_leere_spur(0, max(0, self.tool.medium_cylinders - 1))

    def delete_sector(self) -> bool:
        """Den gewählten Sektor entfernen; sein Bereich wird wieder Gap."""
        if self.aktuell is None:
            return False
        head, cyl, index = self.aktuell
        span = next((s for s in self._sektoren(head, cyl) if s.index == index), None)
        kennung = f"Sektor {span.id}" if span else f"Sektor #{index}"

        antwort = QMessageBox.question(
            self, "Sektor löschen",
            f"{kennung} auf Seite {head}, Spur {cyl} entfernen?\n"
            "Sein Bereich wird wieder Gap; die Spurlänge bleibt.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if antwort != QMessageBox.Yes:
            return False

        try:
            # Wie viel Nachspann dieser Sektor hat, sagt er selbst.
            span = next((x for x in self.tool.track(cyl, head).spans
                         if x.kind == SECTOR and x.index == index), None)
            anhang = getattr(span, "tail_bytes", 0) if span else 0
            self.tool.sector_erase(cyl, head, index, anhang)
        except K1520DiskError as e:
            QMessageBox.critical(self, "Nicht gelöscht", str(e))
            return False

        self.surface.reload_track(self.tool, cyl, head)
        self.disk_changed.emit()
        self._speichern_oder_melden()

        # Weiterzeigen: der nächste vorhandene Sektor, sonst die leere Spur.
        uebrig = self._sektoren(head, cyl)
        if uebrig:
            self.select_sector(head, cyl, uebrig[min(index, len(uebrig) - 1)].index)
        else:
            self._zeige_leere_spur(head, cyl)
        self.hinweis.setText(f"{kennung} gelöscht.")
        return True

    def save_sector(self) -> bool:
        """Nutzdaten und die eingetragene CRC in die Diskette im Speicher schreiben."""
        if self.aktuell is None:
            return False
        head, cyl, index = self.aktuell

        try:
            daten = parse_hexdump(self.hex.toPlainText())
        except ValueError as e:
            QMessageBox.warning(self, "Nicht lesbar", str(e))
            return False

        text = self.crc_feld.text().strip()
        try:
            crc = int(text, 16)
        except ValueError:
            QMessageBox.warning(self, "Nicht lesbar",
                                f"'{text}' ist keine hexadezimale CRC.")
            return False
        if not 0 <= crc <= 0xFFFF:
            QMessageBox.warning(self, "Nicht lesbar", "Die CRC ist 16 Bit breit.")
            return False

        # Der UDOS-Nachspann gehört zum Sektor und wird deshalb mit demselben
        # „Save“ geschrieben — getrennt vom Datenfeld, damit die vorhandene CRC
        # dabei nicht angefasst wird.
        anhang = None
        if self.udos:
            anhang = self.tail_bytes()
            if anhang is None:
                QMessageBox.warning(
                    self, "Nicht lesbar",
                    f"Der UDOS-Anhang braucht genau {UDOS_TAIL} Bytes hexadezimal, "
                    f"z. B. „05 16 05 16“.")
                return False

        try:
            self.tool.sector_write(cyl, head, index, daten, crc)
            if anhang is not None:
                self.tool.sector_write_tail(cyl, head, index, anhang)
        except K1520DiskError as e:
            QMessageBox.critical(self, "Nicht geschrieben", str(e))
            return False

        self.surface.reload_track(self.tool, cyl, head)
        self.disk_changed.emit()

        # Bis in die DATEI durchschreiben: ein Sektoreditor, dessen „Save“ nur den
        # Arbeitsspeicher anfasst, ist eine Falle — man glaubt gespeichert zu haben.
        # Die Sicherungskopie `…~` legt die Bibliothek beim ersten Schreiben an.
        try:
            self.tool.flush()
        except K1520DiskError as e:
            # Der häufigste Fall: ein `.img` ist ein reines Sektorabbild und hat gar
            # kein CRC-Feld — eine absichtlich falsche CRC ist dort nicht darstellbar.
            # Die Änderung steht trotzdem im Speicher; der Weg heraus ist ein
            # Containerwechsel, und der steht in der Meldung.
            QMessageBox.warning(
                self, "Nicht in die Datei geschrieben",
                f"{e}\n\nDer Sektor ist in der Diskette im Speicher geändert. "
                "Um ihn zu behalten: im Hauptfenster „Speichern unter…“ als .hfe "
                "oder .dmk.")
            self.hinweis.setText("Geändert, aber NICHT in die Datei geschrieben — "
                                 "siehe Meldung.")
            self._crc_bewerten()
            return False

        self.hinweis.setText(
            f"Sektor geschrieben — {len(daten)} Byte, CRC {crc:04X}, "
            f"gespeichert in {Path(self.tool.path).name}.")
        self._crc_bewerten()
        return True
