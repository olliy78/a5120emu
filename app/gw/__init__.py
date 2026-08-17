"""Physische Diskette am Greaseweazle — die Gerätehälfte der Anbindung.

Drei Bausteine, gemeinsam für Emulator und k1520DiskTool:

* :mod:`app.gw.sync`   — ctypes-Bindung an den Auftragsweg des Kerns (``k1520s_*``)
* :mod:`app.gw.device` — die einzige Stelle, die ``greaseweazle`` importiert
* :mod:`app.gw.worker` — der Arbeitsfaden, der beides verbindet
* :mod:`app.gw.session` — Sync + Faden als EIN Gegenstand, ohne Qt (Oberflaeche und
  Kommandozeile benutzen dasselbe Stueck)

Der Kern kennt Greaseweazle **nicht**; ausgetauscht werden Aufträge und HFE-Bitzellen.
Ein anderer Adapter wäre daher ein anderes ``device``, keine Kernänderung.

Voller Entwurf: ``doc/design/14_physische_diskette.md``.
"""

from .device import (Adapter, Device, GreaseweazleFehlt, GwFehler, KeinAdapter,
                     finde_adapter, open_device, verfuegbar)
from .session import LAUFWERKE, RATEN, PhysicalSession, verfuegbarkeit
from .sync import Job, JobKind, Priority, Stats, Sync
from .worker import TrackDevice, TrackWorker

__all__ = [
    "Adapter", "Device", "GreaseweazleFehlt", "GwFehler", "KeinAdapter",
    "finde_adapter", "open_device", "verfuegbar",
    "LAUFWERKE", "RATEN", "PhysicalSession", "verfuegbarkeit",
    "Job", "JobKind", "Priority", "Stats", "Sync",
    "TrackDevice", "TrackWorker",
]
