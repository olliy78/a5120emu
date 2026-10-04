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


def test_unbuilt_machine_type_is_refused_with_a_reason():
    """Ein Wert außerhalb der Aufzählung gibt NULL zurück — mit einem Grund in
    `k1520_last_init_error`, nicht still.

    Bis AP-E4b stand hier der K8915 (= 2), bis AP-P1c der PRG710 (= 1); beide sind
    gebaut, übrig bleibt ein Wert außerhalb der Aufzählung.
    """
    from app.core_binding.k1520 import _lib, K1520Handle

    for typ in (7,):
        assert not _lib.k1520_create(typ), typ
        grund = _lib.k1520_last_init_error().decode()
        assert "nicht implementiert" in grund, grund
        assert not _lib.k1520_create_configured(typ, None, None, None, None), typ
        assert "nicht implementiert" in _lib.k1520_last_init_error().decode()

    # Der Grund darf einen folgenden, erfolgreichen Aufruf nicht überdauern.
    handle = _lib.k1520_create(0)
    assert handle
    assert _lib.k1520_last_init_error() == b""
    _lib.k1520_destroy(K1520Handle(handle))


def test_prg710_variants_can_be_created_and_run():
    """`k1520_create_prg710` (AP-P1c): beide Varianten, Typ 1, laufen nach dem
    Netz-Ein einige Takte; `k1520_create(1)` baut die Variante 0; Variante 2 → NULL."""
    from app.core_binding.k1520 import _lib, K1520Handle

    for variante in (0, 1):
        handle = _lib.k1520_create_prg710(variante, None, None, None, None)
        assert handle, _lib.k1520_last_init_error()
        h = K1520Handle(handle)
        try:
            assert _lib.k1520_machine_type(h) == 1
            assert (_lib.k1520_fb_width(h), _lib.k1520_fb_height(h)) == (640, 288)
            _lib.k1520_power_on(h)
            assert _lib.k1520_run(h, 50_000) > 0
            assert _lib.k1520_drive_format_count(h, 0) > 0
            assert _lib.k1520_drive_format_count(h, 2) == 0
        finally:
            _lib.k1520_destroy(h)

    handle = _lib.k1520_create(1)
    assert handle, _lib.k1520_last_init_error()
    _lib.k1520_destroy(K1520Handle(handle))

    assert not _lib.k1520_create_prg710(2, None, None, None, None)
    assert "Variante" in _lib.k1520_last_init_error().decode()


def test_pc1715_can_be_created_and_run():
    """`k1520_create_pc1715` (AP-1b): Typ 3, beide Bildschirme (640x300 / 512x255, je mit Statuszeile), laufen
    einige Takte; `k1520_create(3)` baut die Vorgabe; Variante 1 (PC 1715W, seit AP-W3)
    baut 640x288 und lehnt den K7221 ab; unbekannte Werte → NULL mit Grund."""
    from app.core_binding.k1520 import _lib, K1520Handle

    for bild, groesse in ((0, (640, 300)), (1, (512, 255))):
        handle = _lib.k1520_create_pc1715(0, bild, 0, None, None, None, None)
        assert handle, _lib.k1520_last_init_error()
        h = K1520Handle(handle)
        try:
            assert _lib.k1520_machine_type(h) == 3
            assert (_lib.k1520_fb_width(h), _lib.k1520_fb_height(h)) == groesse
            _lib.k1520_power_on(h)
            assert _lib.k1520_run(h, 50_000) > 0
        finally:
            _lib.k1520_destroy(h)

    handle = _lib.k1520_create(3)
    assert handle, _lib.k1520_last_init_error()
    _lib.k1520_destroy(K1520Handle(handle))

    handle = _lib.k1520_create_pc1715(1, 0, 0, None, None, None, None)
    assert handle, _lib.k1520_last_init_error()
    h = K1520Handle(handle)
    try:
        assert _lib.k1520_machine_type(h) == 3
        assert (_lib.k1520_fb_width(h), _lib.k1520_fb_height(h)) == (640, 288)
        _lib.k1520_power_on(h)
        assert _lib.k1520_run(h, 50_000) > 0
    finally:
        _lib.k1520_destroy(h)
    assert not _lib.k1520_create_pc1715(1, 1, 0, None, None, None, None)
    assert "1715W" in _lib.k1520_last_init_error().decode()
    assert not _lib.k1520_create_pc1715(2, 0, 0, None, None, None, None)
    assert not _lib.k1520_create_pc1715(0, 2, 0, None, None, None, None)
    assert not _lib.k1520_create_pc1715(0, 0, 2, None, None, None, None)


def test_k8915_can_be_created_and_reports_its_type():
    """`k1520_create(K1520_MACHINE_K8915)` liefert seit AP-E4b eine Maschine;
    `k1520_machine_type` meldet 2, die Anzeigen haben ihren Ruhezustand."""
    from app.core_binding.k1520 import _lib, K1520Handle

    handle = _lib.k1520_create(2)
    assert handle, _lib.k1520_last_init_error()
    h = K1520Handle(handle)
    try:
        assert _lib.k1520_machine_type(h) == 2
        assert (_lib.k1520_fb_width(h), _lib.k1520_fb_height(h)) == (640, 288)
        _lib.k1520_power_on(h)
        assert _lib.k1520_panel_lamps(h) == 0xFF, "Latch 61H nach /RESET: alles dunkel"
        assert _lib.k1520_bell_count(h) == 0
        assert _lib.k1520_run(h, 100_000) > 0
        # Vorgabebestückung des Geräts: 2 × K5601, Platz 2/3 leer.
        assert _lib.k1520_drive_format_count(h, 0) > 0
        assert _lib.k1520_drive_format_count(h, 2) == 0
    finally:
        _lib.k1520_destroy(h)

    # Bestückung über create_configured, wie beim A5120.
    handle = _lib.k1520_create_configured(2, b"K5601", b"K5600.20", None, None)
    assert handle, _lib.k1520_last_init_error()
    h = K1520Handle(handle)
    try:
        assert _lib.k1520_machine_type(h) == 2
        assert _lib.k1520_drive_format_count(h, 1) > 0
    finally:
        _lib.k1520_destroy(h)


def test_nmi_button_restarts_the_k8915_self_test_and_does_nothing_on_the_a5120():
    """`k1520_nmi` (AP-UI1): am K8915 setzt die NMI-Flanke das ROM auf 0066H —
    `OUT (61H),FFH`, dann Selbsttest von vorn; die Lampen gehen aus.  Am A5120 hat
    die Funktion keine Wirkung (kein NMI-Taster) — der Lauf geht einfach weiter."""
    from app.core_binding.k1520 import _lib, K1520Handle

    handle = _lib.k1520_create(2)
    assert handle, _lib.k1520_last_init_error()
    h = K1520Handle(handle)
    try:
        _lib.k1520_power_on(h)
        _lib.k1520_run(h, 1_000_000)
        # Die volle Wirkung (Selbsttest von vorn, „DIAGNOSTIC") prüft
        # K8915Boot.NmiImRomStartetDenSelbsttestNeu; hier geht es um die ABI.
        _lib.k1520_nmi(h)
        _lib.k1520_run(h, 200)
        assert _lib.k1520_panel_lamps(h) == 0xFF, "0066H: OUT (61H),FFH"
    finally:
        _lib.k1520_destroy(h)

    handle = _lib.k1520_create(0)
    assert handle
    h = K1520Handle(handle)
    try:
        _lib.k1520_power_on(h)
        _lib.k1520_nmi(h)
        assert _lib.k1520_run(h, 50_000) > 0
        assert _lib.k1520_panel_lamps(h) == 0
    finally:
        _lib.k1520_destroy(h)


def test_a5120_answers_the_machine_neutral_indicators(booted):
    """Die angehängten Anzeigefunktionen ändern am A5120 nichts: Typ 0, kein
    Anzeigefeld, kein Summerzähler; `k1520_screen_char` liest dasselbe Bild wie
    der bisherige Weg über den Bus (F800H) — sobald das Bild steht (vorher kann
    der Bus bei F800H etwas anderes zeigen als die Karte)."""
    from app.core_binding.k1520 import _lib

    emulator = booted
    h = emulator._handle
    assert _lib.k1520_machine_type(h) == 0
    assert _lib.k1520_panel_lamps(h) == 0
    assert _lib.k1520_bell_count(h) == 0
    for r in range(24):
        for c in range(80):
            assert _lib.k1520_screen_char(h, c, r) == emulator.mem_read(0xF800 + r * 80 + c)
    for c, r in ((-1, 0), (80, 0), (0, 24), (0, -1)):
        assert _lib.k1520_screen_char(h, c, r) == 0


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


# ─── Strukturen und Aufzählungen der seriellen Schnittstellen (Entwurf 19 §8) ──

_C_TYP = {
    "uint8_t": ctypes.c_uint8, "uint16_t": ctypes.c_uint16, "uint32_t": ctypes.c_uint32,
    "uint64_t": ctypes.c_uint64, "int": ctypes.c_int, "bool": ctypes.c_bool,
}


def _header_text() -> str:
    text = HEADER.read_text(encoding="utf-8")
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.S)


def _c_feldtyp(typ: str, dims: list):
    t = _C_TYP[typ] if typ != "char" else ctypes.c_char
    for d in reversed(dims):
        t = t * int(d)
    return t


def test_serial_structs_match_the_header_field_by_field():
    """Name, Typ und Feldgröße jedes Feldes der `K1520Ser*`-Strukturen stimmen mit der
    ctypes-Seite überein — gleiche Felderfolge heisst gleiches Layout.  Ein fehlendes
    oder verschobenes Feld bräche sonst still (falsche Werte statt eines Fehlers)."""
    from app.core_binding import k1520 as B
    text = _header_text()
    gefunden = 0
    for m in re.finditer(r"typedef struct \{([^{}]*)\}\s*(K1520Ser(?:Info|Konfig|Status))\s*;", text, flags=re.S):
        koerper, name = m.groups()
        py = getattr(B, name)
        soll = []
        for decl in koerper.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            typ, rest = decl.split(None, 1)
            for feld in rest.split(","):
                fm = re.fullmatch(r"\s*(\w+)((?:\[\d+\])*)\s*", feld)
                assert fm, (name, decl)
                soll.append((fm.group(1), _c_feldtyp(typ, re.findall(r"\[(\d+)\]", fm.group(2)))))
        ist = [(n, t) for n, t in py._fields_]
        assert [n for n, _ in soll] == [n for n, _ in ist], name
        for (n, ts), (_, ti) in zip(soll, ist):
            assert ctypes.sizeof(ts) == ctypes.sizeof(ti), (name, n)
        gefunden += 1
    assert gefunden == 3


def test_serial_enum_values_match_the_binding():
    from app.core_binding import k1520 as B
    text = _header_text()
    werte = {}
    for m in re.finditer(r"typedef enum \{([^{}]*)\}\s*(K1520Ser(?:Betriebsart|Rolle|Zustand)|K1520HostArt)\s*;", text, flags=re.S):
        n = 0
        for eintrag in m.group(1).split(","):
            eintrag = eintrag.strip()
            if not eintrag:
                continue
            k, _, v = eintrag.partition("=")
            n = int(v) if v.strip() else n
            werte[k.strip()] = n
            n += 1
    assert len(werte) == 3 + 2 + 5 + 4
    for k, v in werte.items():
        pyname = k.replace("K1520_", "", 1)
        assert getattr(B, pyname) == v, k

# ─── A5120.16: Erweiterungsmodul (S5) ────────────────────────────────────────

def test_em_state_struct_matches_the_c_layout():
    """`K1520EmState` in ctypes hat genau den Aufbau des C-Headers.

    Eine vergessene oder verschobene Komponente fiele sonst erst in der
    Oberfläche auf — als falsche Registerwerte, nicht als Fehler.
    """
    from app.core_binding.k1520 import _lib, K1520EmState
    assert ctypes.sizeof(K1520EmState) == _lib.k1520_em_state_size()


def test_machine_without_em_reports_nothing(emulator):
    assert emulator.em_variant() == ""
    assert emulator.em_leds() == (False, False)
    assert not emulator.em_mode16()
    assert emulator.em_state() is None


def test_unknown_em_name_is_refused():
    from app.core_binding.k1520 import _lib
    h = _lib.k1520_create_with_em(0, None, None, None, None, b"em999")
    assert not h
    assert b"em999" in _lib.k1520_last_init_error()


@pytest.mark.parametrize("variant,model", [("em256", 1), ("em064", 2)])
def test_machine_with_em_after_power_on(variant, model):
    """Netz-Ein: 8-Bit-Mode (V2 an), RAMEN aus (V1 aus), U8001 im RESET16."""
    from app.core_binding.k1520 import K1520Emulator
    emu = K1520Emulator(em=variant)
    try:
        emu.power_on()
        assert emu.em_variant() == variant
        assert emu.em_leds() == (False, True)
        assert not emu.em_mode16()
        st = emu.em_state()
        assert st is not None
        assert st.model == model
        assert st.mode8 and st.reset16 and st.in_reset
        assert st.a33 == 0 and st.seg_mode == 0
        # Ein kurzer Lauf ändert daran nichts (das Boot-ROM fasst das EM nicht an).
        emu.run(20000)
        assert emu.em_leds()[1]
    finally:
        del emu
