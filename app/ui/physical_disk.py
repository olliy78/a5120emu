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

from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QLabel, QVBoxLayout)

# Der Teil OHNE Anzeige liegt in app/gw/session.py — die Kommandozeile
# (`k1520disktool --physical`) braucht ihn ebenfalls und darf Qt nicht laden.
# Hier weiterreichen, damit vorhandene Aufrufe unveraendert gelten.
from app.gw.session import (LAUFWERKE, RATEN,        # noqa: F401  (Weitergabe)
                            PhysicalSession, verfuegbarkeit)


class PhysicalDiskDialog(QDialog):
    """Abfrage vor dem Einlegen: Laufwerk, Zellrate, Schreibrecht.

    Die Geometrie (Zylinder × Köpfe) kommt **nicht** von hier: im Emulator steht sie
    im Laufwerksprofil des Steckplatzes, im DiskTool wählt sie der Bediener über den
    Laufwerkstyp.  Der Aufrufer gibt sie deshalb als Vorgabe herein.
    """

    def __init__(self, parent=None, *, num_cyls: int = 80, num_heads: int = 2,
                 drive_label: str = "", allow_write: bool = True,
                 writable: bool = False, titel: str = "", abbild: str = ""):
        """
        Args:
            num_cyls: **Zylinder des LAUFWERKS** (Vorbelegung des Feldes), nicht der
                Diskette — die ergibt sich daraus mit den Haken.
            abbild: Geometrie des zu schreibenden Abbilds als Text; wird angezeigt,
                damit man sieht, was man wohin schreibt.
            allow_write: darf überhaupt geschrieben werden?
            writable: Vorbelegung des Hakens.  Vorgabe **aus** — beim blossen Lesen
                soll die Diskette gar nicht in Gefahr sein.  Wer sie ausdrücklich
                überschreiben will, hat das vorher schon bestätigt; dort wäre ein
                leerer Haken eine Stolperfalle.
        """
        super().__init__(parent)
        self.setWindowTitle(titel or "Physisches Laufwerk einlegen")
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
        self._abbild_text = abbild
        self._laufwerk = QComboBox()
        for wert, text in LAUFWERKE:
            self._laufwerk.addItem(text, wert)
        form.addRow("Laufwerk am Kabel:", self._laufwerk)

        self._rate = QComboBox()
        for wert, text in RATEN:
            self._rate.addItem(text, wert)
        form.addRow("Zellrate:", self._rate)

        # Wie weit nach innen gegangen wird.  Das ist zweierlei zugleich: die Bauart
        # des Laufwerks am Kabel (die kennt nur der Bediener) UND eine Begrenzung —
        # 40 zu wählen, obwohl ein 80er dranhängt, liest genau die äusseren 40
        # Zylinder.  Genau das braucht eine 40-Spur-Diskette, die einmal in einem
        # 80-Spur-Laufwerk beschrieben wurde: dahinter steht nur noch Altbestand.
        self._laufwerkspuren = QComboBox()
        for n, text in ((80, "80 — K5601 (96 tpi)"),
                        (40, "40 — K5600.10 (48 tpi), oder nur die äußeren 40")):
            self._laufwerkspuren.addItem(text, n)
        vor = 0 if num_cyls is None or num_cyls > 45 else 1
        self._laufwerkspuren.setCurrentIndex(vor)
        self._laufwerkspuren.setToolTip(
            "So viele Zylinder werden angefahren — nicht mehr.\n"
            "Beim Lesen begrenzt das zugleich, wie weit nach innen gelesen wird: "
            "was dahinter liegt (Altbestand einer früheren Formatierung), bleibt "
            "aussen vor und stört die Erkennung nicht."
            + (f"\nAngeschlossen: {drive_label}" if drive_label else ""))
        form.addRow("Zylinder:", self._laufwerkspuren)
        if abbild:
            form.addRow("Abbild:", QLabel(abbild))

        # Doppelschritt: Spur n liegt auf Zylinder 2n.  Beim LESEN blendet das die
        # ungeraden Zylinder aus — genau dort sitzen die Altlasten einer früheren
        # 80-Spur-Formatierung.  Beim SCHREIBEN entscheidet er, ob die Diskette
        # später in einem 40- oder in einem 80-Spur-Laufwerk lesbar ist.
        self._doppelschritt = QCheckBox("Doppelschritt erzwingen (40-Spur-Diskette)")
        self._doppelschritt.setChecked(False)
        self._doppelschritt.setToolTip(
            "Spur n liegt auf dem physischen Zylinder 2n.\n\n"
            "LESEN:  nur die geraden Zylinder werden geholt — eine 40-Spur-Diskette "
            "kommt sauber herein, Altbestand auf den ungeraden bleibt aussen vor.\n"
            "SCHREIBEN:  die Diskette wird später in einem 40-Spur-Laufwerk "
            "(K5600.10) lesbar; ohne den Haken liegen die Spuren dicht "
            "hintereinander und passen zu einem 80-Spur-Laufwerk (K5601).")
        form.addRow("", self._doppelschritt)

        self._nur_seite0 = QCheckBox("Nur Seite 0")
        self._nur_seite0.setChecked(num_heads == 1)
        self._nur_seite0.setToolTip(
            "Seite 1 wird weder gelesen noch geschrieben.\n"
            "Beim Lesen halbiert das die Zeit und blendet Altbestand auf der "
            "Rückseite aus — eine einseitige Diskette wird dann auch als solche "
            "erkannt.")
        form.addRow("", self._nur_seite0)

        self._geometrie = QLabel("")
        self._geometrie.setToolTip("Was dabei als Abbild entsteht bzw. geschrieben wird")
        form.addRow("Ergibt:", self._geometrie)
        for w in (self._laufwerkspuren, self._doppelschritt, self._nur_seite0):
            (w.currentIndexChanged if hasattr(w, "currentIndexChanged")
             else w.toggled).connect(lambda *_: self._geometrie_zeigen())
        self._geometrie_zeigen()

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
        self._schreiben.setChecked(bool(writable) and allow_write)
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

    def _geometrie_zeigen(self) -> None:
        """Sagen, was aus Laufwerk + Haken folgt — Rechnen soll niemand müssen."""
        spuren, koepfe = self._geometrie_rechnen()
        wie = " (jeder zweite Zylinder)" if self._doppelschritt.isChecked() else ""
        self._geometrie.setText(f"{spuren} Spuren × {koepfe} Seite(n){wie}")

    def _geometrie_rechnen(self) -> tuple:
        """Physische Laufwerkszylinder + Haken → **logische** Geometrie des Abbilds.

        Bei Doppelschritt passt nur die Hälfte der Spuren auf dieselbe Strecke: das
        Abbild hat 40 Spuren, keine 80 mit Lücken.  Alles oberhalb des Geräts rechnet
        deshalb in logischen Spuren.
        """
        phys = int(self._laufwerkspuren.currentData())
        spuren = phys // 2 if self._doppelschritt.isChecked() else phys
        return spuren, (1 if self._nur_seite0.isChecked() else 2)

    def auswahl(self) -> dict:
        """Die getroffene Auswahl als Argumente für :meth:`PhysicalSession.start`."""
        spuren, koepfe = self._geometrie_rechnen()
        return {
            "drive": self._laufwerk.currentData(),
            "cell_rate_kbps": int(self._rate.currentData()),
            "num_cyls": spuren,
            "num_heads": koepfe,
            "double_step": self._doppelschritt.isChecked(),
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
                    text: str = "Diskette wird gelesen…",
                    ziel: Optional[int] = None,
                    was: str = "Spuren gelesen",
                    zaehler=None):
    """Eine **lange** Arbeit am echten Laufwerk mit Fortschrittsanzeige ausführen.

    Am echten Laufwerk dauert jede Spur 0,5–0,8 s.  Im Oberflächenfaden wäre das ein
    eingefrorenes Fenster, deshalb läuft @p arbeit in einem eigenen Faden, während
    hier der Füllstand aus :meth:`PhysicalSession.stats` angezeigt wird
    (doc/design/14_physische_diskette.md §11.2).

    Args:
        arbeit: parameterlose Funktion; ihr Rückgabewert wird durchgereicht.
        ziel: erwartete Spurzahl **dieser** Arbeit.  Beim Öffnen sind das die
            Sondenspuren der Formaterkennung (acht), nicht die 160 der Diskette —
            ein Balken, der gegen 160 läuft und bei 8 stehenbleibt, sagt das
            Falsche.  ``None`` = ganze Diskette.
        was: was gezählt wird, für die Beschriftung („Spuren gelesen",
            „Spuren geschrieben").  Der Text gehört zum ZÄHLER, nicht zur
            Funktion — sonst steht über einem Schreibvorgang „für die
            Formaterkennung".
        zaehler: ``stats -> Zahl``; woran der Fortschritt abzulesen ist.  Vorgabe
            sind die **gelesenen** Spuren.  Beim Schreiben ändern die sich nicht
            (geschrieben wird aus dem Speicher) — dort muss der Zähler auf die
            zurückgeschriebenen Spuren zeigen, sonst steht der Balken still.

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
    if zaehler is None:
        zaehler = lambda st: st.tracks_known      # noqa: E731
    st = sitzung.stats()
    ganze_diskette = st.tracks_total if st else 0
    # Von wo aus gezählt wird: was VOR dieser Arbeit schon geschafft war, ist kein
    # Fortschritt DIESER Arbeit.
    beginn = zaehler(st) if st else 0
    gesamt = ziel if ziel else ganze_diskette

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
            getan = max(0, zaehler(s) - beginn)
            if ziel and getan <= ziel:
                # Solange das Ziel bekannt ist, gibt es einen echten Balken.
                dlg.setMaximum(ziel)
                dlg.setValue(getan)
                dlg.setLabelText(f"{text}\n{getan} von {ziel} {was}")
            else:
                # Darüber hinaus weiss niemand, wie viele es werden (beim Öffnen: das
                # Verzeichnis, bei UDOS je Datei ein Kopfsektor irgendwo).  Deshalb
                # ein UNBESTIMMTER Balken statt einer erfundenen Zielzahl — eine
                # Umschaltung auf die Spurzahl der Diskette behauptete eine
                # Vollmessung, die gar nicht lief.
                dlg.setMaximum(0)
                dlg.setLabelText(f"{text}\n{getan} {was}")
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
