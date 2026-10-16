"""Kassette (K5020/K5200) ueber die C-ABI — Entwurf 29, AP K5.

Die Karte, die Laufwerke und das Band pruefen die C++-Waechter (`K5020Test.*`, `Kassette*`,
`K5020Maschine.*`); hier geht es um den Weg durch `k1520_tape_*` und die ctypes-Bindung:
Stecken je Maschine, Fehlertext, Einlegen/Binden/Auswerfen/Wenden/Schreibschutz, Status,
Blockliste, und „ohne Karte nichts“.  Beschrieben wird ohne Gast nicht — die Kassette
entsteht hier aus einer vorhandenen `.k52`, die der Kern liest.
"""

import pytest

from conftest import requires_core

pytestmark = requires_core

# Maschine → Basis der Karte (Entwurf 29 §2)
MASCHINEN = [("a5120", 0x30), ("k8915", 0xD0), ("prg710", 0x30)]


def _emu(machine, **kw):
    from app.core_binding.k1520 import K1520Emulator
    return K1520Emulator(machine=machine, **kw)


@pytest.mark.parametrize("machine,basis", MASCHINEN)
def test_karte_steckt_in_jeder_maschine(machine, basis):
    e = _emu(machine, tape=True)
    assert e.tape_installed()
    assert e.tape_error() == ""
    st = e.tape_status(0)
    assert st is not None
    assert st["loaded"] is False and st["blocks"] == 0 and st["file"] == ""


@pytest.mark.parametrize("machine,basis", MASCHINEN)
def test_ohne_karte_nichts(machine, basis):
    e = _emu(machine)
    assert not e.tape_installed()
    assert e.tape_status(0) is None
    assert e.tape_load(0, "/tmp/gibt-es-nicht.k52") is False
    assert "keine Kassettenkarte" in e.tape_error()
    assert e.tape_blocks(0, 0) == []
    assert e.tape_block_data(0, 0, 0) is None


def test_zu_spaet_und_doppelt_setzen_den_fehlertext():
    e = _emu("a5120", tape=True)
    from app.core_binding import k1520 as b
    assert b._lib.k1520_tape_install(e._handle) is False     # doppelt
    assert "bereits" in (b._lib.k1520_last_init_error() or b"").decode()
    e2 = _emu("a5120")
    e2.run(1000)
    assert b._lib.k1520_tape_install(e2._handle) is False    # nach dem ersten Lauf
    assert "vor dem ersten" in (b._lib.k1520_last_init_error() or b"").decode()
    assert not e2.tape_installed()


def test_pc1715_steckt_auf_der_ausweichbasis():
    # 30H–37H liegt am PC 1715 unter LT107/BWS; die Karte steckt dort auf D0H (nur mechanisch).
    e = _emu("pc1715", tape=True)
    assert e.tape_installed()


def test_einlegen_binden_und_auswerfen(tmp_path):
    e = _emu("a5120", tape=True)
    k52 = tmp_path / "band.k52"
    assert e.tape_load(0, k52) is True            # fehlende Datei = leere Kassette, sofort angelegt
    assert k52.exists()
    st = e.tape_status(0)
    assert st["loaded"] and st["side"] == 0 and st["file"] == str(k52)
    assert st["len_um"] > 80_000_000 and st["blocks"] == 0
    assert e.tape_load(0, k52) is False           # Laufwerk schon belegt
    assert "Kassette" in e.tape_error()
    assert e.tape_block_data(0, 0, 0) is None
    assert e.tape_blocks(0, 0) == []
    assert e.tape_eject(0) is True
    assert e.tape_status(0)["loaded"] is False
    assert e.tape_eject(0) is False
    assert "keine Kassette" in e.tape_error()


def test_zweites_geraet_ist_unabhaengig(tmp_path):
    e = _emu("k8915", tape=True)
    assert e.tape_new(1) is True
    assert e.tape_status(1)["loaded"] is True and e.tape_status(0)["loaded"] is False
    assert e.tape_load(2, tmp_path / "x.k52") is False      # Geraet 0/1
    assert "0 oder 1" in e.tape_error()


def test_wenden_und_schreibschutz():
    e = _emu("prg710", tape=True)
    assert e.tape_protected(0, 0) is None          # keine Kassette
    assert e.tape_new(0)
    assert e.tape_protected(0, 0) is False and e.tape_protected(0, 1) is False
    assert e.tape_protect(0, 1, True)
    assert e.tape_protected(0, 1) is True and e.tape_protected(0, 0) is False
    assert e.tape_protect(0, 2, True) is False
    assert e.tape_flip(0) is True
    assert e.tape_status(0)["side"] == 1
    assert e.tape_flip(0) is True
    assert e.tape_status(0)["side"] == 0


def test_kopfstelle_setzen_nur_mit_kassette():
    e = _emu("a5120", tape=True)
    assert e.tape_set_position(0, 1_000_000) is False
    assert e.tape_new(0)
    assert e.tape_set_position(0, 30_000_000) is True
    assert abs(e.tape_status(0)["pos_um"] - 30_000_000) < 10


def test_datei_bleibt_beim_lesen_unveraendert(tmp_path):
    k52 = tmp_path / "b.k52"
    e = _emu("a5120", tape=True)
    assert e.tape_load(0, k52)
    vorher = k52.read_bytes()
    e.run(10_000)
    assert e.tape_flush(0) is True
    assert k52.read_bytes() == vorher
    assert vorher[:3] == b"K52" or len(vorher) > 16   # Kopf mit Kennung (Format: K3)


def test_kaputte_datei_wird_mit_grund_abgelehnt(tmp_path):
    kaputt = tmp_path / "kaputt.k52"
    kaputt.write_bytes(b"das ist kein Band")
    e = _emu("a5120", tape=True)
    assert e.tape_load(0, kaputt) is False
    assert e.tape_error() != ""
    assert e.tape_status(0)["loaded"] is False


def test_header_funktionen_stimmen_mit_dem_kern_ueberein():
    """Alle `k1520_tape_*` des Headers sind exportiert und tragen ctypes-Signaturen
    (die Gesamtpruefung steht in test_c_api.py; hier nur die Kassettengruppe)."""
    import re
    from conftest import PROJECT_ROOT
    from app.core_binding import k1520 as b
    text = (PROJECT_ROOT / "core" / "api" / "k1520_api.h").read_text("utf-8")
    namen = set(re.findall(r"\b(k1520_tape_\w+)\s*\(", text))
    assert len(namen) >= 17
    for n in namen:
        fn = getattr(b._lib, n)
        assert fn.restype is not None or n.endswith("_install"), n
