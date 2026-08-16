"""Der Arbeitsfaden zwischen Kern und Laufwerk.

Er tut genau eines: Aufträge abholen, ausführen, zurückmelden.  Die Reihenfolge
bestimmt **der Kern** (drei Prioritäten, doc/design/14_physische_diskette.md §5) —
dieser Faden entscheidet nichts, er arbeitet ab.

::

    from app.gw import Sync, open_device, TrackWorker

    sync = Sync(num_cyls=80, num_heads=2, writable=False)
    worker = TrackWorker(sync, open_device("a"))
    worker.start()
    #   … k1520_mount_physical(emu, 0, sync.handle, True)  bzw.
    #   … k1520d_open_physical(sync.handle, "", True)
    worker.stop()          # shutdown + join, danach sync.close()

Das ``device`` ist austauschbar: alles, was ``read_track``/``write_track`` mit
HFE-Bitzellen beherrscht, taugt — daher lässt sich der Faden ohne Adapter prüfen
(``tests/python/test_gw_worker.py`` bedient ihn aus einer ``.hfe``-Datei).
"""

from __future__ import annotations

import logging
import threading
from typing import Optional, Protocol

from .sync import JobKind, Sync

log = logging.getLogger(__name__)


class TrackDevice(Protocol):
    """Was der Arbeitsfaden von einem Laufwerk braucht — mehr nicht."""

    def read_track(self, cyl: int, head: int) -> tuple[bytes, int]: ...
    def write_track(self, cyl: int, head: int, cells: bytes, bitcells: int) -> None: ...


class TrackWorker:
    """Bedient einen :class:`Sync` aus einem :class:`TrackDevice`.

    Attributes:
        errors: Zahl gescheiterter Aufträge seit dem Start.
        last_error: Text des letzten Fehlschlags ("" = keiner).
    """

    def __init__(self, sync: Sync, device: TrackDevice, *, poll_ms: int = 1000):
        self._sync = sync
        self._dev = device
        self._poll_ms = poll_ms
        self._faden: Optional[threading.Thread] = None
        self.errors = 0
        self.last_error = ""

    # ── Steuerung ───────────────────────────────────────────────────────────

    def start(self) -> None:
        if self._faden is not None:
            raise RuntimeError("Arbeitsfaden läuft bereits")
        self._faden = threading.Thread(target=self._schleife, name="gw-worker",
                                       daemon=True)
        self._faden.start()

    def stop(self, timeout: float = 10.0) -> None:
        """``shutdown`` im Kern anstoßen und den Faden abwarten.

        Danach — und **erst** danach — darf ``sync.close()`` gerufen werden.
        """
        self._sync.shutdown()
        if self._faden is not None:
            self._faden.join(timeout)
            self._faden = None
        abmelden = getattr(self._dev, "deselect", None)
        if abmelden is not None:
            try:
                abmelden()
            except Exception as e:                       # noqa: BLE001
                log.warning("Laufwerk abmelden scheiterte: %s", e)

    @property
    def running(self) -> bool:
        return self._faden is not None and self._faden.is_alive()

    # ── Die Schleife ────────────────────────────────────────────────────────

    def _schleife(self) -> None:
        leerlauf = getattr(self._dev, "tick", None)
        while True:
            auftrag = self._sync.take_job(self._poll_ms)
            if auftrag is None:                  # nichts zu tun
                if leerlauf is not None:
                    leerlauf()                   # Motornachlauf
                continue
            if auftrag.kind == JobKind.STOP:
                return

            try:
                # VERIFY ist fuer diesen Faden dasselbe wie READ: Spur lesen, Bitzellen
                # abliefern.  Ob damit verglichen oder uebernommen wird, entscheidet
                # der Kern (doc/design/14_physische_diskette.md §7.1).
                if auftrag.kind in (JobKind.READ, JobKind.VERIFY):
                    zellen, bitcells = self._dev.read_track(auftrag.cyl, auftrag.head)
                    self._sync.complete_read(auftrag.id, zellen, bitcells)
                else:
                    zellen, bitcells = self._sync.fetch_write(auftrag.id)
                    self._dev.write_track(auftrag.cyl, auftrag.head, zellen, bitcells)
                    self._sync.complete_write(auftrag.id)
            except Exception as e:               # noqa: BLE001 — gw wirft vielerlei
                # Ein Auftrag darf NIE unabgeschlossen liegenbleiben, sonst wartet der
                # Vordergrund bis zur Frist (§9, Regel 3).
                self.errors += 1
                self.last_error = f"Spur {auftrag.cyl}/{auftrag.head}: {e}"
                log.warning("Auftrag gescheitert — %s", self.last_error)
                self._sync.fail(auftrag.id, str(e))
