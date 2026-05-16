# Feinentwurf: C-API (libk1520.so)

**Modul:** `core/api/`  
**Dateien:** `k1520_api.h`, `k1520_api.cpp`

---

## 1. Designziele

- **Stabiles ABI**: `extern "C"`, keine C++-Typen in der öffentlichen Schnittstelle
- **Opake Handle**: Python sieht nur `void*` – keine internen Strukturen
- **Thread-sicher**: `k1520_run()` kann in separatem Thread aufgerufen werden
- **Minimal**: Nur was die GUI tatsächlich braucht; kein Over-Engineering

---

## 2. Header (k1520_api.h)

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ─── Typen ─────────────────────────────────────────────────────── */

typedef void* K1520Handle;

typedef enum {
    K1520_MACHINE_A5120  = 0,
    K1520_MACHINE_PRG710 = 1,
    K1520_MACHINE_K8915  = 2,
} K1520MachineType;

typedef struct {
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint8_t  i, r, im;
    bool     iff1, iff2, halted;
} K1520CpuState;

typedef enum {
    K1520_SERIAL_DFU     = 0,   /* K8025 SIO A33 Kanal A (V.24/IFSS) */
    K1520_SERIAL_PRINTER = 1,   /* K8025 SIO A32 Kanal B (Hauptdrucker) */
    K1520_SERIAL_AUX     = 2,   /* K8025 SIO A32 Kanal A (Zusatzdrucker) */
} K1520SerialPort;

typedef void (*K1520SerialCallback)(void* ctx, uint8_t byte);

/* ─── Lifecycle ──────────────────────────────────────────────────── */

/** Erzeugt eine neue Maschineninstanz (nicht gestartet). */
K1520Handle k1520_create(K1520MachineType type);

/** Zerstört die Instanz und gibt alle Ressourcen frei. */
void k1520_destroy(K1520Handle h);

/** Hardware-Reset (wie /RESET-Taste). CPU springt nach 0000H. */
void k1520_reset(K1520Handle h);

/** Power-On: Reset + ggf. Initialisierung. */
void k1520_power_on(K1520Handle h);

/* ─── Ausführung ─────────────────────────────────────────────────── */

/**
 * Führt bis zu max_cycles CPU-Zyklen aus.
 * Rückgabe: tatsächlich verbrauchte Zyklen.
 * Thread-safe: kann von einem dedizierten Emulator-Thread aufgerufen werden.
 */
int k1520_run(K1520Handle h, int max_cycles);

/** Stoppt die Ausführung (für den nächsten run()-Aufruf). */
void k1520_stop(K1520Handle h);

bool k1520_is_running(K1520Handle h);

/* ─── Framebuffer (GUI-Modus) ────────────────────────────────────── */

/**
 * Gibt Pointer auf den Framebuffer zurück.
 * Format: k1520_fb_width() × k1520_fb_height() Bytes, monochrom (0/255).
 * Gültig bis zum nächsten k1520_run()-Aufruf.
 * Thread-safe: nicht garantiert – nur aus GUI-Thread lesen!
 */
const uint8_t* k1520_framebuffer(K1520Handle h);
int            k1520_fb_width(K1520Handle h);    /* 640 */
int            k1520_fb_height(K1520Handle h);   /* 288 */

/**
 * Gibt true zurück, wenn sich der Framebuffer seit dem letzten
 * k1520_fb_clear_dirty()-Aufruf geändert hat.
 */
bool k1520_fb_dirty(K1520Handle h);
void k1520_fb_clear_dirty(K1520Handle h);

/* ─── Konsolen-Modus (CLI) ───────────────────────────────────────── */

/** Aktiviert/deaktiviert den Text-Bypass-Modus (K7024 → stdout). */
void k1520_set_console_mode(K1520Handle h, bool enable);

/**
 * Poll für Text-Änderungen im VRAM.
 * Gibt true zurück wenn eine Änderung vorliegt; *x, *y, *ch werden gesetzt.
 * Muss mehrfach aufgerufen werden bis false zurückgegeben wird.
 */
bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch);

void k1520_console_get_cursor(K1520Handle h, int* x, int* y);

/* ─── Tastatur ───────────────────────────────────────────────────── */

/**
 * Tastenereignisse (Qt-Keycodes oder ASCII-Werte).
 * shift/ctrl: Modifier-Zustand.
 */
void k1520_key_press(K1520Handle h, uint32_t keycode, bool shift, bool ctrl);
void k1520_key_release(K1520Handle h, uint32_t keycode);

/** Für CLI-Modus: einzelnes ASCII-Zeichen injizieren. */
void k1520_console_key(K1520Handle h, char c);

/* ─── Diskettenlaufwerke ─────────────────────────────────────────── */

/**
 * Mountet ein Disk-Image.
 * drive:       0–3 (A, B, C, D)
 * image_path:  Pfad zur .img-Datei
 * format_name: z.B. "cpa780", "cpa800" (aus eingebautem Format-Register)
 * write_protect: true = schreibgeschützt
 */
bool k1520_mount_disk(K1520Handle h, int drive,
                       const char* image_path,
                       const char* format_name,
                       bool write_protect);

bool k1520_unmount_disk(K1520Handle h, int drive);

/** true wenn Laufwerk aktuell auf Disk zugreift (LED-Anzeige). */
bool k1520_disk_active(K1520Handle h, int drive);

bool k1520_disk_write_protected(K1520Handle h, int drive);
void k1520_set_write_protect(K1520Handle h, int drive, bool wp);

/**
 * Gibt alle bekannten Disk-Format-Namen zurück (null-terminated array).
 * Letzter Eintrag ist NULL.
 */
const char** k1520_disk_format_names(K1520Handle h);

/**
 * Lädt Format-Definitionen aus einer externen .cfg-Datei.
 * (Optional; eingebaute Formate sind immer verfügbar.)
 */
bool k1520_load_disk_formats(K1520Handle h, const char* cfg_path);

/**
 * Erstellt ein neues leeres Disk-Image.
 */
bool k1520_create_disk_image(K1520Handle h, const char* path,
                               const char* format_name);

/* ─── Serielle Schnittstellen ────────────────────────────────────── */

/**
 * Setzt Callback für empfangene Bytes (K1520 → Host).
 * Wird aus dem Emulator-Thread aufgerufen!
 */
void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                              K1520SerialCallback cb, void* ctx);

/** Sendet ein Byte zum K1520 (Host → K1520). */
void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte);

/* ─── Debug / Diagnose ───────────────────────────────────────────── */

void k1520_get_cpu_state(K1520Handle h, K1520CpuState* out);
uint8_t k1520_mem_read(K1520Handle h, uint16_t addr);
void    k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data);
uint8_t k1520_io_read(K1520Handle h, uint8_t port);

/** Gibt zuletzt aufgetretene Fehlermeldung zurück (statischer Buffer). */
const char* k1520_last_error(K1520Handle h);

/** Versionsinformation. */
const char* k1520_version(void);

#ifdef __cplusplus
}
#endif
```

---

## 3. Thread-Sicherheitsmodell

```
┌──────────────────────────────────────────┐
│  GUI-Thread (Python Qt6 main thread)     │
│                                          │
│  k1520_mount_disk()    → mutex lock      │
│  k1520_key_press()     → event queue     │
│  k1520_framebuffer()   → mutex lock      │
│  k1520_fb_dirty()      → atomic bool     │
│  k1520_disk_active()   → atomic bool     │
└──────────────────────────────────────────┘
              │ QThread
              ▼
┌──────────────────────────────────────────┐
│  Emulator-Thread                         │
│                                          │
│  k1520_run()           → Hauptschleife   │
│  (liest event queue)                     │
│  (schreibt in framebuffer)               │
│  (setzt dirty flag)                      │
└──────────────────────────────────────────┘
```

**Implementierung:**
- Framebuffer: Doppelpuffer (front/back buffer swap)
- Keyboard-Events: Thread-safe Queue (std::mutex)
- Disk-Mount/Unmount: Während `k1520_run()` gesperrt (kurzer Lock)
- `k1520_fb_dirty()`: `std::atomic<bool>`

---

## 4. Python ctypes-Bindung

```python
# app/core_binding/k1520.py

import ctypes
import pathlib
import threading
from typing import Callable, Optional

def _load_lib() -> ctypes.CDLL:
    lib_path = pathlib.Path(__file__).parent.parent.parent / "build" / "libk1520core.so"
    lib = ctypes.CDLL(str(lib_path))
    _setup_signatures(lib)
    return lib

def _setup_signatures(lib: ctypes.CDLL):
    lib.k1520_create.restype = ctypes.c_void_p
    lib.k1520_create.argtypes = [ctypes.c_int]

    lib.k1520_destroy.restype = None
    lib.k1520_destroy.argtypes = [ctypes.c_void_p]

    lib.k1520_run.restype = ctypes.c_int
    lib.k1520_run.argtypes = [ctypes.c_void_p, ctypes.c_int]

    lib.k1520_framebuffer.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.k1520_framebuffer.argtypes = [ctypes.c_void_p]

    lib.k1520_fb_width.restype = ctypes.c_int
    lib.k1520_fb_height.restype = ctypes.c_int

    lib.k1520_fb_dirty.restype = ctypes.c_bool
    lib.k1520_fb_clear_dirty.restype = None

    lib.k1520_key_press.restype = None
    lib.k1520_key_press.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                     ctypes.c_bool, ctypes.c_bool]
    lib.k1520_mount_disk.restype = ctypes.c_bool
    lib.k1520_mount_disk.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_char_p,
                                      ctypes.c_bool]
    # ... weitere Signaturen

_lib: Optional[ctypes.CDLL] = None

def get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        _lib = _load_lib()
    return _lib


class K1520Machine:
    """Python-Wrapper um K1520Handle."""

    def __init__(self, machine_type: int = 0):  # 0 = A5120
        lib = get_lib()
        self._handle = lib.k1520_create(machine_type)
        if not self._handle:
            raise RuntimeError("k1520_create failed")
        self._lib = lib

    def __del__(self):
        if self._handle:
            self._lib.k1520_destroy(self._handle)

    def reset(self):
        self._lib.k1520_reset(self._handle)

    def run(self, cycles: int) -> int:
        return self._lib.k1520_run(self._handle, cycles)

    def framebuffer_bytes(self) -> bytes:
        ptr = self._lib.k1520_framebuffer(self._handle)
        w = self._lib.k1520_fb_width(self._handle)
        h = self._lib.k1520_fb_height(self._handle)
        return bytes(ptr[:w * h])

    def fb_dirty(self) -> bool:
        return self._lib.k1520_fb_dirty(self._handle)

    def fb_clear_dirty(self):
        self._lib.k1520_fb_clear_dirty(self._handle)

    def key_press(self, keycode: int, shift: bool = False, ctrl: bool = False):
        self._lib.k1520_key_press(self._handle, keycode, shift, ctrl)

    def mount_disk(self, drive: int, path: str, fmt: str,
                   write_protect: bool = False) -> bool:
        return self._lib.k1520_mount_disk(
            self._handle, drive,
            path.encode(), fmt.encode(), write_protect)
```

---

## 5. Fehlerbehandlung

Alle C-API-Funktionen die fehlschlagen können, geben `false` oder `NULL` zurück. Die Ursache kann mit `k1520_last_error()` abgefragt werden:

```c
if (!k1520_mount_disk(h, 0, "bad_path.img", "cpa780", false)) {
    fprintf(stderr, "Fehler: %s\n", k1520_last_error(h));
}
```

---

## 6. Build-System

```cmake
# core/api/CMakeLists.txt
add_library(k1520core SHARED
    k1520_api.cpp
    # Alle Karten und Primitiven
    ../primitives/z80_cpu.cpp
    ../primitives/z80_pio.cpp
    ../primitives/z80_sio.cpp
    ../primitives/z80_ctc.cpp
    ../primitives/ram_device.cpp
    ../cards/k2526/k2526.cpp
    ../cards/k3526/k3526.cpp
    ../cards/k7024/k7024.cpp
    ../cards/k8025/k8025.cpp
    ../cards/k5122/k5122.cpp
    ../peripherals/k7637/k7637.cpp
    ../peripherals/floppy_drive/floppy_drive.cpp
    ../peripherals/floppy_drive/format_parser.cpp
    ../machines/a5120/a5120.cpp
    ../bus/k1520_bus.cpp
    ../bus/koppelbus.cpp
)

set_target_properties(k1520core PROPERTIES
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
    PUBLIC_HEADER k1520_api.h
)

install(TARGETS k1520core
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include)
```
