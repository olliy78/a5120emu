"""Kasten „Winchester": das Plattenabbild am WDC des P8000.

doc/design/26_p8000emu_oberflaeche.md §5.  Die Platte ist EINE Datei (roh, Sektor nach
Zylinder/Kopf/Sektor, `doc/design/25_p8000.md` §10.8) — anders als eine Diskette:

* **Kein Schreibschutz.**  Ein Winchesterlaufwerk hat keinen; der Kern weist ``wp`` ab
  (`k1520_hd_mount`).  Der Kasten bietet ihn deshalb gar nicht erst an.
* **Geschrieben wird verzögert, aber von selbst** (der Kern zerlegt geänderte Spuren beim
  Zylinderwechsel und nach einer Schreibpause zurück).  Der Kasten stößt ein ``hd_flush`` an,
  wo es auf die Datei ankommt: vor dem Abtrennen, vor einem Zwischenstand und beim Beenden.
* **Der WDC erkennt die Platte beim Hochlauf** (Initialisierung ≈ 4,5 s Maschinenzeit).  Eine
  Platte, die man im Betrieb anschließt, sieht der Gast erst nach *Rückstellen* oder
  *Rechner ein* — der Kasten sagt das, statt die Maschine ungefragt neu zu starten.
* **Neu angelegt wird mit PAR-Sektor** (`Platte::neu`: Parametersatz auf Zylinder 0 / Kopf 0 /
  Sektor 1, Rest 0E5H).  Formatiert ist sie damit NICHT: ``sa.format`` im Gast legt die
  Spuren an; der 16-Bit-Monitor versucht nach dem Hardwaretest, von der leeren Platte zu
  starten (Merkposten p8000 Nr. 21 — Gastverhalten, kein Fehler).
"""

from __future__ import annotations

import os
from typing import Optional

from PySide6.QtCore import QTimer, Signal, Qt
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
                               QHBoxLayout, QLabel, QMessageBox, QPushButton, QVBoxLayout, QWidget)

from app import instanz, paths
from app.ui import status_bar

#: Plattentypen des Kerns (`core/peripherals/winchester/platte.cpp`):
#: (Name für ``hd_create``, Beschriftung, Zylinder, Köpfe, Sektoren je Spur).
TYPEN = (
    ("K5504.50", "K5504.50 (ROB, 1024 Zyl., 5 Köpfe)", 1024, 5, 18),
    ("D5126", "D5126 (NEC, 615 Zyl., 4 Köpfe)", 615, 4, 18),
    ("D5146", "D5146 (NEC, 615 Zyl., 8 Köpfe)", 615, 8, 18),
    ("VS", "VS (Robotron, 820 Zyl., 6 Köpfe)", 820, 6, 18),
)
STANDARD_TYP = "K5504.50"
STANDARD_DATEI = "p8000_platte.img"
UNIT = 0          # nur Laufwerk 0 — die Maschine führt drei, der Kasten bedient das erste


def groesse_text(zyl: int, kopf: int, sektoren: int) -> str:
    mib = zyl * kopf * sektoren * 512 / (1024 * 1024)
    return f"{mib:.0f} MiB"


class PlattenDialog(QDialog):
    """Typ der neuen Platte erfragen (der Pfad kommt danach aus dem Speichern-Dialog)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Neue Platte anlegen")
        lay = QVBoxLayout(self)
        form = QFormLayout()
        self.typ = QComboBox()
        for name, text, z, k, s in TYPEN:
            self.typ.addItem(f"{text} — {groesse_text(z, k, s)}", name)
        form.addRow("Plattentyp:", self.typ)
        lay.addLayout(form)
        lay.addWidget(QLabel("Die Platte entsteht mit gültigem Parametersatz (PAR), aber "
                             "unformatiert:\nformatieren mit sa.format im Gast "
                             "(WEGA-Installation)."))
        tasten = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        tasten.accepted.connect(self.accept)
        tasten.rejected.connect(self.reject)
        lay.addWidget(tasten)

    def typ_name(self) -> str:
        return self.typ.currentData()


class PlattenWidget(QWidget):
    """Anschließen, Neu anlegen und Abtrennen der Winchesterplatte (Laufwerk 0)."""

    #: Die Belegung hat sich geändert (für die Konfiguration).
    geaendert = Signal()
    #: Hinweis für die Statuszeile des Fensters.
    meldung = Signal(str)

    def __init__(self, emulator=None, parent=None):
        super().__init__(parent)
        self.emulator = emulator
        self._pfad = ""
        self._verfuegbar = True
        #: Hat der Anwender (oder die Konfiguration) schon über die Platte entschieden?
        #: Erst dann legt das Programm beim Start keine Standardplatte mehr an.
        self.entschieden = False

        lay = QVBoxLayout(self)
        lay.setContentsMargins(6, 6, 6, 6)
        kopf = QHBoxLayout()
        self.lampe = status_bar.DriveLamp()
        self.titel = QLabel("<b>Winchester</b> — Laufwerk 0")
        kopf.addWidget(self.lampe)
        kopf.addWidget(self.titel, 1)
        lay.addLayout(kopf)
        self.name = QLabel()
        self.name.setWordWrap(True)
        lay.addWidget(self.name)
        self.hinweis = QLabel()
        self.hinweis.setWordWrap(True)
        self.hinweis.setStyleSheet("color: gray;")
        lay.addWidget(self.hinweis)
        reihe = QHBoxLayout()
        self.knopf_anschliessen = QPushButton("Anschließen…")
        self.knopf_neu = QPushButton("Neue Platte…")
        self.knopf_abtrennen = QPushButton("Abtrennen")
        for k in (self.knopf_anschliessen, self.knopf_neu, self.knopf_abtrennen):
            k.setFocusPolicy(Qt.NoFocus)
            reihe.addWidget(k)
        lay.addLayout(reihe)
        lay.addStretch(1)
        self.knopf_anschliessen.clicked.connect(self._anschliessen_dialog)
        self.knopf_neu.clicked.connect(self._neu_dialog)
        self.knopf_abtrennen.clicked.connect(self.abtrennen)
        self.knopf_anschliessen.setToolTip("Ein vorhandenes Plattenabbild (roh) an den WDC hängen")
        self.knopf_neu.setToolTip("Eine neue, unformatierte Platte mit Parametersatz anlegen")
        self.knopf_abtrennen.setToolTip("Die Platte vom WDC lösen (vorher wird sie zurückgeschrieben)")

        # Die Lampe folgt dem ZUGRIFF, im selben 120-ms-Takt wie die Laufwerke.
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._lampe)
        self._timer.start(120)
        self._anzeigen()

    # ── Zustand ──────────────────────────────────────────────────────────────

    def pfad(self) -> str:
        """Pfad des angeschlossenen Abbilds (``""`` = keine Platte)."""
        return self._pfad

    def verfuegbar(self) -> bool:
        return self._verfuegbar

    def set_verfuegbar(self, an: bool):
        """Hat das Modell einen WDC?  Ohne ihn sind alle Knöpfe gesperrt."""
        self._verfuegbar = bool(an)
        self._anzeigen()

    def set_emulator(self, emulator):
        """An die (neue) Maschine hängen und die gemerkte Platte dort anschließen."""
        self.emulator = emulator
        self._neu_anschliessen()
        self._anzeigen()

    def standard_pfad(self) -> str:
        return str(paths.user_disks_dir() / STANDARD_DATEI)

    def _neu_anschliessen(self):
        """Die gemerkte Platte an der (neuen) Maschine anschließen; fehlt die Datei, bleibt sie leer."""
        if not (self._verfuegbar and self._pfad and self.emulator is not None):
            return
        if not os.path.isfile(self._pfad):
            self.meldung.emit(f"Platte {self._pfad} gibt es nicht mehr — Laufwerk bleibt leer.")
            self._pfad = ""
            return
        if not self._sperren(self._pfad):
            self._pfad = ""
            return
        if not self.emulator.hd_mount(UNIT, self._pfad):
            self.meldung.emit(f"Platte {self._pfad} nicht anschließbar: {self.emulator.hd_error()}")
            instanz.sperre_loesen(self._pfad)
            self._pfad = ""

    # ── Bedienung ────────────────────────────────────────────────────────────

    def anschliessen(self, pfad: str, hinweis: bool = True) -> bool:
        """Ein vorhandenes Abbild anschließen (ersetzt die bisherige Platte).

        *hinweis* = False vor dem ersten Einschalten (Start): dort gilt der Rückstell-Hinweis nicht."""
        if not self._verfuegbar or self.emulator is None:
            return False
        self._loesen()
        if not self._sperren(pfad):
            self._anzeigen()
            return False
        if not self.emulator.hd_mount(UNIT, pfad):
            instanz.sperre_loesen(pfad)
            self.meldung.emit(f"Platte nicht angeschlossen: {self.emulator.hd_error()}")
            self._anzeigen()
            return False
        self._pfad = pfad
        self.entschieden = True
        self._anzeigen(neu_angeschlossen=hinweis)
        self.geaendert.emit()
        return True

    def neu_anlegen(self, pfad: str, typ: str = STANDARD_TYP, hinweis: bool = True) -> bool:
        """Eine neue Platte mit PAR-Sektor anlegen und anschließen (*hinweis* wie bei :meth:`anschliessen`)."""
        if not self._verfuegbar or self.emulator is None:
            return False
        self._loesen()
        os.makedirs(os.path.dirname(os.path.abspath(pfad)), exist_ok=True)
        if not self._sperren(pfad):
            self._anzeigen()
            return False
        if not self.emulator.hd_create(UNIT, pfad, typ):
            instanz.sperre_loesen(pfad)
            self.meldung.emit(f"Platte nicht angelegt: {self.emulator.hd_error()}")
            self._anzeigen()
            return False
        self._pfad = pfad
        self.entschieden = True
        self._anzeigen(neu_angeschlossen=hinweis)
        self.geaendert.emit()
        return True

    def abtrennen(self):
        """Die Platte lösen — der Kern schreibt vorher geänderte Spuren zurück."""
        self._loesen()
        self.entschieden = True
        self._anzeigen()
        self.geaendert.emit()

    def _loesen(self):
        if self._pfad and self.emulator is not None:
            self.emulator.hd_unmount(UNIT)
        if self._pfad:
            instanz.sperre_loesen(self._pfad)
        self._pfad = ""

    def _sperren(self, pfad: str) -> bool:
        """Die Platte für diese Instanz reservieren (Mehrinstanzbetrieb, `app/instanz.py`).

        Zwei Rechner auf derselben Platte überschrieben sich gegenseitig; die zweite Instanz
        bekommt sie deshalb nicht (der Kern kennt keinen Schreibschutz für Platten)."""
        fremd = instanz.sperre_nehmen(pfad)
        if fremd is None:
            return True
        self.meldung.emit(f"Die Platte {os.path.basename(pfad)} gehört schon dem Prozess {fremd} "
                          "(andere Instanz) — nicht angeschlossen.  Zwei Rechner auf einer Platte "
                          "zerstören sie; eine eigene Platte über „Neue Platte…“ anlegen.")
        return False

    def freigeben(self):
        """Beim Beenden: die Sperre neben dem Abbild entfernen (die Platte bleibt gemerkt)."""
        if self._pfad:
            instanz.sperre_loesen(self._pfad)

    def sichern(self):
        """Geänderte Spuren jetzt in die Datei schreiben (vor Zwischenstand und Beenden)."""
        if self._pfad and self.emulator is not None:
            self.emulator.hd_flush()

    def _anschliessen_dialog(self):
        start = self._pfad or str(paths.user_disks_dir())
        pfad, _ = QFileDialog.getOpenFileName(
            self, "Plattenabbild anschließen", start,
            "Plattenabbild (*.img *.hd *.bin);;Alle Dateien (*)")
        if pfad:
            self.anschliessen(pfad)

    def _neu_dialog(self):
        dlg = PlattenDialog(self)
        if not dlg.exec():
            return
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Neue Platte speichern unter", self.standard_pfad(),
            "Plattenabbild (*.img);;Alle Dateien (*)")
        if not pfad:
            return
        if "." not in os.path.basename(pfad):
            pfad += ".img"
        if os.path.exists(pfad) and QMessageBox.question(
                self, "Neue Platte", f"{pfad} gibt es schon.  Überschreiben?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        self.neu_anlegen(pfad, dlg.typ_name())

    # ── Konfiguration ────────────────────────────────────────────────────────

    def zustand_lesen(self) -> dict:
        return {"path": self._pfad}

    def zustand_anwenden(self, z) -> None:
        """Den gemerkten Stand herstellen; ``{"path": ""}`` heißt ausdrücklich „keine Platte"."""
        z = z if isinstance(z, dict) else {}
        self.entschieden = True
        pfad = str(z.get("path") or "")
        if not pfad:
            self._loesen()
        elif pfad != self._pfad:
            self._loesen()
            self._pfad = pfad
            self._neu_anschliessen()
        self._anzeigen()

    # ── Anzeige ──────────────────────────────────────────────────────────────

    def _lampe(self):
        zustand = status_bar.LEER
        if self._pfad:
            zustand = status_bar.BELEGT
            try:
                if self.emulator is not None and self.emulator.hd_led(UNIT):
                    zustand = status_bar.ZUGRIFF
            except Exception:
                pass
        self.lampe.set_zustand(zustand)

    def _anzeigen(self, neu_angeschlossen: bool = False):
        for k in (self.knopf_anschliessen, self.knopf_neu):
            k.setEnabled(self._verfuegbar)
        self.knopf_abtrennen.setEnabled(self._verfuegbar and bool(self._pfad))
        if not self._verfuegbar:
            self.name.setText("kein Winchesterkontroller in diesem Modell")
            self.hinweis.setText("Das Modell „ohne Winchester“ bzw. „nur 8-Bit“ hat keinen WDC "
                                 "(Einstellungen ▸ Allgemein ▸ Modell).")
        elif not self._pfad:
            self.name.setText("keine Platte angeschlossen")
            self.hinweis.setText(
                "Das Programm legt keine Platte von selbst an (eine leere Platte schickt den "
                "16-Bit-Monitor nach dem Hardwaretest in den AUTOBOOT).  „Neue Platte…“ legt "
                "ein Abbild an (unformatiert, mit Parametersatz; formatieren mit sa.format im "
                "Gast), „Anschließen…“ nimmt ein vorhandenes.")
        else:
            self.name.setText(os.path.basename(self._pfad))
            self.name.setToolTip(self._pfad)
            self.hinweis.setText(
                "Neu angeschlossen: der WDC erkennt die Platte erst nach Rückstellen oder "
                "Rechner ein." if neu_angeschlossen else
                "Kein Schreibschutz; Änderungen gehen von selbst in die Datei.")
        self._lampe()
