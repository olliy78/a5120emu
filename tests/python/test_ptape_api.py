"""Lochstreifen (K6022) über die C-ABI — Bandformate und Stanzer-Bindung
(doc/design/23_lochstreifen.md §6, AP-L3).

Die Formate selbst und das verzögerte Zurückschreiben prüfen die C++-Wächter
`Lochstreifenformat.*` und `K6022Bindung.*`; hier geht es um den Weg durch
`k1520_ptape_*` und die ctypes-Bindung: Kennungen, Erkennung, Leser mit Format,
Stanzer binden/neues Band/lösen/flush, Fehlertext, und „ohne Karte nichts“.
Gestanzt wird ohne Gast nicht — das Stanzband entsteht hier aus einer vorhandenen
Datei, die beim Binden der Anfang des Bandes wird (§5).
"""

import pytest

from conftest import requires_core

pytestmark = requires_core

MASCHINEN = ["a5120", "k8915", "prg710"]

# Intel HEX: 41 42 43 ab Adresse 0, 55 an Adresse 5 → Band 41 42 43 00 00 55 (Lücke = 00H).
IHEX = b":03000000414243 37\n:0100050055A5\n:00000001FF\n".replace(b" ", b"")
IHEX_BAND = bytes([0x41, 0x42, 0x43, 0x00, 0x00, 0x55])


def _sprosse(b: int) -> str:
    """Eine Datenzeile der ASCII-Art (Spur 8 links, Transportloch zwischen 3 und 4)."""
    loch = lambda bit: "O" if b >> bit & 1 else "."   # noqa: E731
    spuren = [loch(7), loch(6), loch(5), loch(4), loch(3), "o", loch(2), loch(1), loch(0)]
    zeichen = f"  {chr(b)}" if 0x21 <= b <= 0x7E else ""
    return f"   | {' '.join(spuren)} |  {b:02X}{zeichen}"


def _ascii_art(band: bytes) -> bytes:
    zeilen = ["; von Hand", ";    8 7 6 5 4   3 2 1"] + [_sprosse(b) for b in band]
    return ("\n".join(zeilen) + "\n").encode("ascii")


def test_formatkennungen_stimmen_mit_dem_header_ueberein():
    """Die Kennungen sind Teil der ABI — Header, Bindung und Kern zählen gleich."""
    import re
    from conftest import PROJECT_ROOT
    from app.core_binding import k1520 as b

    text = (PROJECT_ROOT / "core" / "api" / "k1520_api.h").read_text("utf-8")
    werte = dict(re.findall(r"#define (K1520_PTAPE_FMT_\w+)\s+(\d+)", text))
    assert werte == {"K1520_PTAPE_FMT_RAW": "0", "K1520_PTAPE_FMT_IHEX": "1",
                     "K1520_PTAPE_FMT_ASCII": "2"}
    assert (b.PTAPE_FMT_RAW, b.PTAPE_FMT_IHEX, b.PTAPE_FMT_ASCII) == (0, 1, 2)


def test_format_nach_endung():
    from app.core_binding.k1520 import (K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX,
                                        PTAPE_FMT_RAW)
    f = K1520Emulator.ptape_format_from_ext
    assert f("a.hex") == f("B.IHX") == PTAPE_FMT_IHEX
    assert f("a.txt") == f("a.tape") == PTAPE_FMT_ASCII
    assert f("a.ptp") == f("a.bin") == f("ohne") == f("a.xyz") == PTAPE_FMT_RAW


def test_format_aus_dem_inhalt(tmp_path):
    from app.core_binding.k1520 import (K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX,
                                        PTAPE_FMT_RAW)
    f = K1520Emulator.ptape_detect_format
    # Absichtlich irreführende Endungen: erkannt wird am Inhalt.
    (tmp_path / "x.txt").write_bytes(IHEX)
    (tmp_path / "y.hex").write_bytes(_ascii_art(b"AB"))
    (tmp_path / "z.tape").write_bytes(b"\x00\x41\xff")
    (tmp_path / "leer.bin").write_bytes(b"")
    assert f(tmp_path / "x.txt") == PTAPE_FMT_IHEX
    assert f(tmp_path / "y.hex") == PTAPE_FMT_ASCII
    assert f(tmp_path / "z.tape") == PTAPE_FMT_RAW
    assert f(tmp_path / "leer.bin") == PTAPE_FMT_RAW
    assert f(tmp_path / "fehlt.ptp") is None


@pytest.mark.parametrize("name", MASCHINEN)
def test_ohne_karte_nichts(name, tmp_path):
    from app.core_binding.k1520 import K1520Emulator, PTAPE_FMT_IHEX

    emu = K1520Emulator(machine=name)
    datei = tmp_path / "band.hex"
    datei.write_bytes(IHEX)
    assert not emu.ptape_installed()
    assert not emu.ptape_load(datei, PTAPE_FMT_IHEX)
    assert "K6022" in emu.ptape_error()
    assert emu.ptape_reader_status() is None
    assert emu.ptape_punch_status() is None
    assert not emu.ptape_punch_bind(tmp_path / "st.hex", PTAPE_FMT_IHEX)
    assert not (tmp_path / "st.hex").exists()
    assert not emu.ptape_punch_unbind()
    assert not emu.ptape_punch_new_tape()
    assert not emu.ptape_punch_flush()
    from app.core_binding.k1520 import _lib
    assert _lib.k1520_ptape_reader_format(emu._handle) == -1
    assert _lib.k1520_ptape_punch_format(emu._handle) == -1
    assert _lib.k1520_ptape_reader_file(emu._handle) == b""
    assert _lib.k1520_ptape_punch_file(emu._handle) == b""


@pytest.mark.parametrize("name", MASCHINEN)
def test_leser_laedt_jedes_format(name, tmp_path):
    from app.core_binding.k1520 import (K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX,
                                        PTAPE_FMT_RAW)

    emu = K1520Emulator(machine=name, ptape=True)
    assert emu.ptape_installed()
    st = emu.ptape_reader_status()
    assert st["inserted"] is False and st["file"] == ""

    hexdatei = tmp_path / "band ä.hex"      # Umlaut: UTF-8-Pfad bis in den Kern und zurück
    hexdatei.write_bytes(IHEX)
    assert emu.ptape_load(hexdatei, PTAPE_FMT_IHEX), emu.ptape_error()
    assert emu.ptape_error() == ""
    st = emu.ptape_reader_status()
    assert st == {"inserted": True, "pos": 0, "len": len(IHEX_BAND), "at_end": False,
                  "file": str(hexdatei), "format": PTAPE_FMT_IHEX}

    art = tmp_path / "band.tape"
    art.write_bytes(_ascii_art(b"A\x93\x00Z"))
    assert emu.ptape_load(art, PTAPE_FMT_ASCII), emu.ptape_error()
    st = emu.ptape_reader_status()
    assert (st["len"], st["file"], st["format"]) == (4, str(art), PTAPE_FMT_ASCII)

    # Alte Funktion ohne Format = Roh: dieselbe Datei als Bytes.
    assert emu.ptape_load(art)
    st = emu.ptape_reader_status()
    assert (st["len"], st["format"]) == (art.stat().st_size, PTAPE_FMT_RAW)

    assert emu.ptape_eject()
    st = emu.ptape_reader_status()
    assert st["inserted"] is False and st["file"] == ""


def test_leser_meldet_den_fehler_mit_zeile(tmp_path):
    from app.core_binding.k1520 import K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX

    emu = K1520Emulator(machine="a5120", ptape=True)
    kaputt = tmp_path / "kaputt.hex"
    kaputt.write_bytes(b":0300000041424338\n:00000001FF\n")     # Prüfsumme falsch
    assert not emu.ptape_load(kaputt, PTAPE_FMT_IHEX)
    assert "Zeile 1" in emu.ptape_error()
    assert emu.ptape_reader_status()["inserted"] is False
    assert not emu.ptape_load(tmp_path / "fehlt.hex", PTAPE_FMT_IHEX)
    assert emu.ptape_error() != ""
    assert not emu.ptape_load(kaputt, 7)                         # unbekanntes Format
    assert "7" in emu.ptape_error()
    # Erfolg leert den Text wieder.
    gut = tmp_path / "gut.txt"
    gut.write_bytes(_ascii_art(b"Q"))
    assert emu.ptape_load(gut, PTAPE_FMT_ASCII) and emu.ptape_error() == ""


@pytest.mark.parametrize("name", MASCHINEN)
def test_stanzer_binden_neues_band_loesen(name, tmp_path):
    from app.core_binding.k1520 import (K1520Emulator, PTAPE_FMT_ASCII, PTAPE_FMT_IHEX,
                                        PTAPE_FMT_RAW)

    emu = K1520Emulator(machine=name, ptape=True)
    assert emu.ptape_punch_status() == {"file": "", "format": PTAPE_FMT_RAW, "len": 0,
                                        "enabled": True}

    # Vorhandene Datei = Anfang des Bandes.
    hexdatei = tmp_path / "stanz.hex"
    hexdatei.write_bytes(IHEX)
    assert emu.ptape_punch_bind(hexdatei, PTAPE_FMT_IHEX), emu.ptape_error()
    assert emu.ptape_punch_status() == {"file": str(hexdatei), "format": PTAPE_FMT_IHEX,
                                        "len": len(IHEX_BAND), "enabled": True}
    assert emu.ptape_punch_flush()                 # nichts geändert → nichts zu tun
    assert hexdatei.read_bytes() == IHEX
    # Neues Band: Datei SOFORT leer — im Format Intel HEX ist das nur der Endsatz.
    assert emu.ptape_punch_new_tape()
    assert emu.ptape_punch_length() == 0
    assert hexdatei.read_bytes().strip() == b":00000001FF"
    assert emu.ptape_punch_unbind()
    assert emu.ptape_punch_status()["file"] == ""

    # Wechsel auf eine ASCII-Art-Datei mit zwei Sprossen.
    art = tmp_path / "stanz ä.tape"
    art.write_bytes(_ascii_art(b"A\x93"))
    assert emu.ptape_punch_bind(art, PTAPE_FMT_ASCII), emu.ptape_error()
    st = emu.ptape_punch_status()
    assert (st["file"], st["format"], st["len"]) == (str(art), PTAPE_FMT_ASCII, 2)
    assert emu.ptape_punch_clear()                 # alter Name = neues Band
    leer = art.read_text("ascii")           # nur noch Kopf und Bandkanten (Kommentare)
    assert not [z for z in leer.splitlines() if z.strip() and not z.startswith(";")]
    # Gelöst bleibt das (leere) Band im Speicher, die Datei bleibt wie sie ist.
    assert emu.ptape_punch_unbind()
    assert emu.ptape_punch_status()["len"] == 0

    # Fehlende Datei wird leer angelegt — Roh ist dann wirklich 0 Byte.
    neu = tmp_path / "neu.ptp"
    assert emu.ptape_punch_bind(neu, PTAPE_FMT_RAW), emu.ptape_error()
    assert neu.exists() and neu.read_bytes() == b""
    assert emu.ptape_punch_status()["len"] == 0
    # Wer gebunden ist, verliert beim Wechsel die alte Bindung nur bei Erfolg.
    kaputt = tmp_path / "kaputt.hex"
    kaputt.write_bytes(b":zz\n")
    assert not emu.ptape_punch_bind(kaputt, PTAPE_FMT_IHEX)
    assert "Zeile 1" in emu.ptape_error()
    assert emu.ptape_punch_status()["file"] == str(neu)
    assert not emu.ptape_punch_bind(neu, 9)
    assert emu.ptape_punch_status()["file"] == str(neu)


def test_doppeltes_stecken_setzt_den_fehlertext():
    from app.core_binding.k1520 import K1520Emulator, _lib

    emu = K1520Emulator(machine="k8915", ptape=True)
    assert not _lib.k1520_ptape_install(emu._handle)
    assert "bereits" in emu.ptape_error()
