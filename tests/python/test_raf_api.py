"""RAM-Floppy RAF über C-ABI und Python-Bindung (AP-R3, doc/design/22_raf512.md §6)."""

import pytest

from conftest import requires_core

pytestmark = requires_core

MASCHINEN = ["a5120", "k8915", "prg710", "prg710-1"]
GROESSE = {"raf128": 128 * 1024, "raf512": 512 * 1024, "raf2m": 2048 * 1024}


def _emu(**kw):
    from app.core_binding.k1520 import K1520Emulator
    return K1520Emulator(**kw)


@pytest.mark.parametrize("maschine", MASCHINEN)
@pytest.mark.parametrize("typ", list(GROESSE))
def test_anlegen_je_maschine_und_typ(maschine, typ):
    emu = _emu(machine=maschine, raf=typ)
    assert emu.raf_variant == typ
    emu.power_on()
    assert emu.raf_peek(0) in range(256)
    assert emu.raf_peek(GROESSE[typ]) == 0xFF   # hinter der Kapazität


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_ohne_raf_nichts(maschine, tmp_path):
    for raf in (None, "none"):
        emu = _emu(machine=maschine, raf=raf)
        assert emu.raf_variant == ""
        assert emu.raf_peek(0) == 0xFF
        assert emu.raf_load(tmp_path / "x.bin") is False
        assert emu.raf_save(tmp_path / "x.bin") is False


def test_a5120_mit_em_und_raf():
    emu = _emu(machine="a5120", em="em256", raf="raf512")
    assert emu.em_variant() == "em256" and emu.raf_variant == "raf512"


def test_unbekannter_typ_ist_valueerror_mit_grund():
    with pytest.raises(ValueError, match="raf"):
        _emu(raf="raf999")


def test_install_nach_erstem_lauf_wird_abgelehnt():
    from app.core_binding.k1520 import _lib
    emu = _emu()
    emu.power_on()
    emu.run(1000)
    assert _lib.k1520_raf_install(emu._handle, b"raf512") is False
    assert b"vor dem ersten" in _lib.k1520_last_init_error()
    assert emu.raf_variant == ""


def test_install_none_ist_true_ohne_karte():
    from app.core_binding.k1520 import _lib
    emu = _emu()
    assert _lib.k1520_raf_install(emu._handle, None) is True
    assert _lib.k1520_raf_install(emu._handle, b"none") is True
    assert emu.raf_variant == ""


def test_zweite_raf_wird_abgelehnt():
    from app.core_binding.k1520 import _lib
    emu = _emu(raf="raf128")
    assert _lib.k1520_raf_install(emu._handle, b"raf512") is False
    assert emu.raf_variant == "raf128"


def test_load_save_rundreise_und_falsche_groesse(tmp_path):
    emu = _emu(raf="raf128")
    emu.power_on()
    daten = bytes((i * 7 + 3) & 0xFF for i in range(GROESSE["raf128"]))
    quelle = tmp_path / "in.bin"
    quelle.write_bytes(daten)
    assert emu.raf_load(quelle) is True
    assert emu.raf_peek(0) == daten[0] and emu.raf_peek(12345) == daten[12345]
    ziel = tmp_path / "out.bin"
    assert emu.raf_save(ziel) is True
    assert ziel.read_bytes() == daten

    falsch = tmp_path / "falsch.bin"
    falsch.write_bytes(b"\x00" * 1000)
    assert emu.raf_load(falsch) is False
    assert emu.raf_peek(12345) == daten[12345]   # unverändert
    assert emu.raf_load(tmp_path / "gibtsnicht.bin") is False
