"""Mitschrift des Terminals: alles, was der Rechner dem Terminal sendet, als Text in eine Datei.

Eingabe sind die **Rohbytes** der Leitung (``K1520Emulator.term_log_read``).  Geschrieben wird, was
ein Mensch am Bildschirm als Text lesen würde: Zeilenenden als ``\\n``, Tabulatoren, druckbare
Zeichen; **Escape-Folgen** (Cursor setzen, Löschen, Attribute …) und sonstige Steuerzeichen fallen
weg, ein Rückschritt nimmt das letzte noch nicht geschriebene Zeichen der Zeile zurück.  Weil
Eingaben vom Rechner zurückgespiegelt werden (Echo), steht damit auch die Eingabe in der Datei
— Passwörter nicht, die werden nicht gespiegelt.

* **Angehängt, nie überschrieben**: die Datei wird je Schreibvorgang im Anhängemodus geöffnet; eine
  vorhandene behält ihren Inhalt.  Ein Schreibfehler (Platte voll, Datei gesperrt) stoppt die
  Mitschrift und wird gemeldet (:attr:`fehler`) — er darf den Emulator nicht stören.
* **Eine angefangene Zeile** (Eingabeaufforderung ohne Zeilenende) wird nach :data:`LEERLAUF_S`
  Sekunden ohne neues Zeichen herausgeschrieben, damit ein Prompt nicht bis zur Eingabe fehlt.
  Danach folgende Rückschritte kommen zu spät und werden verworfen.
* Der Zustand der Escape-Auswertung bleibt über Lesevorgänge erhalten (eine Folge darf zwischen
  zwei Abholungen zerreissen).
* Zeichensatz: Latin-1 → UTF-8 (das Terminal sendet 7-Bit-ASCII bzw. ISO-646-Varianten; eine
  Umsetzung der nationalen Zeichen findet nicht statt).
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Optional

#: So lange ohne neues Zeichen, dann wird die angefangene Zeile herausgeschrieben.
LEERLAUF_S = 1.0

_ESC = 0x1B


class Mitschrift:
    """Wandelt Rohbytes in Text und hängt ihn an eine Datei an."""

    def __init__(self, pfad: str = "", uhr=time.monotonic):
        self._uhr = uhr
        self._pfad: Optional[Path] = None
        self._zeile: list = []                 # angefangene Zeile (noch nicht geschrieben)
        self._esc = None                       # None | ("esc",) | ("csi",) | ("rest", n)
        self._zuletzt = 0.0
        self.fehler = ""
        self.geschrieben = 0                   # Zeichen, die in der Datei gelandet sind (Tests)
        self.setze_pfad(pfad)

    # ── Ziel ─────────────────────────────────────────────────────────────────

    @property
    def pfad(self) -> str:
        return str(self._pfad) if self._pfad else ""

    @property
    def aktiv(self) -> bool:
        return self._pfad is not None and not self.fehler

    def setze_pfad(self, pfad: str) -> bool:
        """Ziel wählen (leer = Mitschrift aus).  Die Datei wird geprüft: anlegbar/beschreibbar?

        Eine angefangene Zeile geht noch in die BISHERIGE Datei.  Rückgabe: bereit.
        """
        self.abschliessen()
        self.fehler = ""
        self._esc = None
        pfad = (pfad or "").strip()
        if not pfad:
            self._pfad = None
            return False
        p = Path(pfad).expanduser()
        try:
            with open(p, "a", encoding="utf-8"):     # legt an, falls neu; ändert Vorhandenes nicht
                pass
        except OSError as e:
            self._pfad = None
            self.fehler = f"{p}: {e.strerror or e}"
            return False
        self._pfad = p
        return True

    # ── Eingabe ──────────────────────────────────────────────────────────────

    def schreibe(self, daten: bytes) -> None:
        """Rohbytes der Leitung verarbeiten; fertige Zeilen gehen sofort in die Datei."""
        if self._pfad is None or not daten:
            return
        fertig = []
        for b in daten:
            self._byte(b, fertig)
        self._zuletzt = self._uhr()
        if fertig:
            self._raus("".join(fertig))

    def takt(self) -> None:
        """Regelmässig rufen: schreibt eine angefangene Zeile nach :data:`LEERLAUF_S` heraus."""
        if (self._zeile and self._pfad is not None
                and self._uhr() - self._zuletzt >= LEERLAUF_S):
            text = "".join(self._zeile)
            self._zeile.clear()
            self._raus(text)

    def abschliessen(self) -> None:
        """Eine angefangene Zeile herausschreiben (Beenden, Zielwechsel, Ausschalten)."""
        if self._zeile and self._pfad is not None:
            text = "".join(self._zeile)
            self._zeile.clear()
            self._raus(text)
        self._zeile.clear()

    # ── Innenleben ───────────────────────────────────────────────────────────

    def _byte(self, b: int, fertig: list) -> None:
        e = self._esc
        if e is not None:
            kind = e[0]
            if kind == "esc":                       # das Byte direkt nach ESC
                if b == ord("["):
                    self._esc = ("csi",)
                elif b == ord("="):                 # ADM31: Cursor setzen, 2 Parameterbytes
                    self._esc = ("rest", 2)
                elif b in (ord("("), ord(")"), ord("#"), ord("*"), ord("+")):
                    self._esc = ("rest", 1)         # Zeichensatzwahl o. ä.: ein Byte dahinter
                else:
                    self._esc = None                # ESC + ein Zeichen (ADM31-Befehl) — fertig
                return
            if kind == "csi":                       # ESC [ … bis zum Endbyte 40H–7EH
                if 0x40 <= b <= 0x7E:
                    self._esc = None
                return
            if kind == "rest":
                n = e[1] - 1
                self._esc = ("rest", n) if n > 0 else None
                return
        if b == _ESC:
            self._esc = ("esc",)
        elif b == 0x0A:
            fertig.append("".join(self._zeile) + "\n")
            self._zeile.clear()
        elif b in (0x08, 0x7F):                     # Rückschritt/DEL: letztes Zeichen der Zeile
            if self._zeile:
                self._zeile.pop()
        elif b == 0x09 or b >= 0x20:
            if b != 0x7F:
                self._zeile.append(chr(b))
        # alles andere (CR, BEL, übrige Steuerzeichen) fällt weg

    def _raus(self, text: str) -> None:
        if not text or self._pfad is None:
            return
        try:
            with open(self._pfad, "a", encoding="utf-8", newline="") as f:
                f.write(text)
            self.geschrieben += len(text)
        except OSError as e:
            self.fehler = f"{self._pfad}: {e.strerror or e}"
            self._pfad = None
