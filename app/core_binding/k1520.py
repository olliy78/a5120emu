"""
K1520 Emulator - Core Binding
==============================

Python ctypes wrapper for libk1520core.so C-API.
Provides high-level Python interface to K1520 emulator.

Typical usage:
    from core_binding.k1520 import K1520Emulator
    
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
from typing import Optional, Tuple
import threading
import time

# ════════════════════════════════════════════════════════════════════════════
# Library Loading
# ════════════════════════════════════════════════════════════════════════════

def find_libk1520core() -> Path:
    """Find libk1520core.so in common build locations."""
    search_paths = [
        Path(__file__).parent.parent / "build" / "libk1520core.so",
        Path(__file__).parent.parent / "build" / "libk1520core.so.1",
        Path("/usr/local/lib/libk1520core.so"),
        Path("/usr/lib/libk1520core.so"),
    ]
    
    for path in search_paths:
        if path.exists():
            return path
    
    raise FileNotFoundError(
        f"libk1520core.so not found in:\n" +
        "\n".join(f"  {p}" for p in search_paths) +
        f"\n\nBuild with: cd {Path(__file__).parent.parent} && "
        "mkdir -p build && cd build && cmake .. && make -j4"
    )

try:
    _lib_path = find_libk1520core()
    print(f"[DEBUG] Loading library from: {_lib_path}", file=sys.stderr)
    _lib = ctypes.CDLL(str(_lib_path), use_errno=True)
    print(f"[DEBUG] Library loaded successfully", file=sys.stderr)
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    sys.exit(1)

# ════════════════════════════════════════════════════════════════════════════
# C-API Function Signatures
# ════════════════════════════════════════════════════════════════════════════

# Handle type (opaque pointer)
K1520Handle = ctypes.c_void_p

# Enum values for format names
DISK_FORMATS = {
    "CPA780": 0,    # CPABCGEN 780K format
    "CPA800": 1,    # CPABCGEN 800K format
    "SCP": 2,       # SCP (Symbolic Computer Protocol)
    "UDOS": 3,      # Unified DOS
    "MUTOS": 4,     # MU-DOS
    "K7": 5,        # K7 format
    "HDOS": 6,      # HDOS
}

# k1520_create(machine_type: int) -> K1520Handle
_lib.k1520_create.argtypes = [ctypes.c_int]
_lib.k1520_create.restype = K1520Handle

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

# k1520_unmount_disk(K1520Handle, drive: int) -> bool
_lib.k1520_unmount_disk.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_unmount_disk.restype = ctypes.c_bool

# k1520_is_disk_active(K1520Handle, drive: int) -> bool
_lib.k1520_disk_active.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_active.restype = ctypes.c_bool

# k1520_is_disk_write_protected(K1520Handle, drive: int) -> bool
_lib.k1520_disk_write_protected.argtypes = [K1520Handle, ctypes.c_int]
_lib.k1520_disk_write_protected.restype = ctypes.c_bool

# k1520_set_write_protect(K1520Handle, drive: int, wp: bool) -> void
_lib.k1520_set_write_protect.argtypes = [K1520Handle, ctypes.c_int, ctypes.c_bool]
_lib.k1520_set_write_protect.restype = None

# ════════════════════════════════════════════════════════════════════════════
# K1520 Emulator Python Class
# ════════════════════════════════════════════════════════════════════════════

class K1520Emulator:
    """Python wrapper for K1520 A5120 emulator."""
    
    def __init__(self):
        """Initialize emulator instance."""
        try:
            handle = _lib.k1520_create(0)  # K1520_MACHINE_A5120 = 0
            if not handle:
                raise RuntimeError("k1520_create returned NULL - possible constructor exception")
            self._handle = handle
        except Exception as e:
            raise RuntimeError(f"Failed to create K1520 emulator: {e}")
        self._running = False
        self._thread: Optional[threading.Thread] = None
    
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
            format_name: Disk format name (cpa800, cpa780, cpa640, cpa624)
            write_protect: Whether disk is write-protected
            
        Returns:
            True if successful
        """
        if not os.path.exists(path):
            raise FileNotFoundError(f"Disk image not found: {path}")
        
        path_bytes = path.encode('utf-8')
        format_bytes = format_name.encode('utf-8')
        return _lib.k1520_mount_disk(self._handle, ctypes.c_int(drive), path_bytes, format_bytes, ctypes.c_bool(write_protect))
    
    def unmount_disk(self, drive: int) -> bool:
        """
        Unmount a disk.
        
        Args:
            drive: Drive number (0-3)
            
        Returns:
            True if successful
        """
        return _lib.k1520_unmount_disk(self._handle, ctypes.c_int(drive))
    
    def is_disk_active(self, drive: int) -> bool:
        """Check if disk is currently mounted."""
        return _lib.k1520_disk_active(self._handle, ctypes.c_int(drive))
    
    def is_disk_write_protected(self, drive: int) -> bool:
        """Check if disk is write-protected."""
        return _lib.k1520_disk_write_protected(self._handle, ctypes.c_int(drive))
    
    def set_disk_write_protect(self, drive: int, write_protect: bool):
        """Set write-protect status of a disk."""
        _lib.k1520_set_write_protect(self._handle, ctypes.c_int(drive), ctypes.c_bool(write_protect))
