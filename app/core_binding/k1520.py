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

# k1520_mount_disk(K1520Handle, drive: int, path: const char*, format: const char*, wp: bool) -> bool
_lib.k1520_mount_disk.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool]
_lib.k1520_mount_disk.restype = ctypes.c_bool

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
K1520SerialRxCb = ctypes.CFUNCTYPE(None, ctypes.c_uint8, ctypes.c_void_p)
_lib.k1520_serial_set_rx_cb.argtypes = [K1520Handle, ctypes.c_int,
                                        K1520SerialRxCb, ctypes.c_void_p]
_lib.k1520_serial_set_rx_cb.restype = None

# Textbildschirm des K7024: 80x24 Zeichen ab 0xF800 (Bit7 = Invers-Attribut).
VRAM_BASE, VRAM_COLS, VRAM_ROWS = 0xF800, 80, 24

# ════════════════════════════════════════════════════════════════════════════
# K1520 Emulator Python Class
# ════════════════════════════════════════════════════════════════════════════

class K1520Emulator:
    """Python wrapper for K1520 A5120 emulator."""
    
    def __init__(self, drive_types: Optional[list] = None):
        """Initialize emulator instance.

        Args:
            drive_types: optional list of up to 4 core DriveProfile names, one per
                K5122 slot (e.g. ``["K5601", "K5601", "K5601", "none"]``).  An entry
                that is ``None`` or ``""`` keeps the slot default (K5601); ``"none"``
                marks an empty slot ("kein Laufwerk").  ``None`` (the default) builds
                the standard machine (4× K5601).
        """
        # Zuerst setzen: schlägt die Erzeugung fehl, läuft __del__ trotzdem und
        # darf nicht über ein fehlendes Attribut stolpern.
        self._handle = None
        self._drive_types = list(drive_types) if drive_types else None
        try:
            handle = self._create_handle(self._drive_types)
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
        self._running = False
        self._thread: Optional[threading.Thread] = None

    @staticmethod
    def _create_handle(drive_types: Optional[list]):
        """Create a core handle, configured with per-slot drive profiles if given."""
        if not drive_types:
            return _lib.k1520_create(0)  # K1520_MACHINE_A5120 = 0, default 4× K5601

        names = list(drive_types)[:4] + [None] * (4 - len(drive_types))

        def enc(name):
            return name.encode("utf-8") if name else None  # None/"" → core keeps default

        return _lib.k1520_create_configured(
            0, enc(names[0]), enc(names[1]), enc(names[2]), enc(names[3]))

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
        _lib.k1520_key_press(self._handle, ctypes.c_uint32(keycode), ctypes.c_bool(shift), ctypes.c_bool(ctrl))
    
    def key_release(self, keycode: int):
        """
        Queue a key release event.
        
        Args:
            keycode: Z80 keyboard scan code
        """
        _lib.k1520_key_release(self._handle, ctypes.c_uint32(keycode))
    
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
        """
        chars = [chr(self.mem_read(VRAM_BASE + i) & 0x7F)
                 for i in range(VRAM_COLS * VRAM_ROWS)]
        return "\n".join("".join(chars[r * VRAM_COLS:(r + 1) * VRAM_COLS])
                          for r in range(VRAM_ROWS))

    @staticmethod
    def version() -> str:
        """Version string of the loaded core library."""
        v = _lib.k1520_version()
        return v.decode("utf-8") if v else ""
