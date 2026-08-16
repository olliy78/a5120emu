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
        assert "Greaseweazle" in fenster.kopf.pfad.text()
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


# ════════════════════════════════════════════════════════════════════════════
# Schadstelle: Meldung und Ausweg
# ════════════════════════════════════════════════════════════════════════════


def _mit_schadstelle(hfe, **kw):
    """Sitzung, deren Diskette nichts mehr annimmt (alle Spuren schadhaft)."""
    sitzung = fake_session(hfe, writable=True, read_ahead=False, **kw)
    sitzung.device.schadhaft = {(c, h) for c in range(sitzung.device.num_cyls)
                                for h in range(sitzung.device.num_heads)}
    return sitzung


def test_disktool_meldet_die_schadstelle_beim_speichern(app, hfe, tmp_path,
                                                        monkeypatch):
    """Der ganze Weg durch die Oberfläche: schreiben → prüfen → Warnung → Ausweg.

    Die Diskette nimmt nichts mehr an; der Verify-Lauf des Gastsystems liefe hier
    gegen das Speicherabbild und sähe nichts.  Das Prüf-Lesen sieht es.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, writable=True, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    # Warnungen abfangen statt anzeigen (headless gäbe es sonst ein modales Fenster).
    gemeldet = []
    monkeypatch.setattr("app.disktool.ui.main_window.QMessageBox.warning",
                        lambda parent, titel, text, *a, **k: gemeldet.append(text))
    monkeypatch.setattr("app.disktool.ui.main_window.QMessageBox.information",
                        lambda parent, titel, text, *a, **k: None)

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a", writable=True)
        assert fenster.act_neu_beschreiben.isVisible(), "Ausweg fehlt"
        assert fenster.act_neu_beschreiben.isEnabled(), "Ausweg gesperrt"

        # Ab jetzt trägt die Diskette nicht mehr.
        sitzung.device.schadhaft = {(c, h) for c in range(sitzung.device.num_cyls)
                                    for h in range(sitzung.device.num_heads)}

        quelle = tmp_path / "PROBE.TXT"
        quelle.write_bytes(b"schadstelle\r\n" * 4)
        ziel = ("Side0/" if fenster.tool.volume_count > 1 else "") + "PROBE.TXT"
        fenster.tool.insert(quelle, ziel, overwrite=True)

        assert fenster.save() is False, "der Schaden wurde als Erfolg verbucht"
        assert gemeldet, "es wurde nicht gewarnt"
        assert "Spur" in gemeldet[0]
        assert "neu beschreiben" in gemeldet[0]
        assert sitzung.stats().tracks_defect > 0
    finally:
        fenster._close_tool()


def test_defektmeldung_erscheint_nur_einmal_je_spur(app, hfe, monkeypatch):
    """Der Zeitgeber fragt zehnmal je Sekunde — melden darf er trotzdem nur einmal."""
    sitzung = fake_session(hfe, writable=True, read_ahead=False)
    try:
        # Zwei Schadstellen vortäuschen, ohne echten Schreibweg.
        folge = ["5/1", "5/1", "5/1, 12/0", "5/1, 12/0"]
        monkeypatch.setattr(type(sitzung), "defect_tracks",
                            property(lambda self: folge.pop(0) if folge else "5/1, 12/0"))

        assert sitzung.neue_defekte() == "5/1"          # erste Meldung
        assert sitzung.neue_defekte() == ""             # unverändert → still
        assert sitzung.neue_defekte() == "5/1, 12/0"    # neue Spur → wieder melden
        assert sitzung.neue_defekte() == ""
    finally:
        sitzung.close()


def test_die_meldung_sagt_was_zu_tun_ist(app, hfe):
    sitzung = fake_session(hfe)
    try:
        text = sitzung.defekt_meldung("5/1")
        assert "Spur 5/1" in text
        assert "Speichern unter" in text, "der Weg in eine Datei fehlt"
        assert "neu beschreiben" in text, "der Weg auf eine neue Diskette fehlt"
        assert "unversehrt" in text, "der Bediener muss wissen, dass nichts verloren ist"
    finally:
        sitzung.close()


def test_emulator_zeigt_den_rettungsknopf_erst_beim_schreiben(app, hfe, monkeypatch):
    """Ohne Schreibrecht gibt es nichts zurückzuschreiben — der Knopf bleibt weg."""
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    for schreibbar in (False, True):
        sitzung = fake_session(hfe, writable=schreibbar, for_emulator=True)
        monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                            classmethod(lambda cls, **kw: sitzung))
        monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                            classmethod(lambda cls, parent=None, **kw: {
                                "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                                "num_heads": 2, "writable": schreibbar,
                                "read_ahead": False}))
        w = DriveWidget(K1520Emulator())
        panel = w._panels[0]
        try:
            panel._phys_btn.click()
            assert panel._rewrite_btn.isHidden() != schreibbar, (
                f"Ausweg falsch sichtbar (schreibbar={schreibbar})")
        finally:
            w.close_physical_sessions()


def test_neu_beschreiben_stellt_die_bekannten_spuren_ein(app, hfe):
    """Der Ausweg selbst: alles Bekannte noch einmal wegschreiben."""
    sitzung = fake_session(hfe, writable=True, read_ahead=True)
    try:
        assert _warte(lambda: (sitzung.stats().tracks_known or 0) > 5)
        # Vorauslesen ANHALTEN, bevor gezählt wird: sonst kommen zwischen dem Ablesen
        # und dem rewrite_all() weitere Spuren herein und die Zahlen weichen ab
        # (unter Last durchaus zu beobachten).
        sitzung.sync.set_read_ahead(False)
        time.sleep(0.2)                       # laufenden Leseauftrag auslaufen lassen
        bekannt = sitzung.stats().tracks_known
        assert sitzung.rewrite_all() == bekannt
        # Und der Vermerk „schon gemeldet" ist zurückgesetzt — auf der neuen
        # Diskette gilt nichts von der alten.
        assert sitzung.gemeldete_defekte == ""
    finally:
        sitzung.close()


def test_das_pruef_lesen_steht_in_der_statuszeile(app, hfe):
    from app.gw import Stats

    sitzung = fake_session(hfe, read_ahead=False)
    try:
        # busy_kind 4 = Verify; der Bediener soll „prüft" lesen, nicht „arbeitet an".
        original = type(sitzung).stats
        werte = Stats(tracks_total=160, tracks_known=8, tracks_dirty=1, tracks_failed=0,
                      tracks_defect=0, reads_done=8, writes_done=1, verifies_done=0,
                      verify_failed=0, errors=0, busy_kind=4, busy_cyl=5, busy_head=1,
                      stopped=False)
        type(sitzung).stats = lambda self: werte
        try:
            text = sitzung.status_text()
            assert "prüft 5/1" in text
            assert "1 zu schreiben" in text
        finally:
            type(sitzung).stats = original
    finally:
        sitzung.close()
