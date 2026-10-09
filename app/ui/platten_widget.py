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
* **Neu angelegt wird standardmäßig OHNE PAR-Sektor** (`:unformatiert`, Merkposten p8000 Nr. 48):
  der Hardwaretest endet mit ERROR 52 39, MON16 kommt zum Prompt ``*``.  Eine Platte MIT
  Parametersatz, aber leerem Block 0 (Wahl „Formatiert mit Parametersatz“ oder die frühere
  Standardplatte) schickt MON16 nach dem Hardwaretest in den AUTOBOOT — kein ``*``
  (Gastverhalten, Nr. 21/54); der Kasten erkennt das (:func:`ohne_startblock`) und sagt es.
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
#: Dateinamenkürzel je Typ (Kern: `winchester::typen()`, geprüft durch `k1520_hd_typ_kuerzel`):
#: Plattenabbilder heißen ``<name>.<kürzel>.img`` (P24) — die Endung bleibt ``.img``, damit
#: DiskTool und Dateimanager sie weiter erkennen.
KUERZEL = {"K5504.50": "k5504", "D5126": "d5126", "D5146": "d5146", "VS": "vs"}
#: WDC-Fassungen, die noch nicht belegt sind (3.x-Spurformat, wdc_firmware.md §12 Nr. 11).
EXPERIMENTELL = ("4.0.05", "3.4.05")
STANDARD_TYP = "K5504.50:unformatiert"   # Vorgabe: Laufwerk wie neu, ohne Parametersatz
STANDARD_DATEI = "p8000_platte.img"
INHALT_UNFORMATIERT = "unformatiert"
INHALT_MIT_PAR = "par"


def typ_mit_inhalt(typ: str, inhalt: str) -> str:
    """Typname für ``hd_create``: ``<typ>:unformatiert`` (kein Parametersatz) bzw. ``<typ>``."""
    return f"{typ}:unformatiert" if inhalt == INHALT_UNFORMATIERT else typ


UNIT = 0          # nur Laufwerk 0 — die Maschine führt drei, der Kasten bedient das erste


def rom_typ(firmware: str) -> str:
    """Plattentyp, den das EPROM der WDC-Firmware festlegt (``""`` = 4.2, laufwerksunabhängig)."""
    from app.core_binding import k1520
    return k1520.hd_rom_typ(firmware or "")


def typ_masse(name: str):
    """(Zylinder, Köpfe, Sektoren) des Typs *name* aus :data:`TYPEN`, sonst ``None``."""
    for n, _text, z, k, sek in TYPEN:
        if n == name:
            return z, k, sek
    return None


def leerzeichen(n: int) -> str:
    """47185920 → ``"47 185 920"`` (wie die Meldungen des Kerns)."""
    return f"{n:,}".replace(",", " ")


def kuerzel_aus_dateiname(pfad: str) -> str:
    """``platte.k5504.img`` → ``k5504``; ``""`` bei einer Datei ohne Typkürzel."""
    name = os.path.basename(pfad).lower()
    if not name.endswith(".img"):
        return ""
    teile = name[:-4].rsplit(".", 1)
    return teile[1] if len(teile) == 2 and teile[1] in KUERZEL.values() else ""


def dateiname_mit_kuerzel(pfad: str, typ: str) -> str:
    """*pfad* auf ``<name>.<kürzel>.img`` bringen: fehlt die Endung, wird sie angehängt, ein anderes
    Typkürzel ersetzt, ``.img`` ohne Kürzel davor ergänzt."""
    kz = KUERZEL.get(typ, "")
    if not kz:
        return pfad if "." in os.path.basename(pfad) else pfad + ".img"
    ordner, name = os.path.split(pfad)
    if kuerzel_aus_dateiname(name):
        name = name[:-4].rsplit(".", 1)[0] + ".img"
    elif not name.lower().endswith(".img"):
        name += ".img"
    return os.path.join(ordner, name[:-4] + f".{kz}.img")


def abweisung(pfad: str, firmware: str) -> str:
    """Passt das Abbild *pfad* zum EPROM der Firmware?  ``""`` = ja (oder 4.2: der Parametersatz der
    Platte entscheidet), sonst der Klartext.  Bis 4.0 legt das EPROM das Laufwerk fest."""
    rom = rom_typ(firmware)
    masse = typ_masse(rom) if rom else None
    if not rom or masse is None:
        return ""
    z, k, sek = masse
    soll = z * k * sek * 512
    kopf = f"Firmware {firmware} gehört zu {rom} ({z}/{k}/{sek} = {leerzeichen(soll)} B)"
    kz = kuerzel_aus_dateiname(pfad)
    if kz and kz != KUERZEL[rom]:
        return f"{kopf}; der Dateiname nennt aber „{kz}“"
    try:
        ist = os.path.getsize(pfad)
    except OSError:
        return ""
    if ist != soll:
        return f"{kopf}, die Datei hat {leerzeichen(ist)} B"
    return ""


#: Hinweis zu einer Platte mit Parametersatz, aber leerem Block 0 (:func:`ohne_startblock`).
HINWEIS_OHNE_START = (
    "Die Platte hat einen Parametersatz, aber keinen Urlader (Block 0 leer): bestehen WDC und Platte "
    "den 16-Bit-Hardwaretest, startet MON16 nach „Press NMI“ und MAXSEG diesen Block (AUTOBOOT) — "
    "es kommt kein „*“.  Zum Prompt „*“: bei „Press NMI“ statt des NMI-Tasters RETURN drücken "
    "(Antwort „?“, dann „*“), dort weiter mit O U / boot / ud(0,0)sa.format.  Oder die Platte abtrennen.")


def ohne_startblock(pfad: str) -> bool:
    """Trägt das Abbild einen gültigen Parametersatz (``PARMTR``), aber in WDC-Block 0 nur ein
    einziges Füllbyte (E5 einer neu angelegten, 00/E5 einer formatierten Platte ohne WEGA)?

    Dann startet MON16 nach einem fehlerfreien Hardwaretest diesen Block (``AUTOBOOT`` in
    ``p.boot.s`` → ``DSK_BOOT`` liest Block 0 nach ``%8000`` und springt hin) und bleibt ohne
    Prompt stehen — Gastverhalten wie am Gerät (Merkposten p8000 Nr. 21/54).  Lage von Block 0 wie
    im DiskTool (`core/filesystem/wega/wega_platte.cpp::offsetVon`): Zylinder 1, Kopf 0, jede
    Defektspur der BTT davor verschiebt um eine Spur.  Unlesbares/Fremdes ⇒ ``False``.
    """
    try:
        with open(pfad, "rb") as f:
            s0 = f.read(512)
            if len(s0) < 512 or s0[0:6] != b"DEFEKT" or s0[256:262] != b"PARMTR":
                return False
            koepfe, sek = s0[279], s0[280]
            if not (2 <= koepfe <= 16 and sek in (17, 18)):
                return False
            n = (s0[6] | s0[7] << 8) // 3
            defekte = sorted(((s0[8 + 3 * i] << 8 | s0[9 + 3 * i]) * koepfe + s0[10 + 3 * i])
                             for i in range(min(n, 40)))
            spur = koepfe                      # Zylinder 1, Kopf 0 = WDC-Block 0
            for d in defekte:
                if d <= spur:
                    spur += 1
            f.seek(spur * sek * 512)
            b0 = f.read(512)
    except OSError:
        return False
    return len(b0) == 512 and b0.count(b0[0]) == 512


def groesse_text(zyl: int, kopf: int, sektoren: int) -> str:
    mib = zyl * kopf * sektoren * 512 / (1024 * 1024)
    return f"{mib:.0f} MiB"


class PlattenDialog(QDialog):
    """Typ der neuen Platte erfragen (der Pfad kommt danach aus dem Speichern-Dialog)."""

    def __init__(self, parent=None, firmware: str = "4.2"):
        super().__init__(parent)
        self.setWindowTitle("Neue Platte anlegen")
        self.firmware = firmware
        lay = QVBoxLayout(self)
        form = QFormLayout()
        self.typ = QComboBox()
        for name, text, z, k, s in TYPEN:
            self.typ.addItem(f"{text} — {groesse_text(z, k, s)}", name)
        form.addRow("Plattentyp:", self.typ)
        # Bis WDC 4.0 steckt das Laufwerk im EPROM: die Wahl entfällt, der Typ des ROMs steht fest.
        self.rom = rom_typ(firmware)
        self.typ_hinweis = QLabel()
        self.typ_hinweis.setWordWrap(True)
        if self.rom:
            self.typ.setCurrentIndex(max(0, self.typ.findData(self.rom)))
            self.typ.setEnabled(False)
            self.typ.setToolTip(f"Die Firmware {firmware} legt das Laufwerk fest "
                                "(Parameter im EPROM eingebrannt).")
            self.typ_hinweis.setText(f"Firmware {firmware}: das EPROM bestimmt das Laufwerk "
                                     f"({self.rom}); die Typwahl ist gesperrt.")
            form.addRow("", self.typ_hinweis)
        self.dateiname = QLabel()
        form.addRow("Dateiendung:", self.dateiname)
        self.typ.currentIndexChanged.connect(self._dateiendung)
        self._dateiendung()
        # Inhalt: Vorgabe = unformatiert, wie ein neues Laufwerk (kein Parametersatz).  Mit
        # Parametersatz aber leer startet der 16-Bit-Monitor die E5-Bytes als Bootprogramm.
        self.inhalt = QComboBox()
        self.inhalt.addItem("Unformatiert, wie ein neues Laufwerk (Standard) — "
                            "mit sa.format formatieren", INHALT_UNFORMATIERT)
        self.inhalt.addItem("Formatiert mit Parametersatz (K5504.50, leer)", INHALT_MIT_PAR)
        form.addRow("Inhalt:", self.inhalt)
        lay.addLayout(form)
        lay.addWidget(QLabel("Standard: Die Platte hat noch keinen Parametersatz; der Monitor "
                             "bleibt bedienbar,\nformatieren mit ud(0,0)sa.format im Gast "
                             "(WEGA-Installation)."))
        tasten = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        tasten.accepted.connect(self.accept)
        tasten.rejected.connect(self.reject)
        lay.addWidget(tasten)

    def kuerzel(self) -> str:
        return KUERZEL.get(self.typ.currentData(), "")

    def _dateiendung(self):
        self.dateiname.setText(f"<name>.{self.kuerzel()}.img  (wird angehängt)")

    def typ_name(self) -> str:
        """Typname für ``hd_create``, bei „unformatiert“ mit dem Suffix des Kerns."""
        return typ_mit_inhalt(self.typ.currentData(), self.inhalt.currentData())


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
        #: WDC-Firmware der Maschine ("4.2" | "4.0.05" | "3.4.05"); bis 4.0 legt das EPROM den Typ fest.
        self.firmware = "4.2"
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
        self.knopf_neu.setToolTip("Eine neue Platte anlegen (Standard: unformatiert, ohne Parametersatz)")
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

    def set_firmware(self, firmware: str):
        """Die WDC-Fassung der Maschine (bestimmt Dialoge und Dateifilter)."""
        self.firmware = firmware or "4.2"
        self._anzeigen()

    def passt_nicht_zu(self, firmware: str) -> str:
        """Klartext, falls die angeschlossene Platte nicht zur Firmware *firmware* passt, sonst ``""``."""
        return abweisung(self._pfad, firmware) if self._pfad else ""

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
        elif ohne_startblock(self._pfad):
            self.meldung.emit(f"Platte {os.path.basename(self._pfad)}: Parametersatz, aber kein "
                              "Urlader — nach NMI kein „*“ (AUTOBOOT); bei „Press NMI“ RETURN drücken.")

    # ── Bedienung ────────────────────────────────────────────────────────────

    def anschliessen(self, pfad: str, hinweis: bool = True) -> bool:
        """Ein vorhandenes Abbild anschließen (ersetzt die bisherige Platte).

        *hinweis* = False vor dem ersten Einschalten (Start): dort gilt der Rückstell-Hinweis nicht."""
        if not self._verfuegbar or self.emulator is None:
            return False
        grund = self._kuerzel_widerspruch(pfad)
        if grund:
            self.meldung.emit(f"Platte nicht angeschlossen: {grund}")
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
        if ohne_startblock(pfad):
            self.meldung.emit(f"Platte {os.path.basename(pfad)}: Parametersatz, aber kein Urlader — "
                              "nach NMI kein „*“ (AUTOBOOT); bei „Press NMI“ RETURN drücken.")
            return True
        rom = rom_typ(self.firmware)
        if rom and not kuerzel_aus_dateiname(pfad):
            self.meldung.emit(f"Datei ohne Typkürzel: nur die Größe wurde geprüft (Firmware "
                              f"{self.firmware} = {rom}, Endung .{KUERZEL.get(rom, '')}.img).")
        return True

    def _kuerzel_widerspruch(self, pfad: str) -> str:
        """Nennt der Dateiname einen anderen Typ als das EPROM festlegt?  (Die Größe prüft der Kern.)"""
        rom = rom_typ(self.firmware)
        kz = kuerzel_aus_dateiname(pfad)
        if rom and kz and kz != KUERZEL.get(rom):
            return (f"Firmware {self.firmware} gehört zu {rom} (.{KUERZEL.get(rom)}.img), "
                    f"der Dateiname nennt „{kz}“")
        return ""

    def neu_anlegen(self, pfad: str, typ: str = STANDARD_TYP, hinweis: bool = True) -> bool:
        """Eine neue Platte anlegen (*typ* ggf. mit Suffix ``:unformatiert``, s. :func:`typ_mit_inhalt`) und anschließen (*hinweis* wie bei :meth:`anschliessen`)."""
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
            self, "Plattenabbild anschließen", start, self.anschliessen_filter())
        if pfad:
            self.anschliessen(pfad)

    def anschliessen_filter(self) -> str:
        """Dateifilter des „Anschließen…“-Dialogs: bis 4.0 nur der Typ des ROMs (``*.k5504.img``),
        sonst alle ``*.img``; „Alle Dateien“ bleibt der Ausweg (Datei ohne Kürzel)."""
        rom = rom_typ(self.firmware)
        if rom:
            return f"Plattenabbild {rom} (*.{KUERZEL.get(rom, '')}.img);;Alle Dateien (*)"
        return "Plattenabbild (*.img);;Alle Dateien (*)"

    def _neu_dialog(self):
        dlg = PlattenDialog(self, self.firmware)
        if not dlg.exec():
            return
        vorschlag = str(paths.user_disks_dir() / f"p8000_platte.{dlg.kuerzel()}.img")
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Neue Platte speichern unter", vorschlag,
            "Plattenabbild (*.img);;Alle Dateien (*)")
        if not pfad:
            return
        pfad = dateiname_mit_kuerzel(pfad, dlg.typ.currentData())
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
                "Das Programm legt keine Platte von selbst an (eine Platte mit Parametersatz, aber leer, schickt den "
                "16-Bit-Monitor nach dem Hardwaretest in den AUTOBOOT).  „Neue Platte…“ legt "
                "ein Abbild an (Standard: unformatiert wie ein neues Laufwerk, ohne "
                "Parametersatz; formatieren mit ud(0,0)sa.format im Gast), „Anschließen…“ "
                "nimmt ein vorhandenes.")
        else:
            self.name.setText(os.path.basename(self._pfad))
            self.name.setToolTip(self._pfad)
            self.hinweis.setText(
                "Neu angeschlossen: der WDC erkennt die Platte erst nach Rückstellen oder "
                "Rechner ein." if neu_angeschlossen else
                "Kein Schreibschutz; Änderungen gehen von selbst in die Datei.")
            if ohne_startblock(self._pfad):
                # Fehlerbericht „kein * nach MAXSEG" (Merkposten p8000 Nr. 54): eine früher
                # angelegte Standardplatte mit PAR, aber ohne WEGA, schickt MON16 in den AUTOBOOT.
                self.hinweis.setText(self.hinweis.text() + "\n" + HINWEIS_OHNE_START)
        if self._verfuegbar and self.firmware in EXPERIMENTELL:
            # Bis WDC 4.0 gilt das Laufwerk des EPROMs; die Fassungen sind noch nicht belegt (P24).
            self.hinweis.setText(self.hinweis.text() + f"\nWDC {self.firmware} (experimentell): das EPROM "
                                 f"legt das Laufwerk fest ({rom_typ(self.firmware)}, Endung "
                                 f".{KUERZEL.get(rom_typ(self.firmware), '')}.img).")
        self._lampe()
