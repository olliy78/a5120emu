"""Kasten „EPROMmer" des PRG710 Emulators (doc/design/20_prg710.md AP-P7c).

Der EPROMmer der ATP 590068 hat **einen** Sockel (Befund aus `PROG`,
`doc/prg710/eprommer.md`).  Im Emulator ist er ein *virtueller Sockel*: ein
PROM-Abbild als rohes ``.bin`` (U555 = 1 KB, U2716 = 2 KB).  Der Kasten zeigt,
was steckt und was das Programm gerade einstellt (Typ über ZRE-PIO 84H Bit 0,
Versorgung und Programmierspannung aus dem Steuerregister D4H), dazu das
Protokoll des Kerns: jede Spannungsänderung, je Lese- bzw. Programmierphase eine
Zusammenfassung, Fehlbedienungen.

Bedient wird über die ``QAction``s des Fensters (`app/ui/actions.py`,
``eprom_*``); die Knöpfe hier sind nur deren Abbild.  Gelöscht wird ein PROM am
Gerät im UV-Löschgerät, nicht über Software — deshalb ist „UV-Löschen" eine
Bedienung des Sockels, kein Befehl an die Maschine.
"""

from __future__ import annotations

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QPlainTextEdit, QToolButton, QVBoxLayout, QWidget,
)

#: Typnummern des Kerns (k1520_eprom_type) → Anzeige.
TYPNAME = {1: "U555 (1 KB)", 2: "U2716 (2 KB)"}


class EpromWidget(QWidget):
    """Sockel, Lampen, Knöpfe (aus Aktionen) und Protokoll."""

    def __init__(self, emulator, aktionen=(), parent=None):
        super().__init__(parent)
        self.emulator = emulator

        lay = QVBoxLayout(self)
        self.sockel = QLabel()
        self.sockel.setWordWrap(True)
        self.eingestellt = QLabel()
        lay.addWidget(self.sockel)
        lay.addWidget(self.eingestellt)

        lampen = QHBoxLayout()
        self.lampe_versorgung = QLabel("Versorgung")
        self.lampe_vpp = QLabel("Programmierspannung")
        for lampe in (self.lampe_versorgung, self.lampe_vpp):
            lampe.setToolTip("Steuerregister D4H des EPROMmers (Bit 4 bzw. Bit 0+1)")
            lampen.addWidget(lampe)
        lampen.addStretch(1)
        lay.addLayout(lampen)

        knoepfe = QHBoxLayout()
        self.knoepfe = []
        for a in aktionen:
            k = QToolButton()
            k.setDefaultAction(a)
            if a.menu() is not None:
                k.setPopupMode(QToolButton.InstantPopup)
            knoepfe.addWidget(k)
            self.knoepfe.append(k)
        knoepfe.addStretch(1)
        lay.addLayout(knoepfe)

        lay.addWidget(QLabel("Protokoll:"))
        self.protokoll = QPlainTextEdit()
        self.protokoll.setReadOnly(True)
        self.protokoll.setMaximumBlockCount(1000)   # wie der Ringpuffer des Kerns
        lay.addWidget(self.protokoll, 1)

        self._takt = QTimer(self)
        self._takt.timeout.connect(self.aktualisieren)
        self._takt.start(250)
        self.aktualisieren()

    # ── Maschine ─────────────────────────────────────────────────────────────

    def set_emulator(self, emulator) -> None:
        """Neue Maschine (Modell- oder Laufwerkswechsel); das Fenster trägt den
        Sockel hinüber (:meth:`sockel_lesen` / :meth:`sockel_anwenden`)."""
        self.emulator = emulator
        self.aktualisieren()

    def sockel_lesen(self):
        """Stand des Sockels zum Hinübertragen: ``None`` = leer."""
        typ = self.emulator.eprom_type()
        if typ <= 0:
            return None
        return (typ, self.emulator.eprom_read(), self.emulator.eprom_path(),
                self.emulator.eprom_modified())

    def sockel_anwenden(self, stand) -> None:
        if stand is None:
            return
        typ, daten, pfad, geaendert = stand
        self.emulator.eprom_insert_data(daten, typ, pfad, geaendert)
        self.aktualisieren()

    # ── Anzeige ──────────────────────────────────────────────────────────────

    @staticmethod
    def _lampe(label: QLabel, an: bool) -> None:
        farbe = "#d02020" if an else "#606060"
        zeichen = "●" if an else "○"
        text = label.text().lstrip("●○ ")
        label.setText(f"{zeichen} {text}")
        label.setStyleSheet(f"color: {farbe};")

    def aktualisieren(self) -> None:
        emu = self.emulator
        if emu is None or not emu.has_eprommer():
            self.sockel.setText("Sockel: kein EPROMmer")
            return
        typ = emu.eprom_type()
        if typ <= 0:
            self.sockel.setText("Sockel: leer")
        else:
            pfad = emu.eprom_path()
            teile = [TYPNAME.get(typ, "?"), pfad or "ohne Datei"]
            if emu.eprom_modified():
                teile.append("geändert, nicht gespeichert")
            self.sockel.setText("Sockel: " + " · ".join(teile))
        gewaehlt = emu.eprom_selected_type()
        self.eingestellt.setText(
            f"Eingestellt (ZRE-PIO 84H Bit 0): {TYPNAME.get(gewaehlt, '?')}")
        d4 = max(emu.eprom_control(), 0)
        self._lampe(self.lampe_versorgung, bool(d4 & 0x10))
        self._lampe(self.lampe_vpp, (d4 & 0x03) == 0x03)
        neu = emu.eprom_log(only_new=True)
        if neu:
            self.protokoll.appendPlainText("\n".join(neu))

    def protokoll_text(self) -> str:
        return self.protokoll.toPlainText()
