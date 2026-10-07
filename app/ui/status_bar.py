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

* **K8915: die sechs Lampen der Frontplatte** (nur im Programmprofil mit
  ``frontplatte``, `app/profil.py`) — in Reihenfolge und Farbe des Geräts
  (doc/design/16_k8915.md §3.6): **Run** grün, **Input File**, **Output File**,
  **RUN Mode** gelb, **ERROR** und **Power** rot.  Die vier mittleren sind das
  Anzeigelatch 61H (``k1520_panel_lamps``, aktiv low: Bit 4/5/6/7).  **Run** und
  **Power** hängen nicht am Latch: Power leuchtet, solange der Rechner
  eingeschaltet ist; Run hängt am Gerät vermutlich an ``/HALT`` **[?]** und
  leuchtet hier, solange die Emulation läuft.  Jede Lampe trägt ihre
  Beschriftung daneben (``Run Input Output Mode Error Power``, AP-UI2) — drei
  gelbe Lampen unterscheidet man an der Farbe nicht.

* **Serielle Schnittstellen** (AP-S7, doc/design/19_serielle_schnittstellen.md §9) —
  zwei Felder hinter dem Takt, die **nur erscheinen, wenn sie etwas zu sagen haben**
  (ausgeblendet, nicht leer): ``Telnet/RFC2217 Server Port: 5000, 5001`` (die
  tatsächlichen Ports der lauschenden/verbundenen Server) und ``V.24 verbunden,
  Drucker verbunden`` (nur VERBUNDEN; ein versuchender Client erscheint hier nie,
  Datei auch nicht).  Der Text kommt fertig aus `app/ui/serial_widget.py`
  (:meth:`MachineStatus.set_seriell`); dieses Modul kennt keine Schnittstellennamen.

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
from app import profil as profile
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


# ── Frontplatte des K8915 ─────────────────────────────────────────────────

#: Farben der Lampen (Gerät, doc/design/16_k8915.md §3.6).
FARBE_GRUEN = "#35c43a"
FARBE_GELB = "#f2c230"
FARBE_ROT = "#e0352b"

#: (Name, Farbe, Latch-Bit oder None, Bedeutung) in Gerätereihenfolge, oben → unten
#: an der Frontplatte, hier links → rechts.  Bit = None: nicht am Latch 61H.
#: Neben jeder Lampe steht ihre Beschriftung (:data:`BESCHRIFTUNG`) — an der
#: Farbe allein erkennt man nicht, welche der drei gelben gerade leuchtet.
FRONTPLATTE = (
    ("Run", FARBE_GRUEN, None,
     "Rechner läuft.  Am Gerät vermutlich /HALT der CPU [?] — hier: leuchtet, "
     "solange die Emulation läuft."),
    ("Input File", FARBE_GELB, 4,
     "Diskette wird gelesen (Anzeigelatch 61H Bit 4)."),
    ("Output File", FARBE_GELB, 5,
     "Diskette wird beschrieben (Anzeigelatch 61H Bit 5)."),
    ("RUN Mode", FARBE_GELB, 6,
     "System bereit — erlischt während eines Diskettenzugriffs (61H Bit 6)."),
    ("ERROR", FARBE_ROT, 7,
     "Fehler: Selbsttest, Lese- oder Schreibfehler (61H Bit 7)."),
    ("Power", FARBE_ROT, None,
     "Netzanzeige — leuchtet, solange der Rechner eingeschaltet ist."),
)


#: Frontplatte des P8000 (``k1520_panel_lamps``, AKTIV HIGH): Bit 0 = RUN-LED der 16-Bit-Karte,
#: Bit 1 = UNIT16 (der U8001 läuft, nicht im Reset), Bit 2 = Plattenzugriff am WDC.  Ohne
#: 16-Bit-Karte liefert der Kern 0 — dann bleiben Run/Unit16/Platte dunkel, ehrlich.
FRONTPLATTE_P8000 = (
    ("Run", FARBE_GRUEN, 0,
     "RUN-LED der 16-Bit-Karte (K14): leuchtet, solange der U8001 Befehle ausführt."),
    ("Unit16", FARBE_GELB, 1,
     "UNIT16: die 16-Bit-Karte ist aus dem Reset genommen (der U8001 läuft, WEGA-Betrieb)."),
    ("Platte", FARBE_GELB, 2,
     "Winchester: Zugriff des WDC auf die Platte in den letzten 0,1 s."),
    ("Power", FARBE_ROT, None,
     "Netzanzeige — leuchtet, solange der Rechner eingeschaltet ist."),
)

#: Lampenreihen je Frontplattenart: (Lampen, aktiv low?).
FRONTPLATTEN = {"k8915": (FRONTPLATTE, True), "p8000": (FRONTPLATTE_P8000, False)}

#: Beschriftung neben der Lampe — kurz, damit die Statuszeile nicht überläuft,
#: aber eindeutig; der volle Name steht im Tooltip.
BESCHRIFTUNG = {"Run": "Run", "Input File": "Input", "Output File": "Output",
                "RUN Mode": "Mode", "ERROR": "Error", "Power": "Power",
                "Unit16": "16-Bit", "Platte": "Platte"}


class PanelLamp(QWidget):
    """Eine Lampe der Frontplatte: farbig an, dunkel (dieselbe Farbe, gedämpft) aus.

    Gezeichnet wie die Laufwerksleuchte — ein Emoji-Kreis sähe je nach Schrift
    anders aus.  Der Tooltip nennt Name, Bedeutung und Zustand.
    """

    KANTE = 10

    def __init__(self, name: str, farbe: str, bedeutung: str, parent=None):
        super().__init__(parent)
        self.name, self.farbe, self.bedeutung = name, QColor(farbe), bedeutung
        self._an = False
        self.setFixedSize(QSize(self.KANTE + 4, self.KANTE + 4))
        self.setFocusPolicy(Qt.NoFocus)
        self._tooltip()

    def an(self) -> bool:
        return self._an

    def set_an(self, an: bool) -> None:
        """Nur bei echter Änderung neu zeichnen — läuft im 120-ms-Takt."""
        an = bool(an)
        if an == self._an:
            return
        self._an = an
        self._tooltip()
        self.update()

    def _tooltip(self):
        self.setToolTip(f"{self.name}: {'an' if self._an else 'aus'}\n{self.bedeutung}")

    def paintEvent(self, event):
        malen = QPainter(self)
        malen.setRenderHint(QPainter.Antialiasing)
        kreis = self.rect().adjusted(2, 2, -2, -2)
        fuellung = self.farbe if self._an else self.farbe.darker(330)
        malen.setPen(QPen(self.farbe.darker(160), 1.0))
        malen.setBrush(fuellung)
        malen.drawEllipse(kreis)
        malen.end()


class Frontplatte(QWidget):
    """Die sechs Lampen des K8915 nebeneinander, jede mit ihrer Beschriftung
    (Quelle: :meth:`zeige`)."""

    def __init__(self, parent=None, art: str = "k8915"):
        super().__init__(parent)
        self.setFocusPolicy(Qt.NoFocus)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(2, 0, 2, 0)
        lay.setSpacing(2)
        self._lampen: List[PanelLamp] = []
        self._schilder: List[QLabel] = []
        self._bits = []
        self._spec, self._aktiv_low = FRONTPLATTEN[art]
        for i, (name, farbe, bit, bedeutung) in enumerate(self._spec):
            if i:
                lay.addSpacing(6)
            lampe = PanelLamp(name, farbe, bedeutung)
            schild = QLabel(BESCHRIFTUNG[name])
            schild.setToolTip(f"{name}\n{bedeutung}")
            schild.setFocusPolicy(Qt.NoFocus)
            lay.addWidget(lampe)
            lay.addWidget(schild)
            self._lampen.append(lampe)
            self._schilder.append(schild)
            self._bits.append(bit)

    def lampen(self) -> List[PanelLamp]:
        """Die Lampen in Gerätereihenfolge (Run … Power)."""
        return list(self._lampen)

    def beschriftungen(self) -> List[str]:
        """Die Beschriftungen neben den Lampen, in Gerätereihenfolge."""
        return [s.text() for s in self._schilder]

    def schild(self, name: str) -> QLabel:
        """Das Schild neben der Lampe *name* (für Tests: Lage, Tooltip)."""
        return self._schilder[[l.name for l in self._lampen].index(name)]

    def zeige(self, latch: int, laeuft: bool, eingeschaltet: bool) -> None:
        """*latch* = Rohbyte von ``k1520_panel_lamps`` (K8915: aktiv low, P8000: aktiv high)."""
        for lampe, bit, (name, *_rest) in zip(self._lampen, self._bits, self._spec):
            if bit is None:
                lampe.set_an(eingeschaltet if name == "Power" else laeuft)
            else:
                gesetzt = bool((int(latch) >> bit) & 1)
                lampe.set_an(eingeschaltet and (gesetzt != self._aktiv_low))

    def zustand(self) -> dict:
        """{Name: an?} — für Tests und Abfragen."""
        return {l.name: l.an() for l in self._lampen}


class SeriellFeld(QWidget):
    """Ein Feld der seriellen Schnittstellen: Trennstrich + Text, beides ausgeblendet,
    solange es nichts zu sagen gibt."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.NoFocus)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        lay.addWidget(_trennstrich())
        self.text = QLabel()
        self.text.setMargin(2)
        lay.addWidget(self.text)
        self.setVisible(False)

    def zeige(self, text: str, tipp: str = "") -> None:
        if text != self.text.text():
            self.text.setText(text)
        if tipp != self.text.toolTip():
            self.text.setToolTip(tipp)
        self.setVisible(bool(text))


class MachineStatus(QWidget):
    """Der dauerhafte Teil der Statuszeile (Takt + Laufwerke, K8915: Frontplatte)."""

    def __init__(self, parent=None, profil=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.NoFocus)
        self.profil = profil or profile.VORGABE
        #: Nenntakt des laufenden Modells (Modellwechsel: `set_nenntakt`).
        self.nenntakt_text = self.profil.nenntakt_text

        self.takt = QLabel()
        self.takt.setMargin(2)
        self._faktor: Optional[float] = None
        self._gemessen: Optional[float] = None
        self.set_takt(None)

        self._lay = QHBoxLayout(self)
        self._lay.setContentsMargins(0, 0, 0, 0)
        self._lay.setSpacing(4)
        # Feste Einträge vorn (K8915: Frontplatte + Strich, dann der Takt);
        # set_drive_types räumt nur, was dahinter kommt.
        self.frontplatte: Optional[Frontplatte] = None
        if self.profil.frontplatte:
            self.frontplatte = Frontplatte(art=self.profil.frontplatte_art)
            self._lay.addWidget(self.frontplatte)
            self._lay.addWidget(_trennstrich())
        self._lay.addWidget(self.takt)
        # Serielle Schnittstellen: hinter dem Takt, vor den Laufwerken (feste
        # Einträge — `set_drive_types` räumt nur, was dahinter kommt).
        self.seriell_server = SeriellFeld()
        self.seriell_verbindungen = SeriellFeld()
        self._lay.addWidget(self.seriell_server)
        self._lay.addWidget(self.seriell_verbindungen)
        self._fest = self._lay.count()

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
        self.set_drive_types(self.profil.standard_laufwerke())

    # ── Takt ─────────────────────────────────────────────────────────────────

    def set_nenntakt(self, text: str) -> None:
        """Nenntakt des Modells (PC 1715W: 3,9936 MHz) und Anzeige nachziehen."""
        self.nenntakt_text = text
        self.set_takt(self._faktor, self._gemessen)

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
        self.takt.setText(f"Takt: {takt.beschriftung(faktor, self.nenntakt_text)}")

        nenntakt = self.nenntakt_text
        tipp = [f"Eingestellt unter Einstellungen ▸ Allgemein.  Der "
                f"{self.profil.rechner} läuft mit {nenntakt}."]
        if gemessen is not None:
            wert = f"{gemessen:.1f}".replace(".", ",")
            tipp.append(f"Gemessen: {wert} × {nenntakt}.")
            if faktor > 0.0 and gemessen < faktor * 0.9:
                tipp.append("Der Wirtsrechner kommt nicht mit.")
        self.takt.setToolTip("\n".join(tipp))

    # ── Serielle Schnittstellen ──────────────────────────────────────────────

    def set_seriell(self, server: str, server_tipp: str,
                    verbindungen: str, verbindungen_tipp: str) -> None:
        """Die beiden Felder setzen; ein leerer Text blendet das Feld aus."""
        self.seriell_server.zeige(server, server_tipp)
        self.seriell_verbindungen.zeige(verbindungen, verbindungen_tipp)

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
        # Alles ausser den festen Feldern (Frontplatte, Takt, serielle Felder) UND
        # der EM-Anzeige abräumen — auch die Dehnfuge am Ende, sonst sammeln sich
        # bei jedem Laufwerkswechsel weitere an.  Die EM-Widgets stehen an fester
        # Stelle direkt danach (siehe __init__) und bleiben hier unangetastet,
        # sichtbar oder nicht.
        ab = self._fest + len(self._em_widgets)
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
