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
_lib.k1520d_open_raw.argtypes = [_CS, _CS, ctypes.c_bool]
_lib.k1520d_open_raw.restype = _H
# Physische Diskette am Greaseweazle (doc/design/14_physische_diskette.md)
_lib.k1520d_open_physical.argtypes = [ctypes.c_void_p, _CS, ctypes.c_bool]
_lib.k1520d_open_physical.restype = _H
_lib.k1520d_open_physical_raw.argtypes = [ctypes.c_void_p, _CS, ctypes.c_bool]
_lib.k1520d_open_physical_raw.restype = _H
_lib.k1520d_has_filesystem.argtypes = [_H]
_lib.k1520d_has_filesystem.restype = ctypes.c_bool
_lib.k1520d_redetect.argtypes = [_H, _CS]
_lib.k1520d_redetect.restype = ctypes.c_bool
_lib.k1520d_keep_even_tracks.argtypes = [_H]
_lib.k1520d_keep_even_tracks.restype = ctypes.c_int
_lib.k1520d_drop_second_side.argtypes = [_H]
_lib.k1520d_drop_second_side.restype = ctypes.c_int
_lib.k1520d_delete_cylinder.argtypes = [_H, ctypes.c_int]
_lib.k1520d_delete_cylinder.restype = ctypes.c_int
_lib.k1520d_insert_cylinder_at.argtypes = [_H, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert_cylinder_at.restype = ctypes.c_int
_lib.k1520d_probe_track_count.argtypes = [ctypes.c_int, ctypes.c_int]
_lib.k1520d_probe_track_count.restype = ctypes.c_int
_lib.k1520d_write_to_physical.argtypes = [_H, ctypes.c_void_p]
_lib.k1520d_write_to_physical.restype = ctypes.c_int
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
_lib.k1520d_detection_examined_tracks.argtypes = [_H]
_lib.k1520d_detection_examined_tracks.restype = ctypes.c_int
_lib.k1520d_refresh_detection.argtypes = [_H]
_lib.k1520d_refresh_detection.restype = ctypes.c_bool

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
# Zweistufiges Verzeichnis (UDOS: Kopfsektoren liegen verstreut, §11.2b)
_lib.k1520d_list_names.argtypes = [_H]
_lib.k1520d_list_names.restype = ctypes.c_int
_lib.k1520d_entry_details_loaded.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_details_loaded.restype = ctypes.c_bool
_lib.k1520d_entry_details_ready.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_details_ready.restype = ctypes.c_bool
_lib.k1520d_entry_load_details.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_load_details.restype = ctypes.c_bool
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
_lib.k1520d_entry_bytes_in_last.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_bytes_in_last.restype = ctypes.c_uint16
_lib.k1520d_entry_extra.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_extra.restype = ctypes.c_uint32
_lib.k1520d_entry_segments.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_segments.restype = _CS
_lib.k1520d_entry_created.argtypes = [_H, ctypes.c_int]
_lib.k1520d_entry_created.restype = _CS
_lib.k1520d_set_udos_attrs.argtypes = [
    _H, _CS, _CS, _CS, _CS, _CS,
    ctypes.c_bool, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16,
    ctypes.c_bool, ctypes.c_uint32,
    ctypes.c_bool, _CS,
]
_lib.k1520d_set_udos_attrs.restype = ctypes.c_bool

_lib.k1520d_set_cpm_attrs.argtypes = [
    _H, _CS,
    ctypes.c_bool, ctypes.c_bool,
    ctypes.c_bool, ctypes.c_bool,
    ctypes.c_bool, ctypes.c_bool,
    ctypes.c_bool, ctypes.c_int,
]
_lib.k1520d_set_cpm_attrs.restype = ctypes.c_bool

# ── Sektoransicht (Diskeditor) ──────────────────────────────────────────────
_lib.k1520d_medium_cylinders.argtypes = [_H]
_lib.k1520d_medium_cylinders.restype = ctypes.c_int
_lib.k1520d_medium_heads.argtypes = [_H]
_lib.k1520d_medium_heads.restype = ctypes.c_int
_lib.k1520d_track_scan.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_track_scan.restype = ctypes.c_int
_lib.k1520d_track_state.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_track_state.restype = ctypes.c_int
_lib.k1520d_track_exists.argtypes = [_H]
_lib.k1520d_track_exists.restype = ctypes.c_bool
_lib.k1520d_track_formatted.argtypes = [_H]
_lib.k1520d_track_formatted.restype = ctypes.c_bool
_lib.k1520d_track_encoding.argtypes = [_H]
_lib.k1520d_track_encoding.restype = _CS
_lib.k1520d_track_bytes.argtypes = [_H]
_lib.k1520d_track_bytes.restype = ctypes.c_int
_lib.k1520d_track_sectors.argtypes = [_H]
_lib.k1520d_track_sectors.restype = ctypes.c_int

# Ausgeschrieben statt in einer Schleife: der Wächter
# `test_every_header_function_has_ctypes_signatures` sucht wörtlich nach
# `_lib.<name>.argtypes` — eine Schleife wäre für ihn unsichtbar.
_lib.k1520d_span_kind.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_kind.restype = ctypes.c_int
_lib.k1520d_span_index.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_index.restype = ctypes.c_int
_lib.k1520d_span_id.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_id.restype = ctypes.c_int
_lib.k1520d_span_cyl.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_cyl.restype = ctypes.c_int
_lib.k1520d_span_head.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_head.restype = ctypes.c_int
_lib.k1520d_span_size.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_size.restype = ctypes.c_int
_lib.k1520d_span_start.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_start.restype = ctypes.c_double
_lib.k1520d_span_end.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_end.restype = ctypes.c_double
_lib.k1520d_span_id_crc_ok.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_id_crc_ok.restype = ctypes.c_bool
_lib.k1520d_span_data_crc_ok.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_data_crc_ok.restype = ctypes.c_bool
_lib.k1520d_span_deleted.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_deleted.restype = ctypes.c_bool
_lib.k1520d_span_blank.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_blank.restype = ctypes.c_bool
_lib.k1520d_span_tail_bytes.argtypes = [_H, ctypes.c_int]
_lib.k1520d_span_tail_bytes.restype = ctypes.c_int

_lib.k1520d_sector_read.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520d_sector_read.restype = ctypes.c_int
_lib.k1520d_sector_crc.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.k1520d_sector_crc.restype = ctypes.c_int
_lib.k1520d_sector_crc_for.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                       ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520d_sector_crc_for.restype = ctypes.c_int
_lib.k1520d_sector_write.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                     ctypes.POINTER(ctypes.c_uint8), ctypes.c_int,
                                     ctypes.c_int]
_lib.k1520d_sector_write.restype = ctypes.c_bool
_lib.k1520d_sector_tail.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520d_sector_tail.restype = ctypes.c_int
_lib.k1520d_sector_write_tail.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                          ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520d_sector_write_tail.restype = ctypes.c_bool
_lib.k1520d_sector_erase.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                     ctypes.c_int]
_lib.k1520d_sector_erase.restype = ctypes.c_bool
_lib.k1520d_sector_create.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_bool]
_lib.k1520d_sector_create.restype = ctypes.c_bool
_lib.k1520d_sector_plan_pos.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                        ctypes.c_int]
_lib.k1520d_sector_plan_pos.restype = ctypes.c_int
_lib.k1520d_sector_plan_len.argtypes = [_H, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                        ctypes.c_int, ctypes.c_bool]
_lib.k1520d_sector_plan_len.restype = ctypes.c_int

# ── Übertragung ─────────────────────────────────────────────────────────────
_lib.k1520d_extract.argtypes = [_H, _CS, _CS, ctypes.c_int]
_lib.k1520d_extract.restype = ctypes.c_bool
_lib.k1520d_insert.argtypes = [_H, _CS, _CS, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert.restype = ctypes.c_bool
_lib.k1520d_erase.argtypes = [_H, _CS]
_lib.k1520d_erase.restype = ctypes.c_bool
_lib.k1520d_insert_forced.argtypes = [_H, _CS, _CS, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert_forced.restype = ctypes.c_bool
_lib.k1520d_insert_with_info.argtypes = [_H, _CS, _CS, ctypes.c_int, ctypes.c_bool, _CS]
_lib.k1520d_insert_with_info.restype = ctypes.c_bool
_lib.k1520d_last_insert_problem.argtypes = [_H]
_lib.k1520d_last_insert_problem.restype = ctypes.c_int
_lib.k1520d_last_accessory_count.argtypes = [_H]
_lib.k1520d_last_accessory_count.restype = ctypes.c_int
_lib.k1520d_accessory_reason.argtypes = [_CS]
_lib.k1520d_accessory_reason.restype = ctypes.c_char_p
_lib.k1520d_set_cpm_fileinfo.argtypes = [_H, ctypes.c_bool]
_lib.k1520d_set_cpm_fileinfo.restype = None
_lib.k1520d_cpm_fileinfo.argtypes = [_H]
_lib.k1520d_cpm_fileinfo.restype = ctypes.c_bool

_lib.k1520d_extract_all.argtypes = [_H, _CS, ctypes.c_int]
_lib.k1520d_extract_all.restype = ctypes.c_bool
_lib.k1520d_insert_all.argtypes = [_H, _CS, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_insert_all.restype = ctypes.c_bool
_lib.k1520d_check_fit.argtypes = [_H, _CS]
_lib.k1520d_check_fit.restype = _CS

# ── Zustand ─────────────────────────────────────────────────────────────────
_lib.k1520d_dirty.argtypes = [_H]
_lib.k1520d_dirty.restype = ctypes.c_bool
_lib.k1520d_check_report.argtypes = [_H]
_lib.k1520d_check_report.restype = _CS
_lib.k1520d_version.argtypes = []
_lib.k1520d_version.restype = _CS

_lib.k1520d_file_first_sector.argtypes = [_H, ctypes.c_char_p,
                                          ctypes.POINTER(ctypes.c_int),
                                          ctypes.POINTER(ctypes.c_int),
                                          ctypes.POINTER(ctypes.c_int)]
_lib.k1520d_file_first_sector.restype = ctypes.c_bool

# ── Dateisystemprüfung (doc/design/15_dateisystempruefung.md) ───────────────
_lib.k1520d_check.argtypes = [_H, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_check.restype = ctypes.c_int
_lib.k1520d_check_complete.argtypes = [_H]
_lib.k1520d_check_complete.restype = ctypes.c_bool
_lib.k1520d_check_tracks_read.argtypes = [_H]
_lib.k1520d_check_tracks_read.restype = ctypes.c_int
_lib.k1520d_check_tracks_total.argtypes = [_H]
_lib.k1520d_check_tracks_total.restype = ctypes.c_int
_lib.k1520d_check_summary.argtypes = [_H]
_lib.k1520d_check_summary.restype = _CS
_lib.k1520d_check_level.argtypes = [_H]
_lib.k1520d_check_level.restype = ctypes.c_int
_lib.k1520d_recover_level.argtypes = [_H]
_lib.k1520d_recover_level.restype = ctypes.c_int
_lib.k1520d_check_step_count.argtypes = [_H]
_lib.k1520d_check_step_count.restype = ctypes.c_int
_lib.k1520d_check_step_id.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_id.restype = _CS
_lib.k1520d_check_step_title.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_title.restype = _CS
_lib.k1520d_check_step_done.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_done.restype = ctypes.c_bool
_lib.k1520d_check_step_why.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_why.restype = _CS
_lib.k1520d_check_step_findings.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_findings.restype = ctypes.c_int
_lib.k1520d_check_step_severity.argtypes = [_H, ctypes.c_int]
_lib.k1520d_check_step_severity.restype = ctypes.c_int
_lib.k1520d_finding_count.argtypes = [_H]
_lib.k1520d_finding_count.restype = ctypes.c_int
_lib.k1520d_finding_id.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_id.restype = _CS
_lib.k1520d_finding_severity.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_severity.restype = ctypes.c_int
_lib.k1520d_finding_layer.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_layer.restype = ctypes.c_int
_lib.k1520d_finding_volume.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_volume.restype = ctypes.c_int
_lib.k1520d_finding_object.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_object.restype = _CS
_lib.k1520d_finding_text.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_text.restype = _CS
_lib.k1520d_finding_cyl.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_cyl.restype = ctypes.c_int
_lib.k1520d_finding_head.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_head.restype = ctypes.c_int
_lib.k1520d_finding_sector.argtypes = [_H, ctypes.c_int]
_lib.k1520d_finding_sector.restype = ctypes.c_int
_lib.k1520d_repair_count.argtypes = [_H, ctypes.c_int]
_lib.k1520d_repair_count.restype = ctypes.c_int
_lib.k1520d_repair_id.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_id.restype = _CS
_lib.k1520d_repair_text.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_text.restype = _CS
_lib.k1520d_repair_destructive.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_destructive.restype = ctypes.c_bool
_lib.k1520d_repair_recommended.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_recommended.restype = ctypes.c_bool
_lib.k1520d_repair_guessed.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_guessed.restype = ctypes.c_bool
_lib.k1520d_repair_blocked.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_blocked.restype = ctypes.c_bool
_lib.k1520d_repair_blocked_why.argtypes = [_H, ctypes.c_int, ctypes.c_int]
_lib.k1520d_repair_blocked_why.restype = _CS
_lib.k1520d_apply_repairs.argtypes = [_H, ctypes.POINTER(ctypes.c_int),
                                      ctypes.POINTER(ctypes.c_int), ctypes.c_int]
_lib.k1520d_apply_repairs.restype = ctypes.c_int
_lib.k1520d_recover_scan.argtypes = [_H, ctypes.c_int, ctypes.c_bool]
_lib.k1520d_recover_scan.restype = ctypes.c_int
_lib.k1520d_recover_complete.argtypes = [_H]
_lib.k1520d_recover_complete.restype = ctypes.c_bool
_lib.k1520d_recover_step_count.argtypes = [_H]
_lib.k1520d_recover_step_count.restype = ctypes.c_int
_lib.k1520d_recover_step_id.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_step_id.restype = _CS
_lib.k1520d_recover_step_title.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_step_title.restype = _CS
_lib.k1520d_recover_step_done.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_step_done.restype = ctypes.c_bool
_lib.k1520d_recover_step_why.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_step_why.restype = _CS
_lib.k1520d_recover_step_finds.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_step_finds.restype = ctypes.c_int
_lib.k1520d_recover_count.argtypes = [_H]
_lib.k1520d_recover_count.restype = ctypes.c_int
_lib.k1520d_recover_name.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_name.restype = _CS
_lib.k1520d_recover_type.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_type.restype = _CS
_lib.k1520d_recover_origin.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_origin.restype = _CS
_lib.k1520d_recover_suggestion.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_suggestion.restype = _CS
_lib.k1520d_recover_volume.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_volume.restype = ctypes.c_int
_lib.k1520d_recover_size.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_size.restype = ctypes.c_uint64
_lib.k1520d_recover_quality.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_quality.restype = ctypes.c_int
_lib.k1520d_recover_detail.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_detail.restype = _CS
_lib.k1520d_recover_restorable.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_restorable.restype = ctypes.c_bool
_lib.k1520d_recover_blocked_why.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_blocked_why.restype = _CS
_lib.k1520d_recover_cyl.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_cyl.restype = ctypes.c_int
_lib.k1520d_recover_head.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_head.restype = ctypes.c_int
_lib.k1520d_recover_sector.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_sector.restype = ctypes.c_int
_lib.k1520d_recover_part_count.argtypes = [_H, ctypes.c_int]
_lib.k1520d_recover_part_count.restype = ctypes.c_int
_lib.k1520d_recover_part.argtypes = [_H, ctypes.c_int, ctypes.c_int,
                                     ctypes.POINTER(ctypes.c_int),
                                     ctypes.POINTER(ctypes.c_int),
                                     ctypes.POINTER(ctypes.c_int)]
_lib.k1520d_recover_part.restype = ctypes.c_bool
_lib.k1520d_recover_preview.argtypes = [_H, ctypes.c_int,
                                        ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520d_recover_preview.restype = ctypes.c_int
_lib.k1520d_recover_extract.argtypes = [_H, ctypes.c_int, _CS]
_lib.k1520d_recover_extract.restype = ctypes.c_bool
_lib.k1520d_recover_restore.argtypes = [_H, ctypes.c_int, _CS]
_lib.k1520d_recover_restore.restype = ctypes.c_bool


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
    #: Stehen die Angaben jenseits des Namens fest?  Bei CP/M immer True; bei UDOS
    #: erst, wenn der Kopfsektor der Datei gelesen ist (siehe DiskTool.list_names).
    details_loaded: bool = True
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
    bytes_in_last: int = 0  # Bytes im letzten Satz (Kopfsektor 22)
    extra: int = 0          # Kopfsektor 44…47 = die vier Bytes hinter Segment 1
    segments: str = ""      # ALLE Segmente, "4400+0041 8442+0026"
    created: str = ""       # Erstellungsvermerk (Datum ODER Versionstext)

    # ── CP/M-Attribute, aus ``attrs`` aufgeschlüsselt ───────────────────────

    @property
    def read_only(self) -> bool:
        return "RO" in self.attrs

    @property
    def system(self) -> bool:
        return "SYS" in self.attrs

    @property
    def archived(self) -> bool:
        return "ARC" in self.attrs

    @property
    def side_prefix(self) -> str:
        """``'Side1/'`` bzw. ``''`` — das Präfix, das die API auch entgegennimmt."""
        return f"{self.side_dir}/" if self.side_dir else ""

    @property
    def ref(self) -> str:
        """Eindeutige Bezeichnung über die ganze Diskette."""
        return self.side_prefix + self.name


# ════════════════════════════════════════════════════════════════════════════
# Sektoransicht (Diskeditor)
# ════════════════════════════════════════════════════════════════════════════


#: Abschnittsarten einer Spur (``TrackSpan::Kind`` der Bibliothek).
UNFORMATTED, GAP, SECTOR = 0, 1, 2


#: Schweregrade eines Befundes — die Zahlen sind ein Vertrag mit der C-ABI.
INFO, WARNUNG, FEHLER, GEFAHR = 0, 1, 2, 3
#: Ebenen: worauf der Befund beruht.  ``EBENE_ERKENNUNG`` ist die Ebene 0 (§11) —
#: sie sagt nicht, was mit dem Dateisystem ist, sondern **warum keines erkannt
#: wurde**; sie kommt nur an einer roh geöffneten Diskette vor.
EBENE_MEDIUM, EBENE_VERWALTUNG, EBENE_DATEIEN, EBENE_ERKENNUNG = 0, 1, 2, 3

SCHWERE_NAME = {INFO: "Hinweis", WARNUNG: "Warnung",
                FEHLER: "Fehler", GEFAHR: "Gefahr"}
EBENE_NAME = {EBENE_MEDIUM: "Medium", EBENE_VERWALTUNG: "Verwaltung",
              EBENE_DATEIEN: "Dateien", EBENE_ERKENNUNG: "Erkennung"}


@dataclass(frozen=True)
class Repair:
    """Ein Reparaturvorschlag zu genau einem Befund.

    ``blocked`` ist kein Versehen, sondern eine Zusage (E8): ein Eingriff, der
    etwas **freigibt**, bleibt gesperrt, solange das Wissen über die Diskette
    unvollständig ist — aus halbem Wissen einen Belegungsplan zu bauen ist der eine
    Fehler, den ein fsck nie machen darf.
    """

    id: str            # stabile Kennung, z. B. "udos.karte.sektoren.sperren"
    text: str
    destructive: bool = False   # verwirft Nutzdaten oder einen Verweis darauf
    recommended: bool = False   # höchstens einer je Befund
    guessed: bool = False       # stellt einen nicht ableitbaren Wert her
    blocked: bool = False
    blocked_why: str = ""

    @property
    def vorauswaehlen(self) -> bool:
        """Darf die Oberfläche ihn ankreuzen, ohne zu fragen?

        Nur der empfohlene Weg, und nur wenn er nichts verwirft — nichts ist
        vorausgewählt, was Daten kostet (Entwurf §16.3).
        """
        return self.recommended and not self.destructive and not self.blocked


@dataclass(frozen=True)
class Finding:
    """Ein Befund der Dateisystemprüfung.

    ``severity == GEFAHR`` heißt nicht „Daten sind verloren", sondern **„der
    nächste Schreibvorgang zerstört Daten"** — das ist die einzige Auskunft, die
    eine sofortige Handlung nach sich zieht.
    """

    id: str            # stabile Kennung, z. B. "cpm.block.doppelt"
    severity: int
    layer: int
    volume: int
    object: str
    text: str
    cyl: int = -1      # -1 = ortlos (kein Sprung in den Diskeditor)
    head: int = -1
    sector: int = -1
    #: 0..n Reparaturvorschläge; leer = der Befund ist eine reine Auskunft.
    repairs: tuple = ()

    @property
    def schwere(self) -> str:
        return SCHWERE_NAME.get(self.severity, "?")

    @property
    def ebene(self) -> str:
        return EBENE_NAME.get(self.layer, "?")

    @property
    def ortbar(self) -> bool:
        """Lässt sich der Befund im Diskeditor zeigen?"""
        return self.cyl >= 0 and self.head >= 0


@dataclass(frozen=True)
class Schritt:
    """Eine Zeile der Checkliste — was die Prüfung bzw. die Suche getan hat (§5a).

    Sie kommt aus dem Prüfer selbst, nicht aus der Oberfläche: eine Checkliste,
    die etwas behauptet, was nicht gelaufen ist, wäre schlimmer als gar keine.
    ``ausgefuehrt=False`` heißt „übersprungen" und trägt den Grund — das ist
    ebenfalls eine Auskunft und darf nicht weggelassen werden.
    """

    id: str = ""
    titel: str = ""
    ausgefuehrt: bool = True
    grund: str = ""
    #: Befunde (Prüfung) bzw. Funde (Suche), die in diesem Schritt entstanden.
    treffer: int = 0
    #: Schwerste Schwere darin — bei der Suche immer ``INFO``.
    hoechste: int = 0

    @property
    def zeichen(self) -> str:
        """Haken, Warnzeichen oder Strich — die Anzeige in einem Zeichen."""
        if not self.ausgefuehrt:
            return "–"
        # Ein Haken auch dann, wenn etwas herauskam, solange nichts davon ein
        # Mangel ist: bei der SUCHE sind Treffer die gute Nachricht, und ein
        # Hinweis der Prüfung („7 gelöschte Plätze") ist keiner.
        return {GEFAHR: "⛔", FEHLER: "✖", WARNUNG: "⚠"}.get(self.hoechste, "✔")


@dataclass(frozen=True)
class CheckReport:
    """Das Ergebnis einer Prüfung."""

    findings: tuple = ()
    #: ``False`` = an einer physischen Diskette waren Spuren noch nicht gelesen.
    #: „Ohne Befund" heißt dann nur „bislang ohne Befund".
    complete: bool = True
    tracks_read: int = 0
    tracks_total: int = 0
    summary: str = ""
    #: Lief eine **Vollprüfung**?  Nicht dasselbe wie ``complete`` (das sagt, ob
    #: alle Spuren dafür da waren).
    voll: bool = False
    #: Die abgearbeitete Checkliste (:class:`Schritt`), in der Reihenfolge des Laufs.
    schritte: tuple = ()

    def __bool__(self) -> bool:
        """Wahr, wenn es etwas zu berichten gibt."""
        return bool(self.findings)

    @property
    def hoechste(self) -> int:
        return max((f.severity for f in self.findings), default=INFO)

    def ab(self, schwere: int) -> tuple:
        """Alle Befunde mindestens dieser Schwere."""
        return tuple(f for f in self.findings if f.severity >= schwere)


#: Güte eines Fundes — die Zahlen sind ein Vertrag mit der C-ABI.
BRUCHSTUECK, WAHRSCHEINLICH, SICHER = 0, 1, 2
#: Suchtiefe: nur die Verzeichnisreste (billig) oder die ganze Oberfläche.
SUCHE_VERZEICHNIS, SUCHE_OBERFLAECHE = 0, 1

GUETE_NAME = {SICHER: "sicher", WAHRSCHEINLICH: "wahrscheinlich",
              BRUCHSTUECK: "Bruchstück"}


@dataclass(frozen=True)
class RecoverFind:
    """Ein Fund der Wiederherstellung — eine gelöschte Datei oder ein Rohbereich.

    ``restorable`` ist nicht dasselbe wie „rettbar": **herausholen** lässt sich
    jeder Fund, auch an einer schreibgeschützten Diskette (E7).  Auf der Diskette
    wieder **eintragen** lässt sich nur, was keiner lebenden Datei ins Gehege
    kommt — sonst entstünde genau die Kreuzbelegung, die die Prüfung als Gefahr
    meldet.  ``blocked_why`` nennt den Grund.
    """

    name: str          # "" = kein Name überliefert (Rohbereich, UDOS)
    type: str          # Dateityp bzw. Einordnung des Inhalts
    origin: str
    suggestion: str    # Vorschlag für den Linux-Dateinamen — nie leer
    volume: int
    size: int
    quality: int
    detail: str = ""
    restorable: bool = False
    blocked_why: str = ""
    cyl: int = -1
    head: int = -1
    sector: int = -1
    #: **Alle** Sektoren des Fundes als ``(cyl, head, sektor_kennung)``, in
    #: Lesereihenfolge — bei UDOS steht der Kopfsektor vorn.  Damit blättert der
    #: Rettungsdialog durch den Fund, BEVOR etwas geschieht: „ist das überhaupt,
    #: was ich suche?" lässt sich sonst nicht beantworten, und bei UDOS gibt es
    #: dafür nicht einmal einen Namen.  Auf 512 Einträge begrenzt.
    parts: tuple = ()

    @property
    def guete(self) -> str:
        return GUETE_NAME.get(self.quality, "?")

    @property
    def ortbar(self) -> bool:
        """Lässt sich der Fund im Diskeditor zeigen?"""
        return self.cyl >= 0 and self.head >= 0


@dataclass(frozen=True)
class RecoverReport:
    """Das Ergebnis eines Suchlaufs."""

    finds: tuple = ()
    #: ``False`` = an einer physischen Diskette waren Spuren noch nicht gelesen;
    #: „nichts gefunden" heißt dann nur „bislang nichts gefunden".
    complete: bool = True
    #: Lief die Suche über die **ganze Oberfläche**?
    voll: bool = False
    #: Die abgearbeitete Checkliste (:class:`Schritt`), in der Reihenfolge des Laufs.
    schritte: tuple = ()

    def __bool__(self) -> bool:
        return bool(self.finds)

    def ab(self, guete: int) -> tuple:
        """Alle Funde mindestens dieser Güte."""
        return tuple(f for f in self.finds if f.quality >= guete)


@dataclass(frozen=True)
class Span:
    """Ein Abschnitt einer Spur — Sektor, Gap oder unformatiert.

    ``start``/``end`` sind Bruchteile **einer Umdrehung** (0 = Index, bei der
    Darstellung 12 Uhr).  Sie stammen aus der Byteposition in der Spur — eine Spur
    *ist* genau eine Umdrehung —, nicht aus der Drehzahl im HFE-Kopf.
    """

    kind: int
    start: float
    end: float
    index: int = -1          # laufende Nummer des Sektors in der Spur
    id: int = 0              # Angaben aus dem ID-Feld …
    cyl: int = 0             # … die von der tatsächlichen Lage abweichen dürfen
    head: int = 0
    size: int = 0
    id_crc_ok: bool = False
    data_crc_ok: bool = False
    deleted: bool = False
    #: Datenfeld ohne unterscheidbaren Inhalt (alle Bytes gleich) — so sieht ein
    #: formatierter, nie beschriebener Sektor aus.  Der UDOS-Anhang zählt nicht mit.
    blank: bool = False
    #: Bytes hinter der Daten-CRC, die KEIN Gap sind (0 = keine, UDOS = 4).
    tail_bytes: int = 0

    @property
    def is_sector(self) -> bool:
        return self.kind == SECTOR

    @property
    def ok(self) -> bool:
        """Beide CRCs stimmen — nur dann ist der Sektor lesbar."""
        return self.id_crc_ok and self.data_crc_ok


@dataclass(frozen=True)
class Track:
    """Eine Spur: Kopfangaben und ihre lückenlose Abschnittsfolge."""

    cyl: int
    head: int
    exists: bool
    formatted: bool
    encoding: str            # 'MFM' | 'FM'
    bytes: int               # Spurlänge (eine Umdrehung)
    sectors: int
    spans: List[Span]


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
    def open_physical(cls, sync, filesystem: Optional[str] = None,
                      read_only: bool = True) -> "DiskTool":
        """**Physische Diskette** in einem echten Laufwerk öffnen.

        ``sync`` ist ein :class:`app.gw.Sync` (oder dessen rohes Handle), der von einem
        laufenden Arbeitsfaden bedient wird — ohne den blockiert der Aufruf bis zur
        Frist.  Ein Handle lässt sich nur **einmal** öffnen.

        **Der Aufruf liest die ganze Diskette**: die Formaterkennung sieht sich jede
        Spur an (rund eine Sekunde je Spur).  Er gehört deshalb in einen Arbeitsfaden
        mit Fortschrittsanzeige, nicht in den Oberflächenfaden.

        Raises:
            K1520DiskError: mit der Meldung der Bibliothek.
        """
        h = _lib.k1520d_open_physical(getattr(sync, "handle", sync),
                                      _b(filesystem or ""), read_only)
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, "")

    @classmethod
    def open_raw(cls, path, filesystem: Optional[str] = None,
                 read_only: bool = True) -> "DiskTool":
        """Wie :meth:`open`, **öffnet aber auch ohne Erkennung**.

        Wird kein Dateisystem gefunden, kommt das Abbild trotzdem heraus — nur eben
        ohne (:attr:`has_filesystem` ist dann False).  Medium, Sektoreditor und
        „Speichern unter" arbeiten weiter; Dateien gibt es keine.

        Gilt für eine Datei genauso wie für eine physische Diskette: eine gemischte
        oder unbekannte Geometrie ist kein Grund, das Abbild gar nicht herzugeben.
        """
        p = os.fspath(path)
        h = _lib.k1520d_open_raw(_b(p), _b(filesystem or ""), read_only)
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, p)

    @classmethod
    def open_physical_raw(cls, sync, filesystem: Optional[str] = None,
                          read_only: bool = True) -> "DiskTool":
        """Wie :meth:`open_physical`, **öffnet aber auch ohne Erkennung**.

        Wird kein Dateisystem gefunden, kommt die Diskette trotzdem heraus — nur
        eben ohne (:attr:`has_filesystem` ist dann False).  Medium, Sektoreditor,
        Abbild sichern und die Schnittwerkzeuge arbeiten weiter; Dateien gibt es
        keine.  Der Grund steht in :attr:`remarks`.
        """
        h = _lib.k1520d_open_physical_raw(getattr(sync, "handle", sync),
                                          _b(filesystem or ""), read_only)
        if not h:
            raise K1520DiskError(_s(_lib.k1520d_last_open_error()))
        return cls(h, "")

    @property
    def has_filesystem(self) -> bool:
        """Ist ein Dateisystem gemountet?  False = roh geöffnet."""
        return bool(_lib.k1520d_has_filesystem(self._h))

    def redetect(self, filesystem: Optional[str] = None) -> bool:
        """Erkennung am **Speicherabbild** wiederholen — ohne neu einzulesen.

        Für „Dateisystem von Hand wählen" und für die Zeit nach einem Schnitt.

        Args:
            filesystem: erzwingen; None/"" = wieder erkennen lassen.

        Returns:
            True, wenn danach ein Dateisystem gemountet ist.
        """
        if not _lib.k1520d_redetect(self._h, _b(filesystem or "")):
            raise K1520DiskError(self._fail())
        return self.has_filesystem

    def keep_even_tracks(self) -> int:
        """Jede zweite Spur wegwerfen — **löst das Abbild vom Laufwerk**.

        Aus einer im Doppelschritt beschriebenen, aber einfachschrittig gelesenen
        Diskette wird das, was ein 40-Spur-Laufwerk sieht.

        Returns: verbliebene Spuren.
        """
        n = int(_lib.k1520d_keep_even_tracks(self._h))
        if n < 0:
            raise K1520DiskError(self._fail())
        return n

    def drop_second_side(self) -> int:
        """Seite 1 wegwerfen — **löst das Abbild vom Laufwerk**."""
        n = int(_lib.k1520d_drop_second_side(self._h))
        if n < 0:
            raise K1520DiskError(self._fail())
        return n

    def delete_cylinder(self, cyl: int) -> int:
        """Einen ganzen Zylinder löschen; alles dahinter rückt auf.

        Für Abbilder mit mehr Spuren, als hineingehören (82 statt 80), und zum
        Zurechtstutzen auf eine Zielgeometrie (77 Spuren für 8″).

        Returns: verbliebene Zylinder.
        """
        n = int(_lib.k1520d_delete_cylinder(self._h, cyl))
        if n < 0:
            raise K1520DiskError(self._fail())
        return n

    def insert_cylinder_at(self, pos: int, mfm: bool = True) -> int:
        """Einen **unformatierten** Zylinder an Position ``pos`` einfügen.

        Der neue Zylinder trägt die Nummer ``pos``; alles von dort an rückt nach
        hinten.  ``pos`` darf die Spurzahl sein (anhängen) und **0** (vor alle
        bestehenden) — das braucht man für eine FM-Spur 0 vor MFM-Daten.

        Das Verfahren folgt **nicht** dem Nachbarn: gerade der Wechsel ist der
        Zweck.  Sektoren legt man danach im Diskeditor an.

        Returns: verbliebene Zylinder.
        """
        n = int(_lib.k1520d_insert_cylinder_at(self._h, pos, mfm))
        if n < 0:
            raise K1520DiskError(self._fail())
        return n

    def write_to_physical(self, sync) -> int:
        """Das Speicherabbild auf ein **echtes Laufwerk** legen.

        Jede bekannte Spur wandert in das Medium hinter ``sync`` und gilt dort als
        geändert — der Arbeitsfaden schreibt sie im Hintergrund auf die eingelegte
        Diskette.  Gewartet wird nicht; der Fortschritt steht in ``sync.stats``,
        abgeschlossen wird mit ``sync.flush()``.

        So kommt eine geladene ``.hfe`` auf eine echte Diskette.  **Was auf der
        Zieldiskette stand, ist danach fort.**

        Returns:
            Zahl der eingestellten Spuren.

        Raises:
            K1520DiskError: wenn gar nichts kopiert wurde (z. B. passt die Diskette
                nicht in die eingestellte Laufwerksgeometrie).
        """
        n = int(_lib.k1520d_write_to_physical(self._h,
                                              getattr(sync, "handle", sync)))
        if n < 0:
            raise K1520DiskError(self._fail())
        return n

    @staticmethod
    def probe_track_count(num_cyls: int, num_heads: int) -> int:
        """Wie viele Spuren die Formaterkennung holen wird (Ziel der Fortschrittsanzeige).

        Kommt aus der Bibliothek, damit die Oberfläche die Sondenregel nicht nachbaut
        und dabei von ihr abweicht (§11.2a).
        """
        return int(_lib.k1520d_probe_track_count(int(num_cyls), int(num_heads)))

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
    def filesystem_type(self) -> str:
        """``'cpm'`` | ``'udos'`` | ``'udos1715'`` | ``''`` — die Familie.

        Die Oberfläche braucht sie, um zu wissen, *welche* Dateiangaben es
        überhaupt gibt.  Leer, wenn der Name in keinem Katalog steht (abgeleitete
        Profile wie ``cpa_auto`` gelten als CP/M).
        """
        typ = _s(_lib.k1520d_fs_type(_b(self.filesystem)))
        if typ:
            return typ
        return "cpm" if self.filesystem.startswith("cpa") else ""

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
        """Auffälligkeiten des Mediums (Altbestand, CRC-Fehler); '' = ohne Befund.

        Achtung: nach einer Stichprobenerkennung gilt das nur für die angesehenen
        Spuren — wie viele das waren, sagt :attr:`examined_tracks`.
        """
        return _s(_lib.k1520d_detection_remarks(self._h))

    @property
    def examined_tracks(self) -> int:
        """Über wie viele Spuren :attr:`remarks` urteilt; 0 = über die ganze Diskette."""
        return int(_lib.k1520d_detection_examined_tracks(self._h))

    def refresh_detection(self) -> bool:
        """Befund neu bewerten, sobald die Diskette vollständig gelesen ist.

        Returns:
            True, wenn sich die Meldung geändert hat (Anzeige auffrischen).
        """
        return bool(_lib.k1520d_refresh_detection(self._h))

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
        """Verzeichnis aller Seiten — **immer frisch** aus dem Medium gelesen.

        Bei UDOS liest das zu JEDER Datei den Kopfsektor; an einem echten Laufwerk
        ist :meth:`list_names` der schnelle Weg (§11.2b).
        """
        return [self._entry(i) for i in range(_lib.k1520d_list(self._h))]

    def list_names(self) -> List[Entry]:
        """Verzeichnis **nur mit den Namen** — bei UDOS drei Spuren statt drei Dutzend.

        Die übrigen Angaben (Größe, Typ, Datum) sind dann leer und
        ``details_loaded`` ist False; nachzutragen mit :meth:`load_entry_details`.
        Bei CP/M ist das Ergebnis dasselbe wie bei :meth:`list` — dort steht alles
        im Verzeichniseintrag selbst.
        """
        return [self._entry(i) for i in range(_lib.k1520d_list_names(self._h))]

    def entry_details_ready(self, i: int) -> bool:
        """Wären die Angaben zu Eintrag ``i`` **ohne Warten** zu haben?"""
        return bool(_lib.k1520d_entry_details_ready(self._h, i))

    def load_entry_details(self, i: int) -> Entry:
        """Angaben zu Eintrag ``i`` nachtragen und den aktualisierten Eintrag liefern.

        **Blockiert** an einem echten Laufwerk, wenn der Kopfsektor erst geholt
        werden muss — vorher :meth:`entry_details_ready` fragen.
        """
        _lib.k1520d_entry_load_details(self._h, i)
        return self._entry(i)

    def _entry(self, i: int) -> Entry:
        """Einen Eintrag aus dem Stand der Bibliothek zusammensetzen."""
        v = _lib.k1520d_entry_volume(self._h, i)
        return Entry(
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
            bytes_in_last=int(_lib.k1520d_entry_bytes_in_last(self._h, i)),
            extra=int(_lib.k1520d_entry_extra(self._h, i)),
            segments=_s(_lib.k1520d_entry_segments(self._h, i)),
            created=_s(_lib.k1520d_entry_created(self._h, i)),
            details_loaded=bool(_lib.k1520d_entry_details_loaded(self._h, i)),
        )

    def check_report(self) -> str:
        """Mehrzeiliger Überblick als Text (Datenträger, Belegung, Auffälligkeiten)."""
        return _s(_lib.k1520d_check_report(self._h))

    # ── Dateisystemprüfung ──────────────────────────────────────────────────
    #
    # Beim Öffnen läuft automatisch eine Schnellprüfung; ``findings()`` liefert
    # ihr Ergebnis, ohne dass jemand ``check()`` rufen müsste.

    def check(self, *, voll: bool = False, nachladen: bool = True) -> "CheckReport":
        """Neu prüfen und den Bericht liefern.  **Ändert die Diskette nicht.**

        ``voll=True`` fasst jede Spur an — an einem echten Laufwerk sind das ein
        bis zwei Minuten, der Aufruf gehört dann in einen Arbeitsfaden.
        """
        _lib.k1520d_check(self._h, 1 if voll else 0, nachladen)
        return self.findings()

    def findings(self) -> "CheckReport":
        """Der zuletzt erhobene Befund (beim Öffnen: die Schnellprüfung)."""
        return CheckReport(
            findings=tuple(self._finding(i)
                           for i in range(int(_lib.k1520d_finding_count(self._h)))),
            complete=bool(_lib.k1520d_check_complete(self._h)),
            tracks_read=int(_lib.k1520d_check_tracks_read(self._h)),
            tracks_total=int(_lib.k1520d_check_tracks_total(self._h)),
            summary=_s(_lib.k1520d_check_summary(self._h)),
            voll=int(_lib.k1520d_check_level(self._h)) == 1,
            schritte=tuple(
                Schritt(
                    id=_s(_lib.k1520d_check_step_id(self._h, k)),
                    titel=_s(_lib.k1520d_check_step_title(self._h, k)),
                    ausgefuehrt=bool(_lib.k1520d_check_step_done(self._h, k)),
                    grund=_s(_lib.k1520d_check_step_why(self._h, k)),
                    treffer=int(_lib.k1520d_check_step_findings(self._h, k)),
                    hoechste=int(_lib.k1520d_check_step_severity(self._h, k)),
                )
                for k in range(int(_lib.k1520d_check_step_count(self._h)))),
        )

    def _finding(self, i: int) -> "Finding":
        return Finding(
            id=_s(_lib.k1520d_finding_id(self._h, i)),
            severity=int(_lib.k1520d_finding_severity(self._h, i)),
            layer=int(_lib.k1520d_finding_layer(self._h, i)),
            volume=int(_lib.k1520d_finding_volume(self._h, i)),
            object=_s(_lib.k1520d_finding_object(self._h, i)),
            text=_s(_lib.k1520d_finding_text(self._h, i)),
            cyl=int(_lib.k1520d_finding_cyl(self._h, i)),
            head=int(_lib.k1520d_finding_head(self._h, i)),
            sector=int(_lib.k1520d_finding_sector(self._h, i)),
            repairs=tuple(self._repair(i, j)
                          for j in range(int(_lib.k1520d_repair_count(self._h, i)))),
        )

    def _repair(self, i: int, j: int) -> "Repair":
        return Repair(
            id=_s(_lib.k1520d_repair_id(self._h, i, j)),
            text=_s(_lib.k1520d_repair_text(self._h, i, j)),
            destructive=bool(_lib.k1520d_repair_destructive(self._h, i, j)),
            recommended=bool(_lib.k1520d_repair_recommended(self._h, i, j)),
            guessed=bool(_lib.k1520d_repair_guessed(self._h, i, j)),
            blocked=bool(_lib.k1520d_repair_blocked(self._h, i, j)),
            blocked_why=_s(_lib.k1520d_repair_blocked_why(self._h, i, j)),
        )

    def apply_repairs(self, auswahl) -> int:
        """Ausgewählte Reparaturen ausführen — **eine** Transaktion (Entwurf §12).

        ``auswahl`` ist eine Folge von Paaren ``(Befundnummer, Vorschlagsnummer)``
        in den zuletzt erhobenen Befund.  Die Reihenfolge ist gleichgültig:
        ausgeführt wird nach Rang (Verzeichnis → Ketten → Belegungsplan → Zähler).
        Danach wird automatisch neu geprüft — ``findings()`` trägt anschließend den
        neuen Stand, und alle bisherigen Indizes sind hinfällig.

        :return: Zahl der ausgeführten Reparaturen.
        :raises K1520DiskError: wenn nichts geschah (die Diskette ist unverändert).
        """
        paare = list(auswahl)
        if not paare:
            raise K1520DiskError("Es wurde keine Reparatur ausgewaehlt")
        n = len(paare)
        befund = (ctypes.c_int * n)(*[int(a) for a, _ in paare])
        repair = (ctypes.c_int * n)(*[int(b) for _, b in paare])
        getan = int(_lib.k1520d_apply_repairs(self._h, befund, repair, n))
        if getan < 0:
            raise K1520DiskError(self._fail())
        return getan

    # ── Wiederherstellung gelöschter Dateien (Entwurf §13) ──────────────────
    #
    # Anders als die Prüfung läuft die Suche **nicht** von selbst beim Öffnen: sie
    # kostet, und sie beantwortet eine Frage, die niemand gestellt hat.

    def recover_scan(self, *, voll: bool = False,
                     nachladen: bool = True) -> "RecoverReport":
        """Nach gelöschten Dateien suchen.  **Ändert die Diskette nicht.**

        ``voll=True`` sieht zusätzlich jeden freien Bereich der Diskette an — das
        findet die Bruchstücke ohne Verzeichnisplatz und kostet die ganze Scheibe.
        """
        _lib.k1520d_recover_scan(self._h, 1 if voll else 0, nachladen)
        return self.recover_finds()

    def recover_finds(self) -> "RecoverReport":
        """Das Ergebnis des letzten Suchlaufs (leer, solange keiner lief)."""
        return RecoverReport(
            finds=tuple(self._find(i)
                        for i in range(int(_lib.k1520d_recover_count(self._h)))),
            complete=bool(_lib.k1520d_recover_complete(self._h)),
            voll=int(_lib.k1520d_recover_level(self._h)) == 1,
            schritte=tuple(
                Schritt(
                    id=_s(_lib.k1520d_recover_step_id(self._h, k)),
                    titel=_s(_lib.k1520d_recover_step_title(self._h, k)),
                    ausgefuehrt=bool(_lib.k1520d_recover_step_done(self._h, k)),
                    grund=_s(_lib.k1520d_recover_step_why(self._h, k)),
                    treffer=int(_lib.k1520d_recover_step_finds(self._h, k)),
                )
                for k in range(int(_lib.k1520d_recover_step_count(self._h)))),
        )

    def _find(self, i: int) -> "RecoverFind":
        return RecoverFind(
            name=_s(_lib.k1520d_recover_name(self._h, i)),
            type=_s(_lib.k1520d_recover_type(self._h, i)),
            origin=_s(_lib.k1520d_recover_origin(self._h, i)),
            suggestion=_s(_lib.k1520d_recover_suggestion(self._h, i)),
            volume=int(_lib.k1520d_recover_volume(self._h, i)),
            size=int(_lib.k1520d_recover_size(self._h, i)),
            quality=int(_lib.k1520d_recover_quality(self._h, i)),
            detail=_s(_lib.k1520d_recover_detail(self._h, i)),
            restorable=bool(_lib.k1520d_recover_restorable(self._h, i)),
            blocked_why=_s(_lib.k1520d_recover_blocked_why(self._h, i)),
            cyl=int(_lib.k1520d_recover_cyl(self._h, i)),
            head=int(_lib.k1520d_recover_head(self._h, i)),
            sector=int(_lib.k1520d_recover_sector(self._h, i)),
            parts=self._find_parts(i),
        )

    def _find_parts(self, i: int) -> tuple:
        """Die Sektoren eines Fundes als ``(cyl, head, sektor)``-Tupel."""
        c, k, s = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        out = []
        for n in range(int(_lib.k1520d_recover_part_count(self._h, i))):
            if _lib.k1520d_recover_part(self._h, i, n,
                                        ctypes.byref(c), ctypes.byref(k), ctypes.byref(s)):
                out.append((c.value, k.value, s.value))
        return tuple(out)

    def recover_preview(self, i: int, n: int = 512) -> bytes:
        """Die ersten ``n`` Byte des Fundes — für die Vorschau."""
        puffer = (ctypes.c_uint8 * n)()
        menge = int(_lib.k1520d_recover_preview(self._h, i, puffer, n))
        if menge < 0:
            raise K1520DiskError(self._fail())
        return bytes(puffer[:menge])

    def recover_extract(self, i: int, dest_path) -> None:
        """Den Fund in eine Linux-Datei retten — geht auch schreibgeschützt (E7).

        Bei einer Güte unter *sicher* entsteht daneben ein Beiblatt
        ``<datei>.rettung.txt``, das die Vorbehalte im Klartext nennt.
        """
        if not _lib.k1520d_recover_extract(self._h, i, _b(str(dest_path))):
            raise K1520DiskError(self._fail())

    def recover_restore(self, i: int, name: str = "") -> None:
        """Den Fund **auf der Diskette** wieder eintragen.  Verlangt Schreibrecht.

        ``name`` leer = der überlieferte Name.  Danach wird neu gesucht und neu
        geprüft; alle bisherigen Indizes sind hinfällig.
        """
        if not _lib.k1520d_recover_restore(self._h, i, _b(name)):
            raise K1520DiskError(self._fail())

    # ── Übertragung ─────────────────────────────────────────────────────────

    def _fail(self) -> str:
        return _s(_lib.k1520d_last_error(self._h)) or "unbekannter Fehler"

    def extract(self, name: str, dest, text: bool = False) -> None:
        """Eine Datei herausholen; ``name`` darf ``'Side1/…'`` sein."""
        if not _lib.k1520d_extract(self._h, _b(name), _b(os.fspath(dest)),
                                   TEXT if text else BINARY):
            raise K1520DiskError(self._fail())

    def insert(self, src, name: str, text: bool = False,
               overwrite: bool = False, force: bool = False,
               info=None) -> None:
        """Eine Datei einfügen.

        Die Kopfsektorangaben kommen — in dieser Rangfolge — aus
        ``<src>.fileinfo``, aus dem Sammelbeiblatt des Ordners, sonst
        nirgendwoher: bei der UDOS-Familie ist das ein
        :class:`K1520DiskError`, kein Raten.  Welcher Fall es war, sagt
        :attr:`last_insert_problem`.

        Args:
            force: eine ANGABENdatei (``.fileinfo``, Sammelbeiblatt) trotzdem
                als gewöhnliche Datei auf die Diskette schreiben.
            info: Pfad einer Angabendatei in der ``.fileinfo``-Zeilenform, die
                statt der neben der Quelle liegenden gilt — der Weg des
                Eingabedialogs.
        """
        if info is not None:
            ok = _lib.k1520d_insert_with_info(
                self._h, _b(os.fspath(src)), _b(name),
                TEXT if text else BINARY, overwrite, _b(os.fspath(info)))
        else:
            fn = _lib.k1520d_insert_forced if force else _lib.k1520d_insert
            ok = fn(self._h, _b(os.fspath(src)), _b(name),
                    TEXT if text else BINARY, overwrite)
        if not ok:
            raise K1520DiskError(self._fail())

    #: :attr:`last_insert_problem` — gewöhnlicher Ablauf (auch der normale Fehler).
    INSERT_OK = 0
    #: UDOS-Familie, und es gibt weder ``.fileinfo`` noch Sammelbeiblatt.
    INSERT_ANGABEN_FEHLEN = 1
    #: Die Quelle ist selbst eine Angabendatei.
    INSERT_ZUBEHOER = 2

    @property
    def last_insert_problem(self) -> int:
        """Warum das letzte :meth:`insert` abgelehnt wurde — s. ``INSERT_*``.

        Nur nach einem Fehlschlag aussagekräftig.  Die Oberfläche antwortet auf
        die beiden Fälle verschieden: auf ``INSERT_ANGABEN_FEHLEN`` mit dem
        Eingabedialog, auf ``INSERT_ZUBEHOER`` mit einer Rückfrage.
        """
        return int(_lib.k1520d_last_insert_problem(self._h))

    @property
    def last_accessory_count(self) -> int:
        """Wie viele Angabendateien das letzte :meth:`insert_all` übersprang."""
        return int(_lib.k1520d_last_accessory_count(self._h))

    @staticmethod
    def accessory_reason(pfad) -> str:
        """``''`` oder der Grund, warum ``pfad`` eine Angabendatei ist.

        Statisch — die Oberfläche fragt das, **bevor** sie eine Diskette anfasst.
        """
        return _s(_lib.k1520d_accessory_reason(_b(os.fspath(pfad))))

    @property
    def cpm_fileinfo(self) -> bool:
        """Legt :meth:`extract` auch bei **CP/M** ein ``.fileinfo`` an?

        Vorgabe: nein.  Bei CP/M ist „Nutzerbereich 0, keine Attribute" der
        Normalfall, den auch das echte CP/M erzeugt — es geht nichts verloren.
        Bei der UDOS-Familie entsteht das ``.fileinfo`` unabhängig davon immer.
        """
        return bool(_lib.k1520d_cpm_fileinfo(self._h))

    @cpm_fileinfo.setter
    def cpm_fileinfo(self, an: bool) -> None:
        _lib.k1520d_set_cpm_fileinfo(self._h, bool(an))

    def erase(self, name: str) -> None:
        if not _lib.k1520d_erase(self._h, _b(name)):
            raise K1520DiskError(self._fail())

    def first_sector(self, name: str):
        """Der erste Sektor der Datei als ``(cyl, head, sector)`` — sonst ``None``.

        Bei CP/M der erste Block von Extent 0, bei UDOS/NDOS der Kopfsektor.  Der
        Wert ist die Sektor-**Kennung**, wie sie der Diskeditor sucht.
        """
        c, k, s = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        if not _lib.k1520d_file_first_sector(self._h, _b(name),
                                             ctypes.byref(c), ctypes.byref(k),
                                             ctypes.byref(s)):
            return None
        return (c.value, k.value, s.value)

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
                       extra: Optional[int] = None,
                       segments: Optional[str] = None) -> None:
        """UDOS-Kopfsektorangaben einer vorhandenen Datei ändern.

        Der Dateiinhalt bleibt unangetastet; **nicht angegebene Felder bleiben
        stehen**.  ``segment=(anfang, länge)`` setzt nur das ERSTE Segment;
        ``segments="4400+0041 8442+0026"`` setzt die ganze Liste und geht vor —
        eine Programmdatei kann mehr als zwei haben.  ``memory=(low, high, stack)``;
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
                extra is not None, int(extra or 0),
                segments is not None, _b(segments or "")):
            raise K1520DiskError(self._fail())

    def set_cpm_attrs(self, name: str, *, read_only: Optional[bool] = None,
                      system: Optional[bool] = None,
                      archived: Optional[bool] = None,
                      user: Optional[int] = None) -> None:
        """CP/M-Attribute und Nutzerbereich einer vorhandenen Datei ändern.

        Der Dateiinhalt bleibt unangetastet; **nicht angegebene Felder bleiben
        stehen**.  Ein Wechsel des Nutzerbereichs verschiebt die Datei nach
        ``3:NAME.TYP``.

        Raises:
            K1520DiskError: schreibgeschützt, Datei unbekannt, Zielbereich belegt,
                oder UDOS (dort gibt es diese Angaben nicht).
        """
        if not _lib.k1520d_set_cpm_attrs(
                self._h, _b(name),
                read_only is not None, bool(read_only),
                system is not None, bool(system),
                archived is not None, bool(archived),
                user is not None, int(user or 0)):
            raise K1520DiskError(self._fail())

    # ── Sektoransicht (Diskeditor) ──────────────────────────────────────────

    @property
    def medium_cylinders(self) -> int:
        """Zylinder des MEDIUMS — was da ist, nicht was das Format vorsieht."""
        return int(_lib.k1520d_medium_cylinders(self._h))

    @property
    def medium_heads(self) -> int:
        return int(_lib.k1520d_medium_heads(self._h))

    #: Spurzustände (siehe :meth:`track_state`).
    SPUR_UNBEKANNT, SPUR_SAUBER, SPUR_GEAENDERT = 0, 1, 2

    def track_state(self, cyl: int, head: int) -> int:
        """Zustand einer Spur, **ohne** sie zu holen.

        ``0`` = noch nie gelesen (an einem echten Laufwerk; der Inhalt ist dann
        bedeutungslos), ``1`` = sauber, ``2`` = geändert.  Eine Übersicht über die
        ganze Diskette muss das fragen, bevor sie :meth:`track` ruft — sonst lädt
        das blosse Zeichnen die Diskette vollständig nach.
        """
        return int(_lib.k1520d_track_state(self._h, cyl, head))

    def track(self, cyl: int, head: int) -> Track:
        """Eine Spur mit allen Abschnitten — immer frisch aus dem Medium.

        **Blockiert** an einem echten Laufwerk, wenn die Spur noch unbekannt ist.
        """
        n = _lib.k1520d_track_scan(self._h, cyl, head)
        spans = []
        for i in range(max(0, n)):
            art = int(_lib.k1520d_span_kind(self._h, i))
            spans.append(Span(
                kind=art,
                start=float(_lib.k1520d_span_start(self._h, i)),
                end=float(_lib.k1520d_span_end(self._h, i)),
                index=int(_lib.k1520d_span_index(self._h, i)),
                id=int(_lib.k1520d_span_id(self._h, i)),
                cyl=int(_lib.k1520d_span_cyl(self._h, i)),
                head=int(_lib.k1520d_span_head(self._h, i)),
                size=int(_lib.k1520d_span_size(self._h, i)),
                id_crc_ok=bool(_lib.k1520d_span_id_crc_ok(self._h, i)),
                data_crc_ok=bool(_lib.k1520d_span_data_crc_ok(self._h, i)),
                deleted=bool(_lib.k1520d_span_deleted(self._h, i)),
                blank=bool(_lib.k1520d_span_blank(self._h, i)),
                tail_bytes=int(_lib.k1520d_span_tail_bytes(self._h, i)),
            ))
        return Track(
            cyl=cyl, head=head,
            exists=bool(_lib.k1520d_track_exists(self._h)),
            formatted=bool(_lib.k1520d_track_formatted(self._h)),
            encoding=_s(_lib.k1520d_track_encoding(self._h)),
            bytes=int(_lib.k1520d_track_bytes(self._h)),
            sectors=int(_lib.k1520d_track_sectors(self._h)),
            spans=spans,
        )

    def sector_data(self, cyl: int, head: int, index: int) -> bytes:
        """Nutzdaten eines Sektors; ``index`` ist ``Span.index``."""
        puffer = (ctypes.c_uint8 * 1024)()
        n = _lib.k1520d_sector_read(self._h, cyl, head, index, puffer, len(puffer))
        if n < 0:
            raise K1520DiskError(self._fail())
        return bytes(puffer[:n])

    def sector_crc(self, cyl: int, head: int, index: int) -> int:
        """Daten-CRC, wie sie auf dem Medium steht."""
        v = _lib.k1520d_sector_crc(self._h, cyl, head, index)
        if v < 0:
            raise K1520DiskError(self._fail())
        return int(v)

    def sector_crc_for(self, cyl: int, head: int, index: int, data: bytes) -> int:
        """Welche Daten-CRC gehörte zu ``data``?  Ändert nichts."""
        roh = (ctypes.c_uint8 * len(data)).from_buffer_copy(bytes(data))
        v = _lib.k1520d_sector_crc_for(self._h, cyl, head, index, roh, len(data))
        if v < 0:
            raise K1520DiskError(self._fail())
        return int(v)

    def sector_write(self, cyl: int, head: int, index: int, data: bytes,
                     crc: Optional[int] = None) -> None:
        """Datenfeld eines Sektors ersetzen — in die Diskette **im Speicher**.

        ``crc=None`` rechnet die Daten-CRC neu; ein angegebener Wert wird wörtlich
        geschrieben, auch wenn er falsch ist (eine schadhafte Diskette lässt sich so
        originalgetreu nachbilden).  In die Datei kommt es erst mit ``flush()``.
        """
        roh = (ctypes.c_uint8 * len(data)).from_buffer_copy(bytes(data))
        if not _lib.k1520d_sector_write(self._h, cyl, head, index, roh, len(data),
                                        -1 if crc is None else int(crc)):
            raise K1520DiskError(self._fail())

    def sector_tail(self, cyl: int, head: int, index: int) -> bytes:
        """Bytes **hinter** der Daten-CRC.

        Bei UDOS ist das der 4-Byte-Sektorkontrollblock (Rückwärts- und
        Vorwärtszeiger, `doc/udos_diskettenformat.md` §1.1); auf einer gewöhnlichen
        IBM-Spur stehen dort schlicht Gap-Füllbytes.
        """
        puffer = (ctypes.c_uint8 * 8)()
        n = _lib.k1520d_sector_tail(self._h, cyl, head, index, puffer, len(puffer))
        if n < 0:
            raise K1520DiskError(self._fail())
        return bytes(puffer[:n])

    def sector_write_tail(self, cyl: int, head: int, index: int,
                          tail: bytes) -> None:
        """Nur den Nachspann schreiben — Nutzdaten und Daten-CRC bleiben.

        Bei UDOS ist das die Dateiverkettung.  Eine absichtlich falsche CRC bleibt
        falsch: sie wird wörtlich übernommen, nicht neu gerechnet.
        """
        roh = (ctypes.c_uint8 * len(tail)).from_buffer_copy(bytes(tail))
        if not _lib.k1520d_sector_write_tail(self._h, cyl, head, index, roh,
                                             len(tail)):
            raise K1520DiskError(self._fail())

    def sector_erase(self, cyl: int, head: int, index: int,
                     tail_bytes: int = 0) -> None:
        """Sektor löschen — sein Bereich wird wieder Gap (die Spurlänge bleibt)."""
        if not _lib.k1520d_sector_erase(self._h, cyl, head, index, tail_bytes):
            raise K1520DiskError(self._fail())

    def sector_create(self, cyl: int, head: int, *, id: int, size: int = 128,
                      gap: int = 0, tail_bytes: int = 0, mfm: bool = True,
                      id_cyl: Optional[int] = None, id_head: Optional[int] = None,
                      fill: int = 0xE5) -> None:
        """Sektor anlegen.  **Die ID bestimmt die Lage.**

        Der neue Sektor kommt hinter den vorhandenen mit der nächstkleineren ID, um
        ``gap`` Bytes versetzt; gibt es keinen kleineren, hinter den Index (12 Uhr).
        Die Spurlänge bleibt fest — geschrieben wird über vorhandenes Gap und, wenn
        der Gap zu knapp ist, über den Nachbarn.  Was das trifft, sagt
        :meth:`sector_plan` vorher.
        """
        if not _lib.k1520d_sector_create(
                self._h, cyl, head, id,
                cyl if id_cyl is None else id_cyl,
                head if id_head is None else id_head,
                size, gap, tail_bytes, fill, mfm):
            raise K1520DiskError(self._fail())

    def sector_plan(self, cyl: int, head: int, *, id: int, size: int = 128,
                    gap: int = 0, tail_bytes: int = 0, mfm: bool = True) -> tuple:
        """``(Byteposition, Länge)`` eines geplanten Sektors — schreibt nichts."""
        von = _lib.k1520d_sector_plan_pos(self._h, cyl, head, id, gap)
        laenge = _lib.k1520d_sector_plan_len(self._h, cyl, head, size, tail_bytes, mfm)
        if von < 0 or laenge < 0:
            raise K1520DiskError(self._fail())
        return int(von), int(laenge)

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
