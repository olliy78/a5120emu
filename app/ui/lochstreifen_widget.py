"""Kasten „Lochstreifen" — Leser und Stanzer an der ADA K6022 (SIF1000).

doc/design/23_lochstreifen.md §7 (AP-L4).  Die K6022 ist in allen drei Programmen eine
steckbare Option (*Einstellungen ▸ Allgemein ▸ Lochstreifen*); der Kasten gibt es nur,
solange sie steckt — das Fenster blendet ihn dann ein und sonst samt Menüeintrag aus.

Zwei Blöcke wie die Laufwerkskästen:

* **Lochstreifenleser** — ein Band ist eine Datei; nach der Dateiwahl fragt
  :mod:`app.ui.lochstreifen_format_dialog` nach dem Format.  Gelesen wird aus dem
  Speicher, die Datei bleibt unberührt.
* **Lochstreifenstanzer** — an eine Datei *gebunden*: was gestanzt wird, schreibt der
  Kern nach jeder Stanzpause in diese Datei zurück (wie eine Diskette).  Eine
  vorhandene Datei ist der Anfang des Bandes; „Neues Band" leert sie sofort.

Der Stand (gelesen/Länge, gestanzt) kommt aus dem Sekundentakt des Fensters
(:meth:`aktualisieren`) und nach jeder Bedienung — kein eigener Zeitgeber.
Keine Tastenkürzel: die Kürzeltabelle des Handbuchs ist ein Vertrag.
"""

from __future__ import annotations

import os
from typing import List, Optional

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit,
    QMessageBox, QPushButton, QVBoxLayout, QWidget,
)

from app import paths
from app.core_binding.k1520 import PTAPE_FORMAT_NAMEN
from app.ui import lochstreifen_format_dialog as formatdialog


def _formatname(fmt) -> str:
    return PTAPE_FORMAT_NAMEN.get(int(fmt), "?") if fmt is not None else "—"


class LochstreifenWidget(QWidget):
    """Leser und Stanzer der K6022."""

    #: Eine Bedienung hat Datei/Format geändert — gehört in die Konfiguration.
    geaendert = Signal()
    #: Eine Zeile für die Statuszeile des Fensters.
    meldung = Signal(str)

    def __init__(self, emulator, parent=None):
        super().__init__(parent)
        self.emulator = emulator
        self._ordner = ""

        lay = QVBoxLayout(self)

        # ── Leser ────────────────────────────────────────────────────────────
        leser = QGroupBox("Lochstreifenleser")
        lf = QFormLayout(leser)
        self.leser_datei = QLineEdit()
        self.leser_datei.setReadOnly(True)
        self.leser_datei.setPlaceholderText("[kein Band]")
        self.leser_format = QLabel("—")
        self.leser_stand = QLabel("—")
        lf.addRow("Datei:", self.leser_datei)
        lf.addRow("Format:", self.leser_format)
        lf.addRow("Stand:", self.leser_stand)
        knoepfe = QHBoxLayout()
        self.leser_oeffnen_knopf = QPushButton("Öffnen…")
        self.leser_oeffnen_knopf.setToolTip(
            "Eine Banddatei in den Leser legen (danach wird das Format erfragt)")
        self.leser_entnehmen_knopf = QPushButton("Entnehmen")
        self.leser_entnehmen_knopf.setToolTip("Das Band aus dem Leser nehmen")
        self.leser_oeffnen_knopf.clicked.connect(lambda: self.leser_oeffnen())
        self.leser_entnehmen_knopf.clicked.connect(self.leser_entnehmen)
        knoepfe.addWidget(self.leser_oeffnen_knopf)
        knoepfe.addWidget(self.leser_entnehmen_knopf)
        knoepfe.addStretch(1)
        lf.addRow(knoepfe)
        lay.addWidget(leser)

        # ── Stanzer ──────────────────────────────────────────────────────────
        stanzer = QGroupBox("Lochstreifenstanzer")
        sf = QFormLayout(stanzer)
        self.stanzer_datei = QLineEdit()
        self.stanzer_datei.setReadOnly(True)
        self.stanzer_datei.setPlaceholderText("[nicht gebunden — Band nur im Speicher]")
        self.stanzer_format = QLabel("—")
        self.stanzer_stand = QLabel("—")
        sf.addRow("Datei:", self.stanzer_datei)
        sf.addRow("Format:", self.stanzer_format)
        sf.addRow("Gestanzt:", self.stanzer_stand)
        knoepfe = QHBoxLayout()
        self.stanzer_oeffnen_knopf = QPushButton("Öffnen…")
        self.stanzer_oeffnen_knopf.setToolTip(
            "Den Stanzer an eine Datei binden: was gestanzt wird, landet dort.  Eine "
            "vorhandene Datei ist der Anfang des Bandes.")
        self.stanzer_loesen_knopf = QPushButton("Lösen")
        self.stanzer_loesen_knopf.setToolTip(
            "Die Bindung lösen (die Datei wird vorher zurückgeschrieben); das Band "
            "bleibt im Stanzer")
        self.neues_band_knopf = QPushButton("Neues Band")
        self.neues_band_knopf.setToolTip(
            "Ein leeres Band einlegen — eine gebundene Datei wird sofort geleert")
        self.stanzer_oeffnen_knopf.clicked.connect(lambda: self.stanzer_oeffnen())
        self.stanzer_loesen_knopf.clicked.connect(self.stanzer_loesen)
        self.neues_band_knopf.clicked.connect(self.neues_band)
        for k in (self.stanzer_oeffnen_knopf, self.stanzer_loesen_knopf,
                  self.neues_band_knopf):
            knoepfe.addWidget(k)
        knoepfe.addStretch(1)
        sf.addRow(knoepfe)
        self.stanzer_ein = QCheckBox("Stanzer ein")
        self.stanzer_ein.setChecked(True)
        self.stanzer_ein.setToolTip(
            "Ausgeschaltet quittiert der Stanzer nicht — der Treiber meldet nach etwa "
            "einer Sekunde einen Fehler (UDOS: ERROR C2)")
        self.stanzer_ein.toggled.connect(self._stanzer_schalten)
        sf.addRow(self.stanzer_ein)
        lay.addWidget(stanzer)
        lay.addStretch(1)

        self.aktualisieren()

    # ── Maschine ─────────────────────────────────────────────────────────────

    def set_emulator(self, emulator) -> None:
        """Neue Maschine; Datei und Format trägt das Fenster über
        :meth:`zustand_lesen` / :meth:`zustand_anwenden` hinüber."""
        self.emulator = emulator
        self.aktualisieren()

    def _gesteckt(self) -> bool:
        return self.emulator is not None and self.emulator.ptape_installed()

    # ── Anzeige ──────────────────────────────────────────────────────────────

    def aktualisieren(self) -> None:
        """Stand aus dem Kern (Sekundentakt des Fensters und nach jeder Bedienung)."""
        gesteckt = self._gesteckt()
        for w in (self.leser_oeffnen_knopf, self.stanzer_oeffnen_knopf,
                  self.neues_band_knopf, self.stanzer_ein):
            w.setEnabled(gesteckt)
        if not gesteckt:
            self.leser_entnehmen_knopf.setEnabled(False)
            self.stanzer_loesen_knopf.setEnabled(False)
            return
        st = self.emulator.ptape_reader_status() or {}
        if st.get("inserted"):
            self.leser_datei.setText(st.get("file") or "")
            self.leser_format.setText(_formatname(st.get("format")))
            ende = " — Bandende" if st.get("at_end") else ""
            self.leser_stand.setText(f"{st['pos']} von {st['len']} Byte gelesen{ende}")
        else:
            self.leser_datei.setText("")
            self.leser_format.setText("—")
            self.leser_stand.setText("kein Band")
        self.leser_entnehmen_knopf.setEnabled(bool(st.get("inserted")))

        ps = self.emulator.ptape_punch_status() or {}
        datei = ps.get("file") or ""
        self.stanzer_datei.setText(datei)
        self.stanzer_format.setText(_formatname(ps.get("format")) if datei else "Roh")
        self.stanzer_stand.setText(f"{ps.get('len', 0)} Byte")
        self.stanzer_loesen_knopf.setEnabled(bool(datei))
        ein = bool(ps.get("enabled", True))
        if ein != self.stanzer_ein.isChecked():
            self.stanzer_ein.blockSignals(True)
            self.stanzer_ein.setChecked(ein)
            self.stanzer_ein.blockSignals(False)

    # ── Bedienung ────────────────────────────────────────────────────────────

    def _startordner(self) -> str:
        return self._ordner or str(paths.user_disks_dir())

    def leser_oeffnen(self, pfad: Optional[str] = None) -> bool:
        """Dateiwahl → Formatdialog → Band in den Leser."""
        if pfad is None:
            pfad, _ = QFileDialog.getOpenFileName(
                self, "Lochstreifen in den Leser legen", self._startordner(),
                formatdialog.FILTER)
        if not pfad:
            return False
        self._ordner = os.path.dirname(pfad)
        fmt = formatdialog.frage(self, pfad, stanzer=False)
        if fmt is None:
            return False
        if not self.emulator.ptape_load(pfad, fmt):
            QMessageBox.warning(self, "Lochstreifenleser",
                                f"Das Band ist nicht lesbar:\n{pfad}\n\n"
                                f"{self.emulator.ptape_error()}")
            self.aktualisieren()
            return False
        self.aktualisieren()
        st = self.emulator.ptape_reader_status() or {}
        self.meldung.emit(f"Lochstreifen {os.path.basename(pfad)} im Leser "
                          f"({st.get('len', 0)} Byte).")
        self.geaendert.emit()
        return True

    def leser_entnehmen(self) -> None:
        self.emulator.ptape_eject()
        self.aktualisieren()
        self.meldung.emit("Lochstreifenleser leer.")
        self.geaendert.emit()

    def stanzer_oeffnen(self, pfad: Optional[str] = None) -> bool:
        """Dateiwahl → Formatdialog → Stanzer an die Datei binden."""
        if pfad is None:
            pfad, _ = QFileDialog.getSaveFileName(
                self, "Stanzer an eine Datei binden", self._startordner(),
                formatdialog.FILTER, "", QFileDialog.DontConfirmOverwrite)
        if not pfad:
            return False
        self._ordner = os.path.dirname(pfad)
        fmt = formatdialog.frage(self, pfad, stanzer=True)
        if fmt is None:
            return False
        if not self.emulator.ptape_punch_bind(pfad, fmt):
            QMessageBox.warning(self, "Lochstreifenstanzer",
                                f"Der Stanzer lässt sich nicht an die Datei binden:\n"
                                f"{pfad}\n\n{self.emulator.ptape_error()}")
            self.aktualisieren()
            return False
        self.aktualisieren()
        self.meldung.emit(f"Stanzer schreibt nach {os.path.basename(pfad)}.")
        self.geaendert.emit()
        return True

    def stanzer_loesen(self) -> None:
        if not self.emulator.ptape_punch_unbind():
            QMessageBox.warning(self, "Lochstreifenstanzer",
                                f"Zurückschreiben gescheitert:\n{self.emulator.ptape_error()}")
        self.aktualisieren()
        self.meldung.emit("Stanzer gelöst — das Band bleibt im Speicher.")
        self.geaendert.emit()

    def neues_band(self) -> None:
        ps = self.emulator.ptape_punch_status() or {}
        if ps.get("len", 0) > 0 and QMessageBox.question(
                self, "Neues Band",
                f"Das Stanzband ({ps['len']} Byte) verwerfen"
                + (f" und {os.path.basename(ps['file'])} leeren" if ps.get("file") else "")
                + "?", QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        if not self.emulator.ptape_punch_new_tape():
            QMessageBox.warning(self, "Lochstreifenstanzer",
                                f"Neues Band gescheitert:\n{self.emulator.ptape_error()}")
        self.aktualisieren()
        self.meldung.emit("Neues (leeres) Stanzband eingelegt.")

    def _stanzer_schalten(self, ein: bool) -> None:
        self.emulator.ptape_punch_enable(bool(ein))
        self.meldung.emit("Stanzer ein." if ein else "Stanzer aus.")

    # ── Konfiguration ────────────────────────────────────────────────────────

    def zustand_lesen(self) -> dict:
        """``{"leser": {datei, format} | None, "stanzer": {datei, format} | None}``."""
        z = {"leser": None, "stanzer": None}
        if not self._gesteckt():
            return z
        st = self.emulator.ptape_reader_status() or {}
        if st.get("inserted") and st.get("file"):
            z["leser"] = {"datei": st["file"], "format": int(st.get("format", 0))}
        ps = self.emulator.ptape_punch_status() or {}
        if ps.get("file"):
            z["stanzer"] = {"datei": ps["file"], "format": int(ps.get("format", 0))}
        return z

    def zustand_anwenden(self, z) -> List[str]:
        """Leser und Stanzer nach *z* (wie :meth:`zustand_lesen`) belegen.

        Eine fehlende Datei wird NICHT gebunden — der Grund kommt als Hinweis zurück
        (für die Statuszeile, kein Meldungsfenster: der Start soll nicht anhalten).
        """
        hinweise: List[str] = []
        if not self._gesteckt() or not isinstance(z, dict):
            return hinweise
        emu = self.emulator
        leser = z.get("leser")
        if isinstance(leser, dict) and leser.get("datei"):
            pfad = str(leser["datei"])
            if not os.path.isfile(pfad):
                emu.ptape_eject()
                hinweise.append(f"Lochstreifenleser: {pfad} fehlt — der Leser bleibt leer.")
            elif not emu.ptape_load(pfad, int(leser.get("format") or 0)):
                hinweise.append(f"Lochstreifenleser: {emu.ptape_error()}")
        elif (emu.ptape_reader_status() or {}).get("inserted"):
            emu.ptape_eject()
        stanzer = z.get("stanzer")
        if isinstance(stanzer, dict) and stanzer.get("datei"):
            pfad = str(stanzer["datei"])
            if not os.path.isfile(pfad):
                hinweise.append(f"Lochstreifenstanzer: {pfad} fehlt — nicht gebunden.")
            elif not emu.ptape_punch_bind(pfad, int(stanzer.get("format") or 0)):
                hinweise.append(f"Lochstreifenstanzer: {emu.ptape_error()}")
        elif (emu.ptape_punch_status() or {}).get("file"):
            emu.ptape_punch_unbind()
        self.aktualisieren()
        return hinweise
