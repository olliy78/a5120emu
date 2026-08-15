"""Physische Diskette (Greaseweazle) — **ohne Hardware**.

Das Laufwerk hängt nicht immer am Rechner, in der CI/CD-Kette nie.  Der Entwurf ist
deshalb so geschnitten, dass die Hardware nur in der äußersten Schale vorkommt: alles
unterhalb von „Aufträge und Bitzellen" ist ohne Adapter prüfbar
(doc/design/14_physische_diskette.md §14).

Hier steht deshalb ein **Ersatzlaufwerk über einer ``.hfe``-Datei**.  Es liefert
dieselben Bitzellen, die der Adapter liefern würde — denn eine HFE-Aufnahme *ist*
genau das, samt Phasenversatz und Jitter der echten Aufnahme.  Benutzt wird
``udos_boot_scp.hfe``: ein echter Greaseweazle-Mitschnitt einer beidseitigen
UDOS-Diskette.

``greaseweazle`` wird hier **nicht** importiert; das Paket ist freiwillig.
"""

from __future__ import annotations

import threading
import time

import pytest

from app.core_binding.k1520disk import DiskTool
from app.gw import JobKind, Priority, Sync, TrackWorker
from gw_fake import HfeDevice

FIXTURE = "udos_boot_scp.hfe"


# Das Ersatzlaufwerk (HfeDevice) steht in gw_fake.py — es wird auch von den
# Oberflächentests gebraucht.

@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


# ════════════════════════════════════════════════════════════════════════════
# Die C-ABI für sich
# ════════════════════════════════════════════════════════════════════════════


def test_frisch_angelegte_diskette_ist_vollstaendig_unbekannt():
    with Sync(num_cyls=8, num_heads=2, read_ahead=False) as s:
        st = s.stats
        assert st.tracks_total == 16
        assert st.tracks_known == 0
        assert st.tracks_dirty == 0
        assert not st.busy


def test_ohne_vorauslesen_gibt_es_nichts_zu_tun():
    with Sync(num_cyls=4, num_heads=1, read_ahead=False) as s:
        assert s.take_job(50) is None


def test_vorauslesen_stellt_von_selbst_auftraege_ein():
    with Sync(num_cyls=4, num_heads=1, read_ahead=True) as s:
        auftrag = s.take_job(500)
        assert auftrag is not None
        assert auftrag.kind is JobKind.READ
        assert auftrag.prio is Priority.READAHEAD


def test_shutdown_beendet_den_arbeitsfaden():
    s = Sync(num_cyls=4, num_heads=1, read_ahead=False)
    s.shutdown()
    auftrag = s.take_job(500)
    assert auftrag is not None and auftrag.kind is JobKind.STOP
    s.close()


def test_ein_zweiter_arbeitsfaden_wird_abgewiesen():
    with Sync(num_cyls=4, num_heads=1, read_ahead=False) as s:
        drin = threading.Event()

        def erster():
            drin.set()
            s.take_job(400)

        t = threading.Thread(target=erster)
        t.start()
        drin.wait(1.0)
        time.sleep(0.05)
        assert s.take_job(50) is None      # abgewiesen, nicht bedient
        t.join()
        assert "zweiter" in s.last_error


# ════════════════════════════════════════════════════════════════════════════
# Arbeitsfaden + Kern
# ════════════════════════════════════════════════════════════════════════════


def test_arbeitsfaden_holt_nur_die_gebrauchten_spuren(hfe):
    """Der Kern des Auftrags: kein Vollabzug, sondern Spuren auf Anforderung."""
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=False) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            time.sleep(0.3)
            assert geraet.gelesen == [], "es wurde ungefragt gelesen"
            assert s.stats.tracks_known == 0
        finally:
            worker.stop()
    assert worker.errors == 0


def test_vorauslesen_fuellt_das_abbild(hfe):
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            ende = time.monotonic() + 20
            while time.monotonic() < ende and s.stats.tracks_known < s.stats.tracks_total:
                time.sleep(0.05)
            st = s.stats
            assert st.tracks_known == st.tracks_total, f"nur {st.tracks_known} gelesen"
            assert st.errors == 0
        finally:
            worker.stop()
    # Jede Spur genau EINMAL — ein Auftrag je Spur (§5.2).
    assert len(geraet.gelesen) == len(set(geraet.gelesen))


def test_disktool_oeffnet_die_physische_diskette_wie_eine_datei(hfe):
    """Die Nagelprobe: dieselbe Diskette einmal als Datei, einmal „physisch".

    Erkennung, Dateisystem und Verzeichnis müssen übereinstimmen — der einzige
    Unterschied ist, dass die Spuren einzeln geholt werden.
    """
    with DiskTool.open(hfe) as datei:
        erwartet_fs = datei.filesystem
        erwartet_baende = datei.volume_count
        erwartet_namen = sorted(e.side_prefix + e.name for e in datei.list())

    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            with DiskTool.open_physical(s) as physisch:
                assert physisch.filesystem == erwartet_fs
                assert physisch.volume_count == erwartet_baende
                namen = sorted(e.side_prefix + e.name for e in physisch.list())
                assert namen == erwartet_namen
        finally:
            worker.stop()
    assert worker.errors == 0


def test_datei_aus_der_physischen_diskette_ist_byteweise_gleich(hfe, tmp_path):
    """Eine Datei holen — über den Umweg Bitzellen muss dasselbe herauskommen."""
    aus_datei = tmp_path / "aus_datei.bin"
    with DiskTool.open(hfe) as datei:
        eintraege = [e for e in datei.list() if e.size > 0]
        assert eintraege, "Fixture ohne Dateien"
        name = eintraege[0].side_prefix + eintraege[0].name
        datei.extract(name, aus_datei)

    aus_laufwerk = tmp_path / "aus_laufwerk.bin"
    geraet = HfeDevice(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            with DiskTool.open_physical(s) as physisch:
                physisch.extract(name, aus_laufwerk)
        finally:
            worker.stop()
    assert aus_laufwerk.read_bytes() == aus_datei.read_bytes()


def test_gescheitertes_lesen_bringt_niemanden_zum_haengen(hfe):
    """Ein Laufwerksfehler darf den Vordergrund nicht bis zur Frist warten lassen."""

    class KaputtesLaufwerk(HfeDevice):
        def read_track(self, cyl, head):
            raise OSError("Kabel ab")

    geraet = KaputtesLaufwerk(hfe)
    with Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
              read_ahead=True, request_timeout_ms=30000) as s:
        worker = TrackWorker(s, geraet, poll_ms=50)
        worker.start()
        try:
            ende = time.monotonic() + 10
            while time.monotonic() < ende and s.stats.tracks_failed < 4:
                time.sleep(0.05)
            assert s.stats.tracks_failed >= 4
            assert s.stats.tracks_known == 0
            assert worker.errors >= 4
            assert "Kabel ab" in worker.last_error
        finally:
            worker.stop()
