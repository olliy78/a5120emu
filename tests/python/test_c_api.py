"""C-ABI: `core/api/k1520_api.h` ↔ `libk1520core.so` ↔ ctypes-Bindung.

Das ist die einzige Schnittstelle zwischen Kern und GUI und die einzige Stelle,
an der eine Änderung **still** bricht: der C++-Compiler prüft die Python-Seite
nicht, und ctypes meldet eine falsche Signatur erst beim Aufruf — oft als
Absturz statt als Fehlermeldung.  Diese Tests vergleichen die drei Seiten
mechanisch miteinander.
"""

import ctypes
import re

import pytest

from conftest import PROJECT_ROOT, requires_core

pytestmark = requires_core

HEADER = PROJECT_ROOT / "core" / "api" / "k1520_api.h"


def header_functions() -> set:
    """Alle im Header deklarierten `k1520_*`-Funktionen."""
    text = HEADER.read_text(encoding="utf-8")
    # Kommentare entfernen, damit Erwähnungen im Fließtext nicht mitzählen.
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return set(re.findall(r"\b(k1520_[a-z0-9_]+)\s*\(", text))


def test_header_declares_the_expected_api():
    """Grundplausibilität: der Header wird gefunden und ist nicht leer."""
    funcs = header_functions()
    assert HEADER.exists(), f"C-API-Header fehlt: {HEADER}"
    assert len(funcs) >= 30, f"nur {len(funcs)} Funktionen im Header gefunden"
    # Kernfunktionen, ohne die die GUI nicht arbeiten kann.
    for essential in ("k1520_create", "k1520_destroy", "k1520_run",
                      "k1520_power_on", "k1520_framebuffer", "k1520_mount_disk"):
        assert essential in funcs


def test_every_header_function_is_exported_by_the_library():
    """Jede deklarierte Funktion ist in der gebauten .so auch vorhanden."""
    from app.core_binding.k1520 import _lib
    missing = [f for f in sorted(header_functions()) if not hasattr(_lib, f)]
    assert not missing, (
        "im Header deklariert, aber nicht in libk1520core.so exportiert: "
        + ", ".join(missing)
    )


def test_every_header_function_has_ctypes_signatures():
    """Jede Funktion ist in der Bindung mit argtypes/restype deklariert.

    Fehlt die Deklaration, konvertiert ctypes stillschweigend nach `int` — auf
    64-Bit-Systemen wird der Handle-Zeiger dabei abgeschnitten und der Aufruf
    stürzt ab oder liefert Unsinn.  Wer die C-API erweitert, muss die Bindung
    mitziehen; genau das erzwingt dieser Test.
    """
    from app.core_binding import k1520 as binding

    source = (PROJECT_ROOT / "app" / "core_binding" / "k1520.py").read_text("utf-8")
    declared = set(re.findall(r"_lib\.(k1520_[a-z0-9_]+)\.argtypes", source))
    undeclared = sorted(header_functions() - declared)
    assert not undeclared, (
        "C-API-Funktionen ohne ctypes-Deklaration in app/core_binding/k1520.py: "
        + ", ".join(undeclared)
    )


def test_version_is_a_semver_string():
    from app.core_binding.k1520 import K1520Emulator
    version = K1520Emulator.version()
    assert re.fullmatch(r"\d+\.\d+\.\d+", version), f"unerwartete Version: {version!r}"


def test_create_and_destroy_roundtrip():
    """Handle-Lebenszyklus direkt auf der C-Ebene (ohne Wrapper)."""
    from app.core_binding.k1520 import _lib, K1520Handle

    handle = _lib.k1520_create(0)
    assert handle, f"k1520_create scheiterte: {_lib.k1520_last_init_error()}"
    assert isinstance(handle, int)      # ctypes liefert c_void_p als int
    _lib.k1520_destroy(K1520Handle(handle))


def test_framebuffer_geometry_matches_pointer_size(emulator):
    """`k1520_fb_width/height` und der Zeigerinhalt passen zusammen.

    Der Framebuffer des K7024 ist monochrom: EIN Byte Helligkeit je Pixel
    (640x288 = 184320 Byte).  Die GUI verlässt sich auf genau diese Geometrie
    (`app/ui/screen_widget.FB_WIDTH/FB_HEIGHT`).
    """
    from app.core_binding.k1520 import _lib

    handle = emulator._handle
    width = _lib.k1520_fb_width(handle)
    height = _lib.k1520_fb_height(handle)
    assert (width, height) == (640, 288), f"unerwartete Bildgröße {width}x{height}"

    ptr = _lib.k1520_framebuffer(handle)
    assert ptr, "k1520_framebuffer lieferte einen Nullzeiger"
    # Letztes Byte lesbar → der Puffer ist wirklich width*height groß.
    ctypes.cast(ptr, ctypes.POINTER(ctypes.c_uint8))[width * height - 1]


def test_missing_disk_file_raises_before_reaching_the_core(emulator):
    """Der Wrapper prüft den Pfad selbst — die C-API sieht ihn gar nicht."""
    with pytest.raises(FileNotFoundError):
        emulator.mount_disk(0, "/gibt/es/nicht.img", "cpa780", False)


def test_error_string_is_set_after_a_failed_mount(emulator, temp_disk):
    """Fehlerpfad: `k1520_last_error` liefert nach einem Fehlschlag Text."""
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert not emulator.mount_disk(0, path, "kein_echtes_format", False)
    message = emulator.last_error()
    assert message, "nach fehlgeschlagenem Mount ist last_error leer"
    assert len(message) > 5


def test_mem_read_write_roundtrip(emulator):
    """Speicherzugriff über die C-API (RAM, nicht ROM-Bereich)."""
    emulator.power_on()
    emulator.mem_write(0x6000, 0xA5)
    assert emulator.mem_read(0x6000) == 0xA5
    emulator.mem_write(0x6000, 0x5A)
    assert emulator.mem_read(0x6000) == 0x5A


@pytest.mark.parametrize("drive", [0, 1, 2])
def test_format_catalogue_is_reachable_per_drive(emulator, drive):
    """Der Formatkatalog (data/formats.yaml) kommt über die C-API an."""
    formats = emulator.drive_formats(drive)
    assert formats, f"Laufwerk {drive} meldet keine Formate"
    assert emulator.drive_default_format(drive) in formats
    # Jedes gemeldete Format hat eine Beschreibung.
    for name in formats:
        assert emulator.format_description(name)


def test_formats_source_points_at_a_real_file(emulator):
    source = emulator.formats_source()
    assert source, "formats_source ist leer — Katalog nicht geladen"
    assert "formats.yaml" in source
