"""Dialog „Verbindung zum Rechner“ des P8000 Terminals (Arbeitsplatz ohne Rechner).

doc/design/26_p8000emu_oberflaeche.md §8.3.  Die Terminaleinheit hat genau eine serielle Leitung
(XB5, Schnittstelle 0 der ``serial_*``-Funktionen, eigener Hub).  Der Dialog ist eine schmale
Bedienung davon — dieselben Einstellungen wie im Reiter *Schnittstellen*, nur auf das
Wesentliche gekürzt:

* **Art**: Telnet (Vorgabe), RFC 2217 oder Datei,
* **Rolle**: Client (verbindet sich mit dem Rechner, Vorgabe) oder Server (wartet auf ihn),
* **Rechner** (Host) und **Port**,
* **Verbinden/Trennen**; der Zustand (getrennt / verbindet / verbunden / Fehler) steht darunter und
  ebenso in der Statuszeile.  Ein Client wiederholt den Verbindungsversuch selbst, bis er
  gelingt — nur *Trennen* beendet das.

Der Kern bleibt maßgeblich: übernommen wird, was ``serial_configure`` annimmt; gesperrte Felder
(im aktiven Betrieb) sind ausgegraut.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
                               QHBoxLayout, QLabel, QLineEdit, QPushButton, QSpinBox,
                               QVBoxLayout)

from app.core_binding import k1520 as K

AKTIV = (K.SER_VERBINDET, K.SER_LAUSCHT, K.SER_VERBUNDEN)

ARTEN = ((K.SER_TELNET, "Telnet"), (K.SER_RFC2217, "RFC 2217"), (K.SER_DATEI, "Datei"))
ROLLEN = ((K.SER_CLIENT, "Client — verbindet sich mit dem Rechner"),
          (K.SER_SERVER, "Server — wartet auf den Rechner"))


def zustand_text(st, konfig=None) -> str:
    """Der Zustand der Leitung in Worten (Dialog und Statuszeile)."""
    if st is None:
        return "keine Leitung"
    ziel = ""
    if st.zustand == K.SER_VERBUNDEN and st.gegenstelle:
        ziel = f" mit {st.gegenstelle}"
    if st.betriebsart == K.SER_DATEI:
        return "Datei offen" if st.zustand in AKTIV else "Datei geschlossen"
    return {K.SER_AUS: "getrennt",
            K.SER_VERBINDET: "verbindet …" + (f" (Versuch {st.versuche})" if st.versuche > 1 else ""),
            K.SER_LAUSCHT: f"wartet auf den Rechner (Port {st.port_aktiv})",
            K.SER_VERBUNDEN: f"verbunden{ziel}",
            K.SER_FEHLER: "Fehler" + (f": {st.meldung}" if st.meldung else "")
            }.get(st.zustand, "unbekannt")


class VerbindungDialog(QDialog):
    """Bedienung der Leitung ``index`` (Terminaleinheit: 0) einer Maschine."""

    def __init__(self, emulator, index: int = 0, parent=None):
        super().__init__(parent)
        self.emulator = emulator
        self.index = index
        self.setWindowTitle("Verbindung zum Rechner")
        lay = QVBoxLayout(self)
        form = QFormLayout()
        self.art = QComboBox()
        for wert, text in ARTEN:
            self.art.addItem(text, wert)
        self.rolle = QComboBox()
        for wert, text in ROLLEN:
            self.rolle.addItem(text, wert)
        self.host = QLineEdit()
        self.host.setPlaceholderText("Name oder Adresse des Rechners, z. B. 127.0.0.1")
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.datei = QLineEdit()
        self.datei_waehlen = QPushButton("…")
        self.datei_waehlen.setFixedWidth(28)
        datei_reihe = QHBoxLayout()
        datei_reihe.addWidget(self.datei, 1)
        datei_reihe.addWidget(self.datei_waehlen)
        form.addRow("Art:", self.art)
        form.addRow("Rolle:", self.rolle)
        self.host_zeile = form.rowCount()
        form.addRow("Rechner:", self.host)
        form.addRow("Port:", self.port)
        form.addRow("Datei:", datei_reihe)
        self._form = form
        lay.addLayout(form)
        self.zustand = QLabel()
        self.zustand.setWordWrap(True)
        lay.addWidget(self.zustand)
        hinweis = QLabel("Der Rechner (Programm p8000emu) bietet eine Leitung unter "
                         "<i>Einstellungen ▸ Schnittstellen</i> als Server an — z. B. tty4 auf Port "
                         "5004.  Hier dieselbe Adresse und denselben Port eintragen.")
        hinweis.setWordWrap(True)
        hinweis.setStyleSheet("color: gray;")
        lay.addWidget(hinweis)
        reihe = QDialogButtonBox()
        self.knopf = reihe.addButton("Verbinden", QDialogButtonBox.ActionRole)
        self.schliessen = reihe.addButton(QDialogButtonBox.Close)
        lay.addWidget(reihe)

        self.art.currentIndexChanged.connect(self._art_gewaehlt)
        self.rolle.currentIndexChanged.connect(self._art_gewaehlt)
        self.knopf.clicked.connect(self._knopf)
        self.schliessen.clicked.connect(self.accept)
        self.datei_waehlen.clicked.connect(self._datei_dialog)
        self._laden()
        self._timer = QTimer(self)
        self._timer.timeout.connect(self.aktualisieren)
        self._timer.start(250)
        self.aktualisieren()

    # ── Kern ⇄ Felder ────────────────────────────────────────────────────────

    def _status(self):
        return self.emulator.serial_status(self.index)

    def _aktiv(self) -> bool:
        st = self._status()
        return st is not None and st.zustand in AKTIV

    def _laden(self):
        k = self.emulator.serial_config(self.index)
        if k is None:
            return
        self.art.blockSignals(True)
        self.art.setCurrentIndex(max(0, self.art.findData(k.betriebsart)))
        self.art.blockSignals(False)
        self.rolle.setCurrentIndex(max(0, self.rolle.findData(k.rolle)))
        self.host.setText(k.host)
        self.port.setValue(k.port or 5000)
        self.datei.setText(k.datei)
        self._felder_zeigen()

    def _felder_zeigen(self):
        datei = self.art.currentData() == K.SER_DATEI
        self.rolle.setEnabled(not datei)
        self.host.setEnabled(not datei and self.rolle.currentData() == K.SER_CLIENT)
        self.port.setEnabled(not datei)
        self.datei.setEnabled(datei)
        self.datei_waehlen.setEnabled(datei)

    def _art_gewaehlt(self, _i: int):
        self._felder_zeigen()

    def _datei_dialog(self):
        pfad, _ = QFileDialog.getSaveFileName(self, "Datei für die Leitung", self.datei.text())
        if pfad:
            self.datei.setText(pfad)

    def _uebernehmen(self) -> bool:
        felder = dict(betriebsart=int(self.art.currentData()), rolle=int(self.rolle.currentData()),
                      host=self.host.text().strip() or "127.0.0.1", port=int(self.port.value()),
                      datei=self.datei.text().strip())
        return self.emulator.serial_configure(self.index, **felder)

    def _knopf(self):
        if self._aktiv():
            self.emulator.serial_stop(self.index)
        else:
            if not self._uebernehmen():
                self.zustand.setText("Die Einstellung wurde vom Kern nicht angenommen "
                                     "(Port, Adresse oder Datei prüfen).")
                return
            if not self.emulator.serial_start(self.index):
                st = self._status()
                self.zustand.setText("Start nicht möglich: " + zustand_text(st))
                return
        self.aktualisieren()

    def aktualisieren(self):
        st = self._status()
        aktiv = st is not None and st.zustand in AKTIV
        if aktiv:                              # im Betrieb sind die Einstellungen gesperrt
            for w in (self.art, self.rolle, self.host, self.port, self.datei, self.datei_waehlen):
                w.setEnabled(False)
        else:
            self.art.setEnabled(True)
            self._felder_zeigen()
        self.knopf.setText("Trennen" if aktiv else "Verbinden")
        self.zustand.setText("Zustand: " + zustand_text(st))
