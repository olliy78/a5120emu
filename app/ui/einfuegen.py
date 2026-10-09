"""Text aus der Zwischenablage ZEICHENWEISE über die Tastatur eingeben (Rechtsklick auf das Bild).

Der Gast sieht Tastendrücke, keine Zeichenkette: ein Zeichen, für das es keine Taste gibt, wird
**übersprungen** (nicht ersetzt, nicht abgebrochen).  Getaktet wird in **Wirtszeit** mit
:data:`ZEICHEN_PRO_SEKUNDE` — schnell genug für ein Skript, langsam genug für die serielle
Tastatur (K7637: 9600 Baud, K7673/K7672: eigene Firmware) und den Gast, der zwischen zwei Tasten
noch rechnen muss.  Ein Zeichen bleibt bis zum nächsten Takt gedrückt; Druck und Loslassen sind
also immer durch mindestens einen Takt getrennt (sonst sähe die Tastenabfrage nur eine Flanke).

Die Maschine liefert nur zwei Handgriffe — :meth:`druecken` (``False`` = keine Taste, übersprungen)
und :meth:`loslassen`; wie das Zeichen zur Taste wird, steht dort (``ScreenWidget`` über die
Bildschirmtastatur, ``OriginalTerminalWidget`` über die K7673).
"""

from __future__ import annotations

from typing import Callable

from PySide6.QtCore import QElapsedTimer, QObject, QTimer, Signal

#: Eingaberate in Zeichen je Sekunde (Wirtszeit).
ZEICHEN_PRO_SEKUNDE = 120
#: Mehr als so viele Zeichen holt ein Takt nie nach (ein träger Wirtsrechner soll keinen Stoss
#: erzeugen — „ohne Tastatur und Computer zu überfordern").
MAX_NACHHOLEN = 2


def tippbare_zeichen(text: str) -> str:
    """Zeilenenden vereinheitlichen: ``\\r\\n`` und ``\\r`` werden ``\\n`` (eine Return-Taste)."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


class TastenEinfueger(QObject):
    """Gibt einen Text mit fester Rate über ``druecken``/``loslassen`` ein."""

    #: (eingegeben, übersprungen) — am Ende oder nach dem Abbruch.
    fertig = Signal(int, int)

    def __init__(self, druecken: Callable[[str], bool], loslassen: Callable[[], None],
                 parent=None, rate: int = ZEICHEN_PRO_SEKUNDE):
        super().__init__(parent)
        self._druecken, self._loslassen = druecken, loslassen
        self._text = ""
        self._pos = 0
        self._gedrueckt = False
        self.eingegeben = 0
        self.uebersprungen = 0
        self._rate = rate
        self._uhr = QElapsedTimer()
        self._soll = 0.0                       # so viele Zeichen sind nach der Uhr fällig
        self._timer = QTimer(self)
        self._timer.setInterval(max(1, round(1000 / rate)))
        self._timer.timeout.connect(self._takt)

    def laeuft(self) -> bool:
        return self._timer.isActive()

    def starten(self, text: str):
        """Neuen Text eingeben; ein noch laufender wird zuvor abgebrochen."""
        self.abbrechen()
        self._text = tippbare_zeichen(text)
        self._pos = 0
        self.eingegeben = self.uebersprungen = 0
        if not self._text:
            return
        self._uhr.start()
        self._soll = 0.0
        self._timer.start()
        self._takt()                           # das erste Zeichen sofort

    def abbrechen(self):
        if not self.laeuft() and not self._gedrueckt:
            return
        self._ende()

    def _takt(self):
        # Fällige Zeichen nach der Uhr (Taktgenauigkeit der Qt-Timer ist grob), höchstens
        # MAX_NACHHOLEN je Takt.  Das vorige Zeichen wird zuerst losgelassen.
        faellig = int(self._uhr.elapsed() * self._rate / 1000) + 1 - (self.eingegeben + self.uebersprungen)
        faellig = max(1, min(faellig, MAX_NACHHOLEN))
        for _ in range(faellig):
            if self._gedrueckt:
                self._loslassen()
                self._gedrueckt = False
            if self._pos >= len(self._text):
                self._ende()
                return
            c = self._text[self._pos]
            self._pos += 1
            if self._druecken(c):
                self._gedrueckt = True
                self.eingegeben += 1
            else:
                self.uebersprungen += 1
            if self._gedrueckt and faellig > 1:
                # Mehrere Zeichen in einem Takt: jedes für sich drücken UND loslassen.
                self._loslassen()
                self._gedrueckt = False
        if self._pos >= len(self._text) and not self._gedrueckt:
            self._ende()

    def _ende(self):
        self._timer.stop()
        if self._gedrueckt:
            self._loslassen()
            self._gedrueckt = False
        self._text, self._pos = "", 0
        self.fertig.emit(self.eingegeben, self.uebersprungen)
