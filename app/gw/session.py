"""Eine laufende Sitzung an einem echten Laufwerk — Sync **und** Arbeitsfaden.

Herausgeloest aus ``app/ui/physical_disk.py`` (2026-08-17), weil ausser der
Oberflaeche auch die **Kommandozeile** (``k1520disktool --physical``) eine Sitzung
aufmacht — und die darf Qt nicht brauchen.  Der Dialog blieb dort, das Stueck ohne
Anzeige liegt hier; ``app.ui.physical_disk`` reicht beides weiter, damit vorhandene
Aufrufe unveraendert gelten.

* :func:`verfuegbarkeit` — kann ueberhaupt ein Laufwerk benutzt werden?  Das Paket
  ``greaseweazle`` ist **freiwillig**; fehlt es, sagt diese Funktion warum, und der
  Aufrufer sperrt seinen Menuepunkt bzw. bricht mit genau diesem Satz ab — statt beim
  Anfassen mit einem Rueckverfolgungsprotokoll abzustuerzen.
* :class:`PhysicalSession` — haelt Sync und Arbeitsfaden zusammen und macht das
  Abmelden zu einem einzigen Aufruf.  Die Reihenfolge dabei ist nicht beliebig:
  erst ``worker.stop()`` (schickt ``shutdown`` und wartet auf den Faden), **dann**
  ``sync.close()`` — andersherum stirbt dem Faden der Synchronisierer unter der Hand.

Siehe doc/design/14_physische_diskette.md §11/§12.
"""

from __future__ import annotations

import sys
from typing import Tuple


#: Laufwerk am Kabel -> Beschriftung.  ``a``/``b`` ist das PC-Kabel mit Verdrehung,
#: ``0``…``3`` die Shugart-Zaehlung (K5601 & Co. haengen dort).
LAUFWERKE = [("a", "A  (PC-Kabel, mit Verdrehung)"),
             ("b", "B  (PC-Kabel)"),
             ("0", "0  (Shugart)"),
             ("1", "1  (Shugart)"),
             ("2", "2  (Shugart)"),
             ("3", "3  (Shugart)")]

#: Zellrate -> Beschriftung.  Die Rate gehoert zur Diskette, nicht zum Adapter.
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
        # Der Befehl nennt DIESEN Interpreter, nicht das blosse `pip`: das Programm
        # läuft in einem venv, ein nacktes `pip install` ginge daneben (ins System,
        # und auf neueren Distributionen scheitert es dort an PEP 668).  Dazu der
        # Pfad, damit man sieht, WOHIN installiert wird — steckt man versehentlich
        # im venv eines anderen Projekts, ist genau das die Auskunft, die fehlt.
        #
        # In einer Installation AUS DEM PAKET darf dieser Fall gar nicht auftreten:
        # der Installer spielt ein mitgeliefertes Rad ein (packaging/gw_pins.txt).
        # Steht es trotzdem hier, ist beim Einrichten etwas schiefgegangen — deshalb
        # der Satz davor, sonst sucht der Anwender den Fehler bei sich.
        return False, ("Das Paket „greaseweazle“ ist in dieser Python-Umgebung nicht "
                       "installiert:\n"
                       f"  {sys.prefix}\n\n"
                       "In einer Installation aus dem Paket gehört es dazu — dann ist "
                       "beim Einrichten etwas schiefgegangen.\n"
                       "Von Hand nachholen:\n"
                       f'  {sys.executable} -m pip install '
                       '"git+https://github.com/keirf/greaseweazle.git@v1.23"')
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
                 cell_rate_kbps: int, num_cyls: int = 0, num_heads: int = 0):
        self.sync = sync
        self.worker = worker
        self.device = device
        self.drive = drive
        self.writable = writable
        self.cell_rate_kbps = cell_rate_kbps
        # Die Geometrie, mit der eingelegt wurde.  `Stats` trägt nur die Spurzahl
        # als Produkt (tracks_total) — wer wissen will, wie viele KÖPFE es sind,
        # bekäme sie von dort nicht zurückgerechnet.
        self.num_cyls = num_cyls
        self.num_heads = num_heads
        self._zu = False

    @classmethod
    def start(cls, *, drive: str = "a", cell_rate_kbps: int = 250,
              num_cyls: int = 80, num_heads: int = 2, writable: bool = False,
              read_ahead: bool = True, for_emulator: bool = False,
              rpm: int = 300, verify_writes: bool = True,
              double_step: bool = False) -> "PhysicalSession":
        """Adapter öffnen, Sync anlegen, Arbeitsfaden starten.

        Es wird dabei **nichts gelesen** — Spuren kommen einzeln, sobald jemand sie
        anfasst.  Der Rückgabewert ist bereits „scharf"; anzumelden ist danach nur
        noch ``session.sync``.

        Raises:
            app.gw.GwFehler: kein Paket, kein Adapter — Text ist anwendertauglich.
        """
        from app.gw import Sync, TrackWorker, open_device

        device = open_device(drive, cell_rate_kbps=cell_rate_kbps,
                             double_step=double_step)
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
                   cell_rate_kbps=cell_rate_kbps, num_cyls=num_cyls,
                   num_heads=num_heads)

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
