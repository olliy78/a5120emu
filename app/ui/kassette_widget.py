"""Kasten „Kassette" — die beiden Laufwerke K5200 am AKB K5020 (K5221-Kassetten).

Entwurf doc/design/29_kassette.md, AP K9.  Die K5020 ist in jedem Programm eine
steckbare Option (*Einstellungen ▸ Allgemein ▸ Kassette*); den Kasten gibt es nur mit
Karte.  Wie der Lochstreifen: jede Kassette ist eine Datei (``.k52``), die der Kern
BINDET (:meth:`K1520Emulator.tape_load` legt eine fehlende Datei als leere Kassette an)
und nach einer Schreibpause selbst zurückschreibt.  Je Laufwerk: Einlegen, Neue (ohne
Datei), Auswerfen, Wenden (Seite A/B), Schreibschutz je Seite, Position, Lampe.

Die Gast-Seite kennt den Kasten nicht; was der Anwender hier tut, ändert nur das Medium.
Kein Tastenkürzel (die Kürzeltabelle des Handbuchs ist ein Vertrag).
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

LAUFWERKE = (0, 1)
SEITEN = "AB"
FILTER = "Kassette (*.k52);;Alle Dateien (*)"


class _Laufwerk(QGroupBox):
    """Ein Laufwerk (Gerät 1 = X3, Gerät 2 = X4)."""

    def __init__(self, nr: int, kasten: "KassetteWidget"):
        super().__init__(f"Laufwerk {nr + 1}")
        self.nr = nr
        self.kasten = kasten
        f = QFormLayout(self)
        kopf = QHBoxLayout()
        self.lampe = QLabel("●")
        self.lampe.setToolTip("Leuchtet, solange das Band läuft")
        self.datei = QLineEdit()
        self.datei.setReadOnly(True)
        self.datei.setPlaceholderText("[keine Kassette]")
        kopf.addWidget(self.lampe)
        kopf.addWidget(self.datei, 1)
        f.addRow(kopf)
        self.seite = QLabel("—")
        self.position = QLabel("—")
        f.addRow("Seite:", self.seite)
        f.addRow("Position:", self.position)

        k = QHBoxLayout()
        self.einlegen_knopf = QPushButton("Einlegen…")
        self.einlegen_knopf.setToolTip(
            "Eine .k52-Datei einlegen und binden; eine fehlende Datei wird als leere "
            "Kassette angelegt")
        self.neu_knopf = QPushButton("Neue")
        self.neu_knopf.setToolTip("Eine leere Kassette ohne Datei einlegen")
        self.auswerfen_knopf = QPushButton("Auswerfen")
        self.auswerfen_knopf.setToolTip(
            "Die Kassette entnehmen (eine gebundene Datei wird vorher geschrieben)")
        self.wenden_knopf = QPushButton("Wenden")
        self.wenden_knopf.setToolTip("Die Kassette wenden: Seite A ↔ B (nur bei stehendem Band)")
        self.einlegen_knopf.clicked.connect(lambda: self.einlegen())
        self.neu_knopf.clicked.connect(self.neu)
        self.auswerfen_knopf.clicked.connect(self.auswerfen)
        self.wenden_knopf.clicked.connect(self.wenden)
        for w in (self.einlegen_knopf, self.neu_knopf, self.auswerfen_knopf,
                  self.wenden_knopf):
            k.addWidget(w)
        k.addStretch(1)
        f.addRow(k)

        self.schutz: List[QCheckBox] = []
        sh = QHBoxLayout()
        for s, name in enumerate(SEITEN):
            c = QCheckBox(f"Seite {name} schreibgeschützt")
            c.setToolTip("Der Schreibschutz der Seite (Aussparung der Kassette)")
            c.toggled.connect(lambda an, s=s: self._schutz(s, an))
            self.schutz.append(c)
            sh.addWidget(c)
        sh.addStretch(1)
        f.addRow(sh)

    @property
    def emu(self):
        return self.kasten.emulator

    # ── Anzeige ──────────────────────────────────────────────────────────────

    def aktualisieren(self, gesteckt: bool) -> None:
        st = self.emu.tape_status(self.nr) if gesteckt else None
        # Ein Laufwerk, das die Maschine nicht hat (MC80.2x: nur das SK 1200), entfällt.
        self.setVisible(not (gesteckt and st is None))
        geladen = bool(st and st["loaded"])
        self.einlegen_knopf.setEnabled(gesteckt)
        self.neu_knopf.setEnabled(gesteckt)
        self.auswerfen_knopf.setEnabled(geladen)
        self.wenden_knopf.setEnabled(geladen)
        laeuft = bool(st and st["moving"])
        self.lampe.setStyleSheet("color: #e03030;" if laeuft else "color: #808080;")
        if not geladen:
            self.datei.setText("")
            self.seite.setText("—")
            self.position.setText("—")
            for c in self.schutz:
                c.setEnabled(False)
                self._setzen(c, False)
            return
        self.datei.setText(st["file"])
        self.seite.setText(SEITEN[st["side"] & 1])
        pos, ln = st["pos_um"] / 1000.0, st["len_um"] / 1000.0
        self.position.setText(f"{pos:.1f} von {ln:.1f} mm, {st['blocks']} Blöcke"
                              + (" — läuft" if laeuft else ""))
        for s, c in enumerate(self.schutz):
            c.setEnabled(True)
            self._setzen(c, bool(self.emu.tape_protected(self.nr, s)))

    @staticmethod
    def _setzen(c: QCheckBox, an: bool) -> None:
        if c.isChecked() != an:
            c.blockSignals(True)
            c.setChecked(an)
            c.blockSignals(False)

    # ── Bedienung ────────────────────────────────────────────────────────────

    def _fehler(self, was: str) -> None:
        QMessageBox.warning(self.kasten, f"Laufwerk {self.nr + 1}",
                            f"{was}\n\n{self.emu.tape_error()}")
        self.kasten.aktualisieren()

    def einlegen(self, pfad: Optional[str] = None) -> bool:
        if pfad is None:
            pfad, _ = QFileDialog.getSaveFileName(
                self.kasten, "Kassette einlegen", self.kasten._startordner(), FILTER, "",
                QFileDialog.DontConfirmOverwrite)
        if not pfad:
            return False
        self.kasten._ordner = os.path.dirname(pfad)
        if not self.emu.tape_load(self.nr, pfad):
            self._fehler(f"Die Kassette lässt sich nicht einlegen:\n{pfad}")
            return False
        self.kasten.aktualisieren()
        self.kasten.meldung.emit(f"Kassette {os.path.basename(pfad)} in Laufwerk {self.nr + 1}.")
        self.kasten.geaendert.emit()
        return True

    def neu(self) -> bool:
        if not self.emu.tape_new(self.nr):
            self._fehler("Eine neue Kassette lässt sich nicht einlegen.")
            return False
        self.kasten.aktualisieren()
        self.kasten.meldung.emit(f"Neue Kassette (ohne Datei) in Laufwerk {self.nr + 1}.")
        self.kasten.geaendert.emit()
        return True

    def auswerfen(self) -> bool:
        if not self.emu.tape_eject(self.nr):
            self._fehler("Die Kassette lässt sich nicht auswerfen (Band läuft?).")
            return False
        self.kasten.aktualisieren()
        self.kasten.meldung.emit(f"Laufwerk {self.nr + 1} leer.")
        self.kasten.geaendert.emit()
        return True

    def wenden(self) -> bool:
        if not self.emu.tape_flip(self.nr):
            self._fehler("Die Kassette lässt sich jetzt nicht wenden (Band läuft?).")
            return False
        self.kasten.aktualisieren()
        self.kasten.meldung.emit(f"Kassette in Laufwerk {self.nr + 1} gewendet.")
        return True

    def _schutz(self, seite: int, an: bool) -> None:
        self.emu.tape_protect(self.nr, seite, bool(an))
        self.kasten.meldung.emit(
            f"Laufwerk {self.nr + 1}, Seite {SEITEN[seite]}: "
            + ("schreibgeschützt." if an else "beschreibbar."))
        self.kasten.geaendert.emit()


class KassetteWidget(QWidget):
    """Die zwei Laufwerke der K5020."""

    #: Eine Bedienung hat Datei/Bestückung geändert — gehört in die Konfiguration.
    geaendert = Signal()
    #: Eine Zeile für die Statuszeile des Fensters.
    meldung = Signal(str)

    def __init__(self, emulator, parent=None):
        super().__init__(parent)
        self.emulator = emulator
        self._ordner = ""
        lay = QVBoxLayout(self)
        self.laufwerke = [_Laufwerk(n, self) for n in LAUFWERKE]
        for lw in self.laufwerke:
            lay.addWidget(lw)
        lay.addStretch(1)
        self.aktualisieren()

    def set_emulator(self, emulator) -> None:
        """Neue Maschine; die Dateien trägt das Fenster über
        :meth:`zustand_lesen` / :meth:`zustand_anwenden` hinüber."""
        self.emulator = emulator
        self.aktualisieren()

    def _gesteckt(self) -> bool:
        return self.emulator is not None and self.emulator.tape_installed()

    def _startordner(self) -> str:
        return self._ordner or str(paths.user_disks_dir())

    def aktualisieren(self) -> None:
        gesteckt = self._gesteckt()
        for lw in self.laufwerke:
            lw.aktualisieren(gesteckt)

    def sichern(self, emulator=None) -> None:
        """Gebundene Dateien jetzt zurückschreiben (Beenden, Neuaufbau der Maschine)."""
        emu = emulator or self.emulator
        if emu is None or not emu.tape_installed():
            return
        for n in LAUFWERKE:
            emu.tape_flush(n)

    # ── Konfiguration ────────────────────────────────────────────────────────

    def zustand_lesen(self) -> dict:
        """``{"laufwerk1": {datei, schutz_a, schutz_b} | None, "laufwerk2": …}``.

        Eine Kassette ohne Datei (``Neue``) lässt sich nicht merken.
        """
        z = {"laufwerk1": None, "laufwerk2": None}
        if not self._gesteckt():
            return z
        for n in LAUFWERKE:
            st = self.emulator.tape_status(n) or {}
            if st.get("loaded") and st.get("file"):
                z[f"laufwerk{n + 1}"] = {
                    "datei": st["file"],
                    "schutz_a": bool(self.emulator.tape_protected(n, 0)),
                    "schutz_b": bool(self.emulator.tape_protected(n, 1)),
                }
        return z

    def zustand_anwenden(self, z) -> List[str]:
        """Beide Laufwerke nach *z* belegen; Hinweise (Statuszeile) zurück.

        Anders als beim Lochstreifen wird eine FEHLENDE Datei angelegt (``tape_load``
        macht daraus eine leere Kassette) — eine Kassette ist ohnehin ein Medium, das
        man neu bespielt.
        """
        hinweise: List[str] = []
        if not self._gesteckt() or not isinstance(z, dict):
            return hinweise
        emu = self.emulator
        for n in LAUFWERKE:
            e = z.get(f"laufwerk{n + 1}")
            if isinstance(e, dict) and e.get("datei"):
                pfad = str(e["datei"])
                if not emu.tape_load(n, pfad):
                    hinweise.append(f"Kassette Laufwerk {n + 1}: {emu.tape_error()}")
                    continue
                emu.tape_protect(n, 0, bool(e.get("schutz_a")))
                emu.tape_protect(n, 1, bool(e.get("schutz_b")))
            elif (emu.tape_status(n) or {}).get("loaded"):
                emu.tape_eject(n)
        self.aktualisieren()
        return hinweise
