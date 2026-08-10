"""k1520DiskTool — Core Binding

ctypes-Bindung an ``libk1520disk.so`` (C-ABI: ``core/api/k1520_disk_api.h``).
Bewusst getrennt von ``k1520.py``: das DiskTool braucht keinen Emulator und
umgekehrt.

Typische Benutzung::

    from core_binding.k1520disk import DiskTool, K1520DiskError

    with DiskTool.open("disks/udos_boot_scp.hfe") as d:
        print(d.filesystem, d.volume_count)        # 'udos_ds77', 2
        for e in d.list():
            print(e.side_prefix + e.name, e.size)  # 'Side1/HELP.DAT.00', 9919
        d.extract_all("~/auszug")                  # legt Side0/ und Side1/ an

Alle schreibenden Aufrufe wirken zunächst nur im Speicher; erst ``flush()``
schreibt in die Datei (und legt dabei einmalig eine Sicherungskopie ``…~`` an).

Siehe doc/design/13_k1520disktool.md §10.
"""

from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

# ════════════════════════════════════════════════════════════════════════════
# Bibliothek finden und laden
# ════════════════════════════════════════════════════════════════════════════


def find_libk1520disk() -> Path:
    """Pfad zu ``libk1520disk.so`` in den üblichen Bauverzeichnissen."""
    project_root = Path(__file__).resolve().parents[2]
    app_root = Path(__file__).resolve().parents[1]
    candidates = [
        project_root / "build" / "libk1520disk.so",
        project_root / "build_trace" / "libk1520disk.so",
        app_root / "build" / "libk1520disk.so",
        Path("/usr/local/lib/libk1520disk.so"),
        Path("/usr/lib/libk1520disk.so"),
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError(
        "libk1520disk.so nicht gefunden in:\n"
        + "\n".join(f"  {p}" for p in candidates)
        + "\n\nBauen mit:  tools/dev.sh build"
    )


_lib = ctypes.CDLL(str(find_libk1520disk()))

_H = ctypes.c_void_p       # K1520Disk
_CS = ctypes.c_char_p
_U64 = ctypes.c_uint64

BINARY = 0
TEXT = 1

# ── Öffnen / Anlegen / Speichern ────────────────────────────────────────────
_lib.k1520d_open.argtypes = [_CS, _CS]
_lib.k1520d_open.restype = _H
_lib.k1520d_create.argtypes = [_CS, _CS, _CS]
_lib.k1520d_create.restype = _H
_lib.k1520d_flush.argtypes = [_H]
_lib.k1520d_flush.restype = ctypes.c_bool
_lib.k1520d_save_as.argtypes = [_H, _CS]
_lib.k1520d_save_as.restype = ctypes.c_bool
_lib.k1520d_set_backup.argtypes = [_H, ctypes.c_bool]
_lib.k1520d_set_backup.restype = None
_lib.k1520d_close.argtypes = [_H]
_lib.k1520d_close.restype = None
_lib.k1520d_last_error.argtypes = [_H]
_lib.k1520d_last_error.restype = _CS
_lib.k1520d_last_open_error.argtypes = []
_lib.k1520d_last_open_error.restype = _CS

# ── Katalog und Erkennung ───────────────────────────────────────────────────
_lib.k1520d_fs_count.argtypes = []
_lib.k1520d_fs_count.restype = ctypes.c_int
_lib.k1520d_fs_name.argtypes = [ctypes.c_int]
_lib.k1520d_fs_name.restype = _CS
_lib.k1520d_fs_description.argtypes = [_CS]
_lib.k1520d_fs_description.restype = _CS
_lib.k1520d_fs_format.argtypes = [_CS]
_lib.k1520d_fs_format.restype = _CS
_lib.k1520d_fs_type.argtypes = [_CS]
_lib.k1520d_fs_type.restype = _CS
_lib.k1520d_catalog_report.argtypes = []
_lib.k1520d_catalog_report.restype = _CS
_lib.k1520d_detect.argtypes = [_CS]
_lib.k1520d_detect.restype = _CS
_lib.k1520d_detected_format.argtypes = [_H]
_lib.k1520d_detected_format.restype = _CS
_lib.k1520d_detected_fs.argtypes = [_H]
_lib.k1520d_detected_fs.restype = _CS
_lib.k1520d_detection_unambiguous.argtypes = [_H]
_lib.k1520d_detection_unambiguous.restype = ctypes.c_bool
_lib.k1520d_detection_alternatives.argtypes = [_H]
_lib.k1520d_detection_alternatives.restype = _CS
_lib.k1520d_detection_remarks.argtypes = [_H]
_lib.k1520d_detection_remarks.restype = _CS

# ── Seiten ──────────────────────────────────────────────────────────────────
_lib.k1520d_volume_count.argtypes = [_H]
_lib.k1520d_volume_count.restype = ctypes.c_int
_lib.k1520d_volume_dir.argtypes = [_H, ctypes.c_int]
_lib.k1520d_volume_dir.restype = _CS
_lib.k1520d_volume_label.argtypes = [_H, ctypes.c_int]
_lib.k1520d_volume_label.restype = _CS
_lib.k1520d_volume_total.argtypes = [_H, ctypes.c_int]
_lib.k1520d_volume_total.restype = _U64
_lib.k1520d_volume_free.argtypes = [_H, ctypes.c_int]
_lib.k1520d_volume_free.restype = _U64
_lib.k1520d_volume_used.argtypes = [_H, ctypes.c_int]
_lib.k1520d_volume_used.restype = _U64

# ── Verzeichnis ─────────────────────────────────────────────────────────────
_lib.k1520d_list.argtypes = [_H]
_lib.k1520d_list.restype = ctypes.c_int
_lib.k1520d_entry_volume.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_volume.restype = ctypes.c_int
_lib.k1520d_entry_name.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_name.restype = _CS
_lib.k1520d_entry_user.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_user.restype = ctypes.c_int
_lib.k1520d_entry_size.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_size.restype = _U64
_lib.k1520d_entry_type.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_type.restype = _CS
_lib.k1520d_entry_attrs.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_attrs.restype = _CS
_lib.k1520d_entry_date.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_date.restype = _CS
_lib.k1520d_entry_hidden.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_hidden.restype = ctypes.c_bool
_lib.k1520d_entry_damaged.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_damaged.restype = ctypes.c_bool

# ── Übertragung ─────────────────────────────────────────────────────────────
_lib.k1520d_extract.argtypes = [_H, _CS, _CS, ctypes.c_int]
_lib.k1520d_extract.restype = ctypes.c_bool
_lib.k1520d_insert.argtypes = [_H, _CS, _CS, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert.restype = ctypes.c_bool
_lib.k1520d_erase.argtypes = [_H, _CS]
_lib.k1520d_erase.restype = ctypes.c_bool
_lib.k1520d_extract_all.argtypes = [_H, _CS, ctypes.c_int]
_lib.k1520d_extract_all.restype = ctypes.c_bool
_lib.k1520d_insert_all.argtypes = [_H, _CS, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert_all.restype = ctypes.c_bool
_lib.k1520d_check_fit.argtypes = [_H, _CS]
_lib.k1520d_check_fit.restype = _CS

# ── Zustand ─────────────────────────────────────────────────────────────────
_lib.k1520d_dirty.argtypes = [_H]
_lib.k1520d_dirty.restype = ctypes.c_bool
_lib.k1520d_check.argtypes = [_H]
_lib.k1520d_check.restype = _CS
_lib.k1520d_version.argtypes = []
_lib.k1520d_version.restype = _CS


def _s(raw) -> str:
    """C-Zeichenkette → str (leer bei NULL)."""
    return raw.decode("utf-8", "replace") if raw else ""


def _b(text: Optional[str]) -> Optional[bytes]:
    return text.encode("utf-8") if text is not None else None


class K1520DiskError(RuntimeError):
    """Fehler aus der Bibliothek — die Meldung kommt im Klartext von dort."""


# ════════════════════════════════════════════════════════════════════════════
# Katalog (ohne geöffnete Diskette)
# ════════════════════════════════════════════════════════════════════════════


@dataclass(frozen=True)
class FileSystemInfo:
    """Ein Dateisystemprofil aus ``data/formats.yaml``."""

    name: str
    type: str          # 'cpm' | 'udos'
    format: str        # Geometrie
    description: str


def filesystems() -> List[FileSystemInfo]:
    """Alle bekannten Dateisysteme."""
    out = []
    for i in range(_lib.k1520d_fs_count()):
        name = _s(_lib.k1520d_fs_name(i))
        out.append(FileSystemInfo(
            name=name,
            type=_s(_lib.k1520d_fs_type(_b(name))),
            format=_s(_lib.k1520d_fs_format(_b(name))),
            description=_s(_lib.k1520d_fs_description(_b(name))),
        ))
    return out


def catalog_report() -> str:
    """Geladene Katalogdateien und Beanstandungen (mehrzeilig)."""
    return _s(_lib.k1520d_catalog_report())


def detect(path) -> str:
    """Erkanntes Dateisystem einer Datei; '' wenn keines passt."""
    return _s(_lib.k1520d_detect(_b(str(path))))


def version() -> str:
    return _s(_lib.k1520d_version())


# ════════════════════════════════════════════════════════════════════════════
# Verzeichniseintrag
# ════════════════════════════════════════════════════════════════════════════


@dataclass(frozen=True)
class Entry:
    """Eine Datei auf der Diskette."""

    volume: int
    name: str
    size: int
    type: str = ""        # UDOS: A/P/P1/B/D · CP/M: ''
    attrs: str = ""
    date: str = ""
    user: int = 0
    hidden: bool = False
    damaged: bool = False
    side_dir: str = ""    # 'Side0'/'Side1' — leer bei einseitigen Dateisystemen

    @property
    def side_prefix(self) -> str:
        """``'Side1/'`` bzw. ``''`` — das Präfix, das die API auch entgegennimmt."""
        return f"{self.side_dir}/" if self.side_dir else ""

    @property
    def ref(self) -> str:
        """Eindeutige Bezeichnung über die ganze Diskette."""
        return self.side_prefix + self.name


# ════════════════════════════════════════════════════════════════════════════
# Diskette
# ════════════════════════════════════════════════════════════════════════════


@dataclass(frozen=True)
class VolumeInfo:
    """Eine Seite (bei UDOS) bzw. der einzige Datenträger."""

    index: int
    dir: str            # 'Side0'/'Side1' oder ''
    label: str
    total: int
    used: int
    free: int


class DiskTool:
    """Eine geöffnete Diskette — 1..n Dateisysteme plus Dateibindung.

    Als Kontextmanager benutzbar; ``close()`` schreibt **nicht** von selbst.
    """

    def __init__(self, handle: int, path: str):
        self._h = handle
        self._path = path

    # ── Öffnen / Anlegen ────────────────────────────────────────────────────

    @classmethod
    def open(cls, path, filesystem: Optional[str] = None) -> "DiskTool":
        """Diskette öffnen; ``filesystem=None`` erkennt selbst.

        Raises:
            K1520DiskError: mit der Meldung der Bibliothek — bei einem Abbild ohne
                Katalogeintrag enthält sie die gemessene Geometrie im Klartext.
        """
        p = os.fspath(path)
        h = _lib.k1520d_open(_b(p), _b(filesystem or ""))
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, p)

    @classmethod
    def create(cls, path, filesystem: str, label: str = "") -> "DiskTool":
        """Neue, leere Diskette anlegen (formatieren + Dateisystem initialisieren)."""
        p = os.fspath(path)
        h = _lib.k1520d_create(_b(p), _b(filesystem), _b(label))
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, p)

    def __enter__(self) -> "DiskTool":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        if getattr(self, "_h", None):
            _lib.k1520d_close(self._h)
            self._h = None

    def __del__(self):
        self.close()

    # ── Auskunft ────────────────────────────────────────────────────────────

    @property
    def path(self) -> str:
        return self._path

    @property
    def format(self) -> str:
        return _s(_lib.k1520d_detected_format(self._h))

    @property
    def filesystem(self) -> str:
        return _s(_lib.k1520d_detected_fs(self._h))

    @property
    def unambiguous(self) -> bool:
        """False = mehrere Dateisysteme passen gleich gut (siehe ``alternatives``)."""
        return bool(_lib.k1520d_detection_unambiguous(self._h))

    @property
    def alternatives(self) -> List[str]:
        text = _s(_lib.k1520d_detection_alternatives(self._h))
        return [x.strip() for x in text.split(",") if x.strip()]

    @property
    def remarks(self) -> str:
        """Auffälligkeiten des Mediums (Altbestand, CRC-Fehler); '' = ohne Befund."""
        return _s(_lib.k1520d_detection_remarks(self._h))

    @property
    def volume_count(self) -> int:
        return _lib.k1520d_volume_count(self._h)

    def volume_dir(self, v: int) -> str:
        return _s(_lib.k1520d_volume_dir(self._h, v))

    def volumes(self) -> List[VolumeInfo]:
        return [
            VolumeInfo(
                index=v,
                dir=self.volume_dir(v),
                label=_s(_lib.k1520d_volume_label(self._h, v)),
                total=int(_lib.k1520d_volume_total(self._h, v)),
                used=int(_lib.k1520d_volume_used(self._h, v)),
                free=int(_lib.k1520d_volume_free(self._h, v)),
            )
            for v in range(self.volume_count)
        ]

    def list(self) -> List[Entry]:
        """Verzeichnis aller Seiten — **immer frisch** aus dem Medium gelesen."""
        n = _lib.k1520d_list(self._h)
        out = []
        for i in range(n):
            v = _lib.k1520d_entry_volume(self._h, i)
            out.append(Entry(
                volume=v,
                name=_s(_lib.k1520d_entry_name(self._h, i)),
                size=int(_lib.k1520d_entry_size(self._h, i)),
                type=_s(_lib.k1520d_entry_type(self._h, i)),
                attrs=_s(_lib.k1520d_entry_attrs(self._h, i)),
                date=_s(_lib.k1520d_entry_date(self._h, i)),
                user=_lib.k1520d_entry_user(self._h, i),
                hidden=bool(_lib.k1520d_entry_hidden(self._h, i)),
                damaged=bool(_lib.k1520d_entry_damaged(self._h, i)),
                side_dir=self.volume_dir(v),
            ))
        return out

    def check(self) -> str:
        """Mehrzeiliger Prüfbericht."""
        return _s(_lib.k1520d_check(self._h))

    # ── Übertragung ─────────────────────────────────────────────────────────

    def _fail(self) -> str:
        return _s(_lib.k1520d_last_error(self._h)) or "unbekannter Fehler"

    def extract(self, name: str, dest, text: bool = False) -> None:
        """Eine Datei herausholen; ``name`` darf ``'Side1/…'`` sein."""
        if not _lib.k1520d_extract(self._h, _b(name), _b(os.fspath(dest)),
                                   TEXT if text else BINARY):
            raise K1520DiskError(self._fail())

    def insert(self, src, name: str, text: bool = False,
               overwrite: bool = False) -> None:
        """Eine Datei einfügen."""
        if not _lib.k1520d_insert(self._h, _b(os.fspath(src)), _b(name),
                                  TEXT if text else BINARY, overwrite):
            raise K1520DiskError(self._fail())

    def erase(self, name: str) -> None:
        if not _lib.k1520d_erase(self._h, _b(name)):
            raise K1520DiskError(self._fail())

    def extract_all(self, dest_dir, text: bool = False) -> None:
        """Alles herausholen; bei mehreren Seiten entstehen ``Side0/``, ``Side1/``."""
        if not _lib.k1520d_extract_all(self._h, _b(os.fspath(dest_dir)),
                                       TEXT if text else BINARY):
            raise K1520DiskError(self._fail())

    def insert_all(self, src_dir, text: bool = False, overwrite: bool = True) -> None:
        """Einen Ordner einfügen — transaktional.

        Bei mehreren Seiten **muss** der Ordner genau die ``SideN/``-Unter-
        verzeichnisse tragen; sonst schlägt der Aufruf fehl und die Diskette
        bleibt unverändert.
        """
        if not _lib.k1520d_insert_all(self._h, _b(os.fspath(src_dir)),
                                      TEXT if text else BINARY, overwrite):
            raise K1520DiskError(self._fail())

    def check_fit(self, src_dir) -> str:
        """``'passt'`` oder der Grund — schreibt nichts."""
        return _s(_lib.k1520d_check_fit(self._h, _b(os.fspath(src_dir))))

    def fits(self, src_dir) -> bool:
        return self.check_fit(src_dir) == "passt"

    # ── Dateibindung ────────────────────────────────────────────────────────

    @property
    def dirty(self) -> bool:
        """Ungespeicherte Änderungen im Speicher?"""
        return bool(_lib.k1520d_dirty(self._h))

    def set_backup(self, on: bool) -> None:
        """Sicherungskopie ``…~`` beim ersten Schreiben (Vorgabe: an)."""
        _lib.k1520d_set_backup(self._h, on)

    def flush(self) -> None:
        if not _lib.k1520d_flush(self._h):
            raise K1520DiskError(self._fail())

    def save_as(self, path) -> None:
        if not _lib.k1520d_save_as(self._h, _b(os.fspath(path))):
            raise K1520DiskError(self._fail())
        self._path = os.fspath(path)
