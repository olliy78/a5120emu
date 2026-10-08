"""Hauptspeicher-Ausbau des P8000 (16-Bit-Seite) — Auswahl, Prüfung und Umzug alter Werte.

Befund: ``doc/p8000/ram_konfiguration.md``.  Der Kern liest denselben Text
(``k1520_create_p8000``, Schlüssel ``dram``) über ``P8000Dram16::parse``; diese Datei
prüft nur so weit, dass die Oberfläche keinen Wert anbietet, den der Kern abweist.

Konfigurationsschlüssel bleibt ``general.dram`` (Programmkonfiguration ``p8000emu.yaml``):
alte Werte aus der ersten Auswahl (``1M@0``, ``256K@0``, ``1M@0+1M@1``) werden auf die
Kurzform mit DERSELBEN Bestückung umgezogen (nicht auf die neue Vorgabe — sonst lüde ein
alter Zwischenstand nicht mehr), jede andere gültige Langform bleibt als
„benutzerdefiniert“ stehen.
"""

from __future__ import annotations

import re

#: Vorgabe des Programms: Standardausbau des Geräts (vier 256-KB-Karten = 1 MB).
STANDARD = "4x256K"

#: Auswahl im Einstellungsdialog: (Wert, Anzeige).  Der erste Eintrag ist die Vorgabe.
AUSBAUTEN = (
    ("4x256K", "4 × 256 KB = 1 MB (Standard)"),
    ("2x256K", "2 × 256 KB = 512 KB"),
    ("1x1M", "1 × 1 MB = 1 MB (eine Karte Index 3)"),
    ("4x1M", "4 × 1 MB = 4 MB"),
    ("16M", "RAM-Karte 16 MB (2009) — WEGA nutzt knapp 8 MB"),
    ("8M", "RAM-Karte mit 8 MB bestückt"),
)

TIPP = (
    "Hauptspeicher-Karten am 16-Bit-Speicherbus (höchstens vier Steckplätze).  "
    "Ausgeliefert wurde der P8000 mit vier 256-KB-Karten (1 MB); die DRAM-Karte Index 3 "
    "gab es auch mit 1 MB (vier Karten = 4 MB).  Die RAM-Karte 16 MB (Eigenbau-Kleinserie "
    "2009, robotrontechnik.de) liegt immer ab Adresse 0; WEGA nutzt davon knapp 8 MB "
    "(8 MB − 64 KB): ohne MMU erreicht der U8001 nur 128 Segmente × 64 KB, und Segment 7FH "
    "ist auf der Karte gesperrt (Umgehung eines Monitorfehlers).  Der Monitor meldet den "
    "Ausbau als MAXSEG (0F = 1 MB, 3F = 4 MB, 7E = 16-MB-Karte).  Andere Bestückungen: "
    "Schlüssel dram in p8000emu.yaml, z. B. 1M@0+256K@4.  Ein Wechsel ist ein Kaltstart."
)

#: Werte der ersten Auswahl (bis P23b) → gleichwertige Kurzform.
#: Bestückung bleibt dieselbe (gleiche Karten, gleiche Moduladressen) — ein gespeicherter
#: Zwischenstand (P8KS, Fingerabdruck mit den Karten) lädt deshalb weiter.
_ALT = {"1m@0": "1x1M", "256k@0": "1x256K", "1m@0+1m@1": "2x1M"}

_KURZ = re.compile(r"^([1-4])[x×](256K|1M)$", re.I)
_RAMKARTE = re.compile(r"^(2|4|8|16)M$", re.I)
_LANG = re.compile(r"^(256K|1M|2M|4M|8M|16M)@(\d{1,2})$", re.I)
_GROESSE = {"256K": 0x40000, "1M": 0x100000, "2M": 0x200000, "4M": 0x400000,
            "8M": 0x800000, "16M": 0x1000000}


def karten(text: str):
    """Karten als Liste ``(basis, groesse)`` — ``None``, wenn der Kern den Text abwiese."""
    t = str(text or "").strip().upper().replace("×", "X")
    m = _KURZ.match(t)
    if m:
        g = _GROESSE[m.group(2)]
        return [(i * g, g) for i in range(int(m.group(1)))]
    if _RAMKARTE.match(t):
        return [(0, _GROESSE[t])]
    aus = []
    for teil in t.split("+"):
        m = _LANG.match(teil)
        if not m:
            return None
        typ, modul = m.group(1), int(m.group(2))
        g = _GROESSE[typ]
        if typ in ("256K", "1M"):
            if modul > (63 if typ == "256K" else 15):
                return None
            aus.append((modul * g, g))
        elif modul != 0:                     # RAM-Karte: keine Moduladresse
            return None
        else:
            aus.append((0, g))
    if not 1 <= len(aus) <= 4:
        return None
    for i, (a, ga) in enumerate(aus):        # Überlappung (Kern: P8000Dram16::pruefe)
        for b, gb in aus[:i]:
            if a < b + gb and b < a + ga:
                return None
    return aus


def gueltig(text: str) -> bool:
    return karten(text) is not None


def normalisieren(wert) -> str:
    """Konfigurationswert → Wert für Kern und Auswahl: alte Werte umgezogen, Ungültiges
    zur Vorgabe, eine gültige eigene Bestückung bleibt (Großschreibung wie im Kern)."""
    v = str(wert or "").strip().replace("×", "x")
    if not v:
        return STANDARD
    klein = v.lower()
    if klein in _ALT:
        return _ALT[klein]
    for w, _a in AUSBAUTEN:
        if klein == w.lower():
            return w
    return v.upper().replace("×", "X") if gueltig(v) else STANDARD


def anzeige(wert: str) -> str:
    """Text im Auswahlfeld (auch für eine Bestückung außerhalb der Liste)."""
    for w, a in AUSBAUTEN:
        if w == wert:
            return a
    ks = karten(wert) or []
    mb = sum(g for _b, g in ks) / 0x100000
    return f"benutzerdefiniert: {wert} ({mb:g} MB)"
