"""Oberflächen der physischen Diskette — Emulator und k1520DiskTool, **ohne Hardware**.

Geprüft wird die Verdrahtung, nicht das Aussehen: dass der Weg „Knopf → Sitzung →
angemeldete Diskette → Anzeige → sauberes Abmelden" in beiden Programmen wirklich
zusammenhängt.  Das echte Laufwerk vertritt dabei das Ersatzlaufwerk aus
:mod:`gw_fake` (eine ``.hfe``-Aufnahme).

Siehe doc/design/14_physische_diskette.md §12.
"""

from __future__ import annotations

import time

import pytest

from gw_fake import fake_session

pytest.importorskip("PySide6")

FIXTURE = "udos_boot_scp.hfe"


@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


@pytest.fixture
def app():
    from PySide6.QtWidgets import QApplication
    a = QApplication.instance() or QApplication([])
    yield a


def _warte(pred, frist=30.0):
    ende = time.monotonic() + frist
    while time.monotonic() < ende and not pred():
        time.sleep(0.05)
    return pred()


# ════════════════════════════════════════════════════════════════════════════
# Gemeinsames Stück: Auswahl und Sitzung
# ════════════════════════════════════════════════════════════════════════════


def test_auswahl_liefert_genau_die_argumente_der_sitzung(app):
    """Was der Dialog ausspuckt, muss ``PhysicalSession.start`` schlucken."""
    import inspect

    from app.ui.physical_disk import PhysicalDiskDialog, PhysicalSession

    d = PhysicalDiskDialog(num_cyls=40, num_heads=1, drive_label="K5600.10")
    wahl = d.auswahl()
    erlaubt = set(inspect.signature(PhysicalSession.start).parameters) - {"cls"}
    assert set(wahl) <= erlaubt, f"unbekannte Argumente: {set(wahl) - erlaubt}"
    assert wahl["num_cyls"] == 40 and wahl["num_heads"] == 1
    assert wahl["writable"] is False, "Schreiben darf nicht die Vorgabe sein"


def test_ohne_paket_bleibt_es_bei_einem_satz(app, monkeypatch):
    """Fehlt ``greaseweazle``, gibt es einen Grund — keinen Absturz."""
    import app.gw as gw
    from app.ui import physical_disk

    monkeypatch.setattr(gw, "verfuegbar", lambda: False)
    ok, grund = physical_disk.verfuegbarkeit()
    assert ok is False
    assert "greaseweazle" in grund and "pip install" in grund


def test_sitzung_meldet_den_fuellstand(app, hfe):
    sitzung = fake_session(hfe, read_ahead=True)
    try:
        assert _warte(lambda: (sitzung.stats().tracks_known or 0) > 3)
        text = sitzung.status_text()
        assert "von" in text and "Spuren gelesen" in text
    finally:
        sitzung.close()


# ════════════════════════════════════════════════════════════════════════════
# Emulator
# ════════════════════════════════════════════════════════════════════════════


def test_emulator_legt_eine_physische_diskette_ein_und_zeigt_sie_an(app, hfe,
                                                                    monkeypatch):
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    sitzung = fake_session(hfe, read_ahead=True, for_emulator=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: {
                            "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                            "num_heads": 2, "writable": False, "read_ahead": True}))

    emu = K1520Emulator()
    w = DriveWidget(emu)
    panel = w._panels[0]
    panel._phys_btn.click()

    assert 0 in w._physical, "die Sitzung wurde nicht gemerkt"
    assert "Greaseweazle" in panel._path_display.text()
    assert panel._toggle_btn.text() == "Auswerfen"
    assert not panel._phys_btn.isEnabled(), "zweimal einlegen darf nicht gehen"

    w._update_physical_status(0)
    assert "Spuren gelesen" in panel._phys_label.text()
    # isVisible() waere hier immer False — das Fenster wird nie gezeigt.
    assert not panel._phys_label.isHidden()

    # Auswerfen beendet die Sitzung UND hängt aus.
    panel._toggle_btn.click()
    assert 0 not in w._physical
    assert panel._toggle_btn.text() == "Mount"
    assert panel._phys_btn.isEnabled()
    assert panel._phys_label.isHidden()


def test_emulator_merkt_sich_kein_physisches_laufwerk_in_der_konfiguration(
        app, hfe, monkeypatch):
    """Eine physische Diskette hat keinen Pfad — sie darf nicht in die Config.

    Sonst stünde beim nächsten Start ein Eintrag da, den niemand einlösen kann
    (das Sync-Handle ist nach dem Einlegen verbraucht).
    """
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    sitzung = fake_session(hfe, for_emulator=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: {
                            "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                            "num_heads": 2, "writable": False, "read_ahead": False}))

    emu = K1520Emulator()
    w = DriveWidget(emu)
    w._panels[0]._phys_btn.click()
    try:
        assert w.get_mounts() == []
        w.remount_all()          # darf die Sitzung nicht anfassen
        assert 0 in w._physical
    finally:
        w.close_physical_sessions()
    assert w._physical == {}


# ════════════════════════════════════════════════════════════════════════════
# k1520DiskTool
# ════════════════════════════════════════════════════════════════════════════


def test_disktool_oeffnet_ueber_die_oberflaeche_und_zeigt_die_dateien(
        app, hfe, monkeypatch):
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a", num_cyls=sitzung.sync.stats.tracks_total // 2,
                                     num_heads=2), "physisches Öffnen scheiterte"
        assert fenster.tool is not None
        assert fenster.tool.filesystem
        assert fenster.tool.list(), "kein Verzeichnis gelesen"
        assert "Greaseweazle" in fenster.disk_view.kopf.text()
        assert fenster.tool.read_only, "ohne Schreibrecht muss es schreibgeschützt sein"
    finally:
        fenster._close_tool()
    assert fenster._physisch is None, "die Sitzung wurde nicht beendet"


def test_disktool_schliesst_die_sitzung_beim_naechsten_oeffnen(app, hfe, tmp_path,
                                                               monkeypatch):
    """Ein Abbild nach einer physischen Diskette darf das Laufwerk nicht halten."""
    import shutil

    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    kopie = tmp_path / "kopie.hfe"
    shutil.copy(hfe, kopie)

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        assert fenster._physisch is sitzung
        assert fenster.open_image(kopie)
        assert fenster._physisch is None, "das echte Laufwerk blieb belegt"
    finally:
        fenster._close_tool()
