"""Lochband am PRG 710 / 710-1 — Leser daro 1210 und Stanzer daro 1215 an der ADA K6022.

doc/design/20_prg710.md AP-P8d.  Die Aktionen stehen wie alle in :mod:`app.ui.actions`
(``NUR_FUER`` = nur das Programmprofil ``prg710``); hier liegen die Methoden, die sie
auslösen, als Mischklasse des Hauptfensters — und das Untermenü *Maschine ▸ Lochband*.

Ein Band ist eine Datei mit den Bytes, wie sie gestanzt sind (8 Kanäle, keine Umsetzung).
Unter UDOS: ``DO TREAD.1210 <datei> F=A`` liest das eingelegte Band, ``DO TWRITE.1215
<datei> F=A`` stanzt (Handbuch, Abschnitt „Lochband und Fernschreiber“).  Der
Fernschreiber (ASS 590069) braucht hier nichts: er ist ein Anschluss im Reiter
„Schnittstellen“.
"""

from __future__ import annotations

import os

from PySide6.QtWidgets import QFileDialog, QMenu, QMessageBox

#: Namen der Aktionen in :mod:`app.ui.actions`, in der Reihenfolge des Untermenüs.
AKTIONEN = ("band_einlegen", "band_entnehmen", None,
            "stanzband_speichern", "stanzband_leeren", "stanzer_ein")

#: Dateifilter der Auswahl (Endung frei: ein Band ist eine rohe Bytefolge).
FILTER = "Lochband (*.ptp *.bin *.txt);;Alle Dateien (*)"


class LochbandMixin:
    """Methoden des Hauptfensters für das Lochband (nur PRG-Profil)."""

    def _lochband_menue(self, menue) -> None:
        """Untermenü *Lochband* in *menue* einhängen — nur, wenn es die Aktionen gibt."""
        if not hasattr(self, "act_band_einlegen"):
            return
        unter = QMenu("&Lochband", self)
        for name in AKTIONEN:
            if name is None:
                unter.addSeparator()
            else:
                unter.addAction(getattr(self, f"act_{name}"))
        unter.aboutToShow.connect(self._lochband_anzeigen)
        menue.addMenu(unter)
        self._lochband_unter = unter
        self.act_stanzer_ein.blockSignals(True)
        self.act_stanzer_ein.setChecked(True)
        self.act_stanzer_ein.blockSignals(False)

    def _lochband_ordner(self) -> str:
        return getattr(self, "_lochband_letzter_ordner", "") or os.path.expanduser("~")

    def _lochband_anzeigen(self) -> None:
        """Beschriftungen nach dem Stand der Geräte (beim Aufklappen)."""
        st = self.emulator.ptape_reader_status() or {}
        if st.get("inserted"):
            ende = " — Bandende" if st.get("at_end") else ""
            self.act_band_entnehmen.setText(
                f"Band aus dem Leser &nehmen ({st['pos']} von {st['len']} Byte gelesen{ende})")
        else:
            self.act_band_entnehmen.setText("Band aus dem Leser &nehmen")
        self.act_band_entnehmen.setEnabled(bool(st.get("inserted")))
        n = self.emulator.ptape_punch_length() or 0
        self.act_stanzband_speichern.setText(f"Stanzband &speichern… ({n} Byte)")
        ein = self.emulator.ptape_punch_enabled()
        if ein is not None and ein != self.act_stanzer_ein.isChecked():
            self.act_stanzer_ein.blockSignals(True)
            self.act_stanzer_ein.setChecked(ein)
            self.act_stanzer_ein.blockSignals(False)

    def _band_einlegen(self) -> None:
        pfad, _ = QFileDialog.getOpenFileName(self, "Lochband in den Leser legen",
                                              self._lochband_ordner(), FILTER)
        if not pfad:
            return
        self._lochband_letzter_ordner = os.path.dirname(pfad)
        if not self.emulator.ptape_load(pfad):
            QMessageBox.warning(self, "Lochband", f"Das Band ist nicht lesbar:\n{pfad}")
            return
        st = self.emulator.ptape_reader_status() or {}
        self.statusBar().showMessage(
            f"Band {os.path.basename(pfad)} im Leser ({st.get('len', 0)} Byte).", 5000)

    def _band_entnehmen(self) -> None:
        self.emulator.ptape_eject()
        self.statusBar().showMessage("Leser leer.", 4000)

    def _stanzband_speichern(self) -> None:
        pfad, _ = QFileDialog.getSaveFileName(self, "Stanzband speichern",
                                              self._lochband_ordner(), FILTER)
        if not pfad:
            return
        self._lochband_letzter_ordner = os.path.dirname(pfad)
        if not self.emulator.ptape_punch_save(pfad):
            QMessageBox.warning(self, "Lochband", f"Das Stanzband ist nicht schreibbar:\n{pfad}")
            return
        n = self.emulator.ptape_punch_length() or 0
        self.statusBar().showMessage(f"Stanzband gespeichert: {pfad} ({n} Byte).", 5000)

    def _stanzband_leeren(self) -> None:
        self.emulator.ptape_punch_clear()
        self.statusBar().showMessage("Neues (leeres) Stanzband eingelegt.", 4000)

    def _stanzer_schalten(self, ein: bool) -> None:
        self.emulator.ptape_punch_enable(bool(ein))
        self.statusBar().showMessage("Stanzer ein." if ein else "Stanzer aus.", 4000)
