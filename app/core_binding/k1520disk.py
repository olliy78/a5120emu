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
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from app import paths                                        # noqa: E402

# ════════════════════════════════════════════════════════════════════════════
# Bibliothek finden und laden
# ════════════════════════════════════════════════════════════════════════════


def find_libk1520disk() -> Path:
    """Pfad der DiskTool-Bibliothek — Quellbaum wie Installation.

    Die Auflösung selbst steht in :mod:`app.paths` (eine Stelle für alle Pfade,
    siehe ``doc/design/13_distribution.md``): im Quellbaum ``build/``, in einer
    Installation ``<wurzel>/bin/``.  Ohne das fände eine installierte Anwendung
    ihre Bibliothek nicht — sie liegt dort nicht neben dem Python-Interpreter.

    Raises:
        FileNotFoundError: kein Kandidat existiert (Meldung listet alle auf).
    """
    return paths.disk_library()


paths.prepare_library_load()      # Windows: DLL-Suchverzeichnis anmelden
_lib = ctypes.CDLL(str(find_libk1520disk()))

_H = ctypes.c_void_p       # K1520Disk
_CS = ctypes.c_char_p
_U64 = ctypes.c_uint64

BINARY = 0
TEXT = 1

# ── Öffnen / Anlegen / Speichern ────────────────────────────────────────────
_lib.k1520d_open.argtypes = [_CS, _CS, ctypes.c_bool]
_lib.k1520d_open.restype = _H
_lib.k1520d_create.argtypes = [_CS, _CS, _CS]
_lib.k1520d_create.restype = _H
_lib.k1520d_create_bootable.argtypes = [_CS, _CS, _CS, _CS]
_lib.k1520d_create_bootable.restype = _H
_lib.k1520d_flush.argtypes = [_H]
_lib.k1520d_flush.restype = ctypes.c_bool
_lib.k1520d_save_as.argtypes = [_H, _CS]
_lib.k1520d_save_as.restype = ctypes.c_bool
_lib.k1520d_export_image.argtypes = [_H, _CS]
_lib.k1520d_export_image.restype = ctypes.c_bool
_lib.k1520d_read_only.argtypes = [_H]
_lib.k1520d_read_only.restype = ctypes.c_bool
_lib.k1520d_set_read_only.argtypes = [_H, ctypes.c_bool]
_lib.k1520d_set_read_only.restype = None
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

# ── Bootabbild (Systemspuren) ───────────────────────────────────────────────
_lib.k1520d_fs_boot_capacity.argtypes = [_CS]
_lib.k1520d_fs_boot_capacity.restype = _U64
_lib.k1520d_boot_area_size.argtypes = [_H, ctypes.c_int]
_lib.k1520d_boot_area_size.restype = _U64
_lib.k1520d_read_boot_image.argtypes = [_H, ctypes.c_int, _CS]
_lib.k1520d_read_boot_image.restype = ctypes.c_bool
_lib.k1520d_write_boot_image.argtypes = [_H, ctypes.c_int, _CS]
_lib.k1520d_write_boot_image.restype = ctypes.c_bool
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

# ── UDOS-Kopfsektorangaben je Eintrag ───────────────────────────────────────
# Einzeln deklariert statt in einer Schleife: `test_disk_c_api.py` gleicht Header,
# Bibliothek und diese Datei MECHANISCH ab und findet nur, was hier wörtlich steht.
_lib.k1520d_entry_start.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_start.restype = ctypes.c_uint16
_lib.k1520d_entry_record_len.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_record_len.restype = ctypes.c_uint16
_lib.k1520d_entry_block_len.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_block_len.restype = ctypes.c_uint16
_lib.k1520d_entry_segment.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_segment.restype = ctypes.c_uint16
_lib.k1520d_entry_segment_len.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_segment_len.restype = ctypes.c_uint16
_lib.k1520d_entry_low_addr.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_low_addr.restype = ctypes.c_uint16
_lib.k1520d_entry_high_addr.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_high_addr.restype = ctypes.c_uint16
_lib.k1520d_entry_stack_size.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_stack_size.restype = ctypes.c_uint16
_lib.k1520d_entry_extra.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_extra.restype = ctypes.c_uint32
_lib.k1520d_entry_created.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_created.restype = _CS
_lib.k1520d_set_udos_attrs.argtypes = [
    _H, _CS, _CS, _CS, _CS, _CS,
    ctypes.c_bool, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint32,
]
_lib.k1520d_set_udos_attrs.restype = ctypes.c_bool

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
    boot_capacity: int = 0   # Byte in den Systemspuren; 0 = nicht bootfähig

    @property
    def bootable(self) -> bool:
        """Kann dieses Dateisystem überhaupt eine Bootdiskette tragen?"""
        return self.boot_capacity > 0


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
            boot_capacity=int(_lib.k1520d_fs_boot_capacity(_b(name))),
        ))
    return out


def boot_capacity(filesystem: str) -> int:
    """Wie viele Byte fassen die Systemspuren dieses Dateisystems? (0 = keine)

    Damit lässt sich schon bei der Auswahl sagen, ob eine Bootdiskette möglich
    ist — ohne dass eine Diskette existiert.
    """
    return int(_lib.k1520d_fs_boot_capacity(_b(filesystem)))


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

    # ── UDOS-Kopfsektorangaben (bei CP/M alle 0 bzw. leer) ──────────────────
    # Sie stehen NICHT in den Bytes der Datei, steuern aber, wie UDOS sie lädt
    # (doc/udos_diskettenformat.md §6/§14).
    entry: int = 0          # ENTRY — Einsprungadresse (Typ P/P1)
    record_len: int = 0     # Satzlänge (Zuteilungseinheit)
    block_len: int = 0      # zweite Längenangabe (Kopfsektor Offset 17)
    segment: int = 0        # SEGMENTS: Anfang …
    segment_len: int = 0    # … und Länge
    low_addr: int = 0       # LOW ADDRESS  \
    high_addr: int = 0      # HIGH ADDRESS  } was der Lader zuteilen lässt
    stack_size: int = 0     # STACK SIZE   /
    extra: int = 0          # Kopfsektor 44…47 (Bedeutung offen)
    created: str = ""       # Erstellungsvermerk (Datum ODER Versionstext)

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
    def open(cls, path, filesystem: Optional[str] = None,
             read_only: bool = True) -> "DiskTool":
        """Diskette öffnen; ``filesystem=None`` erkennt selbst.

        ``read_only`` ist **absichtlich die Vorgabe**: beim blossen Lesen soll eine
        Diskette gar nicht kaputtgehen können.  Ändern verlangt danach den bewussten
        Schritt ``set_read_only(False)`` — in der Oberfläche der Haken „Nur lesen".

        Raises:
            K1520DiskError: mit der Meldung der Bibliothek — bei einem Abbild ohne
                Katalogeintrag enthält sie die gemessene Geometrie im Klartext.
        """
        p = os.fspath(path)
        h = _lib.k1520d_open(_b(p), _b(filesystem or ""), read_only)
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, p)

    @classmethod
    def create(cls, path, filesystem: str, label: str = "",
               boot_image=None) -> "DiskTool":
        """Neue, leere Diskette anlegen (formatieren + Dateisystem initialisieren).

        Mit ``boot_image`` (Pfad einer ``.bin``-Datei) wandert dieses Byteband in die
        **Systemspuren** vor dem Dateisystem — erst das macht eine Diskette bootfähig.
        Passt es nicht hinein, wird **gar nichts** angelegt.

        Raises:
            K1520DiskError: Dateisystem unbekannt, Container unpassend, oder das
                Bootabbild ist grösser als die Systemspuren (die Meldung nennt beide
                Zahlen).
        """
        p = os.fspath(path)
        boot = os.fspath(boot_image) if boot_image else ""
        h = _lib.k1520d_create_bootable(_b(p), _b(filesystem), _b(label), _b(boot))
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
                entry=int(_lib.k1520d_entry_start(self._h, i)),
                record_len=int(_lib.k1520d_entry_record_len(self._h, i)),
                block_len=int(_lib.k1520d_entry_block_len(self._h, i)),
                segment=int(_lib.k1520d_entry_segment(self._h, i)),
                segment_len=int(_lib.k1520d_entry_segment_len(self._h, i)),
                low_addr=int(_lib.k1520d_entry_low_addr(self._h, i)),
                high_addr=int(_lib.k1520d_entry_high_addr(self._h, i)),
                stack_size=int(_lib.k1520d_entry_stack_size(self._h, i)),
                extra=int(_lib.k1520d_entry_extra(self._h, i)),
                created=_s(_lib.k1520d_entry_created(self._h, i)),
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

    def set_udos_attrs(self, name: str, *, type: Optional[str] = None,
                       properties: Optional[str] = None,
                       created: Optional[str] = None,
                       modified: Optional[str] = None,
                       entry: Optional[int] = None,
                       block_len: Optional[int] = None,
                       segment: Optional[tuple] = None,
                       memory: Optional[tuple] = None,
                       extra: Optional[int] = None) -> None:
        """UDOS-Kopfsektorangaben einer vorhandenen Datei ändern.

        Der Dateiinhalt bleibt unangetastet; **nicht angegebene Felder bleiben
        stehen**.  ``segment=(anfang, länge)``, ``memory=(low, high, stack)``;
        ``properties=";"`` löscht alle Eigenschaften.

        Raises:
            K1520DiskError: schreibgeschützt, Datei unbekannt, oder CP/M (dort gibt
                es diese Angaben nicht).
        """
        seg = segment or (0, 0)
        mem = memory or (0, 0, 0)
        if not _lib.k1520d_set_udos_attrs(
                self._h, _b(name), _b(type or ""), _b(properties or ""),
                _b(created or ""), _b(modified or ""),
                entry is not None, int(entry or 0),
                block_len is not None, int(block_len or 0),
                segment is not None, int(seg[0]), int(seg[1]),
                memory is not None, int(mem[0]), int(mem[1]), int(mem[2]),
                extra is not None, int(extra or 0)):
            raise K1520DiskError(self._fail())

    # ── Bootabbild (Systemspuren) ───────────────────────────────────────────

    def boot_area_size(self, volume: int = 0) -> int:
        """Fassungsvermögen der Systemspuren in Byte; 0 = nicht bootfähig."""
        return int(_lib.k1520d_boot_area_size(self._h, volume))

    def read_boot_image(self, path, volume: int = 0) -> None:
        """Systemspuren in eine ``.bin``-Datei sichern (Bootabbild herausholen)."""
        if not _lib.k1520d_read_boot_image(self._h, volume, _b(os.fspath(path))):
            raise K1520DiskError(self._fail())

    def write_boot_image(self, path, volume: int = 0) -> None:
        """Bootabbild in die Systemspuren schreiben (danach ``flush()``)."""
        if not _lib.k1520d_write_boot_image(self._h, volume, _b(os.fspath(path))):
            raise K1520DiskError(self._fail())

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
        """Unter neuem Namen/Container speichern und **umbinden**.

        Auch bei Schreibschutz erlaubt: die Quelle bleibt unberührt.  Genau der Weg,
        um vor Änderungen eine Arbeitskopie anzulegen.
        """
        if not _lib.k1520d_save_as(self._h, _b(os.fspath(path))):
            raise K1520DiskError(self._fail())
        self._path = os.fspath(path)

    def export_image(self, path) -> None:
        """Kopie in einen anderen Container schreiben, **ohne** umzubinden.

        Für Archivierung und Formatumwandlung; die Arbeitsdatei bleibt dieselbe.
        UDOS lässt sich dabei nicht als ``.img`` ablegen.
        """
        if not _lib.k1520d_export_image(self._h, _b(os.fspath(path))):
            raise K1520DiskError(self._fail())

    @property
    def read_only(self) -> bool:
        """Schreibgeschützt?  Vorgabe beim Öffnen: ja."""
        return bool(_lib.k1520d_read_only(self._h))

    def set_read_only(self, ro: bool) -> None:
        _lib.k1520d_set_read_only(self._h, ro)
