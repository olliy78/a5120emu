"""Physische Diskette — ctypes-Bindung an die Auftrags-ABI des Kerns.

Gegenstück zu ``core/api/k1520_sync_api.h``.  Die Funktionen ``k1520s_*`` stehen in
**beiden** Bibliotheken (``libk1520core`` für den Emulator, ``libk1520disk`` für das
DiskTool) — welche geladen wird, entscheidet der Aufrufer über ``for_emulator``.

Der Vertrag in fünf Sätzen (doc/design/14_physische_diskette.md §9):

1. Genau **ein** Arbeitsfaden je :class:`Sync`; ein zweiter ``take_job`` wird abgewiesen.
2. Der Arbeitsfaden ruft **nur** die Methoden dieser Klasse.
3. Jeder abgeholte Auftrag wird abgeschlossen (``complete_*``) oder scheitert (``fail``).
4. ``take_job`` ist die einzige blockierende Funktion; ctypes gibt dabei die GIL frei,
   der Rest der Anwendung läuft also weiter.
5. ``shutdown()`` ist endgültig und löst jeden Wartenden im Kern.

Siehe doc/design/14_physische_diskette.md §10.
"""

from __future__ import annotations

import ctypes
import sys
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from app import paths                                        # noqa: E402


class JobKind(IntEnum):
    """Auftragsart — Spiegel von ``SyncJobKind``."""

    NONE = 0
    READ = 1
    WRITE = 2
    STOP = 3        #: kein Auftrag mehr; der Arbeitsfaden soll enden
    #: Prüf-Lesen nach einem Schreibvorgang.  **Für den Arbeitsfaden dasselbe wie
    #: READ** — Spur lesen, Bitzellen abliefern; verglichen wird im Kern.
    VERIFY = 4


class Priority(IntEnum):
    """Dringlichkeit; kleiner = wichtiger (§5.1)."""

    NONE = 0
    DEMAND = 1      #: jemand wartet auf diese Spur
    WRITEBACK = 2   #: geänderte Spur, zur Ruhe gekommen
    READAHEAD = 3   #: unbekannte Spur, niemand wartet


class _Spec(ctypes.Structure):
    # Spiegel von K1520SyncSpec (core/api/k1520_sync_api.h) — Reihenfolge zählt.
    _fields_ = [
        ("num_cyls", ctypes.c_uint8),
        ("num_heads", ctypes.c_uint8),
        ("cell_rate_kbps", ctypes.c_uint16),
        ("rpm", ctypes.c_uint16),
        ("writable", ctypes.c_bool),
        ("default_encoding", ctypes.c_uint8),
        ("read_ahead", ctypes.c_bool),
        ("write_settle_ms", ctypes.c_uint32),
        ("request_timeout_ms", ctypes.c_uint32),
        ("verify_writes", ctypes.c_bool),
        ("write_verify_retries", ctypes.c_uint8),
    ]


class _Job(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("kind", ctypes.c_uint8),
        ("cyl", ctypes.c_uint8),
        ("head", ctypes.c_uint8),
        ("prio", ctypes.c_uint8),
    ]


class _Stats(ctypes.Structure):
    # Spiegel von K1520SyncStats (core/api/k1520_sync_api.h) — Reihenfolge zählt.
    _fields_ = [
        ("tracks_total", ctypes.c_uint16),
        ("tracks_known", ctypes.c_uint16),
        ("tracks_dirty", ctypes.c_uint16),
        ("tracks_failed", ctypes.c_uint16),
        ("tracks_defect", ctypes.c_uint16),
        ("reads_done", ctypes.c_uint32),
        ("writes_done", ctypes.c_uint32),
        ("verifies_done", ctypes.c_uint32),
        ("verify_failed", ctypes.c_uint32),
        ("errors", ctypes.c_uint32),
        ("busy_kind", ctypes.c_uint8),
        ("busy_cyl", ctypes.c_uint8),
        ("busy_head", ctypes.c_uint8),
        ("stopped", ctypes.c_bool),
    ]


@dataclass(frozen=True)
class Job:
    """Ein Auftrag, wie ihn der Arbeitsfaden bekommt."""

    id: int
    kind: JobKind
    cyl: int
    head: int
    prio: Priority


@dataclass(frozen=True)
class Stats:
    """Momentaufnahme für die Anzeige (»47 von 160 Spuren gelesen«)."""

    tracks_total: int
    tracks_known: int
    tracks_dirty: int
    tracks_failed: int
    tracks_defect: int
    reads_done: int
    writes_done: int
    verifies_done: int
    verify_failed: int
    errors: int
    busy_kind: int
    busy_cyl: int
    busy_head: int
    stopped: bool

    @property
    def busy(self) -> bool:
        return self.busy_cyl != 255

    @property
    def defect(self) -> bool:
        """Gibt es Spuren, die sich nicht schreiben liessen?"""
        return self.tracks_defect > 0


_H = ctypes.c_void_p
_libs: dict[bool, ctypes.CDLL] = {}


def _lib(for_emulator: bool) -> ctypes.CDLL:
    """Bibliothek laden und die ``k1520s_*``-Signaturen anmelden (einmal je Datei)."""
    if for_emulator in _libs:
        return _libs[for_emulator]

    paths.prepare_library_load()
    pfad = paths.core_library() if for_emulator else paths.disk_library()
    lib = ctypes.CDLL(str(pfad))

    lib.k1520s_create.argtypes = [ctypes.POINTER(_Spec)]
    lib.k1520s_create.restype = _H
    lib.k1520s_destroy.argtypes = [_H]
    lib.k1520s_destroy.restype = None
    lib.k1520s_shutdown.argtypes = [_H]
    lib.k1520s_shutdown.restype = None
    lib.k1520s_take_job.argtypes = [_H, ctypes.c_int, ctypes.POINTER(_Job)]
    lib.k1520s_take_job.restype = ctypes.c_bool
    lib.k1520s_fetch_write.argtypes = [_H, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8),
                                       ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.k1520s_fetch_write.restype = ctypes.c_int
    lib.k1520s_complete_read.argtypes = [_H, ctypes.c_uint32,
                                         ctypes.POINTER(ctypes.c_uint8),
                                         ctypes.c_int, ctypes.c_uint32]
    lib.k1520s_complete_read.restype = ctypes.c_bool
    lib.k1520s_complete_write.argtypes = [_H, ctypes.c_uint32]
    lib.k1520s_complete_write.restype = ctypes.c_bool
    lib.k1520s_fail_job.argtypes = [_H, ctypes.c_uint32, ctypes.c_char_p]
    lib.k1520s_fail_job.restype = None
    lib.k1520s_set_read_ahead.argtypes = [_H, ctypes.c_bool]
    lib.k1520s_set_read_ahead.restype = None
    lib.k1520s_stats.argtypes = [_H, ctypes.POINTER(_Stats)]
    lib.k1520s_stats.restype = ctypes.c_bool
    lib.k1520s_last_error.argtypes = [_H]
    lib.k1520s_last_error.restype = ctypes.c_char_p
    lib.k1520s_load_all.argtypes = [_H]
    lib.k1520s_load_all.restype = ctypes.c_bool
    lib.k1520s_flush.argtypes = [_H, ctypes.c_int]
    lib.k1520s_flush.restype = ctypes.c_bool
    lib.k1520s_rewrite_all.argtypes = [_H]
    lib.k1520s_rewrite_all.restype = ctypes.c_int
    lib.k1520s_defect_tracks.argtypes = [_H, ctypes.c_char_p, ctypes.c_int]
    lib.k1520s_defect_tracks.restype = ctypes.c_int

    _libs[for_emulator] = lib
    return lib


class Sync:
    """Eine physische Diskette samt Auftragsweg.

    Es wird beim Anlegen **nichts gelesen** — Spuren kommen einzeln, sobald der
    Emulator bzw. das DiskTool sie anfasst.

    Anmelden danach:

    * Emulator — ``k1520_mount_physical(handle, laufwerk, sync.handle, wp)``
    * DiskTool — ``k1520d_open_physical(sync.handle, fs_name, read_only)``

    Ein Handle lässt sich nur **einmal** anmelden.
    """

    #: Größter Zellstrom einer Spurseite (8″ 500 kbit/s bei 300 U/min ≈ 12,5 KB;
    #: großzügig gerundet, damit auch überabgetastete Aufnahmen hineinpassen).
    MAX_CELL_BYTES = 256 * 1024

    def __init__(self, *, num_cyls: int = 80, num_heads: int = 2,
                 cell_rate_kbps: int = 250, rpm: int = 300,
                 writable: bool = False, encoding: str = "MFM",
                 read_ahead: bool = True, write_settle_ms: int = 0,
                 request_timeout_ms: int = 0, for_emulator: bool = False,
                 verify_writes: bool = True, write_verify_retries: int = 1):
        self._lib = _lib(for_emulator)
        spec = _Spec(
            num_cyls=num_cyls,
            num_heads=num_heads,
            cell_rate_kbps=cell_rate_kbps,
            rpm=rpm,
            writable=writable,
            default_encoding=1 if encoding.upper() == "MFM" else 0,
            read_ahead=read_ahead,
            write_settle_ms=write_settle_ms,
            request_timeout_ms=request_timeout_ms,
            verify_writes=verify_writes,
            write_verify_retries=write_verify_retries,
        )
        self._h = self._lib.k1520s_create(ctypes.byref(spec))
        if not self._h:
            raise RuntimeError("k1520s_create scheiterte (unbrauchbare Angaben)")
        self.cell_rate_kbps = cell_rate_kbps
        self.rpm = rpm
        self.writable = writable

    # ── Handle ──────────────────────────────────────────────────────────────

    @property
    def handle(self):
        """Rohes Handle für ``k1520_mount_physical`` / ``k1520d_open_physical``."""
        return self._h

    def close(self) -> None:
        """Freigeben.  **Erst rufen, wenn der Arbeitsfaden beendet ist.**"""
        if self._h:
            self._lib.k1520s_destroy(self._h)
            self._h = None

    def __enter__(self) -> "Sync":
        return self

    def __exit__(self, *_exc) -> None:
        self.shutdown()
        self.close()

    # ── Arbeitsfaden ────────────────────────────────────────────────────────

    def take_job(self, timeout_ms: int = 1000) -> Optional[Job]:
        """Nächsten Auftrag abholen; blockiert, bis es Arbeit gibt.

        Returns:
            ``None`` bei Ablauf der Wartezeit (dann gibt es schlicht nichts zu tun).
        """
        j = _Job()
        if not self._lib.k1520s_take_job(self._h, timeout_ms, ctypes.byref(j)):
            return None
        return Job(id=j.id, kind=JobKind(j.kind), cyl=j.cyl, head=j.head,
                   prio=Priority(j.prio))

    def fetch_write(self, job_id: int) -> tuple[bytes, int]:
        """Zellstrom eines Schreibauftrags holen (HFE-Konvention, LSB-first)."""
        puffer = (ctypes.c_uint8 * self.MAX_CELL_BYTES)()
        bitcells = ctypes.c_uint32(0)
        n = self._lib.k1520s_fetch_write(self._h, job_id, puffer, self.MAX_CELL_BYTES,
                                         ctypes.byref(bitcells))
        if n < 0:
            raise RuntimeError(f"fetch_write scheiterte: {self.last_error}")
        return bytes(puffer[:n]), bitcells.value

    def complete_read(self, job_id: int, cells: bytes, bitcells: int) -> bool:
        """Leseauftrag abschließen: Bitzellen einer Spurseite abliefern.

        ``cells`` in HFE-Konvention (LSB zuerst je Byte) — genau das, was
        :meth:`app.gw.device.Device.read_track` liefert.
        """
        puffer = (ctypes.c_uint8 * len(cells)).from_buffer_copy(cells)
        return bool(self._lib.k1520s_complete_read(self._h, job_id, puffer,
                                                   len(cells), bitcells))

    def complete_write(self, job_id: int) -> bool:
        return bool(self._lib.k1520s_complete_write(self._h, job_id))

    def fail(self, job_id: int, msg: str) -> None:
        self._lib.k1520s_fail_job(self._h, job_id, msg.encode("utf-8", "replace"))

    # ── Steuerung / Anzeige ─────────────────────────────────────────────────

    def shutdown(self) -> None:
        if self._h:
            self._lib.k1520s_shutdown(self._h)

    def set_read_ahead(self, on: bool) -> None:
        self._lib.k1520s_set_read_ahead(self._h, on)

    @property
    def stats(self) -> Stats:
        s = _Stats()
        if not self._lib.k1520s_stats(self._h, ctypes.byref(s)):
            raise RuntimeError("k1520s_stats scheiterte")
        return Stats(*(getattr(s, f[0]) for f in _Stats._fields_))

    @property
    def last_error(self) -> str:
        p = self._lib.k1520s_last_error(self._h)
        return p.decode("utf-8", "replace") if p else ""

    def load_all(self) -> bool:
        """Alle noch unbekannten Spuren lesen (für »Speichern unter…«).  Blockiert."""
        return bool(self._lib.k1520s_load_all(self._h))

    def flush(self, timeout_ms: int = 0) -> bool:
        """Alle geänderten Spuren zurückschreiben und darauf warten.

        Returns:
            ``False`` auch dann, wenn eine Spur **schadhaft** ist — dann steht in
            :attr:`defect_tracks`, welche.
        """
        return bool(self._lib.k1520s_flush(self._h, timeout_ms))

    def rewrite_all(self) -> int:
        """**Die ganze Diskette neu beschreiben** — für eine frische, fehlerfreie.

        Der Weg aus einer Schadstelle heraus: Diskette wechseln, dann alles noch
        einmal hinausschreiben.  Nur **bekannte** Spuren werden eingestellt; nie
        gelesene tragen bedeutungslose Bytes.

        Returns:
            Zahl der eingestellten Spuren.
        """
        return int(self._lib.k1520s_rewrite_all(self._h))

    @property
    def defect_tracks(self) -> str:
        """Die schadhaften Spuren als Text, z. B. ``"5/1, 12/0"`` (leer = keine)."""
        puffer = ctypes.create_string_buffer(4096)
        n = self._lib.k1520s_defect_tracks(self._h, puffer, len(puffer))
        return puffer.value.decode("utf-8", "replace") if n > 0 else ""
