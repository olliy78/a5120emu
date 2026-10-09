"""Der Takt der emulierten Maschine — eine Stelle für Zahl und Beschriftung.

Der A5120 läuft mit **2,45 MHz** (U880), der K8915 mit **2,4576 MHz** — der
Nenntakt steht im Programmprofil (`app/profil.py`), die Voreinstellung der
Funktionen hier ist der A5120.  Die Oberfläche bietet diesen Nenntakt
und ein paar Vielfache davon an; damit Auswahlfeld
(:class:`~app.ui.settings_widget.SettingsWidget`) und Statuszeile
(:mod:`app.ui.status_bar`) dieselbe Sprache sprechen, stehen Stufen und
Beschriftung hier und nicht zweimal nebenher.

Der **Faktor** ist die Rechengrösse: 1,0 = Echtzeit, >1 = Zeitraffer.  ``0.0`` =
unbegrenzt kennt der Emulationstakt weiter, die Oberfläche bietet es seit
2026-10-09 nicht mehr an (:func:`stufe` bildet es aus älteren Konfigurationen
auf die schnellste Stufe ab).
"""

from __future__ import annotations

#: Nenntakt des A5120 in Hertz (deckt sich mit der Vorgabe des Kerns für die K5122).
NENNTAKT_HZ = 2_450_000

#: Derselbe Wert, wie er dem Anwender gezeigt wird.
NENNTAKT_TEXT = "2,45 MHz"

#: Angebotene Faktoren, in der Reihenfolge des Auswahlfelds.
STUFEN = (1.0, 2.0, 5.0, 10.0)


def stufe(faktor) -> float:
    """*faktor* als angebotene Stufe: unbekannt oder ≤ 0 („unbegrenzt" einer älteren
    Konfiguration) wird die schnellste, sonst die nächstgelegene."""
    try:
        faktor = float(faktor)
    except (TypeError, ValueError):
        return 1.0
    if faktor <= 0.0:
        return max(STUFEN)
    return min(STUFEN, key=lambda f: abs(f - faktor))


def beschriftung(faktor: float, nenntakt: str = NENNTAKT_TEXT) -> str:
    """„2,45 MHz", „10 × 2,45 MHz" oder „unbegrenzt" (*nenntakt* aus dem Profil)."""
    faktor = float(faktor)
    if faktor <= 0.0:
        return "unbegrenzt"
    if abs(faktor - 1.0) < 1e-9:
        return nenntakt
    # Ganze Vielfache ohne Nachkommastelle — „2 ×" liest sich, „2,0 ×" nicht.
    zahl = f"{faktor:g}".replace(".", ",")
    return f"{zahl} × {nenntakt}"


def auswahl(nenntakt: str = NENNTAKT_TEXT) -> list:
    """[(Beschriftung, Faktor)] für ein Auswahlfeld."""
    return [(beschriftung(f, nenntakt), f) for f in STUFEN]
