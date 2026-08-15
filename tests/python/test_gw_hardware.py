"""Physische Diskette — **mit echter Hardware**.

Diese Tests laufen nur, wenn ein Greaseweazle am Rechner hängt und eine Diskette im
Laufwerk liegt.  Sie sind deshalb **standardmäßig übersprungen** und gehören nicht in
die Regression::

    K1520_GW_HARDWARE=1 venv/bin/python3 -m pytest tests/python/test_gw_hardware.py -v

Steuerung über Umgebungsvariablen:

===========================  ==================================================
``K1520_GW_HARDWARE=1``      überhaupt laufen lassen
``K1520_GW_DRIVE=a``         Laufwerk am Kabel (``a``/``b``, Shugart ``0``…``3``)
``K1520_GW_CYLS=80``         Spuren des Laufwerks (K5601 80, K5600.10 40)
``K1520_GW_HEADS=2``         Köpfe
``K1520_GW_RATE=250``        Zellrate in kbit/s (5,25″ DD 250, 8″ MFM 500)
``K1520_GW_WRITE=1``         **beschreibt die eingelegte Diskette** — siehe unten
===========================  ==================================================

**Geschrieben wird nur mit ``K1520_GW_WRITE=1``.**  Ein Schreibtest verändert eine
echte, meist einzige Diskette; welche das ist, entscheidet kein Test von sich aus
(doc/design/14_physische_diskette.md §14).
"""

from __future__ import annotations

import os
import time

import pytest

from app.gw import Sync, TrackWorker, open_device, verfuegbar

pytestmark = pytest.mark.skipif(
    os.environ.get("K1520_GW_HARDWARE") != "1",
    reason="Hardware-Test: braucht einen Greaseweazle mit eingelegter Diskette "
           "(K1520_GW_HARDWARE=1)")


def _umgebung(name: str, vorgabe: int) -> int:
    return int(os.environ.get(name, vorgabe))


@pytest.fixture(scope="module")
def geraet():
    if not verfuegbar():
        pytest.skip("Paket 'greaseweazle' ist nicht installiert")
    d = open_device(os.environ.get("K1520_GW_DRIVE", "a"),
                    cell_rate_kbps=_umgebung("K1520_GW_RATE", 250))
    yield d
    d.deselect()


@pytest.fixture
def sync():
    s = Sync(num_cyls=_umgebung("K1520_GW_CYLS", 80),
             num_heads=_umgebung("K1520_GW_HEADS", 2),
             cell_rate_kbps=_umgebung("K1520_GW_RATE", 250),
             writable=os.environ.get("K1520_GW_WRITE") == "1",
             read_ahead=False)
    yield s
    s.shutdown()
    s.close()


def test_adapter_meldet_sich():
    from app.gw import finde_adapter
    a = finde_adapter()
    assert a.model
    print(f"\nGreaseweazle {a.model}, Firmware {a.firmware}")


def test_eine_spur_lesen_dauert_unter_zwei_sekunden(geraet):
    t0 = time.monotonic()
    zellen, bitcells = geraet.read_track(0, 0)
    dauer = time.monotonic() - t0
    assert bitcells > 10000, "die Spur ist verdächtig kurz"
    assert len(zellen) == (bitcells + 7) // 8
    assert dauer < 2.0, f"{dauer:.2f} s je Spur ist zu langsam"
    print(f"\nSpur 0/0: {bitcells} Zellen in {dauer * 1000:.0f} ms")


def test_auftragsweg_liest_ueber_den_arbeitsfaden(geraet, sync):
    """Kern + Arbeitsfaden + echtes Laufwerk — der ganze Weg einmal durch."""
    worker = TrackWorker(sync, geraet, poll_ms=100)
    worker.start()
    try:
        sync.set_read_ahead(True)
        ende = time.monotonic() + 30
        while time.monotonic() < ende and sync.stats.tracks_known < 3:
            time.sleep(0.1)
        st = sync.stats
        assert st.tracks_known >= 3, f"nur {st.tracks_known} Spuren gelesen"
        assert st.errors == 0, sync.last_error
        assert worker.errors == 0, worker.last_error
    finally:
        worker.stop()


def test_disktool_oeffnet_die_eingelegte_diskette(geraet, sync):
    """Die eingelegte Diskette im k1520DiskTool öffnen und ihr Verzeichnis lesen.

    Braucht eine **lesbare** Diskette (CP/M, SCPX oder UDOS) im Laufwerk — das
    Öffnen liest sie dabei ganz (Formaterkennung, §11.2), dauert also gut eine
    Minute.
    """
    from app.core_binding.k1520disk import DiskTool

    worker = TrackWorker(sync, geraet, poll_ms=100)
    worker.start()
    try:
        with DiskTool.open_physical(sync) as d:
            print(f"\nerkannt: {d.format} / {d.filesystem}")
            for e in d.list():
                print(f"  {e.side_prefix}{e.name:16s} {e.size:7d}")
            assert d.filesystem
    finally:
        worker.stop()


def test_dieselbe_spur_zweimal_gelesen_gibt_dasselbe(geraet):
    """Ein Lesevorgang muss wiederholbar sein — sonst stimmt die Abtastung nicht."""
    a, na = geraet.read_track(1, 0)
    b, nb = geraet.read_track(1, 0)
    # Bitgleich sind zwei Aufnahmen nie (Jitter, Startwinkel); die Länge muss aber
    # auf ein Promille stimmen, sonst passt die Zellrate nicht zur Diskette.
    assert abs(na - nb) < na / 1000, f"{na} vs. {nb} Zellen"


@pytest.mark.skipif(os.environ.get("K1520_GW_WRITE") != "1",
                    reason="beschreibt eine echte Diskette (K1520_GW_WRITE=1)")
def test_geschriebene_spur_liest_sich_zurueck(geraet):
    cyl, head = _umgebung("K1520_GW_TESTCYL", 79), 0
    original, bits = geraet.read_track(cyl, head)
    geraet.write_track(cyl, head, original, bits)
    zurueck, bits2 = geraet.read_track(cyl, head)
    assert abs(bits - bits2) < bits / 100
