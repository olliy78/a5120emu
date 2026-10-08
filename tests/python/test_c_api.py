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


def test_p8000_terminal_als_maschine_und_originalterminal_am_p8000(tmp_path):
    """AP P20d: „P8000 Terminal" (Typ 5, `k1520_create_p8000_terminal`) und `terminal=original` am
    P8000 — Terminal-Funktionen mit Pixelbild, Matrix/Scancode, LEDs, Save-State P8TM; falsche
    Konfiguration → NULL mit Grund."""
    import ctypes
    from app.core_binding.k1520 import _lib, K1520Handle

    for konfig in (None, b"", b"firmware=5.0,zeichensatz=dzs-ezs,teiler=7,art=rfc2217,rolle=server,port=0",
                   b"art=telnet,rolle=client,host=127.0.0.1,port=5000,verbinden=0"):
        h = _lib.k1520_create_p8000_terminal(konfig)
        assert h, _lib.k1520_last_init_error()
        assert _lib.k1520_machine_type(h) == 5
        assert _lib.k1520_term_count(h) == 1 and _lib.k1520_term_kind(h, 0) == 1
        assert _lib.k1520_term_tty(h, 0) == -2 and _lib.k1520_term_tty(h, 1) == -1
        assert _lib.k1520_serial_count(h) == 1                       # Leitung XB5 am eigenen Hub
        assert not _lib.k1520_mount_disk(h, 0, b"/tmp/x.hfe", b"", False)
        _lib.k1520_destroy(K1520Handle(h))
    h = _lib.k1520_create(5)
    assert h and _lib.k1520_machine_type(h) == 5
    _lib.k1520_power_on(h)
    for _ in range(40):                                              # Z8-Takte: 40 × 0,1 s
        _lib.k1520_run(h, 368_640)
    text = ctypes.create_string_buffer(96)
    _lib.k1520_term_text(h, 0, 0, text, 96)
    assert text.value.startswith(b"ADM31/9600 baud"), text.value
    assert _lib.k1520_term_mode(h, 0) == 2 and _lib.k1520_term_flags(h, 0) & 32
    b, hh = ctypes.c_int(), ctypes.c_int()
    assert _lib.k1520_term_framebuffer(h, 0, None, 0, ctypes.byref(b), ctypes.byref(hh)) == 0
    assert (b.value, hh.value) == (640, 312)
    buf = ctypes.create_string_buffer(640 * 312)
    assert _lib.k1520_term_framebuffer(h, 0, buf, len(buf), None, None) == 640 * 312
    assert any(buf.raw)
    assert (_lib.k1520_fb_width(h), _lib.k1520_fb_height(h)) == (640, 312)
    assert _lib.k1520_term_frame_count(h, 0) > 200
    # Tastaturbild: jede Make-Folge gehört zu einer Matrixposition; RETURN = 1CH
    folgen = {_lib.k1520_term_matrix_scancode(h, 0, z, s) for z in range(8) for s in range(16)}
    assert 0x1C in folgen and 0xE048 in folgen
    assert _lib.k1520_term_scancode_key(h, 0, 0x3A, True)            # CAPS LOCK drücken …
    for _ in range(4):
        _lib.k1520_run(h, 36_864)
    assert _lib.k1520_term_scancode_key(h, 0, 0x3A, False)           # … und loslassen
    for _ in range(4):
        _lib.k1520_run(h, 36_864)
    assert _lib.k1520_term_leds(h, 0) & 2 and _lib.k1520_keyboard_leds(h) & 2
    assert not _lib.k1520_term_scancode_key(h, 0, 0x7777, True)
    assert not _lib.k1520_term_matrix_key(h, 0, 8, 0, True)
    pfad = str(tmp_path / "term.p8tm").encode()
    assert _lib.k1520_state_save(h, pfad)
    h2 = _lib.k1520_create_p8000_terminal(None)
    assert _lib.k1520_state_load(h2, pfad), _lib.k1520_state_error(h2)
    _lib.k1520_term_text(h2, 0, 0, text, 96)
    assert text.value.startswith(b"ADM31/9600 baud")
    h3 = _lib.k1520_create_p8000_terminal(b"teiler=7")
    assert not _lib.k1520_state_load(h3, pfad) and _lib.k1520_state_error(h3)
    for x in (h, h2, h3):
        _lib.k1520_destroy(K1520Handle(x))
    for schlecht in (b"firmware=6.0", b"zeichensatz=x", b"teiler=9", b"art=x", b"rolle=x", b"port=70000",
                     b"verbinden=2", b"quatsch=1"):
        assert not _lib.k1520_create_p8000_terminal(schlecht), schlecht
        assert _lib.k1520_last_init_error().decode().startswith("P8000-Terminal"), schlecht

    # Am P8000: terminal=original — tty1, Art 1, Pixelbild; Kern-Terminal: Art 0, kein Pixelbild
    h = _lib.k1520_create_p8000(b"terminal=original,teiler=8,zeichensatz=ezs-dzs,vorlauf=500")
    assert h, _lib.k1520_last_init_error()
    assert _lib.k1520_term_tty(h, 0) == 1 and _lib.k1520_term_kind(h, 0) == 1
    _lib.k1520_destroy(K1520Handle(h))
    h = _lib.k1520_create_p8000(None)
    assert _lib.k1520_term_kind(h, 0) == 0 and _lib.k1520_term_leds(h, 0) == -1
    assert _lib.k1520_term_framebuffer(h, 0, None, 0, None, None) == 0
    assert not _lib.k1520_term_matrix_key(h, 0, 0, 0, True)
    _lib.k1520_destroy(K1520Handle(h))
    for schlecht in (b"terminal=x", b"vorlauf=abc", b"firmware=6.0"):
        assert not _lib.k1520_create_p8000(schlecht), schlecht


def test_p8000_can_be_created_configured_and_refused():
    """`k1520_create_p8000` (AP P7b): Typ 4, Terminal-Funktionen, Konfigurationstext;
    unbekannte/noch nicht gebaute Schlüssel und unzulässige Bestückung → NULL mit Grund
    (16-Bit-Teil seit AP P11)."""
    import ctypes
    from app.core_binding.k1520 import _lib, K1520Handle

    for konfig in (None, b"", b"index8=1,mon8=3.1,lw0=K5601,lw1=none,terminals=1,karte16=0",
                   b"karte16=1,index16=4,mon16=3.1,dram=1M@0",
                   b"index8=1,index16=1,karte16=1,mon16=3.0,dram=256K@0+256K@1",
                   b"karte16=1,dram=4x256K", b"karte16=1,dram=4x1M", b"karte16=1,dram=16M",
                   b"karte16=1,dram=2M@0+1M@2"):
        handle = _lib.k1520_create_p8000(konfig)
        assert handle, _lib.k1520_last_init_error()
        assert _lib.k1520_machine_type(handle) == 4
        assert _lib.k1520_term_count(handle) == 1
        assert _lib.k1520_term_tty(handle, 0) == 1 and _lib.k1520_term_tty(handle, 1) == -1
        assert _lib.k1520_term_mode(handle, 0) == 0          # ADM31 nach dem Einschalten
        col, row = ctypes.c_int(), ctypes.c_int()
        assert _lib.k1520_term_cursor(handle, 0, ctypes.byref(col), ctypes.byref(row))
        assert not _lib.k1520_term_key(handle, 1, ord("a"), False, False)
        assert _lib.k1520_term_char(handle, 0, 80, 0) == 0
        _lib.k1520_power_on(handle)
        assert _lib.k1520_run(handle, 100_000) > 0
        assert not _lib.k1520_raf_install(handle, b"raf512")      # kein K1520-Steckplatz
        _lib.k1520_destroy(K1520Handle(handle))

    # k1520_create(4) = Vorgabekonfiguration
    handle = _lib.k1520_create(4)
    assert handle and _lib.k1520_machine_type(handle) == 4
    _lib.k1520_destroy(K1520Handle(handle))

    for schlecht in (b"quatsch=1", b"index8=2", b"mon8=9", b"index16=2", b"dram=2M@1",
                     b"dram=5x256K", b"dram=32M", b"karte16=1,dram=16M+1M@1", b"karte16=1,dram=8M@0+1M@7",
                     b"dram=1M@16", b"mon16=9", b"karte16=2", b"karte16=1,index8=1",
                     b"karte16=1,dram=1M@0+1M@0", b"wdc=4.2", b"terminals=2", b"lw0", b"lw5=K5601",
                     b"karte16=1,wdc=9", b"plattentyp=XY", b"karte16=1,wdc=4.2,platte=/gibt/es/nicht.img",
                     b"platte=/tmp/x.img", b"karte16=1,wdc=4.2,plattepar=vielleicht",
                     b"karte16=1,wdc=3.4.05,plattentyp=D5126"):
        assert not _lib.k1520_create_p8000(schlecht), schlecht
        assert _lib.k1520_last_init_error().decode().startswith("P8000"), schlecht

    # WDC (AP P13d): Platte anlegen, anschließen, lösen; ohne WDC bzw. an anderen Maschinen Ruhewerte
    import os
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        pfad = os.path.join(d, "platte.img")
        handle = _lib.k1520_create_p8000(b"karte16=1,wdc=4.2")
        assert handle, _lib.k1520_last_init_error()
        assert _lib.k1520_hd_path(handle, 0) == b""
        assert _lib.k1520_hd_create(handle, 0, pfad.encode(), b"D5126"), _lib.k1520_hd_error(handle)
        assert os.path.getsize(pfad) == 615 * 4 * 18 * 512
        assert _lib.k1520_hd_path(handle, 0) == pfad.encode()
        assert not _lib.k1520_hd_mount(handle, 1, pfad.encode(), True)   # kein Schreibschutz
        assert _lib.k1520_hd_error(handle)
        assert not _lib.k1520_hd_create(handle, 0, pfad.encode(), b"quatsch")
        # Suffix ":unformatiert": Laufwerk wie neu, Z0/K0/S1 = E5 (kein Parametersatz)
        roh = os.path.join(d, "roh.img")
        assert _lib.k1520_hd_create(handle, 0, roh.encode(), b"D5126:unformatiert"), _lib.k1520_hd_error(handle)
        with open(roh, "rb") as f:
            assert f.read(512) == b"\xe5" * 512
        assert _lib.k1520_hd_create(handle, 0, pfad.encode(), b"D5126")
        _lib.k1520_power_on(handle)
        assert _lib.k1520_run(handle, 100_000) > 0
        assert not _lib.k1520_hd_led(handle, 0)                          # WDC noch im Reset
        assert _lib.k1520_hd_flush(handle)
        assert _lib.k1520_hd_unmount(handle, 0) and _lib.k1520_hd_path(handle, 0) == b""
        assert not _lib.k1520_hd_unmount(handle, 0)
        _lib.k1520_destroy(K1520Handle(handle))
        handle = _lib.k1520_create_p8000(f"karte16=1,wdc=4.2,platte={pfad}".encode())
        assert handle and _lib.k1520_hd_path(handle, 0) == pfad.encode()
        _lib.k1520_destroy(K1520Handle(handle))
    # P24: das EPROM der Firmware bis 4.0 legt den Typ fest; Kürzel; plattepar=aus
    assert _lib.k1520_hd_rom_typ(b"3.4.05") == b"K5504.50" and _lib.k1520_hd_rom_typ(b"4.0.05") == b"K5504.50"
    assert _lib.k1520_hd_rom_typ(b"4.2") == b"" and _lib.k1520_hd_rom_typ(None) == b""
    assert _lib.k1520_hd_typ_kuerzel(b"K5504.50") == b"k5504" and _lib.k1520_hd_typ_kuerzel(b"VS") == b"vs"
    assert _lib.k1520_hd_typ_kuerzel(b"quatsch") == b""
    with tempfile.TemporaryDirectory() as d:
        klein = os.path.join(d, "klein.img")
        handle = _lib.k1520_create_p8000(b"karte16=1,wdc=3.4.05,plattepar=aus")
        assert handle, _lib.k1520_last_init_error()
        assert not _lib.k1520_hd_create(handle, 0, klein.encode(), b"D5126")
        assert "Firmware 3.4.05 gehört zu K5504.50" in _lib.k1520_hd_error(handle).decode()
        assert _lib.k1520_hd_create(handle, 0, klein.encode(), b"")   # leer = Typ des ROMs
        assert os.path.getsize(klein) == 1024 * 5 * 18 * 512
        _lib.k1520_destroy(K1520Handle(handle))
    handle = _lib.k1520_create_p8000(b"karte16=1")
    assert not _lib.k1520_hd_create(handle, 0, b"/tmp/egal.img", None)
    assert _lib.k1520_hd_error(handle) == "kein WDC".encode()
    _lib.k1520_destroy(K1520Handle(handle))

    # Terminal-API an einer anderen Maschine: Ruhewerte
    a5120 = _lib.k1520_create(0)
    assert not _lib.k1520_hd_mount(a5120, 0, b"/tmp/x.img", False) and _lib.k1520_hd_path(a5120, 0) == b""
    assert not _lib.k1520_hd_flush(a5120) and not _lib.k1520_hd_led(a5120, 0)
    assert _lib.k1520_term_count(a5120) == 0 and _lib.k1520_term_tty(a5120, 0) == -1
    assert _lib.k1520_term_mode(a5120, 0) == -1
    _lib.k1520_destroy(K1520Handle(a5120))


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


def test_pc1715_ex_waehlt_rom_fassungen():
    """`k1520_create_pc1715_ex` (AP-6): ZG-Bestückung und Tastatur-ROM wählbar; mit 0/0 gleich der
    alten Funktion; unbekannte Werte → NULL mit Grund; der 1715W nimmt die Tastatur, ignoriert den ZG-Satz."""
    from app.core_binding.k1520 import _lib, K1520Handle

    for variante, zg_satz, zg_db6, tastatur in ((0, 0, 0, 0), (0, 1, 0, 0), (0, 2, 1, 1),
                                                (0, 0, 0, 1), (1, 0, 0, 1)):
        handle = _lib.k1520_create_pc1715_ex(variante, 0, zg_satz, zg_db6, tastatur,
                                             None, None, None, None)
        assert handle, _lib.k1520_last_init_error()
        h = K1520Handle(handle)
        try:
            assert _lib.k1520_machine_type(h) == 3
            _lib.k1520_power_on(h)
            assert _lib.k1520_run(h, 50_000) > 0
        finally:
            _lib.k1520_destroy(h)
    assert not _lib.k1520_create_pc1715_ex(0, 0, 3, 0, 0, None, None, None, None)
    assert "Zeichengenerator" in _lib.k1520_last_init_error().decode()
    assert not _lib.k1520_create_pc1715_ex(0, 0, 0, 0, 2, None, None, None, None)
    assert "Tastatur" in _lib.k1520_last_init_error().decode()
    assert not _lib.k1520_create_pc1715_ex(0, 0, 0, 2, 0, None, None, None, None)


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
