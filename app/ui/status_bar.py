"""Die Statuszeile des Emulators — was dauerhaft gilt, nicht was gerade geschah.

Links in der Zeile stehen die flüchtigen Meldungen des Fensters
(``statusBar().showMessage``), rechts dieses Widget mit dem **Zustand der
Maschine**:

* **Takt** — der EINGESTELLTE Takt (``2,45 MHz``, ``10 × 2,45 MHz``,
  ``unbegrenzt``), wortgleich mit dem Auswahlfeld unter *Einstellungen ▸
  Allgemein*; die Stufen stehen in :mod:`app.takt`.  Hier stand eine Zeitlang
  die *gemessene* Geschwindigkeit — die schwankt von Sekunde zu Sekunde
  (``10,0×``, ``9,8×``, ``10×``) und liest sich wie ein Fehler, wo keiner ist.
  Sie ist nicht weg, sondern in den Tooltip gewandert: dort beantwortet sie auf
  Nachfrage, ob der Wirtsrechner mitkommt.  Zykluszähler und Bildrate standen
  hier früher und sind ersatzlos weg.
* **je Laufwerk eine Leuchte und ein Feld** — die Leuchte sagt den Zustand
  (leerer Ring = keine Diskette, schwarz = eingelegt, rot = Zugriff läuft), das
  Feld nennt die Abbilddatei und ob sie schreibgeschützt ist (``R/O``) oder
  nicht (``R/W``).  Ein leerer Steckplatz bekommt beides nicht.  Rot leuchtet
  auch ein LEERES Laufwerk, sobald es angesprochen wird — genau wie die Leuchte
  am echten Gerät, und genau das will man sehen, wenn ein Gastsystem auf eine
  Diskette wartet, die niemand eingelegt hat.
* **beim A5120.16 zwei weitere Leuchten V1/V2 + Modus** — nur sichtbar, wenn
  das Modell (*Einstellungen ▸ Allgemein*) ein Erweiterungsmodul hat.  **V1
  ist RAMEN, nicht der Paritätsfehler** (`doc/design/17_a5120_16.md` §S1/S6,
  Scan 9005/2), V2 ist der 8-Bit-Mode der Steuerkarte; ``Modus:`` zeigt, welche
  CPU gerade den Bus hat.  Abgefragt wird ``em_leds()``/``em_mode16()`` im
  selben Sekundentakt wie Takt und Laufwerke.

Der ganze Streifen ist eine Anzeige und kein Bedienelement: er nimmt keinen
Tastaturfokus (der gehört der emulierten Maschine, siehe `app/ui/focus.py`).
"""

from __future__ import annotations

import os
from typing import List, Optional

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QWidget

from app import drive_types as dt
from app import takt

#: Zustände der Laufwerksleuchte.
LEER = "leer"          #: keine Diskette, kein Zugriff — nur der Umriss
BELEGT = "belegt"      #: Diskette liegt im Laufwerk
ZUGRIFF = "zugriff"    #: das Laufwerk ist angesprochen (auch ohne Diskette)

#: Farbe des laufenden Zugriffs (dieselbe rote Leuchtfarbe wie im Laufwerkskasten).
FARBE_ZUGRIFF = "#d0342c"


def _trennstrich() -> QFrame:
    strich = QFrame()
    strich.setFrameShape(QFrame.VLine)
    strich.setFrameShadow(QFrame.Sunken)
    return strich


class DriveLamp(QWidget):
    """Die Leuchte vor einem Laufwerksfeld — ein Ring, gefüllt oder nicht.

    Gezeichnet, nicht getippt: ein Emoji-Kreis (``○``/``●``) sieht in vielen
    Schriften fast gleich aus, und dann sagt die Anzeige nichts.  Die Farbe des
    Umrisses kommt aus der Palette, damit sie im hellen wie im dunklen Thema
    steht.
    """

    KANTE = 12

    def __init__(self, parent=None):
        super().__init__(parent)
        self._zustand = LEER
        self.setFixedSize(QSize(self.KANTE + 4, self.KANTE + 4))
        self.setFocusPolicy(Qt.NoFocus)

    def zustand(self) -> str:
        return self._zustand

    def set_zustand(self, zustand: str) -> None:
        """Nur bei echter Änderung neu zeichnen — das läuft im Sekundentakt."""
        if zustand == self._zustand:
            return
        self._zustand = zustand
        self.setToolTip({
            LEER: "Keine Diskette eingelegt",
            BELEGT: "Diskette eingelegt",
            ZUGRIFF: "Das Laufwerk ist angesprochen — liegt keine Diskette "
                     "darin, wartet das Gastsystem vergeblich.",
        }.get(zustand, ""))
        self.update()

    def paintEvent(self, event):
        malen = QPainter(self)
        malen.setRenderHint(QPainter.Antialiasing)
        rand = self.palette().color(self.foregroundRole())
        kreis = self.rect().adjusted(2, 2, -2, -2)
        if self._zustand == LEER:
            malen.setPen(QPen(rand, 1.4))
            malen.setBrush(Qt.NoBrush)
        else:
            fuellung = QColor(FARBE_ZUGRIFF) if self._zustand == ZUGRIFF else rand
            malen.setPen(QPen(fuellung, 1.0))
            malen.setBrush(fuellung)
        malen.drawEllipse(kreis)
        malen.end()


class EmLamp(QWidget):
    """Eine LED der A5120.16-Steuerkarte (V1/V2) — gefüllt bei „an", sonst der Umriss.

    Dieselbe Zeichnung wie :class:`DriveLamp`, aber nur zwei Zustände: eine
    EM-Leuchte kennt keinen „Zugriff", sie zeigt einen Pegel.
    """

    KANTE = 10

    def __init__(self, parent=None):
        super().__init__(parent)
        self._an = False
        self.setFixedSize(QSize(self.KANTE + 4, self.KANTE + 4))
        self.setFocusPolicy(Qt.NoFocus)

    def set_an(self, an: bool) -> None:
        an = bool(an)
        if an == self._an:
            return
        self._an = an
        self.update()

    def paintEvent(self, event):
        malen = QPainter(self)
        malen.setRenderHint(QPainter.Antialiasing)
        rand = self.palette().color(self.foregroundRole())
        kreis = self.rect().adjusted(2, 2, -2, -2)
        if self._an:
            malen.setPen(QPen(rand, 1.0))
            malen.setBrush(rand)
        else:
            malen.setPen(QPen(rand, 1.4))
            malen.setBrush(Qt.NoBrush)
        malen.drawEllipse(kreis)
        malen.end()


class DriveField(QLabel):
    """Ein Laufwerk: ``A: clock.hfe  R/W``.

    Der volle Pfad steht im Tooltip — in die Zeile passt er nicht, und der
    Dateiname ist das, woran man die Diskette wiedererkennt.
    """

    def __init__(self, drive: int, parent=None):
        super().__init__(parent)
        self.drive = drive
        self.setMargin(2)
        self.zeige(path="", wp=False)

    def zeige(self, path: str, wp: bool, physisch: bool = False,
              vorhanden: bool = True) -> None:
        buchstabe = chr(ord("A") + self.drive)
        if not vorhanden:
            self.setText(f"{buchstabe}: —")
            self.setToolTip(f"Steckplatz {self.drive} ist nicht bestückt")
            return
        if physisch:
            # Eine echte Diskette hat keinen Dateinamen; sie ist trotzdem
            # eingelegt, und der Schreibschutz gilt für sie erst recht.
            name, tip = "⟨echte Diskette⟩", "Echtes Laufwerk am Greaseweazle"
        elif path:
            name, tip = os.path.basename(path), path
        else:
            self.setText(f"{buchstabe}: leer")
            self.setToolTip(f"Laufwerk {buchstabe}: — keine Diskette eingelegt")
            return
        schutz = "R/O" if wp else "R/W"
        self.setText(f"{buchstabe}: {name}  {schutz}")
        self.setToolTip(
            f"Laufwerk {buchstabe}: {tip}\n"
            + ("Schreibgeschützt — die Maschine kann nicht darauf schreiben."
               if wp else "Beschreibbar — Änderungen gehen in die Datei zurück."))


class MachineStatus(QWidget):
    """Der dauerhafte Teil der Statuszeile (Takt + Laufwerke)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.NoFocus)

        self.takt = QLabel()
        self.takt.setMargin(2)
        self._faktor: Optional[float] = None
        self._gemessen: Optional[float] = None
        self.set_takt(None)

        self._lay = QHBoxLayout(self)
        self._lay.setContentsMargins(0, 0, 0, 0)
        self._lay.setSpacing(4)
        self._lay.addWidget(self.takt)

        # A5120.16-Leuchten (V1/V2) + Modus — feste Stelle zwischen Takt und
        # Laufwerken, standardmässig ausgeblendet (nur der A5120 ohne
        # Erweiterung).  EIGENE Widget-Liste, damit `set_drive_types` (das
        # alles nach dem Takt abräumt und neu aufbaut) sie nicht mit wegräumt.
        self._em_strich = _trennstrich()
        self._em_v1_label = QLabel("V1")
        self._em_v1_label.setMargin(2)
        self._em_v1 = EmLamp()
        self._em_v2_label = QLabel("V2")
        self._em_v2_label.setMargin(2)
        self._em_v2 = EmLamp()
        self._em_modus = QLabel()
        self._em_modus.setMargin(2)
        self._em_widgets: List[QWidget] = [
            self._em_strich, self._em_v1_label, self._em_v1,
            self._em_v2_label, self._em_v2, self._em_modus]
        for w in self._em_widgets:
            self._lay.addWidget(w)
        v1_tipp = ("V1 — RAMEN (Steuerkarte 062-9005): das Erweiterungsmodul "
                   "hat den Speicher eingeblendet.  NICHT der Paritätsfehler.")
        v2_tipp = "V2 — 8-Bit-Mode: die Steuerkarte fährt den U880-Bus."
        modus_tipp = "Welche CPU gerade den Bus hat: U880 (8-Bit) oder U8001 (16-Bit)."
        self._em_v1_label.setToolTip(v1_tipp)
        self._em_v1.setToolTip(v1_tipp)
        self._em_v2_label.setToolTip(v2_tipp)
        self._em_v2.setToolTip(v2_tipp)
        self._em_modus.setToolTip(modus_tipp)
        self.set_em_sichtbar(False)

        self._felder: List[DriveField] = []
        self._lampen: List[DriveLamp] = []
        self.set_drive_types(dt.DEFAULT_DRIVE_TYPES)

    # ── Takt ─────────────────────────────────────────────────────────────────

    def set_takt(self, faktor: Optional[float],
                 gemessen: Optional[float] = None) -> None:
        """Den EINGESTELLTEN Takt anzeigen; ``None`` = die Maschine steht.

        *gemessen* ist das tatsächlich erreichte Verhältnis zum Nenntakt (1,0 =
        Echtzeit).  Es steht nur im Tooltip: als Anzeige schwankte es im
        Sekundentakt, als Nachfrage beantwortet es genau eine Frage — kommt der
        Wirtsrechner mit?
        """
        self._faktor, self._gemessen = faktor, gemessen
        if faktor is None:
            self.takt.setText("Takt: —")
            self.takt.setToolTip("Die Maschine läuft nicht.")
            return
        self.takt.setText(f"Takt: {takt.beschriftung(faktor)}")

        tipp = [f"Eingestellt unter Einstellungen ▸ Allgemein.  Der A5120 läuft "
                f"mit {takt.NENNTAKT_TEXT}."]
        if gemessen is not None:
            wert = f"{gemessen:.1f}".replace(".", ",")
            tipp.append(f"Gemessen: {wert} × {takt.NENNTAKT_TEXT}.")
            if faktor > 0.0 and gemessen < faktor * 0.9:
                tipp.append("Der Wirtsrechner kommt nicht mit.")
        self.takt.setToolTip("\n".join(tipp))

    # ── A5120.16: EM-Leuchten V1/V2 + Modus ────────────────────────────────────

    def set_em_sichtbar(self, sichtbar: bool) -> None:
        """Die EM-Anzeige ein-/ausblenden — nur beim A5120.16 bestückt."""
        self._em_sichtbar_wert = bool(sichtbar)
        for w in self._em_widgets:
            w.setVisible(sichtbar)

    def em_sichtbar(self) -> bool:
        """True, solange die EM-Anzeige eingeblendet ist (für Tests).

        Ein eigenes Merkfeld statt ``isVisible()``: das hängt zusätzlich davon
        ab, ob das ganze Fenster schon angezeigt wurde (in Tests nicht immer
        der Fall) und würde dort fälschlich ``False`` melden.
        """
        return getattr(self, "_em_sichtbar_wert", False)

    def set_em(self, v1: bool, v2: bool, mode16: bool) -> None:
        """Die beiden Leuchten und den Modus (8/16-Bit) nachführen."""
        self._em_v1.set_an(v1)
        self._em_v2.set_an(v2)
        self._em_modus.setText("Modus: 16-Bit" if mode16 else "Modus: 8-Bit")

    # ── Laufwerke ────────────────────────────────────────────────────────────

    def set_drive_types(self, drive_types) -> None:
        """Felder neu aufbauen — je bestücktem K5122-Steckplatz eines."""
        # Alles ausser Taktfeld UND EM-Anzeige abräumen — auch die Dehnfuge am
        # Ende, sonst sammeln sich bei jedem Laufwerkswechsel weitere an.  Die
        # EM-Widgets stehen an fester Stelle direkt danach (siehe __init__) und
        # bleiben hier unangetastet, sichtbar oder nicht.
        ab = 1 + len(self._em_widgets)
        while self._lay.count() > ab:
            eintrag = self._lay.takeAt(ab)
            w = eintrag.widget()
            if w is not None:
                w.setParent(None)
                w.deleteLater()
        self._felder = []
        self._lampen = []

        for drive, typ in enumerate(dt.normalize_list(drive_types)):
            if not dt.is_present(typ):
                continue
            self._lay.addWidget(_trennstrich())
            lampe = DriveLamp()
            feld = DriveField(drive)
            self._lay.addWidget(lampe)
            self._lay.addWidget(feld)
            self._lampen.append(lampe)
            self._felder.append(feld)

    def felder(self) -> List[DriveField]:
        """Die Laufwerksfelder in Steckplatzreihenfolge (für Tests und Abfragen)."""
        return list(self._felder)

    def lampe(self, drive: int) -> Optional[DriveLamp]:
        """Die Leuchte des Laufwerks *drive* (``None``, wenn nicht bestückt)."""
        for feld, lampe in zip(self._felder, self._lampen):
            if feld.drive == drive:
                return lampe
        return None
