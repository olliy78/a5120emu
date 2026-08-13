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
from typing import List, Optional, TYPE_CHECKING

from PySide6.QtCore import QPoint, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QFontDatabase, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (
    QDialog, QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPlainTextEdit,
    QPushButton, QSizePolicy, QSplitter, QVBoxLayout, QWidget,
)

from app.core_binding.k1520disk import GAP, SECTOR, UNFORMATTED, K1520DiskError

if TYPE_CHECKING:                                   # pragma: no cover
    from app.core_binding.k1520disk import DiskTool, Track

#: Bytes je Zeile im Hexfeld — ein 1024-B-Sektor ergibt damit 32 Zeilen.
HEX_BREITE = 32

#: Ab dieser Ringhöhe (Pixel) werden Trennlinien gezeichnet.
MIN_LINIE = 5.0

FARBE_OK          = QColor("#3fa34d")   # Sektor, beide CRCs gültig
FARBE_DEFEKT      = QColor("#cc2b2b")   # Sektor mit CRC-Fehler
FARBE_GAP         = QColor("#e8912a")   # Gap zwischen den Sektorfeldern
FARBE_UNFORMAT    = QColor("#b8b8b8")   # unformatiert / markenloser Gap-Fluss
FARBE_LINIE       = QColor("#000000")
FARBE_HINTERGRUND = QColor("#ffffff")
FARBE_AUSWAHL     = QColor("#1060d0")


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
        """Alle Spuren beider Seiten einlesen — immer frisch aus dem Medium."""
        self.cylinders = tool.medium_cylinders
        self.heads = tool.medium_heads
        self.tracks = [[], []]
        for kopf in range(min(2, self.heads)):
            self.tracks[kopf] = [tool.track(c, kopf) for c in range(self.cylinders)]
        self._bild = None
        self.update()

    def reload_track(self, tool: "DiskTool", cyl: int, head: int) -> None:
        """Eine einzelne Spur auffrischen (nach dem Schreiben eines Sektors)."""
        if 0 <= head < len(self.tracks) and 0 <= cyl < len(self.tracks[head]):
            self.tracks[head][cyl] = tool.track(cyl, head)
            self._bild = None
            self.update()

    # ── Geometrie ───────────────────────────────────────────────────────────

    def _mitte(self, kopf: int) -> tuple:
        """Mittelpunkt und Radien der Scheibe von @p kopf."""
        halb = self.width() / 2.0
        cx = halb * (0.5 + kopf)
        cy = self.height() / 2.0
        r_aussen = max(10.0, min(halb, self.height()) / 2.0 - 26.0)
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

        for s in self.tracks[kopf][spur].spans:
            if s.start <= anteil < s.end:
                return kopf, spur, s
        return None

    # ── Zeichnen ────────────────────────────────────────────────────────────

    def _farbe(self, span) -> QColor:
        if span.kind == SECTOR:
            return FARBE_OK if span.ok else FARBE_DEFEKT
        if span.kind == GAP:
            return FARBE_GAP
        return FARBE_UNFORMAT

    def _zeichne_scheibe(self, p: QPainter, kopf: int) -> None:
        cx, cy, r_aussen, r_nabe = self._mitte(kopf)

        p.setPen(QPen(FARBE_LINIE, 1.2))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QRectF(cx - r_aussen, cy - r_aussen, 2 * r_aussen, 2 * r_aussen))
        p.drawEllipse(QRectF(cx - r_nabe, cy - r_nabe, 2 * r_nabe, 2 * r_nabe))

        beschriftung = f"Seite {kopf}"
        p.drawText(QRectF(cx - r_aussen, cy - r_aussen - 24, 2 * r_aussen, 20),
                   Qt.AlignCenter, beschriftung)

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
            for s in self.tracks[kopf][spur].spans:
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
        if s.kind == UNFORMATTED:
            return f"Seite {kopf} · Spur {spur} · unformatiert"
        if s.kind == GAP:
            return f"Seite {kopf} · Spur {spur} · Gap"
        zustand = "" if s.ok else "  ⚠ CRC-Fehler"
        return (f"Seite {kopf} · Spur {spur} · Sektor {s.id}"
                f"  ({s.size} Byte){zustand}")

    def mouseMoveEvent(self, event):  # noqa: N802
        t = self.treffer(event.position())
        self.setToolTip(self.beschreibung(t) if t else "")
        super().mouseMoveEvent(event)

    def mousePressEvent(self, event):  # noqa: N802
        t = self.treffer(event.position())
        if t and t[2].kind == SECTOR:
            kopf, spur, s = t
            self.auswahl = (kopf, spur, s.index)
            self.update()
            self.sector_clicked.emit(kopf, spur, s.index)
        super().mousePressEvent(event)


# ════════════════════════════════════════════════════════════════════════════
# Das Fenster
# ════════════════════════════════════════════════════════════════════════════


class DiskEditorWindow(QDialog):
    """Scheibenansicht oben, Sektorinhalt unten.

    Das Fenster hält **keinen** eigenen Diskettenstand: gelesen wird bei jedem
    Klick frisch aus dem Medium, und ``Save Sektor`` schreibt dorthin zurück (in die
    Datei kommt es erst mit ``Speichern`` im Hauptfenster).
    """

    #: Signal an das Hauptfenster: die Diskette hat sich geändert.
    disk_changed = Signal()

    def __init__(self, tool: "DiskTool", parent=None):
        super().__init__(parent)
        self.tool = tool
        self.setWindowTitle(f"Diskeditor — {tool.path}")
        self.resize(1000, 820)
        self.setSizeGripEnabled(True)

        self.aktuell: Optional[tuple] = None      # (Kopf, Spur, Sektornummer)

        self.surface = DiskSurface()
        self.surface.sector_clicked.connect(self.select_sector)

        # ── Unterer Teil: Angaben, CRC, Hexfeld, Knöpfe ─────────────────────
        self.info = QLabel("Kein Sektor gewählt — auf einen Sektor klicken.")
        self.info.setWordWrap(True)
        self.info.setTextInteractionFlags(Qt.TextSelectableByMouse)

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

        crc_zeile = QHBoxLayout()
        crc_zeile.addWidget(QLabel("Daten-CRC:"))
        crc_zeile.addWidget(self.crc_feld)
        crc_zeile.addWidget(self.crc_urteil, 1)

        self.hex = QPlainTextEdit()
        self.hex.setFont(_monospace())
        self.hex.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.hex.setPlaceholderText(
            "Hier steht der Inhalt des gewählten Sektors.\n"
            "Geändert wird ausschließlich die Hexspalte; Offset und ASCII-Deutung "
            "sind Anzeige.")
        # Auch eine Änderung der DATEN macht die CRC ungültig — das Urteil muss
        # beides im Blick haben, sonst stünde „gültig“ neben veränderten Bytes.
        self.hex.textChanged.connect(self._crc_bewerten)

        self.btn_reload = QPushButton("Reload Sektor")
        self.btn_fixcrc = QPushButton("Fix CRC")
        self.btn_save = QPushButton("Save Sektor")
        self.btn_reload.setToolTip("Änderungen verwerfen und neu aus der Diskette lesen")
        self.btn_fixcrc.setToolTip("Die zu den Daten passende CRC eintragen")
        self.btn_save.setToolTip(
            "Nutzdaten und CRC in die Diskette im Speicher schreiben "
            "(in die Datei erst mit „Speichern“ im Hauptfenster)")
        self.btn_reload.clicked.connect(self.reload_sector)
        self.btn_fixcrc.clicked.connect(self.fix_crc)
        self.btn_save.clicked.connect(self.save_sector)

        knopfzeile = QHBoxLayout()
        knopfzeile.addWidget(self.btn_reload)
        knopfzeile.addWidget(self.btn_fixcrc)
        knopfzeile.addWidget(self.btn_save)
        knopfzeile.addStretch(1)
        self.hinweis = QLabel("")
        self.hinweis.setWordWrap(True)
        knopfzeile.addWidget(self.hinweis, 2)

        unten = QWidget()
        unten_lay = QVBoxLayout(unten)
        unten_lay.setContentsMargins(6, 6, 6, 6)
        unten_lay.addWidget(self.info)
        unten_lay.addLayout(crc_zeile)
        unten_lay.addWidget(self.hex, 1)
        unten_lay.addLayout(knopfzeile)

        teiler = QSplitter(Qt.Vertical)
        teiler.addWidget(self.surface)
        teiler.addWidget(unten)
        teiler.setStretchFactor(0, 3)
        teiler.setStretchFactor(1, 2)

        lay = QVBoxLayout(self)
        lay.addWidget(self._legende())
        lay.addWidget(teiler, 1)

        self.surface.load(tool)
        self._enable(False)

    # ── Aufbau ──────────────────────────────────────────────────────────────

    def _legende(self) -> QWidget:
        w = QWidget()
        zeile = QHBoxLayout(w)
        zeile.setContentsMargins(6, 2, 6, 2)
        for farbe, text in ((FARBE_OK, "Sektor"), (FARBE_DEFEKT, "CRC-Fehler"),
                            (FARBE_GAP, "Gap"), (FARBE_UNFORMAT, "unformatiert")):
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
        self.btn_reload.setEnabled(an)
        self.btn_fixcrc.setEnabled(schreibbar)
        self.btn_save.setEnabled(schreibbar)
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

        spur = self.tool.track(cyl, head)
        span = next((s for s in spur.spans
                     if s.kind == SECTOR and s.index == index), None)
        kennung = f"Sektor {span.id}" if span else f"Sektor #{index}"
        verfahren = f"IBM-{spur.encoding}"
        marke = "  · Datenmarke F8 (gelöscht)" if span and span.deleted else ""
        id_crc = "" if not span or span.id_crc_ok else "  · ID-CRC FEHLERHAFT"
        self.info.setText(
            f"Seite: {head},  Spur: {cyl},  {kennung},  Format: {verfahren},  "
            f"Größe: {len(daten)} Byte{marke}{id_crc}")

        self.crc_feld.setText(f"{crc:04X}")
        self.hex.setPlainText(hexdump(daten))
        self._enable(True)
        self._crc_bewerten()
        return True

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

        try:
            self.tool.sector_write(cyl, head, index, daten, crc)
        except K1520DiskError as e:
            QMessageBox.critical(self, "Nicht geschrieben", str(e))
            return False

        self.surface.reload_track(self.tool, cyl, head)
        self.hinweis.setText(
            f"Sektor geschrieben — {len(daten)} Byte, CRC {crc:04X}. "
            "In die Datei kommt es mit „Speichern“ im Hauptfenster.")
        self.disk_changed.emit()
        self._crc_bewerten()
        return True
