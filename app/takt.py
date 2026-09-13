"""Der Takt der emulierten Maschine — eine Stelle für Zahl und Beschriftung.

Der A5120 läuft mit **2,45 MHz** (U880).  Die Oberfläche bietet diesen Nenntakt
und ein paar Vielfache davon an; damit Auswahlfeld
(:class:`~app.ui.settings_widget.SettingsWidget`) und Statuszeile
(:mod:`app.ui.status_bar`) dieselbe Sprache sprechen, stehen Stufen und
Beschriftung hier und nicht zweimal nebenher.

Der **Faktor** ist die Rechengrösse: 1,0 = Echtzeit, >1 = Zeitraffer, ``0.0`` =
unbegrenzt (so schnell, wie der Wirtsrechner kann — ein Nenntakt lässt sich
dafür nicht angeben, deshalb die Sonderbeschriftung).
"""

from __future__ import annotations

#: Nenntakt des A5120 in Hertz (deckt sich mit der Vorgabe des Kerns für die K5122).
NENNTAKT_HZ = 2_450_000

#: Derselbe Wert, wie er dem Anwender gezeigt wird.
NENNTAKT_TEXT = "2,45 MHz"

#: Angebotene Faktoren, in der Reihenfolge des Auswahlfelds.
STUFEN = (1.0, 2.0, 5.0, 10.0, 0.0)


def beschriftung(faktor: float) -> str:
    """„2,45 MHz", „10 × 2,45 MHz" oder „unbegrenzt"."""
    faktor = float(faktor)
    if faktor <= 0.0:
        return "unbegrenzt"
    if abs(faktor - 1.0) < 1e-9:
        return NENNTAKT_TEXT
    # Ganze Vielfache ohne Nachkommastelle — „2 ×" liest sich, „2,0 ×" nicht.
    zahl = f"{faktor:g}".replace(".", ",")
    return f"{zahl} × {NENNTAKT_TEXT}"


def auswahl() -> list:
    """[(Beschriftung, Faktor)] für ein Auswahlfeld."""
    return [(beschriftung(f), f) for f in STUFEN]
