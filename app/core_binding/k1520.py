"""
K1520 Emulator - Core Binding
==============================

Python ctypes wrapper for libk1520core.so C-API.
Provides high-level Python interface to K1520 emulator.

Typical usage:
    from app.core_binding.k1520 import K1520Emulator
    
    emu = K1520Emulator()
    emu.power_on()
    emu.mount_disk(0, "disk_b.img", "cpa800", False)
    
    # Run for 10000 CPU cycles
    cycles = emu.run(10000)
    
    # Get framebuffer and display
    fb = emu.get_framebuffer()
"""

import ctypes
import os
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import Optional
import threading
import time

from app import paths

# ════════════════════════════════════════════════════════════════════════════
# Library Loading
# ════════════════════════════════════════════════════════════════════════════

def find_libk1520core() -> Path:
    """Pfad der Kernbibliothek — Quellbaum wie Installation.

    Die Auflösung selbst steht in :mod:`app.paths` (eine Stelle für alle
    Pfade, siehe ``doc/design/13_distribution.md``); hier bleibt nur der
    plattformübergreifende Name der Funktion, den der Rest des Projekts kennt.

    Raises:
        FileNotFoundError: kein Kandidat existiert (Meldung listet alle auf).
    """
    return paths.core_library()

#: Ladehinweise nur auf Wunsch — in einer Installation ist die Konsole des
#: Anwenders kein Protokoll.  ``K1520_DEBUG=1`` schaltet sie ein.
_DEBUG_LOAD = bool(os.environ.get("K1520_DEBUG"))

try:
    paths.prepare_library_load()  # Windows: DLL-Suchverzeichnis anmelden
    _lib_path = find_libk1520core()
    if _DEBUG_LOAD:
        print(f"[DEBUG] Loading library from: {_lib_path}", file=sys.stderr)
    _lib = ctypes.CDLL(str(_lib_path), use_errno=True)
    if _DEBUG_LOAD:
        print(f"[DEBUG] Library loaded successfully", file=sys.stderr)
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    if _DEBUG_LOAD:
        import traceback
        traceback.print_exc()
    sys.exit(1)

# ════════════════════════════════════════════════════════════════════════════
# C-API Function Signatures
# ════════════════════════════════════════════════════════════════════════════

# Handle type (opaque pointer)
K1520Handle = ctypes.c_void_p

# Legacy K5601 CP/A format names (kept for reference).  The authoritative,
# drive-type-specific list is queried at runtime via K1520Emulator.drive_formats().
DISK_FORMATS = ["cpa780", "cpa800", "cpa640", "cpa624"]

# Core DriveProfile names per K5122 slot (see core builtinDriveProfile).
# "none" marks an empty slot ("kein Laufwerk"); "" / None keeps the default (K5601).
DRIVE_NONE = "none"

# k1520_create(machine_type: int) -> K1520Handle
_lib.k1520_create.argtypes = [ctypes.c_int]
_lib.k1520_create.restype = K1520Handle

# k1520_create_configured(machine_type, d0, d1, d2, d3: const char*) -> K1520Handle
_lib.k1520_create_configured.argtypes = [
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
]
_lib.k1520_create_configured.restype = K1520Handle

# k1520_last_init_error() -> const char*   (Grund eines fehlgeschlagenen create)
_lib.k1520_last_init_error.argtypes = []
_lib.k1520_last_init_error.restype = ctypes.c_char_p

# k1520_destroy(K1520Handle) -> void
_lib.k1520_destroy.argtypes = [K1520Handle]
_lib.k1520_destroy.restype = None

# k1520_power_on(K1520Handle) -> void
_lib.k1520_power_on.argtypes = [K1520Handle]
_lib.k1520_power_on.restype = None

# k1520_reset(K1520Handle) -> void
_lib.k1520_reset.argtypes = [K1520Handle]
_lib.k1520_reset.restype = None

# k1520_run(K1520Handle, max_cycles: int32_t) -> int32_t
_lib.k1520_run.argtypes = [K1520Handle, ctypes.c_int32]
_lib.k1520_run.restype = ctypes.c_int32

# k1520_framebuffer(K1520Handle) -> const uint8_t*
_lib.k1520_framebuffer.argtypes = [K1520Handle]
_lib.k1520_framebuffer.restype = ctypes.POINTER(ctypes.c_uint8)

# k1520_fb_width(K1520Handle) -> int
_lib.k1520_fb_width.argtypes = [K1520Handle]
_lib.k1520_fb_width.restype = ctypes.c_int

# k1520_fb_height(K1520Handle) -> int
_lib.k1520_fb_height.argtypes = [K1520Handle]
_lib.k1520_fb_height.restype = ctypes.c_int

# k1520_fb_dirty(K1520Handle) -> bool
_lib.k1520_fb_dirty.argtypes = [K1520Handle]
_lib.k1520_fb_dirty.restype = ctypes.c_bool

# k1520_fb_clear_dirty(K1520Handle) -> void
_lib.k1520_fb_clear_dirty.argtypes = [K1520Handle]
_lib.k1520_fb_clear_dirty.restype = None

# k1520_key_press(K1520Handle, keycode: uint32_t, shift: bool, ctrl: bool) -> void
_lib.k1520_key_press.argtypes = [K1520Handle, ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool]
_lib.k1520_key_press.restype = None

# k1520_key_release(K1520Handle, keycode: uint32_t) -> void
_lib.k1520_key_release.argtypes = [K1520Handle, ctypes.c_uint32]
_lib.k1520_key_release.restype = None

# k1520_translate_key(keycode: uint32_t, shift: bool, ctrl: bool) -> uint8_t
_lib.k1520_translate_key.argtypes = [ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool]
_lib.k1520_translate_key.restype = ctypes.c_uint8

# k1520_set_key_repeat_realtime(K1520Handle, realtime: bool) -> void
_lib.k1520_set_key_repeat_realtime.argtypes = [K1520Handle, ctypes.c_bool]
_lib.k1520_set_key_repeat_realtime.restype = None

# k1520_keyboard_leds(K1520Handle) -> uint32_t
_lib.k1520_keyboard_leds.argtypes = [K1520Handle]
_lib.k1520_keyboard_leds.restype = ctypes.c_uint32

# k1520_mount_disk(K1520Handle, drive: int, path: const char*, format: const char*, wp: bool) -> bool
_lib.k1520_mount_disk.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool]
_lib.k1520_mount_disk.restype = ctypes.c_bool
# k1520_mount_physical(K1520Handle, drive: int, K1520Sync, wp: bool) -> bool
# Physische Diskette am Greaseweazle (doc/design/14_physische_diskette.md).
_lib.k1520_mount_physical.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_void_p, ctypes.c_bool]
_lib.k1520_mount_physical.restype = ctypes.c_bool

# k1520_create_disk(K1520Handle, drive: int, path: const char*, format: const char*, wp: bool) -> bool
_lib.k1520_create_disk.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool]
_lib.k1520_create_disk.restype = ctypes.c_bool

# k1520_save_disk_as(K1520Handle, drive: int, path: const char*, format: const char*) -> bool
_lib.k1520_save_disk_as.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p]
_lib.k1520_save_disk_as.restype = ctypes.c_bool

# k1520_disk_raw_compatible(K1520Handle, drive: int) -> bool
_lib.k1520_disk_raw_compatible.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_raw_compatible.restype = ctypes.c_bool

# k1520_disk_path(K1520Handle, drive: int) -> const char*
_lib.k1520_disk_path.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_path.restype = ctypes.c_char_p

# k1520_disk_container(K1520Handle, drive: int) -> const char*
_lib.k1520_disk_container.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_container.restype = ctypes.c_char_p

# k1520_disk_detected_format(K1520Handle, drive: int) -> const char*
_lib.k1520_disk_detected_format.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_detected_format.restype = ctypes.c_char_p

# k1520_disk_notice(K1520Handle, drive: int) -> const char*
_lib.k1520_disk_notice.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_notice.restype = ctypes.c_char_p

# k1520_flush_disks(K1520Handle) -> bool
_lib.k1520_flush_disks.argtypes = [K1520Handle]
_lib.k1520_flush_disks.restype = ctypes.c_bool

# k1520_unmount_disk(K1520Handle, drive: int) -> bool
_lib.k1520_unmount_disk.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_unmount_disk.restype = ctypes.c_bool

# k1520_drive_format_count(K1520Handle, drive: int) -> int
_lib.k1520_drive_format_count.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_drive_format_count.restype = ctypes.c_int

# k1520_drive_format_name(K1520Handle, drive: int, index: int) -> const char*
_lib.k1520_drive_format_name.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_int]
_lib.k1520_drive_format_name.restype = ctypes.c_char_p

# k1520_drive_default_format(K1520Handle, drive: int) -> const char*
_lib.k1520_drive_default_format.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_drive_default_format.restype = ctypes.c_char_p

# k1520_format_description(K1520Handle, name: const char*) -> const char*
_lib.k1520_format_description.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_format_description.restype = ctypes.c_char_p

# k1520_formats_source(K1520Handle) -> const char*
_lib.k1520_formats_source.argtypes = [K1520Handle]
_lib.k1520_formats_source.restype = ctypes.c_char_p

# k1520_last_error(K1520Handle) -> const char*
_lib.k1520_last_error.argtypes = [K1520Handle]
_lib.k1520_last_error.restype = ctypes.c_char_p

# k1520_is_disk_active(K1520Handle, drive: int) -> bool
_lib.k1520_disk_active.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_active.restype = ctypes.c_bool

# k1520_is_disk_write_protected(K1520Handle, drive: int) -> bool
_lib.k1520_disk_write_protected.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_write_protected.restype = ctypes.c_bool

# k1520_set_write_protect(K1520Handle, drive: int, wp: bool) -> void
_lib.k1520_set_write_protect.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_bool]
_lib.k1520_set_write_protect.restype = None

# k1520_disk_led(K1520Handle, drive: int) -> bool
_lib.k1520_disk_led.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_led.restype = ctypes.c_bool

# k1520_disk_motor(K1520Handle, drive: int) -> bool
_lib.k1520_disk_motor.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_motor.restype = ctypes.c_bool

# k1520_head_loaded(K1520Handle) -> bool
_lib.k1520_head_loaded.argtypes = [K1520Handle]
_lib.k1520_head_loaded.restype = ctypes.c_bool

# ─── Bisher unbenutzte, aber exportierte C-API ──────────────────────────────
# Vollständig deklariert, damit die ctypes-Signaturen NIE von k1520_api.h
# abdriften — tests/python/test_c_api.py vergleicht Header und Bindung.

# k1520_stop(K1520Handle) -> void   (laufenden run() abbrechen)
_lib.k1520_stop.argtypes = [K1520Handle]
_lib.k1520_stop.restype = None

# ─── A5120.16: Erweiterungsmodul (EM064/EM256 mit U8001) ─────────────────────
class K1520EmState(ctypes.Structure):
    """Spiegel von `K1520EmState` (core/api/k1520_api.h) — Aufbau wird gegen
    `k1520_em_state_size()` geprüft (tests/python/test_c_api.py)."""
    _fields_ = [
        ("r", ctypes.c_uint16 * 14),
        ("r14", ctypes.c_uint16 * 2),
        ("r15", ctypes.c_uint16 * 2),
        ("fcw", ctypes.c_uint16),
        ("pc", ctypes.c_uint16),
        ("psap_seg", ctypes.c_uint16),
        ("psap_off", ctypes.c_uint16),
        ("refresh", ctypes.c_uint16),
        ("pc_seg", ctypes.c_uint8),
        ("model", ctypes.c_uint8),
        ("cycles", ctypes.c_uint64),
        ("in_reset", ctypes.c_bool),
        ("halted", ctypes.c_bool),
        ("stopped", ctypes.c_bool),
        ("bus_ack", ctypes.c_bool),
        ("mo_active", ctypes.c_bool),
        ("mode8", ctypes.c_bool),
        ("ramen", ctypes.c_bool),
        ("tren", ctypes.c_bool),
        ("trq8", ctypes.c_bool),
        ("busrq16", ctypes.c_bool),
        ("stop16", ctypes.c_bool),
        ("reset16", ctypes.c_bool),
        ("vi_pending", ctypes.c_bool),
        ("nvi", ctypes.c_bool),
        ("parity_error", ctypes.c_bool),
        ("a33", ctypes.c_uint8),
        ("a35", ctypes.c_uint8),
        ("status8", ctypes.c_uint8),
        ("vector8", ctypes.c_uint8),
        ("a53", ctypes.c_uint8),
        ("segment", ctypes.c_uint8),
        ("seg_mode", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8),
    ]


# k1520_create_prg710(variante, d0..d3) -> K1520Handle   (0 = PRG 710, 1 = PRG 710-1)
_lib.k1520_create_prg710.argtypes = [
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
]
_lib.k1520_create_prg710.restype = K1520Handle

# k1520_create_k8915(generation, d0..d3) -> K1520Handle   (0 = V3, 1 = Gen 2; 2 = Gen 1 → NULL)
_lib.k1520_create_k8915.argtypes = [
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
]
_lib.k1520_create_k8915.restype = K1520Handle

# k1520_create_pc1715(variante, bildschirm, zeichensatz, d0..d3) -> K1520Handle
#   variante 0 = PC 1715; bildschirm 0 = K7222 (80x24), 1 = K7221 (64x16); zeichensatz 0 = S619, 1 = S602
_lib.k1520_create_pc1715.argtypes = [
    ctypes.c_int, ctypes.c_int, ctypes.c_int,
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
]
_lib.k1520_create_pc1715.restype = K1520Handle

# k1520_create_pc1715_ex(variante, bildschirm, zg_satz, zg_db6, tastatur, d0..d3) -> K1520Handle  (AP-6)
#   zg_satz 0 = deutsch, 1 = polnisch, 2 = kyrillisch; zg_db6 wie zeichensatz oben; tastatur 0 = S600, 1 = TAST_618
_lib.k1520_create_pc1715_ex.argtypes = [
    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
]
_lib.k1520_create_pc1715_ex.restype = K1520Handle

# k1520_create_p8000(konfig: const char*) -> K1520Handle   (Entwurf 25 §10.9; "schluessel=wert,…")
_lib.k1520_create_p8000.argtypes = [ctypes.c_char_p]
_lib.k1520_create_p8000.restype = K1520Handle

# P8000-Terminals (k1520_term_*; andere Maschinen: 0 / -1 / False)
_lib.k1520_term_count.argtypes = [K1520Handle]
_lib.k1520_term_count.restype = ctypes.c_int
_lib.k1520_term_tty.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_term_tty.restype = ctypes.c_int
_lib.k1520_term_char.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.k1520_term_char.restype = ctypes.c_uint8
_lib.k1520_term_attr.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.k1520_term_attr.restype = ctypes.c_uint8
_lib.k1520_term_text.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_term_text.restype = ctypes.c_int
_lib.k1520_term_cursor.argtypes = [K1520Handle, ctypes.c_int, ctypes.POINTER(ctypes.c_int),
                                   ctypes.POINTER(ctypes.c_int)]
_lib.k1520_term_cursor.restype = ctypes.c_bool
_lib.k1520_term_mode.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_term_mode.restype = ctypes.c_int
_lib.k1520_term_key.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool]
_lib.k1520_term_key.restype = ctypes.c_bool
_lib.k1520_term_send.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_term_send.restype = ctypes.c_bool

# k1520_create_with_em(type, d0..d3, em: const char*) -> K1520Handle
_lib.k1520_create_with_em.argtypes = [
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_char_p
]
_lib.k1520_create_with_em.restype = K1520Handle

# RAM-Floppy RAF (doc/design/22_raf512.md §6)
_lib.k1520_raf_install.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_raf_install.restype = ctypes.c_bool
_lib.k1520_raf_variant.argtypes = [K1520Handle]
_lib.k1520_raf_variant.restype = ctypes.c_char_p
_lib.k1520_raf_peek.argtypes = [K1520Handle, ctypes.c_uint32]
_lib.k1520_raf_peek.restype = ctypes.c_uint8
_lib.k1520_raf_load.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_raf_load.restype = ctypes.c_bool
_lib.k1520_raf_save.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_raf_save.restype = ctypes.c_bool

# k1520_em_variant(K1520Handle) -> const char*   ("" | "em064" | "em256")
_lib.k1520_em_variant.argtypes = [K1520Handle]
_lib.k1520_em_variant.restype = ctypes.c_char_p

# k1520_em_led_v1 / _v2 / k1520_em_mode16 (K1520Handle) -> bool
_lib.k1520_em_led_v1.argtypes = [K1520Handle]
_lib.k1520_em_led_v1.restype = ctypes.c_bool
_lib.k1520_em_led_v2.argtypes = [K1520Handle]
_lib.k1520_em_led_v2.restype = ctypes.c_bool
_lib.k1520_em_mode16.argtypes = [K1520Handle]
_lib.k1520_em_mode16.restype = ctypes.c_bool

# k1520_em_state_size() -> int
_lib.k1520_em_state_size.argtypes = []
_lib.k1520_em_state_size.restype = ctypes.c_int

# k1520_em_state(K1520Handle, K1520EmState*) -> bool
_lib.k1520_em_state.argtypes = [K1520Handle, ctypes.POINTER(K1520EmState)]
_lib.k1520_em_state.restype = ctypes.c_bool

# k1520_version() -> const char*
_lib.k1520_version.argtypes = []
_lib.k1520_version.restype = ctypes.c_char_p

# k1520_mem_read(K1520Handle, addr: uint16) -> uint8
_lib.k1520_mem_read.argtypes = [K1520Handle, ctypes.c_uint16]
_lib.k1520_mem_read.restype = ctypes.c_uint8

# k1520_mem_write(K1520Handle, addr: uint16, data: uint8) -> void
_lib.k1520_mem_write.argtypes = [K1520Handle, ctypes.c_uint16, ctypes.c_uint8]
_lib.k1520_mem_write.restype = None

# k1520_io_read(K1520Handle, port: uint8) -> uint8
_lib.k1520_io_read.argtypes = [K1520Handle, ctypes.c_uint8]
_lib.k1520_io_read.restype = ctypes.c_uint8

# k1520_set_console_mode(K1520Handle, enable: bool) -> void
_lib.k1520_set_console_mode.argtypes = [K1520Handle, ctypes.c_bool]
_lib.k1520_set_console_mode.restype = None

# k1520_console_poll(K1520Handle, x*, y*, ch*) -> bool
_lib.k1520_console_poll.argtypes = [K1520Handle, ctypes.POINTER(ctypes.c_int),
                                    ctypes.POINTER(ctypes.c_int), ctypes.c_char_p]
_lib.k1520_console_poll.restype = ctypes.c_bool

# k1520_console_key(K1520Handle, c: char) -> void
_lib.k1520_console_key.argtypes = [K1520Handle, ctypes.c_char]
_lib.k1520_console_key.restype = None

# k1520_serial_send(K1520Handle, port: int, byte: uint8) -> void
_lib.k1520_serial_send.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_uint8]
_lib.k1520_serial_send.restype = None

# k1520_serial_set_rx_cb(K1520Handle, port: int, cb, user*) -> void
# K1520SerialCallback = void (*)(void* ctx, uint8_t byte) — erst der Kontext, dann das
# Byte (bis AP-T1b stand es hier vertauscht: ein Python-Rückruf bekam ctx als Byte).
K1520SerialRxCb = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint8)
_lib.k1520_serial_set_rx_cb.argtypes = [K1520Handle, ctypes.c_int,
                                        K1520SerialRxCb, ctypes.c_void_p]
_lib.k1520_serial_set_rx_cb.restype = None

# ── Maschinenneutrale Anzeigen (AP-E4b, doc/design/16_k8915.md §8a) ─────────
# k1520_machine_type(K1520Handle) -> int (K1520MachineType)
_lib.k1520_machine_type.argtypes = [K1520Handle]
_lib.k1520_machine_type.restype = ctypes.c_int

# k1520_screen_char(K1520Handle, col, row) -> uint8_t (Bildspeicher der Karte)
_lib.k1520_screen_char.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_int]
_lib.k1520_screen_char.restype = ctypes.c_uint8

# k1520_panel_lamps(K1520Handle) -> uint8_t (K8915: Latch 61H, aktiv low)
_lib.k1520_panel_lamps.argtypes = [K1520Handle]
_lib.k1520_panel_lamps.restype = ctypes.c_uint8

# k1520_bell_count(K1520Handle) -> uint32_t (fortlaufend)
_lib.k1520_bell_count.argtypes = [K1520Handle]
_lib.k1520_bell_count.restype = ctypes.c_uint32

# k1520_nmi(K1520Handle) -> void (K8915: NMI-Taster; A5120: ohne Wirkung)
_lib.k1520_nmi.argtypes = [K1520Handle]
_lib.k1520_nmi.restype = None

# PRG 710/710-1 (AP-P5b): Variante und Speicherverwaltung (Diagnose)
_lib.k1520_prg710_variant.argtypes = [K1520Handle]
_lib.k1520_prg710_variant.restype = ctypes.c_int
_lib.k1520_k8915_generation.argtypes = [K1520Handle]
_lib.k1520_k8915_generation.restype = ctypes.c_int
_lib.k1520_prg710_page.argtypes = [K1520Handle, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8)]
_lib.k1520_prg710_page.restype = ctypes.c_bool
_lib.k1520_prg710_freigabe.argtypes = [K1520Handle]
_lib.k1520_prg710_freigabe.restype = ctypes.c_int

# EPROMmer des PRG (ATP 590068, virtueller Sockel; doc/prg710/eprommer.md, AP-P7b)
_lib.k1520_eprom_insert.argtypes = [K1520Handle, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_eprom_insert.restype = ctypes.c_bool
_lib.k1520_eprom_insert_data.argtypes = [K1520Handle, ctypes.POINTER(ctypes.c_uint8),
                                         ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
                                         ctypes.c_bool]
_lib.k1520_eprom_insert_data.restype = ctypes.c_bool
_lib.k1520_eprom_insert_blank.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_eprom_insert_blank.restype = ctypes.c_bool
_lib.k1520_eprom_remove.argtypes = [K1520Handle]
_lib.k1520_eprom_remove.restype = ctypes.c_bool
_lib.k1520_eprom_save.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_eprom_save.restype = ctypes.c_bool
_lib.k1520_eprom_erase.argtypes = [K1520Handle]
_lib.k1520_eprom_erase.restype = ctypes.c_bool
_lib.k1520_eprom_type.argtypes = [K1520Handle]
_lib.k1520_eprom_type.restype = ctypes.c_int
_lib.k1520_eprom_selected_type.argtypes = [K1520Handle]
_lib.k1520_eprom_selected_type.restype = ctypes.c_int
_lib.k1520_eprom_control.argtypes = [K1520Handle]
_lib.k1520_eprom_control.restype = ctypes.c_int
_lib.k1520_eprom_read.argtypes = [K1520Handle, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
_lib.k1520_eprom_read.restype = ctypes.c_int
_lib.k1520_eprom_modified.argtypes = [K1520Handle]
_lib.k1520_eprom_modified.restype = ctypes.c_bool
_lib.k1520_eprom_path.argtypes = [K1520Handle]
_lib.k1520_eprom_path.restype = ctypes.c_char_p
_lib.k1520_eprom_log.argtypes = [K1520Handle, ctypes.c_bool]
_lib.k1520_eprom_log.restype = ctypes.c_char_p
_lib.k1520_eprom_error.argtypes = [K1520Handle]
_lib.k1520_eprom_error.restype = ctypes.c_char_p

# Lochband an der ADA K6022 (AP-P8b; steckbar in jeder Maschine seit Entwurf 23 AP-L1)
_lib.k1520_ptape_install.argtypes = [K1520Handle]
_lib.k1520_ptape_install.restype = ctypes.c_bool
_lib.k1520_ptape_installed.argtypes = [K1520Handle]
_lib.k1520_ptape_installed.restype = ctypes.c_bool
_lib.k1520_ptape_load.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_ptape_load.restype = ctypes.c_bool
_lib.k1520_ptape_eject.argtypes = [K1520Handle]
_lib.k1520_ptape_eject.restype = ctypes.c_bool
_lib.k1520_ptape_reader_status.argtypes = [
    K1520Handle, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
    ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_int)]
_lib.k1520_ptape_reader_status.restype = ctypes.c_bool
_lib.k1520_ptape_punch_length.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_length.restype = ctypes.c_int64
_lib.k1520_ptape_punch_save.argtypes = [K1520Handle, ctypes.c_char_p]
_lib.k1520_ptape_punch_save.restype = ctypes.c_bool
_lib.k1520_ptape_punch_clear.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_clear.restype = ctypes.c_bool
_lib.k1520_ptape_punch_enable.argtypes = [K1520Handle, ctypes.c_bool]
_lib.k1520_ptape_punch_enable.restype = ctypes.c_bool
_lib.k1520_ptape_punch_enabled.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_enabled.restype = ctypes.c_int
# Bandformate + Stanzer-Bindung (Entwurf 23 AP-L3)
_lib.k1520_ptape_error.argtypes = [K1520Handle]
_lib.k1520_ptape_error.restype = ctypes.c_char_p
_lib.k1520_ptape_detect_format.argtypes = [ctypes.c_char_p]
_lib.k1520_ptape_detect_format.restype = ctypes.c_int
_lib.k1520_ptape_format_from_ext.argtypes = [ctypes.c_char_p]
_lib.k1520_ptape_format_from_ext.restype = ctypes.c_int
_lib.k1520_ptape_load_fmt.argtypes = [K1520Handle, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_ptape_load_fmt.restype = ctypes.c_bool
_lib.k1520_ptape_reader_file.argtypes = [K1520Handle]
_lib.k1520_ptape_reader_file.restype = ctypes.c_char_p
_lib.k1520_ptape_reader_format.argtypes = [K1520Handle]
_lib.k1520_ptape_reader_format.restype = ctypes.c_int
_lib.k1520_ptape_punch_bind.argtypes = [K1520Handle, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_ptape_punch_bind.restype = ctypes.c_bool
_lib.k1520_ptape_punch_unbind.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_unbind.restype = ctypes.c_bool
_lib.k1520_ptape_punch_new_tape.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_new_tape.restype = ctypes.c_bool
_lib.k1520_ptape_punch_flush.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_flush.restype = ctypes.c_bool
_lib.k1520_ptape_punch_file.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_file.restype = ctypes.c_char_p
_lib.k1520_ptape_punch_format.argtypes = [K1520Handle]
_lib.k1520_ptape_punch_format.restype = ctypes.c_int

# Bandformate (K1520_PTAPE_FMT_* in core/api/k1520_api.h) — stabile Kennungen.
PTAPE_FMT_RAW, PTAPE_FMT_IHEX, PTAPE_FMT_ASCII = 0, 1, 2
PTAPE_FORMAT_NAMEN = {PTAPE_FMT_RAW: "Roh", PTAPE_FMT_IHEX: "Intel HEX",
                      PTAPE_FMT_ASCII: "ASCII-Art"}


def _utf8(path) -> bytes:
    """Pfad für die Lochstreifen-Funktionen (der Kern erwartet UTF-8, auch unter Windows)."""
    return os.fspath(path).encode("utf-8")

# Maschinentypen (K1520MachineType in core/api/k1520_api.h) — Name → Wert.
MACHINE_TYPES = {"a5120": 0, "prg710": 1, "prg710-1": 1, "k8915": 2, "k8915-g2": 2, "pc1715": 3,
                 "pc1715-k7221": 3, "pc1715w": 3, "p8000": 4}
# (Variante, Bildschirm) für k1520_create_pc1715: Variante 0 = PC 1715, 1 = PC 1715W (AP-W3);
# Bildschirm 0 = K7222 80×24, 1 = K7221 64×16 (am 1715W abgelehnt).
# AP-6: wählbare ROM-Fassungen → Parameter von k1520_create_pc1715_ex.
PC1715_ZEICHENSAETZE = {"deutsch": 0, "polnisch": 1, "kyrillisch": 2}
PC1715_TASTATUREN = {"s600": 0, "tast618": 1}
PC1715_MODELLE = {"pc1715": (0, 0), "pc1715-k7221": (0, 1), "pc1715w": (1, 0)}
# Variante für k1520_create_prg710 (nur die PRG-Namen).
PRG_VARIANTEN = {"prg710": 0, "prg710-1": 1}
# Bauform für k1520_create_k8915 (doc/design/24_k8915_varianten.md R5); "k8915" bleibt V3.
K8915_GENERATIONEN = {"k8915": 0, "k8915-g2": 1}

# Bildschirmtastatur: `QK_TASTE_BASE | Position` = physische Taste (K7609 am PRG 710,
# K7672 am PRG 710-1 und K8915).  ET1 des PRG 710 (K7609): Position 37H; ET2/ST: 38H.
# Am PRG 710-1 und K8915 gelten die Matrixpositionen der K7672 (Firmware-Tabelle).
QK_TASTE_BASE = 0x03000000
K7609_ET1, K7609_ET2 = 0x37, 0x38

# Textbildschirm des K7024: 80x24 Zeichen ab 0xF800 (Bit7 = Invers-Attribut).
VRAM_BASE, VRAM_COLS, VRAM_ROWS = 0xF800, 80, 24

# ── Tastendiagnose: was schickt die Oberfläche wirklich an die Maschine? ─────
# Mit `K1520_TASTEN_LOG=1` schreibt JEDER Tastendruck eine Zeile nach stderr —
# gleichgültig, ob er von der PC-Tastatur, der Bildschirmtastatur oder einem
# Skript kommt (alle drei Wege laufen durch `key_press`).  Sie beantwortet die
# drei Fragen, die man bei „die Taste tut etwas anderes als erwartet" hat:
# WELCHER Code geht hinein, welches BYTE macht der K7637 daraus (das ist, was
# das Betriebssystem sieht), und kommt der Druck EINMAL oder wiederholt an
# (fehlendes Loslassen ⇒ die Tastenwiederholung des K7637 läuft weiter).
_TASTEN_LOG = os.environ.get("K1520_TASTEN_LOG", "") not in ("", "0")


def _taste_klartext(keycode: int) -> str:
    """Lesbarer Name des Keycodes — Rohcode, ASCII oder Qt-Sondertaste."""
    if (keycode & ~0xFF) == 0x02000000:
        return f"Rohcode 0x{keycode & 0xFF:02X} (Taste der Nachbildung)"
    if 0x20 <= keycode <= 0x7E:
        return f"ASCII '{chr(keycode)}'"
    if keycode & 0x01000000:
        return f"Qt-Sondertaste 0x{keycode:08X}"
    return f"0x{keycode:02X}"


def _protokolliere_taste(was: str, keycode: int, shift: bool = False,
                         ctrl: bool = False):
    try:
        byte = K1520Emulator.translate_key(keycode, shift, ctrl)
    except Exception:                      # ältere Bibliothek ohne die Funktion
        byte = None
    ziel = "" if byte is None else f"  → K7637 sendet 0x{byte:02X}"
    print(f"[taste] {was:11s} {_taste_klartext(keycode)}"
          f"  shift={int(shift)} ctrl={int(ctrl)}{ziel}",
          file=sys.stderr, flush=True)


# ─── Serielle Schnittstellen nach außen (Entwurf 19 §8) ──────────────────────
# Zahlenwerte = k1520_api.h.  Die Strukturen müssen bytegleich zum Header sein;
# test_c_api.py/test_serial_api.py prüfen sizeof gegen die Bibliothek.
SER_TELNET, SER_RFC2217, SER_DATEI = 0, 1, 2          # K1520SerBetriebsart
SER_SERVER, SER_CLIENT = 0, 1                          # K1520SerRolle
SER_AUS, SER_VERBINDET, SER_LAUSCHT, SER_VERBUNDEN, SER_FEHLER = 0, 1, 2, 3, 4  # K1520SerZustand
HOST_UNGUELTIG, HOST_IPV4, HOST_IPV6, HOST_NAME = 0, 1, 2, 3                   # K1520HostArt


class K1520SerInfo(ctypes.Structure):
    _fields_ = [("groesse", ctypes.c_uint32),
                ("name", ctypes.c_char * 32),
                ("stecker", ctypes.c_char * 8),
                ("v24", ctypes.c_bool),
                ("taktquellen", ctypes.c_int),
                ("taktquelle_name", (ctypes.c_char * 32) * 4)]


class K1520SerKonfig(ctypes.Structure):
    _fields_ = [("groesse", ctypes.c_uint32),
                ("betriebsart", ctypes.c_int),
                ("rolle", ctypes.c_int),
                ("host", ctypes.c_char * 256),
                ("port", ctypes.c_uint16),
                ("loop", ctypes.c_bool),
                ("rtscts_bruecke", ctypes.c_bool),
                ("xonxoff", ctypes.c_bool),
                ("taktquelle", ctypes.c_int),
                ("datei", ctypes.c_char * 1024)]


class K1520SerStatus(ctypes.Structure):
    _fields_ = [("groesse", ctypes.c_uint32),
                ("zustand", ctypes.c_int),
                ("port_aktiv", ctypes.c_uint16),
                ("gegenstelle", ctypes.c_char * 96),
                ("meldung", ctypes.c_char * 160),
                ("baud_nenn", ctypes.c_uint32),
                ("daten", ctypes.c_uint8),
                ("paritaet", ctypes.c_uint8),
                ("stopp_halbe", ctypes.c_uint8),
                ("format_gueltig", ctypes.c_bool),
                ("baud_gegenseite", ctypes.c_uint32),
                ("baud_abweichend", ctypes.c_bool),
                ("rts", ctypes.c_bool), ("cts", ctypes.c_bool), ("dtr", ctypes.c_bool),
                ("dsr", ctypes.c_bool), ("dcd", ctypes.c_bool),
                ("bytes_gesendet", ctypes.c_uint64),
                ("bytes_empfangen", ctypes.c_uint64),
                ("puffer_senden", ctypes.c_uint32),
                ("puffer_empfangen", ctypes.c_uint32),
                ("port_vorschlag", ctypes.c_uint16),
                ("rolle", ctypes.c_int),
                ("betriebsart", ctypes.c_int),
                ("versuche", ctypes.c_uint32),
                ("daten_gegenseite", ctypes.c_uint8),
                ("paritaet_gegenseite", ctypes.c_uint8),
                ("stopp_halbe_gegenseite", ctypes.c_uint8),
                ("format_gegenseite_bekannt", ctypes.c_bool),
                ("format_abweichend", ctypes.c_bool),
                ("leitungen_gegenseite", ctypes.c_uint8),
                ("leitungen_gegenseite_bekannt", ctypes.c_uint8)]


# Bits von leitungen_gegenseite(_bekannt) und Paritaet der Gegenseite (AP-S11)
SER_L_RTS, SER_L_DTR, SER_L_CTS, SER_L_DSR, SER_L_DCD, SER_L_RI = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
SER_PAR_KEINE, SER_PAR_UNGERADE, SER_PAR_GERADE, SER_PAR_MARK, SER_PAR_SPACE = 0, 1, 2, 3, 4
_LEITUNG_NAMEN = (("RTS", SER_L_RTS), ("DTR", SER_L_DTR), ("CTS", SER_L_CTS),
                  ("DSR", SER_L_DSR), ("DCD", SER_L_DCD), ("RI", SER_L_RI))


_lib.k1520_serial_count.argtypes = [K1520Handle]
_lib.k1520_serial_count.restype = ctypes.c_int
_lib.k1520_serial_info.argtypes = [K1520Handle, ctypes.c_int, ctypes.POINTER(K1520SerInfo)]
_lib.k1520_serial_info.restype = ctypes.c_bool
_lib.k1520_serial_fixed_name.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
_lib.k1520_serial_fixed_name.restype = ctypes.c_bool
_lib.k1520_serial_get_config.argtypes = [K1520Handle, ctypes.c_int, ctypes.POINTER(K1520SerKonfig)]
_lib.k1520_serial_get_config.restype = ctypes.c_bool
_lib.k1520_serial_configure.argtypes = [K1520Handle, ctypes.c_int, ctypes.POINTER(K1520SerKonfig)]
_lib.k1520_serial_configure.restype = ctypes.c_bool
_lib.k1520_serial_start.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_serial_start.restype = ctypes.c_bool
_lib.k1520_serial_start_auto.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_serial_start_auto.restype = ctypes.c_bool
_lib.k1520_serial_stop.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_serial_stop.restype = None
_lib.k1520_serial_status.argtypes = [K1520Handle, ctypes.c_int, ctypes.POINTER(K1520SerStatus)]
_lib.k1520_serial_status.restype = ctypes.c_bool
_lib.k1520_serial_classify_host.argtypes = [ctypes.c_char_p]
_lib.k1520_serial_classify_host.restype = ctypes.c_int


def _kuerzen(text: str, n: int) -> bytes:
    """UTF-8, höchstens ``n`` Bytes, nie mitten in einem Zeichen geschnitten."""
    return text.encode("utf-8")[:n].decode("utf-8", "ignore").encode("utf-8")


def _txt(b: bytes) -> str:
    return b.decode("utf-8", "replace")


@dataclass
class SerialInfo:
    """Feste Angaben einer Schnittstelle (``K1520SerInfo``)."""
    name: str
    stecker: str
    v24: bool
    taktquellen: list          # Namen; leer = fester Takt


@dataclass
class SerialKonfig:
    """Einstellungen (``K1520SerKonfig``); Vorgaben wie im Kern."""
    betriebsart: int = SER_TELNET
    rolle: int = SER_SERVER
    host: str = "127.0.0.1"
    port: int = 5000
    loop: bool = False
    rtscts_bruecke: bool = False
    xonxoff: bool = False
    taktquelle: int = 0
    datei: str = ""


@dataclass
class SerialStatus:
    """Zustand (``K1520SerStatus``); ``zustand`` = ``SER_*``-Konstante."""
    zustand: int
    port_aktiv: int
    gegenstelle: str
    meldung: str
    baud_nenn: int
    daten: int
    paritaet: int
    stopp_halbe: int
    format_gueltig: bool
    baud_gegenseite: int
    baud_abweichend: bool
    rts: bool
    cts: bool
    dtr: bool
    dsr: bool
    dcd: bool
    bytes_gesendet: int
    bytes_empfangen: int
    puffer_senden: int
    puffer_empfangen: int
    port_vorschlag: int
    rolle: int
    betriebsart: int
    versuche: int
    # AP-S11 (RFC 2217; Bedeutung je Rolle: Header k1520_api.h, K1520SerStatus)
    daten_gegenseite: int = 0
    paritaet_gegenseite: int = 0
    stopp_halbe_gegenseite: int = 0
    format_gegenseite_bekannt: bool = False
    format_abweichend: bool = False
    leitungen_gegenseite: int = 0
    leitungen_gegenseite_bekannt: int = 0

    @property
    def format_gegenseite_text(self) -> Optional[str]:
        """Format der Gegenseite als ``8N1``/``7E1``/``8O2``/``8N1.5``; ``None`` = unbekannt."""
        if not self.format_gegenseite_bekannt:
            return None
        par = "NOEMS"[self.paritaet_gegenseite] if 0 <= self.paritaet_gegenseite <= 4 else "?"
        stopp = {2: "1", 3: "1.5", 4: "2"}.get(self.stopp_halbe_gegenseite, "?")
        return f"{self.daten_gegenseite}{par}{stopp}"

    def leitung_gegenseite(self, bit: int) -> Optional[bool]:
        """Zustand einer Leitung der Gegenseite (``SER_L_*``); ``None`` = unbekannt."""
        if not self.leitungen_gegenseite_bekannt & bit:
            return None
        return bool(self.leitungen_gegenseite & bit)

    @property
    def leitungen_gegenseite_text(self) -> Optional[str]:
        """Aktive bekannte Leitungen, z. B. ``"RTS DTR"``; ``""`` = bekannt, keine aktiv;
        ``None`` = nichts bekannt."""
        if not self.leitungen_gegenseite_bekannt:
            return None
        return " ".join(n for n, b in _LEITUNG_NAMEN
                        if self.leitungen_gegenseite_bekannt & self.leitungen_gegenseite & b)


def classify_host(host: str) -> int:
    """Host-Feld klassifizieren → ``HOST_*`` (ungültig/IPv4/IPv6/Name)."""
    return int(_lib.k1520_serial_classify_host(host.encode("utf-8")))


# ════════════════════════════════════════════════════════════════════════════
# K1520 Emulator Python Class
# ════════════════════════════════════════════════════════════════════════════

class K1520Emulator:
    """Python wrapper for K1520 A5120 emulator."""
    
    def __init__(self, drive_types: Optional[list] = None, machine: str = "a5120",
                 em: Optional[str] = None, raf: Optional[str] = None,
                 ptape: bool = False, zeichensatz: str = "deutsch",
                 tastatur: str = "s600", p8000: Optional[dict] = None):
        """Initialize emulator instance.

        Args:
            drive_types: optional list of up to 4 core DriveProfile names, one per
                K5122 slot (e.g. ``["K5601", "K5601", "K5601", "none"]``).  An entry
                that is ``None`` or ``""`` keeps the slot default; ``"none"``
                marks an empty slot ("kein Laufwerk").  ``None`` (the default) builds
                the standard machine (A5120: 4× K5601; K8915: K5601, K5601, none, none).
            machine: ``"a5120"`` (Vorgabe), ``"k8915"``, ``"k8915-g2"`` (K8915 Gen 2), ``"prg710"`` oder
                ``"prg710-1"``, ``"pc1715"``, ``"pc1715-k7221"`` (PC 1715 mit dem
                Bildschirm K7221, 64×16) oder ``"pc1715w"`` — siehe :data:`MACHINE_TYPES`.
            zeichensatz: nur PC 1715 — Bestückung der ZG-EPROMs, ``"deutsch"``
                (S619 + S602, Vorgabe), ``"polnisch"`` oder ``"kyrillisch"``; beim
                ``"pc1715w"`` ohne Wirkung (der Satz kommt von Diskette).
            tastatur: nur PC 1715/1715W — Tastatur-ROM ``"s600"`` (QWERTY, Vorgabe)
                oder ``"tast618"`` (QWERTZ).
            p8000: nur ``machine="p8000"`` — Konfiguration als Wörterbuch (Schlüssel wie im
                Konfigurationstext von ``k1520_create_p8000``, z. B. ``{"mon8": "3.1"}``);
                die Laufwerksnamen aus ``drive_types`` werden als ``lw0``..``lw3`` ergänzt.
            em: Erweiterungsmodul des A5120.16 — ``None``/``"none"`` = ohne EM,
                ``"em064"`` oder ``"em256"``.  Nur am A5120 (sonst ValueError).
            raf: RAM-Floppy — ``None``/``"none"`` = ohne, ``"raf128"``, ``"raf512"``
                oder ``"raf2m"``; an jeder Maschine.  Wird direkt nach dem Anlegen
                gesteckt (vor dem ersten Lauf); Fehler → ValueError.
            ptape: Lochstreifen-Karte K6022 (SIF1000, E0H–E7H) stecken; an jeder
                Maschine, wie ``raf`` direkt nach dem Anlegen.  Fehler → ValueError.
        """
        # Zuerst setzen: schlägt die Erzeugung fehl, läuft __del__ trotzdem und
        # darf nicht über ein fehlendes Attribut stolpern.
        self._handle = None
        self._drive_types = list(drive_types) if drive_types else None
        if machine not in MACHINE_TYPES:
            raise ValueError(f"unbekannte Maschine {machine!r} "
                             f"(bekannt: {', '.join(MACHINE_TYPES)})")
        self._machine = machine
        if zeichensatz not in PC1715_ZEICHENSAETZE:
            raise ValueError(f"unbekannter Zeichensatz {zeichensatz!r} "
                             f"(bekannt: {', '.join(PC1715_ZEICHENSAETZE)})")
        if tastatur not in PC1715_TASTATUREN:
            raise ValueError(f"unbekanntes Tastatur-ROM {tastatur!r} "
                             f"(bekannt: {', '.join(PC1715_TASTATUREN)})")
        self._zeichensatz = zeichensatz
        self._tastatur = tastatur
        self._em = em if em and em != "none" else None
        if self._em and machine != "a5120":
            raise ValueError(f"ein Erweiterungsmodul gibt es nur am A5120, nicht am {machine!r}")
        try:
            if machine in PRG_VARIANTEN:
                names = (self._drive_types or [])[:4]
                names = names + [None] * (4 - len(names))
                enc = lambda n: n.encode("utf-8") if n else None
                handle = _lib.k1520_create_prg710(
                    PRG_VARIANTEN[machine], enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]))
            elif machine == "k8915-g2":
                names = (self._drive_types or [])[:4]
                names = names + [None] * (4 - len(names))
                enc = lambda n: n.encode("utf-8") if n else None
                handle = _lib.k1520_create_k8915(
                    K8915_GENERATIONEN[machine], enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]))
            elif machine == "p8000":
                teile = [f"{k}={v}" for k, v in (p8000 or {}).items()]
                for i, n in enumerate((self._drive_types or [])[:4]):
                    if n:
                        teile.append(f"lw{i}={n}")
                handle = _lib.k1520_create_p8000(",".join(teile).encode("utf-8"))
            elif machine in PC1715_MODELLE:
                names = (self._drive_types or [])[:4]
                names = names + [None] * (4 - len(names))
                enc = lambda n: n.encode("utf-8") if n else None
                variante, bildschirm = PC1715_MODELLE[machine]
                handle = _lib.k1520_create_pc1715_ex(
                    variante, bildschirm, PC1715_ZEICHENSAETZE[zeichensatz], 0,
                    PC1715_TASTATUREN[tastatur],
                    enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]))
            else:
                handle = self._create_handle(self._drive_types, MACHINE_TYPES[machine], self._em)
        except Exception as e:
            raise RuntimeError(f"Failed to create K1520 emulator: {e}")
        if not handle:
            # Startabbruch im Core (z. B. fehlender/kaputter Diskettenformat-Katalog
            # data/formats.yaml).  Ohne Handle ist last_error() nicht erreichbar —
            # der Grund kommt deshalb aus k1520_last_init_error().
            reason = _lib.k1520_last_init_error()
            reason = reason.decode("utf-8", "replace") if reason else ""
            raise RuntimeError(reason or "k1520_create lieferte NULL (unbekannter Grund)")
        self._handle = handle
        if raf and raf != "none":
            if not _lib.k1520_raf_install(handle, raf.encode("ascii", "replace")):
                reason = _lib.k1520_last_init_error()
                reason = reason.decode("utf-8", "replace") if reason else ""
                _lib.k1520_destroy(handle)
                self._handle = None
                raise ValueError(reason or f"RAF {raf!r} nicht steckbar")
        if ptape and not _lib.k1520_ptape_install(handle):
            reason = _lib.k1520_last_init_error()
            reason = reason.decode("utf-8", "replace") if reason else ""
            _lib.k1520_destroy(handle)
            self._handle = None
            raise ValueError(reason or "K6022 nicht steckbar")
        self._running = False
        self._thread: Optional[threading.Thread] = None

    @staticmethod
    def _create_handle(drive_types: Optional[list], machine_type: int = 0,
                       em: Optional[str] = None):
        """Create a core handle, configured with per-slot drive profiles if given."""
        if not drive_types and not em:
            return _lib.k1520_create(machine_type)  # Vorgabebestückung der Maschine

        drive_types = list(drive_types or [])
        names = drive_types[:4] + [None] * (4 - len(drive_types[:4]))

        def enc(name):
            return name.encode("utf-8") if name else None  # None/"" → core keeps default

        if em:
            return _lib.k1520_create_with_em(
                machine_type, enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]), enc(em))
        return _lib.k1520_create_configured(
            machine_type, enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]))

    @property
    def machine(self) -> str:
        """Name der Maschine, mit der dieses Objekt erzeugt wurde (``"a5120"``/``"k8915"``)."""
        return self._machine

    @property
    def zeichensatz(self) -> str:
        """PC 1715: Bestückung der ZG-EPROMs (``"deutsch"``/``"polnisch"``/``"kyrillisch"``)."""
        return self._zeichensatz

    @property
    def tastatur(self) -> str:
        """PC 1715/1715W: Tastatur-ROM (``"s600"``/``"tast618"``)."""
        return self._tastatur

    def k8915_generation(self) -> Optional[int]:
        """K8915: 0 = V3, 1 = Gen 2 (aus dem Kern); andere Maschinen ``None``."""
        v = int(_lib.k1520_k8915_generation(self._handle))
        return v if v >= 0 else None

    @property
    def prg_variant(self) -> Optional[int]:
        """PRG: 0 = PRG 710, 1 = PRG 710-1 (aus dem Kern); andere Maschinen ``None``."""
        v = int(_lib.k1520_prg710_variant(self._handle))
        return v if v >= 0 else None

    def prg_page(self, n: int) -> Optional[tuple]:
        """PRG: Speicherverwaltung Seite ``n`` (0…15) als ``(E8H, EAH)``; sonst ``None``."""
        a, s = ctypes.c_uint8(), ctypes.c_uint8()
        if not _lib.k1520_prg710_page(self._handle, n, ctypes.byref(a), ctypes.byref(s)):
            return None
        return a.value, s.value

    def prg_freigabe(self) -> Optional[int]:
        """PRG: Freigaberegister EBH (0 = Abbildung aus); sonst ``None``."""
        v = int(_lib.k1520_prg710_freigabe(self._handle))
        return v if v >= 0 else None

    # ─── EPROMmer des PRG (virtueller Sockel, AP-P7b) ─────────────────────────
    #: Typnamen des Sockels (0 = leer); Schlüssel wie k1520_eprom_type.
    EPROM_TYPEN = {0: "", 1: "U555", 2: "U2716"}
    #: Größe je Typ in Byte.
    EPROM_GROESSE = {1: 1024, 2: 2048}

    def has_eprommer(self) -> bool:
        """True, wenn die Maschine einen EPROMmer hat (PRG 710 / 710-1)."""
        return int(_lib.k1520_eprom_type(self._handle)) >= 0

    def eprom_insert(self, path: str, typ: int = 0) -> None:
        """Rohes ``.bin`` einlegen (``typ`` 0 = aus der Größe, 1 = U555, 2 = U2716).

        Raises:
            OSError: mit dem Fehlertext des Kerns.
        """
        if not _lib.k1520_eprom_insert(self._handle, os.fsencode(path), int(typ)):
            raise OSError((_lib.k1520_eprom_error(self._handle) or b"").decode("utf-8", "replace"))

    def eprom_insert_data(self, daten: bytes, typ: int, path: str = "",
                          modified: bool = False) -> None:
        """PROM aus dem Speicher einlegen (Sockel auf eine neue Maschine tragen).

        Raises:
            OSError: mit dem Fehlertext des Kerns.
        """
        buf = (ctypes.c_uint8 * max(len(daten), 1)).from_buffer_copy(bytes(daten) or b"\0")
        if not _lib.k1520_eprom_insert_data(self._handle, buf, len(daten), int(typ),
                                            os.fsencode(path) if path else None,
                                            bool(modified)):
            raise OSError((_lib.k1520_eprom_error(self._handle) or b"").decode("utf-8", "replace"))

    def eprom_insert_blank(self, typ: int) -> bool:
        """Leeres (gelöschtes) PROM einlegen: 1 = U555, 2 = U2716."""
        return bool(_lib.k1520_eprom_insert_blank(self._handle, int(typ)))

    def eprom_remove(self) -> bool:
        return bool(_lib.k1520_eprom_remove(self._handle))

    def eprom_save(self, path: Optional[str] = None) -> None:
        """Inhalt als ``.bin`` schreiben (``None`` = an die gebundene Datei).

        Raises:
            OSError: mit dem Fehlertext des Kerns.
        """
        p = os.fsencode(path) if path else None
        if not _lib.k1520_eprom_save(self._handle, p):
            raise OSError((_lib.k1520_eprom_error(self._handle) or b"").decode("utf-8", "replace"))

    def eprom_erase(self) -> bool:
        """UV-Löschen: alles FFH."""
        return bool(_lib.k1520_eprom_erase(self._handle))

    def eprom_type(self) -> int:
        """Gesteckter Typ: 0 = leer, 1 = U555, 2 = U2716, -1 = kein EPROMmer."""
        return int(_lib.k1520_eprom_type(self._handle))

    def eprom_selected_type(self) -> int:
        """Eingestellter Typ (ZRE-PIO 84H Bit 0): 1 = U555, 2 = U2716, -1."""
        return int(_lib.k1520_eprom_selected_type(self._handle))

    def eprom_control(self) -> int:
        """Steuerregister D4H (Bit 0+1 Programmierspannung, 2 Impuls, 3/4 Versorgung)."""
        return int(_lib.k1520_eprom_control(self._handle))

    def eprom_read(self) -> bytes:
        """Inhalt des gesteckten PROM (leer: ``b""``)."""
        buf = (ctypes.c_uint8 * 2048)()
        n = int(_lib.k1520_eprom_read(self._handle, buf, 2048))
        return bytes(buf[:max(n, 0)])

    def eprom_modified(self) -> bool:
        return bool(_lib.k1520_eprom_modified(self._handle))

    def eprom_path(self) -> str:
        return (_lib.k1520_eprom_path(self._handle) or b"").decode("utf-8", "replace")

    def eprom_log(self, only_new: bool = False) -> list:
        """Protokollzeilen; ``only_new`` = nur seit dem letzten solchen Aufruf."""
        s = (_lib.k1520_eprom_log(self._handle, bool(only_new)) or b"").decode("utf-8", "replace")
        return [z for z in s.split("\n") if z]

    # ── Lochband an der ADA K6022 (AP-P8b; steckbar seit Entwurf 23 AP-L1) ───

    def ptape_installed(self) -> bool:
        """True, wenn die K6022 gesteckt ist (``K1520Emulator(ptape=True)``)."""
        return bool(_lib.k1520_ptape_installed(self._handle))

    def ptape_error(self) -> str:
        """Grund des letzten gescheiterten Lochstreifen-Aufrufs (install/load/bind/…);
        "" nach einem Erfolg."""
        return (_lib.k1520_ptape_error(self._handle) or b"").decode("utf-8", "replace")

    @staticmethod
    def ptape_detect_format(path) -> Optional[int]:
        """Format (``PTAPE_FMT_*``) aus dem Dateiinhalt erraten; ``None`` = unlesbar."""
        v = int(_lib.k1520_ptape_detect_format(_utf8(path)))
        return v if v >= 0 else None

    @staticmethod
    def ptape_format_from_ext(path) -> int:
        """Format (``PTAPE_FMT_*``) nach der Dateiendung; sonst ``PTAPE_FMT_RAW``."""
        return int(_lib.k1520_ptape_format_from_ext(_utf8(path)))

    def ptape_load(self, path, fmt: int = PTAPE_FMT_RAW) -> bool:
        """Band aus einer Datei im Format ``fmt`` in den Leser legen; False (Grund in
        :meth:`ptape_error`) bei Lesefehler, unpassendem Inhalt oder ohne Karte."""
        return bool(_lib.k1520_ptape_load_fmt(self._handle, _utf8(path), int(fmt)))

    def ptape_eject(self) -> bool:
        """Band aus dem Leser nehmen."""
        return bool(_lib.k1520_ptape_eject(self._handle))

    def ptape_reader_status(self) -> Optional[dict]:
        """Leser: ``{"inserted", "pos", "len", "at_end", "file", "format"}``; ohne Karte
        ``None``.  ``file`` = "" ohne Band."""
        ins, end = ctypes.c_int(), ctypes.c_int()
        pos, ln = ctypes.c_uint64(), ctypes.c_uint64()
        if not _lib.k1520_ptape_reader_status(self._handle, ctypes.byref(ins), ctypes.byref(pos),
                                              ctypes.byref(ln), ctypes.byref(end)):
            return None
        datei = (_lib.k1520_ptape_reader_file(self._handle) or b"").decode("utf-8", "replace")
        return {"inserted": bool(ins.value), "pos": int(pos.value), "len": int(ln.value),
                "at_end": bool(end.value), "file": datei,
                "format": int(_lib.k1520_ptape_reader_format(self._handle))}

    def ptape_punch_length(self) -> Optional[int]:
        """Länge des Stanzbandes; ohne Karte ``None``."""
        n = int(_lib.k1520_ptape_punch_length(self._handle))
        return n if n >= 0 else None

    def ptape_punch_status(self) -> Optional[dict]:
        """Stanzer: ``{"file", "format", "len", "enabled"}``; ohne Karte ``None``.
        ``file`` = "" ohne Bindung."""
        n = int(_lib.k1520_ptape_punch_length(self._handle))
        if n < 0:
            return None
        datei = (_lib.k1520_ptape_punch_file(self._handle) or b"").decode("utf-8", "replace")
        return {"file": datei, "format": int(_lib.k1520_ptape_punch_format(self._handle)),
                "len": n, "enabled": bool(_lib.k1520_ptape_punch_enabled(self._handle) > 0)}

    def ptape_punch_bind(self, path, fmt: int = PTAPE_FMT_RAW) -> bool:
        """Stanzer an eine Datei binden (laufend mitgeschrieben); eine vorhandene Datei ist
        der Anfang des Bandes.  False → :meth:`ptape_error`."""
        return bool(_lib.k1520_ptape_punch_bind(self._handle, _utf8(path), int(fmt)))

    def ptape_punch_unbind(self) -> bool:
        """Bindung lösen (zuvor zurückschreiben; das Band bleibt im Speicher)."""
        return bool(_lib.k1520_ptape_punch_unbind(self._handle))

    def ptape_punch_new_tape(self) -> bool:
        """„Neues Band": Stanzband leeren, gebundene Datei sofort leer schreiben."""
        return bool(_lib.k1520_ptape_punch_new_tape(self._handle))

    def ptape_punch_flush(self) -> bool:
        """Gebundene Datei sofort zurückschreiben (sonst nach der Stanzpause)."""
        return bool(_lib.k1520_ptape_punch_flush(self._handle))

    def ptape_punch_save(self, path) -> bool:
        """Stanzband (Roh) in eine Datei schreiben (überschreibt, ohne zu binden)."""
        return bool(_lib.k1520_ptape_punch_save(self._handle, _utf8(path)))

    def ptape_punch_clear(self) -> bool:
        """Wie :meth:`ptape_punch_new_tape` (alter Name)."""
        return bool(_lib.k1520_ptape_punch_clear(self._handle))

    def ptape_punch_enable(self, on: bool) -> bool:
        """Stanzer ein/aus (aus: der Treiber meldet nach seiner Frist C2)."""
        return bool(_lib.k1520_ptape_punch_enable(self._handle, bool(on)))

    def ptape_punch_enabled(self) -> Optional[bool]:
        """Stanzer ein?; ohne Karte ``None``."""
        v = int(_lib.k1520_ptape_punch_enabled(self._handle))
        return None if v < 0 else bool(v)

    def machine_type(self) -> int:
        """K1520MachineType, wie der Kern ihn meldet (0 = A5120, 1 = PRG, 2 = K8915, 3 = PC 1715, 4 = P8000)."""
        return int(_lib.k1520_machine_type(self._handle))

    # ─── Terminals des P8000 (k1520_term_*) ──────────────────────────────────

    def term_count(self) -> int:
        """Zahl der Kern-Terminals (P8000: 1 = tty1; sonst 0)."""
        return int(_lib.k1520_term_count(self._handle))

    def term_tty(self, i: int = 0) -> int:
        """Kanalnummer (ttyN) des Terminals ``i``; -1 bei ungültigem Index."""
        return int(_lib.k1520_term_tty(self._handle, i))

    def term_char(self, i: int, col: int, row: int) -> str:
        """Zeichen einer Terminalzelle (80 × 24); ``""`` außerhalb."""
        c = int(_lib.k1520_term_char(self._handle, i, col, row))
        return chr(c) if c else ""

    def term_attr(self, i: int, col: int, row: int) -> int:
        """Wirksames Attribut der Zelle (Bit 0 blink, 1 invers, 2 leer, 3 hell, 4 unterstrichen)."""
        return int(_lib.k1520_term_attr(self._handle, i, col, row))

    def term_text(self, i: int = 0) -> str:
        """Terminalbild als 24 Zeilen à 80 Zeichen; ``""`` bei ungültigem Index."""
        if self.term_count() <= i:
            return ""
        buf = ctypes.create_string_buffer(96)
        zeilen = []
        for r in range(24):
            n = _lib.k1520_term_text(self._handle, i, r, buf, len(buf))
            zeilen.append(buf.raw[:n].decode("latin-1"))
        return "\n".join(zeilen)

    def term_cursor(self, i: int = 0) -> Optional[tuple]:
        """Cursor ``(Spalte, Zeile)`` oder ``None`` bei ungültigem Index."""
        c, r = ctypes.c_int(), ctypes.c_int()
        if not _lib.k1520_term_cursor(self._handle, i, ctypes.byref(c), ctypes.byref(r)):
            return None
        return c.value, r.value

    def term_mode(self, i: int = 0) -> int:
        """0 = ADM31, 1 = VT100, -1 bei ungültigem Index."""
        return int(_lib.k1520_term_mode(self._handle, i))

    def term_key(self, i: int, keycode: int, shift: bool = False, ctrl: bool = False) -> bool:
        """Taste am Terminal ``i`` (Qt-Code oder ASCII)."""
        return bool(_lib.k1520_term_key(self._handle, i, keycode, shift, ctrl))

    def term_send(self, i: int, text: str) -> bool:
        """Text wie getippt senden (``\\r``/``\\n`` = Return)."""
        data = text.encode("latin-1", "replace")
        return bool(_lib.k1520_term_send(self._handle, i, data, len(data)))

    def panel_lamps(self) -> int:
        """Anzeigefeld: Rohbyte des K8915-Latches 61H, **aktiv low** (FFH = alles
        dunkel; Bit4 Lesen, Bit5 Schreiben, Bit6 bereit, Bit7 Fehler).  A5120: 0."""
        return int(_lib.k1520_panel_lamps(self._handle))

    def nmi(self):
        """NMI-Taster der Frontplatte (K8915): eine /NMI-Flanke, zugestellt beim
        nächsten :meth:`run`.  Kein /RESET.  Am A5120 ohne Wirkung."""
        _lib.k1520_nmi(self._handle)

    def bell_count(self) -> int:
        """Fortlaufender Zähler der Summertöne — die Oberfläche piept bei Zuwachs."""
        return int(_lib.k1520_bell_count(self._handle))

    @property
    def drive_types(self) -> Optional[list]:
        """The per-slot DriveProfile names this machine was created with (or None)."""
        return list(self._drive_types) if self._drive_types else None
    
    def __del__(self):
        """Cleanup on deletion."""
        if self._handle:
            self.stop()
            _lib.k1520_destroy(self._handle)
            self._handle = None
    
    def power_on(self):
        """Power on the emulator."""
        _lib.k1520_power_on(self._handle)
    
    def reset(self):
        """Reset the emulator."""
        _lib.k1520_reset(self._handle)
    
    def run(self, max_cycles: int) -> int:
        """
        Run emulator for max_cycles CPU cycles.
        
        Args:
            max_cycles: Maximum cycles to execute
            
        Returns:
            Actual cycles executed
        """
        return _lib.k1520_run(self._handle, max_cycles)
    
    def run_async(self, cycles_per_frame: int = 10000, fps: int = 50):
        """
        Run emulator in background thread.
        
        Args:
            cycles_per_frame: Cycles to execute per frame update
            fps: Target frames per second
        """
        if self._running:
            return
        
        self._running = True
        frame_time = 1.0 / fps
        
        def run_loop():
            while self._running:
                start = time.time()
                self.run(cycles_per_frame)
                elapsed = time.time() - start
                if elapsed < frame_time:
                    time.sleep(frame_time - elapsed)
        
        self._thread = threading.Thread(target=run_loop, daemon=True)
        self._thread.start()
    
    def stop(self):
        """Stop async execution."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)
            self._thread = None
    
    def framebuffer_size(self) -> tuple:
        """Bildgröße ``(Breite, Höhe)`` in Pixeln, wie der Kern sie meldet."""
        return (int(_lib.k1520_fb_width(self._handle)),
                int(_lib.k1520_fb_height(self._handle)))

    def get_framebuffer(self) -> bytearray:
        """
        Get current framebuffer content.
        
        Returns:
            bytearray of pixels
        """
        width = _lib.k1520_fb_width(self._handle)
        height = _lib.k1520_fb_height(self._handle)
        
        fb_ptr = _lib.k1520_framebuffer(self._handle)
        if not fb_ptr:
            return bytearray(width * height if width and height else 1920)
        
        # Copy framebuffer
        size = width * height if (width and height) else 1920
        return bytearray(ctypes.string_at(fb_ptr, size))
    
    def is_framebuffer_dirty(self) -> bool:
        """Check if framebuffer was updated since last clear."""
        return _lib.k1520_fb_dirty(self._handle)
    
    def clear_framebuffer_dirty_flag(self):
        """Clear the framebuffer dirty flag."""
        _lib.k1520_fb_clear_dirty(self._handle)
    
    def key_press(self, keycode: int, shift: bool = False, ctrl: bool = False):
        """
        Queue a key press event.
        
        Args:
            keycode: Z80 keyboard scan code
            shift: Shift key state
            ctrl: Control key state
        """
        if _TASTEN_LOG:
            _protokolliere_taste("gedrueckt", keycode, shift, ctrl)
        _lib.k1520_key_press(self._handle, ctypes.c_uint32(keycode), ctypes.c_bool(shift), ctypes.c_bool(ctrl))
    
    def key_release(self, keycode: int):
        """
        Queue a key release event.
        
        Args:
            keycode: Z80 keyboard scan code
        """
        if _TASTEN_LOG:
            _protokolliere_taste("losgelassen", keycode)
        _lib.k1520_key_release(self._handle, ctypes.c_uint32(keycode))
    
    def set_key_repeat_realtime(self, realtime: bool = True):
        """Tastenwiederholung in Echtzeit statt in Maschinentakten zählen.

        Die Tastatur hat ihren eigenen Quarz: bei 10 × Rechnertakt soll eine
        gehaltene Taste nicht zehnmal so früh und schnell wiederholen.  Für die
        Oberfläche; Vorgabe des Kerns ist Maschinenzeit (wiederholbare Tests).
        """
        _lib.k1520_set_key_repeat_realtime(self._handle, ctypes.c_bool(realtime))

    @staticmethod
    def translate_key(keycode: int, shift: bool = False, ctrl: bool = False) -> int:
        """Physischer K7637-Code zu einem Tastencode — ohne Maschine.

        Beantwortet „welche Taste der echten Tastatur spricht dieser Anschlag
        an?" und ist damit das Werkzeug, wenn die Oberfläche etwas anderes zu
        tun scheint als erwartet.
        """
        return int(_lib.k1520_translate_key(ctypes.c_uint32(keycode),
                                            ctypes.c_bool(shift),
                                            ctypes.c_bool(ctrl)))

    def keyboard_leds(self) -> int:
        """Zustand der Tastaturanzeigen, Bitbelegung je Tastatur.

        A5120 (K7637): Bit 0…4 = Funktionsanzeigen G00…G04, Bit 5 = Fehleranzeige
        (blinkt, solange gesetzt), Bit 7 = akustisches Signal läuft.
        K8915 (K7672): Register 21H — Bit 3 = Senden frei (XON), Bit 0 = ``ESC [?13h``.
        """
        return int(_lib.k1520_keyboard_leds(self._handle))

    def mount_disk(self, drive: int, path: str, format_name: str, write_protect: bool = False) -> bool:
        """
        Mount a disk image.
        
        Args:
            drive: Drive number (0-3)
            path: Path to disk image file
            format_name: Disk format name; must fit the slot's drive type — see
                :meth:`drive_formats`. For .hfe the geometry comes from the file.
            write_protect: Whether disk is write-protected
            
        Returns:
            True if successful
        """
        if not os.path.exists(path):
            raise FileNotFoundError(f"Disk image not found: {path}")

        # The core is the authority on valid format names (see drive_formats());
        # it reports an error via last_error() if the format does not fit.
        path_bytes = path.encode('utf-8')
        format_bytes = format_name.encode('utf-8')
        return _lib.k1520_mount_disk(self._handle, ctypes.c_int(drive), path_bytes, format_bytes, ctypes.c_bool(write_protect))

    def mount_physical(self, drive: int, sync, write_protect: bool = True) -> bool:
        """Mount a PHYSICAL disk from a real drive on a Greaseweazle adapter.

        *sync* is an :class:`app.gw.Sync` (or its raw handle) served by a running
        worker thread.  Nothing is read at mount time — tracks are fetched one by
        one as the guest touches them, so a boot starts within a second instead of
        after a full disk image dump.

        The read path blocks the machine thread for roughly half a second per
        track, exactly as a real drive would; the GUI thread stays responsive.

        A sync handle can only be mounted **once**.  Write protection is the
        default here: a mistake costs the only remaining copy of a real diskette.

        See doc/design/14_physische_diskette.md.
        """
        return _lib.k1520_mount_physical(self._handle, ctypes.c_int(drive),
                                         getattr(sync, "handle", sync),
                                         ctypes.c_bool(write_protect))
    
    def create_disk(self, drive: int, path: str, format_name: str = "",
                    write_protect: bool = False) -> bool:
        """
        Create a NEW disk and mount it.

        With an EMPTY *format_name* this creates a genuinely blank (unformatted)
        disk in the geometry of the **drive** (K5601 80x2, K5600.10 40x1, ...),
        ready to be formatted by the guest OS — including foreign systems such as
        UDOS that append data behind the data CRC.  A ``.img`` target is rejected
        in that case (a raw sector image cannot express "unformatted"); use
        ``.hfe`` or ``.dmk``.

        With a *format_name* set, a pre-formatted disk per catalog format is
        created (real IDAM/DATA/CRC, 0xE5 data); ``.img`` is allowed then.

        Args:
            drive: Drive number (0-3)
            path: Path of the new disk image file (overwrites if it exists)
            format_name: Disk format name (see :meth:`drive_formats`); empty =
                         blank, unformatted disk
            write_protect: Whether disk is write-protected

        Returns:
            True if successful (see :meth:`last_error` otherwise)
        """
        format_name = format_name or ""
        path_bytes = path.encode('utf-8')
        format_bytes = format_name.encode('utf-8')
        return _lib.k1520_create_disk(self._handle, ctypes.c_int(drive), path_bytes, format_bytes, ctypes.c_bool(write_protect))

    def save_disk_as(self, drive: int, path: str, format_name: str = "") -> bool:
        """
        Save the mounted disk under a new name/container and re-bind to it.

        The container follows the extension (``.img`` / ``.hfe`` / ``.dmk``).  From
        then on all further writes go (delayed) into the new file.  *format_name*
        is only needed for ``.img`` — the other containers are self-describing.

        Returns:
            True if successful (see :meth:`last_error` otherwise)
        """
        path_bytes = path.encode('utf-8')
        format_bytes = (format_name or "").encode('utf-8')
        return _lib.k1520_save_disk_as(self._handle, ctypes.c_int(drive), path_bytes, format_bytes)

    def disk_raw_compatible(self, drive: int) -> bool:
        """True if the mounted disk may be saved as a raw sector image (.img).

        False as soon as a track is unformatted or a sector carries data behind
        the data CRC (UDOS sector control block) — both would be lost in a .img.
        """
        return bool(_lib.k1520_disk_raw_compatible(self._handle, ctypes.c_int(drive)))

    def disk_path(self, drive: int) -> str:
        """Currently bound image file of a slot ("" = memory only / empty drive)."""
        p = _lib.k1520_disk_path(self._handle, ctypes.c_int(drive))
        return p.decode('utf-8', 'replace') if p else ""

    def disk_container(self, drive: int) -> str:
        """Container of the bound file ("img" | "hfe" | "dmk"; "" = none)."""
        c = _lib.k1520_disk_container(self._handle, ctypes.c_int(drive))
        return c.decode('utf-8', 'replace') if c else ""

    def detected_format(self, drive: int) -> str:
        """Auf der eingelegten Diskette ERKANNTES Katalogformat ("" = unbekannt).

        Dieselbe Geometrie-Erkennung wie im k1520DiskTool.  Leer heisst *unbekannt*
        und fasst die drei Fälle zusammen, die die Oberfläche gleich behandeln muss:
        nichts eingelegt, kein Katalogformat passt, oder zwei passen gleich gut.
        Eine Diskette mit unbekanntem Format lässt sich nicht als ``.img``
        ausgeben — die Sektorreihenfolge wäre geraten.
        """
        f = _lib.k1520_disk_detected_format(self._handle, ctypes.c_int(drive))
        return f.decode('utf-8', 'replace') if f else ""

    def disk_notice(self, drive: int) -> str:
        """Wie die eingelegte Diskette ans Laufwerk angepasst wurde ("" = passt).

        Je Einschränkung eine Zeile: "Double Step aktiviert" (40-Spur-Diskette im
        80-Spur-Laufwerk), "Laufwerk liest nur jede zweite Spur" (umgekehrt),
        "Nur Seite 0 verwendbar" (zweiseitige Diskette, einseitiges Laufwerk).
        Kein Fehler — die Diskette ist gemountet und lesbar.
        """
        n = _lib.k1520_disk_notice(self._handle, ctypes.c_int(drive))
        return n.decode('utf-8', 'replace') if n else ""

    def flush_disks(self) -> bool:
        """Write pending changes of all drives to their files immediately."""
        return bool(_lib.k1520_flush_disks(self._handle))

    def last_error(self) -> str:
        """Return the last error message reported by the core (empty if none)."""
        err = _lib.k1520_last_error(self._handle)
        return err.decode('utf-8', 'replace') if err else ""

    def unmount_disk(self, drive: int) -> bool:
        """
        Unmount a disk.
        
        Args:
            drive: Drive number (0-3)
            
        Returns:
            True if successful
        """
        return _lib.k1520_unmount_disk(self._handle, ctypes.c_int(drive))
    
    def drive_formats(self, drive: int) -> list:
        """Built-in disk formats that fit the drive in *drive* (default first).

        Returns an empty list for an empty slot.  These are the names accepted by
        :meth:`mount_disk`/:meth:`create_disk` for that slot.
        """
        n = _lib.k1520_drive_format_count(self._handle, ctypes.c_int(drive))
        out = []
        for i in range(n):
            name = _lib.k1520_drive_format_name(self._handle, ctypes.c_int(drive), ctypes.c_int(i))
            if name:
                out.append(name.decode("utf-8"))
        return out

    def drive_default_format(self, drive: int) -> str:
        """Drive-type default format name (what an empty-format create uses)."""
        name = _lib.k1520_drive_default_format(self._handle, ctypes.c_int(drive))
        return name.decode("utf-8") if name else ""

    def format_description(self, name: str) -> str:
        """Human-readable description of a catalog format ("" if unknown)."""
        d = _lib.k1520_format_description(self._handle, name.encode("utf-8"))
        return d.decode("utf-8") if d else ""

    def formats_source(self) -> str:
        """Path(s) of the loaded formats.yaml — diagnostics."""
        s = _lib.k1520_formats_source(self._handle)
        return s.decode("utf-8") if s else ""

    def is_disk_active(self, drive: int) -> bool:
        """Check if disk is currently mounted."""
        return _lib.k1520_disk_active(self._handle, ctypes.c_int(drive))
    
    def is_disk_write_protected(self, drive: int) -> bool:
        """Check if disk is write-protected."""
        return _lib.k1520_disk_write_protected(self._handle, ctypes.c_int(drive))
    
    def set_disk_write_protect(self, drive: int, write_protect: bool):
        """Set write-protect status of a disk."""
        _lib.k1520_set_write_protect(self._handle, ctypes.c_int(drive), ctypes.c_bool(write_protect))

    def is_disk_led_on(self, drive: int) -> bool:
        """Return True while the drive LED should be lit (drive selected or motor on)."""
        return _lib.k1520_disk_led(self._handle, ctypes.c_int(drive))

    def is_motor_on(self, drive: int) -> bool:
        """Return True while the drive's spindle motor is running (/LCK, port 0x18)."""
        return _lib.k1520_disk_motor(self._handle, ctypes.c_int(drive))

    def is_head_loaded(self) -> bool:
        """Return True while the read/write head is loaded (/HL, ctrl port A bit6)."""
        return _lib.k1520_head_loaded(self._handle)

    # ─── Speicher-/Portzugriff (Diagnose, Tests) ─────────────────────────────

    # ─── RAM-Floppy RAF ────────────────────────────────────────────────────
    @property
    def raf_variant(self) -> str:
        """Bestückung: "" (keine RAF), "raf128", "raf512" oder "raf2m"."""
        v = _lib.k1520_raf_variant(self._handle)
        return v.decode("ascii") if v else ""

    def raf_peek(self, adr: int) -> int:
        """Byte des RAF-Inhalts an linearer Adresse (0xFF ohne RAF/außerhalb)."""
        return int(_lib.k1520_raf_peek(self._handle, adr))

    def raf_load(self, pfad) -> bool:
        """Inhalt aus Rohdatei laden; False ohne RAF oder bei falscher Größe."""
        return bool(_lib.k1520_raf_load(self._handle, os.fsencode(pfad)))

    def raf_save(self, pfad) -> bool:
        """Inhalt als Rohdatei sichern; False ohne RAF."""
        return bool(_lib.k1520_raf_save(self._handle, os.fsencode(pfad)))

    # ─── A5120.16 ──────────────────────────────────────────────────────────
    def em_variant(self) -> str:
        """Bestückung des Erweiterungsmoduls: "" (keins), "em064" oder "em256"."""
        v = _lib.k1520_em_variant(self._handle)
        return v.decode("ascii") if v else ""

    def em_leds(self) -> tuple:
        """(V1, V2) der Steuerkarte: V1 = RAMEN, V2 = 8-Bit-Mode."""
        return (bool(_lib.k1520_em_led_v1(self._handle)),
                bool(_lib.k1520_em_led_v2(self._handle)))

    def em_mode16(self) -> bool:
        """True im 16-Bit-Mode (der U8001 hat den Bus)."""
        return bool(_lib.k1520_em_mode16(self._handle))

    def em_state(self) -> Optional[K1520EmState]:
        """Register des U8001 und Zustand der Steuerkarte; None ohne EM."""
        st = K1520EmState()
        if not _lib.k1520_em_state(self._handle, ctypes.byref(st)):
            return None
        return st

    def mem_read(self, addr: int) -> int:
        """Read one byte through the bus (memory map of the running machine)."""
        return _lib.k1520_mem_read(self._handle, ctypes.c_uint16(addr))

    def mem_write(self, addr: int, data: int):
        """Write one byte through the bus."""
        _lib.k1520_mem_write(self._handle, ctypes.c_uint16(addr), ctypes.c_uint8(data))

    def io_read(self, port: int) -> int:
        """Read one I/O port (non-destructive where the hardware allows it)."""
        return _lib.k1520_io_read(self._handle, ctypes.c_uint8(port))

    def screen_text(self) -> str:
        """Textbildschirm als 24 Zeilen à 80 Zeichen (Attributbit 7 maskiert).

        Liest das K7024-Bildwiederholram direkt — unabhängig vom gerenderten
        Framebuffer und damit die robuste Art, den Bildschirminhalt zu prüfen.
        Beim K8915 über ``k1520_screen_char`` von der Karte: die CPU-Sicht
        (``mem_read``) zeigt bei 1000H je nach Port A8H das RAM der ZRE.
        """
        if self._machine != "a5120":
            return "\n".join(
                "".join(chr(_lib.k1520_screen_char(self._handle, c, r) & 0x7F)
                        for c in range(VRAM_COLS))
                for r in range(VRAM_ROWS))
        chars = [chr(self.mem_read(VRAM_BASE + i) & 0x7F)
                 for i in range(VRAM_COLS * VRAM_ROWS)]
        return "\n".join("".join(chars[r * VRAM_COLS:(r + 1) * VRAM_COLS])
                          for r in range(VRAM_ROWS))

    # ─── Serielle Schnittstellen nach außen (Entwurf 19 §8) ──────────────────

    def serial_count(self) -> int:
        """Zahl der einstellbaren Schnittstellen (A5120, K8915 und PRG 710: 3; PRG 710-1: 2)."""
        return int(_lib.k1520_serial_count(self._handle))

    def serial_info(self, i: int) -> Optional[SerialInfo]:
        """Feste Angaben der Schnittstelle ``i``; ``None`` bei ungültigem Index."""
        r = K1520SerInfo()
        r.groesse = ctypes.sizeof(r)
        if not _lib.k1520_serial_info(self._handle, i, ctypes.byref(r)):
            return None
        return SerialInfo(_txt(r.name), _txt(r.stecker), bool(r.v24),
                          [_txt(r.taktquelle_name[q].value) for q in range(r.taktquellen)])

    def serial_fixed_names(self) -> list:
        """Anzeigenamen der festen, nicht einstellbaren Schnittstellen (Tastatur)."""
        namen, buf = [], ctypes.create_string_buffer(128)
        while _lib.k1520_serial_fixed_name(self._handle, len(namen), buf, len(buf)):
            namen.append(_txt(buf.value))
        return namen

    def serial_config(self, i: int) -> Optional[SerialKonfig]:
        """Zuletzt übernommene Einstellungen; ``None`` bei ungültigem Index."""
        r = K1520SerKonfig()
        r.groesse = ctypes.sizeof(r)
        if not _lib.k1520_serial_get_config(self._handle, i, ctypes.byref(r)):
            return None
        return SerialKonfig(r.betriebsart, r.rolle, _txt(r.host), r.port, bool(r.loop),
                            bool(r.rtscts_bruecke), bool(r.xonxoff), r.taktquelle,
                            _txt(r.datei))

    def serial_configure(self, i: int, **felder) -> bool:
        """Einstellungen übernehmen: die aktuellen, überschrieben um ``felder`` (Namen wie
        :class:`SerialKonfig`).  ``False`` (nichts übernommen) bei Port 0, ungültigem Wert
        oder geändertem gesperrtem Feld im aktiven Betrieb; ``KeyError`` bei unbekanntem Feld."""
        k = self.serial_config(i)
        if k is None:
            return False
        for name, wert in felder.items():
            if not hasattr(k, name):
                raise KeyError(name)
            setattr(k, name, wert)
        r = K1520SerKonfig()
        r.groesse = ctypes.sizeof(r)
        r.betriebsart, r.rolle = int(k.betriebsart), int(k.rolle)
        r.host = _kuerzen(k.host, 255)
        r.port = k.port
        r.loop, r.rtscts_bruecke, r.xonxoff = bool(k.loop), bool(k.rtscts_bruecke), bool(k.xonxoff)
        r.taktquelle = int(k.taktquelle)
        r.datei = _kuerzen(k.datei, 1023)
        return bool(_lib.k1520_serial_configure(self._handle, i, ctypes.byref(r)))

    def serial_start(self, i: int) -> bool:
        """Start von Hand (Server mit Portsuche, Client Dauerversuch, Datei überschreibend)."""
        return bool(_lib.k1520_serial_start(self._handle, i))

    def serial_start_auto(self, i: int) -> bool:
        """Wiederaufnahme beim Programmstart (Server nur auf dem eingestellten Port;
        belegt → ``False`` und ``port_vorschlag`` im Status)."""
        return bool(_lib.k1520_serial_start_auto(self._handle, i))

    def serial_stop(self, i: int):
        """Beenden/Trennen, sofort."""
        _lib.k1520_serial_stop(self._handle, i)

    def serial_status(self, i: int) -> Optional[SerialStatus]:
        """Zustand der Schnittstelle ``i``; ``None`` bei ungültigem Index."""
        r = K1520SerStatus()
        r.groesse = ctypes.sizeof(r)
        if not _lib.k1520_serial_status(self._handle, i, ctypes.byref(r)):
            return None
        werte = {}
        for f, *_ in K1520SerStatus._fields_[1:]:
            v = getattr(r, f)
            werte[f] = _txt(v) if isinstance(v, bytes) else v
        return SerialStatus(**werte)


    @staticmethod
    def version() -> str:
        """Version string of the loaded core library."""
        v = _lib.k1520_version()
        return v.decode("utf-8") if v else ""
