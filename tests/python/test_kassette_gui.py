"""Kassette in der Oberfläche (doc/design/29_kassette.md, AP K9).

Das Kästchen *Einstellungen ▸ Allgemein ▸ Kassette (K5221, AKB K5020)* gibt es in jedem
Programm außer dem P8000 (Vorgabe aus); es steckt die K5020 über einen Neuaufbau der
Maschine.  Der Kasten „Kassette“ erscheint nur mit gesteckter Karte.  Dateidialoge werden
umgangen (``einlegen(pfad)``) — ein ``exec()`` stünde headless bis zum Zeitüberlauf.
"""

from pathlib import Path

import pytest
import yaml

from conftest import requires_core

pytestmark = requires_core

MASCHINEN = ["a5120", "k8915", "prg710", "pc1715"]


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, maschine="a5120"):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _menue(w, titel):
    for a in w.menuBar().actions():
        if a.text() == titel:
            return list(a.menu().actions())
    raise AssertionError(f"kein Menü {titel}")


def _stecken(w, qapp, an=True):
    w.settings_widget.tape_box.setChecked(an)
    qapp.processEvents()


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_vorgabe_aus_und_kasten_folgt_der_karte(qapp, konfig_ordner, maschine):
    w = _fenster(qapp, maschine)
    try:
        box = w.settings_widget.tape_box
        assert box.text() == "Kassette (K5221, AKB K5020)"
        assert not box.isChecked() and not w.emulator.tape_installed()
        assert not w.kassette_dock.isVisible()
        assert not w.act_dock_kassette.isVisible()
        assert w.act_dock_kassette in _menue(w, "&Ansicht")

        alt = w.emulator
        _stecken(w, qapp)
        assert w.emulator is not alt and w.emulator.tape_installed()
        assert w.kassette_dock.isVisible() and w.act_dock_kassette.isVisible()
        assert [g.title() for g in w.kassette_widget.laufwerke] == ["Laufwerk 1", "Laufwerk 2"]
        w._autosave_now()
        daten = yaml.safe_load(Path(w._konfig_pfad()).read_text(encoding="utf-8"))
        assert daten["general"]["tape"] is True

        _stecken(w, qapp, False)
        assert not w.emulator.tape_installed()
        assert not w.kassette_dock.isVisible() and not w.act_dock_kassette.isVisible()
    finally:
        _zu(w, qapp)


def test_p8000_hat_keine_kassette(qapp, konfig_ordner):
    from app import profil
    assert not profil.profil("p8000").tape_wahl


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_auslieferung_hat_die_karte_aus(qapp, konfig_ordner, maschine):
    from app import config_io, profil
    vorgabe = config_io.standard_konfiguration(profil.profil(maschine))
    assert vorgabe.get("general", {}).get("tape", False) is False
    assert "kassette" not in vorgabe


def test_keine_kuerzel(qapp, konfig_ordner):
    from PySide6.QtGui import QAction
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        for a in w.kassette_widget.findChildren(QAction):
            assert a.shortcut().isEmpty(), a.text()
        assert w.act_dock_kassette.shortcut().isEmpty()
    finally:
        _zu(w, qapp)


def test_bedienung_einlegen_wenden_schutz_auswerfen(qapp, konfig_ordner, tmp_path):
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        lw = w.kassette_widget.laufwerke[0]
        band = tmp_path / "a.k52"
        assert lw.einlegen(str(band))                 # fehlende Datei = leere Kassette
        st = w.emulator.tape_status(0)
        assert st["loaded"] and st["file"] == str(band) and band.exists()
        assert lw.datei.text() == str(band) and lw.seite.text() == "A"
        assert lw.auswerfen_knopf.isEnabled()

        assert lw.wenden() and lw.seite.text() == "B"
        lw.schutz[1].setChecked(True)
        assert w.emulator.tape_protected(0, 1) is True
        assert w.emulator.tape_protected(0, 0) is False

        assert lw.auswerfen() and not w.emulator.tape_status(0)["loaded"]
        assert not lw.auswerfen_knopf.isEnabled()
        assert lw.neu() and w.emulator.tape_status(0)["loaded"]
        assert w.emulator.tape_status(0)["file"] == ""
    finally:
        _zu(w, qapp)


def test_wiederherstellung_aus_der_konfiguration(qapp, konfig_ordner, tmp_path):
    band = tmp_path / "b.k52"
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        lw = w.kassette_widget.laufwerke[1]
        assert lw.einlegen(str(band))
        lw.schutz[0].setChecked(True)
    finally:
        _zu(w, qapp)                                  # schreibt die Konfiguration
    daten = yaml.safe_load(Path(w._konfig_pfad()).read_text(encoding="utf-8"))
    assert daten["general"]["tape"] is True
    assert daten["kassette"]["laufwerk1"] is None
    assert daten["kassette"]["laufwerk2"] == {"datei": str(band), "schutz_a": True,
                                              "schutz_b": False}

    w = _fenster(qapp)
    try:
        assert w.emulator.tape_installed() and w.settings_widget.tape_box.isChecked()
        assert w.kassette_dock.isVisible()
        st = w.emulator.tape_status(1)
        assert st["loaded"] and st["file"] == str(band)
        assert w.emulator.tape_protected(1, 0) is True
        assert not w.emulator.tape_status(0)["loaded"]
        # Aus- und wieder Einstecken: die Datei ist nicht vergessen.
        _stecken(w, qapp, False)
        _stecken(w, qapp, True)
        assert w.emulator.tape_status(1)["file"] == str(band)
    finally:
        _zu(w, qapp)

    # Eine fehlende Datei entsteht leer neu (anders als beim Lochstreifen).
    band.unlink()
    w = _fenster(qapp)
    try:
        assert w.emulator.tape_status(1)["loaded"] and band.exists()
    finally:
        _zu(w, qapp)
