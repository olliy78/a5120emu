"""Physisches Diskettenlaufwerk am Greaseweazle — Auswahl und Sitzung.

Gemeinsam von Emulator und k1520DiskTool benutzt, damit „physisches Laufwerk
einlegen" in beiden Programmen dasselbe bedeutet und gleich aussieht:

* :class:`PhysicalDiskDialog` — Adapter, Laufwerk am Kabel, Zellrate, Schreibrecht.
* :class:`PhysicalSession`   — hält Sync **und** Arbeitsfaden zusammen und macht das
  Abmelden zu einem einzigen Aufruf.  Die Reihenfolge dabei ist nicht beliebig:
  erst ``worker.stop()`` (schickt ``shutdown`` und wartet auf den Faden), **dann**
  ``sync.close()`` — andersherum stirbt dem Faden der Synchronisierer unter der Hand.

Das Paket ``greaseweazle`` ist **freiwillig**.  Fehlt es (oder der Adapter), sagt
:func:`verfuegbarkeit` warum, und der Aufrufer sperrt seinen Menüpunkt mit genau
diesem Satz als Hinweis — statt beim Anklicken mit einem Rückverfolgungsprotokoll
abzustürzen.

Siehe doc/design/14_physische_diskette.md §11/§12.
"""

from __future__ import annotations

from typing import Optional, Tuple

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QLabel, QVBoxLayout)

#: Laufwerk am Kabel → Beschriftung.  ``a``/``b`` ist das PC-Kabel mit Verdrehung,
#: ``0``…``3`` die Shugart-Zählung (K5601 & Co. hängen dort).
LAUFWERKE = [("a", "A  (PC-Kabel, mit Verdrehung)"),
             ("b", "B  (PC-Kabel)"),
             ("0", "0  (Shugart)"),
             ("1", "1  (Shugart)"),
             ("2", "2  (Shugart)"),
             ("3", "3  (Shugart)")]

#: Zellrate → Beschriftung.  Die Rate gehört zur Diskette, nicht zum Adapter.
RATEN = [(250, "250 kbit/s — 5,25″ doppelte Dichte (K5601, K5600.x)"),
         (500, "500 kbit/s — 8″ MFM (MF6400) und HD")]


def verfuegbarkeit() -> Tuple[bool, str]:
    """Kann ein physisches Laufwerk benutzt werden?

    Returns:
        ``(True, "")`` oder ``(False, grund)`` — der Grund ist für den Anwender
        geschrieben und taugt als Tooltip einer gesperrten Aktion.
    """
    try:
        from app.gw import verfuegbar
    except Exception as e:                       # noqa: BLE001
        return False, f"Die Greaseweazle-Anbindung ließ sich nicht laden: {e}"
    if not verfuegbar():
        return False, ("Das Paket „greaseweazle“ ist nicht installiert.\n"
                       "Installation:\n"
                       '  pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"')
    return True, ""


class PhysicalSession:
    """Eine laufende Sitzung an einem echten Laufwerk: Sync + Arbeitsfaden.

    Attributes:
        sync: der :class:`app.gw.Sync` — sein ``handle`` wird angemeldet.
        drive: Laufwerk am Kabel (``a``…``3``), nur zur Anzeige.
        writable: ob auf die echte Diskette geschrieben werden darf.
    """

    #: Text der letzten Defektmeldung, die dem Bediener schon gezeigt wurde —
    #: damit dieselbe Schadstelle nicht bei jedem Zeitgeber-Tick ein Fenster öffnet.
    gemeldete_defekte: str = ""

    def __init__(self, sync, worker, device, *, drive: str, writable: bool,
                 cell_rate_kbps: int):
        self.sync = sync
        self.worker = worker
        self.device = device
        self.drive = drive
        self.writable = writable
        self.cell_rate_kbps = cell_rate_kbps
        self._zu = False

    @classmethod
    def start(cls, *, drive: str = "a", cell_rate_kbps: int = 250,
              num_cyls: int = 80, num_heads: int = 2, writable: bool = False,
              read_ahead: bool = True, for_emulator: bool = False,
              rpm: int = 300, verify_writes: bool = True) -> "PhysicalSession":
        """Adapter öffnen, Sync anlegen, Arbeitsfaden starten.

        Es wird dabei **nichts gelesen** — Spuren kommen einzeln, sobald jemand sie
        anfasst.  Der Rückgabewert ist bereits „scharf"; anzumelden ist danach nur
        noch ``session.sync``.

        Raises:
            app.gw.GwFehler: kein Paket, kein Adapter — Text ist anwendertauglich.
        """
        from app.gw import Sync, TrackWorker, open_device

        device = open_device(drive, cell_rate_kbps=cell_rate_kbps)
        sync = Sync(num_cyls=num_cyls, num_heads=num_heads,
                    cell_rate_kbps=cell_rate_kbps, rpm=rpm, writable=writable,
                    read_ahead=read_ahead, for_emulator=for_emulator,
                    verify_writes=verify_writes)
        worker = TrackWorker(sync, device)
        try:
            worker.start()
        except Exception:
            sync.shutdown()
            sync.close()
            raise
        return cls(sync, worker, device, drive=drive, writable=writable,
                   cell_rate_kbps=cell_rate_kbps)

    @property
    def handle(self):
        """Rohes Handle für ``mount_physical`` / ``open_physical``."""
        return self.sync.handle

    def stats(self):
        """Momentaufnahme für die Anzeige (``None``, wenn schon geschlossen)."""
        if self._zu:
            return None
        try:
            return self.sync.stats
        except Exception:                        # noqa: BLE001
            return None

    def status_text(self) -> str:
        """Eine Zeile für die Oberfläche: Füllstand und was gerade läuft."""
        st = self.stats()
        if st is None:
            return ""
        text = f"{st.tracks_known} von {st.tracks_total} Spuren gelesen"
        if st.tracks_dirty:
            text += f" · {st.tracks_dirty} zu schreiben"
        if st.busy:
            was = {1: "liest", 2: "schreibt", 4: "prüft"}.get(st.busy_kind, "arbeitet an")
            text += f" · {was} {st.busy_cyl}/{st.busy_head}"
        if st.tracks_failed:
            text += f" · {st.tracks_failed} unlesbar"
        if st.tracks_defect:
            text += f" · {st.tracks_defect} nicht beschreibbar"
        return text

    # ── Schadstellen ────────────────────────────────────────────────────────

    @property
    def defect_tracks(self) -> str:
        """Die schadhaften Spuren als Text, z. B. ``"5/1, 12/0"`` (leer = keine)."""
        if self._zu:
            return ""
        try:
            return self.sync.defect_tracks
        except Exception:                        # noqa: BLE001
            return ""

    def neue_defekte(self) -> str:
        """Schadstellen, die dem Bediener **noch nicht** gemeldet wurden.

        Der Aufrufer kann diese Methode bei jedem Zeitgeber-Tick rufen; sie liefert
        nur beim ersten Mal (und bei jeder *neu* hinzugekommenen Spur) einen Text.
        """
        jetzt = self.defect_tracks
        if not jetzt or jetzt == self.gemeldete_defekte:
            return ""
        self.gemeldete_defekte = jetzt
        return jetzt

    def defekt_meldung(self, spuren: str) -> str:
        """Der Text, den der Bediener zu sehen bekommt — samt Ausweg."""
        return (
            f"Die Diskette liess sich an dieser Stelle nicht beschreiben:\n\n"
            f"    Spur {spuren}\n\n"
            "Geschrieben wurde es zweimal und danach zurückgelesen — beide Male kam "
            "etwas anderes zurück.  Das ist eine Schadstelle der Diskette, kein "
            "Fehler des Programms.\n\n"
            "Das Abbild im Speicher ist unversehrt.  Retten Sie es, solange dieses "
            "Fenster offen ist:\n"
            "  • „Speichern unter…“ schreibt es in eine Datei, oder\n"
            "  • legen Sie eine fehlerfreie Diskette ein und wählen Sie "
            "„Diskette neu beschreiben“.")

    def rewrite_all(self) -> int:
        """Alle bekannten Spuren erneut zum Schreiben einstellen (neue Diskette).

        Returns:
            Zahl der eingestellten Spuren (0 = nichts bekannt / nicht schreibbar).
        """
        if self._zu:
            return 0
        self.gemeldete_defekte = ""      # auf der neuen Diskette gilt nichts von vorher
        return self.sync.rewrite_all()

    def close(self, timeout: float = 30.0) -> None:
        """Sitzung beenden — **erst** den Faden, dann den Synchronisierer.

        Ausstehende Änderungen werden vorher zurückgeschrieben; das kann dauern
        (je Spur eine knappe Sekunde) und ist der Grund, warum das Abmelden
        überhaupt wartet: eine verlorene Änderung fiele sonst niemandem auf.
        """
        if self._zu:
            return
        self._zu = True
        try:
            if self.writable:
                self.sync.flush(int(timeout * 1000))
        except Exception:                        # noqa: BLE001
            pass
        try:
            self.worker.stop(timeout)
        finally:
            self.sync.close()


class PhysicalDiskDialog(QDialog):
    """Abfrage vor dem Einlegen: Laufwerk, Zellrate, Schreibrecht.

    Die Geometrie (Zylinder × Köpfe) kommt **nicht** von hier: im Emulator steht sie
    im Laufwerksprofil des Steckplatzes, im DiskTool wählt sie der Bediener über den
    Laufwerkstyp.  Der Aufrufer gibt sie deshalb als Vorgabe herein.
    """

    def __init__(self, parent=None, *, num_cyls: int = 80, num_heads: int = 2,
                 drive_label: str = "", allow_write: bool = True):
        super().__init__(parent)
        self.setWindowTitle("Physisches Laufwerk einlegen")
        self._num_cyls = num_cyls
        self._num_heads = num_heads

        layout = QVBoxLayout(self)
        kopf = QLabel(
            "Eine echte Diskette in einem echten Laufwerk am "
            "<b>Greaseweazle</b>.<br>Es wird nichts vorab eingelesen — die Spuren "
            "kommen einzeln, sobald sie gebraucht werden.")
        kopf.setWordWrap(True)
        layout.addWidget(kopf)

        form = QFormLayout()
        self._laufwerk = QComboBox()
        for wert, text in LAUFWERKE:
            self._laufwerk.addItem(text, wert)
        form.addRow("Laufwerk am Kabel:", self._laufwerk)

        self._rate = QComboBox()
        for wert, text in RATEN:
            self._rate.addItem(text, wert)
        form.addRow("Zellrate:", self._rate)

        form.addRow("Geometrie:", QLabel(
            f"{num_cyls} Spuren × {num_heads} Seite(n)"
            + (f"  ({drive_label})" if drive_label else "")))

        self._vorauslesen = QCheckBox(
            "Unbenutzte Spuren im Hintergrund mitlesen")
        self._vorauslesen.setChecked(True)
        self._vorauslesen.setToolTip(
            "Füllt das Abbild in Ruhephasen, damit ein späterer Zugriff nicht auf "
            "das Laufwerk warten muss.  Angeforderte Spuren haben immer Vorrang.")
        form.addRow("", self._vorauslesen)

        self._verify = QCheckBox("Geschriebene Spuren zurücklesen und vergleichen")
        self._verify.setChecked(True)
        self._verify.setToolTip(
            "Findet Schadstellen der Diskette.  Der Verify-Lauf des Gastsystems "
            "(FORMAT) prüft nur das Speicherabbild gegen sich selbst und sieht sie "
            "nie — dieses Prüf-Lesen geht gegen die Scheibe.\n"
            "Kostet die doppelte Zeit je Schreibvorgang.")
        form.addRow("", self._verify)

        self._schreiben = QCheckBox("Auf die echte Diskette schreiben")
        self._schreiben.setChecked(False)
        self._schreiben.setEnabled(allow_write)
        self._schreiben.setToolTip(
            "Ohne Haken bleibt die Diskette unangetastet; Änderungen leben nur im "
            "Abbild.  Mit Haken werden geänderte Spuren zurückgeschrieben — auf ein "
            "Original, von dem es vielleicht keine zweite Kopie gibt.")
        form.addRow("", self._schreiben)
        layout.addLayout(form)

        warnung = QLabel(
            "⚠ Schreiben verändert die eingelegte Diskette dauerhaft.")
        warnung.setStyleSheet("color: #c08a00;")
        warnung.setWordWrap(True)
        layout.addWidget(warnung)

        knoepfe = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel,
                                   Qt.Horizontal, self)
        knoepfe.button(QDialogButtonBox.Ok).setText("Einlegen")
        knoepfe.accepted.connect(self.accept)
        knoepfe.rejected.connect(self.reject)
        layout.addWidget(knoepfe)

    def auswahl(self) -> dict:
        """Die getroffene Auswahl als Argumente für :meth:`PhysicalSession.start`."""
        return {
            "drive": self._laufwerk.currentData(),
            "cell_rate_kbps": int(self._rate.currentData()),
            "num_cyls": self._num_cyls,
            "num_heads": self._num_heads,
            "writable": self._schreiben.isChecked(),
            "read_ahead": self._vorauslesen.isChecked(),
            "verify_writes": self._verify.isChecked(),
        }

    @classmethod
    def frage(cls, parent=None, **kwargs) -> Optional[dict]:
        """Dialog zeigen; ``None``, wenn abgebrochen wurde."""
        d = cls(parent, **kwargs)
        if d.exec() != QDialog.Accepted:
            return None
        return d.auswahl()


def mit_fortschritt(parent, sitzung: PhysicalSession, arbeit, *,
                    titel: str = "Physisches Laufwerk",
                    text: str = "Diskette wird gelesen…"):
    """Eine **lange** Arbeit am echten Laufwerk mit Fortschrittsanzeige ausführen.

    Das Öffnen einer physischen Diskette liest die ganze Scheibe (die
    Formaterkennung sieht sich jede Spur an) — rund anderthalb Minuten.  Im
    Oberflächenfaden wäre das ein eingefrorenes Fenster, deshalb läuft @p arbeit in
    einem eigenen Faden, während hier der Füllstand aus
    :meth:`PhysicalSession.stats` angezeigt wird
    (doc/design/14_physische_diskette.md §11.2).

    Args:
        arbeit: parameterlose Funktion; ihr Rückgabewert wird durchgereicht.

    Returns:
        ``(ergebnis, fehler)`` — genau eines von beiden ist ``None``.  Bricht der
        Bediener ab, sind **beide** ``None`` und die Sitzung ist beendet.
    """
    import threading

    from PySide6.QtCore import QEventLoop, QTimer
    from PySide6.QtWidgets import QProgressDialog

    ergebnis = {}

    def lauf():
        try:
            ergebnis["wert"] = arbeit()
        except BaseException as e:               # noqa: BLE001 — bis in die Anzeige
            ergebnis["fehler"] = e

    faden = threading.Thread(target=lauf, name="gw-arbeit", daemon=True)
    st = sitzung.stats()
    gesamt = st.tracks_total if st else 0

    dlg = QProgressDialog(text, "Abbrechen", 0, gesamt or 0, parent)
    dlg.setWindowTitle(titel)
    dlg.setMinimumDuration(0)
    dlg.setAutoClose(False)
    dlg.setAutoReset(False)
    dlg.setValue(0)

    schleife = QEventLoop(parent)
    abgebrochen = {"ja": False}

    def tick():
        if not faden.is_alive():
            schleife.quit()
            return
        s = sitzung.stats()
        if s is not None:
            dlg.setValue(min(s.tracks_known, dlg.maximum()))
            dlg.setLabelText(f"{text}\n{sitzung.status_text()}")
        if dlg.wasCanceled() and not abgebrochen["ja"]:
            # Der Kern löst jeden Wartenden; die Arbeit endet dann von selbst.
            abgebrochen["ja"] = True
            dlg.setLabelText("Wird abgebrochen…")
            sitzung.sync.shutdown()

    timer = QTimer(parent)
    timer.timeout.connect(tick)
    timer.start(200)
    faden.start()
    dlg.show()
    schleife.exec()
    timer.stop()
    faden.join(5.0)
    dlg.close()

    if abgebrochen["ja"]:
        return None, None
    if "fehler" in ergebnis:
        return None, ergebnis["fehler"]
    return ergebnis.get("wert"), None
